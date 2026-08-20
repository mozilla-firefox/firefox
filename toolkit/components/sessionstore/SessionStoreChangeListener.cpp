/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SessionStoreChangeListener.h"

#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/SessionStoreChild.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_browser.h"

#include "nsBaseHashtable.h"
#include "nsDocShell.h"
#include "nsGenericHTMLElement.h"
#include "nsIXULRuntime.h"
#include "nsPIDOMWindow.h"
#include "nsTHashMap.h"
#include "nsTHashtable.h"
#include "nsLayoutUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {
constexpr auto kInput = u"input"_ns;
constexpr auto kScroll = u"mozvisualscroll"_ns;
constexpr auto kResize = u"mozvisualresize"_ns;

static constexpr char kNoAutoUpdates[] =
    "browser.sessionstore.debug.no_auto_updates";
static constexpr char kInterval[] = "browser.sessionstore.interval";
static const char* kObservedPrefs[] = {kNoAutoUpdates, kInterval, nullptr};
}  // namespace

inline void ImplCycleCollectionUnlink(
    SessionStoreChangeListener::SessionStoreChangeTable& aField) {
  aField.Clear();
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    const SessionStoreChangeListener::SessionStoreChangeTable& aField,
    const char* aName, uint32_t aFlags = 0) {
  for (auto iter = aField.ConstIter(); !iter.Done(); iter.Next()) {
    CycleCollectionNoteChild(aCallback, iter.Key(), aName, aFlags);
  }
}

NS_IMPL_CYCLE_COLLECTION(SessionStoreChangeListener, mBrowsingContext,
                         mCurrentEventTarget, mSessionStoreChild,
                         mSessionStoreChanges)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SessionStoreChangeListener)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(SessionStoreChangeListener)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SessionStoreChangeListener)

NS_IMETHODIMP
SessionStoreChangeListener::GetName(nsACString& aName) {
  aName.AssignLiteral("SessionStoreChangeListener");
  return NS_OK;
}

NS_IMETHODIMP
SessionStoreChangeListener::Notify(nsITimer* aTimer) {
  FlushSessionStore();
  return NS_OK;
}

NS_IMETHODIMP
SessionStoreChangeListener::Observe(nsISupports* aSubject, const char* aTopic,
                                    const char16_t* aData) {
  FlushSessionStore();
  return NS_OK;
}

NS_IMETHODIMP
SessionStoreChangeListener::HandleEvent(dom::Event* aEvent) {
  EventTarget* target = aEvent->GetTarget();
  if (!target) {
    return NS_OK;
  }

  nsIGlobalObject* global = target->GetRelevantGlobal();
  if (!global) {
    return NS_OK;
  }

  nsPIDOMWindowInner* inner = global->GetAsInnerWindow();
  if (!inner) {
    return NS_OK;
  }

  WindowContext* windowContext = inner->GetWindowContext();
  if (!windowContext) {
    return NS_OK;
  }

  BrowsingContext* browsingContext = windowContext->GetBrowsingContext();
  if (!browsingContext) {
    return NS_OK;
  }

  if (browsingContext->IsDynamic()) {
    return NS_OK;
  }

  WidgetEvent* event = aEvent->WidgetEventPtr();
  EventMessage eventMessage = event->mMessage;

  if (eventMessage == eEditorInput ||
      (eventMessage == eUnidentifiedEvent &&
       event->mSpecifiedEventType == nsGkAtoms::oninput)) {
    RecordChange(windowContext, Change::Input);
  } else if (eventMessage == eMozVisualScroll) {
    RecordChange(windowContext, Change::Scroll);
  } else if (eventMessage == eMozVisualResize && browsingContext->IsTop()) {
    RecordChange(windowContext, Change::Resize);
  }
  return NS_OK;
}

