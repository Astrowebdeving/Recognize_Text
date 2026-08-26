#pragma once

#include "core/types/CaptureTypes.hpp"
#include "core/types/OcrTypes.hpp"

namespace loupe::coordinates {

[[nodiscard]] PixelRect intersect(PixelRect a, PixelRect b) noexcept;
[[nodiscard]] PixelRect expandAndClamp(PixelRect rect, int32_t padding, PixelRect bounds) noexcept;
[[nodiscard]] PixelPoint logicalToPhysical(double x, double y, PixelRect logicalDisplay,
                                           PixelRect physicalDisplay) noexcept;
[[nodiscard]] PixelRect logicalToPhysical(PixelRect logical, PixelRect logicalDisplay,
                                          PixelRect physicalDisplay) noexcept;
[[nodiscard]] PixelRect normalizedBounds(const NormalizedQuad& quad, PixelRect target) noexcept;

} // namespace loupe::coordinates

