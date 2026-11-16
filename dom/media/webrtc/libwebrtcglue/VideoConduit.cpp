/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoConduit.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "MediaConduitControl.h"
#include "RtpRtcpConfig.h"
#include "Tracing.h"
#include "VideoStreamFactory.h"
#include "WebrtcCallWrapper.h"
#include "WebrtcTaskQueueWrapper.h"
#include "common/YuvStamper.h"
#include "common/browser_logging/CSFLog.h"
#include "libwebrtcglue/FrameTransformer.h"
#include "libwebrtcglue/FrameTransformerProxy.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsThreadUtils.h"
#include "transport/SrtpFlow.h"  // For SRTP_MAX_EXPANSION

// libwebrtc includes
#include <stdint.h>

#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <utility>

#include "CodecConfig.h"
#include "MainThreadUtils.h"
#include "MediaConduitErrors.h"
#include "MediaConduitInterface.h"
#include "MediaEventSource.h"
#include "PerformanceRecorder.h"
#include "VideoUtils.h"
#include "WebrtcVideoCodecFactory.h"
#include "api/call/transport.h"
#include "api/media_types.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "call/call.h"
#include "call/rtp_config.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "common_video/include/video_frame_buffer_pool.h"
#include "domstubs.h"
#include "jsapi/RTCStatsReport.h"
#include "media/base/media_constants.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "mozilla/Assertions.h"
#include "mozilla/DataMutex.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/ProfilerState.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/ReverseIterator.h"
#include "mozilla/StateWatching.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/fallible.h"
#include "mozilla/glean/DomMediaWebrtcMetrics.h"
#include "mozilla/mozalloc_oom.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIDirectTaskDispatcher.h"
#include "nsISerialEventTarget.h"
#include "nsStringFwd.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/ref_counted_object.h"
#include "transport/mediapacket.h"
#include "video/config/video_encoder_config.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "VideoEngine.h"
#endif

// for ntohs
#ifdef _MSC_VER
#  include "Winsock2.h"
#else
#  include <netinet/in.h>
#endif

#define INVALID_RTP_PAYLOAD 255  // valid payload types are 0 to 127

