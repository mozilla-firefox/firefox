/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Microsoft's API Name hackery sucks
#undef CreateEvent

#include "js/ColumnNumber.h"      // JS::ColumnNumberOneOrigin
#include "js/EnvironmentChain.h"  // JS::EnvironmentChain
#include "js/loader/LoadedScript.h"
#include "js/loader/ScriptFetchOptions.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/HalSensor.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/JSEventHandler.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/AbortSignal.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/EventCallbackDebuggerNotification.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventTargetBinding.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ChromeUtils.h"

#include "EventListenerService.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDOMCID.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "mozilla/dom/Document.h"
#include "nsIScriptGlobalObject.h"
#include "nsISupports.h"
#include "nsJSUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsSandboxFlags.h"
#include "xpcpublic.h"
#include "nsIFrame.h"
#include "nsDisplayList.h"
#include "nsPIWindowRoot.h"

namespace mozilla {

using namespace dom;
using namespace hal;

static uint32_t MutationBitForEventType(EventMessage aEventType) {
  switch (aEventType) {
    case eLegacySubtreeModified:
      return NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED;
    case eLegacyNodeInserted:
      return NS_EVENT_BITS_MUTATION_NODEINSERTED;
    case eLegacyNodeRemoved:
      return NS_EVENT_BITS_MUTATION_NODEREMOVED;
    case eLegacyNodeRemovedFromDocument:
      return NS_EVENT_BITS_MUTATION_NODEREMOVEDFROMDOCUMENT;
    case eLegacyNodeInsertedIntoDocument:
      return NS_EVENT_BITS_MUTATION_NODEINSERTEDINTODOCUMENT;
    case eLegacyAttrModified:
      return NS_EVENT_BITS_MUTATION_ATTRMODIFIED;
    case eLegacyCharacterDataModified:
      return NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED;
    default:
      break;
  }
  return 0;
}

static DeprecatedOperations DeprecatedMutationOperation(EventMessage aMessage) {
  switch (aMessage) {
    case eLegacySubtreeModified:
      return DeprecatedOperations::eDOMSubtreeModified;
    case eLegacyNodeInserted:
      return DeprecatedOperations::eDOMNodeInserted;
    case eLegacyNodeRemoved:
      return DeprecatedOperations::eDOMNodeRemoved;
    case eLegacyNodeRemovedFromDocument:
      return DeprecatedOperations::eDOMNodeRemovedFromDocument;
    case eLegacyNodeInsertedIntoDocument:
      return DeprecatedOperations::eDOMNodeInsertedIntoDocument;
    case eLegacyAttrModified:
      return DeprecatedOperations::eDOMAttrModified;
    case eLegacyCharacterDataModified:
      return DeprecatedOperations::eDOMCharacterDataModified;
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE(
          "aMessage restricted by switch in AddEventListenerInternal");
  }
}

class ListenerMapEntryComparator {
 public:
  explicit ListenerMapEntryComparator(nsAtom* aTarget)
      : mAddressOfEventType(reinterpret_cast<uintptr_t>(aTarget)) {}

  int operator()(
      const EventListenerManager::EventListenerMapEntry& aEntry) const {
    uintptr_t value = reinterpret_cast<uintptr_t>(aEntry.mTypeAtom.get());
    if (mAddressOfEventType == value) {
      return 0;
    }

    if (mAddressOfEventType < value) {
      return -1;
    }

    return 1;
  }

