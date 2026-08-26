#pragma once

#include "core/types/CaptureTypes.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace loupe {

enum class AppCaptureState {
    AutoFollowing,
    AutoDwelling,
    Frozen,
    ManualSelecting,
    Adjusting,
    Pinned,
    PermissionRequired,
    CaptureUnavailable
};

class CaptureCoordinator {
public:
    CaptureCoordinator();

    [[nodiscard]] AppCaptureState state() const noexcept;
    [[nodiscard]] GenerationId generation() const noexcept;
    [[nodiscard]] std::shared_ptr<const RegionSnapshot> region() const;

    GenerationId publish(std::shared_ptr<const RegionSnapshot> region);
    bool publishOcrGeneration(GenerationId generation) const noexcept;

    void beginManualSelection();
    void beginAdjusting();
    void setPinned(bool pinned);
    void setPointerInsideUtility(bool inside);
    void setCaptureStatus(CaptureStatusCode status);
    void resumeAuto();

private:
    mutable std::mutex mutex_;
    std::atomic<GenerationId> generation_{0};
    AppCaptureState state_{AppCaptureState::AutoFollowing};
    std::shared_ptr<const RegionSnapshot> region_;
    bool pointerInside_{};
    bool pinned_{};
};

} // namespace loupe

