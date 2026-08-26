#pragma once

#include "core/types/CaptureTypes.hpp"
#include "vision/recognition/ITextRecognizer.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace loupe {

struct RecognitionJob {
    GenerationId generation{};
    std::shared_ptr<const RegionSnapshot> region;
    RecognitionOptions options;
};

class RecognitionScheduler {
public:
    using ResultCallback = std::function<void(OcrResult)>;

    RecognitionScheduler(std::unique_ptr<ITextRecognizer> recognizer, ResultCallback callback);
    ~RecognitionScheduler();

    RecognitionScheduler(const RecognitionScheduler&) = delete;
    RecognitionScheduler& operator=(const RecognitionScheduler&) = delete;

    void submit(RecognitionJob job);
    void cancelBefore(GenerationId generation) noexcept;
    [[nodiscard]] size_t pendingCount() const;

private:
    void run(std::stop_token stop);

    std::unique_ptr<ITextRecognizer> recognizer_;
    ResultCallback callback_;
    mutable std::mutex mutex_;
    std::condition_variable_any available_;
    std::optional<RecognitionJob> pending_;
    std::atomic<GenerationId> latest_{0};
    std::jthread worker_;
};

} // namespace loupe

