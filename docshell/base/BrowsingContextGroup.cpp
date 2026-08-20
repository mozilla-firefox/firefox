/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BrowsingContextGroup.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/ThrottledEventQueue.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "nsFocusManager.h"
#include "nsTHashMap.h"

namespace mozilla::dom {

// Maximum number of successive dialogs before we prompt users to disable
// dialogs for this window.
#define MAX_SUCCESSIVE_DIALOG_COUNT 5

static StaticRefPtr<BrowsingContextGroup> sChromeGroup;

static StaticAutoPtr<nsTHashMap<uint64_t, RefPtr<BrowsingContextGroup>>>
    sBrowsingContextGroups;

already_AddRefed<BrowsingContextGroup> BrowsingContextGroup::GetOrCreate(
    uint64_t aId) {
  if (!sBrowsingContextGroups) {
    sBrowsingContextGroups =
        new nsTHashMap<nsUint64HashKey, RefPtr<BrowsingContextGroup>>();
    ClearOnShutdown(&sBrowsingContextGroups);
  }

  return do_AddRef(sBrowsingContextGroups->LookupOrInsertWith(
      aId, [&aId] { return do_AddRef(new BrowsingContextGroup(aId)); }));
}

already_AddRefed<BrowsingContextGroup> BrowsingContextGroup::GetExisting(
    uint64_t aId) {
  if (sBrowsingContextGroups) {
    return do_AddRef(sBrowsingContextGroups->Get(aId));
  }
  return nullptr;
}

// Only use 53 bits for the BrowsingContextGroup ID.
static constexpr uint64_t kBrowsingContextGroupIdTotalBits = 53;
static constexpr uint64_t kBrowsingContextGroupIdProcessBits = 22;
static constexpr uint64_t kBrowsingContextGroupIdFlagBits = 1;
static constexpr uint64_t kBrowsingContextGroupIdBits =
    kBrowsingContextGroupIdTotalBits - kBrowsingContextGroupIdProcessBits -
    kBrowsingContextGroupIdFlagBits;

// IDs for the relevant flags
static constexpr uint64_t kPotentiallyCrossOriginIsolatedFlag = 0x1;

// The next ID value which will be used.
static uint64_t sNextBrowsingContextGroupId = 1;

// Generate the next ID with the given flags.
static uint64_t GenerateBrowsingContextGroupId(uint64_t aFlags) {
  MOZ_RELEASE_ASSERT(aFlags < (uint64_t(1) << kBrowsingContextGroupIdFlagBits));
  uint64_t childId = XRE_IsContentProcess()
                         ? ContentChild::GetSingleton()->GetID()
                         : uint64_t(0);
  MOZ_RELEASE_ASSERT(childId <
                     (uint64_t(1) << kBrowsingContextGroupIdProcessBits));
  uint64_t id = sNextBrowsingContextGroupId++;
  MOZ_RELEASE_ASSERT(id < (uint64_t(1) << kBrowsingContextGroupIdBits));

  return (childId << (kBrowsingContextGroupIdBits +
                      kBrowsingContextGroupIdFlagBits)) |
         (id << kBrowsingContextGroupIdFlagBits) | aFlags;
}

// Extract flags from the given ID.
static uint64_t GetBrowsingContextGroupIdFlags(uint64_t aId) {
  return aId & ((uint64_t(1) << kBrowsingContextGroupIdFlagBits) - 1);
}

uint64_t BrowsingContextGroup::CreateId(bool aPotentiallyCrossOriginIsolated) {
  // We encode the potentially cross-origin isolated bit within the ID so that
  // the information can be recovered whenever the group needs to be re-created
  // due to e.g. being garbage-collected.
  //
  // In the future if we end up needing more complex information stored within
  // the ID, we can consider converting it to a more complex type, like a
  // string.
  uint64_t flags =
      aPotentiallyCrossOriginIsolated ? kPotentiallyCrossOriginIsolatedFlag : 0;
  uint64_t id = GenerateBrowsingContextGroupId(flags);
  MOZ_ASSERT(GetBrowsingContextGroupIdFlags(id) == flags);
  return id;
}

already_AddRefed<BrowsingContextGroup> BrowsingContextGroup::Create(
    bool aPotentiallyCrossOriginIsolated) {
  return GetOrCreate(CreateId(aPotentiallyCrossOriginIsolated));
}

BrowsingContextGroup::BrowsingContextGroup(uint64_t aId) : mId(aId) {
  mTimerEventQueue = ThrottledEventQueue::Create(
      GetMainThreadSerialEventTarget(), "BrowsingContextGroup timer queue");

  mWorkerEventQueue = ThrottledEventQueue::Create(
      GetMainThreadSerialEventTarget(), "BrowsingContextGroup worker queue");
}

void BrowsingContextGroup::Register(nsISupports* aContext) {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(aContext);
  mContexts.Insert(aContext);
}

void BrowsingContextGroup::Unregister(nsISupports* aContext) {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(aContext);
  mContexts.Remove(aContext);

  MaybeDestroy();
}

void BrowsingContextGroup::EnsureHostProcess(ContentParent* aProcess) {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(this != sChromeGroup,
                        "cannot have content host for chrome group");
  MOZ_DIAGNOSTIC_ASSERT(aProcess->GetRemoteType() != PREALLOC_REMOTE_TYPE,
                        "cannot use preallocated process as host");
  MOZ_DIAGNOSTIC_ASSERT(!aProcess->GetRemoteType().IsEmpty(),
                        "host process must have remote type");

  // XXX: The diagnostic crashes in bug 1816025 seemed to come through caller
  // ContentParent::GetNewOrUsedLaunchingBrowserProcess where we already
  // did AssertAlive, so IsDead should be irrelevant here. Still it reads
  // wrong that we ever might do AddBrowsingContextGroup if aProcess->IsDead().
  if (aProcess->IsDead() ||
      mHosts.WithEntryHandle(aProcess->GetRemoteType(), [&](auto&& entry) {
        if (entry) {
          // We know from bug 1816025 that this happens quite often and we have
          // bug 1815480 on file that should harden the entire flow. But in the
          // meantime we can just live with NOT replacing the found host
          // process with a new one here if it is still alive.
          MOZ_ASSERT(
              entry.Data() == aProcess,
              "There's already another host process for this remote type");
          if (!entry.Data()->IsShuttingDown()) {
            return false;
          }
        }

        // This process wasn't already marked as our host, so insert it (or
        // update if the old process is shutting down), and begin subscribing,
        // unless the process is still launching.
        entry.InsertOrUpdate(do_AddRef(aProcess));

        return true;
      })) {
    aProcess->AddBrowsingContextGroup(this);
  }
}

void BrowsingContextGroup::RemoveHostProcess(ContentParent* aProcess) {
  MOZ_DIAGNOSTIC_ASSERT(aProcess);
  MOZ_DIAGNOSTIC_ASSERT(aProcess->GetRemoteType() != PREALLOC_REMOTE_TYPE);
  auto entry = mHosts.Lookup(aProcess->GetRemoteType());
  if (entry && entry.Data() == aProcess) {
    entry.Remove();
  }
}

static void CollectContextInitializers(
    Span<RefPtr<BrowsingContext>> aContexts,
    nsTArray<SyncedContextInitializer>& aInits) {
  // The order that we record these initializers is important, as it will keep
  // the order that children are attached to their parent in the newly connected
  // content process consistent.
  for (auto& context : aContexts) {
    aInits.AppendElement(context->GetIPCInitializer());
    for (const auto& window : context->GetWindowContexts()) {
      aInits.AppendElement(window->GetIPCInitializer());
      CollectContextInitializers(window->Children(), aInits);
    }
  }
}

void BrowsingContextGroup::Subscribe(ContentParent* aProcess) {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(aProcess && !aProcess->IsLaunching());
  MOZ_DIAGNOSTIC_ASSERT(aProcess->GetRemoteType() != PREALLOC_REMOTE_TYPE);

  // Check if we're already subscribed to this process.
  if (!mSubscribers.EnsureInserted(aProcess)) {
    return;
  }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  // If the process is already marked as dead, we won't be the host, but may
  // still need to subscribe to the process due to creating a popup while
  // shutting down.
  if (!aProcess->IsDead()) {
    auto hostEntry = mHosts.Lookup(aProcess->GetRemoteType());
    MOZ_DIAGNOSTIC_ASSERT(hostEntry && hostEntry.Data() == aProcess,
                          "Cannot subscribe a non-host process");
  }
#endif

  // FIXME: This won't send non-discarded children of discarded BCs, but those
  // BCs will be in the process of being destroyed anyway.
  // FIXME: Prevent that situation from occuring.
  nsTArray<SyncedContextInitializer> inits(mContexts.Count());
  CollectContextInitializers(mToplevels, inits);

  nsTArray<OriginAgentClusterInitializer> useOriginAgentCluster;
  for (auto& entry : mUseOriginAgentCluster) {
    if (!aProcess->ValidatePrincipal(
            entry.GetKey(), {ValidatePrincipalOptions::AllowNotLoadedOrigin})) {
      continue;
    }

    useOriginAgentCluster.AppendElement(OriginAgentClusterInitializer(
        WrapNotNull(RefPtr{entry.GetKey()}), entry.GetData()));
  }

  // Send all of our contexts to the target content process.
  (void)aProcess->SendRegisterBrowsingContextGroup(Id(), inits,
                                                   useOriginAgentCluster);
}

void BrowsingContextGroup::Unsubscribe(ContentParent* aProcess) {
  MOZ_DIAGNOSTIC_ASSERT(aProcess);
  MOZ_DIAGNOSTIC_ASSERT(aProcess->GetRemoteType() != PREALLOC_REMOTE_TYPE);
  mSubscribers.Remove(aProcess);
  aProcess->RemoveBrowsingContextGroup(this);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  auto hostEntry = mHosts.Lookup(aProcess->GetRemoteType());
  MOZ_DIAGNOSTIC_ASSERT(!hostEntry || hostEntry.Data() != aProcess,
                        "Unsubscribing existing host entry");
#endif
}

ContentParent* BrowsingContextGroup::GetHostProcess(
    const nsACString& aRemoteType) {
  return mHosts.GetWeak(aRemoteType);
}

bool BrowsingContextGroup::IsKnownForMessageReader(
    IPC::MessageReader* aReader) {
  if (!aReader->GetActor()) {
    aReader->FatalError(
        "No actor for BrowsingContextGroup::IsKnownForMessageReader");
    return false;
  }

  mozilla::ipc::IToplevelProtocol* topActor =
      aReader->GetActor()->ToplevelProtocol();
  switch (topActor->GetProtocolId()) {
    case PInProcessMsgStart:
      // PInProcess always exists only within a single process, so we don't need
      // to do any validation on it.
      return true;

    case PContentMsgStart:
      // The process should only be able to name this BCG if it is
      // subscribed, or if the BCG has been destroyed (and has therefore
      // stopped tracking subscribers).
      if (ContentParent* cp = ActorDynCast<ContentParent>(topActor);
          cp && !mDestroyed && !mSubscribers.Contains(cp)) {
        aReader->FatalError(
            "Process is not subscribed to this BrowsingContextGroup");
        return false;
      }
      return true;

    default:
      aReader->FatalError(
          "Unsupported toplevel actor for "
          "BrowsingContextGroup::IsKnownForMessageReader");
      return false;
  }
}

bool BrowsingContextGroup::IsKnownForChildID(GeckoChildID aChildID) {
  // If the origin process is unknown, deny synced contexts from it.
  if (NS_WARN_IF(aChildID == kInvalidGeckoChildID)) {
    MOZ_ASSERT_UNREACHABLE("Unknown ChildID for BrowsingContextGroup");
    return false;
  }

  // Allow deserializing a synced context from the parent process (ID 0), or the
  // current process by-default.
  if (aChildID == 0 || aChildID == XRE_GetChildID()) {
    return true;
  }

  // If we're not in the parent process, deny any other messages (we shouldn't
  // be receiving a BrowsingContext directly from a peer process anyways).
  if (NS_WARN_IF(!XRE_IsParentProcess())) {
    MOZ_ASSERT_UNREACHABLE("Unexpected peer ChildID for BrowsingContextGroup");
    return false;
  }

  // Try to look up the ContentParent for this process.
  // If we can't, be conservative and deny the request. (We should be
  // deserializing StructuredCloneData instances containing BrowsingContexts
  // before the process has a chance to go away)
  ContentProcessManager* cpm = ContentProcessManager::GetSingleton();
  if (NS_WARN_IF(!cpm)) {
    MOZ_ASSERT_UNREACHABLE(
        "Unexpected cross-process deserialization late in shutdown");
    return false;
  }
  RefPtr<ContentParent> contentParent =
      cpm->GetContentProcessById(ContentParentId(aChildID));
  if (NS_WARN_IF(!contentParent)) {
    MOZ_ASSERT_UNREACHABLE(
        "ContentParent dead/missing when deserializing BrowsingContextGroup");
    return false;
  }

  // The process should only be able to name this BCG if it is subscribed, or if
  // the BCG has been destroyed (and has therefore stopped tracking subscribers)
  if (NS_WARN_IF(!mDestroyed && !mSubscribers.Contains(contentParent))) {
    MOZ_ASSERT_UNREACHABLE(
        "Process is not subscribed to this BrowsingContextGroup");
    return false;
  }
  return true;
}

void BrowsingContextGroup::UpdateToplevelsSuspendedIfNeeded() {
  if (!StaticPrefs::dom_suspend_inactive_enabled()) {
    return;
  }

  mToplevelsSuspended = ShouldSuspendAllTopLevelContexts();
  for (const auto& context : mToplevels) {
    nsPIDOMWindowOuter* outer = context->GetDOMWindow();
    if (!outer) {
      continue;
    }
    nsCOMPtr<nsPIDOMWindowInner> inner = outer->GetCurrentInnerWindow();
    if (!inner) {
      continue;
    }
    if (mToplevelsSuspended && !inner->GetWasSuspendedByGroup()) {
      inner->Suspend();
      inner->SetWasSuspendedByGroup(true);
    } else if (!mToplevelsSuspended && inner->GetWasSuspendedByGroup()) {
      inner->Resume();
      inner->SetWasSuspendedByGroup(false);
    }
  }
}

bool BrowsingContextGroup::ShouldSuspendAllTopLevelContexts() const {
  for (const auto& context : mToplevels) {
    if (!context->InactiveForSuspend()) {
      return false;
    }
  }
  return true;
}

BrowsingContextGroup::~BrowsingContextGroup() { Destroy(); }

void BrowsingContextGroup::Destroy() {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (mDestroyed) {
    MOZ_DIAGNOSTIC_ASSERT(mHosts.Count() == 0);
    MOZ_DIAGNOSTIC_ASSERT(mSubscribers.Count() == 0);
    MOZ_DIAGNOSTIC_ASSERT_IF(sBrowsingContextGroups,
                             !sBrowsingContextGroups->Contains(Id()) ||
                                 *sBrowsingContextGroups->Lookup(Id()) != this);
  }
#endif
  mDestroyed = true;

  // Make sure to call `RemoveBrowsingContextGroup` for every entry in both
  // `mHosts` and `mSubscribers`. This will visit most entries twice, but
  // `RemoveBrowsingContextGroup` is safe to call multiple times.
  for (const auto& entry : mHosts.Values()) {
    entry->RemoveBrowsingContextGroup(this);
  }
  for (const auto& key : mSubscribers) {
    key->RemoveBrowsingContextGroup(this);
  }
  mHosts.Clear();
  mSubscribers.Clear();

  if (sBrowsingContextGroups) {
    sBrowsingContextGroups->Remove(Id());
  }
}

void BrowsingContextGroup::AddKeepAlive() {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  mKeepAliveCount++;
}

void BrowsingContextGroup::RemoveKeepAlive() {
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(mKeepAliveCount > 0);
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  mKeepAliveCount--;

  MaybeDestroy();
}

auto BrowsingContextGroup::MakeKeepAlivePtr() -> KeepAlivePtr {
  AddKeepAlive();
  return KeepAlivePtr{do_AddRef(this).take()};
}

void BrowsingContextGroup::MaybeDestroy() {
  // Once there are no synced contexts referencing a `BrowsingContextGroup`, we
  // can clear subscribers and destroy this group. We only do this in the parent
  // process, as it will orchestrate destruction of BCGs in content processes.
  if (XRE_IsParentProcess() && mContexts.IsEmpty() && mKeepAliveCount == 0 &&
      this != sChromeGroup) {
    Destroy();

    // We may have been deleted here, as `Destroy()` will clear references. Do
    // not access any members at this point.
  }
}

void BrowsingContextGroup::ChildDestroy() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsContentProcess());
  MOZ_DIAGNOSTIC_ASSERT(!mDestroyed);
  MOZ_DIAGNOSTIC_ASSERT(mContexts.IsEmpty());
  Destroy();
}

