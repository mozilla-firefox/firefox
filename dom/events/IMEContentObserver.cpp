/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IMEContentObserver.h"

#include "ContentEventHandler.h"
#include "WritingModes.h"
#include "mozilla/Assertions.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/TextControlState.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Selection.h"
#include "nsAtom.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsISelectionController.h"
#include "nsISupports.h"
#include "nsIWeakReferenceUtils.h"
#include "nsIWidget.h"
#include "nsPresContext.h"
#include "nsRange.h"
#include "nsRefreshDriver.h"
#include "nsString.h"

namespace mozilla {

using RawNodePosition = ContentEventHandler::RawNodePosition;

using namespace dom;
using namespace widget;

LazyLogModule sIMECOLog("IMEContentObserver");
LazyLogModule sCacheLog("IMEContentObserverCache");

static const char* ToChar(bool aBool) { return aBool ? "true" : "false"; }

static const char* ShortenFunctionName(const char* aFunctionName) {
  const nsDependentCString name(aFunctionName);
  const int32_t startIndexOfIMEContentObserverPrefix =
      name.Find("IMEContentObserver::", 0);
  if (startIndexOfIMEContentObserverPrefix >= 0) {
    return aFunctionName + startIndexOfIMEContentObserverPrefix +
           strlen("IMEContentObserver::");
  }
  return aFunctionName;
}

/******************************************************************************
 * mozilla::IMEContentObserver
 ******************************************************************************/

NS_IMPL_CYCLE_COLLECTION_CLASS(IMEContentObserver)

// Note that we don't need to add mFirstAddedContainer nor
// mLastAddedContainer to cycle collection because they are non-null only
// during short time and shouldn't be touched while they are non-null.

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(IMEContentObserver)
  nsAutoScriptBlocker scriptBlocker;

  tmp->NotifyIMEOfBlur();
  tmp->UnregisterObservers();

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRootElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRootEditableNodeOrTextControlElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocShell)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEditorBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEndOfAddedTextCache.mContainerNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEndOfAddedTextCache.mContent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mStartOfRemovingTextRangeCache.mContainerNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mStartOfRemovingTextRangeCache.mContent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE

  tmp->mIMENotificationRequests = nullptr;
  tmp->mESM = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(IMEContentObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWidget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFocusedWidget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRootElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRootEditableNodeOrTextControlElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocShell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEditorBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEndOfAddedTextCache.mContainerNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEndOfAddedTextCache.mContent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mStartOfRemovingTextRangeCache.mContainerNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStartOfRemovingTextRangeCache.mContent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IMEContentObserver)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsIReflowObserver)
  NS_INTERFACE_MAP_ENTRY(nsIScrollObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIReflowObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(IMEContentObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IMEContentObserver)

IMEContentObserver::IMEContentObserver() {
#ifdef DEBUG
  // TODO: Make this test as GTest.
  mTextChangeData.Test();
#endif
}

void IMEContentObserver::Init(nsIWidget& aWidget, nsPresContext& aPresContext,
                              Element* aElement, EditorBase& aEditorBase) {
  State state = GetState();
  if (NS_WARN_IF(state == eState_Observing)) {
    return;  // Nothing to do.
  }

  bool firstInitialization = state != eState_StoppedObserving;
  if (!firstInitialization) {
    // If this is now trying to initialize with new contents, all observers
    // should be registered again for simpler implementation.
    UnregisterObservers();
    Clear();
  }

  mESM = aPresContext.EventStateManager();
  mESM->OnStartToObserveContent(this);

  mWidget = &aWidget;
  mIMENotificationRequests = &mWidget->IMENotificationRequestsRef();

  if (!InitWithEditor(aPresContext, aElement, aEditorBase)) {
    MOZ_LOG(sIMECOLog, LogLevel::Error,
            ("0x%p   Init() FAILED, due to InitWithEditor() "
             "failure",
             this));
    Clear();
    return;
  }

  if (firstInitialization) {
    // Now, try to send NOTIFY_IME_OF_FOCUS to IME via the widget.
    MaybeNotifyIMEOfFocusSet();
    // When this is called first time, IME has not received NOTIFY_IME_OF_FOCUS
    // yet since NOTIFY_IME_OF_FOCUS will be sent to widget asynchronously.
    // So, we need to do nothing here.  After NOTIFY_IME_OF_FOCUS has been
    // sent, OnIMEReceivedFocus() will be called and content, selection and/or
    // position changes will be observed
    return;
  }

  // When this is called after editor reframing (i.e., the root editable node
  // is also recreated), IME has usually received NOTIFY_IME_OF_FOCUS.  In this
  // case, we need to restart to observe content, selection and/or position
  // changes in new root editable node.
  ObserveEditableNode();

  if (!NeedsToNotifyIMEOfSomething()) {
    return;
  }

  // Some change events may wait to notify IME because this was being
  // initialized.  It is the time to flush them.
  FlushMergeableNotifications();
}

void IMEContentObserver::OnIMEReceivedFocus() {
  // While Init() notifies IME of focus, pending layout may be flushed
  // because the notification may cause querying content.  Then, recursive
  // call of Init() with the latest content may occur.  In such case, we
  // shouldn't keep first initialization which notified IME of focus.
  if (GetState() != eState_Initializing) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   OnIMEReceivedFocus(), "
             "but the state is not \"initializing\", so does nothing",
             this));
    return;
  }

  // NOTIFY_IME_OF_FOCUS might cause recreating IMEContentObserver
  // instance via IMEStateManager::UpdateIMEState().  So, this
  // instance might already have been destroyed, check it.
  if (!mRootElement) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   OnIMEReceivedFocus(), "
             "but mRootElement has already been cleared, so does nothing",
             this));
    return;
  }

  // Start to observe which is needed by IME when IME actually has focus.
  ObserveEditableNode();

  if (!NeedsToNotifyIMEOfSomething()) {
    return;
  }

  // Some change events may wait to notify IME because this was being
  // initialized.  It is the time to flush them.
  FlushMergeableNotifications();
}

dom::Selection* IMEContentObserver::GetSelection() const {
  if (NS_WARN_IF(!mEditorBase) ||
      NS_WARN_IF(!mRootEditableNodeOrTextControlElement)) {
    return nullptr;
  }
  nsCOMPtr<nsISelectionController> selCon;
  if (mRootEditableNodeOrTextControlElement->IsElement()) {
    selCon = mEditorBase->GetSelectionController();
  } else {
    MOZ_ASSERT(mRootEditableNodeOrTextControlElement->IsDocument());
    selCon =
        mRootEditableNodeOrTextControlElement->AsDocument()->GetPresShell();
  }
  if (NS_WARN_IF(!selCon)) {
    return nullptr;
  }
  return selCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
}

bool IMEContentObserver::InitWithEditor(nsPresContext& aPresContext,
                                        Element* aElement,
                                        EditorBase& aEditorBase) {
  mRootEditableNodeOrTextControlElement =
      aEditorBase.IsTextEditor()
          ? aEditorBase.GetExposedRoot()
          : IMEContentObserver::GetMostDistantInclusiveEditableAncestorNode(
                aPresContext, aElement);
  if (NS_WARN_IF(!mRootEditableNodeOrTextControlElement)) {
    return false;
  }
  MOZ_ASSERT_IF(aEditorBase.IsHTMLEditor() &&
                    mRootEditableNodeOrTextControlElement->IsInDesignMode(),
                IsForDesignMode());
  MOZ_ASSERT_IF(aEditorBase.IsHTMLEditor() &&
                    !mRootEditableNodeOrTextControlElement->IsInDesignMode(),
                !IsForDesignMode());
  MOZ_ASSERT_IF(aEditorBase.IsTextEditor(), !IsForDesignMode());

  mEditorBase = &aEditorBase;

  RefPtr<PresShell> presShell = aPresContext.GetPresShell();

  if (EditContext* editContext = aEditorBase.ComputeEditContext()) {
    mIsForEditContext = true;
    // If there's an active EditContext, set mRootElement from it directly,
    // instead of using ComputeRootElement(), since that looks at the selection
    // (see bug 2055955).
    mRootElement = editContext->GetAssociatedElement();
    MOZ_ASSERT(mRootElement,
               "Active EditContext should always have an associated element.");
  } else {
    RefPtr selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return false;
    }

    mRootElement = ComputeRootElement(presShell);
    // If we're in the design mode, but there are no contents, this document
    // is not editable. However, this is not illegal. Don't warn.
    if (!mRootElement && IsForDesignMode()) {
      return false;
    }
    // Otherwise, there must be a root editable element.
    if (NS_WARN_IF(!mRootElement)) {
      return false;
    }
  }

  if (mEditorBase->IsTextEditor()) {
    MOZ_ASSERT(mRootElement);
    MOZ_ASSERT(mRootElement->GetFirstChild());
    if (auto* text = Text::FromNodeOrNull(
            mRootElement ? mRootElement->GetFirstChild() : nullptr)) {
      mTextControlValueLength = ContentEventHandler::GetNativeTextLength(*text);
    }
    mIsTextControl = true;
  }

  mDocShell = aPresContext.GetDocShell();
  if (NS_WARN_IF(!mDocShell)) {
    return false;
  }

  mDocumentObserver = new DocumentObserver(*this);

  return true;
}

Element* IMEContentObserver::ComputeRootElement(PresShell* aPresShell) const {
  if (NS_WARN_IF(!aPresShell) ||
      NS_WARN_IF(!mRootEditableNodeOrTextControlElement)) {
    return nullptr;
  }
  Selection* const selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return nullptr;
  }

  if (mEditorBase->IsTextEditor()) {
    return mEditorBase->GetRoot();  // The anonymous <div>
  }

  // If there is a selection range, we should compute our root element from the
  // first range.
  if (const nsRange* selRange = selection->GetRangeAt(0)) {
    MOZ_ASSERT(!mIsTextControl);
    if (NS_WARN_IF(!selRange->GetStartContainer())) {
      return nullptr;
    }

    // If an editing host has focus, our root element should be it.
    // Otherwise, if we're in the design mode, the <body> should be the our root
    // element if there is and startContainer is not outside of the <body>.
    // Otherwise, the document element should be used instead.
    return Element::FromNodeOrNull(
        selRange->GetStartContainer()->GetSelectionRootContent(
            aPresShell, nsINode::IgnoreOwnIndependentSelection::Yes,
            nsINode::AllowCrossShadowBoundary::No));
  }

  // If there is no selection range in the design mode, we should use the <body>
  // if there is. Otherwise, the document element.
  if (mRootEditableNodeOrTextControlElement->IsInDesignMode()) {
    MOZ_ASSERT(mRootEditableNodeOrTextControlElement->IsDocument());
    Element* const bodyElement =
        mRootEditableNodeOrTextControlElement->AsDocument()->GetBody();
    if (bodyElement && bodyElement->IsEditable()) {
      return bodyElement;
    }
    return mRootEditableNodeOrTextControlElement->AsDocument()
        ->GetRootElement();
  }

  // If an editing host has focus, it should be the our root element.
  return Element::FromNodeOrNull(
      mRootEditableNodeOrTextControlElement->GetSelectionRootContent(
          aPresShell, nsINode::IgnoreOwnIndependentSelection::Yes,
          nsINode::AllowCrossShadowBoundary::No));
}

void IMEContentObserver::Clear() {
  mEditorBase = nullptr;
  mRootEditableNodeOrTextControlElement = nullptr;
  mRootElement = nullptr;
  mDocShell = nullptr;
  // Should be safe to clear mDocumentObserver here even though it grabs
  // this instance in most cases because this is called by Init() or Destroy().
  // The callers of Init() grab this instance with local RefPtr.
  // The caller of Destroy() also grabs this instance with local RefPtr.
  // So, this won't cause refcount of this instance become 0.
  mDocumentObserver = nullptr;
}

void IMEContentObserver::ObserveEditableNode() {
  MOZ_RELEASE_ASSERT(mRootElement);
  MOZ_RELEASE_ASSERT(GetState() != eState_Observing);

  // If this is called before sending NOTIFY_IME_OF_FOCUS (it's possible when
  // the editor is reframed before sending NOTIFY_IME_OF_FOCUS asynchronously),
  // the notification requests of mWidget may be different from after the widget
  // receives NOTIFY_IME_OF_FOCUS.   So, this should be called again by
  // OnIMEReceivedFocus() which is called after sending NOTIFY_IME_OF_FOCUS.
  if (!mIMEHasFocus) {
    MOZ_ASSERT(!mWidget || mNeedsToNotifyIMEOfFocusSet ||
                   mSendingNotification == NOTIFY_IME_OF_FOCUS,
               "Wow, OnIMEReceivedFocus() won't be called?");
    return;
  }

  mIsObserving = true;
  if (mEditorBase) {
    mEditorBase->SetIMEContentObserver(this);
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p ObserveEditableNode(), starting to observe 0x%p (%s)", this,
           mRootElement.get(), ToString(*mRootElement).c_str()));

  // For EditContext, we shouldn't observe the element for mutations,
  // since updating the text is only done through the updateText() method
  // or text input.
  if (!mIsForEditContext) {
    mRootElement->AddMutationObserver(this);
    // If it's in a document (should be so), we can use document observer to
    // reduce redundant computation of text change offsets.
    Document* doc = mRootElement->GetComposedDoc();
    if (doc) {
      RefPtr<DocumentObserver> documentObserver = mDocumentObserver;
      documentObserver->Observe(doc);
    }
  }

  if (mDocShell) {
    // Add scroll position listener and reflow observer to detect position
    // and size changes
    mDocShell->AddWeakScrollObserver(this);
    mDocShell->AddWeakReflowObserver(this);
  }
}

void IMEContentObserver::NotifyIMEOfBlur() {
  // Prevent any notifications to be sent IME.
  nsCOMPtr<nsIWidget> widget;
  mWidget.swap(widget);
  mIMENotificationRequests = nullptr;

  // If we hasn't been set focus, we shouldn't send blur notification to IME.
  if (!mIMEHasFocus) {
    return;
  }

  // mWidget must have been non-nullptr if IME has focus.
  MOZ_RELEASE_ASSERT(widget);

  RefPtr<IMEContentObserver> kungFuDeathGrip(this);

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p NotifyIMEOfBlur(), sending NOTIFY_IME_OF_BLUR", this));

  // For now, we need to send blur notification in any condition because
  // we don't have any simple ways to send blur notification asynchronously.
  // After this call, Destroy() or Unlink() will stop observing the content
  // and forget everything.  Therefore, if it's not safe to send notification
  // when script blocker is unlocked, we cannot send blur notification after
  // that and before next focus notification.
  // Anyway, as far as we know, IME doesn't try to query content when it loses
  // focus.  So, this may not cause any problem.
  mIMEHasFocus = false;
  IMEStateManager::NotifyIME(IMENotification(NOTIFY_IME_OF_BLUR), widget);

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   NotifyIMEOfBlur(), sent NOTIFY_IME_OF_BLUR", this));
}

