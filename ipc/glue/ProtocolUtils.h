/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_ProtocolUtils_h
#define mozilla_ipc_ProtocolUtils_h

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>
#include "IPCMessageStart.h"
#include "base/basictypes.h"
#include "base/process.h"
#include "chrome/common/ipc_message.h"
#include "mojo/core/ports/port_ref.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/MoveOnlyFunction.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/ipc/MessageLink.h"
#include "mozilla/ipc/Shmem.h"
#include "nsPrintfCString.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsISupports.h"
#include "nsTArrayForwardDeclare.h"
#include "nsTHashSet.h"

// XXX Things that could be moved to ProtocolUtils.cpp
#include "base/process_util.h"  // for CloseProcessHandle
#include "prenv.h"              // for PR_GetEnv

#if defined(ANDROID) && defined(DEBUG)
#  include <android/log.h>
#endif

template <typename T>
class nsPtrHashKey;

// WARNING: this takes into account the private, special-message-type
// enum in ipc_channel.h.  They need to be kept in sync.
namespace {
// XXX the max message ID is actually kuint32max now ... when this
// changed, the assumptions of the special message IDs changed in that
// they're not carving out messages from likely-unallocated space, but
// rather carving out messages from the end of space allocated to
// protocol 0.  Oops!  We can get away with this until protocol 0
// starts approaching its 65,536th message.
enum {
  // Message types used by DataPipe
  DATA_PIPE_CLOSED_MESSAGE_TYPE = kuint16max - 18,
  DATA_PIPE_BYTES_CONSUMED_MESSAGE_TYPE = kuint16max - 17,

  // Message types used by NodeChannel
  ACCEPT_INVITE_MESSAGE_TYPE = kuint16max - 16,
  REQUEST_INTRODUCTION_MESSAGE_TYPE = kuint16max - 15,
  INTRODUCE_MESSAGE_TYPE = kuint16max - 14,
  BROADCAST_MESSAGE_TYPE = kuint16max - 13,
  EVENT_MESSAGE_TYPE = kuint16max - 12,

  // Message types used by MessageChannel
  MANAGED_ENDPOINT_DROPPED_MESSAGE_TYPE = kuint16max - 11,
  MANAGED_ENDPOINT_BOUND_MESSAGE_TYPE = kuint16max - 10,
  IMPENDING_SHUTDOWN_MESSAGE_TYPE = kuint16max - 9,
  BUILD_IDS_MATCH_MESSAGE_TYPE = kuint16max - 8,
  BUILD_ID_MESSAGE_TYPE = kuint16max - 7,  // unused
  CHANNEL_OPENED_MESSAGE_TYPE = kuint16max - 6,
  SHMEM_DESTROYED_MESSAGE_TYPE = kuint16max - 5,
  SHMEM_CREATED_MESSAGE_TYPE = kuint16max - 4,
  GOODBYE_MESSAGE_TYPE = kuint16max - 3,
  CANCEL_MESSAGE_TYPE = kuint16max - 2,

  // kuint16max - 1 is used by ipc_channel.h.
};

}  // namespace

class MessageLoop;
class PickleIterator;
class nsISerialEventTarget;

namespace mozilla {
class SchedulerGroup;
class UntypedManagedContainer;

namespace dom {
class ContentParent;
}  // namespace dom

namespace net {
class NeckoParent;
}  // namespace net

namespace ipc {

class ProtocolFdMapping;
class ProtocolCloneContext;

// Helper type used to specify process info when constructing endpoints for
// [NeedsOtherPid] toplevel actors.
struct EndpointProcInfo {
  base::ProcessId mPid = base::kInvalidProcessId;
  GeckoChildID mChildID = kInvalidGeckoChildID;

  bool operator==(const EndpointProcInfo& aOther) const {
    return mPid == aOther.mPid && mChildID == aOther.mChildID;
  }
  bool operator!=(const EndpointProcInfo& aOther) const {
    return !operator==(aOther);
  }

  static EndpointProcInfo Invalid() { return {}; }
  static EndpointProcInfo Current();
};

// Used to pass references to protocol actors across the wire.
// Actors created on the parent-side have a positive ID, and actors
// allocated on the child side have a negative ID.
using ActorId = IPC::Message::routeid_t;

enum class LinkStatus : uint8_t {
  // The actor has not established a link yet, or the actor is no longer in use
  // by IPC, and its 'Dealloc' method has been called or is being called.
  //
  // NOTE: This state is used instead of an explicit `Freed` state when IPC no
  // longer holds references to the current actor as we currently re-open
  // existing actors. Once we fix these poorly behaved actors, this loopback
  // state can be split to have the final state not be the same as the initial
  // state.
  Inactive,

