#include "core/pipeline/ImageProcessing.hpp"

#include "core/coordinates/Coordinates.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace loupe::image {

std::shared_ptr<const PixelBuffer> crop(const DesktopFrame& frame, PixelRect desktopRect) {
    if (!frame.pixels || !frame.pixels->valid()) return {};
    const auto& source = *frame.pixels;
    // A capture backend should report matching frame and pixel dimensions, but
    // do not trust that invariant at a raw memory-copy boundary.
    const PixelRect pixelExtent{frame.physicalRect.x, frame.physicalRect.y,
                                source.width, source.height};
    const auto available = coordinates::intersect(frame.physicalRect, pixelExtent);
    const auto clipped = coordinates::intersect(desktopRect, available);
    if (clipped.empty()) return {};

    const auto bpp = source.bytesPerPixel();
    if (clipped.width > std::numeric_limits<int32_t>::max() / bpp) return {};
    const auto rowBytes = static_cast<size_t>(clipped.width) * static_cast<size_t>(bpp);
    const auto rows = static_cast<size_t>(clipped.height);
    if (rowBytes > std::numeric_limits<size_t>::max() / rows) return {};
    auto result = std::make_shared<PixelBuffer>();
    result->width = clipped.width;
    result->height = clipped.height;
    result->bytesPerRow = static_cast<int32_t>(rowBytes);
    result->format = source.format;
    result->bytes.resize(rowBytes * rows);

    const auto localX = static_cast<size_t>(
        static_cast<int64_t>(clipped.x) - frame.physicalRect.x);
    const auto localY = static_cast<size_t>(
        static_cast<int64_t>(clipped.y) - frame.physicalRect.y);
    const auto sourceStride = static_cast<size_t>(source.bytesPerRow);
    for (int32_t y = 0; y < clipped.height; ++y) {
        const auto row = static_cast<size_t>(y);
        const auto sourceOffset = (localY + row) * sourceStride
                                + localX * static_cast<size_t>(bpp);
        const auto targetOffset = row * rowBytes;
        if (sourceOffset > source.bytes.size() ||
            rowBytes > source.bytes.size() - sourceOffset) return {};
        std::memcpy(result->bytes.data() + targetOffset,
                    source.bytes.data() + sourceOffset,
                    rowBytes);
    }
    return result;
}

std::shared_ptr<const PixelBuffer> documentEnhance(const PixelBuffer& source) {
    if (!source.valid()) return {};
    auto result = std::make_shared<PixelBuffer>();
    result->width = source.width;
    result->height = source.height;
    result->bytesPerRow = source.width;
    result->format = PixelFormat::Gray8;
    const auto width = static_cast<size_t>(source.width);
    const auto height = static_cast<size_t>(source.height);
    if (width > std::numeric_limits<size_t>::max() / height) return {};
    const auto pixelCount = width * height;
    result->bytes.resize(pixelCount);

    std::vector<uint8_t> grayscale(pixelCount);
    uint8_t low = 255;
    uint8_t high = 0;
    for (int32_t y = 0; y < source.height; ++y) {
        for (int32_t x = 0; x < source.width; ++x) {
            const auto offset = static_cast<size_t>(y) * static_cast<size_t>(source.bytesPerRow)
                              + static_cast<size_t>(x) * static_cast<size_t>(source.bytesPerPixel());
            uint8_t gray{};
            if (source.format == PixelFormat::Gray8) {
                gray = std::to_integer<uint8_t>(source.bytes[offset]);
            } else {
                const auto c0 = std::to_integer<uint8_t>(source.bytes[offset]);
                const auto c1 = std::to_integer<uint8_t>(source.bytes[offset + 1]);
                const auto c2 = std::to_integer<uint8_t>(source.bytes[offset + 2]);
                const auto b = source.format == PixelFormat::Bgra8 ? c0 : c2;
                const auto g = c1;
                const auto r = source.format == PixelFormat::Bgra8 ? c2 : c0;
                gray = static_cast<uint8_t>((77U * r + 150U * g + 29U * b) >> 8U);
            }
            grayscale[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = gray;
            low = std::min(low, gray);
            high = std::max(high, gray);
        }
    }

    const auto range = std::max(24, static_cast<int>(high) - static_cast<int>(low));
    for (int32_t y = 0; y < source.height; ++y) {
        for (int32_t x = 0; x < source.width; ++x) {
            const auto index = static_cast<size_t>(y) * width + static_cast<size_t>(x);
            const auto center = static_cast<int>(grayscale[index]);
            int neighbors = 0;
            int sum = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const auto nx = std::clamp(x + dx, 0, source.width - 1);
                    const auto ny = std::clamp(y + dy, 0, source.height - 1);
                    sum += grayscale[static_cast<size_t>(ny) * width + static_cast<size_t>(nx)];
                    ++neighbors;
                }
            }
            const auto stretched = (center - low) * 255 / range;
            const auto blurred = sum / neighbors;
            const auto sharpened = stretched + (center - blurred) / 2;
            result->bytes[index] = static_cast<std::byte>(std::clamp(sharpened, 0, 255));
        }
    }
    return result;
}

} // namespace loupe::image