void IMEContentObserver::UnregisterObservers() {
  if (!mIsObserving) {
    return;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p UnregisterObservers(), stop observing 0x%p (%s)", this,
           mRootElement.get(),
           mRootElement ? ToString(*mRootElement).c_str() : "nullptr"));

  mIsObserving = false;
  mSelectionData.Clear();
  mFocusedWidget = nullptr;

  if (mEditorBase) {
    mEditorBase->SetIMEContentObserver(nullptr);
  }

  if (mRootElement) {
    mRootElement->RemoveMutationObserver(this);
  }

  if (mDocumentObserver) {
    RefPtr<DocumentObserver> documentObserver = mDocumentObserver;
    documentObserver->StopObserving();
  }

  if (mDocShell) {
    mDocShell->RemoveWeakScrollObserver(this);
    mDocShell->RemoveWeakReflowObserver(this);
  }
}

nsPresContext* IMEContentObserver::GetPresContext() const {
  return mESM ? mESM->GetPresContext() : nullptr;
}

void IMEContentObserver::Destroy() {
  // WARNING: When you change this method, you have to check Unlink() too.

  // Note that don't send any notifications later from here.  I.e., notify
  // IMEStateManager of the blur synchronously because IMEStateManager needs to
  // stop notifying the main process if this is requested by the main process.
  NotifyIMEOfBlur();
  UnregisterObservers();
  Clear();

  mWidget = nullptr;
  mIMENotificationRequests = nullptr;

  if (mESM) {
    mESM->OnStopObservingContent(this);
    mESM = nullptr;
  }
}

bool IMEContentObserver::Destroyed() const { return !mWidget; }

void IMEContentObserver::DisconnectFromEventStateManager() { mESM = nullptr; }

bool IMEContentObserver::MaybeReinitialize(nsIWidget& aWidget,
                                           nsPresContext& aPresContext,
                                           Element* aElement,
                                           EditorBase& aEditorBase) {
  if (!IsObservingElement(aPresContext, aElement)) {
    return false;
  }

  if (GetState() == eState_StoppedObserving) {
    Init(aWidget, aPresContext, aElement, aEditorBase);
  }
  return IsObserving(aPresContext, aElement);
}

bool IMEContentObserver::IsObserving(const nsPresContext& aPresContext,
                                     const Element* aElement) const {
  if (GetState() != eState_Observing) {
    return false;
  }
  // If aElement is not a text control, aElement is an editing host or entire
  // the document is editable in the design mode.  Therefore, return false if
  // we're observing an anonymous subtree of a text control.
  const auto* const textControlElement =
      TextControlElement::FromNodeOrNull(aElement);
  if (!textControlElement ||
      !textControlElement->IsSingleLineTextControlOrTextArea()) {
    if (mIsTextControl) {
      return false;
    }
  }
  // If aElement is a text control, return true if we're observing the anonymous
  // subtree of aElement.  Therefore, return false if we're observing with
  // HTMLEditor.
  else if (!mIsTextControl) {
    return false;
  }
  const bool hasEditContext =
      aElement && aElement->HasFlag(ELEMENT_HAS_EDIT_CONTEXT);
  if (hasEditContext != mIsForEditContext) {
    // For EditContext, we don't observe DOM mutations. So we should
    // return false here if aElement has an EditContext and we don't,
    // or vice versa.
    return false;
  }
  return IsObservingElement(aPresContext, aElement);
}

bool IMEContentObserver::IsBeingInitializedFor(
    const nsPresContext& aPresContext, const Element* aElement,
    const EditorBase& aEditorBase) const {
  return GetState() == eState_Initializing && mEditorBase == &aEditorBase &&
         IsObservingElement(aPresContext, aElement);
}

bool IMEContentObserver::IsObserving(
    const TextComposition& aTextComposition) const {
  if (GetState() != eState_Observing) {
    return false;
  }
  nsPresContext* const presContext = aTextComposition.GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return false;
  }
  if (presContext != GetPresContext()) {
    return false;  // observing different document
  }
  auto* const elementHavingComposition =
      Element::FromNodeOrNull(aTextComposition.GetEventTargetNode());
  bool isObserving = IsObservingElement(*presContext, elementHavingComposition);
#ifdef DEBUG
  if (isObserving) {
    if (mIsTextControl) {
      MOZ_ASSERT(elementHavingComposition);
      MOZ_ASSERT(elementHavingComposition->IsTextControlElement(),
                 "Should've never started to observe non-text-control element");
      // XXX Our fake focus move has not been implemented properly. So, the
      // following assertions may fail, but I don't like to make the failures
      // cause crash even in debug builds because it may block developers to
      // debug web-compat issues.  On the other hand, it'd be nice if we can
      // detect the bug with automated tests.  Therefore, the following
      // assertions are NS_ASSERTION.
      NS_ASSERTION(static_cast<TextControlElement*>(elementHavingComposition)
                       ->IsSingleLineTextControlOrTextArea(),
                   "Should've stopped observing when the type is changed");
      NS_ASSERTION(!elementHavingComposition->IsInDesignMode(),
                   "Should've stopped observing when the design mode started");
    } else if (elementHavingComposition) {
      NS_ASSERTION(
          !elementHavingComposition->IsTextControlElement() ||
              !static_cast<TextControlElement*>(elementHavingComposition)
                   ->IsSingleLineTextControlOrTextArea(),
          "Should've never started to observe text-control element or "
          "stopped observing it when the type is changed");
    } else {
      MOZ_ASSERT(presContext->GetPresShell());
      MOZ_ASSERT(presContext->GetPresShell()->GetDocument());
      NS_ASSERTION(
          presContext->GetPresShell()->GetDocument()->IsInDesignMode(),
          "Should be observing entire the document only in the design mode");
    }
  }
#endif  // #ifdef DEBUG
  return isObserving;
}

IMEContentObserver::State IMEContentObserver::GetState() const {
  if (!mRootElement || !mRootEditableNodeOrTextControlElement) {
    return eState_NotObserving;  // failed to initialize or finalized.
  }
  if (!mRootElement->IsInComposedDoc()) {
    // the focused editor has already been reframed.
    return eState_StoppedObserving;
  }
  return mIsObserving ? eState_Observing : eState_Initializing;
}

bool IMEContentObserver::IsObservingElement(const nsPresContext& aPresContext,
                                            const Element* aElement) const {
  MOZ_ASSERT_IF(aElement,
                aElement->GetPresContext(
                    Element::PresContextFor::eForComposedDoc) == &aPresContext);

  if (GetPresContext() != &aPresContext) {
    return false;
  }
  // If this is initialized with a TextEditor,
  // mRootEditableNodeOrTextControlElement is a text control element. Therefore,
  // aElement should be aElement.
  if (mIsTextControl) {
    return !aElement->IsInDesignMode() &&
           aElement == mRootEditableNodeOrTextControlElement;
  }
  if (!mRootEditableNodeOrTextControlElement || !mRootElement) [[unlikely]] {
    return false;
  }
  // If mRootElement is not an inclusive descendant of the root editable node,
  // it means that mRootElement was a nested editing host in the focused editing
  // host, but now it's moved outside the editing host. In this case, we should
  // not reuse this instance because we need to observe the focused editing
  // host, but we have observed the nested editing host which is now not
  // focused.
  if (!mRootElement->IsInclusiveDescendantOf(
          mRootEditableNodeOrTextControlElement)) [[unlikely]] {
    return false;
  }
  // If design mode state has been changed, IMEContentObserver shouldn't be
  // reused because we handle differently between contenteditable and design
  // mode.
  if (IsForDesignMode()) {
    // If this was initialized for the design mode,
    // mRootEditableNodeOrTextControlElement is the document node. If the
    // document is not in the design mode, this instance shouldn't be reused for
    // contenteditable.
    if (!mRootEditableNodeOrTextControlElement->IsInDesignMode()) {
      return false;
    }
    // In the design mode, GetMostDistantInclusiveEditableAncestorNode() returns
    // the document so that we need to check whether mRootElement is the same.
    return mRootElement && (!aElement || aElement->IsInDesignMode()) &&
           mRootElement == ComputeRootElement(aPresContext.GetPresShell());
  }

  // If this is initialized with an HTMLEditor,
  // mRootEditableNodeOrTextControlElement is an editing host when this is
  // initialized.  However, its ancestor may become editable.  Therefore, we
  // need to check whether it's still an editing host.
  return mRootEditableNodeOrTextControlElement ==
         IMEContentObserver::GetMostDistantInclusiveEditableAncestorNode(
             aPresContext, aElement);
}

// static
nsINode* IMEContentObserver::GetMostDistantInclusiveEditableAncestorNode(
    const nsPresContext& aPresContext, const Element* aElement) {
  if (aElement) {
    // If the focused content is in design mode, return is composed document
    // because aElement may be in UA widget shadow tree.
    if (aElement->IsInDesignMode()) {
      return aElement->GetComposedDoc();
    }
    // Otherwise, return the editing host.
    return aElement->GetEditingHost();
  }

  return aPresContext.Document() && aPresContext.Document()->IsInDesignMode()
             ? aPresContext.Document()
             : nullptr;
}

bool IMEContentObserver::IsEditorHandlingEventForComposition() const {
  if (!mWidget) {
    return false;
  }
  RefPtr<TextComposition> composition =
      IMEStateManager::GetTextCompositionFor(mWidget);
  if (!composition) {
    return false;
  }
  return composition->EditorIsHandlingLatestChange();
}

bool IMEContentObserver::IsPreparingTextEditor() const {
  if (!mIsTextControl || !mRootEditableNodeOrTextControlElement) [[unlikely]] {
    return false;
  }
  const auto* textControl =
      TextControlElement::FromNode(mRootEditableNodeOrTextControlElement);
  MOZ_ASSERT(textControl);
  const auto* state = textControl->GetTextControlState();
  if (!state) [[unlikely]] {
    return false;
  }
  return state->IsPreparingEditor();
}

bool IMEContentObserver::IsEditorComposing() const {
  // Note that don't use TextComposition here. The important thing is,
  // whether the editor already started to handle composition because
  // web contents can change selection, text content and/or something from
  // compositionstart event listener which is run before EditorBase handles it.
  if (NS_WARN_IF(!mEditorBase)) {
    return false;
  }
  return mEditorBase->IsIMEComposing();
}

nsresult IMEContentObserver::GetSelectionAndRoot(Selection** aSelection,
                                                 Element** aRootElement) const {
  auto* selection = GetSelection();
  if (!selection || !mRootElement) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_ASSERTION(mRootElement, "uninitialized content observer");
  NS_ADDREF(*aSelection = selection);
  NS_ADDREF(*aRootElement = mRootElement);
  return NS_OK;
}

void IMEContentObserver::OnSelectionChange(Selection& aSelection) {
  if (!mIsObserving || !mWidget) {
    return;
  }
  if (mIsForEditContext) {
    // For EditContext, we notify the IME of selection change only when
    // EditContext.updateSelection() is called, since the DOM selection
    // should not be used by the IME.
    return;
  }

  bool duringComposition = IsEditorComposing();
  bool causedByComposition =
      IsEditorHandlingEventForComposition() ||
      // Treat the selection changes during initializing `TextEditor` because it
      // inherits the composition from the previous one.
      (mIsTextControl && duringComposition && IsPreparingTextEditor());
  bool causedBySelectionEvent = TextComposition::IsHandlingSelectionEvent();
  MaybeNotifyIMEOfSelectionChange(causedByComposition, causedBySelectionEvent,
                                  duringComposition);
}

void IMEContentObserver::ScrollPositionChanged() {
  if (!NeedsPositionChangeNotification()) {
    return;
  }

  // This may occur continuously. So, let's wait for some things to query the
  // result because it'll require to flush the layout and compute the character
  // rects with the expensive paths.
  MaybeNotifyIMEOfPositionChange(Immediately::No);
}

NS_IMETHODIMP
IMEContentObserver::Reflow(DOMHighResTimeStamp aStart,
                           DOMHighResTimeStamp aEnd) {
  if (!NeedsPositionChangeNotification()) {
    return NS_OK;
  }

  MaybeNotifyIMEOfPositionChange(Immediately::Yes);
  return NS_OK;
}

NS_IMETHODIMP
IMEContentObserver::ReflowInterruptible(DOMHighResTimeStamp aStart,
                                        DOMHighResTimeStamp aEnd) {
  if (!NeedsPositionChangeNotification()) {
    return NS_OK;
  }

  MaybeNotifyIMEOfPositionChange(Immediately::Yes);
  return NS_OK;
}

nsresult IMEContentObserver::HandleQueryContentEvent(
    WidgetQueryContentEvent* aEvent) {
  // If the instance has normal selection cache and the query event queries
  // normal selection's range, it should use the cached selection which was
  // sent to the widget.  However, if this instance has already received new
  // selection change notification but hasn't updated the cache yet (i.e.,
  // not sending selection change notification to IME, don't use the cached
  // value.  Note that don't update selection cache here since if you update
  // selection cache here, IMENotificationSender won't notify IME of selection
  // change because it looks like that the selection isn't actually changed.
  const bool isSelectionCacheAvailable =
      mSelectionData.IsInitialized() && !mNeedsToNotifyIMEOfSelectionChange;
  if (isSelectionCacheAvailable && aEvent->mMessage == eQuerySelectedText &&
      aEvent->mInput.mSelectionType == SelectionType::eNormal) {
    aEvent->EmplaceReply();
    if (mSelectionData.HasRange()) {
      aEvent->mReply->mOffsetAndData.emplace(mSelectionData.mOffset,
                                             mSelectionData.String(),
                                             OffsetAndDataFor::SelectedString);
      aEvent->mReply->mReversed = mSelectionData.mReversed;
    }
    aEvent->mReply->mContentsRoot = mRootElement;
    aEvent->mReply->mWritingMode = mSelectionData.GetWritingMode();
    // The selection cache in IMEContentObserver must always have been in
    // an editing host (or an editable anonymous <div> element).  Therefore,
    // we set mIsEditableContent to true here even though it's already been
    // blurred or changed its editable state but the selection cache has not
    // been invalidated yet.
    aEvent->mReply->mIsEditableContent = true;
    MOZ_LOG(sIMECOLog, LogLevel::Debug,
            ("0x%p HandleQueryContentEvent(aEvent={ "
             "mMessage=%s, mReply=%s })",
             this, ToChar(aEvent->mMessage), ToString(aEvent->mReply).c_str()));
    return NS_OK;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p HandleQueryContentEvent(aEvent={ mMessage=%s })", this,
           ToChar(aEvent->mMessage)));

  // If we can make the event's input offset absolute with TextComposition or
  // mSelection, we should set it here for reducing the cost of computing
  // selection start offset.  If ContentEventHandler receives a
  // WidgetQueryContentEvent whose input offset is relative to insertion point,
  // it computes current selection start offset (this may be expensive) and
  // make the offset absolute value itself.
  // Note that calling MakeOffsetAbsolute() makes the event a query event with
  // absolute offset.  So, ContentEventHandler doesn't pay any additional cost
  // after calling MakeOffsetAbsolute() here.
  if (aEvent->mInput.mRelativeToInsertionPoint &&
      aEvent->mInput.IsValidEventMessage(aEvent->mMessage)) {
    RefPtr<TextComposition> composition =
        IMEStateManager::GetTextCompositionFor(aEvent->mWidget);
    if (composition) {
      uint32_t compositionStart = composition->NativeOffsetOfStartComposition();
      if (NS_WARN_IF(!aEvent->mInput.MakeOffsetAbsolute(compositionStart))) {
        return NS_ERROR_FAILURE;
      }
    } else if (isSelectionCacheAvailable && mSelectionData.HasRange()) {
      const uint32_t selectionStart = mSelectionData.mOffset;
      if (NS_WARN_IF(!aEvent->mInput.MakeOffsetAbsolute(selectionStart))) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  AutoRestore<bool> handling(mIsHandlingQueryContentEvent);
  mIsHandlingQueryContentEvent = true;
  ContentEventHandler handler(GetPresContext());
  nsresult rv = handler.HandleQueryContentEvent(aEvent);
  if (NS_WARN_IF(Destroyed())) {
    // If this has already destroyed during querying the content, the query
    // is outdated even if it's succeeded.  So, make the query fail.
    aEvent->mReply.reset();
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   HandleQueryContentEvent(), WARNING, "
             "IMEContentObserver has been destroyed during the query, "
             "making the query fail",
             this));
    return rv;
  }

  if (aEvent->Succeeded() &&
      NS_WARN_IF(aEvent->mReply->mContentsRoot != mRootElement)) {
    // Focus has changed unexpectedly, so make the query fail.
    aEvent->mReply.reset();
  }
  return rv;
}

