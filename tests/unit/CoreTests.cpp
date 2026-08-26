#include "core/coordinates/Coordinates.hpp"
#include "core/jobs/RecognitionScheduler.hpp"
#include "core/pipeline/ImageProcessing.hpp"
#include "core/state/CaptureCoordinator.hpp"
#include "vision/accounting/AccountingValidator.hpp"
#include "vision/layout/ReaderFormatter.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool value, const char* expression, const char* file, int line) {
    if (value) return;
    std::cerr << file << ':' << line << " check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

std::shared_ptr<const loupe::PixelBuffer> pixels(int width = 4, int height = 3) {
    auto result = std::make_shared<loupe::PixelBuffer>();
    result->width = width;
    result->height = height;
    result->bytesPerRow = width * 4;
    result->format = loupe::PixelFormat::Bgra8;
    result->bytes.resize(static_cast<size_t>(result->bytesPerRow) * static_cast<size_t>(height));
    for (size_t i = 0; i < result->bytes.size(); ++i)
        result->bytes[i] = static_cast<std::byte>(i & 0xFFU);
    return result;
}

std::shared_ptr<const loupe::RegionSnapshot> region(loupe::GenerationId generation) {
    auto result = std::make_shared<loupe::RegionSnapshot>();
    result->generation = generation;
    result->originalPixels = pixels();
    result->userRect = {0, 0, 4, 3};
    return result;
}

void testCoordinates() {
    using loupe::PixelRect;
    const auto intersection = loupe::coordinates::intersect({-10, 10, 40, 30}, {0, 0, 20, 20});
    CHECK(intersection.x == 0);
    CHECK(intersection.y == 10);
    CHECK(intersection.width == 20);
    CHECK(intersection.height == 10);

    const auto physical = loupe::coordinates::logicalToPhysical(
        PixelRect{150, 50, 100, 100}, PixelRect{100, 0, 1000, 500}, PixelRect{-2000, 0, 2000, 1000});
    CHECK(physical.x == -1900);
    CHECK(physical.y == 100);
    CHECK(physical.width == 200);
    CHECK(physical.height == 200);
}

void testCropPreservesOriginal() {
    loupe::DesktopFrame frame;
    frame.physicalRect = {-2, -1, 4, 3};
    frame.pixels = pixels();
    const auto result = loupe::image::crop(frame, {-1, 0, 2, 2});
    CHECK(result != nullptr);
    CHECK(result->width == 2);
    CHECK(result->height == 2);
    CHECK(result->bytes[0] == frame.pixels->bytes[20]);

    const auto enhanced = loupe::image::documentEnhance(*result);
    CHECK(enhanced != nullptr);
    CHECK(enhanced->format == loupe::PixelFormat::Gray8);
    CHECK(result->format == loupe::PixelFormat::Bgra8);

    loupe::DesktopFrame malformedFrame;
    malformedFrame.physicalRect = {10, 20, 4'000, 3'000};
    malformedFrame.pixels = pixels(4, 3);
    const auto bounded = loupe::image::crop(malformedFrame, malformedFrame.physicalRect);
    CHECK(bounded != nullptr);
    CHECK(bounded->width == 4);
    CHECK(bounded->height == 3);
}

void testGenerationGate() {
    loupe::CaptureCoordinator coordinator;
    const auto first = coordinator.publish(region(0));
    const auto second = coordinator.publish(region(0));
    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(!coordinator.publishOcrGeneration(first));
    CHECK(coordinator.publishOcrGeneration(second));
    CHECK(coordinator.region()->generation == second);
}

void testAccountingGrammar() {
    loupe::AccountingValidator validator;
    for (const auto* sample : {"1,803,762.18", "$1,803,762.18", "(1,803,762.18)",
                               "-1,803,762.18", "1 803 762,18", "12.3%", "1,803 CR",
                               "€1.803,72"}) {
        const auto result = validator.assess(sample, 0.95F);
        CHECK(result.numericLike);
        CHECK(result.grammarValid);
    }
    CHECK(!validator.assess("Revenue", 0.9F).numericLike);
    const auto uncertain = validator.assess("18,603.72", 0.62F);
    CHECK(!uncertain.alternatives.empty());
    CHECK(uncertain.normalized == "18,603.72");
}