 private:
  const uintptr_t mAddressOfEventType;  // the address of the atom, can be 0
};

uint32_t EventListenerManager::sMainThreadCreatedCount = 0;

EventListenerManagerBase::EventListenerManagerBase()
    : mMayHaveDOMActivateEventListener(false),
      mMayHaveMutationListeners(false),
      mMayHaveCapturingListeners(false),
      mMayHaveSystemGroupListeners(false),
      mMayHaveTouchEventListener(false),
      mMayHaveMouseEnterLeaveEventListener(false),
      mMayHavePointerEnterLeaveEventListener(false),
      mMayHaveSelectionChangeEventListener(false),
      mMayHaveFormSelectEventListener(false),
      mMayHaveTransitionEventListener(false),
      mMayHaveSMILTimeEventListener(false),
      mClearingListeners(false),
      mIsMainThreadELM(NS_IsMainThread()),
      mMayHaveListenersForUntrustedEvents(false) {
  ClearNoListenersForEvents();
  static_assert(sizeof(EventListenerManagerBase) == sizeof(uint64_t),
                "Keep the size of EventListenerManagerBase size compact!");
}

EventListenerManager::EventListenerManager(EventTarget* aTarget)
    : mTarget(aTarget) {
  NS_ASSERTION(aTarget, "unexpected null pointer");

  if (mIsMainThreadELM) {
    mRefCnt.SetIsOnMainThread();
    ++sMainThreadCreatedCount;
  }
}

EventListenerManager::~EventListenerManager() {
  // If your code fails this assertion, a possible reason is that
  // a class did not call our Disconnect() manually. Note that
  // this class can have Disconnect called in one of two ways:
  // if it is part of a cycle, then in Unlink() (such a cycle
  // would be with one of the listeners, not mTarget which is weak).
  // If not part of a cycle, then Disconnect must be called manually,
  // typically from the destructor of the owner class (mTarget).
  // XXX azakai: Is there any reason to not just call Disconnect
  //             from right here, if not previously called?
  NS_ASSERTION(!mTarget, "didn't call Disconnect");
  RemoveAllListenersSilently();
}

void EventListenerManager::RemoveAllListenersSilently() {
  if (mClearingListeners) {
    return;
  }
  mClearingListeners = true;
  mListenerMap.Clear();
  mClearingListeners = false;
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    EventListenerManager::EventListenerMap& aField, const char* aName,
    uint32_t aFlags = 0) {
  if (MOZ_UNLIKELY(aCallback.WantDebugInfo())) {
    nsAutoCString name;
    name.AppendASCII(aName);
    name.AppendLiteral(" mEntries[i] event=");
    size_t entryPrefixLen = name.Length();
    for (const auto& entry : aField.mEntries) {
      if (entry.mTypeAtom) {
        name.Replace(entryPrefixLen, name.Length() - entryPrefixLen,
                     nsAtomCString(entry.mTypeAtom));
      } else {
        name.Replace(entryPrefixLen, name.Length() - entryPrefixLen,
                     "(all)"_ns);
      }
      ImplCycleCollectionTraverse(aCallback, *entry.mListeners, name.get());
    }
  } else {
    for (const auto& entry : aField.mEntries) {
      ImplCycleCollectionTraverse(aCallback, *entry.mListeners,
                                  ".mEntries[i].mListeners");
    }
  }
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    EventListenerManager::Listener& aField, const char* aName,
    unsigned aFlags) {
  if (MOZ_UNLIKELY(aCallback.WantDebugInfo())) {
    nsAutoCString name;
    name.AppendASCII(aName);
    name.AppendLiteral(" listenerType=");
    name.AppendInt(aField.mListenerType);
    name.AppendLiteral(" ");
    CycleCollectionNoteChild(aCallback, aField.mListener.GetISupports(),
                             name.get(), aFlags);
  } else {
    CycleCollectionNoteChild(aCallback, aField.mListener.GetISupports(), aName,
                             aFlags);
  }

  CycleCollectionNoteChild(aCallback, aField.mSignalFollower.get(),
                           "mSignalFollower", aFlags);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(EventListenerManager)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(EventListenerManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mListenerMap);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(EventListenerManager)
  tmp->Disconnect();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

nsPIDOMWindowInner* EventListenerManager::GetInnerWindowForTarget() {
  if (nsINode* node = nsINode::FromEventTargetOrNull(mTarget)) {
    // XXX sXBL/XBL2 issue -- do we really want the owner here?  What
    // if that's the XBL document?
    return node->OwnerDoc()->GetInnerWindow();
  }

  nsCOMPtr<nsPIDOMWindowInner> window = GetTargetAsInnerWindow();
  return window;
}

already_AddRefed<nsPIDOMWindowInner>
EventListenerManager::GetTargetAsInnerWindow() const {
  nsCOMPtr<nsPIDOMWindowInner> window =
      nsPIDOMWindowInner::FromEventTargetOrNull(mTarget);
  return window.forget();
}

void EventListenerManager::AddEventListenerInternal(
    EventListenerHolder aListenerHolder, EventMessage aEventMessage,
    nsAtom* aTypeAtom, const EventListenerFlags& aFlags, bool aHandler,
    bool aAllEvents, AbortSignal* aSignal) {
  MOZ_ASSERT((aEventMessage && aTypeAtom) || aAllEvents,  // all-events listener
             "Missing type");
  MOZ_ASSERT_IF(
      aEventMessage != eUnidentifiedEvent && !aAllEvents,
      aTypeAtom == nsContentUtils::GetEventTypeFromMessage(aEventMessage));

  if (!aListenerHolder || mClearingListeners) {
    return;
  }

  if (aSignal && aSignal->Aborted()) {
    return;
  }

  // Since there is no public API to call us with an EventListenerHolder, we
  // know that there's an EventListenerHolder on the stack holding a strong ref
  // to the listener.

  RefPtr<ListenerArray> listeners =
      aAllEvents ? mListenerMap.GetOrCreateListenersForAllEvents()
                 : mListenerMap.GetOrCreateListenersForType(aTypeAtom);

  for (const Listener& listener : listeners->NonObservingRange()) {
    // mListener == aListenerHolder is the last one, since it can be a bit slow.
    if (listener.mListenerIsHandler == aHandler &&
        listener.mFlags.EqualsForAddition(aFlags) &&
        listener.mListener == aListenerHolder) {
      return;
    }
  }

  ClearNoListenersForEvents();
  mNoListenerForEventAtom = nullptr;

  Listener* listener = listeners->AppendElement();
  listener->mFlags = aFlags;
  listener->mListenerIsHandler = aHandler;
  listener->mHandlerIsString = false;
  listener->mAllEvents = aAllEvents;

  if (listener->mFlags.mAllowUntrustedEvents) {
    mMayHaveListenersForUntrustedEvents = true;
  }

  // Detect the type of event listener.
  if (aFlags.mListenerIsJSListener) {
    MOZ_ASSERT(!aListenerHolder.HasWebIDLCallback());
    listener->mListenerType = Listener::eJSEventListener;
  } else if (aListenerHolder.HasWebIDLCallback()) {
    listener->mListenerType = Listener::eWebIDLListener;
  } else {
    listener->mListenerType = Listener::eNativeListener;
  }
  listener->mListener = std::move(aListenerHolder);

  if (aSignal) {
    listener->mSignalFollower =
        new ListenerSignalFollower(this, listener, aTypeAtom);
    listener->mSignalFollower->Follow(aSignal);
  }

  if (aFlags.mInSystemGroup) {
    mMayHaveSystemGroupListeners = true;
  }
  if (aFlags.mCapture) {
    mMayHaveCapturingListeners = true;
  }

  // Events which are not supported in the running environment is mapped to
  // eUnidentifiedEvent.  Then, we need to consider the proper event message
  // with comparing the atom.
  {
    EventMessage resolvedEventMessage = aEventMessage;
    if (resolvedEventMessage == eUnidentifiedEvent && aTypeAtom->IsStatic()) {
      // TouchEvents are registered only when
      // nsContentUtils::InitializeTouchEventTable() is called.
      if (aTypeAtom == nsGkAtoms::ontouchstart) {
        resolvedEventMessage = eTouchStart;
      } else if (aTypeAtom == nsGkAtoms::ontouchend) {
        resolvedEventMessage = eTouchEnd;
      } else if (aTypeAtom == nsGkAtoms::ontouchmove) {
        resolvedEventMessage = eTouchMove;
      } else if (aTypeAtom == nsGkAtoms::ontouchcancel) {
        resolvedEventMessage = eTouchCancel;
      }
    }

    switch (resolvedEventMessage) {
      case eLegacyDOMActivate:
        mMayHaveDOMActivateEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasDOMActivateEventListeners();
        }
        break;
      case eLegacySubtreeModified:
      case eLegacyNodeInserted:
      case eLegacyNodeRemoved:
      case eLegacyNodeRemovedFromDocument:
      case eLegacyNodeInsertedIntoDocument:
      case eLegacyAttrModified:
      case eLegacyCharacterDataModified: {
        MOZ_ASSERT(!aFlags.mInSystemGroup,
                   "Legacy mutation events shouldn't be handled by ourselves");
        MOZ_ASSERT(listener->mListenerType != Listener::eNativeListener,
                   "Legacy mutation events shouldn't be handled in C++ code");
        DebugOnly<nsINode*> targetNode =
            nsINode::FromEventTargetOrNull(mTarget);
        // Legacy mutation events shouldn't be handled in chrome documents.
        MOZ_ASSERT_IF(targetNode,
                      !nsContentUtils::IsChromeDoc(targetNode->OwnerDoc()));
        // Legacy mutation events shouldn't listen to mutations in native
        // anonymous subtrees.
        MOZ_ASSERT_IF(targetNode, !targetNode->IsInNativeAnonymousSubtree());
        // For mutation listeners, we need to update the global bit on the DOM
        // window. Otherwise we won't actually fire the mutation event.
        mMayHaveMutationListeners = true;
        // Go from our target to the nearest enclosing DOM window.
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          if (Document* doc = window->GetExtantDoc()) {
            doc->WarnOnceAbout(
                DeprecatedMutationOperation(resolvedEventMessage));
          }
          // If resolvedEventMessage is eLegacySubtreeModified, we need to
          // listen all mutations. nsContentUtils::HasMutationListeners relies
          // on this.
          window->SetMutationListeners(
              (resolvedEventMessage == eLegacySubtreeModified)
                  ? NS_EVENT_BITS_MUTATION_ALL
                  : MutationBitForEventType(resolvedEventMessage));
        }
        break;
      }
      case ePointerEnter:
      case ePointerLeave:
        mMayHavePointerEnterLeaveEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          NS_WARNING_ASSERTION(
              !nsContentUtils::IsChromeDoc(window->GetExtantDoc()),
              "Please do not use pointerenter/leave events in chrome. "
              "They are slower than pointerover/out!");
          window->SetHasPointerEnterLeaveEventListeners();
        }
        break;
      case eGamepadButtonDown:
      case eGamepadButtonUp:
      case eGamepadAxisMove:
      case eGamepadConnected:
      case eGamepadDisconnected:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasGamepadEventListener();
        }
        break;
      case eDeviceOrientation:
      case eDeviceOrientationAbsolute:
      case eUserProximity:
      case eDeviceLight:
      case eDeviceMotion:
#if defined(MOZ_WIDGET_ANDROID)
      case eOrientationChange:
#endif  // #if defined(MOZ_WIDGET_ANDROID)
        EnableDevice(aTypeAtom);
        break;
      case eTouchStart:
      case eTouchEnd:
      case eTouchMove:
      case eTouchCancel:
        mMayHaveTouchEventListener = true;
        // we don't want touchevent listeners added by scrollbars to flip this
        // flag so we ignore listeners created with system event flag
        if (!aFlags.mInSystemGroup) {
          if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
            window->SetHasTouchEventListeners();
          }
        }
        break;
      case eMouseEnter:
      case eMouseLeave:
        mMayHaveMouseEnterLeaveEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          NS_WARNING_ASSERTION(
              !nsContentUtils::IsChromeDoc(window->GetExtantDoc()),
              "Please do not use mouseenter/leave events in chrome. "
              "They are slower than mouseover/out!");
          window->SetHasMouseEnterLeaveEventListeners();
        }
        break;
      case eEditorBeforeInput:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasBeforeInputEventListenersForTelemetry();
        }
        break;
      case eSelectionChange:
        mMayHaveSelectionChangeEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasSelectionChangeEventListeners();
        }
        break;
      case eFormSelect:
        mMayHaveFormSelectEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasFormSelectEventListeners();
        }
        break;
      case eLegacyMouseLineOrPageScroll:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          if (Document* doc = window->GetExtantDoc()) {
            doc->SetUseCounter(eUseCounter_custom_ondommousescroll);
          }
        }
        break;
      case eLegacyMousePixelScroll:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          if (Document* doc = window->GetExtantDoc()) {
            doc->SetUseCounter(eUseCounter_custom_onmozmousepixelscroll);
          }
        }
        break;
      case eTransitionStart:
      case eTransitionRun:
      case eTransitionEnd:
      case eTransitionCancel:
      case eWebkitTransitionEnd:
        mMayHaveTransitionEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasTransitionEventListeners();
        }
        break;
      case eSMILBeginEvent:
      case eSMILEndEvent:
      case eSMILRepeatEvent:
        mMayHaveSMILTimeEventListener = true;
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          window->SetHasSMILTimeEventListeners();
        }
        break;
      case eFormCheckboxStateChange:
        nsContentUtils::SetMayHaveFormCheckboxStateChangeListeners();
        break;
      case eFormRadioStateChange:
        nsContentUtils::SetMayHaveFormRadioStateChangeListeners();
        break;
      case eAfterScriptExecute:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          if (Document* doc = window->GetExtantDoc()) {
            doc->SetUseCounter(eUseCounter_AfterScriptExecuteEvent);
            doc->WarnOnceAbout(DeprecatedOperations::eAfterScriptExecuteEvent);
          }
        }
        break;
      case eBeforeScriptExecute:
        if (nsPIDOMWindowInner* window = GetInnerWindowForTarget()) {
          if (Document* doc = window->GetExtantDoc()) {
            doc->SetUseCounter(eUseCounter_BeforeScriptExecuteEvent);
            doc->WarnOnceAbout(DeprecatedOperations::eBeforeScriptExecuteEvent);
          }
        }
        break;
      default:
        // XXX Use NS_ASSERTION here to print resolvedEventMessage since
        //     MOZ_ASSERT can take only string literal, not pointer to
        //     characters.
        NS_ASSERTION(
            resolvedEventMessage < eLegacyMutationEventFirst ||
                resolvedEventMessage > eLegacyMutationEventLast,
            nsPrintfCString("You added new mutation event, but it's not "
                            "handled above, resolvedEventMessage=%s",
                            ToChar(resolvedEventMessage))
                .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onpointerenter,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onpointerleave,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(
            resolvedEventMessage < eGamepadEventFirst ||
                resolvedEventMessage > eGamepadEventLast,
            nsPrintfCString("You added new gamepad event, but it's not "
                            "handled above, resolvedEventMessage=%s",
                            ToChar(resolvedEventMessage))
                .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ondeviceorientation,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ondeviceorientationabsolute,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onuserproximity,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ondevicelight,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ondevicemotion,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
#if defined(MOZ_WIDGET_ANDROID)
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onorientationchange,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
#endif  // #if defined(MOZ_WIDGET_ANDROID)
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ontouchstart,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ontouchend,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ontouchmove,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::ontouchcancel,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onmouseenter,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onmouseleave,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onbeforeinput,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onselectionchange,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onselect,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onDOMMouseScroll,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        NS_ASSERTION(aTypeAtom != nsGkAtoms::onMozMousePixelScroll,
                     nsPrintfCString("resolvedEventMessage=%s",
                                     ToChar(resolvedEventMessage))
                         .get());
        break;
    }
  }

  if (mIsMainThreadELM && !aFlags.mPassive && IsApzAwareEvent(aTypeAtom)) {
    ProcessApzAwareEventListenerAdd();
  }

  if (mTarget) {
    mTarget->EventListenerAdded(aTypeAtom);
  }

  if (mIsMainThreadELM && mTarget) {
    EventListenerService::NotifyAboutMainThreadListenerChange(mTarget,
                                                              aTypeAtom);
  }
}

void EventListenerManager::ProcessApzAwareEventListenerAdd() {
  Document* doc = nullptr;

  // Mark the node as having apz aware listeners
  if (nsINode* node = nsINode::FromEventTargetOrNull(mTarget)) {
    node->SetMayBeApzAware();
    doc = node->OwnerDoc();
  }

  // Schedule a paint so event regions on the layer tree gets updated
  if (!doc) {
    if (nsCOMPtr<nsPIDOMWindowInner> window = GetTargetAsInnerWindow()) {
      doc = window->GetExtantDoc();
    }
  }
  if (!doc) {
    if (nsCOMPtr<DOMEventTargetHelper> helper = do_QueryInterface(mTarget)) {
      if (nsPIDOMWindowInner* window = helper->GetOwnerWindow()) {
        doc = window->GetExtantDoc();
      }
    }
  }

  if (doc && gfxPlatform::AsyncPanZoomEnabled()) {
    PresShell* presShell = doc->GetPresShell();
    if (presShell) {
      nsIFrame* f = presShell->GetRootFrame();
      if (f) {
        f->SchedulePaint();
      }
    }
  }
}

