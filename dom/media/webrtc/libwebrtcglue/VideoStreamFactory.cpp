/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "VideoStreamFactory.h"

#include "common/browser_logging/CSFLog.h"
#include "VideoConduit.h"

#include <algorithm>
#include "api/video_codecs/video_codec.h"
#include <cmath>
#include <limits>
#include "mozilla/Assertions.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/TemplateLib.h"
#include "rtc_base/time_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "video/config/video_encoder_config.h"

template <class t>
void ConstrainPreservingAspectRatio(uint16_t aMaxWidth, uint16_t aMaxHeight,
                                    t* aWidth, t* aHeight) {
  if (((*aWidth) <= aMaxWidth) && ((*aHeight) <= aMaxHeight)) {
    return;
  }

  if ((*aWidth) * aMaxHeight > aMaxWidth * (*aHeight)) {
    (*aHeight) = aMaxWidth * (*aHeight) / (*aWidth);
    (*aWidth) = aMaxWidth;
  } else {
    (*aWidth) = aMaxHeight * (*aWidth) / (*aHeight);
    (*aHeight) = aMaxHeight;
  }
}

namespace mozilla {

#ifdef LOGTAG
#  undef LOGTAG
#endif
#define LOGTAG "WebrtcVideoSessionConduit"

#define DEFAULT_VIDEO_MAX_FRAMERATE 30u

#define MB_OF(w, h) \
  ((unsigned int)((((w + 15) >> 4)) * ((unsigned int)((h + 15) >> 4))))
// For now, try to set the max rates well above the knee in the curve.
// Chosen somewhat arbitrarily; it's hard to find good data oriented for
// realtime interactive/talking-head recording.  These rates assume
// 30fps.

// XXX Populate this based on a pref (which we should consider sorting because
// people won't assume they need to).
static VideoStreamFactory::ResolutionAndBitrateLimits
    kResolutionAndBitrateLimits[] = {
        // clang-format off
  {MB_OF(1920, 1200), KBPS(1500), KBPS(2000), KBPS(10000)}, // >HD (3K, 4K, etc)
  {MB_OF(1280, 720), KBPS(1200), KBPS(1500), KBPS(5000)}, // HD ~1080-1200
  {MB_OF(800, 480), KBPS(200), KBPS(800), KBPS(2500)}, // HD ~720
  {MB_OF(480, 270), KBPS(150), KBPS(500), KBPS(2000)}, // WVGA
  {tl::Max<MB_OF(400, 240), MB_OF(352, 288)>::value, KBPS(125), KBPS(300), KBPS(1300)}, // VGA
  {MB_OF(176, 144), KBPS(100), KBPS(150), KBPS(500)}, // WQVGA, CIF
  {0 , KBPS(40), KBPS(80), KBPS(250)} // QCIF and below
        // clang-format on
};

auto VideoStreamFactory::GetLimitsFor(unsigned int aWidth, unsigned int aHeight,
                                      int aCapBps /* = 0 */)
    -> ResolutionAndBitrateLimits {
  // max bandwidth should be proportional (not linearly!) to resolution, and
  // proportional (perhaps linearly, or close) to current frame rate.
  int fs = MB_OF(aWidth, aHeight);

  for (const auto& resAndLimits : kResolutionAndBitrateLimits) {
    if (fs > resAndLimits.resolution_in_mb &&
        // pick the highest range where at least start rate is within cap
        // (or if we're at the end of the array).
        (aCapBps == 0 || resAndLimits.start_bitrate_bps <= aCapBps ||
         resAndLimits.resolution_in_mb == 0)) {
      return resAndLimits;
    }
  }

  MOZ_CRASH("Loop should have handled fallback");
}

/**
 * Function to set the encoding bitrate limits based on incoming frame size and
 * rate
 * @param width, height: dimensions of the frame
 * @param min: minimum bitrate in bps
 * @param start: bitrate in bps that the encoder should start with
 * @param cap: user-enforced max bitrate, or 0
 * @param pref_cap: cap enforced by prefs
 * @param negotiated_cap: cap negotiated through SDP
 * @param aVideoStream stream to apply bitrates to
 */
static void SelectBitrates(unsigned short width, unsigned short height, int min,
                           int start, int cap, int pref_cap, int negotiated_cap,
                           webrtc::VideoStream& aVideoStream) {
  int& out_min = aVideoStream.min_bitrate_bps;
  int& out_start = aVideoStream.target_bitrate_bps;
  int& out_max = aVideoStream.max_bitrate_bps;

  VideoStreamFactory::ResolutionAndBitrateLimits resAndLimits =
      VideoStreamFactory::GetLimitsFor(width, height);
  out_min = MinIgnoreZero(resAndLimits.min_bitrate_bps, cap);
  out_start = MinIgnoreZero(resAndLimits.start_bitrate_bps, cap);
  out_max = MinIgnoreZero(resAndLimits.max_bitrate_bps, cap);

  // Note: negotiated_cap is the max transport bitrate - it applies to
  // a single codec encoding, but should also apply to the sum of all
  // simulcast layers in this encoding! So sum(layers.maxBitrate) <=
  // negotiated_cap
  // Note that out_max already has had pref_cap applied to it
  out_max = MinIgnoreZero(negotiated_cap, out_max);
  out_min = std::min(out_min, out_max);
  out_start = std::min(out_start, out_max);

  if (min && min > out_min) {
    out_min = min;
  }
  // If we try to set a minimum bitrate that is too low, ViE will reject it.
  out_min = std::max(kViEMinCodecBitrate_bps, out_min);
  out_max = std::max(kViEMinCodecBitrate_bps, out_max);
  if (start && start > out_start) {
    out_start = start;
  }

  // Ensure that min <= start <= max
  if (out_min > out_max) {
    out_min = out_max;
  }
  out_start = std::min(out_max, std::max(out_start, out_min));

  MOZ_ASSERT(pref_cap == 0 || out_max <= pref_cap);
}

void VideoStreamFactory::SelectMaxFramerate(
    int aWidth, int aHeight, const VideoCodecConfig::Encoding& aEncoding,
    webrtc::VideoStream& aVideoStream) {
  MOZ_ASSERT(aEncoding.constraints.scaleDownBy >= 1.0);
  gfx::IntSize newSize(0, 0);

  if (aWidth && aHeight) {
    auto maxPixelCount = mLockScaling ? 0U : mWants.max_pixel_count;
    newSize = CalculateScaledResolution(
        aWidth, aHeight, aEncoding.constraints.scaleDownBy, maxPixelCount);
  }

  if (newSize.width == 0 || newSize.height == 0) {
    return;
  }

  uint16_t max_width = mCodecConfig.mEncodingConstraints.maxWidth;
  uint16_t max_height = mCodecConfig.mEncodingConstraints.maxHeight;
  if (max_width || max_height) {
    max_width = max_width ? max_width : UINT16_MAX;
    max_height = max_height ? max_height : UINT16_MAX;
    ConstrainPreservingAspectRatio(max_width, max_height, &newSize.width,
                                   &newSize.height);
  }

  MOZ_ASSERT(newSize.width > 0);
  MOZ_ASSERT(newSize.height > 0);
  aVideoStream.width = newSize.width;
  aVideoStream.height = newSize.height;
  SelectMaxFramerateForAllStreams(newSize.width, newSize.height);

  CSFLogInfo(LOGTAG, "%s Input frame %ux%u, RID %s scaling to %zux%zu",
             __FUNCTION__, aWidth, aHeight, aEncoding.rid.c_str(),
             aVideoStream.width, aVideoStream.height);

  // mMaxFramerateForAllStreams is based on codec-wide stuff like fmtp, and
  // hard-coded limits based on the source resolution.
  // mCodecConfig.mEncodingConstraints.maxFps does not take the hard-coded
  // limits into account, so we have mMaxFramerateForAllStreams which
  // incorporates those. Per-encoding max framerate is based on parameters
  // from JS, and maybe rid
  unsigned int max_framerate = SelectFrameRate(
      mMaxFramerateForAllStreams, aVideoStream.width, aVideoStream.height);
  max_framerate = std::min(
      WebrtcVideoConduit::ToLibwebrtcMaxFramerate(aEncoding.constraints.maxFps),
      max_framerate);
  if (max_framerate >= std::numeric_limits<int>::max()) {
    // If nothing has specified any kind of limit (uncommon), pick something
    // reasonable.
    max_framerate = DEFAULT_VIDEO_MAX_FRAMERATE;
  }
  aVideoStream.max_framerate = static_cast<int>(max_framerate);
}

std::vector<webrtc::VideoStream> VideoStreamFactory::CreateEncoderStreams(
    int aWidth, int aHeight, const webrtc::VideoEncoderConfig& aConfig) {
  // We only allow one layer when screensharing
  const size_t streamCount =
      mCodecMode == webrtc::VideoCodecMode::kScreensharing
          ? 1
          : aConfig.number_of_streams;

  MOZ_RELEASE_ASSERT(streamCount >= 1, "Should request at least one stream");
  MOZ_RELEASE_ASSERT(streamCount <= aConfig.simulcast_layers.size());

  std::vector<webrtc::VideoStream> streams;
  streams.reserve(streamCount);

  {
    auto frameRateController = mFramerateController.Lock();
    frameRateController->Reset();
  }

  for (size_t idx = 0; idx < streamCount; ++idx) {
    webrtc::VideoStream video_stream = aConfig.simulcast_layers[idx];
    auto& encoding = mCodecConfig.mEncodings[idx];
    MOZ_ASSERT(video_stream.active == encoding.active);

    SelectMaxFramerate(aWidth, aHeight, encoding, video_stream);

    if (video_stream.width == 0 || video_stream.height == 0) {
      CSFLogInfo(LOGTAG,
                 "%s Stream with RID %s ignored because of no resolution.",
                 __FUNCTION__, encoding.rid.c_str());
      continue;
    }

    CSFLogInfo(LOGTAG, "%s Stream with RID %s maxFps=%d (global max fps = %u)",
               __FUNCTION__, encoding.rid.c_str(), video_stream.max_framerate,
               (unsigned)mMaxFramerateForAllStreams);

    SelectBitrates(video_stream.width, video_stream.height, mMinBitrate,
                   mStartBitrate, encoding.constraints.maxBr, mPrefMaxBitrate,
                   mNegotiatedMaxBitrate, video_stream);

    video_stream.bitrate_priority = aConfig.bitrate_priority;
    video_stream.max_qp = kQpMax;

    if (streamCount > 1) {
      if (mCodecMode == webrtc::VideoCodecMode::kScreensharing) {
        video_stream.num_temporal_layers = 1;
      } else {
        video_stream.num_temporal_layers = 2;
      }
      // XXX Bug 1390215 investigate using more of
      // simulcast.cc:GetSimulcastConfig() or our own algorithm to replace it
    }

    if (mCodecConfig.mName == "H264") {
      if (mCodecConfig.mEncodingConstraints.maxMbps > 0) {
        // Not supported yet!
        CSFLogError(LOGTAG, "%s H.264 max_mbps not supported yet",
                    __FUNCTION__);
      }
    }
    streams.push_back(video_stream);
  }

  MOZ_RELEASE_ASSERT(streams.size(), "Should configure at least one stream");
  return streams;
}

gfx::IntSize VideoStreamFactory::CalculateScaledResolution(
    int aWidth, int aHeight, double aScaleDownByResolution,
    unsigned int aMaxPixelCount) {
  // If any adjustments like scaleResolutionDownBy or maxFS are being given
  // we want to choose a height and width here to provide for more variety
  // in possible resolutions.
  int width = aWidth;
  int height = aHeight;

  if (aScaleDownByResolution > 1) {
    width = static_cast<int>(aWidth / aScaleDownByResolution);
    height = static_cast<int>(aHeight / aScaleDownByResolution);
  }

  // Check if we still need to adjust resolution down more due to other
  // constraints.
  if (mCodecConfig.mEncodingConstraints.maxFs > 0 || aMaxPixelCount > 0) {
    auto currentFs = static_cast<unsigned int>(width * height);
    auto maxFs =
        (mCodecConfig.mEncodingConstraints.maxFs > 0 && aMaxPixelCount > 0)
            ? std::min((mCodecConfig.mEncodingConstraints.maxFs * 16 * 16),
                       aMaxPixelCount)
            : std::max((mCodecConfig.mEncodingConstraints.maxFs * 16 * 16),
                       aMaxPixelCount);

    // If our currentFs is greater than maxFs we calculate a width and height
    // that will get as close as possible to maxFs and try to maintain aspect
    // ratio.
    if (currentFs > maxFs) {
      if (aWidth > aHeight) {  // Landscape
        auto aspectRatio = static_cast<double>(aWidth) / aHeight;

        height = static_cast<int>(std::sqrt(maxFs / aspectRatio));
        width = static_cast<int>(height * aspectRatio);
      } else {  // Portrait
        auto aspectRatio = static_cast<double>(aHeight) / aWidth;

        width = static_cast<int>(std::sqrt(maxFs / aspectRatio));
        height = static_cast<int>(width * aspectRatio);
      }
    }
  }

  // Simplest possible adaptation to resolution alignment.
  width -= width % mWants.resolution_alignment;
  height -= height % mWants.resolution_alignment;

  // Dont scale below our minimum value to prevent problems.
  const int minSize = 1;
  if (width < minSize || height < minSize) {
    width = minSize;
    height = minSize;
  }

  return gfx::IntSize(width, height);
}

void VideoStreamFactory::SelectMaxFramerateForAllStreams(
    unsigned short aWidth, unsigned short aHeight) {
  int max_fs = std::numeric_limits<int>::max();
  if (!mLockScaling) {
    max_fs = mWants.max_pixel_count;
  }
  // Limit resolution to max-fs
  if (mCodecConfig.mEncodingConstraints.maxFs) {
    // max-fs is in macroblocks, convert to pixels
    max_fs = std::min(
        max_fs,
        static_cast<int>(mCodecConfig.mEncodingConstraints.maxFs * (16 * 16)));
  }

  unsigned int framerate_all_streams =
      SelectFrameRate(mMaxFramerateForAllStreams, aWidth, aHeight);
  unsigned int maxFrameRate = mMaxFramerateForAllStreams;
  if (mMaxFramerateForAllStreams != framerate_all_streams) {
    CSFLogDebug(LOGTAG, "%s: framerate changing to %u (from %u)", __FUNCTION__,
                framerate_all_streams, maxFrameRate);
    mMaxFramerateForAllStreams = framerate_all_streams;
  }

  int framerate_with_wants;
  if (framerate_all_streams > std::numeric_limits<int>::max()) {
    framerate_with_wants = std::numeric_limits<int>::max();
  } else {
    framerate_with_wants = static_cast<int>(framerate_all_streams);
  }

  framerate_with_wants =
      std::min(framerate_with_wants, mWants.max_framerate_fps);
  CSFLogDebug(LOGTAG,
              "%s: Calling OnOutputFormatRequest, max_fs=%d, max_fps=%d",
              __FUNCTION__, max_fs, framerate_with_wants);
  auto frameRateController = mFramerateController.Lock();
  frameRateController->SetMaxFramerate(framerate_with_wants);
}

unsigned int VideoStreamFactory::SelectFrameRate(
    unsigned int aOldFramerate, unsigned short aSendingWidth,
    unsigned short aSendingHeight) {
  unsigned int new_framerate = aOldFramerate;

  // Limit frame rate based on max-mbps
  if (mCodecConfig.mEncodingConstraints.maxMbps) {
    unsigned int cur_fs, mb_width, mb_height;

    mb_width = (aSendingWidth + 15) >> 4;
    mb_height = (aSendingHeight + 15) >> 4;

    cur_fs = mb_width * mb_height;
    if (cur_fs > 0) {  // in case no frames have been sent
      new_framerate = mCodecConfig.mEncodingConstraints.maxMbps / cur_fs;
    }
  }

  new_framerate =
      std::min(new_framerate, WebrtcVideoConduit::ToLibwebrtcMaxFramerate(
                                  mCodecConfig.mEncodingConstraints.maxFps));
  return new_framerate;
}

bool VideoStreamFactory::ShouldDropFrame(const webrtc::VideoFrame& aFrame) {
  bool hasNonZeroLayer = false;
  {
    const size_t streamCount =
        mCodecMode == webrtc::VideoCodecMode::kScreensharing
            ? 1
            : mCodecConfig.mEncodings.size();
    for (int idx = streamCount - 1; idx >= 0; --idx) {
      const auto& encoding = mCodecConfig.mEncodings[idx];
      if (aFrame.width() / encoding.constraints.scaleDownBy >= 1.0 &&
          aFrame.height() / encoding.constraints.scaleDownBy >= 1.0) {
        hasNonZeroLayer = true;
        break;
      }
    }
  }
  if (!hasNonZeroLayer) {
    return true;
  }

  auto frameRateController = mFramerateController.Lock();
  return frameRateController->ShouldDropFrame(aFrame.timestamp_us() *
                                              rtc::kNumNanosecsPerMicrosec);
}

}  // namespace mozilla
