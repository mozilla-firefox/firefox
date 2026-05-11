/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PointerLockManager.h"

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WindowContext.h"
#include "nsCOMPtr.h"
#include "nsIWidget.h"
#include "nsMenuPopupFrame.h"
#include "nsSandboxFlags.h"

mozilla::LazyLogModule gPointerLockLog("PointerLock");

#define MOZ_POINTERLOCK_LOG(...) \
  MOZ_LOG(gPointerLockLog, mozilla::LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

using mozilla::dom::BrowserChild;
using mozilla::dom::BrowserParent;
using mozilla::dom::BrowsingContext;
using mozilla::dom::CallerType;
using mozilla::dom::CanonicalBrowsingContext;
using mozilla::dom::Document;
using mozilla::dom::Element;
using mozilla::dom::PointerLockOptions;
using mozilla::dom::Promise;
using mozilla::dom::WindowContext;

// Reference to the pointer locked element.
constinit static nsWeakPtr sLockedElement;

// Reference to the document which requested pointer lock.
constinit static nsWeakPtr sLockedDoc;

// Reference to the BrowserParent requested pointer lock.
static BrowserParent* sLockedRemoteTarget = nullptr;

/* static */
bool PointerLockManager::sIsLocked = false;

/* static */
bool PointerLockManager::sIsLockUnadjustedMovement = false;

// Map a Gecko-internal pointer-lock error string to the DOMException defined in
// the spec, https://w3c.github.io/pointerlock/#dom-element-requestpointerlock.
static void RejectPromiseForError(Promise* aPromise, const char* aError) {
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(aError);

  if (!strcmp(aError, "PointerLockDeniedDisabled")) {
    aPromise->MaybeRejectWithNotSupportedError("Pointer Lock API is disabled.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedNotInDocument")) {
    aPromise->MaybeRejectWithInvalidStateError(
        "The requesting element is not in a document.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedSandboxed")) {
    aPromise->MaybeRejectWithSecurityError(
        "Pointer Lock API is restricted via sandbox.");
    return;
  }
  // Other browsers allow the hidden document to lock the pointer, but there is
  // a spec issue for that, https://github.com/w3c/pointerlock/issues/93.
  if (!strcmp(aError, "PointerLockDeniedHidden")) {
    aPromise->MaybeRejectWithWrongDocumentError("The document is not visible.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedNotFocused")) {
    aPromise->MaybeRejectWithWrongDocumentError("The document is not focused.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedFailedToLock")) {
    aPromise->MaybeRejectWithNotSupportedError(
        "The browser failed to lock the pointer.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedInUse")) {
    aPromise->MaybeRejectWithInvalidStateError(
        "The pointer is currently locked by a different document.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedNotInputDriven")) {
    aPromise->MaybeRejectWithNotAllowedError(
        "Element.requestPointerLock() was not called from inside a short "
        "running user-generated event handler, and the document is not in full "
        "screen.");
    return;
  }
  if (!strcmp(aError, "PointerLockDeniedMovedDocument")) {
    aPromise->MaybeRejectWithInvalidStateError(
        "The requesting element has moved to a different document");
    return;
  }

  MOZ_ASSERT_UNREACHABLE("Unknown pointer lock error");
  aPromise->MaybeRejectWithInvalidStateError("Unknown error.");
}

/* static */
already_AddRefed<dom::Element> PointerLockManager::GetLockedElement() {
  nsCOMPtr<Element> element = do_QueryReferent(sLockedElement);
  return element.forget();
}

/* static */
already_AddRefed<dom::Document> PointerLockManager::GetLockedDocument() {
  nsCOMPtr<Document> document = do_QueryReferent(sLockedDoc);
  return document.forget();
}

/* static */
BrowserParent* PointerLockManager::GetLockedRemoteTarget() {
  MOZ_ASSERT(XRE_IsParentProcess());
  return sLockedRemoteTarget;
}

static void DispatchPointerLockChange(Document* aTarget) {
  if (!aTarget) {
    return;
  }

  MOZ_POINTERLOCK_LOG("Dispatch pointerlockchange event [document=0x%p]",
                      aTarget);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(aTarget, u"pointerlockchange"_ns,
                               CanBubble::eYes, ChromeOnlyDispatch::eNo);
  asyncDispatcher->PostDOMEvent();
}

static void DispatchPointerLockError(Document* aTarget, const char* aMessage) {
  MOZ_ASSERT(aMessage);
  if (!aTarget) {
    return;
  }

  MOZ_POINTERLOCK_LOG(
      "Dispatch pointerlockerror event [document=0x%p, message=%s]", aTarget,
      aMessage);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(aTarget, u"pointerlockerror"_ns, CanBubble::eYes,
                               ChromeOnlyDispatch::eNo);
  asyncDispatcher->PostDOMEvent();
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                  aTarget, PropertiesFile::DOM_PROPERTIES,
                                  aMessage);
}

