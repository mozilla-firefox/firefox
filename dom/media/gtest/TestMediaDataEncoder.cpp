/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cstring>

#include "AOMDecoder.h"
#include "AnnexB.h"
#include "BufferReader.h"
#include "H264.h"
#include "ImageContainer.h"
#include "MediaData.h"
#include "PEMFactory.h"
#include "TimeUnits.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "gtest/gtest.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gtest/ScopedPrefSetter.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/media/MediaUtils.h"  // For media::Await

#ifdef XP_MACOSX
#  include "apple/AppleVTEncoder.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
// Create/init a H.264 encoder and check if it's SW.
#  define SKIP_IF_ANDROID_SW()                                                \
    do {                                                                      \
      RefPtr<MediaDataEncoder> e = CreateH264Encoder(                         \
          Usage::Record,                                                      \
          EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),       \
          kImageSize, ScalabilityMode::None, AsVariant(kH264SpecificAnnexB)); \
      if (EnsureInit(e)) {                                                    \
        nsCString dummy;                                                      \
        bool isSW = !e->IsHardwareAccelerated(dummy);                         \
        WaitForShutdown(e);                                                   \
        if (isSW) {                                                           \
          return;                                                             \
        }                                                                     \
      }                                                                       \
    } while (0)
#else
#  define SKIP_IF_ANDROID_SW() \
    do {                       \
    } while (0)
#endif

#define RUN_IF_SUPPORTED(codecType, test)         \
  do {                                            \
    RefPtr<PEMFactory> f(new PEMFactory());       \
    if (!f->SupportsCodec(codecType).isEmpty()) { \
      test();                                     \
    }                                             \
  } while (0)

#define GET_OR_RETURN_ON_ERROR(expr)                      \
  __extension__({                                         \
    auto mozTryVarTempResult = ::mozilla::ToResult(expr); \
    if (MOZ_UNLIKELY(mozTryVarTempResult.isErr())) {      \
      EXPECT_TRUE(false);                                 \
      return;                                             \
    }                                                     \
    mozTryVarTempResult.unwrap();                         \
  })

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
// The resolution Gecko computes as the H264 Annex A Table A-1 macroblock-cap
// for level 5 (mbs=589824, fs=22080) at a 12800x7200-ish source aspect
// ratio; see bug 2064678. Exceeds Android's real per-frame macroblock
// budget (see H264EncodesLevel5CapAnnexBRealtime below), independent of the
// declared level.
static gfx::IntSize kImageSizeLevel5Cap(3160, 1776);
// Set codec to avc1.42001E - Base profile, constraint 0, level 30.
MOZ_RUNINIT const H264Specific kH264SpecificAnnexB(H264_PROFILE_BASE,
                                                   H264_LEVEL::H264_LEVEL_3,
                                                   H264BitStreamFormat::ANNEXB);
MOZ_RUNINIT const H264Specific kH264SpecificAVCC(H264_PROFILE_BASE,
                                                 H264_LEVEL::H264_LEVEL_3,
                                                 H264BitStreamFormat::AVC);
// Same as kH264SpecificAnnexB, but declaring level 5 -- matching
// kImageSizeLevel5Cap's macroblock budget, unlike level 3's (used by
// kH264SpecificAnnexB), which is nowhere near enough for that resolution.
MOZ_RUNINIT const H264Specific kH264SpecificLevel5AnnexB(
    H264_PROFILE_BASE, H264_LEVEL::H264_LEVEL_5, H264BitStreamFormat::ANNEXB);

class MediaDataEncoderTest : public testing::Test {
 protected:
  void SetUp() override {
    mData.Init(kImageSize);
    mData4K.Init(kImageSize4K);
    mDataLevel5Cap.Init(kImageSizeLevel5Cap);
  }

  void TearDown() override {
    mData.Deinit();
    mData4K.Deinit();
    mDataLevel5Cap.Deinit();
  }

 public:
  struct FrameSource final {
    gfx::IntSize mSize = gfx::IntSize(0, 0);
    layers::PlanarYCbCrData mYUV;
    UniquePtr<uint8_t[]> mBuffer;
    RefPtr<layers::BufferRecycleBin> mRecycleBin;
    int16_t mColorStep = 4;

    gfx::IntSize GetSize() const { return mSize; }

    void Init(const gfx::IntSize& aSize) {
      mSize = aSize;
      mYUV.mPictureRect = gfx::IntRect(0, 0, aSize.width, aSize.height);
      mYUV.mYStride = aSize.width;
      mYUV.mCbCrStride = (aSize.width + 1) / 2;
      mYUV.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
      const auto sizes = mYUV.GetPlaneSizes();
      const auto& ySize = sizes->mY;
      const auto& cbcrSize = sizes->mCbCr;
      size_t bufferSize =
          mYUV.mYStride * ySize.height + 2 * mYUV.mCbCrStride * cbcrSize.height;
      mBuffer = std::make_unique<uint8_t[]>(bufferSize);
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
      mSize = gfx::IntSize(0, 0);
    }

