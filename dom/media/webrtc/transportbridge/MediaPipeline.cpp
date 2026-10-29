/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include "MediaPipeline.h"

#include <stdint.h>

#include <sstream>
#include <utility>

#include "AudioConverter.h"
#include "AudioSegment.h"
#include "DOMMediaStream.h"
#include "ImageContainer.h"
#include "ImageTypes.h"
#include "MediaEngine.h"
#include "MediaSegment.h"
#include "MediaStreamTrack.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "RtpLogger.h"
#include "Tracing.h"
#include "VideoFrameConverter.h"
#include "VideoSegment.h"
#include "VideoStreamTrack.h"
#include "VideoUtils.h"
#include "common_video/include/video_frame_buffer.h"
#include "gfxUtils.h"
#include "jsapi/MediaTransportHandler.h"
#include "jsapi/PeerConnectionImpl.h"
#include "libwebrtcglue/MediaConduitInterface.h"
#include "libwebrtcglue/WebrtcImageBuffer.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Logging.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PeerIdentity.h"
#include "mozilla/Preferences.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsError.h"
#include "nsThreadUtils.h"
#include "transport/runnable_utils.h"

// Max size given stereo is 480*2*2 = 1920 (10ms of 16-bits stereo audio at
// 48KHz)
#define AUDIO_SAMPLE_BUFFER_MAX_BYTES (480 * 2 * 2)
static_assert((WEBRTC_MAX_SAMPLE_RATE / 100) * sizeof(uint16_t) * 2 <=
                  AUDIO_SAMPLE_BUFFER_MAX_BYTES,
              "AUDIO_SAMPLE_BUFFER_MAX_BYTES is not large enough");

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;

mozilla::LazyLogModule gMediaPipelineLog("MediaPipeline");

// The CICP code points (Rec. ITU-T H.273) of the color space a frame
// libwebrtc decoded itself declares, for gfxUtils to map as it does for the
// platform decoders. Every value is listed, so that one added to the enums
// does not go unnoticed.
static gfx::CICP::ColourPrimaries ToCicp(
    webrtc::ColorSpace::PrimaryID aPrimaries) {
  switch (aPrimaries) {
    case webrtc::ColorSpace::PrimaryID::kBT709:
      return gfx::CICP::CP_BT709;
    case webrtc::ColorSpace::PrimaryID::kUnspecified:
      return gfx::CICP::CP_UNSPECIFIED;
    case webrtc::ColorSpace::PrimaryID::kBT470M:
      return gfx::CICP::CP_BT470M;
    case webrtc::ColorSpace::PrimaryID::kBT470BG:
      return gfx::CICP::CP_BT470BG;
    case webrtc::ColorSpace::PrimaryID::kSMPTE170M:
      return gfx::CICP::CP_BT601;
    case webrtc::ColorSpace::PrimaryID::kSMPTE240M:
      return gfx::CICP::CP_SMPTE240;
    case webrtc::ColorSpace::PrimaryID::kFILM:
      return gfx::CICP::CP_GENERIC_FILM;
    case webrtc::ColorSpace::PrimaryID::kBT2020:
      return gfx::CICP::CP_BT2020;
    case webrtc::ColorSpace::PrimaryID::kSMPTEST428:
      return gfx::CICP::CP_XYZ;
    case webrtc::ColorSpace::PrimaryID::kSMPTEST431:
      return gfx::CICP::CP_SMPTE431;
    case webrtc::ColorSpace::PrimaryID::kSMPTEST432:
      return gfx::CICP::CP_SMPTE432;
    case webrtc::ColorSpace::PrimaryID::kJEDECP22:
      return gfx::CICP::CP_EBU3213;
  }
  return gfx::CICP::CP_UNSPECIFIED;
}

static gfx::CICP::TransferCharacteristics ToCicp(
    webrtc::ColorSpace::TransferID aTransfer) {
  switch (aTransfer) {
    case webrtc::ColorSpace::TransferID::kBT709:
      return gfx::CICP::TC_BT709;
    case webrtc::ColorSpace::TransferID::kUnspecified:
      return gfx::CICP::TC_UNSPECIFIED;
    case webrtc::ColorSpace::TransferID::kGAMMA22:
      return gfx::CICP::TC_BT470M;
    case webrtc::ColorSpace::TransferID::kGAMMA28:
      return gfx::CICP::TC_BT470BG;
    case webrtc::ColorSpace::TransferID::kSMPTE170M:
      return gfx::CICP::TC_BT601;
    case webrtc::ColorSpace::TransferID::kSMPTE240M:
      return gfx::CICP::TC_SMPTE240;
    case webrtc::ColorSpace::TransferID::kLINEAR:
      return gfx::CICP::TC_LINEAR;
    case webrtc::ColorSpace::TransferID::kLOG:
      return gfx::CICP::TC_LOG_100;
    case webrtc::ColorSpace::TransferID::kLOG_SQRT:
      return gfx::CICP::TC_LOG_100_SQRT10;
    case webrtc::ColorSpace::TransferID::kIEC61966_2_4:
      return gfx::CICP::TC_IEC61966;
    case webrtc::ColorSpace::TransferID::kBT1361_ECG:
      return gfx::CICP::TC_BT_1361;
    case webrtc::ColorSpace::TransferID::kIEC61966_2_1:
      return gfx::CICP::TC_SRGB;
    case webrtc::ColorSpace::TransferID::kBT2020_10:
      return gfx::CICP::TC_BT2020_10BIT;
    case webrtc::ColorSpace::TransferID::kBT2020_12:
      return gfx::CICP::TC_BT2020_12BIT;
    case webrtc::ColorSpace::TransferID::kSMPTEST2084:
      return gfx::CICP::TC_SMPTE2084;
    case webrtc::ColorSpace::TransferID::kSMPTEST428:
      return gfx::CICP::TC_SMPTE428;
    case webrtc::ColorSpace::TransferID::kARIB_STD_B67:
      return gfx::CICP::TC_HLG;
  }
  return gfx::CICP::TC_UNSPECIFIED;
}

static gfx::CICP::MatrixCoefficients ToCicp(
    webrtc::ColorSpace::MatrixID aMatrix) {
  switch (aMatrix) {
    case webrtc::ColorSpace::MatrixID::kRGB:
      return gfx::CICP::MC_IDENTITY;
    case webrtc::ColorSpace::MatrixID::kBT709:
      return gfx::CICP::MC_BT709;
    case webrtc::ColorSpace::MatrixID::kUnspecified:
      return gfx::CICP::MC_UNSPECIFIED;
    case webrtc::ColorSpace::MatrixID::kFCC:
      return gfx::CICP::MC_FCC;
    case webrtc::ColorSpace::MatrixID::kBT470BG:
      return gfx::CICP::MC_BT470BG;
    case webrtc::ColorSpace::MatrixID::kSMPTE170M:
      return gfx::CICP::MC_BT601;
    case webrtc::ColorSpace::MatrixID::kSMPTE240M:
      return gfx::CICP::MC_SMPTE240;
    case webrtc::ColorSpace::MatrixID::kYCOCG:
      return gfx::CICP::MC_YCGCO;
    case webrtc::ColorSpace::MatrixID::kBT2020_NCL:
      return gfx::CICP::MC_BT2020_NCL;
    case webrtc::ColorSpace::MatrixID::kBT2020_CL:
      return gfx::CICP::MC_BT2020_CL;
    case webrtc::ColorSpace::MatrixID::kSMPTE2085:
      return gfx::CICP::MC_SMPTE2085;
    case webrtc::ColorSpace::MatrixID::kCDNCLS:
      return gfx::CICP::MC_CHROMAT_NCL;
    case webrtc::ColorSpace::MatrixID::kCDCLS:
      return gfx::CICP::MC_CHROMAT_CL;
    case webrtc::ColorSpace::MatrixID::kBT2100_ICTCP:
      return gfx::CICP::MC_ICTCP;
  }
  return gfx::CICP::MC_UNSPECIFIED;
}

static gfx::ColorRange ToColorRange(webrtc::ColorSpace::RangeID aRange) {
  switch (aRange) {
    case webrtc::ColorSpace::RangeID::kFull:
      return gfx::ColorRange::FULL;
    case webrtc::ColorSpace::RangeID::kInvalid:
    case webrtc::ColorSpace::RangeID::kLimited:
    case webrtc::ColorSpace::RangeID::kDerived:
      break;
  }
  return gfx::ColorRange::LIMITED;
}

namespace mozilla {

// An async inserter for audio data, to avoid running audio codec encoders
// on the MTG/input audio thread.  Basically just bounces all the audio
// data to a single audio processing/input queue.  We could if we wanted to
// use multiple threads and a TaskQueue.
class AudioProxyThread {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioProxyThread)

