/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLFormElement.h"

#include <utility>

#include "Attr.h"
#include "jsapi.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/Components.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/PresShell.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/CustomEvent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLFormControlsCollection.h"
#include "mozilla/dom/HTMLFormElementBinding.h"
#include "mozilla/dom/TreeOrderedArrayInlines.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "nsCOMArray.h"
#include "nsContentUtils.h"
#include "nsDOMAttributeMap.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsHTMLDocument.h"
#include "nsInterfaceHashtable.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsStyleConsts.h"
#include "nsTArray.h"

// form submission
#include "HTMLFormSubmissionConstants.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_prompts.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/FormDataEvent.h"
#include "mozilla/dom/SubmitEvent.h"
#include "mozilla/glean/DomSecurityMetrics.h"
#include "mozilla/intl/Localization.h"
#include "nsCategoryManagerUtils.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPromptService.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsISecurityUITelemetry.h"
#include "nsISimpleEnumerator.h"
#include "nsNetUtil.h"
#include "nsRange.h"

// radio buttons
#include "RadioNodeList.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLButtonElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "nsIConstraintValidation.h"
#include "nsLayoutUtils.h"
#include "nsSandboxFlags.h"

// images
#include "mozilla/dom/HTMLButtonElement.h"
#include "mozilla/dom/HTMLImageElement.h"

// construction, destruction
NS_IMPL_NS_NEW_HTML_ELEMENT(Form)

namespace mozilla::dom {

static const uint8_t NS_FORM_AUTOCOMPLETE_ON = 1;
static const uint8_t NS_FORM_AUTOCOMPLETE_OFF = 0;

static constexpr nsAttrValue::EnumTableEntry kFormAutocompleteTable[] = {
    {"on", NS_FORM_AUTOCOMPLETE_ON},
    {"off", NS_FORM_AUTOCOMPLETE_OFF},
};
// Default autocomplete value is 'on'.
static constexpr const nsAttrValue::EnumTableEntry* kFormDefaultAutocomplete =
    &kFormAutocompleteTable[0];

HTMLFormElement::HTMLFormElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)),
      mControls(new HTMLFormControlsCollection(this)),
      mPendingSubmission(nullptr),
      mDefaultSubmitElement(nullptr),
      mFirstSubmitInElements(nullptr),
      mFirstSubmitNotInElements(nullptr),
      mImageNameLookupTable(FORM_CONTROL_LIST_HASHTABLE_LENGTH),
      mPastNameLookupTable(FORM_CONTROL_LIST_HASHTABLE_LENGTH),
      mSubmitPopupState(PopupBlocker::openAbused),
      mInvalidElementsCount(0),
      mFormNumber(-1),
      mGeneratingSubmit(false),
      mGeneratingReset(false),
      mDeferSubmission(false),
      mNotifiedObservers(false),
      mNotifiedObserversResult(false),
      mIsConstructingEntryList(false),
      mIsFiringSubmissionEvents(false) {
  // We start out valid.
  AddStatesSilently(ElementState::VALID);
}

HTMLFormElement::~HTMLFormElement() {
  if (mControls) {
    mControls->DropFormReference();
  }

  Clear();
}

// nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLFormElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLFormElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mControls)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mImageNameLookupTable)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPastNameLookupTable)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRelList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTargetContext)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLFormElement,
                                                nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRelList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTargetContext)
  tmp->Clear();
  tmp->mExpandoAndGeneration.OwnerUnlinked();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLFormElement,
                                               nsGenericHTMLElement)

// EventTarget
void HTMLFormElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  if (aEvent->mEventType == u"DOMFormHasPassword"_ns) {
    mHasPendingPasswordEvent = false;
  } else if (aEvent->mEventType == u"DOMPossibleUsernameInputAdded"_ns) {
    mHasPendingPossibleUsernameEvent = false;
  }
}

nsDOMTokenList* HTMLFormElement::RelList() {
  if (!mRelList) {
    mRelList =
        new nsDOMTokenList(this, nsGkAtoms::rel, sAnchorAndFormRelValues);
  }
  return mRelList;
}

NS_IMPL_ELEMENT_CLONE(HTMLFormElement)

HTMLFormControlsCollection* HTMLFormElement::Elements() { return mControls; }

void HTMLFormElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                    const nsAttrValue* aValue, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::action || aName == nsGkAtoms::target) {
      // Don't forget we've notified the password manager already if the
      // page sets the action/target in the during submit. (bug 343182)
      bool notifiedObservers = mNotifiedObservers;
      ForgetCurrentSubmission();
      mNotifiedObservers = notifiedObservers;
    }
  }

  return nsGenericHTMLElement::BeforeSetAttr(aNamespaceID, aName, aValue,
                                             aNotify);
}

void HTMLFormElement::GetAutocomplete(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::autocomplete, kFormDefaultAutocomplete->tag, aValue);
}

void HTMLFormElement::GetEnctype(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::enctype, kFormDefaultEnctype->tag, aValue);
}

void HTMLFormElement::GetMethod(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::method, kFormDefaultMethod->tag, aValue);
}

void HTMLFormElement::ReportInvalidUnfocusableElements(
    const nsTArray<RefPtr<Element>>&& aInvalidElements) {
  RefPtr<nsFocusManager> focusManager = nsFocusManager::GetFocusManager();
  MOZ_ASSERT(focusManager);

  for (const auto& element : aInvalidElements) {
    bool isFocusable = false;
    // MOZ_KnownLive because 'aInvalidElements' is guaranteed to keep it alive.
    // This can go away once bug 1620312 is fixed.
    focusManager->ElementIsFocusable(MOZ_KnownLive(element), 0, &isFocusable);
    if (!isFocusable) {
      nsTArray<nsString> params;
      nsAutoCString messageName("InvalidFormControlUnfocusable");

      if (Attr* nameAttr = element->GetAttributes()->GetNamedItem(u"name"_ns)) {
        nsAutoString name;
        nameAttr->GetValue(name);
        params.AppendElement(name);
        messageName = "InvalidNamedFormControlUnfocusable";
      }

      nsContentUtils::ReportToConsole(
          nsIScriptError::errorFlag, "DOM"_ns, element->GetOwnerDocument(),
          PropertiesFile::DOM_PROPERTIES, messageName.get(), params,
          SourceLocation(element->GetBaseURI()));
    }
  }
}

// https://html.spec.whatwg.org/multipage/forms.html#concept-form-submit
void HTMLFormElement::MaybeSubmit(Element* aSubmitter) {
#ifdef DEBUG
  if (aSubmitter) {
    const auto* fc = nsIFormControl::FromNode(aSubmitter);
    MOZ_ASSERT(fc);
    MOZ_ASSERT(fc->IsSubmitControl(), "aSubmitter is not a submit control?");
  }
#endif

  // 1-4 of
  // https://html.spec.whatwg.org/multipage/forms.html#concept-form-submit
  RefPtr<Document> doc = GetComposedDoc();
  if (mIsConstructingEntryList || !doc ||
      (doc->GetSandboxFlags() & SANDBOXED_FORMS)) {
    return;
  }

  // 5.1. If form's firing submission events is true, then return.
  if (mIsFiringSubmissionEvents) {
    return;
  }

  // 5.2. Set form's firing submission events to true.
  AutoRestore<bool> resetFiringSubmissionEventsFlag(mIsFiringSubmissionEvents);
  mIsFiringSubmissionEvents = true;

  // Flag elements as user-interacted.
  // FIXME: Should be specified, see:
  // https://github.com/whatwg/html/issues/10066
  {
    for (nsGenericHTMLFormElement* el : mControls->mElements.AsSpan()) {
      el->SetUserInteracted(true);
    }
    for (nsGenericHTMLFormElement* el : mControls->mNotInElements.AsSpan()) {
      el->SetUserInteracted(true);
    }
  }

  // 5.3. If the submitter element's no-validate state is false, then
  //      interactively validate the constraints of form and examine the result.
  //      If the result is negative (i.e., the constraint validation concluded
  //      that there were invalid fields and probably informed the user of this)
  bool noValidateState =
      HasAttr(nsGkAtoms::novalidate) ||
      (aSubmitter && aSubmitter->HasAttr(nsGkAtoms::formnovalidate));
  if (!noValidateState && !CheckValidFormSubmission()) {
    return;
  }

  // Prepare to run DispatchBeforeSubmitChromeOnlyEvent early before the
  // scripts on the page get to modify the form data, possibly
  // throwing off any password manager. (bug 257781)
  bool cancelSubmit = false;
  nsresult rv = DispatchBeforeSubmitChromeOnlyEvent(&cancelSubmit);
  if (NS_SUCCEEDED(rv)) {
    mNotifiedObservers = true;
    mNotifiedObserversResult = cancelSubmit;
  }

  RefPtr<PresShell> presShell = doc->GetPresShell();
  if (!presShell) {
    // We need the nsPresContext for dispatching the submit event. In some
    // rare cases we need to flush notifications to force creation of the
    // nsPresContext here (for example when a script calls form.requestSubmit()
    // from script early during page load). We only flush the notifications
    // if the PresShell hasn't been created yet, to limit the performance
    // impact.
    doc->FlushPendingNotifications(FlushType::EnsurePresShellInitAndFrames);
    presShell = doc->GetPresShell();
  }

  if (!doc->IsCurrentActiveDocument()) {
    // Bug 125624
    return;
  }

  SubmitEventInit init;
  init.mBubbles = true;
  init.mCancelable = true;
  init.mSubmitter =
      aSubmitter ? nsGenericHTMLElement::FromNode(aSubmitter) : nullptr;
  RefPtr<SubmitEvent> event =
      SubmitEvent::Constructor(this, u"submit"_ns, init);
  event->SetTrusted(true);
  nsEventStatus status = nsEventStatus_eIgnore;
  EventDispatcher::DispatchDOMEvent(this, nullptr, event, nullptr, &status);
}

