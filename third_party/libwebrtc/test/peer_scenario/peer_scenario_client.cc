/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/peer_scenario/peer_scenario_client.h"

#include <limits>
#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/environment/environment.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/test/create_time_controller.h"
#include "api/transport/field_trial_based_config.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "media/engine/webrtc_media_engine.h"
#include "modules/audio_device/include/test_audio_device.h"
#include "p2p/client/basic_port_allocator.h"
#include "test/create_frame_generator_capturer.h"
#include "test/fake_decoder.h"
#include "test/fake_vp8_encoder.h"
#include "test/frame_generator_capturer.h"

namespace webrtc {
namespace test {

namespace {

constexpr char kCommonStreamId[] = "stream_id";

std::map<int, EmulatedEndpoint*> CreateEndpoints(
    NetworkEmulationManager* net,
    std::map<int, EmulatedEndpointConfig> endpoint_configs) {
  std::map<int, EmulatedEndpoint*> endpoints;
  for (const auto& kv : endpoint_configs)
    endpoints[kv.first] = net->CreateEndpoint(kv.second);
  return endpoints;
}

class LambdaPeerConnectionObserver final : public PeerConnectionObserver {
 public:
  explicit LambdaPeerConnectionObserver(
      PeerScenarioClient::CallbackHandlers* handlers)
      : handlers_(handlers) {}
  void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) override {
    for (const auto& handler : handlers_->on_signaling_change)
      handler(new_state);
  }
  void OnDataChannel(
      rtc::scoped_refptr<DataChannelInterface> data_channel) override {
    for (const auto& handler : handlers_->on_data_channel)
      handler(data_channel);
  }
  void OnRenegotiationNeeded() override {
    for (const auto& handler : handlers_->on_renegotiation_needed)
      handler();
  }
  void OnStandardizedIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override {
    for (const auto& handler : handlers_->on_standardized_ice_connection_change)
      handler(new_state);
  }
  void OnConnectionChange(
      PeerConnectionInterface::PeerConnectionState new_state) override {
    for (const auto& handler : handlers_->on_connection_change)
      handler(new_state);
  }
  void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) override {
    for (const auto& handler : handlers_->on_ice_gathering_change)
      handler(new_state);
  }
  void OnIceCandidate(const IceCandidateInterface* candidate) override {
    for (const auto& handler : handlers_->on_ice_candidate)
      handler(candidate);
  }
  void OnIceCandidateError(const std::string& address,
                           int port,
                           const std::string& url,
                           int error_code,
                           const std::string& error_text) override {
    for (const auto& handler : handlers_->on_ice_candidate_error)
      handler(address, port, url, error_code, error_text);
  }
  void OnIceCandidatesRemoved(
      const std::vector<cricket::Candidate>& candidates) override {
    for (const auto& handler : handlers_->on_ice_candidates_removed)
      handler(candidates);
  }
  void OnAddTrack(rtc::scoped_refptr<RtpReceiverInterface> receiver,
                  const std::vector<rtc::scoped_refptr<MediaStreamInterface>>&
                      streams) override {
    for (const auto& handler : handlers_->on_add_track)
      handler(receiver, streams);
  }
  void OnTrack(
      rtc::scoped_refptr<RtpTransceiverInterface> transceiver) override {
    for (const auto& handler : handlers_->on_track)
      handler(transceiver);
  }
  void OnRemoveTrack(
      rtc::scoped_refptr<RtpReceiverInterface> receiver) override {
    for (const auto& handler : handlers_->on_remove_track)
      handler(receiver);
  }

