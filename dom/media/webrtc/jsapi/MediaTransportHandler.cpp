/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaTransportHandler.h"

#include "nsITimer.h"
#include "transport/dtlsidentity.h"
#include "transport/nricemediastream.h"
#include "transport/nriceresolver.h"
#include "transport/sigslot.h"
#include "transport/transportflow.h"
#include "transport/transportlayerdtls.h"
#include "transport/transportlayerice.h"
#include "transport/transportlayersrtp.h"

// Config stuff
#include "mozilla/IceServerParser.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/RTCConfigurationBinding.h"

// Logging stuff
#include "common/browser_logging/CSFLog.h"

// For fetching ICE logging
#include "transport/rlogconnector.h"

// DTLS
#include <map>
#include <string>
#include <vector>

#include "common/RandomHex.h"
#include "mozilla/Base64.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/PublicSSL.h"  // For psm::InitializeCipherSuite
#include "mozilla/ReverseIterator.h"
#include "mozilla/StopGapEventTarget.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "nsDNSService2.h"
#include "nsFmtString.h"
#include "nsISocketTransportService.h"
#include "nsSocketTransportService2.h"
#include "nss.h"  // For NSS_NoDB_Init
#include "sdp/SdpAttribute.h"
#include "transport/runnable_utils.h"
#define MEDIA_TRANSPORT_HANDLER_PACKET_RECEIVED(aPacket)              \
  PROFILER_MARKER_TEXT("WebRTC Packet Received", MEDIA_RT, {},        \
                       ProfilerString8View::WrapNullTerminatedString( \
                           MediaPacket::EnumValueToString((aPacket).type())));

namespace mozilla {

static const char* mthLogTag = "MediaTransportHandler";
#ifdef LOGTAG
#  undef LOGTAG
#endif
#define LOGTAG mthLogTag

// How long the previous DTLS association of a fingerprint-changing ICE restart
// is kept for stragglers after the new one opens, unless a packet on the new
// one ends it sooner. Anything later than this is not worth playing out, and a
// missing keyframe self-heals via PLI/FIR.
static constexpr uint32_t kOldFlowGraceMs = 2000;

using ParsedIceServer = IceServerParser::ParsedIceServer;
using IceLogPromise = MediaTransportHandler::IceLogPromise;

// An underlying implementation wrapped by MediaTransportHandlerLocal. Unlike
// MediaTransportHandler, this is not required to be threadsafe or refcounted.
// Everything except Init must be run on the event target Init returns.
class MediaTransportImpl {
 public:
  // MediaTransportEvents is how this will communicate with the wrapping
  // MediaTransportHandlerLocal; this decouples the lifecycles of wrapper/impl.
  explicit MediaTransportImpl(RefPtr<MediaTransportEvents> aEvents)
      : mEvents(aEvents) {}
  virtual ~MediaTransportImpl() = default;

  // Called once, on any thread. Returns the event target every other call must
  // be dispatched to, including our destruction (eg; a no-op runnable carrying
  // a UniquePtr to us). The contract on this event target is as follows:
  // 1 The event target either runs or destroys runnables in the order they
  //   were dispatched. If Dispatch returns an error, this counts as
  //   destruction of the runnable; all previously dispatched runnables must
  //   have already been run, destroyed, or leaked. This is all part of the
  //   contract of nsISerialEventTarget, but it bears repeating here.
  //   This is what makes it safe to carry bareptrs (MediaTransportImpl*) in
  //   these runnables, because the last runnable is the one that destroys us.
  // 2 If this class must be destroyed on the event target, the event target
  //   must be accepting and running runnables. In other words, if the event
  //   target will stop accepting runnables (eg; STS shutdown), the impl must
  //   make itself safe to destroy on any thread *before* that happens.
  // 3 The event target will accept runnables right away, even if it needs to
  //   perform async init before it begins actually running them.
  virtual already_AddRefed<nsISerialEventTarget> Init() = 0;

  virtual void CreateIceCtx(const std::string& aName) = 0;

  virtual RefPtr<IceLogPromise> GetIceLog(const nsCString& aPattern) = 0;
  virtual void ClearIceLog() = 0;
  virtual void EnterPrivateMode() = 0;
  virtual void ExitPrivateMode() = 0;

  virtual void SetIceConfig(nsTArray<ParsedIceServer>&& aIceServers,
                            dom::RTCIceTransportPolicy aIcePolicy) = 0;
  virtual void SetProxyConfig(NrSocketProxyConfig&& aProxyConfig) = 0;
  virtual void EnsureProvisionalTransport(const std::string& aTransportId,
                                          const std::string& aUfrag,
                                          const std::string& aPwd,
                                          int aComponentCount) = 0;
  virtual void SetTargetForDefaultLocalAddressLookup(
      const std::string& aTargetIp, uint16_t aTargetPort) = 0;
  virtual void StartIceGathering(bool aDefaultRouteOnly,
                                 bool aObfuscateHostAddresses,
                                 const nsTArray<NrIceStunAddr>& aStunAddrs) = 0;
  virtual void SetCandidatePoolSize(
      uint8_t aSize, bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
      const nsTArray<NrIceStunAddr>& aStunAddrs) = 0;
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
  virtual void AddIceCandidate(const std::string& aTransportId,
                               const std::string& aCandidate,
                               const std::string& aUfrag,
                               const std::string& aResolvedAddress) = 0;
  virtual void UpdateNetworkState(bool aOnline) = 0;
  virtual void SendPacket(const std::string& aTransportId,
                          MediaPacket&& aPacket) = 0;
  virtual RefPtr<dom::RTCStatsPromise> GetIceStats(
      const std::string& aTransportId, DOMHighResTimeStamp aNow) = 0;

 protected:
  const RefPtr<MediaTransportEvents> mEvents;
};

class STSShutdownHandler;

class MediaTransportImplNicer final : public MediaTransportImpl,
                                      public sigslot::has_slots<> {
 public:
  explicit MediaTransportImplNicer(RefPtr<MediaTransportEvents> aEvents);
  ~MediaTransportImplNicer();

  already_AddRefed<nsISerialEventTarget> Init() override;
  void CreateIceCtx(const std::string& aName) override;

  RefPtr<IceLogPromise> GetIceLog(const nsCString& aPattern) override;
  void ClearIceLog() override;
  void EnterPrivateMode() override;
  void ExitPrivateMode() override;

  void SetIceConfig(nsTArray<ParsedIceServer>&& aIceServers,
                    dom::RTCIceTransportPolicy aIcePolicy) override;

  // We will probably be able to move the proxy lookup stuff into
  // this class once we move mtransport to its own process.
  void SetProxyConfig(NrSocketProxyConfig&& aProxyConfig) override;

  void EnsureProvisionalTransport(const std::string& aTransportId,
                                  const std::string& aUfrag,
                                  const std::string& aPwd,
                                  int aComponentCount) override;

  void SetTargetForDefaultLocalAddressLookup(const std::string& aTargetIp,
                                             uint16_t aTargetPort) override;

  // We set default-route-only as late as possible because it depends on what
  // capture permissions have been granted on the window, which could easily
  // change between Init (ie; when the PC is created) and StartIceGathering
  // (ie; when we set the local description).
  void StartIceGathering(bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
                         // This will go away once mtransport moves to its
                         // own process, because we won't need to get this
                         // via IPC anymore
                         const nsTArray<NrIceStunAddr>& aStunAddrs) override;

  void SetCandidatePoolSize(uint8_t aSize, bool aDefaultRouteOnly,
                            bool aObfuscateHostAddresses,
                            const nsTArray<NrIceStunAddr>& aStunAddrs) override;

  void EmptyCandidatePool() override;

  void ActivateTransport(
      const std::string& aTransportId, const std::string& aLocalUfrag,
      const std::string& aLocalPwd, size_t aComponentCount,
      const std::string& aUfrag, const std::string& aPassword,
      const nsTArray<uint8_t>& aKeyDer, const nsTArray<uint8_t>& aCertDer,
      SSLKEAType aAuthType, bool aDtlsClient, const DtlsDigestList& aDigests,
      bool aPrivacyRequested) override;

  void RemoveTransportsExcept(
      const std::set<std::string>& aTransportIds) override;

  void StartIceChecks(bool aIsControlling,
                      const std::vector<std::string>& aIceOptions) override;

  void AddIceCandidate(const std::string& aTransportId,
                       const std::string& aCandidate, const std::string& aUfrag,
                       const std::string& aResolvedAddress) override;

  void UpdateNetworkState(bool aOnline) override;

  void SendPacket(const std::string& aTransportId,
                  MediaPacket&& aPacket) override;

  RefPtr<dom::RTCStatsPromise> GetIceStats(const std::string& aTransportId,
                                           DOMHighResTimeStamp aNow) override;

  // Releases everything that must be released on STS. Afterwards, this may be
  // destroyed on any thread. Called by STSShutdownHandler, and on destruction.
  // This is how we satisfy item 2 in the contract on Init().
  void DeinitSts();

 private:
  void InitMain(const RefPtr<StopGapEventTarget>& aTarget);
  void InitSts(const RefPtr<STSShutdownHandler>& aHandler,
               const NrIceCtx::GlobalConfig& aConfig,
               const RefPtr<StopGapEventTarget>& aTarget);
  RefPtr<TransportFlow> CreateTransportFlow(
      const std::string& aTransportId, bool aIsRtcp,
      const RefPtr<DtlsIdentity>& aDtlsIdentity, bool aDtlsClient,
      const DtlsDigestList& aDigests, bool aPrivacyRequested);

  // Everything we track per transport id: the flows, the previous
  // association's flows during a fingerprint-changing ICE restart, and what we
  // last reported about them. Lives in mTransports and is used in place, hence
  // not copyable.
  struct Transport {
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    // The close timer's callback holds a plain pointer to us.
    ~Transport() {
      if (mCloseTimer) {
        mCloseTimer->Cancel();
      }
    }

    RefPtr<TransportFlow> GetCurrent(bool aIsRtcp) const {
      return (aIsRtcp && mRtcpFlow) ? mRtcpFlow : mFlow;
    }
    RefPtr<TransportFlow> GetOld(bool aIsRtcp) const {
      return (aIsRtcp && mOldRtcpFlow) ? mOldRtcpFlow : mOldFlow;
    }
    // The flow to send on: the current association once its DTLS is up,
    // otherwise the previous one while it is still operational.
    RefPtr<TransportFlow> GetSendFlow(bool aIsRtcp) const;
    TransportLayer::State CurrentDtlsState(bool aIsRtcp) const;

    // Used for DTLS restart. The current flows become the old flows, to be
    // closed once the new ones are in use.
    void DeprecateCurrentFlows();
    // Closes the old flows and cancels any pending closure.
    void CloseOldFlows();

    struct DtlsState {
      DtlsState() = default;
      DtlsState(DtlsState&&) = default;
      DtlsState& operator=(DtlsState&&) = default;
      // nsTArray doesn't have copy/assignment, `= default` isn't possible.
      DtlsState(const DtlsState& aOther);
      DtlsState& operator=(const DtlsState& aOther);
      bool operator==(const DtlsState& aOther) const = default;

      TransportLayer::State mState = TransportLayer::TS_NONE;
      nsTArray<nsTArray<uint8_t>> mRemoteCerts;
    };
    // Derives and updates the state from the current and old associations.
    // Returns the new state if it changed, otherwise returns Nothing().
    Maybe<DtlsState> UpdateDtlsState(bool aIsRtcp);
    // The negotiated ALPN, if it changed since it was last reported.
    Maybe<std::string> UpdateAlpn();

    RefPtr<TransportFlow> mFlow;
    RefPtr<TransportFlow> mRtcpFlow;
    // These are set during DTLS restart to avoid interrupting media flow.
    RefPtr<TransportFlow> mOldFlow;
    RefPtr<TransportFlow> mOldRtcpFlow;
    // Used by MediaTransportImplNicer to schedule closure of old flows.
    // This class is not very suitable for scheduling these timers itself,
    // because it is not refcounted.
    nsCOMPtr<nsITimer> mCloseTimer;
    // The digests of the current flow. ActivateTransport uses this to detect
    // when a DTLS restart has been requested, and new flows must be created.
    DtlsDigestList mDigests;
    // The remote ICE ufrag of the current flow. ActivateTransport uses this to
    // double-check that content doesn't restart DTLS without restarting ICE.
    std::string mUfrag;

    // Counts of the (decrypted) payload that traverses this transport, used to
    // populate RTCTransportStats. These exclude STUN connectivity checks and
    // DTLS/SRTP protection overhead, both of which are added below this point.
    uint64_t mBytesSent = 0;
    uint64_t mBytesReceived = 0;
    uint64_t mPacketsSent = 0;
    uint64_t mPacketsReceived = 0;
    // Number of times the selected candidate pair has changed (spec:
    // RTCTransportStats.selectedCandidatePairChanges), plus the last selected
    // pair we observed used to detect changes. Maintained in
    // OnConnectionStateChange, and cumulative across the transport's lifetime.
    uint32_t mSelectedCandidatePairChanges = 0;
    std::pair<std::string, std::string> mLastSelectedCandidatePair;
    // The most recent ICE transport state observed in OnConnectionStateChange.
    // GetIceStats reports this rather than deriving from
    // NrIceMediaStream::state(), which has no "new" state.
    dom::RTCIceTransportState mIceState = dom::RTCIceTransportState::New;

   private:
    DtlsState ComputeDtlsState(bool aIsRtcp) const;

    DtlsState mReportedDtlsState;
    DtlsState mReportedRtcpState;
    std::string mReportedAlpn;
  };

  void OnGatheringStateChange(const std::string& aTransportId,
                              NrIceMediaStream::GatheringState aState);
  void OnConnectionStateChange(NrIceMediaStream* aIceStream,
                               NrIceCtx::ConnectionState aState);
  void OnCandidateFound(NrIceMediaStream* aStream,
                        const std::string& aCandidate,
                        const std::string& aUfrag, const std::string& aMDNSAddr,
                        const std::string& aActualAddr);
  void OnCandidateError(NrIceMediaStream* aStream, const std::string& aAddress,
                        uint16_t aPort, const std::string& aUrl,
                        uint16_t aErrorCode, const std::string& aErrorText);
  void OnStateChange(TransportLayer* aLayer, TransportLayer::State);
  void OnRtcpStateChange(TransportLayer* aLayer, TransportLayer::State);
  // Derives the DTLS state (and certs, ALPN, error) to report for a transport
  // from its current (and, during a DTLS restart, old) DTLS association and
  // reports it if it changed. Called on any DTLS state change and whenever a
  // flow is closed.
  void UpdateReportedState(const std::string& aTransportId, bool aIsRtcp);
  // Closes the old DTLS association of a fingerprint-changing ICE restart
  // once the new one is in use: after a grace period from the new association
  // opening, or as soon as a packet arrives on it. The pending closure is
  // Transport::mCloseTimer, which ActivateTransport cancels on a further ICE
  // restart.
  void ScheduleOldFlowClose(const std::string& aTransportId, uint32_t aDelayMs);
  void CloseOldFlows(const std::string& aTransportId);
  void PacketReceived(TransportLayer* aLayer, MediaPacket& aPacket);
  void EncryptedPacketSending(TransportLayer* aLayer, MediaPacket& aPacket);
  RefPtr<TransportFlow> GetTransportFlow(const std::string& aTransportId,
                                         bool aIsRtcp) const;
  void GetIceStats(const NrIceMediaStream& aStream, DOMHighResTimeStamp aNow,
                   dom::RTCStatsCollection* aStats,
                   dom::RTCTransportStats& aTransport) const;

  nsCOMPtr<nsISerialEventTarget> mStsThread;
  // Only set if we registered with it, only touched on STS.
  RefPtr<STSShutdownHandler> mShutdownHandler;
  RefPtr<NrIceCtx> mIceCtx;
  RefPtr<NrIceResolver> mDNSResolver;
  std::map<std::string, Transport> mTransports;
  // We should not need this if STS is keeping its end of the contract around
  // shutdown, but we're going to double-check just in case. If STS has
  // messed up, we'll convert the bug into a safe crash.
  std::atomic<bool> mSTSOnly{false};
  bool mHideLocalPrflx = false;
  bool mTurnDisabled = false;
  uint32_t mMinDtlsVersion = 0;
  uint32_t mMaxDtlsVersion = 0;
  bool mForceNoHost = false;
  bool mAllowLoopback = false;
  bool mAllowLinkLocal = false;
  Maybe<NrIceCtx::NatSimulatorConfig> mNatConfig;

  std::set<std::string> mSignaledAddresses;
};

// Refcounted front for a MediaTransportImpl, usable on any thread. Every call
// is dispatched to the implementation's event target, and so is the
// implementation's destruction, which therefore happens after every call that
// could still act on it.
class MediaTransportHandlerLocal final : public MediaTransportHandler {
 public:
  MediaTransportHandlerLocal()
      : mImpl(std::make_unique<MediaTransportImplNicer>(mEvents)),
        mTarget(mImpl->Init()) {}

