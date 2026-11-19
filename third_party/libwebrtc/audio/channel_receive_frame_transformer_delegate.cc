/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/channel_receive_frame_transformer_delegate.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {

class TransformableIncomingAudioFrame
    : public TransformableAudioFrameInterface {
 public:
  TransformableIncomingAudioFrame(std::span<const uint8_t> payload,
                                  const RTPHeader& header,
                                  uint32_t ssrc,
                                  const std::string& codec_mime_type,
                                  Timestamp receive_time)
      : TransformableAudioFrameInterface(Passkey()),
        payload_(payload.data(), payload.size()),
        header_(header),
        ssrc_(ssrc),
        codec_mime_type_(codec_mime_type),
        receive_time_(receive_time) {}
  ~TransformableIncomingAudioFrame() override = default;
  std::span<const uint8_t> GetData() const override { return payload_; }

  void SetData(std::span<const uint8_t> data) override {
    payload_.SetData(data.data(), data.size());
  }

  void SetRTPTimestamp(uint32_t timestamp) override {
    header_.timestamp = timestamp;
  }

  uint8_t GetPayloadType() const override { return header_.payloadType; }
  uint32_t GetSsrc() const override { return ssrc_; }
  uint32_t GetTimestamp() const override { return header_.timestamp; }
  RtpTimestampInfo GetRtpTimestampInfo() const override {
    return RtpTimestampWithOffset{header_.timestamp};
  }
  std::span<const uint32_t> GetContributingSources() const override {
    return std::span<const uint32_t>(header_.arrOfCSRCs, header_.numCSRCs);
  }
  Direction GetDirection() const override { return Direction::kReceiver; }

  std::string GetMimeType() const override { return codec_mime_type_; }
  const std::optional<uint16_t> SequenceNumber() const override {
    return header_.sequenceNumber;
  }

  std::optional<uint64_t> AbsoluteCaptureTimestamp() const override {
    // This could be extracted from received header extensions + extrapolation,
    // if required in future, eg for being able to re-send received frames.
    return std::nullopt;
  }
  const RTPHeader& Header() const { return header_; }

  FrameType Type() const override {
    if (!header_.extension.audio_level()) {
      // Audio level extension not set.
      return FrameType::kAudioFrameCN;
    }
    return header_.extension.audio_level()->voice_activity()
               ? FrameType::kAudioFrameSpeech
               : FrameType::kAudioFrameCN;
  }

  std::optional<uint8_t> AudioLevel() const override {
    if (header_.extension.audio_level()) {
      return header_.extension.audio_level()->level();
    }
    return std::nullopt;
  }

  bool CanSetAudioLevel() const override { return true; }

  void SetAudioLevel(std::optional<uint8_t> audio_level_dbov) override {
    header_.extension.set_audio_level(
        audio_level_dbov.has_value()
            ? std::make_optional(webrtc::AudioLevel(
                  /*voice_activity=*/true,
                  std::min(*audio_level_dbov, static_cast<uint8_t>(127u))))
            : std::nullopt);
  }

  std::optional<Timestamp> ReceiveTime() const override {
    return receive_time_ == Timestamp::MinusInfinity()
               ? std::nullopt
               : std::optional<Timestamp>(receive_time_);
  }

  std::optional<Timestamp> CaptureTime() const override {
    if (header_.extension.absolute_capture_time) {
      return Timestamp::Micros(UQ32x32ToInt64Us(
          header_.extension.absolute_capture_time->absolute_capture_timestamp));
    }
    return std::nullopt;
  }

  std::optional<TimeDelta> SenderCaptureTimeOffset() const override {
    if (header_.extension.absolute_capture_time &&
        header_.extension.absolute_capture_time
            ->estimated_capture_clock_offset) {
      return TimeDelta::Micros(
          Q32x32ToInt64Us(*header_.extension.absolute_capture_time
                               ->estimated_capture_clock_offset));
    }
    return std::nullopt;
  }

 private:
  Buffer payload_;
  RTPHeader header_;
  uint32_t ssrc_;
  std::string codec_mime_type_;
  Timestamp receive_time_;
};

ChannelReceiveFrameTransformerDelegate::ChannelReceiveFrameTransformerDelegate(
    ReceiveFrameCallback receive_frame_callback,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    TaskQueueBase* absl_nonnull channel_receive_thread)
    : receive_frame_callback_(std::move(receive_frame_callback)),
      frame_transformer_(std::move(frame_transformer)),
      channel_receive_thread_(channel_receive_thread) {
  RTC_DCHECK(receive_frame_callback_);
}

void ChannelReceiveFrameTransformerDelegate::Init() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  frame_transformer_->RegisterTransformedFrameCallback(
      scoped_refptr<TransformedFrameCallback>(this));
}

