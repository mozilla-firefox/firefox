/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef CALL_RTP_STREAM_RECEIVER_CONTROLLER_H_
#define CALL_RTP_STREAM_RECEIVER_CONTROLLER_H_

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "api/task_queue/task_queue_base.h"
#include "call/rtp_demuxer.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "modules/rtp_rtcp/include/recovered_packet_receiver.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class RtpPacketReceived;

// This class represents the RTP receive parsing and demuxing, for a
// single RTP session.
// TODO(bugs.webrtc.org/7135): Add RTCP processing, we should aim to terminate
// RTCP and not leave any RTCP processing to individual receive streams.
class RtpStreamReceiverController : public RtpStreamReceiverControllerInterface,
                                    public RecoveredPacketReceiver {
 public:
  explicit RtpStreamReceiverController(
      TaskQueueBase* absl_nonnull network_thread);
  ~RtpStreamReceiverController() override;

  // Implements RtpStreamReceiverControllerInterface.
  std::unique_ptr<RtpStreamReceiverInterface> CreateReceiver(
      uint32_t ssrc,
      RtpPacketSinkInterface* sink) override;

  RtpPacketSinkInterface* ResolveSink(const RtpPacketReceived& packet);

  // TODO(bugs.webrtc.org/7135): Not yet responsible for parsing.
  bool OnRtpPacket(const RtpPacketReceived& packet);

  // Implements RecoveredPacketReceiver.
  // Responsible for demuxing recovered FLEXFEC packets. Must be called on the
  // network thread.
  void OnRecoveredPacket(const RtpPacketReceived& packet) override;

  // Removes all sinks.
  void DisconnectFromNetworkThread();

  bool IsEmpty() const;

 private:
  class Receiver : public RtpStreamReceiverInterface {
   public:
    Receiver(RtpStreamReceiverController* controller,
             uint32_t ssrc,
             RtpPacketSinkInterface* sink);

    ~Receiver() override;

   private:
    RtpStreamReceiverController* const controller_;
    RtpPacketSinkInterface* const sink_;
  };

  bool AddSink(uint32_t ssrc, RtpPacketSinkInterface* sink);
  bool RemoveSink(const RtpPacketSinkInterface* sink);

  TaskQueueBase* const network_thread_;
  RtpDemuxer demuxer_ RTC_GUARDED_BY(network_thread_){false /*use_mid*/};
};

}  // namespace webrtc

#endif  // CALL_RTP_STREAM_RECEIVER_CONTROLLER_H_
