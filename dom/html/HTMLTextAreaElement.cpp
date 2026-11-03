/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLTextAreaElement.h"

#include "mozAutoDocUpdate.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/FocusModel.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresState.h"
#include "mozilla/TextControlState.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/HTMLTextAreaElementBinding.h"
#include "nsAttrValueInlines.h"
#include "nsBaseCommandController.h"
#include "nsContentCreatorFunctions.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsIConstraintValidation.h"
#include "nsIControllers.h"
#include "nsIFormControl.h"
#include "nsIFrame.h"
#include "nsIMutationObserver.h"
#include "nsLayoutUtils.h"
#include "nsLinebreakConverter.h"
#include "nsPlainTextSerializer.h"
#include "nsPresContext.h"
#include "nsReadableUtils.h"
#include "nsStyleConsts.h"
#include "nsTextControlFrame.h"
#include "nsThreadUtils.h"
#include "nsXULControllers.h"

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(TextArea)

namespace mozilla::dom {

HTMLTextAreaElement::HTMLTextAreaElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo, FromParser aFromParser)
    : TextControlElement(std::move(aNodeInfo), aFromParser,
                         FormControlType::Textarea),
      mDoneAddingChildren(!aFromParser),
      mInhibitStateRestoration(!!(aFromParser & FROM_PARSER_FRAGMENT)),
      mAutocompleteAttrState(nsContentUtils::eAutocompleteAttrState_Unknown),
      mAutocompleteInfoState(nsContentUtils::eAutocompleteAttrState_Unknown),
      mState(TextControlState::Construct(this)) {
  AddMutationObserver(this);

  // Set up our default state.  By default we're enabled (since we're
  // a control type that can be disabled but not actually disabled right now),
  // optional, read-write, and valid. Also by default we don't have to show
  // validity UI and so forth.
  AddStatesSilently(ElementState::ENABLED | ElementState::OPTIONAL_ |
                    ElementState::READWRITE | ElementState::VALID |
                    ElementState::VALUE_EMPTY);
  RemoveStatesSilently(ElementState::READONLY);
}

HTMLTextAreaElement::~HTMLTextAreaElement() {
  mState->Destroy();
  mState = nullptr;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLTextAreaElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLTextAreaElement,
                                                  TextControlElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mValidity)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mControllers)
  if (tmp->mState) {
    tmp->mState->Traverse(cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLTextAreaElement,
                                                TextControlElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mValidity)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mControllers)
  if (tmp->mState) {
    tmp->mState->Unlink();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(HTMLTextAreaElement,
                                             TextControlElement,
                                             nsIMutationObserver,
                                             nsIConstraintValidation)

// nsIDOMHTMLTextAreaElement

nsresult HTMLTextAreaElement::Clone(dom::NodeInfo* aNodeInfo,
                                    nsINode** aResult) const {
  *aResult = nullptr;
  RefPtr<HTMLTextAreaElement> it = new (aNodeInfo->NodeInfoManager())
      HTMLTextAreaElement(do_AddRef(aNodeInfo));

  nsresult rv = const_cast<HTMLTextAreaElement*>(this)->CopyInnerTo(it);
  NS_ENSURE_SUCCESS(rv, rv);

  it->SetLastValueChangeWasInteractive(mLastValueChangeWasInteractive);
  it.forget(aResult);
  return NS_OK;
}

// nsIContent

void HTMLTextAreaElement::Select() {
  if (FocusState() != FocusTristate::eUnfocusable) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      fm->SetFocus(this, nsIFocusManager::FLAG_NOSCROLL);
    }
  }

  // FIXME: The <input> equivalent has ScrollAfterSelection::No
  SetSelectionRange(0, UINT32_MAX, Optional<nsAString>(), IgnoreErrors());
}

enum class Wrap {
  Off,
  Hard,
  Soft,
};

static Wrap WrapValue(const HTMLTextAreaElement& aElement) {
  static mozilla::dom::Element::AttrValuesArray strings[] = {
      nsGkAtoms::HARD, nsGkAtoms::OFF, nullptr};
  switch (aElement.FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::wrap, strings,
                                   eIgnoreCase)) {
    case 0:
      return Wrap::Hard;
    case 1:
      return Wrap::Off;
    default:
      return Wrap::Soft;
  }
}

bool HTMLTextAreaElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                          bool* aIsFocusable,
                                          int32_t* aTabIndex) {
  if (nsGenericHTMLFormControlElementWithState::IsHTMLFocusable(
          aFlags, aIsFocusable, aTabIndex)) {
    return true;
  }

  // disabled textareas are not focusable
  *aIsFocusable = (aFlags & IsFocusableFlags::IgnoreDisabled) || !IsDisabled();
  return false;
}

