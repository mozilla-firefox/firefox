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
#include <string>
#include <vector>

#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "test/gmock.h"
#include "test/gtest.h"

using ::testing::UnorderedElementsAre;

namespace webrtc {

TEST(RtpParametersConversionTest, ToRtcpFeedback) {
  std::optional<RtcpFeedback> result = ToRtcpFeedback({"ccm", "fir"});
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::CCM, RtcpFeedbackMessageType::FIR),
            *result);

  result = ToRtcpFeedback(FeedbackParam("nack"));
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::NACK,
                         RtcpFeedbackMessageType::GENERIC_NACK),
            *result);

  result = ToRtcpFeedback({"nack", "pli"});
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::NACK, RtcpFeedbackMessageType::PLI),
            *result);

  result = ToRtcpFeedback(FeedbackParam("goog-remb"));
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::REMB), *result);

  result = ToRtcpFeedback(FeedbackParam("transport-cc"));
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::TRANSPORT_CC), *result);
}

TEST(RtpParametersConversionTest, ToRtcpFeedbackErrors) {
  // CCM with missing or invalid message type.
  std::optional<RtcpFeedback> result = ToRtcpFeedback({"ccm", "pli"});
  EXPECT_FALSE(result);

  result = ToRtcpFeedback(FeedbackParam("ccm"));
  EXPECT_FALSE(result);

  // NACK with missing or invalid message type.
  result = ToRtcpFeedback({"nack", "fir"});
  EXPECT_FALSE(result);

  // REMB with message type (should be left empty).
  result = ToRtcpFeedback({"goog-remb", "pli"});
  EXPECT_FALSE(result);

  // TRANSPORT_CC with message type (should be left empty).
  result = ToRtcpFeedback({"transport-cc", "fir"});
  EXPECT_FALSE(result);

  // Unknown message type.
  result = ToRtcpFeedback(FeedbackParam("foo"));
  EXPECT_FALSE(result);
}

TEST(RtpParametersConversionTest, ToAudioRtpCodecCapability) {
  Codec cricket_codec = CreateAudioCodec(50, "foo", 22222, 4);
  cricket_codec.params["foo"] = "bar";
  cricket_codec.feedback_params.Add(FeedbackParam("transport-cc"));
  RtpCodecCapability codec = ToRtpCodecCapability(cricket_codec);

  EXPECT_EQ("foo", codec.name);
  EXPECT_EQ(MediaType::AUDIO, codec.kind);
  EXPECT_EQ(50, codec.preferred_payload_type);
  EXPECT_EQ(22222, codec.clock_rate);
  EXPECT_EQ(4, codec.num_channels);
  ASSERT_EQ(1u, codec.parameters.size());
  EXPECT_EQ("bar", codec.parameters["foo"]);
  EXPECT_EQ(1u, codec.rtcp_feedback.size());
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::TRANSPORT_CC),
            codec.rtcp_feedback[0]);
}

TEST(RtpParametersConversionTest, ToVideoRtpCodecCapability) {
  Codec cricket_codec = CreateVideoCodec(101, "VID");
  cricket_codec.clockrate = 80000;
  cricket_codec.params["foo"] = "bar";
  cricket_codec.params["ANOTHER"] = "param";
  cricket_codec.feedback_params.Add(FeedbackParam("transport-cc"));
  cricket_codec.feedback_params.Add({"nack", "pli"});
  RtpCodecCapability codec = ToRtpCodecCapability(cricket_codec);

  EXPECT_EQ("VID", codec.name);
  EXPECT_EQ(MediaType::VIDEO, codec.kind);
  EXPECT_EQ(101, codec.preferred_payload_type);
  EXPECT_EQ(80000, codec.clock_rate);
  ASSERT_EQ(2u, codec.parameters.size());
  EXPECT_EQ("bar", codec.parameters["foo"]);
  EXPECT_EQ("param", codec.parameters["ANOTHER"]);
  EXPECT_EQ(2u, codec.rtcp_feedback.size());
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::TRANSPORT_CC),
            codec.rtcp_feedback[0]);
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::NACK, RtcpFeedbackMessageType::PLI),
            codec.rtcp_feedback[1]);
}

