#include "capture/windows/DxgiDesktopCapture.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <ShellScalingApi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <vector>

namespace loupe {

using Microsoft::WRL::ComPtr;

namespace {

struct OutputState {
    DisplayInfo info;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    std::shared_ptr<const DesktopFrame> lastValid;
    std::mutex mutex;
};

std::string narrow(const wchar_t* value) {
    if (value == nullptr) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

} // namespace

struct DxgiDesktopCapture::Impl {
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<OutputState>> outputs;
    CaptureStatus current{CaptureStatusCode::Stopped, "Capture stopped"};
    std::atomic<FrameId> nextFrame{1};
};

DxgiDesktopCapture::DxgiDesktopCapture() : impl_(std::make_unique<Impl>()) {}
DxgiDesktopCapture::~DxgiDesktopCapture() { stop(); }

CaptureStatus DxgiDesktopCapture::start() {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->outputs.empty()) return impl_->current;
    impl_->current = {CaptureStatusCode::Starting, "Starting DXGI capture…"};

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        impl_->current = {CaptureStatusCode::Failed, "Could not create the DXGI factory"};
        return impl_->current;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL obtained{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        auto deviceResult = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                              D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                              static_cast<UINT>(std::size(levels)),
                                              D3D11_SDK_VERSION, &device, &obtained, &context);
        if (deviceResult == E_INVALIDARG) {
            const D3D_FEATURE_LEVEL fallbackLevels[]{D3D_FEATURE_LEVEL_11_0};
            deviceResult = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, fallbackLevels,
                                             static_cast<UINT>(std::size(fallbackLevels)),
                                             D3D11_SDK_VERSION, &device, &obtained, &context);
        }
        if (FAILED(deviceResult)) continue;

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC description{};
            if (FAILED(output->GetDesc(&description)) || !description.AttachedToDesktop) continue;

            ComPtr<IDXGIOutput5> output5;
            if (FAILED(output.As(&output5))) continue;
            const DXGI_FORMAT formats[]{DXGI_FORMAT_B8G8R8A8_UNORM,
                                        DXGI_FORMAT_R8G8B8A8_UNORM};
            ComPtr<IDXGIOutputDuplication> duplication;
            if (FAILED(output5->DuplicateOutput1(device.Get(), 0,
                                                 static_cast<UINT>(std::size(formats)), formats,
                                                 &duplication))) continue;

            auto state = std::make_shared<OutputState>();
            state->device = device;
            state->context = context;
            state->duplication = duplication;
            state->info.id = (static_cast<uint64_t>(adapterIndex + 1U) << 32U)
                           | static_cast<uint64_t>(outputIndex + 1U);
            const auto& rect = description.DesktopCoordinates;
            state->info.physicalRect = {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
            UINT dpiX = 96;
            UINT dpiY = 96;
            if (FAILED(GetDpiForMonitor(description.Monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                dpiX = dpiY = 96;
            }
            state->info.devicePixelRatio = static_cast<double>(dpiX) / 96.0;
            state->info.name = narrow(description.DeviceName);
            state->info.primary = rect.left == 0 && rect.top == 0;
            impl_->outputs.push_back(std::move(state));
        }
    }

    impl_->current = impl_->outputs.empty()
        ? CaptureStatus{CaptureStatusCode::TemporarilyUnavailable, "No duplicable desktop output is available"}
        : CaptureStatus{CaptureStatusCode::Ready, "DXGI capture ready"};
    return impl_->current;
}

void DxgiDesktopCapture::stop() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->outputs.clear();
    impl_->current = {CaptureStatusCode::Stopped, "Capture stopped"};
}

