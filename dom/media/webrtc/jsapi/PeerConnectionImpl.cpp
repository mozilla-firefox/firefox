/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PeerConnectionImpl.h"

#include <cerrno>
#include <cstdlib>
#include <set>
#include <sstream>
#include <vector>

#include "IPeerConnection.h"
#include "MediaTrackGraph.h"
#include "PeerConnectionCtx.h"
#include "RTCDataChannelDeclarations.h"
#include "RemoteTrackSource.h"
#include "base/histogram.h"
#include "common/browser_logging/CSFLog.h"
#include "common/time_profiling/timecard.h"
#include "jsapi.h"
#include "jsapi/RTCRtpReceiver.h"
#include "jsapi/RTCRtpSender.h"
#include "jsep/JsepSession.h"
#include "jsep/JsepSessionImpl.h"
#include "jsep/JsepTrack.h"
#include "libwebrtcglue/AudioConduit.h"
#include "libwebrtcglue/VideoConduit.h"
#include "libwebrtcglue/WebrtcCallWrapper.h"
#include "libwebrtcglue/WebrtcEnvironmentWrapper.h"
#include "mozilla/IceServerParser.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Sprintf.h"
#include "mozilla/glean/DomMediaWebrtcMetrics.h"
#include "mozilla/media/MediaUtils.h"
#include "nsEffectiveTLDService.h"
#include "nsILoadContext.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"
#include "prtime.h"
#include "sdp/SdpAttribute.h"
#include "transport/dtlsidentity.h"
#include "transport/runnable_utils.h"
#include "transportbridge/MediaPipeline.h"
#include "transportbridge/RtpLogger.h"

#ifdef XP_WIN
// We need to undef the MS macro for Document::CreateEvent
#  ifdef CreateEvent
#    undef CreateEvent
#  endif
#endif  // XP_WIN

#include "AudioStreamTrack.h"
#include "DOMMediaStream.h"
#include "MediaManager.h"
#include "MediaStreamTrack.h"
#include "PeerConnectionImpl.h"
#include "RTCDataChannel.h"
#include "RTCDtlsTransport.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidatePair.h"
#include "RTCSctpTransport.h"
#include "VideoStreamTrack.h"
#include "WebrtcGlobalInformation.h"
#include "js/ArrayBuffer.h"    // JS::NewArrayBufferWithContents
#include "js/GCAnnotations.h"  // JS_HAZ_ROOTED
#include "js/RootingAPI.h"     // JS::{{,Mutable}Handle,Rooted}
#include "jsep/JsepTransport.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PeerIdentity.h"
#include "mozilla/Preferences.h"
#include "mozilla/PublicSSL.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Location.h"
#include "mozilla/dom/PeerConnectionImplBinding.h"
#include "mozilla/dom/PluginCrashedEvent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RTCCertificate.h"
#include "mozilla/dom/RTCDataChannelBinding.h"
#include "mozilla/dom/RTCDtlsTransportBinding.h"  // RTCDtlsTransportState
#include "mozilla/dom/RTCIceTransportBinding.h"   // RTCIceTransportState
#include "mozilla/dom/RTCPeerConnectionBinding.h"
#include "mozilla/dom/RTCRtpReceiverBinding.h"
#include "mozilla/dom/RTCRtpSenderBinding.h"
#include "mozilla/dom/RTCSctpTransportBinding.h"  // RTCSctpTransportState
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/glean/DomMediaWebrtcMetrics.h"
#include "mozilla/net/DataChannelProtocol.h"
#include "mozilla/net/WebrtcProxyConfig.h"
#include "nsContentUtils.h"
#include "nsDOMJSUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsILoadInfo.h"
#include "nsIPrincipal.h"
#include "nsIProxiedChannel.h"
#include "nsIScriptGlobalObject.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPrintfCString.h"
#include "nsURLHelper.h"
#include "nsXULAppAPI.h"
#include "transport/nr_socket_proxy_config.h"

#ifdef XP_WIN
// We need to undef the MS macro again in case the windows include file
// got imported after we included mozilla/dom/Document.h
#  ifdef CreateEvent
#    undef CreateEvent
#  endif
#endif  // XP_WIN

#include "MediaSegment.h"

#ifdef USE_FAKE_PCOBSERVER
#  include "FakePCObserver.h"
#else
#  include "mozilla/dom/PeerConnectionObserverBinding.h"
#endif
#include "mozilla/dom/PeerConnectionObserverEnumsBinding.h"

#define ICE_PARSING \
  "In RTCConfiguration passed to RTCPeerConnection constructor"

using namespace mozilla;
using namespace mozilla::dom;
using glean::webrtc_signaling::AudioMsectionNegotiatedExtra;
using glean::webrtc_signaling::SdpNegotiatedExtra;
using glean::webrtc_signaling::VideoMsectionNegotiatedExtra;

typedef PCObserverString ObString;

static const char* pciLogTag = "PeerConnectionImpl";
#ifdef LOGTAG
#  undef LOGTAG
#endif
#define LOGTAG pciLogTag

static mozilla::LazyLogModule logModuleInfo("signaling");

// Getting exceptions back down from PCObserver is generally not harmful.
namespace {
// This is a terrible hack.  The problem is that SuppressException is not
// inline, and we link this file without libxul in some cases (e.g. for our test
// setup).  So we can't use ErrorResult or IgnoredErrorResult because those call
// SuppressException...  And we can't use FastErrorResult because we can't
// include BindingUtils.h, because our linking is completely broken. Use
// BaseErrorResult directly.  Please do not let me see _anyone_ doing this
// without really careful review from someone who knows what they are doing.
class JSErrorResult : public binding_danger::TErrorResult<
                          binding_danger::JustAssertCleanupPolicy> {
 public:
  ~JSErrorResult() { SuppressException(); }
} JS_HAZ_ROOTED;

// The WrapRunnable() macros copy passed-in args and passes them to the function
// later on the other thread. ErrorResult cannot be passed like this because it
// disallows copy-semantics.
//
// This WrappableJSErrorResult hack solves this by not actually copying the
// ErrorResult, but creating a new one instead, which works because we don't
// care about the result.
//
// Since this is for JS-calls, these can only be dispatched to the main thread.

class WrappableJSErrorResult {
 public:
  WrappableJSErrorResult() : mRv(MakeUnique<JSErrorResult>()), isCopy(false) {}
  WrappableJSErrorResult(const WrappableJSErrorResult& other)
      : mRv(MakeUnique<JSErrorResult>()), isCopy(true) {}
  ~WrappableJSErrorResult() {
    if (isCopy) {
      MOZ_ASSERT(NS_IsMainThread());
    }
  }
  operator ErrorResult&() { return *mRv; }

 private:
  mozilla::UniquePtr<JSErrorResult> mRv;
  bool isCopy;
} JS_HAZ_ROOTED;

}  // namespace

static nsresult InitNSSInContent() {
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_NOT_SAME_THREAD);

  if (!XRE_IsContentProcess()) {
    MOZ_ASSERT_UNREACHABLE("Must be called in content process");
    return NS_ERROR_FAILURE;
  }

  static bool nssStarted = false;
  if (nssStarted) {
    return NS_OK;
  }

  if (NSS_NoDB_Init(nullptr) != SECSuccess) {
    CSFLogError(LOGTAG, "NSS_NoDB_Init failed.");
    return NS_ERROR_FAILURE;
  }

  if (NS_FAILED(mozilla::psm::InitializeCipherSuite())) {
    CSFLogError(LOGTAG, "Fail to set up nss cipher suite.");
    return NS_ERROR_FAILURE;
  }

  mozilla::psm::DisableMD5();

  nssStarted = true;

  return NS_OK;
}

namespace mozilla {
class DataChannel;
}

namespace mozilla {

void PeerConnectionAutoTimer::RegisterConnection() { mRefCnt++; }

void PeerConnectionAutoTimer::UnregisterConnection(bool aContainedAV) {
  MOZ_ASSERT(mRefCnt);
  mRefCnt--;
  mUsedAV |= aContainedAV;
  if (mRefCnt == 0) {
    TimeDuration sample = TimeStamp::Now() - mStart;
    if (mUsedAV) {
      glean::webrtc::av_call_duration.AccumulateRawDuration(sample);
    }
    glean::webrtc::call_duration.AccumulateRawDuration(sample);
  }
}

bool PeerConnectionAutoTimer::IsStopped() { return mRefCnt == 0; }

// There is not presently an implementation of these for nsTHashMap :(
inline void ImplCycleCollectionUnlink(
    PeerConnectionImpl::RTCDtlsTransportMap& aMap) {
  for (auto& tableEntry : aMap) {
    ImplCycleCollectionUnlink(*tableEntry.GetModifiableData());
  }
  aMap.Clear();
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    PeerConnectionImpl::RTCDtlsTransportMap& aMap, const char* aName,
    uint32_t aFlags = 0) {
  for (auto& tableEntry : aMap) {
    ImplCycleCollectionTraverse(aCallback, *tableEntry.GetModifiableData(),
                                aName, aFlags);
  }
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(PeerConnectionImpl)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(PeerConnectionImpl)
  tmp->Close();
  tmp->BreakCycles();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mPCObserver, mWindow, mCertificate, mSTSThread, mReceiveStreams,
      mOperations, mMainthreadDatachannels, mTransportIdToRTCDtlsTransport,
      mSctpTransport, mLastStableSctpTransport, mLastStableSctpDtlsTransport,
      mKungFuDeathGrip)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(PeerConnectionImpl)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mPCObserver, mWindow, mCertificate, mSTSThread, mReceiveStreams,
      mOperations, mTransceivers, mMainthreadDatachannels,
      mTransportIdToRTCDtlsTransport, mSctpTransport, mLastStableSctpTransport,
      mLastStableSctpDtlsTransport, mKungFuDeathGrip)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(PeerConnectionImpl)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PeerConnectionImpl)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PeerConnectionImpl)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

already_AddRefed<PeerConnectionImpl> PeerConnectionImpl::Constructor(
    const dom::GlobalObject& aGlobal) {
  RefPtr pc = MakeRefPtr<PeerConnectionImpl>(&aGlobal);

  CSFLogDebug(LOGTAG, "Created PeerConnection: %p", pc.get());

  return pc.forget();
}

JSObject* PeerConnectionImpl::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return PeerConnectionImpl_Binding::Wrap(aCx, this, aGivenProto);
}

nsPIDOMWindowInner* PeerConnectionImpl::GetParentObject() const {
  return mWindow;
}

bool PCUuidGenerator::Generate(std::string* idp) {
  nsresult rv;

  if (!mGenerator) {
    mGenerator = do_GetService("@mozilla.org/uuid-generator;1", &rv);
    if (NS_FAILED(rv)) {
      return false;
    }
    if (!mGenerator) {
      return false;
    }
  }

  nsID id;
  rv = mGenerator->GenerateUUIDInPlace(&id);
  if (NS_FAILED(rv)) {
    return false;
  }
  char buffer[NSID_LENGTH];
  id.ToProvidedString(buffer);
  idp->assign(buffer);

  return true;
}

bool IsPrivateBrowsing(nsPIDOMWindowInner* aWindow) {
  if (!aWindow) {
    return false;
  }

  Document* doc = aWindow->GetExtantDoc();
  if (!doc) {
    return false;
  }

  nsILoadContext* loadContext = doc->GetLoadContext();
  return loadContext && loadContext->UsePrivateBrowsing();
}

static void RecordCodecTelemetry(const JsepCodecPreferences& aPrefs) {
  if (WebrtcVideoConduit::HasH264Hardware()) {
    glean::webrtc::has_h264_hardware
        .EnumGet(glean::webrtc::HasH264HardwareLabel::eTrue)
        .Add();
  }

  glean::webrtc::software_h264_enabled
      .EnumGet(static_cast<glean::webrtc::SoftwareH264EnabledLabel>(
          aPrefs.SoftwareH264Enabled()))
      .Add();
  glean::webrtc::hardware_h264_enabled
      .EnumGet(static_cast<glean::webrtc::HardwareH264EnabledLabel>(
          aPrefs.HardwareH264Enabled()))
      .Add();
  glean::webrtc::h264_enabled
      .EnumGet(
          static_cast<glean::webrtc::H264EnabledLabel>(aPrefs.H264Enabled()))
      .Add();
}

PeerConnectionImpl::PeerConnectionImpl(const GlobalObject* aGlobal)
    : mTimeCard(MOZ_LOG_TEST(logModuleInfo, LogLevel::Error) ? create_timecard()
                                                             : nullptr),
      mSignalingState(RTCSignalingState::Stable),
      mIceConnectionState(RTCIceConnectionState::New),
      mIceGatheringState(RTCIceGatheringState::New),
      mConnectionState(RTCPeerConnectionState::New),
      mWindow(do_QueryInterface(aGlobal ? aGlobal->GetAsSupports() : nullptr)),
      mCertificate(nullptr),
      mSTSThread(nullptr),
      mForceIceTcp(false),
      mTransportHandler(nullptr),
      mUuidGen(MakeUnique<PCUuidGenerator>()),
      mIceRestartCount(0),
      mIceRollbackCount(0),
      mTrickle(true)  // TODO(ekr@rtfm.com): Use pref
      ,
      mPrivateWindow(false),
      mActiveOnWindow(false),
      mTimestampMaker(dom::RTCStatsTimestampMaker::Create(mWindow)),
      mIdGenerator(MakeRefPtr<RTCStatsIdGenerator>()),
      listenPort(0),
      connectPort(0),
      connectStr(nullptr) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(aGlobal, mWindow);
  mKungFuDeathGrip = this;
  if (aGlobal) {
    if (IsPrivateBrowsing(mWindow)) {
      mPrivateWindow = true;
      mDisableLongTermStats = true;
    }
    mWindow->AddPeerConnection();
    mActiveOnWindow = true;

    if (mWindow->GetDocumentURI()) {
      mWindow->GetDocumentURI()->GetAsciiHost(mHostname);
      nsresult rv;
      nsCOMPtr<nsIEffectiveTLDService> eTLDService(
          do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID, &rv));
      if (eTLDService) {
        (void)eTLDService->GetBaseDomain(mWindow->GetDocumentURI(), 0,
                                         mEffectiveTLDPlus1);
      }

      mRtxIsAllowed = !media::HostnameInPref(
          "media.peerconnection.video.use_rtx.blocklist", mHostname);
      mDuplicateFingerprintQuirk = media::HostnameInPref(
          "media.peerconnection.sdp.quirk.duplicate_fingerprint.allowlist",
          mHostname);
    }
  }

  if (!mUuidGen->Generate(&mHandle)) {
    MOZ_CRASH();
  }

  CSFLogInfo(LOGTAG, "%s: PeerConnectionImpl constructor for %s", __FUNCTION__,
             mHandle.c_str());
  STAMP_TIMECARD(mTimeCard, "Constructor Completed");
  mForceIceTcp =
      Preferences::GetBool("media.peerconnection.ice.force_ice_tcp", false);
  memset(mMaxReceiving, 0, sizeof(mMaxReceiving));
  memset(mMaxSending, 0, sizeof(mMaxSending));
  mJsConfiguration.mCertificatesProvided = false;
  mJsConfiguration.mPeerIdentityProvided = false;
}

PeerConnectionImpl::~PeerConnectionImpl() {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(!mTransportHandler,
             "PeerConnection should either be closed, or not initted in the "
             "first place.");

  if (mTimeCard) {
    STAMP_TIMECARD(mTimeCard, "Destructor Invoked");
    STAMP_TIMECARD(mTimeCard, mHandle.c_str());
    print_timecard(mTimeCard);
    destroy_timecard(mTimeCard);
    mTimeCard = nullptr;
  }

  CSFLogInfo(LOGTAG, "%s: PeerConnectionImpl destructor invoked for %s",
             __FUNCTION__, mHandle.c_str());
}

nsresult PeerConnectionImpl::Initialize(PeerConnectionObserver& aObserver,
                                        nsGlobalWindowInner* aWindow) {
  nsresult res;

  MOZ_ASSERT(NS_IsMainThread());

  mPCObserver = &aObserver;

  // Find the STS thread

  mSTSThread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &res);
  MOZ_ASSERT(mSTSThread);

  RefPtr transportHandler = MediaTransportHandler::Create();
  if (mPrivateWindow) {
    transportHandler->EnterPrivateMode();
  }

  // Initialize NSS if we are in content process. For chrome process, NSS should
  // already been initialized.
  if (XRE_IsParentProcess()) {
    // This code interferes with the C++ unit test startup code.
    nsCOMPtr<nsISupports> nssDummy = do_GetService("@mozilla.org/psm;1", &res);
    NS_ENSURE_SUCCESS(res, res);
  } else {
    NS_ENSURE_SUCCESS(res = InitNSSInContent(), res);
  }

  // Currently no standalone unit tests for DataChannel,
  // which is the user of mWindow
  MOZ_ASSERT(aWindow);
  mWindow = aWindow;
  NS_ENSURE_STATE(mWindow);

  // Now rummage through the objects to get a usable origin
  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(mWindow);
  NS_ENSURE_STATE(sgo);
  nsCOMPtr<nsIScriptContext> scriptContext = sgo->GetContext();
  NS_ENSURE_STATE(scriptContext);

  nsCOMPtr<nsIScriptObjectPrincipal> scriptPrincipal(
      do_QueryInterface(mWindow));
  NS_ENSURE_STATE(scriptPrincipal);
  nsCOMPtr<nsIPrincipal> principal = scriptPrincipal->GetPrincipal();
  NS_ENSURE_STATE(principal);

  res = nsContentUtils::GetWebExposedOriginSerialization(principal, mOrigin);
  NS_ENSURE_SUCCESS(res, res);

  PRTime timestamp = PR_Now();
  // Ok if we truncate this, but we want it to be large enough to reliably
  // contain the location on the tests we run in CI.
  char temp[256];

  nsAutoCString locationCStr;

  RefPtr<Location> location = mWindow->Location();
  res = location->GetHref(locationCStr);
  NS_ENSURE_SUCCESS(res, res);

  SprintfLiteral(temp, "%s %" PRIu64 " (id=%" PRIu64 " url=%s)",
                 mHandle.c_str(), static_cast<uint64_t>(timestamp),
                 static_cast<uint64_t>(mWindow ? mWindow->WindowID() : 0),
                 locationCStr.get() ? locationCStr.get() : "NULL");

  mName = temp;

  STAMP_TIMECARD(mTimeCard, "Initializing PC Ctx");
  res = PeerConnectionCtx::InitializeGlobal();
  NS_ENSURE_SUCCESS(res, res);

  // Only set mTransportHandler here, after the NS_ENSURE_ exit guards, to not
  // leave it in an unusable state -- CreateIceCtx must have been called for
  // other calls to work.
  mTransportHandler = std::move(transportHandler);
  mTransportHandler->CreateIceCtx("PC:" + GetName());

  mJsepSession =
      MakeUnique<JsepSessionImpl>(mName, MakeUnique<PCUuidGenerator>());
  mJsepSession->SetRtxIsAllowed(mRtxIsAllowed);

  res = mJsepSession->Init();
  if (NS_FAILED(res)) {
    CSFLogError(LOGTAG, "%s: Couldn't init JSEP Session, res=%u", __FUNCTION__,
                static_cast<unsigned>(res));
    return res;
  }

  AutoTArray<UniquePtr<JsepCodecDescription>, 16> preferredCodecs;
  EnumerateDefaultVideoCodecs(&preferredCodecs, mPrefs);
  EnumerateDefaultAudioCodecs(&preferredCodecs, mPrefs);
  mJsepSession->SetDefaultCodecs(preferredCodecs);

  RecordCodecTelemetry(mPrefs);

  // We use this to sort the list of codecs once everything is configured
  CompareCodecPriority comparator;
  // Sort by priority
  mJsepSession->SortCodecs(comparator);

  AutoTArray<RtpExtensionHeader, 16> preferredHeaders;
  GetDefaultRtpExtensions(mPrefs, &preferredHeaders);

  for (const auto& header : preferredHeaders) {
    mJsepSession->AddRtpExtension(header.mMediaType, header.extensionname,
                                  header.direction);
  }

  if (XRE_IsContentProcess()) {
    mStunAddrsRequest =
        net::StunAddrsRequestChild::Create(new StunAddrsHandler(this));
  }

  // Initialize the media object.
  mForceProxy = ShouldForceProxy();

  // We put this here, in case we later want to set this based on a non-standard
  // param in RTCConfiguration.
  mAllowOldSetParameters = Preferences::GetBool(
      "media.peerconnection.allow_old_setParameters", false);
  mAllowOldSetParameters |= media::HostnameInPref(
      "media.peerconnection.allow_old_setParameters.allowlist", mHostname);

  // setup the stun local addresses IPC async call
  InitLocalAddrs();

  PeerConnectionCtx::GetInstance()->AddPeerConnection(mHandle, this);

  mGatheringStateChangeListener =
      mTransportHandler->GetGatheringStateChange().Connect(
          GetMainThreadSerialEventTarget(), this,
          &PeerConnectionImpl::IceGatheringStateChange);
  mConnectionStateChangeListener =
      mTransportHandler->GetConnectionStateChange().Connect(
          GetMainThreadSerialEventTarget(), this,
          &PeerConnectionImpl::IceConnectionStateChange);
  mCandidateListener = mTransportHandler->GetCandidateGathered().Connect(
      GetMainThreadSerialEventTarget(), this,
      &PeerConnectionImpl::OnCandidateFound);
  mCandidateErrorListener = mTransportHandler->GetCandidateError().Connect(
      GetMainThreadSerialEventTarget(), this,
      &PeerConnectionImpl::OnCandidateError);
  mAlpnNegotiatedListener = mTransportHandler->GetAlpnNegotiated().Connect(
      GetMainThreadSerialEventTarget(), this,
      &PeerConnectionImpl::OnAlpnNegotiated);
  mStateChangeListener = mTransportHandler->GetStateChange().Connect(
      GetMainThreadSerialEventTarget(), this,
      &PeerConnectionImpl::OnDtlsStateChange);
  mRtcpStateChangeListener = mTransportHandler->GetRtcpStateChange().Connect(
      GetMainThreadSerialEventTarget(), this,
      &PeerConnectionImpl::OnRtcpStateChange);

  return NS_OK;
}

void PeerConnectionImpl::Initialize(PeerConnectionObserver& aObserver,
                                    nsGlobalWindowInner& aWindow,
                                    ErrorResult& rv) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult res = Initialize(aObserver, &aWindow);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return;
  }
}

void PeerConnectionImpl::SetCertificate(
    mozilla::dom::RTCCertificate& aCertificate) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(!mCertificate, "This can only be called once");
  mCertificate = &aCertificate;

  std::vector<uint8_t> fingerprint;
  nsresult rv =
      CalculateFingerprint(DtlsIdentity::DEFAULT_HASH_ALGORITHM, &fingerprint);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: Couldn't calculate fingerprint, rv=%u",
                __FUNCTION__, static_cast<unsigned>(rv));
    mCertificate = nullptr;
    return;
  }
  rv = mJsepSession->AddDtlsFingerprint(DtlsIdentity::DEFAULT_HASH_ALGORITHM,
                                        fingerprint);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: Couldn't set DTLS credentials, rv=%u",
                __FUNCTION__, static_cast<unsigned>(rv));
    mCertificate = nullptr;
  }

  if (mUncommittedJsepSession) {
    (void)mUncommittedJsepSession->AddDtlsFingerprint(
        DtlsIdentity::DEFAULT_HASH_ALGORITHM, fingerprint);
  }
}

