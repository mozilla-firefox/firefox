/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransport.h"

#include <memory>

#include "WebTransportBidirectionalStream.h"
#include "mozilla/Assertions.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/HeadersBinding.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/PWebTransport.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/ReadableStreamBinding.h"
#include "mozilla/dom/ReadableStreamDefaultController.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/WebTransportDatagramDuplexStream.h"
#include "mozilla/dom/WebTransportError.h"
#include "mozilla/dom/WebTransportLog.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WritableStream.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsIURL.h"
#include "nsIWebTransport.h"
#include "nsIWebTransportStream.h"
#include "nsPIDOMWindowInlines.h"
#include "nsTHashSet.h"
#include "nsThreadUtils.h"
#include "nsUTF8Utils.h"

using namespace mozilla::ipc;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WebTransport)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(WebTransport)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIncomingUnidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIncomingBidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIncomingUnidirectionalAlgorithm)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIncomingBidirectionalAlgorithm)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDatagrams)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReady)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDraining)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mClosed)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResponseHeaders)
  for (const auto& hashEntry : tmp->mSendStreams.Values()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mSendStreams entry item");
    cb.NoteXPCOMChild(hashEntry);
  }
  for (const auto& hashEntry : tmp->mReceiveStreams.Values()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mReceiveStreams entry item");
    cb.NoteXPCOMChild(hashEntry);
  }
  for (const auto& promise : tmp->mPendingGetStatsPromises) {
    CycleCollectionNoteChild(cb, promise.get(),
                             "mPendingGetStatsPromises promise");
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(WebTransport)
  tmp->mSendStreams.Clear();
  tmp->mReceiveStreams.Clear();
  tmp->mPendingGetStatsPromises.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mUnidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIncomingUnidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIncomingBidirectionalStreams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIncomingUnidirectionalAlgorithm)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIncomingBidirectionalAlgorithm)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDatagrams)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReady)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDraining)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mClosed)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mResponseHeaders)
  if (tmp->mChild) {
    tmp->mChild->Shutdown(false);
    tmp->mChild = nullptr;
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebTransport)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebTransport)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebTransport)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

WebTransport::WebTransport(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal),
      mState(WebTransportState::CONNECTING),
      mReliability(WebTransportReliabilityMode::Pending) {
  LOG(("Creating WebTransport %p", this));
}

WebTransport::~WebTransport() {
  // Should be empty by this point, because we should always have run cleanup:
  // https://w3c.github.io/webtransport/#webtransport-procedures
  LOG(("~WebTransport() for %p", this));
  MOZ_ASSERT(mSendStreams.IsEmpty());
  MOZ_ASSERT(mReceiveStreams.IsEmpty());
  // If this WebTransport was destroyed without being closed properly, make
  // sure to clean up the channel.
  // Since child has a raw ptr to us, we MUST call Shutdown() before we're
  // destroyed
  if (mChild) {
    mChild->Shutdown(true);
  }
}

// From parent
void WebTransport::NewBidirectionalStream(
    uint64_t aStreamId, const RefPtr<DataPipeReceiver>& aIncoming,
    const RefPtr<DataPipeSender>& aOutgoing) {
  LOG_VERBOSE(("NewBidirectionalStream()"));
  // Create a Bidirectional stream and push it into the
  // IncomingBidirectionalStreams stream. Must be added to the ReceiveStreams
  // and SendStreams arrays

  auto streams = std::make_unique<BidirectionalPair>(aIncoming, aOutgoing);
  auto tuple = std::tuple<uint64_t, UniquePtr<BidirectionalPair>>(
      aStreamId, std::move(streams));
  mBidirectionalStreams.AppendElement(std::move(tuple));
  // We need to delete them all!

  // Notify something to wake up readers of IncomingReceiveStreams
  // The callback is always set/used from the same thread (MainThread or a
  // Worker thread).
  if (mIncomingBidirectionalAlgorithm) {
    RefPtr<WebTransportIncomingStreamsAlgorithms> callback =
        mIncomingBidirectionalAlgorithm;
    LOG(("NotifyIncomingStream"));
    callback->NotifyIncomingStream();
  }
}

void WebTransport::NewUnidirectionalStream(
    uint64_t aStreamId, const RefPtr<mozilla::ipc::DataPipeReceiver>& aStream) {
  LOG_VERBOSE(("NewUnidirectionalStream()"));
  // Create a Unidirectional stream and push it into the
  // IncomingUnidirectionalStreams stream. Must be added to the ReceiveStreams
  // array

  mUnidirectionalStreams.AppendElement(
      std::tuple<uint64_t, RefPtr<mozilla::ipc::DataPipeReceiver>>(aStreamId,
                                                                   aStream));
  // Notify something to wake up readers of IncomingReceiveStreams
  // The callback is always set/used from the same thread (MainThread or a
  // Worker thread).
  if (mIncomingUnidirectionalAlgorithm) {
    RefPtr<WebTransportIncomingStreamsAlgorithms> callback =
        mIncomingUnidirectionalAlgorithm;
    LOG(("NotifyIncomingStream"));
    callback->NotifyIncomingStream();
  }
}

void WebTransport::NewDatagramReceived(nsTArray<uint8_t>&& aData,
                                       const mozilla::TimeStamp& aTimeStamp) {
  mDatagrams->NewDatagramReceived(std::move(aData), aTimeStamp);
}

void WebTransport::OnDatagramReady(uint64_t aDatagramId) {
  mDatagrams->OnDatagramReady(aDatagramId);
}

// WebIDL Boilerplate

nsIGlobalObject* WebTransport::GetParentObject() const { return mGlobal; }

JSObject* WebTransport::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return WebTransport_Binding::Wrap(aCx, this, aGivenProto);
}

// WebIDL Interface