  // A live link is connected to the other side of this actor.
  Connected,

  // The link has begun being destroyed. Messages may no longer be sent or
  // received. The ActorDestroy method is queued to be called, but has not been
  // invoked yet, as managed actors still need to be destroyed first.
  Doomed,

  // The actor has been destroyed, and ActorDestroy has been called, however an
  // ActorLifecycleProxy still holds a reference to the actor.
  Destroyed,
};

typedef IPCMessageStart ProtocolId;

// Generated by IPDL compiler
const char* ProtocolIdToName(IPCMessageStart aId);

// Trait type used to write generic code relating to a specific IPDL actor.
template <class T>
struct ActorTraits;

// Concept supported by all IPDL generated actor types. Types supporting this
// concept can be used to perform checked downcasts from generic I*Protocol
// types to the P* protocol types.
template <typename T>
concept IPDLActorType = std::derived_from<T, IProtocol> && requires {
  { ActorTraits<T>::kProtocolId } -> std::convertible_to<ProtocolId>;
  { ActorTraits<T>::kSide } -> std::convertible_to<Side>;
};

class IRefCountedProtocol;
class IToplevelProtocol;
class ActorLifecycleProxy;
class WeakActorLifecycleProxy;
class IPDLResolverInner;
class UntypedManagedEndpoint;

class IProtocol : public HasResultCodes {
 public:
  enum ActorDestroyReason {
    FailedConstructor,
    Deletion,
    AncestorDeletion,
    NormalShutdown,
    AbnormalShutdown,
    ManagedEndpointDropped
  };

  using ProcessId = base::ProcessId;
  using Message = IPC::Message;

  IProtocol(ProtocolId aProtoId, Side aSide)
      : mId(0),
        mProtocolId(aProtoId),
        mSide(aSide),
        mLinkStatus(LinkStatus::Inactive),
        mLifecycleProxy(nullptr),
        mManager(nullptr),
        mToplevel(nullptr) {}

  IToplevelProtocol* ToplevelProtocol() { return mToplevel; }
  const IToplevelProtocol* ToplevelProtocol() const { return mToplevel; }

  // Lookup() is forwarded directly to the toplevel protocol.
  IProtocol* Lookup(ActorId aId);

  Shmem CreateSharedMemory(size_t aSize, bool aUnsafe);
  Shmem::Segment* LookupSharedMemory(ActorId aId);
  bool IsTrackingSharedMemory(const Shmem::Segment* aSegment);
  bool DestroySharedMemory(Shmem& aShmem);

  MessageChannel* GetIPCChannel();
  const MessageChannel* GetIPCChannel() const;

  // Get the nsISerialEventTarget which all messages sent to this actor will be
  // processed on. Unless stated otherwise, all operations on IProtocol which
  // don't occur on this `nsISerialEventTarget` are unsafe.
  nsISerialEventTarget* GetActorEventTarget();

  // Actor lifecycle and other properties.
  ProtocolId GetProtocolId() const { return mProtocolId; }
  const char* GetProtocolName() const { return ProtocolIdToName(mProtocolId); }

  ActorId Id() const { return mId; }
  IRefCountedProtocol* Manager() const { return mManager; }

  uint32_t AllManagedActorsCount() const;

  ActorLifecycleProxy* GetLifecycleProxy() { return mLifecycleProxy; }
  WeakActorLifecycleProxy* GetWeakLifecycleProxy();

  Side GetSide() const { return mSide; }
  bool CanSend() const { return mLinkStatus == LinkStatus::Connected; }

  // Deallocate a managee given its type.
  virtual void DeallocManagee(ProtocolId, IProtocol*) = 0;

  virtual Result OnMessageReceived(const Message& aMessage) = 0;
  virtual Result OnMessageReceived(const Message& aMessage,
                                   UniquePtr<Message>& aReply) = 0;
  bool AllocShmem(size_t aSize, Shmem* aOutMem);
  bool AllocUnsafeShmem(size_t aSize, Shmem* aOutMem);
  bool DeallocShmem(Shmem& aMem);

