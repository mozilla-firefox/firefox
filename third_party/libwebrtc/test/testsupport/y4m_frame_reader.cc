/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>

#include <charconv>
#include <string>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "rtc_base/logging.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"

namespace webrtc {
namespace test {
namespace {
constexpr int kFrameHeaderSize = 6;  // "FRAME\n"
}  // namespace

void ParseY4mHeader(std::string filepath,
                    Resolution* resolution,
                    int* header_size) {
  FILE* file = fopen(filepath.c_str(), "r");
  RTC_CHECK(file != NULL) << "Cannot open " << filepath;

  // Length of Y4M header is technically unlimited due to the comment tag 'X'.
  char h[1024];
  RTC_CHECK(fgets(h, sizeof(h), file) != NULL)
      << "File " << filepath << " is too small";
  fclose(file);

  std::vector<absl::string_view> header = rtc::split(h, ' ');
  RTC_CHECK(!header.empty() && header[0] == "YUV4MPEG2")
      << filepath << " is not a valid Y4M file";

  for (size_t i = 1; i < header.size(); ++i) {
    RTC_CHECK(!header[i].empty());
    switch (header[i][0]) {
      case 'W': {
        auto n = header[i].substr(1);
        std::from_chars(n.data(), n.data() + n.size(), resolution->width);
        continue;
      }
      case 'H': {
        auto n = header[i].substr(1);
        std::from_chars(n.data(), n.data() + n.size(), resolution->height);
        continue;
      }
      default: {
        continue;
      }
    }
  }

  RTC_CHECK_GT(resolution->width, 0) << "Width must be positive";
  RTC_CHECK_GT(resolution->height, 0) << "Height must be positive";

  *header_size = strcspn(h, "\n") + 1;
  RTC_CHECK(static_cast<unsigned>(*header_size) < sizeof(h))
      << filepath << " has unexpectedly large header";
}

Y4mFrameReaderImpl::Y4mFrameReaderImpl(std::string filepath,
                                       RepeatMode repeat_mode)
    : YuvFrameReaderImpl(filepath, Resolution(), repeat_mode) {}

void Y4mFrameReaderImpl::Init() {
  file_ = fopen(filepath_.c_str(), "rb");
  RTC_CHECK(file_ != nullptr) << "Cannot open " << filepath_;

  ParseY4mHeader(filepath_, &resolution_, &header_size_bytes_);
  frame_size_bytes_ =
      CalcBufferSize(VideoType::kI420, resolution_.width, resolution_.height);
  frame_size_bytes_ += kFrameHeaderSize;

  size_t file_size_bytes = GetFileSize(filepath_);
  RTC_CHECK_GT(file_size_bytes, 0u) << "File " << filepath_ << " is empty";
  RTC_CHECK_GT(file_size_bytes, header_size_bytes_)
      << "File " << filepath_ << " is too small";

  num_frames_ = static_cast<int>((file_size_bytes - header_size_bytes_) /
                                 frame_size_bytes_);
  RTC_CHECK_GT(num_frames_, 0u) << "File " << filepath_ << " is too small";
  header_size_bytes_ += kFrameHeaderSize;
}

std::unique_ptr<FrameReader> CreateY4mFrameReader(std::string filepath) {
  return CreateY4mFrameReader(filepath,
                              YuvFrameReaderImpl::RepeatMode::kSingle);
}

std::unique_ptr<FrameReader> CreateY4mFrameReader(
    std::string filepath,
    YuvFrameReaderImpl::RepeatMode repeat_mode) {
  Y4mFrameReaderImpl* frame_reader =
      new Y4mFrameReaderImpl(filepath, repeat_mode);
  frame_reader->Init();
  return std::unique_ptr<FrameReader>(frame_reader);
}

}  // namespace test
}  // namespace webrtc
