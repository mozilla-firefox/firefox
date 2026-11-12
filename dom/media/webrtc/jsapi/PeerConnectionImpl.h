/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PEER_CONNECTION_IMPL_H_
#define PEER_CONNECTION_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include "mozilla/Attributes.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIThread.h"
#include "nsIUUIDGenerator.h"
#include "nsPIDOMWindow.h"
#include "nsTHashSet.h"
#include "prlock.h"

// Work around nasty macro in webrtc/voice_engine/voice_engine_defines.h
#ifdef GetLastError
#  undef GetLastError
#endif

#include "DefaultCodecPreferences.h"
#include "MediaEventSource.h"
#include "MediaTransportHandler.h"
#include "PrincipalChangeObserver.h"
#include "RTCDtlsTransport.h"
#include "RTCRtpTransceiver.h"
#include "RTCStatsIdGenerator.h"
#include "RTCStatsReport.h"
#include "VideoSegment.h"
#include "VideoUtils.h"
#include "jsapi/PacketDumper.h"
#include "jsep/JsepSession.h"
#include "jsep/JsepSessionImpl.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/IceServerParser.h"
#include "mozilla/PeerIdentity.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/PeerConnectionImplBinding.h"  // ChainedOperation
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/RTCConfigurationBinding.h"
#include "mozilla/dom/RTCPeerConnectionBinding.h"  // mozPacketDumpType, maybe move?
#include "mozilla/dom/RTCRtpCapabilitiesBinding.h"
#include "mozilla/dom/RTCRtpTransceiverBinding.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/net/DataChannel.h"
#include "mozilla/net/StunAddrsRequestChild.h"
#include "nsIHttpChannelInternal.h"
#include "sdp/SdpMediaSection.h"

namespace test {
#ifdef USE_FAKE_PCOBSERVER
class AFakePCObserver;
#endif
}  // namespace test

class nsIPrincipal;

namespace mozilla {
struct CandidateInfo;
class DataChannel;
class DtlsIdentity;
class MediaPipeline;
class MediaPipelineReceive;
class MediaPipelineTransmit;
enum class PrincipalPrivacy : uint8_t;
class SharedWebrtcState;

namespace dom {
class RTCCertificate;
struct RTCConfiguration;
class RTCDataChannel;
struct RTCRtpSourceEntry;
struct RTCIceServer;
struct RTCOfferOptions;
struct RTCRtpParameters;
class RTCRtpSender;
class MediaStreamTrack;

#ifdef USE_FAKE_PCOBSERVER
typedef test::AFakePCObserver PeerConnectionObserver;
typedef const char* PCObserverString;
#else
class PeerConnectionObserver;
typedef NS_ConvertUTF8toUTF16 PCObserverString;
#endif
}  // namespace dom
}  // namespace mozilla

typedef struct Timecard Timecard;

// To preserve blame, convert nsresult to ErrorResult with wrappers. These
// macros help declare wrappers w/function being wrapped when there are no
// differences.

#define NS_IMETHODIMP_TO_ERRORRESULT(func, rv, ...) \
  NS_IMETHODIMP func(__VA_ARGS__);                  \
  void func(__VA_ARGS__, rv)

#define NS_IMETHODIMP_TO_ERRORRESULT_RETREF(resulttype, func, rv, ...) \
  NS_IMETHODIMP func(__VA_ARGS__, resulttype** result);                \
  already_AddRefed<resulttype> func(__VA_ARGS__, rv)

namespace mozilla {

using dom::PeerConnectionObserver;
using dom::RTCConfiguration;
using dom::RTCDataChannel;
using dom::RTCIceServer;
using dom::RTCOfferOptions;

class PeerConnectionWrapper;
class RemoteSourceStreamInfo;

// Uuid Generator
class PCUuidGenerator : public JsepUuidGenerator {
 public:
  virtual bool Generate(std::string* idp) override;
  virtual UniquePtr<JsepUuidGenerator> Clone() const override {
    return std::make_unique<PCUuidGenerator>(*this);
  }

 private:
  nsCOMPtr<nsIUUIDGenerator> mGenerator;
};

// This is a variation of Telemetry::AutoTimer that keeps a reference
// count and records the elapsed time when the count falls to zero. The
// elapsed time is recorded in seconds.
struct PeerConnectionAutoTimer {
  PeerConnectionAutoTimer()
      : mRefCnt(0), mStart(TimeStamp::Now()), mUsedAV(false) {};
  void RegisterConnection();
  void UnregisterConnection(bool aContainedAV);
  bool IsStopped();