  void FatalError(const char* const aErrorMsg);
  virtual void HandleFatalError(const char* aErrorMsg);

 protected:
  virtual ~IProtocol();

  friend class IToplevelProtocol;
  friend class ActorLifecycleProxy;
  friend class IPDLResolverInner;
  friend class UntypedManagedEndpoint;
  friend struct IPC::ParamTraits<IProtocol*>;

  // We have separate functions because the accessibility code and BrowserParent
  // manually calls SetManager.
  void SetManager(IRefCountedProtocol* aManager);

  // Clear `mManager` and `mToplevel` to nullptr. Only intended to be called
  // within the unlink implementation of cycle collected IPDL actors with cycle
  // collected managers.
  void UnlinkManager();

  // Sets the manager for the protocol and registers the protocol with
  // its manager, setting up channels for the protocol as well.  Not
  // for use outside of IPDL.
  bool SetManagerAndRegister(IRefCountedProtocol* aManager,
                             ActorId aId = kNullActorId);

  // Helpers for calling `Send` on our underlying IPC channel.
  bool ChannelSend(UniquePtr<IPC::Message> aMsg,
                   IPC::Message::seqno_t* aSeqno = nullptr);
  bool ChannelSend(UniquePtr<IPC::Message> aMsg,
                   UniquePtr<IPC::Message>* aReply);

  // Internal method called when the actor becomes connected.
  already_AddRefed<ActorLifecycleProxy> ActorConnected();

  // Internal method called to indicate an actor will become disconnected.
  // Implicitly called by ActorDisconnected.
  void DoomSubtree();

  // Internal method called when actor becomes disconnected.
  void ActorDisconnected(ActorDestroyReason aWhy);

  // Gets the list of ProtocolIds managed by this protocol.
  virtual Span<const ProtocolId> ManagedProtocolIds() const = 0;

  // Get the ManagedContainer for actors of the given protocol managed by this
  // protocol. This returns a container if and only if passed a ProtocolId in
  // `ManagedProtocolIds()`.
  virtual UntypedManagedContainer* GetManagedActors(ProtocolId aProtocol) = 0;
  const UntypedManagedContainer* GetManagedActors(ProtocolId aProtocol) const {
    return const_cast<IProtocol*>(this)->GetManagedActors(aProtocol);
  }

  // Called internally to reject the callbacks for all async-returns methods
  // in-progress on this actor with the given ResponseRejectReason.
  virtual void RejectPendingResponses(ResponseRejectReason aReason) {}

  // Called when the actor has been destroyed due to an error, a __delete__
  // message, or a __doom__ reply.
  virtual void ActorDestroy(ActorDestroyReason aWhy) {}

  // Called when IPC has acquired its first reference to the actor. This method
  // may take references which will later be freed by `ActorDealloc`.
  virtual void ActorAlloc() = 0;

  // Called when IPC has released its final reference to the actor. It will call
  // the dealloc method, causing the actor to be actually freed.
  //
  // The actor has been freed after this method returns.
  virtual void ActorDealloc() = 0;

  static const ActorId kNullActorId = 0;

 private:
#ifdef DEBUG
  void WarnMessageDiscarded(IPC::Message* aMsg);
#else
  void WarnMessageDiscarded(IPC::Message*) {}
#endif

  // Internal function returning an arbitrary directly managed actor. Used to
  // identify managed actors to destroy when tearing down an actor tree.
  IProtocol* PeekManagedActor() const;