namespace mozilla {

namespace {

const char* vcLogTag = "WebrtcVideoSessionConduit";
#ifdef LOGTAG
#  undef LOGTAG
#endif
#define LOGTAG vcLogTag

using namespace webrtc;
using LocalDirection = MediaSessionConduitLocalDirection;

const int kNullPayloadType = -1;
const char kRtcpFbCcmParamTmmbr[] = "tmmbr";

/**
 * Perform validation on the codecConfig to be applied
 */
MediaConduitErrorCode ValidateCodecConfig(const VideoCodecConfig& codecInfo) {
  if (codecInfo.mName.empty()) {
    CSFLogError(LOGTAG, "%s Empty Payload Name ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  return kMediaConduitNoError;
}

webrtc::VideoCodecType SupportedCodecType(webrtc::VideoCodecType aType) {
  switch (aType) {
    case webrtc::VideoCodecType::kVideoCodecVP8:
    case webrtc::VideoCodecType::kVideoCodecVP9:
    case webrtc::VideoCodecType::kVideoCodecH264:
    case webrtc::VideoCodecType::kVideoCodecAV1:
      return aType;
    default:
      return webrtc::VideoCodecType::kVideoCodecGeneric;
  }
  // NOTREACHED
}

// Call thread only.
webrtc::scoped_refptr<webrtc::VideoEncoderConfig::EncoderSpecificSettings>
ConfigureVideoEncoderSettings(const VideoCodecConfig& aConfig,
                              const WebrtcVideoConduit* aConduit,
                              webrtc::CodecParameterMap& aParameters) {
  bool is_screencast =
      aConduit->CodecMode() == webrtc::VideoCodecMode::kScreensharing;
  // No automatic resizing when using simulcast or screencast.
  bool automatic_resize = !is_screencast && aConfig.mEncodings.size() <= 1;
  bool denoising;
  bool codec_default_denoising = false;
  if (is_screencast) {
    denoising = false;
  } else {
    // Use codec default if video_noise_reduction is unset.
    denoising = aConduit->Denoising();
    codec_default_denoising = !denoising;
  }

  using Av1Config = JsepVideoCodecDescription::Av1Config;
  aConfig.mAv1Config.apply([&](const Av1Config& config) {
    MOZ_ASSERT(aConfig.mName == kAv1CodecName);
    config.mProfile.apply([&](uint8_t value) {
      aParameters[std::string(kAv1FmtpProfile)] = std::to_string(value);
    });
    config.mLevelIdx.apply([&](uint8_t value) {
      aParameters[std::string(kAv1FmtpLevelIdx)] = std::to_string(value);
    });
    config.mTier.apply([&](uint8_t value) {
      aParameters[std::string(kAv1FmtpTier)] = std::to_string(value);
    });
  });

  if (aConfig.mName == kH264CodecName) {
    aParameters[kH264FmtpPacketizationMode] =
        std::to_string(aConfig.mPacketizationMode);
    {
      std::stringstream ss;
      ss << std::hex << std::setfill('0');
      ss << std::setw(2) << static_cast<uint32_t>(aConfig.mProfile);
      ss << std::setw(2) << static_cast<uint32_t>(aConfig.mConstraints);
      ss << std::setw(2) << static_cast<uint32_t>(aConfig.mLevel);
      std::string profileLevelId = ss.str();
      auto parsedProfileLevelId =
          webrtc::ParseH264ProfileLevelId(profileLevelId.c_str());
      MOZ_DIAGNOSTIC_ASSERT(parsedProfileLevelId);
      if (parsedProfileLevelId) {
        aParameters[kH264FmtpProfileLevelId] = std::move(profileLevelId);
      }
    }
    aParameters[std::string(kH264FmtpSpropParameterSets)] =
        aConfig.mSpropParameterSets;
  }
  if (aConfig.mName == kVp8CodecName) {
    webrtc::VideoCodecVP8 vp8_settings =
        webrtc::VideoEncoder::GetDefaultVp8Settings();
    vp8_settings.automaticResizeOn = automatic_resize;
    // VP8 denoising is enabled by default.
    vp8_settings.denoisingOn = codec_default_denoising ? true : denoising;
    return webrtc::scoped_refptr<
        webrtc::VideoEncoderConfig::EncoderSpecificSettings>(
        new webrtc::RefCountedObject<
            webrtc::VideoEncoderConfig::Vp8EncoderSpecificSettings>(
            vp8_settings));
  }
  if (aConfig.mName == kVp9CodecName) {
    webrtc::VideoCodecVP9 vp9_settings =
        webrtc::VideoEncoder::GetDefaultVp9Settings();
    if (!is_screencast) {
      // Always configure only 1 spatial layer for screencapture as libwebrtc
      // has some special requirements when SVC is active. For non-screencapture
      // the spatial layers are experimentally configurable via a pref.
      vp9_settings.numberOfSpatialLayers = aConduit->SpatialLayers();
    }
    // VP9 denoising is disabled by default.
    vp9_settings.denoisingOn = codec_default_denoising ? false : denoising;
    return webrtc::scoped_refptr<
        webrtc::VideoEncoderConfig::EncoderSpecificSettings>(
        new webrtc::RefCountedObject<
            webrtc::VideoEncoderConfig::Vp9EncoderSpecificSettings>(
            vp9_settings));
  }
  return nullptr;
}

uint32_t GenerateRandomSSRC() {
  uint32_t ssrc;
  do {
    SECStatus rv = PK11_GenerateRandom(reinterpret_cast<unsigned char*>(&ssrc),
                                       sizeof(ssrc));
    MOZ_RELEASE_ASSERT(rv == SECSuccess);
  } while (ssrc == 0);  // webrtc.org code has fits if you select an SSRC of 0

  return ssrc;
}

// TODO: Make this a defaulted operator when we have c++20 (bug 1731036).
bool operator!=(
    const webrtc::VideoReceiveStreamInterface::Config::Rtp& aThis,
    const webrtc::VideoReceiveStreamInterface::Config::Rtp& aOther) {
  return aThis.remote_ssrc != aOther.remote_ssrc ||
         aThis.local_ssrc != aOther.local_ssrc ||
         aThis.rtcp_mode != aOther.rtcp_mode ||
         aThis.rtcp_xr.receiver_reference_time_report !=
             aOther.rtcp_xr.receiver_reference_time_report ||
         aThis.remb != aOther.remb || aThis.tmmbr != aOther.tmmbr ||
         aThis.keyframe_method != aOther.keyframe_method ||
         aThis.lntf.enabled != aOther.lntf.enabled ||
         aThis.nack.rtp_history_ms != aOther.nack.rtp_history_ms ||
         aThis.ulpfec_payload_type != aOther.ulpfec_payload_type ||
         aThis.red_payload_type != aOther.red_payload_type ||
         aThis.rtx_ssrc != aOther.rtx_ssrc ||
         aThis.protected_by_flexfec != aOther.protected_by_flexfec ||
         aThis.rtx_associated_payload_types !=
             aOther.rtx_associated_payload_types ||
         aThis.raw_payload_types != aOther.raw_payload_types;
}

#ifdef DEBUG
// TODO: Make this a defaulted operator when we have c++20 (bug 1731036).
bool operator==(
    const webrtc::VideoReceiveStreamInterface::Config::Rtp& aThis,
    const webrtc::VideoReceiveStreamInterface::Config::Rtp& aOther) {
  return !(aThis != aOther);
}
#endif

// TODO: Make this a defaulted operator when we have c++20 (bug 1731036).
bool operator!=(const webrtc::RtpConfig& aThis,
                const webrtc::RtpConfig& aOther) {
  return aThis.ssrcs != aOther.ssrcs || aThis.rids != aOther.rids ||
         aThis.mid != aOther.mid || aThis.rtcp_mode != aOther.rtcp_mode ||
         aThis.max_packet_size != aOther.max_packet_size ||
         aThis.extmap_allow_mixed != aOther.extmap_allow_mixed ||
         aThis.extensions != aOther.extensions ||
         aThis.payload_name != aOther.payload_name ||
         aThis.payload_type != aOther.payload_type ||
         aThis.raw_payload != aOther.raw_payload ||
         aThis.lntf.enabled != aOther.lntf.enabled ||
         aThis.nack.rtp_history_ms != aOther.nack.rtp_history_ms ||
         !(aThis.ulpfec == aOther.ulpfec) ||
         aThis.flexfec.payload_type != aOther.flexfec.payload_type ||
         aThis.flexfec.ssrc != aOther.flexfec.ssrc ||
         aThis.flexfec.protected_media_ssrcs !=
             aOther.flexfec.protected_media_ssrcs ||
         aThis.rtx.ssrcs != aOther.rtx.ssrcs ||
         aThis.rtx.payload_type != aOther.rtx.payload_type ||
         aThis.c_name != aOther.c_name;
}

#ifdef DEBUG
// TODO: Make this a defaulted operator when we have c++20 (bug 1731036).
bool operator==(const webrtc::RtpConfig& aThis,
                const webrtc::RtpConfig& aOther) {
  return !(aThis != aOther);
}
#endif

}  // namespace

void RecvSinkProxy::OnFrame(const webrtc::VideoFrame& aFrame) {
  MOZ_ALWAYS_SUCCEEDS(mOwner->mFrameRecvThread->Dispatch(NS_NewRunnableFunction(
      __FUNCTION__,
      [owner = RefPtr(mOwner), aFrame] { owner->OnRecvFrame(aFrame); })));
}

void SendSinkProxy::OnFrame(const webrtc::VideoFrame& aFrame) {
  MOZ_ALWAYS_SUCCEEDS(mOwner->mCallThread->Dispatch(NS_NewRunnableFunction(
      __FUNCTION__,
      [owner = RefPtr(mOwner), aFrame] { owner->OnSendFrame(aFrame); })));
}

/**
 * Factory Method for VideoConduit
 */
RefPtr<VideoSessionConduit> VideoSessionConduit::Create(
    RefPtr<WebrtcCallWrapper> aCall, nsCOMPtr<nsISerialEventTarget> aStsThread,
    Options aOptions, std::string aPCHandle,
    const TrackingId& aRecvTrackingId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCall, "missing required parameter: aCall");
  CSFLogVerbose(LOGTAG, "%s", __FUNCTION__);

  if (!aCall) {
    return nullptr;
  }

  auto obj = MakeRefPtr<WebrtcVideoConduit>(
      std::move(aCall), std::move(aStsThread), std::move(aOptions),
      std::move(aPCHandle), aRecvTrackingId);
  if (obj->Init() != kMediaConduitNoError) {
    CSFLogError(LOGTAG, "%s VideoConduit Init Failed ", __FUNCTION__);
    return nullptr;
  }
  CSFLogVerbose(LOGTAG, "%s Successfully created VideoConduit %p", __FUNCTION__,
                obj.get());
  return obj;
}

#define INIT_MIRROR(name, val) \
  name(aCallThread, val, "WebrtcVideoConduit::Control::" #name " (Mirror)")
WebrtcVideoConduit::Control::Control(const RefPtr<AbstractThread>& aCallThread)
    : INIT_MIRROR(mReceiving, false),
      INIT_MIRROR(mTransmitting, false),
      INIT_MIRROR(mLocalSsrcs, Ssrcs()),
      INIT_MIRROR(mLocalRtxSsrcs, Ssrcs()),
      INIT_MIRROR(mLocalCname, std::string()),
      INIT_MIRROR(mMid, std::string()),
      INIT_MIRROR(mRemoteSsrc, 0),
      INIT_MIRROR(mRemoteRtxSsrc, 0),
      INIT_MIRROR(mSyncGroup, std::string()),
      INIT_MIRROR(mLocalRecvRtpExtensions, RtpExtList()),
      INIT_MIRROR(mLocalSendRtpExtensions, RtpExtList()),
      INIT_MIRROR(mSendCodec, Nothing()),
      INIT_MIRROR(mSendRtpRtcpConfig, Nothing()),
      INIT_MIRROR(mRecvCodecs, std::vector<VideoCodecConfig>()),
      INIT_MIRROR(mRecvRtpRtcpConfig, Nothing()),
      INIT_MIRROR(mCodecMode, webrtc::VideoCodecMode::kRealtimeVideo),
      INIT_MIRROR(mFrameTransformerProxySend, nullptr),
      INIT_MIRROR(mFrameTransformerProxyRecv, nullptr),
      INIT_MIRROR(mVideoDegradationPreference,
                  webrtc::DegradationPreference::DISABLED) {}
#undef INIT_MIRROR

#define INIT_CANONICAL(name, thread, val) \
  name(thread, val, "WebrtcVideoConduit::" #name " (Canonical)")
#define INIT_MIRROR(name, val)            \
  name(AbstractThread::MainThread(), val, \
       "WebrtcVideoConduit::" #name " (Mirror)")
WebrtcVideoConduit::WebrtcVideoConduit(
    RefPtr<WebrtcCallWrapper> aCall, nsCOMPtr<nsISerialEventTarget> aStsThread,
    Options aOptions, std::string aPCHandle, const TrackingId& aRecvTrackingId)
    : mRendererMonitor("WebrtcVideoConduit::mRendererMonitor"),
      mCall(std::move(aCall)),
      mCallThread(mCall->mCallThread),
      mStsThread(std::move(aStsThread)),
      mFrameRecvThread(CreateWebrtcTaskQueueWrapper(
          GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER),
          "WebrtcVideoConduit::mFrameRecvThread"_ns,
          TailDispatchPolicy::ConsistentOrdering)),
      mControl(mCall->mCallThread),
      INIT_CANONICAL(mReceivingSize, mFrameRecvThread, {}),
      mWatchManager(this, mCall->mCallThread),
      mMutex("WebrtcVideoConduit::mMutex"),
      mDecoderFactory(std::make_unique<WebrtcVideoDecoderFactory>(
          mCallThread.get(), aPCHandle, aRecvTrackingId)),
      mEncoderFactory(std::make_unique<WebrtcVideoEncoderFactory>(
          mCallThread.get(), std::move(aPCHandle))),
      mRecvSinkProxy(this),
      mSendSinkProxy(this),
      mEngineTransmitting(false),
      mEngineReceiving(false),
      mSourceTracker(webrtc::Clock::GetRealTimeClockOnlyUseForRelativeTime()),
      mVideoLatencyTestEnable(aOptions.mVideoLatencyTestEnable),
      mMinBitrate(aOptions.mMinBitrate),
      mStartBitrate(aOptions.mStartBitrate),
      mPrefMaxBitrate(aOptions.mPrefMaxBitrate),
      mMinBitrateEstimate(aOptions.mMinBitrateEstimate),
      mDenoising(aOptions.mDenoising),
      mLockScaling(aOptions.mLockScaling),
      mSpatialLayers(aOptions.mSpatialLayers),
      mTemporalLayers(aOptions.mTemporalLayers),
      mSendTransport(this),
      mRecvTransport(this),
      mSendStreamConfig(&mSendTransport),
      mVideoStreamFactory("WebrtcVideoConduit::mVideoStreamFactory"),
      mRecvStreamConfig(&mRecvTransport),
      INIT_CANONICAL(mCanonicalRtpSources, mCall->mCallThread, {}),
      INIT_MIRROR(mRtpSources, {}) {
  mRecvStreamConfig.rtp.rtcp_event_observer = this;
}
#undef INIT_MIRROR
#undef INIT_CANONICAL

WebrtcVideoConduit::~WebrtcVideoConduit() {
  CSFLogDebug(LOGTAG, "%s ", __FUNCTION__);
  MOZ_ASSERT(!mSendStream && !mRecvStream,
             "Call DeleteStreams prior to ~WebrtcVideoConduit.");
}

#define CONNECT(aCanonical, aMirror)                                       \
  do {                                                                     \
    /* Ensure the watchmanager is wired up before the mirror receives its  \
     * initial mirrored value. */                                          \
    mCall->mCallThread->DispatchStateChange(                               \
        NS_NewRunnableFunction(__func__, [this, self = RefPtr(this)] {     \
          mWatchManager.Watch(aMirror,                                     \
                              &WebrtcVideoConduit::OnControlConfigChange); \
        }));                                                               \
    (aCanonical).ConnectMirror(&(aMirror));                                \
  } while (0)

void WebrtcVideoConduit::InitControl(VideoConduitControlInterface* aControl) {
  MOZ_ASSERT(NS_IsMainThread());

  CONNECT(aControl->CanonicalReceiving(), mControl.mReceiving);
  CONNECT(aControl->CanonicalTransmitting(), mControl.mTransmitting);
  CONNECT(aControl->CanonicalLocalSsrcs(), mControl.mLocalSsrcs);
  CONNECT(aControl->CanonicalLocalVideoRtxSsrcs(), mControl.mLocalRtxSsrcs);
  CONNECT(aControl->CanonicalLocalCname(), mControl.mLocalCname);
  CONNECT(aControl->CanonicalMid(), mControl.mMid);
  CONNECT(aControl->CanonicalRemoteSsrc(), mControl.mRemoteSsrc);
  CONNECT(aControl->CanonicalRemoteVideoRtxSsrc(), mControl.mRemoteRtxSsrc);
  CONNECT(aControl->CanonicalSyncGroup(), mControl.mSyncGroup);
  CONNECT(aControl->CanonicalLocalRecvRtpExtensions(),
          mControl.mLocalRecvRtpExtensions);
  CONNECT(aControl->CanonicalLocalSendRtpExtensions(),
          mControl.mLocalSendRtpExtensions);
  CONNECT(aControl->CanonicalVideoSendCodec(), mControl.mSendCodec);
  CONNECT(aControl->CanonicalVideoSendRtpRtcpConfig(),
          mControl.mSendRtpRtcpConfig);
  CONNECT(aControl->CanonicalVideoRecvCodecs(), mControl.mRecvCodecs);
  CONNECT(aControl->CanonicalVideoRecvRtpRtcpConfig(),
          mControl.mRecvRtpRtcpConfig);
  CONNECT(aControl->CanonicalVideoCodecMode(), mControl.mCodecMode);
  CONNECT(aControl->CanonicalFrameTransformerProxySend(),
          mControl.mFrameTransformerProxySend);
  CONNECT(aControl->CanonicalFrameTransformerProxyRecv(),
          mControl.mFrameTransformerProxyRecv);
  CONNECT(aControl->CanonicalVideoDegradationPreference(),
          mControl.mVideoDegradationPreference);
}

#undef CONNECT

void WebrtcVideoConduit::OnControlConfigChange() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  bool encoderReconfigureNeeded = false;
  bool remoteSsrcUpdateNeeded = false;
  bool sendStreamRecreationNeeded = false;
  bool sendSourceUpdateNeeded = false;

  if (mControl.mRemoteSsrc.Ref() != mControl.mConfiguredRemoteSsrc) {
    mControl.mConfiguredRemoteSsrc = mControl.mRemoteSsrc;
    remoteSsrcUpdateNeeded = true;
  }

  if (mControl.mRemoteRtxSsrc.Ref() != mControl.mConfiguredRemoteRtxSsrc) {
    mControl.mConfiguredRemoteRtxSsrc = mControl.mRemoteRtxSsrc;
    remoteSsrcUpdateNeeded = true;
  }

  if (mControl.mSyncGroup.Ref() != mRecvStreamConfig.sync_group) {
    mRecvStreamConfig.sync_group = mControl.mSyncGroup;
  }

  if (const auto [codecConfigList, rtpRtcpConfig] = std::make_pair(
          mControl.mRecvCodecs.Ref(), mControl.mRecvRtpRtcpConfig.Ref());
      !codecConfigList.empty() && rtpRtcpConfig.isSome() &&
      (codecConfigList != mControl.mConfiguredRecvCodecs ||
       rtpRtcpConfig != mControl.mConfiguredRecvRtpRtcpConfig)) {
    mControl.mConfiguredRecvCodecs = codecConfigList;
    mControl.mConfiguredRecvRtpRtcpConfig = rtpRtcpConfig;
    webrtc::VideoReceiveStreamInterface::Config::Rtp newRtp(
        mRecvStreamConfig.rtp);
    MOZ_ASSERT(newRtp == mRecvStreamConfig.rtp);
    newRtp.rtx_associated_payload_types.clear();
    newRtp.rtcp_mode = rtpRtcpConfig->GetRtcpMode();
    newRtp.nack.rtp_history_ms = 0;
    newRtp.remb = false;
    newRtp.tmmbr = false;
    newRtp.keyframe_method = webrtc::KeyFrameReqMethod::kNone;
    newRtp.ulpfec_payload_type = kNullPayloadType;
    newRtp.red_payload_type = kNullPayloadType;
    bool use_fec = false;
    std::vector<webrtc::VideoReceiveStreamInterface::Decoder> recv_codecs;

    // Try Applying the codecs in the list
    // we treat as success if at least one codec was applied and reception was
    // started successfully.
    for (const auto& codec_config : codecConfigList) {
      if (auto condError = ValidateCodecConfig(codec_config);
          condError != kMediaConduitNoError) {
        CSFLogError(LOGTAG, "Invalid recv codec config for %s decoder: %i",
                    codec_config.mName.c_str(), condError);
        continue;
      }

      if (codec_config.mName == kUlpfecCodecName) {
        newRtp.ulpfec_payload_type = codec_config.mType;
        continue;
      }

      // Set RTX associated PT here so we can set it for RED without additional
      // checks for things like preventing creating an unncessary decoder for
      // RED. This assumes that any codecs created with RTX enabled
      // (including those not found in SupportedCodecType) intend to use it.
      if (codec_config.RtxPayloadTypeIsSet()) {
        newRtp.rtx_associated_payload_types[codec_config.mRTXPayloadType] =
            codec_config.mType;
      }

      if (codec_config.mName == kRedCodecName) {
        newRtp.red_payload_type = codec_config.mType;
        continue;
      }

      if (SupportedCodecType(
              webrtc::PayloadStringToCodecType(codec_config.mName)) ==
          webrtc::VideoCodecType::kVideoCodecGeneric) {
        CSFLogError(LOGTAG, "%s Unknown decoder type: %s", __FUNCTION__,
                    codec_config.mName.c_str());
        continue;
      }

      // Check for the keyframe request type: PLI is preferred over FIR, and FIR
      // is preferred over none.
      if (codec_config.RtcpFbNackIsSet(std::string(kRtcpFbNackParamPli))) {
        newRtp.keyframe_method = webrtc::KeyFrameReqMethod::kPliRtcp;
      } else if (newRtp.keyframe_method !=
                     webrtc::KeyFrameReqMethod::kPliRtcp &&
                 codec_config.RtcpFbCcmIsSet(std::string(kRtcpFbCcmParamFir))) {
        newRtp.keyframe_method = webrtc::KeyFrameReqMethod::kFirRtcp;
      }

      // What if codec A has Nack and REMB, and codec B has TMMBR, and codec C
      // has none? In practice, that's not a useful configuration, and
      // VideoReceiveStream::Config can't represent that, so simply union the
      // (boolean) settings
      if (codec_config.RtcpFbNackIsSet(std::string(kParamValueEmpty))) {
        newRtp.nack.rtp_history_ms = 1000;
      }
      newRtp.tmmbr |= codec_config.RtcpFbCcmIsSet(kRtcpFbCcmParamTmmbr);
      newRtp.remb |= codec_config.RtcpFbRembIsSet();
      use_fec |= codec_config.RtcpFbFECIsSet();

      auto& decoder = recv_codecs.emplace_back();
      decoder.video_format = webrtc::SdpVideoFormat(codec_config.mName);
      decoder.payload_type = codec_config.mType;
    }

    if (!use_fec) {
      // Reset to defaults
      newRtp.ulpfec_payload_type = kNullPayloadType;
      newRtp.red_payload_type = kNullPayloadType;
    }

    // TODO: This would be simpler, but for some reason gives
    //       "error: invalid operands to binary expression
    //       ('webrtc::VideoReceiveStreamInterface::Decoder' and
    //       'webrtc::VideoReceiveStreamInterface::Decoder')"
    // if (recv_codecs != mRecvStreamConfig.decoders) {
    if (!std::equal(recv_codecs.begin(), recv_codecs.end(),
                    mRecvStreamConfig.decoders.begin(),
                    mRecvStreamConfig.decoders.end(),
                    [](const auto& aLeft, const auto& aRight) {
                      return aLeft == aRight;
                    })) {
      if (recv_codecs.empty()) {
        CSFLogError(LOGTAG, "%s Found no valid receive codecs", __FUNCTION__);
      }
      mRecvStreamConfig.decoders = std::move(recv_codecs);
    }

    if (mRecvStreamConfig.rtp != newRtp) {
      mRecvStreamConfig.rtp = newRtp;
      remoteSsrcUpdateNeeded = true;
    }
  }

  {
    // mSendStreamConfig and other members need the lock
    MutexAutoLock lock(mMutex);
    if (mControl.mLocalSsrcs.Ref() != mSendStreamConfig.rtp.ssrcs) {
      mSendStreamConfig.rtp.ssrcs = mControl.mLocalSsrcs;
      sendStreamRecreationNeeded = true;

      const uint32_t localSsrc = mSendStreamConfig.rtp.ssrcs.empty()
                                     ? 0
                                     : mSendStreamConfig.rtp.ssrcs.front();
      if (localSsrc != mRecvStreamConfig.rtp.local_ssrc) {
        mRecvStreamConfig.rtp.local_ssrc = localSsrc;
      }
    }

    {
      Ssrcs localRtxSsrcs = mControl.mLocalRtxSsrcs.Ref();
      if (!mControl.mSendCodec.Ref()
               .map([](const auto& aCodec) {
                 return aCodec.RtxPayloadTypeIsSet();
               })
               .valueOr(false)) {
        localRtxSsrcs.clear();
      }
      if (localRtxSsrcs != mSendStreamConfig.rtp.rtx.ssrcs) {
        mSendStreamConfig.rtp.rtx.ssrcs = localRtxSsrcs;
        sendStreamRecreationNeeded = true;
      }
    }

    if (mControl.mLocalCname.Ref() != mSendStreamConfig.rtp.c_name) {
      mSendStreamConfig.rtp.c_name = mControl.mLocalCname;
      sendStreamRecreationNeeded = true;
    }

    if (mControl.mMid.Ref() != mSendStreamConfig.rtp.mid) {
      mSendStreamConfig.rtp.mid = mControl.mMid;
      sendStreamRecreationNeeded = true;
    }

    if (mControl.mLocalSendRtpExtensions.Ref() !=
        mSendStreamConfig.rtp.extensions) {
      mSendStreamConfig.rtp.extensions = mControl.mLocalSendRtpExtensions;
      sendStreamRecreationNeeded = true;
    }

    if (const auto [codecConfig, rtpRtcpConfig] = std::make_pair(
            mControl.mSendCodec.Ref(), mControl.mSendRtpRtcpConfig.Ref());
        codecConfig.isSome() && rtpRtcpConfig.isSome() &&
        (codecConfig != mControl.mConfiguredSendCodec ||
         rtpRtcpConfig != mControl.mConfiguredSendRtpRtcpConfig)) {
      CSFLogDebug(LOGTAG, "Configuring codec %s", codecConfig->mName.c_str());

      if (mControl.mConfiguredSendCodec.isSome() &&
          (mControl.mConfiguredSendCodec->mName != codecConfig->mName)) {
        // This tends to happen if the codec is changed mid call.
        // We need to delete the stream now, if we continue to setup the new
        // codec before deleting the send stream libwebrtc will throw erros.
        MemoSendStreamStats();
        DeleteSendStream();
      }
      mControl.mConfiguredSendCodec = codecConfig;
      mControl.mConfiguredSendRtpRtcpConfig = rtpRtcpConfig;

      if (ValidateCodecConfig(*codecConfig) == kMediaConduitNoError) {
        encoderReconfigureNeeded = true;
        sendSourceUpdateNeeded = true;

        mCurSendCodecConfig = codecConfig;

        size_t streamCount = std::min(codecConfig->mEncodings.size(),
                                      (size_t)webrtc::kMaxSimulcastStreams);
        MOZ_RELEASE_ASSERT(streamCount >= 1,
                           "streamCount should be at least one");

        CSFLogDebug(LOGTAG,
                    "Updating send codec for VideoConduit:%p stream count:%zu",
                    this, streamCount);

        // So we can comply with b=TIAS/b=AS/maxbr=X when input resolution
        // changes
        MOZ_ASSERT(codecConfig->mTias < INT_MAX);
        mNegotiatedMaxBitrate = static_cast<int>(codecConfig->mTias);

        if (!mLastSize && mMinBitrateEstimate != 0) {
          // Only do this at the start; use "have we sent a frame" as a
          // reasonable stand-in. min <= start <= max (but all three parameters
          // are optional)
          webrtc::BitrateSettings settings;
          settings.min_bitrate_bps = mMinBitrateEstimate;
          settings.start_bitrate_bps = mMinBitrateEstimate;
          mCall->Call()->SetClientBitratePreferences(settings);
        }

        // XXX parse the encoded SPS/PPS data and set
        // spsData/spsLen/ppsData/ppsLen
        mEncoderConfig.video_format =
            webrtc::SdpVideoFormat(codecConfig->mName);
        mEncoderConfig.encoder_specific_settings =
            ConfigureVideoEncoderSettings(
                *codecConfig, this, mEncoderConfig.video_format.parameters);

        mEncoderConfig.codec_type = SupportedCodecType(
            webrtc::PayloadStringToCodecType(codecConfig->mName));
        MOZ_RELEASE_ASSERT(mEncoderConfig.codec_type !=
                           webrtc::VideoCodecType::kVideoCodecGeneric);

        mEncoderConfig.content_type =
            mControl.mCodecMode.Ref() == webrtc::VideoCodecMode::kRealtimeVideo
                ? webrtc::VideoEncoderConfig::ContentType::kRealtimeVideo
                : webrtc::VideoEncoderConfig::ContentType::kScreen;

        mEncoderConfig.frame_drop_enabled = true;

        mEncoderConfig.min_transmit_bitrate_bps = mMinBitrate;

        // Set the max bitrate, defaulting to 10Mbps, checking:
        // - pref
        // - b=TIAS
        // - codec constraints
        // - encoding parameter if there's a single stream
        int maxBps = KBPS(10000);
        maxBps = MinIgnoreZero(maxBps, mPrefMaxBitrate);
        maxBps = MinIgnoreZero(maxBps, mNegotiatedMaxBitrate);
        maxBps = MinIgnoreZero(
            maxBps,
            SaturatingCast<int>(codecConfig->mEncodingConstraints.maxBr));
        if (codecConfig->mEncodings.size() == 1) {
          maxBps = MinIgnoreZero(
              maxBps, SaturatingCast<int>(
                          codecConfig->mEncodings[0].constraints.maxBr));
        }
        mEncoderConfig.max_bitrate_bps = maxBps;

        // TODO this is for webrtc-priority, but needs plumbing bits
        mEncoderConfig.bitrate_priority = 1.0;

        // Populate simulcast_layers with their config.
        mEncoderConfig.simulcast_layers.clear();
        const auto& codecConstraints = codecConfig->mEncodingConstraints;
        for (size_t idx = 0; idx < streamCount; ++idx) {
          webrtc::VideoStream video_stream;
          const auto& encoding = codecConfig->mEncodings[idx];
          const auto& encodingConstraints = encoding.constraints;

          video_stream.active = encoding.active;

          // Dimensions set here are used by libwebrtc to set the maximum to
          // request from the source, to support scaleDownTo without another
          // encoder reconfiguration on the first frame.
          // We don't support scaleDownTo but use max-width and max-height if
          // signaled.
          video_stream.width = codecConfig->mEncodingConstraints.maxWidth;
          video_stream.height = codecConfig->mEncodingConstraints.maxHeight;

          // Max framerate is also used to cap the source, to avoid processing
          // frames that will have to be dropped. Our signals here are
          // RTCRtpEncodingParameters.maxFramerate (per encoding), max-fr for
          // supported codecs, and any macroblocks-per-second cap implied by a
          // negotiated level (H264 Annex A Table A-1 / AV1 Annex A.3). Since
          // resolution is always scaled to fit within maxFs (see
          // VideoStreamFactory::CalculateScaledResolution), maxMbps / maxFs
          // is a safe worst-case framerate ceiling for whatever resolution
          // ends up being used.
          Maybe<double> levelMaxFps;
          if (codecConstraints.maxMbps && codecConstraints.maxFs) {
            levelMaxFps = Some(static_cast<double>(codecConstraints.maxMbps) /
                               codecConstraints.maxFs);
          }
          video_stream.max_framerate = static_cast<int>(([&]() {
            Maybe<double> fps;
            for (const auto& candidate :
                 {codecConstraints.maxFps, encodingConstraints.maxFps,
                  levelMaxFps}) {
              if (!candidate) {
                continue;
              }
              fps = fps ? Some(std::min(*fps, *candidate)) : candidate;
            }
            return fps.valueOr(-1);
          })());

          // Set each layer's max-bitrate explicitly or libwebrtc may ignore all
          // stream-specific max-bitrate settings later on, as provided by the
          // VideoStreamFactory. Default to our max of 10Mbps, overridden by
          // SDP/JS.
          int maxBps = KBPS(10000);
          maxBps = MinIgnoreZero(maxBps, mPrefMaxBitrate);
          maxBps = MinIgnoreZero(maxBps, mNegotiatedMaxBitrate);
          maxBps = MinIgnoreZero(
              maxBps, SaturatingCast<int>(encodingConstraints.maxBr));
          video_stream.max_bitrate_bps = maxBps;

          // At this time, other values are not used until after
          // CreateEncoderStreams(). We fill these in directly from the codec
          // config in VideoStreamFactory.

          mEncoderConfig.simulcast_layers.push_back(video_stream);
        }

        // Expected max number of encodings
        mEncoderConfig.number_of_streams =
            mEncoderConfig.simulcast_layers.size();

        // libwebrtc disables this by default.
        mSendStreamConfig.suspend_below_min_bitrate = false;

        webrtc::RtpConfig newRtp = mSendStreamConfig.rtp;
        MOZ_ASSERT(newRtp == mSendStreamConfig.rtp);
        newRtp.payload_name = codecConfig->mName;
        newRtp.payload_type = codecConfig->mType;
        newRtp.rtcp_mode = rtpRtcpConfig->GetRtcpMode();
        newRtp.extmap_allow_mixed = rtpRtcpConfig->GetExtmapAllowMixed();
        newRtp.max_packet_size = kVideoMtu;
        newRtp.rtx.payload_type = codecConfig->RtxPayloadTypeIsSet()
                                      ? codecConfig->mRTXPayloadType
                                      : kNullPayloadType;

        {
          // See Bug 1297058, enabling FEC when basic NACK is to be enabled in
          // H.264 is problematic
          const bool useFECDefaults =
              !codecConfig->RtcpFbFECIsSet() ||
              (codecConfig->mName == kH264CodecName &&
               codecConfig->RtcpFbNackIsSet(std::string(kParamValueEmpty)));
          newRtp.ulpfec.ulpfec_payload_type =
              useFECDefaults ? kNullPayloadType
                             : codecConfig->mULPFECPayloadType;
          newRtp.ulpfec.red_payload_type =
              useFECDefaults ? kNullPayloadType : codecConfig->mREDPayloadType;
          newRtp.ulpfec.red_rtx_payload_type =
              useFECDefaults ? kNullPayloadType
                             : codecConfig->mREDRTXPayloadType;
        }

        newRtp.nack.rtp_history_ms =
            codecConfig->RtcpFbNackIsSet(std::string(kParamValueEmpty)) ? 1000
                                                                        : 0;

        newRtp.rids.clear();
        if (!codecConfig->mEncodings.empty() &&
            !codecConfig->mEncodings[0].rid.empty()) {
          for (const auto& encoding : codecConfig->mEncodings) {
            newRtp.rids.push_back(encoding.rid);
          }
        }

        if (mSendStreamConfig.rtp != newRtp) {
          mSendStreamConfig.rtp = newRtp;
          sendStreamRecreationNeeded = true;
        }
      }
    }

    if (mControl.mConfiguredDegradationPreference !=
        mControl.mVideoDegradationPreference) {
      mControl.mConfiguredDegradationPreference =
          mControl.mVideoDegradationPreference.Ref();
      if (mSendStream) {
        mSendStream->SetSource(mTrackSource, DegradationPreference());
      }
    }

    {
      const auto& mode = mControl.mCodecMode.Ref();
      MOZ_ASSERT(mode == webrtc::VideoCodecMode::kRealtimeVideo ||
                 mode == webrtc::VideoCodecMode::kScreensharing);

      auto contentType =
          mode == webrtc::VideoCodecMode::kRealtimeVideo
              ? webrtc::VideoEncoderConfig::ContentType::kRealtimeVideo
              : webrtc::VideoEncoderConfig::ContentType::kScreen;

      if (contentType != mEncoderConfig.content_type) {
        encoderReconfigureNeeded = true;
        sendSourceUpdateNeeded = true;
        mEncoderConfig.content_type = contentType;
      }
    }

    if (mControl.mConfiguredFrameTransformerProxySend.get() !=
        mControl.mFrameTransformerProxySend.Ref().get()) {
      mControl.mConfiguredFrameTransformerProxySend =
          mControl.mFrameTransformerProxySend.Ref();
      if (!mSendStreamConfig.frame_transformer) {
        mSendStreamConfig.frame_transformer =
            new webrtc::RefCountedObject<FrameTransformer>(true);
        sendStreamRecreationNeeded = true;
      }
      static_cast<FrameTransformer*>(mSendStreamConfig.frame_transformer.get())
          ->SetProxy(mControl.mConfiguredFrameTransformerProxySend);
    }

    if (mControl.mConfiguredFrameTransformerProxyRecv.get() !=
        mControl.mFrameTransformerProxyRecv.Ref().get()) {
      mControl.mConfiguredFrameTransformerProxyRecv =
          mControl.mFrameTransformerProxyRecv.Ref();
      if (!mRecvStreamConfig.frame_transformer) {
        mRecvStreamConfig.frame_transformer =
            new webrtc::RefCountedObject<FrameTransformer>(true);
      }
      static_cast<FrameTransformer*>(mRecvStreamConfig.frame_transformer.get())
          ->SetProxy(mControl.mConfiguredFrameTransformerProxyRecv);
      // No flag to set, we always recreate recv streams
    }

    if (remoteSsrcUpdateNeeded) {
      SetRemoteSSRCConfig(mControl.mConfiguredRemoteSsrc,
                          mControl.mConfiguredRemoteRtxSsrc);
    }

    // Handle un-signalled SSRCs by creating random ones and then when they
    // actually get set, we'll destroy and recreate.
    if (mControl.mReceiving || mControl.mTransmitting) {
      const auto remoteSsrc = mRecvStreamConfig.rtp.remote_ssrc;
      const auto localSsrc = mRecvStreamConfig.rtp.local_ssrc;
      const auto localSsrcs = mSendStreamConfig.rtp.ssrcs;
      EnsureLocalSSRC();
      if (mControl.mReceiving) {
        EnsureRemoteSSRC();
      }
      if (localSsrc != mRecvStreamConfig.rtp.local_ssrc ||
          remoteSsrc != mRecvStreamConfig.rtp.remote_ssrc) {
      }
      if (localSsrcs != mSendStreamConfig.rtp.ssrcs) {
        sendStreamRecreationNeeded = true;
      }
    }

    // Recreate receiving streams
    if (mControl.mReceiving) {
      DeleteRecvStream();
      CreateRecvStream();
    }
    if (sendStreamRecreationNeeded) {
      encoderReconfigureNeeded = false;
      MemoSendStreamStats();
      DeleteSendStream();
    }
    if (mControl.mTransmitting) {
      CreateSendStream();
    }
  }

  // We make sure to not hold the lock while stopping/starting/reconfiguring
  // streams, so as to not cause deadlocks. These methods can cause our platform
  // codecs to dispatch sync runnables to main, and main may grab the lock.

  if (mSendStream) {
    if (encoderReconfigureNeeded) {
      MOZ_DIAGNOSTIC_ASSERT(
          mSendStreamConfig.rtp.ssrcs.size() ==
              mEncoderConfig.number_of_streams,
          "Each video substream must have a corresponding ssrc.");
      mEncoderConfig.video_stream_factory = CreateVideoStreamFactory();
      for (const auto& stream : mEncoderConfig.simulcast_layers) {
        CSFLogDebug(
            LOGTAG,
            "%s Reconfigure with simulcast stream maxFps=%d, "
            "bitrate=[%dkbps, %dkbps, %dkbps]",
            __FUNCTION__, stream.max_framerate, stream.min_bitrate_bps / 1000,
            stream.target_bitrate_bps / 1000, stream.max_bitrate_bps / 1000);
      }
      mSendStream->ReconfigureVideoEncoder(mEncoderConfig.Copy());
    }
    if (sendSourceUpdateNeeded && mTrackSource) {
      mSendStream->SetSource(mTrackSource, DegradationPreference());
    }
  }

  if (!mControl.mReceiving) {
    StopReceiving();
  }
  if (!mControl.mTransmitting) {
    StopTransmitting();
  }

  if (mControl.mReceiving) {
    StartReceiving();
  }
  if (mControl.mTransmitting) {
    StartTransmitting();
  }
}

std::vector<unsigned int> WebrtcVideoConduit::GetLocalSSRCs() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mSendStreamConfig.rtp.ssrcs;
}

Maybe<Ssrc> WebrtcVideoConduit::GetAssociatedLocalRtxSSRC(Ssrc aSsrc) const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  for (size_t i = 0; i < mSendStreamConfig.rtp.ssrcs.size() &&
                     i < mSendStreamConfig.rtp.rtx.ssrcs.size();
       ++i) {
    if (mSendStreamConfig.rtp.ssrcs[i] == aSsrc) {
      return Some(mSendStreamConfig.rtp.rtx.ssrcs[i]);
    }
  }
  return Nothing();
}

Maybe<Ssrc> WebrtcVideoConduit::GetAssociatedRemoteRtxSSRC() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (mRecvStreamConfig.rtp.rtx_ssrc) {
    return Some(mRecvStreamConfig.rtp.rtx_ssrc);
  }
  return Nothing();
}