void HTMLFormElement::MaybeReset(Element* aSubmitter) {
  if (!OwnerDoc()->IsCurrentActiveDocument()) {
    // Bug 125624
    return;
  }

  InternalFormEvent event(true, eFormReset);
  event.mOriginator = aSubmitter;
  nsEventStatus status = nsEventStatus_eIgnore;
  EventDispatcher::DispatchDOMEvent(this, &event, nullptr, nullptr, &status);
}

void HTMLFormElement::Submit(ErrorResult& aRv) { aRv = DoSubmit(); }

// https://html.spec.whatwg.org/multipage/forms.html#dom-form-requestsubmit
void HTMLFormElement::RequestSubmit(nsGenericHTMLElement* aSubmitter,
                                    ErrorResult& aRv) {
  // 1. If submitter is not null, then:
  if (aSubmitter) {
    const auto* fc = nsIFormControl::FromNodeOrNull(aSubmitter);

    // 1.1. If submitter is not a submit button, then throw a TypeError.
    if (!fc || !fc->IsSubmitControl()) {
      aRv.ThrowTypeError("The submitter is not a submit button.");
      return;
    }

    // 1.2. If submitter's form owner is not this form element, then throw a
    //      "NotFoundError" DOMException.
    if (fc->GetFormInternal() != this) {
      aRv.ThrowNotFoundError("The submitter is not owned by this form.");
      return;
    }
  }

  // 2. Otherwise, set submitter to this form element.
  // 3. Submit this form element, from submitter.
  MaybeSubmit(aSubmitter);
}

void HTMLFormElement::Reset() {
  InternalFormEvent event(true, eFormReset);
  EventDispatcher::Dispatch(this, nullptr, &event);
}

bool HTMLFormElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                     const nsAString& aValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::method) {
      return aResult.ParseEnumValue(aValue, kFormMethodTable, false);
    }
    if (aAttribute == nsGkAtoms::enctype) {
      return aResult.ParseEnumValue(aValue, kFormEnctypeTable, false);
    }
    if (aAttribute == nsGkAtoms::autocomplete) {
      return aResult.ParseEnumValue(aValue, kFormAutocompleteTable, false);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

nsresult HTMLFormElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (IsInUncomposedDoc() && aContext.OwnerDoc().IsHTMLOrXHTML()) {
    aContext.OwnerDoc().AsHTMLDocument()->AddedForm();
  }

  return rv;
}

template <typename T>
static void MarkOrphans(Span<T*> aArray) {
  for (auto* element : aArray) {
    element->SetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
  }
}

static void CollectOrphans(
    nsINode* aRemovalRoot,
    TreeOrderedArray<nsGenericHTMLFormElement*, TreeKind::ShadowIncludingDOM>&
        aArray
#ifdef DEBUG
    ,
    HTMLFormElement* aThisForm
#endif
) {
  // Put a script blocker around all the notifications we're about to do.
  nsAutoScriptBlocker scriptBlocker;

  // Walk backwards so that if we remove elements we can just keep iterating
  uint32_t length = aArray.Length();
  for (uint32_t i = length; i > 0; --i) {
    nsGenericHTMLFormElement* node = aArray[i - 1];

    // Now if MAYBE_ORPHAN_FORM_ELEMENT is not set, that would mean that the
    // node is in fact a descendant of the form and hence should stay in the
    // form.  If it _is_ set, then we need to check whether the node is a
    // descendant of aRemovalRoot.  If it is, we leave it in the form.
#ifdef DEBUG
    bool removed = false;
#endif
    if (node->HasFlag(MAYBE_ORPHAN_FORM_ELEMENT)) {
      node->UnsetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
      if (!node->IsInclusiveDescendantOf(aRemovalRoot)) {
        nsCOMPtr<nsIFormControl> fc = nsIFormControl::FromNode(node);
        MOZ_ASSERT(fc);
        fc->ClearForm(true, false);
#ifdef DEBUG
        removed = true;
#endif
      }
    }

#ifdef DEBUG
    if (!removed) {
      const auto* fc = nsIFormControl::FromNode(node);
      MOZ_ASSERT(fc);
      HTMLFormElement* form = fc->GetFormInternal();
      // form may be null if this is called during CC after node is unlinked.
      NS_ASSERTION(!form || form == aThisForm, "How did that happen?");
    }
#endif /* DEBUG */
  }
}

static void CollectOrphans(nsINode* aRemovalRoot,
                           const TreeOrderedArray<HTMLImageElement*>& aArray
#ifdef DEBUG
                           ,
                           HTMLFormElement* aThisForm
#endif
) {
  // Walk backwards so that if we remove elements we can just keep iterating
  uint32_t length = aArray.Length();
  for (uint32_t i = length; i > 0; --i) {
    HTMLImageElement* node = aArray[i - 1];

    // Now if MAYBE_ORPHAN_FORM_ELEMENT is not set, that would mean that the
    // node is in fact a descendant of the form and hence should stay in the
    // form.  If it _is_ set, then we need to check whether the node is a
    // descendant of aRemovalRoot.  If it is, we leave it in the form.
#ifdef DEBUG
    bool removed = false;
#endif
    if (node->HasFlag(MAYBE_ORPHAN_FORM_ELEMENT)) {
      node->UnsetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
      if (!node->IsInclusiveDescendantOf(aRemovalRoot)) {
        node->ClearForm(true);

#ifdef DEBUG
        removed = true;
#endif
      }
    }

#ifdef DEBUG
    if (!removed) {
      HTMLFormElement* form = node->GetFormInternal();
      // form may be null if this is called during CC after node is unlinked.
      NS_ASSERTION(!form || form == aThisForm, "How did that happen?");
    }
#endif /* DEBUG */
  }
}

void HTMLFormElement::UnbindFromTree(UnbindContext& aContext) {
  MaybeFireFormRemoved();

  // Note, this is explicitly using uncomposed doc, since we count
  // only forms in document.
  RefPtr<Document> oldDocument = GetUncomposedDoc();

  // Mark all of our controls as maybe being orphans
  MarkOrphans(mControls->mElements.AsSpan());
  MarkOrphans(mControls->mNotInElements.AsSpan());
  MarkOrphans(mImageElements.AsSpan());

  nsGenericHTMLElement::UnbindFromTree(aContext);

  nsINode* ancestor = this;
  nsINode* cur;
  do {
    cur = ancestor->GetParentNode();
    if (!cur) {
      break;
    }
    ancestor = cur;
  } while (true);

  CollectOrphans(ancestor, mControls->mElements
#ifdef DEBUG
                 ,
                 this
#endif
  );
  CollectOrphans(ancestor, mControls->mNotInElements
#ifdef DEBUG
                 ,
                 this
#endif
  );
  CollectOrphans(ancestor, mImageElements
#ifdef DEBUG
                 ,
                 this
#endif
  );

  if (oldDocument && oldDocument->IsHTMLOrXHTML()) {
    oldDocument->AsHTMLDocument()->RemovedForm();
  }
  ForgetCurrentSubmission();
}

