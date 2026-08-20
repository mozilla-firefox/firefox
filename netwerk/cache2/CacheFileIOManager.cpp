/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheFileIOManager.h"

#include <limits>

#include "CacheFile.h"
#include "CacheFileContextEvictor.h"
#include "CacheFileUtils.h"
#include "CacheHashUtils.h"
#include "CacheIndex.h"
#include "CacheLog.h"
#include "CacheObserver.h"
#include "CacheStorageService.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/FileUtils.h"
#include "mozilla/IOUtils.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/glean/NetwerkCache2Metrics.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/MozURL.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsEffectiveTLDService.h"
#include "nsError.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsITimer.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "private/pprio.h"

#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasksRunner.h"
#  include "nsIBackgroundTasks.h"
#endif

// include files for ftruncate (or equivalent)
#if defined(XP_UNIX)
#  include <unistd.h>
#elif defined(XP_WIN)
#  include <windows.h>
#  undef CreateFile
#  undef CREATE_NEW
#else
// XXX add necessary include file for ftruncate (or equivalent)
#endif

namespace mozilla::net {

#define kOpenHandlesLimit 128
#define kMetadataWriteDelay 5000
#define kRemoveTrashStartDelay 60000    // in milliseconds
#define kSmartSizeUpdateInterval 60000  // in milliseconds

#ifdef ANDROID
const uint32_t kMaxCacheSizeKB = 512 * 1024;  // 512 MB
#else
const uint32_t kMaxCacheSizeKB = 1024 * 1024;  // 1 GB
#endif
const uint32_t kMaxClearOnShutdownCacheSizeKB = 150 * 1024;  // 150 MB
const auto kPurgeExtension = ".purge.bg_rm"_ns;

bool CacheFileHandle::DispatchRelease() {
  if (CacheFileIOManager::IsOnIOThreadOrCeased()) {
    return false;
  }

  nsCOMPtr<nsIEventTarget> ioTarget = CacheFileIOManager::IOTarget();
  if (!ioTarget) {
    return false;
  }

  nsresult rv = ioTarget->Dispatch(
      NewNonOwningRunnableMethod("net::CacheFileHandle::Release", this,
                                 &CacheFileHandle::Release),
      nsIEventTarget::DISPATCH_NORMAL);
  return NS_SUCCEEDED(rv);
}

#if defined(MOZ_CACHE_ASYNC_IO)
void CacheFileHandle::StartAsyncOperation() {
  mAsyncRunning++;
  CacheFileIOManager::gInstance->mAsyncRunning++;
}

bool CacheFileHandle::WaitForAsyncCompletion(nsIRunnable* aEvent,
                                             uint32_t aLevel) {
  if (mAsyncRunning) {
    mPendingEvents.AppendElement(PendingItem(aEvent, aLevel));
    return true;
  }
  return false;
}

#  define GUARD_ASYNC_WRITE()                                           \
    if (mHandle->WaitForAsyncCompletion(                                \
            this, mHandle->IsPriority() ? CacheIOThread::WRITE_PRIORITY \
                                        : CacheIOThread::WRITE)) {      \
      return NS_OK;                                                     \
    }

class PendingItemComparator {
 public:
  using PendingItem = CacheFileHandle::PendingItem;
  bool LessThan(const PendingItem& a, const PendingItem& b) const {
    return a.second < b.second;
  }
};

void CacheFileHandle::EndAsyncOperation() {
  MOZ_ASSERT(mAsyncRunning, "EndAsyncOperation called without async operation");
  MOZ_ASSERT(CacheFileIOManager::gInstance->mIOThread->IsCurrentThread());

  if (--mAsyncRunning == 0) {
    // Notify all pending events
    auto pendingEvents = std::move(mPendingEvents);
    pendingEvents.StableSort(PendingItemComparator());
    for (auto& event : pendingEvents) {
      event.first->Run();
    }
  }
  if (--CacheFileIOManager::gInstance->mAsyncRunning == 0) {
    CacheFileIOManager::gInstance->DispatchPendingEvents();
  }
}
#else
#  define GUARD_ASYNC_WRITE()
#endif

NS_IMPL_ADDREF(CacheFileHandle)
NS_IMETHODIMP_(MozExternalRefCountType)
CacheFileHandle::Release() {
  nsrefcnt count = mRefCnt - 1;
  if (DispatchRelease()) {
    // Redispatched to the IO thread.
    return count;
  }

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  LOG(("CacheFileHandle::Release() [this=%p, refcnt=%" PRIuPTR "]", this,
       mRefCnt.get()));
  MOZ_ASSERT(0 != mRefCnt, "dup release");
  count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "CacheFileHandle");

  if (0 == count) {
    mRefCnt = 1;
    delete (this);
    return 0;
  }

  return count;
}

NS_INTERFACE_MAP_BEGIN(CacheFileHandle)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

CacheFileHandle::CacheFileHandle(const SHA1Sum::Hash* aHash, bool aPriority,
                                 PinningStatus aPinning)
    : mHash(aHash),
      mIsDoomed(false),
      mClosed(false),
      mPriority(aPriority),
      mSpecialFile(false),
      mInvalid(false),
      mFileExists(false),
      mDoomWhenFoundPinned(false),
      mDoomWhenFoundNonPinned(false),
      mKilled(false),
      mPinning(aPinning),
      mFileSize(-1),
      mFD(nullptr) {
  // If we initialize mDoomed in the initialization list, that initialization is
  // not guaranteeded to be atomic.  Whereas this assignment here is guaranteed
  // to be atomic.  TSan will see this (atomic) assignment and be satisfied
  // that cross-thread accesses to mIsDoomed are properly synchronized.
  mIsDoomed = false;
  LOG((
      "CacheFileHandle::CacheFileHandle() [this=%p, hash=%08x%08x%08x%08x%08x]",
      this, LOGSHA1(aHash)));
}

CacheFileHandle::CacheFileHandle(const nsACString& aKey, bool aPriority,
                                 PinningStatus aPinning)
    : mHash(nullptr),
      mIsDoomed(false),
      mClosed(false),
      mPriority(aPriority),
      mSpecialFile(true),
      mInvalid(false),
      mFileExists(false),
      mDoomWhenFoundPinned(false),
      mDoomWhenFoundNonPinned(false),
      mKilled(false),
      mPinning(aPinning),
      mFileSize(-1),
      mFD(nullptr),
      mKey(aKey) {
  // See comment above about the initialization of mIsDoomed.
  mIsDoomed = false;
  LOG(("CacheFileHandle::CacheFileHandle() [this=%p, key=%s]", this,
       PromiseFlatCString(aKey).get()));
}

CacheFileHandle::~CacheFileHandle() {
  LOG(("CacheFileHandle::~CacheFileHandle() [this=%p]", this));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  RefPtr<CacheFileIOManager> ioMan = CacheFileIOManager::gInstance;
  if (!IsClosed() && ioMan) {
    ioMan->CloseHandleInternal(this);
  }
}

void CacheFileHandle::Log() {
  nsAutoCString leafName;
  if (mFile) {
    mFile->GetNativeLeafName(leafName);
  }

  if (mSpecialFile) {
    LOG(
        ("CacheFileHandle::Log() - special file [this=%p, "
         "isDoomed=%d, priority=%d, closed=%d, invalid=%d, "
         "pinning=%" PRIu32 ", fileExists=%d, fileSize=%" PRId64
         ", leafName=%s, key=%s]",
         this, bool(mIsDoomed), bool(mPriority), bool(mClosed), bool(mInvalid),
         static_cast<uint32_t>(mPinning), bool(mFileExists), int64_t(mFileSize),
         leafName.get(), mKey.get()));
  } else {
    LOG(
        ("CacheFileHandle::Log() - entry file [this=%p, "
         "hash=%08x%08x%08x%08x%08x, "
         "isDoomed=%d, priority=%d, closed=%d, invalid=%d, "
         "pinning=%" PRIu32 ", fileExists=%d, fileSize=%" PRId64
         ", leafName=%s, key=%s]",
         this, LOGSHA1(mHash), bool(mIsDoomed), bool(mPriority), bool(mClosed),
         bool(mInvalid), static_cast<uint32_t>(mPinning), bool(mFileExists),
         int64_t(mFileSize), leafName.get(), mKey.get()));
  }
}

uint32_t CacheFileHandle::FileSizeInK() const {
  MOZ_ASSERT(mFileSize != -1);
  uint64_t size64 = mFileSize;

  size64 += 0x3FF;
  size64 >>= 10;

  uint32_t size;
  if (size64 >> 32) {
    NS_WARNING(
        "CacheFileHandle::FileSizeInK() - FileSize is too large, "
        "truncating to PR_UINT32_MAX");
    size = PR_UINT32_MAX;
  } else {
    size = static_cast<uint32_t>(size64);
  }

  return size;
}

bool CacheFileHandle::SetPinned(bool aPinned) {
  LOG(("CacheFileHandle::SetPinned [this=%p, pinned=%d]", this, aPinned));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  mPinning = aPinned ? PinningStatus::PINNED : PinningStatus::NON_PINNED;

  if ((MOZ_UNLIKELY(mDoomWhenFoundPinned) && aPinned) ||
      (MOZ_UNLIKELY(mDoomWhenFoundNonPinned) && !aPinned)) {
    LOG(("  dooming, when: pinned=%d, non-pinned=%d, found: pinned=%d",
         bool(mDoomWhenFoundPinned), bool(mDoomWhenFoundNonPinned), aPinned));

    mDoomWhenFoundPinned = false;
    mDoomWhenFoundNonPinned = false;

    return false;
  }

  return true;
}

// Memory reporting

size_t CacheFileHandle::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t n = 0;
  n += mallocSizeOf(mFD);
  n += mKey.SizeOfExcludingThisIfUnshared(mallocSizeOf);
  return n;
}

size_t CacheFileHandle::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  return mallocSizeOf(this) + SizeOfExcludingThis(mallocSizeOf);
}

/******************************************************************************
 *  CacheFileHandles::HandleHashKey
 *****************************************************************************/

void CacheFileHandles::HandleHashKey::AddHandle(CacheFileHandle* aHandle) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  mHandles.InsertElementAt(0, aHandle);
}

void CacheFileHandles::HandleHashKey::RemoveHandle(CacheFileHandle* aHandle) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  DebugOnly<bool> found{};
  found = mHandles.RemoveElement(aHandle);
  MOZ_ASSERT(found);
}

already_AddRefed<CacheFileHandle>
CacheFileHandles::HandleHashKey::GetNewestHandle() {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  RefPtr<CacheFileHandle> handle;
  if (mHandles.Length()) {
    handle = mHandles[0];
  }

  return handle.forget();
}

void CacheFileHandles::HandleHashKey::GetHandles(
    nsTArray<RefPtr<CacheFileHandle>>& aResult) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  for (uint32_t i = 0; i < mHandles.Length(); ++i) {
    CacheFileHandle* handle = mHandles[i];
    aResult.AppendElement(handle);
  }
}

#ifdef DEBUG

void CacheFileHandles::HandleHashKey::AssertHandlesState() {
  for (uint32_t i = 0; i < mHandles.Length(); ++i) {
    CacheFileHandle* handle = mHandles[i];
    MOZ_ASSERT(handle->IsDoomed());
  }
}

#endif

size_t CacheFileHandles::HandleHashKey::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

  size_t n = 0;
  n += mallocSizeOf(mHash.get());
  for (uint32_t i = 0; i < mHandles.Length(); ++i) {
    n += mHandles[i]->SizeOfIncludingThis(mallocSizeOf);
  }

  return n;
}

/******************************************************************************
 *  CacheFileHandles
 *****************************************************************************/

CacheFileHandles::CacheFileHandles() {
  LOG(("CacheFileHandles::CacheFileHandles() [this=%p]", this));
  MOZ_COUNT_CTOR(CacheFileHandles);
}

CacheFileHandles::~CacheFileHandles() {
  LOG(("CacheFileHandles::~CacheFileHandles() [this=%p]", this));
  MOZ_COUNT_DTOR(CacheFileHandles);
}

nsresult CacheFileHandles::GetHandle(const SHA1Sum::Hash* aHash,
                                     CacheFileHandle** _retval) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  MOZ_ASSERT(aHash);

#ifdef DEBUG_HANDLES
  LOG(("CacheFileHandles::GetHandle() [hash=%08x%08x%08x%08x%08x]",
       LOGSHA1(aHash)));
#endif

  // find hash entry for key
  HandleHashKey* entry = mTable.GetEntry(*aHash);
  if (!entry) {
    LOG(
        ("CacheFileHandles::GetHandle() hash=%08x%08x%08x%08x%08x "
         "no handle entries found",
         LOGSHA1(aHash)));
    return NS_ERROR_NOT_AVAILABLE;
  }

#ifdef DEBUG_HANDLES
  Log(entry);
#endif

  // Check if the entry is doomed
  RefPtr<CacheFileHandle> handle = entry->GetNewestHandle();
  if (!handle) {
    LOG(
        ("CacheFileHandles::GetHandle() hash=%08x%08x%08x%08x%08x "
         "no handle found %p, entry %p",
         LOGSHA1(aHash), handle.get(), entry));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (handle->IsDoomed()) {
    LOG(
        ("CacheFileHandles::GetHandle() hash=%08x%08x%08x%08x%08x "
         "found doomed handle %p, entry %p",
         LOGSHA1(aHash), handle.get(), entry));
    return NS_ERROR_NOT_AVAILABLE;
  }

  LOG(
      ("CacheFileHandles::GetHandle() hash=%08x%08x%08x%08x%08x "
       "found handle %p, entry %p",
       LOGSHA1(aHash), handle.get(), entry));

  handle.forget(_retval);
  return NS_OK;
}

already_AddRefed<CacheFileHandle> CacheFileHandles::NewHandle(
    const SHA1Sum::Hash* aHash, bool aPriority,
    CacheFileHandle::PinningStatus aPinning) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  MOZ_ASSERT(aHash);

#ifdef DEBUG_HANDLES
  LOG(("CacheFileHandles::NewHandle() [hash=%08x%08x%08x%08x%08x]",
       LOGSHA1(aHash)));
#endif

  // find hash entry for key
  HandleHashKey* entry = mTable.PutEntry(*aHash);

#ifdef DEBUG_HANDLES
  Log(entry);
#endif

#ifdef DEBUG
  entry->AssertHandlesState();
#endif

  RefPtr<CacheFileHandle> handle =
      new CacheFileHandle(entry->Hash(), aPriority, aPinning);
  entry->AddHandle(handle);

  LOG(
      ("CacheFileHandles::NewHandle() hash=%08x%08x%08x%08x%08x "
       "created new handle %p, entry=%p",
       LOGSHA1(aHash), handle.get(), entry));
  return handle.forget();
}

void CacheFileHandles::RemoveHandle(CacheFileHandle* aHandle) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  MOZ_ASSERT(aHandle);

  if (!aHandle) {
    return;
  }

#ifdef DEBUG_HANDLES
  LOG((
      "CacheFileHandles::RemoveHandle() [handle=%p, hash=%08x%08x%08x%08x%08x]",
      aHandle, LOGSHA1(aHandle->Hash())));
#endif

  // find hash entry for key
  HandleHashKey* entry = mTable.GetEntry(*aHandle->Hash());
  if (!entry) {
    MOZ_ASSERT(CacheFileIOManager::IsShutdown(),
               "Should find entry when removing a handle before shutdown");

    LOG(
        ("CacheFileHandles::RemoveHandle() hash=%08x%08x%08x%08x%08x "
         "no entries found",
         LOGSHA1(aHandle->Hash())));
    return;
  }

#ifdef DEBUG_HANDLES
  Log(entry);
#endif

  LOG(
      ("CacheFileHandles::RemoveHandle() hash=%08x%08x%08x%08x%08x "
       "removing handle %p",
       LOGSHA1(entry->Hash()), aHandle));
  entry->RemoveHandle(aHandle);

  if (entry->IsEmpty()) {
    LOG(
        ("CacheFileHandles::RemoveHandle() hash=%08x%08x%08x%08x%08x "
         "list is empty, removing entry %p",
         LOGSHA1(entry->Hash()), entry));
    mTable.RemoveEntry(entry);
  }
}

void CacheFileHandles::GetAllHandles(
    nsTArray<RefPtr<CacheFileHandle>>* _retval) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
    iter.Get()->GetHandles(*_retval);
  }
}

void CacheFileHandles::GetActiveHandles(
    nsTArray<RefPtr<CacheFileHandle>>* _retval) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<CacheFileHandle> handle = iter.Get()->GetNewestHandle();
    MOZ_ASSERT(handle);

    if (!handle->IsDoomed()) {
      _retval->AppendElement(handle);
    }
  }
}

void CacheFileHandles::ClearAll() {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  mTable.Clear();
}

uint32_t CacheFileHandles::HandleCount() { return mTable.Count(); }

#ifdef DEBUG_HANDLES
void CacheFileHandles::Log(CacheFileHandlesEntry* entry) {
  LOG(("CacheFileHandles::Log() BEGIN [entry=%p]", entry));

  nsTArray<RefPtr<CacheFileHandle>> array;
  aEntry->GetHandles(array);

  for (uint32_t i = 0; i < array.Length(); ++i) {
    CacheFileHandle* handle = array[i];
    handle->Log();
  }

  LOG(("CacheFileHandles::Log() END [entry=%p]", entry));
}
#endif

// Memory reporting

size_t CacheFileHandles::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

  return mTable.SizeOfExcludingThis(mallocSizeOf);
}

// Events

class ShutdownEvent : public Runnable, nsITimerCallback {
  NS_DECL_ISUPPORTS_INHERITED
 public:
  ShutdownEvent() : Runnable("net::ShutdownEvent") {}

 protected:
  ~ShutdownEvent() = default;

