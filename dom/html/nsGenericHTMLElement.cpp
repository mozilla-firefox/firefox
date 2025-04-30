/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/EditorBase.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/FocusModel.h"
#include "mozilla/IMEContentObserver.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/FormData.h"
#include "nsCaseTreatment.h"
#include "nscore.h"
#include "nsGenericHTMLElement.h"
#include "nsCOMPtr.h"
#include "nsAtom.h"
#include "nsQueryObject.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/UnbindContext.h"
#include "nsPIDOMWindow.h"
#include "nsIFrameInlines.h"
#include "nsView.h"
#include "nsViewManager.h"
#include "nsIWidget.h"
#include "nsRange.h"
#include "nsPresContext.h"
#include "nsError.h"
#include "nsIPrincipal.h"
#include "nsContainerFrame.h"
#include "nsStyleUtil.h"
#include "ReferrerInfo.h"

#include "mozilla/PresState.h"
#include "nsILayoutHistoryState.h"

#include "nsHTMLParts.h"
#include "nsContentUtils.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "mozilla/dom/DocumentOrShadowRoot.h"
#include "nsString.h"
#include "nsGkAtoms.h"
#include "nsDOMCSSDeclaration.h"
#include "nsIFormControl.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "nsFocusManager.h"

#include "nsDOMStringMap.h"
#include "nsDOMString.h"

#include "nsLayoutUtils.h"
#include "mozilla/dom/DocumentInlines.h"
#include "HTMLFieldSetElement.h"
#include "nsTextNode.h"
#include "HTMLBRElement.h"
#include "nsDOMMutationObserver.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/ScriptLoader.h"

#include "nsDOMTokenList.h"
#include "nsThreadUtils.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/ToggleEvent.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/InputEvent.h"
#include "mozilla/dom/InvokeEvent.h"
#include "mozilla/ErrorResult.h"
#include "nsHTMLDocument.h"
#include "nsGlobalWindowInner.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "imgIContainer.h"
#include "nsComputedDOMStyle.h"
#include "mozilla/dom/HTMLDialogElement.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/ElementInternals.h"

using namespace mozilla;
using namespace mozilla::dom;

static const uint8_t NS_INPUTMODE_NONE = 1;
static const uint8_t NS_INPUTMODE_TEXT = 2;
static const uint8_t NS_INPUTMODE_TEL = 3;
static const uint8_t NS_INPUTMODE_URL = 4;
static const uint8_t NS_INPUTMODE_EMAIL = 5;
static const uint8_t NS_INPUTMODE_NUMERIC = 6;
static const uint8_t NS_INPUTMODE_DECIMAL = 7;
static const uint8_t NS_INPUTMODE_SEARCH = 8;

static constexpr nsAttrValue::EnumTable kInputmodeTable[] = {
    {"none", NS_INPUTMODE_NONE},
    {"text", NS_INPUTMODE_TEXT},
    {"tel", NS_INPUTMODE_TEL},
    {"url", NS_INPUTMODE_URL},
    {"email", NS_INPUTMODE_EMAIL},
    {"numeric", NS_INPUTMODE_NUMERIC},
    {"decimal", NS_INPUTMODE_DECIMAL},
    {"search", NS_INPUTMODE_SEARCH},
    {nullptr, 0}};

static const uint8_t NS_ENTERKEYHINT_ENTER = 1;
static const uint8_t NS_ENTERKEYHINT_DONE = 2;
static const uint8_t NS_ENTERKEYHINT_GO = 3;
static const uint8_t NS_ENTERKEYHINT_NEXT = 4;
static const uint8_t NS_ENTERKEYHINT_PREVIOUS = 5;
static const uint8_t NS_ENTERKEYHINT_SEARCH = 6;
static const uint8_t NS_ENTERKEYHINT_SEND = 7;

static constexpr nsAttrValue::EnumTable kEnterKeyHintTable[] = {
    {"enter", NS_ENTERKEYHINT_ENTER},
    {"done", NS_ENTERKEYHINT_DONE},
    {"go", NS_ENTERKEYHINT_GO},
    {"next", NS_ENTERKEYHINT_NEXT},
    {"previous", NS_ENTERKEYHINT_PREVIOUS},
    {"search", NS_ENTERKEYHINT_SEARCH},
    {"send", NS_ENTERKEYHINT_SEND},
    {nullptr, 0}};

static const uint8_t NS_AUTOCAPITALIZE_NONE = 1;
static const uint8_t NS_AUTOCAPITALIZE_SENTENCES = 2;
static const uint8_t NS_AUTOCAPITALIZE_WORDS = 3;
static const uint8_t NS_AUTOCAPITALIZE_CHARACTERS = 4;

static constexpr nsAttrValue::EnumTable kAutocapitalizeTable[] = {
    {"none", NS_AUTOCAPITALIZE_NONE},
    {"sentences", NS_AUTOCAPITALIZE_SENTENCES},
    {"words", NS_AUTOCAPITALIZE_WORDS},
    {"characters", NS_AUTOCAPITALIZE_CHARACTERS},
    {"off", NS_AUTOCAPITALIZE_NONE},
    {"on", NS_AUTOCAPITALIZE_SENTENCES},
    {"", 0},
    {nullptr, 0}};

static const nsAttrValue::EnumTable* kDefaultAutocapitalize =
    &kAutocapitalizeTable[1];

nsresult nsGenericHTMLElement::CopyInnerTo(Element* aDst) {
  MOZ_ASSERT(!aDst->GetUncomposedDoc(),
             "Should not CopyInnerTo an Element in a document");

  auto reparse = aDst->OwnerDoc() == OwnerDoc() ? ReparseAttributes::No
                                                : ReparseAttributes::Yes;
  nsresult rv = Element::CopyInnerTo(aDst, reparse);
  NS_ENSURE_SUCCESS(rv, rv);

  // cloning a node must retain its internal nonce slot
  nsString* nonce = static_cast<nsString*>(GetProperty(nsGkAtoms::nonce));
  if (nonce) {
    static_cast<nsGenericHTMLElement*>(aDst)->SetNonce(*nonce);
  }
  return NS_OK;
}

static constexpr nsAttrValue::EnumTable kDirTable[] = {
    {"ltr", Directionality::Ltr},
    {"rtl", Directionality::Rtl},
    {"auto", Directionality::Auto},
    {nullptr, 0},
};

namespace {
// See <https://html.spec.whatwg.org/#the-popover-attribute>.
enum class PopoverAttributeKeyword : uint8_t { Auto, EmptyString, Manual };

static constexpr const char kPopoverAttributeValueAuto[] = "auto";
static constexpr const char kPopoverAttributeValueEmptyString[] = "";
static constexpr const char kPopoverAttributeValueManual[] = "manual";

static constexpr nsAttrValue::EnumTable kPopoverTable[] = {
    {kPopoverAttributeValueAuto, PopoverAttributeKeyword::Auto},
    {kPopoverAttributeValueEmptyString, PopoverAttributeKeyword::EmptyString},
    {kPopoverAttributeValueManual, PopoverAttributeKeyword::Manual},
    {nullptr, 0}};

// See <https://html.spec.whatwg.org/#the-popover-attribute>.
static const nsAttrValue::EnumTable* kPopoverTableInvalidValueDefault =
    &kPopoverTable[2];
}  // namespace

void nsGenericHTMLElement::GetFetchPriority(nsAString& aFetchPriority) const {
  // <https://html.spec.whatwg.org/multipage/urls-and-fetching.html#fetch-priority-attributes>.
  GetEnumAttr(nsGkAtoms::fetchpriority, kFetchPriorityAttributeValueAuto,
              aFetchPriority);
}

/* static */
FetchPriority nsGenericHTMLElement::ToFetchPriority(const nsAString& aValue) {
  nsAttrValue attrValue;
  ParseFetchPriority(aValue, attrValue);
  MOZ_ASSERT(attrValue.Type() == nsAttrValue::eEnum);
  return FetchPriority(attrValue.GetEnumValue());
}

namespace {
// <https://html.spec.whatwg.org/multipage/urls-and-fetching.html#fetch-priority-attributes>.
static constexpr nsAttrValue::EnumTable kFetchPriorityEnumTable[] = {
    {kFetchPriorityAttributeValueHigh, FetchPriority::High},
    {kFetchPriorityAttributeValueLow, FetchPriority::Low},
    {kFetchPriorityAttributeValueAuto, FetchPriority::Auto},
    {nullptr, 0}};

// <https://html.spec.whatwg.org/multipage/urls-and-fetching.html#fetch-priority-attributes>.
static const nsAttrValue::EnumTable*
    kFetchPriorityEnumTableInvalidValueDefault = &kFetchPriorityEnumTable[2];
}  // namespace

FetchPriority nsGenericHTMLElement::GetFetchPriority() const {
  const nsAttrValue* fetchpriorityAttribute =
      GetParsedAttr(nsGkAtoms::fetchpriority);
  if (fetchpriorityAttribute) {
    MOZ_ASSERT(fetchpriorityAttribute->Type() == nsAttrValue::eEnum);
    return FetchPriority(fetchpriorityAttribute->GetEnumValue());
  }

  return FetchPriority::Auto;
}

/* static */
void nsGenericHTMLElement::ParseFetchPriority(const nsAString& aValue,
                                              nsAttrValue& aResult) {
  aResult.ParseEnumValue(aValue, kFetchPriorityEnumTable,
                         false /* aCaseSensitive */,
                         kFetchPriorityEnumTableInvalidValueDefault);
}

void nsGenericHTMLElement::AddToNameTable(nsAtom* aName) {
  MOZ_ASSERT(HasName(), "Node doesn't have name?");
  Document* doc = GetUncomposedDoc();
  if (doc && !IsInNativeAnonymousSubtree()) {
    doc->AddToNameTable(this, aName);
  }
}

void nsGenericHTMLElement::RemoveFromNameTable() {
  if (HasName() && CanHaveName(NodeInfo()->NameAtom())) {
    if (Document* doc = GetUncomposedDoc()) {
      doc->RemoveFromNameTable(this,
                               GetParsedAttr(nsGkAtoms::name)->GetAtomValue());
    }
  }
}

void nsGenericHTMLElement::GetAccessKeyLabel(nsString& aLabel) {
  nsAutoString suffix;
  GetAccessKey(suffix);
  if (!suffix.IsEmpty()) {
    EventStateManager::GetAccessKeyLabelPrefix(this, aLabel);
    aLabel.Append(suffix);
  }
}

// https://html.spec.whatwg.org/#dom-hidden
void nsGenericHTMLElement::GetHidden(
    Nullable<OwningBooleanOrUnrestrictedDoubleOrString>& aHidden) const {
  OwningBooleanOrUnrestrictedDoubleOrString value;
  // 1. If the hidden attribute is in the hidden until found state, then
  //    return "until-found".
  nsAutoString result;
  if (GetAttr(kNameSpaceID_None, nsGkAtoms::hidden, result)) {
    if (StaticPrefs::dom_hidden_until_found_enabled() &&
        result.LowerCaseEqualsLiteral("until-found")) {
      value.SetStringLiteral(u"until-found");
    } else {
      // 2. If the hidden attribute is set, then return true.
      value.SetAsBoolean() = true;
    }
  } else {
    // 3. Return false.
    value.SetAsBoolean() = false;
  }

  aHidden.SetValue(value);
}

// https://html.spec.whatwg.org/#dom-hidden
void nsGenericHTMLElement::SetHidden(
    const Nullable<BooleanOrUnrestrictedDoubleOrString>& aHidden,
    ErrorResult& aRv) {
  // 4. Otherwise, if the given value is null, then remove the hidden attribute.
  if (aHidden.IsNull()) {
    return UnsetAttr(nsGkAtoms::hidden, aRv);
  }
  bool isHidden = true;
  const auto& value = aHidden.Value();
  // 1. If the given value is a string that is an ASCII case-insensitive match
  //    for "until-found", then set the hidden attribute to "until-found".
  if (value.IsString()) {
    const nsAString& stringValue = value.GetAsString();
    // 3. Otherwise, if the given value is the empty string, then remove the
    //    hidden attribute.
    if (stringValue.IsEmpty()) {
      isHidden = false;
    } else if (StaticPrefs::dom_hidden_until_found_enabled() &&
               stringValue.LowerCaseEqualsLiteral("until-found")) {
      return SetAttr(nsGkAtoms::hidden, u"until-found"_ns, aRv);
    }
  }
  // 2. Otherwise, if the given value is false, then remove the hidden
  //    attribute.
  else if (value.IsBoolean()) {
    if (!value.GetAsBoolean()) {
      isHidden = false;
    }
  }
  // 5. Otherwise, if the given value is 0, then remove the hidden attribute.
  // 6. Otherwise, if the given value is NaN, then remove the hidden attribute.
  else if (value.IsUnrestrictedDouble()) {
    double d = value.GetAsUnrestrictedDouble();
    if (d == 0.0 || std::isnan(d)) {
      isHidden = false;
    }
  }

  // 7. Otherwise, set the hidden attribute to the empty string.
  if (isHidden) {
    aRv = SetAttr(kNameSpaceID_None, nsGkAtoms::hidden, u""_ns, true);
  } else {
    aRv = UnsetAttr(kNameSpaceID_None, nsGkAtoms::hidden, true);
  }
}

static bool IsOffsetParent(nsIFrame* aFrame) {
  LayoutFrameType frameType = aFrame->Type();

  if (frameType == LayoutFrameType::TableCell ||
      frameType == LayoutFrameType::TableWrapper) {
    // Per the IDL for Element, only td, th, and table are acceptable
    // offsetParents apart from body or positioned elements; we need to check
    // the content type as well as the frame type so we ignore anonymous tables
    // created by an element with display: table-cell with no actual table
    nsIContent* content = aFrame->GetContent();

    return content->IsAnyOfHTMLElements(nsGkAtoms::table, nsGkAtoms::td,
                                        nsGkAtoms::th);
  }
  return false;
}

struct OffsetResult {
  Element* mParent = nullptr;
  nsRect mRect;
};

static OffsetResult GetUnretargetedOffsetsFor(const Element& aElement) {
  nsIFrame* frame = aElement.GetPrimaryFrame();
  if (!frame) {
    return {};
  }

  nsIFrame* styleFrame = nsLayoutUtils::GetStyleFrame(frame);

  nsIFrame* parent = frame->GetParent();
  nsPoint origin(0, 0);

  nsIContent* offsetParent = nullptr;
  Element* docElement = aElement.GetComposedDoc()->GetRootElement();
  nsIContent* content = frame->GetContent();
  const auto effectiveZoom = frame->Style()->EffectiveZoom();

  if (content &&
      (content->IsHTMLElement(nsGkAtoms::body) || content == docElement)) {
    parent = frame;
  } else {
    const bool isPositioned = styleFrame->IsAbsPosContainingBlock();
    const bool isAbsolutelyPositioned = frame->IsAbsolutelyPositioned();
    origin += frame->GetPositionIgnoringScrolling();

    for (; parent; parent = parent->GetParent()) {
      content = parent->GetContent();

      // Stop at the first ancestor that is positioned.
      if (parent->IsAbsPosContainingBlock()) {
        offsetParent = content;
        break;
      }

      // WebKit-ism: offsetParent stops at zoom changes.
      // See https://github.com/w3c/csswg-drafts/issues/10252
      if (effectiveZoom != parent->Style()->EffectiveZoom()) {
        offsetParent = content;
        break;
      }

      // Add the parent's origin to our own to get to the
      // right coordinate system.
      const bool isOffsetParent = !isPositioned && IsOffsetParent(parent);
      if (!isOffsetParent) {
        origin += parent->GetPositionIgnoringScrolling();
      }

      if (content) {
        // If we've hit the document element, break here.
        if (content == docElement) {
          break;
        }

        // Break if the ancestor frame type makes it suitable as offset parent
        // and this element is *not* positioned or if we found the body element.
        if (isOffsetParent || content->IsHTMLElement(nsGkAtoms::body)) {
          offsetParent = content;
          break;
        }
      }
    }

    if (isAbsolutelyPositioned && !offsetParent) {
      // If this element is absolutely positioned, but we don't have
      // an offset parent it means this element is an absolutely
      // positioned child that's not nested inside another positioned
      // element, in this case the element's frame's parent is the
      // frame for the HTML element so we fail to find the body in the
      // parent chain. We want the offset parent in this case to be
      // the body, so we just get the body element from the document.
      //
      // We use GetBodyElement() here, not GetBody(), because we don't want to
      // end up with framesets here.
      offsetParent = aElement.GetComposedDoc()->GetBodyElement();
    }
  }

  // Make the position relative to the padding edge.
  if (parent) {
    const nsStyleBorder* border = parent->StyleBorder();
    origin.x -= border->GetComputedBorderWidth(eSideLeft);
    origin.y -= border->GetComputedBorderWidth(eSideTop);
  }

  // Get the union of all rectangles in this and continuation frames.
  // It doesn't really matter what we use as aRelativeTo here, since
  // we only care about the size. We just have to use something non-null.
  nsRect rcFrame = nsLayoutUtils::GetAllInFlowRectsUnion(frame, frame);
  rcFrame.MoveTo(origin);
  return {Element::FromNodeOrNull(offsetParent), rcFrame};
}

static bool ShouldBeRetargeted(const Element& aReferenceElement,
                               const Element& aElementToMaybeRetarget) {
  ShadowRoot* shadow = aElementToMaybeRetarget.GetContainingShadow();
  if (!shadow) {
    return false;
  }
  for (ShadowRoot* scope = aReferenceElement.GetContainingShadow(); scope;
       scope = scope->Host()->GetContainingShadow()) {
    if (scope == shadow) {
      return false;
    }
  }

  return true;
}