/* static */
already_AddRefed<WebTransport> WebTransport::Constructor(
    const GlobalObject& aGlobal, const nsAString& aURL,
    const WebTransportOptions& aOptions, ErrorResult& aError) {
  LOG(("Creating WebTransport for %s", NS_ConvertUTF16toUTF8(aURL).get()));
  // https://w3c.github.io/webtransport/#webtransport-constructor

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<WebTransport> result = new WebTransport(global);
  result->Init(aGlobal, aURL, aOptions, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Don't let this document go into BFCache
  result->NotifyToWindow(true);

  // Step 35: Return transport.
  return result.forget();
}

void WebTransport::Init(const GlobalObject& aGlobal, const nsAString& aURL,
                        const WebTransportOptions& aOptions,
                        ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#webtransport-constructor
  // Initiate connection with parent
  using mozilla::ipc::BackgroundChild;
  using mozilla::ipc::Endpoint;
  using mozilla::ipc::PBackgroundChild;

  // https://w3c.github.io/webtransport/#webtransport-constructor
  // Steps 1-5: Let baseURL be this's relevant settings object's API base URL.
  // Let url be the URL record resulting from parsing url with baseURL.
  // If url is failure, throw a SyntaxError exception.
  // If url's scheme is not https, throw a SyntaxError exception.
  // If url's fragment is not null, throw a SyntaxError exception.
  if (!ParseURL(aURL)) {
    aError.ThrowSyntaxError("Invalid WebTransport URL");
    return;
  }
  // Step 6: Let newConnection be "no" if options's allowPooling is true;
  // otherwise "yes-and-dedicated". Represented here as a boolean.
  bool dedicated = !aOptions.mAllowPooling;
  // Step 7: Let serverCertificateHashes be options's serverCertificateHashes.
  // Step 8: If newConnection is "no" and serverCertificateHashes is not empty,
  // then throw a NotSupportedError exception.
  nsTArray<mozilla::ipc::WebTransportHash> aServerCertHashes;
  if (aOptions.mServerCertificateHashes.WasPassed()) {
    if (!dedicated && !aOptions.mServerCertificateHashes.Value().IsEmpty()) {
      aError.ThrowNotSupportedError(
          "serverCertificateHashes not supported for non-dedicated "
          "connections");
      return;
    }
    for (const auto& hash : aOptions.mServerCertificateHashes.Value()) {
      if (hash.mAlgorithm != u"sha-256") {
        LOG(("Algorithms other than SHA-256 are not supported"));
        continue;
      }

      nsTArray<uint8_t> data;
      if (!AppendTypedArrayDataTo(hash.mValue, data)) {
        aError.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }

      nsCString alg = NS_ConvertUTF16toUTF8(hash.mAlgorithm);
      aServerCertHashes.EmplaceBack(alg, data);
    }
  }
  // Step 9: Let requireUnreliable be options's requireUnreliable.
  bool requireUnreliable = aOptions.mRequireUnreliable;
  // Step 10: Let congestionControl be options's congestionControl.
  // Step 11: If congestionControl is not "default", and the user agent
  // does not support any congestion control algorithms that optimize for
  // congestionControl, as allowed by [RFC9002] section 7, then set
  // congestionControl to "default".
  WebTransportCongestionControl congestionControl =
      WebTransportCongestionControl::Default;  // aOptions.mCongestionControl;
  // Set this to 'default' until we add congestion control setting

  // Step 12: Let protocols be options's protocols.
  // Step 13: If any of the values in protocols occur more than once, fail to
  // match the requirements for the negotiated application protocol, or have an
  // isomorphic encoded length of 0 or exceeding 512, throw a SyntaxError.
  nsTArray<nsString> protocols;
  if (aOptions.mProtocols.Length() != 0) {
    const auto& protocolSeq = aOptions.mProtocols;
    protocols.SetCapacity(protocolSeq.Length());

    // Validate protocols per spec
    nsTHashSet<nsString> seen;
    for (const auto& protocol : protocolSeq) {
      // Check for empty string
      if (protocol.IsEmpty()) {
        aError.ThrowSyntaxError("Protocol cannot be empty string");
        return;
      }

      // Protocol strings are serialized as SF strings (RFC 8941), which only
      // allow printable ASCII (0x20-0x7E). Reject anything outside that range.
      for (size_t i = 0; i < protocol.Length(); i++) {
        char16_t c = protocol[i];
        if (c < 0x20 || c > 0x7E) {
          aError.ThrowSyntaxError(
              "Protocol contains characters not allowed in HTTP header fields");
          return;
        }
      }

      // Check for maximum length (512 bytes)
      if (protocol.Length() > 512) {
        aError.ThrowSyntaxError("Protocol name too long (max 512 bytes)");
        return;
      }

      // Check for duplicates
      if (seen.Contains(protocol)) {
        aError.ThrowSyntaxError("Duplicate protocol in list");
        return;
      }
      seen.Insert(protocol);

      protocols.AppendElement(protocol);
    }
  }

  // Step 30: Let headers be a new Headers object filled with
  // options["headers"].
  //
  // Step 31: Let requestHeaders be a new Headers object whose header list is
  // request's header list and guard is "request".
  //
  // Step 32: For each header of headers's header list:
  //   1. If ascii lowercase header's name is "wt-available-protocols", then
  //      throw a TypeError.
  //   2. append header to requestHeaders.
  // NOTE: Headers are subject to Fetch's forbidden request-header
  // restrictions.
  // NOTE: This runs right after the protocols checks (Step 13) rather than at
  // Step 30, so the throws happen before any IPC state is set up.  See
  // https://github.com/w3c/webtransport/issues/805
  nsTArray<mozilla::ipc::WebTransportHeader> requestHeaderList;
  if (aOptions.mHeaders.WasPassed()) {
    const auto& init = aOptions.mHeaders.Value();
    RefPtr<InternalHeaders> headers = new InternalHeaders();
    if (init.IsByteStringSequenceSequence()) {
      headers->Fill(init.GetAsByteStringSequenceSequence(), aError);
    } else if (init.IsByteStringByteStringRecord()) {
      headers->Fill(init.GetAsByteStringByteStringRecord(), aError);
    }
    if (aError.Failed()) {
      return;
    }
    nsTArray<InternalHeaders::Entry> entries;
    headers->GetEntries(entries);
    RefPtr<InternalHeaders> requestHeaders =
        new InternalHeaders(HeadersGuardEnum::Request);
    for (const auto& entry : entries) {
      if (entry.mName.LowerCaseEqualsLiteral("wt-available-protocols")) {
        aError.ThrowTypeError(
            "wt-available-protocols is not allowed in headers");
        return;
      }
      requestHeaders->Append(entry.mName, entry.mValue, aError);
      if (aError.Failed()) {
        return;
      }
    }
    nsTArray<InternalHeaders::Entry> requestEntries;
    requestHeaders->GetEntries(requestEntries);
    for (const auto& entry : requestEntries) {
      requestHeaderList.AppendElement(
          mozilla::ipc::WebTransportHeader(entry.mName, entry.mValue));
    }
  }

  // Step 17: Let incomingDatagrams be a new ReadableStream.
  // Steps 18-19: Let transport be a newly constructed WebTransport object and
  // set transport.[[Datagrams]] to the result of creating a
  // WebTransportDatagramDuplexStream with transport and incomingDatagrams.
  // datagramsReadableType has no default, so a default (non-byte) readable
  // stream is used unless the app opted into byte-stream semantics.
  bool useByteStream =
      aOptions.mDatagramsReadableType.WasPassed() &&
      aOptions.mDatagramsReadableType.Value() == ReadableStreamType::Bytes;
  mDatagrams = new WebTransportDatagramDuplexStream(mGlobal, this);
  mDatagrams->Init(useByteStream, aError);
  if (aError.Failed()) {
    return;
  }

  // TODO: Fix the shared and service worker use cases
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  mService = workerPrivate && (workerPrivate->IsSharedWorker() ||
                               workerPrivate->IsServiceWorker())
                 ? nullptr
                 : net::WebTransportEventService::GetOrCreate();
  // Step 18 (continued): [[Ready]]: new promise
  mReady = Promise::CreateInfallible(mGlobal);

  // Step 18 (continued): [[Draining]]: new promise
  mDraining = Promise::CreateInfallible(mGlobal);

  // Step 18 (continued): [[Closed]]: new promise
  mClosed = Promise::CreateInfallible(mGlobal);

  PBackgroundChild* backgroundChild =
      BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundChild)) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (mGlobal->GetClientInfo().isNothing()) {
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  IPCClientInfo ipcClientInfo = mGlobal->GetClientInfo().ref().ToIPC();

  nsCOMPtr<nsIPrincipal> principal = mGlobal->PrincipalOrNull();

  nsPIDOMWindowInner* window = mGlobal->GetAsInnerWindow();
  if (window) {
    mBrowsingContextID = window->GetBrowsingContext()->Id();
  }
  // Create a new IPC connection
  Endpoint<PWebTransportParent> parentEndpoint;
  Endpoint<PWebTransportChild> childEndpoint;
  MOZ_ALWAYS_SUCCEEDS(
      PWebTransport::CreateEndpoints(&parentEndpoint, &childEndpoint));

  RefPtr<WebTransportChild> child = new WebTransportChild(this);
  if (NS_IsMainThread()) {
    if (!childEndpoint.Bind(child)) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }
  } else if (!childEndpoint.Bind(child, mGlobal->SerialEventTarget())) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  mState = WebTransportState::CONNECTING;

  JSContext* cx = aGlobal.Context();
  // Set up Datagram streams
  // Step 20: Let pullDatagramsAlgorithm be an action that runs pullDatagrams
  // with transport.
  // Step 21: Set up incomingDatagrams with pullAlgorithm set to
  // pullDatagramsAlgorithm, and highWaterMark set to 0.

  // XXX TODO

  // Step 22: Let pullBidirectionalStreamAlgorithm be an action that runs
  // pullBidirectionalStream with transport.
  // Step 23: Set up transport.[[IncomingBidirectionalStreams]] with
  // pullAlgorithm set to pullBidirectionalStreamAlgorithm, and highWaterMark
  // set to 0.
  Optional<JS::Handle<JSObject*>> underlying;
  // Suppress warnings about risk of mGlobal getting nulled during script.
  // We set the global from the aGlobalObject parameter of the constructor, so
  // it must still be set here.
  const nsCOMPtr<nsIGlobalObject> global(mGlobal);

  mIncomingBidirectionalAlgorithm = new WebTransportIncomingStreamsAlgorithms(
      WebTransportIncomingStreamsAlgorithms::StreamType::Bidirectional, this);

  RefPtr<WebTransportIncomingStreamsAlgorithms> algorithm =
      mIncomingBidirectionalAlgorithm;
  mIncomingBidirectionalStreams = ReadableStream::CreateNative(
      cx, global, *algorithm, Some(0.0), nullptr, aError);
  if (aError.Failed()) {
    return;
  }
  // Step 24: Let pullUnidirectionalStreamAlgorithm be an action that runs
  // pullUnidirectionalStream with transport.
  // Step 25: Set up transport.[[IncomingUnidirectionalStreams]] with
  // pullAlgorithm set to pullUnidirectionalStreamAlgorithm, and highWaterMark
  // set to 0.

  mIncomingUnidirectionalAlgorithm = new WebTransportIncomingStreamsAlgorithms(
      WebTransportIncomingStreamsAlgorithms::StreamType::Unidirectional, this);

  algorithm = mIncomingUnidirectionalAlgorithm;
  mIncomingUnidirectionalStreams = ReadableStream::CreateNative(
      cx, global, *algorithm, Some(0.0), nullptr, aError);
  if (aError.Failed()) {
    return;
  }

  // Steps 26-34: Construct the HTTP CONNECT request with url,
  // serverCertificateHashes, protocols, and headers, then fetch it with
  // processResponse set to process a WebTransport fetch response.
  LOG(("Connecting WebTransport to parent for %s",
       NS_ConvertUTF16toUTF8(aURL).get()));

  // https://w3c.github.io/webtransport/#webtransport-constructor Spec 5.2
  mChild = child;

  // Step 14: Let anticipatedConcurrentIncomingUnidirectionalStreams be
  // options's anticipatedConcurrentIncomingUnidirectionalStreams.
  // Step 15: Let anticipatedConcurrentIncomingBidirectionalStreams be
  // options's anticipatedConcurrentIncomingBidirectionalStreams.
  mAnticipatedConcurrentIncomingUnidirectionalStreams =
      aOptions.mAnticipatedConcurrentIncomingUnidirectionalStreams;
  mAnticipatedConcurrentIncomingBidirectionalStreams =
      aOptions.mAnticipatedConcurrentIncomingBidirectionalStreams;

  Maybe<uint16_t> anticipatedUni;
  if (!mAnticipatedConcurrentIncomingUnidirectionalStreams.IsNull()) {
    anticipatedUni =
        Some(mAnticipatedConcurrentIncomingUnidirectionalStreams.Value());
  }
  Maybe<uint16_t> anticipatedBidi;
  if (!mAnticipatedConcurrentIncomingBidirectionalStreams.IsNull()) {
    anticipatedBidi =
        Some(mAnticipatedConcurrentIncomingBidirectionalStreams.Value());
  }

  backgroundChild
      ->SendCreateWebTransportParent(
          aURL, principal, mBrowsingContextID, ipcClientInfo, dedicated,
          requireUnreliable, (uint32_t)congestionControl, protocols,
          requestHeaderList, std::move(aServerCertHashes), anticipatedUni,
          anticipatedBidi, std::move(parentEndpoint))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}](
                 PBackgroundChild::CreateWebTransportParentPromise::
                     ResolveOrRejectValue&& aResult) {
               // aResult is a std::tuple<nsresult, uint8_t, uint64_t>
               // TODO: is there a better/more-spec-compliant error in the
               // reject case? Which begs the question, why would we get a
               // reject?
               nsresult rv = aResult.IsReject()
                                 ? NS_ERROR_FAILURE
                                 : std::get<0>(aResult.ResolveValue());
               LOG(("isreject: %d nsresult 0x%x", aResult.IsReject(),
                    (uint32_t)rv));
               if (NS_FAILED(rv)) {
                 self->RejectWaitingConnection(rv);
               } else {
                 // Set max datagram size from the CreateWebTransportParent
                 // response
                 uint64_t maxDatagramSize = std::get<2>(aResult.ResolveValue());
                 MOZ_ASSERT(self->mDatagrams);
                 self->mDatagrams->SetMaxDatagramSize(maxDatagramSize);
                 LOG(("max datagram size for the session is %" PRIu64,
                      maxDatagramSize));

                 // This will process anything waiting for the connection to
                 // complete
                 self->ResolveWaitingConnection(
                     static_cast<WebTransportReliabilityMode>(
                         std::get<1>(aResult.ResolveValue())));
               }
             });
}