nsresult IMEContentObserver::MaybeHandleSelectionEvent(
    nsPresContext* aPresContext, WidgetSelectionEvent* aEvent) {
  MOZ_ASSERT(aEvent);
  MOZ_ASSERT(aEvent->mMessage == eSetSelection);
  NS_ASSERTION(!mNeedsToNotifyIMEOfSelectionChange,
               "Selection cache has not been updated yet");

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p MaybeHandleSelectionEvent(aEvent={ "
           "mMessage=%s, mOffset=%u, mLength=%u, mReversed=%s, "
           "mExpandToClusterBoundary=%s }), "
           "mSelectionData=%s",
           this, ToChar(aEvent->mMessage), aEvent->mOffset, aEvent->mLength,
           ToChar(aEvent->mReversed), ToChar(aEvent->mExpandToClusterBoundary),
           ToString(mSelectionData).c_str()));

  // When we have Selection cache, and the caller wants to set same selection
  // range, we shouldn't try to compute same range because it may be impossible
  // if the range boundary is around element boundaries which won't be
  // serialized with line breaks like close tags of inline elements.  In that
  // case, inserting new text at different point may be different from intention
  // of users or web apps which set current selection.
  if (!mNeedsToNotifyIMEOfSelectionChange && mSelectionData.IsInitialized() &&
      mSelectionData.HasRange() &&
      mSelectionData.StartOffset() == aEvent->mOffset &&
      mSelectionData.Length() == aEvent->mLength &&
      mSelectionData.mReversed == aEvent->mReversed) {
    if (RefPtr<Selection> selection = GetSelection()) {
      selection->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION);
    }
    aEvent->mSucceeded = true;
    return NS_OK;
  }

  ContentEventHandler handler(aPresContext);
  return handler.OnSelectionEvent(aEvent);
}

bool IMEContentObserver::OnMouseButtonEvent(nsPresContext& aPresContext,
                                            WidgetMouseEvent& aMouseEvent) {
  if (!mIMENotificationRequests ||
      !mIMENotificationRequests->contains(
          IMENotificationRequest::MouseEventOnChar)) {
    return false;
  }
  if (!aMouseEvent.IsTrusted() || aMouseEvent.DefaultPrevented() ||
      !aMouseEvent.mWidget) {
    return false;
  }
  // Now, we need to notify only mouse down and mouse up event.
  switch (aMouseEvent.mMessage) {
    case eMouseUp:
    case eMouseDown:
      break;
    default:
      return false;
  }
  if (NS_WARN_IF(!mWidget) || NS_WARN_IF(mWidget->Destroyed())) {
    return false;
  }

  WidgetQueryContentEvent queryCharAtPointEvent(true, eQueryCharacterAtPoint,
                                                aMouseEvent.mWidget);
  queryCharAtPointEvent.mRefPoint = aMouseEvent.mRefPoint;
  ContentEventHandler handler(&aPresContext);
  handler.OnQueryCharacterAtPoint(&queryCharAtPointEvent);
  if (NS_WARN_IF(queryCharAtPointEvent.Failed()) ||
      queryCharAtPointEvent.DidNotFindChar()) {
    return false;
  }

  // The widget might be destroyed during querying the content since it
  // causes flushing layout.
  if (!mWidget || NS_WARN_IF(mWidget->Destroyed())) {
    return false;
  }

  // The result character rect is relative to the top level widget.
  // We should notify it with offset in the widget.
  nsIWidget* topLevelWidget = mWidget->GetTopLevelWidget();
  if (topLevelWidget && topLevelWidget != mWidget) {
    queryCharAtPointEvent.mReply->mRect.MoveBy(
        topLevelWidget->WidgetToScreenOffset() -
        mWidget->WidgetToScreenOffset());
  }
  // The refPt is relative to its widget.
  // We should notify it with offset in the widget.
  if (aMouseEvent.mWidget != mWidget) {
    queryCharAtPointEvent.mRefPoint +=
        aMouseEvent.mWidget->WidgetToScreenOffset() -
        mWidget->WidgetToScreenOffset();
  }

  IMENotification notification(NOTIFY_IME_OF_MOUSE_BUTTON_EVENT);
  notification.mMouseButtonEventData.mEventMessage = aMouseEvent.mMessage;
  notification.mMouseButtonEventData.mOffset =
      queryCharAtPointEvent.mReply->StartOffset();
  notification.mMouseButtonEventData.mCursorPos =
      queryCharAtPointEvent.mRefPoint;
  notification.mMouseButtonEventData.mCharRect =
      queryCharAtPointEvent.mReply->mRect;
  notification.mMouseButtonEventData.mButton = aMouseEvent.mButton;
  notification.mMouseButtonEventData.mButtons = aMouseEvent.mButtons;
  notification.mMouseButtonEventData.mModifiers = aMouseEvent.mModifiers;

  nsresult rv = IMEStateManager::NotifyIME(notification, mWidget);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  bool consumed = (rv == NS_SUCCESS_EVENT_CONSUMED);
  if (consumed) {
    aMouseEvent.PreventDefault();
  }
  return consumed;
}

void IMEContentObserver::CharacterDataWillChange(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  if (!NeedsTextChangeNotification() || !aContent->IsText() ||
      !nsContentUtils::IsInSameAnonymousTree(mRootElement, aContent)) {
    // Ignore if it's a comment node, a node in a shadow tree or so, or some
    // other invisible data node.
    return;
  }
  MOZ_ASSERT(mPreCharacterDataChangeLength < 0,
             "CharacterDataChanged() should've reset "
             "mPreCharacterDataChangeLength");
  mEndOfAddedTextCache.Clear(__FUNCTION__);
  mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);

  // Although we don't assume this change occurs while this is storing
  // the range of added consecutive nodes, if it actually happens, we need to
  // flush them since this change may occur before or in the range.  So, it's
  // safe to flush pending computation of mTextChangeData before handling this.
  if (mAddedContentCache.HasCache()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }

  mPreCharacterDataChangeLength = ContentEventHandler::GetNativeTextLength(
      *aContent->AsText(), aInfo.mChangeStart, aInfo.mChangeEnd);
  MOZ_ASSERT(
      mPreCharacterDataChangeLength >= aInfo.mChangeEnd - aInfo.mChangeStart,
      "The computed length must be same as or larger than XP length");
}

void IMEContentObserver::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  if (!aContent->IsText() ||
      !nsContentUtils::IsInSameAnonymousTree(mRootElement, aContent)) {
    // Ignore if it's a comment node, a node in a shadow tree or so, or some
    // other invisible data node.
    return;
  }

  // Let TextComposition have a change to update composition string range in
  // the text node if the change is caused by the web apps.
  if (mWidget && !IsEditorHandlingEventForComposition()) {
    if (RefPtr<TextComposition> composition =
            IMEStateManager::GetTextCompositionFor(mWidget)) {
      composition->OnCharacterDataChanged(*aContent->AsText(), aInfo);
    }
  }

  if (!NeedsTextChangeNotification()) {
    return;
  }

  if (mAddedContentCache.HasCache()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }
  mEndOfAddedTextCache.Clear(__FUNCTION__);
  mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
  MOZ_ASSERT(
      !mAddedContentCache.HasCache(),
      "The stored range should be flushed before actually the data is changed");

  int64_t removedLength = mPreCharacterDataChangeLength;
  mPreCharacterDataChangeLength = -1;

  MOZ_ASSERT(removedLength >= 0,
             "mPreCharacterDataChangeLength should've been set by "
             "CharacterDataWillChange()");

  uint32_t offset = 0;
  if (mIsTextControl) {
    // If we're observing a text control, mRootElement is the anonymous <div>
    // element which has only one text node and/or invisible <br> element.
    // TextEditor assumes this structure when it handles editing commands.
    // Therefore, it's safe to assume same things here.
    MOZ_ASSERT(mRootElement->GetFirstChild() == aContent);
    if (aInfo.mChangeStart) {
      offset = ContentEventHandler::GetNativeTextLength(*aContent->AsText(), 0,
                                                        aInfo.mChangeStart);
    }
  } else {
    Result<uint32_t, nsresult> offsetOrError =
        ContentEventHandler::GetFlatTextLengthInRange(
            RawNodePosition::BeforeFirstContentOf(*mRootElement),
            RawNodePosition(aContent, aInfo.mChangeStart), mRootElement);
    if (NS_WARN_IF(offsetOrError.isErr())) {
      return;
    }
    offset = offsetOrError.unwrap();
  }

  uint32_t newLength = ContentEventHandler::GetNativeTextLength(
      *aContent->AsText(), aInfo.mChangeStart,
      aInfo.mChangeStart + aInfo.mReplaceLength);

  uint32_t oldEnd = offset + static_cast<uint32_t>(removedLength);
  uint32_t newEnd = offset + newLength;

  TextChangeData data(offset, oldEnd, newEnd,
                      IsEditorHandlingEventForComposition(),
                      IsEditorComposing());
  MaybeNotifyIMEOfTextChange(data);
}

void IMEContentObserver::EditContextTextChanged(uint32_t aRangeStart,
                                                uint32_t aRangeEnd,
                                                const nsAString& aText) {
  TextChangeData data(aRangeStart, aRangeEnd, aRangeStart + aText.Length(),
                      IsEditorHandlingEventForComposition(),
                      IsEditorComposing());
  MaybeNotifyIMEOfTextChange(data);
}

void IMEContentObserver::EditContextSelectionChanged() {
  bool causedByComposition = IsEditorHandlingEventForComposition();
  bool causedBySelectionEvent = TextComposition::IsHandlingSelectionEvent();
  bool duringComposition = IsEditorComposing();
  MaybeNotifyIMEOfSelectionChange(causedByComposition, causedBySelectionEvent,
                                  duringComposition);
}

void IMEContentObserver::EditContextPositionChanged() {
  MaybeNotifyIMEOfPositionChange(Immediately::No);
}

void IMEContentObserver::ContentAdded(nsINode* aContainer,
                                      nsIContent* aFirstContent,
                                      nsIContent* aLastContent) {
  if (!NeedsTextChangeNotification() ||
      !nsContentUtils::IsInSameAnonymousTree(mRootElement, aFirstContent)) {
    return;
  }

  // We can skip everything when a padding <br> element is added since its text
  // length is 0.
  if (aFirstContent == aLastContent) {
    if (const auto* brElement = HTMLBRElement::FromNode(aFirstContent)) {
      if (MOZ_LIKELY(!brElement->HasChildNodes()) &&
          (brElement->IsPaddingForEmptyEditor() ||
           brElement->IsPaddingForEmptyLastLine())) {
        return;
      }
    }
  }

  MOZ_ASSERT(IsInDocumentChange());
  MOZ_ASSERT_IF(aFirstContent, aFirstContent->GetParentNode() == aContainer);
  MOZ_ASSERT_IF(aLastContent, aLastContent->GetParentNode() == aContainer);

  // While a document change, new nodes should be added consecutively in a
  // container node.  Therefore, we can cache the first added node and the last
  // added node until ending the document change at least.  Then, we can avoid
  // to compute first added node offset in the flattened text repeatedly.
  bool needToCache = true;
  if (mAddedContentCache.HasCache()) {
    MOZ_DIAGNOSTIC_ASSERT(aFirstContent->GetParentNode() ==
                          aLastContent->GetParentNode());
    if (mAddedContentCache.IsInRange(*aFirstContent, mRootElement)) {
      // The new content nodes are in the range, we can include their text
      // length when we flush the cached range later.  Therefore, we need to
      // do nothing in this case.
      needToCache = false;
      MOZ_LOG(sCacheLog, LogLevel::Info,
              ("ContentAdded: mAddedContentCache already caches the give "
               "content nodes"));
      MOZ_ASSERT(mAddedContentCache.IsInRange(*aLastContent, mRootElement));
    }
    // When new nodes are inserted in a different container, let's flush the
    // preceding content first.  Then, we should restart to cache the new
    // inserted nodes.
    else if (!mAddedContentCache.CanMergeWith(*aFirstContent, *aLastContent,
                                              mRootElement)) {
      MOZ_LOG(sCacheLog, LogLevel::Info,
              ("ContentAdded: mAddedContentCache was cached not in current "
               "document change and new content nodes cannot be merged"));
      mEndOfAddedTextCache.Clear(__FUNCTION__);
      mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
      OffsetAndLengthAdjustments differences;
      Result<std::pair<uint32_t, uint32_t>, nsresult> offsetAndLength =
          mAddedContentCache.ComputeFlatTextRangeBeforeInsertingNewContent(
              *aFirstContent, *aLastContent, mRootElement, differences);
      if (NS_WARN_IF(offsetAndLength.isErr())) {
        MOZ_LOG(sCacheLog, LogLevel::Error,
                ("ContentAdded: "
                 "AddedContentCache::"
                 "ComputeFlatTextRangeExcludingInsertingNewContent() failed"));
        mAddedContentCache.Clear(__FUNCTION__);
        return;
      }
      NotifyIMEOfCachedConsecutiveNewNodes(
          __FUNCTION__, Some(offsetAndLength.inspect().first),
          Some(offsetAndLength.inspect().second), differences);
      mAddedContentCache.Clear(__FUNCTION__);
    }
  }

  mEndOfAddedTextCache.ContentAdded(__FUNCTION__, *aFirstContent, *aLastContent,
                                    Nothing(), mRootElement);
  mStartOfRemovingTextRangeCache.ContentAdded(
      __FUNCTION__, *aFirstContent, *aLastContent, Nothing(), mRootElement);

  if (!needToCache) {
    return;
  }

  // Okay, now, we can start to cache new nodes or merge the range of new
  // nodes with the cached range.
  if (!mAddedContentCache.TryToCache(*aFirstContent, *aLastContent,
                                     mRootElement)) {
    // Flush the old range first.
    MOZ_LOG(sCacheLog, LogLevel::Info,
            ("ContentAdded: called during a document change flushed "
             "previous added nodes (aFirstContent=%s, aLastContent=%s)",
             ToString(RefPtr<nsINode>(aFirstContent)).c_str(),
             ToString(RefPtr<nsINode>(aLastContent)).c_str()));
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
    MOZ_ASSERT(!mAddedContentCache.HasCache());
    MOZ_ALWAYS_TRUE(mAddedContentCache.TryToCache(*aFirstContent, *aLastContent,
                                                  mRootElement));
  }
}

