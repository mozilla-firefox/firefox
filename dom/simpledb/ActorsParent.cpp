/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActorsParent.h"

// Local includes
#include "SimpleDBCommon.h"

// Global includes
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/FixedBufferOutputStream.h"
#include "mozilla/Maybe.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Unused.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/PBackgroundSDBConnection.h"
#include "mozilla/dom/PBackgroundSDBConnectionParent.h"
#include "mozilla/dom/PBackgroundSDBRequestParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/ClientImpl.h"
#include "mozilla/dom/quota/FileStreams.h"
#include "mozilla/dom/quota/PrincipalUtils.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "NotifyUtils.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIEventTarget.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsIRunnable.h"
#include "nsISeekableStream.h"
#include "nsISupports.h"
#include "nsIThread.h"
#include "nsLiteralString.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStringStream.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"
#include "nsTStringRepr.h"
#include "nsThreadUtils.h"
#include "nscore.h"
#include "prio.h"

namespace mozilla::dom {

using namespace mozilla::dom::quota;
using namespace mozilla::ipc;

namespace {

/*******************************************************************************
 * Constants
 ******************************************************************************/

const uint32_t kCopyBufferSize = 32768;

constexpr auto kSDBSuffix = u".sdb"_ns;

/*******************************************************************************
 * Actor class declarations
 ******************************************************************************/

class StreamHelper final : public Runnable {
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  nsCOMPtr<nsIFileRandomAccessStream> mFileRandomAccessStream;
  nsCOMPtr<nsIRunnable> mCallback;

 public:
  StreamHelper(nsIFileRandomAccessStream* aFileRandomAccessStream,
               nsIRunnable* aCallback);

  void AsyncClose();

 private:
  ~StreamHelper() override;

  void RunOnBackgroundThread();

  void RunOnIOThread();

  NS_DECL_NSIRUNNABLE
};

class Connection final : public PBackgroundSDBConnectionParent {
  ClientDirectoryLockHandle mDirectoryLockHandle;
  nsCOMPtr<nsIFileRandomAccessStream> mFileRandomAccessStream;
  const PrincipalInfo mPrincipalInfo;
  nsCString mOrigin;
  nsString mName;

  PersistenceType mPersistenceType;
  bool mRunningRequest;
  bool mOpen;
  bool mAllowedToClose;
  bool mActorDestroyed;

 public:
  Connection(PersistenceType aPersistenceType,
             const PrincipalInfo& aPrincipalInfo);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::Connection, override)

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    AssertIsOnBackgroundThread();

    return ToMaybeRef(mDirectoryLockHandle.get());
  }

  nsIFileRandomAccessStream* GetFileRandomAccessStream() const {
    AssertIsOnIOThread();

    return mFileRandomAccessStream;
  }

  PersistenceType GetPersistenceType() const { return mPersistenceType; }

  const PrincipalInfo& GetPrincipalInfo() const {
    AssertIsOnBackgroundThread();

    return mPrincipalInfo;
  }

  const nsCString& Origin() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!mOrigin.IsEmpty());

    return mOrigin;
  }

  const nsString& Name() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!mName.IsEmpty());

    return mName;
  }

  void OnNewRequest();

  void OnRequestFinished();

  void OnOpen(
      const nsACString& aOrigin, const nsAString& aName,
      ClientDirectoryLockHandle aDirectoryLockHandle,
      already_AddRefed<nsIFileRandomAccessStream> aFileRandomAccessStream);

  void OnClose();

  void AllowToClose();

 private:
  ~Connection();

  void MaybeCloseStream();

  bool VerifyRequestParams(const SDBRequestParams& aParams) const;

  // IPDL methods.
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  virtual PBackgroundSDBRequestParent* AllocPBackgroundSDBRequestParent(
      const SDBRequestParams& aParams) override;

  virtual mozilla::ipc::IPCResult RecvPBackgroundSDBRequestConstructor(
      PBackgroundSDBRequestParent* aActor,
      const SDBRequestParams& aParams) override;

  virtual bool DeallocPBackgroundSDBRequestParent(
      PBackgroundSDBRequestParent* aActor) override;
};

class ConnectionOperationBase : public Runnable,
                                public PBackgroundSDBRequestParent {
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  RefPtr<Connection> mConnection;
  nsresult mResultCode;
  Atomic<bool> mOperationMayProceed;
  bool mActorDestroyed;

 public:
  nsIEventTarget* OwningEventTarget() const {
    MOZ_ASSERT(mOwningEventTarget);

    return mOwningEventTarget;
  }

  bool IsOnOwningThread() const {
    MOZ_ASSERT(mOwningEventTarget);

    bool current;
    return NS_SUCCEEDED(mOwningEventTarget->IsOnCurrentThread(&current)) &&
           current;
  }

  void AssertIsOnOwningThread() const {
    MOZ_ASSERT(IsOnBackgroundThread());
    MOZ_ASSERT(IsOnOwningThread());
  }

  Connection* GetConnection() const {
    MOZ_ASSERT(mConnection);

    return mConnection;
  }

  nsresult ResultCode() const { return mResultCode; }

  void MaybeSetFailureCode(nsresult aErrorCode) {
    MOZ_ASSERT(NS_FAILED(aErrorCode));

    if (NS_SUCCEEDED(mResultCode)) {
      mResultCode = aErrorCode;
    }
  }

  // May be called on any thread, but you should call IsActorDestroyed() if
  // you know you're on the background thread because it is slightly faster.
  bool OperationMayProceed() const { return mOperationMayProceed; }

  bool IsActorDestroyed() const {
    AssertIsOnOwningThread();

    return mActorDestroyed;
  }

  // May be overridden by subclasses if they need to perform work on the
  // background thread before being dispatched but must always call the base
  // class implementation. Returning false will kill the child actors and
  // prevent dispatch.
  virtual bool Init();

  virtual nsresult Dispatch();

  // This callback will be called on the background thread before releasing the
  // final reference to this request object. Subclasses may perform any
  // additional cleanup here but must always call the base class implementation.
  virtual void Cleanup();

 protected:
  ConnectionOperationBase(Connection* aConnection)
      : Runnable("dom::ConnectionOperationBase"),
        mOwningEventTarget(GetCurrentSerialEventTarget()),
        mConnection(aConnection),
        mResultCode(NS_OK),
        mOperationMayProceed(true),
        mActorDestroyed(false) {
    AssertIsOnOwningThread();
  }

  ~ConnectionOperationBase() override;

  void SendResults();

  void DatabaseWork();

  // Methods that subclasses must implement.
  virtual nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) = 0;

  // Subclasses use this override to set the IPDL response value.
  virtual void GetResponse(SDBRequestResponse& aResponse) = 0;

  // A method that subclasses may implement.
  virtual void OnSuccess();

 private:
  NS_IMETHOD
  Run() override;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;
};

