/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheFileInputStream.h"

#include <algorithm>

#include "CacheFile.h"
#include "CacheLog.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"

namespace mozilla::net {

NS_IMPL_ADDREF(CacheFileInputStream)
NS_IMETHODIMP_(MozExternalRefCountType)
CacheFileInputStream::Release() {
  MOZ_ASSERT(0 != mRefCnt, "dup release");
  // CacheFile can add a reference from its raw mInputs pointer while holding
  // its lock, so the drop to the CacheFile-owned reference must happen under
  // that lock.  Once RemoveInput() has run, nothing else can add a reference.
  auto [decremented, count] = mRefCnt.DecrementWithLimit<2>();
  if (decremented) {
    NS_LOG_RELEASE(this, count, "CacheFileInputStream");
    return count;
  }

  if (count == 1) {
    count = --mRefCnt;
    NS_LOG_RELEASE(this, count, "CacheFileInputStream");
    mRefCnt = 1;
    delete (this);
    return 0;
  }

  CacheFileAutoLock lock(mFile);
  count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "CacheFileInputStream");
  if (count == 1) {
    mFile->RemoveInput(this, mStatus);
  }

  return count;
}

NS_INTERFACE_MAP_BEGIN(CacheFileInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncInputStream)
  NS_INTERFACE_MAP_ENTRY(nsISeekableStream)
  NS_INTERFACE_MAP_ENTRY(nsITellableStream)
  NS_INTERFACE_MAP_ENTRY(mozilla::net::CacheFileChunkListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInputStream)
NS_INTERFACE_MAP_END

CacheFileInputStream::CacheFileInputStream(CacheFile* aFile,
                                           nsISupports* aEntry,
                                           bool aAlternativeData)
    : mFile(aFile),
      mPos(0),
      mStatus(NS_OK),
      mClosed(false),
      mInReadSegments(false),
      mWaitingForUpdate(false),
      mAlternativeData(aAlternativeData),
      mListeningForChunk(-1),
      mCallbackFlags(0),
      mCacheEntryHandle(aEntry) {
  LOG(("CacheFileInputStream::CacheFileInputStream() [this=%p]", this));

  if (mAlternativeData) {
    mPos = mFile->mAltDataOffset;
  }
}

CacheFileInputStream::~CacheFileInputStream() {
  LOG(("CacheFileInputStream::~CacheFileInputStream() [this=%p]", this));
  MOZ_ASSERT(!mInReadSegments);
}

// nsIInputStream
NS_IMETHODIMP
CacheFileInputStream::Close() {
  LOG(("CacheFileInputStream::Close() [this=%p]", this));
  return CloseWithStatus(NS_OK);
}

