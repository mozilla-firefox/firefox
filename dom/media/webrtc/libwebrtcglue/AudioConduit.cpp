/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioConduit.h"

#include <vector>

#include "CodecConfig.h"
#include "MediaConduitControl.h"
#include "WebrtcCallWrapper.h"
#include "common/browser_logging/CSFLog.h"
#include "libwebrtcglue/FrameTransformer.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RWLock.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StateMirroring.h"
#include "transport/SrtpFlow.h"  // For SRTP_MAX_EXPANSION

// libwebrtc includes
#include <stdint.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "MainThreadUtils.h"
#include "MediaConduitErrors.h"
#include "MediaConduitInterface.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/call/transport.h"
#include "api/media_types.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/transport/rtp/rtp_source.h"
#include "audio/audio_receive_stream.h"
#include "call/audio_receive_stream.h"
#include "call/audio_send_stream.h"
#include "domstubs.h"
#include "jsapi/RTCStatsReport.h"
#include "media/base/media_constants.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/StateWatching.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsISerialEventTarget.h"
#include "nsThreadUtils.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/ref_counted_object.h"
#include "transport/mediapacket.h"

// for ntohs
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#elif defined XP_WIN
#  include <winsock2.h>
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidBridge.h"
#endif

namespace mozilla {

namespace {

static const char* acLogTag = "WebrtcAudioSessionConduit";
#ifdef LOGTAG
#  undef LOGTAG
#endif
#define LOGTAG acLogTag

using namespace webrtc;
using LocalDirection = MediaSessionConduitLocalDirection;

const char kCodecParamCbr[] = "cbr";

}  // namespace

/**
 * Factory Method for AudioConduit
 */
RefPtr<AudioSessionConduit> AudioSessionConduit::Create(
    RefPtr<WebrtcCallWrapper> aCall,
    nsCOMPtr<nsISerialEventTarget> aStsThread) {
  CSFLogDebug(LOGTAG, "%s ", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  auto conduit =
      MakeRefPtr<WebrtcAudioConduit>(std::move(aCall), std::move(aStsThread));
  if (conduit->Init() != kMediaConduitNoError) {
    CSFLogError(LOGTAG, "%s AudioConduit Init Failed ", __FUNCTION__);
    return nullptr;
  }

  CSFLogDebug(LOGTAG, "%s Successfully created AudioConduit %p", __FUNCTION__,
              conduit.get());
  return conduit;
}

#define INIT_MIRROR(name, val) \
  name(aCallThread, val, "WebrtcAudioConduit::Control::" #name " (Mirror)")
WebrtcAudioConduit::Control::Control(const RefPtr<AbstractThread>& aCallThread)
    : INIT_MIRROR(mReceiving, false),
      INIT_MIRROR(mTransmitting, false),
      INIT_MIRROR(mLocalSsrcs, Ssrcs()),
      INIT_MIRROR(mLocalCname, std::string()),
      INIT_MIRROR(mMid, std::string()),
      INIT_MIRROR(mRemoteSsrc, 0),
      INIT_MIRROR(mSyncGroup, std::string()),
      INIT_MIRROR(mLocalRecvRtpExtensions, RtpExtList()),
      INIT_MIRROR(mLocalSendRtpExtensions, RtpExtList()),
      INIT_MIRROR(mSendCodec, Nothing()),
      INIT_MIRROR(mRecvCodecs, std::vector<AudioCodecConfig>()),
      INIT_MIRROR(mFrameTransformerProxySend, nullptr),
      INIT_MIRROR(mFrameTransformerProxyRecv, nullptr) {}
#undef INIT_MIRROR

RefPtr<GenericPromise> WebrtcAudioConduit::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  mControl.mOnDtmfEventListener.DisconnectIfExists();
  mReceiverRtpEventListener.DisconnectIfExists();
  mReceiverRtcpEventListener.DisconnectIfExists();
  mSenderRtcpEventListener.DisconnectIfExists();
  mRtpSources.DisconnectIfConnected();

  return InvokeAsync(
      mCallThread, "WebrtcAudioConduit::Shutdown (main thread)",
      [this, self = RefPtr<WebrtcAudioConduit>(this)] {
        mControl.mReceiving.DisconnectIfConnected();
        mControl.mTransmitting.DisconnectIfConnected();
        mControl.mLocalSsrcs.DisconnectIfConnected();
        mControl.mLocalCname.DisconnectIfConnected();
        mControl.mMid.DisconnectIfConnected();
        mControl.mRemoteSsrc.DisconnectIfConnected();
        mControl.mSyncGroup.DisconnectIfConnected();
        mControl.mLocalRecvRtpExtensions.DisconnectIfConnected();
        mControl.mLocalSendRtpExtensions.DisconnectIfConnected();
        mControl.mSendCodec.DisconnectIfConnected();
        mControl.mRecvCodecs.DisconnectIfConnected();
        mControl.mFrameTransformerProxySend.DisconnectIfConnected();
        mControl.mFrameTransformerProxyRecv.DisconnectIfConnected();
        mWatchManager.Shutdown();

        {
          AutoWriteLock lock(mLock);
          DeleteSendStream();
          DeleteRecvStream();
        }
        // Clear the stats send stream stats cache
        mTransitionalSendStreamStats = Nothing();

        SetIsShutdown();

        return GenericPromise::CreateAndResolve(
            true, "WebrtcAudioConduit::Shutdown (call thread)");
      });
}

bool WebrtcAudioConduit::IsShutdown() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return mIsShutdown;
}

void WebrtcAudioConduit::SetIsShutdown() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  mIsShutdown = true;
}