void WebTransport::ResolveWaitingConnection(
    WebTransportReliabilityMode aReliability) {
  LOG(("Resolved Connection %p, reliability = %u", this,
       (unsigned)aReliability));
  // https://w3c.github.io/webtransport/#webtransport-constructor
  // Step 17 of  initialize WebTransport over HTTP
  // Step 17.1 If transport.[[State]] is not "connecting":
  if (mState != WebTransportState::CONNECTING) {
    // Step 17.1.1: In parallel, terminate session.
    // Step 17.1.2: abort these steps
    // Cleanup should have been called, which means Ready has been rejected
    return;
  }

  // Step 17.2: Set transport.[[State]] to "connected".
  mState = WebTransportState::CONNECTED;
  // Step 17.3: Set transport.[[Session]] to session.
  // Step 17.4: Set transport's [[Reliability]] to "supports-unreliable".
  mReliability = aReliability;
  if (NS_IsMainThread()) {
    nsPIDOMWindowInner* innerWindow = GetParentObject()->GetAsInnerWindow();
    if (innerWindow) {
      mInnerWindowID = innerWindow->WindowID();
    }
  } else {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    if (workerPrivate->IsDedicatedWorker()) {
      mInnerWindowID = workerPrivate->WindowID();
    }
  }

  // Step 17.5: Resolve transport.[[Ready]] with undefined.
  // The max datagram size has already been set from the
  // CreateWebTransportParent response, so datagrams are ready to be sent with
  // the correct size limit.
  mReady->MaybeResolveWithUndefined();

  // Release queued datagrams with the correct max datagram size.
  mDatagrams->SetChild(mChild);

  if (mInnerWindowID != 0) {
    // Get the http chanel created for the web transport session;
    mChild->SendGetHttpChannelID()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = RefPtr{this}](uint64_t&& aHttpChannelId) {
          MOZ_ASSERT(self->mService);
          self->mHttpChannelID = aHttpChannelId;
          self->mService->WebTransportSessionCreated(self->mInnerWindowID,
                                                     aHttpChannelId);
        },
        [](const mozilla::ipc::ResponseRejectReason& aReason) {
          LOG(("WebTransport fetching the channel information failed "));
        });
  }
}

void WebTransport::RejectWaitingConnection(nsresult aRv) {
  LOG(("Rejected connection %p %x", this, (uint32_t)aRv));
  // https://w3c.github.io/webtransport/#initialize-webtransport-over-http

  // Step 10: If connection is failure, then abort the remaining steps and
  // queue a network task with transport to run these steps:
  // Step 10.1: If transport.[[State]] is "closed" or "failed", then abort
  // these steps.

  // Step 14: If the previous step fails, abort the remaining steps and
  // queue a network task with transport to run these steps:
  // Step 14.1: If transport.[[State]] is "closed" or "failed", then abort
  // these steps.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED) {
    if (mChild) {
      mChild->Shutdown(true);
      mChild = nullptr;
    }
    // Cleanup should have been called, which means Ready has been
    // rejected and pulls resolved
    return;
  }

  // Step 14.2: Let error be the result of creating a WebTransportError with
  // "session".
  RefPtr<WebTransportError> error = new WebTransportError(
      "WebTransport connection rejected"_ns, WebTransportErrorSource::Session);
  // Step 14.3: Cleanup transport with error.
  Cleanup(error, nullptr, IgnoreErrors());

  mChild->Shutdown(true);
  mChild = nullptr;
}

bool WebTransport::ParseURL(const nsAString& aURL) const {
  NS_ENSURE_TRUE(!aURL.IsEmpty(), false);

  // 5.4 = https://w3c.github.io/webtransport/#webtransport-constructor
  // 5.4 #1 and #2
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURL);
  NS_ENSURE_SUCCESS(rv, false);

  // 5.4 #3
  if (!uri->SchemeIs("https")) {
    return false;
  }

  // 5.4 #4 no fragments
  bool hasRef;
  rv = uri->GetHasRef(&hasRef);
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && !hasRef, false);

  return true;
}

