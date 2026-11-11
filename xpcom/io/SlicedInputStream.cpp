/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SlicedInputStream.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsISeekableStream.h"
#include "nsStreamUtils.h"

namespace mozilla {

using namespace ipc;

NS_IMPL_ADDREF(SlicedInputStream);
NS_IMPL_RELEASE(SlicedInputStream);

NS_INTERFACE_MAP_BEGIN(SlicedInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICloneableInputStream,
                                     mWeakCloneableInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(
      nsIIPCSerializableInputStream,
      mWeakIPCSerializableInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISeekableStream,
                                     mWeakSeekableInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsITellableStream,
                                     mWeakTellableInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIAsyncInputStream,
                                     mWeakAsyncInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamCallback,
                                     mWeakAsyncInputStream || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamLength,
                                     mWeakInputStreamLength || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(
      nsIAsyncInputStreamLength, mWeakAsyncInputStreamLength || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(
      nsIInputStreamLengthCallback,
      mWeakAsyncInputStreamLength || !mInputStream)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInputStream)
NS_INTERFACE_MAP_END

// It is highly unlikely for a stream to exceed INT64_MAX bytes in length, so we
// clamp these values here to allow callers as well as SlicedInputStream to
// avoid unnecessary integer overflow checks. Out of range start/length values
// should behave as expected in all realistic situations.
//
// Some stream APIs use an int64_t (e.g. Tell), so we use INT64_MAX as the
// maximum internal offset for start/end.
static constexpr uint64_t kMaxStreamPos = INT64_MAX;

SlicedInputStream::SlicedInputStream(
    already_AddRefed<nsIInputStream> aInputStream, uint64_t aStart,
    uint64_t aLength)
    : mWeakCloneableInputStream(nullptr),
      mWeakIPCSerializableInputStream(nullptr),
      mWeakSeekableInputStream(nullptr),
      mWeakTellableInputStream(nullptr),
      mWeakAsyncInputStream(nullptr),
      mWeakInputStreamLength(nullptr),
      mWeakAsyncInputStreamLength(nullptr),
      mStart(std::clamp<uint64_t>(aStart, 0, kMaxStreamPos)),
      mLength(std::clamp<uint64_t>(aLength, 0, kMaxStreamPos - mStart)),
      mCurPos(0),
      mClosed(false),
      mAsyncWaitFlags(0),
      mAsyncWaitRequestedCount(0),
      mMutex("SlicedInputStream::mMutex") {
  nsCOMPtr<nsIInputStream> inputStream = std::move(aInputStream);
  SetSourceStream(inputStream.forget());
}

SlicedInputStream::SlicedInputStream()
    : mWeakCloneableInputStream(nullptr),
      mWeakIPCSerializableInputStream(nullptr),
      mWeakSeekableInputStream(nullptr),
      mWeakTellableInputStream(nullptr),
      mWeakAsyncInputStream(nullptr),
      mWeakInputStreamLength(nullptr),
      mWeakAsyncInputStreamLength(nullptr),
      mStart(0),
      mLength(0),
      mCurPos(0),
      mClosed(false),
      mAsyncWaitFlags(0),
      mAsyncWaitRequestedCount(0),
      mMutex("SlicedInputStream::mMutex") {}

SlicedInputStream::~SlicedInputStream() = default;

void SlicedInputStream::SetSourceStream(
    already_AddRefed<nsIInputStream> aInputStream) {
  MOZ_ASSERT(!mInputStream);

  mInputStream = std::move(aInputStream);

  nsCOMPtr<nsICloneableInputStream> cloneableStream =
      do_QueryInterface(mInputStream);
  if (cloneableStream && SameCOMIdentity(mInputStream, cloneableStream)) {
    mWeakCloneableInputStream = cloneableStream;
  }

  nsCOMPtr<nsIIPCSerializableInputStream> serializableStream =
      do_QueryInterface(mInputStream);
  if (serializableStream && SameCOMIdentity(mInputStream, serializableStream)) {
    mWeakIPCSerializableInputStream = serializableStream;
  }

  nsCOMPtr<nsISeekableStream> seekableStream = do_QueryInterface(mInputStream);
  if (seekableStream && SameCOMIdentity(mInputStream, seekableStream)) {
    mWeakSeekableInputStream = seekableStream;
  }

  nsCOMPtr<nsITellableStream> tellableStream = do_QueryInterface(mInputStream);
  if (tellableStream && SameCOMIdentity(mInputStream, tellableStream)) {
    mWeakTellableInputStream = tellableStream;
  }

  nsCOMPtr<nsIAsyncInputStream> asyncInputStream =
      do_QueryInterface(mInputStream);
  if (asyncInputStream && SameCOMIdentity(mInputStream, asyncInputStream)) {
    mWeakAsyncInputStream = asyncInputStream;
  }

  nsCOMPtr<nsIInputStreamLength> streamLength = do_QueryInterface(mInputStream);
  if (streamLength && SameCOMIdentity(mInputStream, streamLength)) {
    mWeakInputStreamLength = streamLength;
  }

  nsCOMPtr<nsIAsyncInputStreamLength> asyncStreamLength =
      do_QueryInterface(mInputStream);
  if (asyncStreamLength && SameCOMIdentity(mInputStream, asyncStreamLength)) {
    mWeakAsyncInputStreamLength = asyncStreamLength;
  }
}

uint64_t SlicedInputStream::AdjustRange(uint64_t aRange) {
  CheckedUint64 range(aRange);
  range += mCurPos;

  // Let's remove extra length from the end.
  if (range.isValid() && range.value() > mStart + mLength) {
    aRange -= XPCOM_MIN((uint64_t)aRange, range.value() - (mStart + mLength));
  }

  // Let's remove extra length from the begin.
  if (mCurPos < mStart) {
    aRange -= XPCOM_MIN((uint64_t)aRange, mStart - mCurPos);
  }

  return aRange;
}

// nsIInputStream interface

NS_IMETHODIMP
SlicedInputStream::Close() {
  NS_ENSURE_STATE(mInputStream);

  mClosed = true;
  return mInputStream->Close();
}

NS_IMETHODIMP
SlicedInputStream::Available(uint64_t* aLength) {
  NS_ENSURE_STATE(mInputStream);

  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  nsresult rv = mInputStream->Available(aLength);
  if (rv == NS_BASE_STREAM_CLOSED) {
    mClosed = true;
    return rv;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  *aLength = AdjustRange(*aLength);
  return NS_OK;
}

NS_IMETHODIMP
SlicedInputStream::StreamStatus() {
  NS_ENSURE_STATE(mInputStream);

  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  nsresult rv = mInputStream->StreamStatus();
  if (rv == NS_BASE_STREAM_CLOSED) {
    mClosed = true;
  }
  return rv;
}

NS_IMETHODIMP
SlicedInputStream::Read(char* aBuffer, uint32_t aCount, uint32_t* aReadCount) {
  *aReadCount = 0;

  if (mClosed) {
    return NS_OK;
  }

  if (mCurPos < mStart) {
    nsCOMPtr<nsISeekableStream> seekableStream =
        do_QueryInterface(mInputStream);
    if (seekableStream) {
      nsresult rv =
          seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, mStart);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mCurPos = mStart;
    } else {
      char buf[4096];
      while (mCurPos < mStart) {
        uint32_t bytesRead;
        uint64_t bufCount = XPCOM_MIN(mStart - mCurPos, (uint64_t)sizeof(buf));
        nsresult rv = mInputStream->Read(buf, bufCount, &bytesRead);
        if (NS_SUCCEEDED(rv) && bytesRead == 0) {
          mClosed = true;
          return rv;
        }

        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        mCurPos += bytesRead;
      }
    }
  }

  // Let's reduce aCount in case it's too big.
  if (mCurPos + aCount > mStart + mLength) {
    aCount = mStart + mLength - mCurPos;
  }

  // Nothing else to read.
  if (!aCount) {
    return NS_OK;
  }

  nsresult rv = mInputStream->Read(aBuffer, aCount, aReadCount);
  if (NS_SUCCEEDED(rv) && *aReadCount == 0) {
    mClosed = true;
    return rv;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mCurPos += *aReadCount;
  return NS_OK;
}

NS_IMETHODIMP
SlicedInputStream::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                                uint32_t aCount, uint32_t* aResult) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
SlicedInputStream::IsNonBlocking(bool* aNonBlocking) {
  NS_ENSURE_STATE(mInputStream);
  return mInputStream->IsNonBlocking(aNonBlocking);
}

// nsICloneableInputStream interface

NS_IMETHODIMP
SlicedInputStream::GetCloneable(bool* aCloneable) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakCloneableInputStream);

  *aCloneable = true;
  return NS_OK;
}

NS_IMETHODIMP
SlicedInputStream::Clone(nsIInputStream** aResult) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakCloneableInputStream);

  nsCOMPtr<nsIInputStream> clonedStream;
  nsresult rv = mWeakCloneableInputStream->Clone(getter_AddRefs(clonedStream));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIInputStream> sis =
      new SlicedInputStream(clonedStream.forget(), mStart, mLength);

  sis.forget(aResult);
  return NS_OK;
}

