/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EncoderConfig.h"
#include "MediaMIMETypes.h"
#include "PlatformDecoderModule.h"
#include "VideoUtils.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gtest/ScopedPrefSetter.h"
#include "mozilla/media/webrtc/CodecInfo.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using mozilla::WebrtcCodecInfo;

// Build a minimal EncoderConfig matching aMime for support checks
static EncoderConfig MakeWebrtcEncoderConfig(
    const MediaExtendedMIMEType& aMime) {
  CodecType codec = CodecType::Unknown;
  const nsCString& m = aMime.Type().AsString();
  if (m.EqualsLiteral("video/h264")) {
    codec = CodecType::H264;
  } else if (m.EqualsLiteral("video/vp8")) {
    codec = CodecType::VP8;
  } else if (m.EqualsLiteral("video/vp9")) {
    codec = CodecType::VP9;
  } else if (m.EqualsLiteral("video/av1")) {
    codec = CodecType::AV1;
  }
  EncoderConfig::CodecSpecific specific(void_t{});
  if (codec == CodecType::H264) {
    // We want to match WebRTC's default-signaled H264 profile-level-id
    // 0x42e01f (Constrained Baseline level 3.1). H264_PROFILE cannot
    // represent Constrained Baseline, so we choose H264_PROFILE_BASE
    // as the closest approximation. See: Bug 2040726
    specific = AsVariant(H264Specific(H264_PROFILE::H264_PROFILE_BASE,
                                      H264_LEVEL::H264_LEVEL_3_1,
                                      H264BitStreamFormat::ANNEXB));
  }
  return EncoderConfig(
      codec, gfx::IntSize(640, 480), Usage::Realtime,
      EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
      /* fps */ 30u, /* kf interval*/ 0, /* bitrate */ 0, /* br min */ 0,
      /* br max */ 0, BitrateMode::Variable, HardwarePreference::None,
      ScalabilityMode::None, specific);
}

constexpr const char* kVideoTypes[] = {
    "video/av1",
    "video/h264",
    "video/vp8",
    "video/vp9",
};

constexpr const char* kAudioTypes[] = {
    "audio/g722",
    "audio/opus",
    "audio/pcma",
    "audio/pcmu",
};

constexpr const char* kNonCanonicalMimeTypes[] = {
    "video/avc",
    "video/avc1",
    "video/avc3",
};

class WebRTCCodecInfoTest : public testing::Test {
 protected:
  void SetUp() override {
    // Ensure gfxVars is initialized before tests run.
    MOZ_ASSERT(NS_IsMainThread());
    if (!gfx::gfxVars::IsInitialized()) {
      gfx::gfxVars::Initialize();
    }
  }

