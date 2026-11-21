/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/min_video_bitrate_experiment.h"

#include <string>

#include "api/field_trials_view.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"

namespace webrtc {

const int kDefaultMinVideoBitrateBps = 30000;

namespace {
const char kForcedFallbackFieldTrial[] =
    "WebRTC-VP8-Forced-Fallback-Encoder-v2";
const char kMinVideoBitrateExperiment[] = "WebRTC-Video-MinVideoBitrate";

absl::optional<int> GetFallbackMinBpsFromFieldTrial(
    const FieldTrialsView& field_trials,
    VideoCodecType type) {
  if (type != kVideoCodecVP8) {
    return absl::nullopt;
  }

  if (!field_trials.IsEnabled(kForcedFallbackFieldTrial)) {
    return absl::nullopt;
  }

  const std::string group = field_trials.Lookup(kForcedFallbackFieldTrial);
  if (group.empty()) {
    return absl::nullopt;
  }

  int min_pixels;  // Ignored.
  int max_pixels;  // Ignored.
  int min_bps;
  if (sscanf(group.c_str(), "Enabled-%d,%d,%d", &min_pixels, &max_pixels,
             &min_bps) != 3) {
    return absl::nullopt;
  }

  if (min_bps <= 0) {
    return absl::nullopt;
  }

  return min_bps;
}
}  // namespace

absl::optional<DataRate> GetExperimentalMinVideoBitrate(
    const FieldTrialsView& field_trials,
    VideoCodecType type) {
  const absl::optional<int> fallback_min_bitrate_bps =
      GetFallbackMinBpsFromFieldTrial(field_trials, type);
  if (fallback_min_bitrate_bps) {
    return DataRate::BitsPerSec(*fallback_min_bitrate_bps);
  }

  if (field_trials.IsEnabled(kMinVideoBitrateExperiment)) {
    webrtc::FieldTrialFlag enabled("Enabled");

    // Backwards-compatibility with an old experiment - a generic minimum which,
    // if set, applies to all codecs.
    webrtc::FieldTrialOptional<webrtc::DataRate> min_video_bitrate("br");

    // New experiment - per-codec minimum bitrate.
    webrtc::FieldTrialOptional<webrtc::DataRate> min_bitrate_vp8("vp8_br");
    webrtc::FieldTrialOptional<webrtc::DataRate> min_bitrate_vp9("vp9_br");
    webrtc::FieldTrialOptional<webrtc::DataRate> min_bitrate_av1("av1_br");
    webrtc::FieldTrialOptional<webrtc::DataRate> min_bitrate_h264("h264_br");

    webrtc::ParseFieldTrial(
        {&enabled, &min_video_bitrate, &min_bitrate_vp8, &min_bitrate_vp9,
         &min_bitrate_av1, &min_bitrate_h264},
        field_trials.Lookup(kMinVideoBitrateExperiment));

    if (min_video_bitrate) {
      if (min_bitrate_vp8 || min_bitrate_vp9 || min_bitrate_av1 ||
          min_bitrate_h264) {
        // "br" is mutually-exclusive with the other configuration possibilites.
        RTC_LOG(LS_WARNING) << "Self-contradictory experiment config.";
      }
      return *min_video_bitrate;
    }

    switch (type) {
      case kVideoCodecVP8:
        return min_bitrate_vp8.GetOptional();
      case kVideoCodecH265:
      //  TODO(bugs.webrtc.org/13485): Use VP9 bitrate limits for now.
      case kVideoCodecVP9:
        return min_bitrate_vp9.GetOptional();
      case kVideoCodecAV1:
        return min_bitrate_av1.GetOptional();
      case kVideoCodecH264:
        return min_bitrate_h264.GetOptional();
      case kVideoCodecGeneric:
        return absl::nullopt;
    }

    RTC_DCHECK_NOTREACHED();
  }

  return absl::nullopt;
}

}  // namespace webrtc