const RefPtr<mozilla::dom::RTCCertificate>& PeerConnectionImpl::Certificate()
    const {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  return mCertificate;
}

RefPtr<DtlsIdentity> PeerConnectionImpl::Identity() const {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(mCertificate);
  return mCertificate->CreateDtlsIdentity();
}

// Data channels won't work without a window, so in order for the C++ unit
// tests to work (it doesn't have a window available) we ifdef the following
// two implementations.
//
// Note: 'media.peerconnection.sctp.force_maximum_message_size' changes
// behaviour triggered by these parameters.
NS_IMETHODIMP
PeerConnectionImpl::EnsureDataConnection(uint16_t aLocalPort,
                                         uint16_t aNumstreams) {
  PC_AUTO_ENTER_API_CALL(false);

  if (mDataConnection) {
    CSFLogDebug(LOGTAG, "%s DataConnection already connected", __FUNCTION__);
    return NS_OK;
  }

  nsCOMPtr<nsISerialEventTarget> target = GetMainThreadSerialEventTarget();
  if (auto res = DataChannelConnection::Create(this, target, mTransportHandler,
                                               aLocalPort, aNumstreams)) {
    mDataConnection = res.value();
    CSFLogDebug(LOGTAG, "%s DataChannelConnection %p attached to %s",
                __FUNCTION__, (void*)mDataConnection.get(), mHandle.c_str());
    return NS_OK;
  }
  CSFLogError(LOGTAG, "%s DataConnection Create Failed", __FUNCTION__);
  return NS_ERROR_FAILURE;
}

nsresult PeerConnectionImpl::GetDatachannelParameters(
    uint32_t* channels, uint16_t* localport, uint16_t* remoteport,
    uint32_t* remotemaxmessagesize, std::string* transportId,
    bool* client) const {
  // Clear, just in case we fail.
  *channels = 0;
  *localport = 0;
  *remoteport = 0;
  *remotemaxmessagesize = 0;
  transportId->clear();

  Maybe<const JsepTransceiver> datachannelTransceiver =
      mJsepSession->FindTransceiver([](const JsepTransceiver& aTransceiver) {
        return aTransceiver.GetMediaType() == SdpMediaSection::kApplication;
      });

  if (!datachannelTransceiver ||
      !datachannelTransceiver->mTransport.mComponents ||
      !datachannelTransceiver->mTransport.mDtls ||
      !datachannelTransceiver->mSendTrack.GetNegotiatedDetails()) {
    return NS_ERROR_FAILURE;
  }

  // This will release assert if there is no such index, and that's ok
  const JsepTrackEncoding& encoding =
      datachannelTransceiver->mSendTrack.GetNegotiatedDetails()->GetEncoding(0);

  if (NS_WARN_IF(encoding.GetCodecs().empty())) {
    CSFLogError(LOGTAG,
                "%s: Negotiated m=application with no codec. "
                "This is likely to be broken.",
                __FUNCTION__);
    return NS_ERROR_FAILURE;
  }

  for (const auto& codec : encoding.GetCodecs()) {
    if (codec->Type() != SdpMediaSection::kApplication) {
      CSFLogError(LOGTAG,
                  "%s: Codec type for m=application was %u, this "
                  "is a bug.",
                  __FUNCTION__, static_cast<unsigned>(codec->Type()));
      MOZ_ASSERT(false, "Codec for m=application was not \"application\"");
      return NS_ERROR_FAILURE;
    }

    if (codec->mName != "webrtc-datachannel") {
      CSFLogWarn(LOGTAG,
                 "%s: Codec for m=application was not "
                 "webrtc-datachannel (was instead %s). ",
                 __FUNCTION__, codec->mName.c_str());
      continue;
    }

    if (codec->mChannels) {
      *channels = codec->mChannels;
    } else {
      *channels = std::clamp(
          Preferences::GetInt("media.peerconnection.sctp.default_max_streams",
                              WEBRTC_DATACHANNEL_STREAMS_DEFAULT),
          256, 2048);
      ;
    }
    const JsepApplicationCodecDescription* appCodec =
        static_cast<const JsepApplicationCodecDescription*>(codec.get());
    *localport = appCodec->mLocalPort;
    *remoteport = appCodec->mRemotePort;
    *remotemaxmessagesize = appCodec->mRemoteMaxMessageSize;
    MOZ_ASSERT(!datachannelTransceiver->mTransport.mTransportId.empty());
    *transportId = datachannelTransceiver->mTransport.mTransportId;
    *client = datachannelTransceiver->mTransport.mDtls->GetRole() ==
              JsepDtlsTransport::kJsepDtlsClient;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

nsresult PeerConnectionImpl::AddRtpTransceiverToJsepSession(
    JsepTransceiver& transceiver) {
  mJsepSession->AddTransceiver(transceiver);
  return NS_OK;
}

static Maybe<SdpMediaSection::MediaType> ToSdpMediaType(
    const nsAString& aKind) {
  if (aKind.EqualsASCII("audio")) {
    return Some(SdpMediaSection::MediaType::kAudio);
  } else if (aKind.EqualsASCII("video")) {
    return Some(SdpMediaSection::MediaType::kVideo);
  }
  return Nothing();
}

already_AddRefed<RTCRtpTransceiver> PeerConnectionImpl::AddTransceiver(
    const dom::RTCRtpTransceiverInit& aInit, const nsAString& aKind,
    dom::MediaStreamTrack* aSendTrack, bool aAddTrackMagic, ErrorResult& aRv) {
  // Copy, because we might need to modify
  RTCRtpTransceiverInit init(aInit);

  Maybe<SdpMediaSection::MediaType> type = ToSdpMediaType(aKind);
  if (NS_WARN_IF(!type.isSome())) {
    MOZ_ASSERT(false, "Invalid media kind");
    aRv = NS_ERROR_INVALID_ARG;
    return nullptr;
  }

  JsepTransceiver jsepTransceiver(*type, *mUuidGen);
  jsepTransceiver.SetRtxIsAllowed(mRtxIsAllowed);

  // Do this last, since it is not possible to roll back.
  nsresult rv = AddRtpTransceiverToJsepSession(jsepTransceiver);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: AddRtpTransceiverToJsepSession failed, res=%u",
                __FUNCTION__, static_cast<unsigned>(rv));
    aRv = rv;
    return nullptr;
  }

  auto& sendEncodings = init.mSendEncodings;

  // See https://www.w3.org/TR/webrtc/#dom-rtcrtpsender-setparameters step 11
  // Also see https://bugzilla.mozilla.org/show_bug.cgi?id=1968828
  auto getCapabilitiesResult = dom::Nullable<dom::RTCRtpCapabilities>();
  GetCapabilities(aKind, getCapabilitiesResult, sdp::Direction::kSend);
  MOZ_ASSERT(!getCapabilitiesResult.IsNull());
  if (NS_WARN_IF(getCapabilitiesResult.IsNull())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  const auto& codecs = getCapabilitiesResult.Value().mCodecs;
  for (const auto& encoding : sendEncodings) {
    bool found = false;
    if (encoding.mCodec.WasPassed()) {
      for (const auto& codec : codecs) {
        if (DoesCodecParameterMatchCodec(encoding.mCodec.Value(), codec)) {
          found = true;
          break;
        }
      }
      if (!found) {
        const auto mime =
            NS_LossyConvertUTF16toASCII(encoding.mCodec.Value().mMimeType);
        std::stringstream ss;
        ss << "Codec " << mime
           << " does not match any codec "
              "in GetCapabilities";
        nsCString errorStr(ss.str().c_str());
        aRv.ThrowOperationError(errorStr);
        return nullptr;
      }
    }
  }

  // CheckAndRectifyEncodings covers these six:
  // If any encoding contains a rid member whose value does not conform to the
  // grammar requirements specified in Section 10 of [RFC8851], throw a
  // TypeError.

  // If some but not all encodings contain a rid member, throw a TypeError.

  // If any encoding contains a rid member whose value is the same as that of a
  // rid contained in another encoding in sendEncodings, throw a TypeError.

  // If kind is "audio", remove the scaleResolutionDownBy member from all
  // encodings that contain one.

  // If any encoding contains a scaleResolutionDownBy member whose value is
  // less than 1.0, throw a RangeError.

  // Verify that the value of each maxFramerate member in sendEncodings that is
  // defined is greater than 0.0. If one of the maxFramerate values does not
  // meet this requirement, throw a RangeError.

  RTCRtpSender::CheckAndRectifyEncodings(
      sendEncodings, *type == SdpMediaSection::kVideo,
      // No codecs until after negotiation
      Optional<Sequence<RTCRtpCodecParameters>>(), false, false,
      MatchGetCapabilities::NO, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // If any encoding contains a read-only parameter other than rid, throw an
  // InvalidAccessError.
  // NOTE: We don't support any additional read-only params right now. Also,
  // spec shoehorns this in between checks that setParameters also performs
  // (between the rid checks and the scaleResolutionDownBy checks).

  // If any encoding contains a scaleResolutionDownBy member, then for each
  // encoding without one, add a scaleResolutionDownBy member with the value
  // 1.0.
  for (const auto& constEncoding : sendEncodings) {
    if (constEncoding.mScaleResolutionDownBy.WasPassed()) {
      for (auto& encoding : sendEncodings) {
        if (!encoding.mScaleResolutionDownBy.WasPassed()) {
          encoding.mScaleResolutionDownBy.Construct(1.0f);
        }
      }
      break;
    }
  }

  // Let maxN be the maximum number of total simultaneous encodings the user
  // agent may support for this kind, at minimum 1.This should be an optimistic
  // number since the codec to be used is not known yet.
  size_t maxN =
      (*type == SdpMediaSection::kVideo) ? webrtc::kMaxSimulcastStreams : 1;

  // If the number of encodings stored in sendEncodings exceeds maxN, then trim
  // sendEncodings from the tail until its length is maxN.
  // NOTE: Spec has this after all validation steps; even if there are elements
  // that we will trim off, we still validate them.
  if (sendEncodings.Length() > maxN) {
    sendEncodings.TruncateLength(maxN);
  }

  // If kind is "video" and none of the encodings contain a
  // scaleResolutionDownBy member, then for each encoding, add a
  // scaleResolutionDownBy member with the value 2^(length of sendEncodings -
  // encoding index - 1). This results in smaller-to-larger resolutions where
  // the last encoding has no scaling applied to it, e.g. 4:2:1 if the length
  // is 3.
  // NOTE: The code above ensures that these are all set, or all unset, so we
  // can just check the first one.
  if (sendEncodings.Length() && *type == SdpMediaSection::kVideo &&
      !sendEncodings[0].mScaleResolutionDownBy.WasPassed()) {
    double scale = 1.0f;
    for (auto it = sendEncodings.rbegin(); it != sendEncodings.rend(); ++it) {
      it->mScaleResolutionDownBy.Construct(scale);
      scale *= 2;
    }
  }

  // If the number of encodings now stored in sendEncodings is 1, then remove
  // any rid member from the lone entry.
  if (sendEncodings.Length() == 1) {
    sendEncodings[0].mRid.Reset();
  }

  RefPtr<RTCRtpTransceiver> transceiver = CreateTransceiver(
      jsepTransceiver.GetUuid(),
      jsepTransceiver.GetMediaType() == SdpMediaSection::kVideo, init,
      aSendTrack, aAddTrackMagic, aRv);

  if (aRv.Failed()) {
    // Would be nice if we could peek at the rv without stealing it, so we
    // could log...
    CSFLogError(LOGTAG, "%s: failed", __FUNCTION__);
    return nullptr;
  }

  mTransceivers.AppendElement(transceiver);
  return transceiver.forget();
}

bool PeerConnectionImpl::CheckNegotiationNeeded() {
  MOZ_ASSERT(mSignalingState == RTCSignalingState::Stable);
  SyncToJsep();

  // mDataConnection exists when a datachannel was created or an m-section for
  // data was negotiated.
  if (mDataConnection) {
    Maybe<const JsepTransceiver> dcTransceiver =
        mJsepSession->FindTransceiver([](const JsepTransceiver& aTransceiver) {
          return aTransceiver.GetMediaType() == SdpMediaSection::kApplication;
        });
    if (!dcTransceiver || !dcTransceiver->IsNegotiated()) {
      return true;
    }
  }

  return !mLocalIceCredentialsToReplace.empty() ||
         mJsepSession->CheckNegotiationNeeded();
}

bool PeerConnectionImpl::CreatedSender(const dom::RTCRtpSender& aSender) const {
  return aSender.IsMyPc(this);
}

nsresult PeerConnectionImpl::MaybeInitializeDataChannel() {
  PC_AUTO_ENTER_API_CALL(false);
  CSFLogDebug(LOGTAG, "%s", __FUNCTION__);

  uint32_t channels = 0;
  uint16_t localport = 0;
  uint16_t remoteport = 0;
  uint32_t remotemaxmessagesize = 0;
  std::string transportId;
  bool client = false;
  nsresult rv =
      GetDatachannelParameters(&channels, &localport, &remoteport,
                               &remotemaxmessagesize, &transportId, &client);

  if (NS_FAILED(rv)) {
    CSFLogDebug(LOGTAG, "%s: We did not negotiate datachannel", __FUNCTION__);
    return NS_OK;
  }

  if (channels > MAX_NUM_STREAMS) {
    channels = MAX_NUM_STREAMS;
  }

  rv = EnsureDataConnection(localport, channels);
  if (NS_SUCCEEDED(rv)) {
    mDataConnection->SetMaxMessageSize(remotemaxmessagesize);
    if (mDataConnection->ConnectToTransport(transportId, client, localport,
                                            remoteport)) {
      return NS_OK;
    }
    // If we inited the DataConnection, call Destroy() before releasing it
    mDataConnection->Destroy();
  }
  mDataConnection = nullptr;
  return NS_ERROR_FAILURE;
}

already_AddRefed<RTCDataChannel> PeerConnectionImpl::CreateDataChannel(
    const nsACString& aLabel, const nsACString& aProtocol, uint16_t aType,
    bool ordered, uint16_t aMaxTime, uint16_t aMaxNum, bool aExternalNegotiated,
    uint16_t aStream, ErrorResult& rv) {
  RefPtr<RTCDataChannel> result;
  rv = CreateDataChannel(aLabel, aProtocol, aType, ordered, aMaxTime, aMaxNum,
                         aExternalNegotiated, aStream, getter_AddRefs(result));
  return result.forget();
}

NS_IMETHODIMP
PeerConnectionImpl::CreateDataChannel(
    const nsACString& aLabel, const nsACString& aProtocol, uint16_t aType,
    bool ordered, uint16_t aMaxTime, uint16_t aMaxNum, bool aExternalNegotiated,
    uint16_t aStream, RTCDataChannel** aRetval) {
  PC_AUTO_ENTER_API_CALL(false);
  MOZ_ASSERT(aRetval);

  RefPtr<DataChannel> dataChannel;
  DataChannelReliabilityPolicy prPolicy;
  Nullable<uint16_t> maxLifeTime;
  Nullable<uint16_t> maxRetransmits;
  switch (aType) {
    case IPeerConnection::kDataChannelReliable:
      prPolicy = DataChannelReliabilityPolicy::Reliable;
      break;
    case IPeerConnection::kDataChannelPartialReliableRexmit:
      prPolicy = DataChannelReliabilityPolicy::LimitedRetransmissions;
      maxRetransmits.SetValue(aMaxNum);
      break;
    case IPeerConnection::kDataChannelPartialReliableTimed:
      prPolicy = DataChannelReliabilityPolicy::LimitedLifetime;
      maxLifeTime.SetValue(aMaxTime);
      break;
    default:
      MOZ_ASSERT(false);
      return NS_ERROR_FAILURE;
  }

  uint16_t maxStreams = std::clamp(
      Preferences::GetInt("media.peerconnection.sctp.default_max_streams",
                          WEBRTC_DATACHANNEL_STREAMS_DEFAULT),
      256, 2048);

  nsresult rv =
      EnsureDataConnection(WEBRTC_DATACHANNEL_PORT_DEFAULT, maxStreams);
  if (NS_FAILED(rv)) {
    return rv;
  }
  dataChannel = mDataConnection->Open(
      aLabel, aProtocol, prPolicy, ordered,
      prPolicy == DataChannelReliabilityPolicy::LimitedRetransmissions
          ? aMaxNum
          : (prPolicy == DataChannelReliabilityPolicy::LimitedLifetime
                 ? aMaxTime
                 : 0),
      aExternalNegotiated, aStream);
  NS_ENSURE_TRUE(dataChannel, NS_ERROR_NOT_AVAILABLE);

  CSFLogDebug(LOGTAG, "%s: making DOMDataChannel", __FUNCTION__);

  Maybe<JsepTransceiver> dcTransceiver =
      mJsepSession->FindTransceiver([](const JsepTransceiver& aTransceiver) {
        return aTransceiver.GetMediaType() == SdpMediaSection::kApplication;
      });

  if (dcTransceiver) {
    dcTransceiver->RestartDatachannelTransceiver();
    mJsepSession->SetTransceiver(*dcTransceiver);
  } else {
    mJsepSession->AddTransceiver(
        JsepTransceiver(SdpMediaSection::MediaType::kApplication, *mUuidGen));
  }

  RefPtr<RTCDataChannel> retval;
  rv = NS_NewDOMDataChannel(dataChannel.forget(), aLabel, mOrigin, ordered,
                            maxLifeTime, maxRetransmits, aProtocol,
                            aExternalNegotiated, this, mWindow,
                            getter_AddRefs(retval));
  if (NS_FAILED(rv)) {
    return rv;
  }
  mMainthreadDatachannels.AppendElement(retval);
  retval.forget(aRetval);
  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION(PeerConnectionImpl::Operation, mPromise, mPc)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PeerConnectionImpl::Operation)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTING_ADDREF(PeerConnectionImpl::Operation)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PeerConnectionImpl::Operation)

PeerConnectionImpl::Operation::Operation(PeerConnectionImpl* aPc,
                                         ErrorResult& aError)
    : mPromise(aPc->MakePromise(aError)), mPc(aPc) {}

PeerConnectionImpl::Operation::~Operation() = default;

void PeerConnectionImpl::Operation::Call(ErrorResult& aError) {
  RefPtr<dom::Promise> opPromise = CallImpl(aError);
  if (aError.Failed()) {
    return;
  }
  // Upon fulfillment or rejection of the promise returned by the operation,
  // run the following steps:
  // (NOTE: mPromise is p from https://w3c.github.io/webrtc-pc/#dfn-chain,
  // and CallImpl() is what returns the promise for the operation itself)
  opPromise->AppendNativeHandler(this);
}

void PeerConnectionImpl::Operation::ResolvedCallback(
    JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult& aRv) {
  // If connection.[[IsClosed]] is true, abort these steps.
  // (the spec wants p to never settle in this event)
  if (!mPc->IsClosed()) {
    // If the promise returned by operation was fulfilled with a
    // value, fulfill p with that value.
    mPromise->MaybeResolveWithClone(aCx, aValue);
    // Upon fulfillment or rejection of p, execute the following
    // steps:
    // (Static analysis forces us to use a temporary)
    RefPtr<PeerConnectionImpl> pc = mPc;
    pc->RunNextOperation(aRv);
  }
}

void PeerConnectionImpl::Operation::RejectedCallback(
    JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult& aRv) {
  // If connection.[[IsClosed]] is true, abort these steps.
  // (the spec wants p to never settle in this event)
  if (!mPc->IsClosed()) {
    // If the promise returned by operation was rejected with a
    // value, reject p with that value.
    mPromise->MaybeRejectWithClone(aCx, aValue);
    // Upon fulfillment or rejection of p, execute the following
    // steps:
    // (Static analysis forces us to use a temporary)
    RefPtr<PeerConnectionImpl> pc = mPc;
    pc->RunNextOperation(aRv);
  }
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(PeerConnectionImpl::JSOperation,
                                   PeerConnectionImpl::Operation, mOperation)

NS_IMPL_ADDREF_INHERITED(PeerConnectionImpl::JSOperation,
                         PeerConnectionImpl::Operation)
NS_IMPL_RELEASE_INHERITED(PeerConnectionImpl::JSOperation,
                          PeerConnectionImpl::Operation)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PeerConnectionImpl::JSOperation)
NS_INTERFACE_MAP_END_INHERITING(PeerConnectionImpl::Operation)

PeerConnectionImpl::JSOperation::JSOperation(PeerConnectionImpl* aPc,
                                             dom::ChainedOperation& aOp,
                                             ErrorResult& aError)
    : Operation(aPc, aError), mOperation(&aOp) {}

RefPtr<dom::Promise> PeerConnectionImpl::JSOperation::CallImpl(
    ErrorResult& aError) {
  // Static analysis will not let us call this without a temporary :(
  RefPtr<dom::ChainedOperation> op = mOperation;
  return op->Call(aError);
}

already_AddRefed<dom::Promise> PeerConnectionImpl::Chain(
    dom::ChainedOperation& aOperation, ErrorResult& aError) {
  MOZ_RELEASE_ASSERT(!mChainingOperation);
  mChainingOperation = true;
  RefPtr operation = MakeRefPtr<JSOperation>(this, aOperation, aError);
  if (aError.Failed()) {
    return nullptr;
  }
  RefPtr<Promise> promise = Chain(operation, aError);
  if (aError.Failed()) {
    return nullptr;
  }
  mChainingOperation = false;
  return promise.forget();
}

// This is kinda complicated, but it is what the spec requires us to do. The
// core of what makes this complicated is the requirement that |aOperation| be
// run _immediately_ (without any Promise.Then!) if the operations chain is
// empty.
already_AddRefed<dom::Promise> PeerConnectionImpl::Chain(
    const RefPtr<Operation>& aOperation, ErrorResult& aError) {
  // If connection.[[IsClosed]] is true, return a promise rejected with a newly
  // created InvalidStateError.
  if (IsClosed()) {
    CSFLogDebug(LOGTAG, "%s:%d: Peer connection is closed", __FILE__, __LINE__);
    RefPtr<dom::Promise> error = MakePromise(aError);
    if (aError.Failed()) {
      return nullptr;
    }
    error->MaybeRejectWithInvalidStateError("Peer connection is closed");
    return error.forget();
  }

  // Append operation to [[Operations]].
  mOperations.AppendElement(aOperation);

  // If the length of [[Operations]] is exactly 1, execute operation.
  if (mOperations.Length() == 1) {
    aOperation->Call(aError);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  // This is the promise p from https://w3c.github.io/webrtc-pc/#dfn-chain
  return do_AddRef(aOperation->GetPromise());
}

void PeerConnectionImpl::RunNextOperation(ErrorResult& aError) {
  // If connection.[[IsClosed]] is true, abort these steps.
  if (IsClosed()) {
    return;
  }

  // Remove the first element of [[Operations]].
  mOperations.RemoveElementAt(0);

  // If [[Operations]] is non-empty, execute the operation represented by the
  // first element of [[Operations]], and abort these steps.
  if (mOperations.Length()) {
    // Cannot call without a temporary :(
    RefPtr<Operation> op = mOperations[0];
    op->Call(aError);
    return;
  }

  // If connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] is false, abort
  // these steps.
  if (!mUpdateNegotiationNeededFlagOnEmptyChain) {
    return;
  }

  // Set connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] to false.
  mUpdateNegotiationNeededFlagOnEmptyChain = false;
  // Update the negotiation-needed flag for connection.
  UpdateNegotiationNeeded();
}

void PeerConnectionImpl::SyncToJsep() {
  for (const auto& transceiver : mTransceivers) {
    transceiver->SyncToJsep(*mJsepSession);
  }
}

void PeerConnectionImpl::SyncFromJsep() {
  CSFLogDebug(LOGTAG, "%s", __FUNCTION__);
  mJsepSession->ForEachTransceiver(
      [this, self = RefPtr<PeerConnectionImpl>(this)](
          const JsepTransceiver& jsepTransceiver) {
        if (jsepTransceiver.GetMediaType() ==
            SdpMediaSection::MediaType::kApplication) {
          return;
        }

        CSFLogDebug(LOGTAG, "%s: Looking for match", __FUNCTION__);
        RefPtr<RTCRtpTransceiver> transceiver;
        for (auto& temp : mTransceivers) {
          if (temp->GetJsepTransceiverId() == jsepTransceiver.GetUuid()) {
            CSFLogDebug(LOGTAG, "%s: Found match", __FUNCTION__);
            transceiver = temp;
            break;
          }
        }

        if (!transceiver) {
          if (jsepTransceiver.IsRemoved()) {
            return;
          }
          CSFLogDebug(LOGTAG, "%s: No match, making new", __FUNCTION__);
          dom::RTCRtpTransceiverInit init;
          init.mDirection = RTCRtpTransceiverDirection::Recvonly;
          IgnoredErrorResult rv;
          transceiver = CreateTransceiver(
              jsepTransceiver.GetUuid(),
              jsepTransceiver.GetMediaType() == SdpMediaSection::kVideo, init,
              nullptr, false, rv);
          if (NS_WARN_IF(rv.Failed())) {
            MOZ_ASSERT(false);
            return;
          }
          mTransceivers.AppendElement(transceiver);
        }

        CSFLogDebug(LOGTAG, "%s: Syncing transceiver", __FUNCTION__);
        transceiver->SyncFromJsep(*mJsepSession);
      });
}

already_AddRefed<dom::Promise> PeerConnectionImpl::MakePromise(
    ErrorResult& aError) const {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
  return dom::Promise::Create(global, aError);
}

void PeerConnectionImpl::UpdateNegotiationNeeded() {
  // If the length of connection.[[Operations]] is not 0, then set
  // connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] to true, and abort
  // these steps.
  if (mOperations.Length() != 0) {
    mUpdateNegotiationNeededFlagOnEmptyChain = true;
    return;
  }

  // Queue a task to run the following steps:
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<PeerConnectionImpl>(this)] {
        // If connection.[[IsClosed]] is true, abort these steps.
        if (IsClosed()) {
          return;
        }
        // If the length of connection.[[Operations]] is not 0, then set
        // connection.[[UpdateNegotiationNeededFlagOnEmptyChain]] to true, and
        // abort these steps.
        if (mOperations.Length()) {
          mUpdateNegotiationNeededFlagOnEmptyChain = true;
          return;
        }
        // If connection's signaling state is not "stable", abort these steps.
        if (mSignalingState != RTCSignalingState::Stable) {
          return;
        }
        // If the result of checking if negotiation is needed is false, clear
        // the negotiation-needed flag by setting
        // connection.[[NegotiationNeeded]] to false, and abort these steps.
        if (!CheckNegotiationNeeded()) {
          mNegotiationNeeded = false;
          return;
        }

        // If connection.[[NegotiationNeeded]] is already true, abort these
        // steps.
        if (mNegotiationNeeded) {
          return;
        }

        // Set connection.[[NegotiationNeeded]] to true.
        mNegotiationNeeded = true;

        // Fire an event named negotiationneeded at connection.
        ErrorResult rv;
        mPCObserver->FireNegotiationNeededEvent(rv);
      }));
}