 private:
  int64_t mRefCnt;
  TimeStamp mStart;
  bool mUsedAV;
};

// Enter an API call and check that the state is OK,
// the PC isn't closed, etc.
#define PC_AUTO_ENTER_API_CALL(assert_ice_ready)             \
  do {                                                       \
    /* do/while prevents res from conflicting with locals */ \
    nsresult res = CheckApiState(assert_ice_ready);          \
    if (NS_FAILED(res)) return res;                          \
  } while (0)
#define PC_AUTO_ENTER_API_CALL_VOID_RETURN(assert_ice_ready) \
  do {                                                       \
    /* do/while prevents res from conflicting with locals */ \
    nsresult res = CheckApiState(assert_ice_ready);          \
    if (NS_FAILED(res)) return;                              \
  } while (0)
#define PC_AUTO_ENTER_API_CALL_NO_CHECK() CheckThread()

class PeerConnectionImpl final
    : public nsISupports,
      public nsWrapperCache,
      public DataChannelConnection::DataConnectionListener {
  struct Internal;  // Avoid exposing c includes to bindings

 public:
  explicit PeerConnectionImpl(const dom::GlobalObject* aGlobal = nullptr);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(PeerConnectionImpl)

  struct RtpExtensionHeader {
    JsepMediaType mMediaType;
    SdpDirectionAttribute::Direction direction;
    nsCString extensionname;
  };

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;
  nsPIDOMWindowInner* GetParentObject() const;

  static already_AddRefed<PeerConnectionImpl> Constructor(
      const dom::GlobalObject& aGlobal);

  // DataConnection observers
  void NotifyDataChannel(already_AddRefed<DataChannel> aChannel,
                         const nsACString& aLabel, bool aOrdered,
                         dom::Nullable<uint16_t> aMaxLifeTime,
                         dom::Nullable<uint16_t> aMaxRetransmits,
                         const nsACString& aProtocol, bool aNegotiated)
      // PeerConnectionImpl only inherits from DataChannelConnection
      // inside libxul.
      override;

  void NotifyDataChannelOpen(DataChannel*) override;

  void NotifyDataChannelClosed(DataChannel*) override;

  void NotifySctpConnected(Maybe<uint16_t> aMaxChannels) override;

  void NotifySctpClosed() override;

  const RefPtr<MediaTransportHandler> GetTransportHandler() const;

  // Handle system to allow weak references to be passed through C code
  virtual const std::string& GetHandle();

  // Name suitable for exposing to content
  virtual const std::string& GetName();

  // ICE events
  void IceConnectionStateChange(
      const std::string& aTransportId, dom::RTCIceTransportState state,
      const Maybe<dom::IceCandidateAttributePair>& aSelectedPair);
  void IceGatheringStateChange(const std::string& aTransportId,
                               dom::RTCIceGathererState state);
  void OnCandidateFound(const std::string& aTransportId,
                        const CandidateInfo& aCandidateInfo);
  void OnCandidateError(const IceCandidateErrorInfo& aErrorInfo);
  void UpdateDefaultCandidate(const std::string& defaultAddr,
                              uint16_t defaultPort,
                              const std::string& defaultRtcpAddr,
                              uint16_t defaultRtcpPort,
                              const std::string& transportId);

  static void ListenThread(void* aData);
  static void ConnectThread(void* aData);

  // Get the STS thread
  nsISerialEventTarget* GetSTSThread() {
    PC_AUTO_ENTER_API_CALL_NO_CHECK();
    return mSTSThread;
  }

  nsresult Initialize(PeerConnectionObserver& aObserver,
                      nsGlobalWindowInner* aWindow);

  // Initialize PeerConnection from an RTCConfiguration object (JS entrypoint)
  void Initialize(PeerConnectionObserver& aObserver,
                  nsGlobalWindowInner& aWindow, ErrorResult& rv);

  void SetCertificate(dom::RTCCertificate& aCertificate);
  const RefPtr<dom::RTCCertificate>& Certificate() const;
  // This is a hack to support external linkage.
  RefPtr<DtlsIdentity> Identity() const;

  NS_IMETHODIMP_TO_ERRORRESULT(CreateOffer, ErrorResult& rv,
                               const RTCOfferOptions& aOptions) {
    rv = CreateOffer(aOptions);
  }

  NS_IMETHODIMP CreateAnswer();
  void CreateAnswer(ErrorResult& rv) { rv = CreateAnswer(); }

  NS_IMETHODIMP CreateOffer(const JsepOfferOptions& aConstraints);

  NS_IMETHODIMP SetLocalDescription(int32_t aAction, const char* aSDP);

  void SetLocalDescription(int32_t aAction, const nsAString& aSDP,
                           ErrorResult& rv) {
    rv = SetLocalDescription(aAction, NS_ConvertUTF16toUTF8(aSDP).get());
  }

  NS_IMETHODIMP SetRemoteDescription(int32_t aAction, const char* aSDP);

  void SetRemoteDescription(int32_t aAction, const nsAString& aSDP,
                            ErrorResult& rv) {
    rv = SetRemoteDescription(aAction, NS_ConvertUTF16toUTF8(aSDP).get());
  }

  already_AddRefed<dom::Promise> GetStats(dom::MediaStreamTrack* aSelector);

  void GetRemoteStreams(nsTArray<RefPtr<DOMMediaStream>>& aStreamsOut) const;

  NS_IMETHODIMP AddIceCandidate(const char* aCandidate, const char* aMid,
                                const char* aUfrag,
                                const dom::Nullable<unsigned short>& aLevel);

  void AddIceCandidate(const nsAString& aCandidate, const nsAString& aMid,
                       const nsAString& aUfrag,
                       const dom::Nullable<unsigned short>& aLevel,
                       ErrorResult& rv) {
    rv = AddIceCandidate(NS_ConvertUTF16toUTF8(aCandidate).get(),
                         NS_ConvertUTF16toUTF8(aMid).get(),
                         NS_ConvertUTF16toUTF8(aUfrag).get(), aLevel);
  }

  void UpdateNetworkState(bool online);

  NS_IMETHODIMP CloseStreams();

  void CloseStreams(ErrorResult& rv) { rv = CloseStreams(); }

  already_AddRefed<dom::RTCRtpTransceiver> AddTransceiver(
      const dom::RTCRtpTransceiverInit& aInit, const nsAString& aKind,
      dom::MediaStreamTrack* aSendTrack, bool aAddTrackMagic, ErrorResult& aRv);

  bool CheckNegotiationNeeded();
  bool CreatedSender(const dom::RTCRtpSender& aSender) const;

  // test-only
  NS_IMETHODIMP_TO_ERRORRESULT(EnablePacketDump, ErrorResult& rv,
                               unsigned long level, dom::mozPacketDumpType type,
                               bool sending) {
    rv = EnablePacketDump(level, type, sending);
  }

  // test-only
  NS_IMETHODIMP_TO_ERRORRESULT(DisablePacketDump, ErrorResult& rv,
                               unsigned long level, dom::mozPacketDumpType type,
                               bool sending) {
    rv = DisablePacketDump(level, type, sending);
  }

  void GetPeerIdentity(nsAString& peerIdentity) {
    if (mPeerIdentity) {
      peerIdentity = mPeerIdentity->ToString();
      return;
    }

    peerIdentity.SetIsVoid(true);
  }

  const PeerIdentity* GetPeerIdentity() const { return mPeerIdentity; }
  NS_IMETHODIMP_TO_ERRORRESULT(SetPeerIdentity, ErrorResult& rv,
                               const nsAString& peerIdentity) {
    rv = SetPeerIdentity(peerIdentity);
  }

  const std::string& GetIdAsAscii() const { return mName; }

  void GetId(nsAString& id) { id = NS_ConvertASCIItoUTF16(mName.c_str()); }

  void SetId(const nsAString& id) { mName = NS_ConvertUTF16toUTF8(id).get(); }

  // this method checks to see if we've made a promise to protect media.
  bool PrivacyRequested() const {
    return mRequestedPrivacy.valueOr(PrincipalPrivacy::NonPrivate) ==
           PrincipalPrivacy::Private;
  }

  bool DuplicateFingerprintQuirk() { return mDuplicateFingerprintQuirk; }

  NS_IMETHODIMP GetFingerprint(char** fingerprint);
  void GetFingerprint(nsAString& fingerprint) {
    char* tmp;
    nsresult rv = GetFingerprint(&tmp);
    NS_ENSURE_SUCCESS_VOID(rv);
    fingerprint.AssignASCII(tmp);
    delete[] tmp;
  }

  void GetCurrentLocalDescription(nsAString& aSDP) const;
  void GetPendingLocalDescription(nsAString& aSDP) const;

  void GetCurrentRemoteDescription(nsAString& aSDP) const;
  void GetPendingRemoteDescription(nsAString& aSDP) const;

  dom::Nullable<bool> GetCurrentOfferer() const;
  dom::Nullable<bool> GetPendingOfferer() const;

  NS_IMETHODIMP SignalingState(dom::RTCSignalingState* aState);

  dom::RTCSignalingState SignalingState() {
    dom::RTCSignalingState state;
    SignalingState(&state);
    return state;
  }

  NS_IMETHODIMP IceConnectionState(dom::RTCIceConnectionState* aState);

  dom::RTCIceConnectionState IceConnectionState() {
    dom::RTCIceConnectionState state;
    IceConnectionState(&state);
    return state;
  }

  NS_IMETHODIMP IceGatheringState(dom::RTCIceGatheringState* aState);

  dom::RTCIceGatheringState IceGatheringState() { return mIceGatheringState; }

  NS_IMETHODIMP ConnectionState(dom::RTCPeerConnectionState* aState);

  dom::RTCPeerConnectionState ConnectionState() {
    dom::RTCPeerConnectionState state;
    ConnectionState(&state);
    return state;
  }

  NS_IMETHODIMP Close();

  void Close(ErrorResult& rv) { rv = Close(); }

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool PluginCrash(uint32_t aPluginID,
                                               const nsAString& aPluginName);

  NS_IMETHODIMP SetConfiguration(
      const RTCConfiguration& aConfiguration,
      nsTArray<IceServerParser::ParsedIceServer> aParsedIceServers);
  void SetConfiguration(const RTCConfiguration& aConfiguration,
                        ErrorResult& rv);

  dom::RTCSctpTransport* GetSctp() const;

  void RestartIce();
  void RestartIceNoRenegotiationNeeded();

  void RecordEndOfCallTelemetry();

  void RecordSignalingTelemetry() const;

  nsresult MaybeInitializeDataChannel();

  NS_IMETHODIMP_TO_ERRORRESULT_RETREF(RTCDataChannel, CreateDataChannel,
                                      ErrorResult& rv, const nsACString& aLabel,
                                      const nsACString& aProtocol,
                                      uint16_t aType, bool outOfOrderAllowed,
                                      uint16_t aMaxTime, uint16_t aMaxNum,
                                      bool aExternalNegotiated,
                                      uint16_t aStream);

  // Base class for chained operations. Necessary right now because some
  // operations come from JS (in the form of dom::ChainedOperation), and others
  // come from c++ (dom::ChainedOperation is very unwieldy and arcane to build
  // in c++). Once we stop using JSImpl, we should be able to simplify this.
  class Operation : public dom::PromiseNativeHandler {
   public:
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(Operation)
    Operation(PeerConnectionImpl* aPc, ErrorResult& aError);
    MOZ_CAN_RUN_SCRIPT
    void Call(ErrorResult& aError);
    dom::Promise* GetPromise() { return mPromise; }
    MOZ_CAN_RUN_SCRIPT
    void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                          ErrorResult& aRv) override;

    MOZ_CAN_RUN_SCRIPT
    void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                          ErrorResult& aRv) override;

