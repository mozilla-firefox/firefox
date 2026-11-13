/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLDialogElement.h"

#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/CloseWatcher.h"
#include "mozilla/dom/CloseWatcherManager.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/HTMLButtonElement.h"
#include "mozilla/dom/HTMLDialogElementBinding.h"
#include "mozilla/dom/UnbindContext.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIDOMEventListener.h"
#include "nsIFrame.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Dialog)

namespace mozilla::dom {

static constexpr nsAttrValue::EnumTableEntry kClosedbyTable[] = {
    {"", HTMLDialogElement::ClosedBy::Auto},
    {"none", HTMLDialogElement::ClosedBy::None},
    {"any", HTMLDialogElement::ClosedBy::Any},
    {"closerequest", HTMLDialogElement::ClosedBy::CloseRequest},
};

static constexpr const nsAttrValue::EnumTableEntry* kClosedbyAuto =
    &kClosedbyTable[0];
static constexpr const nsAttrValue::EnumTableEntry* kClosedbyDefault =
    &kClosedbyTable[1];
static constexpr const nsAttrValue::EnumTableEntry* kClosedbyModalDefault =
    &kClosedbyTable[3];

HTMLDialogElement::~HTMLDialogElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLDialogElement)

class DialogCloseWatcherListener : public nsIDOMEventListener {
 public:
  NS_DECL_ISUPPORTS

  explicit DialogCloseWatcherListener(HTMLDialogElement* aDialog) {
    mDialog = do_GetWeakReference(aDialog);
  }

  // https://html.spec.whatwg.org/#set-the-dialog-close-watcher
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
  HandleEvent(Event* aEvent) override {
    RefPtr<nsINode> node = do_QueryReferent(mDialog);
    if (HTMLDialogElement* dialog = HTMLDialogElement::FromNodeOrNull(node)) {
      nsAutoString eventType;
      aEvent->GetType(eventType);
      if (eventType.EqualsLiteral("cancel")) {
        // 3. - cancelAction given canPreventClose being to return the result of
        // firing an event named cancel at dialog, with the cancelable attribute
        // initialized to canPreventClose.
        bool defaultAction = true;
        auto cancelable =
            aEvent->Cancelable() ? Cancelable::eYes : Cancelable::eNo;
        nsContentUtils::DispatchTrustedEvent(MOZ_KnownLive(dialog),
                                             u"cancel"_ns, CanBubble::eNo,
                                             cancelable, &defaultAction);
        if (!defaultAction) {
          aEvent->PreventDefault();
        }
      } else if (eventType.EqualsLiteral("close")) {
        // 3. - closeAction being to close the dialog given dialog, dialog's
        // request close return value, and dialog's request close source
        // element.
        Maybe<nsAutoString> retValue;
        dialog->GetRequestCloseReturnValue(retValue);
        RefPtr<Element> source = dialog->GetRequestCloseSourceElement();
        dialog->Close(source, retValue);
      }
    }
    return NS_OK;
  }

 private:
  virtual ~DialogCloseWatcherListener() = default;
  nsWeakPtr mDialog;
};
NS_IMPL_ISUPPORTS(DialogCloseWatcherListener, nsIDOMEventListener)

// https://html.spec.whatwg.org/#computed-closed-by-state
void HTMLDialogElement::GetClosedBy(nsAString& aResult) const {
  aResult.Truncate();
  const nsAttrValue* val = mAttrs.GetAttr(nsGkAtoms::closedby);
  // 1. If the state of dialog's closedby attribute is Auto:
  if (!val || val->GetEnumValue() == kClosedbyAuto->value) {
    //  1.1. If dialog's is modal is true, then return Close Request.
    //  1.2. Return None.
    const char* tag =
        (IsInTopLayer() ? kClosedbyModalDefault->tag : kClosedbyDefault->tag);
    AppendASCIItoUTF16(nsDependentCString(tag), aResult);
    return;
  }
  // 2. Return the state of dialog's closedby attribute.
  val->GetEnumString(aResult, true);
}

