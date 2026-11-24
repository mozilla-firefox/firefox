/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientSource.h"

#include "ClientManager.h"
#include "ClientManagerChild.h"
#include "ClientPrincipalUtils.h"
#include "ClientSourceChild.h"
#include "ClientState.h"
#include "ClientValidation.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/Try.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ServiceWorker.h"
#include "mozilla/dom/ServiceWorkerContainer.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIDocShell.h"
#include "nsPIDOMWindow.h"

namespace mozilla::dom {

using mozilla::dom::ipc::StructuredCloneData;
using mozilla::ipc::CSPInfo;
using mozilla::ipc::CSPToCSPInfo;
using mozilla::ipc::PrincipalInfo;
using mozilla::ipc::PrincipalInfoToPrincipal;

void ClientSource::Shutdown() {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (IsShutdown()) {
    return;
  }

  ShutdownThing();

  mManager = nullptr;
}

void ClientSource::ExecutionReady(const ClientSourceExecutionReadyArgs& aArgs) {
  // Fast fail if we don't understand this particular principal/URL combination.
  // This can happen since we use MozURL for validation which does not handle
  // some of the more obscure internal principal/url combinations.  Normal
  // content pages will pass this check.
  if (NS_WARN_IF(!ClientIsValidCreationURL(mClientInfo.PrincipalInfo(),
                                           aArgs.url()))) {
    Shutdown();
    return;
  }

  mClientInfo.SetURL(aArgs.url());
  mClientInfo.SetFrameType(aArgs.frameType());
  MaybeExecute([aArgs](PClientSourceChild* aActor) {
    aActor->SendExecutionReady(aArgs);
  });
}

Result<ClientState, ErrorResult> ClientSource::SnapshotWindowState() {
  MOZ_ASSERT(NS_IsMainThread());

  nsPIDOMWindowInner* window = GetInnerWindow();
  if (!window || !window->IsCurrentInnerWindow() ||
      !window->HasActiveDocument()) {
    return ClientState(ClientWindowState(VisibilityState::Hidden, TimeStamp(),
                                         StorageAccess::eDeny, false));
  }

  Document* doc = window->GetExtantDoc();
  ErrorResult rv;
  if (NS_WARN_IF(!doc)) {
    rv.ThrowInvalidStateError("Document not active");
    return Err(std::move(rv));
  }

  bool focused = doc->HasFocus(rv);
  if (NS_WARN_IF(rv.Failed())) {
    return Err(std::move(rv));
  }

  StorageAccess storage = StorageAllowedForDocument(doc);

  return ClientState(ClientWindowState(doc->VisibilityState(),
                                       doc->LastFocusTime(), storage, focused));
}

WorkerPrivate* ClientSource::GetWorkerPrivate() const {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (!mOwner.is<WorkerPrivate*>()) {
    return nullptr;
  }
  return mOwner.as<WorkerPrivate*>();
}

nsIDocShell* ClientSource::GetDocShell() const {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (!mOwner.is<nsCOMPtr<nsIDocShell>>()) {
    return nullptr;
  }
  return mOwner.as<nsCOMPtr<nsIDocShell>>();
}

nsIGlobalObject* ClientSource::GetGlobal() const {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  nsPIDOMWindowInner* win = GetInnerWindow();
  if (win) {
    return win->AsGlobal();
  }

  WorkerPrivate* wp = GetWorkerPrivate();
  if (wp) {
    return wp->GlobalScope();
  }

  // Note, ClientSource objects attached to docshell for conceptual
  // initial about:blank will get nullptr here.  The caller should
  // use MaybeCreateIntitialDocument() to create the window before
  // GetGlobal() if it wants this before.

  return nullptr;
}

// We want to be explicit about possible invalid states and
// return them as errors.
Result<bool, ErrorResult> ClientSource::MaybeCreateInitialDocument() {
  // If there is not even a docshell, we do not expect to have a document
  nsIDocShell* docshell = GetDocShell();
  if (!docshell) {
    return false;
  }

  // Force the creation of the initial document if it does not yet exist.
  if (!docshell->GetDocument()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("No document available.");
    return Err(std::move(rv));
  }

  return true;
}

ClientSource::ClientSource(ClientManager* aManager,
                           nsISerialEventTarget* aEventTarget,
                           const ClientSourceConstructorArgs& aArgs)
    : mManager(aManager),
      mEventTarget(aEventTarget),
      mOwner(AsVariant(Nothing())),
      mClientInfo(aArgs.id(), aArgs.agentClusterId(), aArgs.type(),
                  aArgs.principalInfo(), aArgs.creationTime(), aArgs.url(),
                  aArgs.frameType()) {
  MOZ_ASSERT(mManager);
  MOZ_ASSERT(mEventTarget);
}

void ClientSource::Activate(PClientManagerChild* aActor) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  MOZ_ASSERT(!GetActor());

