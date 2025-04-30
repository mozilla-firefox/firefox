/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsGenericHTMLElement_h___
#define nsGenericHTMLElement_h___

#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "nsNameSpaceManager.h"  // for kNameSpaceID_None
#include "nsIFormControl.h"
#include "nsGkAtoms.h"
#include "nsContentCreatorFunctions.h"
#include "nsStyledElement.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/HTMLElementBinding.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/ValidityState.h"
#include "mozilla/dom/PopoverData.h"
#include "mozilla/dom/ToggleEvent.h"

#include <cstdint>

class nsDOMTokenList;
class nsIFrame;
class nsILayoutHistoryState;
class nsIURI;
struct nsSize;

enum nsCSSPropertyID : int32_t;

namespace mozilla {
class EditorBase;
class ErrorResult;
class EventChainPostVisitor;
class EventChainPreVisitor;
class EventChainVisitor;
class EventListenerManager;
class PresState;
namespace dom {
class ElementInternals;
class HTMLFormElement;
enum class FetchPriority : uint8_t;
}  // namespace dom
}  // namespace mozilla

using nsGenericHTMLElementBase = nsStyledElement;

/**
 * A common superclass for HTML elements
 */
class nsGenericHTMLElement : public nsGenericHTMLElementBase {
 public:
  using ContentEditableState = mozilla::ContentEditableState;
  using Element::Focus;
  using Element::SetTabIndex;
  using InvokeAction = mozilla::dom::InvokeAction;

  explicit nsGenericHTMLElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : nsGenericHTMLElementBase(std::move(aNodeInfo)) {
    NS_ASSERTION(mNodeInfo->NamespaceID() == kNameSpaceID_XHTML,
                 "Unexpected namespace");
  }

  NS_INLINE_DECL_REFCOUNTING_INHERITED(nsGenericHTMLElement,
                                       nsGenericHTMLElementBase)

  NS_IMPL_FROMNODE(nsGenericHTMLElement, kNameSpaceID_XHTML)

  // From Element
  nsresult CopyInnerTo(mozilla::dom::Element* aDest);