class OpenOp final : public ConnectionOperationBase {
  enum class State {
    // Just created on the PBackground thread, dispatched to the main thread.
    // Next step is FinishOpen.
    Initial,

    // Ensuring quota manager is created and opening directory on the
    // PBackground thread. Next step is either SendingResults if quota manager
    // is not available or DirectoryOpenPending if quota manager is available.
    FinishOpen,

    // Waiting for directory open allowed on the PBackground thread. The next
    // step is either SendingResults if directory lock failed to acquire, or
    // DatabaseWorkOpen if directory lock is acquired.
    DirectoryOpenPending,

    // Waiting to do/doing work on the QuotaManager IO thread. Its next step is
    // SendingResults.
    DatabaseWorkOpen,

    // Waiting to send/sending results on the PBackground thread. Next step is
    // Completed.
    SendingResults,

    // All done.
    Completed
  };

  const SDBRequestOpenParams mParams;
  ClientDirectoryLockHandle mDirectoryLockHandle;
  nsCOMPtr<nsIFileRandomAccessStream> mFileRandomAccessStream;
  // XXX Consider changing this to ClientMetadata.
  quota::OriginMetadata mOriginMetadata;
  State mState;
  bool mFileRandomAccessStreamOpen;

 public:
  OpenOp(Connection* aConnection, const SDBRequestParams& aParams);

  nsresult Dispatch() override;

 private:
  ~OpenOp() override;

  nsresult Open();

  nsresult FinishOpen();

  nsresult SendToIOThread();

  nsresult DatabaseWork();

  void StreamClosedCallback();

  // ConnectionOperationBase overrides
  nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) override;

  void GetResponse(SDBRequestResponse& aResponse) override;

  void OnSuccess() override;

  void Cleanup() override;

  NS_IMETHOD
  Run() override;

  void DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle);

  void DirectoryLockFailed();
};

class SeekOp final : public ConnectionOperationBase {
  const SDBRequestSeekParams mParams;

 public:
  SeekOp(Connection* aConnection, const SDBRequestParams& aParams);

 private:
  ~SeekOp() override = default;

  nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) override;

  void GetResponse(SDBRequestResponse& aResponse) override;
};

class ReadOp final : public ConnectionOperationBase {
  const SDBRequestReadParams mParams;

  RefPtr<FixedBufferOutputStream> mOutputStream;

 public:
  ReadOp(Connection* aConnection, const SDBRequestParams& aParams);

  bool Init() override;

 private:
  ~ReadOp() override = default;

  nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) override;

  void GetResponse(SDBRequestResponse& aResponse) override;
};

class WriteOp final : public ConnectionOperationBase {
  const SDBRequestWriteParams mParams;

  nsCOMPtr<nsIInputStream> mInputStream;

  uint64_t mSize;

 public:
  WriteOp(Connection* aConnection, const SDBRequestParams& aParams);

  bool Init() override;

 private:
  ~WriteOp() override = default;

  nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) override;

  void GetResponse(SDBRequestResponse& aResponse) override;
};

class CloseOp final : public ConnectionOperationBase {
 public:
  explicit CloseOp(Connection* aConnection);

 private:
  ~CloseOp() override = default;

  nsresult DoDatabaseWork(
      nsIFileRandomAccessStream* aFileRandomAccessStream) override;

  void GetResponse(SDBRequestResponse& aResponse) override;

  void OnSuccess() override;
};

/*******************************************************************************
 * Other class declarations
 ******************************************************************************/

class QuotaClient final : public mozilla::dom::quota::Client {
  static QuotaClient* sInstance;

 public:
  QuotaClient();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(QuotaClient, override)

  Type GetType() override;

  Result<UsageInfo, nsresult> InitOrigin(PersistenceType aPersistenceType,
                                         const OriginMetadata& aOriginMetadata,
                                         const AtomicBool& aCanceled) override;

  nsresult InitOriginWithoutTracking(PersistenceType aPersistenceType,
                                     const OriginMetadata& aOriginMetadata,
                                     const AtomicBool& aCanceled) override;

  Result<UsageInfo, nsresult> GetUsageForOrigin(
      PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
      const AtomicBool& aCanceled) override;

  void OnOriginClearCompleted(const OriginMetadata& aOriginMetadata) override;