Element* nsGenericHTMLElement::GetOffsetRect(CSSIntRect& aRect) {
  aRect = CSSIntRect();

  nsIFrame* frame = GetPrimaryFrame(FlushType::Layout);
  if (!frame) {
    return nullptr;
  }

  OffsetResult thisResult = GetUnretargetedOffsetsFor(*this);
  nsRect rect = thisResult.mRect;
  Element* parent = thisResult.mParent;
  while (parent && ShouldBeRetargeted(*this, *parent)) {
    OffsetResult result = GetUnretargetedOffsetsFor(*parent);
    rect += result.mRect.TopLeft();
    parent = result.mParent;
  }

  aRect = CSSIntRect::FromAppUnitsRounded(
      frame->Style()->EffectiveZoom().Unzoom(rect));
  return parent;
}

bool nsGenericHTMLElement::Spellcheck() {
  // Has the state has been explicitly set?
  nsIContent* node;
  for (node = this; node; node = node->GetParent()) {
    if (node->IsHTMLElement()) {
      static Element::AttrValuesArray strings[] = {nsGkAtoms::_true,
                                                   nsGkAtoms::_false, nullptr};
      switch (node->AsElement()->FindAttrValueIn(
          kNameSpaceID_None, nsGkAtoms::spellcheck, strings, eCaseMatters)) {
        case 0:  // spellcheck = "true"
          return true;
        case 1:  // spellcheck = "false"
          return false;
      }
    }
  }

  // contenteditable/designMode are spellchecked by default
  if (IsEditable()) {
    return true;
  }

  // Is this a chrome element?
  if (nsContentUtils::IsChromeDoc(OwnerDoc())) {
    return false;  // Not spellchecked by default
  }

  // Anything else that's not a form control is not spellchecked by default
  const nsIFormControl* formControl = GetAsFormControl();
  if (!formControl) {
    return false;  // Not spellchecked by default
  }

  // Is this a multiline plaintext input?
  auto controlType = formControl->ControlType();
  if (controlType == FormControlType::Textarea) {
    return true;  // Spellchecked by default
  }

  // Is this anything other than an input text?
  // Other inputs are not spellchecked.
  if (controlType != FormControlType::InputText) {
    return false;  // Not spellchecked by default
  }

  // Does the user want input text spellchecked by default?
  // NOTE: Do not reflect a pref value of 0 back to the DOM getter.
  // The web page should not know if the user has disabled spellchecking.
  // We'll catch this in the editor itself.
  int32_t spellcheckLevel = StaticPrefs::layout_spellcheckDefault();
  return spellcheckLevel == 2;  // "Spellcheck multi- and single-line"
}

bool nsGenericHTMLElement::Autocorrect() const {
  return !AttrValueIs(kNameSpaceID_None, nsGkAtoms::autocorrect, nsGkAtoms::OFF,
                      eIgnoreCase);
}

bool nsGenericHTMLElement::InNavQuirksMode(Document* aDoc) {
  return aDoc && aDoc->GetCompatibilityMode() == eCompatibility_NavQuirks;
}

void nsGenericHTMLElement::UpdateEditableState(bool aNotify) {
  // XXX Should we do this only when in a document?
  ContentEditableState state = GetContentEditableState();
  if (state != ContentEditableState::Inherit) {
    SetEditableFlag(IsEditableState(state));
    UpdateReadOnlyState(aNotify);
    return;
  }
  nsStyledElement::UpdateEditableState(aNotify);
}

nsresult nsGenericHTMLElement::BindToTree(BindContext& aContext,
                                          nsINode& aParent) {
  nsresult rv = nsGenericHTMLElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (IsInComposedDoc()) {
    RegUnRegAccessKey(true);
  }

  if (IsInUncomposedDoc()) {
    Document& doc = aContext.OwnerDoc();
    if (HasName() && CanHaveName(NodeInfo()->NameAtom())) {
      doc.AddToNameTable(this, GetParsedAttr(nsGkAtoms::name)->GetAtomValue());
    }

    nsAtom* id = nullptr;
    if (ShouldExposeIdAsHTMLDocumentProperty(this)) {
      id = DoGetID();
      MOZ_ASSERT(id && id != nsGkAtoms::_empty);
      doc.AddToDocumentNameTable(this, id);
    }
    if (ShouldExposeNameAsHTMLDocumentProperty(this)) {
      nsAtom* name = GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
      MOZ_ASSERT(name && name != nsGkAtoms::_empty);
      // Make sure not to double-add if id and name are the same.
      if (id != name) {
        doc.AddToDocumentNameTable(this, name);
      }
    }
  }

  if (HasFlag(NODE_IS_EDITABLE) &&
      HasContentEditableAttrTrueOrPlainTextOnly() && IsInComposedDoc()) {
    aContext.OwnerDoc().ChangeContentEditableCount(this, +1);
  }

  // Hide any nonce from the DOM, but keep the internal value of the
  // nonce by copying and resetting the internal nonce value.
  if (HasFlag(NODE_HAS_NONCE_AND_HEADER_CSP) && IsInComposedDoc() &&
      OwnerDoc()->GetBrowsingContext()) {
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "nsGenericHTMLElement::ResetNonce::Runnable",
        [self = RefPtr<nsGenericHTMLElement>(this)]() {
          nsAutoString nonce;
          self->GetNonce(nonce);
          self->SetAttr(kNameSpaceID_None, nsGkAtoms::nonce, u""_ns, true);
          self->SetNonce(nonce);
        }));
  }

  // We need to consider a labels element is moved to another subtree
  // with different root, it needs to update labels list and its root
  // as well.
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  if (slots && slots->mLabelsList) {
    slots->mLabelsList->MaybeResetRoot(SubtreeRoot());
  }

  return rv;
}

void nsGenericHTMLElement::UnbindFromTree(UnbindContext& aContext) {
  if (IsInComposedDoc()) {
    // https://html.spec.whatwg.org/#dom-trees:hide-popover-algorithm
    // If removedNode's popover attribute is not in the no popover state, then
    // run the hide popover algorithm given removedNode, false, false, and
    // false.
    if (GetPopoverData()) {
      HidePopoverWithoutRunningScript();
    }
    RegUnRegAccessKey(false);
  }

  RemoveFromNameTable();

  if (Document* doc = GetUncomposedDoc()) {
    nsAtom* id = nullptr;
    if (ShouldExposeIdAsHTMLDocumentProperty(this)) {
      id = DoGetID();
      MOZ_ASSERT(id && id != nsGkAtoms::_empty);
      doc->RemoveFromDocumentNameTable(this, id);
    }
    if (ShouldExposeNameAsHTMLDocumentProperty(this)) {
      nsAtom* name = GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
      MOZ_ASSERT(name && name != nsGkAtoms::_empty);
      // Make sure not to double-remove if id and name are the same.
      if (id != name) {
        doc->RemoveFromDocumentNameTable(this, name);
      }
    }
  }

  if (HasContentEditableAttrTrueOrPlainTextOnly()) {
    if (Document* doc = GetComposedDoc()) {
      doc->ChangeContentEditableCount(this, -1);
    }
  }

  nsStyledElement::UnbindFromTree(aContext);

  // Invalidate .labels list. It will be repopulated when used the next time.
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  if (slots && slots->mLabelsList) {
    slots->mLabelsList->MaybeResetRoot(SubtreeRoot());
  }
}

HTMLFormElement* nsGenericHTMLElement::FindAncestorForm(
    HTMLFormElement* aCurrentForm) {
  NS_ASSERTION(!HasAttr(nsGkAtoms::form) || IsHTMLElement(nsGkAtoms::img),
               "FindAncestorForm should not be called if @form is set!");
  if (IsInNativeAnonymousSubtree()) {
    return nullptr;
  }

  nsIContent* content = this;
  while (content) {
    // If the current ancestor is a form, return it as our form
    if (content->IsHTMLElement(nsGkAtoms::form)) {
#ifdef DEBUG
      if (!nsContentUtils::IsInSameAnonymousTree(this, content)) {
        // It's possible that we started unbinding at |content| or
        // some ancestor of it, and |content| and |this| used to all be
        // anonymous.  Check for this the hard way.
        for (nsIContent* child = this; child != content;
             child = child->GetParent()) {
          NS_ASSERTION(child->ComputeIndexInParentContent().isSome(),
                       "Walked too far?");
        }
      }
#endif
      return static_cast<HTMLFormElement*>(content);
    }

    nsIContent* prevContent = content;
    content = prevContent->GetParent();

    if (!content && aCurrentForm) {
      // We got to the root of the subtree we're in, and we're being removed
      // from the DOM (the only time we get into this method with a non-null
      // aCurrentForm).  Check whether aCurrentForm is in the same subtree.  If
      // it is, we want to return aCurrentForm, since this case means that
      // we're one of those inputs-in-a-table that have a hacked mForm pointer
      // and a subtree containing both us and the form got removed from the
      // DOM.
      if (aCurrentForm->IsInclusiveDescendantOf(prevContent)) {
        return aCurrentForm;
      }
    }
  }

  return nullptr;
}

bool nsGenericHTMLElement::CheckHandleEventForAnchorsPreconditions(
    EventChainVisitor& aVisitor) {
  MOZ_ASSERT(nsCOMPtr<Link>(do_QueryObject(this)),
             "should be called only when |this| implements |Link|");
  // When disconnected, only <a> should navigate away per
  // https://html.spec.whatwg.org/#cannot-navigate
  return IsInComposedDoc() || IsHTMLElement(nsGkAtoms::a);
}

void nsGenericHTMLElement::GetEventTargetParentForAnchors(
    EventChainPreVisitor& aVisitor) {
  nsGenericHTMLElementBase::GetEventTargetParent(aVisitor);

  if (!CheckHandleEventForAnchorsPreconditions(aVisitor)) {
    return;
  }

  GetEventTargetParentForLinks(aVisitor);
}

nsresult nsGenericHTMLElement::PostHandleEventForAnchors(
    EventChainPostVisitor& aVisitor) {
  if (!CheckHandleEventForAnchorsPreconditions(aVisitor)) {
    return NS_OK;
  }

  return PostHandleEventForLinks(aVisitor);
}

bool nsGenericHTMLElement::IsHTMLLink(nsIURI** aURI) const {
  MOZ_ASSERT(aURI, "Must provide aURI out param");

  *aURI = GetHrefURIForAnchors().take();
  // We promise out param is non-null if we return true, so base rv on it
  return *aURI != nullptr;
}

already_AddRefed<nsIURI> nsGenericHTMLElement::GetHrefURIForAnchors() const {
  // This is used by the three Link implementations and
  // nsHTMLStyleElement.

  // Get href= attribute (relative URI).

  // We use the nsAttrValue's copy of the URI string to avoid copying.
  nsCOMPtr<nsIURI> uri;
  GetURIAttr(nsGkAtoms::href, nullptr, getter_AddRefs(uri));
  return uri.forget();
}

void nsGenericHTMLElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                         const nsAttrValue* aValue,
                                         bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::accesskey) {
      // Have to unregister before clearing flag. See UnregAccessKey
      RegUnRegAccessKey(false);
      if (!aValue) {
        UnsetFlags(NODE_HAS_ACCESSKEY);
      }
    } else if (aName == nsGkAtoms::name) {
      // Have to do this before clearing flag. See RemoveFromNameTable
      RemoveFromNameTable();

      nsAtom* exposedIdOnDocument = nullptr;
      Document* doc = GetUncomposedDoc();
      if (doc) {
        nsAtom* exposedNameOnDocument =
            ShouldExposeNameAsHTMLDocumentProperty(this)
                ? GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
                : nullptr;
        exposedIdOnDocument =
            ShouldExposeIdAsHTMLDocumentProperty(this) ? DoGetID() : nullptr;
        if (exposedNameOnDocument &&
            exposedNameOnDocument != exposedIdOnDocument) {
          MOZ_ASSERT(exposedNameOnDocument != nsGkAtoms::_empty);
          doc->RemoveFromDocumentNameTable(this, exposedNameOnDocument);
        }
      }
      if (!aValue || aValue->IsEmptyString()) {
        ClearHasName();
        // The result of ShouldExposeIdAsHTMLDocumentProperty() might change
        // after clearing the hasName flag.
        if (doc && exposedIdOnDocument &&
            !ShouldExposeIdAsHTMLDocumentProperty(this)) {
          doc->RemoveFromDocumentNameTable(this, exposedIdOnDocument);
        }
      }
    } else if (aName == nsGkAtoms::id) {
      if (Document* doc = GetUncomposedDoc()) {
        nsAtom* exposedIdOnDocument =
            ShouldExposeIdAsHTMLDocumentProperty(this) ? DoGetID() : nullptr;
        nsAtom* exposedNameOnDocument =
            ShouldExposeNameAsHTMLDocumentProperty(this)
                ? GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
                : nullptr;
        if (exposedIdOnDocument &&
            exposedIdOnDocument != exposedNameOnDocument) {
          doc->RemoveFromDocumentNameTable(this, exposedIdOnDocument);
        }
      }
    } else if (aName == nsGkAtoms::contenteditable) {
      if (aValue) {
        // Set this before the attribute is set so that any subclass code that
        // runs before the attribute is set won't think we're missing a
        // contenteditable attr when we actually have one.
        SetMayHaveContentEditableAttr();
      }
    }
    if (!aValue && IsEventAttributeName(aName)) {
      if (EventListenerManager* manager = GetExistingListenerManager()) {
        manager->RemoveEventHandler(GetEventNameForAttr(aName));
      }
    }
  }

  return nsGenericHTMLElementBase::BeforeSetAttr(aNamespaceID, aName, aValue,
                                                 aNotify);
}

namespace {
constexpr PopoverAttributeState ToPopoverAttributeState(
    PopoverAttributeKeyword aPopoverAttributeKeyword) {
  // See <https://html.spec.whatwg.org/#the-popover-attribute>.
  switch (aPopoverAttributeKeyword) {
    case PopoverAttributeKeyword::Auto:
      return PopoverAttributeState::Auto;
    case PopoverAttributeKeyword::EmptyString:
      return PopoverAttributeState::Auto;
    case PopoverAttributeKeyword::Manual:
      return PopoverAttributeState::Manual;
    default: {
      MOZ_ASSERT_UNREACHABLE();
      return PopoverAttributeState::None;
    }
  }
}
}  // namespace

void nsGenericHTMLElement::AfterSetPopoverAttr() {
  auto mapPopoverState = [](const nsAttrValue* value) -> PopoverAttributeState {
    if (value) {
      MOZ_ASSERT(value->Type() == nsAttrValue::eEnum);
      const auto popoverAttributeKeyword =
          static_cast<PopoverAttributeKeyword>(value->GetEnumValue());
      return ToPopoverAttributeState(popoverAttributeKeyword);
    }

    // The missing value default is the no popover state, see
    // <https://html.spec.whatwg.org/multipage/popover.html#attr-popover>.
    return PopoverAttributeState::None;
  };

  PopoverAttributeState newState =
      mapPopoverState(GetParsedAttr(nsGkAtoms::popover));

  const PopoverAttributeState oldState = GetPopoverAttributeState();

  if (newState != oldState) {
    PopoverPseudoStateUpdate(false, true);

    if (IsPopoverOpen()) {
      HidePopoverInternal(/* aFocusPreviousElement = */ true,
                          /* aFireEvents = */ true, IgnoreErrors());
      // Event handlers could have removed the popover attribute, or changed
      // its value.
      // https://github.com/whatwg/html/issues/9034
      newState = mapPopoverState(GetParsedAttr(nsGkAtoms::popover));
    }

    if (newState == PopoverAttributeState::None) {
      ClearPopoverData();
      RemoveStates(ElementState::POPOVER_OPEN);
    } else {
      // TODO: what if `HidePopoverInternal` called `ShowPopup()`?
      EnsurePopoverData().SetPopoverAttributeState(newState);
    }
  }
}

void nsGenericHTMLElement::OnAttrSetButNotChanged(
    int32_t aNamespaceID, nsAtom* aName, const nsAttrValueOrString& aValue,
    bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None && aName == nsGkAtoms::popovertarget) {
    ClearExplicitlySetAttrElement(aName);
  }
  return nsGenericHTMLElementBase::OnAttrSetButNotChanged(aNamespaceID, aName,
                                                          aValue, aNotify);
}

void nsGenericHTMLElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                        const nsAttrValue* aValue,
                                        const nsAttrValue* aOldValue,
                                        nsIPrincipal* aMaybeScriptedPrincipal,
                                        bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (IsEventAttributeName(aName) && aValue) {
      MOZ_ASSERT(aValue->Type() == nsAttrValue::eString,
                 "Expected string value for script body");
      SetEventHandler(GetEventNameForAttr(aName), aValue->GetStringValue());
    } else if (aNotify && aName == nsGkAtoms::spellcheck) {
      SyncEditorsOnSubtree(this);
    } else if (aName == nsGkAtoms::popover) {
      nsContentUtils::AddScriptRunner(
          NewRunnableMethod("nsGenericHTMLElement::AfterSetPopoverAttr", this,
                            &nsGenericHTMLElement::AfterSetPopoverAttr));
    } else if (aName == nsGkAtoms::popovertarget) {
      ClearExplicitlySetAttrElement(aName);
    } else if (aName == nsGkAtoms::dir) {
      auto dir = Directionality::Ltr;
      // A boolean tracking whether we need to recompute our directionality.
      // This needs to happen after we update our internal "dir" attribute
      // state but before we call SetDirectionalityOnDescendants.
      bool recomputeDirectionality = false;
      ElementState dirStates;
      if (aValue && aValue->Type() == nsAttrValue::eEnum) {
        SetHasValidDir();
        dirStates |= ElementState::HAS_DIR_ATTR;
        auto dirValue = Directionality(aValue->GetEnumValue());
        if (dirValue == Directionality::Auto) {
          dirStates |= ElementState::HAS_DIR_ATTR_LIKE_AUTO;
        } else {
          dir = dirValue;
          SetDirectionality(dir, aNotify);
          if (dirValue == Directionality::Ltr) {
            dirStates |= ElementState::HAS_DIR_ATTR_LTR;
          } else {
            MOZ_ASSERT(dirValue == Directionality::Rtl);
            dirStates |= ElementState::HAS_DIR_ATTR_RTL;
          }
        }
      } else {
        if (aValue) {
          // We have a value, just not a valid one.
          dirStates |= ElementState::HAS_DIR_ATTR;
        }
        ClearHasValidDir();
        if (NodeInfo()->Equals(nsGkAtoms::bdi)) {
          dirStates |= ElementState::HAS_DIR_ATTR_LIKE_AUTO;
        } else {
          recomputeDirectionality = true;
        }
      }
      // Now figure out what's changed about our dir states.
      ElementState oldDirStates = State() & ElementState::DIR_ATTR_STATES;
      ElementState changedStates = dirStates ^ oldDirStates;
      if (!changedStates.IsEmpty()) {
        ToggleStates(changedStates, aNotify);
      }
      if (recomputeDirectionality) {
        dir = RecomputeDirectionality(this, aNotify);
      }
      SetDirectionalityOnDescendants(this, dir, aNotify);
    } else if (aName == nsGkAtoms::contenteditable) {
      const auto IsEditableExceptInherit = [](const nsAttrValue& aValue) {
        return aValue.Equals(EmptyString(), eCaseMatters) ||
               aValue.Equals(u"true"_ns, eIgnoreCase) ||
               aValue.Equals(u"plaintext-only"_ns, eIgnoreCase);
      };
      // FYI: Now, both HasContentEditableAttrTrueOrPlainTextOnly() and
      // HasContentEditableAttrFalse() return true.  Therefore, we need to clear
      // one of them or both of them.
      int32_t editableCountDelta = 0;
      if (aOldValue && IsEditableExceptInherit(*aOldValue)) {
        editableCountDelta = -1;
        ClearHasContentEditableAttrTrueOrPlainTextOnly();
      }
      if (!aValue) {
        ClearMayHaveContentEditableAttr();
      } else if (IsEditableExceptInherit(*aValue)) {
        ++editableCountDelta;
        SetHasContentEditableAttrTrueOrPlainTextOnly();
      }
      ChangeEditableState(editableCountDelta);
    } else if (aName == nsGkAtoms::accesskey) {
      if (aValue && !aValue->Equals(u""_ns, eIgnoreCase)) {
        SetFlags(NODE_HAS_ACCESSKEY);
        RegUnRegAccessKey(true);
      }
    } else if (aName == nsGkAtoms::inert) {
      if (aValue) {
        AddStates(ElementState::INERT);
      } else {
        RemoveStates(ElementState::INERT);
      }
    } else if (aName == nsGkAtoms::name) {
      if (aValue && !aValue->Equals(u""_ns, eIgnoreCase)) {
        // This may not be quite right because we can have subclass code run
        // before here. But in practice subclasses don't care about this flag,
        // and in particular selector matching does not care.  Otherwise we'd
        // want to handle it like we handle id attributes (in PreIdMaybeChange
        // and PostIdMaybeChange).
        SetHasName();
        if (CanHaveName(NodeInfo()->NameAtom())) {
          AddToNameTable(aValue->GetAtomValue());
        }
        if (Document* doc = GetUncomposedDoc()) {
          if (ShouldExposeNameAsHTMLDocumentProperty(this)) {
            nsAtom* id = ShouldExposeIdAsHTMLDocumentProperty(this) ? DoGetID()
                                                                    : nullptr;
            nsAtom* name = aValue->GetAtomValue();
            // Make sure not to double-add if id and name are the same
            if (id != name) {
              doc->AddToDocumentNameTable(this, name);
            }
          }
        }
      }
    } else if (aName == nsGkAtoms::id) {
      if (Document* doc = GetUncomposedDoc()) {
        if (ShouldExposeIdAsHTMLDocumentProperty(this)) {
          nsAtom* id = aValue->GetAtomValue();
          nsAtom* name = ShouldExposeNameAsHTMLDocumentProperty(this)
                             ? GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
                             : nullptr;
          if (id != name) {
            doc->AddToDocumentNameTable(this, id);
          }
        }
      }
    } else if (aName == nsGkAtoms::inputmode ||
               aName == nsGkAtoms::enterkeyhint) {
      if (nsFocusManager::GetFocusedElementStatic() == this) {
        if (const nsPresContext* presContext =
                GetPresContext(eForComposedDoc)) {
          IMEContentObserver* observer =
              IMEStateManager::GetActiveContentObserver();
          if (observer && observer->IsObserving(*presContext, this)) {
            if (const RefPtr<EditorBase> editorBase = GetExtantEditor()) {
              IMEState newState;
              editorBase->GetPreferredIMEState(&newState);
              OwningNonNull<nsGenericHTMLElement> kungFuDeathGrip(*this);
              IMEStateManager::UpdateIMEState(
                  newState, kungFuDeathGrip, *editorBase,
                  {IMEStateManager::UpdateIMEStateOption::ForceUpdate,
                   IMEStateManager::UpdateIMEStateOption::
                       DontCommitComposition});
            }
          }
        }
      }
    }

    // The nonce will be copied over to an internal slot and cleared from the
    // Element within BindToTree to avoid CSS Selector nonce exfiltration if
    // the CSP list contains a header-delivered CSP.
    if (nsGkAtoms::nonce == aName) {
      if (aValue) {
        SetNonce(aValue->GetStringValue());
        if (OwnerDoc()->GetHasCSPDeliveredThroughHeader()) {
          SetFlags(NODE_HAS_NONCE_AND_HEADER_CSP);
        }
      } else {
        RemoveNonce();
      }
    }
  }

  return nsGenericHTMLElementBase::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

EventListenerManager* nsGenericHTMLElement::GetEventListenerManagerForAttr(
    nsAtom* aAttrName, bool* aDefer) {
  // Attributes on the body and frameset tags get set on the global object
  if ((mNodeInfo->Equals(nsGkAtoms::body) ||
       mNodeInfo->Equals(nsGkAtoms::frameset)) &&
      // We only forward some event attributes from body/frameset to window
      (0
#define EVENT(name_, id_, type_, struct_) /* nothing */
#define FORWARDED_EVENT(name_, id_, type_, struct_) \
  || nsGkAtoms::on##name_ == aAttrName
#define WINDOW_EVENT FORWARDED_EVENT
#include "mozilla/EventNameList.h"  // IWYU pragma: keep
#undef WINDOW_EVENT
#undef FORWARDED_EVENT
#undef EVENT
       )) {
    nsPIDOMWindowInner* win;

    // If we have a document, and it has a window, add the event
    // listener on the window (the inner window). If not, proceed as
    // normal.
    // XXXbz sXBL/XBL2 issue: should we instead use GetComposedDoc() here,
    // override BindToTree for those classes and munge event listeners there?
    Document* document = OwnerDoc();

    *aDefer = false;
    if ((win = document->GetInnerWindow())) {
      nsCOMPtr<EventTarget> piTarget(do_QueryInterface(win));

      return piTarget->GetOrCreateListenerManager();
    }

    return nullptr;
  }

  return nsGenericHTMLElementBase::GetEventListenerManagerForAttr(aAttrName,
                                                                  aDefer);
}

#define EVENT(name_, id_, type_, struct_) /* nothing; handled by nsINode */
#define FORWARDED_EVENT(name_, id_, type_, struct_)                       \
  EventHandlerNonNull* nsGenericHTMLElement::GetOn##name_() {             \
    if (IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {      \
      /* XXXbz note to self: add tests for this! */                       \
      if (nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow()) {       \
        nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win);  \
        return globalWin->GetOn##name_();                                 \
      }                                                                   \
      return nullptr;                                                     \
    }                                                                     \
                                                                          \
    return nsINode::GetOn##name_();                                       \
  }                                                                       \
  void nsGenericHTMLElement::SetOn##name_(EventHandlerNonNull* handler) { \
    if (IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {      \
      nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();             \
      if (!win) {                                                         \
        return;                                                           \
      }                                                                   \
                                                                          \
      nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win);    \
      return globalWin->SetOn##name_(handler);                            \
    }                                                                     \
                                                                          \
    return nsINode::SetOn##name_(handler);                                \
  }
#define ERROR_EVENT(name_, id_, type_, struct_)                                \
  already_AddRefed<EventHandlerNonNull> nsGenericHTMLElement::GetOn##name_() { \
    if (IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {           \
      /* XXXbz note to self: add tests for this! */                            \
      if (nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow()) {            \
        nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win);       \
        OnErrorEventHandlerNonNull* errorHandler = globalWin->GetOn##name_();  \
        if (errorHandler) {                                                    \
          RefPtr<EventHandlerNonNull> handler =                                \
              new EventHandlerNonNull(errorHandler);                           \
          return handler.forget();                                             \
        }                                                                      \
      }                                                                        \
      return nullptr;                                                          \
    }                                                                          \
                                                                               \
    RefPtr<EventHandlerNonNull> handler = nsINode::GetOn##name_();             \
    return handler.forget();                                                   \
  }                                                                            \
  void nsGenericHTMLElement::SetOn##name_(EventHandlerNonNull* handler) {      \
    if (IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {           \
      nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();                  \
      if (!win) {                                                              \
        return;                                                                \
      }                                                                        \
                                                                               \
      nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win);         \
      RefPtr<OnErrorEventHandlerNonNull> errorHandler;                         \
      if (handler) {                                                           \
        errorHandler = new OnErrorEventHandlerNonNull(handler);                \
      }                                                                        \
      return globalWin->SetOn##name_(errorHandler);                            \
    }                                                                          \
                                                                               \
    return nsINode::SetOn##name_(handler);                                     \
  }
#include "mozilla/EventNameList.h"  // IWYU pragma: keep
#undef ERROR_EVENT
#undef FORWARDED_EVENT
#undef EVENT

void nsGenericHTMLElement::GetBaseTarget(nsAString& aBaseTarget) const {
  OwnerDoc()->GetBaseTarget(aBaseTarget);
}

//----------------------------------------------------------------------

bool nsGenericHTMLElement::ParseAttribute(int32_t aNamespaceID,
                                          nsAtom* aAttribute,
                                          const nsAString& aValue,
                                          nsIPrincipal* aMaybeScriptedPrincipal,
                                          nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::dir) {
      return aResult.ParseEnumValue(aValue, kDirTable, false);
    }

    if (aAttribute == nsGkAtoms::popover) {
      return aResult.ParseEnumValue(aValue, kPopoverTable, false,
                                    kPopoverTableInvalidValueDefault);
    }

    if (aAttribute == nsGkAtoms::tabindex) {
      return aResult.ParseIntValue(aValue);
    }

    if (aAttribute == nsGkAtoms::referrerpolicy) {
      return ParseReferrerAttribute(aValue, aResult);
    }

    if (aAttribute == nsGkAtoms::name) {
      // Store name as an atom.  name="" means that the element has no name,
      // not that it has an empty string as the name.
      if (aValue.IsEmpty()) {
        return false;
      }
      aResult.ParseAtom(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::contenteditable ||
        aAttribute == nsGkAtoms::translate) {
      aResult.ParseAtom(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::rel) {
      aResult.ParseAtomArray(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::inputmode) {
      return aResult.ParseEnumValue(aValue, kInputmodeTable, false);
    }

    if (aAttribute == nsGkAtoms::enterkeyhint) {
      return aResult.ParseEnumValue(aValue, kEnterKeyHintTable, false);
    }

    if (aAttribute == nsGkAtoms::autocapitalize) {
      return aResult.ParseEnumValue(aValue, kAutocapitalizeTable, false);
    }
  }

  return nsGenericHTMLElementBase::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

bool nsGenericHTMLElement::ParseBackgroundAttribute(int32_t aNamespaceID,
                                                    nsAtom* aAttribute,
                                                    const nsAString& aValue,
                                                    nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::background && !aValue.IsEmpty()) {
    // Resolve url to an absolute url
    Document* doc = OwnerDoc();
    nsCOMPtr<nsIURI> uri;
    nsresult rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(uri), aValue, doc, GetBaseURI());
    if (NS_FAILED(rv)) {
      return false;
    }
    aResult.SetTo(uri, &aValue);
    return true;
  }

  return false;
}

bool nsGenericHTMLElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  static const MappedAttributeEntry* const map[] = {sCommonAttributeMap};

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc nsGenericHTMLElement::GetAttributeMappingFunction()
    const {
  return &MapCommonAttributesInto;
}

static constexpr nsAttrValue::EnumTable kDivAlignTable[] = {
    {"left", StyleTextAlign::MozLeft},
    {"right", StyleTextAlign::MozRight},
    {"center", StyleTextAlign::MozCenter},
    {"middle", StyleTextAlign::MozCenter},
    {"justify", StyleTextAlign::Justify},
    {nullptr, 0}};

static constexpr nsAttrValue::EnumTable kFrameborderTable[] = {
    {"yes", FrameBorderProperty::Yes},
    {"no", FrameBorderProperty::No},
    {"1", FrameBorderProperty::One},
    {"0", FrameBorderProperty::Zero},
    {nullptr, 0}};

// TODO(emilio): Nobody uses the parsed attribute here.
static constexpr nsAttrValue::EnumTable kScrollingTable[] = {
    {"yes", ScrollingAttribute::Yes},
    {"no", ScrollingAttribute::No},
    {"on", ScrollingAttribute::On},
    {"off", ScrollingAttribute::Off},
    {"scroll", ScrollingAttribute::Scroll},
    {"noscroll", ScrollingAttribute::Noscroll},
    {"auto", ScrollingAttribute::Auto},
    {nullptr, 0}};

static constexpr nsAttrValue::EnumTable kTableVAlignTable[] = {
    {"top", StyleVerticalAlignKeyword::Top},
    {"middle", StyleVerticalAlignKeyword::Middle},
    {"bottom", StyleVerticalAlignKeyword::Bottom},
    {"baseline", StyleVerticalAlignKeyword::Baseline},
    {nullptr, 0}};

bool nsGenericHTMLElement::ParseAlignValue(const nsAString& aString,
                                           nsAttrValue& aResult) {
  static constexpr nsAttrValue::EnumTable kAlignTable[] = {
      {"left", StyleTextAlign::Left},
      {"right", StyleTextAlign::Right},

      {"top", StyleVerticalAlignKeyword::Top},
      {"middle", StyleVerticalAlignKeyword::MozMiddleWithBaseline},

      // Intentionally not bottom.
      {"bottom", StyleVerticalAlignKeyword::Baseline},

      {"center", StyleVerticalAlignKeyword::MozMiddleWithBaseline},
      {"baseline", StyleVerticalAlignKeyword::Baseline},

      {"texttop", StyleVerticalAlignKeyword::TextTop},
      {"absmiddle", StyleVerticalAlignKeyword::Middle},
      {"abscenter", StyleVerticalAlignKeyword::Middle},
      {"absbottom", StyleVerticalAlignKeyword::Bottom},
      {nullptr, 0}};

  static_assert(uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::Top) &&
                uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::MozMiddleWithBaseline) &&
                uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::Baseline) &&
                uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::TextTop) &&
                uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::Middle) &&
                uint8_t(StyleTextAlign::Left) !=
                    uint8_t(StyleVerticalAlignKeyword::Bottom));

  static_assert(uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::Top) &&
                uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::MozMiddleWithBaseline) &&
                uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::Baseline) &&
                uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::TextTop) &&
                uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::Middle) &&
                uint8_t(StyleTextAlign::Right) !=
                    uint8_t(StyleVerticalAlignKeyword::Bottom));

  return aResult.ParseEnumValue(aString, kAlignTable, false);
}

//----------------------------------------

static constexpr nsAttrValue::EnumTable kTableHAlignTable[] = {
    {"left", StyleTextAlign::Left},
    {"right", StyleTextAlign::Right},
    {"center", StyleTextAlign::Center},
    {"justify", StyleTextAlign::Justify},
    {nullptr, 0}};

bool nsGenericHTMLElement::ParseTableHAlignValue(const nsAString& aString,
                                                 nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kTableHAlignTable, false);
}

//----------------------------------------

// This table is used for td, th, tr, col, thead, tbody and tfoot.
static constexpr nsAttrValue::EnumTable kTableCellHAlignTable[] = {
    {"left", StyleTextAlign::MozLeft},
    {"right", StyleTextAlign::MozRight},
    {"center", StyleTextAlign::MozCenter},
    {"justify", StyleTextAlign::Justify},
    {"middle", StyleTextAlign::MozCenter},
    {"absmiddle", StyleTextAlign::Center},
    {nullptr, 0}};

bool nsGenericHTMLElement::ParseTableCellHAlignValue(const nsAString& aString,
                                                     nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kTableCellHAlignTable, false);
}

//----------------------------------------

bool nsGenericHTMLElement::ParseTableVAlignValue(const nsAString& aString,
                                                 nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kTableVAlignTable, false);
}

bool nsGenericHTMLElement::ParseDivAlignValue(const nsAString& aString,
                                              nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kDivAlignTable, false);
}

bool nsGenericHTMLElement::ParseImageAttribute(nsAtom* aAttribute,
                                               const nsAString& aString,
                                               nsAttrValue& aResult) {
  if (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height ||
      aAttribute == nsGkAtoms::hspace || aAttribute == nsGkAtoms::vspace) {
    return aResult.ParseHTMLDimension(aString);
  }
  if (aAttribute == nsGkAtoms::border) {
    return aResult.ParseNonNegativeIntValue(aString);
  }
  return false;
}