  ActorId mId;
  const ProtocolId mProtocolId;
  const Side mSide;
  LinkStatus mLinkStatus;
  ActorLifecycleProxy* mLifecycleProxy;
  RefPtr<IRefCountedProtocol> mManager;
  IToplevelProtocol* mToplevel;
};

#define IPC_OK() mozilla::ipc::IPCResult::Ok()
#define IPC_FAIL(actor, why) \
  mozilla::ipc::IPCResult::Fail(WrapNotNull(actor), __func__, (why))
#define IPC_FAIL_NO_REASON(actor) \
  mozilla::ipc::IPCResult::Fail(WrapNotNull(actor), __func__)

/*
 * IPC_FAIL_UNSAFE_PRINTF(actor, format, ...)
 *
 * Create a failure IPCResult with a dynamic reason-string.
 *
 * @note This macro causes data collection because IPC failure reasons may be
 * sent to crash-stats, where they are publicly visible. Firefox data stewards
 * must do data review on usages of this macro.
 */
#define IPC_FAIL_UNSAFE_PRINTF(actor, format, ...) \
  mozilla::ipc::IPCResult::FailUnsafePrintfImpl(   \
      WrapNotNull(actor), __func__, nsPrintfCString(format, ##__VA_ARGS__))

#define IPC_TEST_FAIL(actor) \
  mozilla::ipc::IPCResult::FailForTesting(WrapNotNull(actor), __func__, "")

/**
 * All message deserializers and message handlers should return this type via
 * the above macros. We use a less generic name here to avoid conflict with
 * `mozilla::Result` because we have quite a few `using namespace mozilla::ipc;`
 * in the code base.
 *
 * Note that merely constructing a failure-result, whether directly or via the
 * IPC_FAIL macros, causes the associated error message to be processed
 * immediately.
 */
class IPCResult {
 public:
  static IPCResult Ok() { return IPCResult(true); }

  // IPC failure messages can sometimes end up in telemetry. As such, to avoid
  // accidentally exfiltrating sensitive information without a data review, we
  // require that they be constant strings.
  template <size_t N, size_t M>
  static IPCResult Fail(NotNull<IProtocol*> aActor, const char (&aWhere)[N],
                        const char (&aWhy)[M]) {
    return FailImpl(aActor, aWhere, aWhy);
  }
  template <size_t N>
  static IPCResult Fail(NotNull<IProtocol*> aActor, const char (&aWhere)[N]) {
    return FailImpl(aActor, aWhere, "");
  }

  MOZ_IMPLICIT operator bool() const { return mSuccess; }

  // Only used by IPC_FAIL_UNSAFE_PRINTF (q.v.). Do not call this directly. (Or
  // at least get data-review's approval if you do.)
  template <size_t N>
  static IPCResult FailUnsafePrintfImpl(NotNull<IProtocol*> aActor,
                                        const char (&aWhere)[N],
                                        nsPrintfCString const& aWhy) {
    return FailImpl(aActor, aWhere, aWhy.get());
  }

  // Only used in testing.
  static IPCResult FailForTesting(NotNull<IProtocol*> aActor,
                                  const char* aWhere, const char* aWhy);

 private:
  static IPCResult FailImpl(NotNull<IProtocol*> aActor, const char* aWhere,
                            const char* aWhy);

  explicit IPCResult(bool aResult) : mSuccess(aResult) {}
  bool mSuccess;
};

class UntypedEndpoint;

template <class PFooSide>
class Endpoint;

template <class PFooSide>
class ManagedEndpoint;

/**
 * All refcounted protocols should inherit this class.
 */
class IRefCountedProtocol : public IProtocol {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  using IProtocol::IProtocol;
};

/**
 * All top-level protocols should inherit this class.
 *
 * IToplevelProtocol tracks all top-level protocol actors created from
 * this protocol actor.
 */
class IToplevelProtocol : public IRefCountedProtocol {
  friend class IProtocol;
  template <class PFooSide>
  friend class Endpoint;

 protected:
  explicit IToplevelProtocol(const char* aName, ProtocolId aProtoId,
                             Side aSide);
  ~IToplevelProtocol() = default;

 public:
  // Shadows the method on IProtocol, which will forward to the top.
  IProtocol* Lookup(ActorId aId);

  // Ensures aId is from the remote side's range, and reserves a slot in
  // mActorMap for a future call to SetManagerAndRegister.
  [[nodiscard]] bool TryReserve(ActorId aId);

  // Abandon a reservation if SetManagerAndRegister will never be called.
  void ClearReservation(ActorId aId);

  Shmem CreateSharedMemory(size_t aSize, bool aUnsafe);
  Shmem::Segment* LookupSharedMemory(Shmem::id_t aId);
  bool IsTrackingSharedMemory(const Shmem::Segment* aSegment);
  bool DestroySharedMemory(Shmem& aShmem);

  MessageChannel* GetIPCChannel() { return &mChannel; }
  const MessageChannel* GetIPCChannel() const { return &mChannel; }

  void SetOtherEndpointProcInfo(EndpointProcInfo aOtherProcInfo);

  virtual void ProcessingError(Result aError, const char* aMsgName) {}

  bool Open(ScopedPort aPort, const nsID& aMessageChannelId,
            EndpointProcInfo aOtherProcInfo,
            nsISerialEventTarget* aEventTarget = nullptr);

  bool Open(IToplevelProtocol* aTarget, nsISerialEventTarget* aEventTarget,
            mozilla::ipc::Side aSide = mozilla::ipc::UnknownSide);

  // Open a toplevel actor such that both ends of the actor's channel are on
  // the same thread. This method should be called on the thread to perform
  // the link.
  //
  // WARNING: Attempting to send a sync message on the same thread will crash.
  bool OpenOnSameThread(IToplevelProtocol* aTarget,
                        mozilla::ipc::Side aSide = mozilla::ipc::UnknownSide);

  /**
   * This sends a special message that is processed on the IO thread, so that
   * other actors can know that the process will soon shutdown.
   */
  void NotifyImpendingShutdown();

  void Close();

  void SetReplyTimeoutMs(int32_t aTimeoutMs);

  void DeallocShmems();
  bool ShmemCreated(const Message& aMsg);
  bool ShmemDestroyed(const Message& aMsg);

  virtual bool ShouldContinueFromReplyTimeout() { return false; }

  // WARNING: This function is called with the MessageChannel monitor held.
  virtual void IntentionalCrash() { MOZ_CRASH("Intentional IPDL crash"); }

  // The code here is only useful for fuzzing. It should not be used for any
  // other purpose.
#ifdef DEBUG
  // Returns true if we should simulate a timeout.
  // WARNING: This is a testing-only function that is called with the
  // MessageChannel monitor held. Don't do anything fancy here or we could
  // deadlock.
  virtual bool ArtificialTimeout() { return false; }

  // Returns true if we want to cause the worker thread to sleep with the
  // monitor unlocked.
  virtual bool NeedArtificialSleep() { return false; }

  // This function should be implemented to sleep for some amount of time on
  // the worker thread. Will only be called if NeedArtificialSleep() returns
  // true.
  virtual void ArtificialSleep() {}
#else
  bool ArtificialTimeout() { return false; }
  bool NeedArtificialSleep() { return false; }
  void ArtificialSleep() {}
#endif

  bool IsOnCxxStack() const;

  virtual void ProcessRemoteNativeEventsInInterruptCall() {}

  virtual void OnChannelReceivedMessage(const Message& aMsg) {}

  // MessageChannel lifecycle callbacks.
  void OnIPCChannelOpened() {
    // Leak the returned ActorLifecycleProxy reference. It will be destroyed in
    // `OnChannelClose` or `OnChannelError`.
    ActorConnected().leak();
  }
  void OnChannelClose() {
    // Re-acquire the ActorLifecycleProxy reference acquired in
    // OnIPCChannelOpened.
    RefPtr<ActorLifecycleProxy> proxy = dont_AddRef(GetLifecycleProxy());
    ActorDisconnected(NormalShutdown);
    DeallocShmems();
  }
  void OnChannelError() {
    // Re-acquire the ActorLifecycleProxy reference acquired in
    // OnIPCChannelOpened.
    RefPtr<ActorLifecycleProxy> proxy = dont_AddRef(GetLifecycleProxy());
    ActorDisconnected(AbnormalShutdown);
    DeallocShmems();
  }

  base::ProcessId OtherPidMaybeInvalid() const { return mOtherPid; }
  GeckoChildID OtherChildIDMaybeInvalid() const { return mOtherChildID; }

 private:
  int64_t NextId();

  template <class T>
  using IDMap = nsTHashMap<int64_t, T>;

  base::ProcessId mOtherPid;
  GeckoChildID mOtherChildID;

  // NOTE NOTE NOTE
  // Used to be on mState
  int64_t mLastLocalId;
  IDMap<RefPtr<ActorLifecycleProxy>> mActorMap;
  IDMap<RefPtr<Shmem::Segment>> mShmemMap;

  MessageChannel mChannel;
};

class IShmemAllocator {
 public:
  virtual bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) = 0;
  virtual bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) = 0;
  virtual bool DeallocShmem(mozilla::ipc::Shmem& aShmem) = 0;
};

#define FORWARD_SHMEM_ALLOCATOR_TO(aImplClass)                             \
  virtual bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem)       \
      override {                                                           \
    return aImplClass::AllocShmem(aSize, aShmem);                          \
  }                                                                        \
  virtual bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) \
      override {                                                           \
    return aImplClass::AllocUnsafeShmem(aSize, aShmem);                    \
  }                                                                        \
  virtual bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override {        \
    return aImplClass::DeallocShmem(aShmem);                               \
  }