// nsIAsyncInputStream interface

NS_IMETHODIMP
SlicedInputStream::CloseWithStatus(nsresult aStatus) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakAsyncInputStream);

  mClosed = true;
  return mWeakAsyncInputStream->CloseWithStatus(aStatus);
}

NS_IMETHODIMP
SlicedInputStream::AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
                             uint32_t aRequestedCount,
                             nsIEventTarget* aEventTarget) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakAsyncInputStream);

  nsCOMPtr<nsIInputStreamCallback> callback = aCallback ? this : nullptr;

  uint32_t flags = aFlags;
  uint32_t requestedCount = aRequestedCount;

  {
    MutexAutoLock lock(mMutex);

    if (NS_WARN_IF(mAsyncWaitCallback && aCallback &&
                   mAsyncWaitCallback != aCallback)) {
      return NS_ERROR_FAILURE;
    }

    mAsyncWaitCallback = aCallback;

    // If we haven't started retrieving data, let's see if we can seek.
    // If we cannot seek, we will do consecutive reads.
    if (mCurPos < mStart && mWeakSeekableInputStream) {
      nsresult rv = mWeakSeekableInputStream->Seek(
          nsISeekableStream::NS_SEEK_SET, mStart);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mCurPos = mStart;
    }

    mAsyncWaitFlags = aFlags;
    mAsyncWaitRequestedCount = aRequestedCount;
    mAsyncWaitEventTarget = aEventTarget;

    // If we are not at the right position, let's do an asyncWait just internal.
    if (mCurPos < mStart) {
      flags = 0;
      requestedCount = mStart - mCurPos;
    }
  }

  return mWeakAsyncInputStream->AsyncWait(callback, flags, requestedCount,
                                          aEventTarget);
}

