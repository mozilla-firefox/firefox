/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RTCRtpReceiver.h"

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "MediaSegment.h"
#include "MediaTrackGraph.h"
#include "MediaTransportHandler.h"
#include "PeerConnectionImpl.h"
#include "PerformanceRecorder.h"
#include "PrincipalHandle.h"
#include "RTCRtpTransceiver.h"
#include "RTCStatsReport.h"
#include "RemoteTrackSource.h"
#include "api/rtp_parameters.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/audio_receive_stream.h"
#include "call/call.h"
#include "call/video_receive_stream.h"
#include "js/RootingAPI.h"
#include "jsep/JsepTransceiver.h"
#include "libwebrtcglue/MediaConduitControl.h"
#include "libwebrtcglue/MediaConduitInterface.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/StateWatching.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/AudioStreamTrack.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MediaStreamTrack.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RTCRtpCapabilitiesBinding.h"
#include "mozilla/dom/RTCRtpReceiverBinding.h"
#include "mozilla/dom/RTCRtpScriptTransform.h"
#include "mozilla/dom/RTCRtpSourcesBinding.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/dom/VideoStreamTrack.h"
#include "mozilla/fallible.h"
#include "mozilla/mozalloc_oom.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMNavigationTiming.h"
#include "nsDebug.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsISupports.h"
#include "nsLiteralString.h"
#include "nsPIDOMWindow.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsWrapperCache.h"
#include "sdp/SdpAttribute.h"
#include "sdp/SdpEnum.h"
#include "system_wrappers/include/clock.h"
#include "transportbridge/MediaPipeline.h"

namespace mozilla::dom {

LazyLogModule gReceiverLog("RTCRtpReceiver");

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(RTCRtpReceiver)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RTCRtpReceiver)
  tmp->Unlink();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RTCRtpReceiver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow, mPc, mTransceiver, mTransform,
                                    mTrack, mTrackSource)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(RTCRtpReceiver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RTCRtpReceiver)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RTCRtpReceiver)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

static PrincipalHandle GetPrincipalHandle(nsPIDOMWindowInner* aWindow,
                                          PrincipalPrivacy aPrivacy) {
  // Set the principal used for creating the tracks. This makes the track
  // data (audio/video samples) accessible to the receiving page. We're
  // only certain that privacy hasn't been requested if we're connected.
  nsCOMPtr<nsIScriptObjectPrincipal> winPrincipal = do_QueryInterface(aWindow);
  RefPtr<nsIPrincipal> principal = winPrincipal->GetPrincipal();
  if (NS_WARN_IF(!principal)) {
    principal = NullPrincipal::CreateWithoutOriginAttributes();
  } else if (aPrivacy == PrincipalPrivacy::Private) {
    principal = NullPrincipal::CreateWithInheritedAttributes(principal);
  }
  return MakePrincipalHandle(principal);
}

#define INIT_CANONICAL(name, val)         \
  name(AbstractThread::MainThread(), val, \
       "RTCRtpReceiver::" #name " (Canonical)")
#define INIT_MIRROR(name, val) \
  name(AbstractThread::MainThread(), val, "RTCRtpReceiver::" #name " (Mirror)")

RTCRtpReceiver::RTCRtpReceiver(
    nsPIDOMWindowInner* aWindow, PrincipalPrivacy aPrivacy,
    PeerConnectionImpl* aPc, MediaTransportHandler* aTransportHandler,
    AbstractThread* aCallThread, nsISerialEventTarget* aStsThread,
    MediaSessionConduit* aConduit, RTCRtpTransceiver* aTransceiver,
    const TrackingId& aTrackingId)
    : mWatchManager(this, AbstractThread::MainThread()),
      mWindow(aWindow),
      mPc(aPc),
      mCallThread(aCallThread),
      mStsThread(aStsThread),
      mTransportHandler(aTransportHandler),
      mTransceiver(aTransceiver),
      INIT_CANONICAL(mSsrc, 0),
      INIT_CANONICAL(mVideoRtxSsrc, 0),
      INIT_CANONICAL(mLocalRtpExtensions, RtpExtList()),
      INIT_CANONICAL(mAudioCodecs, std::vector<AudioCodecConfig>()),
      INIT_CANONICAL(mVideoCodecs, std::vector<VideoCodecConfig>()),
      INIT_CANONICAL(mVideoRtpRtcpConfig, Nothing()),
      INIT_CANONICAL(mReceiving, false),
      INIT_CANONICAL(mFrameTransformerProxy, nullptr),
      INIT_MIRROR(mReceivingSize, {}) {
  PrincipalHandle principalHandle = GetPrincipalHandle(aWindow, aPrivacy);
  const bool isAudio = aConduit->type() == MediaSessionConduit::AUDIO;

  // The remote track is not rendered until it is attached to a sink, so the
  // graph does not need to start with an AudioCallbackDriver. CheckDriver()
  // creates one when it is needed.
  MediaTrackGraph* graph = MediaTrackGraph::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, aWindow,
      MediaTrackGraph::REQUEST_DEFAULT_SAMPLE_RATE,
      MediaTrackGraph::DEFAULT_OUTPUT_DEVICE);

  if (isAudio) {
    auto* source = graph->CreateSourceTrack(MediaSegment::AUDIO);
    mTrackSource = MakeAndAddRef<RemoteTrackSource>(
        source, this, principalHandle, u"remote audio"_ns, aTrackingId);
    mTrack = MakeAndAddRef<AudioStreamTrack>(aWindow, source, mTrackSource);
    mPipeline = MakeAndAddRef<MediaPipelineReceiveAudio>(
        mPc->GetHandle(), aTransportHandler, aCallThread, mStsThread.get(),
        *aConduit->AsAudioSessionConduit(), mTrackSource->Stream(), aTrackingId,
        principalHandle, aPrivacy);
  } else {
    auto* source = graph->CreateSourceTrack(MediaSegment::VIDEO);
    mTrackSource = MakeAndAddRef<RemoteTrackSource>(
        source, this, principalHandle, u"remote video"_ns, aTrackingId);
    mTrack = MakeAndAddRef<VideoStreamTrack>(aWindow, source, mTrackSource);
    mPipeline = MakeAndAddRef<MediaPipelineReceiveVideo>(
        mPc->GetHandle(), aTransportHandler, aCallThread, mStsThread.get(),
        *aConduit->AsVideoSessionConduit(), mTrackSource->Stream(), aTrackingId,
        principalHandle, aPrivacy);
    mReceivingSize.Connect(
        aConduit->AsVideoSessionConduit().ref()->CanonicalReceivingSize());
  }

  mPipeline->InitControl(this);

  // Spec says remote tracks start out muted.
  mTrackSource->SetMuted(true);

  // Until Bug 1232234 is fixed, we'll get extra RTCP BYES during renegotiation,
  // so we'll disable muting on RTCP BYE and timeout for now.
  if (Preferences::GetBool("media.peerconnection.mute_on_bye_or_timeout",
                           false)) {
    mRtcpByeListener = aConduit->RtcpByeEvent().Connect(
        GetMainThreadSerialEventTarget(), this, &RTCRtpReceiver::OnRtcpBye);
    mRtcpTimeoutListener = aConduit->RtcpTimeoutEvent().Connect(
        GetMainThreadSerialEventTarget(), this, &RTCRtpReceiver::OnRtcpTimeout);
  }

  mWatchManager.Watch(mReceiveTrackMute,
                      &RTCRtpReceiver::UpdateReceiveTrackMute);

  mParameters.mCodecs.Construct();
  mParameters.mHeaderExtensions.Construct();

  mParameters.mRtcp.Construct();
  // On a receiver, rtcp.cname is left unset; it is a sender-side value.
  // TODO(bug 1765852): We do not support reduced size yet
  mParameters.mRtcp.Value().mReducedSize.Construct(false);
}

#undef INIT_MIRROR
#undef INIT_CANONICAL

RTCRtpReceiver::~RTCRtpReceiver() { MOZ_ASSERT(!mPipeline); }

JSObject* RTCRtpReceiver::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return RTCRtpReceiver_Binding::Wrap(aCx, this, aGivenProto);
}

