/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZILLA_INTERNAL_API
#  ifndef mozilla_EventDispatcher_h_
#    define mozilla_EventDispatcher_h_

#    include "mozilla/EventForwards.h"
#    include "mozilla/Maybe.h"
#    include "mozilla/dom/BindingDeclarations.h"
#    include "mozilla/dom/Touch.h"
#    include "nsCOMPtr.h"
#    include "nsTArray.h"

// Microsoft's API Name hackery sucks
#    undef CreateEvent

class nsAtom;
class nsIContent;
class nsPresContext;

template <class E>
class nsCOMArray;

namespace mozilla {
namespace dom {
class Event;
class EventTarget;
}  // namespace dom

/**
 * About event dispatching:
 * When either EventDispatcher::Dispatch or
 * EventDispatcher::DispatchDOMEvent is called an event target chain is
 * created. EventDispatcher creates the chain by calling GetEventTargetParent
 * on each event target and the creation continues until either the mCanHandle
 * member of the EventChainPreVisitor object is false or the mParentTarget
 * does not point to a new target. The event target chain is created in the
 * heap.
 *
 * If the event needs retargeting, mEventTargetAtParent must be set in
 * GetEventTargetParent.
 *
 * The capture, target and bubble phases of the event dispatch are handled
 * by iterating through the event target chain. Iteration happens twice,
 * first for the default event group and then for the system event group.
 * While dispatching the event for the system event group PostHandleEvent
 * is called right after calling event listener for the current event target.
 */

class MOZ_STACK_CLASS EventChainVisitor {
 public:
  // For making creators of this class instances guarantee the lifetime of
  // aPresContext, this needs to be marked as MOZ_CAN_RUN_SCRIPT.
  MOZ_CAN_RUN_SCRIPT
  EventChainVisitor(nsPresContext* aPresContext, WidgetEvent* aEvent,
                    dom::Event* aDOMEvent,
                    nsEventStatus aEventStatus = nsEventStatus_eIgnore)
      : mPresContext(aPresContext),
        mEvent(aEvent),
        mDOMEvent(aDOMEvent),
        mEventStatus(aEventStatus),
        mItemFlags(0) {}

  /**
   * The prescontext, possibly nullptr.
   * Note that the lifetime of mPresContext is guaranteed by the creators.
   */
  MOZ_KNOWN_LIVE nsPresContext* const mPresContext;

  /**
   * The WidgetEvent which is being dispatched. Never nullptr.
   */
  WidgetEvent* const mEvent;

  /**
   * The DOM Event assiciated with the mEvent. Possibly nullptr if a DOM Event
   * is not (yet) created.
   */
  dom::Event* mDOMEvent;

  /**
   * The status of the event.
   * @see nsEventStatus.h
   */
  nsEventStatus mEventStatus;

  /**
   * Bits for items in the event target chain.
   * They are scoped to each item and will be set/stored when invoking most
   * of EventTarget's methods.
   *
   * @note These bits are different for each item in the event target chain.
   *       It is up to the EventTarget implementation to decide how to
   *       use these bits.
   *
   * @note Using uint16_t because that is used also in EventTargetChainItem.
   */
  uint16_t mItemFlags;

  /**
   * Data for items in the event target chain, scoped to each item.
   *
   * Only used for (legacy) activation behavior methods.
   *
   * @note This data is different for each item in the event target chain.
   *       It is up to the EventTarget implementation to decide how to
   *       use this.
   */
  nsCOMPtr<nsISupports> mItemData;

  // nsContentUtils::GetEventType(mEvent).
  RefPtr<nsAtom> mTypeAtom;
};

class MOZ_STACK_CLASS EventChainPreVisitor final : public EventChainVisitor {
 public:
  MOZ_CAN_RUN_SCRIPT
  EventChainPreVisitor(nsPresContext* aPresContext, WidgetEvent* aEvent,
                       dom::Event* aDOMEvent, nsEventStatus aEventStatus,
                       bool aIsInAnon,
                       dom::EventTarget* aTargetInKnownToBeHandledScope)
      : EventChainVisitor(aPresContext, aEvent, aDOMEvent, aEventStatus),
        mCanHandle(true),
        mAutomaticChromeDispatch(true),
        mForceContentDispatch(false),
        mRelatedTargetIsInAnon(false),
        mOriginalTargetIsInAnon(aIsInAnon),
        mWantsWillHandleEvent(false),
        mMayHaveListenerManager(true),
        mWantsPreHandleEvent(false),
        mWantsPostHandleEvent(false),
        mRootOfClosedTree(false),
        mItemInShadowTree(false),
        mParentIsSlotInClosedTree(false),
        mParentIsChromeHandler(false),
        mRelatedTargetRetargetedInCurrentScope(false),
        mIgnoreBecauseOfShadowDOM(false),
        mWantsActivationBehavior(false),
        mMaybeUncancelable(false),
        mParentTarget(nullptr),
        mEventTargetAtParent(nullptr),
        mRetargetedRelatedTarget(nullptr),
        mTargetInKnownToBeHandledScope(aTargetInKnownToBeHandledScope) {}

