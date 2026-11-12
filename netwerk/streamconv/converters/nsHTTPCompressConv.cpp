/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHTTPCompressConv.h"

#include "ErrorList.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/glean/GleanPings.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/ChannelEventQueue.h"
#include "mozilla/net/Dictionary.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsComponentManagerUtils.h"
#include "nsError.h"
#include "nsIChannel.h"
#include "nsIEffectiveTLDService.h"
#include "nsIForcePendingChannel.h"
#include "nsIHttpChannel.h"
#include "nsILoadInfo.h"
#include "nsIRequest.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"

// brotli headers
#undef assert
#include "assert.h"
#include "brotli/decode.h"
#include "state.h"

#define ZSTD_STATIC_LINKING_ONLY 1
#include "zstd/zstd.h"

namespace mozilla {
namespace net {

class DictionaryCacheEntry;

extern LazyLogModule gHttpLog;
#define LOG(args) \
  MOZ_LOG(mozilla::net::gHttpLog, mozilla::LogLevel::Debug, args)

extern LazyLogModule gDictionaryLog;
#define DICTIONARY_LOG(args) \
  MOZ_LOG(mozilla::net::gDictionaryLog, mozilla::LogLevel::Debug, args)

class BrotliWrapper {
 public:
  BrotliWrapper() = default;
  ~BrotliWrapper() { BrotliDecoderStateCleanup(&mState); }

  bool Init(nsIRequest* aRequest, nsHTTPCompressConv::CompressMode aMode) {
    if (!BrotliDecoderStateInit(&mState, nullptr, nullptr, nullptr)) {
      return false;
    }

    if (aMode != nsHTTPCompressConv::HTTP_COMPRESS_BROTLI_DICTIONARY) {
      return true;
    }

    nsCOMPtr<nsIHttpChannel> httpchannel(do_QueryInterface(aRequest));
    if (!httpchannel) {
      return false;
    }

    if (NS_SUCCEEDED(httpchannel->GetDecompressDictionary(
            getter_AddRefs(mDictionary))) &&
        mDictionary) {
      // Critical: Dictionary must be fully loaded before use
      if (!mDictionary->DictionaryReady()) {
        DICTIONARY_LOG(("Brotli: dictionary not ready yet!"));
        MOZ_ASSERT(false, "Dictionary should be ready before decompression");
        return false;
      }
      size_t length = mDictionary->GetDictionary().length();
      DICTIONARY_LOG(("Brotli: dictionary %zu bytes", length));
      if (length > 0) {
        BROTLI_BOOL result = BrotliDecoderAttachDictionary(
            &mState, BROTLI_SHARED_DICTIONARY_RAW, length,
            mDictionary->GetDictionary().begin());
        if (!result) {
          DICTIONARY_LOG(("Brotli: AttachDictionary failed"));
          return false;
        }
      }
    }

    return true;
  }

  BrotliDecoderState mState{};
  Atomic<size_t, Relaxed> mTotalOut{0};
  nsresult mStatus = NS_OK;
  Atomic<bool, Relaxed> mBrotliStateIsStreamEnd{false};

  nsIRequest* mRequest{nullptr};
  nsISupports* mContext{nullptr};
  uint64_t mSourceOffset{0};

  RefPtr<DictionaryCacheEntry> mDictionary;

  uint8_t mEaten{0};
  uint8_t mHeader[36];  // \FF\44\43\42 + 32-byte SHA-256
};

#ifdef ZSTD_INFALLIBLE
// zstd can grab large blocks; use an infallible alloctor
static void* zstd_malloc(void*, size_t size) { return moz_xmalloc(size); }

static void zstd_free(void*, void* address) { free(address); }

ZSTD_customMem const zstd_allocators = {zstd_malloc, zstd_free, nullptr};
#endif

class ZstdWrapper {
 public:
  ZstdWrapper(nsIRequest* aRequest, nsHTTPCompressConv::CompressMode aMode) {
    size_t length = 0;
    if (aMode == nsHTTPCompressConv::HTTP_COMPRESS_ZSTD_DICTIONARY) {
      nsCOMPtr<nsIHttpChannel> httpchannel(do_QueryInterface(aRequest));
      if (httpchannel) {
        if (NS_FAILED(httpchannel->GetDecompressDictionary(
                getter_AddRefs(mDictionary))) ||
            !mDictionary) {
          return;
        }
        // Critical: Dictionary must be fully loaded before use
        if (!mDictionary->DictionaryReady()) {
          DICTIONARY_LOG(("Zstd: dictionary not ready yet!"));
          MOZ_ASSERT(false, "Dictionary should be ready before decompression");
          mDictionary = nullptr;
          return;
        }
        length = mDictionary->GetDictionary().length();
      } else {
        // Can't decode without a dictionary
        return;
      }
    }

#ifdef ZSTD_INFALLIBLE
    mDStream = ZSTD_createDStream_advanced(zstd_allocators);  // infallible
#else
    mDStream = ZSTD_createDStream();  // fallible
    if (!mDStream) {
      MOZ_RELEASE_ASSERT(ZSTD_defaultCMem.customAlloc == nullptr &&
                         ZSTD_defaultCMem.customFree == nullptr &&
                         ZSTD_defaultCMem.opaque == nullptr);
      return;
    }
#endif
    if (mDictionary) {
      DICTIONARY_LOG(("zstd: dictionary %zu bytes", length));
      ZSTD_DCtx_reset(mDStream, ZSTD_reset_session_only);
      if (ZSTD_isError(ZSTD_DCtx_loadDictionary(
              mDStream, mDictionary->GetDictionary().begin(), length))) {
        return;
      }
    }

    ZSTD_DCtx_setParameter(mDStream, ZSTD_d_windowLogMax, 23 /*8*1024*1024*/);
  }
  ~ZstdWrapper() {
    if (mDStream) {
      ZSTD_freeDStream(mDStream);
    }
  }