/* static */ already_AddRefed<SessionStoreChangeListener>
SessionStoreChangeListener::Create(BrowsingContext* aBrowsingContext) {
  MOZ_RELEASE_ASSERT(SessionStorePlatformCollection());
  if (!aBrowsingContext) {
    return nullptr;
  }

  RefPtr<SessionStoreChangeListener> listener =
      new SessionStoreChangeListener(aBrowsingContext);
  listener->Init();

  return listener.forget();
}

void SessionStoreChangeListener::Stop() {
  RemoveEventListeners();
  Preferences::RemoveObservers(this, kObservedPrefs);
}

void SessionStoreChangeListener::UpdateEventTargets() {
  RemoveEventListeners();
  AddEventListeners();
}

static void CollectFormData(Document* aDocument,
                            Maybe<sessionstore::FormData>& aFormData) {
  aFormData.emplace();
  auto& formData = aFormData.ref();
  uint32_t size = SessionStoreUtils::CollectFormData(aDocument, formData);

  Element* body = aDocument->GetBody();
  if (aDocument->HasFlag(NODE_IS_EDITABLE) && body) {
    IgnoredErrorResult result;
    body->GetInnerHTML(formData.innerHTML(), result);
    size += formData.innerHTML().Length();
    if (!result.Failed()) {
      formData.hasData() = true;
    }
  }

  if (!formData.hasData()) {
    return;
  }

  nsIURI* documentURI = aDocument->GetDocumentURI();
  if (!documentURI) {
    return;
  }

  documentURI->GetSpecIgnoringRef(formData.uri());

  if (size > StaticPrefs::browser_sessionstore_dom_form_max_limit()) {
    aFormData = Nothing();
  }
}

static void GetZoom(BrowsingContext* aBrowsingContext,
                    Maybe<SessionStoreZoom>& aZoom) {
  nsIDocShell* docShell = aBrowsingContext->GetDocShell();
  if (!docShell) {
    return;
  }

  PresShell* presShell = docShell->GetPresShell();
  if (!presShell) {
    return;
  }

  LayoutDeviceIntSize displaySize;

  if (!nsLayoutUtils::GetDocumentViewerSize(presShell->GetPresContext(),
                                            displaySize)) {
    return;
  }

  aZoom.emplace(presShell->GetResolution(), displaySize.width,
                displaySize.height);
}

void SessionStoreChangeListener::FlushSessionStore() {
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  bool collectSessionHistory = false;
  bool collectWireFrame = false;
  bool didResize = false;

  for (auto& iter : mSessionStoreChanges) {
    WindowContext* windowContext = iter.GetKey();
    if (!windowContext) {
      continue;
    }

    BrowsingContext* browsingContext = windowContext->GetBrowsingContext();

    // This is a bit unfortunate, but is needed when a window context with a
    // recorded change has become non-current before its data has been
    // collected. This can happen either due to navigation or destruction, and
    // in the previous case we don't want to collect, but in the latter we do.
    // This could be cleaned up if we change. See bug 1770773.
    if (!windowContext->IsCurrent() && !browsingContext->IsDiscarded()) {
      continue;
    }

    RefPtr<Document> document = windowContext->GetDocument();
    if (!document) {
      continue;
    }

    const EnumSet<Change>& changes = iter.GetData();
    Maybe<sessionstore::FormData> maybeFormData;
    if (changes.contains(Change::Input)) {
      CollectFormData(document, maybeFormData);
    }

    Maybe<nsPoint> maybeScroll;
    PresShell* presShell = document->GetPresShell();

    if (presShell && changes.contains(Change::Scroll)) {
      maybeScroll = Some(presShell->GetVisualViewportOffset());
    }

    collectWireFrame = collectWireFrame || changes.contains(Change::WireFrame);

    collectSessionHistory =
        collectSessionHistory || changes.contains(Change::SessionHistory);

    if (presShell && changes.contains(Change::Resize)) {
      didResize = true;
    }

    mSessionStoreChild->IncrementalSessionStoreUpdate(
        browsingContext, maybeFormData, maybeScroll, mEpoch);
  }

  if (collectWireFrame) {
    collectSessionHistory = CollectWireframe() || collectSessionHistory;
  }

  mSessionStoreChanges.Clear();

  Maybe<SessionStoreZoom> zoom;
  if (didResize) {
    GetZoom(mBrowsingContext->Top(), zoom);
  }

  mSessionStoreChild->UpdateSessionStore(collectSessionHistory, zoom);
}

