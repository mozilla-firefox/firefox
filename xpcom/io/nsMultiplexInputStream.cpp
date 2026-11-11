/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The multiplex stream concatenates a list of input streams into a single
 * stream.
 */

#include "nsMultiplexInputStream.h"

#include "base/basictypes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Mutex.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsIAsyncInputStream.h"
#include "nsIBufferedStreams.h"
#include "nsIClassInfoImpl.h"
#include "nsICloneableInputStream.h"
#include "nsIIPCSerializableInputStream.h"
#include "nsIInputStreamLength.h"
#include "nsIMultiplexInputStream.h"
#include "nsISeekableStream.h"
#include "nsNetUtil.h"
#include "nsStreamUtils.h"

using namespace mozilla;
using namespace mozilla::ipc;

using mozilla::Abs;

NS_IMPL_ADDREF(nsMultiplexInputStream)
NS_IMPL_RELEASE(nsMultiplexInputStream)

NS_IMPL_CLASSINFO(nsMultiplexInputStream, nullptr, nsIClassInfo::THREADSAFE,
                  NS_MULTIPLEXINPUTSTREAM_CID)

NS_INTERFACE_MAP_BEGIN(nsMultiplexInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIMultiplexInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISeekableStream, IsSeekable())
  NS_INTERFACE_MAP_ENTRY(nsITellableStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIIPCSerializableInputStream,
                                     IsIPCSerializable())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICloneableInputStream, IsCloneable())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIAsyncInputStream, IsAsyncInputStream())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamCallback,
                                     IsAsyncInputStream())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamLength,
                                     IsInputStreamLength())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIAsyncInputStreamLength,
                                     IsAsyncInputStreamLength())
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIMultiplexInputStream)
  NS_IMPL_QUERY_CLASSINFO(nsMultiplexInputStream)
NS_INTERFACE_MAP_END

NS_IMPL_CI_INTERFACE_GETTER(nsMultiplexInputStream, nsIMultiplexInputStream,
                            nsIInputStream, nsISeekableStream,
                            nsITellableStream)

static nsresult AvailableMaybeSeek(nsMultiplexInputStream::StreamData& aStream,
                                   uint64_t* aResult) {
  nsresult rv = aStream.mBufferedStream->Available(aResult);
  if (rv == NS_BASE_STREAM_CLOSED) {
    // Blindly seek to the current position if Available() returns
    // NS_BASE_STREAM_CLOSED.
    // If nsIFileInputStream is closed in Read() due to CLOSE_ON_EOF flag,
    // Seek() could reopen the file if REOPEN_ON_REWIND flag is set.
    if (aStream.mSeekableStream) {
      nsresult rvSeek =
          aStream.mSeekableStream->Seek(nsISeekableStream::NS_SEEK_CUR, 0);
      if (NS_SUCCEEDED(rvSeek)) {
        rv = aStream.mBufferedStream->Available(aResult);
      }
    }
  }
  return rv;
}

nsMultiplexInputStream::nsMultiplexInputStream()
    : mLock("nsMultiplexInputStream lock"),
      mCurrentStream(0),
      mStartedReadingCurrent(false),
      mStatus(NS_OK),
      mAsyncWaitFlags(0),
      mAsyncWaitRequestedCount(0),
      mSeekableStreams(0),
      mIPCSerializableStreams(0),
      mCloneableStreams(0) {}