  void Reset() {
    mItemFlags = 0;
    mItemData = nullptr;
    mCanHandle = true;
    mAutomaticChromeDispatch = true;
    mForceContentDispatch = false;
    mWantsWillHandleEvent = false;
    mMayHaveListenerManager = true;
    mWantsPreHandleEvent = false;
    mWantsPostHandleEvent = false;
    mRootOfClosedTree = false;
    mItemInShadowTree = false;
    mParentIsSlotInClosedTree = false;
    mParentIsChromeHandler = false;
    // Note, we don't clear mRelatedTargetRetargetedInCurrentScope explicitly,
    // since it is used during event path creation to indicate whether
    // relatedTarget may need to be retargeted.
    mIgnoreBecauseOfShadowDOM = false;
    mWantsActivationBehavior = false;
    mParentTarget = nullptr;
    mEventTargetAtParent = nullptr;
    mRetargetedRelatedTarget = nullptr;
    mRetargetedTouchTargets.reset();
  }

  dom::EventTarget* GetParentTarget() { return mParentTarget; }

  void SetParentTarget(dom::EventTarget* aParentTarget, bool aIsChromeHandler) {
    mParentTarget = aParentTarget;
    if (mParentTarget) {
      mParentIsChromeHandler = aIsChromeHandler;
    }
  }

  void IgnoreCurrentTargetBecauseOfShadowDOMRetargeting();

  /**
   * Member that must be set in GetEventTargetParent by event targets. If set to
   * false, indicates that this event target will not be handling the event and
   * construction of the event target chain is complete. The target that sets
   * mCanHandle to false is NOT included in the event target chain.
   */
  bool mCanHandle;

  /**
   * If mCanHandle is false and mAutomaticChromeDispatch is also false
   * event will not be dispatched to the chrome event handler.
   */
  bool mAutomaticChromeDispatch;

  /**
   * If mForceContentDispatch is set to true,
   * content dispatching is not disabled for this event target.
   * FIXME! This is here for backward compatibility. Bug 329119
   */
  bool mForceContentDispatch;

  /**
   * true if it is known that related target is or is a descendant of an
   * element which is anonymous for events.
   */
  bool mRelatedTargetIsInAnon;

  /**
   * true if the original target of the event is inside anonymous content.
   * This is set before calling GetEventTargetParent on event targets.
   */
  bool mOriginalTargetIsInAnon;

  /**
   * Whether or not EventTarget::WillHandleEvent will be
   * called. Default is false;
   */
  bool mWantsWillHandleEvent;

  /**
   * If it is known that the current target doesn't have a listener manager
   * when GetEventTargetParent is called, set this to false.
   */
  bool mMayHaveListenerManager;

  /**
   * Whether or not EventTarget::PreHandleEvent will be called. Default is
   * false;
   */
  bool mWantsPreHandleEvent;

  /**
   * Whether or not EventTarget::PostHandleEvent will be called. Default is
   * false;
   */
  bool mWantsPostHandleEvent;

  /**
   * True if the current target is either closed ShadowRoot or root of
   * chrome only access tree (for example native anonymous content).
   */
  bool mRootOfClosedTree;

  /**
   * If target is node and its root is a shadow root.
   * https://dom.spec.whatwg.org/#event-path-item-in-shadow-tree
   */
  bool mItemInShadowTree;

  /**
   * True if mParentTarget is HTMLSlotElement in a closed shadow tree and the
   * current target is assigned to that slot.
   */
  bool mParentIsSlotInClosedTree;

  /**
   * True if mParentTarget is a chrome handler in the event path.
   */
  bool mParentIsChromeHandler;

  /**
   * True if event's related target has been already retargeted in the
   * current 'scope'. This should be set to false initially and whenever
   * event path creation crosses shadow boundary.
   */
  bool mRelatedTargetRetargetedInCurrentScope;

  /**
   * True if Shadow DOM relatedTarget retargeting causes the current item
   * to not show up in the event path.
   */
  bool mIgnoreBecauseOfShadowDOM;

  /*
   * True if the activation behavior of the current item should run
   * See activationTarget in https://dom.spec.whatwg.org/#concept-event-dispatch
   */
  bool mWantsActivationBehavior;

  /*
   * Some events will be set uncancelable if we know they won't be default
   * prevented. If mMaybeUncancelable is true, we haven't found something
   * that might default prevent the event, like a non-passive listener.
   * https://w3c.github.io/touch-events/#cancelability
   */
  bool mMaybeUncancelable;

