/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <memory>
#include <optional>

#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/environment/environment.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::Field;
using ::testing::Optional;
using Config = AudioDecoderOpus::Config;

enum class StereoParam { kUnset, kMono, kStereo };

SdpAudioFormat GetSdpAudioFormat(StereoParam param) {
  SdpAudioFormat format("opus", 48000, 2);
  switch (param) {
    case StereoParam::kUnset:
      // Do nothing.
      break;
    case StereoParam::kMono:
      format.parameters.emplace("stereo", "0");
      break;
    case StereoParam::kStereo:
      format.parameters.emplace("stereo", "1");
      break;
  }
  return format;
}

constexpr int kDefaultNumChannels = 1;
constexpr int kAlternativeNumChannels = 2;

TEST(AudioDecoderOpusTest, SdpToConfigDoesNotSetNumChannels) {
  const std::optional<Config> config =
      AudioDecoderOpus::SdpToConfig(GetSdpAudioFormat(StereoParam::kUnset));

  EXPECT_THAT(config, Optional(Field(&Config::num_channels, std::nullopt)));
}

TEST(AudioDecoderOpusTest, SdpToConfigForcesMono) {
  const std::optional<Config> config =
      AudioDecoderOpus::SdpToConfig(GetSdpAudioFormat(StereoParam::kMono));

  EXPECT_THAT(config, Optional(Field(&Config::num_channels, 1)));
}

TEST(AudioDecoderOpusTest, SdpToConfigForcesStereo) {
  const std::optional<Config> config =
      AudioDecoderOpus::SdpToConfig(GetSdpAudioFormat(StereoParam::kStereo));

  EXPECT_THAT(config, Optional(Field(&Config::num_channels, 2)));
}

TEST(AudioDecoderOpusTest, MakeAudioDecoderForcesDefaultNumChannels) {
  const Environment env = CreateTestEnvironment();
  auto decoder = AudioDecoderOpus::MakeAudioDecoder(
      env, /*config=*/{.num_channels = std::nullopt});

  EXPECT_EQ(decoder->Channels(), static_cast<size_t>(kDefaultNumChannels));
}

TEST(AudioDecoderOpusTest, MakeAudioDecoderCannotForceDefaultNumChannels) {
  const Environment env = CreateTestEnvironment();
  auto decoder = AudioDecoderOpus::MakeAudioDecoder(
      env, /*config=*/{.num_channels = kAlternativeNumChannels});

  EXPECT_EQ(decoder->Channels(), static_cast<size_t>(kAlternativeNumChannels));
}

TEST(AudioDecoderOpusTest, MakeAudioDecoderForcesStereo) {
  const Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-Audio-OpusDecodeStereoByDefault/Enabled/"});
  auto decoder = AudioDecoderOpus::MakeAudioDecoder(
      env,
      /*config=*/{.num_channels = std::nullopt});

  EXPECT_EQ(decoder->Channels(), 2u);
}

TEST(AudioDecoderOpusTest, MakeAudioDecoderCannotForceStereo) {
  const Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-Audio-OpusDecodeStereoByDefault/Enabled/"});
  auto decoder =
      AudioDecoderOpus::MakeAudioDecoder(env, /*config=*/{.num_channels = 1});

  EXPECT_EQ(decoder->Channels(), 1u);
}

}  // namespace
}  // namespace webrtc
