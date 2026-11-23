/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Implementation of HTML <label> elements.
 */
#include "HTMLLabelElement.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLLabelElementBinding.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/ShadowRoot.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIFrame.h"
#include "nsQueryObject.h"

// construction, destruction

NS_IMPL_NS_NEW_HTML_ELEMENT(Label)

namespace mozilla::dom {

HTMLLabelElement::~HTMLLabelElement() = default;

JSObject* HTMLLabelElement::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return HTMLLabelElement_Binding::Wrap(aCx, this, aGivenProto);
}

// nsIDOMHTMLLabelElement

NS_IMPL_ELEMENT_CLONE(HTMLLabelElement)

Element* HTMLLabelElement::GetFormForBindings() const {
  return RetargetReferenceTargetForBindings(GetFormInternal());
}

HTMLFormElement* HTMLLabelElement::GetFormInternal() const {
  // Not all labeled things have a form association.  Stick to the ones that do.
  const auto* formControl =
      nsIFormControl::FromNodeOrNull(GetLabeledElementInternal());
  if (!formControl) {
    return nullptr;
  }

  return formControl->GetFormInternal();
}

nsGenericHTMLElement* HTMLLabelElement::GetControlForBindings() const {
  nsINode* retargeted =
      nsContentUtils::Retarget(GetLabeledElementInternal(), this);
  if (!retargeted) {
    return nullptr;
  }
  Element* element = retargeted->AsElement();
  MOZ_ASSERT(element);
  return static_cast<nsGenericHTMLElement*>(element);
}

nsChangeHint HTMLLabelElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  nsChangeHint retval =
      nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (OwnerDoc()->ChromeRulesEnabled() &&
      (aAttribute == nsGkAtoms::crop || aAttribute == nsGkAtoms::value)) {
    retval |= nsChangeHint_ReconstructFrame;
  }
  return retval;
}

void HTMLLabelElement::Focus(const FocusOptions& aOptions,
                             const CallerType aCallerType,
                             ErrorResult& aError) {
  {
    nsIFrame* frame = GetPrimaryFrame(FlushType::Frames);
    if (frame && frame->IsFocusable()) {
      return nsGenericHTMLElement::Focus(aOptions, aCallerType, aError);
    }
  }

  if (RefPtr<Element> elem = GetLabeledElementInternal()) {
    return elem->Focus(aOptions, aCallerType, aError);
  }
}

static bool WantsPostHandleEvent(EventChainVisitor& aVisitor) {
  switch (aVisitor.mEvent->mMessage) {
    case ePointerClick:
    case eMouseDown:
      return true;
    default:
      return false;
  }
}

void HTMLLabelElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mWantsPostHandleEvent = WantsPostHandleEvent(aVisitor);
  nsGenericHTMLElement::GetEventTargetParent(aVisitor);
}

