/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/channel_send_frame_transformer_delegate.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "api/frame_transformer_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {
namespace {

using IfaceFrameType = TransformableAudioFrameInterface::FrameType;

IfaceFrameType InternalFrameTypeToInterfaceFrameType(
    const AudioFrameType frame_type) {
  switch (frame_type) {
    case AudioFrameType::kEmptyFrame:
      return IfaceFrameType::kEmptyFrame;
    case AudioFrameType::kAudioFrameSpeech:
      return IfaceFrameType::kAudioFrameSpeech;
    case AudioFrameType::kAudioFrameCN:
      return IfaceFrameType::kAudioFrameCN;
  }
  RTC_DCHECK_NOTREACHED();
  return IfaceFrameType::kEmptyFrame;
}

AudioFrameType InterfaceFrameTypeToInternalFrameType(
    const IfaceFrameType frame_type) {
  switch (frame_type) {
    case IfaceFrameType::kEmptyFrame:
      return AudioFrameType::kEmptyFrame;
    case IfaceFrameType::kAudioFrameSpeech:
      return AudioFrameType::kAudioFrameSpeech;
    case IfaceFrameType::kAudioFrameCN:
      return AudioFrameType::kAudioFrameCN;
  }
  RTC_DCHECK_NOTREACHED();
  return AudioFrameType::kEmptyFrame;
}
}  // namespace

