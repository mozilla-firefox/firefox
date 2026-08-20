/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLButtonElement.h"

#include "HTMLFormSubmissionConstants.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FocusModel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresState.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/CommandEvent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/HTMLButtonElementBinding.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "nsAttrValueInlines.h"
#include "nsAttrValueOrString.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsIContentInlines.h"
#include "nsIFormControl.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsUnicharUtils.h"

#define NS_IN_SUBMIT_CLICK (1 << 0)
#define NS_OUTER_ACTIVATE_EVENT (1 << 1)

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Button)

namespace mozilla::dom {

static constexpr nsAttrValue::EnumTableEntry kButtonTypeTable[] = {
    {"button", FormControlType::ButtonButton},
    {"reset", FormControlType::ButtonReset},
    {"submit", FormControlType::ButtonSubmit},
};

static constexpr nsAttrValue::EnumTableEntry kButtonCommandTable[] = {
    {"close", Element::Command::Close},
    {"hide-popover", Element::Command::HidePopover},

    // Part of "future-invokers" proposal.
    // https://open-ui.org/components/future-invokers.explainer/
    {"open", Element::Command::Open},

    {"request-close", Element::Command::RequestClose},
    {"show-modal", Element::Command::ShowModal},
    {"show-popover", Element::Command::ShowPopover},

    // Part of "future-invokers" proposal.
    // https://open-ui.org/components/future-invokers.explainer/
    {"toggle", Element::Command::Toggle},

    {"toggle-popover", Element::Command::TogglePopover},
};

// The default type is "button" when the command & commandfor attributes are
// present.
static constexpr const nsAttrValue::EnumTableEntry* kButtonButtonType =
    &kButtonTypeTable[0];

// Default type is 'submit' when the `command` or `commandfor` attributes are
// not present.
static constexpr const nsAttrValue::EnumTableEntry* kButtonSubmitType =
    &kButtonTypeTable[2];

// Construction, destruction
HTMLButtonElement::HTMLButtonElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo, FromParser aFromParser)
    : nsGenericHTMLFormControlElementWithState(
          std::move(aNodeInfo), aFromParser,
          FormControlType(kButtonSubmitType->value)),
      mDisabledChanged(false),
      mInInternalActivate(false),
      mInhibitStateRestoration(aFromParser & FROM_PARSER_FRAGMENT) {
  // Set up our default state: enabled
  AddStatesSilently(ElementState::ENABLED);
}

HTMLButtonElement::~HTMLButtonElement() = default;

// nsISupports

NS_IMPL_CYCLE_COLLECTION_INHERITED(HTMLButtonElement,
                                   nsGenericHTMLFormControlElementWithState,
                                   mValidity)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(
    HTMLButtonElement, nsGenericHTMLFormControlElementWithState,
    nsIConstraintValidation)

void HTMLButtonElement::SetCustomValidity(const nsAString& aError) {
  ConstraintValidation::SetCustomValidity(aError);
  UpdateValidityElementStates(true);
}

void HTMLButtonElement::UpdateBarredFromConstraintValidation() {
  SetBarredFromConstraintValidation(
      mType == FormControlType::ButtonButton ||
      mType == FormControlType::ButtonReset ||
      HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR) || IsDisabled());
}

void HTMLButtonElement::FieldSetDisabledChanged(bool aNotify) {
  // FieldSetDisabledChanged *has* to be called *before*
  // UpdateBarredFromConstraintValidation, because the latter depends on our
  // disabled state.
  nsGenericHTMLFormControlElementWithState::FieldSetDisabledChanged(aNotify);

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(aNotify);
}

NS_IMPL_ELEMENT_CLONE(HTMLButtonElement)

void HTMLButtonElement::GetFormEnctype(nsAString& aFormEncType) {
  GetEnumAttr(nsGkAtoms::formenctype, "", kFormDefaultEnctype->tag,
              aFormEncType);
}

void HTMLButtonElement::GetFormMethod(nsAString& aFormMethod) {
  GetEnumAttr(nsGkAtoms::formmethod, "", kFormDefaultMethod->tag, aFormMethod);
}

bool HTMLButtonElement::InAutoState() const {
  const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::type);
  return (!attr || attr->Type() != nsAttrValue::eEnum);
}

