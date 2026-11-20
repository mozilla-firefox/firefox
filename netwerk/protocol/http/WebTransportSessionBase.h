/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_WebTransportSessionBase_h
#define mozilla_net_WebTransportSessionBase_h

#include <functional>

#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/PWebTransport.h"
#include "nsISupportsImpl.h"
#include "nsIWebTransport.h"
#include "nsString.h"
#include "nsTArray.h"

class WebTransportSessionEventListener;

namespace mozilla::net {

class WebTransportStreamBase;
class Http3WebTransportSession;

// Wraps a WebTransportStatsData snapshot so it can be passed through the
// scriptable WebTransportSessionEventListener::OnSessionClosed/
// OnStatsAvailable methods; the raw data itself is only accessible to
// native (C++) consumers via the noscript GetRawStats().
class WebTransportSessionStatsWrapper final
    : public nsIWebTransportSessionStats {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit WebTransportSessionStatsWrapper(
      const mozilla::dom::WebTransportStatsData& aStats)
      : mStats(aStats) {}

  NS_IMETHOD GetRawStats(mozilla::dom::WebTransportStatsData** aStats) override;

 private:
  ~WebTransportSessionStatsWrapper() = default;

  mozilla::dom::WebTransportStatsData mStats;
};

class WebTransportSessionBase {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  WebTransportSessionBase() = default;

  void SetWebTransportSessionEventListener(
      WebTransportSessionEventListener* listener);

  virtual uint64_t GetStreamId() const = 0;
  virtual void CloseSession(uint32_t aStatus, const nsACString& aReason) = 0;
  virtual void GetMaxDatagramSize() = 0;
  virtual void GetResponseHeaders(nsTArray<nsCString>& aNames,
                                  nsTArray<nsCString>& aValues) {}
  virtual nsresult ExportKeyingMaterial(const nsTArray<uint8_t>& aLabel,
                                        const nsTArray<uint8_t>& aContext,
                                        uint32_t aOutputLength,
                                        nsTArray<uint8_t>& aKeyingMaterial) = 0;
  virtual void GetNegotiatedProtocol(nsACString& aProtocol) = 0;
  virtual void GetStats() = 0;
  virtual void SetAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint16_t aValue) = 0;
  virtual void SetAnticipatedConcurrentIncomingBidirectionalStreams(
      uint16_t aValue) = 0;
  // aTimestamp is when the datagram was written, for outgoingMaxAge.
  virtual void SendDatagram(nsTArray<uint8_t>&& aData, uint64_t aTrackingId,
                            uint64_t aSendGroupId, int64_t aSendOrder,
                            TimeStamp aTimestamp) = 0;
  virtual void SetDatagramMaxBufferedDatagrams(
      uint32_t aMaxBufferedDatagrams) = 0;
  virtual void SetDatagramMaxAge(double aMaxAge) = 0;
  virtual nsresult RegisterSendGroup(uint64_t aGroupId) = 0;
  virtual void CreateOutgoingBidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) = 0;
  virtual void CreateOutgoingUnidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) = 0;
  virtual void StartReading() {}

  virtual Http3WebTransportSession* GetHttp3WebTransportSession() {
    return nullptr;
  }

 protected:
  virtual ~WebTransportSessionBase() = default;

  already_AddRefed<WebTransportSessionEventListener> GetListener();
  already_AddRefed<WebTransportSessionEventListener> TakeListener();

  Mutex mListenerLock{"WebTransportSessionBase::mListenerLock"};
  RefPtr<WebTransportSessionEventListener> mListener
      MOZ_GUARDED_BY(mListenerLock);
};

}  // namespace mozilla::net

#endif  // mozilla_net_WebTransportSessionBase_h
