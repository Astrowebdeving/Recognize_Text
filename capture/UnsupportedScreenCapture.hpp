#pragma once

#include "capture/IScreenCapture.hpp"

namespace loupe {

class UnsupportedScreenCapture final : public IScreenCapture {
public:
    CaptureStatus start() override;
    void stop() noexcept override;
    std::shared_ptr<const DesktopFrame> latestFrame(DisplayId) override;
    [[nodiscard]] std::vector<DisplayInfo> displays() const override;
    [[nodiscard]] CaptureStatus status() const override;
};

} // namespace loupe

