/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RTCRtpSender.h"

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "MainThreadUtils.h"
#include "PeerConnectionImpl.h"
#include "RTCRtpTransceiver.h"
#include "RTCStatsReport.h"
#include "api/rtp_parameters.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_codec_constants.h"
#include "api/video_codecs/video_codec.h"
#include "call/audio_send_stream.h"
#include "call/call.h"
#include "call/video_send_stream.h"
#include "js/RootingAPI.h"
#include "jsep/JsepTransceiver.h"
#include "libwebrtcglue/CodecConfig.h"
#include "libwebrtcglue/MediaConduitControl.h"
#include "libwebrtcglue/MediaConduitInterface.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/StateWatching.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/MediaStreamTrack.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RTCRtpParametersBinding.h"
#include "mozilla/dom/RTCRtpScriptTransform.h"
#include "mozilla/dom/RTCRtpSenderBinding.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/dom/VideoStreamTrack.h"
#include "mozilla/fallible.h"
#include "mozilla/glean/DomMediaWebrtcMetrics.h"
#include "mozilla/mozalloc_oom.h"
#include "nsCOMPtr.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "nsISupports.h"
#include "nsLiteralString.h"
#include "nsPIDOMWindow.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsWrapperCache.h"
#include "sdp/SdpAttribute.h"
#include "sdp/SdpEnum.h"
#include "system_wrappers/include/clock.h"

namespace mozilla::dom {

LazyLogModule gSenderLog("RTCRtpSender");

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(RTCRtpSender)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RTCRtpSender)
  tmp->Unlink();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RTCRtpSender)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow, mPc, mSenderTrack, mTransceiver,
                                    mStreams, mTransform, mDtmf)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(RTCRtpSender)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RTCRtpSender)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RTCRtpSender)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

#define INIT_CANONICAL(name, val) \
  name(AbstractThread::MainThread(), val, "RTCRtpSender::" #name " (Canonical)")

RTCRtpSender::RTCRtpSender(nsPIDOMWindowInner* aWindow, PeerConnectionImpl* aPc,
                           MediaTransportHandler* aTransportHandler,
                           AbstractThread* aCallThread,
                           nsISerialEventTarget* aStsThread,
                           MediaSessionConduit* aConduit,
                           dom::MediaStreamTrack* aTrack,
                           const Sequence<RTCRtpEncodingParameters>& aEncodings,
                           RTCRtpTransceiver* aTransceiver)
    : mWatchManager(this, AbstractThread::MainThread()),
      mWindow(aWindow),
      mPc(aPc),
      mSenderTrack(aTrack),
      mTransportHandler(aTransportHandler),
      mTransceiver(aTransceiver),
      INIT_CANONICAL(mSsrcs, Ssrcs()),
      INIT_CANONICAL(mVideoRtxSsrcs, Ssrcs()),
      INIT_CANONICAL(mLocalRtpExtensions, RtpExtList()),
      INIT_CANONICAL(mAudioCodec, Nothing()),
      INIT_CANONICAL(mVideoCodec, Nothing()),
      INIT_CANONICAL(mVideoRtpRtcpConfig, Nothing()),
      INIT_CANONICAL(mVideoCodecMode, webrtc::VideoCodecMode::kRealtimeVideo),
      INIT_CANONICAL(mCname, std::string()),
      INIT_CANONICAL(mTransmitting, false),
      INIT_CANONICAL(mFrameTransformerProxy, nullptr),
      INIT_CANONICAL(mVideoDegradationPreference,
                     webrtc::DegradationPreference::DISABLED) {
  mPipeline = MediaPipelineTransmit::Create(
      mPc->GetHandle(), aTransportHandler, aCallThread, aStsThread,
      aConduit->type() == MediaSessionConduit::VIDEO, aConduit);
  mPipeline->InitControl(this);

  if (aConduit->type() == MediaSessionConduit::AUDIO) {
    mDtmf = MakeRefPtr<RTCDTMFSender>(aWindow, mTransceiver);
  }
  mPipeline->SetTrack(mSenderTrack);

  mozilla::glean::rtcrtpsender::count.Add(1);

  if (mPc->ShouldAllowOldSetParameters()) {
    mAllowOldSetParameters = true;
    mozilla::glean::rtcrtpsender::count_setparameters_compat.Add(1);
  }

  if (aEncodings.Length()) {
    // This sender was created by addTransceiver with sendEncodings.
    mParameters.mEncodings = aEncodings;
    mSimulcastEnvelopeSet = true;
    mozilla::glean::rtcrtpsender::used_sendencodings.AddToNumerator(1);
  } else {
    // This sender was created by addTrack, sRD(offer), or addTransceiver
    // without sendEncodings.
    RTCRtpEncodingParameters defaultEncoding;
    defaultEncoding.mActive = true;
    if (aConduit->type() == MediaSessionConduit::VIDEO) {
      defaultEncoding.mScaleResolutionDownBy.Construct(1.0f);
    }
    (void)mParameters.mEncodings.AppendElement(defaultEncoding, fallible);
    UpdateRestorableEncodings(mParameters.mEncodings);
    MaybeGetJsepRids();
  }

  mParameters.mCodecs.Construct();
  UpdateParametersRtcp();
  mParameters.mHeaderExtensions.Construct();
}

#undef INIT_CANONICAL

RTCRtpSender::~RTCRtpSender() = default;

JSObject* RTCRtpSender::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return RTCRtpSender_Binding::Wrap(aCx, this, aGivenProto);
}

RTCDtlsTransport* RTCRtpSender::GetTransport() const {
  if (!mTransceiver) {
    return nullptr;
  }
  return mTransceiver->GetDtlsTransport();
}

RTCDTMFSender* RTCRtpSender::GetDtmf() const { return mDtmf; }

already_AddRefed<Promise> RTCRtpSender::GetStats(ErrorResult& aError) {
  RefPtr<Promise> promise = MakePromise(aError);
  if (aError.Failed()) {
    return nullptr;
  }

  mTransceiver->ChainToDomPromiseWithCodecStats(GetStatsInternal(), promise);
  return promise.forget();
}

