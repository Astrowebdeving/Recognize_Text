#include "vision/recognition/VisionTextRecognizer.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <numeric>

namespace loupe {

namespace {

std::string errorMessage(NSError* error, const char* fallback) {
    if (error == nil) return fallback;
    const char* utf8 = error.localizedDescription.UTF8String;
    return utf8 == nullptr ? std::string(fallback) : std::string(utf8);
}

} // namespace

OcrResult VisionTextRecognizer::recognize(const PixelBuffer& image,
                                          const RecognitionOptions& options,
                                          CancellationToken cancellation) {
    OcrResult result;
    result.engine = "Apple Vision (local fallback)";
    if (!image.valid() || cancellation.stopRequested()) {
        result.error = "No valid image was supplied";
        return result;
    }

    @autoreleasepool {
        CGColorSpaceRef colorSpace = image.format == PixelFormat::Gray8
            ? CGColorSpaceCreateDeviceGray() : CGColorSpaceCreateDeviceRGB();
        if (colorSpace == nullptr) {
            result.error = "Could not allocate an OCR color space";
            return result;
        }
        const auto bitmapInfo = image.format == PixelFormat::Bgra8
            ? static_cast<CGBitmapInfo>(static_cast<uint32_t>(kCGBitmapByteOrder32Little) |
                                        static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst))
            : static_cast<CGBitmapInfo>(static_cast<uint32_t>(kCGBitmapByteOrder32Big) |
                                        static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
        CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, image.bytes.data(),
                                                                  image.bytes.size(), nullptr);
        if (provider == nullptr) {
            CGColorSpaceRelease(colorSpace);
            result.error = "Could not allocate an OCR data provider";
            return result;
        }
        CGImageRef cgImage = nullptr;
        const auto width = static_cast<size_t>(image.width);
        const auto height = static_cast<size_t>(image.height);
        const auto bytesPerRow = static_cast<size_t>(image.bytesPerRow);
        if (image.format == PixelFormat::Gray8) {
            cgImage = CGImageCreate(width, height, 8, 8, bytesPerRow,
                                    colorSpace, kCGImageAlphaNone, provider, nullptr, false,
                                    kCGRenderingIntentDefault);
        } else {
            cgImage = CGImageCreate(width, height, 8, 32, bytesPerRow,
                                    colorSpace, bitmapInfo, provider, nullptr, false,
                                    kCGRenderingIntentDefault);
        }
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);
        if (cgImage == nullptr) {
            result.error = "Could not create an OCR image";
            return result;
        }

        VNRecognizeTextRequest* request = [VNRecognizeTextRequest new];
        request.recognitionLevel = options.quality == RecognitionQuality::Fast
            ? VNRequestTextRecognitionLevelFast : VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = options.quality != RecognitionQuality::Fast;
        request.minimumTextHeight = 0.01F;
        VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
        NSError* error = nil;
        const BOOL performed = [handler performRequests:@[ request ] error:&error];
        CGImageRelease(cgImage);
        if (!performed || error != nil || cancellation.stopRequested()) {
            result.error = cancellation.stopRequested() ? "Recognition canceled"
                                                 : errorMessage(error, "Apple Vision recognition failed");
            return result;
        }

        uint32_t lineIndex = 0;
        float confidenceSum = 0.0F;
        for (VNRecognizedTextObservation* observation in request.results) {
            VNRecognizedText* candidate = [observation topCandidates:1].firstObject;
            if (candidate == nil) continue;
            const char* candidateText = candidate.string.UTF8String;
            if (candidateText == nullptr) continue;
            const auto box = observation.boundingBox;
            OcrToken token;
            token.text = candidateText;
            token.confidence = candidate.confidence;
            token.lineIndex = lineIndex++;
            const auto left = static_cast<float>(box.origin.x);
            const auto right = static_cast<float>(box.origin.x + box.size.width);
            const auto top = static_cast<float>(1.0 - box.origin.y - box.size.height);
            const auto bottom = static_cast<float>(1.0 - box.origin.y);
            token.sourceQuad = {left, top, right, top, right, bottom, left, bottom};
            result.tokens.push_back(token);
            OcrLine line;
            line.tokenIndices.push_back(result.tokens.size() - 1);
            line.sourceQuad = token.sourceQuad;
            result.lines.push_back(std::move(line));
            confidenceSum += token.confidence;
        }
        result.overallConfidence = result.tokens.empty()
            ? 0.0F : confidenceSum / static_cast<float>(result.tokens.size());
    }
    return result;
}

} // namespace loupe