  RefPtr<IceLogPromise> GetIceLog(const nsCString& aPattern) override {
    return InvokeAsync(mTarget, __func__, [impl = mImpl.get(), aPattern] {
      return impl->GetIceLog(aPattern);
    });
  }

  void ClearIceLog() override {
    Post(__func__, [](MediaTransportImpl& aImpl) { aImpl.ClearIceLog(); });
  }

  void EnterPrivateMode() override {
    Post(__func__, [](MediaTransportImpl& aImpl) { aImpl.EnterPrivateMode(); });
  }

  void ExitPrivateMode() override {
    Post(__func__, [](MediaTransportImpl& aImpl) { aImpl.ExitPrivateMode(); });
  }

  void CreateIceCtx(const std::string& aName) override {
    Post(__func__,
         [aName](MediaTransportImpl& aImpl) { aImpl.CreateIceCtx(aName); });
  }

  nsresult SetIceConfig(const nsTArray<dom::RTCIceServer>& aIceServers,
                        dom::RTCIceTransportPolicy aIcePolicy) override {
    auto result = IceServerParser::Parse(aIceServers);
    if (result.isErr()) {
      // Discard the detailed ErrorResult; callers at this level use nsresult.
      result.unwrapErr().SuppressException();
      return NS_ERROR_FAILURE;
    }
    Post(__func__, [entries = result.unwrap(),
                    aIcePolicy](MediaTransportImpl& aImpl) mutable {
      aImpl.SetIceConfig(std::move(entries), aIcePolicy);
    });
    return NS_OK;
  }

  void SetProxyConfig(NrSocketProxyConfig&& aProxyConfig) override {
    Post(__func__, [aProxyConfig = std::move(aProxyConfig)](
                       MediaTransportImpl& aImpl) mutable {
      aImpl.SetProxyConfig(std::move(aProxyConfig));
    });
  }

  void EnsureProvisionalTransport(const std::string& aTransportId,
                                  const std::string& aUfrag,
                                  const std::string& aPwd,
                                  int aComponentCount) override {
    Post(__func__, [=](MediaTransportImpl& aImpl) {
      aImpl.EnsureProvisionalTransport(aTransportId, aUfrag, aPwd,
                                       aComponentCount);
    });
  }

  void SetTargetForDefaultLocalAddressLookup(const std::string& aTargetIp,
                                             uint16_t aTargetPort) override {
    Post(__func__, [=](MediaTransportImpl& aImpl) {
      aImpl.SetTargetForDefaultLocalAddressLookup(aTargetIp, aTargetPort);
    });
  }

  void StartIceGathering(bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
                         const nsTArray<NrIceStunAddr>& aStunAddrs) override {
    Post(__func__,
         [=, stunAddrs = aStunAddrs.Clone()](MediaTransportImpl& aImpl) {
           aImpl.StartIceGathering(aDefaultRouteOnly, aObfuscateHostAddresses,
                                   stunAddrs);
         });
  }

  void SetCandidatePoolSize(
      uint8_t aSize, bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
      const nsTArray<NrIceStunAddr>& aStunAddrs) override {
    Post(__func__,
         [=, stunAddrs = aStunAddrs.Clone()](MediaTransportImpl& aImpl) {
           aImpl.SetCandidatePoolSize(aSize, aDefaultRouteOnly,
                                      aObfuscateHostAddresses, stunAddrs);
         });
  }

  void EmptyCandidatePool() override {
    Post(__func__,
         [](MediaTransportImpl& aImpl) { aImpl.EmptyCandidatePool(); });
  }

  void ActivateTransport(
      const std::string& aTransportId, const std::string& aLocalUfrag,
      const std::string& aLocalPwd, size_t aComponentCount,
      const std::string& aUfrag, const std::string& aPassword,
      const nsTArray<uint8_t>& aKeyDer, const nsTArray<uint8_t>& aCertDer,
      SSLKEAType aAuthType, bool aDtlsClient, const DtlsDigestList& aDigests,
      bool aPrivacyRequested) override {
    Post(__func__, [=, keyDer = aKeyDer.Clone(),
                    certDer = aCertDer.Clone()](MediaTransportImpl& aImpl) {
      aImpl.ActivateTransport(aTransportId, aLocalUfrag, aLocalPwd,
                              aComponentCount, aUfrag, aPassword, keyDer,
                              certDer, aAuthType, aDtlsClient, aDigests,
                              aPrivacyRequested);
    });
  }

  void RemoveTransportsExcept(
      const std::set<std::string>& aTransportIds) override {
    Post(__func__, [=](MediaTransportImpl& aImpl) {
      aImpl.RemoveTransportsExcept(aTransportIds);
    });
  }

  void StartIceChecks(bool aIsControlling,
                      const std::vector<std::string>& aIceOptions) override {
    Post(__func__, [=](MediaTransportImpl& aImpl) {
      aImpl.StartIceChecks(aIsControlling, aIceOptions);
    });
  }

  void AddIceCandidate(const std::string& aTransportId,
                       const std::string& aCandidate, const std::string& aUfrag,
                       const std::string& aResolvedAddress) override {
    Post(__func__, [=](MediaTransportImpl& aImpl) {
      aImpl.AddIceCandidate(aTransportId, aCandidate, aUfrag, aResolvedAddress);
    });
  }

  void UpdateNetworkState(bool aOnline) override {
    Post(__func__,
         [=](MediaTransportImpl& aImpl) { aImpl.UpdateNetworkState(aOnline); });
  }

  void SendPacket(const std::string& aTransportId,
                  MediaPacket&& aPacket) override {
    Post(__func__, [aTransportId, aPacket = std::move(aPacket)](
                       MediaTransportImpl& aImpl) mutable {
      aImpl.SendPacket(aTransportId, std::move(aPacket));
    });
  }

  RefPtr<dom::RTCStatsPromise> GetIceStats(const std::string& aTransportId,
                                           DOMHighResTimeStamp aNow) override {
    return InvokeAsync(mTarget, __func__,
                       [impl = mImpl.get(), aTransportId, aNow] {
                         return impl->GetIceStats(aTransportId, aNow);
                       });
  }

 private:
  ~MediaTransportHandlerLocal() {
    // If Dispatch fails, the runnable is synchronously destroyed, taking the
    // impl with it. This is why we need item 2 in the contract on
    // MediaTransportImpl::Init; the impl must become safe to destroy on any
    // thread if the event target will stop accepting runnables.
    (void)mTarget->Dispatch(
        NS_NewRunnableFunction(__func__,
                               [impl = std::move(mImpl)]() mutable {}),
        NS_DISPATCH_FALLIBLE);
  }

  template <typename Function>
  void Post(const char* aName, Function&& aFunction) {
    (void)mTarget->Dispatch(
        NS_NewRunnableFunction(aName,
                               [impl = mImpl.get(),
                                function = std::forward<Function>(
                                    aFunction)]() mutable { function(*impl); }),
        NS_DISPATCH_FALLIBLE);
  }