  UniquePtr<uint8_t[]> mOutBuffer;
  nsresult mStatus = NS_OK;
  nsIRequest* mRequest{nullptr};
  nsISupports* mContext{nullptr};
  uint64_t mSourceOffset{0};
  ZSTD_DStream* mDStream{nullptr};

  RefPtr<DictionaryCacheEntry> mDictionary;
};

// nsISupports implementation
NS_IMPL_ISUPPORTS(nsHTTPCompressConv, nsIStreamConverter, nsIStreamListener,
                  nsIRequestObserver, nsICompressConvStats,
                  nsIThreadRetargetableStreamListener)

// nsFTPDirListingConv methods
nsHTTPCompressConv::nsHTTPCompressConv() {
  LOG(("nsHttpCompresssConv %p ctor\n", this));
  if (NS_IsMainThread()) {
    mFailUncleanStops =
        Preferences::GetBool("network.http.enforce-framing.http", false);
  } else {
    mFailUncleanStops = false;
  }
}

nsHTTPCompressConv::~nsHTTPCompressConv() {
  LOG(("nsHttpCompresssConv %p dtor\n", this));
  if (mMainThreadEventQueue) {
    mMainThreadEventQueue->NotifyReleasingOwner();
  }
  if (mInpBuffer) {
    free(mInpBuffer);
  }

  if (mOutBuffer) {
    free(mOutBuffer);
  }

  // For some reason we are not getting Z_STREAM_END.  But this was also seen
  //    for mozilla bug 198133.  Need to handle this case.
  if (mStreamInitialized && !mStreamEnded) {
    inflateEnd(&d_stream);
  }
}

void nsHTTPCompressConv::ReportDecodingErrorWithSite(const nsACString& aLabel) {
  if (mIsPrivateBrowsing) {
    return;
  }

  nsAutoCString site(mSite);
  if (site.IsEmpty()) {
    site.AssignLiteral("unknown");
  }

  mozilla::glean::network::ContentDecodingErrorReportExtra extra = {
      .errorType = Some(nsCString(aLabel)), .topLevelSite = Some(site)};
  glean::network::content_decoding_error_report.Record(Some(extra));
}

NS_IMETHODIMP
nsHTTPCompressConv::GetDecodedDataLength(uint64_t* aDecodedDataLength) {
  // When multiple Content-Encodings are stacked (e.g. "gzip, gzip"), the
  // converters form a chain where this instance's mDecodedDataLength only
  // reflects the bytes emitted after a single decoding pass. The fully
  // decoded body size is what the innermost converter forwards to the real
  // listener, so walk the chain to return that.
  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  if (nsCOMPtr<nsICompressConvStats> inner = do_QueryInterface(listener)) {
    return inner->GetDecodedDataLength(aDecodedDataLength);
  }
  *aDecodedDataLength = mDecodedDataLength;
  return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::AsyncConvertData(const char* aFromType, const char* aToType,
                                     nsIStreamListener* aListener,
                                     nsISupports* aCtxt) {
  if (!nsCRT::strncasecmp(aFromType, HTTP_COMPRESS_TYPE,
                          sizeof(HTTP_COMPRESS_TYPE) - 1) ||
      !nsCRT::strncasecmp(aFromType, HTTP_X_COMPRESS_TYPE,
                          sizeof(HTTP_X_COMPRESS_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_COMPRESS;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_GZIP_TYPE,
                                 sizeof(HTTP_GZIP_TYPE) - 1) ||
             !nsCRT::strncasecmp(aFromType, HTTP_X_GZIP_TYPE,
                                 sizeof(HTTP_X_GZIP_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_GZIP;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_DEFLATE_TYPE,
                                 sizeof(HTTP_DEFLATE_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_DEFLATE;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_BROTLI_TYPE,
                                 sizeof(HTTP_BROTLI_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_BROTLI;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_ZSTD_TYPE,
                                 sizeof(HTTP_ZSTD_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_ZSTD;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_ZST_TYPE,
                                 sizeof(HTTP_ZST_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_ZSTD;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_BROTLI_DICTIONARY_TYPE,
                                 sizeof(HTTP_BROTLI_DICTIONARY_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_BROTLI_DICTIONARY;
  } else if (!nsCRT::strncasecmp(aFromType, HTTP_ZSTD_DICTIONARY_TYPE,
                                 sizeof(HTTP_ZSTD_DICTIONARY_TYPE) - 1)) {
    mMode = HTTP_COMPRESS_ZSTD_DICTIONARY;
  }
  LOG(("nsHttpCompresssConv %p AsyncConvertData %s %s mode %d\n", this,
       aFromType, aToType, (CompressMode)mMode));

  MutexAutoLock lock(mMutex);
  // hook ourself up with the receiving listener.
  mListener = aListener;

  return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::GetConvertedType(const nsACString& aFromType,
                                     nsIChannel* aChannel,
                                     nsACString& aToType) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void nsHTTPCompressConv::SetDispatchToMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDispatchToMainThread) {
    return;
  }
  mMainThreadEventQueue =
      new ChannelEventQueue(static_cast<nsIStreamConverter*>(this));
  mDispatchToMainThread = true;
}

NS_IMETHODIMP
nsHTTPCompressConv::MaybeRetarget(nsIRequest* request) {
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv;
  nsCOMPtr<nsIThreadRetargetableRequest> req = do_QueryInterface(request);
  if (!req) {
    return NS_ERROR_NO_INTERFACE;
  }
  if (!StaticPrefs::network_decompression_off_mainthread2()) {
    return NS_OK;
  }
  nsCOMPtr<nsISerialEventTarget> target;
  rv = req->GetDeliveryTarget(getter_AddRefs(target));
  if (NS_FAILED(rv) || !target || target->IsOnCurrentThread()) {
    nsCOMPtr<nsIChannel> channel(do_QueryInterface(request));
    int64_t length = -1;
    if (channel) {
      channel->GetContentLength(&length);
      // If this fails we'll retarget
    }
    if (length <= 0 ||
        length >=
            StaticPrefs::network_decompression_off_mainthread_min_size()) {
      LOG(("MaybeRetarget: Retargeting to background thread: Length %" PRId64,
           length));
      // No retargetting was performed.  Decompress off MainThread,
      // and dispatch results back to MainThread.
      // Don't do this if the input is small, if we know the length.
      // If the length is 0 (unknown), always use OMT.
      nsCOMPtr<nsISerialEventTarget> backgroundThread;
      rv = NS_CreateBackgroundTaskQueue("nsHTTPCompressConv",
                                        getter_AddRefs(backgroundThread));
      NS_ENSURE_SUCCESS(rv, rv);
      rv = req->RetargetDeliveryTo(backgroundThread);
      NS_ENSURE_SUCCESS(rv, rv);
      if (NS_SUCCEEDED(rv)) {
        SetDispatchToMainThread();
      }
    } else {
      LOG(("MaybeRetarget: Not retargeting: Length %" PRId64, length));
    }
  } else {
    LOG(("MaybeRetarget: Don't need to retarget"));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::OnStartRequest(nsIRequest* request) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("nsHttpCompresssConv %p onstart\n", this));

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  if (channel) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    if (loadInfo && loadInfo->GetOriginAttributes().IsPrivateBrowsing()) {
      mIsPrivateBrowsing = true;
    }
    if (!mIsPrivateBrowsing) {
      nsCOMPtr<nsIURI> uri;
      if (NS_SUCCEEDED(channel->GetURI(getter_AddRefs(uri))) && uri) {
        nsCOMPtr<nsIEffectiveTLDService> eTLDService =
            do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
        if (eTLDService) {
          (void)eTLDService->GetBaseDomain(uri, 0, mSite);
        }
      }
    }
  }

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  nsresult rv = listener->OnStartRequest(request);
  if (NS_SUCCEEDED(rv)) {
    if (XRE_IsContentProcess()) {
      nsCOMPtr<nsIThreadRetargetableStreamListener> retargetlistener =
          do_QueryInterface(listener);
      // |nsHTTPCompressConv| should *always* be dispatched off of the main
      // thread from a content process, even if its listeners don't support it.
      //
      // If its listener chain does not support being retargeted off of the
      // main thread, it will be dispatched back to the main thread in
      // |do_OnDataAvailable| and |OnStopRequest|.
      if (!retargetlistener ||
          NS_FAILED(retargetlistener->CheckListenerChain())) {
        SetDispatchToMainThread();
      }
    }
  }
  return rv;
}

NS_IMETHODIMP
nsHTTPCompressConv::OnStopRequest(nsIRequest* request, nsresult aStatus) {
  if (mDispatchToMainThread) {
    mMainThreadEventQueue->RunOrEnqueue(std::make_unique<ChannelFunctionEvent>(
        []() { return do_AddRef(GetMainThreadSerialEventTarget()); },
        [self = RefPtr{this}, request = nsCOMPtr<nsIRequest>(request),
         aStatus]() {
          AutoEventEnqueuer ensureSerialDispatch(self->mMainThreadEventQueue);
          (void)self->DoOnStopRequest(request, aStatus);
        }));
    return NS_OK;
  }
  return DoOnStopRequest(request, aStatus);
}

nsresult nsHTTPCompressConv::DoOnStopRequest(nsIRequest* request,
                                             nsresult aStatus) {
  nsresult status = aStatus;
  // Bug 1886237 : TRRServiceChannel calls OnStopRequest OMT
  // MOZ_ASSERT(NS_IsMainThread());
  LOG(("nsHttpCompresssConv %p onstop %" PRIx32 " mDispatchToMainThread %d\n",
       this, static_cast<uint32_t>(aStatus), bool(mDispatchToMainThread)));

  // Framing integrity is enforced for content-encoding: gzip, but not for
  // content-encoding: deflate. Note that gzip vs deflate is NOT determined
  // by content sniffing but only via header.
  if (!mStreamEnded && NS_SUCCEEDED(status) &&
      (mFailUncleanStops && (mMode == HTTP_COMPRESS_GZIP))) {
    // This is not a clean end of gzip stream: the transfer is incomplete.
    status = NS_ERROR_NET_PARTIAL_TRANSFER;
    LOG(("nsHttpCompresssConv %p onstop partial gzip\n", this));
  }
  if (NS_SUCCEEDED(status) && (mMode == HTTP_COMPRESS_BROTLI ||
                               mMode == HTTP_COMPRESS_BROTLI_DICTIONARY)) {
    nsCOMPtr<nsIForcePendingChannel> fpChannel = do_QueryInterface(request);
    bool isPending = false;
    if (request) {
      request->IsPending(&isPending);
    }
    if (fpChannel && !isPending) {
      fpChannel->ForcePending(true);
    }
    if (mBrotli && NS_FAILED(mBrotli->mStatus)) {
      ReportDecodingErrorWithSite(
          mMode == HTTP_COMPRESS_BROTLI_DICTIONARY ? "dcb"_ns : "brotli"_ns);
      status = NS_ERROR_INVALID_CONTENT_ENCODING;
    }
    LOG(("nsHttpCompresssConv %p onstop brotlihandler rv %" PRIx32 "\n", this,
         static_cast<uint32_t>(status)));
    if (fpChannel && !isPending) {
      fpChannel->ForcePending(false);
    }
  }
  // We don't need the dictionary data anymore
  if (mBrotli || mZstd) {
    RefPtr<DictionaryCacheEntry> dict;
    nsCOMPtr<nsIHttpChannel> httpchannel(do_QueryInterface(request));
    if (httpchannel) {
      httpchannel->SetDecompressDictionary(nullptr);
    }
    // paranoia
    mBrotli = nullptr;
    mZstd = nullptr;
  }

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }

  return listener->OnStopRequest(request, status);
}

/* static */
nsresult nsHTTPCompressConv::BrotliHandler(nsIInputStream* stream,
                                           void* closure, const char* dataIn,
                                           uint32_t, uint32_t aAvail,
                                           uint32_t* countRead) {
  MOZ_ASSERT(stream);
  nsHTTPCompressConv* self = static_cast<nsHTTPCompressConv*>(closure);
  *countRead = 0;

  const size_t kOutSize = 128 * 1024;  // just a chunk size, we call in a loop
  uint8_t* outPtr;
  size_t outSize;
  size_t avail = aAvail;
  BrotliDecoderResult res;

  if (!self->mBrotli) {
    *countRead = aAvail;
    return NS_OK;
  }

  // Dictionary-encoded brotli has a 36-byte header (4 byte fixed + 32 byte
  // SHA-256)
  if (self->mBrotli->mDictionary && self->mBrotli->mEaten < 36) {
    uint8_t header_needed = 36 - self->mBrotli->mEaten;
    if (avail >= header_needed) {
      memcpy(&self->mBrotli->mHeader[self->mBrotli->mEaten], dataIn,
             header_needed);
      avail -= header_needed;
      dataIn += header_needed;
      self->mBrotli->mEaten = 36;

      // Validate header
      // XXX we could verify the SHA-256 matches what we offered
      static uint8_t brotli_header[4] = {0xff, 0x44, 0x43, 0x42};
      if (memcmp(self->mBrotli->mHeader, brotli_header, 4) != 0) {
        DICTIONARY_LOG(
            ("!! %p Brotli failed: bad magic header 0x%02x%02x%02x%02x", self,
             self->mBrotli->mHeader[0], self->mBrotli->mHeader[1],
             self->mBrotli->mHeader[2], self->mBrotli->mHeader[3]));
        self->ReportDecodingErrorWithSite("dcb"_ns);
        self->mBrotli->mStatus = NS_ERROR_INVALID_CONTENT_ENCODING;
        return self->mBrotli->mStatus;
      }
    } else {
      memcpy(&self->mBrotli->mHeader[self->mBrotli->mEaten], dataIn, aAvail);
      self->mBrotli->mEaten += aAvail;
      *countRead = aAvail;
      return NS_OK;
    }
  }

  auto outBuffer = MakeUniqueFallible<uint8_t[]>(kOutSize);
  if (outBuffer == nullptr) {
    self->mBrotli->mStatus = NS_ERROR_OUT_OF_MEMORY;
    return self->mBrotli->mStatus;
  }
  do {
    outSize = kOutSize;
    outPtr = outBuffer.get();

    // brotli api is documented in brotli/dec/decode.h and brotli/dec/decode.c
    LOG(("nsHttpCompresssConv %p brotlihandler decompress %zu\n", self, avail));
    size_t totalOut = self->mBrotli->mTotalOut;
    res = ::BrotliDecoderDecompressStream(
        &self->mBrotli->mState, &avail,
        reinterpret_cast<const unsigned char**>(&dataIn), &outSize, &outPtr,
        &totalOut);
    outSize = kOutSize - outSize;
    self->mBrotli->mTotalOut = totalOut;
    self->mBrotli->mBrotliStateIsStreamEnd =
        BrotliDecoderIsFinished(&self->mBrotli->mState);
    LOG(("nsHttpCompressConv %p brotlihandler decompress rv=%" PRIx32
         " out=%zu\n",
         self, static_cast<uint32_t>(res), outSize));

    if (res == BROTLI_DECODER_RESULT_ERROR) {
      DICTIONARY_LOG(
          ("nsHttpCompressConv %p decoding error: marking invalid encoding "
           "(%zu)",
           self, avail));
      self->ReportDecodingErrorWithSite(
          self->mMode == HTTP_COMPRESS_BROTLI_DICTIONARY ? "dcb"_ns
                                                         : "brotli"_ns);
      self->mBrotli->mStatus = NS_ERROR_INVALID_CONTENT_ENCODING;
      return self->mBrotli->mStatus;
    }

    // in 'the current implementation' brotli must consume everything before
    // asking for more input
    if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
      MOZ_ASSERT(!avail);
      if (avail) {
        LOG(("nsHttpCompressConv %p did not consume all input", self));
        self->mBrotli->mStatus = NS_ERROR_UNEXPECTED;
        return self->mBrotli->mStatus;
      }
    }

    auto callOnDataAvailable = [&](uint64_t aSourceOffset, const char* aBuffer,
                                   uint32_t aCount) {
      nsresult rv = self->do_OnDataAvailable(self->mBrotli->mRequest,
                                             aSourceOffset, aBuffer, aCount);
      LOG(("nsHttpCompressConv %p BrotliHandler ODA rv=%" PRIx32, self,
           static_cast<uint32_t>(rv)));
      if (NS_FAILED(rv)) {
        self->mBrotli->mStatus = rv;
      }

      return rv;
    };

    if (outSize > 0) {
      if (NS_FAILED(callOnDataAvailable(
              self->mBrotli->mSourceOffset,
              reinterpret_cast<const char*>(outBuffer.get()), outSize))) {
        return self->mBrotli->mStatus;
      }
      self->mBrotli->mSourceOffset += outSize;
    }

    // See bug 1759745. If the decoder has more output data, take it.
    while (::BrotliDecoderHasMoreOutput(&self->mBrotli->mState)) {
      outSize = kOutSize;
      const uint8_t* buffer =
          ::BrotliDecoderTakeOutput(&self->mBrotli->mState, &outSize);
      if (NS_FAILED(callOnDataAvailable(self->mBrotli->mSourceOffset,
                                        reinterpret_cast<const char*>(buffer),
                                        outSize))) {
        return self->mBrotli->mStatus;
      }
      self->mBrotli->mSourceOffset += outSize;
    }

    if (res == BROTLI_DECODER_RESULT_SUCCESS ||
        res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
      *countRead = aAvail;
      return NS_OK;
    }
    MOZ_ASSERT(res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
  } while (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);

  self->mBrotli->mStatus = NS_ERROR_UNEXPECTED;
  return self->mBrotli->mStatus;
}

/* static */
nsresult nsHTTPCompressConv::ZstdHandler(nsIInputStream* stream, void* closure,
                                         const char* dataIn, uint32_t,
                                         uint32_t aAvail, uint32_t* countRead) {
  MOZ_ASSERT(stream);
  nsHTTPCompressConv* self = static_cast<nsHTTPCompressConv*>(closure);
  *countRead = 0;

  const size_t kOutSize = ZSTD_DStreamOutSize();  // normally 128K
  uint8_t* outPtr;
  size_t avail = aAvail;

  // Stop decompressing after an error
  if (self->mZstd->mStatus != NS_OK) {
    *countRead = aAvail;
    return NS_OK;
  }

  if (!self->mZstd->mOutBuffer) {
    self->mZstd->mOutBuffer = MakeUniqueFallible<uint8_t[]>(kOutSize);
    if (!self->mZstd->mOutBuffer) {
      self->mZstd->mStatus = NS_ERROR_OUT_OF_MEMORY;
      return self->mZstd->mStatus;
    }
  }
  ZSTD_inBuffer inBuffer = {.src = dataIn, .size = aAvail, .pos = 0};
  uint32_t last_pos = 0;
  while (inBuffer.pos < inBuffer.size) {
    outPtr = self->mZstd->mOutBuffer.get();

    LOG(("nsHttpCompresssConv %p zstdhandler decompress %zu\n", self, avail));
    // Use ZSTD_(de)compressStream to (de)compress the input buffer into the
    // output buffer, and fill aReadCount with the number of bytes consumed.
    ZSTD_outBuffer outBuffer{.dst = outPtr, .size = kOutSize};
    size_t result;
    bool output_full;
    do {
      outBuffer.pos = 0;
      result =
          ZSTD_decompressStream(self->mZstd->mDStream, &outBuffer, &inBuffer);

      // If we errored when writing, flag this and abort writing.
      if (ZSTD_isError(result)) {
        self->ReportDecodingErrorWithSite(
            self->mMode == HTTP_COMPRESS_ZSTD_DICTIONARY ? "dcz"_ns
                                                         : "zstd"_ns);
        self->mZstd->mStatus = NS_ERROR_INVALID_CONTENT_ENCODING;
        return self->mZstd->mStatus;
      }

      nsresult rv = self->do_OnDataAvailable(
          self->mZstd->mRequest, self->mZstd->mSourceOffset,
          reinterpret_cast<const char*>(outPtr), outBuffer.pos);
      if (NS_FAILED(rv)) {
        self->mZstd->mStatus = rv;
        return rv;
      }
      self->mZstd->mSourceOffset += inBuffer.pos - last_pos;
      last_pos = inBuffer.pos;
      output_full = outBuffer.pos == outBuffer.size;
      // in the unlikely case that the output buffer was full, loop to
      // drain it before processing more input
    } while (output_full);
  }
  *countRead = inBuffer.pos;
  return NS_OK;
}

NS_IMETHODIMP
nsHTTPCompressConv::OnDataAvailable(nsIRequest* request, nsIInputStream* iStr,
                                    uint64_t aSourceOffset, uint32_t aCount) {
  nsresult rv = NS_ERROR_INVALID_CONTENT_ENCODING;
  uint32_t streamLen = aCount;
  LOG(("nsHttpCompressConv %p OnDataAvailable aSourceOffset:%" PRIu64
       " count:%u",
       this, aSourceOffset, aCount));

  if (streamLen == 0) {
    NS_ERROR("count of zero passed to OnDataAvailable");
    return NS_ERROR_UNEXPECTED;
  }

  if (mStreamEnded) {
    // Hmm... this may just indicate that the data stream is done and that
    // what's left is either metadata or padding of some sort.... throwing
    // it out is probably the safe thing to do.
    uint32_t n;
    return iStr->ReadSegments(NS_DiscardSegment, nullptr, streamLen, &n);
  }

  switch (mMode) {
    case HTTP_COMPRESS_GZIP:
      streamLen = check_header(iStr, streamLen, &rv);

      if (rv != NS_OK) {
        ReportDecodingErrorWithSite("gzip"_ns);
        return rv;
      }

      if (streamLen == 0) {
        return NS_OK;
      }

      [[fallthrough]];

    case HTTP_COMPRESS_DEFLATE:
#if defined(__GNUC__) && (__GNUC__ >= 12) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wuse-after-free"
#endif  // __GNUC__ >= 12

      // The return value of realloc is null in case of failure, and the old
      // buffer will stay valid but GCC isn't smart enough to figure that out.
      // See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=110501
      if (mInpBuffer != nullptr && streamLen > mInpBufferLen) {
        unsigned char* originalInpBuffer = mInpBuffer;
        if (!(mInpBuffer = (unsigned char*)realloc(
                  mInpBuffer, mInpBufferLen = streamLen))) {
          free(originalInpBuffer);
          mInpBufferLen = 0;
        }

        if (mOutBufferLen < streamLen * 2) {
          unsigned char* originalOutBuffer = mOutBuffer;
          if (!(mOutBuffer = (unsigned char*)realloc(
                    mOutBuffer, mOutBufferLen = streamLen * 3))) {
            free(originalOutBuffer);
            mOutBufferLen = 0;
          }
        }

#if defined(__GNUC__) && (__GNUC__ >= 12) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif  // __GNUC__ >= 12

        if (mInpBuffer == nullptr || mOutBuffer == nullptr) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      if (mInpBuffer == nullptr) {
        mInpBuffer = (unsigned char*)malloc(mInpBufferLen = streamLen);
      }

      if (mOutBuffer == nullptr) {
        mOutBuffer = (unsigned char*)malloc(mOutBufferLen = streamLen * 3);
      }

      if (mInpBuffer == nullptr || mOutBuffer == nullptr) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      uint32_t unused;
      iStr->Read((char*)mInpBuffer, streamLen, &unused);

      if (mMode == HTTP_COMPRESS_DEFLATE) {
        if (!mStreamInitialized) {
          memset(&d_stream, 0, sizeof(d_stream));

          if (inflateInit(&d_stream) != Z_OK) {
            return NS_ERROR_FAILURE;
          }

          mStreamInitialized = true;
        }
        d_stream.next_in = mInpBuffer;
        d_stream.avail_in = (uInt)streamLen;

        mDummyStreamInitialised = false;
        for (;;) {
          d_stream.next_out = mOutBuffer;
          d_stream.avail_out = (uInt)mOutBufferLen;

          int code = inflate(&d_stream, Z_NO_FLUSH);
          unsigned bytesWritten = (uInt)mOutBufferLen - d_stream.avail_out;

          if (code == Z_STREAM_END) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }

            inflateEnd(&d_stream);
            mStreamEnded = true;
            break;
          }
          if (code == Z_OK) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }
          } else if (code == Z_BUF_ERROR) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }
            break;
          } else if (code == Z_DATA_ERROR) {
            // some servers (notably Apache with mod_deflate) don't generate
            // zlib headers insert a dummy header and try again
            static char dummy_head[2] = {
                0x8 + 0x7 * 0x10,
                (((0x8 + 0x7 * 0x10) * 0x100 + 30) / 31 * 31) & 0xFF,
            };
            inflateReset(&d_stream);
            d_stream.next_in = (Bytef*)dummy_head;
            d_stream.avail_in = sizeof(dummy_head);

            code = inflate(&d_stream, Z_NO_FLUSH);
            if (code != Z_OK) {
              return NS_ERROR_FAILURE;
            }

            // stop an endless loop caused by non-deflate data being labelled as
            // deflate
            if (mDummyStreamInitialised) {
              NS_WARNING(
                  "endless loop detected"
                  " - invalid deflate");
              ReportDecodingErrorWithSite("deflate"_ns);
              return NS_ERROR_INVALID_CONTENT_ENCODING;
            }
            mDummyStreamInitialised = true;
            // reset stream pointers to our original data
            d_stream.next_in = mInpBuffer;
            d_stream.avail_in = (uInt)streamLen;
          } else {
            ReportDecodingErrorWithSite("deflate"_ns);
            return NS_ERROR_INVALID_CONTENT_ENCODING;
          }
        } /* for */
      } else {
        if (!mStreamInitialized) {
          memset(&d_stream, 0, sizeof(d_stream));

          if (inflateInit2(&d_stream, -MAX_WBITS) != Z_OK) {
            return NS_ERROR_FAILURE;
          }

          mStreamInitialized = true;
        }

        d_stream.next_in = mInpBuffer;
        d_stream.avail_in = (uInt)streamLen;

        for (;;) {
          d_stream.next_out = mOutBuffer;
          d_stream.avail_out = (uInt)mOutBufferLen;

          int code = inflate(&d_stream, Z_NO_FLUSH);
          unsigned bytesWritten = (uInt)mOutBufferLen - d_stream.avail_out;

          if (code == Z_STREAM_END) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }

            inflateEnd(&d_stream);
            mStreamEnded = true;
            break;
          }
          if (code == Z_OK) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }
          } else if (code == Z_BUF_ERROR) {
            if (bytesWritten) {
              rv = do_OnDataAvailable(request, aSourceOffset, (char*)mOutBuffer,
                                      bytesWritten);
              if (NS_FAILED(rv)) {
                return rv;
              }
            }
            break;
          } else {
            ReportDecodingErrorWithSite("gzip"_ns);
            return NS_ERROR_INVALID_CONTENT_ENCODING;
          }
        } /* for */
      } /* gzip */
      break;