    already_AddRefed<MediaData> GetFrame(const size_t aIndex) {
      Draw(aIndex);
      RefPtr<layers::PlanarYCbCrImage> img =
          new layers::RecyclingPlanarYCbCrImage(mRecycleBin);
      img->CopyData(mYUV);
      RefPtr<MediaData> frame = VideoData::CreateFromImage(
          mSize, 0,
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
      DrawChessboard(mYUV.mYChannel, mSize.width, mSize.height, aIndex << 1);
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
  FrameSource mDataLevel5Cap;
};

already_AddRefed<MediaDataEncoder> CreateVideoEncoder(
    CodecType aCodec, Usage aUsage, EncoderConfig::SampleFormat aFormat,
    gfx::IntSize aSize, BitrateMode aBitrateMode,
    HardwarePreference aHardwarePreference, ScalabilityMode aScalabilityMode,
    const EncoderConfig::CodecSpecific& aSpecific) {
  RefPtr<PEMFactory> f(new PEMFactory());

  if (f->SupportsCodec(aCodec).isEmpty()) {
    return nullptr;
  }

  const EncoderConfig config(
      aCodec, aSize, aUsage, aFormat, FRAME_RATE /* FPS */,
      KEYFRAME_INTERVAL /* keyframe interval */, BIT_RATE /* bitrate */, 0, 0,
      aBitrateMode, aHardwarePreference, aScalabilityMode, aSpecific);
  if (f->Supports(config).isEmpty()) {
    return nullptr;
  }

  const RefPtr<TaskQueue> taskQueue(
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "TestMediaDataEncoder"));
  RefPtr<MediaDataEncoder> e = f->CreateEncoder(config, taskQueue);
  return e.forget();
}

static bool EnsureInit(const RefPtr<MediaDataEncoder>& aEncoder) {
  if (!aEncoder) {
    return false;
  }
  auto r = WaitFor(aEncoder->Init());
  return r.isOk();
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

static Result<MediaDataEncoder::EncodedData, MediaResult> Drain(
    const RefPtr<MediaDataEncoder>& aEncoder) {
  MOZ_RELEASE_ASSERT(aEncoder);

  size_t pending = 0;
  MediaDataEncoder::EncodedData output;
  do {
    MediaDataEncoder::EncodedData data = MOZ_TRY(WaitFor(aEncoder->Drain()));
    pending = data.Length();
    output.AppendElements(std::move(data));
  } while (pending > 0);

  return output;
}

struct EncodeResult {
  MediaDataEncoder::EncodedData mEncodedData;
  size_t mInputKeyframes = 0;
};
static Result<EncodeResult, MediaResult> EncodeWithInputStats(
    const RefPtr<MediaDataEncoder>& aEncoder, const size_t aNumFrames,
    MediaDataEncoderTest::FrameSource& aSource) {
  MOZ_RELEASE_ASSERT(aEncoder);

  size_t inputKeyframes = 0;
  MediaDataEncoder::EncodedData output;
  for (size_t i = 0; i < aNumFrames; i++) {
    RefPtr<MediaData> frame = aSource.GetFrame(i);
    if (frame->mKeyframe) {
      inputKeyframes++;
    }
    output.AppendElements(MOZ_TRY(WaitFor(aEncoder->Encode(frame))));
  }
  output.AppendElements(std::move(MOZ_TRY(Drain(aEncoder))));
  return EncodeResult{std::move(output), inputKeyframes};
}

static Result<MediaDataEncoder::EncodedData, MediaResult> Encode(
    const RefPtr<MediaDataEncoder>& aEncoder, const size_t aNumFrames,
    MediaDataEncoderTest::FrameSource& aSource) {
  EncodeResult r = MOZ_TRY(EncodeWithInputStats(aEncoder, aNumFrames, aSource));
  return std::move(r.mEncodedData);
}

static Result<EncodeResult, MediaResult> EncodeBatchWithInputStats(
    const RefPtr<MediaDataEncoder>& aEncoder, const size_t aTotalNumFrames,
    MediaDataEncoderTest::FrameSource& aSource, const size_t aBatchSize) {
  if (aBatchSize == 0 || aTotalNumFrames == 0) {
    return Err(MediaResult(
        NS_ERROR_INVALID_ARG,
        "Batch size and total number of frames must be greater than 0"));
  }

  size_t inputKeyframes = 0;
  MediaDataEncoder::EncodedData output;
  nsTArray<RefPtr<MediaData>> frames;
  for (size_t i = 0; i < aTotalNumFrames; i++) {
    RefPtr<MediaData> frame = aSource.GetFrame(i);
    frames.AppendElement(frame);
    if (frame->mKeyframe) {
      inputKeyframes++;
    }
    if (frames.Length() == aBatchSize || i == aTotalNumFrames - 1) {
      nsTArray<RefPtr<MediaData>> batch = std::move(frames);
      output.AppendElements(
          MOZ_TRY(WaitFor(aEncoder->Encode(std::move(batch)))));
    }
  }
  MOZ_RELEASE_ASSERT(frames.IsEmpty());

  output.AppendElements(std::move(MOZ_TRY(Drain(aEncoder))));
  return EncodeResult{std::move(output), inputKeyframes};
}

template <typename T>
size_t GetKeyFrameCount(const T& aData) {
  size_t count = 0;
  for (auto sample : aData) {
    if (sample->mKeyframe) {
      count++;
    }
  }
  return count;
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
        nalLen = MOZ_TRY(reader.ReadU8());
        break;
      case 2:
        nalLen = MOZ_TRY(reader.ReadU16());
        break;
      case 3:
        nalLen = MOZ_TRY(reader.ReadU24());
        break;
      case 4:
        nalLen = MOZ_TRY(reader.ReadU32());
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

// Validate the encoded output shared by the H264 encode tests. In the lossy
// real-time 4K path the encoder may drop frames, including the frame that
// carried a forced-keyframe request. aToleratesKeyframeDrop selects whether
// that path still requires every requested keyframe (single-frame submission,
// which has enough slack not to drop one) or only a stream that starts with a
// keyframe (batch submission, which can drop a forced-keyframe frame).
static void CheckH264EncodeOutput(const MediaDataEncoder::EncodedData& aOutput,
                                  size_t aExpectedFrames,
                                  size_t aInputKeyframes, Usage aUsage,
                                  bool aIs4KOrLarger, bool aIsAVCC,
                                  bool aToleratesKeyframeDrop) {
  const bool lossyRealtime = aUsage == Usage::Realtime && aIs4KOrLarger;
  if (lossyRealtime) {
    // Realtime encoding may drop frames for large frame sizes.
    EXPECT_LE(aOutput.Length(), aExpectedFrames);
  } else {
    EXPECT_EQ(aOutput.Length(), aExpectedFrames);
  }

  ASSERT_FALSE(aOutput.IsEmpty());

  if (lossyRealtime && aToleratesKeyframeDrop) {
    // A dropped frame can be the one that requested a forced keyframe, so the
    // output may contain fewer keyframes than requested; require only that the
    // stream still starts with a keyframe.
    EXPECT_TRUE(aOutput[0]->mKeyframe);
  } else {
    EXPECT_GE(GetKeyFrameCount(aOutput), aInputKeyframes);
  }

  if (aIsAVCC) {
    uint8_t naluSize = GetNALUSize(aOutput[0]).unwrapOr(0);
    EXPECT_GT(naluSize, 0);
    EXPECT_LE(naluSize, 4);
    for (const auto& frame : aOutput) {
      if (frame->mExtraData && !frame->mExtraData->IsEmpty()) {
        naluSize = GetNALUSize(frame).unwrapOr(0);
        EXPECT_GT(naluSize, 0);
        EXPECT_LE(naluSize, 4);
      }
      EXPECT_TRUE(IsValidAVCC(frame, naluSize).isOk());
    }
  } else {
    for (const auto& frame : aOutput) {
      EXPECT_TRUE(AnnexB::IsAnnexB(*frame));
    }
  }
}

static already_AddRefed<MediaDataEncoder> CreateH264Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const EncoderConfig::CodecSpecific& aSpecific =
        AsVariant(kH264SpecificAnnexB)) {
  return CreateVideoEncoder(CodecType::H264, aUsage, aFormat, aSize,
                            BIT_RATE_MODE, HardwarePreference::None,
                            aScalabilityMode, aSpecific);
}

TEST_F(MediaDataEncoderTest, H264Create) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder();
    EXPECT_TRUE(e);
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264Inits) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    // w/o codec specific: should fail for h264.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, AsVariant(void_t{}));
    EXPECT_FALSE(e);

    // w/ codec specific
    e = CreateH264Encoder();
    EXPECT_TRUE(EnsureInit(e));
    WaitForShutdown(e);
  });
}

static void H264EncodesTest(Usage aUsage,
                            const EncoderConfig::CodecSpecific& aSpecific,
                            MediaDataEncoderTest::FrameSource& aFrameSource,
                            bool aToleratesInitFailureOnAndroid = false) {
  ASSERT_TRUE(aSpecific.is<H264Specific>());
  ASSERT_TRUE(aSpecific.as<H264Specific>().mFormat ==
                  H264BitStreamFormat::ANNEXB ||
              aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC);

  RUN_IF_SUPPORTED(CodecType::H264, [&]() {
    bool isAVCC =
        aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC;

    // Encode one frame and output in AnnexB/AVCC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        aUsage, EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        aFrameSource.GetSize(), ScalabilityMode::None, aSpecific);
    bool initOk = EnsureInit(e);
#ifdef MOZ_WIDGET_ANDROID
    if (!initOk && aToleratesInitFailureOnAndroid) {
      return;
    }
#endif
    EXPECT_TRUE(initOk);
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, 1UL, aFrameSource));
    EXPECT_EQ(output.Length(), 1UL);
    EXPECT_TRUE(isAVCC ? AnnexB::IsAVCC(output[0])
                       : AnnexB::IsAnnexB(*output[0]));
    WaitForShutdown(e);
    output.Clear();

    // Encode multiple frames and output in AnnexB/AVCC format.
    e = CreateH264Encoder(
        aUsage, EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        aFrameSource.GetSize(), ScalabilityMode::None, aSpecific);
    EXPECT_TRUE(EnsureInit(e));
    const bool is4KOrLarger = kImageSize4K <= aFrameSource.GetSize();
    const size_t numFrames = NUM_FRAMES / (is4KOrLarger ? 3 : 1);
    EncodeResult r = GET_OR_RETURN_ON_ERROR(
        EncodeWithInputStats(e, numFrames, aFrameSource));
    output = std::move(r.mEncodedData);
    CheckH264EncodeOutput(output, numFrames, r.mInputKeyframes, aUsage,
                          is4KOrLarger, isAVCC,
                          /* aToleratesKeyframeDrop */ false);

    WaitForShutdown(e);
  });
};