  void OnRepositoryClearCompleted(PersistenceType aPersistenceType) override;

  void ReleaseIOThreadObjects() override;

  void AbortOperationsForLocks(
      const DirectoryLockIdTable& aDirectoryLockIds) override;

  void AbortOperationsForProcess(ContentParentId aContentParentId) override;

  void AbortAllOperations() override;

  void StartIdleMaintenance() override;

  void StopIdleMaintenance() override;

 private:
  ~QuotaClient() override;

  void InitiateShutdown() override;
  bool IsShutdownCompleted() const override;
  nsCString GetShutdownStatus() const override;
  void ForceKillActors() override;
  void FinalizeShutdown() override;
};

/*******************************************************************************
 * Globals
 ******************************************************************************/

using ConnectionArray = nsTArray<NotNull<RefPtr<Connection>>>;

StaticAutoPtr<ConnectionArray> gOpenConnections;

template <typename Condition>
void AllowToCloseConnectionsMatching(const Condition& aCondition) {
  AssertIsOnBackgroundThread();

  if (gOpenConnections) {
    for (const auto& connection : *gOpenConnections) {
      if (aCondition(*connection)) {
        connection->AllowToClose();
      }
    }
  }
}

}  // namespace

/*******************************************************************************
 * Exported functions
 ******************************************************************************/

already_AddRefed<PBackgroundSDBConnectionParent>
AllocPBackgroundSDBConnectionParent(const PersistenceType& aPersistenceType,
                                    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  if (NS_WARN_IF(!IsValidPersistenceType(aPersistenceType))) {
    MOZ_CRASH_UNLESS_FUZZING();
    return nullptr;
  }

  if (NS_WARN_IF(aPrincipalInfo.type() == PrincipalInfo::TNullPrincipalInfo)) {
    MOZ_CRASH_UNLESS_FUZZING();
    return nullptr;
  }

  if (NS_WARN_IF(!quota::IsPrincipalInfoValid(aPrincipalInfo))) {
    MOZ_CRASH_UNLESS_FUZZING();
    return nullptr;
  }

  RefPtr<Connection> actor = new Connection(aPersistenceType, aPrincipalInfo);

  return actor.forget();
}

bool RecvPBackgroundSDBConnectionConstructor(
    PBackgroundSDBConnectionParent* aActor,
    const PersistenceType& aPersistenceType,
    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  return true;
}

namespace simpledb {

already_AddRefed<mozilla::dom::quota::Client> CreateQuotaClient() {
  AssertIsOnBackgroundThread();

  RefPtr<QuotaClient> client = new QuotaClient();
  return client.forget();
}

}  // namespace simpledb

/*******************************************************************************
 * StreamHelper
 ******************************************************************************/

StreamHelper::StreamHelper(nsIFileRandomAccessStream* aFileRandomAccessStream,
                           nsIRunnable* aCallback)
    : Runnable("dom::StreamHelper"),
      mOwningEventTarget(GetCurrentSerialEventTarget()),
      mFileRandomAccessStream(aFileRandomAccessStream),
      mCallback(aCallback) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aFileRandomAccessStream);
  MOZ_ASSERT(aCallback);
}

StreamHelper::~StreamHelper() {
  MOZ_ASSERT(!mFileRandomAccessStream);
  MOZ_ASSERT(!mCallback);
}

void StreamHelper::AsyncClose() {
  AssertIsOnBackgroundThread();

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  MOZ_ALWAYS_SUCCEEDS(
      quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL));
}

void StreamHelper::RunOnBackgroundThread() {
  AssertIsOnBackgroundThread();

  nsCOMPtr<nsIFileRandomAccessStream> fileRandomAccessStream;
  mFileRandomAccessStream.swap(fileRandomAccessStream);

  nsCOMPtr<nsIRunnable> callback;
  mCallback.swap(callback);

  callback->Run();
}

void StreamHelper::RunOnIOThread() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mFileRandomAccessStream);

  nsCOMPtr<nsIInputStream> inputStream =
      do_QueryInterface(mFileRandomAccessStream);
  MOZ_ASSERT(inputStream);

  nsresult rv = inputStream->Close();
  Unused << NS_WARN_IF(NS_FAILED(rv));

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));
}

NS_IMETHODIMP
StreamHelper::Run() {
  MOZ_ASSERT(mCallback);

  if (IsOnBackgroundThread()) {
    RunOnBackgroundThread();
  } else {
    RunOnIOThread();
  }

  return NS_OK;
}

/*******************************************************************************
 * Connection
 ******************************************************************************/

Connection::Connection(PersistenceType aPersistenceType,
                       const PrincipalInfo& aPrincipalInfo)
    : mPrincipalInfo(aPrincipalInfo),
      mPersistenceType(aPersistenceType),
      mRunningRequest(false),
      mOpen(false),
      mAllowedToClose(false),
      mActorDestroyed(false) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
}

Connection::~Connection() {
  MOZ_ASSERT(!mRunningRequest);
  MOZ_ASSERT(!mOpen);
  MOZ_ASSERT(mActorDestroyed);
}

void Connection::OnNewRequest() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mRunningRequest);

  mRunningRequest = true;
}

void Connection::OnRequestFinished() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mRunningRequest);

  mRunningRequest = false;

  MaybeCloseStream();
}

