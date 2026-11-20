/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportParent.h"

#include <cmath>

#include "Http3WebTransportSession.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/WebTransportBinding.h"
#include "mozilla/dom/WebTransportLog.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/net/WebTransportHash.h"
#include "mozilla/net/WebTransportSessionProxy.h"
#include "nsIEventTarget.h"
#include "nsIOService.h"
#include "nsIPrincipal.h"
#include "nsIWebTransport.h"
#include "nsIWebTransportStream.h"
#include "nsStreamUtils.h"

using IPCResult = mozilla::ipc::IPCResult;

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(WebTransportParent, WebTransportSessionEventListener);

using CreateWebTransportPromise =
    MozPromise<WebTransportReliabilityMode, nsresult, true>;
WebTransportParent::~WebTransportParent() {
  LOG(("Destroying WebTransportParent %p", this));
}

void WebTransportParent::Create(
    const nsAString& aURL, nsIPrincipal* aPrincipal,
    const uint64_t& aBrowsingContextID, const IPCClientInfo& aClientInfo,
    const bool& aDedicated, const bool& aRequireUnreliable,
    const uint32_t& aCongestionControl, nsTArray<nsString>&& aProtocols,
    nsTArray<WebTransportHeader>&& aHeaders,
    nsTArray<WebTransportHash>&& aServerCertHashes,
    const mozilla::Maybe<uint16_t>&
        aAnticipatedConcurrentIncomingUnidirectionalStreams,
    const mozilla::Maybe<uint16_t>&
        aAnticipatedConcurrentIncomingBidirectionalStreams,
    Endpoint<PWebTransportParent>&& aParentEndpoint,
    std::function<void(std::tuple<const nsresult&, const uint8_t&,
                                  const uint64_t&>)>&& aResolver) {
  LOG(("Created WebTransportParent %p %s %s %s congestion=%s", this,
       NS_ConvertUTF16toUTF8(aURL).get(),
       aDedicated ? "Dedicated" : "AllowPooling",
       aRequireUnreliable ? "RequireUnreliable" : "",
       aCongestionControl ==
               (uint32_t)dom::WebTransportCongestionControl::Throughput
           ? "ThroughPut"
           : (aCongestionControl ==
                      (uint32_t)dom::WebTransportCongestionControl::Low_latency
                  ? "Low-Latency"
                  : "Default")));

  if (!aParentEndpoint.IsValid()) {
    CallResolver(aResolver, NS_ERROR_INVALID_ARG,
                 static_cast<uint8_t>(WebTransportReliabilityMode::Pending), 0);
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mozilla::net::gIOService);
  nsresult rv =
      mozilla::net::gIOService->NewWebTransport(getter_AddRefs(mWebTransport));
  if (NS_FAILED(rv)) {
    CallResolver(aResolver, rv,
                 static_cast<uint8_t>(WebTransportReliabilityMode::Pending), 0);
    return;
  }

  mOwningEventTarget = GetCurrentSerialEventTarget();
  MOZ_ASSERT(aPrincipal);
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aURL);
  if (NS_FAILED(rv)) {
    CallResolver(aResolver, rv,
                 static_cast<uint8_t>(WebTransportReliabilityMode::Pending), 0);
    return;
  }

  nsTArray<RefPtr<nsIWebTransportHash>> nsServerCertHashes;
  for (const auto& hash : aServerCertHashes) {
    nsCString alg = hash.algorithm();
    nsServerCertHashes.AppendElement(
        new net::WebTransportHash(alg, hash.value()));
  }

  // The content process has already applied these restrictions, but it is
  // not trusted.
  nsTArray<nsCString> headerNames;
  nsTArray<nsCString> headerValues;
  RefPtr<InternalHeaders> requestHeaders =
      new InternalHeaders(HeadersGuardEnum::Request);
  for (const auto& header : aHeaders) {
    IgnoredErrorResult erv;
    if (header.name().LowerCaseEqualsLiteral("wt-available-protocols")) {
      erv.ThrowTypeError("wt-available-protocols is reserved");
    } else {
      requestHeaders->Append(header.name(), header.value(), erv);
    }
    if (erv.Failed()) {
      CallResolver(aResolver, NS_ERROR_INVALID_ARG,
                   static_cast<uint8_t>(WebTransportReliabilityMode::Pending),
                   0);
      return;
    }
  }
  nsTArray<InternalHeaders::Entry> headerEntries;
  requestHeaders->GetEntries(headerEntries);
  for (const auto& entry : headerEntries) {
    headerNames.AppendElement(entry.mName);
    headerValues.AppendElement(entry.mValue);
  }

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "WebTransport AsyncConnect",
      [self = RefPtr{this}, uri = std::move(uri),
       dedicated = true /* aDedicated, see BUG 1915735.*/,
       nsServerCertHashes = std::move(nsServerCertHashes),
       principal = RefPtr{aPrincipal}, browsingContextID = aBrowsingContextID,
       flags = nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
       clientInfo = ClientInfo{aClientInfo}, protocols = std::move(aProtocols),
       headerNames = std::move(headerNames),
       headerValues = std::move(headerValues),
       anticipatedUni = aAnticipatedConcurrentIncomingUnidirectionalStreams,
       anticipatedBidi = aAnticipatedConcurrentIncomingBidirectionalStreams] {
        LOG(("WebTransport %p AsyncConnect", self.get()));
        // 0 and "unset" are equivalent downstream: the limit is raise-only.
        uint16_t uniStreams = anticipatedUni.valueOr(0);
        uint16_t bidiStreams = anticipatedBidi.valueOr(0);
        nsresult rv = self->mWebTransport->AsyncConnectWithClient(
            uri, dedicated, std::move(nsServerCertHashes), principal,
            browsingContextID, flags, self, Some(clientInfo), protocols,
            headerNames, headerValues, nsIWebTransport::HTTPVersion::h3,
            uniStreams, bidiStreams);
        if (NS_FAILED(rv)) {
          LOG(("AsyncConnect failure; we should get OnSessionClosed"));
        }
      });

  // Bind to SocketThread for IPC - connection creation/destruction must
  // hit MainThread, but keep all other traffic on SocketThread.  Note that
  // we must call aResolver() on this (PBackground) thread.
  mSocketThread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  InvokeAsync(mSocketThread, __func__,
              [parentEndpoint = std::move(aParentEndpoint), runnable = r,
               resolver = std::move(aResolver), p = RefPtr{this}]() mutable {
                {
                  MutexAutoLock lock(p->mMutex);
                  p->mResolver = resolver;
                }

                LOG(("Binding parent endpoint"));
                if (!parentEndpoint.Bind(p)) {
                  return CreateWebTransportPromise::CreateAndReject(
                      NS_ERROR_FAILURE, __func__);
                }
                // IPC now holds a ref to parent
                // Send connection to the server via MainThread
                NS_DispatchToMainThread(runnable, NS_DISPATCH_NORMAL);

                return CreateWebTransportPromise::CreateAndResolve(
                    WebTransportReliabilityMode::Supports_unreliable, __func__);
              })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [p = RefPtr{this}](
              const CreateWebTransportPromise::ResolveOrRejectValue& aValue) {
            if (aValue.IsReject()) {
              std::function<void(ResolveType)> resolver;
              {
                MutexAutoLock lock(p->mMutex);
                resolver = std::move(p->mResolver);
              }
              if (resolver) {
                CallResolver(
                    resolver, aValue.RejectValue(),
                    static_cast<uint8_t>(WebTransportReliabilityMode::Pending),
                    0);
              }
            }
          });
}

