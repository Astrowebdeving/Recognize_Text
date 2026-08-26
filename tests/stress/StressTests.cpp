#include "core/coordinates/Coordinates.hpp"
#include "core/jobs/RecognitionScheduler.hpp"
#include "core/pipeline/ImageProcessing.hpp"
#include "core/state/CaptureCoordinator.hpp"
#include "vision/accounting/AccountingValidator.hpp"
#include "vision/layout/ReaderFormatter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "STRESS FAILURE: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::shared_ptr<const loupe::PixelBuffer> makePixels(int32_t width, int32_t height,
                                                     loupe::PixelFormat format,
                                                     uint64_t seed) {
    auto pixels = std::make_shared<loupe::PixelBuffer>();
    pixels->width = width;
    pixels->height = height;
    pixels->format = format;
    pixels->bytesPerRow = width * pixels->bytesPerPixel();
    pixels->bytes.resize(static_cast<size_t>(pixels->bytesPerRow) * static_cast<size_t>(height));
    uint64_t value = seed | 1U;
    for (auto& byte : pixels->bytes) {
        value ^= value << 13U;
        value ^= value >> 7U;
        value ^= value << 17U;
        byte = static_cast<std::byte>(value & 0xFFU);
    }
    return pixels;
}

uint64_t checksum(const loupe::PixelBuffer& pixels) {
    uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : pixels.bytes) {
        hash ^= std::to_integer<uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::shared_ptr<const loupe::RegionSnapshot> makeRegion(loupe::GenerationId generation) {
    auto snapshot = std::make_shared<loupe::RegionSnapshot>();
    snapshot->generation = generation;
    snapshot->regionId = generation;
    snapshot->userRect = {0, 0, 32, 16};
    snapshot->originalPixels = makePixels(32, 16, loupe::PixelFormat::Bgra8, generation);
    return snapshot;
}

void stressGeometryAndCropping(std::mt19937_64& random) {
    loupe::DesktopFrame frame;
    frame.physicalRect = {-73, 41, 128, 96};
    frame.pixels = makePixels(128, 96, loupe::PixelFormat::Bgra8, 42);
    const auto originalChecksum = checksum(*frame.pixels);
    std::uniform_int_distribution<int32_t> x(-260, 220);
    std::uniform_int_distribution<int32_t> y(-180, 260);
    std::uniform_int_distribution<int32_t> extent(-20, 320);

    for (int iteration = 0; iteration < 50'000; ++iteration) {
        const loupe::PixelRect requested{x(random), y(random), extent(random), extent(random)};
        const auto expected = loupe::coordinates::intersect(requested, frame.physicalRect);
        const auto cropped = loupe::image::crop(frame, requested);
        if (expected.empty()) {
            require(!cropped, "empty intersection produced a crop");
        } else {
            require(cropped && cropped->valid(), "non-empty intersection produced invalid pixels");
            require(cropped->width == expected.width && cropped->height == expected.height,
                    "crop dimensions do not match intersection");
        }
        require(checksum(*frame.pixels) == originalChecksum, "crop mutated immutable source pixels");
    }

    const loupe::PixelRect extreme{std::numeric_limits<int32_t>::max() - 4, 0, 100, 10};
    require(extreme.right() > std::numeric_limits<int32_t>::max(), "rectangle edge overflowed");
    const auto safe = loupe::coordinates::intersect(extreme, extreme);
    require(safe.width == 100, "extreme rectangle intersection was corrupted");

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const loupe::NormalizedQuad malformed{nan, -50.0F, 4.0F, nan, 2.0F, 3.0F, -1.0F, 0.5F};
    const auto normalized = loupe::coordinates::normalizedBounds(malformed, {0, 0, 100, 100});
    require(normalized.x >= 0 && normalized.y >= 0 && normalized.right() <= 100 &&
            normalized.bottom() <= 100, "malformed normalized geometry escaped its target");

    loupe::PixelBuffer malicious;
    malicious.width = std::numeric_limits<int32_t>::max();
    malicious.height = 1;
    malicious.bytesPerRow = std::numeric_limits<int32_t>::max();
    malicious.format = loupe::PixelFormat::Bgra8;
    require(!malicious.valid(), "overflow-sized pixel buffer was accepted");

    loupe::DesktopFrame inconsistent;
    inconsistent.physicalRect = {-100, -100, 50'000, 50'000};
    inconsistent.pixels = makePixels(8, 6, loupe::PixelFormat::Bgra8, 17);
    const auto boundedCrop = loupe::image::crop(inconsistent, inconsistent.physicalRect);
    require(boundedCrop && boundedCrop->width == 8 && boundedCrop->height == 6,
            "crop trusted inconsistent frame metadata");
}

void stressEnhancement(std::mt19937_64& random) {
    std::uniform_int_distribution<int32_t> size(1, 96);
    std::uniform_int_distribution<int> format(0, 2);
    for (int iteration = 0; iteration < 5'000; ++iteration) {
        const auto width = size(random);
        const auto height = size(random);
        const auto pixelFormat = static_cast<loupe::PixelFormat>(format(random));
        const auto source = makePixels(width, height, pixelFormat, random());
        const auto before = checksum(*source);
        const auto enhanced = loupe::image::documentEnhance(*source);
        require(enhanced && enhanced->valid(), "document enhancement returned invalid pixels");
        require(enhanced->width == width && enhanced->height == height,
                "document enhancement changed geometry");
        require(enhanced->format == loupe::PixelFormat::Gray8,
                "document enhancement returned an unexpected format");
        require(checksum(*source) == before, "document enhancement mutated source pixels");
    }
}

void stressAccountingAndLayout(std::mt19937_64& random) {
    loupe::AccountingValidator validator;
    loupe::ReaderFormatter formatter;
    const std::string alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$€£¥.,-()[] %/\\\t";
    std::uniform_int_distribution<size_t> length(0, 96);
    std::uniform_int_distribution<size_t> character(0, alphabet.size() - 1);
    for (int iteration = 0; iteration < 100'000; ++iteration) {
        std::string text;
        text.reserve(96);
        for (size_t i = 0, count = length(random); i < count; ++i)
            text.push_back(alphabet[character(random)]);
        const auto assessment = validator.assess(text, static_cast<float>((random() % 101U) / 100.0));
        require(assessment.alternatives.size() <= 3, "accounting alternatives exceeded bound");

        if ((iteration % 100) == 0) {
            loupe::OcrResult result;
            const auto tokenCount = static_cast<size_t>(random() % 40U);
            result.lines.resize(1 + tokenCount / 5);
            for (size_t tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex) {
                const auto left = static_cast<float>((tokenIndex % 5U) * 0.19);
                const auto top = static_cast<float>((tokenIndex / 5U) * 0.1);
                result.tokens.push_back({text, 0.8F,
                    {left, top, left + 0.15F, top, left + 0.15F, top + 0.08F, left, top + 0.08F},
                    {}, false, static_cast<uint32_t>(tokenIndex / 5U), std::nullopt});
            }
            (void)formatter.format(result);
            (void)formatter.prefersOverlay(result);
        }
    }
}

void stressCoordinator() {
    loupe::CaptureCoordinator coordinator;
    constexpr int threadCount = 8;
    constexpr int publicationsPerThread = 5'000;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int thread = 0; thread < threadCount; ++thread) {
        threads.emplace_back([&, thread] {
            const auto sharedPixels = makePixels(8, 8, loupe::PixelFormat::Bgra8,
                                                 static_cast<uint64_t>(thread + 1));
            for (int publication = 0; publication < publicationsPerThread; ++publication) {
                auto snapshot = std::make_shared<loupe::RegionSnapshot>();
                snapshot->originalPixels = sharedPixels;
                snapshot->regionId = static_cast<uint64_t>(thread)
                                   * static_cast<uint64_t>(publicationsPerThread)
                                   + static_cast<uint64_t>(publication);
                coordinator.publish(std::move(snapshot));
            }
        });
    }
    for (auto& thread : threads) thread.join();
    threads.clear();
    const auto expected = static_cast<loupe::GenerationId>(threadCount * publicationsPerThread);
    require(coordinator.generation() == expected, "coordinator lost concurrent publications");
    const auto current = coordinator.region();
    require(current && current->generation == expected,
            "coordinator generation and current region diverged");
    require(coordinator.publishOcrGeneration(expected), "current generation was rejected");
    require(!coordinator.publishOcrGeneration(expected - 1), "stale generation was accepted");
}