int32_t HTMLTextAreaElement::TabIndexDefault() { return 0; }

void HTMLTextAreaElement::GetType(nsAString& aType) {
  aType.AssignLiteral("textarea");
}

void HTMLTextAreaElement::GetValue(nsAString& aValue) {
  GetValueInternal(aValue);
  MOZ_ASSERT(aValue.FindChar(static_cast<char16_t>('\r')) == -1);
}

void HTMLTextAreaElement::GetValueInternal(nsAString& aValue) const {
  MOZ_ASSERT(mState);
  mState->GetValue(aValue, /* aForDisplay = */ true);
}

nsIEditor* HTMLTextAreaElement::GetEditorForBindings() {
  if (!GetPrimaryFrame()) {
    GetPrimaryFrame(FlushType::Frames);
  }
  return GetTextEditor();
}

TextEditor* HTMLTextAreaElement::GetTextEditor() {
  MOZ_ASSERT(mState);
  return mState->GetTextEditor();
}

TextEditor* HTMLTextAreaElement::GetExtantTextEditor() const {
  MOZ_ASSERT(mState);
  return mState->GetExtantTextEditor();
}

nsISelectionController* HTMLTextAreaElement::GetSelectionController() {
  MOZ_ASSERT(mState);
  return mState->GetSelectionController();
}

nsFrameSelection* HTMLTextAreaElement::GetIndependentFrameSelection() const {
  MOZ_ASSERT(mState);
  return mState->GetIndependentFrameSelection();
}

nsresult HTMLTextAreaElement::SetValueInternal(
    const nsAString& aValue, const ValueSetterOptions& aOptions) {
  MOZ_ASSERT(mState);

  // Need to set the value changed flag here if our value has in fact changed
  // (i.e. if ValueSetterOption::SetValueChanged is in aOptions), so that
  // retrieves the correct value if needed.
  if (aOptions.contains(ValueSetterOption::SetValueChanged)) {
    SetValueChanged(true);
  }

  if (!mState->SetValue(aValue, aOptions)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

void HTMLTextAreaElement::SetValue(const nsAString& aValue,
                                   ErrorResult& aError) {
  // If the value has been set by a script, we basically want to keep the
  // current change event state. If the element is ready to fire a change
  // event, we should keep it that way. Otherwise, we should make sure the
  // element will not fire any event because of the script interaction.
  //
  // NOTE: this is currently quite expensive work (too much string
  // manipulation). We should probably optimize that.
  nsAutoString currentValue;
  GetValueInternal(currentValue);

  nsresult rv = SetValueInternal(
      aValue,
      {ValueSetterOption::ByContentAPI, ValueSetterOption::SetValueChanged,
       ValueSetterOption::MoveCursorToEndIfValueChanged});
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aError.Throw(rv);
    return;
  }

  if (mFocusedValue.Equals(currentValue)) {
    GetValueInternal(mFocusedValue);
  }
}

void HTMLTextAreaElement::SetUserInput(const nsAString& aValue,
                                       nsIPrincipal& aSubjectPrincipal) {
  SetValueInternal(aValue, {ValueSetterOption::BySetUserInputAPI,
                            ValueSetterOption::SetValueChanged,
                            ValueSetterOption::MoveCursorToEndIfValueChanged});
}

void HTMLTextAreaElement::SetValueChanged(bool aValueChanged) {
  MOZ_ASSERT(mState);

  bool previousValue = mValueChanged;
  mValueChanged = aValueChanged;
  if (mValueChanged == previousValue) {
    return;
  }
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  UpdateValidityElementStates(true);
}

void HTMLTextAreaElement::SetLastValueChangeWasInteractive(
    bool aWasInteractive) {
  if (aWasInteractive == mLastValueChangeWasInteractive) {
    return;
  }
  mLastValueChangeWasInteractive = aWasInteractive;
  const bool wasValid = IsValid();
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  if (wasValid != IsValid()) {
    UpdateValidityElementStates(true);
  }
}

void HTMLTextAreaElement::GetDefaultValue(nsAString& aDefaultValue,
                                          ErrorResult& aError) const {
  if (!nsContentUtils::GetNodeTextContent(this, false, aDefaultValue,
                                          fallible)) {
    aError.Throw(NS_ERROR_OUT_OF_MEMORY);
  }
}

void HTMLTextAreaElement::SetDefaultValue(const nsAString& aDefaultValue,
                                          ErrorResult& aError) {
  // setting the value of an textarea element using `.defaultValue = "foo"`
  // must be interpreted as a two-step operation:
  // 1. clearing all child nodes
  // 2. adding a new text node with the new content
  // Step 1 must therefore collapse the Selection to 0.
  // Calling `SetNodeTextContent()` with an empty string will do that for us.
  nsContentUtils::SetNodeTextContent(this, EmptyString(), true);
  nsresult rv = nsContentUtils::SetNodeTextContent(this, aDefaultValue, true);
  if (NS_SUCCEEDED(rv) && !mValueChanged) {
    Reset();
  }
  if (NS_FAILED(rv)) {
    aError.Throw(rv);
  }
}

bool HTMLTextAreaElement::ParseAttribute(int32_t aNamespaceID,
                                         nsAtom* aAttribute,
                                         const nsAString& aValue,
                                         nsIPrincipal* aMaybeScriptedPrincipal,
                                         nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::maxlength ||
        aAttribute == nsGkAtoms::minlength) {
      return aResult.ParseNonNegativeIntValue(aValue);
    } else if (aAttribute == nsGkAtoms::cols) {
      aResult.ParseIntWithFallback(aValue, DEFAULT_COLS);
      return true;
    } else if (aAttribute == nsGkAtoms::rows) {
      aResult.ParseIntWithFallback(aValue, DEFAULT_ROWS_TEXTAREA);
      return true;
    } else if (aAttribute == nsGkAtoms::autocomplete) {
      aResult.ParseAtomArray(aValue);
      return true;
    }
  }
  return TextControlElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                            aMaybeScriptedPrincipal, aResult);
}

void HTMLTextAreaElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  // wrap=off
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::wrap);
  if (value &&
      (value->Type() == nsAttrValue::eString ||
       value->Type() == nsAttrValue::eAtom) &&
      value->Equals(nsGkAtoms::OFF, eIgnoreCase)) {
    // Equivalent to expanding `white-space; pre`
    aBuilder.SetKeywordValue(eCSSProperty_white_space_collapse,
                             StyleWhiteSpaceCollapse::Preserve);
    aBuilder.SetKeywordValue(eCSSProperty_text_wrap_mode,
                             StyleTextWrapMode::Nowrap);
  }

  nsGenericHTMLFormControlElementWithState::MapDivAlignAttributeInto(aBuilder);
  nsGenericHTMLFormControlElementWithState::MapCommonAttributesInto(aBuilder);
}

nsChangeHint HTMLTextAreaElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  nsChangeHint retval =
      nsGenericHTMLFormControlElementWithState::GetAttributeChangeHint(
          aAttribute, aModType);
  if (aAttribute == nsGkAtoms::rows || aAttribute == nsGkAtoms::cols) {
    retval |= NS_STYLE_HINT_REFLOW;
  } else if (aAttribute == nsGkAtoms::wrap) {
    retval |= nsChangeHint_ReconstructFrame;
  }
  return retval;
}

bool HTMLTextAreaElement::IsNoNamespaceAttrMapped(
    const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {{nsGkAtoms::wrap},
                                                    {nullptr}};

  static const MappedAttributeEntry* const map[] = {
      attributes,
      sDivAlignAttributeMap,
      sCommonAttributeMap,
  };

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc HTMLTextAreaElement::GetAttributeMappingFunction()
    const {
  return &MapAttributesIntoRule;
}

bool HTMLTextAreaElement::IsDisabledForEvents(WidgetEvent* aEvent) {
  return IsElementDisabledForEvents(aEvent, GetPrimaryFrame());
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY
void HTMLTextAreaElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = false;
  if (IsDisabledForEvents(aVisitor.mEvent)) {
    return;
  }

  if (NeedToInitializeEditorForEvent(aVisitor)) {
    // FIXME(bug 2020902): This is rather evil. Remove
    // CAN_RUN_SCRIPT_BOUNDARY when removing this.
    mState->EnsureEditorInitialized();
  }

  // Don't dispatch a second select event if we are already handling
  // one.
  if (aVisitor.mEvent->mMessage == eFormSelect) {
    if (mHandlingSelect) {
      return;
    }
    mHandlingSelect = true;
  } else if (aVisitor.mEvent->mMessage == eFocus ||
             aVisitor.mEvent->mMessage == eBlur) {
    // - eFocus: Set mWantsPreHandleEvent and set mFocusedValue in
    // PreHandleEvent before TextEditor handles the focus.
    // - eBlur: Set mWantsPreHandleEvent and fire change event in PreHandleEvent
    // to prevent it breaks event target chain creation.
    aVisitor.mWantsPreHandleEvent = true;
  }

  nsGenericHTMLFormControlElementWithState::GetEventTargetParent(aVisitor);
}

nsresult HTMLTextAreaElement::PreHandleEvent(EventChainVisitor& aVisitor) {
  if (aVisitor.mEvent->mMessage == eFocus) {
    // XXX Should we restrict this only when the event is trusted?
    GetValueInternal(mFocusedValue);
  } else if (aVisitor.mEvent->mMessage == eBlur) {
    // Fire onchange (if necessary), before we do the blur, bug 370521.
    FireChangeEventIfNeeded();
  }
  return nsGenericHTMLFormControlElementWithState::PreHandleEvent(aVisitor);
}

void HTMLTextAreaElement::FireChangeEventIfNeeded() {
  nsString value;
  GetValueInternal(value);

  // NOTE(emilio): This is not quite on the spec, but matches <input>, see
  // https://github.com/whatwg/html/issues/10011 and
  // https://github.com/whatwg/html/issues/10013
  if (mValueChanged) {
    SetUserInteracted(true);
  }

  if (mFocusedValue.Equals(value)) {
    return;
  }

  // Dispatch the change event.
  mFocusedValue = std::move(value);
  nsContentUtils::DispatchTrustedEvent(this, u"change"_ns, CanBubble::eYes,
                                       Cancelable::eNo);
}

nsresult HTMLTextAreaElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  if (aVisitor.mEvent->mMessage == eFormSelect) {
    mHandlingSelect = false;
  }
  return NS_OK;
}

