/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "pc/datagram_connection_internal.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/datagram_connection.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/enums.h"
#include "api/units/timestamp.h"
#include "call/rtp_demuxer.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/p2p_transport_channel.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_transport.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/dtls_packet_processor.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/dtls_transport.h"
#include "pc/ice_transport.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"

namespace webrtc {
namespace {
using PacketMetadata = DatagramConnection::Observer::PacketMetadata;

const size_t kMaxRtpPacketLen = 2048;
const size_t kIceUfragLength = 16;
const size_t kMinPayloadLengthForRtpCheck = 2;

// Helper function to create IceTransportInit
IceTransportInit CreateIceTransportInit(const Environment& env,
                                        PortAllocator* allocator) {
  IceTransportInit init(env);
  init.set_port_allocator(allocator);
  return init;
}

// Helper function to create DtlsTransportInternal
std::unique_ptr<DtlsTransportInternal> CreateDtlsTransportInternal(
    const Environment& env,
    IceTransportInternal* transport_channel) {
  return std::make_unique<DtlsTransportInternalImpl>(
      env, make_ref_counted<IceTransportWithPointer>(transport_channel),
      CryptoOptions{},
      /*ssl_max_version=*/SSL_PROTOCOL_DTLS_13);
}

bool IsRtpOrRtcpPacket(uint8_t first_byte) {
  return (first_byte & 0xc0) == 0x80;
}

uint8_t ParsePayloadType(uint8_t second_byte) {
  return second_byte & 0x7F;
}

bool PayloadTypeIsReservedForRtcp(uint8_t payload_type) {
  return 64 <= payload_type && payload_type < 96;
}

}  // namespace

DatagramConnectionInternal::DatagramConnectionInternal(
    const Environment& env,
    std::unique_ptr<PortAllocator> port_allocator,
    absl::string_view transport_name,
    bool ice_controlling,
    scoped_refptr<RTCCertificate> certificate,
    std::unique_ptr<Observer> observer,
    WireProtocol wire_protocol,
    std::unique_ptr<IceTransportInternal> custom_ice_transport_internal)
    : env_(env),
      wire_protocol_(wire_protocol),
      observer_(std::move(observer)),
      port_allocator_(std::move(port_allocator)),
      transport_channel_(
          custom_ice_transport_internal
              ? std::move(custom_ice_transport_internal)
              : P2PTransportChannel::Create(
                    transport_name,
                    ICE_CANDIDATE_COMPONENT_RTP,
                    CreateIceTransportInit(env, port_allocator_.get()))),
      internal_transport_(
          CreateDtlsTransportInternal(env, transport_channel_.get())),
      dtls_transport_(
          make_ref_counted<DtlsTransport>(internal_transport_.get())),
      dtls_srtp_transport_(
          wire_protocol_ == WireProtocol::kDtlsSrtp
              ? std::make_unique<DtlsSrtpTransport>(/*rtcp_mux_enabled=*/true,
                                                    env.field_trials())
              : nullptr),
      ice_username_fragment_(CreateRandomString(kIceUfragLength)),
      ice_password_(CreateRandomString(ICE_PWD_LENGTH)) {
  RTC_CHECK(observer_);

  internal_transport_->RegisterReceivedPacketCallback(
      this, [this](PacketTransportInternal* transport,
                   const ReceivedIpPacket& packet) {
        if (packet.decryption_info() != ReceivedIpPacket::kDtlsDecrypted) {
          // Ignore eg SRTP encrypted packets which are handled within
          // dtls_srtp_transport_.
          return;
        }
        this->OnDtlsPacket(
            CopyOnWriteBuffer(packet.payload()),
            packet.arrival_time().value_or(Timestamp::MinusInfinity()),
            packet.ecn());
      });
  if (wire_protocol_ == WireProtocol::kDtlsSrtp) {
    dtls_srtp_transport_->SetDtlsTransports(internal_transport_.get(),
                                            /*rtcp_dtls_transport=*/nullptr);
  }

  internal_transport_->ice_transport()->SubscribeCandidateGathered(
      this,
      std::bind_front(&DatagramConnectionInternal::OnCandidateGathered, this));

  if (wire_protocol_ == WireProtocol::kDtls ||
      wire_protocol_ == WireProtocol::kDtlsWithFeedback) {
    internal_transport_->SubscribeWritableState(this, [this](bool is_writable) {
      this->OnWritableStatePossiblyChanged();
    });
  } else {
    dtls_srtp_transport_->SubscribeWritableState(
        this, [this](bool) { this->OnWritableStatePossiblyChanged(); });
  }

  transport_channel_->SubscribeIceTransportStateChanged(
      this, [this](IceTransportInternal* transport) {
        if (transport->GetIceTransportState() ==
            webrtc::IceTransportState::kFailed) {
          OnConnectionError();
        }
      });
  internal_transport_->SubscribeDtlsHandshakeError(
      this, [this](webrtc::SSLHandshakeError) { OnConnectionError(); });

  internal_transport_->SubscribeDtlsTransportState(
      this, [this](DtlsTransportInternal* transport, DtlsTransportState state) {
        dtls_transport_->OnInternalDtlsState(transport);
      });

  // TODO(crbug.com/443019066): Bind to SetCandidateErrorCallback() and
  // propagate back to the Observer.
  internal_transport_->ice_transport()->SetIceParameters(
      IceParameters(ice_username_fragment_, ice_password_,
                    /*ice_renomination=*/false));
  internal_transport_->ice_transport()->SetIceRole(
      ice_controlling ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
  internal_transport_->ice_transport()->MaybeStartGathering();

  if (wire_protocol_ == WireProtocol::kDtlsSrtp) {
    // Match everything for our fixed SSRC (should be everything).
    RtpDemuxerCriteria demuxer_criteria = RtpDemuxerCriteria::MatchAny();
    dtls_srtp_transport_->RegisterRtpDemuxerSink(demuxer_criteria, this);

    dtls_srtp_transport_->SubscribeSentPacket(
        this, [this](const SentPacketInfo& packet) { OnSentPacket(packet); });

    dtls_srtp_transport_->SubscribeRtcpPacketReceived(
        this, [this](CopyOnWriteBuffer buffer,
                     std::optional<Timestamp> packet_time, EcnMarking ecn) {
          PacketMetadata metadata{
              .receive_time = packet_time.value_or(Timestamp::MinusInfinity()),
              .ecn = ecn};
          observer_->OnPacketReceived(buffer, metadata);
        });
  } else {
    internal_transport_->ice_transport()->SubscribeSentPacket(
        this, [this](PacketTransportInternal*, const SentPacketInfo& packet) {
          OnSentPacket(packet);
        });
  }

  if (wire_protocol_ == WireProtocol::kDtlsWithFeedback) {
    packet_processor_ = std::make_unique<DtlsPacketProcessor>();
  }

  RTC_CHECK(internal_transport_->SetLocalCertificate(certificate));
}

DatagramConnectionInternal::~DatagramConnectionInternal() {
  dtls_transport_->Clear(internal_transport_.get());
}

void DatagramConnectionInternal::SetRemoteIceParameters(
    const IceParameters& ice_parameters) {
  if (current_state_ != State::kActive) {
    // TODO(crbug.com/443019066): Propagate an error back to the caller.
    return;
  }

  internal_transport_->ice_transport()->SetRemoteIceParameters(ice_parameters);
}

void DatagramConnectionInternal::AddRemoteCandidate(
    const Candidate& candidate) {
  if (current_state_ != State::kActive) {
    // TODO(crbug.com/443019066): Propagate an error back to the caller.
    return;
  }

  internal_transport_->ice_transport()->AddRemoteCandidate(candidate);
}

bool DatagramConnectionInternal::Writable() {
  if (current_state_ != State::kActive) {
    return false;
  }
  if (wire_protocol_ == WireProtocol::kDtls ||
      wire_protocol_ == WireProtocol::kDtlsWithFeedback) {
    return internal_transport_->writable();
  }
  return internal_transport_->ice_transport()->writable() &&
         dtls_srtp_transport_->IsSrtpActive();
}

void DatagramConnectionInternal::SetRemoteDtlsParameters(
    absl::string_view digestAlgorithm,
    const uint8_t* digest,
    size_t digest_len,
    SSLRole ssl_role) {
  if (current_state_ != State::kActive) {
    // TODO(crbug.com/443019066): Propagate an error back to the caller.
    return;
  }

  webrtc::SSLRole mapped_ssl_role =
      ssl_role == SSLRole::kClient ? SSL_CLIENT : SSL_SERVER;
  internal_transport_->SetRemoteParameters(digestAlgorithm, digest, digest_len,
                                           mapped_ssl_role);
}

void DatagramConnectionInternal::SendPackets(
    std::span<PacketSendParameters> packets) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  for (size_t i = 0; i < packets.size(); ++i) {
    SendSinglePacket(packets[i],
                     /*last_packet_in_batch=*/i == packets.size() - 1);
  }
}

void DatagramConnectionInternal::SendSinglePacket(
    const PacketSendParameters& packet,
    bool last_packet_in_batch) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  if (current_state_ != State::kActive) {
    DispatchSendOutcome(packet.id, Observer::SendOutcome::Status::kNotSent);
    return;
  }

