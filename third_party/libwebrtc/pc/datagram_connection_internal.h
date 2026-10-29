/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef PC_DATAGRAM_CONNECTION_INTERNAL_H_
#define PC_DATAGRAM_CONNECTION_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/datagram_connection.h"
#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "call/rtp_packet_sink_interface.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/dtls_transport.h"
#include "pc/packet_processor.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/system/rtc_export.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class RTC_EXPORT DatagramConnectionInternal : public DatagramConnection,
                                              public RtpPacketSinkInterface {
 public:
  DatagramConnectionInternal(const Environment& env,
                             std::unique_ptr<PortAllocator> port_allocator,
                             absl::string_view transport_name,
                             bool ice_controlling,
                             scoped_refptr<RTCCertificate> certificate,
                             std::unique_ptr<Observer> observer,
                             WireProtocol wire_protocol,
                             std::unique_ptr<IceTransportInternal>
                                 custom_ice_transport_internal = nullptr);
  ~DatagramConnectionInternal() override;

  void SetRemoteIceParameters(const IceParameters& ice_parameters) override;
  void AddRemoteCandidate(const Candidate& candidate) override;
  bool Writable() override;
  void SetRemoteDtlsParameters(absl::string_view digestAlgorithm,
                               const uint8_t* digest,
                               size_t digest_len,
                               SSLRole ssl_role) override;
  void SendPackets(std::span<PacketSendParameters> packets) override;

  void Terminate(
      absl::AnyInvocable<void()> terminate_complete_callback) override;

  void OnCandidateGathered(IceTransportInternal* ice_transport,
                           const Candidate& candidate);

  void OnWritableStatePossiblyChanged();
  void OnTransportWritableStateChanged(PacketTransportInternal*);
  void OnConnectionError();

  // RtpPacketSinkInterface
  void OnRtpPacket(const RtpPacketReceived& packet) override;

  void OnDtlsPacket(CopyOnWriteBuffer packet,
                    Timestamp receive_time,
                    EcnMarking ecn);

  void OnSentPacket(const SentPacketInfo& packet);

  absl::string_view IceUsernameFragment() override {
    return ice_username_fragment_;
  }

  absl::string_view IcePassword() override { return ice_password_; }

#if RTC_DCHECK_IS_ON
  DtlsSrtpTransport* GetDtlsSrtpTransportForTesting() {
    return dtls_srtp_transport_.get();
  }
#endif

 private:
  void DispatchSendOutcome(PacketId id, Observer::SendOutcome::Status status);
  void SendSinglePacket(const PacketSendParameters& packet,
                        bool last_packet_in_batch);

  enum class State { kActive, kTerminated };
  State current_state_ = State::kActive;

  const Environment env_;
  const WireProtocol wire_protocol_;

  // Must be before Transport types, to ensure it outlives them.
  const std::unique_ptr<Observer> observer_;

  // Note the destruction order of these transport objects must be preserved.
  const std::unique_ptr<PortAllocator> port_allocator_;
  const std::unique_ptr<IceTransportInternal> transport_channel_;
  const std::unique_ptr<DtlsTransportInternal> internal_transport_;
  const scoped_refptr<DtlsTransport> dtls_transport_;
  const std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport_;

  const SequenceChecker sequence_checker_;
  bool last_writable_state_ RTC_GUARDED_BY(sequence_checker_) = false;

  const std::string ice_username_fragment_;
  const std::string ice_password_;

  std::unique_ptr<PacketProcessor> packet_processor_;
};

}  // namespace webrtc
#endif  // PC_DATAGRAM_CONNECTION_INTERNAL_H_
