/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLOptionElement.h"

#include "BindContext.h"
#include "HTMLOptGroupElement.h"
#include "mozilla/dom/HTMLOptionElementBinding.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "nsGkAtoms.h"
#include "nsIFormControl.h"

// Notify/query select frame for selected state
#include "mozilla/dom/Document.h"
#include "nsCOMPtr.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsNodeInfoManager.h"
#include "nsTextNode.h"

/**
 * Implementation of &lt;option&gt;
 */

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Option)

namespace mozilla::dom {

HTMLOptionElement::HTMLOptionElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo, FromParser aFromParser)
    : nsGenericHTMLElement(std::move(aNodeInfo)),
      mIsDoneAddingChildren(!aFromParser) {
  // We start off enabled
  AddStatesSilently(ElementState::ENABLED);
}

HTMLOptionElement::~HTMLOptionElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLOptionElement)

mozilla::dom::Element* HTMLOptionElement::GetFormForBindings() {
  HTMLFormElement* form = GetFormInternal();
  return RetargetReferenceTargetForBindings(form);
}

mozilla::dom::HTMLFormElement* HTMLOptionElement::GetFormInternal() {
  HTMLSelectElement* selectControl = GetSelect();
  return selectControl ? selectControl->GetFormInternal() : nullptr;
}

void HTMLOptionElement::SetSelectedInternal(bool aValue, bool aNotify) {
  mSelectedChanged = true;
  SetStates(ElementState::CHECKED, aValue, aNotify);
}

void HTMLOptionElement::OptGroupDisabledChanged(bool aNotify) {
  UpdateDisabledState(aNotify);
}

void HTMLOptionElement::UpdateDisabledState(bool aNotify) {
  bool isDisabled = HasAttr(nsGkAtoms::disabled);

  if (!isDisabled) {
    // https://html.spec.whatwg.org/#concept-option-disabled
    if (auto* optgroup =
            HTMLSelectElement::ComputeNearestAncestors(*this).mOptGroup) {
      isDisabled = optgroup->IsDisabled();
    }
  }

  ElementState disabledStates;
  if (isDisabled) {
    disabledStates |= ElementState::DISABLED;
  } else {
    disabledStates |= ElementState::ENABLED;
  }

  ElementState oldDisabledStates = State() & ElementState::DISABLED_STATES;
  ElementState changedStates = disabledStates ^ oldDisabledStates;

  if (!changedStates.IsEmpty()) {
    ToggleStates(changedStates, aNotify);
  }
}

void HTMLOptionElement::SetSelected(bool aValue) {
  // Note: The select content obj maintains all the PresState
  // so defer to it to get the answer
  if (HTMLSelectElement* select = GetSelect()) {
    nsAutoScriptBlocker scriptBlocker;
    int32_t index = Index();
    HTMLSelectElement::OptionFlags mask{
        HTMLSelectElement::OptionFlag::SetDisabled,
        HTMLSelectElement::OptionFlag::Notify};
    if (aValue) {
      mask += HTMLSelectElement::OptionFlag::IsSelected;
    }

    // This should end up calling SetSelectedInternal
    if (select->SetOptionsSelectedByIndex(index, index, mask)) {
      // https://html.spec.whatwg.org/#dom-option-selected
      // On setting, ... then ask for a reset given this, which runs the
      // selectedness setting algorithm on the cached nearest ancestor select
      // with skipSelectedcontentUpdate set to false. SetOptionsSelectedByIndex
      // does not always call RunSelectednessSettingAlgorithm (e.g. when only
      // selection, not deselection, occurs), so the selectedcontent update may
      // be missed; schedule it explicitly here. Synchronous, matching
      // select.value/select.selectedIndex.
      select->ScheduleSelectedContentUpdate(
          SelectedContentUpdateMode::ScriptRunner,
          /* aForceUpdate = */ true);
    }
  } else {
    SetSelectedInternal(aValue, true);
  }
}

int32_t HTMLOptionElement::Index() {
  static int32_t defaultIndex = 0;

  // Only select elements can contain a list of options.
  HTMLSelectElement* selectElement = GetSelect();
  if (!selectElement) {
    return defaultIndex;
  }

  HTMLOptionsCollection* options = selectElement->GetOptions();
  if (!options) {
    return defaultIndex;
  }

  nsAutoScriptBlocker scriptBlocker;
  int32_t index = defaultIndex;
  MOZ_ALWAYS_SUCCEEDS(options->GetOptionIndex(this, 0, true, &index));
  return index;
}

nsChangeHint HTMLOptionElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  nsChangeHint retval =
      nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);

  if (aAttribute == nsGkAtoms::label) {
    retval |= nsChangeHint_ReconstructFrame;
  } else if (aAttribute == nsGkAtoms::text) {
    retval |= NS_STYLE_HINT_REFLOW;
  }
  return retval;
}

void HTMLOptionElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                      const nsAttrValue* aValue, bool aNotify) {
  nsGenericHTMLElement::BeforeSetAttr(aNamespaceID, aName, aValue, aNotify);

  if (aNamespaceID != kNameSpaceID_None || aName != nsGkAtoms::selected ||
      mSelectedChanged) {
    return;
  }

  // We just changed out selected state (since we look at the "selected"
  // attribute when mSelectedChanged is false).  Let's tell our select about
  // it.
  HTMLSelectElement* select = GetSelect();
  if (!select) {
    // If option is a child of select, SetOptionsSelectedByIndex will set the
    // selected state if needed.
    // Keep in sync with Element::SetNoNameSpaceAttrOnNewlyCreatedElement!
    SetStates(ElementState::CHECKED, !!aValue, aNotify);
    return;
  }

  NS_ASSERTION(!mSelectedChanged, "Shouldn't be here");

  int32_t index = Index();
  HTMLSelectElement::OptionFlags mask =
      HTMLSelectElement::OptionFlag::SetDisabled;
  if (aValue) {
    mask += HTMLSelectElement::OptionFlag::IsSelected;
  }

  if (aNotify) {
    mask += HTMLSelectElement::OptionFlag::Notify;
  }

  // This can end up calling SetSelectedInternal if our selected state needs to
  // change, which we will allow to take effect so that parts of
  // SetOptionsSelectedByIndex that might depend on it working don't get
  // confused.
  if (select->SetOptionsSelectedByIndex(index, index, mask)) {
    // https://html.spec.whatwg.org/#ask-for-a-reset
    // Same "ask for a reset" step as SetSelected above, triggered here by the
    // selected content attribute instead of the selected IDL attribute.
    select->ScheduleSelectedContentUpdate(
        SelectedContentUpdateMode::ScriptRunner,
        /* aForceUpdate = */ true);
  }

  // the selected state might have been changed by SetOptionsSelectedByIndex,
  // possibly more than once; make sure our mSelectedChanged state is set back
  // correctly.
  mSelectedChanged = false;
}

void HTMLOptionElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::disabled) {
      UpdateDisabledState(aNotify);
    }

    if (aName == nsGkAtoms::value && Selected()) {
      // Since this option is selected, changing value may have changed missing
      // validity state of the select element
      if (HTMLSelectElement* select = GetSelect()) {
        select->UpdateValueMissingValidityState();
      }
    }

    if (aName == nsGkAtoms::selected) {
      SetStates(ElementState::DEFAULT, !!aValue, aNotify);
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void HTMLOptionElement::GetText(nsAString& aText) {
  nsAutoString text;

  nsIContent* child = nsINode::GetFirstChild();
  while (child) {
    if (Text* textChild = child->GetAsText()) {
      textChild->AppendTextTo(text);
    }
    if (child->IsHTMLElement(nsGkAtoms::script) ||
        child->IsSVGElement(nsGkAtoms::script)) {
      child = child->GetNextNonChildNode(this);
    } else {
      child = child->GetNextNode(this);
    }
  }

  // XXX No CompressWhitespace for nsAString.  Sad.
  text.CompressWhitespace(true, true);
  aText = std::move(text);
}

void HTMLOptionElement::SetText(const nsAString& aText, ErrorResult& aRv) {
  aRv = nsContentUtils::SetNodeTextContent(this, aText, false);
}

nsresult HTMLOptionElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  MOZ_TRY(nsGenericHTMLElement::BindToTree(aContext, aParent));

  // Our new parent might change :disabled/:enabled state.
  UpdateDisabledState(false);

  // https://html.spec.whatwg.org/#the-option-element
  // The option HTML element insertion steps, given insertedOption, are to run
  // update an option's nearest ancestor select given insertedOption.
  UpdateNearestAncestorSelect();

  // The option HTML element post-connection steps, given insertedOption:
  // 1. If insertedOption's cached nearest ancestor select element is not null
  // and insertedOption is selected, then update select's descendant
  // selectedcontent elements given insertedOption's cached nearest ancestor
  // select element.
  //
  // NOTE: Post-connection steps only run when connecting to a composed doc,
  // unlike insertion steps above which run for any tree insertion.
  if (aContext.InComposedDoc() && mCachedNearestAncestorSelect &&
      mIsDoneAddingChildren && Selected()) {
    mCachedNearestAncestorSelect->ScheduleSelectedContentUpdate(
        SelectedContentUpdateMode::ScriptRunner);
  }

  return NS_OK;
}

