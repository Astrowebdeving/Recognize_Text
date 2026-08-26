#pragma once

#include "core/types/CaptureTypes.hpp"

#include <memory>

namespace loupe::image {

[[nodiscard]] std::shared_ptr<const PixelBuffer> crop(const DesktopFrame& frame,
                                                      PixelRect desktopRect);
[[nodiscard]] std::shared_ptr<const PixelBuffer> documentEnhance(const PixelBuffer& source);

} // namespace loupe::image