#define INIT_CANONICAL(name, val) \
  name(mCallThread, val, "WebrtcAudioConduit::" #name " (Canonical)")
#define INIT_MIRROR(name, val)            \
  name(AbstractThread::MainThread(), val, \
       "WebrtcAudioConduit::" #name " (Mirror)")

WebrtcAudioConduit::WebrtcAudioConduit(
    RefPtr<WebrtcCallWrapper> aCall, nsCOMPtr<nsISerialEventTarget> aStsThread)
    : mCall(std::move(aCall)),
      mSendTransport(this),
      mRecvTransport(this),
      mRecvStream(nullptr),
      mSourceTracker(webrtc::Clock::GetRealTimeClockOnlyUseForRelativeTime()),
      mSendStreamConfig(&mSendTransport),
      mSendStream(nullptr),
      mSendStreamRunning(false),
      mRecvStreamRunning(false),
      mDtmfEnabled(false),
      mLock("WebrtcAudioConduit::mLock"),
      mCallThread(mCall->mCallThread),
      mStsThread(std::move(aStsThread)),
      mControl(mCall->mCallThread),
      mWatchManager(this, mCall->mCallThread),
      INIT_CANONICAL(mCanonicalRtpSources, {}),
      INIT_MIRROR(mRtpSources, {}) {
  mRecvStreamConfig.rtcp_send_transport = &mRecvTransport;
  mRecvStreamConfig.rtp.rtcp_event_observer = this;
}
#undef INIT_MIRROR
#undef INIT_CANONICAL

/**
 * Destruction defines for our super-classes
 */
WebrtcAudioConduit::~WebrtcAudioConduit() {
  CSFLogDebug(LOGTAG, "%s ", __FUNCTION__);
  MOZ_ASSERT(!mSendStream && !mRecvStream,
             "Call DeleteStreams prior to ~WebrtcAudioConduit.");
}

#define CONNECT(aCanonical, aMirror)                                       \
  do {                                                                     \
    /* Ensure the watchmanager is wired up before the mirror receives its  \
     * initial mirrored value. */                                          \
    mCall->mCallThread->DispatchStateChange(                               \
        NS_NewRunnableFunction(__func__, [this, self = RefPtr(this)] {     \
          mWatchManager.Watch(aMirror,                                     \
                              &WebrtcAudioConduit::OnControlConfigChange); \
        }));                                                               \
    (aCanonical).ConnectMirror(&(aMirror));                                \
  } while (0)

void WebrtcAudioConduit::InitControl(AudioConduitControlInterface* aControl) {
  MOZ_ASSERT(NS_IsMainThread());

  CONNECT(aControl->CanonicalReceiving(), mControl.mReceiving);
  CONNECT(aControl->CanonicalTransmitting(), mControl.mTransmitting);
  CONNECT(aControl->CanonicalLocalSsrcs(), mControl.mLocalSsrcs);
  CONNECT(aControl->CanonicalLocalCname(), mControl.mLocalCname);
  CONNECT(aControl->CanonicalMid(), mControl.mMid);
  CONNECT(aControl->CanonicalRemoteSsrc(), mControl.mRemoteSsrc);
  CONNECT(aControl->CanonicalSyncGroup(), mControl.mSyncGroup);
  CONNECT(aControl->CanonicalLocalRecvRtpExtensions(),
          mControl.mLocalRecvRtpExtensions);
  CONNECT(aControl->CanonicalLocalSendRtpExtensions(),
          mControl.mLocalSendRtpExtensions);
  CONNECT(aControl->CanonicalAudioSendCodec(), mControl.mSendCodec);
  CONNECT(aControl->CanonicalAudioRecvCodecs(), mControl.mRecvCodecs);
  CONNECT(aControl->CanonicalFrameTransformerProxySend(),
          mControl.mFrameTransformerProxySend);
  CONNECT(aControl->CanonicalFrameTransformerProxyRecv(),
          mControl.mFrameTransformerProxyRecv);
  mControl.mOnDtmfEventListener = aControl->OnDtmfEvent().Connect(
      mCall->mCallThread, this, &WebrtcAudioConduit::OnDtmfEvent);
}

#undef CONNECT

MediaConduitErrorCode WebrtcAudioConduit::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  mRtpSources.Connect(&mCanonicalRtpSources);
  return kMediaConduitNoError;
}

void WebrtcAudioConduit::OnDtmfEvent(const DtmfEvent& aEvent) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mDtmfEnabled);
  // Drop DTMF if sending is disabled.
  if (!mSendStream) {
    return;
  }
  mSendStream->SendTelephoneEvent(aEvent.mPayloadType, aEvent.mPayloadFrequency,
                                  aEvent.mEventCode, aEvent.mLengthMs);
}

