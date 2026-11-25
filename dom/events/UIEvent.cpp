/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/UIEvent.h"

#include "base/basictypes.h"
#include "ipc/IPCMessageUtils.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/Assertions.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/TextEvents.h"
#include "nsCOMPtr.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "nsIFrame.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsLayoutUtils.h"
#include "prtime.h"

namespace mozilla::dom {

UIEvent::UIEvent(EventTarget* aOwner, nsPresContext* aPresContext,
                 WidgetGUIEvent* aEvent)
    : Event(aOwner, aPresContext,
            aEvent ? aEvent : new InternalUIEvent(false, eVoidEvent, nullptr)),
      mLayerPoint(0, 0) {
  if (aEvent) {
    mEventIsInternal = false;
  } else {
    mEventIsInternal = true;
  }

  // Fill mDetail and mView according to the mEvent (widget-generated
  // event) we've got
  switch (mEvent->mClass) {
    case eUIEventClass: {
      mDetail = mEvent->AsUIEvent()->mDetail;
      break;
    }

    case eScrollPortEventClass: {
      InternalScrollPortEvent* scrollEvent = mEvent->AsScrollPortEvent();
      mDetail = static_cast<int32_t>(scrollEvent->mOrient);
      break;
    }

    default:
      mDetail = 0;
      break;
  }

  mView = nullptr;
  if (mPresContext) {
    nsIDocShell* docShell = mPresContext->GetDocShell();
    if (docShell) {
      mView = docShell->GetWindow();
    }
  }
}

// static
already_AddRefed<UIEvent> UIEvent::Constructor(const GlobalObject& aGlobal,
                                               const nsAString& aType,
                                               const UIEventInit& aParam) {
  nsCOMPtr<EventTarget> t = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<UIEvent> e = new UIEvent(t, nullptr, nullptr);
  bool trusted = e->Init(t);
  e->InitUIEvent(aType, aParam.mBubbles, aParam.mCancelable, aParam.mView,
                 aParam.mDetail);
  e->SetTrusted(trusted);
  e->SetComposed(aParam.mComposed);
  return e.forget();
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(UIEvent, Event, mView)

NS_IMPL_ADDREF_INHERITED(UIEvent, Event)
NS_IMPL_RELEASE_INHERITED(UIEvent, Event)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(UIEvent)
NS_INTERFACE_MAP_END_INHERITING(Event)

void UIEvent::InitUIEvent(const nsAString& typeArg, bool canBubbleArg,
                          bool cancelableArg, nsGlobalWindowInner* viewArg,
                          int32_t detailArg) {
  if (NS_WARN_IF(mEvent->mFlags.mIsBeingDispatched)) {
    return;
  }

  Event::InitEvent(typeArg, canBubbleArg, cancelableArg);

  mDetail = detailArg;
  mView = viewArg ? viewArg->GetOuterWindow() : nullptr;
}

already_AddRefed<nsIContent> UIEvent::GetRangeParentContentAndOffset(
    int32_t* aOffset) const {
  if (NS_WARN_IF(!mPresContext)) {
    return nullptr;
  }
  RefPtr<PresShell> presShell = mPresContext->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return nullptr;
  }
  nsCOMPtr<nsIContent> container;
  nsLayoutUtils::GetContainerAndOffsetAtEvent(
      presShell, mEvent, getter_AddRefs(container), aOffset);
  return container.forget();
}

int32_t UIEvent::RangeOffset() const {
  if (NS_WARN_IF(!mPresContext)) {
    return 0;
  }
  RefPtr<PresShell> presShell = mPresContext->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return 0;
  }
  int32_t offset = 0;
  nsLayoutUtils::GetContainerAndOffsetAtEvent(presShell, mEvent, nullptr,
                                              &offset);
  return offset;
}