nsISupports* BrowsingContextGroup::GetParentObject() const {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

JSObject* BrowsingContextGroup::WrapObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return BrowsingContextGroup_Binding::Wrap(aCx, this, aGivenProto);
}

nsresult BrowsingContextGroup::QueuePostMessageEvent(nsIRunnable* aRunnable) {
  MOZ_ASSERT(StaticPrefs::dom_separate_event_queue_for_post_message_enabled());

  if (!mPostMessageEventQueue) {
    nsCOMPtr<nsISerialEventTarget> target = GetMainThreadSerialEventTarget();
    mPostMessageEventQueue = ThrottledEventQueue::Create(
        target, "PostMessage Queue",
        nsIRunnablePriority::PRIORITY_DEFERRED_TIMERS);
    nsresult rv = mPostMessageEventQueue->SetIsPaused(false);
    MOZ_ALWAYS_SUCCEEDS(rv);
  }

  // Ensure the queue is enabled
  if (mPostMessageEventQueue->IsPaused()) {
    nsresult rv = mPostMessageEventQueue->SetIsPaused(false);
    MOZ_ALWAYS_SUCCEEDS(rv);
  }

  return mPostMessageEventQueue->Dispatch(aRunnable, NS_DISPATCH_NORMAL);
}

void BrowsingContextGroup::FlushPostMessageEvents() {
  if (!mPostMessageEventQueue) {
    return;
  }
  nsresult rv = mPostMessageEventQueue->SetIsPaused(true);
  MOZ_ALWAYS_SUCCEEDS(rv);
  nsCOMPtr<nsIRunnable> event;
  while ((event = mPostMessageEventQueue->GetEvent())) {
    NS_DispatchToMainThread(event.forget());
  }
}