    case HTTP_COMPRESS_BROTLI:
    case HTTP_COMPRESS_BROTLI_DICTIONARY: {
      if (!mBrotli) {
        mBrotli = std::make_unique<BrotliWrapper>();
        if (!mBrotli->Init(request, mMode)) {
          return NS_ERROR_FAILURE;
        }
      }

      mBrotli->mRequest = request;
      mBrotli->mContext = nullptr;
      mBrotli->mSourceOffset = aSourceOffset;

      uint32_t countRead;
      rv = iStr->ReadSegments(BrotliHandler, this, streamLen, &countRead);
      if (NS_SUCCEEDED(rv)) {
        rv = mBrotli->mStatus;
      }
      if (NS_FAILED(rv)) {
        return rv;
      }
    } break;

    case HTTP_COMPRESS_ZSTD:
    case HTTP_COMPRESS_ZSTD_DICTIONARY: {
      if (!mZstd) {
        mZstd = std::make_unique<ZstdWrapper>(request, mMode);
        if (!mZstd->mDStream) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      mZstd->mRequest = request;
      mZstd->mContext = nullptr;
      mZstd->mSourceOffset = aSourceOffset;

      uint32_t countRead;
      rv = iStr->ReadSegments(ZstdHandler, this, streamLen, &countRead);
      if (NS_SUCCEEDED(rv)) {
        rv = mZstd->mStatus;
      }
      if (NS_FAILED(rv)) {
        return rv;
      }
    } break;

    default: {
      // Pass-through modes must honour mDispatchToMainThread as well.
      // CheckListenerChain() reports success for chains that can only run on
      // the main thread, on the promise that this converter bounces the data
      // back there.
      if (mDispatchToMainThread && !NS_IsMainThread()) {
        nsAutoCString data;
        MOZ_TRY(NS_ReadInputStreamToString(iStr, data, streamLen));
        return do_OnDataAvailable(request, aSourceOffset, data.BeginReading(),
                                  data.Length());
      }

      nsCOMPtr<nsIStreamListener> listener;
      {
        MutexAutoLock lock(mMutex);
        listener = mListener;
      }
      rv = listener->OnDataAvailable(request, iStr, aSourceOffset, aCount);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } break;
  } /* switch */