void Connection::OnOpen(
    const nsACString& aOrigin, const nsAString& aName,
    ClientDirectoryLockHandle aDirectoryLockHandle,
    already_AddRefed<nsIFileRandomAccessStream> aFileRandomAccessStream) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!aOrigin.IsEmpty());
  MOZ_ASSERT(!aName.IsEmpty());
  MOZ_ASSERT(mOrigin.IsEmpty());
  MOZ_ASSERT(mName.IsEmpty());
  MOZ_ASSERT(!mDirectoryLockHandle);
  MOZ_ASSERT(!mFileRandomAccessStream);
  MOZ_ASSERT(!mOpen);

  mOrigin = aOrigin;
  mName = aName;
  mDirectoryLockHandle = std::move(aDirectoryLockHandle);
  mFileRandomAccessStream = aFileRandomAccessStream;
  mOpen = true;

  if (!gOpenConnections) {
    gOpenConnections = new ConnectionArray();
  }

  gOpenConnections->AppendElement(WrapNotNullUnchecked(this));

  if (mDirectoryLockHandle->Invalidated()) {
    AllowToClose();
  }
}

void Connection::OnClose() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mOrigin.IsEmpty());
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mFileRandomAccessStream);
  MOZ_ASSERT(mOpen);

  mOrigin.Truncate();
  mName.Truncate();

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  mFileRandomAccessStream = nullptr;
  mOpen = false;

  MOZ_ASSERT(gOpenConnections);
  gOpenConnections->RemoveElement(this);

  if (gOpenConnections->IsEmpty()) {
    gOpenConnections = nullptr;
  }

  if (mAllowedToClose && !mActorDestroyed) {
    Unused << SendClosed();
  }
}

void Connection::AllowToClose() {
  AssertIsOnBackgroundThread();

  if (mAllowedToClose) {
    return;
  }

  mAllowedToClose = true;

  if (!mActorDestroyed) {
    Unused << SendAllowToClose();
  }

  MaybeCloseStream();
}

void Connection::MaybeCloseStream() {
  AssertIsOnBackgroundThread();

  if (!mRunningRequest && mOpen && mAllowedToClose) {
    nsCOMPtr<nsIRunnable> callback = NewRunnableMethod(
        "dom::Connection::OnClose", this, &Connection::OnClose);

    RefPtr<StreamHelper> helper =
        new StreamHelper(mFileRandomAccessStream, callback);
    helper->AsyncClose();
  }
}

bool Connection::VerifyRequestParams(const SDBRequestParams& aParams) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != SDBRequestParams::T__None);

  switch (aParams.type()) {
    case SDBRequestParams::TSDBRequestOpenParams: {
      if (NS_WARN_IF(mOpen)) {
        MOZ_CRASH_UNLESS_FUZZING();
        return false;
      }

      break;
    }

    case SDBRequestParams::TSDBRequestSeekParams:
    case SDBRequestParams::TSDBRequestReadParams:
    case SDBRequestParams::TSDBRequestWriteParams:
    case SDBRequestParams::TSDBRequestCloseParams: {
      if (NS_WARN_IF(!mOpen)) {
        MOZ_CRASH_UNLESS_FUZZING();
        return false;
      }

      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  return true;
}

void Connection::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;

  AllowToClose();
}

mozilla::ipc::IPCResult Connection::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  IProtocol* mgr = Manager();
  if (!PBackgroundSDBConnectionParent::Send__delete__(this)) {
    return IPC_FAIL_NO_REASON(mgr);
  }

  return IPC_OK();
}

PBackgroundSDBRequestParent* Connection::AllocPBackgroundSDBRequestParent(
    const SDBRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != SDBRequestParams::T__None);

  if (aParams.type() == SDBRequestParams::TSDBRequestOpenParams &&
      NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  if (mAllowedToClose) {
    return nullptr;
  }

#ifdef DEBUG
  // Always verify parameters in DEBUG builds!
  bool trustParams = false;
#else
  PBackgroundParent* backgroundActor = Manager();
  MOZ_ASSERT(backgroundActor);

  bool trustParams = !BackgroundParent::IsOtherProcessActor(backgroundActor);
#endif

  if (NS_WARN_IF(!trustParams && !VerifyRequestParams(aParams))) {
    MOZ_CRASH_UNLESS_FUZZING();
    return nullptr;
  }

  if (NS_WARN_IF(mRunningRequest)) {
    MOZ_CRASH_UNLESS_FUZZING();
    return nullptr;
  }

  QM_TRY(QuotaManager::EnsureCreated(), nullptr);

  RefPtr<ConnectionOperationBase> actor;

  switch (aParams.type()) {
    case SDBRequestParams::TSDBRequestOpenParams:
      actor = new OpenOp(this, aParams);
      break;

    case SDBRequestParams::TSDBRequestSeekParams:
      actor = new SeekOp(this, aParams);
      break;

    case SDBRequestParams::TSDBRequestReadParams:
      actor = new ReadOp(this, aParams);
      break;

    case SDBRequestParams::TSDBRequestWriteParams:
      actor = new WriteOp(this, aParams);
      break;

    case SDBRequestParams::TSDBRequestCloseParams:
      actor = new CloseOp(this);
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

mozilla::ipc::IPCResult Connection::RecvPBackgroundSDBRequestConstructor(
    PBackgroundSDBRequestParent* aActor, const SDBRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != SDBRequestParams::T__None);
  MOZ_ASSERT_IF(aParams.type() == SDBRequestParams::TSDBRequestOpenParams,
                !QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(!mAllowedToClose);
  MOZ_ASSERT(!mRunningRequest);

  auto* op = static_cast<ConnectionOperationBase*>(aActor);

  if (NS_WARN_IF(!op->Init())) {
    op->Cleanup();
    return IPC_FAIL_NO_REASON(this);
  }

  if (NS_WARN_IF(NS_FAILED(op->Dispatch()))) {
    op->Cleanup();
    return IPC_FAIL_NO_REASON(this);
  }

  return IPC_OK();
}

bool Connection::DeallocPBackgroundSDBRequestParent(
    PBackgroundSDBRequestParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<ConnectionOperationBase> actor =
      dont_AddRef(static_cast<ConnectionOperationBase*>(aActor));
  return true;
}

/*******************************************************************************
 * ConnectionOperationBase
 ******************************************************************************/

ConnectionOperationBase::~ConnectionOperationBase() {
  MOZ_ASSERT(
      !mConnection,
      "ConnectionOperationBase::Cleanup() was not called by a subclass!");
  MOZ_ASSERT(mActorDestroyed);
}

bool ConnectionOperationBase::Init() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mConnection);

  mConnection->OnNewRequest();

  return true;
}

