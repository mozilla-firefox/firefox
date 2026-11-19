/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "VideoStreamFactory.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "GMPUtils.h"
#include "VideoConduit.h"
#include "api/video/resolution.h"
#include "common/browser_logging/CSFLog.h"
#include "mozilla/Assertions.h"
#include "mozilla/gfx/Point.h"
#include "video/config/simulcast.h"
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
  ((unsigned int)(((((w) + 15) >> 4)) * ((unsigned int)(((h) + 15) >> 4))))
auto VideoStreamFactory::GetUnicastLimitsFor(gfx::IntSize aSize,
                                             int aCapBps /* = 0 */)
    -> ResolutionAndBitrateLimits {
  // For now, try to set the max rates well above the knee in the curve.
  // Chosen somewhat arbitrarily; it's hard to find good data oriented for
  // realtime interactive/talking-head recording.  These rates assume
  // 30fps.
  //
  // The minimums are libwebrtc's lowest video bitrate, for every codec.
  // Without suspend_below_min_bitrate libwebrtc gives a stream its minimum even
  // when the bandwidth estimate is lower, so higher ones keep us from adapting.

  // XXX Populate this based on a pref (which we should consider sorting because
  // people won't assume they need to).
  static constexpr ResolutionAndBitrateLimits kResolutionAndBitrateLimits[] = {
      // clang-format off
  {MB_OF(1920, 1200), kViEMinCodecBitrate_bps, KBPS(2000), KBPS(10000)}, // >HD (3K, 4K, etc)
  {MB_OF(1280, 720), kViEMinCodecBitrate_bps, KBPS(1500), KBPS(5000)}, // HD ~1080-1200
  {MB_OF(800, 480), kViEMinCodecBitrate_bps, KBPS(800), KBPS(2500)}, // HD ~720
  {MB_OF(480, 270), kViEMinCodecBitrate_bps, KBPS(500), KBPS(2000)}, // WVGA
  {std::max(MB_OF(400, 240), MB_OF(352, 288)), kViEMinCodecBitrate_bps, KBPS(300), KBPS(1300)}, // VGA
  {MB_OF(176, 144), kViEMinCodecBitrate_bps, KBPS(150), KBPS(500)}, // WQVGA, CIF
  {0 , kViEMinCodecBitrate_bps, KBPS(80), KBPS(250)} // QCIF and below
      // clang-format on
  };

  // max bandwidth should be proportional (not linearly!) to resolution, and
  // proportional (perhaps linearly, or close) to current frame rate.
  int fs = MB_OF(aSize.width, aSize.height);

  for (const auto& resAndLimits : kResolutionAndBitrateLimits) {
    if (fs >= resAndLimits.resolution_in_mb &&
        // pick the highest range where at least start rate is within cap
        // (or if we're at the end of the array).
        (aCapBps == 0 || resAndLimits.start_bitrate_bps <= aCapBps ||
         resAndLimits.resolution_in_mb == 0)) {
      return resAndLimits;
    }
  }

  MOZ_CRASH("Loop should have handled fallback");
}