// Combined error path: fires pointerlockerror for backwards compatibility AND
// rejects the spec-mandated Promise with the appropriate DOMException.
static void FailWith(Document* aTarget, Promise* aPromise, const char* aError) {
  DispatchPointerLockError(aTarget, aError);
  RejectPromiseForError(aPromise, aError);
}

static bool IsPopupOpened() {
  // Check if any popup is open.
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return false;
  }

  nsTArray<nsMenuPopupFrame*> popups;
  pm->GetVisiblePopups(popups, true);

  for (nsMenuPopupFrame* popup : popups) {
    if (popup->GetPopupType() != widget::PopupType::Tooltip) {
      return true;
    }
  }

  return false;
}

static const char* GetPointerLockError(Element* aElement, Element* aCurrentLock,
                                       bool aNoFocusCheck = false) {
  // Check if pointer lock pref is enabled
  if (!StaticPrefs::full_screen_api_pointer_lock_enabled()) {
    return "PointerLockDeniedDisabled";
  }

  nsCOMPtr<Document> ownerDoc = aElement->OwnerDoc();
  if (aCurrentLock && aCurrentLock->OwnerDoc() != ownerDoc) {
    return "PointerLockDeniedInUse";
  }

  if (!aElement->IsInComposedDoc()) {
    return "PointerLockDeniedNotInDocument";
  }

  if (ownerDoc->GetSandboxFlags() & SANDBOXED_POINTER_LOCK) {
    return "PointerLockDeniedSandboxed";
  }

  // Check if the element is in a document with a docshell.
  if (!ownerDoc->GetContainer()) {
    return "PointerLockDeniedHidden";
  }
  nsCOMPtr<nsPIDOMWindowOuter> ownerWindow = ownerDoc->GetWindow();
  if (!ownerWindow) {
    return "PointerLockDeniedHidden";
  }
  nsCOMPtr<nsPIDOMWindowInner> ownerInnerWindow = ownerDoc->GetInnerWindow();
  if (!ownerInnerWindow) {
    return "PointerLockDeniedHidden";
  }
  if (ownerWindow->GetCurrentInnerWindow() != ownerInnerWindow) {
    return "PointerLockDeniedHidden";
  }

  BrowsingContext* bc = ownerDoc->GetBrowsingContext();
  BrowsingContext* topBC = bc ? bc->Top() : nullptr;
  WindowContext* topWC = ownerDoc->GetTopLevelWindowContext();
  if (!topBC || !topBC->IsActive() || !topWC ||
      topWC != topBC->GetCurrentWindowContext()) {
    return "PointerLockDeniedHidden";
  }

  if (!aNoFocusCheck) {
    if (!IsInActiveTab(ownerDoc)) {
      return "PointerLockDeniedNotFocused";
    }
  }

  if (IsPopupOpened()) {
    return "PointerLockDeniedFailedToLock";
  }

  return nullptr;
}