nsresult ConnectionOperationBase::Dispatch() {
  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    return NS_ERROR_ABORT;
  }

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  nsresult rv = quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void ConnectionOperationBase::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mConnection);

  mConnection->OnRequestFinished();

  mConnection = nullptr;
}

void ConnectionOperationBase::SendResults() {
  AssertIsOnOwningThread();

  if (IsActorDestroyed()) {
    MaybeSetFailureCode(NS_ERROR_FAILURE);
  } else {
    SDBRequestResponse response;

    if (NS_SUCCEEDED(mResultCode)) {
      GetResponse(response);

      MOZ_ASSERT(response.type() != SDBRequestResponse::T__None);
      MOZ_ASSERT(response.type() != SDBRequestResponse::Tnsresult);

      OnSuccess();
    } else {
      response = mResultCode;
    }

    Unused << PBackgroundSDBRequestParent::Send__delete__(this, response);
  }

  Cleanup();
}

void ConnectionOperationBase::DatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(NS_SUCCEEDED(mResultCode));

  if (!OperationMayProceed()) {
    // The operation was canceled in some way, likely because the child process
    // has crashed.
    mResultCode = NS_ERROR_ABORT;
  } else {
    nsIFileRandomAccessStream* fileRandomAccessStream =
        mConnection->GetFileRandomAccessStream();
    MOZ_ASSERT(fileRandomAccessStream);

    nsresult rv = DoDatabaseWork(fileRandomAccessStream);
    if (NS_FAILED(rv)) {
      mResultCode = rv;
    }
  }

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));
}

void ConnectionOperationBase::OnSuccess() { AssertIsOnOwningThread(); }

NS_IMETHODIMP
ConnectionOperationBase::Run() {
  if (IsOnBackgroundThread()) {
    SendResults();
  } else {
    DatabaseWork();
  }

  return NS_OK;
}

void ConnectionOperationBase::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  mOperationMayProceed = false;
  mActorDestroyed = true;
}

OpenOp::OpenOp(Connection* aConnection, const SDBRequestParams& aParams)
    : ConnectionOperationBase(aConnection),
      mParams(aParams.get_SDBRequestOpenParams()),
      mState(State::Initial),
      mFileRandomAccessStreamOpen(false) {
  MOZ_ASSERT(aParams.type() == SDBRequestParams::TSDBRequestOpenParams);
}

OpenOp::~OpenOp() {
  MOZ_ASSERT(mDirectoryLockHandle.IsInert());
  MOZ_ASSERT(!mFileRandomAccessStream);
  MOZ_ASSERT(!mFileRandomAccessStreamOpen);
  MOZ_ASSERT_IF(OperationMayProceed(),
                mState == State::Initial || mState == State::Completed);
}

nsresult OpenOp::Dispatch() {
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(this));

  return NS_OK;
}

nsresult OpenOp::Open() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState == State::Initial);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    return NS_ERROR_ABORT;
  }

  if (NS_WARN_IF(!Preferences::GetBool(kPrefSimpleDBEnabled, false))) {
    return NS_ERROR_UNEXPECTED;
  }

  mState = State::FinishOpen;
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

nsresult OpenOp::FinishOpen() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mOriginMetadata.mOrigin.IsEmpty());
  MOZ_ASSERT(!mDirectoryLockHandle);
  MOZ_ASSERT(mState == State::FinishOpen);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    return NS_ERROR_ABORT;
  }

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  const PrincipalInfo& principalInfo = GetConnection()->GetPrincipalInfo();

  PersistenceType persistenceType = GetConnection()->GetPersistenceType();

  if (principalInfo.type() == PrincipalInfo::TSystemPrincipalInfo) {
    mOriginMetadata = {quota::GetInfoForChrome(), persistenceType};
  } else {
    MOZ_ASSERT(principalInfo.type() == PrincipalInfo::TContentPrincipalInfo);

    QM_TRY_UNWRAP(
        auto principalMetadata,
        quota::GetInfoFromValidatedPrincipalInfo(*quotaManager, principalInfo));

    mOriginMetadata = {std::move(principalMetadata), persistenceType};
  }

  if (gOpenConnections) {
    for (const auto& connection : *gOpenConnections) {
      if (connection->Origin() == mOriginMetadata.mOrigin &&
          connection->Name() == mParams.name()) {
        return NS_ERROR_STORAGE_BUSY;
      }
    }
  }

  // Open the directory

  mState = State::DirectoryOpenPending;

  quotaManager
      ->OpenClientDirectory({mOriginMetadata, mozilla::dom::quota::Client::SDB})
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](QuotaManager::ClientDirectoryLockHandlePromise::
                                    ResolveOrRejectValue&& aValue) {
            if (aValue.IsResolve()) {
              self->DirectoryLockAcquired(std::move(aValue.ResolveValue()));
            } else {
              self->DirectoryLockFailed();
            }
          });

  return NS_OK;
}