void WebTransportParent::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("ActorDestroy WebTransportParent %d", aWhy));
  // If the actor pair is torn down for any reason other than an explicit
  // RecvClose()/OnSessionClosed() having already run (e.g. the parent
  // unilaterally destroying this actor), notify the child so it settles any
  // still-pending promises (createXStream, atomicWrite, ready/closed)
  // instead of leaving them hanging. This mirrors
  // FileSystemManagerParent/CacheStreamControlParent's SendCloseAll() calls
  // during their own shutdown paths. A no-op if the session already closed
  // cleanly: RemoteClosed() ignores this once state is closed/failed.
  if (CanSend()) {
    (void)SendCloseAll();
  }
  // Before mWebTransport is retargeted to the socket thread, the
  // QueryNegotiatedProtocol runnable from OnSessionReady() closes it instead.
  if (mRetargeted) {
    CloseSessionForDestroyedActor();
  }
}

void WebTransportParent::CloseSessionForDestroyedActor() {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  MOZ_ASSERT(mRetargeted);
  {
    MutexAutoLock lock(mMutex);
    if (mClosed) {
      return;
    }
    mClosed.Flip();
    mExecuteAfterResolverCallback = nullptr;
  }
  LOG(("Closing session %p for destroyed actor", this));
  mWebTransport->CloseSession(0, ""_ns);
}

// We may not receive this response if the child side is destroyed without
// `Close` or `Shutdown` being explicitly called.
IPCResult WebTransportParent::RecvClose(const uint32_t& aCode,
                                        const nsACString& aReason,
                                        CloseResolver&& aResolver) {
  LOG(("Close for %p received, code = %u, reason = %s", this, aCode,
       PromiseFlatCString(aReason).get()));
  if (!mSessionReady) {
    return IPC_FAIL(this, "Close received before session was ready");
  }

  // Close and get stats synchronously
  Maybe<WebTransportStatsData> stats;
  if (mWebTransport) {
    // Cast to access internal method for synchronous close with stats
    RefPtr<net::WebTransportSessionProxy> proxy =
        static_cast<net::WebTransportSessionProxy*>(mWebTransport.get());

    WebTransportStatsData statsData;
    if (proxy->CloseSessionAndGetStats(aCode, aReason, statsData)) {
      stats = Some(statsData);
      LOG(("Retrieved stats from close: bytesSent=%llu",
           (unsigned long long)statsData.bytesSent()));
    } else {
      LOG(("No stats available from close"));
    }
  }

  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(!mClosed);
    mClosed.Flip();
  }

  // Return stats to child
  LOG(("Returning stats to child: stats.isSome()=%d", stats.isSome()));
  aResolver(stats);

  // CloseSession() drops the proxy's listener and its queued events, so an
  // in-flight gather would never call back; settle it with the stats we just
  // took at close time.
  ResolvePendingGetStats(stats);

  // Clean up - CloseSession to trigger state cleanup
  if (mWebTransport) {
    mWebTransport->CloseSession(aCode, aReason);
  }
  Close();
  return IPC_OK();
}

class BidiReceiveStream : public nsIWebTransportStreamCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBTRANSPORTSTREAMCALLBACK

  BidiReceiveStream(
      WebTransportParent::CreateBidirectionalStreamResolver&& aResolver,
      std::function<
          void(uint64_t, WebTransportParent::OnResetOrStopSendingCallback&&,
               nsIWebTransportBidirectionalStream* aStream)>&& aStreamCallback,
      int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
      nsCOMPtr<nsISerialEventTarget>& aSocketThread)
      : mResolver(std::move(aResolver)),
        mStreamCallback(std::move(aStreamCallback)),
        mSendOrder(aSendOrder),
        mSendGroupId(aSendGroupId),
        mSocketThread(aSocketThread) {}

 private:
  virtual ~BidiReceiveStream() = default;
  WebTransportParent::CreateBidirectionalStreamResolver mResolver;
  std::function<void(uint64_t,
                     WebTransportParent::OnResetOrStopSendingCallback&&,
                     nsIWebTransportBidirectionalStream* aStream)>
      mStreamCallback;
  int64_t mSendOrder;
  Maybe<uint64_t> mSendGroupId;
  nsCOMPtr<nsISerialEventTarget> mSocketThread;
};

class UniReceiveStream : public nsIWebTransportStreamCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBTRANSPORTSTREAMCALLBACK

  UniReceiveStream(
      WebTransportParent::CreateUnidirectionalStreamResolver&& aResolver,
      std::function<void(uint64_t,
                         WebTransportParent::OnResetOrStopSendingCallback&&,
                         nsIWebTransportSendStream* aStream)>&& aStreamCallback,
      int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
      nsCOMPtr<nsISerialEventTarget>& aSocketThread)
      : mResolver(std::move(aResolver)),
        mStreamCallback(std::move(aStreamCallback)),
        mSendOrder(aSendOrder),
        mSendGroupId(aSendGroupId),
        mSocketThread(aSocketThread) {}

 private:
  virtual ~UniReceiveStream() = default;
  WebTransportParent::CreateUnidirectionalStreamResolver mResolver;
  std::function<void(uint64_t,
                     WebTransportParent::OnResetOrStopSendingCallback&&,
                     nsIWebTransportSendStream* aStream)>
      mStreamCallback;
  int64_t mSendOrder;
  Maybe<uint64_t> mSendGroupId;
  nsCOMPtr<nsISerialEventTarget> mSocketThread;
};

NS_IMPL_ISUPPORTS(BidiReceiveStream, nsIWebTransportStreamCallback)
NS_IMPL_ISUPPORTS(UniReceiveStream, nsIWebTransportStreamCallback)

