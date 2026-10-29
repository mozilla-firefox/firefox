/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_FAKE_PEER_CONNECTION_FOR_STATS_H_
#define PC_TEST_FAKE_PEER_CONNECTION_FOR_STATS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_device.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/data_channel_transport_interface.h"
#include "call/call.h"
#include "call/payload_type_picker.h"
#include "media/base/fake_media_engine.h"
#include "media/base/media_channel.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "p2p/test/fake_ice_transport.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/channel.h"
#include "pc/channel_interface.h"
#include "pc/connection_context.h"
#include "pc/data_channel_utils.h"
#include "pc/dtls_transport.h"
#include "pc/jsep_transport_controller.h"
#include "pc/rtc_stats_collector.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/rtp_transport_internal.h"
#include "pc/sctp_data_channel.h"
#include "pc/session_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/fake_codec_lookup_helper.h"
#include "pc/test/fake_data_channel_controller.h"
#include "pc/test/fake_peer_connection_base.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/unique_id_generator.h"

namespace webrtc {

class FakeIceTransportFactory : public IceTransportFactory {
 public:
  ~FakeIceTransportFactory() override = default;
  scoped_refptr<IceTransportInterface> CreateIceTransport(
      const std::string& transport_name,
      int component,
      IceTransportInit init) override {
    auto internal = std::make_unique<FakeIceTransportInternal>(
        init.env(), transport_name, component);
    return make_ref_counted<FakeIceTransport>(std::move(internal));
  }
};

// Fake VoiceMediaChannel where the result of GetStats can be configured.
class FakeVoiceMediaSendChannelForStats : public FakeVoiceMediaSendChannel {
 public:
  explicit FakeVoiceMediaSendChannelForStats(TaskQueueBase* network_thread)
      : FakeVoiceMediaSendChannel(AudioOptions(), network_thread) {}

  void SetStats(const VoiceMediaInfo& voice_info) {
    send_stats_ = VoiceMediaSendInfo();
    send_stats_->senders = voice_info.senders;
    send_stats_->send_codecs = voice_info.send_codecs;
  }

  // VoiceMediaChannel overrides.
  bool GetStats(VoiceMediaSendInfo* info) override {
    if (send_stats_) {
      *info = *send_stats_;
      return true;
    }
    return false;
  }

  absl::AnyInvocable<std::optional<VoiceMediaSendInfo>()> GetStatsTask()
      override {
    return [this, safety = task_safety_.flag()]() mutable
               -> std::optional<VoiceMediaSendInfo> {
      if (!safety->alive()) {
        return std::nullopt;
      }
      VoiceMediaSendInfo info;
      if (GetStats(&info)) {
        return info;
      }
      return std::nullopt;
    };
  }

 private:
  ScopedTaskSafety task_safety_;
  std::optional<VoiceMediaSendInfo> send_stats_;
};

class FakeVoiceMediaReceiveChannelForStats
    : public FakeVoiceMediaReceiveChannel {
 public:
  explicit FakeVoiceMediaReceiveChannelForStats(TaskQueueBase* network_thread)
      : FakeVoiceMediaReceiveChannel(AudioOptions(), network_thread) {}

  void SetStats(const VoiceMediaInfo& voice_info) {
    receive_stats_ = VoiceMediaReceiveInfo();
    receive_stats_->receivers = voice_info.receivers;
    receive_stats_->receive_codecs = voice_info.receive_codecs;
    receive_stats_->device_underrun_count = voice_info.device_underrun_count;
  }

  // VoiceMediaChannel overrides.
  bool GetStats(VoiceMediaReceiveInfo* info,
                bool get_and_clear_legacy_stats) override {
    if (receive_stats_) {
      *info = *receive_stats_;
      return true;
    }
    return false;
  }

  absl::AnyInvocable<std::optional<VoiceMediaReceiveInfo>()> GetStatsTask(
      bool reset_legacy) override {
    return [this, safety = task_safety_.flag()]() mutable
               -> std::optional<VoiceMediaReceiveInfo> {
      if (!safety->alive()) {
        return std::nullopt;
      }
      VoiceMediaReceiveInfo info;
      if (GetStats(&info, /*get_and_clear_legacy_stats=*/false)) {
        return info;
      }
      return std::nullopt;
    };
  }

 private:
  ScopedTaskSafety task_safety_;
  std::optional<VoiceMediaReceiveInfo> receive_stats_;
};

// Fake VideoMediaChannel where the result of GetStats can be configured.
class FakeVideoMediaSendChannelForStats : public FakeVideoMediaSendChannel {
 public:
  explicit FakeVideoMediaSendChannelForStats(TaskQueueBase* network_thread)
      : FakeVideoMediaSendChannel(VideoOptions(), network_thread) {}