// aDroppedIncoming/aExpiredIncoming are passed separately because they are
// tracked at the DOM layer (WebTransportDatagramDuplexStream::mDroppedIncoming
// / mExpiredIncoming) rather than by the neqo transport, which doesn't track
// per-session incoming datagram drops/expiry at all (its datagrams().
// droppedIncoming()/expiredIncoming() are always 0). The DOM-level queue is
// what actually enforces incomingMaxBufferedDatagrams/incomingMaxAge, so it's
// the source of truth for these two fields.
static void PopulateConnectionStats(WebTransportConnectionStats& aStats,
                                    const WebTransportStatsData& aSource,
                                    uint64_t aDroppedIncoming,
                                    uint64_t aExpiredIncoming) {
  // bytesSent maps to neqo's bytes_tx (total wire bytes including QUIC
  // framing and retransmissions). The spec calls for payload bytes excluding
  // framing and retransmissions, which neqo does not expose separately, so
  // bytesSentOverhead (the difference) is omitted rather than reported as 0,
  // per the spec's "unavailable stats are absent" rule. See bug 2051624.
  aStats.mBytesSent.Construct(aSource.bytesSent());
  aStats.mBytesAcknowledged.Construct(aSource.bytesAcknowledged());
  aStats.mPacketsSent.Construct(aSource.packetsSent());
  aStats.mBytesLost.Construct(aSource.bytesLost());
  aStats.mPacketsLost.Construct(aSource.packetsLost());
  aStats.mBytesReceived.Construct(aSource.bytesReceived());
  aStats.mPacketsReceived.Construct(aSource.packetsReceived());
  aStats.mSmoothedRtt.Construct(aSource.smoothedRtt());
  aStats.mRttVariation.Construct(aSource.rttVariation());
  aStats.mMinRtt.Construct(aSource.minRtt());
  // estimatedSendRate is nullable; aSource.estimatedSendRate() == -1
  // signals "unknown".
  if (aSource.estimatedSendRate() >= 0) {
    aStats.mEstimatedSendRate.SetValue(
        static_cast<uint64_t>(aSource.estimatedSendRate()));
  }
  aStats.mAtSendCapacity = aSource.atSendCapacity();
  aStats.mDatagrams.mDroppedIncoming.Construct(aDroppedIncoming);
  aStats.mDatagrams.mExpiredIncoming.Construct(aExpiredIncoming);
  aStats.mDatagrams.mExpiredOutgoing.Construct(
      aSource.datagrams().expiredOutgoing());
  aStats.mDatagrams.mLostOutgoing.Construct(aSource.datagrams().lostOutgoing());
}

already_AddRefed<Promise> WebTransport::GetStats(ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#dom-webtransport-getstats
  LOG(("GetStats() called, mClosePending=%d", mClosePending));

  // Step 1: Let transport be this. (implicit)

  // Step 2: Let p be a new promise.
  RefPtr<Promise> promise = Promise::CreateInfallible(GetParentObject());

  // Step 3: If transport.[[State]] is "failed", reject p with an
  // InvalidStateError and abort these steps.
  if (mState == WebTransportState::FAILED) {
    promise->MaybeRejectWithInvalidStateError("WebTransport failed");
    return promise.forget();
  }

  // If close() has been called but hasn't completed (we're waiting on the
  // parent to return final stats), queue this request; it's resolved or
  // rejected once Close()'s IPC callback runs (see Close()).
  if (mClosePending) {
    LOG(("GetStats: close is pending, queuing promise (queue size: %zu)",
         mPendingGetStatsPromises.Length()));
    mPendingGetStatsPromises.AppendElement(promise);
    return promise.forget();
  }

  // Step 4: Run the following steps in parallel.
  //
  // Step 4.1 is handled below, before dispatching to SendGetStatsRequest().
  // Step 4.2 (reject if "failed" after waiting) is handled in
  // SendGetStatsRequest()'s IPC success callback, once the parent reports no
  // stats.
  //
  // Step 4.3: if [[State]] is "closed", resolve with the most recent stats
  // available for the connection. The exact collection point is
  // implementation-defined; this implementation caches stats when close()
  // completes (see Close()) or when the connection is closed remotely (see
  // RemoteClosed()).
  if (mState == WebTransportState::CLOSED) {
    LOG(("GetStats: state is CLOSED, hasCachedStats=%d",
         mCachedStats.isSome()));
    RefPtr<Promise> p = promise;
    if (mCachedStats) {
      WebTransportStatsData cachedStats = *mCachedStats;
      uint64_t droppedIncoming = mCachedDroppedIncoming;
      uint64_t expiredIncoming = mCachedExpiredIncoming;
      GetCurrentSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
          "WebTransport::GetStats", [p, cachedStats = std::move(cachedStats),
                                     droppedIncoming, expiredIncoming]() {
            WebTransportConnectionStats stats;
            PopulateConnectionStats(stats, cachedStats, droppedIncoming,
                                    expiredIncoming);
            p->MaybeResolve(stats);
          }));
    } else {
      GetCurrentSerialEventTarget()->Dispatch(
          NS_NewRunnableFunction("WebTransport::GetStats", [p]() {
            p->MaybeRejectWithInvalidStateError(
                "No stats available for closed connection");
          }));
    }
    return promise.forget();
  }

  // mChild may be null only if we entered an unexpected state; all normal
  // states (CONNECTING, CONNECTED) have mChild set (CLOSED is handled above).
  if (!mChild) {
    promise->MaybeRejectWithInvalidStateError("WebTransport not connected");
    return promise.forget();
  }

  // Step 4.1: If transport.[[State]] is "connecting", wait for the state to
  // change before gathering stats, so that getStats() resolves only after
  // [[Ready]] settles (and rejects if connecting fails). The resolve step is a
  // network task queued after the connecting state change (spec step 4.1/4.6),
  // so racing it against [[Ready]] must observe ready first.
  if (mState == WebTransportState::CONNECTING) {
    mReady->AddCallbacksWithCycleCollectedArgs(
        [](JSContext*, JS::Handle<JS::Value>, ErrorResult&, WebTransport* aSelf,
           Promise* aPromise) { aSelf->SendGetStatsRequest(aPromise); },
        [](JSContext*, JS::Handle<JS::Value>, ErrorResult&, WebTransport*,
           Promise* aPromise) {
          aPromise->MaybeRejectWithInvalidStateError("WebTransport failed");
        },
        RefPtr{this}, promise);
    return promise.forget();
  }

  // Steps 4.4-4.6: state is "connected"; gather stats now (see
  // SendGetStatsRequest()).
  SendGetStatsRequest(promise);

  // Step 5: Return p.
  return promise.forget();
}

void WebTransport::SendGetStatsRequest(Promise* aPromise) {
  // Step 4.4: Let gatheredStats be the list of stats specific to the
  // underlying connection needed to populate WebTransportConnectionStats and
  // WebTransportDatagramStats accurately. This is done in parallel via IPC.
  //
  // Step 4.5 (removing non-pooled-connection stats when [[NewConnection]] is
  // "no") does not apply: Firefox does not yet support WebTransport
  // connection pooling, so gatheredStats is never filtered.
  //
  // Step 4.6: Queue a network task with transport to run the following steps.
  if (!mChild) {
    aPromise->MaybeRejectWithInvalidStateError("WebTransport not connected");
    return;
  }
  mChild->SendGetStats(
      [promise = RefPtr(aPromise),
       self = RefPtr(this)](Maybe<WebTransportStatsData>&& aStats) {
        LOG(("GetStats callback: aStats.isSome() = %d", aStats.isSome()));
        if (!aStats) {
          // Step 4.2: transport.[[State]] became "failed" while waiting.
          LOG(("GetStats: No stats available\n"));
          promise->MaybeRejectWithInvalidStateError("Failed to get stats");
          return;
        }

        LOG(
            ("GetStats: bytesSent=%llu, bytesReceived=%llu, "
             "minRtt=%f, smoothedRtt=%f\n",
             (unsigned long long)aStats->bytesSent(),
             (unsigned long long)aStats->bytesReceived(), aStats->minRtt(),
             aStats->smoothedRtt()));

        // Step 4.6.1-4: Create a WebTransportConnectionStats object, create a
        // WebTransportDatagramStats object, set datagrams, and populate each
        // member from gatheredStats.
        // Also update the cache so post-close getStats() returns fresh data.
        // droppedIncoming/expiredIncoming are tracked at the DOM layer
        // (datagrams queue drops/expiry) rather than at the transport layer,
        // so snapshot them here.
        self->mCachedStats = Some(*aStats);
        self->mCachedDroppedIncoming =
            self->mDatagrams ? self->mDatagrams->GetDroppedIncoming() : 0;
        // The queue doesn't count expiry until bug 2023587 adds it.
        self->mCachedExpiredIncoming = 0;

        WebTransportConnectionStats stats;
        PopulateConnectionStats(stats, *aStats, self->mCachedDroppedIncoming,
                                self->mCachedExpiredIncoming);

        // Step 4.6.5: Resolve p with stats.
        promise->MaybeResolve(stats);
      },
      [promise = RefPtr(aPromise)](mozilla::ipc::ResponseRejectReason) {
        // IPC failure: treat as if the transport failed.
        promise->MaybeRejectWithInvalidStateError("Failed to get stats");
      });
}