nsIntPoint UIEvent::GetLayerPoint() const {
  if (mEvent->mFlags.mIsPositionless) {
    return nsIntPoint(0, 0);
  }
  if (!mEvent ||
      (mEvent->mClass != eMouseEventClass &&
       mEvent->mClass != eMouseScrollEventClass &&
       mEvent->mClass != eWheelEventClass &&
       mEvent->mClass != ePointerEventClass &&
       mEvent->mClass != eTouchEventClass &&
       mEvent->mClass != eDragEventClass &&
       mEvent->mClass != eSimpleGestureEventClass) ||
      !mPresContext || mEventIsInternal) {
    return mLayerPoint;
  }
  nsIFrame* targetFrame = mPresContext->EventStateManager()->GetEventTarget();
  if (!targetFrame) {
    return mLayerPoint;
  }
  // NOTE(emilio): This matches Blink to my knowledge, but it's generally not
  // super-well specified, see https://github.com/w3c/uievents/issues/398
  RelativeTo root{targetFrame->PresShell()->GetRootFrame()};
  const nsPoint rootPoint =
      nsLayoutUtils::GetEventCoordinatesRelativeTo(mEvent, root);
  nsIFrame* layer = nsLayoutUtils::GetClosestLayer(targetFrame);
  nsPoint layerRootPoint{0, 0};
  if (nsLayoutUtils::TransformPoint(RelativeTo{layer}, RelativeTo{root},
                                    layerRootPoint) !=
      nsLayoutUtils::TRANSFORM_SUCCEEDED) {
    return mLayerPoint;
  }
  return RoundedToInt(CSSPoint::FromAppUnits(rootPoint - layerRootPoint))
      .ToUnknownPoint();
}

void UIEvent::DuplicatePrivateData() {
  mLayerPoint = GetLayerPoint();

  // GetScreenCoords() converts mEvent->mRefPoint to right coordinates.
  // Note that mPresContext will be cleared by Event::DuplicatePrivateData().
  // Therefore, we need to use mPresContext before calling it.
  const CSSIntPoint screenPoint = RoundedToInt(
      Event::GetScreenCoords(mPresContext, mEvent, mEvent->mRefPoint)
          .valueOr(CSSIntPoint{0, 0}));

  Event::DuplicatePrivateData();
  MOZ_ASSERT_IF(!mEventIsInternal, !mPresContext);

  // GetScreenCoords() has already computed the screen point in CSS pixels which
  // applied the scale of mPresContext.  Additionally, we don't have the widget
  // anymore.  Therefore, we need to cache the point as in the screen
  // coordinates.
  MOZ_ASSERT(!mEvent || !mEvent->AsGUIEvent() ||
             !mEvent->AsGUIEvent()->mWidget);
  mEvent->mRefPoint = RoundedToInt(screenPoint * CSSToLayoutDeviceScale(1));
}

void UIEvent::Serialize(IPC::MessageWriter* aWriter,
                        bool aSerializeInterfaceType) {
  if (aSerializeInterfaceType) {
    IPC::WriteParam(aWriter, u"uievent"_ns);
  }

  Event::Serialize(aWriter, false);

  IPC::WriteParam(aWriter, Detail());
}

bool UIEvent::Deserialize(IPC::MessageReader* aReader) {
  NS_ENSURE_TRUE(Event::Deserialize(aReader), false);
  NS_ENSURE_TRUE(IPC::ReadParam(aReader, &mDetail), false);
  return true;
}

// XXX Following struct and array are used only in
//     UIEvent::ComputeModifierState(), but if we define them in it,
//     we fail to build on Mac at calling std::size().
struct ModifierPair {
  Modifier modifier;
  const char* name;
};
static const ModifierPair kPairs[] = {
    // clang-format off
  { MODIFIER_ALT,        NS_DOM_KEYNAME_ALT },
  { MODIFIER_ALTGRAPH,   NS_DOM_KEYNAME_ALTGRAPH },
  { MODIFIER_CAPSLOCK,   NS_DOM_KEYNAME_CAPSLOCK },
  { MODIFIER_CONTROL,    NS_DOM_KEYNAME_CONTROL },
  { MODIFIER_FN,         NS_DOM_KEYNAME_FN },
  { MODIFIER_FNLOCK,     NS_DOM_KEYNAME_FNLOCK },
  { MODIFIER_META,       NS_DOM_KEYNAME_META },
  { MODIFIER_NUMLOCK,    NS_DOM_KEYNAME_NUMLOCK },
  { MODIFIER_SCROLLLOCK, NS_DOM_KEYNAME_SCROLLLOCK },
  { MODIFIER_SHIFT,      NS_DOM_KEYNAME_SHIFT },
  { MODIFIER_SYMBOL,     NS_DOM_KEYNAME_SYMBOL },
  { MODIFIER_SYMBOLLOCK, NS_DOM_KEYNAME_SYMBOLLOCK },
    // clang-format on
};

