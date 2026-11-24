/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The MIME stream separates headers and a datastream. It also allows
 * automatic creation of the content-length header.
 */

#include "nsMIMEInputStream.h"

#include "ipc/IPCMessageUtils.h"
#include "mozilla/Mutex.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIAsyncInputStream.h"
#include "nsIClassInfoImpl.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIIPCSerializableInputStream.h"
#include "nsIInputStreamLength.h"
#include "nsIMIMEInputStream.h"
#include "nsISeekableStream.h"
#include "nsString.h"

using namespace mozilla::ipc;
using mozilla::Maybe;

class nsMIMEInputStream : public nsIMIMEInputStream,
                          public nsISeekableStream,
                          public nsIIPCSerializableInputStream,
                          public nsIAsyncInputStream,
                          public nsIInputStreamCallback,
                          public nsIInputStreamLength,
                          public nsIAsyncInputStreamLength,
                          public nsIInputStreamLengthCallback,
                          public nsICloneableInputStream {
  virtual ~nsMIMEInputStream() = default;

 public:
  nsMIMEInputStream() = default;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINPUTSTREAM
  NS_DECL_NSIMIMEINPUTSTREAM
  NS_DECL_NSISEEKABLESTREAM
  NS_DECL_NSITELLABLESTREAM
  NS_DECL_NSIIPCSERIALIZABLEINPUTSTREAM
  NS_DECL_NSIASYNCINPUTSTREAM
  NS_DECL_NSIINPUTSTREAMCALLBACK
  NS_DECL_NSIINPUTSTREAMLENGTH
  NS_DECL_NSIASYNCINPUTSTREAMLENGTH
  NS_DECL_NSIINPUTSTREAMLENGTHCALLBACK
  NS_DECL_NSICLONEABLEINPUTSTREAM

 private:
  struct MOZ_STACK_CLASS ReadSegmentsState {
    nsCOMPtr<nsIInputStream> mThisStream;
    nsWriteSegmentFun mWriter{nullptr};
    void* mClosure{nullptr};
  };
  static nsresult ReadSegCb(nsIInputStream* aIn, void* aClosure,
                            const char* aFromRawSegment, uint32_t aToOffset,
                            uint32_t aCount, uint32_t* aWriteCount);

  bool IsSeekableInputStream() const;
  bool IsAsyncInputStream() const;
  bool IsInputStreamLength() const;
  bool IsAsyncInputStreamLength() const;
  bool IsCloneableInputStream() const;

  nsTArray<HeaderEntry> mHeaders;

  nsCOMPtr<nsIInputStream> mStream;
  mozilla::Atomic<bool, mozilla::Relaxed> mStartedReading{false};

  mozilla::Mutex mMutex{"nsMIMEInputStream::mMutex"};
  nsCOMPtr<nsIInputStreamCallback> mAsyncWaitCallback MOZ_GUARDED_BY(mMutex);

  // This is protected by mutex.
  nsCOMPtr<nsIInputStreamLengthCallback> mAsyncInputStreamLengthCallback;
};

NS_IMPL_ADDREF(nsMIMEInputStream)
NS_IMPL_RELEASE(nsMIMEInputStream)

NS_IMPL_CLASSINFO(nsMIMEInputStream, nullptr, nsIClassInfo::THREADSAFE,
                  NS_MIMEINPUTSTREAM_CID)

NS_INTERFACE_MAP_BEGIN(nsMIMEInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIMIMEInputStream)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIInputStream, nsIMIMEInputStream)
  NS_INTERFACE_MAP_ENTRY(nsITellableStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISeekableStream, IsSeekableInputStream())
  NS_INTERFACE_MAP_ENTRY(nsIIPCSerializableInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIAsyncInputStream, IsAsyncInputStream())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamCallback,
                                     IsAsyncInputStream())
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIMIMEInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamLength,
                                     IsInputStreamLength())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIAsyncInputStreamLength,
                                     IsAsyncInputStreamLength())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIInputStreamLengthCallback,
                                     IsAsyncInputStreamLength())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICloneableInputStream,
                                     IsCloneableInputStream())
  NS_IMPL_QUERY_CLASSINFO(nsMIMEInputStream)
NS_INTERFACE_MAP_END

