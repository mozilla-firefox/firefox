/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/fake_vp8_decoder.h"

#include <stddef.h>

#include "absl/types/optional.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

namespace webrtc {
namespace test {

namespace {
// Read width and height from the payload of the frame if it is a key frame the
// same way as the real VP8 decoder.
// FakeEncoder writes width, height and frame type.
void ParseFakeVp8(const unsigned char* data, int* width, int* height) {
  bool key_frame = data[0] == 0;
  if (key_frame) {
    *width = ((data[7] << 8) + data[6]) & 0x3FFF;
    *height = ((data[9] << 8) + data[8]) & 0x3FFF;
  }
}
}  // namespace

FakeVp8Decoder::FakeVp8Decoder() : callback_(nullptr), width_(0), height_(0) {}

bool FakeVp8Decoder::Configure(const Settings& settings) {
  return true;
}

int32_t FakeVp8Decoder::Decode(const EncodedImage& input,
                               int64_t render_time_ms) {
  constexpr size_t kMinPayLoadHeaderLength = 10;
  if (input.size() < kMinPayLoadHeaderLength) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  ParseFakeVp8(input.data(), &width_, &height_);

  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(I420Buffer::Create(width_, height_))
          .set_rotation(webrtc::kVideoRotation_0)
          .set_timestamp_ms(render_time_ms)
          .build();
  frame.set_rtp_timestamp(input.RtpTimestamp());
  frame.set_ntp_time_ms(input.ntp_time_ms_);

  callback_->Decoded(frame, /*decode_time_ms=*/absl::nullopt,
                     /*qp=*/absl::nullopt);

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t FakeVp8Decoder::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t FakeVp8Decoder::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

VideoDecoder::DecoderInfo FakeVp8Decoder::GetDecoderInfo() const {
  DecoderInfo info;
  info.implementation_name = kImplementationName;
  info.is_hardware_accelerated = false;
  return info;
}

const char* FakeVp8Decoder::ImplementationName() const {
  return kImplementationName;
}

}  // namespace test
}  // namespace webrtc