void IMEContentObserver::NotifyIMEOfCachedConsecutiveNewNodes(
    const char* aCallerName,
    const Maybe<uint32_t>& aOffsetOfFirstContent /* = Nothing() */,
    const Maybe<uint32_t>& aLengthOfContentNNodes /* = Nothing() */,
    const OffsetAndLengthAdjustments& aAdjustments /* = Nothing() */) {
  MOZ_ASSERT(mAddedContentCache.HasCache());

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p "
           "IMEContentObserver::NotifyIMEOfCachedConsecutiveNewNodes(), "
           "flushing stored consecutive nodes",
           this));
  MOZ_LOG(
      sCacheLog, LogLevel::Info,
      ("NotifyIMEOfCachedConsecutiveNewNodes: called by %s "
       "(mAddedContentCache=%s)",
       ShortenFunctionName(aCallerName), ToString(mAddedContentCache).c_str()));

  // If 2 <div> elements are inserted into the DOM, we wan't the text length
  // from start of the first <div> (including line break caused by its open
  // tag) to end of the second <div>.  I.e., we want to compute:
  // ...{<div>.....</div><div>......</div>}...
  //    ^  ^               ^              ^
  //    |  mFirst          |              |
  //    |                  mLast          |
  //    offset                            (offset + length)
  Maybe<uint32_t> offset =
      aOffsetOfFirstContent.isSome()
          ? aOffsetOfFirstContent
          : mEndOfAddedTextCache.GetFlatTextLengthBeforeContent(
                *mAddedContentCache.mFirst, mRootElement);
  if (offset.isNothing()) {
    Result<uint32_t, nsresult> textLengthBeforeFirstContentOrError =
        FlatTextCache::ComputeTextLengthBeforeContent(
            *mAddedContentCache.mFirst, mRootElement);
    if (NS_WARN_IF(textLengthBeforeFirstContentOrError.isErr())) {
      mEndOfAddedTextCache.Clear(__FUNCTION__);
      mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
      MOZ_LOG(
          sCacheLog, LogLevel::Error,
          ("NotifyContentAdded: failed to compute text length before mFirst"));
      mAddedContentCache.Clear(__FUNCTION__);
      return;
    }
    offset = Some(textLengthBeforeFirstContentOrError.unwrap());
  }
  Maybe<uint32_t> length = aLengthOfContentNNodes;
  if (aLengthOfContentNNodes.isNothing()) {
    Result<uint32_t, nsresult> addingLengthOrError =
        FlatTextCache::ComputeTextLengthStartOfContentToEndOfContent(
            *mAddedContentCache.mFirst, *mAddedContentCache.mLast,
            mRootElement);
    if (NS_WARN_IF(addingLengthOrError.isErr())) {
      mEndOfAddedTextCache.Clear(__FUNCTION__);
      mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
      MOZ_LOG(sCacheLog, LogLevel::Error,
              ("NotifyContentAdded: failed to compute text length of added"));
      mAddedContentCache.Clear(__FUNCTION__);
      return;
    }
    length = Some(addingLengthOrError.inspect());
  }

  // If multiple lines are being inserted in an HTML editor, next call of
  // NotifyContentAdded() is for adding next node.  Therefore, caching the text
  // length can skip to compute the text length before the adding node and
  // before of it.
  mEndOfAddedTextCache.CacheFlatTextLengthBeforeEndOfContent(
      __FUNCTION__, *mAddedContentCache.mLast,
      aAdjustments.AdjustedEndOffset(*offset + *length), mRootElement);
  mStartOfRemovingTextRangeCache.ContentAdded(
      __FUNCTION__, *mAddedContentCache.mFirst, *mAddedContentCache.mLast,
      Some(aAdjustments.AdjustedEndOffset(*offset + *length)), mRootElement);

  mAddedContentCache.Clear(__FUNCTION__);

  if (*length == 0u) {
    return;
  }

  TextChangeData data(*offset, *offset, *offset + *length,
                      IsEditorHandlingEventForComposition(),
                      IsEditorComposing());
  MaybeNotifyIMEOfTextChange(data);
}

void IMEContentObserver::ContentAppended(nsIContent* aFirstNewContent,
                                         const ContentAppendInfo&) {
  nsIContent* parent = aFirstNewContent->GetParent();
  MOZ_ASSERT(parent);
  ContentAdded(parent, aFirstNewContent, parent->GetLastChild());
}

void IMEContentObserver::ContentInserted(nsIContent* aChild,
                                         const ContentInsertInfo&) {
  MOZ_ASSERT(aChild);
  ContentAdded(aChild->GetParentNode(), aChild, aChild);
}

void IMEContentObserver::ContentWillBeRemoved(nsIContent* aChild,
                                              const ContentRemoveInfo&) {
  if (!NeedsTextChangeNotification() ||
      !nsContentUtils::IsInSameAnonymousTree(mRootElement, aChild)) {
    return;
  }

  // We can skip everything when padding <br> element is removed since its text
  // length is 0.
  if (const auto* brElement = HTMLBRElement::FromNode(aChild)) {
    if (MOZ_LIKELY(!brElement->HasChildNodes()) &&
        (brElement->IsPaddingForEmptyEditor() ||
         brElement->IsPaddingForEmptyLastLine())) {
      return;
    }
  }

  const Result<uint32_t, nsresult> textLengthOrError =
      FlatTextCache::ComputeTextLengthOfContent(*aChild, mRootElement);
  if (NS_WARN_IF(textLengthOrError.isErr())) {
    mEndOfAddedTextCache.Clear(__FUNCTION__);
    mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
    mAddedContentCache.Clear(__FUNCTION__);
    return;
  }

  if (mAddedContentCache.HasCache()) {
    mEndOfAddedTextCache.ContentWillBeRemoved(
        *aChild, textLengthOrError.inspect(), mRootElement);
    mStartOfRemovingTextRangeCache.ContentWillBeRemoved(
        *aChild, textLengthOrError.inspect(), mRootElement);
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
    MOZ_DIAGNOSTIC_ASSERT(!mAddedContentCache.HasCache());
  }

  nsINode* containerNode = aChild->GetParentNode();
  MOZ_ASSERT(containerNode);

  mEndOfAddedTextCache.ContentWillBeRemoved(
      *aChild, textLengthOrError.inspect(), mRootElement);

  Maybe<uint32_t> offset =
      mStartOfRemovingTextRangeCache.GetFlatTextLengthBeforeContent(
          *aChild, mRootElement);
  nsIContent* const prevSibling = aChild->GetPreviousSibling();
  if (offset.isSome()) {
    // Update the cache because next remove may be the previous or the next
    // sibling removal.  So, caching offset of currently removing content node
    // makes us skip computing offset of next removal.
    if (prevSibling) {
      mStartOfRemovingTextRangeCache.CacheFlatTextLengthBeforeEndOfContent(
          __FUNCTION__, *prevSibling, *offset, mRootElement);
    } else {
      mStartOfRemovingTextRangeCache.CacheFlatTextLengthBeforeFirstContent(
          __FUNCTION__, *containerNode, *offset, mRootElement);
    }
  } else {
    if (prevSibling) {
      // When we compute preceding text length of the removing content node, we
      // cannot make the range cross the removing node boundary because
      // containerNode->ComputeIndexOf(aChild) returns Nothing so that
      // ContentEventHandler fails to compute the length.  Therefore, if a <div>
      // is being removed, we want to compute the length of `...}<div>`.
      if (NS_WARN_IF(
              NS_FAILED(mStartOfRemovingTextRangeCache
                            .ComputeAndCacheFlatTextLengthBeforeEndOfContent(
                                __FUNCTION__, *prevSibling, mRootElement)))) {
        return;
      }
    } else {
      // At removing a child node of containerNode, we need the line break
      // caused by open tag of containerNode.
      if (NS_WARN_IF(
              NS_FAILED(mStartOfRemovingTextRangeCache
                            .ComputeAndCacheFlatTextLengthBeforeFirstContent(
                                __FUNCTION__, *containerNode, mRootElement)))) {
        return;
      }
    }
    offset = Some(mStartOfRemovingTextRangeCache.GetFlatTextLength());
  }

  // We do not need a text change notification since removing aChild does not
  // change flattened text and no pending added length.
  if (textLengthOrError.inspect() == 0u) {
    return;
  }

  TextChangeData data(*offset, *offset + textLengthOrError.inspect(), *offset,
                      IsEditorHandlingEventForComposition(),
                      IsEditorComposing());
  MaybeNotifyIMEOfTextChange(data);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void IMEContentObserver::ParentChainChanged(
    nsIContent* aContent) {
  MOZ_ASSERT(aContent);
  // When the observing element itself is directly removed from the document
  // without a focus move, i.e., it's the root of the removed document fragment
  // and the editor was handling the design mode, we have already stopped
  // observing the element because IMEStateManager::OnRemoveContent() should
  // have already been called for it and the instance which was observing the
  // node has already been destroyed.  Therefore, this is called only when
  // this is observing the <body> in the design mode and it's disconnected from
  // the tree by an <html> element removal.  Even in this case, IMEStateManager
  // never gets a focus change notification, but we need to notify IME of focus
  // change because we cannot interact with IME anymore due to no editable
  // content.  Therefore, this method notifies IMEStateManager of the
  // disconnection of the observing node to emulate a blur from the editable
  // content.
  MOZ_ASSERT(mIsObserving);
  OwningNonNull<IMEContentObserver> observer(*this);
  IMEStateManager::OnParentChainChangedOfObservingElement(observer, *aContent);
}

void IMEContentObserver::BeginDocumentUpdate() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug, ("0x%p BeginDocumentUpdate()", this));
}

void IMEContentObserver::EndDocumentUpdate() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug, ("0x%p EndDocumentUpdate()", this));

  if (mAddedContentCache.HasCache() && !EditorIsHandlingEditSubAction()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }
}

void IMEContentObserver::SuppressNotifyingIME() {
  mSuppressNotifications++;

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p SuppressNotifyingIME(), mSuppressNotifications=%u", this,
           mSuppressNotifications));
}

void IMEContentObserver::UnsuppressNotifyingIME() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p UnsuppressNotifyingIME(), mSuppressNotifications=%u", this,
           mSuppressNotifications));
  if (!mSuppressNotifications || --mSuppressNotifications) {
    return;
  }
  FlushMergeableNotifications();
}

void IMEContentObserver::OnEditActionHandled() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug, ("0x%p OnEditActionHandled()", this));

  if (mAddedContentCache.HasCache()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }
  mEndOfAddedTextCache.Clear(__FUNCTION__);
  mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
  FlushMergeableNotifications();
}

void IMEContentObserver::BeforeEditAction() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug, ("0x%p BeforeEditAction()", this));

  if (mAddedContentCache.HasCache()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }
  mEndOfAddedTextCache.Clear(__FUNCTION__);
  mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
}

void IMEContentObserver::CancelEditAction() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug, ("0x%p CancelEditAction()", this));

  if (mAddedContentCache.HasCache()) {
    NotifyIMEOfCachedConsecutiveNewNodes(__FUNCTION__);
  }
  mEndOfAddedTextCache.Clear(__FUNCTION__);
  mStartOfRemovingTextRangeCache.Clear(__FUNCTION__);
  FlushMergeableNotifications();
}

bool IMEContentObserver::EditorIsHandlingEditSubAction() const {
  return mEditorBase && mEditorBase->IsInEditSubAction();
}

void IMEContentObserver::PostFocusSetNotification() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p PostFocusSetNotification()", this));

  mNeedsToNotifyIMEOfFocusSet = true;
}

void IMEContentObserver::PostTextChangeNotification() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p PostTextChangeNotification(mTextChangeData=%s)", this,
           ToString(mTextChangeData).c_str()));

  MOZ_ASSERT(mTextChangeData.IsValid(),
             "mTextChangeData must have text change data");
  mNeedsToNotifyIMEOfTextChange = true;
  // Even if the observer hasn't received selection change, selection in the
  // flat text may have already been changed.  For example, when previous `<p>`
  // element of another `<p>` element which contains caret is removed by a DOM
  // mutation, selection change event won't be fired, but selection start offset
  // should be decreased by the length of removed `<p>` element.
  // In such case, HandleQueryContentEvent shouldn't use the selection cache
  // anymore.  Therefore, we also need to post selection change notification
  // too.  eQuerySelectedText event may be dispatched at sending a text change
  // notification.
  mNeedsToNotifyIMEOfSelectionChange = true;
}

void IMEContentObserver::PostSelectionChangeNotification() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p PostSelectionChangeNotification(), mSelectionData={ "
           "mCausedByComposition=%s, mCausedBySelectionEvent=%s }",
           this, ToChar(mSelectionData.mCausedByComposition),
           ToChar(mSelectionData.mCausedBySelectionEvent)));

  mNeedsToNotifyIMEOfSelectionChange = true;
}

void IMEContentObserver::MaybeNotifyIMEOfFocusSet() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p MaybeNotifyIMEOfFocusSet()", this));

  PostFocusSetNotification();
  FlushMergeableNotifications();
}

void IMEContentObserver::MaybeNotifyIMEOfTextChange(
    const TextChangeDataBase& aTextChangeData) {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p MaybeNotifyIMEOfTextChange(aTextChangeData=%s)", this,
           ToString(aTextChangeData).c_str()));

  if (mEditorBase && mEditorBase->IsTextEditor()) {
    MOZ_DIAGNOSTIC_ASSERT(static_cast<int64_t>(mTextControlValueLength) +
                              aTextChangeData.Difference() >=
                          0);
    mTextControlValueLength += aTextChangeData.Difference();
  }

  mTextChangeData += aTextChangeData;
  PostTextChangeNotification();
  FlushMergeableNotifications();
}

void IMEContentObserver::CancelNotifyingIMEOfTextChange() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p CancelNotifyingIMEOfTextChange()", this));
  mTextChangeData.Clear();
  mNeedsToNotifyIMEOfTextChange = false;
}

void IMEContentObserver::MaybeNotifyIMEOfSelectionChange(
    bool aCausedByComposition, bool aCausedBySelectionEvent,
    bool aOccurredDuringComposition) {
  MOZ_LOG_FMT(sIMECOLog, LogLevel::Info,
              "{} MaybeNotifyIMEOfSelectionChange(aCausedByComposition={}, "
              "aCausedBySelectionEvent={}, aOccurredDuringComposition={})",
              static_cast<void*>(this), TrueOrFalse(aCausedByComposition),
              TrueOrFalse(aCausedBySelectionEvent),
              TrueOrFalse(aOccurredDuringComposition));

  mSelectionData.AssignReason(aCausedByComposition, aCausedBySelectionEvent,
                              aOccurredDuringComposition);
  PostSelectionChangeNotification();
  FlushMergeableNotifications();
}

void IMEContentObserver::MaybeNotifyIMEOfPositionChange(
    Immediately aImmediately) {
  MOZ_LOG(sIMECOLog, LogLevel::Verbose,
          ("0x%p MaybeNotifyIMEOfPositionChange()", this));
  // If reflow is caused by ContentEventHandler during PositionChangeEvent
  // sending NOTIFY_IME_OF_POSITION_CHANGE, we don't need to notify IME of it
  // again since ContentEventHandler returns the result including this reflow's
  // result.
  if (mIsHandlingQueryContentEvent &&
      mSendingNotification == NOTIFY_IME_OF_POSITION_CHANGE) {
    MOZ_LOG(sIMECOLog, LogLevel::Verbose,
            ("0x%p   MaybeNotifyIMEOfPositionChange(), ignored since caused by "
             "ContentEventHandler during sending NOTIFY_IME_OF_POSITION_CHANGE",
             this));
    return;
  }
  PostPositionChangeNotification(aImmediately);
  FlushMergeableNotifications();
}