 public:
  NS_IMETHOD Run() override {
#if defined(MOZ_CACHE_ASYNC_IO)
    if (CacheFileIOManager::gInstance->mAsyncRunning > 0) {
      // If there are any async operations running, we will wait for them to
      // finish before shutting down the IO thread.
      LOG(("CacheFileIOManager::ShutdownEvent - waiting for async operations"));
      CacheFileIOManager::gInstance->mPendingEvents.AppendElement(
          CacheFileIOManager::PendingItem(this, CacheIOThread::WRITE));
      return NS_OK;
    }
#endif
    CacheFileIOManager::gInstance->ShutdownInternal();

    mNotified = true;

    NS_DispatchToMainThread(
        NS_NewRunnableFunction("CacheFileIOManager::ShutdownEvent::Run", []() {
          // This empty runnable is dispatched just in case the MT event loop
          // becomes empty - we need to process a task to break out of
          // SpinEventLoopUntil.
        }));

    return NS_OK;
  }

  NS_IMETHOD Notify(nsITimer* timer) override {
    if (mNotified) {
      return NS_OK;
    }

    // If there is any IO blocking on the IO thread, this will
    // try to cancel it.
    CacheFileIOManager::gInstance->mIOThread->CancelBlockingIO();

    // After this runs the first time, the browser_cache_max_shutdown_io_lag
    // time has elapsed. The CacheIO thread may pick up more blocking IO tasks
    // so we want to block those too if necessary.
    mTimer->SetDelay(
        StaticPrefs::browser_cache_shutdown_io_time_between_cancellations_ms());
    return NS_OK;
  }

  void PostAndWait() {
    nsresult rv = CacheFileIOManager::gInstance->mIOThread->Dispatch(
        this,
        CacheIOThread::WRITE);  // When writes and closing of handles is done
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // If we failed to post the even there's no reason to go into the loop
    // because mNotified will never be set to true.
    if (NS_FAILED(rv)) {
      NS_WARNING("Posting ShutdownEvent task failed");
      return;
    }

    rv = NS_NewTimerWithCallback(
        getter_AddRefs(mTimer), this,
        StaticPrefs::browser_cache_max_shutdown_io_lag() * 1000,
        nsITimer::TYPE_REPEATING_SLACK);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    mozilla::SpinEventLoopUntil("CacheFileIOManager::ShutdownEvent"_ns,
                                [&]() { return bool(mNotified); });

    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }
  }

 protected:
  Atomic<bool> mNotified{false};
  nsCOMPtr<nsITimer> mTimer;
};

NS_IMPL_ISUPPORTS_INHERITED(ShutdownEvent, Runnable, nsITimerCallback)

class OpenFileEvent : public Runnable {
 public:
  OpenFileEvent(const nsACString& aKey, uint32_t aFlags,
                CacheFileIOListener* aCallback)
      : Runnable("net::OpenFileEvent"),
        mFlags(aFlags),
        mCallback(aCallback),
        mKey(aKey) {
    mIOMan = CacheFileIOManager::gInstance;
  }

 protected:
  ~OpenFileEvent() = default;

 public:
  NS_IMETHOD Run() override {
    nsresult rv = NS_OK;

    if (!(mFlags & CacheFileIOManager::SPECIAL_FILE)) {
      SHA1Sum sum;
      sum.update(mKey.BeginReading(), mKey.Length());
      sum.finish(mHash);
    }

    if (!mIOMan) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      if (mFlags & CacheFileIOManager::SPECIAL_FILE) {
        rv = mIOMan->OpenSpecialFileInternal(mKey, mFlags,
                                             getter_AddRefs(mHandle));
      } else {
        rv = mIOMan->OpenFileInternal(&mHash, mKey, mFlags,
                                      getter_AddRefs(mHandle));
      }
      mIOMan = nullptr;
      if (mHandle) {
        if (mHandle->Key().IsEmpty()) {
          mHandle->Key() = mKey;
        }
      }
    }

    mCallback->OnFileOpened(mHandle, rv);
    return NS_OK;
  }

 protected:
  SHA1Sum::Hash mHash{};
  uint32_t mFlags;
  nsCOMPtr<CacheFileIOListener> mCallback;
  RefPtr<CacheFileIOManager> mIOMan;
  RefPtr<CacheFileHandle> mHandle;
  nsCString mKey;
};

class ReadEvent : public Runnable {
 public:
  ReadEvent(CacheFileHandle* aHandle, int64_t aOffset, char* aBuf,
            int32_t aCount, CacheFileIOListener* aCallback)
      : Runnable("net::ReadEvent"),
        mHandle(aHandle),
        mOffset(aOffset),
        mBuf(aBuf),
        mCount(aCount),
        mCallback(aCallback) {}

 protected:
  ~ReadEvent() = default;

 public:
  nsresult InlineRun(bool aAsyncDataRead) {
    nsresult rv;

    MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

    if (mHandle->IsClosed() || (mCallback && mCallback->IsKilled())) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = CacheFileIOManager::gInstance->ReadInternal(mHandle, mOffset, mBuf,
                                                       mCount, this);
#if defined(MOZ_CACHE_ASYNC_IO)
      if (NS_SUCCEEDED(rv)) {
        /* The request has been performed asynchronously. It should
         * complete later.
         */
        return NS_OK;
      }
#endif
    }

#if defined(MOZ_CACHE_ASYNC_IO)
    if (aAsyncDataRead) {
      nsCOMPtr<nsIEventTarget> ioTarget = CacheFileIOManager::IOTarget();
      ioTarget->Dispatch(NS_NewRunnableFunction(
          "net::ReadEvent::Callback", [self = RefPtr(this), rv]() {
            // Prevent calling back twice
            nsCOMPtr<CacheFileIOListener> cb = std::move(self->mCallback);
            cb->OnDataRead(self->mHandle, self->mBuf, rv);
          }));
      return NS_OK;
    }
#endif

    // Prevent calling back twice
    nsCOMPtr<CacheFileIOListener> cb = std::move(mCallback);
    cb->OnDataRead(mHandle, mBuf, rv);
    return NS_OK;
  }

  NS_IMETHOD Run() override { return InlineRun(false); }

#if defined(MOZ_CACHE_ASYNC_IO)
  nsresult OnComplete(nsresult aStatus) {
    // Prevent calling back twice
    nsCOMPtr<CacheFileIOListener> cb = std::move(mCallback);
    cb->OnDataRead(mHandle, mBuf, aStatus);
    mHandle->EndAsyncOperation();
    return NS_OK;
  }
#endif

 protected:
  RefPtr<CacheFileHandle> mHandle;
  int64_t mOffset;
  char* mBuf;
  int32_t mCount;
  nsCOMPtr<CacheFileIOListener> mCallback;
};

class WriteEvent : public Runnable {
 public:
  WriteEvent(CacheFileHandle* aHandle, int64_t aOffset, const char* aBuf,
             int32_t aCount, bool aValidate, bool aTruncate,
             CacheFileIOListener* aCallback)
      : Runnable("net::WriteEvent"),
        mHandle(aHandle),
        mOffset(aOffset),
        mBuf(aBuf),
        mCount(aCount),
        mValidate(aValidate),
        mTruncate(aTruncate),
        mCallback(aCallback) {}

 protected:
  ~WriteEvent() {
    if (!mCallback && mBuf) {
      free(const_cast<char*>(mBuf));
    }
  }

 public:
  NS_IMETHOD Run() override {
    GUARD_ASYNC_WRITE();
    nsresult rv;

    if (mHandle->IsClosed() || (mCallback && mCallback->IsKilled())) {
      // We usually get here only after the internal shutdown
      // (i.e. mShuttingDown == true).  Pretend write has succeeded
      // to avoid any past-shutdown file dooming.
      rv = (CacheObserver::IsPastShutdownIOLag() ||
            CacheFileIOManager::gInstance->mShuttingDown)
               ? NS_OK
               : NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = CacheFileIOManager::gInstance->WriteInternal(
          mHandle, mOffset, mBuf, mCount, mValidate, mTruncate);
      if (NS_FAILED(rv) && !mCallback) {
        // No listener is going to handle the error, doom the file
        CacheFileIOManager::gInstance->DoomFileInternal(mHandle);
      }
    }
    if (mCallback) {
      mCallback->OnDataWritten(mHandle, mBuf, rv);
    } else {
      free(const_cast<char*>(mBuf));
      mBuf = nullptr;
    }

    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;
  int64_t mOffset;
  const char* mBuf;
  int32_t mCount;
  bool mValidate : 1;
  bool mTruncate : 1;
  nsCOMPtr<CacheFileIOListener> mCallback;
};

class DoomFileEvent : public Runnable {
 public:
  DoomFileEvent(CacheFileHandle* aHandle, CacheFileIOListener* aCallback)
      : Runnable("net::DoomFileEvent"),
        mCallback(aCallback),
        mHandle(aHandle) {}

 protected:
  ~DoomFileEvent() = default;

 public:
  NS_IMETHOD Run() override {
    nsresult rv;

    if (mHandle->IsClosed()) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = CacheFileIOManager::gInstance->DoomFileInternal(mHandle);
    }

    if (mCallback) {
      mCallback->OnFileDoomed(mHandle, rv);
    }

    return NS_OK;
  }

 protected:
  nsCOMPtr<CacheFileIOListener> mCallback;
  nsCOMPtr<nsIEventTarget> mTarget;
  RefPtr<CacheFileHandle> mHandle;
};

class DoomFileByKeyEvent : public Runnable {
 public:
  DoomFileByKeyEvent(const nsACString& aKey, CacheFileIOListener* aCallback)
      : Runnable("net::DoomFileByKeyEvent"), mCallback(aCallback) {
    SHA1Sum sum;
    sum.update(aKey.BeginReading(), aKey.Length());
    sum.finish(mHash);

    mIOMan = CacheFileIOManager::gInstance;
  }

 protected:
  ~DoomFileByKeyEvent() = default;

 public:
  NS_IMETHOD Run() override {
    nsresult rv;

    if (!mIOMan) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = mIOMan->DoomFileByKeyInternal(&mHash);
      mIOMan = nullptr;
    }

    if (mCallback) {
      mCallback->OnFileDoomed(nullptr, rv);
    }

    return NS_OK;
  }

 protected:
  SHA1Sum::Hash mHash{};
  nsCOMPtr<CacheFileIOListener> mCallback;
  RefPtr<CacheFileIOManager> mIOMan;
};

class ReleaseNSPRHandleEvent : public Runnable {
 public:
  explicit ReleaseNSPRHandleEvent(CacheFileHandle* aHandle)
      : Runnable("net::ReleaseNSPRHandleEvent"), mHandle(aHandle) {}

 protected:
  ~ReleaseNSPRHandleEvent() = default;

 public:
  NS_IMETHOD Run() override {
    if (!mHandle->IsClosed()) {
      CacheFileIOManager::gInstance->MaybeReleaseNSPRHandleInternal(mHandle);
    }

    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;
};

class TruncateSeekSetEOFEvent : public Runnable {
 public:
  TruncateSeekSetEOFEvent(CacheFileHandle* aHandle, int64_t aTruncatePos,
                          int64_t aEOFPos, CacheFileIOListener* aCallback)
      : Runnable("net::TruncateSeekSetEOFEvent"),
        mHandle(aHandle),
        mTruncatePos(aTruncatePos),
        mEOFPos(aEOFPos),
        mCallback(aCallback) {}

 protected:
  ~TruncateSeekSetEOFEvent() = default;

 public:
  NS_IMETHOD Run() override {
    GUARD_ASYNC_WRITE();
    nsresult rv;

    if (mHandle->IsClosed() || (mCallback && mCallback->IsKilled())) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = CacheFileIOManager::gInstance->TruncateSeekSetEOFInternal(
          mHandle, mTruncatePos, mEOFPos);
    }

    if (mCallback) {
      mCallback->OnEOFSet(mHandle, rv);
    }

    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;
  int64_t mTruncatePos;
  int64_t mEOFPos;
  nsCOMPtr<CacheFileIOListener> mCallback;
};

class RenameFileEvent : public Runnable {
 public:
  RenameFileEvent(CacheFileHandle* aHandle, const nsACString& aNewName,
                  CacheFileIOListener* aCallback)
      : Runnable("net::RenameFileEvent"),
        mHandle(aHandle),
        mNewName(aNewName),
        mCallback(aCallback) {}

 protected:
  ~RenameFileEvent() = default;

 public:
  NS_IMETHOD Run() override {
    GUARD_ASYNC_WRITE();
    nsresult rv;

    if (mHandle->IsClosed()) {
      rv = NS_ERROR_NOT_INITIALIZED;
    } else {
      rv = CacheFileIOManager::gInstance->RenameFileInternal(mHandle, mNewName);
    }

    if (mCallback) {
      mCallback->OnFileRenamed(mHandle, rv);
    }

    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;
  nsCString mNewName;
  nsCOMPtr<CacheFileIOListener> mCallback;
};

class InitIndexEntryEvent : public Runnable {
 public:
  InitIndexEntryEvent(CacheFileHandle* aHandle,
                      OriginAttrsHash aOriginAttrsHash, bool aAnonymous,
                      bool aPinning)
      : Runnable("net::InitIndexEntryEvent"),
        mHandle(aHandle),
        mOriginAttrsHash(aOriginAttrsHash),
        mAnonymous(aAnonymous),
        mPinning(aPinning) {}

 protected:
  ~InitIndexEntryEvent() = default;

 public:
  NS_IMETHOD Run() override {
    if (mHandle->IsClosed() || mHandle->IsDoomed()) {
      return NS_OK;
    }

    CacheIndex::InitEntry(mHandle->Hash(), mOriginAttrsHash, mAnonymous,
                          mPinning);

    // We cannot set the filesize before we init the entry. If we're opening
    // an existing entry file, frecency will be set after parsing the entry
    // file, but we must set the filesize here since nobody is going to set it
    // if there is no write to the file.
    uint32_t sizeInK = mHandle->FileSizeInK();
    CacheIndex::UpdateEntry(mHandle->Hash(), nullptr, nullptr, nullptr, nullptr,
                            nullptr, &sizeInK);

    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;
  OriginAttrsHash mOriginAttrsHash;
  bool mAnonymous;
  bool mPinning;
};

class UpdateIndexEntryEvent : public Runnable {
 public:
  UpdateIndexEntryEvent(CacheFileHandle* aHandle, const uint32_t* aFrecency,
                        const bool* aHasAltData, const uint32_t* aLastFetched,
                        const uint32_t* aFetchCount,
                        const uint8_t* aContentType)
      : Runnable("net::UpdateIndexEntryEvent"),
        mHandle(aHandle),
        mHasFrecency(false),
        mHasHasAltData(false),
        mHasLastFetched(false),
        mHasFetchCount(false),
        mHasContentType(false),
        mFrecency(0),
        mHasAltData(false),
        mLastFetched(0),
        mFetchCount(0),
        mContentType(nsICacheEntry::CONTENT_TYPE_UNKNOWN) {
    if (aFrecency) {
      mHasFrecency = true;
      mFrecency = *aFrecency;
    }
    if (aHasAltData) {
      mHasHasAltData = true;
      mHasAltData = *aHasAltData;
    }
    if (aLastFetched) {
      mHasLastFetched = true;
      mLastFetched = *aLastFetched;
    }
    if (aFetchCount) {
      mHasFetchCount = true;
      mFetchCount = *aFetchCount;
    }
    if (aContentType) {
      mHasContentType = true;
      mContentType = *aContentType;
    }
  }

 protected:
  ~UpdateIndexEntryEvent() = default;

 public:
  NS_IMETHOD Run() override {
    if (mHandle->IsClosed() || mHandle->IsDoomed()) {
      return NS_OK;
    }

    CacheIndex::UpdateEntry(mHandle->Hash(),
                            mHasFrecency ? &mFrecency : nullptr,
                            mHasHasAltData ? &mHasAltData : nullptr,
                            mHasLastFetched ? &mLastFetched : nullptr,
                            mHasFetchCount ? &mFetchCount : nullptr,
                            mHasContentType ? &mContentType : nullptr, nullptr);
    return NS_OK;
  }

 protected:
  RefPtr<CacheFileHandle> mHandle;

  bool mHasFrecency;
  bool mHasHasAltData;
  bool mHasLastFetched;
  bool mHasFetchCount;
  bool mHasContentType;

  uint32_t mFrecency;
  bool mHasAltData;
  uint32_t mLastFetched;
  uint32_t mFetchCount;
  uint8_t mContentType;
};

class MetadataWriteScheduleEvent : public Runnable {
 public:
  enum EMode { SCHEDULE, UNSCHEDULE, SHUTDOWN } mMode;

  RefPtr<CacheFile> mFile;
  RefPtr<CacheFileIOManager> mIOMan;

  MetadataWriteScheduleEvent(CacheFileIOManager* aManager, CacheFile* aFile,
                             EMode aMode)
      : Runnable("net::MetadataWriteScheduleEvent"),
        mMode(aMode),
        mFile(aFile),
        mIOMan(aManager) {}

  virtual ~MetadataWriteScheduleEvent() = default;

