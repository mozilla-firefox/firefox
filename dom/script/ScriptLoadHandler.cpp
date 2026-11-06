/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptLoadHandler.h"

#include <stdlib.h>

#include <utility>

#include "ScriptCompression.h"
#include "ScriptLoader.h"
#include "ScriptTrace.h"
#include "js/Transcoding.h"
#include "js/loader/LoadedScript.h"
#include "js/loader/ModuleLoadRequest.h"
#include "js/loader/ScriptLoadRequest.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Encoding.h"
#include "mozilla/Logging.h"
#include "mozilla/PerfStats.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SharedSubResourceCache.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/Utf8.h"
#include "mozilla/Vector.h"
#include "mozilla/dom/CacheExpirationTime.h"
#include "mozilla/dom/Document.h"
#ifdef NIGHTLY_BUILD
#  include "mozilla/dom/IntegrityPolicyWAICT.h"
#  include "mozilla/dom/PolicyContainer.h"
#  include "mozilla/dom/ResourceHasher.h"
#  include "mozilla/dom/WAICTUtils.h"
#endif
#include "mozilla/dom/SRICheck.h"
#include "mozilla/dom/ScriptDecoding.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICacheInfoChannel.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsIRequest.h"
#include "nsIScriptElement.h"
#include "nsIURI.h"
#include "nsJSUtils.h"
#include "nsMimeTypes.h"
#include "nsString.h"
#include "nsTArray.h"
#include "zlib.h"