Maybe<gfx::IntSize> WebrtcVideoConduit::GetLastResolution() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mLastSize;
}

void WebrtcVideoConduit::DeleteSendStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  if (!mSendStream) {
    return;
  }
  mCall->Call()->DestroyVideoSendStream(mSendStream);
  mEngineTransmitting = false;
  mSendStream = nullptr;

  // Reset base_seqs in case ssrcs get re-used.
  mRtpSendBaseSeqs.clear();
}

void WebrtcVideoConduit::MemoSendStreamStats() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  // If we are going to be recreating the send stream, we hold onto stats until
  // libwebrtc stats collection catches up.
  if (mControl.mTransmitting && mSendStream) {
    const auto stats = mSendStream->GetStats();
    // If we have no streams we don't need to hold onto anything
    if (stats.substreams.size()) {
      mTransitionalSendStreamStats = Some(stats);
    }
  }
}

void WebrtcVideoConduit::CreateSendStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  if (mSendStream) {
    return;
  }

  glean::webrtc_video::send_codec_used
      .Get(nsDependentCString(mSendStreamConfig.rtp.payload_name.c_str()))
      .Add(1);

  mEncoderConfig.video_stream_factory = CreateVideoStreamFactory();

  mSendStreamConfig.encoder_settings.encoder_factory = mEncoderFactory.get();
  mSendStreamConfig.encoder_settings.bitrate_allocator_factory =
      mCall->mVideoBitrateAllocatorFactory.get();

  MOZ_DIAGNOSTIC_ASSERT(
      mSendStreamConfig.rtp.ssrcs.size() == mEncoderConfig.number_of_streams,
      "Each video substream must have a corresponding ssrc.");

  mSendStream = mCall->Call()->CreateVideoSendStream(mSendStreamConfig.Copy(),
                                                     mEncoderConfig.Copy());

  if (mTrackSource) {
    mSendStream->SetSource(mTrackSource, DegradationPreference());
  }
}