  NS_IMETHOD Run() override {
    RefPtr<CacheFileIOManager> ioMan = CacheFileIOManager::gInstance;
    if (!ioMan) {
      NS_WARNING(
          "CacheFileIOManager already gone in "
          "MetadataWriteScheduleEvent::Run()");
      return NS_OK;
    }

    switch (mMode) {
      case SCHEDULE:
        ioMan->ScheduleMetadataWriteInternal(mFile);
        break;
      case UNSCHEDULE:
        ioMan->UnscheduleMetadataWriteInternal(mFile);
        break;
      case SHUTDOWN:
        ioMan->ShutdownMetadataWriteSchedulingInternal();
        break;
    }
    return NS_OK;
  }
};

StaticRefPtr<CacheFileIOManager> CacheFileIOManager::gInstance;

NS_IMPL_ISUPPORTS(CacheFileIOManager, nsITimerCallback, nsINamed)

CacheFileIOManager::CacheFileIOManager()

{
  LOG(("CacheFileIOManager::CacheFileIOManager [this=%p]", this));
  MOZ_ASSERT(!gInstance, "multiple CacheFileIOManager instances!");
}

CacheFileIOManager::~CacheFileIOManager() {
  LOG(("CacheFileIOManager::~CacheFileIOManager [this=%p]", this));
}

// static
nsresult CacheFileIOManager::Init() {
  LOG(("CacheFileIOManager::Init()"));

  MOZ_ASSERT(NS_IsMainThread());

  if (gInstance) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  RefPtr<CacheFileIOManager> ioMan = new CacheFileIOManager();

  nsresult rv = ioMan->InitInternal();
  NS_ENSURE_SUCCESS(rv, rv);

  gInstance = std::move(ioMan);

  // Ensure that transport service is initialized on the main thread.
  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  (void)target;

  return NS_OK;
}

nsresult CacheFileIOManager::InitInternal() {
  nsresult rv;

  mIOThread = new CacheIOThread();

  rv = mIOThread->Init();
  MOZ_ASSERT(NS_SUCCEEDED(rv), "Can't create background thread");
  NS_ENSURE_SUCCESS(rv, rv);

  mStartTime = TimeStamp::NowLoRes();

  return NS_OK;
}

// static
nsresult CacheFileIOManager::Shutdown() {
  LOG(("CacheFileIOManager::Shutdown() [gInstance=%p]", gInstance.get()));

  MOZ_ASSERT(NS_IsMainThread());

  if (!gInstance) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  CacheIndex::PreShutdown();

  ShutdownMetadataWriteScheduling();

  RefPtr<ShutdownEvent> ev = new ShutdownEvent();
  ev->PostAndWait();

  MOZ_ASSERT(gInstance->mHandles.HandleCount() == 0);
  MOZ_ASSERT(gInstance->mHandlesByLastUsed.Length() == 0);

  if (gInstance->mIOThread) {
    gInstance->mIOThread->Shutdown();
  }

  CacheIndex::Shutdown();
  DictionaryCache::Shutdown();

  if (CacheObserver::ClearCacheOnShutdown()) {
    auto totalTimer =
        glean::network::disk_cache2_shutdown_clear_private.Measure();
    gInstance->SyncRemoveAllCacheFiles();
  }

  gInstance = nullptr;

  return NS_OK;
}

void CacheFileIOManager::ShutdownInternal() {
  LOG(("CacheFileIOManager::ShutdownInternal() [this=%p]", this));

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  // No new handles can be created after this flag is set
  mShuttingDown = true;

  if (mTrashTimer) {
    mTrashTimer->Cancel();
    mTrashTimer = nullptr;
  }

  // close all handles and delete all associated files
  nsTArray<RefPtr<CacheFileHandle>> handles;
  mHandles.GetAllHandles(&handles);
  handles.AppendElements(mSpecialHandles);

  for (uint32_t i = 0; i < handles.Length(); i++) {
    CacheFileHandle* h = handles[i];
    h->mClosed = true;

    h->Log();

    // Close completely written files.
    MaybeReleaseNSPRHandleInternal(h);
    // Don't bother removing invalid and/or doomed files to improve
    // shutdown perfomrance.
    // Doomed files are already in the doomed directory from which
    // we never reuse files and delete the dir on next session startup.
    // Invalid files don't have metadata and thus won't load anyway
    // (hashes won't match).

    if (!h->IsSpecialFile() && !h->mIsDoomed && !h->mFileExists) {
      CacheIndex::RemoveEntry(h->Hash(), h->Key());
    }

    // Remove the handle from mHandles/mSpecialHandles
    if (h->IsSpecialFile()) {
      mSpecialHandles.RemoveElement(h);
    } else {
      mHandles.RemoveHandle(h);
    }

    // Pointer to the hash is no longer valid once the last handle with the
    // given hash is released. Null out the pointer so that we crash if there
    // is a bug in this code and we dereference the pointer after this point.
    if (!h->IsSpecialFile()) {
      h->mHash = nullptr;
    }
  }

  // Assert the table is empty. When we are here, no new handles can be added
  // and handles will no longer remove them self from this table and we don't
  // want to keep invalid handles here. Also, there is no lookup after this
  // point to happen.
  MOZ_ASSERT(mHandles.HandleCount() == 0);

  // Release trash directory enumerator
  if (mTrashDirEnumerator) {
    mTrashDirEnumerator->Close();
    mTrashDirEnumerator = nullptr;
  }

  if (mContextEvictor) {
    mContextEvictor->Shutdown();
    mContextEvictor = nullptr;
  }
}

// static
nsresult CacheFileIOManager::OnProfile() {
  LOG(("CacheFileIOManager::OnProfile() [gInstance=%p]", gInstance.get()));

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (!ioMan) {
    // CacheFileIOManager::Init() failed, probably could not create the IO
    // thread, just go with it...
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv;

  nsCOMPtr<nsIFile> directory;

  CacheObserver::ParentDirOverride(getter_AddRefs(directory));

#if defined(MOZ_WIDGET_ANDROID)
  nsCOMPtr<nsIFile> profilelessDirectory;
  char* cachePath = getenv("CACHE_DIRECTORY");
  if (!directory && cachePath && *cachePath) {
    rv = NS_NewNativeLocalFile(nsDependentCString(cachePath),
                               getter_AddRefs(directory));
    if (NS_SUCCEEDED(rv)) {
      // Save this directory as the profileless path.
      rv = directory->Clone(getter_AddRefs(profilelessDirectory));
      NS_ENSURE_SUCCESS(rv, rv);

      // Add profile leaf name to the directory name to distinguish
      // multiple profiles Fennec supports.
      nsCOMPtr<nsIFile> profD;
      rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                  getter_AddRefs(profD));

      nsAutoCString leafName;
      if (NS_SUCCEEDED(rv)) {
        rv = profD->GetNativeLeafName(leafName);
      }
      if (NS_SUCCEEDED(rv)) {
        rv = directory->AppendNative(leafName);
      }
      if (NS_FAILED(rv)) {
        directory = nullptr;
      }
    }
  }
#endif

  if (!directory) {
    rv = NS_GetSpecialDirectory(NS_APP_CACHE_PARENT_DIR,
                                getter_AddRefs(directory));
  }

  if (!directory) {
    rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                                getter_AddRefs(directory));
  }

