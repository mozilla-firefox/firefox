/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DocumentPictureInPicture.h"

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentPictureInPictureEvent.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/widget/Screen.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsGlobalWindowOuter.h"
#include "nsIWindowWatcher.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPIWindowWatcher.h"
#include "nsServiceManagerUtils.h"
#include "nsWindowWatcher.h"

namespace mozilla::dom {

static mozilla::LazyLogModule gDPIPLog("DocumentPIP");

NS_IMPL_CYCLE_COLLECTION_CLASS(DocumentPictureInPicture)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(DocumentPictureInPicture,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLastOpenedWindow)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(DocumentPictureInPicture,
                                                DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLastOpenedWindow)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DocumentPictureInPicture)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(DocumentPictureInPicture, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(DocumentPictureInPicture, DOMEventTargetHelper)

JSObject* DocumentPictureInPicture::WrapObject(
    JSContext* cx, JS::Handle<JSObject*> aGivenProto) {
  return DocumentPictureInPicture_Binding::Wrap(cx, this, aGivenProto);
}

DocumentPictureInPicture::DocumentPictureInPicture(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow) {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_TRUE_VOID(os);
  MOZ_ALWAYS_SUCCEEDS(os->AddObserver(this, "domwindowclosed", false));
  MOZ_ALWAYS_SUCCEEDS(
      os->AddObserver(this, "docshell-position-size-changed", false));
}

DocumentPictureInPicture::~DocumentPictureInPicture() {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_TRUE_VOID(os);
  MOZ_ALWAYS_SUCCEEDS(os->RemoveObserver(this, "domwindowclosed"));
  MOZ_ALWAYS_SUCCEEDS(
      os->RemoveObserver(this, "docshell-position-size-changed"));
}

void DocumentPictureInPicture::OnPiPResized() {
  if (!mLastOpenedWindow) {
    return;
  }

  RefPtr<nsGlobalWindowInner> innerWindow =
      nsGlobalWindowInner::Cast(mLastOpenedWindow);

  int x = innerWindow->GetScreenLeft(CallerType::System, IgnoreErrors());
  int y = innerWindow->GetScreenTop(CallerType::System, IgnoreErrors());
  int width = static_cast<int>(std::round(
      innerWindow->GetInnerWidth(dom::CallerType::System, IgnoreErrors())));
  int height = static_cast<int>(std::round(
      innerWindow->GetInnerHeight(dom::CallerType::System, IgnoreErrors())));

  mPreviousExtent = Some(CSSIntRect(x, y, width, height));

  MOZ_LOG(gDPIPLog, LogLevel::Debug,
          ("PiP was resized, remembering position %s",
           ToString(mPreviousExtent).c_str()));
}

void DocumentPictureInPicture::OnPiPClosed() {
  if (!mLastOpenedWindow) {
    return;
  }

  MOZ_LOG(gDPIPLog, LogLevel::Debug, ("PiP was closed"));

  mLastOpenedWindow = nullptr;

  if (RefPtr<nsPIDOMWindowInner> ownerWin = GetOwnerWindow()) {
    if (BrowsingContext* bc = ownerWin->GetBrowsingContext()) {
      MOZ_ASSERT(bc->GetControlsDocumentPiP());
      DebugOnly<nsresult> rv = bc->SetControlsDocumentPiP(false);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }
}

nsGlobalWindowInner* DocumentPictureInPicture::GetWindow() const {
  if (mLastOpenedWindow && mLastOpenedWindow->GetOuterWindow() &&
      !mLastOpenedWindow->GetOuterWindow()->Closed()) {
    return nsGlobalWindowInner::Cast(mLastOpenedWindow);
  }
  return nullptr;
}

// Some sane default.
const CSSIntSize DocumentPictureInPicture::sDefaultSize = {400, 300};
const CSSIntSize DocumentPictureInPicture::sMinSize = {240, 50};

MOZ_CAN_RUN_SCRIPT
static nsresult OpenPiPWindowUtility(nsPIDOMWindowOuter* aParent,
                                     const CSSIntRect& aExtent, bool aPrivate,
                                     bool aDisallowReturnToOpener,
                                     mozilla::dom::BrowsingContext** aRet) {
  MOZ_DIAGNOSTIC_ASSERT(aParent);

  nsresult rv = NS_OK;
  nsCOMPtr<nsIWindowWatcher> ww =
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsPIWindowWatcher> pww(do_QueryInterface(ww));
  NS_ENSURE_TRUE(pww, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), "about:blank"_ns, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsDocShellLoadState> loadState =
      nsWindowWatcher::CreateLoadState(uri, aParent);

  // Ensure we load with this's relevant global object's principal
  loadState->SetTriggeringPrincipal(aParent->GetExtantDoc()->NodePrincipal());

  // pictureinpicture, disallow_return_to_oopener are non-standard window
  // features not available from JS
  nsPrintfCString features("pictureinpicture,top=%d,left=%d,width=%d,height=%d",
                           aExtent.y, aExtent.x, aExtent.width, aExtent.height);
  if (aDisallowReturnToOpener) {
    features += ",disallow_return_to_opener";
  }

  rv = pww->OpenWindow2(aParent, uri, u"_blank"_ns, features,
                        mozilla::dom::UserActivation::Modifiers::None(), false,
                        false, true, nullptr, false, false, false,
                        nsPIWindowWatcher::PrintKind::PRINT_NONE, loadState,
                        aRet);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(aRet, NS_ERROR_FAILURE);
  return NS_OK;
}

/* static */
Maybe<CSSIntRect> DocumentPictureInPicture::GetScreenRect(
    nsPIDOMWindowOuter* aWindow) {
  nsCOMPtr<nsIWidget> widget = widget::WidgetUtils::DOMWindowToWidget(aWindow);
  NS_ENSURE_TRUE(widget, Nothing());
  RefPtr<widget::Screen> screen = widget->GetWidgetScreen();
  NS_ENSURE_TRUE(screen, Nothing());
  LayoutDeviceIntRect rect = screen->GetRect();

  nsGlobalWindowOuter* outerWindow = nsGlobalWindowOuter::Cast(aWindow);
  NS_ENSURE_TRUE(outerWindow, Nothing());
  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = outerWindow->GetTreeOwnerWindow();
  NS_ENSURE_TRUE(treeOwnerAsWin, Nothing());
  auto scale = outerWindow->CSSToDevScaleForBaseWindow(treeOwnerAsWin);

  return Some(RoundedToInt(rect / scale));
}

// Place window in the bottom right of the opener window's screen
static CSSIntRect CalcInitialExtent(const CSSIntRect& aScreen,
                                    const CSSIntSize& aSize) {
  // aSize is the inner size not including browser UI. But we need the outer
  // size for calculating where the top left corner of the PiP should be
  // initially. For now use a guess of ~80px for the browser UI?
  const CSSIntPoint pos = {
      std::max(aScreen.X(), aScreen.XMost() - aSize.width - 100),
      std::max(aScreen.Y(), aScreen.YMost() - aSize.height - 100 - 80)};
  return CSSIntRect(pos, aSize);
}

/* static */
CSSIntSize DocumentPictureInPicture::CalcMaxDimensions(
    const CSSIntRect& aScreen) {
  // Limit PIP size to 80% (arbitrary number) of screen size
  // https://wicg.github.io/document-picture-in-picture/#maximum-size
  CSSIntSize size =
      RoundedToInt(aScreen.Size() * gfx::ScaleFactor<CSSPixel, CSSPixel>(0.8));
  size.width = std::max(size.width, sMinSize.width);
  size.height = std::max(size.height, sMinSize.height);
  return size;
}

CSSIntRect DocumentPictureInPicture::DetermineExtent(
    bool aPreferInitialWindowPlacement, const CSSIntSize& aRequestedSize,
    const CSSIntRect& aScreen) {
  // The user agent may use the previous position and size.
  // https://wicg.github.io/document-picture-in-picture/#example-prefer-initial-window-placement

  // If no width/height was specified (it's 0), consider it unchanged too.
  const bool emptyRequest = aRequestedSize == CSSIntSize(0, 0);
  const bool requestChanged =
      !emptyRequest &&
      (mLastRequestedSize.isNothing() || *mLastRequestedSize != aRequestedSize);

  if (!emptyRequest) {
    mLastRequestedSize = Some(aRequestedSize);
  }

  // If we remembered an extent, don't preferInitialWindowPlacement, and the
  // requested size didn't change, then restore the remembered extent.
  const bool reusePreviousExtent = mPreviousExtent.isSome() &&
                                   !aPreferInitialWindowPlacement &&
                                   !requestChanged;

  MOZ_LOG_FMT(gDPIPLog, LogLevel::Debug,
              "{} reuse previous extent (hasPrevious={}, preferInitial={}, "
              "requestChanged={})",
              reusePreviousExtent ? "Will" : "Won't", mPreviousExtent.isSome(),
              aPreferInitialWindowPlacement, requestChanged);

  CSSIntRect extent;
  if (reusePreviousExtent) {
    extent = mPreviousExtent.value();
  } else {
    extent = CalcInitialExtent(aScreen,
                               emptyRequest ? sDefaultSize : aRequestedSize);

    MOZ_LOG(gDPIPLog, LogLevel::Debug,
            ("Calculated initial PiP rect %s", ToString(extent).c_str()));
  }

  // https://wicg.github.io/document-picture-in-picture/#maximum-size
  CSSIntSize maxSize = CalcMaxDimensions(aScreen);
  extent.width = std::clamp(extent.width, sMinSize.width, maxSize.width);
  extent.height = std::clamp(extent.height, sMinSize.height, maxSize.height);

  return extent;
}

already_AddRefed<Promise> DocumentPictureInPicture::RequestWindow(
    const DocumentPictureInPictureOptions& aOptions, ErrorResult& aRv) {
  // Not part of the spec, but check the document is active
  RefPtr<nsPIDOMWindowInner> ownerWin = GetOwnerWindow();
  if (!ownerWin || !ownerWin->IsFullyActive()) {
    aRv.ThrowNotAllowedError("Document is not fully active");
    return nullptr;
  }

  // 2. Throw if not top-level
  RefPtr<BrowsingContext> bc = ownerWin->GetBrowsingContext();
  if (!bc || !bc->IsTop()) {
    aRv.ThrowNotAllowedError(
        "Document Picture-in-Picture is only available in top-level contexts");
    return nullptr;
  }

  // 3. Throw if already in a Document PIP window
  if (bc->GetIsDocumentPiP()) {
    aRv.ThrowNotAllowedError(
        "Cannot open a Picture-in-Picture window from inside one");
    return nullptr;
  }

  // 4, 7. Require transient activation
  // XXX maybe exempt extensions, see bug 2047870.
  WindowContext* wc = ownerWin->GetWindowContext();
  if (!wc || !wc->ConsumeTransientUserGestureActivation()) {
    aRv.ThrowNotAllowedError(
        "Document Picture-in-Picture requires user activation");
    return nullptr;
  }

  // 5-6. If width or height is given, both must be specified
  if ((aOptions.mWidth > 0) != (aOptions.mHeight > 0)) {
    aRv.ThrowRangeError(
        "requestWindow: width and height must be specified together");
    return nullptr;
  }

  // 8. Possibly close last opened window
  if (RefPtr<nsPIDOMWindowInner> lastOpenedWindow = mLastOpenedWindow) {
    // Hack: Clean up synchronously before we open the next window
    OnPiPClosed();
    lastOpenedWindow->Close();
  }

  CSSIntRect screen;
  if (Maybe<CSSIntRect> maybeScreen =
          GetScreenRect(ownerWin->GetOuterWindow())) {
    screen = maybeScreen.value();
  } else {
    aRv.ThrowRangeError("Could not determine screen for window");
    return nullptr;
  }

  // 13-15. Determine PiP extent
  const CSSIntSize requestedSize = {SaturatingCast<int>(aOptions.mWidth),
                                    SaturatingCast<int>(aOptions.mHeight)};
  CSSIntRect extent = DetermineExtent(aOptions.mPreferInitialWindowPlacement,
                                      requestedSize, screen);

  MOZ_LOG(gDPIPLog, LogLevel::Debug,
          ("Will place PiP at rect %s", ToString(extent).c_str()));

  // 9. Optionally, close any existing PIP windows
  // I think it's useful to have multiple PiP windows from different top pages.

  // Mark opener as controlling PiP before opening, since window creation
  // can change activeness and the opener must be active when setting the flag.
  MOZ_ASSERT(!bc->GetControlsDocumentPiP());
  nsresult rv = bc->SetControlsDocumentPiP(true);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // 10. Create a new top-level traversable for target _blank
  // 15. aOptions.mDisallowReturnToOpener
  // 16. Configure PIP to float on top via window features
  RefPtr<BrowsingContext> pipTraversable;
  RefPtr outer = ownerWin->GetOuterWindow();
  rv = OpenPiPWindowUtility(outer, extent, bc->UsePrivateBrowsing(),
                            aOptions.mDisallowReturnToOpener,
                            getter_AddRefs(pipTraversable));
  if (NS_FAILED(rv)) {
    rv = bc->SetControlsDocumentPiP(false);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    aRv.ThrowUnknownError("Failed to create PIP window");
    return nullptr;
  }

  // Opening a window is MOZ_CAN_RUN_SCRIPT, we might be going away.
  if (!ownerWin->IsFullyActive()) {
    pipTraversable->Close(CallerType::System, IgnoreErrors());
    (void)bc->SetControlsDocumentPiP(false);
    aRv.ThrowNotAllowedError("Document is not fully active");
    return nullptr;
  }

  // 11. Set PIP's active document's mode to this's document's mode
  pipTraversable->GetDocument()->SetCompatibilityMode(
      ownerWin->GetDoc()->GetCompatibilityMode());

  // 12. Set PIP's IsDocumentPIP flag
  rv = pipTraversable->SetIsDocumentPiP(true);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // 16. Set mLastOpenedWindow
  mLastOpenedWindow = pipTraversable->GetDOMWindow()->GetCurrentInnerWindow();
  MOZ_ASSERT(mLastOpenedWindow);

  // 17. Queue a task to fire a DocumentPictureInPictureEvent named "enter" on
  // this with pipTraversable as it's window attribute
  DocumentPictureInPictureEventInit eventInit;
  eventInit.mWindow = nsGlobalWindowInner::Cast(mLastOpenedWindow);
  RefPtr<Event> event =
      DocumentPictureInPictureEvent::Constructor(this, u"enter"_ns, eventInit);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this, event.forget());
  asyncDispatcher->PostDOMEvent();

  // 18. Return pipTraversable
  RefPtr<Promise> promise = Promise::CreateInfallible(GetRelevantGlobal());
  promise->MaybeResolve(nsGlobalWindowInner::Cast(mLastOpenedWindow));
  return promise.forget();
}

NS_IMETHODIMP DocumentPictureInPicture::Observe(nsISupports* aSubject,
                                                const char* aTopic,
                                                const char16_t* aData) {
  if (!mLastOpenedWindow) {
    return NS_OK;
  }

  if (nsCRT::strcmp(aTopic, "domwindowclosed") == 0) {
    nsCOMPtr<nsPIDOMWindowOuter> subjectWin = do_QueryInterface(aSubject);
    NS_ENSURE_TRUE(!!subjectWin, NS_OK);

    if (subjectWin->GetCurrentInnerWindow() == mLastOpenedWindow) {
      OnPiPClosed();
    }
  } else if (nsCRT::strcmp(aTopic, "docshell-position-size-changed") == 0) {
    nsCOMPtr<nsIDocShell> docshell = do_QueryInterface(aSubject);
    NS_ENSURE_TRUE(!!docshell, NS_OK);
    BrowsingContext* bc = docshell->GetBrowsingContext();

    if (!bc || !bc->GetIsDocumentPiP()) {
      return NS_OK;
    }

    if (bc == mLastOpenedWindow->GetBrowsingContext()) {
      // Async invocation due to MOZ_CAN_RUN_SCRIPT
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "DocumentPictureInPicture::OnPiPResized",
          [_self = RefPtr(this)]()
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA { _self->OnPiPResized(); }));
    }
  }
  return NS_OK;
}

}  // namespace mozilla::dom
