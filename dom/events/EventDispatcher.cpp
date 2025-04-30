/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "nsPresContext.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsError.h"
#include <new>
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "mozilla/dom/Document.h"
#include "nsINode.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsPIDOMWindow.h"
#include "nsRefreshDriver.h"
#include "AnimationEvent.h"
#include "BeforeUnloadEvent.h"
#include "ClipboardEvent.h"
#include "CommandEvent.h"
#include "CompositionEvent.h"
#include "DeviceMotionEvent.h"
#include "DragEvent.h"
#include "KeyboardEvent.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CloseEvent.h"
#include "mozilla/dom/CustomEvent.h"
#include "mozilla/dom/DeviceOrientationEvent.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/FocusEvent.h"
#include "mozilla/dom/HashChangeEvent.h"
#include "mozilla/dom/InputEvent.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MouseScrollEvent.h"
#include "mozilla/dom/MutationEvent.h"
#include "mozilla/dom/NotifyPaintEvent.h"
#include "mozilla/dom/PageTransitionEvent.h"
#include "mozilla/dom/PerformanceEventTiming.h"
#include "mozilla/dom/PerformanceMainThread.h"
#include "mozilla/dom/PointerEvent.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScrollAreaEvent.h"
#include "mozilla/dom/SimpleGestureEvent.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/StorageEvent.h"
#include "mozilla/dom/TextEvent.h"
#include "mozilla/dom/TimeEvent.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/TransitionEvent.h"
#include "mozilla/dom/WheelEvent.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/XULCommandEvent.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/Unused.h"

namespace mozilla {

using namespace dom;

class ELMCreationDetector {
 public:
  ELMCreationDetector()
      // We can do this optimization only in the main thread.
      : mNonMainThread(!NS_IsMainThread()),
        mInitialCount(mNonMainThread
                          ? 0
                          : EventListenerManager::sMainThreadCreatedCount) {}

  bool MayHaveNewListenerManager() {
    return mNonMainThread ||
           mInitialCount != EventListenerManager::sMainThreadCreatedCount;
  }

  bool IsMainThread() { return !mNonMainThread; }

 private:
  bool mNonMainThread;
  uint32_t mInitialCount;
};

static bool IsEventTargetChrome(EventTarget* aEventTarget,
                                Document** aDocument = nullptr) {
  if (aDocument) {
    *aDocument = nullptr;
  }

  Document* doc = nullptr;
  if (nsINode* node = nsINode::FromEventTargetOrNull(aEventTarget)) {
    doc = node->OwnerDoc();
  } else if (nsPIDOMWindowInner* window =
                 nsPIDOMWindowInner::FromEventTargetOrNull(aEventTarget)) {
    doc = window->GetExtantDoc();
  }

  // nsContentUtils::IsChromeDoc is null-safe.
  bool isChrome = false;
  if (doc) {
    isChrome = nsContentUtils::IsChromeDoc(doc);
    if (aDocument) {
      nsCOMPtr<Document> retVal = doc;
      retVal.swap(*aDocument);
    }
  } else if (nsCOMPtr<nsIScriptObjectPrincipal> sop =
                 do_QueryInterface(aEventTarget->GetOwnerGlobal())) {
    isChrome = sop->GetPrincipal()->IsSystemPrincipal();
  }
  return isChrome;
}

// EventTargetChainItem represents a single item in the event target chain.
class EventTargetChainItem {
 public:
  explicit EventTargetChainItem(EventTarget* aTarget)
      : mTarget(aTarget), mItemFlags(0) {
    MOZ_COUNT_CTOR(EventTargetChainItem);
  }

  MOZ_COUNTED_DTOR(EventTargetChainItem)

  static EventTargetChainItem* Create(nsTArray<EventTargetChainItem>& aChain,
                                      EventTarget* aTarget,
                                      EventTargetChainItem* aChild = nullptr) {
    // The last item which can handle the event must be aChild.
    MOZ_ASSERT(GetLastCanHandleEventTarget(aChain) == aChild);
    MOZ_ASSERT(!aTarget || aTarget == aTarget->GetTargetForEventTargetChain());
    EventTargetChainItem* etci = aChain.AppendElement(aTarget);
    return etci;
  }

  static void DestroyLast(nsTArray<EventTargetChainItem>& aChain,
                          EventTargetChainItem* aItem) {
    MOZ_ASSERT(&aChain.LastElement() == aItem);
    aChain.RemoveLastElement();
  }

  static EventTargetChainItem* GetFirstCanHandleEventTarget(
      nsTArray<EventTargetChainItem>& aChain) {
    return &aChain[GetFirstCanHandleEventTargetIdx(aChain)];
  }

  static uint32_t GetFirstCanHandleEventTargetIdx(
      nsTArray<EventTargetChainItem>& aChain) {
    // aChain[i].PreHandleEventOnly() = true only when the target element wants
    // PreHandleEvent and set mCanHandle=false. So we find the first element
    // which can handle the event.
    for (uint32_t i = 0; i < aChain.Length(); ++i) {
      if (!aChain[i].PreHandleEventOnly()) {
        return i;
      }
    }
    MOZ_ASSERT(false);
    return 0;
  }

  static EventTargetChainItem* GetLastCanHandleEventTarget(
      nsTArray<EventTargetChainItem>& aChain) {
    // Fine the last item which can handle the event.
    for (int32_t i = aChain.Length() - 1; i >= 0; --i) {
      if (!aChain[i].PreHandleEventOnly()) {
        return &aChain[i];
      }
    }
    return nullptr;
  }

  bool IsValid() const {
    NS_WARNING_ASSERTION(!!(mTarget), "Event target is not valid!");
    return !!(mTarget);
  }

  EventTarget* GetNewTarget() const { return mNewTarget; }

  void SetNewTarget(EventTarget* aNewTarget) { mNewTarget = aNewTarget; }

  EventTarget* GetRetargetedRelatedTarget() { return mRetargetedRelatedTarget; }

  void SetRetargetedRelatedTarget(EventTarget* aTarget) {
    mRetargetedRelatedTarget = aTarget;
  }

  void SetRetargetedTouchTarget(
      Maybe<nsTArray<RefPtr<EventTarget>>>&& aTargets) {
    mRetargetedTouchTargets = std::move(aTargets);
  }

  bool HasRetargetTouchTargets() const {
    return mRetargetedTouchTargets.isSome() || mInitialTargetTouches.isSome();
  }

  void RetargetTouchTargets(WidgetTouchEvent* aTouchEvent, Event* aDOMEvent) {
    MOZ_ASSERT(HasRetargetTouchTargets());
    MOZ_ASSERT(aTouchEvent,
               "mRetargetedTouchTargets should be empty when dispatching "
               "non-touch events.");

    if (mRetargetedTouchTargets.isSome()) {
      WidgetTouchEvent::TouchArray& touches = aTouchEvent->mTouches;
      MOZ_ASSERT(!touches.Length() ||
                 touches.Length() == mRetargetedTouchTargets->Length());
      for (uint32_t i = 0; i < touches.Length(); ++i) {
        touches[i]->mTarget = mRetargetedTouchTargets->ElementAt(i);
      }
    }

    if (aDOMEvent) {
      // The number of touch objects in targetTouches list may change depending
      // on the retargeting.
      TouchEvent* touchDOMEvent = static_cast<TouchEvent*>(aDOMEvent);
      TouchList* targetTouches = touchDOMEvent->GetExistingTargetTouches();
      if (targetTouches) {
        targetTouches->Clear();
        if (mInitialTargetTouches.isSome()) {
          for (uint32_t i = 0; i < mInitialTargetTouches->Length(); ++i) {
            Touch* touch = mInitialTargetTouches->ElementAt(i);
            if (touch) {
              touch->mTarget = touch->mOriginalTarget;
            }
            targetTouches->Append(touch);
          }
        }
      }
    }
  }

  void SetInitialTargetTouches(
      Maybe<nsTArray<RefPtr<dom::Touch>>>&& aInitialTargetTouches) {
    mInitialTargetTouches = std::move(aInitialTargetTouches);
  }

  void SetForceContentDispatch(bool aForce) {
    mFlags.mForceContentDispatch = aForce;
  }

  bool ForceContentDispatch() const { return mFlags.mForceContentDispatch; }

  void SetWantsWillHandleEvent(bool aWants) {
    mFlags.mWantsWillHandleEvent = aWants;
  }

  bool WantsWillHandleEvent() const { return mFlags.mWantsWillHandleEvent; }

  void SetWantsPreHandleEvent(bool aWants) {
    mFlags.mWantsPreHandleEvent = aWants;
  }