  // Only released on destruction.
  UniquePtr<MediaTransportImpl> mImpl;
  const nsCOMPtr<nsISerialEventTarget> mTarget;
};

/* static */
already_AddRefed<MediaTransportHandler> MediaTransportHandler::Create() {
  RefPtr<MediaTransportHandler> result =
      MakeRefPtr<MediaTransportHandlerLocal>();
  result->Initialize();
  return result.forget();
}

// Tells every live MediaTransportImplNicer to release what it holds on STS
// when STS shuts down. Observe() is called on main just before the STS thread
// is joined, so the work has to be queued there right away.
class STSShutdownHandler final : public nsISTSShutdownObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  // Lazy instance for the current STS lifetime. Null if STS is shutting down
  // (or shut down); a handler created in such a state would never get a
  // shutdown callback before STS stops working. If STS is restarted (eg; after
  // a profile-change network teardown), we will be able to create a new
  // instance.
  static RefPtr<STSShutdownHandler> GetOrCreate() {
    MOZ_ASSERT(NS_IsMainThread());
    RefPtr<STSShutdownHandler>& instance = Instance();
    if (!instance) {
      // Make sure STS exists and has been brought up.
      nsCOMPtr<nsISocketTransportService> sts =
          do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
      if (sts && net::gSocketTransportService &&
          !net::gSocketTransportService->IsShuttingDownOrShutDown()) {
        instance = MakeRefPtr<STSShutdownHandler>(sts.forget());
      }
    }
    return instance;
  }

  explicit STSShutdownHandler(nsCOMPtr<nsISocketTransportService> aSts)
      : mSts(aSts) {
    CSFLogDebug(LOGTAG, "%s", __func__);
    aSts->AddShutdownObserver(this);
    mStsThread = do_QueryInterface(aSts);
    MOZ_RELEASE_ASSERT(mStsThread);
  }

  NS_IMETHOD Observe() override {
    CSFLogDebug(LOGTAG, "%s", __func__);
    MOZ_ASSERT(NS_IsMainThread());
    // Make all of the impls safe to destroy on any thread before STS stops
    // accepting runnables.
    mStsThread->Dispatch(NS_NewRunnableFunction(
        __func__,
        [self = RefPtr<STSShutdownHandler>(this)] { self->Shutdown(); }));
    mSts->RemoveShutdownObserver(this);
    Instance() = nullptr;
    return NS_OK;
  }

 private:
  static RefPtr<STSShutdownHandler>& Instance() {
    MOZ_ASSERT(NS_IsMainThread());
    static RefPtr<STSShutdownHandler> sHandler;
    return sHandler;
  }

 public:
  void Register(MediaTransportImplNicer* aImpl) {
    MOZ_ASSERT(mStsThread->IsOnCurrentThread());
    mImpls.insert(aImpl);
  }

  void Deregister(MediaTransportImplNicer* aImpl) {
    MOZ_ASSERT(mStsThread->IsOnCurrentThread());
    mImpls.erase(aImpl);
  }

  nsCOMPtr<nsISerialEventTarget> GetStsThread() const { return mStsThread; }

 private:
  virtual ~STSShutdownHandler() = default;

  void Shutdown() {
    MOZ_ASSERT(mStsThread->IsOnCurrentThread());
    std::set<MediaTransportImplNicer*> impls = std::move(mImpls);
    mImpls.clear();
    // Each of these deregisters, which needs mStsThread.
    for (auto* impl : impls) {
      impl->DeinitSts();
    }
    mStsThread = nullptr;
  }

  nsCOMPtr<nsISerialEventTarget> mStsThread;
  nsCOMPtr<nsISocketTransportService> mSts;
  // STS only. Raw ptrs; each deregisters itself when it releases its STS
  // resources.
  std::set<MediaTransportImplNicer*> mImpls;
};

NS_IMPL_ISUPPORTS(STSShutdownHandler, nsISTSShutdownObserver);

MediaTransportImplNicer::MediaTransportImplNicer(
    RefPtr<MediaTransportEvents> aEvents)
    : MediaTransportImpl(std::move(aEvents)) {
  // We do not do substantive init in here, because we could be on any thread
  // (eg; PBackground). That happens in InitMain/InitSts.
  CSFLogDebug(LOGTAG, "%s done %p", __func__, this);
}

MediaTransportImplNicer::~MediaTransportImplNicer() {
  // mStsThread is not set if InitMain never got that far.
  if (mStsThread && mStsThread->IsOnCurrentThread()) {
    DeinitSts();
  } else {
    // We have either shut down already, or never initted.
    MOZ_RELEASE_ASSERT(
        !mSTSOnly,
        "Destructor running on non-STS, but we still have STS-only state!");
  }
}

static NrIceCtx::Policy toNrIcePolicy(dom::RTCIceTransportPolicy aPolicy) {
  switch (aPolicy) {
    case dom::RTCIceTransportPolicy::Relay:
      return NrIceCtx::ICE_POLICY_RELAY;
    case dom::RTCIceTransportPolicy::All:
      return NrIceCtx::ICE_POLICY_ALL;
    default:
      MOZ_CRASH();
  }
  return NrIceCtx::ICE_POLICY_ALL;
}

static NrIceCtx::GlobalConfig GetGlobalConfig() {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled =
      Preferences::GetBool("media.peerconnection.ice.tcp", false);
  config.mStunClientMaxTransmits = Preferences::GetInt(
      "media.peerconnection.ice.stun_client_maximum_transmits",
      config.mStunClientMaxTransmits);
  config.mTrickleIceGracePeriod =
      Preferences::GetInt("media.peerconnection.ice.trickle_grace_period",
                          config.mTrickleIceGracePeriod);
  config.mIceTcpSoSockCount = Preferences::GetInt(
      "media.peerconnection.ice.tcp_so_sock_count", config.mIceTcpSoSockCount);
  config.mIceTcpListenBacklog =
      Preferences::GetInt("media.peerconnection.ice.tcp_listen_backlog",
                          config.mIceTcpListenBacklog);
  (void)Preferences::GetCString("media.peerconnection.ice.force_interface",
                                config.mForceNetInterface);
  return config;
}

static Maybe<NrIceCtx::NatSimulatorConfig> GetNatConfig() {
  bool block_tcp = Preferences::GetBool(
      "media.peerconnection.nat_simulator.block_tcp", false);
  bool block_udp = Preferences::GetBool(
      "media.peerconnection.nat_simulator.block_udp", false);
  bool block_tls = Preferences::GetBool(
      "media.peerconnection.nat_simulator.block_tls", false);
  int error_code_for_drop = Preferences::GetInt(
      "media.peerconnection.nat_simulator.error_code_for_drop", 0);
  nsAutoCString mapping_type;
  (void)Preferences::GetCString(
      "media.peerconnection.nat_simulator.mapping_type", mapping_type);
  nsAutoCString filtering_type;
  (void)Preferences::GetCString(
      "media.peerconnection.nat_simulator.filtering_type", filtering_type);
  nsAutoCString redirect_address;
  (void)Preferences::GetCString(
      "media.peerconnection.nat_simulator.redirect_address", redirect_address);
  nsAutoCString redirect_targets;
  (void)Preferences::GetCString(
      "media.peerconnection.nat_simulator.redirect_targets", redirect_targets);
  int network_delay_ms = Preferences::GetInt(
      "media.peerconnection.nat_simulator.network_delay_ms", 0);

  if (block_udp || block_tcp || block_tls || !mapping_type.IsEmpty() ||
      !filtering_type.IsEmpty() || !redirect_address.IsEmpty()) {
    CSFLogDebug(LOGTAG, "NAT filtering type: %s", filtering_type.get());
    CSFLogDebug(LOGTAG, "NAT mapping type: %s", mapping_type.get());
    CSFLogDebug(LOGTAG, "NAT network delay: %d", network_delay_ms);
    NrIceCtx::NatSimulatorConfig natConfig;
    natConfig.mBlockUdp = block_udp;
    natConfig.mBlockTcp = block_tcp;
    natConfig.mBlockTls = block_tls;
    natConfig.mErrorCodeForDrop = error_code_for_drop;
    natConfig.mFilteringType = std::move(filtering_type);
    natConfig.mMappingType = std::move(mapping_type);
    natConfig.mNetworkDelayMs = network_delay_ms;
    if (redirect_address.Length()) {
      CSFLogDebug(LOGTAG, "Redirect address: %s", redirect_address.get());
      CSFLogDebug(LOGTAG, "Redirect targets: %s", redirect_targets.get());
      natConfig.mRedirectAddress = std::move(redirect_address);
      std::stringstream str(redirect_targets.get());
      std::string target;
      while (getline(str, target, ',')) {
        CSFLogDebug(LOGTAG, "Adding target: %s", target.c_str());
        natConfig.mRedirectTargets.AppendElement(std::move(target));
      }
    }
    return Some(std::move(natConfig));
  }
  return Nothing();
}

already_AddRefed<nsISerialEventTarget> MediaTransportImplNicer::Init() {
  // The StopGapEventTarget accepts runnables right away (item 3 in the
  // contract), and holds them until InitSts succeeds. We do not keep a ref
  // because that would create a reference cycle, but we do hold a ref until we
  // have finished init. If init does not complete, any queued runnables
  // (including our destruction) are dropped along with the StopGapEventTarget
  // when our wrapper goes away; this is fine on any thread because we do not
  // have any STS-only state in that case (item 2 in the contract).
  RefPtr<StopGapEventTarget> target = MakeRefPtr<StopGapEventTarget>();
  GetMainThreadSerialEventTarget()->Dispatch(
      NS_NewRunnableFunction(__func__, [this, target] { InitMain(target); }));
  return target.forget();
}

void MediaTransportImplNicer::InitMain(
    const RefPtr<StopGapEventTarget>& aTarget) {
  MOZ_ASSERT(NS_IsMainThread());
  CSFLogDebug(LOGTAG, "%s starting", __func__);

  // Null if STS is shutting/shut down; there is not a usable event target for
  // STS, don't bother continuing init.
  RefPtr<STSShutdownHandler> shutdownHandler =
      STSShutdownHandler::GetOrCreate();
  if (!shutdownHandler) {
    return;
  }

  mStsThread = shutdownHandler->GetStsThread();
  if (!mStsThread) {
    MOZ_CRASH();
    return;
  }

  if (!NSS_IsInitialized()) {
    if (NSS_NoDB_Init(nullptr) != SECSuccess) {
      MOZ_CRASH();
      return;
    }

    if (NS_FAILED(mozilla::psm::InitializeCipherSuite())) {
      MOZ_CRASH();
      return;
    }

    mozilla::psm::DisableMD5();
  }

  static bool globalInitDone = false;
  if (!globalInitDone) {
    // Ensure the DNS service is initted for the first time on main
    DebugOnly<RefPtr<nsIDNSService>> dnsService =
        RefPtr<nsIDNSService>(nsDNSService::GetXPCOMSingleton());
    MOZ_ASSERT(dnsService.value);
    globalInitDone = true;
  }

  // Give us a way to globally turn off TURN support
  mTurnDisabled =
      Preferences::GetBool("media.peerconnection.turn.disable", false);
  // We are reading these here, because when we setup the DTLS transport
  // we are on the wrong thread to read prefs
  mMinDtlsVersion =
      Preferences::GetUint("media.peerconnection.dtls.version.min");
  mMaxDtlsVersion =
      Preferences::GetUint("media.peerconnection.dtls.version.max");
  mForceNoHost =
      Preferences::GetBool("media.peerconnection.ice.no_host", false);
  mNatConfig = GetNatConfig();
  mAllowLoopback =
      Preferences::GetBool("media.peerconnection.ice.loopback", false);
  mAllowLinkLocal =
      Preferences::GetBool("media.peerconnection.ice.link_local", false);

  // This goes straight to STS. The StopGapEventTarget might already have
  // runnables in it, and they must wait until InitSts has succeeded.
  mStsThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, shutdownHandler = std::move(shutdownHandler),
                 config = GetGlobalConfig(), target = aTarget] {
        InitSts(shutdownHandler, config, target);
      }));
}