void WebrtcAudioConduit::OnControlConfigChange() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  bool recvStreamReconfigureNeeded = false;
  bool sendStreamReconfigureNeeded = false;
  bool recvStreamRecreationNeeded = false;
  bool sendStreamRecreationNeeded = false;

  if (!mControl.mLocalSsrcs.Ref().empty()) {
    if (mControl.mLocalSsrcs.Ref()[0] != mSendStreamConfig.rtp.ssrc) {
      sendStreamRecreationNeeded = true;

      // For now...
      recvStreamRecreationNeeded = true;
    }
    mRecvStreamConfig.rtp.local_ssrc = mControl.mLocalSsrcs.Ref()[0];
    mSendStreamConfig.rtp.ssrc = mControl.mLocalSsrcs.Ref()[0];

    // In the future we can do this instead of recreating the recv stream:
    // if (mRecvStream) {
    //   mCall->Call()->OnLocalSsrcUpdated(mRecvStream,
    //                                     mControl.mLocalSsrcs.Ref()[0]);
    // }
  }

  if (mControl.mLocalCname.Ref() != mSendStreamConfig.rtp.c_name) {
    mSendStreamConfig.rtp.c_name = mControl.mLocalCname.Ref();
    sendStreamReconfigureNeeded = true;
  }

  if (mControl.mMid.Ref() != mSendStreamConfig.rtp.mid) {
    mSendStreamConfig.rtp.mid = mControl.mMid.Ref();
    sendStreamRecreationNeeded = true;
  }

  if (mControl.mRemoteSsrc.Ref() != mControl.mConfiguredRemoteSsrc) {
    mRecvStreamConfig.rtp.remote_ssrc = mControl.mConfiguredRemoteSsrc =
        mControl.mRemoteSsrc.Ref();
    recvStreamRecreationNeeded = true;
  }

  if (mControl.mSyncGroup.Ref() != mRecvStreamConfig.sync_group) {
    mRecvStreamConfig.sync_group = mControl.mSyncGroup.Ref();
    if (mRecvStream) {
      mCall->Call()->OnUpdateSyncGroup(*mRecvStream,
                                       mRecvStreamConfig.sync_group);
    }
  }

  if (auto filteredExtensions = FilterExtensions(
          LocalDirection::kSend, mControl.mLocalSendRtpExtensions);
      filteredExtensions != mSendStreamConfig.rtp.extensions) {
    // At the very least, we need a reconfigure. Recreation needed if the
    // extmap for any extension has changed, but not for adding/removing
    // extensions.
    sendStreamReconfigureNeeded = true;

    for (const auto& newExt : filteredExtensions) {
      if (sendStreamRecreationNeeded) {
        break;
      }
      for (const auto& oldExt : mSendStreamConfig.rtp.extensions) {
        if (newExt.uri == oldExt.uri) {
          if (newExt.id != oldExt.id) {
            sendStreamRecreationNeeded = true;
          }
          // We're done handling newExt, one way or another
          break;
        }
      }
    }

    mSendStreamConfig.rtp.extensions = std::move(filteredExtensions);
  }

  mControl.mSendCodec.Ref().apply([&](const auto& aConfig) {
    if (mControl.mConfiguredSendCodec != mControl.mSendCodec.Ref()) {
      mControl.mConfiguredSendCodec = mControl.mSendCodec;
      if (ValidateCodecConfig(aConfig, true) == kMediaConduitNoError) {
        mSendStreamConfig.encoder_factory =
            webrtc::CreateBuiltinAudioEncoderFactory();

        webrtc::AudioSendStream::Config::SendCodecSpec spec(
            aConfig.mType, CodecConfigToLibwebrtcFormat(aConfig));
        const auto info =
            mSendStreamConfig.encoder_factory->QueryAudioEncoder(spec.format);
        if (info) {
          if (const auto& maxBps = mControl.mConfiguredSendCodec
                                       ->mEncodingConstraints.maxBitrateBps) {
            spec.target_bitrate_bps =
                std::clamp(SaturatingCast<int>(*maxBps), info->min_bitrate_bps,
                           info->max_bitrate_bps);
          }
        }
        mSendStreamConfig.send_codec_spec = std::move(spec);

        const int kDefaultAudioBitrateBps = 32000;
        // Use the codec's own default bitrate when the application has not
        // configured a max -- it already accounts for stereo, SDP
        // maxaveragebitrate/maxplaybackrate, and fixed-rate codecs like
        // PCMU/PCMA/G.722.
        const int targetBps =
            mSendStreamConfig.send_codec_spec->target_bitrate_bps.value_or(
                info ? info->default_bitrate_bps : kDefaultAudioBitrateBps);
        mSendStreamConfig.min_bitrate_bps = targetBps;
        mSendStreamConfig.max_bitrate_bps = targetBps;
        mSendStreamConfig.include_in_congestion_control_allocation =
            aConfig.HasCongestionControlAck();

        mDtmfEnabled = aConfig.mDtmfEnabled;
        sendStreamReconfigureNeeded = true;
      }
    }
  });

  if (mControl.mConfiguredRecvCodecs != mControl.mRecvCodecs.Ref()) {
    mControl.mConfiguredRecvCodecs = mControl.mRecvCodecs;
    mRecvStreamConfig.decoder_factory = mCall->mAudioDecoderFactory;
    mRecvStreamConfig.decoder_map.clear();

    for (const auto& codec : mControl.mRecvCodecs.Ref()) {
      if (ValidateCodecConfig(codec, false) != kMediaConduitNoError) {
        continue;
      }
      mRecvStreamConfig.decoder_map.emplace(
          codec.mType, CodecConfigToLibwebrtcFormat(codec));
    }

    recvStreamReconfigureNeeded = true;
  }

  if (mControl.mConfiguredFrameTransformerProxySend.get() !=
      mControl.mFrameTransformerProxySend.Ref().get()) {
    mControl.mConfiguredFrameTransformerProxySend =
        mControl.mFrameTransformerProxySend.Ref();
    if (!mSendStreamConfig.frame_transformer) {
      mSendStreamConfig.frame_transformer =
          new webrtc::RefCountedObject<FrameTransformer>(false);
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
          new webrtc::RefCountedObject<FrameTransformer>(false);
      recvStreamRecreationNeeded = true;
    }
    static_cast<FrameTransformer*>(mRecvStreamConfig.frame_transformer.get())
        ->SetProxy(mControl.mConfiguredFrameTransformerProxyRecv);
  }

  if (!recvStreamReconfigureNeeded && !sendStreamReconfigureNeeded &&
      !recvStreamRecreationNeeded && !sendStreamRecreationNeeded &&
      mControl.mReceiving == mRecvStreamRunning &&
      mControl.mTransmitting == mSendStreamRunning) {
    // No changes applied -- no need to lock.
    return;
  }

  if (recvStreamRecreationNeeded) {
    recvStreamReconfigureNeeded = false;
  }
  if (sendStreamRecreationNeeded) {
    sendStreamReconfigureNeeded = false;
  }

  {
    AutoWriteLock lock(mLock);
    // Recreate/Stop/Start streams as needed.
    if (recvStreamRecreationNeeded) {
      DeleteRecvStream();
    }
    if (mControl.mReceiving) {
      CreateRecvStream();
    }
    if (sendStreamRecreationNeeded) {
      if (mControl.mTransmitting) {
        MemoSendStreamStats();
      }
      DeleteSendStream();
    }
    if (mControl.mTransmitting) {
      CreateSendStream();
    }
  }

  // We make sure to not hold the lock while stopping/starting/reconfiguring
  // streams, so as to not cause deadlocks. These methods can cause our platform
  // codecs to dispatch sync runnables to main, and main may grab the lock.

  if (mRecvStream && recvStreamReconfigureNeeded) {
    MOZ_ASSERT(!recvStreamRecreationNeeded);
    mRecvStream->SetDecoderMap(mRecvStreamConfig.decoder_map);
  }

  if (mSendStream && sendStreamReconfigureNeeded) {
    MOZ_ASSERT(!sendStreamRecreationNeeded);
    // TODO: Pass a callback here, so we can react to RTCErrors thrown by
    // libwebrtc.
    mSendStream->Reconfigure(mSendStreamConfig, nullptr);
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

std::vector<uint32_t> WebrtcAudioConduit::GetLocalSSRCs() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  return std::vector<uint32_t>(1, mRecvStreamConfig.rtp.local_ssrc);
}

