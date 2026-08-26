#pragma once

#include "vision/recognition/ITextRecognizer.hpp"

namespace loupe {

class VisionTextRecognizer final : public ITextRecognizer {
public:
    OcrResult recognize(const PixelBuffer& image,
                        const RecognitionOptions& options,
                        CancellationToken cancellation) override;
};

} // namespace loupe