  if (IsShutdown()) {
    return;
  }

  // Fast fail if we don't understand this particular kind of PrincipalInfo.
  // This can happen since we use MozURL for validation which does not handle
  // some of the more obscure internal principal/url combinations.  Normal
  // content pages will pass this check.
  RefPtr<LoadedOriginSet> loadedOrigins = CurrentLoadedOriginSet();
  if (NS_WARN_IF(!ClientIsValidPrincipalInfo(mClientInfo.PrincipalInfo(),
                                             loadedOrigins))) {
    Shutdown();
    return;
  }

  ClientSourceConstructorArgs args(
      mClientInfo.Id(), Nothing(), mClientInfo.Type(),
      mClientInfo.PrincipalInfo(), mClientInfo.CreationTime(), VoidCString(),
      FrameType::None);
  RefPtr<ClientSourceChild> actor = new ClientSourceChild(args);
  if (!aActor->SendPClientSourceConstructor(actor, args)) {
    Shutdown();
    return;
  }

  ActivateThing(actor);
}

ClientSource::~ClientSource() { Shutdown(); }

nsPIDOMWindowInner* ClientSource::GetInnerWindow() const {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (!mOwner.is<RefPtr<nsPIDOMWindowInner>>()) {
    return nullptr;
  }
  return mOwner.as<RefPtr<nsPIDOMWindowInner>>();
}

void ClientSource::WorkerExecutionReady(WorkerPrivate* aWorkerPrivate) {
  MOZ_DIAGNOSTIC_ASSERT(aWorkerPrivate);
  aWorkerPrivate->AssertIsOnWorkerThread();

  if (IsShutdown()) {
    return;
  }

  // Its safe to store the WorkerPrivate* here because the ClientSource
  // is explicitly destroyed by WorkerPrivate before exiting its run loop.
  //
  // We need to initialize our owner before the call to
  // ServiceWorkersStorageAllowedForGlobal below which will use SnapshotState to
  // retrieve our StorageAccess.
  MOZ_DIAGNOSTIC_ASSERT(mOwner.is<Nothing>());
  mOwner = AsVariant(aWorkerPrivate);

  // A client without access to storage should never be controlled by
  // a service worker.  Check this here in case we were controlled before
  // execution ready.  We can't reliably determine what our storage policy
  // is before execution ready, unfortunately.
  if (mController.isSome()) {
    MOZ_DIAGNOSTIC_ASSERT(
        ServiceWorkersStorageAllowedForGlobal(aWorkerPrivate->GlobalScope()) ||
        StringBeginsWith(aWorkerPrivate->ScriptURL(), u"blob:"_ns));
  }

  nsCString spec;
  (void)aWorkerPrivate->GetBaseURI()->GetSpec(spec);

  ClientSourceExecutionReadyArgs args(spec, FrameType::None);

  ExecutionReady(args);
}

nsresult ClientSource::WindowExecutionReady(nsPIDOMWindowInner* aInnerWindow) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aInnerWindow);
  MOZ_ASSERT(aInnerWindow->IsCurrentInnerWindow());
  MOZ_ASSERT(aInnerWindow->HasActiveDocument());

  if (IsShutdown()) {
    return NS_OK;
  }

  Document* doc = aInnerWindow->GetExtantDoc();
  NS_ENSURE_TRUE(doc, NS_ERROR_DOM_INVALID_STATE_ERR);

  nsIURI* uri = doc->GetOriginalURI();
  NS_ENSURE_TRUE(uri, NS_ERROR_NOT_AVAILABLE);

  // Don't use nsAutoCString here since IPC requires a full nsCString anyway.
  nsCString spec;
  nsresult rv = uri->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  // A client without access to storage should never be controlled by
  // a service worker.  Check this here in case we were controlled before
  // execution ready.  We can't reliably determine what our storage policy
  // is before execution ready, unfortunately.
  //
  // Note, explicitly avoid checking storage policy for windows that inherit
  // service workers from their parent.  If a user opens a controlled window
  // and then blocks storage, that window will continue to be controlled by
  // the SW until the window is closed.  Any about:blank or blob URL should
  // continue to inherit the SW as well.  We need to avoid triggering the
  // assertion in this corner case.
