/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Declaration of HTML <label> elements.
 */
#ifndef HTMLLabelElement_h
#define HTMLLabelElement_h

#include "mozilla/Attributes.h"
#include "nsGenericHTMLElement.h"

namespace mozilla {
class EventChainPostVisitor;
namespace dom {

class HTMLLabelElement final : public nsGenericHTMLElement {
 public:
  explicit HTMLLabelElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
      : nsGenericHTMLElement(std::move(aNodeInfo)), mHandlingEvent(false) {}

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLLabelElement, label)

  // nsISupports
  NS_INLINE_DECL_REFCOUNTING_INHERITED(HTMLLabelElement, nsGenericHTMLElement)

  // Element
  virtual bool IsInteractiveHTMLContent() const override { return true; }

  Element* GetFormForBindings() const;
  HTMLFormElement* GetFormInternal() const;
  void GetHtmlFor(nsString& aHtmlFor) {
    GetHTMLAttr(nsGkAtoms::_for, aHtmlFor);
  }
  void SetHtmlFor(const nsAString& aHtmlFor) {
    SetHTMLAttr(nsGkAtoms::_for, aHtmlFor);
  }
  nsGenericHTMLElement* GetControlForBindings() const;

  nsChangeHint GetAttributeChangeHint(const nsAtom* aAttribute,
                                      AttrModType aModType) const override;

  using nsGenericHTMLElement::Focus;
  virtual void Focus(const FocusOptions& aOptions,
                     const mozilla::dom::CallerType aCallerType,
                     ErrorResult& aError) override;

  // nsIContent
  virtual void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual nsresult PostHandleEvent(EventChainPostVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT
  virtual Result<bool, nsresult> PerformAccesskey(
      bool aKeyCausesActivation, bool aIsTrustedEvent) override;
  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  nsGenericHTMLElement* GetLabeledElementInternal() const;

 protected:
  virtual ~HTMLLabelElement();

  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

  nsGenericHTMLElement* GetFirstLabelableDescendant() const;

  // XXX It would be nice if we could use an event flag instead.
  bool mHandlingEvent;
};

}  // namespace dom
}  // namespace mozilla

#endif /* HTMLLabelElement_h */