already_AddRefed<Promise> WebTransport::ExportKeyingMaterial(
    const BufferSource& aLabel, const BufferSource& aContext,
    uint32_t aOutputLength, ErrorResult& aError) {
  LOG(("ExportKeyingMaterial() called"));

  // https://w3c.github.io/webtransport/#dom-webtransport-exportkeyingmaterial
  // Step 1: Let labelLength be label's byte length.
  nsTArray<uint8_t> labelData;
  ProcessTypedArraysFixed(aLabel, [&](const Span<const uint8_t>& aData) {
    labelData.AppendElements(aData.Elements(), aData.Length());
  });

  // Step 2: If labelLength is more than 255, return a promise rejected with a
  // RangeError.
  if (labelData.Length() > 255) {
    aError.ThrowRangeError("label must not exceed 255 bytes");
    return nullptr;
  }

  // Step 3-4: Let contextLength be context's byte length (context is a
  // required argument).
  nsTArray<uint8_t> contextData;
  ProcessTypedArraysFixed(aContext, [&](const Span<const uint8_t>& aData) {
    contextData.AppendElements(aData.Elements(), aData.Length());
  });

  // Step 5: If contextLength is more than 255, return a promise rejected with a
  // RangeError.
  if (contextData.Length() > 255) {
    aError.ThrowRangeError("context must not exceed 255 bytes");
    return nullptr;
  }

  // Step 6: If outputLength is 0 or more than the implementation-defined
  // maximum (which the spec requires to be at least 4096), return a promise
  // rejected with a RangeError.
  if (aOutputLength == 0 ||
      aOutputLength > nsIWebTransport::MAX_KEYING_MATERIAL_LENGTH) {
    aError.ThrowRangeError("outputLength must be between 1 and 4096 bytes");
    return nullptr;
  }

  // Step 7: Let p be a new promise.
  RefPtr<Promise> promise = Promise::CreateInfallible(GetParentObject());

  // The parallel steps abort with an InvalidStateError if the transport becomes
  // "closed" or "failed"; reject synchronously here as well.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED || !mChild) {
    promise->MaybeRejectWithInvalidStateError("Transport closed or failed");
    return promise.forget();
  }

  mChild->SendExportKeyingMaterial(
      labelData, contextData, aOutputLength,
      [self = RefPtr{this}, promise](Maybe<nsTArray<uint8_t>>&& aKeyingMaterial)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            // Step 8: If transport.[[State]] became "closed" or "failed",
            // reject with InvalidStateError
            if (self->mState == WebTransportState::CLOSED ||
                self->mState == WebTransportState::FAILED) {
              promise->MaybeRejectWithInvalidStateError(
                  "Transport closed or failed during export");
              return;
            }

            // The parent rejects with Nothing() on any internal export failure
            // (e.g. the TLS keying material exporter itself failed); there's no
            // spec-defined error for this case, so use InvalidStateError to
            // match the state-based rejection above.
            if (!aKeyingMaterial) {
              promise->MaybeRejectWithInvalidStateError(
                  "Failed to export keying material");
              return;
            }
            const nsTArray<uint8_t>& keyingMaterial = *aKeyingMaterial;

            // Convert the byte array to ArrayBuffer
            AutoJSAPI jsapi;
            if (!jsapi.Init(promise->GetGlobalObject())) {
              promise->MaybeRejectWithTypeError("Failed to initialize JS API");
              return;
            }
            JSContext* cx = jsapi.cx();

            UniquePtr<uint8_t[], JS::FreePolicy> data(
                static_cast<uint8_t*>(JS_malloc(cx, keyingMaterial.Length())));
            if (!data) {
              promise->MaybeRejectWithTypeError("Out of memory");
              return;
            }
            memcpy(data.get(), keyingMaterial.Elements(),
                   keyingMaterial.Length());

            JS::Rooted<JSObject*> arrayBuffer(
                cx, JS::NewArrayBufferWithContents(cx, keyingMaterial.Length(),
                                                   std::move(data)));
            if (!arrayBuffer) {
              promise->MaybeRejectWithTypeError("Failed to create ArrayBuffer");
              return;
            }

            JSObject* uint8Array = JS_NewUint8ArrayWithBuffer(
                cx, arrayBuffer, 0,
                static_cast<int64_t>(keyingMaterial.Length()));
            if (!uint8Array) {
              promise->MaybeRejectWithTypeError("Failed to create Uint8Array");
              return;
            }

            // Step 8.1/8.2: Let keyingMaterial be the result of exporting TLS
            // keying material, as defined in [WEB-TRANSPORT-OVERVIEW]
            // Section 4.1, with labelLength, label, contextLength, and if
            // present, context. Queue a network task with transport to resolve
            // p with keyingMaterial.
            JS::Rooted<JS::Value> value(cx, JS::ObjectValue(*uint8Array));
            promise->MaybeResolve(value);
          },
      [promise](mozilla::ipc::ResponseRejectReason) {
        promise->MaybeRejectWithInvalidStateError(
            "Failed to export keying material");
      });

  return promise.forget();
}

WebTransportReliabilityMode WebTransport::Reliability() { return mReliability; }

WebTransportCongestionControl WebTransport::CongestionControl() {
  // XXX We only implement cubic congestion control currently in QUIC
  return WebTransportCongestionControl::Default;
}

bool WebTransport::SupportsReliableOnly(const GlobalObject& aGlobal) {
  // XXX Change to true when we land http/2 support if on http2 (Bug 2071107)
  return false;
}

void WebTransport::GetProtocol(nsAString& aProtocol) { aProtocol = mProtocol; }

Nullable<uint16_t>
WebTransport::GetAnticipatedConcurrentIncomingUnidirectionalStreams() {
  return mAnticipatedConcurrentIncomingUnidirectionalStreams;
}

void WebTransport::SetAnticipatedConcurrentIncomingUnidirectionalStreams(
    const Nullable<uint16_t>& aValue) {
  mAnticipatedConcurrentIncomingUnidirectionalStreams = aValue;
  if (mChild) {
    mChild->SetAnticipatedConcurrentIncomingUnidirectionalStreams(aValue);
  }
}

Nullable<uint16_t>
WebTransport::GetAnticipatedConcurrentIncomingBidirectionalStreams() {
  return mAnticipatedConcurrentIncomingBidirectionalStreams;
}

void WebTransport::SetAnticipatedConcurrentIncomingBidirectionalStreams(
    const Nullable<uint16_t>& aValue) {
  mAnticipatedConcurrentIncomingBidirectionalStreams = aValue;
  if (mChild) {
    mChild->SetAnticipatedConcurrentIncomingBidirectionalStreams(aValue);
  }
}

void WebTransport::ResolveDraining() {
  LOG(("ResolveDraining() called"));
  mDraining->MaybeResolveWithUndefined();
}

void WebTransport::SetNegotiatedProtocol(const nsACString& aProtocol) {
  LOG(("SetNegotiatedProtocol: %s", PromiseFlatCString(aProtocol).get()));
  CopyUTF8toUTF16(aProtocol, mProtocol);
}

void WebTransport::SetResponseHeaders(nsTArray<WebTransportHeader>&& aHeaders) {
  auto internalHeaders =
      MakeRefPtr<InternalHeaders>(HeadersGuardEnum::Response);
  for (const auto& header : aHeaders) {
    internalHeaders->Append(header.name(), header.value(), IgnoreErrors());
  }
  // https://w3c.github.io/webtransport/#process-a-webtransport-fetch-response
  // Step 7: transport.[[ResponseHeaders]] must be an immutable Headers
  // object; without this, content script could mutate what is supposed to
  // be a read-only snapshot of the server's response headers.
  internalHeaders->SetGuard(HeadersGuardEnum::Immutable, IgnoreErrors());
  mResponseHeaders = new Headers(mGlobal, internalHeaders);
}

void WebTransport::RemoteClosed(bool aCleanly, const uint32_t& aCode,
                                const nsACString& aReason,
                                const Maybe<WebTransportStatsData>& aStats) {
  LOG(("Server closed: cleanly: %d, code %u, reason %s", aCleanly, aCode,
       PromiseFlatCString(aReason).get()));

  // Cache stats from the remote close notification. This is required by the
  // spec to allow getStats() to return statistics after the connection is
  // closed (per https://w3c.github.io/webtransport/#dom-webtransport-getstats
  // step 4.3).
  if (!mCachedStats && aStats) {
    LOG(("Caching stats from RemoteClosed"));
    mCachedStats = Some(*aStats);
    mCachedDroppedIncoming = mDatagrams ? mDatagrams->GetDroppedIncoming() : 0;
  }

  // Step 2 of https://w3c.github.io/webtransport/#web-transport-termination
  // We calculate cleanly on the parent
  // Step 2.1: If transport.[[State]] is "closed" or "failed", abort these
  // steps.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED) {
    return;
  }
  // Step 2.2: Let error be the result of creating a WebTransportError with
  // "session".
  RefPtr<WebTransportError> error = new WebTransportError(
      "remote WebTransport close"_ns, WebTransportErrorSource::Session);
  // Step 2.3: If cleanly is false, then cleanup transport with error, and
  // abort these steps.
  ErrorResult errorresult;
  if (!aCleanly) {
    Cleanup(error, nullptr, errorresult);
    return;
  }
  // Step 2.4: Let closeInfo be a new WebTransportCloseInfo.
  // Step 2.5: If code is given, set closeInfo’s closeCode to code.
  // Step 2.6: If reasonBytes is given, set closeInfo’s reason to reasonBytes,
  // UTF-8 decoded.
  WebTransportCloseInfo closeinfo;
  closeinfo.mCloseCode = aCode;
  closeinfo.mReason = aReason;

  // Step 2.7: Cleanup transport with error and closeInfo.
  Cleanup(error, &closeinfo, errorresult);
}