class JitterRecognizer final : public loupe::ITextRecognizer {
public:
    loupe::OcrResult recognize(const loupe::PixelBuffer&,
                               const loupe::RecognitionOptions&,
                               loupe::CancellationToken cancellation) override {
        for (int spin = 0; spin < 4 && !cancellation.stopRequested(); ++spin)
            std::this_thread::sleep_for(50us);
        loupe::OcrResult result;
        result.overallConfidence = 0.8F;
        return result;
    }
};

void stressLatestWinsScheduler() {
    constexpr loupe::GenerationId sentinel = 1'000'000'000ULL;
    std::mutex mutex;
    std::condition_variable complete;
    loupe::GenerationId lastPublished{};
    {
        loupe::RecognitionScheduler scheduler(std::make_unique<JitterRecognizer>(),
            [&](loupe::OcrResult result) {
                std::scoped_lock lock(mutex);
                lastPublished = result.generation;
                complete.notify_all();
            });

        std::atomic<loupe::GenerationId> next{1};
        std::vector<std::thread> submitters;
        for (int thread = 0; thread < 8; ++thread) {
            submitters.emplace_back([&] {
                for (int job = 0; job < 2'000; ++job) {
                    const auto generation = next.fetch_add(1, std::memory_order_relaxed);
                    scheduler.submit({generation, makeRegion(generation), {}});
                    require(scheduler.pendingCount() <= 1, "recognition queue became unbounded");
                }
            });
        }
        for (auto& submitter : submitters) submitter.join();
        submitters.clear();
        scheduler.submit({sentinel, makeRegion(sentinel), {}});
        {
            std::unique_lock lock(mutex);
            require(complete.wait_for(lock, 5s, [&] { return lastPublished == sentinel; }),
                    "latest recognition result was not published");
        }
        std::this_thread::sleep_for(10ms);
        {
            std::scoped_lock lock(mutex);
            require(lastPublished == sentinel, "a stale result replaced the sentinel result");
        }
    }

    for (int lifecycle = 0; lifecycle < 200; ++lifecycle) {
        loupe::RecognitionScheduler scheduler(std::make_unique<JitterRecognizer>(),
                                               [](loupe::OcrResult) {});
        for (loupe::GenerationId generation = 1; generation <= 50; ++generation)
            scheduler.submit({generation, makeRegion(generation), {}});
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const auto started = std::chrono::steady_clock::now();
    const bool concurrencyOnly = argc > 1 && std::string_view(argv[1]) == "--concurrency-only";
    std::mt19937_64 random(0xD0C0A11E5EEDULL);
    if (!concurrencyOnly) {
        stressGeometryAndCropping(random);
        stressEnhancement(random);
        stressAccountingAndLayout(random);
    }
    stressCoordinator();
    stressLatestWinsScheduler();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "Document Loupe stress suite passed in " << elapsed.count() << " ms\n";
    return EXIT_SUCCESS;
}