nsresult OpenOp::SendToIOThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    return NS_ERROR_ABORT;
  }

  mFileRandomAccessStream = new FileRandomAccessStream(
      GetConnection()->GetPersistenceType(), mOriginMetadata,
      mozilla::dom::quota::Client::SDB);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // Must set this before dispatching otherwise we will race with the IO thread.
  mState = State::DatabaseWorkOpen;

  nsresult rv = quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  simpledb::NotifyDatabaseWorkStarted();

  return NS_OK;
}

nsresult OpenOp::DatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);
  MOZ_ASSERT(mFileRandomAccessStream);
  MOZ_ASSERT(!mFileRandomAccessStreamOpen);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    return NS_ERROR_ABORT;
  }

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_INSPECT(
      const auto& dbDirectory,
      ([persistenceType = GetConnection()->GetPersistenceType(), &quotaManager,
        this]() -> mozilla::Result<nsCOMPtr<nsIFile>, nsresult> {
        if (persistenceType == PERSISTENCE_TYPE_PERSISTENT) {
          QM_TRY_RETURN(quotaManager->GetOriginDirectory(mOriginMetadata));
        }

        QM_TRY_RETURN(
            quotaManager->GetOrCreateTemporaryOriginDirectory(mOriginMetadata));
      }()));

  nsresult rv =
      dbDirectory->Append(NS_LITERAL_STRING_FROM_CSTRING(SDB_DIRECTORY_NAME));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  bool exists;
  rv = dbDirectory->Exists(&exists);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!exists) {
    rv = dbDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
#ifdef DEBUG
  else {
    bool isDirectory;
    MOZ_ASSERT(NS_SUCCEEDED(dbDirectory->IsDirectory(&isDirectory)));
    MOZ_ASSERT(isDirectory);
  }
#endif

  nsCOMPtr<nsIFile> dbFile;
  rv = dbDirectory->Clone(getter_AddRefs(dbFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = dbFile->Append(mParams.name() + kSDBSuffix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsString databaseFilePath;
  rv = dbFile->GetPath(databaseFilePath);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = mFileRandomAccessStream->Init(dbFile, PR_RDWR | PR_CREATE_FILE, 0644, 0);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mFileRandomAccessStreamOpen = true;

  rv = DoDatabaseWork(mFileRandomAccessStream);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Must set mState before dispatching otherwise we will race with the owning
  // thread.
  mState = State::SendingResults;

  rv = OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void OpenOp::StreamClosedCallback() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(ResultCode()));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mFileRandomAccessStream);
  MOZ_ASSERT(mFileRandomAccessStreamOpen);

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  mFileRandomAccessStream = nullptr;
  mFileRandomAccessStreamOpen = false;
}

nsresult OpenOp::DoDatabaseWork(
    nsIFileRandomAccessStream* aFileRandomAccessStream) {
  AssertIsOnIOThread();

  SleepIfEnabled(
      StaticPrefs::dom_simpledb_databaseInitialization_pauseOnIOThreadMs());

  return NS_OK;
}

void OpenOp::GetResponse(SDBRequestResponse& aResponse) {
  AssertIsOnOwningThread();

  aResponse = SDBRequestOpenResponse();
}

void OpenOp::OnSuccess() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));
  MOZ_ASSERT(!mOriginMetadata.mOrigin.IsEmpty());
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mFileRandomAccessStream);
  MOZ_ASSERT(mFileRandomAccessStreamOpen);

  ClientDirectoryLockHandle directoryLockHandle;
  nsCOMPtr<nsIFileRandomAccessStream> fileRandomAccessStream;

  directoryLockHandle = std::move(mDirectoryLockHandle);
  mFileRandomAccessStream.swap(fileRandomAccessStream);
  mFileRandomAccessStreamOpen = false;

  GetConnection()->OnOpen(mOriginMetadata.mOrigin, mParams.name(),
                          std::move(directoryLockHandle),
                          fileRandomAccessStream.forget());
}

void OpenOp::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT_IF(mFileRandomAccessStreamOpen, mFileRandomAccessStream);

  if (mFileRandomAccessStream && mFileRandomAccessStreamOpen) {
    // If we have an initialized file stream then the operation must have failed
    // and there must be a directory lock too.
    MOZ_ASSERT(NS_FAILED(ResultCode()));
    MOZ_ASSERT(mDirectoryLockHandle);

    // We must close the stream on the I/O thread before releasing it on this
    // thread. The directory lock can't be released either.
    nsCOMPtr<nsIRunnable> callback =
        NewRunnableMethod("dom::OpenOp::StreamClosedCallback", this,
                          &OpenOp::StreamClosedCallback);

    RefPtr<StreamHelper> helper =
        new StreamHelper(mFileRandomAccessStream, callback);
    helper->AsyncClose();
  } else {
    {
      auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
    }

    mFileRandomAccessStream = nullptr;
    MOZ_ASSERT(!mFileRandomAccessStreamOpen);
  }

  ConnectionOperationBase::Cleanup();
}

