#include "core/jobs/RecognitionScheduler.hpp"

#include <exception>

namespace loupe {

RecognitionScheduler::RecognitionScheduler(std::unique_ptr<ITextRecognizer> recognizer,
                                           ResultCallback callback)
    : recognizer_(std::move(recognizer)), callback_(std::move(callback)),
      worker_([this](std::stop_token stop) { run(stop); }) {}

RecognitionScheduler::~RecognitionScheduler() {
    worker_.request_stop();
    available_.notify_all();
}

void RecognitionScheduler::submit(RecognitionJob job) {
    if (!job.region || !job.region->originalPixels) return;
    {
        std::scoped_lock lock(mutex_);
        const auto latest = latest_.load(std::memory_order_relaxed);
        if (job.generation < latest) return;
        latest_.store(job.generation, std::memory_order_release);
        pending_ = std::move(job); // Bounded queue: one pending, newest replaces oldest.
    }
    available_.notify_one();
}

void RecognitionScheduler::cancelBefore(GenerationId generation) noexcept {
    std::scoped_lock lock(mutex_);
    const auto cutoff = std::max(generation, latest_.load(std::memory_order_relaxed));
    latest_.store(cutoff, std::memory_order_release);
    if (pending_ && pending_->generation < cutoff) pending_.reset();
}

size_t RecognitionScheduler::pendingCount() const {
    std::scoped_lock lock(mutex_);
    return pending_.has_value() ? 1U : 0U;
}

void RecognitionScheduler::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::optional<RecognitionJob> job;
        {
            std::unique_lock lock(mutex_);
            available_.wait(lock, stop, [this] { return pending_.has_value(); });
            if (stop.stop_requested()) break;
            job = std::move(pending_);
            pending_.reset();
        }
        if (!job || !recognizer_) continue;
        if (job->generation != latest_.load(std::memory_order_acquire)) continue;
        OcrResult result;
        try {
            result = recognizer_->recognize(*job->region->originalPixels, job->options, stop);
        } catch (const std::exception& error) {
            result.error = std::string("Recognition failed: ") + error.what();
        } catch (...) {
            result.error = "Recognition failed with an unknown error";
        }
        result.generation = job->generation;
        if (!stop.stop_requested() && job->generation == latest_.load(std::memory_order_acquire)) {
            if (callback_) {
                try {
                    callback_(std::move(result));
                } catch (...) {
                    // A client callback must not terminate the worker thread or process.
                }
            }
        }
    }
}

} // namespace loupe