  static media::EncodeSupportSet QueryEncode(
      const MediaExtendedMIMEType& aMime) {
    media::EncodeSupportSet result;
    bool done = false;
    SupportsVideoEncodeForWebrtc(MakeWebrtcEncoderConfig(aMime))
        ->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [&](media::EncodeSupportSet aSupport) {
              result = aSupport;
              done = true;
            },
            [&](nsresult) { done = true; });
    SpinEventLoopUntil("TestWebRTCCodecInfo::QueryEncode"_ns,
                       [&] { return done; });
    return result;
  }
  static media::DecodeSupportSet QueryDecode(
      const MediaExtendedMIMEType& aMime) {
    UniquePtr<TrackInfo> info =
        CreateTrackInfoWithMIMEType(aMime.Type().AsString());
    if (!info) {
      return {};
    }
    SupportDecoderParams params(*info);
    media::DecodeSupportSet result;
    bool done = false;
    SupportsVideoDecodeForWebrtc(aMime, params)
        ->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [&](media::DecodeSupportSet aSupport) {
              result = aSupport;
              done = true;
            },
            [&](nsresult) { done = true; });
    SpinEventLoopUntil("TestWebRTCCodecInfo::QueryDecode"_ns,
                       [&] { return done; });
    return result;
  }

  // Returns false if the MIME string is unparseable or unsupported.
  static bool SupportsSWEncode(const WebrtcCodecInfo& aInfo,
                               const char* aMime) {
    Maybe<MediaExtendedMIMEType> mime = MakeMediaExtendedMIMEType(aMime);
    return mime && aInfo.CheckEncodeType(*mime) &&
           (!mime->Type().HasVideoMajorType() ||
            QueryEncode(*mime).contains(media::EncodeSupport::SoftwareEncode));
  }
  static bool SupportsSWDecode(const WebrtcCodecInfo& aInfo,
                               const char* aMime) {
    Maybe<MediaExtendedMIMEType> mime = MakeMediaExtendedMIMEType(aMime);
    return mime && aInfo.CheckDecodeType(*mime) &&
           (!mime->Type().HasVideoMajorType() ||
            QueryDecode(*mime).contains(media::DecodeSupport::SoftwareDecode));
  }
  static bool SupportsHWEncode(const WebrtcCodecInfo& aInfo,
                               const char* aMime) {
    Maybe<MediaExtendedMIMEType> mime = MakeMediaExtendedMIMEType(aMime);
    return mime && aInfo.CheckEncodeType(*mime) &&
           mime->Type().HasVideoMajorType() &&
           QueryEncode(*mime).contains(media::EncodeSupport::HardwareEncode);
  }
  static bool SupportsHWDecode(const WebrtcCodecInfo& aInfo,
                               const char* aMime) {
    Maybe<MediaExtendedMIMEType> mime = MakeMediaExtendedMIMEType(aMime);
    return mime && aInfo.CheckDecodeType(*mime) &&
           mime->Type().HasVideoMajorType() &&
           QueryDecode(*mime).contains(media::DecodeSupport::HardwareDecode);
  }

  // Helper function used to verify audio encode/decode is still working for use
  // in the video pref tests.
  static void TestAudioDecodeEncodeSWHW(const WebrtcCodecInfo* aCodecInfo) {
    for (const auto& type : kAudioTypes) {
      EXPECT_TRUE(SupportsSWDecode(*aCodecInfo, type))
          << "Type failed: " << type;
      EXPECT_TRUE(SupportsSWEncode(*aCodecInfo, type))
          << "Type failed: " << type;
      EXPECT_FALSE(SupportsHWDecode(*aCodecInfo, type))
          << "Type failed: " << type;
      EXPECT_FALSE(SupportsHWEncode(*aCodecInfo, type))
          << "Type failed: " << type;
    }
  }
};

// Test that invalid MIME types return false for all support queries.
TEST_F(WebRTCCodecInfoTest, InvalidMimeType) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, ""));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "foobar"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/fake"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "/"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "/fake"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "fake/"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video fake"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/no; fake"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "\xe6\xbc\xa2\xe5\xad\x97"));
}

// Test that RTP supplementary codecs are filtered as per WPTs. See:
// https://searchfox.org/firefox-main/rev/f37efeb9fd346125bfc98d132ae0dea48a1e2584/testing/web-platform/tests/media-capabilities/decodingInfo-webrtc.any.js#221
TEST_F(WebRTCCodecInfoTest, FilterRTPSupplementaryCodecs) {
  const auto codecInfo = WebrtcCodecInfo::Create();

  // RTX - Retransmission (RFC 4588)
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/rtx"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/rtx"));

  // RED - Redundant encoding (RFC 2198)
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/red"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/red"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "audio/red"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "audio/red"));

  // ULPFEC - Forward error correction (RFC 5109)
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/ulpfec"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/ulpfec"));

  // FlexFEC - Flexible forward error correction
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/flexfec-03"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/flexfec-03"));

  // Telephone-event - DTMF tones (RFC 4733)
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "audio/telephone-event"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "audio/telephone-event"));

  // CN - Comfort Noise
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "audio/CN"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "audio/CN"));
}