NS_IMETHODIMP
CacheFileInputStream::Available(uint64_t* _retval) {
  CacheFileAutoLock lock(mFile);

  if (mClosed) {
    LOG(
        ("CacheFileInputStream::Available() - Stream is closed. [this=%p, "
         "status=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(mStatus)));
    return NS_FAILED(mStatus) ? mStatus : NS_BASE_STREAM_CLOSED;
  }

  EnsureCorrectChunk(false);
  if (NS_FAILED(mStatus)) {
    LOG(
        ("CacheFileInputStream::Available() - EnsureCorrectChunk failed. "
         "[this=%p, status=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(mStatus)));
    return mStatus;
  }

  nsresult rv = NS_OK;
  *_retval = 0;

  if (mChunk) {
    int64_t canRead = mFile->BytesFromChunk(mChunk->Index(), mAlternativeData);
    canRead -= (mPos % kChunkSize);

    if (canRead > 0) {
      *_retval = canRead;
    } else if (canRead == 0 && !mFile->OutputStreamExists(mAlternativeData)) {
      rv = NS_BASE_STREAM_CLOSED;
    }
  }

  LOG(("CacheFileInputStream::Available() [this=%p, retval=%" PRIu64
       ", rv=0x%08" PRIx32 "]",
       this, *_retval, static_cast<uint32_t>(rv)));

  return rv;
}

NS_IMETHODIMP
CacheFileInputStream::StreamStatus() {
  CacheFileAutoLock lock(mFile);

  if (mClosed) {
    LOG(
        ("CacheFileInputStream::StreamStatus() - Stream is closed. [this=%p, "
         "status=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(mStatus)));
    return NS_FAILED(mStatus) ? mStatus : NS_BASE_STREAM_CLOSED;
  }

  return NS_OK;
}

NS_IMETHODIMP
CacheFileInputStream::Read(char* aBuf, uint32_t aCount, uint32_t* _retval) {
  LOG(("CacheFileInputStream::Read() [this=%p, count=%d]", this, aCount));
  return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, _retval);
}

NS_IMETHODIMP
CacheFileInputStream::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                                   uint32_t aCount, uint32_t* _retval) {
  CacheFileAutoLock lock(mFile);

  LOG(("CacheFileInputStream::ReadSegments() [this=%p, count=%d]", this,
       aCount));

  nsresult rv = NS_OK;

  *_retval = 0;

  if (mInReadSegments) {
    LOG(
        ("CacheFileInputStream::ReadSegments() - Cannot be called while the "
         "stream is in ReadSegments!"));
    return NS_ERROR_UNEXPECTED;
  }

  if (mClosed) {
    LOG(
        ("CacheFileInputStream::ReadSegments() - Stream is closed. [this=%p, "
         "status=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(mStatus)));

    if (NS_FAILED(mStatus)) {
      return mStatus;
    }

    return NS_OK;
  }

  if (aCount == 0) {
    return NS_OK;
  }

  EnsureCorrectChunk(false);

  while (true) {
    if (NS_FAILED(mStatus)) return mStatus;

    if (!mChunk) {
      if (mListeningForChunk == -1) {
        return NS_OK;
      }
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    CacheFileChunkReadHandle hnd = mChunk->GetReadHandle();
    int64_t canRead = CanRead(&hnd);
    if (NS_FAILED(mStatus)) {
      return mStatus;
    }

    if (canRead < 0) {
      // file was truncated ???
      MOZ_ASSERT(false, "SetEOF is currenty not implemented?!");
      rv = NS_OK;
    } else if (canRead > 0) {
      uint32_t toRead = std::min(static_cast<uint32_t>(canRead), aCount);
      uint32_t read;
      const char* buf = hnd.Buf() + (mPos - hnd.Offset());

      mInReadSegments = true;
      lock.Unlock();

      rv = aWriter(this, aClosure, buf, *_retval, toRead, &read);

      lock.Lock();
      mInReadSegments = false;

      if (NS_SUCCEEDED(rv)) {
        MOZ_ASSERT(read <= toRead,
                   "writer should not write more than we asked it to write");

        *_retval += read;
        mPos += read;
        aCount -= read;

        if (!mClosed) {
          // The last chunk is released after the caller closes this stream.
          EnsureCorrectChunk(false);

          if (mChunk && aCount) {
            // Check whether there is more data available to read.
            continue;
          }
        }
      }

      if (mClosed) {
        // The stream was closed from aWriter, do the cleanup.
        CleanUp();
      }

      rv = NS_OK;
    } else {
      if (*_retval == 0 && mFile->OutputStreamExists(mAlternativeData)) {
        rv = NS_BASE_STREAM_WOULD_BLOCK;
      } else {
        rv = NS_OK;
      }
    }

    break;
  }

  LOG(("CacheFileInputStream::ReadSegments() [this=%p, rv=0x%08" PRIx32
       ", retval=%d]",
       this, static_cast<uint32_t>(rv), *_retval));

  return rv;
}

NS_IMETHODIMP
CacheFileInputStream::IsNonBlocking(bool* _retval) {
  *_retval = true;
  return NS_OK;
}

// nsIAsyncInputStream
NS_IMETHODIMP
CacheFileInputStream::CloseWithStatus(nsresult aStatus) {
  CacheFileAutoLock lock(mFile);

  LOG(("CacheFileInputStream::CloseWithStatus() [this=%p, aStatus=0x%08" PRIx32
       "]",
       this, static_cast<uint32_t>(aStatus)));

  CloseWithStatusLocked(aStatus);
  return NS_OK;
}

void CacheFileInputStream::CloseWithStatusLocked(nsresult aStatus) {
  LOG(
      ("CacheFileInputStream::CloseWithStatusLocked() [this=%p, "
       "aStatus=0x%08" PRIx32 "]",
       this, static_cast<uint32_t>(aStatus)));

  if (mClosed) {
    // We notify listener and null out mCallback immediately after closing
    // the stream. If we're in ReadSegments we postpone notification until we
    // step out from ReadSegments. So if the stream is already closed the
    // following assertion must be true.
    MOZ_ASSERT(!mCallback || mInReadSegments);
    return;
  }

  mClosed = true;
  mStatus = NS_FAILED(aStatus) ? aStatus : NS_BASE_STREAM_CLOSED;

  if (!mInReadSegments) {
    CleanUp();
  }
}

void CacheFileInputStream::CleanUp() {
  MOZ_ASSERT(!mInReadSegments);
  MOZ_ASSERT(mClosed);

  if (mChunk) {
    ReleaseChunk();
  }

  // TODO propagate error from input stream to other streams ???

  MaybeNotifyListener();

  mFile->ReleaseOutsideLock(std::move(mCacheEntryHandle));
}

NS_IMETHODIMP
CacheFileInputStream::AsyncWait(nsIInputStreamCallback* aCallback,
                                uint32_t aFlags, uint32_t aRequestedCount,
                                nsIEventTarget* aEventTarget) {
  CacheFileAutoLock lock(mFile);

  LOG(
      ("CacheFileInputStream::AsyncWait() [this=%p, callback=%p, flags=%d, "
       "requestedCount=%d, eventTarget=%p]",
       this, aCallback, aFlags, aRequestedCount, aEventTarget));

  if (mInReadSegments) {
    LOG(
        ("CacheFileInputStream::AsyncWait() - Cannot be called while the stream"
         " is in ReadSegments!"));
    MOZ_ASSERT(false,
               "Unexpected call. If it's a valid usage implement it. "
               "Otherwise fix the caller.");
    return NS_ERROR_UNEXPECTED;
  }

  mCallback = aCallback;
  mCallbackFlags = aFlags;
  mCallbackTarget = aEventTarget;

  if (!mCallback) {
    if (mWaitingForUpdate) {
      mChunk->CancelWait(this);
      mWaitingForUpdate = false;
    }
    return NS_OK;
  }

  if (mClosed) {
    NotifyListener();
    return NS_OK;
  }

  EnsureCorrectChunk(false);

  MaybeNotifyListener();

  return NS_OK;
}

// nsISeekableStream
NS_IMETHODIMP
CacheFileInputStream::Seek(int32_t whence, int64_t offset) {
  CacheFileAutoLock lock(mFile);
  mFile->AssertOwnsLock();  // For thread-safety analysis

  LOG(("CacheFileInputStream::Seek() [this=%p, whence=%d, offset=%" PRId64 "]",
       this, whence, offset));

  if (mInReadSegments) {
    LOG(
        ("CacheFileInputStream::Seek() - Cannot be called while the stream is "
         "in ReadSegments!"));
    return NS_ERROR_UNEXPECTED;
  }

  if (mClosed) {
    LOG(("CacheFileInputStream::Seek() - Stream is closed. [this=%p]", this));
    return NS_BASE_STREAM_CLOSED;
  }

  int64_t newPos = offset;
  switch (whence) {
    case NS_SEEK_SET:
      if (mAlternativeData) {
        newPos += mFile->mAltDataOffset;
      }
      break;
    case NS_SEEK_CUR:
      newPos += mPos;
      break;
    case NS_SEEK_END:
      if (mAlternativeData) {
        newPos += mFile->mDataSize;
      } else {
        newPos += mFile->mAltDataOffset;
      }
      break;
    default:
      NS_ERROR("invalid whence");
      return NS_ERROR_INVALID_ARG;
  }
  mPos = newPos;
  EnsureCorrectChunk(false);

  LOG(("CacheFileInputStream::Seek() [this=%p, pos=%" PRId64 "]", this, mPos));
  return NS_OK;
}

NS_IMETHODIMP
CacheFileInputStream::SetEOF() {
  MOZ_ASSERT(false, "Don't call SetEOF on cache input stream");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsITellableStream
NS_IMETHODIMP
CacheFileInputStream::Tell(int64_t* _retval) {
  CacheFileAutoLock lock(mFile);
  mFile->AssertOwnsLock();  // For thread-safety analysis

  if (mClosed) {
    LOG(("CacheFileInputStream::Tell() - Stream is closed. [this=%p]", this));
    return NS_BASE_STREAM_CLOSED;
  }

  *_retval = mPos;

  if (mAlternativeData) {
    *_retval -= mFile->mAltDataOffset;
  }

  LOG(("CacheFileInputStream::Tell() [this=%p, retval=%" PRId64 "]", this,
       *_retval));
  return NS_OK;
}

// CacheFileChunkListener
nsresult CacheFileInputStream::OnChunkRead(nsresult aResult,
                                           CacheFileChunk* aChunk) {
  MOZ_CRASH("CacheFileInputStream::OnChunkRead should not be called!");
  return NS_ERROR_UNEXPECTED;
}

nsresult CacheFileInputStream::OnChunkWritten(nsresult aResult,
                                              CacheFileChunk* aChunk) {
  MOZ_CRASH("CacheFileInputStream::OnChunkWritten should not be called!");
  return NS_ERROR_UNEXPECTED;
}

nsresult CacheFileInputStream::OnChunkAvailable(nsresult aResult,
                                                uint32_t aChunkIdx,
                                                CacheFileChunk* aChunk) {
  CacheFileAutoLock lock(mFile);

  LOG(("CacheFileInputStream::OnChunkAvailable() [this=%p, result=0x%08" PRIx32
       ", "
       "idx=%d, chunk=%p]",
       this, static_cast<uint32_t>(aResult), aChunkIdx, aChunk));

  MOZ_ASSERT(mListeningForChunk != -1);

  if (mListeningForChunk != static_cast<int64_t>(aChunkIdx)) {
    // This is not a chunk that we're waiting for
    LOG(
        ("CacheFileInputStream::OnChunkAvailable() - Notification is for a "
         "different chunk. [this=%p, listeningForChunk=%" PRId64 "]",
         this, mListeningForChunk));

    return NS_OK;
  }

  MOZ_ASSERT(!mChunk);
  MOZ_ASSERT(!mWaitingForUpdate);
  MOZ_ASSERT(!mInReadSegments);
  mListeningForChunk = -1;

  if (mClosed) {
    MOZ_ASSERT(!mCallback);

    LOG(
        ("CacheFileInputStream::OnChunkAvailable() - Stream is closed, "
         "ignoring notification. [this=%p]",
         this));

    return NS_OK;
  }

  if (NS_SUCCEEDED(aResult)) {
    mChunk = aChunk;
  } else if (aResult != NS_ERROR_NOT_AVAILABLE) {
    // Close the stream with error. The consumer will receive this error later
    // in Read(), Available() etc. We need to handle NS_ERROR_NOT_AVAILABLE
    // differently since it is returned when the requested chunk is not
    // available and there is no writer that could create it, i.e. it means that
    // we've reached the end of the file.
    CloseWithStatusLocked(aResult);

    return NS_OK;
  }

  MaybeNotifyListener();

  return NS_OK;
}

nsresult CacheFileInputStream::OnChunkUpdated(CacheFileChunk* aChunk) {
  CacheFileAutoLock lock(mFile);

  LOG(("CacheFileInputStream::OnChunkUpdated() [this=%p, idx=%d]", this,
       aChunk->Index()));

  if (!mWaitingForUpdate) {
    LOG(
        ("CacheFileInputStream::OnChunkUpdated() - Ignoring notification since "
         "mWaitingforUpdate == false. [this=%p]",
         this));

    return NS_OK;
  }

  mWaitingForUpdate = false;

  MOZ_ASSERT(mChunk == aChunk);

  MaybeNotifyListener();

  return NS_OK;
}

void CacheFileInputStream::ReleaseChunk() {
  mFile->AssertOwnsLock();

  LOG(("CacheFileInputStream::ReleaseChunk() [this=%p, idx=%d]", this,
       mChunk->Index()));

  MOZ_ASSERT(!mInReadSegments);

  if (mWaitingForUpdate) {
    LOG(
        ("CacheFileInputStream::ReleaseChunk() - Canceling waiting for update. "
         "[this=%p]",
         this));

    mChunk->CancelWait(this);
    mWaitingForUpdate = false;
  }

  mFile->ReleaseOutsideLock(std::move(mChunk));
}

void CacheFileInputStream::EnsureCorrectChunk(bool aReleaseOnly) {
  mFile->AssertOwnsLock();

  LOG(("CacheFileInputStream::EnsureCorrectChunk() [this=%p, releaseOnly=%d]",
       this, aReleaseOnly));

  nsresult rv;

  uint32_t chunkIdx = mPos / kChunkSize;

  if (mInReadSegments) {
    // We must have correct chunk
    MOZ_ASSERT(mChunk);
    MOZ_ASSERT(mChunk->Index() == chunkIdx);
    return;
  }

  if (mChunk) {
    if (mChunk->Index() == chunkIdx) {
      // we have a correct chunk
      LOG(
          ("CacheFileInputStream::EnsureCorrectChunk() - Have correct chunk "
           "[this=%p, idx=%d]",
           this, chunkIdx));

      return;
    }
    ReleaseChunk();
  }

  MOZ_ASSERT(!mWaitingForUpdate);

  if (aReleaseOnly) return;

  if (mListeningForChunk == static_cast<int64_t>(chunkIdx)) {
    // We're already waiting for this chunk
    LOG(
        ("CacheFileInputStream::EnsureCorrectChunk() - Already listening for "
         "chunk %" PRId64 " [this=%p]",
         mListeningForChunk, this));

    return;
  }

  rv = mFile->GetChunkLocked(chunkIdx, CacheFile::READER, this,
                             getter_AddRefs(mChunk));
  if (NS_FAILED(rv)) {
    LOG(
        ("CacheFileInputStream::EnsureCorrectChunk() - GetChunkLocked failed. "
         "[this=%p, idx=%d, rv=0x%08" PRIx32 "]",
         this, chunkIdx, static_cast<uint32_t>(rv)));
    if (rv != NS_ERROR_NOT_AVAILABLE) {
      // Close the stream with error. The consumer will receive this error later
      // in Read(), Available() etc. We need to handle NS_ERROR_NOT_AVAILABLE
      // differently since it is returned when the requested chunk is not
      // available and there is no writer that could create it, i.e. it means
      // that we've reached the end of the file.
      CloseWithStatusLocked(rv);

      return;
    }
  } else if (!mChunk) {
    mListeningForChunk = static_cast<int64_t>(chunkIdx);
  }

  MaybeNotifyListener();
}

int64_t CacheFileInputStream::CanRead(CacheFileChunkReadHandle* aHandle) {
  mFile->AssertOwnsLock();

  MOZ_ASSERT(mChunk);
  MOZ_ASSERT(mPos / kChunkSize == mChunk->Index());

  int64_t retval = aHandle->Offset() + aHandle->DataSize();

  if (!mAlternativeData && mFile->mAltDataOffset != -1 &&
      mFile->mAltDataOffset < retval) {
    retval = mFile->mAltDataOffset;
  }

  retval -= mPos;
  if (retval <= 0 && NS_FAILED(mChunk->GetStatus())) {
    CloseWithStatusLocked(mChunk->GetStatus());
  }

  LOG(("CacheFileInputStream::CanRead() [this=%p, canRead=%" PRId64 "]", this,
       retval));

  return retval;
}

void CacheFileInputStream::NotifyListener() {
  mFile->AssertOwnsLock();

  LOG(("CacheFileInputStream::NotifyListener() [this=%p]", this));

  MOZ_ASSERT(mCallback);
  MOZ_ASSERT(!mInReadSegments);

  if (!mCallbackTarget) {
    mCallbackTarget = CacheFileIOManager::IOTarget();
    if (!mCallbackTarget) {
      LOG(
          ("CacheFileInputStream::NotifyListener() - Cannot get Cache I/O "
           "thread! Using main thread for callback."));
      mCallbackTarget = GetMainThreadSerialEventTarget();
    }
  }

  nsCOMPtr<nsIInputStreamCallback> asyncCallback = NS_NewInputStreamReadyEvent(
      "CacheFileInputStream::NotifyListener", mCallback, mCallbackTarget);

  mCallback = nullptr;
  mCallbackTarget = nullptr;

  asyncCallback->OnInputStreamReady(this);
}

void CacheFileInputStream::MaybeNotifyListener() {
  mFile->AssertOwnsLock();

  LOG(
      ("CacheFileInputStream::MaybeNotifyListener() [this=%p, mCallback=%p, "
       "mClosed=%d, mStatus=0x%08" PRIx32
       ", mChunk=%p, mListeningForChunk=%" PRId64 ", "
       "mWaitingForUpdate=%d]",
       this, mCallback.get(), mClosed, static_cast<uint32_t>(mStatus),
       mChunk.get(), mListeningForChunk, mWaitingForUpdate));

  MOZ_ASSERT(!mInReadSegments);

  if (!mCallback) return;

  if (mClosed || NS_FAILED(mStatus)) {
    NotifyListener();
    return;
  }

  if (!mChunk) {
    if (mListeningForChunk == -1) {
      // EOF, should we notify even if mCallbackFlags == WAIT_CLOSURE_ONLY ??
      NotifyListener();
    }
    return;
  }

  MOZ_ASSERT(mPos / kChunkSize == mChunk->Index());

  if (mWaitingForUpdate) return;

  CacheFileChunkReadHandle hnd = mChunk->GetReadHandle();
  int64_t canRead = CanRead(&hnd);
  if (NS_FAILED(mStatus)) {
    // CanRead() called CloseWithStatusLocked() which called
    // MaybeNotifyListener() so the listener was already notified. Stop here.
    MOZ_ASSERT(!mCallback);
    return;
  }

  if (canRead > 0) {
    if (!(mCallbackFlags & WAIT_CLOSURE_ONLY)) NotifyListener();
  } else if (canRead == 0) {
    if (!mFile->OutputStreamExists(mAlternativeData)) {
      // EOF
      NotifyListener();
    } else {
      mChunk->WaitForUpdate(this);
      mWaitingForUpdate = true;
    }
  } else {
    // Output have set EOF before mPos?
    MOZ_ASSERT(false, "SetEOF is currenty not implemented?!");
    NotifyListener();
  }
}

// Memory reporting

size_t CacheFileInputStream::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  // Everything the stream keeps a reference to is already reported somewhere
  // else. mFile reports itself. mChunk reported as part of CacheFile. mCallback
  // is usually CacheFile or a class that is reported elsewhere.
  return mallocSizeOf(this);
}

}  // namespace mozilla::net