RefPtr<dom::RTCRtpTransceiver> PeerConnectionImpl::GetTransceiver(
    const std::string& aTransceiverId) {
  for (const auto& transceiver : mTransceivers) {
    if (transceiver->GetJsepTransceiverId() == aTransceiverId) {
      return transceiver;
    }
  }
  return nullptr;
}

void PeerConnectionImpl::NotifyDataChannel(
    already_AddRefed<DataChannel> aChannel, const nsACString& aLabel,
    bool aOrdered, mozilla::dom::Nullable<uint16_t> aMaxLifeTime,
    mozilla::dom::Nullable<uint16_t> aMaxRetransmits,
    const nsACString& aProtocol, bool aNegotiated) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();

  RefPtr<DataChannel> channel(aChannel);
  MOZ_ASSERT(channel);
  CSFLogDebug(LOGTAG, "%s: channel: %p", __FUNCTION__, channel.get());

  RefPtr<RTCDataChannel> domchannel;
  nsresult rv = NS_NewDOMDataChannel(channel.forget(), aLabel, mOrigin,
                                     aOrdered, aMaxLifeTime, aMaxRetransmits,
                                     aProtocol, aNegotiated, this, mWindow,
                                     getter_AddRefs(domchannel));
  NS_ENSURE_SUCCESS_VOID(rv);
  mMainthreadDatachannels.AppendElement(domchannel);

  domchannel->SetReadyState(RTCDataChannelState::Open);

  JSErrorResult jrv;
  mPCObserver->NotifyDataChannel(*domchannel, jrv);
}

void PeerConnectionImpl::NotifyDataChannelOpen(DataChannel*) {
  mDataChannelsOpened++;
}

void PeerConnectionImpl::NotifyDataChannelClosed(DataChannel*) {
  mDataChannelsClosed++;
}

void PeerConnectionImpl::NotifySctpConnected(Maybe<uint16_t> aMaxChannels) {
  if (!mSctpTransport) {
    MOZ_ASSERT(false);
    return;
  }

  // Set [[MaxChannels]] to the minimum of the negotiated amount of incoming
  // and outgoing SCTP streams.
  if (aMaxChannels.isSome()) {
    mSctpTransport->SetMaxChannels(Nullable<uint16_t>(*aMaxChannels));
  }

  // Strictly speaking, the step for updating the RTCDataChannel objects is
  // supposed to go here, but that is handled over in the DataChannelConnection
  // code. The current spec step for this is busted because "Let channel be the
  // RTCDataChannel object." cannot even be done with worker datachannels here.
  // The timing ought to work out fine though, since those RTCDataChannel
  // updates are speced to happen in queued tasks, and DataChannelConnection
  // does that. It should not matter whether maxChannels is updated before or
  // after those tasks are queued.

  // Fire an event named statechange at transport.
  mSctpTransport->UpdateState(RTCSctpTransportState::Connected);
}

void PeerConnectionImpl::NotifySctpClosed() {
  if (!mSctpTransport) {
    MOZ_ASSERT(false);
    return;
  }

  mSctpTransport->UpdateState(RTCSctpTransportState::Closed);
}

NS_IMETHODIMP
PeerConnectionImpl::CreateOffer(const RTCOfferOptions& aOptions) {
  JsepOfferOptions options;
  // convert the RTCOfferOptions to JsepOfferOptions
  if (aOptions.mOfferToReceiveAudio.WasPassed()) {
    options.mOfferToReceiveAudio =
        mozilla::Some(size_t(aOptions.mOfferToReceiveAudio.Value()));
  }

  if (aOptions.mOfferToReceiveVideo.WasPassed()) {
    options.mOfferToReceiveVideo =
        mozilla::Some(size_t(aOptions.mOfferToReceiveVideo.Value()));
  }

  options.mIceRestart = mozilla::Some(aOptions.mIceRestart ||
                                      !mLocalIceCredentialsToReplace.empty());

  return CreateOffer(options);
}

static void DeferredCreateOffer(const std::string& aPcHandle,
                                const JsepOfferOptions& aOptions) {
  PeerConnectionWrapper wrapper(aPcHandle);

  if (wrapper.impl()) {
    if (!PeerConnectionCtx::GetInstance()->isReady()) {
      MOZ_CRASH(
          "Why is DeferredCreateOffer being executed when the "
          "PeerConnectionCtx isn't ready?");
    }
    wrapper.impl()->CreateOffer(aOptions);
  }
}

// Have to use unique_ptr because webidl enums are generated without a
// copy c'tor.
static std::unique_ptr<dom::PCErrorData> buildJSErrorData(
    const JsepSession::Result& aResult, const std::string& aMessage) {
  auto result = std::make_unique<dom::PCErrorData>();
  result->mName = *aResult.mError;
  result->mMessage = NS_ConvertASCIItoUTF16(aMessage.c_str());
  // Populate RTCError-specific fields when the error has an errorDetail.
  if (aResult.mErrorDetail.isSome()) {
    result->mErrorDetail.Construct(
        NS_ConvertASCIItoUTF16(aResult.mErrorDetail->c_str()));
    if (aResult.mSdpLineNumber.isSome()) {
      result->mSdpLineNumber.Construct((int32_t)aResult.mSdpLineNumber.value());
    }
  }
  return result;
}

// Used by unit tests and the IDL CreateOffer.
NS_IMETHODIMP
PeerConnectionImpl::CreateOffer(const JsepOfferOptions& aOptions) {
  PC_AUTO_ENTER_API_CALL(true);

  if (!PeerConnectionCtx::GetInstance()->isReady()) {
    // Uh oh. We're not ready yet. Enqueue this operation.
    PeerConnectionCtx::GetInstance()->queueJSEPOperation(
        WrapRunnableNM(DeferredCreateOffer, mHandle, aOptions));
    STAMP_TIMECARD(mTimeCard, "Deferring CreateOffer (not ready)");
    return NS_OK;
  }

  CSFLogDebug(LOGTAG, "CreateOffer()");
  STAMP_TIMECARD(mTimeCard, "Create Offer");

  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<PeerConnectionImpl>(this), aOptions] {
        std::string offer;

        SyncToJsep();
        UniquePtr<JsepSession> uncommittedJsepSession(mJsepSession->Clone());
        JsepSession::Result result =
            uncommittedJsepSession->CreateOffer(aOptions, &offer);
        JSErrorResult rv;
        if (result.mError.isSome()) {
          std::string errorString = uncommittedJsepSession->GetLastError();

          CSFLogError(LOGTAG, "%s: pc = %s, error = %s", __FUNCTION__,
                      mHandle.c_str(), errorString.c_str());

          mPCObserver->OnCreateOfferError(
              *buildJSErrorData(result, errorString), rv);
        } else {
          mJsepSession = std::move(uncommittedJsepSession);
          mPCObserver->OnCreateOfferSuccess(ObString(offer.c_str()), rv);
        }
      }));

  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CreateAnswer() {
  PC_AUTO_ENTER_API_CALL(true);

  CSFLogDebug(LOGTAG, "CreateAnswer()");

  STAMP_TIMECARD(mTimeCard, "Create Answer");
  // TODO(bug 1098015): Once RTCAnswerOptions is standardized, we'll need to
  // add it as a param to CreateAnswer, and convert it here.
  JsepAnswerOptions options;

  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<PeerConnectionImpl>(this), options] {
        std::string answer;
        SyncToJsep();
        UniquePtr<JsepSession> uncommittedJsepSession(mJsepSession->Clone());
        JsepSession::Result result =
            uncommittedJsepSession->CreateAnswer(options, &answer);
        JSErrorResult rv;
        if (result.mError.isSome()) {
          std::string errorString = uncommittedJsepSession->GetLastError();

          CSFLogError(LOGTAG, "%s: pc = %s, error = %s", __FUNCTION__,
                      mHandle.c_str(), errorString.c_str());

          mPCObserver->OnCreateAnswerError(
              *buildJSErrorData(result, errorString), rv);
        } else {
          mJsepSession = std::move(uncommittedJsepSession);
          mPCObserver->OnCreateAnswerSuccess(ObString(answer.c_str()), rv);
        }
      }));

  return NS_OK;
}

dom::RTCSdpType ToDomSdpType(JsepSdpType aType) {
  switch (aType) {
    case kJsepSdpOffer:
      return dom::RTCSdpType::Offer;
    case kJsepSdpAnswer:
      return dom::RTCSdpType::Answer;
    case kJsepSdpPranswer:
      return dom::RTCSdpType::Pranswer;
    case kJsepSdpRollback:
      return dom::RTCSdpType::Rollback;
  }

  MOZ_CRASH("Nonexistent JsepSdpType");
}

JsepSdpType ToJsepSdpType(dom::RTCSdpType aType) {
  switch (aType) {
    case dom::RTCSdpType::Offer:
      return kJsepSdpOffer;
    case dom::RTCSdpType::Pranswer:
      return kJsepSdpPranswer;
    case dom::RTCSdpType::Answer:
      return kJsepSdpAnswer;
    case dom::RTCSdpType::Rollback:
      return kJsepSdpRollback;
  }

  MOZ_CRASH("Nonexistent dom::RTCSdpType");
}

NS_IMETHODIMP
PeerConnectionImpl::SetLocalDescription(int32_t aAction, const char* aSDP) {
  PC_AUTO_ENTER_API_CALL(true);

  if (!aSDP) {
    CSFLogError(LOGTAG, "%s - aSDP is NULL", __FUNCTION__);
    return NS_ERROR_FAILURE;
  }

  STAMP_TIMECARD(mTimeCard, "Set Local Description");

  if (AnyLocalTrackHasPeerIdentity()) {
    mRequestedPrivacy = Some(PrincipalPrivacy::Private);
  }

  mozilla::dom::RTCSdpHistoryEntryInternal sdpEntry;
  sdpEntry.mIsLocal = true;
  sdpEntry.mTimestamp = mTimestampMaker.GetNow().ToDom();
  sdpEntry.mSdp = NS_ConvertASCIItoUTF16(aSDP);
  auto appendHistory = [&]() {
    if (!mSdpHistory.AppendElement(sdpEntry, fallible)) {
      mozalloc_handle_oom(0);
    }
  };

  mLocalRequestedSDP = aSDP;

  SyncToJsep();

  bool wasRestartingIce = mJsepSession->IsIceRestarting();
  JsepSdpType sdpType;
  switch (aAction) {
    case IPeerConnection::kActionOffer:
      sdpType = mozilla::kJsepSdpOffer;
      break;
    case IPeerConnection::kActionAnswer:
      sdpType = mozilla::kJsepSdpAnswer;
      break;
    case IPeerConnection::kActionPRAnswer:
      sdpType = mozilla::kJsepSdpPranswer;
      break;
    case IPeerConnection::kActionRollback:
      sdpType = mozilla::kJsepSdpRollback;
      break;
    default:
      MOZ_ASSERT(false);
      appendHistory();
      return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(!mUncommittedJsepSession);
  mUncommittedJsepSession = mJsepSession->Clone();
  JsepSession::Result result =
      mUncommittedJsepSession->SetLocalDescription(sdpType, mLocalRequestedSDP);
  JSErrorResult rv;
  if (result.mError.isSome()) {
    std::string errorString = mUncommittedJsepSession->GetLastError();
    mUncommittedJsepSession = nullptr;
    CSFLogError(LOGTAG, "%s: pc = %s, error = %s", __FUNCTION__,
                mHandle.c_str(), errorString.c_str());
    mPCObserver->OnSetDescriptionError(*buildJSErrorData(result, errorString),
                                       rv);
    sdpEntry.mErrors = GetLastSdpParsingErrors();
  } else {
    if (wasRestartingIce) {
      RecordIceRestartStatistics(sdpType);
    }

    mPCObserver->OnSetDescriptionSuccess(rv);
  }

  appendHistory();

  if (rv.Failed()) {
    return rv.StealNSResult();
  }

  return NS_OK;
}

static void DeferredSetRemote(const std::string& aPcHandle, int32_t aAction,
                              const std::string& aSdp) {
  PeerConnectionWrapper wrapper(aPcHandle);

  if (wrapper.impl()) {
    if (!PeerConnectionCtx::GetInstance()->isReady()) {
      MOZ_CRASH(
          "Why is DeferredSetRemote being executed when the "
          "PeerConnectionCtx isn't ready?");
    }
    wrapper.impl()->SetRemoteDescription(aAction, aSdp.c_str());
  }
}

NS_IMETHODIMP
PeerConnectionImpl::SetRemoteDescription(int32_t action, const char* aSDP) {
  PC_AUTO_ENTER_API_CALL(true);

  if (!aSDP) {
    CSFLogError(LOGTAG, "%s - aSDP is NULL", __FUNCTION__);
    return NS_ERROR_FAILURE;
  }

  if (action == IPeerConnection::kActionOffer) {
    if (!PeerConnectionCtx::GetInstance()->isReady()) {
      // Uh oh. We're not ready yet. Enqueue this operation. (This must be a
      // remote offer, or else we would not have gotten this far)
      PeerConnectionCtx::GetInstance()->queueJSEPOperation(WrapRunnableNM(
          DeferredSetRemote, mHandle, action, std::string(aSDP)));
      STAMP_TIMECARD(mTimeCard, "Deferring SetRemote (not ready)");
      return NS_OK;
    }
  }

  STAMP_TIMECARD(mTimeCard, "Set Remote Description");

  mozilla::dom::RTCSdpHistoryEntryInternal sdpEntry;
  sdpEntry.mIsLocal = false;
  sdpEntry.mTimestamp = mTimestampMaker.GetNow().ToDom();
  sdpEntry.mSdp = NS_ConvertASCIItoUTF16(aSDP);
  auto appendHistory = [&]() {
    if (!mSdpHistory.AppendElement(sdpEntry, fallible)) {
      mozalloc_handle_oom(0);
    }
  };

  SyncToJsep();

  mRemoteRequestedSDP = aSDP;
  bool wasRestartingIce = mJsepSession->IsIceRestarting();
  JsepSdpType sdpType;
  switch (action) {
    case IPeerConnection::kActionOffer:
      sdpType = mozilla::kJsepSdpOffer;
      break;
    case IPeerConnection::kActionAnswer:
      sdpType = mozilla::kJsepSdpAnswer;
      break;
    case IPeerConnection::kActionPRAnswer:
      sdpType = mozilla::kJsepSdpPranswer;
      break;
    case IPeerConnection::kActionRollback:
      sdpType = mozilla::kJsepSdpRollback;
      break;
    default:
      MOZ_ASSERT(false);
      return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(!mUncommittedJsepSession);
  mUncommittedJsepSession = mJsepSession->Clone();
  JsepSession::Result result = mUncommittedJsepSession->SetRemoteDescription(
      sdpType, mRemoteRequestedSDP);
  JSErrorResult jrv;
  if (result.mError.isSome()) {
    std::string errorString = mUncommittedJsepSession->GetLastError();
    mUncommittedJsepSession = nullptr;
    sdpEntry.mErrors = GetLastSdpParsingErrors();
    CSFLogError(LOGTAG, "%s: pc = %s, error = %s", __FUNCTION__,
                mHandle.c_str(), errorString.c_str());
    mPCObserver->OnSetDescriptionError(*buildJSErrorData(result, errorString),
                                       jrv);
  } else {
    if (wasRestartingIce) {
      RecordIceRestartStatistics(sdpType);
    }

    mPCObserver->OnSetDescriptionSuccess(jrv);
  }

  appendHistory();

  if (jrv.Failed()) {
    return jrv.StealNSResult();
  }

  return NS_OK;
}

already_AddRefed<dom::Promise> PeerConnectionImpl::GetStats(
    MediaStreamTrack* aSelector) {
  if (!mWindow) {
    MOZ_CRASH("Cannot create a promise without a window!");
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(global, rv);
  if (NS_WARN_IF(rv.Failed())) {
    MOZ_CRASH("Failed to create a promise!");
  }

  GetStats(aSelector, false)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [promise,
           window = mWindow](UniquePtr<dom::RTCStatsReportInternal>&& aReport) {
            RefPtr report = MakeRefPtr<RTCStatsReport>(window);
            report->Incorporate(*aReport);
            promise->MaybeResolve(std::move(report));
          },
          [promise, window = mWindow](nsresult aError) {
            RefPtr report = MakeRefPtr<RTCStatsReport>(window);
            promise->MaybeResolve(std::move(report));
          });

  return promise.forget();
}

void PeerConnectionImpl::GetRemoteStreams(
    nsTArray<RefPtr<DOMMediaStream>>& aStreamsOut) const {
  aStreamsOut = mReceiveStreams.Clone();
}

NS_IMETHODIMP
PeerConnectionImpl::AddIceCandidate(
    const char* aCandidate, const char* aMid, const char* aUfrag,
    const dom::Nullable<unsigned short>& aLevel) {
  PC_AUTO_ENTER_API_CALL(true);

  // TODO(https://bugzilla.mozilla.org/show_bug.cgi?id=2036944)
  // Tighten this up.

  // We may end up storing an RTCIceCandidate for this when we implement
  // RTCIceTransport.getRemoteCandidates(), in which case it will handle
  // stripping the "a=" off.
  std::string candidate(aCandidate);
  if (candidate.find("a=") == 0) {
    candidate = candidate.substr(2);
  }

  if (mForceIceTcp && std::string::npos != candidate.find(" UDP ")) {
    CSFLogError(LOGTAG, "Blocking remote UDP candidate: %s", candidate.c_str());
    return NS_OK;
  }

  STAMP_TIMECARD(mTimeCard, "Add Ice Candidate");

  CSFLogDebug(LOGTAG, "AddIceCandidate: %s %s", candidate.c_str(), aUfrag);

  std::string transportId;
  Maybe<unsigned short> level;
  if (!aLevel.IsNull()) {
    level = Some(aLevel.Value());
  }
  MOZ_DIAGNOSTIC_ASSERT(
      !mUncommittedJsepSession,
      "AddIceCandidate is chained, which means it should never "
      "run while an sRD/sLD is in progress");
  JsepSession::Result result = mJsepSession->AddRemoteIceCandidate(
      candidate, aMid, level, aUfrag, &transportId);

  if (!result.mError.isSome()) {
    // We do not bother the MediaTransportHandler about this before
    // offer/answer concludes.  Once offer/answer concludes, we will extract
    // these candidates from the remote SDP.
    if (mSignalingState == RTCSignalingState::Stable && !transportId.empty()) {
      AddIceCandidate(candidate, transportId, aUfrag);
      mRawTrickledCandidates.push_back(candidate);
    }
    // Spec says we queue a task for these updates
    GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
        __func__, [this, self = RefPtr<PeerConnectionImpl>(this)] {
          if (IsClosed()) {
            return;
          }
          mPendingRemoteDescription =
              mJsepSession->GetRemoteDescription(kJsepDescriptionPending);
          mCurrentRemoteDescription =
              mJsepSession->GetRemoteDescription(kJsepDescriptionCurrent);
          JSErrorResult rv;
          mPCObserver->OnAddIceCandidateSuccess(rv);
        }));
  } else {
    std::string errorString = mJsepSession->GetLastError();

    CSFLogError(LOGTAG,
                "Failed to incorporate remote candidate into SDP:"
                " res = %u, candidate = %s, level = %i, error = %s",
                static_cast<unsigned>(*result.mError), candidate.c_str(),
                level.valueOr(-1), errorString.c_str());

    GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
        __func__,
        [this, self = RefPtr<PeerConnectionImpl>(this),
         errorString = std::move(errorString), result = std::move(result)] {
          if (IsClosed()) {
            return;
          }
          JSErrorResult rv;
          mPCObserver->OnAddIceCandidateError(
              *buildJSErrorData(result, errorString), rv);
        }));
  }

  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::CloseStreams() {
  PC_AUTO_ENTER_API_CALL(false);

  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::SetPeerIdentity(const nsAString& aPeerIdentity) {
  PC_AUTO_ENTER_API_CALL(true);
  MOZ_ASSERT(!aPeerIdentity.IsEmpty());

  // once set, this can't be changed
  if (mPeerIdentity) {
    if (!mPeerIdentity->Equals(aPeerIdentity)) {
      return NS_ERROR_FAILURE;
    }
  } else {
    mPeerIdentity = MakeRefPtr<PeerIdentity>(aPeerIdentity);
    Document* doc = mWindow->GetExtantDoc();
    if (!doc) {
      CSFLogInfo(LOGTAG, "Can't update principal on streams; document gone");
      return NS_ERROR_FAILURE;
    }
    for (const auto& transceiver : mTransceivers) {
      transceiver->Sender()->GetPipeline()->UpdateSinkIdentity(
          doc->NodePrincipal(), mPeerIdentity);
    }
  }
  return NS_OK;
}

nsresult PeerConnectionImpl::OnAlpnNegotiated(const std::string& aAlpn,
                                              bool aPrivacyRequested) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");
  RefPtr<PeerConnectionImpl> kungFuDeathGrip(this);
  if (IsClosed()) {
    return NS_OK;  // Nod and smile
  }
  MOZ_DIAGNOSTIC_ASSERT(!mRequestedPrivacy ||
                        (*mRequestedPrivacy == PrincipalPrivacy::Private) ==
                            aPrivacyRequested);
  (void)aAlpn;

  mRequestedPrivacy = Some(aPrivacyRequested ? PrincipalPrivacy::Private
                                             : PrincipalPrivacy::NonPrivate);
  // This updates the MediaPipelines with a private PrincipalHandle. Note that
  // MediaPipelineReceive has its own AlpnNegotiated handler so it can get
  // signaled off-main to drop data until it receives the new PrincipalHandle
  // from us.
  UpdateMediaPipelines();
  return NS_OK;
}