  explicit AudioProxyThread(RefPtr<AudioSessionConduit> aConduit)
      : mConduit(std::move(aConduit)),
        mTaskQueue(TaskQueue::Create(
            GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER), "AudioProxy")),
        mAudioConverter(nullptr) {
    MOZ_ASSERT(mConduit);
    MOZ_COUNT_CTOR(AudioProxyThread);
  }

  // This function is the identity if aInputRate is supported.
  // Else, it returns a rate that is supported, that ensure no loss in audio
  // quality: the sampling rate returned is always greater to the inputed
  // sampling-rate, if they differ..
  uint32_t AppropriateSendingRateForInputRate(uint32_t aInputRate) {
    AudioSessionConduit* conduit =
        static_cast<AudioSessionConduit*>(mConduit.get());
    if (conduit->IsSamplingFreqSupported(aInputRate)) {
      return aInputRate;
    }
    if (aInputRate < 16000) {
      return 16000;
    }
    if (aInputRate < 32000) {
      return 32000;
    }
    if (aInputRate < 44100) {
      return 44100;
    }
    return 48000;
  }

  // From an arbitrary AudioChunk at sampling-rate aRate, process the audio into
  // something the conduit can work with (or send silence if the track is not
  // enabled), and send the audio in 10ms chunks to the conduit.
  void InternalProcessAudioChunk(TrackRate aRate, const AudioChunk& aChunk,
                                 bool aEnabled) {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());

    // Convert to interleaved 16-bits integer audio, with a maximum of two
    // channels (since the WebRTC.org code below makes the assumption that the
    // input audio is either mono or stereo), with a sample-rate rate that is
    // 16, 32, 44.1, or 48kHz.
    uint32_t outputChannels = aChunk.ChannelCount() == 1 ? 1 : 2;
    int32_t transmissionRate = AppropriateSendingRateForInputRate(aRate);

    // We take advantage of the fact that the common case (microphone directly
    // to PeerConnection, that is, a normal call), the samples are already
    // 16-bits mono, so the representation in interleaved and planar is the
    // same, and we can just use that.
    if (aEnabled && outputChannels == 1 &&
        aChunk.mBufferFormat == AUDIO_FORMAT_S16 && transmissionRate == aRate) {
      const int16_t* samples = aChunk.ChannelData<int16_t>().Elements()[0];
      PacketizeAndSend(samples, transmissionRate, outputChannels,
                       aChunk.mDuration);
      return;
    }

    uint32_t sampleCount = aChunk.mDuration * outputChannels;
    if (mInterleavedAudio.Length() < sampleCount) {
      mInterleavedAudio.SetLength(sampleCount);
    }

    if (!aEnabled || aChunk.mBufferFormat == AUDIO_FORMAT_SILENCE) {
      PodZero(mInterleavedAudio.Elements(), sampleCount);
    } else if (aChunk.mBufferFormat == AUDIO_FORMAT_FLOAT32) {
      DownmixAndInterleave(aChunk.ChannelData<float>(), aChunk.mDuration,
                           aChunk.mVolume, outputChannels,
                           mInterleavedAudio.Elements());
    } else if (aChunk.mBufferFormat == AUDIO_FORMAT_S16) {
      DownmixAndInterleave(aChunk.ChannelData<int16_t>(), aChunk.mDuration,
                           aChunk.mVolume, outputChannels,
                           mInterleavedAudio.Elements());
    }
    int16_t* inputAudio = mInterleavedAudio.Elements();
    size_t inputAudioFrameCount = aChunk.mDuration;

    AudioConfig inputConfig(AudioConfig::ChannelLayout(outputChannels), aRate,
                            AudioConfig::FORMAT_S16);
    AudioConfig outputConfig(AudioConfig::ChannelLayout(outputChannels),
                             transmissionRate, AudioConfig::FORMAT_S16);
    // Resample to an acceptable sample-rate for the sending side
    if (!mAudioConverter || mAudioConverter->InputConfig() != inputConfig ||
        mAudioConverter->OutputConfig() != outputConfig) {
      mAudioConverter =
          std::make_unique<AudioConverter>(inputConfig, outputConfig);
    }

    int16_t* processedAudio = nullptr;
    size_t framesProcessed =
        mAudioConverter->Process(inputAudio, inputAudioFrameCount);

    if (framesProcessed == 0) {
      // In place conversion not possible, use a buffer.
      framesProcessed = mAudioConverter->Process(mOutputAudio, inputAudio,
                                                 inputAudioFrameCount);
      processedAudio = mOutputAudio.Data();
    } else {
      processedAudio = inputAudio;
    }

    PacketizeAndSend(processedAudio, transmissionRate, outputChannels,
                     framesProcessed);
  }

  // This packetizes aAudioData in 10ms chunks and sends it.
  // aAudioData is interleaved audio data at a rate and with a channel count
  // that is appropriate to send with the conduit.
  void PacketizeAndSend(const int16_t* aAudioData, uint32_t aRate,
                        uint32_t aChannels, uint32_t aFrameCount) {
    MOZ_ASSERT(AppropriateSendingRateForInputRate(aRate) == aRate);
    MOZ_ASSERT(aChannels == 1 || aChannels == 2);
    MOZ_ASSERT(aAudioData);

    uint32_t audio_10ms = aRate / 100;

    if (!mPacketizer || mPacketizer->mPacketSize != audio_10ms ||
        mPacketizer->mChannels != aChannels) {
      // It's the right thing to drop the bit of audio still in the packetizer:
      // we don't want to send to the conduit audio that has two different
      // rates while telling it that it has a constante rate.
      mPacketizer = std::make_unique<AudioPacketizer<int16_t, int16_t>>(
          audio_10ms, aChannels);
      mPacket = std::make_unique<int16_t[]>(audio_10ms * aChannels);
    }

    // aFrameCount is one graph iteration's worth and the packetizer is drained
    // to under one packet every call, with channels capped at two (above), so
    // the queued sample count stays small and Input always succeeds here.
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(mPacketizer->Input(aAudioData, aFrameCount)));

    while (mPacketizer->PacketsAvailable()) {
      mPacketizer->Output(mPacket.get());
      auto frame = std::make_unique<webrtc::AudioFrame>();
      // UpdateFrame makes a copy of the audio data.
      frame->UpdateFrame(frame->timestamp_, mPacket.get(),
                         mPacketizer->mPacketSize, aRate, frame->speech_type_,
                         frame->vad_activity_, mPacketizer->mChannels);
      mConduit->SendAudioFrame(std::move(frame));
    }
  }

  void QueueAudioChunk(TrackRate aRate, const AudioChunk& aChunk,
                       bool aEnabled) {
    RefPtr<AudioProxyThread> self = this;
    nsresult rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
        "AudioProxyThread::QueueAudioChunk", [self, aRate, aChunk, aEnabled]() {
          self->InternalProcessAudioChunk(aRate, aChunk, aEnabled);
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

 protected:
  virtual ~AudioProxyThread() { MOZ_COUNT_DTOR(AudioProxyThread); }

  const RefPtr<AudioSessionConduit> mConduit;
  const RefPtr<TaskQueue> mTaskQueue;
  // Only accessed on mTaskQueue
  UniquePtr<AudioPacketizer<int16_t, int16_t>> mPacketizer;
  // A buffer to hold a single packet of audio.
  UniquePtr<int16_t[]> mPacket;
  nsTArray<int16_t> mInterleavedAudio;
  AlignedShortBuffer mOutputAudio;
  UniquePtr<AudioConverter> mAudioConverter;
};

#define INIT_MIRROR(name, val) \
  name(AbstractThread::MainThread(), val, "MediaPipeline::" #name " (Mirror)")

MediaPipeline::MediaPipeline(const std::string& aPc,
                             RefPtr<MediaTransportHandler> aTransportHandler,
                             DirectionType aDirection,
                             RefPtr<AbstractThread> aCallThread,
                             RefPtr<nsISerialEventTarget> aStsThread,
                             RefPtr<MediaSessionConduit> aConduit)
    : mConduit(std::move(aConduit)),
      mDirection(aDirection),
      mCallThread(std::move(aCallThread)),
      mStsThread(std::move(aStsThread)),
      INIT_MIRROR(mActive, false),
      mActiveSts(false),
      mLevel(0),
      mTransportHandler(std::move(aTransportHandler)),
      mRtpPacketsSent(0),
      mRtcpPacketsSent(0),
      mRtpPacketsReceived(0),
      mRtpBytesSent(0),
      mRtpBytesReceived(0),
      mPc(aPc),
      mRtpHeaderExtensionMap(std::make_unique<webrtc::RtpHeaderExtensionMap>()),
      mPacketDumper(PacketDumper::GetPacketDumper(mPc)) {
  if (mDirection == DirectionType::TRANSMIT) {
    mRtpSendEventListener = mConduit->SenderRtpSendEvent().Connect(
        mStsThread, this, &MediaPipeline::SendPacket);
    mSenderRtcpSendEventListener = mConduit->SenderRtcpSendEvent().Connect(
        mStsThread, this, &MediaPipeline::SendPacket);
    mConduit->ConnectSenderRtcpEvent(mRtcpReceiveEvent);
  } else {
    mConduit->ConnectReceiverRtpEvent(mRtpReceiveEvent);
    mConduit->ConnectReceiverRtcpEvent(mRtcpReceiveEvent);
    mReceiverRtcpSendEventListener = mConduit->ReceiverRtcpSendEvent().Connect(
        mStsThread, this, &MediaPipeline::SendPacket);
  }
}

#undef INIT_MIRROR

MediaPipeline::~MediaPipeline() {
  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info, "Destroying MediaPipeline: {}",
              mDescription);
}

void MediaPipeline::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  mActive.DisconnectIfConnected();
  RUN_ON_THREAD(mStsThread,
                WrapRunnable(RefPtr<MediaPipeline>(this),
                             &MediaPipeline::DetachTransport_s),
                NS_DISPATCH_NORMAL);
}

