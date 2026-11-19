/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_video_frame_transformer_delegate.h"

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
#include "api/task_queue/task_queue_factory.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_metadata.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video_codecs/video_codec.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {
namespace {

// Using a reasonable default of 10ms for the retransmission delay for frames
// not coming from this sender's encoder. This is usually taken from an
// estimate of the RTT of the link,so 10ms should be a reasonable estimate for
// frames being re-transmitted to a peer, probably on the same network.
const TimeDelta kDefaultRetransmissionsTime = TimeDelta::Millis(10);
}  // namespace

class TransformableVideoSenderFrame : public TransformableVideoFrameInterface {
 public:
  TransformableVideoSenderFrame(const EncodedImage& encoded_image,
                                const RTPVideoHeader& video_header,
                                int payload_type,
                                VideoCodecType codec_type,
                                uint32_t rtp_timestamp_with_offset,
                                TimeDelta expected_retransmission_time,
                                uint32_t ssrc,
                                std::vector<uint32_t> csrcs,
                                std::string rid)
      : TransformableVideoSenderFrame(
            encoded_image,
            video_header,
            payload_type,
            codec_type,
            RtpTimestampInfo(RtpTimestampWithOffset{rtp_timestamp_with_offset}),
            expected_retransmission_time,
            ssrc,
            std::move(csrcs),
            std::move(rid)) {}

  TransformableVideoSenderFrame(const EncodedImage& encoded_image,
                                const RTPVideoHeader& video_header,
                                int payload_type,
                                VideoCodecType codec_type,
                                RtpTimestampInfo rtp_timestamp_info,
                                TimeDelta expected_retransmission_time,
                                uint32_t ssrc,
                                std::vector<uint32_t> csrcs,
                                std::string rid)
      : TransformableVideoFrameInterface(Passkey()),
        encoded_data_(encoded_image.GetEncodedData()),
        pre_transform_payload_size_(encoded_image.size()),
        header_(video_header),
        frame_type_(encoded_image.frame_type()),
        payload_type_(payload_type),
        codec_type_(codec_type),
        rtp_timestamp_info_(rtp_timestamp_info),
        capture_time_(encoded_image.CaptureTime()),
        presentation_timestamp_(encoded_image.PresentationTimestamp()),
        expected_retransmission_time_(expected_retransmission_time),
        ssrc_(ssrc),
        csrcs_(std::move(csrcs)),
        rid_(std::move(rid)) {
    RTC_DCHECK_GE(payload_type_, 0);
    RTC_DCHECK_LE(payload_type_, 127);
  }

  ~TransformableVideoSenderFrame() override = default;

  // Implements TransformableVideoFrameInterface.
  std::span<const uint8_t> GetData() const override { return *encoded_data_; }

  void SetData(std::span<const uint8_t> data) override {
    encoded_data_ = EncodedImageBuffer::Create(data.data(), data.size());
  }

  size_t GetPreTransformPayloadSize() const {
    return pre_transform_payload_size_;
  }

  uint32_t GetTimestamp() const override {
    if (std::holds_alternative<RtpTimestampWithOffset>(rtp_timestamp_info_)) {
      return std::get<RtpTimestampWithOffset>(rtp_timestamp_info_);
    }
    return 0;
  }
  RtpTimestampInfo GetRtpTimestampInfo() const override {
    return rtp_timestamp_info_;
  }
  void SetRTPTimestamp(uint32_t timestamp) override {
    rtp_timestamp_info_ = RtpTimestampWithOffset{timestamp};
  }

  uint32_t GetSsrc() const override { return ssrc_; }

  bool IsKeyFrame() const override {
    return frame_type_ == VideoFrameType::kVideoFrameKey;
  }

  std::optional<std::string> Rid() const override { return rid_; }

  VideoFrameMetadata Metadata() const override {
    VideoFrameMetadata metadata = header_.GetAsMetadata();
    metadata.SetSsrc(ssrc_);
    metadata.SetCsrcs(csrcs_);
    return metadata;
  }

  void SetMetadata(const VideoFrameMetadata& metadata) override {
    header_.SetFromMetadata(metadata);
    ssrc_ = metadata.GetSsrc();
    csrcs_ = metadata.GetCsrcs();
  }

  const RTPVideoHeader& GetHeader() const { return header_; }
  uint8_t GetPayloadType() const override { return payload_type_; }
  VideoCodecType GetCodecType() const { return codec_type_; }
  std::optional<Timestamp> GetCaptureTimeIdentifier() const override {
    return presentation_timestamp_;
  }
  std::optional<Timestamp> GetPresentationTimestamp() const override {
    return presentation_timestamp_;
  }

