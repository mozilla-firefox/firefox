/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/fake_decoder.h"

#include <string.h>

#include <memory>

#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "rtc_base/time_utils.h"

namespace webrtc {
namespace test {

FakeDecoder::FakeDecoder() : FakeDecoder(nullptr) {}

FakeDecoder::FakeDecoder(TaskQueueFactory* task_queue_factory)
    : callback_(nullptr),
      width_(kDefaultWidth),
      height_(kDefaultHeight),
      task_queue_factory_(task_queue_factory),
      decode_delay_ms_(0) {}

bool FakeDecoder::Configure(const Settings& settings) {
  return true;
}

int32_t FakeDecoder::Decode(const EncodedImage& input,
                            int64_t render_time_ms) {
  if (input._encodedWidth > 0 && input._encodedHeight > 0) {
    width_ = input._encodedWidth;
    height_ = input._encodedHeight;
  }

  rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(width_, height_);
  I420Buffer::SetBlack(buffer.get());
  VideoFrame frame = VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_rotation(webrtc::kVideoRotation_0)
                         .set_timestamp_ms(render_time_ms)
                         .build();
  frame.set_rtp_timestamp(input.RtpTimestamp());
  frame.set_ntp_time_ms(input.ntp_time_ms_);

  if (decode_delay_ms_ == 0 || !task_queue_) {
    callback_->Decoded(frame);
  } else {
    task_queue_->PostDelayedHighPrecisionTask(
        [frame, this]() {
          VideoFrame copy = frame;
          callback_->Decoded(copy);
        },
        TimeDelta::Millis(decode_delay_ms_));
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

void FakeDecoder::SetDelayedDecoding(int decode_delay_ms) {
  RTC_CHECK(task_queue_factory_);
  if (!task_queue_) {
    task_queue_ = task_queue_factory_->CreateTaskQueue(
        "fake_decoder", TaskQueueFactory::Priority::NORMAL);
  }
  decode_delay_ms_ = decode_delay_ms;
}

int32_t FakeDecoder::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t FakeDecoder::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

const char* FakeDecoder::kImplementationName = "fake_decoder";
VideoDecoder::DecoderInfo FakeDecoder::GetDecoderInfo() const {
  DecoderInfo info;
  info.implementation_name = kImplementationName;
  info.is_hardware_accelerated = true;
  return info;
}
const char* FakeDecoder::ImplementationName() const {
  return kImplementationName;
}

int32_t FakeH264Decoder::Decode(const EncodedImage& input,
                                int64_t render_time_ms) {
  uint8_t value = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    uint8_t kStartCode[] = {0, 0, 0, 1};
    if (i < input.size() - sizeof(kStartCode) &&
        !memcmp(&input.data()[i], kStartCode, sizeof(kStartCode))) {
      i += sizeof(kStartCode) + 1;  // Skip start code and NAL header.
    }
    if (input.data()[i] != value) {
      RTC_CHECK_EQ(value, input.data()[i])
          << "Bitstream mismatch between sender and receiver.";
      return -1;
    }
    ++value;
  }
  return FakeDecoder::Decode(input, render_time_ms);
}

}  // namespace test
}  // namespace webrtc