   protected:
    MOZ_CAN_RUN_SCRIPT
    virtual RefPtr<dom::Promise> CallImpl(ErrorResult& aError) = 0;
    virtual ~Operation();
    // This is the promise p from https://w3c.github.io/webrtc-pc/#dfn-chain
    // This will be a content promise, since we return this to the caller of
    // Chain.
    RefPtr<dom::Promise> mPromise;
    RefPtr<PeerConnectionImpl> mPc;
  };

  class JSOperation final : public Operation {
   public:
    JSOperation(PeerConnectionImpl* aPc, dom::ChainedOperation& aOp,
                ErrorResult& aError);
    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(JSOperation, Operation)

   private:
    MOZ_CAN_RUN_SCRIPT
    RefPtr<dom::Promise> CallImpl(ErrorResult& aError) override;
    ~JSOperation() = default;
    RefPtr<dom::ChainedOperation> mOperation;
  };

  MOZ_CAN_RUN_SCRIPT
  already_AddRefed<dom::Promise> Chain(dom::ChainedOperation& aOperation,
                                       ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT
  already_AddRefed<dom::Promise> Chain(const RefPtr<Operation>& aOperation,
                                       ErrorResult& aError);
  already_AddRefed<dom::Promise> MakePromise(ErrorResult& aError) const;

  void UpdateNegotiationNeeded();

  void GetTransceivers(
      nsTArray<RefPtr<dom::RTCRtpTransceiver>>& aTransceiversOut) {
    aTransceiversOut = mTransceivers.Clone();
  }

  RefPtr<dom::RTCRtpTransceiver> GetTransceiver(
      const std::string& aTransceiverId);

  // See JsepSession::LocalOfferedRecvParamsChanged.
  bool LocalOfferedRecvParamsChanged(const std::string& aMid) {
    MOZ_ASSERT(NS_IsMainThread());
    return mJsepSession && mJsepSession->LocalOfferedRecvParamsChanged(aMid);
  }

  // Gets the RTC Signaling State of the JSEP session
  dom::RTCSignalingState GetSignalingState() const;

  already_AddRefed<dom::Promise> OnSetDescriptionSuccess(
      dom::RTCSdpType aSdpType, bool aRemote, ErrorResult& aError);

  void OnSetDescriptionError();

  bool IsClosed() const;

  // called when DTLS connects; we only need this once
  nsresult OnAlpnNegotiated(const std::string& aAlpn, bool aPrivacyRequested);

  void OnDtlsStateChange(const std::string& aTransportId,
                         TransportLayer::State aState,
                         const nsTArray<nsTArray<uint8_t>>& aRemoteCerts,
                         Maybe<dom::RTCErrorParams> aError);
  void OnRtcpStateChange(const std::string& aTransportId,
                         TransportLayer::State aState,
                         Maybe<dom::RTCErrorParams> aError);
  dom::RTCPeerConnectionState GetNewConnectionState() const;
  // Returns whether we need to fire a state change event
  bool UpdateConnectionState();
  dom::RTCIceConnectionState GetNewIceConnectionState() const;
  // Returns whether we need to fire a state change event
  bool UpdateIceConnectionState();
  dom::RTCIceGatheringState GetNewIceGatheringState() const;
  // Returns whether we need to fire a state change event
  bool UpdateIceGatheringState();

  // initialize telemetry for when calls start
  void StartCallTelem();

  // Gets all codec stats for all transports, coalesced to transport level.
  nsTArray<dom::RTCCodecStats> GetCodecStats(DOMHighResTimeStamp aNow);

  RefPtr<dom::RTCStatsReportPromise> GetStats(dom::MediaStreamTrack* aSelector,
                                              bool aInternalStats);

  void CollectConduitTelemetryData();

  void OnMediaError(const std::string& aError);

  void DumpPacket_m(size_t level, dom::mozPacketDumpType type, bool sending,
                    UniquePtr<uint8_t[]>& packet, size_t size);

  const dom::RTCStatsTimestampMaker& GetTimestampMaker() const {
    return mTimestampMaker;
  }

  void StampTimecard(const char* aEvent);

  bool RelayOnly() const {
    return mJsConfiguration.mIceTransportPolicy.WasPassed() &&
           mJsConfiguration.mIceTransportPolicy.Value() ==
               dom::RTCIceTransportPolicy::Relay;
  }

  RefPtr<PacketDumper> GetPacketDumper() {
    if (!mPacketDumper) {
      mPacketDumper = new PacketDumper(mHandle);
    }

    return mPacketDumper;
  }

  nsString GenerateUUID() const {
    std::string result;
    if (!mUuidGen->Generate(&result)) {
      MOZ_CRASH();
    }
    return NS_ConvertUTF8toUTF16(result.c_str());
  }

  bool ShouldAllowOldSetParameters() const { return mAllowOldSetParameters; }

  nsCString GetHostname() const { return mHostname; }
  nsCString GetEffectiveTLDPlus1() const { return mEffectiveTLDPlus1; }

  void SendWarningToConsole(const nsCString& aWarning);

  const UniquePtr<dom::RTCStatsReportInternal>& GetFinalStats() const {
    return mFinalStats;
  }

  void DisableLongTermStats() { mDisableLongTermStats = true; }

  bool LongTermStatsIsDisabled() const { return mDisableLongTermStats; }

  static void GetDefaultRtpExtensions(
      const JsepCodecPreferences& aPrefs,
      nsTArray<RtpExtensionHeader>* aRtpExtensions);

  static void GetCapabilities(const nsAString& aKind,
                              dom::Nullable<dom::RTCRtpCapabilities>& aResult,
                              sdp::Direction aDirection);

  void BreakCycles();

  using RTCDtlsTransportMap =
      nsTHashMap<nsCStringHashKey, RefPtr<dom::RTCDtlsTransport>>;

 private:
  nsTArray<IceServerParser::ParsedIceServer> ParseIceServers(
      const nsTArray<dom::RTCIceServer>& aIceServers, ErrorResult& aRv);

  virtual ~PeerConnectionImpl();
  PeerConnectionImpl(const PeerConnectionImpl& rhs);
  PeerConnectionImpl& operator=(PeerConnectionImpl);

  RefPtr<dom::RTCStatsPromise> GetDataChannelStats(
      const DOMHighResTimeStamp aTimestamp);
  nsresult CalculateFingerprint(const nsACString& algorithm,
                                std::vector<uint8_t>* fingerprint) const;

  NS_IMETHODIMP EnsureDataConnection(uint16_t aLocalPort, uint16_t aNumstreams);

  nsresult CheckApiState(bool assert_ice_ready) const;
  void StoreFinalStats(UniquePtr<dom::RTCStatsReportInternal>&& report);
  void CheckThread() const { MOZ_ASSERT(NS_IsMainThread(), "Wrong thread"); }

  // test-only: called from AddRIDExtension and AddRIDFilter
  // for simulcast mochitests.
  RefPtr<MediaPipeline> GetMediaPipelineForTrack(
      dom::MediaStreamTrack& aRecvTrack);

  void CandidateReady(const std::string& candidate,
                      const std::string& transportId, const std::string& ufrag);
  void SendLocalIceCandidateToContent(uint16_t level, const std::string& mid,
                                      const std::string& candidate,
                                      const std::string& ufrag);

  nsresult GetDatachannelParameters(uint32_t* channels, uint16_t* localport,
                                    uint16_t* remoteport,
                                    uint32_t* maxmessagesize,
                                    std::string* transportId,
                                    bool* client) const;

  nsresult AddRtpTransceiverToJsepSession(JsepTransceiver& transceiver);

  void RecordIceRestartStatistics(JsepSdpType type);

  void StoreConfigurationForAboutWebrtc(const RTCConfiguration& aConfig);

  dom::Sequence<dom::RTCSdpParsingErrorInternal> GetLastSdpParsingErrors()
      const;

  MOZ_CAN_RUN_SCRIPT
  void RunNextOperation(ErrorResult& aError);

  void SyncToJsep();
  void SyncFromJsep();

  void DoSetDescriptionSuccessPostProcessing(dom::RTCSdpType aSdpType,
                                             bool aRemote,
                                             const RefPtr<dom::Promise>& aP);

  // Timecard used to measure processing time. This should be the first class
  // attribute so that we accurately measure the time required to instantiate
  // any other attributes of this class.
  Timecard* mTimeCard;

  // Configuration used to initialize the PeerConnection
  dom::RTCConfigurationInternal mJsConfiguration;

  dom::RTCSignalingState mSignalingState;

  // ICE State
  dom::RTCIceConnectionState mIceConnectionState;
  dom::RTCIceGatheringState mIceGatheringState;

  dom::RTCPeerConnectionState mConnectionState;

  RefPtr<PeerConnectionObserver> mPCObserver;

  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  nsString mOrigin;

  // The SDP sent in from JS
  std::string mLocalRequestedSDP;
  std::string mRemoteRequestedSDP;
  // Only accessed from main
  dom::Sequence<dom::RTCSdpHistoryEntryInternal> mSdpHistory;
  std::string mPendingLocalDescription;
  std::string mPendingRemoteDescription;
  std::string mCurrentLocalDescription;
  std::string mCurrentRemoteDescription;
  Maybe<bool> mPendingOfferer;
  Maybe<bool> mCurrentOfferer;

  // DTLS fingerprint
  std::string mFingerprint;
  std::string mRemoteFingerprint;

  // identity-related fields
  // The entity on the other end of the peer-to-peer connection;
  // void if they are not yet identified, and no identity setting has been set
  RefPtr<PeerIdentity> mPeerIdentity;
  // The certificate we are using.
  RefPtr<dom::RTCCertificate> mCertificate;
  // Whether an app should be prevented from accessing media produced by the PC
  // If this is true, then media will not be sent until mPeerIdentity matches
  // local streams PeerIdentity; and remote streams are protected from content
  //
  // This can be false if mPeerIdentity is set, in the case where identity is
  // provided, but the media is not protected from the app on either side
  Maybe<PrincipalPrivacy> mRequestedPrivacy;

  // A handle to refer to this PC with
  std::string mHandle;

  // A name for this PC that we are willing to expose to content.
  std::string mName;
  nsCString mHostname;
  nsCString mEffectiveTLDPlus1;

  // The target to run stuff on
  nsCOMPtr<nsISerialEventTarget> mSTSThread;

  // DataConnection that's used to get all the DataChannels
  RefPtr<DataChannelConnection> mDataConnection;
  unsigned int mDataChannelsOpened = 0;
  unsigned int mDataChannelsClosed = 0;

  bool mForceIceTcp;
  Maybe<dom::RTCRtcpMuxPolicy> mRtcpMuxPolicy;
  RefPtr<MediaTransportHandler> mTransportHandler;

  // The JSEP negotiation session.
  UniquePtr<PCUuidGenerator> mUuidGen;
  UniquePtr<JsepSession> mJsepSession;
  // There are lots of error cases where we want to abandon an sRD/sLD _after_
  // it has already been applied to the JSEP engine, and revert back to the
  // previous state. We also want to ensure that the various modifications
  // to the JSEP engine are not exposed to JS until the sRD/sLD completes,
  // which is why we have a new "uncommitted" JSEP engine.
  UniquePtr<JsepSession> mUncommittedJsepSession;
  unsigned long mIceRestartCount;
  unsigned long mIceRollbackCount;

  // The following are used for Telemetry:
  bool mCallTelemStarted = false;
  bool mCallTelemEnded = false;

  // We _could_ make mFinalStatsQuery be an RTCStatsReportPromise, but that
  // would require RTCStatsReportPromise to no longer be exclusive, which is
  // a bit of a hassle, and not very performant.
  RefPtr<GenericNonExclusivePromise> mFinalStatsQuery;
  UniquePtr<dom::RTCStatsReportInternal> mFinalStats;
  bool mDisableLongTermStats = false;

  // Start time of ICE.
  TimeStamp mIceStartTime;
  // Hold PeerConnectionAutoTimer instances for each window.
  static std::map<uint64_t, PeerConnectionAutoTimer> sCallDurationTimers;

  bool mTrickle;

  bool mPrivateWindow;

  // Whether this PeerConnection is being counted as active by mWindow
  bool mActiveOnWindow;

  // storage for Telemetry data
  uint16_t mMaxReceiving[SdpMediaSection::kMediaTypes];
  uint16_t mMaxSending[SdpMediaSection::kMediaTypes];

  // used to store the raw trickle candidate string for display
  // on the about:webrtc raw candidates table.
  std::vector<std::string> mRawTrickledCandidates;

  dom::RTCStatsTimestampMaker mTimestampMaker;

  RefPtr<RTCStatsIdGenerator> mIdGenerator;
  // Ordinarily, I would use a std::map here, but this used to be a JS Map
  // which iterates in insertion order, and I want to avoid changing this.
  nsTArray<RefPtr<DOMMediaStream>> mReceiveStreams;

  DOMMediaStream* GetReceiveStream(const std::string& aId) const;
  DOMMediaStream* CreateReceiveStream(const std::string& aId);

  void InitLocalAddrs();  // for stun local address IPC request
  bool ShouldForceProxy() const;
  std::unique_ptr<NrSocketProxyConfig> GetProxyConfig() const;

  class StunAddrsHandler : public net::StunAddrsListener {
   public:
    explicit StunAddrsHandler(PeerConnectionImpl* aPc)
        : mPcHandle(aPc->GetHandle()) {}

    void OnMDNSQueryComplete(const nsCString& hostname,
                             const Maybe<nsCString>& address) override;

    void OnStunAddrsAvailable(const net::NrIceStunAddrArray& addrs) override;

   private:
    // This class is not cycle-collected, so we must avoid grabbing a strong
    // reference.
    const std::string mPcHandle;
    virtual ~StunAddrsHandler() = default;
  };

  // Manage ICE transports.
  void UpdateTransport(const JsepTransceiver& aTransceiver, bool aForceIceTcp);

  void GatherIfReady();
  void FlushIceCtxOperationQueueIfReady();
  void PerformOrEnqueueIceCtxOperation(nsIRunnable* runnable);
  nsresult SetTargetForDefaultLocalAddressLookup();
  // Calls SetTargetForDefaultLocalAddressLookup the first time either the
  // candidate pool or regular gathering is about to gather.
  void EnsureTargetForDefaultLocalAddressLookup();
  void EnsureIceGathering(bool aDefaultRouteOnly, bool aObfuscateHostAddresses);

  bool GetPrefDefaultAddressOnly() const;
  bool GetPrefObfuscateHostAddresses() const;

  bool IsIceCtxReady() const {
    return mLocalAddrsRequestState == STUN_ADDR_REQUEST_COMPLETE;
  }

  // Ensure ICE transports exist that we might need when offer/answer concludes
  void EnsureTransports(const JsepSession& aSession);

  // Releases whatever is left of the candidate pool, once gathering has started
  // or the pool has to be replaced.
  void EmptyCandidatePool();

  void UpdateRTCDtlsTransports();
  // Creates/updates mSctpTransport based on whether a data section has
  // appeared in an SDP. May run in have-remote-offer, where the
  // RTCSctpTransport has a null transport until UpdateRTCDtlsTransports fills
  // it in.
  void UpdateRTCSctpTransport();
  void SaveStateForRollback();
  void RestoreStateForRollback();
  std::set<RefPtr<dom::RTCDtlsTransport>> GetActiveTransports() const;

  // Activate ICE transports at the conclusion of offer/answer, or when
  // rollback occurs. A provisional answer activates its transports too, but
  // does not remove the ones it leaves unused.
  nsresult UpdateTransports(const JsepSession& aSession, const bool forceIceTcp,
                            const bool aProvisional);

  void ResetStunAddrsForIceRestart() { mStunAddrs.Clear(); }

  // Start ICE checks.
  void StartIceChecks(const JsepSession& session);

  // Process a trickle ICE candidate.
  void AddIceCandidate(const std::string& candidate,
                       const std::string& aTransportId,
                       const std::string& aUFrag);

  // Handle complete media pipelines.
  // This updates codec parameters, starts/stops send/receive, and other
  // stuff that doesn't necessarily require negotiation. This can be called at
  // any time, not just when an offer/answer exchange completes.
  nsresult UpdateMediaPipelines();

  already_AddRefed<dom::RTCRtpTransceiver> CreateTransceiver(
      const std::string& aId, bool aIsVideo,
      const dom::RTCRtpTransceiverInit& aInit,
      dom::MediaStreamTrack* aSendTrack, bool aAddTrackMagic, ErrorResult& aRv);

  std::string GetTransportIdMatchingSendTrack(
      const dom::MediaStreamTrack& aTrack) const;

  // this determines if any track is peerIdentity constrained
  bool AnyLocalTrackHasPeerIdentity() const;

  bool AnyCodecHasPluginID(uint64_t aPluginID);

  already_AddRefed<nsIHttpChannelInternal> GetChannel() const;

  bool HasPendingSetParameters() const;
  void InvalidateLastReturnedParameters();

  RefPtr<WebrtcCallWrapper> mCall;

 public:
  const DefaultCodecPreferences mPrefs;

 private:
  // See Bug 1642419, this can be removed when all sites are working with RTX.
  bool mRtxIsAllowed = true;

  bool mDuplicateFingerprintQuirk = false;

  nsTArray<RefPtr<Operation>> mOperations;
  bool mChainingOperation = false;
  bool mUpdateNegotiationNeededFlagOnEmptyChain = false;
  bool mNegotiationNeeded = false;
  std::set<std::pair<std::string, std::string>> mLocalIceCredentialsToReplace;

  nsTArray<RefPtr<dom::RTCRtpTransceiver>> mTransceivers;
  // The lifecycle of worker datachannels is managed in RTCDataChannel
  nsTArray<RefPtr<dom::RTCDataChannel>> mMainthreadDatachannels;
  RTCDtlsTransportMap mTransportIdToRTCDtlsTransport;
  RefPtr<dom::RTCSctpTransport> mSctpTransport;
  // This is similar to [[LastStableStateSender/ReceiverTransport]], but for
  // DataChannel.
  RefPtr<dom::RTCSctpTransport> mLastStableSctpTransport;
  RefPtr<dom::RTCDtlsTransport> mLastStableSctpDtlsTransport;

  // Used whenever we need to dispatch a runnable to STS to tweak something
  // on our ICE ctx, but are not ready to do so at the moment (eg; we are
  // waiting to get a callback with our http proxy config before we start
  // gathering or start checking)
  std::vector<nsCOMPtr<nsIRunnable>> mQueuedIceCtxOperations;

  // Set if prefs dictate that we should force the use of a web proxy.
  bool mForceProxy = false;

  // Set if the window's Connection Allowlists block WebRTC, which makes every
  // local address, remote candidate and ICE server administratively
  // prohibited.
  bool mBlockedByConnectionAllowlists = false;

  // Used to cancel incoming stun addrs response
  RefPtr<net::StunAddrsRequestChild> mStunAddrsRequest;

  enum StunAddrRequestState {
    STUN_ADDR_REQUEST_NONE,
    STUN_ADDR_REQUEST_PENDING,
    STUN_ADDR_REQUEST_COMPLETE
  };
  // Used to track the state of the stun addr IPC request
  StunAddrRequestState mLocalAddrsRequestState = STUN_ADDR_REQUEST_NONE;

  // Used to store the result of the stun addr IPC request
  nsTArray<NrIceStunAddr> mStunAddrs;

  // Used to ensure the target for default local address lookup is only set
  // once.
  bool mTargetForDefaultLocalAddressLookupIsSet = false;

  // Keep track of local hostnames to register. Registration is deferred
  // until StartIceChecks has run. Accessed on main thread only.
  std::map<std::string, std::string> mMDNSHostnamesToRegister;
  bool mCanRegisterMDNSHostnamesDirectly = false;

  // Used to store the mDNS hostnames that we have registered
  std::set<std::string> mRegisteredMDNSHostnames;

  // web-compat stopgap
  bool mAllowOldSetParameters = false;

  // For candidates that require an mDNS query before they can be used.
  struct PendingIceCandidate {
    std::string mCandidate;
    std::string mTransportId;
    std::string mUfrag;
  };
  std::map<std::string, std::list<PendingIceCandidate>> mQueriedMDNSHostnames;

  // The size of the candidate pool last handed to the transport handler, or 0
  // if there is no pool to release. Which transports end up using it, and how
  // much of it they use, is decided below this layer.
  uint8_t mIceCandidatePoolSize = 0;
  // The ICE servers most recently handed to the transport handler, parsed, so
  // that setConfiguration can tell whether they actually changed.
  nsTArray<IceServerParser::ParsedIceServer> mParsedIceServers;

  MediaEventListener mGatheringStateChangeListener;
  MediaEventListener mConnectionStateChangeListener;
  MediaEventListener mCandidateListener;
  MediaEventListener mCandidateErrorListener;
  MediaEventListener mAlpnNegotiatedListener;
  MediaEventListener mStateChangeListener;
  MediaEventListener mRtcpStateChangeListener;

  // Make absolutely sure our refcount does not go to 0 before Close() is called
  // This is because Close does a stats query, which needs the
  // PeerConnectionImpl to stick around until the query is done.
  RefPtr<PeerConnectionImpl> mKungFuDeathGrip;
  RefPtr<PacketDumper> mPacketDumper;

 public:
  // these are temporary until the DataChannel Listen/Connect API is removed
  unsigned short listenPort;
  unsigned short connectPort;
  char* connectStr;  // XXX ownership/free
};

// This is what is returned when you acquire on a handle
class PeerConnectionWrapper {
 public:
  explicit PeerConnectionWrapper(const std::string& handle);

  PeerConnectionImpl* impl() { return impl_; }

 private:
  RefPtr<PeerConnectionImpl> impl_;
};

}  // namespace mozilla

#undef NS_IMETHODIMP_TO_ERRORRESULT
#undef NS_IMETHODIMP_TO_ERRORRESULT_RETREF
#endif  // PEER_CONNECTION_IMPL_H_
