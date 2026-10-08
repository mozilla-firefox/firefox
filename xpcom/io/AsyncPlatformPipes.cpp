/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AsyncPlatformPipes.h"

#include "base/eintr_wrapper.h"
#include "base/message_loop.h"
#include "mozilla/CondVar.h"
#include "mozilla/EventTargetCapability.h"
#include "mozilla/UniquePtr.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#ifndef XP_WIN
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace mozilla {

namespace platform_pipe_detail {

class PlatformPipeLink
#ifdef XP_WIN
    : public MessageLoopForIO::IOHandler
#else
    : public MessageLoopForIO::Watcher
#endif
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_EVENT_TARGET(
      PlatformPipeLink, mIOThread.GetEventTarget())

 public:
  PlatformPipeLink(UniqueFileHandle aHandle, uint32_t aBufferSize);

  // Clears mPending to nullptr, returning the previous value. Also notifies
  // mPendingCV in case there are any threads synchronously waiting for pending
  // I/O to complete.
  already_AddRefed<PlatformPipeLink> TakePending() MOZ_REQUIRES(mMutex);

  // NOTE: This performs potentially-blocking I/O to close mHandle, so should
  // not be called on the IO thread!
  void Close(nsresult aStatus, bool aInternal) MOZ_EXCLUDES(mMutex)
      MOZ_EXCLUDES(mIOThread);

  // Dispatch an internal Close(...) call to be performed in a background task.
  // Called when background I/O operations fail.
  void DispatchPipeError(nsresult aStatus) MOZ_REQUIRES(mMutex, mIOThread);

  // Dispatch notification of mCallback to another thread.
  //
  // NOTE: We intentionally do not notify directly on the current thread, even
  // if no event target is provided, as we don't want user code running on the
  // IPC I/O thread as it is very latency sensitive.
  void DispatchNotify() MOZ_REQUIRES(mMutex);

  void AdvanceIO() MOZ_EXCLUDES(mMutex) MOZ_REQUIRES(mIOThread);

  void AdvanceIOLocked() MOZ_REQUIRES(mMutex, mIOThread);

#ifdef XP_WIN
  void OnIOCompleted(MessageLoopForIO::IOContext* aContext,
                     DWORD aBytesTransferred, DWORD aError) override;
#else
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;
#endif

  const EventTargetCapability<nsISerialEventTarget> mIOThread;
  Mutex mMutex{"PlatformPipeReader"};

  UniqueFileHandle mHandle MOZ_GUARDED_BY(mMutex);
  const UniquePtr<char[]> mBuffer;
  const uint32_t mBufferSize;

  bool mProcessingSegment MOZ_GUARDED_BY(mMutex) = false;
  bool mClosing MOZ_GUARDED_BY(mMutex) = false;

  nsresult mStatus MOZ_GUARDED_BY(mMutex) = NS_OK;

  // `mOffset` is the start of the readable region in the buffer, and
  // `mAvailable` is its size.
  uint32_t mOffset MOZ_GUARDED_BY(mMutex) = 0;
  uint32_t mAvailable MOZ_GUARDED_BY(mMutex) = 0;

  bool mCallbackClosureOnly MOZ_GUARDED_BY(mMutex) = false;
  nsCOMPtr<nsIRunnable> mCallback MOZ_GUARDED_BY(mMutex);
  nsCOMPtr<nsIEventTarget> mCallbackTarget MOZ_GUARDED_BY(mMutex);

  // A reference keeping `this` alive while I/O is in-flight.
  // This is particularly important on Windows where mBuffer needs to be kept
  // alive while Overlapped IO is ongoing.
  RefPtr<PlatformPipeLink> mPending MOZ_GUARDED_BY(mMutex);

  // CondVar which is notified whenever mPending is cleared to `nullptr`.
  // Used by Close() to wait for pending I/O to complete.
  CondVar mPendingCV{mMutex, "PlatformPipeReader::mPendingCV"};

#ifdef XP_WIN
  MessageLoopForIO::IOContext mIOContext MOZ_GUARDED_BY(mMutex) = {};
#else
  MessageLoopForIO::FileDescriptorWatcher mWatcher MOZ_GUARDED_BY(mIOThread);
#endif

 private:
  ~PlatformPipeLink() = default;
};

PlatformPipeLink::PlatformPipeLink(UniqueFileHandle aHandle,
                                   uint32_t aBufferSize)
    : mIOThread(XRE_GetAsyncIOEventTarget()),
      mHandle(std::move(aHandle)),
      mBuffer(MakeUnique<char[]>(aBufferSize)),
      mBufferSize(aBufferSize) {
  MOZ_ASSERT(aBufferSize > 1, "invalid buffer size");
  MOZ_ASSERT(mHandle, "invalid handle");

#if defined(DEBUG) && !defined(XP_WIN)
  struct stat st{};
  MOZ_ASSERT(fstat(mHandle.get(), &st) == 0 && !S_ISREG(st.st_mode),
             "PlatformPipeLink does not support regular files");
  MOZ_ASSERT(fcntl(mHandle.get(), F_GETFL) & O_NONBLOCK,
             "PlatformPipeLink requires non-blocking file descriptors");
#endif
}

already_AddRefed<PlatformPipeLink> PlatformPipeLink::TakePending() {
  RefPtr<PlatformPipeLink> pending = mPending.forget();
  if (pending) {
    mPendingCV.NotifyAll();
  }
  return pending.forget();
}

void PlatformPipeLink::Close(nsresult aStatus, bool aInternal) {
  MOZ_RELEASE_ASSERT(!mIOThread.IsOnCurrentThread(),
                     "Close may deadlock if called on the IO thread");

  MutexAutoLock lock(mMutex);
  MOZ_RELEASE_ASSERT(aInternal || !mProcessingSegment,
                     "Cannot close pipe during ReadSegments callback");
  if (NS_FAILED(mStatus)) {
    return;
  }

  // Ensure we're marked as closing before we cancel pending IO, this prevents
  // new pending IO from being started.
  mClosing = true;

  if (mPending) {
#ifdef XP_WIN
    // On Windows, if we still have pending IO, request that it is cancelled.
    // We can do that from any thread using CancelIoEx. The notification will be
    // delivered to the IO thread.
    CancelIoEx(mHandle.get(), &mIOContext.overlapped);
#else
    // On POSIX, request cancellation by dispatching a runnable to stop watching
    // our file descriptor, and clear mPending.
    MOZ_ALWAYS_SUCCEEDS(mIOThread.Dispatch(NS_NewRunnableFunction(
        "PlatformPipeLink::CancelIO", [self = RefPtr{this}] {
          self->mIOThread.AssertOnCurrentThread();
          RefPtr<PlatformPipeLink> pending;
          MutexAutoLock lock(self->mMutex);
          if (self->mPending) {
            self->mWatcher.StopWatchingFileDescriptor();
            pending = self->TakePending();
          }
        })));
#endif

    // Wait on the condvar until all pending operations are completed.
    // NOTE: This unlocks mMutex while we wait.
    while (mPending) {
      mPendingCV.Wait();
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(!mPending, "How do we still have pending I/O?");

  // Someone else may have closed the link while we were unlocked.
  if (NS_FAILED(mStatus)) {
    return;
  }

  mStatus = NS_SUCCEEDED(aStatus) ? NS_BASE_STREAM_CLOSED : aStatus;
  DispatchNotify();

  // NOTE: Make sure we've closed the handle synchronously here, as callers rely
  // on the kernel file object being destroyed.
  mHandle = nullptr;
}

void PlatformPipeLink::DispatchPipeError(nsresult aStatus) {
  MOZ_ASSERT(!mPending,
             "Shouldn't be pending when closing due to a pipe error");

  // Ensure other threads don't start new I/O after this point.
  // Don't actually change mStatus until we're in the Close method.
  mClosing = true;

  // Perform the Close() operation on a background task which is allowed to
  // block. This operation may be blocking, as it closes the pipe, which could
  // do expensive I/O.
  NS_DispatchBackgroundTask(
      NewRunnableMethod<nsresult, bool>("PlatformPipeLink::Close", this,
                                        &PlatformPipeLink::Close, aStatus,
                                        /* aInternal */ true),
      NS_DISPATCH_EVENT_MAY_BLOCK);
}

void PlatformPipeLink::DispatchNotify() {
  nsCOMPtr<nsIRunnable> callback = mCallback.forget();
  nsCOMPtr<nsIEventTarget> target = mCallbackTarget.forget();
  if (!callback) {
    return;
  }
  if (target) {
    target->Dispatch(callback.forget());
  } else {
    NS_DispatchBackgroundTask(callback.forget());
  }
}

void PlatformPipeLink::AdvanceIO() {
  MutexAutoLock lock(mMutex);
  AdvanceIOLocked();
}

void PlatformPipeLink::AdvanceIOLocked() {
  // If we've closed, or are in the process of closing, our PlatformPipeLink,
  // don't start any new I/O operations.
  if (mClosing || !mHandle || NS_FAILED(mStatus)) {
    return;
  }

  // We still have outstanding I/O, or our buffer already has data waiting to
  // be consumed. Either way, don't start new I/O.
  if (mPending || mAvailable) {
    return;
  }

#ifdef XP_WIN
  // On Windows, we need to register the IO handler the first time we're on the
  // I/O thread.
  if (!mIOContext.handler) {
    MessageLoopForIO::current()->RegisterIOHandler(mHandle.get(), this);
    mIOContext.handler = this;
  }
  BOOL ok = ReadFile(mHandle.get(), mBuffer.get(), mBufferSize, nullptr,
                     &mIOContext.overlapped);
  if (!ok) {
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
      mPending = this;
      return;
    }
    if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) {
      DispatchPipeError(NS_BASE_STREAM_CLOSED);
    } else {
      DispatchPipeError(NS_ERROR_FAILURE);
    }
    return;
  }

  mPending = this;
#else
  ssize_t rv = HANDLE_EINTR(read(mHandle.get(), mBuffer.get(), mBufferSize));
  if (rv > 0) {
    mOffset = 0;
    mAvailable = static_cast<uint32_t>(rv);
    if (!mCallbackClosureOnly) {
      DispatchNotify();
    }
    return;
  }

  if (rv == 0) {
    DispatchPipeError(NS_BASE_STREAM_CLOSED);
    return;
  }

  if (errno == EAGAIN
#  if EWOULDBLOCK != EAGAIN
      || errno == EWOULDBLOCK
#  endif
  ) {
    if (MessageLoopForIO::current()->WatchFileDescriptor(
            mHandle.get(), false, MessageLoopForIO::WATCH_READ, &mWatcher,
            this)) {
      mPending = this;
      return;
    }
  }

  DispatchPipeError(NS_ERROR_FAILURE);
#endif
}

#ifdef XP_WIN
void PlatformPipeLink::OnIOCompleted(MessageLoopForIO::IOContext* aContext,
                                     DWORD aBytesTransferred, DWORD aError) {
  mIOThread.AssertOnCurrentThread();
  RefPtr<PlatformPipeLink> pending;
  MutexAutoLock lock(mMutex);
  if (aContext != &mIOContext) {
    return;
  }

  pending = TakePending();
  if (!pending) {
    return;
  }

  if (mClosing || NS_FAILED(mStatus)) {
    return;
  }

  if (aError != ERROR_SUCCESS) {
    if (aError == ERROR_BROKEN_PIPE || aError == ERROR_HANDLE_EOF ||
        aError == ERROR_OPERATION_ABORTED) {
      DispatchPipeError(NS_BASE_STREAM_CLOSED);
    } else {
      DispatchPipeError(NS_ERROR_FAILURE);
    }
    return;
  }

  if (aBytesTransferred == 0) {
    DispatchPipeError(NS_BASE_STREAM_CLOSED);
    return;
  }

  mOffset = 0;
  mAvailable = aBytesTransferred;
  if (!mCallbackClosureOnly) {
    DispatchNotify();
  }
}
#else
void PlatformPipeLink::OnFileCanReadWithoutBlocking(int fd) {
  mIOThread.AssertOnCurrentThread();
  RefPtr<PlatformPipeLink> pending;
  MutexAutoLock lock(mMutex);
  pending = TakePending();
  AdvanceIOLocked();
}

void PlatformPipeLink::OnFileCanWriteWithoutBlocking(int fd) {
  MOZ_ASSERT_UNREACHABLE();
}
#endif

}  // namespace platform_pipe_detail

