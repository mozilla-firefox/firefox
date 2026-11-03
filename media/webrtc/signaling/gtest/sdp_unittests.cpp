/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "nss.h"
#include "sdp/ParsingResultComparer.h"
#include "sdp/RsdparsaSdpParser.h"
#include "sdp/SdpAttribute.h"
#include "sdp/SdpHelper.h"
#include "sdp/SdpImpl.h"
#include "sdp/SdpMediaSection.h"
#include "sdp/SipccSdpParser.h"
#include "ssl.h"

extern "C" {
#include "sdp_private.h"
#include "sipcc_sdp.h"
}

#ifdef CRLF
#  undef CRLF
#endif
#define CRLF "\r\n"

#define SKIP_TEST_WITH_RUST_PARSER      \
  if (!::testing::get<1>(GetParam())) { \
    return;                             \
  }
#define SKIP_TEST_WITH_SIPCC_PARSER \
  if (ResultsAreFromSipcc()) {      \
    return;                         \
  }

// If you want to see the SDP as it is parsed
// #define DEBUG_DISPLAY_SDP

using namespace mozilla;

namespace test {

class SdpTest : public ::testing::Test {
 public:
  SdpTest() : final_level_(0), sdp_ptr_(nullptr) {}

  ~SdpTest() { sdp_free_description(sdp_ptr_); }

  static void SetUpTestCase() {
    NSS_NoDB_Init(nullptr);
    NSS_SetDomesticPolicy();
  }

  void SetUp() {
    final_level_ = 0;
    sdp_ptr_ = nullptr;
  }

  static void TearDownTestCase() {}

  void ResetSdp() {
    if (!sdp_ptr_) {
      sdp_free_description(sdp_ptr_);
    }

    sdp_media_e supported_media[] = {
        SDP_MEDIA_AUDIO,      SDP_MEDIA_VIDEO,        SDP_MEDIA_APPLICATION,
        SDP_MEDIA_DATA,       SDP_MEDIA_CONTROL,      SDP_MEDIA_NAS_RADIUS,
        SDP_MEDIA_NAS_TACACS, SDP_MEDIA_NAS_DIAMETER, SDP_MEDIA_NAS_L2TP,
        SDP_MEDIA_NAS_LOGIN,  SDP_MEDIA_NAS_NONE,     SDP_MEDIA_IMAGE,
    };

    sdp_conf_options_t* config_p = sdp_init_config();
    unsigned int i;
    for (i = 0; i < sizeof(supported_media) / sizeof(sdp_media_e); i++) {
      sdp_media_supported(config_p, supported_media[i], true);
    }
    sdp_nettype_supported(config_p, SDP_NT_INTERNET, true);
    sdp_addrtype_supported(config_p, SDP_AT_IP4, true);
    sdp_addrtype_supported(config_p, SDP_AT_IP6, true);
    sdp_transport_supported(config_p, SDP_TRANSPORT_RTPSAVPF, true);
    sdp_transport_supported(config_p, SDP_TRANSPORT_UDPTL, true);
    sdp_require_session_name(config_p, false);

    sdp_ptr_ = sdp_init_description(config_p);
    if (!sdp_ptr_) {
      sdp_free_config(config_p);
    }
  }

  void ParseSdp(const std::string& sdp_str) {
    const char* buf = sdp_str.data();
    ResetSdp();
    ASSERT_EQ(sdp_parse(sdp_ptr_, buf, sdp_str.size()), SDP_SUCCESS);
  }

  void InitLocalSdp() {
    ResetSdp();
    ASSERT_EQ(sdp_set_version(sdp_ptr_, 0), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_username(sdp_ptr_, "-"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_sessionid(sdp_ptr_, "132954853"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_version(sdp_ptr_, "0"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_network_type(sdp_ptr_, SDP_NT_INTERNET),
              SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_address_type(sdp_ptr_, SDP_AT_IP4), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_owner_address(sdp_ptr_, "198.51.100.7"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_session_name(sdp_ptr_, "SDP Unit Test"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_time_start(sdp_ptr_, "0"), SDP_SUCCESS);
    ASSERT_EQ(sdp_set_time_stop(sdp_ptr_, "0"), SDP_SUCCESS);
  }

  std::string SerializeSdp() {
    flex_string fs;
    flex_string_init(&fs);
    EXPECT_EQ(sdp_build(sdp_ptr_, &fs), SDP_SUCCESS);
    std::string body(fs.buffer);
    flex_string_free(&fs);
    return body;
  }

  // Returns "level" for new media section
  int AddNewMedia(sdp_media_e /*type*/) {
    final_level_++;
    EXPECT_EQ(sdp_insert_media_line(sdp_ptr_, final_level_), SDP_SUCCESS);
    EXPECT_EQ(sdp_set_conn_nettype(sdp_ptr_, final_level_, SDP_NT_INTERNET),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_set_conn_addrtype(sdp_ptr_, final_level_, SDP_AT_IP4),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_set_conn_address(sdp_ptr_, final_level_, "198.51.100.7"),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_set_media_type(sdp_ptr_, final_level_, SDP_MEDIA_VIDEO),
              SDP_SUCCESS);
    EXPECT_EQ(
        sdp_set_media_transport(sdp_ptr_, final_level_, SDP_TRANSPORT_RTPAVP),
        SDP_SUCCESS);
    EXPECT_EQ(sdp_set_media_portnum(sdp_ptr_, final_level_, 12345, 0),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_add_media_payload_type(sdp_ptr_, final_level_, 120,
                                         SDP_PAYLOAD_NUMERIC),
              SDP_SUCCESS);
    return final_level_;
  }

  uint16_t AddNewRtcpFbAck(int level, sdp_rtcp_fb_ack_type_e type,
                           uint16_t payload = SDP_ALL_PAYLOADS) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(
        sdp_attr_set_rtcp_fb_ack(sdp_ptr_, level, payload, inst_num, type),
        SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewRtcpFbNack(int level, sdp_rtcp_fb_nack_type_e type,
                            uint16_t payload = SDP_ALL_PAYLOADS) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(
        sdp_attr_set_rtcp_fb_nack(sdp_ptr_, level, payload, inst_num, type),
        SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewRtcpFbTrrInt(int level, uint32_t interval,
                              uint16_t payload = SDP_ALL_PAYLOADS) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_rtcp_fb_trr_int(sdp_ptr_, level, payload, inst_num,
                                           interval),
              SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewRtcpFbRemb(int level, uint16_t payload = SDP_ALL_PAYLOADS) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_rtcp_fb_remb(sdp_ptr_, level, payload, inst_num),
              SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewRtcpFbCcm(int level, sdp_rtcp_fb_ccm_type_e type,
                           uint16_t payload = SDP_ALL_PAYLOADS) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_RTCP_FB, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(
        sdp_attr_set_rtcp_fb_ccm(sdp_ptr_, level, payload, inst_num, type),
        SDP_SUCCESS);
    return inst_num;
  }
  uint16_t AddNewExtMap(int level, const char* uri) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_EXTMAP, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_extmap(sdp_ptr_, level, inst_num, uri, inst_num),
              SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewFmtpMaxFs(int level, uint32_t max_fs) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_FMTP, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_payload_type(sdp_ptr_, level, 0, inst_num, 120),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_max_fs(sdp_ptr_, level, 0, inst_num, max_fs),
              SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewFmtpMaxFr(int level, uint32_t max_fr) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_FMTP, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_payload_type(sdp_ptr_, level, 0, inst_num, 120),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_max_fr(sdp_ptr_, level, 0, inst_num, max_fr),
              SDP_SUCCESS);
    return inst_num;
  }

  uint16_t AddNewFmtpMaxFsFr(int level, uint32_t max_fs, uint32_t max_fr) {
    uint16_t inst_num = 0;
    EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, level, 0, SDP_ATTR_FMTP, &inst_num),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_payload_type(sdp_ptr_, level, 0, inst_num, 120),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_max_fs(sdp_ptr_, level, 0, inst_num, max_fs),
              SDP_SUCCESS);
    EXPECT_EQ(sdp_attr_set_fmtp_max_fr(sdp_ptr_, level, 0, inst_num, max_fr),
              SDP_SUCCESS);
    return inst_num;
  }

 protected:
  int final_level_;
  sdp_t* sdp_ptr_;
};

MOZ_RUNINIT static const std::string kVideoSdp =
    "v=0\r\n"
    "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
    "s=SIP Call\r\n"
    "c=IN IP4 198.51.100.7\r\n"
    "t=0 0\r\n"
    "m=video 56436 RTP/SAVPF 120\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

TEST_F(SdpTest, parseRtcpFbAckRpsi) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack rpsi\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_RPSI);
}

TEST_F(SdpTest, parseRtcpFbAckApp) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack app\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_ACK_APP);
}

TEST_F(SdpTest, parseRtcpFbAckAppFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack app foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_ACK_APP);
}

TEST_F(SdpTest, parseRtcpFbAckFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack foo bar\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbAckFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ack foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbNack) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_BASIC);
}

TEST_F(SdpTest, parseRtcpFbNackPli) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack pli\r\n");
}

TEST_F(SdpTest, parseRtcpFbNackSli) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack sli\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_SLI);
}

TEST_F(SdpTest, parseRtcpFbNackRpsi) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack rpsi\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_RPSI);
}

TEST_F(SdpTest, parseRtcpFbNackApp) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackAppFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackAppFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack app foo bar\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_APP);
}

TEST_F(SdpTest, parseRtcpFbNackFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 nack foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbRemb) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 goog-remb\r\n");
  ASSERT_EQ((bool)sdp_attr_get_rtcp_fb_remb_enabled(sdp_ptr_, 1, 120), true);
}

TEST_F(SdpTest, parseRtcpRbRembAllPt) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:* goog-remb\r\n");
  ASSERT_EQ(
      (bool)sdp_attr_get_rtcp_fb_remb_enabled(sdp_ptr_, 1, SDP_ALL_PAYLOADS),
      true);
}

TEST_F(SdpTest, parseRtcpFbTrrInt0) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 trr-int 0\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 0U);
}

TEST_F(SdpTest, parseRtcpFbTrrInt123) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 trr-int 123\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 123U);
}

TEST_F(SdpTest, parseRtcpFbCcmFir) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm fir\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_CCM_FIR);
}

TEST_F(SdpTest, parseRtcpFbCcmTmmbr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tmmbr\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TMMBR);
}

TEST_F(SdpTest, parseRtcpFbCcmTmmbrSmaxpr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tmmbr smaxpr=456\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TMMBR);
}

TEST_F(SdpTest, parseRtcpFbCcmTstr) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm tstr\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_TSTR);
}

TEST_F(SdpTest, parseRtcpFbCcmVbcm) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm vbcm 123 456 789\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_VBCM);
  // We don't currently parse out VBCM submessage types, since we don't have
  // any use for them.
}

TEST_F(SdpTest, parseRtcpFbCcmFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm foo\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbCcmFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 ccm foo bar baz\r\n");
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_CCM_UNKNOWN);
}

TEST_F(SdpTest, parseRtcpFbFoo) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo\r\n");
}

TEST_F(SdpTest, parseRtcpFbFooBar) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo bar\r\n");
}

TEST_F(SdpTest, parseRtcpFbFooBarBaz) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:120 foo bar baz\r\n");
}

MOZ_RUNINIT static const std::string kVideoSdpWithUnknonwBrokenFtmp =
    "v=0\r\n"
    "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
    "s=SIP Call\r\n"
    "c=IN IP4 198.51.100.7\r\n"
    "t=0 0\r\n"
    "m=video 56436 RTP/SAVPF 120\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=fmtp:122 unknown=10\n"
    "a=rtpmap:122 red/90000\r\n";

TEST_F(SdpTest, parseUnknownBrokenFtmp) {
  ParseSdp(kVideoSdpWithUnknonwBrokenFtmp);
}

TEST_F(SdpTest, parseRtcpFbKitchenSink) {
  ParseSdp(kVideoSdp +
           "a=rtcp-fb:120 ack rpsi\r\n"
           "a=rtcp-fb:120 ack app\r\n"
           "a=rtcp-fb:120 ack app foo\r\n"
           "a=rtcp-fb:120 ack foo bar\r\n"
           "a=rtcp-fb:120 ack foo bar baz\r\n"
           "a=rtcp-fb:120 nack\r\n"
           "a=rtcp-fb:120 nack pli\r\n"
           "a=rtcp-fb:120 nack sli\r\n"
           "a=rtcp-fb:120 nack rpsi\r\n"
           "a=rtcp-fb:120 nack app\r\n"
           "a=rtcp-fb:120 nack app foo\r\n"
           "a=rtcp-fb:120 nack app foo bar\r\n"
           "a=rtcp-fb:120 nack foo bar baz\r\n"
           "a=rtcp-fb:120 trr-int 0\r\n"
           "a=rtcp-fb:120 trr-int 123\r\n"
           "a=rtcp-fb:120 goog-remb\r\n"
           "a=rtcp-fb:120 ccm fir\r\n"
           "a=rtcp-fb:120 ccm tmmbr\r\n"
           "a=rtcp-fb:120 ccm tmmbr smaxpr=456\r\n"
           "a=rtcp-fb:120 ccm tstr\r\n"
           "a=rtcp-fb:120 ccm vbcm 123 456 789\r\n"
           "a=rtcp-fb:120 ccm foo\r\n"
           "a=rtcp-fb:120 ccm foo bar baz\r\n"
           "a=rtcp-fb:120 foo\r\n"
           "a=rtcp-fb:120 foo bar\r\n"
           "a=rtcp-fb:120 foo bar baz\r\n");

  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_ACK_RPSI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 2), SDP_RTCP_FB_ACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 3), SDP_RTCP_FB_ACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_ACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_ACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_ACK_NOT_FOUND);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 1),
            SDP_RTCP_FB_NACK_BASIC);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 2),
            SDP_RTCP_FB_NACK_PLI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 3),
            SDP_RTCP_FB_NACK_SLI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_NACK_RPSI);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 7),
            SDP_RTCP_FB_NACK_APP);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 8),
            SDP_RTCP_FB_NACK_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_nack(sdp_ptr_, 1, 120, 9),
            SDP_RTCP_FB_NACK_NOT_FOUND);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 1), 0U);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 2), 123U);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_trr_int(sdp_ptr_, 1, 120, 3), 0xFFFFFFFF);

  ASSERT_EQ((bool)sdp_attr_get_rtcp_fb_remb_enabled(sdp_ptr_, 1, 120), true);
  ASSERT_EQ((bool)sdp_attr_get_rtcp_fb_remb_enabled(sdp_ptr_, 2, 120), false);

  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 1), SDP_RTCP_FB_CCM_FIR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 2),
            SDP_RTCP_FB_CCM_TMMBR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 3),
            SDP_RTCP_FB_CCM_TMMBR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 4),
            SDP_RTCP_FB_CCM_TSTR);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 5),
            SDP_RTCP_FB_CCM_VBCM);
  // We don't currently parse out VBCM submessage types, since we don't have
  // any use for them.
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 6),
            SDP_RTCP_FB_CCM_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 7),
            SDP_RTCP_FB_CCM_UNKNOWN);
  ASSERT_EQ(sdp_attr_get_rtcp_fb_ccm(sdp_ptr_, 1, 120, 8),
            SDP_RTCP_FB_CCM_NOT_FOUND);
}

TEST_F(SdpTest, addRtcpFbAckRpsi) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbAck(level, SDP_RTCP_FB_ACK_RPSI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ack rpsi\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbAckRpsiAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbAck(level, SDP_RTCP_FB_ACK_RPSI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ack rpsi\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbAckApp) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbAck(level, SDP_RTCP_FB_ACK_APP, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ack app\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbAckAppAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbAck(level, SDP_RTCP_FB_ACK_APP);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ack app\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNack) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_BASIC, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_BASIC);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackSli) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_SLI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack sli\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackSliAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_SLI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack sli\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackPli) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_PLI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack pli\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackPliAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_PLI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack pli\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackRpsi) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_RPSI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack rpsi\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackRpsiAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_RPSI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack rpsi\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackApp) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_APP, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack app\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackAppAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_APP);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack app\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackRai) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_RAI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack rai\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackRaiAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_RAI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack rai\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackTllei) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_TLLEI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack tllei\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackTlleiAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_TLLEI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack tllei\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackPslei) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_PSLEI, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack pslei\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackPsleiAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_PSLEI);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack pslei\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackEcn) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_ECN, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 nack ecn\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackEcnAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbNack(level, SDP_RTCP_FB_NACK_ECN);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* nack ecn\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbRemb) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbRemb(level, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 goog-remb\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbRembAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbRemb(level);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* goog-remb\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbTrrInt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbTrrInt(level, 12345, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 trr-int 12345\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbNackTrrIntAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbTrrInt(level, 0);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* trr-int 0\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmFir) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_FIR, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ccm fir\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmFirAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_FIR);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ccm fir\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmTmmbr) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_TMMBR, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ccm tmmbr\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmTmmbrAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_TMMBR);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ccm tmmbr\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmTstr) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_TSTR, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ccm tstr\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmTstrAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_TSTR);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ccm tstr\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmVbcm) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_VBCM, 120);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:120 ccm vbcm\r\n"), std::string::npos);
}

TEST_F(SdpTest, addRtcpFbCcmVbcmAllPt) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewRtcpFbCcm(level, SDP_RTCP_FB_CCM_VBCM);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=rtcp-fb:* ccm vbcm\r\n"), std::string::npos);
}

TEST_F(SdpTest, parseRtcpFbAllPayloads) {
  ParseSdp(kVideoSdp + "a=rtcp-fb:* ack rpsi\r\n");
  for (int i = 0; i < 128; i++) {
    ASSERT_EQ(sdp_attr_get_rtcp_fb_ack(sdp_ptr_, 1, i, 1),
              SDP_RTCP_FB_ACK_RPSI);
  }
}
TEST_F(SdpTest, addExtMap) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewExtMap(level, SDP_EXTMAP_AUDIO_LEVEL);
  std::string body = SerializeSdp();
  ASSERT_NE(
      body.find("a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"),
      std::string::npos);
}

TEST_F(SdpTest, parseExtMap) {
  ParseSdp(kVideoSdp +
           "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n");
  ASSERT_STREQ(sdp_attr_get_extmap_uri(sdp_ptr_, 1, 1), SDP_EXTMAP_AUDIO_LEVEL);
  ASSERT_EQ(sdp_attr_get_extmap_id(sdp_ptr_, 1, 1), 1);
}