  TimeDelta GetExpectedRetransmissionTime() const {
    return expected_retransmission_time_;
  }

  Direction GetDirection() const override { return Direction::kSender; }
  std::string GetMimeType() const override {
    std::string mime_type = "video/";
    return mime_type + CodecTypeToPayloadString(codec_type_);
  }

  std::optional<Timestamp> ReceiveTime() const override { return std::nullopt; }

  std::optional<Timestamp> CaptureTime() const override {
    return capture_time_;
  }

  std::optional<TimeDelta> SenderCaptureTimeOffset() const override {
    return std::nullopt;
  }

 private:
  scoped_refptr<EncodedImageBufferInterface> encoded_data_;
  const size_t pre_transform_payload_size_;
  RTPVideoHeader header_;
  const VideoFrameType frame_type_;
  const uint8_t payload_type_;
  const VideoCodecType codec_type_;
  RtpTimestampInfo rtp_timestamp_info_;
  const Timestamp capture_time_;
  const std::optional<Timestamp> presentation_timestamp_;
  const TimeDelta expected_retransmission_time_;

  uint32_t ssrc_;
  std::vector<uint32_t> csrcs_;
  const std::string rid_;
};

RTPSenderVideoFrameTransformerDelegate::RTPSenderVideoFrameTransformerDelegate(
    RTPVideoFrameSenderInterface* sender,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    uint32_t ssrc,
    std::string rid,
    TaskQueueFactory* task_queue_factory,
    TaskQueueFactory::Priority transformation_queue_priority)
    : sender_(sender),
      frame_transformer_(std::move(frame_transformer)),
      ssrc_(ssrc),
      rid_(std::move(rid)),
      transformation_queue_(
          task_queue_factory->CreateTaskQueue("VideoFrameTransformerQueue",
                                              transformation_queue_priority)) {}

void RTPSenderVideoFrameTransformerDelegate::Init() {
  frame_transformer_->RegisterTransformedFrameSinkCallback(
      scoped_refptr<TransformedFrameCallback>(this), ssrc_);
}

bool RTPSenderVideoFrameTransformerDelegate::TransformFrame(
    int payload_type,
    VideoCodecType codec_type,
    uint32_t rtp_timestamp,
    const EncodedImage& encoded_image,
    RTPVideoHeader video_header,
    TimeDelta expected_retransmission_time,
    const std::vector<uint32_t>& csrcs) {
  {
    MutexLock lock(&sender_lock_);
    if (short_circuit_) {
      sender_->SendVideoFrame(
          payload_type, codec_type, RtpTimestampWithOffset{rtp_timestamp},
          encoded_image.CaptureTime(), *encoded_image.GetEncodedData(),
          encoded_image.size(), video_header, expected_retransmission_time,
          csrcs);
      return true;
    }
  }
  frame_transformer_->Transform(std::make_unique<TransformableVideoSenderFrame>(
      encoded_image, video_header, payload_type, codec_type, rtp_timestamp,
      expected_retransmission_time, ssrc_, csrcs, rid_));
  return true;
}

void RTPSenderVideoFrameTransformerDelegate::OnTransformedFrame(
    std::unique_ptr<TransformableFrameInterface> frame) {
  MutexLock lock(&sender_lock_);

  if (!sender_) {
    return;
  }
  scoped_refptr<RTPSenderVideoFrameTransformerDelegate> delegate(this);
  transformation_queue_->PostTask(
      [delegate = std::move(delegate), frame = std::move(frame)]() mutable {
        RTC_DCHECK_RUN_ON(delegate->transformation_queue_.get());
        delegate->SendVideo(std::move(frame));
      });
}

void RTPSenderVideoFrameTransformerDelegate::StartShortCircuiting() {
  MutexLock lock(&sender_lock_);
  short_circuit_ = true;
}

void RTPSenderVideoFrameTransformerDelegate::StopShortCircuiting() {
  MutexLock lock(&sender_lock_);
  short_circuit_ = false;
}