  void SetStats(const VideoMediaInfo& video_info) {
    send_stats_ = VideoMediaSendInfo();
    send_stats_->senders = video_info.senders;
    send_stats_->aggregated_senders = video_info.aggregated_senders;
    send_stats_->send_codecs = video_info.send_codecs;
  }

  // VideoMediaChannel overrides.
  bool GetStats(VideoMediaSendInfo* info) override {
    if (send_stats_) {
      *info = *send_stats_;
      return true;
    }
    return false;
  }

  absl::AnyInvocable<std::optional<VideoMediaSendInfo>()> GetStatsTask()
      override {
    return [this, safety = task_safety_.flag()]() mutable
               -> std::optional<VideoMediaSendInfo> {
      if (!safety->alive()) {
        return std::nullopt;
      }
      VideoMediaSendInfo info;
      if (GetStats(&info)) {
        return info;
      }
      return std::nullopt;
    };
  }

 private:
  ScopedTaskSafety task_safety_;
  std::optional<VideoMediaSendInfo> send_stats_;
};

class FakeVideoMediaReceiveChannelForStats
    : public FakeVideoMediaReceiveChannel {
 public:
  explicit FakeVideoMediaReceiveChannelForStats(TaskQueueBase* network_thread)
      : FakeVideoMediaReceiveChannel(VideoOptions(), network_thread) {}

  void SetStats(const VideoMediaInfo& video_info) {
    receive_stats_ = VideoMediaReceiveInfo();
    receive_stats_->receivers = video_info.receivers;
    receive_stats_->receive_codecs = video_info.receive_codecs;
  }

  // VideoMediaChannel overrides.
  bool GetStats(VideoMediaReceiveInfo* info) override {
    if (receive_stats_) {
      *info = *receive_stats_;
      return true;
    }
    return false;
  }

  absl::AnyInvocable<std::optional<VideoMediaReceiveInfo>()> GetStatsTask()
      override {
    return [this, safety = task_safety_.flag()]() mutable
               -> std::optional<VideoMediaReceiveInfo> {
      if (!safety->alive()) {
        return std::nullopt;
      }
      VideoMediaReceiveInfo info;
      if (GetStats(&info)) {
        return info;
      }
      return std::nullopt;
    };
  }

 private:
  ScopedTaskSafety task_safety_;
  std::optional<VideoMediaReceiveInfo> receive_stats_;
};

constexpr bool kDefaultRtcpMuxRequired = true;
constexpr bool kDefaultSrtpRequired = true;

class ChannelForTesting : public BaseChannel {
 public:
  ChannelForTesting(
      Thread* worker_thread,
      Thread* network_thread,
      Thread* signaling_thread,
      std::unique_ptr<MediaSendChannelInterface> send_channel,
      std::unique_ptr<MediaReceiveChannelInterface> receive_channel,
      const std::string& content_name,
      MediaType media_type,
      bool srtp_required,
      CryptoOptions crypto_options,
      UniqueRandomIdGenerator* ssrc_generator,
      std::string transport_name)
      : BaseChannel(worker_thread,
                    network_thread,
                    signaling_thread,
                    std::move(send_channel),
                    std::move(receive_channel),
                    content_name,
                    media_type,
                    srtp_required,
                    std::move(crypto_options),
                    ssrc_generator),
        test_transport_name_(std::move(transport_name)) {}