void HTMLTextAreaElement::DoneAddingChildren(bool aHaveNotified) {
  if (!mValueChanged) {
    if (!mDoneAddingChildren) {
      // Reset now that we're done adding children if the content sink tried to
      // sneak some text in without calling AppendChildTo.
      Reset();
    }

    if (!mInhibitStateRestoration) {
      GenerateStateKey();
      RestoreFormControlState();
    }
  }

  mDoneAddingChildren = true;
}

// Controllers Methods

nsIControllers* HTMLTextAreaElement::GetControllers(ErrorResult& aError) {
  if (!mControllers) {
    mControllers = new nsXULControllers();
    if (!mControllers) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    RefPtr<nsBaseCommandController> commandController =
        nsBaseCommandController::CreateEditorController();
    if (!commandController) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    mControllers->AppendController(commandController);

    commandController = nsBaseCommandController::CreateEditingController();
    if (!commandController) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    mControllers->AppendController(commandController);
  }

  return GetExtantControllers();
}

nsresult HTMLTextAreaElement::GetControllers(nsIControllers** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  ErrorResult error;
  *aResult = GetControllers(error);
  NS_IF_ADDREF(*aResult);

  return error.StealNSResult();
}

uint32_t HTMLTextAreaElement::GetTextLength() {
  nsAutoString val;
  GetValue(val);
  return val.Length();
}

Nullable<uint32_t> HTMLTextAreaElement::GetSelectionStart(ErrorResult& aError) {
  uint32_t selStart, selEnd;
  GetSelectionRange(&selStart, &selEnd, aError);
  return Nullable<uint32_t>(selStart);
}

void HTMLTextAreaElement::SetSelectionStart(
    const Nullable<uint32_t>& aSelectionStart, ErrorResult& aError) {
  MOZ_ASSERT(mState);
  mState->SetSelectionStart(aSelectionStart, aError);
}

Nullable<uint32_t> HTMLTextAreaElement::GetSelectionEnd(ErrorResult& aError) {
  uint32_t selStart, selEnd;
  GetSelectionRange(&selStart, &selEnd, aError);
  return Nullable<uint32_t>(selEnd);
}

void HTMLTextAreaElement::SetSelectionEnd(
    const Nullable<uint32_t>& aSelectionEnd, ErrorResult& aError) {
  MOZ_ASSERT(mState);
  mState->SetSelectionEnd(aSelectionEnd, aError);
}

void HTMLTextAreaElement::GetSelectionRange(uint32_t* aSelectionStart,
                                            uint32_t* aSelectionEnd,
                                            ErrorResult& aRv) {
  MOZ_ASSERT(mState);
  return mState->GetSelectionRange(aSelectionStart, aSelectionEnd, aRv);
}

void HTMLTextAreaElement::GetSelectionDirection(nsAString& aDirection,
                                                ErrorResult& aError) {
  MOZ_ASSERT(mState);
  mState->GetSelectionDirectionString(aDirection, aError);
}

void HTMLTextAreaElement::SetSelectionDirection(const nsAString& aDirection,
                                                ErrorResult& aError) {
  MOZ_ASSERT(mState);
  mState->SetSelectionDirection(aDirection, aError);
}

void HTMLTextAreaElement::SetSelectionRange(
    uint32_t aSelectionStart, uint32_t aSelectionEnd,
    const Optional<nsAString>& aDirection, ErrorResult& aError) {
  MOZ_ASSERT(mState);
  mState->SetSelectionRange(aSelectionStart, aSelectionEnd, aDirection, aError);
}

void HTMLTextAreaElement::SetRangeText(const nsAString& aReplacement,
                                       ErrorResult& aRv) {
  MOZ_ASSERT(mState);
  mState->SetRangeText(aReplacement, aRv);
}

