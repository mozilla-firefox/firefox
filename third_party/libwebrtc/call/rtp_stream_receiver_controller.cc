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
#include <utility>

#include "absl/base/nullability.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

RtpStreamReceiverController::Receiver::Receiver(
    RtpStreamReceiverController* controller,
    uint32_t ssrc,
    std::unique_ptr<ProxySink> proxy_sink)
    : controller_(controller), proxy_sink_(std::move(proxy_sink)) {
  controller_->sink_validator_->OnSinkAdded(proxy_sink_.get());
  auto add_sink =
      [controller = controller_, ssrc, proxy = proxy_sink_.get()] {
        RTC_DCHECK_RUN_ON(controller->network_thread_);
        if (!controller->AddSink(ssrc, proxy)) {
          RTC_LOG(LS_ERROR)
              << "RtpStreamReceiverController::Receiver::Receiver: Sink "
                 "could not be added for SSRC="
              << ssrc << ".";
        }
      };
  if (controller_->network_thread_->IsCurrent()) {
    add_sink();
  } else {
    controller_->network_thread_->PostTask(
        SafeTask(controller_->network_safety_, std::move(add_sink)));
  }
}

RtpStreamReceiverController::Receiver::~Receiver() {
  controller_->sink_validator_->OnSinkRemoved(proxy_sink_.get());
  auto remove_sink = [controller = controller_,
                      proxy = std::move(proxy_sink_)] {
    RTC_DCHECK_RUN_ON(controller->network_thread_);
    controller->RemoveSink(proxy.get());
  };
  if (controller_->network_thread_->IsCurrent()) {
    remove_sink();
  } else {
    controller_->network_thread_->PostTask(
        SafeTask(controller_->network_safety_, std::move(remove_sink)));
  }
}

RtpStreamReceiverController::RtpStreamReceiverController(
    TaskQueueBase* absl_nonnull network_thread,
    RtpSinkValidator* absl_nonnull sink_validator)
    : network_thread_(network_thread),
      sink_validator_(sink_validator) {
  RTC_DCHECK(network_thread_);
  RTC_DCHECK(sink_validator_);
}

RtpStreamReceiverController::~RtpStreamReceiverController() = default;

std::unique_ptr<RtpStreamReceiverInterface>
RtpStreamReceiverController::CreateReceiver(uint32_t ssrc,
                                            RtpPacketSinkInterface* sink) {
  return std::make_unique<Receiver>(this, ssrc,
                                    std::make_unique<ProxySink>(sink));
}

void RtpStreamReceiverController::DisconnectFromNetworkThread() {
  RTC_DCHECK_RUN_ON(network_thread_);
  demuxer_.RemoveAllSinks();
  network_safety_->SetNotAlive();
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
  if (!network_thread_->IsCurrent()) {
    network_thread_->PostTask(SafeTask(
        network_safety_, [this, packet] { OnRecoveredPacket(packet); }));
    return;
  }

  RTC_DCHECK_RUN_ON(network_thread_);
  RtpPacketSinkInterface* proxy_sink = demuxer_.ResolveSink(packet);
  if (proxy_sink != nullptr && sink_validator_->IsValidSink(proxy_sink)) {
    proxy_sink->OnRtpPacket(packet);
  }
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