  void GetTitle(mozilla::dom::DOMString& aTitle) {
    GetHTMLAttr(nsGkAtoms::title, aTitle);
  }
  void SetTitle(const nsAString& aTitle) {
    SetHTMLAttr(nsGkAtoms::title, aTitle);
  }
  void GetLang(mozilla::dom::DOMString& aLang) {
    GetHTMLAttr(nsGkAtoms::lang, aLang);
  }
  void SetLang(const nsAString& aLang) { SetHTMLAttr(nsGkAtoms::lang, aLang); }
  bool Translate() const override;
  void SetTranslate(bool aTranslate, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::translate, aTranslate ? u"yes"_ns : u"no"_ns,
                aError);
  }
  void GetDir(nsAString& aDir) { GetHTMLEnumAttr(nsGkAtoms::dir, aDir); }
  void SetDir(const nsAString& aDir, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::dir, aDir, aError);
  }
  void GetPopover(nsString& aPopover) const;
  void SetPopover(const nsAString& aPopover, mozilla::ErrorResult& aError) {
    SetOrRemoveNullableStringAttr(nsGkAtoms::popover, aPopover, aError);
  }

  void GetHidden(mozilla::dom::Nullable<
                 mozilla::dom::OwningBooleanOrUnrestrictedDoubleOrString>&
                     aHidden) const;

  void SetHidden(
      const mozilla::dom::Nullable<
          mozilla::dom::BooleanOrUnrestrictedDoubleOrString>& aHidden,
      mozilla::ErrorResult& aRv);

  bool Inert() const { return GetBoolAttr(nsGkAtoms::inert); }
  void SetInert(bool aInert, mozilla::ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::inert, aInert, aError);
  }
  MOZ_CAN_RUN_SCRIPT void Click(mozilla::dom::CallerType aCallerType);
  void GetAccessKey(nsString& aAccessKey) {
    GetHTMLAttr(nsGkAtoms::accesskey, aAccessKey);
  }
  void SetAccessKey(const nsAString& aAccessKey, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::accesskey, aAccessKey, aError);
  }
  void GetAccessKeyLabel(nsString& aAccessKeyLabel);
  virtual bool Draggable() const {
    return AttrValueIs(kNameSpaceID_None, nsGkAtoms::draggable,
                       nsGkAtoms::_true, eIgnoreCase);
  }
  void SetDraggable(bool aDraggable, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::draggable, aDraggable ? u"true"_ns : u"false"_ns,
                aError);
  }
  void GetContentEditable(nsString& aContentEditable) const {
    switch (GetContentEditableState()) {
      case ContentEditableState::True:
        aContentEditable.AssignLiteral("true");
        return;
      case ContentEditableState::False:
        aContentEditable.AssignLiteral("false");
        return;
      case ContentEditableState::PlainTextOnly:
        aContentEditable.AssignLiteral("plaintext-only");
        return;
      case ContentEditableState::Inherit:
        aContentEditable.AssignLiteral("inherit");
        return;
    }
  }
  void SetContentEditable(const nsAString& aContentEditable,
                          mozilla::ErrorResult& aError) {
    if (aContentEditable.LowerCaseEqualsLiteral("inherit")) {
      UnsetHTMLAttr(nsGkAtoms::contenteditable, aError);
    } else if (aContentEditable.LowerCaseEqualsLiteral("true")) {
      SetHTMLAttr(nsGkAtoms::contenteditable, u"true"_ns, aError);
    } else if (aContentEditable.LowerCaseEqualsLiteral("false")) {
      SetHTMLAttr(nsGkAtoms::contenteditable, u"false"_ns, aError);
    } else if (aContentEditable.LowerCaseEqualsLiteral("plaintext-only")) {
      SetHTMLAttr(nsGkAtoms::contenteditable, u"plaintext-only"_ns, aError);
    } else {
      aError.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    }
  }

  [[nodiscard]] bool IsContentEditable() const;

  /**
   * Returns ContentEditableState::True if the element has a contentEditable
   * attribute and its value is "true" or an empty string. Returns
   * ContentEditableState::False if the *element has a contentEditable attribute
   * and its value is "false". Returns ContentEditableState::PlainTextOnly if
   * the element has a contentEditable attribute and its value is
   * "plaintext-only". Otherwise returns ContentEditableState::Inherit.
   */
  [[nodiscard]] inline ContentEditableState GetContentEditableState() const {
    if (!MayHaveContentEditableAttr()) {
      return ContentEditableState::Inherit;
    }
    static constexpr AttrValuesArray kValidValuesExceptInherit[] = {
        nsGkAtoms::_empty, nsGkAtoms::_true, nsGkAtoms::plaintextOnly,
        nsGkAtoms::_false, nullptr};
    switch (mAttrs.FindAttrValueIn(kNameSpaceID_None,
                                   nsGkAtoms::contenteditable,
                                   kValidValuesExceptInherit, eIgnoreCase)) {
      case 0:
      case 1:
        return ContentEditableState::True;
      case 2:
        return ContentEditableState::PlainTextOnly;
      case 3:
        return ContentEditableState::False;
      default:
        return ContentEditableState::Inherit;
    }
  }

  mozilla::dom::PopoverAttributeState GetPopoverAttributeState() const;
  void PopoverPseudoStateUpdate(bool aOpen, bool aNotify);
  bool PopoverOpen() const;
  bool CheckPopoverValidity(mozilla::dom::PopoverVisibilityState aExpectedState,
                            Document* aExpectedDocument, ErrorResult& aRv);
  already_AddRefed<mozilla::dom::ToggleEvent> CreateToggleEvent(
      const nsAString& aEventType, const nsAString& aOldState,
      const nsAString& aNewState, mozilla::Cancelable);
  /** Returns true if the event has been cancelled. */
  MOZ_CAN_RUN_SCRIPT bool FireToggleEvent(const nsAString& aOldState,
                                          const nsAString& aNewState,
                                          const nsAString& aType);
  MOZ_CAN_RUN_SCRIPT void QueuePopoverEventTask(
      mozilla::dom::PopoverVisibilityState aOldState);
  MOZ_CAN_RUN_SCRIPT void RunPopoverToggleEventTask(
      mozilla::dom::PopoverToggleEventTask* aTask,
      mozilla::dom::PopoverVisibilityState aOldState);
  MOZ_CAN_RUN_SCRIPT void ShowPopover(ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void ShowPopoverInternal(Element* aInvoker,
                                              ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void HidePopoverWithoutRunningScript();
  MOZ_CAN_RUN_SCRIPT void HidePopoverInternal(bool aFocusPreviousElement,
                                              bool aFireEvents,
                                              ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void HidePopover(ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT bool TogglePopover(
      const mozilla::dom::Optional<bool>& aForce, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void FocusPopover();
  void ForgetPreviouslyFocusedElementAfterHidingPopover();
  MOZ_CAN_RUN_SCRIPT void FocusPreviousElementAfterHidingPopover();

  bool IsValidInvokeAction(mozilla::dom::InvokeAction aAction) const override;

  MOZ_CAN_RUN_SCRIPT bool HandleInvokeInternal(
      Element* aInvoker, mozilla::dom::InvokeAction aAction,
      ErrorResult& aRv) override;

  MOZ_CAN_RUN_SCRIPT void FocusCandidate(Element*, bool aClearUpFocus);

  void SetNonce(const nsAString& aNonce) {
    SetProperty(nsGkAtoms::nonce, new nsString(aNonce),
                nsINode::DeleteProperty<nsString>, /* aTransfer = */ true);
  }
  void RemoveNonce() { RemoveProperty(nsGkAtoms::nonce); }
  void GetNonce(nsAString& aNonce) const {
    nsString* cspNonce = static_cast<nsString*>(GetProperty(nsGkAtoms::nonce));
    if (cspNonce) {
      aNonce = *cspNonce;
    }
  }

  /** Returns whether a form control should be default-focusable. */
  bool IsFormControlDefaultFocusable(mozilla::IsFocusableFlags) const;

  /**
   * Returns the count of descendants (inclusive of this node) in
   * the uncomposed document that are explicitly set as editable.
   */
  uint32_t EditableInclusiveDescendantCount();

  bool Spellcheck();
  void SetSpellcheck(bool aSpellcheck, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::spellcheck, aSpellcheck ? u"true"_ns : u"false"_ns,
                aError);
  }

  MOZ_CAN_RUN_SCRIPT
  void GetInnerText(mozilla::dom::DOMString& aValue, ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT
  void GetOuterText(mozilla::dom::DOMString& aValue, ErrorResult& aError) {
    return GetInnerText(aValue, aError);
  }
  MOZ_CAN_RUN_SCRIPT void SetInnerText(const nsAString& aValue);
  MOZ_CAN_RUN_SCRIPT void SetOuterText(const nsAString& aValue,
                                       ErrorResult& aRv);

  void GetInputMode(nsAString& aValue) {
    GetEnumAttr(nsGkAtoms::inputmode, nullptr, aValue);
  }
  void SetInputMode(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::inputmode, aValue, aRv);
  }
  virtual void GetAutocapitalize(nsAString& aValue) const;
  void SetAutocapitalize(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::autocapitalize, aValue, aRv);
  }

  void GetEnterKeyHint(nsAString& aValue) const {
    GetEnumAttr(nsGkAtoms::enterkeyhint, nullptr, aValue);
  }
  void SetEnterKeyHint(const nsAString& aValue, ErrorResult& aRv) {
    SetHTMLAttr(nsGkAtoms::enterkeyhint, aValue, aRv);
  }

  virtual bool Autocorrect() const;
  void SetAutocorrect(bool aAutocorrect, mozilla::ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::autocorrect, aAutocorrect ? u"on"_ns : u"off"_ns,
                aError);
  }

  /**
   * Determine whether an attribute is an event (onclick, etc.)
   * @param aName the attribute
   * @return whether the name is an event handler name
   */
  bool IsEventAttributeNameInternal(nsAtom* aName) override;

#define EVENT(name_, id_, type_, struct_) /* nothing; handled by nsINode */
// The using nsINode::Get/SetOn* are to avoid warnings about shadowing the XPCOM
// getter and setter on nsINode.
#define FORWARDED_EVENT(name_, id_, type_, struct_)  \
  using nsINode::GetOn##name_;                       \
  using nsINode::SetOn##name_;                       \
  mozilla::dom::EventHandlerNonNull* GetOn##name_(); \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler);
#define ERROR_EVENT(name_, id_, type_, struct_)                       \
  using nsINode::GetOn##name_;                                        \
  using nsINode::SetOn##name_;                                        \
  already_AddRefed<mozilla::dom::EventHandlerNonNull> GetOn##name_(); \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler);
#include "mozilla/EventNameList.h"  // IWYU pragma: keep
#undef ERROR_EVENT
#undef FORWARDED_EVENT
#undef EVENT
  mozilla::dom::Element* GetOffsetParent() {
    mozilla::CSSIntRect rcFrame;
    return GetOffsetRect(rcFrame);
  }
  int32_t OffsetTop() {
    mozilla::CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);

    return rcFrame.y;
  }
  int32_t OffsetLeft() {
    mozilla::CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);

    return rcFrame.x;
  }
  int32_t OffsetWidth() {
    mozilla::CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);

    return rcFrame.Width();
  }
  int32_t OffsetHeight() {
    mozilla::CSSIntRect rcFrame;
    GetOffsetRect(rcFrame);

    return rcFrame.Height();
  }

  // These methods are already implemented in nsIContent but we want something
  // faster for HTMLElements ignoring the namespace checking.
  // This is safe because we already know that we are in the HTML namespace.
  inline bool IsHTMLElement() const { return true; }

  inline bool IsHTMLElement(nsAtom* aTag) const {
    return mNodeInfo->Equals(aTag);
  }

  template <typename First, typename... Args>
  inline bool IsAnyOfHTMLElements(First aFirst, Args... aArgs) const {
    return IsNodeInternal(aFirst, aArgs...);
  }

  // https://html.spec.whatwg.org/multipage/custom-elements.html#dom-attachinternals
  virtual already_AddRefed<mozilla::dom::ElementInternals> AttachInternals(
      ErrorResult& aRv);

  mozilla::dom::ElementInternals* GetInternals() const;

  bool IsFormAssociatedCustomElements() const;

  // Returns true if the event should not be handled from GetEventTargetParent.
  virtual bool IsDisabledForEvents(mozilla::WidgetEvent* aEvent) {
    return false;
  }

  bool Autofocus() const { return GetBoolAttr(nsGkAtoms::autofocus); }
  void SetAutofocus(bool aVal, ErrorResult& aRv) {
    SetHTMLBoolAttr(nsGkAtoms::autofocus, aVal, aRv);
  }

 protected:
  virtual ~nsGenericHTMLElement() = default;

 public:
  // Implementation for nsIContent
  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;

  Focusable IsFocusableWithoutStyle(mozilla::IsFocusableFlags aFlags =
                                        mozilla::IsFocusableFlags(0)) override {
    Focusable result;
    IsHTMLFocusable(aFlags, &result.mFocusable, &result.mTabIndex);
    return result;
  }
  /**
   * Returns true if a subclass is not allowed to override the value returned
   * in aIsFocusable.
   */
  virtual bool IsHTMLFocusable(mozilla::IsFocusableFlags, bool* aIsFocusable,
                               int32_t* aTabIndex);
  MOZ_CAN_RUN_SCRIPT
  mozilla::Result<bool, nsresult> PerformAccesskey(
      bool aKeyCausesActivation, bool aIsTrustedEvent) override;

  /**
   * Check if an event for an anchor can be handled
   * @return true if the event can be handled, false otherwise
   */
  bool CheckHandleEventForAnchorsPreconditions(
      mozilla::EventChainVisitor& aVisitor);
  void GetEventTargetParentForAnchors(mozilla::EventChainPreVisitor& aVisitor);
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEventForAnchors(mozilla::EventChainPostVisitor& aVisitor);
  bool IsHTMLLink(nsIURI** aURI) const;

  // HTML element methods
  void Compact() { mAttrs.Compact(); }

  void UpdateEditableState(bool aNotify) override;

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  bool ParseBackgroundAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                const nsAString& aValue, nsAttrValue& aResult);

  NS_IMETHOD_(bool) IsAttributeMapped(const nsAtom* aAttribute) const override;
  nsMapRuleToAttributesFunc GetAttributeMappingFunction() const override;

  /**
   * Get the base target for any links within this piece
   * of content. Generally, this is the document's base target,
   * but certain content carries a local base for backward
   * compatibility.
   *
   * @param aBaseTarget the base target [OUT]
   */
  void GetBaseTarget(nsAString& aBaseTarget) const;

  /**
   * Parse an alignment attribute (top/middle/bottom/baseline)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseAlignValue(const nsAString& aString, nsAttrValue& aResult);

  /**
   * Parse a div align string to value (left/right/center/middle/justify)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseDivAlignValue(const nsAString& aString,
                                 nsAttrValue& aResult);

  /**
   * Convert a table halign string to value (left/right/center/char/justify)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseTableHAlignValue(const nsAString& aString,
                                    nsAttrValue& aResult);

  /**
   * Convert a table cell halign string to value
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseTableCellHAlignValue(const nsAString& aString,
                                        nsAttrValue& aResult);

  /**
   * Convert a table valign string to value (left/right/center/char/justify/
   * abscenter/absmiddle/middle)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseTableVAlignValue(const nsAString& aString,
                                    nsAttrValue& aResult);

  /**
   * Convert an image attribute to value (width, height, hspace, vspace, border)
   *
   * @param aAttribute the attribute to parse
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseImageAttribute(nsAtom* aAttribute, const nsAString& aString,
                                  nsAttrValue& aResult);

  static bool ParseReferrerAttribute(const nsAString& aString,
                                     nsAttrValue& aResult);

  /**
   * Convert a frameborder string to value (yes/no/1/0)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseFrameborderValue(const nsAString& aString,
                                    nsAttrValue& aResult);

  /**
   * Convert a scrolling string to value (yes/no/on/off/scroll/noscroll/auto)
   *
   * @param aString the string to parse
   * @param aResult the resulting HTMLValue
   * @return whether the value was parsed
   */
  static bool ParseScrollingValue(const nsAString& aString,
                                  nsAttrValue& aResult);

  /*
   * Attribute Mapping Helpers
   */

  /**
   * A style attribute mapping function for the most common attributes, to be
   * called by subclasses' attribute mapping functions.  Currently handles
   * dir, lang and hidden, could handle others.
   *
   * @param aAttributes the list of attributes to map
   * @param aData the returned rule data [INOUT]
   * @see GetAttributeMappingFunction
   */
  static void MapCommonAttributesInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Same as MapCommonAttributesInto except that it does not handle hidden.
   * @see GetAttributeMappingFunction
   */
  static void MapCommonAttributesIntoExceptHidden(
      mozilla::MappedDeclarationsBuilder&);

  static const MappedAttributeEntry sCommonAttributeMap[];
  static const MappedAttributeEntry sImageMarginSizeAttributeMap[];
  static const MappedAttributeEntry sImageBorderAttributeMap[];
  static const MappedAttributeEntry sImageAlignAttributeMap[];
  static const MappedAttributeEntry sDivAlignAttributeMap[];
  static const MappedAttributeEntry sBackgroundAttributeMap[];
  static const MappedAttributeEntry sBackgroundColorAttributeMap[];

  /**
   * Helper to map the align attribute.
   * @see GetAttributeMappingFunction
   */
  static void MapImageAlignAttributeInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map the align attribute for things like <div>, <h1>, etc.
   * @see GetAttributeMappingFunction
   */
  static void MapDivAlignAttributeInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map the valign attribute for things like <col>, <tr>, <section>.
   * @see GetAttributeMappingFunction
   */
  static void MapVAlignAttributeInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map the image border attribute.
   * @see GetAttributeMappingFunction
   */
  static void MapImageBorderAttributeInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Helper to map the image margin attribute into a style struct.
   *
   * @param aAttributes the list of attributes to map
   * @param aData the returned rule data [INOUT]
   * @see GetAttributeMappingFunction
   */
  static void MapImageMarginAttributeInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map a given dimension (width/height) into the declaration
   * block, handling percentages and numbers.
   */
  static void MapDimensionAttributeInto(mozilla::MappedDeclarationsBuilder&,
                                        nsCSSPropertyID, const nsAttrValue&);

  /**
   * Maps the aspect ratio given width and height attributes.
   */
  static void DoMapAspectRatio(const nsAttrValue& aWidth,
                               const nsAttrValue& aHeight,
                               mozilla::MappedDeclarationsBuilder&);

  // Whether to map the width and height attributes to aspect-ratio.
  enum class MapAspectRatio { No, Yes };

  /**
   * Helper to map the image position attribute into a style struct.
   */
  static void MapImageSizeAttributesInto(mozilla::MappedDeclarationsBuilder&,
                                         MapAspectRatio = MapAspectRatio::No);

  /**
   * Helper to map the width and height attributes into the aspect-ratio
   * property.
   *
   * If you also map the width/height attributes to width/height (as you should
   * for any HTML element that isn't <canvas>) then you should use
   * MapImageSizeAttributesInto instead, passing MapAspectRatio::Yes instead, as
   * that'd be faster.
   */
  static void MapAspectRatioInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map `width` attribute into a style struct.
   *
   * @param aAttributes the list of attributes to map
   * @param aData the returned rule data [INOUT]
   * @see GetAttributeMappingFunction
   */
  static void MapWidthAttributeInto(mozilla::MappedDeclarationsBuilder&);

  /**
   * Helper to map `height` attribute.
   * @see GetAttributeMappingFunction
   */
  static void MapHeightAttributeInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Helper to map the background attribute
   * @see GetAttributeMappingFunction
   */
  static void MapBackgroundInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Helper to map the bgcolor attribute
   * @see GetAttributeMappingFunction
   */
  static void MapBGColorInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Helper to map the background attributes (currently background and bgcolor)
   * @see GetAttributeMappingFunction
   */
  static void MapBackgroundAttributesInto(mozilla::MappedDeclarationsBuilder&);
  /**
   * Helper to map the scrolling attribute on FRAME and IFRAME.
   * @see GetAttributeMappingFunction
   */
  static void MapScrollingAttributeInto(mozilla::MappedDeclarationsBuilder&);

  // Form Helper Routines
  /**
   * Find an ancestor of this content node which is a form (could be null)
   * @param aCurrentForm the current form for this node.  If this is
   *        non-null, and no ancestor form is found, and the current form is in
   *        a connected subtree with the node, the current form will be
   *        returned.  This is needed to handle cases when HTML elements have a
   *        current form that they're not descendants of.
   * @note This method should not be called if the element has a form attribute.
   */
  mozilla::dom::HTMLFormElement* FindAncestorForm(
      mozilla::dom::HTMLFormElement* aCurrentForm = nullptr);

  /**
   * See if the document being tested has nav-quirks mode enabled.
   * @param doc the document
   */
  static bool InNavQuirksMode(Document*);

  /**
   * Gets the absolute URI value of an attribute, by resolving any relative
   * URIs in the attribute against the baseuri of the element. If the attribute
   * isn't a relative URI the value of the attribute is returned as is. Only
   * works for attributes in null namespace.
   *
   * @param aAttr      name of attribute.
   * @param aBaseAttr  name of base attribute.
   * @param aResult    result value [out]
   */
  void GetURIAttr(nsAtom* aAttr, nsAtom* aBaseAttr, nsAString& aResult) const;
  void GetURIAttr(nsAtom* aAttr, nsAtom* aBaseAttr, nsACString& aResult) const;

  /**
   * Gets the absolute URI values of an attribute, by resolving any relative
   * URIs in the attribute against the baseuri of the element. If a substring
   * isn't a relative URI, the substring is returned as is. Only works for
   * attributes in null namespace.
   */
  const nsAttrValue* GetURIAttr(nsAtom* aAttr, nsAtom* aBaseAttr,
                                nsIURI** aURI) const;

  bool IsHidden() const { return HasAttr(nsGkAtoms::hidden); }

  bool IsLabelable() const override;

  static bool MatchLabelsElement(Element* aElement, int32_t aNamespaceID,
                                 nsAtom* aAtom, void* aData);

  already_AddRefed<nsINodeList> Labels();

  static bool LegacyTouchAPIEnabled(JSContext* aCx, JSObject* aObj);

  // https://html.spec.whatwg.org/#dom-window-nameditem-filter
  static inline bool CanHaveName(nsAtom* aTag) {
    return aTag == nsGkAtoms::img || aTag == nsGkAtoms::form ||
           aTag == nsGkAtoms::embed || aTag == nsGkAtoms::object;
  }
  static inline bool ShouldExposeNameAsWindowProperty(Element* aElement) {
    return aElement->IsHTMLElement() &&
           CanHaveName(aElement->NodeInfo()->NameAtom());
  }
  // https://html.spec.whatwg.org/#dom-document-nameditem-filter
  static inline bool ShouldExposeIdAsHTMLDocumentProperty(Element* aElement) {
    if (!aElement->HasID() || aElement->IsInNativeAnonymousSubtree()) {
      return false;
    }
    // XXX Not all objects is exposed per spec, but other browsers doesn't check
    // if object is exposed, either.
    if (aElement->IsHTMLElement(nsGkAtoms::object)) {
      return true;
    }

    // Per spec, <img> is exposed by id only if it also has a nonempty
    // name (which doesn't have to match the id or anything).
    // HasName() is true precisely when name is nonempty.
    return aElement->IsHTMLElement(nsGkAtoms::img) && aElement->HasName();
  }
  static inline bool ShouldExposeNameAsHTMLDocumentProperty(Element* aElement) {
    if (!aElement->HasName() || aElement->IsInNativeAnonymousSubtree()) {
      return false;
    }
    // XXX Not all embeds/objects are exposed per spec, but other browser
    // doesn't check if embeds/objects are exposed.
    return aElement->IsAnyOfHTMLElements(nsGkAtoms::embed, nsGkAtoms::form,
                                         nsGkAtoms::iframe, nsGkAtoms::img,
                                         nsGkAtoms::object);
  }

  virtual inline void ResultForDialogSubmit(nsAString& aResult) {
    GetAttr(nsGkAtoms::value, aResult);
  }

  // <https://html.spec.whatwg.org/#fetch-priority-attribute>.
  static mozilla::dom::FetchPriority ToFetchPriority(const nsAString& aValue);

  void GetFetchPriority(nsAString& aFetchPriority) const;

  void SetFetchPriority(const nsAString& aFetchPriority) {
    SetHTMLAttr(nsGkAtoms::fetchpriority, aFetchPriority);
  }

 protected:
  mozilla::dom::FetchPriority GetFetchPriority() const;

  static void ParseFetchPriority(const nsAString& aValue, nsAttrValue& aResult);

 private:
  /**
   * Add/remove this element to the documents name cache
   */
  void AddToNameTable(nsAtom* aName);
  void RemoveFromNameTable();

  /**
   * Register or unregister an access key to this element based on the
   * accesskey attribute.
   */
  void RegUnRegAccessKey(bool aDoReg) override {
    if (!HasFlag(NODE_HAS_ACCESSKEY)) {
      return;
    }

    nsStyledElement::RegUnRegAccessKey(aDoReg);
  }

 protected:
  void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;
  // TODO: Convert AfterSetAttr to MOZ_CAN_RUN_SCRIPT and get rid of
  // kungFuDeathGrip in it.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void AfterSetAttr(
      int32_t aNamespaceID, nsAtom* aName, const nsAttrValue* aValue,
      const nsAttrValue* aOldValue, nsIPrincipal* aMaybeScriptedPrincipal,
      bool aNotify) override;

  void OnAttrSetButNotChanged(int32_t aNamespaceID, nsAtom* aName,
                              const nsAttrValueOrString& aValue,
                              bool aNotify) override;

  MOZ_CAN_RUN_SCRIPT void AfterSetPopoverAttr();

  mozilla::EventListenerManager* GetEventListenerManagerForAttr(
      nsAtom* aAttrName, bool* aDefer) override;

  /**
   * Handles dispatching a simulated click on `this` on space or enter.
   * TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void HandleKeyboardActivation(
      mozilla::EventChainPostVisitor&);

  /** Dispatch a simulated mouse click by keyboard to the given element. */
  MOZ_CAN_RUN_SCRIPT static nsresult DispatchSimulatedClick(
      nsGenericHTMLElement* aElement, bool aIsTrusted,
      nsPresContext* aPresContext);

  /**
   * Create a URI for the given aURISpec string.
   * Returns INVALID_STATE_ERR and nulls *aURI if aURISpec is empty
   * and the document's URI matches the element's base URI.
   */
  nsresult NewURIFromString(const nsAString& aURISpec, nsIURI** aURI);

  void GetHTMLAttr(nsAtom* aName, nsAString& aResult) const {
    GetAttr(aName, aResult);
  }
  void GetHTMLAttr(nsAtom* aName, mozilla::dom::DOMString& aResult) const {
    GetAttr(aName, aResult);
  }
  void GetHTMLEnumAttr(nsAtom* aName, nsAString& aResult) const {
    GetEnumAttr(aName, nullptr, aResult);
  }
  void GetHTMLURIAttr(nsAtom* aName, nsAString& aResult) const {
    GetURIAttr(aName, nullptr, aResult);
  }
  void GetHTMLURIAttr(nsAtom* aName, nsACString& aResult) const {
    GetURIAttr(aName, nullptr, aResult);
  }

  void SetHTMLAttr(nsAtom* aName, const nsAString& aValue) {
    SetAttr(kNameSpaceID_None, aName, aValue, true);
  }
  void SetHTMLAttr(nsAtom* aName, const nsAString& aValue,
                   mozilla::ErrorResult& aError) {
    SetAttr(aName, aValue, aError);
  }
  void SetHTMLAttr(nsAtom* aName, const nsAString& aValue,
                   nsIPrincipal* aTriggeringPrincipal,
                   mozilla::ErrorResult& aError) {
    SetAttr(aName, aValue, aTriggeringPrincipal, aError);
  }
  void UnsetHTMLAttr(nsAtom* aName, mozilla::ErrorResult& aError) {
    UnsetAttr(aName, aError);
  }
  void SetHTMLBoolAttr(nsAtom* aName, bool aValue,
                       mozilla::ErrorResult& aError) {
    if (aValue) {
      SetHTMLAttr(aName, u""_ns, aError);
    } else {
      UnsetHTMLAttr(aName, aError);
    }
  }
  template <typename T>
  void SetHTMLIntAttr(nsAtom* aName, T aValue, mozilla::ErrorResult& aError) {
    nsAutoString value;
    value.AppendInt(aValue);

    SetHTMLAttr(aName, value, aError);
  }

  /**
   * Gets the integer-value of an attribute, returns specified default value
   * if the attribute isn't set or isn't set to an integer. Only works for
   * attributes in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aDefault default-value to return if attribute isn't set.
   */
  int32_t GetIntAttr(nsAtom* aAttr, int32_t aDefault) const;

  /**
   * Sets value of attribute to specified integer. Only works for attributes
   * in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aValue   Integer value of attribute.
   */
  nsresult SetIntAttr(nsAtom* aAttr, int32_t aValue);

  /**
   * Gets the unsigned integer-value of an attribute, returns specified default
   * value if the attribute isn't set or isn't set to an integer. Only works for
   * attributes in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aDefault default-value to return if attribute isn't set.
   */
  uint32_t GetUnsignedIntAttr(nsAtom* aAttr, uint32_t aDefault) const;

  /**
   * Sets value of attribute to specified unsigned integer. Only works for
   * attributes in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aValue   Integer value of attribute.
   * @param aDefault Default value (in case value is out of range).  If the spec
   *                 doesn't provide one, should be 1 if the value is limited to
   *                 nonzero values, and 0 otherwise.
   */
  void SetUnsignedIntAttr(nsAtom* aName, uint32_t aValue, uint32_t aDefault,
                          mozilla::ErrorResult& aError) {
    nsAutoString value;
    if (aValue > INT32_MAX) {
      value.AppendInt(aDefault);
    } else {
      value.AppendInt(aValue);
    }

    SetHTMLAttr(aName, value, aError);
  }

  /**
   * Gets the unsigned integer-value of an attribute that is stored as a
   * dimension (i.e. could be an integer or a percentage), returns specified
   * default value if the attribute isn't set or isn't set to a dimension. Only
   * works for attributes in null namespace.
   *
   * @param aAttr    name of attribute.
   * @param aDefault default-value to return if attribute isn't set.
   */
  uint32_t GetDimensionAttrAsUnsignedInt(nsAtom* aAttr,
                                         uint32_t aDefault) const;

  enum class Reflection {
    Unlimited,
    OnlyPositive,
  };

  /**
   * Sets value of attribute to specified double. Only works for attributes
   * in null namespace.
   *
   * Implements
   * https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes:idl-double
   *
   * @param aAttr    name of attribute.
   * @param aValue   Double value of attribute.
   */
  template <Reflection Limited = Reflection::Unlimited>
  void SetDoubleAttr(nsAtom* aAttr, double aValue, mozilla::ErrorResult& aRv) {
    // 1. If the reflected IDL attribute is limited to only positive numbers and
    //    the given value is not greater than 0, then return.
    if (Limited == Reflection::OnlyPositive && aValue <= 0) {
      return;
    }

    // 2. Run this's set the content attribute with the given value, converted
    //    to the best representation of the number as a floating-point number.
    nsAutoString value;
    value.AppendFloat(aValue);

    SetHTMLAttr(aAttr, value, aRv);
  }

  /**
   * Locates the EditorBase associated with this node.  In general this is
   * equivalent to GetEditorInternal(), but for designmode or contenteditable,
   * this may need to get an editor that's not actually on this element's
   * associated TextControlFrame.  This is used by the spellchecking routines
   * to get the editor affected by changing the spellcheck attribute on this
   * node.
   */
  virtual already_AddRefed<mozilla::EditorBase> GetAssociatedEditor();

  /**
   * Get the frame's offset information for offsetTop/Left/Width/Height.
   * Returns the parent the offset is relative to.
   * @note This method flushes pending notifications (FlushType::Layout).
   * @param aRect the offset information [OUT]
   */
  mozilla::dom::Element* GetOffsetRect(mozilla::CSSIntRect& aRect);

  /**
   * Ensures all editors associated with a subtree are synced, for purposes of
   * spellchecking.
   */
  static void SyncEditorsOnSubtree(nsIContent* content);

  [[nodiscard]] inline static bool IsEditableState(
      ContentEditableState aState) {
    return aState == ContentEditableState::True ||
           aState == ContentEditableState::PlainTextOnly;
  }

  // Used by A, AREA, LINK, and STYLE.
  already_AddRefed<nsIURI> GetHrefURIForAnchors() const;

 private:
  MOZ_CAN_RUN_SCRIPT void ChangeEditableState(int32_t aChange);
};