  bool WantsPreHandleEvent() const { return mFlags.mWantsPreHandleEvent; }

  void SetPreHandleEventOnly(bool aWants) {
    mFlags.mPreHandleEventOnly = aWants;
  }

  bool PreHandleEventOnly() const { return mFlags.mPreHandleEventOnly; }

  void SetRootOfClosedTree(bool aSet) { mFlags.mRootOfClosedTree = aSet; }

  bool IsRootOfClosedTree() const { return mFlags.mRootOfClosedTree; }

  void SetItemInShadowTree(bool aSet) { mFlags.mItemInShadowTree = aSet; }

  bool IsItemInShadowTree() const { return mFlags.mItemInShadowTree; }

  void SetIsSlotInClosedTree(bool aSet) { mFlags.mIsSlotInClosedTree = aSet; }

  bool IsSlotInClosedTree() const { return mFlags.mIsSlotInClosedTree; }

  void SetIsChromeHandler(bool aSet) { mFlags.mIsChromeHandler = aSet; }

  bool IsChromeHandler() const { return mFlags.mIsChromeHandler; }

  void SetMayHaveListenerManager(bool aMayHave) {
    mFlags.mMayHaveManager = aMayHave;
  }

  bool MayHaveListenerManager() { return mFlags.mMayHaveManager; }

  EventTarget* CurrentTarget() const { return mTarget; }

  /**
   * Dispatches event through the event target chain.
   * Handles capture, target and bubble phases both in default
   * and system event group and calls also PostHandleEvent for each
   * item in the chain.
   */
  MOZ_CAN_RUN_SCRIPT
  static void HandleEventTargetChain(nsTArray<EventTargetChainItem>& aChain,
                                     EventChainPostVisitor& aVisitor,
                                     EventDispatchingCallback* aCallback,
                                     ELMCreationDetector& aCd);

  /**
   * Resets aVisitor object and calls GetEventTargetParent.
   * Copies mItemFlags and mItemData to the current EventTargetChainItem.
   */
  void GetEventTargetParent(EventChainPreVisitor& aVisitor);

  /**
   * Copies mItemFlags, mItemData to aVisitor,
   * calls LegacyPreActivationBehavior and copies both members back
   * to this EventTargetChainitem.
   */
  void LegacyPreActivationBehavior(EventChainVisitor& aVisitor);

  /**
   * Copies mItemFlags and mItemData to aVisitor and calls ActivationBehavior.
   */
  MOZ_CAN_RUN_SCRIPT
  void ActivationBehavior(EventChainPostVisitor& aVisitor);

  /**
   * Copies mItemFlags and mItemData to aVisitor and
   * calls LegacyCanceledActivationBehavior.
   */
  void LegacyCanceledActivationBehavior(EventChainPostVisitor& aVisitor);

  /**
   * Copies mItemFlags and mItemData to aVisitor.
   * Calls PreHandleEvent for those items which called SetWantsPreHandleEvent.
   */
  void PreHandleEvent(EventChainVisitor& aVisitor);

  /**
   * If the current item in the event target chain has an event listener
   * manager, this method calls EventListenerManager::HandleEvent().
   */
  void HandleEvent(EventChainPostVisitor& aVisitor, ELMCreationDetector& aCd) {
    if (WantsWillHandleEvent()) {
      mTarget->WillHandleEvent(aVisitor);
    }
    if (aVisitor.mEvent->PropagationStopped()) {
      return;
    }
    if (aVisitor.mEvent->mFlags.mOnlySystemGroupDispatch &&
        !aVisitor.mEvent->mFlags.mInSystemGroup) {
      return;
    }
    if (aVisitor.mEvent->mFlags.mOnlySystemGroupDispatchInContent &&
        !aVisitor.mEvent->mFlags.mInSystemGroup && !IsCurrentTargetChrome()) {
      return;
    }
    if (!mManager) {
      if (!MayHaveListenerManager() && !aCd.MayHaveNewListenerManager()) {
        return;
      }
      mManager = mTarget->GetExistingListenerManager();
    }
    if (mManager) {
      NS_ASSERTION(aVisitor.mEvent->mCurrentTarget == nullptr,
                   "CurrentTarget should be null!");

      mManager->HandleEvent(aVisitor.mPresContext, aVisitor.mEvent,
                            &aVisitor.mDOMEvent, CurrentTarget(),
                            &aVisitor.mEventStatus, IsItemInShadowTree());
      NS_ASSERTION(aVisitor.mEvent->mCurrentTarget == nullptr,
                   "CurrentTarget should be null!");
    }
  }

  /**
   * Copies mItemFlags and mItemData to aVisitor and calls PostHandleEvent.
   */
  MOZ_CAN_RUN_SCRIPT void PostHandleEvent(EventChainPostVisitor& aVisitor);

 private:
  const nsCOMPtr<EventTarget> mTarget;
  nsCOMPtr<EventTarget> mRetargetedRelatedTarget;
  Maybe<nsTArray<RefPtr<EventTarget>>> mRetargetedTouchTargets;
  Maybe<nsTArray<RefPtr<dom::Touch>>> mInitialTargetTouches;

  class EventTargetChainFlags {
   public:
    explicit EventTargetChainFlags() { SetRawFlags(0); }
    // Cached flags for each EventTargetChainItem which are set when calling
    // GetEventTargetParent to create event target chain. They are used to
    // manage or speedup event dispatching.
    bool mForceContentDispatch : 1;
    bool mWantsWillHandleEvent : 1;
    bool mMayHaveManager : 1;
    bool mChechedIfChrome : 1;
    bool mIsChromeContent : 1;
    bool mWantsPreHandleEvent : 1;
    bool mPreHandleEventOnly : 1;
    bool mRootOfClosedTree : 1;
    bool mItemInShadowTree : 1;
    bool mIsSlotInClosedTree : 1;
    bool mIsChromeHandler : 1;

   private:
    using RawFlags = uint32_t;
    void SetRawFlags(RawFlags aRawFlags) {
      static_assert(
          sizeof(EventTargetChainFlags) <= sizeof(RawFlags),
          "EventTargetChainFlags must not be bigger than the RawFlags");
      memcpy(this, &aRawFlags, sizeof(EventTargetChainFlags));
    }
  } mFlags;

  uint16_t mItemFlags;
  nsCOMPtr<nsISupports> mItemData;
  // Event retargeting must happen whenever mNewTarget is non-null.
  nsCOMPtr<EventTarget> mNewTarget;
  // Cache mTarget's event listener manager.
  RefPtr<EventListenerManager> mManager;

  bool IsCurrentTargetChrome() {
    if (!mFlags.mChechedIfChrome) {
      mFlags.mChechedIfChrome = true;
      if (IsEventTargetChrome(mTarget)) {
        mFlags.mIsChromeContent = true;
      }
    }
    return mFlags.mIsChromeContent;
  }
};

void EventTargetChainItem::GetEventTargetParent(
    EventChainPreVisitor& aVisitor) {
  aVisitor.Reset();
  mTarget->GetEventTargetParent(aVisitor);
  SetForceContentDispatch(aVisitor.mForceContentDispatch);
  SetWantsWillHandleEvent(aVisitor.mWantsWillHandleEvent);
  SetMayHaveListenerManager(aVisitor.mMayHaveListenerManager);
  SetWantsPreHandleEvent(aVisitor.mWantsPreHandleEvent);
  SetPreHandleEventOnly(aVisitor.mWantsPreHandleEvent && !aVisitor.mCanHandle);
  SetRootOfClosedTree(aVisitor.mRootOfClosedTree);
  SetItemInShadowTree(aVisitor.mItemInShadowTree);
  SetRetargetedRelatedTarget(aVisitor.mRetargetedRelatedTarget);
  SetRetargetedTouchTarget(std::move(aVisitor.mRetargetedTouchTargets));
  mItemFlags = aVisitor.mItemFlags;
  mItemData = aVisitor.mItemData;
}

void EventTargetChainItem::LegacyPreActivationBehavior(
    EventChainVisitor& aVisitor) {
  aVisitor.mItemFlags = mItemFlags;
  aVisitor.mItemData = mItemData;
  mTarget->LegacyPreActivationBehavior(aVisitor);
  mItemFlags = aVisitor.mItemFlags;
  mItemData = aVisitor.mItemData;
}

void EventTargetChainItem::PreHandleEvent(EventChainVisitor& aVisitor) {
  if (!WantsPreHandleEvent()) {
    return;
  }
  aVisitor.mItemFlags = mItemFlags;
  aVisitor.mItemData = mItemData;
  Unused << mTarget->PreHandleEvent(aVisitor);
  MOZ_ASSERT(mItemFlags == aVisitor.mItemFlags);
  MOZ_ASSERT(mItemData == aVisitor.mItemData);
}

void EventTargetChainItem::ActivationBehavior(EventChainPostVisitor& aVisitor) {
  aVisitor.mItemFlags = mItemFlags;
  aVisitor.mItemData = mItemData;
  mTarget->ActivationBehavior(aVisitor);
  MOZ_ASSERT(mItemFlags == aVisitor.mItemFlags);
  MOZ_ASSERT(mItemData == aVisitor.mItemData);
}

void EventTargetChainItem::LegacyCanceledActivationBehavior(
    EventChainPostVisitor& aVisitor) {
  aVisitor.mItemFlags = mItemFlags;
  aVisitor.mItemData = mItemData;
  mTarget->LegacyCanceledActivationBehavior(aVisitor);
  MOZ_ASSERT(mItemFlags == aVisitor.mItemFlags);
  MOZ_ASSERT(mItemData == aVisitor.mItemData);
}

void EventTargetChainItem::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  aVisitor.mItemFlags = mItemFlags;
  aVisitor.mItemData = mItemData;
  mTarget->PostHandleEvent(aVisitor);
  MOZ_ASSERT(mItemFlags == aVisitor.mItemFlags);
  MOZ_ASSERT(mItemData == aVisitor.mItemData);
}

