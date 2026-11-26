/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLOptGroupElement.h"

#include "mozilla/EventDispatcher.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLOptGroupElementBinding.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/Text.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsTextNode.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(OptGroup)

namespace mozilla::dom {

/**
 * The implementation of &lt;optgroup&gt;
 */

HTMLOptGroupElement::HTMLOptGroupElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {
  // We start off enabled
  AddStatesSilently(ElementState::ENABLED);
}

nsresult HTMLOptGroupElement::BindToTree(BindContext& aContext,
                                         nsINode& aParent) {
  MOZ_TRY(nsGenericHTMLElement::BindToTree(aContext, aParent));
  if (IsInComposedDoc() && !GetShadowRoot()) {
    SetupShadowTree();
  }
  return NS_OK;
}

// https://html.spec.whatwg.org/#the-select-element-2:the-optgroup-element-5
void HTMLOptGroupElement::SetupShadowTree() {
  AttachAndSetUAShadowRoot(NotifyUAWidget::No, DelegatesFocus::No,
                           CustomSlotDispatch::Yes);
  RefPtr<ShadowRoot> sr = GetShadowRoot();
  if (!sr) {
    return;
  }
  sr->AppendBuiltInStyleSheet(BuiltInStyleSheet::OptGroup);
  Document* doc = OwnerDoc();
  // An optgroup legend slot, which takes the first child element if it is a
  // legend.
  RefPtr legendSlot = doc->CreateHTMLElement(nsGkAtoms::slot);
  legendSlot->SetAttr(kNameSpaceID_None, nsGkAtoms::name,
                      u"internal-optgroup-legend"_ns, false,
                      IsKnownNewAttr::Yes);
  // An optgroup label element, appended to the optgroup legend slot.
  RefPtr label = doc->CreateHTMLElement(nsGkAtoms::div);
  label->SetPseudoElementType(PseudoStyleType::MozOptgroupLabel);
  RefPtr text = doc->CreateEmptyTextNode();
  nsAutoString labelText;
  GetLabel(labelText);
  text->SetText(labelText, false);
  label->AppendChildTo(text, false, IgnoreErrors());
  legendSlot->AppendChildTo(label, false, IgnoreErrors());
  sr->AppendChildTo(legendSlot, false, IgnoreErrors());
  // An optgroup contents slot, which takes all remaining child nodes.
  RefPtr contentsSlot = doc->CreateHTMLElement(nsGkAtoms::slot);
  sr->AppendChildTo(contentsSlot, false, IgnoreErrors());
}

Text* HTMLOptGroupElement::GetLabelText() const {
  auto* sr = GetShadowRoot();
  if (!sr) {
    return nullptr;
  }
  nsINode* legendSlot = sr->GetFirstChild();
  nsINode* label = legendSlot ? legendSlot->GetFirstChild() : nullptr;
  return Text::FromNodeOrNull(label ? label->GetFirstChild() : nullptr);
}

void HTMLOptGroupElement::GetSlotNameFor(const ShadowRoot&,
                                         const nsIContent& aContent,
                                         nsAString& aName) const {
  if (aContent.IsHTMLElement(nsGkAtoms::legend) &&
      aContent.GetParent() == this && GetFirstElementChild() == &aContent) {
    aName.AssignLiteral("internal-optgroup-legend");
  }
}

void HTMLOptGroupElement::OnChildBeforeSlotted(ShadowRoot& aShadow,
                                               nsIContent& aChild) {
  if (!aChild.IsElement()) {
    return;
  }
  // An element inserted before the slotted legend stops it being the first
  // element child.
  HTMLSlotElement* slot =
      aShadow.GetFirstNamedSlot(u"internal-optgroup-legend"_ns);
  MOZ_RELEASE_ASSERT(slot);
  const auto& assigned = slot->AssignedNodes();
  if (!assigned.IsEmpty()) {
    aShadow.MaybeReassignContent(*assigned[0]->AsContent());
  }
}

void HTMLOptGroupElement::OnChildUnslotted(ShadowRoot& aShadow,
                                           nsIContent& aChild) {
  if (!aChild.IsElement() || MOZ_UNLIKELY(!aShadow.GetHost())) {
    return;
  }
  // Removing an element may make a legend the first element child.
  if (Element* first = GetFirstElementChild();
      first && first->IsHTMLElement(nsGkAtoms::legend)) {
    aShadow.MaybeReassignContent(*first);
  }
}

HTMLOptGroupElement::~HTMLOptGroupElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLOptGroupElement)

void HTMLOptGroupElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = false;
  nsGenericHTMLElement::GetEventTargetParent(aVisitor);
}

HTMLSelectElement* HTMLOptGroupElement::GetSelect() const {
  return HTMLSelectElement::FromNodeOrNull(GetParentNode());
}

void HTMLOptGroupElement::GetRenderedLabel(nsAString& aLabel) const {
  // https://html.spec.whatwg.org/#concept-optgroup-label
  // 1. A first element child legend takes precedence over the label attribute.
  if (const Element* first = GetFirstElementChild();
      first && first->IsHTMLElement(nsGkAtoms::legend)) {
    nsContentUtils::GetHTMLAwareTextContent(*first, AltText::Include, aLabel);
    return;
  }
  // 2-3. Return the label attribute, or the empty string if it is absent.
  GetHTMLAttr(nsGkAtoms::label, aLabel);
}

void HTMLOptGroupElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                       const nsAttrValue* aValue,
                                       const nsAttrValue* aOldValue,
                                       nsIPrincipal* aSubjectPrincipal,
                                       bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::label) {
    if (RefPtr<Text> text = GetLabelText()) {
      nsAutoString label;
      if (aValue) {
        aValue->ToString(label);
      }
      text->SetText(label, aNotify);
    }
  }

  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::disabled) {
    ElementState disabledStates;
    if (aValue) {
      disabledStates |= ElementState::DISABLED;
    } else {
      disabledStates |= ElementState::ENABLED;
    }

    ElementState oldDisabledStates = State() & ElementState::DISABLED_STATES;
    ElementState changedStates = disabledStates ^ oldDisabledStates;

    if (!changedStates.IsEmpty()) {
      ToggleStates(changedStates, aNotify);

      // Descendant options have their :disabled state depending on our
      // disabled attribute. Walk descendants, skipping boundary elements
      // whose children are not affected by this optgroup's disabled state.
      for (nsIContent* node = GetFirstChild(); node;) {
        if (auto* opt = HTMLOptionElement::FromNode(node)) {
          opt->OptGroupDisabledChanged(true);
          node = node->GetNextNonChildNode(this);
          continue;
        }
        if (HTMLSelectElement::IsOptionListBoundary(*node) ||
            node->IsHTMLElement(nsGkAtoms::optgroup)) {
          node = node->GetNextNonChildNode(this);
          continue;
        }
        node = node->GetNextNode(this);
      }
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

JSObject* HTMLOptGroupElement::WrapNode(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return HTMLOptGroupElement_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