 private:
  PeerScenarioClient::CallbackHandlers* handlers_;
};

class LambdaCreateSessionDescriptionObserver
    : public CreateSessionDescriptionObserver {
 public:
  explicit LambdaCreateSessionDescriptionObserver(
      std::function<void(std::unique_ptr<SessionDescriptionInterface> desc)>
          on_success)
      : on_success_(on_success) {}
  void OnSuccess(SessionDescriptionInterface* desc) override {
    // Takes ownership of answer, according to CreateSessionDescriptionObserver
    // convention.
    on_success_(absl::WrapUnique(desc));
  }
  void OnFailure(RTCError error) override {
    RTC_DCHECK_NOTREACHED() << error.message();
  }

 private:
  std::function<void(std::unique_ptr<SessionDescriptionInterface> desc)>
      on_success_;
};

class LambdaSetLocalDescriptionObserver
    : public SetLocalDescriptionObserverInterface {
 public:
  explicit LambdaSetLocalDescriptionObserver(
      std::function<void(RTCError)> on_complete)
      : on_complete_(on_complete) {}
  void OnSetLocalDescriptionComplete(RTCError error) override {
    on_complete_(error);
  }

 private:
  std::function<void(RTCError)> on_complete_;
};

class LambdaSetRemoteDescriptionObserver
    : public SetRemoteDescriptionObserverInterface {
 public:
  explicit LambdaSetRemoteDescriptionObserver(
      std::function<void(RTCError)> on_complete)
      : on_complete_(on_complete) {}
  void OnSetRemoteDescriptionComplete(RTCError error) override {
    on_complete_(error);
  }

 private:
  std::function<void(RTCError)> on_complete_;
};

class FakeVideoEncoderFactory : public VideoEncoderFactory {
 public:
  std::vector<SdpVideoFormat> GetSupportedFormats() const override {
    return {SdpVideoFormat::VP8()};
  }
  std::unique_ptr<VideoEncoder> Create(const Environment& env,
                                       const SdpVideoFormat& format) override {
    RTC_CHECK_EQ(format.name, "VP8");
    return std::make_unique<FakeVp8Encoder>(env);
  }
};

class FakeVideoDecoderFactory : public VideoDecoderFactory {
 public:
  std::vector<SdpVideoFormat> GetSupportedFormats() const override {
    return {SdpVideoFormat::VP8()};
  }
  std::unique_ptr<VideoDecoder> Create(const Environment& env,
                                       const SdpVideoFormat& format) override {
    return std::make_unique<FakeDecoder>();
  }
};
}  // namespace