void testReaderGeometry() {
    loupe::OcrResult result;
    result.lines.resize(2);
    result.tokens.push_back({"Revenue", 0.95F, {0.0F, 0.1F, 0.2F, 0.1F, 0.2F, 0.2F, 0.0F, 0.2F},
                             {}, false, 0, std::nullopt});
    result.tokens.push_back({"18,302", 0.95F, {0.6F, 0.1F, 0.8F, 0.1F, 0.8F, 0.2F, 0.6F, 0.2F},
                             {}, true, 0, 1});
    result.tokens.push_back({"EBITDA", 0.95F, {0.0F, 0.4F, 0.2F, 0.4F, 0.2F, 0.5F, 0.0F, 0.5F},
                             {}, false, 1, std::nullopt});
    result.tokens.push_back({"4,211", 0.95F, {0.6F, 0.4F, 0.8F, 0.4F, 0.8F, 0.5F, 0.6F, 0.5F},
                             {}, true, 1, 1});
    loupe::ReaderFormatter formatter;
    const auto text = formatter.format(result);
    CHECK(text.find("Revenue") != std::string::npos);
    CHECK(text.find("18,302") != std::string::npos);
    CHECK(text.find('\n') != std::string::npos);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    result.tokens[0].sourceQuad = {nan, nan, nan, nan, nan, nan, nan, nan};
    CHECK(!formatter.format(result).empty());
    CHECK(loupe::confidenceBand(nan) == loupe::ConfidenceBand::Unknown);
}

class SlowRecognizer final : public loupe::ITextRecognizer {
public:
    loupe::OcrResult recognize(const loupe::PixelBuffer&, const loupe::RecognitionOptions&,
                               std::stop_token stop) override {
        for (int i = 0; i < 30 && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(1ms);
        loupe::OcrResult result;
        result.overallConfidence = 1.0F;
        return result;
    }
};

class ThrowingRecognizer final : public loupe::ITextRecognizer {
public:
    loupe::OcrResult recognize(const loupe::PixelBuffer&, const loupe::RecognitionOptions&,
                               std::stop_token) override {
        throw std::runtime_error("test recognizer failure");
    }
};

void testLatestWinsQueue() {
    std::mutex mutex;
    std::condition_variable ready;
    loupe::GenerationId published = 0;
    loupe::RecognitionScheduler scheduler(std::make_unique<SlowRecognizer>(),
        [&](loupe::OcrResult result) {
            {
                std::scoped_lock lock(mutex);
                published = result.generation;
            }
            ready.notify_one();
        });
    scheduler.submit({1, region(1), {}});
    std::this_thread::sleep_for(3ms);
    scheduler.submit({2, region(2), {}});
    scheduler.submit({3, region(3), {}});
    CHECK(scheduler.pendingCount() <= 1);
    std::unique_lock lock(mutex);
    ready.wait_for(lock, 1s, [&] { return published == 3; });
    CHECK(published == 3);
}

void testRecognitionExceptionBoundary() {
    std::mutex mutex;
    std::condition_variable ready;
    std::string error;
    loupe::RecognitionScheduler scheduler(std::make_unique<ThrowingRecognizer>(),
        [&](loupe::OcrResult result) {
            {
                std::scoped_lock lock(mutex);
                error = std::move(result.error);
            }
            ready.notify_one();
        });
    scheduler.submit({7, region(7), {}});
    std::unique_lock lock(mutex);
    CHECK(ready.wait_for(lock, 1s, [&] { return !error.empty(); }));
    CHECK(error.find("test recognizer failure") != std::string::npos);

    loupe::RecognitionScheduler callbackBoundary(std::make_unique<SlowRecognizer>(),
        [](loupe::OcrResult) { throw std::runtime_error("test callback failure"); });
    callbackBoundary.submit({8, region(8), {}});
    std::this_thread::sleep_for(50ms);
}

} // namespace

int main() {
    testCoordinates();
    testCropPreservesOriginal();
    testGenerationGate();
    testAccountingGrammar();
    testReaderGeometry();
    testLatestWinsQueue();
    testRecognitionExceptionBoundary();
    if (failures == 0) {
        std::cout << "All Document Loupe core tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
}