void WebrtcVideoConduit::DeleteRecvStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  if (!mRecvStream) {
    return;
  }

  mCall->Call()->DestroyVideoReceiveStream(mRecvStream);
  mEngineReceiving = false;
  mRecvStream = nullptr;
}

void WebrtcVideoConduit::CreateRecvStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  if (mRecvStream) {
    return;
  }

  mRecvStreamConfig.renderer = &mRecvSinkProxy;

  for (auto& decoder : mRecvStreamConfig.decoders) {
    glean::webrtc_video::recv_codec_used
        .Get(nsDependentCString(decoder.video_format.name.c_str()))
        .Add(1);
  }

  mRecvStreamConfig.decoder_factory = mDecoderFactory.get();

  // Config::Copy() does not copy move-only fields like
  // on_frame_delivered_callback, so it needs to be (re)installed on the copy
  // that is actually handed to the stream.
  webrtc::VideoReceiveStreamInterface::Config config = mRecvStreamConfig.Copy();
  // The receive stream retains this callback (and the RefPtr captured in
  // it) until DeleteRecvStream() runs it down as part of conduit shutdown,
  // so there is no cycle beyond that lifetime. Called synchronously on
  // whatever thread decodes video, not necessarily mCallThread, so hop over
  // before touching mSourceTracker, which is not thread-safe.
  config.on_frame_delivered_callback =
      [self = RefPtr<WebrtcVideoConduit>(this)](
          const webrtc::RtpPacketInfos& aPacketInfos,
          webrtc::Timestamp aTimestamp) {
        self->mCallThread->Dispatch(NS_NewRunnableFunction(
            "WebrtcVideoConduit::OnFrameDelivered",
            [self, aPacketInfos, aTimestamp] {
              self->mSourceTracker.OnFrameDelivered(aPacketInfos, aTimestamp);
            }));
      };
  mRecvStream = mCall->Call()->CreateVideoReceiveStream(std::move(config));
  // Ensure that we set the jitter buffer target on this stream.
  mRecvStream->SetBaseMinimumPlayoutDelayMs(mJitterBufferTargetMs);

  CSFLogDebug(LOGTAG, "Created VideoReceiveStream %p for SSRC %u (0x%x)",
              mRecvStream, mRecvStreamConfig.rtp.remote_ssrc,
              mRecvStreamConfig.rtp.remote_ssrc);
}