 private:
  absl::string_view transport_name() const override {
    return test_transport_name_;
  }

  const std::string test_transport_name_;
};

// This class is intended to be fed into the StatsCollector and
// RTCStatsCollector so that the stats functionality can be unit tested.
// Individual tests can configure this fake as needed to simulate scenarios
// under which to test the stats collectors.
//
// TODO: bugs.webrtc.org/470300031 - At the moment this class uses transceivers
// via the PlanB methods This needs to be fixed.
class FakePeerConnectionForStats : public FakePeerConnectionBase,
                                   public JsepTransportController::Observer {
 public:
  explicit FakePeerConnectionForStats(
      const Environment& env,
      Thread* network_thread = Thread::Current())
      : FakePeerConnectionBase(env),
        network_thread_(network_thread),
        worker_thread_(network_thread_),
        signaling_thread_(Thread::Current()),
        // TODO(hta): remove separate thread variables and use context.
        dependencies_(MakeDependencies(signaling_thread_, network_thread_)),
        context_(ConnectionContext::Create(env, &dependencies_)),
        data_channel_controller_(network_thread_),
        codec_lookup_helper_(context_.get(), env.field_trials()),
        ice_transport_factory_(std::make_unique<FakeIceTransportFactory>()) {
    JsepTransportController::Config config;
    config.ice_transport_factory = ice_transport_factory_.get();
    config.transport_observer = this;
    config.rtcp_handler = [](const CopyOnWriteBuffer& packet,
                             int64_t packet_time_us) {};
    config.un_demuxable_packet_handler =
        [](const RtpPacketReceived& parsed_packet) {};
    port_allocator_.emplace(env, network_thread_->socketserver(),
                            network_thread_);
    transport_controller_ = std::make_unique<JsepTransportController>(
        env, signaling_thread_, network_thread_, &(*port_allocator_),
        /*async_dns_resolver_factory=*/nullptr,
        /*lna_permission_factory=*/nullptr, std::move(config));
  }

  ~FakePeerConnectionForStats() override {
    for (auto transceiver : transceivers_) {
      transceiver->internal()->ClearChannel();
    }
    network_thread_->BlockingCall([&]() {
      transport_controller_.reset();
      port_allocator_ = std::nullopt;
    });
  }

  static PeerConnectionFactoryDependencies MakeDependencies(
      Thread* signaling_thread,
      Thread* network_thread) {
    PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network_thread;
    dependencies.signaling_thread = signaling_thread;
    EnableFakeMedia(dependencies);
    return dependencies;
  }

  PLAN_B_ONLY scoped_refptr<RtpSenderInterface> AddSender(
      scoped_refptr<RtpSenderInternal> sender) {
    // TODO(steveanton): Switch tests to use RtpTransceivers directly.
    auto sender_proxy = RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
        signaling_thread_, sender);
    GetOrCreateFirstTransceiverOfType(sender->media_type())
        ->internal()
        ->AddSenderPlanB(sender_proxy);
    return sender_proxy;
  }

  PLAN_B_ONLY void RemoveSender(scoped_refptr<RtpSenderInterface> sender) {
    GetOrCreateFirstTransceiverOfType(sender->media_type())
        ->internal()
        ->RemoveSenderPlanB(sender.get());
  }