void HTMLTextAreaElement::SetRangeText(const nsAString& aReplacement,
                                       uint32_t aStart, uint32_t aEnd,
                                       SelectionMode aSelectMode,
                                       ErrorResult& aRv) {
  MOZ_ASSERT(mState);
  mState->SetRangeText(aReplacement, aStart, aEnd, aSelectMode, aRv);
}

void HTMLTextAreaElement::GetValueFromSetRangeText(nsAString& aValue) {
  GetValueInternal(aValue);
}

nsresult HTMLTextAreaElement::SetValueFromSetRangeText(
    const nsAString& aValue) {
  return SetValueInternal(aValue, {ValueSetterOption::ByContentAPI,
                                   ValueSetterOption::BySetRangeTextAPI,
                                   ValueSetterOption::SetValueChanged});
}

nsresult HTMLTextAreaElement::Reset() {
  nsAutoString resetVal;
  GetDefaultValue(resetVal, IgnoreErrors());
  SetValueChanged(false);
  SetUserInteracted(false);
  return SetValueInternal(resetVal, ValueSetterOption::ByInternalAPI);
}

NS_IMETHODIMP
HTMLTextAreaElement::SubmitNamesValues(FormData* aFormData) {
  //
  // Get the name (if no name, no submit)
  //
  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);
  if (name.IsEmpty()) {
    return NS_OK;
  }

  // Get the value
  nsAutoString value;
  GetValueInternal(value);
  if (WrapValue(*this) == Wrap::Hard) {
    if (auto cols = GetWrapCols(); cols > 0) {
      int32_t flags = nsIDocumentEncoder::OutputLFLineBreak |
                      nsIDocumentEncoder::OutputPreformatted |
                      nsIDocumentEncoder::OutputPersistNBSP |
                      nsIDocumentEncoder::OutputBodyOnly |
                      nsIDocumentEncoder::OutputWrap;
      nsPlainTextSerializer::HardWrapString(value, cols, flags);
    }
  }

  // Submit name=value
  const nsresult rv = aFormData->AddNameValuePair(name, value);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Submit dirname=dir
  return SubmitDirnameDir(aFormData);
}

void HTMLTextAreaElement::SaveState() {
  // Only save if value != defaultValue (bug 62713)
  PresState* state = nullptr;
  if (mValueChanged) {
    state = GetPrimaryPresState();
    if (state) {
      nsAutoString value;
      GetValueInternal(value);

      if (NS_FAILED(nsLinebreakConverter::ConvertStringLineBreaks(
              value, nsLinebreakConverter::eLinebreakPlatform,
              nsLinebreakConverter::eLinebreakContent))) {
        NS_ERROR("Converting linebreaks failed!");
        return;
      }

      state->contentData() =
          TextContentData(value, mLastValueChangeWasInteractive);
    }
  }

  if (mDisabledChanged) {
    if (!state) {
      state = GetPrimaryPresState();
    }
    if (state) {
      // We do not want to save the real disabled state but the disabled
      // attribute.
      state->disabled() = HasAttr(nsGkAtoms::disabled);
      state->disabledSet() = true;
    }
  }
}

bool HTMLTextAreaElement::RestoreState(PresState* aState) {
  const PresContentData& state = aState->contentData();

  if (state.type() == PresContentData::TTextContentData) {
    ErrorResult rv;
    SetValue(state.get_TextContentData().value(), rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
      return false;
    }
    if (state.get_TextContentData().lastValueChangeWasInteractive()) {
      SetLastValueChangeWasInteractive(true);
    }
  }
  if (aState->disabledSet() && !aState->disabled()) {
    SetDisabled(false, IgnoreErrors());
  }

  return false;
}

void HTMLTextAreaElement::UpdateValidityElementStates(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::VALIDITY_STATES);
  if (!IsCandidateForConstraintValidation()) {
    return;
  }
  ElementState state;
  if (IsValid()) {
    state |= ElementState::VALID;
    if (mUserInteracted) {
      state |= ElementState::USER_VALID;
    }
  } else {
    state |= ElementState::INVALID;
    if (mUserInteracted) {
      state |= ElementState::USER_INVALID;
    }
  }
  AddStatesSilently(state);
}

nsresult HTMLTextAreaElement::BindToTree(BindContext& aContext,
                                         nsINode& aParent) {
  nsresult rv =
      nsGenericHTMLFormControlElementWithState::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set direction based on value if dir=auto
  ResetDirFormAssociatedElement(this, false, HasDirAuto());

  // If there is a disabled fieldset in the parent chain, the element is now
  // barred from constraint validation and can't suffer from value missing.
  UpdateValueMissingValidityState();
  UpdateBarredFromConstraintValidation();

  // And now make sure our state is up to date
  UpdateValidityElementStates(false);

  if (IsInComposedDoc()) {
    AttachAndSetUAShadowRoot(NotifyUAWidget::No, DelegatesFocus::No);
    if (auto* sr = GetShadowRoot()) {
      SetupShadowTree(*sr, /* aNotify = */ false);
    }
  }
  return rv;
}

