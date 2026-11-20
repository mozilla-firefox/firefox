/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_PARENT_WEBTRANSPORTPARENT_H_
#define DOM_WEBTRANSPORT_PARENT_WEBTRANSPORTPARENT_H_

#include <limits>

#include "ErrorList.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/PWebTransportParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsIPrincipal.h"
#include "nsISupports.h"
#include "nsIWebTransport.h"
#include "nsIWebTransportStream.h"
#include "nsTHashMap.h"

namespace mozilla::dom {

enum class WebTransportReliabilityMode : uint8_t;

class WebTransportParent : public PWebTransportParent,
                           public WebTransportSessionEventListener {
  using IPCResult = mozilla::ipc::IPCResult;

 public:
  WebTransportParent() = default;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_WEBTRANSPORTSESSIONEVENTLISTENER

  void Create(const nsAString& aURL, nsIPrincipal* aPrincipal,
              const uint64_t& aBrowsingContextID,
              const IPCClientInfo& aClientInfo, const bool& aDedicated,
              const bool& aRequireUnreliable,
              const uint32_t& aCongestionControl,
              nsTArray<nsString>&& aProtocols,
              nsTArray<WebTransportHeader>&& aHeaders,
              nsTArray<WebTransportHash>&& aServerCertHashes,
              const mozilla::Maybe<uint16_t>&
                  aAnticipatedConcurrentIncomingUnidirectionalStreams,
              const mozilla::Maybe<uint16_t>&
                  aAnticipatedConcurrentIncomingBidirectionalStreams,
              Endpoint<PWebTransportParent>&& aParentEndpoint,
              std::function<void(std::tuple<const nsresult&, const uint8_t&,
                                            const uint64_t&>)>&& aResolver);

  IPCResult RecvClose(const uint32_t& aCode, const nsACString& aReason,
                      CloseResolver&& aResolver);

  IPCResult RecvSetSendOrder(uint64_t aStreamId, int64_t aSendOrder);

  IPCResult RecvSetSendGroup(uint64_t aStreamId, uint64_t aGroupId);

  IPCResult RecvCreateSendGroup(uint64_t aGroupId);

  IPCResult RecvSetAnticipatedConcurrentIncomingUnidirectionalStreams(
      const Maybe<uint16_t>& aValue);

  IPCResult RecvSetAnticipatedConcurrentIncomingBidirectionalStreams(
      const Maybe<uint16_t>& aValue);

  IPCResult RecvExportKeyingMaterial(nsTArray<uint8_t>&& aLabel,
                                     nsTArray<uint8_t>&& aContext,
                                     uint32_t aOutputLength,
                                     ExportKeyingMaterialResolver&& aResolver);

  IPCResult RecvGetStats(GetStatsResolver&& aResolver);

  IPCResult RecvCreateUnidirectionalStream(
      int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
      CreateUnidirectionalStreamResolver&& aResolver);
  IPCResult RecvCreateBidirectionalStream(
      int64_t aSendOrder, Maybe<uint64_t> aSendGroupId,
      CreateBidirectionalStreamResolver&& aResolver);

  ::mozilla::ipc::IPCResult RecvOutgoingDatagram(
      const uint64_t& aDatagramId, nsTArray<uint8_t>&& aData,
      const TimeStamp& aTimestamp, const uint64_t& aSendGroupId,
      const int64_t& aSendOrder, OutgoingDatagramResolver&& aResolver);

  ::mozilla::ipc::IPCResult RecvUpdateDatagramQueueParams(
      const uint32_t& aOutgoingMaxBufferedDatagrams,
      const double& aOutgoingMaxAge);

  ::mozilla::ipc::IPCResult RecvGetMaxDatagramSize(
      GetMaxDatagramSizeResolver&& aResolver);

  ::mozilla::ipc::IPCResult RecvGetHttpChannelID(
      GetHttpChannelIDResolver&& aResolver);

  // Drops the callback-map entries of a peer-opened stream whose pipes have
  // all finished copying.
  void RemoveFinishedIncomingStream(uint64_t aStreamId);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  class OnResetOrStopSendingCallback final {
   public:
    // aStopSending, if given, handles STOP_SENDING, which only concerns the
    // send half of a bidirectional stream; aCallback handles the rest.
    explicit OnResetOrStopSendingCallback(
        std::function<void(nsresult)>&& aCallback,
        std::function<void(nsresult)>&& aStopSending = nullptr)
        : mCallback(std::move(aCallback)),
          mStopSending(std::move(aStopSending)) {}
    ~OnResetOrStopSendingCallback() = default;

    void OnResetOrStopSending(nsresult aError) { mCallback(aError); }
    void OnStopSending(nsresult aError) {
      (mStopSending ? mStopSending : mCallback)(aError);
    }

   private:
    std::function<void(nsresult)> mCallback;
    std::function<void(nsresult)> mStopSending;
  };

 protected:
  virtual ~WebTransportParent();

 private:
  void NotifyRemoteClosed(bool aCleanly, uint32_t aErrorCode,
                          const nsACString& aReason,
                          const WebTransportStatsData& aStats);

  // Settles and clears every resolver waiting on the in-flight gather. Must be
  // called on the socket thread.
  void ResolvePendingGetStats(const Maybe<WebTransportStatsData>& aStats);

  // Closes the session for a child that went away without Close().  Must be
  // called on the socket thread after mWebTransport has been retargeted there.
  void CloseSessionForDestroyedActor();

  using ResolveType =
      std::tuple<const nsresult&, const uint8_t&, const uint64_t&>;

  static void CallResolver(std::function<void(ResolveType)>& aFn, nsresult aRv,
                           uint8_t aReliability, uint64_t aMaxDatagramSize) {
    aFn(ResolveType(aRv, aReliability, aMaxDatagramSize));
  }
  nsCOMPtr<nsISerialEventTarget> mSocketThread;
  Atomic<bool> mSessionReady{false};
  // Set once mWebTransport has been retargeted to the socket thread.  Socket
  // thread only.
  bool mRetargeted = false;
  uint64_t mSessionId{0};

  mozilla::Mutex mMutex{"WebTransportParent::mMutex"};
  std::function<void(ResolveType)> mResolver MOZ_GUARDED_BY(mMutex);
  // This is needed because mResolver is resolved on the background thread and
  // OnSessionClosed is called on the socket thread.
  std::function<void()> mExecuteAfterResolverCallback MOZ_GUARDED_BY(mMutex);
  OutgoingDatagramResolver mOutgoingDatagramResolver;
  GetMaxDatagramSizeResolver mMaxDatagramSizeResolver;
  uint64_t mMaxDatagramSizeValue{0};
  // Last value set via RecvUpdateDatagramQueueParams(), used by
  // RecvOutgoingDatagram() to drop datagrams that are already older than
  // outgoingMaxAge by the time they reach the parent (see comment there).
  double mDatagramMaxAge{std::numeric_limits<double>::infinity()};
  // Latest write timestamp handed to neqo; see RecvOutgoingDatagram().
  TimeStamp mLastOutgoingDatagramTimestamp;
  // When OnSessionReady() ran; the earliest a datagram could have been sent.
  TimeStamp mSessionReadyTime;
  // Resolvers for in-flight GetStats() requests; only one gather runs at a
  // time, so concurrent calls share it. Socket thread only; drained via
  // ResolvePendingGetStats().
  nsTArray<GetStatsResolver> mGetStatsResolvers;
  FlippedOnce<false> mClosed MOZ_GUARDED_BY(mMutex);

  nsCOMPtr<nsIWebTransport> mWebTransport;
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;

  // What we need to be able to lookup by streamId
  template <typename T>
  struct StreamHash {
    OnResetOrStopSendingCallback mCallback;
    nsCOMPtr<T> mStream;
    // Bidirectional streams only: the entry is removed once both are set.
    bool mResetReceived = false;
    bool mStopSendingReceived = false;
  };
  nsTHashMap<NoMemMoveKey<nsUint64HashKey>,
             StreamHash<nsIWebTransportBidirectionalStream>>
      mBidiStreamCallbackMap;
  nsTHashMap<NoMemMoveKey<nsUint64HashKey>,
             StreamHash<nsIWebTransportSendStream>>
      mUniStreamCallbackMap;
  // Incoming (receive-only) unidirectional streams: tracked separately from
  // mUniStreamCallbackMap (which holds outgoing send streams) so
  // RecvStopSendingStream() can find the stream a cancel() on the DOM side
  // targets and call its own sendStopSending().
  nsTHashMap<NoMemMoveKey<nsUint64HashKey>,
             StreamHash<nsIWebTransportReceiveStream>>
      mIncomingUniStreamCallbackMap;
};

}  // namespace mozilla::dom

#endif  // DOM_WEBTRANSPORT_PARENT_WEBTRANSPORTPARENT_H_
