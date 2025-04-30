/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "AnnexB.h"
#include "BufferReader.h"
#include "H264.h"
#include "ImageContainer.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/media/MediaUtils.h"  // For media::Await
#include "PEMFactory.h"
#include "TimeUnits.h"
#include "VideoUtils.h"
#include "VPXDecoder.h"
#include <algorithm>

#ifdef MOZ_WIDGET_ANDROID
// Create/init a H.264 encoder and check if it's SW.
#  define SKIP_IF_ANDROID_SW()                                           \
    do {                                                                 \
      RefPtr<MediaDataEncoder> e = CreateH264Encoder(                    \
          Usage::Record,                                                 \
          EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),  \
          kImageSize, ScalabilityMode::None, Some(kH264SpecificAnnexB)); \
      if (EnsureInit(e)) {                                               \
        nsCString dummy;                                                 \
        bool isSW = !e->IsHardwareAccelerated(dummy);                    \
        WaitForShutdown(e);                                              \
        if (isSW) {                                                      \
          return;                                                        \
        }                                                                \
      }                                                                  \
    } while (0)
#else
#  define SKIP_IF_ANDROID_SW() \
    do {                       \
    } while (0)
#endif

#define RUN_IF_SUPPORTED(codecType, test)   \
  do {                                      \
    RefPtr<PEMFactory> f(new PEMFactory()); \
    if (f->SupportsCodec(codecType)) {      \
      test();                               \
    }                                       \
  } while (0)

#define BLOCK_SIZE 64
#define NUM_FRAMES 150UL
#define FRAME_RATE 30
#define FRAME_DURATION (1000000 / FRAME_RATE)
#define BIT_RATE (1000 * 1000)  // 1Mbps
#define BIT_RATE_MODE BitrateMode::Variable
#define KEYFRAME_INTERVAL FRAME_RATE  // 1 keyframe per second

using namespace mozilla;

static gfx::IntSize kImageSize(640, 480);
static gfx::IntSize kImageSize4K(3840, 2160);
// Set codec to avc1.42001E - Base profile, constraint 0, level 30.
MOZ_RUNINIT const H264Specific kH264SpecificAnnexB(H264_PROFILE_BASE,
                                                   H264_LEVEL::H264_LEVEL_3,
                                                   H264BitStreamFormat::ANNEXB);
MOZ_RUNINIT const H264Specific kH264SpecificAVCC(H264_PROFILE_BASE,
                                                 H264_LEVEL::H264_LEVEL_3,
                                                 H264BitStreamFormat::AVC);

class MediaDataEncoderTest : public testing::Test {
 protected:
  void SetUp() override {
    Preferences::SetBool("media.ffmpeg.encoder.enabled", true);
    Preferences::SetInt("logging.FFmpegVideo", 5);
    mData.Init(kImageSize);
    mData4K.Init(kImageSize4K);
  }

  void TearDown() override {
    mData.Deinit();
    mData4K.Deinit();
  }

 public:
  struct FrameSource final {
    layers::PlanarYCbCrData mYUV;
    UniquePtr<uint8_t[]> mBuffer;
    RefPtr<layers::BufferRecycleBin> mRecycleBin;
    int16_t mColorStep = 4;

    void Init(const gfx::IntSize& aSize) {
      mYUV.mPictureRect = gfx::IntRect(0, 0, aSize.width, aSize.height);
      mYUV.mYStride = aSize.width;
      mYUV.mCbCrStride = (aSize.width + 1) / 2;
      mYUV.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
      auto ySize = mYUV.YDataSize();
      auto cbcrSize = mYUV.CbCrDataSize();
      size_t bufferSize =
          mYUV.mYStride * ySize.height + 2 * mYUV.mCbCrStride * cbcrSize.height;
      mBuffer = MakeUnique<uint8_t[]>(bufferSize);
      std::fill_n(mBuffer.get(), bufferSize, 0x7F);
      mYUV.mYChannel = mBuffer.get();
      mYUV.mCbChannel = mYUV.mYChannel + mYUV.mYStride * ySize.height;
      mYUV.mCrChannel = mYUV.mCbChannel + mYUV.mCbCrStride * cbcrSize.height;
      mYUV.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
      mRecycleBin = new layers::BufferRecycleBin();
    }

    void Deinit() {
      mBuffer.reset();
      mRecycleBin = nullptr;
    }