void PeerConnectionImpl::OnDtlsStateChange(
    const std::string& aTransportId, TransportLayer::State aState,
    const nsTArray<nsTArray<uint8_t>>& aRemoteCerts,
    Maybe<dom::RTCErrorParams> aError) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");
  RefPtr<PeerConnectionImpl> kungFuDeathGrip(this);
  if (IsClosed()) {
    return;
  }

  nsCString key(aTransportId.data(), aTransportId.size());
  RefPtr<RTCDtlsTransport> dtlsTransport =
      mTransportIdToRTCDtlsTransport.Get(key);
  if (!dtlsTransport) {
    return;
  }

  nsTArray<nsTArray<uint8_t>> certsCopy;
  certsCopy.SetCapacity(aRemoteCerts.Length());
  for (const auto& cert : aRemoteCerts) {
    certsCopy.AppendElement(cert.Clone());
  }
  dtlsTransport->UpdateState(aState, std::move(certsCopy), std::move(aError));
  // Whenever the state of an RTCDtlsTransport changes or when the [[IsClosed]]
  // slot turns true, the user agent MUST update the connection state by
  // queueing a task that runs the following steps:
  // NOTE: The business about [[IsClosed]] here is probably a bug, because the
  // rest of the spec makes it very clear that events should never fire when
  // [[IsClosed]] is true. See https://github.com/w3c/webrtc-pc/issues/2865
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<PeerConnectionImpl>(this)] {
        // Let connection be this RTCPeerConnection object.
        // Let newState be the value of deriving a new state value as described
        // by the RTCPeerConnectionState enum.
        // If connection.[[ConnectionState]] is equal to newState, abort these
        // steps.
        // Set connection.[[ConnectionState]] to newState.
        if (UpdateConnectionState()) {
          // Fire an event named connectionstatechange at connection.
          JSErrorResult jrv;
          mPCObserver->OnStateChange(PCObserverStateType::ConnectionState, jrv);
        }
      }));
}

void PeerConnectionImpl::OnRtcpStateChange(const std::string& aTransportId,
                                           TransportLayer::State aState,
                                           Maybe<dom::RTCErrorParams> aError) {
  OnDtlsStateChange(aTransportId, aState, {}, std::move(aError));
}

RTCPeerConnectionState PeerConnectionImpl::GetNewConnectionState() const {
  // closed 	The RTCPeerConnection object's [[IsClosed]] slot is true.
  if (IsClosed()) {
    return RTCPeerConnectionState::Closed;
  }

  // Would use a bitset, but that requires lots of static_cast<size_t>
  // Oh well.
  std::set<RTCDtlsTransportState> statesFound;
  std::set<RefPtr<RTCDtlsTransport>> transports(GetActiveTransports());
  for (const auto& transport : transports) {
    statesFound.insert(transport->State());
  }

  // failed 	The previous state doesn't apply, and either
  // [[IceConnectionState]] is "failed" or any RTCDtlsTransports are in the
  // "failed" state.
  if (mIceConnectionState == RTCIceConnectionState::Failed ||
      statesFound.count(RTCDtlsTransportState::Failed)) {
    return RTCPeerConnectionState::Failed;
  }

  // disconnected 	None of the previous states apply, and
  // [[IceConnectionState]] is "disconnected".
  if (mIceConnectionState == RTCIceConnectionState::Disconnected) {
    return RTCPeerConnectionState::Disconnected;
  }

  // new 	None of the previous states apply, and either
  // [[IceConnectionState]] is "new", and all RTCDtlsTransports are in the
  // "new" or "closed" state...
  if (mIceConnectionState == RTCIceConnectionState::New &&
      !statesFound.count(RTCDtlsTransportState::Connecting) &&
      !statesFound.count(RTCDtlsTransportState::Connected) &&
      !statesFound.count(RTCDtlsTransportState::Failed)) {
    return RTCPeerConnectionState::New;
  }

  // ...or there are no transports.
  if (statesFound.empty()) {
    return RTCPeerConnectionState::New;
  }

  // connected 	None of the previous states apply,
  // [[IceConnectionState]] is "connected", and all RTCDtlsTransports are in
  // the "connected" or "closed" state.
  if (mIceConnectionState == RTCIceConnectionState::Connected &&
      !statesFound.count(RTCDtlsTransportState::New) &&
      !statesFound.count(RTCDtlsTransportState::Failed) &&
      !statesFound.count(RTCDtlsTransportState::Connecting)) {
    return RTCPeerConnectionState::Connected;
  }

  // connecting 	None of the previous states apply.
  return RTCPeerConnectionState::Connecting;
}

bool PeerConnectionImpl::UpdateConnectionState() {
  auto newState = GetNewConnectionState();
  if (newState != mConnectionState) {
    CSFLogInfo(LOGTAG, "%s: %d -> %d (%p)", __FUNCTION__,
               static_cast<int>(mConnectionState), static_cast<int>(newState),
               this);
    mConnectionState = newState;
    if (mConnectionState != RTCPeerConnectionState::Closed) {
      return true;
    }
  }

  return false;
}

void PeerConnectionImpl::OnMediaError(const std::string& aError) {
  CSFLogError(LOGTAG, "Encountered media error! %s", aError.c_str());
  // TODO: Let content know about this somehow.
}

void PeerConnectionImpl::DumpPacket_m(size_t level, dom::mozPacketDumpType type,
                                      bool sending,
                                      UniquePtr<uint8_t[]>& packet,
                                      size_t size) {
  if (IsClosed()) {
    return;
  }

  // TODO: Is this efficient? Should we try grabbing our JS ctx from somewhere
  // else?
  AutoJSAPI jsapi;
  if (!jsapi.Init(mWindow)) {
    return;
  }

  UniquePtr<void, JS::FreePolicy> packetPtr{packet.release()};
  JS::Rooted<JSObject*> jsobj(
      jsapi.cx(),
      JS::NewArrayBufferWithContents(jsapi.cx(), size, std::move(packetPtr)));

  RootedSpiderMonkeyInterface<ArrayBuffer> arrayBuffer(jsapi.cx());
  if (!arrayBuffer.Init(jsobj)) {
    return;
  }

  JSErrorResult jrv;
  mPCObserver->OnPacket(level, type, sending, arrayBuffer, jrv);
}

nsresult PeerConnectionImpl::EnablePacketDump(unsigned long level,
                                              dom::mozPacketDumpType type,
                                              bool sending) {
  return GetPacketDumper()->EnablePacketDump(level, type, sending);
}

nsresult PeerConnectionImpl::DisablePacketDump(unsigned long level,
                                               dom::mozPacketDumpType type,
                                               bool sending) {
  return GetPacketDumper()->DisablePacketDump(level, type, sending);
}

void PeerConnectionImpl::StampTimecard(const char* aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  STAMP_TIMECARD(mTimeCard, aEvent);
}

void PeerConnectionImpl::SendWarningToConsole(const nsCString& aWarning) {
  nsAutoString msg = NS_ConvertASCIItoUTF16(aWarning);
  nsContentUtils::ReportToConsoleByWindowID(msg, nsIScriptError::warningFlag,
                                            "WebRTC"_ns, mWindow->WindowID());
}

void PeerConnectionImpl::GetDefaultRtpExtensions(
    const JsepCodecPreferences& aPrefs,
    nsTArray<RtpExtensionHeader>* aRtpExtensions) {
  MOZ_ASSERT(aRtpExtensions);
  RtpExtensionHeader audioLevel = {
      JsepMediaType::kAudio, SdpDirectionAttribute::Direction::kSendrecv,
      nsLiteralCString(webrtc::RtpExtension::kAudioLevelUri)};
  aRtpExtensions->AppendElement(std::move(audioLevel));

  RtpExtensionHeader csrcAudioLevels = {
      JsepMediaType::kAudio, SdpDirectionAttribute::Direction::kRecvonly,
      nsLiteralCString(webrtc::RtpExtension::kCsrcAudioLevelsUri)};
  aRtpExtensions->AppendElement(std::move(csrcAudioLevels));

  RtpExtensionHeader mid = {JsepMediaType::kAudioVideo,
                            SdpDirectionAttribute::Direction::kSendrecv,
                            nsLiteralCString(webrtc::RtpExtension::kMidUri)};
  aRtpExtensions->AppendElement(std::move(mid));

  RtpExtensionHeader absSendTime = {
      JsepMediaType::kVideo, SdpDirectionAttribute::Direction::kSendrecv,
      nsLiteralCString(webrtc::RtpExtension::kAbsSendTimeUri)};
  aRtpExtensions->AppendElement(std::move(absSendTime));

  RtpExtensionHeader timestampOffset = {
      JsepMediaType::kVideo, SdpDirectionAttribute::Direction::kSendrecv,
      nsLiteralCString(webrtc::RtpExtension::kTimestampOffsetUri)};
  aRtpExtensions->AppendElement(std::move(timestampOffset));

  RtpExtensionHeader playoutDelay = {
      JsepMediaType::kVideo, SdpDirectionAttribute::Direction::kRecvonly,
      nsLiteralCString(webrtc::RtpExtension::kPlayoutDelayUri)};
  aRtpExtensions->AppendElement(std::move(playoutDelay));

  JsepMediaType transportSequenceNumberMediaType = JsepMediaType::kNone;
  if (aPrefs.UseAudioTransportCC() && aPrefs.UseTransportCC()) {
    transportSequenceNumberMediaType = JsepMediaType::kAudioVideo;
  } else if (aPrefs.UseAudioTransportCC()) {
    transportSequenceNumberMediaType = JsepMediaType::kAudio;
  } else if (aPrefs.UseTransportCC()) {
    transportSequenceNumberMediaType = JsepMediaType::kVideo;
  }
  if (transportSequenceNumberMediaType != JsepMediaType::kNone) {
    RtpExtensionHeader transportSequenceNumber = {
        transportSequenceNumberMediaType,
        SdpDirectionAttribute::Direction::kSendrecv,
        nsLiteralCString(webrtc::RtpExtension::kTransportSequenceNumberUri)};
    aRtpExtensions->AppendElement(std::move(transportSequenceNumber));
  }
}

/* static */
void PeerConnectionImpl::GetCapabilities(
    const nsAString& aKind, dom::Nullable<dom::RTCRtpCapabilities>& aResult,
    sdp::Direction aDirection) {
  DefaultCodecPreferences prefs;
  AutoTArray<UniquePtr<JsepCodecDescription>, 16> codecs;
  AutoTArray<RtpExtensionHeader, 16> headers;
  auto mediaType = JsepMediaType::kNone;

  if (aKind.EqualsASCII("video")) {
    // Note to reviewers, this forced RTX to true.
    // RTX is supported by default, so I am not sure if that was necessary.
    // When it has been explicitly disabled by pref, is there a point in
    // forcing it here?
    EnumerateDefaultVideoCodecs(&codecs, prefs);
    mediaType = JsepMediaType::kVideo;
  } else if (aKind.EqualsASCII("audio")) {
    EnumerateDefaultAudioCodecs(&codecs, prefs);
    mediaType = JsepMediaType::kAudio;
  } else {
    return;
  }

  GetDefaultRtpExtensions(prefs, &headers);

  bool haveAddedRtx = false;

  // Use the codecs for kind to fill out the RTCRtpCodec
  for (const auto& codec : codecs) {
    // To avoid misleading information on codec capabilities skip:
    // - Any disabled by pref
    // - Recvonly codecs for send capabilities -- we have no sendonly codecs
    // - Those not signaled for audio/video (webrtc-datachannel)
    if (!codec->mEnabled || !codec->DirectionSupported(aDirection) ||
        codec->mName == "webrtc-datachannel") {
      continue;
    }

    dom::RTCRtpCodec capability;
    RTCRtpTransceiver::ToDomRtpCodec(*codec, &capability);

    if (!aResult.SetValue().mCodecs.AppendElement(capability, fallible)) {
      mozalloc_handle_oom(0);
    }

    // We need to manually add rtx for video.
    // Spec says: There will only be a single entry in codecs for
    // retransmission via RTX, with sdpFmtpLine not present.
    if (mediaType == JsepMediaType::kVideo && !haveAddedRtx) {
      const JsepVideoCodecDescription& videoCodec =
          static_cast<JsepVideoCodecDescription&>(*codec);
      if (videoCodec.mRtxEnabled) {
        dom::RTCRtpCodec rtx;
        RTCRtpTransceiver::ToDomRtpCodecRtx(videoCodec, &rtx);
        rtx.mSdpFmtpLine.Reset();
        if (!aResult.SetValue().mCodecs.AppendElement(rtx, fallible)) {
          mozalloc_handle_oom(0);
        }
        haveAddedRtx = true;
      }
    }
  }

  // Add headers that match the direction and media type requested.
  for (const auto& header : headers) {
    if ((header.direction & aDirection) && (header.mMediaType & mediaType)) {
      dom::RTCRtpHeaderExtensionCapability rtpHeader;
      rtpHeader.mUri.AssignASCII(header.extensionname);
      if (!aResult.SetValue().mHeaderExtensions.AppendElement(rtpHeader,
                                                              fallible)) {
        mozalloc_handle_oom(0);
      }
    }
  }
}

nsresult PeerConnectionImpl::CalculateFingerprint(
    const nsACString& algorithm, std::vector<uint8_t>* fingerprint) const {
  DtlsDigest digest(algorithm);

  MOZ_ASSERT(fingerprint);
  const UniqueCERTCertificate& cert = mCertificate->Certificate();
  nsresult rv = DtlsIdentity::ComputeFingerprint(cert, &digest);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "Unable to calculate certificate fingerprint, rv=%u",
                static_cast<unsigned>(rv));
    return rv;
  }
  *fingerprint = digest.value_;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::GetFingerprint(char** fingerprint) {
  MOZ_ASSERT(fingerprint);
  MOZ_ASSERT(mCertificate);
  std::vector<uint8_t> fp;
  nsresult rv = CalculateFingerprint(DtlsIdentity::DEFAULT_HASH_ALGORITHM, &fp);
  NS_ENSURE_SUCCESS(rv, rv);
  std::ostringstream os;
  os << DtlsIdentity::DEFAULT_HASH_ALGORITHM << ' '
     << SdpFingerprintAttributeList::FormatFingerprint(fp);
  std::string fpStr = os.str();

  char* tmp = new char[fpStr.size() + 1];
  std::copy(fpStr.begin(), fpStr.end(), tmp);
  tmp[fpStr.size()] = '\0';

  *fingerprint = tmp;
  return NS_OK;
}

void PeerConnectionImpl::GetCurrentLocalDescription(nsAString& aSDP) const {
  aSDP = NS_ConvertASCIItoUTF16(mCurrentLocalDescription.c_str());
}

void PeerConnectionImpl::GetPendingLocalDescription(nsAString& aSDP) const {
  aSDP = NS_ConvertASCIItoUTF16(mPendingLocalDescription.c_str());
}

void PeerConnectionImpl::GetCurrentRemoteDescription(nsAString& aSDP) const {
  aSDP = NS_ConvertASCIItoUTF16(mCurrentRemoteDescription.c_str());
}

void PeerConnectionImpl::GetPendingRemoteDescription(nsAString& aSDP) const {
  aSDP = NS_ConvertASCIItoUTF16(mPendingRemoteDescription.c_str());
}

dom::Nullable<bool> PeerConnectionImpl::GetCurrentOfferer() const {
  dom::Nullable<bool> result;
  if (mCurrentOfferer.isSome()) {
    result.SetValue(*mCurrentOfferer);
  }
  return result;
}

dom::Nullable<bool> PeerConnectionImpl::GetPendingOfferer() const {
  dom::Nullable<bool> result;
  if (mPendingOfferer.isSome()) {
    result.SetValue(*mPendingOfferer);
  }
  return result;
}

NS_IMETHODIMP
PeerConnectionImpl::SignalingState(RTCSignalingState* aState) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(aState);

  *aState = mSignalingState;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::IceConnectionState(RTCIceConnectionState* aState) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(aState);

  *aState = mIceConnectionState;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::IceGatheringState(RTCIceGatheringState* aState) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(aState);

  *aState = mIceGatheringState;
  return NS_OK;
}

NS_IMETHODIMP
PeerConnectionImpl::ConnectionState(RTCPeerConnectionState* aState) {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(aState);

  *aState = mConnectionState;
  return NS_OK;
}

nsresult PeerConnectionImpl::CheckApiState(bool assert_ice_ready) const {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  MOZ_ASSERT(mTrickle || !assert_ice_ready ||
             (mIceGatheringState == RTCIceGatheringState::Complete));

  if (IsClosed()) {
    CSFLogError(LOGTAG, "%s: called API while closed", __FUNCTION__);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void PeerConnectionImpl::StoreFinalStats(
    UniquePtr<RTCStatsReportInternal>&& report) {
  using namespace Telemetry;

  report->mClosed = true;

  for (const auto& inboundRtpStats : report->mInboundRtpStreamStats) {
    bool isVideo = (inboundRtpStats.mId.Value().Find(u"video") != -1);
    if (!isVideo) {
      continue;
    }
    if (inboundRtpStats.mDiscardedPackets.WasPassed() &&
        report->mCallDurationMs.WasPassed()) {
      double mins = report->mCallDurationMs.Value() / (1000 * 60);
      if (mins > 0) {
        glean::webrtc::video_decoder_discarded_packets_per_call_ppm
            .AccumulateSingleSample(uint32_t(
                double(inboundRtpStats.mDiscardedPackets.Value()) / mins));
      }
    }
  }

  // Finally, store the stats
  mFinalStats = std::move(report);
}

NS_IMETHODIMP
PeerConnectionImpl::Close() {
  CSFLogDebug(LOGTAG, "%s: for %s", __FUNCTION__, mHandle.c_str());
  PC_AUTO_ENTER_API_CALL_NO_CHECK();

  if (IsClosed()) {
    return NS_OK;
  }

  STAMP_TIMECARD(mTimeCard, "Close");

  // When ICE completes, we record some telemetry. We do this at the end of the
  // call because we want to make sure we've waited for all trickle ICE
  // candidates to come in; this can happen well after we've transitioned to
  // connected. As a bonus, this allows us to detect race conditions where a
  // stats dispatch happens right as the PC closes.
  RecordEndOfCallTelemetry();

  CSFLogInfo(LOGTAG,
             "%s: Closing PeerConnectionImpl %s; "
             "ending call",
             __FUNCTION__, mHandle.c_str());
  if (mJsepSession) {
    mJsepSession->Close();
  }
  if (mDataConnection) {
    CSFLogInfo(LOGTAG, "%s: Destroying DataChannelConnection %p for %s",
               __FUNCTION__, (void*)mDataConnection.get(), mHandle.c_str());
    mDataConnection->Destroy();
    mDataConnection =
        nullptr;  // it may not go away until the runnables are dead
  }

  if (mStunAddrsRequest) {
    for (const auto& hostname : mRegisteredMDNSHostnames) {
      mStunAddrsRequest->SendUnregisterMDNSHostname(
          nsCString(hostname.c_str()));
    }
    mRegisteredMDNSHostnames.clear();
    mStunAddrsRequest->Cancel();
    mStunAddrsRequest = nullptr;
  }

  for (auto& transceiver : mTransceivers) {
    transceiver->Close();
  }

  mTransportIdToRTCDtlsTransport.Clear();

  mQueuedIceCtxOperations.clear();

  mOperations.Clear();

  // Uncount this connection as active on the inner window upon close.
  if (mWindow && mActiveOnWindow) {
    mWindow->RemovePeerConnection();
    mActiveOnWindow = false;
  }

  mSignalingState = RTCSignalingState::Closed;
  mIceConnectionState = RTCIceConnectionState::Closed;
  mConnectionState = RTCPeerConnectionState::Closed;

  if (!mTransportHandler) {
    // We were never initialized, apparently.
    return NS_OK;
  }

  mGatheringStateChangeListener.DisconnectIfExists();
  mConnectionStateChangeListener.DisconnectIfExists();
  mCandidateListener.DisconnectIfExists();
  mCandidateErrorListener.DisconnectIfExists();
  mAlpnNegotiatedListener.DisconnectIfExists();
  mStateChangeListener.DisconnectIfExists();
  mRtcpStateChangeListener.DisconnectIfExists();

  // Clear any resources held by libwebrtc through our Call instance.
  RefPtr<GenericPromise> callDestroyPromise;
  if (mCall) {
    // Make sure the compiler does not get confused and try to acquire a
    // reference to this thread _after_ we null out mCall.
    auto callThread = mCall->mCallThread;
    callDestroyPromise =
        InvokeAsync(callThread, __func__, [call = std::move(mCall)]() {
          call->Destroy();
          return GenericPromise::CreateAndResolve(
              true, "PCImpl->WebRtcCallWrapper::Destroy");
        });
  } else {
    callDestroyPromise = GenericPromise::CreateAndResolve(true, __func__);
  }

  mFinalStatsQuery =
      GetStats(nullptr, true)
          ->Then(
              GetMainThreadSerialEventTarget(), __func__,
              [this, self = RefPtr<PeerConnectionImpl>(this)](
                  UniquePtr<dom::RTCStatsReportInternal>&& aReport) mutable {
                StoreFinalStats(std::move(aReport));
                return GenericNonExclusivePromise::CreateAndResolve(true,
                                                                    __func__);
              },
              [](nsresult aError) {
                return GenericNonExclusivePromise::CreateAndResolve(true,
                                                                    __func__);
              });

  // 1. Allow final stats query to complete.
  // 2. Tear down call, if necessary. We do this before we shut down the
  //    transport handler, so RTCP BYE can be sent.
  // 3. Tear down the transport handler, and deregister from PeerConnectionCtx.
  //    When we deregister from PeerConnectionCtx, our final stats (if any)
  //    will be stored.
  mFinalStatsQuery
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [callDestroyPromise]() mutable { return callDestroyPromise; })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [this, self = RefPtr<PeerConnectionImpl>(this)]() mutable {
            CSFLogDebug(LOGTAG, "PCImpl->mTransportHandler::RemoveTransports");
            mTransportHandler->RemoveTransportsExcept(std::set<std::string>());
            if (mPrivateWindow) {
              mTransportHandler->ExitPrivateMode();
            }
            mTransportHandler = nullptr;
            if (PeerConnectionCtx::isActive()) {
              // If we're shutting down xpcom, this Instance will be unset
              // before calling Close() on all remaining PCs, to avoid
              // reentrancy.
              PeerConnectionCtx::GetInstance()->RemovePeerConnection(mHandle);
            }
          });

  return NS_OK;
}