// nsIInputStreamCallback

NS_IMETHODIMP
SlicedInputStream::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  MOZ_ASSERT(mInputStream);
  MOZ_ASSERT(mWeakAsyncInputStream);
  MOZ_ASSERT(mWeakAsyncInputStream == aStream);

  nsCOMPtr<nsIInputStreamCallback> callback;
  uint32_t asyncWaitFlags = 0;
  uint32_t asyncWaitRequestedCount = 0;
  nsCOMPtr<nsIEventTarget> asyncWaitEventTarget;

  {
    MutexAutoLock lock(mMutex);

    // We have been canceled in the meanwhile.
    if (!mAsyncWaitCallback) {
      return NS_OK;
    }

    auto raii = MakeScopeExit([&] {
      mMutex.AssertCurrentThreadOwns();
      mAsyncWaitCallback = nullptr;
      mAsyncWaitEventTarget = nullptr;
    });

    asyncWaitFlags = mAsyncWaitFlags;
    asyncWaitRequestedCount = mAsyncWaitRequestedCount;
    asyncWaitEventTarget = mAsyncWaitEventTarget;

    // If at the end of this locked block, the callback is not null, it will be
    // executed, otherwise, we are going to exec another AsyncWait().
    callback = mAsyncWaitCallback;

    if (mCurPos < mStart) {
      char buf[4096];
      nsresult rv = NS_OK;
      while (mCurPos < mStart) {
        uint32_t bytesRead;
        uint64_t bufCount = XPCOM_MIN(mStart - mCurPos, (uint64_t)sizeof(buf));
        rv = mInputStream->Read(buf, bufCount, &bytesRead);
        if (NS_SUCCEEDED(rv) && bytesRead == 0) {
          mClosed = true;
          break;
        }

        if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
          asyncWaitFlags = 0;
          asyncWaitRequestedCount = mStart - mCurPos;
          // Here we want to exec another AsyncWait().
          callback = nullptr;
          break;
        }

        if (NS_WARN_IF(NS_FAILED(rv))) {
          break;
        }

        mCurPos += bytesRead;
      }

      // Now we are ready to do the 'real' asyncWait.
      if (mCurPos >= mStart) {
        // We don't want to nullify the callback now, because it will be needed
        // at the next ::OnInputStreamReady.
        raii.release();
        callback = nullptr;
      }
    }
  }

  if (callback) {
    return callback->OnInputStreamReady(this);
  }

  return mWeakAsyncInputStream->AsyncWait(
      this, asyncWaitFlags, asyncWaitRequestedCount, asyncWaitEventTarget);
}

