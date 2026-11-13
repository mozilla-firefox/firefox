/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "CoalescedMouseData.h"

#include "BrowserChild.h"
#include "base/basictypes.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsRefreshDriver.h"

using namespace mozilla;
using namespace mozilla::dom;

void CoalescedMouseData::Coalesce(const WidgetMouseEvent& aMouseOrPointerEvent,
                                  const ScrollableLayerGuid& aGuid,
                                  const uint64_t& aInputBlockId,
                                  const nsID& aUUID) {
  MOZ_ASSERT_IF(aMouseOrPointerEvent.AsPointerEvent(),
                aMouseOrPointerEvent.mMessage == eContextMenu);
  MOZ_ASSERT(!aMouseOrPointerEvent.AsDragEvent());
  if (IsEmpty()) {
    mCoalescedInputEvent = [&]() -> UniquePtr<WidgetMouseEvent> {
      if (aMouseOrPointerEvent.mClass == ePointerEventClass) {
        MOZ_DIAGNOSTIC_ASSERT(aMouseOrPointerEvent.AsPointerEvent());
        return std::make_unique<WidgetPointerEvent>(
            static_cast<const WidgetPointerEvent&>(
                *aMouseOrPointerEvent.AsPointerEvent()));
      }
      MOZ_DIAGNOSTIC_ASSERT(!aMouseOrPointerEvent.AsPointerEvent());
      return std::make_unique<WidgetMouseEvent>(aMouseOrPointerEvent);
    }();
    mCoalescedInputEvent->mCallbackId = aMouseOrPointerEvent.mCallbackId;
    mGuid = aGuid;
    mInputBlockId = aInputBlockId;
    MOZ_ASSERT(!mCoalescedInputEvent->mCoalescedWidgetEvents);
  } else {
    MOZ_ASSERT(aMouseOrPointerEvent.mCallbackId.isNothing());
    MOZ_ASSERT(mGuid == aGuid);
    MOZ_ASSERT(mInputBlockId == aInputBlockId);
    MOZ_ASSERT(mCoalescedInputEvent->mModifiers ==
               aMouseOrPointerEvent.mModifiers);
    MOZ_ASSERT(mCoalescedInputEvent->mReason == aMouseOrPointerEvent.mReason);
    MOZ_ASSERT(mCoalescedInputEvent->mInputSource ==
               aMouseOrPointerEvent.mInputSource);
    MOZ_ASSERT(mCoalescedInputEvent->mButton == aMouseOrPointerEvent.mButton);
    MOZ_ASSERT(mCoalescedInputEvent->mButtons == aMouseOrPointerEvent.mButtons);
    mCoalescedInputEvent->mTimeStamp = aMouseOrPointerEvent.mTimeStamp;
    mCoalescedInputEvent->mRefPoint = aMouseOrPointerEvent.mRefPoint;
    mCoalescedInputEvent->mPressure = aMouseOrPointerEvent.mPressure;
    mCoalescedInputEvent->AssignPointerHelperData(aMouseOrPointerEvent);
    // Accumulate motion across coalesced events. Without this,
    // the dispatched event would report only the first motion,
    // missing subsequent movements.
    if (mCoalescedInputEvent->mMovement) {
      MOZ_ASSERT(aMouseOrPointerEvent.mMovement);
      *mCoalescedInputEvent->mMovement += *aMouseOrPointerEvent.mMovement;
    }
  }

  if (aMouseOrPointerEvent.mMessage == eMouseMove) {
    // PointerEvent::getCoalescedEvents is only applied to pointermove events.
    if (!mCoalescedInputEvent->mCoalescedWidgetEvents) {
      mCoalescedInputEvent->mCoalescedWidgetEvents =
          new WidgetPointerEventHolder();
    }
    // Append current event in mCoalescedWidgetEvents. We use them to generate
    // DOM events when content calls PointerEvent::getCoalescedEvents.
    WidgetPointerEvent* event =
        mCoalescedInputEvent->mCoalescedWidgetEvents->mEvents.AppendElement(
            WidgetPointerEvent::MakeCopyFromMouseEvent(aMouseOrPointerEvent));

    event->mMessage = ePointerMove;
    event->mButton = MouseButton::eNotPressed;
    event->mPressure = aMouseOrPointerEvent.ComputeMouseButtonPressure();
    event->mFlags.mBubbles = false;
    event->mFlags.mCancelable = false;
  }

  if (aUUID != nsID{}) {
    MOZ_ASSERT(mUUID == nsID{}, "Should only coalesce one event with a UUID.");
    mUUID = aUUID;
  }
}

bool CoalescedMouseData::CanCoalesce(const WidgetMouseEvent& aMouseMoveEvent,
                                     const ScrollableLayerGuid& aGuid,
                                     const uint64_t& aInputBlockId,
                                     const nsRefreshDriver* aRefreshDriver) {
  MOZ_ASSERT(aMouseMoveEvent.mMessage == eMouseMove);
  if (!mCoalescedInputEvent) {
    return true;
  }
  if (mCoalescedInputEvent->mFlags.mIsSynthesizedForTests !=
          aMouseMoveEvent.mFlags.mIsSynthesizedForTests ||
      mCoalescedInputEvent->mModifiers != aMouseMoveEvent.mModifiers ||
      mCoalescedInputEvent->mInputSource != aMouseMoveEvent.mInputSource ||
      mCoalescedInputEvent->pointerId != aMouseMoveEvent.pointerId ||
      mCoalescedInputEvent->mButton != aMouseMoveEvent.mButton ||
      mCoalescedInputEvent->mButtons != aMouseMoveEvent.mButtons ||
      mCoalescedInputEvent->mMovement.isSome() !=
          aMouseMoveEvent.mMovement.isSome() ||
      mGuid != aGuid || mInputBlockId != aInputBlockId) {
    return false;
  }
  // Basically, tests do not want to coalesces the consecutive mouse events.
  // However, if the test calls nsIDOMWindowUtils::AdvanceTimeAndRefresh(0),
  // they must try to check coalesced mouse move events.
  if (!aMouseMoveEvent.mFlags.mIsSynthesizedForTests) {
    return true;
  }
  return aRefreshDriver && aRefreshDriver->IsTestControllingRefreshesEnabled();
}

CoalescedMouseMoveFlusher::CoalescedMouseMoveFlusher(
    BrowserChild* aBrowserChild)
    : CoalescedInputFlusher(aBrowserChild) {}

void CoalescedMouseMoveFlusher::WillRefresh(mozilla::TimeStamp aTime) {
  MOZ_ASSERT(mRefreshDriver);
  RefPtr<BrowserChild> browserChild(mBrowserChild);
  browserChild->FlushAllCoalescedMouseData();
  browserChild->ProcessPendingCoalescedMouseDataAndDispatchEvents();
}

CoalescedMouseMoveFlusher::~CoalescedMouseMoveFlusher() { RemoveObserver(); }