void PeerConnectionImpl::BreakCycles() {
  for (auto& transceiver : mTransceivers) {
    transceiver->BreakCycles();
  }
  mTransceivers.Clear();
}

bool PeerConnectionImpl::HasPendingSetParameters() const {
  for (const auto& transceiver : mTransceivers) {
    if (transceiver->Sender()->HasPendingSetParameters()) {
      return true;
    }
  }
  return false;
}

void PeerConnectionImpl::InvalidateLastReturnedParameters() {
  for (const auto& transceiver : mTransceivers) {
    transceiver->Sender()->InvalidateLastReturnedParameters();
  }
}

void PeerConnectionImpl::ParseIceServers(
    const nsTArray<dom::RTCIceServer>& aIceServers, ErrorResult& aRv) {
  auto result = IceServerParser::Parse(aIceServers);
  if (result.isErr()) {
    aRv = result.unwrapErr();
  }
}

static JsepRtcpMuxPolicy ToJsepRtcpMuxPolicy(dom::RTCRtcpMuxPolicy aPolicy) {
  switch (aPolicy) {
    case dom::RTCRtcpMuxPolicy::Require:
      return kRtcpMuxRequire;
    case dom::RTCRtcpMuxPolicy::Negotiate:
      return kRtcpMuxNegotiate;
  }
  MOZ_CRASH("Unexpected RTCRtcpMuxPolicy value");
}

void PeerConnectionImpl::SetConfiguration(
    const RTCConfiguration& aConfiguration, ErrorResult& aRv) {
  // Spec: check rtcpMuxPolicy consistency before ICE server validation.
  if (!mRtcpMuxPolicy.isSome()) {
    if (NS_FAILED(mJsepSession->SetRtcpMuxPolicy(
            ToJsepRtcpMuxPolicy(aConfiguration.mRtcpMuxPolicy)))) {
      aRv.ThrowOperationError("Failed to set rtcpMuxPolicy");
      return;
    }
    mRtcpMuxPolicy = Some(aConfiguration.mRtcpMuxPolicy);
    if (aConfiguration.mRtcpMuxPolicy == dom::RTCRtcpMuxPolicy::Negotiate) {
      mozilla::glean::rtcpeerconnection::count_rtcp_mux_policy_negotiate.Add(1);
    }
  } else if (*mRtcpMuxPolicy != aConfiguration.mRtcpMuxPolicy) {
    aRv.ThrowInvalidModificationError(
        "Cannot change rtcpMuxPolicy with setConfiguration");
    return;
  }

  ParseIceServers(aConfiguration.mIceServers, aRv);
  if (aRv.Failed()) {
    return;
  }

  aRv = SetConfiguration(aConfiguration);
}

nsresult PeerConnectionImpl::SetConfiguration(
    const RTCConfiguration& aConfiguration) {
  nsresult rv = mTransportHandler->SetIceConfig(
      aConfiguration.mIceServers, aConfiguration.mIceTransportPolicy);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  JsepBundlePolicy bundlePolicy;
  switch (aConfiguration.mBundlePolicy) {
    case dom::RTCBundlePolicy::Balanced:
      bundlePolicy = kBundleBalanced;
      break;
    case dom::RTCBundlePolicy::Max_compat:
      bundlePolicy = kBundleMaxCompat;
      break;
    case dom::RTCBundlePolicy::Max_bundle:
      bundlePolicy = kBundleMaxBundle;
      break;
    default:
      MOZ_CRASH();
  }

  // Ignore errors, since those ought to be handled earlier.
  (void)mJsepSession->SetBundlePolicy(bundlePolicy);

  if (!aConfiguration.mPeerIdentity.IsEmpty()) {
    mPeerIdentity = MakeRefPtr<PeerIdentity>(aConfiguration.mPeerIdentity);
    mRequestedPrivacy = Some(PrincipalPrivacy::Private);
  }

  auto proxyConfig = GetProxyConfig();
  if (proxyConfig) {
    // Note that this could check if PrivacyRequested() is set on the PC and
    // remove "webrtc" from the ALPN list.  But that would only work if the PC
    // was constructed with a peerIdentity constraint, not when isolated
    // streams are added.  If we ever need to signal to the proxy that the
    // media is isolated, then we would need to restructure this code.
    mTransportHandler->SetProxyConfig(std::move(*proxyConfig));
  }

  mJsepSession->SetAlwaysNegotiateDataChannels(
      aConfiguration.mAlwaysNegotiateDataChannels);
  // Store the configuration for about:webrtc
  StoreConfigurationForAboutWebrtc(aConfiguration);

  return NS_OK;
}

RTCSctpTransport* PeerConnectionImpl::GetSctp() const {
  return mSctpTransport.get();
}

void PeerConnectionImpl::RestartIce() {
  RestartIceNoRenegotiationNeeded();
  // Update the negotiation-needed flag for connection.
  UpdateNegotiationNeeded();
}

// webrtc-pc does not specify any situations where this is done, but the JSEP
// spec does, in some situations due to setConfiguration.
void PeerConnectionImpl::RestartIceNoRenegotiationNeeded() {
  // Empty connection.[[LocalIceCredentialsToReplace]], and populate it with
  // all ICE credentials (ice-ufrag and ice-pwd as defined in section 15.4 of
  // [RFC5245]) found in connection.[[CurrentLocalDescription]], as well as all
  // ICE credentials found in connection.[[PendingLocalDescription]].
  mLocalIceCredentialsToReplace = mJsepSession->GetLocalIceCredentials();
}

bool PeerConnectionImpl::PluginCrash(uint32_t aPluginID,
                                     const nsAString& aPluginName) {
  // fire an event to the DOM window if this is "ours"
  if (!AnyCodecHasPluginID(aPluginID)) {
    return false;
  }

  CSFLogError(LOGTAG, "%s: Our plugin %llu crashed", __FUNCTION__,
              static_cast<unsigned long long>(aPluginID));

  RefPtr<Document> doc = mWindow->GetExtantDoc();
  if (!doc) {
    NS_WARNING("Couldn't get document for PluginCrashed event!");
    return true;
  }

  PluginCrashedEventInit init;
  init.mPluginID = aPluginID;
  init.mPluginName = aPluginName;
  init.mSubmittedCrashReport = false;
  init.mGmpPlugin = true;
  init.mBubbles = true;
  init.mCancelable = true;

  RefPtr<PluginCrashedEvent> event =
      PluginCrashedEvent::Constructor(doc, u"PluginCrashed"_ns, init);

  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;

  nsCOMPtr<nsPIDOMWindowInner> window = mWindow;
  // MOZ_KnownLive due to bug 1506441
  EventDispatcher::DispatchDOMEvent(
      MOZ_KnownLive(nsGlobalWindowInner::Cast(window)), nullptr, event, nullptr,
      nullptr);

  return true;
}

void PeerConnectionImpl::RecordEndOfCallTelemetry() {
  if (!mCallTelemStarted) {
    return;
  }
  MOZ_RELEASE_ASSERT(!mCallTelemEnded, "Don't end telemetry twice");
  MOZ_RELEASE_ASSERT(mJsepSession,
                     "Call telemetry only starts after jsep session start");
  MOZ_RELEASE_ASSERT(mJsepSession->GetNegotiations() > 0,
                     "Call telemetry only starts after first connection");

  // Bitmask used for WEBRTC/LOOP_CALL_TYPE telemetry reporting
  static const uint32_t kAudioTypeMask = 1;
  static const uint32_t kVideoTypeMask = 2;
  static const uint32_t kDataChannelTypeMask = 4;

  // Report end-of-call Telemetry
  glean::webrtc::renegotiations.AccumulateSingleSample(
      mJsepSession->GetNegotiations() - 1);
  glean::webrtc::max_video_send_track.AccumulateSingleSample(
      mMaxSending[SdpMediaSection::MediaType::kVideo]);
  glean::webrtc::max_video_receive_track.AccumulateSingleSample(
      mMaxReceiving[SdpMediaSection::MediaType::kVideo]);
  glean::webrtc::max_audio_send_track.AccumulateSingleSample(
      mMaxSending[SdpMediaSection::MediaType::kAudio]);
  glean::webrtc::max_audio_receive_track.AccumulateSingleSample(
      mMaxReceiving[SdpMediaSection::MediaType::kAudio]);
  // DataChannels appear in both Sending and Receiving
  glean::webrtc::datachannel_negotiated
      .EnumGet(static_cast<glean::webrtc::DatachannelNegotiatedLabel>(
          mMaxSending[SdpMediaSection::MediaType::kApplication]))
      .Add();
  // Enumerated/bitmask: 1 = Audio, 2 = Video, 4 = DataChannel
  // A/V = 3, A/V/D = 7, etc
  uint32_t type = 0;
  if (mMaxSending[SdpMediaSection::MediaType::kAudio] ||
      mMaxReceiving[SdpMediaSection::MediaType::kAudio]) {
    type = kAudioTypeMask;
  }
  if (mMaxSending[SdpMediaSection::MediaType::kVideo] ||
      mMaxReceiving[SdpMediaSection::MediaType::kVideo]) {
    type |= kVideoTypeMask;
  }
  if (mMaxSending[SdpMediaSection::MediaType::kApplication]) {
    type |= kDataChannelTypeMask;
  }
  glean::webrtc::call_type.AccumulateSingleSample(type);

  MOZ_RELEASE_ASSERT(mWindow);
  auto found = sCallDurationTimers.find(mWindow->WindowID());
  if (found != sCallDurationTimers.end()) {
    found->second.UnregisterConnection((type & kAudioTypeMask) ||
                                       (type & kVideoTypeMask));
    if (found->second.IsStopped()) {
      sCallDurationTimers.erase(found);
    }
  }
  mCallTelemEnded = true;
}

void PeerConnectionImpl::RecordSignalingTelemetry() const {
  uint16_t recvonly[SdpMediaSection::kMediaTypes];
  uint16_t sendonly[SdpMediaSection::kMediaTypes];
  uint16_t sendrecv[SdpMediaSection::kMediaTypes];
  mJsepSession->CountTransceivers(recvonly, sendonly, sendrecv);

  uint32_t numTransports = 0;
  mJsepSession->ForEachTransceiver([&](const auto& aTransceiver) {
    if (aTransceiver.HasOwnTransport()) {
      ++numTransports;
    }
  });

  SdpNegotiatedExtra extra = {
      .bundlePolicy = (mJsConfiguration.mBundlePolicy.WasPassed()
                           ? Some(mJsConfiguration.mBundlePolicy.Value())
                           : Nothing())
                          .map([](RTCBundlePolicy aPolicy) {
                            return GetEnumString(aPolicy);
                          }),
      .iceTransportPolicy =
          (mJsConfiguration.mIceTransportPolicy.WasPassed()
               ? Some(mJsConfiguration.mIceTransportPolicy.Value())
               : Nothing())
              .map([](RTCIceTransportPolicy aPolicy) {
                return GetEnumString(aPolicy);
              }),
      .isRemoteIceLite = Some(mJsepSession->RemoteIsIceLite()),
      .negotiationCount = Some(mJsepSession->GetNegotiations()),
      .numMsectionsAudioRecvonly = Some(recvonly[SdpMediaSection::kAudio]),
      .numMsectionsAudioSendonly = Some(sendonly[SdpMediaSection::kAudio]),
      .numMsectionsAudioSendrecv = Some(sendrecv[SdpMediaSection::kAudio]),
      .numMsectionsData = Some(sendrecv[SdpMediaSection::kApplication]),
      .numMsectionsVideoRecvonly = Some(recvonly[SdpMediaSection::kVideo]),
      .numMsectionsVideoSendonly = Some(sendonly[SdpMediaSection::kVideo]),
      .numMsectionsVideoSendrecv = Some(sendrecv[SdpMediaSection::kVideo]),
      .numTransports = Some(numTransports),
      .pcId = Some(nsCString(mHandle.c_str())),
  };
  glean::webrtc_signaling::sdp_negotiated.Record(Some(std::move(extra)));

  mJsepSession->ForEachTransceiver([&](const JsepTransceiver& aTransceiver) {
    if (const auto type = aTransceiver.GetMediaType();
        type != SdpMediaSection::kAudio && type != SdpMediaSection::kVideo) {
      return;
    }
    if (!aTransceiver.IsNegotiated()) {
      return;
    }
    const bool sending = aTransceiver.mSendTrack.GetActive();
    const bool receiving = aTransceiver.mRecvTrack.GetActive();
    const nsFmtCString codecString =
        ([](const JsepTrackNegotiatedDetails* aDetails) {
          std::set<std::string> payload_names;
          const size_t count = aDetails ? aDetails->GetEncodingCount() : 0;
          for (size_t i = 0; i < count; ++i) {
            const auto& encoding = aDetails->GetEncoding(i);
            for (const auto& codec : encoding.GetCodecs()) {
              if (codec->mEnabled) {
                payload_names.insert(codec->mName);
              }
            }
          }
          return nsFmtCString{"{}", fmt::join(payload_names, ", ")};
        })((sending ? aTransceiver.mSendTrack : aTransceiver.mRecvTrack)
               .GetNegotiatedDetails());
    const char* direction = ([&]() {
      if (sending && receiving) {
        return "sendrecv";
      }
      if (sending) {
        return "sendonly";
      }
      if (receiving) {
        return "recvonly";
      }
      return "inactive";
    })();
    const bool hasRtcpMux = aTransceiver.mTransport.mComponents == 1;
    if (aTransceiver.GetMediaType() == SdpMediaSection::kVideo) {
      VideoMsectionNegotiatedExtra extraVideo{
          .codecs = Some(codecString),
          .direction = Some(direction),
          .hasRtcpMux = Some(hasRtcpMux),
          .numSendSimulcastLayers =
              sending ? Some(aTransceiver.mSendTrack.GetRids().size())
                      : Nothing(),
          .pcId = Some(nsCString(mHandle.c_str())),
          .pcNegotiationCount = Some(mJsepSession->GetNegotiations()),
          .preferredRecvCodec =
              receiving ? Some(nsDependentCString(
                              aTransceiver.mRecvTrack.GetVideoPreferredCodec()
                                  .c_str()))
                        : Nothing(),
          .preferredSendCodec =
              sending ? Some(nsDependentCString(
                            aTransceiver.mSendTrack.GetVideoPreferredCodec()
                                .c_str()))
                      : Nothing(),
      };
      glean::webrtc_signaling::video_msection_negotiated.Record(
          Some(extraVideo));
    } else {
      AudioMsectionNegotiatedExtra extraAudio{
          .codecs = Some(codecString),
          .direction = Some(direction),
          .hasRtcpMux = Some(hasRtcpMux),
          .pcId = Some(nsCString(mHandle.c_str())),
          .pcNegotiationCount = Some(mJsepSession->GetNegotiations()),
          .preferredRecvCodec =
              receiving ? Some(nsDependentCString(
                              aTransceiver.mRecvTrack.GetAudioPreferredCodec()
                                  .c_str()))
                        : Nothing(),
          .preferredSendCodec =
              sending ? Some(nsDependentCString(
                            aTransceiver.mSendTrack.GetAudioPreferredCodec()
                                .c_str()))
                      : Nothing(),
      };
      glean::webrtc_signaling::audio_msection_negotiated.Record(
          Some(extraAudio));
    }
  });
}

DOMMediaStream* PeerConnectionImpl::GetReceiveStream(
    const std::string& aId) const {
  nsString wanted = NS_ConvertASCIItoUTF16(aId.c_str());
  for (auto& stream : mReceiveStreams) {
    nsString id;
    stream->GetId(id);
    if (id == wanted) {
      return stream;
    }
  }
  return nullptr;
}

DOMMediaStream* PeerConnectionImpl::CreateReceiveStream(
    const std::string& aId) {
  mReceiveStreams.AppendElement(MakeRefPtr<DOMMediaStream>(mWindow));
  mReceiveStreams.LastElement()->AssignId(NS_ConvertASCIItoUTF16(aId.c_str()));
  return mReceiveStreams.LastElement();
}

already_AddRefed<dom::Promise> PeerConnectionImpl::OnSetDescriptionSuccess(
    dom::RTCSdpType aSdpType, bool aRemote, ErrorResult& aError) {
  CSFLogDebug(LOGTAG, __FUNCTION__);

  RefPtr<dom::Promise> p = MakePromise(aError);
  if (aError.Failed()) {
    return nullptr;
  }

  DoSetDescriptionSuccessPostProcessing(aSdpType, aRemote, p);

  return p.forget();
}