RTCDtlsTransport* RTCRtpReceiver::GetTransport() const {
  if (!mTransceiver) {
    return nullptr;
  }
  return mTransceiver->GetDtlsTransport();
}

void RTCRtpReceiver::GetCapabilities(
    const GlobalObject&, const nsAString& aKind,
    Nullable<dom::RTCRtpCapabilities>& aResult) {
  PeerConnectionImpl::GetCapabilities(aKind, aResult, sdp::Direction::kRecv);
}

void RTCRtpReceiver::GetParameters(RTCRtpReceiveParameters& aParameters) const {
  aParameters = mParameters;
}

already_AddRefed<Promise> RTCRtpReceiver::GetStats(ErrorResult& aError) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);
  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (NS_WARN_IF(aError.Failed())) {
    return nullptr;
  }

  mTransceiver->ChainToDomPromiseWithCodecStats(GetStatsInternal(), promise);
  return promise.forget();
}

nsTArray<RefPtr<RTCStatsPromise>> RTCRtpReceiver::GetStatsInternal(
    bool aSkipIceStats) {
  MOZ_ASSERT(NS_IsMainThread());
  nsTArray<RefPtr<RTCStatsPromise>> promises(3);

  if (!mPipeline) {
    return promises;
  }

  if (!mHaveStartedReceiving) {
    return promises;
  }

  nsString recvTrackId;
  MOZ_ASSERT(mTrack);
  if (mTrack) {
    mTrack->GetId(recvTrackId);
  }

  std::string mid = mTransceiver->GetMidAscii();
  nsString transportId =
      NS_ConvertASCIItoUTF16(GetJsepTransceiver().mTransport.mTransportId);

  {
    // Add bandwidth estimation stats
    promises.AppendElement(InvokeAsync(
        mCallThread, __func__,
        [conduit = mPipeline->mConduit, recvTrackId]() mutable {
          auto report = std::make_unique<dom::RTCStatsCollection>();
          const Maybe<webrtc::Call::Stats> stats = conduit->GetCallStats();
          stats.apply([&](const auto& aStats) {
            dom::RTCBandwidthEstimationInternal bw;
            bw.mTrackIdentifier = recvTrackId;
            bw.mSendBandwidthBps.Construct(aStats.send_bandwidth_bps / 8);
            bw.mMaxPaddingBps.Construct(aStats.max_padding_bitrate_bps / 8);
            bw.mReceiveBandwidthBps.Construct(aStats.recv_bandwidth_bps / 8);
            bw.mPacerDelayMs.Construct(aStats.pacer_delay_ms);
            if (aStats.rtt_ms >= 0) {
              bw.mRttMs.Construct(aStats.rtt_ms);
            }
            if (!report->mBandwidthEstimations.AppendElement(std::move(bw),
                                                             fallible)) {
              mozalloc_handle_oom(0);
            }
          });
          return RTCStatsPromise::CreateAndResolve(std::move(report), __func__);
        }));
  }

  promises.AppendElement(
      InvokeAsync(
          mCallThread, __func__,
          [pipeline = mPipeline, recvTrackId = std::move(recvTrackId),
           mid = std::move(mid), transportId = std::move(transportId)] {
            auto report = std::make_unique<dom::RTCStatsCollection>();
            auto asAudio = pipeline->mConduit->AsAudioSessionConduit();
            auto asVideo = pipeline->mConduit->AsVideoSessionConduit();

            nsString kind = asVideo.isNothing() ? u"audio"_ns : u"video"_ns;
            nsString idstr = kind + u"_"_ns;
            idstr.AppendInt(static_cast<uint32_t>(pipeline->Level()));

            Maybe<uint32_t> ssrc = pipeline->mConduit->GetRemoteSSRC();

            // Add frame history
            asVideo.apply([&](const auto& conduit) {
              if (conduit->AddFrameHistory(&report->mVideoFrameHistories)) {
                auto& history = report->mVideoFrameHistories.LastElement();
                history.mTrackIdentifier = recvTrackId;
              }
            });

            // TODO(@@NG):ssrcs handle Conduits having multiple stats at the
            // same level.
            // This is pending spec work.
            // Gather pipeline stats.
            nsString localId = u"inbound_rtp_"_ns + idstr;
            nsString remoteId;

            auto constructCommonRemoteOutboundRtpStats =
                [&](RTCRemoteOutboundRtpStreamStats& aRemote,
                    const DOMHighResTimeStamp& aTimestamp) {
                  remoteId = u"inbound_rtcp_"_ns + idstr;
                  aRemote.mTimestamp.Construct(aTimestamp);
                  aRemote.mId.Construct(remoteId);
                  aRemote.mType.Construct(RTCStatsType::Remote_outbound_rtp);
                  ssrc.apply([&](uint32_t aSsrc) { aRemote.mSsrc = aSsrc; });
                  aRemote.mKind = kind;
                  aRemote.mMediaType.Construct(
                      kind);  // mediaType is the old name for kind.
                  aRemote.mLocalId.Construct(localId);
                  if (!transportId.IsEmpty()) {
                    aRemote.mTransportId.Construct(transportId);
                  }
                };

            auto constructCommonInboundRtpStats =
                [&](RTCInboundRtpStreamStats& aLocal) {
                  aLocal.mTrackIdentifier = recvTrackId;
                  if (mid != "") {
                    aLocal.mMid.Construct(NS_ConvertUTF8toUTF16(mid).get());
                  }
                  aLocal.mTimestamp.Construct(
                      pipeline->GetTimestampMaker().GetNow().ToDom());
                  aLocal.mId.Construct(localId);
                  aLocal.mType.Construct(RTCStatsType::Inbound_rtp);
                  ssrc.apply([&](uint32_t aSsrc) { aLocal.mSsrc = aSsrc; });
                  aLocal.mKind = kind;
                  aLocal.mMediaType.Construct(
                      kind);  // mediaType is the old name for kind.
                  if (remoteId.Length()) {
                    aLocal.mRemoteId.Construct(remoteId);
                  }
                  if (!transportId.IsEmpty()) {
                    aLocal.mTransportId.Construct(transportId);
                  }
                };

            asAudio.apply([&](auto& aConduit) {
              Maybe<webrtc::AudioReceiveStreamInterface::Stats> audioStats =
                  aConduit->GetReceiverStats();
              if (audioStats.isNothing()) {
                return;
              }

              if (!audioStats->last_packet_received.has_value()) {
                // By spec: "The lifetime of all RTP monitored objects starts
                // when the RTP stream is first used: When the first RTP packet
                // is sent or received on the SSRC it represents"
                return;
              }

              // First, fill in remote stat with rtcp sender data, if present.
              if (audioStats->last_sender_report_utc_timestamp) {
                RTCRemoteOutboundRtpStreamStats remote;
                constructCommonRemoteOutboundRtpStats(
                    remote,
                    RTCStatsTimestamp::FromNtp(
                        aConduit->GetTimestampMaker(),
                        /*webrtc::Timestamp::Millis(*/
                        *audioStats->last_sender_report_utc_timestamp +
                            webrtc::TimeDelta::Seconds(webrtc::kNtpJan1970))
                        .ToDom());
                remote.mPacketsSent.Construct(
                    audioStats->sender_reports_packets_sent);
                remote.mBytesSent.Construct(
                    audioStats->sender_reports_bytes_sent);
                remote.mRemoteTimestamp.Construct(
                    audioStats->last_sender_report_remote_utc_timestamp
                        ->ms<double>());
                if (!report->mRemoteOutboundRtpStreamStats.AppendElement(
                        std::move(remote), fallible)) {
                  mozalloc_handle_oom(0);
                }
              }

              // Then, fill in local side (with cross-link to remote only if
              // present)
              RTCInboundRtpStreamStats local;
              constructCommonInboundRtpStats(local);
              local.mJitter.Construct(audioStats->jitter_ms / 1000.0);
              local.mPacketsLost.Construct(audioStats->packets_lost);
              local.mPacketsReceived.Construct(audioStats->packets_received);
              local.mPacketsDiscarded.Construct(audioStats->packets_discarded);
              local.mBytesReceived.Construct(
                  audioStats->payload_bytes_received);
              if (audioStats->estimated_playout_ntp_timestamp_ms) {
                local.mEstimatedPlayoutTimestamp.Construct(
                    RTCStatsTimestamp::FromNtp(
                        aConduit->GetTimestampMaker(),
                        webrtc::Timestamp::Millis(
                            *audioStats->estimated_playout_ntp_timestamp_ms))
                        .ToDom());
              }
              local.mJitterBufferDelay.Construct(
                  audioStats->jitter_buffer_delay_seconds);
              local.mJitterBufferTargetDelay.Construct(
                  audioStats->jitter_buffer_target_delay_seconds);
              local.mJitterBufferEmittedCount.Construct(
                  audioStats->jitter_buffer_emitted_count);
              local.mJitterBufferMinimumDelay.Construct(
                  audioStats->jitter_buffer_minimum_delay_seconds);
              local.mTotalSamplesReceived.Construct(
                  audioStats->total_samples_received);
              local.mConcealedSamples.Construct(audioStats->concealed_samples);
              local.mSilentConcealedSamples.Construct(
                  audioStats->silent_concealed_samples);
              if (audioStats->last_packet_received.has_value()) {
                local.mLastPacketReceivedTimestamp.Construct(
                    RTCStatsTimestamp::FromNtp(
                        aConduit->GetTimestampMaker(),
                        webrtc::Timestamp::Millis(
                            audioStats->last_packet_received->ms()) +
                            webrtc::TimeDelta::Seconds(webrtc::kNtpJan1970))
                        .ToDom());
              }
              local.mHeaderBytesReceived.Construct(
                  audioStats->header_and_padding_bytes_received);
              local.mFecPacketsReceived.Construct(
                  audioStats->fec_packets_received);
              local.mFecPacketsDiscarded.Construct(
                  audioStats->fec_packets_discarded);
              local.mConcealmentEvents.Construct(
                  audioStats->concealment_events);

              local.mInsertedSamplesForDeceleration.Construct(
                  audioStats->inserted_samples_for_deceleration);
              local.mRemovedSamplesForAcceleration.Construct(
                  audioStats->removed_samples_for_acceleration);
              if (audioStats->audio_level >= 0 &&
                  audioStats->audio_level <= 32767) {
                local.mAudioLevel.Construct(audioStats->audio_level / 32767.0);
              }
              local.mTotalAudioEnergy.Construct(
                  audioStats->total_output_energy);
              local.mTotalSamplesDuration.Construct(
                  audioStats->total_output_duration);

              if (!report->mInboundRtpStreamStats.AppendElement(
                      std::move(local), fallible)) {
                mozalloc_handle_oom(0);
              }
            });

            asVideo.apply([&](auto& aConduit) {
              Maybe<webrtc::VideoReceiveStreamInterface::Stats> videoStats =
                  aConduit->GetReceiverStats();
              if (videoStats.isNothing()) {
                return;
              }

              if (!videoStats->rtp_stats.last_packet_received.has_value()) {
                // By spec: "The lifetime of all RTP monitored objects starts
                // when the RTP stream is first used: When the first RTP packet
                // is sent or received on the SSRC it represents"
                return;
              }

              // First, fill in remote stat with rtcp sender data, if present.
              if (videoStats->rtcp_sender_ntp_timestamp_ms) {
                RTCRemoteOutboundRtpStreamStats remote;
                constructCommonRemoteOutboundRtpStats(
                    remote, RTCStatsTimestamp::FromNtp(
                                aConduit->GetTimestampMaker(),
                                webrtc::Timestamp::Millis(
                                    videoStats->rtcp_sender_ntp_timestamp_ms))
                                .ToDom());
                remote.mPacketsSent.Construct(
                    videoStats->rtcp_sender_packets_sent);
                remote.mBytesSent.Construct(
                    videoStats->rtcp_sender_octets_sent);
                remote.mRemoteTimestamp.Construct(
                    (webrtc::TimeDelta::Millis(
                         videoStats->rtcp_sender_remote_ntp_timestamp_ms) -
                     webrtc::TimeDelta::Seconds(webrtc::kNtpJan1970))
                        .ms());
                if (!report->mRemoteOutboundRtpStreamStats.AppendElement(
                        std::move(remote), fallible)) {
                  mozalloc_handle_oom(0);
                }
              }

              // Then, fill in local side (with cross-link to remote only if
              // present)
              RTCInboundRtpStreamStats local;
              constructCommonInboundRtpStats(local);
              local.mJitter.Construct(
                  static_cast<double>(videoStats->rtp_stats.jitter) /
                  webrtc::kVideoPayloadTypeFrequency);
              local.mPacketsLost.Construct(videoStats->rtp_stats.packets_lost);
              local.mPacketsReceived.Construct(
                  videoStats->rtp_stats.packet_counter.packets);
              local.mPacketsDiscarded.Construct(videoStats->packets_discarded);
              local.mDiscardedPackets.Construct(videoStats->packets_discarded);
              local.mBytesReceived.Construct(
                  videoStats->rtp_stats.packet_counter.payload_bytes);
              aConduit->GetAssociatedRemoteRtxSSRC().apply([&](const auto
                                                                   rtxSsrc) {
                local.mRtxSsrc.Construct(rtxSsrc);
                // rtx_rtp_stats is only set once an RTX packet has been
                // received, but the retransmitted counters should be present
                // for the lifetime of the negotiated RTX stream.
                if (videoStats->rtx_rtp_stats) {
                  local.mRetransmittedPacketsReceived.Construct(
                      videoStats->rtx_rtp_stats->packet_counter.packets);
                  local.mRetransmittedBytesReceived.Construct(
                      videoStats->rtx_rtp_stats->packet_counter.payload_bytes);
                } else {
                  local.mRetransmittedPacketsReceived.Construct(0);
                  local.mRetransmittedBytesReceived.Construct(0);
                }
              });

              // Fill in packet type statistics
              local.mNackCount.Construct(
                  videoStats->rtcp_packet_type_counts.nack_packets);
              local.mFirCount.Construct(
                  videoStats->rtcp_packet_type_counts.fir_packets);
              local.mPliCount.Construct(
                  videoStats->rtcp_packet_type_counts.pli_packets);

              // Lastly, fill in video decoder stats
              local.mFramesDecoded.Construct(videoStats->frames_decoded);
              local.mKeyFramesDecoded.Construct(videoStats->key_frames_decoded);

              local.mFramesPerSecond.Construct(videoStats->decode_frame_rate);
              local.mFrameWidth.Construct(videoStats->width);
              local.mFrameHeight.Construct(videoStats->height);
              // XXX: key_frames + delta_frames may undercount frames because
              // they were dropped in FrameBuffer::InsertFrame. (bug 1766553)
              local.mFramesReceived.Construct(
                  videoStats->received_frame_counts.key_frames +
                  videoStats->received_frame_counts.delta_frames);
              local.mJitterBufferDelay.Construct(
                  videoStats->jitter_buffer_delay.seconds<double>());
              local.mJitterBufferTargetDelay.Construct(
                  videoStats->jitter_buffer_target_delay.seconds<double>());
              local.mJitterBufferEmittedCount.Construct(
                  videoStats->jitter_buffer_emitted_count);
              local.mJitterBufferMinimumDelay.Construct(
                  videoStats->jitter_buffer_minimum_delay.seconds<double>());

              if (videoStats->qp_sum) {
                local.mQpSum.Construct(videoStats->qp_sum.value());
              }
              local.mTotalDecodeTime.Construct(
                  double(videoStats->total_decode_time.ms()) / 1000);
              local.mTotalInterFrameDelay.Construct(
                  videoStats->total_inter_frame_delay);
              local.mTotalSquaredInterFrameDelay.Construct(
                  videoStats->total_squared_inter_frame_delay);
              local.mPauseCount.Construct(videoStats->pause_count);
              local.mTotalPausesDuration.Construct(
                  double(videoStats->total_pauses_duration_ms) / 1000.0);
              local.mFreezeCount.Construct(videoStats->freeze_count);
              local.mTotalFreezesDuration.Construct(
                  double(videoStats->total_freezes_duration_ms) / 1000.0);

              if (videoStats->rtp_stats.last_packet_received.has_value()) {
                local.mLastPacketReceivedTimestamp.Construct(
                    RTCStatsTimestamp::FromNtp(
                        aConduit->GetTimestampMaker(),
                        webrtc::Timestamp::Millis(
                            videoStats->rtp_stats.last_packet_received->ms()) +
                            webrtc::TimeDelta::Seconds(webrtc::kNtpJan1970))
                        .ToDom());
              }
              local.mHeaderBytesReceived.Construct(
                  videoStats->rtp_stats.packet_counter.header_bytes +
                  videoStats->rtp_stats.packet_counter.padding_bytes);
              local.mTotalProcessingDelay.Construct(
                  videoStats->total_processing_delay.seconds<double>());
              local.mFramesAssembledFromMultiplePackets.Construct(
                  videoStats->frames_assembled_from_multiple_packets);
              local.mTotalAssemblyTime.Construct(
                  videoStats->total_assembly_time.seconds<double>());
              // For audio this is a Timestamp, but for video it is an int64_t
              if (videoStats->estimated_playout_ntp_timestamp_ms.value_or(0)) {
                local.mEstimatedPlayoutTimestamp.Construct(
                    RTCStatsTimestamp::FromNtp(
                        aConduit->GetTimestampMaker(),
                        webrtc::Timestamp::Millis(
                            *videoStats->estimated_playout_ntp_timestamp_ms))
                        .ToDom());
              }
              // Not including frames dropped in the rendering pipe, which
              // is not of webrtc's concern anyway?!
              local.mFramesDropped.Construct(videoStats->frames_dropped);
              if (!report->mInboundRtpStreamStats.AppendElement(
                      std::move(local), fallible)) {
                mozalloc_handle_oom(0);
              }
              // We need to first support the corruption detection RTP header
              // extension before we can add these stats.
              // if (videoStats->corruption_score_sum.has_value()) {
              //   local.mTotalCorruptionProbability.Construct(
              //     videoStats->corruption_score_sum.value());
              // }
              // if (videoStats->corruption_score_squared_sum.has_value()) {
              //   local.mTotalSquaredCorruptionProbability.Construct(
              //     videoStats->corruption_score_squared_sum.value());
              // }
              // local.mCorruptionMeasurements.Construct(
              //   videoStats->corruption_score_count);
            });
            return RTCStatsPromise::CreateAndResolve(std::move(report),
                                                     __func__);
          })
          ->Then(
              mStsThread, __func__,
              [pipeline = mPipeline](UniquePtr<RTCStatsCollection> aReport) {
                // Fill in Contributing Source statistics
                if (!aReport->mInboundRtpStreamStats.IsEmpty() &&
                    aReport->mInboundRtpStreamStats[0].mId.WasPassed()) {
                  pipeline->GetContributingSourceStats(
                      aReport->mInboundRtpStreamStats[0].mId.Value(),
                      aReport->mRtpContributingSourceStats);
                }
                return RTCStatsPromise::CreateAndResolve(std::move(aReport),
                                                         __func__);
              },
              [] {
                MOZ_CRASH("Unexpected reject");
                return RTCStatsPromise::CreateAndReject(NS_ERROR_UNEXPECTED,
                                                        __func__);
              }));

  if (!aSkipIceStats && GetJsepTransceiver().mTransport.mComponents) {
    promises.AppendElement(mTransportHandler->GetIceStats(
        GetJsepTransceiver().mTransport.mTransportId,
        mPipeline->GetTimestampMaker().GetNow().ToDom()));
  }

  return promises;
}