// https://html.spec.whatwg.org/#computed-closed-by-state
HTMLDialogElement::ClosedBy HTMLDialogElement::GetClosedBy() const {
  const nsAttrValue* val = mAttrs.GetAttr(nsGkAtoms::closedby);
  // 1. If the state of dialog's closedby attribute is Auto:
  if (!val || val->GetEnumValue() == kClosedbyAuto->value) {
    //  1.1. If dialog's is modal is true, then return Close Request.
    //  1.2. Return None.
    return static_cast<ClosedBy>(IsInTopLayer() ? kClosedbyModalDefault->value
                                                : kClosedbyDefault->value);
  }
  // 2. Return the state of dialog's closedby attribute.
  return static_cast<ClosedBy>(val->GetEnumValue());
}

bool HTMLDialogElement::ParseClosedByAttribute(const nsAString& aValue,
                                               nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aValue, kClosedbyTable,
                                /* aCaseSensitive = */ false, kClosedbyAuto);
}

bool HTMLDialogElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::closedby) {
      return ParseClosedByAttribute(aValue, aResult);
    }
  }
  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

// https://html.spec.whatwg.org/#dom-dialog-close
// https://html.spec.whatwg.org/#close-the-dialog
void HTMLDialogElement::Close(Element* aSource,
                              const Maybe<nsAutoString>& aReturnValue) {
  // 1. If subject does not have an open attribute, then return.
  if (!Open()) {
    return;
  }

  // 2. Fire an event named beforetoggle, using ToggleEvent, with the oldState
  // attribute initialized to "open", the newState attribute initialized to
  // "closed", and the source attribute initialized to source at subject.
  FireToggleEvent(u"open"_ns, u"closed"_ns, u"beforetoggle"_ns, aSource);

  // 3. If subject does not have an open attribute, then return.
  if (!Open()) {
    return;
  }

  // 4. Queue a dialog toggle event task given subject, "open", "closed", and
  // source.
  QueueToggleEventTask(aSource);

  // 5. Remove subject's open attribute.
  SetOpen(false, IgnoreErrors());

  // 6. If is modal of subject is true, then request an element to be removed
  // from the top layer given subject.
  // 7. Let wasModal be the value of subject's is modal flag.
  bool wasModal = IsInTopLayer();
  // 8. Set is modal of subject to false.
  RemoveFromTopLayerIfNeeded();

  // 9. If result is not null, then set subject's returnValue attribute to
  // result.
  if (aReturnValue.isSome()) {
    SetReturnValue(aReturnValue.ref());
  }

  // 10. Set subject's request close return value to null.
  ClearRequestCloseReturnValue();

  // 11. Set subject's request close source element to null.
  mRequestCloseSourceElement = nullptr;

  MOZ_ASSERT(!OwnerDoc()->DialogIsInOpenDialogsList(*this),
             "Dialog should not being in Open Dialog List");

  // 12. If subject's previously focused element is not null, then:
  // 12.1. Let element be subject's previously focused element.
  RefPtr<Element> previouslyFocusedElement =
      do_QueryReferent(mPreviouslyFocusedElement);

  if (previouslyFocusedElement) {
    // 12.2. Set subject's previously focused element to null.
    mPreviouslyFocusedElement = nullptr;

    // 12.3. If subject's node document's focused area of the document's DOM
    // anchor is a shadow-including inclusive descendant of subject, or wasModal
    // is true, then run the focusing steps for element; the viewport should not
    // be scrolled by doing this step.
    bool resetFocus = true;
    if (!wasModal) {
      resetFocus = false;
      if (auto* focusedContent = OwnerDoc()->GetUnretargetedFocusedContent()) {
        // Actually use flat tree instead of shadow-including traversal to be
        // consistent with Chrome:
        // https://github.com/web-platform-tests/wpt/pull/39579#issuecomment-2666758496
        for (auto* dialog :
             focusedContent
                 ->InclusiveFlatTreeAncestorsOfType<HTMLDialogElement>()) {
          if (dialog == this) {
            resetFocus = true;
            break;
          }
        }
      }
    }
    if (resetFocus) {
      FocusOptions options;
      options.mPreventScroll = true;
      previouslyFocusedElement->Focus(options, CallerType::NonSystem,
                                      IgnoredErrorResult());
    }
  }

  // 13. Queue an element task on the user interaction task source given the
  // subject element to fire an event named close at subject.
  RefPtr<AsyncEventDispatcher> eventDispatcher =
      new AsyncEventDispatcher(this, u"close"_ns, CanBubble::eNo);
  eventDispatcher->PostDOMEvent();
}