NS_IMETHODIMP
nsMultiplexInputStream::GetCount(uint32_t* aCount) {
  MutexAutoLock lock(mLock);
  *aCount = mStreams.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::AppendStream(nsIInputStream* aStream) {
  MutexAutoLock lock(mLock);

  StreamData* streamData = mStreams.AppendElement(fallible);
  if (NS_WARN_IF(!streamData)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsresult rv = streamData->Initialize(aStream);
  NS_ENSURE_SUCCESS(rv, rv);

  UpdateQIMap(*streamData);

  if (mStatus == NS_BASE_STREAM_CLOSED) {
    // We were closed, but now we have more data to read.
    mStatus = NS_OK;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::GetStream(uint32_t aIndex, nsIInputStream** aResult) {
  MutexAutoLock lock(mLock);

  if (aIndex >= mStreams.Length()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  StreamData& streamData = mStreams.ElementAt(aIndex);
  nsCOMPtr<nsIInputStream> stream = streamData.mOriginalStream;
  stream.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::Close() {
  nsTArray<nsCOMPtr<nsIInputStream>> streams;

  // Let's take a copy of the streams becuase, calling close() it could trigger
  // a nsIInputStreamCallback immediately and we don't want to create a deadlock
  // with mutex.
  {
    MutexAutoLock lock(mLock);
    uint32_t len = mStreams.Length();
    for (uint32_t i = 0; i < len; ++i) {
      if (NS_WARN_IF(
              !streams.AppendElement(mStreams[i].mBufferedStream, fallible))) {
        mStatus = NS_BASE_STREAM_CLOSED;
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
    mStatus = NS_BASE_STREAM_CLOSED;
  }

  nsresult rv = NS_OK;

  uint32_t len = streams.Length();
  for (uint32_t i = 0; i < len; ++i) {
    nsresult rv2 = streams[i]->Close();
    // We still want to close all streams, but we should return an error
    if (NS_FAILED(rv2)) {
      rv = rv2;
    }
  }

  return rv;
}

NS_IMETHODIMP
nsMultiplexInputStream::Available(uint64_t* aResult) {
  *aResult = 0;

  MutexAutoLock lock(mLock);
  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  uint64_t avail = 0;
  nsresult rv = NS_BASE_STREAM_CLOSED;

  uint32_t len = mStreams.Length();
  for (uint32_t i = mCurrentStream; i < len; i++) {
    uint64_t streamAvail;
    rv = AvailableMaybeSeek(mStreams[i], &streamAvail);
    if (rv == NS_BASE_STREAM_CLOSED) {
      // If a stream is closed, we continue with the next one.
      // If this is the current stream we move to the following stream.
      if (mCurrentStream == i) {
        NextStream();
      }

      // If this is the last stream, we want to return this error code.
      continue;
    }

    if (NS_WARN_IF(NS_FAILED(rv))) {
      mStatus = rv;
      return mStatus;
    }

    // If the current stream is async, we have to return what we have so far
    // without processing the following streams. This is needed because
    // ::Available should return only what is currently available. In case of an
    // nsIAsyncInputStream, we have to call AsyncWait() in order to read more.
    if (mStreams[i].mAsyncStream) {
      avail += streamAvail;
      break;
    }

    if (streamAvail == 0) {
      // Nothing to read for this stream. Let's move to the next one.
      continue;
    }

    avail += streamAvail;
  }

  // We still have something to read. We don't want to return an error code yet.
  if (avail) {
    *aResult = avail;
    return NS_OK;
  }

  // Let's propagate the last error message.
  mStatus = rv;
  return rv;
}

NS_IMETHODIMP
nsMultiplexInputStream::StreamStatus() {
  MutexAutoLock lock(mLock);
  return mStatus;
}

NS_IMETHODIMP
nsMultiplexInputStream::Read(char* aBuf, uint32_t aCount, uint32_t* aResult) {
  MutexAutoLock lock(mLock);
  // It is tempting to implement this method in terms of ReadSegments, but
  // that would prevent this class from being used with streams that only
  // implement Read (e.g., file streams).

  *aResult = 0;

  if (mStatus == NS_BASE_STREAM_CLOSED) {
    return NS_OK;
  }
  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  nsresult rv = NS_OK;

  uint32_t len = mStreams.Length();
  while (mCurrentStream < len && aCount) {
    uint32_t read;
    rv = mStreams[mCurrentStream].mBufferedStream->Read(aBuf, aCount, &read);

    // XXX some streams return NS_BASE_STREAM_CLOSED to indicate EOF.
    // (This is a bug in those stream implementations)
    if (rv == NS_BASE_STREAM_CLOSED) {
      MOZ_ASSERT_UNREACHABLE(
          "Input stream's Read method returned "
          "NS_BASE_STREAM_CLOSED");
      rv = NS_OK;
      read = 0;
    } else if (NS_FAILED(rv)) {
      break;
    }

    if (read == 0) {
      NextStream();
    } else {
      NS_ASSERTION(aCount >= read, "Read more than requested");
      *aResult += read;
      aCount -= read;
      aBuf += read;
      mStartedReadingCurrent = true;

      mStreams[mCurrentStream].mCurrentPos += read;
    }
  }
  return *aResult ? NS_OK : rv;
}

NS_IMETHODIMP
nsMultiplexInputStream::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                                     uint32_t aCount, uint32_t* aResult) {
  MutexAutoLock lock(mLock);

  if (mStatus == NS_BASE_STREAM_CLOSED) {
    *aResult = 0;
    return NS_OK;
  }
  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  NS_ASSERTION(aWriter, "missing aWriter");

  nsresult rv = NS_OK;
  ReadSegmentsState state;
  state.mThisStream = this;
  state.mOffset = 0;
  state.mWriter = aWriter;
  state.mClosure = aClosure;
  state.mDone = false;

  uint32_t len = mStreams.Length();
  while (mCurrentStream < len && aCount) {
    uint32_t read;
    rv = mStreams[mCurrentStream].mBufferedStream->ReadSegments(
        ReadSegCb, &state, aCount, &read);

    // XXX some streams return NS_BASE_STREAM_CLOSED to indicate EOF.
    // (This is a bug in those stream implementations)
    if (rv == NS_BASE_STREAM_CLOSED) {
      MOZ_ASSERT_UNREACHABLE(
          "Input stream's Read method returned "
          "NS_BASE_STREAM_CLOSED");
      rv = NS_OK;
      read = 0;
    }

    // if |aWriter| decided to stop reading segments...
    if (state.mDone || NS_FAILED(rv)) {
      break;
    }

    // if stream is empty, then advance to the next stream.
    if (read == 0) {
      NextStream();
    } else {
      NS_ASSERTION(aCount >= read, "Read more than requested");
      state.mOffset += read;
      aCount -= read;
      mStartedReadingCurrent = true;

      mStreams[mCurrentStream].mCurrentPos += read;
    }
  }

  // if we successfully read some data, then this call succeeded.
  *aResult = state.mOffset;
  return state.mOffset ? NS_OK : rv;
}

nsresult nsMultiplexInputStream::ReadSegCb(nsIInputStream* aIn, void* aClosure,
                                           const char* aFromRawSegment,
                                           uint32_t aToOffset, uint32_t aCount,
                                           uint32_t* aWriteCount) {
  nsresult rv;
  ReadSegmentsState* state = (ReadSegmentsState*)aClosure;
  rv = (state->mWriter)(state->mThisStream, state->mClosure, aFromRawSegment,
                        aToOffset + state->mOffset, aCount, aWriteCount);
  if (NS_FAILED(rv)) {
    state->mDone = true;
  }
  return rv;
}

NS_IMETHODIMP
nsMultiplexInputStream::IsNonBlocking(bool* aNonBlocking) {
  MutexAutoLock lock(mLock);

  uint32_t len = mStreams.Length();
  if (len == 0) {
    // Claim to be non-blocking, since we won't block the caller.
    *aNonBlocking = true;
    return NS_OK;
  }

  for (uint32_t i = 0; i < len; ++i) {
    nsresult rv = mStreams[i].mBufferedStream->IsNonBlocking(aNonBlocking);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    // If one is blocking the entire stream becomes blocking.
    if (!*aNonBlocking) {
      return NS_OK;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::Seek(int32_t aWhence, int64_t aOffset) {
  MutexAutoLock lock(mLock);

  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  nsresult rv;

  uint32_t oldCurrentStream = mCurrentStream;
  bool oldStartedReadingCurrent = mStartedReadingCurrent;

  if (aWhence == NS_SEEK_SET) {
    int64_t remaining = aOffset;
    if (aOffset == 0) {
      mCurrentStream = 0;
      mStartedReadingCurrent = false;
    }
    for (uint32_t i = 0; i < mStreams.Length(); ++i) {
      nsCOMPtr<nsISeekableStream> stream = mStreams[i].mSeekableStream;
      if (!stream) {
        return NS_ERROR_FAILURE;
      }

      // See if all remaining streams should be rewound
      if (remaining == 0) {
        if (i < oldCurrentStream ||
            (i == oldCurrentStream && oldStartedReadingCurrent)) {
          rv = stream->Seek(NS_SEEK_SET, 0);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }

          mStreams[i].mCurrentPos = 0;
          continue;
        } else {
          break;
        }
      }

      // Get position in the current stream
      int64_t streamPos;
      if (i > oldCurrentStream ||
          (i == oldCurrentStream && !oldStartedReadingCurrent)) {
        streamPos = 0;
      } else {
        streamPos = mStreams[i].mCurrentPos;
      }

      // See if we need to seek the current stream forward or backward
      if (remaining < streamPos) {
        rv = stream->Seek(NS_SEEK_SET, remaining);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        mStreams[i].mCurrentPos = remaining;
        mCurrentStream = i;
        mStartedReadingCurrent = remaining != 0;

        remaining = 0;
      } else if (remaining > streamPos) {
        if (i < oldCurrentStream) {
          // We're already at end so no need to seek this stream
          remaining -= streamPos;
          NS_ASSERTION(remaining >= 0, "Remaining invalid");
        } else {
          uint64_t avail;
          rv = AvailableMaybeSeek(mStreams[i], &avail);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }

          int64_t newPos = XPCOM_MIN(remaining, streamPos + (int64_t)avail);

          rv = stream->Seek(NS_SEEK_SET, newPos);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }

          mStreams[i].mCurrentPos = newPos;
          mCurrentStream = i;
          mStartedReadingCurrent = true;

          remaining -= newPos;
          NS_ASSERTION(remaining >= 0, "Remaining invalid");
        }
      } else {
        NS_ASSERTION(remaining == streamPos, "Huh?");
        MOZ_ASSERT(remaining != 0, "Zero remaining should be handled earlier");
        remaining = 0;
        mCurrentStream = i;
        mStartedReadingCurrent = true;
      }
    }

    return NS_OK;
  }

  if (aWhence == NS_SEEK_CUR && aOffset > 0) {
    int64_t remaining = aOffset;
    for (uint32_t i = mCurrentStream; remaining && i < mStreams.Length(); ++i) {
      uint64_t avail;
      rv = AvailableMaybeSeek(mStreams[i], &avail);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      int64_t seek = XPCOM_MIN((int64_t)avail, remaining);

      rv = mStreams[i].mSeekableStream->Seek(NS_SEEK_CUR, seek);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mStreams[i].mCurrentPos += seek;
      mCurrentStream = i;
      mStartedReadingCurrent = true;

      remaining -= seek;
    }

    return NS_OK;
  }

  if (aWhence == NS_SEEK_CUR && aOffset < 0) {
    int64_t remaining = -aOffset;
    for (uint32_t i = mCurrentStream; remaining && i != (uint32_t)-1; --i) {
      int64_t pos = mStreams[i].mCurrentPos;

      int64_t seek = XPCOM_MIN(pos, remaining);

      rv = mStreams[i].mSeekableStream->Seek(NS_SEEK_CUR, -seek);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mStreams[i].mCurrentPos -= seek;
      mCurrentStream = i;
      mStartedReadingCurrent = seek != pos;

      remaining -= seek;
    }

    return NS_OK;
  }

  if (aWhence == NS_SEEK_CUR) {
    NS_ASSERTION(aOffset == 0, "Should have handled all non-zero values");

    return NS_OK;
  }

  if (aWhence == NS_SEEK_END) {
    if (aOffset > 0) {
      return NS_ERROR_INVALID_ARG;
    }

    int64_t remaining = aOffset;
    int32_t i;
    for (i = mStreams.Length() - 1; i >= 0; --i) {
      nsCOMPtr<nsISeekableStream> stream = mStreams[i].mSeekableStream;

      uint64_t avail;
      rv = AvailableMaybeSeek(mStreams[i], &avail);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      uint64_t streamLength = avail + mStreams[i].mCurrentPos;

      // The seek(END) can be completed in the current stream.
      if (streamLength >= Abs(remaining)) {
        rv = stream->Seek(NS_SEEK_END, remaining);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        mStreams[i].mCurrentPos = streamLength + remaining;
        mCurrentStream = i;
        mStartedReadingCurrent = true;
        break;
      }

      // We are at the beginning of this stream.
      rv = stream->Seek(NS_SEEK_SET, 0);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      MOZ_ASSERT(remaining <= 0 && (remaining + (int64_t)streamLength) < 0);
      remaining += streamLength;
      mStreams[i].mCurrentPos = 0;
    }

    // Any other stream must be set to the end.
    for (--i; i >= 0; --i) {
      nsCOMPtr<nsISeekableStream> stream = mStreams[i].mSeekableStream;

      uint64_t avail;
      rv = AvailableMaybeSeek(mStreams[i], &avail);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      int64_t streamLength = avail + mStreams[i].mCurrentPos;

      rv = stream->Seek(NS_SEEK_END, 0);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mStreams[i].mCurrentPos = streamLength;
    }

    return NS_OK;
  }

  // other Seeks not implemented yet
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMultiplexInputStream::Tell(int64_t* aResult) {
  MutexAutoLock lock(mLock);

  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  int64_t ret64 = 0;
#ifdef DEBUG
  bool zeroFound = false;
#endif

  for (uint32_t i = 0; i < mStreams.Length(); ++i) {
    ret64 += mStreams[i].mCurrentPos;

#ifdef DEBUG
    // When we see 1 stream with currentPos = 0, all the remaining streams must
    // be set to 0 as well.
    MOZ_ASSERT_IF(zeroFound, mStreams[i].mCurrentPos == 0);
    if (mStreams[i].mCurrentPos == 0) {
      zeroFound = true;
    }
#endif
  }
  *aResult = ret64;

  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::SetEOF() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsMultiplexInputStream::CloseWithStatus(nsresult aStatus) { return Close(); }

// This class is used to inform nsMultiplexInputStream that it's time to execute
// the asyncWait callback.
class AsyncWaitRunnable final : public DiscardableRunnable {
  RefPtr<nsMultiplexInputStream> mStream;

 public:
  static void Create(nsMultiplexInputStream* aStream,
                     nsIEventTarget* aEventTarget) {
    RefPtr<AsyncWaitRunnable> runnable = new AsyncWaitRunnable(aStream);
    if (aEventTarget) {
      aEventTarget->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
    } else {
      runnable->Run();
    }
  }

  NS_IMETHOD
  Run() override {
    mStream->AsyncWaitCompleted();
    return NS_OK;
  }

 private:
  explicit AsyncWaitRunnable(nsMultiplexInputStream* aStream)
      : DiscardableRunnable("AsyncWaitRunnable"), mStream(aStream) {
    MOZ_ASSERT(aStream);
  }
};

NS_IMETHODIMP
nsMultiplexInputStream::AsyncWait(nsIInputStreamCallback* aCallback,
                                  uint32_t aFlags, uint32_t aRequestedCount,
                                  nsIEventTarget* aEventTarget) {
  {
    MutexAutoLock lock(mLock);

    // We must execute the callback also when the stream is closed.
    if (NS_FAILED(mStatus) && mStatus != NS_BASE_STREAM_CLOSED) {
      return mStatus;
    }

    if (NS_WARN_IF(mAsyncWaitCallback && aCallback &&
                   mAsyncWaitCallback != aCallback)) {
      return NS_ERROR_FAILURE;
    }

    mAsyncWaitCallback = aCallback;
    mAsyncWaitFlags = aFlags;
    mAsyncWaitRequestedCount = aRequestedCount;
    mAsyncWaitEventTarget = aEventTarget;
  }

  return AsyncWaitInternal();
}

nsresult nsMultiplexInputStream::AsyncWaitInternal() {
  nsCOMPtr<nsIAsyncInputStream> stream;
  nsIInputStreamCallback* asyncWaitCallback = nullptr;
  uint32_t asyncWaitFlags = 0;
  uint32_t asyncWaitRequestedCount = 0;
  nsCOMPtr<nsIEventTarget> asyncWaitEventTarget;

  {
    MutexAutoLock lock(mLock);

    // Let's take the first async stream if we are not already closed, and if
    // it has data to read or if it async.
    if (mStatus != NS_BASE_STREAM_CLOSED) {
      for (; mCurrentStream < mStreams.Length(); NextStream()) {
        stream = mStreams[mCurrentStream].mAsyncStream;
        if (stream) {
          break;
        }

        uint64_t avail = 0;
        nsresult rv = AvailableMaybeSeek(mStreams[mCurrentStream], &avail);
        if (rv == NS_BASE_STREAM_CLOSED || (NS_SUCCEEDED(rv) && avail == 0)) {
          // Nothing to read here. Let's move on.
          continue;
        }

        if (NS_FAILED(rv)) {
          return rv;
        }

        break;
      }
    }

    asyncWaitCallback = mAsyncWaitCallback ? this : nullptr;
    asyncWaitFlags = mAsyncWaitFlags;
    asyncWaitRequestedCount = mAsyncWaitRequestedCount;
    asyncWaitEventTarget = mAsyncWaitEventTarget;

    MOZ_ASSERT_IF(stream, NS_SUCCEEDED(mStatus));
  }

  // If we are here it's because we are already closed, or if the current stream
  // is not async. In both case we have to execute the callback.
  if (!stream) {
    if (asyncWaitCallback) {
      AsyncWaitRunnable::Create(this, asyncWaitEventTarget);
    }
    return NS_OK;
  }

  return stream->AsyncWait(asyncWaitCallback, asyncWaitFlags,
                           asyncWaitRequestedCount, asyncWaitEventTarget);
}

NS_IMETHODIMP
nsMultiplexInputStream::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  nsCOMPtr<nsIInputStreamCallback> callback;

  // When OnInputStreamReady is called, we could be in 2 scenarios:
  // a. there is something to read;
  // b. the stream is closed.
  // But if the stream is closed and it was not the last one, we must proceed
  // with the following stream in order to have something to read by the callee.

  {
    MutexAutoLock lock(mLock);

    // The callback has been nullified in the meantime.
    if (!mAsyncWaitCallback) {
      return NS_OK;
    }

    if (NS_SUCCEEDED(mStatus)) {
      uint64_t avail = 0;
      nsresult rv = NS_OK;
      // Only check `Available()` if `aStream` is actually the current stream,
      // otherwise we'll always want to re-poll, as we got the callback for the
      // wrong stream.
      if (mCurrentStream < mStreams.Length() &&
          aStream == mStreams[mCurrentStream].mAsyncStream) {
        rv = aStream->Available(&avail);
      }
      if (rv == NS_BASE_STREAM_CLOSED || (NS_SUCCEEDED(rv) && avail == 0)) {
        // This stream is either closed, has no data available, or is not the
        // current stream. If it is closed and current, move to the next stream,
        // otherwise re-wait on the current stream until it has data available
        // or becomes closed.
        // Unlike streams not implementing nsIAsyncInputStream, async streams
        // cannot use `Available() == 0` to indicate EOF, so we re-poll in that
        // situation.
        if (NS_FAILED(rv)) {
          NextStream();
        }

        // Unlock and invoke AsyncWaitInternal to wait again.  If this succeeds,
        // we'll be called again, otherwise fall through and notify.
        MutexAutoUnlock unlock(mLock);
        if (NS_SUCCEEDED(AsyncWaitInternal())) {
          return NS_OK;
        }
      }
    }

    mAsyncWaitCallback.swap(callback);
    mAsyncWaitEventTarget = nullptr;
  }

  return callback ? callback->OnInputStreamReady(this) : NS_OK;
}

void nsMultiplexInputStream::AsyncWaitCompleted() {
  nsCOMPtr<nsIInputStreamCallback> callback;

  {
    MutexAutoLock lock(mLock);

    // The callback has been nullified in the meantime.
    if (!mAsyncWaitCallback) {
      return;
    }

    mAsyncWaitCallback.swap(callback);
    mAsyncWaitEventTarget = nullptr;
  }

  callback->OnInputStreamReady(this);
}

nsresult nsMultiplexInputStreamConstructor(REFNSIID aIID, void** aResult) {
  *aResult = nullptr;

  RefPtr inst = MakeRefPtr<nsMultiplexInputStream>();

  return inst->QueryInterface(aIID, aResult);
}

void nsMultiplexInputStream::SerializedComplexity(uint32_t aMaxSize,
                                                  uint32_t* aSizeUsed,
                                                  uint32_t* aPipes,
                                                  uint32_t* aTransferables) {
  MutexAutoLock lock(mLock);
  bool serializeAsPipe = false;
  SerializedComplexityInternal(aMaxSize, aSizeUsed, aPipes, aTransferables,
                               &serializeAsPipe);
}

void nsMultiplexInputStream::SerializedComplexityInternal(
    uint32_t aMaxSize, uint32_t* aSizeUsed, uint32_t* aPipes,
    uint32_t* aTransferables, bool* aSerializeAsPipe) {
  mLock.AssertCurrentThreadOwns();
  CheckedUint32 totalSizeUsed = 0;
  CheckedUint32 totalPipes = 0;
  CheckedUint32 totalTransferables = 0;
  CheckedUint32 maxSize = aMaxSize;

  uint32_t streamCount = mStreams.Length();

  for (uint32_t index = 0; index < streamCount; index++) {
    uint32_t sizeUsed = 0;
    uint32_t pipes = 0;
    uint32_t transferables = 0;
    InputStreamHelper::SerializedComplexity(mStreams[index].mOriginalStream,
                                            maxSize.value(), &sizeUsed, &pipes,
                                            &transferables);

    MOZ_ASSERT(maxSize.value() >= sizeUsed);

    maxSize -= sizeUsed;
    MOZ_DIAGNOSTIC_ASSERT(maxSize.isValid());
    totalSizeUsed += sizeUsed;
    MOZ_DIAGNOSTIC_ASSERT(totalSizeUsed.isValid());
    totalPipes += pipes;
    MOZ_DIAGNOSTIC_ASSERT(totalPipes.isValid());
    totalTransferables += transferables;
    MOZ_DIAGNOSTIC_ASSERT(totalTransferables.isValid());
  }

  // If the combination of all streams when serialized independently is
  // sufficiently complex, we may choose to serialize it as a pipe to limit the
  // complexity of the payload.
  if (totalTransferables.value() == 0) {
    // If there are no transferables within our serialization, and it would
    // contain at least one pipe, serialize the entire payload as a pipe for
    // simplicity.
    *aSerializeAsPipe = totalSizeUsed.value() > 0 && totalPipes.value() > 0;
  } else {
    // Otherwise, we may want to still serialize in segments to take advantage
    // of the efficiency of serializing transferables. We'll only serialize as a
    // pipe if the total attachment count exceeds kMaxAttachmentThreshold.
    static constexpr uint32_t kMaxAttachmentThreshold = 8;
    CheckedUint32 totalAttachments = totalPipes + totalTransferables;
    *aSerializeAsPipe = !totalAttachments.isValid() ||
                        totalAttachments.value() > kMaxAttachmentThreshold;
  }

  if (*aSerializeAsPipe) {
    NS_WARNING(
        nsPrintfCString("Choosing to serialize multiplex stream as a pipe "
                        "(would be %u bytes, %u pipes, %u transferables)",
                        totalSizeUsed.value(), totalPipes.value(),
                        totalTransferables.value())
            .get());
    *aSizeUsed = 0;
    *aPipes = 1;
    *aTransferables = 0;
  } else {
    *aSizeUsed = totalSizeUsed.value();
    *aPipes = totalPipes.value();
    *aTransferables = totalTransferables.value();
  }
}

void nsMultiplexInputStream::Serialize(InputStreamParams& aParams,
                                       uint32_t aMaxSize, uint32_t* aSizeUsed) {
  MutexAutoLock lock(mLock);

  // Check if we should serialize this stream as a pipe to reduce complexity.
  uint32_t dummySizeUsed = 0, dummyPipes = 0, dummyTransferables = 0;
  bool serializeAsPipe = false;
  SerializedComplexityInternal(aMaxSize, &dummySizeUsed, &dummyPipes,
                               &dummyTransferables, &serializeAsPipe);
  if (serializeAsPipe) {
    *aSizeUsed = 0;
    MutexAutoUnlock unlock(mLock);
    InputStreamHelper::SerializeInputStreamAsPipe(this, aParams);
    return;
  }

  MultiplexInputStreamParams params;

  CheckedUint32 totalSizeUsed = 0;
  CheckedUint32 maxSize = aMaxSize;

  uint32_t streamCount = mStreams.Length();
  if (streamCount) {
    nsTArray<InputStreamParams>& streams = params.streams();

    streams.SetCapacity(streamCount);
    for (uint32_t index = 0; index < streamCount; index++) {
      uint32_t sizeUsed = 0;
      InputStreamHelper::SerializeInputStream(mStreams[index].mOriginalStream,
                                              *streams.AppendElement(),
                                              maxSize.value(), &sizeUsed);

      MOZ_ASSERT(maxSize.value() >= sizeUsed);

      maxSize -= sizeUsed;
      MOZ_DIAGNOSTIC_ASSERT(maxSize.isValid());

      totalSizeUsed += sizeUsed;
      MOZ_DIAGNOSTIC_ASSERT(totalSizeUsed.isValid());
    }
  }

  params.currentStream() = mCurrentStream;
  params.status() = mStatus;
  params.startedReadingCurrent() = mStartedReadingCurrent;

  aParams = std::move(params);

  MOZ_ASSERT(aSizeUsed);
  *aSizeUsed = totalSizeUsed.value();
}

bool nsMultiplexInputStream::Deserialize(const InputStreamParams& aParams) {
  if (aParams.type() != InputStreamParams::TMultiplexInputStreamParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  const MultiplexInputStreamParams& params =
      aParams.get_MultiplexInputStreamParams();

  const nsTArray<InputStreamParams>& streams = params.streams();

  uint32_t streamCount = streams.Length();
  for (uint32_t index = 0; index < streamCount; index++) {
    nsCOMPtr<nsIInputStream> stream =
        InputStreamHelper::DeserializeInputStream(streams[index]);
    if (!stream) {
      NS_WARNING("Deserialize failed!");
      return false;
    }

    if (NS_FAILED(AppendStream(stream))) {
      NS_WARNING("AppendStream failed!");
      return false;
    }
  }

  MutexAutoLock lock(mLock);
  mCurrentStream = params.currentStream();
  mStatus = params.status();
  mStartedReadingCurrent = params.startedReadingCurrent();

  return true;
}

NS_IMETHODIMP
nsMultiplexInputStream::GetCloneable(bool* aCloneable) {
  MutexAutoLock lock(mLock);
  // XXXnsm Cloning a multiplex stream which has started reading is not
  // permitted right now.
  if (mCurrentStream > 0 || mStartedReadingCurrent) {
    *aCloneable = false;
    return NS_OK;
  }

  uint32_t len = mStreams.Length();
  for (uint32_t i = 0; i < len; ++i) {
    nsCOMPtr<nsICloneableInputStream> cis =
        do_QueryInterface(mStreams[i].mBufferedStream);
    if (!cis || !cis->GetCloneable()) {
      *aCloneable = false;
      return NS_OK;
    }
  }

  *aCloneable = true;
  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::Clone(nsIInputStream** aClone) {
  MutexAutoLock lock(mLock);

  // XXXnsm Cloning a multiplex stream which has started reading is not
  // permitted right now.
  if (mCurrentStream > 0 || mStartedReadingCurrent) {
    return NS_ERROR_FAILURE;
  }

  RefPtr clone = MakeRefPtr<nsMultiplexInputStream>();

  nsresult rv;
  uint32_t len = mStreams.Length();
  for (uint32_t i = 0; i < len; ++i) {
    nsCOMPtr<nsICloneableInputStream> substream =
        do_QueryInterface(mStreams[i].mBufferedStream);
    if (NS_WARN_IF(!substream)) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIInputStream> clonedSubstream;
    rv = substream->Clone(getter_AddRefs(clonedSubstream));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv = clone->AppendStream(clonedSubstream);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  clone.forget(aClone);
  return NS_OK;
}

NS_IMETHODIMP
nsMultiplexInputStream::Length(int64_t* aLength) {
  MutexAutoLock lock(mLock);

  if (mCurrentStream > 0 || mStartedReadingCurrent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CheckedInt64 length = 0;
  nsresult retval = NS_OK;

  for (uint32_t i = 0, len = mStreams.Length(); i < len; ++i) {
    nsCOMPtr<nsIInputStreamLength> substream =
        do_QueryInterface(mStreams[i].mBufferedStream);
    if (!substream) {
      // Let's use available as fallback.
      uint64_t streamAvail = 0;
      nsresult rv = AvailableMaybeSeek(mStreams[i], &streamAvail);
      if (rv == NS_BASE_STREAM_CLOSED) {
        continue;
      }

      if (NS_WARN_IF(NS_FAILED(rv))) {
        mStatus = rv;
        return mStatus;
      }

      length += streamAvail;
      if (!length.isValid()) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      continue;
    }

    int64_t size = 0;
    nsresult rv = substream->Length(&size);
    if (rv == NS_BASE_STREAM_CLOSED) {
      continue;
    }

    if (rv == NS_ERROR_NOT_AVAILABLE) {
      return rv;
    }

    // If one stream blocks, we all block.
    if (rv != NS_BASE_STREAM_WOULD_BLOCK && NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // We want to return WOULD_BLOCK if there is 1 stream that blocks. But want
    // to see if there are other streams with length = -1.
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      retval = NS_BASE_STREAM_WOULD_BLOCK;
      continue;
    }

    // If one of the stream doesn't know the size, we all don't know the size.
    if (size == -1) {
      *aLength = -1;
      return NS_OK;
    }

    length += size;
    if (!length.isValid()) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  *aLength = length.value();
  return retval;
}

class nsMultiplexInputStream::AsyncWaitLengthHelper final
    : public nsIInputStreamLengthCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  AsyncWaitLengthHelper()
      : mStreamNotified(false), mLength(0), mNegativeSize(false) {}

  bool AddStream(nsIAsyncInputStreamLength* aStream) {
    return mPendingStreams.AppendElement(aStream, fallible);
  }

  bool AddSize(int64_t aSize) {
    MOZ_ASSERT(!mNegativeSize);

    mLength += aSize;
    return mLength.isValid();
  }

  void NegativeSize() {
    MOZ_ASSERT(!mNegativeSize);
    mNegativeSize = true;
  }

  nsresult Proceed(nsMultiplexInputStream* aParentStream,
                   nsIEventTarget* aEventTarget,
                   const MutexAutoLock& aProofOfLock) {
    MOZ_ASSERT(!mStream);

    // If we don't need to wait, let's inform the callback immediately.
    if (mPendingStreams.IsEmpty() || mNegativeSize) {
      RefPtr<nsMultiplexInputStream> parentStream = aParentStream;
      int64_t length = -1;
      if (!mNegativeSize && mLength.isValid()) {
        length = mLength.value();
      }
      nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
          "AsyncWaitLengthHelper", [parentStream, length]() {
            MutexAutoLock lock(parentStream->GetLock());
            parentStream->AsyncWaitCompleted(length, lock);
          });
      return aEventTarget->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
    }

    // Let's store the callback and the parent stream until we have
    // notifications from the async length streams.

    mStream = aParentStream;

    // Let's activate all the pending streams.
    for (nsIAsyncInputStreamLength* stream : mPendingStreams) {
      nsresult rv = stream->AsyncLengthWait(this, aEventTarget);
      if (rv == NS_BASE_STREAM_CLOSED) {
        continue;
      }

      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }

    return NS_OK;
  }

  NS_IMETHOD
  OnInputStreamLengthReady(nsIAsyncInputStreamLength* aStream,
                           int64_t aLength) override {
    MutexAutoLock lock(mStream->GetLock());

    MOZ_ASSERT(mPendingStreams.Contains(aStream));
    mPendingStreams.RemoveElement(aStream);

    // Already notified.
    if (mStreamNotified) {
      return NS_OK;
    }

    if (aLength == -1) {
      mNegativeSize = true;
    } else {
      mLength += aLength;
      if (!mLength.isValid()) {
        mNegativeSize = true;
      }
    }

    // We need to wait.
    if (!mNegativeSize && !mPendingStreams.IsEmpty()) {
      return NS_OK;
    }

    // Let's notify the parent stream.
    mStreamNotified = true;
    mStream->AsyncWaitCompleted(mNegativeSize ? -1 : mLength.value(), lock);
    return NS_OK;
  }

 private:
  ~AsyncWaitLengthHelper() = default;

  RefPtr<nsMultiplexInputStream> mStream;
  bool mStreamNotified;

  CheckedInt64 mLength;
  bool mNegativeSize;

  nsTArray<nsCOMPtr<nsIAsyncInputStreamLength>> mPendingStreams;
};

NS_IMPL_ISUPPORTS(nsMultiplexInputStream::AsyncWaitLengthHelper,
                  nsIInputStreamLengthCallback)

NS_IMETHODIMP
nsMultiplexInputStream::AsyncLengthWait(nsIInputStreamLengthCallback* aCallback,
                                        nsIEventTarget* aEventTarget) {
  if (NS_WARN_IF(!aEventTarget)) {
    return NS_ERROR_NULL_POINTER;
  }

  MutexAutoLock lock(mLock);

  if (mCurrentStream > 0 || mStartedReadingCurrent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!aCallback) {
    mAsyncWaitLengthCallback = nullptr;
    return NS_OK;
  }

  // We have a pending operation! Let's use this instead of creating a new one.
  if (mAsyncWaitLengthHelper) {
    mAsyncWaitLengthCallback = aCallback;
    return NS_OK;
  }

  RefPtr<AsyncWaitLengthHelper> helper = new AsyncWaitLengthHelper();

  for (uint32_t i = 0, len = mStreams.Length(); i < len; ++i) {
    nsCOMPtr<nsIAsyncInputStreamLength> asyncStream =
        do_QueryInterface(mStreams[i].mBufferedStream);
    if (asyncStream) {
      if (NS_WARN_IF(!helper->AddStream(asyncStream))) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      continue;
    }

    nsCOMPtr<nsIInputStreamLength> stream =
        do_QueryInterface(mStreams[i].mBufferedStream);
    if (!stream) {
      // Let's use available as fallback.
      uint64_t streamAvail = 0;
      nsresult rv = AvailableMaybeSeek(mStreams[i], &streamAvail);
      if (rv == NS_BASE_STREAM_CLOSED) {
        continue;
      }

      if (NS_WARN_IF(NS_FAILED(rv))) {
        mStatus = rv;
        return mStatus;
      }

      if (NS_WARN_IF(!helper->AddSize(streamAvail))) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      continue;
    }

    int64_t size = 0;
    nsresult rv = stream->Length(&size);
    if (rv == NS_BASE_STREAM_CLOSED) {
      continue;
    }

    MOZ_ASSERT(rv != NS_BASE_STREAM_WOULD_BLOCK,
               "A nsILengthInutStream returns NS_BASE_STREAM_WOULD_BLOCK but "
               "it doesn't implement nsIAsyncInputStreamLength.");

    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (size == -1) {
      helper->NegativeSize();
      break;
    }

    if (NS_WARN_IF(!helper->AddSize(size))) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  nsresult rv = helper->Proceed(this, aEventTarget, lock);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mAsyncWaitLengthHelper = helper;
  mAsyncWaitLengthCallback = aCallback;
  return NS_OK;
}

void nsMultiplexInputStream::AsyncWaitCompleted(
    int64_t aLength, const MutexAutoLock& aProofOfLock) {
  mLock.AssertCurrentThreadOwns();

  nsCOMPtr<nsIInputStreamLengthCallback> callback;
  callback.swap(mAsyncWaitLengthCallback);

  mAsyncWaitLengthHelper = nullptr;

  // Already canceled.
  if (!callback) {
    return;
  }

  MutexAutoUnlock unlock(mLock);
  callback->OnInputStreamLengthReady(this, aLength);
}

#define MAYBE_UPDATE_VALUE_REAL(x, y) \
  if (y) {                            \
    ++x;                              \
  }

#define MAYBE_UPDATE_VALUE(x, y)                                        \
  {                                                                     \
    nsCOMPtr<y> substream = do_QueryInterface(aStream.mBufferedStream); \
    MAYBE_UPDATE_VALUE_REAL(x, substream)                               \
  }

#define MAYBE_UPDATE_BOOL(x, y)                                         \
  if (!x) {                                                             \
    nsCOMPtr<y> substream = do_QueryInterface(aStream.mBufferedStream); \
    if (substream) {                                                    \
      x = true;                                                         \
    }                                                                   \
  }

void nsMultiplexInputStream::UpdateQIMap(StreamData& aStream) {
  auto length = mStreams.Length();

  MAYBE_UPDATE_VALUE_REAL(mSeekableStreams, aStream.mSeekableStream)
  mIsSeekableStream = (mSeekableStreams == length);
  MAYBE_UPDATE_VALUE(mIPCSerializableStreams, nsIIPCSerializableInputStream)
  mIsIPCSerializableStream = (mIPCSerializableStreams == length);
  MAYBE_UPDATE_VALUE(mCloneableStreams, nsICloneableInputStream)
  mIsCloneableStream = (mCloneableStreams == length);
  // nsMultiplexInputStream is nsIAsyncInputStream if at least 1 of the
  // substream implements that interface
  if (!mIsAsyncInputStream && aStream.mAsyncStream) {
    mIsAsyncInputStream = true;
  }
  MAYBE_UPDATE_BOOL(mIsInputStreamLength, nsIInputStreamLength)
  MAYBE_UPDATE_BOOL(mIsAsyncInputStreamLength, nsIAsyncInputStreamLength)
}

#undef MAYBE_UPDATE_VALUE
#undef MAYBE_UPDATE_VALUE_REAL
#undef MAYBE_UPDATE_BOOL

bool nsMultiplexInputStream::IsSeekable() const { return mIsSeekableStream; }

bool nsMultiplexInputStream::IsIPCSerializable() const {
  return mIsIPCSerializableStream;
}

bool nsMultiplexInputStream::IsCloneable() const { return mIsCloneableStream; }

bool nsMultiplexInputStream::IsAsyncInputStream() const {
  // nsMultiplexInputStream is nsIAsyncInputStream if at least 1 of the
  // substream implements that interface.
  return mIsAsyncInputStream;
}

bool nsMultiplexInputStream::IsInputStreamLength() const {
  return mIsInputStreamLength;
}

bool nsMultiplexInputStream::IsAsyncInputStreamLength() const {
  return mIsAsyncInputStreamLength;
}