NS_IMPL_CI_INTERFACE_GETTER(nsMIMEInputStream, nsIMIMEInputStream,
                            nsIAsyncInputStream, nsIInputStream,
                            nsISeekableStream, nsITellableStream)

NS_IMETHODIMP
nsMIMEInputStream::AddHeader(const char* aName, const char* aValue) {
  NS_ENSURE_FALSE(mStartedReading, NS_ERROR_FAILURE);

  HeaderEntry* entry = mHeaders.AppendElement();
  entry->name().Append(aName);
  entry->value().Append(aValue);

  return NS_OK;
}

NS_IMETHODIMP
nsMIMEInputStream::VisitHeaders(nsIHttpHeaderVisitor* visitor) {
  nsresult rv;

  for (auto& header : mHeaders) {
    rv = visitor->VisitHeader(header.name(), header.value());
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMIMEInputStream::SetData(nsIInputStream* aStream) {
  NS_ENSURE_FALSE(mStartedReading, NS_ERROR_FAILURE);

  mStream = aStream;
  return NS_OK;
}

NS_IMETHODIMP
nsMIMEInputStream::GetData(nsIInputStream** aStream) {
  NS_ENSURE_ARG_POINTER(aStream);
  *aStream = do_AddRef(mStream).take();
  return NS_OK;
}

#define INITSTREAMS                               \
  if (!mStartedReading) {                         \
    NS_ENSURE_TRUE(mStream, NS_ERROR_UNEXPECTED); \
    mStartedReading = true;                       \
  }

// Reset mStartedReading when Seek-ing to start
NS_IMETHODIMP
nsMIMEInputStream::Seek(int32_t whence, int64_t offset) {
  NS_ENSURE_TRUE(mStream, NS_ERROR_UNEXPECTED);

  nsresult rv;
  nsCOMPtr<nsISeekableStream> stream = do_QueryInterface(mStream);

  if (whence == NS_SEEK_SET && offset == 0) {
    rv = stream->Seek(whence, offset);
    if (NS_SUCCEEDED(rv)) mStartedReading = false;
  } else {
    INITSTREAMS;
    rv = stream->Seek(whence, offset);
  }

  return rv;
}

// Proxy ReadSegments since we need to be a good little nsIInputStream
NS_IMETHODIMP nsMIMEInputStream::ReadSegments(nsWriteSegmentFun aWriter,
                                              void* aClosure, uint32_t aCount,
                                              uint32_t* _retval) {
  INITSTREAMS;
  ReadSegmentsState state;
  // Disambiguate ambiguous nsIInputStream.
  state.mThisStream =
      static_cast<nsIInputStream*>(static_cast<nsIMIMEInputStream*>(this));
  state.mWriter = aWriter;
  state.mClosure = aClosure;
  return mStream->ReadSegments(ReadSegCb, &state, aCount, _retval);
}

nsresult nsMIMEInputStream::ReadSegCb(nsIInputStream* aIn, void* aClosure,
                                      const char* aFromRawSegment,
                                      uint32_t aToOffset, uint32_t aCount,
                                      uint32_t* aWriteCount) {
  ReadSegmentsState* state = (ReadSegmentsState*)aClosure;
  return (state->mWriter)(state->mThisStream, state->mClosure, aFromRawSegment,
                          aToOffset, aCount, aWriteCount);
}

/**
 * Forward everything else to the mStream after calling INITSTREAMS
 */

// nsIInputStream
NS_IMETHODIMP nsMIMEInputStream::Close(void) {
  INITSTREAMS;
  return mStream->Close();
}
NS_IMETHODIMP nsMIMEInputStream::Available(uint64_t* _retval) {
  INITSTREAMS;
  return mStream->Available(_retval);
}
NS_IMETHODIMP nsMIMEInputStream::StreamStatus() {
  INITSTREAMS;
  return mStream->StreamStatus();
}
NS_IMETHODIMP nsMIMEInputStream::Read(char* buf, uint32_t count,
                                      uint32_t* _retval) {
  INITSTREAMS;
  return mStream->Read(buf, count, _retval);
}
NS_IMETHODIMP nsMIMEInputStream::IsNonBlocking(bool* aNonBlocking) {
  INITSTREAMS;
  return mStream->IsNonBlocking(aNonBlocking);
}

// nsIAsyncInputStream
NS_IMETHODIMP
nsMIMEInputStream::CloseWithStatus(nsresult aStatus) {
  INITSTREAMS;
  nsCOMPtr<nsIAsyncInputStream> asyncStream = do_QueryInterface(mStream);
  return asyncStream->CloseWithStatus(aStatus);
}

NS_IMETHODIMP
nsMIMEInputStream::AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
                             uint32_t aRequestedCount,
                             nsIEventTarget* aEventTarget) {
  INITSTREAMS;
  nsCOMPtr<nsIAsyncInputStream> asyncStream = do_QueryInterface(mStream);
  if (NS_WARN_IF(!asyncStream)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIInputStreamCallback> callback = aCallback ? this : nullptr;
  {
    mozilla::MutexAutoLock lock(mMutex);
    if (NS_WARN_IF(mAsyncWaitCallback && aCallback &&
                   mAsyncWaitCallback != aCallback)) {
      return NS_ERROR_FAILURE;
    }

    mAsyncWaitCallback = aCallback;
  }

  return asyncStream->AsyncWait(callback, aFlags, aRequestedCount,
                                aEventTarget);
}

// nsIInputStreamCallback

NS_IMETHODIMP
nsMIMEInputStream::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  nsCOMPtr<nsIInputStreamCallback> callback;

  {
    mozilla::MutexAutoLock lock(mMutex);

    // We have been canceled in the meanwhile.
    if (!mAsyncWaitCallback) {
      return NS_OK;
    }

    callback.swap(mAsyncWaitCallback);
  }

  MOZ_ASSERT(callback);
  return callback->OnInputStreamReady(this);
}

// nsITellableStream
NS_IMETHODIMP nsMIMEInputStream::Tell(int64_t* _retval) {
  INITSTREAMS;
  nsCOMPtr<nsITellableStream> stream = do_QueryInterface(mStream);
  return stream->Tell(_retval);
}

// nsISeekableStream
NS_IMETHODIMP nsMIMEInputStream::SetEOF(void) {
  INITSTREAMS;
  nsCOMPtr<nsISeekableStream> stream = do_QueryInterface(mStream);
  return stream->SetEOF();
}

/**
 * Factory method used by do_CreateInstance
 */

nsresult nsMIMEInputStreamConstructor(REFNSIID iid, void** result) {
  *result = nullptr;

  RefPtr<nsMIMEInputStream> inst = new nsMIMEInputStream();
  if (!inst) return NS_ERROR_OUT_OF_MEMORY;

  return inst->QueryInterface(iid, result);
}

void nsMIMEInputStream::SerializedComplexity(uint32_t aMaxSize,
                                             uint32_t* aSizeUsed,
                                             uint32_t* aPipes,
                                             uint32_t* aTransferables) {
  if (nsCOMPtr<nsIIPCSerializableInputStream> serializable =
          do_QueryInterface(mStream)) {
    InputStreamHelper::SerializedComplexity(mStream, aMaxSize, aSizeUsed,
                                            aPipes, aTransferables);
  } else {
    *aPipes = 1;
  }
}

void nsMIMEInputStream::Serialize(InputStreamParams& aParams, uint32_t aMaxSize,
                                  uint32_t* aSizeUsed) {
  MOZ_ASSERT(aSizeUsed);
  *aSizeUsed = 0;

  MIMEInputStreamParams params;
  params.headers() = mHeaders.Clone();
  params.startedReading() = mStartedReading;

  if (!mStream) {
    aParams = params;
    return;
  }

  InputStreamParams wrappedParams;

  if (nsCOMPtr<nsIIPCSerializableInputStream> serializable =
          do_QueryInterface(mStream)) {
    InputStreamHelper::SerializeInputStream(mStream, wrappedParams, aMaxSize,
                                            aSizeUsed);
  } else {
    // Falling back to sending the underlying stream over a pipe when
    // sending an nsMIMEInputStream over IPC is potentially wasteful
    // if it is sent several times. This can possibly happen with
    // fission. There are two ways to improve this, see bug 1648369
    // and bug 1648370.
    InputStreamHelper::SerializeInputStreamAsPipe(mStream, wrappedParams);
  }

  NS_ASSERTION(wrappedParams.type() != InputStreamParams::T__None,
               "Wrapped stream failed to serialize!");

  params.optionalStream().emplace(wrappedParams);
  aParams = params;
}

bool nsMIMEInputStream::Deserialize(const InputStreamParams& aParams) {
  if (aParams.type() != InputStreamParams::TMIMEInputStreamParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  const MIMEInputStreamParams& params = aParams.get_MIMEInputStreamParams();
  const Maybe<InputStreamParams>& wrappedParams = params.optionalStream();

  if (wrappedParams.isSome()) {
    nsCOMPtr<nsIInputStream> stream;
    stream = InputStreamHelper::DeserializeInputStream(wrappedParams.ref());
    if (!stream) {
      NS_WARNING("Failed to deserialize wrapped stream!");
      return false;
    }

    MOZ_ALWAYS_SUCCEEDS(SetData(stream));
  }

  mHeaders = params.headers().Clone();
  mStartedReading = params.startedReading();

  return true;
}

NS_IMETHODIMP
nsMIMEInputStream::Length(int64_t* aLength) {
  nsCOMPtr<nsIInputStreamLength> stream = do_QueryInterface(mStream);
  if (NS_WARN_IF(!stream)) {
    return NS_ERROR_FAILURE;
  }

  return stream->Length(aLength);
}

NS_IMETHODIMP
nsMIMEInputStream::AsyncLengthWait(nsIInputStreamLengthCallback* aCallback,
                                   nsIEventTarget* aEventTarget) {
  nsCOMPtr<nsIAsyncInputStreamLength> stream = do_QueryInterface(mStream);
  if (NS_WARN_IF(!stream)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIInputStreamLengthCallback> callback = aCallback ? this : nullptr;
  {
    mozilla::MutexAutoLock lock(mMutex);
    mAsyncInputStreamLengthCallback = aCallback;
  }

  return stream->AsyncLengthWait(callback, aEventTarget);
}

NS_IMETHODIMP
nsMIMEInputStream::OnInputStreamLengthReady(nsIAsyncInputStreamLength* aStream,
                                            int64_t aLength) {
  nsCOMPtr<nsIInputStreamLengthCallback> callback;
  {
    mozilla::MutexAutoLock lock(mMutex);
    // We have been canceled in the meanwhile.
    if (!mAsyncInputStreamLengthCallback) {
      return NS_OK;
    }

    callback.swap(mAsyncInputStreamLengthCallback);
  }

  MOZ_ASSERT(callback);
  return callback->OnInputStreamLengthReady(this, aLength);
}

bool nsMIMEInputStream::IsSeekableInputStream() const {
  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mStream);
  return !!seekable;
}

bool nsMIMEInputStream::IsAsyncInputStream() const {
  nsCOMPtr<nsIAsyncInputStream> asyncStream = do_QueryInterface(mStream);
  return !!asyncStream;
}

bool nsMIMEInputStream::IsInputStreamLength() const {
  nsCOMPtr<nsIInputStreamLength> stream = do_QueryInterface(mStream);
  return !!stream;
}

bool nsMIMEInputStream::IsAsyncInputStreamLength() const {
  nsCOMPtr<nsIAsyncInputStreamLength> stream = do_QueryInterface(mStream);
  return !!stream;
}

bool nsMIMEInputStream::IsCloneableInputStream() const {
  nsCOMPtr<nsICloneableInputStream> stream = do_QueryInterface(mStream);
  return !!stream;
}

// nsICloneableInputStream interface

NS_IMETHODIMP
nsMIMEInputStream::GetCloneable(bool* aCloneable) {
  nsCOMPtr<nsICloneableInputStream> stream = do_QueryInterface(mStream);
  if (!mStream) {
    return NS_ERROR_FAILURE;
  }

  return stream->GetCloneable(aCloneable);
}

NS_IMETHODIMP
nsMIMEInputStream::Clone(nsIInputStream** aResult) {
  nsCOMPtr<nsICloneableInputStream> stream = do_QueryInterface(mStream);
  if (!mStream) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIInputStream> clonedStream;
  nsresult rv = stream->Clone(getter_AddRefs(clonedStream));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIMIMEInputStream> mimeStream = new nsMIMEInputStream();

  rv = mimeStream->SetData(clonedStream);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  for (const HeaderEntry& entry : mHeaders) {
    rv = mimeStream->AddHeader(entry.name().get(), entry.value().get());
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  static_cast<nsMIMEInputStream*>(mimeStream.get())->mStartedReading =
      mStartedReading.load();

  mimeStream.forget(aResult);
  return NS_OK;
}
