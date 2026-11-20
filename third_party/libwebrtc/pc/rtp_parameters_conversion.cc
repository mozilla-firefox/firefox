/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtp_parameters_conversion.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "rtc_base/logging.h"

namespace webrtc {

std::optional<RtcpFeedback> ToRtcpFeedback(
    const FeedbackParam& cricket_feedback) {
  if (cricket_feedback.id() == kRtcpFbParamCcm) {
    if (cricket_feedback.param() == kRtcpFbCcmParamFir) {
      return RtcpFeedback(RtcpFeedbackType::CCM, RtcpFeedbackMessageType::FIR);
    } else {
      RTC_LOG(LS_WARNING) << "Unsupported parameter for CCM RTCP feedback: "
                          << cricket_feedback.param();
      return std::nullopt;
    }
  } else if (cricket_feedback.id() == kRtcpFbParamNack) {
    if (cricket_feedback.param().empty()) {
      return RtcpFeedback(RtcpFeedbackType::NACK,
                          RtcpFeedbackMessageType::GENERIC_NACK);
    } else if (cricket_feedback.param() == kRtcpFbNackParamPli) {
      return RtcpFeedback(RtcpFeedbackType::NACK, RtcpFeedbackMessageType::PLI);
    } else {
      RTC_LOG(LS_WARNING) << "Unsupported parameter for NACK RTCP feedback: "
                          << cricket_feedback.param();
      return std::nullopt;
    }
  } else if (cricket_feedback.id() == kRtcpFbParamRemb) {
    if (!cricket_feedback.param().empty()) {
      RTC_LOG(LS_WARNING) << "Unsupported parameter for REMB RTCP feedback: "
                          << cricket_feedback.param();
      return std::nullopt;
    } else {
      return RtcpFeedback(RtcpFeedbackType::REMB);
    }
  } else if (cricket_feedback.id() == kRtcpFbParamTransportCc) {
    if (!cricket_feedback.param().empty()) {
      RTC_LOG(LS_WARNING)
          << "Unsupported parameter for transport-cc RTCP feedback: "
          << cricket_feedback.param();
      return std::nullopt;
    } else {
      return RtcpFeedback(RtcpFeedbackType::TRANSPORT_CC);
    }
  }
  RTC_LOG(LS_WARNING) << "Unsupported RTCP feedback type: "
                      << cricket_feedback.id();
  return std::nullopt;
}

RtpCodecCapability ToRtpCodecCapability(const Codec& cricket_codec) {
  RtpCodecCapability codec;
  codec.name = cricket_codec.name;
  codec.kind = cricket_codec.type == Codec::Type::kAudio ? MediaType::AUDIO
                                                         : MediaType::VIDEO;
  codec.clock_rate.emplace(cricket_codec.clockrate);
  codec.preferred_payload_type.emplace(cricket_codec.id.value());
  for (const FeedbackParam& cricket_feedback :
       cricket_codec.feedback_params.params()) {
    std::optional<RtcpFeedback> feedback = ToRtcpFeedback(cricket_feedback);
    if (feedback) {
      codec.rtcp_feedback.push_back(feedback.value());
    }
  }
  switch (cricket_codec.type) {
    case Codec::Type::kAudio:
      codec.num_channels = static_cast<int>(cricket_codec.channels);
      break;
    case Codec::Type::kVideo:
      codec.scalability_modes = cricket_codec.scalability_modes;
      break;
  }
  codec.parameters.insert(cricket_codec.params.begin(),
                          cricket_codec.params.end());
  return codec;
}

RtpCapabilities ToRtpCapabilities(
    std::span<const Codec> cricket_codecs,
    std::span<const RtpHeaderExtensionCapability> extensions) {
  RtpCapabilities capabilities;
  bool have_red = false;
  bool have_ulpfec = false;
  bool have_flexfec = false;
  bool have_rtx = false;
  for (const Codec& cricket_codec : cricket_codecs) {
    if (cricket_codec.name == kRedCodecName) {
      if (have_red) {
        // There should only be one RED codec entry in caps.
        continue;
      }
      have_red = true;
    } else if (cricket_codec.name == kUlpfecCodecName) {
      have_ulpfec = true;
    } else if (cricket_codec.name == kFlexfecCodecName) {
      have_flexfec = true;
    } else if (cricket_codec.name == kRtxCodecName) {
      if (have_rtx) {
        // There should only be one RTX codec entry in caps.
        continue;
      }
      have_rtx = true;
    }
    auto codec_capability = ToRtpCodecCapability(cricket_codec);
    if (cricket_codec.name == kRtxCodecName ||
        cricket_codec.name == kRedCodecName) {
      // For RTX this removes the APT which points to a payload type.
      // For RED this removes the redundancy spec which points to a payload
      // type.
      codec_capability.parameters.clear();
    }
    capabilities.codecs.push_back(codec_capability);
  }

  capabilities.header_extensions.assign(extensions.begin(), extensions.end());

  if (have_red) {
    capabilities.fec.push_back(FecMechanism::RED);
  }
  if (have_red && have_ulpfec) {
    capabilities.fec.push_back(FecMechanism::RED_AND_ULPFEC);
  }
  if (have_flexfec) {
    capabilities.fec.push_back(FecMechanism::FLEXFEC);
  }
  return capabilities;
}

}  // namespace webrtc