void MediaTransportImplNicer::InitSts(
    const RefPtr<STSShutdownHandler>& aHandler,
    const NrIceCtx::GlobalConfig& aConfig,
    const RefPtr<StopGapEventTarget>& aTarget) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  // It is possible that STS shutdown has started, but if so the DeinitSts
  // runnable is already queued. There is no harm in initting/registering in
  // that case, we'll just deinit when DeinitSts runs.
  aHandler->Register(this);
  // This is the first point where we have state that is STS only. We've
  // already checked that STS is not shutting down, and we have registered with
  // the STSShutdownHandler to ensure we get a chance to clean up if STS begins
  // shutdown (item 2 in the contract for our event target).
  mSTSOnly = true;
  NrIceCtx::InitializeGlobals(aConfig);
  mShutdownHandler = aHandler;
  // Everything queued for us goes to STS now.
  aTarget->SetRealEventTarget(mStsThread);
}

void MediaTransportImplNicer::CreateIceCtx(const std::string& aName) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  if (!mShutdownHandler) {
    // STS is shutting down, or already told us to shut down.
    return;
  }

  if (NS_WARN_IF(mIceCtx)) {
    CSFLogError(LOGTAG, "%s: ICE context already created", __func__);
    return;
  }

  mIceCtx = NrIceCtx::Create(aName);
  if (!mIceCtx) {
    CSFLogError(LOGTAG, "%s: NrIceCtx::Create failed", __func__);
    return;
  }

  mIceCtx->SignalConnectionStateChange.connect(
      this, &MediaTransportImplNicer::OnConnectionStateChange);

  mDNSResolver = MakeRefPtr<NrIceResolver>();
  nsresult rv;
  if (NS_FAILED(rv = mDNSResolver->Init())) {
    CSFLogError(LOGTAG, "%s: Failed to initialize dns resolver", __FUNCTION__);
    DeinitSts();
    return;
  }
  if (NS_FAILED(rv = mIceCtx->SetResolver(mDNSResolver->AllocateResolver()))) {
    CSFLogError(LOGTAG, "%s: Failed to get dns resolver", __FUNCTION__);
    DeinitSts();
    return;
  }

  CSFLogDebug(LOGTAG, "%s done", __func__);
}

void MediaTransportImplNicer::SetIceConfig(
    nsTArray<ParsedIceServer>&& aIceServers,
    dom::RTCIceTransportPolicy aIcePolicy) {
  if (!mIceCtx) {
    CSFLogError(LOGTAG, "%s: mIceCtx is null", __FUNCTION__);
    return;
  }
  NrIceCtx::Config config;
  config.mPolicy = toNrIcePolicy(aIcePolicy);
  if (config.mPolicy == NrIceCtx::ICE_POLICY_ALL && mForceNoHost) {
    config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  }

  config.mAllowLoopback = mAllowLoopback;
  config.mAllowLinkLocal = mAllowLinkLocal;
  config.mNatSimulatorConfig = mNatConfig;

  nsresult rv;

  if (NS_FAILED(rv = mIceCtx->SetIceServers(aIceServers, mTurnDisabled))) {
    CSFLogError(LOGTAG, "%s: Failed to set ICE servers", __FUNCTION__);
    return;
  }
  if (NS_FAILED(rv = mIceCtx->SetIceConfig(config))) {
    CSFLogError(LOGTAG, "%s: Failed to set config", __FUNCTION__);
  }
}

void MediaTransportImplNicer::DeinitSts() {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());

  if (mShutdownHandler) {
    mShutdownHandler->Deregister(this);
    mShutdownHandler = nullptr;
  }
  // Clear the transports before destroying the ice ctx so that
  // the close_notify alerts have a chance to be sent as the
  // TransportFlow destructors execute.
  mTransports.clear();
  if (mIceCtx) {
    CSFLogDebug(LOGTAG, "%s", __func__);
    NrIceStats stats = mIceCtx->Destroy();
    CSFLogDebug(LOGTAG,
                "Ice Telemetry: stun (retransmits: %d)"
                "   turn (401s: %d   403s: %d   438s: %d)",
                stats.stun_retransmits, stats.turn_401s, stats.turn_403s,
                stats.turn_438s);
  }
  mIceCtx = nullptr;
  mDNSResolver = nullptr;
  // We are now safe to destroy on any thread.
  mSTSOnly = false;
}

void MediaTransportImplNicer::SetProxyConfig(
    NrSocketProxyConfig&& aProxyConfig) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  mIceCtx->SetProxyConfig(std::move(aProxyConfig));
}

void MediaTransportImplNicer::EnsureProvisionalTransport(
    const std::string& aTransportId, const std::string& aUfrag,
    const std::string& aPwd, int aComponentCount) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  RefPtr<NrIceMediaStream> stream(mIceCtx->GetStream(aTransportId));
  if (!stream) {
    CSFLogDebug(LOGTAG, "%s: Creating ICE media stream=%s components=%d",
                mIceCtx->name().c_str(), aTransportId.c_str(), aComponentCount);

    std::ostringstream os;
    os << mIceCtx->name() << " transport-id=" << aTransportId;
    stream = mIceCtx->CreateStream(aTransportId, os.str(), aComponentCount);

    if (!stream) {
      CSFLogError(LOGTAG, "Failed to create ICE stream.");
      return;
    }

    stream->SignalCandidate.connect(this,
                                    &MediaTransportImplNicer::OnCandidateFound);
    stream->SignalCandidateError.connect(
        this, &MediaTransportImplNicer::OnCandidateError);
    stream->SignalGatheringStateChange.connect(
        this, &MediaTransportImplNicer::OnGatheringStateChange);
  }

  // Begins an ICE restart if this stream has a different ufrag/pwd
  stream->SetIceCredentials(aUfrag, aPwd);

  // Make sure there's an entry in mTransports
  mTransports[aTransportId];
}

void MediaTransportImplNicer::ActivateTransport(
    const std::string& aTransportId, const std::string& aLocalUfrag,
    const std::string& aLocalPwd, size_t aComponentCount,
    const std::string& aUfrag, const std::string& aPassword,
    const nsTArray<uint8_t>& aKeyDer, const nsTArray<uint8_t>& aCertDer,
    SSLKEAType aAuthType, bool aDtlsClient, const DtlsDigestList& aDigests,
    bool aPrivacyRequested) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  MOZ_ASSERT(aComponentCount);
  RefPtr<DtlsIdentity> dtlsIdentity(
      DtlsIdentity::Deserialize(aKeyDer, aCertDer, aAuthType));
  if (!dtlsIdentity) {
    MOZ_ASSERT(false);
    return;
  }

  RefPtr<NrIceMediaStream> stream(mIceCtx->GetStream(aTransportId));
  if (!stream) {
    MOZ_ASSERT(false);
    return;
  }

  CSFLogDebug(LOGTAG, "%s: Activating ICE media stream=%s components=%u",
              mIceCtx->name().c_str(), aTransportId.c_str(),
              static_cast<unsigned>(aComponentCount));

  std::vector<std::string> attrs;
  attrs.reserve(2 /* ufrag + pwd */);
  attrs.push_back("ice-ufrag:" + aUfrag);
  attrs.push_back("ice-pwd:" + aPassword);

  // If we started an ICE restart in EnsureProvisionalTransport, this is
  // where we decide whether to commit or rollback.
  nsresult rv = stream->ConnectToPeer(aLocalUfrag, aLocalPwd, attrs);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "Couldn't parse ICE attributes, rv=%u",
                static_cast<unsigned>(rv));
    MOZ_ASSERT(false);
    return;
  }

  Transport& transport = mTransports[aTransportId];

  if (transport.mFlow) {
    // Pre-existing transport
    if (transport.mUfrag != aUfrag) {
      // An ICE restart. nICEr keeps at most two ICE streams, so the one
      // a lingering old flow was bound to has just been closed.
      transport.CloseOldFlows();
      if (transport.mDigests != aDigests) {
        // Stand up a new, parallel DTLS association over the new ICE
        // credentials and let the old flow linger to catch stragglers.
        stream->AdvanceDtlsId();
        transport.DeprecateCurrentFlows();
      }
    } else if (transport.mDigests != aDigests) {
      // Content process checks this, but we check here too in case the
      // content process has gone off the rails.
      CSFLogError(LOGTAG,
                  "%s: Ignoring remote DTLS fingerprint change without "
                  "an ICE restart on transport %s",
                  mIceCtx->name().c_str(), aTransportId.c_str());
    }
  }
  transport.mUfrag = aUfrag;

  if (!transport.mFlow) {
    transport.mFlow =
        CreateTransportFlow(aTransportId, false, dtlsIdentity, aDtlsClient,
                            aDigests, aPrivacyRequested);
    if (!transport.mFlow) {
      return;
    }
    transport.mDigests = aDigests;
    TransportLayer* dtls = transport.mFlow->GetLayer(TransportLayerDtls::ID());
    dtls->SignalStateChange.connect(this,
                                    &MediaTransportImplNicer::OnStateChange);
    if (aComponentCount < 2) {
      dtls->SignalStateChange.connect(
          this, &MediaTransportImplNicer::OnRtcpStateChange);
    }
  }

  if (aComponentCount == 2) {
    if (!transport.mRtcpFlow) {
      transport.mRtcpFlow =
          CreateTransportFlow(aTransportId, true, dtlsIdentity, aDtlsClient,
                              aDigests, aPrivacyRequested);
      if (!transport.mRtcpFlow) {
        return;
      }
      TransportLayer* dtls =
          transport.mRtcpFlow->GetLayer(TransportLayerDtls::ID());
      dtls->SignalStateChange.connect(
          this, &MediaTransportImplNicer::OnRtcpStateChange);
    }
  } else {
    transport.mRtcpFlow = nullptr;
    // components are 1-indexed
    stream->DisableComponent(2);
  }

  UpdateReportedState(aTransportId, /* aIsRtcp = */ false);
  UpdateReportedState(aTransportId, /* aIsRtcp = */ true);
}

void MediaTransportImplNicer::SetTargetForDefaultLocalAddressLookup(
    const std::string& aTargetIp, uint16_t aTargetPort) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  mIceCtx->SetTargetForDefaultLocalAddressLookup(aTargetIp, aTargetPort);
}

void MediaTransportImplNicer::StartIceGathering(
    bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
    const nsTArray<NrIceStunAddr>& aStunAddrs) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  mHideLocalPrflx = aObfuscateHostAddresses;

  // Belt and suspenders - in e10s mode, the call below to SetStunAddrs
  // needs to have the proper flags set on ice ctx.  For non-e10s,
  // setting those flags happens in StartGathering.  We could probably
  // just set them here, and only do it here.
  mIceCtx->SetCtxFlags(aDefaultRouteOnly);

  if (aStunAddrs.Length()) {
    mIceCtx->SetStunAddrs(aStunAddrs);
  }

  // Start gathering, but only if there are streams
  if (!mIceCtx->GetStreams().empty()) {
    mIceCtx->StartGathering(aDefaultRouteOnly, aObfuscateHostAddresses);
  }
}

void MediaTransportImplNicer::SetCandidatePoolSize(
    uint8_t aSize, bool aDefaultRouteOnly, bool aObfuscateHostAddresses,
    const nsTArray<NrIceStunAddr>& aStunAddrs) {
  if (!mIceCtx) {
    return;
  }

  mHideLocalPrflx = aObfuscateHostAddresses;
  mIceCtx->SetGatheringFlags(aDefaultRouteOnly, aObfuscateHostAddresses);

  if (aStunAddrs.Length()) {
    mIceCtx->SetStunAddrs(aStunAddrs);
  }

  // The pool gathers when it is asked for rather than waiting for the
  // gather that setting a local description kicks off, which is the
  // point of asking for it this early.
  // Nothing knows yet which transport will use what the pool gathers, so it
  // gathers under placeholder credentials until a component takes it and is
  // re-keyed. They are random, like the ones JSEP generates, rather than fixed
  // so that they are not guessable.
  mIceCtx->ResizeCandidatePool(aSize, GetRandomHex(1), GetRandomHex(4));
}

void MediaTransportImplNicer::EmptyCandidatePool() {
  if (!mIceCtx) {
    return;
  }
  mIceCtx->EmptyCandidatePool();
}