// Test that audio MIME types are rejected when querying video support
// and vice versa.
TEST_F(WebRTCCodecInfoTest, AudioVideoMismatch) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  const auto swapMajorType = [](const char* aMime) -> std::string {
    std::string s(aMime);
    if (s.starts_with("video/")) {
      s.replace(0, 5, "audio");
    } else if (s.starts_with("audio/")) {
      s.replace(0, 5, "video");
    }
    return s;
  };
  for (const auto& type : kVideoTypes) {
    const auto swapped = swapMajorType(type);
    EXPECT_FALSE(SupportsSWDecode(*codecInfo, swapped.c_str()))
        << "Type failed: " << swapped;
    EXPECT_FALSE(SupportsSWEncode(*codecInfo, swapped.c_str()))
        << "Type failed: " << swapped;
  }
  for (const auto& type : kAudioTypes) {
    const auto swapped = swapMajorType(type);
    EXPECT_FALSE(SupportsSWDecode(*codecInfo, swapped.c_str()))
        << "Type failed: " << swapped;
    EXPECT_FALSE(SupportsSWEncode(*codecInfo, swapped.c_str()))
        << "Type failed: " << swapped;
  }
}

// Test that common WebRTC video codecs are supported (software)
TEST_F(WebRTCCodecInfoTest, CommonVideoCodecsSupported) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const auto& type : kVideoTypes) {
    EXPECT_TRUE(SupportsSWDecode(*codecInfo, type)) << "Type failed: " << type;
    EXPECT_TRUE(SupportsSWEncode(*codecInfo, type)) << "Type failed: " << type;
  }
}

// Test that common WebRTC audio codecs are supported.
TEST_F(WebRTCCodecInfoTest, CommonAudioCodecsSupported) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}

// Test that non-canonical WebRTC H264 MIME types are rejected.
TEST_F(WebRTCCodecInfoTest, H264NonCanonicalMimeTypes) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const auto& type : kNonCanonicalMimeTypes) {
    EXPECT_FALSE(SupportsSWEncode(*codecInfo, type));
    EXPECT_FALSE(SupportsSWDecode(*codecInfo, type));
  }
}

// Test case insensitivity of MIME type parsing.
TEST_F(WebRTCCodecInfoTest, CaseInsensitiveMimeTypes) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const auto& type : kVideoTypes) {
    nsAutoCString upper(type);
    ToUpperCase(upper);
    EXPECT_TRUE(SupportsSWEncode(*codecInfo, upper.get()))
        << "Type failed: " << upper.get();
    EXPECT_TRUE(SupportsSWDecode(*codecInfo, upper.get()))
        << "Type failed: " << upper.get();
  }
  for (const auto& type : kAudioTypes) {
    nsAutoCString upper(type);
    ToUpperCase(upper);
    EXPECT_TRUE(SupportsSWEncode(*codecInfo, upper.get()))
        << "Type failed: " << upper.get();
    EXPECT_TRUE(SupportsSWDecode(*codecInfo, upper.get()))
        << "Type failed: " << upper.get();
  }
}

// Test that H.264-specific WebRTC pref blocks HW encode/decode if false
TEST_F(WebRTCCodecInfoTest, H264HWBlockedByWebRTCPref) {
  const ScopedPrefSetter h264Pref("media.webrtc.hw.h264.enabled", false);
  const auto codecInfo = WebrtcCodecInfo::Create();
  EXPECT_FALSE(SupportsHWDecode(*codecInfo, "video/h264"));
  EXPECT_FALSE(SupportsHWEncode(*codecInfo, "video/h264"));
  // SW should still work
  EXPECT_TRUE(SupportsSWDecode(*codecInfo, "video/h264"));
  EXPECT_TRUE(SupportsSWEncode(*codecInfo, "video/h264"));
  // Audio shouldn't be affected
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}

// Test that AV1 WebRTC pref disables AV1 support if false
TEST_F(WebRTCCodecInfoTest, AV1BlockedByWebRTCPref) {
  const ScopedPrefSetter av1Pref("media.webrtc.codec.video.av1.enabled", false);
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const auto& type : kVideoTypes) {
    if (strcmp(type, "video/av1") == 0) {
      EXPECT_FALSE(SupportsSWDecode(*codecInfo, type)) << "Type: " << type;
      EXPECT_FALSE(SupportsSWEncode(*codecInfo, type)) << "Type: " << type;
    } else {
      EXPECT_TRUE(SupportsSWDecode(*codecInfo, type)) << "Type: " << type;
      EXPECT_TRUE(SupportsSWEncode(*codecInfo, type)) << "Type: " << type;
    }
  }
  // Audio shouldn't be affected
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}

