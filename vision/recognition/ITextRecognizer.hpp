#pragma once

#include "core/types/OcrTypes.hpp"
#include "core/types/PixelBuffer.hpp"

#include <stop_token>

namespace loupe {

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
                                std::stop_token stop) = 0;
};

} // namespace loupe

