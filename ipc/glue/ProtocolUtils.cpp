/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/process_util.h"
#include "base/task.h"

#ifdef XP_UNIX
#  include <errno.h>
#endif

#include "mozilla/IntegerPrintfMacros.h"

#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/ipc/ProtocolUtils.h"

#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/StaticMutex.h"
#if defined(DEBUG) || defined(FUZZING)
#  include "mozilla/Tokenizer.h"
#endif
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "prtime.h"

#if defined(MOZ_SANDBOX) && defined(XP_WIN)
#  include "mozilla/sandboxTarget.h"
#endif

#if defined(XP_WIN)
#  include "aclapi.h"
#  include "sddl.h"
#endif

#ifdef FUZZING_SNAPSHOT
#  include "mozilla/fuzzing/IPCFuzzController.h"
#endif

using namespace IPC;

using base::GetCurrentProcId;
using base::ProcessHandle;
using base::ProcessId;

namespace mozilla {
namespace ipc {

/* static */
EndpointProcInfo EndpointProcInfo::Current() {
  return EndpointProcInfo{.mPid = GetCurrentProcId(),
                          .mChildID = XRE_GetChildID()};
}

/* static */
IPCResult IPCResult::FailImpl(NotNull<IProtocol*> actor, const char* where,
                              const char* why) {
  // Calls top-level protocol to handle the error.
  nsPrintfCString errorMsg("%s %s\n", where, why);
  actor->GetIPCChannel()->Listener()->ProcessingError(
      HasResultCodes::MsgProcessingError, errorMsg.get());

#if defined(DEBUG) && !defined(FUZZING)
  // We do not expect IPC_FAIL to ever happen in normal operations. If this
  // happens in DEBUG, we most likely see some behavior during a test we should
  // really investigate.
  nsPrintfCString crashMsg(
      "Use IPC_FAIL only in an "
      "unrecoverable, unexpected state: %s",
      errorMsg.get());
  // We already leak the same information potentially on child process failures
  // even in release, and here we are only in DEBUG.
  MOZ_CRASH_UNSAFE(crashMsg.get());
#else
  return IPCResult(false);
#endif
}

/* static */
IPCResult IPCResult::FailForTesting(NotNull<IProtocol*> actor,
                                    const char* where, const char* why) {
  return IPCResult(false);
}

void AnnotateSystemError() {
  uint32_t error = 0;
#if defined(XP_WIN)
  error = ::GetLastError();
#else
  error = errno;
#endif
  if (error) {
    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::IPCSystemError, error);
  }
}

#if defined(XP_MACOSX)
void AnnotateCrashReportWithErrno(CrashReporter::Annotation tag, int error) {
  CrashReporter::RecordAnnotationU32(tag, static_cast<uint32_t>(error));
}
#endif  // defined(XP_MACOSX)

#if defined(DEBUG) || defined(FUZZING)
// If aTopLevelProtocol matches any token in aFilter, return true.
//
// aTopLevelProtocol is a protocol name, without the "Parent" / "Child" suffix.
// aSide indicates whether we're logging parent-side or child-side activity.
//
// aFilter is a list of protocol names separated by commas and/or
// spaces. These may include the "Child" / "Parent" suffix, or omit
// the suffix to log activity on both sides.
//
// This overload is for testability; application code should use the single-
// argument version (defined in the ProtocolUtils.h) which takes the filter from
// the environment.
bool LoggingEnabledFor(const char* aTopLevelProtocol, Side aSide,
                       const char* aFilter) {
  if (!aFilter) {
    return false;
  }
  if (strcmp(aFilter, "1") == 0) {
    return true;
  }

  const char kDelimiters[] = ", ";
  Tokenizer tokens(aFilter, kDelimiters);
  Tokenizer::Token t;
  while (tokens.Next(t)) {
    if (t.Type() == Tokenizer::TOKEN_WORD) {
      const auto& filter = t.AsString();

      // Since aTopLevelProtocol never includes the "Parent" / "Child" suffix,
      // this will only occur when filter doesn't include it either, meaning
      // that we should log activity on both sides.
      if (filter == aTopLevelProtocol) {
        return true;
      }

      if (aSide == ParentSide &&
          StringEndsWith(filter, nsDependentCString("Parent")) &&
          Substring(filter, 0, filter.Length() - 6) == aTopLevelProtocol) {
        return true;
      }

      if (aSide == ChildSide &&
          StringEndsWith(filter, nsDependentCString("Child")) &&
          Substring(filter, 0, filter.Length() - 5) == aTopLevelProtocol) {
        return true;
      }
    }
  }

  return false;
}
#endif  // defined(DEBUG) || defined(FUZZING)

void LogMessageForProtocol(const char* aTopLevelProtocol,
                           base::ProcessId aOtherPid,
                           const char* aContextDescription, uint32_t aMessageId,
                           MessageDirection aDirection) {
  nsPrintfCString logMessage(
      "[time: %" PRId64 "][%" PRIPID "%s%" PRIPID "] [%s] %s %s\n", PR_Now(),
      base::GetCurrentProcId(),
      aDirection == MessageDirection::eReceiving ? "<-" : "->", aOtherPid,
      aTopLevelProtocol, aContextDescription,
      StringFromIPCMessageType(aMessageId));
#ifdef ANDROID
  __android_log_write(ANDROID_LOG_INFO, "GeckoIPC", logMessage.get());
#endif
  fputs(logMessage.get(), stderr);
}

void ProtocolErrorBreakpoint(const char* aMsg) {
  // Bugs that generate these error messages can be tough to
  // reproduce.  Log always in the hope that someone finds the error
  // message.
  printf_stderr("IPDL protocol error: %s\n", aMsg);
}

void PickleFatalError(const char* aMsg, IProtocol* aActor) {
  if (aActor) {
    aActor->FatalError(aMsg);
  } else {
    FatalError(aMsg, false);
  }
}

void FatalError(const char* aMsg, bool aIsParent) {
#ifndef FUZZING
  ProtocolErrorBreakpoint(aMsg);
#endif

  nsAutoCString formattedMessage("IPDL error: \"");
  formattedMessage.AppendASCII(aMsg);
  if (aIsParent) {
    // We're going to crash the parent process because at this time
    // there's no other really nice way of getting a minidump out of
    // this process if we're off the main thread.
    formattedMessage.AppendLiteral("\". Intentionally crashing.");
    NS_ERROR(formattedMessage.get());
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::IPCFatalErrorMsg, aMsg);
    AnnotateSystemError();
#ifndef FUZZING
    MOZ_CRASH("IPC FatalError in the parent process!");
#endif
  } else {
    formattedMessage.AppendLiteral("\". abort()ing as a result.");
#ifndef FUZZING
    MOZ_CRASH_UNSAFE(formattedMessage.get());
#endif
  }
}