/* static */
void PointerLockManager::RequestLock(Element* aElement,
                                     const PointerLockOptions& aOptions,
                                     CallerType aCallerType,
                                     Promise* aPromise) {
  MOZ_ASSERT(aPromise);
  NS_ASSERTION(aElement,
               "Must pass non-null element to PointerLockManager::RequestLock");

  RefPtr<Document> doc = aElement->OwnerDoc();
  nsCOMPtr<Element> pointerLockedElement = GetLockedElement();
  MOZ_POINTERLOCK_LOG(
      "Request lock on element 0x%p [document=0x%p, "
      "unadjustedMovement=%s]",
      aElement, doc.get(), aOptions.mUnadjustedMovement ? "true" : "false");

  // XXX: https://bugzilla.mozilla.org/show_bug.cgi?id=2037874.
  // Spec does this check in queued task instead, see step 6-2 of
  // https://w3c.github.io/pointerlock/#dom-element-requestpointerlock.
  if (aElement == pointerLockedElement &&
      sIsLockUnadjustedMovement == aOptions.mUnadjustedMovement) {
    DispatchPointerLockChange(doc);
    aPromise->MaybeResolveWithUndefined();
    return;
  }

  if (const char* msg = GetPointerLockError(aElement, pointerLockedElement)) {
    FailWith(doc, aPromise, msg);
    return;
  }

  bool userInputOrSystemCaller =
      doc->HasValidTransientUserGestureActivation() ||
      aCallerType == CallerType::System;
  nsCOMPtr<nsIRunnable> request =
      new PointerLockRequest(aElement, userInputOrSystemCaller,
                             aOptions.mUnadjustedMovement, aPromise);
  doc->Dispatch(request.forget());
}

/* static */
void PointerLockManager::Unlock(const char* aReason, Document* aDoc) {
  if (sLockedRemoteTarget) {
    MOZ_ASSERT(XRE_IsParentProcess());
    MOZ_ASSERT(!sIsLocked);
    MOZ_POINTERLOCK_LOG(
        "Unlock document 0x%p [sLockedRemoteTarget=0x%p, reason=%s]", aDoc,
        sLockedRemoteTarget, aReason);

    if (aDoc) {
      CanonicalBrowsingContext* lockedBc =
          sLockedRemoteTarget->GetBrowsingContext();
      if (lockedBc &&
          lockedBc->TopCrossChromeBoundary()->GetExtantDocument() != aDoc) {
        return;
      }
    }

    (void)sLockedRemoteTarget->SendReleasePointerLock();
    sLockedRemoteTarget = nullptr;
    return;
  }

  if (!sIsLocked) {
    return;
  }

  nsCOMPtr<Document> pointerLockedDoc = GetLockedDocument();
  MOZ_POINTERLOCK_LOG("Unlock document 0x%p [LockedDocument=0x%p, reason=%s]",
                      aDoc, pointerLockedDoc.get(), aReason);

  if (!pointerLockedDoc || (aDoc && aDoc != pointerLockedDoc)) {
    return;
  }
  if (!SetPointerLock(nullptr, pointerLockedDoc, StyleCursorKind::Auto,
                      /* aUnadjustedMovement */ false)) {
    return;
  }

  nsCOMPtr<Element> pointerLockedElement = GetLockedElement();
  ChangePointerLockedElement(nullptr, pointerLockedDoc, pointerLockedElement);

  if (BrowserChild* browserChild =
          BrowserChild::GetFrom(pointerLockedDoc->GetDocShell())) {
    browserChild->SendReleasePointerLock();
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *pointerLockedElement, u"MozDOMPointerLock:Exited"_ns, CanBubble::eYes,
      ChromeOnlyDispatch::eYes);
}