// https://html.spec.whatwg.org/#dom-dialog-requestclose
// https://html.spec.whatwg.org/#dialog-request-close
void HTMLDialogElement::RequestClose(Element* aSource,
                                     const Maybe<nsAutoString>& aReturnValue) {
  RefPtr closeWatcher = mCloseWatcher;
  // 1. If subject does not have an open attribute, then return.
  if (!Open()) {
    return;
  }

  // 2. If subject is not connected or subject's node document is not fully
  // active, then return.
  if (!IsInComposedDoc() || !OwnerDoc()->IsFullyActive()) {
    return;
  }

  // 3. Assert: subject's close watcher is not null.
  if (StaticPrefs::dom_closewatcher_enabled()) {
    MOZ_ASSERT(closeWatcher, "RequestClose needs mCloseWatcher");
  }

  // 4. Set subject's enable close watcher for request close to true.
  if (StaticPrefs::dom_closewatcher_enabled()) {
    // XXX: Rather than store a "enable close watcher for request close" state,
    // we set the CloseWatcher Enabled state to true manually here, and revert
    // it lower down...
    closeWatcher->SetEnabled(true);
  }

  // 5. Set subject's request close return value to returnValue.
  if (aReturnValue.isSome()) {
    SetRequestCloseReturnValue(aReturnValue.ref());
  }

  // 6. Set subject's request close source element to source.
  mRequestCloseSourceElement = do_GetWeakReference(aSource);

  // 7. Request to close subject's close watcher with false.
  if (StaticPrefs::dom_closewatcher_enabled()) {
    closeWatcher->RequestToClose(false);
  } else {
    RunCancelDialogSteps();
  }

  // 8. Set subject's enable close watcher for request close to false.
  if (closeWatcher) {
    // XXX: Rather than store a "enable close watcher for request close" state,
    // we can simply set the close watcher enabled state to whatever it was
    // before we set it to true (above). SetCloseWatcherEnabledState() will do
    // this:
    SetCloseWatcherEnabledState();
  }
}

RefPtr<Element> HTMLDialogElement::GetRequestCloseSourceElement() {
  return do_QueryReferent(mRequestCloseSourceElement);
}

// https://html.spec.whatwg.org/#dom-dialog-show
void HTMLDialogElement::Show(ErrorResult& aError) {
  // 1. If this has an open attribute and is modal of this is false, then
  // return.
  if (Open()) {
    if (!IsInTopLayer()) {
      return;
    }

    // 2. If this has an open attribute, then throw an "InvalidStateError"
    // DOMException.
    return aError.ThrowInvalidStateError(
        "Cannot call show() on an open modal dialog.");
  }

  // 3. If the result of firing an event named beforetoggle, using ToggleEvent,
  // with the cancelable attribute initialized to true, the oldState attribute
  // initialized to "closed", and the newState attribute initialized to "open"
  // at this is false, then return.
  if (FireToggleEvent(u"closed"_ns, u"open"_ns, u"beforetoggle"_ns, nullptr)) {
    return;
  }

  // 4. If this has an open attribute, then return.
  if (Open()) {
    return;
  }

  // 5. Queue a dialog toggle event task given this, "closed", and "open".
  QueueToggleEventTask(nullptr);

  // 6. Add an open attribute to this, whose value is the empty string.
  SetOpen(true, IgnoreErrors());

  // 7. Set this's previously focused element to the focused element.
  StorePreviouslyFocusedElement();

  // 8. Let document be this's node document.

  // 9. Let hideUntil be the result of running topmost popover ancestor given
  // this, null, and false.
  RefPtr<Element> hideUntil = GetTopmostPopoverAncestor(nullptr, false);

  // 10. Run hide popovers until given document, hideUntil, false, and true.
  RefPtr<Document> doc = OwnerDoc();
  doc->HidePopoversUntil(hideUntil, false, true);

  // 11. Run the dialog focusing steps given this.
  FocusDialog();
}