TEST_F(MediaDataEncoderTest, H264EncodesAnnexBRecord) {
  H264EncodesTest(Usage::Record, AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodesAnnexBRealtime) {
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodesAVCCRecord) {
  H264EncodesTest(Usage::Record, AsVariant(kH264SpecificAVCC), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodesAVCCRealtime) {
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificAVCC), mData);
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAnnexBRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodesTest(Usage::Record, AsVariant(kH264SpecificAnnexB), mData4K);
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAnnexBRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB), mData4K);
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAVCCRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodesTest(Usage::Record, AsVariant(kH264SpecificAVCC), mData4K);
}

TEST_F(MediaDataEncoderTest, H264Encodes4KAVCCRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificAVCC), mData4K);
}

// This resolution is well within what H264 level 5 permits, but exceeds the
// real per-frame macroblock budget Android's encoders actually have
// (roughly level 4-equivalent, independent of the declared level) -- see
// bug 2064678. Tolerate a graceful init failure there, but nowhere else:
// on every other platform this resolution is expected to encode fine.
//
// NB: kH264SpecificAnnexB declares level 3, whose macroblock budget is far
// too small for this resolution -- this deliberately mismatches declared
// level vs. actual resolution, to check whether that mismatch itself (as
// opposed to the resolution alone) is what breaks encoder init. Compare
// against H264EncodesLevel5CapMatchedLevelAnnexBRealtime below.
TEST_F(MediaDataEncoderTest, H264EncodesLevel5CapAnnexBRealtime) {
#ifdef XP_MACOSX
  GTEST_SKIP() << "Bug 2074146: VideoToolbox drops frames at this size";
#else
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB),
                  mDataLevel5Cap, /* aToleratesInitFailureOnAndroid */ true);
#endif
}

// Same resolution as above, but with a level that actually matches it. Fails
// the same way on Android, for the same reason: the declared level was never
// the limiting factor, the platform's real macroblock budget is.
TEST_F(MediaDataEncoderTest, H264EncodesLevel5CapMatchedLevelAnnexBRealtime) {
#ifdef XP_MACOSX
  GTEST_SKIP() << "Bug 2074146: VideoToolbox drops frames at this size";
#else
  H264EncodesTest(Usage::Realtime, AsVariant(kH264SpecificLevel5AnnexB),
                  mDataLevel5Cap, /* aToleratesInitFailureOnAndroid */ true);
#endif
}

static void H264EncodeBatchTest(
    Usage aUsage, const EncoderConfig::CodecSpecific& aSpecific,
    MediaDataEncoderTest::FrameSource& aFrameSource) {
  ASSERT_TRUE(aSpecific.is<H264Specific>());
  ASSERT_TRUE(aSpecific.as<H264Specific>().mFormat ==
                  H264BitStreamFormat::ANNEXB ||
              aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC);

  RUN_IF_SUPPORTED(CodecType::H264, [&]() {
    bool isAVCC =
        aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC;

    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        aUsage, EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        aFrameSource.GetSize(), ScalabilityMode::None, aSpecific);
    EXPECT_TRUE(EnsureInit(e));

    const bool is4KOrLarger = kImageSize4K <= aFrameSource.GetSize();
    const size_t numFrames = NUM_FRAMES / (is4KOrLarger ? 3 : 1);
    constexpr size_t batchSize = 6;
    EncodeResult r = GET_OR_RETURN_ON_ERROR(
        EncodeBatchWithInputStats(e, numFrames, aFrameSource, batchSize));
    MediaDataEncoder::EncodedData output = std::move(r.mEncodedData);
    CheckH264EncodeOutput(output, numFrames, r.mInputKeyframes, aUsage,
                          is4KOrLarger, isAVCC,
                          /* aToleratesKeyframeDrop */ true);

    WaitForShutdown(e);
  });
};

TEST_F(MediaDataEncoderTest, H264EncodeBatchAnnexBRecord) {
  H264EncodeBatchTest(Usage::Record, AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatchAnnexBRealtime) {
  H264EncodeBatchTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatchAVCCRecord) {
  H264EncodeBatchTest(Usage::Record, AsVariant(kH264SpecificAVCC), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatchAVCCRealtime) {
  H264EncodeBatchTest(Usage::Realtime, AsVariant(kH264SpecificAVCC), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatch4KAnnexBRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodeBatchTest(Usage::Record, AsVariant(kH264SpecificAnnexB), mData4K);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatch4KAnnexBRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodeBatchTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB), mData4K);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatch4KAVCCRecord) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodeBatchTest(Usage::Record, AsVariant(kH264SpecificAVCC), mData4K);
}

TEST_F(MediaDataEncoderTest, H264EncodeBatch4KAVCCRealtime) {
  SKIP_IF_ANDROID_SW();  // Android SW can't encode 4K.
  H264EncodeBatchTest(Usage::Realtime, AsVariant(kH264SpecificAVCC), mData4K);
}

