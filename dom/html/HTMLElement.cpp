/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLElement.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/PresState.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementInternalsBinding.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/HTMLElementBinding.h"
#include "mozilla/dom/LifecycleCallbackArgs.h"
#include "nsContentUtils.h"
#include "nsGenericHTMLElement.h"
#include "nsILayoutHistoryState.h"

namespace mozilla::dom {

HTMLElement::HTMLElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
                         FromParser aFromParser)
    : nsGenericHTMLFormElement(std::move(aNodeInfo)) {
  if (NodeInfo()->Equals(nsGkAtoms::bdi)) {
    AddStatesSilently(ElementState::HAS_DIR_ATTR_LIKE_AUTO);
    OwnerDoc()->SetNeedsDirHandling();
  }

  InhibitRestoration(!(aFromParser & FROM_PARSER_NETWORK));
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(HTMLElement, nsGenericHTMLFormElement)

// QueryInterface implementation for HTMLElement

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(HTMLElement)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsIFormControl, GetElementInternals())
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsIConstraintValidation, GetElementInternals())
NS_INTERFACE_MAP_END_INHERITING(nsGenericHTMLFormElement)

NS_IMPL_ADDREF_INHERITED(HTMLElement, nsGenericHTMLFormElement)
NS_IMPL_RELEASE_INHERITED(HTMLElement, nsGenericHTMLFormElement)

NS_IMPL_ELEMENT_CLONE(HTMLElement)

JSObject* HTMLElement::WrapNode(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return dom::HTMLElement_Binding::Wrap(aCx, this, aGivenProto);
}

void HTMLElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  if (IsDisabledForEvents(aVisitor.mEvent)) {
    // Do not process any DOM events if the element is disabled
    aVisitor.mCanHandle = false;
    return;
  }

  nsGenericHTMLFormElement::GetEventTargetParent(aVisitor);
}

nsINode* HTMLElement::GetScopeChainParent() const {
  if (IsFormAssociatedCustomElement()) {
    if (auto* form = GetFormInternal()) {
      return form;
    }
  }
  return nsGenericHTMLFormElement::GetScopeChainParent();
}

nsresult HTMLElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsGenericHTMLFormElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(false);
  return rv;
}

void HTMLElement::UnbindFromTree(UnbindContext& aContext) {
  nsGenericHTMLFormElement::UnbindFromTree(aContext);

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(false);
}

void HTMLElement::DoneCreatingElement() {
  if (MOZ_UNLIKELY(IsFormAssociatedElement())) {
    MaybeRestoreFormAssociatedCustomElementState();
  }
}

void HTMLElement::SaveState() {
  if (MOZ_LIKELY(!IsFormAssociatedElement())) {
    return;
  }

  auto* internals = GetElementInternals();

  nsCString stateKey = internals->GetStateKey();
  if (stateKey.IsEmpty()) {
    return;
  }

  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistory(false);
  if (!history) {
    return;
  }

  // Get the pres state for this key, if it doesn't exist, create one.
  PresState* result = history->GetState(stateKey);
  if (!result) {
    UniquePtr<PresState> newState = NewPresState();
    result = newState.get();
    history->AddState(stateKey, std::move(newState));
  }

  const auto& state = internals->GetFormState();
  const auto& value = internals->GetFormSubmissionValue();
  result->contentData() = CustomElementTuple(
      nsContentUtils::ConvertToCustomElementFormValue(value),
      nsContentUtils::ConvertToCustomElementFormValue(state));
}

void HTMLElement::MaybeRestoreFormAssociatedCustomElementState() {
  MOZ_ASSERT(IsFormAssociatedElement());

  if (HasFlag(HTML_ELEMENT_INHIBIT_RESTORATION)) {
    return;
  }

  auto* internals = GetElementInternals();
  if (internals->GetStateKey().IsEmpty()) {
    Document* doc = GetUncomposedDoc();
    nsCString stateKey;
    nsContentUtils::GenerateStateKey(this, doc, stateKey);
    internals->SetStateKey(std::move(stateKey));

    RestoreFormAssociatedCustomElementState();
  }
}