void HTMLTextAreaElement::UnbindFromTree(UnbindContext& aContext) {
  if (IsInComposedDoc()) {
    TeardownUAShadowRoot(NotifyUAWidget::No);
  }
  nsGenericHTMLFormControlElementWithState::UnbindFromTree(aContext);

  // We might be no longer disabled because of parent chain changed.
  UpdateValueMissingValidityState();
  UpdateBarredFromConstraintValidation();

  // And now make sure our state is up to date
  UpdateValidityElementStates(false);
}

void HTMLTextAreaElement::BeforeSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                        const nsAttrValue* aValue,
                                        bool aNotify) {
  if (aNotify && aName == nsGkAtoms::disabled &&
      aNameSpaceID == kNameSpaceID_None) {
    mDisabledChanged = true;
  }

  return nsGenericHTMLFormControlElementWithState::BeforeSetAttr(
      aNameSpaceID, aName, aValue, aNotify);
}

void HTMLTextAreaElement::CharacterDataChanged(nsIContent* aContent,
                                               const CharacterDataChangeInfo&) {
  ContentChanged(aContent);
}

void HTMLTextAreaElement::ContentAppended(nsIContent* aFirstNewContent,
                                          const ContentAppendInfo&) {
  ContentChanged(aFirstNewContent);
}

void HTMLTextAreaElement::ContentInserted(nsIContent* aChild,
                                          const ContentInsertInfo&) {
  ContentChanged(aChild);
}

void HTMLTextAreaElement::ContentWillBeRemoved(nsIContent* aChild,
                                               const ContentRemoveInfo& aInfo) {
  if (mValueChanged || !mDoneAddingChildren ||
      (aInfo.mBatchRemovalState && !aInfo.mBatchRemovalState->mIsFirst) ||
      !nsContentUtils::IsInSameAnonymousTree(this, aChild)) {
    return;
  }
  if (mState->IsSelectionCached()) {
    // Collapse the selection when removing nodes if necessary, see bug 1818686.
    auto& props = mState->GetSelectionProperties();
    props.CollapseToStart();
  }
  nsContentUtils::AddScriptRunner(
      NewRunnableMethod("HTMLTextAreaElement::ResetIfUnchanged", this,
                        &HTMLTextAreaElement::ResetIfUnchanged));
}

void HTMLTextAreaElement::ContentChanged(nsIContent* aContent) {
  if (mValueChanged || !mDoneAddingChildren ||
      !nsContentUtils::IsInSameAnonymousTree(this, aContent)) {
    return;
  }
  // We should wait all ranges finish handling the mutation before updating
  // the anonymous subtree with a call of Reset.
  nsContentUtils::AddScriptRunner(
      NewRunnableMethod("HTMLTextAreaElement::ResetIfUnchanged", this,
                        &HTMLTextAreaElement::ResetIfUnchanged));
}

void HTMLTextAreaElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                       const nsAttrValue* aValue,
                                       const nsAttrValue* aOldValue,
                                       nsIPrincipal* aSubjectPrincipal,
                                       bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::required || aName == nsGkAtoms::disabled ||
        aName == nsGkAtoms::readonly) {
      if (aName == nsGkAtoms::disabled) {
        // This *has* to be called *before* validity state check because
        // UpdateBarredFromConstraintValidation and
        // UpdateValueMissingValidityState depend on our disabled state.
        UpdateDisabledState(aNotify);
      }

      if (aName == nsGkAtoms::required) {
        // This *has* to be called *before* UpdateValueMissingValidityState
        // because UpdateValueMissingValidityState depends on our required
        // state.
        UpdateRequiredState(!!aValue, aNotify);
      }

      if (aName == nsGkAtoms::readonly && !!aValue != !!aOldValue) {
        UpdateReadOnlyState(aNotify);
      }

      UpdateValueMissingValidityState();

      // This *has* to be called *after* validity has changed.
      if (aName == nsGkAtoms::readonly || aName == nsGkAtoms::disabled) {
        UpdateBarredFromConstraintValidation();
      }
      UpdateValidityElementStates(aNotify);
    } else if (aName == nsGkAtoms::autocomplete) {
      // Clear the cached @autocomplete attribute state.
      mAutocompleteAttrState = nsContentUtils::eAutocompleteAttrState_Unknown;
      mAutocompleteInfoState = nsContentUtils::eAutocompleteAttrState_Unknown;
    } else if (aName == nsGkAtoms::maxlength) {
      UpdateTooLongValidityState();
      UpdateValidityElementStates(aNotify);
      if (auto* editor = GetExtantTextEditor()) {
        editor->SetMaxTextLength(UsedMaxLength());
      }
    } else if (aName == nsGkAtoms::minlength) {
      UpdateTooShortValidityState();
      UpdateValidityElementStates(aNotify);
    } else if (aName == nsGkAtoms::placeholder) {
      UpdatePlaceholder(aOldValue, aValue);
      UpdatePlaceholderShownState();
    } else if (aName == nsGkAtoms::dir && aValue &&
               aValue->Equals(nsGkAtoms::_auto, eIgnoreCase)) {
      ResetDirFormAssociatedElement(this, aNotify, true);
    }
  }

  return nsGenericHTMLFormControlElementWithState::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