void EventTargetChainItem::HandleEventTargetChain(
    nsTArray<EventTargetChainItem>& aChain, EventChainPostVisitor& aVisitor,
    EventDispatchingCallback* aCallback, ELMCreationDetector& aCd) {
  // Save the target so that it can be restored later.
  nsCOMPtr<EventTarget> firstTarget = aVisitor.mEvent->mTarget;
  nsCOMPtr<EventTarget> firstRelatedTarget = aVisitor.mEvent->mRelatedTarget;
  Maybe<AutoTArray<nsCOMPtr<EventTarget>, 10>> firstTouchTargets;
  WidgetTouchEvent* touchEvent = nullptr;
  if (aVisitor.mEvent->mClass == eTouchEventClass) {
    touchEvent = aVisitor.mEvent->AsTouchEvent();
    if (!aVisitor.mEvent->mFlags.mInSystemGroup) {
      firstTouchTargets.emplace();
      WidgetTouchEvent* touchEvent = aVisitor.mEvent->AsTouchEvent();
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      for (uint32_t i = 0; i < touches.Length(); ++i) {
        firstTouchTargets->AppendElement(touches[i]->mTarget);
      }
    }
  }

  uint32_t chainLength = aChain.Length();
  EventTargetChainItem* chain = aChain.Elements();
  uint32_t firstCanHandleEventTargetIdx =
      EventTargetChainItem::GetFirstCanHandleEventTargetIdx(aChain);

  // Capture
  aVisitor.mEvent->mFlags.mInCapturePhase = true;
  aVisitor.mEvent->mFlags.mInBubblingPhase = false;
  aVisitor.mEvent->mFlags.mInTargetPhase = false;
  for (uint32_t i = chainLength - 1; i > firstCanHandleEventTargetIdx; --i) {
    EventTargetChainItem& item = chain[i];
    if (item.PreHandleEventOnly()) {
      continue;
    }
    if ((!aVisitor.mEvent->mFlags.mNoContentDispatch ||
         item.ForceContentDispatch()) &&
        !aVisitor.mEvent->PropagationStopped()) {
      item.HandleEvent(aVisitor, aCd);
    }

    if (item.GetNewTarget()) {
      // item is at anonymous boundary. Need to retarget for the child items.
      for (uint32_t j = i; j > 0; --j) {
        uint32_t childIndex = j - 1;
        EventTarget* newTarget = chain[childIndex].GetNewTarget();
        if (newTarget) {
          aVisitor.mEvent->mTarget = newTarget;
          break;
        }
      }
    }

    // https://dom.spec.whatwg.org/#dispatching-events
    // Step 14.2
    // "Set event's relatedTarget to tuple's relatedTarget."
    // Note, the initial retargeting was done already when creating
    // event target chain, so we need to do this only after calling
    // HandleEvent, not before, like in the specification.
    if (item.GetRetargetedRelatedTarget()) {
      bool found = false;
      for (uint32_t j = i; j > 0; --j) {
        uint32_t childIndex = j - 1;
        EventTarget* relatedTarget =
            chain[childIndex].GetRetargetedRelatedTarget();
        if (relatedTarget) {
          found = true;
          aVisitor.mEvent->mRelatedTarget = relatedTarget;
          break;
        }
      }
      if (!found) {
        aVisitor.mEvent->mRelatedTarget =
            aVisitor.mEvent->mOriginalRelatedTarget;
      }
    }

    if (item.HasRetargetTouchTargets()) {
      bool found = false;
      for (uint32_t j = i; j > 0; --j) {
        uint32_t childIndex = j - 1;
        if (chain[childIndex].HasRetargetTouchTargets()) {
          found = true;
          chain[childIndex].RetargetTouchTargets(touchEvent,
                                                 aVisitor.mDOMEvent);
          break;
        }
      }
      if (!found) {
        WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
        for (uint32_t i = 0; i < touches.Length(); ++i) {
          touches[i]->mTarget = touches[i]->mOriginalTarget;
        }
      }
    }
  }

  // Target
  aVisitor.mEvent->mFlags.mInTargetPhase = true;
  EventTargetChainItem& targetItem = chain[firstCanHandleEventTargetIdx];
  // Need to explicitly retarget touch targets so that initial targets get set
  // properly in case nothing else retargeted touches.
  if (targetItem.HasRetargetTouchTargets()) {
    targetItem.RetargetTouchTargets(touchEvent, aVisitor.mDOMEvent);
  }
  if (!aVisitor.mEvent->PropagationStopped() &&
      (!aVisitor.mEvent->mFlags.mNoContentDispatch ||
       targetItem.ForceContentDispatch())) {
    targetItem.HandleEvent(aVisitor, aCd);
  }
  aVisitor.mEvent->mFlags.mInCapturePhase = false;
  aVisitor.mEvent->mFlags.mInBubblingPhase = true;
  if (!aVisitor.mEvent->PropagationStopped() &&
      (!aVisitor.mEvent->mFlags.mNoContentDispatch ||
       targetItem.ForceContentDispatch())) {
    targetItem.HandleEvent(aVisitor, aCd);
  }

  if (aVisitor.mEvent->mFlags.mInSystemGroup) {
    targetItem.PostHandleEvent(aVisitor);
  }
  aVisitor.mEvent->mFlags.mInTargetPhase = false;

  // Bubble
  for (uint32_t i = firstCanHandleEventTargetIdx + 1; i < chainLength; ++i) {
    EventTargetChainItem& item = chain[i];
    if (item.PreHandleEventOnly()) {
      continue;
    }
    EventTarget* newTarget = item.GetNewTarget();
    if (newTarget) {
      // Item is at anonymous boundary. Need to retarget for the current item
      // and for parent items.
      aVisitor.mEvent->mTarget = newTarget;
    }

    // https://dom.spec.whatwg.org/#dispatching-events
    // Step 15.2
    // "Set event's relatedTarget to tuple's relatedTarget."
    EventTarget* relatedTarget = item.GetRetargetedRelatedTarget();
    if (relatedTarget) {
      aVisitor.mEvent->mRelatedTarget = relatedTarget;
    }

    if (item.HasRetargetTouchTargets()) {
      item.RetargetTouchTargets(touchEvent, aVisitor.mDOMEvent);
    }

    if (aVisitor.mEvent->mFlags.mBubbles || newTarget) {
      if ((!aVisitor.mEvent->mFlags.mNoContentDispatch ||
           item.ForceContentDispatch()) &&
          !aVisitor.mEvent->PropagationStopped()) {
        item.HandleEvent(aVisitor, aCd);
      }
      if (aVisitor.mEvent->mFlags.mInSystemGroup) {
        item.PostHandleEvent(aVisitor);
      }
    }
  }
  aVisitor.mEvent->mFlags.mInBubblingPhase = false;

  if (!aVisitor.mEvent->mFlags.mInSystemGroup &&
      aVisitor.mEvent->IsAllowedToDispatchInSystemGroup()) {
    // Dispatch to the system event group.  Make sure to clear the
    // STOP_DISPATCH flag since this resets for each event group.
    aVisitor.mEvent->mFlags.mPropagationStopped = false;
    aVisitor.mEvent->mFlags.mImmediatePropagationStopped = false;

    // Setting back the original target of the event.
    aVisitor.mEvent->mTarget = aVisitor.mEvent->mOriginalTarget;
    aVisitor.mEvent->mRelatedTarget = aVisitor.mEvent->mOriginalRelatedTarget;
    if (firstTouchTargets) {
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      for (uint32_t i = 0; i < touches.Length(); ++i) {
        touches[i]->mTarget = touches[i]->mOriginalTarget;
      }
    }

    // Special handling if PresShell (or some other caller)
    // used a callback object.
    if (aCallback) {
      aCallback->HandleEvent(aVisitor);
    }

    // Retarget for system event group (which does the default handling too).
    // Setting back the target which was used also for default event group.
    aVisitor.mEvent->mTarget = firstTarget;
    aVisitor.mEvent->mRelatedTarget = firstRelatedTarget;
    if (firstTouchTargets) {
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      for (uint32_t i = 0; i < firstTouchTargets->Length(); ++i) {
        touches[i]->mTarget = firstTouchTargets->ElementAt(i);
      }
    }

    aVisitor.mEvent->mFlags.mInSystemGroup = true;
    HandleEventTargetChain(aChain, aVisitor, aCallback, aCd);
    aVisitor.mEvent->mFlags.mInSystemGroup = false;

    // After dispatch, clear all the propagation flags so that
    // system group listeners don't affect to the event.
    aVisitor.mEvent->mFlags.mPropagationStopped = false;
    aVisitor.mEvent->mFlags.mImmediatePropagationStopped = false;
  }
}

