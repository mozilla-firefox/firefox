/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/srtp_transport.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "api/crypto/crypto_options.h"
#include "api/field_trials_view.h"
#include "api/rtp_header_extension_id.h"
#include "api/units/timestamp.h"
#include "call/rtp_demuxer.h"
#include "media/base/rtp_utils.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "p2p/base/packet_transport_internal.h"
#include "pc/rtp_transport.h"
#include "pc/srtp_session.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/trace_event.h"

namespace webrtc {

SrtpTransport::SrtpTransport(bool rtcp_mux_enabled,
                             const FieldTrialsView& field_trials)
    : RtpTransport(rtcp_mux_enabled, field_trials),
      field_trials_(field_trials) {}

bool SrtpTransport::SendRtpPacket(CopyOnWriteBuffer* packet,
                                  const AsyncSocketPacketOptions& options,
                                  int flags) {
  RTC_DCHECK(packet);
  if (!IsSrtpActive()) {
    RTC_LOG(LS_ERROR)
        << "Failed to send the packet because SRTP transport is inactive.";
    return false;
  }
  TRACE_EVENT0("webrtc", "SRTP Encode");
  bool res = ProtectRtp(*packet);
  if (!res) {
    uint16_t seq_num = ParseRtpSequenceNumber(*packet);
    uint32_t ssrc = ParseRtpSsrc(*packet);
    RTC_LOG(LS_ERROR) << "Failed to protect RTP packet: size=" << packet->size()
                      << ", seqnum=" << seq_num << ", SSRC=" << ssrc;
    return false;
  }

  return RtpTransport::SendRtpPacket(packet, options, flags);
}

bool SrtpTransport::SendRtcpPacket(CopyOnWriteBuffer* packet,
                                   const AsyncSocketPacketOptions& options,
                                   int flags) {
  RTC_DCHECK(packet);
  if (!IsSrtpActive()) {
    RTC_LOG(LS_ERROR)
        << "Failed to send the packet because SRTP transport is inactive.";
    return false;
  }

  TRACE_EVENT0("webrtc", "SRTP Encode");
  if (!ProtectRtcp(*packet)) {
    int type = -1;
    GetRtcpType(packet->data(), packet->size(), &type);
    RTC_LOG(LS_ERROR) << "Failed to protect RTCP packet: size="
                      << packet->size() << ", type=" << type;
    return false;
  }

  return RtpTransport::SendRtcpPacket(packet, options, flags);
}

void SrtpTransport::OnRtpPacketReceived(const ReceivedIpPacket& packet) {
  TRACE_EVENT0("webrtc", "SrtpTransport::OnRtpPacketReceived");
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING)
        << "Inactive SRTP transport received an RTP packet. Drop it.";
    return;
  }

  CopyOnWriteBuffer payload(packet.payload());
  if (!UnprotectRtp(payload)) {
    // Limit the error logging to avoid excessive logs when there are lots of
    // bad packets.
    const int kFailureLogThrottleCount = 100;
    if (decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      RTC_LOG(LS_ERROR) << "Failed to unprotect RTP packet: size="
                        << payload.size()
                        << ", seqnum=" << ParseRtpSequenceNumber(payload)
                        << ", SSRC=" << ParseRtpSsrc(payload)
                        << ", previous failure count: "
                        << decryption_failure_count_;
    }
    ++decryption_failure_count_;
    return;
  }
  DemuxPacket(std::move(payload),
              packet.arrival_time().value_or(Timestamp::MinusInfinity()),
              packet.ecn());
}

void SrtpTransport::OnRtcpPacketReceived(const ReceivedIpPacket& packet) {
  TRACE_EVENT0("webrtc", "SrtpTransport::OnRtcpPacketReceived");
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING)
        << "Inactive SRTP transport received an RTCP packet. Drop it.";
    return;
  }
  CopyOnWriteBuffer payload(packet.payload());
  if (!UnprotectRtcp(payload)) {
    // Limit the error logging to avoid excessive logs when there are lots of
    // bad packets.
    const int kFailureLogThrottleCount = 100;
    if (rtcp_decryption_failure_count_ % kFailureLogThrottleCount == 0) {
      int type = -1;
      GetRtcpType(payload.data(), payload.size(), &type);
      RTC_LOG(LS_ERROR) << "Failed to unprotect RTCP packet: size="
                        << payload.size() << ", type=" << type
                        << ", previous failure count: "
                        << rtcp_decryption_failure_count_;
    }
    ++rtcp_decryption_failure_count_;
    return;
  }
  SendRtcpPacketReceived(std::move(payload), packet.arrival_time(),
                         packet.ecn());
}