TEST_F(SdpTest, parseFmtpBitrate) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bitrate=400\r\n");
  ASSERT_EQ(400, sdp_attr_get_fmtp_bitrate_type(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBitrateWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bitrate=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_bitrate_type(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBitrateWith32001) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bitrate=32001\r\n");
  ASSERT_EQ(32001, sdp_attr_get_fmtp_bitrate_type(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBitrateWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bitrate=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_bitrate_type(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpMode) {
  ParseSdp(kVideoSdp + "a=fmtp:120 mode=200\r\n");
  ASSERT_EQ(200U, sdp_attr_get_fmtp_mode_for_payload_type(sdp_ptr_, 1, 0, 120));
}

TEST_F(SdpTest, parseFmtpModeWith4294967295) {
  ParseSdp(kVideoSdp + "a=fmtp:120 mode=4294967295\r\n");
  ASSERT_EQ(4294967295,
            sdp_attr_get_fmtp_mode_for_payload_type(sdp_ptr_, 1, 0, 120));
}

TEST_F(SdpTest, parseFmtpModeWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 mode=4294967296\r\n");
  // returns 0 if not found
  ASSERT_EQ(0U, sdp_attr_get_fmtp_mode_for_payload_type(sdp_ptr_, 1, 0, 120));
}

TEST_F(SdpTest, parseFmtpQcif) {
  ParseSdp(kVideoSdp + "a=fmtp:120 qcif=20\r\n");
  ASSERT_EQ(20, sdp_attr_get_fmtp_qcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpQcifWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 qcif=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_qcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpQcifWith33) {
  ParseSdp(kVideoSdp + "a=fmtp:120 qcif=33\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_qcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif=11\r\n");
  ASSERT_EQ(11, sdp_attr_get_fmtp_cif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCifWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCifWith33) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif=33\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpMaxbr) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxbr=21\r\n");
  ASSERT_EQ(21, sdp_attr_get_fmtp_maxbr(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpMaxbrWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxbr=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_maxbr(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpMaxbrWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxbr=65536\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_maxbr(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpSqcif) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sqcif=6\r\n");
  ASSERT_EQ(6, sdp_attr_get_fmtp_sqcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpSqcifWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sqcif=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_sqcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpSqcifWith33) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sqcif=33\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_sqcif(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif4) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif4=11\r\n");
  ASSERT_EQ(11, sdp_attr_get_fmtp_cif4(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif4With0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif4=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif4(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif4With33) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif4=33\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif4(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif16) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif16=11\r\n");
  ASSERT_EQ(11, sdp_attr_get_fmtp_cif16(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif16With0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif16=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif16(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpCif16With33) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cif16=33\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_cif16(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBpp) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bpp=7\r\n");
  ASSERT_EQ(7, sdp_attr_get_fmtp_bpp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBppWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bpp=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_bpp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpBppWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 bpp=65536\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_bpp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpHrd) {
  ParseSdp(kVideoSdp + "a=fmtp:120 hrd=800\r\n");
  ASSERT_EQ(800, sdp_attr_get_fmtp_hrd(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpHrdWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 hrd=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_hrd(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpHrdWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 hrd=65536\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_hrd(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpProfile) {
  ParseSdp(kVideoSdp + "a=fmtp:120 profile=4\r\n");
  ASSERT_EQ(4, sdp_attr_get_fmtp_profile(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpProfileWith11) {
  ParseSdp(kVideoSdp + "a=fmtp:120 profile=11\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_profile(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpLevel) {
  ParseSdp(kVideoSdp + "a=fmtp:120 level=56\r\n");
  ASSERT_EQ(56, sdp_attr_get_fmtp_level(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpLevelWith101) {
  ParseSdp(kVideoSdp + "a=fmtp:120 level=101\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE, sdp_attr_get_fmtp_level(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpPacketizationMode) {
  ParseSdp(kVideoSdp + "a=fmtp:120 packetization-mode=1\r\n");
  uint16_t packetizationMode;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_pack_mode(sdp_ptr_, 1, 0, 1, &packetizationMode));
  ASSERT_EQ(1, packetizationMode);
}

TEST_F(SdpTest, parseFmtpPacketizationModeWith3) {
  ParseSdp(kVideoSdp + "a=fmtp:120 packetization-mode=3\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_pack_mode(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpInterleavingDepth) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-interleaving-depth=566\r\n");
  uint16_t interleavingDepth;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_interleaving_depth(
                             sdp_ptr_, 1, 0, 1, &interleavingDepth));
  ASSERT_EQ(566, interleavingDepth);
}

TEST_F(SdpTest, parseFmtpInterleavingDepthWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-interleaving-depth=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_interleaving_depth(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpInterleavingDepthWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-interleaving-depth=65536\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_interleaving_depth(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpDeintBuf) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-deint-buf-req=4294967295\r\n");
  uint32_t deintBuf;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_deint_buf_req(sdp_ptr_, 1, 0, 1, &deintBuf));
  ASSERT_EQ(4294967295, deintBuf);
}

TEST_F(SdpTest, parseFmtpDeintBufWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-deint-buf-req=0\r\n");
  uint32_t deintBuf;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_deint_buf_req(sdp_ptr_, 1, 0, 1, &deintBuf));
  ASSERT_EQ(0U, deintBuf);
}

TEST_F(SdpTest, parseFmtpDeintBufWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-deint-buf-req=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_deint_buf_req(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxDonDiff) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-max-don-diff=5678\r\n");
  uint32_t maxDonDiff;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_max_don_diff(sdp_ptr_, 1, 0, 1, &maxDonDiff));
  ASSERT_EQ(5678U, maxDonDiff);
}

TEST_F(SdpTest, parseFmtpMaxDonDiffWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-max-don-diff=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_don_diff(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxDonDiffWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-max-don-diff=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_don_diff(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpInitBufTime) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-init-buf-time=4294967295\r\n");
  uint32_t initBufTime;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_init_buf_time(sdp_ptr_, 1, 0, 1, &initBufTime));
  ASSERT_EQ(4294967295, initBufTime);
}

TEST_F(SdpTest, parseFmtpInitBufTimeWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-init-buf-time=0\r\n");
  uint32_t initBufTime;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_init_buf_time(sdp_ptr_, 1, 0, 1, &initBufTime));
  ASSERT_EQ(0U, initBufTime);
}

TEST_F(SdpTest, parseFmtpInitBufTimeWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 sprop-init-buf-time=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_init_buf_time(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxMbps) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-mbps=46789\r\n");
  uint32_t maxMpbs;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_max_mbps(sdp_ptr_, 1, 0, 1, &maxMpbs));
  ASSERT_EQ(46789U, maxMpbs);
}

TEST_F(SdpTest, parseFmtpMaxMbpsWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-mbps=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_mbps(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxMbpsWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-mbps=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_mbps(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxCpb) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-cpb=47891\r\n");
  uint32_t maxCpb;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_cpb(sdp_ptr_, 1, 0, 1, &maxCpb));
  ASSERT_EQ(47891U, maxCpb);
}

TEST_F(SdpTest, parseFmtpMaxCpbWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-cpb=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_cpb(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxCpbWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-cpb=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_cpb(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxDpb) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-dpb=47892\r\n");
  uint32_t maxDpb;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_dpb(sdp_ptr_, 1, 0, 1, &maxDpb));
  ASSERT_EQ(47892U, maxDpb);
}

TEST_F(SdpTest, parseFmtpMaxDpbWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-dpb=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_dpb(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxDpbWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-dpb=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_dpb(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxBr) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-br=47893\r\n");
  uint32_t maxBr;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_br(sdp_ptr_, 1, 0, 1, &maxBr));
  ASSERT_EQ(47893U, maxBr);
}

TEST_F(SdpTest, parseFmtpMaxBrWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-br=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_br(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxBrWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-br=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_br(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpRedundantPicCap) {
  ParseSdp(kVideoSdp + "a=fmtp:120 redundant-pic-cap=1\r\n");
  ASSERT_EQ(1, sdp_attr_fmtp_is_redundant_pic_cap(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpRedundantPicCapWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 redundant-pic-cap=0\r\n");
  ASSERT_EQ(0, sdp_attr_fmtp_is_redundant_pic_cap(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpRedundantPicCapWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 redundant-pic-cap=2\r\n");
  ASSERT_EQ(0, sdp_attr_fmtp_is_redundant_pic_cap(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpDeintBufCap) {
  ParseSdp(kVideoSdp + "a=fmtp:120 deint-buf-cap=4294967295\r\n");
  uint32_t deintBufCap;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_deint_buf_cap(sdp_ptr_, 1, 0, 1, &deintBufCap));
  ASSERT_EQ(4294967295, deintBufCap);
}

TEST_F(SdpTest, parseFmtpDeintBufCapWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 deint-buf-cap=0\r\n");
  uint32_t deintBufCap;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_deint_buf_cap(sdp_ptr_, 1, 0, 1, &deintBufCap));
  ASSERT_EQ(0U, deintBufCap);
}

TEST_F(SdpTest, parseFmtpDeintBufCapWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 deint-buf-cap=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_deint_buf_cap(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxRcmdNaluSize) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-rcmd-nalu-size=4294967295\r\n");
  uint32_t maxRcmdNaluSize;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_rcmd_nalu_size(
                             sdp_ptr_, 1, 0, 1, &maxRcmdNaluSize));
  ASSERT_EQ(4294967295, maxRcmdNaluSize);
}

TEST_F(SdpTest, parseFmtpMaxRcmdNaluSizeWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-rcmd-nalu-size=0\r\n");
  uint32_t maxRcmdNaluSize;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_rcmd_nalu_size(
                             sdp_ptr_, 1, 0, 1, &maxRcmdNaluSize));
  ASSERT_EQ(0U, maxRcmdNaluSize);
}

TEST_F(SdpTest, parseFmtpMaxRcmdNaluSizeWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-rcmd-nalu-size=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_rcmd_nalu_size(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpParameterAdd) {
  ParseSdp(kVideoSdp + "a=fmtp:120 parameter-add=1\r\n");
  ASSERT_EQ(1, sdp_attr_fmtp_is_parameter_add(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpParameterAddWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 parameter-add=0\r\n");
  ASSERT_EQ(0, sdp_attr_fmtp_is_parameter_add(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpParameterAddWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 parameter-add=2\r\n");
  ASSERT_EQ(0, sdp_attr_fmtp_is_parameter_add(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexK) {
  ParseSdp(kVideoSdp + "a=fmtp:120 K=566\r\n");
  ASSERT_EQ(566, sdp_attr_get_fmtp_annex_k_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexKWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 K=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_annex_k_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexKWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 K=65536\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_annex_k_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexN) {
  ParseSdp(kVideoSdp + "a=fmtp:120 N=4567\r\n");
  ASSERT_EQ(4567, sdp_attr_get_fmtp_annex_n_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexNWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 N=0\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_annex_n_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexNWith65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 N=65536\r\n");
  ASSERT_EQ(SDP_INVALID_VALUE,
            sdp_attr_get_fmtp_annex_n_val(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexP) {
  ParseSdp(kVideoSdp + "a=fmtp:120 P=5678,2\r\n");
  ASSERT_EQ(5678, sdp_attr_get_fmtp_annex_p_picture_resize(sdp_ptr_, 1, 0, 1));
  ASSERT_EQ(2, sdp_attr_get_fmtp_annex_p_warp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexPWithResize0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 P=0,3\r\n");
  ASSERT_EQ(0, sdp_attr_get_fmtp_annex_p_picture_resize(sdp_ptr_, 1, 0, 1));
  ASSERT_EQ(3, sdp_attr_get_fmtp_annex_p_warp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexPWithResize65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 P=65536,4\r\n");
  ASSERT_EQ(0, sdp_attr_get_fmtp_annex_p_picture_resize(sdp_ptr_, 1, 0, 1));
  // if the first fails, the second will too.  Both default to 0 on failure.
  ASSERT_EQ(0, sdp_attr_get_fmtp_annex_p_warp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpAnnexPWithWarp65536) {
  ParseSdp(kVideoSdp + "a=fmtp:120 P=346,65536\r\n");
  ASSERT_EQ(346, sdp_attr_get_fmtp_annex_p_picture_resize(sdp_ptr_, 1, 0, 1));
  ASSERT_EQ(0, sdp_attr_get_fmtp_annex_p_warp(sdp_ptr_, 1, 0, 1));
}

TEST_F(SdpTest, parseFmtpLevelAsymmetryAllowed) {
  ParseSdp(kVideoSdp + "a=fmtp:120 level-asymmetry-allowed=1\r\n");

  uint16_t levelAsymmetryAllowed;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_level_asymmetry_allowed(
                             sdp_ptr_, 1, 0, 1, &levelAsymmetryAllowed));
  ASSERT_EQ(1U, levelAsymmetryAllowed);
}

TEST_F(SdpTest, parseFmtpLevelAsymmetryAllowedWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 level-asymmetry-allowed=0\r\n");
  uint16_t levelAsymmetryAllowed;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_level_asymmetry_allowed(
                             sdp_ptr_, 1, 0, 1, &levelAsymmetryAllowed));
  ASSERT_EQ(0U, levelAsymmetryAllowed);
}

TEST_F(SdpTest, parseFmtpLevelAsymmetryAllowedWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 level-asymmetry-allowed=2\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER, sdp_attr_get_fmtp_level_asymmetry_allowed(
                                       sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxAverageBitrate) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxaveragebitrate=47893\r\n");
  uint32_t maxAverageBitrate;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_max_average_bitrate(
                             sdp_ptr_, 1, 0, 1, &maxAverageBitrate));
  ASSERT_EQ(47893U, maxAverageBitrate);
}

TEST_F(SdpTest, parseFmtpMaxAverageBitrateWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxaveragebitrate=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_average_bitrate(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxAverageBitrateWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxaveragebitrate=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_average_bitrate(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpUsedTx) {
  ParseSdp(kVideoSdp + "a=fmtp:120 usedtx=1\r\n");
  tinybool usedTx;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_usedtx(sdp_ptr_, 1, 0, 1, &usedTx));
  ASSERT_EQ(1, usedTx);
}

TEST_F(SdpTest, parseFmtpUsedTxWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 usedtx=0\r\n");
  tinybool usedTx;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_usedtx(sdp_ptr_, 1, 0, 1, &usedTx));
  ASSERT_EQ(0, usedTx);
}

TEST_F(SdpTest, parseFmtpUsedTxWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 usedtx=2\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_usedtx(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpStereo) {
  ParseSdp(kVideoSdp + "a=fmtp:120 stereo=1\r\n");
  tinybool stereo;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_stereo(sdp_ptr_, 1, 0, 1, &stereo));
  ASSERT_EQ(1, stereo);
}

TEST_F(SdpTest, parseFmtpStereoWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 stereo=0\r\n");
  tinybool stereo;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_stereo(sdp_ptr_, 1, 0, 1, &stereo));
  ASSERT_EQ(0, stereo);
}

TEST_F(SdpTest, parseFmtpStereoWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 stereo=2\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_stereo(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpUseInBandFec) {
  ParseSdp(kVideoSdp + "a=fmtp:120 useinbandfec=1\r\n");
  tinybool useInbandFec;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_useinbandfec(sdp_ptr_, 1, 0, 1, &useInbandFec));
  ASSERT_EQ(1, useInbandFec);
}

TEST_F(SdpTest, parseFmtpUseInBandWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 useinbandfec=0\r\n");
  tinybool useInbandFec;
  ASSERT_EQ(SDP_SUCCESS,
            sdp_attr_get_fmtp_useinbandfec(sdp_ptr_, 1, 0, 1, &useInbandFec));
  ASSERT_EQ(0, useInbandFec);
}

TEST_F(SdpTest, parseFmtpUseInBandWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 useinbandfec=2\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_useinbandfec(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxCodedAudioBandwidth) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxcodedaudiobandwidth=abcdefg\r\n");
  char* maxCodedAudioBandwith =
      sdp_attr_get_fmtp_maxcodedaudiobandwidth(sdp_ptr_, 1, 0, 1);
  ASSERT_EQ(0, strcmp("abcdefg", maxCodedAudioBandwith));
}

TEST_F(SdpTest, parseFmtpMaxCodedAudioBandwidthBad) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxcodedaudiobandwidth=\r\n");
  char* maxCodedAudioBandwith =
      sdp_attr_get_fmtp_maxcodedaudiobandwidth(sdp_ptr_, 1, 0, 1);
  ASSERT_EQ(0, *maxCodedAudioBandwith);
}

TEST_F(SdpTest, parseFmtpCbr) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cbr=1\r\n");
  tinybool cbr;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_cbr(sdp_ptr_, 1, 0, 1, &cbr));
  ASSERT_EQ(1, cbr);
}

TEST_F(SdpTest, parseFmtpCbrWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cbr=0\r\n");
  tinybool cbr;
  ASSERT_EQ(SDP_SUCCESS, sdp_attr_get_fmtp_cbr(sdp_ptr_, 1, 0, 1, &cbr));
  ASSERT_EQ(0, cbr);
}

TEST_F(SdpTest, parseFmtpCbrWith2) {
  ParseSdp(kVideoSdp + "a=fmtp:120 cbr=2\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_cbr(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxPlaybackRate) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxplaybackrate=47900\r\n");
  sdp_attr_t* attr_p = sdp_find_attr(sdp_ptr_, 1, 0, SDP_ATTR_FMTP, 1);
  ASSERT_NE(nullptr, attr_p);
  ASSERT_EQ(47900U, attr_p->attr.fmtp.maxplaybackrate);
}

TEST_F(SdpTest, parseFmtpMaxPlaybackRateWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxplaybackrate=0\r\n");
  sdp_attr_t* attr_p = sdp_find_attr(sdp_ptr_, 1, 0, SDP_ATTR_FMTP, 1);
  ASSERT_EQ(nullptr, attr_p);
}

TEST_F(SdpTest, parseFmtpMaxPlaybackRateWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 maxplaybackrate=4294967296\r\n");
  sdp_attr_t* attr_p = sdp_find_attr(sdp_ptr_, 1, 0, SDP_ATTR_FMTP, 1);
  ASSERT_EQ(nullptr, attr_p);
}

TEST_F(SdpTest, parseFmtpMaxFs) {
  uint32_t val = 0;
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fs=300;max-fr=30\r\n");
  ASSERT_EQ(sdp_attr_get_fmtp_max_fs(sdp_ptr_, 1, 0, 1, &val), SDP_SUCCESS);
  ASSERT_EQ(val, 300U);
}
TEST_F(SdpTest, parseFmtpMaxFsWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fs=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_fs(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxFsWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fs=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_fs(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxFr) {
  uint32_t val = 0;
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fs=300;max-fr=30\r\n");
  ASSERT_EQ(sdp_attr_get_fmtp_max_fr(sdp_ptr_, 1, 0, 1, &val), SDP_SUCCESS);
  ASSERT_EQ(val, 30U);
}

TEST_F(SdpTest, parseFmtpMaxFrWith0) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fr=0\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_fr(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, parseFmtpMaxFrWith4294967296) {
  ParseSdp(kVideoSdp + "a=fmtp:120 max-fr=4294967296\r\n");
  ASSERT_EQ(SDP_INVALID_PARAMETER,
            sdp_attr_get_fmtp_max_fr(sdp_ptr_, 1, 0, 1, nullptr));
}

TEST_F(SdpTest, addFmtpMaxFs) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewFmtpMaxFs(level, 300);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=fmtp:120 max-fs=300\r\n"), std::string::npos);
}

TEST_F(SdpTest, addFmtpMaxFr) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewFmtpMaxFr(level, 30);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=fmtp:120 max-fr=30\r\n"), std::string::npos);
}

TEST_F(SdpTest, addFmtpMaxFsFr) {
  InitLocalSdp();
  int level = AddNewMedia(SDP_MEDIA_VIDEO);
  AddNewFmtpMaxFsFr(level, 300, 30);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=fmtp:120 max-fs=300;max-fr=30\r\n"),
            std::string::npos);
}

MOZ_RUNINIT static const std::string kBrokenFmtp =
    "v=0\r\n"
    "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
    "s=SIP Call\r\n"
    "t=0 0\r\n"
    "m=video 56436 RTP/SAVPF 120\r\n"
    "c=IN IP4 198.51.100.7\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    /* Note: the \0 in this string triggered bz://1089207
     */
    "a=fmtp:120 max-fs=300;max\0fr=30";

TEST_F(SdpTest, parseBrokenFmtp) {
  uint32_t val = 0;
  const char* buf = kBrokenFmtp.data();
  ResetSdp();
  /* We need to manually invoke the parser here to be able to specify the length
   * of the string beyond the \0 in last line of the string.
   */
  ASSERT_EQ(sdp_parse(sdp_ptr_, buf, 165), SDP_SUCCESS);
  ASSERT_EQ(sdp_attr_get_fmtp_max_fs(sdp_ptr_, 1, 0, 1, &val),
            SDP_INVALID_PARAMETER);
}

TEST_F(SdpTest, addIceLite) {
  InitLocalSdp();
  uint16_t inst_num = 0;
  EXPECT_EQ(sdp_add_new_attr(sdp_ptr_, SDP_SESSION_LEVEL, 0, SDP_ATTR_ICE_LITE,
                             &inst_num),
            SDP_SUCCESS);
  std::string body = SerializeSdp();
  ASSERT_NE(body.find("a=ice-lite\r\n"), std::string::npos);
}

TEST_F(SdpTest, parseIceLite) {
  std::string sdp =
      "v=0\r\n"
      "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
      "s=SIP Call\r\n"
      "t=0 0\r\n"
      "a=ice-lite\r\n";
  ParseSdp(sdp);
  ASSERT_TRUE(
      sdp_attr_is_present(sdp_ptr_, SDP_ATTR_ICE_LITE, SDP_SESSION_LEVEL, 0));
}

TEST_F(SdpTest, parseExtmapAllowMixed) {
  std::string sdp =
      "v=0\r\n"
      "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
      "s=SIP Call\r\n"
      "t=0 0\r\n"
      "a=extmap-allow-mixed\r\n";
  ParseSdp(sdp);
  ASSERT_TRUE(sdp_attr_is_present(sdp_ptr_, SDP_ATTR_EXTMAP_ALLOW_MIXED,
                                  SDP_SESSION_LEVEL, 0));
}

class NewSdpTest
    : public ::testing::Test,
      public ::testing::WithParamInterface< ::testing::tuple<bool, bool> > {
 public:
  NewSdpTest() = default;

  void ParseSdp(const std::string& sdp, bool expectSuccess = true,
                bool expectEqual = true) {
    UniquePtr<SdpParser> firstParser(new RsdparsaSdpParser());
    UniquePtr<SdpParser> secondParser(new SipccSdpParser());
    if (::testing::get<1>(GetParam())) {
      firstParser.swap(secondParser);
    }
    mResults = firstParser->Parse(sdp);
#ifdef DEBUG_DISPLAY_SDP
    std::cout << firstParser->Name() << " Parsing SDP:" << std::endl;
    std::stringstream sdpStream(sdp);
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(sdpStream, line, '\n')) {
      if (line.length() && line.back() == '\r') {
        line.pop_back();
      }
      lineNumber++;
      std::cout << std::setw(4) << lineNumber << " " << line << std::endl;
    }
#endif
    // Are we configured to do a parse and serialize before actually
    // running the test?
    if (::testing::get<0>(GetParam())) {
      if (expectSuccess) {
        ASSERT_TRUE(!!mResults->Sdp())
        << "Parse failed on first pass: " << SerializeParseErrors();
      }

      if (mResults->Sdp()) {
        std::stringstream os, os2;
        // Serialize and re-parse
        mResults->Sdp()->Serialize(os);
        const auto secondResults = secondParser->Parse(os.str());
        // Whether we expected the parse to work or not, it should
        // succeed the second time if it succeeded the first.
        ASSERT_TRUE(!!secondResults->Sdp())
        << "Parse failed on second pass, SDP was: " << std::endl
        << os.str() << std::endl
        << "Errors were: " << IntSerializeParseErrors(secondResults);
        // Serialize again and compare
        secondResults->Sdp()->Serialize(os2);
        if (expectEqual) {
          ASSERT_EQ(os.str(), os2.str())
              << "FIRST IS " << mResults->ParserName() << ", SECOND IS "
              << secondResults->ParserName();
        } else {
          ASSERT_NE(os.str(), os2.str())
              << "FIRST IS " << mResults->ParserName() << ", SECOND IS "
              << secondResults->ParserName();
        }
      }
    }

    if (expectSuccess) {
      ASSERT_TRUE(!!mResults->Sdp())
      << "Parse failed: " << SerializeParseErrors();
      ASSERT_EQ(0U, ParseErrorCount())
          << "Got unexpected parse errors/warnings: " << SerializeParseErrors();
    }
  }
  const UniquePtr<class Sdp>& Sdp() const {
    static const UniquePtr<class Sdp> NO_SDP(nullptr);
    return mResults ? mResults->Sdp() : NO_SDP;
  }

  size_t ParseErrorCount() const { return mResults->Errors().size(); }
  size_t ParseWarningCount() const { return mResults->Warnings().size(); }

  std::string IntSerializeParseErrors(
      const UniquePtr<SdpParser::Results>& aResults) const {
    std::stringstream output;
    for (const auto& e : aResults->Errors()) {
      output << e.first << ": " << e.second << std::endl;
    }
    return output.str();
  }
  // For streaming parse errors
  std::string SerializeParseErrors() const {
    return IntSerializeParseErrors(mResults);
  }

  std::string IntSerializeParseWarnings(
      const UniquePtr<SdpParser::Results>& aResults) const {
    std::stringstream output;
    for (const auto& e : aResults->Warnings()) {
      output << e.first << ": " << e.second << std::endl;
    }
    return output.str();
  }

  std::string SerializeParseWarnings() const {
    return IntSerializeParseWarnings(mResults);
  }

  void CheckRtpmap(const std::string& expected_pt,
                   SdpRtpmapAttributeList::CodecType codec,
                   const std::string& name, uint32_t clock, uint16_t channels,
                   const std::string& search_pt,
                   const SdpRtpmapAttributeList& rtpmaps) const {
    ASSERT_TRUE(rtpmaps.HasEntry(search_pt));
    const auto& attr = rtpmaps.GetEntry(search_pt);
    ASSERT_EQ(expected_pt, attr.pt);
    ASSERT_EQ(codec, attr.codec);
    std::cout << "Codec = " << name << std::endl;
    ASSERT_EQ(name, attr.name);
    ASSERT_EQ(clock, attr.clock);
    ASSERT_EQ(channels, attr.channels);
  }

  void CheckSctpmap(const std::string& expected_pt, const std::string& name,
                    uint16_t streams, const std::string& search_pt,
                    const SdpSctpmapAttributeList& sctpmaps) const {
    ASSERT_TRUE(sctpmaps.HasEntry(search_pt));
    const auto& attr = sctpmaps.GetFirstEntry();
    ASSERT_EQ(expected_pt, search_pt);
    ASSERT_EQ(expected_pt, attr.pt);
    ASSERT_EQ(name, attr.name);
    ASSERT_EQ(streams, attr.streams);
  }

  void CheckRtcpFb(const SdpRtcpFbAttributeList::Feedback& feedback,
                   const std::string& pt, SdpRtcpFbAttributeList::Type type,
                   const std::string& first_parameter,
                   const std::string& extra = "") const {
    ASSERT_EQ(pt, feedback.pt);
    ASSERT_EQ(type, feedback.type);
    ASSERT_EQ(first_parameter, feedback.parameter);
    ASSERT_EQ(extra, feedback.extra);
  }

  void CheckDtmfFmtp(const std::string& expectedDtmfTones) const {
    ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kFmtpAttribute));
    auto audio_format_params =
        Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
    ASSERT_EQ(2U, audio_format_params.size());

    ASSERT_EQ("101", audio_format_params[1].format);
    ASSERT_TRUE(!!audio_format_params[1].parameters);
    const SdpFmtpAttributeList::TelephoneEventParameters* te_parameters =
        static_cast<SdpFmtpAttributeList::TelephoneEventParameters*>(
            audio_format_params[1].parameters.get());
    ASSERT_NE(0U, te_parameters->dtmfTones.size());
    ASSERT_EQ(expectedDtmfTones, te_parameters->dtmfTones);
  }

  void CheckSerialize(const std::string& expected,
                      const SdpAttribute& attr) const {
    std::stringstream str;
    attr.Serialize(str);
    ASSERT_EQ(expected, str.str());
  }

  bool ResultsAreFromSipcc() const {
    return mResults && SipccSdpParser::IsNamed(mResults->ParserName());
  }

  mozilla::UniquePtr<SdpParser::Results> mResults;
};  // class NewSdpTest

TEST_P(NewSdpTest, CreateDestroy) {}

TEST_P(NewSdpTest, ParseEmpty) {
  ParseSdp("", false);
  ASSERT_FALSE(Sdp());
  ASSERT_NE(0U, ParseErrorCount()) << "Expected at least one parse error.";
}

MOZ_RUNINIT const std::string kBadSdp = "This is SDPARTA!!!!";

TEST_P(NewSdpTest, ParseGarbage) {
  ParseSdp(kBadSdp, false);
  ASSERT_FALSE(Sdp());
  ASSERT_NE(0U, ParseErrorCount()) << "Expected at least one parse error.";
}

TEST_P(NewSdpTest, ParseGarbageTwice) {
  ParseSdp(kBadSdp, false);
  ASSERT_FALSE(Sdp());
  size_t errorCount = ParseErrorCount();
  ASSERT_NE(0U, errorCount) << "Expected at least one parse error.";
  ParseSdp(kBadSdp, false);
  ASSERT_FALSE(Sdp());
  ASSERT_EQ(errorCount, ParseErrorCount())
      << "Expected same error count for same SDP.";
}

TEST_P(NewSdpTest, ParseMinimal) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(0U, ParseErrorCount())
      << "Got parse errors: " << SerializeParseErrors();
}

TEST_P(NewSdpTest, CheckOriginGetUsername) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ("-", Sdp()->GetOrigin().GetUsername())
      << "Wrong username in origin";
}

TEST_P(NewSdpTest, CheckOriginGetSessionId) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(4294967296U, Sdp()->GetOrigin().GetSessionId())
      << "Wrong session id in origin";
}

TEST_P(NewSdpTest, CheckOriginGetSessionVersion) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(2U, Sdp()->GetOrigin().GetSessionVersion())
      << "Wrong version in origin";
}

TEST_P(NewSdpTest, CheckOriginGetAddrType) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(sdp::kIPv4, Sdp()->GetOrigin().GetAddrType())
      << "Wrong address type in origin";
}

TEST_P(NewSdpTest, CheckOriginGetAddress) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ("127.0.0.1", Sdp()->GetOrigin().GetAddress())
      << "Wrong address in origin";
}

TEST_P(NewSdpTest, parseSessionInformation) {
  SKIP_TEST_WITH_SIPCC_PARSER
  ParseSdp(
      "v=0\r\n"
      "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
      "s=With information\r\n"
      "i=an information\r\n"
      "c=IN IP4 198.51.100.7\r\n"
      "t=0 0\r\n"
      "m=video 56436 RTP/SAVPF 120\r\n"
      "a=rtpmap:120 VP8/90000\r\n");
}

TEST_P(NewSdpTest, parseMediaInformation) {
  SKIP_TEST_WITH_SIPCC_PARSER
  ParseSdp(
      "v=0\r\n"
      "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
      "s=With information\r\n"
      "c=IN IP4 198.51.100.7\r\n"
      "t=0 0\r\n"
      "m=video 56436 RTP/SAVPF 120\r\n"
      "i=an information\r\n"
      "a=rtpmap:120 VP8/90000\r\n");
}

TEST_P(NewSdpTest, CheckGetMissingBandwidth) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(0U, Sdp()->GetBandwidth("CT")) << "Wrong bandwidth in session";
}

TEST_P(NewSdpTest, CheckGetBandwidth) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "b=FOOBAR:10" CRLF
           "b=AS:4" CRLF "t=0 0" CRLF "m=video 56436 RTP/SAVPF 120" CRLF
           "a=rtpmap:120 VP8/90000" CRLF);
  ASSERT_EQ(5000U, Sdp()->GetBandwidth("CT"))
      << "Wrong CT bandwidth in session";
  ASSERT_EQ(0U, Sdp()->GetBandwidth("FOOBAR"))
      << "Wrong FOOBAR bandwidth in session";
  ASSERT_EQ(4U, Sdp()->GetBandwidth("AS")) << "Wrong AS bandwidth in session";
}

TEST_P(NewSdpTest, CheckGetMediaSectionsCount) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";
}

TEST_P(NewSdpTest, CheckMediaSectionGetMediaType) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(SdpMediaSection::kVideo, Sdp()->GetMediaSection(0).GetMediaType())
      << "Wrong type for first media section";
}

TEST_P(NewSdpTest, CheckMediaSectionGetProtocol) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(SdpMediaSection::kRtpSavpf, Sdp()->GetMediaSection(0).GetProtocol())
      << "Wrong protocol for video";
}

TEST_P(NewSdpTest, CheckMediaSectionGetFormats) {
  ParseSdp(kVideoSdp);
  auto video_formats = Sdp()->GetMediaSection(0).GetFormats();
  ASSERT_EQ(1U, video_formats.size()) << "Wrong number of formats for video";
  ASSERT_EQ("120", video_formats[0]);
}

TEST_P(NewSdpTest, CheckMediaSectionGetPort) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(56436U, Sdp()->GetMediaSection(0).GetPort())
      << "Wrong port number in media section";
}

TEST_P(NewSdpTest, CheckMediaSectionGetMissingPortCount) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(0U, Sdp()->GetMediaSection(0).GetPortCount())
      << "Wrong port count in media section";
}

TEST_P(NewSdpTest, CheckMediaSectionGetPortCount) {
  ParseSdp(kVideoSdp + "m=audio 12345/2 RTP/SAVPF 0" CRLF
                       "a=rtpmap:0 PCMU/8000" CRLF);
  ASSERT_EQ(2U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";
  ASSERT_EQ(2U, Sdp()->GetMediaSection(1).GetPortCount())
      << "Wrong port count in media section";
}

TEST_P(NewSdpTest, CheckMediaSectionGetMissingBandwidth) {
  ParseSdp(kVideoSdp);
  ASSERT_EQ(0U, Sdp()->GetMediaSection(0).GetBandwidth("CT"))
      << "Wrong bandwidth in media section";
}

TEST_P(NewSdpTest, CheckMediaSectionGetBandwidth) {
  ParseSdp(
      "v=0\r\n"
      "o=- 4294967296 2 IN IP4 127.0.0.1\r\n"
      "s=SIP Call\r\n"
      "c=IN IP4 198.51.100.7\r\n"
      "t=0 0\r\n"
      "m=video 56436 RTP/SAVPF 120\r\n"
      "b=CT:1000\r\n"
      "a=rtpmap:120 VP8/90000\r\n");
  ASSERT_EQ(1000U, Sdp()->GetMediaSection(0).GetBandwidth("CT"))
      << "Wrong bandwidth in media section";
}

// Define a string that is 258 characters long. We use a long string here so
// that we can test that we are able to parse and handle a string longer than
// the default maximum length of 256 in sipcc.
#define ID_A "1234567890abcdef"
#define ID_B ID_A ID_A ID_A ID_A
#define LONG_IDENTITY ID_B ID_B ID_B ID_B "xx"

#define BASE64_DTLS_HELLO                                                     \
  "FgEAAAAAAAAAAAAAagEAAF4AAAAAAAAAXgEARI11KHx3QB6Ky"                         \
  "CKgoBj/kwjKrApkL8kiZLwIqBaJGT8AAAA2ADkAOAA1ABYAEwAKADMAMgAvAAcAZgAFAAQAYw" \
  "BiAGEAFQASAAkAZQBkAGAAFAARAAgABgADAQA="

// SDP from a basic A/V apprtc call FFX/FFX
MOZ_RUNINIT const std::vector<std::string> kBasicAudioVideoOfferLines = {
    "v=0",
    "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0",
    "s=SIP Call",
    "c=IN IP4 224.0.0.1/100/12",
    "t=0 0",
    "a=dtls-message:client " BASE64_DTLS_HELLO,
    "a=ice-ufrag:4a799b2e",
    "a=ice-pwd:e4cc12a910f106a0a744719425510e17",
    "a=ice-lite",
    "a=ice-options:trickle foo",
    "a=msid-semantic:WMS stream streama",
    "a=msid-semantic:foo stream",
    "a=fingerprint:sha-256 "
    "DF:2E:AC:8A:FD:0A:8E:99:BF:5D:E8:3C:E7:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:"
    "A0:72:9B:14:08:6D:0F:4C",
    "a=identity:" LONG_IDENTITY,
    "a=group:BUNDLE first second",
    "a=group:BUNDLE third",
    "a=group:LS first third",
    "m=audio 9 RTP/SAVPF 109 9 0 8 101",
    "c=IN IP4 0.0.0.0",
    "a=mid:first",
    "a=rtpmap:109 opus/48000/2",
    "a=fmtp:109 maxplaybackrate=32000;stereo=1",
    "a=ptime:20",
    "a=maxptime:20",
    "a=rtpmap:9 G722/8000",
    "a=rtpmap:0 PCMU/8000",
    "a=rtpmap:8 PCMA/8000",
    "a=rtpmap:101 telephone-event/8000",
    "a=fmtp:101 0-15,66,32-34,67",
    "a=ice-ufrag:00000000",
    "a=ice-pwd:0000000000000000000000000000000",
    "a=sendonly",
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level",
    "a=extmap-allow-mixed",
    "a=setup:actpass",
    "a=rtcp-mux",
    "a=msid:stream track",
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 62453 typ host",
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 62453 typ srflx raddr "
    "10.0.0.36 rport 62453",
    "a=candidate:3 1 UDP 100401151 162.222.183.171 49761 typ relay raddr "
    "162.222.183.171 rport 49761",
    "a=candidate:6 1 UDP 16515071 162.222.183.171 51858 typ relay raddr "
    "162.222.183.171 rport 51858",
    "a=candidate:3 2 UDP 100401150 162.222.183.171 62454 typ relay raddr "
    "162.222.183.171 rport 62454",
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 55428 typ srflx raddr "
    "10.0.0.36 rport 55428",
    "a=candidate:6 2 UDP 16515070 162.222.183.171 50340 typ relay raddr "
    "162.222.183.171 rport 50340",
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 55428 typ host",
    "a=rtcp:62454 IN IP4 162.222.183.171",
    "a=end-of-candidates",
    "a=ssrc:5150",
    "m=video 9 RTP/SAVPF 120 121 122 123",
    "c=IN IP6 ::1",
    "a=fingerprint:sha-1 "
    "DF:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:08:6D:0F:4C",
    "a=mid:second",
    "a=rtpmap:120 VP8/90000",
    "a=fmtp:120 max-fs=3600;max-fr=30",
    "a=rtpmap:121 VP9/90000",
    "a=fmtp:121 max-fs=3600;max-fr=30",
    "a=rtpmap:122 red/90000",
    "a=rtpmap:123 ulpfec/90000",
    "a=fmtp:122 120/121/123",
    "a=extmap-allow-mixed",
    "a=recvonly",
    "a=rtcp-fb:120 nack",
    "a=rtcp-fb:120 nack pli",
    "a=rtcp-fb:120 ccm fir",
    "a=rtcp-fb:121 nack",
    "a=rtcp-fb:121 nack pli",
    "a=rtcp-fb:121 ccm fir",
    "a=setup:active",
    "a=rtcp-mux",
    "a=msid:streama tracka",
    "a=msid:streamb trackb",
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 59530 typ host",
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 64378 typ host",
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 64378 typ srflx raddr "
    "10.0.0.36 rport 64378",
    "a=candidate:6 2 UDP 16515070 162.222.183.171 64941 typ relay raddr "
    "162.222.183.171 rport 64941",
    "a=candidate:6 1 UDP 16515071 162.222.183.171 64800 typ relay raddr "
    "162.222.183.171 rport 64800",
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 59530 typ srflx raddr "
    "10.0.0.36 rport 59530",
    "a=candidate:3 1 UDP 100401151 162.222.183.171 62935 typ relay raddr "
    "162.222.183.171 rport 62935",
    "a=candidate:3 2 UDP 100401150 162.222.183.171 61026 typ relay raddr "
    "162.222.183.171 rport 61026",
    "a=rtcp:61026",
    "a=end-of-candidates",
    "a=ssrc:1111 foo",
    "a=ssrc:1111 foo:bar",
    "a=ssrc:1111 msid:1d0cdb4e-5934-4f0f-9f88-40392cb60d31 "
    "315b086a-5cb6-4221-89de-caf0b038c79d",
    "a=imageattr:120 send * recv *",
    "a=imageattr:121 send [x=640,y=480] recv [x=640,y=480]",
    "a=rid:bar recv pt=120;max-width=800;max-height=600",
    "a=rid:bar123 recv max-width=1920;max-height=1080",
    "a=simulcast:recv bar;bar123",
    "m=audio 9 RTP/SAVPF 0",
    "a=mid:third",
    "a=rtpmap:0 PCMU/8000",
    "a=ice-options:foo bar",
    "a=msid:noappdata",
    "a=extmap-allow-mixed",
    "a=bundle-only"};

// SDP from a basic A/V apprtc call FFX/FFX
MOZ_RUNINIT const std::vector<std::string> kBasicAV1AudioVideoOfferLines = {
    "v=0",
    "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0",
    "s=SIP Call",
    "c=IN IP4 224.0.0.1/100/12",
    "t=0 0",
    "a=dtls-message:client " BASE64_DTLS_HELLO,
    "a=ice-ufrag:4a799b2e",
    "a=ice-pwd:e4cc12a910f106a0a744719425510e17",
    "a=ice-lite",
    "a=ice-options:trickle foo",
    "a=msid-semantic:WMS stream streama",
    "a=msid-semantic:foo stream",
    "a=fingerprint:sha-256 "
    "DF:2E:AC:8A:FD:0A:8E:99:BF:5D:E8:3C:E7:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:"
    "A0:72:9B:14:08:6D:0F:4C",
    "a=identity:" LONG_IDENTITY,
    "a=group:BUNDLE first second",
    "a=group:BUNDLE third",
    "a=group:LS first third",
    "m=audio 9 RTP/SAVPF 109 9 0 8 101",
    "c=IN IP4 0.0.0.0",
    "a=mid:first",
    "a=rtpmap:109 opus/48000/2",
    "a=fmtp:109 maxplaybackrate=32000;stereo=1",
    "a=ptime:20",
    "a=maxptime:20",
    "a=rtpmap:9 G722/8000",
    "a=rtpmap:0 PCMU/8000",
    "a=rtpmap:8 PCMA/8000",
    "a=rtpmap:101 telephone-event/8000",
    "a=fmtp:101 0-15,66,32-34,67",
    "a=ice-ufrag:00000000",
    "a=ice-pwd:0000000000000000000000000000000",
    "a=sendonly",
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level",
    "a=extmap-allow-mixed",
    "a=setup:actpass",
    "a=rtcp-mux",
    "a=msid:stream track",
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 62453 typ host",
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 62453 typ srflx raddr "
    "10.0.0.36 rport 62453",
    "a=candidate:3 1 UDP 100401151 162.222.183.171 49761 typ relay raddr "
    "162.222.183.171 rport 49761",
    "a=candidate:6 1 UDP 16515071 162.222.183.171 51858 typ relay raddr "
    "162.222.183.171 rport 51858",
    "a=candidate:3 2 UDP 100401150 162.222.183.171 62454 typ relay raddr "
    "162.222.183.171 rport 62454",
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 55428 typ srflx raddr "
    "10.0.0.36 rport 55428",
    "a=candidate:6 2 UDP 16515070 162.222.183.171 50340 typ relay raddr "
    "162.222.183.171 rport 50340",
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 55428 typ host",
    "a=rtcp:62454 IN IP4 162.222.183.171",
    "a=end-of-candidates",
    "a=ssrc:5150",
    "m=video 9 RTP/SAVPF 98 122 123",
    "c=IN IP6 ::1",
    "a=fingerprint:sha-1 "
    "DF:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:08:6D:0F:4C",
    "a=mid:second",
    "a=rtpmap:98 AV1/90000",
    "a=extmap-allow-mixed",
    "a=fmtp:98 profile=1;level-idx=1;tier=1;max-fs=3600;max-fr=30",
    "a=rtpmap:122 red/90000",
    "a=rtpmap:123 ulpfec/90000",
    "a=fmtp:122 98/123",
    "a=recvonly",
    "a=rtcp-fb:98 nack",
    "a=rtcp-fb:98 nack pli",
    "a=rtcp-fb:98 ccm fir",
    "a=rtcp-fb:121 nack",
    "a=rtcp-fb:121 nack pli",
    "a=rtcp-fb:121 ccm fir",
    "a=setup:active",
    "a=rtcp-mux",
    "a=msid:streama tracka",
    "a=msid:streamb trackb",
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 59530 typ host",
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 64378 typ host",
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 64378 typ srflx raddr "
    "10.0.0.36 rport 64378",
    "a=candidate:6 2 UDP 16515070 162.222.183.171 64941 typ relay raddr "
    "162.222.183.171 rport 64941",
    "a=candidate:6 1 UDP 16515071 162.222.183.171 64800 typ relay raddr "
    "162.222.183.171 rport 64800",
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 59530 typ srflx raddr "
    "10.0.0.36 rport 59530",
    "a=candidate:3 1 UDP 100401151 162.222.183.171 62935 typ relay raddr "
    "162.222.183.171 rport 62935",
    "a=candidate:3 2 UDP 100401150 162.222.183.171 61026 typ relay raddr "
    "162.222.183.171 rport 61026",
    "a=rtcp:61026",
    "a=end-of-candidates",
    "a=ssrc:1111 foo",
    "a=ssrc:1111 foo:bar",
    "a=ssrc:1111 msid:1d0cdb4e-5934-4f0f-9f88-40392cb60d31 "
    "315b086a-5cb6-4221-89de-caf0b038c79d",
    "a=imageattr:120 send * recv *",
    "a=imageattr:121 send [x=640,y=480] recv [x=640,y=480]",
    "m=audio 9 RTP/SAVPF 0",
    "a=mid:third",
    "a=rtpmap:0 PCMU/8000",
    "a=ice-options:foo bar",
    "a=msid:noappdata",
    "a=extmap-allow-mixed",
    "a=bundle-only"};

static std::string joinSdp(const std::vector<std::string>& aSdp,
                           const std::string& aEndl) {
  std::ostringstream result;
  for (const auto& line : aSdp) {
    result << line << aEndl;
  }
  // Extra endl!
  result << aEndl;
  return result.str();
}

MOZ_RUNINIT const std::string kBasicAudioVideoOffer =
    joinSdp(kBasicAudioVideoOfferLines, "\r\n");

MOZ_RUNINIT const std::string kBasicAudioVideoOfferLinefeedOnly =
    joinSdp(kBasicAudioVideoOfferLines, "\n");

TEST_P(NewSdpTest, BasicAudioVideoSdpParse) { ParseSdp(kBasicAudioVideoOffer); }

MOZ_RUNINIT const std::string kAv1AudioVideoOffer =
    joinSdp(kBasicAV1AudioVideoOfferLines, "\r\n");

MOZ_RUNINIT const std::string kAv1AudioVideoOfferLinefeedOnly =
    joinSdp(kBasicAV1AudioVideoOfferLines, "\n");

TEST_P(NewSdpTest, Av1AudioVideoSdpParse) { ParseSdp(kAv1AudioVideoOffer); }

TEST_P(NewSdpTest, Av1AudioVideoSdpParseLinefeedOnly) {
  ParseSdp(kAv1AudioVideoOfferLinefeedOnly);
}

TEST_P(NewSdpTest, CheckAv1Fmtp) {
  // https://aomediacodec.github.io/av1-rtp-spec/#sdp-parameters
  // Out-of-range parameters are ignored.
  // This changed from being rejected by webrtc-sdp, to bring the two
  // parsers in line with each other, before switching over to using
  // webrtc-sdp exclusively. We can then address the differences one
  // by one. See bug 2067241.
  for (const auto* outOfRange : {"profile=3", "level-idx=32", "tier=2"}) {
    ParseSdp(
        kVideoSdp + "a=rtpmap:99 AV1/90000\r\na=fmtp:99 " + outOfRange + "\r\n",
        false);
    ASSERT_TRUE(!!Sdp())
    << "Parse failed for " << outOfRange << ": " << SerializeParseErrors();
    const auto* ignored = Sdp()->GetMediaSection(0).FindFmtp("99");
    ASSERT_TRUE(ignored);
    const auto& ignoredAv1 =
        static_cast<const SdpFmtpAttributeList::Av1Parameters&>(*ignored);
    EXPECT_TRUE(ignoredAv1.profile.isNothing()) << outOfRange;
    EXPECT_TRUE(ignoredAv1.levelIdx.isNothing()) << outOfRange;
    EXPECT_TRUE(ignoredAv1.tier.isNothing()) << outOfRange;
  }

  ParseSdp(kVideoSdp +
           "a=rtpmap:99 AV1/90000\r\na=fmtp:99 "
           "profile=0;level-idx=9;tier=0\r\n");

  const SdpMediaSection& msec = Sdp()->GetMediaSection(0);
  const auto* params = msec.FindFmtp("99");
  ASSERT_TRUE(params);
  ASSERT_EQ(SdpRtpmapAttributeList::kAV1, params->codec_type);

  const auto& av1 =
      static_cast<const SdpFmtpAttributeList::Av1Parameters&>(*params);
  ASSERT_EQ(Some(static_cast<uint8_t>(0)), av1.profile);
  ASSERT_EQ(Some(static_cast<uint8_t>(9)), av1.levelIdx);
  ASSERT_EQ(Some(static_cast<uint8_t>(0)), av1.tier);
}

TEST_P(NewSdpTest, CheckRemoveFmtp) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  SdpAttributeList& audioAttrList =
      Sdp()->GetMediaSection(0).GetAttributeList();

  ASSERT_TRUE(audioAttrList.HasAttribute(SdpAttribute::kFmtpAttribute));
  ASSERT_EQ(2U, audioAttrList.GetFmtp().mFmtps.size());
  ASSERT_TRUE(Sdp()->GetMediaSection(0).FindFmtp("109"));
  ASSERT_TRUE(Sdp()->GetMediaSection(0).FindFmtp("101"));

  Sdp()->GetMediaSection(0).RemoveFmtp("101");

  ASSERT_TRUE(audioAttrList.HasAttribute(SdpAttribute::kFmtpAttribute));
  ASSERT_EQ(1U, audioAttrList.GetFmtp().mFmtps.size());
  ASSERT_TRUE(Sdp()->GetMediaSection(0).FindFmtp("109"));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).FindFmtp("101"));

  Sdp()->GetMediaSection(0).RemoveFmtp("109");

  ASSERT_TRUE(audioAttrList.HasAttribute(SdpAttribute::kFmtpAttribute));
  ASSERT_EQ(0U, audioAttrList.GetFmtp().mFmtps.size());
  ASSERT_FALSE(Sdp()->GetMediaSection(0).FindFmtp("109"));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).FindFmtp("101"));

  // make sure we haven't disturbed the video fmtps
  SdpAttributeList& videoAttrList =
      Sdp()->GetMediaSection(1).GetAttributeList();
  ASSERT_TRUE(videoAttrList.HasAttribute(SdpAttribute::kFmtpAttribute));
  ASSERT_EQ(3U, videoAttrList.GetFmtp().mFmtps.size());
  ASSERT_TRUE(Sdp()->GetMediaSection(1).FindFmtp("120"));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).FindFmtp("121"));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).FindFmtp("122"));
}

TEST_P(NewSdpTest, CheckIceUfrag) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kIceUfragAttribute));
  auto ice_ufrag = Sdp()->GetAttributeList().GetIceUfrag();
  ASSERT_EQ("4a799b2e", ice_ufrag) << "Wrong ice-ufrag value";

  ice_ufrag = Sdp()->GetMediaSection(0).GetAttributeList().GetIceUfrag();
  ASSERT_EQ("00000000", ice_ufrag) << "ice-ufrag isn't overridden";

  ice_ufrag = Sdp()->GetMediaSection(1).GetAttributeList().GetIceUfrag();
  ASSERT_EQ("4a799b2e", ice_ufrag) << "ice-ufrag isn't carried to m-section";
}