void RTCRtpReceiver::SetJitterBufferTarget(
    const Nullable<DOMHighResTimeStamp>& aTargetMs, ErrorResult& aError) {
  // Spec says jitter buffer target cannot be negative or larger than 4000
  // milliseconds and to throw RangeError if it is. If an invalid value is
  // received we return early to preserve the current JitterBufferTarget
  // internal slot and jitter buffer values.
  if (mPipeline && mPipeline->mConduit) {
    if (!aTargetMs.IsNull() &&
        (aTargetMs.Value() < 0.0 || aTargetMs.Value() > 4000.0)) {
      aError.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("jitterBufferTarget");
      return;
    }

    mJitterBufferTarget.reset();

    if (!aTargetMs.IsNull()) {
      mJitterBufferTarget = Some(aTargetMs.Value());
    }
    // If aJitterBufferTarget is null then we are resetting the jitter buffer so
    // pass the default target of 0.0.
    mPipeline->mConduit->SetJitterBufferTarget(
        mJitterBufferTarget.valueOr(0.0));
  }
}

void RTCRtpReceiver::GetContributingSources(
    nsTArray<RTCRtpContributingSource>& aSources) {
  // Duplicate code...
  if (mPipeline && mPipeline->mConduit) {
    nsTArray<dom::RTCRtpSourceEntry> sources;
    mPipeline->mConduit->GetRtpSources(sources);
    sources.RemoveElementsBy([](const dom::RTCRtpSourceEntry& aEntry) {
      return aEntry.mSourceType != dom::RTCRtpSourceEntryType::Contributing;
    });
    aSources.ReplaceElementsAt(0, aSources.Length(), sources.Elements(),
                               sources.Length());
  }
}