/* static */
void PointerLockManager::ChangePointerLockedElement(
    Element* aElement, Document* aDocument, Element* aPointerLockedElement) {
  // aDocument here is not really necessary, as it is the uncomposed
  // document of both aElement and aPointerLockedElement as far as one
  // is not nullptr, and they wouldn't both be nullptr in any case.
  // But since the caller of this function should have known what the
  // document is, we just don't try to figure out what it should be.
  MOZ_ASSERT(aDocument);
  MOZ_ASSERT(aElement != aPointerLockedElement);
  MOZ_POINTERLOCK_LOG("Change locked element from 0x%p to 0x%p [document=0x%p]",
                      aPointerLockedElement, aElement, aDocument);
  if (aPointerLockedElement) {
    MOZ_ASSERT(aPointerLockedElement->GetComposedDoc() == aDocument);
    aPointerLockedElement->ClearPointerLock();
  }
  if (aElement) {
    MOZ_ASSERT(aElement->GetComposedDoc() == aDocument);
    aElement->SetPointerLock();
    sLockedElement = do_GetWeakReference(aElement);
    sLockedDoc = do_GetWeakReference(aDocument);
    NS_ASSERTION(sLockedElement && sLockedDoc,
                 "aElement and this should support weak references!");
  } else {
    sLockedElement = nullptr;
    sLockedDoc = nullptr;
  }
  // Retarget all events to aElement via capture or
  // stop retargeting if aElement is nullptr.
  PresShell::SetCapturingContent(aElement, CaptureFlags::PointerLock);
  DispatchPointerLockChange(aDocument);
}

/* static */
bool PointerLockManager::StartSetPointerLock(Element* aElement,
                                             Document* aDocument,
                                             bool aUnadjustedMovement) {
  if (!SetPointerLock(aElement, aDocument, StyleCursorKind::None,
                      aUnadjustedMovement)) {
    DispatchPointerLockError(aDocument, "PointerLockDeniedFailedToLock");
    return false;
  }

  ChangePointerLockedElement(aElement, aDocument, nullptr);
  nsContentUtils::DispatchEventOnlyToChrome(
      aDocument, aElement, u"MozDOMPointerLock:Entered"_ns, CanBubble::eYes,
      Cancelable::eNo, /* DefaultAction */ nullptr);

  return true;
}

/* static */
bool PointerLockManager::SetPointerLock(Element* aElement, Document* aDocument,
                                        StyleCursorKind aCursorStyle,
                                        bool aUnadjustedMovement) {
  MOZ_ASSERT(!aElement || aElement->OwnerDoc() == aDocument,
             "We should be either unlocking pointer (aElement is nullptr), "
             "or locking pointer to an element in this document");
#ifdef DEBUG
  if (!aElement) {
    nsCOMPtr<Document> pointerLockedDoc = GetLockedDocument();
    MOZ_ASSERT(pointerLockedDoc == aDocument);
  }
#endif

  PresShell* presShell = aDocument->GetPresShell();
  if (!presShell) {
    NS_WARNING("SetPointerLock(): No PresShell");
    if (!aElement) {
      sIsLocked = false;
      sIsLockUnadjustedMovement = false;
      // If we are unlocking pointer lock, but for some reason the doc
      // has already detached from the presshell, just ask the event
      // state manager to release the pointer.
      EventStateManager::SetPointerLock(nullptr, nullptr,
                                        /* aUnadjustedMovement */ false);
      return true;
    }
    return false;
  }
  RefPtr<nsPresContext> presContext = presShell->GetPresContext();
  if (!presContext) {
    NS_WARNING("SetPointerLock(): Unable to get PresContext");
    return false;
  }

  nsCOMPtr<nsIWidget> widget;
  nsIFrame* rootFrame = presShell->GetRootFrame();
  if (!NS_WARN_IF(!rootFrame)) {
    widget = rootFrame->GetNearestWidget();
    NS_WARNING_ASSERTION(widget,
                         "SetPointerLock(): Unable to find widget in "
                         "presShell->GetRootFrame()->GetNearestWidget();");
  }

  if (aElement && !widget) {
    NS_WARNING("SetPointerLock(): No Widget while requesting pointer lock");
    return false;
  }

  sIsLocked = !!aElement;
  sIsLockUnadjustedMovement = !!aElement && aUnadjustedMovement;

  // Hide the cursor and set pointer lock for future mouse events
  RefPtr<EventStateManager> esm = presContext->EventStateManager();
  esm->SetCursor(aCursorStyle, nullptr, {}, Nothing(), widget, true);
  EventStateManager::SetPointerLock(widget, presContext,
                                    sIsLockUnadjustedMovement);

  return true;
}

