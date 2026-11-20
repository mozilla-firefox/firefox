/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_WEBTRANSPORTCHILD_H_
#define DOM_WEBTRANSPORT_WEBTRANSPORTCHILD_H_

#include "mozilla/TimeStamp.h"
#include "mozilla/dom/PWebTransportChild.h"
#include "mozilla/ipc/DataPipe.h"
#include "nsISupportsImpl.h"

namespace mozilla::dom {

class WebTransport;

class WebTransportChild : public PWebTransportChild {
 public:
  NS_INLINE_DECL_REFCOUNTING(WebTransportChild)
  explicit WebTransportChild(WebTransport* aTransport)
      : mTransport(aTransport) {}

  virtual void CloseAll();

  void Shutdown(bool aClose);

  void SetAnticipatedConcurrentIncomingUnidirectionalStreams(
      const Nullable<uint16_t>& aValue);
  void SetAnticipatedConcurrentIncomingBidirectionalStreams(
      const Nullable<uint16_t>& aValue);

  ::mozilla::ipc::IPCResult RecvCloseAll(CloseAllResolver&& aResolver);

  ::mozilla::ipc::IPCResult RecvRemoteClosed(
      const bool& aCleanly, const uint32_t& aCode, const nsACString& aReason,
      const Maybe<WebTransportStatsData>& aStats);

  ::mozilla::ipc::IPCResult RecvIncomingBidirectionalStream(
      const uint64_t& aStreamId,
      const RefPtr<mozilla::ipc::DataPipeReceiver>& aIncoming,
      const RefPtr<mozilla::ipc::DataPipeSender>& aOutgoing);

  ::mozilla::ipc::IPCResult RecvIncomingUnidirectionalStream(
      const uint64_t& aStreamId,
      const RefPtr<mozilla::ipc::DataPipeReceiver>& aStream);

  ::mozilla::ipc::IPCResult RecvIncomingDatagram(
      nsTArray<uint8_t>&& aData, const TimeStamp& aRecvTimeStamp);

  ::mozilla::ipc::IPCResult RecvDatagramReady(const uint64_t& aDatagramId);

  ::mozilla::ipc::IPCResult RecvDraining();

  ::mozilla::ipc::IPCResult RecvNegotiatedProtocolAndHeaders(
      const nsACString& aSubprotocol, nsTArray<WebTransportHeader>&& aHeaders);

  ::mozilla::ipc::IPCResult RecvOnStreamResetOrStopSending(
      const uint64_t& aStreamId, const StreamResetOrStopSendingError& aError);

 protected:
  WebTransport* mTransport;  // WebTransport holds a strong reference to us, and
                             // calls Shutdown() before releasing it
  virtual ~WebTransportChild() { MOZ_ASSERT(!mTransport); }
};

}  // namespace mozilla::dom

#endif  // DOM_WEBTRANSPORT_WEBTRANSPORTCHILD_H_