void SrtpTransport::OnNetworkRouteChanged(
    std::optional<NetworkRoute> network_route) {
  // Only append the SRTP overhead when there is a selected network route.
  if (network_route) {
    int srtp_overhead = 0;
    if (IsSrtpActive()) {
      GetSrtpOverhead(&srtp_overhead);
    }
    srtp_overhead_ = srtp_overhead;
    network_route->packet_overhead += srtp_overhead;
  }
  SendNetworkRouteChanged(network_route);
}

void SrtpTransport::OnWritableState(PacketTransportInternal* packet_transport) {
  SendWritableState(IsWritable(/*rtcp=*/false) && IsWritable(/*rtcp=*/true));
}

bool SrtpTransport::UseCryptex(CryptoOptions::Srtp::CryptexPolicy policy,
                               bool negotiated) {
  cryptex_policy_ = policy;
  use_cryptex_ = negotiated;
  if (policy == CryptoOptions::Srtp::CryptexPolicy::kDisabled) {
    return true;
  }
  bool require = policy == CryptoOptions::Srtp::CryptexPolicy::kRequire;
  if (send_session_) {
    if (!send_session_->UseCryptex(use_cryptex_, require,
                                   /*sending_session=*/true)) {
      RTC_LOG(LS_ERROR) << "Updating send session cryptex failed";
      return false;
    }
  }
  if (recv_session_) {
    if (!recv_session_->UseCryptex(use_cryptex_, require,
                                   /*sending_session=*/false)) {
      RTC_LOG(LS_ERROR) << "Updating recv session cryptex failed";
      return false;
    }
  }

  return true;
}

bool SrtpTransport::SetRtpParams(
    int send_crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& send_key,
    const std::vector<RtpHeaderExtensionId>& send_extension_ids,
    int recv_crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& recv_key,
    const std::vector<RtpHeaderExtensionId>& recv_extension_ids) {
  // If parameters are being set for the first time, we should create new SRTP
  // sessions and call "SetSend/SetReceive". Otherwise we should call
  // "UpdateSend"/"UpdateReceive" on the existing sessions, which will
  // internally call "srtp_update".
  bool new_sessions = false;
  if (!send_session_) {
    RTC_DCHECK(!recv_session_);
    CreateSrtpSessions();
    new_sessions = true;
  }
  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !send_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/true)) {
    RTC_LOG(LS_ERROR) << "Updating send session cryptex failed";
    return false;
  }
  bool ret = new_sessions
                 ? send_session_->SetSend(send_crypto_suite, send_key,
                                          send_extension_ids)
                 : send_session_->UpdateSend(send_crypto_suite, send_key,
                                             send_extension_ids);
  if (!ret) {
    ResetParams();
    return false;
  }

  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !recv_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/false)) {
    RTC_LOG(LS_ERROR) << "Updating recv session cryptex failed";
    return false;
  }
  ret = new_sessions ? recv_session_->SetReceive(recv_crypto_suite, recv_key,
                                                 recv_extension_ids)
                     : recv_session_->UpdateReceive(recv_crypto_suite, recv_key,
                                                    recv_extension_ids);
  if (!ret) {
    ResetParams();
    return false;
  }
  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !recv_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/false)) {
    RTC_LOG(LS_ERROR) << "Updating recv session cryptex failed";
    return false;
  }

  RTC_LOG(LS_INFO) << "SRTP " << (new_sessions ? "activated" : "updated")
                   << " with negotiated parameters:" << " send crypto_suite "
                   << send_crypto_suite << " recv crypto_suite "
                   << recv_crypto_suite << " cryptex " << use_cryptex_ << "/"
                   << (cryptex_policy_ ==
                       CryptoOptions::Srtp::CryptexPolicy::kRequire);
  MaybeUpdateWritableState();
  return true;
}

bool SrtpTransport::SetRtcpParams(
    int send_crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& send_key,
    const std::vector<RtpHeaderExtensionId>& send_extension_ids,
    int recv_crypto_suite,
    const ZeroOnFreeBuffer<uint8_t>& recv_key,
    const std::vector<RtpHeaderExtensionId>& recv_extension_ids) {
  // This can only be called once, but can be safely called after
  // SetRtpParams
  if (send_rtcp_session_ || recv_rtcp_session_) {
    RTC_LOG(LS_ERROR) << "Tried to set SRTCP Params when filter already active";
    return false;
  }

  send_rtcp_session_.reset(new SrtpSession(field_trials_));
  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !send_rtcp_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/true)) {
    return false;
  }
  if (!send_rtcp_session_->SetSend(send_crypto_suite, send_key,
                                   send_extension_ids)) {
    return false;
  }

  recv_rtcp_session_.reset(new SrtpSession(field_trials_));
  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !recv_rtcp_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/false)) {
    return false;
  }
  if (!recv_rtcp_session_->SetReceive(recv_crypto_suite, recv_key,
                                      recv_extension_ids)) {
    return false;
  }
  if (cryptex_policy_ != CryptoOptions::Srtp::CryptexPolicy::kDisabled &&
      !recv_rtcp_session_->UseCryptex(
          use_cryptex_,
          cryptex_policy_ == CryptoOptions::Srtp::CryptexPolicy::kRequire,
          /*sending_session=*/false)) {
    return false;
  }

  RTC_LOG(LS_INFO) << "SRTCP activated with negotiated parameters:"
                   << " send crypto_suite " << send_crypto_suite
                   << " recv crypto_suite " << recv_crypto_suite << " cryptex "
                   << use_cryptex_ << "/"
                   << (cryptex_policy_ ==
                       CryptoOptions::Srtp::CryptexPolicy::kRequire);
  MaybeUpdateWritableState();
  return true;
}