NS_IMETHODIMP
OpenOp::Run() {
  nsresult rv;

  switch (mState) {
    case State::Initial:
      rv = Open();
      break;

    case State::FinishOpen:
      rv = FinishOpen();
      break;

    case State::DatabaseWorkOpen:
      rv = DatabaseWork();
      break;

    case State::SendingResults:
      SendResults();
      return NS_OK;

    default:
      MOZ_CRASH("Bad state!");
  }

  if (NS_WARN_IF(NS_FAILED(rv)) && mState != State::SendingResults) {
    MaybeSetFailureCode(rv);

    // Must set mState before dispatching otherwise we will race with the owning
    // thread.
    mState = State::SendingResults;

    if (IsOnOwningThread()) {
      SendResults();
    } else {
      MOZ_ALWAYS_SUCCEEDS(
          OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));
    }
  }

  return NS_OK;
}

void OpenOp::DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  mDirectoryLockHandle = std::move(aLockHandle);

  auto cleanupAndReturn = [self = RefPtr(this)](const nsresult rv) {
    self->MaybeSetFailureCode(rv);

    // The caller holds a strong reference to us, no need for a self reference
    // before calling Run().

    self->mState = State::SendingResults;
    MOZ_ALWAYS_SUCCEEDS(self->Run());
  };

  if (mDirectoryLockHandle->Invalidated()) {
    return cleanupAndReturn(NS_ERROR_ABORT);
  }

  QM_TRY(MOZ_TO_RESULT(SendToIOThread()), cleanupAndReturn);
}

void OpenOp::DirectoryLockFailed() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  MaybeSetFailureCode(NS_ERROR_FAILURE);

  // The caller holds a strong reference to us, no need for a self reference
  // before calling Run().

  mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(Run());
}

SeekOp::SeekOp(Connection* aConnection, const SDBRequestParams& aParams)
    : ConnectionOperationBase(aConnection),
      mParams(aParams.get_SDBRequestSeekParams()) {
  MOZ_ASSERT(aParams.type() == SDBRequestParams::TSDBRequestSeekParams);
}

nsresult SeekOp::DoDatabaseWork(
    nsIFileRandomAccessStream* aFileRandomAccessStream) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFileRandomAccessStream);

  nsresult rv = aFileRandomAccessStream->Seek(nsISeekableStream::NS_SEEK_SET,
                                              mParams.offset());

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void SeekOp::GetResponse(SDBRequestResponse& aResponse) {
  aResponse = SDBRequestSeekResponse();
}

ReadOp::ReadOp(Connection* aConnection, const SDBRequestParams& aParams)
    : ConnectionOperationBase(aConnection),
      mParams(aParams.get_SDBRequestReadParams()) {
  MOZ_ASSERT(aParams.type() == SDBRequestParams::TSDBRequestReadParams);
}

bool ReadOp::Init() {
  AssertIsOnOwningThread();

  if (NS_WARN_IF(!ConnectionOperationBase::Init())) {
    return false;
  }

  if (NS_WARN_IF(mParams.size() > std::numeric_limits<std::size_t>::max())) {
    return false;
  }

  mOutputStream = FixedBufferOutputStream::Create(mParams.size(), fallible);
  if (NS_WARN_IF(!mOutputStream)) {
    return false;
  }

  return true;
}

nsresult ReadOp::DoDatabaseWork(
    nsIFileRandomAccessStream* aFileRandomAccessStream) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFileRandomAccessStream);

  nsCOMPtr<nsIInputStream> inputStream =
      do_QueryInterface(aFileRandomAccessStream);
  MOZ_ASSERT(inputStream);

  nsresult rv;

  uint64_t offset = 0;

  do {
    char copyBuffer[kCopyBufferSize];

    uint64_t max = mParams.size() - offset;
    if (max == 0) {
      break;
    }

    uint32_t count = sizeof(copyBuffer);
    if (count > max) {
      count = max;
    }

    uint32_t numRead;
    rv = inputStream->Read(copyBuffer, count, &numRead);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (!numRead) {
      break;
    }

    uint32_t numWrite;
    rv = mOutputStream->Write(copyBuffer, numRead, &numWrite);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (NS_WARN_IF(numWrite != numRead)) {
      return NS_ERROR_FAILURE;
    }

    offset += numWrite;
  } while (true);

  MOZ_ASSERT(offset == mParams.size());

  MOZ_ALWAYS_SUCCEEDS(mOutputStream->Close());

  return NS_OK;
}

void ReadOp::GetResponse(SDBRequestResponse& aResponse) {
  aResponse = SDBRequestReadResponse(nsCString(mOutputStream->WrittenData()));
}

WriteOp::WriteOp(Connection* aConnection, const SDBRequestParams& aParams)
    : ConnectionOperationBase(aConnection),
      mParams(aParams.get_SDBRequestWriteParams()),
      mSize(0) {
  MOZ_ASSERT(aParams.type() == SDBRequestParams::TSDBRequestWriteParams);
}

bool WriteOp::Init() {
  AssertIsOnOwningThread();

  if (NS_WARN_IF(!ConnectionOperationBase::Init())) {
    return false;
  }

  const nsCString& string = mParams.data();

  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream), string);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  mInputStream = std::move(inputStream);
  mSize = string.Length();

  return true;
}

nsresult WriteOp::DoDatabaseWork(
    nsIFileRandomAccessStream* aFileRandomAccessStream) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFileRandomAccessStream);

  nsCOMPtr<nsIOutputStream> outputStream =
      do_QueryInterface(aFileRandomAccessStream);
  MOZ_ASSERT(outputStream);

  nsresult rv;

  do {
    char copyBuffer[kCopyBufferSize];

    uint32_t numRead;
    rv = mInputStream->Read(copyBuffer, sizeof(copyBuffer), &numRead);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    if (!numRead) {
      break;
    }

    uint32_t numWrite;
    rv = outputStream->Write(copyBuffer, numRead, &numWrite);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (NS_WARN_IF(numWrite != numRead)) {
      return NS_ERROR_FAILURE;
    }
  } while (true);

  MOZ_ALWAYS_SUCCEEDS(mInputStream->Close());

  return NS_OK;
}