  if (directory) {
    rv = directory->Append(u"cache2"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // All functions return a clone.
  ioMan->mCacheDirectory.swap(directory);

#if defined(MOZ_WIDGET_ANDROID)
  if (profilelessDirectory) {
    rv = profilelessDirectory->Append(u"cache2"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  ioMan->mCacheProfilelessDirectory.swap(profilelessDirectory);
#endif

  if (ioMan->mCacheDirectory) {
    CacheIndex::Init(ioMan->mCacheDirectory);
  }

  return NS_OK;
}

static bool inBackgroundTask() {
  MOZ_ASSERT(NS_IsMainThread(), "backgroundtasks are main thread only");
#if defined(MOZ_BACKGROUNDTASKS)
  nsCOMPtr<nsIBackgroundTasks> backgroundTaskService =
      do_GetService("@mozilla.org/backgroundtasks;1");
  if (!backgroundTaskService) {
    return false;
  }
  bool isBackgroundTask = false;
  backgroundTaskService->GetIsBackgroundTaskMode(&isBackgroundTask);
  return isBackgroundTask;
#else
  return false;
#endif
}

// static
nsresult CacheFileIOManager::OnDelayedStartupFinished() {
  // If we don't clear the cache at shutdown, or we don't use a
  // background task then there's no need to dispatch a cleanup task
  // at startup
  if (!CacheObserver::ClearCacheOnShutdown()) {
    return NS_OK;
  }
  if (!StaticPrefs::network_cache_shutdown_purge_in_background_task()) {
    return NS_OK;
  }

  // If this is a background task already, there's no need to
  // dispatch another one.
  if (inBackgroundTask()) {
    return NS_OK;
  }

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  nsCOMPtr<nsIEventTarget> target = IOTarget();
  if (NS_WARN_IF(!ioMan || !target)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return target->Dispatch(
      NS_NewRunnableFunction("CacheFileIOManager::OnDelayedStartupFinished",
                             [ioMan = std::move(ioMan)] {
                               ioMan->DispatchPurgeTask(""_ns, "0"_ns,
                                                        kPurgeExtension);
                             }),
      nsIEventTarget::DISPATCH_NORMAL);
}

// static
nsresult CacheFileIOManager::OnIdleDaily() {
  // In case the background task process fails for some reason (bug 1848542)
  // We will remove the directories in the main process on a daily idle.
  if (!CacheObserver::ClearCacheOnShutdown()) {
    return NS_OK;
  }
  if (!StaticPrefs::network_cache_shutdown_purge_in_background_task()) {
    return NS_OK;
  }

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  nsCOMPtr<nsIFile> parentDir;
  nsresult rv = ioMan->mCacheDirectory->GetParent(getter_AddRefs(parentDir));
  if (NS_FAILED(rv) || !parentDir) {
    return NS_OK;
  }

  NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "CacheFileIOManager::CheckLeftoverFolders",
          [dir = std::move(parentDir)] {
            nsCOMPtr<nsIDirectoryEnumerator> directories;
            nsresult rv = dir->GetDirectoryEntries(getter_AddRefs(directories));
            if (NS_FAILED(rv)) {
              return;
            }
            bool hasMoreElements = false;
            while (
                NS_SUCCEEDED(directories->HasMoreElements(&hasMoreElements)) &&
                hasMoreElements) {
              nsCOMPtr<nsIFile> subdir;
              rv = directories->GetNextFile(getter_AddRefs(subdir));
              if (NS_FAILED(rv) || !subdir) {
                break;
              }
              nsAutoCString leafName;
              rv = subdir->GetNativeLeafName(leafName);
              if (NS_FAILED(rv)) {
                continue;
              }
              if (leafName.Find(kPurgeExtension) != kNotFound) {
                mozilla::glean::networking::residual_cache_folder_count.Add(1);
                // A read-only entry anywhere in the folder makes both
                // DeleteFileW and RemoveDirectoryW fail with ACCESS_DENIED, so
                // clear the attribute and retry rather than leaving the folder
                // behind forever (bug 1882163).
                if (IOUtils::RemoveSync(subdir, /* aIgnoreAbsent */ true,
                                        /* aRecursive */ true,
                                        /* aRetryReadonly */ true)
                        .isOk()) {
                  mozilla::glean::networking::residual_cache_folder_removal
                      .Get("success"_ns)
                      .Add(1);
                } else {
                  mozilla::glean::networking::residual_cache_folder_removal
                      .Get("failure"_ns)
                      .Add(1);
                }
              }
            }

            return;
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  return NS_OK;
}

// static
already_AddRefed<nsIEventTarget> CacheFileIOManager::IOTarget() {
  nsCOMPtr<nsIEventTarget> target;
  if (gInstance && gInstance->mIOThread) {
    target = gInstance->mIOThread->Target();
  }

  return target.forget();
}

// static
already_AddRefed<CacheIOThread> CacheFileIOManager::IOThread() {
  RefPtr<CacheIOThread> thread;
  if (gInstance) {
    thread = gInstance->mIOThread;
  }

  return thread.forget();
}

// static
bool CacheFileIOManager::IsOnIOThread() {
  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (ioMan && ioMan->mIOThread) {
    return ioMan->mIOThread->IsCurrentThread();
  }

  return false;
}

// static
bool CacheFileIOManager::IsOnIOThreadOrCeased() {
  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (ioMan && ioMan->mIOThread) {
    return ioMan->mIOThread->IsCurrentThread();
  }

  // Ceased...
  return true;
}

// static
bool CacheFileIOManager::IsShutdown() {
  if (!gInstance) {
    return true;
  }
  return gInstance->mShuttingDown;
}

// static
nsresult CacheFileIOManager::ScheduleMetadataWrite(CacheFile* aFile) {
  RefPtr<CacheFileIOManager> ioMan = gInstance;
  NS_ENSURE_TRUE(ioMan, NS_ERROR_NOT_INITIALIZED);

  NS_ENSURE_TRUE(!ioMan->mShuttingDown, NS_ERROR_NOT_INITIALIZED);

  RefPtr<MetadataWriteScheduleEvent> event = new MetadataWriteScheduleEvent(
      ioMan, aFile, MetadataWriteScheduleEvent::SCHEDULE);
  nsCOMPtr<nsIEventTarget> target = ioMan->IOTarget();
  NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
  return target->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
}

nsresult CacheFileIOManager::ScheduleMetadataWriteInternal(CacheFile* aFile) {
  MOZ_ASSERT(IsOnIOThreadOrCeased());

  nsresult rv;

  if (!mMetadataWritesTimer) {
    rv = NS_NewTimerWithCallback(getter_AddRefs(mMetadataWritesTimer), this,
                                 kMetadataWriteDelay, nsITimer::TYPE_ONE_SHOT);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mScheduledMetadataWrites.IndexOf(aFile) !=
      nsTArray<RefPtr<mozilla::net::CacheFile>>::NoIndex) {
    return NS_OK;
  }

  mScheduledMetadataWrites.AppendElement(aFile);

  return NS_OK;
}

// static
nsresult CacheFileIOManager::UnscheduleMetadataWrite(CacheFile* aFile) {
  RefPtr<CacheFileIOManager> ioMan = gInstance;
  NS_ENSURE_TRUE(ioMan, NS_ERROR_NOT_INITIALIZED);

  NS_ENSURE_TRUE(!ioMan->mShuttingDown, NS_ERROR_NOT_INITIALIZED);

  RefPtr<MetadataWriteScheduleEvent> event = new MetadataWriteScheduleEvent(
      ioMan, aFile, MetadataWriteScheduleEvent::UNSCHEDULE);
  nsCOMPtr<nsIEventTarget> target = ioMan->IOTarget();
  NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
  return target->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
}

void CacheFileIOManager::UnscheduleMetadataWriteInternal(CacheFile* aFile) {
  MOZ_ASSERT(IsOnIOThreadOrCeased());

  mScheduledMetadataWrites.RemoveElement(aFile);

  if (mScheduledMetadataWrites.Length() == 0 && mMetadataWritesTimer) {
    mMetadataWritesTimer->Cancel();
    mMetadataWritesTimer = nullptr;
  }
}

// static
nsresult CacheFileIOManager::ShutdownMetadataWriteScheduling() {
  RefPtr<CacheFileIOManager> ioMan = gInstance;
  NS_ENSURE_TRUE(ioMan, NS_ERROR_NOT_INITIALIZED);

  RefPtr<MetadataWriteScheduleEvent> event = new MetadataWriteScheduleEvent(
      ioMan, nullptr, MetadataWriteScheduleEvent::SHUTDOWN);
  nsCOMPtr<nsIEventTarget> target = ioMan->IOTarget();
  NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
  return target->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
}

void CacheFileIOManager::ShutdownMetadataWriteSchedulingInternal() {
  MOZ_ASSERT(IsOnIOThreadOrCeased());

  nsTArray<RefPtr<CacheFile>> files = std::move(mScheduledMetadataWrites);
  for (uint32_t i = 0; i < files.Length(); ++i) {
    CacheFile* file = files[i];
    file->WriteMetadataIfNeeded();
  }

  if (mMetadataWritesTimer) {
    mMetadataWritesTimer->Cancel();
    mMetadataWritesTimer = nullptr;
  }
}

NS_IMETHODIMP
CacheFileIOManager::Notify(nsITimer* aTimer) {
  MOZ_ASSERT(IsOnIOThreadOrCeased());
  MOZ_ASSERT(mMetadataWritesTimer == aTimer);

  mMetadataWritesTimer = nullptr;

  nsTArray<RefPtr<CacheFile>> files = std::move(mScheduledMetadataWrites);
  for (uint32_t i = 0; i < files.Length(); ++i) {
    CacheFile* file = files[i];
    file->WriteMetadataIfNeeded();
  }

  return NS_OK;
}

NS_IMETHODIMP
CacheFileIOManager::GetName(nsACString& aName) {
  aName.AssignLiteral("CacheFileIOManager");
  return NS_OK;
}

// static
nsresult CacheFileIOManager::OpenFile(const nsACString& aKey, uint32_t aFlags,
                                      CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::OpenFile() [key=%s, flags=%d, listener=%p]",
       PromiseFlatCString(aKey).get(), aFlags, aCallback));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  bool priority = aFlags & CacheFileIOManager::PRIORITY;
  RefPtr<OpenFileEvent> ev = new OpenFileEvent(aKey, aFlags, aCallback);
  rv = ioMan->mIOThread->Dispatch(
      ev, priority ? CacheIOThread::OPEN_PRIORITY : CacheIOThread::OPEN);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::OpenFileInternal(const SHA1Sum::Hash* aHash,
                                              const nsACString& aKey,
                                              uint32_t aFlags,
                                              CacheFileHandle** _retval) {
  LOG(
      ("CacheFileIOManager::OpenFileInternal() [hash=%08x%08x%08x%08x%08x, "
       "key=%s, flags=%d]",
       LOGSHA1(aHash), PromiseFlatCString(aKey).get(), aFlags));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

  nsresult rv;

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  CacheIOThread::Cancelable cancelable(
      true /* never called for special handles */);

  if (!mTreeCreated) {
    rv = CreateCacheTree();
    if (NS_FAILED(rv)) return rv;
  }

  CacheFileHandle::PinningStatus pinning =
      aFlags & PINNED ? CacheFileHandle::PinningStatus::PINNED
                      : CacheFileHandle::PinningStatus::NON_PINNED;

  nsCOMPtr<nsIFile> file;
  rv = GetFile(aHash, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<CacheFileHandle> handle;
  mHandles.GetHandle(aHash, getter_AddRefs(handle));

  if ((aFlags & (OPEN | CREATE | CREATE_NEW)) == CREATE_NEW) {
    if (handle) {
      rv = DoomFileInternal(handle);
      NS_ENSURE_SUCCESS(rv, rv);
      handle = nullptr;
    }

    handle = mHandles.NewHandle(aHash, aFlags & PRIORITY, pinning);

    bool exists;
    rv = file->Exists(&exists);
    NS_ENSURE_SUCCESS(rv, rv);

    if (exists) {
      CacheIndex::RemoveEntry(aHash, handle->Key());

      LOG(
          ("CacheFileIOManager::OpenFileInternal() - Removing old file from "
           "disk"));
      rv = file->Remove(false);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot remove old entry from the disk");
        LOG(
            ("CacheFileIOManager::OpenFileInternal() - Removing old file failed"
             ". [rv=0x%08" PRIx32 "]",
             static_cast<uint32_t>(rv)));
      }
    }

    CacheIndex::AddEntry(aHash);
    handle->mFile.swap(file);
    handle->mFileSize = 0;
  }

  if (handle) {
    handle.swap(*_retval);
    return NS_OK;
  }

  bool exists, evictedAsPinned = false, evictedAsNonPinned = false;
  rv = file->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (exists && mContextEvictor) {
    if (mContextEvictor->ContextsCount() == 0) {
      mContextEvictor = nullptr;
    } else {
      mContextEvictor->WasEvicted(aKey, file, &evictedAsPinned,
                                  &evictedAsNonPinned);
    }
  }

  if (!exists && (aFlags & (OPEN | CREATE | CREATE_NEW)) == OPEN) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (exists) {
    // For existing files we determine the pinning status later, after the
    // metadata gets parsed.
    pinning = CacheFileHandle::PinningStatus::UNKNOWN;
  }

  handle = mHandles.NewHandle(aHash, aFlags & PRIORITY, pinning);
  if (exists) {
    // If this file has been found evicted through the context file evictor
    // above for any of pinned or non-pinned state, these calls ensure we doom
    // the handle ASAP we know the real pinning state after metadata has been
    // parsed.  DoomFileInternal on the |handle| doesn't doom right now, since
    // the pinning state is unknown and we pass down a pinning restriction.
    if (evictedAsPinned) {
      rv = DoomFileInternal(handle, DOOM_WHEN_PINNED);
      MOZ_ASSERT(!handle->IsDoomed() && NS_SUCCEEDED(rv));
    }
    if (evictedAsNonPinned) {
      rv = DoomFileInternal(handle, DOOM_WHEN_NON_PINNED);
      MOZ_ASSERT(!handle->IsDoomed() && NS_SUCCEEDED(rv));
    }

    int64_t fileSize = -1;
    rv = file->GetFileSize(&fileSize);
    NS_ENSURE_SUCCESS(rv, rv);

    handle->mFileSize = fileSize;
    handle->mFileExists = true;

    CacheIndex::EnsureEntryExists(aHash);
  } else {
    handle->mFileSize = 0;

    CacheIndex::AddEntry(aHash);
  }

  handle->mFile.swap(file);
  handle.swap(*_retval);
  return NS_OK;
}

nsresult CacheFileIOManager::OpenSpecialFileInternal(
    const nsACString& aKey, uint32_t aFlags, CacheFileHandle** _retval) {
  LOG(("CacheFileIOManager::OpenSpecialFileInternal() [key=%s, flags=%d]",
       PromiseFlatCString(aKey).get(), aFlags));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

  nsresult rv;

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!mTreeCreated) {
    rv = CreateCacheTree();
    if (NS_FAILED(rv)) return rv;
  }

  nsCOMPtr<nsIFile> file;
  rv = GetSpecialFile(aKey, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<CacheFileHandle> handle;
  for (uint32_t i = 0; i < mSpecialHandles.Length(); i++) {
    if (!mSpecialHandles[i]->IsDoomed() && mSpecialHandles[i]->Key() == aKey) {
      handle = mSpecialHandles[i];
      break;
    }
  }

  if ((aFlags & (OPEN | CREATE | CREATE_NEW)) == CREATE_NEW) {
    if (handle) {
      rv = DoomFileInternal(handle);
      NS_ENSURE_SUCCESS(rv, rv);
      handle = nullptr;
    }

    handle = new CacheFileHandle(aKey, aFlags & PRIORITY,
                                 CacheFileHandle::PinningStatus::NON_PINNED);
    mSpecialHandles.AppendElement(handle);

    bool exists;
    rv = file->Exists(&exists);
    NS_ENSURE_SUCCESS(rv, rv);

    if (exists) {
      LOG(
          ("CacheFileIOManager::OpenSpecialFileInternal() - Removing file from "
           "disk"));
      rv = file->Remove(false);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot remove old entry from the disk");
        LOG(
            ("CacheFileIOManager::OpenSpecialFileInternal() - Removing file "
             "failed. [rv=0x%08" PRIx32 "]",
             static_cast<uint32_t>(rv)));
      }
    }

    handle->mFile.swap(file);
    handle->mFileSize = 0;
  }

  if (handle) {
    handle.swap(*_retval);
    return NS_OK;
  }

  bool exists;
  rv = file->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists && (aFlags & (OPEN | CREATE | CREATE_NEW)) == OPEN) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  handle = new CacheFileHandle(aKey, aFlags & PRIORITY,
                               CacheFileHandle::PinningStatus::NON_PINNED);
  mSpecialHandles.AppendElement(handle);

  if (exists) {
    int64_t fileSize = -1;
    rv = file->GetFileSize(&fileSize);
    NS_ENSURE_SUCCESS(rv, rv);

    handle->mFileSize = fileSize;
    handle->mFileExists = true;
  } else {
    handle->mFileSize = 0;
  }

  handle->mFile.swap(file);
  handle.swap(*_retval);
  return NS_OK;
}

void CacheFileIOManager::CloseHandleInternal(CacheFileHandle* aHandle) {
  nsresult rv;
  LOG(("CacheFileIOManager::CloseHandleInternal() [handle=%p]", aHandle));

  MOZ_ASSERT(!aHandle->IsClosed());

  aHandle->Log();

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  CacheIOThread::Cancelable cancelable(!aHandle->IsSpecialFile());

  // Maybe close file handle (can be legally bypassed after shutdown)
  rv = MaybeReleaseNSPRHandleInternal(aHandle);

  // Delete the file if the entry was doomed or invalid and
  // filedesc properly closed
  if ((aHandle->mIsDoomed || aHandle->mInvalid) && aHandle->mFileExists &&
      NS_SUCCEEDED(rv)) {
    LOG(
        ("CacheFileIOManager::CloseHandleInternal() - Removing file from "
         "disk"));

    rv = aHandle->mFile->Remove(false);
    if (NS_SUCCEEDED(rv)) {
      aHandle->mFileExists = false;
    } else {
      LOG(("  failed to remove the file [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
    }
  }

  if (!aHandle->IsSpecialFile() && !aHandle->mIsDoomed &&
      (aHandle->mInvalid || !aHandle->mFileExists)) {
    CacheIndex::RemoveEntry(aHandle->Hash(), aHandle->Key());
  }

  // Don't remove handles after shutdown
  if (!mShuttingDown) {
    if (aHandle->IsSpecialFile()) {
      mSpecialHandles.RemoveElement(aHandle);
    } else {
      mHandles.RemoveHandle(aHandle);
    }
  }
}

// static
nsresult CacheFileIOManager::Read(CacheFileHandle* aHandle, int64_t aOffset,
                                  char* aBuf, int32_t aCount,
                                  CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::Read() [handle=%p, offset=%" PRId64 ", count=%d, "
       "listener=%p]",
       aHandle, aOffset, aCount, aCallback));

  if (CacheObserver::ShuttingDown()) {
    LOG(("  no reads after shutdown"));
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!aHandle) {
    return NS_ERROR_NULL_POINTER;
  }

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<ReadEvent> ev =
      new ReadEvent(aHandle, aOffset, aBuf, aCount, aCallback);
#if defined(MOZ_CACHE_ASYNC_IO)
  if (IsOnIOThread()) {
    // If we are already on the IO thread, we can run the event
    // inline. However, preserve the behavior that OnDataRead will be
    // called async when read operation is unsucessful in case the
    // caller relies this assumption.
    ev->InlineRun(true);
    return NS_OK;
  }
#endif

  rv = ioMan->mIOThread->Dispatch(ev, aHandle->IsPriority()
                                          ? CacheIOThread::READ_PRIORITY
                                          : CacheIOThread::READ);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::ReadInternal(CacheFileHandle* aHandle,
                                          int64_t aOffset, char* aBuf,
                                          int32_t aCount,
                                          ReadEvent* aReadEvent) {
  LOG(("CacheFileIOManager::ReadInternal() [handle=%p, offset=%" PRId64
       ", count=%d]",
       aHandle, aOffset, aCount));

  nsresult rv;

  if (CacheObserver::ShuttingDown()) {
    LOG(("  no reads after shutdown"));
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!aHandle->mFileExists) {
    NS_WARNING("Trying to read from non-existent file");
    return NS_ERROR_NOT_AVAILABLE;
  }

  CacheIOThread::Cancelable cancelable(!aHandle->IsSpecialFile());

  if (!aHandle->mFD) {
    rv = OpenNSPRHandle(aHandle);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NSPRHandleUsed(aHandle);
  }

  // Check again, OpenNSPRHandle could figure out the file was gone.
  if (!aHandle->mFileExists) {
    NS_WARNING("Trying to read from non-existent file");
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_ASSERT(aOffset + aCount <= aHandle->mFileSize);

#if !defined(MOZ_CACHE_ASYNC_IO)
  int64_t offset = PR_Seek64(aHandle->mFD, aOffset, PR_SEEK_SET);
  if (offset == -1) {
    return NS_ERROR_FAILURE;
  }

  int32_t bytesRead = PR_Read(aHandle->mFD, aBuf, aCount);
  if (bytesRead != aCount) {
    return NS_ERROR_FAILURE;
  }
#else
  PRFileDesc* fd;
  AutoFDClose autoFd;

  if (!aHandle->IsAsyncOperationRunning()) {
    // If the handle is not running an async operation, we can use the
    // file descriptor directly without extra file open operation.
    fd = aHandle->mFD;
  } else {
    PRFileDesc* rawFd = nullptr;
    rv = aHandle->mFile->OpenNSPRFileDesc(PR_RDONLY, 0600, &rawFd);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    // Assign the raw pointer to AutoFDClose
    autoFd.reset(rawFd);
    fd = autoFd.get();
  }

  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  MOZ_ASSERT(target);
  const bool isPriority = aHandle->IsPriority();
  rv = target->Dispatch(
      NS_NewRunnableFunction(
          "CacheFileIOManager::ReadInternal",
          [fd, autoFd = std::move(autoFd), aBuf, aCount, aOffset,
           readevent = RefPtr(aReadEvent), isPriority]() mutable {
            nsresult rv = NS_OK;
            int64_t offset = PR_Seek64(fd, aOffset, PR_SEEK_SET);
            if (offset == -1) {
              LOG(
                  ("CacheFileIOManager::ReadInternal() - PR_Seek64 failed "
                   "[offset=%d]\n",
                   static_cast<int32_t>(aOffset)));
              rv = NS_ERROR_FAILURE;
            }

            if (NS_SUCCEEDED(rv)) {
              int32_t bytesRead = PR_Read(fd, aBuf, aCount);
              if (bytesRead != aCount) {
                LOG(
                    ("CacheFileIOManager::ReadInternal() - PR_Read failed "
                     "[bytesRead=%d]\n",
                     bytesRead));
                rv = NS_ERROR_FAILURE;
              }
            }

            // Post completion back to the cache IO thread at the READ level,
            // so that test suspensions at READ properly block completion.
            DebugOnly<nsresult> rv_dispatch = gInstance->mIOThread->Dispatch(
                NewRunnableMethod<nsresult>(
                    "CacheFileIOManager::ReadEvent::OnComplete", readevent,
                    &ReadEvent::OnComplete, rv),
                isPriority ? CacheIOThread::READ_PRIORITY
                           : CacheIOThread::READ);

            MOZ_ASSERT(NS_SUCCEEDED(rv_dispatch));
          }),
      NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  aHandle->StartAsyncOperation();
#endif

  return NS_OK;
}

// static
nsresult CacheFileIOManager::Write(CacheFileHandle* aHandle, int64_t aOffset,
                                   const char* aBuf, int32_t aCount,
                                   bool aValidate, bool aTruncate,
                                   CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::Write() [handle=%p, offset=%" PRId64 ", count=%d, "
       "validate=%d, truncate=%d, listener=%p]",
       aHandle, aOffset, aCount, aValidate, aTruncate, aCallback));

  MOZ_ASSERT(aCallback);

  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || aCallback->IsKilled() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<WriteEvent> ev = new WriteEvent(aHandle, aOffset, aBuf, aCount,
                                         aValidate, aTruncate, aCallback);
  return ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                            ? CacheIOThread::WRITE_PRIORITY
                                            : CacheIOThread::WRITE);
}

// static
nsresult CacheFileIOManager::WriteWithoutCallback(CacheFileHandle* aHandle,
                                                  int64_t aOffset, char* aBuf,
                                                  int32_t aCount,
                                                  bool aValidate,
                                                  bool aTruncate) {
  LOG(("CacheFileIOManager::WriteWithoutCallback() [handle=%p, offset=%" PRId64
       ", count=%d, "
       "validate=%d, truncate=%d]",
       aHandle, aOffset, aCount, aValidate, aTruncate));

  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    // When no callback is provided, CacheFileIOManager is responsible for
    // releasing the buffer. We must release it even in case of failure.
    free(aBuf);
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<WriteEvent> ev = new WriteEvent(aHandle, aOffset, aBuf, aCount,
                                         aValidate, aTruncate, nullptr);
  return ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                            ? CacheIOThread::WRITE_PRIORITY
                                            : CacheIOThread::WRITE);
}

static nsresult TruncFile(PRFileDesc* aFD, int64_t aEOF) {
#if defined(XP_UNIX)
  if (ftruncate(PR_FileDesc2NativeHandle(aFD), aEOF) != 0) {
    NS_ERROR("ftruncate failed");
    return NS_ERROR_FAILURE;
  }
#elif defined(XP_WIN)
  int64_t cnt = PR_Seek64(aFD, aEOF, PR_SEEK_SET);
  if (cnt == -1) {
    return NS_ERROR_FAILURE;
  }
  if (!SetEndOfFile((HANDLE)PR_FileDesc2NativeHandle(aFD))) {
    NS_ERROR("SetEndOfFile failed");
    return NS_ERROR_FAILURE;
  }
#else
  MOZ_ASSERT(false, "Not implemented!");
  return NS_ERROR_NOT_IMPLEMENTED;
#endif

  return NS_OK;
}

nsresult CacheFileIOManager::WriteInternal(CacheFileHandle* aHandle,
                                           int64_t aOffset, const char* aBuf,
                                           int32_t aCount, bool aValidate,
                                           bool aTruncate) {
  LOG(("CacheFileIOManager::WriteInternal() [handle=%p, offset=%" PRId64
       ", count=%d, "
       "validate=%d, truncate=%d]",
       aHandle, aOffset, aCount, aValidate, aTruncate));

  nsresult rv;

  if (aHandle->mKilled) {
    LOG(("  handle already killed, nothing written"));
    return NS_OK;
  }

  if (CacheObserver::ShuttingDown() && (!aValidate || !aHandle->mFD)) {
    aHandle->mKilled = true;
    LOG(("  killing the handle, nothing written"));
    return NS_OK;
  }

  if (CacheObserver::IsPastShutdownIOLag()) {
    LOG(("  past the shutdown I/O lag, nothing written"));
    // Pretend the write has succeeded, otherwise upper layers will doom
    // the file and we end up with I/O anyway.
    return NS_OK;
  }

  CacheIOThread::Cancelable cancelable(!aHandle->IsSpecialFile());

  if (!aHandle->mFileExists) {
    rv = CreateFile(aHandle);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!aHandle->mFD) {
    rv = OpenNSPRHandle(aHandle);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NSPRHandleUsed(aHandle);
  }

  // Check again, OpenNSPRHandle could figure out the file was gone.
  if (!aHandle->mFileExists) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // When this operation would increase cache size, check whether the cache size
  // reached the hard limit and whether it would cause critical low disk space.
  if (aHandle->mFileSize < aOffset + aCount) {
    if (mOverLimitEvicting && mCacheSizeOnHardLimit) {
      LOG(
          ("CacheFileIOManager::WriteInternal() - failing because cache size "
           "reached hard limit!"));
      return NS_ERROR_FILE_NO_DEVICE_SPACE;
    }

    int64_t freeSpace;
    rv = mCacheDirectory->GetDiskSpaceAvailable(&freeSpace);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      freeSpace = -1;
      LOG(
          ("CacheFileIOManager::WriteInternal() - GetDiskSpaceAvailable() "
           "failed! [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
    } else {
      freeSpace >>= 10;  // bytes to kilobytes
      uint32_t limit = CacheObserver::DiskFreeSpaceHardLimit();
      if (freeSpace - aOffset - aCount + aHandle->mFileSize < limit) {
        LOG(
            ("CacheFileIOManager::WriteInternal() - Low free space, refusing "
             "to write! [freeSpace=%" PRId64 "kB, limit=%ukB]",
             freeSpace, limit));
        return NS_ERROR_FILE_NO_DEVICE_SPACE;
      }
    }
  }

  // Write invalidates the entry by default
  aHandle->mInvalid = true;

  int64_t offset = PR_Seek64(aHandle->mFD, aOffset, PR_SEEK_SET);
  if (offset == -1) {
    return NS_ERROR_FAILURE;
  }

  int32_t bytesWritten = PR_Write(aHandle->mFD, aBuf, aCount);

  if (bytesWritten != -1) {
    uint32_t oldSizeInK = aHandle->FileSizeInK();
    int64_t writeEnd = aOffset + bytesWritten;

    if (aTruncate) {
      rv = TruncFile(aHandle->mFD, writeEnd);
      NS_ENSURE_SUCCESS(rv, rv);

      aHandle->mFileSize = writeEnd;
    } else {
      if (aHandle->mFileSize < writeEnd) {
        aHandle->mFileSize = writeEnd;
      }
    }

    uint32_t newSizeInK = aHandle->FileSizeInK();

    if (oldSizeInK != newSizeInK && !aHandle->IsDoomed() &&
        !aHandle->IsSpecialFile()) {
      CacheIndex::UpdateEntry(aHandle->Hash(), nullptr, nullptr, nullptr,
                              nullptr, nullptr, &newSizeInK);

      if (oldSizeInK < newSizeInK) {
        EvictIfOverLimitInternal();
      }
    }

    CacheIndex::UpdateTotalBytesWritten(bytesWritten);
  }

  if (bytesWritten != aCount) {
    return NS_ERROR_FAILURE;
  }

  // Write was successful and this write validates the entry (i.e. metadata)
  if (aValidate) {
    aHandle->mInvalid = false;
  }

  return NS_OK;
}

// static
nsresult CacheFileIOManager::DoomFile(CacheFileHandle* aHandle,
                                      CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::DoomFile() [handle=%p, listener=%p]", aHandle,
       aCallback));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<DoomFileEvent> ev = new DoomFileEvent(aHandle, aCallback);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->IsPriority()
                                          ? CacheIOThread::OPEN_PRIORITY
                                          : CacheIOThread::OPEN);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::DoomFileInternal(
    CacheFileHandle* aHandle, PinningDoomRestriction aPinningDoomRestriction,
    bool aClearDictionary) {
  LOG(("CacheFileIOManager::DoomFileInternal() [handle=%p]", aHandle));
  aHandle->Log();

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  nsresult rv;

  if (aHandle->IsDoomed()) {
    return NS_OK;
  }

  CacheIOThread::Cancelable cancelable(!aHandle->IsSpecialFile());

  if (aPinningDoomRestriction > NO_RESTRICTION) {
    switch (aHandle->mPinning) {
      case CacheFileHandle::PinningStatus::NON_PINNED:
        if (MOZ_LIKELY(aPinningDoomRestriction != DOOM_WHEN_NON_PINNED)) {
          LOG(("  not dooming, it's a non-pinned handle"));
          return NS_OK;
        }
        // Doom now
        break;

      case CacheFileHandle::PinningStatus::PINNED:
        if (MOZ_UNLIKELY(aPinningDoomRestriction != DOOM_WHEN_PINNED)) {
          LOG(("  not dooming, it's a pinned handle"));
          return NS_OK;
        }
        // Doom now
        break;

      case CacheFileHandle::PinningStatus::UNKNOWN:
        if (MOZ_LIKELY(aPinningDoomRestriction == DOOM_WHEN_NON_PINNED)) {
          LOG(("  doom when non-pinned set"));
          aHandle->mDoomWhenFoundNonPinned = true;
        } else if (MOZ_UNLIKELY(aPinningDoomRestriction == DOOM_WHEN_PINNED)) {
          LOG(("  doom when pinned set"));
          aHandle->mDoomWhenFoundPinned = true;
        }

        LOG(("  pinning status not known, deferring doom decision"));
        return NS_OK;
    }
  }

  if (aHandle->mFileExists) {
    // we need to move the current file to the doomed directory
    rv = MaybeReleaseNSPRHandleInternal(aHandle, true);
    NS_ENSURE_SUCCESS(rv, rv);

    // find unused filename
    nsCOMPtr<nsIFile> file;
    rv = GetDoomedFile(getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIFile> parentDir;
    rv = file->GetParent(getter_AddRefs(parentDir));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aHandle->mFile->MoveToNative(parentDir, leafName);
    if (NS_ERROR_FILE_NOT_FOUND == rv) {
      LOG(("  file already removed under our hands"));
      aHandle->mFileExists = false;
      rv = NS_OK;
    } else {
      NS_ENSURE_SUCCESS(rv, rv);
      aHandle->mFile.swap(file);
    }
  }

  if (!aHandle->IsSpecialFile()) {
    // Ensure the string doesn't disappear with the handle
    RefPtr<CacheFileHandle> handle(aHandle);
    CacheIndex::RemoveEntry(aHandle->Hash(), aHandle->Key(), aClearDictionary);
  }

  aHandle->mIsDoomed = true;

  if (!aHandle->IsSpecialFile()) {
    RefPtr<CacheStorageService> storageService = CacheStorageService::Self();
    if (storageService) {
      nsAutoCString idExtension, url;
      nsCOMPtr<nsILoadContextInfo> info =
          CacheFileUtils::ParseKey(aHandle->Key(), &idExtension, &url);
      MOZ_ASSERT(info);
      if (info) {
        storageService->CacheFileDoomed(aHandle->mKey, info, idExtension, url);
      }
    }
  }

  return NS_OK;
}

// static
nsresult CacheFileIOManager::DoomFileByKey(const nsACString& aKey,
                                           CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::DoomFileByKey() [key=%s, listener=%p]",
       PromiseFlatCString(aKey).get(), aCallback));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<DoomFileByKeyEvent> ev = new DoomFileByKeyEvent(aKey, aCallback);
  rv = ioMan->mIOThread->DispatchAfterPendingOpens(ev);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::DoomFileByKeyInternal(const SHA1Sum::Hash* aHash) {
  LOG((
      "CacheFileIOManager::DoomFileByKeyInternal() [hash=%08x%08x%08x%08x%08x]",
      LOGSHA1(aHash)));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  nsresult rv;

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!mCacheDirectory) {
    return NS_ERROR_FILE_INVALID_PATH;
  }

  // Find active handle
  RefPtr<CacheFileHandle> handle;
  mHandles.GetHandle(aHash, getter_AddRefs(handle));

  if (handle) {
    handle->Log();

    return DoomFileInternal(handle);
  }

  CacheIOThread::Cancelable cancelable(true);

  // There is no handle for this file, delete the file if exists
  nsCOMPtr<nsIFile> file;
  rv = GetFile(aHash, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists;
  rv = file->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  LOG(
      ("CacheFileIOManager::DoomFileByKeyInternal() - Removing file from "
       "disk"));
  rv = file->Remove(false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Cannot remove old entry from the disk");
    LOG(
        ("CacheFileIOManager::DoomFileByKeyInternal() - Removing file failed. "
         "[rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
  }

  // Find the key for the hash
  // Read metadata from the file synchronously
  RefPtr<CacheFileMetadata> metadata = new CacheFileMetadata();
  rv = metadata->SyncReadMetadata(file);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CacheIndex::RemoveEntry(aHash, ""_ns);
  } else {
    CacheIndex::RemoveEntry(aHash, metadata->GetKey());
  }

  return NS_OK;
}

// static
nsresult CacheFileIOManager::ReleaseNSPRHandle(CacheFileHandle* aHandle) {
  LOG(("CacheFileIOManager::ReleaseNSPRHandle() [handle=%p]", aHandle));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<ReleaseNSPRHandleEvent> ev = new ReleaseNSPRHandleEvent(aHandle);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                          ? CacheIOThread::WRITE_PRIORITY
                                          : CacheIOThread::WRITE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::MaybeReleaseNSPRHandleInternal(
    CacheFileHandle* aHandle, bool aIgnoreShutdownLag) {
  LOG(
      ("CacheFileIOManager::MaybeReleaseNSPRHandleInternal() [handle=%p, "
       "ignore shutdown=%d]",
       aHandle, aIgnoreShutdownLag));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());

  if (aHandle->mFD) {
    DebugOnly<bool> found{};
    found = mHandlesByLastUsed.RemoveElement(aHandle);
    MOZ_ASSERT(found);
  }

  PRFileDesc* fd = aHandle->mFD;
  aHandle->mFD = nullptr;

  // Leak invalid (w/o metadata) and doomed handles immediately after shutdown.
  // Leak other handles when past the shutdown time maximum lag.
  if (
#ifndef DEBUG
      ((aHandle->mInvalid || aHandle->mIsDoomed) &&
       MOZ_UNLIKELY(CacheObserver::ShuttingDown())) ||
#endif
      MOZ_UNLIKELY(!aIgnoreShutdownLag &&
                   CacheObserver::IsPastShutdownIOLag())) {
    // Don't bother closing this file.  Return a failure code from here will
    // cause any following IO operation on the file (mainly removal) to be
    // bypassed, which is what we want.
    // For mInvalid == true the entry will never be used, since it doesn't
    // have correct metadata, thus we don't need to worry about removing it.
    // For mIsDoomed == true the file is already in the doomed sub-dir and
    // will be removed on next session start.
    LOG(("  past the shutdown I/O lag, leaking file handle"));
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!fd) {
    // The filedesc has already been closed before, just let go.
    return NS_OK;
  }

  bool cancelable = !aHandle->IsSpecialFile();
#if defined(MOZ_CACHE_ASYNC_IO)
  if (aHandle->mAsyncRunning) {
    // Need to wait for async operation to finish before closing the
    // file descriptor.
    RefPtr<nsIRunnable> closeFunc = NS_NewRunnableFunction(
        "CacheFileIOManager::MaybeReleaseNSPRHandleInternal",
        [fd, cancelable]() {
          CacheIOThread::Cancelable _cancelable(cancelable);

          PR_Close(fd);
        });

    if (aHandle->WaitForAsyncCompletion(closeFunc, CacheIOThread::OPEN)) {
      return NS_OK;
    }
  }
#endif

  CacheIOThread::Cancelable _cancelable(cancelable);

  PRStatus status = PR_Close(fd);
  if (status != PR_SUCCESS) {
    LOG(
        ("CacheFileIOManager::MaybeReleaseNSPRHandleInternal() "
         "failed to close [handle=%p, status=%u]",
         aHandle, status));
    return NS_ERROR_FAILURE;
  }

  LOG(("CacheFileIOManager::MaybeReleaseNSPRHandleInternal() DONE"));

  return NS_OK;
}

// static
nsresult CacheFileIOManager::TruncateSeekSetEOF(
    CacheFileHandle* aHandle, int64_t aTruncatePos, int64_t aEOFPos,
    CacheFileIOListener* aCallback) {
  LOG(
      ("CacheFileIOManager::TruncateSeekSetEOF() [handle=%p, "
       "truncatePos=%" PRId64 ", "
       "EOFPos=%" PRId64 ", listener=%p]",
       aHandle, aTruncatePos, aEOFPos, aCallback));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || (aCallback && aCallback->IsKilled()) || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<TruncateSeekSetEOFEvent> ev =
      new TruncateSeekSetEOFEvent(aHandle, aTruncatePos, aEOFPos, aCallback);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                          ? CacheIOThread::WRITE_PRIORITY
                                          : CacheIOThread::WRITE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// static
void CacheFileIOManager::GetCacheDirectory(nsIFile** result) {
  *result = nullptr;

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (!ioMan || !ioMan->mCacheDirectory) {
    return;
  }

  ioMan->mCacheDirectory->Clone(result);
}

#if defined(MOZ_WIDGET_ANDROID)

// static
void CacheFileIOManager::GetProfilelessCacheDirectory(nsIFile** result) {
  *result = nullptr;

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (!ioMan || !ioMan->mCacheProfilelessDirectory) {
    return;
  }

  ioMan->mCacheProfilelessDirectory->Clone(result);
}

#endif

// static
nsresult CacheFileIOManager::GetEntryInfo(
    const SHA1Sum::Hash* aHash,
    CacheStorageService::EntryInfoCallback* aCallback) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThread());

  nsresult rv;

  RefPtr<CacheFileIOManager> ioMan = gInstance;
  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsAutoCString enhanceId;
  nsAutoCString uriSpec;

  RefPtr<CacheFileHandle> handle;
  ioMan->mHandles.GetHandle(aHash, getter_AddRefs(handle));
  if (handle) {
    RefPtr<nsILoadContextInfo> info =
        CacheFileUtils::ParseKey(handle->Key(), &enhanceId, &uriSpec);

    MOZ_ASSERT(info);
    if (!info) {
      return NS_OK;  // ignore
    }

    RefPtr<CacheStorageService> service = CacheStorageService::Self();
    if (!service) {
      return NS_ERROR_NOT_INITIALIZED;
    }

    // Invokes OnCacheEntryInfo when an existing entry is found
    if (service->GetCacheEntryInfo(info, enhanceId, uriSpec, aCallback)) {
      return NS_OK;
    }

    // When we are here, there is no existing entry and we need
    // to synchrnously load metadata from a disk file.
  }

  // Locate the actual file
  nsCOMPtr<nsIFile> file;
  ioMan->GetFile(aHash, getter_AddRefs(file));

  // Read metadata from the file synchronously
  RefPtr<CacheFileMetadata> metadata = new CacheFileMetadata();
  rv = metadata->SyncReadMetadata(file);
  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  // Now get the context + enhance id + URL from the key.
  RefPtr<nsILoadContextInfo> info =
      CacheFileUtils::ParseKey(metadata->GetKey(), &enhanceId, &uriSpec);
  MOZ_ASSERT(info);
  if (!info) {
    return NS_OK;
  }

  // Pick all data to pass to the callback.
  int64_t dataSize = metadata->Offset();
  int64_t altDataSize = 0;
  uint32_t fetchCount = metadata->GetFetchCount();
  uint32_t expirationTime = metadata->GetExpirationTime();
  uint32_t lastModified = metadata->GetLastModified();

  const char* altDataElement =
      metadata->GetElement(CacheFileUtils::kAltDataKey);
  if (altDataElement) {
    int64_t altDataOffset = std::numeric_limits<int64_t>::max();
    if (NS_SUCCEEDED(CacheFileUtils::ParseAlternativeDataInfo(
            altDataElement, &altDataOffset, nullptr)) &&
        altDataOffset < dataSize) {
      dataSize = altDataOffset;
      altDataSize = metadata->Offset() - altDataOffset;
    } else {
      LOG(("CacheFileIOManager::GetEntryInfo() invalid alternative data info"));
      return NS_OK;
    }
  }

  // Call directly on the callback.
  aCallback->OnEntryInfo(uriSpec, enhanceId, dataSize, altDataSize, fetchCount,
                         lastModified, expirationTime, metadata->Pinned(),
                         info);

  return NS_OK;
}

nsresult CacheFileIOManager::TruncateSeekSetEOFInternal(
    CacheFileHandle* aHandle, int64_t aTruncatePos, int64_t aEOFPos) {
  LOG(
      ("CacheFileIOManager::TruncateSeekSetEOFInternal() [handle=%p, "
       "truncatePos=%" PRId64 ", EOFPos=%" PRId64 "]",
       aHandle, aTruncatePos, aEOFPos));

  nsresult rv;

  if (aHandle->mKilled) {
    LOG(("  handle already killed, file not truncated"));
    return NS_OK;
  }

  if (CacheObserver::ShuttingDown() && !aHandle->mFD) {
    aHandle->mKilled = true;
    LOG(("  killing the handle, file not truncated"));
    return NS_OK;
  }

  CacheIOThread::Cancelable cancelable(!aHandle->IsSpecialFile());

  if (!aHandle->mFileExists) {
    rv = CreateFile(aHandle);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!aHandle->mFD) {
    rv = OpenNSPRHandle(aHandle);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NSPRHandleUsed(aHandle);
  }

  // Check again, OpenNSPRHandle could figure out the file was gone.
  if (!aHandle->mFileExists) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // When this operation would increase cache size, check whether the cache size
  // reached the hard limit and whether it would cause critical low disk space.
  if (aHandle->mFileSize < aEOFPos) {
    if (mOverLimitEvicting && mCacheSizeOnHardLimit) {
      LOG(
          ("CacheFileIOManager::TruncateSeekSetEOFInternal() - failing because "
           "cache size reached hard limit!"));
      return NS_ERROR_FILE_NO_DEVICE_SPACE;
    }

    int64_t freeSpace;
    rv = mCacheDirectory->GetDiskSpaceAvailable(&freeSpace);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      freeSpace = -1;
      LOG(
          ("CacheFileIOManager::TruncateSeekSetEOFInternal() - "
           "GetDiskSpaceAvailable() failed! [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
    } else {
      freeSpace >>= 10;  // bytes to kilobytes
      uint32_t limit = CacheObserver::DiskFreeSpaceHardLimit();
      if (freeSpace - aEOFPos + aHandle->mFileSize < limit) {
        LOG(
            ("CacheFileIOManager::TruncateSeekSetEOFInternal() - Low free space"
             ", refusing to write! [freeSpace=%" PRId64 "kB, limit=%ukB]",
             freeSpace, limit));
        return NS_ERROR_FILE_NO_DEVICE_SPACE;
      }
    }
  }

  // This operation always invalidates the entry
  aHandle->mInvalid = true;

  rv = TruncFile(aHandle->mFD, aTruncatePos);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aTruncatePos != aEOFPos) {
    rv = TruncFile(aHandle->mFD, aEOFPos);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  uint32_t oldSizeInK = aHandle->FileSizeInK();
  aHandle->mFileSize = aEOFPos;
  uint32_t newSizeInK = aHandle->FileSizeInK();

  if (oldSizeInK != newSizeInK && !aHandle->IsDoomed() &&
      !aHandle->IsSpecialFile()) {
    CacheIndex::UpdateEntry(aHandle->Hash(), nullptr, nullptr, nullptr, nullptr,
                            nullptr, &newSizeInK);

    if (oldSizeInK < newSizeInK) {
      EvictIfOverLimitInternal();
    }
  }

  return NS_OK;
}

// static
nsresult CacheFileIOManager::RenameFile(CacheFileHandle* aHandle,
                                        const nsACString& aNewName,
                                        CacheFileIOListener* aCallback) {
  LOG(("CacheFileIOManager::RenameFile() [handle=%p, newName=%s, listener=%p]",
       aHandle, PromiseFlatCString(aNewName).get(), aCallback));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!aHandle->IsSpecialFile()) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<RenameFileEvent> ev =
      new RenameFileEvent(aHandle, aNewName, aCallback);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                          ? CacheIOThread::WRITE_PRIORITY
                                          : CacheIOThread::WRITE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::RenameFileInternal(CacheFileHandle* aHandle,
                                                const nsACString& aNewName) {
  LOG(("CacheFileIOManager::RenameFileInternal() [handle=%p, newName=%s]",
       aHandle, PromiseFlatCString(aNewName).get()));

  nsresult rv;

  MOZ_ASSERT(aHandle->IsSpecialFile());

  if (aHandle->IsDoomed()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Doom old handle if it exists and is not doomed
  for (uint32_t i = 0; i < mSpecialHandles.Length(); i++) {
    if (!mSpecialHandles[i]->IsDoomed() &&
        mSpecialHandles[i]->Key() == aNewName) {
      MOZ_ASSERT(aHandle != mSpecialHandles[i]);
      rv = DoomFileInternal(mSpecialHandles[i]);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    }
  }

  nsCOMPtr<nsIFile> file;
  rv = GetSpecialFile(aNewName, getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists;
  rv = file->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (exists) {
    LOG(
        ("CacheFileIOManager::RenameFileInternal() - Removing old file from "
         "disk"));
    rv = file->Remove(false);
    if (NS_FAILED(rv)) {
      NS_WARNING("Cannot remove file from the disk");
      LOG(
          ("CacheFileIOManager::RenameFileInternal() - Removing old file failed"
           ". [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
    }
  }

  if (!aHandle->FileExists()) {
    aHandle->mKey = aNewName;
    return NS_OK;
  }

  rv = MaybeReleaseNSPRHandleInternal(aHandle, true);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aHandle->mFile->MoveToNative(nullptr, aNewName);
  NS_ENSURE_SUCCESS(rv, rv);

  aHandle->mKey = aNewName;
  return NS_OK;
}

// static
nsresult CacheFileIOManager::EvictIfOverLimit() {
  LOG(("CacheFileIOManager::EvictIfOverLimit()"));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsIRunnable> ev;
  ev = NewRunnableMethod("net::CacheFileIOManager::EvictIfOverLimitInternal",
                         ioMan, &CacheFileIOManager::EvictIfOverLimitInternal);

  rv = ioMan->mIOThread->Dispatch(ev, CacheIOThread::EVICT);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::EvictIfOverLimitInternal() {
  LOG(("CacheFileIOManager::EvictIfOverLimitInternal()"));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mOverLimitEvicting) {
    LOG(
        ("CacheFileIOManager::EvictIfOverLimitInternal() - Eviction already "
         "running."));
    return NS_OK;
  }

  CacheIOThread::Cancelable cancelable(true);

  int64_t freeSpace;
  rv = mCacheDirectory->GetDiskSpaceAvailable(&freeSpace);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    freeSpace = -1;

    // Do not change smart size.
    LOG(
        ("CacheFileIOManager::EvictIfOverLimitInternal() - "
         "GetDiskSpaceAvailable() failed! [rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
  } else {
    freeSpace >>= 10;  // bytes to kilobytes
    UpdateSmartCacheSize(freeSpace);
  }

  uint32_t cacheUsage;
  rv = CacheIndex::GetCacheSize(&cacheUsage);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t cacheLimit = CacheObserver::DiskCacheCapacity();
  uint32_t freeSpaceLimit = CacheObserver::DiskFreeSpaceSoftLimit();

  if (cacheUsage <= cacheLimit &&
      (freeSpace == -1 || freeSpace >= freeSpaceLimit)) {
    LOG(
        ("CacheFileIOManager::EvictIfOverLimitInternal() - Cache size and free "
         "space in limits. [cacheSize=%ukB, cacheSizeLimit=%ukB, "
         "freeSpace=%" PRId64 "kB, freeSpaceLimit=%ukB]",
         cacheUsage, cacheLimit, freeSpace, freeSpaceLimit));
    return NS_OK;
  }

  LOG(
      ("CacheFileIOManager::EvictIfOverLimitInternal() - Cache size exceeded "
       "limit. Starting overlimit eviction. [cacheSize=%ukB, limit=%ukB]",
       cacheUsage, cacheLimit));

  nsCOMPtr<nsIRunnable> ev;
  ev = NewRunnableMethod("net::CacheFileIOManager::OverLimitEvictionInternal",
                         this, &CacheFileIOManager::OverLimitEvictionInternal);

  rv = mIOThread->Dispatch(ev, CacheIOThread::EVICT);
  NS_ENSURE_SUCCESS(rv, rv);

  mOverLimitEvicting = true;
  return NS_OK;
}

nsresult CacheFileIOManager::OverLimitEvictionInternal() {
  LOG(("CacheFileIOManager::OverLimitEvictionInternal()"));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  // mOverLimitEvicting is accessed only on IO thread, so we can set it to false
  // here and set it to true again once we dispatch another event that will
  // continue with the eviction. The reason why we do so is that we can fail
  // early anywhere in this method and the variable will contain a correct
  // value. Otherwise we would need to set it to false on every failing place.
  mOverLimitEvicting = false;

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  auto frecencySnapshot = CacheIndex::GetSortedSnapshotForEviction();
  while (true) {
    int64_t freeSpace;
    rv = mCacheDirectory->GetDiskSpaceAvailable(&freeSpace);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      freeSpace = -1;

      // Do not change smart size.
      LOG(
          ("CacheFileIOManager::EvictIfOverLimitInternal() - "
           "GetDiskSpaceAvailable() failed! [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
    } else {
      freeSpace >>= 10;  // bytes to kilobytes
      UpdateSmartCacheSize(freeSpace);
    }

    uint32_t cacheUsage;
    rv = CacheIndex::GetCacheSize(&cacheUsage);
    NS_ENSURE_SUCCESS(rv, rv);

    uint32_t cacheLimit = CacheObserver::DiskCacheCapacity();
    uint32_t freeSpaceLimit = CacheObserver::DiskFreeSpaceSoftLimit();

    if (cacheUsage > cacheLimit) {
      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - Cache size over "
           "limit. [cacheSize=%ukB, limit=%ukB]",
           cacheUsage, cacheLimit));

      // We allow cache size to go over the specified limit. Eviction should
      // keep the size within the limit in a long run, but in case the eviction
      // is too slow, the cache could go way over the limit. To prevent this we
      // set flag mCacheSizeOnHardLimit when the size reaches 105% of the limit
      // and WriteInternal() and TruncateSeekSetEOFInternal() fail to cache
      // additional data.
      if ((cacheUsage - cacheLimit) > (cacheLimit / 20)) {
        LOG(
            ("CacheFileIOManager::OverLimitEvictionInternal() - Cache size "
             "reached hard limit."));
        mCacheSizeOnHardLimit = true;
      } else {
        mCacheSizeOnHardLimit = false;
      }
    } else if (freeSpace != -1 && freeSpace < freeSpaceLimit) {
      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - Free space under "
           "limit. [freeSpace=%" PRId64 "kB, freeSpaceLimit=%ukB]",
           freeSpace, freeSpaceLimit));
    } else {
      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - Cache size and "
           "free space in limits. [cacheSize=%ukB, cacheSizeLimit=%ukB, "
           "freeSpace=%" PRId64 "kB, freeSpaceLimit=%ukB]",
           cacheUsage, cacheLimit, freeSpace, freeSpaceLimit));

      mCacheSizeOnHardLimit = false;
      return NS_OK;
    }

    if (CacheIOThread::YieldAndRerun()) {
      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - Breaking loop "
           "for higher level events."));
      mOverLimitEvicting = true;
      return NS_OK;
    }

    SHA1Sum::Hash hash;
    uint32_t cnt = 0;
    static uint32_t consecutiveFailures = 0;
    rv = CacheIndex::GetEntryForEviction(frecencySnapshot, false, &hash, &cnt);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = DoomFileByKeyInternal(&hash);
    if (NS_SUCCEEDED(rv)) {
      consecutiveFailures = 0;
    } else if (rv == NS_ERROR_NOT_AVAILABLE) {
      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - "
           "DoomFileByKeyInternal() failed. [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
      // TODO index is outdated, start update

      // Make sure index won't return the same entry again
      // XXX find the key for the hash
      CacheIndex::RemoveEntry(&hash, ""_ns);
      consecutiveFailures = 0;
    } else {
      // This shouldn't normally happen, but the eviction must not fail
      // completely if we ever encounter this problem.
      NS_WARNING(
          "CacheFileIOManager::OverLimitEvictionInternal() - Unexpected "
          "failure of DoomFileByKeyInternal()");

      LOG(
          ("CacheFileIOManager::OverLimitEvictionInternal() - "
           "DoomFileByKeyInternal() failed. [rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));

      if (++consecutiveFailures >= cnt) {
        // This doesn't necessarily mean that we've tried to doom every entry
        // but we've reached a sane number of tries. It is likely that another
        // eviction will start soon. And as said earlier, this normally doesn't
        // happen at all.
        return NS_OK;
      }
    }
  }

  MOZ_ASSERT_UNREACHABLE("We should never get here");
  return NS_OK;
}

// static
nsresult CacheFileIOManager::EvictAll() {
  LOG(("CacheFileIOManager::EvictAll()"));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsIRunnable> ev;
  ev = NewRunnableMethod("net::CacheFileIOManager::EvictAllInternal", ioMan,
                         &CacheFileIOManager::EvictAllInternal);

  rv = ioMan->mIOThread->DispatchAfterPendingOpens(ev);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

namespace {

class EvictionNotifierRunnable : public Runnable {
 public:
  EvictionNotifierRunnable() : Runnable("EvictionNotifierRunnable") {}
  NS_DECL_NSIRUNNABLE
};

NS_IMETHODIMP
EvictionNotifierRunnable::Run() {
  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  if (obsSvc) {
    obsSvc->NotifyObservers(nullptr, "cacheservice:empty-cache", nullptr);
  }
  return NS_OK;
}

}  // namespace

nsresult CacheFileIOManager::EvictAllInternal() {
  LOG(("CacheFileIOManager::EvictAllInternal()"));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  RefPtr<EvictionNotifierRunnable> r = new EvictionNotifierRunnable();

  if (!mCacheDirectory) {
    // This is a kind of hack. Somebody called EvictAll() without a profile.
    // This happens in xpcshell tests that use cache without profile. We need
    // to notify observers in this case since the tests are waiting for it.
    NS_DispatchToMainThread(r);
    return NS_ERROR_FILE_INVALID_PATH;
  }

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!mTreeCreated) {
    rv = CreateCacheTree();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Doom all active handles
  nsTArray<RefPtr<CacheFileHandle>> handles;
  mHandles.GetActiveHandles(&handles);

  for (uint32_t i = 0; i < handles.Length(); ++i) {
    rv = DoomFileInternal(handles[i]);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(
          ("CacheFileIOManager::EvictAllInternal() - Cannot doom handle "
           "[handle=%p]",
           handles[i].get()));
    }
  }

  nsCOMPtr<nsIFile> file;
  rv = mCacheDirectory->Clone(getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = file->AppendNative(nsLiteralCString(ENTRIES_DIR));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Trash current entries directory
  rv = TrashDirectory(file);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Files are now inaccessible in entries directory, notify observers.
  NS_DispatchToMainThread(r);

  // Create a new empty entries directory
  rv = CheckAndCreateDir(mCacheDirectory, ENTRIES_DIR, false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  CacheIndex::RemoveAll();

  return NS_OK;
}

// static
nsresult CacheFileIOManager::EvictByContext(
    nsILoadContextInfo* aLoadContextInfo, bool aPinned,
    const nsAString& aOrigin, const nsAString& aBaseDomain) {
  LOG(("CacheFileIOManager::EvictByContext() [loadContextInfo=%p]",
       aLoadContextInfo));

  // XXX evict dictionary data from memory cache
  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsIRunnable> ev;
  ev =
      NewRunnableMethod<nsCOMPtr<nsILoadContextInfo>, bool, nsString, nsString>(
          "net::CacheFileIOManager::EvictByContextInternal", ioMan,
          &CacheFileIOManager::EvictByContextInternal, aLoadContextInfo,
          aPinned, aOrigin, aBaseDomain);

  rv = ioMan->mIOThread->DispatchAfterPendingOpens(ev);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  // Clear the entries from the Index immediately, to comply with
  // https://www.w3.org/TR/clear-site-data/#fetch-integration
  // aBaseDomain isn't needed for Clear-Site-Data, but is for
  // ClearBaseDomain.  This can also make CacheStorageService::Clear() and
  // ClearBaseDomain() be synchronous.
  // Note that we will effectively hide the entries until the actual evict
  // happens.
  CacheIndex::EvictByContext(aOrigin, aBaseDomain);

  return NS_OK;
}

nsresult CacheFileIOManager::EvictByContextInternal(
    nsILoadContextInfo* aLoadContextInfo, bool aPinned,
    const nsAString& aOrigin, const nsAString& aBaseDomain) {
  LOG(
      ("CacheFileIOManager::EvictByContextInternal() [loadContextInfo=%p, "
       "pinned=%d]",
       aLoadContextInfo, aPinned));

  nsresult rv;

  if (aLoadContextInfo) {
    nsAutoCString suffix;
    aLoadContextInfo->OriginAttributesPtr()->CreateSuffix(suffix);
    LOG(("  anonymous=%u, suffix=%s]", aLoadContextInfo->IsAnonymous(),
         suffix.get()));

    MOZ_ASSERT(mIOThread->IsCurrentThread());

    MOZ_ASSERT(!aLoadContextInfo->IsPrivate());
    if (aLoadContextInfo->IsPrivate()) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  if (!mCacheDirectory) {
    // This is a kind of hack. Somebody called EvictAll() without a profile.
    // This happens in xpcshell tests that use cache without profile. We need
    // to notify observers in this case since the tests are waiting for it.
    // Also notify for aPinned == true, those are interested as well.

    // XXX This doesn't actually clear anything in this case (is there anything
    // to clear?)
    if (!aLoadContextInfo) {
      RefPtr<EvictionNotifierRunnable> r = new EvictionNotifierRunnable();
      NS_DispatchToMainThread(r);
    }
    return NS_ERROR_FILE_INVALID_PATH;
  }

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!mTreeCreated) {
    rv = CreateCacheTree();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  NS_ConvertUTF16toUTF8 origin(aOrigin);
  NS_ConvertUTF16toUTF8 baseDomain(aBaseDomain);

  // Doom all active handles that matches the load context
  // NOTE: Dictionaries have already been cleared synchronously,
  // so there's no need to re-clear them (which might cause
  // problems if they were re-created in to interim).
  nsTArray<RefPtr<CacheFileHandle>> handles;
  mHandles.GetActiveHandles(&handles);

  for (uint32_t i = 0; i < handles.Length(); ++i) {
    CacheFileHandle* handle = handles[i];

    const bool shouldRemove = [&] {
      nsAutoCString uriSpec;
      RefPtr<nsILoadContextInfo> info =
          CacheFileUtils::ParseKey(handle->Key(), nullptr, &uriSpec);
      if (!info) {
        LOG(
            ("CacheFileIOManager::EvictByContextInternal() - Cannot parse key "
             "in "
             "handle! [handle=%p, key=%s]",
             handle, handle->Key().get()));
        MOZ_CRASH("Unexpected error!");
      }

      // Filter by base domain.
      if (!aBaseDomain.IsEmpty()) {
        if (StoragePrincipalHelper::PartitionKeyHasBaseDomain(
                info->OriginAttributesPtr()->mPartitionKey, aBaseDomain)) {
          return true;
        }

        // If the partitionKey does not match, check the entry URI next.

        // Get host portion of uriSpec.
        nsCOMPtr<nsIURI> uri;
        rv = NS_NewURI(getter_AddRefs(uri), uriSpec);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return false;
        }
        nsAutoCString host;
        rv = uri->GetHost(host);
        // Some entries may not have valid hosts. We can skip them.
        if (NS_FAILED(rv) || host.IsEmpty()) {
          return false;
        }

        // Clear entry if the host belongs to the given base domain.
        bool hasRootDomain = false;
        rv = HasRootDomain(host, baseDomain, &hasRootDomain);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return false;
        }

        return hasRootDomain;
      }

      // Filter by LoadContextInfo.
      if (aLoadContextInfo && !info->Equals(aLoadContextInfo)) {
        return false;
      }

      // Filter by origin.
      if (!origin.IsEmpty()) {  // XXX also look for dict:<origin>, or let that
                                // be handled by Doom?  Probably Doom
        RefPtr<MozURL> url;
        rv = MozURL::Init(getter_AddRefs(url), uriSpec);
        if (NS_FAILED(rv)) {
          return false;
        }

        nsAutoCString urlOrigin;
        url->Origin(urlOrigin);

        if (!urlOrigin.Equals(origin)) {
          return false;
        }
      }
      return true;
    }();

    if (!shouldRemove) {
      continue;
    }

    // handle will be doomed only when pinning status is known and equal or
    // doom decision will be deferred until pinning status is determined.
    rv = DoomFileInternal(handle,
                          aPinned ? CacheFileIOManager::DOOM_WHEN_PINNED
                                  : CacheFileIOManager::DOOM_WHEN_NON_PINNED,
                          false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(
          ("CacheFileIOManager::EvictByContextInternal() - Cannot doom handle"
           " [handle=%p]",
           handle));
    }
  }

  if (!aLoadContextInfo) {
    RefPtr<EvictionNotifierRunnable> r = new EvictionNotifierRunnable();
    NS_DispatchToMainThread(r);
  }

  if (!mContextEvictor) {
    mContextEvictor = new CacheFileContextEvictor();
    mContextEvictor->Init(mCacheDirectory);
  }

  mContextEvictor->AddContext(aLoadContextInfo, aPinned, aOrigin, aBaseDomain);

  return NS_OK;
}

// static
nsresult CacheFileIOManager::CacheIndexStateChanged() {
  LOG(("CacheFileIOManager::CacheIndexStateChanged()"));

  nsresult rv;

  // CacheFileIOManager lives longer than CacheIndex so gInstance must be
  // non-null here.
  MOZ_ASSERT(gInstance);

  // We have to re-dispatch even if we are on IO thread to prevent reentering
  // the lock in CacheIndex
  nsCOMPtr<nsIRunnable> ev = NewRunnableMethod(
      "net::CacheFileIOManager::CacheIndexStateChangedInternal",
      gInstance.get(), &CacheFileIOManager::CacheIndexStateChangedInternal);

  nsCOMPtr<nsIEventTarget> ioTarget = IOTarget();
  MOZ_ASSERT(ioTarget);

  rv = ioTarget->Dispatch(ev, nsIEventTarget::DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void CacheFileIOManager::CacheIndexStateChangedInternal() {
  if (mShuttingDown) {
    // ignore notification during shutdown
    return;
  }

  if (!mContextEvictor) {
    return;
  }

  mContextEvictor->CacheIndexStateChanged();
}

nsresult CacheFileIOManager::TrashDirectory(nsIFile* aFile) {
  LOG(("CacheFileIOManager::TrashDirectory() [file=%s]",
       aFile->HumanReadablePath().get()));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());
  MOZ_ASSERT(mCacheDirectory);

  // When the directory is empty, it is cheaper to remove it directly instead of
  // using the trash mechanism.
  bool isEmpty;
  rv = IsEmptyDirectory(aFile, &isEmpty);
  NS_ENSURE_SUCCESS(rv, rv);

  if (isEmpty) {
    rv = aFile->Remove(false);
    LOG(
        ("CacheFileIOManager::TrashDirectory() - Directory removed "
         "[rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
    return rv;
  }

#ifdef DEBUG
  nsCOMPtr<nsIFile> dirCheck;
  rv = aFile->GetParent(getter_AddRefs(dirCheck));
  NS_ENSURE_SUCCESS(rv, rv);

  bool equals = false;
  rv = dirCheck->Equals(mCacheDirectory, &equals);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(equals);
#endif

  nsCOMPtr<nsIFile> dir, trash;
  nsAutoCString leaf;

  rv = aFile->Clone(getter_AddRefs(dir));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aFile->Clone(getter_AddRefs(trash));
  NS_ENSURE_SUCCESS(rv, rv);

  const int32_t kMaxTries = 16;
  srand(static_cast<unsigned>(PR_Now()));
  for (int32_t triesCount = 0;; ++triesCount) {
    leaf = TRASH_DIR;
    leaf.AppendInt(rand());
    rv = trash->SetNativeLeafName(leaf);
    NS_ENSURE_SUCCESS(rv, rv);

    bool exists;
    if (NS_SUCCEEDED(trash->Exists(&exists)) && !exists) {
      break;
    }

    LOG(
        ("CacheFileIOManager::TrashDirectory() - Trash directory already "
         "exists [leaf=%s]",
         leaf.get()));

    if (triesCount == kMaxTries) {
      LOG(
          ("CacheFileIOManager::TrashDirectory() - Could not find unused trash "
           "directory in %d tries.",
           kMaxTries));
      return NS_ERROR_FAILURE;
    }
  }

  LOG(("CacheFileIOManager::TrashDirectory() - Renaming directory [leaf=%s]",
       leaf.get()));

  rv = dir->MoveToNative(nullptr, leaf);
  NS_ENSURE_SUCCESS(rv, rv);

  StartRemovingTrash();
  return NS_OK;
}

// static
void CacheFileIOManager::OnTrashTimer(nsITimer* aTimer, void* aClosure) {
  LOG(("CacheFileIOManager::OnTrashTimer() [timer=%p, closure=%p]", aTimer,
       aClosure));

  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (!ioMan) {
    return;
  }

  ioMan->mTrashTimer = nullptr;
  ioMan->StartRemovingTrash();
}

nsresult CacheFileIOManager::StartRemovingTrash() {
  LOG(("CacheFileIOManager::StartRemovingTrash()"));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!mCacheDirectory) {
    return NS_ERROR_FILE_INVALID_PATH;
  }

  if (mTrashTimer) {
    LOG(("CacheFileIOManager::StartRemovingTrash() - Trash timer exists."));
    return NS_OK;
  }

  if (mRemovingTrashDirs) {
    LOG(
        ("CacheFileIOManager::StartRemovingTrash() - Trash removing in "
         "progress."));
    return NS_OK;
  }

  uint32_t elapsed = (TimeStamp::NowLoRes() - mStartTime).ToMilliseconds();
  if (elapsed < kRemoveTrashStartDelay) {
    nsCOMPtr<nsIEventTarget> ioTarget = IOTarget();
    MOZ_ASSERT(ioTarget);

    return NS_NewTimerWithFuncCallback(
        getter_AddRefs(mTrashTimer), CacheFileIOManager::OnTrashTimer, nullptr,
        kRemoveTrashStartDelay - elapsed, nsITimer::TYPE_ONE_SHOT,
        "net::CacheFileIOManager::StartRemovingTrash"_ns, ioTarget);
  }

  nsCOMPtr<nsIRunnable> ev;
  ev = NewRunnableMethod("net::CacheFileIOManager::RemoveTrashInternal", this,
                         &CacheFileIOManager::RemoveTrashInternal);

  rv = mIOThread->Dispatch(ev, CacheIOThread::EVICT);
  NS_ENSURE_SUCCESS(rv, rv);

  mRemovingTrashDirs = true;
  return NS_OK;
}

nsresult CacheFileIOManager::RemoveTrashInternal() {
  LOG(("CacheFileIOManager::RemoveTrashInternal()"));

  nsresult rv;

  MOZ_ASSERT(mIOThread->IsCurrentThread());

  if (mShuttingDown) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  CacheIOThread::Cancelable cancelable(true);

  MOZ_ASSERT(!mTrashTimer);
  MOZ_ASSERT(mRemovingTrashDirs);

  if (!mTreeCreated) {
    rv = CreateCacheTree();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // mRemovingTrashDirs is accessed only on IO thread, so we can drop the flag
  // here and set it again once we dispatch a continuation event. By doing so,
  // we don't have to drop the flag on any possible early return.
  mRemovingTrashDirs = false;

  while (true) {
    if (CacheIOThread::YieldAndRerun()) {
      LOG(
          ("CacheFileIOManager::RemoveTrashInternal() - Breaking loop for "
           "higher level events."));
      mRemovingTrashDirs = true;
      return NS_OK;
    }

    // Find some trash directory
    if (!mTrashDir) {
      MOZ_ASSERT(!mTrashDirEnumerator);

      rv = FindTrashDirToRemove();
      if (rv == NS_ERROR_NOT_AVAILABLE) {
        LOG(
            ("CacheFileIOManager::RemoveTrashInternal() - No trash directory "
             "found."));
        return NS_OK;
      }
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mTrashDir->GetDirectoryEntries(getter_AddRefs(mTrashDirEnumerator));
      NS_ENSURE_SUCCESS(rv, rv);

      continue;  // check elapsed time
    }

    // We null out mTrashDirEnumerator once we remove all files in the
    // directory, so remove the trash directory if we don't have enumerator.
    if (!mTrashDirEnumerator) {
      rv = mTrashDir->Remove(false);
      if (NS_FAILED(rv)) {
        // There is no reason why removing an empty directory should fail, but
        // if it does, we should continue and try to remove all other trash
        // directories.
        nsAutoCString leafName;
        mTrashDir->GetNativeLeafName(leafName);
        mFailedTrashDirs.AppendElement(leafName);
        LOG(
            ("CacheFileIOManager::RemoveTrashInternal() - Cannot remove "
             "trashdir. [name=%s]",
             leafName.get()));
      }

      mTrashDir = nullptr;
      continue;  // check elapsed time
    }

    nsCOMPtr<nsIFile> file;
    rv = mTrashDirEnumerator->GetNextFile(getter_AddRefs(file));
    if (!file) {
      mTrashDirEnumerator->Close();
      mTrashDirEnumerator = nullptr;
      continue;  // check elapsed time
    }
    bool isDir = false;
    file->IsDirectory(&isDir);
    if (isDir) {
      NS_WARNING(
          "Found a directory in a trash directory! It will be removed "
          "recursively, but this can block IO thread for a while!");
      if (LOG_ENABLED()) {
        LOG(
            ("CacheFileIOManager::RemoveTrashInternal() - Found a directory in "
             "a trash "
             "directory! It will be removed recursively, but this can block IO "
             "thread for a while! [file=%s]",
             file->HumanReadablePath().get()));
      }
    }
    file->Remove(isDir);
  }

  MOZ_ASSERT_UNREACHABLE("We should never get here");
  return NS_OK;
}

nsresult CacheFileIOManager::FindTrashDirToRemove() {
  LOG(("CacheFileIOManager::FindTrashDirToRemove()"));

  nsresult rv;

  if (!mCacheDirectory) {
    return NS_ERROR_UNEXPECTED;
  }

  // We call this method on the main thread during shutdown when user wants to
  // remove all cache files.
  MOZ_ASSERT(mIOThread->IsCurrentThread() || mShuttingDown);

  nsCOMPtr<nsIDirectoryEnumerator> iter;
  rv = mCacheDirectory->GetDirectoryEntries(getter_AddRefs(iter));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> file;
  while (NS_SUCCEEDED(iter->GetNextFile(getter_AddRefs(file))) && file) {
    bool isDir = false;
    file->IsDirectory(&isDir);
    if (!isDir) {
      continue;
    }

    nsAutoCString leafName;
    rv = file->GetNativeLeafName(leafName);
    if (NS_FAILED(rv)) {
      continue;
    }

    if (leafName.Length() < strlen(TRASH_DIR)) {
      continue;
    }

    if (!StringBeginsWith(leafName, nsLiteralCString(TRASH_DIR))) {
      continue;
    }

    if (mFailedTrashDirs.Contains(leafName)) {
      continue;
    }

    LOG(("CacheFileIOManager::FindTrashDirToRemove() - Returning directory %s",
         leafName.get()));

    mTrashDir = std::move(file);
    return NS_OK;
  }

  // When we're here we've tried to delete all trash directories. Clear
  // mFailedTrashDirs so we will try to delete them again when we start removing
  // trash directories next time.
  mFailedTrashDirs.Clear();
  return NS_ERROR_NOT_AVAILABLE;
}

// static
nsresult CacheFileIOManager::InitIndexEntry(CacheFileHandle* aHandle,
                                            OriginAttrsHash aOriginAttrsHash,
                                            bool aAnonymous, bool aPinning) {
  LOG(
      ("CacheFileIOManager::InitIndexEntry() [handle=%p, "
       "originAttrsHash=%" PRIx64 ", "
       "anonymous=%d, pinning=%d]",
       aHandle, aOriginAttrsHash, aAnonymous, aPinning));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aHandle->IsSpecialFile()) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<InitIndexEntryEvent> ev =
      new InitIndexEntryEvent(aHandle, aOriginAttrsHash, aAnonymous, aPinning);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                          ? CacheIOThread::WRITE_PRIORITY
                                          : CacheIOThread::WRITE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// static
nsresult CacheFileIOManager::UpdateIndexEntry(CacheFileHandle* aHandle,
                                              const uint32_t* aFrecency,
                                              const bool* aHasAltData,
                                              const uint32_t* aLastFetched,
                                              const uint32_t* aFetchCount,
                                              const uint8_t* aContentType) {
  LOG(
      ("CacheFileIOManager::UpdateIndexEntry() [handle=%p, frecency=%s, "
       "hasAltData=%s, lastFetched=%s, fetchCount=%s, contentType=%s]",
       aHandle, aFrecency ? nsPrintfCString("%u", *aFrecency).get() : "",
       aHasAltData ? (*aHasAltData ? "true" : "false") : "",
       aLastFetched ? nsPrintfCString("%u", *aLastFetched).get() : "",
       aFetchCount ? nsPrintfCString("%u", *aFetchCount).get() : "",
       aContentType ? nsPrintfCString("%u", *aContentType).get() : ""));

  nsresult rv;
  RefPtr<CacheFileIOManager> ioMan = gInstance;

  if (aHandle->IsClosed() || !ioMan) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aHandle->IsSpecialFile()) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<UpdateIndexEntryEvent> ev = new UpdateIndexEntryEvent(
      aHandle, aFrecency, aHasAltData, aLastFetched, aFetchCount, aContentType);
  rv = ioMan->mIOThread->Dispatch(ev, aHandle->mPriority
                                          ? CacheIOThread::WRITE_PRIORITY
                                          : CacheIOThread::WRITE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CacheFileIOManager::CreateFile(CacheFileHandle* aHandle) {
  MOZ_ASSERT(!aHandle->mFD);
  MOZ_ASSERT(aHandle->mFile);

  nsresult rv;

  if (aHandle->IsDoomed()) {
    nsCOMPtr<nsIFile> file;

    rv = GetDoomedFile(getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, rv);

    aHandle->mFile.swap(file);
  } else {
    bool exists;
    if (NS_SUCCEEDED(aHandle->mFile->Exists(&exists)) && exists) {
      NS_WARNING("Found a file that should not exist!");
    }
  }

  rv = OpenNSPRHandle(aHandle, true);
  NS_ENSURE_SUCCESS(rv, rv);

  aHandle->mFileSize = 0;
  return NS_OK;
}

// static
void CacheFileIOManager::HashToStr(const SHA1Sum::Hash* aHash,
                                   nsACString& _retval) {
  _retval.Truncate();
  const char hexChars[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  for (uint32_t i = 0; i < sizeof(SHA1Sum::Hash); i++) {
    _retval.Append(hexChars[(*aHash)[i] >> 4]);
    _retval.Append(hexChars[(*aHash)[i] & 0xF]);
  }
}

// static
nsresult CacheFileIOManager::StrToHash(const nsACString& aHash,
                                       SHA1Sum::Hash* _retval) {
  if (aHash.Length() != 2 * sizeof(SHA1Sum::Hash)) {
    return NS_ERROR_INVALID_ARG;
  }

  for (uint32_t i = 0; i < aHash.Length(); i++) {
    uint8_t value;

    if (aHash[i] >= '0' && aHash[i] <= '9') {
      value = aHash[i] - '0';
    } else if (aHash[i] >= 'A' && aHash[i] <= 'F') {
      value = aHash[i] - 'A' + 10;
    } else if (aHash[i] >= 'a' && aHash[i] <= 'f') {
      value = aHash[i] - 'a' + 10;
    } else {
      return NS_ERROR_INVALID_ARG;
    }

    if (i % 2 == 0) {
      (reinterpret_cast<uint8_t*>(_retval))[i / 2] = value << 4;
    } else {
      (reinterpret_cast<uint8_t*>(_retval))[i / 2] += value;
    }
  }

  return NS_OK;
}

nsresult CacheFileIOManager::GetFile(const SHA1Sum::Hash* aHash,
                                     nsIFile** _retval) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;
  rv = mCacheDirectory->Clone(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->AppendNative(nsLiteralCString(ENTRIES_DIR));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString leafName;
  HashToStr(aHash, leafName);

  rv = file->AppendNative(leafName);
  NS_ENSURE_SUCCESS(rv, rv);

  file.swap(*_retval);
  return NS_OK;
}

nsresult CacheFileIOManager::GetSpecialFile(const nsACString& aKey,
                                            nsIFile** _retval) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;
  rv = mCacheDirectory->Clone(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->AppendNative(aKey);
  NS_ENSURE_SUCCESS(rv, rv);

  file.swap(*_retval);
  return NS_OK;
}

nsresult CacheFileIOManager::GetDoomedFile(nsIFile** _retval) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;
  rv = mCacheDirectory->Clone(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->AppendNative(nsLiteralCString(DOOMED_DIR));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->AppendNative("dummyleaf"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  const int32_t kMaxTries = 64;
  srand(static_cast<unsigned>(PR_Now()));
  nsAutoCString leafName;
  for (int32_t triesCount = 0;; ++triesCount) {
    leafName.AppendInt(rand());
    rv = file->SetNativeLeafName(leafName);
    NS_ENSURE_SUCCESS(rv, rv);

    bool exists;
    if (NS_SUCCEEDED(file->Exists(&exists)) && !exists) {
      break;
    }

    if (triesCount == kMaxTries) {
      LOG(
          ("CacheFileIOManager::GetDoomedFile() - Could not find unused file "
           "name in %d tries.",
           kMaxTries));
      return NS_ERROR_FAILURE;
    }

    leafName.Truncate();
  }

  file.swap(*_retval);
  return NS_OK;
}

nsresult CacheFileIOManager::IsEmptyDirectory(nsIFile* aFile, bool* _retval) {
  MOZ_ASSERT(mIOThread->IsCurrentThread());

  nsresult rv;

  nsCOMPtr<nsIDirectoryEnumerator> enumerator;
  rv = aFile->GetDirectoryEntries(getter_AddRefs(enumerator));
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasMoreElements = false;
  rv = enumerator->HasMoreElements(&hasMoreElements);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = !hasMoreElements;
  return NS_OK;
}

nsresult CacheFileIOManager::CheckAndCreateDir(nsIFile* aFile, const char* aDir,
                                               bool aEnsureEmptyDir) {
  nsresult rv;

  nsCOMPtr<nsIFile> file;
  if (!aDir) {
    file = aFile;
  } else {
    nsAutoCString dir(aDir);
    rv = aFile->Clone(getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = file->AppendNative(dir);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  bool exists = false;
  rv = file->Exists(&exists);
  if (NS_SUCCEEDED(rv) && exists) {
    bool isDirectory = false;
    rv = file->IsDirectory(&isDirectory);
    if (NS_FAILED(rv) || !isDirectory) {
      // Try to remove the file
      rv = file->Remove(false);
      if (NS_SUCCEEDED(rv)) {
        exists = false;
      }
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aEnsureEmptyDir && NS_SUCCEEDED(rv) && exists) {
    bool isEmpty;
    rv = IsEmptyDirectory(file, &isEmpty);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!isEmpty) {
      // Don't check the result, if this fails, it's OK.  We do this
      // only for the doomed directory that doesn't need to be deleted
      // for the cost of completely disabling the whole browser.
      TrashDirectory(file);
    }
  }

  if (NS_SUCCEEDED(rv) && !exists) {
    rv = file->Create(nsIFile::DIRECTORY_TYPE, 0700);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("Cannot create directory");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult CacheFileIOManager::CreateCacheTree() {
  MOZ_ASSERT(mIOThread->IsCurrentThread());
  MOZ_ASSERT(!mTreeCreated);

  if (!mCacheDirectory || mTreeCreationFailed) {
    return NS_ERROR_FILE_INVALID_PATH;
  }

  nsresult rv;

  // Set the flag here and clear it again below when the tree is created
  // successfully.
  mTreeCreationFailed = true;

  // ensure parent directory exists
  nsCOMPtr<nsIFile> parentDir;
  rv = mCacheDirectory->GetParent(getter_AddRefs(parentDir));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = CheckAndCreateDir(parentDir, nullptr, false);
  NS_ENSURE_SUCCESS(rv, rv);

  // ensure cache directory exists
  rv = CheckAndCreateDir(mCacheDirectory, nullptr, false);
  NS_ENSURE_SUCCESS(rv, rv);

  // ensure entries directory exists
  rv = CheckAndCreateDir(mCacheDirectory, ENTRIES_DIR, false);
  NS_ENSURE_SUCCESS(rv, rv);

  // ensure doomed directory exists
  rv = CheckAndCreateDir(mCacheDirectory, DOOMED_DIR, true);
  NS_ENSURE_SUCCESS(rv, rv);

  mTreeCreated = true;
  mTreeCreationFailed = false;

  if (!mContextEvictor) {
    RefPtr<CacheFileContextEvictor> contextEvictor;
    contextEvictor = new CacheFileContextEvictor();

    // Init() method will try to load unfinished contexts from the disk. Store
    // the evictor as a member only when there is some unfinished job.
    contextEvictor->Init(mCacheDirectory);
    if (contextEvictor->ContextsCount()) {
      contextEvictor.swap(mContextEvictor);
    }
  }

  StartRemovingTrash();

  return NS_OK;
}

nsresult CacheFileIOManager::OpenNSPRHandle(CacheFileHandle* aHandle,
                                            bool aCreate) {
  LOG(("CacheFileIOManager::OpenNSPRHandle BEGIN, handle=%p", aHandle));

  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  MOZ_ASSERT(!aHandle->mFD);
  MOZ_ASSERT(mHandlesByLastUsed.IndexOf(aHandle) == mHandlesByLastUsed.NoIndex);
  MOZ_ASSERT(mHandlesByLastUsed.Length() <= kOpenHandlesLimit);
  MOZ_ASSERT((aCreate && !aHandle->mFileExists) ||
             (!aCreate && aHandle->mFileExists));

  nsresult rv;

  if (mHandlesByLastUsed.Length() == kOpenHandlesLimit) {
    // close handle that hasn't been used for the longest time
    rv = MaybeReleaseNSPRHandleInternal(mHandlesByLastUsed[0], true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aCreate) {
    rv = aHandle->mFile->OpenNSPRFileDesc(
        PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, 0600, &aHandle->mFD);
    if (rv == NS_ERROR_FILE_ALREADY_EXISTS ||   // error from nsLocalFileWin
        rv == NS_ERROR_FILE_NO_DEVICE_SPACE) {  // error from nsLocalFileUnix
      LOG(
          ("CacheFileIOManager::OpenNSPRHandle() - Cannot create a new file, we"
           " might reached a limit on FAT32. Will evict a single entry and try "
           "again. [hash=%08x%08x%08x%08x%08x]",
           LOGSHA1(aHandle->Hash())));

      SHA1Sum::Hash hash;
      uint32_t cnt;
      auto snapshot = CacheIndex::GetSortedSnapshotForEviction();
      rv = CacheIndex::GetEntryForEviction(snapshot, true, &hash, &cnt);
      if (NS_SUCCEEDED(rv)) {
        rv = DoomFileByKeyInternal(&hash);
      }
      if (NS_SUCCEEDED(rv)) {
        rv = aHandle->mFile->OpenNSPRFileDesc(
            PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, 0600, &aHandle->mFD);
        LOG(
            ("CacheFileIOManager::OpenNSPRHandle() - Successfully evicted entry"
             " with hash %08x%08x%08x%08x%08x. %s to create the new file.",
             LOGSHA1(&hash), NS_SUCCEEDED(rv) ? "Succeeded" : "Failed"));
      } else {
        LOG(
            ("CacheFileIOManager::OpenNSPRHandle() - Couldn't evict an existing"
             " entry."));
        rv = NS_ERROR_FILE_NO_DEVICE_SPACE;
      }
    }
    if (NS_FAILED(rv)) {
      LOG(
          ("CacheFileIOManager::OpenNSPRHandle() Create failed with "
           "0x%08" PRIx32,
           static_cast<uint32_t>(rv)));
    }
    NS_ENSURE_SUCCESS(rv, rv);

    aHandle->mFileExists = true;
  } else {
    rv = aHandle->mFile->OpenNSPRFileDesc(PR_RDWR, 0600, &aHandle->mFD);
    if (NS_ERROR_FILE_NOT_FOUND == rv) {
      LOG(("  file doesn't exists"));
      aHandle->mFileExists = false;
      return DoomFileInternal(aHandle);
    }
    if (NS_FAILED(rv)) {
      LOG(("CacheFileIOManager::OpenNSPRHandle() Open failed with 0x%08" PRIx32,
           static_cast<uint32_t>(rv)));
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mHandlesByLastUsed.AppendElement(aHandle);

  LOG(("CacheFileIOManager::OpenNSPRHandle END, handle=%p", aHandle));

  return NS_OK;
}

void CacheFileIOManager::NSPRHandleUsed(CacheFileHandle* aHandle) {
  MOZ_ASSERT(CacheFileIOManager::IsOnIOThreadOrCeased());
  MOZ_ASSERT(aHandle->mFD);

  DebugOnly<bool> found{};
  found = mHandlesByLastUsed.RemoveElement(aHandle);
  MOZ_ASSERT(found);

  mHandlesByLastUsed.AppendElement(aHandle);
}

nsresult CacheFileIOManager::SyncRemoveDir(nsIFile* aFile, const char* aDir) {
  nsresult rv;
  nsCOMPtr<nsIFile> file;

  if (!aFile) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aDir) {
    file = aFile;
  } else {
    rv = aFile->Clone(getter_AddRefs(file));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv = file->AppendNative(nsDependentCString(aDir));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  if (LOG_ENABLED()) {
    LOG(("CacheFileIOManager::SyncRemoveDir() - Removing directory %s",
         file->HumanReadablePath().get()));
  }

  rv = file->Remove(true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(
        ("CacheFileIOManager::SyncRemoveDir() - Removing failed! "
         "[rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
  }

  return rv;
}

nsresult CacheFileIOManager::DispatchPurgeTask(
    const nsCString& aCacheDirName, const nsCString& aSecondsToWait,
    const nsCString& aPurgeExtension) {
#if !defined(MOZ_BACKGROUNDTASKS)
  // If background tasks are disabled, then we should just bail out early.
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  nsCOMPtr<nsIFile> cacheDir;
  nsresult rv = mCacheDirectory->Clone(getter_AddRefs(cacheDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> profileDir;
  rv = cacheDir->GetParent(getter_AddRefs(profileDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> lf;
  rv = XRE_GetBinaryPath(getter_AddRefs(lf));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString path;
#  if !defined(XP_WIN)
  rv = profileDir->GetNativePath(path);
#  else
  rv = profileDir->GetNativeTarget(path);
#  endif
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIBackgroundTasksRunner> runner =
      do_GetService("@mozilla.org/backgroundtasksrunner;1");

  return runner->RemoveDirectoryInDetachedProcess(
      path, aCacheDirName, aSecondsToWait, aPurgeExtension, "HttpCache"_ns);
#endif
}

void CacheFileIOManager::SyncRemoveAllCacheFiles() {
  LOG(("CacheFileIOManager::SyncRemoveAllCacheFiles()"));
  nsresult rv;

  // If we are already running in a background task, we
  // don't want to spawn yet another one at shutdown.
  if (inBackgroundTask()) {
    return;
  }

  if (StaticPrefs::network_cache_shutdown_purge_in_background_task()) {
    rv = [&]() -> nsresult {
      nsresult rv;

      // If there is no cache directory, there's nothing to remove.
      if (!mCacheDirectory) {
        return NS_OK;
      }

      nsAutoCString leafName;
      rv = mCacheDirectory->GetNativeLeafName(leafName);
      NS_ENSURE_SUCCESS(rv, rv);

      leafName.Append('.');

      PRExplodedTime now;
      PR_ExplodeTime(PR_Now(), PR_GMTParameters, &now);
      leafName.AppendPrintf("%04d-%02d-%02d-%02d-%02d-%02d", now.tm_year,
                            now.tm_month + 1, now.tm_mday, now.tm_hour,
                            now.tm_min, now.tm_sec);
      leafName.Append(kPurgeExtension);

      nsAutoCString secondsToWait;
      secondsToWait.AppendInt(
          StaticPrefs::network_cache_shutdown_purge_folder_wait_seconds());

      rv = DispatchPurgeTask(leafName, secondsToWait, kPurgeExtension);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mCacheDirectory->RenameToNative(nullptr, leafName);
      NS_ENSURE_SUCCESS(rv, rv);

      return NS_OK;
    }();

    // Dispatching to the background task has succeeded. This is finished.
    if (NS_SUCCEEDED(rv)) {
      return;
    }
  }

  SyncRemoveDir(mCacheDirectory, ENTRIES_DIR);
  SyncRemoveDir(mCacheDirectory, DOOMED_DIR);

  // Clear any intermediate state of trash dir enumeration.
  mFailedTrashDirs.Clear();
  mTrashDir = nullptr;

  while (true) {
    // FindTrashDirToRemove() fills mTrashDir if there is any trash directory.
    rv = FindTrashDirToRemove();
    if (rv == NS_ERROR_NOT_AVAILABLE) {
      LOG(
          ("CacheFileIOManager::SyncRemoveAllCacheFiles() - No trash directory "
           "found."));
      break;
    }
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(
          ("CacheFileIOManager::SyncRemoveAllCacheFiles() - "
           "FindTrashDirToRemove() returned an unexpected error. "
           "[rv=0x%08" PRIx32 "]",
           static_cast<uint32_t>(rv)));
      break;
    }

    rv = SyncRemoveDir(mTrashDir, nullptr);
    if (NS_FAILED(rv)) {
      nsAutoCString leafName;
      mTrashDir->GetNativeLeafName(leafName);
      mFailedTrashDirs.AppendElement(leafName);
    }
  }
}

// Returns default ("smart") size (in KB) of cache, given available disk space
// (also in KB)
static uint32_t SmartCacheSize(const int64_t availKB) {
  uint32_t maxSize;

  if (CacheObserver::ClearCacheOnShutdown()) {
    maxSize = kMaxClearOnShutdownCacheSizeKB;
  } else {
    maxSize = kMaxCacheSizeKB;
  }

  if (availKB > 25 * 1024 * 1024) {
    return maxSize;  // skip computing if we're over 25 GB
  }

  // Grow/shrink in 10 MB units, deliberately, so that in the common case we
  // don't shrink cache and evict items every time we startup (it's important
  // that we don't slow down startup benchmarks).
  uint32_t sz10MBs = 0;
  uint32_t avail10MBs = availKB / (1024 * 10);

  // 2.5% of space above 7GB
  if (avail10MBs > 700) {
    sz10MBs += static_cast<uint32_t>((avail10MBs - 700) * .025);
    avail10MBs = 700;
  }
  // 7.5% of space between 500 MB -> 7 GB
  if (avail10MBs > 50) {
    sz10MBs += static_cast<uint32_t>((avail10MBs - 50) * .075);
    avail10MBs = 50;
  }

#ifdef ANDROID
  // On Android, smaller/older devices may have very little storage and
  // device owners may be sensitive to storage footprint: Use a smaller
  // percentage of available space and a smaller minimum.

  // 16% of space up to 500 MB (10 MB min)
  sz10MBs += std::max<uint32_t>(1, static_cast<uint32_t>(avail10MBs * .16));
#else
  // 30% of space up to 500 MB (50 MB min)
  sz10MBs += std::max<uint32_t>(5, static_cast<uint32_t>(avail10MBs * .3));
#endif

  return std::min<uint32_t>(maxSize, sz10MBs * 10 * 1024);
}

nsresult CacheFileIOManager::UpdateSmartCacheSize(int64_t aFreeSpace) {
  MOZ_ASSERT(mIOThread->IsCurrentThread());

  nsresult rv;

  if (!CacheObserver::SmartCacheSizeEnabled()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Wait at least kSmartSizeUpdateInterval before recomputing smart size.
  static const TimeDuration kUpdateLimit =
      TimeDuration::FromMilliseconds(kSmartSizeUpdateInterval);
  if (!mLastSmartSizeTime.IsNull() &&
      (TimeStamp::NowLoRes() - mLastSmartSizeTime) < kUpdateLimit) {
    return NS_OK;
  }

  // Do not compute smart size when cache size is not reliable.
  bool isUpToDate = false;
  CacheIndex::IsUpToDate(&isUpToDate);
  if (!isUpToDate) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  uint32_t cacheUsage;
  rv = CacheIndex::GetCacheSize(&cacheUsage);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(
        ("CacheFileIOManager::UpdateSmartCacheSize() - Cannot get cacheUsage! "
         "[rv=0x%08" PRIx32 "]",
         static_cast<uint32_t>(rv)));
    return rv;
  }

  mLastSmartSizeTime = TimeStamp::NowLoRes();

  uint32_t smartSize = SmartCacheSize(aFreeSpace + cacheUsage);

  if (smartSize == CacheObserver::DiskCacheCapacity()) {
    // Smart size has not changed.
    return NS_OK;
  }

  CacheObserver::SetSmartDiskCacheCapacity(smartSize);

  return NS_OK;
}

// Memory reporting

namespace {

// A helper class that dispatches and waits for an event that gets result of
// CacheFileIOManager->mHandles.SizeOfExcludingThis() on the I/O thread
// to safely get handles memory report.
// We must do this, since the handle list is only accessed and managed w/o
// locking on the I/O thread.  That is by design.
class SizeOfHandlesRunnable : public Runnable {
 public:
  SizeOfHandlesRunnable(mozilla::MallocSizeOf mallocSizeOf,
                        CacheFileHandles const& handles,
                        nsTArray<CacheFileHandle*> const& specialHandles)
      : Runnable("net::SizeOfHandlesRunnable"),
        mMonitor("SizeOfHandlesRunnable.mMonitor"),
        mMonitorNotified(false),
        mMallocSizeOf(mallocSizeOf),
        mHandles(handles),
        mSpecialHandles(specialHandles),
        mSize(0) {}

  size_t Get(CacheIOThread* thread) {
    nsCOMPtr<nsIEventTarget> target = thread->Target();
    if (!target) {
      NS_ERROR("If we have the I/O thread we also must have the I/O target");
      return 0;
    }

    mozilla::MonitorAutoLock mon(mMonitor);
    mMonitorNotified = false;
    nsresult rv = target->Dispatch(this, nsIEventTarget::DISPATCH_NORMAL);
    if (NS_FAILED(rv)) {
      NS_ERROR("Dispatch failed, cannot do memory report of CacheFileHandles");
      return 0;
    }

    while (!mMonitorNotified) {
      mon.Wait();
    }
    return mSize;
  }

  NS_IMETHOD Run() override {
    mozilla::MonitorAutoLock mon(mMonitor);
    // Excluding this since the object itself is a member of CacheFileIOManager
    // reported in CacheFileIOManager::SizeOfIncludingThis as part of |this|.
    mSize = mHandles.SizeOfExcludingThis(mMallocSizeOf);
    for (uint32_t i = 0; i < mSpecialHandles.Length(); ++i) {
      mSize += mSpecialHandles[i]->SizeOfIncludingThis(mMallocSizeOf);
    }

    mMonitorNotified = true;
    mon.Notify();
    return NS_OK;
  }

 private:
  mozilla::Monitor mMonitor;
  bool mMonitorNotified;
  mozilla::MallocSizeOf mMallocSizeOf;
  CacheFileHandles const& mHandles;
  nsTArray<CacheFileHandle*> const& mSpecialHandles;
  size_t mSize;
};

}  // namespace

size_t CacheFileIOManager::SizeOfExcludingThisInternal(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t n = 0;

  if (mIOThread) {
    n += mIOThread->SizeOfIncludingThis(mallocSizeOf);

    // mHandles and mSpecialHandles must be accessed only on the I/O thread,
    // must sync dispatch.
    RefPtr<SizeOfHandlesRunnable> sizeOfHandlesRunnable =
        new SizeOfHandlesRunnable(mallocSizeOf, mHandles, mSpecialHandles);
    n += sizeOfHandlesRunnable->Get(mIOThread);
  }

  // mHandlesByLastUsed just refers handles reported by mHandles.

  // mCacheDirectory is an nsIFile which we don't have reporting for.

  // mMetadataWritesTimer is an nsITimer which we don't have reporting for.
  // Note that it would need to be accessed on the I/O thread.

  // mTrashTimer is an nsITimer which we don't have reporting for.

  // mTrashDir is an nsIFile which we don't have reporting for.

  for (uint32_t i = 0; i < mFailedTrashDirs.Length(); ++i) {
    n += mFailedTrashDirs[i].SizeOfExcludingThisIfUnshared(mallocSizeOf);
  }

  return n;
}

// static
size_t CacheFileIOManager::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  if (!gInstance) return 0;

  return gInstance->SizeOfExcludingThisInternal(mallocSizeOf);
}

// static
size_t CacheFileIOManager::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  return mallocSizeOf(gInstance) + SizeOfExcludingThis(mallocSizeOf);
}

#if defined(MOZ_CACHE_ASYNC_IO)
void CacheFileIOManager::DispatchPendingEvents() {
  auto pendingEvents = std::move(mPendingEvents);
  for (auto& event : pendingEvents) {
    mIOThread->Dispatch(event.first, event.second);
  }
}
#endif

}  // namespace mozilla::net