//-----------------------------------------------------------------------------
// PlatformPipeReader
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(PlatformPipeReader, nsIInputStream, nsIAsyncInputStream)

PlatformPipeReader::PlatformPipeReader(UniqueFileHandle aHandle,
                                       uint32_t aBufferSize)
    : mLink(new platform_pipe_detail::PlatformPipeLink(std::move(aHandle),
                                                       aBufferSize)) {}

PlatformPipeReader::~PlatformPipeReader() { Close(); }

NS_IMETHODIMP PlatformPipeReader::Close() {
  return CloseWithStatus(NS_BASE_STREAM_CLOSED);
}

NS_IMETHODIMP PlatformPipeReader::Available(uint64_t* aAvailable) {
  MutexAutoLock lock(mLink->mMutex);
  if (NS_FAILED(mLink->mStatus)) {
    return mLink->mStatus;
  }
  *aAvailable = mLink->mAvailable;
  return NS_OK;
}

NS_IMETHODIMP PlatformPipeReader::StreamStatus() {
  MutexAutoLock lock(mLink->mMutex);
  return mLink->mStatus;
}

NS_IMETHODIMP PlatformPipeReader::Read(char* aBuf, uint32_t aCount,
                                       uint32_t* aReadCount) {
  return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, aReadCount);
}