// There are often 2 nested event dispatches ongoing at the same time, so
// have 2 separate caches.
static const uint32_t kCachedMainThreadChainSize = 128;
struct CachedChains {
  nsTArray<EventTargetChainItem> mChain1;
  nsTArray<EventTargetChainItem> mChain2;
};
static CachedChains* sCachedMainThreadChains = nullptr;

/* static */
void EventDispatcher::Shutdown() {
  delete sCachedMainThreadChains;
  sCachedMainThreadChains = nullptr;
}

EventTargetChainItem* EventTargetChainItemForChromeTarget(
    nsTArray<EventTargetChainItem>& aChain, nsINode* aNode,
    EventTargetChainItem* aChild = nullptr) {
  if (!aNode->IsInComposedDoc()) {
    return nullptr;
  }
  nsPIDOMWindowInner* win = aNode->OwnerDoc()->GetInnerWindow();
  EventTarget* piTarget = win ? win->GetParentTarget() : nullptr;
  NS_ENSURE_TRUE(piTarget, nullptr);

  EventTargetChainItem* etci = EventTargetChainItem::Create(
      aChain, piTarget->GetTargetForEventTargetChain(), aChild);
  if (!etci->IsValid()) {
    EventTargetChainItem::DestroyLast(aChain, etci);
    return nullptr;
  }
  return etci;
}

/* static */ EventTargetChainItem* MayRetargetToChromeIfCanNotHandleEvent(
    nsTArray<EventTargetChainItem>& aChain, EventChainPreVisitor& aPreVisitor,
    EventTargetChainItem* aTargetEtci, EventTargetChainItem* aChildEtci,
    nsINode* aContent) {
  if (!aPreVisitor.mWantsPreHandleEvent) {
    // Keep EventTargetChainItem if we need to call PreHandleEvent on it.
    EventTargetChainItem::DestroyLast(aChain, aTargetEtci);
  }
  if (aPreVisitor.mAutomaticChromeDispatch && aContent) {
    aPreVisitor.mRelatedTargetRetargetedInCurrentScope = false;
    // Event target couldn't handle the event. Try to propagate to chrome.
    EventTargetChainItem* chromeTargetEtci =
        EventTargetChainItemForChromeTarget(aChain, aContent, aChildEtci);
    if (chromeTargetEtci) {
      // If we propagate to chrome, need to ensure we mark
      // EventTargetChainItem to be chrome handler so that event.composedPath()
      // can return the right value.
      chromeTargetEtci->SetIsChromeHandler(true);
      chromeTargetEtci->GetEventTargetParent(aPreVisitor);
      return chromeTargetEtci;
    }
  }
  return nullptr;
}

static bool ShouldClearTargets(WidgetEvent* aEvent) {
  if (auto* finalTarget = nsIContent::FromEventTargetOrNull(aEvent->mTarget)) {
    if (finalTarget->IsInShadowTree()) {
      return true;
    }
  }

  if (auto* finalRelatedTarget =
          nsIContent::FromEventTargetOrNull(aEvent->mRelatedTarget)) {
    if (finalRelatedTarget->IsInShadowTree()) {
      return true;
    }
  }
  // XXXsmaug Check also all the touch objects.

  return false;
}

static void DescribeEventTargetForProfilerMarker(const EventTarget* aTarget,
                                                 nsACString& aDescription) {
  auto* node = aTarget->GetAsNode();
  if (node) {
    if (node->IsElement()) {
      nsAutoString nodeDescription;
      node->AsElement()->Describe(nodeDescription, true);
      aDescription = NS_ConvertUTF16toUTF8(nodeDescription);
    } else if (node->IsDocument()) {
      aDescription.AssignLiteral("document");
    } else if (node->IsText()) {
      aDescription.AssignLiteral("text");
    } else if (node->IsDocumentFragment()) {
      aDescription.AssignLiteral("document fragment");
    }
  } else if (aTarget->IsInnerWindow() || aTarget->IsOuterWindow()) {
    aDescription.AssignLiteral("window");
  } else if (aTarget->IsRootWindow()) {
    aDescription.AssignLiteral("root window");
  } else {
    // Probably something that inherits from DOMEventTargetHelper.
  }
}

/**
 * https://w3c.github.io/touch-events/#cancelability
 * https://w3c.github.io/uievents/#cancelability-of-wheel-events
 */
static bool IsUncancelableIfOnlyPassiveListeners(const WidgetEvent* aEvent) {
  if (!aEvent->IsTrusted() || !aEvent->mFlags.mCancelable) {
    return false;
  }

  switch (aEvent->mMessage) {
    case eTouchStart:
    case eTouchEnd:
    case eTouchMove:
    case eWheel:
    case eLegacyMouseLineOrPageScroll:
    case eLegacyMousePixelScroll:
      break;
    default:
      return false;
  }

  // There might be non-passive listeners in the remote document
  // So return false if we are in the parent process with remote target
  nsCOMPtr<nsIContent> target = nsIContent::FromEventTargetOrNull(aEvent->mOriginalTarget);
  return !(XRE_IsParentProcess() && BrowserParent::GetFrom(target));
}