bool nsGenericHTMLElement::ParseReferrerAttribute(const nsAString& aString,
                                                  nsAttrValue& aResult) {
  using mozilla::dom::ReferrerInfo;
  static constexpr nsAttrValue::EnumTable kReferrerPolicyTable[] = {
      {GetEnumString(ReferrerPolicy::No_referrer).get(),
       static_cast<int16_t>(ReferrerPolicy::No_referrer)},
      {GetEnumString(ReferrerPolicy::Origin).get(),
       static_cast<int16_t>(ReferrerPolicy::Origin)},
      {GetEnumString(ReferrerPolicy::Origin_when_cross_origin).get(),
       static_cast<int16_t>(ReferrerPolicy::Origin_when_cross_origin)},
      {GetEnumString(ReferrerPolicy::No_referrer_when_downgrade).get(),
       static_cast<int16_t>(ReferrerPolicy::No_referrer_when_downgrade)},
      {GetEnumString(ReferrerPolicy::Unsafe_url).get(),
       static_cast<int16_t>(ReferrerPolicy::Unsafe_url)},
      {GetEnumString(ReferrerPolicy::Strict_origin).get(),
       static_cast<int16_t>(ReferrerPolicy::Strict_origin)},
      {GetEnumString(ReferrerPolicy::Same_origin).get(),
       static_cast<int16_t>(ReferrerPolicy::Same_origin)},
      {GetEnumString(ReferrerPolicy::Strict_origin_when_cross_origin).get(),
       static_cast<int16_t>(ReferrerPolicy::Strict_origin_when_cross_origin)},
      {nullptr, ReferrerPolicy::_empty}};
  return aResult.ParseEnumValue(aString, kReferrerPolicyTable, false);
}

bool nsGenericHTMLElement::ParseFrameborderValue(const nsAString& aString,
                                                 nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kFrameborderTable, false);
}

bool nsGenericHTMLElement::ParseScrollingValue(const nsAString& aString,
                                               nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aString, kScrollingTable, false);
}

static inline void MapLangAttributeInto(MappedDeclarationsBuilder& aBuilder) {
  const nsAttrValue* langValue = aBuilder.GetAttr(nsGkAtoms::lang);
  if (!langValue) {
    return;
  }
  MOZ_ASSERT(langValue->Type() == nsAttrValue::eAtom);
  aBuilder.SetIdentAtomValueIfUnset(eCSSProperty__x_lang,
                                    langValue->GetAtomValue());
  if (!aBuilder.PropertyIsSet(eCSSProperty_text_emphasis_position)) {
    const nsAtom* lang = langValue->GetAtomValue();
    if (nsStyleUtil::MatchesLanguagePrefix(lang, u"zh")) {
      aBuilder.SetKeywordValue(eCSSProperty_text_emphasis_position,
                               StyleTextEmphasisPosition::UNDER._0);
    } else if (nsStyleUtil::MatchesLanguagePrefix(lang, u"ja") ||
               nsStyleUtil::MatchesLanguagePrefix(lang, u"mn")) {
      // This branch is currently no part of the spec.
      // See bug 1040668 comment 69 and comment 75.
      aBuilder.SetKeywordValue(eCSSProperty_text_emphasis_position,
                               StyleTextEmphasisPosition::OVER._0);
    }
  }
}

/**
 * Handle attributes common to all html elements
 */
void nsGenericHTMLElement::MapCommonAttributesIntoExceptHidden(
    MappedDeclarationsBuilder& aBuilder) {
  MapLangAttributeInto(aBuilder);
}

void nsGenericHTMLElement::MapCommonAttributesInto(
    MappedDeclarationsBuilder& aBuilder) {
  MapCommonAttributesIntoExceptHidden(aBuilder);
  MOZ_ASSERT(!aBuilder.PropertyIsSet(eCSSProperty_display));
  MOZ_ASSERT(!aBuilder.PropertyIsSet(eCSSProperty_content_visibility));

  if (const nsAttrValue* hidden = aBuilder.GetAttr(nsGkAtoms::hidden)) {
    if (StaticPrefs::dom_hidden_until_found_enabled() &&
        hidden->Equals(nsGkAtoms::untilFound, eIgnoreCase)) {
      aBuilder.SetKeywordValue(eCSSProperty_content_visibility,
                               StyleContentVisibility::Hidden);
    } else {
      aBuilder.SetKeywordValue(eCSSProperty_display, StyleDisplay::None._0);
    }
  }
}

/* static */
const nsGenericHTMLElement::MappedAttributeEntry
    nsGenericHTMLElement::sCommonAttributeMap[] = {{nsGkAtoms::contenteditable},
                                                   {nsGkAtoms::lang},
                                                   {nsGkAtoms::hidden},
                                                   {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sImageMarginSizeAttributeMap[] = {{nsGkAtoms::width},
                                                            {nsGkAtoms::height},
                                                            {nsGkAtoms::hspace},
                                                            {nsGkAtoms::vspace},
                                                            {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sImageAlignAttributeMap[] = {{nsGkAtoms::align},
                                                       {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sDivAlignAttributeMap[] = {{nsGkAtoms::align},
                                                     {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sImageBorderAttributeMap[] = {{nsGkAtoms::border},
                                                        {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sBackgroundAttributeMap[] = {
        {nsGkAtoms::background}, {nsGkAtoms::bgcolor}, {nullptr}};

/* static */
const Element::MappedAttributeEntry
    nsGenericHTMLElement::sBackgroundColorAttributeMap[] = {
        {nsGkAtoms::bgcolor}, {nullptr}};

void nsGenericHTMLElement::MapImageAlignAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::align);
  if (value && value->Type() == nsAttrValue::eEnum) {
    int32_t align = value->GetEnumValue();
    if (!aBuilder.PropertyIsSet(eCSSProperty_float)) {
      if (align == uint8_t(StyleTextAlign::Left)) {
        aBuilder.SetKeywordValue(eCSSProperty_float, StyleFloat::Left);
      } else if (align == uint8_t(StyleTextAlign::Right)) {
        aBuilder.SetKeywordValue(eCSSProperty_float, StyleFloat::Right);
      }
    }
    if (!aBuilder.PropertyIsSet(eCSSProperty_vertical_align)) {
      switch (align) {
        case uint8_t(StyleTextAlign::Left):
        case uint8_t(StyleTextAlign::Right):
          break;
        default:
          aBuilder.SetKeywordValue(eCSSProperty_vertical_align, align);
          break;
      }
    }
  }
}

void nsGenericHTMLElement::MapDivAlignAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  if (!aBuilder.PropertyIsSet(eCSSProperty_text_align)) {
    // align: enum
    const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::align);
    if (value && value->Type() == nsAttrValue::eEnum)
      aBuilder.SetKeywordValue(eCSSProperty_text_align, value->GetEnumValue());
  }
}

void nsGenericHTMLElement::MapVAlignAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  if (!aBuilder.PropertyIsSet(eCSSProperty_vertical_align)) {
    // align: enum
    const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::valign);
    if (value && value->Type() == nsAttrValue::eEnum)
      aBuilder.SetKeywordValue(eCSSProperty_vertical_align,
                               value->GetEnumValue());
  }
}

void nsGenericHTMLElement::MapDimensionAttributeInto(
    MappedDeclarationsBuilder& aBuilder, nsCSSPropertyID aProp,
    const nsAttrValue& aValue) {
  MOZ_ASSERT(!aBuilder.PropertyIsSet(aProp),
             "Why mapping the same property twice?");
  if (aValue.Type() == nsAttrValue::eInteger) {
    return aBuilder.SetPixelValue(aProp, aValue.GetIntegerValue());
  }
  if (aValue.Type() == nsAttrValue::ePercent) {
    return aBuilder.SetPercentValue(aProp, aValue.GetPercentValue());
  }
  if (aValue.Type() == nsAttrValue::eDoubleValue) {
    return aBuilder.SetPixelValue(aProp, aValue.GetDoubleValue());
  }
}

void nsGenericHTMLElement::MapImageMarginAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  // hspace: value
  if (const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::hspace)) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_margin_left, *value);
    MapDimensionAttributeInto(aBuilder, eCSSProperty_margin_right, *value);
  }

  // vspace: value
  if (const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::vspace)) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_margin_top, *value);
    MapDimensionAttributeInto(aBuilder, eCSSProperty_margin_bottom, *value);
  }
}

void nsGenericHTMLElement::MapWidthAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  if (const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::width)) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_width, *value);
  }
}

void nsGenericHTMLElement::MapHeightAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  if (const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::height)) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_height, *value);
  }
}

void nsGenericHTMLElement::DoMapAspectRatio(
    const nsAttrValue& aWidth, const nsAttrValue& aHeight,
    MappedDeclarationsBuilder& aBuilder) {
  Maybe<double> w;
  if (aWidth.Type() == nsAttrValue::eInteger) {
    w.emplace(aWidth.GetIntegerValue());
  } else if (aWidth.Type() == nsAttrValue::eDoubleValue) {
    w.emplace(aWidth.GetDoubleValue());
  }

  Maybe<double> h;
  if (aHeight.Type() == nsAttrValue::eInteger) {
    h.emplace(aHeight.GetIntegerValue());
  } else if (aHeight.Type() == nsAttrValue::eDoubleValue) {
    h.emplace(aHeight.GetDoubleValue());
  }

  if (w && h) {
    aBuilder.SetAspectRatio(*w, *h);
  }
}

void nsGenericHTMLElement::MapImageSizeAttributesInto(
    MappedDeclarationsBuilder& aBuilder, MapAspectRatio aMapAspectRatio) {
  auto* width = aBuilder.GetAttr(nsGkAtoms::width);
  auto* height = aBuilder.GetAttr(nsGkAtoms::height);
  if (width) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_width, *width);
  }
  if (height) {
    MapDimensionAttributeInto(aBuilder, eCSSProperty_height, *height);
  }
  if (aMapAspectRatio == MapAspectRatio::Yes && width && height) {
    DoMapAspectRatio(*width, *height, aBuilder);
  }
}

void nsGenericHTMLElement::MapAspectRatioInto(
    MappedDeclarationsBuilder& aBuilder) {
  auto* width = aBuilder.GetAttr(nsGkAtoms::width);
  auto* height = aBuilder.GetAttr(nsGkAtoms::height);
  if (width && height) {
    DoMapAspectRatio(*width, *height, aBuilder);
  }
}

void nsGenericHTMLElement::MapImageBorderAttributeInto(
    MappedDeclarationsBuilder& aBuilder) {
  // border: pixels
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::border);
  if (!value) return;

  nscoord val = 0;
  if (value->Type() == nsAttrValue::eInteger) val = value->GetIntegerValue();

  aBuilder.SetPixelValueIfUnset(eCSSProperty_border_top_width, (float)val);
  aBuilder.SetPixelValueIfUnset(eCSSProperty_border_right_width, (float)val);
  aBuilder.SetPixelValueIfUnset(eCSSProperty_border_bottom_width, (float)val);
  aBuilder.SetPixelValueIfUnset(eCSSProperty_border_left_width, (float)val);

  aBuilder.SetKeywordValueIfUnset(eCSSProperty_border_top_style,
                                  StyleBorderStyle::Solid);
  aBuilder.SetKeywordValueIfUnset(eCSSProperty_border_right_style,
                                  StyleBorderStyle::Solid);
  aBuilder.SetKeywordValueIfUnset(eCSSProperty_border_bottom_style,
                                  StyleBorderStyle::Solid);
  aBuilder.SetKeywordValueIfUnset(eCSSProperty_border_left_style,
                                  StyleBorderStyle::Solid);

  aBuilder.SetCurrentColorIfUnset(eCSSProperty_border_top_color);
  aBuilder.SetCurrentColorIfUnset(eCSSProperty_border_right_color);
  aBuilder.SetCurrentColorIfUnset(eCSSProperty_border_bottom_color);
  aBuilder.SetCurrentColorIfUnset(eCSSProperty_border_left_color);
}

void nsGenericHTMLElement::MapBackgroundInto(
    MappedDeclarationsBuilder& aBuilder) {
  if (!aBuilder.PropertyIsSet(eCSSProperty_background_image)) {
    // background
    if (const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::background)) {
      aBuilder.SetBackgroundImage(*value);
    }
  }
}

void nsGenericHTMLElement::MapBGColorInto(MappedDeclarationsBuilder& aBuilder) {
  if (aBuilder.PropertyIsSet(eCSSProperty_background_color)) {
    return;
  }
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::bgcolor);
  nscolor color;
  if (value && value->GetColorValue(color)) {
    aBuilder.SetColorValue(eCSSProperty_background_color, color);
  }
}

void nsGenericHTMLElement::MapBackgroundAttributesInto(
    MappedDeclarationsBuilder& aBuilder) {
  MapBackgroundInto(aBuilder);
  MapBGColorInto(aBuilder);
}

//----------------------------------------------------------------------

int32_t nsGenericHTMLElement::GetIntAttr(nsAtom* aAttr,
                                         int32_t aDefault) const {
  const nsAttrValue* attrVal = mAttrs.GetAttr(aAttr);
  if (attrVal && attrVal->Type() == nsAttrValue::eInteger) {
    return attrVal->GetIntegerValue();
  }
  return aDefault;
}

nsresult nsGenericHTMLElement::SetIntAttr(nsAtom* aAttr, int32_t aValue) {
  nsAutoString value;
  value.AppendInt(aValue);

  return SetAttr(kNameSpaceID_None, aAttr, value, true);
}

uint32_t nsGenericHTMLElement::GetUnsignedIntAttr(nsAtom* aAttr,
                                                  uint32_t aDefault) const {
  const nsAttrValue* attrVal = mAttrs.GetAttr(aAttr);
  if (!attrVal || attrVal->Type() != nsAttrValue::eInteger) {
    return aDefault;
  }

  return attrVal->GetIntegerValue();
}

uint32_t nsGenericHTMLElement::GetDimensionAttrAsUnsignedInt(
    nsAtom* aAttr, uint32_t aDefault) const {
  const nsAttrValue* attrVal = mAttrs.GetAttr(aAttr);
  if (!attrVal) {
    return aDefault;
  }

  if (attrVal->Type() == nsAttrValue::eInteger) {
    return attrVal->GetIntegerValue();
  }

  if (attrVal->Type() == nsAttrValue::ePercent) {
    // This is a nasty hack.  When we parsed the value, we stored it as an
    // ePercent, not eInteger, because there was a '%' after it in the string.
    // But the spec says to basically re-parse the string as an integer.
    // Luckily, we can just return the value we have stored.  But
    // GetPercentValue() divides it by 100, so we need to multiply it back.
    return uint32_t(attrVal->GetPercentValue() * 100.0f);
  }

  if (attrVal->Type() == nsAttrValue::eDoubleValue) {
    return uint32_t(attrVal->GetDoubleValue());
  }

  // Unfortunately, the set of values that are valid dimensions is not a
  // superset of values that are valid unsigned ints.  In particular "+100" is
  // not a valid dimension, but should parse as the unsigned int "100".  So if
  // we got here and we don't have a valid dimension value, just try re-parsing
  // the string we have as an integer.
  nsAutoString val;
  attrVal->ToString(val);
  nsContentUtils::ParseHTMLIntegerResultFlags result;
  int32_t parsedInt = nsContentUtils::ParseHTMLInteger(val, &result);
  if ((result & nsContentUtils::eParseHTMLInteger_Error) || parsedInt < 0) {
    return aDefault;
  }

  return parsedInt;
}

void nsGenericHTMLElement::GetURIAttr(nsAtom* aAttr, nsAtom* aBaseAttr,
                                      nsAString& aResult) const {
  nsCOMPtr<nsIURI> uri;
  const nsAttrValue* attr = GetURIAttr(aAttr, aBaseAttr, getter_AddRefs(uri));
  if (!attr) {
    aResult.Truncate();
    return;
  }
  if (!uri) {
    // Just return the attr value
    attr->ToString(aResult);
    return;
  }
  nsAutoCString spec;
  uri->GetSpec(spec);
  CopyUTF8toUTF16(spec, aResult);
}

void nsGenericHTMLElement::GetURIAttr(nsAtom* aAttr, nsAtom* aBaseAttr,
                                      nsACString& aResult) const {
  nsCOMPtr<nsIURI> uri;
  const nsAttrValue* attr = GetURIAttr(aAttr, aBaseAttr, getter_AddRefs(uri));
  if (!attr) {
    aResult.Truncate();
    return;
  }
  if (!uri) {
    // Just return the attr value
    nsAutoString value;
    attr->ToString(value);
    CopyUTF16toUTF8(value, aResult);
    return;
  }
  uri->GetSpec(aResult);
}

const nsAttrValue* nsGenericHTMLElement::GetURIAttr(nsAtom* aAttr,
                                                    nsAtom* aBaseAttr,
                                                    nsIURI** aURI) const {
  *aURI = nullptr;

  const nsAttrValue* attr = mAttrs.GetAttr(aAttr);
  if (!attr) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> baseURI = GetBaseURI();
  if (aBaseAttr) {
    nsAutoString baseAttrValue;
    if (GetAttr(aBaseAttr, baseAttrValue)) {
      nsCOMPtr<nsIURI> baseAttrURI;
      nsresult rv = nsContentUtils::NewURIWithDocumentCharset(
          getter_AddRefs(baseAttrURI), baseAttrValue, OwnerDoc(), baseURI);
      if (NS_FAILED(rv)) {
        return attr;
      }
      baseURI.swap(baseAttrURI);
    }
  }

  // Don't care about return value.  If it fails, we still want to
  // return true, and *aURI will be null.
  nsContentUtils::NewURIWithDocumentCharset(aURI, attr->GetStringValue(),
                                            OwnerDoc(), baseURI);
  return attr;
}

bool nsGenericHTMLElement::IsContentEditable() const {
  for (const auto* element : InclusiveAncestorsOfType<nsGenericHTMLElement>()) {
    const ContentEditableState state = element->GetContentEditableState();
    if (state != ContentEditableState::Inherit) {
      return IsEditableState(state);
    }
  }
  return false;
}

