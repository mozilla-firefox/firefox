/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OverscrollHandoffState.h"

#include <algorithm>  // for std::stable_sort

#include "AsyncPanZoomController.h"
#include "mozilla/Assertions.h"

namespace mozilla {
namespace layers {

void OverscrollHandoffChain::Add(AsyncPanZoomController* aApzc) {
  mChain.push_back(aApzc);
}

const RefPtr<AsyncPanZoomController>& OverscrollHandoffChain::GetApzcAtIndex(
    uint32_t aIndex) const {
  MOZ_ASSERT(aIndex < Length());
  return mChain[aIndex];
}

uint32_t OverscrollHandoffChain::IndexOf(
    const AsyncPanZoomController* aApzc) const {
  uint32_t i;
  for (i = 0; i < Length(); ++i) {
    if (mChain[i] == aApzc) {
      break;
    }
  }
  return i;
}

void OverscrollHandoffChain::ForEachApzc(APZCMethod aMethod) const {
  for (uint32_t i = 0; i < Length(); ++i) {
    (mChain[i]->*aMethod)();
  }
}

bool OverscrollHandoffChain::AnyApzc(APZCPredicate aPredicate) const {
  MOZ_ASSERT(Length() > 0);
  for (uint32_t i = 0; i < Length(); ++i) {
    if ((mChain[i]->*aPredicate)()) {
      return true;
    }
  }
  return false;
}

void OverscrollHandoffChain::FlushRepaints() const {
  ForEachApzc(&AsyncPanZoomController::FlushRepaintForOverscrollHandoff);
}

void OverscrollHandoffChain::CancelAnimations(
    CancelAnimationFlags aFlags) const {
  MOZ_ASSERT(Length() > 0);
  for (uint32_t i = 0; i < Length(); ++i) {
    mChain[i]->CancelAnimation(aFlags);
  }
}

void OverscrollHandoffChain::ClearOverscroll() const {
  ForEachApzc(&AsyncPanZoomController::ClearOverscroll);
}

void OverscrollHandoffChain::ClearScrolledByHandedOffGesture() const {
  ForEachApzc(&AsyncPanZoomController::ClearScrolledByHandedOffGesture);
}

void OverscrollHandoffChain::SnapBackOverscrolledApzc(
    const AsyncPanZoomController* aStart) const {
  uint32_t i = IndexOf(aStart);
  for (; i < Length(); ++i) {
    AsyncPanZoomController* apzc = mChain[i];
    if (!apzc->IsDestroyed()) {
      apzc->SnapBackIfOverscrolled();
    }
  }
}

void OverscrollHandoffChain::SnapBackOverscrolledApzcForMomentum(
    const AsyncPanZoomController* aStart,
    const ParentLayerPoint& aVelocity) const {
  uint32_t i = IndexOf(aStart);
  for (; i < Length(); ++i) {
    AsyncPanZoomController* apzc = mChain[i];
    if (!apzc->IsDestroyed()) {
      apzc->SnapBackIfOverscrolledForMomentum(aVelocity);
    }
  }
}

bool OverscrollHandoffChain::CanBePanned(
    const AsyncPanZoomController* aApzc) const {
  // Find |aApzc| in the handoff chain.
  uint32_t i = IndexOf(aApzc);

  // See whether any APZC in the handoff chain starting from |aApzc|
  // has room to be panned.
  for (uint32_t j = i; j < Length(); ++j) {
    if (mChain[j]->IsPannable()) {
      return true;
    }
  }

  return false;
}

bool OverscrollHandoffChain::CanScrollInDirection(
    const AsyncPanZoomController* aApzc, ScrollDirection aDirection) const {
  // Find |aApzc| in the handoff chain.
  uint32_t i = IndexOf(aApzc);

  // See whether any APZC in the handoff chain starting from |aApzc|
  // has room to scroll in the given direction.
  for (uint32_t j = i; j < Length(); ++j) {
    if (mChain[j]->CanScroll(aDirection)) {
      return true;
    }
  }

  return false;
}

bool OverscrollHandoffChain::HasOverscrolledApzc() const {
  return AnyApzc(&AsyncPanZoomController::IsOverscrolled);
}

bool OverscrollHandoffChain::HasFastFlungApzc() const {
  return AnyApzc(&AsyncPanZoomController::IsFlingingFast);
}

bool OverscrollHandoffChain::HasAutoscrollApzc() const {
  return AnyApzc(&AsyncPanZoomController::IsAutoscroll);
}

bool OverscrollHandoffChain::HasAnimatingApzc() const {
  return AnyApzc(&AsyncPanZoomController::IsAnimationRunning);
}

RefPtr<AsyncPanZoomController> OverscrollHandoffChain::FindScrollTarget(
    const InputData& aInput, ScrollDirections* aOutAllowedScrollDirections,
    IncludeOverscroll aIncludeOverscroll) const {
  // |aOutAllowedScrollDirections| carries the overscroll-behavior handoff
  // restrictions of the APZCs that precede the returned one in the chain. The
  // caller rebuilds the handoff chain from the returned APZC, so those APZCs
  // drop out of it and no longer get to enforce their own restrictions when the
  // scroll is dispatched. That's why this info is necessary.
  *aOutAllowedScrollDirections = EitherScrollDirection;

  // The directions handoff still allows on entry to each APZC the scroll pass
  // reaches.
  AutoTArray<ScrollDirections, 8> allowedDirectionsOnEntry;
  ScrollDirections allowed = EitherScrollDirection;
  for (size_t i = 0; i < Length(); i++) {
    if (mChain[i]->CanScroll(aInput)) {
      *aOutAllowedScrollDirections = allowed;
      MOZ_ASSERT(!aOutAllowedScrollDirections->isEmpty());
      return mChain[i];
    }
    allowedDirectionsOnEntry.AppendElement(allowed);
    allowed &= mChain[i]->GetAllowedHandoffDirections();
    if (allowed.isEmpty()) {
      break;
    }
  }

  if (!StaticPrefs::apz_overscroll_enabled() || !bool(aIncludeOverscroll) ||
      // FIXME: Bug 1707491: Drop this pan gesture input check.
      aInput.mInputType != PANGESTURE_INPUT) {
    MOZ_ASSERT(!aOutAllowedScrollDirections->isEmpty());
    return nullptr;
  }

  // Check overscroll possibility. Note that the check starts from an outer
  // frame.
  for (size_t i = allowedDirectionsOnEntry.Length(); i-- > 0;) {
    // |overscrollable| describes what this frame itself can do, so
    // they only decide whether it gets picked. They deliberately do not narrow
    // |aOutAllowedScrollDirections|, which reports what the earlier frames
    // forbid, so |reachable[i]| is what gets written out.
    ScrollDirections overscrollable = mChain[i]->GetOverscrollableDirections();
    ParentLayerPoint delta = mChain[i]->GetDeltaForEvent(aInput);
    if (mChain[i]->IsZero(delta.x)) {
      overscrollable -= ScrollDirection::eHorizontal;
    }
    if (mChain[i]->IsZero(delta.y)) {
      overscrollable -= ScrollDirection::eVertical;
    }
    if (!(overscrollable & allowedDirectionsOnEntry[i]).isEmpty()) {
      // Don't intersect it with overscrollable here. See the above comment.
      *aOutAllowedScrollDirections = allowedDirectionsOnEntry[i];
      MOZ_ASSERT(!aOutAllowedScrollDirections->isEmpty());
      return mChain[i];
    }
  }

  MOZ_ASSERT(!aOutAllowedScrollDirections->isEmpty());
  return nullptr;
}

std::tuple<bool, const AsyncPanZoomController*>
OverscrollHandoffChain::ScrollingDownWillMoveDynamicToolbar(
    const AsyncPanZoomController* aApzc) const {
  MOZ_ASSERT(aApzc && !aApzc->IsRootContent(),
             "Should be used for non-root APZC");

  for (uint32_t i = IndexOf(aApzc); i < Length(); i++) {
    if (mChain[i]->IsRootContent()) {
      bool scrollable = mChain[i]->CanVerticalScrollWithDynamicToolbar();
      return {scrollable, scrollable ? mChain[i].get() : nullptr};
    }

    if (mChain[i]->CanScrollDownwards()) {
      return {false, nullptr};
    }
  }

  return {false, nullptr};
}

bool OverscrollHandoffChain::ScrollingUpWillTriggerPullToRefresh(
    const AsyncPanZoomController* aApzc) const {
  MOZ_ASSERT(aApzc && !aApzc->IsRootContent(),
             "Should be used for non-root APZC");

  for (uint32_t i = IndexOf(aApzc); i < Length(); i++) {
    if (mChain[i]->IsRootContent()) {
      return mChain[i]->CanOverscrollUpwards(HandoffConsumer::PullToRefresh);
    }

    if (!mChain[i]->CanOverscrollUpwards(HandoffConsumer::PullToRefresh)) {
      return false;
    }
  }
  return false;
}

SideBits OverscrollHandoffChain::GetSwipableDirections(
    const AsyncPanZoomController& aTarget) const {
  SideBits result = SideBits::eLeftRight;
  for (auto i = IndexOf(&aTarget); i < Length(); i++) {
    const auto& apzc = mChain[i];
    result &= apzc->OverscrollBehaviorAllowsSwipe()
                  ? ~(apzc->ScrollableDirections()) & SideBits::eLeftRight
                  : SideBits::eNone;
    if (result == SideBits::eNone) {
      return result;
    }
  }
  return result;
}

bool OverscrollHandoffState::IsScrolledByHandedOffGesture() const {
  // A chain index > 0 means the gesture originated on a descendant APZ
  // and was handed off to this one, rather than starting here.
  return mChainIndex > 0 &&
         (mScrollSource == ScrollSource::Touchscreen ||
          mScrollSource == ScrollSource::Touchpad) &&
         mChain.GetApzcAtIndex(0)->IsInScrollingGesture();
}

}  // namespace layers
}  // namespace mozilla