std::shared_ptr<const DesktopFrame> DxgiDesktopCapture::latestFrame(DisplayId display) {
    std::shared_ptr<OutputState> state;
    {
        std::scoped_lock lock(impl_->mutex);
        for (const auto& output : impl_->outputs) {
            if (output->info.id == display) { state = output; break; }
        }
    }
    if (!state) return {};
    std::scoped_lock stateLock(state->mutex);

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> resource;
    const auto acquire = state->duplication->AcquireNextFrame(5, &frameInfo, &resource);
    if (acquire == DXGI_ERROR_WAIT_TIMEOUT) return state->lastValid;
    if (acquire == DXGI_ERROR_ACCESS_LOST || acquire == DXGI_ERROR_SESSION_DISCONNECTED) {
        std::scoped_lock lock(impl_->mutex);
        impl_->current = {CaptureStatusCode::TemporarilyUnavailable,
                          "Desktop capture session changed; capture will be recreated"};
        return state->lastValid;
    }
    if (FAILED(acquire)) return state->lastValid;

    struct ReleaseGuard {
        IDXGIOutputDuplication* duplication;
        ~ReleaseGuard() { duplication->ReleaseFrame(); }
    } release{state->duplication.Get()};

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return state->lastValid;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    bool stagingMatches = false;
    if (state->staging) {
        D3D11_TEXTURE2D_DESC stagingDescription{};
        state->staging->GetDesc(&stagingDescription);
        stagingMatches = stagingDescription.Width == description.Width &&
                         stagingDescription.Height == description.Height &&
                         stagingDescription.Format == description.Format;
    }
    if (!stagingMatches) {
        state->staging.Reset();
        auto stagingDescription = description;
        stagingDescription.BindFlags = 0;
        stagingDescription.MiscFlags = 0;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(state->device->CreateTexture2D(&stagingDescription, nullptr, &state->staging)))
            return state->lastValid;
    }
    state->context->CopyResource(state->staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(state->context->Map(state->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return state->lastValid;
    struct UnmapGuard {
        ID3D11DeviceContext* context;
        ID3D11Resource* resource;
        ~UnmapGuard() { context->Unmap(resource, 0); }
    } unmap{state->context.Get(), state->staging.Get()};

    if (description.Width == 0 || description.Height == 0 ||
        description.Width > static_cast<UINT>(std::numeric_limits<int32_t>::max() / 4) ||
        description.Height > static_cast<UINT>(std::numeric_limits<int32_t>::max()))
        return state->lastValid;
    const auto rowBytes = static_cast<size_t>(description.Width) * 4U;
    const auto rows = static_cast<size_t>(description.Height);
    if (static_cast<size_t>(mapped.RowPitch) < rowBytes ||
        rowBytes > std::numeric_limits<size_t>::max() / rows)
        return state->lastValid;

    std::shared_ptr<PixelBuffer> pixels;
    try {
        pixels = std::make_shared<PixelBuffer>();
        pixels->width = static_cast<int32_t>(description.Width);
        pixels->height = static_cast<int32_t>(description.Height);
        pixels->bytesPerRow = static_cast<int32_t>(rowBytes);
        pixels->format = description.Format == DXGI_FORMAT_R8G8B8A8_UNORM
            ? PixelFormat::Rgba8 : PixelFormat::Bgra8;
        pixels->bytes.resize(rowBytes * rows);
    } catch (const std::bad_alloc&) {
        std::scoped_lock lock(impl_->mutex);
        impl_->current = {CaptureStatusCode::TemporarilyUnavailable,
                          "Not enough memory for a desktop frame"};
        return state->lastValid;
    }
    for (size_t y = 0; y < rows; ++y) {
        std::memcpy(pixels->bytes.data() + y * rowBytes,
                    static_cast<const std::byte*>(mapped.pData)
                        + y * static_cast<size_t>(mapped.RowPitch),
                    rowBytes);
    }

    auto frame = std::make_shared<DesktopFrame>();
    frame->frameId = impl_->nextFrame.fetch_add(1, std::memory_order_relaxed);
    frame->display = display;
    frame->physicalRect = state->info.physicalRect;
    frame->pixels = std::move(pixels);
    frame->capturedAt = std::chrono::steady_clock::now();
    frame->maskedContentPresent = frameInfo.ProtectedContentMaskedOut != FALSE;
    state->lastValid = frame;
    return frame;
}

std::vector<DisplayInfo> DxgiDesktopCapture::displays() const {
    std::vector<DisplayInfo> result;
    std::scoped_lock lock(impl_->mutex);
    result.reserve(impl_->outputs.size());
    for (const auto& output : impl_->outputs) result.push_back(output->info);
    return result;
}

CaptureStatus DxgiDesktopCapture::status() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->current;
}

} // namespace loupe