namespace mozilla::dom {
class HTMLFieldSetElement;
}  // namespace mozilla::dom

#define HTML_ELEMENT_FLAG_BIT(n_) \
  NODE_FLAG_BIT(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// HTMLElement specific bits
enum {
  // Used to handle keyboard activation.
  HTML_ELEMENT_ACTIVE_FOR_KEYBOARD = HTML_ELEMENT_FLAG_BIT(0),
  // Similar to HTMLInputElement's mInhibitRestoration, used to prevent
  // form-associated custom elements not created by a network parser from
  // being restored.
  HTML_ELEMENT_INHIBIT_RESTORATION = HTML_ELEMENT_FLAG_BIT(1),

  // Remaining bits are type specific.
  HTML_ELEMENT_TYPE_SPECIFIC_BITS_OFFSET =
      ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + 2,
};

ASSERT_NODE_FLAGS_SPACE(HTML_ELEMENT_TYPE_SPECIFIC_BITS_OFFSET);

#define FORM_ELEMENT_FLAG_BIT(n_) \
  NODE_FLAG_BIT(HTML_ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// Form element specific bits
enum {
  // If this flag is set on an nsGenericHTMLFormElement or an HTMLImageElement,
  // that means that we have added ourselves to our mForm.  It's possible to
  // have a non-null mForm, but not have this flag set.  That happens when the
  // form is set via the content sink.
  ADDED_TO_FORM = FORM_ELEMENT_FLAG_BIT(0),

  // If this flag is set on an nsGenericHTMLFormElement or an HTMLImageElement,
  // that means that its form is in the process of being unbound from the tree,
  // and this form element hasn't re-found its form in
  // nsGenericHTMLFormElement::UnbindFromTree yet.
  MAYBE_ORPHAN_FORM_ELEMENT = FORM_ELEMENT_FLAG_BIT(1),

  // If this flag is set on an nsGenericHTMLElement or an HTMLImageElement, then
  // the element might be in the past names map of its form.
  MAY_BE_IN_PAST_NAMES_MAP = FORM_ELEMENT_FLAG_BIT(2)
};

// NOTE: I don't think it's possible to have both ADDED_TO_FORM and
// MAYBE_ORPHAN_FORM_ELEMENT set at the same time, so if it becomes an issue we
// can probably merge them into the same bit.  --bz

ASSERT_NODE_FLAGS_SPACE(HTML_ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + 3);

#undef FORM_ELEMENT_FLAG_BIT

/**
 * A helper class for form elements that can contain children
 */
class nsGenericHTMLFormElement : public nsGenericHTMLElement {
 public:
  nsGenericHTMLFormElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

  // nsIContent
  void SaveSubtreeState() override;
  nsresult BindToTree(BindContext&, nsINode& aParent) override;
  void UnbindFromTree(UnbindContext&) override;

  /**
   * This callback is called by a fieldest on all its elements whenever its
   * disabled attribute is changed so the element knows its disabled state
   * might have changed.
   *
   * @note Classes redefining this method should not do any content
   * state updates themselves but should just make sure to call into
   * nsGenericHTMLFormElement::FieldSetDisabledChanged.
   */
  virtual void FieldSetDisabledChanged(bool aNotify);

  void FieldSetFirstLegendChanged(bool aNotify) { UpdateFieldSet(aNotify); }

  /**
   * This callback is called by a fieldset on all it's elements when it's being
   * destroyed. When called, the elements should check that aFieldset is there
   * first parent fieldset and null mFieldset in that case only.
   *
   * @param aFieldSet The fieldset being removed.
   */
  void ForgetFieldSet(nsIContent* aFieldset);

  void ClearForm(bool aRemoveFromForm, bool aUnbindOrDelete);

  /**
   * Get the layout history object for a particular piece of content.
   *
   * @param aRead if true, won't return a layout history state if the
   *              layout history state is empty.
   * @return the history state object
   */
  already_AddRefed<nsILayoutHistoryState> GetLayoutHistory(bool aRead);

  // Sets the user-interacted flag in
  // https://html.spec.whatwg.org/#user-interacted, if it applies.
  virtual void SetUserInteracted(bool aNotify) {}

 protected:
  virtual ~nsGenericHTMLFormElement() = default;

  void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) override;

  void AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aMaybeScriptedPrincipal,
                    bool aNotify) override;

  virtual void BeforeSetForm(mozilla::dom::HTMLFormElement* aForm,
                             bool aBindToTree) {}

  virtual void AfterClearForm(bool aUnbindOrDelete) {}

  /**
   * Check our disabled content attribute and fieldset's (if it exists) disabled
   * state to decide whether our disabled flag should be toggled.
   */
  virtual void UpdateDisabledState(bool aNotify);
  bool IsReadOnlyInternal() const final;

  virtual void SetFormInternal(mozilla::dom::HTMLFormElement* aForm,
                               bool aBindToTree) {}

  virtual mozilla::dom::HTMLFormElement* GetFormInternal() const {
    return nullptr;
  }

  virtual mozilla::dom::HTMLFieldSetElement* GetFieldSetInternal() const {
    return nullptr;
  }

  virtual void SetFieldSetInternal(
      mozilla::dom::HTMLFieldSetElement* aFieldset) {}

  /**
   * This method will update the form owner, using @form or looking to a parent.
   *
   * @param aBindToTree Whether the element is being attached to the tree.
   * @param aFormIdElement The element associated with the id in @form. If
   * aBindToTree is false, aFormIdElement *must* contain the element associated
   * with the id in @form. Otherwise, it *must* be null.
   *
   * @note Callers of UpdateFormOwner have to be sure the element is in a
   * document (GetUncomposedDoc() != nullptr).
   */
  virtual void UpdateFormOwner(bool aBindToTree, Element* aFormIdElement);

  /**
   * This method will update mFieldset and set it to the first fieldset parent.
   */
  void UpdateFieldSet(bool aNotify);

  /**
   * Add a form id observer which will observe when the element with the id in
   * @form will change.
   *
   * @return The element associated with the current id in @form (may be null).
   */
  Element* AddFormIdObserver();

  /**
   * Remove the form id observer.
   */
  void RemoveFormIdObserver();

  /**
   * This method is a a callback for IDTargetObserver (from Document).
   * It will be called each time the element associated with the id in @form
   * changes.
   */
  static bool FormIdUpdated(Element* aOldElement, Element* aNewElement,
                            void* aData);

  // Returns true if the event should not be handled from GetEventTargetParent
  bool IsElementDisabledForEvents(mozilla::WidgetEvent* aEvent,
                                  nsIFrame* aFrame);

  /**
   * Returns if the control can be disabled.
   */
  virtual bool CanBeDisabled() const { return false; }

  /**
   * Returns true if :read-write pseudo class may match the element even if the
   * element isn't part of designMode or contenteditable.
   */
  virtual bool DoesReadWriteApply() const { return false; }

  /**
   *  Returns true if the element is a form associated element.
   *  See https://html.spec.whatwg.org/#form-associated-element.
   */
  virtual bool IsFormAssociatedElement() const { return false; }

  /**
   * Save to presentation state.  The form element will determine whether it
   * has anything to save and if so, create an entry in the layout history for
   * its pres context.
   */
  virtual void SaveState() {}
};