void IMEContentObserver::CancelNotifyingIMEOfPositionChange() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p CancelNotifyIMEOfPositionChange()", this));
  mTicksUntilNotifyIMEOfPositionChange = 0;
}

void IMEContentObserver::MaybeNotifyCompositionEventHandled(
    EventMessage aEventMessage) {
  MOZ_LOG_FMT(sIMECOLog, LogLevel::Info,
              "{} MaybeNotifyCompositionEventHandled(aEventMessage={})",
              static_cast<void*>(this), ToChar(aEventMessage));

  PostCompositionEventHandledNotification();
  FlushMergeableNotifications();
}

bool IMEContentObserver::UpdateSelectionCache(bool aRequireFlush /* = true */) {
  MOZ_ASSERT(IsSafeToNotifyIME());

  mSelectionData.ClearSelectionData();

  // XXX Cannot we cache some information for reducing the cost to compute
  //     selection offset and writing mode?
  WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                 mWidget);
  querySelectedTextEvent.mNeedsToFlushLayout = aRequireFlush;
  ContentEventHandler handler(GetPresContext());
  handler.OnQuerySelectedText(&querySelectedTextEvent);
  if (NS_WARN_IF(querySelectedTextEvent.Failed()) ||
      NS_WARN_IF(querySelectedTextEvent.mReply->mContentsRoot !=
                 mRootElement)) {
    return false;
  }

  mFocusedWidget = querySelectedTextEvent.mReply->mFocusedWidget;
  mSelectionData.Assign(querySelectedTextEvent);

  // WARNING: Don't set the reason of selection change here because it should be
  //          set the reason at sending the notification.

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p UpdateSelectionCache(), mSelectionData=%s", this,
           ToString(mSelectionData).c_str()));

  return true;
}

void IMEContentObserver::PostPositionChangeNotification(
    Immediately aImmediately) {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p PostPositionChangeNotification(aImmediately=%s)", this,
           YesOrNo(static_cast<bool>(aImmediately))));

  // If we need to send the notification immediately, we should do it in the
  // next tick.
  if (aImmediately == Immediately::Yes) {
    mTicksUntilNotifyIMEOfPositionChange = 1u;
    return;
  }

  // Otherwise, we should send the notification after some ticks. To avoid
  // delaying IME UI updates too much, apply the delay only when there is no
  // pending position change notification.
  if (!mTicksUntilNotifyIMEOfPositionChange) {
    mTicksUntilNotifyIMEOfPositionChange = static_cast<
        uint8_t>(std::min<uint32_t>(
        StaticPrefs::
            intl_ime_content_observer_notifications_position_change_ticks_after_scrolling(),
        UINT8_MAX));
  }
}

void IMEContentObserver::PostCompositionEventHandledNotification() {
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p PostCompositionEventHandledNotification()", this));

  mNeedsToNotifyIMEOfCompositionEventHandled = true;
}

bool IMEContentObserver::IsReflowLocked() const {
  nsPresContext* presContext = GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return false;
  }
  PresShell* presShell = presContext->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return false;
  }
  // During reflow, we shouldn't notify IME because IME may query content
  // synchronously.  Then, it causes ContentEventHandler will try to flush
  // pending notifications during reflow.
  return presShell->IsReflowLocked();
}

bool IMEContentObserver::IsSafeToNotifyIME() const {
  // If this is already detached from the widget, this doesn't need to notify
  // anything.
  if (!mWidget) {
    MOZ_LOG(sIMECOLog, LogLevel::Debug,
            ("0x%p   IsSafeToNotifyIME(), it's not safe because of no widget",
             this));
    return false;
  }

  // Don't notify IME of anything if it's not good time to do it.
  if (mSuppressNotifications) {
    MOZ_LOG(sIMECOLog, LogLevel::Debug,
            ("0x%p   IsSafeToNotifyIME(), it's not safe because of no widget",
             this));
    return false;
  }

  if (!mESM || NS_WARN_IF(!GetPresContext())) {
    MOZ_LOG(sIMECOLog, LogLevel::Debug,
            ("0x%p   IsSafeToNotifyIME(), it's not safe because of no "
             "EventStateManager and/or PresContext",
             this));
    return false;
  }

  // If it's in reflow, we should wait to finish the reflow.
  // FYI: This should be called again from Reflow() or ReflowInterruptible().
  if (IsReflowLocked()) {
    MOZ_LOG(
        sIMECOLog, LogLevel::Debug,
        ("0x%p   IsSafeToNotifyIME(), it's not safe because of reflow locked",
         this));
    return false;
  }

  // If we're in handling an edit action, this method will be called later.
  if (EditorIsHandlingEditSubAction()) {
    MOZ_LOG(sIMECOLog, LogLevel::Debug,
            ("0x%p   IsSafeToNotifyIME(), it's not safe because of focused "
             "editor handling somethings",
             this));
    return false;
  }

  return true;
}

void IMEContentObserver::FlushMergeableNotifications() {
  if (!IsSafeToNotifyIME()) {
    // So, if this is already called, this should do nothing.
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   FlushMergeableNotifications(), Warning, do nothing due to "
             "unsafe to notify IME",
             this));
    return;
  }

  // Notifying something may cause nested call of this method.  For example,
  // when somebody notified one of the notifications may dispatch query content
  // event. Then, it causes flushing layout which may cause another layout
  // change notification.

  if (mQueuedSender) {
    // So, if this is already called, this should do nothing.
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   FlushMergeableNotifications(), Warning, do nothing due to "
             "already flushing pending notifications",
             this));
    return;
  }

  // If text change notification and/or position change notification becomes
  // unnecessary, let's cancel them.
  if (mNeedsToNotifyIMEOfTextChange && !NeedsTextChangeNotification()) {
    CancelNotifyingIMEOfTextChange();
  }
  if (mTicksUntilNotifyIMEOfPositionChange &&
      !NeedsPositionChangeNotification()) {
    CancelNotifyingIMEOfPositionChange();
  }

  if (!NeedsToNotifyIMEOfSomething()) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   FlushMergeableNotifications(), Warning, due to no pending "
             "notifications",
             this));
    return;
  }

  // NOTE: Reset each pending flag because sending notification may cause
  //       another change.

  MOZ_LOG(
      sIMECOLog, LogLevel::Info,
      ("0x%p FlushMergeableNotifications(), creating IMENotificationSender...",
       this));

  // If contents in selection range is modified, the selection range still
  // has removed node from the tree.  In such case, ContentIterator won't
  // work well.  Therefore, we shouldn't use AddScriptRunner() here since
  // it may kick runnable event immediately after DOM tree is changed but
  // the selection range isn't modified yet.
  mQueuedSender = new IMENotificationSender(this);
  mQueuedSender->Dispatch(mDocShell);
  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   FlushMergeableNotifications(), finished", this));
}

void IMEContentObserver::TryToFlushPendingNotifications(bool aAllowAsync) {
  // If a sender instance is sending notifications, we shouldn't try to create
  // a new sender again because the sender will recreate by itself if there are
  // new pending notifications.
  if (mSendingNotification != NOTIFY_IME_OF_NOTHING) {
    return;
  }

  // When the caller allows to put off notifying IME, we can wait the next
  // call of this method or to run the queued sender.
  if (mQueuedSender && XRE_IsContentProcess() && aAllowAsync) {
    return;
  }

  if (!mQueuedSender) {
    // If it was not safe to dispatch notifications when the pending
    // notifications are posted, this may not have IMENotificationSender
    // instance because it couldn't dispatch it, e.g., when an edit sub-action
    // is being handled in the editor, we shouldn't do it even if it's safe to
    // run script.  Therefore, we need to create the sender instance here in the
    // case.
    if (!NeedsToNotifyIMEOfSomething()) {
      return;
    }
    mQueuedSender = new IMENotificationSender(this);
  }

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p TryToFlushPendingNotifications(), performing queued "
           "IMENotificationSender forcibly",
           this));
  RefPtr<IMENotificationSender> queuedSender = mQueuedSender;
  queuedSender->Run();
}

/******************************************************************************
 * mozilla::IMEContentObserver::AChangeEvent
 ******************************************************************************/

bool IMEContentObserver::AChangeEvent::CanNotifyIME(
    ChangeEventType aChangeEventType) const {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (NS_WARN_IF(!observer)) {
    return false;
  }

  const LogLevel debugOrVerbose =
      aChangeEventType == ChangeEventType::eChangeEventType_Position
          ? LogLevel::Verbose
          : LogLevel::Debug;

  if (aChangeEventType == eChangeEventType_CompositionEventHandled) {
    if (observer->mWidget) {
      return true;
    }
    MOZ_LOG(sIMECOLog, debugOrVerbose,
            ("0x%p   AChangeEvent::CanNotifyIME(), Cannot notify IME of "
             "composition event handled because of no widget",
             this));
    return false;
  }
  State state = observer->GetState();
  // If it's not initialized, we should do nothing.
  if (state == eState_NotObserving) {
    MOZ_LOG(sIMECOLog, debugOrVerbose,
            ("0x%p   AChangeEvent::CanNotifyIME(), Cannot notify IME because "
             "of not observing",
             this));
    return false;
  }
  // If setting focus, just check the state.
  if (aChangeEventType == eChangeEventType_Focus) {
    if (!observer->mIMEHasFocus) {
      return true;
    }
    MOZ_LOG(sIMECOLog, debugOrVerbose,
            ("0x%p   AChangeEvent::CanNotifyIME(), Cannot notify IME of focus "
             "change because of already focused",
             this));
    NS_WARNING("IME already has focus");
    return false;
  }
  // If we've not notified IME of focus yet, we shouldn't notify anything.
  if (!observer->mIMEHasFocus) {
    MOZ_LOG(sIMECOLog, debugOrVerbose,
            ("0x%p   AChangeEvent::CanNotifyIME(), Cannot notify IME because "
             "of not focused",
             this));
    return false;
  }

  // If IME has focus, IMEContentObserver must hold the widget.
  MOZ_ASSERT(observer->mWidget);

  return true;
}

bool IMEContentObserver::AChangeEvent::IsSafeToNotifyIME(
    ChangeEventType aChangeEventType) const {
  const LogLevel warningOrVerbose =
      aChangeEventType == ChangeEventType::eChangeEventType_Position
          ? LogLevel::Verbose
          : LogLevel::Warning;

  if (NS_WARN_IF(!nsContentUtils::IsSafeToRunScript())) {
    MOZ_LOG(sIMECOLog, warningOrVerbose,
            ("0x%p   AChangeEvent::IsSafeToNotifyIME(), Warning, Cannot notify "
             "IME because of not safe to run script",
             this));
    return false;
  }

  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    MOZ_LOG(sIMECOLog, warningOrVerbose,
            ("0x%p   AChangeEvent::IsSafeToNotifyIME(), Warning, Cannot notify "
             "IME because of no observer",
             this));
    return false;
  }

  // While we're sending a notification, we shouldn't send another notification
  // recursively.
  if (observer->mSendingNotification != NOTIFY_IME_OF_NOTHING) {
    MOZ_LOG(sIMECOLog, warningOrVerbose,
            ("0x%p   AChangeEvent::IsSafeToNotifyIME(), Warning, Cannot notify "
             "IME because of the observer sending another notification",
             this));
    return false;
  }
  State state = observer->GetState();
  if (aChangeEventType == eChangeEventType_Focus) {
    if (NS_WARN_IF(state != eState_Initializing && state != eState_Observing)) {
      MOZ_LOG(sIMECOLog, warningOrVerbose,
              ("0x%p   AChangeEvent::IsSafeToNotifyIME(), Warning, Cannot "
               "notify IME of focus because of not observing",
               this));
      return false;
    }
  } else if (aChangeEventType == eChangeEventType_CompositionEventHandled) {
    // It doesn't need to check the observing status.
  } else if (state != eState_Observing) {
    MOZ_LOG(sIMECOLog, warningOrVerbose,
            ("0x%p   AChangeEvent::IsSafeToNotifyIME(), Warning, Cannot notify "
             "IME because of not observing",
             this));
    return false;
  }
  return observer->IsSafeToNotifyIME();
}

/******************************************************************************
 * mozilla::IMEContentObserver::IMENotificationSender
 ******************************************************************************/

void IMEContentObserver::IMENotificationSender::Dispatch(
    nsIDocShell* aDocShell) {
  if (XRE_IsContentProcess() && aDocShell) {
    if (RefPtr<nsPresContext> presContext = aDocShell->GetPresContext()) {
      if (nsRefreshDriver* refreshDriver = presContext->RefreshDriver()) {
        refreshDriver->AddEarlyRunner(this);
        return;
      }
    }
  }
  NS_DispatchToCurrentThread(this);
}