void MediaPipeline::DetachTransport_s() {
  ASSERT_ON_THREAD(mStsThread);

  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info, "{} in {}", mDescription,
              __FUNCTION__);

  disconnect_all();
  mRtpState = TransportLayer::TS_NONE;
  mRtcpState = TransportLayer::TS_NONE;
  mTransportId.clear();
  mConduit->SetTransportActive(false);
  mRtpSendEventListener.DisconnectIfExists();
  mSenderRtcpSendEventListener.DisconnectIfExists();
  mReceiverRtcpSendEventListener.DisconnectIfExists();
  mRtpPacketReceivedListener.DisconnectIfExists();
  mStateChangeListener.DisconnectIfExists();
  mRtcpStateChangeListener.DisconnectIfExists();
  mEncryptedSendingListener.DisconnectIfExists();
  mAlpnNegotiatedListener.DisconnectIfExists();
}

void MediaPipeline::UpdateTransport_m(const std::string& aTransportId,
                                      UniquePtr<MediaPipelineFilter>&& aFilter,
                                      bool aSignalingStable) {
  mStsThread->Dispatch(NS_NewRunnableFunction(
      __func__,
      [aTransportId, filter = std::move(aFilter),
       self = RefPtr<MediaPipeline>(this), aSignalingStable]() mutable {
        self->UpdateTransport_s(aTransportId, std::move(filter),
                                aSignalingStable);
      }));
}

void MediaPipeline::UpdateTransport_s(const std::string& aTransportId,
                                      UniquePtr<MediaPipelineFilter>&& aFilter,
                                      bool aSignalingStable) {
  ASSERT_ON_THREAD(mStsThread);
  // TODO: Now that this no longer uses sigslot, we can handle these events on
  // threads other than STS, when it makes sense to. It might be worthwhile to
  // do the packet handling on the call thread only, to save a thread dispatch.
  if (!mSignalsConnected) {
    mStateChangeListener = mTransportHandler->GetStateChange().Connect(
        mStsThread, this, &MediaPipeline::RtpStateChange);
    mRtcpStateChangeListener = mTransportHandler->GetRtcpStateChange().Connect(
        mStsThread, this, &MediaPipeline::RtcpStateChange);
    // Probably want to only conditionally register this
    mEncryptedSendingListener =
        mTransportHandler->GetEncryptedSending().Connect(
            mStsThread, this, &MediaPipeline::EncryptedPacketSending);
    mRtpPacketReceivedListener =
        mTransportHandler->GetRtpPacketReceived().Connect(
            mStsThread, this, &MediaPipeline::PacketReceived);
    mAlpnNegotiatedListener = mTransportHandler->GetAlpnNegotiated().Connect(
        mStsThread, this, &MediaPipeline::AlpnNegotiated);
    mSignalsConnected = true;
  }

  if (aTransportId != mTransportId) {
    mTransportId = aTransportId;
    mRtpState = mTransportHandler->GetState(mTransportId, false);
    mRtcpState = mTransportHandler->GetState(mTransportId, true);
    CheckTransportStates();
  }

  if (mFilter) {
    for (const auto& extension : mFilter->GetExtmap()) {
      mRtpHeaderExtensionMap->Deregister(extension.uri);
    }
  }
  if (mFilter && aFilter) {
    // Use the new filter, but don't forget any remote SSRCs that we've learned
    // by receiving traffic.
    mFilter->Update(*aFilter, aSignalingStable);
  } else {
    mFilter = std::move(aFilter);
  }
  if (mFilter) {
    for (const auto& extension : mFilter->GetExtmap()) {
      mRtpHeaderExtensionMap->RegisterByUri(extension.id, extension.uri);
    }
  }
}

void MediaPipeline::GetContributingSourceStats(
    const nsString& aInboundRtpStreamId,
    FallibleTArray<dom::RTCRTPContributingSourceStats>& aArr) const {
  ASSERT_ON_THREAD(mStsThread);
  // Get the expiry from now
  DOMHighResTimeStamp expiry =
      RtpCSRCStats::GetExpiryFromTime(GetTimestampMaker().GetNow().ToDom());
  for (const auto& info : mCsrcStats) {
    if (!info.second.Expired(expiry)) {
      RTCRTPContributingSourceStats stats;
      info.second.GetWebidlInstance(stats, aInboundRtpStreamId);
      if (!aArr.AppendElement(stats, fallible)) {
        mozalloc_handle_oom(0);
      }
    }
  }
}

void MediaPipeline::RtpStateChange(const std::string& aTransportId,
                                   TransportLayer::State aState,
                                   const nsTArray<nsTArray<uint8_t>>&,
                                   Maybe<dom::RTCErrorParams>) {
  if (mTransportId != aTransportId) {
    return;
  }
  mRtpState = aState;
  CheckTransportStates();
}

void MediaPipeline::RtcpStateChange(const std::string& aTransportId,
                                    TransportLayer::State aState,
                                    Maybe<dom::RTCErrorParams>) {
  if (mTransportId != aTransportId) {
    return;
  }
  mRtcpState = aState;
  CheckTransportStates();
}

void MediaPipeline::CheckTransportStates() {
  ASSERT_ON_THREAD(mStsThread);

  if (mRtpState == TransportLayer::TS_CLOSED ||
      mRtpState == TransportLayer::TS_ERROR ||
      mRtcpState == TransportLayer::TS_CLOSED ||
      mRtcpState == TransportLayer::TS_ERROR) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Warning,
                "RTP Transport failed for pipeline {} flow {}", fmt::ptr(this),
                mDescription);

    NS_WARNING(
        "MediaPipeline Transport failed. This is not properly cleaned up yet");
    // TODO(ekr@rtfm.com): SECURITY: Figure out how to clean up if the
    // connection was good and now it is bad.
    // TODO(ekr@rtfm.com): Report up so that the PC knows we
    // have experienced an error.
    mConduit->SetTransportActive(false);
    mRtpSendEventListener.DisconnectIfExists();
    mSenderRtcpSendEventListener.DisconnectIfExists();
    mReceiverRtcpSendEventListener.DisconnectIfExists();
    return;
  }

  if (mRtpState == TransportLayer::TS_OPEN) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                "RTP Transport ready for pipeline {} flow {}", fmt::ptr(this),
                mDescription);
  }

  if (mRtcpState == TransportLayer::TS_OPEN) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                "RTCP Transport ready for pipeline {} flow {}", fmt::ptr(this),
                mDescription);
  }

  if (mRtpState == TransportLayer::TS_OPEN && mRtcpState == mRtpState) {
    mConduit->SetTransportActive(true);
    TransportReady_s();
  }
}

void MediaPipeline::SendPacket(MediaPacket&& aPacket) {
  ASSERT_ON_THREAD(mStsThread);

  const bool isRtp = aPacket.type() == MediaPacket::RTP;

  if (isRtp && mRtpState != TransportLayer::TS_OPEN) {
    return;
  }

  if (!isRtp && mRtcpState != TransportLayer::TS_OPEN) {
    return;
  }

  aPacket.sdp_level() = Some(Level());

  if (RtpLogger::IsPacketLoggingOn()) {
    RtpLogger::LogPacket(aPacket, false, mDescription);
  }

  if (isRtp) {
    mPacketDumper->Dump(Level(), dom::mozPacketDumpType::Rtp, true,
                        aPacket.data(), aPacket.len());
    IncrementRtpPacketsSent(aPacket);
  } else {
    mPacketDumper->Dump(Level(), dom::mozPacketDumpType::Rtcp, true,
                        aPacket.data(), aPacket.len());
    IncrementRtcpPacketsSent();
  }

  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug, "{} sending {} packet",
              mDescription, (isRtp ? "RTP" : "RTCP"));

  mTransportHandler->SendPacket(mTransportId, std::move(aPacket));
}

void MediaPipeline::IncrementRtpPacketsSent(const MediaPacket& aPacket) {
  ASSERT_ON_THREAD(mStsThread);
  ++mRtpPacketsSent;
  mRtpBytesSent += aPacket.len();

  if (!(mRtpPacketsSent % 100)) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                "RTP sent packet count for {} Pipeline {}: {} ({} bytes)",
                mDescription, fmt::ptr(this), mRtpPacketsSent, mRtpBytesSent);
  }
}

void MediaPipeline::IncrementRtcpPacketsSent() {
  ASSERT_ON_THREAD(mStsThread);
  ++mRtcpPacketsSent;
  if (!(mRtcpPacketsSent % 100)) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                "RTCP sent packet count for {} Pipeline {}: {}", mDescription,
                fmt::ptr(this), mRtcpPacketsSent);
  }
}

void MediaPipeline::IncrementRtpPacketsReceived(int32_t aBytes) {
  ASSERT_ON_THREAD(mStsThread);
  ++mRtpPacketsReceived;
  mRtpBytesReceived += aBytes;
  if (!(mRtpPacketsReceived % 100)) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                "RTP received packet count for {} Pipeline {}: {} ({} bytes)",
                mDescription, fmt::ptr(this), mRtpPacketsReceived,
                mRtpBytesReceived);
  }
}

void MediaPipeline::PacketReceived(std::string& aTransportId,
                                   MediaPacket& packet) {
  ASSERT_ON_THREAD(mStsThread);

  if (!mActiveSts) {
    return;
  }

  if (mTransportId != aTransportId) {
    return;
  }

  MOZ_ASSERT(mRtpState == TransportLayer::TS_OPEN);

  if (!packet.len() || !packet.data()) {
    return;
  }

  switch (packet.type()) {
    case MediaPacket::RTP:
      RtpPacketReceived(aTransportId, packet);
      break;
    case MediaPacket::RTCP:
      RtcpPacketReceived(aTransportId, packet);
      break;
    default:;
  }
}