bool EventListenerManager::IsDeviceType(nsAtom* aTypeAtom) {
  return aTypeAtom == nsGkAtoms::ondeviceorientation ||
         aTypeAtom == nsGkAtoms::ondeviceorientationabsolute ||
         aTypeAtom == nsGkAtoms::ondevicemotion ||
         aTypeAtom == nsGkAtoms::ondevicelight
#if defined(MOZ_WIDGET_ANDROID)
         || aTypeAtom == nsGkAtoms::onorientationchange
#endif
         || aTypeAtom == nsGkAtoms::onuserproximity;
}

void EventListenerManager::EnableDevice(nsAtom* aTypeAtom) {
  nsCOMPtr<nsPIDOMWindowInner> window = GetTargetAsInnerWindow();
  if (!window) {
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondeviceorientation) {
#ifdef MOZ_WIDGET_ANDROID
    // Falls back to SENSOR_ROTATION_VECTOR and SENSOR_ORIENTATION if
    // unavailable on device.
    window->EnableDeviceSensor(SENSOR_GAME_ROTATION_VECTOR);
    window->EnableDeviceSensor(SENSOR_ROTATION_VECTOR);
#else
    window->EnableDeviceSensor(SENSOR_ORIENTATION);
#endif
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondeviceorientationabsolute) {
#ifdef MOZ_WIDGET_ANDROID
    // Falls back to SENSOR_ORIENTATION if unavailable on device.
    window->EnableDeviceSensor(SENSOR_ROTATION_VECTOR);
#else
    window->EnableDeviceSensor(SENSOR_ORIENTATION);
#endif
    return;
  }

  if (aTypeAtom == nsGkAtoms::onuserproximity) {
    window->EnableDeviceSensor(SENSOR_PROXIMITY);
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondevicelight) {
    window->EnableDeviceSensor(SENSOR_LIGHT);
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondevicemotion) {
    window->EnableDeviceSensor(SENSOR_ACCELERATION);
    window->EnableDeviceSensor(SENSOR_LINEAR_ACCELERATION);
    window->EnableDeviceSensor(SENSOR_GYROSCOPE);
    return;
  }

#if defined(MOZ_WIDGET_ANDROID)
  if (aTypeAtom == nsGkAtoms::onorientationchange) {
    window->EnableOrientationChangeListener();
    return;
  }
#endif

  NS_WARNING("Enabling an unknown device sensor.");
}

void EventListenerManager::DisableDevice(nsAtom* aTypeAtom) {
  nsCOMPtr<nsPIDOMWindowInner> window = GetTargetAsInnerWindow();
  if (!window) {
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondeviceorientation) {
#ifdef MOZ_WIDGET_ANDROID
    // Disable all potential fallback sensors.
    window->DisableDeviceSensor(SENSOR_GAME_ROTATION_VECTOR);
    window->DisableDeviceSensor(SENSOR_ROTATION_VECTOR);
#endif
    window->DisableDeviceSensor(SENSOR_ORIENTATION);
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondeviceorientationabsolute) {
#ifdef MOZ_WIDGET_ANDROID
    window->DisableDeviceSensor(SENSOR_ROTATION_VECTOR);
#endif
    window->DisableDeviceSensor(SENSOR_ORIENTATION);
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondevicemotion) {
    window->DisableDeviceSensor(SENSOR_ACCELERATION);
    window->DisableDeviceSensor(SENSOR_LINEAR_ACCELERATION);
    window->DisableDeviceSensor(SENSOR_GYROSCOPE);
    return;
  }

  if (aTypeAtom == nsGkAtoms::onuserproximity) {
    window->DisableDeviceSensor(SENSOR_PROXIMITY);
    return;
  }

  if (aTypeAtom == nsGkAtoms::ondevicelight) {
    window->DisableDeviceSensor(SENSOR_LIGHT);
    return;
  }

#if defined(MOZ_WIDGET_ANDROID)
  if (aTypeAtom == nsGkAtoms::onorientationchange) {
    window->DisableOrientationChangeListener();
    return;
  }
#endif

  NS_WARNING("Disabling an unknown device sensor.");
}

void EventListenerManager::NotifyEventListenerRemoved(nsAtom* aUserType) {
  // If the following code is changed, other callsites of EventListenerRemoved
  // and NotifyAboutMainThreadListenerChange should be changed too.
  ClearNoListenersForEvents();
  mNoListenerForEventAtom = nullptr;
  if (mTarget) {
    mTarget->EventListenerRemoved(aUserType);
  }
  if (mIsMainThreadELM && mTarget) {
    EventListenerService::NotifyAboutMainThreadListenerChange(mTarget,
                                                              aUserType);
  }
}

void EventListenerManager::RemoveEventListenerInternal(
    EventListenerHolder aListenerHolder, nsAtom* aUserType,
    const EventListenerFlags& aFlags, bool aAllEvents) {
  if (!aListenerHolder || (!aUserType && !aAllEvents) || mClearingListeners) {
    return;
  }

  Maybe<size_t> entryIndex = aAllEvents
                                 ? mListenerMap.EntryIndexForAllEvents()
                                 : mListenerMap.EntryIndexForType(aUserType);
  if (!entryIndex) {
    return;
  }

  ListenerArray& listenerArray = *mListenerMap.mEntries[*entryIndex].mListeners;

  Maybe<uint32_t> listenerIndex = [&]() -> Maybe<uint32_t> {
    uint32_t count = listenerArray.Length();
    for (uint32_t i = 0; i < count; ++i) {
      Listener* listener = &listenerArray.ElementAt(i);
      if (listener->mListener == aListenerHolder &&
          listener->mFlags.EqualsForRemoval(aFlags)) {
        return Some(i);
      }
    }
    return Nothing();
  }();

  if (!listenerIndex) {
    return;
  }

  listenerArray.RemoveElementAt(*listenerIndex);
  if (listenerArray.IsEmpty()) {
    mListenerMap.mEntries.RemoveElementAt(*entryIndex);
  }

  RefPtr<EventListenerManager> kungFuDeathGrip(this);
  if (!aAllEvents) {
    NotifyEventListenerRemoved(aUserType);
    if (IsDeviceType(aUserType)) {
      DisableDevice(aUserType);
    }
  }
}

static bool IsDefaultPassiveWhenOnRoot(EventMessage aMessage) {
  if (aMessage == eTouchStart || aMessage == eTouchMove || aMessage == eWheel ||
      aMessage == eLegacyMouseLineOrPageScroll ||
      aMessage == eLegacyMousePixelScroll) {
    return true;
  }
  return false;
}

static bool IsRootEventTarget(EventTarget* aTarget) {
  if (!aTarget) {
    return false;
  }
  if (aTarget->IsInnerWindow()) {
    return true;
  }
  const nsINode* node = nsINode::FromEventTarget(aTarget);
  if (!node) {
    return false;
  }
  Document* doc = node->OwnerDoc();
  return node == doc || node == doc->GetRootElement() || node == doc->GetBody();
}

void EventListenerManager::MaybeMarkPassive(EventMessage aMessage,
                                            EventListenerFlags& aFlags) {
  if (!mIsMainThreadELM) {
    return;
  }
  if (!IsDefaultPassiveWhenOnRoot(aMessage)) {
    return;
  }
  if (!IsRootEventTarget(mTarget)) {
    return;
  }
  aFlags.mPassive = true;
}

void EventListenerManager::AddEventListenerByType(
    EventListenerHolder aListenerHolder, const nsAString& aType,
    const EventListenerFlags& aFlags, const Optional<bool>& aPassive,
    AbortSignal* aSignal) {
  RefPtr<nsAtom> atom;
  EventMessage message =
      GetEventMessageAndAtomForListener(aType, getter_AddRefs(atom));

  EventListenerFlags flags = aFlags;
  if (aPassive.WasPassed()) {
    flags.mPassive = aPassive.Value();
  } else {
    MaybeMarkPassive(message, flags);
  }

  AddEventListenerInternal(std::move(aListenerHolder), message, atom, flags,
                           false, false, aSignal);
}

void EventListenerManager::RemoveEventListenerByType(
    EventListenerHolder aListenerHolder, const nsAString& aType,
    const EventListenerFlags& aFlags) {
  RefPtr<nsAtom> atom;
  (void)GetEventMessageAndAtomForListener(aType, getter_AddRefs(atom));
  RemoveEventListenerInternal(std::move(aListenerHolder), atom, aFlags);
}

EventListenerManager::Listener* EventListenerManager::FindEventHandler(
    nsAtom* aTypeAtom) {
  // Run through the listeners for this type and see if a script
  // listener is registered
  RefPtr<ListenerArray> listeners = mListenerMap.GetListenersForType(aTypeAtom);
  if (!listeners) {
    return nullptr;
  }

  uint32_t count = listeners->Length();
  for (uint32_t i = 0; i < count; ++i) {
    Listener* listener = &listeners->ElementAt(i);
    if (listener->mListenerIsHandler) {
      return listener;
    }
  }
  return nullptr;
}

EventListenerManager::Listener* EventListenerManager::SetEventHandlerInternal(
    nsAtom* aName, const TypedEventHandler& aTypedHandler,
    bool aPermitUntrustedEvents) {
  MOZ_ASSERT(aName);

  EventMessage eventMessage = GetEventMessage(aName);
  Listener* listener = FindEventHandler(aName);

  if (!listener) {
    // If we didn't find a script listener or no listeners existed
    // create and add a new one.
    EventListenerFlags flags;
    flags.mListenerIsJSListener = true;
    MaybeMarkPassive(eventMessage, flags);

    nsCOMPtr<JSEventHandler> jsEventHandler;
    NS_NewJSEventHandler(mTarget, aName, aTypedHandler,
                         getter_AddRefs(jsEventHandler));
    AddEventListenerInternal(EventListenerHolder(jsEventHandler), eventMessage,
                             aName, flags, true);

    listener = FindEventHandler(aName);
  } else {
    JSEventHandler* jsEventHandler = listener->GetJSEventHandler();
    MOZ_ASSERT(jsEventHandler,
               "How can we have an event handler with no JSEventHandler?");

    bool same = jsEventHandler->GetTypedEventHandler() == aTypedHandler;
    // Possibly the same listener, but update still the context and scope.
    jsEventHandler->SetHandler(aTypedHandler);
    if (mTarget && !same) {
      mTarget->EventListenerRemoved(aName);
      mTarget->EventListenerAdded(aName);
    }
    if (mIsMainThreadELM && mTarget) {
      EventListenerService::NotifyAboutMainThreadListenerChange(mTarget, aName);
    }
  }

  // Set flag to indicate possible need for compilation later
  listener->mHandlerIsString = !aTypedHandler.HasEventHandler();
  if (aPermitUntrustedEvents) {
    listener->mFlags.mAllowUntrustedEvents = true;
    mMayHaveListenersForUntrustedEvents = true;
  }

  return listener;
}