TEST_P(NewSdpTest, CheckIcePwd) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kIcePwdAttribute));
  auto ice_pwd = Sdp()->GetAttributeList().GetIcePwd();
  ASSERT_EQ("e4cc12a910f106a0a744719425510e17", ice_pwd)
      << "Wrong ice-pwd value";

  ice_pwd = Sdp()->GetMediaSection(0).GetAttributeList().GetIcePwd();
  ASSERT_EQ("0000000000000000000000000000000", ice_pwd)
      << "ice-pwd isn't overridden";

  ice_pwd = Sdp()->GetMediaSection(1).GetAttributeList().GetIcePwd();
  ASSERT_EQ("e4cc12a910f106a0a744719425510e17", ice_pwd)
      << "ice-pwd isn't carried to m-section";
}

TEST_P(NewSdpTest, CheckIceOptions) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kIceOptionsAttribute));
  auto ice_options = Sdp()->GetAttributeList().GetIceOptions();
  ASSERT_EQ(2U, ice_options.mValues.size()) << "Wrong ice-options size";
  ASSERT_EQ("trickle", ice_options.mValues[0]) << "Wrong ice-options value";
  ASSERT_EQ("foo", ice_options.mValues[1]) << "Wrong ice-options value";

  ASSERT_TRUE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kIceOptionsAttribute));
  auto ice_options_media_level =
      Sdp()->GetMediaSection(2).GetAttributeList().GetIceOptions();
  ASSERT_EQ(2U, ice_options_media_level.mValues.size())
      << "Wrong ice-options size";
  ASSERT_EQ("foo", ice_options_media_level.mValues[0])
      << "Wrong ice-options value";
  ASSERT_EQ("bar", ice_options_media_level.mValues[1])
      << "Wrong ice-options value";
}

TEST_P(NewSdpTest, CheckFingerprint) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kFingerprintAttribute));
  auto fingerprints = Sdp()->GetAttributeList().GetFingerprint();
  ASSERT_EQ(1U, fingerprints.mFingerprints.size());
  ASSERT_EQ(SdpFingerprintAttributeList::kSha256,
            fingerprints.mFingerprints[0].hashFunc)
      << "Wrong hash function";
  ASSERT_EQ(
      "DF:2E:AC:8A:FD:0A:8E:99:BF:5D:E8:3C:E7:FA:FB:08:"
      "3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:08:6D:0F:4C",
      SdpFingerprintAttributeList::FormatFingerprint(
          fingerprints.mFingerprints[0].fingerprint))
      << "Wrong fingerprint";
  ASSERT_EQ(0xdfU, fingerprints.mFingerprints[0].fingerprint[0])
      << "first fingerprint element is iffy";

  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount());

  // Fallback to session level
  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFingerprintAttribute));
  fingerprints = Sdp()->GetMediaSection(0).GetAttributeList().GetFingerprint();
  ASSERT_EQ(1U, fingerprints.mFingerprints.size());
  ASSERT_EQ(SdpFingerprintAttributeList::kSha256,
            fingerprints.mFingerprints[0].hashFunc)
      << "Wrong hash function";
  ASSERT_EQ(
      "DF:2E:AC:8A:FD:0A:8E:99:BF:5D:E8:3C:E7:FA:FB:08:"
      "3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:08:6D:0F:4C",
      SdpFingerprintAttributeList::FormatFingerprint(
          fingerprints.mFingerprints[0].fingerprint))
      << "Wrong fingerprint";
  ASSERT_EQ(0xdfU, fingerprints.mFingerprints[0].fingerprint[0])
      << "first fingerprint element is iffy";

  // Media level
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kFingerprintAttribute));
  fingerprints = Sdp()->GetMediaSection(1).GetAttributeList().GetFingerprint();
  ASSERT_EQ(1U, fingerprints.mFingerprints.size());
  ASSERT_EQ(SdpFingerprintAttributeList::kSha1,
            fingerprints.mFingerprints[0].hashFunc)
      << "Wrong hash function";
  ASSERT_EQ(
      "DF:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:"
      "08:6D:0F:4C",
      SdpFingerprintAttributeList::FormatFingerprint(
          fingerprints.mFingerprints[0].fingerprint))
      << "Wrong fingerprint";
  ASSERT_EQ(0xdfU, fingerprints.mFingerprints[0].fingerprint[0])
      << "first fingerprint element is iffy";
}

TEST_P(NewSdpTest, CheckIdentity) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kIdentityAttribute));
  auto identity = Sdp()->GetAttributeList().GetIdentity();
  ASSERT_EQ(LONG_IDENTITY, identity) << "Wrong identity assertion";
}

TEST_P(NewSdpTest, CheckDtlsMessage) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_TRUE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kDtlsMessageAttribute));
  auto dtls_message = Sdp()->GetAttributeList().GetDtlsMessage();
  ASSERT_EQ(SdpDtlsMessageAttribute::kClient, dtls_message.mRole)
      << "Wrong dtls-message role";
  ASSERT_EQ(BASE64_DTLS_HELLO, dtls_message.mValue)
      << "Wrong dtls-message value";
}

TEST_P(NewSdpTest, CheckNumberOfMediaSections) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";
}

TEST_P(NewSdpTest, CheckMlines) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";
  ASSERT_EQ(SdpMediaSection::kAudio, Sdp()->GetMediaSection(0).GetMediaType())
      << "Wrong type for first media section";
  ASSERT_EQ(SdpMediaSection::kRtpSavpf, Sdp()->GetMediaSection(0).GetProtocol())
      << "Wrong protocol for audio";
  auto audio_formats = Sdp()->GetMediaSection(0).GetFormats();
  ASSERT_EQ(5U, audio_formats.size()) << "Wrong number of formats for audio";
  ASSERT_EQ("109", audio_formats[0]);
  ASSERT_EQ("9", audio_formats[1]);
  ASSERT_EQ("0", audio_formats[2]);
  ASSERT_EQ("8", audio_formats[3]);
  ASSERT_EQ("101", audio_formats[4]);

  ASSERT_EQ(SdpMediaSection::kVideo, Sdp()->GetMediaSection(1).GetMediaType())
      << "Wrong type for second media section";
  ASSERT_EQ(SdpMediaSection::kRtpSavpf, Sdp()->GetMediaSection(1).GetProtocol())
      << "Wrong protocol for video";
  auto video_formats = Sdp()->GetMediaSection(1).GetFormats();
  ASSERT_EQ(4U, video_formats.size()) << "Wrong number of formats for video";
  ASSERT_EQ("120", video_formats[0]);
  ASSERT_EQ("121", video_formats[1]);
  ASSERT_EQ("122", video_formats[2]);
  ASSERT_EQ("123", video_formats[3]);

  ASSERT_EQ(SdpMediaSection::kAudio, Sdp()->GetMediaSection(2).GetMediaType())
      << "Wrong type for third media section";
}

TEST_P(NewSdpTest, CheckSetup) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kSetupAttribute));
  ASSERT_EQ(SdpSetupAttribute::kActpass,
            Sdp()->GetMediaSection(0).GetAttributeList().GetSetup().mRole);
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kSetupAttribute));
  ASSERT_EQ(SdpSetupAttribute::kActive,
            Sdp()->GetMediaSection(1).GetAttributeList().GetSetup().mRole);
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kSetupAttribute));
}

TEST_P(NewSdpTest, CheckSsrc) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kSsrcAttribute));
  auto ssrcs = Sdp()->GetMediaSection(0).GetAttributeList().GetSsrc().mSsrcs;
  ASSERT_EQ(1U, ssrcs.size());
  ASSERT_EQ(5150U, ssrcs[0].ssrc);
  ASSERT_EQ("", ssrcs[0].attribute);

  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kSsrcAttribute));
  ssrcs = Sdp()->GetMediaSection(1).GetAttributeList().GetSsrc().mSsrcs;
  ASSERT_EQ(3U, ssrcs.size());
  ASSERT_EQ(1111U, ssrcs[0].ssrc);
  ASSERT_EQ("foo", ssrcs[0].attribute);
  ASSERT_EQ(1111U, ssrcs[1].ssrc);
  ASSERT_EQ("foo:bar", ssrcs[1].attribute);
  ASSERT_EQ(1111U, ssrcs[2].ssrc);
  ASSERT_EQ(
      "msid:1d0cdb4e-5934-4f0f-9f88-40392cb60d31 "
      "315b086a-5cb6-4221-89de-caf0b038c79d",
      ssrcs[2].attribute);
}

TEST_P(NewSdpTest, CheckRtpmap) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  const SdpMediaSection& audiosec = Sdp()->GetMediaSection(0);
  const SdpRtpmapAttributeList& rtpmap =
      audiosec.GetAttributeList().GetRtpmap();
  ASSERT_EQ(5U, rtpmap.mRtpmaps.size())
      << "Wrong number of rtpmap attributes for audio";

  // Need to know name of type
  CheckRtpmap("109", SdpRtpmapAttributeList::kOpus, "opus", 48000, 2,
              audiosec.GetFormats()[0], rtpmap);

  CheckRtpmap("9", SdpRtpmapAttributeList::kG722, "G722", 8000, 1,
              audiosec.GetFormats()[1], rtpmap);

  CheckRtpmap("0", SdpRtpmapAttributeList::kPCMU, "PCMU", 8000, 1,
              audiosec.GetFormats()[2], rtpmap);

  CheckRtpmap("8", SdpRtpmapAttributeList::kPCMA, "PCMA", 8000, 1,
              audiosec.GetFormats()[3], rtpmap);

  CheckRtpmap("101", SdpRtpmapAttributeList::kTelephoneEvent, "telephone-event",
              8000, 1, audiosec.GetFormats()[4], rtpmap);

  const SdpMediaSection& videosec = Sdp()->GetMediaSection(1);
  const SdpRtpmapAttributeList videoRtpmap =
      videosec.GetAttributeList().GetRtpmap();
  ASSERT_EQ(4U, videoRtpmap.mRtpmaps.size())
      << "Wrong number of rtpmap attributes for video";

  CheckRtpmap("120", SdpRtpmapAttributeList::kVP8, "VP8", 90000, 0,
              videosec.GetFormats()[0], videoRtpmap);

  CheckRtpmap("121", SdpRtpmapAttributeList::kVP9, "VP9", 90000, 0,
              videosec.GetFormats()[1], videoRtpmap);

  CheckRtpmap("122", SdpRtpmapAttributeList::kRed, "red", 90000, 0,
              videosec.GetFormats()[2], videoRtpmap);

  CheckRtpmap("123", SdpRtpmapAttributeList::kUlpfec, "ulpfec", 90000, 0,
              videosec.GetFormats()[3], videoRtpmap);
}