bool BrowsingContextGroup::HasActiveBC() {
  for (auto& topLevelBC : Toplevels()) {
    if (topLevelBC->IsActive()) {
      return true;
    }
  }
  return false;
}

void BrowsingContextGroup::IncInputEventSuspensionLevel() {
  MOZ_ASSERT(StaticPrefs::dom_input_events_canSuspendInBCG_enabled());
  if (!mHasIncreasedInputTaskManagerSuspensionLevel && HasActiveBC()) {
    IncInputTaskManagerSuspensionLevel();
  }
  ++mInputEventSuspensionLevel;
}

void BrowsingContextGroup::DecInputEventSuspensionLevel() {
  MOZ_ASSERT(StaticPrefs::dom_input_events_canSuspendInBCG_enabled());
  --mInputEventSuspensionLevel;
  if (!mInputEventSuspensionLevel &&
      mHasIncreasedInputTaskManagerSuspensionLevel) {
    DecInputTaskManagerSuspensionLevel();
  }
}

void BrowsingContextGroup::DecInputTaskManagerSuspensionLevel() {
  MOZ_ASSERT(StaticPrefs::dom_input_events_canSuspendInBCG_enabled());
  MOZ_ASSERT(mHasIncreasedInputTaskManagerSuspensionLevel);

  InputTaskManager::Get()->DecSuspensionLevel();
  mHasIncreasedInputTaskManagerSuspensionLevel = false;
}