bool HTMLDialogElement::Open() const {
  MOZ_ASSERT(GetBoolAttr(nsGkAtoms::open) ==
             State().HasState(ElementState::OPEN));
  return State().HasState(ElementState::OPEN);
}

bool HTMLDialogElement::IsInTopLayer() const {
  return State().HasState(ElementState::MODAL);
}

void HTMLDialogElement::AddToTopLayerIfNeeded() {
  MOZ_ASSERT(IsInComposedDoc(), "AddToTopLayerIfNeeded needs IsInComposedDoc");
  if (IsInTopLayer()) {
    return;
  }

  OwnerDoc()->AddModalDialog(*this);

  // A change to the modal state may cause the CloseWatcher enabled state to
  // change, if the `closedby` attribute is missing and therefore in the Auto
  // (computed) state.
  SetCloseWatcherEnabledState();
}

void HTMLDialogElement::RemoveFromTopLayerIfNeeded() {
  if (!IsInTopLayer()) {
    return;
  }
  OwnerDoc()->RemoveModalDialog(*this);

  // A change to the modal state may cause the CloseWatcher enabled state to
  // change, if the `closedby` attribute is missing and therefore in the Auto
  // (computed) state.
  SetCloseWatcherEnabledState();
}

void HTMLDialogElement::StorePreviouslyFocusedElement() {
  if (Element* element = nsFocusManager::GetFocusedElementStatic()) {
    if (NS_SUCCEEDED(nsContentUtils::CheckSameOrigin(this, element))) {
      mPreviouslyFocusedElement = do_GetWeakReference(element);
    }
  } else if (Document* doc = GetComposedDoc()) {
    // Looks like there's a discrepancy sometimes when focus is moved
    // to a different in-process window.
    if (nsIContent* unretargetedFocus = doc->GetUnretargetedFocusedContent()) {
      mPreviouslyFocusedElement = do_GetWeakReference(unretargetedFocus);
    }
  }
}

nsresult HTMLDialogElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  MOZ_TRY(nsGenericHTMLElement::BindToTree(aContext, aParent));

  // https://html.spec.whatwg.org/#the-dialog-element:html-element-insertion-steps
  // 1. If insertedNode's node document is not fully active, then return.
  // 2. If insertedNode is connected, then run the
  // dialog setup steps given insertedNode.
  if (Open() && IsInComposedDoc() && OwnerDoc()->IsFullyActive() &&
      !aContext.IsMove()) {
    SetupSteps();
  }

  return NS_OK;
}

// https://html.spec.whatwg.org/interactive-elements.html#the-dialog-element:html-element-removing-steps
void HTMLDialogElement::UnbindFromTree(UnbindContext& aContext) {
  if (!aContext.IsMove()) {
    // 1. If removedNode has an open attribute, then run the dialog cleanup
    // steps given removedNode.
    if (Open()) {
      CleanupSteps();
    }

    // 2. If removedNode's node document's top layer contains removedNode, then
    // remove an element from the top layer immediately given removedNode.
    RemoveFromTopLayerIfNeeded();

    // 3. Set is modal of removedNode to false.
  }

  nsGenericHTMLElement::UnbindFromTree(aContext);
}