void WriteOp::GetResponse(SDBRequestResponse& aResponse) {
  aResponse = SDBRequestWriteResponse();
}

CloseOp::CloseOp(Connection* aConnection)
    : ConnectionOperationBase(aConnection) {}

nsresult CloseOp::DoDatabaseWork(
    nsIFileRandomAccessStream* aFileRandomAccessStream) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFileRandomAccessStream);

  nsCOMPtr<nsIInputStream> inputStream =
      do_QueryInterface(aFileRandomAccessStream);
  MOZ_ASSERT(inputStream);

  nsresult rv = inputStream->Close();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void CloseOp::GetResponse(SDBRequestResponse& aResponse) {
  aResponse = SDBRequestCloseResponse();
}

void CloseOp::OnSuccess() {
  AssertIsOnOwningThread();

  GetConnection()->OnClose();
}

/*******************************************************************************
 * QuotaClient
 ******************************************************************************/

QuotaClient* QuotaClient::sInstance = nullptr;

QuotaClient::QuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!sInstance, "We expect this to be a singleton!");

  sInstance = this;
}

QuotaClient::~QuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(sInstance == this, "We expect this to be a singleton!");

  sInstance = nullptr;
}

mozilla::dom::quota::Client::Type QuotaClient::GetType() {
  return QuotaClient::SDB;
}

Result<UsageInfo, nsresult> QuotaClient::InitOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  return GetUsageForOrigin(aPersistenceType, aOriginMetadata, aCanceled);
}

nsresult QuotaClient::InitOriginWithoutTracking(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  return NS_OK;
}

Result<UsageInfo, nsresult> QuotaClient::GetUsageForOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == aPersistenceType);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_UNWRAP(auto directory,
                quotaManager->GetOriginDirectory(aOriginMetadata));

  MOZ_ASSERT(directory);

  nsresult rv =
      directory->Append(NS_LITERAL_STRING_FROM_CSTRING(SDB_DIRECTORY_NAME));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }

  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(directory->Exists(&exists)) && exists);

  QM_TRY_RETURN(ReduceEachFileAtomicCancelable(
      *directory, aCanceled, UsageInfo{},
      [](UsageInfo usageInfo,
         const nsCOMPtr<nsIFile>& file) -> Result<UsageInfo, nsresult> {
        QM_TRY_INSPECT(const bool& isDirectory,
                       MOZ_TO_RESULT_INVOKE_MEMBER(file, IsDirectory));

        if (isDirectory) {
          Unused << WARN_IF_FILE_IS_UNKNOWN(*file);
          return usageInfo;
        }

        nsString leafName;
        QM_TRY(MOZ_TO_RESULT(file->GetLeafName(leafName)));

        if (StringEndsWith(leafName, kSDBSuffix)) {
          QM_TRY_INSPECT(const int64_t& fileSize,
                         MOZ_TO_RESULT_INVOKE_MEMBER(file, GetFileSize));

          MOZ_ASSERT(fileSize >= 0);

          return usageInfo +
                 UsageInfo{DatabaseUsageType(Some(uint64_t(fileSize)))};
        }

        Unused << WARN_IF_FILE_IS_UNKNOWN(*file);

        return usageInfo;
      }));
}

void QuotaClient::OnOriginClearCompleted(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
}

void QuotaClient::OnRepositoryClearCompleted(PersistenceType aPersistenceType) {
  AssertIsOnIOThread();
}

void QuotaClient::ReleaseIOThreadObjects() { AssertIsOnIOThread(); }

void QuotaClient::AbortOperationsForLocks(
    const DirectoryLockIdTable& aDirectoryLockIds) {
  AssertIsOnBackgroundThread();

  AllowToCloseConnectionsMatching([&aDirectoryLockIds](const auto& connection) {
    // If the connections is registered in gOpenConnections then it must have
    // a directory lock.
    return IsLockForObjectContainedInLockTable(connection, aDirectoryLockIds);
  });
}

void QuotaClient::AbortOperationsForProcess(ContentParentId aContentParentId) {
  AssertIsOnBackgroundThread();
}

void QuotaClient::AbortAllOperations() {
  AssertIsOnBackgroundThread();

  AllowToCloseConnectionsMatching([](const auto&) { return true; });
}

void QuotaClient::StartIdleMaintenance() { AssertIsOnBackgroundThread(); }

void QuotaClient::StopIdleMaintenance() { AssertIsOnBackgroundThread(); }

void QuotaClient::InitiateShutdown() {
  AssertIsOnBackgroundThread();

  if (gOpenConnections) {
    for (const auto& connection : *gOpenConnections) {
      connection->AllowToClose();
    }
  }
}

bool QuotaClient::IsShutdownCompleted() const { return !gOpenConnections; }

void QuotaClient::ForceKillActors() {
  // Currently we don't implement killing actors (are there any to kill here?).
}

nsCString QuotaClient::GetShutdownStatus() const {
  // XXX Gather information here.
  return "To be implemented"_ns;
}

void QuotaClient::FinalizeShutdown() {
  // Nothing to do here.
}

}  // namespace mozilla::dom