bool WebrtcAudioConduit::OverrideRemoteSSRC(uint32_t aSsrc) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (mRecvStreamConfig.rtp.remote_ssrc == aSsrc) {
    return true;
  }
  mRecvStreamConfig.rtp.remote_ssrc = aSsrc;

  const bool wasReceiving = mRecvStreamRunning;
  const bool hadRecvStream = mRecvStream;

  StopReceiving();

  if (hadRecvStream) {
    AutoWriteLock lock(mLock);
    DeleteRecvStream();
    CreateRecvStream();
  }

  if (wasReceiving) {
    StartReceiving();
  }
  return true;
}

Maybe<Ssrc> WebrtcAudioConduit::GetRemoteSSRC() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  // libwebrtc uses 0 to mean a lack of SSRC. That is not to spec.
  return mRecvStreamConfig.rtp.remote_ssrc == 0
             ? Nothing()
             : Some(mRecvStreamConfig.rtp.remote_ssrc);
}

Maybe<webrtc::AudioReceiveStreamInterface::Stats>
WebrtcAudioConduit::GetReceiverStats() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (!mRecvStream) {
    return Nothing();
  }
  return Some(mRecvStream->GetStats());
}

Maybe<webrtc::AudioSendStream::Stats> WebrtcAudioConduit::GetSenderStats()
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
    // stats from the SSRC that was negotiated in SDP so that outbound-rtp
    // entries appear in getStats() even before a track arrives.
    const auto& ssrcs = mControl.mLocalSsrcs.Ref();
    if (ssrcs.empty()) {
      return Nothing();
    }
    webrtc::AudioSendStream::Stats synthStats;
    synthStats.local_ssrc = ssrcs[0];
    return Some(std::move(synthStats));
  }
  // Successfully got stats, so clear the transitional stats.
  mTransitionalSendStreamStats = Nothing();
  return Some(mSendStream->GetStats());
}

