/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Event.h"

#include "AccessCheck.h"
#include "base/basictypes.h"
#include "ipc/IPCMessageUtils.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/SVGOuterSVGFrame.h"
#include "nsContentUtils.h"
#include "nsCOMPtr.h"
#include "nsDeviceContext.h"
#include "nsError.h"
#include "nsGlobalWindowInner.h"
#include "nsIFrame.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsJSEnvironment.h"
#include "nsLayoutUtils.h"
#include "nsPIWindowRoot.h"
#include "nsRFPService.h"

namespace mozilla::dom {

Event::Event(EventTarget* aOwner, nsPresContext* aPresContext,
             WidgetEvent* aEvent) {
  ConstructorInit(aOwner, aPresContext, aEvent);
}

Event::Event(nsPIDOMWindowInner* aParent) {
  ConstructorInit(nsGlobalWindowInner::Cast(aParent), nullptr, nullptr);
}

void Event::ConstructorInit(EventTarget* aOwner, nsPresContext* aPresContext,
                            WidgetEvent* aEvent) {
  SetOwner(aOwner);
  mIsMainThreadEvent = NS_IsMainThread();
  if (mIsMainThreadEvent) {
    mRefCnt.SetIsOnMainThread();
  }

  mPrivateDataDuplicated = false;
  mWantsPopupControlCheck = false;

  if (aEvent) {
    mEvent = aEvent;
    mEventIsInternal = false;
  } else {
    mEventIsInternal = true;
    /*
      A derived class might want to allocate its own type of aEvent
      (derived from WidgetEvent). To do this, it should take care to pass
      a non-nullptr aEvent to this ctor, e.g.:

        FooEvent::FooEvent(..., WidgetEvent* aEvent)
          : Event(..., aEvent ? aEvent : new WidgetEvent())

      Then, to override the mEventIsInternal assignments done by the
      base ctor, it should do this in its own ctor:

        FooEvent::FooEvent(..., WidgetEvent* aEvent)
        ...
        {
          ...
          if (aEvent) {
            mEventIsInternal = false;
          }
          else {
            mEventIsInternal = true;
          }
          ...
        }
     */
    mEvent = new WidgetEvent(false, eVoidEvent);
  }

  InitPresContextData(aPresContext);
}

void Event::InitPresContextData(nsPresContext* aPresContext) {
  mPresContext = aPresContext;
  // Get the explicit original target (if it's anonymous make it null)
  {
    nsIContent* content = GetTargetFromFrame();
    if (content && !content->IsInNativeAnonymousSubtree()) {
      mExplicitOriginalTarget = content;
    } else {
      mExplicitOriginalTarget = nullptr;
    }
  }
}

Event::~Event() {
  NS_ASSERT_OWNINGTHREAD(Event);

  if (mEventIsInternal && mEvent) {
    delete mEvent;
  }
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Event)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(Event)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Event)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(Event, LastRelease())

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Event)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Event)
  if (tmp->mEventIsInternal) {
    tmp->mEvent->mTarget = nullptr;
    tmp->mEvent->mCurrentTarget = nullptr;
    tmp->mEvent->mOriginalTarget = nullptr;
    tmp->mEvent->mRelatedTarget = nullptr;
    tmp->mEvent->mOriginalRelatedTarget = nullptr;
    switch (tmp->mEvent->mClass) {
      case eDragEventClass: {
        WidgetDragEvent* dragEvent = tmp->mEvent->AsDragEvent();
        dragEvent->mDataTransfer = nullptr;
        break;
      }
      case eClipboardEventClass:
        tmp->mEvent->AsClipboardEvent()->mClipboardData = nullptr;
        break;
      case eEditorInputEventClass: {
        InternalEditorInputEvent* inputEvent =
            tmp->mEvent->AsEditorInputEvent();
        inputEvent->mDataTransfer = nullptr;
        inputEvent->mTargetRanges.Clear();
        break;
      }
      case eMutationEventClass:
        tmp->mEvent->AsMutationEvent()->mRelatedNode = nullptr;
        break;
      default:
        break;
    }

    if (WidgetMouseEvent* mouseEvent = tmp->mEvent->AsMouseEvent()) {
      mouseEvent->mClickTarget = nullptr;
    }
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mExplicitOriginalTarget);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner);
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Event)
  if (tmp->mEventIsInternal) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvent->mTarget)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvent->mCurrentTarget)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvent->mOriginalTarget)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvent->mRelatedTarget)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvent->mOriginalRelatedTarget);
    switch (tmp->mEvent->mClass) {
      case eDragEventClass: {
        WidgetDragEvent* dragEvent = tmp->mEvent->AsDragEvent();
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mEvent->mDataTransfer");
        cb.NoteXPCOMChild(dragEvent->mDataTransfer);
        break;
      }
      case eClipboardEventClass:
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mEvent->mClipboardData");
        cb.NoteXPCOMChild(tmp->mEvent->AsClipboardEvent()->mClipboardData);
        break;
      case eEditorInputEventClass:
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mEvent->mDataTransfer");
        cb.NoteXPCOMChild(tmp->mEvent->AsEditorInputEvent()->mDataTransfer);
        NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
            mEvent->AsEditorInputEvent()->mTargetRanges);
        break;
      case eMutationEventClass:
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mEvent->mRelatedNode");
        cb.NoteXPCOMChild(tmp->mEvent->AsMutationEvent()->mRelatedNode);
        break;
      default:
        break;
    }

    if (WidgetMouseEvent* mouseEvent = tmp->mEvent->AsMouseEvent()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mEvent->mClickTarget");
      cb.NoteXPCOMChild(mouseEvent->mClickTarget);
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mExplicitOriginalTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(Event)
  if (tmp->HasKnownLiveWrapper()) {
    if (tmp->mEventIsInternal) {
      if (WidgetEvent* event = tmp->mEvent) {
        auto mark = [](EventTarget* aTarget) {
          if (!aTarget) {
            return;
          }
          if (nsINode* node = aTarget->GetAsNode()) {
            FragmentOrElement::MarkNodeChildren(node);
            if (node->HasKnownLiveWrapper()) {
              // Use CanSkip to possibly mark more nodes to be certainly alive.
              FragmentOrElement::CanSkip(node, true);
            }
          }
        };

        mark(event->mTarget);
        mark(event->mCurrentTarget);
        mark(event->mOriginalTarget);
        mark(event->mRelatedTarget);
        mark(event->mOriginalRelatedTarget);
      }
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(Event)
  return tmp->HasKnownLiveWrapperAndDoesNotNeedTracing(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(Event)
  return tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

void Event::LastRelease() {
  nsISupports* supports = nullptr;
  QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                 reinterpret_cast<void**>(&supports));
  nsXPCOMCycleCollectionParticipant* p = nullptr;
  CallQueryInterface(this, &p);
  p->Unlink(supports);
}

JSObject* Event::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return WrapObjectInternal(aCx, aGivenProto);
}

JSObject* Event::WrapObjectInternal(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return Event_Binding::Wrap(aCx, this, aGivenProto);
}

void Event::GetType(nsAString& aType) const {
  GetWidgetEventType(mEvent, aType);
}

EventTarget* Event::GetTarget() const { return mEvent->GetDOMEventTarget(); }

already_AddRefed<Document> Event::GetDocument() const {
  nsCOMPtr<EventTarget> eventTarget = GetTarget();

  if (!eventTarget) {
    return nullptr;
  }

  nsIGlobalObject* global = eventTarget->GetOwnerGlobal();
  if (!global) {
    return nullptr;
  }

  nsPIDOMWindowInner* win = global->GetAsInnerWindow();
  if (!win) {
    return nullptr;
  }

  nsCOMPtr<Document> doc;
  doc = win->GetExtantDoc();

  return doc.forget();
}

EventTarget* Event::GetCurrentTarget() const {
  return mEvent->GetCurrentDOMEventTarget();
}

void Event::ComposedPath(nsTArray<RefPtr<EventTarget>>& aPath) {
  EventDispatcher::GetComposedPathFor(mEvent, aPath);
}

//
// Get the actual event target node (may have been retargeted for mouse events)
//
nsIContent* Event::GetTargetFromFrame() {
  if (!mPresContext) {
    return nullptr;
  }

  // Get the mTarget frame (have to get the ESM first)
  nsIFrame* targetFrame = mPresContext->EventStateManager()->GetEventTarget();
  if (!targetFrame) {
    return nullptr;
  }

  // get the real content
  return targetFrame->GetContentForEvent(mEvent);
}

EventTarget* Event::GetExplicitOriginalTarget() const {
  if (mExplicitOriginalTarget) {
    return mExplicitOriginalTarget;
  }
  return GetTarget();
}

EventTarget* Event::GetOriginalTarget() const {
  return mEvent->GetOriginalDOMEventTarget();
}

EventTarget* Event::GetComposedTarget() const {
  EventTarget* et = GetOriginalTarget();
  nsIContent* content = nsIContent::FromEventTargetOrNull(et);
  if (!content) {
    return et;
  }
  nsIContent* nonChrome = content->FindFirstNonChromeOnlyAccessContent();
  return nonChrome ? static_cast<EventTarget*>(nonChrome)
                   : static_cast<EventTarget*>(content->GetComposedDoc());
}

void Event::SetTrusted(bool aTrusted) { mEvent->mFlags.mIsTrusted = aTrusted; }

bool Event::ShouldIgnoreChromeEventTargetListener() const {
  MOZ_ASSERT(NS_IsMainThread());
  if (!XRE_IsParentProcess()) {
    return false;
  }
  if (EventTarget* currentTarget = GetCurrentTarget();
      NS_WARN_IF(!currentTarget) || !currentTarget->IsRootWindow()) {
    return false;
  }
  EventTarget* et = GetOriginalTarget();
  if (NS_WARN_IF(!et)) {
    return false;
  }
  nsIGlobalObject* global = et->GetOwnerGlobal();
  if (NS_WARN_IF(!global)) {
    return false;
  }
  nsPIDOMWindowInner* win = global->GetAsInnerWindow();
  if (NS_WARN_IF(!win)) {
    return false;
  }
  BrowsingContext* bc = win->GetBrowsingContext();
  if (NS_WARN_IF(!bc)) {
    return false;
  }
  // If this is a content event on an nsWindowRoot, then we also handle this in
  // InProcessBrowserChildMessageManager, so we can ignore this event.
  return bc->IsContent();
}

bool Event::Init(mozilla::dom::EventTarget* aGlobal) {
  if (!mIsMainThreadEvent) {
    return IsCurrentThreadRunningChromeWorker();
  }
  bool trusted = false;
  if (aGlobal) {
    if (nsPIDOMWindowInner* w = aGlobal->GetAsInnerWindow()) {
      if (Document* d = w->GetExtantDoc()) {
        trusted = nsContentUtils::IsChromeDoc(d);
        if (nsPresContext* presContext = d->GetPresContext()) {
          InitPresContextData(presContext);
        }
      }
    }
  }
  return trusted;
}

// static
already_AddRefed<Event> Event::Constructor(const GlobalObject& aGlobal,
                                           const nsAString& aType,
                                           const EventInit& aParam) {
  nsCOMPtr<mozilla::dom::EventTarget> t =
      do_QueryInterface(aGlobal.GetAsSupports());
  return Constructor(t, aType, aParam);
}

// static
already_AddRefed<Event> Event::Constructor(EventTarget* aEventTarget,
                                           const nsAString& aType,
                                           const EventInit& aParam) {
  RefPtr<Event> e = new Event(aEventTarget, nullptr, nullptr);
  bool trusted = e->Init(aEventTarget);
  e->InitEvent(aType, aParam.mBubbles, aParam.mCancelable);
  e->SetTrusted(trusted);
  e->SetComposed(aParam.mComposed);
  return e.forget();
}

uint16_t Event::EventPhase() const {
  if ((mEvent->mCurrentTarget && mEvent->mCurrentTarget == mEvent->mTarget) ||
      mEvent->mFlags.mInTargetPhase) {
    return Event_Binding::AT_TARGET;
  }
  if (mEvent->mFlags.mInCapturePhase) {
    return Event_Binding::CAPTURING_PHASE;
  }
  if (mEvent->mFlags.mInBubblingPhase) {
    return Event_Binding::BUBBLING_PHASE;
  }
  return Event_Binding::NONE;
}

void Event::StopPropagation() { mEvent->StopPropagation(); }

void Event::StopImmediatePropagation() { mEvent->StopImmediatePropagation(); }

void Event::StopCrossProcessForwarding() {
  mEvent->StopCrossProcessForwarding();
}

void Event::PreventDefault() {
  // This method is called only from C++ code which must handle default action
  // of this event.  So, pass true always.
  PreventDefaultInternal(true);
}

void Event::PreventDefault(JSContext* aCx, CallerType aCallerType) {
  // Note that at handling default action, another event may be dispatched.
  // Then, JS in content mey be call preventDefault()
  // even in the event is in system event group.  Therefore, don't refer
  // mInSystemGroup here.
  nsIPrincipal* principal =
      mIsMainThreadEvent ? nsContentUtils::SubjectPrincipal(aCx) : nullptr;

  PreventDefaultInternal(aCallerType == CallerType::System, principal);
}

void Event::PreventDefaultInternal(bool aCalledByDefaultHandler,
                                   nsIPrincipal* aPrincipal) {
  if (mEvent->mFlags.mInPassiveListener) {
    if (nsPIDOMWindowInner* win = mOwner->GetAsInnerWindow()) {
      if (Document* doc = win->GetExtantDoc()) {
        if (!doc->HasWarnedAbout(
                Document::ePreventDefaultFromPassiveListener)) {
          AutoTArray<nsString, 1> params;
          GetType(*params.AppendElement());
          doc->WarnOnceAbout(Document::ePreventDefaultFromPassiveListener,
                             false, params);
        }
      }
    }
    return;
  }
  if (!mEvent->mFlags.mCancelable) {
    return;
  }

  mEvent->PreventDefault(aCalledByDefaultHandler, aPrincipal);

  if (!IsTrusted()) {
    return;
  }

  if (mEvent->mClass == eDragEventClass) {
    UpdateDefaultPreventedOnContentForDragEvent();
  }
}

void Event::UpdateDefaultPreventedOnContentForDragEvent() {
  WidgetDragEvent* dragEvent = mEvent->AsDragEvent();
  if (!dragEvent) {
    return;
  }

  nsIPrincipal* principal = nullptr;
  // Since we now have HTMLEditorEventListener registered on nsWindowRoot,
  // mCurrentTarget could be nsWindowRoot, so we need to use
  // mTarget if that's the case.
  MOZ_ASSERT_IF(dragEvent->mInHTMLEditorEventListener,
                mEvent->mCurrentTarget->IsRootWindow());
  EventTarget* target = dragEvent->mInHTMLEditorEventListener
                            ? mEvent->mTarget
                            : mEvent->mCurrentTarget;

  nsINode* node = nsINode::FromEventTargetOrNull(target);
  if (node) {
    principal = node->NodePrincipal();
  } else {
    nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(target);
    if (sop) {
      principal = sop->GetPrincipal();
    }
  }
  if (principal && !principal->IsSystemPrincipal()) {
    dragEvent->mDefaultPreventedOnContent = true;
  }
}

void Event::SetEventType(const nsAString& aEventTypeArg) {
  mEvent->mSpecifiedEventTypeString.Truncate();
  if (mIsMainThreadEvent) {
    EventClassID classID = mEvent->mClass;
    if (classID == eMouseEventClass) {
      // Some pointer event types were changed from MouseEvent.  For backward
      // compatibility, we need to handle untrusted events of them created with
      // MouseEvent instance in some places.
      if (aEventTypeArg.EqualsLiteral(u"click") ||
          aEventTypeArg.EqualsLiteral(u"auxclick") ||
          aEventTypeArg.EqualsLiteral(u"contextmenu")) {
        classID = ePointerEventClass;
      }
    }
    mEvent->mSpecifiedEventType = nsContentUtils::GetEventMessageAndAtom(
        aEventTypeArg, classID, &(mEvent->mMessage));
    mEvent->SetDefaultComposed();
  } else {
    mEvent->mSpecifiedEventType = NS_Atomize(u"on"_ns + aEventTypeArg);
    mEvent->mMessage = eUnidentifiedEvent;
    mEvent->SetComposed(aEventTypeArg);
  }
  mEvent->SetDefaultComposedInNativeAnonymousContent();
}

already_AddRefed<EventTarget> Event::EnsureWebAccessibleRelatedTarget(
    EventTarget* aRelatedTarget) {
  nsCOMPtr<EventTarget> relatedTarget = aRelatedTarget;
  if (relatedTarget) {
    nsIContent* content = nsIContent::FromEventTarget(relatedTarget);
    if (content && content->ChromeOnlyAccess() &&
        !nsContentUtils::CanAccessNativeAnon()) {
      content = content->FindFirstNonChromeOnlyAccessContent();
      relatedTarget = content;
    }

    if (relatedTarget) {
      relatedTarget = relatedTarget->GetTargetForDOMEvent();
    }
  }
  return relatedTarget.forget();
}

void Event::InitEvent(const nsAString& aEventTypeArg,
                      mozilla::CanBubble aCanBubbleArg,
                      mozilla::Cancelable aCancelableArg,
                      mozilla::Composed aComposedArg) {
  // Make sure this event isn't already being dispatched.
  NS_ENSURE_TRUE_VOID(!mEvent->mFlags.mIsBeingDispatched);

  if (IsTrusted()) {
    // Ensure the caller is permitted to dispatch trusted DOM events.
    if (!nsContentUtils::ThreadsafeIsCallerChrome()) {
      SetTrusted(false);
    }
  }

  SetEventType(aEventTypeArg);

  mEvent->mFlags.mBubbles = aCanBubbleArg == CanBubble::eYes;
  mEvent->mFlags.mCancelable = aCancelableArg == Cancelable::eYes;
  if (aComposedArg != Composed::eDefault) {
    mEvent->mFlags.mComposed = aComposedArg == Composed::eYes;
  }

  mEvent->mFlags.mDefaultPrevented = false;
  mEvent->mFlags.mDefaultPreventedByContent = false;
  mEvent->mFlags.mDefaultPreventedByChrome = false;
  mEvent->mFlags.mPropagationStopped = false;
  mEvent->mFlags.mImmediatePropagationStopped = false;

  // Clearing the old targets, so that the event is targeted correctly when
  // re-dispatching it.
  mEvent->mTarget = nullptr;
  mEvent->mOriginalTarget = nullptr;
}

void Event::DuplicatePrivateData() {
  NS_ASSERTION(mEvent, "No WidgetEvent for Event duplication!");
  if (mEventIsInternal) {
    return;
  }

  mEvent = mEvent->Duplicate();
  mPresContext = nullptr;
  mEventIsInternal = true;
  mPrivateDataDuplicated = true;
}

void Event::SetTarget(EventTarget* aTarget) { mEvent->mTarget = aTarget; }

bool Event::IsDispatchStopped() { return mEvent->PropagationStopped(); }

WidgetEvent* Event::WidgetEventPtr() { return mEvent; }

// static
Maybe<CSSDoublePoint> Event::GetScreenCoords(
    nsPresContext* aPresContext, WidgetEvent* aEvent,
    const LayoutDeviceDoublePoint& aWidgetOrScreenRelativePoint) {
  if (PointerLockManager::IsLocked()) {
    return Some(EventStateManager::sLastScreenPoint);
  }

  if (!aEvent || !aEvent->DOMEventSupportsCoords()) {
    return Nothing();
  }

  // Doing a straight conversion from LayoutDeviceDoublePoint to CSSDoublePoint
  // seem incorrect, but it is needed to maintain legacy functionality.
  const WidgetGUIEvent* guiEvent = aEvent->AsGUIEvent();
  if (MOZ_UNLIKELY(!aPresContext) || !(guiEvent && guiEvent->mWidget)) {
    // XXX aPresContext is usually available.  Then, we can know the latest
    // scale of the document.  Should we apply it?
    return Some(CSSDoublePoint(aWidgetOrScreenRelativePoint.x,
                               aWidgetOrScreenRelativePoint.y));
  }

  // (Potentially) transform the point from the coordinate space of an
  // out-of-process iframe to the coordinate space of the native
  // window. The transform can only be applied to a point whose components
  // are floating-point values, so convert the integer point first, then
  // transform, and then round the result back to an integer point.
  const LayoutDeviceIntPoint topLevelPoint = LayoutDeviceIntPoint::Round(
      guiEvent->mWidget->WidgetToTopLevelWidgetTransform().TransformPoint(
          aWidgetOrScreenRelativePoint));
  const CSSPoint pt = CSSPixel::FromAppUnits(
      LayoutDevicePixel::ToAppUnits(
          topLevelPoint, aPresContext->DeviceContext()->AppUnitsPerDevPixel()) +
      LayoutDevicePixel::ToAppUnits(
          guiEvent->mWidget->TopLevelWidgetToScreenOffset(),
          aPresContext->DeviceContext()->AppUnitsPerDevPixel()));
  return Some(CSSDoublePoint(pt.x, pt.y));
}

// static
CSSDoublePoint Event::GetPageCoords(
    nsPresContext* aPresContext, WidgetEvent* aEvent,
    const LayoutDeviceDoublePoint& aWidgetOrScreenRelativePoint,
    const CSSDoublePoint& aDefaultClientPoint) {
  const CSSDoublePoint clientCoords = Event::GetClientCoords(
      aPresContext, aEvent, aWidgetOrScreenRelativePoint, aDefaultClientPoint);

  // If there is some scrolling, add scroll info to client point.
  const CSSPoint scrollPoint = CSSPixel::FromAppUnits([&]() {
    if (aPresContext && aPresContext->GetPresShell()) {
      if (const ScrollContainerFrame* const sf =
              aPresContext->PresShell()->GetRootScrollContainerFrame()) {
        return sf->GetScrollPosition();
      }
    }
    return nsPoint{};
  }());
  return clientCoords + CSSDoublePoint(scrollPoint.x, scrollPoint.y);
}

// static
CSSDoublePoint Event::GetClientCoords(
    nsPresContext* aPresContext, WidgetEvent* aEvent,
    const LayoutDeviceDoublePoint& aWidgetOrScreenRelativePoint,
    const CSSDoublePoint& aDefaultClientPoint) {
  if (PointerLockManager::IsLocked()) {
    return EventStateManager::sLastClientPoint;
  }

  if (MOZ_UNLIKELY(!aPresContext) || MOZ_UNLIKELY(!aEvent) ||
      !aEvent->DOMEventSupportsCoords() ||
      MOZ_UNLIKELY(!aEvent->AsGUIEvent()->mWidget)) {
    return aDefaultClientPoint;
  }

  const PresShell* const presShell = aPresContext->GetPresShell();
  if (MOZ_UNLIKELY(!presShell)) {
    return CSSDoublePoint(0, 0);
  }
  // XXX Why don't we flush pending notifications before computing the offset
  // from the root frame?
  const nsIFrame* const rootFrame = presShell->GetRootFrame();
  if (MOZ_UNLIKELY(!rootFrame)) {
    return CSSDoublePoint(0, 0);
  }
  const CSSPoint pt =
      CSSPixel::FromAppUnits(nsLayoutUtils::GetEventCoordinatesRelativeTo(
          aEvent, LayoutDeviceIntPoint::Round(aWidgetOrScreenRelativePoint),
          RelativeTo{rootFrame}));
  return CSSDoublePoint(pt.x, pt.y);
}

// static
nsIFrame* Event::GetPrimaryFrameOfEventTarget(const nsPresContext& aPresContext,
                                              const WidgetEvent& aEvent) {
  const nsCOMPtr<nsIContent> content =
      nsIContent::FromEventTargetOrNull(aEvent.mTarget);
  if (!content) {
    return nullptr;
  }
  // XXX Even after the event target content is moved to different document, we
  // may get its primary frame.  In this case, should we return nullptr here?
  nsIFrame* const frame = content->GetPrimaryFrame(FlushType::Layout);
  if (MOZ_UNLIKELY(!frame || frame->PresContext() != &aPresContext)) {
    return nullptr;
  }
  // For compat, see https://github.com/w3c/csswg-drafts/issues/1508. In SVG
  // we just return the coordinates of the outer SVG box. This is all kinda
  // unfortunate.
  if (frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT) &&
      StaticPrefs::dom_events_offset_in_svg_relative_to_svg_root()) {
    return SVGUtils::GetOuterSVGFrame(frame);
  }
  return frame;
}

// static
CSSDoublePoint Event::GetOffsetCoords(
    nsPresContext* aPresContext, WidgetEvent* aEvent,
    const LayoutDeviceDoublePoint& aWidgetOrScreenRelativePoint,
    const CSSDoublePoint& aDefaultClientPoint) {
  if (!aEvent->mTarget) {
    return GetPageCoords(aPresContext, aEvent, aWidgetOrScreenRelativePoint,
                         aDefaultClientPoint);
  }
  if (!nsIContent::FromEventTarget(aEvent->mTarget) || !aPresContext) {
    return CSSDoublePoint();
  }
  const nsIFrame* const frame =
      GetPrimaryFrameOfEventTarget(*aPresContext, *aEvent);
  if (MOZ_UNLIKELY(!frame)) {
    return CSSDoublePoint();
  }
  MOZ_ASSERT(aPresContext->PresShell()->GetRootFrame());
  const CSSDoublePoint clientCoords = GetClientCoords(
      aPresContext, aEvent, aWidgetOrScreenRelativePoint, aDefaultClientPoint);
  nsPoint ptInAppUnits = CSSPixel::ToAppUnits(CSSPoint(
      static_cast<float>(clientCoords.x), static_cast<float>(clientCoords.y)));
  if (nsLayoutUtils::TransformPoint(
          RelativeTo{aPresContext->PresShell()->GetRootFrame()},
          RelativeTo{frame},
          ptInAppUnits) != nsLayoutUtils::TRANSFORM_SUCCEEDED) {
    return CSSDoublePoint();
  }
  ptInAppUnits -= frame->GetPaddingRectRelativeToSelf().TopLeft();
  const CSSPoint pt = CSSPixel::FromAppUnits(ptInAppUnits);
  return CSSDoublePoint(pt.x, pt.y);
}

// To be called ONLY by Event::GetType (which has the additional
// logic for handling user-defined events).
// static
const char16_t* Event::GetEventName(EventMessage aEventType) {
  switch (aEventType) {
#define MESSAGE_TO_EVENT(name_, _message, _type, _struct) \
  case _message:                                          \
    return u"" #name_;
#include "mozilla/EventNameList.h"
#undef MESSAGE_TO_EVENT
    default:
      break;
  }
  // XXXldb We can hit this case for WidgetEvent objects that we didn't
  // create and that are not user defined events since this function and
  // SetEventType are incomplete.  (But fixing that requires fixing the
  // arrays in nsEventListenerManager too, since the events for which
  // this is a problem generally *are* created by Event.)
  return nullptr;
}

bool Event::DefaultPrevented(CallerType aCallerType) const {
  NS_ENSURE_TRUE(mEvent, false);

  // If preventDefault() has never been called, just return false.
  if (!mEvent->DefaultPrevented()) {
    return false;
  }

  // If preventDefault() has been called by content, return true.  Otherwise,
  // i.e., preventDefault() has been called by chrome, return true only when
  // this is called by chrome.
  return mEvent->DefaultPreventedByContent() ||
         aCallerType == CallerType::System;
}

bool Event::ReturnValue(CallerType aCallerType) const {
  return !DefaultPrevented(aCallerType);
}

void Event::SetReturnValue(bool aReturnValue, CallerType aCallerType) {
  if (!aReturnValue) {
    PreventDefaultInternal(aCallerType == CallerType::System);
  }
}

double Event::TimeStamp() {
  if (mEvent->mTimeStamp.IsNull()) {
    return 0.0;
  }

  if (mIsMainThreadEvent) {
    if (NS_WARN_IF(!mOwner)) {
      return 0.0;
    }

    nsPIDOMWindowInner* win = mOwner->GetAsInnerWindow();
    if (NS_WARN_IF(!win)) {
      return 0.0;
    }

    Performance* perf = win->GetPerformance();
    if (NS_WARN_IF(!perf)) {
      return 0.0;
    }

    double ret =
        perf->GetDOMTiming()->TimeStampToDOMHighRes(mEvent->mTimeStamp);
    MOZ_ASSERT(mOwner->PrincipalOrNull());

    return nsRFPService::ReduceTimePrecisionAsMSecs(
        ret, perf->GetRandomTimelineSeed(), perf->GetRTPCallerType());
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  double ret = workerPrivate->TimeStampToDOMHighRes(mEvent->mTimeStamp);

  return nsRFPService::ReduceTimePrecisionAsMSecs(
      ret, workerPrivate->GetRandomTimelineSeed(),
      workerPrivate->GlobalScope()->GetRTPCallerType());
}

void Event::Serialize(IPC::MessageWriter* aWriter,
                      bool aSerializeInterfaceType) {
  if (aSerializeInterfaceType) {
    IPC::WriteParam(aWriter, u"event"_ns);
  }

  nsString type;
  GetType(type);
  IPC::WriteParam(aWriter, type);

  IPC::WriteParam(aWriter, Bubbles());
  IPC::WriteParam(aWriter, Cancelable());
  IPC::WriteParam(aWriter, IsTrusted());
  IPC::WriteParam(aWriter, Composed());

  // No timestamp serialization for now!
}

bool Event::Deserialize(IPC::MessageReader* aReader) {
  nsString type;
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &type), false);

  bool bubbles = false;
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &bubbles), false);

  bool cancelable = false;
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &cancelable), false);

  bool trusted = false;
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &trusted), false);

  bool composed = false;
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &composed), false);

  InitEvent(type, bubbles, cancelable);
  SetTrusted(trusted);
  SetComposed(composed);

  return true;
}