class TransformableOutgoingAudioFrame
    : public TransformableAudioFrameInterface {
 public:
  TransformableOutgoingAudioFrame(
      AudioFrameType frame_type,
      uint8_t payload_type,
      uint32_t rtp_timestamp_with_offset,
      const uint8_t* payload_data,
      size_t payload_size,
      std::optional<uint64_t> absolute_capture_timestamp_ms,
      uint32_t ssrc,
      std::vector<uint32_t> csrcs,
      const std::string& codec_mime_type,
      std::optional<uint16_t> sequence_number,
      std::optional<uint8_t> audio_level_dbov)
      : TransformableOutgoingAudioFrame(
            frame_type,
            payload_type,
            RtpTimestampInfo(RtpTimestampWithOffset{rtp_timestamp_with_offset}),
            payload_data,
            payload_size,
            absolute_capture_timestamp_ms,
            ssrc,
            std::move(csrcs),
            codec_mime_type,
            sequence_number,
            audio_level_dbov) {}

  TransformableOutgoingAudioFrame(
      AudioFrameType frame_type,
      uint8_t payload_type,
      RtpTimestampInfo rtp_timestamp_info,
      const uint8_t* payload_data,
      size_t payload_size,
      std::optional<uint64_t> absolute_capture_timestamp_ms,
      uint32_t ssrc,
      std::vector<uint32_t> csrcs,
      const std::string& codec_mime_type,
      std::optional<uint16_t> sequence_number,
      std::optional<uint8_t> audio_level_dbov)
      : TransformableAudioFrameInterface(Passkey()),
        frame_type_(frame_type),
        payload_type_(payload_type),
        rtp_timestamp_info_(rtp_timestamp_info),
        payload_(payload_data, payload_size),
        absolute_capture_timestamp_ms_(absolute_capture_timestamp_ms),
        ssrc_(ssrc),
        csrcs_(std::move(csrcs)),
        codec_mime_type_(codec_mime_type),
        sequence_number_(sequence_number),
        audio_level_dbov_(audio_level_dbov) {}

  ~TransformableOutgoingAudioFrame() override = default;
  std::span<const uint8_t> GetData() const override { return payload_; }
  void SetData(std::span<const uint8_t> data) override {
    payload_.SetData(data.data(), data.size());
  }
  uint32_t GetTimestamp() const override {
    if (std::holds_alternative<RtpTimestampWithOffset>(rtp_timestamp_info_)) {
      return std::get<RtpTimestampWithOffset>(rtp_timestamp_info_);
    }
    return 0;
  }
  uint32_t GetSsrc() const override { return ssrc_; }

  IfaceFrameType Type() const override {
    return InternalFrameTypeToInterfaceFrameType(frame_type_);
  }

  uint8_t GetPayloadType() const override { return payload_type_; }
  bool CanSetPayloadType() const override { return true; }
  void SetPayloadType(uint8_t payload_type) override {
    payload_type_ = payload_type;
  }
  Direction GetDirection() const override { return Direction::kSender; }
  std::string GetMimeType() const override { return codec_mime_type_; }

  std::span<const uint32_t> GetContributingSources() const override {
    return csrcs_;
  }

  const std::optional<uint16_t> SequenceNumber() const override {
    return sequence_number_;
  }

  void SetRTPTimestamp(uint32_t rtp_timestamp_with_offset) override {
    rtp_timestamp_info_ = RtpTimestampWithOffset{rtp_timestamp_with_offset};
  }

  std::optional<uint64_t> AbsoluteCaptureTimestamp() const override {
    return absolute_capture_timestamp_ms_;
  }

  std::optional<uint8_t> AudioLevel() const override {
    return audio_level_dbov_;
  }

  bool CanSetAudioLevel() const override { return true; }
  void SetAudioLevel(std::optional<uint8_t> audio_level_dbov) override {
    if (audio_level_dbov.has_value() && audio_level_dbov > 127u) {
      audio_level_dbov = 127u;
    }
    audio_level_dbov_ = audio_level_dbov;
  }

  std::optional<Timestamp> ReceiveTime() const override { return std::nullopt; }

  std::optional<Timestamp> CaptureTime() const override {
    return absolute_capture_timestamp_ms_
               ? std::make_optional(
                     Timestamp::Millis(*absolute_capture_timestamp_ms_))
               : std::nullopt;
  }
  bool CanSetCaptureTime() const override { return true; }
  void SetCaptureTime(std::optional<Timestamp> capture_time) override {
    absolute_capture_timestamp_ms_ =
        capture_time ? std::make_optional(capture_time->ms()) : std::nullopt;
  }
  std::optional<TimeDelta> SenderCaptureTimeOffset() const override {
    return std::nullopt;
  }

  RtpTimestampInfo GetRtpTimestampInfo() const override {
    return rtp_timestamp_info_;
  }

 private:
  AudioFrameType frame_type_;
  uint8_t payload_type_;
  RtpTimestampInfo rtp_timestamp_info_;
  Buffer payload_;
  std::optional<uint64_t> absolute_capture_timestamp_ms_;
  uint32_t ssrc_;
  std::vector<uint32_t> csrcs_;
  std::string codec_mime_type_;
  std::optional<uint16_t> sequence_number_;
  std::optional<uint8_t> audio_level_dbov_;
};

ChannelSendFrameTransformerDelegate::ChannelSendFrameTransformerDelegate(
    SendFrameCallback send_frame_callback,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    TaskQueueBase* send_queue)
    : send_frame_callback_(std::move(send_frame_callback)),
      frame_transformer_(std::move(frame_transformer)),
      send_queue_(send_queue) {}

void ChannelSendFrameTransformerDelegate::Init() {
  frame_transformer_->RegisterTransformedFrameCallback(
      scoped_refptr<TransformedFrameCallback>(this));
}

void ChannelSendFrameTransformerDelegate::Reset() {
  frame_transformer_->UnregisterTransformedFrameCallback();
  frame_transformer_ = nullptr;

  MutexLock lock(&send_lock_);
  send_frame_callback_ = SendFrameCallback();
}