auto VideoStreamFactory::GetBitrateLimitsFor(
    const webrtc::FieldTrialsView& aFieldTrials,
    const webrtc::VideoEncoderConfig& aConfig, bool aTemporalLayersSupported,
    const std::vector<webrtc::VideoStream>& aStreams)
    -> AutoTArray<ResolutionAndBitrateLimits, webrtc::kMaxSimulcastStreams> {
  AutoTArray<ResolutionAndBitrateLimits, webrtc::kMaxSimulcastStreams> limits;

  if (aStreams.size() <= 1) {
    if (!aStreams.empty()) {
      limits.AppendElement(GetUnicastLimitsFor(
          gfx::IntSize(SaturatingCast<int>(aStreams[0].width),
                       SaturatingCast<int>(aStreams[0].height))));
    }
    return limits;
  }

  std::vector<webrtc::Resolution> resolutions;
  resolutions.reserve(aStreams.size());
  for (const auto& stream : aStreams) {
    resolutions.push_back(webrtc::Resolution{
        SaturatingCast<int>(stream.width), SaturatingCast<int>(stream.height)});
  }
  const std::vector<webrtc::VideoStream> layers = webrtc::GetSimulcastConfig(
      resolutions, /* is_screenshare_with_conference_mode= */ false,
      aTemporalLayersSupported, aFieldTrials, aConfig.codec_type);
  MOZ_ASSERT(layers.size() == aStreams.size());
  for (const auto& layer : layers) {
    limits.AppendElement(ResolutionAndBitrateLimits{
        SaturatingCast<int>(MB_OF(layer.width, layer.height)),
        layer.min_bitrate_bps, layer.target_bitrate_bps,
        layer.max_bitrate_bps});
  }

  // Like libwebrtc's EncoderStreamFactory, let the lowest active layer go down
  // to the minimum video bitrate. Otherwise e.g. a lone HD layer would be
  // forced to its high minimum.
  Maybe<ResolutionAndBitrateLimits&> lowestActive;
  for (size_t i = 0; i < limits.Length(); ++i) {
    if (aStreams[i].active &&
        (lowestActive.isNothing() ||
         limits[i].max_bitrate_bps < lowestActive->max_bitrate_bps)) {
      lowestActive = SomeRef(limits[i]);
    }
  }
  if (lowestActive) {
    lowestActive->min_bitrate_bps = kViEMinCodecBitrate_bps;
  }
  return limits;
}

/**
 * Function to set the encoding bitrate limits based on incoming frame size and
 * rate
 * @param defaults: limits to start from, for the resolution being sent
 * @param min: minimum bitrate in bps
 * @param start: bitrate in bps that the encoder should start with
 * @param cap: user-enforced max bitrate, or 0
 * @param pref_cap: cap enforced by prefs
 * @param negotiated_cap: cap negotiated through SDP
 * @param aVideoStream stream to apply bitrates to
 */
static void SelectBitrates(
    const VideoStreamFactory::ResolutionAndBitrateLimits& defaults, int min,
    int start, int cap, int pref_cap, int negotiated_cap,
    webrtc::VideoStream& aVideoStream) {
  int& out_min = aVideoStream.min_bitrate_bps;
  int& out_start = aVideoStream.target_bitrate_bps;
  int& out_max = aVideoStream.max_bitrate_bps;

  out_min = MinIgnoreZero(defaults.min_bitrate_bps, cap);
  out_start = MinIgnoreZero(defaults.start_bitrate_bps, cap);
  out_max = MinIgnoreZero(defaults.max_bitrate_bps, cap);

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
  out_start = std::clamp(out_start, out_min, out_max);

  MOZ_ASSERT(pref_cap == 0 || out_max <= pref_cap);
}