NS_IMETHODIMP PlatformPipeReader::ReadSegments(nsWriteSegmentFun aWriter,
                                               void* aClosure, uint32_t aCount,
                                               uint32_t* aReadCount) {
  *aReadCount = 0;

  MutexAutoLock lock(mLink->mMutex);
  if (NS_FAILED(mLink->mStatus)) {
    return mLink->mStatus == NS_BASE_STREAM_CLOSED ? NS_OK : mLink->mStatus;
  }

  if (!mLink->mAvailable) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  MOZ_RELEASE_ASSERT(!mLink->mProcessingSegment,
                     "Only one thread may be processing a segment at a time");

  char* start = mLink->mBuffer.get() + mLink->mOffset;
  uint32_t length = std::min(aCount, mLink->mAvailable);

  mLink->mProcessingSegment = true;
  {
    MutexAutoUnlock unlock(mLink->mMutex);
    nsresult rv = aWriter(this, aClosure, start, 0, length, aReadCount);
    if (NS_FAILED(rv)) {
      *aReadCount = 0;
    }
    MOZ_RELEASE_ASSERT(*aReadCount <= length);
  }
  mLink->mProcessingSegment = false;

  mLink->mOffset += *aReadCount;
  mLink->mAvailable -= *aReadCount;

  // If a closure-only callback is armed, the caller isn't listening for new
  // data, so only the IO thread is able to notice the peer closing. Re-kick
  // AdvanceIO once the buffer has drained so a subsequent close is observed.
  if (!mLink->mAvailable && mLink->mCallback && mLink->mCallbackClosureOnly) {
    mLink->mIOThread.Dispatch(
        NewRunnableMethod("PlatformPipeLink::AdvanceIO", mLink,
                          &platform_pipe_detail::PlatformPipeLink::AdvanceIO));
  }
  return NS_OK;
}