// https://html.spec.whatwg.org/multipage/#the-button-element%3Aconcept-submit-button
const nsAttrValue::EnumTableEntry* HTMLButtonElement::ResolveAutoState(
    const nsINode* aParent) const {
  // A button element is said to be a submit button if any of the following are
  // true: the type attribute is in the Auto state, both the command and
  // commandfor content attributes are not present, and the parent node is not a
  // select element; or the type attribute is in the Submit Button state.
  if (HasAttr(nsGkAtoms::commandfor) || HasAttr(nsGkAtoms::command)) {
    return kButtonButtonType;
  }
  const nsINode* parent = aParent ? aParent : GetParentNode();
  if (parent && parent->IsHTMLElement(nsGkAtoms::select)) {
    return kButtonButtonType;
  }
  return kButtonSubmitType;
}

void HTMLButtonElement::GetType(nsAString& aType) {
  aType.Truncate();
  GetEnumAttr(nsGkAtoms::type, ResolveAutoState()->tag, aType);
  MOZ_ASSERT(aType.Length() > 0);
}

int32_t HTMLButtonElement::TabIndexDefault() { return 0; }

bool HTMLButtonElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                        bool* aIsFocusable,
                                        int32_t* aTabIndex) {
  if (nsGenericHTMLFormControlElementWithState::IsHTMLFocusable(
          aFlags, aIsFocusable, aTabIndex)) {
    return true;
  }
  *aIsFocusable = IsFormControlDefaultFocusable(aFlags) && !IsDisabled();
  return false;
}

bool HTMLButtonElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::type) {
      return aResult.ParseEnumValue(aValue, kButtonTypeTable, false);
    }

    if (aAttribute == nsGkAtoms::formmethod) {
      return aResult.ParseEnumValue(aValue, kFormMethodTable, false);
    }
    if (aAttribute == nsGkAtoms::formenctype) {
      return aResult.ParseEnumValue(aValue, kFormEnctypeTable, false);
    }
    if (aAttribute == nsGkAtoms::command) {
      return aResult.ParseEnumValue(aValue, kButtonCommandTable, false);
    }
    if (aAttribute == nsGkAtoms::commandfor) {
      aResult.ParseAtom(aValue);
      return true;
    }
  }

  return nsGenericHTMLFormControlElementWithState::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

bool HTMLButtonElement::IsDisabledForEvents(WidgetEvent* aEvent) {
  return IsElementDisabledForEvents(aEvent, GetPrimaryFrame());
}

void HTMLButtonElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = false;

  if (IsDisabledForEvents(aVisitor.mEvent)) {
    return;
  }

  // Track whether we're in the outermost Dispatch invocation that will
  // cause activation of the input.  That is, if we're a click event, or a
  // DOMActivate that was dispatched directly, this will be set, but if we're
  // a DOMActivate dispatched from click handling, it will not be set.
  WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
  bool outerActivateEvent =
      ((mouseEvent && mouseEvent->IsLeftClickEvent()) ||
       (aVisitor.mEvent->mMessage == eLegacyDOMActivate &&
        !mInInternalActivate && aVisitor.mEvent->mOriginalTarget == this));

  if (outerActivateEvent) {
    aVisitor.mItemFlags |= NS_OUTER_ACTIVATE_EVENT;
    aVisitor.mWantsActivationBehavior = true;
  }

  nsGenericHTMLElement::GetEventTargetParent(aVisitor);
}

void HTMLButtonElement::LegacyPreActivationBehavior(
    EventChainVisitor& aVisitor) {
  // out-of-spec legacy pre-activation behavior needed because of bug 1803805
  if (mType == FormControlType::ButtonSubmit && mForm) {
    aVisitor.mItemFlags |= NS_IN_SUBMIT_CLICK;
    aVisitor.mItemData = static_cast<Element*>(mForm);
    // tell the form that we are about to enter a click handler.
    // that means that if there are scripted submissions, the
    // latest one will be deferred until after the exit point of the handler.
    mForm->OnSubmitClickBegin();
  }
}