void PeerConnectionImpl::DoSetDescriptionSuccessPostProcessing(
    dom::RTCSdpType aSdpType, bool aRemote, const RefPtr<dom::Promise>& aP) {
  // Spec says we queue a task for all the stuff that ends up back in JS
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__,
      [this, self = RefPtr<PeerConnectionImpl>(this), aSdpType, aRemote, aP] {
        if (IsClosed()) {
          // Yes, we do not settle the promise here. Yes, this is what the spec
          // wants.
          return;
        }

        MOZ_ASSERT(mUncommittedJsepSession);

        // sRD/sLD needs to be redone in certain circumstances
        bool needsRedo = HasPendingSetParameters();
        if (!needsRedo && aRemote && (aSdpType == dom::RTCSdpType::Offer)) {
          for (auto& transceiver : mTransceivers) {
            if (!mUncommittedJsepSession->GetTransceiver(
                    transceiver->GetJsepTransceiverId())) {
              needsRedo = true;
              break;
            }
          }
        }

        if (needsRedo) {
          // Spec says to abort, and re-do the sRD!
          // This happens either when there is a SetParameters call in
          // flight (that will race against the [[SendEncodings]]
          // modification caused by sRD(offer)), or when addTrack has been
          // called while sRD(offer) was in progress.
          mUncommittedJsepSession = mJsepSession->Clone();
          JsepSession::Result result;
          if (aRemote) {
            mUncommittedJsepSession->SetRemoteDescription(
                ToJsepSdpType(aSdpType), mRemoteRequestedSDP);
          } else {
            mUncommittedJsepSession->SetLocalDescription(
                ToJsepSdpType(aSdpType), mLocalRequestedSDP);
          }
          if (result.mError.isSome()) {
            // wat
            nsCString error(
                "When redoing sRD/sLD because it raced against "
                "addTrack or setParameters, we encountered a failure that "
                "did not happen "
                "the first time. This should never happen. The error was: ");
            error += mUncommittedJsepSession->GetLastError().c_str();
            aP->MaybeRejectWithOperationError(error);
            MOZ_ASSERT(false);
          } else {
            DoSetDescriptionSuccessPostProcessing(aSdpType, aRemote, aP);
          }
          return;
        }

        for (auto& transceiver : mTransceivers) {
          if (!mUncommittedJsepSession->GetTransceiver(
                  transceiver->GetJsepTransceiverId())) {
            // sLD, or sRD(answer), just make sure the new transceiver is
            // added, no need to re-do anything.
            mUncommittedJsepSession->AddTransceiver(
                transceiver->GetJsepTransceiver());
          }
        }

        auto oldIceCredentials = mJsepSession->GetLocalIceCredentials();
        auto newIceCredentials =
            mUncommittedJsepSession->GetLocalIceCredentials();

        bool iceRestartDetected =
            (!oldIceCredentials.empty() && !newIceCredentials.empty() &&
             (oldIceCredentials != newIceCredentials));

        mJsepSession = std::move(mUncommittedJsepSession);

        auto newSignalingState = GetSignalingState();
        SyncFromJsep();
        if (aRemote || aSdpType == dom::RTCSdpType::Pranswer ||
            aSdpType == dom::RTCSdpType::Answer) {
          InvalidateLastReturnedParameters();
        }

        if (aSdpType == dom::RTCSdpType::Offer &&
            mSignalingState == RTCSignalingState::Stable) {
          // If description is of type "offer" and
          // connection.[[SignalingState]] is "stable" then for each
          // transceiver in connection's set of transceivers, run the following
          // steps:
          SaveStateForRollback();
        }

        // Section 4.4.1.5 Set the RTCSessionDescription:
        if (aSdpType == dom::RTCSdpType::Rollback) {
          // - step 4.5.10, type is rollback
          RestoreStateForRollback();
        } else {
          // The RTCSctpTransport is created as soon as the data section is
          // set in any SDP, which can be in have-remote-offer, before any
          // RTCDtlsTransport exists. Its transport member is filled in by
          // UpdateRTCDtlsTransports below once we have a local description.
          UpdateRTCSctpTransport();
          // - step 4.5.9.1 when remote is false
          // - step 4.5.9.2.13 when remote is true, type answer or pranswer
          // RTCDtlsTransports are created when a local description is applied,
          // i.e. not for remote offers.
          if (!(aRemote && aSdpType == dom::RTCSdpType::Offer)) {
            UpdateRTCDtlsTransports();
          }
        }

        // Did we just apply a local description?
        if (!aRemote) {
          // We'd like to handle this in PeerConnectionImpl::UpdateNetworkState.
          // Unfortunately, if the WiFi switch happens quickly, we never see
          // that state change.  We need to detect the ice restart here and
          // reset the PeerConnectionImpl's stun addresses so they are
          // regathered when PeerConnectionImpl::GatherIfReady is called.
          if (iceRestartDetected || mJsepSession->IsIceRestarting()) {
            ResetStunAddrsForIceRestart();
          }
          EnsureTransports(*mJsepSession);
        }

        if (mJsepSession->GetState() == kJsepStateStable) {
          // If we're rolling back a local offer, we might need to remove some
          // transports, and stomp some MediaPipeline setup, but nothing further
          // needs to be done.
          UpdateTransports(*mJsepSession, mForceIceTcp);
          if (NS_FAILED(UpdateMediaPipelines())) {
            CSFLogError(LOGTAG, "Error Updating MediaPipelines");
            NS_ASSERTION(
                false,
                "Error Updating MediaPipelines in OnSetDescriptionSuccess()");
            aP->MaybeRejectWithOperationError("Error Updating MediaPipelines");
          }

          if (aSdpType != dom::RTCSdpType::Rollback) {
            StartIceChecks(*mJsepSession);
          }

          // Telemetry: record info on the current state of
          // streams/renegotiations/etc Note: this code gets run on rollbacks as
          // well!

          // Update the max channels used with each direction for each type
          uint16_t receiving[SdpMediaSection::kMediaTypes];
          uint16_t sending[SdpMediaSection::kMediaTypes];
          mJsepSession->CountTracksAndDatachannels(receiving, sending);
          for (size_t i = 0; i < SdpMediaSection::kMediaTypes; i++) {
            if (mMaxReceiving[i] < receiving[i]) {
              mMaxReceiving[i] = receiving[i];
            }
            if (mMaxSending[i] < sending[i]) {
              mMaxSending[i] = sending[i];
            }
          }
        } else if (aSdpType == dom::RTCSdpType::Offer && !aRemote) {
          // We do this to ensure the mediaPipelineFilter is ready to receive
          // PTs in our offer. This is mainly used for when bundle is involved
          // but for whatever reason mid or SSRC is not signaled.
          for (const auto& transceiverImpl : mTransceivers) {
            if ((transceiverImpl->Direction() ==
                 RTCRtpTransceiverDirection::Sendrecv) ||
                (transceiverImpl->Direction() ==
                 RTCRtpTransceiverDirection::Recvonly)) {
              transceiverImpl->Receiver()->UpdateTransport();
            }
          }
        }

        mPendingRemoteDescription =
            mJsepSession->GetRemoteDescription(kJsepDescriptionPending);
        mCurrentRemoteDescription =
            mJsepSession->GetRemoteDescription(kJsepDescriptionCurrent);
        mPendingLocalDescription =
            mJsepSession->GetLocalDescription(kJsepDescriptionPending);
        mCurrentLocalDescription =
            mJsepSession->GetLocalDescription(kJsepDescriptionCurrent);
        mPendingOfferer = mJsepSession->IsPendingOfferer();
        mCurrentOfferer = mJsepSession->IsCurrentOfferer();

        if (aSdpType == dom::RTCSdpType::Answer) {
          std::set<std::pair<std::string, std::string>> iceCredentials =
              mJsepSession->GetLocalIceCredentials();
          std::vector<std::pair<std::string, std::string>>
              iceCredentialsNotReplaced;
          std::set_intersection(mLocalIceCredentialsToReplace.begin(),
                                mLocalIceCredentialsToReplace.end(),
                                iceCredentials.begin(), iceCredentials.end(),
                                std::back_inserter(iceCredentialsNotReplaced));

          if (iceCredentialsNotReplaced.empty()) {
            mLocalIceCredentialsToReplace.clear();
          }
        }

        if (newSignalingState == RTCSignalingState::Stable) {
          mNegotiationNeeded = false;
          UpdateNegotiationNeeded();
        }

        bool signalingStateChanged = false;
        if (newSignalingState != mSignalingState) {
          mSignalingState = newSignalingState;
          signalingStateChanged = true;
        }

        // Spec does not actually tell us to do this, but that is probably a
        // spec bug.
        // https://github.com/w3c/webrtc-pc/issues/2817
        bool gatheringStateChanged = UpdateIceGatheringState();

        bool iceConnectionStateChanged = UpdateIceConnectionState();

        bool connectionStateChanged = UpdateConnectionState();

        // This only gets populated for remote descriptions
        dom::RTCRtpReceiver::StreamAssociationChanges changes;
        if (aRemote) {
          for (const auto& transceiver : mTransceivers) {
            transceiver->Receiver()->UpdateStreams(&changes);
          }
        }

        if (aSdpType == dom::RTCSdpType::Answer) {
          dom::RTCIceRole role = mJsepSession->IsIceControlling()
                                     ? dom::RTCIceRole::Controlling
                                     : dom::RTCIceRole::Controlled;
          for (const auto& dtlsTransport : GetActiveTransports()) {
            dtlsTransport->IceTransport()->SetRole(role);
          }
        }

        // Make sure to wait until after we've calculated track changes before
        // doing this.
        for (size_t i = 0; i < mTransceivers.Length();) {
          auto& transceiver = mTransceivers[i];
          if (transceiver->ShouldRemove()) {
            mTransceivers[i]->Close();
            mTransceivers[i]->SetRemovedFromPc();
            mTransceivers.RemoveElementAt(i);
          } else {
            ++i;
          }
        }

        // JS callbacks happen below. DO NOT TOUCH STATE AFTER THIS UNLESS SPEC
        // EXPLICITLY SAYS TO, OTHERWISE STATES THAT ARE NOT SUPPOSED TO BE
        // OBSERVABLE TO JS WILL BE!

        JSErrorResult jrv;
        RefPtr<PeerConnectionObserver> pcObserver(mPCObserver);
        if (signalingStateChanged) {
          pcObserver->OnStateChange(PCObserverStateType::SignalingState, jrv);
        }

        if (gatheringStateChanged) {
          pcObserver->OnStateChange(PCObserverStateType::IceGatheringState,
                                    jrv);
        }

        if (iceConnectionStateChanged) {
          pcObserver->OnStateChange(PCObserverStateType::IceConnectionState,
                                    jrv);
        }

        if (connectionStateChanged) {
          pcObserver->OnStateChange(PCObserverStateType::ConnectionState, jrv);
        }

        for (const auto& receiver : changes.mReceiversToMute) {
          // This sets the muted state for the recv track and all its clones.
          receiver->SetTrackMuteFromRemoteSdp();
        }

        for (const auto& association : changes.mStreamAssociationsRemoved) {
          RefPtr<DOMMediaStream> stream =
              GetReceiveStream(association.mStreamId);
          if (stream && stream->HasTrack(*association.mTrack)) {
            stream->RemoveTrackInternal(association.mTrack);
          }
        }

        // TODO(Bug 1241291): For legacy event, remove eventually
        std::vector<RefPtr<DOMMediaStream>> newStreams;

        for (const auto& association : changes.mStreamAssociationsAdded) {
          RefPtr<DOMMediaStream> stream =
              GetReceiveStream(association.mStreamId);
          if (!stream) {
            stream = CreateReceiveStream(association.mStreamId);
            newStreams.push_back(stream);
          }

          if (!stream->HasTrack(*association.mTrack)) {
            stream->AddTrackInternal(association.mTrack);
          }
        }

        for (const auto& trackEvent : changes.mTrackEvents) {
          dom::Sequence<OwningNonNull<DOMMediaStream>> streams;
          for (const auto& id : trackEvent.mStreamIds) {
            RefPtr<DOMMediaStream> stream = GetReceiveStream(id);
            if (!stream) {
              MOZ_ASSERT(false);
              continue;
            }
            if (!streams.AppendElement(*stream, fallible)) {
              // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which
              // might involve multiple reallocations) and potentially
              // crashing here, SetCapacity could be called outside the loop
              // once.
              mozalloc_handle_oom(0);
            }
          }
          pcObserver->FireTrackEvent(*trackEvent.mReceiver, streams, jrv);
        }

        // TODO(Bug 1241291): Legacy event, remove eventually
        for (const auto& stream : newStreams) {
          pcObserver->FireStreamEvent(*stream, jrv);
        }

        if (signalingStateChanged &&
            mSignalingState == dom::RTCSignalingState::Stable &&
            aSdpType != RTCSdpType::Rollback) {
          RecordSignalingTelemetry();
        }

        aP->MaybeResolveWithUndefined();
      }));
}

void PeerConnectionImpl::OnSetDescriptionError() {
  mUncommittedJsepSession = nullptr;
}

RTCSignalingState PeerConnectionImpl::GetSignalingState() const {
  switch (mJsepSession->GetState()) {
    case kJsepStateStable:
      return RTCSignalingState::Stable;
      break;
    case kJsepStateHaveLocalOffer:
      return RTCSignalingState::Have_local_offer;
      break;
    case kJsepStateHaveRemoteOffer:
      return RTCSignalingState::Have_remote_offer;
      break;
    case kJsepStateHaveLocalPranswer:
      return RTCSignalingState::Have_local_pranswer;
      break;
    case kJsepStateHaveRemotePranswer:
      return RTCSignalingState::Have_remote_pranswer;
      break;
    case kJsepStateClosed:
      return RTCSignalingState::Closed;
      break;
  }
  MOZ_CRASH("Invalid JSEP state");
}

bool PeerConnectionImpl::IsClosed() const {
  return mSignalingState == RTCSignalingState::Closed;
}

PeerConnectionWrapper::PeerConnectionWrapper(const std::string& handle)
    : impl_(nullptr) {
  if (PeerConnectionCtx::isActive()) {
    impl_ = PeerConnectionCtx::GetInstance()->GetPeerConnection(handle);
  }
}

const RefPtr<MediaTransportHandler> PeerConnectionImpl::GetTransportHandler()
    const {
  return mTransportHandler;
}

const std::string& PeerConnectionImpl::GetHandle() { return mHandle; }

const std::string& PeerConnectionImpl::GetName() {
  PC_AUTO_ENTER_API_CALL_NO_CHECK();
  return mName;
}

void PeerConnectionImpl::CandidateReady(const std::string& candidate,
                                        const std::string& transportId,
                                        const std::string& ufrag) {
  STAMP_TIMECARD(mTimeCard, "Ice Candidate gathered");
  PC_AUTO_ENTER_API_CALL_VOID_RETURN(false);

  if (mForceIceTcp && std::string::npos != candidate.find(" UDP ")) {
    CSFLogWarn(LOGTAG, "Blocking local UDP candidate: %s", candidate.c_str());
    STAMP_TIMECARD(mTimeCard, "UDP Ice Candidate blocked");
    return;
  }

  // One of the very few places we still use level; required by the JSEP API
  uint16_t level = 0;
  std::string mid;
  bool skipped = false;

  if (mUncommittedJsepSession) {
    // An sLD or sRD is in progress, and while that is the case, we need to add
    // the candidate to both the current JSEP engine, and the uncommitted JSEP
    // engine. We ignore errors because the spec says to only take into account
    // the current/pending local descriptions when determining whether to
    // surface the candidate to content, which does not take into account any
    // in-progress sRD/sLD.
    (void)mUncommittedJsepSession->AddLocalIceCandidate(
        candidate, transportId, ufrag, &level, &mid, &skipped);
  }

  nsresult res = mJsepSession->AddLocalIceCandidate(
      candidate, transportId, ufrag, &level, &mid, &skipped);

  if (NS_FAILED(res)) {
    std::string errorString = mJsepSession->GetLastError();

    STAMP_TIMECARD(mTimeCard, "Local Ice Candidate invalid");
    CSFLogError(LOGTAG,
                "Failed to incorporate local candidate into SDP:"
                " res = %u, candidate = %s, transport-id = %s,"
                " error = %s",
                static_cast<unsigned>(res), candidate.c_str(),
                transportId.c_str(), errorString.c_str());
    return;
  }

  if (skipped) {
    STAMP_TIMECARD(mTimeCard, "Local Ice Candidate skipped");
    CSFLogInfo(LOGTAG,
               "Skipped adding local candidate %s (transport-id %s) "
               "to SDP, this typically happens because the m-section "
               "is bundled, which means it doesn't make sense for it "
               "to have its own transport-related attributes.",
               candidate.c_str(), transportId.c_str());
    return;
  }

  mPendingLocalDescription =
      mJsepSession->GetLocalDescription(kJsepDescriptionPending);
  mCurrentLocalDescription =
      mJsepSession->GetLocalDescription(kJsepDescriptionCurrent);
  CSFLogInfo(LOGTAG, "Passing local candidate to content: %s",
             candidate.c_str());
  SendLocalIceCandidateToContent(level, mid, candidate, ufrag);
}

void PeerConnectionImpl::SendLocalIceCandidateToContent(
    uint16_t level, const std::string& mid, const std::string& candidate,
    const std::string& ufrag) {
  STAMP_TIMECARD(mTimeCard, "Send Ice Candidate to content");
  JSErrorResult rv;
  mPCObserver->OnIceCandidate(level, ObString(mid.c_str()),
                              ObString(candidate.c_str()),
                              ObString(ufrag.c_str()), rv);
}

// Implements the steps to "change the selected candidate pair and state".
// https://w3c.github.io/webrtc-pc/#dfn-change-the-selected-candidate-pair-and-state
void PeerConnectionImpl::IceConnectionStateChange(
    const std::string& aTransportId, dom::RTCIceTransportState domState,
    const Maybe<dom::IceCandidateAttributePair>& aSelectedPair) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");

  // 1. Let connection be the RTCPeerConnection object associated with this
  //    ICE Agent.
  RefPtr<PeerConnectionImpl> connection(this);

  // 2. If connection.[[IsClosed]] is true, abort these steps.
  if (IsClosed()) {
    return;
  }

  CSFLogDebug(LOGTAG, "IceConnectionStateChange: %s %d (%p)",
              aTransportId.c_str(), static_cast<int>(domState), this);

  // 3. Let transport be the RTCIceTransport whose state is changing.
  nsCString key(aTransportId.data(), aTransportId.size());
  RefPtr<RTCDtlsTransport> dtlsTransport =
      mTransportIdToRTCDtlsTransport.Get(key);
  if (!dtlsTransport) {
    return;
  }
  RefPtr<RTCIceTransport> transport = dtlsTransport->IceTransport();

  if (domState == RTCIceTransportState::Closed) {
    mTransportIdToRTCDtlsTransport.Remove(key);
  }

  // 4. Let selectedCandidatePairChanged be false.
  bool selectedCandidatePairChanged = false;

  // 5. Let transportIceConnectionStateChanged be false.
  bool transportIceConnectionStateChanged = false;

  // 6. Let connectionIceConnectionStateChanged be false.
  bool connectionIceConnectionStateChanged = false;

  // 7. Let connectionStateChanged be false.
  bool connectionStateChanged = false;

  // 8. If transport's selected candidate pair was changed, run the
  //    following steps:
  RefPtr<RTCIceCandidatePair> oldPair = transport->GetSelectedCandidatePair();
  bool noChange = false;
  if (!oldPair && aSelectedPair.isNothing()) {
    noChange = true;
  } else if (oldPair && aSelectedPair.isSome()) {
    // Compare against the internal (un-redacted) candidate strings.
    noChange = oldPair->Local()->CandidateInternal().EqualsASCII(
                   aSelectedPair->local().get()) &&
               oldPair->Remote()->CandidateInternal().EqualsASCII(
                   aSelectedPair->remote().get());
  }

  if (!noChange) {
    // 8.1. Let newCandidatePair be the result of creating an
    //      RTCIceCandidatePair with local and remote, representing the
    //      local and remote candidates of the indicated pair if one is
    //      selected, and null otherwise.
    RefPtr<RTCIceCandidatePair> newCandidatePair;
    if (aSelectedPair.isSome()) {
      nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
      RefPtr<RTCIceCandidate> local =
          RTCIceCandidate::FromAttribute(global, aSelectedPair->local());
      RefPtr<RTCIceCandidate> remote = RTCIceCandidate::FromAttribute(
          global, aSelectedPair->remote(), /*aRemote=*/true);
      newCandidatePair = new RTCIceCandidatePair(global, local, remote);
    }

    // 8.2. Set transport.[[SelectedCandidatePair]] to newCandidatePair.
    transport->SetSelectedCandidatePair(newCandidatePair);

    // 8.3. Set selectedCandidatePairChanged to true.
    selectedCandidatePairChanged = true;
  }

  // 9. If transport's RTCIceTransportState was changed, run the following
  //    steps:
  if (transport->State() != domState) {
    // 9.1. Set transport.[[IceTransportState]] to the new indicated
    //      RTCIceTransportState.
    transport->SetState(domState);

    // 9.2. Set transportIceConnectionStateChanged to true.
    transportIceConnectionStateChanged = true;

    // 9.3. Set connection.[[IceConnectionState]] to the value of deriving
    //      a new state value as described by the RTCIceConnectionState
    //      enum.
    if (UpdateIceConnectionState()) {
      // 9.4. If connection.[[IceConnectionState]] changed in the previous
      //      step, set connectionIceConnectionStateChanged to true.
      connectionIceConnectionStateChanged = true;
    }

    // 9.5. Set connection.[[ConnectionState]] to the value of deriving a
    //      new state value as described by the RTCPeerConnectionState
    //      enum.
    if (UpdateConnectionState()) {
      // 9.6. If connection.[[ConnectionState]] changed in the previous
      //      step, set connectionStateChanged to true.
      connectionStateChanged = true;
    }
  }

  // 10. If selectedCandidatePairChanged is true, fire an event named
  //     selectedcandidatepairchange at transport.
  if (selectedCandidatePairChanged) {
    transport->FireSelectedCandidatePairChangeEvent();
  }

  // 11. If transportIceConnectionStateChanged is true, fire an event
  //     named statechange at transport.
  if (transportIceConnectionStateChanged) {
    transport->FireStateChangeEvent();
  }

  WrappableJSErrorResult rv;
  RefPtr<PeerConnectionObserver> pcObserver(mPCObserver);

  // 12. If connectionIceConnectionStateChanged is true, fire an event
  //     named iceconnectionstatechange at connection.
  if (connectionIceConnectionStateChanged) {
    pcObserver->OnStateChange(PCObserverStateType::IceConnectionState, rv);
  }

  // 13. If connectionStateChanged is true, fire an event named
  //     connectionstatechange at connection.
  if (connectionStateChanged) {
    pcObserver->OnStateChange(PCObserverStateType::ConnectionState, rv);
  }
}

RTCIceConnectionState PeerConnectionImpl::GetNewIceConnectionState() const {
  // closed 	The RTCPeerConnection object's [[IsClosed]] slot is true.
  if (IsClosed()) {
    return RTCIceConnectionState::Closed;
  }

  // Would use a bitset, but that requires lots of static_cast<size_t>
  // Oh well.
  std::set<RTCIceTransportState> statesFound;
  std::set<RefPtr<RTCDtlsTransport>> transports(GetActiveTransports());
  for (const auto& transport : transports) {
    RefPtr<dom::RTCIceTransport> iceTransport = transport->IceTransport();
    CSFLogWarn(LOGTAG, "GetNewIceConnectionState: %p %d", iceTransport.get(),
               static_cast<int>(iceTransport->State()));
    statesFound.insert(iceTransport->State());
  }

  // failed 	 None of the previous states apply and any RTCIceTransports are
  // in the "failed" state.
  if (statesFound.count(RTCIceTransportState::Failed)) {
    return RTCIceConnectionState::Failed;
  }

  // disconnected 	 None of the previous states apply and any
  // RTCIceTransports are in the "disconnected" state.
  if (statesFound.count(RTCIceTransportState::Disconnected)) {
    return RTCIceConnectionState::Disconnected;
  }

  // new 	 None of the previous states apply and all RTCIceTransports are
  // in the "new" or "closed" state, or there are no transports.
  if (!statesFound.count(RTCIceTransportState::Checking) &&
      !statesFound.count(RTCIceTransportState::Completed) &&
      !statesFound.count(RTCIceTransportState::Connected)) {
    return RTCIceConnectionState::New;
  }

  // checking   None of the previous states apply and any RTCIceTransports are
  // in the "new" or "checking" state.
  if (statesFound.count(RTCIceTransportState::New) ||
      statesFound.count(RTCIceTransportState::Checking)) {
    return RTCIceConnectionState::Checking;
  }

  // completed  None of the previous states apply and all RTCIceTransports are
  // in the "completed" or "closed" state.
  if (!statesFound.count(RTCIceTransportState::Connected)) {
    return RTCIceConnectionState::Completed;
  }

  // connected 	None of the previous states apply.
  return RTCIceConnectionState::Connected;
}

bool PeerConnectionImpl::UpdateIceConnectionState() {
  auto newState = GetNewIceConnectionState();
  if (newState != mIceConnectionState) {
    CSFLogInfo(LOGTAG, "%s: %d -> %d (%p)", __FUNCTION__,
               static_cast<int>(mIceConnectionState),
               static_cast<int>(newState), this);
    mIceConnectionState = newState;
    // Start call telemtry logging on connected.
    if (mIceConnectionState == RTCIceConnectionState::Connected) {
      StartCallTelem();
    }
    if (mIceConnectionState != RTCIceConnectionState::Closed) {
      return true;
    }
  }

  return false;
}

void PeerConnectionImpl::OnCandidateFound(const std::string& aTransportId,
                                          const CandidateInfo& aCandidateInfo) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");
  RefPtr<PeerConnectionImpl> kungFuDeathGrip(this);

  if (mStunAddrsRequest && !aCandidateInfo.mMDNSAddress.empty()) {
    MOZ_ASSERT(!aCandidateInfo.mActualAddress.empty());

    if (mCanRegisterMDNSHostnamesDirectly) {
      auto itor = mRegisteredMDNSHostnames.find(aCandidateInfo.mMDNSAddress);

      // We'll see the address twice if we're generating both UDP and TCP
      // candidates.
      if (itor == mRegisteredMDNSHostnames.end()) {
        mRegisteredMDNSHostnames.insert(aCandidateInfo.mMDNSAddress);
        mStunAddrsRequest->SendRegisterMDNSHostname(
            nsCString(aCandidateInfo.mMDNSAddress.c_str()),
            nsCString(aCandidateInfo.mActualAddress.c_str()));
      }
    } else {
      mMDNSHostnamesToRegister.emplace(aCandidateInfo.mMDNSAddress,
                                       aCandidateInfo.mActualAddress);
    }
  }

  if (!aCandidateInfo.mDefaultHostRtp.empty()) {
    UpdateDefaultCandidate(aCandidateInfo.mDefaultHostRtp,
                           aCandidateInfo.mDefaultPortRtp,
                           aCandidateInfo.mDefaultHostRtcp,
                           aCandidateInfo.mDefaultPortRtcp, aTransportId);
  }
  CandidateReady(aCandidateInfo.mCandidate, aTransportId,
                 aCandidateInfo.mUfrag);
}

void PeerConnectionImpl::OnCandidateError(
    const IceCandidateErrorInfo& aErrorInfo) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");
  RefPtr<PeerConnectionImpl> kungFuDeathGrip(this);

  // Spec does not actually say to do this. That's probably a spec bug.
  if (IsClosed()) {
    return;
  }

  STAMP_TIMECARD(mTimeCard, "Sending icecandidateerror event");
  uint16_t errorCode = aErrorInfo.mErrorCode ? aErrorInfo.mErrorCode : 701;
  JSErrorResult rv;
  mPCObserver->OnIceCandidateError(ObString(aErrorInfo.mAddress.c_str()),
                                   aErrorInfo.mPort,
                                   ObString(aErrorInfo.mUrl.c_str()), errorCode,
                                   ObString(aErrorInfo.mErrorText.c_str()), rv);
}