  PLAN_B_ONLY scoped_refptr<RtpReceiverInterface> AddReceiver(
      scoped_refptr<RtpReceiverInternal> receiver) {
    // TODO(steveanton): Switch tests to use RtpTransceivers directly.
    auto receiver_proxy =
        RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
            signaling_thread_, worker_thread_, receiver);
    GetOrCreateFirstTransceiverOfType(receiver->media_type())
        ->internal()
        ->AddReceiverPlanB(receiver_proxy);
    return receiver_proxy;
  }

  PLAN_B_ONLY void RemoveReceiver(
      scoped_refptr<RtpReceiverInterface> receiver) {
    GetOrCreateFirstTransceiverOfType(receiver->media_type())
        ->internal()
        ->RemoveReceiverPlanB(receiver.get());
  }

  PLAN_B_ONLY std::pair<FakeVoiceMediaSendChannelForStats*,
                        FakeVoiceMediaReceiveChannelForStats*>
  AddVoiceChannel(const std::string& mid,
                  const std::string& transport_name,
                  VoiceMediaInfo initial_stats = VoiceMediaInfo()) {
    auto voice_media_send_channel =
        std::make_unique<FakeVoiceMediaSendChannelForStats>(network_thread_);
    auto voice_media_receive_channel =
        std::make_unique<FakeVoiceMediaReceiveChannelForStats>(network_thread_);
    auto* voice_media_send_channel_ptr = voice_media_send_channel.get();
    auto* voice_media_receive_channel_ptr = voice_media_receive_channel.get();
    auto voice_channel = std::make_unique<ChannelForTesting>(
        worker_thread_, network_thread_, signaling_thread_,
        std::move(voice_media_send_channel),
        std::move(voice_media_receive_channel), mid, MediaType::AUDIO,
        kDefaultSrtpRequired, CryptoOptions(), context_->ssrc_generator(),
        transport_name);
    auto transceiver =
        GetOrCreateFirstTransceiverOfType(webrtc::MediaType::AUDIO, mid)
            ->internal();
    if (transceiver->HasChannel()) {
      // This transceiver already has a channel, create a new one.
      transceiver =
          CreateTransceiverOfType(webrtc::MediaType::AUDIO, mid)->internal();
    }
    RTC_DCHECK(!transceiver->HasChannel());
    RTC_DCHECK(transceiver->mid());
    UpdateJsepTransportController(mid, transport_name);
    SetChannelAndPushMediaChannels(transceiver, std::move(voice_channel),
                                   voice_media_send_channel_ptr,
                                   voice_media_receive_channel_ptr);
    auto dtls_transport = transport_controller_->LookupDtlsTransportByMid(mid);
    if (!dtls_transport) {
      auto fake_dtls = std::make_unique<FakeDtlsTransport>(
          env(), transport_name, ICE_CANDIDATE_COMPONENT_RTP);
      auto wrapper = make_ref_counted<DtlsTransport>(fake_dtls.get());
      fake_dtls_transports_[mid] = std::move(fake_dtls);
      dtls_transport = wrapper;
    }
    transceiver->SetTransport(dtls_transport, transport_name);
    voice_media_send_channel_ptr->SetStats(initial_stats);
    voice_media_receive_channel_ptr->SetStats(initial_stats);
    return std::make_pair(voice_media_send_channel_ptr,
                          voice_media_receive_channel_ptr);
  }

  PLAN_B_ONLY std::pair<FakeVideoMediaSendChannelForStats*,
                        FakeVideoMediaReceiveChannelForStats*>
  AddVideoChannel(const std::string& mid,
                  const std::string& transport_name,
                  VideoMediaInfo initial_stats = VideoMediaInfo()) {
    auto video_media_send_channel =
        std::make_unique<FakeVideoMediaSendChannelForStats>(network_thread_);
    auto video_media_receive_channel =
        std::make_unique<FakeVideoMediaReceiveChannelForStats>(network_thread_);
    auto video_media_send_channel_ptr = video_media_send_channel.get();
    auto video_media_receive_channel_ptr = video_media_receive_channel.get();
    auto video_channel = std::make_unique<ChannelForTesting>(
        worker_thread_, network_thread_, signaling_thread_,
        std::move(video_media_send_channel),
        std::move(video_media_receive_channel), mid, MediaType::VIDEO,
        kDefaultSrtpRequired, CryptoOptions(), context_->ssrc_generator(),
        transport_name);
    auto transceiver =
        GetOrCreateFirstTransceiverOfType(webrtc::MediaType::VIDEO, mid)
            ->internal();
    if (transceiver->HasChannel()) {
      // This transceiver already has a channel, create a new one.
      transceiver =
          CreateTransceiverOfType(webrtc::MediaType::VIDEO, mid)->internal();
    }
    RTC_DCHECK(!transceiver->HasChannel());
    RTC_DCHECK(transceiver->mid());
    UpdateJsepTransportController(mid, transport_name);
    SetChannelAndPushMediaChannels(transceiver, std::move(video_channel),
                                   video_media_send_channel_ptr,
                                   video_media_receive_channel_ptr);
    auto dtls_transport = transport_controller_->LookupDtlsTransportByMid(mid);
    if (!dtls_transport) {
      auto fake_dtls = std::make_unique<FakeDtlsTransport>(
          env(), transport_name, ICE_CANDIDATE_COMPONENT_RTP);
      auto wrapper = make_ref_counted<DtlsTransport>(fake_dtls.get());
      fake_dtls_transports_[mid] = std::move(fake_dtls);
      dtls_transport = wrapper;
    }
    transceiver->SetTransport(dtls_transport, transport_name);
    video_media_send_channel_ptr->SetStats(initial_stats);
    video_media_receive_channel_ptr->SetStats(initial_stats);
    return std::make_pair(video_media_send_channel_ptr,
                          video_media_receive_channel_ptr);
  }

  void AddSctpDataChannel(const std::string& label) {
    AddSctpDataChannel(label, InternalDataChannelInit());
  }

  void AddSctpDataChannel(const std::string& label,
                          const InternalDataChannelInit& init) {
    AddSctpDataChannel(SctpDataChannel::Create(
        data_channel_controller_.weak_ptr(), label, false, init, std::nullopt,
        signaling_safety_.flag(), signaling_thread_, network_thread_));
  }

  void AddSctpDataChannel(scoped_refptr<SctpDataChannel> data_channel) {
    sctp_data_channels_.push_back(data_channel);
  }

  void SetTransportStats(const std::string& transport_name,
                         const TransportChannelStats& channel_stats) {
    SetTransportStats(transport_name,
                      std::vector<TransportChannelStats>{channel_stats});
  }

  void SetTransportStats(
      const std::string& transport_name,
      const std::vector<TransportChannelStats>& channel_stats_list) {
    TransportStats transport_stats;
    transport_stats.transport_name = transport_name;
    transport_stats.channel_stats = channel_stats_list;
    transport_stats_by_name_[transport_name] = transport_stats;
  }

  void SetCallStats(const Call::Stats& call_stats) { call_stats_ = call_stats; }

  void SetAudioDeviceStats(
      std::optional<AudioDeviceModule::Stats> audio_device_stats) {
    audio_device_stats_ = audio_device_stats;
  }

  void SetLocalCertificate(const std::string& transport_name,
                           scoped_refptr<RTCCertificate> certificate) {
    local_certificates_by_transport_[transport_name] = certificate;
  }

  void SetRemoteCertChain(const std::string& transport_name,
                          std::unique_ptr<SSLCertChain> chain) {
    remote_cert_chains_by_transport_[transport_name] = std::move(chain);
  }

  // PeerConnectionInterface overrides.

  std::vector<scoped_refptr<RtpSenderInterface>> GetSenders() const override {
    std::vector<scoped_refptr<RtpSenderInterface>> senders;
    for (auto transceiver : transceivers_) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
      for (auto sender : transceiver->internal()->senders()) {
        senders.push_back(sender);
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END()
    }
    return senders;
  }

  std::vector<scoped_refptr<RtpReceiverInterface>> GetReceivers()
      const override {
    std::vector<scoped_refptr<RtpReceiverInterface>> receivers;
    for (auto transceiver : transceivers_) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
      for (auto receiver : transceiver->internal()->receivers()) {
        receivers.push_back(receiver);
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END()
    }
    return receivers;
  }

  // PeerConnectionInternal overrides.

  Thread* network_thread() const override { return network_thread_; }

  Thread* worker_thread() const override { return worker_thread_; }

  JsepTransportController* transport_controller_n() override {
    return transport_controller_.get();
  }

  Thread* signaling_thread() const override { return signaling_thread_; }

  std::vector<scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>>
  GetTransceiversInternal() const override {
    return transceivers_;
  }

  // Matches PeerConnection::GetDataChannelStats() and
  // DataChannelController::GetDataChannelStats() which run on network_thread.
  std::vector<DataChannelStats> GetDataChannelStats() const override {
    RTC_DCHECK_RUN_ON(network_thread());
    std::vector<DataChannelStats> stats;
    for (const auto& channel : sctp_data_channels_)
      stats.push_back(channel->GetStats());
    return stats;
  }

  CandidateStatsList GetPooledCandidateStats() const override { return {}; }

  std::map<std::string, TransportStats> GetTransportStatsByNames(
      const std::set<std::string>& transport_names) override {
    RTC_DCHECK_RUN_ON(network_thread_);
    std::map<std::string, TransportStats> transport_stats_by_name;
    std::set<std::string> all_names = transport_names;
    // In some legacy stats tests, the fake PeerConnection's
    // JsepTransportController is not fully configured, causing
    // `GetTransportName` to return `std::nullopt`. This fallback loop ensures
    // stats are still retrieved for these transports.
    for (const auto& entry : transport_names_by_mid_) {
      all_names.insert(entry.second);
    }
    for (const std::string& transport_name : all_names) {
      transport_stats_by_name[transport_name] =
          GetTransportStatsByName(transport_name);
    }
    return transport_stats_by_name;
  }

  Call::Stats GetCallStats() override { return call_stats_; }

  std::optional<AudioDeviceModule::Stats> GetAudioDeviceStats() override {
    return audio_device_stats_;
  }

  bool GetLocalCertificate(
      const std::string& transport_name,
      scoped_refptr<RTCCertificate>* certificate) override {
    auto it = local_certificates_by_transport_.find(transport_name);
    if (it != local_certificates_by_transport_.end()) {
      *certificate = it->second;
      return true;
    } else {
      return false;
    }
  }

  std::unique_ptr<SSLCertChain> GetRemoteSSLCertChain(
      const std::string& transport_name) override {
    auto it = remote_cert_chains_by_transport_.find(transport_name);
    if (it != remote_cert_chains_by_transport_.end()) {
      return it->second->Clone();
    } else {
      return nullptr;
    }
  }
  PayloadTypePicker& payload_type_picker() override {
    return payload_type_picker_;
  }

 private:
  void SetChannelAndPushMediaChannels(
      RtpTransceiver* transceiver,
      std::unique_ptr<ChannelInterface> channel,
      MediaSendChannelInterface* send_channel,
      MediaReceiveChannelInterface* receive_channel) {
    std::string mid = channel->mid();
    transceiver->SetChannelForTest(
        std::move(channel), [this, mid = std::move(mid)]() {
          return transport_controller_->GetRtpTransport(mid);
        });
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    for (const auto& sender : transceiver->senders()) {
      sender->internal()->SetMediaChannel(send_channel);
    }
    for (const auto& receiver : transceiver->receivers()) {
      receiver->internal()->SetMediaChannel(receive_channel);
    }
    RTC_ALLOW_PLAN_B_DEPRECATION_END()
  }
  TransportStats GetTransportStatsByName(const std::string& transport_name) {
    auto it = transport_stats_by_name_.find(transport_name);
    if (it != transport_stats_by_name_.end()) {
      // If specific transport stats have been specified, return those.
      return it->second;
    }
    // Otherwise, generate some dummy stats.
    TransportChannelStats channel_stats;
    channel_stats.component = ICE_CANDIDATE_COMPONENT_RTP;
    TransportStats transport_stats;
    transport_stats.transport_name = transport_name;
    transport_stats.channel_stats.push_back(channel_stats);
    return transport_stats;
  }

  scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  GetOrCreateFirstTransceiverOfType(MediaType media_type,
                                    absl::string_view mid = "") {
    for (auto transceiver : transceivers_) {
      if (transceiver->internal()->media_type() == media_type) {
        // This is the first transceiver of this type - make sure it has the
        // requested mid set.
        if (!mid.empty() && !transceiver->internal()->mid()) {
          transceiver->internal()->set_mid(std::string(mid));
        }
        return transceiver;
      }
    }
    return CreateTransceiverOfType(media_type, mid);
  }

  scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  CreateTransceiverOfType(MediaType media_type, absl::string_view mid = "") {
    auto transceiver = RtpTransceiverProxyWithInternal<RtpTransceiver>::Create(
        signaling_thread_,
        make_ref_counted<RtpTransceiver>(env_, media_type, context_.get(),
                                         &codec_lookup_helper_, nullptr,
                                         AudioOptions(), VideoOptions()));
    transceiver->internal()->set_current_direction(
        RtpTransceiverDirection::kSendRecv);
    if (!mid.empty()) {
      transceiver->internal()->set_mid(std::string(mid));
    }
    transceivers_.push_back(transceiver);
    return transceiver;
  }

  bool OnTransportChanged(
      absl::string_view mid,
      RtpTransportInternal* rtp_transport,
      scoped_refptr<DtlsTransport> dtls_transport,
      DataChannelTransportInterface* data_channel_transport) override {
    return true;
  }

  void UpdateJsepTransportController(const std::string& mid,
                                     const std::string& transport_name) {
    transport_names_by_mid_[mid] = transport_name;

    SdpType type = SdpType::kOffer;
    ContentGroup bundle_group("BUNDLE");
    // Reconstruct the bundle group to include all contents sharing the
    // transport. For simplicity, we just rebuild the description with all
    // contents so far. This is a naive implementation sufficient for tests.
    auto description = std::make_unique<SessionDescription>();
    for (const auto& [mid_entry, transport_entry] : transport_names_by_mid_) {
      auto audio = std::make_unique<AudioContentDescription>();
      audio->set_protocol("UDP/TLS/RTP/SAVPF");
      ContentInfo content(MediaProtocolType::kRtp, mid_entry, std::move(audio));
      description->AddContent(std::move(content));
      description->AddTransportInfo(
          TransportInfo(mid_entry, TransportDescription("ufrag", "pwd")));
      if (transport_entry == transport_name) {
        bundle_group.AddContentName(mid_entry);
      }
    }
    // If we had multiple bundle groups we would need more logic, but for tests
    // we usually only test one bundle group or distinct transports.
    // This logic simplistically bundles everything that matches the current
    // transport_name.
    description->AddGroup(bundle_group);

    transport_controller_->SetLocalDescription(type, description.get(),
                                               nullptr);
    transport_controller_->MaybeStartGathering(
        /*discard_candidate_pool=*/false);
  }

  Thread* const network_thread_;
  Thread* const worker_thread_;
  Thread* const signaling_thread_;
  ScopedTaskSafety signaling_safety_;

  PeerConnectionFactoryDependencies dependencies_;
  scoped_refptr<ConnectionContext> context_;

  std::vector<scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>>
      transceivers_;

  FakeDataChannelController data_channel_controller_;

  std::vector<scoped_refptr<SctpDataChannel>> sctp_data_channels_;

  std::map<std::string, TransportStats> transport_stats_by_name_;

  Call::Stats call_stats_;

  std::optional<AudioDeviceModule::Stats> audio_device_stats_;

  std::map<std::string, scoped_refptr<RTCCertificate>>
      local_certificates_by_transport_;
  std::map<std::string, std::unique_ptr<SSLCertChain>>
      remote_cert_chains_by_transport_;
  PayloadTypePicker payload_type_picker_;
  FakeCodecLookupHelper codec_lookup_helper_;
  std::unique_ptr<IceTransportFactory> ice_transport_factory_;
  std::optional<FakePortAllocator> port_allocator_;
  std::unique_ptr<JsepTransportController> transport_controller_;
  std::map<std::string, std::string> transport_names_by_mid_;
  std::map<std::string, std::unique_ptr<FakeDtlsTransport>>
      fake_dtls_transports_;
};

}  // namespace webrtc

#endif  // PC_TEST_FAKE_PEER_CONNECTION_FOR_STATS_H_
