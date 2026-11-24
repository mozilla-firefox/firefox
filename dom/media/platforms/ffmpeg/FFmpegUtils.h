/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGUTILS_H_
#define DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGUTILS_H_

#include <cstddef>

#include "FFmpegLibWrapper.h"
#include "nsStringFwd.h"

// This must be the last header included
#include "FFmpegLibs.h"

namespace mozilla {

#if LIBAVCODEC_VERSION_MAJOR >= 57
using FFmpegBitRate = int64_t;
constexpr size_t FFmpegErrorMaxStringSize = AV_ERROR_MAX_STRING_SIZE;
#else
using FFmpegBitRate = int;
constexpr size_t FFmpegErrorMaxStringSize = 64;
#endif

nsCString MakeErrorString(const FFmpegLibWrapper* aLib, int aErrNum);

template <typename T, typename F>
void IterateZeroTerminated(const T& aList, F&& aLambda) {
  for (size_t i = 0; aList[i] != 0; i++) {
    aLambda(aList[i]);
  }
}

inline bool IsVideoCodec(AVCodecID aCodecID) {
  switch (aCodecID) {
    case AV_CODEC_ID_H264:
#if LIBAVCODEC_VERSION_MAJOR >= 54
    case AV_CODEC_ID_VP8:
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AV_CODEC_ID_VP9:
    case AV_CODEC_ID_HEVC:
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 59
    case AV_CODEC_ID_AV1:
#endif
      return true;
    default:
      return false;
  }
}

inline int64_t GetFramePts(const AVFrame* aFrame) {
#if LIBAVCODEC_VERSION_MAJOR > 57
  return aFrame->pts;
#else
  return aFrame->pkt_pts;
#endif
}

// Access the correct location for the channel count, based on ffmpeg version.
template <typename T>
inline int& ChannelCount(T* aObject) {
#if LIBAVCODEC_VERSION_MAJOR <= 59
  return aObject->channels;
#else
  return aObject->ch_layout.nb_channels;
#endif
}

// Access the correct location for the duration, based on ffmpeg version.
template <typename T>
inline int64_t& Duration(T* aObject) {
#if LIBAVCODEC_VERSION_MAJOR < 61
  return aObject->pkt_duration;
#else
  return aObject->duration;
#endif
}

// Access the correct location for the duration, based on ffmpeg version.
template <typename T>
inline const int64_t& Duration(const T* aObject) {
#if LIBAVCODEC_VERSION_MAJOR < 61
  return aObject->pkt_duration;
#else
  return aObject->duration;
#endif
}

const char* AVCodecToString(const AVCodecID& aCodec);

// ffmpeg/libavcodec/internal.h defines STRIDE_ALIGN for
// avcodec_default_get_buffer2() according to used instructions set.
// The recent biggest value is 64 for AVX-512.
// We need to keep our internal align in sync with the biggest value
// to make sure our custom get_buffer2() allocator works with any system
// provided ffmpeg.

// STRIDE_ALIGN from libavcodec/internal.h
constexpr int32_t kStrideAlign = 64;
constexpr int32_t kPlaneTrailingPadding = 16 + kStrideAlign - 1;

constexpr int32_t GetBuffer2StrideAlign(int32_t aStride) {
  return (kPlaneTrailingPadding + aStride - 1) / aStride;
}

}  // namespace mozilla

#endif  // DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGUTILS_H_
