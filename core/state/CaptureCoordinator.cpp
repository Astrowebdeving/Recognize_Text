#include "core/state/CaptureCoordinator.hpp"

namespace loupe {

CaptureCoordinator::CaptureCoordinator() = default;

AppCaptureState CaptureCoordinator::state() const noexcept {
    std::scoped_lock lock(mutex_);
    return state_;
}

GenerationId CaptureCoordinator::generation() const noexcept {
    return generation_.load(std::memory_order_acquire);
}

std::shared_ptr<const RegionSnapshot> CaptureCoordinator::region() const {
    std::scoped_lock lock(mutex_);
    return region_;
}

GenerationId CaptureCoordinator::publish(std::shared_ptr<const RegionSnapshot> region) {
    if (!region || !region->originalPixels) return generation();
    auto owned = std::make_shared<RegionSnapshot>(*region);
    GenerationId next{};
    {
        std::scoped_lock lock(mutex_);
        next = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
        owned->generation = next;
        region_ = std::move(owned);
        if (pinned_) state_ = AppCaptureState::Pinned;
        else if (pointerInside_) state_ = AppCaptureState::Frozen;
        else if (state_ == AppCaptureState::ManualSelecting || state_ == AppCaptureState::Adjusting)
            state_ = AppCaptureState::Frozen;
        else state_ = AppCaptureState::AutoDwelling;
    }
    return next;
}

bool CaptureCoordinator::publishOcrGeneration(GenerationId value) const noexcept {
    return value != 0 && value == generation_.load(std::memory_order_acquire);
}

void CaptureCoordinator::beginManualSelection() {
    std::scoped_lock lock(mutex_);
    if (!pinned_) state_ = AppCaptureState::ManualSelecting;
}

void CaptureCoordinator::beginAdjusting() {
    std::scoped_lock lock(mutex_);
    if (region_) state_ = AppCaptureState::Adjusting;
}

void CaptureCoordinator::setPinned(bool pinned) {
    std::scoped_lock lock(mutex_);
    pinned_ = pinned;
    state_ = pinned ? AppCaptureState::Pinned
                    : (pointerInside_ ? AppCaptureState::Frozen : AppCaptureState::AutoFollowing);
}

void CaptureCoordinator::setPointerInsideUtility(bool inside) {
    std::scoped_lock lock(mutex_);
    pointerInside_ = inside;
    if (pinned_) return;
    if (inside && (state_ == AppCaptureState::AutoFollowing || state_ == AppCaptureState::AutoDwelling))
        state_ = AppCaptureState::Frozen;
    else if (!inside && state_ == AppCaptureState::Frozen)
        state_ = AppCaptureState::AutoFollowing;
}

void CaptureCoordinator::setCaptureStatus(CaptureStatusCode status) {
    std::scoped_lock lock(mutex_);
    if (status == CaptureStatusCode::PermissionRequired)
        state_ = AppCaptureState::PermissionRequired;
    else if (status == CaptureStatusCode::Unsupported || status == CaptureStatusCode::Failed)
        state_ = AppCaptureState::CaptureUnavailable;
    else if (status == CaptureStatusCode::Ready &&
             (state_ == AppCaptureState::PermissionRequired || state_ == AppCaptureState::CaptureUnavailable))
        state_ = AppCaptureState::AutoFollowing;
}

void CaptureCoordinator::resumeAuto() {
    std::scoped_lock lock(mutex_);
    pinned_ = false;
    state_ = pointerInside_ ? AppCaptureState::Frozen : AppCaptureState::AutoFollowing;
}

} // namespace loupe