// nsIIPCSerializableInputStream

void SlicedInputStream::SerializedComplexity(uint32_t aMaxSize,
                                             uint32_t* aSizeUsed,
                                             uint32_t* aPipes,
                                             uint32_t* aTransferables) {
  InputStreamHelper::SerializedComplexity(mInputStream, aMaxSize, aSizeUsed,
                                          aPipes, aTransferables);

  // If we're going to be serializing a pipe to transfer the sliced data, and we
  // are getting no efficiency improvements from transferables, stream this
  // sliced input stream directly as a pipe to avoid streaming data which will
  // be sliced off anyway.
  if (*aPipes > 0 && *aTransferables == 0) {
    *aSizeUsed = 0;
    *aPipes = 1;
    *aTransferables = 0;
  }
}

void SlicedInputStream::Serialize(mozilla::ipc::InputStreamParams& aParams,
                                  uint32_t aMaxSize, uint32_t* aSizeUsed) {
  MOZ_ASSERT(mInputStream);
  MOZ_ASSERT(mWeakIPCSerializableInputStream);

  uint32_t sizeUsed = 0, pipes = 0, transferables = 0;
  SerializedComplexity(aMaxSize, &sizeUsed, &pipes, &transferables);
  if (pipes > 0 && transferables == 0) {
    InputStreamHelper::SerializeInputStreamAsPipe(this, aParams);
    return;
  }

  SlicedInputStreamParams params;
  InputStreamHelper::SerializeInputStream(mInputStream, params.stream(),
                                          aMaxSize, aSizeUsed);
  params.start() = mStart;
  params.length() = mLength;
  params.curPos() = mCurPos;
  params.closed() = mClosed;

  aParams = params;
}

