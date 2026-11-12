/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef API_DATAGRAM_CONNECTION_H_
#define API_DATAGRAM_CONNECTION_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/ref_count.h"
#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "p2p/base/transport_description.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

// Experimental class to support prototyping of a packet-level web API
// "RtcTransport" being discussed in the w3c working group.
// Subject to dramatic change without notice.
//
// All interactions should be on the same thread which is also used for
// networking internals.
class RTC_EXPORT DatagramConnection : public RefCountInterface {
 public:
  enum class WireProtocol {
    kDtls,
    kDtlsSrtp,
    // DTLS-only transport that additionally frames every payload with a small
    // transport header (packet type + transport sequence number). This is the
    // wire-format foundation for carrying congestion-control feedback inside
    // the DTLS records; the feedback codec itself is not part of this mode yet.
    // See DtlsPacketProcessor.
    kDtlsWithFeedback,
  };

  using PacketId = uint32_t;

  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void OnCandidateGathered(const Candidate& candidate) = 0;

    struct PacketMetadata {
      Timestamp receive_time;
      // ECN marking of the IP packet as received from the network.
      EcnMarking ecn = EcnMarking::kNotEct;
    };
    virtual void OnPacketReceived(std::span<const uint8_t> data,
                                  PacketMetadata metadata) = 0;

    // Notification of outcome of an earlier call to SendPacket.
    struct SendOutcome {
      PacketId id;

      enum class Status {
        kSuccess,
        kNotSent,
      };
      Status status;
      // Time sent on network.
      Timestamp send_time = Timestamp::MinusInfinity();
      // Actual UDP payload bytes sent on the network.
      size_t bytes_sent = 0;
    };
    virtual void OnSendOutcome(SendOutcome send_outcome) {}

    // TODO(crbug.com/443019066): Migrate to OnSendOutcome.
    virtual void OnSendError() {}

    // Notification of an error unrelated to sending. Observers should
    // check the current state of the connection.
    virtual void OnConnectionError() = 0;

    virtual void OnWritableChange() = 0;
  };

  ~DatagramConnection() override = default;

  virtual void SetRemoteIceParameters(const IceParameters& ice_parameters) = 0;
  virtual void AddRemoteCandidate(const Candidate& candidate) = 0;

  // Whether SendPacket calls should be expected to succeed.
  // See also Observer::OnWritableChange().
  virtual bool Writable() = 0;

  enum class SSLRole { kClient, kServer };
  virtual void SetRemoteDtlsParameters(absl::string_view digestAlgorithm,
                                       const uint8_t* digest,
                                       size_t digest_len,
                                       SSLRole ssl_role) = 0;
  struct PacketSendParameters {
    // Used to tie to async feedback of the sending outcome. No deduping is
    // performed, the caller is responsible for ensuring uniqueness and handing
    // rollovers.
    PacketId id = 0;
    std::span<const uint8_t> payload;
  };

  // Send a batch of packets on this connection. Listen to
  // Observer::OnSendOutcome for notification of whether each was sent
  // successfully.
  virtual void SendPackets(std::span<PacketSendParameters> packets) = 0;

  // Initiate closing connection and releasing resources. Must be called before
  // destruction.
  virtual void Terminate(
      absl::AnyInvocable<void()> terminate_complete_callback) = 0;

  virtual std::string_view IceUsernameFragment() = 0;
  virtual std::string_view IcePassword() = 0;
};

}  // namespace webrtc
#endif  // API_DATAGRAM_CONNECTION_H_