nsTArray<RefPtr<dom::RTCStatsPromise>> RTCRtpSender::GetStatsInternal(
    bool aSkipIceStats) {
  MOZ_ASSERT(NS_IsMainThread());
  nsTArray<RefPtr<RTCStatsPromise>> promises(2);
  if (!mPipeline) {
    return promises;
  }

  nsAutoString trackName;
  if (auto track = mPipeline->GetTrack()) {
    track->GetId(trackName);
  }

  std::string mid = mTransceiver->GetMidAscii();
  nsString transportId =
      NS_ConvertASCIItoUTF16(GetJsepTransceiver().mTransport.mTransportId);
  std::map<uint32_t, std::string> videoSsrcToRidMap;
  const auto encodings = mVideoCodec.Ref().andThen(
      [](const auto& aCodec) { return SomeRef(aCodec.mEncodings); });
  if (encodings && !encodings->empty() && encodings->front().rid != "") {
    for (size_t i = 0; i < std::min(mSsrcs.Ref().size(), encodings->size());
         ++i) {
      videoSsrcToRidMap.insert({mSsrcs.Ref()[i], (*encodings)[i].rid});
    }
  }

  {
    // Add bandwidth estimation stats
    promises.AppendElement(InvokeAsync(
        mPipeline->mCallThread, __func__,
        [conduit = mPipeline->mConduit, trackName]() mutable {
          auto report = std::make_unique<dom::RTCStatsCollection>();
          Maybe<webrtc::Call::Stats> stats = conduit->GetCallStats();
          stats.apply([&](const auto aStats) {
            dom::RTCBandwidthEstimationInternal bw;
            bw.mTrackIdentifier = trackName;
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

  const bool isSending = mTransceiver->HasBeenUsedToSend();

  promises.AppendElement(InvokeAsync(
      mPipeline->mCallThread, __func__,
      [pipeline = mPipeline, trackName, mid = std::move(mid),
       transportId = std::move(transportId),
       videoSsrcToRidMap = std::move(videoSsrcToRidMap), isSending,
       audioCodec = mAudioCodec.Ref()] {
        auto report = std::make_unique<dom::RTCStatsCollection>();
        auto asAudio = pipeline->mConduit->AsAudioSessionConduit();
        auto asVideo = pipeline->mConduit->AsVideoSessionConduit();

        nsString kind = asVideo.isNothing() ? u"audio"_ns : u"video"_ns;
        nsString idstr = kind + u"_"_ns;
        idstr.AppendInt(static_cast<uint32_t>(pipeline->Level()));
        const bool isSendStable =
            !pipeline->mConduit->IsShutdown() && isSending;
        for (uint32_t ssrc : pipeline->mConduit->GetLocalSSRCs()) {
          nsString localId = u"outbound_rtp_"_ns + idstr + u"_"_ns;
          localId.AppendInt(ssrc);
          nsString remoteId;
          Maybe<uint16_t> base_seq =
              pipeline->mConduit->RtpSendBaseSeqFor(ssrc);

          auto constructCommonRemoteInboundRtpStats =
              [&](RTCRemoteInboundRtpStreamStats& aRemote,
                  const webrtc::ReportBlockData& aRtcpData) {
                remoteId = u"outbound_rtcp_"_ns + idstr + u"_"_ns;
                remoteId.AppendInt(ssrc);
                aRemote.mTimestamp.Construct(
                    RTCStatsTimestamp::FromNtp(
                        pipeline->GetTimestampMaker(),
                        webrtc::Timestamp::Micros(
                            aRtcpData.report_block_timestamp_utc().us()) +
                            webrtc::TimeDelta::Seconds(webrtc::kNtpJan1970))
                        .ToDom());
                aRemote.mId.Construct(remoteId);
                aRemote.mType.Construct(RTCStatsType::Remote_inbound_rtp);
                aRemote.mSsrc = ssrc;
                aRemote.mKind = kind;
                aRemote.mMediaType.Construct(
                    kind);  // mediaType is the old name for kind.
                aRemote.mLocalId.Construct(localId);
                if (!transportId.IsEmpty()) {
                  aRemote.mTransportId.Construct(transportId);
                }
                if (base_seq) {
                  if (aRtcpData.extended_highest_sequence_number() <
                      *base_seq) {
                    aRemote.mPacketsReceived.Construct(0);
                    aRemote.mPacketsLost.Construct(0);
                  } else {
                    aRemote.mPacketsReceived.Construct(
                        aRtcpData.extended_highest_sequence_number() -
                        aRtcpData.cumulative_lost() - *base_seq + 1);
                    aRemote.mPacketsLost.Construct(aRtcpData.cumulative_lost());
                  }
                }
                if (aRtcpData.has_rtt()) {
                  aRemote.mRoundTripTime.Construct(
                      aRtcpData.last_rtt().template ms<double>() / 1000.0);
                }
                aRemote.mTotalRoundTripTime.Construct(
                    aRtcpData.sum_rtts().template ms<double>() / 1000.0);
                aRemote.mFractionLost.Construct(
                    static_cast<float>(aRtcpData.fraction_lost_raw()) /
                    (1 << 8));
                aRemote.mRoundTripTimeMeasurements.Construct(
                    aRtcpData.num_rtts());
              };

          auto constructCommonOutboundRtpStats =
              [&](RTCOutboundRtpStreamStats& aLocal) {
                aLocal.mSsrc = ssrc;
                aLocal.mTimestamp.Construct(
                    pipeline->GetTimestampMaker().GetNow().ToDom());
                aLocal.mId.Construct(localId);
                aLocal.mType.Construct(RTCStatsType::Outbound_rtp);
                aLocal.mKind = kind;
                aLocal.mMediaType.Construct(
                    kind);  // mediaType is the old name for kind.
                if (remoteId.Length()) {
                  aLocal.mRemoteId.Construct(remoteId);
                }
                if (!mid.empty()) {
                  aLocal.mMid.Construct(NS_ConvertUTF8toUTF16(mid).get());
                }
                if (!transportId.IsEmpty()) {
                  aLocal.mTransportId.Construct(transportId);
                }
              };

          auto constructCommonMediaSourceStats =
              [&](RTCMediaSourceStats& aStats) {
                nsString id = u"mediasource_"_ns + idstr + trackName;
                aStats.mTimestamp.Construct(
                    pipeline->GetTimestampMaker().GetNow().ToDom());
                aStats.mId.Construct(id);
                aStats.mType.Construct(RTCStatsType::Media_source);
                aStats.mTrackIdentifier = trackName;
                aStats.mKind = kind;
              };

          asAudio.apply([&](auto& aConduit) {
            if (!isSendStable) {
              // https://www.w3.org/TR/webrtc-stats/#the-rtp-statistics-hierarchy
              // The lifetime of all RTP monitored objects are tied to SSRCs.
              // The RTCOutboundRtpStreamStats is created when the RTP sender
              // is configured by setting a local or remote SDP answer (via
              // setLocalDescription() or setRemoteDescription() and the
              // signaling state returning to "stable").
              return;
            }

            Maybe<webrtc::AudioSendStream::Stats> audioStats =
                aConduit->GetSenderStats();

            // First, fill in remote stat with rtcp receiver data, if present.
            // ReceiverReports have less information than SenderReports, so fill
            // in what we can.
            if (audioStats) {
              Maybe<webrtc::ReportBlockData> reportBlockData;
              for (auto& data : audioStats->report_block_datas) {
                if (data.source_ssrc() == ssrc) {
                  reportBlockData.emplace(data);
                  break;
                }
              }
              reportBlockData.apply([&](auto& aReportBlockData) {
                RTCRemoteInboundRtpStreamStats remote;
                constructCommonRemoteInboundRtpStats(remote, aReportBlockData);
                if (aReportBlockData.jitter() >= 0 && audioCodec) {
                  remote.mJitter.Construct(
                      aReportBlockData.jitter(audioCodec->mFreq)
                          .template ms<double>() /
                      1000.0);
                }
                if (!report->mRemoteInboundRtpStreamStats.AppendElement(
                        std::move(remote), fallible)) {
                  mozalloc_handle_oom(0);
                }
              });
            }

            // Then, fill in local side (with cross-link to remote only if
            // present). Counters that require conduit data default to 0 until
            // the conduit begins reporting.
            // https://www.w3.org/TR/webrtc-stats/#guidelines-for-implementing-stats-objects
            // When a feature is a cumulative measure and it has not been
            // sampled yet, but is likely to increment in the future, report a
            // count of zero.
            RTCOutboundRtpStreamStats local;
            constructCommonOutboundRtpStats(local);
            if (audioStats) {
              local.mPacketsSent.Construct(audioStats->packets_sent);
              local.mBytesSent.Construct(audioStats->payload_bytes_sent);
              local.mNackCount.Construct(
                  audioStats->rtcp_packet_type_counts.nack_packets);
              local.mHeaderBytesSent.Construct(
                  audioStats->header_and_padding_bytes_sent);
              local.mRetransmittedPacketsSent.Construct(
                  audioStats->retransmitted_packets_sent);
              local.mRetransmittedBytesSent.Construct(
                  audioStats->retransmitted_bytes_sent);
              if (audioStats->target_bitrate_bps > 0) {
                local.mTargetBitrate.Construct(
                    static_cast<double>(audioStats->target_bitrate_bps));
              }
            } else {
              local.mPacketsSent.Construct(0);
              local.mBytesSent.Construct(0);
              local.mNackCount.Construct(0);
              local.mHeaderBytesSent.Construct(0);
              local.mRetransmittedPacketsSent.Construct(0);
              local.mRetransmittedBytesSent.Construct(0);
            }
            if (!report->mOutboundRtpStreamStats.AppendElement(std::move(local),
                                                               fallible)) {
              mozalloc_handle_oom(0);
            }

            // TODO(bug 1804678): Use RTCAudioSourceStats
            if (audioStats) {
              RTCMediaSourceStats mediaSourceStats;
              constructCommonMediaSourceStats(mediaSourceStats);
              if (!report->mMediaSourceStats.AppendElement(
                      std::move(mediaSourceStats), fallible)) {
                mozalloc_handle_oom(0);
              }
            }
          });

          asVideo.apply([&](auto& aConduit) {
            if (!isSendStable) {
              // https://www.w3.org/TR/webrtc-stats/#the-rtp-statistics-hierarchy
              // The lifetime of all RTP monitored objects are tied to SSRCs.
              // The RTCOutboundRtpStreamStats is created when the RTP sender
              // is configured by setting a local or remote SDP answer (via
              // setLocalDescription() or setRemoteDescription() and the
              // signaling state returning to "stable").
              return;
            }

            Maybe<webrtc::VideoSendStream::Stats> videoStats =
                aConduit->GetSenderStats();

            Maybe<webrtc::VideoSendStream::StreamStats> streamStats;
            if (videoStats) {
              auto kv = videoStats->substreams.find(ssrc);
              if (kv != videoStats->substreams.end()) {
                streamStats = Some(kv->second);
              }
            }

            if (streamStats) {
              aConduit->GetAssociatedLocalRtxSSRC(ssrc).apply(
                  [&](const auto rtxSsrc) {
                    auto kv = videoStats->substreams.find(rtxSsrc);
                    if (kv != videoStats->substreams.end()) {
                      streamStats->rtp_stats.Add(kv->second.rtp_stats);
                    }
                  });

              // First, fill in remote stat with rtcp receiver data, if present.
              // ReceiverReports have less information than SenderReports, so
              // fill in what we can.
              if (streamStats->report_block_data) {
                const webrtc::ReportBlockData& rtcpReportData =
                    *streamStats->report_block_data;
                RTCRemoteInboundRtpStreamStats remote;
                remote.mJitter.Construct(
                    static_cast<double>(rtcpReportData.jitter()) /
                    webrtc::kVideoPayloadTypeFrequency);
                constructCommonRemoteInboundRtpStats(remote, rtcpReportData);
                if (!report->mRemoteInboundRtpStreamStats.AppendElement(
                        std::move(remote), fallible)) {
                  mozalloc_handle_oom(0);
                }
              }
            }

            // Then, fill in local side (with cross-link to remote only if
            // present). Non-counter fields that require conduit data are only
            // filled in once the conduit reports substream stats.
            //
            // https://www.w3.org/TR/webrtc-stats/#guidelines-for-implementing-stats-objects
            // When a feature is a cumulative measure and it has not been
            // sampled yet, but is likely to increment in the future, report a
            // count of zero.
            RTCOutboundRtpStreamStats local;
            constructCommonOutboundRtpStats(local);
            if (auto it = videoSsrcToRidMap.find(ssrc);
                it != videoSsrcToRidMap.end() && it->second != "") {
              local.mRid.Construct(NS_ConvertUTF8toUTF16(it->second).get());
            }
            if (streamStats) {
              local.mPacketsSent.Construct(
                  streamStats->rtp_stats.transmitted.packets);
              local.mBytesSent.Construct(
                  streamStats->rtp_stats.transmitted.payload_bytes);
              local.mNackCount.Construct(
                  streamStats->rtcp_packet_type_counts.nack_packets);
              local.mFirCount.Construct(
                  streamStats->rtcp_packet_type_counts.fir_packets);
              local.mPliCount.Construct(
                  streamStats->rtcp_packet_type_counts.pli_packets);
              local.mFramesEncoded.Construct(streamStats->frames_encoded);
              local.mKeyFramesEncoded.Construct(
                  streamStats->frame_counts.key_frames);
              if (streamStats->qp_sum) {
                local.mQpSum.Construct(*streamStats->qp_sum);
              }
              local.mHeaderBytesSent.Construct(
                  streamStats->rtp_stats.transmitted.header_bytes +
                  streamStats->rtp_stats.transmitted.padding_bytes);
              local.mRetransmittedPacketsSent.Construct(
                  streamStats->rtp_stats.retransmitted.packets);
              local.mRetransmittedBytesSent.Construct(
                  streamStats->rtp_stats.retransmitted.payload_bytes);
              local.mFrameWidth.Construct(streamStats->width);
              local.mFrameHeight.Construct(streamStats->height);
              local.mFramesPerSecond.Construct(streamStats->encode_frame_rate);
              local.mFramesSent.Construct(streamStats->frames_encoded);
              local.mHugeFramesSent.Construct(streamStats->huge_frames_sent);
              local.mTotalEncodeTime.Construct(
                  double(streamStats->total_encode_time_ms) / 1000.);
              if (streamStats->target_bitrate) {
                local.mTargetBitrate.Construct(
                    streamStats->target_bitrate->bps<double>());
              }
            } else {
              local.mPacketsSent.Construct(0);
              local.mBytesSent.Construct(0);
              local.mNackCount.Construct(0);
              local.mFirCount.Construct(0);
              local.mPliCount.Construct(0);
              local.mFramesEncoded.Construct(0);
              local.mKeyFramesEncoded.Construct(0);
              local.mHeaderBytesSent.Construct(0);
              local.mRetransmittedPacketsSent.Construct(0);
              local.mRetransmittedBytesSent.Construct(0);
              local.mFramesSent.Construct(0);
              local.mHugeFramesSent.Construct(0);
              local.mTotalEncodeTime.Construct(0);
            }
            aConduit->GetAssociatedLocalRtxSSRC(ssrc).apply(
                [&](const auto rtxSsrc) { local.mRtxSsrc.Construct(rtxSsrc); });
            if (!report->mOutboundRtpStreamStats.AppendElement(std::move(local),
                                                               fallible)) {
              mozalloc_handle_oom(0);
            }

            if (videoStats) {
              RTCVideoSourceStats videoSourceStats;
              constructCommonMediaSourceStats(videoSourceStats);
              // webrtc::VideoSendStream::Stats does not have width/height. We
              // might be able to get this somewhere else?
              // videoStats->frames is not documented, but looking at the
              // implementation it appears to be the number of frames inputted
              // to the encoder, which ought to work.
              videoSourceStats.mFrames.Construct(videoStats->frames);
              videoSourceStats.mFramesPerSecond.Construct(
                  videoStats->input_frame_rate);
              auto resolution = aConduit->GetLastResolution();
              resolution.apply([&](const auto& aResolution) {
                videoSourceStats.mWidth.Construct(aResolution.width);
                videoSourceStats.mHeight.Construct(aResolution.height);
              });
              if (!report->mVideoSourceStats.AppendElement(
                      std::move(videoSourceStats), fallible)) {
                mozalloc_handle_oom(0);
              }
            }
          });
        }

        return RTCStatsPromise::CreateAndResolve(std::move(report), __func__);
      }));

  if (!aSkipIceStats && GetJsepTransceiver().mTransport.mComponents) {
    promises.AppendElement(mTransportHandler->GetIceStats(
        GetJsepTransceiver().mTransport.mTransportId,
        mPipeline->GetTimestampMaker().GetNow().ToDom()));
  }

  return promises;
}

void RTCRtpSender::GetCapabilities(const GlobalObject&, const nsAString& aKind,
                                   Nullable<dom::RTCRtpCapabilities>& aResult) {
  PeerConnectionImpl::GetCapabilities(aKind, aResult, sdp::Direction::kSend);
}

void RTCRtpSender::WarnAboutBadSetParameters(const nsCString& aError) {
  nsCString warning(
      "WARNING! Invalid setParameters call detected! The good news? Firefox "
      "supports sendEncodings in addTransceiver now, so we ask that you switch "
      "over to using the parameters code you use for other browsers. Thank you "
      "for your patience and support. The specific error was: ");
  warning += aError;
  mPc->SendWarningToConsole(warning);
}

nsCString RTCRtpSender::GetEffectiveTLDPlus1() const {
  return mPc->GetEffectiveTLDPlus1();
}

already_AddRefed<Promise> RTCRtpSender::SetParameters(
    const dom::RTCRtpSendParameters& aParameters,
    const dom::RTCSetParameterOptions& aSetParameterOptions,
    ErrorResult& aError) {
  dom::RTCRtpSendParameters paramsCopy(aParameters);
  // When the setParameters method is called, the user agent MUST run the
  // following steps:
  // Let parameters be the method's first argument.
  // Let sender be the RTCRtpSender object on which setParameters is invoked.
  // Let transceiver be the RTCRtpTransceiver object associated with sender
  // (i.e.sender is transceiver.[[Sender]]).

  RefPtr<dom::Promise> p = MakePromise(aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (mPc->IsClosed()) {
    p->MaybeRejectWithInvalidStateError("Peer connection is closed");
    return p.forget();
  }

  // If transceiver.[[Stopping]] is true, return a promise rejected with a newly
  // created InvalidStateError.
  if (mTransceiver->Stopping()) {
    p->MaybeRejectWithInvalidStateError(
        "This sender's transceiver is stopping/stopped");
    return p.forget();
  }

  // If sender.[[LastReturnedParameters]] is null, return a promise rejected
  // with a newly created InvalidStateError.
  if (!mLastReturnedParameters.isSome()) {
    nsCString error;
    if (mLastTransactionId.isSome() && paramsCopy.mTransactionId.WasPassed() &&
        *mLastTransactionId == paramsCopy.mTransactionId.Value()) {
      error =
          "Event loop was relinquished between getParameters and setParameters "
          "calls";
    } else {
      error = "Cannot call setParameters without first calling getParameters";
    }

    if (mAllowOldSetParameters) {
      if (!mHaveWarnedBecauseNoGetParameters) {
        mHaveWarnedBecauseNoGetParameters = true;
        mozilla::glean::rtcrtpsender_setparameters::warn_no_getparameters
            .AddToNumerator(1);
      }
      WarnAboutBadSetParameters(error);
    } else {
      if (!mHaveFailedBecauseNoGetParameters) {
        mHaveFailedBecauseNoGetParameters = true;
        mozilla::glean::rtcrtpsender_setparameters::fail_no_getparameters
            .AddToNumerator(1);
      }
      p->MaybeRejectWithInvalidStateError(error);
      return p.forget();
    }
  }

  // According to the spec, our consistency checking is based on
  // [[LastReturnedParameters]], but if we're letting
  // [[LastReturnedParameters]]==null slide, we still want to do
  // consistency checking on _something_ so we can warn implementers if they
  // are messing that up also. Just find something, _anything_, to do that
  // checking with.
  // TODO(bug 1803388): Remove this stuff once it is no longer needed.
  // TODO(bug 1803389): Remove the glean errors once they are no longer needed.
  Maybe<RTCRtpSendParameters> oldParams;
  if (mAllowOldSetParameters) {
    if (mLastReturnedParameters.isSome()) {
      oldParams = mLastReturnedParameters;
    } else if (mPendingParameters.isSome()) {
      oldParams = mPendingParameters;
    } else {
      oldParams = Some(mParameters);
    }
    MOZ_ASSERT(oldParams.isSome());
  } else {
    oldParams = mLastReturnedParameters;
  }
  MOZ_ASSERT(oldParams.isSome());

  // Validate parameters by running the following steps:
  // Let encodings be parameters.encodings.
  // Let codecs be parameters.codecs.
  // Let N be the number of RTCRtpEncodingParameters stored in
  // sender.[[SendEncodings]].
  // If any of the following conditions are met,
  // return a promise rejected with a newly created InvalidModificationError:

  bool pendingRidChangeFromCompatMode = false;
  // encodings.length is different from N.
  if (paramsCopy.mEncodings.Length() != oldParams->mEncodings.Length()) {
    nsCString error("Cannot change the number of encodings with setParameters");
    if (!mAllowOldSetParameters) {
      if (!mHaveFailedBecauseEncodingCountChange) {
        mHaveFailedBecauseEncodingCountChange = true;
        mozilla::glean::rtcrtpsender_setparameters::fail_length_changed
            .AddToNumerator(1);
      }
      p->MaybeRejectWithInvalidModificationError(error);
      return p.forget();
    }
    // Make sure we don't use the old rids in SyncToJsep while we wait for the
    // queued task below to update mParameters.
    pendingRidChangeFromCompatMode = true;
    mSimulcastEnvelopeSet = true;
    if (!mHaveWarnedBecauseEncodingCountChange) {
      mHaveWarnedBecauseEncodingCountChange = true;
      mozilla::glean::rtcrtpsender_setparameters::warn_length_changed
          .AddToNumerator(1);
    }
    WarnAboutBadSetParameters(error);
  } else {
    // encodings has been re-ordered.
    for (size_t i = 0; i < paramsCopy.mEncodings.Length(); ++i) {
      const auto& oldEncoding = oldParams->mEncodings[i];
      const auto& newEncoding = paramsCopy.mEncodings[i];
      if (oldEncoding.mRid != newEncoding.mRid) {
        nsCString error("Cannot change rid, or reorder encodings");
        if (!mHaveFailedBecauseRidChange) {
          mHaveFailedBecauseRidChange = true;
          mozilla::glean::rtcrtpsender_setparameters::fail_rid_changed
              .AddToNumerator(1);
        }
        p->MaybeRejectWithInvalidModificationError(error);
        return p.forget();
      }
    }
  }

  // TODO(bug 1803388): Handle this in webidl, once we stop allowing the old
  // setParameters style.
  if (!paramsCopy.mTransactionId.WasPassed()) {
    nsCString error("transactionId is not set!");
    if (!mAllowOldSetParameters) {
      if (!mHaveFailedBecauseNoTransactionId) {
        mHaveFailedBecauseNoTransactionId = true;
        mozilla::glean::rtcrtpsender_setparameters::fail_no_transactionid
            .AddToNumerator(1);
      }
      p->MaybeRejectWithTypeError(error);
      return p.forget();
    }
    if (!mHaveWarnedBecauseNoTransactionId) {
      mHaveWarnedBecauseNoTransactionId = true;
      mozilla::glean::rtcrtpsender_setparameters::warn_no_transactionid
          .AddToNumerator(1);
    }
    WarnAboutBadSetParameters(error);
  } else if (oldParams->mTransactionId.WasPassed() &&
             oldParams->mTransactionId != paramsCopy.mTransactionId) {
    // Any parameter in parameters is marked as a Read-only parameter (such as
    // RID) and has a value that is different from the corresponding parameter
    // value in sender.[[LastReturnedParameters]]. Note that this also applies
    // to transactionId.
    // Don't throw this error if letting the "stale getParameters" case slide.
    nsCString error(
        "Cannot change transaction id: call getParameters, modify the result, "
        "and then call setParameters");
    if (!mHaveFailedBecauseStaleTransactionId) {
      mHaveFailedBecauseStaleTransactionId = true;
      mozilla::glean::rtcrtpsender_setparameters::fail_stale_transactionid
          .AddToNumerator(1);
    }
    p->MaybeRejectWithInvalidModificationError(error);
    return p.forget();
  }

  // This could conceivably happen if we are allowing the old setParameters
  // behavior.
  if (!paramsCopy.mEncodings.Length()) {
    nsCString error("Cannot set an empty encodings array");
    if (!mAllowOldSetParameters) {
      if (!mHaveFailedBecauseNoEncodings) {
        mHaveFailedBecauseNoEncodings = true;
        mozilla::glean::rtcrtpsender_setparameters::fail_no_encodings
            .AddToNumerator(1);
      }

      p->MaybeRejectWithInvalidModificationError(error);
      return p.forget();
    }
    // TODO: Add some warning telemetry here
    WarnAboutBadSetParameters(error);
    // Just don't do this; it's stupid.
    paramsCopy.mEncodings = oldParams->mEncodings;
  }

  if (!(oldParams->mCodecs == paramsCopy.mCodecs)) {
    nsCString error("RTCRtpParameters.codecs is a read-only parameter");
    if (!mAllowOldSetParameters) {
      p->MaybeRejectWithInvalidModificationError(error);
      return p.forget();
    }
    WarnAboutBadSetParameters(error);
  }

  // Coverts a list of JsepCodecDescription to a list of
  // dom::RTCRtpCodecParameters
  auto toDomCodecParametersList =
      [](const nsTArray<UniquePtr<JsepCodecDescription>>& aJsepCodec)
      -> dom::Sequence<RTCRtpCodecParameters> {
    dom::Sequence<RTCRtpCodecParameters> codecs;
    for (const auto& codec : aJsepCodec) {
      if (codec) {
        auto type = codec->Type();
        std::string typeStr;
        switch (type) {
          case SdpMediaSection::MediaType::kApplication:
            typeStr = "application";
            break;
          case SdpMediaSection::MediaType::kAudio:
            typeStr = "audio";
            break;
          case SdpMediaSection::MediaType::kVideo:
            typeStr = "video";
            break;
          case SdpMediaSection::MediaType::kMessage:
            typeStr = "message";
            break;
          case SdpMediaSection::MediaType::kText:
            typeStr = "text";
            break;
          default:
            MOZ_CRASH("Unexpected SdpMediaSection::MediaType");
        };
        if (typeStr == "audio" || typeStr == "video") {
          dom::RTCRtpCodecParameters domCodec;
          RTCRtpTransceiver::ToDomRtpCodecParameters(*codec, &domCodec);
          (void)codecs.AppendElement(domCodec, fallible);
        }
      }
    }
    return codecs;
  };

  if (mLastReturnedParameters.isSome() &&
      paramsCopy.mTransactionId.WasPassed()) {
    // We check mLastReturnedParameters and presence of transaction id because
    // if these aren't right and we're letting it slide, we do not expect these
    // read-only members to be present.

    if (oldParams->mRtcp != paramsCopy.mRtcp) {
      p->MaybeRejectWithInvalidModificationError(
          "RTCRtpParameters.rtcp is a read-only parameter");
      return p.forget();
    }

    if (oldParams->mHeaderExtensions != paramsCopy.mHeaderExtensions) {
      p->MaybeRejectWithInvalidModificationError(
          "RTCRtpParameters.headerExtensions is a read-only parameter");
      return p.forget();
    }
  }

  // CheckAndRectifyEncodings handles the following steps:
  // If transceiver kind is "audio", remove the scaleResolutionDownBy member
  // from all encodings that contain one.
  //
  // If transceiver kind is "video", and any encoding in encodings contains a
  // scaleResolutionDownBy member whose value is less than 1.0, return a
  // promise rejected with a newly created RangeError.
  //
  // Verify that each encoding in encodings has a maxFramerate member whose
  // value is greater than or equal to 0.0. If one of the maxFramerate values
  // does not meet this requirement, return a promise rejected with a newly
  // created RangeError.

  auto choosableCodecs =
      mParameters.mCodecs.WasPassed() && mParameters.mCodecs.Value().Length()
          ? mParameters.mCodecs.Value()
          : Sequence<RTCRtpCodecParameters>();
  if (choosableCodecs.Length() == 0) {
    // If choosableCodecs is still an empty list, set choosableCodecs to the
    // list of implemented send codecs for transceiver's kind.
    AutoTArray<UniquePtr<JsepCodecDescription>, 16> codecs;
    if (mTransceiver->IsVideo()) {
      EnumerateDefaultVideoCodecs(&codecs, mPc->mPrefs);
    } else {
      EnumerateDefaultAudioCodecs(&codecs, mPc->mPrefs);
    }
    choosableCodecs = toDomCodecParametersList(codecs);
  }
  ErrorResult rv;
  CheckAndRectifyEncodings(paramsCopy.mEncodings, mTransceiver->IsVideo(),
                           dom::Optional(choosableCodecs), true, false,
                           MatchGetCapabilities::NO, rv);
  if (rv.Failed()) {
    if (!mHaveFailedBecauseOtherError) {
      mHaveFailedBecauseOtherError = true;
      mozilla::glean::rtcrtpsender_setparameters::fail_other.AddToNumerator(1);
    }
    p->MaybeReject(std::move(rv));
    return p.forget();
  }

  // https://w3c.github.io/webrtc-extensions/#rtcrtpsender-setparameters-keyframe
  // If the transceiver kind of the associated transceiver is "audio", set
  // setParameterOptions.encodingOptions to the empty list.
  const auto& encodingOptions = aSetParameterOptions.mEncodingOptions;
  const bool useEncodingOptions =
      mTransceiver->IsVideo() && !encodingOptions.IsEmpty();
  // If setParameterOptions.encodingOptions is not empty and
  // setParameterOptions.encodingOptions.length is different from
  // parameters.encodings.length, return a promise rejected with a newly
  // created InvalidModificationError.
  if (useEncodingOptions &&
      encodingOptions.Length() != paramsCopy.mEncodings.Length()) {
    p->MaybeRejectWithInvalidModificationError(
        "encodingOptions must have the same length as encodings");
    return p.forget();
  }

  // If transceiver kind is "video", then for each encoding in encodings that
  // doesn't contain a scaleResolutionDownBy member, add a
  // scaleResolutionDownBy member with the value 1.0.
  if (mTransceiver->IsVideo()) {
    for (auto& encoding : paramsCopy.mEncodings) {
      if (!encoding.mScaleResolutionDownBy.WasPassed()) {
        encoding.mScaleResolutionDownBy.Construct(1.0);
      }
    }
  }

  // Let p be a new promise. (see above)

  // In parallel, configure the media stack to use parameters to transmit
  // sender.[[SenderTrack]].
  // Right now this is infallible. That may change someday.

  // We need to put this in a member variable, since MaybeUpdateConduit needs it
  // This also allows PeerConnectionImpl to detect when there is a pending
  // setParameters, which has implcations for the handling of
  // setRemoteDescription.
  mPendingRidChangeFromCompatMode = pendingRidChangeFromCompatMode;
  mPendingParameters = Some(paramsCopy);
  uint32_t serialNumber = ++mNumSetParametersCalls;
  MaybeUpdateConduit();

  // If we have a degradation value passed convert it from a
  // dom::RTCDegradationPreference to a webrtc::DegradationPreference and set
  // mVideoDegradationPreference which will trigger the onconfigure change in
  // the videoConduit.
  if (paramsCopy.mDegradationPreference.WasPassed()) {
    const auto degradationPreference = [&] {
      switch (paramsCopy.mDegradationPreference.Value()) {
        case mozilla::dom::RTCDegradationPreference::Balanced:
          return webrtc::DegradationPreference::BALANCED;
        case mozilla::dom::RTCDegradationPreference::Maintain_framerate:
          return webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
        case mozilla::dom::RTCDegradationPreference::Maintain_resolution:
          return webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
      }
      MOZ_CRASH("Unexpected RTCDegradationPreference");
    };
    mVideoDegradationPreference = degradationPreference();
  } else {
    // Default to disabled when unset to allow for correct degradation
    mVideoDegradationPreference = webrtc::DegradationPreference::DISABLED;
  }

  // For each setParameterOptions.encodingOptions indexed by i, if
  // setParameterOptions.encodingOptions[i].keyFrame is set to true, request
  // that the encoder associated with parameters.encodings[i] generates a key
  // frame.
  Maybe<std::vector<std::string>> keyFrameRids;
  if (useEncodingOptions) {
    for (size_t i = 0; i < encodingOptions.Length(); ++i) {
      if (!encodingOptions[i].mKeyFrame) {
        continue;
      }
      if (!keyFrameRids) {
        keyFrameRids.emplace();
      }
      if (paramsCopy.mEncodings[i].mRid.WasPassed()) {
        keyFrameRids->push_back(
            NS_ConvertUTF16toUTF8(paramsCopy.mEncodings[i].mRid.Value()).get());
      }
    }
  }

  // If the media stack is successfully configured with parameters,
  // queue a task to run the following steps:
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<RTCRtpSender>(this), p,
                 paramsCopy = std::move(paramsCopy), serialNumber,
                 keyFrameRids = std::move(keyFrameRids)]() mutable {
        // Requested here so it reaches the call thread after the new config.
        if (keyFrameRids && mPipeline) {
          mPipeline->mConduit->AsVideoSessionConduit().apply(
              [&](const auto& conduit) {
                conduit->GenerateKeyFrames(std::move(*keyFrameRids));
              });
        }
        // Set sender.[[LastReturnedParameters]] to null.
        mLastReturnedParameters = Nothing();
        // Set sender.[[SendEncodings]] to parameters.encodings.
        mParameters.mEncodings = std::move(paramsCopy.mEncodings);
        UpdateRestorableEncodings(mParameters.mEncodings);
        // If we had a valid degradation preference passed in store it in
        // mParameters so we can return it if needed via GetParameters calls.
        mParameters.mDegradationPreference.Reset();
        if (paramsCopy.mDegradationPreference.WasPassed()) {
          mParameters.mDegradationPreference.Construct(
              paramsCopy.mDegradationPreference.Value());
        }
        // Only clear mPendingParameters if it matches; there could have been
        // back-to-back calls to setParameters, and we only want to clear this
        // if no subsequent setParameters is pending.
        if (serialNumber == mNumSetParametersCalls) {
          mPendingParameters = Nothing();
          // Ok, nothing has called SyncToJsep while this async task was
          // pending. No need for special handling anymore.
          mPendingRidChangeFromCompatMode = false;
        }
        MOZ_ASSERT(mParameters.mEncodings.Length());
        // Resolve p with undefined.
        p->MaybeResolveWithUndefined();
      }));

  // Return p.
  return p.forget();
}

using FmtpParamKey = nsString;
using FmtpParamValue = nsString;

// Helper type for the codec dictionary matching functions.
// This stores the level & (h264) subprofile separately from the parameter set.
// Which parameters are considered part of the level is codec-specific.
// All of the matching logic is kept out of this struct, instead it all lives
// in the DoesCodecParameterMatchCodec function. This is to make it easier to
// read against the spec.
struct ParametersAndLevel {
  Maybe<std::set<std::tuple<FmtpParamKey, FmtpParamValue>>> mSet = Nothing();
  Maybe<uint32_t> mLevel = Nothing();
  Maybe<uint32_t> mSubprofile = Nothing();
  Maybe<uint32_t> mProfile = Nothing();

  // Helper function to get the default level for a codec.
  static Maybe<uint32_t> DefaultLevelForCodec(const nsString& aMimeType) {
    // AV1 has a defined default level-idx of 5, which is omittable by spec.
    if (aMimeType.LowerCaseEqualsASCII("video/av1")) {
      return Some(5);
    }
    // https://datatracker.ietf.org/doc/html/rfc6184 defines a default value
    // for this parameter, 0x420010.
    if (aMimeType.LowerCaseEqualsASCII("video/h264")) {
      return Some(JsepVideoCodecDescription::GetSaneH264Level(0x420010));
    }
    // VP8 and VP9 are not defined to have a level parameter. Though they do
    // have a profile-id, which doesn't seem to be used in our negotiation.
    if (aMimeType.LowerCaseEqualsASCII("video/vp8") ||
        aMimeType.LowerCaseEqualsASCII("video/vp9")) {
      return Nothing();  // A little pedantic...
    }
    return Nothing();
  }

  static Maybe<uint32_t> DefaultSubprofileForCodec(const nsString& aMimeType) {
    // See DefaultLevelCodec for comments on the default level.
    if (aMimeType.LowerCaseEqualsASCII("video/h264")) {
      return Some(JsepVideoCodecDescription::GetSubprofile(0x420010));
    }
    return Nothing();
  }

  // AV1 has a defined default profile of 0, which is omittable by spec.
  // https://aomediacodec.github.io/av1-rtp-spec/#72-sdp-parameters
  static Maybe<uint32_t> DefaultProfileForCodec(const nsString& aMimeType) {
    if (aMimeType.LowerCaseEqualsASCII("video/av1")) {
      return Some(0);
    }
    return Nothing();
  }

  // A helper function to extract the level from a parameter set in a codec-
  // specific way. If the level cannot be extracted, Nothing() is returned.
  static Maybe<uint32_t> ExtractLevel(const nsString& aMimeType,
                                      const FmtpParamKey& aKey,
                                      const FmtpParamValue& aValue) {
    if (aMimeType.LowerCaseEqualsASCII("video/h264") &&
        aKey.LowerCaseEqualsASCII("profile-level-id")) {
      // The level is the last two characters of the value.
      nsresult rv;
      // Note the radix
      auto val = aValue.ToUnsignedInteger(&rv, 16);
      if (NS_FAILED(rv)) {
        return Nothing();
      }
      return Some(JsepVideoCodecDescription::GetSaneH264Level(val));
    }
    if (aMimeType.LowerCaseEqualsASCII("video/av1") &&
        aKey.EqualsLiteral("level-idx")) {
      nsresult rv;
      auto val = aValue.ToUnsignedInteger(&rv);
      if (NS_FAILED(rv)) {
        return Nothing();
      }
      return Some(val);
    }
    return Nothing();
  }

  // Helper function to get the subprofile for a codec.
  static Maybe<uint32_t> ExtractSubprofile(const nsString& aMimeType,
                                           const FmtpParamKey& aKey,
                                           const FmtpParamValue& aValue) {
    if (aMimeType.LowerCaseEqualsASCII("video/h264") &&
        aKey.EqualsLiteral("profile-level-id")) {
      nsresult rv;
      auto val = aValue.ToUnsignedInteger(&rv, 16);
      if (NS_FAILED(rv)) {
        return Nothing();
      }
      return Some(JsepVideoCodecDescription::GetSubprofile(val));
    }
    return Nothing();
  }

  // AV1's "profile" is, like level, an asymmetric/declarative parameter (see
  // https://aomediacodec.github.io/av1-rtp-spec/#723-usage-with-the-sdp-offeranswer-model),
  // but unlike H264, it's a separate fmtp key from the level ("level-idx"),
  // not encoded together with it. So it needs its own extraction, independent
  // of ExtractLevel.
  static Maybe<uint32_t> ExtractProfile(const nsString& aMimeType,
                                        const FmtpParamKey& aKey,
                                        const FmtpParamValue& aValue) {
    if (aMimeType.LowerCaseEqualsASCII("video/av1") &&
        aKey.EqualsLiteral("profile")) {
      nsresult rv;
      auto val = aValue.ToUnsignedInteger(&rv);
      if (NS_FAILED(rv)) {
        return Nothing();
      }
      return Some(val);
    }
    return Nothing();
  }
};

// We can not directly compare H264 or AV1 FMTP parameter sets, since the level
// and subprofile information must be treated separately as a hierarchical
// value.
//  So we need to separate the regular parameters from profile-level-id for
// H264, and levelidx for AV1. This is done by parsing the FMTP line into a set
// of key-value pairs and a level/subprofile value. If the FMTP line is not in
// a key-value pair format, then we return an empty parameter set.
ParametersAndLevel FmtpToParametersAndLevel(const nsString& aMimeType,
                                            const nsString& aFmtp) {
  auto resultParams = std::set<std::tuple<FmtpParamKey, FmtpParamValue>>();
  Maybe<uint32_t> resultLevel = Nothing();
  Maybe<uint32_t> resultSubprofile = Nothing();
  Maybe<uint32_t> resultProfile = Nothing();
  nsTArray<nsString> parts;
  for (const auto& kvp : aFmtp.Split(';')) {
    auto parts = nsTArray<nsString>();
    for (const auto& part : kvp.Split('=')) {
      parts.AppendElement(part);
    }
    // To be a valid key-value pair, there must be exactly two parts.
    if (parts.Length() == 2) {
      // Check to see if it is the level parameter.
      auto level =
          ParametersAndLevel::ExtractLevel(aMimeType, parts[0], parts[1]);
      // Check to see if it is AV1's profile parameter. Unlike H264's
      // subprofile, this is a separate key from the level, not encoded
      // together with it, so it's checked independently.
      auto profile =
          ParametersAndLevel::ExtractProfile(aMimeType, parts[0], parts[1]);
      if (level.isSome()) {
        // It is the level parameter, so we do not store it in the result
        // parameter set. Instead we store it in the result level, and
        // subprofile (if provided).
        resultSubprofile = ParametersAndLevel::ExtractSubprofile(
            aMimeType, parts[0], parts[1]);
        // Store the level separately
        resultLevel = level;
      } else if (profile.isSome()) {
        // It is AV1's profile parameter, so store it separately too.
        resultProfile = profile;
      } else {
        // It is a regular parameter.
        // We store the key-value pair in the result parameter set.
        resultParams.insert(std::make_tuple(parts[0], parts[1]));
      }
    } else {
      // This is not a valid key-value pair FMTP line, so we do not have
      // parameters.
      return ParametersAndLevel{
          .mSet = Nothing(),
          .mLevel = resultLevel.orElse([&]() -> Maybe<uint32_t> {
            return ParametersAndLevel::DefaultLevelForCodec(aMimeType);
          }),
          .mSubprofile = resultSubprofile,
          .mProfile = resultProfile.orElse([&]() -> Maybe<uint32_t> {
            return ParametersAndLevel::DefaultProfileForCodec(aMimeType);
          }),
      };
    }
  }
  return ParametersAndLevel{
      .mSet = Some(resultParams),
      .mLevel = resultLevel.orElse([&]() -> Maybe<uint32_t> {
        return ParametersAndLevel::DefaultLevelForCodec(aMimeType);
      }),
      .mSubprofile = resultSubprofile,
      .mProfile = resultProfile.orElse([&]() -> Maybe<uint32_t> {
        return ParametersAndLevel::DefaultProfileForCodec(aMimeType);
      }),
  };
}

bool DoesCodecParameterMatchCodec(const RTCRtpCodec& aCodec1,
                                  const RTCRtpCodec& aCodec2,
                                  const bool aIgnoreLevels) {
  const auto compare = [](const nsString& aStr1, const nsString& aStr2) {
    return NS_LossyConvertUTF16toASCII(aStr1).EqualsIgnoreCase(
        NS_LossyConvertUTF16toASCII(aStr2).get());
  };
  if (!compare(aCodec1.mMimeType, aCodec2.mMimeType)) {
    return false;
  }
  if (aCodec1.mClockRate != aCodec2.mClockRate) {
    return false;
  }

  if (aCodec1.mChannels != aCodec2.mChannels) {
    return false;
  }
  // To match both or neither should have a sdpFmtpLine.
  if (aCodec1.mSdpFmtpLine.WasPassed() != aCodec2.mSdpFmtpLine.WasPassed()) {
    return false;
  }
  // If both have a sdpFmtpLine, compare them, and conditionally take levels
  // into account.
  if (aCodec1.mSdpFmtpLine.WasPassed() && aCodec2.mSdpFmtpLine.WasPassed()) {
    // Get the key-value pairs from the sdpFmtpLine if they are in a key-value
    // pair format.
    const auto pset1 = FmtpToParametersAndLevel(aCodec1.mMimeType,
                                                aCodec1.mSdpFmtpLine.Value());
    const auto pset2 = FmtpToParametersAndLevel(aCodec2.mMimeType,
                                                aCodec2.mSdpFmtpLine.Value());
    if (pset1.mSet.isNothing() || pset2.mSet.isNothing()) {
      // If either or both are not in a key-value pair format, they should be
      // compared using string equality.
      if (aCodec1.mSdpFmtpLine != aCodec2.mSdpFmtpLine) {
        return false;
      }
    } else {
      // If both are in a key-value pair format, compare the key-value pairs.
      const auto& set1 = pset1.mSet.value();
      const auto& set2 = pset2.mSet.value();

      if (set1.size() != set2.size()) {
        return false;
      }
      if (!aIgnoreLevels && (pset1.mLevel != pset2.mLevel ||
                             pset1.mSubprofile != pset2.mSubprofile ||
                             pset1.mProfile != pset2.mProfile)) {
        return false;
      }
      // Compare pair-wise the two parameter sets.
      for (const auto& pair : set1) {
        if (set2.find(pair) == set2.end()) {
          return false;
        }
      }
    }
  }
  return true;
}

// static
void RTCRtpSender::CheckAndRectifyEncodings(
    Sequence<RTCRtpEncodingParameters>& aEncodings, bool aVideo,
    const Optional<Sequence<RTCRtpCodecParameters>>& aCodecs,
    const bool aIgnoreLevels, const bool aCodecErasure,
    const MatchGetCapabilities aMatchGetCapabilities, ErrorResult& aRv) {
  // If any encoding contains a rid member whose value does not conform to the
  // grammar requirements specified in Section 10 of [RFC8851], throw a
  // TypeError.
  for (const auto& encoding : aEncodings) {
    if (encoding.mRid.WasPassed()) {
      std::string utf8Rid = NS_ConvertUTF16toUTF8(encoding.mRid.Value()).get();
      std::string error;
      if (!SdpRidAttributeList::CheckRidValidity(utf8Rid, &error)) {
        aRv.ThrowTypeError(nsCString(error));
        return;
      }
      if (utf8Rid.size() > SdpRidAttributeList::kMaxRidLength) {
        std::ostringstream ss;
        ss << "Rid can be at most " << SdpRidAttributeList::kMaxRidLength
           << " characters long (due to internal limitations)";
        aRv.ThrowTypeError(nsCString(ss.str()));
        return;
      }
    }
  }

  // Post-negotiation we should have a list of aCodecs, and for any encoding in
  // aEncodings that is using a codec that is not in aCodecs, we erase the codec
  // field from the encoding during set session description.
  // Yes. Really. This is what the spec says.
  // https://w3c.github.io/webrtc-pc/#set-the-session-description 4.6.13
  // Let codecs be transceiver.[[Sender]].[[SendCodecs]].
  //  If codecs is not an empty list:
  //    For each encoding in transceiver.[[Sender]].[[SendEncodings]], if
  //    encoding.codec does not match any entry in codecs, using the codec
  //    dictionary match algorithm with ignoreLevels set to true, remove
  //    encoding.codec.
  if (aCodecs.WasPassed() && aCodecs.Value().Length()) {
    for (auto& encoding : aEncodings) {
      if (encoding.mCodec.WasPassed()) {
        bool matched = false;
        for (const auto& codec : aCodecs.Value()) {
          if (DoesCodecParameterMatchCodec(encoding.mCodec.Value(), codec,
                                           aIgnoreLevels)) {
            matched = true;
            break;
          }
        }
        if (!matched) {
          if (aCodecErasure) {
            encoding.mCodec.Reset();
          } else {
            std::stringstream ss;
            ss << "Codec " << encoding.mCodec.Value().mMimeType
               << " not found in send codecs";
            aRv.ThrowInvalidModificationError(nsCString(ss.str()));
            return;
          }
        }
      }
    }
  }

  // https://w3c.github.io/webrtc-pc/#dom-rtcpeerconnection-addtransceiver
  // Step 8.3
  // If any encoding contains a codec member whose value does not match any
  // codec in RTCRtpSender.getCapabilities(kind).codecs, throw an
  // OperationError.
  if (aMatchGetCapabilities == MatchGetCapabilities::YES) {
    MOZ_ASSERT(aCodecs.WasPassed(),
               "aCodecs must be passed if aMatchGetCapabilities is YES");

    bool found = false;
    for (const auto& encoding : aEncodings) {
      if (encoding.mCodec.WasPassed()) {
        for (const auto& codec : aCodecs.Value()) {
          if (DoesCodecParameterMatchCodec(encoding.mCodec.Value(), codec,
                                           aIgnoreLevels)) {
            found = true;
            break;
          }
        }
        if (!found) {
          aRv.ThrowOperationError("Codec not found in codecs");
          return;
        }
      }
    }
  }

  if (aEncodings.Length() > 1) {
    // If some but not all encodings contain a rid member, throw a TypeError.
    // rid must be set if there is more than one encoding
    // NOTE: Since rid is read-only, and the number of encodings cannot grow,
    // this should never happen in setParameters.
    for (const auto& encoding : aEncodings) {
      if (!encoding.mRid.WasPassed()) {
        aRv.ThrowTypeError("Missing rid");
        return;
      }
    }

    // If any encoding contains a rid member whose value is the same as that of
    // a rid contained in another encoding in sendEncodings, throw a TypeError.
    // NOTE: Since rid is read-only, and the number of encodings cannot grow,
    // this should never happen in setParameters.
    std::set<nsString> uniqueRids;
    for (const auto& encoding : aEncodings) {
      if (uniqueRids.count(encoding.mRid.Value())) {
        aRv.ThrowTypeError("Duplicate rid");
        return;
      }
      uniqueRids.insert(encoding.mRid.Value());
    }
  }
  // TODO: ptime/adaptivePtime validation (bug 1733647)

  // If kind is "audio", remove the scaleResolutionDownBy member from all
  // encodings that contain one.
  if (!aVideo) {
    for (auto& encoding : aEncodings) {
      if (encoding.mScaleResolutionDownBy.WasPassed()) {
        encoding.mScaleResolutionDownBy.Reset();
      }
      if (encoding.mMaxFramerate.WasPassed()) {
        encoding.mMaxFramerate.Reset();
      }
    }
  }

  // If any encoding contains a scaleResolutionDownBy member whose value is
  // less than 1.0, throw a RangeError.
  for (const auto& encoding : aEncodings) {
    if (encoding.mScaleResolutionDownBy.WasPassed()) {
      if (encoding.mScaleResolutionDownBy.Value() < 1.0f) {
        aRv.ThrowRangeError("scaleResolutionDownBy must be >= 1.0");
        return;
      }
    }
  }

  // Verify that the value of each maxFramerate member in sendEncodings that is
  // defined is greater than 0.0. If one of the maxFramerate values does not
  // meet this requirement, throw a RangeError.
  for (const auto& encoding : aEncodings) {
    if (encoding.mMaxFramerate.WasPassed()) {
      if (encoding.mMaxFramerate.Value() < 0.0f) {
        aRv.ThrowRangeError("maxFramerate must be non-negative");
        return;
      }
    }
  }
}

void RTCRtpSender::GetParameters(RTCRtpSendParameters& aParameters) {
  MOZ_ASSERT(mParameters.mEncodings.Length());
  // If sender.[[LastReturnedParameters]] is not null, return
  // sender.[[LastReturnedParameters]], and abort these steps.
  if (mLastReturnedParameters.isSome()) {
    aParameters = *mLastReturnedParameters;
    return;
  }

  // Let result be a new RTCRtpSendParameters dictionary constructed as follows:

  // transactionId is set to a new unique identifier
  aParameters.mTransactionId.Construct(mPc->GenerateUUID());

  // encodings is set to the value of the [[SendEncodings]] internal slot.
  aParameters.mEncodings = mParameters.mEncodings;

  aParameters.mHeaderExtensions.Construct(
      mParameters.mHeaderExtensions.Value());
  aParameters.mRtcp.Construct(mParameters.mRtcp.Value());
  if (mParameters.mDegradationPreference.WasPassed()) {
    aParameters.mDegradationPreference.Construct(
        mParameters.mDegradationPreference.Value());
  }
  if (mParameters.mCodecs.WasPassed()) {
    aParameters.mCodecs.Construct(mParameters.mCodecs.Value());
  }

  // Set sender.[[LastReturnedParameters]] to result.
  mLastReturnedParameters = Some(aParameters);

  // Used to help with warning strings
  mLastTransactionId = Some(aParameters.mTransactionId.Value());

  // Queue a task that sets sender.[[LastReturnedParameters]] to null.
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [this, self = RefPtr<RTCRtpSender>(this)] {
        mLastReturnedParameters = Nothing();
      }));
}

bool operator==(const RTCRtpEncodingParameters& a1,
                const RTCRtpEncodingParameters& a2) {
  // webidl does not generate types that are equality comparable
  return a1.mActive == a2.mActive && a1.mMaxBitrate == a2.mMaxBitrate &&
         a1.mMaxFramerate == a2.mMaxFramerate && a1.mPriority == a2.mPriority &&
         a1.mRid == a2.mRid &&
         a1.mScaleResolutionDownBy == a2.mScaleResolutionDownBy;
}

bool operator==(const RTCRtcpParameters& a1, const RTCRtcpParameters& a2) {
  // webidl does not generate types that are equality comparable
  return a1.mCname == a2.mCname && a1.mReducedSize == a2.mReducedSize;
}

bool operator==(const RTCRtpHeaderExtensionParameters& a1,
                const RTCRtpHeaderExtensionParameters& a2) {
  // webidl does not generate types that are equality comparable
  return a1.mUri == a2.mUri && a1.mId == a2.mId &&
         a1.mEncrypted == a2.mEncrypted;
}

// static
void RTCRtpSender::ApplyJsEncodingToConduitEncoding(
    const RTCRtpEncodingParameters& aJsEncoding,
    VideoCodecConfig::Encoding* aConduitEncoding) {
  aConduitEncoding->active = aJsEncoding.mActive;
  if (aJsEncoding.mMaxBitrate.WasPassed()) {
    aConduitEncoding->constraints.maxBr = aJsEncoding.mMaxBitrate.Value();
  }
  if (aJsEncoding.mMaxFramerate.WasPassed()) {
    aConduitEncoding->constraints.maxFps =
        Some(aJsEncoding.mMaxFramerate.Value());
  }
  if (aJsEncoding.mScaleResolutionDownBy.WasPassed()) {
    // Optional does not have a valueOr, despite being based on Maybe
    // :(
    aConduitEncoding->constraints.scaleDownBy =
        aJsEncoding.mScaleResolutionDownBy.Value();
  } else {
    aConduitEncoding->constraints.scaleDownBy = 1.0f;
  }
}

void RTCRtpSender::UpdateRestorableEncodings(
    const Sequence<RTCRtpEncodingParameters>& aEncodings) {
  MOZ_ASSERT(aEncodings.Length());

  if (GetJsepTransceiver().mSendTrack.GetNegotiatedDetails()) {
    // Once initial negotiation completes, we are no longer allowed to restore
    // the unicast encoding.
    mUnicastEncoding.reset();
  } else if (mParameters.mEncodings.Length() == 1 &&
             !mParameters.mEncodings[0].mRid.WasPassed()) {
    // If we have not completed the initial negotiation, and we currently are
    // ridless unicast, we need to save our unicast encoding in case a
    // rollback occurs.
    mUnicastEncoding = Some(mParameters.mEncodings[0]);
  }
}

Sequence<RTCRtpEncodingParameters> RTCRtpSender::ToSendEncodings(
    const std::vector<std::string>& aRids) const {
  MOZ_ASSERT(!aRids.empty());

  Sequence<RTCRtpEncodingParameters> result;
  // If sendEncodings is given as input to this algorithm, and is non-empty,
  // set the [[SendEncodings]] slot to sendEncodings.
  for (const auto& rid : aRids) {
    MOZ_ASSERT(!rid.empty());
    RTCRtpEncodingParameters encoding;
    encoding.mActive = true;
    encoding.mRid.Construct(NS_ConvertUTF8toUTF16(rid.c_str()));
    (void)result.AppendElement(encoding, fallible);
  }

  // If sendEncodings is non-empty, set each encoding's scaleResolutionDownBy
  // to 2^(length of sendEncodings - encoding index - 1).
  if (mTransceiver->IsVideo()) {
    double scale = 1.0f;
    for (auto it = result.rbegin(); it != result.rend(); ++it) {
      it->mScaleResolutionDownBy.Construct(scale);
      scale *= 2;
    }
  }

  return result;
}

void RTCRtpSender::MaybeGetJsepRids() {
  MOZ_ASSERT(!mSimulcastEnvelopeSet);
  MOZ_ASSERT(mParameters.mEncodings.Length());

  auto jsepRids = GetJsepTransceiver().mSendTrack.GetRids();
  if (!jsepRids.empty()) {
    UpdateRestorableEncodings(mParameters.mEncodings);
    if (jsepRids.size() != 1 || !jsepRids[0].empty()) {
      // JSEP is using at least one rid. Stomp our single ridless encoding
      mParameters.mEncodings = ToSendEncodings(jsepRids);
    }
    mSimulcastEnvelopeSet = true;
    mSimulcastEnvelopeSetByJSEP = true;
  }
}

Sequence<RTCRtpEncodingParameters> RTCRtpSender::GetMatchingEncodings(
    const std::vector<std::string>& aRids) const {
  Sequence<RTCRtpEncodingParameters> result;

  if (!aRids.empty() && !aRids[0].empty()) {
    // Simulcast, or unicast with rid
    for (const auto& encoding : mParameters.mEncodings) {
      for (const auto& rid : aRids) {
        auto utf16Rid = NS_ConvertUTF8toUTF16(rid.c_str());
        if (!encoding.mRid.WasPassed() || (utf16Rid == encoding.mRid.Value())) {
          auto encodingCopy(encoding);
          if (!encodingCopy.mRid.WasPassed()) {
            encodingCopy.mRid.Construct(NS_ConvertUTF8toUTF16(rid.c_str()));
          }
          (void)result.AppendElement(encodingCopy, fallible);
          break;
        }
      }
    }
  }

  // If we're allowing the old setParameters behavior, we _might_ be able to
  // get into this situation even if there were rids above. Be extra careful.
  // Under normal circumstances, this just handles the ridless case.
  if (!result.Length()) {
    // Unicast with no specified rid. Restore mUnicastEncoding, if
    // it exists, otherwise pick the first encoding.
    if (mUnicastEncoding.isSome()) {
      (void)result.AppendElement(*mUnicastEncoding, fallible);
    } else {
      (void)result.AppendElement(mParameters.mEncodings[0], fallible);
    }
  }

  return result;
}

void RTCRtpSender::SetStreams(
    const Sequence<OwningNonNull<DOMMediaStream>>& aStreams, ErrorResult& aRv) {
  if (mPc->IsClosed()) {
    aRv.ThrowInvalidStateError(
        "Cannot call setStreams if the peer connection is closed");
    return;
  }

  SetStreamsImpl(aStreams);
  mPc->UpdateNegotiationNeeded();
}

void RTCRtpSender::SetStreamsImpl(
    const Sequence<OwningNonNull<DOMMediaStream>>& aStreams) {
  mStreams.Clear();
  std::set<nsString> ids;
  for (const auto& stream : aStreams) {
    nsString id;
    stream->GetId(id);
    if (!ids.count(id)) {
      ids.insert(std::move(id));
      mStreams.AppendElement(stream);
    }
  }
}

void RTCRtpSender::GetStreams(nsTArray<RefPtr<DOMMediaStream>>& aStreams) {
  aStreams = mStreams.Clone();
}

class ReplaceTrackOperation final : public PeerConnectionImpl::Operation {
 public:
  ReplaceTrackOperation(PeerConnectionImpl* aPc,
                        const RefPtr<RTCRtpTransceiver>& aTransceiver,
                        const RefPtr<MediaStreamTrack>& aTrack,
                        ErrorResult& aError);
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ReplaceTrackOperation,
                                           PeerConnectionImpl::Operation)

 private:
  MOZ_CAN_RUN_SCRIPT
  RefPtr<dom::Promise> CallImpl(ErrorResult& aError) override;
  ~ReplaceTrackOperation() = default;
  RefPtr<RTCRtpTransceiver> mTransceiver;
  RefPtr<MediaStreamTrack> mNewTrack;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(ReplaceTrackOperation,
                                   PeerConnectionImpl::Operation, mTransceiver,
                                   mNewTrack)

NS_IMPL_ADDREF_INHERITED(ReplaceTrackOperation, PeerConnectionImpl::Operation)
NS_IMPL_RELEASE_INHERITED(ReplaceTrackOperation, PeerConnectionImpl::Operation)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ReplaceTrackOperation)
NS_INTERFACE_MAP_END_INHERITING(PeerConnectionImpl::Operation)

ReplaceTrackOperation::ReplaceTrackOperation(
    PeerConnectionImpl* aPc, const RefPtr<RTCRtpTransceiver>& aTransceiver,
    const RefPtr<MediaStreamTrack>& aTrack, ErrorResult& aError)
    : PeerConnectionImpl::Operation(aPc, aError),
      mTransceiver(aTransceiver),
      mNewTrack(aTrack) {}

RefPtr<dom::Promise> ReplaceTrackOperation::CallImpl(ErrorResult& aError) {
  RefPtr<RTCRtpSender> sender = mTransceiver->Sender();
  // If transceiver.[[Stopping]] is true, return a promise rejected with a
  // newly created InvalidStateError.
  if (mTransceiver->Stopped() || mTransceiver->Stopping()) {
    RefPtr<dom::Promise> error = sender->MakePromise(aError);
    if (aError.Failed()) {
      return nullptr;
    }
    MOZ_LOG_FMT(gSenderLog, LogLevel::Debug,
                "{} Cannot call replaceTrack when transceiver is stopping",
                __FUNCTION__);
    error->MaybeRejectWithInvalidStateError(
        "Cannot call replaceTrack when transceiver is stopping");
    return error;
  }

  // Let p be a new promise.
  RefPtr<dom::Promise> p = sender->MakePromise(aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (!sender->SeamlessTrackSwitch(mNewTrack)) {
    MOZ_LOG_FMT(gSenderLog, LogLevel::Info,
                "{} Could not seamlessly replace track", __FUNCTION__);
    p->MaybeRejectWithInvalidModificationError(
        "Could not seamlessly replace track");
    return p;
  }

  // Queue a task that runs the following steps:
  GetMainThreadSerialEventTarget()->Dispatch(NS_NewRunnableFunction(
      __func__, [p, sender, track = mNewTrack]() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        // If connection.[[IsClosed]] is true, abort these steps.
        // Set sender.[[SenderTrack]] to withTrack.
        if (sender->SetSenderTrackWithClosedCheck(track)) {
          // mSenderTrack is now updated. SeamlessTrackSwitch already called
          // MaybeUpdateConduit() synchronously, but at that point mSenderTrack
          // still held the old value (spec requires the update to happen in
          // this queued task). Call it again now that mSenderTrack is final so
          // the conduit can correctly start or stop based on the new track.
          sender->MaybeUpdateConduit();
          // Resolve p with undefined.
          p->MaybeResolveWithUndefined();
        }
      }));

  // Return p.
  return p;
}

already_AddRefed<dom::Promise> RTCRtpSender::ReplaceTrack(
    dom::MediaStreamTrack* aWithTrack, ErrorResult& aError) {
  // If withTrack is non-null and withTrack.kind differs from the transceiver
  // kind of transceiver, return a promise rejected with a newly created
  // TypeError.
  if (aWithTrack) {
    nsString newKind;
    aWithTrack->GetKind(newKind);
    nsString oldKind;
    mTransceiver->GetKind(oldKind);
    if (newKind != oldKind) {
      RefPtr<dom::Promise> error = MakePromise(aError);
      if (aError.Failed()) {
        return nullptr;
      }
      error->MaybeRejectWithTypeError(
          "Cannot replaceTrack with a different kind!");
      return error.forget();
    }
  }

  MOZ_LOG_FMT(gSenderLog, LogLevel::Debug, "{}[{}]: {} ({} to {})",
              mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__,
              fmt::ptr(mSenderTrack.get()), fmt::ptr(aWithTrack));

  // Return the result of chaining the following steps to connection's
  // operations chain:
  RefPtr op =
      MakeRefPtr<ReplaceTrackOperation>(mPc, mTransceiver, aWithTrack, aError);
  if (aError.Failed()) {
    return nullptr;
  }
  // Static analysis forces us to use a temporary.
  auto pc = mPc;
  return pc->Chain(op, aError);
}

nsPIDOMWindowInner* RTCRtpSender::GetParentObject() const { return mWindow; }

already_AddRefed<dom::Promise> RTCRtpSender::MakePromise(
    ErrorResult& aError) const {
  return mPc->MakePromise(aError);
}

bool RTCRtpSender::SeamlessTrackSwitch(
    const RefPtr<MediaStreamTrack>& aWithTrack) {
  // We do not actually update mSenderTrack here! Spec says that happens in a
  // queued task after this is done (this happens in
  // SetSenderTrackWithClosedCheck).

  mPipeline->SetTrack(aWithTrack);

  MaybeUpdateConduit();

  // There may eventually be cases where a renegotiation is necessary to switch.
  return true;
}

void RTCRtpSender::SetTrack(const RefPtr<MediaStreamTrack>& aTrack) {
  // Used for RTCPeerConnection.removeTrack and RTCPeerConnection.addTrack
  if (mTransceiver->Stopping()) {
    return;
  }
  mSenderTrack = aTrack;
  SeamlessTrackSwitch(aTrack);
  if (aTrack) {
    // RFC says (in the section on remote rollback):
    // However, an RtpTransceiver MUST NOT be removed if a track was attached
    // to the RtpTransceiver via the addTrack method.
    mSenderTrackSetByAddTrack = true;
  }
}

bool RTCRtpSender::SetSenderTrackWithClosedCheck(
    const RefPtr<MediaStreamTrack>& aTrack) {
  if (!mPc->IsClosed()) {
    mSenderTrack = aTrack;
    return true;
  }

  return false;
}

void RTCRtpSender::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  mWatchManager.Shutdown();
  mPipeline->Shutdown();
  mPipeline = nullptr;
  if (mTransform) {
    mTransform->GetProxy().SetSender(nullptr);
  }
}