NS_IMETHODIMP
IMEContentObserver::IMENotificationSender::Run() {
  if (NS_WARN_IF(mIsRunning)) {
    MOZ_LOG(
        sIMECOLog, LogLevel::Error,
        ("0x%p IMENotificationSender::Run(), FAILED, due to called recursively",
         this));
    return NS_OK;
  }

  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return NS_OK;
  }

  AutoRestore<bool> running(mIsRunning);
  mIsRunning = true;

  // This instance was already performed forcibly.
  if (observer->mQueuedSender != this) {
    return NS_OK;
  }

  // NOTE: Reset each pending flag because sending notification may cause
  //       another change.

  if (observer->mNeedsToNotifyIMEOfFocusSet) {
    observer->mNeedsToNotifyIMEOfFocusSet = false;
    SendFocusSet();
    observer->mQueuedSender = nullptr;
    // If it's not safe to notify IME of focus, SendFocusSet() sets
    // mNeedsToNotifyIMEOfFocusSet true again.  For guaranteeing to send the
    // focus notification later,  we should put a new sender into the queue but
    // this case must be rare.  Note that if mIMEContentObserver is already
    // destroyed, mNeedsToNotifyIMEOfFocusSet is never set true again.
    if (observer->mNeedsToNotifyIMEOfFocusSet) {
      MOZ_ASSERT(!observer->mIMEHasFocus);
      MOZ_LOG(sIMECOLog, LogLevel::Debug,
              ("0x%p IMENotificationSender::Run(), posting "
               "IMENotificationSender to current thread",
               this));
      observer->mQueuedSender = new IMENotificationSender(observer);
      observer->mQueuedSender->Dispatch(observer->mDocShell);
      return NS_OK;
    }
    // This is the first notification to IME. So, we don't need to notify
    // anymore since IME starts to query content after it gets focus.
    observer->ClearPendingNotifications();
    return NS_OK;
  }

  const bool allowToNotifyIMEOfPositionChange =
      observer->mNeedsToNotifyIMEOfTextChange ||
      observer->mNeedsToNotifyIMEOfSelectionChange ||
      observer->mNeedsToNotifyIMEOfCompositionEventHandled ||
      observer->mTicksUntilNotifyIMEOfPositionChange == 1;
  if (observer->mNeedsToNotifyIMEOfTextChange) {
    observer->mNeedsToNotifyIMEOfTextChange = false;
    SendTextChange();
  }

  // If a text change notification causes another text change again, we should
  // notify IME of that before sending a selection change notification.
  if (!observer->mNeedsToNotifyIMEOfTextChange) {
    // Be aware, PuppetWidget depends on the order of this. A selection change
    // notification should not be sent before a text change notification because
    // PuppetWidget shouldn't query new text content every selection change.
    if (observer->mNeedsToNotifyIMEOfSelectionChange) {
      observer->mNeedsToNotifyIMEOfSelectionChange = false;
      SendSelectionChange();
    }
  }

  // If a text change notification causes another text change again or a
  // selection change notification causes either a text change or another
  // selection change, we should notify IME of those before sending a position
  // change notification.
  if (observer->mTicksUntilNotifyIMEOfPositionChange &&
      !observer->mNeedsToNotifyIMEOfTextChange &&
      !observer->mNeedsToNotifyIMEOfSelectionChange) {
    observer->mTicksUntilNotifyIMEOfPositionChange--;
    // If text or selection is changed, IME may need to the latest layout
    // information immediately. Thus, we need to send position change
    // notification right now.  Otherwise, we should put it off to stop
    // notifying too many position change notifications during a scroll or a
    // layout change caused by the web app.
    if (allowToNotifyIMEOfPositionChange) {
      observer->mTicksUntilNotifyIMEOfPositionChange = 0;
      SendPositionChange();
    } else {
      // If we need to notify IME of a position change later, we should enqueue
      // this again to save the cost of creating new sender and notify IME of
      // the other things at that time.
      if (observer->mQueuedSender != this) {
        observer->mQueuedSender = new IMENotificationSender(observer);
      }
      observer->mQueuedSender->Dispatch(observer->mDocShell);
      return NS_OK;
    }
  }

  // Composition event handled notification should be sent after all the
  // other notifications because this notifies widget of finishing all pending
  // events are handled completely.
  if (!observer->mNeedsToNotifyIMEOfTextChange &&
      !observer->mNeedsToNotifyIMEOfSelectionChange &&
      !observer->mTicksUntilNotifyIMEOfPositionChange) {
    if (observer->mNeedsToNotifyIMEOfCompositionEventHandled) {
      observer->mNeedsToNotifyIMEOfCompositionEventHandled = false;
      SendCompositionEventHandled();
    }
  }

  observer->mQueuedSender = nullptr;

  // If notifications caused some new change, we should notify them now.
  if (observer->NeedsToNotifyIMEOfSomething()) {
    if (observer->GetState() == eState_StoppedObserving) {
      MOZ_LOG(sIMECOLog, LogLevel::Debug,
              ("0x%p IMENotificationSender::Run(), waiting "
               "IMENotificationSender to be reinitialized",
               this));
    } else {
      MOZ_LOG(sIMECOLog, LogLevel::Debug,
              ("0x%p IMENotificationSender::Run(), posting "
               "IMENotificationSender to current thread",
               this));
      observer->mQueuedSender = new IMENotificationSender(observer);
      observer->mQueuedSender->Dispatch(observer->mDocShell);
    }
  }
  return NS_OK;
}

void IMEContentObserver::IMENotificationSender::SendFocusSet() {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return;
  }

  if (!CanNotifyIME(eChangeEventType_Focus)) {
    // If IMEContentObserver has already gone, we don't need to notify IME of
    // focus.
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendFocusSet(), Warning, does not "
             "send notification due to impossible to notify IME of focus",
             this));
    observer->ClearPendingNotifications();
    return;
  }

  if (!IsSafeToNotifyIME(eChangeEventType_Focus)) {
    MOZ_LOG(
        sIMECOLog, LogLevel::Warning,
        ("0x%p   IMENotificationSender::SendFocusSet(), Warning, does not send "
         "notification due to unsafe, retrying to send NOTIFY_IME_OF_FOCUS...",
         this));
    observer->PostFocusSetNotification();
    return;
  }

  observer->mIMEHasFocus = true;
  // Initialize selection cache with the first selection data.  However, this
  // may be handled synchronously when the editor gets focus.  In that case,
  // some frames may be dirty and they may be required to get caret frame in
  // ContentEventHandler::Init() to get the nearest widget from the selection.
  // Therefore, we need to update selection cache with flushing the pending
  // notifications.
  observer->UpdateSelectionCache(true);
  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p IMENotificationSender::SendFocusSet(), sending "
           "NOTIFY_IME_OF_FOCUS...",
           this));

  MOZ_RELEASE_ASSERT(observer->mSendingNotification == NOTIFY_IME_OF_NOTHING);
  observer->mSendingNotification = NOTIFY_IME_OF_FOCUS;
  IMEStateManager::NotifyIME(IMENotification(NOTIFY_IME_OF_FOCUS),
                             observer->mWidget);
  observer->mSendingNotification = NOTIFY_IME_OF_NOTHING;

  // IMENotificationRequests referred by ObserveEditableNode() may be different
  // before or after widget receives NOTIFY_IME_OF_FOCUS.  Therefore, we need
  // to guarantee to call ObserveEditableNode() after sending
  // NOTIFY_IME_OF_FOCUS.
  observer->OnIMEReceivedFocus();

  MOZ_LOG(
      sIMECOLog, LogLevel::Debug,
      ("0x%p   IMENotificationSender::SendFocusSet(), sent NOTIFY_IME_OF_FOCUS",
       this));
}

void IMEContentObserver::IMENotificationSender::SendSelectionChange() {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return;
  }

  if (!CanNotifyIME(eChangeEventType_Selection)) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendSelectionChange(), Warning, "
             "does not send notification due to impossible to notify IME of "
             "selection change",
             this));
    return;
  }

  if (!IsSafeToNotifyIME(eChangeEventType_Selection)) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendSelectionChange(), Warning, "
             "does not send notification due to unsafe, retrying to send "
             "NOTIFY_IME_OF_SELECTION_CHANGE...",
             this));
    observer->PostSelectionChangeNotification();
    return;
  }

  SelectionChangeData lastSelChangeData = observer->mSelectionData;
  if (NS_WARN_IF(!observer->UpdateSelectionCache())) {
    MOZ_LOG(sIMECOLog, LogLevel::Error,
            ("0x%p   IMENotificationSender::SendSelectionChange(), FAILED, due "
             "to UpdateSelectionCache() failure",
             this));
    return;
  }

  // The state may be changed since querying content causes flushing layout.
  if (!CanNotifyIME(eChangeEventType_Selection)) {
    MOZ_LOG(sIMECOLog, LogLevel::Error,
            ("0x%p   IMENotificationSender::SendSelectionChange(), FAILED, due "
             "to flushing layout having changed something",
             this));
    return;
  }

  // If the selection isn't changed actually, we shouldn't notify IME of
  // selection change.
  SelectionChangeData& newSelChangeData = observer->mSelectionData;
  if (lastSelChangeData.IsInitialized() &&
      lastSelChangeData.EqualsRangeAndDirectionAndWritingMode(
          newSelChangeData)) {
    MOZ_LOG(
        sIMECOLog, LogLevel::Debug,
        ("0x%p IMENotificationSender::SendSelectionChange(), not notifying IME "
         "of NOTIFY_IME_OF_SELECTION_CHANGE due to not changed actually",
         this));
    return;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p IMENotificationSender::SendSelectionChange(), sending "
           "NOTIFY_IME_OF_SELECTION_CHANGE... newSelChangeData=%s",
           this, ToString(newSelChangeData).c_str()));

  IMENotification notification(NOTIFY_IME_OF_SELECTION_CHANGE);
  notification.SetData(observer->mSelectionData);

  MOZ_RELEASE_ASSERT(observer->mSendingNotification == NOTIFY_IME_OF_NOTHING);
  observer->mSendingNotification = NOTIFY_IME_OF_SELECTION_CHANGE;
  IMEStateManager::NotifyIME(notification, observer->mWidget);
  observer->mSendingNotification = NOTIFY_IME_OF_NOTHING;

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   IMENotificationSender::SendSelectionChange(), sent "
           "NOTIFY_IME_OF_SELECTION_CHANGE",
           this));
}

void IMEContentObserver::IMENotificationSender::SendTextChange() {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return;
  }

  if (!CanNotifyIME(eChangeEventType_Text)) {
    MOZ_LOG(
        sIMECOLog, LogLevel::Warning,
        ("0x%p   IMENotificationSender::SendTextChange(), Warning, does not "
         "send notification due to impossible to notify IME of text change",
         this));
    return;
  }

  if (!IsSafeToNotifyIME(eChangeEventType_Text)) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendTextChange(), Warning, does "
             "not send notification due to unsafe, retrying to send "
             "NOTIFY_IME_OF_TEXT_CHANGE...",
             this));
    observer->PostTextChangeNotification();
    return;
  }

  // If text change notification is unnecessary anymore, just cancel it.
  if (!observer->NeedsTextChangeNotification()) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendTextChange(), Warning, "
             "canceling sending NOTIFY_IME_OF_TEXT_CHANGE",
             this));
    observer->CancelNotifyingIMEOfTextChange();
    return;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p IMENotificationSender::SendTextChange(), sending "
           "NOTIFY_IME_OF_TEXT_CHANGE... mIMEContentObserver={ "
           "mTextChangeData=%s }",
           this, ToString(observer->mTextChangeData).c_str()));

  IMENotification notification(NOTIFY_IME_OF_TEXT_CHANGE);
  notification.SetData(observer->mTextChangeData);
  observer->mTextChangeData.Clear();

  MOZ_RELEASE_ASSERT(observer->mSendingNotification == NOTIFY_IME_OF_NOTHING);
  observer->mSendingNotification = NOTIFY_IME_OF_TEXT_CHANGE;
  IMEStateManager::NotifyIME(notification, observer->mWidget);
  observer->mSendingNotification = NOTIFY_IME_OF_NOTHING;

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   IMENotificationSender::SendTextChange(), sent "
           "NOTIFY_IME_OF_TEXT_CHANGE",
           this));
}

void IMEContentObserver::IMENotificationSender::SendPositionChange() {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return;
  }

  if (!CanNotifyIME(eChangeEventType_Position)) {
    MOZ_LOG(sIMECOLog, LogLevel::Verbose,
            ("0x%p   IMENotificationSender::SendPositionChange(), Warning, "
             "does not send notification due to impossible to notify IME of "
             "position change",
             this));
    return;
  }

  if (!IsSafeToNotifyIME(eChangeEventType_Position)) {
    MOZ_LOG(sIMECOLog, LogLevel::Verbose,
            ("0x%p   IMENotificationSender::SendPositionChange(), Warning, "
             "does not send notification due to unsafe, retrying to send "
             "NOTIFY_IME_OF_POSITION_CHANGE...",
             this));
    observer->PostPositionChangeNotification(Immediately::Yes);
    return;
  }

  // If position change notification is unnecessary anymore, just cancel it.
  if (!observer->NeedsPositionChangeNotification()) {
    MOZ_LOG(sIMECOLog, LogLevel::Verbose,
            ("0x%p   IMENotificationSender::SendPositionChange(), Warning, "
             "canceling sending NOTIFY_IME_OF_POSITION_CHANGE",
             this));
    observer->CancelNotifyingIMEOfPositionChange();
    return;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p IMENotificationSender::SendPositionChange(), sending "
           "NOTIFY_IME_OF_POSITION_CHANGE...",
           this));

  MOZ_RELEASE_ASSERT(observer->mSendingNotification == NOTIFY_IME_OF_NOTHING);
  observer->mSendingNotification = NOTIFY_IME_OF_POSITION_CHANGE;
  IMEStateManager::NotifyIME(IMENotification(NOTIFY_IME_OF_POSITION_CHANGE),
                             observer->mWidget);
  observer->mSendingNotification = NOTIFY_IME_OF_NOTHING;

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   IMENotificationSender::SendPositionChange(), sent "
           "NOTIFY_IME_OF_POSITION_CHANGE",
           this));
}

void IMEContentObserver::IMENotificationSender::SendCompositionEventHandled() {
  RefPtr<IMEContentObserver> observer = GetObserver();
  if (!observer) {
    return;
  }

  if (!CanNotifyIME(eChangeEventType_CompositionEventHandled)) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendCompositionEventHandled(), "
             "Warning, does not send notification due to impossible to notify "
             "IME of composition event handled",
             this));
    return;
  }

  if (!IsSafeToNotifyIME(eChangeEventType_CompositionEventHandled)) {
    MOZ_LOG(sIMECOLog, LogLevel::Warning,
            ("0x%p   IMENotificationSender::SendCompositionEventHandled(), "
             "Warning, does not send notification due to unsafe, retrying to "
             "send NOTIFY_IME_OF_POSITION_CHANGE...",
             this));
    observer->PostCompositionEventHandledNotification();
    return;
  }

  MOZ_LOG(sIMECOLog, LogLevel::Info,
          ("0x%p IMENotificationSender::SendCompositionEventHandled(), sending "
           "NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED...",
           this));

  MOZ_RELEASE_ASSERT(observer->mSendingNotification == NOTIFY_IME_OF_NOTHING);
  observer->mSendingNotification = NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED;
  IMEStateManager::NotifyIME(
      IMENotification(NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED),
      observer->mWidget);
  observer->mSendingNotification = NOTIFY_IME_OF_NOTHING;

  MOZ_LOG(sIMECOLog, LogLevel::Debug,
          ("0x%p   IMENotificationSender::SendCompositionEventHandled(), sent "
           "NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED",
           this));
}

/******************************************************************************
 * mozilla::IMEContentObserver::DocumentObservingHelper
 ******************************************************************************/

NS_IMPL_CYCLE_COLLECTION_CLASS(IMEContentObserver::DocumentObserver)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(IMEContentObserver::DocumentObserver)
  // StopObserving() releases mIMEContentObserver and mDocument.
  tmp->StopObserving();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(IMEContentObserver::DocumentObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIMEContentObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IMEContentObserver::DocumentObserver)
  NS_INTERFACE_MAP_ENTRY(nsIDocumentObserver)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(IMEContentObserver::DocumentObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IMEContentObserver::DocumentObserver)

void IMEContentObserver::DocumentObserver::Observe(Document* aDocument) {
  MOZ_ASSERT(aDocument);

  // Guarantee that aDocument won't be destroyed during a call of
  // StopObserving().
  RefPtr<Document> newDocument = aDocument;

  StopObserving();

  mDocument = std::move(newDocument);
  mDocument->AddObserver(this);
}

void IMEContentObserver::DocumentObserver::StopObserving() {
  if (!IsObserving()) {
    return;
  }

  // Grab IMEContentObserver which could be destroyed during method calls.
  RefPtr<IMEContentObserver> observer = std::move(mIMEContentObserver);

  // Stop observing the document first.
  RefPtr<Document> document = std::move(mDocument);
  document->RemoveObserver(this);

  // Notify IMEContentObserver of ending of document updates if this already
  // notified it of beginning of document updates.
  for (; IsUpdating(); --mDocumentUpdating) {
    // FYI: IsUpdating() returns true until mDocumentUpdating becomes 0.
    //      However, IsObserving() returns false now because mDocument was
    //      already cleared above.  Therefore, this method won't be called
    //      recursively.
    observer->EndDocumentUpdate();
  }
}

void IMEContentObserver::DocumentObserver::Destroy() {
  StopObserving();
  mIMEContentObserver = nullptr;
}