void HTMLOptionElement::DoneAddingChildren(bool) {
  MOZ_ASSERT(!mIsDoneAddingChildren);
  mIsDoneAddingChildren = true;

  // https://html.spec.whatwg.org/#the-option-element:stack-of-open-elements
  //
  // When an option element option is popped off the stack of open elements of
  // an HTML parser or XML parser, update descendant selectedcontent elements
  // for an option given option.
  if (mCachedNearestAncestorSelect && Selected() && IsInComposedDoc()) {
    mCachedNearestAncestorSelect->ScheduleSelectedContentUpdate(
        SelectedContentUpdateMode::ScriptRunner);
  }
}

void HTMLOptionElement::UnbindFromTree(UnbindContext& aContext) {
  // https://html.spec.whatwg.org/#the-option-element
  // The option HTML element removing steps, given removedOption and oldParent:
  //
  // 1. Let select be removedOption's cached nearest ancestor select element.
  RefPtr<HTMLSelectElement> oldSelect = mCachedNearestAncestorSelect;

  nsGenericHTMLElement::UnbindFromTree(aContext);

  // 3. Run update an option's nearest ancestor select given removedOption.
  UpdateNearestAncestorSelect();

  // 2. If removedOption is selected and select is not null and select has at
  //    least one selectedcontent element descendant, then queue a microtask to
  //    update a select's descendant selectedcontent elements given select.
  // NOTE: omitting the "has selectedcontent descendant" check for now.
  if (oldSelect && oldSelect != mCachedNearestAncestorSelect && Selected()) {
    oldSelect->ScheduleSelectedContentUpdate();
  }

  UpdateDisabledState(false);
}

// https://html.spec.whatwg.org/#update-an-options-nearest-ancestor-select
void HTMLOptionElement::UpdateNearestAncestorSelect() {
  // 1. Let oldSelect be option's cached nearest ancestor select element.
  // 2. Let newSelect be option's option element nearest ancestor select.
  // 3. Set option's cached nearest ancestor select element to newSelect.
  mCachedNearestAncestorSelect =
      HTMLSelectElement::ComputeNearestAncestors(*this).mSelect;
  // 4. If oldSelect is not newSelect:
  //    4.1/4.2: Run the selectedness setting algorithm on old/new select.
  //    NOTE: Deferred to HTMLSelectElement's mutation observer callbacks
  //    (ContentAppendedOrInserted / ContentWillBeRemoved) which run once
  //    per DOM mutation.
}

// Returns this option's nearest ancestor select element (cached), or null.
HTMLSelectElement* HTMLOptionElement::GetSelect() const {
  return mCachedNearestAncestorSelect;
}

already_AddRefed<HTMLOptionElement> HTMLOptionElement::Option(
    const GlobalObject& aGlobal, const nsAString& aText,
    const Optional<nsAString>& aValue, bool aDefaultSelected, bool aSelected,
    ErrorResult& aError) {
  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(aGlobal.GetAsSupports());
  Document* doc;
  if (!win || !(doc = win->GetExtantDoc())) {
    aError.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo = doc->NodeInfoManager()->GetNodeInfo(
      nsGkAtoms::option, nullptr, kNameSpaceID_XHTML, ELEMENT_NODE);

  auto* nim = nodeInfo->NodeInfoManager();
  RefPtr<HTMLOptionElement> option =
      new (nim) HTMLOptionElement(nodeInfo.forget());

  if (!aText.IsEmpty()) {
    // Create a new text node and append it to the option
    RefPtr<nsTextNode> textContent = new (option->NodeInfo()->NodeInfoManager())
        nsTextNode(option->NodeInfo()->NodeInfoManager());

    textContent->SetText(aText, false);

    option->AppendChildTo(textContent, false, aError);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  if (aValue.WasPassed()) {
    // Set the value attribute for this element. We're calling SetAttr
    // directly because we want to pass aNotify == false.
    aError = option->SetAttr(kNameSpaceID_None, nsGkAtoms::value,
                             aValue.Value(), false, IsKnownNewAttr::Yes);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  if (aDefaultSelected) {
    // We're calling SetAttr directly because we want to pass
    // aNotify == false.
    aError = option->SetAttr(kNameSpaceID_None, nsGkAtoms::selected, u""_ns,
                             false, IsKnownNewAttr::Yes);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  option->SetSelected(aSelected);
  option->SetSelectedChanged(false);

  return option.forget();
}

nsresult HTMLOptionElement::CopyInnerTo(Element* aDest) {
  nsresult rv = nsGenericHTMLElement::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aDest->OwnerDoc()->IsStaticDocument()) {
    static_cast<HTMLOptionElement*>(aDest)->SetSelected(Selected());
  }
  return NS_OK;
}

JSObject* HTMLOptionElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return HTMLOptionElement_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