nsresult EventListenerManager::SetEventHandler(nsAtom* aName,
                                               const nsAString& aBody,
                                               bool aDeferCompilation,
                                               bool aPermitUntrustedEvents,
                                               Element* aElement) {
  auto removeEventHandler = MakeScopeExit([&] { RemoveEventHandler(aName); });

  nsCOMPtr<Document> doc;
  nsCOMPtr<nsIScriptGlobalObject> global =
      GetScriptGlobalAndDocument(getter_AddRefs(doc));

  if (!global) {
    // This can happen; for example this document might have been
    // loaded as data.
    return NS_OK;
  }

  nsresult rv = NS_OK;
  // return early preventing the event listener from being added
  // 'doc' is fetched above
  if (doc) {
    // Don't allow adding an event listener if the document is sandboxed
    // without 'allow-scripts'.
    if (doc->HasScriptsBlockedBySandbox()) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    // Perform CSP check
    nsCOMPtr<nsIContentSecurityPolicy> csp = doc->GetCsp();
    uint32_t lineNum = 0;
    JS::ColumnNumberOneOrigin columnNum;

    JSContext* cx = nsContentUtils::GetCurrentJSContext();
    if (cx) {
      JS::DescribeScriptedCaller(nullptr, cx, &lineNum, &columnNum);
    }

    if (csp) {
      bool allowsInlineScript = true;
      rv = csp->GetAllowsInline(
          nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE,
          true,    // aHasUnsafeHash
          u""_ns,  // aNonce
          true,    // aParserCreated (true because attribute event handler)
          aElement,
          nullptr,  // nsICSPEventListener
          aBody, lineNum, columnNum.oneOriginValue(), &allowsInlineScript);
      NS_ENSURE_SUCCESS(rv, rv);

      // return early if CSP wants us to block inline scripts
      if (!allowsInlineScript) {
        return NS_OK;
      }
    }
  }

  // This might be the first reference to this language in the global
  // We must init the language before we attempt to fetch its context.
  if (NS_FAILED(global->EnsureScriptEnvironment())) {
    NS_WARNING("Failed to setup script environment for this language");
    // but fall through and let the inevitable failure below handle it.
  }

  nsIScriptContext* context = global->GetScriptContext();
  NS_ENSURE_TRUE(context, NS_ERROR_FAILURE);
  NS_ENSURE_STATE(global->HasJSGlobal());

  removeEventHandler.release();

  Listener* listener = SetEventHandlerInternal(aName, TypedEventHandler(),
                                               aPermitUntrustedEvents);

  if (!aDeferCompilation) {
    return CompileEventHandlerInternal(listener, aName, &aBody, aElement);
  }

  return NS_OK;
}

void EventListenerManager::RemoveEventHandler(nsAtom* aName) {
  if (mClearingListeners) {
    return;
  }

  Maybe<size_t> entryIndex = mListenerMap.EntryIndexForType(aName);
  if (!entryIndex) {
    return;
  }

  ListenerArray& listenerArray = *mListenerMap.mEntries[*entryIndex].mListeners;

  Maybe<uint32_t> listenerIndex = [&]() -> Maybe<uint32_t> {
    uint32_t count = listenerArray.Length();
    for (uint32_t i = 0; i < count; ++i) {
      Listener* listener = &listenerArray.ElementAt(i);
      if (listener->mListenerIsHandler) {
        return Some(i);
      }
    }
    return Nothing();
  }();

  if (!listenerIndex) {
    return;
  }

  listenerArray.RemoveElementAt(*listenerIndex);
  if (listenerArray.IsEmpty()) {
    mListenerMap.mEntries.RemoveElementAt(*entryIndex);
  }

  RefPtr<EventListenerManager> kungFuDeathGrip(this);
  NotifyEventListenerRemoved(aName);
  if (IsDeviceType(aName)) {
    DisableDevice(aName);
  }
}

