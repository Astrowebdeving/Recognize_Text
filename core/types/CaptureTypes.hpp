#pragma once

#include "core/types/PixelBuffer.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace loupe {

using FrameId = uint64_t;
using RegionId = uint64_t;
using GenerationId = uint64_t;
using DisplayId = uint64_t;

struct PixelPoint {
    int32_t x{};
    int32_t y{};
};

struct PixelRect {
    int32_t x{};
    int32_t y{};
    int32_t width{};
    int32_t height{};

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] int64_t right() const noexcept {
        return static_cast<int64_t>(x) + width;
    }
    [[nodiscard]] int64_t bottom() const noexcept {
        return static_cast<int64_t>(y) + height;
    }
    [[nodiscard]] bool contains(PixelPoint p) const noexcept {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
};

struct DisplayInfo {
    DisplayId id{};
    PixelRect physicalRect;
    double devicePixelRatio{1.0};
    std::string name;
    bool primary{};
};

struct DesktopFrame {
    FrameId frameId{};
    DisplayId display{};
    PixelRect physicalRect;
    ImmutablePixels pixels;
    std::chrono::steady_clock::time_point capturedAt;
    bool protectedContent{};
    bool maskedContentPresent{};
};

enum class CaptureOrigin {
    AutoPointer,
    ManualDrag,
    Adjusted,
    Pinned
};

struct RegionSnapshot {
    FrameId frameId{};
    RegionId regionId{};
    GenerationId generation{};
    CaptureOrigin origin{CaptureOrigin::AutoPointer};
    PixelRect userRect;
    PixelRect analysisRect;
    ImmutablePixels originalPixels;
    DisplayId display{};
    double devicePixelRatio{1.0};
    std::chrono::steady_clock::time_point capturedAt;
};

enum class CaptureStatusCode {
    Stopped,
    Starting,
    Ready,
    PermissionRequired,
    ProtectedContent,
    TemporarilyUnavailable,
    Unsupported,
    Failed
};

struct CaptureStatus {
    CaptureStatusCode code{CaptureStatusCode::Stopped};
    std::string message;

    [[nodiscard]] bool usable() const noexcept {
        return code == CaptureStatusCode::Starting || code == CaptureStatusCode::Ready;
    }
};

} // namespace loupe