void PeerConnectionImpl::IceGatheringStateChange(
    const std::string& aTransportId, dom::RTCIceGathererState state) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread");
  RefPtr<PeerConnectionImpl> kungFuDeathGrip(this);

  // If connection.[[IsClosed]] is true, abort these steps.
  if (IsClosed()) {
    return;
  }

  CSFLogWarn(LOGTAG, "IceGatheringStateChange: %s %d (%p)",
             aTransportId.c_str(), static_cast<int>(state), this);

  // Let transport be the RTCIceTransport for which candidate gathering
  // began/finished.
  nsCString key(aTransportId.data(), aTransportId.size());
  RefPtr<RTCDtlsTransport> dtlsTransport =
      mTransportIdToRTCDtlsTransport.Get(key);
  if (!dtlsTransport) {
    return;
  }
  RefPtr<RTCIceTransport> transport = dtlsTransport->IceTransport();

  if (transport->GatheringState() == state) {
    return;
  }

  // Set transport.[[IceGathererState]] to gathering.
  // or
  // Set transport.[[IceGathererState]] to complete.
  transport->SetGatheringState(state);

  // Set connection.[[IceGatheringState]] to the value of deriving a new state
  // value as described by the RTCIceGatheringState enum.
  //
  // Let connectionIceGatheringStateChanged be true if
  // connection.[[IceGatheringState]] changed in the previous step, otherwise
  // false.
  bool gatheringStateChanged = UpdateIceGatheringState();

  // Do not read or modify state beyond this point.

  // Fire an event named gatheringstatechange at transport.
  transport->FireGatheringStateChangeEvent();

  // If connectionIceGatheringStateChanged is true, fire an event named
  // icegatheringstatechange at connection.
  if (gatheringStateChanged) {
    // NOTE: If we're in the "complete" case, our JS code will fire a null
    // icecandidate event after firing the icegatheringstatechange event.
    // Fire an event named icecandidate using the RTCPeerConnectionIceEvent
    // interface with the candidate attribute set to null at connection.
    JSErrorResult rv;
    mPCObserver->OnStateChange(PCObserverStateType::IceGatheringState, rv);
  }
}

bool PeerConnectionImpl::UpdateIceGatheringState() {
  // If connection.[[IsClosed]] is true, abort these steps.
  if (IsClosed()) {
    return false;
  }

  // Let newState be the value of deriving a new state value as
  // described by the RTCIceGatheringState enum.
  auto newState = GetNewIceGatheringState();

  // If connection.[[IceGatheringState]] is equal to newState, abort
  // these steps.
  if (newState == mIceGatheringState) {
    return false;
  }

  CSFLogInfo(LOGTAG, "UpdateIceGatheringState: %d -> %d (%p)",
             static_cast<int>(mIceGatheringState), static_cast<int>(newState),
             this);
  // Set connection.[[IceGatheringState]] to newState.
  mIceGatheringState = newState;

  // Would be nice if we had a means of converting one of these dom
  // enums to a string that wasn't almost as much text as this switch
  // statement...
  switch (mIceGatheringState) {
    case RTCIceGatheringState::New:
      STAMP_TIMECARD(mTimeCard, "Ice gathering state: new");
      break;
    case RTCIceGatheringState::Gathering:
      STAMP_TIMECARD(mTimeCard, "Ice gathering state: gathering");
      break;
    case RTCIceGatheringState::Complete:
      STAMP_TIMECARD(mTimeCard, "Ice gathering state: complete");
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected mIceGatheringState!");
  }

  return true;
}

RTCIceGatheringState PeerConnectionImpl::GetNewIceGatheringState() const {
  // new 	Any of the RTCIceTransports are in the "new" gathering state
  // and none of the transports are in the "gathering" state, or there are no
  // transports.

  // NOTE! This derives the RTCIce**Gathering**State from the individual
  // RTCIce**Gatherer**State of the transports. These are different enums.
  // But they have exactly the same values, in the same order.
  // ¯\_(ツ)_/¯
  bool foundComplete = false;
  std::set<RefPtr<RTCDtlsTransport>> transports(GetActiveTransports());
  for (const auto& transport : transports) {
    RefPtr<dom::RTCIceTransport> iceTransport = transport->IceTransport();
    switch (iceTransport->GatheringState()) {
      case RTCIceGathererState::New:
        break;
      case RTCIceGathererState::Gathering:
        // gathering 	Any of the RTCIceTransports are in the "gathering"
        // state.
        return RTCIceGatheringState::Gathering;
      case RTCIceGathererState::Complete:
        foundComplete = true;
        break;
    }
  }

  if (!foundComplete) {
    return RTCIceGatheringState::New;
  }

  // This could change depending on the outcome in
  // https://github.com/w3c/webrtc-pc/issues/2914
  return RTCIceGatheringState::Complete;
}

void PeerConnectionImpl::UpdateDefaultCandidate(
    const std::string& defaultAddr, uint16_t defaultPort,
    const std::string& defaultRtcpAddr, uint16_t defaultRtcpPort,
    const std::string& transportId) {
  CSFLogDebug(LOGTAG, "%s", __FUNCTION__);
  mJsepSession->UpdateDefaultCandidate(
      defaultAddr, defaultPort, defaultRtcpAddr, defaultRtcpPort, transportId);
  if (mUncommittedJsepSession) {
    mUncommittedJsepSession->UpdateDefaultCandidate(
        defaultAddr, defaultPort, defaultRtcpAddr, defaultRtcpPort,
        transportId);
  }
}

RefPtr<dom::RTCStatsPromise> PeerConnectionImpl::GetDataChannelStats(
    const DOMHighResTimeStamp aTimestamp) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDataConnection) {
    return mDataConnection->GetStats(aTimestamp)
        ->Then(GetMainThreadSerialEventTarget(), __func__,
               [](DataChannelConnection::StatsPromise::ResolveOrRejectValue&&
                      aResult) {
                 auto report = MakeUnique<dom::RTCStatsCollection>();
                 if (aResult.IsResolve()) {
                   if (!report->mDataChannelStats.AppendElements(
                           aResult.ResolveValue(), fallible)) {
                     mozalloc_handle_oom(0);
                   }
                 }
                 return RTCStatsPromise::CreateAndResolve(std::move(report),
                                                          __func__);
               });
  }
  return nullptr;
}

void PeerConnectionImpl::CollectConduitTelemetryData() {
  MOZ_ASSERT(NS_IsMainThread());

  nsTArray<RefPtr<VideoSessionConduit>> conduits;
  for (const auto& transceiver : mTransceivers) {
    if (RefPtr<MediaSessionConduit> conduit = transceiver->GetConduit()) {
      conduit->AsVideoSessionConduit().apply(
          [&](const auto& aVideo) { conduits.AppendElement(aVideo); });
    }
  }

  if (!conduits.IsEmpty() && mCall) {
    mCall->mCallThread->Dispatch(
        NS_NewRunnableFunction(__func__, [conduits = std::move(conduits)] {
          for (const auto& conduit : conduits) {
            conduit->CollectTelemetryData();
          }
        }));
  }
}

nsTArray<dom::RTCCodecStats> PeerConnectionImpl::GetCodecStats(
    DOMHighResTimeStamp aNow) {
  MOZ_ASSERT(NS_IsMainThread());
  nsTArray<dom::RTCCodecStats> result;

  struct CodecComparator {
    bool operator()(const JsepCodecDescription* aA,
                    const JsepCodecDescription* aB) const {
      return aA->StatsId() < aB->StatsId();
    }
  };

  // transportId -> codec; per direction (whether the codecType
  // shall be "encode", "decode" or absent (if a codec exists in both maps for a
  // transport)). These do the bookkeeping to ensure codec stats get coalesced
  // to transport level.
  std::map<std::string, std::set<JsepCodecDescription*, CodecComparator>>
      sendCodecMap;
  std::map<std::string, std::set<JsepCodecDescription*, CodecComparator>>
      recvCodecMap;

  // Find all JsepCodecDescription instances we want to turn into codec stats.
  for (const auto& transceiver : mTransceivers) {
    // TODO: Grab these from the JSEP transceivers instead
    auto sendCodecs = transceiver->GetNegotiatedSendCodecs();
    auto recvCodecs = transceiver->GetNegotiatedRecvCodecs();

    const std::string transportId = transceiver->GetTransportId();
    // This ensures both codec maps have the same size.
    auto& sendMap = sendCodecMap[transportId];
    auto& recvMap = recvCodecMap[transportId];

    sendCodecs.apply([&](const auto& aCodecs) {
      for (const auto& codec : aCodecs) {
        sendMap.insert(codec.get());
      }
    });
    recvCodecs.apply([&](const auto& aCodecs) {
      for (const auto& codec : aCodecs) {
        recvMap.insert(codec.get());
      }
    });
  }

  auto createCodecStat = [&](const JsepCodecDescription* aCodec,
                             const nsString& aTransportId,
                             Maybe<RTCCodecType> aCodecType) {
    uint16_t pt;
    {
      DebugOnly<bool> rv = aCodec->GetPtAsInt(&pt);
      MOZ_ASSERT(rv);
    }
    nsString mimeType;
    mimeType.AppendPrintf(
        "%s/%s", aCodec->Type() == SdpMediaSection::kVideo ? "video" : "audio",
        aCodec->mName.c_str());
    nsString id = aTransportId;
    id.Append(u"_");
    id.Append(aCodec->StatsId());

    dom::RTCCodecStats codec;
    codec.mId.Construct(std::move(id));
    codec.mTimestamp.Construct(aNow);
    codec.mType.Construct(RTCStatsType::Codec);
    codec.mPayloadType = pt;
    if (aCodecType) {
      codec.mCodecType.Construct(*aCodecType);
    }
    codec.mTransportId = aTransportId;
    codec.mMimeType = std::move(mimeType);
    codec.mClockRate.Construct(aCodec->mClock);
    if (aCodec->Type() == SdpMediaSection::MediaType::kAudio) {
      codec.mChannels.Construct(aCodec->mChannels);
    }
    if (aCodec->mSdpFmtpLine) {
      codec.mSdpFmtpLine.Construct(
          NS_ConvertUTF8toUTF16(aCodec->mSdpFmtpLine->c_str()));
    }

    result.AppendElement(std::move(codec));
  };

  // Create codec stats for the gathered codec descriptions, sorted primarily
  // by transportId, secondarily by payload type (from StatsId()).
  for (const auto& [transportId, sendCodecs] : sendCodecMap) {
    const auto& recvCodecs = recvCodecMap[transportId];
    const nsString tid = NS_ConvertASCIItoUTF16(transportId);
    AutoTArray<JsepCodecDescription*, 16> bidirectionalCodecs;
    AutoTArray<JsepCodecDescription*, 16> unidirectionalCodecs;
    std::set_intersection(sendCodecs.cbegin(), sendCodecs.cend(),
                          recvCodecs.cbegin(), recvCodecs.cend(),
                          MakeBackInserter(bidirectionalCodecs),
                          CodecComparator());
    std::set_symmetric_difference(sendCodecs.cbegin(), sendCodecs.cend(),
                                  recvCodecs.cbegin(), recvCodecs.cend(),
                                  MakeBackInserter(unidirectionalCodecs),
                                  CodecComparator());
    for (const auto* codec : bidirectionalCodecs) {
      createCodecStat(codec, tid, Nothing());
    }
    for (const auto* codec : unidirectionalCodecs) {
      createCodecStat(
          codec, tid,
          Some(codec->mDirection == sdp::kSend ? RTCCodecType::Encode
                                               : RTCCodecType::Decode));
    }
  }

  return result;
}

RefPtr<dom::RTCStatsReportPromise> PeerConnectionImpl::GetStats(
    dom::MediaStreamTrack* aSelector, bool aInternalStats) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mFinalStatsQuery) {
    // This case should be _extremely_ rare; this will basically only happen
    // when WebrtcGlobalInformation tries to get our stats while we are tearing
    // down.
    return mFinalStatsQuery->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [this, self = RefPtr<PeerConnectionImpl>(this)]() {
          UniquePtr<dom::RTCStatsReportInternal> finalStats =
              MakeUnique<dom::RTCStatsReportInternal>();
          // Might not be set if this encountered some error.
          if (mFinalStats) {
            *finalStats = *mFinalStats;
          }
          return RTCStatsReportPromise::CreateAndResolve(std::move(finalStats),
                                                         __func__);
        });
  }

  nsTArray<RefPtr<dom::RTCStatsPromise>> promises;
  DOMHighResTimeStamp now = mTimestampMaker.GetNow().ToDom();

  nsTArray<dom::RTCCodecStats> codecStats = GetCodecStats(now);
  std::set<std::string> transportIds;

  if (!aSelector) {
    // There might not be any senders/receivers if we're DataChannel only, so we
    // don't handle the null selector case in the loop below.
    transportIds.insert("");
  }

  nsTArray<
      std::tuple<RTCRtpTransceiver*, RefPtr<RTCStatsPromise::AllPromiseType>>>
      transceiverStatsPromises;
  for (const auto& transceiver : mTransceivers) {
    const bool sendSelected = transceiver->Sender()->HasTrack(aSelector) ||
                              (!aSelector && transceiver->HasBeenUsedToSend());
    const bool recvSelected = transceiver->Receiver()->HasTrack(aSelector);
    if (!sendSelected && !recvSelected) {
      continue;
    }

    if (aSelector) {
      transportIds.insert(transceiver->GetTransportId());
    }

    nsTArray<RefPtr<RTCStatsPromise>> rtpStreamPromises;
    // Get all rtp stream stats for the given selector. Then filter away any
    // codec stat not related to the selector, and assign codec ids to the
    // stream stats.
    // Skips the ICE stats; we do our own queries based on |transportIds| to
    // avoid duplicates
    if (sendSelected) {
      rtpStreamPromises.AppendElements(
          transceiver->Sender()->GetStatsInternal(true));
    }
    if (recvSelected) {
      rtpStreamPromises.AppendElements(
          transceiver->Receiver()->GetStatsInternal(true));
    }
    transceiverStatsPromises.AppendElement(
        std::make_tuple(transceiver.get(),
                        RTCStatsPromise::All(GetMainThreadSerialEventTarget(),
                                             rtpStreamPromises)));
  }

  promises.AppendElement(RTCRtpTransceiver::ApplyCodecStats(
      std::move(codecStats), std::move(transceiverStatsPromises)));

  for (const auto& transportId : transportIds) {
    promises.AppendElement(mTransportHandler->GetIceStats(transportId, now));
  }

  if (mDataConnection) {
    promises.AppendElement(GetDataChannelStats(now));
  }

  auto pcStatsCollection = MakeUnique<dom::RTCStatsCollection>();
  RTCPeerConnectionStats pcStats;
  pcStats.mTimestamp.Construct(now);
  pcStats.mType.Construct(RTCStatsType::Peer_connection);
  pcStats.mId.Construct(NS_ConvertUTF8toUTF16(mHandle.c_str()));
  pcStats.mDataChannelsOpened.Construct(mDataChannelsOpened);
  pcStats.mDataChannelsClosed.Construct(mDataChannelsClosed);
  if (!pcStatsCollection->mPeerConnectionStats.AppendElement(std::move(pcStats),
                                                             fallible)) {
    mozalloc_handle_oom(0);
  }
  promises.AppendElement(RTCStatsPromise::CreateAndResolve(
      std::move(pcStatsCollection), __func__));

  // This is what we're going to return; all the stuff in |promises| will be
  // accumulated here.
  auto report = MakeUnique<dom::RTCStatsReportInternal>();
  report->mPcid = NS_ConvertASCIItoUTF16(mName.c_str());
  if (mWindow && mWindow->GetBrowsingContext()) {
    report->mBrowserId = mWindow->GetBrowsingContext()->BrowserId();
  }
  report->mConfiguration.Construct(mJsConfiguration);
  // TODO(bug 1589416): We need to do better here.
  if (!mIceStartTime.IsNull()) {
    report->mCallDurationMs.Construct(
        (TimeStamp::Now() - mIceStartTime).ToMilliseconds());
  }
  report->mIceRestarts = mIceRestartCount;
  report->mIceRollbacks = mIceRollbackCount;
  report->mClosed = false;
  report->mTimestamp = now;

  if (aInternalStats && mJsepSession) {
    for (const auto& candidate : mRawTrickledCandidates) {
      if (!report->mRawRemoteCandidates.AppendElement(
              NS_ConvertASCIItoUTF16(candidate.c_str()), fallible)) {
        // XXX(Bug 1632090) Instead of extending the array 1-by-1 (which might
        // involve multiple reallocations) and potentially crashing here,
        // SetCapacity could be called outside the loop once.
        mozalloc_handle_oom(0);
      }
    }

    if (mJsepSession) {
      // TODO we probably should report Current and Pending SDPs here
      // separately. Plus the raw SDP we got from JS (mLocalRequestedSDP).
      // And if it's the offer or answer would also be nice.
      std::string localDescription =
          mJsepSession->GetLocalDescription(kJsepDescriptionPendingOrCurrent);
      std::string remoteDescription =
          mJsepSession->GetRemoteDescription(kJsepDescriptionPendingOrCurrent);
      if (!report->mSdpHistory.AppendElements(mSdpHistory, fallible)) {
        mozalloc_handle_oom(0);
      }
      if (mJsepSession->IsPendingOfferer().isSome()) {
        report->mOfferer.Construct(*mJsepSession->IsPendingOfferer());
      } else if (mJsepSession->IsCurrentOfferer().isSome()) {
        report->mOfferer.Construct(*mJsepSession->IsCurrentOfferer());
      } else {
        // Silly.
        report->mOfferer.Construct(false);
      }
    }
  }

  return dom::RTCStatsPromise::All(GetMainThreadSerialEventTarget(), promises)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [report = std::move(report), idGen = mIdGenerator](
              nsTArray<UniquePtr<dom::RTCStatsCollection>> aStats) mutable {
            idGen->RewriteIds(std::move(aStats), report.get());
            return dom::RTCStatsReportPromise::CreateAndResolve(
                std::move(report), __func__);
          },
          [](nsresult rv) {
            return dom::RTCStatsReportPromise::CreateAndReject(rv, __func__);
          });
}

void PeerConnectionImpl::RecordIceRestartStatistics(JsepSdpType type) {
  switch (type) {
    case mozilla::kJsepSdpOffer:
    case mozilla::kJsepSdpPranswer:
      break;
    case mozilla::kJsepSdpAnswer:
      ++mIceRestartCount;
      break;
    case mozilla::kJsepSdpRollback:
      ++mIceRollbackCount;
      break;
  }
}

void PeerConnectionImpl::StoreConfigurationForAboutWebrtc(
    const dom::RTCConfiguration& aConfig) {
  // This will only be called once, when the PeerConnection is initially
  // configured, at least until setConfiguration is implemented
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=1253706
  // @TODO bug 1739451 call this from setConfiguration
  mJsConfiguration.mIceServers.Clear();
  for (const auto& server : aConfig.mIceServers) {
    RTCIceServerInternal internal;
    internal.mCredentialProvided = server.mCredential.WasPassed();
    internal.mUserNameProvided = server.mUsername.WasPassed();
    if (server.mUrl.WasPassed()) {
      if (!internal.mUrls.AppendElement(server.mUrl.Value(), fallible)) {
        mozalloc_handle_oom(0);
      }
    }
    if (server.mUrls.WasPassed()) {
      for (const auto& url : server.mUrls.Value().GetAsStringSequence()) {
        if (!internal.mUrls.AppendElement(url, fallible)) {
          mozalloc_handle_oom(0);
        }
      }
    }
    if (!mJsConfiguration.mIceServers.AppendElement(internal, fallible)) {
      mozalloc_handle_oom(0);
    }
  }
  mJsConfiguration.mSdpSemantics.Reset();
  if (aConfig.mSdpSemantics.WasPassed()) {
    mJsConfiguration.mSdpSemantics.Construct(aConfig.mSdpSemantics.Value());
  }

  mJsConfiguration.mIceTransportPolicy.Reset();
  mJsConfiguration.mIceTransportPolicy.Construct(aConfig.mIceTransportPolicy);
  mJsConfiguration.mBundlePolicy.Reset();
  mJsConfiguration.mBundlePolicy.Construct(aConfig.mBundlePolicy);
  mJsConfiguration.mPeerIdentityProvided = !aConfig.mPeerIdentity.IsEmpty();
  mJsConfiguration.mCertificatesProvided = !aConfig.mCertificates.Length();
  mJsConfiguration.mAlwaysNegotiateDataChannels =
      aConfig.mAlwaysNegotiateDataChannels;
}

dom::Sequence<dom::RTCSdpParsingErrorInternal>
PeerConnectionImpl::GetLastSdpParsingErrors() const {
  const auto& sdpErrors = mJsepSession->GetLastSdpParsingErrors();
  dom::Sequence<dom::RTCSdpParsingErrorInternal> domErrors;
  if (!domErrors.SetCapacity(domErrors.Length(), fallible)) {
    mozalloc_handle_oom(0);
  }
  for (const auto& error : sdpErrors) {
    mozilla::dom::RTCSdpParsingErrorInternal internal;
    internal.mLineNumber = error.first;
    if (!AppendASCIItoUTF16(MakeStringSpan(error.second.c_str()),
                            internal.mError, fallible)) {
      mozalloc_handle_oom(0);
    }
    if (!domErrors.AppendElement(std::move(internal), fallible)) {
      mozalloc_handle_oom(0);
    }
  }
  return domErrors;
}

// Telemetry for when calls start
void PeerConnectionImpl::StartCallTelem() {
  if (mCallTelemStarted) {
    return;
  }
  MOZ_RELEASE_ASSERT(mWindow);
  uint64_t windowId = mWindow->WindowID();
  auto found = sCallDurationTimers.find(windowId);
  if (found == sCallDurationTimers.end()) {
    found =
        sCallDurationTimers.emplace(windowId, PeerConnectionAutoTimer()).first;
  }
  found->second.RegisterConnection();
  mCallTelemStarted = true;

  // Increment session call counter
  // If we want to track Loop calls independently here, we need two
  // histograms.
  //
  // NOTE: As of bug 1654248 landing we are no longer counting renegotiations
  // as separate calls. Expect numbers to drop compared to
  // WEBRTC_CALL_COUNT_2.
  glean::webrtc::call_count_3.Add(1);
}

void PeerConnectionImpl::StunAddrsHandler::OnMDNSQueryComplete(
    const nsCString& hostname, const Maybe<nsCString>& address) {
  MOZ_ASSERT(NS_IsMainThread());
  PeerConnectionWrapper pcw(mPcHandle);
  if (!pcw.impl()) {
    return;
  }
  auto itor = pcw.impl()->mQueriedMDNSHostnames.find(hostname.get());
  if (itor != pcw.impl()->mQueriedMDNSHostnames.end()) {
    if (address) {
      for (auto& cand : itor->second) {
        // Replace obfuscated address with actual address
        std::string obfuscatedAddr = cand.mTokenizedCandidate[4];
        cand.mTokenizedCandidate[4] = address->get();
        std::ostringstream o;
        for (size_t i = 0; i < cand.mTokenizedCandidate.size(); ++i) {
          o << cand.mTokenizedCandidate[i];
          if (i + 1 != cand.mTokenizedCandidate.size()) {
            o << " ";
          }
        }
        std::string mungedCandidate = o.str();
        pcw.impl()->StampTimecard("Done looking up mDNS name");
        pcw.impl()->mTransportHandler->AddIceCandidate(
            cand.mTransportId, mungedCandidate, cand.mUfrag, obfuscatedAddr);
      }
    } else {
      pcw.impl()->StampTimecard("Failed looking up mDNS name");
    }
    pcw.impl()->mQueriedMDNSHostnames.erase(itor);
  }
}

void PeerConnectionImpl::StunAddrsHandler::OnStunAddrsAvailable(
    const mozilla::net::NrIceStunAddrArray& addrs) {
  CSFLogInfo(LOGTAG, "%s: receiving (%d) stun addrs", __FUNCTION__,
             (int)addrs.Length());
  PeerConnectionWrapper pcw(mPcHandle);
  if (!pcw.impl()) {
    return;
  }
  pcw.impl()->mStunAddrs = addrs.Clone();
  pcw.impl()->mLocalAddrsRequestState = STUN_ADDR_REQUEST_COMPLETE;
  pcw.impl()->FlushIceCtxOperationQueueIfReady();
  // If this fails, ICE cannot succeed, but we need to still go through the
  // motions.
}