class nsGenericHTMLFormControlElement : public nsGenericHTMLFormElement,
                                        public nsIFormControl {
 public:
  nsGenericHTMLFormControlElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo, FormControlType);

  NS_DECL_ISUPPORTS_INHERITED

  NS_IMPL_FROMNODE_HELPER(nsGenericHTMLFormControlElement,
                          IsHTMLFormControlElement())

  [[nodiscard]] nsIFormControl* GetAsFormControl() final { return this; }
  [[nodiscard]] const nsIFormControl* GetAsFormControl() const final {
    return this;
  }

  // nsINode
  nsINode* GetScopeChainParent() const override;
  bool IsHTMLFormControlElement() const final { return true; }

  // nsIContent
  IMEState GetDesiredIMEState() override;

  // nsGenericHTMLElement
  // autocapitalize attribute support
  void GetAutocapitalize(nsAString& aValue) const override;
  // autocorrect attribute support
  bool Autocorrect() const override;
  bool IsHTMLFocusable(mozilla::IsFocusableFlags, bool* aIsFocusable,
                       int32_t* aTabIndex) override;

  // nsIFormControl
  mozilla::dom::HTMLFieldSetElement* GetFieldSet() override;
  mozilla::dom::HTMLFormElement* GetForm() const override { return mForm; }
  void SetForm(mozilla::dom::HTMLFormElement* aForm) override;
  void ClearForm(bool aRemoveFromForm, bool aUnbindOrDelete) override;

 protected:
  virtual ~nsGenericHTMLFormControlElement();

  // Element
  bool IsLabelable() const override;

  // nsGenericHTMLFormElement
  bool CanBeDisabled() const override;
  bool DoesReadWriteApply() const override;
  void SetFormInternal(mozilla::dom::HTMLFormElement* aForm,
                       bool aBindToTree) override;
  mozilla::dom::HTMLFormElement* GetFormInternal() const override;
  mozilla::dom::HTMLFieldSetElement* GetFieldSetInternal() const override;
  void SetFieldSetInternal(
      mozilla::dom::HTMLFieldSetElement* aFieldset) override;
  bool IsFormAssociatedElement() const override { return true; }

  /**
   * Update our required/optional flags to match the given aIsRequired boolean.
   */
  void UpdateRequiredState(bool aIsRequired, bool aNotify);

  bool IsAutocapitalizeOrAutocorrectInheriting() const;

  nsresult SubmitDirnameDir(mozilla::dom::FormData* aFormData);

  void GetFormAutofillState(nsAString& aState) const;
  void SetFormAutofillState(const nsAString& aState);

  /** The form that contains this control */
  mozilla::dom::HTMLFormElement* mForm;

  /* This is a pointer to our closest fieldset parent if any */
  mozilla::dom::HTMLFieldSetElement* mFieldSet;
};