void LogicError(const char* aMsg) { MOZ_CRASH_UNSAFE(aMsg); }

void ActorIdReadError(const char* aActorDescription) {
#ifndef FUZZING
  MOZ_CRASH_UNSAFE_PRINTF("Error deserializing id for %s", aActorDescription);
#endif
}

void BadActorIdError(const char* aActorDescription) {
  nsPrintfCString message("bad id for %s", aActorDescription);
  ProtocolErrorBreakpoint(message.get());
}

void ActorLookupError(const char* aActorDescription) {
  nsPrintfCString message("could not lookup id for %s", aActorDescription);
  ProtocolErrorBreakpoint(message.get());
}

void MismatchedActorTypeError(const char* aActorDescription) {
  nsPrintfCString message("actor that should be of type %s has different type",
                          aActorDescription);
  ProtocolErrorBreakpoint(message.get());
}

void UnionTypeReadError(const char* aUnionName) {
  MOZ_CRASH_UNSAFE_PRINTF("error deserializing type of union %s", aUnionName);
}

void ArrayLengthReadError(const char* aElementName) {
  MOZ_CRASH_UNSAFE_PRINTF("error deserializing length of %s[]", aElementName);
}

void SentinelReadError(const char* aClassName) {
  MOZ_CRASH_UNSAFE_PRINTF("incorrect sentinel when reading %s", aClassName);
}