void WebrtcVideoConduit::NotifyUnsetCurrentRemoteSSRC() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  CSFLogDebug(LOGTAG, "%s (%p): Unsetting SSRC %u in other conduits",
              __FUNCTION__, this, mRecvStreamConfig.rtp.remote_ssrc);
  mCall->UnregisterConduit(this);
  mCall->UnsetRemoteSSRC(mRecvStreamConfig.rtp.remote_ssrc);
  mCall->RegisterConduit(this);
}

void WebrtcVideoConduit::SetRemoteSSRCConfig(uint32_t aSsrc,
                                             uint32_t aRtxSsrc) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  CSFLogDebug(LOGTAG, "%s: SSRC %u (0x%x)", __FUNCTION__, aSsrc, aSsrc);

  if (mRecvStreamConfig.rtp.remote_ssrc != aSsrc) {
    nsCOMPtr<nsIDirectTaskDispatcher> dtd = do_QueryInterface(mCallThread);
    MOZ_ALWAYS_SUCCEEDS(dtd->DispatchDirectTask(NewRunnableMethod(
        "WebrtcVideoConduit::NotifyUnsetCurrentRemoteSSRC", this,
        &WebrtcVideoConduit::NotifyUnsetCurrentRemoteSSRC)));
  }

  mRecvSSRC = mRecvStreamConfig.rtp.remote_ssrc = aSsrc;
  // If we have no associated PT then ensure we don't have an rtx_ssrc set.
  mRecvStreamConfig.rtp.rtx_ssrc =
      mRecvStreamConfig.rtp.rtx_associated_payload_types.empty() ? 0 : aRtxSsrc;
}

void WebrtcVideoConduit::SetRemoteSSRCAndRestartAsNeeded(uint32_t aSsrc,
                                                         uint32_t aRtxSsrc) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (mRecvStreamConfig.rtp.remote_ssrc == aSsrc &&
      mRecvStreamConfig.rtp.rtx_ssrc == aRtxSsrc) {
    return;
  }

  SetRemoteSSRCConfig(aSsrc, aRtxSsrc);

  const bool wasReceiving = mEngineReceiving;
  const bool hadRecvStream = mRecvStream;

  StopReceiving();

  if (hadRecvStream) {
    MutexAutoLock lock(mMutex);
    DeleteRecvStream();
    CreateRecvStream();
  }

  if (wasReceiving) {
    StartReceiving();
  }
}

void WebrtcVideoConduit::EnsureRemoteSSRC() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  const auto& ssrcs = mSendStreamConfig.rtp.ssrcs;
  if (mRecvStreamConfig.rtp.remote_ssrc != 0 &&
      std::find(ssrcs.begin(), ssrcs.end(),
                mRecvStreamConfig.rtp.remote_ssrc) == ssrcs.end()) {
    return;
  }

  uint32_t ssrc;
  do {
    ssrc = GenerateRandomSSRC();
  } while (
      NS_WARN_IF(std::find(ssrcs.begin(), ssrcs.end(), ssrc) != ssrcs.end()));
  CSFLogDebug(LOGTAG, "VideoConduit %p: Generated remote SSRC %u", this, ssrc);
  SetRemoteSSRCConfig(ssrc, 0);
}

void WebrtcVideoConduit::EnsureLocalSSRC() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertCurrentThreadOwns();

  auto& ssrcs = mSendStreamConfig.rtp.ssrcs;
  if (ssrcs.empty()) {
    ssrcs.push_back(GenerateRandomSSRC());
  }

  // Reverse-iterating here so that the first dupe in `ssrcs` always wins.
  for (auto& ssrc : Reversed(ssrcs)) {
    if (ssrc != 0 && ssrc != mRecvStreamConfig.rtp.remote_ssrc &&
        std::count(ssrcs.begin(), ssrcs.end(), ssrc) == 1) {
      continue;
    }
    do {
      ssrc = GenerateRandomSSRC();
    } while (NS_WARN_IF(ssrc == mRecvStreamConfig.rtp.remote_ssrc) ||
             NS_WARN_IF(std::count(ssrcs.begin(), ssrcs.end(), ssrc) > 1));
    CSFLogDebug(LOGTAG, "%s (%p): Generated local SSRC %u", __FUNCTION__, this,
                ssrc);
  }
  mRecvStreamConfig.rtp.local_ssrc = ssrcs[0];
}

void WebrtcVideoConduit::UnsetRemoteSSRC(uint32_t aSsrc) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertNotCurrentThreadOwns();

  if (mRecvStreamConfig.rtp.remote_ssrc != aSsrc &&
      mRecvStreamConfig.rtp.rtx_ssrc != aSsrc) {
    return;
  }

  const auto& ssrcs = mSendStreamConfig.rtp.ssrcs;
  uint32_t our_ssrc = 0;
  do {
    our_ssrc = GenerateRandomSSRC();
  } while (NS_WARN_IF(our_ssrc == aSsrc) ||
           NS_WARN_IF(std::find(ssrcs.begin(), ssrcs.end(), our_ssrc) !=
                      ssrcs.end()));

  CSFLogDebug(LOGTAG, "%s (%p): Generated remote SSRC %u", __FUNCTION__, this,
              our_ssrc);

  // There is a (tiny) chance that this new random ssrc will collide with some
  // other conduit's remote ssrc, in which case that conduit will choose a new
  // one.
  SetRemoteSSRCAndRestartAsNeeded(our_ssrc, 0);
}

/*static*/
unsigned WebrtcVideoConduit::ToLibwebrtcMaxFramerate(
    const Maybe<double>& aMaxFramerate) {
  Maybe<unsigned> negotiatedMaxFps;
  if (aMaxFramerate.isSome()) {
    // libwebrtc does not handle non-integer max framerate.
    unsigned integerMaxFps = static_cast<unsigned>(std::round(*aMaxFramerate));
    // libwebrtc crashes with a max framerate of 0, even though the
    // spec says this is valid. For now, we treat this as no limit.
    if (integerMaxFps) {
      negotiatedMaxFps = Some(integerMaxFps);
    }
  }
  // We do not use DEFAULT_VIDEO_MAX_FRAMERATE here; that is used at the very
  // end in VideoStreamFactory, once codec-wide and per-encoding limits are
  // known.
  return negotiatedMaxFps.refOr(std::numeric_limits<unsigned int>::max());
}

Maybe<Ssrc> WebrtcVideoConduit::GetRemoteSSRC() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  // libwebrtc uses 0 to mean a lack of SSRC. That is not to spec.
  return mRecvStreamConfig.rtp.remote_ssrc == 0
             ? Nothing()
             : Some(mRecvStreamConfig.rtp.remote_ssrc);
}

Maybe<webrtc::VideoReceiveStreamInterface::Stats>
WebrtcVideoConduit::GetReceiverStats() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (!mRecvStream) {
    return Nothing();
  }
  return Some(mRecvStream->GetStats());
}

Maybe<webrtc::VideoSendStream::Stats> WebrtcVideoConduit::GetSenderStats()
    const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (!mSendStream) {
    // Prefer transitional stats left over from a recently destroyed stream
    // (e.g. during a codec change). These carry real cumulative counters and
    // should take priority over the synthesised fallback below.
    if (mTransitionalSendStreamStats) {
      return mTransitionalSendStreamStats;
    }
    // The send stream is only created when mTransmitting is true, which
    // requires a track to be bound (see RTCRtpSender::UpdateBaseConfig). For
    // a trackless sender the stream never starts, yet the WebRTC stats spec
    // requires RTCOutboundRtpStreamStats to exist as soon as the sender is
    // configured by a completed offer/answer exchange. Synthesise minimal
    // stats keyed by the SSRCs that were negotiated in SDP so that
    // outbound-rtp entries appear in getStats() even before a track arrives.
    const auto& ssrcs = mSendStreamConfig.rtp.ssrcs;
    if (ssrcs.empty()) {
      return Nothing();
    }
    webrtc::VideoSendStream::Stats synthStats;
    for (uint32_t ssrc : ssrcs) {
      webrtc::VideoSendStream::StreamStats streamStats;
      streamStats.type =
          webrtc::VideoSendStream::StreamStats::StreamType::kMedia;
      synthStats.substreams[ssrc] = streamStats;
    }
    return Some(std::move(synthStats));
  }
  auto stats = mSendStream->GetStats();
  if (stats.substreams.empty()) {
    if (!mTransitionalSendStreamStats) {
      CSFLogError(LOGTAG, "%s: No SSRC in send stream stats", __FUNCTION__);
    }
    return mTransitionalSendStreamStats;
  }
  // Successfully got stats, so clear the transitional stats.
  mTransitionalSendStreamStats = Nothing();
  return Some(mSendStream->GetStats());
}