enum class PopoverTargetAction : uint8_t {
  Toggle,
  Show,
  Hide,
};

class nsGenericHTMLFormControlElementWithState
    : public nsGenericHTMLFormControlElement {
 public:
  nsGenericHTMLFormControlElementWithState(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
      mozilla::dom::FromParser aFromParser, FormControlType);

  bool IsGenericHTMLFormControlElementWithState() const final { return true; }
  NS_IMPL_FROMNODE_HELPER(nsGenericHTMLFormControlElementWithState,
                          IsGenericHTMLFormControlElementWithState())

  // Element
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  // PopoverInvokerElement
  mozilla::dom::Element* GetPopoverTargetElement() const;
  void SetPopoverTargetElement(mozilla::dom::Element*);
  void GetPopoverTargetAction(nsAString& aValue) const {
    GetHTMLEnumAttr(nsGkAtoms::popovertargetaction, aValue);
  }
  void SetPopoverTargetAction(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::popovertargetaction, aValue);
  }

  // InvokerElement
  mozilla::dom::Element* GetInvokeTargetElement() const;
  void SetInvokeTargetElement(mozilla::dom::Element*);
  void GetInvokeAction(nsAString& aValue) const;
  InvokeAction GetInvokeAction(nsAtom* aAtom) const;
  void SetInvokeAction(const nsAString& aValue) {
    SetHTMLAttr(nsGkAtoms::invokeaction, aValue);
  }

  /**
   * https://html.spec.whatwg.org/#popover-target-attribute-activation-behavior
   */
  MOZ_CAN_RUN_SCRIPT void HandlePopoverTargetAction();

  MOZ_CAN_RUN_SCRIPT void HandleInvokeTargetAction();

  /**
   * Get the presentation state for a piece of content, or create it if it does
   * not exist.  Generally used by SaveState().
   */
  mozilla::PresState* GetPrimaryPresState();

  /**
   * Called when we have been cloned and adopted, and the information of the
   * node has been changed.
   */
  void NodeInfoChanged(Document* aOldDoc) override;

  void GetFormAction(nsString& aValue);

 protected:
  /**
   * Restore from presentation state.  You pass in the presentation state for
   * this form control (generated with GenerateStateKey() + "-C") and the form
   * control will grab its state from there.
   *
   * @param aState the pres state to use to restore the control
   * @return true if the form control was a checkbox and its
   *         checked state was restored, false otherwise.
   */
  virtual bool RestoreState(mozilla::PresState* aState) { return false; }

  /**
   * Restore the state for a form control in response to the element being
   * inserted into the document by the parser.  Ends up calling RestoreState().
   *
   * GenerateStateKey() must already have been called.
   *
   * @return false if RestoreState() was not called, the return
   *         value of RestoreState() otherwise.
   */
  bool RestoreFormControlState();

  /* Generates the state key for saving the form state in the session if not
     computed already. The result is stored in mStateKey. */
  void GenerateStateKey();

  int32_t GetParserInsertedControlNumberForStateKey() const override {
    return mControlNumber;
  }

  /* Used to store the key to that element in the session. Is void until
     GenerateStateKey has been used */
  nsCString mStateKey;

  // A number for this form control that is unique within its owner document.
  // This is only set to a number for elements inserted into the document by
  // the parser from the network.  Otherwise, it is -1.
  int32_t mControlNumber;
};