void RTPSenderVideoFrameTransformerDelegate::SendVideo(
    std::unique_ptr<TransformableFrameInterface> transformed_frame) const {
  RTC_DCHECK_RUN_ON(transformation_queue_.get());
  MutexLock lock(&sender_lock_);
  if (!sender_)
    return;
  if (transformed_frame->GetDirection() ==
      TransformableFrameInterface::Direction::kSender) {
    auto* transformed_video_frame =
        static_cast<TransformableVideoSenderFrame*>(transformed_frame.get());
    RTC_CHECK(transformed_video_frame->CaptureTime().has_value());
    sender_->SendVideoFrame(
        transformed_video_frame->GetPayloadType(),
        transformed_video_frame->GetCodecType(),
        transformed_video_frame->GetRtpTimestampInfo(),
        *transformed_video_frame->CaptureTime(),
        transformed_video_frame->GetData(),
        transformed_video_frame->GetPreTransformPayloadSize(),
        transformed_video_frame->GetHeader(),
        transformed_video_frame->GetExpectedRetransmissionTime(),
        transformed_video_frame->Metadata().GetCsrcs());
  } else {
    auto* transformed_video_frame =
        static_cast<TransformableVideoFrameInterface*>(transformed_frame.get());
    VideoFrameMetadata metadata = transformed_video_frame->Metadata();
    // TODO(bugs.webrtc.org/14708): Use an actual RTT estimate for the
    // retransmission time instead of a const default, in the same way as a
    // locally encoded frame.
    sender_->SendVideoFrame(transformed_video_frame->GetPayloadType(),
                            metadata.GetCodec(),
                            transformed_video_frame->GetRtpTimestampInfo(),
                            /*capture_time=*/Timestamp::MinusInfinity(),
                            transformed_video_frame->GetData(),
                            transformed_video_frame->GetData().size(),
                            RTPVideoHeader::FromMetadata(metadata),
                            kDefaultRetransmissionsTime, metadata.GetCsrcs());
  }
}

void RTPSenderVideoFrameTransformerDelegate::SetVideoStructureUnderLock(
    const FrameDependencyStructure* video_structure) {
  MutexLock lock(&sender_lock_);
  RTC_CHECK(sender_);
  sender_->SetVideoStructureAfterTransformation(video_structure);
}

void RTPSenderVideoFrameTransformerDelegate::SetVideoLayersAllocationUnderLock(
    VideoLayersAllocation allocation) {
  MutexLock lock(&sender_lock_);
  RTC_CHECK(sender_);
  sender_->SetVideoLayersAllocationAfterTransformation(std::move(allocation));
}

void RTPSenderVideoFrameTransformerDelegate::Reset() {
  frame_transformer_->UnregisterTransformedFrameSinkCallback(ssrc_);
  frame_transformer_ = nullptr;
  {
    MutexLock lock(&sender_lock_);
    sender_ = nullptr;
  }
  // Wait until all pending tasks are executed, to ensure that the last ref
  // standing is not on the transformation queue.
  Event flush;
  transformation_queue_->PostTask([this, &flush]() {
    RTC_DCHECK_RUN_ON(transformation_queue_.get());
    flush.Set();
  });
  flush.Wait(Event::kForever);
}

std::unique_ptr<TransformableVideoFrameInterface> CloneSenderVideoFrame(
    TransformableVideoFrameInterface* original) {
  auto encoded_image_buffer = EncodedImageBuffer::Create(
      original->GetData().data(), original->GetData().size());
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(encoded_image_buffer);
  encoded_image.set_frame_type(original->IsKeyFrame()
                                   ? VideoFrameType::kVideoFrameKey
                                   : VideoFrameType::kVideoFrameDelta);
  // TODO(bugs.webrtc.org/14708): Fill in other EncodedImage parameters
  // TODO(bugs.webrtc.org/14708): Use an actual RTT estimate for the
  // retransmission time instead of a const default, in the same way as a
  // locally encoded frame.
  VideoFrameMetadata metadata = original->Metadata();
  RTPVideoHeader new_header = RTPVideoHeader::FromMetadata(metadata);
  return std::make_unique<TransformableVideoSenderFrame>(
      encoded_image, new_header, original->GetPayloadType(), new_header.codec,
      original->GetRtpTimestampInfo(), kDefaultRetransmissionsTime,
      original->GetSsrc(), metadata.GetCsrcs(),
      original->Rid().value_or(std::string()));
}

std::unique_ptr<TransformableVideoFrameInterface> CreateSenderVideoFrame(
    const EncodedImage& encoded_image,
    const RTPVideoHeader& video_header,
    uint8_t payload_type,
    VideoCodecType codec_type,
    RtpTimestampInfo rtp_timestamp_info,
    const std::vector<uint32_t>& csrcs) {
  return std::make_unique<TransformableVideoSenderFrame>(
      encoded_image, video_header, payload_type, codec_type, rtp_timestamp_info,
      kDefaultRetransmissionsTime, /*ssrc=*/0, csrcs, /*rid=*/"");
}

}  // namespace webrtc
