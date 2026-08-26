#include "core/coordinates/Coordinates.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace loupe::coordinates {

namespace {

int32_t saturatingInt32(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto minimum = static_cast<double>(std::numeric_limits<int32_t>::min());
    const auto maximum = static_cast<double>(std::numeric_limits<int32_t>::max());
    return static_cast<int32_t>(std::lround(std::clamp(value, minimum, maximum)));
}

int32_t boundedExtent(int64_t value) noexcept {
    return static_cast<int32_t>(std::clamp<int64_t>(
        value, 0, std::numeric_limits<int32_t>::max()));
}

float normalized(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

} // namespace

PixelRect intersect(PixelRect a, PixelRect b) noexcept {
    const auto left = static_cast<int64_t>(std::max(a.x, b.x));
    const auto top = static_cast<int64_t>(std::max(a.y, b.y));
    const auto right = std::min(a.right(), b.right());
    const auto bottom = std::min(a.bottom(), b.bottom());
    const auto width = std::clamp<int64_t>(right - left, 0, std::numeric_limits<int32_t>::max());
    const auto height = std::clamp<int64_t>(bottom - top, 0, std::numeric_limits<int32_t>::max());
    return {static_cast<int32_t>(left), static_cast<int32_t>(top),
            static_cast<int32_t>(width), static_cast<int32_t>(height)};
}

PixelRect expandAndClamp(PixelRect rect, int32_t padding, PixelRect bounds) noexcept {
    const auto expandedX = static_cast<int64_t>(rect.x) - padding;
    const auto expandedY = static_cast<int64_t>(rect.y) - padding;
    PixelRect expanded{
        saturatingInt32(static_cast<double>(expandedX)),
        saturatingInt32(static_cast<double>(expandedY)),
        boundedExtent(static_cast<int64_t>(rect.width) + static_cast<int64_t>(padding) * 2),
        boundedExtent(static_cast<int64_t>(rect.height) + static_cast<int64_t>(padding) * 2)
    };
    return intersect(expanded, bounds);
}

PixelPoint logicalToPhysical(double x, double y, PixelRect logicalDisplay,
                             PixelRect physicalDisplay) noexcept {
    const auto scaleX = logicalDisplay.width == 0
        ? 1.0 : static_cast<double>(physicalDisplay.width) / logicalDisplay.width;
    const auto scaleY = logicalDisplay.height == 0
        ? 1.0 : static_cast<double>(physicalDisplay.height) / logicalDisplay.height;
    return {
        saturatingInt32(physicalDisplay.x + (x - logicalDisplay.x) * scaleX),
        saturatingInt32(physicalDisplay.y + (y - logicalDisplay.y) * scaleY)
    };
}

PixelRect logicalToPhysical(PixelRect logical, PixelRect logicalDisplay,
                            PixelRect physicalDisplay) noexcept {
    const auto topLeft = logicalToPhysical(logical.x, logical.y, logicalDisplay, physicalDisplay);
    const auto bottomRight = logicalToPhysical(logical.right(), logical.bottom(), logicalDisplay,
                                               physicalDisplay);
    return {topLeft.x, topLeft.y,
            boundedExtent(static_cast<int64_t>(bottomRight.x) - topLeft.x),
            boundedExtent(static_cast<int64_t>(bottomRight.y) - topLeft.y)};
}

PixelRect normalizedBounds(const NormalizedQuad& q, PixelRect target) noexcept {
    const auto minX = std::min({normalized(q.x1), normalized(q.x2), normalized(q.x3), normalized(q.x4)});
    const auto maxX = std::max({normalized(q.x1), normalized(q.x2), normalized(q.x3), normalized(q.x4)});
    const auto minY = std::min({normalized(q.y1), normalized(q.y2), normalized(q.y3), normalized(q.y4)});
    const auto maxY = std::max({normalized(q.y1), normalized(q.y2), normalized(q.y3), normalized(q.y4)});
    const auto x = saturatingInt32(target.x + minX * target.width);
    const auto y = saturatingInt32(target.y + minY * target.height);
    const auto right = saturatingInt32(target.x + maxX * target.width);
    const auto bottom = saturatingInt32(target.y + maxY * target.height);
    return {x, y,
            boundedExtent(static_cast<int64_t>(right) - x),
            boundedExtent(static_cast<int64_t>(bottom) - y)};
}

} // namespace loupe::coordinates