PeerScenarioClient::PeerScenarioClient(
    NetworkEmulationManager* net,
    rtc::Thread* signaling_thread,
    std::unique_ptr<LogWriterFactoryInterface> log_writer_factory,
    PeerScenarioClient::Config config)
    : endpoints_(CreateEndpoints(net, config.endpoints)),
      task_queue_factory_(net->time_controller()->GetTaskQueueFactory()),
      signaling_thread_(signaling_thread),
      log_writer_factory_(std::move(log_writer_factory)),
      worker_thread_(net->time_controller()->CreateThread("worker")),
      handlers_(config.handlers),
      observer_(new LambdaPeerConnectionObserver(&handlers_)) {
  handlers_.on_track.push_back(
      [this](rtc::scoped_refptr<RtpTransceiverInterface> transceiver) {
        auto track = transceiver->receiver()->track().get();
        if (track->kind() == MediaStreamTrackInterface::kVideoKind) {
          auto* video = static_cast<VideoTrackInterface*>(track);
          RTC_DCHECK_RUN_ON(signaling_thread_);
          for (auto* sink : track_id_to_video_sinks_[track->id()]) {
            video->AddOrUpdateSink(sink, rtc::VideoSinkWants());
          }
        }
      });
  handlers_.on_signaling_change.push_back(
      [this](PeerConnectionInterface::SignalingState state) {
        RTC_DCHECK_RUN_ON(signaling_thread_);
        if (state == PeerConnectionInterface::SignalingState::kStable &&
            peer_connection_->current_remote_description()) {
          for (const auto& candidate : pending_ice_candidates_) {
            RTC_CHECK(peer_connection_->AddIceCandidate(candidate.get()));
          }
          pending_ice_candidates_.clear();
        }
      });

  std::vector<EmulatedEndpoint*> endpoints_vector;
  for (const auto& kv : endpoints_)
    endpoints_vector.push_back(kv.second);
  auto* manager = net->CreateEmulatedNetworkManagerInterface(endpoints_vector);

  PeerConnectionFactoryDependencies pcf_deps;
  pcf_deps.network_thread = manager->network_thread();
  pcf_deps.signaling_thread = signaling_thread_;
  pcf_deps.worker_thread = worker_thread_.get();
  pcf_deps.task_queue_factory =
      net->time_controller()->CreateTaskQueueFactory();
  pcf_deps.event_log_factory = std::make_unique<RtcEventLogFactory>();
  pcf_deps.trials = std::make_unique<FieldTrialBasedConfig>();

  pcf_deps.adm = TestAudioDeviceModule::Create(
      task_queue_factory_,
      TestAudioDeviceModule::CreatePulsedNoiseCapturer(
          config.audio.pulsed_noise->amplitude *
              std::numeric_limits<int16_t>::max(),
          config.audio.sample_rate, config.audio.channels),
      TestAudioDeviceModule::CreateDiscardRenderer(config.audio.sample_rate));

  if (config.video.use_fake_codecs) {
    pcf_deps.video_encoder_factory =
        std::make_unique<FakeVideoEncoderFactory>();
    pcf_deps.video_decoder_factory =
        std::make_unique<FakeVideoDecoderFactory>();
  } else {
    pcf_deps.video_encoder_factory =
        std::make_unique<VideoEncoderFactoryTemplate<
            LibvpxVp8EncoderTemplateAdapter, LibvpxVp9EncoderTemplateAdapter,
            OpenH264EncoderTemplateAdapter, LibaomAv1EncoderTemplateAdapter>>();
    pcf_deps.video_decoder_factory =
        std::make_unique<VideoDecoderFactoryTemplate<
            LibvpxVp8DecoderTemplateAdapter, LibvpxVp9DecoderTemplateAdapter,
            OpenH264DecoderTemplateAdapter, Dav1dDecoderTemplateAdapter>>();
  }

  EnableMediaWithDefaultsAndTimeController(*net->time_controller(), pcf_deps);

  pcf_deps.fec_controller_factory = nullptr;
  pcf_deps.network_controller_factory = nullptr;
  pcf_deps.network_state_predictor_factory = nullptr;

  pc_factory_ = CreateModularPeerConnectionFactory(std::move(pcf_deps));
  PeerConnectionFactoryInterface::Options pc_options;
  pc_options.disable_encryption = config.disable_encryption;
  pc_factory_->SetOptions(pc_options);

  PeerConnectionDependencies pc_deps(observer_.get());
  pc_deps.allocator = std::make_unique<cricket::BasicPortAllocator>(
      manager->network_manager(), manager->packet_socket_factory());
  pc_deps.allocator->set_flags(pc_deps.allocator->flags() |
                               cricket::PORTALLOCATOR_DISABLE_TCP);
  peer_connection_ =
      pc_factory_
          ->CreatePeerConnectionOrError(config.rtc_config, std::move(pc_deps))
          .MoveValue();
  if (log_writer_factory_) {
    peer_connection_->StartRtcEventLog(log_writer_factory_->Create(".rtc.dat"),
                                       /*output_period_ms=*/1000);
  }
}

EmulatedEndpoint* PeerScenarioClient::endpoint(int index) {
  RTC_CHECK_GT(endpoints_.size(), index);
  return endpoints_.at(index);
}

PeerScenarioClient::AudioSendTrack PeerScenarioClient::CreateAudio(
    std::string track_id,
    cricket::AudioOptions options) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  AudioSendTrack res;
  auto source = pc_factory_->CreateAudioSource(options);
  auto track = pc_factory_->CreateAudioTrack(track_id, source.get());
  res.track = track;
  res.sender = peer_connection_->AddTrack(track, {kCommonStreamId}).value();
  return res;
}

PeerScenarioClient::VideoSendTrack PeerScenarioClient::CreateVideo(
    std::string track_id,
    VideoSendTrackConfig config) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  VideoSendTrack res;
  auto capturer = CreateFrameGeneratorCapturer(clock(), *task_queue_factory_,
                                               config.generator);
  res.capturer = capturer.get();
  capturer->Init();
  res.source = rtc::make_ref_counted<FrameGeneratorCapturerVideoTrackSource>(
      std::move(capturer), config.screencast);
  res.source->Start();
  auto track = pc_factory_->CreateVideoTrack(res.source, track_id);
  res.track = track.get();
  res.sender =
      peer_connection_->AddTrack(track, {kCommonStreamId}).MoveValue().get();
  return res;
}

