#include "vision/recognition/WindowsTextRecognizer.hpp"

#include <Windows.h>
#include <appmodel.h>
#include <robuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace loupe {

namespace {

winrt::Windows::Graphics::Imaging::SoftwareBitmap makeBitmap(const PixelBuffer& image) {
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Storage::Streams;
    const auto format = image.format == PixelFormat::Gray8
        ? BitmapPixelFormat::Gray8 : BitmapPixelFormat::Bgra8;
    const auto alpha = image.format == PixelFormat::Gray8
        ? BitmapAlphaMode::Ignore : BitmapAlphaMode::Premultiplied;
    SoftwareBitmap bitmap(format, image.width, image.height, alpha);

    const auto packedStride = static_cast<size_t>(image.width)
                            * static_cast<size_t>(image.bytesPerPixel());
    const auto rows = static_cast<size_t>(image.height);
    if (packedStride > std::numeric_limits<uint32_t>::max() / rows)
        winrt::throw_hresult(E_INVALIDARG);
    const auto packedSize = static_cast<uint32_t>(packedStride * rows);
    Buffer buffer(packedSize);
    buffer.Length(packedSize);
    uint8_t* target = nullptr;
    winrt::check_hresult(buffer.as<IBufferByteAccess>()->Buffer(&target));
    if (target == nullptr) winrt::throw_hresult(E_OUTOFMEMORY);
    for (int32_t y = 0; y < image.height; ++y) {
        const auto* source = image.bytes.data() + static_cast<size_t>(y) * image.bytesPerRow;
        auto* destination = target + static_cast<size_t>(y) * packedStride;
        if (image.format != PixelFormat::Rgba8) {
            std::memcpy(destination, source, static_cast<size_t>(packedStride));
            continue;
        }
        for (int32_t x = 0; x < image.width; ++x) {
            const auto offset = static_cast<size_t>(x) * 4U;
            destination[offset] = std::to_integer<uint8_t>(source[offset + 2]);
            destination[offset + 1] = std::to_integer<uint8_t>(source[offset + 1]);
            destination[offset + 2] = std::to_integer<uint8_t>(source[offset]);
            destination[offset + 3] = std::to_integer<uint8_t>(source[offset + 3]);
        }
    }
    bitmap.CopyFromBuffer(buffer);
    return bitmap;
}

NormalizedQuad quadFor(const winrt::Windows::Foundation::Rect& rect,
                       int32_t width, int32_t height) {
    const auto left = std::clamp(rect.X / width, 0.0F, 1.0F);
    const auto right = std::clamp((rect.X + rect.Width) / width, 0.0F, 1.0F);
    const auto top = std::clamp(rect.Y / height, 0.0F, 1.0F);
    const auto bottom = std::clamp((rect.Y + rect.Height) / height, 0.0F, 1.0F);
    return {left, top, right, top, right, bottom, left, bottom};
}

} // namespace

OcrResult WindowsTextRecognizer::recognize(const PixelBuffer& image,
                                            const RecognitionOptions& options,
                                            CancellationToken cancellation) {
    (void)options;
    OcrResult result;
    result.engine = "Windows OCR (local fallback)";
    if (!image.valid() || cancellation.stopRequested()) {
        result.error = "No valid image was supplied";
        return result;
    }

    UINT32 packageNameLength = 0;
    const auto identityStatus = GetCurrentPackageFullName(&packageNameLength, nullptr);
    if (identityStatus == APPMODEL_ERROR_NO_PACKAGE) {
        result.error = "Windows OCR requires an MSIX-installed build; raw magnification remains active";
        return result;
    }
    if (identityStatus != ERROR_INSUFFICIENT_BUFFER) {
        result.error = "Windows package identity could not be verified; raw magnification remains active";
        return result;
    }

    try {
        thread_local const bool initialized = [] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return true;
        }();
        (void)initialized;
        const auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            result.error = "No compatible local OCR language is installed";
            return result;
        }
        const auto bitmap = makeBitmap(image);
        const auto recognized = engine.RecognizeAsync(bitmap).get();
        if (cancellation.stopRequested()) {
            result.error = "Recognition canceled";
            return result;
        }

        uint32_t lineIndex = 0;
        for (const auto& recognizedLine : recognized.Lines()) {
            OcrLine line;
            bool hasBounds = false;
            float minX = 1.0F, minY = 1.0F, maxX = 0.0F, maxY = 0.0F;
            for (const auto& word : recognizedLine.Words()) {
                OcrToken token;
                token.text = winrt::to_string(word.Text());
                token.confidence = 0.80F; // The Windows API exposes no calibrated word confidence.
                token.lineIndex = lineIndex;
                token.sourceQuad = quadFor(word.BoundingRect(), image.width, image.height);
                minX = std::min(minX, token.sourceQuad.x1);
                minY = std::min(minY, token.sourceQuad.y1);
                maxX = std::max(maxX, token.sourceQuad.x3);
                maxY = std::max(maxY, token.sourceQuad.y3);
                line.tokenIndices.push_back(result.tokens.size());
                result.tokens.push_back(std::move(token));
                hasBounds = true;
            }
            if (hasBounds) {
                line.sourceQuad = {minX, minY, maxX, minY, maxX, maxY, minX, maxY};
                result.lines.push_back(std::move(line));
                ++lineIndex;
            }
        }
        result.overallConfidence = result.tokens.empty() ? 0.0F : 0.80F;
    } catch (const winrt::hresult_error& error) {
        result.error = winrt::to_string(error.message());
    }
    return result;
}

} // namespace loupe
