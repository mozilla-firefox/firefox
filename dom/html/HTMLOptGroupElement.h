/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLOptGroupElement_h
#define mozilla_dom_HTMLOptGroupElement_h

#include "nsGenericHTMLElement.h"

namespace mozilla {
class ErrorResult;
class EventChainPreVisitor;
namespace dom {

class HTMLOptGroupElement final : public nsGenericHTMLElement {
 public:
  explicit HTMLOptGroupElement(
      already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLOptGroupElement, optgroup)

  // nsISupports
  NS_INLINE_DECL_REFCOUNTING_INHERITED(HTMLOptGroupElement,
                                       nsGenericHTMLElement)

  // nsIContent
  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;

  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;

  bool Disabled() const { return GetBoolAttr(nsGkAtoms::disabled); }
  void SetDisabled(bool aValue, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::disabled, aValue, aError);
  }

  void GetLabel(nsAString& aValue) const {
    GetHTMLAttr(nsGkAtoms::label, aValue);
  }
  void SetLabel(const nsAString& aLabel, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::label, aLabel, aError);
  }

  // Get the select content element that contains this optgroup
  HTMLSelectElement* GetSelect() const;

 protected:
  virtual ~HTMLOptGroupElement();
  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_HTMLOptGroupElement_h */