nsresult HTMLTextAreaElement::CopyInnerTo(Element* aDest) {
  nsresult rv = nsGenericHTMLFormControlElementWithState::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mValueChanged || aDest->OwnerDoc()->IsStaticDocument()) {
    // Set our value on the clone.
    auto* dest = static_cast<HTMLTextAreaElement*>(aDest);

    nsAutoString value;
    GetValueInternal(value);

    // SetValueInternal handles setting mValueChanged for us. dest is a fresh
    // element so setting its value can't really run script.
    if (NS_WARN_IF(
            NS_FAILED(rv = MOZ_KnownLive(dest)->SetValueInternal(
                          value, {ValueSetterOption::SetValueChanged})))) {
      return rv;
    }
  }

  return NS_OK;
}

bool HTMLTextAreaElement::IsMutable() const { return !IsDisabledOrReadOnly(); }

void HTMLTextAreaElement::SetCustomValidity(const nsAString& aError) {
  ConstraintValidation::SetCustomValidity(aError);
  UpdateValidityElementStates(true);
}

bool HTMLTextAreaElement::IsTooLong() {
  if (!mValueChanged || !mLastValueChangeWasInteractive ||
      !HasAttr(nsGkAtoms::maxlength)) {
    return false;
  }

  int32_t maxLength = MaxLength();

  // Maxlength of -1 means parsing error.
  if (maxLength == -1) {
    return false;
  }

  int32_t textLength = GetTextLength();

  return textLength > maxLength;
}

bool HTMLTextAreaElement::IsTooShort() {
  if (!mValueChanged || !mLastValueChangeWasInteractive ||
      !HasAttr(nsGkAtoms::minlength)) {
    return false;
  }

  int32_t minLength = MinLength();

  // Minlength of -1 means parsing error.
  if (minLength == -1) {
    return false;
  }

  int32_t textLength = GetTextLength();

  return textLength && textLength < minLength;
}

bool HTMLTextAreaElement::IsValueMissing() const {
  if (!Required() || !IsMutable()) {
    return false;
  }
  return IsValueEmpty();
}

void HTMLTextAreaElement::UpdateTooLongValidityState() {
  SetValidityState(VALIDITY_STATE_TOO_LONG, IsTooLong());
}

void HTMLTextAreaElement::UpdateTooShortValidityState() {
  SetValidityState(VALIDITY_STATE_TOO_SHORT, IsTooShort());
}

void HTMLTextAreaElement::UpdateValueMissingValidityState() {
  SetValidityState(VALIDITY_STATE_VALUE_MISSING, IsValueMissing());
}

void HTMLTextAreaElement::UpdateBarredFromConstraintValidation() {
  SetBarredFromConstraintValidation(
      HasAttr(nsGkAtoms::readonly) ||
      HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR) || IsDisabled());
}

nsresult HTMLTextAreaElement::GetValidationMessage(
    nsAString& aValidationMessage, ValidityStateType aType) {
  nsresult rv = NS_OK;

  switch (aType) {
    case VALIDITY_STATE_TOO_LONG: {
      nsAutoString message;
      int32_t maxLength = MaxLength();
      int32_t textLength = GetTextLength();
      nsAutoString strMaxLength;
      nsAutoString strTextLength;

      strMaxLength.AppendInt(maxLength);
      strTextLength.AppendInt(textLength);

      rv = nsContentUtils::FormatMaybeLocalizedString(
          message, PropertiesFile::DOM_PROPERTIES, "FormValidationTextTooLong",
          OwnerDoc(), strMaxLength, strTextLength);
      aValidationMessage = std::move(message);
    } break;
    case VALIDITY_STATE_TOO_SHORT: {
      nsAutoString message;
      int32_t minLength = MinLength();
      int32_t textLength = GetTextLength();
      nsAutoString strMinLength;
      nsAutoString strTextLength;

      strMinLength.AppendInt(minLength);
      strTextLength.AppendInt(textLength);

      rv = nsContentUtils::FormatMaybeLocalizedString(
          message, PropertiesFile::DOM_PROPERTIES, "FormValidationTextTooShort",
          OwnerDoc(), strMinLength, strTextLength);
      aValidationMessage = std::move(message);
    } break;
    case VALIDITY_STATE_VALUE_MISSING: {
      nsAutoString message;
      rv = nsContentUtils::GetMaybeLocalizedString(
          PropertiesFile::DOM_PROPERTIES, "FormValidationValueMissing",
          OwnerDoc(), message);
      aValidationMessage = std::move(message);
    } break;
    default:
      rv =
          ConstraintValidation::GetValidationMessage(aValidationMessage, aType);
  }

  return rv;
}