MOZ_RUNINIT static const std::string kAudioWithTelephoneEvent =
    "v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
    "c=IN IP4 198.51.100.7" CRLF "t=0 0" CRLF
    "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=mid:first" CRLF "a=rtpmap:109 opus/48000/2" CRLF
    "a=fmtp:109 maxplaybackrate=32000;stereo=1" CRLF "a=ptime:20" CRLF
    "a=maxptime:20" CRLF "a=rtpmap:9 G722/8000" CRLF "a=rtpmap:0 PCMU/8000" CRLF
    "a=rtpmap:8 PCMA/8000" CRLF "a=rtpmap:101 telephone-event/8000" CRLF;

TEST_P(NewSdpTest, CheckTelephoneEventNoFmtp) {
  ParseSdp(kAudioWithTelephoneEvent);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto audio_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(1U, audio_format_params.size());

  // make sure we don't get a fmtp for codec 101
  for (size_t i = 0; i < audio_format_params.size(); ++i) {
    ASSERT_NE("101", audio_format_params[i].format);
  }
}

TEST_P(NewSdpTest, CheckTelephoneEventWithDefaultEvents) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0-15" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventWithBadCharacter) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0-5." CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventIncludingCommas) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0-15,66,67" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("0-15,66,67");
}

TEST_P(NewSdpTest, CheckTelephoneEventComplexEvents) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0,1,2-4,5-15,66,67" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("0,1,2-4,5-15,66,67");
}

TEST_P(NewSdpTest, CheckTelephoneEventNoHyphen) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 5,6,7" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("5,6,7");
}

TEST_P(NewSdpTest, CheckTelephoneEventOnlyZero) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("0");
}

TEST_P(NewSdpTest, CheckTelephoneEventOnlyOne) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 1" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  CheckDtmfFmtp("1");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadThreeDigit) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 123" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadThreeDigitWithHyphen) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 0-123" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadLeadingHyphen) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 -12" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadTrailingHyphen) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 12-" CRLF, false);
}

TEST_P(NewSdpTest, CheckTelephoneEventBadTrailingHyphenInMiddle) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 1,12-,4" CRLF, false);
}

TEST_P(NewSdpTest, CheckTelephoneEventBadLeadingComma) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 ,2,3" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadMultipleLeadingComma) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 ,,,2,3" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadConsecutiveCommas) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 1,,,,,,,,3" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadTrailingComma) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 1,2,3," CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadTwoHyphens) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 1-2-3" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadSixDigit) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 112233" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

TEST_P(NewSdpTest, CheckTelephoneEventBadRangeReversed) {
  ParseSdp(kAudioWithTelephoneEvent + "a=fmtp:101 33-2" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  // check for the default dtmf tones
  CheckDtmfFmtp("0-15");
}

MOZ_RUNINIT static const std::string kVideoWithRedAndUlpfecSdp =
    "v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
    "c=IN IP4 198.51.100.7" CRLF "t=0 0" CRLF
    "m=video 9 RTP/SAVPF 97 120 121 122 123" CRLF "c=IN IP6 ::1" CRLF
    "a=fingerprint:sha-1 "
    "DF:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:A0:72:9B:14:08:6D:0F:4C" CRLF
    "a=rtpmap:97 H264/90000" CRLF "a=fmtp:97 profile-level-id=42a01e" CRLF
    "a=rtpmap:120 VP8/90000" CRLF "a=fmtp:120 max-fs=3600;max-fr=30" CRLF
    "a=rtpmap:121 VP9/90000" CRLF "a=fmtp:121 max-fs=3600;max-fr=30" CRLF
    "a=rtpmap:122 red/90000" CRLF "a=rtpmap:123 ulpfec/90000" CRLF;

TEST_P(NewSdpTest, CheckRedNoFmtp) {
  ParseSdp(kVideoWithRedAndUlpfecSdp);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto video_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(3U, video_format_params.size());

  // make sure we don't get a fmtp for codec 122
  for (size_t i = 0; i < video_format_params.size(); ++i) {
    ASSERT_NE("122", video_format_params[i].format);
  }
}

TEST_P(NewSdpTest, CheckRedEmptyFmtp) {
  // if serializing and re-parsing, we expect errors
  if (::testing::get<0>(GetParam())) {
    ParseSdp(kVideoWithRedAndUlpfecSdp + "a=fmtp:122" CRLF, false);
  } else {
    ParseSdp(kVideoWithRedAndUlpfecSdp + "a=fmtp:122" CRLF, false);

    if (ResultsAreFromSipcc()) {
      ASSERT_NE(0U, ParseErrorCount());
    } else {
      // This is the branch for the rust parser to check for warnings, as the
      // rust parser will give a warning at that point instead of an error.
      ASSERT_NE(0U, ParseWarningCount());
    }
  }

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto video_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(3U, video_format_params.size());

  // make sure we don't get a fmtp for codec 122
  for (size_t i = 0; i < video_format_params.size(); ++i) {
    ASSERT_NE("122", video_format_params[i].format);
  }
}

TEST_P(NewSdpTest, CheckRedFmtpWith2Codecs) {
  ParseSdp(kVideoWithRedAndUlpfecSdp + "a=fmtp:122 120/121" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto video_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(4U, video_format_params.size());

  ASSERT_EQ("122", video_format_params[3].format);
  ASSERT_TRUE(!!video_format_params[3].parameters);
  ASSERT_EQ(SdpRtpmapAttributeList::kRed,
            video_format_params[3].parameters->codec_type);
  const SdpFmtpAttributeList::RedParameters* red_parameters(
      static_cast<SdpFmtpAttributeList::RedParameters*>(
          video_format_params[3].parameters.get()));
  ASSERT_EQ(2U, red_parameters->encodings.size());
  ASSERT_EQ(120U, red_parameters->encodings[0]);
  ASSERT_EQ(121U, red_parameters->encodings[1]);
}

TEST_P(NewSdpTest, CheckRedFmtpWith3Codecs) {
  ParseSdp(kVideoWithRedAndUlpfecSdp + "a=fmtp:122 120/121/123" CRLF);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto video_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(4U, video_format_params.size());

  ASSERT_EQ("122", video_format_params[3].format);
  ASSERT_TRUE(!!video_format_params[3].parameters);
  ASSERT_EQ(SdpRtpmapAttributeList::kRed,
            video_format_params[3].parameters->codec_type);
  const SdpFmtpAttributeList::RedParameters* red_parameters(
      static_cast<SdpFmtpAttributeList::RedParameters*>(
          video_format_params[3].parameters.get()));
  ASSERT_EQ(3U, red_parameters->encodings.size());
  ASSERT_EQ(120U, red_parameters->encodings[0]);
  ASSERT_EQ(121U, red_parameters->encodings[1]);
  ASSERT_EQ(123U, red_parameters->encodings[2]);
}

MOZ_RUNINIT const std::string kH264AudioVideoOffer =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=ice-ufrag:4a799b2e" CRLF
    "a=ice-pwd:e4cc12a910f106a0a744719425510e17" CRLF "a=ice-lite" CRLF
    "a=msid-semantic:WMS stream streama" CRLF
    "a=fingerprint:sha-256 "
    "DF:2E:AC:8A:FD:0A:8E:99:BF:5D:E8:3C:E7:FA:FB:08:3B:3C:54:1D:D7:D4:05:77:"
    "A0:72:9B:14:08:6D:0F:4C" CRLF "a=group:BUNDLE first second" CRLF
    "a=group:BUNDLE third" CRLF "a=group:LS first third" CRLF
    "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=mid:first" CRLF "a=rtpmap:109 opus/48000/2" CRLF "a=ptime:20" CRLF
    "a=maxptime:20" CRLF "a=rtpmap:9 G722/8000" CRLF "a=rtpmap:0 PCMU/8000" CRLF
    "a=rtpmap:8 PCMA/8000" CRLF "a=rtpmap:101 telephone-event/8000" CRLF
    "a=fmtp:109 maxplaybackrate=32000;stereo=1;useinbandfec=1" CRLF
    "a=fmtp:101 0-15,66,32-34,67" CRLF "a=ice-ufrag:00000000" CRLF
    "a=ice-pwd:0000000000000000000000000000000" CRLF "a=sendonly" CRLF
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level" CRLF
    "a=setup:actpass" CRLF "a=rtcp-mux" CRLF "a=msid:stream track" CRLF
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 62453 typ host" CRLF
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 62453 typ srflx raddr "
    "10.0.0.36 rport 62453" CRLF
    "a=candidate:3 1 UDP 100401151 162.222.183.171 49761 typ relay raddr "
    "162.222.183.171 rport 49761" CRLF
    "a=candidate:6 1 UDP 16515071 162.222.183.171 51858 typ relay raddr "
    "162.222.183.171 rport 51858" CRLF
    "a=candidate:3 2 UDP 100401150 162.222.183.171 62454 typ relay raddr "
    "162.222.183.171 rport 62454" CRLF
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 55428 typ srflx raddr "
    "10.0.0.36 rport 55428" CRLF
    "a=candidate:6 2 UDP 16515070 162.222.183.171 50340 typ relay raddr "
    "162.222.183.171 rport 50340" CRLF
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 55428 typ host" CRLF
    "m=video 9 RTP/SAVPF 97 98 120" CRLF "c=IN IP6 ::1" CRLF "a=mid:second" CRLF
    "a=rtpmap:97 H264/90000" CRLF "a=fmtp:97 profile-level-id=42a01e" CRLF
    "a=rtpmap:98 H264/90000" CRLF
    "a=fmtp:98 "
    "PROFILE=0;LEVEL=0;parameter-add=1;profile-level-id=42a00d;packetization-"
    "mode=1;level-asymmetry-allowed=1;max-mbps=42000;max-fs=1400;max-cpb=1000;"
    "max-dpb=1000;max-br=180000;usedtx=0;stereo=0;useinbandfec=0;cbr=0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF "a=fmtp:120 max-fs=3601;max-fr=31" CRLF
    "a=recvonly" CRLF "a=setup:active" CRLF "a=rtcp-mux" CRLF
    "a=msid:streama tracka" CRLF "a=msid:streamb trackb" CRLF
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 59530 typ host" CRLF
    "a=candidate:0 2 UDP 2130379006 10.0.0.36 64378 typ host" CRLF
    "a=candidate:2 2 UDP 1694236670 24.6.134.204 64378 typ srflx raddr "
    "10.0.0.36 rport 64378" CRLF
    "a=candidate:6 2 UDP 16515070 162.222.183.171 64941 typ relay raddr "
    "162.222.183.171 rport 64941" CRLF
    "a=candidate:6 1 UDP 16515071 162.222.183.171 64800 typ relay raddr "
    "162.222.183.171 rport 64800" CRLF
    "a=candidate:2 1 UDP 1694236671 24.6.134.204 59530 typ srflx raddr "
    "10.0.0.36 rport 59530" CRLF
    "a=candidate:3 1 UDP 100401151 162.222.183.171 62935 typ relay raddr "
    "162.222.183.171 rport 62935" CRLF
    "a=candidate:3 2 UDP 100401150 162.222.183.171 61026 typ relay raddr "
    "162.222.183.171 rport 61026" CRLF "m=audio 9 RTP/SAVPF 0" CRLF
    "a=mid:third" CRLF "a=rtpmap:0 PCMU/8000" CRLF "a=msid:noappdata" CRLF;

TEST_P(NewSdpTest, CheckFormatParameters) {
  ParseSdp(kH264AudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto audio_format_params =
      Sdp()->GetMediaSection(0).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(2U, audio_format_params.size());
  ASSERT_EQ("109", audio_format_params[0].format);
  ASSERT_TRUE(!!audio_format_params[0].parameters);
  const SdpFmtpAttributeList::OpusParameters* opus_parameters =
      static_cast<SdpFmtpAttributeList::OpusParameters*>(
          audio_format_params[0].parameters.get());
  ASSERT_EQ(32000U, opus_parameters->maxplaybackrate);
  ASSERT_EQ(1U, opus_parameters->stereo);
  ASSERT_EQ(1U, opus_parameters->useInBandFec);
  ASSERT_EQ(0U, opus_parameters->maxAverageBitrate);
  ASSERT_EQ(0U, opus_parameters->useDTX);
  ASSERT_EQ(0U, opus_parameters->useCbr);
  ASSERT_EQ(0U, opus_parameters->frameSizeMs);
  ASSERT_EQ(0U, opus_parameters->minFrameSizeMs);
  ASSERT_EQ(0U, opus_parameters->maxFrameSizeMs);
  ASSERT_EQ("101", audio_format_params[1].format);
  ASSERT_TRUE(!!audio_format_params[1].parameters);
  const SdpFmtpAttributeList::TelephoneEventParameters* te_parameters =
      static_cast<SdpFmtpAttributeList::TelephoneEventParameters*>(
          audio_format_params[1].parameters.get());
  ASSERT_NE(0U, te_parameters->dtmfTones.size());
  ASSERT_EQ("0-15,66,32-34,67", te_parameters->dtmfTones);

  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  auto video_format_params =
      Sdp()->GetMediaSection(1).GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(3U, video_format_params.size());
  ASSERT_EQ("97", video_format_params[0].format);
  ASSERT_TRUE(!!video_format_params[0].parameters);
  ASSERT_EQ(SdpRtpmapAttributeList::kH264,
            video_format_params[0].parameters->codec_type);
  const SdpFmtpAttributeList::H264Parameters* h264_parameters(
      static_cast<SdpFmtpAttributeList::H264Parameters*>(
          video_format_params[0].parameters.get()));
  ASSERT_EQ((uint32_t)0x42a01e, h264_parameters->profile_level_id);
  ASSERT_EQ(0U, h264_parameters->packetization_mode);
  ASSERT_FALSE(static_cast<bool>(h264_parameters->level_asymmetry_allowed));
  ASSERT_EQ(0U, h264_parameters->max_mbps);
  ASSERT_EQ(0U, h264_parameters->max_fs);
  ASSERT_EQ(0U, h264_parameters->max_cpb);
  ASSERT_EQ(0U, h264_parameters->max_dpb);
  ASSERT_EQ(0U, h264_parameters->max_br);

  ASSERT_EQ("98", video_format_params[1].format);
  ASSERT_TRUE(!!video_format_params[1].parameters);
  ASSERT_EQ(SdpRtpmapAttributeList::kH264,
            video_format_params[1].parameters->codec_type);
  h264_parameters = static_cast<SdpFmtpAttributeList::H264Parameters*>(
      video_format_params[1].parameters.get());
  ASSERT_EQ((uint32_t)0x42a00d, h264_parameters->profile_level_id);
  ASSERT_EQ(1U, h264_parameters->packetization_mode);
  ASSERT_TRUE(static_cast<bool>(h264_parameters->level_asymmetry_allowed));
  ASSERT_EQ(42000U, h264_parameters->max_mbps);
  ASSERT_EQ(1400U, h264_parameters->max_fs);
  ASSERT_EQ(1000U, h264_parameters->max_cpb);
  ASSERT_EQ(1000U, h264_parameters->max_dpb);
  ASSERT_EQ(180000U, h264_parameters->max_br);

  ASSERT_EQ("120", video_format_params[2].format);
  ASSERT_TRUE(!!video_format_params[2].parameters);
  ASSERT_EQ(SdpRtpmapAttributeList::kVP8,
            video_format_params[2].parameters->codec_type);
  const SdpFmtpAttributeList::VP8Parameters* vp8_parameters =
      static_cast<SdpFmtpAttributeList::VP8Parameters*>(
          video_format_params[2].parameters.get());
  ASSERT_EQ(3601U, vp8_parameters->max_fs);
  ASSERT_EQ(31U, vp8_parameters->max_fr);

  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
}

MOZ_RUNINIT const std::string kOpusFmtpWithPtime =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "m=audio 9 RTP/SAVPF 109" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:109 opus/48000/2" CRLF
    "a=fmtp:109 ptime=40;minptime=10;maxptime=60;useinbandfec=1" CRLF;

TEST_P(NewSdpTest, CheckOpusFmtpPtime) {
  ParseSdp(kOpusFmtpWithPtime);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();

  const auto* params = Sdp()->GetMediaSection(0).FindFmtp("109");
  ASSERT_TRUE(params);
  ASSERT_EQ(SdpRtpmapAttributeList::kOpus, params->codec_type);

  const auto& opus =
      static_cast<const SdpFmtpAttributeList::OpusParameters&>(*params);
  ASSERT_EQ(40U, opus.frameSizeMs);
  ASSERT_EQ(10U, opus.minFrameSizeMs);
  ASSERT_EQ(60U, opus.maxFrameSizeMs);
  ASSERT_EQ(1U, opus.useInBandFec);
}

TEST_P(NewSdpTest, CheckPtime) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_EQ(20U, Sdp()->GetMediaSection(0).GetAttributeList().GetPtime());
  ASSERT_FALSE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kPtimeAttribute));
}

TEST_P(NewSdpTest, CheckFlags) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kIceLiteAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kIceLiteAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kIceLiteAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kIceLiteAttribute));

  ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kExtmapAllowMixedAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kExtmapAllowMixedAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kExtmapAllowMixedAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kExtmapAllowMixedAttribute));

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kRtcpMuxAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kRtcpMuxAttribute));

  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kBundleOnlyAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kBundleOnlyAttribute));

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kEndOfCandidatesAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kEndOfCandidatesAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kEndOfCandidatesAttribute));
}

TEST_P(NewSdpTest, CheckConnectionLines) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  const SdpConnection& conn1 = Sdp()->GetMediaSection(0).GetConnection();
  ASSERT_EQ(sdp::kIPv4, conn1.GetAddrType());
  ASSERT_EQ("0.0.0.0", conn1.GetAddress());
  ASSERT_EQ(0U, conn1.GetTtl());
  ASSERT_EQ(0U, conn1.GetCount());

  const SdpConnection& conn2 = Sdp()->GetMediaSection(1).GetConnection();
  ASSERT_EQ(sdp::kIPv6, conn2.GetAddrType());
  ASSERT_EQ("::1", conn2.GetAddress());
  ASSERT_EQ(0U, conn2.GetTtl());
  ASSERT_EQ(0U, conn2.GetCount());

  // tests that we can fall through to session level as appropriate
  const SdpConnection& conn3 = Sdp()->GetMediaSection(2).GetConnection();
  ASSERT_EQ(sdp::kIPv4, conn3.GetAddrType());
  ASSERT_EQ("224.0.0.1", conn3.GetAddress());
  ASSERT_EQ(100U, conn3.GetTtl());
  ASSERT_EQ(12U, conn3.GetCount());
}

TEST_P(NewSdpTest, CheckDirections) {
  ParseSdp(kBasicAudioVideoOffer);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(SdpDirectionAttribute::kSendonly,
            Sdp()->GetMediaSection(0).GetAttributeList().GetDirection());
  ASSERT_EQ(SdpDirectionAttribute::kRecvonly,
            Sdp()->GetMediaSection(1).GetAttributeList().GetDirection());
  ASSERT_EQ(SdpDirectionAttribute::kSendrecv,
            Sdp()->GetMediaSection(2).GetAttributeList().GetDirection());
}

TEST_P(NewSdpTest, CheckCandidates) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kCandidateAttribute));
  auto audio_candidates =
      Sdp()->GetMediaSection(0).GetAttributeList().GetCandidate();
  ASSERT_EQ(8U, audio_candidates.size());
  ASSERT_EQ("0 1 UDP 2130379007 10.0.0.36 62453 typ host", audio_candidates[0]);
  ASSERT_EQ(
      "2 1 UDP 1694236671 24.6.134.204 62453 typ srflx raddr 10.0.0.36 rport "
      "62453",
      audio_candidates[1]);
  ASSERT_EQ(
      "3 1 UDP 100401151 162.222.183.171 49761 typ relay raddr 162.222.183.171 "
      "rport 49761",
      audio_candidates[2]);
  ASSERT_EQ(
      "6 1 UDP 16515071 162.222.183.171 51858 typ relay raddr 162.222.183.171 "
      "rport 51858",
      audio_candidates[3]);
  ASSERT_EQ(
      "3 2 UDP 100401150 162.222.183.171 62454 typ relay raddr 162.222.183.171 "
      "rport 62454",
      audio_candidates[4]);
  ASSERT_EQ(
      "2 2 UDP 1694236670 24.6.134.204 55428 typ srflx raddr 10.0.0.36 rport "
      "55428",
      audio_candidates[5]);
  ASSERT_EQ(
      "6 2 UDP 16515070 162.222.183.171 50340 typ relay raddr 162.222.183.171 "
      "rport 50340",
      audio_candidates[6]);
  ASSERT_EQ("0 2 UDP 2130379006 10.0.0.36 55428 typ host", audio_candidates[7]);

  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kCandidateAttribute));
  auto video_candidates =
      Sdp()->GetMediaSection(1).GetAttributeList().GetCandidate();
  ASSERT_EQ(8U, video_candidates.size());
  ASSERT_EQ("0 1 UDP 2130379007 10.0.0.36 59530 typ host", video_candidates[0]);
  ASSERT_EQ("0 2 UDP 2130379006 10.0.0.36 64378 typ host", video_candidates[1]);
  ASSERT_EQ(
      "2 2 UDP 1694236670 24.6.134.204 64378 typ srflx raddr 10.0.0.36 rport "
      "64378",
      video_candidates[2]);
  ASSERT_EQ(
      "6 2 UDP 16515070 162.222.183.171 64941 typ relay raddr 162.222.183.171 "
      "rport 64941",
      video_candidates[3]);
  ASSERT_EQ(
      "6 1 UDP 16515071 162.222.183.171 64800 typ relay raddr 162.222.183.171 "
      "rport 64800",
      video_candidates[4]);
  ASSERT_EQ(
      "2 1 UDP 1694236671 24.6.134.204 59530 typ srflx raddr 10.0.0.36 rport "
      "59530",
      video_candidates[5]);
  ASSERT_EQ(
      "3 1 UDP 100401151 162.222.183.171 62935 typ relay raddr 162.222.183.171 "
      "rport 62935",
      video_candidates[6]);
  ASSERT_EQ(
      "3 2 UDP 100401150 162.222.183.171 61026 typ relay raddr 162.222.183.171 "
      "rport 61026",
      video_candidates[7]);

  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kCandidateAttribute));
}

TEST_P(NewSdpTest, CheckMid) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_EQ("first", Sdp()->GetMediaSection(0).GetAttributeList().GetMid());
  ASSERT_EQ("second", Sdp()->GetMediaSection(1).GetAttributeList().GetMid());
  ASSERT_EQ("third", Sdp()->GetMediaSection(2).GetAttributeList().GetMid());
}

TEST_P(NewSdpTest, CheckMsid) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kMsidSemanticAttribute));
  auto semantics = Sdp()->GetAttributeList().GetMsidSemantic().mMsidSemantics;
  ASSERT_EQ(2U, semantics.size());
  ASSERT_EQ("WMS", semantics[0].semantic);
  ASSERT_EQ(2U, semantics[0].msids.size());
  ASSERT_EQ("stream", semantics[0].msids[0]);
  ASSERT_EQ("streama", semantics[0].msids[1]);
  ASSERT_EQ("foo", semantics[1].semantic);
  ASSERT_EQ(1U, semantics[1].msids.size());
  ASSERT_EQ("stream", semantics[1].msids[0]);

  const SdpMsidAttributeList& msids1 =
      Sdp()->GetMediaSection(0).GetAttributeList().GetMsid();
  ASSERT_EQ(1U, msids1.mMsids.size());
  ASSERT_EQ("stream", msids1.mMsids[0].identifier);
  ASSERT_EQ("track", msids1.mMsids[0].appdata);
  const SdpMsidAttributeList& msids2 =
      Sdp()->GetMediaSection(1).GetAttributeList().GetMsid();
  ASSERT_EQ(2U, msids2.mMsids.size());
  ASSERT_EQ("streama", msids2.mMsids[0].identifier);
  ASSERT_EQ("tracka", msids2.mMsids[0].appdata);
  ASSERT_EQ("streamb", msids2.mMsids[1].identifier);
  ASSERT_EQ("trackb", msids2.mMsids[1].appdata);
  const SdpMsidAttributeList& msids3 =
      Sdp()->GetMediaSection(2).GetAttributeList().GetMsid();
  ASSERT_EQ(1U, msids3.mMsids.size());
  ASSERT_EQ("noappdata", msids3.mMsids[0].identifier);
  ASSERT_EQ("", msids3.mMsids[0].appdata);
}

TEST_P(NewSdpTest, CheckManyMsidSemantics) {
  if (::testing::get<0>(GetParam())) {
    return;
  }
  std::ostringstream msid;
  msid << "a=msid-semantic: foo";
  for (size_t i = 0; i < SDP_MAX_MEDIA_STREAMS + 1; ++i) {
    msid << " " << i;
  }
  msid << CRLF;

  std::string offer =
      "v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
      "c=IN IP4 198.51.100.7" CRLF "t=0 0" CRLF;
  offer += msid.str();
  offer += "m=video 56436 RTP/SAVPF 120" CRLF "b=CT:1000" CRLF
           "a=rtpmap:120 VP8/90000" CRLF;

  ParseSdp(offer);
  auto semantics = Sdp()->GetAttributeList().GetMsidSemantic().mMsidSemantics;
  ASSERT_EQ(1U, semantics.size());

  // Only sipcc is limited by SDP_MAX_MEDIA_STREAMS, the Rust parser
  // will parse all of the msids.
  ASSERT_GE(semantics[0].msids.size(),
            static_cast<size_t>(SDP_MAX_MEDIA_STREAMS));
}

TEST_P(NewSdpTest, CheckRid) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_FALSE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRidAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kRidAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kRidAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kRidAttribute));

  const SdpRidAttributeList& rids =
      Sdp()->GetMediaSection(1).GetAttributeList().GetRid();

  ASSERT_EQ(2U, rids.mRids.size());

  ASSERT_EQ("bar", rids.mRids[0].id);
  ASSERT_EQ(sdp::kRecv, rids.mRids[0].direction);
  ASSERT_EQ(1U, rids.mRids[0].formats.size());
  ASSERT_EQ(120U, rids.mRids[0].formats[0]);
  ASSERT_EQ(800U, rids.mRids[0].constraints.maxWidth);
  ASSERT_EQ(600U, rids.mRids[0].constraints.maxHeight);

  ASSERT_EQ("bar123", rids.mRids[1].id);
  ASSERT_EQ(sdp::kRecv, rids.mRids[1].direction);
  ASSERT_EQ(0U, rids.mRids[1].formats.size());
  ASSERT_EQ(1920U, rids.mRids[1].constraints.maxWidth);
  ASSERT_EQ(1080U, rids.mRids[1].constraints.maxHeight);
}

