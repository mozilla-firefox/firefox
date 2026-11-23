/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_stream_receiver_controller.h"

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

RtpStreamReceiverController::Receiver::Receiver(
    RtpStreamReceiverController* controller,
    uint32_t ssrc,
    RtpPacketSinkInterface* sink)
    : controller_(controller), sink_(sink) {
  RTC_DCHECK_RUN_ON(controller_->network_thread_);
  if (!controller_->AddSink(ssrc, sink_)) {
    RTC_LOG(LS_ERROR)
        << "RtpStreamReceiverController::Receiver::Receiver: Sink "
           "could not be added for SSRC="
        << ssrc << ".";
  }
}

RtpStreamReceiverController::Receiver::~Receiver() {
  RTC_DCHECK_RUN_ON(controller_->network_thread_);
  controller_->RemoveSink(sink_);
}

RtpStreamReceiverController::RtpStreamReceiverController(
    TaskQueueBase* absl_nonnull network_thread)
    : network_thread_(network_thread) {
  RTC_DCHECK(network_thread_);
}

RtpStreamReceiverController::~RtpStreamReceiverController() = default;

std::unique_ptr<RtpStreamReceiverInterface>
RtpStreamReceiverController::CreateReceiver(uint32_t ssrc,
                                            RtpPacketSinkInterface* sink) {
  return std::make_unique<Receiver>(this, ssrc, sink);
}

void RtpStreamReceiverController::DisconnectFromNetworkThread() {
  RTC_DCHECK_RUN_ON(network_thread_);
  demuxer_.RemoveAllSinks();
}

bool RtpStreamReceiverController::IsEmpty() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return demuxer_.IsEmpty();
}

RtpPacketSinkInterface* RtpStreamReceiverController::ResolveSink(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  return demuxer_.ResolveSink(packet);
}

bool RtpStreamReceiverController::OnRtpPacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  return demuxer_.OnRtpPacket(packet);
}

void RtpStreamReceiverController::OnRecoveredPacket(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  demuxer_.OnRtpPacket(packet);
}

bool RtpStreamReceiverController::AddSink(uint32_t ssrc,
                                          RtpPacketSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(network_thread_);
  return demuxer_.AddSink(ssrc, sink);
}

bool RtpStreamReceiverController::RemoveSink(
    const RtpPacketSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(network_thread_);
  return demuxer_.RemoveSink(sink);
}

}  // namespace webrtc