// Test that H264 with invalid fmtp params is unsupported, while H264 with no
// or valid params remains supported.
TEST_F(WebRTCCodecInfoTest, H264InvalidFmtpParamsUnsupported) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const char* type :
       {"video/h264;profile-level-id=zze01f",
        "video/h264;profile-level-id=42e0",
        "video/h264;profile-level-id=f4001f", "video/h264;packetization-mode=3",
        "video/h264;packetization-mode=banana"}) {
    SCOPED_TRACE(type);
    EXPECT_FALSE(SupportsSWDecode(*codecInfo, type));
    EXPECT_FALSE(SupportsSWEncode(*codecInfo, type));
  }

  EXPECT_TRUE(SupportsSWDecode(*codecInfo, "video/h264"));
  EXPECT_TRUE(SupportsSWEncode(*codecInfo, "video/h264"));
  EXPECT_TRUE(SupportsSWDecode(
      *codecInfo, "video/h264;profile-level-id=42e01f;packetization-mode=1"));
  EXPECT_TRUE(SupportsSWEncode(
      *codecInfo, "video/h264;profile-level-id=42e01f;packetization-mode=1"));
}

// Test that AV1 with invalid fmtp params is unsupported, while other codecs
// (and AV1 with no or valid params) remain unaffected.
TEST_F(WebRTCCodecInfoTest, AV1InvalidFmtpParamsUnsupported) {
  const auto codecInfo = WebrtcCodecInfo::Create();
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/av1;profile=9"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/av1;profile=9"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/av1;level-idx=99"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/av1;level-idx=99"));
  EXPECT_FALSE(SupportsSWDecode(*codecInfo, "video/av1;tier=2"));
  EXPECT_FALSE(SupportsSWEncode(*codecInfo, "video/av1;tier=2"));

  EXPECT_TRUE(SupportsSWDecode(*codecInfo, "video/av1"));
  EXPECT_TRUE(SupportsSWEncode(*codecInfo, "video/av1"));
  EXPECT_TRUE(
      SupportsSWDecode(*codecInfo, "video/av1;profile=0;level-idx=9;tier=0"));
  EXPECT_TRUE(
      SupportsSWEncode(*codecInfo, "video/av1;profile=0;level-idx=9;tier=0"));

  // Other codecs shouldn't be affected.
  EXPECT_TRUE(SupportsSWDecode(*codecInfo, "video/h264"));
  EXPECT_TRUE(SupportsSWEncode(*codecInfo, "video/h264"));
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}

// Test that VP9 WebRTC pref disables VP9 support if false
TEST_F(WebRTCCodecInfoTest, VP9BlockedByWebRTCPref) {
  const ScopedPrefSetter vp9Pref("media.peerconnection.video.vp9_enabled",
                                 false);
  const auto codecInfo = WebrtcCodecInfo::Create();
  for (const auto& type : kVideoTypes) {
    if (strcmp(type, "video/vp9") == 0) {
      EXPECT_FALSE(SupportsSWDecode(*codecInfo, type)) << "Type: " << type;
      EXPECT_FALSE(SupportsSWEncode(*codecInfo, type)) << "Type: " << type;
    } else {
      EXPECT_TRUE(SupportsSWDecode(*codecInfo, type)) << "Type: " << type;
      EXPECT_TRUE(SupportsSWEncode(*codecInfo, type)) << "Type: " << type;
    }
  }
  // Audio shouldn't be affected
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}

// Test that the H.264 baseline disable pref does not affect non-baseline H264.
// Note: baseline profiles specifically cannot be verified here because
// CodecInfo does not yet inspect fmtp parameters (Bug 2024767).
TEST_F(WebRTCCodecInfoTest, H264BaselineBlockedByWebRTCPref) {
  const ScopedPrefSetter baselinePref(
      "media.navigator.video.disable_h264_baseline", true);
  const auto codecInfo = WebrtcCodecInfo::Create();
  // Non-baseline H264 should still be supported
  EXPECT_TRUE(SupportsSWDecode(*codecInfo, "video/h264"));
  EXPECT_TRUE(SupportsSWEncode(*codecInfo, "video/h264"));
  // Audio shouldn't be affected
  TestAudioDecodeEncodeSWHW(codecInfo.get());
}