template <typename Stream>
void WebTransport::PropagateError(Stream* aStream, WebTransportError* aError) {
  ErrorResult rv;
  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobal)) {
    rv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> errorValue(cx);
  bool ok = ToJSValue(cx, aError, &errorValue);
  if (!ok) {
    rv.ThrowUnknownError("Internal error");
    return;
  }

  aStream->ErrorNative(cx, errorValue, IgnoreErrors());
}

static Nullable<uint32_t> ToNullableErrorCode(const Maybe<uint32_t>& aCode) {
  Nullable<uint32_t> code;
  if (aCode) {
    code.SetValue(*aCode);
  }
  return code;
}

void WebTransport::OnStreamResetOrStopSending(
    uint64_t aStreamId, const StreamResetOrStopSendingError& aError) {
  LOG(("WebTransport::OnStreamResetOrStopSending %p id=%" PRIx64, this,
       aStreamId));
  if (aError.type() == StreamResetOrStopSendingError::TStopSendingError) {
    RefPtr<WebTransportSendStream> stream = mSendStreams.Get(aStreamId);
    if (!stream) {
      return;
    }
    RefPtr<WebTransportError> error = new WebTransportError(
        "WebTransportStream StopSending"_ns, WebTransportErrorSource::Stream,
        ToNullableErrorCode(aError.get_StopSendingError().errorCode()));
    PropagateError(stream.get(), error);
  } else if (aError.type() == StreamResetOrStopSendingError::TResetError) {
    RefPtr<WebTransportReceiveStream> stream = mReceiveStreams.Get(aStreamId);
    LOG(("WebTransport::OnStreamResetOrStopSending reset %p stream=%p", this,
         stream.get()));
    if (!stream) {
      return;
    }
    RefPtr<WebTransportError> error = new WebTransportError(
        "WebTransportStream Reset"_ns, WebTransportErrorSource::Stream,
        ToNullableErrorCode(aError.get_ResetError().errorCode()));
    PropagateError(stream.get(), error);
  }
}

void WebTransport::Close(const WebTransportCloseInfo& aOptions,
                         ErrorResult& aRv) {
  LOG(("Close() called"));
  // https://w3c.github.io/webtransport/#dom-webtransport-close
  // Step 1 and Step 2: If transport.[[State]] is "closed" or "failed", then
  // abort these steps.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED) {
    return;
  }
  // Step 3: If transport.[[State]] is "connecting":
  if (mState == WebTransportState::CONNECTING) {
    // Step 3.1: Let error be the result of creating a WebTransportError with
    // "session".
    RefPtr<WebTransportError> error = new WebTransportError(
        "close() called on WebTransport while connecting"_ns,
        WebTransportErrorSource::Session);
    // Step 3.2: Cleanup transport with error.
    Cleanup(error, nullptr, aRv);
    // Step 3.3: Abort these steps.
    mChild->Shutdown(true);
    mChild = nullptr;
    return;
  }
  LOG(("Sending Close"));
  MOZ_ASSERT(mChild);

  // Step 4: Let session be transport.[[Session]].
  // Step 5: Let code be closeInfo.closeCode.
  // Step 6: "Let reasonString be the maximal code unit prefix of
  // closeInfo.reason where the length of the UTF-8 encoded prefix
  // doesn't exceed 1024."
  // Take the maximal "code unit prefix" of mReason and limit to 1024 bytes
  // Step 7: Let reason be reasonString, UTF-8 encoded.
  // Step 8: In parallel, terminate session with code and reason.

  nsCString reason;
  if (aOptions.mReason.Length() > 1024u) {
    // We want to start looking for the previous code point at one past the
    // limit, since if a code point ends exactly at the specified length, the
    // next byte will be the start of a new code point. Note
    // RewindToPriorUTF8Codepoint doesn't reduce the index if it points to the
    // start of a code point. We know reason[1024] is accessible since
    // Length() > 1024
    reason =
        Substring(aOptions.mReason, 0,
                  RewindToPriorUTF8Codepoint(aOptions.mReason.get(), 1024u));
  } else {
    reason = aOptions.mReason;
  }

  // Set close pending state so GetStats requests will be queued
  LOG(("Setting close pending state"));
  mClosePending = true;

  // Send Close IPC and get stats back.  We need to cache stats after close to
  // allow us to respond to GetStats() post-close with final stats, and Close()
  // will kill the IPC connection to the parent (and the WebTransport session
  // will go away in any case, so IPC to the  parent wouldn't help us).
  RefPtr<WebTransport> self = this;
  RefPtr<WebTransportError> error =
      new WebTransportError("close()"_ns, WebTransportErrorSource::Session);
  WebTransportCloseInfo closeInfo;
  closeInfo.mCloseCode = aOptions.mCloseCode;
  closeInfo.mReason = aOptions.mReason;

  mChild->SendClose(
      aOptions.mCloseCode, reason,
      [self, error, closeInfo](Maybe<WebTransportStatsData>&& aStats) {
        LOG(("Close callback received"));
        if (aStats) {
          LOG(("Caching stats from close: bytesSent=%llu",
               (unsigned long long)aStats->bytesSent()));
          self->mCachedStats = Some(*aStats);
          self->mCachedDroppedIncoming =
              self->mDatagrams ? self->mDatagrams->GetDroppedIncoming() : 0;
          // The queue doesn't count expiry until bug 2023587 adds it.
          self->mCachedExpiredIncoming = 0;
        } else {
          LOG(("No stats returned from close"));
          self->mCachedStats = Nothing();
        }

        // Resolve any pending GetStats promises
        LOG(("Resolving %zu pending GetStats promises",
             self->mPendingGetStatsPromises.Length()));
        for (auto& promise : self->mPendingGetStatsPromises) {
          if (self->mCachedStats) {
            WebTransportConnectionStats stats;
            PopulateConnectionStats(stats, *self->mCachedStats,
                                    self->mCachedDroppedIncoming,
                                    self->mCachedExpiredIncoming);
            promise->MaybeResolve(stats);
          } else {
            promise->MaybeRejectWithInvalidStateError(
                "No stats available for closed connection");
          }
        }
        self->mPendingGetStatsPromises.Clear();

        self->mClosePending = false;
      },
      [self, error, closeInfo](mozilla::ipc::ResponseRejectReason) {
        LOG(("Close IPC failed"));
        // Reject pending GetStats promises
        for (auto& promise : self->mPendingGetStatsPromises) {
          promise->MaybeRejectWithInvalidStateError("Close IPC failed");
        }
        self->mPendingGetStatsPromises.Clear();

        self->mClosePending = false;
      });
  LOG(("Close sent"));

  // Step 9: Cleanup transport with AbortError and closeInfo (sets mState to
  // CLOSED). Per spec, only step 8 ("terminate session") runs in parallel;
  // this must happen synchronously as part of close(), not deferred until
  // the Close IPC round trip (used above only to cache final stats)
  // completes.
  Cleanup(error, &closeInfo, aRv);
  LOG(("Cleanup done"));

  // The other side will call `Close()` for us now, make sure we don't call it
  // in our destructor.
  mChild->Shutdown(false);
  mChild = nullptr;
  LOG(("Close done"));
}

already_AddRefed<WebTransportDatagramDuplexStream> WebTransport::GetDatagrams(
    ErrorResult& aError) {
  return do_AddRef(mDatagrams);
}

already_AddRefed<Promise> WebTransport::CreateBidirectionalStream(
    const WebTransportSendStreamOptions& aOptions, ErrorResult& aRv) {
  return CreateBidirectionalStreamInternal(aOptions, aOptions.mSendGroup,
                                           aOptions.mSendOrder, aRv);
}