  return NS_OK;
} /* OnDataAvailable */

// XXX/ruslan: need to implement this too

NS_IMETHODIMP
nsHTTPCompressConv::Convert(nsIInputStream* aFromStream, const char* aFromType,
                            const char* aToType, nsISupports* aCtxt,
                            nsIInputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsHTTPCompressConv::do_OnDataAvailable(nsIRequest* request,
                                                uint64_t offset,
                                                const char* buffer,
                                                uint32_t count) {
  LOG(
      ("nsHttpCompressConv %p do_OnDataAvailable mDispatchToMainThread %d "
       "count %u",
       this, bool(mDispatchToMainThread), count));
  if (count == 0) {
    // Never send 0-byte OnDataAvailables; imglib at least barfs on them and
    // they're not useful
    return NS_OK;
  }
  if (mDispatchToMainThread && !NS_IsMainThread()) {
    nsCOMPtr<nsIInputStream> stream;
    MOZ_TRY(NS_NewByteInputStream(getter_AddRefs(stream), Span(buffer, count),
                                  nsAssignmentType::NS_ASSIGNMENT_COPY));

    nsCOMPtr<nsIStreamListener> listener;
    {
      MutexAutoLock lock(mMutex);
      listener = mListener;
    }

    mDecodedDataLength += count;
    // Serialize bounced data and stop notifications across nested event loops.
    mMainThreadEventQueue->RunOrEnqueue(std::make_unique<ChannelFunctionEvent>(
        []() { return do_AddRef(GetMainThreadSerialEventTarget()); },
        [self = RefPtr{this}, request{RefPtr<nsIRequest>(request)},
         stream{std::move(stream)}, listener{std::move(listener)}, offset,
         count]() {
          AutoEventEnqueuer ensureSerialDispatch(self->mMainThreadEventQueue);
          LOG(("nsHttpCompressConv Calling OnDataAvailable on Mainthread"));
          (void)listener->OnDataAvailable(request, stream, offset, count);
        }));

    return NS_OK;
  }

  if (!mStream) {
    mStream = do_CreateInstance(NS_STRINGINPUTSTREAM_CONTRACTID);
    NS_ENSURE_STATE(mStream);
  }

  mStream->ShareData(buffer, count);

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
  }
  LOG(("nsHTTPCompressConv::do_OnDataAvailable req:%p offset: offset:%" PRIu64
       "count:%u",
       request, offset, count));
  nsresult rv = listener->OnDataAvailable(request, mStream, offset, count);

  // Make sure the stream no longer references |buffer| in case our listener
  // is crazy enough to try to read from |mStream| after ODA.
  mStream->ShareData("", 0);
  mDecodedDataLength += count;

  return rv;
}