#if !defined(ANDROID)
static void H264EncodeAfterDrainTest(
    Usage aUsage, const EncoderConfig::CodecSpecific& aSpecific,
    MediaDataEncoderTest::FrameSource& aFrameSource) {
  ASSERT_TRUE(aSpecific.is<H264Specific>());
  ASSERT_TRUE(aSpecific.as<H264Specific>().mFormat ==
                  H264BitStreamFormat::ANNEXB ||
              aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC);

  RUN_IF_SUPPORTED(CodecType::H264, [&]() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        aUsage, EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        aFrameSource.GetSize(), ScalabilityMode::None, aSpecific);

    EXPECT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, aFrameSource));
    EXPECT_EQ(output.Length(), NUM_FRAMES);

    output = GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, aFrameSource));
    EXPECT_EQ(output.Length(), NUM_FRAMES);

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264EncodeAfterDrainAnnexBRecord) {
  H264EncodeAfterDrainTest(Usage::Record, AsVariant(kH264SpecificAnnexB),
                           mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeAfterDrainAnnexBRealtime) {
  H264EncodeAfterDrainTest(Usage::Realtime, AsVariant(kH264SpecificAnnexB),
                           mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeAfterDrainAVCCRecord) {
  H264EncodeAfterDrainTest(Usage::Record, AsVariant(kH264SpecificAVCC), mData);
}

TEST_F(MediaDataEncoderTest, H264EncodeAfterDrainAVCCRealtime) {
  H264EncodeAfterDrainTest(Usage::Realtime, AsVariant(kH264SpecificAVCC),
                           mData);
}

static void H264InterleavedEncodeAndDrainTest(
    Usage aUsage, const EncoderConfig::CodecSpecific& aSpecific,
    MediaDataEncoderTest::FrameSource& aFrameSource) {
  ASSERT_TRUE(aSpecific.is<H264Specific>());
  ASSERT_TRUE(aSpecific.as<H264Specific>().mFormat ==
                  H264BitStreamFormat::ANNEXB ||
              aSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC);

  RUN_IF_SUPPORTED(CodecType::H264, [&]() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        aUsage, EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        aFrameSource.GetSize(), ScalabilityMode::None, aSpecific);

    EXPECT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output;
    for (size_t i = 0; i < NUM_FRAMES; i++) {
      RefPtr<MediaData> frame = aFrameSource.GetFrame(i);
      output.AppendElements(GET_OR_RETURN_ON_ERROR(WaitFor(e->Encode(frame))));
      if (i % 5 == 0) {
        output.AppendElements(GET_OR_RETURN_ON_ERROR(Drain(e)));
      }
    }
    output.AppendElements(GET_OR_RETURN_ON_ERROR(Drain(e)));

    EXPECT_EQ(output.Length(), NUM_FRAMES);

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264InterleavedEncodeAndDrainAnnexBRecord) {
  H264InterleavedEncodeAndDrainTest(Usage::Record,
                                    AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264InterleavedEncodeAndDrainAnnexBRealtime) {
  H264InterleavedEncodeAndDrainTest(Usage::Realtime,
                                    AsVariant(kH264SpecificAnnexB), mData);
}

TEST_F(MediaDataEncoderTest, H264InterleavedEncodeAndDrainAVCCRecord) {
  H264InterleavedEncodeAndDrainTest(Usage::Record, AsVariant(kH264SpecificAVCC),
                                    mData);
}

TEST_F(MediaDataEncoderTest, H264InterleavedEncodeAndDrainAVCCRealtime) {
  H264InterleavedEncodeAndDrainTest(Usage::Realtime,
                                    AsVariant(kH264SpecificAVCC), mData);
}
#endif

TEST_F(MediaDataEncoderTest, H264Duration) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder();
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, H264InvalidSize) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e0x0 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {0, 0},
        ScalabilityMode::None, AsVariant(kH264SpecificAnnexB));
    EXPECT_EQ(e0x0, nullptr);

    RefPtr<MediaDataEncoder> e0x1 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {0, 1},
        ScalabilityMode::None, AsVariant(kH264SpecificAnnexB));
    EXPECT_EQ(e0x1, nullptr);

    RefPtr<MediaDataEncoder> e1x0 = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {1, 0},
        ScalabilityMode::None, AsVariant(kH264SpecificAnnexB));
    EXPECT_EQ(e1x0, nullptr);
  });
}

#if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, H264AVCC) {
  RUN_IF_SUPPORTED(CodecType::H264, [this]() {
    // Encod frames in avcC format.
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, ScalabilityMode::None, AsVariant(kH264SpecificAVCC));
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (auto frame : output) {
      EXPECT_FALSE(AnnexB::IsAnnexB(*frame));
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

#ifdef XP_MACOSX
static already_AddRefed<MediaData> CreateNV12Frame(const gfx::IntSize& aSize) {
  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect(0, 0, aSize.width, aSize.height);
  data.mYStride = aSize.width;
  data.mCbCrStride = aSize.width;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  data.mCbSkip = 1;
  data.mCrSkip = 1;
  const auto sizes = data.GetPlaneSizes();
  const auto& ySize = sizes->mY;
  const auto& cbcrSize = sizes->mCbCr;
  const size_t yBytes = static_cast<size_t>(data.mYStride) * ySize.height;
  const size_t cbcrBytes =
      static_cast<size_t>(data.mCbCrStride) * cbcrSize.height;
  auto buffer = std::make_unique<uint8_t[]>(yBytes + cbcrBytes);
  std::fill_n(buffer.get(), yBytes, static_cast<uint8_t>(235));
  std::fill_n(buffer.get() + yBytes, cbcrBytes, static_cast<uint8_t>(128));
  data.mYChannel = buffer.get();
  data.mCbChannel = buffer.get() + yBytes;
  data.mCrChannel = data.mCbChannel + 1;
  RefPtr<layers::NVImage> image = new layers::NVImage();
  if (NS_FAILED(image->SetData(data))) {
    return nullptr;
  }
  RefPtr<MediaData> frame = VideoData::CreateFromImage(
      aSize, 0, media::TimeUnit::Zero(),
      media::TimeUnit::FromMicroseconds(FRAME_DURATION), image, true,
      media::TimeUnit::Zero());
  return frame.forget();
}

struct RawBgrxFrame {
  static constexpr size_t kBytesPerPixel = 4;

  gfx::IntSize mSize;
  size_t mStride;
  nsTArray<uint8_t> mData;

  size_t PixelOffset(int32_t aX, int32_t aY) const {
    const bool isInBounds =
        aX >= 0 && aX < mSize.width && aY >= 0 && aY < mSize.height;
    EXPECT_TRUE(isInBounds);
    if (!isInBounds) {
      return 0;
    }
    const bool hasPixel = mData.Length() >= kBytesPerPixel;
    EXPECT_TRUE(hasPixel);
    if (!hasPixel) {
      return 0;
    }
    const CheckedInt<size_t> offset =
        CheckedInt<size_t>(static_cast<size_t>(aY)) * mStride +
        CheckedInt<size_t>(static_cast<size_t>(aX)) * kBytesPerPixel;
    const bool isValidOffset =
        offset.isValid() && offset.value() <= mData.Length() - kBytesPerPixel;
    EXPECT_TRUE(isValidOffset);
    if (!isValidOffset) {
      return 0;
    }
    return offset.value();
  }

  uint8_t* PixelData(int32_t aX, int32_t aY) {
    return mData.Elements() + PixelOffset(aX, aY);
  }

  const uint8_t* PixelData(int32_t aX, int32_t aY) const {
    return mData.Elements() + PixelOffset(aX, aY);
  }

  const uint8_t* RowData(int32_t aY) const { return PixelData(0, aY); }
};

static UniquePtr<RawBgrxFrame> CreateCornerPattern(const gfx::IntSize& aSize,
                                                   int32_t aMarker) {
  if (aSize.width <= 0 || aSize.height <= 0 || aMarker <= 0 ||
      aMarker > (aSize.width - 1) / 2 || aMarker > (aSize.height - 1) / 2) {
    return nullptr;
  }

  const CheckedInt<size_t> rowBytes =
      CheckedInt<size_t>(static_cast<size_t>(aSize.width)) *
      RawBgrxFrame::kBytesPerPixel;
  const CheckedInt<size_t> bufferBytes =
      rowBytes * static_cast<size_t>(aSize.height);
  if (!rowBytes.isValid() || !bufferBytes.isValid()) {
    return nullptr;
  }

  auto frame = std::make_unique<RawBgrxFrame>();
  frame->mSize = aSize;
  frame->mStride = rowBytes.value();
  if (!frame->mData.SetLength(bufferBytes.value(), fallible)) {
    return nullptr;
  }
  std::fill_n(frame->mData.Elements(), frame->mData.Length(),
              static_cast<uint8_t>(255));

  for (int32_t y = 0; y < aSize.height; ++y) {
    for (int32_t x = 0; x < aSize.width; ++x) {
      if (!((x < aMarker || x >= aSize.width - aMarker) &&
            (y < aMarker || y >= aSize.height - aMarker))) {
        continue;
      }
      uint8_t* pixel = frame->PixelData(x, y);
      pixel[0] = 0;
      pixel[1] = 0;
      pixel[2] = 0;
    }
  }
  return frame;
}

class MutateOnUnmapDataSourceSurface final : public gfx::DataSourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(MutateOnUnmapDataSourceSurface, final)

  explicit MutateOnUnmapDataSourceSurface(gfx::DataSourceSurface* aSurface)
      : mSurface(aSurface) {}

  uint8_t* GetData() final { return mSurface->GetData(); }
  int32_t Stride() final { return mSurface->Stride(); }
  gfx::IntSize GetSize() const final { return mSurface->GetSize(); }
  gfx::SurfaceFormat GetFormat() const final { return mSurface->GetFormat(); }

  bool Map(MapType aType, MappedSurface* aMap) final {
    return mSurface->Map(aType, aMap);
  }

  void Unmap() final {
    mSurface->Unmap();
    if (!mMutateOnUnmap) {
      return;
    }
    mMutateOnUnmap = false;
    MappedSurface map;
    if (!mSurface->Map(WRITE, &map)) {
      return;
    }
    const size_t bytes =
        gfx::BufferSizeFromStrideAndHeight(map.mStride, GetSize().height);
    if (map.mData && bytes) {
      std::fill_n(map.mData, bytes, static_cast<uint8_t>(0));
      mDidMutate = true;
    }
    mSurface->Unmap();
  }

  void MutateOnNextUnmap() { mMutateOnUnmap = true; }
  bool DidMutate() const { return mDidMutate; }

 private:
  ~MutateOnUnmapDataSourceSurface() final = default;

  RefPtr<gfx::DataSourceSurface> mSurface;
  bool mMutateOnUnmap = false;
  bool mDidMutate = false;
};

static already_AddRefed<MediaData> CreateSourceSurfaceFrame(
    const RawBgrxFrame& aFrame,
    RefPtr<MutateOnUnmapDataSourceSurface>* aMutatingSurface) {
  RefPtr<gfx::DataSourceSurface> surface =
      gfx::Factory::CreateDataSourceSurface(aFrame.mSize,
                                            gfx::SurfaceFormat::B8G8R8X8, true);
  if (!surface) {
    return nullptr;
  }
  gfx::DataSourceSurface::ScopedMap map(surface, gfx::DataSourceSurface::WRITE);
  if (!map.IsMapped() || map.GetStride() <= 0 || !map.GetData() ||
      static_cast<size_t>(map.GetStride()) < aFrame.mStride ||
      !gfx::BufferSizeFromStrideAndHeight(map.GetStride(),
                                          aFrame.mSize.height)) {
    return nullptr;
  }
  for (int32_t y = 0; y < aFrame.mSize.height; ++y) {
    const CheckedInt<size_t> offset =
        CheckedInt<size_t>(static_cast<size_t>(y)) * map.GetStride();
    if (!offset.isValid()) {
      return nullptr;
    }
    std::copy_n(aFrame.RowData(y), aFrame.mStride,
                map.GetData() + offset.value());
  }
  RefPtr<MutateOnUnmapDataSourceSurface> mutatingSurface =
      new MutateOnUnmapDataSourceSurface(surface);
  RefPtr<layers::Image> image =
      new layers::SourceSurfaceImage(aFrame.mSize, mutatingSurface);
  *aMutatingSurface = std::move(mutatingSurface);
  RefPtr<MediaData> frame = VideoData::CreateFromImage(
      aFrame.mSize, 0, media::TimeUnit::Zero(),
      media::TimeUnit::FromMicroseconds(FRAME_DURATION), image, true,
      media::TimeUnit::Zero());
  return frame.forget();
}

static void AssertPixelBufferMatches(CVPixelBufferRef aPixelBuffer,
                                     const RawBgrxFrame& aExpected) {
  ASSERT_EQ(CVPixelBufferGetPixelFormatType(aPixelBuffer),
            kCVPixelFormatType_32BGRA);
  ASSERT_EQ(CVPixelBufferGetWidth(aPixelBuffer),
            static_cast<size_t>(aExpected.mSize.width));
  ASSERT_EQ(CVPixelBufferGetHeight(aPixelBuffer),
            static_cast<size_t>(aExpected.mSize.height));
  ASSERT_EQ(
      CVPixelBufferLockBaseAddress(aPixelBuffer, kCVPixelBufferLock_ReadOnly),
      kCVReturnSuccess);
  auto unlock = MakeScopeExit([&] {
    CVPixelBufferUnlockBaseAddress(aPixelBuffer, kCVPixelBufferLock_ReadOnly);
  });

  const size_t destinationStride = CVPixelBufferGetBytesPerRow(aPixelBuffer);
  const CheckedInt<size_t> destinationLength =
      CheckedInt<size_t>(destinationStride) *
      static_cast<size_t>(aExpected.mSize.height);
  ASSERT_TRUE(destinationLength.isValid());
  ASSERT_LE(destinationLength.value(), CVPixelBufferGetDataSize(aPixelBuffer));
  ASSERT_GE(destinationStride, aExpected.mStride);
  const uint8_t* destination =
      static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(aPixelBuffer));
  ASSERT_TRUE(destination);

  for (int32_t y = 0; y < aExpected.mSize.height; ++y) {
    const CheckedInt<size_t> destinationOffset =
        CheckedInt<size_t>(static_cast<size_t>(y)) * destinationStride;
    ASSERT_TRUE(destinationOffset.isValid());
    SCOPED_TRACE(y);
    ASSERT_EQ(std::memcmp(destination + destinationOffset.value(),
                          aExpected.RowData(y), aExpected.mStride),
              0);
  }
}

