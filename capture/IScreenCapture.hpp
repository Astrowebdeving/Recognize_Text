#pragma once

#include "core/types/CaptureTypes.hpp"

#include <memory>
#include <vector>

namespace loupe {

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;
    virtual CaptureStatus start() = 0;
    virtual void stop() noexcept = 0;
    virtual std::shared_ptr<const DesktopFrame> latestFrame(DisplayId display) = 0;
    [[nodiscard]] virtual std::vector<DisplayInfo> displays() const = 0;
    [[nodiscard]] virtual CaptureStatus status() const = 0;
};

} // namespace loupe

