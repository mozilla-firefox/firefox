/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebTransport.h"
#include "mozilla/dom/WebTransportLog.h"

namespace mozilla::dom {

void WebTransportChild::Shutdown(bool aClose) {
  LOG(("WebTransportChild::Shutdown() for %p (%p)", this, mTransport));
  mTransport = nullptr;
  if (!aClose || !CanSend()) {
    return;
  }

  Close();
}

void WebTransportChild::CloseAll() {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->RemoteClosed(false, 0, EmptyCString(), Nothing());
  }
}

::mozilla::ipc::IPCResult WebTransportChild::RecvCloseAll(
    CloseAllResolver&& aResolver) {
  CloseAll();
  aResolver(NS_OK);
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvRemoteClosed(
    const bool& aCleanly, const uint32_t& aCode, const nsACString& aReason,
    const Maybe<WebTransportStatsData>& aStats) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->RemoteClosed(aCleanly, aCode, aReason, aStats);
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvIncomingBidirectionalStream(
    const uint64_t& aStreamId, const RefPtr<DataPipeReceiver>& aIncoming,
    const RefPtr<DataPipeSender>& aOutgoing) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->NewBidirectionalStream(aStreamId, aIncoming, aOutgoing);
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvIncomingUnidirectionalStream(
    const uint64_t& aStreamId, const RefPtr<DataPipeReceiver>& aStream) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->NewUnidirectionalStream(aStreamId, aStream);
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvIncomingDatagram(
    nsTArray<uint8_t>&& aData, const TimeStamp& aRecvTimeStamp) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->NewDatagramReceived(std::move(aData), aRecvTimeStamp);
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvDatagramReady(
    const uint64_t& aDatagramId) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->OnDatagramReady(aDatagramId);
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvDraining() {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->ResolveDraining();
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvNegotiatedProtocolAndHeaders(
    const nsACString& aSubprotocol, nsTArray<WebTransportHeader>&& aHeaders) {
  LOG(("WebTransportChild::RecvNegotiatedProtocolAndHeaders: %s",
       PromiseFlatCString(aSubprotocol).get()));
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->SetNegotiatedProtocol(aSubprotocol);
    self->SetResponseHeaders(std::move(aHeaders));
  }
  return IPC_OK();
}

::mozilla::ipc::IPCResult WebTransportChild::RecvOnStreamResetOrStopSending(
    const uint64_t& aStreamId, const StreamResetOrStopSendingError& aError) {
  if (mTransport) {
    RefPtr<WebTransport> self(mTransport);
    self->OnStreamResetOrStopSending(aStreamId, aError);
  }
  return IPC_OK();
}

void WebTransportChild::SetAnticipatedConcurrentIncomingUnidirectionalStreams(
    const Nullable<uint16_t>& aValue) {
  if (CanSend()) {
    Maybe<uint16_t> maybeValue;
    if (!aValue.IsNull()) {
      maybeValue = Some(aValue.Value());
    }
    SendSetAnticipatedConcurrentIncomingUnidirectionalStreams(maybeValue);
  }
}

void WebTransportChild::SetAnticipatedConcurrentIncomingBidirectionalStreams(
    const Nullable<uint16_t>& aValue) {
  if (CanSend()) {
    Maybe<uint16_t> maybeValue;
    if (!aValue.IsNull()) {
      maybeValue = Some(aValue.Value());
    }
    SendSetAnticipatedConcurrentIncomingBidirectionalStreams(maybeValue);
  }
}

}  // namespace mozilla::dom