    already_AddRefed<MediaData> GetFrame(const size_t aIndex) {
      Draw(aIndex);
      RefPtr<layers::PlanarYCbCrImage> img =
          new layers::RecyclingPlanarYCbCrImage(mRecycleBin);
      img->CopyData(mYUV);
      RefPtr<MediaData> frame = VideoData::CreateFromImage(
          kImageSize, 0,
          // The precise time unit should be media::TimeUnit(1, FRAME_RATE)
          // instead of media::TimeUnit(FRAME_DURATION, USECS_PER_S)
          // (FRAME_DURATION microseconds), but this setting forces us to take
          // care some potential rounding issue, e.g., when converting to a time
          // unit based in FRAME_RATE by TimeUnit::ToTicksAtRate(FRAME_RATE),
          // the time unit would be calculated from 999990 / 1000000, which
          // could be zero.
          media::TimeUnit::FromMicroseconds(AssertedCast<int64_t>(aIndex) *
                                            FRAME_DURATION),
          media::TimeUnit::FromMicroseconds(FRAME_DURATION), img,
          (aIndex & 0xF) == 0,
          media::TimeUnit::FromMicroseconds(AssertedCast<int64_t>(aIndex) *
                                            FRAME_DURATION));
      return frame.forget();
    }

    void DrawChessboard(uint8_t* aAddr, const size_t aWidth,
                        const size_t aHeight, const size_t aOffset) {
      uint8_t pixels[2][BLOCK_SIZE];
      size_t x = aOffset % BLOCK_SIZE;
      if ((aOffset / BLOCK_SIZE) & 1) {
        x = BLOCK_SIZE - x;
      }
      for (size_t i = 0; i < x; i++) {
        pixels[0][i] = 0x00;
        pixels[1][i] = 0xFF;
      }
      for (size_t i = x; i < BLOCK_SIZE; i++) {
        pixels[0][i] = 0xFF;
        pixels[1][i] = 0x00;
      }

      uint8_t* p = aAddr;
      for (size_t row = 0; row < aHeight; row++) {
        for (size_t col = 0; col < aWidth; col += BLOCK_SIZE) {
          memcpy(p, pixels[((row / BLOCK_SIZE) + (col / BLOCK_SIZE)) % 2],
                 BLOCK_SIZE);
          p += BLOCK_SIZE;
        }
      }
    }

    void Draw(const size_t aIndex) {
      auto ySize = mYUV.YDataSize();
      DrawChessboard(mYUV.mYChannel, ySize.width, ySize.height, aIndex << 1);
      int16_t color = AssertedCast<int16_t>(mYUV.mCbChannel[0] + mColorStep);
      if (color > 255 || color < 0) {
        mColorStep = AssertedCast<int16_t>(-mColorStep);
        color = AssertedCast<int16_t>(mYUV.mCbChannel[0] + mColorStep);
      }

      size_t size = (mYUV.mCrChannel - mYUV.mCbChannel);

      std::fill_n(mYUV.mCbChannel, size, static_cast<uint8_t>(color));
      std::fill_n(mYUV.mCrChannel, size, 0xFF - static_cast<uint8_t>(color));
    }
  };

 public:
  FrameSource mData;
  FrameSource mData4K;
};

template <typename T>
already_AddRefed<MediaDataEncoder> CreateVideoEncoder(
    CodecType aCodec, Usage aUsage, EncoderConfig::SampleFormat aFormat,
    gfx::IntSize aSize, ScalabilityMode aScalabilityMode,
    const Maybe<T>& aSpecific) {
  RefPtr<PEMFactory> f(new PEMFactory());

  if (!f->SupportsCodec(aCodec)) {
    return nullptr;
  }

  const RefPtr<TaskQueue> taskQueue(
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "TestMediaDataEncoder"));

  RefPtr<MediaDataEncoder> e;
  const HardwarePreference pref = HardwarePreference::None;
  e = f->CreateEncoder(
      EncoderConfig(aCodec, aSize, aUsage, aFormat, FRAME_RATE /* FPS */,
                    KEYFRAME_INTERVAL /* keyframe interval */,
                    BIT_RATE /* bitrate */, 0, 0, BIT_RATE_MODE, pref,
                    aScalabilityMode, aSpecific),
      taskQueue);

  return e.forget();
}