// https://html.spec.whatwg.org/#show-a-modal-dialog
void HTMLDialogElement::ShowModal(Element* aSource, ErrorResult& aError) {
  // 1. If subject has an open attribute and is modal of subject is true, then
  // return.
  if (Open()) {
    if (IsInTopLayer()) {
      return;
    }

    // 2. If subject has an open attribute, then throw an "InvalidStateError"
    // DOMException.
    return aError.ThrowInvalidStateError(
        "Cannot call showModal() on an open non-modal dialog.");
  }

  // 3. If subject's node document is not fully active, then throw an
  // "InvalidStateError" DOMException.
  if (!OwnerDoc()->IsFullyActive()) {
    return aError.ThrowInvalidStateError(
        "The owner document is not fully active");
  }

  // 4. If subject is not connected, then throw an "InvalidStateError"
  // DOMException.
  if (!IsInComposedDoc()) {
    return aError.ThrowInvalidStateError("Dialog element is not connected");
  }

  // 5. If subject is in the popover showing state, then throw an
  // "InvalidStateError" DOMException.
  if (IsPopoverOpen()) {
    return aError.ThrowInvalidStateError(
        "Dialog element is already an open popover.");
  }

  // 6. If the result of firing an event named beforetoggle, using
  // ToggleEvent, with the cancelable attribute initialized to true, the
  // oldState attribute initialized to "closed", and the newState attribute
  // initialized to "open" at subject is false, then return.
  if (FireToggleEvent(u"closed"_ns, u"open"_ns, u"beforetoggle"_ns, aSource)) {
    return;
  }

  // 7. If subject has an open attribute, then return.
  // 8. If subject is not connected, then return.
  // 9. If subject is in the popover showing state, then return.
  if (Open() || !IsInComposedDoc() || IsPopoverOpen()) {
    return;
  }

  // 10. Queue a dialog toggle event task given subject, "closed", and "open".
  QueueToggleEventTask(aSource);

  // 11. Add an open attribute to subject, whose value is the empty string.
  SetOpen(true, aError);

  // 12. Assert: subject's close watcher is not null.
  if (StaticPrefs::dom_closewatcher_enabled()) {
    MOZ_ASSERT(mCloseWatcher, "ShowModal needs mCloseWatcher");
  }

  // 13. Set is modal of subject to true.
  // 14. Set subject's node document to be blocked by the modal dialog subject.
  // 15. If subject's node document's top layer does not already contain
  // subject, then add an element to the top layer given subject.
  AddToTopLayerIfNeeded();

  // 16. Set subject's previously focused element to the focused element.
  StorePreviouslyFocusedElement();

  // 17. Let document be subject's node document.

  // 18. Let hideUntil be the result of running topmost popover ancestor given
  // subject, null, and false.
  RefPtr<Element> hideUntil = GetTopmostPopoverAncestor(nullptr, false);

  // 19. Run hide popovers until given document, hideUntil, false, and true.
  RefPtr<Document> doc = OwnerDoc();
  doc->HidePopoversUntil(hideUntil, false, true);

  // 20. Run the dialog focusing steps given subject.
  FocusDialog();

  aError.SuppressException();
}

// https://html.spec.whatwg.org/#the-dialog-element:concept-element-attributes-change-ext
void HTMLDialogElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     bool aNotify) {
  nsGenericHTMLElement::AfterSetAttr(aNameSpaceID, aName, aValue, aOldValue,
                                     aMaybeScriptedPrincipal, aNotify);
  // 1. If namespace is not null, then return.
  if (aNameSpaceID != kNameSpaceID_None) {
    return;
  }

  // https://html.spec.whatwg.org/#set-the-dialog-close-watcher
  // https://github.com/whatwg/html/issues/11267
  // XXX: CloseWatcher currently uses a `getEnabledState` algorithm to set a
  // boolean, but this is quite a lot of additional infrastructure which could
  // be simplified by CloseWatcher having an "Enabled" state.
  // If the closedby attribute changes, it may or may not toggle the
  // CloseWatcher enabled state.
  if (aName == nsGkAtoms::closedby) {
    SetCloseWatcherEnabledState();
  }

  // 2. If localName is not open, then return.
  if (aName != nsGkAtoms::open) {
    return;
  }

  bool wasOpen = !!aOldValue;
  bool isOpen = !!aValue;

  MOZ_ASSERT(GetBoolAttr(nsGkAtoms::open) == isOpen);
  SetStates(ElementState::OPEN, isOpen);

  // 3. If value is null and oldValue is not null, then run the dialog cleanup
  // steps given element.
  if (!isOpen && wasOpen) {
    CleanupSteps();
  }

  // 4. If element's node document is not fully active, then return.
  if (!OwnerDoc()->IsFullyActive()) {
    return;
  }

  // 5. If element is not connected, then return.
  if (!IsInComposedDoc()) {
    return;
  }

  // 6. If value is not null and oldValue is null, then run the dialog setup
  // steps given element.
  if (isOpen && !wasOpen) {
    SetupSteps();
  }
}

void HTMLDialogElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  if (mToggleEventDispatcher == aEvent) {
    mToggleEventDispatcher = nullptr;
  }
}

void HTMLDialogElement::FocusDialog() {
  // 1) If subject is inert, return.
  // 2) Let control be the first descendant element of subject, in tree
  // order, that is not inert and has the autofocus attribute specified.
  RefPtr<Document> doc = OwnerDoc();
  if (IsInComposedDoc()) {
    doc->FlushPendingNotifications(FlushType::Frames);
  }

  RefPtr<Element> control = HasAttr(nsGkAtoms::autofocus)
                                ? this
                                : GetFocusDelegate(IsFocusableFlags(0));

  // If there isn't one of those either, then let control be subject.
  if (!control) {
    control = this;
  }

  FocusCandidate(control, IsInTopLayer());
}

int32_t HTMLDialogElement::TabIndexDefault() { return 0; }

void HTMLDialogElement::RunCancelDialogSteps() {
  // 1) Let close be the result of firing an event named cancel at dialog,
  // with the cancelable attribute initialized to true.
  bool defaultAction = true;
  nsContentUtils::DispatchTrustedEvent(this, u"cancel"_ns, CanBubble::eNo,
                                       Cancelable::eYes, &defaultAction);

  // 2) If close is true and dialog has an open attribute, then close the
  // dialog with ~~no return value.~~
  // XXX(keithamus): RequestClose's steps expect the return value to be
  // RequestCloseReturnValue. RunCancelDialogSteps has been refactored out of
  // the spec, over CloseWatcher though, so one day this code will need to be
  // refactored when the CloseWatcher specifications settle.
  if (defaultAction) {
    Maybe<nsAutoString> retValue;
    GetRequestCloseReturnValue(retValue);
    RefPtr<Element> source = GetRequestCloseSourceElement();
    Close(source, retValue);
  }
}

bool HTMLDialogElement::IsValidCommandAction(Command aCommand) const {
  return nsGenericHTMLElement::IsValidCommandAction(aCommand) ||
         aCommand == Command::ShowModal || aCommand == Command::Close ||
         aCommand == Command::RequestClose;
}

bool HTMLDialogElement::HandleCommandInternal(Element* aSource,
                                              Command aCommand,
                                              ErrorResult& aRv) {
  if (nsGenericHTMLElement::HandleCommandInternal(aSource, aCommand, aRv)) {
    return true;
  }

  MOZ_ASSERT(IsValidCommandAction(aCommand));

  if ((aCommand == Command::Close || aCommand == Command::RequestClose) &&
      Open()) {
    Maybe<nsAutoString> retValue;
    if (aSource->HasAttr(nsGkAtoms::value)) {
      if (auto* button = HTMLButtonElement::FromNodeOrNull(aSource)) {
        retValue.emplace();
        button->GetValue(retValue.ref());
      }
    }
    if (aCommand == Command::Close) {
      Close(aSource, retValue);
    } else {
      MOZ_ASSERT(aCommand == Command::RequestClose);
      RequestClose(aSource, retValue);
    }
    return true;
  }

  if (IsInComposedDoc() && !Open() && aCommand == Command::ShowModal) {
    ShowModal(aSource, aRv);
    return true;
  }

  return false;
}

void HTMLDialogElement::QueueToggleEventTask(Element* aSource) {
  nsAutoString oldState;
  auto newState = Open() ? u"closed"_ns : u"open"_ns;
  if (mToggleEventDispatcher) {
    oldState.Truncate();
    static_cast<ToggleEvent*>(mToggleEventDispatcher->mEvent.get())
        ->GetOldState(oldState);
    mToggleEventDispatcher->Cancel();
  } else {
    oldState.Assign(Open() ? u"open"_ns : u"closed"_ns);
  }
  RefPtr<ToggleEvent> toggleEvent = CreateToggleEvent(
      u"toggle"_ns, oldState, newState, Cancelable::eNo, aSource);
  mToggleEventDispatcher = new AsyncEventDispatcher(this, toggleEvent.forget());
  mToggleEventDispatcher->PostDOMEvent();
}