void BrowsingContextGroup::IncInputTaskManagerSuspensionLevel() {
  MOZ_ASSERT(StaticPrefs::dom_input_events_canSuspendInBCG_enabled());
  MOZ_ASSERT(!mHasIncreasedInputTaskManagerSuspensionLevel);
  MOZ_ASSERT(HasActiveBC());

  InputTaskManager::Get()->IncSuspensionLevel();
  mHasIncreasedInputTaskManagerSuspensionLevel = true;
}

void BrowsingContextGroup::UpdateInputTaskManagerIfNeeded(bool aIsActive) {
  MOZ_ASSERT(StaticPrefs::dom_input_events_canSuspendInBCG_enabled());
  if (!aIsActive) {
    if (mHasIncreasedInputTaskManagerSuspensionLevel) {
      MOZ_ASSERT(mInputEventSuspensionLevel > 0);
      if (!HasActiveBC()) {
        DecInputTaskManagerSuspensionLevel();
      }
    }
  } else {
    if (mInputEventSuspensionLevel &&
        !mHasIncreasedInputTaskManagerSuspensionLevel) {
      IncInputTaskManagerSuspensionLevel();
    }
  }
}

/* static */
BrowsingContextGroup* BrowsingContextGroup::GetChromeGroup() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  if (!sChromeGroup && XRE_IsParentProcess()) {
    sChromeGroup = BrowsingContextGroup::Create();
    ClearOnShutdown(&sChromeGroup);
  }

  return sChromeGroup;
}