/* static */
bool PointerLockManager::IsInLockContext(BrowsingContext* aContext) {
  if (!aContext) {
    return false;
  }

  nsCOMPtr<Document> pointerLockedDoc = GetLockedDocument();
  if (!pointerLockedDoc || !pointerLockedDoc->GetBrowsingContext()) {
    return false;
  }

  BrowsingContext* lockTop = pointerLockedDoc->GetBrowsingContext()->Top();
  BrowsingContext* top = aContext->Top();

  return top == lockTop;
}

/* static */
void PointerLockManager::SetLockedRemoteTarget(BrowserParent* aBrowserParent,
                                               nsACString& aError) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (sLockedRemoteTarget) {
    if (sLockedRemoteTarget != aBrowserParent) {
      aError = "PointerLockDeniedInUse"_ns;
    }
    return;
  }

  // Check if any popup is open.
  if (IsPopupOpened()) {
    aError = "PointerLockDeniedFailedToLock"_ns;
    return;
  }

  MOZ_POINTERLOCK_LOG("Set locked remote target to 0x%p", aBrowserParent);
  sLockedRemoteTarget = aBrowserParent;
  PointerEventHandler::ReleaseAllPointerCaptureRemoteTarget();
}

/* static */
void PointerLockManager::ReleaseLockedRemoteTarget(
    BrowserParent* aBrowserParent) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (sLockedRemoteTarget == aBrowserParent) {
    MOZ_POINTERLOCK_LOG("Release locked remote target 0x%p",
                        sLockedRemoteTarget);
    sLockedRemoteTarget = nullptr;
  }
}

static nsIWidget* GetWidgetForDocument(Document* aDocument) {
  if (!aDocument) {
    return nullptr;
  }
  PresShell* presShell = aDocument->GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  return presShell->GetRootWidget();
}

PointerLockManager::PointerLockRequest::PointerLockRequest(
    Element* aElement, bool aUserInputOrChromeCaller, bool aUnadjustedMovement,
    Promise* aPromise)
    : mozilla::Runnable("PointerLockRequest"),
      mElement(do_GetWeakReference(aElement)),
      mDocument(do_GetWeakReference(aElement->OwnerDoc())),
      mUserInputOrChromeCaller(aUserInputOrChromeCaller),
      mUnadjustedMovement(aUnadjustedMovement),
      mPromise(aPromise) {}

