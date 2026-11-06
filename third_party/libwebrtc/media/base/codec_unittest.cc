/*
 *  Copyright (c) 2009 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/codec.h"

#include <tuple>

#include "api/video_codecs/av1_profile.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/vp9_profile.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "rtc_base/gunit.h"

using cricket::AudioCodec;
using cricket::Codec;
using cricket::FeedbackParam;
using cricket::kCodecParamAssociatedPayloadType;
using cricket::kCodecParamMaxBitrate;
using cricket::kCodecParamMinBitrate;
using cricket::VideoCodec;

class TestCodec : public Codec {
 public:
  TestCodec(int id, const std::string& name, int clockrate)
      : Codec(Type::kAudio, id, name, clockrate) {}
  TestCodec() : Codec(Type::kAudio) {}
  TestCodec(const TestCodec& c) = default;
  TestCodec& operator=(const TestCodec& c) = default;
};

TEST(CodecTest, TestCodecOperators) {
  TestCodec c0(96, "D", 1000);
  c0.SetParam("a", 1);

  TestCodec c1 = c0;
  EXPECT_TRUE(c1 == c0);

  int param_value0;
  int param_value1;
  EXPECT_TRUE(c0.GetParam("a", &param_value0));
  EXPECT_TRUE(c1.GetParam("a", &param_value1));
  EXPECT_EQ(param_value0, param_value1);

  c1.id = 86;
  EXPECT_TRUE(c0 != c1);

  c1 = c0;
  c1.name = "x";
  EXPECT_TRUE(c0 != c1);

  c1 = c0;
  c1.clockrate = 2000;
  EXPECT_TRUE(c0 != c1);

  c1 = c0;
  c1.SetParam("a", 2);
  EXPECT_TRUE(c0 != c1);

  TestCodec c5;
  TestCodec c6(0, "", 0);
  EXPECT_TRUE(c5 == c6);
}

TEST(CodecTest, TestAudioCodecOperators) {
  AudioCodec c0 = cricket::CreateAudioCodec(96, "A", 44100, 2);
  AudioCodec c1 = cricket::CreateAudioCodec(95, "A", 44100, 2);
  AudioCodec c2 = cricket::CreateAudioCodec(96, "x", 44100, 2);
  AudioCodec c3 = cricket::CreateAudioCodec(96, "A", 48000, 2);
  AudioCodec c4 = cricket::CreateAudioCodec(96, "A", 44100, 2);
  c4.bitrate = 10000;
  AudioCodec c5 = cricket::CreateAudioCodec(96, "A", 44100, 1);
  EXPECT_NE(c0, c1);
  EXPECT_NE(c0, c2);
  EXPECT_NE(c0, c3);
  EXPECT_NE(c0, c4);
  EXPECT_NE(c0, c5);

  AudioCodec c8 = cricket::CreateAudioCodec(0, "", 0, 0);
  AudioCodec c9 = c0;
  EXPECT_EQ(c9, c0);

  AudioCodec c10(c0);
  AudioCodec c11(c0);
  AudioCodec c12(c0);
  AudioCodec c13(c0);
  c10.params["x"] = "abc";
  c11.params["x"] = "def";
  c12.params["y"] = "abc";
  c13.params["x"] = "abc";
  EXPECT_NE(c10, c0);
  EXPECT_NE(c11, c0);
  EXPECT_NE(c11, c10);
  EXPECT_NE(c12, c0);
  EXPECT_NE(c12, c10);
  EXPECT_NE(c12, c11);
  EXPECT_EQ(c13, c10);
}

TEST(CodecTest, TestAudioCodecMatches) {
  // Test a codec with a static payload type.
  AudioCodec c0 = cricket::CreateAudioCodec(34, "A", 44100, 1);
  EXPECT_TRUE(c0.Matches(cricket::CreateAudioCodec(34, "", 44100, 1)));
  EXPECT_TRUE(c0.Matches(cricket::CreateAudioCodec(34, "", 44100, 0)));
  EXPECT_TRUE(c0.Matches(cricket::CreateAudioCodec(34, "", 44100, 0)));
  EXPECT_TRUE(c0.Matches(cricket::CreateAudioCodec(34, "", 0, 0)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(96, "A", 44100, 1)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(96, "", 44100, 1)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(95, "", 55100, 1)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(95, "", 44100, 1)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(95, "", 44100, 2)));
  EXPECT_FALSE(c0.Matches(cricket::CreateAudioCodec(95, "", 55100, 2)));

  // Test a codec with a dynamic payload type.
  AudioCodec c1 = cricket::CreateAudioCodec(96, "A", 44100, 1);
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(96, "A", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(97, "A", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(96, "a", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(97, "a", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(35, "a", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(42, "a", 0, 0)));
  EXPECT_TRUE(c1.Matches(cricket::CreateAudioCodec(65, "a", 0, 0)));
  EXPECT_FALSE(c1.Matches(cricket::CreateAudioCodec(95, "A", 0, 0)));
  EXPECT_FALSE(c1.Matches(cricket::CreateAudioCodec(34, "A", 0, 0)));
  EXPECT_FALSE(c1.Matches(cricket::CreateAudioCodec(96, "", 44100, 2)));
  EXPECT_FALSE(c1.Matches(cricket::CreateAudioCodec(96, "A", 55100, 1)));

  // Test a codec with a dynamic payload type, and auto bitrate.
  AudioCodec c2 = cricket::CreateAudioCodec(97, "A", 16000, 1);
  // Use default bitrate.
  EXPECT_TRUE(c2.Matches(cricket::CreateAudioCodec(97, "A", 16000, 1)));
  EXPECT_TRUE(c2.Matches(cricket::CreateAudioCodec(97, "A", 16000, 0)));
  // Use explicit bitrate.
  EXPECT_TRUE(c2.Matches(cricket::CreateAudioCodec(97, "A", 16000, 1)));
  // Backward compatibility with clients that might send "-1" (for default).
  EXPECT_TRUE(c2.Matches(cricket::CreateAudioCodec(97, "A", 16000, 1)));

  // Stereo doesn't match channels = 0.
  AudioCodec c3 = cricket::CreateAudioCodec(96, "A", 44100, 2);
  EXPECT_TRUE(c3.Matches(cricket::CreateAudioCodec(96, "A", 44100, 2)));
  EXPECT_FALSE(c3.Matches(cricket::CreateAudioCodec(96, "A", 44100, 1)));
  EXPECT_FALSE(c3.Matches(cricket::CreateAudioCodec(96, "A", 44100, 0)));
}

TEST(CodecTest, TestOpusAudioCodecWithDifferentParameters) {
  AudioCodec opus_with_fec = cricket::CreateAudioCodec(96, "opus", 48000, 2);
  opus_with_fec.params["useinbandfec"] = "1";
  AudioCodec opus_without_fec = cricket::CreateAudioCodec(96, "opus", 48000, 2);

  EXPECT_TRUE(opus_with_fec != opus_without_fec);
  // Matches does not compare parameters for audio.
  EXPECT_TRUE(opus_with_fec.Matches(opus_without_fec));

  webrtc::RtpCodecParameters rtp_opus_with_fec =
      opus_with_fec.ToCodecParameters();
  // MatchesRtpCodec takes parameters into account.
  EXPECT_TRUE(opus_with_fec.MatchesRtpCodec(rtp_opus_with_fec));
  EXPECT_FALSE(opus_without_fec.MatchesRtpCodec(rtp_opus_with_fec));
}

TEST(CodecTest, TestVideoCodecOperators) {
  VideoCodec c0 = cricket::CreateVideoCodec(96, "V");
  VideoCodec c1 = cricket::CreateVideoCodec(95, "V");
  VideoCodec c2 = cricket::CreateVideoCodec(96, "x");

  EXPECT_TRUE(c0 != c1);
  EXPECT_TRUE(c0 != c2);

  VideoCodec c8 = cricket::CreateVideoCodec(0, "");
  VideoCodec c9 = c0;
  EXPECT_TRUE(c9 == c0);

  VideoCodec c10(c0);
  VideoCodec c11(c0);
  VideoCodec c12(c0);
  VideoCodec c13(c0);
  c10.params["x"] = "abc";
  c11.params["x"] = "def";
  c12.params["y"] = "abc";
  c13.params["x"] = "abc";
  EXPECT_TRUE(c10 != c0);
  EXPECT_TRUE(c11 != c0);
  EXPECT_TRUE(c11 != c10);
  EXPECT_TRUE(c12 != c0);
  EXPECT_TRUE(c12 != c10);
  EXPECT_TRUE(c12 != c11);
  EXPECT_TRUE(c13 == c10);
}

TEST(CodecTest, TestVideoCodecEqualsWithDifferentPacketization) {
  VideoCodec c0 = cricket::CreateVideoCodec(100, cricket::kVp8CodecName);
  VideoCodec c1 = cricket::CreateVideoCodec(100, cricket::kVp8CodecName);
  VideoCodec c2 = cricket::CreateVideoCodec(100, cricket::kVp8CodecName);
  c2.packetization = "raw";

  EXPECT_EQ(c0, c1);
  EXPECT_NE(c0, c2);
  EXPECT_NE(c2, c0);
  EXPECT_EQ(c2, c2);
}

TEST(CodecTest, TestVideoCodecMatches) {
  // Test a codec with a static payload type.
  VideoCodec c0 = cricket::CreateVideoCodec(34, "V");
  EXPECT_TRUE(c0.Matches(cricket::CreateVideoCodec(34, "")));
  EXPECT_FALSE(c0.Matches(cricket::CreateVideoCodec(96, "")));
  EXPECT_FALSE(c0.Matches(cricket::CreateVideoCodec(96, "V")));

  // Test a codec with a dynamic payload type.
  VideoCodec c1 = cricket::CreateVideoCodec(96, "V");
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(96, "V")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(97, "V")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(96, "v")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(97, "v")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(35, "v")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(42, "v")));
  EXPECT_TRUE(c1.Matches(cricket::CreateVideoCodec(65, "v")));
  EXPECT_FALSE(c1.Matches(cricket::CreateVideoCodec(96, "")));
  EXPECT_FALSE(c1.Matches(cricket::CreateVideoCodec(95, "V")));
  EXPECT_FALSE(c1.Matches(cricket::CreateVideoCodec(34, "V")));
}

TEST(CodecTest, TestVideoCodecMatchesWithDifferentPacketization) {
  VideoCodec c0 = cricket::CreateVideoCodec(100, cricket::kVp8CodecName);
  VideoCodec c1 = cricket::CreateVideoCodec(101, cricket::kVp8CodecName);
  c1.packetization = "raw";

  EXPECT_TRUE(c0.Matches(c1));
  EXPECT_TRUE(c1.Matches(c0));
}

// AV1 codecs compare profile information.
TEST(CodecTest, TestAV1CodecMatches) {
  const char kProfile0[] = "0";
  const char kProfile1[] = "1";
  const char kProfile2[] = "2";

  VideoCodec c_no_profile =
      cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
  VideoCodec c_profile0 = cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
  c_profile0.params[cricket::kAv1FmtpProfile] = kProfile0;
  VideoCodec c_profile1 = cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
  c_profile1.params[cricket::kAv1FmtpProfile] = kProfile1;
  VideoCodec c_profile2 = cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
  c_profile2.params[cricket::kAv1FmtpProfile] = kProfile2;

  // An AV1 entry with no profile specified should be treated as profile-0.
  EXPECT_TRUE(c_profile0.Matches(c_no_profile));

  {
    // Two AV1 entries without a profile specified are treated as duplicates.
    VideoCodec c_no_profile_eq =
        cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
    EXPECT_TRUE(c_no_profile.Matches(c_no_profile_eq));
  }

  {
    // Two AV1 entries with profile 0 specified are treated as duplicates.
    VideoCodec c_profile0_eq =
        cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
    c_profile0_eq.params[cricket::kAv1FmtpProfile] = kProfile0;
    EXPECT_TRUE(c_profile0.Matches(c_profile0_eq));
  }

  {
    // Two AV1 entries with profile 1 specified are treated as duplicates.
    VideoCodec c_profile1_eq =
        cricket::CreateVideoCodec(95, cricket::kAv1CodecName);
    c_profile1_eq.params[cricket::kAv1FmtpProfile] = kProfile1;
    EXPECT_TRUE(c_profile1.Matches(c_profile1_eq));
  }

  // AV1 entries with different profiles (0 and 1) are seen as distinct.
  EXPECT_FALSE(c_profile0.Matches(c_profile1));
  EXPECT_FALSE(c_no_profile.Matches(c_profile1));

  // AV1 entries with different profiles (0 and 2) are seen as distinct.
  EXPECT_FALSE(c_profile0.Matches(c_profile2));
  EXPECT_FALSE(c_no_profile.Matches(c_profile2));
}

// VP9 codecs compare profile information.
TEST(CodecTest, TestVP9CodecMatches) {
  const char kProfile0[] = "0";
  const char kProfile2[] = "2";

  VideoCodec c_no_profile =
      cricket::CreateVideoCodec(95, cricket::kVp9CodecName);
  VideoCodec c_profile0 = cricket::CreateVideoCodec(95, cricket::kVp9CodecName);
  c_profile0.params[webrtc::kVP9FmtpProfileId] = kProfile0;

  EXPECT_TRUE(c_profile0.Matches(c_no_profile));

  {
    VideoCodec c_profile0_eq =
        cricket::CreateVideoCodec(95, cricket::kVp9CodecName);
    c_profile0_eq.params[webrtc::kVP9FmtpProfileId] = kProfile0;
    EXPECT_TRUE(c_profile0.Matches(c_profile0_eq));
  }

  {
    VideoCodec c_profile2 =
        cricket::CreateVideoCodec(95, cricket::kVp9CodecName);
    c_profile2.params[webrtc::kVP9FmtpProfileId] = kProfile2;
    EXPECT_FALSE(c_profile0.Matches(c_profile2));
    EXPECT_FALSE(c_no_profile.Matches(c_profile2));
  }

  {
    VideoCodec c_no_profile_eq =
        cricket::CreateVideoCodec(95, cricket::kVp9CodecName);
    EXPECT_TRUE(c_no_profile.Matches(c_no_profile_eq));
  }
}

// Matching H264 codecs also need to have matching profile-level-id and
// packetization-mode.
TEST(CodecTest, TestH264CodecMatches) {
  const char kProfileLevelId1[] = "42e01f";
  const char kProfileLevelId2[] = "42a01e";
  const char kProfileLevelId3[] = "42e01e";

  VideoCodec pli_1_pm_0 = cricket::CreateVideoCodec(95, "H264");
  pli_1_pm_0.params[cricket::kH264FmtpProfileLevelId] = kProfileLevelId1;
  pli_1_pm_0.params[cricket::kH264FmtpPacketizationMode] = "0";

  {
    VideoCodec pli_1_pm_blank = cricket::CreateVideoCodec(95, "H264");
    pli_1_pm_blank.params[cricket::kH264FmtpProfileLevelId] = kProfileLevelId1;
    pli_1_pm_blank.params.erase(
        pli_1_pm_blank.params.find(cricket::kH264FmtpPacketizationMode));

    // Matches since if packetization-mode is not specified it defaults to "0".
    EXPECT_TRUE(pli_1_pm_0.Matches(pli_1_pm_blank));

    // MatchesRtpCodec does exact comparison of parameters.
    EXPECT_FALSE(
        pli_1_pm_0.MatchesRtpCodec(pli_1_pm_blank.ToCodecParameters()));
  }

  {
    VideoCodec pli_1_pm_1 = cricket::CreateVideoCodec(95, "H264");
    pli_1_pm_1.params[cricket::kH264FmtpProfileLevelId] = kProfileLevelId1;
    pli_1_pm_1.params[cricket::kH264FmtpPacketizationMode] = "1";

    // Does not match since packetization-mode is different.
    EXPECT_FALSE(pli_1_pm_0.Matches(pli_1_pm_1));

    EXPECT_FALSE(pli_1_pm_0.MatchesRtpCodec(pli_1_pm_1.ToCodecParameters()));
  }

  {
    VideoCodec pli_2_pm_0 = cricket::CreateVideoCodec(95, "H264");
    pli_2_pm_0.params[cricket::kH264FmtpProfileLevelId] = kProfileLevelId2;
    pli_2_pm_0.params[cricket::kH264FmtpPacketizationMode] = "0";

    // Does not match since profile-level-id is different.
    EXPECT_FALSE(pli_1_pm_0.Matches(pli_2_pm_0));

    EXPECT_FALSE(pli_1_pm_0.MatchesRtpCodec(pli_2_pm_0.ToCodecParameters()));
  }

  {
    VideoCodec pli_3_pm_0_asym = cricket::CreateVideoCodec(95, "H264");
    pli_3_pm_0_asym.params[cricket::kH264FmtpProfileLevelId] = kProfileLevelId3;
    pli_3_pm_0_asym.params[cricket::kH264FmtpPacketizationMode] = "0";

    // Does match, profile-level-id is different but the level is not compared.
    // and the profile matches.
    EXPECT_TRUE(pli_1_pm_0.Matches(pli_3_pm_0_asym));

    EXPECT_FALSE(
        pli_1_pm_0.MatchesRtpCodec(pli_3_pm_0_asym.ToCodecParameters()));

    //
  }
}

#ifdef RTC_ENABLE_H265
// Matching H.265 codecs should have matching profile/tier/level and tx-mode.
TEST(CodecTest, TestH265CodecMatches) {
  constexpr char kProfile1[] = "1";
  constexpr char kTier1[] = "1";
  constexpr char kLevel3_1[] = "93";
  constexpr char kLevel4[] = "120";
  constexpr char kTxMrst[] = "MRST";

  VideoCodec c_ptl_blank =
      cricket::CreateVideoCodec(95, cricket::kH265CodecName);

  {
    VideoCodec c_profile_1 =
        cricket::CreateVideoCodec(95, cricket::kH265CodecName);
    c_profile_1.params[cricket::kH265FmtpProfileId] = kProfile1;

    // Matches since profile-id unspecified defaults to "1".
    EXPECT_TRUE(c_ptl_blank.Matches(c_profile_1));
  }

  {
    VideoCodec c_tier_flag_1 =
        cricket::CreateVideoCodec(95, cricket::kH265CodecName);
    c_tier_flag_1.params[cricket::kH265FmtpTierFlag] = kTier1;

    // Does not match since profile-space unspecified defaults to "0".
    EXPECT_FALSE(c_ptl_blank.Matches(c_tier_flag_1));
  }

  {
    VideoCodec c_level_id_3_1 =
        cricket::CreateVideoCodec(95, cricket::kH265CodecName);
    c_level_id_3_1.params[cricket::kH265FmtpLevelId] = kLevel3_1;

    // Matches since level-id unspecified defaults to "93".
    EXPECT_TRUE(c_ptl_blank.Matches(c_level_id_3_1));
  }

  {
    VideoCodec c_level_id_4 =
        cricket::CreateVideoCodec(95, cricket::kH265CodecName);
    c_level_id_4.params[cricket::kH265FmtpLevelId] = kLevel4;

    // Does not match since different level-ids are specified.
    EXPECT_FALSE(c_ptl_blank.Matches(c_level_id_4));
  }

  {
    VideoCodec c_tx_mode_mrst =
        cricket::CreateVideoCodec(95, cricket::kH265CodecName);
    c_tx_mode_mrst.params[cricket::kH265FmtpTxMode] = kTxMrst;

    // Does not match since tx-mode implies to "SRST" and must be not specified
    // when it is the only mode supported:
    // https://datatracker.ietf.org/doc/html/draft-ietf-avtcore-hevc-webrtc
    EXPECT_FALSE(c_ptl_blank.Matches(c_tx_mode_mrst));
  }
}
#endif

TEST(CodecTest, TestSetParamGetParamAndRemoveParam) {
  AudioCodec codec = cricket::CreateAudioCodec(0, "foo", 22222, 2);
  codec.SetParam("a", "1");
  codec.SetParam("b", "x");

  int int_value = 0;
  EXPECT_TRUE(codec.GetParam("a", &int_value));
  EXPECT_EQ(1, int_value);
  EXPECT_FALSE(codec.GetParam("b", &int_value));
  EXPECT_FALSE(codec.GetParam("c", &int_value));

  std::string str_value;
  EXPECT_TRUE(codec.GetParam("a", &str_value));
  EXPECT_EQ("1", str_value);
  EXPECT_TRUE(codec.GetParam("b", &str_value));
  EXPECT_EQ("x", str_value);
  EXPECT_FALSE(codec.GetParam("c", &str_value));
  EXPECT_TRUE(codec.RemoveParam("a"));
  EXPECT_FALSE(codec.RemoveParam("c"));
}

TEST(CodecTest, TestIntersectFeedbackParams) {
  const FeedbackParam a1("a", "1");
  const FeedbackParam b2("b", "2");
  const FeedbackParam b3("b", "3");
  const FeedbackParam c3("c", "3");
  TestCodec c1;
  c1.AddFeedbackParam(a1);  // Only match with c2.
  c1.AddFeedbackParam(b2);  // Same param different values.
  c1.AddFeedbackParam(c3);  // Not in c2.
  TestCodec c2;
  c2.AddFeedbackParam(a1);
  c2.AddFeedbackParam(b3);

  c1.IntersectFeedbackParams(c2);
  EXPECT_TRUE(c1.HasFeedbackParam(a1));
  EXPECT_FALSE(c1.HasFeedbackParam(b2));
  EXPECT_FALSE(c1.HasFeedbackParam(c3));
}

TEST(CodecTest, TestGetCodecType) {
  // Codec type comparison should be case insensitive on names.
  const VideoCodec codec = cricket::CreateVideoCodec(96, "V");
  const VideoCodec rtx_codec = cricket::CreateVideoCodec(96, "rTx");
  const VideoCodec ulpfec_codec = cricket::CreateVideoCodec(96, "ulpFeC");
  const VideoCodec flexfec_codec = cricket::CreateVideoCodec(96, "FlExFeC-03");
  const VideoCodec red_codec = cricket::CreateVideoCodec(96, "ReD");
  EXPECT_TRUE(codec.IsMediaCodec());
  EXPECT_EQ(codec.GetResiliencyType(), Codec::ResiliencyType::kNone);
  EXPECT_EQ(rtx_codec.GetResiliencyType(), Codec::ResiliencyType::kRtx);
  EXPECT_EQ(ulpfec_codec.GetResiliencyType(), Codec::ResiliencyType::kUlpfec);
  EXPECT_EQ(flexfec_codec.GetResiliencyType(), Codec::ResiliencyType::kFlexfec);
  EXPECT_EQ(red_codec.GetResiliencyType(), Codec::ResiliencyType::kRed);
}

TEST(CodecTest, TestCreateRtxCodec) {
  const VideoCodec rtx_codec = cricket::CreateVideoRtxCodec(96, 120);
  EXPECT_EQ(96, rtx_codec.id);
  EXPECT_EQ(rtx_codec.GetResiliencyType(), Codec::ResiliencyType::kRtx);
  int associated_payload_type;
  ASSERT_TRUE(rtx_codec.GetParam(kCodecParamAssociatedPayloadType,
                                 &associated_payload_type));
  EXPECT_EQ(120, associated_payload_type);
}

TEST(CodecTest, TestMatchesRtpCodecRtx) {
  const VideoCodec rtx_codec_1 = cricket::CreateVideoRtxCodec(96, 120);
  const VideoCodec rtx_codec_2 = cricket::CreateVideoRtxCodec(96, 121);
  EXPECT_TRUE(rtx_codec_1.Matches(rtx_codec_2));
  // MatchesRtpCodec ignores the different associated payload type (apt) for
  // RTX.
  EXPECT_TRUE(rtx_codec_1.MatchesRtpCodec(rtx_codec_2.ToCodecParameters()));
}

TEST(CodecTest, TestValidateCodecFormat) {
  const VideoCodec codec = cricket::CreateVideoCodec(96, "V");
  ASSERT_TRUE(codec.ValidateCodecFormat());

  // Accept 0-127 as payload types.
  VideoCodec low_payload_type = codec;
  low_payload_type.id = 0;
  VideoCodec high_payload_type = codec;
  high_payload_type.id = 127;
  ASSERT_TRUE(low_payload_type.ValidateCodecFormat());
  EXPECT_TRUE(high_payload_type.ValidateCodecFormat());

  // Reject negative payloads.
  VideoCodec negative_payload_type = codec;
  negative_payload_type.id = -1;
  EXPECT_FALSE(negative_payload_type.ValidateCodecFormat());

  // Reject too-high payloads.
  VideoCodec too_high_payload_type = codec;
  too_high_payload_type.id = 128;
  EXPECT_FALSE(too_high_payload_type.ValidateCodecFormat());

  // Reject codecs with min bitrate > max bitrate.
  VideoCodec incorrect_bitrates = codec;
  incorrect_bitrates.params[kCodecParamMinBitrate] = "100";
  incorrect_bitrates.params[kCodecParamMaxBitrate] = "80";
  EXPECT_FALSE(incorrect_bitrates.ValidateCodecFormat());

  // Accept min bitrate == max bitrate.
  VideoCodec equal_bitrates = codec;
  equal_bitrates.params[kCodecParamMinBitrate] = "100";
  equal_bitrates.params[kCodecParamMaxBitrate] = "100";
  EXPECT_TRUE(equal_bitrates.ValidateCodecFormat());

  // Accept min bitrate < max bitrate.
  VideoCodec different_bitrates = codec;
  different_bitrates.params[kCodecParamMinBitrate] = "99";
  different_bitrates.params[kCodecParamMaxBitrate] = "100";
  EXPECT_TRUE(different_bitrates.ValidateCodecFormat());
}

TEST(CodecTest, TestToCodecParameters) {
  VideoCodec v = cricket::CreateVideoCodec(96, "V");
  v.SetParam("p1", "v1");
  webrtc::RtpCodecParameters codec_params_1 = v.ToCodecParameters();
  EXPECT_EQ(96, codec_params_1.payload_type);
  EXPECT_EQ(cricket::MEDIA_TYPE_VIDEO, codec_params_1.kind);
  EXPECT_EQ("V", codec_params_1.name);
  EXPECT_EQ(cricket::kVideoCodecClockrate, codec_params_1.clock_rate);
  EXPECT_EQ(absl::nullopt, codec_params_1.num_channels);
  ASSERT_EQ(1u, codec_params_1.parameters.size());
  EXPECT_EQ("p1", codec_params_1.parameters.begin()->first);
  EXPECT_EQ("v1", codec_params_1.parameters.begin()->second);

  AudioCodec a = cricket::CreateAudioCodec(97, "A", 44100, 2);
  a.SetParam("p1", "a1");
  webrtc::RtpCodecParameters codec_params_2 = a.ToCodecParameters();
  EXPECT_EQ(97, codec_params_2.payload_type);
  EXPECT_EQ(cricket::MEDIA_TYPE_AUDIO, codec_params_2.kind);
  EXPECT_EQ("A", codec_params_2.name);
  EXPECT_EQ(44100, codec_params_2.clock_rate);
  EXPECT_EQ(2, codec_params_2.num_channels);
  ASSERT_EQ(1u, codec_params_2.parameters.size());
  EXPECT_EQ("p1", codec_params_2.parameters.begin()->first);
  EXPECT_EQ("a1", codec_params_2.parameters.begin()->second);
}

TEST(CodecTest, H264CostrainedBaselineIsAddedIfH264IsSupported) {
  const std::vector<webrtc::SdpVideoFormat> kExplicitlySupportedFormats = {
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                               webrtc::H264Level::kLevel3_1, "1"),
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                               webrtc::H264Level::kLevel3_1, "0")};

  std::vector<webrtc::SdpVideoFormat> supported_formats =
      kExplicitlySupportedFormats;
  cricket::AddH264ConstrainedBaselineProfileToSupportedFormats(
      &supported_formats);

  const webrtc::SdpVideoFormat kH264ConstrainedBasedlinePacketization1 =
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                               webrtc::H264Level::kLevel3_1, "1");
  const webrtc::SdpVideoFormat kH264ConstrainedBasedlinePacketization0 =
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                               webrtc::H264Level::kLevel3_1, "0");

  EXPECT_EQ(supported_formats[0], kExplicitlySupportedFormats[0]);
  EXPECT_EQ(supported_formats[1], kExplicitlySupportedFormats[1]);
  EXPECT_EQ(supported_formats[2], kH264ConstrainedBasedlinePacketization1);
  EXPECT_EQ(supported_formats[3], kH264ConstrainedBasedlinePacketization0);
}

TEST(CodecTest, H264CostrainedBaselineIsNotAddedIfH264IsUnsupported) {
  const std::vector<webrtc::SdpVideoFormat> kExplicitlySupportedFormats = {
      {cricket::kVp9CodecName,
       {{webrtc::kVP9FmtpProfileId,
         VP9ProfileToString(webrtc::VP9Profile::kProfile0)}}}};

  std::vector<webrtc::SdpVideoFormat> supported_formats =
      kExplicitlySupportedFormats;
  cricket::AddH264ConstrainedBaselineProfileToSupportedFormats(
      &supported_formats);

  EXPECT_EQ(supported_formats[0], kExplicitlySupportedFormats[0]);
  EXPECT_EQ(supported_formats.size(), kExplicitlySupportedFormats.size());
}

TEST(CodecTest, H264CostrainedBaselineNotAddedIfAlreadySpecified) {
  const std::vector<webrtc::SdpVideoFormat> kExplicitlySupportedFormats = {
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                               webrtc::H264Level::kLevel3_1, "1"),
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                               webrtc::H264Level::kLevel3_1, "0"),
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                               webrtc::H264Level::kLevel3_1, "1"),
      webrtc::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                               webrtc::H264Level::kLevel3_1, "0")};

  std::vector<webrtc::SdpVideoFormat> supported_formats =
      kExplicitlySupportedFormats;
  cricket::AddH264ConstrainedBaselineProfileToSupportedFormats(
      &supported_formats);

  EXPECT_EQ(supported_formats[0], kExplicitlySupportedFormats[0]);
  EXPECT_EQ(supported_formats[1], kExplicitlySupportedFormats[1]);
  EXPECT_EQ(supported_formats[2], kExplicitlySupportedFormats[2]);
  EXPECT_EQ(supported_formats[3], kExplicitlySupportedFormats[3]);
  EXPECT_EQ(supported_formats.size(), kExplicitlySupportedFormats.size());
}