#ifdef DEBUG
  if (mController.isSome()) {
    bool isAboutBlankURL = spec.LowerCaseEqualsLiteral("about:blank");
    bool isBlobURL = StringBeginsWith(spec, "blob:"_ns);
    bool isStorageAllowed =
        ServiceWorkersStorageAllowedForGlobal(aInnerWindow->AsGlobal());
    MOZ_ASSERT(isAboutBlankURL || isBlobURL || isStorageAllowed);
  }
#endif

  nsPIDOMWindowOuter* outer = aInnerWindow->GetOuterWindow();
  NS_ENSURE_TRUE(outer, NS_ERROR_UNEXPECTED);

  FrameType frameType = FrameType::Top_level;
  if (!outer->GetBrowsingContext()->IsTop()) {
    frameType = FrameType::Nested;
  } else if (outer->GetBrowsingContext()->HadOriginalOpener()) {
    frameType = FrameType::Auxiliary;
  }

  // We should either be setting a window execution ready for the
  // first time or setting the same window execution ready again.
  // The secondary calls are due to initial about:blank replacement.
  MOZ_DIAGNOSTIC_ASSERT(mOwner.is<Nothing>() ||
                        mOwner.is<nsCOMPtr<nsIDocShell>>() ||
                        GetInnerWindow() == aInnerWindow);

  // This creates a cycle with the window.  It is broken when
  // nsGlobalWindow::FreeInnerObjects() deletes the ClientSource.
  mOwner = AsVariant(RefPtr<nsPIDOMWindowInner>(aInnerWindow));

  ClientSourceExecutionReadyArgs args(spec, frameType);
  ExecutionReady(args);

  return NS_OK;
}

nsresult ClientSource::DocShellExecutionReady(nsIDocShell* aDocShell) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aDocShell);

  if (IsShutdown()) {
    return NS_OK;
  }

  nsPIDOMWindowOuter* outer = aDocShell->GetWindow();
  if (NS_WARN_IF(!outer)) {
    return NS_ERROR_UNEXPECTED;
  }

  // Note: We don't assert storage access for a controlled client.  If
  // the about:blank actually gets used then WindowExecutionReady() will
  // get called which asserts storage access.

  // TODO: dedupe this with WindowExecutionReady
  FrameType frameType = FrameType::Top_level;
  if (!outer->GetBrowsingContext()->IsTop()) {
    frameType = FrameType::Nested;
  } else if (outer->GetBrowsingContext()->HadOriginalOpener()) {
    frameType = FrameType::Auxiliary;
  }

  MOZ_DIAGNOSTIC_ASSERT(mOwner.is<Nothing>());

  // This creates a cycle with the docshell.  It is broken when
  // nsDocShell::Destroy() deletes the ClientSource.
  mOwner = AsVariant(nsCOMPtr<nsIDocShell>(aDocShell));

  ClientSourceExecutionReadyArgs args("about:blank"_ns, frameType);
  ExecutionReady(args);

  return NS_OK;
}

void ClientSource::Freeze() {
  MaybeExecute([](PClientSourceChild* aActor) { aActor->SendFreeze(); });
}

void ClientSource::Thaw() {
  MaybeExecute([](PClientSourceChild* aActor) { aActor->SendThaw(); });
}

void ClientSource::EvictFromBFCache() {
  if (nsCOMPtr<nsPIDOMWindowInner> win = GetInnerWindow()) {
    win->RemoveFromBFCacheSync();
  } else if (WorkerPrivate* vp = GetWorkerPrivate()) {
    vp->EvictFromBFCache();
  }
}

RefPtr<ClientOpPromise> ClientSource::EvictFromBFCacheOp() {
  EvictFromBFCache();
  return ClientOpPromise::CreateAndResolve(CopyableErrorResult(), __func__);
}

const ClientInfo& ClientSource::Info() const { return mClientInfo; }