inline bool LoggingEnabled() {
#if defined(DEBUG) || defined(FUZZING)
  return !!PR_GetEnv("MOZ_IPC_MESSAGE_LOG");
#else
  return false;
#endif
}

#if defined(DEBUG) || defined(FUZZING)
bool LoggingEnabledFor(const char* aTopLevelProtocol, mozilla::ipc::Side aSide,
                       const char* aFilter);
#endif

inline bool LoggingEnabledFor(const char* aTopLevelProtocol,
                              mozilla::ipc::Side aSide) {
#if defined(DEBUG) || defined(FUZZING)
  return LoggingEnabledFor(aTopLevelProtocol, aSide,
                           PR_GetEnv("MOZ_IPC_MESSAGE_LOG"));
#else
  return false;
#endif
}

MOZ_NEVER_INLINE void LogMessageForProtocol(const char* aTopLevelProtocol,
                                            base::ProcessId aOtherPid,
                                            const char* aContextDescription,
                                            uint32_t aMessageId,
                                            MessageDirection aDirection);

MOZ_NEVER_INLINE void ProtocolErrorBreakpoint(const char* aMsg);

// IPC::MessageReader and IPC::MessageWriter call this function for FatalError
// calls which come from serialization/deserialization.
MOZ_NEVER_INLINE void PickleFatalError(const char* aMsg, IProtocol* aActor);