void MediaTransportImplNicer::StartIceChecks(
    bool aIsControlling, const std::vector<std::string>& aIceOptions) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  nsresult rv = mIceCtx->ParseGlobalAttributes(aIceOptions);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: couldn't parse global parameters", __FUNCTION__);
    return;
  }

  rv = mIceCtx->SetControlling(aIsControlling ? NrIceCtx::ICE_CONTROLLING
                                              : NrIceCtx::ICE_CONTROLLED);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: couldn't set controlling to %d", __FUNCTION__,
                aIsControlling);
    return;
  }

  rv = mIceCtx->StartChecks();
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: couldn't start checks", __FUNCTION__);
    return;
  }
}

void TokenizeCandidate(const std::string& aCandidate,
                       std::vector<std::string>& aTokens) {
  aTokens.clear();

  std::istringstream iss(aCandidate);
  std::string token;
  while (std::getline(iss, token, ' ')) {
    aTokens.push_back(token);
  }
}

void MediaTransportImplNicer::AddIceCandidate(
    const std::string& aTransportId, const std::string& aCandidate,
    const std::string& aUfrag, const std::string& aResolvedAddress) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  RefPtr<NrIceMediaStream> stream(mIceCtx->GetStream(aTransportId));
  if (!stream) {
    CSFLogError(LOGTAG, "No ICE stream for candidate with transport id %s: %s",
                aTransportId.c_str(), aCandidate.c_str());
    return;
  }

  // Re-parsing this is kinda silly. We probably want to have
  // ParseTrickleCandidate actually give us a parsed representation.
  std::vector<std::string> tokens;
  TokenizeCandidate(aCandidate, tokens);
  if (tokens.size() > 4) {
    mSignaledAddresses.insert(tokens[4]);
  }

  nsresult rv =
      stream->ParseTrickleCandidate(aCandidate, aUfrag, aResolvedAddress);
  if (!NS_SUCCEEDED(rv)) {
    CSFLogError(LOGTAG,
                "Couldn't process ICE candidate with transport id %s: "
                "%s",
                aTransportId.c_str(), aCandidate.c_str());
  }
}

void MediaTransportImplNicer::UpdateNetworkState(bool aOnline) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  mIceCtx->UpdateNetworkState(aOnline);
}

void MediaTransportImplNicer::RemoveTransportsExcept(
    const std::set<std::string>& aTransportIds) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  for (auto it = mTransports.begin(); it != mTransports.end();) {
    const std::string transportId(it->first);
    if (!aTransportIds.count(transportId)) {
      mEvents->OnStateChange(transportId, TransportLayer::TS_CLOSED, {});
      mEvents->OnRtcpStateChange(transportId, TransportLayer::TS_CLOSED);
      // Erase the transport before destroying the ice stream so that
      // the close_notify alerts have a chance to be sent as the
      // TransportFlow destructors execute.
      it = mTransports.erase(it);
      // We're already on the STS thread, but the TransportFlow
      // destructor executed when mTransports.erase(it) is called
      // above dispatches the call to DestroyFinal to the STS thread. If
      // we don't also dispatch the call to destroy the NrIceMediaStream
      // to the STS thread, it will tear down the NrIceMediaStream
      // before the TransportFlow is destroyed.  Without a valid
      // NrIceMediaStream the close_notify alert cannot be sent.
      mStsThread->Dispatch(NS_NewRunnableFunction(
          __func__, [iceCtx = RefPtr<NrIceCtx>(mIceCtx), transportId] {
            iceCtx->DestroyStream(transportId);
          }));
    } else {
      MOZ_ASSERT(it->second.mFlow);
      ++it;
    }
  }
}

void MediaTransportImplNicer::SendPacket(const std::string& aTransportId,
                                         MediaPacket&& aPacket) {
  if (!mIceCtx) {
    return;  // Probably due to XPCOM shutdown
  }

  MOZ_ASSERT(aPacket.type() != MediaPacket::UNCLASSIFIED);
  RefPtr<TransportFlow> flow =
      GetTransportFlow(aTransportId, aPacket.type() == MediaPacket::RTCP);

  if (!flow) {
    CSFLogError(LOGTAG, "%s: No such transport flow (%s) for outgoing packet",
                mIceCtx->name().c_str(), aTransportId.c_str());
    return;
  }

  TransportLayer* layer = nullptr;
  switch (aPacket.type()) {
    case MediaPacket::SCTP:
      layer = flow->GetLayer(TransportLayerDtls::ID());
      break;
    case MediaPacket::RTP:
    case MediaPacket::RTCP:
      layer = flow->GetLayer(TransportLayerSrtp::ID());
      break;
    default:
      // Maybe it would be useful to allow the injection of other packet
      // types for testing?
      MOZ_ASSERT(false);
      return;
  }

  MOZ_ASSERT(layer);

  if (int error = layer->SendPacket(aPacket); error < 0) {
    CSFLogError(LOGTAG,
                "%s: Transport flow (%s) failed to send packet. error=%d",
                mIceCtx->name().c_str(), aTransportId.c_str(), error);
  } else if (auto it = mTransports.find(aTransportId);
             it != mTransports.end()) {
    // On success the layer returns the number of (unencrypted) payload
    // bytes it was handed.
    it->second.mBytesSent += error;
    it->second.mPacketsSent += 1;
  }
}

TransportLayer::State MediaTransportEvents::GetState(
    const std::string& aTransportId, bool aRtcp) const {
  MutexAutoLock lock(mStateCacheMutex);
  const std::map<std::string, TransportLayer::State>* cache = nullptr;
  if (aRtcp) {
    cache = &mRtcpStateCache;
  } else {
    cache = &mStateCache;
  }

  auto it = cache->find(aTransportId);
  if (it != cache->end()) {
    return it->second;
  }
  return TransportLayer::TS_NONE;
}

void MediaTransportEvents::OnCandidate(const std::string& aTransportId,
                                       CandidateInfo&& aCandidateInfo) {
  mCandidateGathered.Notify(aTransportId, std::move(aCandidateInfo));
}

void MediaTransportEvents::OnCandidateError(
    IceCandidateErrorInfo&& aErrorInfo) {
  mCandidateError.Notify(std::move(aErrorInfo));
}

void MediaTransportEvents::OnAlpnNegotiated(const std::string& aAlpn) {
  const bool privacyRequested = aAlpn == "c-webrtc";
  mAlpnNegotiated.Notify(aAlpn, privacyRequested);
}

void MediaTransportEvents::OnGatheringStateChange(
    const std::string& aTransportId, dom::RTCIceGathererState aState) {
  mGatheringStateChange.Notify(aTransportId, aState);
}

void MediaTransportEvents::OnConnectionStateChange(
    const std::string& aTransportId, dom::RTCIceTransportState aState,
    const Maybe<dom::IceCandidateAttributePair>& aSelectedPair) {
  mConnectionStateChange.Notify(aTransportId, aState, aSelectedPair);
}

void MediaTransportEvents::OnPacketReceived(std::string&& aTransportId,
                                            MediaPacket&& aPacket) {
  switch (aPacket.type()) {
    case MediaPacket::UNCLASSIFIED:
    case MediaPacket::DTLS:
    case MediaPacket::SRTP:
    case MediaPacket::SRTCP:
      // Shouldn't happen, and nothing would care if it did
      break;
    case MediaPacket::RTP:
    case MediaPacket::RTCP:
      mRtpPacketReceived.Notify(std::move(aTransportId), std::move(aPacket));
      break;
    case MediaPacket::SCTP:
      mSctpPacketReceived.Notify(std::move(aTransportId), std::move(aPacket));
      break;
  }
}

void MediaTransportEvents::OnEncryptedSending(const std::string& aTransportId,
                                              MediaPacket&& aPacket) {
  mEncryptedSending.Notify(aTransportId, std::move(aPacket));
}

void MediaTransportEvents::OnStateChange(
    const std::string& aTransportId, TransportLayer::State aState,
    nsTArray<nsTArray<uint8_t>>&& aRemoteCerts,
    Maybe<dom::RTCErrorParams> aError) {
  {
    MutexAutoLock lock(mStateCacheMutex);
    mStateCache[aTransportId] = aState;
  }
  mStateChange.Notify(aTransportId, aState, std::move(aRemoteCerts), aError);
}

void MediaTransportEvents::OnRtcpStateChange(
    const std::string& aTransportId, TransportLayer::State aState,
    Maybe<dom::RTCErrorParams> aError) {
  {
    MutexAutoLock lock(mStateCacheMutex);
    mRtcpStateCache[aTransportId] = aState;
  }
  mRtcpStateChange.Notify(aTransportId, aState, aError);
}

static uint16_t ToDtlsWireVersion(uint16_t aProtocolVersion) {
  switch (aProtocolVersion) {
    case SSL_LIBRARY_VERSION_DTLS_1_0:
      return SSL_LIBRARY_VERSION_DTLS_1_0_WIRE;
    case SSL_LIBRARY_VERSION_DTLS_1_2:
      return SSL_LIBRARY_VERSION_DTLS_1_2_WIRE;
    case SSL_LIBRARY_VERSION_DTLS_1_3:
      return SSL_LIBRARY_VERSION_DTLS_1_3_WIRE;
    default:
      return 0;
  }
}

// BuildCertificateStats returns the issuerCertificateId.
// https://w3c.github.io/webrtc-stats/#dom-rtccertificatestats-issuercertificateid
// "The issuerCertificateId refers to the stats object that contains the next
// certificate in the certificate chain."
static nsString BuildCertificateStats(const nsTArray<uint8_t>& aDerCert,
                                      const nsAString& aIssuerId,
                                      DOMHighResTimeStamp aNow,
                                      dom::RTCStatsCollection* aStats) {
  if (aDerCert.IsEmpty()) {
    return nsString();
  }

  DtlsDigest digest(DtlsIdentity::DEFAULT_HASH_ALGORITHM);
  if (NS_FAILED(DtlsIdentity::ComputeFingerprint(aDerCert.Elements(),
                                                 aDerCert.Length(), &digest))) {
    return nsString();
  }
  NS_ConvertUTF8toUTF16 fingerprint(
      SdpFingerprintAttributeList::FormatFingerprint(digest.value_).c_str());

  nsFmtString id(u"certificate_{}", fingerprint);

  for (const auto& existing : aStats->mCertificateStats) {
    if (existing.mId.WasPassed() && existing.mId.Value() == id) {
      return id;
    }
  }

  nsCString base64Cert;
  if (NS_FAILED(Base64Encode(reinterpret_cast<const char*>(aDerCert.Elements()),
                             aDerCert.Length(), base64Cert))) {
    return nsString();
  }

  dom::RTCCertificateStats cert;
  cert.mId.Construct(id);
  cert.mTimestamp.Construct(aNow);
  cert.mType.Construct(dom::RTCStatsType::Certificate);
  cert.mFingerprint = fingerprint;
  cert.mFingerprintAlgorithm = NS_ConvertUTF8toUTF16(digest.algorithm_);
  cert.mBase64Certificate = NS_ConvertUTF8toUTF16(base64Cert);
  if (!aIssuerId.IsEmpty()) {
    cert.mIssuerCertificateId.Construct(aIssuerId);
  }

  if (!aStats->mCertificateStats.AppendElement(cert, fallible)) {
    mozalloc_handle_oom(0);
  }
  return id;
}