void Event::SetOwner(EventTarget* aOwner) {
  mOwner = nullptr;

  if (!aOwner) {
    return;
  }

  if (nsINode* n = aOwner->GetAsNode()) {
    mOwner = n->OwnerDoc()->GetScopeObject();
    return;
  }

  if (nsPIDOMWindowInner* w = aOwner->GetAsInnerWindow()) {
    mOwner = w->AsGlobal();
    return;
  }

  nsCOMPtr<DOMEventTargetHelper> eth = do_QueryInterface(aOwner);
  if (eth) {
    mOwner = eth->GetParentObject();
    return;
  }

#ifdef DEBUG
  nsCOMPtr<nsPIWindowRoot> root = do_QueryInterface(aOwner);
  MOZ_ASSERT(root, "Unexpected EventTarget!");
#endif
}

void Event::GetWidgetEventType(WidgetEvent* aEvent, nsAString& aType) {
  if (!aEvent->mSpecifiedEventTypeString.IsEmpty()) {
    aType = aEvent->mSpecifiedEventTypeString;
    return;
  }

  const char16_t* name = GetEventName(aEvent->mMessage);

  if (name) {
    aType.AssignLiteral(name, nsString::char_traits::length(name));
    return;
  } else if (aEvent->mMessage == eUnidentifiedEvent &&
             aEvent->mSpecifiedEventType) {
    // Remove "on"
    aType = Substring(nsDependentAtomString(aEvent->mSpecifiedEventType), 2);
    aEvent->mSpecifiedEventTypeString = aType;
    return;
  }

  aType.Truncate();
}

bool Event::IsDragExitEnabled(JSContext* aCx, JSObject* aGlobal) {
  return StaticPrefs::dom_event_dragexit_enabled() ||
         nsContentUtils::IsSystemCaller(aCx);
}

}  // namespace mozilla::dom

using namespace mozilla;
using namespace mozilla::dom;

already_AddRefed<Event> NS_NewDOMEvent(EventTarget* aOwner,
                                       nsPresContext* aPresContext,
                                       WidgetEvent* aEvent) {
  RefPtr<Event> it = new Event(aOwner, aPresContext, aEvent);
  return it.forget();
}