// https://html.spec.whatwg.org/#set-the-dialog-close-watcher
void HTMLDialogElement::SetDialogCloseWatcherIfNeeded() {
  MOZ_ASSERT(StaticPrefs::dom_closewatcher_enabled(), "CloseWatcher enabled");
  // 1. Assert: dialog's close watcher is null.
  MOZ_ASSERT(!mCloseWatcher);

  // 2. Assert: dialog has an open attribute and dialog's node document is
  // fully active.
  RefPtr<Document> doc = OwnerDoc();
  RefPtr window = doc->GetInnerWindow();
  MOZ_ASSERT(Open() && window && window->IsFullyActive());

  // 3. Set dialog's close watcher to the result of establishing a close
  // watcher given dialog's relevant global object, with:
  mCloseWatcher = new CloseWatcher(window);
  RefPtr<DialogCloseWatcherListener> eventListener =
      new DialogCloseWatcherListener(this);

  // - cancelAction given canPreventClose being to return the result of firing
  // an event named cancel at dialog, with the cancelable attribute
  // initialized to canPreventClose.
  mCloseWatcher->AddSystemEventListener(u"cancel"_ns, eventListener,
                                        false /* aUseCapture */,
                                        false /* aWantsUntrusted */);

  // - closeAction being to close the dialog given dialog and dialog's request
  // close return value.
  mCloseWatcher->AddSystemEventListener(u"close"_ns, eventListener,
                                        false /* aUseCapture */,
                                        false /* aWantsUntrusted */);

  // - getEnabledState being to return true if dialog's enable close watcher
  // for requestClose() is true or dialog's computed closed-by state is not
  // None; otherwise false.
  //
  // XXX: Rather than creating a function pointer to manage the state of two
  // boolean conditions, we set the enabled state of the close watcher
  // explicitly whenever the state of those two conditions change. The first
  // condition "enable close watcher for requestclose" is managed in
  // RequestClose(), the other condition is managed by this function:
  SetCloseWatcherEnabledState();

  mCloseWatcher->AddToWindowsCloseWatcherManager();
}

// https://html.spec.whatwg.org/multipage#dialog-setup-steps
void HTMLDialogElement::SetupSteps() {
  // 1. Assert: subject has an open attribute.
  MOZ_ASSERT(Open());

  // 2. Assert: subject is connected.
  MOZ_ASSERT(IsInComposedDoc(), "Dialog SetupSteps needs IsInComposedDoc");

  // 3. Assert: subject's node document's open dialogs list does not contain
  // subject.
  MOZ_ASSERT(!OwnerDoc()->DialogIsInOpenDialogsList(*this));

  // 4. Add subject to subject's node document's open dialogs list.
  OwnerDoc()->AddOpenDialog(*this);

  // 5. Set the dialog close watcher with subject.
  if (StaticPrefs::dom_closewatcher_enabled()) {
    SetDialogCloseWatcherIfNeeded();
  }
}

void HTMLDialogElement::SetCloseWatcherEnabledState() {
  if (StaticPrefs::dom_closewatcher_enabled() && mCloseWatcher) {
    mCloseWatcher->SetEnabled(GetClosedBy() != ClosedBy::None);
  }
}

// https://html.spec.whatwg.org/#dialog-cleanup-steps
void HTMLDialogElement::CleanupSteps() {
  // 1. Remove subject from subject's node document's open dialogs list.
  OwnerDoc()->RemoveOpenDialog(*this);

  // 2. If subject's close watcher is not null, and subject does not have an
  // open attribute, then:
  if (mCloseWatcher) {
    // 3. Destroy subject's close watcher.
    mCloseWatcher->Destroy();

    // 4. Set subject's close watcher to null.
    mCloseWatcher = nullptr;
  }
}

JSObject* HTMLDialogElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return HTMLDialogElement_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