/* static */
nsresult EventDispatcher::Dispatch(EventTarget* aTarget,
                                   nsPresContext* aPresContext,
                                   WidgetEvent* aEvent, Event* aDOMEvent,
                                   nsEventStatus* aEventStatus,
                                   EventDispatchingCallback* aCallback,
                                   nsTArray<EventTarget*>* aTargets) {
  AUTO_PROFILER_LABEL_HOT("EventDispatcher::Dispatch", OTHER);

  NS_ASSERTION(aEvent, "Trying to dispatch without WidgetEvent!");
  NS_ENSURE_TRUE(!aEvent->mFlags.mIsBeingDispatched,
                 NS_ERROR_DOM_INVALID_STATE_ERR);
  NS_ASSERTION(!aTargets || !aEvent->mMessage, "Wrong parameters!");

  // If we're dispatching an already created DOMEvent object, make
  // sure it is initialized!
  // If aTargets is non-null, the event isn't going to be dispatched.
  NS_ENSURE_TRUE(aEvent->mMessage || !aDOMEvent || aTargets,
                 NS_ERROR_DOM_INVALID_STATE_ERR);

  // Events shall not be fired while we are in stable state to prevent anything
  // visible from the scripts.
  MOZ_ASSERT(!nsContentUtils::IsInStableOrMetaStableState());
  NS_ENSURE_TRUE(!nsContentUtils::IsInStableOrMetaStableState(),
                 NS_ERROR_DOM_INVALID_STATE_ERR);

  nsCOMPtr<EventTarget> target(aTarget);

  RefPtr<PerformanceEventTiming> eventTimingEntry;
  // Similar to PerformancePaintTiming, we don't need to
  // expose them for printing documents
  if (aPresContext && !aPresContext->IsPrintingOrPrintPreview()) {
    eventTimingEntry =
        PerformanceEventTiming::TryGenerateEventTiming(target, aEvent);

    if (aEvent->IsTrusted() && aEvent->mMessage == eScroll) {
      if (auto* perf = aPresContext->GetPerformanceMainThread()) {
        if (!perf->HasDispatchedScrollEvent()) {
          perf->SetHasDispatchedScrollEvent();
        }
      }
    }
  }

  bool retargeted = false;

  if (aEvent->mFlags.mRetargetToNonNativeAnonymous) {
    nsIContent* content = nsIContent::FromEventTargetOrNull(target);
    if (content && content->IsInNativeAnonymousSubtree()) {
      nsCOMPtr<EventTarget> newTarget =
          content->FindFirstNonChromeOnlyAccessContent();
      NS_ENSURE_STATE(newTarget);

      aEvent->mOriginalTarget = target;
      target = newTarget;
      retargeted = true;
    }
  }

  if (aEvent->mFlags.mOnlyChromeDispatch) {
    nsCOMPtr<Document> doc;
    if (!IsEventTargetChrome(target, getter_AddRefs(doc)) && doc) {
      nsPIDOMWindowInner* win = doc->GetInnerWindow();
      // If we can't dispatch the event to chrome, do nothing.
      EventTarget* piTarget = win ? win->GetParentTarget() : nullptr;
      if (!piTarget) {
        return NS_OK;
      }

      // Set the target to be the original dispatch target,
      aEvent->mTarget = target;
      // but use chrome event handler or BrowserChildMessageManager for event
      // target chain.
      target = piTarget;
    } else if (NS_WARN_IF(!doc)) {
      return NS_ERROR_UNEXPECTED;
    }
  }

#ifdef DEBUG
  if (NS_IsMainThread() && aEvent->mMessage != eVoidEvent &&
      !nsContentUtils::IsSafeToRunScript()) {
    static const auto warn = [](bool aIsSystem) {
      if (aIsSystem) {
        NS_WARNING("Fix the caller!");
      } else {
        MOZ_CRASH("This is unsafe! Fix the caller!");
      }
    };
    if (nsINode* node = nsINode::FromEventTargetOrNull(target)) {
      // If this is a node, it's possible that this is some sort of DOM tree
      // that is never accessed by script (for example an SVG image or XBL
      // binding document or whatnot).  We really only want to warn/assert here
      // if there might be actual scripted listeners for this event, so restrict
      // the warnings/asserts to the case when script can or once could touch
      // this node's document.
      Document* doc = node->OwnerDoc();
      bool hasHadScriptHandlingObject;
      nsIGlobalObject* global =
          doc->GetScriptHandlingObject(hasHadScriptHandlingObject);
      if (global || hasHadScriptHandlingObject) {
        warn(nsContentUtils::IsChromeDoc(doc));
      }
    } else if (nsCOMPtr<nsIGlobalObject> global = target->GetOwnerGlobal()) {
      warn(global->PrincipalOrNull()->IsSystemPrincipal());
    }
  }

  if (aDOMEvent) {
    WidgetEvent* innerEvent = aDOMEvent->WidgetEventPtr();
    NS_ASSERTION(innerEvent == aEvent,
                 "The inner event of aDOMEvent is not the same as aEvent!");
  }
#endif

  nsresult rv = NS_OK;
  bool externalDOMEvent = !!(aDOMEvent);

  // If we have a PresContext, make sure it doesn't die before
  // event dispatching is finished.
  RefPtr<nsPresContext> kungFuDeathGrip(aPresContext);

  ELMCreationDetector cd;
  nsTArray<EventTargetChainItem> chain;
  if (cd.IsMainThread()) {
    if (!sCachedMainThreadChains) {
      sCachedMainThreadChains = new CachedChains();
    }

    if (sCachedMainThreadChains->mChain1.Capacity() ==
        kCachedMainThreadChainSize) {
      chain = std::move(sCachedMainThreadChains->mChain1);
    } else if (sCachedMainThreadChains->mChain2.Capacity() ==
               kCachedMainThreadChainSize) {
      chain = std::move(sCachedMainThreadChains->mChain2);
    } else {
      chain.SetCapacity(kCachedMainThreadChainSize);
    }
  }

  // Create the event target chain item for the event target.
  EventTargetChainItem* targetEtci = EventTargetChainItem::Create(
      chain, target->GetTargetForEventTargetChain());
  MOZ_ASSERT(&chain[0] == targetEtci);
  if (!targetEtci->IsValid()) {
    EventTargetChainItem::DestroyLast(chain, targetEtci);
    return NS_ERROR_FAILURE;
  }

  // Make sure that Event::target and Event::originalTarget
  // point to the last item in the chain.
  if (!aEvent->mTarget) {
    // Note, CurrentTarget() points always to the object returned by
    // GetTargetForEventTargetChain().
    aEvent->mTarget = targetEtci->CurrentTarget();
  } else {
    // XXX But if the target is already set, use that. This is a hack
    //     for the 'load', 'beforeunload' and 'unload' events,
    //     which are dispatched to |window| but have document as their target.
    //
    // Make sure that the event target points to the right object.
    aEvent->mTarget = aEvent->mTarget->GetTargetForEventTargetChain();
    NS_ENSURE_STATE(aEvent->mTarget);
  }

  if (retargeted) {
    aEvent->mOriginalTarget =
        aEvent->mOriginalTarget->GetTargetForEventTargetChain();
    NS_ENSURE_STATE(aEvent->mOriginalTarget);
  } else {
    aEvent->mOriginalTarget = aEvent->mTarget;
  }

  aEvent->mOriginalRelatedTarget = aEvent->mRelatedTarget;

  bool clearTargets = false;

  nsCOMPtr<nsIContent> content =
      nsIContent::FromEventTargetOrNull(aEvent->mOriginalTarget);

  const bool isInAnon = content && content->ChromeOnlyAccessForEvents();
  aEvent->mFlags.mIsBeingDispatched = true;

  Maybe<uint32_t> activationTargetItemIndex;

  // https://w3c.github.io/touch-events/#cancelability
  bool maybeUncancelable = IsUncancelableIfOnlyPassiveListeners(aEvent);

  // Create visitor object and start event dispatching.
  // GetEventTargetParent for the original target.
  nsEventStatus status = aDOMEvent && aDOMEvent->DefaultPrevented()
                             ? nsEventStatus_eConsumeNoDefault
                         : aEventStatus ? *aEventStatus
                                        : nsEventStatus_eIgnore;
  nsCOMPtr<EventTarget> targetForPreVisitor = aEvent->mTarget;
  EventChainPreVisitor preVisitor(aPresContext, aEvent, aDOMEvent, status,
                                  isInAnon, targetForPreVisitor);
  targetEtci->GetEventTargetParent(preVisitor);

  if (preVisitor.mWantsActivationBehavior) {
    MOZ_ASSERT(&chain[0] == targetEtci);
    activationTargetItemIndex.emplace(0);
  }

  if (!preVisitor.mCanHandle) {
    targetEtci = MayRetargetToChromeIfCanNotHandleEvent(
        chain, preVisitor, targetEtci, nullptr, content);
  }
  if (!preVisitor.mCanHandle) {
    // The original target and chrome target (mAutomaticChromeDispatch=true)
    // can not handle the event but we still have to call their PreHandleEvent.
    for (uint32_t i = 0; i < chain.Length(); ++i) {
      chain[i].PreHandleEvent(preVisitor);
    }

    clearTargets = ShouldClearTargets(aEvent);
  } else {
    if (maybeUncancelable && preVisitor.mMayHaveListenerManager) {
      if (EventListenerManager* const manager =
              targetEtci->CurrentTarget()->GetExistingListenerManager()) {
        maybeUncancelable = !manager->HasNonPassiveListenersFor(aEvent);
      }
    }

    // At least the original target can handle the event.
    // Setting the retarget to the |target| simplifies retargeting code.
    nsCOMPtr<EventTarget> t = aEvent->mTarget;
    targetEtci->SetNewTarget(t);
    // In order to not change the targetTouches array passed to TouchEvents
    // when dispatching events from JS, we need to store the initial Touch
    // objects on the list.
    if (aEvent->mClass == eTouchEventClass && aDOMEvent) {
      TouchEvent* touchEvent = static_cast<TouchEvent*>(aDOMEvent);
      TouchList* targetTouches = touchEvent->GetExistingTargetTouches();
      if (targetTouches) {
        Maybe<nsTArray<RefPtr<dom::Touch>>> initialTargetTouches;
        initialTargetTouches.emplace();
        for (uint32_t i = 0; i < targetTouches->Length(); ++i) {
          initialTargetTouches->AppendElement(targetTouches->Item(i));
        }
        targetEtci->SetInitialTargetTouches(std::move(initialTargetTouches));
        targetTouches->Clear();
      }
    }
    EventTargetChainItem* topEtci = targetEtci;
    targetEtci = nullptr;
    while (preVisitor.GetParentTarget()) {
      EventTarget* parentTarget = preVisitor.GetParentTarget();
      EventTargetChainItem* parentEtci =
          EventTargetChainItem::Create(chain, parentTarget, topEtci);
      if (!parentEtci->IsValid()) {
        EventTargetChainItem::DestroyLast(chain, parentEtci);
        rv = NS_ERROR_FAILURE;
        break;
      }

      parentEtci->SetIsSlotInClosedTree(preVisitor.mParentIsSlotInClosedTree);
      parentEtci->SetIsChromeHandler(preVisitor.mParentIsChromeHandler);

      // Item needs event retargetting.
      if (preVisitor.mEventTargetAtParent) {
        // Need to set the target of the event
        // so that also the next retargeting works.
        preVisitor.mTargetInKnownToBeHandledScope = preVisitor.mEvent->mTarget;
        preVisitor.mEvent->mTarget = preVisitor.mEventTargetAtParent;
        parentEtci->SetNewTarget(preVisitor.mEventTargetAtParent);
      }

      if (preVisitor.mRetargetedRelatedTarget) {
        preVisitor.mEvent->mRelatedTarget = preVisitor.mRetargetedRelatedTarget;
      }

      parentEtci->GetEventTargetParent(preVisitor);

      if (preVisitor.mWantsActivationBehavior &&
          activationTargetItemIndex.isNothing() && aEvent->mFlags.mBubbles) {
        MOZ_ASSERT(&chain.LastElement() == parentEtci);
        activationTargetItemIndex.emplace(chain.Length() - 1);
      }

      if (!preVisitor.mCanHandle) {
        bool ignoreBecauseOfShadowDOM = preVisitor.mIgnoreBecauseOfShadowDOM;
        nsCOMPtr<nsINode> disabledTarget =
            nsINode::FromEventTargetOrNull(parentTarget);
        parentEtci = MayRetargetToChromeIfCanNotHandleEvent(
            chain, preVisitor, parentEtci, topEtci, disabledTarget);
        if (parentEtci && preVisitor.mCanHandle) {
          EventTargetChainItem* item =
              EventTargetChainItem::GetFirstCanHandleEventTarget(chain);
          if (!ignoreBecauseOfShadowDOM) {
            // If we ignored the target because of Shadow DOM retargeting, we
            // shouldn't treat the target to be in the event path at all.
            item->SetNewTarget(parentTarget);
          }
        }
      }

      if (parentEtci && preVisitor.mCanHandle) {
        preVisitor.mTargetInKnownToBeHandledScope = preVisitor.mEvent->mTarget;
        topEtci = parentEtci;
      } else {
        break;
      }

      if (maybeUncancelable && preVisitor.mMayHaveListenerManager) {
        if (EventListenerManager* const manager =
                parentEtci->CurrentTarget()->GetExistingListenerManager()) {
          maybeUncancelable = !manager->HasNonPassiveListenersFor(aEvent);
        }
      }
    }

    if (activationTargetItemIndex) {
      chain[activationTargetItemIndex.value()].LegacyPreActivationBehavior(
          preVisitor);
    }

    if (NS_SUCCEEDED(rv)) {
      if (maybeUncancelable) {
        aEvent->mFlags.mCancelable = false;
      }

      if (aTargets) {
        aTargets->Clear();
        uint32_t numTargets = chain.Length();
        EventTarget** targets = aTargets->AppendElements(numTargets);
        for (uint32_t i = 0; i < numTargets; ++i) {
          targets[i] = chain[i].CurrentTarget()->GetTargetForDOMEvent();
        }
      } else {
        // Event target chain is created. PreHandle the chain.
        for (uint32_t i = 0; i < chain.Length(); ++i) {
          chain[i].PreHandleEvent(preVisitor);
        }

        RefPtr<nsRefreshDriver> refreshDriver;
        if (aEvent->IsTrusted() &&
            (aEvent->mMessage == eKeyPress ||
             aEvent->mMessage == ePointerClick) &&
            aPresContext && aPresContext->GetRootPresContext()) {
          refreshDriver = aPresContext->GetRootPresContext()->RefreshDriver();
          if (refreshDriver) {
            refreshDriver->EnterUserInputProcessing();
          }
        }
        auto cleanup = MakeScopeExit([&] {
          if (refreshDriver) {
            refreshDriver->ExitUserInputProcessing();
          }
        });

        clearTargets = ShouldClearTargets(aEvent);

        // Handle the chain.
        EventChainPostVisitor postVisitor(preVisitor);
        MOZ_RELEASE_ASSERT(!aEvent->mPath);
        aEvent->mPath = &chain;

        if (profiler_is_active()) {
          // Add a profiler label and a profiler marker for the actual
          // dispatch of the event.
          // This is a very hot code path, so we need to make sure not to
          // do this extra work when we're not profiling.
          if (!postVisitor.mDOMEvent) {
            // This is tiny bit slow, but happens only once per event.
            // Similar code also in EventListenerManager.
            RefPtr<Event> event = EventDispatcher::CreateEvent(
                aEvent->mOriginalTarget, aPresContext, aEvent, u""_ns);
            event.swap(postVisitor.mDOMEvent);
          }
          nsAutoString typeStr;
          postVisitor.mDOMEvent->GetType(typeStr);
          AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING(
              "EventDispatcher::Dispatch", OTHER, typeStr);

          MarkerInnerWindowId innerWindowId;
          if (nsIGlobalObject* global = aEvent->mTarget->GetOwnerGlobal()) {
            if (nsPIDOMWindowInner* inner = global->GetAsInnerWindow()) {
              innerWindowId = MarkerInnerWindowId{inner->WindowID()};
            }
          }

          struct DOMEventMarker {
            static constexpr Span<const char> MarkerTypeName() {
              return MakeStringSpan("DOMEvent");
            }
            static void StreamJSONMarkerData(
                baseprofiler::SpliceableJSONWriter& aWriter,
                const ProfilerString16View& aEventType,
                const nsCString& aTarget, const TimeStamp& aStartTime,
                const TimeStamp& aEventTimeStamp) {
              aWriter.StringProperty("eventType",
                                     NS_ConvertUTF16toUTF8(aEventType));
              if (!aTarget.IsEmpty()) {
                aWriter.StringProperty("target", aTarget);
              }
              // This is the event processing latency, which is the time from
              // when the event was created, to when it was started to be
              // processed. Note that the computation of this latency is
              // deferred until serialization time, at the expense of some extra
              // memory.
              aWriter.DoubleProperty(
                  "latency", (aStartTime - aEventTimeStamp).ToMilliseconds());
            }
            static MarkerSchema MarkerTypeDisplay() {
              using MS = MarkerSchema;
              MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable,
                        MS::Location::TimelineOverview};
              schema.SetChartLabel("{marker.data.eventType}");
              schema.SetTooltipLabel("{marker.data.eventType} - DOMEvent");
              schema.SetTableLabel(
                  "{marker.data.eventType} - {marker.data.target}");
              schema.AddKeyLabelFormatSearchable("target", "Event Target",
                                                 MS::Format::String,
                                                 MS::Searchable::Searchable);
              schema.AddKeyLabelFormat("latency", "Latency",
                                       MS::Format::Duration);
              schema.AddKeyLabelFormatSearchable("eventType", "Event Type",
                                                 MS::Format::String,
                                                 MS::Searchable::Searchable);
              return schema;
            }
          };

          nsAutoCString target;
          DescribeEventTargetForProfilerMarker(aEvent->mTarget, target);

          auto startTime = TimeStamp::Now();
          profiler_add_marker("DOMEvent", geckoprofiler::category::DOM,
                              {MarkerTiming::IntervalStart(startTime),
                               MarkerInnerWindowId(innerWindowId)},
                              DOMEventMarker{}, typeStr, target, startTime,
                              aEvent->mTimeStamp);

          EventTargetChainItem::HandleEventTargetChain(chain, postVisitor,
                                                       aCallback, cd);

          profiler_add_marker(
              "DOMEvent", geckoprofiler::category::DOM,
              {MarkerTiming::IntervalEnd(), std::move(innerWindowId)},
              DOMEventMarker{}, typeStr, target, startTime, aEvent->mTimeStamp);
        } else {
          EventTargetChainItem::HandleEventTargetChain(chain, postVisitor,
                                                       aCallback, cd);
        }
        aEvent->mPath = nullptr;

        if (aEvent->IsTrusted() &&
            (aEvent->mMessage == eKeyPress ||
             aEvent->mMessage == ePointerClick) &&
            aPresContext && aPresContext->GetRootPresContext()) {
          nsRefreshDriver* driver =
              aPresContext->GetRootPresContext()->RefreshDriver();
          if (driver && driver->HasPendingTick()) {
            switch (aEvent->mMessage) {
              case eKeyPress:
                driver->RegisterCompositionPayload(
                    {layers::CompositionPayloadType::eKeyPress,
                     aEvent->mTimeStamp});
                break;
              case ePointerClick: {
                if (aEvent->AsMouseEvent()->mInputSource ==
                        MouseEvent_Binding::MOZ_SOURCE_MOUSE ||
                    aEvent->AsMouseEvent()->mInputSource ==
                        MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
                  driver->RegisterCompositionPayload(
                      {layers::CompositionPayloadType::eMouseUpFollowedByClick,
                       aEvent->mTimeStamp});
                }
                break;
              }
              default:
                break;
            }
          }
        }

        preVisitor.mEventStatus = postVisitor.mEventStatus;
        // If the DOM event was created during event flow.
        if (!preVisitor.mDOMEvent && postVisitor.mDOMEvent) {
          preVisitor.mDOMEvent = postVisitor.mDOMEvent;
        }
      }
    }
  }

  // Note, EventTargetChainItem objects are deleted when the chain goes out of
  // the scope.

  aEvent->mFlags.mIsBeingDispatched = false;
  aEvent->mFlags.mDispatchedAtLeastOnce = true;

  if (eventTimingEntry) {
    eventTimingEntry->FinalizeEventTiming(aEvent);
  }
  // https://dom.spec.whatwg.org/#concept-event-dispatch
  // step 10. If clearTargets, then:
  //          1. Set event's target to null.
  //          2. Set event's relatedTarget to null.
  //          3. Set event's touch target list to the empty list.
  if (clearTargets) {
    aEvent->mTarget = nullptr;
    aEvent->mOriginalTarget = nullptr;
    aEvent->mRelatedTarget = nullptr;
    aEvent->mOriginalRelatedTarget = nullptr;
    // XXXsmaug Check also all the touch objects.
  }

  if (activationTargetItemIndex) {
    EventChainPostVisitor postVisitor(preVisitor);
    if (preVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault) {
      chain[activationTargetItemIndex.value()].LegacyCanceledActivationBehavior(
          postVisitor);
    } else {
      chain[activationTargetItemIndex.value()].ActivationBehavior(postVisitor);
    }
    preVisitor.mEventStatus = postVisitor.mEventStatus;
    // If the DOM event was created during event flow.
    if (!preVisitor.mDOMEvent && postVisitor.mDOMEvent) {
      preVisitor.mDOMEvent = postVisitor.mDOMEvent;
    }
  }

  if (!externalDOMEvent && preVisitor.mDOMEvent) {
    // A dom::Event was created while dispatching the event.
    // Duplicate private data if someone holds a pointer to it.
    nsrefcnt rc = 0;
    NS_RELEASE2(preVisitor.mDOMEvent, rc);
    if (preVisitor.mDOMEvent) {
      preVisitor.mDOMEvent->DuplicatePrivateData();
    }
  }

  if (aEventStatus) {
    *aEventStatus = preVisitor.mEventStatus;
  }

  if (cd.IsMainThread() && chain.Capacity() == kCachedMainThreadChainSize &&
      sCachedMainThreadChains) {
    if (sCachedMainThreadChains->mChain1.Capacity() !=
        kCachedMainThreadChainSize) {
      chain.ClearAndRetainStorage();
      chain.SwapElements(sCachedMainThreadChains->mChain1);
    } else if (sCachedMainThreadChains->mChain2.Capacity() !=
               kCachedMainThreadChainSize) {
      chain.ClearAndRetainStorage();
      chain.SwapElements(sCachedMainThreadChains->mChain2);
    }
  }

  return rv;
}