void MediaPipeline::RtpPacketReceived(std::string& aTransportId,
                                      MediaPacket& packet) {
  if (mDirection == DirectionType::TRANSMIT) {
    return;
  }

  webrtc::RTPHeader header;

  // It is really, really lame that CopyOnWriteBuffer cannot take ownership of
  // a buffer. Conceivably, we could avoid the copy by using CopyOnWriteBuffer
  // inside MediaPacket, but that would let libwebrtc stuff leak into all parts
  // of our codebase.
  webrtc::CopyOnWriteBuffer packet_buffer({packet.data(), packet.len()});
  webrtc::RtpPacketReceived parsedPacket(mRtpHeaderExtensionMap.get());
  if (!parsedPacket.Parse(packet_buffer)) {
    return;
  }
  parsedPacket.GetHeader(&header);

  if (mFilter && !mFilter->Filter(header)) {
    return;
  }

  auto now = GetTimestampMaker().GetNow();
  parsedPacket.set_arrival_time(now.ToRealtime());
  if (IsVideo()) {
    parsedPacket.set_payload_type_frequency(webrtc::kVideoPayloadTypeFrequency);
  }

  // Remove expired RtpCSRCStats
  if (!mCsrcStats.empty()) {
    auto expiry = RtpCSRCStats::GetExpiryFromTime(now.ToDom());
    for (auto p = mCsrcStats.begin(); p != mCsrcStats.end();) {
      if (p->second.Expired(expiry)) {
        p = mCsrcStats.erase(p);
        continue;
      }
      p++;
    }
  }

  // Add new RtpCSRCStats
  if (header.numCSRCs) {
    for (auto i = 0; i < header.numCSRCs; i++) {
      auto csrcInfo = mCsrcStats.find(header.arrOfCSRCs[i]);
      if (csrcInfo == mCsrcStats.end()) {
        mCsrcStats.insert(
            std::make_pair(header.arrOfCSRCs[i],
                           RtpCSRCStats(header.arrOfCSRCs[i], now.ToDom())));
      } else {
        csrcInfo->second.SetTimestamp(now.ToDom());
      }
    }
  }

  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug, "{} received RTP packet.",
              mDescription);
  IncrementRtpPacketsReceived(packet.len());

  RtpLogger::LogPacket(packet, true, mDescription);

  // Might be nice to pass ownership of the buffer in this case, but it is a
  // small optimization in a rare case.
  mPacketDumper->Dump(mLevel, dom::mozPacketDumpType::Srtp, false,
                      packet.encrypted_data(), packet.encrypted_len());

  mPacketDumper->Dump(mLevel, dom::mozPacketDumpType::Rtp, false, packet.data(),
                      packet.len());

  mRtpReceiveEvent.Notify(std::move(parsedPacket), header);
}

void MediaPipeline::RtcpPacketReceived(std::string& aTransportId,
                                       MediaPacket& aPacket) {
  // The first MediaPipeline to get this notification handles the packet, all
  // others will see an empty packet and ignore it. It does not matter whether
  // the pipeline is transmit or receive, or which m-section it is associated
  // with.
  MediaPacket packet(std::move(aPacket));

  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug, "{} received RTCP packet.",
              mDescription);

  RtpLogger::LogPacket(packet, true, mDescription);

  // Might be nice to pass ownership of the buffer in this case, but it is a
  // small optimization in a rare case.
  mPacketDumper->Dump(SIZE_MAX, dom::mozPacketDumpType::Srtcp, false,
                      packet.encrypted_data(), packet.encrypted_len());

  mPacketDumper->Dump(SIZE_MAX, dom::mozPacketDumpType::Rtcp, false,
                      packet.data(), packet.len());

  if (StaticPrefs::media_webrtc_net_force_disable_rtcp_reception()) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                "{} RTCP packet forced to be dropped", mDescription);
    return;
  }

  // CopyOnWriteBuffer cannot take ownership of an existing buffer. Sadface.
  // But, this is RTCP, so the packets are relatively small and infrequent.
  mRtcpReceiveEvent.Notify(
      webrtc::CopyOnWriteBuffer({packet.data(), packet.len()}));
}

void MediaPipeline::AlpnNegotiated(const std::string& aAlpn,
                                   bool aPrivacyRequested) {
  ASSERT_ON_THREAD(mStsThread);

  if (aPrivacyRequested && Direction() == DirectionType::RECEIVE) {
    // This will force the receive pipelines to drop data until they have
    // received a private PrincipalHandle from RTCRtpReceiver (which takes a
    // detour via main thread).
    static_cast<MediaPipelineReceive*>(this)->OnPrivacyRequested_s();
  }
}

void MediaPipeline::EncryptedPacketSending(const std::string& aTransportId,
                                           const MediaPacket& aPacket) {
  ASSERT_ON_THREAD(mStsThread);

  if (mTransportId == aTransportId) {
    dom::mozPacketDumpType type;
    if (aPacket.type() == MediaPacket::SRTP) {
      type = dom::mozPacketDumpType::Srtp;
    } else if (aPacket.type() == MediaPacket::SRTCP) {
      type = dom::mozPacketDumpType::Srtcp;
    } else if (aPacket.type() == MediaPacket::DTLS) {
      // TODO(bug 1497936): Implement packet dump for DTLS
      return;
    } else {
      MOZ_ASSERT(false);
      return;
    }
    mPacketDumper->Dump(Level(), type, true, aPacket.data(), aPacket.len());
  }
}

class MediaPipelineTransmit::PipelineListener
    : public DirectMediaTrackListener {
  friend class MediaPipelineTransmit;

 public:
  explicit PipelineListener(RefPtr<MediaSessionConduit> aConduit)
      : mConduit(std::move(aConduit)),
        mActive(false),
        mEnabled(false),
        mDirectConnect(false) {}

  ~PipelineListener() {
    if (mConverter) {
      mConverter->Shutdown();
    }
  }

  void SetActive(bool aActive) {
    mActive = aActive;
    if (mConverter) {
      mConverter->SetActive(aActive);
    }
  }
  void SetEnabled(bool aEnabled) { mEnabled = aEnabled; }

  // These are needed since nested classes don't have access to any particular
  // instance of the parent
  void SetAudioProxy(RefPtr<AudioProxyThread> aProxy) {
    mAudioProcessing = std::move(aProxy);
  }

  void SetVideoFrameConverter(RefPtr<VideoFrameConverter> aConverter) {
    mConverter = std::move(aConverter);
  }

  // Implement MediaTrackListener
  void NotifyQueuedChanges(MediaTrackGraph* aGraph, TrackTime aOffset,
                           const MediaSegment& aQueuedMedia) override;
  void NotifyEnabledStateChanged(MediaTrackGraph* aGraph,
                                 bool aEnabled) override;

  // Implement DirectMediaTrackListener
  void NotifyRealtimeTrackData(MediaTrackGraph* aGraph, TrackTime aOffset,
                               const MediaSegment& aMedia) override;
  void NotifyDirectListenerInstalled(InstallationResult aResult) override;
  void NotifyDirectListenerUninstalled() override;

 private:
  void NewData(const MediaSegment& aMedia, TrackRate aRate = 0);

  const RefPtr<MediaSessionConduit> mConduit;
  RefPtr<AudioProxyThread> mAudioProcessing;
  RefPtr<VideoFrameConverter> mConverter;

  // active is true if there is a transport to send on
  mozilla::Atomic<bool> mActive;
  // enabled is true if the media access control permits sending
  // actual content; when false you get black/silence
  mozilla::Atomic<bool> mEnabled;

  // Written and read on the MediaTrackGraph thread
  bool mDirectConnect;
};

MediaPipelineTransmit::MediaPipelineTransmit(
    const std::string& aPc, RefPtr<MediaTransportHandler> aTransportHandler,
    RefPtr<AbstractThread> aCallThread, RefPtr<nsISerialEventTarget> aStsThread,
    bool aIsVideo, RefPtr<MediaSessionConduit> aConduit)
    : MediaPipeline(aPc, std::move(aTransportHandler), DirectionType::TRANSMIT,
                    std::move(aCallThread), std::move(aStsThread),
                    std::move(aConduit)),
      mWatchManager(this, AbstractThread::MainThread()),
      mIsVideo(aIsVideo),
      mListener(MakeRefPtr<PipelineListener>(mConduit)),
      mDomTrack(nullptr, "MediaPipelineTransmit::mDomTrack"),
      mSendTrackOverride(nullptr, "MediaPipelineTransmit::mSendTrackOverride") {
  if (!IsVideo()) {
    mAudioProcessing =
        MakeAndAddRef<AudioProxyThread>(*mConduit->AsAudioSessionConduit());
    mListener->SetAudioProxy(mAudioProcessing);
  }

  mWatchManager.Watch(mActive, &MediaPipeline::UpdateActive);
  mWatchManager.Watch(mActive, &MediaPipelineTransmit::UpdateSendState);
  mWatchManager.Watch(mDomTrack, &MediaPipelineTransmit::UpdateSendState);
  mWatchManager.Watch(mSendTrackOverride,
                      &MediaPipelineTransmit::UpdateSendState);

  mDescription = GenerateDescription();
}