// nsIWebTransportStreamCallback:
NS_IMETHODIMP BidiReceiveStream::OnBidirectionalStreamReady(
    nsIWebTransportBidirectionalStream* aStream) {
  LOG(("Bidirectional stream ready!"));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  aStream->SetSendOrder(mSendOrder);
  if (mSendGroupId.isSome() && mSendGroupId.value() != 0) {
    aStream->SetSendGroup(mSendGroupId.value());
  }

  RefPtr<mozilla::ipc::DataPipeSender> inputsender;
  RefPtr<mozilla::ipc::DataPipeReceiver> inputreceiver;
  nsresult rv =
      NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                  getter_AddRefs(inputsender), getter_AddRefs(inputreceiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  uint64_t id;
  (void)aStream->GetStreamId(&id);
  nsCOMPtr<nsIAsyncInputStream> inputStream;
  aStream->GetInputStream(getter_AddRefs(inputStream));
  MOZ_ASSERT(inputStream);
  nsCOMPtr<nsISupports> inputCopyContext;
  rv = NS_AsyncCopy(inputStream, inputsender, mSocketThread,
                    NS_ASYNCCOPY_VIA_WRITESEGMENTS,  // can we use READSEGMENTS?
                    mozilla::ipc::kDefaultDataPipeCapacity, nullptr, nullptr,
                    true, true, getter_AddRefs(inputCopyContext));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  RefPtr<mozilla::ipc::DataPipeSender> outputsender;
  RefPtr<mozilla::ipc::DataPipeReceiver> outputreceiver;
  rv =
      NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                  getter_AddRefs(outputsender), getter_AddRefs(outputreceiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  nsCOMPtr<nsIAsyncOutputStream> outputStream;
  aStream->GetOutputStream(getter_AddRefs(outputStream));
  MOZ_ASSERT(outputStream);
  nsCOMPtr<nsISupports> outputCopyContext;
  rv = NS_AsyncCopy(outputreceiver, outputStream, mSocketThread,
                    NS_ASYNCCOPY_VIA_READSEGMENTS,
                    mozilla::ipc::kDefaultDataPipeCapacity, nullptr, nullptr,
                    true, true, getter_AddRefs(outputCopyContext));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  LOG(("Returning BidirectionalStream pipe to content"));
  mResolver(BidirectionalStream(id, inputreceiver, outputsender));

  auto onResetOrStopSending = [inputCopyContext(inputCopyContext),
                               inputsender(inputsender)](nsresult aError) {
    LOG(("onReset err=%x", static_cast<uint32_t>(aError)));
    NS_CancelAsyncCopy(inputCopyContext, aError);
    inputsender->CloseWithStatus(aError);
  };

  auto onStopSending = [outputCopyContext(outputCopyContext),
                        outputreceiver(outputreceiver)](nsresult aError) {
    LOG(("onStopSending err=%x", static_cast<uint32_t>(aError)));
    NS_CancelAsyncCopy(outputCopyContext, aError);
    outputreceiver->CloseWithStatus(aError);
  };

  // Store onResetOrStopSending in WebTransportParent::mBidiStreamCallbackMap
  // and onResetOrStopSending will be called when a stream receives STOP_SENDING
  // or RESET.
  mStreamCallback(
      id,
      WebTransportParent::OnResetOrStopSendingCallback(
          std::move(onResetOrStopSending), std::move(onStopSending)),
      aStream);
  return NS_OK;
}

NS_IMETHODIMP UniReceiveStream::OnBidirectionalStreamReady(
    nsIWebTransportBidirectionalStream* aStream) {
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
UniReceiveStream::OnUnidirectionalStreamReady(
    nsIWebTransportSendStream* aStream) {
  LOG(("Unidirectional stream ready!"));
  // We should be on the Socket Thread
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  aStream->SetSendOrder(mSendOrder);
  if (mSendGroupId.isSome() && mSendGroupId.value() != 0) {
    aStream->SetSendGroup(mSendGroupId.value());
  }

  RefPtr<::mozilla::ipc::DataPipeSender> sender;
  RefPtr<::mozilla::ipc::DataPipeReceiver> receiver;
  nsresult rv = NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                            getter_AddRefs(sender), getter_AddRefs(receiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  uint64_t id;
  (void)aStream->GetStreamId(&id);
  nsCOMPtr<nsIAsyncOutputStream> outputStream;
  aStream->GetOutputStream(getter_AddRefs(outputStream));
  MOZ_ASSERT(outputStream);
  nsCOMPtr<nsISupports> copyContext;
  rv = NS_AsyncCopy(receiver, outputStream, mSocketThread,
                    NS_ASYNCCOPY_VIA_READSEGMENTS,
                    mozilla::ipc::kDefaultDataPipeCapacity, nullptr, nullptr,
                    true, true, getter_AddRefs(copyContext));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mResolver(rv);
    return rv;
  }

  LOG(("Returning UnidirectionalStream pipe to content"));
  // pass the DataPipeSender to the content process
  mResolver(UnidirectionalStream(id, sender));

  auto onResetOrStopSending = [copyContext(copyContext),
                               receiver(receiver)](nsresult aError) {
    LOG(("onResetOrStopSending err=%x", static_cast<uint32_t>(aError)));
    NS_CancelAsyncCopy(copyContext, aError);
    receiver->CloseWithStatus(aError);
  };

  // Store onResetOrStopSending in WebTransportParent::mStreamCallbackMap and
  // onResetOrStopSending will be called when a stream receives STOP_SENDING.
  mStreamCallback(id,
                  WebTransportParent::OnResetOrStopSendingCallback(
                      std::move(onResetOrStopSending)),
                  aStream);

  return NS_OK;
}

NS_IMETHODIMP
BidiReceiveStream::OnUnidirectionalStreamReady(
    nsIWebTransportSendStream* aStream) {
  return NS_ERROR_FAILURE;
}

JS_HAZ_CAN_RUN_SCRIPT NS_IMETHODIMP UniReceiveStream::OnError(uint8_t aError) {
  nsresult rv = aError == nsIWebTransport::INVALID_STATE_ERROR
                    ? NS_ERROR_DOM_INVALID_STATE_ERR
                    : NS_ERROR_FAILURE;
  LOG(("CreateStream OnError: %u", aError));
  mResolver(rv);
  return NS_OK;
}

JS_HAZ_CAN_RUN_SCRIPT NS_IMETHODIMP BidiReceiveStream::OnError(uint8_t aError) {
  nsresult rv = aError == nsIWebTransport::INVALID_STATE_ERROR
                    ? NS_ERROR_DOM_INVALID_STATE_ERR
                    : NS_ERROR_FAILURE;
  LOG(("CreateStream OnError: %u", aError));
  mResolver(rv);
  return NS_OK;
}

IPCResult WebTransportParent::RecvSetSendOrder(uint64_t aStreamId,
                                               int64_t aSendOrder) {
  LOG(("Set sendOrder=%" PRIi64 " for streamId %" PRIu64, aSendOrder,
       aStreamId));
  if (auto entry = mUniStreamCallbackMap.Lookup(aStreamId)) {
    entry->mStream->SetSendOrder(aSendOrder);
  } else if (auto entry = mBidiStreamCallbackMap.Lookup(aStreamId)) {
    entry->mStream->SetSendOrder(aSendOrder);
  }
  return IPC_OK();
}

IPCResult WebTransportParent::RecvSetSendGroup(uint64_t aStreamId,
                                               uint64_t aGroupId) {
  LOG(("Set sendGroup=%" PRIu64 " for streamId %" PRIu64, aGroupId, aStreamId));
  if (auto entry = mUniStreamCallbackMap.Lookup(aStreamId)) {
    entry->mStream->SetSendGroup(aGroupId);
  } else if (auto entry = mBidiStreamCallbackMap.Lookup(aStreamId)) {
    entry->mStream->SetSendGroup(aGroupId);
  }
  return IPC_OK();
}

IPCResult
WebTransportParent::RecvSetAnticipatedConcurrentIncomingUnidirectionalStreams(
    const Maybe<uint16_t>& aValue) {
  LOG(("Set anticipatedConcurrentIncomingUnidirectionalStreams=%s",
       aValue.isSome() ? nsPrintfCString("%u", aValue.value()).get() : "null"));
  if (mWebTransport) {
    mWebTransport->SetAnticipatedConcurrentIncomingUnidirectionalStreams(
        aValue.valueOr(0));
  }
  return IPC_OK();
}

IPCResult
WebTransportParent::RecvSetAnticipatedConcurrentIncomingBidirectionalStreams(
    const Maybe<uint16_t>& aValue) {
  LOG(("Set anticipatedConcurrentIncomingBidirectionalStreams=%s",
       aValue.isSome() ? nsPrintfCString("%u", aValue.value()).get() : "null"));
  if (mWebTransport) {
    mWebTransport->SetAnticipatedConcurrentIncomingBidirectionalStreams(
        aValue.valueOr(0));
  }
  return IPC_OK();
}

IPCResult WebTransportParent::RecvExportKeyingMaterial(
    nsTArray<uint8_t>&& aLabel, nsTArray<uint8_t>&& aContext,
    uint32_t aOutputLength, ExportKeyingMaterialResolver&& aResolver) {
  LOG(
      ("ExportKeyingMaterial for %p, label length=%zu, context length=%zu, "
       "outputLength=%u",
       this, aLabel.Length(), aContext.Length(), aOutputLength));

  // aOutputLength is content-process-controlled. The 1..4096 bound is also
  // enforced in the DOM binding (WebTransport::ExportKeyingMaterial), but
  // that check cannot be trusted here: an IPC argument must be re-validated
  // at the trust boundary before being used to size an allocation.
  if (aOutputLength == 0 ||
      aOutputLength > nsIWebTransport::MAX_KEYING_MATERIAL_LENGTH) {
    aResolver(Nothing());
    return IPC_OK();
  }

  if (!mWebTransport) {
    aResolver(Nothing());
    return IPC_OK();
  }

  nsTArray<uint8_t> keyingMaterial;
  nsresult rv = mWebTransport->ExportKeyingMaterial(
      aLabel, aContext, aOutputLength, keyingMaterial);

  if (NS_FAILED(rv)) {
    LOG(("ExportKeyingMaterial failed with rv=0x%08x",
         static_cast<uint32_t>(rv)));
    aResolver(Nothing());
    return IPC_OK();
  }

  LOG(("ExportKeyingMaterial succeeded, returning %zu bytes",
       keyingMaterial.Length()));
  aResolver(Some(std::move(keyingMaterial)));
  return IPC_OK();
}

IPCResult WebTransportParent::RecvCreateSendGroup(uint64_t aGroupId) {
  LOG(("%s for %p received, groupId=%" PRIu64, __func__, this, aGroupId));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  if (mWebTransport) {
    nsresult rv = mWebTransport->RegisterSendGroup(aGroupId);
    if (NS_FAILED(rv)) {
      LOG(("RegisterSendGroup failed: %x", static_cast<uint32_t>(rv)));
    }
  } else {
    LOG(("CreateSendGroup called with null mWebTransport"));
  }
  return IPC_OK();
}

IPCResult WebTransportParent::RecvGetStats(GetStatsResolver&& aResolver) {
  LOG(("GetStats for %p", this));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  if (!mWebTransport) {
    aResolver(Nothing());
    return IPC_OK();
  }

  bool gatherAlreadyInFlight = !mGetStatsResolvers.IsEmpty();
  mGetStatsResolvers.AppendElement(std::move(aResolver));
  if (gatherAlreadyInFlight) {
    // A gather is already pending; it will resolve this resolver too once it
    // completes (see OnStatsAvailable()).
    return IPC_OK();
  }

  // This should trigger a callback to OnStatsAvailable; if the request can't
  // even be dispatched, resolve now instead of leaving mGetStatsResolvers set
  // (and the child's promise(s) pending) forever.
  nsresult rv = mWebTransport->GetStats();
  if (NS_FAILED(rv)) {
    LOG(("GetStats: dispatch failed: %x", static_cast<uint32_t>(rv)));
    ResolvePendingGetStats(Nothing());
  }
  return IPC_OK();
}

IPCResult WebTransportParent::RecvCreateUnidirectionalStream(
    int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
    CreateUnidirectionalStreamResolver&& aResolver) {
  LOG(("%s for %p received, sendOrder=%" PRIi64 ", sendGroupId=%" PRIu64,
       __func__, this, aSendOrder,
       aSendGroupId.isSome() ? aSendGroupId.value() : 0));

  auto streamCb =
      [self = RefPtr{this}](
          uint64_t aStreamId,
          WebTransportParent::OnResetOrStopSendingCallback&& aCallback,
          nsIWebTransportSendStream* aStream) {
        self->mUniStreamCallbackMap.InsertOrUpdate(
            aStreamId, StreamHash<nsIWebTransportSendStream>{
                           std::move(aCallback), aStream});
      };
  RefPtr<UniReceiveStream> callback =
      new UniReceiveStream(std::move(aResolver), std::move(streamCb),
                           aSendOrder, aSendGroupId, mSocketThread);
  nsresult rv;
  rv = mWebTransport->CreateOutgoingUnidirectionalStream(callback);
  if (NS_FAILED(rv)) {
    callback->OnError(0);  // XXX
  }
  return IPC_OK();
}

IPCResult WebTransportParent::RecvCreateBidirectionalStream(
    int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
    CreateBidirectionalStreamResolver&& aResolver) {
  LOG(("%s for %p received, sendOrder=%" PRIi64 ", sendGroupId=%" PRIu64,
       __func__, this, aSendOrder,
       aSendGroupId.isSome() ? aSendGroupId.value() : 0));

  auto streamCb =
      [self = RefPtr{this}](
          uint64_t aStreamId,
          WebTransportParent::OnResetOrStopSendingCallback&& aCallback,
          nsIWebTransportBidirectionalStream* aStream) {
        self->mBidiStreamCallbackMap.InsertOrUpdate(
            aStreamId, StreamHash<nsIWebTransportBidirectionalStream>{
                           std::move(aCallback), aStream});
      };
  RefPtr<BidiReceiveStream> callback =
      new BidiReceiveStream(std::move(aResolver), std::move(streamCb),
                            aSendOrder, aSendGroupId, mSocketThread);
  nsresult rv;
  rv = mWebTransport->CreateOutgoingBidirectionalStream(callback);
  if (NS_FAILED(rv)) {
    callback->OnError(0);  // XXX
  }
  return IPC_OK();
}

// We recieve this notification from the WebTransportSessionProxy if session was
// successfully created at the end of
// WebTransportSessionProxy::OnStopRequest
NS_IMETHODIMP
WebTransportParent::OnSessionReady(uint64_t aSessionId) {
  MOZ_ASSERT(mOwningEventTarget);
  MOZ_ASSERT(!mOwningEventTarget->IsOnCurrentThread());

  LOG(("Created web transport session, sessionID = %" PRIu64 ", for %p",
       aSessionId, this));

  mSessionReady = true;
  mSessionId = aSessionId;
  mSessionReadyTime = TimeStamp::Now();

  // Retarget to socket thread. After this, WebTransportParent and
  // |mWebTransport| should be only accessed on the socket thread.
  nsresult rv = mWebTransport->RetargetTo(mSocketThread);
  if (NS_FAILED(rv)) {
    mOwningEventTarget->Dispatch(NS_NewRunnableFunction(
        "WebTransportParent::OnSessionReady Failed",
        [self = RefPtr{this}, result = rv] {
          MutexAutoLock lock(self->mMutex);
          if (!self->mClosed && self->mResolver) {
            CallResolver(self->mResolver, result,
                         static_cast<uint8_t>(
                             WebTransportReliabilityMode::Supports_unreliable),
                         0);
            self->mResolver = nullptr;
          }
        }));
    return NS_OK;
  }

  mSocketThread->Dispatch(NS_NewRunnableFunction(
      "WebTransportParent::QueryNegotiatedProtocol", [self = RefPtr{this}] {
        self->mRetargeted = true;
        if (!self->CanSend()) {
          self->CloseSessionForDestroyedActor();
          return;
        }

        nsAutoCString subprotocol;
        if (self->mWebTransport) {
          self->mWebTransport->GetNegotiatedProtocol(subprotocol);
          LOG(("Negotiated protocol: %s", subprotocol.get()));
          if (self->CanSend()) {
            nsTArray<nsCString> names;
            nsTArray<nsCString> values;
            self->mWebTransport->GetResponseHeaders(names, values);
            nsTArray<WebTransportHeader> headers(names.Length());
            for (size_t i = 0; i < names.Length() && i < values.Length(); ++i) {
              headers.AppendElement(WebTransportHeader(names[i], values[i]));
            }
            (void)self->SendNegotiatedProtocolAndHeaders(subprotocol, headers);
          }

          // Fetch max datagram size synchronously on socket thread
          // This will call OnMaxDatagramSize which stores it in
          // mMaxDatagramSizeValue
          self->mWebTransport->GetMaxDatagramSize();
        }

        // Resolve ready promise AFTER sending the negotiated protocol and
        // fetching max datagram size to ensure both are available when ready
        // resolves
        uint64_t maxDatagramSize = self->mMaxDatagramSizeValue;
        self->mOwningEventTarget->Dispatch(NS_NewRunnableFunction(
            "WebTransportParent::OnSessionReady", [self, maxDatagramSize]() {
              MutexAutoLock lock(self->mMutex);
              if (!self->mClosed && self->mResolver) {
                CallResolver(
                    self->mResolver, NS_OK,
                    static_cast<uint8_t>(
                        WebTransportReliabilityMode::Supports_unreliable),
                    maxDatagramSize);
                self->mResolver = nullptr;
                if (self->mExecuteAfterResolverCallback) {
                  self->mExecuteAfterResolverCallback();
                  self->mExecuteAfterResolverCallback = nullptr;
                }
              } else {
                if (self->mClosed) {
                  LOG(("Session already closed at OnSessionReady %p",
                       self.get()));
                } else {
                  LOG(("No resolver at OnSessionReady %p", self.get()));
                }
              }
            }));
      }));

  return NS_OK;
}

// We receive this notification from the WebTransportSessionProxy if session
// creation was unsuccessful at the end of
// WebTransportSessionProxy::OnStopRequest
// Pointer lifetime: aStats is owned by the caller
// (WebTransportSessionProxy::OnSessionClosed or
// WebTransportSessionProxy::CallOnSessionClosed) and remains valid for the
// duration of this synchronous call. We copy the data when capturing it in
// lambdas for dispatch to other threads.
NS_IMETHODIMP
WebTransportParent::OnSessionClosed(const bool aCleanly,
                                    const uint32_t aErrorCode,
                                    const nsACString& aReason,
                                    nsIWebTransportSessionStats* aStats) {
  nsresult rv = NS_OK;

  MOZ_ASSERT(mOwningEventTarget);
  MOZ_ASSERT(!mOwningEventTarget->IsOnCurrentThread());

  WebTransportStatsData* rawStats = nullptr;
  MOZ_ALWAYS_SUCCEEDS(aStats->GetRawStats(&rawStats));
  MOZ_ASSERT(rawStats);

  // currently we just know if session was closed gracefully or not.
  // we need better error propagation from lower-levels of http3
  // webtransport session and it's subsequent error mapping to DOM.
  // XXX See Bug 1806834
  if (!mSessionReady) {
    // this is an unclean close (we never got to ready)
    LOG(("webtransport %p session creation failed code= %u, reason= %s", this,
         aErrorCode, PromiseFlatCString(aReason).get()));
    // we know we haven't gone Ready yet
    rv = NS_ERROR_FAILURE;
    // A queued GetStats() would otherwise hang forever. We're on the main
    // thread here, not yet the socket thread, so dispatch.
    mSocketThread->Dispatch(NS_NewRunnableFunction(
        "WebTransportParent::OnSessionClosed",
        [self = RefPtr{this}] { self->ResolvePendingGetStats(Nothing()); }));
    mOwningEventTarget->Dispatch(NS_NewRunnableFunction(
        "WebTransportParent::OnSessionClosed",
        [self = RefPtr{this}, result = rv] {
          MutexAutoLock lock(self->mMutex);
          if (!self->mClosed && self->mResolver) {
            CallResolver(self->mResolver, result,
                         static_cast<uint8_t>(
                             WebTransportReliabilityMode::Supports_unreliable),
                         0);
            self->mResolver = nullptr;
          }
        }));
  } else {
    {
      MutexAutoLock lock(mMutex);
      if (mResolver && !mClosed) {
        LOG(("[%p] NotifyRemoteClosed to be called later", this));
        // NotifyRemoteClosed needs to wait until mResolver is invoked.
        mExecuteAfterResolverCallback = [self = RefPtr{this}, aCleanly,
                                         aErrorCode, statsData = *rawStats,
                                         reason = nsCString{aReason}]() {
          self->NotifyRemoteClosed(aCleanly, aErrorCode, reason, statsData);
        };
        return NS_OK;
      }
    }
    // https://w3c.github.io/webtransport/#web-transport-termination
    // Step 1: Let cleanly be a boolean representing whether the HTTP/3
    // stream associated with the CONNECT request that initiated
    // transport.[[Session]] is in the "Data Recvd" state. [QUIC]
    // XXX not calculated yet
    NotifyRemoteClosed(aCleanly, aErrorCode, aReason, *rawStats);
  }

  return NS_OK;
}

NS_IMETHODIMP WebTransportParent::OnDraining() {
  LOG(("WebTransportParent::OnDraining %p", this));

  // A server that drains immediately upon connection can deliver this before
  // WebTransportSessionProxy::RetargetTo(mSocketThread) (called from
  // OnSessionReady) has taken effect, in which case we're still on the main
  // thread here. SendDraining() requires the actor's worker thread
  // (mSocketThread, the thread the parent endpoint was Bind()'d on), so hop
  // there explicitly instead of assuming our caller already did.
  mSocketThread->Dispatch(NS_NewRunnableFunction(
      "WebTransportParent::OnDraining",
      [self = RefPtr{this}] { (void)self->SendDraining(); }));

  return NS_OK;
}

// The WebIDL streamErrorCode is an unsigned long; anything wider is a peer
// that sent no application error code.
static Maybe<uint32_t> ToApplicationErrorCode(uint64_t aErrorCode) {
  if (aErrorCode > std::numeric_limits<uint32_t>::max()) {
    return Nothing();
  }
  return Some(static_cast<uint32_t>(aErrorCode));
}

NS_IMETHODIMP WebTransportParent::OnStopSending(uint64_t aStreamId,
                                                uint64_t aErrorCode) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  LOG(("WebTransportParent::OnStopSending %p stream id=%" PRIx64
       " errorCode=%" PRIu64,
       this, aStreamId, aErrorCode));
  StopSendingError error;
  error.errorCode() = ToApplicationErrorCode(aErrorCode);
  if (CanSend()) {
    (void)SendOnStreamResetOrStopSending(aStreamId, error);
  }
  if (auto entry = mUniStreamCallbackMap.Lookup(aStreamId)) {
    entry->mCallback.OnStopSending(NS_ERROR_ABORT);
    mUniStreamCallbackMap.Remove(aStreamId);
  } else if (auto entry = mBidiStreamCallbackMap.Lookup(aStreamId)) {
    entry->mCallback.OnStopSending(NS_ERROR_ABORT);
    entry->mStopSendingReceived = true;
    if (entry->mResetReceived) {
      mBidiStreamCallbackMap.Remove(aStreamId);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP WebTransportParent::OnResetReceived(uint64_t aStreamId,
                                                  uint64_t aErrorCode) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  LOG(("WebTransportParent::OnResetReceived %p stream id=%" PRIx64
       " errorCode=%" PRIu64,
       this, aStreamId, aErrorCode));
  ResetError error;
  error.errorCode() = ToApplicationErrorCode(aErrorCode);
  if (CanSend()) {
    (void)SendOnStreamResetOrStopSending(aStreamId, error);
  }
  if (auto entry = mUniStreamCallbackMap.Lookup(aStreamId)) {
    entry->mCallback.OnResetOrStopSending(NS_ERROR_ABORT);
    mUniStreamCallbackMap.Remove(aStreamId);
  } else if (auto entry = mBidiStreamCallbackMap.Lookup(aStreamId)) {
    entry->mCallback.OnResetOrStopSending(NS_ERROR_ABORT);
    entry->mResetReceived = true;
    if (entry->mStopSendingReceived) {
      mBidiStreamCallbackMap.Remove(aStreamId);
    }
  } else if (auto entry = mIncomingUniStreamCallbackMap.Lookup(aStreamId)) {
    entry->mCallback.OnResetOrStopSending(NS_ERROR_ABORT);
    mIncomingUniStreamCallbackMap.Remove(aStreamId);
  }
  return NS_OK;
}

void WebTransportParent::NotifyRemoteClosed(
    bool aCleanly, uint32_t aErrorCode, const nsACString& aReason,
    const WebTransportStatsData& aStats) {
  LOG(("webtransport %p session remote closed cleanly=%d code= %u, reason= %s",
       this, aCleanly, aErrorCode, PromiseFlatCString(aReason).get()));

  // Always provide stats since neqo provides valid transport stats.
  Maybe<WebTransportStatsData> stats = Some(aStats);

  mSocketThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, aErrorCode, reason = nsCString{aReason},
                 aCleanly, stats = std::move(stats)]() {
        // The session is gone, so an in-flight gather will never call back;
        // settle it with the close-time stats.
        self->ResolvePendingGetStats(stats);
        // Tell the content side we were closed by the server
        (void)self->SendRemoteClosed(aCleanly, aErrorCode, reason, stats);
        // Let the other end shut down the IPC channel after RecvClose()
      }));
}

namespace {
// Each NS_AsyncCopy() of one peer-opened stream holds a reference; the stream
// is removed from the callback maps when the last of them completes (on the
// socket thread).
class IncomingStreamCopies final {
 public:
  NS_INLINE_DECL_REFCOUNTING(IncomingStreamCopies)

  IncomingStreamCopies(WebTransportParent* aParent, uint64_t aStreamId)
      : mParent(aParent), mStreamId(aStreamId) {}

  nsresult AsyncCopy(nsIInputStream* aSource, nsIOutputStream* aSink,
                     nsIEventTarget* aTarget, nsAsyncCopyMode aMode) {
    RefPtr<IncomingStreamCopies> self(this);
    nsresult rv =
        NS_AsyncCopy(aSource, aSink, aTarget, aMode,
                     mozilla::ipc::kDefaultDataPipeCapacity, CopyDone, this);
    if (NS_SUCCEEDED(rv)) {
      self.forget().leak();
    }
    return rv;
  }

 private:
  ~IncomingStreamCopies() { mParent->RemoveFinishedIncomingStream(mStreamId); }

  static void CopyDone(void* aClosure, nsresult) {
    RefPtr<IncomingStreamCopies> copies =
        dont_AddRef(static_cast<IncomingStreamCopies*>(aClosure));
  }

  const RefPtr<WebTransportParent> mParent;
  const uint64_t mStreamId;
};
}  // namespace

void WebTransportParent::RemoveFinishedIncomingStream(uint64_t aStreamId) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  mIncomingUniStreamCallbackMap.Remove(aStreamId);
  mBidiStreamCallbackMap.Remove(aStreamId);
}