nsresult HTMLButtonElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  nsresult rv = NS_OK;
  if (!aVisitor.mPresContext) {
    return rv;
  }

  if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault) {
    WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
    if (mouseEvent && mouseEvent->IsLeftClickEvent() &&
        OwnerDoc()->MayHaveDOMActivateListeners()) {
      // DOMActive event should be trusted since the activation is actually
      // occurred even if the cause is an untrusted click event.
      InternalUIEvent actEvent(true, eLegacyDOMActivate, mouseEvent);
      actEvent.mDetail = 1;

      if (RefPtr<PresShell> presShell = aVisitor.mPresContext->GetPresShell()) {
        nsEventStatus status = nsEventStatus_eIgnore;
        mInInternalActivate = true;
        presShell->HandleDOMEventWithTarget(this, &actEvent, &status);
        mInInternalActivate = false;

        // If activate is cancelled, we must do the same as when click is
        // cancelled (revert the checkbox to its original value).
        if (status == nsEventStatus_eConsumeNoDefault) {
          aVisitor.mEventStatus = status;
        }
      }
    }
  }

  if (nsEventStatus_eIgnore == aVisitor.mEventStatus) {
    WidgetKeyboardEvent* keyEvent = aVisitor.mEvent->AsKeyboardEvent();
    if (keyEvent && keyEvent->IsTrusted()) {
      HandleKeyboardActivation(aVisitor);
    }

    // Bug 1459231: Temporarily needed till links respect activation target
    // Then also remove NS_OUTER_ACTIVATE_EVENT
    if ((aVisitor.mItemFlags & NS_OUTER_ACTIVATE_EVENT) && mForm &&
        (mType == FormControlType::ButtonReset ||
         mType == FormControlType::ButtonSubmit)) {
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
    }
  }

  return rv;
}

void EndSubmitClick(EventChainVisitor& aVisitor) {
  if ((aVisitor.mItemFlags & NS_IN_SUBMIT_CLICK)) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(aVisitor.mItemData));
    RefPtr<HTMLFormElement> form = HTMLFormElement::FromNodeOrNull(content);
    MOZ_ASSERT(form);
    // Tell the form that we are about to exit a click handler,
    // so the form knows not to defer subsequent submissions.
    // The pending ones that were created during the handler
    // will be flushed or forgotten.
    form->OnSubmitClickEnd();
    // Tell the form to flush a possible pending submission.
    // the reason is that the script returned false (the event was
    // not ignored) so if there is a stored submission, it needs to
    // be submitted immediatelly.
    // Note, NS_IN_SUBMIT_CLICK is set only when we're in outer activate event.
    form->FlushPendingSubmission();
  }
}

// https://html.spec.whatwg.org/multipage/form-elements.html#the-button-element:activation-behaviour
void HTMLButtonElement::ActivationBehavior(EventChainPostVisitor& aVisitor) {
  auto endSubmit = MakeScopeExit([&] { EndSubmitClick(aVisitor); });

  if (!aVisitor.mPresContext) {
    return;
  }

  // 1. If element is disabled, then return.
  if (IsDisabled()) {
    return;
  }

  // 2. If element's node document is not fully active, then return.

  // 3. If element has a form owner:
  if (mForm) {
    // Hold a strong ref while dispatching
    RefPtr<mozilla::dom::HTMLFormElement> form(mForm);
    // 3.1. If element is a submit button, then submit element's form owner from
    // element with userInvolvement set to event's user navigation involvement,
    // and return.
    if (mType == FormControlType::ButtonSubmit) {
      form->MaybeSubmit(this);
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      return;
    }
    // 3.2. If element's type attribute is in the Reset Button state, then reset
    // element's form owner, and return.
    if (mType == FormControlType::ButtonReset) {
      form->MaybeReset(this);
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      return;
    }
    // 3.3. If element's type attribute is in the Auto state, then return.
    if (InAutoState()) {
      return;
    }
  }

  // 4. Let target be the result of running element's get the
  // commandfor-associated element.
  RefPtr<Element> target = GetCommandForElementInternal();

  // 5. If target is not null:
  if (target) {
    // 5.1. Let command be element's command attribute.
    Element::Command command = GetCommand();

    // 5.2. If command is in the Unknown state, then return.
    if (command == Command::Invalid) {
      return;
    }

    // 5.3. Let isPopover be true if target's popover attribute is not in the No
    // Popover state; otherwise false.
    // 5.4. If isPopover is false and command is not in the Custom state:
    // (Checking isPopover is handled as part of IsValidCommandAction)
    // 5.4.1. Assert: target's namespace is the HTML namespace.
    // 5.4.2. If this standard does not define is valid invoker command steps
    // for target's local name, then return.
    // 5.4.3. Otherwise, if the result of running target's corresponding is
    // valid invoker command steps given command is false, then return.
    if (command != Command::Custom && !target->IsValidCommandAction(command)) {
      return;
    }

    // 5.5. Let continue be the result of firing an event named command at
    // target, using CommandEvent, with its command attribute initialized to
    // command, its source attribute initialized to element, and its cancelable
    // attribute initialized to true.
    CommandEventInit init;
    GetCommand(init.mCommand);
    init.mSource = this;
    init.mCancelable = true;
    RefPtr<Event> event = CommandEvent::Constructor(this, u"command"_ns, init);
    event->SetTrusted(true);
    event->SetTarget(target);
    EventDispatcher::DispatchDOMEvent(target, nullptr, event, nullptr, nullptr);

    // 5.6. If continue is false, then return.
    // 5.7. If target is not connected, then return.
    // 5.8. If command is in the Custom state, then return.
    if (event->DefaultPrevented() || !target->IsInComposedDoc() ||
        command == Command::Custom) {
      return;
    }

    // Steps 5.9...5.12. handled with HandleCommandInternal:
    target->HandleCommandInternal(this, command, IgnoreErrors());

  } else {
    nsCOMPtr<Element> eventTarget =
        do_QueryInterface(aVisitor.mEvent->mOriginalTarget);
    // 6. Otherwise, run the popover target attribute activation behavior given
    // element and event's target.
    HandlePopoverTargetAction(eventTarget);
  }
}