TEST_P(NewSdpTest, CheckMediaLevelIceUfrag) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kIceUfragAttribute, true));
  ASSERT_EQ("00000000",
            Sdp()->GetMediaSection(0).GetAttributeList().GetIceUfrag());

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kIceUfragAttribute, false));

  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kIceUfragAttribute, true));
  ASSERT_EQ("4a799b2e",
            Sdp()->GetMediaSection(1).GetAttributeList().GetIceUfrag());
}

TEST_P(NewSdpTest, CheckMediaLevelIcePwd) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kIcePwdAttribute));
  ASSERT_EQ("0000000000000000000000000000000",
            Sdp()->GetMediaSection(0).GetAttributeList().GetIcePwd());

  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kIcePwdAttribute));
  ASSERT_EQ("e4cc12a910f106a0a744719425510e17",
            Sdp()->GetMediaSection(1).GetAttributeList().GetIcePwd());
}

TEST_P(NewSdpTest, CheckGroups) {
  ParseSdp(kBasicAudioVideoOffer);
  const SdpGroupAttributeList& group = Sdp()->GetAttributeList().GetGroup();
  const SdpGroupAttributeList::Group& group1 = group.mGroups[0];
  ASSERT_EQ(SdpGroupAttributeList::kBundle, group1.semantics);
  ASSERT_EQ(2U, group1.tags.size());
  ASSERT_EQ("first", group1.tags[0]);
  ASSERT_EQ("second", group1.tags[1]);

  const SdpGroupAttributeList::Group& group2 = group.mGroups[1];
  ASSERT_EQ(SdpGroupAttributeList::kBundle, group2.semantics);
  ASSERT_EQ(1U, group2.tags.size());
  ASSERT_EQ("third", group2.tags[0]);

  const SdpGroupAttributeList::Group& group3 = group.mGroups[2];
  ASSERT_EQ(SdpGroupAttributeList::kLs, group3.semantics);
  ASSERT_EQ(2U, group3.tags.size());
  ASSERT_EQ("first", group3.tags[0]);
  ASSERT_EQ("third", group3.tags[1]);
}

TEST_P(NewSdpTest, CheckManyGroups) {
  std::ostringstream bundle;
  bundle << "a=group:BUNDLE";
  for (size_t i = 0; i < SDP_MAX_MEDIA_STREAMS + 1; ++i) {
    bundle << " " << i;
  }
  bundle << CRLF;

  std::string offer =
      "v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
      "c=IN IP4 198.51.100.7" CRLF "t=0 0" CRLF;
  offer += bundle.str();
  offer += "m=video 56436 RTP/SAVPF 120" CRLF "b=CT:1000" CRLF
           "a=rtpmap:120 VP8/90000" CRLF;
  // Rust will reserialize what Sipcc serializes to, but otherwise they will
  // differ.
  ParseSdp(offer, true,
           ::testing::get<0>(GetParam()) && ::testing::get<1>(GetParam()));
  const SdpGroupAttributeList& group = Sdp()->GetAttributeList().GetGroup();
  const SdpGroupAttributeList::Group& group1 = group.mGroups[0];
  ASSERT_EQ(SdpGroupAttributeList::kBundle, group1.semantics);

  // Only sipcc is limited by SDP_MAX_MEDIA_STREAMS, the Rust parser
  // will parse all of the groups.
  ASSERT_GE(group1.tags.size(), static_cast<size_t>(SDP_MAX_MEDIA_STREAMS));
}

// SDP from a basic A/V call with data channel FFX/FFX
MOZ_RUNINIT const std::string kBasicAudioVideoDataOffer =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 27987 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "t=0 0" CRLF "a=ice-ufrag:8a39d2ae" CRLF
    "a=ice-pwd:601d53aba51a318351b3ecf5ee00048f" CRLF
    "a=fingerprint:sha-256 "
    "30:FF:8E:2B:AC:9D:ED:70:18:10:67:C8:AE:9E:68:F3:86:53:51:B0:AC:31:B7:BE:"
    "6D:CF:A4:2E:D3:6E:B4:28" CRLF "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:109 opus/48000/2" CRLF "a=ptime:20" CRLF
    "a=rtpmap:9 G722/8000" CRLF "a=rtpmap:0 PCMU/8000" CRLF
    "a=rtpmap:8 PCMA/8000" CRLF "a=rtpmap:101 telephone-event/8000" CRLF
    "a=fmtp:101 0-15" CRLF "a=sendrecv" CRLF
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level" CRLF
    "a=extmap:2/sendonly some_extension" CRLF
    "a=extmap:3 some_other_extension some_params some more params" CRLF
    "a=setup:actpass" CRLF "a=rtcp-mux" CRLF
    "m=video 9 RTP/SAVPF 120 126 97" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF "a=rtpmap:126 H264/90000" CRLF
    "a=fmtp:126 profile-level-id=42e01f;packetization-mode=1" CRLF
    "a=rtpmap:97 H264/90000" CRLF "a=fmtp:97 profile-level-id=42e01f" CRLF
    "a=sendrecv" CRLF
    // sipcc barfs on this, despite that it is valid syntax
    // Do we care about fixing?
    //"a=rtcp-fb:120 ack" CRLF // Should be ignored by sipcc
    "a=rtcp-fb:120 ack rpsi" CRLF "a=rtcp-fb:120 ack app foo" CRLF
    "a=rtcp-fb:120 ack foo" CRLF  // Should be ignored
    "a=rtcp-fb:120 nack" CRLF "a=rtcp-fb:120 nack sli" CRLF
    "a=rtcp-fb:120 nack pli" CRLF "a=rtcp-fb:120 nack rpsi" CRLF
    "a=rtcp-fb:120 nack app foo" CRLF
    "a=rtcp-fb:120 nack foo" CRLF  // Should be ignored
    "a=rtcp-fb:120 ccm fir" CRLF "a=rtcp-fb:120 ccm tmmbr" CRLF
    "a=rtcp-fb:120 ccm tstr" CRLF "a=rtcp-fb:120 ccm vbcm" CRLF
    "a=rtcp-fb:120 ccm foo" CRLF  // Should be ignored
    "a=rtcp-fb:120 trr-int 10" CRLF "a=rtcp-fb:120 goog-remb" CRLF
    "a=rtcp-fb:120 foo" CRLF  // Should be ignored
    "a=rtcp-fb:126 nack" CRLF "a=rtcp-fb:126 nack pli" CRLF
    "a=rtcp-fb:126 ccm fir" CRLF "a=rtcp-fb:97 nack" CRLF
    "a=rtcp-fb:97 nack pli" CRLF "a=rtcp-fb:97 ccm fir" CRLF
    "a=rtcp-fb:* ccm tmmbr" CRLF "a=rtcp-fb:120 transport-cc" CRLF
    "a=setup:actpass" CRLF "a=rtcp-mux" CRLF
    "m=application 9 DTLS/SCTP 5000" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=sctpmap:5000 webrtc-datachannel 16" CRLF "a=setup:actpass" CRLF;

TEST_P(NewSdpTest, BasicAudioVideoDataSdpParse) {
  ParseSdp(kBasicAudioVideoDataOffer, true);
  ASSERT_EQ(0U, ParseErrorCount())
      << "Got parse errors: " << SerializeParseErrors();
}

TEST_P(NewSdpTest, CheckApplicationParameters) {
  ParseSdp(kBasicAudioVideoDataOffer, true);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";
  ASSERT_EQ(SdpMediaSection::kAudio, Sdp()->GetMediaSection(0).GetMediaType())
      << "Wrong type for first media section";
  ASSERT_EQ(SdpMediaSection::kVideo, Sdp()->GetMediaSection(1).GetMediaType())
      << "Wrong type for second media section";
  ASSERT_EQ(SdpMediaSection::kApplication,
            Sdp()->GetMediaSection(2).GetMediaType())
      << "Wrong type for third media section";

  ASSERT_EQ(SdpMediaSection::kDtlsSctp, Sdp()->GetMediaSection(2).GetProtocol())
      << "Wrong protocol for application";
  auto app_formats = Sdp()->GetMediaSection(2).GetFormats();
  ASSERT_EQ(1U, app_formats.size()) << "Wrong number of formats for audio";
  ASSERT_EQ("5000", app_formats[0]);

  const SdpConnection& conn3 = Sdp()->GetMediaSection(2).GetConnection();
  ASSERT_EQ(sdp::kIPv4, conn3.GetAddrType());
  ASSERT_EQ("0.0.0.0", conn3.GetAddress());
  ASSERT_EQ(0U, conn3.GetTtl());
  ASSERT_EQ(0U, conn3.GetCount());

  ASSERT_TRUE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kSetupAttribute));
  ASSERT_EQ(SdpSetupAttribute::kActpass,
            Sdp()->GetMediaSection(2).GetAttributeList().GetSetup().mRole);
}

TEST_P(NewSdpTest, CheckExtmap) {
  ParseSdp(kBasicAudioVideoDataOffer, true);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kExtmapAttribute));

  auto extmaps =
      Sdp()->GetMediaSection(0).GetAttributeList().GetExtmap().mExtmaps;
  ASSERT_EQ(3U, extmaps.size());

  ASSERT_EQ(1U, extmaps[0].entry);
  ASSERT_FALSE(extmaps[0].direction_specified);
  ASSERT_EQ("urn:ietf:params:rtp-hdrext:ssrc-audio-level"_ns,
            extmaps[0].extensionname);
  ASSERT_EQ(""_ns, extmaps[0].extensionattributes);

  ASSERT_EQ(2U, extmaps[1].entry);
  ASSERT_TRUE(extmaps[1].direction_specified);
  ASSERT_EQ(SdpDirectionAttribute::kSendonly, extmaps[1].direction);
  ASSERT_EQ("some_extension"_ns, extmaps[1].extensionname);
  ASSERT_EQ(""_ns, extmaps[1].extensionattributes);

  ASSERT_EQ(3U, extmaps[2].entry);
  ASSERT_FALSE(extmaps[2].direction_specified);
  ASSERT_EQ("some_other_extension"_ns, extmaps[2].extensionname);
  ASSERT_EQ("some_params some more params"_ns, extmaps[2].extensionattributes);
}

TEST_P(NewSdpTest, CheckRtcpFb) {
  ParseSdp(kBasicAudioVideoDataOffer, true);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  auto& video_attrs = Sdp()->GetMediaSection(1).GetAttributeList();
  ASSERT_TRUE(video_attrs.HasAttribute(SdpAttribute::kRtcpFbAttribute));
  auto& rtcpfbs = video_attrs.GetRtcpFb().mFeedbacks;

  if (ResultsAreFromSipcc()) {
    ASSERT_EQ(21U, rtcpfbs.size());
  } else {
    ASSERT_EQ("WEBRTCSDP", mResults->ParserName());
    std::stringstream os;
    Sdp()->Serialize(os);
    // Reserialized results should be the same
    if (::testing::get<1>(GetParam())) {
      ASSERT_EQ(21U, rtcpfbs.size());
    } else {
      ASSERT_EQ(21U, rtcpfbs.size());
    }
  }

  CheckRtcpFb(rtcpfbs[0], "120", SdpRtcpFbAttributeList::kAck, "rpsi");
  CheckRtcpFb(rtcpfbs[1], "120", SdpRtcpFbAttributeList::kAck, "app", "foo");
  CheckRtcpFb(rtcpfbs[2], "120", SdpRtcpFbAttributeList::kNack, "");
  CheckRtcpFb(rtcpfbs[3], "120", SdpRtcpFbAttributeList::kNack, "sli");
  CheckRtcpFb(rtcpfbs[4], "120", SdpRtcpFbAttributeList::kNack, "pli");
  CheckRtcpFb(rtcpfbs[5], "120", SdpRtcpFbAttributeList::kNack, "rpsi");
  CheckRtcpFb(rtcpfbs[6], "120", SdpRtcpFbAttributeList::kNack, "app", "foo");
  CheckRtcpFb(rtcpfbs[7], "120", SdpRtcpFbAttributeList::kCcm, "fir");
  CheckRtcpFb(rtcpfbs[8], "120", SdpRtcpFbAttributeList::kCcm, "tmmbr");
  CheckRtcpFb(rtcpfbs[9], "120", SdpRtcpFbAttributeList::kCcm, "tstr");
  CheckRtcpFb(rtcpfbs[10], "120", SdpRtcpFbAttributeList::kCcm, "vbcm");
  CheckRtcpFb(rtcpfbs[11], "120", SdpRtcpFbAttributeList::kTrrInt, "10");
  CheckRtcpFb(rtcpfbs[12], "120", SdpRtcpFbAttributeList::kRemb, "");
  CheckRtcpFb(rtcpfbs[13], "126", SdpRtcpFbAttributeList::kNack, "");
  CheckRtcpFb(rtcpfbs[14], "126", SdpRtcpFbAttributeList::kNack, "pli");
  CheckRtcpFb(rtcpfbs[15], "126", SdpRtcpFbAttributeList::kCcm, "fir");
  CheckRtcpFb(rtcpfbs[16], "97", SdpRtcpFbAttributeList::kNack, "");
  CheckRtcpFb(rtcpfbs[17], "97", SdpRtcpFbAttributeList::kNack, "pli");
  CheckRtcpFb(rtcpfbs[18], "97", SdpRtcpFbAttributeList::kCcm, "fir");
  CheckRtcpFb(rtcpfbs[19], "*", SdpRtcpFbAttributeList::kCcm, "tmmbr");
  CheckRtcpFb(rtcpfbs[20], "120", SdpRtcpFbAttributeList::kTransportCC, "");
}

TEST_P(NewSdpTest, CheckRtcp) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_FALSE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRtcpAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kRtcpAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kRtcpAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kRtcpAttribute));

  auto& rtcpAttr_0 = Sdp()->GetMediaSection(0).GetAttributeList().GetRtcp();
  ASSERT_EQ(62454U, rtcpAttr_0.mPort);
  ASSERT_EQ(sdp::kInternet, rtcpAttr_0.mNetType);
  ASSERT_EQ(sdp::kIPv4, rtcpAttr_0.mAddrType);
  ASSERT_EQ("162.222.183.171", rtcpAttr_0.mAddress);

  auto& rtcpAttr_1 = Sdp()->GetMediaSection(1).GetAttributeList().GetRtcp();
  ASSERT_EQ(61026U, rtcpAttr_1.mPort);
  ASSERT_EQ("", rtcpAttr_1.mAddress);
}

TEST_P(NewSdpTest, CheckImageattr) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kImageattrAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kImageattrAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kImageattrAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kImageattrAttribute));

  const SdpImageattrAttributeList& imageattrs =
      Sdp()->GetMediaSection(1).GetAttributeList().GetImageattr();

  ASSERT_EQ(2U, imageattrs.mImageattrs.size());
  const SdpImageattrAttributeList::Imageattr& imageattr_0(
      imageattrs.mImageattrs[0]);
  ASSERT_TRUE(imageattr_0.pt.isSome());
  ASSERT_EQ(120U, *imageattr_0.pt);
  ASSERT_TRUE(imageattr_0.sendAll);
  ASSERT_TRUE(imageattr_0.recvAll);

  const SdpImageattrAttributeList::Imageattr& imageattr_1(
      imageattrs.mImageattrs[1]);
  ASSERT_TRUE(imageattr_1.pt.isSome());
  ASSERT_EQ(121U, *imageattr_1.pt);
  ASSERT_FALSE(imageattr_1.sendAll);
  ASSERT_FALSE(imageattr_1.recvAll);
  ASSERT_EQ(1U, imageattr_1.sendSets.size());
  ASSERT_EQ(1U, imageattr_1.sendSets[0].xRange.discreteValues.size());
  ASSERT_EQ(640U, imageattr_1.sendSets[0].xRange.discreteValues.front());
  ASSERT_EQ(1U, imageattr_1.sendSets[0].yRange.discreteValues.size());
  ASSERT_EQ(480U, imageattr_1.sendSets[0].yRange.discreteValues.front());
  ASSERT_EQ(1U, imageattr_1.recvSets.size());
  ASSERT_EQ(1U, imageattr_1.recvSets[0].xRange.discreteValues.size());
  ASSERT_EQ(640U, imageattr_1.recvSets[0].xRange.discreteValues.front());
  ASSERT_EQ(1U, imageattr_1.recvSets[0].yRange.discreteValues.size());
  ASSERT_EQ(480U, imageattr_1.recvSets[0].yRange.discreteValues.front());
}

TEST_P(NewSdpTest, CheckSimulcast) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp());
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kSimulcastAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kSimulcastAttribute));
  ASSERT_TRUE(Sdp()->GetMediaSection(1).GetAttributeList().HasAttribute(
      SdpAttribute::kSimulcastAttribute));
  ASSERT_FALSE(Sdp()->GetMediaSection(2).GetAttributeList().HasAttribute(
      SdpAttribute::kSimulcastAttribute));

  const SdpSimulcastAttribute& simulcast =
      Sdp()->GetMediaSection(1).GetAttributeList().GetSimulcast();

  ASSERT_EQ(2U, simulcast.recvVersions.size());
  ASSERT_EQ(0U, simulcast.sendVersions.size());
  ASSERT_EQ(1U, simulcast.recvVersions[0].choices.size());
  ASSERT_EQ("bar", simulcast.recvVersions[0].choices[0].rid);
  ASSERT_EQ(1U, simulcast.recvVersions[1].choices.size());
  ASSERT_EQ("bar123", simulcast.recvVersions[1].choices[0].rid);
}

TEST_P(NewSdpTest, CheckSctpmap) {
  ParseSdp(kBasicAudioVideoDataOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  const SdpMediaSection& appsec = Sdp()->GetMediaSection(2);
  ASSERT_TRUE(
      appsec.GetAttributeList().HasAttribute(SdpAttribute::kSctpmapAttribute));
  const SdpSctpmapAttributeList& sctpmap =
      appsec.GetAttributeList().GetSctpmap();

  ASSERT_EQ(1U, sctpmap.mSctpmaps.size())
      << "Wrong number of sctpmap attributes";
  ASSERT_EQ(1U, appsec.GetFormats().size());

  // Need to know name of type
  CheckSctpmap("5000", "webrtc-datachannel", 16, appsec.GetFormats()[0],
               sctpmap);
}

TEST_P(NewSdpTest, CheckMaxPtime) {
  ParseSdp(kBasicAudioVideoOffer);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kMaxptimeAttribute));
  ASSERT_EQ(Sdp()->GetMediaSection(0).GetAttributeList().GetMaxptime(), 20U);
}

MOZ_RUNINIT const std::string kNewSctpportOfferDraft21 =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 27987 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "t=0 0" CRLF "a=ice-ufrag:8a39d2ae" CRLF
    "a=ice-pwd:601d53aba51a318351b3ecf5ee00048f" CRLF
    "a=fingerprint:sha-256 "
    "30:FF:8E:2B:AC:9D:ED:70:18:10:67:C8:AE:9E:68:F3:86:53:51:B0:AC:31:B7:BE:"
    "6D:CF:A4:2E:D3:6E:B4:28" CRLF
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=sctp-port:5000" CRLF
    "a=max-message-size:10000" CRLF "a=setup:actpass" CRLF;

TEST_P(NewSdpTest, NewSctpportSdpParse) {
  ParseSdp(kNewSctpportOfferDraft21, false);
}

INSTANTIATE_TEST_SUITE_P(RoundTripSerialize, NewSdpTest,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()));

MOZ_RUNINIT const std::string kCandidateInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=candidate:0 1 UDP 2130379007 10.0.0.36 62453 typ host" CRLF
    "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:109 opus/48000/2" CRLF;

// This may or may not parse, but if it does, the errant candidate attribute
// should be ignored.
TEST_P(NewSdpTest, CheckCandidateInSessionLevel) {
  ParseSdp(kCandidateInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kCandidateAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kCandidateAttribute));
  }
}

MOZ_RUNINIT const std::string kBundleOnlyInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=bundle-only" CRLF "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:109 opus/48000/2" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckBundleOnlyInSessionLevel) {
  ParseSdp(kBundleOnlyInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kBundleOnlyAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kBundleOnlyAttribute));
  }
}

MOZ_RUNINIT const std::string kFmtpInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=fmtp:109 0-15" CRLF "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:109 opus/48000/2" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckFmtpInSessionLevel) {
  ParseSdp(kFmtpInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kFmtpAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kFmtpAttribute));
  }
}

MOZ_RUNINIT const std::string kIceMismatchInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=ice-mismatch" CRLF "m=audio 9 RTP/SAVPF 109 9 0 8 101" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:109 opus/48000/2" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckIceMismatchInSessionLevel) {
  ParseSdp(kIceMismatchInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kIceMismatchAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kIceMismatchAttribute));
  }
}

MOZ_RUNINIT const std::string kImageattrInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=imageattr:120 send * recv *" CRLF "m=video 9 RTP/SAVPF 120" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckImageattrInSessionLevel) {
  ParseSdp(kImageattrInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kImageattrAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kImageattrAttribute));
  }
}

MOZ_RUNINIT const std::string kLabelInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=label:foobar" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckLabelInSessionLevel) {
  ParseSdp(kLabelInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kLabelAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kLabelAttribute));
  }
}

MOZ_RUNINIT const std::string kMaxptimeInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=maxptime:100" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckMaxptimeInSessionLevel) {
  ParseSdp(kMaxptimeInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kMaxptimeAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kMaxptimeAttribute));
  }
}

MOZ_RUNINIT const std::string kMidInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=mid:foobar" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckMidInSessionLevel) {
  ParseSdp(kMidInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kMidAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kMidAttribute));
  }
}

MOZ_RUNINIT const std::string kMsidInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=msid:foobar" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckMsidInSessionLevel) {
  ParseSdp(kMsidInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kMsidAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kMsidAttribute));
  }
}

MOZ_RUNINIT const std::string kPtimeInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=ptime:50" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckPtimeInSessionLevel) {
  ParseSdp(kPtimeInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kPtimeAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kPtimeAttribute));
  }
}

MOZ_RUNINIT const std::string kRemoteCandidatesInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=remote-candidates:0 10.0.0.1 5555" CRLF "m=video 9 RTP/SAVPF 120" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRemoteCandidatesInSessionLevel) {
  ParseSdp(kRemoteCandidatesInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRemoteCandidatesAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kRemoteCandidatesAttribute));
  }
}

MOZ_RUNINIT const std::string kRtcpInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=rtcp:5555" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRtcpInSessionLevel) {
  ParseSdp(kRtcpInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRtcpAttribute));
  }
}

MOZ_RUNINIT const std::string kRtcpFbInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=rtcp-fb:120 nack" CRLF "m=video 9 RTP/SAVPF 120" CRLF
    "c=IN IP4 0.0.0.0" CRLF "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRtcpFbInSessionLevel) {
  ParseSdp(kRtcpFbInSessionSDP, false, true);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpFbAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRtcpFbAttribute));
  }
}

MOZ_RUNINIT const std::string kRtcpMuxInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=rtcp-mux" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRtcpMuxInSessionLevel) {
  ParseSdp(kRtcpMuxInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpMuxAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpMuxAttribute));
  }
}

MOZ_RUNINIT const std::string kRtcpRsizeInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=rtcp-rsize" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRtcpRsizeInSessionLevel) {
  ParseSdp(kRtcpRsizeInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpRsizeAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kRtcpRsizeAttribute));
  }
}

MOZ_RUNINIT const std::string kRtpmapInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF "m=video 9 RTP/SAVPF 120" CRLF
    "c=IN IP4 0.0.0.0" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckRtpmapInSessionLevel) {
  ParseSdp(kRtpmapInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kRtpmapAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRtpmapAttribute));
  }
}

MOZ_RUNINIT const std::string kSctpmapInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=sctpmap:5000" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckSctpmapInSessionLevel) {
  ParseSdp(kSctpmapInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kSctpmapAttribute));
    ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(
        SdpAttribute::kSctpmapAttribute));
  }
}

MOZ_RUNINIT const std::string kSsrcInSessionSDP =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "a=ssrc:5000" CRLF "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

// This may or may not parse, but if it does, the errant attribute
// should be ignored.
TEST_P(NewSdpTest, CheckSsrcInSessionLevel) {
  ParseSdp(kSsrcInSessionSDP, false);
  if (Sdp()) {
    ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kSsrcAttribute));
    ASSERT_FALSE(
        Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kSsrcAttribute));
  }
}

MOZ_RUNINIT const std::string kMalformedImageattr =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF "a=imageattr:flob" CRLF;

TEST_P(NewSdpTest, CheckMalformedImageattr) {
  if (::testing::get<0>(GetParam())) {
    // Don't do a parse/serialize before running this test
    return;
  }

  ParseSdp(kMalformedImageattr, false);
  ASSERT_NE(0U, ParseErrorCount());
}

TEST_P(NewSdpTest, ParseInvalidSimulcastNoSuchSendRid) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendrecv" CRLF "a=simulcast:send 9" CRLF
           "a=rid:9 recv max-width=800;max-height=600" CRLF,
           false);
  ASSERT_NE(0U, ParseErrorCount());
}

TEST_P(NewSdpTest, ParseInvalidSimulcastNoSuchRecvRid) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendrecv" CRLF "a=simulcast:recv 9" CRLF
           "a=rid:9 send max-width=800;max-height=600" CRLF,
           false);
  ASSERT_NE(0U, ParseErrorCount());
}

TEST_P(NewSdpTest, ParseSimulcastNotSending) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=recvonly" CRLF "a=simulcast:send 120" CRLF "a=rid:120 send" CRLF,
           false);
  ASSERT_EQ(0U, ParseErrorCount());
}

TEST_P(NewSdpTest, ParseSimulcastNotReceiving) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendonly" CRLF "a=simulcast:recv 120" CRLF "a=rid:120 recv" CRLF,
           false);
  ASSERT_EQ(0U, ParseErrorCount());
}

TEST_P(NewSdpTest, ParseInvalidRidNoSuchPt) {
  SKIP_TEST_WITH_SIPCC_PARSER
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendrecv" CRLF "a=simulcast:recv rid=9" CRLF
           "a=rid:9 recv pt=101;max-width=800;max-height=600" CRLF,
           false);
  ASSERT_NE(0U, ParseErrorCount());
}

MOZ_RUNINIT const std::string kNoAttributes =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