RefPtr<dom::RTCStatsPromise> MediaTransportImplNicer::GetIceStats(
    const std::string& aTransportId, DOMHighResTimeStamp aNow) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  auto stats = std::make_unique<dom::RTCStatsCollection>();
  if (mIceCtx) {
    dom::RTCIceRole iceRole =
        mIceCtx->GetControlling() == NrIceCtx::ICE_CONTROLLING
            ? dom::RTCIceRole::Controlling
            : dom::RTCIceRole::Controlled;
    for (const auto& stream : mIceCtx->GetStreams()) {
      if (aTransportId.empty() || aTransportId == stream->GetId()) {
        dom::RTCTransportStats transport;
        transport.mId.Construct(
            NS_ConvertASCIItoUTF16(stream->GetId().c_str()));
        transport.mTimestamp.Construct(aNow);
        transport.mType.Construct(dom::RTCStatsType::Transport);
        transport.mIceRole.Construct(iceRole);
        std::string ufrag = stream->GetUfrag();
        if (!ufrag.empty()) {
          transport.mIceLocalUsernameFragment.Construct(
              NS_ConvertASCIItoUTF16(ufrag.c_str()));
        }
        auto transportIt = mTransports.find(stream->GetId());
        // Report the ICE transport state captured from connection-state
        // changes, which distinguishes "new" (no connectivity checks yet)
        // from "checking"; NrIceMediaStream::state() has no "new" state.
        // This also keeps the stat consistent with RTCIceTransport.state.
        transport.mIceState.Construct(transportIt != mTransports.end()
                                          ? transportIt->second.mIceState
                                          : dom::RTCIceTransportState::New);
        // XXX(Bug 1225723) Determine if dtlsState should be `required`.
        transport.mDtlsState = dom::RTCDtlsTransportState::New;
        // The DTLS role is not known until it has been negotiated (via
        // a=setup) and a DTLS transport exists. Until then, report
        // "unknown" rather than leaving the member unset. This is
        // overridden below once the DTLS transport is available.
        transport.mDtlsRole.Construct(dom::RTCDtlsRole::Unknown);
        if (transportIt != mTransports.end() && transportIt->second.mFlow) {
          if (auto* dtlsLayer = static_cast<TransportLayerDtls*>(
                  transportIt->second.mFlow->GetLayer(
                      TransportLayerDtls::ID()))) {
            transport.mDtlsRole.Reset();
            transport.mDtlsRole.Construct(dtlsLayer->role() ==
                                                  TransportLayerDtls::CLIENT
                                              ? dom::RTCDtlsRole::Client
                                              : dom::RTCDtlsRole::Server);
            switch (dtlsLayer->state()) {
              case TransportLayer::TS_NONE:
              case TransportLayer::TS_INIT:
                transport.mDtlsState = dom::RTCDtlsTransportState::New;
                break;
              case TransportLayer::TS_CONNECTING:
                transport.mDtlsState = dom::RTCDtlsTransportState::Connecting;
                break;
              case TransportLayer::TS_OPEN:
                transport.mDtlsState = dom::RTCDtlsTransportState::Connected;
                break;
              case TransportLayer::TS_CLOSED:
                transport.mDtlsState = dom::RTCDtlsTransportState::Closed;
                break;
              case TransportLayer::TS_ERROR:
                transport.mDtlsState = dom::RTCDtlsTransportState::Failed;
                break;
            }
            uint16_t srtpCipher = 0;
            if (NS_SUCCEEDED(dtlsLayer->GetSrtpCipher(&srtpCipher))) {
              const char* name =
                  TransportLayerDtls::GetSrtpCipherName(srtpCipher);
              if (name) {
                transport.mSrtpCipher.Construct(NS_ConvertASCIItoUTF16(name));
              }
            }
            SSLChannelInfo channelInfo;
            if (NS_SUCCEEDED(dtlsLayer->GetChannelInfo(&channelInfo))) {
              if (uint16_t v = ToDtlsWireVersion(channelInfo.protocolVersion)) {
                transport.mTlsVersion.Construct(nsFmtString(u"{:04X}", v));
              }
              SSLCipherSuiteInfo info;
              if (SSL_GetCipherSuiteInfo(channelInfo.cipherSuite, &info,
                                         sizeof(info)) == SECSuccess &&
                  info.cipherSuiteName) {
                transport.mDtlsCipher.Construct(
                    NS_ConvertASCIItoUTF16(info.cipherSuiteName));
              }
            }

            if (dtlsLayer->state() == TransportLayer::TS_OPEN) {
              {
                nsString localId = BuildCertificateStats(
                    dtlsLayer->GetLocalCertDer(), u""_ns, aNow, stats.get());
                if (!localId.IsEmpty()) {
                  transport.mLocalCertificateId.Construct(localId);
                }
              }

              {
                nsTArray<nsTArray<uint8_t>> remoteChain =
                    dtlsLayer->GetPeerCertChainDer();
                nsString issuerId;
                // The chain is leaf-first. Here we start with the root;
                // for the root the issuerId is empty. In WebRTC the chain
                // often consists of a single certificate, i.e. it is
                // self-signed:
                // https://w3c.github.io/webrtc-stats/#dom-rtccertificatestats-issuercertificateid
                // "If the current certificate is at the end of the chain
                // (i.e. a self-signed certificate), this will not be
                // set."
                for (const auto& der : Reversed(remoteChain)) {
                  issuerId =
                      BuildCertificateStats(der, issuerId, aNow, stats.get());
                }
                // Having walked the chain root-first, issuerId now holds
                // the leaf certificate's id.
                if (!issuerId.IsEmpty()) {
                  transport.mRemoteCertificateId.Construct(issuerId);
                }
              }
            }
          }
          transport.mBytesSent.Construct(transportIt->second.mBytesSent);
          transport.mBytesReceived.Construct(
              transportIt->second.mBytesReceived);
          transport.mPacketsSent.Construct(transportIt->second.mPacketsSent);
          transport.mPacketsReceived.Construct(
              transportIt->second.mPacketsReceived);
        }
        transport.mSelectedCandidatePairChanges.Construct(
            transportIt != mTransports.end()
                ? transportIt->second.mSelectedCandidatePairChanges
                : 0);
        // XXX(Bug 2037532) Fill missing fields on the transport.
        GetIceStats(*stream, aNow, stats.get(), transport);

        // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which
        // might involve multiple reallocations) and potentially crashing
        // here, SetCapacity could be called outside the loop once.
        if (!stats->mTransportStats.AppendElement(transport, fallible)) {
          mozalloc_handle_oom(0);
        }
      }
    }
  }
  return dom::RTCStatsPromise::CreateAndResolve(std::move(stats), __func__);
}

RefPtr<IceLogPromise> MediaTransportImplNicer::GetIceLog(
    const nsCString& aPattern) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  dom::Sequence<nsString> converted;
  RLogConnector* logs = RLogConnector::GetInstance();
  std::deque<std::string> result;
  // Might not exist yet.
  if (logs) {
    logs->Filter(aPattern.get(), 0, &result);
  }
  /// XXX(Bug 1631386) Check if we should reject the promise instead of
  /// crashing in an OOM situation.
  if (!converted.SetCapacity(result.size(), fallible)) {
    mozalloc_handle_oom(sizeof(nsString) * result.size());
  }
  for (auto& line : result) {
    // Cannot fail, SetCapacity was called before.
    (void)converted.AppendElement(NS_ConvertUTF8toUTF16(line.c_str()),
                                  fallible);
  }
  return IceLogPromise::CreateAndResolve(std::move(converted), __func__);
}

void MediaTransportImplNicer::ClearIceLog() {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  RLogConnector* logs = RLogConnector::GetInstance();
  if (logs) {
    logs->Clear();
  }
}

void MediaTransportImplNicer::EnterPrivateMode() {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  auto* log = RLogConnector::GetInstance();
  MOZ_ASSERT(log);
  if (log) {
    log->EnterPrivateMode();
  }
}

void MediaTransportImplNicer::ExitPrivateMode() {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  auto* log = RLogConnector::GetInstance();
  MOZ_ASSERT(log);
  if (log) {
    log->ExitPrivateMode();
  }
}

static void ToRTCIceCandidateStats(
    const std::vector<NrIceCandidate>& candidates,
    dom::RTCStatsType candidateType, const nsString& transportId,
    DOMHighResTimeStamp now, dom::RTCStatsCollection* stats,
    bool hideLocalPrflx, const std::set<std::string>& signaledAddresses) {
  MOZ_ASSERT(stats);
  for (const auto& candidate : candidates) {
    dom::RTCIceCandidateStats cand;
    auto hideAddress = [&cand]() {
      cand.mAddress.Construct();
      cand.mAddress.Value().SetIsVoid(true);
    };
    cand.mType.Construct(candidateType);
    NS_ConvertASCIItoUTF16 codeword(candidate.codeword.c_str());
    cand.mTransportId = transportId;
    cand.mId.Construct(codeword);
    cand.mTimestamp.Construct(now);
    cand.mCandidateType.Construct(dom::RTCIceCandidateType(candidate.type));
    cand.mPriority.Construct(candidate.priority);
    // https://tools.ietf.org/html/draft-ietf-rtcweb-mdns-ice-candidates-03#section-3.3.1
    // This obfuscates the address with the mDNS address if one exists
    if (!candidate.domain_name.empty()) {
      // Stats must contain either the host that appeared in the candidate, or
      // nothing at all. It is never valid to put a resolved IP address in this
      // field. If `domain_name` is set, that is what was in the original
      // candidate, and it is also safe to expose regardless of any of the
      // stuff checked below.
      cand.mAddress.Construct(
          NS_ConvertASCIItoUTF16(candidate.domain_name.c_str()));
    } else if (candidateType == dom::RTCStatsType::Remote_candidate &&
               signaledAddresses.find(candidate.cand_addr.host) ==
                   signaledAddresses.end()) {
      // The address of remote candidates is hidden if it has never been passed
      // to us from content. In practice this only happens with prflx.
      hideAddress();
    } else if (candidateType == dom::RTCStatsType::Local_candidate &&
               hideLocalPrflx &&
               candidate.type == NrIceCandidate::ICE_PEER_REFLEXIVE) {
      // A local prflx candidate is our address as some peer saw it, and that
      // peer may be another RTCPeerConnection in the same document. Our host
      // candidates always carry a name when we are hiding addresses, and srflx
      // is fine because the STUN/TURN server the origin supplied has already
      // seen our packets.
      hideAddress();
    } else {
      // If `domain_name` is not set, this will be an IP address.
      cand.mAddress.Construct(
          NS_ConvertASCIItoUTF16(candidate.cand_addr.host.c_str()));
    }

    cand.mPort.Construct(candidate.cand_addr.port);
    cand.mProtocol.Construct(
        NS_ConvertASCIItoUTF16(candidate.cand_addr.transport.c_str()));
    if (candidateType == dom::RTCStatsType::Local_candidate &&
        dom::RTCIceCandidateType(candidate.type) ==
            dom::RTCIceCandidateType::Relay) {
      cand.mRelayProtocol.Construct(
          NS_ConvertASCIItoUTF16(candidate.local_addr.transport.c_str()));
    }
    cand.mUsernameFragment.Construct(
        NS_ConvertASCIItoUTF16(candidate.username_fragment.c_str()));
    // Foundation is not set for peer-reflexive candidates.
    if (candidate.type != NrIceCandidate::ICE_PEER_REFLEXIVE) {
      cand.mFoundation.Construct(
          NS_ConvertASCIItoUTF16(candidate.foundation.c_str()));
    }
    if (candidate.tcp_type == NrIceCandidate::ICE_ACTIVE) {
      cand.mTcpType.Construct(dom::RTCIceTcpCandidateType::Active);
    } else if (candidate.tcp_type == NrIceCandidate::ICE_PASSIVE) {
      cand.mTcpType.Construct(dom::RTCIceTcpCandidateType::Passive);
    }
    cand.mProxied.Construct(NS_ConvertASCIItoUTF16(
        candidate.is_proxied ? "proxied" : "non-proxied"));
    if (!stats->mIceCandidateStats.AppendElement(cand, fallible)) {
      // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which might
      // involve multiple reallocations) and potentially crashing here,
      // SetCapacity could be called outside the loop once.
      mozalloc_handle_oom(0);
    }
    if (candidate.trickled) {
      if (!stats->mTrickledIceCandidateStats.AppendElement(cand, fallible)) {
        mozalloc_handle_oom(0);
      }
    }
  }
}

