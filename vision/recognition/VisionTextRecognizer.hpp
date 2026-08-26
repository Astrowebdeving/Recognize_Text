#pragma once

#include "vision/recognition/ITextRecognizer.hpp"

namespace loupe {

class VisionTextRecognizer final : public ITextRecognizer {
public:
    OcrResult recognize(const PixelBuffer& image,
                        const RecognitionOptions& options,
                        std::stop_token stop) override;
};

} // namespace loupe