void ChannelReceiveFrameTransformerDelegate::Reset() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  frame_transformer_->UnregisterTransformedFrameCallback();
  frame_transformer_ = nullptr;
  receive_frame_callback_ = ReceiveFrameCallback();
}

void ChannelReceiveFrameTransformerDelegate::Transform(
    std::span<const uint8_t> packet,
    const RTPHeader& header,
    uint32_t ssrc,
    const std::string& codec_mime_type,
    Timestamp receive_time) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (short_circuit_) {
    if (receive_frame_callback_) {
      receive_frame_callback_(packet, header, receive_time);
    }
  } else {
    frame_transformer_->Transform(
        std::make_unique<TransformableIncomingAudioFrame>(
            packet, header, ssrc, codec_mime_type, receive_time));
  }
}

void ChannelReceiveFrameTransformerDelegate::OnTransformedFrame(
    std::unique_ptr<TransformableFrameInterface> frame) {
  scoped_refptr<ChannelReceiveFrameTransformerDelegate> delegate(this);
  channel_receive_thread_->PostTask(
      [delegate = std::move(delegate), frame = std::move(frame)]() mutable {
        delegate->ReceiveFrame(std::move(frame));
      });
}

void ChannelReceiveFrameTransformerDelegate::StartShortCircuiting() {
  SetShortCircuiting(/*short_circuit=*/true);
}

void ChannelReceiveFrameTransformerDelegate::StopShortCircuiting() {
  SetShortCircuiting(/*short_circuit=*/false);
}

void ChannelReceiveFrameTransformerDelegate::SetShortCircuiting(
    bool short_circuit) {
  scoped_refptr<ChannelReceiveFrameTransformerDelegate> delegate(this);
  channel_receive_thread_->PostTask(
      [delegate = std::move(delegate), short_circuit]() {
        RTC_DCHECK_RUN_ON(&delegate->sequence_checker_);
        delegate->short_circuit_ = short_circuit;
      });
}

void ChannelReceiveFrameTransformerDelegate::ReceiveFrame(
    std::unique_ptr<TransformableFrameInterface> frame) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (!receive_frame_callback_)
    return;

  auto* transformed_frame =
      static_cast<TransformableAudioFrameInterface*>(frame.get());
  Timestamp receive_time =
      transformed_frame->ReceiveTime().value_or(Timestamp::MinusInfinity());
  RTPHeader header;
  if (frame->GetDirection() ==
      TransformableFrameInterface::Direction::kSender) {
    header.payloadType = transformed_frame->GetPayloadType();
    RTC_CHECK(std::holds_alternative<RtpTimestampWithOffset>(
        transformed_frame->GetRtpTimestampInfo()));
    header.timestamp = std::get<RtpTimestampWithOffset>(
        transformed_frame->GetRtpTimestampInfo());
    header.ssrc = transformed_frame->GetSsrc();
    if (transformed_frame->AbsoluteCaptureTimestamp().has_value()) {
      header.extension.absolute_capture_time = AbsoluteCaptureTime();
      header.extension.absolute_capture_time->absolute_capture_timestamp =
          transformed_frame->AbsoluteCaptureTimestamp().value();
    }
    if (transformed_frame->AudioLevel().has_value()) {
      // TODO(crbug.com/webrtc/419746427): Add support for voice activity in
      // TransformableAudioFrameInterface.
      header.extension.set_audio_level(AudioLevel(
          /*voice_activity=*/true, *transformed_frame->AudioLevel()));
    }
  } else {
    auto* transformed_incoming_frame =
        static_cast<TransformableIncomingAudioFrame*>(frame.get());
    header = transformed_incoming_frame->Header();
  }

  // TODO(crbug.com/1464860): Take an explicit struct with the required
  // information rather than the RTPHeader to make it easier to
  // construct the required information when injecting transformed frames not
  // originally from this receiver.
  receive_frame_callback_(frame->GetData(), header, receive_time);
}

scoped_refptr<FrameTransformerInterface>
ChannelReceiveFrameTransformerDelegate::FrameTransformer() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return frame_transformer_;
}

std::unique_ptr<TransformableAudioFrameInterface> CloneReceiverAudioFrame(
    TransformableAudioFrameInterface* original) {
  RTC_CHECK(original->GetDirection() ==
            TransformableFrameInterface::Direction::kReceiver);

  auto* original_incoming_frame =
      static_cast<TransformableIncomingAudioFrame*>(original);
  return std::make_unique<TransformableIncomingAudioFrame>(
      original->GetData(), original_incoming_frame->Header(),
      original->GetSsrc(), original->GetMimeType(),
      original->ReceiveTime().value_or(Timestamp::MinusInfinity()));
}
}  // namespace webrtc