NS_IMETHODIMP PlatformPipeReader::IsNonBlocking(bool* aNonBlocking) {
  *aNonBlocking = true;
  return NS_OK;
}

NS_IMETHODIMP PlatformPipeReader::CloseWithStatus(nsresult aStatus) {
  mLink->Close(aStatus, /* aInternal */ false);
  return NS_OK;
}

NS_IMETHODIMP PlatformPipeReader::AsyncWait(nsIInputStreamCallback* aCallback,
                                            uint32_t aFlags,
                                            uint32_t aRequestedCount,
                                            nsIEventTarget* aTarget) {
  MutexAutoLock lock(mLink->mMutex);

  if (!aCallback) {
    mLink->mCallback = nullptr;
    mLink->mCallbackTarget = nullptr;
    return NS_OK;
  }

  mLink->mCallback = NS_NewRunnableFunction(
      "PlatformPipeReader::AsyncWait",
      [self = RefPtr{this}, callback = RefPtr{aCallback}] {
        callback->OnInputStreamReady(self);
      });
  mLink->mCallbackTarget = aTarget;
  mLink->mCallbackClosureOnly = aFlags & WAIT_CLOSURE_ONLY;

  if (NS_FAILED(mLink->mStatus) ||
      (!mLink->mCallbackClosureOnly && mLink->mAvailable)) {
    mLink->DispatchNotify();
  } else {
    mLink->mIOThread.Dispatch(
        NewRunnableMethod("PlatformPipeLink::AdvanceIO", mLink,
                          &platform_pipe_detail::PlatformPipeLink::AdvanceIO));
  }
  return NS_OK;
}

}  // namespace mozilla
