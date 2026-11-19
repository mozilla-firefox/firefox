/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#define GTEST_HAS_RTTI 0
#include "AudioConduit.h"
#include "Canonicals.h"
#include "MockCall.h"
#include "api/audio_codecs/opus/audio_encoder_opus_config.h"
#include "gtest/gtest.h"
#include "mozilla/Span.h"

using namespace mozilla;
using namespace testing;
using namespace webrtc;

namespace test {

class AudioConduitTest : public ::testing::Test {
 public:
  AudioConduitTest()
      : mCallWrapper(MockCallWrapper::Create()),
        mAudioConduit(MakeRefPtr<WebrtcAudioConduit>(
            mCallWrapper, GetCurrentSerialEventTarget())),
        mControl(GetCurrentSerialEventTarget()) {
    mControl.Update(
        [&](auto& /*aControl*/) { mAudioConduit->InitControl(&mControl); });
  }

  ~AudioConduitTest() override {
    (void)WaitFor(mAudioConduit->Shutdown());
    mCallWrapper->Destroy();
  }

  MockCall* Call() { return mCallWrapper->GetMockCall(); }

  const RefPtr<MockCallWrapper> mCallWrapper;
  const RefPtr<WebrtcAudioConduit> mAudioConduit;
  ConcreteControl mControl;
};

TEST_F(AudioConduitTest, TestNetworkStateFollowsTransportReadiness) {
  mControl.Update([&](auto& aControl) {
    aControl.mAudioSendCodec =
        Some(AudioCodecConfig(114, "opus", 48000, 2, false));
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  NS_ProcessPendingEvents(nullptr);
  auto& signals = Call()->mNetworkStateSignals;
  EXPECT_TRUE(signals.IsEmpty());

  mAudioConduit->SetTransportActive(true);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(Span(signals), ElementsAre(FieldsAre(webrtc::MediaType::AUDIO,
                                                   webrtc::kNetworkUp)));
  signals.Clear();

  mAudioConduit->SetTransportActive(false);
  NS_ProcessPendingEvents(nullptr);
  EXPECT_THAT(Span(signals), ElementsAre(FieldsAre(webrtc::MediaType::AUDIO,
                                                   webrtc::kNetworkDown)));
}

TEST_F(AudioConduitTest, TestConfigureSendMediaCodec) {
  mControl.Update([&](auto& aControl) {
    // defaults
    aControl.mAudioSendCodec =
        Some(AudioCodecConfig(114, "opus", 48000, 2, false));
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }

  mControl.Update([&](auto& aControl) {
    // empty codec name
    aControl.mAudioSendCodec = Some(AudioCodecConfig(114, "", 48000, 2, false));
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    // Invalid codec was ignored.
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusMono) {
  mControl.Update([&](auto& aControl) {
    // opus mono
    aControl.mAudioSendCodec =
        Some(AudioCodecConfig(114, "opus", 48000, 1, false));
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 1UL);
    ASSERT_EQ(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusFEC) {
  mControl.Update([&](auto& aControl) {
    // opus with inband Forward Error Correction
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, true);
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_NE(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("useinbandfec"), "1");
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusMaxPlaybackRate) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mMaxPlaybackRate = 1234;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_NE(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxplaybackrate"), "1234");
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusMaxAverageBitrate) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mMaxAverageBitrate = 12345;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_NE(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxaveragebitrate"), "12345");
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusDtx) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mDTXEnabled = true;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_NE(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("usedtx"), "1");
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusCbr) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mCbrEnabled = true;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_NE(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("cbr"), "1");
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusPtime) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mFrameSizeMs = 100;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_NE(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("ptime"), "100");
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusMinPtime) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mMinFrameSizeMs = 201;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_NE(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("minptime"), "201");
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusMaxPtime) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, false);
    codecConfig.mMaxFrameSizeMs = 321;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_NE(f.parameters.find("maxptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxptime"), "321");
  }
}

TEST_F(AudioConduitTest, TestConfigureSendOpusAllParams) {
  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig =
        AudioCodecConfig(114, "opus", 48000, 2, true);
    codecConfig.mMaxPlaybackRate = 5432;
    codecConfig.mMaxAverageBitrate = 54321;
    codecConfig.mDTXEnabled = true;
    codecConfig.mCbrEnabled = true;
    codecConfig.mFrameSizeMs = 999;
    codecConfig.mMinFrameSizeMs = 123;
    codecConfig.mMaxFrameSizeMs = 789;
    aControl.mAudioSendCodec = Some(codecConfig);
    aControl.mTransmitting = true;
  });

  ASSERT_TRUE(Call()->mAudioSendConfig);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioSendConfig->send_codec_spec->format;
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_NE(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("useinbandfec"), "1");
    ASSERT_NE(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxplaybackrate"), "5432");
    ASSERT_NE(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxaveragebitrate"), "54321");
    ASSERT_NE(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("usedtx"), "1");
    ASSERT_NE(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("cbr"), "1");
    ASSERT_NE(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("ptime"), "999");
    ASSERT_NE(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("minptime"), "123");
    ASSERT_NE(f.parameters.find("maxptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxptime"), "789");
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveMediaCodecs) {
  mControl.Update([&](auto& aControl) {
    // just default opus stereo
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, false));
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "");
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }

