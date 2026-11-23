/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLSummaryElement_h
#define mozilla_dom_HTMLSummaryElement_h

#include "nsGenericHTMLElement.h"

namespace mozilla::dom {
class HTMLDetailsElement;

// HTMLSummaryElement implements the <summary> tag, which is used as a summary
// or legend of the <details> tag. Please see the spec for more information.
// https://html.spec.whatwg.org/multipage/forms.html#the-details-element
//
class HTMLSummaryElement final : public nsGenericHTMLElement {
 public:
  using NodeInfo = mozilla::dom::NodeInfo;

  explicit HTMLSummaryElement(already_AddRefed<NodeInfo> aNodeInfo)
      : nsGenericHTMLElement(std::move(aNodeInfo)) {}

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLSummaryElement, summary)

  nsresult Clone(NodeInfo*, nsINode** aResult) const override;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  nsresult PostHandleEvent(EventChainPostVisitor& aVisitor) override;

  bool IsHTMLFocusable(IsFocusableFlags, bool* aIsFocusable,
                       int32_t* aTabIndex) override;

  int32_t TabIndexDefault() override;

  // Return true if this is the first summary element child of a details or the
  // default summary element.
  bool IsMainSummary() const;

  // Return the details element which contains this summary. Otherwise return
  // nullptr if there is no such details element.
  HTMLDetailsElement* GetDetails() const;

 protected:
  virtual ~HTMLSummaryElement();

  JSObject* WrapNode(JSContext* aCx,
                     JS::Handle<JSObject*> aGivenProto) override;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_HTMLSummaryElement_h */
