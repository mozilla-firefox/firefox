/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/peer_scenario/scenario_connection.h"

#include "absl/memory/memory.h"
#include "media/base/rtp_utils.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/client/basic_port_allocator.h"
#include "pc/channel.h"
#include "pc/jsep_transport_controller.h"
#include "pc/rtp_transport_internal.h"
#include "pc/session_description.h"
#include "rtc_base/task_queue_for_test.h"

namespace webrtc {
class ScenarioIceConnectionImpl : public ScenarioIceConnection,
                                  public sigslot::has_slots<>,
                                  private JsepTransportController::Observer,
                                  private RtpPacketSinkInterface {
 public:
  ScenarioIceConnectionImpl(const Environment& env,
                            test::NetworkEmulationManagerImpl* net,
                            IceConnectionObserver* observer);
  ~ScenarioIceConnectionImpl() override;

  void SendRtpPacket(rtc::ArrayView<const uint8_t> packet_view) override;
  void SendRtcpPacket(rtc::ArrayView<const uint8_t> packet_view) override;

  void SetRemoteSdp(SdpType type, const std::string& remote_sdp) override;
  void SetLocalSdp(SdpType type, const std::string& local_sdp) override;

  EmulatedEndpoint* endpoint() override { return endpoint_; }
  const cricket::TransportDescription& transport_description() const override {
    return transport_description_;
  }

 private:
  JsepTransportController::Config CreateJsepConfig();
  bool OnTransportChanged(
      const std::string& mid,
      RtpTransportInternal* rtp_transport,
      rtc::scoped_refptr<DtlsTransport> dtls_transport,
      DataChannelTransportInterface* data_channel_transport) override;

  void OnRtpPacket(const RtpPacketReceived& packet) override;
  void OnCandidates(const std::string& mid,
                    const std::vector<cricket::Candidate>& candidates);

  IceConnectionObserver* const observer_;
  EmulatedEndpoint* const endpoint_;
  EmulatedNetworkManagerInterface* const manager_;
  rtc::Thread* const signaling_thread_;
  rtc::Thread* const network_thread_;
  rtc::scoped_refptr<rtc::RTCCertificate> const certificate_
      RTC_GUARDED_BY(network_thread_);
  cricket::TransportDescription const transport_description_
      RTC_GUARDED_BY(signaling_thread_);
  std::unique_ptr<cricket::BasicPortAllocator> port_allocator_
      RTC_GUARDED_BY(network_thread_);
  std::unique_ptr<JsepTransportController> jsep_controller_;
  RtpTransportInternal* rtp_transport_ RTC_GUARDED_BY(network_thread_) =
      nullptr;
  std::unique_ptr<SessionDescriptionInterface> remote_description_
      RTC_GUARDED_BY(signaling_thread_);
  std::unique_ptr<SessionDescriptionInterface> local_description_
      RTC_GUARDED_BY(signaling_thread_);
};

std::unique_ptr<ScenarioIceConnection> ScenarioIceConnection::Create(
    const Environment& env,
    webrtc::test::NetworkEmulationManagerImpl* net,
    IceConnectionObserver* observer) {
  return std::make_unique<ScenarioIceConnectionImpl>(env, net, observer);
}

ScenarioIceConnectionImpl::ScenarioIceConnectionImpl(
    const Environment& env,
    test::NetworkEmulationManagerImpl* net,
    IceConnectionObserver* observer)
    : observer_(observer),
      endpoint_(net->CreateEndpoint(EmulatedEndpointConfig())),
      manager_(net->CreateEmulatedNetworkManagerInterface({endpoint_})),
      signaling_thread_(rtc::Thread::Current()),
      network_thread_(manager_->network_thread()),
      certificate_(rtc::RTCCertificate::Create(
          rtc::SSLIdentity::Create("", ::rtc::KT_DEFAULT))),
      transport_description_(
          /*transport_options*/ {},
          rtc::CreateRandomString(cricket::ICE_UFRAG_LENGTH),
          rtc::CreateRandomString(cricket::ICE_PWD_LENGTH),
          cricket::IceMode::ICEMODE_FULL,
          cricket::ConnectionRole::CONNECTIONROLE_PASSIVE,
          rtc::SSLFingerprint::CreateFromCertificate(*certificate_.get())
              .get()),
      port_allocator_(
          new cricket::BasicPortAllocator(manager_->network_manager(),
                                          manager_->packet_socket_factory())),
      jsep_controller_(
          new JsepTransportController(env,
                                      network_thread_,
                                      port_allocator_.get(),
                                      /*async_resolver_factory*/ nullptr,
                                      CreateJsepConfig())) {
  SendTask(network_thread_, [this] {
    RTC_DCHECK_RUN_ON(network_thread_);
    uint32_t flags = cricket::PORTALLOCATOR_DISABLE_TCP;
    port_allocator_->set_flags(port_allocator_->flags() | flags);
    port_allocator_->Initialize();
    RTC_CHECK(port_allocator_->SetConfiguration(/*stun_servers*/ {},
                                                /*turn_servers*/ {}, 0,
                                                webrtc::NO_PRUNE));
    jsep_controller_->SetLocalCertificate(certificate_);
  });
}

ScenarioIceConnectionImpl::~ScenarioIceConnectionImpl() {
  SendTask(network_thread_, [this] {
    RTC_DCHECK_RUN_ON(network_thread_);
    jsep_controller_.reset();
    port_allocator_.reset();
    rtp_transport_ = nullptr;
  });
}

JsepTransportController::Config ScenarioIceConnectionImpl::CreateJsepConfig() {
  JsepTransportController::Config config;
  config.transport_observer = this;
  config.bundle_policy =
      PeerConnectionInterface::BundlePolicy::kBundlePolicyMaxBundle;
  config.rtcp_handler = [this](const rtc::CopyOnWriteBuffer& packet,
                               int64_t packet_time_us) {
    RTC_DCHECK_RUN_ON(network_thread_);
    observer_->OnPacketReceived(packet);
  };
  return config;
}

void ScenarioIceConnectionImpl::SendRtpPacket(
    rtc::ArrayView<const uint8_t> packet_view) {
  rtc::CopyOnWriteBuffer packet(packet_view.data(), packet_view.size(),
                                ::cricket::kMaxRtpPacketLen);
  network_thread_->PostTask([this, packet = std::move(packet)]() mutable {
    RTC_DCHECK_RUN_ON(network_thread_);
    if (rtp_transport_ != nullptr)
      rtp_transport_->SendRtpPacket(&packet, rtc::PacketOptions(),
                                    cricket::PF_SRTP_BYPASS);
  });
}

void ScenarioIceConnectionImpl::SendRtcpPacket(
    rtc::ArrayView<const uint8_t> packet_view) {
  rtc::CopyOnWriteBuffer packet(packet_view.data(), packet_view.size(),
                                ::cricket::kMaxRtpPacketLen);
  network_thread_->PostTask([this, packet = std::move(packet)]() mutable {
    RTC_DCHECK_RUN_ON(network_thread_);
    if (rtp_transport_ != nullptr)
      rtp_transport_->SendRtcpPacket(&packet, rtc::PacketOptions(),
                                     cricket::PF_SRTP_BYPASS);
  });
}
void ScenarioIceConnectionImpl::SetRemoteSdp(SdpType type,
                                             const std::string& remote_sdp) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  remote_description_ = webrtc::CreateSessionDescription(type, remote_sdp);
  jsep_controller_->SubscribeIceCandidateGathered(
      [this](const std::string& transport,
             const std::vector<cricket::Candidate>& candidate) {
        ScenarioIceConnectionImpl::OnCandidates(transport, candidate);
      });