void HTMLElement::RestoreFormAssociatedCustomElementState() {
  MOZ_ASSERT(IsFormAssociatedElement());

  auto* internals = GetElementInternals();

  const nsCString& stateKey = internals->GetStateKey();
  if (stateKey.IsEmpty()) {
    return;
  }
  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistory(true);
  if (!history) {
    return;
  }
  PresState* result = history->GetState(stateKey);
  if (!result) {
    return;
  }
  auto& content = result->contentData();
  if (content.type() != PresContentData::TCustomElementTuple) {
    return;
  }

  auto& ce = content.get_CustomElementTuple();
  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  internals->RestoreFormValue(
      nsContentUtils::ExtractFormAssociatedCustomElementValue(global,
                                                              ce.value()),
      nsContentUtils::ExtractFormAssociatedCustomElementValue(global,
                                                              ce.state()));
}

void HTMLElement::InhibitRestoration(bool aShouldInhibit) {
  if (aShouldInhibit) {
    SetFlags(HTML_ELEMENT_INHIBIT_RESTORATION);
  } else {
    UnsetFlags(HTML_ELEMENT_INHIBIT_RESTORATION);
  }
}

void HTMLElement::SetCustomElementDefinition(
    CustomElementDefinition* aDefinition) {
  nsGenericHTMLFormElement::SetCustomElementDefinition(aDefinition);
  // Always create an ElementInternal for form-associated custom element as the
  // Form related implementation lives in ElementInternal which implements
  // nsIFormControl. It is okay for the attachElementInternal API as there is a
  // separated flag for whether attachElementInternal is called.
  if (aDefinition && !aDefinition->IsCustomBuiltIn() &&
      aDefinition->mFormAssociated) {
    CustomElementData* data = GetCustomElementData();
    MOZ_ASSERT(data);
    auto* internals = data->GetOrCreateElementInternals(this);

    // This is for the case that script constructs a custom element directly,
    // e.g. via new MyCustomElement(), where the upgrade steps won't be ran to
    // update the disabled state in UpdateFormOwner().
    if (data->mState == CustomElementData::State::eCustom) {
      UpdateDisabledState(true);
    } else if (!HasFlag(HTML_ELEMENT_INHIBIT_RESTORATION)) {
      internals->InitializeControlNumber();
    }
  }
}

// https://html.spec.whatwg.org/commit-snapshots/53bc3803433e1c817918b83e8a84f3db900031dd/#dom-attachinternals
already_AddRefed<ElementInternals> HTMLElement::AttachInternals(
    ErrorResult& aRv) {
  CustomElementData* ceData = GetCustomElementData();

  // 1. If element's is value is not null, then throw a "NotSupportedError"
  //    DOMException.
  if (nsAtom* isAtom = ceData ? ceData->GetIs(this) : nullptr) {
    aRv.ThrowNotSupportedError(nsPrintfCString(
        "Cannot attach ElementInternals to a customized built-in element "
        "'%s'",
        NS_ConvertUTF16toUTF8(isAtom->GetUTF16String()).get()));
    return nullptr;
  }

  // 2. Let definition be the result of looking up a custom element definition
  //    given element's node document, its namespace, its local name, and null
  //    as is value.
  nsAtom* nameAtom = NodeInfo()->NameAtom();
  CustomElementDefinition* definition = nullptr;
  if (ceData) {
    definition = ceData->GetCustomElementDefinition();

    // If the definition is null, the element possible hasn't yet upgraded.
    // Fallback to use LookupCustomElementDefinition to find its definition.
    if (!definition) {
      definition = nsContentUtils::LookupCustomElementDefinition(
          NodeInfo()->GetDocument(), nameAtom, NodeInfo()->NamespaceID(),
          ceData->GetCustomElementType());
    }
  }

  // 3. If definition is null, then throw an "NotSupportedError" DOMException.
  if (!definition) {
    aRv.ThrowNotSupportedError(nsPrintfCString(
        "Cannot attach ElementInternals to a non-custom element '%s'",
        NS_ConvertUTF16toUTF8(nameAtom->GetUTF16String()).get()));
    return nullptr;
  }

  // 4. If definition's disable internals is true, then throw a
  //    "NotSupportedError" DOMException.
  if (definition->mDisableInternals) {
    aRv.ThrowNotSupportedError(nsPrintfCString(
        "AttachInternal() to '%s' is disabled by disabledFeatures",
        NS_ConvertUTF16toUTF8(nameAtom->GetUTF16String()).get()));
    return nullptr;
  }

  // If this is not a custom element, i.e. ceData is nullptr, we are unable to
  // find a definition and should return earlier above.
  MOZ_ASSERT(ceData);

  // 5. If element's attached internals is true, then throw an
  //    "NotSupportedError" DOMException.
  if (ceData->HasAttachedInternals()) {
    aRv.ThrowNotSupportedError(nsPrintfCString(
        "AttachInternals() has already been called from '%s'",
        NS_ConvertUTF16toUTF8(nameAtom->GetUTF16String()).get()));
    return nullptr;
  }

  // 6. If element's custom element state is not "precustomized" or "custom",
  //    then throw a "NotSupportedError" DOMException.
  if (ceData->mState != CustomElementData::State::ePrecustomized &&
      ceData->mState != CustomElementData::State::eCustom) {
    aRv.ThrowNotSupportedError(
        R"(Custom element state is not "precustomized" or "custom".)");
    return nullptr;
  }

  // 7. Set element's attached internals to true.
  ceData->AttachedInternals();

  // 8. Create a new ElementInternals instance targeting element, and return it.
  return do_AddRef(ceData->GetOrCreateElementInternals(this));
}