static bool CanSubmit(WidgetEvent& aEvent) {
  // According to the UI events spec section "Trusted events", we shouldn't
  // trigger UA default action with an untrusted event except click.
  // However, there are still some sites depending on sending untrusted event
  // to submit form, see Bug 1370630.
  return !StaticPrefs::dom_forms_submit_trusted_event_only() ||
         aEvent.IsTrusted();
}

void HTMLFormElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mWantsWillHandleEvent = true;
  if (aVisitor.mEvent->mOriginalTarget == static_cast<nsIContent*>(this) &&
      CanSubmit(*aVisitor.mEvent)) {
    uint32_t msg = aVisitor.mEvent->mMessage;
    if (msg == eFormSubmit) {
      if (mGeneratingSubmit) {
        aVisitor.mCanHandle = false;
        return;
      }
      mGeneratingSubmit = true;

      // XXXedgar, the untrusted event would trigger form submission, in this
      // case, form need to handle defer flag and flushing pending submission by
      // itself. This could be removed after Bug 1370630.
      if (!aVisitor.mEvent->IsTrusted()) {
        // let the form know that it needs to defer the submission,
        // that means that if there are scripted submissions, the
        // latest one will be deferred until after the exit point of the
        // handler.
        mDeferSubmission = true;
      }
    } else if (msg == eFormReset) {
      if (mGeneratingReset) {
        aVisitor.mCanHandle = false;
        return;
      }
      mGeneratingReset = true;
    }
  }
  nsGenericHTMLElement::GetEventTargetParent(aVisitor);
}

void HTMLFormElement::WillHandleEvent(EventChainPostVisitor& aVisitor) {
  // If this is the bubble stage and there is a nested form below us which
  // received a submit event we do *not* want to handle the submit event
  // for this form too.
  if ((aVisitor.mEvent->mMessage == eFormSubmit ||
       aVisitor.mEvent->mMessage == eFormReset) &&
      aVisitor.mEvent->mFlags.mInBubblingPhase &&
      aVisitor.mEvent->mOriginalTarget != static_cast<nsIContent*>(this)) {
    aVisitor.mEvent->StopPropagation();
  }
}

nsresult HTMLFormElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  if (aVisitor.mEvent->mOriginalTarget == static_cast<nsIContent*>(this) &&
      CanSubmit(*aVisitor.mEvent)) {
    EventMessage msg = aVisitor.mEvent->mMessage;
    if (aVisitor.mEventStatus == nsEventStatus_eIgnore) {
      switch (msg) {
        case eFormReset: {
          DoReset();
          break;
        }
        case eFormSubmit: {
          if (!aVisitor.mEvent->IsTrusted()) {
            // Warning about the form submission is from untrusted event.
            OwnerDoc()->WarnOnceAbout(
                DeprecatedOperations::eFormSubmissionUntrustedEvent);
          }
          RefPtr<Event> event = aVisitor.mDOMEvent;
          DoSubmit(event);
          break;
        }
        default:
          break;
      }
    }

    // XXXedgar, the untrusted event would trigger form submission, in this
    // case, form need to handle defer flag and flushing pending submission by
    // itself. This could be removed after Bug 1370630.
    if (msg == eFormSubmit && !aVisitor.mEvent->IsTrusted()) {
      // let the form know not to defer subsequent submissions
      mDeferSubmission = false;
      // tell the form to flush a possible pending submission.
      FlushPendingSubmission();
    }

    if (msg == eFormSubmit) {
      mGeneratingSubmit = false;
    } else if (msg == eFormReset) {
      mGeneratingReset = false;
    }
  }
  return NS_OK;
}

nsresult HTMLFormElement::DoReset() {
  // Make sure the presentation is up-to-date
  if (Document* doc = GetComposedDoc()) {
    doc->FlushPendingNotifications(FlushType::ContentAndNotify);
  }

  // JBK walk the elements[] array instead of form frame controls - bug 34297
  uint32_t numElements = mControls->Length();
  for (uint32_t elementX = 0; elementX < numElements; ++elementX) {
    // Hold strong ref in case the reset does something weird
    if (elementX >= mControls->mElements.Length()) {
      continue;
    }
    nsCOMPtr<nsIFormControl> controlNode =
        nsIFormControl::FromNode(mControls->mElements[elementX]);
    if (controlNode) {
      controlNode->Reset();
    }
  }

  return NS_OK;
}

#define NS_ENSURE_SUBMIT_SUCCESS(rv) \
  if (NS_FAILED(rv)) {               \
    ForgetCurrentSubmission();       \
    return rv;                       \
  }

nsresult HTMLFormElement::DoSubmit(Event* aEvent) {
  Document* doc = GetComposedDoc();
  NS_ASSERTION(doc, "Should never get here without a current doc");

  // Make sure the presentation is up-to-date
  if (doc) {
    doc->FlushPendingNotifications(FlushType::ContentAndNotify);
  }

  // Don't submit if we're not in a document or if we're in
  // a sandboxed frame and form submit is disabled.
  if (mIsConstructingEntryList || !doc ||
      (doc->GetSandboxFlags() & SANDBOXED_FORMS)) {
    return NS_OK;
  }

  if (IsSubmitting()) {
    NS_WARNING("Preventing double form submission");
    // XXX Should this return an error?
    return NS_OK;
  }

  mTargetContext = nullptr;
  mCurrentLoadId = Nothing();

  UniquePtr<HTMLFormSubmission> submission;

  //
  // prepare the submission object
  //
  nsresult rv = BuildSubmission(getter_Transfers(submission), aEvent);

  // Don't raise an error if form cannot navigate.
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }

  NS_ENSURE_SUCCESS(rv, rv);

  // XXXbz if the script global is that for an sXBL/XBL2 doc, it won't
  // be a window...
  nsPIDOMWindowOuter* window = OwnerDoc()->GetWindow();
  if (window) {
    mSubmitPopupState = PopupBlocker::GetPopupControlState();
  } else {
    mSubmitPopupState = PopupBlocker::openAbused;
  }

  //
  // perform the submission
  //
  if (!submission) {
#ifdef DEBUG
    HTMLDialogElement* dialog = nullptr;
    for (nsIContent* parent = GetParent(); parent;
         parent = parent->GetParent()) {
      dialog = HTMLDialogElement::FromNodeOrNull(parent);
      if (dialog) {
        break;
      }
    }
    MOZ_ASSERT(!dialog || !dialog->Open());
#endif
    return NS_OK;
  }

  if (DialogFormSubmission* dialogSubmission =
          submission->GetAsDialogSubmission()) {
    return SubmitDialog(dialogSubmission);
  }

  if (mDeferSubmission) {
    // we are in an event handler, JS submitted so we have to
    // defer this submission. let's remember it and return
    // without submitting
    mPendingSubmission = std::move(submission);
    return NS_OK;
  }

  return SubmitSubmission(submission.get());
}