bool nsGenericHTMLElement::IsLabelable() const {
  return IsAnyOfHTMLElements(nsGkAtoms::progress, nsGkAtoms::meter);
}

/* static */
bool nsGenericHTMLElement::MatchLabelsElement(Element* aElement,
                                              int32_t aNamespaceID,
                                              nsAtom* aAtom, void* aData) {
  HTMLLabelElement* element = HTMLLabelElement::FromNode(aElement);
  return element && element->GetControl() == aData;
}

already_AddRefed<nsINodeList> nsGenericHTMLElement::Labels() {
  MOZ_ASSERT(IsLabelable(),
             "Labels() only allow labelable elements to use it.");
  nsExtendedDOMSlots* slots = ExtendedDOMSlots();

  if (!slots->mLabelsList) {
    slots->mLabelsList =
        new nsLabelsNodeList(SubtreeRoot(), MatchLabelsElement, nullptr, this);
  }

  RefPtr<nsLabelsNodeList> labels = slots->mLabelsList;
  return labels.forget();
}

// static
bool nsGenericHTMLElement::LegacyTouchAPIEnabled(JSContext* aCx,
                                                 JSObject* aGlobal) {
  return TouchEvent::LegacyAPIEnabled(aCx, aGlobal);
}

bool nsGenericHTMLElement::IsFormControlDefaultFocusable(
    IsFocusableFlags aFlags) const {
  if (!(aFlags & IsFocusableFlags::WithMouse)) {
    return true;
  }
  switch (StaticPrefs::accessibility_mouse_focuses_formcontrol()) {
    case 0:
      return false;
    case 1:
      return true;
    default:
      return !IsInChromeDocument();
  }
}

//----------------------------------------------------------------------

nsGenericHTMLFormElement::nsGenericHTMLFormElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {
  // We should add the ElementState::ENABLED bit here as needed, but that
  // depends on our type, which is not initialized yet.  So we have to do this
  // in subclasses. Same for a couple other bits.
}

void nsGenericHTMLFormElement::ClearForm(bool aRemoveFromForm,
                                         bool aUnbindOrDelete) {
  MOZ_ASSERT(IsFormAssociatedElement());

  HTMLFormElement* form = GetFormInternal();
  NS_ASSERTION((form != nullptr) == HasFlag(ADDED_TO_FORM),
               "Form control should have had flag set correctly");

  if (!form) {
    return;
  }

  if (aRemoveFromForm) {
    nsAutoString nameVal, idVal;
    GetAttr(nsGkAtoms::name, nameVal);
    GetAttr(nsGkAtoms::id, idVal);

    form->RemoveElement(this, true);

    if (!nameVal.IsEmpty()) {
      form->RemoveElementFromTable(this, nameVal);
    }

    if (!idVal.IsEmpty()) {
      form->RemoveElementFromTable(this, idVal);
    }
  }

  UnsetFlags(ADDED_TO_FORM);
  SetFormInternal(nullptr, false);
  AfterClearForm(aUnbindOrDelete);
}

nsresult nsGenericHTMLFormElement::BindToTree(BindContext& aContext,
                                              nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (IsFormAssociatedElement()) {
    // If @form is set, the element *has* to be in a composed document,
    // otherwise it wouldn't be possible to find an element with the
    // corresponding id. If @form isn't set, the element *has* to have a parent,
    // otherwise it wouldn't be possible to find a form ancestor. We should not
    // call UpdateFormOwner if none of these conditions are fulfilled.
    if (HasAttr(nsGkAtoms::form) ? IsInComposedDoc() : aParent.IsContent()) {
      UpdateFormOwner(true, nullptr);
    }
  }

  // Set parent fieldset which should be used for the disabled state.
  UpdateFieldSet(false);
  return NS_OK;
}

void nsGenericHTMLFormElement::UnbindFromTree(UnbindContext& aContext) {
  // Save state before doing anything else.
  SaveState();

  if (IsFormAssociatedElement()) {
    if (HTMLFormElement* form = GetFormInternal()) {
      // Might need to unset form
      if (aContext.IsUnbindRoot(this)) {
        // No more parent means no more form
        ClearForm(true, true);
      } else {
        // Recheck whether we should still have an form.
        if (HasAttr(nsGkAtoms::form) || !FindAncestorForm(form)) {
          ClearForm(true, true);
        } else {
          UnsetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
        }
      }
    }

    // We have to remove the form id observer if there was one.
    // We will re-add one later if needed (during bind to tree).
    if (nsContentUtils::HasNonEmptyAttr(this, kNameSpaceID_None,
                                        nsGkAtoms::form)) {
      RemoveFormIdObserver();
    }
  }

  nsGenericHTMLElement::UnbindFromTree(aContext);

  // The element might not have a fieldset anymore.
  UpdateFieldSet(false);
}

void nsGenericHTMLFormElement::BeforeSetAttr(int32_t aNameSpaceID,
                                             nsAtom* aName,
                                             const nsAttrValue* aValue,
                                             bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None && IsFormAssociatedElement()) {
    nsAutoString tmp;
    HTMLFormElement* form = GetFormInternal();

    // remove the control from the hashtable as needed

    if (form && (aName == nsGkAtoms::name || aName == nsGkAtoms::id)) {
      GetAttr(aName, tmp);

      if (!tmp.IsEmpty()) {
        form->RemoveElementFromTable(this, tmp);
      }
    }

    if (form && aName == nsGkAtoms::type) {
      GetAttr(nsGkAtoms::name, tmp);

      if (!tmp.IsEmpty()) {
        form->RemoveElementFromTable(this, tmp);
      }

      GetAttr(nsGkAtoms::id, tmp);

      if (!tmp.IsEmpty()) {
        form->RemoveElementFromTable(this, tmp);
      }

      form->RemoveElement(this, false);
    }

    if (aName == nsGkAtoms::form) {
      // If @form isn't set or set to the empty string, there were no observer
      // so we don't have to remove it.
      if (nsContentUtils::HasNonEmptyAttr(this, kNameSpaceID_None,
                                          nsGkAtoms::form)) {
        // The current form id observer is no longer needed.
        // A new one may be added in AfterSetAttr.
        RemoveFormIdObserver();
      }
    }
  }

  return nsGenericHTMLElement::BeforeSetAttr(aNameSpaceID, aName, aValue,
                                             aNotify);
}

void nsGenericHTMLFormElement::AfterSetAttr(
    int32_t aNameSpaceID, nsAtom* aName, const nsAttrValue* aValue,
    const nsAttrValue* aOldValue, nsIPrincipal* aMaybeScriptedPrincipal,
    bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None && IsFormAssociatedElement()) {
    HTMLFormElement* form = GetFormInternal();

    // add the control to the hashtable as needed
    if (form && (aName == nsGkAtoms::name || aName == nsGkAtoms::id) &&
        aValue && !aValue->IsEmptyString()) {
      MOZ_ASSERT(aValue->Type() == nsAttrValue::eAtom,
                 "Expected atom value for name/id");
      form->AddElementToTable(this,
                              nsDependentAtomString(aValue->GetAtomValue()));
    }

    if (form && aName == nsGkAtoms::type) {
      nsAutoString tmp;

      GetAttr(nsGkAtoms::name, tmp);

      if (!tmp.IsEmpty()) {
        form->AddElementToTable(this, tmp);
      }

      GetAttr(nsGkAtoms::id, tmp);

      if (!tmp.IsEmpty()) {
        form->AddElementToTable(this, tmp);
      }

      form->AddElement(this, false, aNotify);
    }

    if (aName == nsGkAtoms::form) {
      // We need a new form id observer.
      DocumentOrShadowRoot* docOrShadow =
          GetUncomposedDocOrConnectedShadowRoot();
      if (docOrShadow) {
        Element* formIdElement = nullptr;
        if (aValue && !aValue->IsEmptyString()) {
          formIdElement = AddFormIdObserver();
        }

        // Because we have a new @form value (or no more @form), we have to
        // update our form owner.
        UpdateFormOwner(false, formIdElement);
      }
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

void nsGenericHTMLFormElement::ForgetFieldSet(nsIContent* aFieldset) {
  MOZ_DIAGNOSTIC_ASSERT(IsFormAssociatedElement());
  if (GetFieldSetInternal() == aFieldset) {
    SetFieldSetInternal(nullptr);
  }
}

Element* nsGenericHTMLFormElement::AddFormIdObserver() {
  MOZ_ASSERT(IsFormAssociatedElement());

  nsAutoString formId;
  DocumentOrShadowRoot* docOrShadow = GetUncomposedDocOrConnectedShadowRoot();
  GetAttr(nsGkAtoms::form, formId);
  NS_ASSERTION(!formId.IsEmpty(),
               "@form value should not be the empty string!");
  RefPtr<nsAtom> atom = NS_Atomize(formId);

  return docOrShadow->AddIDTargetObserver(atom, FormIdUpdated, this, false);
}

void nsGenericHTMLFormElement::RemoveFormIdObserver() {
  MOZ_ASSERT(IsFormAssociatedElement());

  DocumentOrShadowRoot* docOrShadow = GetUncomposedDocOrConnectedShadowRoot();
  if (!docOrShadow) {
    return;
  }

  nsAutoString formId;
  GetAttr(nsGkAtoms::form, formId);
  NS_ASSERTION(!formId.IsEmpty(),
               "@form value should not be the empty string!");
  RefPtr<nsAtom> atom = NS_Atomize(formId);

  docOrShadow->RemoveIDTargetObserver(atom, FormIdUpdated, this, false);
}

/* static */
bool nsGenericHTMLFormElement::FormIdUpdated(Element* aOldElement,
                                             Element* aNewElement,
                                             void* aData) {
  nsGenericHTMLFormElement* element =
      static_cast<nsGenericHTMLFormElement*>(aData);

  NS_ASSERTION(element->IsHTMLElement(), "aData should be an HTML element");

  element->UpdateFormOwner(false, aNewElement);

  return true;
}

bool nsGenericHTMLFormElement::IsElementDisabledForEvents(WidgetEvent* aEvent,
                                                          nsIFrame* aFrame) {
  MOZ_ASSERT(aEvent);

  // Allow dispatch of CustomEvent and untrusted Events.
  if (!aEvent->IsTrusted()) {
    return false;
  }

  switch (aEvent->mMessage) {
    case eAnimationStart:
    case eAnimationEnd:
    case eAnimationIteration:
    case eAnimationCancel:
    case eFormChange:
    case eMouseMove:
    case eMouseOver:
    case eMouseOut:
    case eMouseEnter:
    case eMouseLeave:
    case ePointerMove:
    case ePointerOver:
    case ePointerOut:
    case ePointerEnter:
    case ePointerLeave:
    case eTransitionCancel:
    case eTransitionEnd:
    case eTransitionRun:
    case eTransitionStart:
    case eWheel:
    case eLegacyMouseLineOrPageScroll:
    case eLegacyMousePixelScroll:
      return false;
    case eFocus:
    case eBlur:
    case eFocusIn:
    case eFocusOut:
    case eKeyPress:
    case eKeyUp:
    case eKeyDown:
      if (StaticPrefs::dom_forms_always_allow_key_and_focus_events_enabled()) {
        return false;
      }
      [[fallthrough]];
    case ePointerDown:
    case ePointerUp:
    case ePointerCancel:
    case ePointerGotCapture:
    case ePointerLostCapture:
      if (StaticPrefs::dom_forms_always_allow_pointer_events_enabled()) {
        return false;
      }
      [[fallthrough]];
    default:
      break;
  }

  if (aEvent->mSpecifiedEventType == nsGkAtoms::oninput) {
    return false;
  }

  // FIXME(emilio): This poking at the style of the frame is slightly bogus
  // unless we flush before every event, which we don't really want to do.
  if (aFrame && aFrame->StyleUI()->UserInput() == StyleUserInput::None) {
    return true;
  }

  return IsDisabled();
}

void nsGenericHTMLFormElement::UpdateFormOwner(bool aBindToTree,
                                               Element* aFormIdElement) {
  MOZ_ASSERT(IsFormAssociatedElement());
  MOZ_ASSERT(!aBindToTree || !aFormIdElement,
             "aFormIdElement shouldn't be set if aBindToTree is true!");

  HTMLFormElement* form = GetFormInternal();
  if (!aBindToTree) {
    ClearForm(true, false);
    form = nullptr;
  }

  HTMLFormElement* oldForm = form;
  if (!form) {
    // If @form is set, we have to use that to find the form.
    nsAutoString formId;
    if (GetAttr(nsGkAtoms::form, formId)) {
      if (!formId.IsEmpty()) {
        Element* element = nullptr;

        if (aBindToTree) {
          element = AddFormIdObserver();
        } else {
          element = aFormIdElement;
        }

        NS_ASSERTION(!IsInComposedDoc() ||
                         element == GetUncomposedDocOrConnectedShadowRoot()
                                        ->GetElementById(formId),
                     "element should be equals to the current element "
                     "associated with the id in @form!");

        if (element && element->IsHTMLElement(nsGkAtoms::form) &&
            nsContentUtils::IsInSameAnonymousTree(this, element)) {
          form = static_cast<HTMLFormElement*>(element);
          SetFormInternal(form, aBindToTree);
        }
      }
    } else {
      // We now have a parent, so we may have picked up an ancestor form. Search
      // for it.  Note that if form is already set we don't want to do this,
      // because that means someone (probably the content sink) has already set
      // it to the right value.  Also note that even if being bound here didn't
      // change our parent, we still need to search, since our parent chain
      // probably changed _somewhere_.
      form = FindAncestorForm();
      SetFormInternal(form, aBindToTree);
    }
  }

  if (form && !HasFlag(ADDED_TO_FORM)) {
    // Now we need to add ourselves to the form
    nsAutoString nameVal, idVal;
    GetAttr(nsGkAtoms::name, nameVal);
    GetAttr(nsGkAtoms::id, idVal);

    SetFlags(ADDED_TO_FORM);

    // Notify only if we just found this form.
    form->AddElement(this, true, oldForm == nullptr);

    if (!nameVal.IsEmpty()) {
      form->AddElementToTable(this, nameVal);
    }

    if (!idVal.IsEmpty()) {
      form->AddElementToTable(this, idVal);
    }
  }
}

void nsGenericHTMLFormElement::UpdateFieldSet(bool aNotify) {
  if (IsInNativeAnonymousSubtree() || !IsFormAssociatedElement()) {
    MOZ_ASSERT_IF(IsFormAssociatedElement(), !GetFieldSetInternal());
    return;
  }

  nsIContent* parent = nullptr;
  nsIContent* prev = nullptr;
  HTMLFieldSetElement* fieldset = GetFieldSetInternal();

  for (parent = GetParent(); parent;
       prev = parent, parent = parent->GetParent()) {
    HTMLFieldSetElement* parentFieldset = HTMLFieldSetElement::FromNode(parent);
    if (parentFieldset && (!prev || parentFieldset->GetFirstLegend() != prev)) {
      if (fieldset == parentFieldset) {
        // We already have the right fieldset;
        return;
      }

      if (fieldset) {
        fieldset->RemoveElement(this);
      }
      SetFieldSetInternal(parentFieldset);
      parentFieldset->AddElement(this);

      // The disabled state may have changed
      FieldSetDisabledChanged(aNotify);
      return;
    }
  }

  // No fieldset found.
  if (fieldset) {
    fieldset->RemoveElement(this);
    SetFieldSetInternal(nullptr);
    // The disabled state may have changed
    FieldSetDisabledChanged(aNotify);
  }
}

void nsGenericHTMLFormElement::UpdateDisabledState(bool aNotify) {
  if (!CanBeDisabled()) {
    return;
  }

  HTMLFieldSetElement* fieldset = GetFieldSetInternal();
  const bool isDisabled =
      HasAttr(nsGkAtoms::disabled) || (fieldset && fieldset->IsDisabled());

  const ElementState disabledStates =
      isDisabled ? ElementState::DISABLED : ElementState::ENABLED;

  ElementState oldDisabledStates = State() & ElementState::DISABLED_STATES;
  ElementState changedStates = disabledStates ^ oldDisabledStates;

  if (!changedStates.IsEmpty()) {
    ToggleStates(changedStates, aNotify);
    if (DoesReadWriteApply()) {
      // :disabled influences :read-only / :read-write.
      UpdateReadOnlyState(aNotify);
    }
  }
}

bool nsGenericHTMLFormElement::IsReadOnlyInternal() const {
  if (DoesReadWriteApply()) {
    return IsDisabled() || GetBoolAttr(nsGkAtoms::readonly);
  }
  return nsGenericHTMLElement::IsReadOnlyInternal();
}

void nsGenericHTMLFormElement::FieldSetDisabledChanged(bool aNotify) {
  UpdateDisabledState(aNotify);
}

void nsGenericHTMLFormElement::SaveSubtreeState() {
  SaveState();

  nsGenericHTMLElement::SaveSubtreeState();
}

//----------------------------------------------------------------------

void nsGenericHTMLElement::Click(CallerType aCallerType) {
  if (HandlingClick()) {
    return;
  }

  // There are two notions of disabled.
  // "disabled":
  // https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#attr-fe-disabled
  // "actually disabled":
  // https://html.spec.whatwg.org/multipage/semantics-other.html#concept-element-disabled
  // click() reads the former but IsDisabled() is for the latter. <fieldset> is
  // included only in the latter, so we exclude it here.
  // XXX(krosylight): What about <optgroup>? And should we add a separate method
  // for this?
  if (IsDisabled() &&
      !(mNodeInfo->Equals(nsGkAtoms::fieldset) &&
        StaticPrefs::dom_forms_fieldset_disable_only_descendants_enabled())) {
    return;
  }

  // Strong in case the event kills it
  nsCOMPtr<Document> doc = GetComposedDoc();

  RefPtr<nsPresContext> context;
  if (doc) {
    PresShell* presShell = doc->GetPresShell();
    if (!presShell) {
      // We need the nsPresContext for dispatching the click event. In some
      // rare cases we need to flush notifications to force creation of the
      // nsPresContext here (for example when a script calls button.click()
      // from script early during page load). We only flush the notifications
      // if the PresShell hasn't been created yet, to limit the performance
      // impact.
      doc->FlushPendingNotifications(FlushType::EnsurePresShellInitAndFrames);
      presShell = doc->GetPresShell();
    }
    if (presShell) {
      context = presShell->GetPresContext();
    }
  }

  SetHandlingClick();

  // Mark this event trusted if Click() is called from system code.
  WidgetPointerEvent event(aCallerType == CallerType::System, ePointerClick,
                           nullptr);
  event.mFlags.mIsPositionless = true;
  event.mInputSource = MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
  // pointerId definition in Pointer Events:
  // > The pointerId value of -1 MUST be reserved and used to indicate events
  // > that were generated by something other than a pointing device.
  event.pointerId = -1;

  EventDispatcher::Dispatch(this, context, &event);

  ClearHandlingClick();
}

bool nsGenericHTMLElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                           bool* aIsFocusable,
                                           int32_t* aTabIndex) {
  MOZ_ASSERT(aIsFocusable);
  MOZ_ASSERT(aTabIndex);
  if (ShadowRoot* root = GetShadowRoot()) {
    if (root->DelegatesFocus()) {
      *aIsFocusable = false;
      return true;
    }
  }

  if (!IsInComposedDoc() || IsInDesignMode()) {
    // In designMode documents we only allow focusing the document.
    *aTabIndex = -1;
    *aIsFocusable = false;
    return true;
  }

  *aTabIndex = TabIndex();
  bool disabled = false;
  bool disallowOverridingFocusability = true;
  Maybe<int32_t> attrVal = GetTabIndexAttrValue();
  if (IsEditingHost()) {
    // Editable roots should always be focusable.
    disallowOverridingFocusability = true;

    // Ignore the disabled attribute in editable contentEditable/designMode
    // roots.
    if (attrVal.isNothing()) {
      // The default value for tabindex should be 0 for editable
      // contentEditable roots.
      *aTabIndex = 0;
    }
  } else {
    disallowOverridingFocusability = false;

    // Just check for disabled attribute on form controls
    disabled = IsDisabled();
    if (disabled) {
      *aTabIndex = -1;
    }
  }

  // If a tabindex is specified at all, or the default tabindex is 0, we're
  // focusable.
  *aIsFocusable = (*aTabIndex >= 0 || (!disabled && attrVal.isSome()));
  return disallowOverridingFocusability;
}

Result<bool, nsresult> nsGenericHTMLElement::PerformAccesskey(
    bool aKeyCausesActivation, bool aIsTrustedEvent) {
  RefPtr<nsPresContext> presContext = GetPresContext(eForComposedDoc);
  if (!presContext) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  // It's hard to say what HTML4 wants us to do in all cases.
  bool focused = true;
  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    fm->SetFocus(this, nsIFocusManager::FLAG_BYKEY);

    // Return true if the element became the current focus within its window.
    nsPIDOMWindowOuter* window = OwnerDoc()->GetWindow();
    focused = window && window->GetFocusedElement() == this;
  }

  if (aKeyCausesActivation) {
    // Click on it if the users prefs indicate to do so.
    AutoHandlingUserInputStatePusher userInputStatePusher(aIsTrustedEvent);
    AutoPopupStatePusher popupStatePusher(
        aIsTrustedEvent ? PopupBlocker::openAllowed : PopupBlocker::openAbused);
    DispatchSimulatedClick(this, aIsTrustedEvent, presContext);
    return focused;
  }

  // If the accesskey won't cause the activation and the focus isn't changed,
  // either. Return error so EventStateManager would try to find next element
  // to handle the accesskey.
  return focused ? Result<bool, nsresult>{focused} : Err(NS_ERROR_ABORT);
}