already_AddRefed<Promise> WebTransport::CreateBidirectionalStreamInternal(
    const WebTransportSendStreamOptions& aOptions,
    WebTransportSendGroup* aSendGroup, int64_t aSendOrder, ErrorResult& aRv) {
  LOG(("CreateBidirectionalStreamInternal() called"));
  // https://w3c.github.io/webtransport/#dom-webtransport-createbidirectionalstream
  RefPtr<Promise> promise = Promise::CreateInfallible(GetParentObject());

  // Step 3: If transport.[[State]] is "closed" or "failed", return a new
  // rejected promise with an InvalidStateError.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED || !mChild) {
    aRv.ThrowInvalidStateError("WebTransport closed or failed");
    return nullptr;
  }

  // Step 2.5: Let sendGroup be options's sendGroup. If sendGroup is not null,
  // and sendGroup.[[Transport]] is not this.[[Transport]], throw an
  // InvalidStateError.
  if (aSendGroup && aSendGroup->GetTransport() != this) {
    aRv.ThrowInvalidStateError("sendGroup belongs to a different WebTransport");
    return nullptr;
  }

  // Step 3: Let sendOrder be options's sendOrder
  RefPtr<WebTransportSendGroup> sendGroup = aSendGroup;
  int64_t sendOrder = aSendOrder;
  // Step 4: Let p be a new promise.
  // Step 5: Run the following steps in parallel, but abort them whenever
  // transport's [[State]] becomes "closed" or "failed", and instead queue
  // a network task with transport to reject p with an InvalidStateError.

  // Ask the parent to create the stream and send us the DataPipeSender/Receiver
  // pair
  Maybe<uint64_t> sendGroupId;
  if (sendGroup) {
    sendGroupId = Some(sendGroup->GetGroupId());
  }
  mChild->SendCreateBidirectionalStream(
      sendOrder, sendGroupId,
      [self = RefPtr{this}, sendOrder, sendGroup, promise](
          BidirectionalStreamResponse&& aPipes) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        LOG(("CreateBidirectionalStream response"));
        if (BidirectionalStreamResponse::Tnsresult == aPipes.type()) {
          promise->MaybeReject(aPipes.get_nsresult());
          return;
        }
        // Step 5.2.1: If transport.[[State]] is "closed" or "failed",
        // reject p with an InvalidStateError and abort these steps.
        if (BidirectionalStreamResponse::Tnsresult == aPipes.type()) {
          promise->MaybeReject(aPipes.get_nsresult());
          return;
        }
        if (self->mState == WebTransportState::CLOSED ||
            self->mState == WebTransportState::FAILED) {
          promise->MaybeRejectWithInvalidStateError(
              "Transport close/errored before CreateBidirectional finished");
          return;
        }
        uint64_t id = aPipes.get_BidirectionalStream().streamId();
        LOG(("Create WebTransportBidirectionalStream id=%" PRIx64, id));
        ErrorResult error;
        RefPtr<WebTransportBidirectionalStream> newStream =
            WebTransportBidirectionalStream::Create(
                self, self->mGlobal, id,
                aPipes.get_BidirectionalStream().inStream(),
                aPipes.get_BidirectionalStream().outStream(), sendOrder,
                sendGroup, error);
        LOG(("Returning a bidirectionalStream"));
        promise->MaybeResolve(newStream);
      },
      [self = RefPtr{this}, promise](mozilla::ipc::ResponseRejectReason) {
        LOG(("CreateBidirectionalStream reject"));
        promise->MaybeRejectWithInvalidStateError(
            "Transport close/errored before CreateBidirectional started");
      });

  // Step 6: return p
  return promise.forget();
}

already_AddRefed<ReadableStream> WebTransport::IncomingBidirectionalStreams() {
  return do_AddRef(mIncomingBidirectionalStreams);
}

already_AddRefed<Promise> WebTransport::CreateUnidirectionalStream(
    const WebTransportSendStreamOptions& aOptions, ErrorResult& aRv) {
  return CreateUnidirectionalStreamInternal(aOptions, aOptions.mSendGroup,
                                            aOptions.mSendOrder, aRv);
}

already_AddRefed<Promise> WebTransport::CreateUnidirectionalStreamInternal(
    const WebTransportSendStreamOptions& aOptions,
    WebTransportSendGroup* aSendGroup, int64_t aSendOrder, ErrorResult& aRv) {
  LOG(("CreateUnidirectionalStreamInternal() called"));
  // https://w3c.github.io/webtransport/#dom-webtransport-createunidirectionalstream
  // Step 1: If transport.[[State]] is "closed" or "failed", return a new
  // rejected promise with an InvalidStateError.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED || !mChild) {
    aRv.ThrowInvalidStateError("WebTransport closed or failed");
    return nullptr;
  }

  // Step 2: Let sendGroup be options's sendGroup.
  // Step 3: If sendGroup is not null, and sendGroup.[[Transport]] is not
  // this.[[Transport]], throw an InvalidStateError.
  if (aSendGroup && aSendGroup->GetTransport() != this) {
    aRv.ThrowInvalidStateError("sendGroup belongs to a different WebTransport");
    return nullptr;
  }

  // Step 4: Let sendOrder be options's sendOrder
  RefPtr<WebTransportSendGroup> sendGroup = aSendGroup;
  int64_t sendOrder = aSendOrder;

  // Step 4: Let p be a new promise.
  RefPtr<Promise> promise = Promise::CreateInfallible(GetParentObject());

  // Step 5: Run the following steps in parallel, but abort them whenever
  // transport's[[State]] becomes "closed" or "failed", and instead queue
  // a network task with transport to reject p with an InvalidStateError.

  // Ask the parent to create the stream and send us the DataPipeSender
  Maybe<uint64_t> sendGroupId;
  if (sendGroup) {
    sendGroupId = Some(sendGroup->GetGroupId());
  }
  mChild->SendCreateUnidirectionalStream(
      sendOrder, sendGroupId,
      [self = RefPtr{this}, sendOrder, sendGroup,
       promise](UnidirectionalStreamResponse&& aResponse)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            LOG(("CreateUnidirectionalStream response"));
            if (UnidirectionalStreamResponse::Tnsresult == aResponse.type()) {
              promise->MaybeReject(aResponse.get_nsresult());
              return;
            }
            // Step 5.1: Let internalStream be the result of creating an
            // outgoing unidirectional stream with transport.[[Session]].
            // Step 5.2: Queue a network task with transport to run the
            // following steps:
            // Step 5.2.1 If transport.[[State]] is "closed" or "failed",
            // reject p with an InvalidStateError and abort these steps.
            if (self->mState == WebTransportState::CLOSED ||
                self->mState == WebTransportState::FAILED ||
                aResponse.type() !=
                    UnidirectionalStreamResponse::TUnidirectionalStream) {
              promise->MaybeRejectWithInvalidStateError(
                  "Transport close/errored during CreateUnidirectional");
              return;
            }

            // Step 5.2.2.: Let stream be the result of creating a
            // WebTransportSendStream with internalStream, transport, and
            // sendOrder.
            ErrorResult error;
            uint64_t id = aResponse.get_UnidirectionalStream().streamId();
            LOG(("Create WebTransportSendStream id=%" PRIx64, id));
            RefPtr<WebTransportSendStream> writableStream =
                WebTransportSendStream::Create(
                    self, self->mGlobal, id,
                    aResponse.get_UnidirectionalStream().outStream(), sendOrder,
                    sendGroup, error);
            if (!writableStream) {
              promise->MaybeReject(std::move(error));
              return;
            }
            LOG(("Returning a writableStream"));
            // Step 5.2.3: Resolve p with stream.
            promise->MaybeResolve(writableStream);
          },
      [self = RefPtr{this}, promise](mozilla::ipc::ResponseRejectReason) {
        LOG(("CreateUnidirectionalStream reject"));
        promise->MaybeRejectWithInvalidStateError(
            "Transport close/errored during CreateUnidirectional");
      });

  // Step 6: return p
  return promise.forget();
}

already_AddRefed<ReadableStream> WebTransport::IncomingUnidirectionalStreams() {
  return do_AddRef(mIncomingUnidirectionalStreams);
}

already_AddRefed<WebTransportSendGroup> WebTransport::CreateSendGroup(
    ErrorResult& aRv) {
  LOG(("WebTransport::CreateSendGroup() called"));
  // https://w3c.github.io/webtransport/#dom-webtransport-createsendgroup
  // Step 1: If this.[[State]] is "closed" or "failed", throw an
  // InvalidStateError.
  if (mState == WebTransportState::CLOSED ||
      mState == WebTransportState::FAILED || !mChild) {
    aRv.ThrowInvalidStateError("WebTransport closed or failed");
    return nullptr;
  }
  // Step 2: Return the result of creating a WebTransportSendGroup with this.
  RefPtr<WebTransportSendGroup> group =
      new WebTransportSendGroup(mGlobal, this);

  // Assign a unique group ID locally so the group is immediately usable,
  // then notify the parent process to register it in the network stack.
  uint64_t groupId = mNextSendGroupId++;
  group->SetGroupId(groupId);
  LOG(("CreateSendGroup assigned ID: %" PRIu64, groupId));
  mChild->SendCreateSendGroup(groupId);

  return group.forget();
}