namespace mozilla::dom {

#ifdef NIGHTLY_BUILD
using mozilla::waict::gWaictLog;
#endif

#undef LOG
#define LOG(args) \
  MOZ_LOG(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug, args)

#define LOG_ENABLED() \
  MOZ_LOG_TEST(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug)

ScriptDecoder::ScriptDecoder(const Encoding* aEncoding,
                             ScriptDecoder::BOMHandling handleBOM) {
  if (handleBOM == BOMHandling::Ignore) {
    mDecoder = aEncoding->NewDecoderWithoutBOMHandling();
  } else {
    mDecoder = aEncoding->NewDecoderWithBOMRemoval();
  }
  MOZ_ASSERT(mDecoder);
}

template <typename Unit>
nsresult ScriptDecoder::DecodeRawDataHelper(
    JS::loader::ScriptLoadRequest* aRequest, const uint8_t* aData,
    uint32_t aDataLength, bool aEndOfStream) {
  CheckedInt<size_t> needed =
      ScriptDecoding<Unit>::MaxBufferLength(mDecoder, aDataLength);
  if (!needed.isValid()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Reference to the script source buffer which we will update.
  JS::loader::ScriptLoadRequest::ScriptTextBuffer<Unit>& scriptText =
      aRequest->ScriptText<Unit>();

  uint32_t haveRead = scriptText.length();

  CheckedInt<uint32_t> capacity = haveRead;
  capacity += needed.value();

  if (!capacity.isValid() || !scriptText.resize(capacity.value())) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  size_t written = ScriptDecoding<Unit>::DecodeInto(
      mDecoder, Span(aData, aDataLength),
      Span(scriptText.begin() + haveRead, needed.value()), aEndOfStream);
  MOZ_ASSERT(written <= needed.value());

  haveRead += written;
  MOZ_ASSERT(haveRead <= capacity.value(),
             "mDecoder produced more data than expected");
  MOZ_ALWAYS_TRUE(scriptText.resize(haveRead));
  aRequest->SetReceivedScriptTextLength(scriptText.length());

  return NS_OK;
}

nsresult ScriptDecoder::DecodeRawData(JS::loader::ScriptLoadRequest* aRequest,
                                      const uint8_t* aData,
                                      uint32_t aDataLength, bool aEndOfStream) {
  if (aRequest->IsUTF16Text()) {
    return DecodeRawDataHelper<char16_t>(aRequest, aData, aDataLength,
                                         aEndOfStream);
  }

  return DecodeRawDataHelper<Utf8Unit>(aRequest, aData, aDataLength,
                                       aEndOfStream);
}

ScriptLoadHandler::ScriptLoadHandler(
    ScriptLoader* aScriptLoader, JS::loader::ScriptLoadRequest* aRequest,
    UniquePtr<SRICheckDataVerifier>&& aSRIDataVerifier)
    : mScriptLoader(aScriptLoader),
      mRequest(aRequest),
      mSRIDataVerifier(std::move(aSRIDataVerifier)),
      mSRIStatus(NS_OK) {
  MOZ_ASSERT(aRequest->IsUnknownDataType());
  MOZ_ASSERT(!aRequest->IsRetrievedFromMemoryCache());
  MOZ_ASSERT(aRequest->IsFetching());
}

ScriptLoadHandler::~ScriptLoadHandler() = default;

NS_IMPL_ISUPPORTS(ScriptLoadHandler, nsIIncrementalStreamLoaderObserver,
                  nsIChannelEventSink, nsIInterfaceRequestor)

NS_IMETHODIMP
ScriptLoadHandler::OnStartRequest(nsIRequest* aRequest) {
  mRequest->SetMinimumExpirationTime(
      nsContentUtils::GetSubresourceCacheExpirationTime(aRequest,
                                                        mRequest->URI()));

#ifdef NIGHTLY_BUILD
  // Only create a ResourceHasher when we need to enforce WAICT.
  if (mScriptLoader->WAICTHandlesScripts()) {
    mResourceHasher = mozilla::dom::ResourceHasher::Init();
  }
#endif

  return NS_OK;
}

NS_IMETHODIMP
ScriptLoadHandler::OnIncrementalData(nsIIncrementalStreamLoader* aLoader,
                                     nsISupports* aContext,
                                     uint32_t aDataLength, const uint8_t* aData,
                                     uint32_t* aConsumedLength) {
  nsCOMPtr<nsIRequest> channelRequest;
  aLoader->GetRequest(getter_AddRefs(channelRequest));
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(channelRequest);
  MOZ_ASSERT(channel, "StreamLoader must have a channel");

  auto firstTime = !mPreloadStartNotified;
  if (!mPreloadStartNotified) {
    mPreloadStartNotified = true;
    mRequest->GetScriptLoadContext()->NotifyStart(channelRequest);
  }

  if (mRequest->IsCanceled()) {
    // If request cancelled, ignore any incoming data.
    *aConsumedLength = aDataLength;
    return NS_OK;
  }

  nsresult rv = NS_OK;
  if (mRequest->IsUnknownDataType()) {
    rv = EnsureKnownDataType(channel);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mRequest->IsRetrievedAsSerializedStencil() && firstTime) {
    PerfStats::RecordMeasurementStart(PerfStats::Metric::JSBC_IO_Read);
  }

  if (mRequest->IsFetchedAsTextSource()) {
#ifdef NIGHTLY_BUILD
    // If we have a resource hasher, update it with the new data.
    if (mResourceHasher) {
      rv = mResourceHasher->Update(aData, aDataLength);
      NS_ENSURE_SUCCESS(rv, rv);
    }
#endif

    if (!EnsureDecoder(channel, aData, aDataLength,
                       /* aEndOfStream = */ false)) {
      return NS_OK;
    }

    // Below we will/shall consume entire data chunk.
    *aConsumedLength = aDataLength;

    // Decoder has already been initialized. -- trying to decode all loaded
    // bytes.
    rv = mDecoder->DecodeRawData(mRequest, aData, aDataLength,
                                 /* aEndOfStream = */ false);
    NS_ENSURE_SUCCESS(rv, rv);

    // If SRI is required for this load, appending new bytes to the hash.
    if (mSRIDataVerifier && NS_SUCCEEDED(mSRIStatus)) {
      mSRIStatus = mSRIDataVerifier->Update(aDataLength, aData);
    }
  } else if (mRequest->IsWasmBytes()) {
    auto& wasmBytes = mRequest->WasmBytes();
    if (!wasmBytes.append(aData, aDataLength)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    *aConsumedLength = aDataLength;
  } else {
    MOZ_ASSERT(mRequest->IsRetrievedAsSerializedStencil());
    if (!mRequest->SRIAndSerializedStencil().append(aData, aDataLength)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    *aConsumedLength = aDataLength;
    uint32_t sriLength = 0;
    rv = MaybeDecodeSRI(&sriLength);
    if (NS_FAILED(rv)) {
      return channelRequest->Cancel(mScriptLoader->RestartLoad(mRequest));
    }
    if (sriLength) {
      uint32_t alignedSRILength = JS::AlignTranscodingBytecodeOffset(sriLength);
      mRequest->SetAlignedSRILength(alignedSRILength);
    }
  }

  return rv;
}

bool ScriptLoadHandler::TrySetDecoder(nsIChannel* aChannel,
                                      const uint8_t* aData,
                                      uint32_t aDataLength, bool aEndOfStream) {
  MOZ_ASSERT(mDecoder == nullptr,
             "can't have a decoder already if we're trying to set one");

  // JavaScript modules are always UTF-8.
  if (mRequest->IsModuleRequest()) {
    MOZ_ASSERT(!mRequest->getLoadedScript()->ClassicScriptEncoding());
    mDecoder = std::make_unique<ScriptDecoder>(
        UTF_8_ENCODING, ScriptDecoder::BOMHandling::Remove);
    return true;
  }

  MOZ_ASSERT(mRequest->IsClassicScript());

  // Determine if BOM check should be done.  This occurs either
  // if end-of-stream has been reached, or at least 3 bytes have
  // been read from input.
  if (!aEndOfStream && (aDataLength < 3)) {
    return false;
  }

  // Do BOM detection.
  const Encoding* encoding;
  std::tie(encoding, std::ignore) = Encoding::ForBOM(Span(aData, aDataLength));
  if (encoding) {
    mRequest->getLoadedScript()->SetClassicScriptEncodingFromBOM(encoding);
    mDecoder = std::make_unique<ScriptDecoder>(
        encoding, ScriptDecoder::BOMHandling::Remove);
    return true;
  }

  // BOM detection failed, check content stream for charset.
  nsAutoCString label;
  if (NS_SUCCEEDED(aChannel->GetContentCharset(label)) &&
      (encoding = Encoding::ForLabel(label))) {
    mRequest->getLoadedScript()->SetClassicScriptEncodingFromCharsetParameter(
        encoding);
    mDecoder = std::make_unique<ScriptDecoder>(
        encoding, ScriptDecoder::BOMHandling::Ignore);
    return true;
  }

  encoding = mScriptLoader->GetClassicScriptFallbackEncoding(mRequest);
  mRequest->getLoadedScript()->SetClassicScriptEncodingFromFallback(encoding);
  mDecoder = std::make_unique<ScriptDecoder>(
      encoding, ScriptDecoder::BOMHandling::Ignore);
  return true;
}

nsresult ScriptLoadHandler::MaybeDecodeSRI(uint32_t* sriLength) {
  *sriLength = 0;

  if (!mSRIDataVerifier || mSRIDataVerifier->IsInvalid() ||
      mSRIDataVerifier->IsComplete() || NS_FAILED(mSRIStatus)) {
    return NS_OK;
  }

  // Skip until the content is large enough to be decoded.
  JS::TranscodeBuffer& receivedData = mRequest->SRIAndSerializedStencil();
  if (receivedData.length() <= mSRIDataVerifier->DataSummaryLength()) {
    return NS_OK;
  }

  mSRIStatus = mSRIDataVerifier->ImportDataSummary(receivedData.length(),
                                                   receivedData.begin());

  if (NS_FAILED(mSRIStatus)) {
    // We are unable to decode the hash contained in the alternate data which
    // contains the serialized Stencil, or it does not use the same algorithm.
    LOG(
        ("ScriptLoadHandler::MaybeDecodeSRI, failed to decode SRI, restart "
         "request"));
    return mSRIStatus;
  }

  *sriLength = mSRIDataVerifier->DataSummaryLength();
  MOZ_ASSERT(*sriLength > 0);
  return NS_OK;
}

nsresult ScriptLoadHandler::EnsureKnownDataType(nsIChannel* aChannel) {
  MOZ_ASSERT(mRequest->IsUnknownDataType());
  MOZ_ASSERT(!mRequest->IsRetrievedFromMemoryCache());
  MOZ_ASSERT(mRequest->IsFetching());

#ifdef NIGHTLY_BUILD
  if (StaticPrefs::javascript_options_experimental_wasm_esm_integration()) {
    if (mRequest->IsModuleRequest()) {
      // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-single-module-script
      // Extract the content-type. If its essence is wasm, we'll attempt to
      // compile this module as a wasm module. (Steps 13.2, 13.6)
      nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
      if (httpChannel) {
        nsAutoCString mimeType;
        if (NS_SUCCEEDED(httpChannel->GetContentType(mimeType))) {
          if (nsContentUtils::HasWasmMimeTypeEssence(
                  NS_ConvertUTF8toUTF16(mimeType))) {
            mRequest->AsModuleRequest()->SetHasWasmMimeTypeEssence();
            mRequest->getLoadedScript()->SetWasmBytes();
            return NS_OK;
          }
        }
      }
    }
  }
#endif

  if (mRequest->mFetchSourceOnly) {
    mRequest->SetTextSource(mRequest->mLoadContext.get());
    TRACE_FOR_TEST(mRequest, "load:source");
    return NS_OK;
  }

  if (nsCOMPtr<nsICacheInfoChannel> cic = do_QueryInterface(aChannel)) {
    nsAutoCString altDataType;
    cic->GetAlternativeDataType(altDataType);
    if (altDataType.Equals(ScriptLoader::BytecodeMimeTypeFor(mRequest))) {
      mRequest->SetSerializedStencil();
      TRACE_FOR_TEST(mRequest, "load:diskcache");
      return NS_OK;
    }
    MOZ_ASSERT(altDataType.IsEmpty());
  }

  mRequest->SetTextSource(mRequest->mLoadContext.get());
  TRACE_FOR_TEST(mRequest, "load:source");

  MOZ_ASSERT(!mRequest->IsUnknownDataType());
  MOZ_ASSERT(mRequest->IsFetching());
  return NS_OK;
}

NS_IMETHODIMP
ScriptLoadHandler::OnStreamComplete(nsIIncrementalStreamLoader* aLoader,
                                    nsISupports* aContext, nsresult aStatus,
                                    uint32_t aDataLength, const uint8_t* aData)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  nsCOMPtr<nsIRequest> channelRequest;
  aLoader->GetRequest(getter_AddRefs(channelRequest));
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(channelRequest);

#ifndef NIGHTLY_BUILD
  return DoOnStreamComplete(channel, aStatus, aDataLength, aData);
#else
  if (!mResourceHasher) {
    return DoOnStreamComplete(channel, aStatus, aDataLength, aData);
  }

  nsresult rv = mResourceHasher->Update(aData, aDataLength);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gWaictLog, LogLevel::Error,
            ("ScriptLoadHandler::OnStreamComplete: Failed to update resource "
             "hash\n"));
    return rv;
  }

  mResourceHasher->Finish();
  nsAutoCString computedHash(mResourceHasher->GetHash());
  if (computedHash.IsEmpty()) {
    MOZ_LOG_FMT(
        gWaictLog, LogLevel::Error,
        "ScriptLoadHandler::OnStreamComplete: Failed to compute resource hash");
    return NS_ERROR_FAILURE;
  }

  RefPtr<IntegrityPolicyWAICT> integrity =
      mScriptLoader->mDocument
          ? PolicyContainer::GetIntegrityPolicyWAICT(
                mScriptLoader->mDocument->GetPolicyContainer())
          : nullptr;
  if (!integrity) {
    MOZ_LOG_FMT(
        gWaictLog, LogLevel::Error,
        "ScriptLoadHandler::OnStreamComplete: Could not get IntegrityPolicy");
    return NS_ERROR_FAILURE;
  }

  integrity->WaitForManifestLoad()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, channel = std::move(channel),
       integrity = RefPtr{integrity}, computedHash = std::move(computedHash),
       aStatus, aDataLength, aData](bool) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        MOZ_LOG_FMT(gWaictLog, LogLevel::Debug,
                    "ScriptLoadHandler::OnStreamComplete: WaitForManifestLoad "
                    "promise resolved");