Maybe<webrtc::Call::Stats> WebrtcAudioConduit::GetCallStats() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  if (!mCall->Call()) {
    return Nothing();
  }
  return Some(mCall->Call()->GetStats());
}

void WebrtcAudioConduit::OnRtcpBye() { mRtcpByeEvent.Notify(); }

void WebrtcAudioConduit::OnRtcpTimeout() { mRtcpTimeoutEvent.Notify(); }

void WebrtcAudioConduit::SetTransportActive(bool aActive) {
  MOZ_ASSERT(mStsThread->IsOnCurrentThread());
  if (mTransportActive == aActive) {
    return;
  }

  // If false, This stops us from sending
  mTransportActive = aActive;
}

// AudioSessionConduit Implementation
MediaConduitErrorCode WebrtcAudioConduit::SendAudioFrame(
    std::unique_ptr<webrtc::AudioFrame> frame) {
  CSFLogDebug(LOGTAG, "%s ", __FUNCTION__);
  // Following checks need to be performed
  // 1. Non null audio buffer pointer, and
  // 2. Valid sample rate, and
  // 3. Appropriate Sample Length for 10 ms audio-frame. This represents the
  //    block size used upstream for processing.
  //    Ex: for 16000 sample rate , valid block-length is 160.
  //    Similarly for 32000 sample rate, valid block length is 320.

  if (!frame->data() ||
      (IsSamplingFreqSupported(frame->sample_rate_hz()) == false) ||
      ((frame->samples_per_channel() % (frame->sample_rate_hz() / 100) != 0))) {
    CSFLogError(LOGTAG, "%s Invalid Parameters ", __FUNCTION__);
    MOZ_ASSERT(PR_FALSE);
    return kMediaConduitMalformedArgument;
  }

  // This is the AudioProxyThread, blocking it for a bit is fine.
  AutoReadLock lock(mLock);
  if (!mSendStreamRunning) {
    CSFLogError(LOGTAG, "%s Engine not transmitting ", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  mSendStream->SendAudioData(std::move(frame));
  return kMediaConduitNoError;
}

MediaConduitErrorCode WebrtcAudioConduit::GetAudioFrame(
    int32_t samplingFreqHz, webrtc::AudioFrame* frame) {
  CSFLogDebug(LOGTAG, "%s ", __FUNCTION__);

  // validate params
  if (!frame) {
    CSFLogError(LOGTAG, "%s Null Audio Buffer Pointer", __FUNCTION__);
    MOZ_ASSERT(PR_FALSE);
    return kMediaConduitMalformedArgument;
  }

  // Validate sample length
  if (GetNum10msSamplesForFrequency(samplingFreqHz) == 0) {
    CSFLogError(LOGTAG, "%s Invalid Sampling Frequency ", __FUNCTION__);
    MOZ_ASSERT(PR_FALSE);
    return kMediaConduitMalformedArgument;
  }

  // If the lock is taken, skip this chunk to avoid blocking the audio thread.
  AutoTryReadLock tryLock(mLock);
  if (!tryLock) {
    CSFLogError(LOGTAG, "%s Conduit going through negotiation ", __FUNCTION__);
    return kMediaConduitPlayoutError;
  }

  // Conduit should have reception enabled before we ask for decoded
  // samples
  if (!mRecvStreamRunning) {
    CSFLogError(LOGTAG, "%s Engine not Receiving ", __FUNCTION__);
    return kMediaConduitSessionNotInited;
  }

  // Unfortunate to have to cast to an internal class, but that looks like the
  // only way short of interfacing with a layer above (which mixes all streams,
  // which we don't want) or a layer below (which we try to avoid because it is
  // less stable).
  auto info = static_cast<webrtc::AudioReceiveStreamImpl*>(mRecvStream)
                  ->GetAudioFrameWithInfo(samplingFreqHz, frame);

  if (info == webrtc::AudioMixer::Source::AudioFrameInfo::kError) {
    CSFLogError(LOGTAG, "%s Getting audio frame failed", __FUNCTION__);
    return kMediaConduitPlayoutError;
  }

  CSFLogDebug(LOGTAG, "%s Got %zu channels of %zu samples", __FUNCTION__,
              frame->num_channels(), frame->samples_per_channel());
  return kMediaConduitNoError;
}

// Transport Layer Callbacks
void WebrtcAudioConduit::OnRtpReceived(webrtc::RtpPacketReceived&& aPacket,
                                       webrtc::RTPHeader&& aHeader) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  // We should only be handling packets on this conduit if we are set to receive
  // them.
  if (!mControl.mReceiving) {
    CSFLogVerbose(LOGTAG,
                  "AudioConduit %p: Discarding packet SEQ# %u SSRC %u as not "
                  "configured to receive.",
                  this, aPacket.SequenceNumber(), aHeader.ssrc);
    return;
  }

  if (mAllowSsrcChange && mRecvStreamConfig.rtp.remote_ssrc != aHeader.ssrc) {
    CSFLogDebug(LOGTAG, "%s: switching from SSRC %u to %u", __FUNCTION__,
                mRecvStreamConfig.rtp.remote_ssrc, aHeader.ssrc);
    OverrideRemoteSSRC(aHeader.ssrc);
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
        webrtc::MediaType::AUDIO, std::move(aPacket),
        [self = RefPtr<WebrtcAudioConduit>(this)](
            const webrtc::RtpPacketReceived& packet) {
          CSFLogVerbose(
              LOGTAG,
              "AudioConduit %p: failed demuxing packet, ssrc: %u seq: %u",
              self.get(), packet.Ssrc(), packet.SequenceNumber());
          return false;
        });
  }
}