  mControl.Update([&](auto& aControl) {
    // multiple codecs
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(9, "g722", 16000, 2, false));
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, false));
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "");
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 2U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(9);
    ASSERT_EQ(f.name, "g722");
    ASSERT_EQ(f.clockrate_hz, 16000);
    ASSERT_EQ(f.num_channels, 2U);
    ASSERT_EQ(f.parameters.size(), 0U);
  }
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2U);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
  }

  mControl.Update([&](auto& aControl) {
    // no codecs
    std::vector<mozilla::AudioCodecConfig> codecs;
    aControl.mAudioRecvCodecs = codecs;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 0U);

  mControl.Update([&](auto& aControl) {
    // invalid codec name
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "", 48000, 2, false));
    aControl.mAudioRecvCodecs = codecs;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 0U);

  mControl.Update([&](auto& aControl) {
    // invalid number of channels
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 42, false));
    aControl.mAudioRecvCodecs = codecs;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 0U);
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusMono) {
  mControl.Update([&](auto& aControl) {
    // opus mono
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 1, false));
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "");
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 1UL);
    ASSERT_EQ(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusDtx) {
  mControl.Update([&](auto& aControl) {
    // opus mono
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, false));
    codecs[0].mDTXEnabled = true;
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "");
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_NE(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("usedtx"), "1");
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusFEC) {
  mControl.Update([&](auto& aControl) {
    // opus with inband Forward Error Correction
    std::vector<mozilla::AudioCodecConfig> codecs;
    codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, true));
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "");
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_NE(f.parameters.find("stereo"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_NE(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("useinbandfec"), "1");
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusMaxPlaybackRate) {
  std::vector<mozilla::AudioCodecConfig> codecs;
  codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, false));

  mControl.Update([&](auto& aControl) {
    codecs[0].mMaxPlaybackRate = 0;
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.count("maxplaybackrate"), 0U);
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }

  mControl.Update([&](auto& aControl) {
    codecs[0].mMaxPlaybackRate = 8000;
    aControl.mAudioRecvCodecs = codecs;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.at("maxplaybackrate"), "8000");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxaveragebitrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusMaxAverageBitrate) {
  std::vector<mozilla::AudioCodecConfig> codecs;
  codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, false));
  mControl.Update([&](auto& aControl) {
    codecs[0].mMaxAverageBitrate = 0;
    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.count("maxaveragebitrate"), 0U);
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }

  mControl.Update([&](auto& aControl) {
    codecs[0].mMaxAverageBitrate = 8000;
    aControl.mAudioRecvCodecs = codecs;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.find("useinbandfec"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxplaybackrate"), f.parameters.end());
    ASSERT_EQ(f.parameters.at("maxaveragebitrate"), "8000");
    ASSERT_EQ(f.parameters.find("usedtx"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("cbr"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("ptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("minptime"), f.parameters.end());
    ASSERT_EQ(f.parameters.find("maxptime"), f.parameters.end());
  }
}

TEST_F(AudioConduitTest, TestConfigureReceiveOpusAllParameters) {
  std::vector<mozilla::AudioCodecConfig> codecs;
  codecs.emplace_back(AudioCodecConfig(114, "opus", 48000, 2, true));

  mControl.Update([&](auto& aControl) {
    codecs[0].mMaxPlaybackRate = 8000;
    codecs[0].mMaxAverageBitrate = 9000;
    codecs[0].mDTXEnabled = true;
    codecs[0].mCbrEnabled = true;
    codecs[0].mFrameSizeMs = 10;
    codecs[0].mMinFrameSizeMs = 20;
    codecs[0].mMaxFrameSizeMs = 30;

    aControl.mAudioRecvCodecs = codecs;
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->decoder_map.size(), 1U);
  {
    const webrtc::SdpAudioFormat& f =
        Call()->mAudioReceiveConfig->decoder_map.at(114);
    ASSERT_EQ(f.name, "opus");
    ASSERT_EQ(f.clockrate_hz, 48000);
    ASSERT_EQ(f.num_channels, 2UL);
    ASSERT_EQ(f.parameters.at("stereo"), "1");
    ASSERT_EQ(f.parameters.at("useinbandfec"), "1");
    ASSERT_EQ(f.parameters.at("maxplaybackrate"), "8000");
    ASSERT_EQ(f.parameters.at("maxaveragebitrate"), "9000");
    ASSERT_EQ(f.parameters.at("usedtx"), "1");
    ASSERT_EQ(f.parameters.at("cbr"), "1");
    ASSERT_EQ(f.parameters.at("ptime"), "10");
    ASSERT_EQ(f.parameters.at("minptime"), "20");
    ASSERT_EQ(f.parameters.at("maxptime"), "30");
  }
}

TEST_F(AudioConduitTest, TestSetLocalRTPExtensions) {
  // Empty extensions
  mControl.Update([&](auto& aControl) {
    RtpExtList extensions;
    aControl.mLocalRecvRtpExtensions = extensions;
    aControl.mReceiving = true;
    aControl.mLocalSendRtpExtensions = extensions;
    aControl.mTransmitting = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_TRUE(Call()->mAudioSendConfig);
  ASSERT_TRUE(Call()->mAudioSendConfig->rtp.extensions.empty());

  // Audio level
  mControl.Update([&](auto& aControl) {
    RtpExtList extensions;
    webrtc::RtpExtension extension;
    extension.uri = webrtc::RtpExtension::kAudioLevelUri;
    extension.id = webrtc::RtpHeaderExtensionId(1);
    extensions.emplace_back(extension);
    aControl.mLocalRecvRtpExtensions = extensions;
    aControl.mLocalSendRtpExtensions = extensions;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_TRUE(Call()->mAudioSendConfig);
  ASSERT_EQ(Call()->mAudioSendConfig->rtp.extensions.back().uri,
            webrtc::RtpExtension::kAudioLevelUri);

  // Contributing sources audio level
  mControl.Update([&](auto& aControl) {
    // We do not support configuring sending csrc-audio-level. It will be
    // ignored.
    RtpExtList extensions;
    webrtc::RtpExtension extension;
    extension.uri = webrtc::RtpExtension::kCsrcAudioLevelsUri;
    extension.id = webrtc::RtpHeaderExtensionId(1);
    extensions.emplace_back(extension);
    aControl.mLocalRecvRtpExtensions = extensions;
    aControl.mLocalSendRtpExtensions = extensions;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_TRUE(Call()->mAudioSendConfig);
  ASSERT_TRUE(Call()->mAudioSendConfig->rtp.extensions.empty());

  // Mid
  mControl.Update([&](auto& aControl) {
    // We do not support configuring receiving MId. It will be ignored.
    RtpExtList extensions;
    webrtc::RtpExtension extension;
    extension.uri = webrtc::RtpExtension::kMidUri;
    extension.id = webrtc::RtpHeaderExtensionId(1);
    extensions.emplace_back(extension);
    aControl.mLocalRecvRtpExtensions = extensions;
    aControl.mLocalSendRtpExtensions = extensions;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioSendConfig->rtp.extensions.back().uri,
            webrtc::RtpExtension::kMidUri);
}

TEST_F(AudioConduitTest, TestSyncGroup) {
  mControl.Update([&](auto& aControl) {
    aControl.mSyncGroup = "test";
    aControl.mReceiving = true;
  });
  ASSERT_TRUE(Call()->mAudioReceiveConfig);
  ASSERT_EQ(Call()->mAudioReceiveConfig->sync_group, "test");
}

TEST_F(AudioConduitTest, TestConfigureSendMediaCodecOpusMaxBr) {
  using Config = AudioEncoderOpusConfig;
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    AudioCodecConfig codecConfig(109, "opus", 48000, 2, /*FECEnabled=*/true);
    codecConfig.mEncodingConstraints.maxBitrateBps = Some(5000);
    aControl.mAudioSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  EXPECT_EQ(Call()->mAudioSendConfig->send_codec_spec->target_bitrate_bps,
            std::clamp(5000, Config::kMinBitrateBps, Config::kMaxBitrateBps));

  mControl.Update([&](auto& aControl) {
    auto c = aControl.mAudioSendCodec.Ref();
    c->mEncodingConstraints.maxBitrateBps = Some(256000);
    aControl.mAudioSendCodec = c;
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  EXPECT_EQ(Call()->mAudioSendConfig->send_codec_spec->target_bitrate_bps,
            std::clamp(256000, Config::kMinBitrateBps, Config::kMaxBitrateBps));
}

TEST_F(AudioConduitTest, TestConfigureSendMediaCodecG722MaxBr) {
  constexpr int kFixedG722BitratePerChannelBps = 64000;
  mControl.Update([&](auto& aControl) {
    aControl.mTransmitting = true;
    AudioCodecConfig codecConfig(9, "G722", 8000, 1, /*FECEnabled=*/false);
    codecConfig.mEncodingConstraints.maxBitrateBps = Some(5000);
    aControl.mAudioSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  EXPECT_EQ(Call()->mAudioSendConfig->send_codec_spec->target_bitrate_bps,
            kFixedG722BitratePerChannelBps);

  mControl.Update([&](auto& aControl) {
    AudioCodecConfig codecConfig(9, "G722", 8000, 2, /*FECEnabled=*/false);
    codecConfig.mEncodingConstraints.maxBitrateBps = Some(256000);
    aControl.mAudioSendCodec = Some(codecConfig);
  });
  ASSERT_TRUE(Call()->mAudioSendConfig);
  EXPECT_EQ(Call()->mAudioSendConfig->send_codec_spec->target_bitrate_bps,
            2 * kFixedG722BitratePerChannelBps);
}

}  // End namespace test.