TEST_P(NewSdpTest, CheckNoAttributes) {
  ParseSdp(kNoAttributes);

  for (auto a = static_cast<size_t>(SdpAttribute::kFirstAttribute);
       a <= static_cast<size_t>(SdpAttribute::kLastAttribute); ++a) {
    SdpAttribute::AttributeType type =
        static_cast<SdpAttribute::AttributeType>(a);

    // rtpmap is a special case right now, we throw parse errors if it is
    // missing, and then insert one.
    // direction is another special case that gets a default if not present
    if (type != SdpAttribute::kRtpmapAttribute &&
        type != SdpAttribute::kDirectionAttribute) {
      ASSERT_FALSE(
          Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(type))
      << "Attribute " << a << " should not have been present at media level";
      ASSERT_FALSE(Sdp()->GetAttributeList().HasAttribute(type))
      << "Attribute " << a << " should not have been present at session level";
    }
  }

  ASSERT_FALSE(
      Sdp()->GetAttributeList().HasAttribute(SdpAttribute::kRtpmapAttribute));

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kDirectionAttribute));
  ASSERT_EQ(SdpDirectionAttribute::kSendrecv,
            Sdp()->GetMediaSection(0).GetAttributeList().GetDirection());
  ASSERT_TRUE(Sdp()->GetAttributeList().HasAttribute(
      SdpAttribute::kDirectionAttribute));
  ASSERT_EQ(SdpDirectionAttribute::kSendrecv,
            Sdp()->GetAttributeList().GetDirection());
}

MOZ_RUNINIT const std::string kMediaLevelDtlsMessage =
    "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
    "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
    "m=video 9 RTP/SAVPF 120" CRLF "c=IN IP4 0.0.0.0" CRLF
    "a=dtls-message:client " BASE64_DTLS_HELLO CRLF
    "a=rtpmap:120 VP8/90000" CRLF;

TEST_P(NewSdpTest, CheckMediaLevelDtlsMessage) {
  ParseSdp(kMediaLevelDtlsMessage);
  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();

  // dtls-message is not defined for use at the media level; we don't
  // parse it
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kDtlsMessageAttribute));
}

TEST_P(NewSdpTest, CheckSetPort) {
  // Parse any valid sdp with a media section
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendonly" CRLF,
           false);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();

  constexpr unsigned int expectedParesPort = 56436;
  unsigned int currentPort = Sdp()->GetMediaSection(0).GetPort();
  ASSERT_EQ(expectedParesPort, currentPort);

  Sdp()->GetMediaSection(0).SetPort(currentPort + 1);
  ASSERT_EQ(currentPort + 1, Sdp()->GetMediaSection(0).GetPort());
}

TEST_P(NewSdpTest, CheckAddCodec) {
  // Parse any valid sdp with a media section
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendonly" CRLF);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount());

  ASSERT_EQ(1U, Sdp()->GetMediaSection(0).GetFormats().size());
  ASSERT_EQ(
      1U,
      Sdp()->GetMediaSection(0).GetAttributeList().GetRtpmap().mRtpmaps.size());

  Sdp()->GetMediaSection(0).AddCodec("110", "opus", 48000, 2);

  ASSERT_EQ(2U, Sdp()->GetMediaSection(0).GetFormats().size());
  const auto& rtpmaps =
      Sdp()->GetMediaSection(0).GetAttributeList().GetRtpmap();
  ASSERT_EQ(2U, rtpmaps.mRtpmaps.size());

  ASSERT_TRUE(rtpmaps.HasEntry("120"));
  ASSERT_TRUE(rtpmaps.HasEntry("110"));
  const auto aRtpmap = rtpmaps.GetEntry("110");
  ASSERT_EQ(aRtpmap.pt, "110");
  ASSERT_EQ(aRtpmap.codec, SdpRtpmapAttributeList::kOpus);
  ASSERT_EQ(aRtpmap.name, "opus");
  ASSERT_EQ(aRtpmap.clock, 48000U);
  ASSERT_EQ(aRtpmap.channels, 2U);
}

TEST_P(NewSdpTest, CheckClearCodecs) {
  // Parse any valid sdp with a media section
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=video 56436 RTP/SAVPF 120 110" CRLF "a=rtpmap:120 VP8/90000" CRLF
           "a=sendonly" CRLF "a=rtpmap:110 opus/48000" CRLF);
  // SIPCC strips the channels on opus so the /2 was omitted

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount());

  ASSERT_EQ(2U, Sdp()->GetMediaSection(0).GetFormats().size());
  ASSERT_EQ(
      2U,
      Sdp()->GetMediaSection(0).GetAttributeList().GetRtpmap().mRtpmaps.size());

  Sdp()->GetMediaSection(0).ClearCodecs();

  ASSERT_EQ(0U, Sdp()->GetMediaSection(0).GetFormats().size());
  ASSERT_FALSE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kRtpmapAttribute));
}

TEST_P(NewSdpTest, CheckAddMediaSection) {
  ParseSdp(kBasicAudioVideoOffer);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(3U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections";

  Sdp()->AddMediaSection({.type = SdpMediaSection::kVideo},
                         SdpDirectionAttribute::Direction::kSendrecv, 58000,
                         SdpMediaSection::kUdpDtlsSctp, sdp::kIPv4,
                         "127.0.0.1");

  ASSERT_EQ(4U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections after adding media section";

  const SdpMediaSection& newMediaSection = Sdp()->GetMediaSection(3);

  ASSERT_EQ(SdpMediaSection::kVideo, newMediaSection.GetMediaType());
  ASSERT_EQ(SdpDirectionAttribute::Direction::kSendrecv,
            newMediaSection.GetDirectionAttribute().mValue);
  ASSERT_EQ(58000U, newMediaSection.GetPort());
  ASSERT_EQ(SdpMediaSection::kUdpDtlsSctp, newMediaSection.GetProtocol());
  ASSERT_EQ(sdp::kIPv4, newMediaSection.GetConnection().GetAddrType());
  ASSERT_EQ("127.0.0.1", newMediaSection.GetConnection().GetAddress());

  Sdp()->AddMediaSection({.type = SdpMediaSection::kAudio},
                         SdpDirectionAttribute::Direction::kSendonly, 14006,
                         SdpMediaSection::kTcpDtlsRtpSavpf, sdp::kIPv6,
                         "2607:f8b0:4004:801::2013");

  ASSERT_EQ(5U, Sdp()->GetMediaSectionCount())
      << "Wrong number of media sections after adding media section";

  const SdpMediaSection& nextNewMediaSection = Sdp()->GetMediaSection(4);

  ASSERT_EQ(SdpMediaSection::kAudio, nextNewMediaSection.GetMediaType());
  ASSERT_EQ(SdpDirectionAttribute::Direction::kSendonly,
            nextNewMediaSection.GetDirectionAttribute().mValue);
  ASSERT_EQ(14006U, nextNewMediaSection.GetPort());
  ASSERT_EQ(SdpMediaSection::kTcpDtlsRtpSavpf,
            nextNewMediaSection.GetProtocol());
  ASSERT_EQ(sdp::kIPv6, nextNewMediaSection.GetConnection().GetAddrType());
  ASSERT_EQ("2607:f8b0:4004:801::2013",
            nextNewMediaSection.GetConnection().GetAddress());
}

TEST_P(NewSdpTest, CheckAddDataChannel_Draft05) {
  // Parse any valid sdp with a media section
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=application 56436 DTLS/SCTP 5000" CRLF);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount());

  auto& mediaSection = Sdp()->GetMediaSection(0);
  mediaSection.AddDataChannel("webrtc-datachannel", 6000, 16, 0);

  ASSERT_FALSE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kMaxMessageSizeAttribute));
  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kSctpmapAttribute));
  ASSERT_TRUE(mediaSection.GetAttributeList().GetSctpmap().HasEntry("6000"));
  ASSERT_EQ(
      16U,
      mediaSection.GetAttributeList().GetSctpmap().GetFirstEntry().streams);
  ASSERT_EQ("webrtc-datachannel",
            mediaSection.GetAttributeList().GetSctpmap().GetFirstEntry().name);

  mediaSection.AddDataChannel("webrtc-datachannel", 15000, 8, 1800);

  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kMaxMessageSizeAttribute));
  ASSERT_EQ(1800U, mediaSection.GetAttributeList().GetMaxMessageSize());
  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kSctpmapAttribute));
  ASSERT_TRUE(mediaSection.GetAttributeList().GetSctpmap().HasEntry("15000"));
  ASSERT_EQ(
      8U, mediaSection.GetAttributeList().GetSctpmap().GetFirstEntry().streams);
}

TEST_P(NewSdpTest, CheckAddDataChannel) {
  ParseSdp("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=SIP Call" CRLF
           "c=IN IP4 198.51.100.7" CRLF "b=CT:5000" CRLF "t=0 0" CRLF
           "m=application 56436 UDP/DTLS/SCTP webrtc-datachannel" CRLF);

  ASSERT_TRUE(!!Sdp())
  << "Parse failed: " << SerializeParseErrors();
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount());

  auto& mediaSection = Sdp()->GetMediaSection(0);
  mediaSection.AddDataChannel("webrtc-datachannel", 6000, 16, 0);

  ASSERT_FALSE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kMaxMessageSizeAttribute));
  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kSctpPortAttribute));
  ASSERT_EQ(6000U, mediaSection.GetAttributeList().GetSctpPort());

  mediaSection.AddDataChannel("webrtc-datachannel", 15000, 8, 1800);

  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kMaxMessageSizeAttribute));
  ASSERT_EQ(1800U, mediaSection.GetAttributeList().GetMaxMessageSize());
  ASSERT_TRUE(mediaSection.GetAttributeList().HasAttribute(
      SdpAttribute::kSctpPortAttribute));
  ASSERT_EQ(15000U, mediaSection.GetAttributeList().GetSctpPort());
}

// Neither parser can programatically create a media section with an unknown
// media type, so build one by hand and check that it round-trips through
// serialization. Since we aren't using the parsers to create documents, this
// is a limitation of the tests rather than a bug.
// See CheckUnknownMediaTypeParse for the webrtc-sdp parser path.
TEST(NewSdpTestNoFixture, CheckUnknownMediaTypeSerialize)
{
  SdpImpl sdp(SdpOrigin("-", 4294967296, 2, sdp::kIPv4, "127.0.0.1"));

  const SdpMediaSection& msection = sdp.AddMediaSection(
      {.type = SdpMediaSection::kUnknown,
       .unknown = Some(SdpMediaSection::UnknownMediaTypeData{
           .name = "sprinkles", .formatString = "8 16 foo"})},
      SdpDirectionAttribute::kSendrecv, 9, SdpMediaSection::kUdpTlsRtpSavpf,
      sdp::kIPv4, "0.0.0.0");

  ASSERT_EQ(SdpMediaSection::kUnknown, msection.GetMediaType());
  ASSERT_TRUE(msection.GetMediaTypeInfo().unknown.isSome());
  ASSERT_EQ("sprinkles", msection.GetMediaTypeInfo().unknown->name);
  ASSERT_EQ("8 16 foo", msection.GetMediaTypeInfo().unknown->formatString);
  ASSERT_TRUE(msection.GetFormats().empty());

  std::ostringstream os;
  msection.Serialize(os);
  ASSERT_EQ("m=sprinkles 9 UDP/TLS/RTP/SAVPF 8 16 foo" CRLF
            "c=IN IP4 0.0.0.0" CRLF "a=sendrecv" CRLF,
            os.str());

  std::ostringstream sdpOs;
  sdp.Serialize(sdpOs);
  ASSERT_EQ("v=0" CRLF "o=- 4294967296 2 IN IP4 127.0.0.1" CRLF "s=-" CRLF
            "t=0 0" CRLF "m=sprinkles 9 UDP/TLS/RTP/SAVPF 8 16 foo" CRLF
            "c=IN IP4 0.0.0.0" CRLF "a=sendrecv" CRLF,
            sdpOs.str());
}

// An unknown media type with no formats must not leave a trailing space on the
// m= line.
TEST(NewSdpTestNoFixture, CheckUnknownMediaTypeNoFormatsSerialize)
{
  SdpImpl sdp(SdpOrigin("-", 4294967296, 2, sdp::kIPv4, "127.0.0.1"));

  const SdpMediaSection& msection = sdp.AddMediaSection(
      {.type = SdpMediaSection::kUnknown,
       .unknown = Some(SdpMediaSection::UnknownMediaTypeData{
           .name = "clowncar", .formatString = ""})},
      SdpDirectionAttribute::kInactive, 0, SdpMediaSection::kUdpTlsRtpSavpf,
      sdp::kIPv4, "0.0.0.0");

  std::ostringstream os;
  msection.Serialize(os);
  ASSERT_EQ("m=clowncar 0 UDP/TLS/RTP/SAVPF" CRLF "c=IN IP4 0.0.0.0" CRLF
            "a=inactive" CRLF,
            os.str());
}

TEST(NewSdpTestNoFixture, CheckUnknownMediaTypeParse)
{
  const std::string kUnknownMediaTypeOffer =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=audio 9 RTP/SAVPF 109" CRLF "a=rtpmap:109 opus/48000/2" CRLF
      "m=sprinkles 9 UDP/TLS/RTP/SAVPF 8 16 foo" CRLF
      "m=clowncar 0 UDP/TLS/RTP/SAVPF" CRLF;

  RsdparsaSdpParser parser;
  const auto results = parser.Parse(kUnknownMediaTypeOffer);
  ASSERT_TRUE(!!results->Sdp());
  ASSERT_EQ(0U, results->Errors().size());
  ASSERT_EQ(3U, results->Sdp()->GetMediaSectionCount());

  const SdpMediaSection& audio = results->Sdp()->GetMediaSection(0);
  ASSERT_EQ(SdpMediaSection::kAudio, audio.GetMediaType());
  ASSERT_TRUE(audio.GetMediaTypeInfo().unknown.isNothing());

  const SdpMediaSection& sprinkles = results->Sdp()->GetMediaSection(1);
  ASSERT_EQ(SdpMediaSection::kUnknown, sprinkles.GetMediaType());
  ASSERT_TRUE(sprinkles.GetMediaTypeInfo().unknown.isSome());
  ASSERT_EQ("sprinkles", sprinkles.GetMediaTypeInfo().unknown->name);
  ASSERT_EQ("8 16 foo", sprinkles.GetMediaTypeInfo().unknown->formatString);
  // The format list of an unknown media type is not tokenized
  ASSERT_TRUE(sprinkles.GetFormats().empty());

  const SdpMediaSection& clowncar = results->Sdp()->GetMediaSection(2);
  ASSERT_EQ(SdpMediaSection::kUnknown, clowncar.GetMediaType());
  ASSERT_TRUE(clowncar.GetMediaTypeInfo().unknown.isSome());
  ASSERT_EQ("clowncar", clowncar.GetMediaTypeInfo().unknown->name);
  ASSERT_EQ("", clowncar.GetMediaTypeInfo().unknown->formatString);

  // Both m= lines must come back out the way they went in
  // Make sure we don't add a trailing space to the format string for unknown
  // media types
  std::ostringstream os;
  results->Sdp()->Serialize(os);
  ASSERT_NE(std::string::npos,
            os.str().find("m=sprinkles 9 UDP/TLS/RTP/SAVPF 8 16 foo" CRLF));
  ASSERT_NE(std::string::npos,
            os.str().find("m=clowncar 0 UDP/TLS/RTP/SAVPF" CRLF));

  // Reparsing our own serialization has to be stable. It will not match the
  // SDP we were given, because serialization normalizes the session name,
  // moves the session level connection line into each media section, and
  // writes out the default directions.
  const auto reparsed = parser.Parse(os.str());
  ASSERT_TRUE(!!reparsed->Sdp());
  std::ostringstream reserialized;
  reparsed->Sdp()->Serialize(reserialized);
  ASSERT_EQ(os.str(), reserialized.str());
}

TEST(NewSdpTestNoFixture, CheckParsingResultComparer)
{
  auto check_comparison = [](const std::string sdp_string) {
    SipccSdpParser sipccParser;
    RsdparsaSdpParser rustParser;
    auto print_errors =
        [](const mozilla::UniquePtr<SdpParser::Results>& results,
           const char* name) {
          for (const auto& e : results->Errors()) {
            std::cerr << name << " Line " << e.first << ": " << e.second;
          }
        };
    auto sipccResults = sipccParser.Parse(sdp_string);
    auto sipccSdp = std::move(sipccResults->Sdp());
    print_errors(sipccResults, "sipcc");

    auto rustResults = rustParser.Parse(sdp_string);
    auto rustSdp = std::move(rustResults->Sdp());
    print_errors(rustResults, "webrtc-sdp");

    ParsingResultComparer comparer;
    return sipccSdp && rustSdp
               ? comparer.Compare(*rustSdp, *sipccSdp, sdp_string)
               : false;
  };

  ASSERT_TRUE(check_comparison(kBasicAudioVideoOffer));
  ASSERT_TRUE(check_comparison(kBasicAudioVideoOfferLinefeedOnly));
  ASSERT_TRUE(check_comparison(kH264AudioVideoOffer));

  // Check the Fmtp comprison
  const std::string kBasicOpusFmtp1 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 opus/48000/1" CRLF
      "a=fmtp:120 stereo=1;useinbandfec=1" CRLF;
  ASSERT_TRUE(check_comparison(kBasicOpusFmtp1));

  const std::string kBasicOpusFmtp2 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 opus/48000" CRLF
      "a=fmtp:120 useinbandfec=1;stereo=1;maxplaybackrate=32000" CRLF;
  ASSERT_TRUE(check_comparison(kBasicOpusFmtp2));

  const std::string kBasicVP8Fmtp1 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
      "a=fmtp:120 max-fs=3600;max-fr=60" CRLF;
  ASSERT_TRUE(check_comparison(kBasicVP8Fmtp1));
  //
  const std::string kBasicVP8Fmtp2 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 VP8/90000" CRLF
      "a=fmtp:120 max-fr=60;max-fs=3600" CRLF;
  ASSERT_TRUE(check_comparison(kBasicVP8Fmtp2));

  const std::string kBasicH264Fmtp1 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 H264/90000" CRLF
      "a=fmtp:120 profile-level-id=42a01e;level_asymmetry_allowed=1" CRLF;
  ASSERT_TRUE(check_comparison(kBasicH264Fmtp1));

  const std::string kBasicH264Fmtp2 =
      "v=0" CRLF "o=Mozilla-SIPUA-35.0a1 5184 0 IN IP4 0.0.0.0" CRLF
      "s=SIP Call" CRLF "c=IN IP4 224.0.0.1/100/12" CRLF "t=0 0" CRLF
      "m=video 9 RTP/SAVPF 120" CRLF "a=rtpmap:120 H264/90000" CRLF
      "a=fmtp:120 "
      "level_asymmetry_allowed=1;profile-level-id=42a01e;max_fs=3600" CRLF;
  ASSERT_TRUE(check_comparison(kBasicH264Fmtp2));
}

TEST(NewSdpTestNoFixture, CheckAttributeTypeSerialize)
{
  for (auto a = static_cast<size_t>(SdpAttribute::kFirstAttribute);
       a <= static_cast<size_t>(SdpAttribute::kLastAttribute); ++a) {
    SdpAttribute::AttributeType type =
        static_cast<SdpAttribute::AttributeType>(a);

    // Direction attributes are handled a little differently
    if (type != SdpAttribute::kDirectionAttribute) {
      std::ostringstream os;
      os << type;
      ASSERT_NE("", os.str());
    }
  }
}

static SdpImageattrAttributeList::XYRange ParseXYRange(
    const std::string& input) {
  std::istringstream is(input + ",");
  std::string error;
  SdpImageattrAttributeList::XYRange range;
  EXPECT_TRUE(range.Parse(is, &error)) << error;
  EXPECT_EQ(',', is.get());
  EXPECT_EQ(EOF, is.get());
  return range;
}

TEST(NewSdpTestNoFixture, CheckImageattrXYRangeParseValid)
{
  {
    SdpImageattrAttributeList::XYRange range(ParseXYRange("640"));
    ASSERT_EQ(1U, range.discreteValues.size());
    ASSERT_EQ(640U, range.discreteValues[0]);
  }

  {
    SdpImageattrAttributeList::XYRange range(ParseXYRange("[320,640]"));
    ASSERT_EQ(2U, range.discreteValues.size());
    ASSERT_EQ(320U, range.discreteValues[0]);
    ASSERT_EQ(640U, range.discreteValues[1]);
  }

  {
    SdpImageattrAttributeList::XYRange range(ParseXYRange("[320,640,1024]"));
    ASSERT_EQ(3U, range.discreteValues.size());
    ASSERT_EQ(320U, range.discreteValues[0]);
    ASSERT_EQ(640U, range.discreteValues[1]);
    ASSERT_EQ(1024U, range.discreteValues[2]);
  }

  {
    SdpImageattrAttributeList::XYRange range(ParseXYRange("[320:640]"));
    ASSERT_EQ(0U, range.discreteValues.size());
    ASSERT_EQ(320U, range.min);
    ASSERT_EQ(1U, range.step);
    ASSERT_EQ(640U, range.max);
  }

  {
    SdpImageattrAttributeList::XYRange range(ParseXYRange("[320:16:640]"));
    ASSERT_EQ(0U, range.discreteValues.size());
    ASSERT_EQ(320U, range.min);
    ASSERT_EQ(16U, range.step);
    ASSERT_EQ(640U, range.max);
  }
}

template <typename T>
void ParseInvalid(const std::string& input, size_t last) {
  std::istringstream is(input);
  T parsed;
  std::string error;
  ASSERT_FALSE(parsed.Parse(is, &error))
  << "\'" << input << "\' should not have parsed successfully";
  is.clear();
  ASSERT_EQ(last, static_cast<size_t>(is.tellg()))
      << "Parse failed at unexpected location:" << std::endl
      << input << std::endl
      << std::string(is.tellg(), ' ') << "^" << std::endl;
  // For a human to eyeball to make sure the error strings look sane
  std::cout << "\"" << input << "\" - " << error << std::endl;
}

TEST(NewSdpTestNoFixture, CheckImageattrXYRangeParseInvalid)
{
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[-1", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[-", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[-v", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:-1", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:-1", 8);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640,-1", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640,-]", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("-v", 0);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("-1", 0);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("", 0);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[v", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[ 640", 1);
  // It looks like the overflow detection only happens once the whole number
  // is scanned...
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[99999999999999999:", 18);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640", 4);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:v", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16", 7);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:", 8);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:v", 8);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:320]", 11);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:320", 11);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:16:320v", 11);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:1024", 9);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:320]", 8);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640:1024v", 9);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640,", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640,v", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640]", 4);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640x", 4);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("[640,]", 5);
  ParseInvalid<SdpImageattrAttributeList::XYRange>(" ", 0);
  ParseInvalid<SdpImageattrAttributeList::XYRange>("v", 0);
}

static SdpImageattrAttributeList::SRange ParseSRange(const std::string& input) {
  std::istringstream is(input + ",");
  std::string error;
  SdpImageattrAttributeList::SRange range;
  EXPECT_TRUE(range.Parse(is, &error)) << error;
  EXPECT_EQ(',', is.get());
  EXPECT_EQ(EOF, is.get());
  return range;
}

TEST(NewSdpTestNoFixture, CheckImageattrSRangeParseValid)
{
  {
    SdpImageattrAttributeList::SRange range(ParseSRange("0.1"));
    ASSERT_EQ(1U, range.discreteValues.size());
    ASSERT_FLOAT_EQ(0.1f, range.discreteValues[0]);
  }

  {
    SdpImageattrAttributeList::SRange range(ParseSRange("[0.1,0.2]"));
    ASSERT_EQ(2U, range.discreteValues.size());
    ASSERT_FLOAT_EQ(0.1f, range.discreteValues[0]);
    ASSERT_FLOAT_EQ(0.2f, range.discreteValues[1]);
  }

  {
    SdpImageattrAttributeList::SRange range(ParseSRange("[0.1,0.2,0.3]"));
    ASSERT_EQ(3U, range.discreteValues.size());
    ASSERT_FLOAT_EQ(0.1f, range.discreteValues[0]);
    ASSERT_FLOAT_EQ(0.2f, range.discreteValues[1]);
    ASSERT_FLOAT_EQ(0.3f, range.discreteValues[2]);
  }

  {
    SdpImageattrAttributeList::SRange range(ParseSRange("[0.1-0.2]"));
    ASSERT_EQ(0U, range.discreteValues.size());
    ASSERT_FLOAT_EQ(0.1f, range.min);
    ASSERT_FLOAT_EQ(0.2f, range.max);
  }
}

TEST(NewSdpTestNoFixture, CheckImageattrSRangeParseInvalid)
{
  ParseInvalid<SdpImageattrAttributeList::SRange>("", 0);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[v", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[-1", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[-", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[v", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[ 0.2", 1);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[10.1-", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.08-", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2", 4);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2-", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2-v", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2--1", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2-0.3", 8);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2-0.1]", 8);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2-0.3v", 8);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2,", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2,v", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2,-1", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2]", 4);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2v", 4);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2,]", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>("[0.2,-]", 5);
  ParseInvalid<SdpImageattrAttributeList::SRange>(" ", 0);
  ParseInvalid<SdpImageattrAttributeList::SRange>("v", 0);
  ParseInvalid<SdpImageattrAttributeList::SRange>("-v", 0);
  ParseInvalid<SdpImageattrAttributeList::SRange>("-1", 0);
}

static SdpImageattrAttributeList::PRange ParsePRange(const std::string& input) {
  std::istringstream is(input + ",");
  std::string error;
  SdpImageattrAttributeList::PRange range;
  EXPECT_TRUE(range.Parse(is, &error)) << error;
  EXPECT_EQ(',', is.get());
  EXPECT_EQ(EOF, is.get());
  return range;
}

TEST(NewSdpTestNoFixture, CheckImageattrPRangeParseValid)
{
  SdpImageattrAttributeList::PRange range(ParsePRange("[0.1000-9.9999]"));
  ASSERT_FLOAT_EQ(0.1f, range.min);
  ASSERT_FLOAT_EQ(9.9999f, range.max);
}

TEST(NewSdpTestNoFixture, CheckImageattrPRangeParseInvalid)
{
  ParseInvalid<SdpImageattrAttributeList::PRange>("", 0);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[v", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[-1", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[-", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[v", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[ 0.2", 1);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[10.1-", 5);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.08-", 5);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2", 4);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2-", 5);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2-v", 5);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2--1", 5);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2-0.3", 8);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2-0.1]", 8);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2-0.3v", 8);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2,", 4);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2:", 4);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2]", 4);
  ParseInvalid<SdpImageattrAttributeList::PRange>("[0.2v", 4);
  ParseInvalid<SdpImageattrAttributeList::PRange>(" ", 0);
  ParseInvalid<SdpImageattrAttributeList::PRange>("v", 0);
  ParseInvalid<SdpImageattrAttributeList::PRange>("-x", 0);
  ParseInvalid<SdpImageattrAttributeList::PRange>("-1", 0);
}

static SdpImageattrAttributeList::Set ParseSet(const std::string& input) {
  std::istringstream is(input + " ");
  std::string error;
  SdpImageattrAttributeList::Set set;
  EXPECT_TRUE(set.Parse(is, &error)) << error;
  EXPECT_EQ(' ', is.get());
  EXPECT_EQ(EOF, is.get());
  return set;
}