void BrowsingContextGroup::GetDocGroups(nsTArray<DocGroup*>& aDocGroups) {
  MOZ_ASSERT(NS_IsMainThread());
  AppendToArray(aDocGroups, mDocGroups.Values());
}

already_AddRefed<DocGroup> BrowsingContextGroup::AddDocument(
    Document* aDocument) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIPrincipal> principal = aDocument->NodePrincipal();

  // Determine the DocGroup (agent cluster) key to use for this principal.
  //
  // https://html.spec.whatwg.org/#obtain-similar-origin-window-agent
  DocGroupKey key;
  if (auto originKeyed = UsesOriginAgentCluster(principal)) {
    key.mOriginKeyed = *originKeyed;
  } else {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    MOZ_CRASH(
        "Document loading without first determining origin keying for origin!");
#endif
    key.mOriginKeyed = false;
  }
  if (key.mOriginKeyed) {
    nsresult rv = principal->GetOrigin(key.mKey);
    NS_ENSURE_SUCCESS(rv, nullptr);
  } else {
    nsresult rv = principal->GetSiteOrigin(key.mKey);
    NS_ENSURE_SUCCESS(rv, nullptr);
  }

  RefPtr<DocGroup>& docGroup = mDocGroups.LookupOrInsertWith(
      key, [&] { return DocGroup::Create(this, key); });

  docGroup->AddDocument(aDocument);
  return do_AddRef(docGroup);
}