void MediaPipelineTransmit::RegisterListener() {
  if (!IsVideo()) {
    return;
  }
  RefPtr videoConduit = *mConduit->AsVideoSessionConduit();
  mConverter = VideoFrameConverter::Create(
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER),
                        "VideoFrameConverter")
          .forget(),
      GetTimestampMaker(), videoConduit->LockScaling());
  mConverter->SetIdleFrameDuplicationInterval(TimeDuration::FromSeconds(1));
  videoConduit->SetTrackSource(mConverter);
  mListener->SetVideoFrameConverter(mConverter);
}

already_AddRefed<MediaPipelineTransmit> MediaPipelineTransmit::Create(
    const std::string& aPc, RefPtr<MediaTransportHandler> aTransportHandler,
    RefPtr<AbstractThread> aCallThread, RefPtr<nsISerialEventTarget> aStsThread,
    bool aIsVideo, RefPtr<MediaSessionConduit> aConduit) {
  RefPtr<MediaPipelineTransmit> transmit = new MediaPipelineTransmit(
      aPc, std::move(aTransportHandler), std::move(aCallThread),
      std::move(aStsThread), aIsVideo, std::move(aConduit));

  transmit->RegisterListener();

  return transmit.forget();
}

MediaPipelineTransmit::~MediaPipelineTransmit() {
  mFrameListener.DisconnectIfExists();

  MOZ_ASSERT(!mTransmitting);
  MOZ_ASSERT(!mDomTrack.Ref());
}

void MediaPipelineTransmit::InitControl(
    MediaPipelineTransmitControlInterface* aControl) {
  aControl->CanonicalTransmitting().ConnectMirror(&mActive);
}

void MediaPipelineTransmit::Shutdown() {
  MediaPipeline::Shutdown();
  mWatchManager.Shutdown();
  if (mDomTrack.Ref()) {
    mDomTrack.Ref()->RemovePrincipalChangeObserver(this);
    mDomTrack = nullptr;
  }
  mUnsettingSendTrack = false;
  UpdateSendState();
  MOZ_ASSERT(!mTransmitting);
}

void MediaPipeline::SetDescription_s(const std::string& description) {
  ASSERT_ON_THREAD(mStsThread);
  mDescription = description;
}

std::string MediaPipelineTransmit::GenerateDescription() const {
  MOZ_ASSERT(NS_IsMainThread());

  std::stringstream description;
  description << mPc << "| ";
  description << (mIsVideo ? "Transmit video[" : "Transmit audio[");

  if (mDomTrack.Ref()) {
    nsString nsTrackId;
    mDomTrack.Ref()->GetId(nsTrackId);
    description << NS_ConvertUTF16toUTF8(nsTrackId).get();
  } else if (mSendTrackOverride.Ref()) {
    description << "override " << mSendTrackOverride.Ref().get();
  } else {
    description << "no track";
  }

  description << "]";

  return description.str();
}

void MediaPipeline::UpdateActive() {
  MOZ_ASSERT(NS_IsMainThread());
  mStsThread->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr(this), active = mActive.Ref()] {
        mActiveSts = active;
      }));
}

void MediaPipelineTransmit::UpdateSendState() {
  MOZ_ASSERT(NS_IsMainThread());

  // This runs because either mActive, mDomTrack or mSendTrackOverride changed,
  // or because mSendTrack was unset async. Based on these inputs this method
  // is responsible for hooking up mSendTrack to mListener in order to feed data
  // to the conduit.
  //
  // If we are inactive, or if the send track does not match what we want to
  // send (mDomTrack or mSendTrackOverride), we must stop feeding data to the
  // conduit. NB that removing the listener from mSendTrack is async, and we
  // must wait for it to resolve before adding mListener to another track.
  // mUnsettingSendTrack gates us until the listener has been removed from
  // mSendTrack.
  //
  // If we are active and the send track does match what we want to send, we
  // make sure mListener is added to the send track. Either now, or if we're
  // still waiting for another send track to be removed, during a future call to
  // this method.

  if (mUnsettingSendTrack) {
    // We must wait for the send track to be unset before we can set it again,
    // to avoid races. Once unset this function is triggered again.
    return;
  }

  const bool wasTransmitting = mTransmitting;

  const bool haveLiveSendTrack = mSendTrack && !mSendTrack->IsDestroyed();
  const bool haveLiveDomTrack = mDomTrack.Ref() && !mDomTrack.Ref()->Ended();
  const bool haveLiveOverrideTrack =
      mSendTrackOverride.Ref() && !mSendTrackOverride.Ref()->IsDestroyed();
  const bool mustRemoveSendTrack =
      haveLiveSendTrack && !mSendTrackOverride.Ref() &&
      (!haveLiveDomTrack || mDomTrack.Ref()->GetTrack() != mSendPortSource);

  mTransmitting = mActive && (haveLiveDomTrack || haveLiveOverrideTrack) &&
                  !mustRemoveSendTrack;

  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
              "MediaPipeline {} UpdateSendState wasTransmitting={}, active={}, "
              "sendTrack={} ({}), domTrack={} ({}), "
              "sendTrackOverride={} ({}), mustRemove={}, mTransmitting={}",
              fmt::ptr(this), wasTransmitting, mActive.Ref(),
              fmt::ptr(mSendTrack.get()), haveLiveSendTrack ? "live" : "ended",
              fmt::ptr(mDomTrack.Ref().get()),
              haveLiveDomTrack ? "live" : "ended",
              fmt::ptr(mSendTrackOverride.Ref().get()),
              haveLiveOverrideTrack ? "live" : "ended", mustRemoveSendTrack,
              mTransmitting);

  if (!wasTransmitting && mTransmitting) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                "Attaching pipeline {} to track {} conduit type={}",
                fmt::ptr(this), fmt::ptr(mDomTrack.Ref().get()),
                mIsVideo ? "video" : "audio");
    if (mDescriptionInvalidated) {
      // Only update the description when we attach to a track, as detaching is
      // always a longer async step than updating the description. Updating on
      // detach would cause the wrong track id to be attributed in logs.
      RUN_ON_THREAD(mStsThread,
                    WrapRunnable(RefPtr<MediaPipeline>(this),
                                 &MediaPipelineTransmit::SetDescription_s,
                                 GenerateDescription()),
                    NS_DISPATCH_NORMAL);
      mDescriptionInvalidated = false;
    }
    if (mSendTrackOverride.Ref()) {
      // Special path that allows unittests to avoid mDomTrack and the graph by
      // manually calling SetSendTrack.
      mSendTrack = mSendTrackOverride.Ref();
    } else {
      mSendTrack = mDomTrack.Ref()->Graph()->CreateForwardedInputTrack(
          mDomTrack.Ref()->GetTrack()->mType);
      mSendPortSource = mDomTrack.Ref()->GetTrack();
      mSendPort = mSendTrack->AllocateInputPort(mSendPortSource.get());
    }
    if (mIsVideo) {
      mConverter->SetTrackingId(mDomTrack.Ref()->GetSource().mTrackingId);
    }
    mSendTrack->QueueSetAutoend(false);
    if (mIsVideo) {
      mSendTrack->AddDirectListener(mListener);
    }
    mSendTrack->AddListener(mListener);
  }

  if (wasTransmitting && !mTransmitting) {
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                "Detaching pipeline {} from track {} conduit type={}",
                fmt::ptr(this), fmt::ptr(mDomTrack.Ref().get()),
                mIsVideo ? "video" : "audio");
    mUnsettingSendTrack = true;
    if (mIsVideo) {
      mSendTrack->RemoveDirectListener(mListener);
    }
    mSendTrack->RemoveListener(mListener)->Then(
        AbstractThread::MainThread(), __func__,
        [this, self = RefPtr<MediaPipelineTransmit>(this)] {
          mUnsettingSendTrack = false;
          mSendTrack = nullptr;
          if (!mWatchManager.IsShutdown()) {
            mWatchManager.ManualNotify(&MediaPipelineTransmit::UpdateSendState);
          }
        });
    if (!mSendTrackOverride.Ref()) {
      // If an override is set it may be re-used.
      mSendTrack->Destroy();
      mSendPort->Destroy();
      mSendPort = nullptr;
      mSendPortSource = nullptr;
    }
  }
}

bool MediaPipelineTransmit::Transmitting() const {
  MOZ_ASSERT(NS_IsMainThread());

  return mActive;
}

bool MediaPipelineTransmit::IsVideo() const { return mIsVideo; }

void MediaPipelineTransmit::PrincipalChanged(dom::MediaStreamTrack* aTrack) {
  MOZ_ASSERT(aTrack && aTrack == mDomTrack.Ref());

  PeerConnectionWrapper pcw(mPc);
  if (pcw.impl()) {
    Document* doc = pcw.impl()->GetParentObject()->GetExtantDoc();
    if (doc) {
      UpdateSinkIdentity(doc->NodePrincipal(), pcw.impl()->GetPeerIdentity());
    } else {
      MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
                  "Can't update sink principal; document gone");
    }
  }
}

