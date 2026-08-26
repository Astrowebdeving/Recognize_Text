#pragma once

#include "capture/IScreenCapture.hpp"

#include <memory>

namespace loupe {

class DxgiDesktopCapture final : public IScreenCapture {
public:
    DxgiDesktopCapture();
    ~DxgiDesktopCapture() override;

    CaptureStatus start() override;
    void stop() noexcept override;
    std::shared_ptr<const DesktopFrame> latestFrame(DisplayId display) override;
    [[nodiscard]] std::vector<DisplayInfo> displays() const override;
    [[nodiscard]] CaptureStatus status() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace loupe