TEST(NewSdpTestNoFixture, CheckImageattrSetParseValid)
{
  {
    SdpImageattrAttributeList::Set set(ParseSet("[x=320,y=240]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.5f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(ParseSet("[X=320,Y=240]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.5f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(ParseSet("[x=320,y=240,par=[0.1-0.2]]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_TRUE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.pRange.min);
    ASSERT_FLOAT_EQ(0.2f, set.pRange.max);
    ASSERT_FLOAT_EQ(0.5f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(ParseSet("[x=320,y=240,sar=[0.1-0.2]]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_TRUE(set.sRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.sRange.min);
    ASSERT_FLOAT_EQ(0.2f, set.sRange.max);
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.5f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(ParseSet("[x=320,y=240,q=0.1]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(
        ParseSet("[x=320,y=240,par=[0.1-0.2],sar=[0.3-0.4],q=0.6]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_TRUE(set.sRange.IsSet());
    ASSERT_FLOAT_EQ(0.3f, set.sRange.min);
    ASSERT_FLOAT_EQ(0.4f, set.sRange.max);
    ASSERT_TRUE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.pRange.min);
    ASSERT_FLOAT_EQ(0.2f, set.pRange.max);
    ASSERT_FLOAT_EQ(0.6f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(ParseSet("[x=320,y=240,foo=bar,q=0.1]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(
        ParseSet("[x=320,y=240,foo=bar,q=0.1,bar=baz]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(
        ParseSet("[x=320,y=240,foo=[bar],q=0.1,bar=[baz]]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.qValue);
  }

  {
    SdpImageattrAttributeList::Set set(
        ParseSet("[x=320,y=240,foo=[par=foo,sar=bar],q=0.1,bar=[baz]]"));
    ASSERT_EQ(1U, set.xRange.discreteValues.size());
    ASSERT_EQ(320U, set.xRange.discreteValues[0]);
    ASSERT_EQ(1U, set.yRange.discreteValues.size());
    ASSERT_EQ(240U, set.yRange.discreteValues[0]);
    ASSERT_FALSE(set.sRange.IsSet());
    ASSERT_FALSE(set.pRange.IsSet());
    ASSERT_FLOAT_EQ(0.1f, set.qValue);
  }
}

TEST(NewSdpTestNoFixture, CheckImageattrSetParseInvalid)
{
  ParseInvalid<SdpImageattrAttributeList::Set>("", 0);
  ParseInvalid<SdpImageattrAttributeList::Set>("x", 0);
  ParseInvalid<SdpImageattrAttributeList::Set>("[", 1);
  ParseInvalid<SdpImageattrAttributeList::Set>("[=", 2);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x", 2);
  ParseInvalid<SdpImageattrAttributeList::Set>("[y=", 3);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=[", 4);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320", 6);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320v", 6);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,", 7);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,=", 8);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,x", 8);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,x=", 9);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=[", 10);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240", 12);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240x", 12);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,", 13);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=", 15);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=v", 15);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5", 18);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5,", 19);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5,]", 20);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5,=]", 20);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5,sar=v]", 23);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,q=0.5,q=0.4", 21);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,sar=", 17);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,sar=v", 17);
  ParseInvalid<SdpImageattrAttributeList::Set>(
      "[x=320,y=240,sar=[0.5-0.6],sar=[0.7-0.8]", 31);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,par=", 17);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,par=x", 17);
  ParseInvalid<SdpImageattrAttributeList::Set>(
      "[x=320,y=240,par=[0.5-0.6],par=[0.7-0.8]", 31);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,foo=", 17);
  ParseInvalid<SdpImageattrAttributeList::Set>("[x=320,y=240,foo=x", 18);
}

static SdpImageattrAttributeList::Imageattr ParseImageattr(
    const std::string& input) {
  std::istringstream is(input);
  std::string error;
  SdpImageattrAttributeList::Imageattr imageattr;
  EXPECT_TRUE(imageattr.Parse(is, &error)) << error;
  EXPECT_TRUE(is.eof());
  return imageattr;
}

TEST(NewSdpTestNoFixture, CheckImageattrParseValid)
{
  {
    SdpImageattrAttributeList::Imageattr imageattr(ParseImageattr("* send *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_FALSE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(ParseImageattr("* SEND *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_FALSE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(ParseImageattr("* recv *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_FALSE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(ParseImageattr("* RECV *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_FALSE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("* recv * send *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("* send * recv *"));
    ASSERT_FALSE(imageattr.pt.isSome());
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("8 send * recv *"));
    ASSERT_EQ(8U, *imageattr.pt);
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("8 send [x=320,y=240] recv *"));
    ASSERT_EQ(8U, *imageattr.pt);
    ASSERT_FALSE(imageattr.sendAll);
    ASSERT_EQ(1U, imageattr.sendSets.size());
    ASSERT_EQ(1U, imageattr.sendSets[0].xRange.discreteValues.size());
    ASSERT_EQ(320U, imageattr.sendSets[0].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.sendSets[0].yRange.discreteValues.size());
    ASSERT_EQ(240U, imageattr.sendSets[0].yRange.discreteValues[0]);
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("8 send [x=320,y=240] [x=640,y=480] recv *"));
    ASSERT_EQ(8U, *imageattr.pt);
    ASSERT_FALSE(imageattr.sendAll);
    ASSERT_EQ(2U, imageattr.sendSets.size());
    ASSERT_EQ(1U, imageattr.sendSets[0].xRange.discreteValues.size());
    ASSERT_EQ(320U, imageattr.sendSets[0].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.sendSets[0].yRange.discreteValues.size());
    ASSERT_EQ(240U, imageattr.sendSets[0].yRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.sendSets[1].xRange.discreteValues.size());
    ASSERT_EQ(640U, imageattr.sendSets[1].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.sendSets[1].yRange.discreteValues.size());
    ASSERT_EQ(480U, imageattr.sendSets[1].yRange.discreteValues[0]);
    ASSERT_TRUE(imageattr.recvAll);
    ASSERT_TRUE(imageattr.recvSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("8 send * recv [x=320,y=240]"));
    ASSERT_EQ(8U, *imageattr.pt);
    ASSERT_FALSE(imageattr.recvAll);
    ASSERT_EQ(1U, imageattr.recvSets.size());
    ASSERT_EQ(1U, imageattr.recvSets[0].xRange.discreteValues.size());
    ASSERT_EQ(320U, imageattr.recvSets[0].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.recvSets[0].yRange.discreteValues.size());
    ASSERT_EQ(240U, imageattr.recvSets[0].yRange.discreteValues[0]);
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
  }

  {
    SdpImageattrAttributeList::Imageattr imageattr(
        ParseImageattr("8 send * recv [x=320,y=240] [x=640,y=480]"));
    ASSERT_EQ(8U, *imageattr.pt);
    ASSERT_FALSE(imageattr.recvAll);
    ASSERT_EQ(2U, imageattr.recvSets.size());
    ASSERT_EQ(1U, imageattr.recvSets[0].xRange.discreteValues.size());
    ASSERT_EQ(320U, imageattr.recvSets[0].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.recvSets[0].yRange.discreteValues.size());
    ASSERT_EQ(240U, imageattr.recvSets[0].yRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.recvSets[1].xRange.discreteValues.size());
    ASSERT_EQ(640U, imageattr.recvSets[1].xRange.discreteValues[0]);
    ASSERT_EQ(1U, imageattr.recvSets[1].yRange.discreteValues.size());
    ASSERT_EQ(480U, imageattr.recvSets[1].yRange.discreteValues[0]);
    ASSERT_TRUE(imageattr.sendAll);
    ASSERT_TRUE(imageattr.sendSets.empty());
  }
}

TEST(NewSdpTestNoFixture, CheckImageattrParseInvalid)
{
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("", 0);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>(" ", 0);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("-1", 0);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("99999 ", 5);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("*", 1);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* sen", 5);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* vcer *", 6);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send x", 7);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send [x=640,y=480] [",
                                                     22);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send * sen", 12);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send * vcer *", 13);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send * send *", 13);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* recv * recv *", 13);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>("* send * recv x", 14);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>(
      "* send * recv [x=640,y=480] [", 29);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>(
      "* send * recv [x=640,y=480] *", 28);
  ParseInvalid<SdpImageattrAttributeList::Imageattr>(
      "* send * recv [x=640,y=480] foobajooba", 28);
}

TEST(NewSdpTestNoFixture, CheckImageattrXYRangeSerialization)
{
  SdpImageattrAttributeList::XYRange range;
  std::stringstream os;

  range.min = 320;
  range.max = 640;
  range.Serialize(os);
  ASSERT_EQ("[320:640]", os.str());
  os.str("");  // clear

  range.step = 16;
  range.Serialize(os);
  ASSERT_EQ("[320:16:640]", os.str());
  os.str("");  // clear

  range.min = 0;
  range.max = 0;
  range.discreteValues.push_back(320);
  range.Serialize(os);
  ASSERT_EQ("320", os.str());
  os.str("");

  range.discreteValues.push_back(640);
  range.Serialize(os);
  ASSERT_EQ("[320,640]", os.str());
}

TEST(NewSdpTestNoFixture, CheckImageattrSRangeSerialization)
{
  SdpImageattrAttributeList::SRange range;
  std::ostringstream os;

  range.min = 0.1f;
  range.max = 0.9999f;
  range.Serialize(os);
  ASSERT_EQ("[0.1000-0.9999]", os.str());
  os.str("");

  range.min = 0.0f;
  range.max = 0.0f;
  range.discreteValues.push_back(0.1f);
  range.Serialize(os);
  ASSERT_EQ("0.1000", os.str());
  os.str("");

  range.discreteValues.push_back(0.5f);
  range.Serialize(os);
  ASSERT_EQ("[0.1000,0.5000]", os.str());
}

TEST(NewSdpTestNoFixture, CheckImageattrPRangeSerialization)
{
  SdpImageattrAttributeList::PRange range;
  std::ostringstream os;

  range.min = 0.1f;
  range.max = 0.9999f;
  range.Serialize(os);
  ASSERT_EQ("[0.1000-0.9999]", os.str());
}

TEST(NewSdpTestNoFixture, CheckImageattrSetSerialization)
{
  SdpImageattrAttributeList::Set set;
  std::ostringstream os;

  set.xRange.discreteValues.push_back(640);
  set.yRange.discreteValues.push_back(480);
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480]", os.str());
  os.str("");

  set.qValue = 0.00f;
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480,q=0.00]", os.str());
  os.str("");

  set.qValue = 0.10f;
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480,q=0.10]", os.str());
  os.str("");

  set.qValue = 1.00f;
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480,q=1.00]", os.str());
  os.str("");

  set.sRange.discreteValues.push_back(1.1f);
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480,sar=1.1000,q=1.00]", os.str());
  os.str("");

  set.pRange.min = 0.9f;
  set.pRange.max = 1.1f;
  set.Serialize(os);
  ASSERT_EQ("[x=640,y=480,sar=1.1000,par=[0.9000-1.1000],q=1.00]", os.str());
  os.str("");
}

TEST(NewSdpTestNoFixture, CheckImageattrSerialization)
{
  SdpImageattrAttributeList::Imageattr imageattr;
  std::ostringstream os;

  imageattr.sendAll = true;
  imageattr.pt = Some<uint16_t>(8U);
  imageattr.Serialize(os);
  ASSERT_EQ("8 send *", os.str());
  os.str("");

  imageattr.pt.reset();
  ;
  imageattr.Serialize(os);
  ASSERT_EQ("* send *", os.str());
  os.str("");

  imageattr.sendAll = false;
  imageattr.recvAll = true;
  imageattr.Serialize(os);
  ASSERT_EQ("* recv *", os.str());
  os.str("");

  imageattr.sendAll = true;
  imageattr.Serialize(os);
  ASSERT_EQ("* send * recv *", os.str());
  os.str("");

  imageattr.sendAll = false;
  imageattr.sendSets.push_back(SdpImageattrAttributeList::Set());
  imageattr.sendSets.back().xRange.discreteValues.push_back(320);
  imageattr.sendSets.back().yRange.discreteValues.push_back(240);
  imageattr.Serialize(os);
  ASSERT_EQ("* send [x=320,y=240] recv *", os.str());
  os.str("");

  imageattr.sendSets.push_back(SdpImageattrAttributeList::Set());
  imageattr.sendSets.back().xRange.discreteValues.push_back(640);
  imageattr.sendSets.back().yRange.discreteValues.push_back(480);
  imageattr.Serialize(os);
  ASSERT_EQ("* send [x=320,y=240] [x=640,y=480] recv *", os.str());
  os.str("");

  imageattr.recvAll = false;
  imageattr.recvSets.push_back(SdpImageattrAttributeList::Set());
  imageattr.recvSets.back().xRange.discreteValues.push_back(320);
  imageattr.recvSets.back().yRange.discreteValues.push_back(240);
  imageattr.Serialize(os);
  ASSERT_EQ("* send [x=320,y=240] [x=640,y=480] recv [x=320,y=240]", os.str());
  os.str("");

  imageattr.recvSets.push_back(SdpImageattrAttributeList::Set());
  imageattr.recvSets.back().xRange.discreteValues.push_back(640);
  imageattr.recvSets.back().yRange.discreteValues.push_back(480);
  imageattr.Serialize(os);
  ASSERT_EQ(
      "* send [x=320,y=240] [x=640,y=480] recv [x=320,y=240] [x=640,y=480]",
      os.str());
  os.str("");
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionSerialize)
{
  std::ostringstream os;

  SdpSimulcastAttribute::Version version;
  version.choices.push_back(SdpSimulcastAttribute::Encoding("8", false));
  version.Serialize(os);
  ASSERT_EQ("8", os.str());
  os.str("");

  version.choices.push_back(SdpSimulcastAttribute::Encoding("9", true));
  version.Serialize(os);
  ASSERT_EQ("8,~9", os.str());
  os.str("");

  version.choices.push_back(SdpSimulcastAttribute::Encoding("0", false));
  version.Serialize(os);
  ASSERT_EQ("8,~9,0", os.str());
  os.str("");
}

static SdpSimulcastAttribute::Version ParseSimulcastVersion(
    const std::string& input) {
  std::istringstream is(input + ";");
  std::string error;
  SdpSimulcastAttribute::Version version;
  EXPECT_TRUE(version.Parse(is, &error)) << error;
  EXPECT_EQ(';', is.get());
  EXPECT_EQ(EOF, is.get());
  return version;
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionValidParse)
{
  {
    SdpSimulcastAttribute::Version version(ParseSimulcastVersion("1"));
    ASSERT_EQ(1U, version.choices.size());
    ASSERT_EQ("1", version.choices[0].rid);
    ASSERT_FALSE(version.choices[0].paused);
  }

  {
    SdpSimulcastAttribute::Version version(ParseSimulcastVersion("1,~2"));
    ASSERT_EQ(2U, version.choices.size());
    ASSERT_EQ("1", version.choices[0].rid);
    ASSERT_EQ("2", version.choices[1].rid);
    ASSERT_FALSE(version.choices[0].paused);
    ASSERT_TRUE(version.choices[1].paused);
  }
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionInvalidParse)
{
  ParseInvalid<SdpSimulcastAttribute::Version>("", 0);
  ParseInvalid<SdpSimulcastAttribute::Version>(",", 0);
  ParseInvalid<SdpSimulcastAttribute::Version>(";", 0);
  ParseInvalid<SdpSimulcastAttribute::Version>(" ", 0);
  ParseInvalid<SdpSimulcastAttribute::Version>("8,", 2);
  ParseInvalid<SdpSimulcastAttribute::Version>("8, ", 2);
  ParseInvalid<SdpSimulcastAttribute::Version>("8,,", 2);
  ParseInvalid<SdpSimulcastAttribute::Version>("8,;", 2);
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionsSerialize)
{
  std::ostringstream os;

  SdpSimulcastAttribute::Versions versions;
  versions.push_back(SdpSimulcastAttribute::Version());
  versions.back().choices.push_back(
      SdpSimulcastAttribute::Encoding("8", false));
  versions.Serialize(os);
  ASSERT_EQ("8", os.str());
  os.str("");

  versions.push_back(SdpSimulcastAttribute::Version());
  versions.Serialize(os);
  ASSERT_EQ("8", os.str());
  os.str("");

  versions.back().choices.push_back(SdpSimulcastAttribute::Encoding("9", true));
  versions.Serialize(os);
  ASSERT_EQ("8;~9", os.str());
  os.str("");

  versions.push_back(SdpSimulcastAttribute::Version());
  versions.back().choices.push_back(
      SdpSimulcastAttribute::Encoding("0", false));
  versions.Serialize(os);
  ASSERT_EQ("8;~9;0", os.str());
  os.str("");
}

static SdpSimulcastAttribute::Versions ParseSimulcastVersions(
    const std::string& input) {
  std::istringstream is(input + " ");
  std::string error;
  SdpSimulcastAttribute::Versions list;
  EXPECT_TRUE(list.Parse(is, &error)) << error;
  EXPECT_EQ(' ', is.get());
  EXPECT_EQ(EOF, is.get());
  return list;
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionsValidParse)
{
  {
    SdpSimulcastAttribute::Versions versions(ParseSimulcastVersions("8"));
    ASSERT_EQ(1U, versions.size());
    ASSERT_EQ(1U, versions[0].choices.size());
    ASSERT_EQ("8", versions[0].choices[0].rid);
    ASSERT_FALSE(versions[0].choices[0].paused);
  }

  {
    SdpSimulcastAttribute::Versions versions(ParseSimulcastVersions("~8,9"));
    ASSERT_EQ(1U, versions.size());
    ASSERT_EQ(2U, versions[0].choices.size());
    ASSERT_EQ("8", versions[0].choices[0].rid);
    ASSERT_EQ("9", versions[0].choices[1].rid);
    ASSERT_TRUE(versions[0].choices[0].paused);
    ASSERT_FALSE(versions[0].choices[1].paused);
  }

  {
    SdpSimulcastAttribute::Versions versions(ParseSimulcastVersions("8,9;~10"));
    ASSERT_EQ(2U, versions.size());
    ASSERT_EQ(2U, versions[0].choices.size());
    ASSERT_EQ("8", versions[0].choices[0].rid);
    ASSERT_EQ("9", versions[0].choices[1].rid);
    ASSERT_FALSE(versions[0].choices[0].paused);
    ASSERT_FALSE(versions[0].choices[1].paused);
    ASSERT_EQ(1U, versions[1].choices.size());
    ASSERT_EQ("10", versions[1].choices[0].rid);
    ASSERT_TRUE(versions[1].choices[0].paused);
  }
}

TEST(NewSdpTestNoFixture, CheckSimulcastVersionsInvalidParse)
{
  ParseInvalid<SdpSimulcastAttribute::Versions>("", 0);
  ParseInvalid<SdpSimulcastAttribute::Versions>(";", 0);
  ParseInvalid<SdpSimulcastAttribute::Versions>("8;", 2);
  ParseInvalid<SdpSimulcastAttribute::Versions>("8;;", 2);
}

TEST(NewSdpTestNoFixture, CheckSimulcastSerialize)
{
  std::ostringstream os;

  SdpSimulcastAttribute simulcast;
  simulcast.recvVersions.push_back(SdpSimulcastAttribute::Version());
  simulcast.recvVersions.back().choices.push_back(
      SdpSimulcastAttribute::Encoding("8", false));
  simulcast.Serialize(os);
  ASSERT_EQ("a=simulcast:recv 8" CRLF, os.str());
  os.str("");

  simulcast.sendVersions.push_back(SdpSimulcastAttribute::Version());
  simulcast.sendVersions.back().choices.push_back(
      SdpSimulcastAttribute::Encoding("9", true));
  simulcast.Serialize(os);
  ASSERT_EQ("a=simulcast:send ~9 recv 8" CRLF, os.str());
}

static SdpSimulcastAttribute ParseSimulcast(const std::string& input) {
  std::istringstream is(input);
  std::string error;
  SdpSimulcastAttribute simulcast;
  EXPECT_TRUE(simulcast.Parse(is, &error)) << error;
  EXPECT_TRUE(is.eof());
  return simulcast;
}

TEST(NewSdpTestNoFixture, CheckSimulcastValidParse)
{
  {
    SdpSimulcastAttribute simulcast(ParseSimulcast("send 8"));
    ASSERT_EQ(1U, simulcast.sendVersions.size());
    ASSERT_EQ(1U, simulcast.sendVersions[0].choices.size());
    ASSERT_EQ("8", simulcast.sendVersions[0].choices[0].rid);
    ASSERT_FALSE(simulcast.sendVersions[0].choices[0].paused);
    ASSERT_EQ(0U, simulcast.recvVersions.size());
  }

  {
    SdpSimulcastAttribute simulcast(ParseSimulcast(" SEND 8"));
    ASSERT_EQ(1U, simulcast.sendVersions.size());
    ASSERT_EQ(1U, simulcast.sendVersions[0].choices.size());
    ASSERT_EQ("8", simulcast.sendVersions[0].choices[0].rid);
    ASSERT_FALSE(simulcast.sendVersions[0].choices[0].paused);
    ASSERT_EQ(0U, simulcast.recvVersions.size());
  }

  {
    SdpSimulcastAttribute simulcast(ParseSimulcast("recv 8"));
    ASSERT_EQ(1U, simulcast.recvVersions.size());
    ASSERT_EQ(1U, simulcast.recvVersions[0].choices.size());
    ASSERT_EQ("8", simulcast.recvVersions[0].choices[0].rid);
    ASSERT_FALSE(simulcast.recvVersions[0].choices[0].paused);
    ASSERT_EQ(0U, simulcast.sendVersions.size());
  }

  {
    SdpSimulcastAttribute simulcast(
        ParseSimulcast("send 8,9;~101;97,~98 recv 101,120;97"));
    ASSERT_EQ(3U, simulcast.sendVersions.size());
    ASSERT_EQ(2U, simulcast.sendVersions[0].choices.size());
    ASSERT_EQ("8", simulcast.sendVersions[0].choices[0].rid);
    ASSERT_EQ("9", simulcast.sendVersions[0].choices[1].rid);
    ASSERT_FALSE(simulcast.sendVersions[0].choices[0].paused);
    ASSERT_FALSE(simulcast.sendVersions[0].choices[1].paused);
    ASSERT_EQ(1U, simulcast.sendVersions[1].choices.size());
    ASSERT_EQ("101", simulcast.sendVersions[1].choices[0].rid);
    ASSERT_TRUE(simulcast.sendVersions[1].choices[0].paused);
    ASSERT_EQ(2U, simulcast.sendVersions[2].choices.size());
    ASSERT_EQ("97", simulcast.sendVersions[2].choices[0].rid);
    ASSERT_EQ("98", simulcast.sendVersions[2].choices[1].rid);
    ASSERT_FALSE(simulcast.sendVersions[2].choices[0].paused);
    ASSERT_TRUE(simulcast.sendVersions[2].choices[1].paused);

    ASSERT_EQ(2U, simulcast.recvVersions.size());
    ASSERT_EQ(2U, simulcast.recvVersions[0].choices.size());
    ASSERT_EQ("101", simulcast.recvVersions[0].choices[0].rid);
    ASSERT_EQ("120", simulcast.recvVersions[0].choices[1].rid);
    ASSERT_EQ(1U, simulcast.recvVersions[1].choices.size());
    ASSERT_EQ("97", simulcast.recvVersions[1].choices[0].rid);
  }
}

TEST(NewSdpTestNoFixture, CheckSimulcastInvalidParse)
{
  ParseInvalid<SdpSimulcastAttribute>("", 0);
  ParseInvalid<SdpSimulcastAttribute>(" ", 1);
  ParseInvalid<SdpSimulcastAttribute>("vcer ", 4);
  ParseInvalid<SdpSimulcastAttribute>(" send 8 send ", 12);
  ParseInvalid<SdpSimulcastAttribute>(" recv 8 recv ", 12);
}

static SdpRidAttributeList::Rid ParseRid(const std::string& input) {
  std::istringstream is(input);
  std::string error;
  SdpRidAttributeList::Rid rid;
  EXPECT_TRUE(rid.Parse(is, &error)) << error;
  EXPECT_TRUE(is.eof());
  return rid;
}