void MediaPipelineTransmit::UpdateSinkIdentity(
    nsIPrincipal* aPrincipal, const PeerIdentity* aSinkIdentity) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mDomTrack.Ref()) {
    // Nothing to do here
    return;
  }

  bool enableTrack = aPrincipal->Subsumes(mDomTrack.Ref()->GetPrincipal());
  if (!enableTrack) {
    // first try didn't work, but there's a chance that this is still available
    // if our track is bound to a peerIdentity, and the peer connection (our
    // sink) is bound to the same identity, then we can enable the track.
    const PeerIdentity* trackIdentity = mDomTrack.Ref()->GetPeerIdentity();
    if (aSinkIdentity && trackIdentity) {
      enableTrack = (*aSinkIdentity == *trackIdentity);
    }
  }

  mListener->SetEnabled(enableTrack);
}

void MediaPipelineTransmit::TransportReady_s() {
  ASSERT_ON_THREAD(mStsThread);
  // Call base ready function.
  MediaPipeline::TransportReady_s();
  mListener->SetActive(true);
}

nsresult MediaPipelineTransmit::SetTrack(
    const RefPtr<MediaStreamTrack>& aDomTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDomTrack.Ref()) {
    mDomTrack.Ref()->RemovePrincipalChangeObserver(this);
  }

  if (aDomTrack) {
    nsString nsTrackId;
    aDomTrack->GetId(nsTrackId);
    MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                "Reattaching pipeline to track {} track {} conduit type: {}",
                fmt::ptr(aDomTrack.get()),
                NS_ConvertUTF16toUTF8(nsTrackId).get(),
                mIsVideo ? "video" : "audio");
  }

  mDescriptionInvalidated = true;
  mDomTrack = aDomTrack;
  if (mDomTrack.Ref()) {
    mDomTrack.Ref()->AddPrincipalChangeObserver(this);
    PrincipalChanged(mDomTrack.Ref());
  }

  return NS_OK;
}

RefPtr<dom::MediaStreamTrack> MediaPipelineTransmit::GetTrack() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDomTrack;
}

void MediaPipelineTransmit::SetSendTrackOverride(
    const RefPtr<ProcessedMediaTrack>& aSendTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(!mSendTrack);
  MOZ_RELEASE_ASSERT(!mSendPort);
  MOZ_RELEASE_ASSERT(!mSendTrackOverride.Ref());
  mDescriptionInvalidated = true;
  mSendTrackOverride = aSendTrack;
}

// Called if we're attached with AddDirectListener()
void MediaPipelineTransmit::PipelineListener::NotifyRealtimeTrackData(
    MediaTrackGraph* aGraph, TrackTime aOffset, const MediaSegment& aMedia) {
  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
              "MediaPipeline::NotifyRealtimeTrackData() listener={}, "
              "offset={}, duration={}",
              fmt::ptr(this), aOffset, aMedia.GetDuration());
  TRACE_COMMENT(
      "MediaPipelineTransmit::PipelineListener::NotifyRealtimeTrackData", "%s",
      aMedia.GetType() == MediaSegment::VIDEO ? "Video" : "Audio");
  NewData(aMedia, aGraph->GraphRate());
}

void MediaPipelineTransmit::PipelineListener::NotifyQueuedChanges(
    MediaTrackGraph* aGraph, TrackTime aOffset,
    const MediaSegment& aQueuedMedia) {
  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
              "MediaPipeline::NotifyQueuedChanges()");

  if (aQueuedMedia.GetType() == MediaSegment::VIDEO) {
    // We always get video from the direct listener.
    return;
  }

  TRACE("MediaPipelineTransmit::PipelineListener::NotifyQueuedChanges (Audio)");

  if (mDirectConnect) {
    // ignore non-direct data if we're also getting direct data
    return;
  }

  size_t rate;
  if (aGraph) {
    rate = aGraph->GraphRate();
  } else {
    // When running tests, graph may be null. In that case use a default.
    rate = 16000;
  }
  NewData(aQueuedMedia, rate);
}

void MediaPipelineTransmit::PipelineListener::NotifyEnabledStateChanged(
    MediaTrackGraph* aGraph, bool aEnabled) {
  if (mConduit->type() != MediaSessionConduit::VIDEO) {
    return;
  }
  MOZ_ASSERT(mConverter);
  mConverter->SetTrackEnabled(aEnabled);
}

void MediaPipelineTransmit::PipelineListener::NotifyDirectListenerInstalled(
    InstallationResult aResult) {
  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
              "MediaPipeline::NotifyDirectListenerInstalled() listener={},"
              " result={}",
              fmt::ptr(this), static_cast<int32_t>(aResult));

  mDirectConnect = InstallationResult::SUCCESS == aResult;
}

void MediaPipelineTransmit::PipelineListener::
    NotifyDirectListenerUninstalled() {
  MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Info,
              "MediaPipeline::NotifyDirectListenerUninstalled() listener={}",
              fmt::ptr(this));

  if (mConduit->type() == MediaSessionConduit::VIDEO) {
    // Reset the converter's track-enabled state. If re-added to a new track
    // later and that track is disabled, we will be signaled explicitly.
    MOZ_ASSERT(mConverter);
    mConverter->SetTrackEnabled(true);
  }

  mDirectConnect = false;
}

void MediaPipelineTransmit::PipelineListener::NewData(
    const MediaSegment& aMedia, TrackRate aRate /* = 0 */) {
  if (mConduit->type() != (aMedia.GetType() == MediaSegment::AUDIO
                               ? MediaSessionConduit::AUDIO
                               : MediaSessionConduit::VIDEO)) {
    MOZ_ASSERT(false,
               "The media type should always be correct since the "
               "listener is locked to a specific track");
    return;
  }

  // TODO(ekr@rtfm.com): For now assume that we have only one
  // track type and it's destined for us
  // See bug 784517
  if (aMedia.GetType() == MediaSegment::AUDIO) {
    MOZ_RELEASE_ASSERT(aRate > 0);

    if (!mActive) {
      MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                  "Discarding audio packets because transport not ready");
      return;
    }

    const AudioSegment* audio = static_cast<const AudioSegment*>(&aMedia);
    for (AudioSegment::ConstChunkIterator iter(*audio); !iter.IsEnded();
         iter.Next()) {
      mAudioProcessing->QueueAudioChunk(aRate, *iter, mEnabled);
    }
  } else {
    const VideoSegment* video = static_cast<const VideoSegment*>(&aMedia);

    for (VideoSegment::ConstChunkIterator iter(*video); !iter.IsEnded();
         iter.Next()) {
      mConverter->QueueVideoChunk(*iter, !mEnabled);
    }
  }
}

class GenericReceiveListener : public MediaTrackListener {
 public:
  GenericReceiveListener(RefPtr<SourceMediaTrack> aSource,
                         TrackingId aTrackingId)
      : mSource(std::move(aSource)),
        mTrackingId(std::move(aTrackingId)),
        mIsAudio(mSource->mType == MediaSegment::AUDIO),
        mEnabled(false) {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(mSource, "Must be used with a SourceMediaTrack");
  }

  virtual ~GenericReceiveListener() = default;

  void Init() { mSource->AddListener(this); }
  void Shutdown() { mSource->RemoveListener(this); }

  void SetEnabled(bool aEnabled) {
    if (mEnabled == aEnabled) {
      return;
    }
    mEnabled = aEnabled;
    if (mIsAudio && !mSource->IsDestroyed()) {
      mSource->SetPullingEnabled(mEnabled);
    }
  }

 protected:
  const RefPtr<SourceMediaTrack> mSource;
  const TrackingId mTrackingId;
  const bool mIsAudio;
  // Main thread only.
  bool mEnabled;
};

MediaPipelineReceive::MediaPipelineReceive(
    const std::string& aPc, RefPtr<MediaTransportHandler> aTransportHandler,
    RefPtr<AbstractThread> aCallThread, RefPtr<nsISerialEventTarget> aStsThread,
    RefPtr<MediaSessionConduit> aConduit)
    : MediaPipeline(aPc, std::move(aTransportHandler), DirectionType::RECEIVE,
                    std::move(aCallThread), std::move(aStsThread),
                    std::move(aConduit)),
      mWatchManager(this, AbstractThread::MainThread()) {
  mWatchManager.Watch(mActive, &MediaPipeline::UpdateActive);
  mWatchManager.Watch(mActive, &MediaPipelineReceive::UpdateListener);
}

MediaPipelineReceive::~MediaPipelineReceive() = default;

void MediaPipelineReceive::InitControl(
    MediaPipelineReceiveControlInterface* aControl) {
  aControl->CanonicalReceiving().ConnectMirror(&mActive);
}

void MediaPipelineReceive::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MediaPipeline::Shutdown();
  mWatchManager.Shutdown();
}