void ClientSource::WorkerSyncPing(WorkerPrivate* aWorkerPrivate) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  MOZ_DIAGNOSTIC_ASSERT(aWorkerPrivate);

  if (IsShutdown()) {
    return;
  }

  // We need to make sure the mainthread is unblocked.
  AutoYieldJSThreadExecution yield;

  MOZ_DIAGNOSTIC_ASSERT(aWorkerPrivate == mManager->GetWorkerPrivate());
  aWorkerPrivate->AssertIsOnWorkerThread();
  MOZ_DIAGNOSTIC_ASSERT(GetActor());

  GetActor()->SendWorkerSyncPing();
}

void ClientSource::SetController(
    const ServiceWorkerDescriptor& aServiceWorker) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  // We should never have a cross-origin controller.  Since this would be
  // same-origin policy violation we do a full release assertion here.
  MOZ_RELEASE_ASSERT(ClientMatchPrincipalInfo(mClientInfo.PrincipalInfo(),
                                              aServiceWorker.PrincipalInfo()));

  // A client without access to storage should never be controlled a
  // a service worker.  If we are already execution ready with a real
  // window or worker, then verify assert the storage policy is correct.
  //
  // Note, explicitly avoid checking storage policy for clients that inherit
  // service workers from their parent.  This basically means blob: URLs
  // and about:blank windows.
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (GetInnerWindow()) {
    bool IsAboutBlankURL = Info().URL().LowerCaseEqualsLiteral("about:blank");
    bool IsBlobURL = StringBeginsWith(Info().URL(), "blob:"_ns);
    bool IsStorageAllowed = ServiceWorkersStorageAllowedForGlobal(GetGlobal());
    MOZ_DIAGNOSTIC_ASSERT(IsAboutBlankURL || IsBlobURL || IsStorageAllowed);
  } else if (GetWorkerPrivate()) {
    MOZ_DIAGNOSTIC_ASSERT(
        ServiceWorkersStorageAllowedForGlobal(GetGlobal()) ||
        StringBeginsWith(GetWorkerPrivate()->ScriptURL(), u"blob:"_ns));
  }
#endif

  if (mController.isSome() && mController.ref() == aServiceWorker) {
    return;
  }

  mController.reset();
  mController.emplace(aServiceWorker);

  RefPtr<ServiceWorkerContainer> swc;
  nsPIDOMWindowInner* window = GetInnerWindow();
  if (window) {
    swc = window->Navigator()->ServiceWorker();
  }

  // TODO: Also self.navigator.serviceWorker on workers when its exposed there

  if (swc && nsContentUtils::IsSafeToRunScript()) {
    swc->ControllerChanged(IgnoreErrors());
  }
}

RefPtr<ClientOpPromise> ClientSource::Control(
    const ClientControlledArgs& aArgs) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  // Determine if the client is allowed to be controlled.  Currently we
  // prevent service workers from controlling clients that cannot access
  // storage.  We exempt this restriction for local URL clients, like
  // about:blank and blob:, since access to service workers is dictated by their
  // parent.
  //
  // Note, we default to allowing the client to be controlled in the case
  // where we are not execution ready yet.  This can only happen if the
  // the non-subresource load is intercepted by a service worker.  Since
  // ServiceWorkerInterceptController() uses StorageAllowedForChannel()
  // it should be fine to accept these control messages.
  //
  // Its also fine to default to allowing ClientSource attached to a docshell
  // to be controlled.  These clients represent inital about:blank windows
  // that do not have an inner window created yet.  We explicitly allow initial
  // about:blank.
  bool controlAllowed = true;
  if (GetInnerWindow()) {
    // Local URL windows and windows with access to storage can be controlled.
    bool isAboutBlankURL = Info().URL().LowerCaseEqualsLiteral("about:blank");
    bool isBlobURL = StringBeginsWith(Info().URL(), "blob:"_ns);
    bool isStorageAllowed = ServiceWorkersStorageAllowedForGlobal(GetGlobal());
    controlAllowed = isAboutBlankURL || isBlobURL || isStorageAllowed;
  } else if (GetWorkerPrivate()) {
    // Local URL workers and workers with access to storage can be controlled.
    controlAllowed =
        ServiceWorkersStorageAllowedForGlobal(GetGlobal()) ||
        StringBeginsWith(GetWorkerPrivate()->ScriptURL(), u"blob:"_ns);
  }

  if (NS_WARN_IF(!controlAllowed)) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Client cannot be controlled");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }

  SetController(ServiceWorkerDescriptor(aArgs.serviceWorker()));

  return ClientOpPromise::CreateAndResolve(CopyableErrorResult(), __func__);
}