#define NS_INTERFACE_MAP_ENTRY_IF_TAG(_interface, _tag) \
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(_interface,        \
                                     mNodeInfo->Equals(nsGkAtoms::_tag))

namespace mozilla::dom {

using HTMLContentCreatorFunction =
    nsGenericHTMLElement* (*)(already_AddRefed<mozilla::dom::NodeInfo>&&,
                              mozilla::dom::FromParser);

}  // namespace mozilla::dom

/**
 * A macro to declare the NS_NewHTMLXXXElement() functions.
 */
#define NS_DECLARE_NS_NEW_HTML_ELEMENT(_elementName)        \
  namespace mozilla {                                       \
  namespace dom {                                           \
  class HTML##_elementName##Element;                        \
  }                                                         \
  }                                                         \
  nsGenericHTMLElement* NS_NewHTML##_elementName##Element(  \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo, \
      mozilla::dom::FromParser aFromParser = mozilla::dom::NOT_FROM_PARSER);

#define NS_DECLARE_NS_NEW_HTML_ELEMENT_AS_SHARED(_elementName)                \
  inline nsGenericHTMLElement* NS_NewHTML##_elementName##Element(             \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,                   \
      mozilla::dom::FromParser aFromParser = mozilla::dom::NOT_FROM_PARSER) { \
    return NS_NewHTMLSharedElement(std::move(aNodeInfo), aFromParser);        \
  }