class MediaPipelineReceiveAudio::PipelineListener
    : public GenericReceiveListener {
 public:
  PipelineListener(RefPtr<SourceMediaTrack> aSource, TrackingId aTrackingId,
                   RefPtr<MediaSessionConduit> aConduit,
                   PrincipalHandle aPrincipalHandle, PrincipalPrivacy aPrivacy)
      : GenericReceiveListener(std::move(aSource), std::move(aTrackingId)),
        mConduit(std::move(aConduit)),
        // AudioSession conduit only supports 16, 32, 44.1 and 48kHz
        // This is an artificial limitation, it would however require more
        // changes to support any rates. If the sampling rate is not-supported,
        // we will use 48kHz instead.
        mRate(static_cast<AudioSessionConduit*>(mConduit.get())
                      ->IsSamplingFreqSupported(mSource->Graph()->GraphRate())
                  ? mSource->Graph()->GraphRate()
                  : WEBRTC_MAX_SAMPLE_RATE),
        mTaskQueue(TaskQueue::Create(
            GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER),
            "AudioPipelineListener")),
        mPlayedTicks(0),
        mAudioFrame(std::make_unique<webrtc::AudioFrame>()),
        mPrincipalHandle(std::move(aPrincipalHandle)),
        mPrivacy(aPrivacy),
        mForceSilence(false) {}

  void Init() {
    GenericReceiveListener::Init();
    mSource->SetAppendDataSourceRate(mRate);
  }

  // Implement MediaTrackListener
  void NotifyPull(MediaTrackGraph* aGraph, TrackTime aEndOfAppendedData,
                  TrackTime aDesiredTime) override {
    NotifyPullImpl(aDesiredTime);
  }

  void OnPrivacyRequested_s() {
    if (mPrivacy == PrincipalPrivacy::Private) {
      return;
    }
    mForceSilence = true;
  }

  void SetPrivatePrincipal(PrincipalHandle aHandle) {
    MOZ_ASSERT(NS_IsMainThread());

    if (mSource->IsDestroyed()) {
      return;
    }

    mSource->QueueControlMessageWithNoShutdown(
        [self = RefPtr{this}, this,
         privatePrincipal = std::move(aHandle)]() mutable {
          if (mPrivacy == PrincipalPrivacy::Private) {
            return;
          }
          mPrincipalHandle = std::move(privatePrincipal);
          mPrivacy = PrincipalPrivacy::Private;
          mForceSilence = false;
        });
  }

 private:
  ~PipelineListener() = default;

  void NotifyPullImpl(TrackTime aDesiredTime) {
    TRACE_COMMENT("PiplineListener::NotifyPullImpl", "PipelineListener %p",
                  this);
    uint32_t samplesPer10ms = mRate / 100;

    // mSource's rate is not necessarily the same as the graph rate, since there
    // are sample-rate constraints on the inbound audio: only 16, 32, 44.1 and
    // 48kHz are supported. The audio frames we get here is going to be
    // resampled when inserted into the graph. aDesiredTime and mPlayedTicks are
    // in the graph rate.

    while (mPlayedTicks < aDesiredTime) {
      // This fetches 10ms of data, either mono or stereo
      MediaConduitErrorCode err =
          static_cast<AudioSessionConduit*>(mConduit.get())
              ->GetAudioFrame(mRate, mAudioFrame.get());

      if (err != kMediaConduitNoError) {
        // Insert silence on conduit/GIPS failure (extremely unlikely)
        MOZ_LOG_FMT(
            gMediaPipelineLog, LogLevel::Error,
            "Audio conduit failed ({}) to return data @ {} (desired {} -> {})",
            static_cast<int>(err), mPlayedTicks, aDesiredTime,
            mSource->TrackTimeToSeconds(aDesiredTime));
        constexpr size_t mono = 1;
        mAudioFrame->UpdateFrame(
            mAudioFrame->timestamp_, nullptr, samplesPer10ms, mRate,
            mAudioFrame->speech_type_, mAudioFrame->vad_activity_,
            std::max(mono, mAudioFrame->num_channels()));
      }

      MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Debug,
                  "Audio conduit returned buffer for {} channels, {} frames",
                  mAudioFrame->num_channels(),
                  mAudioFrame->samples_per_channel());

      AudioSegment segment;
      if (mForceSilence || mAudioFrame->muted()) {
        segment.AppendNullData(mAudioFrame->samples_per_channel());
      } else {
        CheckedInt<size_t> bufferSize(sizeof(uint16_t));
        bufferSize *= mAudioFrame->samples_per_channel();
        bufferSize *= mAudioFrame->num_channels();
        RefPtr<SharedBuffer> samples = SharedBuffer::Create(bufferSize);
        int16_t* samplesData = static_cast<int16_t*>(samples->Data());
        AutoTArray<int16_t*, 2> channels;
        AutoTArray<const int16_t*, 2> outputChannels;

        channels.SetLength(mAudioFrame->num_channels());

        size_t offset = 0;
        for (size_t i = 0; i < mAudioFrame->num_channels(); i++) {
          channels[i] = samplesData + offset;
          offset += mAudioFrame->samples_per_channel();
        }

        DeinterleaveAndConvertBuffer(
            mAudioFrame->data(), mAudioFrame->samples_per_channel(),
            mAudioFrame->num_channels(), channels.Elements());

        outputChannels.AppendElements(channels);

        segment.AppendFrames(samples.forget(), outputChannels,
                             mAudioFrame->samples_per_channel(),
                             mPrincipalHandle);
      }

      // Handle track not actually added yet or removed/finished
      if (TrackTime appended = mSource->AppendData(&segment)) {
        mPlayedTicks += appended;
      } else {
        MOZ_LOG_FMT(gMediaPipelineLog, LogLevel::Error, "AppendData failed");
        // we can't un-read the data, but that's ok since we don't want to
        // buffer - but don't i-loop!
        break;
      }
    }
  }

  const RefPtr<MediaSessionConduit> mConduit;
  // This conduit's sampling rate. This is either 16, 32, 44.1 or 48kHz, and
  // tries to be the same as the graph rate. If the graph rate is higher than
  // 48kHz, mRate is capped to 48kHz. If mRate does not match the graph rate,
  // audio is resampled to the graph rate.
  const TrackRate mRate;
  const RefPtr<TaskQueue> mTaskQueue;
  // Number of frames of data that has been added to the SourceMediaTrack in
  // the graph's rate. Graph thread only.
  TrackTicks mPlayedTicks;
  // Allocation of an audio frame used as a scratch buffer when reading data out
  // of libwebrtc for forwarding into the graph. Graph thread only.
  std::unique_ptr<webrtc::AudioFrame> mAudioFrame;
  // Principal handle used when appending data to the SourceMediaTrack. Graph
  // thread only.
  PrincipalHandle mPrincipalHandle;
  // Privacy of mPrincipalHandle. Graph thread only.
  PrincipalPrivacy mPrivacy;
  // Set to true on the sts thread if privacy is requested when ALPN was
  // negotiated. Set to false again when mPrincipalHandle is private.
  Atomic<bool> mForceSilence;
};

MediaPipelineReceiveAudio::MediaPipelineReceiveAudio(
    const std::string& aPc, RefPtr<MediaTransportHandler> aTransportHandler,
    RefPtr<AbstractThread> aCallThread, RefPtr<nsISerialEventTarget> aStsThread,
    RefPtr<AudioSessionConduit> aConduit, RefPtr<SourceMediaTrack> aSource,
    TrackingId aTrackingId, PrincipalHandle aPrincipalHandle,
    PrincipalPrivacy aPrivacy)
    : MediaPipelineReceive(aPc, std::move(aTransportHandler),
                           std::move(aCallThread), std::move(aStsThread),
                           std::move(aConduit)),
      mListener(aSource ? MakeRefPtr<PipelineListener>(
                              std::move(aSource), std::move(aTrackingId),
                              mConduit, std::move(aPrincipalHandle), aPrivacy)
                        : nullptr) {
  mDescription = mPc + "| Receive audio";
  if (mListener) {
    mListener->Init();
  }
}

void MediaPipelineReceiveAudio::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MediaPipelineReceive::Shutdown();
  if (mListener) {
    mListener->Shutdown();
  }
}

void MediaPipelineReceiveAudio::OnPrivacyRequested_s() {
  ASSERT_ON_THREAD(mStsThread);
  if (mListener) {
    mListener->OnPrivacyRequested_s();
  }
}

void MediaPipelineReceiveAudio::SetPrivatePrincipal(PrincipalHandle aHandle) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mListener) {
    mListener->SetPrivatePrincipal(std::move(aHandle));
  }
}

void MediaPipelineReceiveAudio::UpdateListener() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mListener) {
    mListener->SetEnabled(mActive.Ref());
  }
}

static VideoRotation FromWebrtcVideoRotation(webrtc::VideoRotation aRotation) {
  switch (aRotation) {
    case webrtc::kVideoRotation_0:
      return VideoRotation::kDegree_0;
    case webrtc::kVideoRotation_90:
      return VideoRotation::kDegree_90;
    case webrtc::kVideoRotation_180:
      return VideoRotation::kDegree_180;
    case webrtc::kVideoRotation_270:
      return VideoRotation::kDegree_270;
  }
  MOZ_ASSERT_UNREACHABLE("Bad webrtc::VideoRotation value");
  return VideoRotation::kDegree_0;
}