void ClientSource::InheritController(
    const ServiceWorkerDescriptor& aServiceWorker) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  // Tell the parent-side ClientManagerService that the controller was
  // inherited.  This is necessary for clients.matchAll() to work properly.
  // In parent-side intercept mode this will also note the inheritance in
  // the parent-side SWM.
  MaybeExecute([aServiceWorker](PClientSourceChild* aActor) {
    aActor->SendInheritController(ClientControlledArgs(aServiceWorker.ToIPC()));
  });

  // Finally, record the new controller in our local ClientSource for any
  // immediate synchronous access.
  SetController(aServiceWorker);
}

const Maybe<ServiceWorkerDescriptor>& ClientSource::GetController() const {
  return mController;
}

void ClientSource::NoteDOMContentLoaded() {
  MaybeExecute(
      [](PClientSourceChild* aActor) { aActor->SendNoteDOMContentLoaded(); });
}

RefPtr<ClientOpPromise> ClientSource::Focus(const ClientFocusArgs& aArgs) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  if (mClientInfo.Type() != ClientType::Window) {
    CopyableErrorResult rv;
    rv.ThrowNotSupportedError("Not a Window client");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }
  nsCOMPtr<nsPIDOMWindowOuter> outer;
  nsCOMPtr<nsPIDOMWindowInner> inner = GetInnerWindow();
  nsIDocShell* docshell = nullptr;
  if (inner) {
    outer = inner->GetOuterWindow();
  } else {
    docshell = GetDocShell();
    if (docshell) {
      outer = docshell->GetWindow();
    }
  }

  if (!outer) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Browsing context discarded");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }

  MOZ_ASSERT(NS_IsMainThread());

  // Inlined from `ClientSource::SnapshotWindowState()`:
  // Should not be necessary after bug 543435. Clean this up in bug 2025284.
  if (docshell) {
    // Force the creation of the initial document if it does not yet exist.
    if (!docshell->GetDocument()) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("No document available.");
      return ClientOpPromise::CreateAndReject(rv, __func__);
    }
    inner = GetInnerWindow();
  }

  nsFocusManager::FocusWindow(outer, aArgs.callerType());

  Result<ClientState, ErrorResult> state =
      [&]() -> Result<ClientState, ErrorResult> {
    if (!inner) {
      // Inlined from `ClientSource::SnapshotWindowState()`:
      return ClientState(ClientWindowState(VisibilityState::Hidden, TimeStamp(),
                                           StorageAccess::eDeny, false));
    }
    if (inner->GetClientSource() == this) {
      // The pointer comparison assumes that an inner window
      // cannot gain a new ClientSource other than this same
      // `ClientSource` having moved from a docshell owner to an
      // inner window owner gained via `outer`, so we don't need to worry
      // about a newly-allocated ClientSource occupying the same
      // memory as the one pointed to by `this`. That is, in the case
      // of the pointers being unequal, `inner->GetClientSource()`
      // returns `nullptr` and `this` is an invalid pointer.
      // Per [expr.eq], it's not UB to compare a pointer to a deleted
      // object, since no pointer comparisons are UB anymore. The
      // case about pointer-past-end for a different object being
      // _unspecified_ behavior does not apply here.
      return SnapshotState();
    }
    ErrorResult rv;
    rv.ThrowInvalidStateError("Client destroyed during focus");
    return Err(std::move(rv));
  }();

  if (state.isErr()) {
    return ClientOpPromise::CreateAndReject(
        CopyableErrorResult(state.unwrapErr()), __func__);
  }

  return ClientOpPromise::CreateAndResolve(state.inspect().ToIPC(), __func__);
}

RefPtr<ClientOpPromise> ClientSource::PostMessage(
    const ClientPostMessageArgs& aArgs) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  // (This only returns a global for windows or workers.)
  nsIGlobalObject* global = GetGlobal();
  if (global) {
    const RefPtr<ServiceWorkerContainer> container =
        global->GetServiceWorkerContainer();
    MOZ_ASSERT_DEBUG_OR_FUZZING(container);

    // Note, EvictFromBFCache() may delete the ClientSource object
    // when bfcache lives in the child process.
    EvictFromBFCache();
    container->ReceiveMessage(aArgs);
    return ClientOpPromise::CreateAndResolve(CopyableErrorResult(), __func__);
  }

  CopyableErrorResult rv;
  rv.ThrowInvalidStateError("Global discarded");
  return ClientOpPromise::CreateAndReject(rv, __func__);
}