/**
 * A macro to implement the NS_NewHTMLXXXElement() functions.
 */
#define NS_IMPL_NS_NEW_HTML_ELEMENT(_elementName)                     \
  nsGenericHTMLElement* NS_NewHTML##_elementName##Element(            \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,           \
      mozilla::dom::FromParser aFromParser) {                         \
    RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);               \
    auto* nim = nodeInfo->NodeInfoManager();                          \
    MOZ_ASSERT(nim);                                                  \
    return new (nim)                                                  \
        mozilla::dom::HTML##_elementName##Element(nodeInfo.forget()); \
  }

#define NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(_elementName)  \
  nsGenericHTMLElement* NS_NewHTML##_elementName##Element(      \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,     \
      mozilla::dom::FromParser aFromParser) {                   \
    RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);         \
    auto* nim = nodeInfo->NodeInfoManager();                    \
    MOZ_ASSERT(nim);                                            \
    return new (nim) mozilla::dom::HTML##_elementName##Element( \
        nodeInfo.forget(), aFromParser);                        \
  }

// Here, we expand 'NS_DECLARE_NS_NEW_HTML_ELEMENT()' by hand.
// (Calling the macro directly (with no args) produces compiler warnings.)
nsGenericHTMLElement* NS_NewHTMLElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    mozilla::dom::FromParser aFromParser = mozilla::dom::NOT_FROM_PARSER);