void HTMLElement::AfterClearForm(bool aUnbindOrDelete) {
  // No need to enqueue formAssociated callback if we aren't releasing or
  // unbinding from tree, UpdateFormOwner() will handle it.
  if (aUnbindOrDelete) {
    MOZ_ASSERT(IsFormAssociatedElement());
    nsContentUtils::EnqueueLifecycleCallback(
        ElementCallbackType::eFormAssociated, this, {});
  }
}

void HTMLElement::UpdateFormOwner() {
  MOZ_ASSERT(IsFormAssociatedElement());
  MOZ_ASSERT(GetCustomElementData());
  MOZ_ASSERT(GetCustomElementData()->mState ==
             CustomElementData::State::eCustom);

  // If @form is set, the element *has* to be in a composed document,
  // otherwise it wouldn't be possible to find an element with the
  // corresponding id. If @form isn't set, the element *has* to have a parent,
  // otherwise it wouldn't be possible to find a form ancestor. We should not
  // call UpdateFormOwner if none of these conditions are fulfilled.
  if (HasAttr(nsGkAtoms::form) ? IsInComposedDoc() : !!GetParent()) {
    UpdateFormOwner(true, nullptr);
  }
  UpdateFieldSet(true);
  UpdateDisabledState(true);
  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(true);

  MaybeRestoreFormAssociatedCustomElementState();
}

bool HTMLElement::IsDisabledForEvents(WidgetEvent* aEvent) {
  if (IsFormAssociatedElement()) {
    return IsElementDisabledForEvents(aEvent, GetPrimaryFrame());
  }

  return false;
}

void HTMLElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                               const nsAttrValue* aValue,
                               const nsAttrValue* aOldValue,
                               nsIPrincipal* aMaybeScriptedPrincipal,
                               bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aName == nsGkAtoms::disabled || aName == nsGkAtoms::readonly)) {
    if (aName == nsGkAtoms::disabled) {
      // This *has* to be called *before* validity state check because
      // UpdateBarredFromConstraintValidation depend on our disabled state.
      UpdateDisabledState(aNotify);
    }
    if (aName == nsGkAtoms::readonly && !!aValue != !!aOldValue) {
      UpdateReadOnlyState(aNotify);
    }
    UpdateBarredFromConstraintValidation();
    UpdateValidityElementStates(aNotify);
  }

  return nsGenericHTMLFormElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

void HTMLElement::UpdateValidityElementStates(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::VALIDITY_STATES);
  ElementInternals* internals = GetElementInternals();
  if (!internals || !internals->IsCandidateForConstraintValidation()) {
    return;
  }
  if (internals->IsValid()) {
    AddStatesSilently(ElementState::VALID | ElementState::USER_VALID);
  } else {
    AddStatesSilently(ElementState::INVALID | ElementState::USER_INVALID);
  }
}

