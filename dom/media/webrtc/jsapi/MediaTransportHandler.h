/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MTRANSPORTHANDLER_H_
#define MTRANSPORTHANDLER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "MediaEventSource.h"
#include "RTCStatsReport.h"
#include "common/CandidateInfo.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/RTCConfigurationBinding.h"
#include "mozilla/dom/RTCErrorBinding.h"
#include "mozilla/dom/RTCIceTransportBinding.h"  // RTCIceTransportState
#include "mozilla/dom/RTCPeerConnectionBinding.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "transport/MediaTransportTypes.h"
#include "transport/dtlsidentity.h"  // For DtlsDigest
#include "transport/nr_socket_proxy_config.h"
#include "transport/nricectx.h"        // Need some enums
#include "transport/transportlayer.h"  // Need the State enum

namespace mozilla {
class DtlsIdentity;
class NrIceCtx;
class NrIceMediaStream;
class NrIceResolver;
class TransportFlow;
class RTCStatsQuery;

namespace dom {
struct RTCStatsReportInternal;
}

// The events a MediaTransportHandler emits, and the DTLS state it last
// reported. Shared between a MediaTransportHandler and whatever produces its
// events, so the producer never needs a reference to the handler itself, which
// allows the lifecycles of each side to be decoupled.
class MediaTransportEvents final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaTransportEvents)

  TransportLayer::State GetState(const std::string& aTransportId,
                                 bool aRtcp) const;

  void OnCandidate(const std::string& aTransportId,
                   CandidateInfo&& aCandidateInfo);
  void OnCandidateError(IceCandidateErrorInfo&& aErrorInfo);
  void OnAlpnNegotiated(const std::string& aAlpn);
  void OnGatheringStateChange(const std::string& aTransportId,
                              dom::RTCIceGathererState aState);
  void OnConnectionStateChange(
      const std::string& aTransportId, dom::RTCIceTransportState aState,
      const Maybe<dom::IceCandidateAttributePair>& aSelectedPair);
  void OnPacketReceived(std::string&& aTransportId, MediaPacket&& aPacket);
  void OnEncryptedSending(const std::string& aTransportId,
                          MediaPacket&& aPacket);
  void OnStateChange(const std::string& aTransportId,
                     TransportLayer::State aState,
                     nsTArray<nsTArray<uint8_t>>&& aRemoteCerts,
                     Maybe<dom::RTCErrorParams> aError = Nothing());
  void OnRtcpStateChange(const std::string& aTransportId,
                         TransportLayer::State aState,
                         Maybe<dom::RTCErrorParams> aError = Nothing());

  // Just RTP/RTCP
  MediaEventProducerOneCopyPerThread<std::string, MediaPacket>
      mRtpPacketReceived;
  // Just SCTP
  MediaEventProducerOneCopyPerThread<std::string, MediaPacket>
      mSctpPacketReceived;
  MediaEventProducer<std::string, CandidateInfo> mCandidateGathered;
  MediaEventProducer<IceCandidateErrorInfo> mCandidateError;
  MediaEventProducer<std::string, bool> mAlpnNegotiated;
  MediaEventProducer<std::string, dom::RTCIceGathererState>
      mGatheringStateChange;
  MediaEventProducer<std::string, dom::RTCIceTransportState,
                     Maybe<dom::IceCandidateAttributePair>>
      mConnectionStateChange;
  MediaEventProducer<std::string, MediaPacket> mEncryptedSending;
  MediaEventProducer<std::string, TransportLayer::State,
                     nsTArray<nsTArray<uint8_t>>, Maybe<dom::RTCErrorParams>>
      mStateChange;
  MediaEventProducer<std::string, TransportLayer::State,
                     Maybe<dom::RTCErrorParams>>
      mRtcpStateChange;

 private:
  ~MediaTransportEvents() = default;

  mutable Mutex mStateCacheMutex{"MediaTransportEvents::mStateCacheMutex"};
  std::map<std::string, TransportLayer::State> mStateCache
      MOZ_GUARDED_BY(mStateCacheMutex);
  std::map<std::string, TransportLayer::State> mRtcpStateCache
      MOZ_GUARDED_BY(mStateCacheMutex);
};