/* static */
SessionStoreChangeListener* SessionStoreChangeListener::CollectSessionStoreData(
    WindowContext* aWindowContext, const EnumSet<Change>& aChanges) {
  SessionStoreChild* sessionStoreChild =
      SessionStoreChild::From(aWindowContext->GetWindowGlobalChild());
  if (!sessionStoreChild) {
    return nullptr;
  }

  SessionStoreChangeListener* sessionStoreChangeListener =
      sessionStoreChild->GetSessionStoreChangeListener();

  if (!sessionStoreChangeListener) {
    return nullptr;
  }

  sessionStoreChangeListener->RecordChange(aWindowContext, aChanges);

  return sessionStoreChangeListener;
}

void SessionStoreChangeListener::SetActor(
    SessionStoreChild* aSessionStoreChild) {
  mSessionStoreChild = aSessionStoreChild;
}

bool SessionStoreChangeListener::CollectWireframe() {
  if (auto* docShell = nsDocShell::Cast(mBrowsingContext->GetDocShell())) {
    return docShell->CollectWireframe();
  }

  return false;
}

void SessionStoreChangeListener::RecordChange(WindowContext* aWindowContext,
                                              EnumSet<Change> aChange) {
  EnsureTimer();

  (void)mSessionStoreChanges.WithEntryHandle(
      aWindowContext, [&](auto entryHandle) -> EnumSet<Change>& {
        if (entryHandle) {
          *entryHandle += aChange;
          return *entryHandle;
        }

        return entryHandle.Insert(aChange);
      });
}

SessionStoreChangeListener::SessionStoreChangeListener(
    BrowsingContext* aBrowsingContext)
    : mBrowsingContext(aBrowsingContext),
      mEpoch(aBrowsingContext->GetSessionStoreEpoch()) {}

void SessionStoreChangeListener::Init() {
  AddEventListeners();

  Preferences::AddStrongObservers(this, kObservedPrefs);
}

EventTarget* SessionStoreChangeListener::GetEventTarget() {
  if (mBrowsingContext->GetDOMWindow()) {
    return mBrowsingContext->GetDOMWindow()->GetChromeEventHandler();
  }

  return nullptr;
}

void SessionStoreChangeListener::AddEventListeners() {
  if (EventTarget* target = GetEventTarget()) {
    target->AddSystemEventListener(kInput, this, false);
    target->AddSystemEventListener(kScroll, this, false);
    target->AddSystemEventListener(kResize, this, false);
    mCurrentEventTarget = target;
  }
}

void SessionStoreChangeListener::RemoveEventListeners() {
  if (mCurrentEventTarget) {
    mCurrentEventTarget->RemoveSystemEventListener(kInput, this, false);
    mCurrentEventTarget->RemoveSystemEventListener(kScroll, this, false);
    mCurrentEventTarget->RemoveSystemEventListener(kResize, this, false);
  }

  mCurrentEventTarget = nullptr;
}

void SessionStoreChangeListener::EnsureTimer() {
  if (mTimer) {
    return;
  }

  if (!StaticPrefs::browser_sessionstore_debug_no_auto_updates()) {
    auto result = NS_NewTimerWithCallback(
        this, StaticPrefs::browser_sessionstore_interval(),
        nsITimer::TYPE_ONE_SHOT);
    if (result.isErr()) {
      return;
    }

    mTimer = result.unwrap();
  }
}