  AsyncSocketPacketOptions options;
  options.packet_id = packet.id;
  options.batchable = true;
  options.last_packet_in_batch = last_packet_in_batch;

  if (wire_protocol_ == WireProtocol::kDtls) {
    // Directly send the payload inside a DTLS packet.
    internal_transport_->SendPacket(
        reinterpret_cast<const char*>(packet.payload.data()),
        packet.payload.size(), options);
    return;
  }

  if (wire_protocol_ == WireProtocol::kDtlsWithFeedback) {
    // Frame the payload with a transport header (and possibly piggybacked
    // feedback) before sending it inside a DTLS packet. The provider records
    // the packet internally for later correlation with incoming feedback.
    CopyOnWriteBuffer framed = packet_processor_->ProcessOutgoingPacket(
        packet.payload, env_.clock().CurrentTime());
    if (internal_transport_->SendPacket(
            reinterpret_cast<const char*>(framed.cdata()), framed.size(),
            options) < 0) {
      DispatchSendOutcome(packet.id, Observer::SendOutcome::Status::kNotSent);
    }
    return;
  }

  if (!dtls_srtp_transport_->IsSrtpActive()) {
    RTC_LOG(LS_ERROR) << "Dropping packet on non-active SRTP connection";
    DispatchSendOutcome(packet.id, Observer::SendOutcome::Status::kNotSent);
    return;
  }

