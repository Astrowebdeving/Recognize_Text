#include "capture/UnsupportedScreenCapture.hpp"

namespace loupe {

CaptureStatus UnsupportedScreenCapture::start() {
    return {CaptureStatusCode::Unsupported, "Screen capture is not implemented on this platform"};
}
void UnsupportedScreenCapture::stop() noexcept {}
std::shared_ptr<const DesktopFrame> UnsupportedScreenCapture::latestFrame(DisplayId) { return {}; }
std::vector<DisplayInfo> UnsupportedScreenCapture::displays() const { return {}; }
CaptureStatus UnsupportedScreenCapture::status() const {
    return {CaptureStatusCode::Unsupported, "Screen capture is not implemented on this platform"};
}

} // namespace loupe