void MediaTransportImplNicer::GetIceStats(
    const NrIceMediaStream& aStream, DOMHighResTimeStamp aNow,
    dom::RTCStatsCollection* aStats, dom::RTCTransportStats& aTransport) const {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());

  NS_ConvertASCIItoUTF16 transportId(aStream.GetId().c_str());

  std::vector<NrIceCandidatePair> candPairs;
  nsresult res = aStream.GetCandidatePairs(&candPairs);
  if (NS_FAILED(res)) {
    CSFLogError(LOGTAG,
                "%s: Error getting candidate pairs for transport id \"%s\"",
                __FUNCTION__, aStream.GetId().c_str());
    return;
  }

  for (auto& candPair : candPairs) {
    NS_ConvertASCIItoUTF16 codeword(candPair.codeword.c_str());
    NS_ConvertASCIItoUTF16 localCodeword(candPair.local.codeword.c_str());
    NS_ConvertASCIItoUTF16 remoteCodeword(candPair.remote.codeword.c_str());
    // Only expose candidate-pair statistics to chrome, until we've thought
    // through the implications of exposing it to content.

    dom::RTCIceCandidatePairStats s;
    s.mId.Construct(codeword);
    s.mTransportId = transportId;
    s.mTimestamp.Construct(aNow);
    s.mType.Construct(dom::RTCStatsType::Candidate_pair);
    s.mLocalCandidateId.Construct(localCodeword);
    s.mRemoteCandidateId.Construct(remoteCodeword);
    s.mNominated.Construct(candPair.nominated);
    s.mWritable.Construct(candPair.writable);
    s.mReadable.Construct(candPair.readable);
    s.mPriority.Construct(candPair.priority);
    s.mSelected.Construct(candPair.selected);
    s.mBytesSent.Construct(candPair.bytes_sent);
    s.mBytesReceived.Construct(candPair.bytes_recvd);
    s.mPacketsSent.Construct(candPair.packets_sent);
    s.mPacketsReceived.Construct(candPair.packets_recvd);
    s.mLastPacketSentTimestamp.Construct(candPair.ms_since_last_send);
    s.mLastPacketReceivedTimestamp.Construct(candPair.ms_since_last_recv);
    s.mState.Construct(dom::RTCStatsIceCandidatePairState(candPair.state));
    s.mResponsesReceived.Construct(candPair.responses_recvd);
    s.mCurrentRoundTripTime.Construct(candPair.current_rtt_ms / 1000.0);
    s.mTotalRoundTripTime.Construct(candPair.total_rtt_ms / 1000.0);
    s.mComponentId.Construct(candPair.component_id);
    if (candPair.selected && candPair.component_id == 1) {
      aTransport.mSelectedCandidatePairId.Construct(codeword);
    }
    if (!aStats->mIceCandidatePairStats.AppendElement(s, fallible)) {
      // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which might
      // involve multiple reallocations) and potentially crashing here,
      // SetCapacity could be called outside the loop once.
      mozalloc_handle_oom(0);
    }
  }

  std::vector<NrIceCandidate> candidates;
  if (NS_SUCCEEDED(aStream.GetLocalCandidates(&candidates))) {
    ToRTCIceCandidateStats(candidates, dom::RTCStatsType::Local_candidate,
                           transportId, aNow, aStats, mHideLocalPrflx,
                           std::set<std::string>());
    // add the local candidates unparsed string to a sequence
    for (const auto& candidate : candidates) {
      if (!aStats->mRawLocalCandidates.AppendElement(
              NS_ConvertASCIItoUTF16(candidate.label.c_str()), fallible)) {
        // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which might
        // involve multiple reallocations) and potentially crashing here,
        // SetCapacity could be called outside the loop once.
        mozalloc_handle_oom(0);
      }
    }
  }
  candidates.clear();

  if (NS_SUCCEEDED(aStream.GetRemoteCandidates(&candidates))) {
    // Remote addresses are hidden unless content gave them to us, regardless
    // of whether we are hiding our own.
    ToRTCIceCandidateStats(candidates, dom::RTCStatsType::Remote_candidate,
                           transportId, aNow, aStats, /*hideLocalPrflx=*/false,
                           mSignaledAddresses);
    // add the remote candidates unparsed string to a sequence
    for (const auto& candidate : candidates) {
      if (!aStats->mRawRemoteCandidates.AppendElement(
              NS_ConvertASCIItoUTF16(candidate.label.c_str()), fallible)) {
        // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which might
        // involve multiple reallocations) and potentially crashing here,
        // SetCapacity could be called outside the loop once.
        mozalloc_handle_oom(0);
      }
    }
  }
}

static TransportLayerDtls* GetDtlsLayer(const RefPtr<TransportFlow>& aFlow) {
  return aFlow ? static_cast<TransportLayerDtls*>(
                     aFlow->GetLayer(TransportLayerDtls::ID()))
               : nullptr;
}

static TransportLayer::State GetDtlsState(const RefPtr<TransportFlow>& aFlow) {
  TransportLayerDtls* dtls = GetDtlsLayer(aFlow);
  return dtls ? dtls->state() : TransportLayer::TS_NONE;
}

RefPtr<TransportFlow> MediaTransportImplNicer::Transport::GetSendFlow(
    bool aIsRtcp) const {
  RefPtr<TransportFlow> current = GetCurrent(aIsRtcp);
  RefPtr<TransportFlow> old = GetOld(aIsRtcp);
  if (GetDtlsState(current) == TransportLayer::TS_OPEN) {
    return current;
  }
  if (GetDtlsState(old) == TransportLayer::TS_OPEN) {
    return old;
  }
  return current ? current : old;
}

TransportLayer::State MediaTransportImplNicer::Transport::CurrentDtlsState(
    bool aIsRtcp) const {
  return GetDtlsState(GetCurrent(aIsRtcp));
}

void MediaTransportImplNicer::Transport::DeprecateCurrentFlows() {
  mOldFlow = std::move(mFlow);
  mOldRtcpFlow = std::move(mRtcpFlow);
}

void MediaTransportImplNicer::Transport::CloseOldFlows() {
  if (mCloseTimer) {
    mCloseTimer->Cancel();
    mCloseTimer = nullptr;
  }
  mOldFlow = nullptr;
  mOldRtcpFlow = nullptr;
}

MediaTransportImplNicer::Transport::DtlsState
MediaTransportImplNicer::Transport::ComputeDtlsState(bool aIsRtcp) const {
  DtlsState result;
  result.mState = CurrentDtlsState(aIsRtcp);
  const bool currentOpen = result.mState == TransportLayer::TS_OPEN;
  const bool currentDead = result.mState == TransportLayer::TS_ERROR ||
                           result.mState == TransportLayer::TS_CLOSED;
  const bool oldOpen = GetDtlsState(GetOld(aIsRtcp)) == TransportLayer::TS_OPEN;

  // Report the current association's state. The one exception: while the
  // old association is still carrying media, a new one that is not yet up
  // is reported as connected, much like ICE is during an ICE restart.
  if (!currentOpen && !currentDead && oldOpen) {
    result.mState = TransportLayer::TS_OPEN;
  }

  if (result.mState == TransportLayer::TS_OPEN && !aIsRtcp) {
    // Don't surface a new fingerprint before the new association is up.
    TransportLayerDtls* connected =
        GetDtlsLayer(currentOpen ? GetCurrent(aIsRtcp) : GetOld(aIsRtcp));
    if (NS_WARN_IF(!connected)) {
      MOZ_ASSERT(false);
    } else {
      result.mRemoteCerts = connected->GetPeerCertChainDer();
    }
  }
  return result;
}

Maybe<MediaTransportImplNicer::Transport::DtlsState>
MediaTransportImplNicer::Transport::UpdateDtlsState(bool aIsRtcp) {
  DtlsState newState = ComputeDtlsState(aIsRtcp);
  DtlsState& currentState = aIsRtcp ? mReportedRtcpState : mReportedDtlsState;
  if (newState == currentState) {
    return Nothing();
  }
  currentState = newState;
  return Some(std::move(newState));
}

MediaTransportImplNicer::Transport::DtlsState::DtlsState(
    const DtlsState& aOther)
    : mState(aOther.mState) {
  // Not only does nsTArray not have copy/assignment, Clone() doesn't work on
  // nested nsTArray either, because Clone() tries to copy elements. :(
  for (const auto& cert : aOther.mRemoteCerts) {
    mRemoteCerts.AppendElement(cert.Clone());
  }
}

MediaTransportImplNicer::Transport::DtlsState&
MediaTransportImplNicer::Transport::DtlsState::operator=(
    const DtlsState& aOther) {
  DtlsState copy(aOther);
  return *this = std::move(copy);
}

Maybe<std::string> MediaTransportImplNicer::Transport::UpdateAlpn() {
  TransportLayerDtls* connected = GetDtlsLayer(GetSendFlow(false));
  if (!connected || connected->state() != TransportLayer::TS_OPEN) {
    return Nothing();
  }
  std::string alpn = connected->GetNegotiatedAlpn();
  if (alpn.empty() || alpn == mReportedAlpn) {
    return Nothing();
  }
  mReportedAlpn = alpn;
  return Some(std::move(alpn));
}

RefPtr<TransportFlow> MediaTransportImplNicer::GetTransportFlow(
    const std::string& aTransportId, bool aIsRtcp) const {
  auto it = mTransports.find(aTransportId);
  if (it == mTransports.end()) {
    return nullptr;
  }
  return it->second.GetSendFlow(aIsRtcp);
}

RefPtr<TransportFlow> MediaTransportImplNicer::CreateTransportFlow(
    const std::string& aTransportId, bool aIsRtcp,
    const RefPtr<DtlsIdentity>& aDtlsIdentity, bool aDtlsClient,
    const DtlsDigestList& aDigests, bool aPrivacyRequested) {
  nsresult rv;
  RefPtr flow = MakeRefPtr<TransportFlow>(aTransportId);

  // The media streams are made on STS so we need to defer setup.
  auto ice = std::make_unique<TransportLayerIce>();
  auto dtls = std::make_unique<TransportLayerDtls>();
  auto srtp = std::make_unique<TransportLayerSrtp>(*dtls);
  dtls->SetRole(aDtlsClient ? TransportLayerDtls::CLIENT
                            : TransportLayerDtls::SERVER);

  dtls->SetIdentity(aDtlsIdentity);

  dtls->SetMinMaxVersion(
      static_cast<TransportLayerDtls::Version>(mMinDtlsVersion),
      static_cast<TransportLayerDtls::Version>(mMaxDtlsVersion));

  for (const auto& digest : aDigests) {
    rv = dtls->SetVerificationDigest(digest);
    if (NS_FAILED(rv)) {
      CSFLogError(LOGTAG, "Could not set fingerprint");
      return nullptr;
    }
  }

  std::vector<uint16_t> srtpCiphers =
      TransportLayerDtls::GetDefaultSrtpCiphers();

  rv = dtls->SetSrtpCiphers(srtpCiphers);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "Couldn't set SRTP ciphers");
    return nullptr;
  }

  // Always permits negotiation of the confidential mode.
  // Only allow non-confidential (which is an allowed default),
  // if we aren't confidential.
  std::set<std::string> alpn = {"c-webrtc"};
  std::string alpnDefault;
  if (!aPrivacyRequested) {
    alpnDefault = "webrtc";
    alpn.insert(alpnDefault);
  }
  rv = dtls->SetAlpn(alpn, alpnDefault);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "Couldn't set ALPN");
    return nullptr;
  }

  ice->SetParameters(mIceCtx->GetStream(aTransportId), aIsRtcp ? 2 : 1);
  NS_ENSURE_SUCCESS(ice->Init(), nullptr);
  NS_ENSURE_SUCCESS(dtls->Init(), nullptr);
  NS_ENSURE_SUCCESS(srtp->Init(), nullptr);
  dtls->Chain(ice.get());
  srtp->Chain(ice.get());

  dtls->SignalPacketReceived.connect(this,
                                     &MediaTransportImplNicer::PacketReceived);
  srtp->SignalPacketReceived.connect(this,
                                     &MediaTransportImplNicer::PacketReceived);
  ice->SignalPacketSending.connect(
      this, &MediaTransportImplNicer::EncryptedPacketSending);
  flow->PushLayer(ice.release());
  flow->PushLayer(dtls.release());
  flow->PushLayer(srtp.release());
  return flow;
}

static mozilla::dom::RTCIceGathererState toDomIceGathererState(
    NrIceMediaStream::GatheringState aState) {
  switch (aState) {
    case NrIceMediaStream::ICE_STREAM_GATHER_INIT:
      return dom::RTCIceGathererState::New;
    case NrIceMediaStream::ICE_STREAM_GATHER_STARTED:
      return dom::RTCIceGathererState::Gathering;
    case NrIceMediaStream::ICE_STREAM_GATHER_COMPLETE:
      return dom::RTCIceGathererState::Complete;
  }
  MOZ_CRASH();
}