void IMEContentObserver::DocumentObserver::BeginUpdate(Document* aDocument) {
  if (NS_WARN_IF(Destroyed()) || NS_WARN_IF(!IsObserving())) {
    return;
  }
  mIMEContentObserver->BeginDocumentUpdate();
  mDocumentUpdating++;
}

void IMEContentObserver::DocumentObserver::EndUpdate(Document* aDocument) {
  if (NS_WARN_IF(Destroyed()) || NS_WARN_IF(!IsObserving()) ||
      NS_WARN_IF(!IsUpdating())) {
    return;
  }
  mDocumentUpdating--;
  mIMEContentObserver->EndDocumentUpdate();
}

/******************************************************************************
 * mozilla::IMEContentObserver::FlatTextCache
 ******************************************************************************/

void IMEContentObserver::FlatTextCache::Clear(const char* aCallerName) {
  if (!HasCache()) {
    return;
  }
  MOZ_LOG(sCacheLog, LogLevel::Info,
          ("%s.Clear: called by %s", mInstanceName, aCallerName));
  mContainerNode = nullptr;
  mContent = nullptr;
  mFlatTextLength = 0;
}

nsresult IMEContentObserver::FlatTextCache::
    ComputeAndCacheFlatTextLengthBeforeEndOfContent(
        const char* aCallerName, const nsIContent& aContent,
        const Element* aRootElement) {
  MOZ_ASSERT(aRootElement);
  MOZ_ASSERT(aContent.GetParentNode());

  Result<uint32_t, nsresult> lengthOrError =
      ContentEventHandler::GetFlatTextLengthInRange(
          RawNodePosition::BeforeFirstContentOf(*aRootElement),
          RawNodePosition::After(aContent), aRootElement);
  if (lengthOrError.isErr()) [[unlikely]] {
    Clear(aCallerName);
    return lengthOrError.unwrapErr();
  }

  CacheFlatTextLengthBeforeEndOfContent(aCallerName, aContent,
                                        lengthOrError.inspect(), aRootElement);
  return NS_OK;
}

void IMEContentObserver::FlatTextCache::CacheFlatTextLengthBeforeEndOfContent(
    const char* aCallerName, const nsIContent& aContent,
    uint32_t aFlatTextLength, const dom::Element* aRootElement) {
  mContainerNode = aContent.GetParentNode();
  mContent = const_cast<nsIContent*>(&aContent);
  mFlatTextLength = aFlatTextLength;
  MOZ_ASSERT(IsCachingToEndOfContent());
  MOZ_LOG(sCacheLog, LogLevel::Info,
          ("%s.%s: called by %s -> %s", mInstanceName, __func__,
           ShortenFunctionName(aCallerName), ToString(*this).c_str()));
  AssertValidCache(aRootElement);
}

nsresult IMEContentObserver::FlatTextCache::
    ComputeAndCacheFlatTextLengthBeforeFirstContent(
        const char* aCallerName, const nsINode& aContainer,
        const Element* aRootElement) {
  MOZ_ASSERT(aRootElement);

  const Result<uint32_t, nsresult>
      lengthIncludingLineBreakCausedByOpenTagOfContainer =
          FlatTextCache::ComputeTextLengthBeforeFirstContentOf(aContainer,
                                                               aRootElement);
  if (MOZ_UNLIKELY(
          lengthIncludingLineBreakCausedByOpenTagOfContainer.isErr())) {
    Clear(__FUNCTION__);
    return lengthIncludingLineBreakCausedByOpenTagOfContainer.inspectErr();
  }

  CacheFlatTextLengthBeforeFirstContent(
      aCallerName, aContainer,
      lengthIncludingLineBreakCausedByOpenTagOfContainer.inspect(),
      aRootElement);
  return NS_OK;
}

void IMEContentObserver::FlatTextCache::CacheFlatTextLengthBeforeFirstContent(
    const char* aCallerName, const nsINode& aContainer,
    uint32_t aFlatTextLength, const dom::Element* aRootElement) {
  mContainerNode = const_cast<nsINode*>(&aContainer);
  mContent = nullptr;
  mFlatTextLength = aFlatTextLength;
  MOZ_ASSERT(IsCachingToStartOfContainer());
  MOZ_LOG(sCacheLog, LogLevel::Info,
          ("%s.%s: called by %s -> %s", mInstanceName, __func__,
           ShortenFunctionName(aCallerName), ToString(*this).c_str()));
  AssertValidCache(aRootElement);
}

Maybe<uint32_t>
IMEContentObserver::FlatTextCache::GetFlatTextLengthBeforeContent(
    const nsIContent& aContent, const dom::Element* aRootElement) const {
  MOZ_ASSERT(aRootElement);
  if (!mContainerNode) {
    return Nothing();
  }

  nsIContent* const prevSibling = aContent.GetPreviousSibling();
  if (IsCachingToStartOfContainer()) {
    MOZ_ASSERT(!mContent);
    // If aContent is the first child of mContainerNode and we're caching text
    // length before first child of mContainerNode, we're caching the result
    // as-is..
    if (!prevSibling && mContainerNode == aContent.GetParentNode()) {
      return Some(mFlatTextLength);
    }
    return Nothing();
  }

  MOZ_ASSERT(IsCachingToEndOfContent());
  MOZ_ASSERT(mContent);

  // If we're caching text length before end of previous sibling of aContent,
  // the cached length is the result of this call.
  if (mContent == prevSibling) {
    return Some(mFlatTextLength);
  }

  // If we're caching text length before end of aContent, aContent siblings
  // may be being removed backward because aContent is the previous sibling of
  // previously removed node.  We should return the length with computing the
  // text length of aContent because it's much faster than computing the length
  // starting from the root element especially when there are a lot of preceding
  // content.
  if (mContent == &aContent) {
    const Result<uint32_t, nsresult> textLength =
        FlatTextCache::ComputeTextLengthOfContent(aContent, aRootElement);
    if (NS_WARN_IF(textLength.isErr()) ||
        NS_WARN_IF(mFlatTextLength < textLength.inspect())) {
      return Nothing();
    }
    return Some(mFlatTextLength - textLength.inspect());
  }
  return Nothing();
}

Maybe<uint32_t> IMEContentObserver::FlatTextCache::GetFlatTextOffsetOnInsertion(
    const nsIContent& aFirstContent, const nsIContent& aLastContent,
    const dom::Element* aRootElement) const {
  MOZ_ASSERT(aRootElement);
  MOZ_ASSERT(aFirstContent.GetParentNode() == aLastContent.GetParentNode());
  MOZ_ASSERT(!aFirstContent.IsBeingRemoved());
  MOZ_ASSERT(!aLastContent.IsBeingRemoved());

  if (!mContainerNode || mContainerNode != aFirstContent.GetParentNode()) {
    return Nothing();
  }

  if (IsCachingToStartOfContainer()) {
    MOZ_ASSERT(!mContent);
    // If aFirstContent is the first child of mContainerNode, we're caching the
    // result as-is.
    if (mContainerNode->GetFirstChild() == &aFirstContent) {
      return Some(mFlatTextLength);
    }
    return Nothing();
  }

  MOZ_ASSERT(IsCachingToEndOfContent());
  MOZ_ASSERT(mContent);
  MOZ_ASSERT(mContent != &aFirstContent);
  MOZ_ASSERT(mContent != &aLastContent);

  // When the content nodes are inserted forward, we may cache text length
  // before end of last inserted content.  If so, mContent should be the
  // previous sibling of aFirstContent.  Then, we can return the cached length
  // simply.
  if (mContent == aFirstContent.GetPreviousSibling()) {
    return Some(mFlatTextLength);
  }
  // When the content nodes inserted backward, we may cache text length before
  // the end of the last inserted content which is next or latter sibling of
  // aLastContent.  In this case, we can compute the length with the cache with
  // computing text length starting from the next sibling of aLastContent to
  // mContent which were previously inserted.  That must be faster than
  // computing the length starting from the root element.
  if (mContent == aLastContent.GetNextSibling() ||
      aLastContent.ComputeIndexInParentNode().valueOr(UINT32_MAX) <
          mContent->ComputeIndexInParentNode().valueOr(0u)) {
    Result<uint32_t, nsresult> previouslyInsertedTextLengthOrError =
        FlatTextCache::ComputeTextLengthStartOfContentToEndOfContent(
            *aLastContent.GetNextSibling(), *mContent, aRootElement);
    if (NS_WARN_IF(previouslyInsertedTextLengthOrError.isErr()) ||
        NS_WARN_IF(mFlatTextLength <
                   previouslyInsertedTextLengthOrError.inspect())) {
      return Nothing();
    }
    // mFlatTextLength contains the last inserted text length, but it does not
    // contain text length starting from aFirstContent to aLastContent.
    // Therefore, subtracting the last inserted text length from mFlatTextLength
    // equals the text length before aFirstContent.
    return Some(mFlatTextLength - previouslyInsertedTextLengthOrError.unwrap());
  }
  return Nothing();
}

/* static */
Result<uint32_t, nsresult>
IMEContentObserver::FlatTextCache::ComputeTextLengthOfContent(
    const nsIContent& aContent, const dom::Element* aRootElement) {
  MOZ_ASSERT(aRootElement);

  if (const Text* textNode = Text::FromNode(aContent)) {
    return ContentEventHandler::GetNativeTextLength(*textNode);
  }

  return ComputeTextLengthStartOfContentToEndOfContent(aContent, aContent,
                                                       aRootElement);
}

/* static */
Result<uint32_t, nsresult>
IMEContentObserver::FlatTextCache::ComputeTextLengthBeforeContent(
    const nsIContent& aContent, const dom::Element* aRootElement) {
  return ContentEventHandler::GetFlatTextLengthInRange(
      RawNodePosition::BeforeFirstContentOf(*aRootElement),
      RawNodePosition::Before(aContent), aRootElement);
}

/* static */
Result<uint32_t, nsresult> IMEContentObserver::FlatTextCache::
    ComputeTextLengthStartOfContentToEndOfContent(
        const nsIContent& aStartContent, const nsIContent& aEndContent,
        const dom::Element* aRootElement) {
  return ContentEventHandler::GetFlatTextLengthInRange(
      RawNodePosition::Before(aStartContent),
      RawNodePosition::After(aEndContent), aRootElement);
}

/* static */
Result<uint32_t, nsresult>
IMEContentObserver::FlatTextCache::ComputeTextLengthBeforeFirstContentOf(
    const nsINode& aContainer, const dom::Element* aRootElement) {
  return ContentEventHandler::GetFlatTextLengthInRange(
      RawNodePosition::BeforeFirstContentOf(*aRootElement),
      // Include the line break caused by open tag of aContainer if it's an
      // element when we cache text length before first content of aContainer.
      RawNodePosition(const_cast<nsINode*>(&aContainer), nullptr),
      aRootElement);
}

void IMEContentObserver::FlatTextCache::AssertValidCache(
    const Element* aRootElement) const {
#ifdef DEBUG
  if (MOZ_LIKELY(
          !StaticPrefs::test_ime_content_observer_assert_valid_cache())) {
    return;
  }
  MOZ_ASSERT(aRootElement);
  if (!mContainerNode) {
    return;
  }
  MOZ_ASSERT(mContainerNode->IsInclusiveDescendantOf(aRootElement));
  MOZ_ASSERT_IF(mContent, mContent->IsInclusiveDescendantOf(aRootElement));

  if (IsCachingToEndOfContent()) {
    MOZ_ASSERT(mContent);
    Result<uint32_t, nsresult> offset =
        FlatTextCache::ComputeTextLengthBeforeContent(*mContent, aRootElement);
    MOZ_ASSERT(offset.isOk());
    Result<uint32_t, nsresult> length =
        FlatTextCache::ComputeTextLengthStartOfContentToEndOfContent(
            *mContent, *mContent, aRootElement);
    MOZ_ASSERT(length.isOk());
    if (mFlatTextLength != offset.inspect() + length.inspect()) {
      nsAutoString innerHTMLOfEditable;
      const_cast<Element*>(aRootElement)
          ->GetInnerHTML(innerHTMLOfEditable, IgnoreErrors());
      NS_WARNING(
          nsPrintfCString(
              "mFlatTextLength=%u, offset: %u, length: %u, mContainerNode:%s, "
              "mContent=%s (%s)",
              mFlatTextLength, offset.inspect(), length.inspect(),
              ToString(mContainerNode).c_str(), ToString(*mContent).c_str(),
              NS_ConvertUTF16toUTF8(innerHTMLOfEditable).get())
              .get());
    }
    MOZ_ASSERT(mFlatTextLength == offset.inspect() + length.inspect());
    return;
  }

  MOZ_ASSERT(!mContent);
  MOZ_ASSERT(mContainerNode->IsContent());
  Result<uint32_t, nsresult> offset =
      ComputeTextLengthBeforeFirstContentOf(*mContainerNode, aRootElement);
  MOZ_ASSERT(offset.isOk());
  if (mFlatTextLength != offset.inspect()) {
    nsAutoString innerHTMLOfEditable;
    const_cast<Element*>(aRootElement)
        ->GetInnerHTML(innerHTMLOfEditable, IgnoreErrors());
    NS_WARNING(nsPrintfCString(
                   "mFlatTextLength=%u, offset: %u, mContainerNode:%s (%s)",
                   mFlatTextLength, offset.inspect(),
                   ToString(mContainerNode).c_str(),
                   NS_ConvertUTF16toUTF8(innerHTMLOfEditable).get())
                   .get());
  }
  MOZ_ASSERT(mFlatTextLength == offset.inspect());
#endif  // #ifdef DEBUG
}

void IMEContentObserver::FlatTextCache::ContentAdded(
    const char* aCallerName, const nsIContent& aFirstContent,
    const nsIContent& aLastContent, const Maybe<uint32_t>& aAddedFlatTextLength,
    const Element* aRootElement) {
  // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
  // boundaries.
  MOZ_ASSERT(nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                 ConstRawRangeBoundary::FromChild(aFirstContent),
                 ConstRawRangeBoundary::FromChild(aLastContent))
                 .value() <= 0);
  if (!mContainerNode) {
    return;  // No cache.
  }

  // We can keep cache without anything if the next sibling is the first added
  // content.
  if (mContent && &aFirstContent == mContent->GetNextSibling()) {
    return;
  }

  if (IsCachingToStartOfContainer()) {
    MOZ_ASSERT(!mContent);
    // We can keep the cache if added nodes are children of mContainerNode since
    // we cache the text length before its first child.
    if (mContainerNode == aFirstContent.GetParentNode()) {
      AssertValidCache(aRootElement);
      return;
    }

    // Let's clear the cache for avoiding to do anything expensive for a hot
    // path only for not frequent cases.  Be aware, this is a hot code path
    // here.  Therefore, expensive computation would make the DOM mutation
    // slower.
    Clear(aCallerName);
    return;
  }

  MOZ_ASSERT(IsCachingToEndOfContent());
  MOZ_ASSERT(mContent);
  if (aAddedFlatTextLength.isSome() &&
      aLastContent.GetNextSibling() == mContent) {
    // If we cache test length before end of next sibling of the last added
    // content node, we can update the cached text simply.
    CacheFlatTextLengthBeforeEndOfContent(
        aCallerName, *mContent, mFlatTextLength + *aAddedFlatTextLength,
        aRootElement);
    return;
  }

  // If empty nodes are appended, we can avoid to clear the cache because the
  // new nodes won't affect to the flattened text content.
  const bool addingEmptyNode = [&]() {
    if (aAddedFlatTextLength.isSome()) {
      return !aAddedFlatTextLength.value();
    }
    if (&aFirstContent != &aLastContent) {
      return false;  // If it's better to check here strictly, please do that.
    }
    if (aFirstContent.IsText()) {
      return !aFirstContent.AsText()->TextDataLength();
    }
    if (aFirstContent.IsCharacterData()) {
      return true;  // Should be invisible.
    }
    if (aFirstContent.HasChildren()) {
      return false;  // If it's better to check here strictly, please do that.
    }
    Result<uint32_t, nsresult> lengthOrError =
        ComputeTextLengthOfContent(aFirstContent, aRootElement);
    return lengthOrError.isOk() && !lengthOrError.unwrap();
  }();
  if (addingEmptyNode) {
    return;
  }

  // Let's clear the cache for avoiding to do anything expensive for a hot
  // path only for not frequent cases.  Be aware, this is a hot code path here.
  // Therefore, expensive computation would make the DOM mutation slower.
  Clear(aCallerName);
}