// The code generator calls this function for errors which come from the
// methods of protocols.  Doing this saves codesize by making the error
// cases significantly smaller.
MOZ_NEVER_INLINE void FatalError(const char* aMsg, bool aIsParent);

// The code generator calls this function for errors which are not
// protocol-specific: errors in generated struct methods or errors in
// transition functions, for instance.  Doing this saves codesize by
// by making the error cases significantly smaller.
MOZ_NEVER_INLINE void LogicError(const char* aMsg);

MOZ_NEVER_INLINE void ActorIdReadError(const char* aActorDescription);

MOZ_NEVER_INLINE void BadActorIdError(const char* aActorDescription);

MOZ_NEVER_INLINE void ActorLookupError(const char* aActorDescription);

MOZ_NEVER_INLINE void MismatchedActorTypeError(const char* aActorDescription);

MOZ_NEVER_INLINE void UnionTypeReadError(const char* aUnionName);

MOZ_NEVER_INLINE void ArrayLengthReadError(const char* aElementName);

MOZ_NEVER_INLINE void SentinelReadError(const char* aElementName);

/**
 * Annotate the crash reporter with the error code from the most recent system
 * call. Returns the system error.
 */
void AnnotateSystemError();

// Statically checked cast between different types representing the same actor.
//
// e.g. `ActorCast<NeckoParent>(...)` called on a `PNeckoParent*` pointer will
// perform the cast.
//
// NOTE: Actors with `Impl=virtual` do not support casting to their concrete
// type, as virtual actors may have multiple concrete implementations.
template <IPDLActorType To, IPDLActorType From>
  requires(ActorTraits<From>::kProtocolId == ActorTraits<To>::kProtocolId &&
           ActorTraits<From>::kSide == ActorTraits<To>::kSide)
constexpr To* ActorCast(From* aActor) {
  return static_cast<To*>(aActor);
}

// Dynamically checked cast to another type representing the same actor object.
//
// e.g. `ActorDynCast<ContentParent>(...)` called on a generic
// `IToplevelProtocol*` pointer will return the value downcast if it names a
// `ContentParent` actor.
//
// NOTE: Actors with `Impl=virtual` do not support casting to their concrete
// type, as virtual actors may have multiple concrete implementations.
template <IPDLActorType To>
constexpr To* ActorDynCast(IProtocol* aActor) {
  if (aActor && aActor->GetProtocolId() == ActorTraits<To>::kProtocolId &&
      aActor->GetSide() == ActorTraits<To>::kSide) {
    return static_cast<To*>(aActor);
  }
  return nullptr;
}

