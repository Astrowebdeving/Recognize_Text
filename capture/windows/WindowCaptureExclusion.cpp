#include "capture/windows/WindowCaptureExclusion.hpp"

#include <Windows.h>

namespace loupe {

bool excludeWindowFromCapture(uintptr_t nativeWindowHandle) noexcept {
    if (nativeWindowHandle == 0) return false;
    constexpr DWORD excludeFromCapture = 0x00000011;
    return SetWindowDisplayAffinity(reinterpret_cast<HWND>(nativeWindowHandle),
                                    excludeFromCapture) != FALSE;
}

} // namespace loupe