  if (packet.payload.size() >= kMinPayloadLengthForRtpCheck &&
      IsRtpOrRtcpPacket(packet.payload[0])) {
    // Copy the payload into a buffer with some extra capacity to allow space
    // for the SRTP encryption tag to be added.
    CopyOnWriteBuffer buffer(packet.payload.data(), packet.payload.size(),
                             kMaxRtpPacketLen);

    // Provide the flag PF_SRTP_BYPASS as these packets are being encrypted by
    // SRTP, so should bypass DTLS encryption.
    uint8_t send_flags = PF_SRTP_BYPASS;
    bool send_successful;
    if (PayloadTypeIsReservedForRtcp(ParsePayloadType(packet.payload[1]))) {
      send_successful =
          dtls_srtp_transport_->SendRtcpPacket(&buffer, options, send_flags);
    } else {
      send_successful =
          dtls_srtp_transport_->SendRtpPacket(&buffer, options, send_flags);
    }

    if (!send_successful) {
      DispatchSendOutcome(packet.id, Observer::SendOutcome::Status::kNotSent);
    }
  } else {
    // Running DTLS-SRTP but not given an RTP/RTCP packet, so just DTLS encrypt.
    if (internal_transport_->SendPacket(
            reinterpret_cast<const char*>(packet.payload.data()),
            packet.payload.size(), options) < 0) {
      DispatchSendOutcome(packet.id, Observer::SendOutcome::Status::kNotSent);
    }
  }
}

