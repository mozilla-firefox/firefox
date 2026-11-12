/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtp_receiver.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/frame_transformer_interface.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/sframe/sframe_types.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/units/timestamp.h"
#include "media/base/media_channel.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "pc/media_stream.h"
#include "pc/media_stream_proxy.h"
#include "rtc_base/checks.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

// This function is only expected to be called on the signalling thread.
// On the other hand, some test or even production setups may use
// several signaling threads.
int RtpReceiverInternal::GenerateUniqueId() {
  static std::atomic<int> g_unique_id{0};

  return ++g_unique_id;
}

std::vector<scoped_refptr<MediaStreamInterface>>
RtpReceiverInternal::CreateStreamsFromIds(
    std::span<const std::string> stream_ids) {
  std::vector<scoped_refptr<MediaStreamInterface>> streams;
  streams.reserve(stream_ids.size());
  for (const std::string& stream_id : stream_ids) {
    streams.push_back(MediaStreamProxy::Create(Thread::Current(),
                                               MediaStream::Create(stream_id)));
  }
  return streams;
}

RtpReceiverBase::RtpReceiverBase(
    Thread* worker_thread,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    Clock* clock)
    : worker_thread_(worker_thread),
      source_tracker_(clock),
      enable_sframe_at_owner_(std::move(enable_sframe_at_owner)) {}

std::optional<uint32_t> RtpReceiverBase::ssrc() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!signaled_ssrc_.has_value() && media_channel()) {
    return media_channel()->GetUnsignaledSsrc();
  }
  return signaled_ssrc_;
}

std::optional<uint32_t> RtpReceiverBase::ssrc_s() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  if (!signaled_ssrc_s_.has_value()) {
    return unsignaled_ssrc_s_;
  }
  return signaled_ssrc_s_;
}

bool RtpReceiverBase::has_signaled_ssrc_s() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return signaled_ssrc_s_.has_value();
}

void RtpReceiverBase::SetUnsignaledSsrc_s(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  RTC_DCHECK(!signaled_ssrc_s_.has_value());
  unsignaled_ssrc_s_ = ssrc;
}

void RtpReceiverBase::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  frame_decryptor_ = std::move(frame_decryptor);
  // Special Case: Set the frame decryptor to any value on any existing channel.
  if (media_channel() && signaled_ssrc_) {
    media_channel()->SetFrameDecryptor(*signaled_ssrc_, frame_decryptor_);
  }
}

scoped_refptr<FrameDecryptorInterface> RtpReceiverBase::GetFrameDecryptor()
    const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  return frame_decryptor_;
}

void RtpReceiverBase::SetFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  frame_transformer_ = std::move(frame_transformer);
  if (media_channel()) {
    media_channel()->SetDepacketizerToDecoderFrameTransformer(
        signaled_ssrc_.value_or(0), frame_transformer_);
  }
}

std::vector<RtpSource> RtpReceiverBase::GetSources() const {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  return source_tracker_.GetSources();
}

void RtpReceiverBase::SetObserver(RtpReceiverObserverInterface* observer) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  if (observer_ == observer) {
    return;
  }
  observer_ = observer;
  if (observer_ != nullptr) {
    // Deliver initial notifications synchronously for sources and packets
    // already received prior to observer registration, matching the synchronous
    // delivery pattern of OnFirstPacketReceived. Dispatch to the local
    // `observer` argument so that if a callback reentrantly invokes
    // SetObserver(), `observer_` is safely updated without corrupting or
    // duplicating calls on the active invocation target.
    if (received_first_packet_) {
      observer->OnFirstPacketReceived(media_type());
    }
    if (observer_ != observer) {
      return;  // Reentrantly replaced or removed.
    }
    if (source_tracker_.has_delivered_frame()) {
      observer->OnSourceChanged(/*ssrc_changed=*/true,
                                source_tracker_.last_frame_has_csrcs());
    }
  }
}

void RtpReceiverBase::NotifyFirstPacketReceived(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  // Set received_first_packet_ before notifying observer so that if the
  // callback synchronously calls SetObserver(), the new observer receives the
  // notification.
  received_first_packet_ = true;
  if (observer_ != nullptr) {
    observer_->OnFirstPacketReceived(media_type());
  }
}

void RtpReceiverBase::NotifyFirstPacketReceivedAfterReceptiveChange(
    uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  if (observer_ != nullptr) {
    observer_->OnFirstPacketReceivedAfterReceptiveChange(media_type());
  }
}

void RtpReceiverBase::OnFrameDelivered(const RtpPacketInfos& infos,
                                       Timestamp delivery_time) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);
  SourceTracker::SourceChanged changed =
      source_tracker_.OnFrameDelivered(infos, delivery_time);
  if ((changed.ssrc_changed || changed.csrc_changed) && observer_ != nullptr) {
    observer_->OnSourceChanged(changed.ssrc_changed, changed.csrc_changed);
  }
}

RTCErrorOr<scoped_refptr<SframeDecryptorInterface>>
RtpReceiverBase::CreateSframeDecryptorOrError(
    SframeCipherSuite cipher_suite,
    SframeDecryptErrorCallback error_callback) {
  RTC_DCHECK_RUN_ON(&signaling_thread_checker_);

  if (!enable_sframe_at_owner_) {
    return RTCError(RTCErrorType::INTERNAL_ERROR,
                    "Receiver is not associated with a transceiver");
  }

  RTCError error = enable_sframe_at_owner_();
  if (!error.ok()) {
    return error;
  }

  // TODO(bugs.webrtc.org/479862368): Create the internal Sframe decryption
  // pipeline and return a key management handle.
  return RTCError(RTCErrorType::UNSUPPORTED_OPERATION,
                  "Sframe decrypter not yet implemented");
}

}  // namespace webrtc