void nsGenericHTMLElement::HandleKeyboardActivation(
    EventChainPostVisitor& aVisitor) {
  MOZ_ASSERT(aVisitor.mEvent->HasKeyEventMessage());
  MOZ_ASSERT(aVisitor.mEvent->IsTrusted());

  // If focused element is different from this element, it may be editable.
  // In that case, associated editor for the element should handle the keyboard
  // instead.  Therefore, if this is not the focused element, we should not
  // handle the event here.  Note that this element may be an editing host,
  // i.e., focused and editable.  In the case, keyboard events should be
  // handled by the focused element instead of associated editor because
  // Chrome handles the case so.  For compatibility with Chrome, we follow them.
  if (nsFocusManager::GetFocusedElementStatic() != this) {
    return;
  }

  const auto message = aVisitor.mEvent->mMessage;
  const WidgetKeyboardEvent* keyEvent = aVisitor.mEvent->AsKeyboardEvent();
  if (nsEventStatus_eIgnore != aVisitor.mEventStatus) {
    if (message == eKeyUp && keyEvent->mKeyCode == NS_VK_SPACE) {
      // Unset the flag even if the event is default-prevented or something.
      UnsetFlags(HTML_ELEMENT_ACTIVE_FOR_KEYBOARD);
    }
    return;
  }

  bool shouldActivate = false;
  switch (message) {
    case eKeyDown:
      if (keyEvent->ShouldWorkAsSpaceKey()) {
        SetFlags(HTML_ELEMENT_ACTIVE_FOR_KEYBOARD);
      }
      return;
    case eKeyPress:
      shouldActivate = keyEvent->mKeyCode == NS_VK_RETURN;
      if (keyEvent->ShouldWorkAsSpaceKey()) {
        // Consume 'space' key to prevent scrolling the page down.
        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      }
      break;
    case eKeyUp:
      shouldActivate = keyEvent->ShouldWorkAsSpaceKey() &&
                       HasFlag(HTML_ELEMENT_ACTIVE_FOR_KEYBOARD);
      if (shouldActivate) {
        UnsetFlags(HTML_ELEMENT_ACTIVE_FOR_KEYBOARD);
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("why didn't we bail out earlier?");
      break;
  }

  if (!shouldActivate) {
    return;
  }

  RefPtr<nsPresContext> presContext = aVisitor.mPresContext;
  DispatchSimulatedClick(this, aVisitor.mEvent->IsTrusted(), presContext);
  aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
}

nsresult nsGenericHTMLElement::DispatchSimulatedClick(
    nsGenericHTMLElement* aElement, bool aIsTrusted,
    nsPresContext* aPresContext) {
  WidgetPointerEvent event(aIsTrusted, ePointerClick, nullptr);
  event.mFlags.mIsPositionless = true;
  event.mInputSource = MouseEvent_Binding::MOZ_SOURCE_KEYBOARD;
  // pointerId definition in Pointer Events:
  // > The pointerId value of -1 MUST be reserved and used to indicate events
  // > that were generated by something other than a pointing device.
  event.pointerId = -1;
  return EventDispatcher::Dispatch(aElement, aPresContext, &event);
}

already_AddRefed<EditorBase> nsGenericHTMLElement::GetAssociatedEditor() {
  // If contenteditable is ever implemented, it might need to do something
  // different here?

  RefPtr<TextEditor> textEditor = GetTextEditorInternal();
  return textEditor.forget();
}

// static
void nsGenericHTMLElement::SyncEditorsOnSubtree(nsIContent* content) {
  /* Sync this node */
  nsGenericHTMLElement* element = FromNode(content);
  if (element) {
    if (RefPtr<EditorBase> editorBase = element->GetAssociatedEditor()) {
      editorBase->SyncRealTimeSpell();
    }
  }

  /* Sync all children */
  for (nsIContent* child = content->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    SyncEditorsOnSubtree(child);
  }
}

static void MakeContentDescendantsEditable(nsIContent* aContent) {
  // If aContent is not an element, we just need to update its
  // internal editable state and don't need to notify anyone about
  // that.  For elements, we need to send a ElementStateChanged
  // notification.
  if (!aContent->IsElement()) {
    aContent->UpdateEditableState(false);
    return;
  }

  Element* element = aContent->AsElement();

  element->UpdateEditableState(true);

  for (nsIContent* child = aContent->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (!child->IsElement() ||
        !child->AsElement()->HasAttr(nsGkAtoms::contenteditable)) {
      MakeContentDescendantsEditable(child);
    }
  }
}

void nsGenericHTMLElement::ChangeEditableState(int32_t aChange) {
  Document* document = GetComposedDoc();
  if (!document) {
    return;
  }

  Document::EditingState previousEditingState = Document::EditingState::eOff;
  if (aChange != 0) {
    document->ChangeContentEditableCount(this, aChange);
    previousEditingState = document->GetEditingState();
  }

  // MakeContentDescendantsEditable is going to call ElementStateChanged for
  // this element and all descendants if editable state has changed.
  // We might as well wrap it all in one script blocker.
  nsAutoScriptBlocker scriptBlocker;
  MakeContentDescendantsEditable(this);

  // If the document already had contenteditable and JS adds new
  // contenteditable, that might cause changing editing host to current editing
  // host's ancestor.  In such case, HTMLEditor needs to know that
  // synchronously to update selection limitter.
  // Additionally, elements in shadow DOM is not editable in the normal cases,
  // but if its content has `contenteditable`, only in it can be ediable.
  // So we don't need to notify HTMLEditor of this change only when we're not
  // in shadow DOM and the composed document is in design mode.
  if (IsInDesignMode() && !IsInShadowTree() && aChange > 0 &&
      previousEditingState == Document::EditingState::eContentEditable) {
    if (const RefPtr<HTMLEditor> htmlEditor =
            nsContentUtils::GetHTMLEditor(document->GetPresContext())) {
      htmlEditor->NotifyEditingHostMaybeChanged();
    }
  }
}

//----------------------------------------------------------------------

nsGenericHTMLFormControlElement::nsGenericHTMLFormControlElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo, FormControlType aType)
    : nsGenericHTMLFormElement(std::move(aNodeInfo)),
      nsIFormControl(aType),
      mForm(nullptr),
      mFieldSet(nullptr) {}

nsGenericHTMLFormControlElement::~nsGenericHTMLFormControlElement() {
  if (mFieldSet) {
    mFieldSet->RemoveElement(this);
  }

  // Check that this element doesn't know anything about its form at this point.
  NS_ASSERTION(!mForm, "mForm should be null at this point!");
}

NS_IMPL_ISUPPORTS_INHERITED(nsGenericHTMLFormControlElement,
                            nsGenericHTMLFormElement, nsIFormControl)

nsINode* nsGenericHTMLFormControlElement::GetScopeChainParent() const {
  return mForm ? mForm : nsGenericHTMLElement::GetScopeChainParent();
}

nsIContent::IMEState nsGenericHTMLFormControlElement::GetDesiredIMEState() {
  TextEditor* textEditor = GetTextEditorInternal();
  if (!textEditor) {
    return nsGenericHTMLFormElement::GetDesiredIMEState();
  }
  IMEState state;
  nsresult rv = textEditor->GetPreferredIMEState(&state);
  if (NS_FAILED(rv)) {
    return nsGenericHTMLFormElement::GetDesiredIMEState();
  }
  return state;
}

void nsGenericHTMLFormControlElement::GetAutocapitalize(
    nsAString& aValue) const {
  if (nsContentUtils::HasNonEmptyAttr(this, kNameSpaceID_None,
                                      nsGkAtoms::autocapitalize)) {
    nsGenericHTMLFormElement::GetAutocapitalize(aValue);
    return;
  }

  if (mForm && IsAutocapitalizeOrAutocorrectInheriting()) {
    mForm->GetAutocapitalize(aValue);
  }
}

// https://html.spec.whatwg.org/#dom-autocorrect
bool nsGenericHTMLFormControlElement::Autocorrect() const {
  auto controlType = ControlType();

  switch (controlType) {
    case FormControlType::InputEmail:
    case FormControlType::InputPassword:
    case FormControlType::InputUrl:
      return false;
    default:
      break;
  }

  if (HasAttr(kNameSpaceID_None, nsGkAtoms::autocorrect)) {
    return nsGenericHTMLElement::Autocorrect();
  }

  if (mForm && IsAutocapitalizeOrAutocorrectInheriting()) {
    return mForm->Autocorrect();
  }

  return true;
}

bool nsGenericHTMLFormControlElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                                      bool* aIsFocusable,
                                                      int32_t* aTabIndex) {
  if (nsGenericHTMLFormElement::IsHTMLFocusable(aFlags, aIsFocusable,
                                                aTabIndex)) {
    return true;
  }

  *aIsFocusable = *aIsFocusable && IsFormControlDefaultFocusable(aFlags);
  return false;
}

HTMLFieldSetElement* nsGenericHTMLFormControlElement::GetFieldSet() {
  return GetFieldSetInternal();
}

void nsGenericHTMLFormControlElement::SetForm(HTMLFormElement* aForm) {
  MOZ_ASSERT(aForm, "Don't pass null here");
  NS_ASSERTION(!mForm,
               "We don't support switching from one non-null form to another.");

  SetFormInternal(aForm, false);
}

void nsGenericHTMLFormControlElement::ClearForm(bool aRemoveFromForm,
                                                bool aUnbindOrDelete) {
  nsGenericHTMLFormElement::ClearForm(aRemoveFromForm, aUnbindOrDelete);
}

bool nsGenericHTMLFormControlElement::IsLabelable() const {
  auto type = ControlType();
  return (IsInputElement(type) && type != FormControlType::InputHidden) ||
         IsButtonElement(type) || type == FormControlType::Output ||
         type == FormControlType::Select || type == FormControlType::Textarea;
}

bool nsGenericHTMLFormControlElement::CanBeDisabled() const {
  auto type = ControlType();
  // It's easier to test the types that _cannot_ be disabled
  return type != FormControlType::Object && type != FormControlType::Output;
}

bool nsGenericHTMLFormControlElement::DoesReadWriteApply() const {
  auto type = ControlType();
  if (!IsInputElement(type) && type != FormControlType::Textarea) {
    return false;
  }

  switch (type) {
    case FormControlType::InputHidden:
    case FormControlType::InputButton:
    case FormControlType::InputImage:
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputRadio:
    case FormControlType::InputFile:
    case FormControlType::InputCheckbox:
    case FormControlType::InputRange:
    case FormControlType::InputColor:
      return false;
#ifdef DEBUG
    case FormControlType::Textarea:
    case FormControlType::InputText:
    case FormControlType::InputPassword:
    case FormControlType::InputSearch:
    case FormControlType::InputTel:
    case FormControlType::InputEmail:
    case FormControlType::InputUrl:
    case FormControlType::InputNumber:
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return true;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected input type in DoesReadWriteApply()");
      return true;
#else   // DEBUG
    default:
      return true;
#endif  // DEBUG
  }
}

void nsGenericHTMLFormControlElement::SetFormInternal(HTMLFormElement* aForm,
                                                      bool aBindToTree) {
  if (aForm) {
    BeforeSetForm(aForm, aBindToTree);
  }

  // keep a *weak* ref to the form here
  mForm = aForm;
}

HTMLFormElement* nsGenericHTMLFormControlElement::GetFormInternal() const {
  return mForm;
}

HTMLFieldSetElement* nsGenericHTMLFormControlElement::GetFieldSetInternal()
    const {
  return mFieldSet;
}

void nsGenericHTMLFormControlElement::SetFieldSetInternal(
    HTMLFieldSetElement* aFieldset) {
  mFieldSet = aFieldset;
}

void nsGenericHTMLFormControlElement::UpdateRequiredState(bool aIsRequired,
                                                          bool aNotify) {
#ifdef DEBUG
  auto type = ControlType();
#endif
  MOZ_ASSERT(IsInputElement(type) || type == FormControlType::Select ||
                 type == FormControlType::Textarea,
             "This should be called only on types that @required applies");

#ifdef DEBUG
  if (HTMLInputElement* input = HTMLInputElement::FromNode(this)) {
    MOZ_ASSERT(
        input->DoesRequiredApply(),
        "This should be called only on input types that @required applies");
  }
#endif

  ElementState requiredStates;
  if (aIsRequired) {
    requiredStates |= ElementState::REQUIRED;
  } else {
    requiredStates |= ElementState::OPTIONAL_;
  }

  ElementState oldRequiredStates = State() & ElementState::REQUIRED_STATES;
  ElementState changedStates = requiredStates ^ oldRequiredStates;

  if (!changedStates.IsEmpty()) {
    ToggleStates(changedStates, aNotify);
  }
}

bool nsGenericHTMLFormControlElement::IsAutocapitalizeOrAutocorrectInheriting()
    const {
  auto type = ControlType();
  return IsInputElement(type) || IsButtonElement(type) ||
         type == FormControlType::Fieldset || type == FormControlType::Output ||
         type == FormControlType::Select || type == FormControlType::Textarea;
}

nsresult nsGenericHTMLFormControlElement::SubmitDirnameDir(
    FormData* aFormData) {
  // Submit dirname=dir if element has non-empty dirname attribute
  if (HasAttr(nsGkAtoms::dirname)) {
    nsAutoString dirname;
    GetAttr(nsGkAtoms::dirname, dirname);
    if (!dirname.IsEmpty()) {
      const Directionality dir = GetDirectionality();
      MOZ_ASSERT(dir == Directionality::Ltr || dir == Directionality::Rtl,
                 "The directionality of an element is either ltr or rtl");
      return aFormData->AddNameValuePair(
          dirname, dir == Directionality::Ltr ? u"ltr"_ns : u"rtl"_ns);
    }
  }
  return NS_OK;
}

