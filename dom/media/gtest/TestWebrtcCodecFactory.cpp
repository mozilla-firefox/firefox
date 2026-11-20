/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaMIMETypes.h"
#include "PlatformDecoderModule.h"
#include "WebrtcEnvironmentWrapper.h"
#include "WebrtcGmpVideoCodec.h"
#include "WebrtcVideoCodecFactory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/gtest/WaitFor.h"

using testing::_;
using testing::MockFunction;
using testing::Test;

namespace mozilla {

enum class TestType : uint8_t { DecoderFactory, EncoderFactory };

struct TestWebrtcVideoDecoderFactory : public WebrtcVideoDecoderFactory,
                                       public Test {
  static constexpr TestType kTestType = TestType::DecoderFactory;
  RefPtr<WebrtcEnvironmentWrapper> mEnvironment;
  TestWebrtcVideoDecoderFactory()
      : WebrtcVideoDecoderFactory(GetCurrentSerialEventTarget(),
                                  /*aPCHandle=*/"dummy_handle",
                                  /*aTrackingId=*/{}),
        mEnvironment(WebrtcEnvironmentWrapper::Create(
            dom::RTCStatsTimestampMaker::Create())) {}

  void SetUp() override {
    Preferences::SetBool("media.navigator.mediadatadecoder_h264_enabled",
                         false);

    media::DecodeSupportSet h264GmpSupport =
        WaitFor(WebrtcVideoDecoderFactory::SupportsCodec(
                    *MakeMediaExtendedMIMEType("video/H264"_ns),
                    SupportDecoderParams(VideoInfo())))
            .unwrap();

#ifdef ANDROID
    ASSERT_EQ(h264GmpSupport, media::DecodeSupportSet{});
    GTEST_SKIP() << "fakeopenh264 plugin does not load on Android";
#else
    ASSERT_EQ(h264GmpSupport,
              media::DecodeSupportSet{media::DecodeSupport::SoftwareDecode});
#endif
  }

  void TearDown() override {
    Preferences::ClearUser("media.navigator.mediadatadecoder_h264_enabled");
  }
};

struct TestWebrtcVideoEncoderFactory : public WebrtcVideoEncoderFactory,
                                       public Test {
  static constexpr TestType kTestType = TestType::EncoderFactory;
  RefPtr<WebrtcEnvironmentWrapper> mEnvironment;
  TestWebrtcVideoEncoderFactory()
      : WebrtcVideoEncoderFactory(GetCurrentSerialEventTarget(),
                                  /*aPCHandle=*/"dummy_handle"),
        mEnvironment(WebrtcEnvironmentWrapper::Create(
            dom::RTCStatsTimestampMaker::Create())) {}

  void SetUp() override {
    Preferences::SetInt("media.webrtc.encoder_creation_strategy", 0);
    Preferences::SetBool("media.webrtc.simulcast.h264.enabled", false);

    media::EncodeSupportSet h264GmpSupport =
        WaitFor(
            WebrtcVideoEncoderFactory::SupportsCodec(EncoderConfig(
                CodecType::H264, {640, 480}, Usage::Realtime,
                EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
                30, 240, 100'000, 50'000, 1'000'000, BitrateMode::Constant,
                HardwarePreference::None, ScalabilityMode::None,
                EncoderConfig::CodecSpecific(H264Specific()))))
            .unwrap();

#ifdef ANDROID
    ASSERT_EQ(h264GmpSupport, media::EncodeSupportSet{});
    GTEST_SKIP() << "fakeopenh264 plugin does not load on Android";
#else
    ASSERT_EQ(h264GmpSupport,
              media::EncodeSupportSet{media::EncodeSupport::SoftwareEncode});
#endif
  }

  void TearDown() override {
    Preferences::ClearUser("media.webrtc.encoder_creation_strategy");
    Preferences::ClearUser("media.webrtc.simulcast.h264.enabled");
  }
};

template <TestType kTestType>
void DoTestRacyGmpPluginForwards(auto& aFactory) {
  static_assert(kTestType == TestType::DecoderFactory ||
                kTestType == TestType::EncoderFactory);
  constexpr bool kIsDecoderTest = kTestType == TestType::DecoderFactory;
  using CodecImplType =
      std::conditional_t<kIsDecoderTest, WebrtcVideoDecoderProxy,
                         WebrtcVideoEncoderProxy>;

  nsCOMPtr<nsISerialEventTarget> owningThread = GetCurrentSerialEventTarget();
  enum EventType { Created, Released };
  MockFunction<void(EventType type, uint64_t id)> checkpoint;
  MediaEventListener createdListener = aFactory.CreatedGmpPluginEvent().Connect(
      owningThread, [&](uint64_t aId) { checkpoint.Call(Created, aId); });
  MediaEventListener releasedListener =
      aFactory.ReleasedGmpPluginEvent().Connect(
          owningThread, [&](uint64_t aId) { checkpoint.Call(Released, aId); });

  // It's a tight race, use a decent number. Still quick (~10ms opt non-debug).
  constexpr size_t kIterations = 1000;
  EXPECT_CALL(checkpoint, Call(Created, _)).Times(kIterations);
  EXPECT_CALL(checkpoint, Call(Released, _)).Times(kIterations);

  auto donePromise = TakeN(aFactory.ReleasedGmpPluginEvent(), kIterations);

  for (size_t i = 0; i < kIterations; ++i) {
    nsCOMPtr<nsISerialEventTarget> worker;
    MOZ_ALWAYS_SUCCEEDS(
        NS_CreateBackgroundTaskQueue(__func__, getter_AddRefs(worker)));
    MOZ_ALWAYS_SUCCEEDS(worker->Dispatch(NS_NewRunnableFunction(__func__, [&] {
      auto codec = aFactory.Create(
          aFactory.mEnvironment->Environment(),
          webrtc::SdpVideoFormat(
              webrtc::CodecTypeToPayloadString(webrtc::kVideoCodecH264)));
      EXPECT_TRUE(codec);
      auto* impl = static_cast<CodecImplType*>(codec.get());
      auto init = TakeN(*impl->InitPluginEvent(), 1);
      if constexpr (kIsDecoderTest) {
        EXPECT_TRUE(codec->Configure({}));
      } else {
        webrtc::VideoCodec codec_properties;
        webrtc::VideoEncoder::Capabilities capabilities(
            /*loss_notification=*/false);
        webrtc::VideoEncoder::Settings settings(
            capabilities, /*number_of_cores=*/1, /*max_payload_size=*/0);
        EXPECT_EQ(codec->InitEncode(&codec_properties, settings), 0);
      }
      init->Then(
          GetCurrentSerialEventTarget(), "RacyGmpPluginForwards",
          [codec = std::move(codec)] { EXPECT_EQ(codec->Release(), 0); });
    })));
  }

  (void)WaitFor(donePromise);

  aFactory.DisconnectAll();
  createdListener.Disconnect();
  releasedListener.Disconnect();
}

TEST_F(TestWebrtcVideoDecoderFactory, RacyGmpPluginForwards) {
  DoTestRacyGmpPluginForwards<kTestType>(*this);
}

TEST_F(TestWebrtcVideoEncoderFactory, RacyGmpPluginForwards) {
  DoTestRacyGmpPluginForwards<kTestType>(*this);
}

}  // namespace mozilla