static already_AddRefed<MediaDataEncoder> CreateH264Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const Maybe<H264Specific>& aSpecific = Some(kH264SpecificAnnexB)) {
  return CreateVideoEncoder(CodecType::H264, aUsage, aFormat, aSize,
                            aScalabilityMode, aSpecific);
}

void WaitForShutdown(const RefPtr<MediaDataEncoder>& aEncoder) {
  MOZ_RELEASE_ASSERT(aEncoder);

  Maybe<bool> result;
  // media::Await() supports exclusive promises only, but ShutdownPromise is
  // not.
  aEncoder->Shutdown()->Then(
      AbstractThread::MainThread(), __func__,
      [&result](bool rv) {
        EXPECT_TRUE(rv);
        result = Some(true);
      },
      []() { FAIL() << "Shutdown should never be rejected"; });
  SpinEventLoopUntil("TestMediaDataEncoder.cpp:WaitForShutdown"_ns,
                     [&result]() { return result; });
}

TEST_F(MediaDataEncoderTest, H264Create) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder();
    EXPECT_TRUE(e);
    WaitForShutdown(e);
  });
}

static bool EnsureInit(const RefPtr<MediaDataEncoder>& aEncoder) {
  if (!aEncoder) {
    return false;
  }

  bool succeeded;
  media::Await(
      GetMediaThreadPool(MediaThreadType::SUPERVISOR), aEncoder->Init(),
      [&succeeded](bool) { succeeded = true; },
      [&succeeded](const MediaResult& r) { succeeded = false; });
  return succeeded;
}

TEST_F(MediaDataEncoderTest, H264Inits) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    // w/o codec specific: should fail for h264.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Nothing());
    EXPECT_FALSE(e);

    // w/ codec specific
    e = CreateH264Encoder();
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);
  });
}

static MediaDataEncoder::EncodedData Encode(
    const RefPtr<MediaDataEncoder>& aEncoder, const size_t aNumFrames,
    MediaDataEncoderTest::FrameSource& aSource) {
  MediaDataEncoder::EncodedData output;
  bool succeeded;
  for (size_t i = 0; i < aNumFrames; i++) {
    RefPtr<MediaData> frame = aSource.GetFrame(i);
    media::Await(
        GetMediaThreadPool(MediaThreadType::SUPERVISOR),
        aEncoder->Encode(frame),
        [&output, &succeeded](MediaDataEncoder::EncodedData encoded) {
          output.AppendElements(std::move(encoded));
          succeeded = true;
        },
        [&succeeded](const MediaResult& r) { succeeded = false; });
    EXPECT_TRUE(succeeded);
    if (!succeeded) {
      return output;
    }
  }

  size_t pending = 0;
  do {
    media::Await(
        GetMediaThreadPool(MediaThreadType::SUPERVISOR), aEncoder->Drain(),
        [&pending, &output, &succeeded](MediaDataEncoder::EncodedData encoded) {
          pending = encoded.Length();
          output.AppendElements(std::move(encoded));
          succeeded = true;
        },
        [&succeeded](const MediaResult& r) { succeeded = false; });
    EXPECT_TRUE(succeeded);
    if (!succeeded) {
      return output;
    }
  } while (pending > 0);

  return output;
}

Result<uint8_t, nsresult> GetNALUSize(const mozilla::MediaRawData* aSample) {
  return AVCCConfig::Parse(aSample).map(
      [](AVCCConfig config) { return config.NALUSize(); });
}

Result<Ok, nsresult> IsValidAVCC(const mozilla::MediaRawData* aSample,
                                 uint8_t aNALUSize) {
  BufferReader reader(aSample->Data(), aSample->Size());
  while (reader.Remaining() >= aNALUSize) {
    uint32_t nalLen;
    switch (aNALUSize) {
      case 1:
        MOZ_TRY_VAR(nalLen, reader.ReadU8());
        break;
      case 2:
        MOZ_TRY_VAR(nalLen, reader.ReadU16());
        break;
      case 3:
        MOZ_TRY_VAR(nalLen, reader.ReadU24());
        break;
      case 4:
        MOZ_TRY_VAR(nalLen, reader.ReadU32());
        break;
      default:
        return Err(NS_ERROR_INVALID_ARG);
    }
    const uint8_t* p = reader.Read(nalLen);
    if (!p) {
      return Err(NS_ERROR_ILLEGAL_VALUE);
    }
  }
  return Ok();
}

