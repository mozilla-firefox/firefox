/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <type_traits>

#include "H264.h"
#include "ImageContainer.h"
#include "MP4Demuxer.h"
#include "MockMediaResource.h"
#include "PDMFactory.h"
#include "VideoUtils.h"
#include "gfxPlatform.h"
#include "gtest/gtest.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gtest/ScopedPrefSetter.h"
#include "mozilla/gtest/WaitFor.h"

using namespace mozilla;

using MDD = MediaDataDecoder;
using ParamType = std::underlying_type<MDD::PropertyName>::type;

class PropertyTest : public ::testing::TestWithParam<ParamType> {
 public:
  static void SetUpTestSuite() {
    // This ensures static media data is initialized.
    gfxPlatform::GetPlatform();

    sFactory = MakeRefPtr<PDMFactory>();
    sAVCInfo = std::make_unique<VideoInfo>(sDummyVideoSize);
    sAVCInfo->mMimeType = "video/avc"_ns;
    sAVCInfo->mExtraData = H264::CreateExtraData(
        H264_PROFILE::H264_PROFILE_BASE, 0 /* constraint */,
        H264_LEVEL::H264_LEVEL_1, sDummyVideoSize);
    sVP9Info = std::make_unique<VideoInfo>(sDummyVideoSize);
    sVP9Info->mMimeType = "video/vp9"_ns;
    sVP9Info->SetAlpha(true);
  }

  static void TearDownTestSuite() {
    sFactory = nullptr;
    sTaskQueue = nullptr;
    sAVCInfo.reset();
    sVP9Info.reset();
  }

  static constexpr gfx::IntSize sDummyVideoSize{640, 480};
  static RefPtr<PDMFactory> sFactory;
  static RefPtr<TaskQueue> sTaskQueue;
  static UniquePtr<VideoInfo> sAVCInfo;
  static UniquePtr<VideoInfo> sVP9Info;
};

constinit RefPtr<PDMFactory> PropertyTest::sFactory;
constinit RefPtr<TaskQueue> PropertyTest::sTaskQueue;
constinit UniquePtr<VideoInfo> PropertyTest::sAVCInfo;
constinit UniquePtr<VideoInfo> PropertyTest::sVP9Info;

void CheckEquals(VideoInfo& aVideoInfo, MDD::PropertyName aPropertyName,
                 const Maybe<MDD::PropertyValue>&& aExpectedValue,
                 const char* aCallSite) {
  using V = Maybe<MDD::PropertyValue>;
  auto d = WaitFor(
      PropertyTest::sFactory->CreateDecoder(CreateDecoderParams{aVideoInfo}));
  EXPECT_TRUE(d.isOk());
  RefPtr<MDD> dec = d.unwrap();
  auto t = WaitFor(dec->Init());
  EXPECT_TRUE(t.isOk());
  EXPECT_EQ(t.unwrap(), TrackInfo::TrackType::kVideoTrack);
  const V v = dec->GetDecodeProperty(aPropertyName);
  // Although Maybe supports operator<<(), PropertyValue/Variant doesn't and
  // needs special care.
  auto maybeStr = [](const V& v) -> std::string {
    if (v.isNothing()) {
      return "undefined";
    }
    // Only uint32_t for now.
    return std::to_string(v.ref().match([](uint32_t x) { return x; }));
  };

  EXPECT_TRUE(v == aExpectedValue)
      << "[" << aCallSite << "] "
      << "Decode property: " << MDD::EnumValueToString(aPropertyName)
      << std::endl
      << "  Actual: " << maybeStr(v)
      << "  Expected: " << maybeStr(aExpectedValue);
}

#define CHECK_NOT_DEFINED(info, prop)             \
  do {                                            \
    CheckEquals(info, prop, Nothing(), __func__); \
  } while (0)