class MediaPipelineReceiveVideo::PipelineListener
    : public GenericReceiveListener {
 public:
  PipelineListener(RefPtr<SourceMediaTrack> aSource, TrackingId aTrackingId,
                   PrincipalHandle aPrincipalHandle, PrincipalPrivacy aPrivacy)
      : GenericReceiveListener(std::move(aSource), std::move(aTrackingId)),
        mImageContainer(MakeAndAddRef<ImageContainer>(
            ImageUsageType::Webrtc, ImageContainer::ASYNCHRONOUS)),
        mMutex("MediaPipelineReceiveVideo::PipelineListener::mMutex"),
        mPrincipalHandle(std::move(aPrincipalHandle)),
        mPrivacy(aPrivacy) {}
  void OnPrivacyRequested_s() {
    MutexAutoLock lock(mMutex);
    if (mPrivacy == PrincipalPrivacy::Private) {
      return;
    }
    mForceDropFrames = true;
  }

  void SetPrivatePrincipal(PrincipalHandle aHandle) {
    MutexAutoLock lock(mMutex);
    if (mPrivacy == PrincipalPrivacy::Private) {
      return;
    }
    mPrincipalHandle = std::move(aHandle);
    mPrivacy = PrincipalPrivacy::Private;
    mForceDropFrames = false;
  }

  void RenderVideoFrame(const webrtc::VideoFrame& aVideoFrame) {
    PrincipalHandle principal;
    {
      MutexAutoLock lock(mMutex);
      if (mForceDropFrames) {
        return;
      }
      principal = mPrincipalHandle;
    }
    RefPtr<Image> image;
    const webrtc::VideoFrameBuffer& buffer = *aVideoFrame.video_frame_buffer();
    if (buffer.type() == webrtc::VideoFrameBuffer::Type::kNative) {
      // We assume that only native handles are used with the
      // WebrtcMediaDataCodec decoder.
      const ImageBuffer* imageBuffer = static_cast<const ImageBuffer*>(&buffer);
      image = imageBuffer->GetNativeImage();
    } else {
      MOZ_ASSERT(buffer.type() == webrtc::VideoFrameBuffer::Type::kI420);
      webrtc::scoped_refptr<const webrtc::I420BufferInterface> i420(
          buffer.GetI420());

      MOZ_ASSERT(i420->DataY());
      // Create a video frame using |buffer|.
      PerformanceRecorder<CopyVideoStage> rec(
          "MediaPipelineReceiveVideo::CopyToImage"_ns, mTrackingId,
          i420->width(), i420->height());

      RefPtr<PlanarYCbCrImage> yuvImage =
          mImageContainer->CreatePlanarYCbCrImage();

      PlanarYCbCrData yuvData;
      yuvData.mYChannel = const_cast<uint8_t*>(i420->DataY());
      yuvData.mYStride = i420->StrideY();
      MOZ_ASSERT(i420->StrideU() == i420->StrideV());
      yuvData.mCbCrStride = i420->StrideU();
      yuvData.mCbChannel = const_cast<uint8_t*>(i420->DataU());
      yuvData.mCrChannel = const_cast<uint8_t*>(i420->DataV());
      yuvData.mPictureRect = IntRect(0, 0, i420->width(), i420->height());
      yuvData.mStereoMode = StereoMode::MONO;
      // The color space the stream declares, which libwebrtc reads from an AV1
      // sequence header or a VP9 frame header (or the RTP color space
      // extension, which we don't negotiate); BT.601 limited where it declares
      // none, as VP8 is by definition.
      yuvData.mYUVColorSpace = gfx::YUVColorSpace::BT601;
      if (const auto& colorSpace = aVideoFrame.color_space()) {
        const auto primaries = ToCicp(colorSpace->primaries());
        yuvData.mYUVColorSpace =
            gfxUtils::CicpToColorSpace(ToCicp(colorSpace->matrix()), primaries,
                                       gMediaPipelineLog)
                .valueOr(yuvData.mYUVColorSpace);
        yuvData.mColorPrimaries =
            gfxUtils::CicpToColorPrimaries(primaries, gMediaPipelineLog)
                .valueOr(yuvData.mColorPrimaries);
        yuvData.mTransferFunction =
            gfxUtils::CicpToTransferFunction(ToCicp(colorSpace->transfer()))
                .valueOr(yuvData.mTransferFunction);
        yuvData.mColorRange = ToColorRange(colorSpace->range());
      }
      yuvData.mChromaSubsampling =
          gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

      if (NS_FAILED(yuvImage->CopyData(yuvData))) {
        MOZ_ASSERT(false);
        return;
      }
      rec.Record();

      image = std::move(yuvImage);
    }

    Maybe<webrtc::Timestamp> receiveTime;
    for (const auto& packet : aVideoFrame.packet_infos()) {
      if (!receiveTime || *receiveTime < packet.receive_time()) {
        receiveTime = Some(packet.receive_time());
      }
    }

    VideoSegment segment;
    VideoRotation rotation = FromWebrtcVideoRotation(aVideoFrame.rotation());
    auto size = image->GetSize();
    auto processingDuration =
        aVideoFrame.processing_time()
            ? media::TimeUnit::FromMicroseconds(
                  aVideoFrame.processing_time()->Elapsed().us())
            : media::TimeUnit::Invalid();
    segment.AppendWebrtcRemoteFrame(
        image.forget(), size, principal,
        /* aForceBlack */ false, TimeStamp::Now(), processingDuration,
        aVideoFrame.rtp_timestamp(), aVideoFrame.ntp_time_ms(),
        receiveTime ? receiveTime->us() : 0, rotation);
    mSource->AppendData(&segment);
  }

 private:
  RefPtr<layers::ImageContainer> mImageContainer;
  Mutex mMutex;
  PrincipalHandle mPrincipalHandle MOZ_GUARDED_BY(mMutex);
  PrincipalPrivacy mPrivacy MOZ_GUARDED_BY(mMutex);
  // Set to true on the sts thread if privacy is requested when ALPN was
  // negotiated. Set to false again when mPrincipalHandle is private.
  bool mForceDropFrames MOZ_GUARDED_BY(mMutex) = false;
};

class MediaPipelineReceiveVideo::PipelineRenderer
    : public mozilla::VideoRenderer {
 public:
  explicit PipelineRenderer(MediaPipelineReceiveVideo* aPipeline)
      : mPipeline(aPipeline) {}

  void Detach() { mPipeline = nullptr; }

  // Implement VideoRenderer
  void RenderVideoFrame(const webrtc::VideoFrame& aVideoFrame) override {
    mPipeline->mListener->RenderVideoFrame(aVideoFrame);
  }

 private:
  MediaPipelineReceiveVideo* mPipeline;  // Raw pointer to avoid cycles
};

MediaPipelineReceiveVideo::MediaPipelineReceiveVideo(
    const std::string& aPc, RefPtr<MediaTransportHandler> aTransportHandler,
    RefPtr<AbstractThread> aCallThread, RefPtr<nsISerialEventTarget> aStsThread,
    RefPtr<VideoSessionConduit> aConduit, RefPtr<SourceMediaTrack> aSource,
    TrackingId aTrackingId, PrincipalHandle aPrincipalHandle,
    PrincipalPrivacy aPrivacy)
    : MediaPipelineReceive(aPc, std::move(aTransportHandler),
                           std::move(aCallThread), std::move(aStsThread),
                           std::move(aConduit)),
      mRenderer(MakeRefPtr<PipelineRenderer>(this)),
      mListener(aSource ? MakeRefPtr<PipelineListener>(
                              std::move(aSource), std::move(aTrackingId),
                              std::move(aPrincipalHandle), aPrivacy)
                        : nullptr) {
  mDescription = mPc + "| Receive video";
  if (mListener) {
    mListener->Init();
  }
  static_cast<VideoSessionConduit*>(mConduit.get())->AttachRenderer(mRenderer);
}

void MediaPipelineReceiveVideo::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MediaPipelineReceive::Shutdown();
  if (mListener) {
    mListener->Shutdown();
  }

  // stop generating video and thus stop invoking the PipelineRenderer
  // and PipelineListener - the renderer has a raw ptr to the Pipeline to
  // avoid cycles, and the render callbacks are invoked from a different
  // thread so simple null-checks would cause TSAN bugs without locks.
  static_cast<VideoSessionConduit*>(mConduit.get())->DetachRenderer();
}

void MediaPipelineReceiveVideo::OnPrivacyRequested_s() {
  ASSERT_ON_THREAD(mStsThread);
  if (mListener) {
    mListener->OnPrivacyRequested_s();
  }
}

void MediaPipelineReceiveVideo::SetPrivatePrincipal(PrincipalHandle aHandle) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mListener) {
    mListener->SetPrivatePrincipal(std::move(aHandle));
  }
}

void MediaPipelineReceiveVideo::UpdateListener() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mListener) {
    mListener->SetEnabled(mActive.Ref());
  }
}

const dom::RTCStatsTimestampMaker& MediaPipeline::GetTimestampMaker() const {
  return mConduit->GetTimestampMaker();
}

DOMHighResTimeStamp MediaPipeline::RtpCSRCStats::GetExpiryFromTime(
    const DOMHighResTimeStamp aTime) {
  // DOMHighResTimeStamp is a unit measured in ms
  return aTime + EXPIRY_TIME_MILLISECONDS;
}

MediaPipeline::RtpCSRCStats::RtpCSRCStats(const uint32_t aCsrc,
                                          const DOMHighResTimeStamp aTime)
    : mCsrc(aCsrc), mTimestamp(aTime) {}

void MediaPipeline::RtpCSRCStats::GetWebidlInstance(
    dom::RTCRTPContributingSourceStats& aWebidlObj,
    const nsString& aInboundRtpStreamId) const {
  nsString statId = u"csrc_"_ns + aInboundRtpStreamId;
  statId.AppendLiteral("_");
  statId.AppendInt(mCsrc);
  aWebidlObj.mId.Construct(statId);
  aWebidlObj.mType.Construct(RTCStatsType::Csrc);
  aWebidlObj.mTimestamp.Construct(mTimestamp);
  aWebidlObj.mContributorSsrc.Construct(mCsrc);
  aWebidlObj.mInboundRtpStreamId.Construct(aInboundRtpStreamId);
}

}  // namespace mozilla