TEST_F(MediaDataEncoderTest, H264EncodeNV12Input) {
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    const gfx::IntSize size(640, 480);
    RefPtr<MediaDataEncoder> encoder = CreateH264Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420SP_NV12),
        size, ScalabilityMode::None, AsVariant(kH264SpecificAVCC));
    if (!encoder) {
      return;
    }
    ASSERT_TRUE(EnsureInit(encoder));
    RefPtr<MediaData> frame = CreateNV12Frame(size);
    ASSERT_TRUE(frame);
    auto encoded = WaitFor(encoder->Encode(frame));
    ASSERT_TRUE(encoded.isOk());
    MediaDataEncoder::EncodedData output = encoded.unwrap();
    auto drained = Drain(encoder);
    ASSERT_TRUE(drained.isOk());
    output.AppendElements(std::move(drained.unwrap()));
    WaitForShutdown(encoder);
    CheckH264EncodeOutput(output, 1, 1, Usage::Record,
                          /* aIs4KOrLarger */ false,
                          /* aIsAVCC */ true,
                          /* aToleratesKeyframeDrop */ false);
  });
}

namespace mozilla {

// Poison the source on unmap to verify that the returned pixel buffer owns an
// independent copy rather than retaining the scoped map's address.
TEST(AppleVTEncoderTest, CopiesPackedInputBeforeUnmap)
{
  const gfx::IntSize size(640, 480);
  constexpr int32_t kCornerMarkerSize = 80;
  UniquePtr<RawBgrxFrame> expected =
      CreateCornerPattern(size, kCornerMarkerSize);
  ASSERT_TRUE(expected);
  const EncoderConfig config(
      CodecType::H264, size, Usage::Record,
      EncoderConfig::SampleFormat(dom::ImageBitmapFormat::BGRA32), FRAME_RATE,
      KEYFRAME_INTERVAL, BIT_RATE, 0, 0, BIT_RATE_MODE,
      HardwarePreference::None, ScalabilityMode::None,
      AsVariant(kH264SpecificAVCC));
  RefPtr<TaskQueue> taskQueue = TaskQueue::Create(
      GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER), __func__);
  ASSERT_TRUE(taskQueue);
  RefPtr<AppleVTEncoder> encoder = new AppleVTEncoder(config, taskQueue);
  CVPixelBufferRef pixelBuffer = nullptr;
  auto cleanup = MakeScopeExit([&] {
    if (pixelBuffer) {
      CVPixelBufferRelease(pixelBuffer);
    }
    encoder = nullptr;
    taskQueue->BeginShutdown();
    taskQueue->AwaitShutdownAndIdle();
  });