// Distinct from the above in order to have function pointer that compared
// unequal to a function pointer to the above.
nsGenericHTMLElement* NS_NewCustomElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    mozilla::dom::FromParser aFromParser = mozilla::dom::NOT_FROM_PARSER);

NS_DECLARE_NS_NEW_HTML_ELEMENT(Shared)
NS_DECLARE_NS_NEW_HTML_ELEMENT(SharedList)

NS_DECLARE_NS_NEW_HTML_ELEMENT(Anchor)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Area)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Audio)
NS_DECLARE_NS_NEW_HTML_ELEMENT(BR)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Body)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Button)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Canvas)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Content)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Mod)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Data)
NS_DECLARE_NS_NEW_HTML_ELEMENT(DataList)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Details)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Dialog)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Div)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Embed)
NS_DECLARE_NS_NEW_HTML_ELEMENT(FieldSet)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Font)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Form)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Frame)
NS_DECLARE_NS_NEW_HTML_ELEMENT(FrameSet)
NS_DECLARE_NS_NEW_HTML_ELEMENT(HR)
NS_DECLARE_NS_NEW_HTML_ELEMENT_AS_SHARED(Head)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Heading)
NS_DECLARE_NS_NEW_HTML_ELEMENT_AS_SHARED(Html)
NS_DECLARE_NS_NEW_HTML_ELEMENT(IFrame)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Image)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Input)
NS_DECLARE_NS_NEW_HTML_ELEMENT(LI)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Label)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Legend)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Link)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Marquee)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Map)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Menu)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Meta)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Meter)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Object)
NS_DECLARE_NS_NEW_HTML_ELEMENT(OptGroup)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Option)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Output)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Paragraph)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Picture)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Pre)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Progress)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Script)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Select)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Slot)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Source)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Span)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Style)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Summary)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TableCaption)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TableCell)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TableCol)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Table)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TableRow)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TableSection)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Tbody)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Template)
NS_DECLARE_NS_NEW_HTML_ELEMENT(TextArea)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Tfoot)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Thead)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Time)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Title)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Track)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Unknown)
NS_DECLARE_NS_NEW_HTML_ELEMENT(Video)

#endif /* nsGenericHTMLElement_h___ */