Maybe<webrtc::Call::Stats> WebrtcVideoConduit::GetCallStats() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (!mCall->Call()) {
    return Nothing();
  }
  return Some(mCall->Call()->GetStats());
}

MediaConduitErrorCode WebrtcVideoConduit::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  CSFLogDebug(LOGTAG, "%s this=%p", __FUNCTION__, this);

#ifdef MOZ_WIDGET_ANDROID
  if (mozilla::camera::VideoEngine::SetAndroidObjects() != 0) {
    CSFLogError(LOGTAG, "%s: could not set Android objects", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }
#endif  // MOZ_WIDGET_ANDROID

  mSendPluginCreated = mEncoderFactory->CreatedGmpPluginEvent().Connect(
      GetMainThreadSerialEventTarget(),
      [self = detail::RawPtr(this)](uint64_t aPluginID) {
        self.get()->mSendCodecPluginIDs.AppendElement(aPluginID);
      });
  mSendPluginReleased = mEncoderFactory->ReleasedGmpPluginEvent().Connect(
      GetMainThreadSerialEventTarget(),
      [self = detail::RawPtr(this)](uint64_t aPluginID) {
        self.get()->mSendCodecPluginIDs.RemoveElement(aPluginID);
      });
  mRecvPluginCreated = mDecoderFactory->CreatedGmpPluginEvent().Connect(
      GetMainThreadSerialEventTarget(),
      [self = detail::RawPtr(this)](uint64_t aPluginID) {
        self.get()->mRecvCodecPluginIDs.AppendElement(aPluginID);
      });
  mRecvPluginReleased = mDecoderFactory->ReleasedGmpPluginEvent().Connect(
      GetMainThreadSerialEventTarget(),
      [self = detail::RawPtr(this)](uint64_t aPluginID) {
        self.get()->mRecvCodecPluginIDs.RemoveElement(aPluginID);
      });
  mRtpSources.Connect(&mCanonicalRtpSources);

  MOZ_ALWAYS_SUCCEEDS(mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<WebrtcVideoConduit>(this)] {
        mCall->RegisterConduit(this);
      })));

  CSFLogDebug(LOGTAG, "%s Initialization Done", __FUNCTION__);
  return kMediaConduitNoError;
}

RefPtr<GenericPromise> WebrtcVideoConduit::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  mSendPluginCreated.DisconnectIfExists();
  mSendPluginReleased.DisconnectIfExists();
  mRecvPluginCreated.DisconnectIfExists();
  mRecvPluginReleased.DisconnectIfExists();
  mReceiverRtpEventListener.DisconnectIfExists();
  mReceiverRtcpEventListener.DisconnectIfExists();
  mSenderRtcpEventListener.DisconnectIfExists();
  mRtpSources.DisconnectIfConnected();

  return InvokeAsync(
      mCallThread, __func__, [this, self = RefPtr<WebrtcVideoConduit>(this)] {
        if (mSendBitrate.NumDataValues() > 0) {
          glean::webrtc::video_encoder_bitrate_avg_per_call_kbps
              .AccumulateSingleSample(
                  static_cast<unsigned>(mSendBitrate.Mean() / 1000));
          glean::webrtc::video_encoder_bitrate_std_dev_per_call_kbps
              .AccumulateSingleSample(static_cast<unsigned>(
                  mSendBitrate.StandardDeviation() / 1000));
          mSendBitrate.Clear();
        }
        if (mSendFramerate.NumDataValues() > 0) {
          glean::webrtc::video_encoder_framerate_avg_per_call
              .AccumulateSingleSample(
                  static_cast<unsigned>(mSendFramerate.Mean()));
          glean::webrtc::video_encoder_framerate_10x_std_dev_per_call
              .AccumulateSingleSample(static_cast<unsigned>(
                  mSendFramerate.StandardDeviation() * 10));
          mSendFramerate.Clear();
        }

        if (mRecvBitrate.NumDataValues() > 0) {
          glean::webrtc::video_decoder_bitrate_avg_per_call_kbps
              .AccumulateSingleSample(
                  static_cast<unsigned>(mRecvBitrate.Mean() / 1000));
          glean::webrtc::video_decoder_bitrate_std_dev_per_call_kbps
              .AccumulateSingleSample(static_cast<unsigned>(
                  mRecvBitrate.StandardDeviation() / 1000));
          mRecvBitrate.Clear();
        }
        if (mRecvFramerate.NumDataValues() > 0) {
          glean::webrtc::video_decoder_framerate_avg_per_call
              .AccumulateSingleSample(
                  static_cast<unsigned>(mRecvFramerate.Mean()));
          glean::webrtc::video_decoder_framerate_10x_std_dev_per_call
              .AccumulateSingleSample(static_cast<unsigned>(
                  mRecvFramerate.StandardDeviation() * 10));
          mRecvFramerate.Clear();
        }

        mControl.mReceiving.DisconnectIfConnected();
        mControl.mTransmitting.DisconnectIfConnected();
        mControl.mLocalSsrcs.DisconnectIfConnected();
        mControl.mLocalRtxSsrcs.DisconnectIfConnected();
        mControl.mLocalCname.DisconnectIfConnected();
        mControl.mMid.DisconnectIfConnected();
        mControl.mRemoteSsrc.DisconnectIfConnected();
        mControl.mRemoteRtxSsrc.DisconnectIfConnected();
        mControl.mSyncGroup.DisconnectIfConnected();
        mControl.mLocalRecvRtpExtensions.DisconnectIfConnected();
        mControl.mLocalSendRtpExtensions.DisconnectIfConnected();
        mControl.mSendCodec.DisconnectIfConnected();
        mControl.mSendRtpRtcpConfig.DisconnectIfConnected();
        mControl.mRecvCodecs.DisconnectIfConnected();
        mControl.mRecvRtpRtcpConfig.DisconnectIfConnected();
        mControl.mCodecMode.DisconnectIfConnected();
        mControl.mFrameTransformerProxySend.DisconnectIfConnected();
        mControl.mFrameTransformerProxyRecv.DisconnectIfConnected();
        mControl.mVideoDegradationPreference.DisconnectIfConnected();
        mWatchManager.Shutdown();

        if (mTrackSource) {
          mTrackSource->RemoveSink(&mSendSinkProxy);
        }

        mCall->UnregisterConduit(this);
        {
          MutexAutoLock lock(mMutex);
          DeleteSendStream();
          DeleteRecvStream();
        }
        mDecoderFactory->DisconnectAll();
        mEncoderFactory->DisconnectAll();
        // Clear the stats send stream stats cache
        mTransitionalSendStreamStats = Nothing();

        SetIsShutdown();

        return GenericPromise::CreateAndResolve(true, __func__);
      });
}

bool WebrtcVideoConduit::IsShutdown() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mIsShutdown;
}

void WebrtcVideoConduit::SetIsShutdown() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mIsShutdown = true;
}

webrtc::VideoCodecMode WebrtcVideoConduit::CodecMode() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mControl.mCodecMode;
}

webrtc::DegradationPreference WebrtcVideoConduit::DegradationPreference()
    const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (mControl.mConfiguredDegradationPreference !=
      webrtc::DegradationPreference::DISABLED) {
    return mControl.mConfiguredDegradationPreference;
  }

  if (mLockScaling || CodecMode() == webrtc::VideoCodecMode::kScreensharing) {
    return webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
  }
  // Fall back to MAINTAIN_FRAMERATE by default. This is what libwebrtc/Chrome
  // uses, because BALANCED hasn't been tuned yet. See
  // https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/media/engine/webrtc_video_engine.cc;l=1939;drc=7c2b25f6a19cfeeea67f0f43ed33617840bab33d
  return webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
}

MediaConduitErrorCode WebrtcVideoConduit::AttachRenderer(
    RefPtr<mozilla::VideoRenderer> aVideoRenderer) {
  MOZ_ASSERT(NS_IsMainThread());

  CSFLogDebug(LOGTAG, "%s", __FUNCTION__);

  // null renderer
  if (!aVideoRenderer) {
    CSFLogError(LOGTAG, "%s NULL Renderer", __FUNCTION__);
    MOZ_ASSERT(false);
    return kMediaConduitInvalidRenderer;
  }

  // This function is called only from main, so we only need to protect against
  // modifying mRenderer while any webrtc.org code is trying to use it.
  {
    ReentrantMonitorAutoEnter enter(mRendererMonitor);
    mRenderer = aVideoRenderer;
  }

  return kMediaConduitNoError;
}

void WebrtcVideoConduit::DetachRenderer() {
  MOZ_ASSERT(NS_IsMainThread());

  ReentrantMonitorAutoEnter enter(mRendererMonitor);
  if (mRenderer) {
    mRenderer = nullptr;
  }
}

webrtc::RefCountedObject<mozilla::VideoStreamFactory>*
WebrtcVideoConduit::CreateVideoStreamFactory() {
  auto videoStreamFactory = mVideoStreamFactory.Lock();
  *videoStreamFactory = new webrtc::RefCountedObject<VideoStreamFactory>(
      *mCurSendCodecConfig, mMinBitrate, mStartBitrate, mPrefMaxBitrate,
      mNegotiatedMaxBitrate);
  return videoStreamFactory->get();
}

void WebrtcVideoConduit::OnSendFrame(const webrtc::VideoFrame& aFrame) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  // XXX Google uses a "timestamp_aligner" to translate timestamps from the
  // camera via TranslateTimestamp(); we should look at doing the same.  This
  // avoids sampling error when capturing frames, but google had to deal with
  // some broken cameras, include Logitech c920's IIRC.

  const gfx::IntSize size{aFrame.width(), aFrame.height()};

  {
    const auto ssrc = mSendStreamConfig.rtp.ssrcs.empty()
                          ? 0u
                          : mSendStreamConfig.rtp.ssrcs.front();
    CSFLogVerbose(LOGTAG, "WebrtcVideoConduit %p %s (send SSRC %u (0x%x))",
                  this, __FUNCTION__, ssrc, ssrc);
  }

  if (Some(size) != mLastSize) {
    MOZ_ASSERT(size != gfx::IntSize(0, 0));
    // Note coverity will flag this since it thinks they can be 0
    MOZ_ASSERT(mCurSendCodecConfig);
    mLastSize = Some(size);
  }

  MOZ_ASSERT(!aFrame.color_space(), "Unexpected use of color space");
  MOZ_ASSERT(!aFrame.has_update_rect(), "Unexpected use of update rect");