void BrowsingContextGroup::RemoveDocument(Document* aDocument,
                                          DocGroup* aDocGroup) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<DocGroup> docGroup = aDocGroup;
  // Removing the last document in DocGroup might decrement the
  // DocGroup BrowsingContextGroup's refcount to 0.
  RefPtr<BrowsingContextGroup> kungFuDeathGrip(this);
  docGroup->RemoveDocument(aDocument);

  if (docGroup->IsEmpty()) {
    mDocGroups.Remove(docGroup->GetKey());
  }
}

already_AddRefed<BrowsingContextGroup> BrowsingContextGroup::Select(
    WindowContext* aParent, BrowsingContext* aOpener) {
  if (aParent) {
    return do_AddRef(aParent->Group());
  }
  if (aOpener) {
    return do_AddRef(aOpener->Group());
  }
  return Create();
}

void BrowsingContextGroup::GetAllGroups(
    nsTArray<RefPtr<BrowsingContextGroup>>& aGroups) {
  aGroups.Clear();
  if (!sBrowsingContextGroups) {
    return;
  }

  aGroups = ToArray(sBrowsingContextGroups->Values());
}

// For tests only.
void BrowsingContextGroup::ResetDialogAbuseState() {
  mDialogAbuseCount = 0;
  // Reset the timer.
  mLastDialogQuitTime =
      TimeStamp::Now() -
      TimeDuration::FromSeconds(DEFAULT_SUCCESSIVE_DIALOG_TIME_LIMIT);
}

bool BrowsingContextGroup::DialogsAreBeingAbused() {
  if (mLastDialogQuitTime.IsNull() || nsContentUtils::IsCallerChrome()) {
    return false;
  }

  TimeDuration dialogInterval(TimeStamp::Now() - mLastDialogQuitTime);
  if (dialogInterval.ToSeconds() <
      Preferences::GetInt("dom.successive_dialog_time_limit",
                          DEFAULT_SUCCESSIVE_DIALOG_TIME_LIMIT)) {
    mDialogAbuseCount++;

    return PopupBlocker::GetPopupControlState() > PopupBlocker::openAllowed ||
           mDialogAbuseCount > MAX_SUCCESSIVE_DIALOG_COUNT;
  }

  // Reset the abuse counter
  mDialogAbuseCount = 0;

  return false;
}

bool BrowsingContextGroup::IsPotentiallyCrossOriginIsolated() {
  return GetBrowsingContextGroupIdFlags(mId) &
         kPotentiallyCrossOriginIsolatedFlag;
}

void BrowsingContextGroup::NotifyFocusedOrActiveBrowsingContextToProcess(
    ContentParent* aProcess) {
  MOZ_DIAGNOSTIC_ASSERT(aProcess);
  // If the focused or active BrowsingContexts belong in this group,
  // tell the newly subscribed process.
  if (nsFocusManager* fm = nsFocusManager::GetFocusManager()) {
    BrowsingContext* focused = fm->GetFocusedBrowsingContextInChrome();
    if (focused && focused->Group() != this) {
      focused = nullptr;
    }
    BrowsingContext* active = fm->GetActiveBrowsingContextInChrome();
    if (active && active->Group() != this) {
      active = nullptr;
    }

    if (focused || active) {
      (void)aProcess->SendSetupFocusedAndActive(
          focused, fm->GetActionIdForFocusedBrowsingContextInChrome(), active,
          fm->GetActionIdForActiveBrowsingContextInChrome());
    }
  }
}