#define ASCII_FLAG 0x01  /* bit 0 set: file probably ascii text */
#define HEAD_CRC 0x02    /* bit 1 set: header CRC present */
#define EXTRA_FIELD 0x04 /* bit 2 set: extra field present */
#define ORIG_NAME 0x08   /* bit 3 set: original file name present */
#define COMMENT 0x10     /* bit 4 set: file comment present */
#define RESERVED 0xE0    /* bits 5..7: reserved */

static unsigned gz_magic[2] = {GZIP_MAGIC_0,
                               GZIP_MAGIC_1}; /* gzip magic header */

uint32_t nsHTTPCompressConv::check_header(nsIInputStream* iStr,
                                          uint32_t streamLen, nsresult* rs) {
  enum {
    GZIP_INIT = 0,
    GZIP_OS,
    GZIP_EXTRA0,
    GZIP_EXTRA1,
    GZIP_EXTRA2,
    GZIP_ORIG,
    GZIP_COMMENT,
    GZIP_CRC
  };
  char c;

  *rs = NS_OK;

  if (mCheckHeaderDone) {
    return streamLen;
  }

  while (streamLen) {
    switch (hMode) {
      case GZIP_INIT:
        uint32_t unused;
        iStr->Read(&c, 1, &unused);
        streamLen--;

        if (mSkipCount == 0 && ((unsigned)c & 0377) != gz_magic[0]) {
          *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
          return 0;
        }

        if (mSkipCount == 1 && ((unsigned)c & 0377) != gz_magic[1]) {
          *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
          return 0;
        }

        if (mSkipCount == 2 && ((unsigned)c & 0377) != Z_DEFLATED) {
          *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
          return 0;
        }

        mSkipCount++;
        if (mSkipCount == 4) {
          mFlags = (unsigned)c & 0377;
          if (mFlags & RESERVED) {
            *rs = NS_ERROR_INVALID_CONTENT_ENCODING;
            return 0;
          }
          hMode = GZIP_OS;
          mSkipCount = 0;
        }
        break;

      case GZIP_OS:
        iStr->Read(&c, 1, &unused);
        streamLen--;
        mSkipCount++;

        if (mSkipCount == 6) {
          hMode = GZIP_EXTRA0;
        }
        break;

      case GZIP_EXTRA0:
        if (mFlags & EXTRA_FIELD) {
          iStr->Read(&c, 1, &unused);
          streamLen--;
          mLen = (uInt)c & 0377;
          hMode = GZIP_EXTRA1;
        } else {
          hMode = GZIP_ORIG;
        }
        break;

      case GZIP_EXTRA1:
        iStr->Read(&c, 1, &unused);
        streamLen--;
        mLen |= ((uInt)c & 0377) << 8;
        mSkipCount = 0;
        hMode = GZIP_EXTRA2;
        break;

      case GZIP_EXTRA2:
        if (mSkipCount == mLen) {
          hMode = GZIP_ORIG;
        } else {
          iStr->Read(&c, 1, &unused);
          streamLen--;
          mSkipCount++;
        }
        break;

      case GZIP_ORIG:
        if (mFlags & ORIG_NAME) {
          iStr->Read(&c, 1, &unused);
          streamLen--;
          if (c == 0) hMode = GZIP_COMMENT;
        } else {
          hMode = GZIP_COMMENT;
        }
        break;

      case GZIP_COMMENT:
        if (mFlags & COMMENT) {
          iStr->Read(&c, 1, &unused);
          streamLen--;
          if (c == 0) {
            hMode = GZIP_CRC;
            mSkipCount = 0;
          }
        } else {
          hMode = GZIP_CRC;
          mSkipCount = 0;
        }
        break;

      case GZIP_CRC:
        if (mFlags & HEAD_CRC) {
          iStr->Read(&c, 1, &unused);
          streamLen--;
          mSkipCount++;
          if (mSkipCount == 2) {
            mCheckHeaderDone = true;
            return streamLen;
          }
        } else {
          mCheckHeaderDone = true;
          return streamLen;
        }
        break;
    }
  }
  return streamLen;
}