bool SrtpTransport::IsSrtpActive() const {
  return send_session_ && recv_session_;
}

bool SrtpTransport::IsWritable(bool rtcp) const {
  return IsSrtpActive() && RtpTransport::IsWritable(rtcp);
}

void SrtpTransport::ResetParams() {
  send_session_ = nullptr;
  recv_session_ = nullptr;
  send_rtcp_session_ = nullptr;
  recv_rtcp_session_ = nullptr;
  MaybeUpdateWritableState();
  RTC_LOG(LS_INFO) << "The params in SRTP transport are reset.";
}

void SrtpTransport::CreateSrtpSessions() {
  send_session_.reset(new SrtpSession(field_trials_));
  recv_session_.reset(new SrtpSession(field_trials_));
}

bool SrtpTransport::ProtectRtp(CopyOnWriteBuffer& buffer) {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to ProtectRtp: SRTP not active";
    return false;
  }
  RTC_CHECK(send_session_);
  return send_session_->ProtectRtp(buffer);
}

bool SrtpTransport::ProtectRtp(CopyOnWriteBuffer& buffer, int64_t* index) {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to ProtectRtp: SRTP not active";
    return false;
  }
  RTC_CHECK(send_session_);
  return send_session_->ProtectRtp(buffer, index);
}

bool SrtpTransport::ProtectRtcp(CopyOnWriteBuffer& buffer) {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to ProtectRtcp: SRTP not active";
    return false;
  }
  if (send_rtcp_session_) {
    return send_rtcp_session_->ProtectRtcp(buffer);
  } else {
    RTC_CHECK(send_session_);
    return send_session_->ProtectRtcp(buffer);
  }
}

bool SrtpTransport::UnprotectRtp(CopyOnWriteBuffer& buffer) {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to UnprotectRtp: SRTP not active";
    return false;
  }
  RTC_CHECK(recv_session_);
  return recv_session_->UnprotectRtp(buffer);
}

bool SrtpTransport::UnprotectRtcp(CopyOnWriteBuffer& buffer) {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to UnprotectRtcp: SRTP not active";
    return false;
  }
  if (recv_rtcp_session_) {
    return recv_rtcp_session_->UnprotectRtcp(buffer);
  } else {
    RTC_CHECK(recv_session_);
    return recv_session_->UnprotectRtcp(buffer);
  }
}

bool SrtpTransport::GetSrtpOverhead(int* srtp_overhead) const {
  if (!IsSrtpActive()) {
    RTC_LOG(LS_WARNING) << "Failed to GetSrtpOverhead: SRTP not active";
    return false;
  }

  RTC_CHECK(send_session_);
  *srtp_overhead = send_session_->GetSrtpOverhead();
  return true;
}

void SrtpTransport::MaybeUpdateWritableState() {
  bool writable = IsWritable(/*rtcp=*/true) && IsWritable(/*rtcp=*/false);
  // Only fire the signal if the writable state changes.
  if (writable_ != writable) {
    writable_ = writable;
    SendWritableState(writable_);
  }
  MaybeUpdateSrtpOverhead();
}

void SrtpTransport::MaybeUpdateSrtpOverhead() {
  if (!update_network_route_on_srtp_activation_ || !rtp_packet_transport()) {
    return;
  }
  int srtp_overhead = 0;
  if (IsSrtpActive()) {
    GetSrtpOverhead(&srtp_overhead);
  }
  if (srtp_overhead != srtp_overhead_) {
    OnNetworkRouteChanged(rtp_packet_transport()->network_route());
  }
}

bool SrtpTransport::UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) {
  if (recv_session_ &&
      field_trials_.IsEnabled("WebRTC-SrtpRemoveReceiveStream")) {
    // Remove the SSRCs explicitly registered with the demuxer
    // (via SDP negotiation) from the SRTP session.
    for (const auto ssrc : GetSsrcsForSink(sink)) {
      if (!recv_session_->RemoveSsrcFromSession(ssrc)) {
        RTC_LOG(LS_WARNING)
            << "Could not remove SSRC " << ssrc << " from SRTP session.";
      }
    }
  }
  return RtpTransport::UnregisterRtpDemuxerSink(sink);
}

}  // namespace webrtc
