/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLButtonElement_h
#define mozilla_dom_HTMLButtonElement_h

#include "mozilla/Attributes.h"
#include "mozilla/dom/ConstraintValidation.h"
#include "nsGenericHTMLElement.h"

namespace mozilla {
class EventChainPostVisitor;
class EventChainPreVisitor;
namespace dom {
class FormData;

class HTMLButtonElement final : public nsGenericHTMLFormControlElementWithState,
                                public ConstraintValidation {
 public:
  using ConstraintValidation::GetValidationMessage;

  explicit HTMLButtonElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo,
                             FromParser aFromParser = NOT_FROM_PARSER);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(
      HTMLButtonElement, nsGenericHTMLFormControlElementWithState)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  int32_t TabIndexDefault() override;

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLButtonElement, button)

  // Element
  bool IsInteractiveHTMLContent() const override { return true; }

  // nsGenericHTMLFormElement
  void SaveState() override;
  bool RestoreState(PresState* aState) override;

  // overriden nsIFormControl methods
  NS_IMETHOD Reset() override;
  NS_IMETHOD SubmitNamesValues(FormData* aFormData) override;

  void FieldSetDisabledChanged(bool aNotify) override;

  // EventTarget
  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult PostHandleEvent(EventChainPostVisitor& aVisitor) override;
  void LegacyPreActivationBehavior(EventChainVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT
  void ActivationBehavior(EventChainPostVisitor& aVisitor) override;
  void LegacyCanceledActivationBehavior(
      EventChainPostVisitor& aVisitor) override;

  // nsINode
  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;
  JSObject* WrapNode(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  // nsIContent
  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;
  void DoneCreatingElement() override;

  void UpdateBarredFromConstraintValidation();
  void UpdateValidityElementStates(bool aNotify);
  /**
   * Called when an attribute is about to be changed
   */
  void BeforeSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;
  /**
   * Called when an attribute has just been changed
   */
  void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  // nsGenericHTMLElement
  bool IsHTMLFocusable(IsFocusableFlags, bool* aIsFocusable,
                       int32_t* aTabIndex) override;
  bool IsDisabledForEvents(WidgetEvent* aEvent) override;

  // WebIDL
  bool Disabled() const { return GetBoolAttr(nsGkAtoms::disabled); }
  void SetDisabled(bool aDisabled, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::disabled, aDisabled, aError);
  }
  // GetFormAction implemented in superclass
  void SetFormAction(const nsAString& aFormAction, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::formaction, aFormAction, aRv);
  }
  void GetFormEnctype(nsAString& aFormEncType);
  void SetFormEnctype(const nsAString& aFormEnctype, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::formenctype, aFormEnctype, aRv);
  }
  void GetFormMethod(nsAString& aFormMethod);
  void SetFormMethod(const nsAString& aFormMethod, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::formmethod, aFormMethod, aRv);
  }
  bool FormNoValidate() const { return GetBoolAttr(nsGkAtoms::formnovalidate); }
  void SetFormNoValidate(bool aFormNoValidate, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::formnovalidate, aFormNoValidate, aError);
  }
  void GetFormTarget(DOMString& aFormTarget) {
    GetHTMLAttr(nsGkAtoms::formtarget, aFormTarget);
  }
  void SetFormTarget(const nsAString& aFormTarget, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::formtarget, aFormTarget, aRv);
  }
  void GetName(DOMString& aName) { GetHTMLAttr(nsGkAtoms::name, aName); }
  void SetName(const nsAString& aName, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::name, aName, aRv);
  }
  void GetType(nsAString& aType);
  void SetType(const nsAString& aType, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::type, aType, aRv);
  }
  void GetValue(nsAString& aValue) { GetHTMLAttr(nsGkAtoms::value, aValue); }
  void SetValue(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::value, aValue, aRv);
  }

  // Override SetCustomValidity so we update our state properly when it's called
  // via bindings.
  void SetCustomValidity(const nsAString& aError);

  // Command & CommandFor
  Element* GetCommandForElementForBindings() const;
  Element* GetCommandForElementInternal() const;
  void SetCommandForElementForBindings(Element*);
  void GetCommand(nsAString& aCommand) const;
  Element::Command GetCommand() const;
  void SetCommand(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::command, aValue);
  }

 protected:
  virtual ~HTMLButtonElement();

  bool InAutoState() const;
  // aParent overrides the parent node used to decide whether this button is a
  // select's button; when null the current parent node is used.
  const nsAttrValue::EnumTableEntry* ResolveAutoState(
      const nsINode* aParent = nullptr) const;

  bool mDisabledChanged : 1;
  bool mInInternalActivate : 1;
  bool mInhibitStateRestoration : 1;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_HTMLButtonElement_h