void HTMLButtonElement::LegacyCanceledActivationBehavior(
    EventChainPostVisitor& aVisitor) {
  // still need to end submission, see bug 1803805
  // e.g. when parent element of button has event handler preventing default
  // legacy canceled instead of activation behavior will be run
  EndSubmitClick(aVisitor);
}

nsresult HTMLButtonElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  // Whether this button is a submit button depends on whether its parent is a
  // select element, so recompute the resolved type before form association.
  // Use aParent because the parser can bind before the parent link is set.
  if (InAutoState()) {
    mType = FormControlType(ResolveAutoState(&aParent)->value);
  }
  nsresult rv =
      nsGenericHTMLFormControlElementWithState::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(false);

  return NS_OK;
}

void HTMLButtonElement::UnbindFromTree(UnbindContext& aContext) {
  nsGenericHTMLFormControlElementWithState::UnbindFromTree(aContext);

  // The parent may no longer be a select, so recompute the resolved type.
  if (InAutoState()) {
    mType = FormControlType(ResolveAutoState()->value);
  }

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(false);
}

NS_IMETHODIMP
HTMLButtonElement::Reset() { return NS_OK; }

NS_IMETHODIMP
HTMLButtonElement::SubmitNamesValues(FormData* aFormData) {
  //
  // We only submit if we were the button pressed
  //
  if (aFormData->GetSubmitterElement() != this) {
    return NS_OK;
  }

  //
  // Get the name (if no name, no submit)
  //
  nsAutoString name;
  GetHTMLAttr(nsGkAtoms::name, name);
  if (name.IsEmpty()) {
    return NS_OK;
  }

  //
  // Get the value
  //
  nsAutoString value;
  GetHTMLAttr(nsGkAtoms::value, value);

  //
  // Submit
  //
  return aFormData->AddNameValuePair(name, value);
}

void HTMLButtonElement::DoneCreatingElement() {
  if (!mInhibitStateRestoration) {
    GenerateStateKey();
    RestoreFormControlState();
  }
}

void HTMLButtonElement::BeforeSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                      const nsAttrValue* aValue, bool aNotify) {
  if (aNotify && aName == nsGkAtoms::disabled &&
      aNameSpaceID == kNameSpaceID_None) {
    mDisabledChanged = true;
  }

  return nsGenericHTMLFormControlElementWithState::BeforeSetAttr(
      aNameSpaceID, aName, aValue, aNotify);
}

void HTMLButtonElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::type) {
      if (aValue && aValue->Type() == nsAttrValue::eEnum) {
        mType = FormControlType(aValue->GetEnumValue());
      } else {
        mType = FormControlType(ResolveAutoState()->value);
      }
    }

    // If the command/commandfor attributes are added and Type is auto, it may
    // need to be recalculated:
    if (aName == nsGkAtoms::command || aName == nsGkAtoms::commandfor) {
      if (InAutoState()) {
        mType = FormControlType(ResolveAutoState()->value);
      }
    }

    MOZ_ASSERT(mType == FormControlType::ButtonButton ||
               mType == FormControlType::ButtonSubmit ||
               mType == FormControlType::ButtonReset);

    if (aName == nsGkAtoms::type || aName == nsGkAtoms::disabled ||
        aName == nsGkAtoms::command || aName == nsGkAtoms::commandfor) {
      if (aName == nsGkAtoms::disabled) {
        // This *has* to be called *before* validity state check because
        // UpdateBarredFromConstraintValidation depends on our disabled state.
        UpdateDisabledState(aNotify);
      }

      UpdateBarredFromConstraintValidation();
      UpdateValidityElementStates(aNotify);
    }
  }

  return nsGenericHTMLFormControlElementWithState::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void HTMLButtonElement::SaveState() {
  if (!mDisabledChanged) {
    return;
  }

  PresState* state = GetPrimaryPresState();
  if (state) {
    // We do not want to save the real disabled state but the disabled
    // attribute.
    state->disabled() = HasAttr(nsGkAtoms::disabled);
    state->disabledSet() = true;
  }
}

bool HTMLButtonElement::RestoreState(PresState* aState) {
  if (aState && aState->disabledSet() && !aState->disabled()) {
    SetDisabled(false, IgnoreErrors());
  }
  return false;
}

void HTMLButtonElement::UpdateValidityElementStates(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::VALIDITY_STATES);
  if (!IsCandidateForConstraintValidation()) {
    return;
  }
  if (IsValid()) {
    AddStatesSilently(ElementState::VALID | ElementState::USER_VALID);
  } else {
    AddStatesSilently(ElementState::INVALID | ElementState::USER_INVALID);
  }
}

void HTMLButtonElement::GetCommand(nsAString& aCommand) const {
  aCommand.Truncate();
  Element::Command command = GetCommand();
  if (command == Command::Invalid) {
    return;
  }
  if (command == Command::Custom) {
    const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::command);
    MOZ_ASSERT(attr->Type() == nsAttrValue::eString ||
               attr->Type() == nsAttrValue::eAtom);
    aCommand.Assign(nsAttrValueOrString(attr).String());
    MOZ_ASSERT(
        aCommand.Length() >= 2,
        "Custom commands start with '--' so must be atleast 2 chars long!");
    MOZ_ASSERT(StringBeginsWith(aCommand, u"--"_ns),
               "Custom commands start with '--'");
    return;
  }
  GetEnumAttr(nsGkAtoms::command, "", aCommand);
}

Element::Command HTMLButtonElement::GetCommand() const {
  if (const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::command)) {
    if (attr->Type() == nsAttrValue::eEnum) {
      auto command = Command(attr->GetEnumValue());
      // "open" and "toggle" commands are for the Detials feature, part of
      // "future-invokers" proposal. They should not be exposed as valid
      // commands unless the details feature is enabled. "close" is also part of
      // this feature, but it is also valid for dialogs, so can be exposed.
      // https://open-ui.org/components/future-invokers.explainer/
      if ((command == Command::Open || command == Command::Toggle) &&
          !StaticPrefs::dom_element_commandfor_on_details_enabled()) {
        return Command::Invalid;
      }
      return command;
    }
    if (StringBeginsWith(nsAttrValueOrString(attr).String(), u"--"_ns)) {
      return Command::Custom;
    }
  }
  return Command::Invalid;
}

Element* HTMLButtonElement::GetCommandForElementForBindings() const {
  return GetAttrAssociatedElementForBindings(nsGkAtoms::commandfor);
}

Element* HTMLButtonElement::GetCommandForElementInternal() const {
  return GetAttrAssociatedElementInternal(nsGkAtoms::commandfor);
}

void HTMLButtonElement::SetCommandForElementForBindings(Element* aElement) {
  ExplicitlySetAttrElement(nsGkAtoms::commandfor, aElement);
}

JSObject* HTMLButtonElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return HTMLButtonElement_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
#undef NS_IN_SUBMIT_CLICK
#undef NS_OUTER_ACTIVATE_EVENT