ActorLifecycleProxy::ActorLifecycleProxy(IProtocol* aActor)
#ifdef MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED
    : _mOwningThread(aActor->GetActorEventTarget()), mActor(aActor) {
#else
    : mActor(aActor) {
#endif
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(mActor->CanSend(),
             "Cannot create LifecycleProxy for non-connected actor!");

  // Record that we've taken our first reference to our actor.
  mActor->ActorAlloc();
}

WeakActorLifecycleProxy* ActorLifecycleProxy::GetWeakProxy() {
  if (!mWeakProxy) {
    mWeakProxy = new WeakActorLifecycleProxy(this);
  }
  return mWeakProxy;
}

ActorLifecycleProxy::~ActorLifecycleProxy() {
  if (mWeakProxy) {
    mWeakProxy->mProxy = nullptr;
    mWeakProxy = nullptr;
  }

  // When the LifecycleProxy's lifetime has come to an end, it means that the
  // actor should have its `Dealloc` method called on it. In a well-behaved
  // actor, this will release the IPC-held reference to the actor.
  //
  // If the actor has already died before the `LifecycleProxy`, the `IProtocol`
  // destructor below will clear our reference to it, preventing us from
  // performing a use-after-free here.
  if (!mActor) {
    return;
  }

  // Clear our actor's state back to inactive, and then invoke ActorDealloc.
  MOZ_ASSERT(mActor->mLinkStatus == LinkStatus::Destroyed,
             "Deallocating non-destroyed actor!");
  mActor->mLifecycleProxy = nullptr;
  mActor->mLinkStatus = LinkStatus::Inactive;
  mActor->ActorDealloc();
  mActor = nullptr;
}

WeakActorLifecycleProxy::WeakActorLifecycleProxy(ActorLifecycleProxy* aProxy)
    : mProxy(aProxy), mActorEventTarget(aProxy->Get()->GetActorEventTarget()) {
  MOZ_ASSERT(mActorEventTarget->IsOnCurrentThread());
}

WeakActorLifecycleProxy::~WeakActorLifecycleProxy() {
  MOZ_DIAGNOSTIC_ASSERT(!mProxy, "Destroyed before mProxy was cleared?");
}

IProtocol* WeakActorLifecycleProxy::Get() const {
  MOZ_DIAGNOSTIC_ASSERT(mActorEventTarget->IsOnCurrentThread());
  return mProxy ? mProxy->Get() : nullptr;
}

WeakActorLifecycleProxy* IProtocol::GetWeakLifecycleProxy() {
  return mLifecycleProxy ? mLifecycleProxy->GetWeakProxy() : nullptr;
}

IProtocol::~IProtocol() {
  // If the actor still has a lifecycle proxy when it is being torn down, it
  // means that IPC was not given control over the lifecycle of the actor
  // correctly. Usually this means that the actor was destroyed while IPC is
  // calling a message handler for it, and the actor incorrectly frees itself
  // during that operation.
  //
  // As this happens unfortunately frequently, due to many odd protocols in
  // Gecko, simply emit a warning and clear the weak backreference from our
  // LifecycleProxy back to us.
  if (mLifecycleProxy) {
    MOZ_ASSERT(mLinkStatus != LinkStatus::Inactive);
    NS_WARNING(
        nsPrintfCString("Actor destructor for '%s%s' called before IPC "
                        "lifecycle complete!\n"
                        "References to this actor may unexpectedly dangle!",
                        GetProtocolName(), StringFromIPCSide(GetSide()))
            .get());

    mLifecycleProxy->mActor = nullptr;
    mLifecycleProxy = nullptr;
  }
}

// The following methods either directly forward to the toplevel protocol, or
// almost directly do.
IProtocol* IProtocol::Lookup(ActorId aId) { return mToplevel->Lookup(aId); }

Shmem IProtocol::CreateSharedMemory(size_t aSize, bool aUnsafe) {
  return mToplevel->CreateSharedMemory(aSize, aUnsafe);
}
Shmem::Segment* IProtocol::LookupSharedMemory(Shmem::id_t aId) {
  return mToplevel->LookupSharedMemory(aId);
}
bool IProtocol::IsTrackingSharedMemory(const Shmem::Segment* aSegment) {
  return mToplevel->IsTrackingSharedMemory(aSegment);
}
bool IProtocol::DestroySharedMemory(Shmem& aShmem) {
  return mToplevel->DestroySharedMemory(aShmem);
}

MessageChannel* IProtocol::GetIPCChannel() {
  return mToplevel->GetIPCChannel();
}
const MessageChannel* IProtocol::GetIPCChannel() const {
  return mToplevel->GetIPCChannel();
}

nsISerialEventTarget* IProtocol::GetActorEventTarget() {
  return GetIPCChannel()->GetWorkerEventTarget();
}

void IProtocol::FatalError(const char* const aErrorMsg) {
  HandleFatalError(aErrorMsg);
}

void IProtocol::HandleFatalError(const char* aErrorMsg) {
  if (IProtocol* manager = Manager()) {
    manager->HandleFatalError(aErrorMsg);
    return;
  }

  mozilla::ipc::FatalError(aErrorMsg, mSide == ParentSide);
  if (CanSend()) {
    GetIPCChannel()->InduceConnectionError();
  }
}

bool IProtocol::AllocShmem(size_t aSize, Shmem* aOutMem) {
  if (!CanSend()) {
    NS_WARNING(
        "Shmem not allocated.  Cannot communicate with the other actor.");
    return false;
  }

  *aOutMem = CreateSharedMemory(aSize, false);
  return aOutMem->IsReadable();
}

bool IProtocol::AllocUnsafeShmem(size_t aSize, Shmem* aOutMem) {
  if (!CanSend()) {
    NS_WARNING(
        "Shmem not allocated.  Cannot communicate with the other actor.");
    return false;
  }

  *aOutMem = CreateSharedMemory(aSize, true);
  return aOutMem->IsReadable();
}

bool IProtocol::DeallocShmem(Shmem& aMem) {
  bool ok = DestroySharedMemory(aMem);
#ifdef DEBUG
  if (!ok) {
    if (mSide == ChildSide) {
      FatalError("bad Shmem");
    } else {
      NS_WARNING("bad Shmem");
    }
    return false;
  }
#endif  // DEBUG
  aMem.forget();
  return ok;
}

void IProtocol::SetManager(IRefCountedProtocol* aManager) {
  MOZ_RELEASE_ASSERT(!mManager || mManager == aManager);
  mManager = aManager;
  mToplevel = aManager->mToplevel;
}

bool IProtocol::SetManagerAndRegister(IRefCountedProtocol* aManager,
                                      ActorId aId) {
  MOZ_RELEASE_ASSERT(mLinkStatus == LinkStatus::Inactive,
                     "Actor must be inactive to SetManagerAndRegister");

  // Set to `false` if the actor is to be torn down after registration.
  bool success = true;

  // Set the manager prior to registering so registering properly inherits
  // the manager's event target.
  SetManager(aManager);

  mId = aId == kNullActorId ? mToplevel->NextId() : aId;

  RefPtr<ActorLifecycleProxy> proxy = ActorConnected();
  MOZ_ASSERT(proxy->Get() == this);

  mToplevel->mActorMap.WithEntryHandle(mId, [&](auto entry) {
    if (aId == kNullActorId) {
      MOZ_RELEASE_ASSERT(!entry, "Entry must not exist for new actor ID");
    } else {
      MOZ_RELEASE_ASSERT(entry && !entry.Data(),
                         "Entry must be a reservation for reserved actor ID");
    }
    entry.InsertOrUpdate(proxy);
  });

  UntypedManagedContainer* container =
      aManager->GetManagedActors(GetProtocolId());
  if (container) {
    container->Insert(this);
  } else {
    NS_WARNING("Manager does not manage actors with this ProtocolId");
    success = false;
  }

  // If our manager is already dying, mark ourselves as doomed as well.
  if (aManager && aManager->mLinkStatus != LinkStatus::Connected) {
    mLinkStatus = LinkStatus::Doomed;
    if (aManager->mLinkStatus != LinkStatus::Doomed) {
      // Our manager is already fully dead, make sure we call
      // `ActorDisconnected`.
      success = false;
    }
  }

  // If setting the manager failed, call `ActorDisconnected` and return false.
  if (!success) {
    ActorDisconnected(FailedConstructor);
    MOZ_ASSERT(mLinkStatus == LinkStatus::Destroyed);
    return false;
  }
  return true;
}

void IProtocol::UnlinkManager() {
  mToplevel = nullptr;
  mManager = nullptr;
}

bool IProtocol::ChannelSend(UniquePtr<IPC::Message> aMsg,
                            IPC::Message::seqno_t* aSeqno) {
  if (CanSend()) {
    // NOTE: This send call failing can only occur during toplevel channel
    // teardown. As this is an async call, this isn't reasonable to predict or
    // respond to, so just drop the message on the floor silently.
    GetIPCChannel()->Send(std::move(aMsg), aSeqno);
    return true;
  }

  WarnMessageDiscarded(aMsg.get());
  return false;
}

bool IProtocol::ChannelSend(UniquePtr<IPC::Message> aMsg,
                            UniquePtr<IPC::Message>* aReply) {
  if (CanSend()) {
    return GetIPCChannel()->Send(std::move(aMsg), aReply);
  }

  WarnMessageDiscarded(aMsg.get());
  return false;
}

#ifdef DEBUG
void IProtocol::WarnMessageDiscarded(IPC::Message* aMsg) {
  NS_WARNING(nsPrintfCString("IPC message '%s' discarded: actor cannot send",
                             aMsg->name())
                 .get());
}
#endif

uint32_t IProtocol::AllManagedActorsCount() const {
  uint32_t total = 0;
  for (ProtocolId id : ManagedProtocolIds()) {
    total += GetManagedActors(id)->Count();
  }
  return total;
}

already_AddRefed<ActorLifecycleProxy> IProtocol::ActorConnected() {
  if (mLinkStatus != LinkStatus::Inactive) {
    return nullptr;
  }

#ifdef FUZZING_SNAPSHOT
  fuzzing::IPCFuzzController::instance().OnActorConnected(this);
#endif

  mLinkStatus = LinkStatus::Connected;

  MOZ_ASSERT(!mLifecycleProxy, "double-connecting live actor");
  RefPtr<ActorLifecycleProxy> proxy = new ActorLifecycleProxy(this);
  mLifecycleProxy = proxy;
  return proxy.forget();
}

void IProtocol::ActorDisconnected(ActorDestroyReason aWhy) {
  MOZ_ASSERT(mLifecycleProxy, "destroying zombie actor");
  // If the actor has already been marked as `Destroyed`, there's nothing to do.
  if (mLinkStatus != LinkStatus::Connected &&
      mLinkStatus != LinkStatus::Doomed) {
    return;
  }

  // Mark the entire subtree as doomed so that no further messages can be
  // sent/recieved, and newly created managed actors are immediately marked as
  // doomed on creation.
  DoomSubtree();

  // Perform the steps to fully destroy an actor after it has been unregistered
  // from its manager.
  auto doActorDestroy = [toplevel = mToplevel, ipcChannel = GetIPCChannel()](
                            IProtocol* actor, ActorDestroyReason why) {
    MOZ_ASSERT(actor->mLinkStatus == LinkStatus::Doomed,
               "Actor must be doomed when calling doActorDestroy");
    MOZ_ASSERT(actor->AllManagedActorsCount() == 0,
               "All managed actors must have been destroyed first");

    // Mark the actor as Destroyed, ensuring we can't re-enter `ActorDestroy`,
    // even if an callback spins a nested event loop.
    actor->mLinkStatus = LinkStatus::Destroyed;

#ifdef FUZZING_SNAPSHOT
    fuzzing::IPCFuzzController::instance().OnActorDestroyed(actor);
#endif

    ActorId id = actor->mId;
    if (IProtocol* manager = actor->Manager()) {
      auto entry = toplevel->mActorMap.Lookup(id);
      MOZ_DIAGNOSTIC_ASSERT(entry && *entry == actor->GetLifecycleProxy(),
                            "ID must be present and reference this actor");
      entry.Remove();
      if (auto* container = manager->GetManagedActors(actor->GetProtocolId())) {
        container->EnsureRemoved(actor);
      }
    }

    actor->RejectPendingResponses(ResponseRejectReason::ActorDestroyed);
    actor->ActorDestroy(why);
  };

  // Hold all ActorLifecycleProxy instances for managed actors until we return.
  nsTArray<RefPtr<ActorLifecycleProxy>> proxyHolder;
  proxyHolder.AppendElement(GetLifecycleProxy());

  // Invoke `ActorDestroy` for all managed actors in the subtree. These are
  // handled one at a time, so that new actors which are potentially registered
  // during `ActorDestroy` callbacks are not missed.
  ActorDestroyReason subtreeWhy = aWhy;
  if (aWhy == Deletion || aWhy == FailedConstructor) {
    subtreeWhy = AncestorDeletion;
  }
  while (IProtocol* actor = PeekManagedActor()) {
    // If the selected actor manages other actors, destroy those first.
    while (IProtocol* inner = actor->PeekManagedActor()) {
      actor = inner;
    }

    proxyHolder.AppendElement(actor->GetLifecycleProxy());
    doActorDestroy(actor, subtreeWhy);
  }

  // Destroy ourselves if we were not not otherwise destroyed while destroying
  // managed actors.
  if (mLinkStatus == LinkStatus::Doomed) {
    doActorDestroy(this, aWhy);
  }
}

void IProtocol::DoomSubtree() {
  // If we're already `Doomed` or `Destroyed`, there's nothing to do.
  if (mLinkStatus != LinkStatus::Connected) {
    return;
  }
  for (ProtocolId id : ManagedProtocolIds()) {
    for (IProtocol* actor : *GetManagedActors(id)) {
      actor->DoomSubtree();
    }
  }
  mLinkStatus = LinkStatus::Doomed;
}

IProtocol* IProtocol::PeekManagedActor() const {
  for (ProtocolId id : ManagedProtocolIds()) {
    const UntypedManagedContainer& container = *GetManagedActors(id);
    if (!container.IsEmpty()) {
      // Return the last element first, to reduce the copying required when
      // removing it.
      return *(container.end() - 1);
    }
  }
  return nullptr;
}

IToplevelProtocol::IToplevelProtocol(const char* aName, ProtocolId aProtoId,
                                     Side aSide)
    : IRefCountedProtocol(aProtoId, aSide),
      mOtherPid(base::kInvalidProcessId),
      mLastLocalId(kNullActorId),
      mChannel(aName, this) {
  mToplevel = this;
}

void IToplevelProtocol::SetOtherEndpointProcInfo(
    EndpointProcInfo aOtherProcInfo) {
  mOtherPid = aOtherProcInfo.mPid;
  mOtherChildID = aOtherProcInfo.mChildID;
}

bool IToplevelProtocol::Open(ScopedPort aPort, const nsID& aMessageChannelId,
                             EndpointProcInfo aOtherProcInfo,
                             nsISerialEventTarget* aEventTarget) {
  SetOtherEndpointProcInfo(aOtherProcInfo);
  return GetIPCChannel()->Open(std::move(aPort), mSide, aMessageChannelId,
                               aEventTarget);
}

bool IToplevelProtocol::Open(IToplevelProtocol* aTarget,
                             nsISerialEventTarget* aEventTarget,
                             mozilla::ipc::Side aSide) {
  SetOtherEndpointProcInfo(EndpointProcInfo::Current());
  aTarget->SetOtherEndpointProcInfo(EndpointProcInfo::Current());
  return GetIPCChannel()->Open(aTarget->GetIPCChannel(), aEventTarget, aSide);
}

bool IToplevelProtocol::OpenOnSameThread(IToplevelProtocol* aTarget,
                                         Side aSide) {
  SetOtherEndpointProcInfo(EndpointProcInfo::Current());
  aTarget->SetOtherEndpointProcInfo(EndpointProcInfo::Current());
  return GetIPCChannel()->OpenOnSameThread(aTarget->GetIPCChannel(), aSide);
}

void IToplevelProtocol::NotifyImpendingShutdown() {
  if (CanSend()) {
    GetIPCChannel()->NotifyImpendingShutdown();
  }
}

void IToplevelProtocol::Close() { GetIPCChannel()->Close(); }

void IToplevelProtocol::SetReplyTimeoutMs(int32_t aTimeoutMs) {
  GetIPCChannel()->SetReplyTimeoutMs(aTimeoutMs);
}

bool IToplevelProtocol::IsOnCxxStack() const {
  return GetIPCChannel()->IsOnCxxStack();
}

int64_t IToplevelProtocol::NextId() {
  // Generate the next ID to use for a shared memory or protocol. Parent and
  // Child sides of the protocol use different pools.
  MOZ_RELEASE_ASSERT(mozilla::Abs(mLastLocalId) < MSG_ROUTING_CONTROL - 1,
                     "actor id overflow");
  return (GetSide() == ChildSide) ? --mLastLocalId : ++mLastLocalId;
}

IProtocol* IToplevelProtocol::Lookup(ActorId aId) {
  if (auto entry = mActorMap.Lookup(aId); entry && entry.Data()) {
    return entry.Data()->Get();
  }
  return nullptr;
}

bool IToplevelProtocol::TryReserve(ActorId aId) {
  // The ID must be coming from the other side.
  // This logic should check for the opposite sign as NextId().
  if (mozilla::Abs(aId) >= MSG_ROUTING_CONTROL ||
      (GetSide() == ChildSide && aId <= kNullActorId) ||
      (GetSide() == ParentSide && aId >= kNullActorId)) {
    return false;
  }

  // Ensure the entry isn't already in use, and then insert it into our map.
  return mActorMap.WithEntryHandle(aId, [&](auto entry) {
    if (entry) {
      return false;
    }
    entry.Insert(nullptr);
    return true;
  });
}

void IToplevelProtocol::ClearReservation(ActorId aId) {
  auto entry = mActorMap.Lookup(aId);
  // Only remove if it's still a placeholder.
  if (entry && !entry.Data()) {
    entry.Remove();
  }
}

Shmem IToplevelProtocol::CreateSharedMemory(size_t aSize, bool aUnsafe) {
  auto shmemBuilder = Shmem::Builder(aSize);
  if (!shmemBuilder) {
    return {};
  }
  auto [createdMessage, shmem] =
      shmemBuilder.Build(NextId(), aUnsafe, MSG_ROUTING_CONTROL);
  if (!createdMessage) {
    return {};
  }
  (void)GetIPCChannel()->Send(std::move(createdMessage));

  MOZ_ASSERT(!mShmemMap.Contains(shmem.Id()),
             "Don't insert with an existing ID");
  mShmemMap.InsertOrUpdate(shmem.Id(), shmem.GetSegment());
  return shmem;
}

Shmem::Segment* IToplevelProtocol::LookupSharedMemory(Shmem::id_t aId) {
  auto entry = mShmemMap.Lookup(aId);
  return entry ? entry.Data().get() : nullptr;
}

bool IToplevelProtocol::IsTrackingSharedMemory(const Shmem::Segment* segment) {
  for (const auto& shmem : mShmemMap.Values()) {
    if (segment == shmem) {
      return true;
    }
  }
  return false;
}

bool IToplevelProtocol::DestroySharedMemory(Shmem& shmem) {
  Shmem::id_t aId = shmem.Id();
  if (!LookupSharedMemory(aId)) {
    return false;
  }

  UniquePtr<Message> descriptor = shmem.MkDestroyedMessage(MSG_ROUTING_CONTROL);

  MOZ_ASSERT(mShmemMap.Contains(aId),
             "Attempting to remove an ID not in the shmem map");
  mShmemMap.Remove(aId);

  MessageChannel* channel = GetIPCChannel();
  if (!channel->CanSend()) {
    return true;
  }

  return descriptor && channel->Send(std::move(descriptor));
}

void IToplevelProtocol::DeallocShmems() { mShmemMap.Clear(); }

bool IToplevelProtocol::ShmemCreated(const Message& aMsg) {
  Shmem::id_t id;
  RefPtr<Shmem::Segment> segment(Shmem::OpenExisting(aMsg, &id, true));
  if (!segment) {
    return false;
  }
  MOZ_ASSERT(!mShmemMap.Contains(id), "Don't insert with an existing ID");
  mShmemMap.InsertOrUpdate(id, std::move(segment));
  return true;
}

bool IToplevelProtocol::ShmemDestroyed(const Message& aMsg) {
  Shmem::id_t id;
  MessageReader reader(aMsg);
  if (!IPC::ReadParam(&reader, &id)) {
    return false;
  }
  reader.EndRead();

  mShmemMap.Remove(id);
  return true;
}

IPDLResolverInner::IPDLResolverInner(UniquePtr<IPC::Message> aReply,
                                     IProtocol* aActor)
    : mReply(std::move(aReply)),
      mWeakProxy(aActor->GetLifecycleProxy()->GetWeakProxy()) {}

void IPDLResolverInner::ResolveOrReject(
    bool aResolve, FunctionRef<void(IPC::Message*, IProtocol*)> aWrite) {
  MOZ_ASSERT(mWeakProxy);
  MOZ_ASSERT(mWeakProxy->ActorEventTarget()->IsOnCurrentThread());
  MOZ_ASSERT(mReply);

  UniquePtr<IPC::Message> reply = std::move(mReply);

  IProtocol* actor = mWeakProxy->Get();
  if (!actor) {
    NS_WARNING(nsPrintfCString("Not resolving response '%s': actor is dead",
                               reply->name())
                   .get());
    return;
  }

  IPC::MessageWriter writer(*reply, actor);
  WriteParam(&writer, aResolve);
  aWrite(reply.get(), actor);

  actor->ChannelSend(std::move(reply));
}

void IPDLResolverInner::Destroy() {
  if (mReply) {
    NS_PROXY_DELETE_TO_EVENT_TARGET(IPDLResolverInner,
                                    mWeakProxy->ActorEventTarget());
  } else {
    // If we've already been consumed, just delete without proxying. This avoids
    // leaking the resolver if the actor's thread is already dead.
    delete this;
  }
}

IPDLResolverInner::~IPDLResolverInner() {
  if (mReply) {
    NS_WARNING(
        nsPrintfCString(
            "Rejecting reply '%s': resolver dropped without being called",
            mReply->name())
            .get());
    ResolveOrReject(false, [](IPC::Message* aMessage, IProtocol* aActor) {
      IPC::MessageWriter writer(*aMessage, aActor);
      ResponseRejectReason reason = ResponseRejectReason::ResolverDestroyed;
      WriteParam(&writer, reason);
    });
  }
}

bool IPDLAsyncReturnsCallbacks::EntryKey::operator==(
    const EntryKey& aOther) const {
  return mSeqno == aOther.mSeqno && mType == aOther.mType;
}

bool IPDLAsyncReturnsCallbacks::EntryKey::operator<(
    const EntryKey& aOther) const {
  return mSeqno < aOther.mSeqno ||
         (mSeqno == aOther.mSeqno && mType < aOther.mType);
}

void IPDLAsyncReturnsCallbacks::AddCallback(IPC::Message::seqno_t aSeqno,
                                            msgid_t aType, Callback aResolve,
                                            RejectCallback aReject) {
  Entry entry{{aSeqno, aType}, std::move(aResolve), std::move(aReject)};
  MOZ_ASSERT(!mMap.ContainsSorted(entry));
  mMap.InsertElementSorted(std::move(entry));
}

auto IPDLAsyncReturnsCallbacks::GotReply(IProtocol* aActor,
                                         const IPC::Message& aMessage)
    -> Result {
  // Check if we have an entry for the given seqno and message type.
  EntryKey key{aMessage.seqno(), aMessage.type()};
  size_t index = mMap.BinaryIndexOf(key);
  if (index == nsTArray<Entry>::NoIndex) {
    return MsgProcessingError;
  }

  // Move the callbacks out of the map, as we will now be handling it.
  Entry entry = std::move(mMap[index]);
  mMap.RemoveElementAt(index);
  MOZ_ASSERT(entry == key);

  // Deserialize the message which was serialized by IPDLResolverInner.
  IPC::MessageReader reader{aMessage, aActor};
  bool resolve = false;
  if (!IPC::ReadParam(&reader, &resolve)) {
    entry.mReject(ResponseRejectReason::HandlerRejected);
    return MsgValueError;
  }

  if (resolve) {
    // Hand off resolve-case deserialization & success to the callback.
    Result rv = entry.mResolve(&reader);
    if (rv != MsgProcessed) {
      // If deserialization failed, we need to call the reject handler.
      entry.mReject(ResponseRejectReason::HandlerRejected);
    }
    return rv;
  }

  ResponseRejectReason reason;
  if (!IPC::ReadParam(&reader, &reason)) {
    entry.mReject(ResponseRejectReason::HandlerRejected);
    return MsgValueError;
  }
  reader.EndRead();

  entry.mReject(reason);
  return MsgProcessed;
}

void IPDLAsyncReturnsCallbacks::RejectPendingResponses(
    ResponseRejectReason aReason) {
  nsTArray<Entry> pending = std::move(mMap);
  for (auto& entry : pending) {
    entry.mReject(aReason);
  }
}

}  // namespace ipc
}  // namespace mozilla