RefPtr<ClientOpPromise> ClientSource::GetInfoAndState(
    const ClientGetInfoAndStateArgs& aArgs) {
  Result<ClientState, ErrorResult> state = SnapshotState();
  if (state.isErr()) {
    return ClientOpPromise::CreateAndReject(
        CopyableErrorResult(state.unwrapErr()), __func__);
  }

  return ClientOpPromise::CreateAndResolve(
      ClientInfoAndState(mClientInfo.ToIPC(), state.inspect().ToIPC()),
      __func__);
}

Result<ClientState, ErrorResult> ClientSource::SnapshotState() {
  NS_ASSERT_OWNINGTHREAD(ClientSource);

  if (mClientInfo.Type() == ClientType::Window) {
    // If there is a docshell, try to create a document, too.
    MOZ_TRY(MaybeCreateInitialDocument());
    // SnapshotWindowState can deal with a missing inner window
    return SnapshotWindowState();
  }

  WorkerPrivate* workerPrivate = GetWorkerPrivate();
  if (!workerPrivate) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Worker terminated");
    return Err(std::move(rv));
  }

  return ClientState(ClientWorkerState(workerPrivate->StorageAccess()));
}

nsISerialEventTarget* ClientSource::EventTarget() const { return mEventTarget; }

void ClientSource::SetPolicyContainer(nsIPolicyContainer* aPolicyContainer) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (!aPolicyContainer) {
    return;
  }

  mozilla::ipc::PolicyContainerArgs policyContainerArgs;
  PolicyContainer::ToArgs(PolicyContainer::Cast(aPolicyContainer),
                          policyContainerArgs);

  SetPolicyContainerArgs(policyContainerArgs);
}

void ClientSource::SetPreloadCsp(nsIContentSecurityPolicy* aPreloadCsp) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  if (!aPreloadCsp) {
    return;
  }

  CSPInfo cspPreloadInfo;
  nsresult rv = CSPToCSPInfo(aPreloadCsp, &cspPreloadInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  mClientInfo.SetPreloadCspInfo(cspPreloadInfo);
}

void ClientSource::SetPolicyContainerArgs(
    const mozilla::ipc::PolicyContainerArgs& aPolicyContainer) {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  mClientInfo.SetPolicyContainerArgs(aPolicyContainer);
}

const Maybe<mozilla::ipc::PolicyContainerArgs>&
ClientSource::GetPolicyContainerArgs() {
  NS_ASSERT_OWNINGTHREAD(ClientSource);
  return mClientInfo.GetPolicyContainerArgs();
}

void ClientSource::Traverse(nsCycleCollectionTraversalCallback& aCallback,
                            const char* aName, uint32_t aFlags) {
  if (mOwner.is<RefPtr<nsPIDOMWindowInner>>()) {
    ImplCycleCollectionTraverse(
        aCallback, mOwner.as<RefPtr<nsPIDOMWindowInner>>(), aName, aFlags);
  } else if (mOwner.is<nsCOMPtr<nsIDocShell>>()) {
    ImplCycleCollectionTraverse(aCallback, mOwner.as<nsCOMPtr<nsIDocShell>>(),
                                aName, aFlags);
  }
}

void ClientSource::NoteCalledRegisterForServiceWorkerScope(
    const nsACString& aScope) {
  if (mRegisteringScopeList.Contains(aScope)) {
    return;
  }
  mRegisteringScopeList.AppendElement(aScope);
}

bool ClientSource::CalledRegisterForServiceWorkerScope(
    const nsACString& aScope) {
  return mRegisteringScopeList.Contains(aScope);
}

nsIPrincipal* ClientSource::GetPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());

  // We only create the principal if necessary because creating a principal is
  // expensive.
  if (!mPrincipal) {
    auto principalOrErr = Info().GetPrincipal();
    nsCOMPtr<nsIPrincipal> prin =
        principalOrErr.isOk() ? principalOrErr.unwrap() : nullptr;

    mPrincipal.emplace(prin);
  }

  return mPrincipal.ref();
}

}  // namespace mozilla::dom