nsresult EventListenerManager::CompileEventHandlerInternal(
    Listener* aListener, nsAtom* aTypeAtom, const nsAString* aBody,
    Element* aElement) {
  MOZ_ASSERT(aListener->GetJSEventHandler());
  MOZ_ASSERT(aListener->mHandlerIsString,
             "Why are we compiling a non-string JS listener?");
  JSEventHandler* jsEventHandler = aListener->GetJSEventHandler();
  MOZ_ASSERT(!jsEventHandler->GetTypedEventHandler().HasEventHandler(),
             "What is there to compile?");

  nsresult result = NS_OK;
  nsCOMPtr<Document> doc;
  nsCOMPtr<nsIScriptGlobalObject> global =
      GetScriptGlobalAndDocument(getter_AddRefs(doc));
  NS_ENSURE_STATE(global);

  // Activate JSAPI, and make sure that exceptions are reported on the right
  // Window.
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(global))) {
    return NS_ERROR_UNEXPECTED;
  }
  JSContext* cx = jsapi.cx();

  nsAtom* attrName = aTypeAtom;

  // Flag us as not a string so we don't keep trying to compile strings which
  // can't be compiled.
  aListener->mHandlerIsString = false;

  // mTarget may not be an Element if it's a window and we're
  // getting an inline event listener forwarded from <html:body> or
  // <html:frameset> or <xul:window> or the like.
  // XXX I don't like that we have to reference content from
  // here. The alternative is to store the event handler string on
  // the JSEventHandler itself, and that still doesn't address
  // the arg names issue.
  RefPtr<Element> element = Element::FromEventTargetOrNull(mTarget);
  MOZ_ASSERT(element || aBody, "Where will we get our body?");
  nsAutoString handlerBody;
  const nsAString* body = aBody;
  if (!aBody) {
    if (aTypeAtom == nsGkAtoms::onSVGLoad) {
      attrName = nsGkAtoms::onload;
    } else if (aTypeAtom == nsGkAtoms::onSVGScroll) {
      attrName = nsGkAtoms::onscroll;
    } else if (aTypeAtom == nsGkAtoms::onbeginEvent) {
      attrName = nsGkAtoms::onbegin;
    } else if (aTypeAtom == nsGkAtoms::onrepeatEvent) {
      attrName = nsGkAtoms::onrepeat;
    } else if (aTypeAtom == nsGkAtoms::onendEvent) {
      attrName = nsGkAtoms::onend;
    } else if (aTypeAtom == nsGkAtoms::onwebkitAnimationEnd) {
      attrName = nsGkAtoms::onwebkitanimationend;
    } else if (aTypeAtom == nsGkAtoms::onwebkitAnimationIteration) {
      attrName = nsGkAtoms::onwebkitanimationiteration;
    } else if (aTypeAtom == nsGkAtoms::onwebkitAnimationStart) {
      attrName = nsGkAtoms::onwebkitanimationstart;
    } else if (aTypeAtom == nsGkAtoms::onwebkitTransitionEnd) {
      attrName = nsGkAtoms::onwebkittransitionend;
    }

    element->GetAttr(attrName, handlerBody);
    body = &handlerBody;
    aElement = element;
  }
  aListener = nullptr;

  nsAutoCString url("-moz-evil:lying-event-listener"_ns);
  MOZ_ASSERT(body);
  MOZ_ASSERT(aElement);
  nsIURI* uri = aElement->OwnerDoc()->GetDocumentURI();
  if (uri) {
    uri->GetSpec(url);
  }

  nsCOMPtr<nsPIDOMWindowInner> win =
      nsPIDOMWindowInner::FromEventTargetOrNull(mTarget);
  uint32_t argCount;
  const char** argNames;
  nsContentUtils::GetEventArgNames(aElement->GetNameSpaceID(), aTypeAtom, win,
                                   &argCount, &argNames);

  // Wrap the event target, so that we can use it as the scope for the event
  // handler. Note that mTarget is different from aElement in the <body> case,
  // where mTarget is a Window.
  //
  // The wrapScope doesn't really matter here, because the target will create
  // its reflector in the proper scope, and then we'll enter that realm.
  JS::Rooted<JSObject*> wrapScope(cx, global->GetGlobalJSObject());
  JS::Rooted<JS::Value> v(cx);
  {
    JSAutoRealm ar(cx, wrapScope);
    nsresult rv = nsContentUtils::WrapNative(cx, mTarget, &v,
                                             /* aAllowWrapping = */ false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  JS::Rooted<JSObject*> target(cx, &v.toObject());
  JSAutoRealm ar(cx, target);

  // Now that we've entered the realm we actually care about, create our
  // environment chain.  Note that we start with |element|, not aElement,
  // because mTarget is different from aElement in the <body> case, where
  // mTarget is a Window, and in that case we do not want the environment chain
  // to include the body or the document.
  JS::EnvironmentChain envChain(cx, JS::SupportUnscopables::Yes);
  if (!nsJSUtils::GetEnvironmentChainForElement(cx, element, envChain)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsDependentAtomString str(attrName);
  // Most of our names are short enough that we don't even have to malloc
  // the JS string stuff, so don't worry about playing games with
  // refcounting XPCOM stringbuffers.
  JS::Rooted<JSString*> jsStr(
      cx, JS_NewUCStringCopyN(cx, str.BeginReading(), str.Length()));
  NS_ENSURE_TRUE(jsStr, NS_ERROR_OUT_OF_MEMORY);

  // Get the reflector for |aElement|, so that we can pass to setElement.
  if (NS_WARN_IF(!GetOrCreateDOMReflector(cx, aElement, &v))) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<JS::loader::ScriptFetchOptions> fetchOptions =
      new JS::loader::ScriptFetchOptions(
          CORS_NONE, /* aNonce = */ u""_ns, RequestPriority::Auto,
          JS::loader::ParserMetadata::NotParserInserted,
          aElement->OwnerDoc()->NodePrincipal());

  RefPtr<JS::loader::EventScript> eventScript = new JS::loader::EventScript(
      aElement->OwnerDoc()->GetReferrerPolicy(), fetchOptions, uri);

  JS::CompileOptions options(cx);
  // Use line 0 to make the function body starts from line 1.
  options.setIntroductionType("eventHandler")
      .setFileAndLine(url.get(), 0)
      .setDeferDebugMetadata(true);

  JS::Rooted<JSObject*> handler(cx);
  result = nsJSUtils::CompileFunction(jsapi, envChain, options,
                                      nsAtomCString(aTypeAtom), argCount,
                                      argNames, *body, handler.address());
  NS_ENSURE_SUCCESS(result, result);
  NS_ENSURE_TRUE(handler, NS_ERROR_FAILURE);

  JS::Rooted<JS::Value> privateValue(cx, JS::PrivateValue(eventScript));
  result = nsJSUtils::UpdateFunctionDebugMetadata(jsapi, handler, options,
                                                  jsStr, privateValue);
  NS_ENSURE_SUCCESS(result, result);

  MOZ_ASSERT(js::IsObjectInContextCompartment(handler, cx));
  JS::Rooted<JSObject*> handlerGlobal(cx, JS::CurrentGlobalOrNull(cx));

  if (jsEventHandler->EventName() == nsGkAtoms::onerror && win) {
    RefPtr<OnErrorEventHandlerNonNull> handlerCallback =
        new OnErrorEventHandlerNonNull(static_cast<JSContext*>(nullptr),
                                       handler, handlerGlobal,
                                       /* aIncumbentGlobal = */ nullptr);
    jsEventHandler->SetHandler(handlerCallback);
  } else if (jsEventHandler->EventName() == nsGkAtoms::onbeforeunload && win) {
    RefPtr<OnBeforeUnloadEventHandlerNonNull> handlerCallback =
        new OnBeforeUnloadEventHandlerNonNull(static_cast<JSContext*>(nullptr),
                                              handler, handlerGlobal,
                                              /* aIncumbentGlobal = */ nullptr);
    jsEventHandler->SetHandler(handlerCallback);
  } else {
    RefPtr<EventHandlerNonNull> handlerCallback = new EventHandlerNonNull(
        static_cast<JSContext*>(nullptr), handler, handlerGlobal,
        /* aIncumbentGlobal = */ nullptr);
    jsEventHandler->SetHandler(handlerCallback);
  }

  return result;
}

bool EventListenerManager::HandleEventSingleListener(
    Listener* aListener, nsAtom* aTypeAtom, WidgetEvent* aEvent,
    Event* aDOMEvent, EventTarget* aCurrentTarget, bool aItemInShadowTree) {
  if (!aEvent->mCurrentTarget) {
    aEvent->mCurrentTarget = aCurrentTarget->GetTargetForDOMEvent();
    if (!aEvent->mCurrentTarget) {
      return false;
    }
  }

  aEvent->mFlags.mInPassiveListener = aListener->mFlags.mPassive;

  nsCOMPtr<nsPIDOMWindowInner> innerWindow =
      WindowFromListener(aListener, aTypeAtom, aItemInShadowTree);
  mozilla::dom::Event* oldWindowEvent = nullptr;
  if (innerWindow) {
    oldWindowEvent = innerWindow->SetEvent(aDOMEvent);
  }

  nsresult result = NS_OK;

  // strong ref
  EventListenerHolder listenerHolder(aListener->mListener.Clone());

  // If this is a script handler and we haven't yet
  // compiled the event handler itself
  if ((aListener->mListenerType == Listener::eJSEventListener) &&
      aListener->mHandlerIsString) {
    result =
        CompileEventHandlerInternal(aListener, aTypeAtom, nullptr, nullptr);
    aListener = nullptr;
  }

  if (NS_SUCCEEDED(result)) {
    Maybe<EventCallbackDebuggerNotificationGuard> dbgGuard;
    if (dom::ChromeUtils::IsDevToolsOpened() || profiler_is_active()) {
      dbgGuard.emplace(aCurrentTarget, aDOMEvent);
    }
    nsAutoMicroTask mt;

    // Event::currentTarget is set in EventDispatcher.
    if (listenerHolder.HasWebIDLCallback()) {
      ErrorResult rv;
      listenerHolder.GetWebIDLCallback()->HandleEvent(aCurrentTarget,
                                                      *aDOMEvent, rv);
      result = rv.StealNSResult();
    } else {
      // listenerHolder is holding a stack ref here.
      result = MOZ_KnownLive(listenerHolder.GetXPCOMCallback())
                   ->HandleEvent(aDOMEvent);
    }
  }

  if (innerWindow) {
    Unused << innerWindow->SetEvent(oldWindowEvent);
  }

  if (NS_FAILED(result)) {
    aEvent->mFlags.mExceptionWasRaised = true;
  }
  aEvent->mFlags.mInPassiveListener = false;
  return !aEvent->mFlags.mImmediatePropagationStopped;
}

/* static */ EventMessage EventListenerManager::GetLegacyEventMessage(
    EventMessage aEventMessage) {
  // webkit-prefixed legacy events:
  if (aEventMessage == eTransitionEnd) {
    return eWebkitTransitionEnd;
  }
  if (aEventMessage == eAnimationStart) {
    return eWebkitAnimationStart;
  }
  if (aEventMessage == eAnimationEnd) {
    return eWebkitAnimationEnd;
  }
  if (aEventMessage == eAnimationIteration) {
    return eWebkitAnimationIteration;
  }

  switch (aEventMessage) {
    case eFullscreenChange:
      return eMozFullscreenChange;
    case eFullscreenError:
      return eMozFullscreenError;
    default:
      return aEventMessage;
  }
}

EventMessage EventListenerManager::GetEventMessage(nsAtom* aEventName) const {
  if (mIsMainThreadELM) {
    return nsContentUtils::GetEventMessage(aEventName);
  }

  // The nsContentUtils event message hashtables aren't threadsafe, so just fall
  // back to eUnidentifiedEvent.
  return eUnidentifiedEvent;
}

EventMessage EventListenerManager::GetEventMessageAndAtomForListener(
    const nsAString& aType, nsAtom** aAtom) {
  if (mIsMainThreadELM) {
    return nsContentUtils::GetEventMessageAndAtomForListener(aType, aAtom);
  }

  *aAtom = NS_Atomize(u"on"_ns + aType).take();
  return eUnidentifiedEvent;
}

already_AddRefed<nsPIDOMWindowInner> EventListenerManager::WindowFromListener(
    Listener* aListener, nsAtom* aTypeAtom, bool aItemInShadowTree) {
  nsCOMPtr<nsPIDOMWindowInner> innerWindow;
  if (!aItemInShadowTree) {
    if (aListener->mListener.HasWebIDLCallback()) {
      CallbackObject* callback = aListener->mListener.GetWebIDLCallback();
      nsIGlobalObject* global = nullptr;
      if (callback) {
        global = callback->IncumbentGlobalOrNull();
      }
      if (global) {
        innerWindow = global->GetAsInnerWindow();  // Can be nullptr
      }
    } else if (mTarget) {
      // This ensures `window.event` can be set properly for
      // nsWindowRoot to handle KeyPress event.
      if (aListener && aTypeAtom == nsGkAtoms::onkeypress &&
          mTarget->IsRootWindow()) {
        nsPIWindowRoot* root = mTarget->AsWindowRoot();
        if (nsPIDOMWindowOuter* outerWindow = root->GetWindow()) {
          innerWindow = outerWindow->GetCurrentInnerWindow();
        }
      } else {
        // Can't get the global from
        // listener->mListener.GetXPCOMCallback().
        // In most cases, it would be the same as for
        // the target, so let's do that.
        if (nsIGlobalObject* global = mTarget->GetOwnerGlobal()) {
          innerWindow = global->GetAsInnerWindow();
        }
      }
    }
  }
  return innerWindow.forget();
}

Maybe<size_t> EventListenerManager::EventListenerMap::EntryIndexForType(
    nsAtom* aTypeAtom) const {
  MOZ_ASSERT(aTypeAtom);

  size_t matchIndexOrInsertionPoint = 0;
  bool foundMatch = BinarySearchIf(mEntries, 0, mEntries.Length(),
                                   ListenerMapEntryComparator(aTypeAtom),
                                   &matchIndexOrInsertionPoint);
  return foundMatch ? Some(matchIndexOrInsertionPoint) : Nothing();
}

Maybe<size_t> EventListenerManager::EventListenerMap::EntryIndexForAllEvents()
    const {
  // If we have an entry for "all events listeners", it'll be at the beginning
  // of the list and its type atom will be null.
  return !mEntries.IsEmpty() && mEntries[0].mTypeAtom == nullptr ? Some(0)
                                                                 : Nothing();
}

RefPtr<EventListenerManager::ListenerArray>
EventListenerManager::EventListenerMap::GetListenersForType(
    nsAtom* aTypeAtom) const {
  Maybe<size_t> index = EntryIndexForType(aTypeAtom);
  return index ? mEntries[*index].mListeners : nullptr;
}

RefPtr<EventListenerManager::ListenerArray>
EventListenerManager::EventListenerMap::GetListenersForAllEvents() const {
  Maybe<size_t> index = EntryIndexForAllEvents();
  return index ? mEntries[*index].mListeners : nullptr;
}

RefPtr<EventListenerManager::ListenerArray>
EventListenerManager::EventListenerMap::GetOrCreateListenersForType(
    nsAtom* aTypeAtom) {
  MOZ_ASSERT(aTypeAtom);
  size_t matchIndexOrInsertionPoint = 0;
  bool foundMatch = BinarySearchIf(mEntries, 0, mEntries.Length(),
                                   ListenerMapEntryComparator(aTypeAtom),
                                   &matchIndexOrInsertionPoint);
  if (foundMatch) {
    return mEntries[matchIndexOrInsertionPoint].mListeners;
  }
  RefPtr<ListenerArray> listeners = MakeRefPtr<ListenerArray>();
  mEntries.InsertElementAt(matchIndexOrInsertionPoint,
                           EventListenerMapEntry{aTypeAtom, listeners});

  return listeners;
}

RefPtr<EventListenerManager::ListenerArray>
EventListenerManager::EventListenerMap::GetOrCreateListenersForAllEvents() {
  RefPtr<ListenerArray> listeners = GetListenersForAllEvents();
  if (!listeners) {
    listeners = MakeRefPtr<ListenerArray>();
    mEntries.InsertElementAt(0, EventListenerMapEntry{nullptr, listeners});
  }
  return listeners;
}

void EventListenerManager::HandleEventInternal(nsPresContext* aPresContext,
                                               WidgetEvent* aEvent,
                                               Event** aDOMEvent,
                                               EventTarget* aCurrentTarget,
                                               nsEventStatus* aEventStatus,
                                               bool aItemInShadowTree) {
  MOZ_ASSERT_IF(aEvent->mMessage != eUnidentifiedEvent, mIsMainThreadELM);

  // Set the value of the internal PreventDefault flag properly based on
  // aEventStatus
  if (!aEvent->DefaultPrevented() &&
      *aEventStatus == nsEventStatus_eConsumeNoDefault) {
    // Assume that if only aEventStatus claims that the event has already been
    // consumed, the consumer is default event handler.
    aEvent->PreventDefault();
  }

  if (aEvent->mFlags.mImmediatePropagationStopped) {
    return;
  }

  Maybe<AutoHandlingUserInputStatePusher> userInputStatePusher;
  Maybe<AutoPopupStatePusher> popupStatePusher;
  if (mIsMainThreadELM) {
    userInputStatePusher.emplace(UserActivation::IsUserInteractionEvent(aEvent),
                                 aEvent);
    popupStatePusher.emplace(
        PopupBlocker::GetEventPopupControlState(aEvent, *aDOMEvent));
  }

  RefPtr<nsAtom> typeAtom = nsContentUtils::GetEventType(aEvent);
  if (!typeAtom) {
    // Some messages don't have a corresponding type atom, e.g.
    // eMouseEnterIntoWidget. These events can't have a listener, so we
    // can stop here.
    return;
  }

  EventMessage eventMessage = aEvent->mMessage;
  bool hasAnyListenerForEventType = false;

  // First, notify any "all events" listeners.
  if (RefPtr<ListenerArray> listenersForAllEvents =
          mListenerMap.GetListenersForAllEvents()) {
    HandleEventWithListenerArray(listenersForAllEvents, typeAtom, eventMessage,
                                 aPresContext, aEvent, aDOMEvent,
                                 aCurrentTarget, aItemInShadowTree);
    hasAnyListenerForEventType = true;
  }

  // Now look for listeners for typeAtom, and call them if we have any.
  bool hasAnyListenerMatchingGroup = false;
  if (RefPtr<ListenerArray> listeners =
          mListenerMap.GetListenersForType(typeAtom)) {
    hasAnyListenerMatchingGroup = HandleEventWithListenerArray(
        listeners, typeAtom, eventMessage, aPresContext, aEvent, aDOMEvent,
        aCurrentTarget, aItemInShadowTree);
    hasAnyListenerForEventType = true;
  }

  if (!hasAnyListenerMatchingGroup && aEvent->IsTrusted()) {
    // If we didn't find any matching listeners, and our event has a legacy
    // version, check the listeners for the legacy version.
    EventMessage legacyEventMessage = GetLegacyEventMessage(eventMessage);
    if (legacyEventMessage != eventMessage) {
      MOZ_ASSERT(
          GetLegacyEventMessage(legacyEventMessage) == legacyEventMessage,
          "Legacy event messages should not themselves have legacy versions");
      RefPtr<nsAtom> legacyTypeAtom =
          nsContentUtils::GetEventTypeFromMessage(legacyEventMessage);
      if (RefPtr<ListenerArray> legacyListeners =
              mListenerMap.GetListenersForType(legacyTypeAtom)) {
        HandleEventWithListenerArray(
            legacyListeners, legacyTypeAtom, legacyEventMessage, aPresContext,
            aEvent, aDOMEvent, aCurrentTarget, aItemInShadowTree);
        hasAnyListenerForEventType = true;
      }
    }
  }

  aEvent->mCurrentTarget = nullptr;

  if (mIsMainThreadELM && !hasAnyListenerForEventType) {
    if (aEvent->mMessage != eUnidentifiedEvent) {
      mNoListenerForEvents[2] = mNoListenerForEvents[1];
      mNoListenerForEvents[1] = mNoListenerForEvents[0];
      mNoListenerForEvents[0] = aEvent->mMessage;
    } else {
      mNoListenerForEventAtom = aEvent->mSpecifiedEventType;
    }
  }

  if (aEvent->DefaultPrevented()) {
    *aEventStatus = nsEventStatus_eConsumeNoDefault;
  }
}

bool EventListenerManager::HandleEventWithListenerArray(
    ListenerArray* aListeners, nsAtom* aTypeAtom, EventMessage aEventMessage,
    nsPresContext* aPresContext, WidgetEvent* aEvent, Event** aDOMEvent,
    EventTarget* aCurrentTarget, bool aItemInShadowTree) {
  auto ensureDOMEvent = [&]() {
    if (!*aDOMEvent) {
      // Lazily create the DOM event.
      // This is tiny bit slow, but happens only once per event.
      // Similar code also in EventDispatcher.
      nsCOMPtr<EventTarget> et = aEvent->mOriginalTarget;
      RefPtr<Event> event =
          EventDispatcher::CreateEvent(et, aPresContext, aEvent, u""_ns);
      event.forget(aDOMEvent);
    }
    return *aDOMEvent != nullptr;
  };

  Maybe<EventMessageAutoOverride> eventMessageAutoOverride;
  bool isOverridingEventMessage = aEvent->mMessage != aEventMessage;
  bool hasAnyListenerMatchingGroup = false;
  bool didReplaceOnceListener = false;

  for (Listener& listenerRef : aListeners->EndLimitedRange()) {
    Listener* listener = &listenerRef;
    if (!ListenerCanHandle(listener, aEvent)) {
      continue;
    }
    hasAnyListenerMatchingGroup = true;

    // Check that the phase is same in event and event listener. Also check
    // that the event is trusted or that the listener allows untrusted events.
    if (!listener->MatchesEventPhase(aEvent) ||
        !listener->AllowsEventTrustedness(aEvent)) {
      continue;
    }

    Maybe<Listener> listenerHolder;
    if (listener->mFlags.mOnce) {
      // Move the listener to the stack before handling the event.
      // The order is important, otherwise the listener could be
      // called again inside the listener.
      listenerHolder.emplace(std::move(*listener));
      listener = listenerHolder.ptr();
      didReplaceOnceListener = true;
    }
    if (ensureDOMEvent()) {
      if (isOverridingEventMessage && !eventMessageAutoOverride) {
        // Override the domEvent's event-message (its .type) until we
        // finish traversing listeners (when eventMessageAutoOverride
        // destructs).
        eventMessageAutoOverride.emplace(*aDOMEvent, aEventMessage);
      }
      if (!HandleEventSingleListener(listener, aTypeAtom, aEvent, *aDOMEvent,
                                     aCurrentTarget, aItemInShadowTree)) {
        break;
      }
    }
  }

  if (didReplaceOnceListener) {
    // If there are any once listeners replaced with a placeholder during the
    // loop above, we need to clean up them here. Note that this could clear
    // once listeners handled in some outer level as well, but that should not
    // affect the result.
    size_t oldLength = aListeners->Length();
    aListeners->NonObservingRemoveElementsBy([](const Listener& aListener) {
      return aListener.mListenerType == Listener::eNoListener;
    });
    size_t newLength = aListeners->Length();
    if (newLength == 0) {
      // Remove the entry that has now become empty.
      mListenerMap.mEntries.RemoveElementsBy([](EventListenerMapEntry& entry) {
        return entry.mListeners->IsEmpty();
      });
    }
    if (newLength < oldLength) {
      // Call NotifyEventListenerRemoved once for every removed listener.
      size_t removedCount = oldLength - newLength;
      for (size_t i = 0; i < removedCount; i++) {
        NotifyEventListenerRemoved(aTypeAtom);
      }
      if (IsDeviceType(aTypeAtom)) {
        // Call DisableDevice once for every removed listener.
        for (size_t i = 0; i < removedCount; i++) {
          DisableDevice(aTypeAtom);
        }
      }
    }
  }

  return hasAnyListenerMatchingGroup;
}

void EventListenerManager::Disconnect() {
  mTarget = nullptr;
  RemoveAllListenersSilently();
}

void EventListenerManager::AddEventListener(const nsAString& aType,
                                            EventListenerHolder aListenerHolder,
                                            bool aUseCapture,
                                            bool aWantsUntrusted) {
  EventListenerFlags flags;
  flags.mCapture = aUseCapture;
  flags.mAllowUntrustedEvents = aWantsUntrusted;
  return AddEventListenerByType(std::move(aListenerHolder), aType, flags);
}

void EventListenerManager::AddEventListener(
    const nsAString& aType, EventListenerHolder aListenerHolder,
    const dom::AddEventListenerOptionsOrBoolean& aOptions,
    bool aWantsUntrusted) {
  EventListenerFlags flags;
  Optional<bool> passive;
  AbortSignal* signal = nullptr;
  if (aOptions.IsBoolean()) {
    flags.mCapture = aOptions.GetAsBoolean();
  } else {
    const auto& options = aOptions.GetAsAddEventListenerOptions();
    flags.mCapture = options.mCapture;
    flags.mInSystemGroup = options.mMozSystemGroup;
    flags.mOnce = options.mOnce;
    if (options.mPassive.WasPassed()) {
      passive.Construct(options.mPassive.Value());
    }

    if (options.mSignal.WasPassed()) {
      signal = &options.mSignal.Value();
    }
  }

  flags.mAllowUntrustedEvents = aWantsUntrusted;
  return AddEventListenerByType(std::move(aListenerHolder), aType, flags,
                                passive, signal);
}

void EventListenerManager::RemoveEventListener(
    const nsAString& aType, EventListenerHolder aListenerHolder,
    bool aUseCapture) {
  EventListenerFlags flags;
  flags.mCapture = aUseCapture;
  RemoveEventListenerByType(std::move(aListenerHolder), aType, flags);
}

void EventListenerManager::RemoveEventListener(
    const nsAString& aType, EventListenerHolder aListenerHolder,
    const dom::EventListenerOptionsOrBoolean& aOptions) {
  EventListenerFlags flags;
  if (aOptions.IsBoolean()) {
    flags.mCapture = aOptions.GetAsBoolean();
  } else {
    const auto& options = aOptions.GetAsEventListenerOptions();
    flags.mCapture = options.mCapture;
    flags.mInSystemGroup = options.mMozSystemGroup;
  }
  RemoveEventListenerByType(std::move(aListenerHolder), aType, flags);
}

void EventListenerManager::AddListenerForAllEvents(EventListener* aDOMListener,
                                                   bool aUseCapture,
                                                   bool aWantsUntrusted,
                                                   bool aSystemEventGroup) {
  EventListenerFlags flags;
  flags.mCapture = aUseCapture;
  flags.mAllowUntrustedEvents = aWantsUntrusted;
  flags.mInSystemGroup = aSystemEventGroup;
  AddEventListenerInternal(EventListenerHolder(aDOMListener), eAllEvents,
                           nullptr, flags, false, true);
}

void EventListenerManager::RemoveListenerForAllEvents(
    EventListener* aDOMListener, bool aUseCapture, bool aSystemEventGroup) {
  EventListenerFlags flags;
  flags.mCapture = aUseCapture;
  flags.mInSystemGroup = aSystemEventGroup;
  RemoveEventListenerInternal(EventListenerHolder(aDOMListener), nullptr, flags,
                              true);
}

bool EventListenerManager::HasMutationListeners() {
  if (mMayHaveMutationListeners) {
    for (const auto& entry : mListenerMap.mEntries) {
      EventMessage message = GetEventMessage(entry.mTypeAtom);
      if (message >= eLegacyMutationEventFirst &&
          message <= eLegacyMutationEventLast) {
        return true;
      }
    }
  }

  return false;
}

uint32_t EventListenerManager::MutationListenerBits() {
  uint32_t bits = 0;
  if (mMayHaveMutationListeners) {
    for (const auto& entry : mListenerMap.mEntries) {
      EventMessage message = GetEventMessage(entry.mTypeAtom);
      if (message >= eLegacyMutationEventFirst &&
          message <= eLegacyMutationEventLast) {
        if (message == eLegacySubtreeModified) {
          return NS_EVENT_BITS_MUTATION_ALL;
        }
        bits |= MutationBitForEventType(message);
      }
    }
  }
  return bits;
}

bool EventListenerManager::HasListenersFor(const nsAString& aEventName) const {
  RefPtr<nsAtom> atom = NS_Atomize(u"on"_ns + aEventName);
  return HasListenersFor(atom);
}

bool EventListenerManager::HasListenersFor(nsAtom* aEventNameWithOn) const {
  return HasListenersForInternal(aEventNameWithOn, false);
}

bool EventListenerManager::HasNonPassiveListenersFor(
    const WidgetEvent* aEvent) const {
  if (RefPtr<nsAtom> typeAtom = nsContentUtils::GetEventType(aEvent)) {
    if (const auto& listeners = mListenerMap.GetListenersForType(typeAtom)) {
      for (const Listener& listener : listeners->NonObservingRange()) {
        if (!listener.mFlags.mPassive && ListenerCanHandle(&listener, aEvent)) {
          return true;
        }
      }
    }

    // After dispatching wheel, legacy mouse scroll events are dispatched
    // and listeners on those can also default prevent the behavior.
    if (aEvent->mMessage == eWheel) {
      if (const auto& listeners = mListenerMap.GetListenersForType(nsGkAtoms::onDOMMouseScroll)) {
        for (const Listener& listener : listeners->NonObservingRange()) {
          if (!listener.mFlags.mPassive && ListenerCanHandle(&listener, aEvent)) {
            return true;
          }
        }
      }
      if (const auto& listeners = mListenerMap.GetListenersForType(nsGkAtoms::onMozMousePixelScroll)) {
        for (const Listener& listener : listeners->NonObservingRange()) {
          if (!listener.mFlags.mPassive && ListenerCanHandle(&listener, aEvent)) {
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool EventListenerManager::ListenerCanHandle(const Listener* aListener,
                                             const WidgetEvent* aEvent) const {
  if (aListener->mListenerType == Listener::eNoListener) {
    // The listener is a placeholder value of a removed "once" listener.
    return false;
  }
  if (!aListener->mEnabled) {
    // The listener has been disabled, for example by devtools.
    return false;
  }
  if (!aListener->MatchesEventGroup(aEvent)) {
    return false;
  }

  return true;
}

bool EventListenerManager::HasNonSystemGroupListenersFor(
    nsAtom* aEventNameWithOn) const {
  return HasListenersForInternal(aEventNameWithOn, true);
}

bool EventListenerManager::HasListenersForInternal(
    nsAtom* aEventNameWithOn, bool aIgnoreSystemGroup) const {
#ifdef DEBUG
  nsAutoString name;
  aEventNameWithOn->ToString(name);
#endif
  NS_ASSERTION(StringBeginsWith(name, u"on"_ns),
               "Event name does not start with 'on'");
  RefPtr<ListenerArray> listeners =
      mListenerMap.GetListenersForType(aEventNameWithOn);
  if (!listeners) {
    return false;
  }

  MOZ_ASSERT(!listeners->IsEmpty());

  if (!aIgnoreSystemGroup) {
    return true;
  }

  // Check if any non-system-group listeners exist in `listeners`.
  for (const auto& listener : listeners->NonObservingRange()) {
    if (!listener.mFlags.mInSystemGroup) {
      return true;
    }
  }

  return false;
}

bool EventListenerManager::HasListeners() const {
  return !mListenerMap.IsEmpty();
}

nsresult EventListenerManager::GetListenerInfo(
    nsTArray<RefPtr<nsIEventListenerInfo>>& aList) {
  nsCOMPtr<EventTarget> target = mTarget;
  NS_ENSURE_STATE(target);
  aList.Clear();
  for (const auto& entry : mListenerMap.mEntries) {
    for (const Listener& listener : entry.mListeners->ForwardRange()) {
      // If this is a script handler and we haven't yet
      // compiled the event handler itself go ahead and compile it
      if (listener.mListenerType == Listener::eJSEventListener &&
          listener.mHandlerIsString) {
        CompileEventHandlerInternal(const_cast<Listener*>(&listener),
                                    entry.mTypeAtom, nullptr, nullptr);
      }
      nsAutoString eventType;
      if (listener.mAllEvents) {
        eventType.SetIsVoid(true);
      } else if (listener.mListenerType == Listener::eNoListener) {
        continue;
      } else {
        eventType.Assign(Substring(nsDependentAtomString(entry.mTypeAtom), 2));
      }

      JS::Rooted<JSObject*> callback(RootingCx());
      JS::Rooted<JSObject*> callbackGlobal(RootingCx());
      if (JSEventHandler* handler = listener.GetJSEventHandler()) {
        if (handler->GetTypedEventHandler().HasEventHandler()) {
          CallbackFunction* callbackFun = handler->GetTypedEventHandler().Ptr();
          callback = callbackFun->CallableOrNull();
          callbackGlobal = callbackFun->CallbackGlobalOrNull();
          if (!callback) {
            // This will be null for cross-compartment event listeners
            // which have been destroyed.
            continue;
          }
        }
      } else if (listener.mListenerType == Listener::eWebIDLListener) {
        EventListener* listenerCallback =
            listener.mListener.GetWebIDLCallback();
        callback = listenerCallback->CallbackOrNull();
        callbackGlobal = listenerCallback->CallbackGlobalOrNull();
        if (!callback) {
          // This will be null for cross-compartment event listeners
          // which have been destroyed.
          continue;
        }
      }

      RefPtr<EventListenerInfo> info = new EventListenerInfo(
          this, eventType, callback, callbackGlobal, listener.mFlags.mCapture,
          listener.mFlags.mAllowUntrustedEvents, listener.mFlags.mInSystemGroup,
          listener.mListenerIsHandler);
      aList.AppendElement(info.forget());
    }
  }
  return NS_OK;
}

EventListenerManager::Listener* EventListenerManager::GetListenerFor(
    nsAString& aType, JSObject* aListener, bool aCapturing,
    bool aAllowsUntrusted, bool aInSystemEventGroup, bool aIsHandler) {
  NS_ENSURE_TRUE(aListener, nullptr);

  RefPtr<ListenerArray> listeners = ([&]() -> RefPtr<ListenerArray> {
    if (aType.IsVoid()) {
      return mListenerMap.GetListenersForAllEvents();
    }

    for (auto& mapEntry : mListenerMap.mEntries) {
      if (RefPtr<nsAtom> typeAtom = mapEntry.mTypeAtom) {
        if (Substring(nsDependentAtomString(typeAtom), 2).Equals(aType)) {
          return mapEntry.mListeners;
        }
      }
    }

    return nullptr;
  })();

  if (!listeners) {
    return nullptr;
  }

  for (Listener& listener : listeners->ForwardRange()) {
    if (listener.mListenerType == Listener::eNoListener) {
      continue;
    }

    if (listener.mFlags.mCapture != aCapturing ||
        listener.mFlags.mAllowUntrustedEvents != aAllowsUntrusted ||
        listener.mFlags.mInSystemGroup != aInSystemEventGroup) {
      continue;
    }

    if (aIsHandler) {
      if (JSEventHandler* handler = listener.GetJSEventHandler()) {
        if (handler->GetTypedEventHandler().HasEventHandler()) {
          if (handler->GetTypedEventHandler().Ptr()->CallableOrNull() ==
              aListener) {
            return &listener;
          }
        }
      }
    } else if (listener.mListenerType == Listener::eWebIDLListener &&
               listener.mListener.GetWebIDLCallback()->CallbackOrNull() ==
                   aListener) {
      return &listener;
    }
  }
  return nullptr;
}

nsresult EventListenerManager::IsListenerEnabled(
    nsAString& aType, JSObject* aListener, bool aCapturing,
    bool aAllowsUntrusted, bool aInSystemEventGroup, bool aIsHandler,
    bool* aEnabled) {
  Listener* listener =
      GetListenerFor(aType, aListener, aCapturing, aAllowsUntrusted,
                     aInSystemEventGroup, aIsHandler);
  NS_ENSURE_TRUE(listener, NS_ERROR_NOT_AVAILABLE);
  *aEnabled = listener->mEnabled;
  return NS_OK;
}

nsresult EventListenerManager::SetListenerEnabled(
    nsAString& aType, JSObject* aListener, bool aCapturing,
    bool aAllowsUntrusted, bool aInSystemEventGroup, bool aIsHandler,
    bool aEnabled) {
  Listener* listener =
      GetListenerFor(aType, aListener, aCapturing, aAllowsUntrusted,
                     aInSystemEventGroup, aIsHandler);
  NS_ENSURE_TRUE(listener, NS_ERROR_NOT_AVAILABLE);
  listener->mEnabled = aEnabled;
  if (aEnabled) {
    // We may have enabled some listener, clear the cache for which events
    // we don't have listeners.
    ClearNoListenersForEvents();
    mNoListenerForEventAtom = nullptr;
  }
  return NS_OK;
}

bool EventListenerManager::HasUnloadListeners() {
  return mListenerMap.GetListenersForType(nsGkAtoms::onunload) != nullptr;
}

bool EventListenerManager::HasBeforeUnloadListeners() {
  return mListenerMap.GetListenersForType(nsGkAtoms::onbeforeunload) != nullptr;
}

void EventListenerManager::SetEventHandler(nsAtom* aEventName,
                                           EventHandlerNonNull* aHandler) {
  if (!aHandler) {
    RemoveEventHandler(aEventName);
    return;
  }

  // Untrusted events are always permitted for non-chrome script
  // handlers.
  SetEventHandlerInternal(
      aEventName, TypedEventHandler(aHandler),
      !mIsMainThreadELM || !nsContentUtils::IsCallerChrome());
}

void EventListenerManager::SetEventHandler(
    OnErrorEventHandlerNonNull* aHandler) {
  if (!aHandler) {
    RemoveEventHandler(nsGkAtoms::onerror);
    return;
  }

  // Untrusted events are always permitted on workers and for non-chrome script
  // on the main thread.
  bool allowUntrusted = !mIsMainThreadELM || !nsContentUtils::IsCallerChrome();

  SetEventHandlerInternal(nsGkAtoms::onerror, TypedEventHandler(aHandler),
                          allowUntrusted);
}

void EventListenerManager::SetEventHandler(
    OnBeforeUnloadEventHandlerNonNull* aHandler) {
  if (!aHandler) {
    RemoveEventHandler(nsGkAtoms::onbeforeunload);
    return;
  }

  // Untrusted events are always permitted for non-chrome script
  // handlers.
  SetEventHandlerInternal(
      nsGkAtoms::onbeforeunload, TypedEventHandler(aHandler),
      !mIsMainThreadELM || !nsContentUtils::IsCallerChrome());
}

const TypedEventHandler* EventListenerManager::GetTypedEventHandler(
    nsAtom* aEventName) {
  Listener* listener = FindEventHandler(aEventName);

  if (!listener) {
    return nullptr;
  }

  JSEventHandler* jsEventHandler = listener->GetJSEventHandler();

  if (listener->mHandlerIsString) {
    CompileEventHandlerInternal(listener, aEventName, nullptr, nullptr);
  }

  const TypedEventHandler& typedHandler =
      jsEventHandler->GetTypedEventHandler();
  return typedHandler.HasEventHandler() ? &typedHandler : nullptr;
}

size_t EventListenerManager::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + mListenerMap.SizeOfExcludingThis(aMallocSizeOf);
}

size_t EventListenerManager::EventListenerMap::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = mEntries.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mEntries) {
    n += entry.SizeOfExcludingThis(aMallocSizeOf);
  }
  return n;
}

size_t EventListenerManager::EventListenerMapEntry::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return mListeners->SizeOfIncludingThis(aMallocSizeOf);
}

size_t EventListenerManager::ListenerArray::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& listener : NonObservingRange()) {
    JSEventHandler* jsEventHandler = listener.GetJSEventHandler();
    if (jsEventHandler) {
      n += jsEventHandler->SizeOfIncludingThis(aMallocSizeOf);
    }
  }
  return n;
}

uint32_t EventListenerManager::ListenerCount() const {
  uint32_t count = 0;
  for (const auto& entry : mListenerMap.mEntries) {
    count += entry.mListeners->Length();
  }
  return count;
}

void EventListenerManager::MarkForCC() {
  for (const auto& entry : mListenerMap.mEntries) {
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      JSEventHandler* jsEventHandler = listener.GetJSEventHandler();
      if (jsEventHandler) {
        const TypedEventHandler& typedHandler =
            jsEventHandler->GetTypedEventHandler();
        if (typedHandler.HasEventHandler()) {
          typedHandler.Ptr()->MarkForCC();
        }
      } else if (listener.mListenerType == Listener::eWebIDLListener) {
        listener.mListener.GetWebIDLCallback()->MarkForCC();
      }
    }
  }
  if (mRefCnt.IsPurple()) {
    mRefCnt.RemovePurple();
  }
}

void EventListenerManager::TraceListeners(JSTracer* aTrc) {
  for (const auto& entry : mListenerMap.mEntries) {
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      JSEventHandler* jsEventHandler = listener.GetJSEventHandler();
      if (jsEventHandler) {
        const TypedEventHandler& typedHandler =
            jsEventHandler->GetTypedEventHandler();
        if (typedHandler.HasEventHandler()) {
          mozilla::TraceScriptHolder(typedHandler.Ptr(), aTrc);
        }
      } else if (listener.mListenerType == Listener::eWebIDLListener) {
        mozilla::TraceScriptHolder(listener.mListener.GetWebIDLCallback(),
                                   aTrc);
      }
      // We might have eWrappedJSListener, but that is the legacy type for
      // JS implemented event listeners, and trickier to handle here.
    }
  }
}