#ifdef MOZ_REAL_TIME_TRACING
  if (profiler_is_active()) {
    MutexAutoLock lock(mMutex);
    nsAutoCStringN<256> ssrcsCommaSeparated;
    bool first = true;
    for (auto ssrc : mSendStreamConfig.rtp.ssrcs) {
      if (!first) {
        ssrcsCommaSeparated.AppendASCII(", ");
      } else {
        first = false;
      }
      ssrcsCommaSeparated.AppendInt(ssrc);
    }

    const webrtc::Timestamp currentTimestamp =
        webrtc::Timestamp::Micros(aFrame.timestamp_us());
    // The first frame has a delta of zero.
    webrtc::TimeDelta timestampDelta =
        mLastTimestampSend.isSome()
            ? currentTimestamp - mLastTimestampSend.value()
            : webrtc::TimeDelta::Zero();
    mLastTimestampSend = Some(currentTimestamp);
    TRACE_COMMENT("VideoConduit::OnSendFrame (async)",
                  "t-delta=%.1fms, ssrcs=%s", timestampDelta.ms<double>(),
                  ssrcsCommaSeparated.get());
  }
#endif
}

void WebrtcVideoConduit::SetTrackSource(
    webrtc::VideoTrackSourceInterface* aSource) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ALWAYS_SUCCEEDS(mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr(this), src = RefPtr(aSource)] {
        if (mTrackSource) {
          mTrackSource->RemoveSink(&mSendSinkProxy);
        }
        if (src) {
          src->AddOrUpdateSink(&mSendSinkProxy, {});
        }
        mTrackSource = src;
        if (mSendStream) {
          mSendStream->SetSource(mTrackSource, DegradationPreference());
        }
      })));
}

// Transport Layer Callbacks

void WebrtcVideoConduit::DeliverPacket(webrtc::CopyOnWriteBuffer packet,
                                       PacketType type) {
  // Currently unused.
  MOZ_ASSERT(false);
}

void WebrtcVideoConduit::OnRtpReceived(webrtc::RtpPacketReceived&& aPacket,
                                       webrtc::RTPHeader&& aHeader) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  // We should only be handling packets on this conduit if we are set to receive
  // them.
  if (!mControl.mReceiving) {
    CSFLogVerbose(LOGTAG,
                  "VideoConduit %p: Discarding packet SEQ# %u SSRC %u as not "
                  "configured to receive.",
                  this, aPacket.SequenceNumber(), aHeader.ssrc);
    return;
  }

  mRemoteSendSSRC = aHeader.ssrc;

  if (mAllowSsrcChange || mRecvStreamConfig.rtp.remote_ssrc == 0) {
    bool switchRequired = mRecvStreamConfig.rtp.remote_ssrc != aHeader.ssrc;
    if (switchRequired) {
      // Handle the unknown ssrc (and ssrc-not-signaled case).

      // We need to check that the newly received ssrc is not already
      // associated with ulpfec or rtx. This is how webrtc.org handles
      // things, see https://codereview.webrtc.org/1226093002.
      const webrtc::VideoReceiveStreamInterface::Config::Rtp& rtp =
          mRecvStreamConfig.rtp;
      switchRequired =
          rtp.rtx_associated_payload_types.find(aHeader.payloadType) ==
              rtp.rtx_associated_payload_types.end() &&
          rtp.ulpfec_payload_type != aHeader.payloadType;
    }

    if (switchRequired) {
      CSFLogInfo(LOGTAG, "VideoConduit %p: Switching remote SSRC from %u to %u",
                 this, mRecvStreamConfig.rtp.remote_ssrc, aHeader.ssrc);
      SetRemoteSSRCAndRestartAsNeeded(aHeader.ssrc, 0);
    }
  }

  CSFLogVerbose(LOGTAG, "%s: seq# %u, Len %zu, SSRC %u (0x%x) ", __FUNCTION__,
                aPacket.SequenceNumber(), aPacket.size(), aPacket.Ssrc(),
                aPacket.Ssrc());

  // Libwebrtc commit cde4b67d9d now expect calls to
  // SourceTracker::GetSources() to happen on the call thread.  We'll
  // grab the value now while on the call thread, and dispatch to main
  // to store the cached value if we have new source information.
  // See Bug 1845621.
  mCanonicalRtpSources = mSourceTracker.GetSources();

  mRtpPacketEvent.Notify();
  if (mCall->Call()) {
    mCall->Call()->Receiver()->DeliverRtpPacket(
        webrtc::MediaType::VIDEO, std::move(aPacket),
        [self = RefPtr<WebrtcVideoConduit>(this)](
            const webrtc::RtpPacketReceived& packet) {
          CSFLogVerbose(
              LOGTAG,
              "VideoConduit %p: failed demuxing packet, ssrc: %u seq: %u",
              self.get(), packet.Ssrc(), packet.SequenceNumber());
          return false;
        });
  }
}

void WebrtcVideoConduit::OnRtcpReceived(webrtc::CopyOnWriteBuffer&& aPacket) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (mCall->Call()) {
    mCall->Call()->Receiver()->DeliverRtcpPacket(std::move(aPacket));
  }
}

Maybe<uint16_t> WebrtcVideoConduit::RtpSendBaseSeqFor(uint32_t aSsrc) const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  auto it = mRtpSendBaseSeqs.find(aSsrc);
  if (it == mRtpSendBaseSeqs.end()) {
    return Nothing();
  }
  return Some(it->second);
}

const dom::RTCStatsTimestampMaker& WebrtcVideoConduit::GetTimestampMaker()
    const {
  return mCall->GetTimestampMaker();
}

void WebrtcVideoConduit::StopTransmitting() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertNotCurrentThreadOwns();

  if (!mEngineTransmitting) {
    return;
  }

  if (mSendStream) {
    CSFLogDebug(LOGTAG, "%s Stopping send stream", __FUNCTION__);
    mSendStream->Stop();
  }

  mEngineTransmitting = false;
}

void WebrtcVideoConduit::StartTransmitting() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mSendStream);
  mMutex.AssertNotCurrentThreadOwns();

  if (mEngineTransmitting) {
    return;
  }

  CSFLogDebug(LOGTAG, "%s Starting send stream", __FUNCTION__);

  mSendStream->Start();
  // XXX File a bug to consider hooking this up to the state of mtransport
  mCall->Call()->SignalChannelNetworkState(webrtc::MediaType::VIDEO,
                                           webrtc::kNetworkUp);
  mEngineTransmitting = true;
}

void WebrtcVideoConduit::StopReceiving() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mMutex.AssertNotCurrentThreadOwns();

  // Are we receiving already? If so, stop receiving and playout
  // since we can't apply new recv codec when the engine is playing.
  if (!mEngineReceiving) {
    return;
  }

  if (mRecvStream) {
    CSFLogDebug(LOGTAG, "%s Stopping receive stream", __FUNCTION__);
    mRecvStream->Stop();
  }

  mEngineReceiving = false;
}

void WebrtcVideoConduit::StartReceiving() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mRecvStream);
  mMutex.AssertNotCurrentThreadOwns();

  if (mEngineReceiving) {
    return;
  }

  CSFLogDebug(LOGTAG, "%s Starting receive stream (SSRC %u (0x%x))",
              __FUNCTION__, mRecvStreamConfig.rtp.remote_ssrc,
              mRecvStreamConfig.rtp.remote_ssrc);
  // Start Receiving on the video engine
  mRecvStream->Start();

  // XXX File a bug to consider hooking this up to the state of mtransport
  mCall->Call()->SignalChannelNetworkState(webrtc::MediaType::VIDEO,
                                           webrtc::kNetworkUp);
  mEngineReceiving = true;
}

bool WebrtcVideoConduit::SendRtp(const uint8_t* aData, size_t aLength,
                                 const webrtc::PacketOptions& aOptions) {
  MOZ_ASSERT(aLength >= 12);
  const uint16_t seqno = ntohs(*((uint16_t*)&aData[2]));
  const uint32_t ssrc = ntohl(*((uint32_t*)&aData[8]));

  CSFLogVerbose(
      LOGTAG,
      "VideoConduit %p: Sending RTP Packet seq# %u, len %zu, SSRC %u (0x%x)",
      this, seqno, aLength, ssrc, ssrc);

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "VideoConduit %p: RTP Packet Send Failed", this);
    return false;
  }

  MediaPacket packet;
  packet.Copy(aData, aLength, aLength + SRTP_MAX_EXPANSION);
  packet.SetType(MediaPacket::RTP);
  mSenderRtpSendEvent.Notify(std::move(packet));

  // Parse the sequence number of the first rtp packet as base_seq.
  const auto inserted = mRtpSendBaseSeqs_n.insert({ssrc, seqno}).second;

  if (inserted || aOptions.packet_id >= 0) {
    int64_t now_ms = PR_Now() / 1000;
    MOZ_ALWAYS_SUCCEEDS(mCallThread->Dispatch(NS_NewRunnableFunction(
        __func__, [this, self = RefPtr<WebrtcVideoConduit>(this),
                   packet_id = aOptions.packet_id, now_ms, ssrc, seqno] {
          mRtpSendBaseSeqs.insert({ssrc, seqno});
          if (packet_id >= 0) {
            if (mCall->Call()) {
              // TODO: This notification should ideally happen after the
              // transport layer has sent the packet on the wire.
              mCall->Call()->OnSentPacket({packet_id, now_ms});
            }
          }
        })));
  }
  return true;
}

bool WebrtcVideoConduit::SendSenderRtcp(const uint8_t* aData, size_t aLength) {
  CSFLogVerbose(
      LOGTAG,
      "VideoConduit %p: Sending RTCP SR Packet, len %zu, SSRC %u (0x%x)", this,
      aLength, (uint32_t)ntohl(*((uint32_t*)&aData[4])),
      (uint32_t)ntohl(*((uint32_t*)&aData[4])));

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "VideoConduit %p: RTCP SR Packet Send Failed", this);
    return false;
  }

  MediaPacket packet;
  packet.Copy(aData, aLength, aLength + SRTP_MAX_EXPANSION);
  packet.SetType(MediaPacket::RTCP);
  mSenderRtcpSendEvent.Notify(std::move(packet));
  return true;
}

bool WebrtcVideoConduit::SendReceiverRtcp(const uint8_t* aData,
                                          size_t aLength) {
  CSFLogVerbose(
      LOGTAG,
      "VideoConduit %p: Sending RTCP RR Packet, len %zu, SSRC %u (0x%x)", this,
      aLength, (uint32_t)ntohl(*((uint32_t*)&aData[4])),
      (uint32_t)ntohl(*((uint32_t*)&aData[4])));

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "VideoConduit %p: RTCP RR Packet Send Failed", this);
    return false;
  }

  MediaPacket packet;
  packet.Copy(aData, aLength, aLength + SRTP_MAX_EXPANSION);
  packet.SetType(MediaPacket::RTCP);
  mReceiverRtcpSendEvent.Notify(std::move(packet));
  return true;
}