void PeerConnectionImpl::InitLocalAddrs() {
  if (mLocalAddrsRequestState == STUN_ADDR_REQUEST_PENDING) {
    return;
  }
  if (mStunAddrsRequest) {
    mLocalAddrsRequestState = STUN_ADDR_REQUEST_PENDING;
    mStunAddrsRequest->SendGetStunAddrs();
  } else {
    mLocalAddrsRequestState = STUN_ADDR_REQUEST_COMPLETE;
  }
}

bool PeerConnectionImpl::ShouldForceProxy() const {
  if (Preferences::GetBool("media.peerconnection.ice.proxy_only", false)) {
    return true;
  }

  bool isPBM = false;
  // This complicated null check is being extra conservative to avoid
  // introducing crashes. It may not be needed.
  if (mWindow && mWindow->GetExtantDoc() &&
      mWindow->GetExtantDoc()->GetPrincipal() &&
      mWindow->GetExtantDoc()
          ->GetPrincipal()
          ->OriginAttributesRef()
          .IsPrivateBrowsing()) {
    isPBM = true;
  }

  if (isPBM && Preferences::GetBool(
                   "media.peerconnection.ice.proxy_only_if_pbmode", false)) {
    return true;
  }

  if (!Preferences::GetBool(
          "media.peerconnection.ice.proxy_only_if_behind_proxy", false)) {
    return false;
  }

  // Ok, we're supposed to be proxy_only, but only if a proxy is configured.
  // Let's just see if the document was loaded via a proxy.

  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal = GetChannel();
  if (!httpChannelInternal) {
    return false;
  }

  bool proxyUsed = false;
  (void)httpChannelInternal->GetIsProxyUsed(&proxyUsed);
  return proxyUsed;
}

void PeerConnectionImpl::EnsureTransports(const JsepSession& aSession) {
  mJsepSession->ForEachTransceiver([this,
                                    self = RefPtr<PeerConnectionImpl>(this)](
                                       const JsepTransceiver& transceiver) {
    if (transceiver.HasOwnTransport()) {
      mTransportHandler->EnsureProvisionalTransport(
          transceiver.mTransport.mTransportId,
          transceiver.mTransport.mLocalUfrag, transceiver.mTransport.mLocalPwd,
          transceiver.mTransport.mComponents);
    }
  });

  GatherIfReady();
}

void PeerConnectionImpl::UpdateRTCDtlsTransports() {
  mJsepSession->ForEachTransceiver([this,
                                    self = RefPtr<PeerConnectionImpl>(this)](
                                       const JsepTransceiver& jsepTransceiver) {
    std::string transportId = jsepTransceiver.mTransport.mTransportId;
    RefPtr<dom::RTCDtlsTransport> dtlsTransport;
    if (!transportId.empty()) {
      nsCString key(transportId.data(), transportId.size());
      dtlsTransport =
          mTransportIdToRTCDtlsTransport.GetOrInsertNew(key, GetParentObject());
    }

    if (jsepTransceiver.GetMediaType() == SdpMediaSection::kApplication) {
      if (mSctpTransport) {
        mSctpTransport->SetTransport(dtlsTransport.get());
      }
    } else {
      RefPtr<dom::RTCRtpTransceiver> domTransceiver =
          GetTransceiver(jsepTransceiver.GetUuid());
      if (domTransceiver) {
        domTransceiver->SetDtlsTransport(dtlsTransport);
      }
    }
  });
}

void PeerConnectionImpl::UpdateRTCSctpTransport() {
  // mDataConnection is only initialized once negotiation completes (see
  // GetDatachannelParameters).
  MaybeInitializeDataChannel();

  // The RTCSctpTransport is not a control surface, so it does not need the
  // DataConnection (or an RTCDtlsTransport) to exist. We create it as soon as
  // a data (application) m-section has appeared in an SDP, which can be as
  // early as have-remote-offer, and fill in negotiated details later.
  mJsepSession->ForEachTransceiver([&](const JsepTransceiver& jsepTransceiver) {
    if (jsepTransceiver.GetMediaType() == SdpMediaSection::kApplication) {
      // Spec says to update maxMessageSize when negotiation completes, even if
      // a remote offer has already specified it. Spec does not say anything
      // about unsetting RTCPeerConnection.sctp if a datachannel m-section is
      // rejected, and once there is a datachannel m-section it will always be
      // there.
      Nullable<double> maxMessageSize;
      if (mDataConnection) {
        maxMessageSize.SetValue(mDataConnection->GetMaxMessageSize());
      }

      // Preserve the existing object (and its transport) across renegotiation.
      if (!mSctpTransport) {
        // maxChannels stays null until the SCTP association connects; it is
        // populated in NotifySctpConnected().
        Nullable<uint16_t> maxChannels;
        mSctpTransport = MakeRefPtr<RTCSctpTransport>(
            GetParentObject(), maxMessageSize, maxChannels);
      } else {
        mSctpTransport->SetMaxMessageSize(maxMessageSize);
      }
    }
  });
}

void PeerConnectionImpl::SaveStateForRollback() {
  // This could change depending on the outcome in
  // https://github.com/w3c/webrtc-pc/issues/2899
  if (mSctpTransport) {
    // We have to save both of these things, because the DTLS transport could
    // change without the SCTP transport changing.
    mLastStableSctpTransport = mSctpTransport;
    mLastStableSctpDtlsTransport = mSctpTransport->GetTransport();
  } else {
    mLastStableSctpTransport = nullptr;
    mLastStableSctpDtlsTransport = nullptr;
  }

  for (auto& transceiver : mTransceivers) {
    transceiver->SaveStateForRollback();
  }
}

void PeerConnectionImpl::RestoreStateForRollback() {
  for (auto& transceiver : mTransceivers) {
    transceiver->RollbackToStableDtlsTransport();
  }

  mSctpTransport = mLastStableSctpTransport;
  if (mSctpTransport) {
    // The stable DTLS transport may be null (e.g. rolling back to
    // have-remote-offer, where the RTCDtlsTransport did not yet exist).
    mSctpTransport->SetTransport(mLastStableSctpDtlsTransport.get());
  }
}

std::set<RefPtr<dom::RTCDtlsTransport>>
PeerConnectionImpl::GetActiveTransports() const {
  std::set<RefPtr<dom::RTCDtlsTransport>> result;
  for (const auto& transceiver : mTransceivers) {
    if (transceiver->GetDtlsTransport()) {
      result.insert(transceiver->GetDtlsTransport());
    }
  }

  if (mSctpTransport && mSctpTransport->GetTransport()) {
    result.insert(mSctpTransport->GetTransport());
  }
  return result;
}

nsresult PeerConnectionImpl::UpdateTransports(const JsepSession& aSession,
                                              const bool forceIceTcp) {
  std::set<std::string> finalTransports;
  mJsepSession->ForEachTransceiver(
      [&, this, self = RefPtr<PeerConnectionImpl>(this)](
          const JsepTransceiver& transceiver) {
        if (transceiver.HasOwnTransport()) {
          finalTransports.insert(transceiver.mTransport.mTransportId);
          UpdateTransport(transceiver, forceIceTcp);
        }
      });

  mTransportHandler->RemoveTransportsExcept(finalTransports);

  for (const auto& transceiverImpl : mTransceivers) {
    transceiverImpl->UpdateTransport();
  }

  return NS_OK;
}

void PeerConnectionImpl::UpdateTransport(const JsepTransceiver& aTransceiver,
                                         bool aForceIceTcp) {
  std::string ufrag;
  std::string pwd;
  std::vector<std::string> candidates;
  size_t components = 0;

  const JsepTransport& transport = aTransceiver.mTransport;
  unsigned level = aTransceiver.GetLevel();

  CSFLogDebug(LOGTAG, "ACTIVATING TRANSPORT! - PC %s: level=%u components=%u",
              mHandle.c_str(), (unsigned)level,
              (unsigned)transport.mComponents);

  ufrag = transport.mIce->GetUfrag();
  pwd = transport.mIce->GetPassword();
  candidates = transport.mIce->GetCandidates();
  components = transport.mComponents;
  if (aForceIceTcp) {
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [](const std::string& s) {
                         return s.find(" UDP ") != std::string::npos ||
                                s.find(" udp ") != std::string::npos;
                       }),
        candidates.end());
  }

  nsTArray<uint8_t> keyDer;
  nsTArray<uint8_t> certDer;
  nsresult rv = Identity()->Serialize(&keyDer, &certDer);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: Failed to serialize DTLS identity: %d",
                __FUNCTION__, (int)rv);
    return;
  }

  DtlsDigestList digests;
  for (const auto& fingerprint :
       transport.mDtls->GetFingerprints().mFingerprints) {
    digests.emplace_back(ToString(fingerprint.hashFunc),
                         fingerprint.fingerprint);
  }

  mTransportHandler->ActivateTransport(
      transport.mTransportId, transport.mLocalUfrag, transport.mLocalPwd,
      components, ufrag, pwd, keyDer, certDer, Identity()->auth_type(),
      transport.mDtls->GetRole() == JsepDtlsTransport::kJsepDtlsClient, digests,
      PrivacyRequested());

  for (auto& candidate : candidates) {
    AddIceCandidate("candidate:" + candidate, transport.mTransportId, ufrag);
  }
}

nsresult PeerConnectionImpl::UpdateMediaPipelines() {
  for (RefPtr<RTCRtpTransceiver>& transceiver : mTransceivers) {
    transceiver->ResetSync();
  }

  for (RefPtr<RTCRtpTransceiver>& transceiver : mTransceivers) {
    if (!transceiver->IsVideo()) {
      nsresult rv = transceiver->SyncWithMatchingVideoConduits(mTransceivers);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }

    transceiver->UpdatePrincipalPrivacy(PrivacyRequested()
                                            ? PrincipalPrivacy::Private
                                            : PrincipalPrivacy::NonPrivate);

    nsresult rv = transceiver->UpdateConduit();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

void PeerConnectionImpl::StartIceChecks(const JsepSession& aSession) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mJsepSession->GetState() == kJsepStateStable);

  auto transports = GetActiveTransports();

  if (!mCanRegisterMDNSHostnamesDirectly) {
    for (auto& pair : mMDNSHostnamesToRegister) {
      mRegisteredMDNSHostnames.insert(pair.first);
      mStunAddrsRequest->SendRegisterMDNSHostname(
          nsCString(pair.first.c_str()), nsCString(pair.second.c_str()));
    }
    mMDNSHostnamesToRegister.clear();
    mCanRegisterMDNSHostnamesDirectly = true;
  }

  std::vector<std::string> attributes;
  if (aSession.RemoteIsIceLite()) {
    attributes.push_back("ice-lite");
  }

  if (!aSession.GetIceOptions().empty()) {
    attributes.push_back("ice-options:");
    for (const auto& option : aSession.GetIceOptions()) {
      attributes.back() += option + ' ';
    }
  }

  nsCOMPtr<nsIRunnable> runnable(
      WrapRunnable(mTransportHandler, &MediaTransportHandler::StartIceChecks,
                   aSession.IsIceControlling(), attributes));

  PerformOrEnqueueIceCtxOperation(runnable);
}

bool PeerConnectionImpl::GetPrefDefaultAddressOnly() const {
  MOZ_ASSERT(NS_IsMainThread());

  uint64_t winId = mWindow->WindowID();

  bool default_address_only = Preferences::GetBool(
      "media.peerconnection.ice.default_address_only", false);
  default_address_only |=
      !MediaManager::Get()->IsActivelyCapturingOrHasAPermission(winId);
  return default_address_only;
}

bool PeerConnectionImpl::GetPrefObfuscateHostAddresses() const {
  MOZ_ASSERT(NS_IsMainThread());

  uint64_t winId = mWindow->WindowID();

  bool obfuscate_host_addresses = Preferences::GetBool(
      "media.peerconnection.ice.obfuscate_host_addresses", false);
  obfuscate_host_addresses &=
      !MediaManager::Get()->IsActivelyCapturingOrHasAPermission(winId);
  obfuscate_host_addresses &= !media::HostnameInPref(
      "media.peerconnection.ice.obfuscate_host_addresses.blocklist", mHostname);
  obfuscate_host_addresses &= XRE_IsContentProcess();

  return obfuscate_host_addresses;
}

void PeerConnectionImpl::AddIceCandidate(const std::string& aCandidate,
                                         const std::string& aTransportId,
                                         const std::string& aUfrag) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aTransportId.empty());

  bool obfuscate_host_addresses = Preferences::GetBool(
      "media.peerconnection.ice.obfuscate_host_addresses", false);

  if (obfuscate_host_addresses && !RelayOnly()) {
    std::vector<std::string> tokens;
    TokenizeCandidate(aCandidate, tokens);

    if (tokens.size() > 4) {
      std::string addr = tokens[4];

      // Check for address ending with .local
      size_t nPeriods = std::count(addr.begin(), addr.end(), '.');
      size_t dotLocalLength = 6;  // length of ".local"

      if (nPeriods == 1 &&
          addr.rfind(".local") + dotLocalLength == addr.length()) {
        if (mStunAddrsRequest) {
          PendingIceCandidate cand;
          cand.mTokenizedCandidate = std::move(tokens);
          cand.mTransportId = aTransportId;
          cand.mUfrag = aUfrag;
          mQueriedMDNSHostnames[addr].push_back(std::move(cand));

          GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
              "PeerConnectionImpl::SendQueryMDNSHostname",
              [self = RefPtr<PeerConnectionImpl>(this),
               addr = std::move(addr)]() mutable {
                if (self->mStunAddrsRequest) {
                  self->StampTimecard("Look up mDNS name");
                  self->mStunAddrsRequest->SendQueryMDNSHostname(
                      nsCString(nsAutoCString(addr.c_str())));
                }
                NS_ReleaseOnMainThread(
                    "PeerConnectionImpl::SendQueryMDNSHostname", self.forget());
              }));
        }
        // TODO: Bug 1535690, we don't want to tell the ICE context that remote
        // trickle is done if we are waiting to resolve a mDNS candidate.
        return;
      }
    }
  }

  mTransportHandler->AddIceCandidate(aTransportId, aCandidate, aUfrag, "");
}

void PeerConnectionImpl::UpdateNetworkState(bool online) {
  if (mTransportHandler) {
    mTransportHandler->UpdateNetworkState(online);
  }
}

void PeerConnectionImpl::FlushIceCtxOperationQueueIfReady() {
  MOZ_ASSERT(NS_IsMainThread());

  if (IsIceCtxReady()) {
    for (auto& queuedIceCtxOperation : mQueuedIceCtxOperations) {
      queuedIceCtxOperation->Run();
    }
    mQueuedIceCtxOperations.clear();
  }
}

void PeerConnectionImpl::PerformOrEnqueueIceCtxOperation(
    nsIRunnable* runnable) {
  MOZ_ASSERT(NS_IsMainThread());

  if (IsIceCtxReady()) {
    runnable->Run();
  } else {
    mQueuedIceCtxOperations.push_back(runnable);
  }
}

void PeerConnectionImpl::GatherIfReady() {
  MOZ_ASSERT(NS_IsMainThread());

  // Init local addrs here so that if we re-gather after an ICE restart
  // resulting from changing WiFi networks, we get new local addrs.
  // Otherwise, we would reuse the addrs from the original WiFi network
  // and the ICE restart will fail.
  if (!mStunAddrs.Length()) {
    InitLocalAddrs();
  }

  // If we had previously queued gathering or ICE start, unqueue them
  mQueuedIceCtxOperations.clear();
  nsCOMPtr<nsIRunnable> runnable(WrapRunnable(
      RefPtr<PeerConnectionImpl>(this), &PeerConnectionImpl::EnsureIceGathering,
      GetPrefDefaultAddressOnly(), GetPrefObfuscateHostAddresses()));

  PerformOrEnqueueIceCtxOperation(runnable);
}

already_AddRefed<nsIHttpChannelInternal> PeerConnectionImpl::GetChannel()
    const {
  Document* doc = mWindow->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    NS_WARNING("Unable to get document from window");
    return nullptr;
  }

  if (!doc->GetDocumentURI()->SchemeIs("file")) {
    nsIChannel* channel = doc->GetChannel();
    if (!channel) {
      NS_WARNING("Unable to get channel from document");
      return nullptr;
    }

    nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
        do_QueryInterface(channel);
    if (NS_WARN_IF(!httpChannelInternal)) {
      CSFLogInfo(LOGTAG, "%s: Document does not have an HTTP channel",
                 __FUNCTION__);
      return nullptr;
    }
    return httpChannelInternal.forget();
  }
  return nullptr;
}

nsresult PeerConnectionImpl::SetTargetForDefaultLocalAddressLookup() {
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal = GetChannel();
  if (!httpChannelInternal) {
    return NS_OK;
  }

  nsCString remoteIp;
  nsresult rv = httpChannelInternal->GetRemoteAddress(remoteIp);
  if (NS_FAILED(rv) || remoteIp.IsEmpty()) {
    CSFLogError(LOGTAG, "%s: Failed to get remote IP address: %d", __FUNCTION__,
                (int)rv);
    return rv;
  }

  int32_t remotePort;
  rv = httpChannelInternal->GetRemotePort(&remotePort);
  if (NS_FAILED(rv)) {
    CSFLogError(LOGTAG, "%s: Failed to get remote port number: %d",
                __FUNCTION__, (int)rv);
    return rv;
  }

  mTransportHandler->SetTargetForDefaultLocalAddressLookup(remoteIp.get(),
                                                           remotePort);

  return NS_OK;
}

void PeerConnectionImpl::EnsureIceGathering(bool aDefaultRouteOnly,
                                            bool aObfuscateHostAddresses) {
  if (!mTargetForDefaultLocalAddressLookupIsSet) {
    nsresult rv = SetTargetForDefaultLocalAddressLookup();
    if (NS_FAILED(rv)) {
      NS_WARNING("Unable to set target for default local address lookup");
    }
    mTargetForDefaultLocalAddressLookupIsSet = true;
  }

  // Make sure we don't call StartIceGathering if we're in e10s mode
  // and we received no STUN addresses from the parent process.  In the
  // absence of previously provided STUN addresses, StartIceGathering will
  // attempt to gather them (as in non-e10s mode), and this will cause a
  // sandboxing exception in e10s mode.
  if (!mStunAddrs.Length() && XRE_IsContentProcess()) {
    CSFLogInfo(LOGTAG, "%s: No STUN addresses returned from parent process",
               __FUNCTION__);
    return;
  }

  mTransportHandler->StartIceGathering(aDefaultRouteOnly,
                                       aObfuscateHostAddresses, mStunAddrs);
}

already_AddRefed<dom::RTCRtpTransceiver> PeerConnectionImpl::CreateTransceiver(
    const std::string& aId, bool aIsVideo, const RTCRtpTransceiverInit& aInit,
    dom::MediaStreamTrack* aSendTrack, bool aAddTrackMagic, ErrorResult& aRv) {
  PeerConnectionCtx* ctx = PeerConnectionCtx::GetInstance();
  if (!mCall) {
    auto envWrapper = WebrtcEnvironmentWrapper::Create(GetTimestampMaker());
    mCall = WebrtcCallWrapper::Create(
        std::move(envWrapper), GetTimestampMaker(),
        media::ShutdownBlockingTicket::Create(
            u"WebrtcCallWrapper shutdown blocker"_ns,
            NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__),
        ctx->GetSharedWebrtcState());
  }

  if (aAddTrackMagic) {
    mJsepSession->ApplyToTransceiver(aId, [](JsepTransceiver& aTransceiver) {
      aTransceiver.SetAddTrackMagic();
    });
  }

  RefPtr transceiver = MakeRefPtr<RTCRtpTransceiver>(
      mWindow, PrivacyRequested(), this, mTransportHandler, mJsepSession.get(),
      aId, aIsVideo, mSTSThread.get(), aSendTrack, mCall.get(), mIdGenerator);

  transceiver->Init(aInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (aSendTrack) {
    // implement checking for peerIdentity (where failure == black/silence)
    Document* doc = mWindow->GetExtantDoc();
    if (doc) {
      transceiver->Sender()->GetPipeline()->UpdateSinkIdentity(
          doc->NodePrincipal(), GetPeerIdentity());
    } else {
      MOZ_CRASH();
      aRv = NS_ERROR_FAILURE;
      return nullptr;  // Don't remove this till we know it's safe.
    }
  }

  return transceiver.forget();
}

std::string PeerConnectionImpl::GetTransportIdMatchingSendTrack(
    const dom::MediaStreamTrack& aTrack) const {
  for (const RefPtr<RTCRtpTransceiver>& transceiver : mTransceivers) {
    if (transceiver->Sender()->HasTrack(&aTrack)) {
      return transceiver->GetTransportId();
    }
  }
  return std::string();
}

/**
 * Tells you if any local track is isolated to a specific peer identity.
 * Obviously, we want all the tracks to be isolated equally so that they can
 * all be sent or not.  We check once when we are setting a local description
 * and that determines if we flip the "privacy requested" bit on.  Once the bit
 * is on, all media originating from this peer connection is isolated.
 *
 * @returns true if any track has a peerIdentity set on it
 */
bool PeerConnectionImpl::AnyLocalTrackHasPeerIdentity() const {
  MOZ_ASSERT(NS_IsMainThread());

  for (const RefPtr<RTCRtpTransceiver>& transceiver : mTransceivers) {
    if (transceiver->Sender()->GetTrack() &&
        transceiver->Sender()->GetTrack()->GetPeerIdentity()) {
      return true;
    }
  }
  return false;
}

bool PeerConnectionImpl::AnyCodecHasPluginID(uint64_t aPluginID) {
  for (RefPtr<RTCRtpTransceiver>& transceiver : mTransceivers) {
    if (transceiver->ConduitHasPluginID(aPluginID)) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<NrSocketProxyConfig> PeerConnectionImpl::GetProxyConfig()
    const {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mForceProxy &&
      Preferences::GetBool("media.peerconnection.disable_http_proxy", false)) {
    return nullptr;
  }

  auto* browserChild = BrowserChild::GetFrom(mWindow);
  if (!browserChild) {
    // Android doesn't have browser child apparently...
    return nullptr;
  }

  Document* doc = mWindow->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    NS_WARNING("Unable to get document from window");
    return nullptr;
  }

  TabId id = browserChild->GetTabId();
  Result<RefPtr<net::LoadInfo>, nsresult> maybeLoadInfo = net::LoadInfo::Create(
      doc->NodePrincipal(), doc->NodePrincipal(), doc,
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      nsIContentPolicy::TYPE_PROXIED_WEBRTC_MEDIA);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return nullptr;
  }
  RefPtr<net::LoadInfo> loadInfo = maybeLoadInfo.unwrap();

  net::LoadInfoArgs loadInfoArgs;
  MOZ_ALWAYS_SUCCEEDS(
      mozilla::ipc::LoadInfoToLoadInfoArgs(loadInfo, &loadInfoArgs));
  return std::make_unique<NrSocketProxyConfig>(
      net::WebrtcProxyConfig(id, loadInfoArgs, mForceProxy));
}

MOZ_RUNINIT std::map<uint64_t, PeerConnectionAutoTimer>
    PeerConnectionImpl::sCallDurationTimers;
}  // namespace mozilla