void DatagramConnectionInternal::Terminate(
    absl::AnyInvocable<void()> terminate_complete_callback) {
  if (current_state_ != State::kActive) {
    terminate_complete_callback();
    return;
  }

  if (wire_protocol_ == WireProtocol::kDtlsSrtp) {
    dtls_srtp_transport_->UnregisterRtpDemuxerSink(this);
  }
  // TODO(crbug.com/443019066): Once we need asynchronous termination, set state
  // to TerminationInProgress here and Terminated later once done.
  current_state_ = State::kTerminated;
  terminate_complete_callback();
}
void DatagramConnectionInternal::OnCandidateGathered(
    IceTransportInternal*,
    const Candidate& candidate) {
  if (current_state_ != State::kActive) {
    return;
  }
  observer_->OnCandidateGathered(candidate);
}

void DatagramConnectionInternal::OnTransportWritableStateChanged(
    PacketTransportInternal*) {
  OnWritableStatePossiblyChanged();
}

void DatagramConnectionInternal::OnWritableStatePossiblyChanged() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (current_state_ != State::kActive) {
    return;
  }
  bool writable = Writable();
  if (last_writable_state_ != writable) {
    observer_->OnWritableChange();
    last_writable_state_ = writable;
  }
}

void DatagramConnectionInternal::OnConnectionError() {
  if (current_state_ != State::kActive) {
    return;
  }
  observer_->OnConnectionError();
}

void DatagramConnectionInternal::OnRtpPacket(const RtpPacketReceived& packet) {
  if (current_state_ != State::kActive) {
    return;
  }
  PacketMetadata metadata{.receive_time = packet.arrival_time(),
                          .ecn = packet.ecn()};
  observer_->OnPacketReceived(packet.Buffer(), metadata);
}

void DatagramConnectionInternal::OnDtlsPacket(CopyOnWriteBuffer packet,
                                              Timestamp receive_time,
                                              EcnMarking ecn) {
  if (current_state_ != State::kActive) {
    return;
  }
  PacketMetadata metadata{.receive_time = receive_time, .ecn = ecn};
  if (wire_protocol_ == WireProtocol::kDtlsWithFeedback) {
    // Strip the transport framing and route any parsed feedback to the feedback
    // provider; deliver only the application payload (if any) to the app,
    // through the same path as every other wire protocol.
    std::optional<CopyOnWriteBuffer> payload =
        packet_processor_->ProcessIncomingPacket(std::move(packet),
                                                 receive_time, ecn);
    if (payload) {
      observer_->OnPacketReceived(
          std::span<const uint8_t>(payload->cdata(), payload->size()),
          metadata);
    }
    return;
  }
  observer_->OnPacketReceived(packet, metadata);
}

void DatagramConnectionInternal::OnSentPacket(const SentPacketInfo& sent_info) {
  // Ignore internal transport packets (e.g. DTLS handshakes, session tickets,
  // STUN connectivity checks) which are sent with the default packet ID of -1.
  if (sent_info.packet_id == -1) {
    return;
  }
  Observer::SendOutcome outcome{
      .id = static_cast<DatagramConnection::PacketId>(sent_info.packet_id),
      .status = Observer::SendOutcome::Status::kSuccess,
      .send_time = sent_info.send_time_ms >= 0
                       ? Timestamp::Millis(sent_info.send_time_ms)
                       : Timestamp::MinusInfinity(),
      .bytes_sent = sent_info.info.packet_size_bytes};
  observer_->OnSendOutcome(outcome);
}

void DatagramConnectionInternal::DispatchSendOutcome(
    PacketId id,
    Observer::SendOutcome::Status status) {
  Observer::SendOutcome outcome{.id = id, .status = status};
  observer_->OnSendOutcome(outcome);
}

}  // namespace webrtc