Maybe<int32_t> HTMLTextAreaElement::GetCols() {
  const nsAttrValue* value = GetParsedAttr(nsGkAtoms::cols);
  if (!value || value->Type() != nsAttrValue::eInteger) {
    return {};
  }
  return Some(value->GetIntegerValue());
}

int32_t HTMLTextAreaElement::GetWrapCols() {
  if (WrapValue(*this) == Wrap::Off) {
    return 0;
  }
  // Otherwise we just wrap at the given number of columns
  return GetColsOrDefault();
}

int32_t HTMLTextAreaElement::GetRows() {
  const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::rows);
  if (attr && attr->Type() == nsAttrValue::eInteger) {
    int32_t rows = attr->GetIntegerValue();
    return (rows <= 0) ? DEFAULT_ROWS_TEXTAREA : rows;
  }

  return DEFAULT_ROWS_TEXTAREA;
}

void HTMLTextAreaElement::GetDefaultValueFromContent(nsAString& aValue, bool) {
  GetDefaultValue(aValue, IgnoreErrors());
}

bool HTMLTextAreaElement::ValueChanged() const { return mValueChanged; }

void HTMLTextAreaElement::GetTextEditorValue(nsAString& aValue) const {
  MOZ_ASSERT(mState);
  mState->GetValue(aValue, /* aForDisplay = */ true);
}

void HTMLTextAreaElement::UpdatePlaceholderShownState() {
  SetStates(ElementState::PLACEHOLDER_SHOWN,
            IsValueEmpty() && HasAttr(nsGkAtoms::placeholder));
}

void HTMLTextAreaElement::OnValueChanged(ValueChangeKind aKind,
                                         bool aNewValueEmpty,
                                         const nsAString* aKnownNewValue) {
  if (aKind != ValueChangeKind::Internal) {
    mLastValueChangeWasInteractive = aKind == ValueChangeKind::UserInteraction;
  }

  if (aNewValueEmpty != IsValueEmpty()) {
    SetStates(ElementState::VALUE_EMPTY, aNewValueEmpty);
    UpdatePlaceholderShownState();
  }

  // Update the validity state
  const bool validBefore = IsValid();
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  UpdateValueMissingValidityState();

  ResetDirFormAssociatedElement(this, true, HasDirAuto(), aKnownNewValue);

  if (validBefore != IsValid()) {
    UpdateValidityElementStates(true);
  }
}

bool HTMLTextAreaElement::HasCachedSelection() {
  MOZ_ASSERT(mState);
  return mState->IsSelectionCached();
}

void HTMLTextAreaElement::SetUserInteracted(bool aInteracted) {
  if (mUserInteracted == aInteracted) {
    return;
  }
  mUserInteracted = aInteracted;
  UpdateValidityElementStates(true);
}

void HTMLTextAreaElement::FieldSetDisabledChanged(bool aNotify) {
  // This *has* to be called before UpdateBarredFromConstraintValidation and
  // UpdateValueMissingValidityState because these two functions depend on our
  // disabled state.
  nsGenericHTMLFormControlElementWithState::FieldSetDisabledChanged(aNotify);

  UpdateValueMissingValidityState();
  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(true);
}

JSObject* HTMLTextAreaElement::WrapNode(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return HTMLTextAreaElement_Binding::Wrap(aCx, this, aGivenProto);
}

void HTMLTextAreaElement::GetAutocomplete(nsAString& aValue) {
  aValue.Truncate();
  const nsAttrValue* attributeVal = GetParsedAttr(nsGkAtoms::autocomplete);

  mAutocompleteAttrState = nsContentUtils::SerializeAutocompleteAttribute(
      attributeVal, aValue, mAutocompleteAttrState);
}

void HTMLTextAreaElement::GetAutocompleteInfo(AutocompleteInfo& aInfo) {
  const nsAttrValue* attributeVal = GetParsedAttr(nsGkAtoms::autocomplete);
  mAutocompleteInfoState = nsContentUtils::SerializeAutocompleteAttribute(
      attributeVal, aInfo, mAutocompleteInfoState, true);
}

}  // namespace mozilla::dom