  auto res = jsep_controller_->SetRemoteDescription(
      remote_description_->GetType(),
      local_description_ ? local_description_->description() : nullptr,
      remote_description_->description());
  RTC_CHECK(res.ok()) << res.message();
  RtpDemuxerCriteria criteria;
  for (const auto& content : remote_description_->description()->contents()) {
    for (const auto& codec : content.media_description()->codecs()) {
      criteria.payload_types().insert(codec.id);
    }
  }

  network_thread_->PostTask([this, criteria]() {
    RTC_DCHECK_RUN_ON(network_thread_);
    RTC_DCHECK(rtp_transport_);
    rtp_transport_->RegisterRtpDemuxerSink(criteria, this);
  });
}

void ScenarioIceConnectionImpl::SetLocalSdp(SdpType type,
                                            const std::string& local_sdp) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  local_description_ = webrtc::CreateSessionDescription(type, local_sdp);
  auto res = jsep_controller_->SetLocalDescription(
      local_description_->GetType(), local_description_->description(),
      remote_description_ ? remote_description_->description() : nullptr);
  RTC_CHECK(res.ok()) << res.message();
  jsep_controller_->MaybeStartGathering();
}

bool ScenarioIceConnectionImpl::OnTransportChanged(
    const std::string& mid,
    RtpTransportInternal* rtp_transport,
    rtc::scoped_refptr<DtlsTransport> dtls_transport,
    DataChannelTransportInterface* data_channel_transport) {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (rtp_transport == nullptr) {
    rtp_transport_->UnregisterRtpDemuxerSink(this);
  } else {
    RTC_DCHECK(rtp_transport_ == nullptr || rtp_transport_ == rtp_transport);
    if (rtp_transport_ != rtp_transport) {
      rtp_transport_ = rtp_transport;
    }
    RtpDemuxerCriteria criteria(mid);
    rtp_transport_->RegisterRtpDemuxerSink(criteria, this);
  }
  return true;
}

void ScenarioIceConnectionImpl::OnRtpPacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  observer_->OnPacketReceived(packet.Buffer());
}

void ScenarioIceConnectionImpl::OnCandidates(
    const std::string& mid,
    const std::vector<cricket::Candidate>& candidates) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  observer_->OnIceCandidates(mid, candidates);
}

}  // namespace webrtc
