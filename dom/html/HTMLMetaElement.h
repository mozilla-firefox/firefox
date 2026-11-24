/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLMetaElement_h
#define mozilla_dom_HTMLMetaElement_h

#include "nsGenericHTMLElement.h"

namespace mozilla::dom {

class HTMLMetaElement final : public nsGenericHTMLElement {
 public:
  explicit HTMLMetaElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  // nsISupports
  NS_INLINE_DECL_REFCOUNTING_INHERITED(HTMLMetaElement, nsGenericHTMLElement)

  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;

  void CreateAndDispatchEvent(Document&, const nsAString& aEventName);

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  void GetName(nsAString& aValue) { GetHTMLAttr(nsGkAtoms::name, aValue); }
  void SetName(const nsAString& aName, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::name, aName, aRv);
  }
  void GetHttpEquiv(nsAString& aValue) {
    GetHTMLAttr(nsGkAtoms::httpEquiv, aValue);
  }
  void SetHttpEquiv(const nsAString& aHttpEquiv, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::httpEquiv, aHttpEquiv, aRv);
  }
  void GetContent(nsAString& aValue) {
    GetHTMLAttr(nsGkAtoms::content, aValue);
  }
  void SetContent(const nsAString& aContent, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::content, aContent, aRv);
  }
  void GetScheme(nsAString& aValue) { GetHTMLAttr(nsGkAtoms::scheme, aValue); }
  void SetScheme(const nsAString& aScheme, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::scheme, aScheme, aRv);
  }
  void GetMedia(nsAString& aValue) { GetHTMLAttr(nsGkAtoms::media, aValue); }
  void SetMedia(const nsAString& aMedia, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::media, aMedia, aRv);
  }

  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~HTMLMetaElement();

 private:
  enum class ChangeKind : uint8_t { TreeChange, NameChange, ContentChange };
  void MetaRemoved(Document& aDoc, const nsAttrValue& aName,
                   ChangeKind aChangeKind);
  void MetaAddedOrChanged(Document& aDoc, const nsAttrValue& aName,
                          ChangeKind aChangeKind);
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_HTMLMetaElement_h