void PeerScenarioClient::AddVideoReceiveSink(
    std::string track_id,
    rtc::VideoSinkInterface<VideoFrame>* video_sink) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  track_id_to_video_sinks_[track_id].push_back(video_sink);
}

void PeerScenarioClient::CreateAndSetSdp(
    std::function<void(SessionDescriptionInterface*)> munge_offer,
    std::function<void(std::string)> offer_handler) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  peer_connection_->CreateOffer(
      rtc::make_ref_counted<LambdaCreateSessionDescriptionObserver>(
          [=](std::unique_ptr<SessionDescriptionInterface> offer) {
            RTC_DCHECK_RUN_ON(signaling_thread_);
            if (munge_offer) {
              munge_offer(offer.get());
            }
            std::string sdp_offer;
            RTC_CHECK(offer->ToString(&sdp_offer));
            peer_connection_->SetLocalDescription(
                std::move(offer),
                rtc::make_ref_counted<LambdaSetLocalDescriptionObserver>(
                    [sdp_offer, offer_handler](RTCError) {
                      offer_handler(sdp_offer);
                    }));
          })
          .get(),
      PeerConnectionInterface::RTCOfferAnswerOptions());
}

void PeerScenarioClient::SetSdpOfferAndGetAnswer(
    std::string remote_offer,
    std::function<void()> remote_description_set,
    std::function<void(std::string)> answer_handler) {
  if (!signaling_thread_->IsCurrent()) {
    signaling_thread_->PostTask([=] {
      SetSdpOfferAndGetAnswer(remote_offer, remote_description_set,
                              answer_handler);
    });
    return;
  }
  RTC_DCHECK_RUN_ON(signaling_thread_);
  peer_connection_->SetRemoteDescription(
      CreateSessionDescription(SdpType::kOffer, remote_offer),
      rtc::make_ref_counted<LambdaSetRemoteDescriptionObserver>([=](RTCError) {
        RTC_DCHECK_RUN_ON(signaling_thread_);
        if (remote_description_set) {
          // Allow the caller to modify transceivers
          // before creating the answer.
          remote_description_set();
        }
        peer_connection_->CreateAnswer(
            rtc::make_ref_counted<LambdaCreateSessionDescriptionObserver>(
                [=](std::unique_ptr<SessionDescriptionInterface> answer) {
                  RTC_DCHECK_RUN_ON(signaling_thread_);
                  std::string sdp_answer;
                  answer->ToString(&sdp_answer);
                  RTC_LOG(LS_INFO) << sdp_answer;
                  peer_connection_->SetLocalDescription(
                      std::move(answer),
                      rtc::make_ref_counted<LambdaSetLocalDescriptionObserver>(
                          [answer_handler, sdp_answer](RTCError) {
                            answer_handler(sdp_answer);
                          }));
                })
                .get(),
            PeerConnectionInterface::RTCOfferAnswerOptions());
      }));
}

void PeerScenarioClient::SetSdpAnswer(
    std::string remote_answer,
    std::function<void(const SessionDescriptionInterface&)> done_handler) {
  if (!signaling_thread_->IsCurrent()) {
    signaling_thread_->PostTask(
        [=] { SetSdpAnswer(remote_answer, done_handler); });
    return;
  }
  RTC_DCHECK_RUN_ON(signaling_thread_);
  peer_connection_->SetRemoteDescription(
      CreateSessionDescription(SdpType::kAnswer, remote_answer),
      rtc::make_ref_counted<LambdaSetRemoteDescriptionObserver>(
          [remote_answer, done_handler](RTCError) {
            auto answer =
                CreateSessionDescription(SdpType::kAnswer, remote_answer);
            done_handler(*answer);
          }));
}

void PeerScenarioClient::AddIceCandidate(
    std::unique_ptr<IceCandidateInterface> candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  if (peer_connection_->signaling_state() ==
          PeerConnectionInterface::SignalingState::kStable &&
      peer_connection_->current_remote_description()) {
    RTC_CHECK(peer_connection_->AddIceCandidate(candidate.get()));
  } else {
    pending_ice_candidates_.push_back(std::move(candidate));
  }
}

}  // namespace test
}  // namespace webrtc