void RTCRtpReceiver::GetSynchronizationSources(
    nsTArray<dom::RTCRtpSynchronizationSource>& aSources) {
  // Duplicate code...
  if (mPipeline && mPipeline->mConduit) {
    nsTArray<dom::RTCRtpSourceEntry> sources;
    mPipeline->mConduit->GetRtpSources(sources);
    sources.RemoveElementsBy([](const dom::RTCRtpSourceEntry& aEntry) {
      return aEntry.mSourceType != dom::RTCRtpSourceEntryType::Synchronization;
    });
    aSources.ReplaceElementsAt(0, aSources.Length(), sources.Elements(),
                               sources.Length());
  }
}

nsPIDOMWindowInner* RTCRtpReceiver::GetParentObject() const { return mWindow; }

void RTCRtpReceiver::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  mWatchManager.Shutdown();
  if (mPipeline) {
    mPipeline->Shutdown();
    mPipeline = nullptr;
  }
  if (mTrackSource) {
    mTrackSource->Destroy();
  }
  mCallThread = nullptr;
  mReceivingSize.DisconnectIfConnected();
  mRtcpByeListener.DisconnectIfExists();
  mRtcpTimeoutListener.DisconnectIfExists();
  mUnmuteListener.DisconnectIfExists();
  if (mTransform) {
    mTransform->GetProxy().SetReceiver(nullptr);
  }
}