        // Using NS_SUCCESS_ADOPTED_DATA we are taking ownership of the data, so
        // we have to free it after DoOnStreamComplete completes.
        std::unique_ptr<const uint8_t> data{aData};

        // We have to use the pre-redirect URL for the check.
        nsCOMPtr<nsIURI> originalURI;
        channel->GetOriginalURI(getter_AddRefs(originalURI));
        if (!integrity->MaybeCheckResourceIntegrity(
                originalURI, IntegrityPolicy::DestinationType::Script,
                computedHash)) {
          MOZ_LOG_FMT(gWaictLog, LogLevel::Warning,
                      "ScriptLoadHandler::OnStreamComplete: Wrong script hash");
          self->DoOnStreamComplete(channel, NS_ERROR_FAILURE, aDataLength,
                                   data.get());
          return;
        }

        MOZ_LOG_FMT(
            gWaictLog, LogLevel::Debug,
            "ScriptLoadHandler::OnStreamComplete: Correct script hash :)");
        self->DoOnStreamComplete(channel, aStatus, aDataLength, data.get());
      },
      [](bool) {
        MOZ_ASSERT_UNREACHABLE(
            "WaitForManifestLoad() promise should never be rejected");
      });

  return NS_SUCCESS_ADOPTED_DATA;
#endif
}

