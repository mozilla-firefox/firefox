/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/internal_decoder_factory.h"

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/video_codecs/av1_profile.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/vp9_profile.h"
#include "media/base/media_constants.h"
#include "system_wrappers/include/field_trial.h"
#include "test/field_trial.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {
using ::testing::Contains;
using ::testing::Field;
using ::testing::Not;

using ::webrtc::field_trial::InitFieldTrialsFromString;

#ifdef RTC_ENABLE_VP9
constexpr bool kVp9Enabled = true;
#else
constexpr bool kVp9Enabled = false;
#endif
#ifdef WEBRTC_USE_H264
constexpr bool kH264Enabled = true;
#else
constexpr bool kH264Enabled = false;
#endif
#ifdef RTC_DAV1D_IN_INTERNAL_DECODER_FACTORY
constexpr bool kDav1dIsIncluded = true;
#else
constexpr bool kDav1dIsIncluded = false;
#endif
constexpr bool kH265Enabled = false;

constexpr VideoDecoderFactory::CodecSupport kSupported = {
    /*is_supported=*/true, /*is_power_efficient=*/false};
constexpr VideoDecoderFactory::CodecSupport kUnsupported = {
    /*is_supported=*/false, /*is_power_efficient=*/false};

MATCHER_P(Support, expected, "") {
  return arg.is_supported == expected.is_supported &&
         arg.is_power_efficient == expected.is_power_efficient;
}

TEST(InternalDecoderFactoryTest, Vp8) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat::VP8());
  EXPECT_TRUE(decoder);
}

TEST(InternalDecoderFactoryTest, Vp9Profile0) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat::VP9Profile0());
  EXPECT_EQ(static_cast<bool>(decoder), kVp9Enabled);
}

TEST(InternalDecoderFactoryTest, Vp9Profile1) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat::VP9Profile1());
  EXPECT_EQ(static_cast<bool>(decoder), kVp9Enabled);
}

TEST(InternalDecoderFactoryTest, H264) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat::H264());
  EXPECT_EQ(static_cast<bool>(decoder), kH264Enabled);
}

TEST(InternalDecoderFactoryTest, Av1Profile0) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  if (kDav1dIsIncluded) {
    EXPECT_THAT(factory.GetSupportedFormats(),
                Contains(Field(&SdpVideoFormat::name, cricket::kAv1CodecName)));
    EXPECT_TRUE(factory.Create(env, SdpVideoFormat::AV1Profile0()));
  } else {
    EXPECT_THAT(
        factory.GetSupportedFormats(),
        Not(Contains(Field(&SdpVideoFormat::name, cricket::kAv1CodecName))));
  }
}

// At current stage since internal H.265 decoder is not implemented,
TEST(InternalDecoderFactoryTest, H265IsNotEnabled) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat(cricket::kH265CodecName));
  EXPECT_EQ(static_cast<bool>(decoder), kH265Enabled);
}

#if defined(RTC_DAV1D_IN_INTERNAL_DECODER_FACTORY)
TEST(InternalDecoderFactoryTest, Av1) {
  InternalDecoderFactory factory;
  EXPECT_THAT(factory.GetSupportedFormats(),
              Contains(Field(&SdpVideoFormat::name, cricket::kAv1CodecName)));
}
#endif

TEST(InternalDecoderFactoryTest, Av1Profile1_Dav1dDecoderTrialEnabled) {
  const Environment env = CreateEnvironment();
  InternalDecoderFactory factory;
  std::unique_ptr<VideoDecoder> decoder =
      factory.Create(env, SdpVideoFormat::AV1Profile1());
  EXPECT_EQ(static_cast<bool>(decoder), kDav1dIsIncluded);
}

TEST(InternalDecoderFactoryTest, QueryCodecSupportNoReferenceScaling) {
  InternalDecoderFactory factory;
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::VP8(),
                                        /*reference_scaling=*/false),
              Support(kSupported));
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::VP9Profile0(),
                                        /*reference_scaling=*/false),
              Support(kVp9Enabled ? kSupported : kUnsupported));
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::VP9Profile1(),
                                        /*reference_scaling=*/false),
              Support(kVp9Enabled ? kSupported : kUnsupported));

#if defined(RTC_DAV1D_IN_INTERNAL_DECODER_FACTORY)
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::AV1Profile0(),
                                        /*reference_scaling=*/false),
              Support(kSupported));
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::AV1Profile1(),
                                        /*reference_scaling=*/false),
              Support(kSupported));

#endif
}

TEST(InternalDecoderFactoryTest, QueryCodecSupportReferenceScaling) {
  InternalDecoderFactory factory;
  // VP9 and AV1 support for spatial layers.
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::VP9Profile0(),
                                        /*reference_scaling=*/true),
              Support(kVp9Enabled ? kSupported : kUnsupported));
#if defined(RTC_DAV1D_IN_INTERNAL_DECODER_FACTORY)
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::AV1Profile0(),
                                        /*reference_scaling=*/true),
              Support(kSupported));
#endif

  // Invalid config even though VP8 and H264 are supported.
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::H264(),
                                        /*reference_scaling=*/true),
              Support(kUnsupported));
  EXPECT_THAT(factory.QueryCodecSupport(SdpVideoFormat::VP8(),
                                        /*reference_scaling=*/true),
              Support(kUnsupported));
}

}  // namespace
}  // namespace webrtc