// Non-http(s) principals always use origin agent clusters.
static bool AlwaysUseOriginAgentCluster(nsIPrincipal* aPrincipal) {
  return !aPrincipal->GetIsContentPrincipal() ||
         (!aPrincipal->SchemeIs("http") && !aPrincipal->SchemeIs("https"));
}

void BrowsingContextGroup::SetUseOriginAgentClusterFromNetwork(
    nsIPrincipal* aPrincipal, bool aUseOriginAgentCluster) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Ignore this set call if it will have no effect on loads in this BCG (e.g.
  // because the BCG is cross-origin isolated or the origin will always be
  // origin-keyed).
  if (AlwaysUseOriginAgentCluster(aPrincipal) ||
      IsPotentiallyCrossOriginIsolated()) {
    return;
  }

  MOZ_ASSERT(!mUseOriginAgentCluster.Contains(aPrincipal));
  mUseOriginAgentCluster.InsertOrUpdate(aPrincipal, aUseOriginAgentCluster);

  // Tell any content processes subscribed to this BrowsingContextGroup about
  // the new flag.
  EachParent([&](ContentParent* aContentParent) {
    // If this ContentParent can never load this principal, don't send it the
    // information.
    if (!aContentParent->ValidatePrincipal(
            aPrincipal, {ValidatePrincipalOptions::AllowNotLoadedOrigin})) {
      return;
    }

    (void)aContentParent->SendSetUseOriginAgentCluster(
        Id(), WrapNotNull(aPrincipal), aUseOriginAgentCluster);
  });
}

void BrowsingContextGroup::SetUseOriginAgentClusterFromIPC(
    nsIPrincipal* aPrincipal, bool aUseOriginAgentCluster) {
  MOZ_ASSERT(!AlwaysUseOriginAgentCluster(aPrincipal));
  MOZ_ASSERT(!IsPotentiallyCrossOriginIsolated());
  MOZ_ASSERT(!mUseOriginAgentCluster.Contains(aPrincipal));
  mUseOriginAgentCluster.InsertOrUpdate(aPrincipal, aUseOriginAgentCluster);
}

Maybe<bool> BrowsingContextGroup::UsesOriginAgentCluster(
    nsIPrincipal* aPrincipal) {
  // Check if agent clusters (DocGroups) for aPrincipal should be origin-keyed.
  // https://html.spec.whatwg.org/#origin-keyed-agent-clusters
  if (AlwaysUseOriginAgentCluster(aPrincipal) ||
      IsPotentiallyCrossOriginIsolated()) {
    return Some(true);
  }

  // If this assertion fails, we may return `Nothing()` below unexpectedly, as
  // the parent process may have chosen to not process-switch.
  RefPtr<LoadedOriginSet> loadedOrigins = CurrentLoadedOriginSet();
  MOZ_DIAGNOSTIC_ASSERT(
      XRE_IsParentProcess() ||
          loadedOrigins->ValidatePrincipal(
              aPrincipal, {ValidatePrincipalOptions::AllowNotLoadedOrigin}),
      "Attempting to create document with unexpected principal");

  if (auto entry = mUseOriginAgentCluster.Lookup(aPrincipal)) {
    return Some(entry.Data());
  }
  return Nothing();
}

void BrowsingContextGroup::EnsureUsesOriginAgentClusterInitialized(
    nsIPrincipal* aPrincipal) {
  if (UsesOriginAgentCluster(aPrincipal).isSome()) {
    return;
  }

  MOZ_RELEASE_ASSERT(!XRE_IsContentProcess(),
                     "Cannot determine origin-keying in content process!");

  // We're about to load a document for this principal without hitting the
  // network, so simulate no HTTP header being received on the network.
  SetUseOriginAgentClusterFromNetwork(
      aPrincipal, StaticPrefs::dom_origin_agent_cluster_default());
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(BrowsingContextGroup, mContexts,
                                      mToplevels, mHosts, mSubscribers,
                                      mTimerEventQueue, mWorkerEventQueue,
                                      mDocGroups)

}  // namespace mozilla::dom