bool EventListenerManager::HasNonSystemGroupListenersForUntrustedKeyEvents() {
  for (const auto& entry : mListenerMap.mEntries) {
    if (entry.mTypeAtom != nsGkAtoms::onkeydown &&
        entry.mTypeAtom != nsGkAtoms::onkeypress &&
        entry.mTypeAtom != nsGkAtoms::onkeyup) {
      continue;
    }
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      if (!listener.mFlags.mInSystemGroup &&
          listener.mFlags.mAllowUntrustedEvents) {
        return true;
      }
    }
  }
  return false;
}

bool EventListenerManager::
    HasNonPassiveNonSystemGroupListenersForUntrustedKeyEvents() {
  for (const auto& entry : mListenerMap.mEntries) {
    if (entry.mTypeAtom != nsGkAtoms::onkeydown &&
        entry.mTypeAtom != nsGkAtoms::onkeypress &&
        entry.mTypeAtom != nsGkAtoms::onkeyup) {
      continue;
    }
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      if (!listener.mFlags.mPassive && !listener.mFlags.mInSystemGroup &&
          listener.mFlags.mAllowUntrustedEvents) {
        return true;
      }
    }
  }
  return false;
}

bool EventListenerManager::HasApzAwareListeners() {
  if (!mIsMainThreadELM) {
    return false;
  }

  for (const auto& entry : mListenerMap.mEntries) {
    if (!IsApzAwareEvent(entry.mTypeAtom)) {
      continue;
    }
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      if (!listener.mFlags.mPassive) {
        return true;
      }
    }
  }
  return false;
}