class MediaTransportHandler {
 public:
  // Creates a MediaTransportHandlerLocal.
  static already_AddRefed<MediaTransportHandler> Create();

  explicit MediaTransportHandler() = default;

  typedef MozPromise<dom::Sequence<nsString>, nsresult, true> IceLogPromise;

  virtual void Initialize() {}

  // There's a wrinkle here; the ICE logging is not separated out by
  // MediaTransportHandler. These are a little more like static methods, but
  // to avoid needing yet another IPC interface, we bolt them on here.
  virtual RefPtr<IceLogPromise> GetIceLog(const nsCString& aPattern) = 0;
  virtual void ClearIceLog() = 0;
  virtual void EnterPrivateMode() = 0;
  virtual void ExitPrivateMode() = 0;

  virtual void CreateIceCtx(const std::string& aName) = 0;

  virtual nsresult SetIceConfig(const nsTArray<dom::RTCIceServer>& aIceServers,
                                dom::RTCIceTransportPolicy aIcePolicy) = 0;

  // We will probably be able to move the proxy lookup stuff into
  // this class once we move mtransport to its own process.
  virtual void SetProxyConfig(NrSocketProxyConfig&& aProxyConfig) = 0;

  virtual void EnsureProvisionalTransport(const std::string& aTransportId,
                                          const std::string& aLocalUfrag,
                                          const std::string& aLocalPwd,
                                          int aComponentCount) = 0;

  virtual void SetTargetForDefaultLocalAddressLookup(
      const std::string& aTargetIp, uint16_t aTargetPort) = 0;

  // We set default-route-only as late as possible because it depends on what
  // capture permissions have been granted on the window, which could easily
  // change between Init (ie; when the PC is created) and StartIceGathering
  // (ie; when we set the local description).
  virtual void StartIceGathering(bool aDefaultRouteOnly,
                                 bool aObfuscateHostAddresses,
                                 // TODO: It probably makes sense to look
                                 // this up internally.
                                 const nsTArray<NrIceStunAddr>& aStunAddrs) = 0;

  // Make the pool hold aSize pre-gathered sets of candidates. Called when
  // iceCandidatePoolSize is set on RTCConfiguration before
  // setLocalDescription. A transport created afterwards takes what it needs
  // from the pool instead of gathering, and the candidates surface as that
  // transport's own, so there is nothing to name here: the pool is not
  // addressable.
  // Sets already pre-gathered are kept, per RFC 9429 section 4.1.18: growing
  // gathers only the sets being added, and shrinking releases only the sets
  // beyond aSize.
  virtual void SetCandidatePoolSize(
      uint8_t aSize, bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
      const nsTArray<NrIceStunAddr>& aStunAddrs) = 0;

  // Release whatever is left in the pool. RFC 9429 section 3.5.4 empties the
  // candidate pool after the first offer/answer exchange; until then leftovers
  // keep their sockets (and any TURN allocations) alive.
  virtual void EmptyCandidatePool() = 0;

  virtual void ActivateTransport(
      const std::string& aTransportId, const std::string& aLocalUfrag,
      const std::string& aLocalPwd, size_t aComponentCount,
      const std::string& aUfrag, const std::string& aPassword,
      const nsTArray<uint8_t>& aKeyDer, const nsTArray<uint8_t>& aCertDer,
      SSLKEAType aAuthType, bool aDtlsClient, const DtlsDigestList& aDigests,
      bool aPrivacyRequested) = 0;

  virtual void RemoveTransportsExcept(
      const std::set<std::string>& aTransportIds) = 0;

  virtual void StartIceChecks(bool aIsControlling,
                              const std::vector<std::string>& aIceOptions) = 0;

  virtual void SendPacket(const std::string& aTransportId,
                          MediaPacket&& aPacket) = 0;

  virtual void AddIceCandidate(const std::string& aTransportId,
                               const std::string& aCandidate,
                               const std::string& aUFrag,
                               const std::string& aResolvedAddress) = 0;

  virtual void UpdateNetworkState(bool aOnline) = 0;