#ifdef MOZ_WIDGET_ANDROID
void CheckAndroid(VideoInfo& aVideoInfo, MDD::PropertyName aProperty) {
  switch (aProperty) {
    case MDD::PropertyName::MaxNumVideoBuffers:
      [[fallthrough]];
    case MDD::PropertyName::MinNumVideoBuffers:
      CheckEquals(aVideoInfo, aProperty, Some(MDD::PropertyValue(3U)),
                  __func__);
      break;
    case MDD::PropertyName::MaxNumCurrentImages:
      CheckEquals(aVideoInfo, aProperty, Some(MDD::PropertyValue(1U)),
                  __func__);
      break;
    default:
      CHECK_NOT_DEFINED(aVideoInfo, aProperty);
  }
}
#endif

static RefPtr<VideoData> DecodeFirstAV1FrameViaFFVPX(
    const char* aFileName, gfx::ColorSpace2 aConfigPrimaries,
    gfx::TransferFunction aConfigTransfer) {
  RefPtr<MockMediaResource> resource = new MockMediaResource(aFileName);
  if (NS_FAILED(resource->Open())) {
    ADD_FAILURE() << "Failed to open " << aFileName;
    return nullptr;
  }

  RefPtr<MP4Demuxer> demuxer = new MP4Demuxer(resource);
  RefPtr<TaskQueue> taskQueue = TaskQueue::Create(
      GetMediaThreadPool(MediaThreadType::SUPERVISOR), "TestMediaDataDecoder");
  auto shutdownTaskQueue = MakeScopeExit([&] {
    taskQueue->BeginShutdown();
    taskQueue->AwaitShutdownAndIdle();
  });

  WaitForResolve(
      InvokeAsync(taskQueue, __func__, [demuxer] { return demuxer->Init(); }));
  RefPtr<MediaTrackDemuxer> track =
      demuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
  UniquePtr<TrackInfo> trackInfo = track ? track->GetInfo() : nullptr;
  if (!trackInfo || !trackInfo->GetAsVideoInfo()) {
    ADD_FAILURE() << "Missing video track";
    return nullptr;
  }
  VideoInfo* videoInfo = trackInfo->GetAsVideoInfo();
  videoInfo->mColorPrimaries = Some(aConfigPrimaries);
  videoInfo->mTransferFunction = Some(aConfigTransfer);

  auto samples = WaitFor(InvokeAsync(taskQueue, __func__,
                                     [track] { return track->GetSamples(1); }));
  if (samples.isErr() || samples.inspect()->GetSamples().IsEmpty()) {
    ADD_FAILURE() << "Failed to demux an AV1 sample";
    return nullptr;
  }

  ScopedPrefSetter decoderPrefs({
      {"media.gpu-process-decoder", false},
      {"media.rdd-process.enabled", false},
      {"media.utility-process.enabled", false},
      {"media.prefer-non-ffvpx", false},
  });
  RefPtr<PDMFactory> factory = MakeRefPtr<PDMFactory>();

  RefPtr<layers::ImageContainer> imageContainer =
      MakeRefPtr<layers::ImageContainer>(
          layers::ImageUsageType::VideoFrameContainer,
          layers::ImageContainer::ASYNCHRONOUS);
  auto created = WaitFor(factory->CreateDecoder(CreateDecoderParams{
      *videoInfo, imageContainer.get(),
      CreateDecoderParams::WrapperSet({/* No wrapper */})}));
  if (created.isErr()) {
    ADD_FAILURE() << "Failed to create ffvpx AV1 decoder";
    return nullptr;
  }
  RefPtr<MDD> decoder = created.unwrap();
  if (!decoder->GetDescriptionName().EqualsLiteral("ffvpx video decoder")) {
    ADD_FAILURE() << "Expected the bundled FFmpeg decoder";
    return nullptr;
  }
  auto shutdownDecoder =
      MakeScopeExit([&] { WaitForResolve(decoder->Shutdown()); });

  auto init = WaitFor(decoder->Init());
  if (init.isErr()) {
    ADD_FAILURE() << "Failed to initialize ffvpx AV1 decoder";
    return nullptr;
  }

  auto decoded =
      WaitFor(decoder->Decode(samples.inspect()->GetSamples()[0].get()));
  if (decoded.isErr() || decoded.inspect().IsEmpty()) {
    ADD_FAILURE() << "Failed to decode an AV1 frame";
    return nullptr;
  }
  return do_AddRef(decoded.inspect()[0]->As<VideoData>());
}