void WebrtcAudioConduit::OnRtcpReceived(webrtc::CopyOnWriteBuffer&& aPacket) {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  if (mCall->Call()) {
    mCall->Call()->Receiver()->DeliverRtcpPacket(std::move(aPacket));
  }
}

Maybe<uint16_t> WebrtcAudioConduit::RtpSendBaseSeqFor(uint32_t aSsrc) const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  auto it = mRtpSendBaseSeqs.find(aSsrc);
  if (it == mRtpSendBaseSeqs.end()) {
    return Nothing();
  }
  return Some(it->second);
}

const dom::RTCStatsTimestampMaker& WebrtcAudioConduit::GetTimestampMaker()
    const {
  return mCall->GetTimestampMaker();
}

void WebrtcAudioConduit::StopTransmitting() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(!mLock.LockedForWritingByCurrentThread());

  if (!mSendStreamRunning) {
    return;
  }

  if (mSendStream) {
    CSFLogDebug(LOGTAG, "%s Stopping send stream", __FUNCTION__);
    mSendStream->Stop();
  }

  mSendStreamRunning = false;
}

void WebrtcAudioConduit::StartTransmitting() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mSendStream);
  MOZ_ASSERT(!mLock.LockedForWritingByCurrentThread());

  if (mSendStreamRunning) {
    return;
  }

  CSFLogDebug(LOGTAG, "%s Starting send stream", __FUNCTION__);

  mCall->Call()->SignalChannelNetworkState(webrtc::MediaType::AUDIO,
                                           webrtc::kNetworkUp);
  mSendStream->Start();
  mSendStreamRunning = true;
}

void WebrtcAudioConduit::StopReceiving() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(!mLock.LockedForWritingByCurrentThread());

  if (!mRecvStreamRunning) {
    return;
  }

  if (mRecvStream) {
    CSFLogDebug(LOGTAG, "%s Stopping recv stream", __FUNCTION__);
    mRecvStream->Stop();
  }

  mRecvStreamRunning = false;
}

void WebrtcAudioConduit::StartReceiving() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mRecvStream);
  MOZ_ASSERT(!mLock.LockedForWritingByCurrentThread());

  if (mRecvStreamRunning) {
    return;
  }

  CSFLogDebug(LOGTAG, "%s Starting receive stream (SSRC %u (0x%x))",
              __FUNCTION__, mRecvStreamConfig.rtp.remote_ssrc,
              mRecvStreamConfig.rtp.remote_ssrc);

  mCall->Call()->SignalChannelNetworkState(webrtc::MediaType::AUDIO,
                                           webrtc::kNetworkUp);
  mRecvStream->Start();
  mRecvStreamRunning = true;
}

bool WebrtcAudioConduit::SendRtp(const uint8_t* aData, size_t aLength,
                                 const webrtc::PacketOptions& aOptions) {
  MOZ_ASSERT(aLength >= 12);
  const uint16_t seqno = ntohs(*((uint16_t*)&aData[2]));
  const uint32_t ssrc = ntohl(*((uint32_t*)&aData[8]));

  CSFLogVerbose(
      LOGTAG,
      "AudioConduit %p: Sending RTP Packet seq# %u, len %zu, SSRC %u (0x%x)",
      this, seqno, aLength, ssrc, ssrc);

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "AudioConduit %p: RTP Packet Send Failed ", this);
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
        __func__, [this, self = RefPtr<WebrtcAudioConduit>(this),
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

bool WebrtcAudioConduit::SendSenderRtcp(const uint8_t* aData, size_t aLength) {
  CSFLogVerbose(
      LOGTAG,
      "AudioConduit %p: Sending RTCP SR Packet, len %zu, SSRC %u (0x%x)", this,
      aLength, (uint32_t)ntohl(*((uint32_t*)&aData[4])),
      (uint32_t)ntohl(*((uint32_t*)&aData[4])));

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "%s RTCP SR Packet Send Failed ", __FUNCTION__);
    return false;
  }

  MediaPacket packet;
  packet.Copy(aData, aLength, aLength + SRTP_MAX_EXPANSION);
  packet.SetType(MediaPacket::RTCP);
  mSenderRtcpSendEvent.Notify(std::move(packet));
  return true;
}