void RTCRtpReceiver::BreakCycles() {
  mWindow = nullptr;
  mPc = nullptr;
  mTrack = nullptr;
  mTrackSource = nullptr;
}

void RTCRtpReceiver::Unlink() {
  if (mTransceiver) {
    mTransceiver->Unlink();
  }
}

void RTCRtpReceiver::UpdateTransport() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mHaveSetupTransport && GetJsepTransceiver().HasLevel()) {
    mPipeline->SetLevel(GetJsepTransceiver().GetLevel());
    mHaveSetupTransport = true;
  }

  UniquePtr<MediaPipelineFilter> filter;
  bool signalingStable =
      (mPc->GetSignalingState() == RTCSignalingState::Stable);

  auto const& details = GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails();
  std::map<std::string, webrtc::RtpExtension> extmapsByUri;
  if (GetJsepTransceiver().HasBundleLevel()) {
    if (details) {
      details->ForEachRTPHeaderExtension(
          [&extmapsByUri](const SdpExtmapAttributeList::Extmap& extmap) {
            extmapsByUri.insert_or_assign(
                extmap.extensionname.get(),
                webrtc::RtpExtension(
                    extmap.extensionname,
                    webrtc::RtpHeaderExtensionId(extmap.entry)));
          });
    }
    if (!signalingStable) {
      // Early media (bug 2019381): the negotiated details above only
      // reflect the last completed offer/answer round, which can be stale
      // during a pending local offer -- e.g. an extension the offer just
      // (re)added wouldn't be recognized yet, since Negotiate() hasn't run
      // for this round. Layer in whatever the pending offer itself
      // declares, so the demux filter built below already knows about it.
      for (const auto& extmap :
           GetJsepTransceiver().mRecvTrack.GetEarlyRtpExtensions()) {
        extmapsByUri.insert_or_assign(
            extmap.extensionname.get(),
            webrtc::RtpExtension(extmap.extensionname,
                                 webrtc::RtpHeaderExtensionId(extmap.entry)));
      }
    }
    std::vector<webrtc::RtpExtension> extmaps;
    extmaps.reserve(extmapsByUri.size());
    for (auto& [uri, extmap] : extmapsByUri) {
      extmaps.push_back(extmap);
    }

    filter = std::make_unique<MediaPipelineFilter>(extmaps);

    // Add remote SSRCs so we can distinguish which RTP packets actually
    // belong to this pipeline (also RTCP sender reports).
    for (uint32_t ssrc : GetJsepTransceiver().mRecvTrack.GetSsrcs()) {
      filter->AddRemoteSSRC(ssrc);
    }
    for (uint32_t ssrc : GetJsepTransceiver().mRecvTrack.GetRtxSsrcs()) {
      filter->AddRemoteSSRC(ssrc);
    }
    auto mid = Maybe<std::string>();
    if (GetMid() != "") {
      mid = Some(GetMid());
    }
    filter->SetRemoteMediaStreamId(mid);

    // Add unique payload types as a last-ditch fallback
    auto uniquePts =
        GetJsepTransceiver().mRecvTrack.GetUniqueReceivePayloadTypes();
    for (auto uniquePt : uniquePts) {
      filter->AddUniqueReceivePT(uniquePt);
    }

    // Add duplicate payload types
    auto duplicatePts =
        GetJsepTransceiver().mRecvTrack.GetOtherReceivePayloadTypes();

    for (auto duplicatePt : duplicatePts) {
      filter->AddOtherReceivePT(duplicatePt);
    }
  }

  if (signalingStable || filter) {
    mPipeline->UpdateTransport_m(GetJsepTransceiver().mTransport.mTransportId,
                                 std::move(filter), signalingStable);
  }
}

