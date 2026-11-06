/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLTemplateElement_h
#define mozilla_dom_HTMLTemplateElement_h

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/ShadowRootBinding.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"

namespace mozilla::dom {

class HTMLTemplateElement final : public nsGenericHTMLElement {
 public:
  explicit HTMLTemplateElement(
      already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLTemplateElement, _template);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLTemplateElement,
                                           nsGenericHTMLElement)

  void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  DocumentFragment* Content() { return mContent; }
  void SetContent(DocumentFragment* aContent) { mContent = aContent; }

  void GetHtmlFor(nsAString& aHtmlFor) {
    GetHTMLAttr(nsGkAtoms::_for, aHtmlFor);
  }
  void SetHtmlFor(const nsAString& aHtmlFor) {
    SetHTMLAttr(nsGkAtoms::_for, aHtmlFor);
  }

  void GetShadowRootMode(nsAString& aResult) const {
    GetEnumAttr(nsGkAtoms::shadowrootmode, nullptr, aResult);
  }
  void SetShadowRootMode(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::shadowrootmode, aValue);
  }

  bool ShadowRootDelegatesFocus() const {
    return GetBoolAttr(nsGkAtoms::shadowrootdelegatesfocus);
  }
  void SetShadowRootDelegatesFocus(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::shadowrootdelegatesfocus, aValue, aRv);
  }

  bool ShadowRootClonable() const {
    return GetBoolAttr(nsGkAtoms::shadowrootclonable);
  }
  void SetShadowRootClonable(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::shadowrootclonable, aValue, aRv);
  }

  bool ShadowRootSerializable() const {
    return GetBoolAttr(nsGkAtoms::shadowrootserializable);
  }
  void SetShadowRootSerializable(bool aValue, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::shadowrootserializable, aValue, aRv);
  }

  void GetShadowRootCustomElementRegistry(nsAString& aResult) const {
    GetHTMLAttr(nsGkAtoms::shadowrootcustomelementregistry, aResult);
  }
  void SetShadowRootCustomElementRegistry(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::shadowrootcustomelementregistry, aValue);
  }

  REFLECT_NULLABLE_DOMSTRING_ATTR(ShadowRootReferenceTarget,
                                  shadowrootreferencetarget)

  void GetShadowRootSlotAssignment(nsAString& aResult) const;
  void SetShadowRootSlotAssignment(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::shadowrootslotassignment, aValue);
  }

  void SetHTML(const nsAString& aInnerHTML, const SetHTMLOptions& aOptions,
               ErrorResult& aError) final;

  MOZ_CAN_RUN_SCRIPT
  void SetHTMLUnsafe(const TrustedHTMLOrString& aHTML,
                     const SetHTMLUnsafeOptions& aOptions,
                     nsIPrincipal* aSubjectPrincipal,
                     ErrorResult& aError) final;

 protected:
  virtual ~HTMLTemplateElement();

  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

  RefPtr<DocumentFragment> mContent;
  Maybe<ShadowRootMode> mShadowRootMode;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_HTMLTemplateElement_h