  virtual RefPtr<dom::RTCStatsPromise> GetIceStats(
      const std::string& aTransportId, DOMHighResTimeStamp aNow) = 0;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaTransportHandler)

  TransportLayer::State GetState(const std::string& aTransportId,
                                 bool aRtcp) const {
    return mEvents->GetState(aTransportId, aRtcp);
  }

  MediaEventSourceOneCopyPerThread<std::string, MediaPacket>&
  GetRtpPacketReceived() {
    return mEvents->mRtpPacketReceived;
  }

  MediaEventSourceOneCopyPerThread<std::string, MediaPacket>&
  GetSctpPacketReceived() {
    return mEvents->mSctpPacketReceived;
  }

  MediaEventSource<std::string, CandidateInfo>& GetCandidateGathered() {
    return mEvents->mCandidateGathered;
  }

  MediaEventSource<IceCandidateErrorInfo>& GetCandidateError() {
    return mEvents->mCandidateError;
  }

  MediaEventSource<std::string, bool>& GetAlpnNegotiated() {
    return mEvents->mAlpnNegotiated;
  }

  MediaEventSource<std::string, dom::RTCIceGathererState>&
  GetGatheringStateChange() {
    return mEvents->mGatheringStateChange;
  }
  MediaEventSource<std::string, dom::RTCIceTransportState,
                   Maybe<dom::IceCandidateAttributePair>>&
  GetConnectionStateChange() {
    return mEvents->mConnectionStateChange;
  }
  MediaEventSource<std::string, MediaPacket>& GetEncryptedSending() {
    return mEvents->mEncryptedSending;
  }
  MediaEventSource<std::string, TransportLayer::State,
                   nsTArray<nsTArray<uint8_t>>, Maybe<dom::RTCErrorParams>>&
  GetStateChange() {
    return mEvents->mStateChange;
  }
  MediaEventSource<std::string, TransportLayer::State,
                   Maybe<dom::RTCErrorParams>>&
  GetRtcpStateChange() {
    return mEvents->mRtcpStateChange;
  }

 protected:
  void OnCandidate(const std::string& aTransportId,
                   CandidateInfo&& aCandidateInfo) {
    mEvents->OnCandidate(aTransportId, std::move(aCandidateInfo));
  }
  void OnCandidateError(IceCandidateErrorInfo&& aErrorInfo) {
    mEvents->OnCandidateError(std::move(aErrorInfo));
  }
  void OnAlpnNegotiated(const std::string& aAlpn) {
    mEvents->OnAlpnNegotiated(aAlpn);
  }
  void OnGatheringStateChange(const std::string& aTransportId,
                              dom::RTCIceGathererState aState) {
    mEvents->OnGatheringStateChange(aTransportId, aState);
  }
  void OnConnectionStateChange(
      const std::string& aTransportId, dom::RTCIceTransportState aState,
      const Maybe<dom::IceCandidateAttributePair>& aSelectedPair) {
    mEvents->OnConnectionStateChange(aTransportId, aState, aSelectedPair);
  }
  void OnPacketReceived(std::string&& aTransportId, MediaPacket&& aPacket) {
    mEvents->OnPacketReceived(std::move(aTransportId), std::move(aPacket));
  }
  void OnEncryptedSending(const std::string& aTransportId,
                          MediaPacket&& aPacket) {
    mEvents->OnEncryptedSending(aTransportId, std::move(aPacket));
  }
  void OnStateChange(const std::string& aTransportId,
                     TransportLayer::State aState,
                     nsTArray<nsTArray<uint8_t>>&& aRemoteCerts,
                     Maybe<dom::RTCErrorParams> aError = Nothing()) {
    mEvents->OnStateChange(aTransportId, aState, std::move(aRemoteCerts),
                           std::move(aError));
  }
  void OnRtcpStateChange(const std::string& aTransportId,
                         TransportLayer::State aState,
                         Maybe<dom::RTCErrorParams> aError = Nothing()) {
    mEvents->OnRtcpStateChange(aTransportId, aState, std::move(aError));
  }
  virtual ~MediaTransportHandler() = default;

  // This can outlive MediaTransportHandlerLocal, which means
  // MediaTransportImpl does not need to worry about its wrapper being
  // destroyed.
  const RefPtr<MediaTransportEvents> mEvents =
      MakeRefPtr<MediaTransportEvents>();
};

void TokenizeCandidate(const std::string& aCandidate,
                       std::vector<std::string>& aTokens);

}  // namespace mozilla

#endif  // MTRANSPORTHANDLER_H_