nsresult HTMLFormElement::BuildSubmission(HTMLFormSubmission** aFormSubmission,
                                          Event* aEvent) {
  // Get the submitter element
  nsGenericHTMLElement* submitter = nullptr;
  if (aEvent) {
    SubmitEvent* submitEvent = aEvent->AsSubmitEvent();
    if (submitEvent) {
      submitter = submitEvent->GetSubmitter();
    }
  }

  nsresult rv;

  //
  // Walk over the form elements and call SubmitNamesValues() on them to get
  // their data.
  //
  auto encoding = GetSubmitEncoding()->OutputEncoding();
  RefPtr<FormData> formData =
      new FormData(GetRelevantGlobal(), encoding, submitter);
  rv = ConstructEntryList(formData);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  // Step 9. If form cannot navigate, then return.
  // https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm
  if (!GetComposedDoc()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  //
  // Get the submission object
  //
  rv = HTMLFormSubmission::GetFromForm(this, submitter, encoding, formData,
                                       aFormSubmission);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  //
  // Dump the data into the submission object
  //
  if (!(*aFormSubmission)->GetAsDialogSubmission()) {
    rv = formData->CopySubmissionDataTo(*aFormSubmission);
    NS_ENSURE_SUBMIT_SUCCESS(rv);
  }

  return NS_OK;
}

nsresult HTMLFormElement::SubmitSubmission(
    HTMLFormSubmission* aFormSubmission) {
  MOZ_ASSERT(!mDeferSubmission);
  MOZ_ASSERT(!mPendingSubmission);

  nsCOMPtr<nsIURI> actionURI = aFormSubmission->GetActionURL();
  if (!actionURI) {
    return NS_OK;
  }

  // If there is no link handler, then we won't actually be able to submit.
  RefPtr<Document> doc = GetComposedDoc();
  RefPtr<nsDocShell> container =
      doc ? nsDocShell::Cast(doc->GetDocShell()) : nullptr;
  if (!container || IsEditable()) {
    return NS_OK;
  }

  if (doc->GetSandboxFlags() & SANDBOXED_FORMS) {
    return NS_OK;
  }

  // javascript URIs are not really submissions; they just call a function.
  // Also, they may synchronously call submit(), and we want them to be able to
  // do so while still disallowing other double submissions. (Bug 139798)
  // Note that any other URI types that are of equivalent type should also be
  // added here.
  // XXXbz this is a mess.  The real issue here is that nsJSChannel sets the
  // LOAD_BACKGROUND flag, so doesn't notify us, compounded by the fact that
  // the JS executes before we forget the submission in OnStateChange on
  // STATE_STOP.  As a result, we have to make sure that we simply pretend
  // we're not submitting when submitting to a JS URL.  That's kinda bogus, but
  // there we are.
  bool schemeIsJavaScript = actionURI->SchemeIs("javascript");

  //
  // Notify observers of submit
  //
  nsresult rv;
  bool cancelSubmit = false;
  if (mNotifiedObservers) {
    cancelSubmit = mNotifiedObserversResult;
  } else {
    rv = DispatchBeforeSubmitChromeOnlyEvent(&cancelSubmit);
    NS_ENSURE_SUBMIT_SUCCESS(rv);
  }

  if (cancelSubmit) {
    return NS_OK;
  }

  cancelSubmit = false;
  rv = DoSecureToInsecureSubmitCheck(actionURI, &cancelSubmit);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  if (cancelSubmit) {
    return NS_OK;
  }

  //
  // Submit
  //
  uint64_t currentLoadId = 0;

  {
    AutoPopupStatePusher popupStatePusher(mSubmitPopupState);

    AutoHandlingUserInputStatePusher userInpStatePusher(
        aFormSubmission->IsInitiatedFromUserInput());

    nsCOMPtr<nsIInputStream> postDataStream;
    rv = aFormSubmission->GetEncodedSubmission(
        actionURI, getter_AddRefs(postDataStream), actionURI);
    NS_ENSURE_SUBMIT_SUCCESS(rv);

    nsAutoString target;
    aFormSubmission->GetTarget(target);

    RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(actionURI);
    loadState->SetTarget(target);
    loadState->SetPostDataStream(postDataStream);
    loadState->SetFirstParty(true);
    loadState->SetIsFormSubmission(true);
    loadState->SetTriggeringPrincipal(NodePrincipal());
    loadState->SetPrincipalToInherit(NodePrincipal());
    loadState->SetPolicyContainer(GetPolicyContainer());
    loadState->SetAllowFocusMove(UserActivation::IsHandlingUserInput());

    const bool hasValidUserGestureActivation =
        doc->HasValidTransientUserGestureActivation();
    loadState->SetHasValidUserGestureActivation(hasValidUserGestureActivation);
    loadState->SetTextDirectiveUserActivation(
        doc->ConsumeTextDirectiveUserActivation() ||
        hasValidUserGestureActivation);
    loadState->SetFormDataEntryList(aFormSubmission->GetFormData());
    if (aFormSubmission->IsInitiatedFromUserInput()) {
      loadState->SetUserNavigationInvolvement(
          UserNavigationInvolvement::Activation);
    }
    if (Element* element = aFormSubmission->GetSubmitterElement()) {
      loadState->SetSourceElement(element);
    } else {
      loadState->SetSourceElement(this);
    }
    nsresult rv = container->OnFormSubmit(this, loadState);
    NS_ENSURE_SUBMIT_SUCCESS(rv);

    mTargetContext = loadState->TargetBrowsingContext().GetMaybeDiscarded();
    currentLoadId = loadState->GetLoadIdentifier();
  }

  // Even if the submit succeeds, it's possible for there to be no
  // browsing context; for example, if it's to a named anchor within
  // the same page the submit will not really do anything.
  if (mTargetContext && !mTargetContext->IsDiscarded() && !schemeIsJavaScript) {
    mCurrentLoadId = Some(currentLoadId);
  } else {
    ForgetCurrentSubmission();
  }

  return rv;
}

// https://html.spec.whatwg.org/#concept-form-submit step 11
nsresult HTMLFormElement::SubmitDialog(DialogFormSubmission* aFormSubmission) {
  // Close the dialog subject. If there is a result, let that be the return
  // value.
  HTMLDialogElement* dialog = aFormSubmission->DialogElement();
  MOZ_ASSERT(dialog);

  Optional<nsAString> retValue;
  retValue = &aFormSubmission->ReturnValue();
  dialog->Close(retValue);

  return NS_OK;
}

nsresult HTMLFormElement::DoSecureToInsecureSubmitCheck(nsIURI* aActionURL,
                                                        bool* aCancelSubmit) {
  *aCancelSubmit = false;

  if (!StaticPrefs::security_warn_submit_secure_to_insecure()) {
    return NS_OK;
  }

  // Only ask the user about posting from a secure URI to an insecure URI if
  // this element is in the root document. When this is not the case, the mixed
  // content blocker will take care of security for us.
  if (!OwnerDoc()->IsTopLevelContentDocument()) {
    return NS_OK;
  }

  if (nsMixedContentBlocker::IsPotentiallyTrustworthyLoopbackURL(aActionURL)) {
    return NS_OK;
  }

  if (nsMixedContentBlocker::URISafeToBeLoadedInSecureContext(aActionURL)) {
    return NS_OK;
  }

  if (nsMixedContentBlocker::IsPotentiallyTrustworthyOnion(aActionURL)) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = OwnerDoc()->GetWindow();
  if (!window) {
    return NS_ERROR_FAILURE;
  }

  // Now that we know the action URI is insecure check if we're submitting from
  // a secure URI and if so fall thru and prompt user about posting.
  if (nsCOMPtr<nsPIDOMWindowInner> innerWindow = OwnerDoc()->GetInnerWindow()) {
    if (!innerWindow->IsSecureContext()) {
      return NS_OK;
    }
  }

  // Bug 1351358: While file URIs are considered to be secure contexts we allow
  // submitting a form to an insecure URI from a file URI without an alert in an
  // attempt to avoid compatibility issues.
  if (window->GetDocumentURI()->SchemeIs("file")) {
    return NS_OK;
  }

  nsCOMPtr<nsIDocShell> docShell = window->GetDocShell();
  if (!docShell) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  nsCOMPtr<nsIPromptService> promptSvc =
      do_GetService("@mozilla.org/prompter;1", &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsTArray<nsCString> resIds = {"toolkit/global/htmlForm.ftl"_ns};
  RefPtr<intl::Localization> l10n = intl::Localization::Create(resIds, true);
  nsAutoCString title;
  nsAutoCString message;
  nsAutoCString cont;
  ErrorResult error;
  l10n->FormatValueSync("form-post-secure-to-insecure-warning-title"_ns, {},
                        title, error);
  NS_ENSURE_TRUE(!error.Failed(), error.StealNSResult());
  l10n->FormatValueSync("form-post-secure-to-insecure-warning-message"_ns, {},
                        message, error);
  NS_ENSURE_TRUE(!error.Failed(), error.StealNSResult());
  l10n->FormatValueSync("form-post-secure-to-insecure-warning-continue"_ns, {},
                        cont, error);
  NS_ENSURE_TRUE(!error.Failed(), error.StealNSResult());
  int32_t buttonPressed;
  bool checkState =
      false;  // this is unused (ConfirmEx requires this parameter)
  rv = promptSvc->ConfirmExBC(
      docShell->GetBrowsingContext(),
      StaticPrefs::prompts_modalType_insecureFormSubmit(),
      NS_ConvertUTF8toUTF16(title).get(), NS_ConvertUTF8toUTF16(message).get(),
      (nsIPromptService::BUTTON_TITLE_IS_STRING *
       nsIPromptService::BUTTON_POS_0) +
          (nsIPromptService::BUTTON_TITLE_CANCEL *
           nsIPromptService::BUTTON_POS_1),
      NS_ConvertUTF8toUTF16(cont).get(), nullptr, nullptr, nullptr, &checkState,
      &buttonPressed);
  if (NS_FAILED(rv)) {
    return rv;
  }
  *aCancelSubmit = (buttonPressed == 1);
  uint32_t telemetryBucket =
      nsISecurityUITelemetry::WARNING_CONFIRM_POST_TO_INSECURE_FROM_SECURE;
  mozilla::glean::security_ui::events.AccumulateSingleSample(telemetryBucket);
  if (!*aCancelSubmit) {
    // The user opted to continue, so note that in the next telemetry bucket.
    mozilla::glean::security_ui::events.AccumulateSingleSample(telemetryBucket +
                                                               1);
  }
  return NS_OK;
}

nsresult HTMLFormElement::DispatchBeforeSubmitChromeOnlyEvent(
    bool* aCancelSubmit) {
  bool defaultAction = true;
  nsresult rv = nsContentUtils::DispatchEventOnlyToChrome(
      OwnerDoc(), static_cast<nsINode*>(this), u"DOMFormBeforeSubmit"_ns,
      CanBubble::eYes, Cancelable::eYes, &defaultAction);
  *aCancelSubmit = !defaultAction;
  if (*aCancelSubmit) {
    return NS_OK;
  }
  return rv;
}

nsresult HTMLFormElement::ConstructEntryList(FormData* aFormData) {
  MOZ_ASSERT(aFormData, "Must have FormData!");
  if (mIsConstructingEntryList) {
    // Step 2.2 of https://xhr.spec.whatwg.org/#dom-formdata.
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  AutoRestore<bool> resetConstructingEntryList(mIsConstructingEntryList);
  mIsConstructingEntryList = true;
  // This shouldn't be called recursively, so use a rather large value
  // for the preallocated buffer.
  AutoTArray<RefPtr<nsGenericHTMLFormElement>, 100> sortedControls;
  nsresult rv = mControls->GetSortedControls(sortedControls);
  NS_ENSURE_SUCCESS(rv, rv);

  // Walk the list of nodes and call SubmitNamesValues() on the controls
  for (nsGenericHTMLFormElement* control : sortedControls) {
    // Disabled elements don't submit
    if (!control->IsDisabled()) {
      nsCOMPtr<nsIFormControl> fc = nsIFormControl::FromNode(control);
      MOZ_ASSERT(fc);
      // Tell the control to submit its name/value pairs to the submission
      fc->SubmitNamesValues(aFormData);
    }
  }

  FormDataEventInit init;
  init.mBubbles = true;
  init.mCancelable = false;
  init.mFormData = aFormData;
  RefPtr<FormDataEvent> event =
      FormDataEvent::Constructor(this, u"formdata"_ns, init);
  event->SetTrusted(true);

  EventDispatcher::DispatchDOMEvent(this, nullptr, event, nullptr, nullptr);

  return NS_OK;
}

NotNull<const Encoding*> HTMLFormElement::GetSubmitEncoding() {
  nsAutoString acceptCharsetValue;
  GetAttr(nsGkAtoms::acceptcharset, acceptCharsetValue);

  int32_t charsetLen = acceptCharsetValue.Length();
  if (charsetLen > 0) {
    int32_t offset = 0;
    int32_t spPos = 0;
    // get charset from charsets one by one
    do {
      spPos = acceptCharsetValue.FindChar(char16_t(' '), offset);
      int32_t cnt = ((-1 == spPos) ? (charsetLen - offset) : (spPos - offset));
      if (cnt > 0) {
        nsAutoString uCharset;
        acceptCharsetValue.Mid(uCharset, offset, cnt);

        auto encoding = Encoding::ForLabelNoReplacement(uCharset);
        if (encoding) {
          return WrapNotNull(encoding);
        }
      }
      offset = spPos + 1;
    } while (spPos != -1);
  }
  // if there are no accept-charset or all the charset are not supported
  // Get the charset from document
  Document* doc = GetComposedDoc();
  if (doc) {
    return doc->GetDocumentCharacterSet();
  }
  return UTF_8_ENCODING;
}

Element* HTMLFormElement::IndexedGetter(uint32_t aIndex, bool& aFound) {
  Element* element = mControls->mElements.SafeElementAt(aIndex, nullptr);
  aFound = element != nullptr;
  return element;
}

nsresult HTMLFormElement::AddElement(nsGenericHTMLFormElement* aChild,
                                     bool aUpdateValidity, bool aNotify) {
  // If an element has a @form, we can assume it *might* be able to not have
  // a parent and still be in the form.
  NS_ASSERTION(aChild->HasAttr(nsGkAtoms::form) || aChild->GetParent(),
               "Form control should have a parent");
  nsCOMPtr<nsIFormControl> fc = nsIFormControl::FromNode(aChild);
  MOZ_ASSERT(fc);
  // Determine whether to add the new element to the elements or
  // the not-in-elements list.
  bool childInElements = HTMLFormControlsCollection::ShouldBeInElements(fc);
  TreeOrderedArray<nsGenericHTMLFormElement*, TreeKind::ShadowIncludingDOM>&
      controlList =
          childInElements ? mControls->mElements : mControls->mNotInElements;

  const size_t insertedIndex = controlList.Insert(*aChild, this);
  const bool lastElement = controlList.Length() == insertedIndex + 1;

  auto type = fc->ControlType();

  // Default submit element handling
  if (fc->IsSubmitControl()) {
    // Update mDefaultSubmitElement, mFirstSubmitInElements,
    // mFirstSubmitNotInElements.

    nsGenericHTMLFormElement** firstSubmitSlot =
        childInElements ? &mFirstSubmitInElements : &mFirstSubmitNotInElements;

    // The new child is the new first submit in its list if the firstSubmitSlot
    // is currently empty or if the child is before what's currently in the
    // slot.  Note that if we already have a control in firstSubmitSlot and
    // we're appending this element can't possibly replace what's currently in
    // the slot.  Also note that aChild can't become the mDefaultSubmitElement
    // unless it replaces what's in the slot.  If it _does_ replace what's in
    // the slot, it becomes the default submit if either the default submit is
    // what's in the slot or the child is earlier than the default submit.
    if (!*firstSubmitSlot ||
        (!lastElement && nsContentUtils::CompareTreePosition<TreeKind::DOM>(
                             aChild, *firstSubmitSlot, this) < 0)) {
      // Update mDefaultSubmitElement if it's currently in a valid state.
      // Valid state means either non-null or null because there are in fact
      // no submit elements around.
      if ((mDefaultSubmitElement ||
           (!mFirstSubmitInElements && !mFirstSubmitNotInElements)) &&
          (*firstSubmitSlot == mDefaultSubmitElement ||
           nsContentUtils::CompareTreePosition<TreeKind::DOM>(
               aChild, mDefaultSubmitElement, this) < 0)) {
        SetDefaultSubmitElement(aChild);
      }
      *firstSubmitSlot = aChild;
    }

    MOZ_ASSERT(mDefaultSubmitElement == mFirstSubmitInElements ||
                   mDefaultSubmitElement == mFirstSubmitNotInElements ||
                   !mDefaultSubmitElement,
               "What happened here?");
  }

  // If the element is subject to constraint validaton and is invalid, we need
  // to update our internal counter.
  if (aUpdateValidity) {
    nsCOMPtr<nsIConstraintValidation> cvElmt = do_QueryObject(aChild);
    if (cvElmt && cvElmt->IsCandidateForConstraintValidation() &&
        !cvElmt->IsValid()) {
      UpdateValidity(false);
    }
  }

  // Notify the radio button it's been added to a group
  // This has to be done _after_ UpdateValidity() call to prevent the element
  // being count twice.
  if (type == FormControlType::InputRadio) {
    RefPtr<HTMLInputElement> radio = static_cast<HTMLInputElement*>(aChild);
    radio->AddToRadioGroup();
  }

  return NS_OK;
}

nsresult HTMLFormElement::AddElementToTable(nsGenericHTMLFormElement* aChild,
                                            const nsAString& aName) {
  return mControls->AddElementToTable(aChild, aName);
}

void HTMLFormElement::SetDefaultSubmitElement(
    nsGenericHTMLFormElement* aElement) {
  if (mDefaultSubmitElement) {
    // It just so happens that a radio button or an <option> can't be our
    // default submit element, so we can just blindly remove the bit.
    mDefaultSubmitElement->RemoveStates(ElementState::DEFAULT);
  }
  mDefaultSubmitElement = aElement;
  if (mDefaultSubmitElement) {
    mDefaultSubmitElement->AddStates(ElementState::DEFAULT);
  }
}

nsresult HTMLFormElement::RemoveElement(nsGenericHTMLFormElement* aChild,
                                        bool aUpdateValidity) {
  RemoveElementFromPastNamesMap(aChild);

  //
  // Remove it from the radio group if it's a radio button
  //
  nsresult rv = NS_OK;
  nsCOMPtr<nsIFormControl> fc = nsIFormControl::FromNode(aChild);
  MOZ_ASSERT(fc);
  if (fc->ControlType() == FormControlType::InputRadio) {
    RefPtr<HTMLInputElement> radio = static_cast<HTMLInputElement*>(aChild);
    radio->RemoveFromRadioGroup();
  }

  // Determine whether to remove the child from the elements list
  // or the not in elements list.
  bool childInElements = HTMLFormControlsCollection::ShouldBeInElements(fc);
  TreeOrderedArray<nsGenericHTMLFormElement*, TreeKind::ShadowIncludingDOM>&
      controls =
          childInElements ? mControls->mElements : mControls->mNotInElements;

  // Find the index of the child. This will be used later if necessary
  // to find the default submit.
  size_t index = controls.IndexOf(aChild);
  NS_ENSURE_STATE(index != controls.NoIndex);

  controls.RemoveElementAt(index);

  // Update our mFirstSubmit* values.
  nsGenericHTMLFormElement** firstSubmitSlot =
      childInElements ? &mFirstSubmitInElements : &mFirstSubmitNotInElements;
  if (aChild == *firstSubmitSlot) {
    *firstSubmitSlot = nullptr;

    // We are removing the first submit in this list, find the new first submit
    uint32_t length = controls.Length();
    for (uint32_t i = index; i < length; ++i) {
      const auto* currentControl =
          nsIFormControl::FromNode(controls.ElementAt(i));
      MOZ_ASSERT(currentControl);
      if (currentControl->IsSubmitControl()) {
        *firstSubmitSlot = controls.ElementAt(i);
        break;
      }
    }
  }

  if (aChild == mDefaultSubmitElement) {
    // Need to reset mDefaultSubmitElement.  Do this asynchronously so
    // that we're not doing it while the DOM is in flux.
    SetDefaultSubmitElement(nullptr);
    nsContentUtils::AddScriptRunner(MakeAndAddRef<RemoveElementRunnable>(this));

    // Note that we don't need to notify on the old default submit (which is
    // being removed) because it's either being removed from the DOM or
    // changing attributes in a way that makes it responsible for sending its
    // own notifications.
  }

  // If the element was subject to constraint validation and is invalid, we need
  // to update our internal counter.
  if (aUpdateValidity) {
    nsCOMPtr<nsIConstraintValidation> cvElmt = do_QueryObject(aChild);
    if (cvElmt && cvElmt->IsCandidateForConstraintValidation() &&
        !cvElmt->IsValid()) {
      UpdateValidity(true);
    }
  }

  return rv;
}

void HTMLFormElement::HandleDefaultSubmitRemoval() {
  if (mDefaultSubmitElement) {
    // Already got reset somehow; nothing else to do here
    return;
  }

  nsGenericHTMLFormElement* newDefaultSubmit;
  if (!mFirstSubmitNotInElements) {
    newDefaultSubmit = mFirstSubmitInElements;
  } else if (!mFirstSubmitInElements) {
    newDefaultSubmit = mFirstSubmitNotInElements;
  } else {
    NS_ASSERTION(mFirstSubmitInElements != mFirstSubmitNotInElements,
                 "How did that happen?");
    // Have both; use the earlier one
    newDefaultSubmit =
        nsContentUtils::CompareTreePosition<TreeKind::DOM>(
            mFirstSubmitInElements, mFirstSubmitNotInElements, this) < 0
            ? mFirstSubmitInElements
            : mFirstSubmitNotInElements;
  }
  SetDefaultSubmitElement(newDefaultSubmit);

  MOZ_ASSERT(mDefaultSubmitElement == mFirstSubmitInElements ||
                 mDefaultSubmitElement == mFirstSubmitNotInElements,
             "What happened here?");
}

nsresult HTMLFormElement::RemoveElementFromTableInternal(
    nsInterfaceHashtable<nsStringHashKey, nsISupports>& aTable,
    nsIContent* aChild, const nsAString& aName) {
  auto entry = aTable.Lookup(aName);
  if (!entry) {
    return NS_OK;
  }
  // Single element in the hash, just remove it if it's the one
  // we're trying to remove...
  if (entry.Data() == aChild) {
    entry.Remove();
    ++mExpandoAndGeneration.generation;
    return NS_OK;
  }

  nsCOMPtr<nsIContent> content(do_QueryInterface(entry.Data()));
  if (content) {
    return NS_OK;
  }

  // If it's not a content node then it must be a RadioNodeList.
  MOZ_ASSERT(nsCOMPtr<RadioNodeList>(do_QueryInterface(entry.Data())));
  auto* list = static_cast<RadioNodeList*>(entry->get());

  list->RemoveElement(aChild);

  uint32_t length = list->Length();

  if (!length) {
    // If the list is empty we remove if from our hash, this shouldn't
    // happen tho
    entry.Remove();
    ++mExpandoAndGeneration.generation;
  } else if (length == 1) {
    // Only one element left, replace the list in the hash with the
    // single element.
    nsIContent* node = list->Item(0);
    if (node) {
      entry.Data() = node;
    }
  }

  return NS_OK;
}

nsresult HTMLFormElement::RemoveElementFromTable(
    nsGenericHTMLFormElement* aElement, const nsAString& aName) {
  return mControls->RemoveElementFromTable(aElement, aName);
}

already_AddRefed<nsISupports> HTMLFormElement::ResolveName(
    const nsAString& aName) {
  nsCOMPtr<nsISupports> result = mControls->NamedItemInternal(aName);
  if (result) {
    AddToPastNamesMap(aName, result);
    return result.forget();
  }

  result = mImageNameLookupTable.GetWeak(aName);
  if (result) {
    AddToPastNamesMap(aName, result);
    return result.forget();
  }

  result = mPastNameLookupTable.GetWeak(aName);
  return result.forget();
}

already_AddRefed<nsISupports> HTMLFormElement::NamedGetter(
    const nsAString& aName, bool& aFound) {
  if (nsCOMPtr<nsISupports> result = ResolveName(aName)) {
    aFound = true;
    return result.forget();
  }

  aFound = false;
  return nullptr;
}

void HTMLFormElement::GetSupportedNames(nsTArray<nsString>& aRetval) {
  // TODO https://github.com/whatwg/html/issues/1731
}

void HTMLFormElement::OnSubmitClickBegin() { mDeferSubmission = true; }

void HTMLFormElement::OnSubmitClickEnd() { mDeferSubmission = false; }

void HTMLFormElement::FlushPendingSubmission() {
  MOZ_ASSERT(!mDeferSubmission);

  if (mPendingSubmission) {
    // Transfer owning reference so that the submission doesn't get deleted
    // if we reenter
    UniquePtr<HTMLFormSubmission> submission = std::move(mPendingSubmission);

    SubmitSubmission(submission.get());
  }
}

void HTMLFormElement::GetAction(nsString& aValue) {
  if (!GetAttr(nsGkAtoms::action, aValue) || aValue.IsEmpty()) {
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
    GetURIAttr(nsGkAtoms::action, nullptr, aValue);
  }
}

nsresult HTMLFormElement::GetActionURL(nsIURI** aActionURL,
                                       Element* aOriginatingElement) {
  nsresult rv = NS_OK;

  *aActionURL = nullptr;

  //
  // Grab the URL string
  //
  // If the originating element is a submit control and has the formaction
  // attribute specified, it should be used. Otherwise, the action attribute
  // from the form element should be used.
  //
  nsAutoString action;

  if (aOriginatingElement &&
      aOriginatingElement->HasAttr(nsGkAtoms::formaction)) {
#ifdef DEBUG
    const auto* formControl = nsIFormControl::FromNode(aOriginatingElement);
    NS_ASSERTION(formControl && formControl->IsSubmitControl(),
                 "The originating element must be a submit form control!");
#endif  // DEBUG

    HTMLInputElement* inputElement =
        HTMLInputElement::FromNode(aOriginatingElement);
    if (inputElement) {
      inputElement->GetFormAction(action);
    } else {
      auto buttonElement = HTMLButtonElement::FromNode(aOriginatingElement);
      if (buttonElement) {
        buttonElement->GetFormAction(action);
      } else {
        NS_ERROR("Originating element must be an input or button element!");
        return NS_ERROR_UNEXPECTED;
      }
    }
  } else {
    GetAction(action);
  }

  //
  // Form the full action URL
  //

  // Get the document to form the URL.
  // We'll also need it later to get the DOM window when notifying form submit
  // observers (bug 33203)
  if (!IsInComposedDoc()) {
    return NS_OK;  // No doc means don't submit, see Bug 28988
  }

  // Get base URL
  Document* document = OwnerDoc();
  nsIURI* docURI = document->GetDocumentURI();
  NS_ENSURE_TRUE(docURI, NS_ERROR_UNEXPECTED);

  // If an action is not specified and we are inside
  // a HTML document then reload the URL. This makes us
  // compatible with 4.x browsers.
  // If we are in some other type of document such as XML or
  // XUL, do nothing. This prevents undesirable reloading of
  // a document inside XUL.

  nsCOMPtr<nsIURI> actionURL;
  if (action.IsEmpty()) {
    if (!document->IsHTMLOrXHTML()) {
      // Must be a XML, XUL or other non-HTML document type
      // so do nothing.
      return NS_OK;
    }

    actionURL = docURI;
  } else {
    nsIURI* baseURL = GetBaseURI();
    NS_ASSERTION(baseURL, "No Base URL found in Form Submit!\n");
    if (!baseURL) {
      return NS_OK;  // No base URL -> exit early, see Bug 30721
    }
    rv = NS_NewURI(getter_AddRefs(actionURL), action, nullptr, baseURL);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  //
  // Verify the URL should be reached
  //
  // Get security manager, check to see if access to action URI is allowed.
  //
  nsIScriptSecurityManager* securityManager =
      nsContentUtils::GetSecurityManager();
  rv = securityManager->CheckLoadURIWithPrincipal(
      NodePrincipal(), actionURL, nsIScriptSecurityManager::STANDARD,
      OwnerDoc()->InnerWindowID());
  NS_ENSURE_SUCCESS(rv, rv);

  // Potentially the page uses the CSP directive 'upgrade-insecure-requests'. In
  // such a case we have to upgrade the action url from http:// to https://.
  // The upgrade is only required if the actionURL is http and not a potentially
  // trustworthy loopback URI.
  bool needsUpgrade =
      actionURL->SchemeIs("http") &&
      !nsMixedContentBlocker::IsPotentiallyTrustworthyLoopbackURL(actionURL) &&
      document->GetUpgradeInsecureRequests(false);
  if (needsUpgrade) {
    // let's use the old specification before the upgrade for logging
    AutoTArray<nsString, 2> params;
    nsAutoCString spec;
    rv = actionURL->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);
    CopyUTF8toUTF16(spec, *params.AppendElement());

    // upgrade the actionURL from http:// to use https://
    nsCOMPtr<nsIURI> upgradedActionURL;
    rv = NS_GetSecureUpgradedURI(actionURL, getter_AddRefs(upgradedActionURL));
    NS_ENSURE_SUCCESS(rv, rv);
    actionURL = std::move(upgradedActionURL);

    // let's log a message to the console that we are upgrading a request
    nsAutoCString scheme;
    rv = actionURL->GetScheme(scheme);
    NS_ENSURE_SUCCESS(rv, rv);
    CopyUTF8toUTF16(scheme, *params.AppendElement());

    CSP_LogLocalizedStr(
        "upgradeInsecureRequest", params,
        ""_ns,   // aSourceFile
        u""_ns,  // aScriptSample
        0,       // aLineNumber
        1,       // aColumnNumber
        nsIScriptError::warningFlag, "upgradeInsecureRequest"_ns,
        document->InnerWindowID(),
        document->NodePrincipal()->OriginAttributesRef().IsPrivateBrowsing());
  }

  //
  // Assign to the output
  //
  actionURL.forget(aActionURL);

  return rv;
}

void HTMLFormElement::GetSubmissionTarget(nsGenericHTMLElement* aSubmitter,
                                          nsAString& aTarget) {
  // https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm
  // 19. If the submitter element is a submit button and it has a formtarget
  // attribute, then set formTarget to the formtarget attribute value.
  // 20. Let target be the result of getting an element's target given
  // submitter's form owner and formTarget.
  //
  // Note: Falling back to the base target is part of "get an element's target".
  if (!(aSubmitter && aSubmitter->GetAttr(nsGkAtoms::formtarget, aTarget)) &&
      !GetAttr(nsGkAtoms::target, aTarget)) {
    GetBaseTarget(aTarget);
  }
  SanitizeLinkOrFormTarget(aTarget);
}

nsGenericHTMLFormElement* HTMLFormElement::GetDefaultSubmitElement() const {
  MOZ_ASSERT(mDefaultSubmitElement == mFirstSubmitInElements ||
                 mDefaultSubmitElement == mFirstSubmitNotInElements,
             "What happened here?");

  return mDefaultSubmitElement;
}

bool HTMLFormElement::ImplicitSubmissionIsDisabled() const {
  // Input text controls are always in the elements list.
  uint32_t numDisablingControlsFound = 0;
  for (auto* element : mControls->mElements.AsSpan()) {
    const auto* fc = nsIFormControl::FromNode(element);
    MOZ_ASSERT(fc);
    if (fc->IsSingleLineTextControl(false)) {
      numDisablingControlsFound++;
      if (numDisablingControlsFound > 1) {
        break;
      }
    }
  }
  return numDisablingControlsFound != 1;
}

bool HTMLFormElement::IsLastActiveElement(
    const nsGenericHTMLFormElement* aElement) const {
  MOZ_ASSERT(aElement, "Unexpected call");

  // See bug 2007450 for why this temporary is needed.
  Span elements = mControls->mElements.AsSpan();
  for (auto* element : Reversed(elements)) {
    const auto* fc = nsIFormControl::FromNode(element);
    MOZ_ASSERT(fc);
    // XXX How about date/time control?
    if (fc->IsTextControl(false) && !element->IsDisabled()) {
      return element == aElement;
    }
  }
  return false;
}

int32_t HTMLFormElement::Length() { return mControls->Length(); }

void HTMLFormElement::ForgetCurrentSubmission() {
  mNotifiedObservers = false;
  mTargetContext = nullptr;
  mCurrentLoadId = Nothing();
}

bool HTMLFormElement::CheckFormValidity(
    nsTArray<RefPtr<Element>>* aInvalidElements) const {
  bool ret = true;

  // This shouldn't be called recursively, so use a rather large value
  // for the preallocated buffer.
  AutoTArray<RefPtr<nsGenericHTMLFormElement>, 100> sortedControls;
  if (NS_FAILED(mControls->GetSortedControls(sortedControls))) {
    return false;
  }

  uint32_t len = sortedControls.Length();

  for (uint32_t i = 0; i < len; ++i) {
    nsCOMPtr<nsIConstraintValidation> cvElmt =
        do_QueryObject(sortedControls[i]);
    bool defaultAction = true;
    if (cvElmt && !cvElmt->CheckValidity(*sortedControls[i], &defaultAction)) {
      ret = false;

      // Add all unhandled invalid controls to aInvalidElements if the caller
      // requested them.
      if (defaultAction && aInvalidElements) {
        aInvalidElements->AppendElement(sortedControls[i]);
      }
    }
  }

  return ret;
}

bool HTMLFormElement::CheckValidFormSubmission() {
  /**
   * Check for form validity: do not submit a form if there are unhandled
   * invalid controls in the form.
   * This should not be done if the form has been submitted with .submit() or
   * has been submitted and novalidate/formnovalidate is used.
   *
   * NOTE: for the moment, we are also checking that whether the MozInvalidForm
   * event gets prevented default so it will prevent blocking form submission if
   * the browser does not have implemented a UI yet.
   *
   * TODO: the check for MozInvalidForm event should be removed later when HTML5
   * Forms will be spread enough and authors will assume forms can't be
   * submitted when invalid. See bug 587671.
   */

  AutoTArray<RefPtr<Element>, 32> invalidElements;
  if (CheckFormValidity(&invalidElements)) {
    return true;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(GetRelevantGlobal())) {
    return false;
  }
  JS::Rooted<JS::Value> detail(jsapi.cx());
  if (!ToJSValue(jsapi.cx(), invalidElements, &detail)) {
    return false;
  }

  RefPtr<CustomEvent> event =
      NS_NewDOMCustomEvent(OwnerDoc(), nullptr, nullptr);
  event->InitCustomEvent(jsapi.cx(), u"MozInvalidForm"_ns,
                         /* CanBubble */ true,
                         /* Cancelable */ true, detail);
  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;

  DispatchEvent(*event);

  ReportInvalidUnfocusableElements(std::move(invalidElements));

  return !event->DefaultPrevented();
}

void HTMLFormElement::UpdateValidity(bool aElementValidity) {
  if (aElementValidity) {
    --mInvalidElementsCount;
  } else {
    ++mInvalidElementsCount;
  }

  NS_ASSERTION(mInvalidElementsCount >= 0, "Something went seriously wrong!");

  // The form validity has just changed if:
  // - there are no more invalid elements ;
  // - or there is one invalid elmement and an element just became invalid.
  // If we have invalid elements and we used to before as well, do nothing.
  if (mInvalidElementsCount &&
      (mInvalidElementsCount != 1 || aElementValidity)) {
    return;
  }

  AutoStateChangeNotifier notifier(*this, true);
  RemoveStatesSilently(ElementState::VALID | ElementState::INVALID);
  AddStatesSilently(mInvalidElementsCount ? ElementState::INVALID
                                          : ElementState::VALID);
}

int32_t HTMLFormElement::IndexOfContent(nsIContent* aContent) {
  int32_t index = 0;
  return mControls->IndexOfContent(aContent, &index) == NS_OK ? index : 0;
}

void HTMLFormElement::Clear() {
  for (HTMLImageElement* image : mImageElements.AsSpan()) {
    image->ClearForm(false);
  }
  mImageElements.Clear();
  mImageNameLookupTable.Clear();
  mPastNameLookupTable.Clear();
}

namespace {

struct PositionComparator {
  nsIContent* const mElement;
  mutable nsContentUtils::NodeIndexCache mCache;
  explicit PositionComparator(nsIContent* const aElement)
      : mElement(aElement) {}

  int operator()(nsIContent* aElement) const {
    if (mElement == aElement) {
      return 0;
    }
    return nsContentUtils::CompareTreePosition<TreeKind::DOM>(
        mElement, aElement, nullptr, &mCache);
  }
};

struct RadioNodeListAdaptor {
  RadioNodeList* const mList;
  explicit RadioNodeListAdaptor(RadioNodeList* aList) : mList(aList) {}
  nsIContent* operator[](size_t aIdx) const { return mList->Item(aIdx); }
};

}  // namespace

nsresult HTMLFormElement::AddElementToTableInternal(
    nsInterfaceHashtable<nsStringHashKey, nsISupports>& aTable,
    nsIContent* aChild, const nsAString& aName) {
  return aTable.WithEntryHandle(aName, [&](auto&& entry) {
    if (!entry) {
      // No entry found, add the element
      entry.Insert(aChild);
      ++mExpandoAndGeneration.generation;
    } else {
      // Found something in the hash, check its type
      nsCOMPtr<nsIContent> content = do_QueryInterface(entry.Data());

      if (content) {
        // Check if the new content is the same as the one we found in the
        // hash, if it is then we leave it in the hash as it is, this will
        // happen if a form control has both a name and an id with the same
        // value
        if (content == aChild) {
          return NS_OK;
        }

        // Found an element, create a list, add the element to the list and put
        // the list in the hash
        RefPtr list = new RadioNodeList(this);

        // If an element has a @form, we can assume it *might* be able to not
        // have a parent and still be in the form.
        NS_ASSERTION(
            (content->IsElement() && content->AsElement()->HasAttr(
                                         kNameSpaceID_None, nsGkAtoms::form)) ||
                content->GetParent(),
            "Item in list without parent");

        // Determine the ordering between the new and old element.
        bool newFirst = nsContentUtils::PositionIsBefore(aChild, content);

        list->AppendElement(newFirst ? aChild : content.get());
        list->AppendElement(newFirst ? content.get() : aChild);

        // Replace the element with the list.
        entry.Data() = std::move(list);
      } else {
        // There's already a list in the hash, add the child to the list.
        MOZ_ASSERT(nsCOMPtr<RadioNodeList>(do_QueryInterface(entry.Data())));
        auto* list = static_cast<RadioNodeList*>(entry->get());

        NS_ASSERTION(
            list->Length() > 1,
            "List should have been converted back to a single element");

        PositionComparator cmp(aChild);

        // Fast-path appends; this check is ok even if the child is
        // already in the list, since if it tests true the child would
        // have come at the end of the list, and the PositionIsBefore
        // will test false.
        if (cmp(list->Item(list->Length() - 1)) > 0) {
          list->AppendElement(aChild);
          return NS_OK;
        }

        size_t idx;
        const bool found = BinarySearchIf(RadioNodeListAdaptor(list), 0,
                                          list->Length(), cmp, &idx);
        if (found &&
            (list->Item(idx) == aChild || list->IndexOf(aChild) != -1)) {
          // If a control has a name equal to its id, it could be in the list
          // already. Also, found could be true mid-unbind even though the node
          // is not the same. That's a temporarily-broken state.
          return NS_OK;
        }
        list->InsertElementAt(aChild, idx);
      }
    }

    return NS_OK;
  });
}

nsresult HTMLFormElement::AddImageElement(HTMLImageElement* aElement) {
  mImageElements.Insert(*aElement, this);
  return NS_OK;
}

nsresult HTMLFormElement::AddImageElementToTable(HTMLImageElement* aChild,
                                                 const nsAString& aName) {
  return AddElementToTableInternal(mImageNameLookupTable, aChild, aName);
}

nsresult HTMLFormElement::RemoveImageElement(HTMLImageElement* aElement) {
  RemoveElementFromPastNamesMap(aElement);
  mImageElements.RemoveElement(*aElement);
  return NS_OK;
}

nsresult HTMLFormElement::RemoveImageElementFromTable(
    HTMLImageElement* aElement, const nsAString& aName) {
  return RemoveElementFromTableInternal(mImageNameLookupTable, aElement, aName);
}

void HTMLFormElement::AddToPastNamesMap(const nsAString& aName,
                                        nsISupports* aChild) {
  // If candidates contains exactly one node. Add a mapping from name to the
  // node in candidates in the form element's past names map, replacing the
  // previous entry with the same name, if any.
  nsCOMPtr<nsIContent> node = do_QueryInterface(aChild);
  if (node) {
    mPastNameLookupTable.InsertOrUpdate(aName, ToSupports(node));
    node->SetFlags(MAY_BE_IN_PAST_NAMES_MAP);
  }
}

void HTMLFormElement::RemoveElementFromPastNamesMap(Element* aElement) {
  if (!aElement->HasFlag(MAY_BE_IN_PAST_NAMES_MAP)) {
    return;
  }

  aElement->UnsetFlags(MAY_BE_IN_PAST_NAMES_MAP);

  uint32_t oldCount = mPastNameLookupTable.Count();
  for (auto iter = mPastNameLookupTable.Iter(); !iter.Done(); iter.Next()) {
    if (aElement == iter.Data()) {
      iter.Remove();
    }
  }
  if (oldCount != mPastNameLookupTable.Count()) {
    ++mExpandoAndGeneration.generation;
  }
}

JSObject* HTMLFormElement::WrapNode(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return HTMLFormElement_Binding::Wrap(aCx, this, aGivenProto);
}

int32_t HTMLFormElement::GetFormNumberForStateKey() {
  if (mFormNumber == -1) {
    mFormNumber = OwnerDoc()->GetNextFormNumber();
  }
  return mFormNumber;
}

void HTMLFormElement::NodeInfoChanged(Document* aOldDoc) {
  nsGenericHTMLElement::NodeInfoChanged(aOldDoc);

  // When a <form> element is adopted into a new document, we want any state
  // keys generated from it to no longer consider this element to be parser
  // inserted, and so have state keys based on the position of the <form>
  // element in the document, rather than the order it was inserted in.
  //
  // This is not strictly necessary, since we only ever look at the form number
  // for parser inserted form controls, and we do that at the time the form
  // control element is inserted into its original document by the parser.
  mFormNumber = -1;
}

bool HTMLFormElement::IsSubmitting() const {
  bool loading = mTargetContext && !mTargetContext->IsDiscarded() &&
                 mCurrentLoadId &&
                 mTargetContext->IsLoadingIdentifier(*mCurrentLoadId);
  return loading;
}

void HTMLFormElement::MaybeFireFormRemoved() {
  // We want this event to be fired only when the form is removed from the DOM
  // tree, not when it is released (ex, tab is closed). So don't fire an event
  // when the form doesn't have a docshell.
  Document* doc = GetComposedDoc();
  nsIDocShell* container = doc ? doc->GetDocShell() : nullptr;
  if (!container) {
    return;
  }

  // Right now, only the password manager and formautofill listen to the event
  // and only listen to it under certain circumstances. So don't fire this event
  // unless necessary.
  if (!doc->ShouldNotifyFormOrPasswordRemoved()) {
    return;
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *this, u"DOMFormRemoved"_ns, CanBubble::eNo, ChromeOnlyDispatch::eYes);
}

}  // namespace mozilla::dom