void WebrtcVideoConduit::OnRecvFrame(const webrtc::VideoFrame& aFrame) {
  MOZ_ASSERT(mFrameRecvThread->IsOnCurrentThread());
  const uint32_t localRecvSsrc = mRecvSSRC;
  const uint32_t remoteSendSsrc = mRemoteSendSSRC;

  CSFLogVerbose(
      LOGTAG,
      "VideoConduit %p: Rendering frame, Remote SSRC %u (0x%x), size %ux%u",
      this, static_cast<uint32_t>(remoteSendSsrc),
      static_cast<uint32_t>(remoteSendSsrc), aFrame.width(), aFrame.height());
  ReentrantMonitorAutoEnter enter(mRendererMonitor);

  if (!mRenderer) {
    CSFLogError(LOGTAG, "VideoConduit %p: Cannot render frame, no renderer",
                this);
    return;
  }

  bool needsNewHistoryElement = mReceivedFrameHistory.mEntries.IsEmpty();
  {
    const Maybe frameSize = Some(gfx::IntSize{aFrame.width(), aFrame.height()});
    if (frameSize != mReceivingSize.Ref()) {
      mReceivingSize = frameSize;
      needsNewHistoryElement = true;
    }
  }

  if (!needsNewHistoryElement) {
    auto& currentEntry = mReceivedFrameHistory.mEntries.LastElement();
    needsNewHistoryElement =
        currentEntry.mRotationAngle !=
            static_cast<unsigned long>(aFrame.rotation()) ||
        currentEntry.mLocalSsrc != localRecvSsrc ||
        currentEntry.mRemoteSsrc != remoteSendSsrc;
  }

  // Record frame history
  const auto historyNow = mCall->GetTimestampMaker().GetNow().ToDom();
  if (needsNewHistoryElement) {
    dom::RTCVideoFrameHistoryEntryInternal frameHistoryElement;
    frameHistoryElement.mConsecutiveFrames = 0;
    frameHistoryElement.mWidth = aFrame.width();
    frameHistoryElement.mHeight = aFrame.height();
    frameHistoryElement.mRotationAngle =
        static_cast<unsigned long>(aFrame.rotation());
    frameHistoryElement.mFirstFrameTimestamp = historyNow;
    frameHistoryElement.mLocalSsrc = localRecvSsrc;
    frameHistoryElement.mRemoteSsrc = remoteSendSsrc;
    if (!mReceivedFrameHistory.mEntries.AppendElement(frameHistoryElement,
                                                      fallible)) {
      mozalloc_handle_oom(0);
    }
  }
  auto& currentEntry = mReceivedFrameHistory.mEntries.LastElement();

  currentEntry.mConsecutiveFrames++;
  currentEntry.mLastFrameTimestamp = historyNow;
  // Attempt to retrieve an timestamp encoded in the image pixels if enabled.
  if (mVideoLatencyTestEnable && mReceivingSize.Ref()) {
    uint64_t now = PR_Now();
    uint64_t timestamp = 0;
    uint8_t* data =
        const_cast<uint8_t*>(aFrame.video_frame_buffer()->GetI420()->DataY());
    bool ok = YuvStamper::Decode(
        mReceivingSize.Ref()->width, mReceivingSize.Ref()->height,
        mReceivingSize.Ref()->width, data,
        reinterpret_cast<unsigned char*>(&timestamp), sizeof(timestamp), 0, 0);
    if (ok) {
      VideoLatencyUpdate(now - timestamp);
    }
  }
#ifdef MOZ_REAL_TIME_TRACING
  if (profiler_is_active()) {
    MutexAutoLock lock(mMutex);
    // The first frame has a delta of zero.
    uint32_t rtpTimestamp = aFrame.rtp_timestamp();
    uint32_t timestampDelta =
        mLastRTPTimestampReceive.isSome()
            ? rtpTimestamp - mLastRTPTimestampReceive.value()
            : 0;
    mLastRTPTimestampReceive = Some(rtpTimestamp);
    TRACE_COMMENT("VideoConduit::OnRecvFrame", "t-delta=%.1fms, ssrc=%u",
                  timestampDelta * 1000.f / webrtc::kVideoPayloadTypeFrequency,
                  localRecvSsrc);
  }
#endif

  mRenderer->RenderVideoFrame(aFrame);
}

bool WebrtcVideoConduit::AddFrameHistory(
    dom::Sequence<dom::RTCVideoFrameHistoryInternal>* outHistories) const {
  ReentrantMonitorAutoEnter enter(mRendererMonitor);
  if (!outHistories->AppendElement(mReceivedFrameHistory, fallible)) {
    mozalloc_handle_oom(0);
    return false;
  }
  return true;
}

void WebrtcVideoConduit::SetJitterBufferTarget(DOMHighResTimeStamp aTargetMs) {
  MOZ_RELEASE_ASSERT(aTargetMs <= std::numeric_limits<uint16_t>::max());
  MOZ_RELEASE_ASSERT(aTargetMs >= 0);

  MOZ_ALWAYS_SUCCEEDS(mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__,
      [this, self = RefPtr<WebrtcVideoConduit>(this), targetMs = aTargetMs] {
        mJitterBufferTargetMs = static_cast<uint16_t>(targetMs);
        if (mRecvStream) {
          mRecvStream->SetBaseMinimumPlayoutDelayMs(targetMs);
        }
      })));
}

void WebrtcVideoConduit::DumpCodecDB() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  for (const auto& entry : mControl.mConfiguredRecvCodecs) {
    CSFLogDebug(LOGTAG, "Payload Name: %s", entry.mName.c_str());
    CSFLogDebug(LOGTAG, "Payload Type: %d", entry.mType);
    CSFLogDebug(LOGTAG, "Payload Max Frame Size: %d",
                entry.mEncodingConstraints.maxFs);
    if (entry.mEncodingConstraints.maxFps.isSome()) {
      CSFLogDebug(LOGTAG, "Payload Max Frame Rate: %f",
                  *entry.mEncodingConstraints.maxFps);
    }
  }
}

void WebrtcVideoConduit::VideoLatencyUpdate(uint64_t aNewSample) {
  mRendererMonitor.AssertCurrentThreadIn();

  mVideoLatencyAvg =
      (sRoundingPadding * aNewSample + sAlphaNum * mVideoLatencyAvg) /
      sAlphaDen;
}

uint64_t WebrtcVideoConduit::MozVideoLatencyAvg() {
  mRendererMonitor.AssertCurrentThreadIn();

  return mVideoLatencyAvg / sRoundingPadding;
}

void WebrtcVideoConduit::CollectTelemetryData() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (mEngineTransmitting) {
    webrtc::VideoSendStream::Stats stats = mSendStream->GetStats();
    mSendBitrate.Push(stats.media_bitrate_bps);
    mSendFramerate.Push(stats.encode_frame_rate);
  }
  if (mEngineReceiving) {
    webrtc::VideoReceiveStreamInterface::Stats stats = mRecvStream->GetStats();
    mRecvBitrate.Push(stats.total_bitrate_bps);
    mRecvFramerate.Push(stats.decode_frame_rate);
  }
}

void WebrtcVideoConduit::OnRtcpBye() { mRtcpByeEvent.Notify(); }

void WebrtcVideoConduit::OnRtcpTimeout() { mRtcpTimeoutEvent.Notify(); }

void WebrtcVideoConduit::SetTransportActive(bool aActive) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  if (mTransportActive == aActive) {
    return;
  }

  // If false, This stops us from sending
  mTransportActive = aActive;
}

const std::vector<webrtc::RtpSource>&
WebrtcVideoConduit::GetUpstreamRtpSources() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mRtpSources;
}

void WebrtcVideoConduit::RequestKeyFrame(FrameTransformerProxy* aProxy) {
  mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<WebrtcVideoConduit>(this),
                 proxy = RefPtr<FrameTransformerProxy>(aProxy)] {
        if (mRecvStream && mEngineReceiving) {
          // This is a misnomer. This requests a keyframe from the other side.
          mRecvStream->GenerateKeyFrame();
        }
        proxy->KeyFrameRequestDone();
      }));
}

void WebrtcVideoConduit::GenerateKeyFrame(const Maybe<std::string>& aRid,
                                          FrameTransformerProxy* aProxy) {
  // libwebrtc does not implement error handling in the way that
  // webrtc-encoded-transform specifies. So, we'll need to do that here.
  // Also, spec wants us to synchronously check whether there's an encoder, but
  // that's not something that can be checked synchronously.

  mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr(this), proxy = RefPtr(aProxy), aRid] {
        // If encoder is undefined, reject promise with InvalidStateError,
        // abort these steps.

        // If encoder is not processing video frames, reject promise with
        // InvalidStateError, abort these steps.
        if (!mSendStream || !mCurSendCodecConfig) {
          CopyableErrorResult result;
          result.ThrowInvalidStateError("No encoders");
          proxy->GenerateKeyFrameError(aRid, result);
          return;
        }

        // Gather a list of video encoders, named videoEncoders from encoder,
        // ordered according negotiated RIDs if any.
        // NOTE: This is represented by mCurSendCodecConfig->mEncodings

        // If rid is defined, remove from videoEncoders any video encoder that
        // does not match rid.

        // If rid is undefined, remove from videoEncoders all video encoders
        // except the first one.
        bool found = false;
        std::vector<std::string> rids;
        if (!aRid.isSome()) {
          // If rid is undefined, set rid to the RID value corresponding to
          // videoEncoder.
          if (!mCurSendCodecConfig->mEncodings.empty()) {
            if (!mCurSendCodecConfig->mEncodings[0].rid.empty()) {
              rids.push_back(mCurSendCodecConfig->mEncodings[0].rid);
            }
            found = true;
          }
        } else {
          for (const auto& encoding : mCurSendCodecConfig->mEncodings) {
            if (encoding.rid == *aRid) {
              found = true;
              rids.push_back(encoding.rid);
              break;
            }
          }
        }

        // If videoEncoders is empty, reject promise with NotFoundError and
        // abort these steps. videoEncoders is expected to be empty if the
        // corresponding RTCRtpSender is not active, or the corresponding
        // RTCRtpSender track is ended.
        if (!found) {
          CopyableErrorResult result;
          result.ThrowNotFoundError("Rid not in use");
          proxy->GenerateKeyFrameError(aRid, result);
        }

        // NOTE: We don't do this stuff, because libwebrtc's interface is
        // rid-based.
        // Let videoEncoder be the first encoder in videoEncoders.
        // If rid is undefined, set rid to the RID value corresponding to
        // videoEncoder.

        mSendStream->GenerateKeyFrame(rids);
      }));
}

void WebrtcVideoConduit::GenerateKeyFrames(std::vector<std::string> aRids) {
  mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr(this), rids = std::move(aRids)] {
        if (mSendStream) {
          mSendStream->GenerateKeyFrame(rids);
        }
      }));
}

bool WebrtcVideoConduit::HasCodecPluginID(uint64_t aPluginID) const {
  MOZ_ASSERT(NS_IsMainThread());

  return mSendCodecPluginIDs.Contains(aPluginID) ||
         mRecvCodecPluginIDs.Contains(aPluginID);
}

bool WebrtcVideoConduit::HasH264Hardware() {
  return gfx::gfxVars::IsInitialized() && gfx::gfxVars::HasWebrtcH264Hw();
}

bool WebrtcVideoConduit::HasAv1() { return true; }

Maybe<int> WebrtcVideoConduit::ActiveSendPayloadType() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (!mSendStream) {
    return Nothing();
  }

  if (mSendStreamConfig.rtp.payload_type == -1) {
    return Nothing();
  }

  return Some(mSendStreamConfig.rtp.payload_type);
}

Maybe<int> WebrtcVideoConduit::ActiveRecvPayloadType() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  auto stats = GetReceiverStats();
  if (!stats) {
    return Nothing();
  }

  if (stats->current_payload_type == -1) {
    return Nothing();
  }

  return Some(stats->current_payload_type);
}

}  // namespace mozilla