// An unknown feedback param should just be ignored.
TEST(RtpParametersConversionTest, ToRtpCodecCapabilityUnknownFeedbackParam) {
  Codec cricket_codec = CreateAudioCodec(50, "foo", 22222, 4);
  cricket_codec.params["foo"] = "bar";
  cricket_codec.feedback_params.Add({"unknown", "param"});
  cricket_codec.feedback_params.Add(FeedbackParam("transport-cc"));
  RtpCodecCapability codec = ToRtpCodecCapability(cricket_codec);

  ASSERT_EQ(1u, codec.rtcp_feedback.size());
  EXPECT_EQ(RtcpFeedback(RtcpFeedbackType::TRANSPORT_CC),
            codec.rtcp_feedback[0]);
}

// Most of ToRtpCapabilities is tested by ToRtpCodecCapability, but we need to
// test that the result of ToRtpCodecCapability ends up in the result, and that
// the "fec" list is assembled correctly.
TEST(RtpParametersConversionTest, ToRtpCapabilities) {
  Codec vp8 = CreateVideoCodec(101, "VP8");

  Codec red = CreateVideoCodec(102, "red");
  // Note: fmtp not usually done for video-red but we want it filtered.
  red.SetParam(kCodecParamNotInNameValueFormat, "101/101");

  Codec red2 = CreateVideoCodec(127, "red");
  Codec ulpfec = CreateVideoCodec(103, "ulpfec");
  Codec flexfec = CreateVideoCodec(102, "flexfec-03");
  Codec rtx = CreateVideoRtxCodec(104, 101);
  Codec rtx2 = CreateVideoRtxCodec(105, 109);

  RtpCapabilities capabilities = ToRtpCapabilities(
      {{vp8, ulpfec, rtx, rtx2}},
      {{{"uri", RtpHeaderExtensionId(1)}, {"uri2", RtpHeaderExtensionId(3)}}});
  ASSERT_EQ(3u, capabilities.codecs.size());
  EXPECT_EQ("VP8", capabilities.codecs[0].name);
  EXPECT_EQ("ulpfec", capabilities.codecs[1].name);
  EXPECT_EQ("rtx", capabilities.codecs[2].name);
  EXPECT_EQ(0u, capabilities.codecs[2].parameters.size());
  ASSERT_EQ(2u, capabilities.header_extensions.size());
  EXPECT_EQ("uri", capabilities.header_extensions[0].uri);
  EXPECT_EQ(RtpHeaderExtensionId(1),
            capabilities.header_extensions[0].preferred_id);
  EXPECT_EQ("uri2", capabilities.header_extensions[1].uri);
  EXPECT_EQ(RtpHeaderExtensionId(3),
            capabilities.header_extensions[1].preferred_id);
  EXPECT_EQ(0u, capabilities.fec.size());

  capabilities = ToRtpCapabilities({{vp8, red, red2, ulpfec, rtx}}, {});
  EXPECT_EQ(4u, capabilities.codecs.size());
  EXPECT_THAT(
      capabilities.fec,
      UnorderedElementsAre(FecMechanism::RED, FecMechanism::RED_AND_ULPFEC));

  capabilities = ToRtpCapabilities({{vp8, red, flexfec}}, {});
  EXPECT_EQ(3u, capabilities.codecs.size());
  EXPECT_THAT(capabilities.fec,
              UnorderedElementsAre(FecMechanism::RED, FecMechanism::FLEXFEC));
  EXPECT_EQ(capabilities.codecs[1].name, "red");
  EXPECT_TRUE(capabilities.codecs[1].parameters.empty());
}

}  // namespace webrtc