void nsGenericHTMLFormControlElement::GetFormAutofillState(
    nsAString& aState) const {
  if (State().HasState(ElementState::AUTOFILL_PREVIEW)) {
    aState.AssignLiteral("preview");
  } else if (State().HasState(ElementState::AUTOFILL)) {
    aState.AssignLiteral("autofill");
  } else {
    aState.Truncate();
  }
}

void nsGenericHTMLFormControlElement::SetFormAutofillState(
    const nsAString& aState) {
  if (aState.EqualsLiteral("autofill")) {
    RemoveStates(ElementState::AUTOFILL_PREVIEW);
    AddStates(ElementState::AUTOFILL);
  } else if (aState.EqualsLiteral("preview")) {
    AddStates(ElementState::AUTOFILL | ElementState::AUTOFILL_PREVIEW);
  } else {
    RemoveStates(ElementState::AUTOFILL | ElementState::AUTOFILL_PREVIEW);
  }
}

//----------------------------------------------------------------------

static constexpr nsAttrValue::EnumTable kPopoverTargetActionTable[] = {
    {"toggle", PopoverTargetAction::Toggle},
    {"show", PopoverTargetAction::Show},
    {"hide", PopoverTargetAction::Hide},
    {nullptr, 0}};

static const nsAttrValue::EnumTable* kPopoverTargetActionDefault =
    &kPopoverTargetActionTable[0];

nsGenericHTMLFormControlElementWithState::
    nsGenericHTMLFormControlElementWithState(
        already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
        FromParser aFromParser, FormControlType aType)
    : nsGenericHTMLFormControlElement(std::move(aNodeInfo), aType),
      mControlNumber(!!(aFromParser & FROM_PARSER_NETWORK)
                         ? OwnerDoc()->GetNextControlNumber()
                         : -1) {
  mStateKey.SetIsVoid(true);
}

bool nsGenericHTMLFormControlElementWithState::ParseAttribute(
    int32_t aNamespaceID, nsAtom* aAttribute, const nsAString& aValue,
    nsIPrincipal* aMaybeScriptedPrincipal, nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::popovertargetaction) {
      return aResult.ParseEnumValue(aValue, kPopoverTargetActionTable, false,
                                    kPopoverTargetActionDefault);
    }
    if (aAttribute == nsGkAtoms::popovertarget) {
      aResult.ParseAtom(aValue);
      return true;
    }

    if (StaticPrefs::dom_element_invokers_enabled()) {
      if (aAttribute == nsGkAtoms::invokeaction) {
        aResult.ParseAtom(aValue);
        return true;
      }
      if (aAttribute == nsGkAtoms::invoketarget) {
        aResult.ParseAtom(aValue);
        return true;
      }
    }
  }

  return nsGenericHTMLFormControlElement::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

mozilla::dom::Element*
nsGenericHTMLFormControlElementWithState::GetPopoverTargetElement() const {
  return GetAttrAssociatedElement(nsGkAtoms::popovertarget);
}

void nsGenericHTMLFormControlElementWithState::SetPopoverTargetElement(
    mozilla::dom::Element* aElement) {
  ExplicitlySetAttrElement(nsGkAtoms::popovertarget, aElement);
}

void nsGenericHTMLFormControlElementWithState::HandlePopoverTargetAction() {
  RefPtr<nsGenericHTMLElement> target = GetEffectivePopoverTargetElement();
  if (!target) {
    return;
  }

  auto action = PopoverTargetAction::Toggle;
  if (const nsAttrValue* value =
          GetParsedAttr(nsGkAtoms::popovertargetaction)) {
    MOZ_ASSERT(value->Type() == nsAttrValue::eEnum);
    action = static_cast<PopoverTargetAction>(value->GetEnumValue());
  }

  bool canHide = action == PopoverTargetAction::Hide ||
                 action == PopoverTargetAction::Toggle;
  bool shouldHide = canHide && target->IsPopoverOpen();
  bool canShow = action == PopoverTargetAction::Show ||
                 action == PopoverTargetAction::Toggle;
  bool shouldShow = canShow && !target->IsPopoverOpen();

  if (shouldHide) {
    target->HidePopover(IgnoreErrors());
  } else if (shouldShow) {
    target->ShowPopoverInternal(this, IgnoreErrors());
  }
}

void nsGenericHTMLFormControlElementWithState::GetInvokeAction(
    nsAString& aValue) const {
  const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::invokeaction);
  if (attr) {
    attr->GetAtomValue()->ToString(aValue);
  }
}

InvokeAction nsGenericHTMLFormControlElementWithState::GetInvokeAction(
    nsAtom* aAtom) const {
  if (aAtom == nsGkAtoms::_empty) {
    return InvokeAction::Auto;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::showpopover)) {
    return InvokeAction::ShowPopover;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::hidepopover)) {
    return InvokeAction::HidePopover;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::togglepopover)) {
    return InvokeAction::TogglePopover;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::showmodal)) {
    return InvokeAction::ShowModal;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::toggle)) {
    return InvokeAction::Toggle;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::close)) {
    return InvokeAction::Close;
  }
  if (nsContentUtils::EqualsIgnoreASCIICase(aAtom, nsGkAtoms::open)) {
    return InvokeAction::Open;
  }
  if (nsContentUtils::ContainsChar(aAtom, '-')) {
    return InvokeAction::Custom;
  }
  return InvokeAction::Invalid;
}

mozilla::dom::Element*
nsGenericHTMLFormControlElementWithState::GetInvokeTargetElement() const {
  if (StaticPrefs::dom_element_invokers_enabled()) {
    return GetAttrAssociatedElement(nsGkAtoms::invoketarget);
  }
  return nullptr;
}

void nsGenericHTMLFormControlElementWithState::SetInvokeTargetElement(
    mozilla::dom::Element* aElement) {
  ExplicitlySetAttrElement(nsGkAtoms::invoketarget, aElement);
}

void nsGenericHTMLFormControlElementWithState::HandleInvokeTargetAction() {
  RefPtr<Element> invokee = GetInvokeTargetElement();

  if (!invokee) {
    return;
  }

  // 1. Let action be element's invokeaction attribute.
  const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::invokeaction);

  nsAtom* actionRaw = attr ? attr->GetAtomValue() : nsGkAtoms::_empty;
  InvokeAction action = GetInvokeAction(actionRaw);

  // 5.3. Otherwise, if the result of running invokee's corresponding is valid
  // invoke action steps given action is not true, then return.
  if (action != InvokeAction::Custom && !invokee->IsValidInvokeAction(action)) {
    return;
  }

  // 6. Let continue be the result of firing an event named invoke at invokee,
  // using InvokeEvent, with its action attribute initialized to action's value,
  // its invoker attribute initialized to element, and its cancelable and
  // composed attributes initialized to true.
  InvokeEventInit init;
  actionRaw->ToString(init.mAction);
  init.mInvoker = this;
  init.mCancelable = true;
  init.mComposed = true;
  RefPtr<Event> event = InvokeEvent::Constructor(this, u"invoke"_ns, init);
  event->SetTrusted(true);
  event->SetTarget(invokee);

  EventDispatcher::DispatchDOMEvent(invokee, nullptr, event, nullptr, nullptr);

  // 7. If continue is false, then return.
  // 8. If isCustom is true, then return.
  if (action == InvokeAction::Custom || event->DefaultPrevented()) {
    return;
  }

  invokee->HandleInvokeInternal(this, action, IgnoreErrors());
}

bool nsGenericHTMLElement::IsValidInvokeAction(InvokeAction aAction) const {
  return Element::IsValidInvokeAction(aAction) ||
         aAction == InvokeAction::ShowPopover ||
         aAction == InvokeAction::TogglePopover ||
         aAction == InvokeAction::HidePopover;
}

MOZ_CAN_RUN_SCRIPT bool nsGenericHTMLElement::HandleInvokeInternal(
    Element* aInvoker, InvokeAction aAction, ErrorResult& aRv) {
  if (Element::HandleInvokeInternal(aInvoker, aAction, aRv)) {
    return true;
  }

  // If the element is a `popover` then we may want to handle the
  // invokeaction...
  auto popoverState = GetPopoverAttributeState();
  if (popoverState == PopoverAttributeState::None) {
    return false;
  }

  const bool canShow = aAction == InvokeAction::Auto ||
                       aAction == InvokeAction::TogglePopover ||
                       aAction == InvokeAction::ShowPopover;
  const bool canHide = aAction == InvokeAction::Auto ||
                       aAction == InvokeAction::TogglePopover ||
                       aAction == InvokeAction::HidePopover;

  if (canShow && !IsPopoverOpen()) {
    ShowPopoverInternal(aInvoker, aRv);
    return true;
  }

  if (canHide && IsPopoverOpen()) {
    HidePopoverInternal(/* aFocusPreviousElement = */ true,
                        /* aFireEvents = */ true, IgnoreErrors());
    return true;
  }

  return false;
}

void nsGenericHTMLFormControlElementWithState::GenerateStateKey() {
  // Keep the key if already computed
  if (!mStateKey.IsVoid()) {
    return;
  }

  Document* doc = GetUncomposedDoc();
  if (!doc) {
    mStateKey.Truncate();
    return;
  }

  // Generate the state key
  nsContentUtils::GenerateStateKey(this, doc, mStateKey);

  // If the state key is blank, this is anonymous content or for whatever
  // reason we are not supposed to save/restore state: keep it as such.
  if (!mStateKey.IsEmpty()) {
    // Add something unique to content so layout doesn't muck us up.
    mStateKey += "-C";
  }
}

PresState* nsGenericHTMLFormControlElementWithState::GetPrimaryPresState() {
  if (mStateKey.IsEmpty()) {
    return nullptr;
  }

  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistory(false);

  if (!history) {
    return nullptr;
  }

  // Get the pres state for this key, if it doesn't exist, create one.
  PresState* result = history->GetState(mStateKey);
  if (!result) {
    UniquePtr<PresState> newState = NewPresState();
    result = newState.get();
    history->AddState(mStateKey, std::move(newState));
  }

  return result;
}

already_AddRefed<nsILayoutHistoryState>
nsGenericHTMLFormElement::GetLayoutHistory(bool aRead) {
  nsCOMPtr<Document> doc = GetUncomposedDoc();
  if (!doc) {
    return nullptr;
  }

  //
  // Get the history
  //
  nsCOMPtr<nsILayoutHistoryState> history = doc->GetLayoutHistoryState();
  if (!history) {
    return nullptr;
  }

  if (aRead && !history->HasStates()) {
    return nullptr;
  }

  return history.forget();
}

bool nsGenericHTMLFormControlElementWithState::RestoreFormControlState() {
  MOZ_ASSERT(!mStateKey.IsVoid(),
             "GenerateStateKey must already have been called");

  if (mStateKey.IsEmpty()) {
    return false;
  }

  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistory(true);
  if (!history) {
    return false;
  }

  // Get the pres state for this key
  PresState* state = history->GetState(mStateKey);
  if (state) {
    bool result = RestoreState(state);
    history->RemoveState(mStateKey);
    return result;
  }

  return false;
}

void nsGenericHTMLFormControlElementWithState::NodeInfoChanged(
    Document* aOldDoc) {
  nsGenericHTMLFormControlElement::NodeInfoChanged(aOldDoc);

  // We need to regenerate the state key now we're in a new document.  Clearing
  // mControlNumber means we stop considering this control to be parser
  // inserted, and we'll generate a state key based on its position in the
  // document rather than the order it was inserted into the document.
  mControlNumber = -1;
  mStateKey.SetIsVoid(true);
}

void nsGenericHTMLFormControlElementWithState::GetFormAction(nsString& aValue) {
  auto type = ControlType();
  if (!IsInputElement(type) && !IsButtonElement(type)) {
    return;
  }

  if (!GetAttr(nsGkAtoms::formaction, aValue) || aValue.IsEmpty()) {
    Document* document = OwnerDoc();
    nsIURI* docURI = document->GetDocumentURI();
    if (docURI) {
      nsAutoCString spec;
      nsresult rv = docURI->GetSpec(spec);
      if (NS_FAILED(rv)) {
        return;
      }

      CopyUTF8toUTF16(spec, aValue);
    }
  } else {
    GetURIAttr(nsGkAtoms::formaction, nullptr, aValue);
  }
}

bool nsGenericHTMLElement::IsEventAttributeNameInternal(nsAtom* aName) {
  return nsContentUtils::IsEventAttributeName(aName, EventNameType_HTML);
}

/**
 * Construct a URI from a string, as an element.src attribute
 * would be set to. Helper for the media elements.
 */