void HTMLElement::SetFormInternal(HTMLFormElement* aForm, bool aBindToTree) {
  ElementInternals* internals = GetElementInternals();
  MOZ_ASSERT(internals);
  internals->SetForm(aForm);
}

HTMLFormElement* HTMLElement::GetFormInternal() const {
  ElementInternals* internals = GetElementInternals();
  MOZ_ASSERT(internals);
  return internals->GetFormInternal();
}

void HTMLElement::SetFieldSetInternal(HTMLFieldSetElement* aFieldset) {
  ElementInternals* internals = GetElementInternals();
  MOZ_ASSERT(internals);
  internals->SetFieldSet(aFieldset);
}

HTMLFieldSetElement* HTMLElement::GetFieldSetInternal() const {
  ElementInternals* internals = GetElementInternals();
  MOZ_ASSERT(internals);
  return internals->GetFieldSet();
}

bool HTMLElement::CanBeDisabled() const { return IsFormAssociatedElement(); }

void HTMLElement::UpdateDisabledState(bool aNotify) {
  bool oldState = IsDisabled();
  nsGenericHTMLFormElement::UpdateDisabledState(aNotify);
  if (oldState != IsDisabled()) {
    MOZ_ASSERT(IsFormAssociatedElement());
    LifecycleCallbackArgs args;
    args.mDisabled = !oldState;
    nsContentUtils::EnqueueLifecycleCallback(ElementCallbackType::eFormDisabled,
                                             this, args);
  }
}

void HTMLElement::UpdateFormOwner(bool aBindToTree, Element* aFormIdElement) {
  MOZ_ASSERT(IsFormAssociatedElement());

  CustomElementData* data = GetCustomElementData();
  if (data->mState != CustomElementData::State::eCustom) {
    return;
  }

  HTMLFormElement* oldForm = GetFormInternal();
  nsGenericHTMLFormElement::UpdateFormOwner(aBindToTree, aFormIdElement);
  HTMLFormElement* newForm = GetFormInternal();
  if (newForm != oldForm) {
    LifecycleCallbackArgs args;
    args.mForm = newForm;
    nsContentUtils::EnqueueLifecycleCallback(
        ElementCallbackType::eFormAssociated, this, args);
  }
}

bool HTMLElement::IsFormAssociatedElement() const {
  return IsFormAssociatedCustomElement();
}

void HTMLElement::FieldSetDisabledChanged(bool aNotify) {
  // This *has* to be called *before* UpdateBarredFromConstraintValidation
  // because this function depend on our disabled state.
  nsGenericHTMLFormElement::FieldSetDisabledChanged(aNotify);

  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(aNotify);
}

ElementInternals* HTMLElement::GetElementInternals() const {
  CustomElementData* data = GetCustomElementData();
  if (!data || !data->IsFormAssociated()) {
    // If the element is not a form associated custom element, it should not be
    // able to be QueryInterfaced to nsIFormControl and could not perform
    // the form operation, either, so we return nullptr here.
    return nullptr;
  }

  return data->GetElementInternals();
}

nsIFormControl* HTMLElement::GetAsFormControl() {
  return GetElementInternals();
}

const nsIFormControl* HTMLElement::GetAsFormControl() const {
  return GetElementInternals();
}

void HTMLElement::UpdateBarredFromConstraintValidation() {
  CustomElementData* data = GetCustomElementData();
  if (data && data->IsFormAssociated()) {
    ElementInternals* internals = data->GetElementInternals();
    MOZ_ASSERT(internals);
    internals->UpdateBarredFromConstraintValidation();
  }
}

}  // namespace mozilla::dom

// Here, we expand 'NS_IMPL_NS_NEW_HTML_ELEMENT()' by hand.
// (Calling the macro directly (with no args) produces compiler warnings.)
nsGenericHTMLElement* NS_NewHTMLElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
    mozilla::dom::FromParser aFromParser) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  return new (nim) mozilla::dom::HTMLElement(nodeInfo.forget(), aFromParser);
}
