/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_SRTP_TRANSPORT_H_
#define PC_SRTP_TRANSPORT_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/crypto/crypto_options.h"
#include "api/field_trials_view.h"
#include "api/rtp_header_extension_id.h"
#include "call/rtp_demuxer.h"
#include "p2p/base/packet_transport_internal.h"
#include "pc/rtp_transport.h"
#include "pc/srtp_session.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network_route.h"

namespace webrtc {

// This subclass of the RtpTransport is used for SRTP which is reponsible for
// protecting/unprotecting the packets. It provides interfaces to set the crypto
// parameters for the SrtpSession underneath.
class SrtpTransport : public RtpTransport {
 public:
  SrtpTransport(bool rtcp_mux_enabled, const FieldTrialsView& field_trials);

  ~SrtpTransport() override = default;

  bool SendRtpPacket(CopyOnWriteBuffer* packet,
                     const AsyncSocketPacketOptions& options,
                     int flags) override;

  bool SendRtcpPacket(CopyOnWriteBuffer* packet,
                      const AsyncSocketPacketOptions& options,
                      int flags) override;

  // The transport becomes active if the send_session_ and recv_session_ are
  // created.
  bool IsSrtpActive() const override;

  bool IsWritable(bool rtcp) const override;

  // Configures cryptex from the local policy and the negotiation result.
  bool UseCryptex(CryptoOptions::Srtp::CryptexPolicy policy, bool negotiated);

  // Create new send/recv sessions and set the negotiated crypto keys for RTP
  // packet encryption. The keys can either come from SDES negotiation or DTLS
  // handshake.
  bool SetRtpParams(
      int send_crypto_suite,
      const ZeroOnFreeBuffer<uint8_t>& send_key,
      const std::vector<RtpHeaderExtensionId>& send_extension_ids,
      int recv_crypto_suite,
      const ZeroOnFreeBuffer<uint8_t>& recv_key,
      const std::vector<RtpHeaderExtensionId>& recv_extension_ids);

  // Create new send/recv sessions and set the negotiated crypto keys for RTCP
  // packet encryption. The keys can either come from SDES negotiation or DTLS
  // handshake.
  bool SetRtcpParams(
      int send_crypto_suite,
      const ZeroOnFreeBuffer<uint8_t>& send_key,
      const std::vector<RtpHeaderExtensionId>& send_extension_ids,
      int recv_crypto_suite,
      const ZeroOnFreeBuffer<uint8_t>& recv_key,
      const std::vector<RtpHeaderExtensionId>& recv_extension_ids);

  void ResetParams();

  // Returns srtp overhead for rtp packets.
  bool GetSrtpOverhead(int* srtp_overhead) const;

  // In addition to unregistering the sink, the SRTP transport
  // disassociates all SSRCs of the sink from libSRTP.
  bool UnregisterRtpDemuxerSink(RtpPacketSinkInterface* sink) override;

 protected:
  // If the writable state changed, fire the SignalWritableState.
  void MaybeUpdateWritableState();
  void MaybeUpdateSrtpOverhead();

 private:
  void ConnectToRtpTransport();
  void CreateSrtpSessions();

  void OnRtpPacketReceived(const ReceivedIpPacket& packet) override;
  void OnRtcpPacketReceived(const ReceivedIpPacket& packet) override;
  void OnNetworkRouteChanged(
      std::optional<NetworkRoute> network_route) override;

  // Override the RtpTransport::OnWritableState.
  void OnWritableState(PacketTransportInternal* packet_transport) override;

  bool ProtectRtp(CopyOnWriteBuffer& buffer);
  // Overloaded version, outputs packet index.
  bool ProtectRtp(CopyOnWriteBuffer& buffer, int64_t* index);
  bool ProtectRtcp(CopyOnWriteBuffer& buffer);

  // Decrypts/verifies an invidiual RTP/RTCP packet.
  // If an HMAC is used, this will decrease the packet size.
  bool UnprotectRtp(CopyOnWriteBuffer& buffer);
  bool UnprotectRtcp(CopyOnWriteBuffer& buffer);

  const std::string content_name_;

  std::unique_ptr<SrtpSession> send_session_;
  std::unique_ptr<SrtpSession> recv_session_;
  // Non-muxed RTCP requires different SRTP sessions as it leads to
  // separate DTLS handshakes.
  std::unique_ptr<SrtpSession> send_rtcp_session_;
  std::unique_ptr<SrtpSession> recv_rtcp_session_;

  std::optional<int> send_crypto_suite_;
  std::optional<int> recv_crypto_suite_;
  ZeroOnFreeBuffer<uint8_t> send_key_;
  ZeroOnFreeBuffer<uint8_t> recv_key_;

  bool writable_ = false;
  std::optional<int> srtp_overhead_;

  int decryption_failure_count_ = 0;
  int rtcp_decryption_failure_count_ = 0;

  CryptoOptions::Srtp::CryptexPolicy cryptex_policy_ =
      CryptoOptions::Srtp::CryptexPolicy::kDisabled;
  bool use_cryptex_ = false;

  const FieldTrialsView& field_trials_;
};

}  // namespace webrtc

#endif  // PC_SRTP_TRANSPORT_H_