 private:
  /**
   * Parent item in the event target chain.
   */
  dom::EventTarget* mParentTarget;

 public:
  /**
   * If the event needs to be retargeted, this is the event target,
   * which should be used when the event is handled at mParentTarget.
   */
  dom::EventTarget* mEventTargetAtParent;

  /**
   * If the related target of the event needs to be retargeted, set this
   * to a new EventTarget.
   */
  dom::EventTarget* mRetargetedRelatedTarget;

  /**
   * If mEvent is a WidgetTouchEvent and its mTouches needs retargeting,
   * set the targets to this array. The array should contain one entry per
   * each object in WidgetTouchEvent::mTouches.
   */
  mozilla::Maybe<nsTArray<RefPtr<dom::EventTarget>>> mRetargetedTouchTargets;

  /**
   * Set to the value of mEvent->mTarget of the previous scope in case of
   * Shadow DOM or such, and if there is no anonymous content this just points
   * to the initial target.
   */
  dom::EventTarget* mTargetInKnownToBeHandledScope;
};

class MOZ_STACK_CLASS EventChainPostVisitor final
    : public mozilla::EventChainVisitor {
 public:
  // Note that for making guarantee the lifetime of mPresContext and mDOMEvent,
  // creators should guarantee that aOther won't be deleted while the instance
  // of this class is alive.
  MOZ_CAN_RUN_SCRIPT
  explicit EventChainPostVisitor(EventChainVisitor& aOther)
      : EventChainVisitor(aOther.mPresContext, aOther.mEvent,
                          MOZ_KnownLive(aOther.mDOMEvent),
                          aOther.mEventStatus) {
    mTypeAtom = aOther.mTypeAtom;
  }
};

/**
 * If an EventDispatchingCallback object is passed to Dispatch,
 * its HandleEvent method is called after handling the default event group,
 * before handling the system event group.
 * This is used in PresShell.
 */
class MOZ_STACK_CLASS EventDispatchingCallback {
 public:
  MOZ_CAN_RUN_SCRIPT
  virtual void HandleEvent(EventChainPostVisitor& aVisitor) = 0;
};

/**
 * The generic class for event dispatching.
 * Must not be used outside Gecko!
 */
class EventDispatcher {
 public:
  /**
   * If the target of aEvent is set before calling this method, the target of
   * aEvent is used as the target (unless there is event
   * retargeting) and the originalTarget of the DOM Event.
   * aTarget is always used as the starting point for constructing the event
   * target chain, no matter what the value of aEvent->mTarget is.
   * In other words, aEvent->mTarget is only a property of the event and it has
   * nothing to do with the construction of the event target chain.
   * Neither aTarget nor aEvent is allowed to be nullptr.
   *
   * If aTargets is non-null, event target chain will be created, but
   * event won't be handled. In this case aEvent->mMessage should be
   * eVoidEvent.
   * @note Use this method when dispatching a WidgetEvent.
   */
  MOZ_CAN_RUN_SCRIPT static nsresult Dispatch(
      dom::EventTarget* aTarget, nsPresContext* aPresContext,
      WidgetEvent* aEvent, dom::Event* aDOMEvent = nullptr,
      nsEventStatus* aEventStatus = nullptr,
      EventDispatchingCallback* aCallback = nullptr,
      nsTArray<dom::EventTarget*>* aTargets = nullptr);

  /**
   * Dispatches an event.
   * If aDOMEvent is not nullptr, it is used for dispatching
   * (aEvent can then be nullptr) and (if aDOMEvent is not |trusted| already),
   * the |trusted| flag is set if the caller uses the system principal.
   * Otherwise this works like EventDispatcher::Dispatch.
   * @note Use this method when dispatching a dom::Event.
   */
  MOZ_CAN_RUN_SCRIPT static nsresult DispatchDOMEvent(
      dom::EventTarget* aTarget, WidgetEvent* aEvent, dom::Event* aDOMEvent,
      nsPresContext* aPresContext, nsEventStatus* aEventStatus);

  /**
   * Creates a DOM Event.  Returns null if the event type is unsupported.
   */
  static already_AddRefed<dom::Event> CreateEvent(
      dom::EventTarget* aOwner, nsPresContext* aPresContext,
      WidgetEvent* aEvent, const nsAString& aEventType,
      dom::CallerType aCallerType = dom::CallerType::System);

  static void GetComposedPathFor(WidgetEvent* aEvent,
                                 nsTArray<RefPtr<dom::EventTarget>>& aPath);

  /**
   * Called at shutting down.
   */
  static void Shutdown();
};

}  // namespace mozilla

#  endif  // mozilla_EventDispatcher_h_
#endif