TEST(NewSdpTestNoFixture, CheckRidValidParse)
{
  {
    SdpRidAttributeList::Rid rid(ParseRid("1 send"));
    ASSERT_EQ("1", rid.id);
    ASSERT_EQ(sdp::kSend, rid.direction);
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("1 send pt=96;max-width=800"));
    ASSERT_EQ("1", rid.id);
    ASSERT_EQ(sdp::kSend, rid.direction);
    ASSERT_EQ(1U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("1 send pt=96,97,98;max-width=800"));
    ASSERT_EQ("1", rid.id);
    ASSERT_EQ(sdp::kSend, rid.direction);
    ASSERT_EQ(3U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(97U, rid.formats[1]);
    ASSERT_EQ(98U, rid.formats[2]);
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send pt=96"));
    ASSERT_EQ(1U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  // This is not technically permitted by the BNF, but the parse code is simpler
  // if we allow it. If we decide to stop allowing this, this will need to be
  // converted to an invalid parse test-case.
  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-br=30000;pt=96"));
    ASSERT_EQ(1U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(30000U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send pt=96,97,98"));
    ASSERT_EQ(3U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(97U, rid.formats[1]);
    ASSERT_EQ(98U, rid.formats[2]);
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-width=800"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-height=640"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(640U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-fps=30"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_EQ(30.0, *rid.constraints.maxFps);
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-fs=3600"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(3600U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-br=30000"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(30000U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-pps=9216000"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(9216000U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send depend=foo"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(1U, rid.dependIds.size());
    ASSERT_EQ("foo", rid.dependIds[0]);
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send max-foo=20"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(ParseRid("foo send depend=foo,bar"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(0U, rid.constraints.maxWidth);
    ASSERT_EQ(0U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(2U, rid.dependIds.size());
    ASSERT_EQ("foo", rid.dependIds[0]);
    ASSERT_EQ("bar", rid.dependIds[1]);
  }

  {
    SdpRidAttributeList::Rid rid(
        ParseRid("foo send max-width=800;max-height=600"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(600U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(
        ParseRid("foo send pt=96,97;max-width=800;max-height=600"));
    ASSERT_EQ(2U, rid.formats.size());
    ASSERT_EQ(96U, rid.formats[0]);
    ASSERT_EQ(97U, rid.formats[1]);
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(600U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }

  {
    SdpRidAttributeList::Rid rid(
        ParseRid("foo send depend=foo,bar;max-width=800;max-height=600"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(600U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(2U, rid.dependIds.size());
    ASSERT_EQ("foo", rid.dependIds[0]);
    ASSERT_EQ("bar", rid.dependIds[1]);
  }

  {
    SdpRidAttributeList::Rid rid(
        ParseRid("foo send max-foo=20;max-width=800;max-height=600"));
    ASSERT_EQ(0U, rid.formats.size());
    ASSERT_EQ(800U, rid.constraints.maxWidth);
    ASSERT_EQ(600U, rid.constraints.maxHeight);
    ASSERT_FALSE(rid.constraints.maxFps.isSome());
    ASSERT_EQ(0U, rid.constraints.maxFs);
    ASSERT_EQ(0U, rid.constraints.maxBr);
    ASSERT_EQ(0U, rid.constraints.maxPps);
    ASSERT_EQ(0U, rid.dependIds.size());
  }
}

TEST(NewSdpTestNoFixture, CheckRidInvalidParse)
{
  ParseInvalid<SdpRidAttributeList::Rid>("", 0);
  ParseInvalid<SdpRidAttributeList::Rid>(" ", 0);
  ParseInvalid<SdpRidAttributeList::Rid>("foo", 3);
  ParseInvalid<SdpRidAttributeList::Rid>("foo ", 4);
  ParseInvalid<SdpRidAttributeList::Rid>("foo  ", 5);
  ParseInvalid<SdpRidAttributeList::Rid>("foo bar", 7);
  ParseInvalid<SdpRidAttributeList::Rid>("foo recv ", 9);
  ParseInvalid<SdpRidAttributeList::Rid>("foo recv pt=", 12);
  ParseInvalid<SdpRidAttributeList::Rid>(" ", 0);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt", 11);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt=", 12);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt=x", 12);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt=-1", 12);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt=96,", 15);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send pt=196", 15);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width", 18);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width=", 19);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width=x", 19);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width=-1", 19);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width=800;", 23);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send max-width=800; ", 24);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send depend=", 16);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send depend=,", 16);
  ParseInvalid<SdpRidAttributeList::Rid>("foo send depend=1,", 18);
  ParseInvalid<SdpRidAttributeList::Rid>("0123456789az-_", 14);
}

TEST(NewSdpTestNoFixture, CheckRidSerialize)
{
  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.formats.push_back(96);
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send pt=96", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.formats.push_back(96);
    rid.formats.push_back(97);
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send pt=96,97", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxWidth = 800;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-width=800", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxHeight = 600;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-height=600", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxFps = Some(30);
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-fps=30", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxFs = 3600;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-fs=3600", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxBr = 30000;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-br=30000", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.constraints.maxPps = 9216000;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send max-pps=9216000", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.dependIds.push_back("foo");
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send depend=foo", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.dependIds.push_back("foo");
    rid.dependIds.push_back("bar");
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send depend=foo,bar", os.str());
  }

  {
    SdpRidAttributeList::Rid rid;
    rid.id = "foo";
    rid.direction = sdp::kSend;
    rid.formats.push_back(96);
    rid.constraints.maxBr = 30000;
    std::ostringstream os;
    rid.Serialize(os);
    ASSERT_EQ("foo send pt=96;max-br=30000", os.str());
  }
}

TEST_F(SdpTest, hugeSdp) {
  std::string offer =
      "v=0\r\n"
      "o=- 1109973417102828257 2 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE audio video\r\n"
      "a=msid-semantic: WMS 1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIP\r\n"
      "m=audio 32952 UDP/TLS/RTP/SAVPF 111 103 104 0 8 107 106 105 13 126\r\n"
      "c=IN IP4 128.64.32.16\r\n"
      "a=rtcp:32952 IN IP4 128.64.32.16\r\n"
      "a=candidate:77142221 1 udp 2113937151 192.168.137.1 54081 typ host "
      "generation 0\r\n"
      "a=candidate:77142221 2 udp 2113937151 192.168.137.1 54081 typ host "
      "generation 0\r\n"
      "a=candidate:983072742 1 udp 2113937151 172.22.0.56 54082 typ host "
      "generation 0\r\n"
      "a=candidate:983072742 2 udp 2113937151 172.22.0.56 54082 typ host "
      "generation 0\r\n"
      "a=candidate:2245074553 1 udp 1845501695 32.64.128.1 62397 typ srflx "
      "raddr 192.168.137.1 rport 54081 generation 0\r\n"
      "a=candidate:2245074553 2 udp 1845501695 32.64.128.1 62397 typ srflx "
      "raddr 192.168.137.1 rport 54081 generation 0\r\n"
      "a=candidate:2479353907 1 udp 1845501695 32.64.128.1 54082 typ srflx "
      "raddr 172.22.0.56 rport 54082 generation 0\r\n"
      "a=candidate:2479353907 2 udp 1845501695 32.64.128.1 54082 typ srflx "
      "raddr 172.22.0.56 rport 54082 generation 0\r\n"
      "a=candidate:1243276349 1 tcp 1509957375 192.168.137.1 0 typ host "
      "generation 0\r\n"
      "a=candidate:1243276349 2 tcp 1509957375 192.168.137.1 0 typ host "
      "generation 0\r\n"
      "a=candidate:1947960086 1 tcp 1509957375 172.22.0.56 0 typ host "
      "generation 0\r\n"
      "a=candidate:1947960086 2 tcp 1509957375 172.22.0.56 0 typ host "
      "generation 0\r\n"
      "a=candidate:1808221584 1 udp 33562367 128.64.32.16 32952 typ relay "
      "raddr 32.64.128.1 rport 62398 generation 0\r\n"
      "a=candidate:1808221584 2 udp 33562367 128.64.32.16 32952 typ relay "
      "raddr 32.64.128.1 rport 62398 generation 0\r\n"
      "a=candidate:507872740 1 udp 33562367 128.64.32.16 40975 typ relay raddr "
      "32.64.128.1 rport 54085 generation 0\r\n"
      "a=candidate:507872740 2 udp 33562367 128.64.32.16 40975 typ relay raddr "
      "32.64.128.1 rport 54085 generation 0\r\n"
      "a=ice-ufrag:xQuJwjX3V3eMA81k\r\n"
      "a=ice-pwd:ZUiRmjS2GDhG140p73dAsSVP\r\n"
      "a=ice-options:google-ice\r\n"
      "a=fingerprint:sha-256 "
      "59:4A:8B:73:A7:73:53:71:88:D7:4D:58:28:0C:79:72:31:29:9B:05:37:DD:58:43:"
      "C2:D4:85:A2:B3:66:38:7A\r\n"
      "a=setup:active\r\n"
      "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=rtcp-mux\r\n"
      "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
      "inline:/U44g3ULdtapeiSg+T3n6dDLBKIjpOhb/NXAL/2b\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=fmtp:111 minptime=10\r\n"
      "a=rtpmap:103 ISAC/16000\r\n"
      "a=rtpmap:104 ISAC/32000\r\n"
      "a=rtpmap:0 PCMU/8000\r\n"
      "a=rtpmap:8 PCMA/8000\r\n"
      "a=rtpmap:107 CN/48000\r\n"
      "a=rtpmap:106 CN/32000\r\n"
      "a=rtpmap:105 CN/16000\r\n"
      "a=rtpmap:13 CN/8000\r\n"
      "a=rtpmap:126 telephone-event/8000\r\n"
      "a=maxptime:60\r\n"
      "a=ssrc:2271517329 cname:mKDNt7SQf6pwDlIn\r\n"
      "a=ssrc:2271517329 msid:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIP "
      "1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIPa0\r\n"
      "a=ssrc:2271517329 mslabel:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIP\r\n"
      "a=ssrc:2271517329 label:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIPa0\r\n"
      "m=video 32952 UDP/TLS/RTP/SAVPF 100 116 117\r\n"
      "c=IN IP4 128.64.32.16\r\n"
      "a=rtcp:32952 IN IP4 128.64.32.16\r\n"
      "a=candidate:77142221 1 udp 2113937151 192.168.137.1 54081 typ host "
      "generation 0\r\n"
      "a=candidate:77142221 2 udp 2113937151 192.168.137.1 54081 typ host "
      "generation 0\r\n"
      "a=candidate:983072742 1 udp 2113937151 172.22.0.56 54082 typ host "
      "generation 0\r\n"
      "a=candidate:983072742 2 udp 2113937151 172.22.0.56 54082 typ host "
      "generation 0\r\n"
      "a=candidate:2245074553 1 udp 1845501695 32.64.128.1 62397 typ srflx "
      "raddr 192.168.137.1 rport 54081 generation 0\r\n"
      "a=candidate:2245074553 2 udp 1845501695 32.64.128.1 62397 typ srflx "
      "raddr 192.168.137.1 rport 54081 generation 0\r\n"
      "a=candidate:2479353907 1 udp 1845501695 32.64.128.1 54082 typ srflx "
      "raddr 172.22.0.56 rport 54082 generation 0\r\n"
      "a=candidate:2479353907 2 udp 1845501695 32.64.128.1 54082 typ srflx "
      "raddr 172.22.0.56 rport 54082 generation 0\r\n"
      "a=candidate:1243276349 1 tcp 1509957375 192.168.137.1 0 typ host "
      "generation 0\r\n"
      "a=candidate:1243276349 2 tcp 1509957375 192.168.137.1 0 typ host "
      "generation 0\r\n"
      "a=candidate:1947960086 1 tcp 1509957375 172.22.0.56 0 typ host "
      "generation 0\r\n"
      "a=candidate:1947960086 2 tcp 1509957375 172.22.0.56 0 typ host "
      "generation 0\r\n"
      "a=candidate:1808221584 1 udp 33562367 128.64.32.16 32952 typ relay "
      "raddr 32.64.128.1 rport 62398 generation 0\r\n"
      "a=candidate:1808221584 2 udp 33562367 128.64.32.16 32952 typ relay "
      "raddr 32.64.128.1 rport 62398 generation 0\r\n"
      "a=candidate:507872740 1 udp 33562367 128.64.32.16 40975 typ relay raddr "
      "32.64.128.1 rport 54085 generation 0\r\n"
      "a=candidate:507872740 2 udp 33562367 128.64.32.16 40975 typ relay raddr "
      "32.64.128.1 rport 54085 generation 0\r\n"
      "a=ice-ufrag:xQuJwjX3V3eMA81k\r\n"
      "a=ice-pwd:ZUiRmjS2GDhG140p73dAsSVP\r\n"
      "a=ice-options:google-ice\r\n"
      "a=fingerprint:sha-256 "
      "59:4A:8B:73:A7:73:53:71:88:D7:4D:58:28:0C:79:72:31:29:9B:05:37:DD:58:43:"
      "C2:D4:85:A2:B3:66:38:7A\r\n"
      "a=setup:active\r\n"
      "a=extmap:2 urn:ietf:params:rtp-hdrext:toffset\r\n"
      "a=extmap:3 "
      "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\n"
      "a=extmap:6 "
      "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay\r\n"
      "a=sendrecv\r\n"
      "a=mid:video\r\n"
      "a=rtcp-mux\r\n"
      "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
      "inline:/U44g3ULdtapeiSg+T3n6dDLBKIjpOhb/NXAL/2b\r\n"
      "a=rtpmap:100 VP8/90000\r\n"
      "a=rtcp-fb:100 ccm fir\r\n"
      "a=rtcp-fb:100 nack\r\n"
      "a=rtcp-fb:100 goog-remb\r\n"
      "a=rtpmap:116 red/90000\r\n"
      "a=rtpmap:117 ulpfec/90000\r\n"
      "a=ssrc:54724160 cname:mKDNt7SQf6pwDlIn\r\n"
      "a=ssrc:54724160 msid:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIP "
      "1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIPv0\r\n"
      "a=ssrc:54724160 mslabel:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIP\r\n"
      "a=ssrc:54724160 label:1PBxet5BYh0oYodwsvNM4k6KiO2eWCX40VIPv0\r\n";

  ParseSdp(offer);
}

TEST_P(NewSdpTest, CheckSsrcGroup) {
  ParseSdp(kVideoSdp + "a=ssrc-group:\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:BLAH\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 1 2 3 four\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 1\r\n");

  std::array<char, 8192> gigantic = {{0}};
  gigantic.fill('A');
  gigantic[gigantic.size() - 1] = 0;
  ParseSdp(kVideoSdp + "a=ssrc-group:" + gigantic.data() + "\r\n", false);

  ParseSdp(kVideoSdp + "a=ssrc-group:fid 1\r\n");
  ParseSdp(kVideoSdp + "a=ssrc-group:FID \r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 0\r\n");
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 9999999999\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 99999999999999999999\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID 1twothree\r\n", false);
  ParseSdp(kVideoSdp + "a=ssrc-group:FID -1\r\n", false);

  ParseSdp(kVideoSdp + "a=ssrc-group:FID 3156517279 2673335628\r\n");
  ASSERT_EQ(1U, Sdp()->GetMediaSectionCount());

  const SdpSsrcGroupAttributeList& group =
      Sdp()->GetMediaSection(0).GetAttributeList().GetSsrcGroup();

  ASSERT_EQ(1U, group.mSsrcGroups.size());
  ASSERT_EQ(SdpSsrcGroupAttributeList::Semantics::kFid,
            group.mSsrcGroups[0].semantics);
  ASSERT_EQ(2U, group.mSsrcGroups[0].ssrcs.size());
  ASSERT_EQ(3156517279U, group.mSsrcGroups[0].ssrcs[0]);
  ASSERT_EQ(2673335628U, group.mSsrcGroups[0].ssrcs[1]);
}

TEST_P(NewSdpTest, CheckSsrcGroupSerialization) {
  std::vector ssrcs = {3156517279U, 2673335628U};
  {
    SdpSsrcGroupAttributeList list;
    list.PushEntry(SdpSsrcGroupAttributeList::Semantics::kFec, ssrcs);
    CheckSerialize("a=ssrc-group:FEC 3156517279 2673335628\r\n", list);
  }
  {
    SdpSsrcGroupAttributeList list;
    list.PushEntry(SdpSsrcGroupAttributeList::Semantics::kFid, ssrcs);
    CheckSerialize("a=ssrc-group:FID 3156517279 2673335628\r\n", list);
  }
  {
    SdpSsrcGroupAttributeList list;
    list.PushEntry(SdpSsrcGroupAttributeList::Semantics::kFecFr, ssrcs);
    CheckSerialize("a=ssrc-group:FEC-FR 3156517279 2673335628\r\n", list);
  }
  {
    SdpSsrcGroupAttributeList list;
    list.PushEntry(SdpSsrcGroupAttributeList::Semantics::kDup, ssrcs);
    CheckSerialize("a=ssrc-group:DUP 3156517279 2673335628\r\n", list);
  }
  {
    SdpSsrcGroupAttributeList list;
    list.PushEntry(SdpSsrcGroupAttributeList::Semantics::kSim, ssrcs);
    CheckSerialize("a=ssrc-group:SIM 3156517279 2673335628\r\n", list);
  }
}

TEST_P(NewSdpTest, CheckRtxApt) {
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96", false);
  ParseSdp(kVideoSdp +
           "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120; apt=124\r\n");
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=", false);
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=blah", false);
  ParseSdp(kVideoSdp +
               "a=rtpmap:96 rtx/90000\r\na=fmtp:96 "
               "apt=999999999999999999999999999999",
           false);
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=-120\r\n",
           false);
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=1twenty\r\n",
           false);
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 APT=120\r\n");

  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120\r\n");

  const SdpMediaSection& msec = Sdp()->GetMediaSection(0);
  ASSERT_EQ(1U, msec.GetFormats().size());
  ASSERT_EQ(2U, msec.GetAttributeList().GetRtpmap().mRtpmaps.size());
  const SdpRtpmapAttributeList::Rtpmap& rtpmap =
      msec.GetAttributeList().GetRtpmap().GetEntry("96");
  ASSERT_EQ(rtpmap.pt, "96");
  ASSERT_EQ(rtpmap.codec, SdpRtpmapAttributeList::CodecType::kRtx);
  ASSERT_EQ(rtpmap.name, "rtx");
  ASSERT_EQ(rtpmap.clock, 90000U);

  ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
      SdpAttribute::kFmtpAttribute));
  const auto& format_params = msec.GetAttributeList().GetFmtp().mFmtps;
  ASSERT_EQ(1U, format_params.size());
  ASSERT_EQ("96", format_params[0].format);
  ASSERT_EQ(SdpRtpmapAttributeList::CodecType::kRtx,
            format_params[0].parameters->codec_type);
  ASSERT_EQ(static_cast<uint8_t>(120),
            static_cast<SdpFmtpAttributeList::RtxParameters*>(
                format_params[0].parameters.get())
                ->apt);
  ASSERT_EQ(Nothing(), static_cast<SdpFmtpAttributeList::RtxParameters*>(
                           format_params[0].parameters.get())
                           ->rtx_time);
}

TEST_P(NewSdpTest, CheckRtxAptRtxTime) {
  ParseSdp(kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;rtx-time=",
           false);
  ParseSdp(
      kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;rtx-time=blah",
      false);
  ParseSdp(kVideoSdp +
               "a=rtpmap:96 rtx/90000\r\na=fmtp:96 "
               "apt=120;rtx-time=9999999999999999999999999999999",
           false);
  ParseSdp(
      kVideoSdp + "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;rtx-time=-3000",
      false);
  ParseSdp(kVideoSdp +
               "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;rtx-time=3thousand",
           false);

  {
    ParseSdp(kVideoSdp +
             "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;RTX-TIME=3000\r\n");

    const SdpMediaSection& msec = Sdp()->GetMediaSection(0);
    ASSERT_EQ(1U, msec.GetFormats().size());
    ASSERT_EQ(2U, msec.GetAttributeList().GetRtpmap().mRtpmaps.size());
    const SdpRtpmapAttributeList::Rtpmap& rtpmap =
        msec.GetAttributeList().GetRtpmap().GetEntry("96");
    ASSERT_EQ(rtpmap.pt, "96");
    ASSERT_EQ(rtpmap.codec, SdpRtpmapAttributeList::CodecType::kRtx);
    ASSERT_EQ(rtpmap.name, "rtx");
    ASSERT_EQ(rtpmap.clock, 90000U);

    ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kFmtpAttribute));
    const auto& format_params = msec.GetAttributeList().GetFmtp().mFmtps;
    ASSERT_EQ(1U, format_params.size());
    ASSERT_EQ("96", format_params[0].format);
    ASSERT_EQ(SdpRtpmapAttributeList::CodecType::kRtx,
              format_params[0].parameters->codec_type);
    ASSERT_EQ(static_cast<uint8_t>(120),
              static_cast<SdpFmtpAttributeList::RtxParameters*>(
                  format_params[0].parameters.get())
                  ->apt);
    ASSERT_EQ(Some(3000U), static_cast<SdpFmtpAttributeList::RtxParameters*>(
                               format_params[0].parameters.get())
                               ->rtx_time);
  }

  {
    ParseSdp(kVideoSdp +
             "a=rtpmap:96 rtx/90000\r\na=fmtp:96 "
             "apt=120;rtx-time=3000;rtx-time=3300\r\n");

    const SdpMediaSection& msec = Sdp()->GetMediaSection(0);
    ASSERT_EQ(1U, msec.GetFormats().size());
    ASSERT_EQ(2U, msec.GetAttributeList().GetRtpmap().mRtpmaps.size());
    const SdpRtpmapAttributeList::Rtpmap& rtpmap =
        msec.GetAttributeList().GetRtpmap().GetEntry("96");
    ASSERT_EQ(rtpmap.pt, "96");
    ASSERT_EQ(rtpmap.codec, SdpRtpmapAttributeList::CodecType::kRtx);
    ASSERT_EQ(rtpmap.name, "rtx");
    ASSERT_EQ(rtpmap.clock, 90000U);

    ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kFmtpAttribute));
    const auto& format_params = msec.GetAttributeList().GetFmtp().mFmtps;
    ASSERT_EQ(1U, format_params.size());
    ASSERT_EQ("96", format_params[0].format);
    ASSERT_EQ(SdpRtpmapAttributeList::CodecType::kRtx,
              format_params[0].parameters->codec_type);
    ASSERT_EQ(static_cast<uint8_t>(120),
              static_cast<SdpFmtpAttributeList::RtxParameters*>(
                  format_params[0].parameters.get())
                  ->apt);

    // Last rtx-time wins. This parameter is unused by our implementation
    // anyway.
    ASSERT_EQ(Some(3300U), static_cast<SdpFmtpAttributeList::RtxParameters*>(
                               format_params[0].parameters.get())
                               ->rtx_time);
  }

  {
    ParseSdp(kVideoSdp +
             "a=rtpmap:96 rtx/90000\r\na=fmtp:96 apt=120;rtx-time=3000\r\n");

    const SdpMediaSection& msec = Sdp()->GetMediaSection(0);
    ASSERT_EQ(1U, msec.GetFormats().size());
    ASSERT_EQ(2U, msec.GetAttributeList().GetRtpmap().mRtpmaps.size());
    const SdpRtpmapAttributeList::Rtpmap& rtpmap =
        msec.GetAttributeList().GetRtpmap().GetEntry("96");
    ASSERT_EQ(rtpmap.pt, "96");
    ASSERT_EQ(rtpmap.codec, SdpRtpmapAttributeList::CodecType::kRtx);
    ASSERT_EQ(rtpmap.name, "rtx");
    ASSERT_EQ(rtpmap.clock, 90000U);

    ASSERT_TRUE(Sdp()->GetMediaSection(0).GetAttributeList().HasAttribute(
        SdpAttribute::kFmtpAttribute));
    const auto& format_params = msec.GetAttributeList().GetFmtp().mFmtps;
    ASSERT_EQ(1U, format_params.size());
    ASSERT_EQ("96", format_params[0].format);
    ASSERT_EQ(SdpRtpmapAttributeList::CodecType::kRtx,
              format_params[0].parameters->codec_type);
    ASSERT_EQ(static_cast<uint8_t>(120),
              static_cast<SdpFmtpAttributeList::RtxParameters*>(
                  format_params[0].parameters.get())
                  ->apt);
    ASSERT_EQ(Some(3000U), static_cast<SdpFmtpAttributeList::RtxParameters*>(
                               format_params[0].parameters.get())
                               ->rtx_time);
  }
}

// MediaTypeInfo deliberately has no operator==; MatchesTypeOf compares the
// media type alone, while IdenticalTo also compares the format list.
TEST(NewSdpTestNoFixture, CheckMediaTypeInfoComparison)
{
  const SdpMediaSection::MediaTypeInfo audio{.type = SdpMediaSection::kAudio};
  const SdpMediaSection::MediaTypeInfo video{.type = SdpMediaSection::kVideo};

  ASSERT_TRUE(audio.MatchesTypeOf(audio));
  ASSERT_TRUE(audio.IdenticalTo(audio));
  ASSERT_FALSE(audio.MatchesTypeOf(video));
  ASSERT_FALSE(audio.IdenticalTo(video));

  const auto unknown = [](const std::string& aName,
                          const std::string& aFormatString) {
    return SdpMediaSection::MediaTypeInfo{
        .type = SdpMediaSection::kUnknown,
        .unknown = Some(SdpMediaSection::UnknownMediaTypeData{
            .name = aName, .formatString = aFormatString})};
  };
  const auto sprinkles = unknown("sprinkles", "8 16 foo");

  // Media type names are case-insensitive for both comparisons, even though
  // the case an unknown media type was received in is what gets reproduced.
  ASSERT_TRUE(sprinkles.MatchesTypeOf(unknown("SPRINKLES", "8 16 foo")));
  ASSERT_TRUE(sprinkles.MatchesTypeOf(unknown("SpRiNkLeS", "8 16 foo")));
  ASSERT_TRUE(sprinkles.IdenticalTo(unknown("SPRINKLES", "8 16 foo")));

  // The format list is not part of the media type, and is compared verbatim
  ASSERT_TRUE(sprinkles.MatchesTypeOf(unknown("sprinkles", "0")));
  ASSERT_FALSE(sprinkles.IdenticalTo(unknown("sprinkles", "0")));
  ASSERT_FALSE(sprinkles.IdenticalTo(unknown("sprinkles", "8 16 FOO")));

  ASSERT_FALSE(sprinkles.MatchesTypeOf(unknown("clowncar", "8 16 foo")));
  ASSERT_FALSE(sprinkles.MatchesTypeOf(audio));
  ASSERT_FALSE(audio.MatchesTypeOf(sprinkles));
}

// A rejected m= section still has to fit the SDP grammar, which is why
// DisableMsection gives the media types it understands a placeholder format.
// The format list of an unknown media type is reproduced verbatim, so it must
// not gain a placeholder that its own m= line does not mention.
TEST(NewSdpTestNoFixture, CheckDisableMsectionKeepsUnknownFormats)
{
  SdpImpl sdp(SdpOrigin("-", 4294967296, 2, sdp::kIPv4, "127.0.0.1"));

  SdpMediaSection& unknown = sdp.AddMediaSection(
      {.type = SdpMediaSection::kUnknown,
       .unknown = Some(SdpMediaSection::UnknownMediaTypeData{
           .name = "sprinkles", .formatString = "8 16 foo"})},
      SdpDirectionAttribute::kSendrecv, 9, SdpMediaSection::kUdpTlsRtpSavpf,
      sdp::kIPv4, "0.0.0.0");
  SdpHelper::DisableMsection(&sdp, &unknown);

  ASSERT_EQ(0U, unknown.GetPort());
  ASSERT_FALSE(
      unknown.GetAttributeList().HasAttribute(SdpAttribute::kRtpmapAttribute));
  std::ostringstream unknownOs;
  unknown.Serialize(unknownOs);
  ASSERT_NE(
      std::string::npos,
      unknownOs.str().find("m=sprinkles 0 UDP/TLS/RTP/SAVPF 8 16 foo" CRLF));

  // An empty format list would leave the m= line without the fmt token the
  // grammar requires, so it gets the placeholder.
  SdpMediaSection& noFormats = sdp.AddMediaSection(
      {.type = SdpMediaSection::kUnknown,
       .unknown = Some(SdpMediaSection::UnknownMediaTypeData{
           .name = "clowncar", .formatString = ""})},
      SdpDirectionAttribute::kSendrecv, 9, SdpMediaSection::kUdpTlsRtpSavpf,
      sdp::kIPv4, "0.0.0.0");
  SdpHelper::DisableMsection(&sdp, &noFormats);

  ASSERT_FALSE(noFormats.GetAttributeList().HasAttribute(
      SdpAttribute::kRtpmapAttribute));
  std::ostringstream noFormatsOs;
  noFormats.Serialize(noFormatsOs);
  ASSERT_NE(std::string::npos,
            noFormatsOs.str().find("m=clowncar 0 UDP/TLS/RTP/SAVPF 19" CRLF));

  // A media type we do know gets the placeholder, which is the branch the
  // unknown media type has to stay out of.
  SdpMediaSection& audio = sdp.AddMediaSection(
      {.type = SdpMediaSection::kAudio}, SdpDirectionAttribute::kSendrecv, 9,
      SdpMediaSection::kUdpTlsRtpSavpf, sdp::kIPv4, "0.0.0.0");
  SdpHelper::DisableMsection(&sdp, &audio);

  ASSERT_EQ(0U, audio.GetPort());
  ASSERT_TRUE(
      audio.GetAttributeList().HasAttribute(SdpAttribute::kRtpmapAttribute));
}

}  // End namespace test.