bool SlicedInputStream::Deserialize(
    const mozilla::ipc::InputStreamParams& aParams) {
  MOZ_ASSERT(!mInputStream);
  MOZ_ASSERT(!mWeakIPCSerializableInputStream);

  if (aParams.type() != InputStreamParams::TSlicedInputStreamParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  const SlicedInputStreamParams& params = aParams.get_SlicedInputStreamParams();

  if (params.start() > kMaxStreamPos ||
      params.length() > kMaxStreamPos - params.start()) {
    return false;
  }
  if (params.curPos() > params.start() + params.length()) {
    return false;
  }

  nsCOMPtr<nsIInputStream> stream =
      InputStreamHelper::DeserializeInputStream(params.stream());
  if (!stream) {
    NS_WARNING("Deserialize failed!");
    return false;
  }

  SetSourceStream(stream.forget());

  mStart = params.start();
  mLength = params.length();
  mCurPos = params.curPos();
  mClosed = params.closed();

  return true;
}

// nsISeekableStream

NS_IMETHODIMP
SlicedInputStream::Seek(int32_t aWhence, int64_t aOffset) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakSeekableInputStream);

  int64_t offset;
  nsresult rv;

  switch (aWhence) {
    case NS_SEEK_SET:
      offset = mStart + aOffset;
      break;
    case NS_SEEK_CUR:
      // mCurPos could be lower than mStart if the reading has not started yet.
      offset = XPCOM_MAX(mStart, mCurPos) + aOffset;
      break;
    case NS_SEEK_END: {
      uint64_t available;
      rv = mInputStream->Available(&available);
      if (rv == NS_BASE_STREAM_CLOSED) {
        mClosed = true;
        return rv;
      }

      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      offset = XPCOM_MIN(mStart + mLength, available) + aOffset;
      break;
    }
    default:
      return NS_ERROR_ILLEGAL_VALUE;
  }

  if (offset < (int64_t)mStart || offset > (int64_t)(mStart + mLength)) {
    return NS_ERROR_INVALID_ARG;
  }

  rv = mWeakSeekableInputStream->Seek(NS_SEEK_SET, offset);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mCurPos = offset;
  return NS_OK;
}

NS_IMETHODIMP
SlicedInputStream::SetEOF() {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakSeekableInputStream);

  mClosed = true;
  return mWeakSeekableInputStream->SetEOF();
}

// nsITellableStream

NS_IMETHODIMP
SlicedInputStream::Tell(int64_t* aResult) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakTellableInputStream);

  int64_t tell = 0;

  nsresult rv = mWeakTellableInputStream->Tell(&tell);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (tell < (int64_t)mStart) {
    *aResult = 0;
    return NS_OK;
  }

  *aResult = tell - mStart;
  if (*aResult > (int64_t)mLength) {
    *aResult = mLength;
  }

  return NS_OK;
}

// nsIInputStreamLength

NS_IMETHODIMP
SlicedInputStream::Length(int64_t* aLength) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakInputStreamLength);

  nsresult rv = mWeakInputStreamLength->Length(aLength);
  if (rv == NS_BASE_STREAM_CLOSED) {
    mClosed = true;
    return rv;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (*aLength == -1) {
    return NS_OK;
  }

  *aLength = (int64_t)AdjustRange((uint64_t)*aLength);
  return NS_OK;
}

// nsIAsyncInputStreamLength

NS_IMETHODIMP
SlicedInputStream::AsyncLengthWait(nsIInputStreamLengthCallback* aCallback,
                                   nsIEventTarget* aEventTarget) {
  NS_ENSURE_STATE(mInputStream);
  NS_ENSURE_STATE(mWeakAsyncInputStreamLength);

  nsCOMPtr<nsIInputStreamLengthCallback> callback = aCallback ? this : nullptr;
  {
    MutexAutoLock lock(mMutex);
    mAsyncWaitLengthCallback = aCallback;
  }

  return mWeakAsyncInputStreamLength->AsyncLengthWait(callback, aEventTarget);
}

// nsIInputStreamLengthCallback

NS_IMETHODIMP
SlicedInputStream::OnInputStreamLengthReady(nsIAsyncInputStreamLength* aStream,
                                            int64_t aLength) {
  MOZ_ASSERT(mInputStream);
  MOZ_ASSERT(mWeakAsyncInputStreamLength);
  MOZ_ASSERT(mWeakAsyncInputStreamLength == aStream);

  nsCOMPtr<nsIInputStreamLengthCallback> callback;
  {
    MutexAutoLock lock(mMutex);

    // We have been canceled in the meanwhile.
    if (!mAsyncWaitLengthCallback) {
      return NS_OK;
    }

    callback.swap(mAsyncWaitLengthCallback);
  }

  if (aLength != -1) {
    aLength = (int64_t)AdjustRange((uint64_t)aLength);
  }

  MOZ_ASSERT(callback);
  return callback->OnInputStreamLengthReady(this, aLength);
}

}  // namespace mozilla