void MediaTransportImplNicer::OnGatheringStateChange(
    const std::string& aTransportId, NrIceMediaStream::GatheringState aState) {
  mEvents->OnGatheringStateChange(aTransportId, toDomIceGathererState(aState));
}

static mozilla::dom::RTCIceTransportState toDomIceTransportState(
    NrIceCtx::ConnectionState aState) {
  switch (aState) {
    case NrIceCtx::ICE_CTX_INIT:
      return dom::RTCIceTransportState::New;
    case NrIceCtx::ICE_CTX_CHECKING:
      return dom::RTCIceTransportState::Checking;
    case NrIceCtx::ICE_CTX_CONNECTED:
      return dom::RTCIceTransportState::Connected;
    case NrIceCtx::ICE_CTX_COMPLETED:
      return dom::RTCIceTransportState::Completed;
    case NrIceCtx::ICE_CTX_FAILED:
      return dom::RTCIceTransportState::Failed;
    case NrIceCtx::ICE_CTX_DISCONNECTED:
      return dom::RTCIceTransportState::Disconnected;
    case NrIceCtx::ICE_CTX_CLOSED:
      return dom::RTCIceTransportState::Closed;
  }
  MOZ_CRASH();
}

void MediaTransportImplNicer::OnConnectionStateChange(
    NrIceMediaStream* aIceStream, NrIceCtx::ConnectionState aState) {
  // Capture the currently-selected pair (if any) at the same time the state
  // change is observed, so the spec's unified "change the selected candidate
  // pair and state" algorithm can run with both bits of information
  // atomically.
  Maybe<dom::IceCandidateAttributePair> selectedPair;
  std::string localAttr;
  std::string remoteAttr;
  // Only get the pair for the RTP component (1). webrtc-pc has no way of
  // surfacing a separate pair for RTCP when rtcp-mux is not in use.
  if (NS_SUCCEEDED(
          aIceStream->GetActivePairAsAttributes(1, &localAttr, &remoteAttr))) {
    selectedPair = Some(dom::IceCandidateAttributePair{nsCString(localAttr),
                                                       nsCString(remoteAttr)});
  }
  if (auto it = mTransports.find(aIceStream->GetId());
      it != mTransports.end()) {
    it->second.mIceState = toDomIceTransportState(aState);
    auto newPair = std::make_pair(localAttr, remoteAttr);
    if (newPair != it->second.mLastSelectedCandidatePair) {
      it->second.mSelectedCandidatePairChanges += 1;
      it->second.mLastSelectedCandidatePair = std::move(newPair);
    }
  }
  mEvents->OnConnectionStateChange(
      aIceStream->GetId(), toDomIceTransportState(aState), selectedPair);
}

void MediaTransportImplNicer::OnCandidateFound(NrIceMediaStream* aStream,
                                               const std::string& aCandidate,
                                               const std::string& aUfrag,
                                               const std::string& aMDNSAddr,
                                               const std::string& aActualAddr) {
  CandidateInfo info;
  info.mCandidate = aCandidate;
  MOZ_ASSERT(!aUfrag.empty());
  info.mUfrag = aUfrag;
  NrIceCandidate defaultRtpCandidate;
  NrIceCandidate defaultRtcpCandidate;
  nsresult rv = aStream->GetDefaultCandidate(1, &defaultRtpCandidate);
  if (NS_SUCCEEDED(rv)) {
    if (!defaultRtpCandidate.domain_name.empty()) {
      info.mDefaultHostRtp = "0.0.0.0";
      info.mDefaultPortRtp = 9;
    } else {
      info.mDefaultHostRtp = defaultRtpCandidate.cand_addr.host;
      info.mDefaultPortRtp = defaultRtpCandidate.cand_addr.port;
    }
  } else {
    CSFLogError(LOGTAG,
                "%s: GetDefaultCandidates failed for transport id %s, "
                "res=%u",
                __FUNCTION__, aStream->GetId().c_str(),
                static_cast<unsigned>(rv));
  }

  // Optional; component won't exist if doing rtcp-mux
  if (NS_SUCCEEDED(aStream->GetDefaultCandidate(2, &defaultRtcpCandidate))) {
    if (!defaultRtcpCandidate.domain_name.empty()) {
      info.mDefaultHostRtcp = "0.0.0.0";
      info.mDefaultPortRtcp = 9;
    } else {
      info.mDefaultHostRtcp = defaultRtcpCandidate.cand_addr.host;
      info.mDefaultPortRtcp = defaultRtcpCandidate.cand_addr.port;
    }
  }

  info.mMDNSAddress = aMDNSAddr;
  info.mActualAddress = aActualAddr;

  mEvents->OnCandidate(aStream->GetId(), std::move(info));
}

void MediaTransportImplNicer::OnCandidateError(NrIceMediaStream* aStream,
                                               const std::string& aAddress,
                                               uint16_t aPort,
                                               const std::string& aUrl,
                                               uint16_t aErrorCode,
                                               const std::string& aErrorText) {
  IceCandidateErrorInfo info;
  info.mAddress = aAddress;
  info.mPort = aPort;
  info.mUrl = aUrl;
  info.mErrorCode = aErrorCode;
  info.mErrorText = aErrorText;
  mEvents->OnCandidateError(std::move(info));
}

// Returns Nothing unless the error originated in DTLS itself; an error in the
// layer below us (ie; ICE) is not a DTLS error, and webrtc-pc does not want us
// to fire an error event on RTCDtlsTransport for it.
Maybe<dom::RTCErrorParams> GetErrorInfo(const TransportLayerDtls& aDtlsLayer) {
  dom::RTCErrorInit error;
  if (aDtlsLayer.HasFingerprintError()) {
    // We might have sent an alert for this, but webrtc-pc says sendAlert is
    // only set when the error detail is "dtls-failure".
    error.mErrorDetail = dom::RTCErrorDetailType::Fingerprint_failure;
  } else if (aDtlsLayer.HasDtlsFailureError()) {
    error.mErrorDetail = dom::RTCErrorDetailType::Dtls_failure;
    // Spec says these cannot be set in the "fingerprint-failure" case
    aDtlsLayer.GetSentAlert().apply(
        [&](auto value) { error.mSentAlert.Construct(value); });
    aDtlsLayer.GetReceivedAlert().apply(
        [&](auto value) { error.mReceivedAlert.Construct(value); });
  } else {
    return Nothing();
  }

  return Some(
      dom::RTCErrorParams{std::move(error), aDtlsLayer.GetErrorDescription()});
}

void MediaTransportImplNicer::OnStateChange(TransportLayer* aLayer,
                                            TransportLayer::State) {
  MOZ_ASSERT(aLayer->id() == TransportLayerDtls::ID());
  UpdateReportedState(aLayer->flow_id(), /* aIsRtcp = */ false);
}

void MediaTransportImplNicer::OnRtcpStateChange(TransportLayer* aLayer,
                                                TransportLayer::State) {
  MOZ_ASSERT(aLayer->id() == TransportLayerDtls::ID());
  UpdateReportedState(aLayer->flow_id(), /* aIsRtcp = */ true);
}

void MediaTransportImplNicer::UpdateReportedState(
    const std::string& aTransportId, bool aIsRtcp) {
  auto it = mTransports.find(aTransportId);
  if (it == mTransports.end()) {
    return;
  }
  Transport& transport = it->second;

  if (!aIsRtcp && transport.mOldFlow) {
    switch (transport.CurrentDtlsState(aIsRtcp)) {
      case TransportLayer::TS_OPEN:
        ScheduleOldFlowClose(aTransportId, kOldFlowGraceMs);
        break;
      case TransportLayer::TS_ERROR:
      case TransportLayer::TS_CLOSED:
        // RFC 8842 section 5.5 notes "be prepared to receive data on both the
        // new and old DTLS associations as long as both are alive". Only the
        // old association is alive now, so we stop using it instead of sending
        // to it indefinitely. Content sees the failure and can restart again.
        ScheduleOldFlowClose(aTransportId, 0);
        // Do not fire the error/closure event just yet, otherwise packet
        // listeners might see packets *after* that event. We update the state
        // when the old flow is actually closed.
        return;
      case TransportLayer::TS_NONE:
      case TransportLayer::TS_INIT:
      case TransportLayer::TS_CONNECTING:
        break;
    }
  }

  if (!aIsRtcp) {
    if (Maybe<std::string> alpn = transport.UpdateAlpn()) {
      mEvents->OnAlpnNegotiated(*alpn);
    }
  }

  Maybe<Transport::DtlsState> newState = transport.UpdateDtlsState(aIsRtcp);
  if (!newState) {
    return;
  }

  Maybe<dom::RTCErrorParams> error;
  if (newState->mState == TransportLayer::TS_ERROR) {
    // Only the current association's failure is ever reported.
    auto dtlsLayer = GetDtlsLayer(transport.GetCurrent(aIsRtcp));
    if (NS_WARN_IF(!dtlsLayer)) {
      MOZ_ASSERT(false);
    } else {
      error = GetErrorInfo(*dtlsLayer);
    }
  }

  if (aIsRtcp) {
    mEvents->OnRtcpStateChange(aTransportId, newState->mState,
                               std::move(error));
  } else {
    mEvents->OnStateChange(aTransportId, newState->mState,
                           std::move(newState->mRemoteCerts), std::move(error));
  }
}

void MediaTransportImplNicer::ScheduleOldFlowClose(
    const std::string& aTransportId, uint32_t aDelayMs) {
  auto it = mTransports.find(aTransportId);
  if (it == mTransports.end() || !it->second.mOldFlow) {
    return;
  }
  Transport& transport = it->second;
  if (transport.mCloseTimer) {
    // Close is already scheduled...
    if (aDelayMs) {
      // ...but we don't bother adjusting the duration, unless...
      return;
    }
    // ...we've been asked to close immediately. Hurry things along.
    transport.mCloseTimer->Cancel();
  }
  NS_NewTimerWithCallback(
      getter_AddRefs(transport.mCloseTimer),
      // Transport cancels this on destruction.
      [this, aTransportId](nsITimer*) { CloseOldFlows(aTransportId); },
      aDelayMs, nsITimer::TYPE_ONE_SHOT,
      "MediaTransportImplNicer::CloseOldFlows"_ns, mStsThread);
}

void MediaTransportImplNicer::CloseOldFlows(const std::string& aTransportId) {
  auto it = mTransports.find(aTransportId);
  if (it == mTransports.end() || !it->second.mOldFlow) {
    return;
  }
  CSFLogInfo(LOGTAG, "Closing old DTLS association on transport %s",
             aTransportId.c_str());
  it->second.CloseOldFlows();
  if (mIceCtx) {
    if (RefPtr<NrIceMediaStream> stream = mIceCtx->GetStream(aTransportId)) {
      stream->CloseOldStream();
    }
  }
  UpdateReportedState(aTransportId, /* aIsRtcp = */ false);
  UpdateReportedState(aTransportId, /* aIsRtcp = */ true);
}

void MediaTransportImplNicer::PacketReceived(TransportLayer* aLayer,
                                             MediaPacket& aPacket) {
  MEDIA_TRANSPORT_HANDLER_PACKET_RECEIVED(aPacket);
  if (auto it = mTransports.find(aLayer->flow_id()); it != mTransports.end()) {
    Transport& transport = it->second;
    transport.mBytesReceived += aPacket.len();
    transport.mPacketsReceived += 1;
    if (transport.mOldFlow && transport.mFlow &&
        transport.mFlow->GetLayer(aLayer->id()) == aLayer) {
      // The peer is sending on the new association.
      ScheduleOldFlowClose(std::string(aLayer->flow_id()), 0);
    }
  }
  mEvents->OnPacketReceived(std::string(aLayer->flow_id()), std::move(aPacket));
}

void MediaTransportImplNicer::EncryptedPacketSending(TransportLayer* aLayer,
                                                     MediaPacket& aPacket) {
  mEvents->OnEncryptedSending(aLayer->flow_id(), std::move(aPacket));
}

}  // namespace mozilla

#undef MEDIA_TRANSPORT_HANDLER_PACKET_RECEIVED