void RTCRtpSender::BreakCycles() {
  mWindow = nullptr;
  mPc = nullptr;
  mSenderTrack = nullptr;
  mTransceiver = nullptr;
  mStreams.Clear();
  mDtmf = nullptr;
}

void RTCRtpSender::Unlink() {
  if (mTransceiver) {
    mTransceiver->Unlink();
  }
}

void RTCRtpSender::UpdateTransport() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mHaveSetupTransport) {
    mPipeline->SetLevel(GetJsepTransceiver().GetLevel());
    mHaveSetupTransport = true;
  }

  mPipeline->UpdateTransport_m(GetJsepTransceiver().mTransport.mTransportId,
                               nullptr, true);
}

void RTCRtpSender::MaybeUpdateConduit() {
  // NOTE(pkerr) - the Call API requires the both local_ssrc and remote_ssrc be
  // set to a non-zero value or the CreateVideo...Stream call will fail.
  if (NS_WARN_IF(GetJsepTransceiver().mSendTrack.GetSsrcs().empty())) {
    MOZ_ASSERT(
        false,
        "No local ssrcs! This is a bug in the jsep engine, and should never "
        "happen!");
    return;
  }

  if (!mPipeline) {
    return;
  }

  bool wasTransmitting = mTransmitting;

  if (mPipeline->mConduit->type() == MediaSessionConduit::VIDEO) {
    Maybe<VideoConfig> newConfig = GetNewVideoConfig();
    if (newConfig.isSome()) {
      ApplyVideoConfig(*newConfig);
    }
  } else {
    Maybe<AudioConfig> newConfig = GetNewAudioConfig();
    if (newConfig.isSome()) {
      ApplyAudioConfig(*newConfig);
    }
  }

  if (!mSenderTrack && !wasTransmitting && mTransmitting) {
    MOZ_LOG_FMT(gSenderLog, LogLevel::Debug,
                "{}[{}]: {} Starting transmit conduit without send track!",
                mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
  }
}

void RTCRtpSender::UpdateParametersCodecs() {
  mParameters.mCodecs.Reset();
  mParameters.mCodecs.Construct();

  if (GetJsepTransceiver().mSendTrack.GetNegotiatedDetails()) {
    const JsepTrackNegotiatedDetails details(
        *GetJsepTransceiver().mSendTrack.GetNegotiatedDetails());
    if (details.GetEncodingCount()) {
      for (const auto& jsepCodec : details.GetEncoding(0).GetCodecs()) {
        if (!jsepCodec->mEnabled ||
            !jsepCodec->DirectionSupported(sdp::kSend)) {
          // This codec is disabled or sending it is unsupported.
          continue;
        }
        RTCRtpCodecParameters codec;
        RTCRtpTransceiver::ToDomRtpCodecParameters(*jsepCodec, &codec);
        (void)mParameters.mCodecs.Value().AppendElement(codec, fallible);
        if (jsepCodec->Type() == SdpMediaSection::kVideo) {
          const JsepVideoCodecDescription& videoJsepCodec =
              static_cast<JsepVideoCodecDescription&>(*jsepCodec);
          // In our JSEP implementation, RTX is an addon to an existing codec,
          // not a codec object in its own right. webrtc-pc treats RTX as a
          // separate codec, however.
          if (videoJsepCodec.mRtxEnabled) {
            RTCRtpCodecParameters rtx;
            RTCRtpTransceiver::ToDomRtpCodecParametersRtx(videoJsepCodec, &rtx);
            (void)mParameters.mCodecs.Value().AppendElement(rtx, fallible);
          }
        }
      }
    }

    // Check to see if we have a codec that is not in the codecs list.
    const auto& hasCodecMatch =
        [&](const RTCRtpEncodingParameters& param) -> bool {
      if (mParameters.mCodecs.WasPassed()) {
        for (const auto& codec : mParameters.mCodecs.Value()) {
          if (DoesCodecParameterMatchCodec(param.mCodec.Value(), codec, true)) {
            return true;
          }
        }
      }
      return false;
    };
    // If we have a codec that is not in the codecs list, remove it from the
    // encoding.
    for (auto& encoding : mParameters.mEncodings) {
      if (encoding.mCodec.WasPassed() && !hasCodecMatch(encoding)) {
        encoding.mCodec.Reset();
      }
    }
  }
}

void RTCRtpSender::UpdateParametersRtcp() {
  mParameters.mRtcp.Reset();
  mParameters.mRtcp.Construct();

  mParameters.mRtcp.Value().mCname.Construct(
      NS_ConvertUTF8toUTF16(GetJsepTransceiver().mSendTrack.GetCNAME()));
  // TODO(bug 1765852): We do not support reduced size yet
  mParameters.mRtcp.Value().mReducedSize.Construct(false);
}

void RTCRtpSender::UpdateParametersHeaderExtensions() {
  if (const JsepTrackNegotiatedDetails* details =
          GetJsepTransceiver().mSendTrack.GetNegotiatedDetails()) {
    mParameters.mHeaderExtensions.Reset();
    mParameters.mHeaderExtensions.Construct();
    RTCRtpTransceiver::ToDomHeaderExtensions(
        *details, mParameters.mHeaderExtensions.Value());
  }
}

void RTCRtpSender::SyncFromJsep(const JsepTransceiver& aJsepTransceiver) {
  if (!mSimulcastEnvelopeSet) {
    // JSEP is establishing the simulcast envelope for the first time, right now
    // This is the addTrack (or addTransceiver without sendEncodings) case.
    MaybeGetJsepRids();
  } else if (!aJsepTransceiver.mSendTrack.GetNegotiatedDetails() ||
             !aJsepTransceiver.mSendTrack.IsInHaveRemote()) {
    // Spec says that we do not update our encodings until we're in stable,
    // _unless_ this is the first negotiation.
    std::vector<std::string> rids = aJsepTransceiver.mSendTrack.GetRids();
    if (mSimulcastEnvelopeSetByJSEP && rids.empty()) {
      // JSEP previously set the simulcast envelope, but now it has no opinion
      // regarding unicast/simulcast. This can only happen on rollback of the
      // initial remote offer.
      mParameters.mEncodings = GetMatchingEncodings(rids);
      MOZ_ASSERT(mParameters.mEncodings.Length());
      mSimulcastEnvelopeSetByJSEP = false;
      mSimulcastEnvelopeSet = false;
    } else if (!rids.empty()) {
      // JSEP has an opinion on the simulcast envelope, which trumps anything
      // we have already.
      mParameters.mEncodings = GetMatchingEncodings(rids);
      MOZ_ASSERT(mParameters.mEncodings.Length());
    }
  }
  UpdateParametersCodecs();
  UpdateParametersRtcp();
  UpdateParametersHeaderExtensions();

  MaybeUpdateConduit();
}

void RTCRtpSender::SyncToJsep(JsepTransceiver& aJsepTransceiver) const {
  if (!mTransceiver->GetPreferredCodecs().IsEmpty()) {
    aJsepTransceiver.mSendTrack.PopulatePreferredCodecs(
        mTransceiver->GetPreferredCodecs(),
        mTransceiver->GetPreferredCodecsInUse());
  }

  std::vector<std::string> streamIds;
  for (const auto& stream : mStreams) {
    nsString wideStreamId;
    stream->GetId(wideStreamId);
    std::string streamId = NS_ConvertUTF16toUTF8(wideStreamId).get();
    MOZ_ASSERT(!streamId.empty());
    streamIds.push_back(std::move(streamId));
  }

  aJsepTransceiver.mSendTrack.UpdateStreamIds(streamIds);

  if (mSimulcastEnvelopeSet) {
    std::vector<std::string> rids;
    Maybe<RTCRtpSendParameters> parameters;
    if (mPendingRidChangeFromCompatMode) {
      // *sigh* If we have just let a setParameters change our rids, but we have
      // not yet updated mParameters because the queued task hasn't run yet,
      // we want to set the _new_ rids on the JsepTrack. So, we are forced to
      // grab them from mPendingParameters.
      parameters = mPendingParameters;
    } else {
      parameters = Some(mParameters);
    }
    for (const auto& encoding : parameters->mEncodings) {
      if (encoding.mRid.WasPassed()) {
        rids.push_back(NS_ConvertUTF16toUTF8(encoding.mRid.Value()).get());
      } else {
        rids.push_back("");
      }
    }

    aJsepTransceiver.mSendTrack.SetRids(rids);
  }

  if (mTransceiver->IsVideo()) {
    aJsepTransceiver.mSendTrack.SetMaxEncodings(webrtc::kMaxSimulcastStreams);
  } else {
    aJsepTransceiver.mSendTrack.SetMaxEncodings(1);
  }

  if (mSenderTrackSetByAddTrack) {
    aJsepTransceiver.SetOnlyExistsBecauseOfSetRemote(false);
  }
}

template <typename CodecConfigT>
CodecConfigT& findMatchingCodec(
    std::vector<CodecConfigT>& aCodecs,
    Maybe<const RTCRtpEncodingParameters&> aParameters) {
  MOZ_ASSERT(aCodecs.size());

  if (!aParameters || !aParameters->mCodec.WasPassed()) {
    return aCodecs[0];
  }

  for (auto& codec : aCodecs) {
    if (aParameters->mCodec.Value().mMimeType.EqualsIgnoreCase(
            codec.MimeType())) {
      return codec;
    }
  }

  return aCodecs[0];
}

Maybe<RTCRtpSender::VideoConfig> RTCRtpSender::GetNewVideoConfig() {
  // It is possible for SDP to signal that there is a send track, but there not
  // actually be a send track, according to the specification; all that needs to
  // happen is for the transceiver to be configured to send...
  if (!GetJsepTransceiver().mSendTrack.GetNegotiatedDetails()) {
    return Nothing();
  }

  VideoConfig oldConfig;
  oldConfig.mSsrcs = mSsrcs;
  oldConfig.mLocalRtpExtensions = mLocalRtpExtensions;
  oldConfig.mCname = mCname;
  oldConfig.mTransmitting = mTransmitting;
  oldConfig.mVideoRtxSsrcs = mVideoRtxSsrcs;
  oldConfig.mVideoCodec = mVideoCodec;
  oldConfig.mVideoRtpRtcpConfig = mVideoRtpRtcpConfig;
  oldConfig.mVideoCodecMode = mVideoCodecMode;

  VideoConfig newConfig(oldConfig);

  UpdateBaseConfig(&newConfig);

  newConfig.mVideoRtxSsrcs = GetJsepTransceiver().mSendTrack.GetRtxSsrcs();

  const JsepTrackNegotiatedDetails details(
      *GetJsepTransceiver().mSendTrack.GetNegotiatedDetails());

  if (mSenderTrack) {
    RefPtr<mozilla::dom::VideoStreamTrack> videotrack =
        mSenderTrack->AsVideoStreamTrack();

    if (!videotrack) {
      MOZ_CRASH(
          "In ConfigureVideoCodecMode, mSenderTrack is not video! This should "
          "never happen!");
    }

    dom::MediaSourceEnum source = videotrack->GetSource().GetMediaSource();
    switch (source) {
      case dom::MediaSourceEnum::Browser:
      case dom::MediaSourceEnum::Screen:
      case dom::MediaSourceEnum::Window:
      case dom::MediaSourceEnum::Application:
        newConfig.mVideoCodecMode = webrtc::VideoCodecMode::kScreensharing;
        break;

      case dom::MediaSourceEnum::Camera:
      case dom::MediaSourceEnum::Other:
        // Other is used by canvas capture, which we treat as realtime video.
        // This seems debatable, but we've been doing it this way for a long
        // time, so this is likely fine.
        newConfig.mVideoCodecMode = webrtc::VideoCodecMode::kRealtimeVideo;
        break;

      case dom::MediaSourceEnum::Microphone:
      case dom::MediaSourceEnum::AudioCapture:
        MOZ_ASSERT(false);
        break;
    }
  }

  std::vector<VideoCodecConfig> configs;
  RTCRtpTransceiver::NegotiatedDetailsToVideoCodecConfigs(details, &configs);

  if (configs.empty()) {
    // TODO: Are we supposed to plumb this error back to JS? This does not
    // seem like a failure to set an answer, it just means that codec
    // negotiation failed. For now, we're just doing the same thing we do
    // if negotiation as a whole failed.
    MOZ_LOG_FMT(gSenderLog, LogLevel::Error,
                "{}[{}]: {}  No video codecs were negotiated (send).",
                mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
    return Nothing();
  }

  // Spec says that we start using new parameters right away, _before_ we
  // update the parameters that are visible to JS (ie; mParameters).
  const auto& parameters = mPendingParameters.refOr(mParameters);
  const auto& encodings = parameters.mEncodings;
  newConfig.mVideoCodec = Some(findMatchingCodec(
      configs, encodings.IsEmpty() ? Nothing() : SomeRef(encodings[0])));
  for (VideoCodecConfig::Encoding& conduitEncoding :
       newConfig.mVideoCodec->mEncodings) {
    for (const RTCRtpEncodingParameters& jsEncoding : parameters.mEncodings) {
      std::string rid;
      if (jsEncoding.mRid.WasPassed()) {
        rid = NS_ConvertUTF16toUTF8(jsEncoding.mRid.Value()).get();
      }
      if (conduitEncoding.rid == rid) {
        ApplyJsEncodingToConduitEncoding(jsEncoding, &conduitEncoding);
        break;
      }
    }
  }

  if (!mHaveLoggedUlpfecInfo) {
    bool ulpfecNegotiated = false;
    for (const auto& codec : configs) {
      if (nsCRT::strcasestr(codec.mName.c_str(), "ulpfec")) {
        ulpfecNegotiated = true;
      }
    }
    mozilla::glean::codec_stats::ulpfec_negotiated
        .Get(ulpfecNegotiated ? "negotiated"_ns : "not_negotiated"_ns)
        .Add(1);
    mHaveLoggedUlpfecInfo = true;
  }

  // Log codec information we are tracking
  if (!mHaveLoggedOtherFec &&
      !GetJsepTransceiver().mSendTrack.GetFecCodecName().empty()) {
    mozilla::glean::codec_stats::other_fec_signaled
        .Get(nsDependentCString(
            GetJsepTransceiver().mSendTrack.GetFecCodecName().c_str()))
        .Add(1);
    mHaveLoggedOtherFec = true;
  }
  if (!mHaveLoggedVideoPreferredCodec &&
      !GetJsepTransceiver().mSendTrack.GetVideoPreferredCodec().empty()) {
    mozilla::glean::codec_stats::video_preferred_codec
        .Get(nsDependentCString(
            GetJsepTransceiver().mSendTrack.GetVideoPreferredCodec().c_str()))
        .Add(1);
    mHaveLoggedVideoPreferredCodec = true;
  }

  newConfig.mVideoRtpRtcpConfig = Some(details.GetRtpRtcpConfig());

  if (newConfig == oldConfig) {
    MOZ_LOG_FMT(gSenderLog, LogLevel::Debug,
                "{}[{}]: {}  No change in video config",
                mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
    return Nothing();
  }

  if (newConfig.mVideoCodec.isSome()) {
    MOZ_ASSERT(newConfig.mSsrcs.size() ==
               newConfig.mVideoCodec->mEncodings.size());
  }
  return Some(newConfig);
}

Maybe<RTCRtpSender::AudioConfig> RTCRtpSender::GetNewAudioConfig() {
  AudioConfig oldConfig;
  oldConfig.mSsrcs = mSsrcs;
  oldConfig.mLocalRtpExtensions = mLocalRtpExtensions;
  oldConfig.mCname = mCname;
  oldConfig.mTransmitting = mTransmitting;
  oldConfig.mAudioCodec = mAudioCodec;

  AudioConfig newConfig(oldConfig);

  UpdateBaseConfig(&newConfig);

  if (GetJsepTransceiver().mSendTrack.GetNegotiatedDetails() &&
      GetJsepTransceiver().mSendTrack.GetActive()) {
    const auto& details(
        *GetJsepTransceiver().mSendTrack.GetNegotiatedDetails());

    std::vector<AudioCodecConfig> configs;
    RTCRtpTransceiver::NegotiatedDetailsToAudioCodecConfigs(details, &configs);
    if (configs.empty()) {
      // TODO: Are we supposed to plumb this error back to JS? This does not
      // seem like a failure to set an answer, it just means that codec
      // negotiation failed. For now, we're just doing the same thing we do
      // if negotiation as a whole failed.
      MOZ_LOG_FMT(gSenderLog, LogLevel::Error,
                  "{}[{}]: {} No audio codecs were negotiated (send)",
                  mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
      return Nothing();
    }

    const auto& parameters = mPendingParameters.refOr(mParameters);
    const auto& encodings = parameters.mEncodings;
    auto encoding = encodings.IsEmpty() ? Nothing() : SomeRef(encodings[0]);
    AudioCodecConfig& sendCodec = findMatchingCodec(configs, encoding);

    if (encoding) {
      if (encoding->mMaxBitrate.WasPassed()) {
        sendCodec.mEncodingConstraints.maxBitrateBps.emplace(
            encoding->mMaxBitrate.Value());
      }
      // Audio has a single encoding; an inactive encoding must not transmit.
      newConfig.mTransmitting = newConfig.mTransmitting && encoding->mActive;
    }

    std::vector<AudioCodecConfig> dtmfConfigs;
    std::copy_if(
        configs.begin(), configs.end(), std::back_inserter(dtmfConfigs),
        [](const auto& value) { return value.mName == "telephone-event"; });
    if (!dtmfConfigs.empty()) {
      // There is at least one telephone-event codec.
      // We primarily choose the codec whose frequency matches the send codec.
      // Secondarily we choose the one with the lowest frequency.
      auto dtmfIterator =
          std::find_if(dtmfConfigs.begin(), dtmfConfigs.end(),
                       [&sendCodec](const auto& dtmfCodec) {
                         return dtmfCodec.mFreq == sendCodec.mFreq;
                       });
      if (dtmfIterator == dtmfConfigs.end()) {
        dtmfIterator = std::min_element(
            dtmfConfigs.begin(), dtmfConfigs.end(),
            [](const auto& a, const auto& b) { return a.mFreq < b.mFreq; });
      }
      MOZ_ASSERT(dtmfIterator != dtmfConfigs.end());
      newConfig.mDtmfPt = dtmfIterator->mType;
      newConfig.mDtmfFreq = dtmfIterator->mFreq;
    }

    newConfig.mAudioCodec = Some(sendCodec);
  }

  if (!mHaveLoggedAudioPreferredCodec &&
      !GetJsepTransceiver().mSendTrack.GetAudioPreferredCodec().empty()) {
    mozilla::glean::codec_stats::audio_preferred_codec
        .Get(nsDependentCString(
            GetJsepTransceiver().mSendTrack.GetAudioPreferredCodec().c_str()))
        .Add(1);
    mHaveLoggedAudioPreferredCodec = true;
  }

  if (newConfig == oldConfig) {
    MOZ_LOG_FMT(gSenderLog, LogLevel::Debug,
                "{}[{}]: {}  No change in audio config",
                mPc->GetHandle().c_str(), GetMid().c_str(), __FUNCTION__);
    return Nothing();
  }

  return Some(newConfig);
}

void RTCRtpSender::UpdateBaseConfig(BaseConfig* aConfig) {
  aConfig->mSsrcs = GetJsepTransceiver().mSendTrack.GetSsrcs();
  aConfig->mCname = GetJsepTransceiver().mSendTrack.GetCNAME();

  if (GetJsepTransceiver().mSendTrack.GetNegotiatedDetails() &&
      GetJsepTransceiver().mSendTrack.GetActive()) {
    const auto& details(
        *GetJsepTransceiver().mSendTrack.GetNegotiatedDetails());
    {
      std::vector<webrtc::RtpExtension> extmaps;
      // @@NG read extmap from track
      details.ForEachRTPHeaderExtension(
          [&extmaps](const SdpExtmapAttributeList::Extmap& extmap) {
            extmaps.emplace_back(extmap.extensionname,
                                 webrtc::RtpHeaderExtensionId(extmap.entry));
          });
      aConfig->mLocalRtpExtensions = std::move(extmaps);
    }
  }
  // RTCRtpTransceiver::IsSending is updated after negotiation completes, in a
  // queued task (which we may be in right now). Don't use
  // JsepTrack::GetActive, because that updates before the queued task, which
  // is too early for some of the things we interact with here (eg;
  // RTCDTMFSender).
  aConfig->mTransmitting = mTransceiver->IsSending() && mSenderTrack;
}

void RTCRtpSender::ApplyVideoConfig(const VideoConfig& aConfig) {
  if (aConfig.mVideoCodec.isSome()) {
    MOZ_ASSERT(aConfig.mSsrcs.size() == aConfig.mVideoCodec->mEncodings.size());
  }

  mSsrcs = aConfig.mSsrcs;
  mCname = aConfig.mCname;
  mLocalRtpExtensions = aConfig.mLocalRtpExtensions;

  mVideoRtxSsrcs = aConfig.mVideoRtxSsrcs;
  mVideoCodec = aConfig.mVideoCodec;
  mVideoRtpRtcpConfig = aConfig.mVideoRtpRtcpConfig;
  mVideoCodecMode = aConfig.mVideoCodecMode;

  mTransmitting = aConfig.mTransmitting;
}

void RTCRtpSender::ApplyAudioConfig(const AudioConfig& aConfig) {
  mSsrcs = aConfig.mSsrcs;
  mCname = aConfig.mCname;
  mLocalRtpExtensions = aConfig.mLocalRtpExtensions;

  mAudioCodec = aConfig.mAudioCodec;

  if (aConfig.mDtmfPt >= 0) {
    mDtmf->SetPayloadType(aConfig.mDtmfPt, aConfig.mDtmfFreq);
  }

  mTransmitting = aConfig.mTransmitting;
}

void RTCRtpSender::Stop() {
  MOZ_ASSERT(mTransceiver->Stopping());
  mTransmitting = false;
}

bool RTCRtpSender::HasTrack(const dom::MediaStreamTrack* aTrack) const {
  if (!mSenderTrack) {
    return false;
  }

  if (!aTrack) {
    return true;
  }

  return mSenderTrack.get() == aTrack;
}

RefPtr<MediaPipelineTransmit> RTCRtpSender::GetPipeline() const {
  return mPipeline;
}

std::string RTCRtpSender::GetMid() const { return mTransceiver->GetMidAscii(); }

JsepTransceiver& RTCRtpSender::GetJsepTransceiver() {
  return mTransceiver->GetJsepTransceiver();
}

void RTCRtpSender::SetTransform(RTCRtpScriptTransform* aTransform,
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

  // Seamless switch for frames
  if (aTransform) {
    mFrameTransformerProxy = &aTransform->GetProxy();
  } else {
    mFrameTransformerProxy = nullptr;
  }

  if (mTransform) {
    mTransform->GetProxy().SetSender(nullptr);
  }

  mTransform = const_cast<RTCRtpScriptTransform*>(aTransform);

  if (mTransform) {
    mTransform->GetProxy().SetSender(this);
    mTransform->SetClaimed();
  }
}

bool RTCRtpSender::GenerateKeyFrame(const Maybe<std::string>& aRid) {
  if (!mTransform || !mPipeline) {
    return false;
  }

  mPipeline->mConduit->AsVideoSessionConduit().apply([&](const auto& conduit) {
    conduit->GenerateKeyFrame(aRid, &mTransform->GetProxy());
  });
  return true;
}

}  // namespace mozilla::dom

#undef LOGTAG