nsresult nsGenericHTMLElement::NewURIFromString(const nsAString& aURISpec,
                                                nsIURI** aURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  *aURI = nullptr;

  nsCOMPtr<Document> doc = OwnerDoc();

  nsresult rv = nsContentUtils::NewURIWithDocumentCharset(aURI, aURISpec, doc,
                                                          GetBaseURI());
  NS_ENSURE_SUCCESS(rv, rv);

  bool equal;
  if (aURISpec.IsEmpty() && doc->GetDocumentURI() &&
      NS_SUCCEEDED(doc->GetDocumentURI()->Equals(*aURI, &equal)) && equal) {
    // Assume an element can't point to a fragment of its embedding
    // document. Fail here instead of returning the recursive URI
    // and waiting for the subsequent load to fail.
    NS_RELEASE(*aURI);
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  return NS_OK;
}

void nsGenericHTMLElement::GetInnerText(mozilla::dom::DOMString& aValue,
                                        mozilla::ErrorResult& aError) {
  // innerText depends on layout. For example, white space processing is
  // something that happens during reflow and which must be reflected by
  // innerText.  So for:
  //
  //   <div style="white-space:normal"> A     B C </div>
  //
  // innerText should give "A B C".
  //
  // The approach taken here to avoid the expense of reflow is to flush style
  // and then see whether it's necessary to flush layout afterwards. Flushing
  // layout can be skipped if we can detect that the element or its descendants
  // are not dirty.

  // Obtain the composed doc to handle elements in Shadow DOM.
  Document* doc = GetComposedDoc();
  if (doc) {
    doc->FlushPendingNotifications(FlushType::Style);
  }

  // Elements with `display: content` will not have a frame. To handle Shadow
  // DOM, walk the flattened tree looking for parent frame.
  nsIFrame* frame = GetPrimaryFrame();
  if (IsDisplayContents()) {
    for (Element* parent = GetFlattenedTreeParentElement(); parent;
         parent = parent->GetFlattenedTreeParentElement()) {
      frame = parent->GetPrimaryFrame();
      if (frame) {
        break;
      }
    }
  }

  // Check for dirty reflow roots in the subtree from targetFrame; this requires
  // a reflow flush.
  bool dirty = frame && frame->PresShell()->FrameIsAncestorOfDirtyRoot(frame);

  // The way we do that is by checking whether the element has either of the two
  // dirty bits (NS_FRAME_IS_DIRTY or NS_FRAME_HAS_DIRTY_DESCENDANTS) or if any
  // ancestor has NS_FRAME_IS_DIRTY.  We need to check for NS_FRAME_IS_DIRTY on
  // ancestors since that is something that implies NS_FRAME_IS_DIRTY on all
  // descendants.
  dirty |= frame && frame->HasAnyStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
  while (!dirty && frame) {
    dirty |= frame->HasAnyStateBits(NS_FRAME_IS_DIRTY);
    frame = frame->GetInFlowParent();
  }

  // Flush layout if we determined a reflow is required.
  if (dirty && doc) {
    doc->FlushPendingNotifications(FlushType::Layout);
  }

  if (!IsRendered()) {
    GetTextContentInternal(aValue, aError);
  } else {
    nsRange::GetInnerTextNoFlush(aValue, aError, this);
  }
}

static already_AddRefed<nsINode> TextToNode(const nsAString& aString,
                                            nsNodeInfoManager* aNim) {
  nsString str;
  const char16_t* s = aString.BeginReading();
  const char16_t* end = aString.EndReading();
  RefPtr<DocumentFragment> fragment;
  while (true) {
    if (s != end && *s == '\r' && s + 1 != end && s[1] == '\n') {
      // a \r\n pair should only generate one <br>, so just skip the \r
      ++s;
    }
    if (s == end || *s == '\r' || *s == '\n') {
      if (!str.IsEmpty()) {
        RefPtr<nsTextNode> textContent = new (aNim) nsTextNode(aNim);
        textContent->SetText(str, true);
        if (!fragment) {
          if (s == end) {
            return textContent.forget();
          }
          fragment = new (aNim) DocumentFragment(aNim);
        }
        fragment->AppendChildTo(textContent, true, IgnoreErrors());
      }
      if (s == end) {
        break;
      }
      str.Truncate();
      RefPtr<NodeInfo> ni = aNim->GetNodeInfo(
          nsGkAtoms::br, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);
      auto* nim = ni->NodeInfoManager();
      RefPtr<HTMLBRElement> br = new (nim) HTMLBRElement(ni.forget());
      if (!fragment) {
        if (s + 1 == end) {
          return br.forget();
        }
        fragment = new (aNim) DocumentFragment(aNim);
      }
      fragment->AppendChildTo(br, true, IgnoreErrors());
    } else {
      str.Append(*s);
    }
    ++s;
  }
  return fragment.forget();
}

void nsGenericHTMLElement::SetInnerText(const nsAString& aValue) {
  RefPtr<nsINode> node = TextToNode(aValue, NodeInfo()->NodeInfoManager());
  ReplaceChildren(node, IgnoreErrors());
}

// https://html.spec.whatwg.org/#merge-with-the-next-text-node
static void MergeWithNextTextNode(Text& aText, ErrorResult& aRv) {
  RefPtr<Text> nextSibling = Text::FromNodeOrNull(aText.GetNextSibling());
  if (!nextSibling) {
    return;
  }
  nsAutoString data;
  nextSibling->GetData(data);
  aText.AppendData(data, aRv);
  nextSibling->Remove();
}

// https://html.spec.whatwg.org/#dom-outertext
void nsGenericHTMLElement::SetOuterText(const nsAString& aValue,
                                        ErrorResult& aRv) {
  nsCOMPtr<nsINode> parent = GetParentNode();
  if (!parent) {
    return aRv.ThrowNoModificationAllowedError("Element has no parent");
  }

  RefPtr<nsINode> next = GetNextSibling();
  RefPtr<nsINode> previous = GetPreviousSibling();

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(OwnerDoc(), nullptr);

  nsNodeInfoManager* nim = NodeInfo()->NodeInfoManager();
  RefPtr<nsINode> node = TextToNode(aValue, nim);
  if (!node) {
    // This doesn't match the spec, see
    // https://github.com/whatwg/html/issues/7508
    node = new (nim) nsTextNode(nim);
  }
  parent->ReplaceChild(*node, *this, aRv);
  if (aRv.Failed()) {
    return;
  }

  if (next) {
    if (RefPtr<Text> text = Text::FromNodeOrNull(next->GetPreviousSibling())) {
      MergeWithNextTextNode(*text, aRv);
      if (aRv.Failed()) {
        return;
      }
    }
  }
  if (auto* text = Text::FromNodeOrNull(previous)) {
    MergeWithNextTextNode(*text, aRv);
  }
}

// This should be true when `:open` should match.
bool nsGenericHTMLElement::PopoverOpen() const {
  if (PopoverData* popoverData = GetPopoverData()) {
    return popoverData->GetPopoverVisibilityState() ==
           PopoverVisibilityState::Showing;
  }
  return false;
}

// https://html.spec.whatwg.org/#check-popover-validity
bool nsGenericHTMLElement::CheckPopoverValidity(
    PopoverVisibilityState aExpectedState, Document* aExpectedDocument,
    ErrorResult& aRv) {
  if (GetPopoverAttributeState() == PopoverAttributeState::None) {
    aRv.ThrowNotSupportedError("Element is in the no popover state");
    return false;
  }

  if (GetPopoverData()->GetPopoverVisibilityState() != aExpectedState) {
    return false;
  }

  if (!IsInComposedDoc()) {
    aRv.ThrowInvalidStateError("Element is not connected");
    return false;
  }

  if (aExpectedDocument && aExpectedDocument != OwnerDoc()) {
    aRv.ThrowInvalidStateError("Element is moved to other document");
    return false;
  }

  if (auto* dialog = HTMLDialogElement::FromNode(this)) {
    if (dialog->IsInTopLayer()) {
      aRv.ThrowInvalidStateError("Element is a modal <dialog> element");
      return false;
    }
  }

  if (State().HasState(ElementState::FULLSCREEN)) {
    aRv.ThrowInvalidStateError("Element is fullscreen");
    return false;
  }

  return true;
}

PopoverAttributeState nsGenericHTMLElement::GetPopoverAttributeState() const {
  return GetPopoverData() ? GetPopoverData()->GetPopoverAttributeState()
                          : PopoverAttributeState::None;
}

void nsGenericHTMLElement::PopoverPseudoStateUpdate(bool aOpen, bool aNotify) {
  SetStates(ElementState::POPOVER_OPEN, aOpen, aNotify);
}

already_AddRefed<ToggleEvent> nsGenericHTMLElement::CreateToggleEvent(
    const nsAString& aEventType, const nsAString& aOldState,
    const nsAString& aNewState, Cancelable aCancelable) {
  ToggleEventInit init;
  init.mBubbles = false;
  init.mOldState = aOldState;
  init.mNewState = aNewState;
  init.mCancelable = aCancelable == Cancelable::eYes;
  RefPtr<ToggleEvent> event = ToggleEvent::Constructor(this, aEventType, init);
  event->SetTrusted(true);
  event->SetTarget(this);
  return event.forget();
}

bool nsGenericHTMLElement::FireToggleEvent(const nsAString& aOldState,
                                           const nsAString& aNewState,
                                           const nsAString& aType) {
  const auto cancelable = aType == u"beforetoggle"_ns && aNewState == u"open"_ns
                              ? Cancelable::eYes
                              : Cancelable::eNo;
  RefPtr event = CreateToggleEvent(aType, aOldState, aNewState, cancelable);
  EventDispatcher::DispatchDOMEvent(this, nullptr, event, nullptr, nullptr);
  return event->DefaultPrevented();
}

// https://html.spec.whatwg.org/#queue-a-popover-toggle-event-task
void nsGenericHTMLElement::QueuePopoverEventTask(
    PopoverVisibilityState aOldState) {
  auto* data = GetPopoverData();
  MOZ_ASSERT(data, "Should have popover data");

  if (auto* queuedToggleEventTask = data->GetToggleEventTask()) {
    aOldState = queuedToggleEventTask->GetOldState();
  }

  auto task =
      MakeRefPtr<PopoverToggleEventTask>(do_GetWeakReference(this), aOldState);
  data->SetToggleEventTask(task);
  OwnerDoc()->Dispatch(task.forget());
}

void nsGenericHTMLElement::RunPopoverToggleEventTask(
    PopoverToggleEventTask* aTask, PopoverVisibilityState aOldState) {
  auto* data = GetPopoverData();
  if (!data) {
    return;
  }

  auto* popoverToggleEventTask = data->GetToggleEventTask();
  if (!popoverToggleEventTask || aTask != popoverToggleEventTask) {
    return;
  }
  data->ClearToggleEventTask();
  // Intentionally ignore the return value here as only on open event the
  // cancelable attribute is initialized to true for beforetoggle event.
  auto stringForState = [](PopoverVisibilityState state) {
    return state == PopoverVisibilityState::Hidden ? u"closed"_ns : u"open"_ns;
  };
  FireToggleEvent(stringForState(aOldState),
                  stringForState(data->GetPopoverVisibilityState()),
                  u"toggle"_ns);
}

// https://html.spec.whatwg.org/#dom-showpopover
void nsGenericHTMLElement::ShowPopover(ErrorResult& aRv) {
  return ShowPopoverInternal(nullptr, aRv);
}
void nsGenericHTMLElement::ShowPopoverInternal(Element* aInvoker,
                                               ErrorResult& aRv) {
  if (!CheckPopoverValidity(PopoverVisibilityState::Hidden, nullptr, aRv)) {
    return;
  }
  RefPtr<Document> document = OwnerDoc();

  MOZ_ASSERT(!GetPopoverData() || !GetPopoverData()->GetInvoker());
  MOZ_ASSERT(!OwnerDoc()->TopLayerContains(*this));

  bool wasShowingOrHiding = GetPopoverData()->IsShowingOrHiding();
  GetPopoverData()->SetIsShowingOrHiding(true);
  auto cleanupShowingFlag = MakeScopeExit([&]() {
    if (auto* popoverData = GetPopoverData()) {
      popoverData->SetIsShowingOrHiding(wasShowingOrHiding);
    }
  });

  // Fire beforetoggle event and re-check popover validity.
  if (FireToggleEvent(u"closed"_ns, u"open"_ns, u"beforetoggle"_ns)) {
    return;
  }
  if (!CheckPopoverValidity(PopoverVisibilityState::Hidden, document, aRv)) {
    return;
  }

  bool shouldRestoreFocus = false;
  nsWeakPtr originallyFocusedElement;
  if (IsAutoPopover()) {
    auto originalState = GetPopoverAttributeState();
    RefPtr<nsINode> ancestor = GetTopmostPopoverAncestor(aInvoker, true);
    if (!ancestor) {
      ancestor = document;
    }
    document->HideAllPopoversUntil(*ancestor, false,
                                   /* aFireEvents = */ !wasShowingOrHiding);
    if (GetPopoverAttributeState() != originalState) {
      aRv.ThrowInvalidStateError(
          "The value of the popover attribute was changed while hiding the "
          "popover.");
      return;
    }

    // TODO: Handle if document changes, see
    // https://github.com/whatwg/html/issues/9177
    if (!IsAutoPopover() ||
        !CheckPopoverValidity(PopoverVisibilityState::Hidden, document, aRv)) {
      return;
    }

    shouldRestoreFocus = !document->GetTopmostAutoPopover();
    // Let originallyFocusedElement be document's focused area of the document's
    // DOM anchor.
    if (nsIContent* unretargetedFocus =
            document->GetUnretargetedFocusedContent()) {
      originallyFocusedElement =
          do_GetWeakReference(unretargetedFocus->AsElement());
    }

    if (StaticPrefs::dom_closewatcher_enabled()) {
      GetPopoverData()->EnsureCloseWatcher(this);
    }
  }

  document->AddPopoverToTopLayer(*this);

  PopoverPseudoStateUpdate(true, true);

  {
    auto* popoverData = GetPopoverData();
    popoverData->SetPopoverVisibilityState(PopoverVisibilityState::Showing);
    popoverData->SetInvoker(aInvoker);
  }

  // Run the popover focusing steps given element.
  FocusPopover();
  if (shouldRestoreFocus &&
      GetPopoverAttributeState() != PopoverAttributeState::None) {
    GetPopoverData()->SetPreviouslyFocusedElement(originallyFocusedElement);
  }

  // Queue popover toggle event task.
  QueuePopoverEventTask(PopoverVisibilityState::Hidden);
}

void nsGenericHTMLElement::HidePopoverWithoutRunningScript() {
  HidePopoverInternal(/* aFocusPreviousElement = */ false,
                      /* aFireEvents = */ false, IgnoreErrors());
}

// https://html.spec.whatwg.org/#dom-hidepopover
void nsGenericHTMLElement::HidePopover(ErrorResult& aRv) {
  HidePopoverInternal(/* aFocusPreviousElement = */ true,
                      /* aFireEvents = */ true, aRv);
}

void nsGenericHTMLElement::HidePopoverInternal(bool aFocusPreviousElement,
                                               bool aFireEvents,
                                               ErrorResult& aRv) {
  OwnerDoc()->HidePopover(*this, aFocusPreviousElement, aFireEvents, aRv);
}

void nsGenericHTMLElement::ForgetPreviouslyFocusedElementAfterHidingPopover() {
  auto* data = GetPopoverData();
  MOZ_ASSERT(data, "Should have popover data");
  data->SetPreviouslyFocusedElement(nullptr);
}

void nsGenericHTMLElement::FocusPreviousElementAfterHidingPopover() {
  auto* data = GetPopoverData();
  MOZ_ASSERT(data, "Should have popover data");

  RefPtr<Element> control =
      do_QueryReferent(data->GetPreviouslyFocusedElement().get());
  data->SetPreviouslyFocusedElement(nullptr);

  if (!control) {
    return;
  }

  // Step 14.2 at
  // https://html.spec.whatwg.org/multipage/popover.html#hide-popover-algorithm
  // If focusPreviousElement is true and document's focused area of the
  // document's DOM anchor is a shadow-including inclusive descendant of
  // element, then run the focusing steps for previouslyFocusedElement;
  nsIContent* currentFocus = OwnerDoc()->GetUnretargetedFocusedContent();
  if (currentFocus &&
      currentFocus->IsShadowIncludingInclusiveDescendantOf(this)) {
    FocusOptions options;
    options.mPreventScroll = true;
    control->Focus(options, CallerType::NonSystem, IgnoreErrors());
  }
}

// https://html.spec.whatwg.org/multipage/popover.html#dom-togglepopover
bool nsGenericHTMLElement::TogglePopover(const Optional<bool>& aForce,
                                         ErrorResult& aRv) {
  if (PopoverOpen() && (!aForce.WasPassed() || !aForce.Value())) {
    HidePopover(aRv);
  } else if (!aForce.WasPassed() || aForce.Value()) {
    ShowPopover(aRv);
  } else {
    CheckPopoverValidity(GetPopoverData()
                             ? GetPopoverData()->GetPopoverVisibilityState()
                             : PopoverVisibilityState::Showing,
                         nullptr, aRv);
  }

  return PopoverOpen();
}

// https://html.spec.whatwg.org/multipage/popover.html#popover-focusing-steps
void nsGenericHTMLElement::FocusPopover() {
  if (auto* dialog = HTMLDialogElement::FromNode(this)) {
    return MOZ_KnownLive(dialog)->FocusDialog();
  }

  if (RefPtr<Document> doc = GetComposedDoc()) {
    doc->FlushPendingNotifications(FlushType::Frames);
  }

  RefPtr<Element> control = GetBoolAttr(nsGkAtoms::autofocus)
                                ? this
                                : GetAutofocusDelegate(IsFocusableFlags(0));
  if (!control) {
    return;
  }
  FocusCandidate(control, false /* aClearUpFocus */);
}

void nsGenericHTMLElement::FocusCandidate(Element* aControl,
                                          bool aClearUpFocus) {
  // 1) Run the focusing steps given control.
  IgnoredErrorResult rv;
  if (RefPtr<Element> elementToFocus = nsFocusManager::GetTheFocusableArea(
          aControl, nsFocusManager::ProgrammaticFocusFlags(FocusOptions()))) {
    elementToFocus->Focus(FocusOptions(), CallerType::NonSystem, rv);
    if (rv.Failed()) {
      return;
    }
  } else if (aClearUpFocus) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      // Clear the focus which ends up making the body gets focused
      nsCOMPtr<nsPIDOMWindowOuter> outerWindow = OwnerDoc()->GetWindow();
      fm->ClearFocus(outerWindow);
    }
  }

  // 2) Let topDocument be the active document of control's node document's
  // browsing context's top-level browsing context.
  // 3) If control's node document's origin is not the same as the origin of
  // topDocument, then return.
  BrowsingContext* bc = aControl->OwnerDoc()->GetBrowsingContext();
  if (bc && bc->IsInProcess() && bc->SameOriginWithTop()) {
    if (nsCOMPtr<nsIDocShell> docShell = bc->Top()->GetDocShell()) {
      if (Document* topDocument = docShell->GetExtantDocument()) {
        // 4) Empty topDocument's autofocus candidates.
        // 5) Set topDocument's autofocus processed flag to true.
        topDocument->SetAutoFocusFired();
      }
    }
  }
}

already_AddRefed<ElementInternals> nsGenericHTMLElement::AttachInternals(
    ErrorResult& aRv) {
  // ElementInternals is only available on autonomous custom element, so throws
  // an error by default. The spec steps are implemented in HTMLElement because
  // ElementInternals needs to hold a pointer to HTMLElement in order to forward
  // form operation to it.
  aRv.ThrowNotSupportedError(nsPrintfCString(
      "Cannot attach ElementInternals to a customized built-in or non-custom "
      "element "
      "'%s'",
      NS_ConvertUTF16toUTF8(NodeInfo()->NameAtom()->GetUTF16String()).get()));
  return nullptr;
}

ElementInternals* nsGenericHTMLElement::GetInternals() const {
  if (CustomElementData* data = GetCustomElementData()) {
    return data->GetElementInternals();
  }
  return nullptr;
}

bool nsGenericHTMLElement::IsFormAssociatedCustomElements() const {
  if (CustomElementData* data = GetCustomElementData()) {
    return data->IsFormAssociated();
  }
  return false;
}

void nsGenericHTMLElement::GetAutocapitalize(nsAString& aValue) const {
  GetEnumAttr(nsGkAtoms::autocapitalize, nullptr, kDefaultAutocapitalize->tag,
              aValue);
}

bool nsGenericHTMLElement::Translate() const {
  if (const nsAttrValue* attr = mAttrs.GetAttr(nsGkAtoms::translate)) {
    if (attr->IsEmptyString() || attr->Equals(nsGkAtoms::yes, eIgnoreCase)) {
      return true;
    }
    if (attr->Equals(nsGkAtoms::no, eIgnoreCase)) {
      return false;
    }
  }
  return nsGenericHTMLElementBase::Translate();
}

void nsGenericHTMLElement::GetPopover(nsString& aPopover) const {
  GetHTMLEnumAttr(nsGkAtoms::popover, aPopover);
  if (aPopover.IsEmpty() && !DOMStringIsNull(aPopover)) {
    aPopover.Assign(NS_ConvertUTF8toUTF16(kPopoverAttributeValueAuto));
  }
}

/******************************************************************************
 *  nsIFormControl
 *****************************************************************************/

// static
nsIFormControl* nsIFormControl::FromEventTarget(
    mozilla::dom::EventTarget* aTarget) {
  MOZ_ASSERT(aTarget);
  return aTarget->IsNode() ? aTarget->AsNode()->GetAsFormControl() : nullptr;
}

// static
nsIFormControl* nsIFormControl::FromEventTargetOrNull(
    mozilla::dom::EventTarget* aTarget) {
  return aTarget && aTarget->IsNode() ? aTarget->AsNode()->GetAsFormControl()
                                      : nullptr;
}

// static
const nsIFormControl* nsIFormControl::FromEventTarget(
    const mozilla::dom::EventTarget* aTarget) {
  MOZ_ASSERT(aTarget);
  return aTarget->IsNode() ? aTarget->AsNode()->GetAsFormControl() : nullptr;
}

// static
const nsIFormControl* nsIFormControl::FromEventTargetOrNull(
    const mozilla::dom::EventTarget* aTarget) {
  return aTarget && aTarget->IsNode() ? aTarget->AsNode()->GetAsFormControl()
                                      : nullptr;
}

// static
nsIFormControl* nsIFormControl::FromNode(nsINode* aNode) {
  MOZ_ASSERT(aNode);
  return aNode->GetAsFormControl();
}

// static
nsIFormControl* nsIFormControl::FromNodeOrNull(nsINode* aNode) {
  return aNode ? aNode->GetAsFormControl() : nullptr;
}

// static
const nsIFormControl* nsIFormControl::FromNode(const nsINode* aNode) {
  MOZ_ASSERT(aNode);
  return aNode->GetAsFormControl();
}

// static
const nsIFormControl* nsIFormControl::FromNodeOrNull(const nsINode* aNode) {
  return aNode ? aNode->GetAsFormControl() : nullptr;
}