nsresult HTMLLabelElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  MOZ_ASSERT(WantsPostHandleEvent(aVisitor));

  WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
  if (mHandlingEvent ||
      (!(mouseEvent && mouseEvent->IsLeftClickEvent()) &&
       aVisitor.mEvent->mMessage != eMouseDown) ||
      aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault ||
      !aVisitor.mPresContext ||
      // Don't handle the event if it's already been handled by another label
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented) {
    return NS_OK;
  }

  nsCOMPtr<Element> target =
      do_QueryInterface(aVisitor.mEvent->GetOriginalDOMEventTarget());
  if (nsContentUtils::IsInInteractiveHTMLContent(target, this)) {
    return NS_OK;
  }

  // Strong ref because event dispatch is going to happen.
  RefPtr<Element> content = GetLabeledElementInternal();

  if (!content || content->IsDisabled()) {
    return NS_OK;
  }

  mHandlingEvent = true;
  switch (aVisitor.mEvent->mMessage) {
    case eMouseDown:
      if (mouseEvent->mButton == MouseButton::ePrimary) {
        // We reset the mouse-down point on every event because there is
        // no guarantee we will reach the ePointerClick code below.
        LayoutDeviceIntPoint* curPoint =
            new LayoutDeviceIntPoint(mouseEvent->mRefPoint);
        SetProperty(nsGkAtoms::labelMouseDownPtProperty,
                    static_cast<void*>(curPoint),
                    nsINode::DeleteProperty<LayoutDeviceIntPoint>);
      }
      break;

    case ePointerClick:
      if (mouseEvent->IsLeftClickEvent()) {
        LayoutDeviceIntPoint* mouseDownPoint =
            static_cast<LayoutDeviceIntPoint*>(
                GetProperty(nsGkAtoms::labelMouseDownPtProperty));

        bool dragSelect = false;
        if (mouseDownPoint) {
          LayoutDeviceIntPoint dragDistance = *mouseDownPoint;
          RemoveProperty(nsGkAtoms::labelMouseDownPtProperty);

          dragDistance -= mouseEvent->mRefPoint;
          const int CLICK_DISTANCE = 2;
          dragSelect = dragDistance.x > CLICK_DISTANCE ||
                       dragDistance.x < -CLICK_DISTANCE ||
                       dragDistance.y > CLICK_DISTANCE ||
                       dragDistance.y < -CLICK_DISTANCE;
        }
        // Don't click the for-content if we did drag-select text or if we
        // have a kbd modifier (which adjusts a selection).
        if (dragSelect || mouseEvent->IsShift() || mouseEvent->IsControl() ||
            mouseEvent->IsAlt() || mouseEvent->IsMeta()) {
          break;
        }
        // Only set focus on the first click of multiple clicks to prevent
        // to prevent immediate de-focus.
        if (mouseEvent->mClickCount <= 1) {
          if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
            // Use FLAG_BYMOVEFOCUS here so that the label is scrolled to.
            // Also, within HTMLInputElement::PostHandleEvent, inputs will
            // be selected only when focused via a key or when the navigation
            // flag is used and we want to select the text on label clicks as
            // well.
            // If the label has been clicked by the user, we also want to
            // pass FLAG_BYMOUSE so that we get correct focus ring behavior,
            // but we don't want to pass FLAG_BYMOUSE if this click event was
            // caused by the user pressing an accesskey.
            bool byMouse = (mouseEvent->mInputSource !=
                            MouseEvent_Binding::MOZ_SOURCE_KEYBOARD);
            bool byTouch = (mouseEvent->mInputSource ==
                            MouseEvent_Binding::MOZ_SOURCE_TOUCH);
            fm->SetFocus(content,
                         nsIFocusManager::FLAG_BYMOVEFOCUS |
                             (byMouse ? nsIFocusManager::FLAG_BYMOUSE : 0) |
                             (byTouch ? nsIFocusManager::FLAG_BYTOUCH : 0));
          }
        }
        // Dispatch a new click event to |content|
        //    (For compatibility with IE, we do only left click.  If
        //    we wanted to interpret the HTML spec very narrowly, we
        //    would do nothing.  If we wanted to do something
        //    sensible, we might send more events through like
        //    this.)  See bug 7554, bug 49897, and bug 96813.
        nsEventStatus status = aVisitor.mEventStatus;
        // Ok to use aVisitor.mEvent as parameter because DispatchClickEvent
        // will actually create a new event.
        EventFlags eventFlags;
        eventFlags.mMultipleActionsPrevented = true;
        DispatchClickEvent(aVisitor.mPresContext, mouseEvent, content, false,
                           &eventFlags, &status);
        // Do we care about the status this returned?  I don't think we do...
        // Don't run another <label> off of this click
        mouseEvent->mFlags.mMultipleActionsPrevented = true;
      }
      break;

    default:
      break;
  }
  mHandlingEvent = false;
  return NS_OK;
}

Result<bool, nsresult> HTMLLabelElement::PerformAccesskey(
    bool aKeyCausesActivation, bool aIsTrustedEvent) {
  if (!aKeyCausesActivation) {
    RefPtr<Element> element = GetLabeledElementInternal();
    if (element) {
      return element->PerformAccesskey(aKeyCausesActivation, aIsTrustedEvent);
    }
    return Err(NS_ERROR_ABORT);
  }

  RefPtr<nsPresContext> presContext = GetPresContext(eForComposedDoc);
  if (!presContext) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  // Click on it if the users prefs indicate to do so.
  AutoHandlingUserInputStatePusher userInputStatePusher(aIsTrustedEvent);
  AutoPopupStatePusher popupStatePusher(
      aIsTrustedEvent ? PopupBlocker::openAllowed : PopupBlocker::openAbused);
  DispatchSimulatedClick(this, aIsTrustedEvent, presContext);

  // XXXedgar, do we need to check whether the focus is really changed?
  return true;
}

nsGenericHTMLElement* HTMLLabelElement::GetLabeledElementInternal() const {
  nsAutoString elementId;

  if (!GetAttr(nsGkAtoms::_for, elementId)) {
    // No @for, so we are a label for our first form control element.
    // Do a depth-first traversal to look for the first form control element.
    return GetFirstLabelableDescendant();
  }

  // We have a @for. The id has to be linked to an element in the same tree
  // and this element should be a labelable form control.
  Element* element = GetAttrAssociatedElementInternal(nsGkAtoms::_for);
  if (element && element->IsLabelable()) {
    return static_cast<nsGenericHTMLElement*>(element);
  }

  return nullptr;
}

nsGenericHTMLElement* HTMLLabelElement::GetFirstLabelableDescendant() const {
  for (nsIContent* cur = nsINode::GetFirstChild(); cur;
       cur = cur->GetNextNode(this)) {
    Element* element = Element::FromNode(cur);
    if (element) {
      Element* referenceTarget = element->ResolveReferenceTarget();
      if (referenceTarget && referenceTarget->IsLabelable()) {
        return static_cast<nsGenericHTMLElement*>(referenceTarget);
      }
    }
  }

  return nullptr;
}

}  // namespace mozilla::dom