NS_IMETHODIMP
WebTransportParent::OnIncomingUnidirectionalStreamAvailable(
    nsIWebTransportReceiveStream* aStream) {
  // Note: we need to hold a reference to the stream if we want to get stats,
  // etc
  LOG(("%p IncomingUnidirectonalStream available", this));

  // We must be on the Socket Thread
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  RefPtr<DataPipeSender> sender;
  RefPtr<DataPipeReceiver> receiver;
  nsresult rv = NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                            getter_AddRefs(sender), getter_AddRefs(receiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  uint64_t id;
  (void)aStream->GetStreamId(&id);

  nsCOMPtr<nsIAsyncInputStream> inputStream;
  aStream->GetInputStream(getter_AddRefs(inputStream));
  MOZ_ASSERT(inputStream);
  auto copies = MakeRefPtr<IncomingStreamCopies>(this, id);
  rv = copies->AsyncCopy(inputStream, sender, mSocketThread,
                         NS_ASYNCCOPY_VIA_WRITESEGMENTS);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  LOG(("%p Sending UnidirectionalStream pipe to content", this));
  // pass the DataPipeReceiver to the content process

  // Register the stream so RecvStopSendingStream() can find it when content
  // calls cancel() on the corresponding WebTransportReceiveStream, and so a
  // RESET_STREAM from the peer closes our local pipe.
  // Capture our end of the pipe: serializing receiver to content below closes
  // the parent's copy of it.
  auto onResetOrStopSending = [sender = RefPtr{sender}](nsresult aError) {
    sender->CloseWithStatus(aError);
  };
  mIncomingUniStreamCallbackMap.InsertOrUpdate(
      id, StreamHash<nsIWebTransportReceiveStream>{
              OnResetOrStopSendingCallback(std::move(onResetOrStopSending)),
              aStream});

  (void)SendIncomingUnidirectionalStream(id, receiver);

  return NS_OK;
}

NS_IMETHODIMP
WebTransportParent::OnIncomingBidirectionalStreamAvailable(
    nsIWebTransportBidirectionalStream* aStream) {
  // Note: we need to hold a reference to the stream if we want to get stats,
  // etc
  LOG(("%p IncomingBidirectonalStream available", this));

  // We must be on the Socket Thread
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  RefPtr<DataPipeSender> inputSender;
  RefPtr<DataPipeReceiver> inputReceiver;
  nsresult rv =
      NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                  getter_AddRefs(inputSender), getter_AddRefs(inputReceiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr<DataPipeSender> outputSender;
  RefPtr<DataPipeReceiver> outputReceiver;
  rv =
      NewDataPipe(mozilla::ipc::kDefaultDataPipeCapacity,
                  getter_AddRefs(outputSender), getter_AddRefs(outputReceiver));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  uint64_t id;
  (void)aStream->GetStreamId(&id);

  nsCOMPtr<nsIAsyncInputStream> inputStream;
  aStream->GetInputStream(getter_AddRefs(inputStream));
  MOZ_ASSERT(inputStream);
  nsCOMPtr<nsIAsyncOutputStream> outputStream;
  aStream->GetOutputStream(getter_AddRefs(outputStream));
  MOZ_ASSERT(outputStream);

  auto copies = MakeRefPtr<IncomingStreamCopies>(this, id);
  rv = copies->AsyncCopy(inputStream, inputSender, mSocketThread,
                         NS_ASYNCCOPY_VIA_WRITESEGMENTS);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = copies->AsyncCopy(outputReceiver, outputStream, mSocketThread,
                         NS_ASYNCCOPY_VIA_READSEGMENTS);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  LOG(("%p Sending BidirectionalStream pipe to content", this));
  // pass the DataPipeSender to the content process
  (void)SendIncomingBidirectionalStream(id, inputReceiver, outputSender);
  return NS_OK;
}

::mozilla::ipc::IPCResult WebTransportParent::RecvGetMaxDatagramSize(
    GetMaxDatagramSizeResolver&& aResolver) {
  LOG(("WebTransportParent RecvGetMaxDatagramSize"));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  MOZ_ASSERT(mWebTransport);
  MOZ_ASSERT(!mMaxDatagramSizeResolver);

  mMaxDatagramSizeResolver = std::move(aResolver);
  // maximum datagram size for the session is returned from network stack
  // synchronously via WebTransportSessionEventListener::OnMaxDatagramSize
  // interface
  mWebTransport->GetMaxDatagramSize();
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportParent::RecvGetHttpChannelID(
    GetHttpChannelIDResolver&& aResolver) {
  LOG(("WebTransportParent Channel ID"));
  MOZ_ASSERT(mWebTransport);
  uint64_t aHttpChannelId;
  mWebTransport->GetHttpChannelID(&aHttpChannelId);
  aResolver(aHttpChannelId);
  return IPC_OK();
}

// The promise is now resolved via DatagramReady event from neqo
::mozilla::ipc::IPCResult WebTransportParent::RecvOutgoingDatagram(
    const uint64_t& aDatagramId, nsTArray<uint8_t>&& aData,
    const TimeStamp& aTimestamp, const uint64_t& aSendGroupId,
    const int64_t& aSendOrder, OutgoingDatagramResolver&& aResolver) {
  LOG(("WebTransportParent sending datagram id=%" PRIu64 ", length=%zu",
       aDatagramId, aData.Length()));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  MOZ_ASSERT(mWebTransport);

  // Resolve IPC immediately - actual outcome notification happens via
  // DatagramReady
  aResolver(NS_OK);

  double ageMs = (TimeStamp::Now() - aTimestamp).ToMilliseconds();
  LOG(("Forwarding datagram id=%" PRIu64
       " to neqo queue (timestamp=%f ms), sendGroup=%" PRIu64
       ", sendOrder=%" PRId64,
       aDatagramId, ageMs, aSendGroupId, aSendOrder));

  // neqo measures outgoingMaxAge from aTimestamp too, but a datagram already
  // past it need not be queued at all.
  if (ageMs > mDatagramMaxAge) {
    LOG(("Dropping datagram id=%" PRIu64 ": age %f ms exceeds outgoingMaxAge "
         "%f ms before reaching neqo",
         aDatagramId, ageMs, mDatagramMaxAge));
    if (CanSend()) {
      (void)SendDatagramReady(aDatagramId);
    }
    return IPC_OK();
  }

  // A datagram written before the session was ready could not have been sent
  // any sooner, so unless the page set an explicit outgoingMaxAge, don't
  // count the connection setup against neqo's implementation-defined default
  // max age (a small multiple of the RTT).  Datagrams written right after
  // constructing the WebTransport would otherwise expire whenever the
  // handshake took longer than that.
  TimeStamp timestamp = aTimestamp;
  if (!std::isfinite(mDatagramMaxAge) && timestamp < mSessionReadyTime) {
    timestamp = mSessionReadyTime;
  }

  // neqo's queue requires a session's timestamps to be non-decreasing.
  // Writes from several writables, flushed one writable at a time at
  // connect, need not be, so never go backwards.
  if (mLastOutgoingDatagramTimestamp.IsNull() ||
      timestamp > mLastOutgoingDatagramTimestamp) {
    mLastOutgoingDatagramTimestamp = timestamp;
  }

  // Send to neqo's datagram queue. The datagram will be sent when the queue
  // is processed, and OnOutgoingDatagramOutCome will be called with the result.
  (void)mWebTransport->SendDatagramAt(aData, aDatagramId, aSendGroupId,
                                      aSendOrder,
                                      mLastOutgoingDatagramTimestamp);

  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportParent::RecvUpdateDatagramQueueParams(
    const uint32_t& aOutgoingMaxBufferedDatagrams,
    const double& aOutgoingMaxAge) {
  LOG(
      ("WebTransportParent::RecvUpdateDatagramQueueParams: "
       "outgoingMaxBufferedDatagrams=%" PRIu32 ", maxAge=%f",
       aOutgoingMaxBufferedDatagrams, aOutgoingMaxAge));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  MOZ_ASSERT(mWebTransport);
  mWebTransport->SetDatagramMaxBufferedDatagrams(aOutgoingMaxBufferedDatagrams);
  mWebTransport->SetDatagramMaxAge(aOutgoingMaxAge);
  mDatagramMaxAge = aOutgoingMaxAge;

  return IPC_OK();
}

NS_IMETHODIMP WebTransportParent::OnDatagramReceived(
    const nsTArray<uint8_t>& aData) {
  // We must be on the Socket Thread
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  LOG(("WebTransportParent received datagram length = %zu", aData.Length()));

  TimeStamp ts = TimeStamp::Now();
  (void)SendIncomingDatagram(aData, ts);

  return NS_OK;
}

NS_IMETHODIMP
WebTransportParent::OnOutgoingDatagramOutCome(
    uint64_t aId, WebTransportSessionEventListener::DatagramOutcome aOutCome) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());

  if (aOutCome == WebTransportSessionEventListener::DatagramOutcome::SENT) {
    LOG(("Sent datagram id= %" PRIu64, aId));
  } else {
    LOG(("Dropped datagram id= %" PRIu64, aId));
  }

  // With the datagram queue, outcomes are reported asynchronously.
  // Send DatagramReady IPC message to notify the content process.
  if (CanSend()) {
    (void)SendDatagramReady(aId);
  }

  return NS_OK;
}

NS_IMETHODIMP WebTransportParent::OnMaxDatagramSize(uint64_t aSize) {
  LOG(("Max datagram size is %" PRIu64, aSize));
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  if (mMaxDatagramSizeResolver) {
    // Called from RecvGetMaxDatagramSize IPC path
    mMaxDatagramSizeResolver(aSize);
    mMaxDatagramSizeResolver = nullptr;
  } else {
    // Called from OnSessionReady path - store for resolver
    mMaxDatagramSizeValue = aSize;
  }
  return NS_OK;
}

// Pointer lifetime: aStats is owned by the caller
// (WebTransportSessionProxy::OnStatsAvailable) and remains valid for the
// duration of this synchronous call, or null if stats could not be gathered.
// We copy the data into the resolver before returning.
NS_IMETHODIMP WebTransportParent::OnStatsAvailable(
    nsIWebTransportSessionStats* aStats) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  WebTransportStatsData* rawStats = nullptr;
  if (aStats) {
    MOZ_ALWAYS_SUCCEEDS(aStats->GetRawStats(&rawStats));
  }
  if (rawStats) {
    LOG(
        ("Stats available: bytesSent=%llu, bytesReceived=%llu, minRtt=%f, "
         "smoothedRtt=%f",
         (unsigned long long)rawStats->bytesSent(),
         (unsigned long long)rawStats->bytesReceived(), rawStats->minRtt(),
         rawStats->smoothedRtt()));
  } else {
    LOG(("Stats unavailable"));
  }

  // The gather can outlive the requests it was started for if the session went
  // away first (see ResolvePendingGetStats() callers); there is nothing left to
  // report to.
  if (mGetStatsResolvers.IsEmpty()) {
    return NS_OK;
  }

  ResolvePendingGetStats(rawStats ? Some(*rawStats) : Nothing());
  return NS_OK;
}

void WebTransportParent::ResolvePendingGetStats(
    const Maybe<WebTransportStatsData>& aStats) {
  MOZ_ASSERT(mSocketThread->IsOnCurrentThread());
  nsTArray<GetStatsResolver> resolvers = std::move(mGetStatsResolvers);
  for (auto& resolver : resolvers) {
    resolver(aStats);
  }
}
}  // namespace mozilla::dom
