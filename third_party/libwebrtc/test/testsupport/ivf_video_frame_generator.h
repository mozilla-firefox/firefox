/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_TESTSUPPORT_IVF_VIDEO_FRAME_GENERATOR_H_
#define TEST_TESTSUPPORT_IVF_VIDEO_FRAME_GENERATOR_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/test/frame_generator_interface.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_decoder.h"
#include "modules/video_coding/utility/ivf_file_reader.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {
namespace test {

// All methods except constructor must be used from the same thread.
class IvfVideoFrameGenerator : public FrameGeneratorInterface {
 public:
  IvfVideoFrameGenerator(const Environment& env, absl::string_view file_name);
  ~IvfVideoFrameGenerator() override;

  VideoFrameData NextFrame() override;
  void ChangeResolution(size_t width, size_t height) override;
  Resolution GetResolution() const override;

  absl::optional<int> fps() const override { return absl::nullopt; }

 private:
  class DecodedCallback : public DecodedImageCallback {
   public:
    explicit DecodedCallback(IvfVideoFrameGenerator* reader)
        : reader_(reader) {}

    int32_t Decoded(VideoFrame& decoded_image) override;
    int32_t Decoded(VideoFrame& decoded_image, int64_t decode_time_ms) override;
    void Decoded(VideoFrame& decoded_image,
                 absl::optional<int32_t> decode_time_ms,
                 absl::optional<uint8_t> qp) override;

   private:
    IvfVideoFrameGenerator* const reader_;
  };

  void OnFrameDecoded(const VideoFrame& decoded_frame);

  DecodedCallback callback_;
  std::unique_ptr<IvfFileReader> file_reader_;
  std::unique_ptr<VideoDecoder> video_decoder_;

  size_t width_;
  size_t height_;

  // This lock is used to ensure that all API method will be called
  // sequentially. It is required because we need to ensure that generator
  // won't be destroyed while it is reading the next frame on another thread,
  // because it will cause SIGSEGV when decoder callback will be invoked.
  //
  // FrameGenerator is injected into PeerConnection via some scoped_ref object
  // and it can happen that the last pointer will be destroyed on the different
  // thread comparing to the one from which frames were read.
  Mutex lock_;
  // This lock is used to sync between sending and receiving frame from decoder.
  // We can't reuse `lock_` because then generator can be destroyed between
  // frame was sent to decoder and decoder callback was invoked.
  Mutex frame_decode_lock_;

  rtc::Event next_frame_decoded_;
  absl::optional<VideoFrame> next_frame_ RTC_GUARDED_BY(frame_decode_lock_);
};

}  // namespace test
}  // namespace webrtc

#endif  // TEST_TESTSUPPORT_IVF_VIDEO_FRAME_GENERATOR_H_