bool WebrtcAudioConduit::SendReceiverRtcp(const uint8_t* aData,
                                          size_t aLength) {
  CSFLogVerbose(
      LOGTAG,
      "AudioConduit %p: Sending RTCP RR Packet, len %zu, SSRC %u (0x%x)", this,
      aLength, (uint32_t)ntohl(*((uint32_t*)&aData[4])),
      (uint32_t)ntohl(*((uint32_t*)&aData[4])));

  if (!mTransportActive) {
    CSFLogError(LOGTAG, "AudioConduit %p: RTCP RR Packet Send Failed", this);
    return false;
  }

  MediaPacket packet;
  packet.Copy(aData, aLength, aLength + SRTP_MAX_EXPANSION);
  packet.SetType(MediaPacket::RTCP);
  mReceiverRtcpSendEvent.Notify(std::move(packet));
  return true;
}

/**
 *  Supported Sampling Frequencies.
 */
bool WebrtcAudioConduit::IsSamplingFreqSupported(int freq) const {
  return GetNum10msSamplesForFrequency(freq) != 0;
}

const std::vector<webrtc::RtpSource>&
WebrtcAudioConduit::GetUpstreamRtpSources() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mRtpSources;
}

/* Return block-length of 10 ms audio frame in number of samples */
unsigned int WebrtcAudioConduit::GetNum10msSamplesForFrequency(
    int samplingFreqHz) const {
  switch (samplingFreqHz) {
    case 16000:
      return 160;  // 160 samples
    case 32000:
      return 320;  // 320 samples
    case 44100:
      return 441;  // 441 samples
    case 48000:
      return 480;  // 480 samples
    default:
      return 0;  // invalid or unsupported
  }
}

/**
 * Perform validation on the codecConfig to be applied.
 * Verifies if the codec is already applied.
 */
MediaConduitErrorCode WebrtcAudioConduit::ValidateCodecConfig(
    const AudioCodecConfig& codecInfo, bool send) {
  if (codecInfo.mName.empty()) {
    CSFLogError(LOGTAG, "%s Empty Payload Name ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  // Only mono or stereo channels supported
  if ((codecInfo.mChannels != 1) && (codecInfo.mChannels != 2)) {
    CSFLogError(LOGTAG, "%s Channel Unsupported ", __FUNCTION__);
    return kMediaConduitMalformedArgument;
  }

  return kMediaConduitNoError;
}

RtpExtList WebrtcAudioConduit::FilterExtensions(LocalDirection aDirection,
                                                const RtpExtList& aExtensions) {
  const bool isSend = aDirection == LocalDirection::kSend;
  RtpExtList filteredExtensions;

  for (const auto& extension : aExtensions) {
    // ssrc-audio-level RTP header extension
    if (extension.uri == webrtc::RtpExtension::kAudioLevelUri) {
      filteredExtensions.push_back(
          webrtc::RtpExtension(extension.uri, extension.id));
    }

    // csrc-audio-level RTP header extension
    if (extension.uri == webrtc::RtpExtension::kCsrcAudioLevelsUri) {
      if (isSend) {
        continue;
      }
      filteredExtensions.push_back(
          webrtc::RtpExtension(extension.uri, extension.id));
    }

    // MID RTP header extension
    if (extension.uri == webrtc::RtpExtension::kMidUri) {
      if (!isSend) {
        // TODO: recv mid support, see also bug 1727211
        continue;
      }
      filteredExtensions.push_back(
          webrtc::RtpExtension(extension.uri, extension.id));
    }

    // TransportCC header extension
    if (extension.uri == webrtc::RtpExtension::kTransportSequenceNumberUri) {
      filteredExtensions.push_back(
          webrtc::RtpExtension(extension.uri, extension.id));
    }
  }

  return filteredExtensions;
}

webrtc::SdpAudioFormat WebrtcAudioConduit::CodecConfigToLibwebrtcFormat(
    const AudioCodecConfig& aConfig) {
  webrtc::CodecParameterMap parameters;
  if (aConfig.mName == kOpusCodecName) {
    if (aConfig.mChannels == 2) {
      parameters[kCodecParamStereo] = kParamValueTrue;
    }
    if (aConfig.mFECEnabled) {
      parameters[kCodecParamUseInbandFec] = kParamValueTrue;
    }
    if (aConfig.mDTXEnabled) {
      parameters[kCodecParamUseDtx] = kParamValueTrue;
    }
    if (aConfig.mMaxPlaybackRate) {
      parameters[std::string(kCodecParamMaxPlaybackRate)] =
          std::to_string(aConfig.mMaxPlaybackRate);
    }
    if (aConfig.mMaxAverageBitrate) {
      parameters[std::string(kCodecParamMaxAverageBitrate)] =
          std::to_string(aConfig.mMaxAverageBitrate);
    }
    if (aConfig.mFrameSizeMs) {
      parameters[std::string(kCodecParamPTime)] =
          std::to_string(aConfig.mFrameSizeMs);
    }
    if (aConfig.mMinFrameSizeMs) {
      parameters[std::string(kCodecParamMinPTime)] =
          std::to_string(aConfig.mMinFrameSizeMs);
    }
    if (aConfig.mMaxFrameSizeMs) {
      parameters[std::string(kCodecParamMaxPTime)] =
          std::to_string(aConfig.mMaxFrameSizeMs);
    }
    if (aConfig.mCbrEnabled) {
      parameters[kCodecParamCbr] = kParamValueTrue;
    }
  }

  return webrtc::SdpAudioFormat(aConfig.mName, aConfig.mFreq, aConfig.mChannels,
                                parameters);
}

void WebrtcAudioConduit::DeleteSendStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mLock.LockedForWritingByCurrentThread());

  if (!mSendStream) {
    return;
  }

  mCall->Call()->DestroyAudioSendStream(mSendStream);
  mSendStreamRunning = false;
  mSendStream = nullptr;

  // Reset base_seqs in case ssrcs get re-used.
  mRtpSendBaseSeqs.clear();
}