void IMEContentObserver::FlatTextCache::ContentWillBeRemoved(
    const nsIContent& aContent, uint32_t aFlatTextLengthOfContent,
    const Element* aRootElement) {
  if (!mContainerNode) {
    return;  // No cache.
  }

  // We can keep the cache without anything if the next sibling is removed.
  if (mContent && mContent == aContent.GetPreviousSibling()) {
    return;
  }

  if (IsCachingToStartOfContainer()) {
    MOZ_ASSERT(!mContent);
    // We're caching text length before first child of mContainerNode.
    // Therefore, if a child of mContainerNode is being removed, we can keep the
    // cache.
    if (mContainerNode == aContent.GetParentNode()) {
      AssertValidCache(aRootElement);
      return;
    }

    // Let's clear the cache for avoiding to do anything expensive for a hot
    // path only for not frequent cases.  Be aware, this is a hot code path
    // here.  Therefore, expensive computation would make the DOM mutation
    // slower.
    Clear(__FUNCTION__);
    return;
  }

  MOZ_ASSERT(IsCachingToEndOfContent());
  if (&aContent == mContent) {
    MOZ_ASSERT(mFlatTextLength >= aFlatTextLengthOfContent);
    if (NS_WARN_IF(mFlatTextLength < aFlatTextLengthOfContent)) {
      Clear(__FUNCTION__);
      return;
    }
    // We're caching text length before end of aContent.  So, if there is a
    // previous sibling, we can cache text length before aContent with
    // subtracting the text length caused by aContent from the cached value.
    if (nsIContent* prevSibling = aContent.GetPreviousSibling()) {
      CacheFlatTextLengthBeforeEndOfContent(
          __FUNCTION__, *prevSibling,
          mFlatTextLength - aFlatTextLengthOfContent, aRootElement);
      return;
    }
    // Otherwise, i.e., if aContent is first child of mContainerNode, we can
    // cache text length before first content of mContainerNode with subtracting
    // the text length caused by aContent from the cached value.
    CacheFlatTextLengthBeforeFirstContent(
        __FUNCTION__, *mContainerNode,
        mFlatTextLength - aFlatTextLengthOfContent, aRootElement);
    return;
  }

  // If the removing content is empty, removing that won't affect to the
  // flattened text.  Therefore, we can avoid to clear the cache.
  if (!aFlatTextLengthOfContent) {
    return;
  }

  // Let's clear the cache for avoiding to do anything expensive for a hot
  // path only for not frequent cases.  Be aware, this is a hot code path here.
  // Therefore, expensive computation would make the DOM mutation slower.
  Clear(__FUNCTION__);
}

/******************************************************************************
 * mozilla::IMEContentObserver::AddedContentCache
 ******************************************************************************/

void IMEContentObserver::AddedContentCache::Clear(const char* aCallerName) {
  mFirst = nullptr;
  mLast = nullptr;
  MOZ_LOG(sCacheLog, LogLevel::Info,
          ("AddedContentCache::Clear: called by %s",
           ShortenFunctionName(aCallerName)));
}

bool IMEContentObserver::AddedContentCache::IsInRange(
    const nsIContent& aContent, const dom::Element* aRootElement) const {
  MOZ_ASSERT(HasCache());

  // First, try to find sibling of mFirst from the ancestor chain of aContent.
  const nsIContent* sibling = [&]() -> const nsIContent* {
    const nsIContent* maybeSibling = &aContent;
    const nsIContent* const container = mFirst->GetParent();
    for (const nsIContent* ancestor : aContent.AncestorsOfType<nsIContent>()) {
      if (ancestor == container) {
        return maybeSibling;
      }
      if (ancestor == aRootElement) {
        return nullptr;
      }
      maybeSibling = ancestor;
    }
    return nullptr;
  }();
  if (!sibling) {
    return false;  // Not in same container node
  }
  // Let's avoid to compute indices...
  if (mFirst == sibling || mLast == sibling ||
      (mFirst != mLast && (mFirst->GetNextSibling() == sibling ||
                           sibling->GetNextSibling() == mLast))) {
    return true;
  }
  if (mFirst == mLast || sibling->GetNextSibling() == mFirst ||
      mLast->GetNextSibling() == sibling || !sibling->GetPreviousSibling() ||
      !sibling->GetNextSibling()) {
    return false;
  }
  const Maybe<uint32_t> index = aContent.ComputeIndexInParentNode();
  MOZ_ASSERT(index.isSome());
  const Maybe<uint32_t> firstIndex = mFirst->ComputeIndexInParentNode();
  MOZ_ASSERT(firstIndex.isSome());
  const Maybe<uint32_t> lastIndex = mLast->ComputeIndexInParentNode();
  MOZ_ASSERT(lastIndex.isSome());
  return firstIndex.value() < index.value() &&
         index.value() < lastIndex.value();
}

bool IMEContentObserver::AddedContentCache::CanMergeWith(
    const nsIContent& aFirstContent, const nsIContent& aLastContent,
    const dom::Element* aRootElement) const {
  MOZ_ASSERT(HasCache());
  if (aLastContent.GetNextSibling() == mFirst ||
      mLast->GetNextSibling() == &aFirstContent) {
    return true;
  }
  MOZ_DIAGNOSTIC_ASSERT(aFirstContent.GetParentNode() ==
                        aLastContent.GetParentNode());
  if (mFirst->GetParentNode() != aFirstContent.GetParentNode()) {
    return false;
  }
  const Maybe<uint32_t> newFirstIndex =
      aFirstContent.ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(newFirstIndex.isSome());
  const Maybe<uint32_t> newLastIndex =
      &aFirstContent == &aLastContent ? newFirstIndex
                                      : aLastContent.ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(newLastIndex.isSome());
  const Maybe<uint32_t> currentFirstIndex = mFirst->ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(currentFirstIndex.isSome());
  const Maybe<uint32_t> currentLastIndex =
      mFirst == mLast ? currentFirstIndex : mLast->ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(currentLastIndex.isSome());
  MOZ_ASSERT(!(newFirstIndex.value() < currentFirstIndex.value() &&
               newLastIndex.value() > currentLastIndex.value()),
             "New content nodes shouldn't contain mFirst nor mLast");
  MOZ_ASSERT(!(newFirstIndex.value() < currentFirstIndex.value() &&
               newLastIndex.value() > currentFirstIndex.value()),
             "New content nodes shouldn't contain mFirst");
  MOZ_ASSERT(!(newFirstIndex.value() < currentLastIndex.value() &&
               newLastIndex.value() > currentLastIndex.value()),
             "New content nodes shouldn't contain mLast");
  return *newFirstIndex > *currentFirstIndex &&
         *newLastIndex < *currentLastIndex;
}

bool IMEContentObserver::AddedContentCache::TryToCache(
    const nsIContent& aFirstContent, const nsIContent& aLastContent,
    const dom::Element* aRootElement) {
  if (!HasCache()) {
    mFirst = const_cast<nsIContent*>(&aFirstContent);
    mLast = const_cast<nsIContent*>(&aLastContent);
    MOZ_LOG(
        sCacheLog, LogLevel::Info,
        ("AddedContentCache::TryToCache: Starting to cache the range: %s - %s",
         ToString(mFirst).c_str(), ToString(mLast).c_str()));
    return true;
  }
  MOZ_ASSERT(mFirst != &aFirstContent);
  MOZ_ASSERT(mLast != &aLastContent);
  if (aLastContent.GetNextSibling() == mFirst) {
    MOZ_ASSERT(CanMergeWith(aFirstContent, aLastContent, aRootElement));
    mFirst = const_cast<nsIContent*>(&aFirstContent);
    MOZ_LOG(
        sCacheLog, LogLevel::Info,
        ("AddedContentCache::TryToCache: Extending the range backward (to %s)",
         ToString(mFirst).c_str()));
    return true;
  }
  if (mLast->GetNextSibling() == &aFirstContent) {
    MOZ_ASSERT(CanMergeWith(aFirstContent, aLastContent, aRootElement));
    mLast = const_cast<nsIContent*>(&aLastContent);
    MOZ_LOG(
        sCacheLog, LogLevel::Info,
        ("AddedContentCache::TryToCache: Extending the range forward (to %s)",
         ToString(mLast).c_str()));
    return true;
  }

  MOZ_DIAGNOSTIC_ASSERT(aFirstContent.GetParentNode() ==
                        aLastContent.GetParentNode());
  if (mFirst->GetParentNode() != aFirstContent.GetParentNode()) {
    MOZ_ASSERT(!CanMergeWith(aFirstContent, aLastContent, aRootElement));
    return false;
  }
  const Maybe<uint32_t> newFirstIndex =
      aFirstContent.ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(newFirstIndex.isSome());
  const Maybe<uint32_t> newLastIndex =
      &aFirstContent == &aLastContent ? newFirstIndex
                                      : aLastContent.ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(newLastIndex.isSome());
  const Maybe<uint32_t> currentFirstIndex = mFirst->ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(currentFirstIndex.isSome());
  const Maybe<uint32_t> currentLastIndex =
      mFirst == mLast ? currentFirstIndex : mLast->ComputeIndexInParentNode();
  MOZ_RELEASE_ASSERT(currentLastIndex.isSome());
  MOZ_ASSERT(!(newFirstIndex.value() < currentFirstIndex.value() &&
               newLastIndex.value() > currentLastIndex.value()),
             "New content nodes shouldn't contain mFirst nor mLast");
  MOZ_ASSERT(!(newFirstIndex.value() < currentFirstIndex.value() &&
               newLastIndex.value() > currentFirstIndex.value()),
             "New content nodes shouldn't contain mFirst");
  MOZ_ASSERT(!(newFirstIndex.value() < currentLastIndex.value() &&
               newLastIndex.value() > currentLastIndex.value()),
             "New content nodes shouldn't contain mLast");
  if (*newFirstIndex > *currentFirstIndex &&
      *newLastIndex < *currentLastIndex) {
    MOZ_ASSERT(CanMergeWith(aFirstContent, aLastContent, aRootElement));
    MOZ_LOG(sCacheLog, LogLevel::Info,
            ("AddedContentCache::TryToCache: New nodes in the range"));
    return true;
  }
  MOZ_ASSERT(!CanMergeWith(aFirstContent, aLastContent, aRootElement));
  return false;
}

Result<std::pair<uint32_t, uint32_t>, nsresult> IMEContentObserver::
    AddedContentCache::ComputeFlatTextRangeBeforeInsertingNewContent(
        const nsIContent& aNewFirstContent, const nsIContent& aNewLastContent,
        const dom::Element* aRootElement,
        OffsetAndLengthAdjustments& aDifferences) const {
  MOZ_ASSERT(HasCache());
  // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
  // boundaries.
  const Maybe<int32_t> newLastContentComparedWithCachedFirstContent =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          ConstRawRangeBoundary::FromChild(aNewLastContent),
          ConstRawRangeBoundary::FromChild(*mFirst));
  MOZ_RELEASE_ASSERT(newLastContentComparedWithCachedFirstContent.isSome());
  MOZ_ASSERT(*newLastContentComparedWithCachedFirstContent != 0);
  MOZ_ASSERT((*nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                  ConstRawRangeBoundary::FromChild(aNewFirstContent),
                  ConstRawRangeBoundary::FromChild(*mFirst)) > 0) ==
                 (*newLastContentComparedWithCachedFirstContent > 0),
             "New nodes shouldn't contain mFirst");
  const Maybe<int32_t> newFirstContentComparedWithCachedLastContent =
      mLast->GetNextSibling() == &aNewFirstContent
          ? Some(1)
          : nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                ConstRawRangeBoundary::FromChild(aNewFirstContent),
                // aNewFirstContent and aNewLastContent may be descendants of
                // mLast. Then, we need to ignore the new length.  Therefore,
                // we need to compare aNewFirstContent position with next
                // sibling of mLast.
                ConstRawRangeBoundary::After(*mLast));
  MOZ_RELEASE_ASSERT(newFirstContentComparedWithCachedLastContent.isSome());
  MOZ_ASSERT(*newFirstContentComparedWithCachedLastContent != 0);
  MOZ_ASSERT((*newFirstContentComparedWithCachedLastContent > 0) ==
                 (*nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                      ConstRawRangeBoundary::FromChild(aNewLastContent),
                      ConstRawRangeBoundary::After(*mLast)) > 0),
             "New nodes shouldn't contain mLast");

  Result<uint32_t, nsresult> length =
      FlatTextCache::ComputeTextLengthStartOfContentToEndOfContent(
          *mFirst, *mLast, aRootElement);
  if (NS_WARN_IF(length.isErr())) {
    return length.propagateErr();
  }
  Result<uint32_t, nsresult> offset =
      FlatTextCache::ComputeTextLengthBeforeContent(*mFirst, aRootElement);
  if (NS_WARN_IF(offset.isErr())) {
    return offset.propagateErr();
  }

  // If new content nodes are after the cached range, we can just ignore the
  // new content nodes.
  if (*newFirstContentComparedWithCachedLastContent == 1u) {
    aDifferences = OffsetAndLengthAdjustments{0, 0};
    return std::make_pair(offset.inspect(), length.inspect());
  }

  Result<uint32_t, nsresult> newLength =
      FlatTextCache::ComputeTextLengthStartOfContentToEndOfContent(
          aNewFirstContent, aNewLastContent, aRootElement);
  if (NS_WARN_IF(newLength.isErr())) {
    return newLength.propagateErr();
  }

  // If new content nodes are in the cached range, we need to subtract the new
  // content length from cached content length.
  if (*newLastContentComparedWithCachedFirstContent == 1u) {
    MOZ_RELEASE_ASSERT(length.inspect() >= newLength.inspect());
    aDifferences = OffsetAndLengthAdjustments{0, newLength.inspect()};
    return std::make_pair(offset.inspect(),
                          length.inspect() - newLength.inspect());
  }

  // If new content nodes are before the cached range, we need to subtract the
  // new content length from cached offset.
  MOZ_RELEASE_ASSERT(offset.inspect() >= newLength.inspect());
  aDifferences = OffsetAndLengthAdjustments{newLength.inspect(), 0};
  return std::make_pair(offset.inspect() - newLength.inspect(),
                        length.inspect());
}

}  // namespace mozilla