/* static */
nsresult EventDispatcher::DispatchDOMEvent(EventTarget* aTarget,
                                           WidgetEvent* aEvent,
                                           Event* aDOMEvent,
                                           nsPresContext* aPresContext,
                                           nsEventStatus* aEventStatus) {
  if (aDOMEvent) {
    WidgetEvent* innerEvent = aDOMEvent->WidgetEventPtr();
    NS_ENSURE_TRUE(innerEvent, NS_ERROR_ILLEGAL_VALUE);

    // Don't modify the event if it's being dispatched right now.
    if (innerEvent->mFlags.mIsBeingDispatched) {
      return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    bool dontResetTrusted = false;
    if (innerEvent->mFlags.mDispatchedAtLeastOnce) {
      innerEvent->mTarget = nullptr;
      innerEvent->mOriginalTarget = nullptr;
    } else {
      dontResetTrusted = aDOMEvent->IsTrusted();
    }

    if (!dontResetTrusted) {
      // Check security state to determine if dispatcher is trusted
      bool trusted = NS_IsMainThread()
                         ? nsContentUtils::LegacyIsCallerChromeOrNativeCode()
                         : IsCurrentThreadRunningChromeWorker();
      aDOMEvent->SetTrusted(trusted);
    }

    return EventDispatcher::Dispatch(aTarget, aPresContext, innerEvent,
                                     aDOMEvent, aEventStatus);
  } else if (aEvent) {
    return EventDispatcher::Dispatch(aTarget, aPresContext, aEvent, aDOMEvent,
                                     aEventStatus);
  }
  return NS_ERROR_ILLEGAL_VALUE;
}

/* static */ already_AddRefed<dom::Event> EventDispatcher::CreateEvent(
    EventTarget* aOwner, nsPresContext* aPresContext, WidgetEvent* aEvent,
    const nsAString& aEventType, CallerType aCallerType) {
  if (aEvent) {
    switch (aEvent->mClass) {
      case eMutationEventClass:
        return NS_NewDOMMutationEvent(aOwner, aPresContext,
                                      aEvent->AsMutationEvent());
      case eGUIEventClass:
      case eScrollPortEventClass:
      case eUIEventClass:
        return NS_NewDOMUIEvent(aOwner, aPresContext, aEvent->AsGUIEvent());
      case eScrollAreaEventClass:
        return NS_NewDOMScrollAreaEvent(aOwner, aPresContext,
                                        aEvent->AsScrollAreaEvent());
      case eKeyboardEventClass:
        return NS_NewDOMKeyboardEvent(aOwner, aPresContext,
                                      aEvent->AsKeyboardEvent());
      case eCompositionEventClass:
        return NS_NewDOMCompositionEvent(aOwner, aPresContext,
                                         aEvent->AsCompositionEvent());
      case eMouseEventClass:
        return NS_NewDOMMouseEvent(aOwner, aPresContext,
                                   aEvent->AsMouseEvent());
      case eFocusEventClass:
        return NS_NewDOMFocusEvent(aOwner, aPresContext,
                                   aEvent->AsFocusEvent());
      case eMouseScrollEventClass:
        return NS_NewDOMMouseScrollEvent(aOwner, aPresContext,
                                         aEvent->AsMouseScrollEvent());
      case eWheelEventClass:
        return NS_NewDOMWheelEvent(aOwner, aPresContext,
                                   aEvent->AsWheelEvent());
      case eEditorInputEventClass:
        return NS_NewDOMInputEvent(aOwner, aPresContext,
                                   aEvent->AsEditorInputEvent());
      case eLegacyTextEventClass:
        return NS_NewDOMTextEvent(aOwner, aPresContext,
                                  aEvent->AsLegacyTextEvent());
      case eDragEventClass:
        return NS_NewDOMDragEvent(aOwner, aPresContext, aEvent->AsDragEvent());
      case eClipboardEventClass:
        return NS_NewDOMClipboardEvent(aOwner, aPresContext,
                                       aEvent->AsClipboardEvent());
      case eSMILTimeEventClass:
        return NS_NewDOMTimeEvent(aOwner, aPresContext,
                                  aEvent->AsSMILTimeEvent());
      case eCommandEventClass:
        return NS_NewDOMCommandEvent(aOwner, aPresContext,
                                     aEvent->AsCommandEvent());
      case eSimpleGestureEventClass:
        return NS_NewDOMSimpleGestureEvent(aOwner, aPresContext,
                                           aEvent->AsSimpleGestureEvent());
      case ePointerEventClass:
        return NS_NewDOMPointerEvent(aOwner, aPresContext,
                                     aEvent->AsPointerEvent());
      case eTouchEventClass:
        return NS_NewDOMTouchEvent(aOwner, aPresContext,
                                   aEvent->AsTouchEvent());
      case eTransitionEventClass:
        return NS_NewDOMTransitionEvent(aOwner, aPresContext,
                                        aEvent->AsTransitionEvent());
      case eAnimationEventClass:
        return NS_NewDOMAnimationEvent(aOwner, aPresContext,
                                       aEvent->AsAnimationEvent());
      default:
        // For all other types of events, create a vanilla event object.
        return NS_NewDOMEvent(aOwner, aPresContext, aEvent);
    }
  }

  // And if we didn't get an event, check the type argument.

  if (aEventType.LowerCaseEqualsLiteral("mouseevent") ||
      aEventType.LowerCaseEqualsLiteral("mouseevents")) {
    return NS_NewDOMMouseEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("dragevent")) {
    return NS_NewDOMDragEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("keyboardevent")) {
    return NS_NewDOMKeyboardEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("compositionevent")) {
    return NS_NewDOMCompositionEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("textevent")) {
    if (!StaticPrefs::dom_events_textevent_enabled()) {
      return NS_NewDOMCompositionEvent(aOwner, aPresContext, nullptr);
    }
    return NS_NewDOMTextEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("mutationevent") ||
      aEventType.LowerCaseEqualsLiteral("mutationevents")) {
    return NS_NewDOMMutationEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("deviceorientationevent")) {
    DeviceOrientationEventInit init;
    RefPtr<Event> event =
        DeviceOrientationEvent::Constructor(aOwner, u""_ns, init);
    event->MarkUninitialized();
    return event.forget();
  }
  if (aEventType.LowerCaseEqualsLiteral("devicemotionevent")) {
    return NS_NewDOMDeviceMotionEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("uievent") ||
      aEventType.LowerCaseEqualsLiteral("uievents")) {
    return NS_NewDOMUIEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("event") ||
      aEventType.LowerCaseEqualsLiteral("events") ||
      aEventType.LowerCaseEqualsLiteral("htmlevents") ||
      aEventType.LowerCaseEqualsLiteral("svgevents")) {
    return NS_NewDOMEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("messageevent")) {
    RefPtr<Event> event = new MessageEvent(aOwner, aPresContext, nullptr);
    return event.forget();
  }
  if (aEventType.LowerCaseEqualsLiteral("beforeunloadevent")) {
    return NS_NewDOMBeforeUnloadEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("touchevent") &&
      TouchEvent::LegacyAPIEnabled(
          nsContentUtils::GetDocShellForEventTarget(aOwner),
          aCallerType == CallerType::System)) {
    return NS_NewDOMTouchEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("hashchangeevent")) {
    HashChangeEventInit init;
    RefPtr<Event> event = HashChangeEvent::Constructor(aOwner, u""_ns, init);
    event->MarkUninitialized();
    return event.forget();
  }
  if (aEventType.LowerCaseEqualsLiteral("customevent")) {
    return NS_NewDOMCustomEvent(aOwner, aPresContext, nullptr);
  }
  if (aEventType.LowerCaseEqualsLiteral("storageevent")) {
    RefPtr<Event> event =
        StorageEvent::Constructor(aOwner, u""_ns, StorageEventInit());
    event->MarkUninitialized();
    return event.forget();
  }
  if (aEventType.LowerCaseEqualsLiteral("focusevent")) {
    RefPtr<Event> event = NS_NewDOMFocusEvent(aOwner, aPresContext, nullptr);
    event->MarkUninitialized();
    return event.forget();
  }

  // Only allow these events for chrome
  if (aCallerType == CallerType::System) {
    if (aEventType.LowerCaseEqualsLiteral("simplegestureevent")) {
      return NS_NewDOMSimpleGestureEvent(aOwner, aPresContext, nullptr);
    }
    if (aEventType.LowerCaseEqualsLiteral("xulcommandevent") ||
        aEventType.LowerCaseEqualsLiteral("xulcommandevents")) {
      return NS_NewDOMXULCommandEvent(aOwner, aPresContext, nullptr);
    }
  }

  // NEW EVENT TYPES SHOULD NOT BE ADDED HERE; THEY SHOULD USE ONLY EVENT
  // CONSTRUCTORS

  return nullptr;
}

struct CurrentTargetPathInfo {
  uint32_t mIndex;
  int32_t mHiddenSubtreeLevel;
};

static CurrentTargetPathInfo TargetPathInfo(
    const nsTArray<EventTargetChainItem>& aEventPath,
    const EventTarget& aCurrentTarget) {
  int32_t currentTargetHiddenSubtreeLevel = 0;
  for (uint32_t index = aEventPath.Length(); index--;) {
    const EventTargetChainItem& item = aEventPath.ElementAt(index);
    if (item.PreHandleEventOnly()) {
      continue;
    }

    if (item.IsRootOfClosedTree()) {
      currentTargetHiddenSubtreeLevel++;
    }

    if (item.CurrentTarget() == &aCurrentTarget) {
      return {index, currentTargetHiddenSubtreeLevel};
    }

    if (item.IsSlotInClosedTree()) {
      currentTargetHiddenSubtreeLevel--;
    }
  }
  MOZ_ASSERT_UNREACHABLE("No target found?");
  return {0, 0};
}

// https://dom.spec.whatwg.org/#dom-event-composedpath
void EventDispatcher::GetComposedPathFor(WidgetEvent* aEvent,
                                         nsTArray<RefPtr<EventTarget>>& aPath) {
  MOZ_ASSERT(aPath.IsEmpty());
  nsTArray<EventTargetChainItem>* path = aEvent->mPath;
  if (!path || path->IsEmpty() || !aEvent->mCurrentTarget) {
    return;
  }

  EventTarget* currentTarget =
      aEvent->mCurrentTarget->GetTargetForEventTargetChain();
  if (!currentTarget) {
    return;
  }

  CurrentTargetPathInfo currentTargetInfo =
      TargetPathInfo(*path, *currentTarget);

  {
    int32_t maxHiddenLevel = currentTargetInfo.mHiddenSubtreeLevel;
    int32_t currentHiddenLevel = currentTargetInfo.mHiddenSubtreeLevel;
    for (uint32_t index = currentTargetInfo.mIndex; index--;) {
      EventTargetChainItem& item = path->ElementAt(index);
      if (item.PreHandleEventOnly()) {
        continue;
      }

      if (item.IsRootOfClosedTree()) {
        currentHiddenLevel++;
      }

      if (currentHiddenLevel <= maxHiddenLevel) {
        aPath.AppendElement(item.CurrentTarget()->GetTargetForDOMEvent());
      }

      if (item.IsChromeHandler()) {
        break;
      }

      if (item.IsSlotInClosedTree()) {
        currentHiddenLevel--;
        maxHiddenLevel = std::min(maxHiddenLevel, currentHiddenLevel);
      }
    }

    aPath.Reverse();
  }

  aPath.AppendElement(currentTarget->GetTargetForDOMEvent());

  {
    int32_t maxHiddenLevel = currentTargetInfo.mHiddenSubtreeLevel;
    int32_t currentHiddenLevel = currentTargetInfo.mHiddenSubtreeLevel;
    for (uint32_t index = currentTargetInfo.mIndex + 1; index < path->Length();
         ++index) {
      EventTargetChainItem& item = path->ElementAt(index);
      if (item.PreHandleEventOnly()) {
        continue;
      }

      if (item.IsSlotInClosedTree()) {
        currentHiddenLevel++;
      }

      if (item.IsChromeHandler()) {
        break;
      }

      if (currentHiddenLevel <= maxHiddenLevel) {
        aPath.AppendElement(item.CurrentTarget()->GetTargetForDOMEvent());
      }

      if (item.IsRootOfClosedTree()) {
        currentHiddenLevel--;
        maxHiddenLevel = std::min(maxHiddenLevel, currentHiddenLevel);
      }
    }
  }
}

void EventChainPreVisitor::IgnoreCurrentTargetBecauseOfShadowDOMRetargeting() {
  mCanHandle = false;
  mIgnoreBecauseOfShadowDOM = true;

  EventTarget* target = nullptr;

  auto getWindow = [this]() -> nsPIDOMWindowOuter* {
    nsINode* node = nsINode::FromEventTargetOrNull(this->mParentTarget);
    if (!node) {
      return nullptr;
    }
    Document* doc = node->GetComposedDoc();
    if (!doc) {
      return nullptr;
    }

    return doc->GetWindow();
  };

  // The HTMLEditor is registered to nsWindowRoot, so we
  // want to dispatch events to it.
  if (nsCOMPtr<nsPIDOMWindowOuter> win = getWindow()) {
    target = win->GetParentTarget();
  }
  SetParentTarget(target, false);

  mEventTargetAtParent = nullptr;
}

}  // namespace mozilla
