/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/media_engine.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "api/audio/audio_device.h"
#include "api/field_trials_view.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "call/audio_state.h"
#include "media/base/codec.h"
#include "rtc_base/system/file_wrapper.h"
#include "test/gmock.h"
#include "test/gtest.h"

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Return;
using ::testing::StrEq;
using ::webrtc::RtpExtension;
using ::webrtc::RtpHeaderExtensionCapability;
using ::webrtc::RtpTransceiverDirection;

namespace webrtc {
namespace {

class MockRtpHeaderExtensionQueryInterface
    : public RtpHeaderExtensionQueryInterface {
 public:
  MOCK_METHOD(std::vector<RtpHeaderExtensionCapability>,
              GetRtpHeaderExtensions,
              (const FieldTrialsView*),
              (const, override));
};

}  // namespace

TEST(MediaEngineTest, ReturnsNotStoppedHeaderExtensions) {
  MockRtpHeaderExtensionQueryInterface mock;
  std::vector<RtpHeaderExtensionCapability> extensions(
      {RtpHeaderExtensionCapability("uri1", RtpHeaderExtensionId(1),
                                    RtpTransceiverDirection::kInactive),
       RtpHeaderExtensionCapability("uri2", RtpHeaderExtensionId(2),
                                    RtpTransceiverDirection::kSendRecv),
       RtpHeaderExtensionCapability("uri3", RtpHeaderExtensionId(3),
                                    RtpTransceiverDirection::kStopped),
       RtpHeaderExtensionCapability("uri4", RtpHeaderExtensionId(4),
                                    RtpTransceiverDirection::kSendOnly),
       RtpHeaderExtensionCapability("uri5", RtpHeaderExtensionId(5),
                                    RtpTransceiverDirection::kRecvOnly)});
  EXPECT_CALL(mock, GetRtpHeaderExtensions).WillOnce(Return(extensions));
  EXPECT_THAT(
      GetDefaultEnabledRtpHeaderCapabilities(mock, nullptr),
      ElementsAre(Field(&RtpHeaderExtensionCapability::uri, StrEq("uri1")),
                  Field(&RtpHeaderExtensionCapability::uri, StrEq("uri2")),
                  Field(&RtpHeaderExtensionCapability::uri, StrEq("uri4")),
                  Field(&RtpHeaderExtensionCapability::uri, StrEq("uri5"))));
}

// This class mocks methods declared as pure virtual in the interface.
// Since the tests are aiming to check the patterns of overrides, the
// functions with default implementations are not mocked.
class MostlyMockVoiceEngineInterface : public VoiceEngineInterface {
 public:
  MOCK_METHOD(std::vector<RtpHeaderExtensionCapability>,
              GetRtpHeaderExtensions,
              (const FieldTrialsView*),
              (const, override));
  MOCK_METHOD(void, Init, (), (override));
  MOCK_METHOD(void, Terminate, (), (override));
  MOCK_METHOD(scoped_refptr<AudioState>, GetAudioState, (), (const, override));
  MOCK_METHOD(std::vector<Codec>&, LegacySendCodecs, (), (const, override));
  MOCK_METHOD(std::vector<Codec>&, LegacyRecvCodecs, (), (const, override));
  MOCK_METHOD(bool,
              StartAecDump,
              (FileWrapper file, int64_t max_size_bytes),
              (override));
  MOCK_METHOD(void, StopAecDump, (), (override));
  MOCK_METHOD(std::optional<AudioDeviceModule::Stats>,
              GetAudioDeviceStats,
              (),
              (override));
};

TEST(MediaEngineTest, CheckScalabilityModeValuesUnsupportedCodec) {
  std::vector<Codec> send_codecs = {CreateVideoCodec(
      SdpVideoFormat(SdpVideoFormat::VP8(),
                     {ScalabilityMode::kL1T1, ScalabilityMode::kL1T2}))};

  RtpParameters params;
  params.encodings.resize(2);
  params.encodings[0].codec = send_codecs[0].ToCodecParameters();
  params.encodings[1].codec =
      CreateVideoCodec(SdpVideoFormat::AV1Profile0()).ToCodecParameters();

  RTCError error =
      CheckScalabilityModeValues(params, send_codecs, std::nullopt);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
  EXPECT_STREQ(error.message(),
               "Attempted to use an unsupported codec AV1 for encodings[1].");
}

TEST(MediaEngineTest,
     CheckScalabilityModeValuesUnsupportedScalabilityModeForCodec) {
  std::vector<Codec> send_codecs = {
      CreateVideoCodec(
          SdpVideoFormat(SdpVideoFormat::VP8(),
                         {ScalabilityMode::kL1T1, ScalabilityMode::kL1T2})),
      CreateVideoCodec(
          SdpVideoFormat(SdpVideoFormat::AV1Profile0(),
                         {ScalabilityMode::kL1T1, ScalabilityMode::kL2T2}))};

  RtpParameters params;
  params.encodings.resize(1);
  params.encodings[0].codec = send_codecs[0].ToCodecParameters();
  params.encodings[0].scalability_mode = "L2T2";

  RTCError error =
      CheckScalabilityModeValues(params, send_codecs, std::nullopt);
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
  EXPECT_STREQ(error.message(),
               "Attempted to use an unsupported scalabilityMode 'L2T2' for "
               "codec VP8 in encodings[0].");
}

TEST(MediaEngineTest,
     CheckScalabilityModeValuesSupportedBySecondMatchingCodecEntry) {
  std::vector<Codec> send_codecs = {
      CreateVideoCodec(SdpVideoFormat(
          "H264", {{"profile-level-id", "42e01f"}, {"packetization-mode", "1"}},
          {ScalabilityMode::kL1T1})),
      CreateVideoCodec(SdpVideoFormat(
          "H264", {{"profile-level-id", "42e034"}, {"packetization-mode", "1"}},
          {ScalabilityMode::kL1T1, ScalabilityMode::kL1T2}))};

  RtpParameters params;
  params.encodings.resize(1);
  params.encodings[0].codec = send_codecs[0].ToCodecParameters();
  params.encodings[0].scalability_mode = "L1T2";

  EXPECT_TRUE(
      CheckScalabilityModeValues(params, send_codecs, std::nullopt).ok());
}

}  // namespace webrtc