  RefPtr<MutateOnUnmapDataSourceSurface> sourceSurface;
  RefPtr<MediaData> frame = CreateSourceSurfaceFrame(*expected, &sourceSurface);
  ASSERT_TRUE(frame);
  ASSERT_TRUE(sourceSurface);
  RefPtr<layers::Image> image = frame->As<VideoData>()->mImage;
  ASSERT_TRUE(image);
  sourceSurface->MutateOnNextUnmap();

  (void)WaitFor(InvokeAsync(taskQueue, __func__, [&] {
    pixelBuffer = encoder->CreateCVPixelBuffer(image);
    return GenericPromise::CreateAndResolve(true, __func__);
  }));

  ASSERT_TRUE(sourceSurface->DidMutate());
  ASSERT_TRUE(pixelBuffer);
  AssertPixelBufferMatches(pixelBuffer, *expected);
}

}  // namespace mozilla
#endif

// For Android HW encoder only.
#ifdef MOZ_WIDGET_ANDROID
TEST_F(MediaDataEncoderTest, AndroidNotSupportedSize) {
  SKIP_IF_ANDROID_SW();
  RUN_IF_SUPPORTED(CodecType::H264, []() {
    RefPtr<MediaDataEncoder> e = CreateH264Encoder(
        Usage::Realtime,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P), {1, 1},
        ScalabilityMode::None, AsVariant(kH264SpecificAnnexB));
    EXPECT_NE(e, nullptr);
    EXPECT_FALSE(EnsureInit(e));
  });
}
#endif

#if !(defined(MOZ_WIDGET_GTK) && defined(__i386__))
static already_AddRefed<MediaDataEncoder> CreateVP8Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const EncoderConfig::CodecSpecific& aSpecific = AsVariant(VP8Specific())) {
  return CreateVideoEncoder(CodecType::VP8, aUsage, aFormat, aSize,
                            BIT_RATE_MODE, HardwarePreference::None,
                            aScalabilityMode, aSpecific);
}

static already_AddRefed<MediaDataEncoder> CreateVP9Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None,
    const EncoderConfig::CodecSpecific& aSpecific = AsVariant(VP9Specific())) {
  return CreateVideoEncoder(CodecType::VP9, aUsage, aFormat, aSize,
                            BIT_RATE_MODE, HardwarePreference::None,
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
        kImageSize, ScalabilityMode::None, AsVariant(void_t{}));
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
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, 1UL, mData));
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
    EXPECT_TRUE(EnsureInit(e));
    output = GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

#  if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, VP8EncodeAfterDrain) {
  RUN_IF_SUPPORTED(CodecType::VP8, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP8Encoder();
    EXPECT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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

    output = GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
        kImageSize, ScalabilityMode::L1T2, AsVariant(specific));
    EXPECT_TRUE(EnsureInit(e));

    const nsTArray<uint8_t> pattern({0, 1});
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
        kImageSize, ScalabilityMode::L1T3, AsVariant(specific));
    EXPECT_TRUE(EnsureInit(e));

    const nsTArray<uint8_t> pattern({0, 2, 1, 2});
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
#  endif

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
        kImageSize, ScalabilityMode::None, AsVariant(void_t{}));
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
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, 1UL, mData));
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
    EXPECT_TRUE(EnsureInit(e));
    output = GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
    EXPECT_TRUE(EnsureInit(e));
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
    EXPECT_EQ(output.Length(), NUM_FRAMES);
    for (const auto& frame : output) {
      EXPECT_GT(frame->mDuration, media::TimeUnit::Zero());
    }
    WaitForShutdown(e);
  });
}

#  if !defined(ANDROID)
TEST_F(MediaDataEncoderTest, VP9EncodeAfterDrain) {
  RUN_IF_SUPPORTED(CodecType::VP9, [this]() {
    RefPtr<MediaDataEncoder> e = CreateVP9Encoder();
    EXPECT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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

    output = GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
        kImageSize, ScalabilityMode::L1T2, AsVariant(specific));
    EXPECT_TRUE(EnsureInit(e));

    const nsTArray<uint8_t> pattern({0, 1});
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
        kImageSize, ScalabilityMode::L1T3, AsVariant(specific));
    EXPECT_TRUE(EnsureInit(e));

    const nsTArray<uint8_t> pattern({0, 2, 1, 2});
    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, NUM_FRAMES, mData));
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
#  endif
#endif

already_AddRefed<MediaDataEncoder> CreateAudioEncoder(
    CodecType aCodec, uint32_t aChannels, uint32_t aSampleRate,
    uint32_t aBitrate, BitrateMode aBitrateMode,
    const EncoderConfig::CodecSpecific& aSpecific =
        EncoderConfig::CodecSpecific(void_t{})) {
  RefPtr<PEMFactory> f(new PEMFactory());

  if (f->SupportsCodec(aCodec).isEmpty()) {
    return nullptr;
  }

  const EncoderConfig config(aCodec, aChannels, aBitrateMode, aSampleRate,
                             aBitrate, aSpecific);
  if (f->Supports(config).isEmpty()) {
    return nullptr;
  }

  const RefPtr<TaskQueue> taskQueue(
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "TestMediaDataEncoder"));
  RefPtr<MediaDataEncoder> e = f->CreateEncoder(config, taskQueue);
  return e.forget();
}

TEST_F(MediaDataEncoderTest, RejectsOversizedOutput) {
  ScopedPrefSetter encoderEnabled("media.ffmpeg.encoder.enabled", true);

  // Opus only supports a fixed set of sample rates, so FFmpegAudioEncoder snaps
  // this 1 Hz request up to Opus's lowest supported rate, 8000 Hz, and installs
  // a 1 -> 8000 resampler. At that 8000x ratio, kOverLargeFrameCount input
  // frames resample to more than UINT32_MAX output frames, which the encoder
  // cannot represent, so Encode() rejects the AudioData.
  const uint32_t kChannels = 1;
  const uint32_t kInputRate = 1;
  const size_t kOverLargeFrameCount = 600000;

  RefPtr<MediaDataEncoder> e =
      CreateAudioEncoder(CodecType::Opus, kChannels, kInputRate,
                         128000 /* bitrate */, BitrateMode::Constant);
  EXPECT_TRUE(EnsureInit(e));

  AlignedAudioBuffer data(kOverLargeFrameCount * kChannels);
  ASSERT_TRUE(!!data);
  RefPtr<MediaData> frame =
      new AudioData(0, media::TimeUnit::Zero(kInputRate), std::move(data),
                    kChannels, kInputRate);
  nsTArray<RefPtr<MediaData>> batch;
  batch.AppendElement(std::move(frame));

  EXPECT_TRUE(WaitFor(e->Encode(std::move(batch))).isErr());
  EXPECT_TRUE(WaitFor(e->Drain()).isOk());

  WaitForShutdown(e);
}