void WebrtcAudioConduit::MemoSendStreamStats() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  // If we are going to be recreating the send stream, we hold onto stats until
  // libwebrtc stats collection catches up.
  if (mControl.mTransmitting && mSendStream) {
    const auto stats = mSendStream->GetStats();
    mTransitionalSendStreamStats = Some(stats);
  }
}

void WebrtcAudioConduit::CreateSendStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mLock.LockedForWritingByCurrentThread());

  if (mSendStream) {
    return;
  }

  mSendStream = mCall->Call()->CreateAudioSendStream(mSendStreamConfig);
}

void WebrtcAudioConduit::DeleteRecvStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mLock.LockedForWritingByCurrentThread());

  if (!mRecvStream) {
    return;
  }

  mCall->Call()->DestroyAudioReceiveStream(mRecvStream);
  mRecvStreamRunning = false;
  mRecvStream = nullptr;
}

void WebrtcAudioConduit::CreateRecvStream() {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());
  MOZ_ASSERT(mLock.LockedForWritingByCurrentThread());

  if (mRecvStream) {
    return;
  }

  // Config::Copy() does not copy move-only fields like
  // on_frame_delivered_callback, so it needs to be (re)installed on the copy
  // that is actually handed to the stream.
  webrtc::AudioReceiveStreamInterface::Config config = mRecvStreamConfig.Copy();
  // The receive stream retains this callback (and the RefPtr captured in
  // it) until DeleteRecvStream() runs it down as part of conduit shutdown,
  // so there is no cycle beyond that lifetime. Called synchronously on
  // whatever thread decodes audio, not necessarily mCallThread, so hop over
  // before touching mSourceTracker, which is not thread-safe.
  config.on_frame_delivered_callback =
      [self = RefPtr<WebrtcAudioConduit>(this)](
          const webrtc::RtpPacketInfos& aPacketInfos,
          webrtc::Timestamp aTimestamp) {
        self->mCallThread->Dispatch(NS_NewRunnableFunction(
            "WebrtcAudioConduit::OnFrameDelivered",
            [self, aPacketInfos, aTimestamp] {
              self->mSourceTracker.OnFrameDelivered(aPacketInfos, aTimestamp);
            }));
      };
  mRecvStream = mCall->Call()->CreateAudioReceiveStream(std::move(config));
  // Ensure that we set the jitter buffer target on this stream.
  mRecvStream->SetBaseMinimumPlayoutDelayMs(mJitterBufferTargetMs);
}

void WebrtcAudioConduit::SetJitterBufferTarget(DOMHighResTimeStamp aTargetMs) {
  MOZ_RELEASE_ASSERT(aTargetMs <= std::numeric_limits<uint16_t>::max());
  MOZ_RELEASE_ASSERT(aTargetMs >= 0);

  MOZ_ALWAYS_SUCCEEDS(mCallThread->Dispatch(NS_NewRunnableFunction(
      __func__,
      [this, self = RefPtr<WebrtcAudioConduit>(this), targetMs = aTargetMs] {
        mJitterBufferTargetMs = static_cast<uint16_t>(targetMs);
        if (mRecvStream) {
          mRecvStream->SetBaseMinimumPlayoutDelayMs(targetMs);
        }
      })));
}

void WebrtcAudioConduit::DeliverPacket(webrtc::CopyOnWriteBuffer packet,
                                       PacketType type) {
  // Currently unused.
  MOZ_ASSERT(false);
}

Maybe<int> WebrtcAudioConduit::ActiveSendPayloadType() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  auto stats = GetSenderStats();
  if (!stats) {
    return Nothing();
  }

  if (!stats->codec_payload_type) {
    return Nothing();
  }

  return Some(*stats->codec_payload_type);
}

Maybe<int> WebrtcAudioConduit::ActiveRecvPayloadType() const {
  MOZ_ASSERT(mCallThread->IsOnCurrentThread());

  auto stats = GetReceiverStats();
  if (!stats) {
    return Nothing();
  }

  if (!stats->codec_payload_type) {
    return Nothing();
  }

  return Some(*stats->codec_payload_type);
}

}  // namespace mozilla