nsresult ScriptLoadHandler::DoOnStreamComplete(nsIChannel* aChannel,
                                               nsresult aStatus,
                                               uint32_t aDataLength,
                                               const uint8_t* aData) {
  nsresult rv = NS_OK;
  if (LOG_ENABLED()) {
    nsAutoCString url;
    mRequest->URI()->GetAsciiSpec(url);
    LOG(("ScriptLoadRequest (%p): Stream complete (url = %s)", mRequest.get(),
         url.get()));
  }

  mRequest->mNetworkMetadata = new SubResourceNetworkMetadataHolder(aChannel);

  aChannel->SetNotificationCallbacks(nullptr);

  auto firstMessage = !mPreloadStartNotified;
  if (!mPreloadStartNotified) {
    mPreloadStartNotified = true;
    mRequest->GetScriptLoadContext()->NotifyStart(aChannel);
  }

  auto notifyStop = MakeScopeExit(
      [&] { mRequest->GetScriptLoadContext()->NotifyStop(aChannel, rv); });

  if (!mRequest->IsCanceled()) {
    if (mRequest->IsUnknownDataType()) {
      rv = EnsureKnownDataType(aChannel);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (mRequest->IsRetrievedAsSerializedStencil() && !firstMessage) {
      // if firstMessage, then entire stream is in aData, and PerfStats would
      // measure 0 time
      PerfStats::RecordMeasurementEnd(PerfStats::Metric::JSBC_IO_Read);
    }

    if (mRequest->IsFetchedAsTextSource()) {
      DebugOnly<bool> encoderSet = EnsureDecoder(aChannel, aData, aDataLength,
                                                 /* aEndOfStream = */ true);
      MOZ_ASSERT(encoderSet);
      rv = mDecoder->DecodeRawData(mRequest, aData, aDataLength,
                                   /* aEndOfStream = */ true);
      NS_ENSURE_SUCCESS(rv, rv);

      LOG(("ScriptLoadRequest (%p): Source length in code units = %u",
           mRequest.get(), unsigned(mRequest->ScriptTextLength())));

      // If SRI is required for this load, appending new bytes to the hash.
      if (mSRIDataVerifier && NS_SUCCEEDED(mSRIStatus)) {
        mSRIStatus = mSRIDataVerifier->Update(aDataLength, aData);
      }
    } else if (mRequest->IsWasmBytes()) {
      auto& wasmBytes = mRequest->WasmBytes();
      if (!wasmBytes.append(aData, aDataLength)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    } else {
      MOZ_ASSERT(mRequest->IsRetrievedAsSerializedStencil());

      if (NS_SUCCEEDED(aStatus)) {
        JS::TranscodeBuffer& buf = mRequest->SRIAndSerializedStencil();
        if (!buf.append(aData, aDataLength)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }

        LOG(("ScriptLoadRequest (%p): SRIAndSerializedStencil length = %u",
             mRequest.get(), unsigned(buf.length())));

        // If we abort while decoding the SRI, we fallback on explicitly
        // requesting the source. Thus, we should not continue in
        // ScriptLoader::OnStreamComplete, which removes the request from the
        // waiting lists.
        //
        // We calculate the SRI length below.
        uint32_t unused;
        rv = MaybeDecodeSRI(&unused);
        if (NS_FAILED(rv)) {
          return aChannel->Cancel(mScriptLoader->RestartLoad(mRequest));
        }

        // The serialized stencil always starts with the SRI hash, thus even if
        // there is no SRI data verifier instance, we still want to skip the
        // hash.
        uint32_t sriLength;
        rv = SRICheckDataVerifier::DataSummaryLength(buf.length(), buf.begin(),
                                                     &sriLength);
        if (NS_FAILED(rv)) {
          return aChannel->Cancel(mScriptLoader->RestartLoad(mRequest));
        }

        uint32_t alignedSRILength =
            JS::AlignTranscodingBytecodeOffset(sriLength);
        if (alignedSRILength > buf.length() ||
            buf.length() - alignedSRILength <
                JS::loader::LoadedScript::EncodingHeaderSize) {
          return aChannel->Cancel(mScriptLoader->RestartLoad(mRequest));
        }

        mRequest->SetAlignedSRILength(alignedSRILength);

        const JS::loader::LoadedScript::EncodingHeader* header =
            reinterpret_cast<const JS::loader::LoadedScript::EncodingHeader*>(
                buf.begin() + alignedSRILength);
        if (!mRequest->getLoadedScript()->ReadFromEncodingHeader(header)) {
          // Corrupted data.
          return aChannel->Cancel(mScriptLoader->RestartLoad(mRequest));
        }

        if (mRequest->IsClassicScript() &&
            mRequest->getLoadedScript()->DependsOnClassicScriptHintEncoding()) {
          const Encoding* fallbackEncoding =
              mScriptLoader->GetClassicScriptFallbackEncoding(mRequest);
          if (mRequest->getLoadedScript()->ClassicScriptEncoding() !=
              fallbackEncoding) {
            return aChannel->Cancel(mScriptLoader->RestartLoad(mRequest));
          }
        }

        Vector<uint8_t> compressed;
        // mRequest has the compressed data, but will be filled with the
        // uncompressed data
        compressed.swap(buf);
        if (!JS::loader::ScriptBytecodeDecompress(
                compressed, mRequest->GetSerializedStencilOffset(), buf)) {
          return NS_ERROR_UNEXPECTED;
        }
      }
    }
  }

  // Everything went well, keep the CacheInfoChannel alive such that we can
  // later save the serialized stencil on the cache entry.
  // we have to mediate and use mRequest.
  const RefPtr<ScriptLoader> scriptLoader = mScriptLoader;
  const RefPtr<JS::loader::ScriptLoadRequest> request = mRequest;
  rv = scriptLoader->OnStreamComplete(aChannel, request, aStatus, mSRIStatus,
                                      mSRIDataVerifier.get());

  return rv;
}

NS_IMETHODIMP
ScriptLoadHandler::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    return QueryInterface(aIID, aResult);
  }

  return NS_NOINTERFACE;
}

nsresult ScriptLoadHandler::AsyncOnChannelRedirect(
    nsIChannel* aOld, nsIChannel* aNew, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCallback) {
  mRequest->SetHadRedirected();
  mRequest->SetMinimumExpirationTime(
      nsContentUtils::GetSubresourceCacheExpirationTime(aOld, mRequest->URI()));

  aCallback->OnRedirectVerifyCallback(NS_OK);

  return NS_OK;
}

#undef LOG_ENABLED
#undef LOG

}  // namespace mozilla::dom