TEST_F(MediaDataEncoderTest, H264EncodesAnnexBRecord) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in AnnexB format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAnnexB(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in AnnexB format.
    e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      EXPECT_TRUE(AnnexB::IsAnnexB(frame));
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264EncodesAnnexBRealtime) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in AnnexB format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAnnexB(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in AnnexB format.
    e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      EXPECT_TRUE(AnnexB::IsAnnexB(frame));
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264EncodesAVCCRecord) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in avcC format.
    e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    uint8_t naluSize = GetNALUSize(output[0]).unwrapOr(0);
    EXPECT_GT(naluSize, 0);
    EXPECT_LE(naluSize, 4);
    for (auto frame : output) {
      if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
        naluSize = GetNALUSize(frame).unwrapOr(0);
        EXPECT_GT(naluSize, 0);
        EXPECT_LE(naluSize, 4);
      }
      EXPECT_TRUE(IsValidAVCC(frame, naluSize).isOk());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264EncodesAVCCRealtime) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in avcC format.
    e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    uint8_t naluSize = GetNALUSize(output[0]).unwrapOr(0);
    EXPECT_GT(naluSize, 0);
    EXPECT_LE(naluSize, 4);
    for (auto frame : output) {
      if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
        naluSize = GetNALUSize(frame).unwrapOr(0);
        EXPECT_GT(naluSize, 0);
        EXPECT_LE(naluSize, 4);
      }
      EXPECT_TRUE(IsValidAVCC(frame, naluSize).isOk());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAnnexBRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in AnnexB format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData4K);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAnnexB(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in AnnexB format.
    e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData4K);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      EXPECT_TRUE(AnnexB::IsAnnexB(frame));
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAnnexBRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in AnnexB format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData4K);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAnnexB(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in AnnexB format.
    e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData4K);
    EXPECT_LE(output.Length(),
              NUM_FRAMES);  // Frames can be dropped in realtime usage.
    for (auto frame : output) {
      EXPECT_TRUE(AnnexB::IsAnnexB(frame));
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAVCCRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData4K);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in avcC format.
    e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData4K);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    uint8_t naluSize = GetNALUSize(output[0]).unwrapOr(0);
    EXPECT_GT(naluSize, 0);
    EXPECT_LE(naluSize, 4);
    for (auto frame : output) {
      if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
        naluSize = GetNALUSize(frame).unwrapOr(0);
        EXPECT_GT(naluSize, 0);
        EXPECT_LE(naluSize, 4);
      }
      EXPECT_TRUE(IsValidAVCC(frame, naluSize).isOk());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAVCCRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encode one frame and output in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData4K);
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    WaitForShutdown(e);

    // Encode multiple frames and output in avcC format.
    e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize4K, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData4K);
    EXPECT_LE(output.Length(),
              NUM_FRAMES);  // Frames can be dropped in realtime usage.
    EXPECT_TRUE(AnnexB::IsAVCC(output[0]));
    uint8_t naluSize = GetNALUSize(output[0]).unwrapOr(0);
    EXPECT_GT(naluSize, 0);
    EXPECT_LE(naluSize, 4);
    for (auto frame : output) {
      if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
        naluSize = GetNALUSize(frame).unwrapOr(0);
        EXPECT_GT(naluSize, 0);
        EXPECT_LE(naluSize, 4);
      }
      EXPECT_TRUE(IsValidAVCC(frame, naluSize).isOk());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Duration) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder();
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, InvalidSize) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e0x0 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {0, 0},
        ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EXPECT_EQ(e0x0, nullptr);

    RefPtr<MediaDataEncoder> e0x1 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {0, 1},
        ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EXPECT_EQ(e0x1, nullptr);

    RefPtr<MediaDataEncoder> e1x0 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {1, 0},
        ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EXPECT_EQ(e1x0, nullptr);
  });
}

// For Android HW encoder only.
#ifdef MOZ_WIDGET_ANDROID
TEST_F(MediaDataEncoderTest, AndroidNotSupportedSize) {
  SKIP_IF_ANDROID_SW();
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {1, 1},
        ScalabilityMode::None, Some(kH264SpecificAnnexB));
    EXPECT_NE(e, nullptr);
    EXPECT_FALSE(EnsureInit(e));
  });
}
#endif

