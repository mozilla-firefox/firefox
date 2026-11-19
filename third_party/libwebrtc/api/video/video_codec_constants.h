/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_VIDEO_CODEC_CONSTANTS_H_
#define API_VIDEO_VIDEO_CODEC_CONSTANTS_H_

#include <cstddef>

namespace webrtc {

inline constexpr size_t kMaxEncoderBuffers = 8;
inline constexpr size_t kMaxSimulcastStreams = 3;
inline constexpr size_t kMaxSpatialLayers = 5;
inline constexpr size_t kMaxTemporalStreams = 4;
inline constexpr size_t kMaxPreferredPixelFormats = 5;
inline constexpr int kMaxFrameSidePixels = 16384;
inline constexpr int kMaxFrameSizePixels =
    kMaxFrameSidePixels * kMaxFrameSidePixels;

// This represents a best-effort complexity set, that can be configured on
// webrtc::VideoEncoder instance. A higher complexity indicates that more CPU
// cycles can be spent in order to improve the quality of the encoded picture.
// Note that the exact behavior is implementation specific and no guarantee of
// the exact behavior exists. For instance, even if a higher complexity is
// available to the encoder implementation, it may choose not to activate it if
// it deems that it will not result in a noticeable quality difference.
// The relative _target_ CPU usage is listed as comments below.
enum class VideoCodecComplexity {
  kComplexityLow = -1,    // As low as possible.
  kComplexityNormal = 0,  // Default (baseline as if not configured).
  kComplexityHigh = 1,    // 15% higher than normal.
  kComplexityHigher = 2,  // 50% higher than normal.
  kComplexityMax = 3      // Maximum RTC-compatiable setting available.
};

}  // namespace webrtc

#endif  // API_VIDEO_VIDEO_CODEC_CONSTANTS_H_