bool RTCRtpReceiver::CanReceiveEarlyMedia() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!GetJsepTransceiver().mRecvTrack.GetReceptive()) {
    // The local description we just applied isn't actually offering to
    // receive right now ([[Receptive]] per WEBRTC-PC); defer entirely to
    // IsReceiving(), which reflects the real negotiated outcome via
    // mCurrentDirection. mTransceiver->Direction() is only the app's
    // preferred direction and can disagree with what was actually applied.
    return false;
  }
  if (!GetJsepTransceiver().mRecvTrack.HasEarlyRecvCodecs()) {
    // Nothing to decode with; starting a receive stream would assert.
    return false;
  }
  if (mPc->GetSignalingState() != RTCSignalingState::Have_local_offer) {
    // No pending, unanswered local offer right now to be tentative about.
    return false;
  }
  if (!HasNegotiatedBundleOwner()) {
    // Only a bundle with at least one negotiated member is actually live
    // (has gone through ICE/DTLS); a fresh bundle cannot have received
    // anything yet.
    return false;
  }
  // Only tentative if our own recv-relevant offer actually changed this
  // round; otherwise the real negotiated details -- still valid, and
  // unaffected by an unrelated renegotiation elsewhere in the session --
  // should keep being used instead.
  return mPc->LocalOfferedRecvParamsChanged(GetMid());
}

bool RTCRtpReceiver::HasNegotiatedBundleOwner() {
  MOZ_ASSERT(NS_IsMainThread());
  // See JsepSessionImpl::SetLocalDescription() for how this is computed:
  // it identifies whichever transceiver currently owns this mid's transport
  // (the bundle tag, or this transceiver itself if unbundled) and checks
  // whether *that* transceiver already had its own transport as of last
  // round -- not just BundleLevel() (which JSEP can clear speculatively
  // mid-round -- see EnsureHasOwnTransport()).
  return GetJsepTransceiver().CanUseExistingTransport();
}

void RTCRtpReceiver::UpdateConduit() {
  if (mPipeline->mConduit->type() == MediaSessionConduit::VIDEO) {
    UpdateVideoConduit();
  } else {
    UpdateAudioConduit();
  }

  if ((mReceiving = mTransceiver->IsReceiving() || CanReceiveEarlyMedia())) {
    mHaveStartedReceiving = true;
  }
}