// Can be invoked with "error", "error, error, and true/false", or "error and
// closeInfo", but reason and abruptly are never used, and it does use closeinfo
void WebTransport::Cleanup(WebTransportError* aError,
                           const WebTransportCloseInfo* aCloseInfo,
                           ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#webtransport-cleanup
  // Step 1: Let sendStreams be a copy of transport.[[SendStreams]]
  // Step 2: Let receiveStreams be a copy of transport.[[ReceiveStreams]]
  // Steps 3-4 and 11-13 (datagrams) are handled by
  // WebTransportDatagramDuplexStream below.
  // Step 5: Let ready be transport.[[Ready]].
  // Step 6: Let closed be transport.[[Closed]].
  // Step 7: Let incomingBidirectionalStreams be
  // transport.[[IncomingBidirectionalStreams]].
  // Step 8: Let incomingUnidirectionalStreams be
  // transport.[[IncomingUnidirectionalStreams]].
  // Step 9: Set transport.[[SendStreams]] to an empty set.
  // Step 10: Set transport.[[ReceiveStreams]] to an empty set.
  // Script can run synchronously (via a 'then' getter) when closed resolves
  // and the incoming streams close, so nothing past this point may read
  // transport's slots.
  LOG(("Cleanup started"));
  nsTHashMap<uint64_t, RefPtr<WebTransportSendStream>> sendStreams;
  sendStreams.SwapElements(mSendStreams);
  nsTHashMap<uint64_t, RefPtr<WebTransportReceiveStream>> receiveStreams;
  receiveStreams.SwapElements(mReceiveStreams);
  RefPtr<Promise> ready = mReady;
  RefPtr<Promise> closed = mClosed;
  RefPtr<ReadableStream> incomingBidirectionalStreams =
      mIncomingBidirectionalStreams;
  RefPtr<ReadableStream> incomingUnidirectionalStreams =
      mIncomingUnidirectionalStreams;
  RefPtr<WebTransportDatagramDuplexStream> datagrams = mDatagrams;

  // Step 14: If closeInfo is given, then set transport.[[State]] to "closed".
  // Otherwise, set transport.[[State]] to "failed".
  mState = aCloseInfo ? WebTransportState::CLOSED : WebTransportState::FAILED;

  // Notify all the listeners of the closed session
  if (aCloseInfo && mInnerWindowID != 0) {
    mService->WebTransportSessionClosed(
        mInnerWindowID, mHttpChannelID, aCloseInfo->mCloseCode,
        NS_ConvertUTF8toUTF16(aCloseInfo->mReason));
  }

  // Step 15: For each stream in sendStreams, run the following steps:
  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobal)) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> errorValue(cx);
  bool ok = ToJSValue(cx, aError, &errorValue);
  if (!ok) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }

  for (const auto& stream : sendStreams.Values()) {
    // This MOZ_KnownLive is redundant, see bug 1620312
    // Step 15.2: Error stream with error.
    MOZ_KnownLive(stream)->ErrorNative(cx, errorValue, IgnoreErrors());
  }
  // Step 16: For each stream in receiveStreams, error stream with error.
  for (const auto& stream : receiveStreams.Values()) {
    stream->ErrorNative(cx, errorValue, IgnoreErrors());
  }
  // Step 17:
  if (aCloseInfo) {
    // 17.1: Resolve closed with closeInfo.
    LOG(("Resolving closed with closeinfo"));
    closed->MaybeResolve(*aCloseInfo);
    // 17.2: Assert: ready is settled.
    // However, if the connection closes before maxDatagramSize is received,
    // ready may still be pending. Resolve it in that case.
    if (ready->State() == Promise::PromiseState::Pending) {
      ready->MaybeResolveWithUndefined();
    }
    // 17.3: Close incomingBidirectionalStreams
    incomingBidirectionalStreams->CloseNative(cx, IgnoreErrors());
    // 17.4: Close incomingUnidirectionalStreams
    incomingUnidirectionalStreams->CloseNative(cx, IgnoreErrors());
  } else {
    // Step 18
    // 18.1: Reject closed with error
    LOG(("Rejecting closed"));
    closed->MaybeReject(errorValue);
    // 18.2: Set closed.[[PromiseIsHandled]] to true.
    closed->SetSettledPromiseIsHandled();
    // 18.3: Reject ready with error
    ready->MaybeReject(errorValue);
    // 18.4: Set ready.[[PromiseIsHandled]] to true.
    ready->SetSettledPromiseIsHandled();
    // 18.5: Error incomingBidirectionalStreams with error
    incomingBidirectionalStreams->ErrorNative(cx, errorValue, IgnoreErrors());
    // 18.6: Error incomingUnidirectionalStreams with error
    incomingUnidirectionalStreams->ErrorNative(cx, errorValue, IgnoreErrors());
  }
  // Let go of the algorithms
  mIncomingBidirectionalAlgorithm = nullptr;
  mIncomingUnidirectionalAlgorithm = nullptr;

  // Empty each outgoing datagram writable's [[OutgoingDatagramsQueue]], then
  // close (clean close) or error (failure) each writable itself, like the
  // send/receive streams above.
  // TODO: Cleanup() also resolves each writable's withheld write promise,
  // which the current spec doesn't do; see
  // https://github.com/w3c/webtransport/pull/790.
  if (datagrams) {
    datagrams->Cleanup();
    datagrams->CloseOrErrorWritables(cx, !!aCloseInfo, errorValue);
  }

  // We no longer block BFCache
  NotifyToWindow(false);
}

void WebTransport::SendSetSendOrder(uint64_t aStreamId, int64_t aSendOrder) {
  if (!mChild || !mChild->CanSend()) {
    return;
  }
  mChild->SendSetSendOrder(aStreamId, aSendOrder);
}

void WebTransport::SendSetSendGroup(uint64_t aStreamId, uint64_t aGroupId) {
  if (!mChild || !mChild->CanSend()) {
    return;
  }
  mChild->SendSetSendGroup(aStreamId, aGroupId);
}

void WebTransport::NotifyBFCacheOnMainThread(nsPIDOMWindowInner* aInner,
                                             bool aCreated) {
  AssertIsOnMainThread();
  if (!aInner) {
    return;
  }
  if (aCreated) {
    aInner->RemoveFromBFCacheSync();
  }

  uint32_t count = aInner->UpdateWebTransportCount(aCreated);
  // It's okay for WindowGlobalChild to not exist, as it should mean it already
  // is destroyed and can't enter bfcache anyway.
  if (WindowGlobalChild* child = aInner->GetWindowGlobalChild()) {
    if (aCreated && count == 1) {
      // The first WebTransport is active.
      child->BlockBFCacheFor(BFCacheStatus::ACTIVE_WEBTRANSPORT);
    } else if (count == 0) {
      child->UnblockBFCacheFor(BFCacheStatus::ACTIVE_WEBTRANSPORT);
    }
  }
}

class BFCacheNotifyWTRunnable final : public WorkerProxyToMainThreadRunnable {
 public:
  explicit BFCacheNotifyWTRunnable(bool aCreated) : mCreated(aCreated) {}

  void RunOnMainThread(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();
    if (aWorkerPrivate->IsDedicatedWorker()) {
      WebTransport::NotifyBFCacheOnMainThread(
          aWorkerPrivate->GetAncestorWindow(), mCreated);
      return;
    }
    if (aWorkerPrivate->IsSharedWorker()) {
      aWorkerPrivate->GetRemoteWorkerController()->NotifyWebTransport(mCreated);
      return;
    }
    MOZ_ASSERT_UNREACHABLE("Unexpected worker type");
  }

  void RunBackOnWorkerThreadForCleanup(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
  }

 private:
  bool mCreated;
};

void WebTransport::NotifyToWindow(bool aCreated) const {
  if (NS_IsMainThread()) {
    NotifyBFCacheOnMainThread(GetParentObject()->GetAsInnerWindow(), aCreated);
    return;
  }

  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  if (wp->IsDedicatedWorker() || wp->IsSharedWorker()) {
    RefPtr<BFCacheNotifyWTRunnable> runnable =
        new BFCacheNotifyWTRunnable(aCreated);

    runnable->Dispatch(wp);
  }
};

}  // namespace mozilla::dom
