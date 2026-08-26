#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace loupe {

enum class PixelFormat {
    Bgra8,
    Rgba8,
    Gray8
};

struct PixelBuffer {
    int32_t width{};
    int32_t height{};
    int32_t bytesPerRow{};
    PixelFormat format{PixelFormat::Bgra8};
    std::vector<std::byte> bytes;

    [[nodiscard]] int32_t bytesPerPixel() const noexcept {
        return format == PixelFormat::Gray8 ? 1 : 4;
    }

    [[nodiscard]] bool valid() const noexcept {
        const auto minimumRowBytes = static_cast<int64_t>(width) * bytesPerPixel();
        if (width <= 0 || height <= 0 || bytesPerRow < minimumRowBytes) {
            return false;
        }
        const auto rowBytes = static_cast<size_t>(bytesPerRow);
        const auto rows = static_cast<size_t>(height);
        if (rowBytes > std::numeric_limits<size_t>::max() / rows) return false;
        return bytes.size() >= rowBytes * rows;
    }

    [[nodiscard]] std::span<const std::byte> row(int32_t y) const {
        if (!valid() || y < 0 || y >= height) {
            throw std::out_of_range("PixelBuffer row");
        }
        const auto offset = static_cast<size_t>(y) * static_cast<size_t>(bytesPerRow);
        return {bytes.data() + offset, static_cast<size_t>(bytesPerRow)};
    }
};

using ImmutablePixels = std::shared_ptr<const PixelBuffer>;

} // namespace loupe