TEST_F(MediaDataEncoderTest, SmallDownsampledInput) {
  ScopedPrefSetter encoderEnabled("media.ffmpeg.encoder.enabled", true);

  // libopus's maximum supported sample rate.
  constexpr uint32_t kCodecRate = 48000;
  // Above 48 kHz libopus snaps the rate down to kCodecRate; 384000 Hz gives an
  // 8:1 downsample through the resampler.
  constexpr uint32_t kInputRate = 384000;
  // kInputCount below divides by kInputRate / kCodecRate and relies on libopus
  // snapping kInputRate down to kCodecRate, so the ratio must be a whole number
  // greater than one.
  static_assert(kInputRate > kCodecRate && kInputRate % kCodecRate == 0,
                "kInputRate must be a whole multiple of kCodecRate above it");

  // Pin the shortest Opus frame (2.5 ms) so a packet needs the fewest input
  // frames. The codec-rate packet size is kCodecRate * frame duration.
  const uint32_t kFrameDurationUs = 2500;  // microseconds
  OpusSpecific opus;
  opus.mFrameDuration = kFrameDurationUs;
  const uint32_t kCodecFramesPerPacket =
      kCodecRate * kFrameDurationUs / 1000000;

  constexpr uint32_t kChannels = 2;
  RefPtr<MediaDataEncoder> e = CreateAudioEncoder(
      CodecType::Opus, kChannels, kInputRate, 128000 /* bitrate */,
      BitrateMode::Constant, AsVariant(opus));
  EXPECT_TRUE(EnsureInit(e));

  // The resampler delivers floor(N / R) - L frames to the packetizer, where
  //   N = kInputCount, the input frames fed,
  //   R = kInputRate / kCodecRate, the downsample ratio,
  //   L = the speex resampler's filter latency: output frames still inside the
  //       filter, never flushed because the encoder drains the packetizer, not
  //       the resampler.
  //
  // To emit a whole packet the resampler must deliver at least
  // kCodecFramesPerPacket frames:
  //   floor(N / R) - L >= kCodecFramesPerPacket
  //   => N >= R * (kCodecFramesPerPacket + L)
  //
  // The resampler latency stays below one packet, so for any
  // L <= kCodecFramesPerPacket:
  //   R * (kCodecFramesPerPacket + L) <= 2 * R * kCodecFramesPerPacket
  // Hence N = 2 * R * kCodecFramesPerPacket always clears a full packet,
  // regardless of the exact latency. (With the bug every input is dropped, so
  // nothing accumulates and output stays empty.)
  const size_t kInputCount =
      2 * kCodecFramesPerPacket * (kInputRate / kCodecRate);
  nsTArray<RefPtr<MediaData>> batch;
  for (size_t i = 0; i < kInputCount; i++) {
    AlignedAudioBuffer data(kChannels);  // one frame, two channels
    ASSERT_TRUE(!!data);
    // One frame per input keeps every call on the small-downsample path whose
    // per-call output capacity floors to zero.
    RefPtr<MediaData> frame =
        new AudioData(0, media::TimeUnit::Zero(kInputRate), std::move(data),
                      kChannels, kInputRate);
    batch.AppendElement(std::move(frame));
  }

  auto result = WaitFor(e->Encode(std::move(batch)));
  ASSERT_TRUE(result.isOk());
  MediaDataEncoder::EncodedData output = result.unwrap();

  auto drained = WaitFor(e->Drain());
  ASSERT_TRUE(drained.isOk());
  output.AppendElements(drained.unwrap());

  EXPECT_GT(output.Length(), 0u)
      << "small downsampled input was dropped instead of consumed";

  WaitForShutdown(e);
}

static already_AddRefed<MediaDataEncoder> CreateAV1Encoder(
    Usage aUsage = Usage::Realtime,
    EncoderConfig::SampleFormat aFormat =
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
    gfx::IntSize aSize = kImageSize, BitrateMode aBitrateMode = BIT_RATE_MODE,
    HardwarePreference aHardwarePreference = HardwarePreference::None,
    ScalabilityMode aScalabilityMode = ScalabilityMode::None) {
  return CreateVideoEncoder(CodecType::AV1, aUsage, aFormat, aSize,
                            aBitrateMode, aHardwarePreference, aScalabilityMode,
                            AsVariant(void_t{}));
}

struct AV1ColorTestData {
  gfx::ColorRange mRange;
  gfx::YUVColorSpace mMatrix;
  gfx::ColorSpace2 mPrimaries;
  gfx::TransferFunction mTransfer;
  gfx::CICP::ColourPrimaries mExpectedPrimaries;
  gfx::CICP::TransferCharacteristics mExpectedTransfer;
  gfx::CICP::MatrixCoefficients mExpectedMatrix;
};

static constexpr AV1ColorTestData kBT2020PQFull{
    gfx::ColorRange::FULL,    gfx::YUVColorSpace::BT2020,
    gfx::ColorSpace2::BT2020, gfx::TransferFunction::PQ,
    gfx::CICP::CP_BT2020,     gfx::CICP::TC_SMPTE2084,
    gfx::CICP::MC_BT2020_NCL,
};

static constexpr AV1ColorTestData kBT709Limited{
    gfx::ColorRange::LIMITED, gfx::YUVColorSpace::BT709,
    gfx::ColorSpace2::BT709,  gfx::TransferFunction::BT709,
    gfx::CICP::CP_BT709,      gfx::CICP::TC_BT709,
    gfx::CICP::MC_BT709,
};

static constexpr AV1ColorTestData kUnspecifiedPrimaries{
    gfx::ColorRange::LIMITED,  gfx::YUVColorSpace::BT709,
    gfx::ColorSpace2::UNKNOWN, gfx::TransferFunction::BT709,
    gfx::CICP::CP_UNSPECIFIED, gfx::CICP::TC_BT709,
    gfx::CICP::MC_BT709,
};

static void SetFrameColor(MediaDataEncoderTest::FrameSource& aSource,
                          const AV1ColorTestData& aColor) {
  aSource.mYUV.mColorRange = aColor.mRange;
  aSource.mYUV.mYUVColorSpace = aColor.mMatrix;
  aSource.mYUV.mColorPrimaries = aColor.mPrimaries;
  aSource.mYUV.mTransferFunction = aColor.mTransfer;
}

static EncoderConfig::SampleFormat AV1ColorFormat(
    const AV1ColorTestData& aColor) {
  return EncoderConfig::SampleFormat(
      dom::ImageBitmapFormat::YUV420P,
      EncoderConfig::VideoColorSpace(aColor.mRange, aColor.mMatrix,
                                     aColor.mPrimaries, aColor.mTransfer));
}

static void ExpectAV1Color(const MediaRawData& aPacket,
                           const AV1ColorTestData& aExpected) {
  AOMDecoder::AV1SequenceInfo info;
  MediaResult result = AOMDecoder::ReadSequenceHeaderInfo(
      Span(aPacket.Data(), aPacket.Size()), info);
  ASSERT_EQ(result.Code(), NS_OK) << "packet must contain the sequence header";
  EXPECT_EQ(info.mColorSpace.mPrimaries, aExpected.mExpectedPrimaries);
  EXPECT_EQ(info.mColorSpace.mTransfer, aExpected.mExpectedTransfer);
  EXPECT_EQ(info.mColorSpace.mMatrix, aExpected.mExpectedMatrix);
  EXPECT_EQ(info.mColorSpace.mRange, aExpected.mRange);
}

static Result<bool, MediaResult> EncodeAV1ColorFrame(
    const RefPtr<MediaDataEncoder>& aEncoder,
    MediaDataEncoderTest::FrameSource& aSource, size_t aIndex,
    const AV1ColorTestData& aColor, MediaDataEncoder::EncodedData& aOutput) {
  SetFrameColor(aSource, aColor);
  RefPtr<MediaData> frame = aSource.GetFrame(aIndex);
  bool requestedKeyframe = frame->mKeyframe;
  aOutput.AppendElements(MOZ_TRY(WaitFor(aEncoder->Encode(frame))));
  return requestedKeyframe;
}