void VideoStreamFactory::SelectResolutionAndMaxFramerate(
    gfx::IntSize aSize, const VideoCodecConfig::Encoding& aEncoding,
    webrtc::VideoStream& aVideoStream) {
  MOZ_ASSERT(aSize.width > 0);
  MOZ_ASSERT(aSize.height > 0);
  MOZ_ASSERT(aEncoding.constraints.scaleDownBy >= 1.0);
  gfx::IntSize newSize(0, 0);

  newSize = CalculateScaledResolution(aSize, aEncoding.constraints.scaleDownBy);

  if (newSize.width == 0 || newSize.height == 0) {
    aVideoStream.width = aVideoStream.height = 0;
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
  SelectMaxFramerateForAllStreams(newSize);

  CSFLogInfo(LOGTAG, "%s Input frame %ux%u, RID %s scaling to %zux%zu",
             __FUNCTION__, aSize.width, aSize.height, aEncoding.rid.c_str(),
             aVideoStream.width, aVideoStream.height);

  // mMaxFramerateForAllStreams is based on codec-wide stuff like fmtp, and
  // hard-coded limits based on the source resolution.
  // mCodecConfig.mEncodingConstraints.maxFps does not take the hard-coded
  // limits into account, so we have mMaxFramerateForAllStreams which
  // incorporates those. Per-encoding max framerate is based on parameters
  // from JS, and maybe rid
  unsigned int max_framerate = SelectFrameRate(
      mMaxFramerateForAllStreams, {aVideoStream.width, aVideoStream.height});
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
    const webrtc::FieldTrialsView& field_trials, int aWidth, int aHeight,
    const webrtc::VideoEncoderConfig& aConfig) {
  mEncodeQueue->AssertOnCurrentThread();
  const size_t streamCount = aConfig.number_of_streams;

  MOZ_RELEASE_ASSERT(streamCount >= 1, "Should request at least one stream");
  MOZ_RELEASE_ASSERT(streamCount <= aConfig.simulcast_layers.size());
  MOZ_RELEASE_ASSERT(streamCount <= mCodecConfig.mEncodings.size());

  bool temporalLayersSupported = true;
  if (streamCount > 1 && mCodecConfig.mName == "H264") {
#ifdef ANDROID
    temporalLayersSupported = false;
#else
    temporalLayersSupported =
        HaveGMPFor("encode-video"_ns, {"moz-h264-temporal-svc"_ns});
#endif
  }

  std::vector<webrtc::VideoStream> streams;
  streams.reserve(streamCount);

  for (size_t idx = 0; idx < streamCount; ++idx) {
    webrtc::VideoStream video_stream = aConfig.simulcast_layers[idx];
    const auto& encoding = mCodecConfig.mEncodings[idx];
    MOZ_ASSERT(video_stream.active == encoding.active);

    if (streamCount > 1) {
      // Simulcast uses two temporal layers where the codec supports them.
      video_stream.num_temporal_layers = temporalLayersSupported ? 2 : 1;
      // XXX Bug 1390215 investigate using more of
      // simulcast.cc:GetSimulcastConfig() or our own algorithm to replace it
    }

    SelectResolutionAndMaxFramerate({aWidth, aHeight}, encoding, video_stream);

    CSFLogInfo(
        LOGTAG,
        "%s Stream %zu with RID %s scaling %dx%d->%zux%zu; scaleDownBy=%.2f).",
        __FUNCTION__, idx, encoding.rid.c_str(), aWidth, aHeight,
        video_stream.width, video_stream.height,
        encoding.constraints.scaleDownBy);

    if (video_stream.width == 0 || video_stream.height == 0) {
      CSFLogInfo(LOGTAG, "%s Stream with RID %s ignored: has no resolution.",
                 __FUNCTION__, encoding.rid.c_str());
    }

    streams.push_back(video_stream);
  }

  const auto bitrateLimits = GetBitrateLimitsFor(
      field_trials, aConfig, temporalLayersSupported, streams);

  for (size_t idx = 0; idx < streamCount; ++idx) {
    webrtc::VideoStream& video_stream = streams[idx];
    const auto& encoding = mCodecConfig.mEncodings[idx];

    SelectBitrates(bitrateLimits[idx], mMinBitrate, mStartBitrate,
                   SaturatingCast<int>(encoding.constraints.maxBr),
                   mPrefMaxBitrate, mNegotiatedMaxBitrate, video_stream);

    CSFLogInfo(LOGTAG,
               "%s Stream with RID %s maxFps=%d (global max fps = %u), "
               "bitrate=[%dkbps, %dkbps, %dkbps]",
               __FUNCTION__, encoding.rid.c_str(), video_stream.max_framerate,
               (unsigned)mMaxFramerateForAllStreams,
               video_stream.min_bitrate_bps / 1000,
               video_stream.target_bitrate_bps / 1000,
               video_stream.max_bitrate_bps / 1000);

    video_stream.bitrate_priority = aConfig.bitrate_priority;
    video_stream.max_qp = kQpMax;
  }

  MOZ_RELEASE_ASSERT(streams.size(), "Should configure at least one stream");
  return streams;
}

void VideoStreamFactory::SetEncoderInfo(
    const webrtc::VideoEncoder::EncoderInfo& aInfo) {
  if (!mEncodeQueue) {
    mEncodeQueue = Nothing();
    mEncodeQueue.emplace(GetCurrentSerialEventTarget());
  }
  mEncodeQueue->AssertOnCurrentThread();
  mRequestedResolutionAlignment =
      Some(SaturatingCast<int>(aInfo.requested_resolution_alignment));
}

gfx::IntSize VideoStreamFactory::CalculateScaledResolution(
    gfx::IntSize aSize, double aScaleDownByResolution) {
  mEncodeQueue->AssertOnCurrentThread();
  // If any adjustments like scaleResolutionDownBy or maxFS are being given
  // we want to choose a height and width here to provide for more variety
  // in possible resolutions.
  int width = aSize.width;
  int height = aSize.height;

  if (aScaleDownByResolution > 1) {
    width = static_cast<int>(aSize.width / aScaleDownByResolution);
    height = static_cast<int>(aSize.height / aScaleDownByResolution);
  }

  // Check if we still need to adjust resolution down more due to other
  // constraints.
  if (mCodecConfig.mEncodingConstraints.maxFs > 0) {
    auto currentFs = static_cast<unsigned int>(width * height);
    auto maxFs = mCodecConfig.mEncodingConstraints.maxFs * 16 * 16;

    // If our currentFs is greater than maxFs we calculate a width and height
    // that will get as close as possible to maxFs and try to maintain aspect
    // ratio.
    if (currentFs > maxFs) {
      if (aSize.width > aSize.height) {  // Landscape
        auto aspectRatio = static_cast<double>(aSize.width) / aSize.height;

        height = static_cast<int>(std::sqrt(maxFs / aspectRatio));
        width = static_cast<int>(height * aspectRatio);
      } else {  // Portrait
        auto aspectRatio = static_cast<double>(aSize.height) / aSize.width;

        width = static_cast<int>(std::sqrt(maxFs / aspectRatio));
        height = static_cast<int>(width * aspectRatio);
      }
    }
  }

  // Simplest possible adaptation to resolution alignment.
  width -= width % *mRequestedResolutionAlignment;
  height -= height % *mRequestedResolutionAlignment;

  // Guard against a negative size.
  const int minSize = 0;
  if (width < minSize || height < minSize) {
    width = minSize;
    height = minSize;
  }

  return gfx::IntSize(width, height);
}

void VideoStreamFactory::SelectMaxFramerateForAllStreams(gfx::IntSize aSize) {
  unsigned int framerate_all_streams =
      SelectFrameRate(mMaxFramerateForAllStreams, aSize);
  unsigned int maxFrameRate = mMaxFramerateForAllStreams;
  if (mMaxFramerateForAllStreams != framerate_all_streams) {
    CSFLogDebug(LOGTAG, "%s: framerate changing to %u (from %u)", __FUNCTION__,
                framerate_all_streams, maxFrameRate);
    mMaxFramerateForAllStreams = framerate_all_streams;
  }
}

unsigned int VideoStreamFactory::SelectFrameRate(unsigned int aOldFramerate,
                                                 gfx::IntSize aSize) {
  unsigned int new_framerate = aOldFramerate;

  // Limit frame rate based on max-mbps
  if (mCodecConfig.mEncodingConstraints.maxMbps) {
    unsigned int cur_fs, mb_width, mb_height;

    mb_width = (aSize.width + 15) >> 4;
    mb_height = (aSize.height + 15) >> 4;

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

}  // namespace mozilla