#if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, H264AVCC) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encod frames in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Some(kH264SpecificAVCC));
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      EXPECT_FALSE(AnnexB::IsAnnexB(frame));
      if (frame->mKeyframe) {
        // The extradata may be included at the beginning, whenever it changes,
        // or with every keyframe to support robust seeking or decoder resets.
        if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
          EXPECT_TRUE(AnnexB::IsAVCC(frame));
          AVCCConfig config = AVCCConfig::Parse(frame).unwrap();
          EXPECT_EQ(config.mAVCProfileIndication,
                    static_cast<decltype(config.mAVCProfileIndication)>(
                        kH264SpecificAVCC.mProfile));
          EXPECT_EQ(config.mAVCLevelIndication,
                    static_cast<decltype(config.mAVCLevelIndication)>(
                        kH264SpecificAVCC.mLevel));
        }
      }
    }
    WaitForShutdown(e);
  });
}
#endif

static already_AddRefed<MediaDataEncoder> CreateVP8Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const Maybe<VP8Specific>& aSpecific = Some(VP8Specific())) {
  return CreateVideoEncoder(CodecType::VP8, aUsage, aFormat, aSize,
                            aScalabilityMode, aSpecific);
}

static already_AddRefed<MediaDataEncoder> CreateVP9Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const Maybe<VP9Specific>& aSpecific = Some(VP9Specific())) {
  return CreateVideoEncoder(CodecType::VP9, aUsage, aFormat, aSize,
                            aScalabilityMode, aSpecific);
}

TEST_F(MediaDataEncoderTest, VP8Create) {
  RUN_IF_SUPPORTED(CodecType::VP8, []() {
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder();
    EXPECT_TRUE(e);
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP8Inits) {
  RUN_IF_SUPPORTED(CodecType::VP8, []() {
    // w/o codec specific.
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Nothing());
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);

    // w/ codec specific
    e = CreateVP8Encoder();
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP8Encodes) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    // Encode one VPX frame.
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder();
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    VPXDecoder::VPXStreamInfo info;
    EXPECT_TRUE(
        VPXDecoder::GetStreamInfo(*output[0], info, VPXDecoder::Codec::VP8));
    EXPECT_EQ(info.mKeyFrame, output[0]->mKeyframe);
    if (info.mKeyFrame) {
      EXPECT_EQ(info.mImage, kImageSize);
    }
    WaitForShutdown(e);

    // Encode multiple VPX frames.
    e = CreateVP8Encoder();
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP8));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP8Duration) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder();
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

#if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, VP8EncodeAfterDrain) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder();
    EnsureInit(e);

    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP8));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }
    output.Clear();

    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP8));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP8EncodeWithScalabilityModeL1T2) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    VP8Specific specific(VPXComplexity::Normal, /* mComplexity */
                         true,                  /* mResilience */
                         2,                     /* mNumTemporalLayers */
                         true,                  /* mDenoising */
                         false,                 /* mAutoResize */
                         false                  /* mFrameDropping */
    );
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::L1T2, Some(specific));
    EnsureInit(e);

    const nsTArray<uint8_t> pattern({0, 1});
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    int temporal_idx = 0;
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (size_t i = 0; i < output.Length(); ++i) {
      const RefPtr<MediaRawData> frame = output[i];
      if (frame->mKeyframe) {
        temporal_idx = 0;
      }
      EXPECT_TRUE(frame->mTemporalLayerId);
      size_t idx = temporal_idx++ % pattern.Length();
      EXPECT_EQ(frame->mTemporalLayerId.value(), pattern[idx]);
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP8EncodeWithScalabilityModeL1T3) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    VP8Specific specific(VPXComplexity::Normal, /* mComplexity */
                         true,                  /* mResilience */
                         3,                     /* mNumTemporalLayers */
                         true,                  /* mDenoising */
                         false,                 /* mAutoResize */
                         false                  /* mFrameDropping */
    );
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::L1T3, Some(specific));
    EnsureInit(e);

    const nsTArray<uint8_t> pattern({0, 2, 1, 2});
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    int temporal_idx = 0;
    for (size_t i = 0; i < output.Length(); ++i) {
      const RefPtr<MediaRawData> frame = output[i];
      if (frame->mKeyframe) {
        temporal_idx = 0;
      }
      EXPECT_TRUE(frame->mTemporalLayerId);
      size_t idx = temporal_idx++ % pattern.Length();
      EXPECT_EQ(frame->mTemporalLayerId.value(), pattern[idx]);
    }
    WaitForShutdown(e);
  });
}
#endif