NS_IMETHODIMP
PointerLockManager::PointerLockRequest::Run() {
  nsCOMPtr<Element> element = do_QueryReferent(mElement);
  nsCOMPtr<Document> document = do_QueryReferent(mDocument);
  RefPtr<Promise> promise = std::move(mPromise);

  const char* error = nullptr;
  if (!element || !document || !element->GetComposedDoc()) {
    error = "PointerLockDeniedNotInDocument";
  } else if (element->GetComposedDoc() != document) {
    error = "PointerLockDeniedMovedDocument";
  } else if (mUnadjustedMovement) {
    nsCOMPtr<nsIWidget> widget = GetWidgetForDocument(document);
    if (!widget || !widget->SupportsUnadjustedMovement()) {
      // XXX Reuse the existing error code for now, we should have a more
      // specific error code for this case.
      error = "PointerLockDeniedFailedToLock";
    }
  }

  if (!error) {
    nsCOMPtr<Element> pointerLockedElement = do_QueryReferent(sLockedElement);
    // XXX The steps are not exactly the same as in the spec, but they should
    // result in the same behavior.
    if (element == pointerLockedElement &&
        sIsLockUnadjustedMovement == mUnadjustedMovement) {
      DispatchPointerLockChange(document);
      promise->MaybeResolveWithUndefined();
      return NS_OK;
    }
    // Note, we must bypass focus change, so pass true as the last parameter!
    error = GetPointerLockError(element, pointerLockedElement, true);
    // Another element in the same document is requesting pointer lock,
    // just grant it without user input check.
    if (!error && pointerLockedElement) {
      // Apply new options on the existing lock.
      if (sIsLockUnadjustedMovement != mUnadjustedMovement) {
        nsCOMPtr<nsIWidget> widget = GetWidgetForDocument(document);
        if (NS_WARN_IF(!widget)) {
          FailWith(document, promise, "PointerLockDeniedFailedToLock");
          return NS_OK;
        }
        MOZ_ASSERT(widget->SupportsUnadjustedMovement());
        widget->SetNativePointerLockMode(
            mUnadjustedMovement ? nsIWidget::NativePointerLockMode::Unadjusted
                                : nsIWidget::NativePointerLockMode::Regular);
        sIsLockUnadjustedMovement = mUnadjustedMovement;
      }
      if (element != pointerLockedElement) {
        ChangePointerLockedElement(element, document, pointerLockedElement);
      } else {
        DispatchPointerLockChange(document);
      }
      promise->MaybeResolveWithUndefined();
      return NS_OK;
    }
  }
  // If it is neither user input initiated, nor requested in fullscreen,
  // it should be rejected.
  if (!error && !mUserInputOrChromeCaller && !document->Fullscreen()) {
    error = "PointerLockDeniedNotInputDriven";
  }

  if (error) {
    FailWith(document, promise, error);
    return NS_OK;
  }

  if (BrowserChild* browserChild =
          BrowserChild::GetFrom(document->GetDocShell())) {
    nsWeakPtr e = do_GetWeakReference(element);
    nsWeakPtr doc = do_GetWeakReference(element->OwnerDoc());
    nsWeakPtr bc = do_GetWeakReference(browserChild);
    browserChild->SendRequestPointerLock(
        [e, doc, bc, promise,
         unadjustedMovement = mUnadjustedMovement](const nsCString& aError) {
          nsCOMPtr<Document> document = do_QueryReferent(doc);
          if (!aError.IsEmpty()) {
            FailWith(document, promise, aError.get());
            return;
          }

          const char* error = nullptr;
          auto autoCleanup = MakeScopeExit([&] {
            if (error) {
              FailWith(document, promise, error);
              // If we are failed to set pointer lock, notify parent to stop
              // redirect mouse event to this process.
              if (nsCOMPtr<nsIBrowserChild> browserChild =
                      do_QueryReferent(bc)) {
                static_cast<BrowserChild*>(browserChild.get())
                    ->SendReleasePointerLock();
              }
            }
          });

          nsCOMPtr<Element> element = do_QueryReferent(e);
          if (!element || !document || !element->GetComposedDoc()) {
            error = "PointerLockDeniedNotInDocument";
            return;
          }

          if (element->GetComposedDoc() != document) {
            error = "PointerLockDeniedMovedDocument";
            return;
          }

          nsCOMPtr<Element> pointerLockedElement = GetLockedElement();
          error = GetPointerLockError(element, pointerLockedElement, true);
          if (error) {
            return;
          }

          if (!StartSetPointerLock(element, document, unadjustedMovement)) {
            error = "PointerLockDeniedFailedToLock";
            return;
          }

          promise->MaybeResolveWithUndefined();
        },
        [doc, promise](mozilla::ipc::ResponseRejectReason) {
          // IPC layer error
          nsCOMPtr<Document> document = do_QueryReferent(doc);
          FailWith(document, promise, "PointerLockDeniedFailedToLock");
        });
  } else {
    if (StartSetPointerLock(element, document, mUnadjustedMovement)) {
      promise->MaybeResolveWithUndefined();
    } else {
      FailWith(document, promise, "PointerLockDeniedFailedToLock");
    }
  }

  return NS_OK;
}

}  // namespace mozilla