void RTCRtpReceiver::UpdateVideoConduit() {
  RefPtr<VideoSessionConduit> conduit =
      *mPipeline->mConduit->AsVideoSessionConduit();

  // NOTE(pkerr) - this is new behavior. Needed because the
  // CreateVideoReceiveStream method of the Call API will assert (in debug)
  // and fail if a value is not provided for the remote_ssrc that will be used
  // by the far-end sender.
  if (!GetJsepTransceiver().mRecvTrack.GetSsrcs().empty()) {
    MOZ_LOG_FMT(gReceiverLog, LogLevel::Debug,
                "{}[{}]: {} Setting remote SSRC {}", mPc->GetHandle().c_str(),
                GetMid().c_str(), __FUNCTION__,
                GetJsepTransceiver().mRecvTrack.GetSsrcs().front());
    uint32_t rtxSsrc =
        GetJsepTransceiver().mRecvTrack.GetRtxSsrcs().empty()
            ? 0
            : GetJsepTransceiver().mRecvTrack.GetRtxSsrcs().front();
    mSsrc = GetJsepTransceiver().mRecvTrack.GetSsrcs().front();
    mVideoRtxSsrc = rtxSsrc;

    // TODO (bug 1423041) once we pay attention to receiving MID's in RTP
    // packets (see bug 1405495) we could make this depending on the presence of
    // MID in the RTP packets instead of relying on the signaling.
    // In any case, do not disable SSRC changes if no SSRCs were negotiated
    if (GetJsepTransceiver().HasBundleLevel() &&
        (!GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails() ||
         !GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails()->GetExt(
             nsLiteralCString(webrtc::RtpExtension::kMidUri)))) {
      mCallThread->Dispatch(
          NewRunnableMethod("VideoSessionConduit::DisableSsrcChanges", conduit,
                            &VideoSessionConduit::DisableSsrcChanges));
    }
  }

  if (CanReceiveEarlyMedia()) {
    // Early media (bug 2019381); see CanReceiveEarlyMedia(). Once a real
    // answer arrives, the negotiated-details branch below takes back over.
    std::vector<VideoCodecConfig> configs;
    RTCRtpTransceiver::EarlyRecvCodecsToVideoCodecConfigs(
        GetJsepTransceiver().mRecvTrack, &configs);
    if (!configs.empty()) {
      mVideoCodecs = configs;
      mVideoRtpRtcpConfig =
          Some(RtpRtcpConfig(webrtc::RtcpMode::kCompound, true));
    }
  } else if (GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails() &&
             GetJsepTransceiver().mRecvTrack.GetActive()) {
    const auto& details(
        *GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails());

    {
      std::vector<webrtc::RtpExtension> extmaps;
      // @@NG read extmap from track
      details.ForEachRTPHeaderExtension(
          [&extmaps](const SdpExtmapAttributeList::Extmap& extmap) {
            extmaps.emplace_back(extmap.extensionname,
                                 webrtc::RtpHeaderExtensionId(extmap.entry));
          });
      mLocalRtpExtensions = extmaps;
    }

    std::vector<VideoCodecConfig> configs;
    RTCRtpTransceiver::NegotiatedDetailsToVideoCodecConfigs(details, &configs);
    if (configs.empty()) {
      // TODO: Are we supposed to plumb this error back to JS? This does not
      // seem like a failure to set an answer, it just means that codec
      // negotiation failed. For now, we're just doing the same thing we do
      // if negotiation as a whole failed.
      MOZ_LOG_FMT(gReceiverLog, LogLevel::Error,
                  "{}[{}]: {}  No video codecs were negotiated (recv).",
                  mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
      return;
    }

    mVideoCodecs = configs;
    mVideoRtpRtcpConfig = Some(details.GetRtpRtcpConfig());
  }
}

void RTCRtpReceiver::UpdateAudioConduit() {
  RefPtr<AudioSessionConduit> conduit =
      *mPipeline->mConduit->AsAudioSessionConduit();

  if (!GetJsepTransceiver().mRecvTrack.GetSsrcs().empty()) {
    MOZ_LOG_FMT(gReceiverLog, LogLevel::Debug,
                "{}[{}]: {} Setting remote SSRC {}", mPc->GetHandle().c_str(),
                GetMid().c_str(), __FUNCTION__,
                GetJsepTransceiver().mRecvTrack.GetSsrcs().front());
    mSsrc = GetJsepTransceiver().mRecvTrack.GetSsrcs().front();

    // TODO (bug 1423041) once we pay attention to receiving MID's in RTP
    // packets (see bug 1405495) we could make this depending on the presence of
    // MID in the RTP packets instead of relying on the signaling.
    // In any case, do not disable SSRC changes if no SSRCs were negotiated
    if (GetJsepTransceiver().HasBundleLevel() &&
        (!GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails() ||
         !GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails()->GetExt(
             nsLiteralCString(webrtc::RtpExtension::kMidUri)))) {
      mCallThread->Dispatch(
          NewRunnableMethod("AudioSessionConduit::DisableSsrcChanges", conduit,
                            &AudioSessionConduit::DisableSsrcChanges));
    }
  }

  if (CanReceiveEarlyMedia()) {
    // Early media (bug 2019381); see CanReceiveEarlyMedia(). Once a real
    // answer arrives, the negotiated-details branch below takes back over.
    std::vector<AudioCodecConfig> configs;
    RTCRtpTransceiver::EarlyRecvCodecsToAudioCodecConfigs(
        GetJsepTransceiver().mRecvTrack, &configs);
    if (!configs.empty()) {
      mAudioCodecs = configs;
    }
  } else if (GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails() &&
             GetJsepTransceiver().mRecvTrack.GetActive()) {
    const auto& details(
        *GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails());
    std::vector<AudioCodecConfig> configs;
    RTCRtpTransceiver::NegotiatedDetailsToAudioCodecConfigs(details, &configs);
    if (configs.empty()) {
      // TODO: Are we supposed to plumb this error back to JS? This does not
      // seem like a failure to set an answer, it just means that codec
      // negotiation failed. For now, we're just doing the same thing we do
      // if negotiation as a whole failed.
      MOZ_LOG_FMT(gReceiverLog, LogLevel::Error,
                  "{}[{}]: {} No audio codecs were negotiated (recv)",
                  mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
      return;
    }

    // Ensure conduit knows about extensions prior to creating streams
    {
      std::vector<webrtc::RtpExtension> extmaps;
      // @@NG read extmap from track
      details.ForEachRTPHeaderExtension(
          [&extmaps](const SdpExtmapAttributeList::Extmap& extmap) {
            extmaps.emplace_back(extmap.extensionname,
                                 webrtc::RtpHeaderExtensionId(extmap.entry));
          });
      mLocalRtpExtensions = extmaps;
    }

    mAudioCodecs = configs;
  }
}

void RTCRtpReceiver::Stop() {
  MOZ_ASSERT(mTransceiver->Stopped() || mTransceiver->Stopping());
  mReceiving = false;
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [trackSource = mTrackSource] { trackSource->ForceEnded(); }));
}

bool RTCRtpReceiver::HasTrack(const dom::MediaStreamTrack* aTrack) const {
  return !aTrack || (mTrack == aTrack);
}

void RTCRtpReceiver::SyncFromJsep(const JsepTransceiver& aJsepTransceiver) {
  // Spec says we set [[Receptive]] to true on sLD(sendrecv/recvonly), and to
  // false on sRD(recvonly/inactive), sLD(sendonly/inactive), or when stop()
  // is called.
  // Update mReceptive and disconnect before the mPipeline guard: if mPipeline
  // was cleared by an async Shutdown(), UpdateStreams and
  // SetTrackMuteFromRemoteSdp must still see a consistent mReceptive state.
  bool wasReceptive = mReceptive;
  mReceptive = aJsepTransceiver.mRecvTrack.GetReceptive();
  if (wasReceptive && !mReceptive) {
    mUnmuteListener.DisconnectIfExists();
  }

  if (!mPipeline) {
    return;
  }

  if (GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails()) {
    const auto& details(
        *GetJsepTransceiver().mRecvTrack.GetNegotiatedDetails());
    mParameters.mHeaderExtensions.Reset();
    mParameters.mHeaderExtensions.Construct();
    RTCRtpTransceiver::ToDomHeaderExtensions(
        details, mParameters.mHeaderExtensions.Value());
    mParameters.mCodecs.Reset();
    mParameters.mCodecs.Construct();
    if (details.GetEncodingCount()) {
      for (const auto& jsepCodec : details.GetEncoding(0).GetCodecs()) {
        if (!jsepCodec->mEnabled ||
            !jsepCodec->DirectionSupported(sdp::kRecv)) {
          // This codec is disabled or receiving it is unsupported.
          continue;
        }
        RTCRtpCodecParameters codec;
        RTCRtpTransceiver::ToDomRtpCodecParameters(*jsepCodec, &codec);
        (void)mParameters.mCodecs.Value().AppendElement(codec, fallible);
        if (jsepCodec->Type() == SdpMediaSection::kVideo) {
          const JsepVideoCodecDescription& videoJsepCodec =
              static_cast<JsepVideoCodecDescription&>(*jsepCodec);
          if (videoJsepCodec.mRtxEnabled) {
            RTCRtpCodecParameters rtx;
            RTCRtpTransceiver::ToDomRtpCodecParametersRtx(videoJsepCodec, &rtx);
            (void)mParameters.mCodecs.Value().AppendElement(rtx, fallible);
          }
        }
      }
    }
  }

  if (!wasReceptive && mReceptive) {
    mUnmuteListener = mPipeline->mConduit->RtpPacketEvent().Connect(
        GetMainThreadSerialEventTarget(), this, &RTCRtpReceiver::OnRtpPacket);
  }
}

