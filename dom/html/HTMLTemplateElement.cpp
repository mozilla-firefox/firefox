/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLTemplateElement.h"

#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLTemplateElementBinding.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "nsAtom.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Template)

namespace mozilla::dom {

static constexpr nsAttrValue::EnumTableEntry kShadowRootModeTable[] = {
    {"open", ShadowRootMode::Open},
    {"closed", ShadowRootMode::Closed},
};

static constexpr nsAttrValue::EnumTableEntry kSlotAssignmentTable[] = {
    {"named", SlotAssignmentMode::Named},
    {"manual", SlotAssignmentMode::Manual},
};

static constexpr const nsAttrValue::EnumTableEntry* kSlotAssignmentDefault =
    &kSlotAssignmentTable[0];

HTMLTemplateElement::HTMLTemplateElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {
  SetHasWeirdParserInsertionMode();

  Document* contentsOwner = OwnerDoc()->GetTemplateContentsOwner();
  if (!contentsOwner) {
    MOZ_CRASH("There should always be a template contents owner.");
  }

  mContent = contentsOwner->CreateDocumentFragment();
  mContent->SetHost(this);
}

HTMLTemplateElement::~HTMLTemplateElement() {
  if (mContent && mContent->GetHost() == this) {
    mContent->SetHost(nullptr);
  }
}

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLTemplateElement,
                                               nsGenericHTMLElement)

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLTemplateElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLTemplateElement,
                                                nsGenericHTMLElement)
  if (tmp->mContent) {
    if (tmp->mContent->GetHost() == tmp) {
      tmp->mContent->SetHost(nullptr);
    }
    tmp->mContent = nullptr;
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLTemplateElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ELEMENT_CLONE(HTMLTemplateElement)

JSObject* HTMLTemplateElement::WrapNode(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return HTMLTemplateElement_Binding::Wrap(aCx, this, aGivenProto);
}

void HTMLTemplateElement::GetShadowRootSlotAssignment(
    nsAString& aResult) const {
  GetEnumAttr(nsGkAtoms::shadowrootslotassignment, kSlotAssignmentDefault->tag,
              aResult);
}

void HTMLTemplateElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                       const nsAttrValue* aValue,
                                       const nsAttrValue* aOldValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None && aName == nsGkAtoms::shadowrootmode &&
      aValue && aValue->Type() == nsAttrValue::ValueType::eEnum &&
      !mShadowRootMode.isSome()) {
    mShadowRootMode.emplace(
        static_cast<ShadowRootMode>(aValue->GetEnumValue()));
  }

  nsGenericHTMLElement::AfterSetAttr(aNamespaceID, aName, aValue, aOldValue,
                                     aMaybeScriptedPrincipal, aNotify);
}

bool HTMLTemplateElement::ParseAttribute(int32_t aNamespaceID,
                                         nsAtom* aAttribute,
                                         const nsAString& aValue,
                                         nsIPrincipal* aMaybeScriptedPrincipal,
                                         nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::shadowrootmode) {
      return aResult.ParseEnumValue(aValue, kShadowRootModeTable, false,
                                    nullptr);
    }
    if (aAttribute == nsGkAtoms::shadowrootslotassignment) {
      return aResult.ParseEnumValue(aValue, kSlotAssignmentTable, false,
                                    kSlotAssignmentDefault);
    }
  }
  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLTemplateElement::SetHTML(const nsAString& aHTML,
                                  const SetHTMLOptions& aOptions,
                                  ErrorResult& aError) {
  RefPtr<DocumentFragment> content = mContent;
  nsContentUtils::SetHTML(content, this, aHTML, aOptions, aError);
}

/* https://html.spec.whatwg.org/#dom-element-sethtmlunsafe */
void HTMLTemplateElement::SetHTMLUnsafe(const TrustedHTMLOrString& aHTML,
                                        const SetHTMLUnsafeOptions& aOptions,
                                        nsIPrincipal* aSubjectPrincipal,
                                        ErrorResult& aError) {
  RefPtr<DocumentFragment> content = mContent;
  nsContentUtils::SetHTMLUnsafe(content, this, aHTML, aOptions,
                                false /*aIsShadowRoot*/, aSubjectPrincipal,
                                aError);
}

}  // namespace mozilla::dom
