#pragma once

#include "core/types/OcrTypes.hpp"
#include "core/types/PixelBuffer.hpp"

#include <atomic>

namespace loupe {

class CancellationToken {
public:
    CancellationToken() noexcept = default;
    explicit CancellationToken(const std::atomic_bool& stopped) noexcept
        : stopped_(&stopped) {}

    [[nodiscard]] bool stopRequested() const noexcept {
        return stopped_ != nullptr && stopped_->load(std::memory_order_acquire);
    }

private:
    const std::atomic_bool* stopped_{};
};

enum class RecognitionQuality {
    Fast,
    Balanced,
    Accurate
};

struct RecognitionOptions {
    RecognitionQuality quality{RecognitionQuality::Balanced};
    bool accountingHints{true};
};

class ITextRecognizer {
public:
    virtual ~ITextRecognizer() = default;
    virtual OcrResult recognize(const PixelBuffer& image,
                                const RecognitionOptions& options,
                                CancellationToken cancellation) = 0;
};

} // namespace loupe