// The ActorLifecycleProxy is a helper type used internally by IPC to maintain a
// maybe-owning reference to an IProtocol object. For well-behaved actors
// which are not freed until after their `Dealloc` method is called, a
// reference to an actor's `ActorLifecycleProxy` object is an owning one, as the
// `Dealloc` method will only be called when all references to the
// `ActorLifecycleProxy` are released.
//
// Unfortunately, some actors may be destroyed before their `Dealloc` method
// is called. For these actors, `ActorLifecycleProxy` acts as a weak pointer,
// and will begin to return `nullptr` from its `Get()` method once the
// corresponding actor object has been destroyed.
//
// When calling a `Recv` method, IPC will hold a `ActorLifecycleProxy` reference
// to the target actor, meaning that well-behaved actors can behave as though a
// strong reference is being held.
//
// Generic IPC code MUST treat ActorLifecycleProxy references as weak
// references!
class ActorLifecycleProxy {
 public:
  ActorLifecycleProxy(const ActorLifecycleProxy&) = delete;
  ActorLifecycleProxy& operator=(const ActorLifecycleProxy&) = delete;

  NS_INLINE_DECL_REFCOUNTING_ONEVENTTARGET(ActorLifecycleProxy)

  IProtocol* Get() { return mActor; }

  WeakActorLifecycleProxy* GetWeakProxy();

 private:
  friend class IProtocol;

  explicit ActorLifecycleProxy(IProtocol* aActor);
  ~ActorLifecycleProxy();

  IProtocol* MOZ_NON_OWNING_REF mActor;

  // When requested, the current self-referencing weak reference for this
  // ActorLifecycleProxy.
  RefPtr<WeakActorLifecycleProxy> mWeakProxy;
};

// Unlike ActorLifecycleProxy, WeakActorLifecycleProxy only holds a weak
// reference to both the proxy and the actual actor, meaning that holding this
// type will not attempt to keep the actor object alive.
//
// This type is safe to hold on threads other than the actor's thread, but is
// _NOT_ safe to access on other threads, as actors and ActorLifecycleProxy
// objects are not threadsafe.
class WeakActorLifecycleProxy final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WeakActorLifecycleProxy)

  // May only be called on the actor's event target.
  // Will return `nullptr` if the actor has already been destroyed from IPC's
  // point of view.
  IProtocol* Get() const;

  // Safe to call on any thread.
  nsISerialEventTarget* ActorEventTarget() const { return mActorEventTarget; }

 private:
  friend class ActorLifecycleProxy;

  explicit WeakActorLifecycleProxy(ActorLifecycleProxy* aProxy);
  ~WeakActorLifecycleProxy();

  WeakActorLifecycleProxy(const WeakActorLifecycleProxy&) = delete;
  WeakActorLifecycleProxy& operator=(const WeakActorLifecycleProxy&) = delete;

  // This field may only be accessed on the actor's thread, and will be
  // automatically cleared when the ActorLifecycleProxy is destroyed.
  ActorLifecycleProxy* MOZ_NON_OWNING_REF mProxy;

  // The serial event target which owns the actor, and is the only thread where
  // it is OK to access the ActorLifecycleProxy.
  const nsCOMPtr<nsISerialEventTarget> mActorEventTarget;
};

class IPDLResolverInner final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(IPDLResolverInner,
                                                     Destroy())

  explicit IPDLResolverInner(UniquePtr<IPC::Message> aReply, IProtocol* aActor);

  template <typename F>
  void Resolve(F&& aWrite) {
    ResolveOrReject(true, std::forward<F>(aWrite));
  }

 private:
  void ResolveOrReject(bool aResolve,
                       FunctionRef<void(IPC::Message*, IProtocol*)> aWrite);

  void Destroy();
  ~IPDLResolverInner();

  UniquePtr<IPC::Message> mReply;
  RefPtr<WeakActorLifecycleProxy> mWeakProxy;
};

// Member type added by the IPDL compiler to actors with async-returns messages.
// Manages a table mapping outstanding async message seqnos to the corresponding
// IPDL-generated callback which handles validating, deserializing, and
// dispatching the reply.
class IPDLAsyncReturnsCallbacks : public HasResultCodes {
 public:
  // Internal resolve callback signature. The callback should deserialize from
  // the IPC::MessageReader* argument.
  using Callback =
      mozilla::MoveOnlyFunction<Result(IPC::MessageReader* IProtocol)>;
  using seqno_t = IPC::Message::seqno_t;
  using msgid_t = IPC::Message::msgid_t;

  void AddCallback(seqno_t aSeqno, msgid_t aType, Callback aResolve,
                   RejectCallback aReject);
  Result GotReply(IProtocol* aActor, const IPC::Message& aMessage);
  void RejectPendingResponses(ResponseRejectReason aReason);