void ChannelSendFrameTransformerDelegate::Transform(
    AudioFrameType frame_type,
    uint8_t payload_type,
    uint32_t rtp_timestamp,
    const uint8_t* payload_data,
    size_t payload_size,
    int64_t absolute_capture_timestamp_ms,
    uint32_t ssrc,
    const std::string& codec_mime_type,
    std::optional<uint8_t> audio_level_dbov,
    const std::vector<uint32_t>& csrcs) {
  auto frame = std::make_unique<TransformableOutgoingAudioFrame>(
      frame_type, payload_type, rtp_timestamp, payload_data, payload_size,
      absolute_capture_timestamp_ms, ssrc, csrcs, codec_mime_type,
      /*sequence_number=*/std::nullopt, audio_level_dbov);
  {
    MutexLock lock(&send_lock_);
    if (short_circuit_) {
      scoped_refptr<ChannelSendFrameTransformerDelegate> delegate(this);
      send_queue_->PostTask(
          [delegate = std::move(delegate), frame = std::move(frame)]() mutable {
            delegate->SendFrame(std::move(frame));
          });
      return;
    }
  }
  frame_transformer_->Transform(std::move(frame));
}

void ChannelSendFrameTransformerDelegate::OnTransformedFrame(
    std::unique_ptr<TransformableFrameInterface> frame) {
  MutexLock lock(&send_lock_);
  if (!send_frame_callback_)
    return;
  scoped_refptr<ChannelSendFrameTransformerDelegate> delegate(this);
  send_queue_->PostTask(
      [delegate = std::move(delegate), frame = std::move(frame)]() mutable {
        delegate->SendFrame(std::move(frame));
      });
}

void ChannelSendFrameTransformerDelegate::StartShortCircuiting() {
  MutexLock lock(&send_lock_);
  short_circuit_ = true;
}

void ChannelSendFrameTransformerDelegate::SendFrame(
    std::unique_ptr<TransformableFrameInterface> frame) const {
  MutexLock lock(&send_lock_);
  RTC_DCHECK_RUN_ON(send_queue_);
  if (!send_frame_callback_)
    return;
  auto* transformed_frame =
      static_cast<TransformableAudioFrameInterface*>(frame.get());
  send_frame_callback_(
      InterfaceFrameTypeToInternalFrameType(transformed_frame->Type()),
      transformed_frame->GetPayloadType(),
      transformed_frame->GetRtpTimestampInfo(), transformed_frame->GetData(),
      transformed_frame->AbsoluteCaptureTimestamp()
          ? *transformed_frame->AbsoluteCaptureTimestamp()
          : 0,
      transformed_frame->GetContributingSources(),
      transformed_frame->AudioLevel());
}

std::unique_ptr<TransformableAudioFrameInterface> CloneSenderAudioFrame(
    TransformableAudioFrameInterface* original) {
  std::vector<uint32_t> csrcs;
  csrcs.assign(original->GetContributingSources().begin(),
               original->GetContributingSources().end());
  return std::make_unique<TransformableOutgoingAudioFrame>(
      InterfaceFrameTypeToInternalFrameType(original->Type()),
      original->GetPayloadType(), original->GetRtpTimestampInfo(),
      original->GetData().data(), original->GetData().size(),
      original->AbsoluteCaptureTimestamp(), original->GetSsrc(),
      std::move(csrcs), original->GetMimeType(), original->SequenceNumber(),
      original->AudioLevel());
}

std::unique_ptr<TransformableAudioFrameInterface> CreateSenderAudioFrame(
    TransformableAudioFrameInterface::FrameType frame_type,
    uint8_t payload_type,
    RtpTimestampInfo rtp_timestamp_info,
    const uint8_t* payload_data,
    size_t payload_size,
    std::optional<uint64_t> absolute_capture_timestamp_ms,
    uint32_t ssrc,
    const std::vector<uint32_t>& csrcs,
    const std::string& codec_mime_type,
    std::optional<uint16_t> sequence_number,
    std::optional<uint8_t> audio_level_dbov) {
  return std::make_unique<TransformableOutgoingAudioFrame>(
      InterfaceFrameTypeToInternalFrameType(frame_type), payload_type,
      rtp_timestamp_info, payload_data, payload_size,
      absolute_capture_timestamp_ms, ssrc, csrcs, codec_mime_type,
      sequence_number, audio_level_dbov);
}

}  // namespace webrtc