static bool IsWheelEventType(nsAtom* aEvent) {
  if (aEvent == nsGkAtoms::onwheel || aEvent == nsGkAtoms::onDOMMouseScroll ||
      aEvent == nsGkAtoms::onmousewheel ||
      aEvent == nsGkAtoms::onMozMousePixelScroll) {
    return true;
  }
  return false;
}

bool EventListenerManager::IsApzAwareEvent(nsAtom* aEvent) {
  if (IsWheelEventType(aEvent)) {
    return true;
  }
  // In theory we should schedule a repaint if the touch event pref changes,
  // because the event regions might be out of date. In practice that seems like
  // overkill because users generally shouldn't be flipping this pref, much
  // less expecting touch listeners on the page to immediately start preventing
  // scrolling without so much as a repaint. Tests that we write can work
  // around this constraint easily enough.
  if (aEvent == nsGkAtoms::ontouchstart || aEvent == nsGkAtoms::ontouchmove) {
    return TouchEvent::PrefEnabled(
        nsContentUtils::GetDocShellForEventTarget(mTarget));
  }
  return false;
}

bool EventListenerManager::HasNonPassiveWheelListener() {
  MOZ_ASSERT(NS_IsMainThread());
  for (const auto& entry : mListenerMap.mEntries) {
    if (!IsWheelEventType(entry.mTypeAtom)) {
      continue;
    }
    for (const auto& listener : entry.mListeners->NonObservingRange()) {
      if (!listener.mFlags.mPassive) {
        return true;
      }
    }
  }
  return false;
}