// static
Modifiers UIEvent::ComputeModifierState(const nsAString& aModifiersList) {
  if (aModifiersList.IsEmpty()) {
    return 0;
  }

  // Be careful about the performance.  If aModifiersList is too long,
  // parsing it needs too long time.
  // XXX Should we abort if aModifiersList is too long?

  Modifiers modifiers = 0;

  nsAString::const_iterator listStart, listEnd;
  aModifiersList.BeginReading(listStart);
  aModifiersList.EndReading(listEnd);

  for (auto entry : kPairs) {
    nsAString::const_iterator start(listStart), end(listEnd);
    if (!FindInReadable(NS_ConvertASCIItoUTF16(entry.name), start, end)) {
      continue;
    }

    if ((start != listStart && !NS_IsAsciiWhitespace(*(--start))) ||
        (end != listEnd && !NS_IsAsciiWhitespace(*(end)))) {
      continue;
    }
    modifiers |= entry.modifier;
  }

  return modifiers;
}

bool UIEvent::GetModifierStateInternal(const nsAString& aKey) {
  WidgetInputEvent* inputEvent = mEvent->AsInputEvent();
  MOZ_ASSERT(inputEvent, "mEvent must be WidgetInputEvent or derived class");
  return ((inputEvent->mModifiers & WidgetInputEvent::GetModifier(aKey)) != 0);
}

static Modifiers ConvertToModifiers(const EventModifierInit& aParam) {
  Modifiers bits = MODIFIER_NONE;

#define SET_MODIFIER(aName, aValue) bits |= aParam.m##aName ? (aValue) : 0;

  SET_MODIFIER(CtrlKey, MODIFIER_CONTROL)
  SET_MODIFIER(ShiftKey, MODIFIER_SHIFT)
  SET_MODIFIER(AltKey, MODIFIER_ALT)
  SET_MODIFIER(MetaKey, MODIFIER_META)
  SET_MODIFIER(ModifierAltGraph, MODIFIER_ALTGRAPH)
  SET_MODIFIER(ModifierCapsLock, MODIFIER_CAPSLOCK)
  SET_MODIFIER(ModifierFn, MODIFIER_FN)
  SET_MODIFIER(ModifierFnLock, MODIFIER_FNLOCK)
  SET_MODIFIER(ModifierNumLock, MODIFIER_NUMLOCK)
  SET_MODIFIER(ModifierScrollLock, MODIFIER_SCROLLLOCK)
  SET_MODIFIER(ModifierSymbol, MODIFIER_SYMBOL)
  SET_MODIFIER(ModifierSymbolLock, MODIFIER_SYMBOLLOCK)

#undef SET_MODIFIER

  return bits;
}

void UIEvent::InitModifiers(const EventModifierInit& aParam) {
  if (NS_WARN_IF(!mEvent)) {
    return;
  }
  WidgetInputEvent* inputEvent = mEvent->AsInputEvent();
  MOZ_ASSERT(inputEvent,
             "This method shouldn't be called if it doesn't have modifiers");
  if (NS_WARN_IF(!inputEvent)) {
    return;
  }

  inputEvent->mModifiers = ConvertToModifiers(aParam);
}

}  // namespace mozilla::dom

using namespace mozilla;
using namespace mozilla::dom;

already_AddRefed<UIEvent> NS_NewDOMUIEvent(EventTarget* aOwner,
                                           nsPresContext* aPresContext,
                                           WidgetGUIEvent* aEvent) {
  RefPtr<UIEvent> it = new UIEvent(aOwner, aPresContext, aEvent);
  return it.forget();
}