static const layers::PlanarYCbCrData* GetPlanarData(VideoData* aVideo) {
  if (!aVideo || !aVideo->mImage || !aVideo->mImage->AsPlanarYCbCrImage()) {
    return nullptr;
  }
  return aVideo->mImage->AsPlanarYCbCrImage()->GetData();
}

TEST(MediaDataDecoder, FFmpegFrameColorMetadataOverridesConfig)
{
  constexpr gfx::ColorSpace2 kFixtureColorPrimaries = gfx::ColorSpace2::BT2020;
  constexpr gfx::TransferFunction kFixtureTransferFunction =
      gfx::TransferFunction::PQ;
  RefPtr<VideoData> video = DecodeFirstAV1FrameViaFFVPX(
      "av1-color-metadata.mp4", gfx::ColorSpace2::BT709,
      gfx::TransferFunction::BT709);
  const layers::PlanarYCbCrData* data = GetPlanarData(video);
  ASSERT_TRUE(data);
  EXPECT_EQ(data->mColorPrimaries, kFixtureColorPrimaries);
  EXPECT_EQ(data->mTransferFunction, kFixtureTransferFunction);
}

TEST(MediaDataDecoder, FFmpegFrameColorMetadataFallsBackToConfig)
{
  constexpr gfx::ColorSpace2 kConfigColorPrimaries = gfx::ColorSpace2::BT2020;
  constexpr gfx::TransferFunction kConfigTransferFunction =
      gfx::TransferFunction::PQ;
  RefPtr<VideoData> video = DecodeFirstAV1FrameViaFFVPX(
      "av1.mp4", kConfigColorPrimaries, kConfigTransferFunction);
  const layers::PlanarYCbCrData* data = GetPlanarData(video);
  ASSERT_TRUE(data);
  EXPECT_EQ(data->mColorPrimaries, kConfigColorPrimaries);
  EXPECT_EQ(data->mTransferFunction, kConfigTransferFunction);
}

#ifdef MOZ_APPLEMEDIA
void CheckApple(VideoInfo& aVideoInfo, MDD::PropertyName aProperty) {
  switch (aProperty) {
    case MDD::PropertyName::MinNumVideoBuffers:
      CheckEquals(aVideoInfo, aProperty, Some(MDD::PropertyValue(10U)),
                  __func__);
      break;
    default:
      CHECK_NOT_DEFINED(aVideoInfo, aProperty);
  }
}
#endif

INSTANTIATE_TEST_SUITE_P(TestMediaDataDecoder, PropertyTest,
                         ::testing::Range<ParamType>(0,
                                                     MDD::sPropertyNameCount),
                         [](const ::testing::TestParamInfo<ParamType>& info) {
                           return std::string(MDD::EnumValueToString(
                               static_cast<MDD::PropertyName>(info.param)));
                         });

TEST_P(PropertyTest, DefaultValues) {
  auto param = static_cast<MDD::PropertyName>(GetParam());
#ifdef MOZ_WIDGET_ANDROID
  CheckAndroid(*sAVCInfo, param);
#elif defined(MOZ_APPLEMEDIA)
  CheckApple(*sAVCInfo, param);
#else
  CHECK_NOT_DEFINED(*sAVCInfo, param);
#endif
}

// On Android, VP9 video with alpha channel is decoded with libvpx.
#ifdef MOZ_WIDGET_ANDROID
TEST_P(PropertyTest, NotDefinedForVP9WithAlphaOnAndroid) {
  CHECK_NOT_DEFINED(*sVP9Info, static_cast<MDD::PropertyName>(GetParam()));
}
#endif
