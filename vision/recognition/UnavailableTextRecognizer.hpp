#pragma once

#include "vision/recognition/ITextRecognizer.hpp"

namespace loupe {

class UnavailableTextRecognizer final : public ITextRecognizer {
public:
    OcrResult recognize(const PixelBuffer&, const RecognitionOptions&, std::stop_token) override {
        OcrResult result;
        result.engine = "Unavailable";
        result.error = "OCR models are unavailable; raw magnification remains active";
        return result;
    }
};

} // namespace loupe