 private:
  struct EntryKey {
    seqno_t mSeqno;
    msgid_t mType;

    bool operator==(const EntryKey& aOther) const;
    bool operator<(const EntryKey& aOther) const;
  };
  struct Entry : EntryKey {
    Callback mResolve;
    RejectCallback mReject;
  };

  // NOTE: We expect this table to be quite small most of the time (usually 0-1
  // entries), so use a sorted array as backing storage to reduce unnecessary
  // overhead.
  nsTArray<Entry> mMap;
};

}  // namespace ipc

// Base class for `ManagedContainer` - contains a series of IProtocol* instances
// of the same type (as specified by the subclass), and allows iterating over
// them.
class UntypedManagedContainer {
 public:
  using iterator = nsTArray<mozilla::ipc::IProtocol*>::const_iterator;

  iterator begin() const { return mArray.cbegin(); }
  iterator end() const { return mArray.cend(); }

  bool IsEmpty() const { return mArray.IsEmpty(); }
  uint32_t Count() const { return mArray.Length(); }

 protected:
  explicit UntypedManagedContainer(mozilla::ipc::ProtocolId aProtocolId)
#ifdef DEBUG
      : mProtocolId(aProtocolId)
#endif
  {
  }

 private:
  friend class mozilla::ipc::IProtocol;

  bool EnsureRemoved(mozilla::ipc::IProtocol* aElement) {
    return mArray.RemoveElementSorted(aElement);
  }

  void Insert(mozilla::ipc::IProtocol* aElement) {
    MOZ_ASSERT(aElement->GetProtocolId() == mProtocolId,
               "ManagedContainer can only contain a single protocol");
    // Equivalent to `InsertElementSorted`, avoiding inserting a duplicate
    // element. See bug 1896166.
    size_t index = mArray.IndexOfFirstElementGt(aElement);
    if (index == 0 || mArray[index - 1] != aElement) {
      mArray.InsertElementAt(index, aElement);
    }
  }

  nsTArray<mozilla::ipc::IProtocol*> mArray;
#ifdef DEBUG
  mozilla::ipc::ProtocolId mProtocolId;
#endif
};

template <typename Protocol>
class ManagedContainer : public UntypedManagedContainer {
 public:
  ManagedContainer() : UntypedManagedContainer(Protocol::kProtocolId) {}

  // Input iterator which downcasts to the protocol type while iterating over
  // the untyped container.
  class iterator {
   public:
    using value_type = Protocol*;
    using difference_type = ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type;
    using iterator_category = std::input_iterator_tag;

   private:
    friend class ManagedContainer;
    explicit iterator(const UntypedManagedContainer::iterator& aIter)
        : mIter(aIter) {}
    UntypedManagedContainer::iterator mIter;

   public:
    iterator() = default;

    bool operator==(const iterator& aRhs) const { return mIter == aRhs.mIter; }
    bool operator!=(const iterator& aRhs) const { return mIter != aRhs.mIter; }

    // NOTE: operator->() cannot be implemented without a proxy type.
    // This is OK, and the same approach taken by C++20's transform_view.
    reference operator*() const { return static_cast<value_type>(*mIter); }

    iterator& operator++() {
      ++mIter;
      return *this;
    }
    iterator operator++(int) { return iterator{mIter++}; }
  };

  iterator begin() const { return iterator{UntypedManagedContainer::begin()}; }
  iterator end() const { return iterator{UntypedManagedContainer::end()}; }

  void ToArray(nsTArray<Protocol*>& aArray) const {
    aArray.SetCapacity(Count());
    for (Protocol* p : *this) {
      aArray.AppendElement(p);
    }
  }
};

template <typename Protocol>
Protocol* LoneManagedOrNullAsserts(
    const ManagedContainer<Protocol>& aManagees) {
  if (aManagees.IsEmpty()) {
    return nullptr;
  }
  MOZ_ASSERT(aManagees.Count() == 1);
  return *aManagees.begin();
}

template <typename Protocol>
Protocol* SingleManagedOrNull(const ManagedContainer<Protocol>& aManagees) {
  if (aManagees.Count() != 1) {
    return nullptr;
  }
  return *aManagees.begin();
}

}  // namespace mozilla

#endif  // mozilla_ipc_ProtocolUtils_h
