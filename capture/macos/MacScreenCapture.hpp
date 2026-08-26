#pragma once

#include "capture/IScreenCapture.hpp"

#include <memory>

namespace loupe {

class MacScreenCapture final : public IScreenCapture {
public:
    MacScreenCapture();
    ~MacScreenCapture() override;

    MacScreenCapture(const MacScreenCapture&) = delete;
    MacScreenCapture& operator=(const MacScreenCapture&) = delete;

    CaptureStatus start() override;
    void stop() noexcept override;
    std::shared_ptr<const DesktopFrame> latestFrame(DisplayId display) override;
    [[nodiscard]] std::vector<DisplayInfo> displays() const override;
    [[nodiscard]] CaptureStatus status() const override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace loupe