void RTCRtpReceiver::SyncToJsep(JsepTransceiver& aJsepTransceiver) const {
  if (!mTransceiver->GetPreferredCodecs().IsEmpty()) {
    aJsepTransceiver.mRecvTrack.PopulateCodecs(
        mTransceiver->GetPreferredCodecs(),
        mTransceiver->GetPreferredCodecsInUse());
  }
}

void RTCRtpReceiver::UpdateStreams(StreamAssociationChanges* aChanges) {
  // We don't sort and use set_difference, because we need to report the
  // added/removed streams in the order that they appear in the SDP.
  std::set<std::string> newIds(
      GetJsepTransceiver().mRecvTrack.GetStreamIds().begin(),
      GetJsepTransceiver().mRecvTrack.GetStreamIds().end());
  MOZ_ASSERT(GetJsepTransceiver().mRecvTrack.GetRemoteSetSendBit() ||
             newIds.empty());
  bool needsTrackEvent = false;
  for (const auto& id : mStreamIds) {
    if (!newIds.count(id)) {
      aChanges->mStreamAssociationsRemoved.push_back({mTrack, id});
    }
  }

  std::set<std::string> oldIds(mStreamIds.begin(), mStreamIds.end());
  for (const auto& id : GetJsepTransceiver().mRecvTrack.GetStreamIds()) {
    if (!oldIds.count(id)) {
      needsTrackEvent = true;
      aChanges->mStreamAssociationsAdded.push_back({mTrack, id});
    }
  }

  mStreamIds = GetJsepTransceiver().mRecvTrack.GetStreamIds();

  if (mRemoteSetSendBit !=
      GetJsepTransceiver().mRecvTrack.GetRemoteSetSendBit()) {
    mRemoteSetSendBit = GetJsepTransceiver().mRecvTrack.GetRemoteSetSendBit();
    if (mRemoteSetSendBit) {
      needsTrackEvent = true;
    } else {
      aChanges->mReceiversToMute.push_back(this);
    }
  }

  if (needsTrackEvent) {
    aChanges->mTrackEvents.push_back({this, mStreamIds});
  }
}

void RTCRtpReceiver::UpdatePrincipalPrivacy(PrincipalPrivacy aPrivacy) {
  if (!mPipeline) {
    return;
  }

  if (aPrivacy != PrincipalPrivacy::Private) {
    return;
  }

  mPipeline->SetPrivatePrincipal(GetPrincipalHandle(mWindow, aPrivacy));
}

// test-only: adds fake CSRCs and audio data
void RTCRtpReceiver::MozInsertAudioLevelForContributingSource(
    const uint32_t aSource, const DOMHighResTimeStamp aTimestamp,
    const uint32_t aRtpTimestamp, const bool aHasLevel, const uint8_t aLevel) {
  if (!mPipeline || mPipeline->IsVideo() || !mPipeline->mConduit) {
    return;
  }
  mPipeline->mConduit->InsertAudioLevelForContributingSource(
      aSource, aTimestamp, aRtpTimestamp, aHasLevel, aLevel);
}

void RTCRtpReceiver::OnRtcpBye() { mReceiveTrackMute = true; }

void RTCRtpReceiver::OnRtcpTimeout() { mReceiveTrackMute = true; }

void RTCRtpReceiver::SetTrackMuteFromRemoteSdp() {
  MOZ_ASSERT(!mReceptive,
             "PeerConnectionImpl should have blocked unmute events prior to "
             "firing mute");
  if (!mTrack || mTrack->Ended()) {
    // Track has already ended (e.g. transceiver was stopped before this
    // renegotiation), so there is nothing to mute.
    return;
  }
  mReceiveTrackMute = true;
  // Set the mute state (and fire the mute event) synchronously. Unmute is
  // handled asynchronously after receiving RTP packets.
  UpdateReceiveTrackMute();
  MOZ_ASSERT(mTrack->Muted(), "Muted state was indeed set synchronously");
}

void RTCRtpReceiver::OnRtpPacket() {
  MOZ_ASSERT(mReceptive, "We should not be registered unless this is set!");
  // We should be registered since we're currently getting a callback.
  mUnmuteListener.Disconnect();
  if (mReceptive) {
    mReceiveTrackMute = false;
  }
}

void RTCRtpReceiver::UpdateReceiveTrackMute() {
  if (!mTrack) {
    return;
  }
  if (!mTrackSource) {
    return;
  }
  // This sets the muted state for mTrack and all its clones.
  // Idempotent -- only reacts to changes.
  mTrackSource->SetMuted(mReceiveTrackMute);
}

std::string RTCRtpReceiver::GetMid() const {
  return mTransceiver->GetMidAscii();
}

JsepTransceiver& RTCRtpReceiver::GetJsepTransceiver() {
  MOZ_ASSERT(mTransceiver);
  return mTransceiver->GetJsepTransceiver();
}

const JsepTransceiver& RTCRtpReceiver::GetJsepTransceiver() const {
  MOZ_ASSERT(mTransceiver);
  return mTransceiver->GetJsepTransceiver();
}

void RTCRtpReceiver::SetTransform(RTCRtpScriptTransform* aTransform,
                                  ErrorResult& aError) {
  if (aTransform == mTransform.get()) {
    // Ok... smile and nod
    // TODO: Depending on spec, this might throw
    // https://github.com/w3c/webrtc-encoded-transform/issues/189
    return;
  }

  if (aTransform && aTransform->IsClaimed()) {
    aError.ThrowInvalidStateError("transform has already been used elsewhere");
    return;
  }

  if (aTransform) {
    mFrameTransformerProxy = &aTransform->GetProxy();
  } else {
    mFrameTransformerProxy = nullptr;
  }

  if (mTransform) {
    mTransform->GetProxy().SetReceiver(nullptr);
  }

  mTransform = const_cast<RTCRtpScriptTransform*>(aTransform);

  if (mTransform) {
    mTransform->GetProxy().SetReceiver(this);
    mTransform->SetClaimed();
  }
}

void RTCRtpReceiver::RequestKeyFrame() {
  if (!mTransform || !mPipeline) {
    return;
  }

  mPipeline->mConduit->AsVideoSessionConduit().apply([&](const auto& conduit) {
    conduit->RequestKeyFrame(&mTransform->GetProxy());
  });
}

const RTCStatsTimestampMaker* RTCRtpReceiver::GetTimestampMaker() const {
  if (!mPc) {
    return nullptr;
  }
  return &mPc->GetTimestampMaker();
}

Maybe<gfx::IntSize> RTCRtpReceiver::ReceivingSize() const {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(mPipeline,
                mPipeline->mConduit->type() == MediaSessionConduit::VIDEO);
  return mReceivingSize.Ref();
}

}  // namespace mozilla::dom

#undef LOGTAG