TEST_F(MediaDataEncoderTest, AV1SignalsColorConfigInSequenceHeader) {
  RUN_IF_SUPPORTED(CodecType::AV1, [this]() {
    SetFrameColor(mData, kBT2020PQFull);

    RefPtr<MediaDataEncoder> e =
        CreateAV1Encoder(Usage::Record, AV1ColorFormat(kBT2020PQFull));
    EXPECT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output =
        GET_OR_RETURN_ON_ERROR(Encode(e, 1UL, mData));
    EXPECT_EQ(output.Length(), 1UL);

    ExpectAV1Color(*output[0], kBT2020PQFull);

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, AV1FrameColorOverridesConfig) {
  RUN_IF_SUPPORTED(CodecType::AV1, [this]() {
    SetFrameColor(mData, kBT2020PQFull);
    RefPtr<MediaDataEncoder> e = CreateAV1Encoder(
        Usage::Record, AV1ColorFormat(kBT709Limited), kImageSize, BIT_RATE_MODE,
        HardwarePreference::RequireSoftware);
    ASSERT_TRUE(EnsureInit(e));

    auto r = Encode(e, 1U, mData);
    ASSERT_TRUE(r.isOk());
    MediaDataEncoder::EncodedData output = r.unwrap();
    ASSERT_EQ(output.Length(), 1u);
    ExpectAV1Color(*output[0], kBT2020PQFull);

    WaitForShutdown(e);
  });
}

static Maybe<uint8_t> GetAV1FrameTemporalId(const MediaRawData& aPacket) {
  auto data = Span(aPacket.Data(), aPacket.Size());
  auto iter = AOMDecoder::ReadOBUs(data);
  while (iter.HasNext()) {
    AOMDecoder::OBUInfo obu = iter.Next();
    if (obu.mType == AOMDecoder::OBUType::FrameHeader ||
        obu.mType == AOMDecoder::OBUType::Frame) {
      return Some(obu.mTemporalId);
    }
  }
  return Nothing();
}

TEST_F(MediaDataEncoderTest, AV1SVCTemporalIdsMatchBitstream) {
  RUN_IF_SUPPORTED(CodecType::AV1, [this]() {
    RefPtr<MediaDataEncoder> e = CreateAV1Encoder(
        Usage::Record,
        EncoderConfig::SampleFormat(dom::ImageBitmapFormat::YUV420P),
        kImageSize, BitrateMode::Constant, HardwarePreference::RequireSoftware,
        ScalabilityMode::L1T3);
    ASSERT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output;
    for (size_t i = 0; i <= KEYFRAME_INTERVAL; ++i) {
      RefPtr<MediaData> frame = mData.GetFrame(i);
      frame->mKeyframe = i == 0;
      output.AppendElements(GET_OR_RETURN_ON_ERROR(WaitFor(e->Encode(frame))));
    }
    output.AppendElements(GET_OR_RETURN_ON_ERROR(Drain(e)));

    ASSERT_EQ(output.Length(), size_t{KEYFRAME_INTERVAL + 1});
    for (size_t i = 0; i < output.Length(); ++i) {
      SCOPED_TRACE(i);
      Maybe<uint8_t> temporalId = GetAV1FrameTemporalId(*output[i]);
      ASSERT_TRUE(temporalId);
      EXPECT_EQ(output[i]->mTemporalLayerId, temporalId);
    }

    const RefPtr<MediaRawData>& keyframe = output[KEYFRAME_INTERVAL];
    ASSERT_TRUE(keyframe->mKeyframe);
    EXPECT_EQ(GetAV1FrameTemporalId(*keyframe), Some(uint8_t{1}));

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, AV1ColorChangeForcesKeyframe) {
  RUN_IF_SUPPORTED(CodecType::AV1, [this]() {
    RefPtr<MediaDataEncoder> e = CreateAV1Encoder(
        Usage::Realtime, AV1ColorFormat(kBT709Limited), kImageSize,
        BIT_RATE_MODE, HardwarePreference::RequireSoftware);
    ASSERT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output;
    auto firstResult = EncodeAV1ColorFrame(e, mData, 0, kBT709Limited, output);
    ASSERT_TRUE(firstResult.isOk());
    EXPECT_TRUE(firstResult.unwrap());

    auto secondResult = EncodeAV1ColorFrame(e, mData, 1, kBT2020PQFull, output);
    ASSERT_TRUE(secondResult.isOk());
    EXPECT_FALSE(secondResult.unwrap());

    auto thirdResult =
        EncodeAV1ColorFrame(e, mData, 2, kUnspecifiedPrimaries, output);
    ASSERT_TRUE(thirdResult.isOk());
    EXPECT_FALSE(thirdResult.unwrap());

    auto drainResult = Drain(e);
    ASSERT_TRUE(drainResult.isOk());
    output.AppendElements(drainResult.unwrap());

    ASSERT_EQ(output.Length(), 3u);
    EXPECT_TRUE(output[0]->mKeyframe);
    EXPECT_TRUE(output[1]->mKeyframe);
    EXPECT_TRUE(output[2]->mKeyframe);
    ExpectAV1Color(*output[0], kBT709Limited);
    ExpectAV1Color(*output[1], kBT2020PQFull);
    ExpectAV1Color(*output[2], kUnspecifiedPrimaries);

    WaitForShutdown(e);
  });
}

TEST_F(MediaDataEncoderTest, AV1SVCColorChangeResetsTemporalLayer) {
  RUN_IF_SUPPORTED(CodecType::AV1, [this]() {
    RefPtr<MediaDataEncoder> e = CreateAV1Encoder(
        Usage::Record, AV1ColorFormat(kBT709Limited), kImageSize,
        BitrateMode::Constant, HardwarePreference::RequireSoftware,
        ScalabilityMode::L1T2);
    ASSERT_TRUE(EnsureInit(e));

    MediaDataEncoder::EncodedData output;
    auto firstResult = EncodeAV1ColorFrame(e, mData, 0, kBT709Limited, output);
    ASSERT_TRUE(firstResult.isOk());
    EXPECT_TRUE(firstResult.unwrap());

    auto secondResult = EncodeAV1ColorFrame(e, mData, 1, kBT2020PQFull, output);
    ASSERT_TRUE(secondResult.isOk());
    EXPECT_FALSE(secondResult.unwrap());

    auto thirdResult =
        EncodeAV1ColorFrame(e, mData, 2, kUnspecifiedPrimaries, output);
    ASSERT_TRUE(thirdResult.isOk());
    EXPECT_FALSE(thirdResult.unwrap());

    auto fourthResult =
        EncodeAV1ColorFrame(e, mData, 3, kUnspecifiedPrimaries, output);
    ASSERT_TRUE(fourthResult.isOk());
    EXPECT_FALSE(fourthResult.unwrap());

    auto drainResult = Drain(e);
    ASSERT_TRUE(drainResult.isOk());
    output.AppendElements(drainResult.unwrap());

    ASSERT_EQ(output.Length(), 4u);
    EXPECT_TRUE(output[0]->mKeyframe);
    EXPECT_TRUE(output[1]->mKeyframe);
    EXPECT_TRUE(output[2]->mKeyframe);
    EXPECT_FALSE(output[3]->mKeyframe);
    EXPECT_EQ(output[1]->mTemporalLayerId, Some(uint8_t{0}));
    EXPECT_EQ(output[2]->mTemporalLayerId, Some(uint8_t{0}));
    EXPECT_EQ(output[3]->mTemporalLayerId, Some(uint8_t{1}));
    EXPECT_EQ(GetAV1FrameTemporalId(*output[1]), Some(uint8_t{0}));
    EXPECT_EQ(GetAV1FrameTemporalId(*output[2]), Some(uint8_t{0}));
    EXPECT_EQ(GetAV1FrameTemporalId(*output[3]), Some(uint8_t{1}));

    WaitForShutdown(e);
  });
}

#undef BLOCK_SIZE
#undef GET_OR_RETURN_ON_ERROR
#undef RUN_IF_SUPPORTED