TEST_F(MediaDataEncoderTest, VP9Create) {
  RUN_IF_SUPPORTED(CodecType::VP9, []() {
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder();
    EXPECT_TRUE(e);
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP9Inits) {
  RUN_IF_SUPPORTED(CodecType::VP9, []() {
    // w/o codec specific.
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, Nothing());
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);

    // w/ codec specific
    e = CreateVP9Encoder();
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP9Encodes) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder();
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, 1UL, mData);
    EXPECT_EQ(output.Length(), 1UL);
    VPXDecoder::VPXStreamInfo info;
    EXPECT_TRUE(
        VPXDecoder::GetStreamInfo(*output[0], info, VPXDecoder::Codec::VP9));
    EXPECT_EQ(info.mKeyFrame, output[0]->mKeyframe);
    if (info.mKeyFrame) {
      EXPECT_EQ(info.mImage, kImageSize);
    }
    WaitForShutdown(e);

    e = CreateVP9Encoder();
    EnsureInit(e);
    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP9));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP9Duration) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder();
    EnsureInit(e);
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

#if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, VP9EncodeAfterDrain) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder();
    EnsureInit(e);

    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP9));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }
    output.Clear();

    output = Encode(e, NUM_FRAMES, mData);
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      VPXDecoder::VPXStreamInfo info;
      EXPECT_TRUE(
          VPXDecoder::GetStreamInfo(*frame, info, VPXDecoder::Codec::VP9));
      EXPECT_EQ(info.mKeyFrame, frame->mKeyframe);
      if (info.mKeyFrame) {
        EXPECT_EQ(info.mImage, kImageSize);
      }
    }

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP9EncodeWithScalabilityModeL1T2) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    VP9Specific specific(VPXComplexity::Normal, /* mComplexity */
                         true,                  /* mResilience */
                         2,                     /* mNumTemporalLayers */
                         true,                  /* mDenoising */
                         false,                 /* mAutoResize */
                         false,                 /* mFrameDropping */
                         true,                  /* mAdaptiveQp */
                         1,                     /* mNumSpatialLayers */
                         false                  /* mFlexible */
    );

    RefPtr<MediaDataEncoder> e = CreateVP9Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::L1T2, Some(specific));
    EnsureInit(e);

    const nsTArray<uint8_t> pattern({0, 1});
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    int temporal_idx = 0;
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (size_t i = 0; i < output.Length(); ++i) {
      const RefPtr<MediaRawData> frame = output[i];
      if (frame->mKeyframe) {
        temporal_idx = 0;
      }
      EXPECT_TRUE(frame->mTemporalLayerId);
      size_t idx = temporal_idx++ % pattern.Length();
      EXPECT_EQ(frame->mTemporalLayerId.value(), pattern[idx]);
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, VP9EncodeWithScalabilityModeL1T3) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    VP9Specific specific(VPXComplexity::Normal, /* mComplexity */
                         true,                  /* mResilience */
                         3,                     /* mNumTemporalLayers */
                         true,                  /* mDenoising */
                         false,                 /* mAutoResize */
                         false,                 /* mFrameDropping */
                         true,                  /* mAdaptiveQp */
                         1,                     /* mNumSpatialLayers */
                         false                  /* mFlexible */
    );

    RefPtr<MediaDataEncoder> e = CreateVP9Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::L1T3, Some(specific));
    EnsureInit(e);

    const nsTArray<uint8_t> pattern({0, 2, 1, 2});
    MediaDataEncoder::EncodedData output = Encode(e, NUM_FRAMES, mData);
    int temporal_idx = 0;
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (size_t i = 0; i < output.Length(); ++i) {
      const RefPtr<MediaRawData> frame = output[i];
      if (frame->mKeyframe) {
        temporal_idx = 0;
      }
      EXPECT_TRUE(frame->mTemporalLayerId);
      size_t idx = temporal_idx++ % pattern.Length();
      EXPECT_EQ(frame->mTemporalLayerId.value(), pattern[idx]);
    }
    WaitForShutdown(e);
  });
}
#endif