void EventListenerManager::RemoveAllListeners() {
  while (!mListenerMap.IsEmpty()) {
    size_t entryIndex = mListenerMap.mEntries.Length() - 1;
    EventListenerMapEntry& entry = mListenerMap.mEntries[entryIndex];
    RefPtr<nsAtom> type = entry.mTypeAtom;
    MOZ_ASSERT(!entry.mListeners->IsEmpty());
    size_t idx = entry.mListeners->Length() - 1;
    entry.mListeners->RemoveElementAt(idx);
    if (entry.mListeners->IsEmpty()) {
      mListenerMap.mEntries.RemoveElementAt(entryIndex);
    }
    NotifyEventListenerRemoved(type);
    if (IsDeviceType(type)) {
      DisableDevice(type);
    }
  }
}

already_AddRefed<nsIScriptGlobalObject>
EventListenerManager::GetScriptGlobalAndDocument(Document** aDoc) {
  nsCOMPtr<Document> doc;
  nsCOMPtr<nsPIDOMWindowInner> win;
  if (nsINode* node = nsINode::FromEventTargetOrNull(mTarget)) {
    // Try to get context from doc
    doc = node->OwnerDoc();
    if (doc->IsLoadedAsData()) {
      return nullptr;
    }

    win = do_QueryInterface(doc->GetScopeObject());
  } else if ((win = GetTargetAsInnerWindow())) {
    doc = win->GetExtantDoc();
  }

  if (!win || !win->IsCurrentInnerWindow()) {
    return nullptr;
  }

  doc.forget(aDoc);
  nsCOMPtr<nsIScriptGlobalObject> global = do_QueryInterface(win);
  return global.forget();
}

EventListenerManager::ListenerSignalFollower::ListenerSignalFollower(
    EventListenerManager* aListenerManager,
    EventListenerManager::Listener* aListener, nsAtom* aTypeAtom)
    : dom::AbortFollower(),
      mListenerManager(aListenerManager),
      mListener(aListener->mListener.Clone()),
      mTypeAtom(aTypeAtom),
      mAllEvents(aListener->mAllEvents),
      mFlags(aListener->mFlags) {};

NS_IMPL_CYCLE_COLLECTION_CLASS(EventListenerManager::ListenerSignalFollower)

NS_IMPL_CYCLE_COLLECTING_ADDREF(EventListenerManager::ListenerSignalFollower)
NS_IMPL_CYCLE_COLLECTING_RELEASE(EventListenerManager::ListenerSignalFollower)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(
    EventListenerManager::ListenerSignalFollower)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mListener)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(
    EventListenerManager::ListenerSignalFollower)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mListener)
  tmp->mListenerManager = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(
    EventListenerManager::ListenerSignalFollower)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

void EventListenerManager::ListenerSignalFollower::RunAbortAlgorithm() {
  if (mListenerManager) {
    RefPtr<EventListenerManager> elm = mListenerManager;
    mListenerManager = nullptr;
    elm->RemoveEventListenerInternal(std::move(mListener), mTypeAtom, mFlags,
                                     mAllEvents);
  }
}

}  // namespace mozilla
