/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <map>
#include <vector>

#include "api/crypto/crypto_options.h"
#include "api/rtp_headers.h"
#include "call/flexfec_receive_stream.h"
#include "call/rtp_config.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "test/call_test.h"
#include "test/gtest.h"
#include "test/null_transport.h"

namespace webrtc {

class ConfigEndToEndTest : public test::CallTest {};

namespace {
void VerifyEmptyNackConfig(const NackConfig& config) {
  EXPECT_EQ(0, config.rtp_history_ms)
      << "Enabling NACK requires rtcp-fb: nack negotiation.";
}

void VerifyEmptyUlpfecConfig(const UlpfecConfig& config) {
  EXPECT_EQ(-1, config.ulpfec_payload_type)
      << "Enabling ULPFEC requires rtpmap: ulpfec negotiation.";
  EXPECT_EQ(-1, config.red_payload_type)
      << "Enabling ULPFEC requires rtpmap: red negotiation.";
  EXPECT_EQ(-1, config.red_rtx_payload_type)
      << "Enabling RTX in ULPFEC requires rtpmap: rtx negotiation.";
}

void VerifyEmptyFlexfecConfig(const RtpConfig::Flexfec& config) {
  EXPECT_EQ(-1, config.payload_type)
      << "Enabling FlexFEC requires rtpmap: flexfec negotiation.";
  EXPECT_EQ(0U, config.ssrc)
      << "Enabling FlexFEC requires ssrc-group: FEC-FR negotiation.";
  EXPECT_TRUE(config.protected_media_ssrcs.empty())
      << "Enabling FlexFEC requires ssrc-group: FEC-FR negotiation.";
}
}  // namespace

TEST_F(ConfigEndToEndTest, VerifyDefaultSendConfigParameters) {
  VideoSendStream::Config default_send_config(nullptr);
  EXPECT_EQ(0, default_send_config.rtp.nack.rtp_history_ms)
      << "Enabling NACK require rtcp-fb: nack negotiation.";
  EXPECT_TRUE(default_send_config.rtp.rtx.ssrcs.empty())
      << "Enabling RTX requires rtpmap: rtx negotiation.";
  EXPECT_TRUE(default_send_config.rtp.extensions.empty())
      << "Enabling RTP extensions require negotiation.";
  EXPECT_EQ(nullptr, default_send_config.frame_encryptor)
      << "Enabling Frame Encryption requires a frame encryptor to be attached";
  EXPECT_FALSE(
      default_send_config.crypto_options.sframe.require_frame_encryption)
      << "Enabling Require Frame Encryption means an encryptor must be "
         "attached";

  VerifyEmptyNackConfig(default_send_config.rtp.nack);
  VerifyEmptyUlpfecConfig(default_send_config.rtp.ulpfec);
  VerifyEmptyFlexfecConfig(default_send_config.rtp.flexfec);
}

TEST_F(ConfigEndToEndTest, VerifyDefaultVideoReceiveConfigParameters) {
  VideoReceiveStreamInterface::Config default_receive_config(nullptr);
  EXPECT_EQ(RtcpMode::kCompound, default_receive_config.rtp.rtcp_mode)
      << "Reduced-size RTCP require rtcp-rsize to be negotiated.";
  EXPECT_FALSE(
      default_receive_config.rtp.rtcp_xr.receiver_reference_time_report)
      << "RTCP XR settings require rtcp-xr to be negotiated.";
  EXPECT_EQ(0U, default_receive_config.rtp.rtx_ssrc)
      << "Enabling RTX requires ssrc-group: FID negotiation";
  EXPECT_TRUE(default_receive_config.rtp.rtx_associated_payload_types.empty())
      << "Enabling RTX requires rtpmap: rtx negotiation.";
  VerifyEmptyNackConfig(default_receive_config.rtp.nack);
  EXPECT_EQ(-1, default_receive_config.rtp.ulpfec_payload_type)
      << "Enabling ULPFEC requires rtpmap: ulpfec negotiation.";
  EXPECT_EQ(-1, default_receive_config.rtp.red_payload_type)
      << "Enabling ULPFEC requires rtpmap: red negotiation.";
  EXPECT_EQ(nullptr, default_receive_config.frame_decryptor)
      << "Enabling Frame Decryption requires a frame decryptor to be attached";
  EXPECT_FALSE(
      default_receive_config.crypto_options.sframe.require_frame_encryption)
      << "Enabling Require Frame Encryption means a decryptor must be attached";
}

TEST_F(ConfigEndToEndTest, VerifyDefaultFlexfecReceiveConfigParameters) {
  test::NullTransport rtcp_send_transport;
  FlexfecReceiveStream::Config default_receive_config(&rtcp_send_transport);
  EXPECT_EQ(-1, default_receive_config.payload_type)
      << "Enabling FlexFEC requires rtpmap: flexfec negotiation.";
  EXPECT_EQ(0U, default_receive_config.remote_ssrc)
      << "Enabling FlexFEC requires ssrc-group: FEC-FR negotiation.";
  EXPECT_TRUE(default_receive_config.protected_media_ssrcs.empty())
      << "Enabling FlexFEC requires ssrc-group: FEC-FR negotiation.";
}

}  // namespace webrtc