namespace IPC {

void ParamTraits<mozilla::ipc::IProtocol*>::Write(MessageWriter* aWriter,
                                                  const paramType& aParam) {
  MOZ_RELEASE_ASSERT(aWriter->GetActor(),
                     "Cannot serialize managed actors without an actor");

  mozilla::ipc::ActorId id = mozilla::ipc::IProtocol::kNullActorId;
  if (aParam) {
    id = aParam->Id();
    MOZ_RELEASE_ASSERT(id != mozilla::ipc::IProtocol::kNullActorId,
                       "Actor has ID of 0?");
    MOZ_RELEASE_ASSERT(aParam->CanSend(),
                       "Actor must still be open when sending");
    MOZ_RELEASE_ASSERT(
        aWriter->GetActor()->GetIPCChannel() == aParam->GetIPCChannel(),
        "Actor must be from the same tree as the actor it is being sent over");
  }

  IPC::WriteParam(aWriter, id);
}

bool ParamTraits<mozilla::ipc::IProtocol*>::Read(MessageReader* aReader,
                                                 paramType* aResult) {
  MOZ_RELEASE_ASSERT(aReader->GetActor(),
                     "Cannot serialize managed actors without an actor");

  mozilla::ipc::ActorId id;
  if (!IPC::ReadParam(aReader, &id)) {
    return false;
  }

  if (id == mozilla::ipc::IProtocol::kNullActorId) {
    *aResult = nullptr;
    return true;
  }

  *aResult = aReader->GetActor()->Lookup(id);
  return *aResult != nullptr;
}

}  // namespace IPC