NS_IMETHODIMP
nsHTTPCompressConv::CheckListenerChain() {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIThreadRetargetableStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = do_QueryInterface(mListener);
  }

  nsresult rv = NS_ERROR_NO_INTERFACE;
  if (listener) {
    rv = listener->CheckListenerChain();
  }

  // handle decompression OMT always.  If the chain needs to be MT,
  // we'll determine that in OnStartRequest and dispatch to MT
  bool alwaysOMT = XRE_IsContentProcess() &&
                   StaticPrefs::network_decompression_off_mainthread2();

  if (NS_FAILED(rv) && alwaysOMT) {
    SetDispatchToMainThread();
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsHTTPCompressConv::OnDataFinished(nsresult aStatus) {
  if (mDispatchToMainThread && !NS_IsMainThread()) {
    // If this is called off main thread, but the listener can only
    // handle calls on the main thread, then just return.
    // Also important - never QI the listener off main thread
    // if mDispatchToMainThread is true, because the listener
    // might be JS implemented and won't support that.
    return NS_OK;
  }

  nsCOMPtr<nsIThreadRetargetableStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = do_QueryInterface(mListener);
  }

  if (listener) {
    return listener->OnDataFinished(aStatus);
  }

  return NS_OK;
}

}  // namespace net
}  // namespace mozilla

nsresult NS_NewHTTPCompressConv(
    mozilla::net::nsHTTPCompressConv** aHTTPCompressConv) {
  MOZ_ASSERT(aHTTPCompressConv != nullptr, "null ptr");
  if (!aHTTPCompressConv) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<mozilla::net::nsHTTPCompressConv> outVal =
      new mozilla::net::nsHTTPCompressConv();
  if (!outVal) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  outVal.forget(aHTTPCompressConv);
  return NS_OK;
}
