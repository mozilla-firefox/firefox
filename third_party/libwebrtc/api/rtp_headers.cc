/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/rtp_headers.h"

namespace webrtc {

AudioLevel::AudioLevel() : voice_activity_(false), audio_level_(0) {}

AudioLevel::AudioLevel(bool voice_activity, int audio_level)
    : voice_activity_(voice_activity), audio_level_(audio_level) {
  RTC_CHECK_GE(audio_level, 0);
  RTC_CHECK_LE(audio_level, 127);
}

RTPHeaderExtension::RTPHeaderExtension()
    : hasTransmissionTimeOffset(false),
      transmissionTimeOffset(0),
      hasAbsoluteSendTime(false),
      absoluteSendTime(0),
      hasTransportSequenceNumber(false),
      transportSequenceNumber(0),
      hasAudioLevel(false),
      voiceActivity(false),
      audioLevel(0),
      hasVideoRotation(false),
      videoRotation(kVideoRotation_0),
      hasVideoContentType(false),
      videoContentType(VideoContentType::UNSPECIFIED),
      has_video_timing(false),
      csrcAudioLevels() {}

RTPHeaderExtension::RTPHeaderExtension(const RTPHeaderExtension& other) =
    default;

RTPHeaderExtension& RTPHeaderExtension::operator=(
    const RTPHeaderExtension& other) = default;

absl::optional<AudioLevel> RTPHeaderExtension::audio_level() const {
  if (!hasAudioLevel) {
    return absl::nullopt;
  }
  return AudioLevel(voiceActivity, audioLevel);
}

void RTPHeaderExtension::set_audio_level(
    absl::optional<AudioLevel> audio_level) {
  if (audio_level) {
    hasAudioLevel = true;
    voiceActivity = audio_level->voice_activity();
    audioLevel = audio_level->level();
  } else {
    hasAudioLevel = false;
  }
}

RTPHeader::RTPHeader()
    : markerBit(false),
      payloadType(0),
      sequenceNumber(0),
      timestamp(0),
      ssrc(0),
      numCSRCs(0),
      arrOfCSRCs(),
      paddingLength(0),
      headerLength(0),
      extension() {}

RTPHeader::RTPHeader(const RTPHeader& other) = default;

RTPHeader& RTPHeader::operator=(const RTPHeader& other) = default;

}  // namespace webrtc
