/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_test_gtest_Common_h
#define mozilla_image_test_gtest_Common_h

#include <memory>
#include <vector>

#include "Decoder.h"
#include "SurfaceFlags.h"
#include "SurfacePipe.h"
#include "SurfacePipeFactory.h"
#include "gfxColor.h"
#include "gfxPlatform.h"
#include "gtest/gtest.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsCOMPtr.h"

class nsIInputStream;

namespace mozilla {
namespace image {

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

struct BGRAColor {
  BGRAColor() : BGRAColor(0, 0, 0, 0) {}

  BGRAColor(uint8_t aBlue, uint8_t aGreen, uint8_t aRed, uint8_t aAlpha,
            bool aPremultiplied = false, bool asRGB = true)
      : mBlue(aBlue),
        mGreen(aGreen),
        mRed(aRed),
        mAlpha(aAlpha),
        mPremultiplied(aPremultiplied),
        msRGB(asRGB) {}

  static BGRAColor Green() { return BGRAColor(0x00, 0xFF, 0x00, 0xFF); }
  static BGRAColor Red() { return BGRAColor(0x00, 0x00, 0xFF, 0xFF); }
  static BGRAColor Blue() { return BGRAColor(0xFF, 0x00, 0x00, 0xFF); }
  static BGRAColor Transparent() { return BGRAColor(0x00, 0x00, 0x00, 0x00); }

  static BGRAColor FromPixel(uint32_t aPixel) {
    uint8_t r, g, b, a;
    r = (aPixel >> gfx::SurfaceFormatBit::OS_R) & 0xFF;
    g = (aPixel >> gfx::SurfaceFormatBit::OS_G) & 0xFF;
    b = (aPixel >> gfx::SurfaceFormatBit::OS_B) & 0xFF;
    a = (aPixel >> gfx::SurfaceFormatBit::OS_A) & 0xFF;
    return BGRAColor(b, g, r, a, true);
  }

  BGRAColor DeviceColor() const {
    MOZ_RELEASE_ASSERT(!mPremultiplied);
    if (msRGB) {
      gfx::DeviceColor color = gfx::ToDeviceColor(
          gfx::sRGBColor(float(mRed) / 255.0f, float(mGreen) / 255.0f,
                         float(mBlue) / 255.0f, 1.0));
      return BGRAColor(uint8_t(color.b * 255.0f), uint8_t(color.g * 255.0f),
                       uint8_t(color.r * 255.0f), mAlpha, mPremultiplied,
                       /* asRGB */ false);
    }
    return *this;
  }

  BGRAColor sRGBColor() const {
    MOZ_RELEASE_ASSERT(msRGB);
    MOZ_RELEASE_ASSERT(!mPremultiplied);
    return *this;
  }

  BGRAColor Premultiply() const {
    if (!mPremultiplied) {
      return BGRAColor(gfxPreMultiply(mBlue, mAlpha),
                       gfxPreMultiply(mGreen, mAlpha),
                       gfxPreMultiply(mRed, mAlpha), mAlpha, true);
    }
    return *this;
  }

  uint32_t AsPixel() const {
    if (!mPremultiplied) {
      return gfxPackedPixel(mAlpha, mRed, mGreen, mBlue);
    }
    return gfxPackedPixelNoPreMultiply(mAlpha, mRed, mGreen, mBlue);
  }

  uint8_t mBlue;
  uint8_t mGreen;
  uint8_t mRed;
  uint8_t mAlpha;
  bool mPremultiplied;
  bool msRGB;
};

enum TestCaseFlags {
  TEST_CASE_DEFAULT_FLAGS = 0,
  TEST_CASE_IS_FUZZY = 1 << 0,
  TEST_CASE_HAS_ERROR = 1 << 1,
  TEST_CASE_IS_TRANSPARENT = 1 << 2,
  TEST_CASE_IS_ANIMATED = 1 << 3,
  TEST_CASE_IGNORE_OUTPUT = 1 << 4,
  TEST_CASE_ASSUME_SRGB_OUTPUT = 1 << 5,
};

struct ImageTestCase {
  ImageTestCase(const char* aPath, const char* aMimeType, gfx::IntSize aSize,
                uint32_t aFlags = TEST_CASE_DEFAULT_FLAGS,
                uint32_t aFrameCount = 1)
      : mPath(aPath),
        mMimeType(aMimeType),
        mSize(aSize),
        mOutputSize(aSize),
        mFrameCount(aFrameCount),
        mFlags(aFlags),
        mSurfaceFlags(DefaultSurfaceFlags()),
        mColor(BGRAColor::Green()) {}

  ImageTestCase(const char* aPath, const char* aMimeType, gfx::IntSize aSize,
                gfx::IntSize aOutputSize,
                uint32_t aFlags = TEST_CASE_DEFAULT_FLAGS)
      : mPath(aPath),
        mMimeType(aMimeType),
        mSize(aSize),
        mOutputSize(aOutputSize),
        mFlags(aFlags),
        mSurfaceFlags(DefaultSurfaceFlags()),
        mColor(BGRAColor::Green()) {}

  ImageTestCase WithSurfaceFlags(SurfaceFlags aSurfaceFlags) const {
    ImageTestCase self = *this;
    self.mSurfaceFlags = aSurfaceFlags;
    return self;
  }

  ImageTestCase WithFlags(uint32_t aFlags) const {
    ImageTestCase self = *this;
    self.mFlags = aFlags;
    return self;
  }

  BGRAColor ChooseColor(const BGRAColor& aColor) const {
    // If we are forcing the output to be sRGB via the surface flag, or the
    // test case is marked as assuming sRGB (used when the image itself is not
    // explicitly tagged, and as a result, imagelib won't perform any color
    // conversion), we should use the sRGB presentation of the color.
    if ((mSurfaceFlags & SurfaceFlags::TO_SRGB_COLORSPACE) ||
        (mFlags & TEST_CASE_ASSUME_SRGB_OUTPUT)) {
      return aColor.sRGBColor();
    }
    return aColor.DeviceColor();
  }

  BGRAColor Color() const { return ChooseColor(mColor); }

  uint8_t Fuzz() const {
    // If we are using device space, there can easily be off by 1 channel errors
    // depending on the color profile and how the rounding went.
    if (mFlags & TEST_CASE_IS_FUZZY ||
        !(mSurfaceFlags & SurfaceFlags::TO_SRGB_COLORSPACE)) {
      return 1;
    }
    return 0;
  }

  const char* mPath;
  const char* mMimeType;
  gfx::IntSize mSize;
  gfx::IntSize mOutputSize;
  uint32_t mFrameCount = 0;
  uint32_t mFlags;
  SurfaceFlags mSurfaceFlags;
  BGRAColor mColor;
};

///////////////////////////////////////////////////////////////////////////////
// General Helpers
///////////////////////////////////////////////////////////////////////////////

/**
 * A RAII class that ensure that ImageLib services are available. Any tests that
 * require ImageLib to be initialized (for example, any test that uses the
 * SurfaceCache; see image::EnsureModuleInitialized() for the full list) can
 * use this class to ensure that ImageLib services are available. Failure to do
 * so can result in strange, non-deterministic failures.
 */
class AutoInitializeImageLib {
 public:
  AutoInitializeImageLib();
};

/**
 * A test fixture class used for benchmark tests. It preloads the image data
 * from disk to avoid including that in the timing.
 */
class ImageBenchmarkBase : public ::testing::Test {
 protected:
  ImageBenchmarkBase(const ImageTestCase& aTestCase) : mTestCase(aTestCase) {}

  void SetUp() override;
  void TearDown() override;

  AutoInitializeImageLib mInit;
  ImageTestCase mTestCase;
  RefPtr<SourceBuffer> mSourceBuffer;
};

/// Spins on the main thread to process any pending events.
void SpinPendingEvents();

/// Loads a file from the current directory. @return an nsIInputStream for it.
already_AddRefed<nsIInputStream> LoadFile(const char* aRelativePath);

/**
 * @returns true if every pixel of @aSurface is @aColor.
 *
 * If @aFuzz is nonzero, a tolerance of @aFuzz is allowed in each color
 * component. This may be necessary for tests that involve JPEG images or
 * downscaling.
 */
bool IsSolidColor(gfx::SourceSurface* aSurface, BGRAColor aColor,
                  uint8_t aFuzz = 0);

/**
 * @returns true if every pixel in the range of rows specified by @aStartRow and
 * @aRowCount of @aSurface is @aColor.
 *
 * If @aFuzz is nonzero, a tolerance of @aFuzz is allowed in each color
 * component. This may be necessary for tests that involve JPEG images or
 * downscaling.
 */
bool RowsAreSolidColor(gfx::SourceSurface* aSurface, int32_t aStartRow,
                       int32_t aRowCount, BGRAColor aColor, uint8_t aFuzz = 0);

/**
 * @returns true if every pixel in the rect specified by @aRect is @aColor.
 *
 * If @aFuzz is nonzero, a tolerance of @aFuzz is allowed in each color
 * component. This may be necessary for tests that involve JPEG images or
 * downscaling.
 */
bool RectIsSolidColor(gfx::SourceSurface* aSurface, const gfx::IntRect& aRect,
                      BGRAColor aColor, uint8_t aFuzz = 0);

/**
 * @returns true if the pixels in @aRow of @aSurface match the pixels given in
 * @aPixels.
 */
bool RowHasPixels(gfx::SourceSurface* aSurface, int32_t aRow,
                  const std::vector<BGRAColor>& aPixels);

// ExpectNoResume is an IResumable implementation for use by tests that expect
// Resume() to never get called.
class ExpectNoResume final : public IResumable {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ExpectNoResume, override)

  void Resume() override { FAIL() << "Resume() should not get called"; }

 private:
  ~ExpectNoResume() override {}
};

// CountResumes is an IResumable implementation for use by tests that expect
// Resume() to get called a certain number of times.
class CountResumes : public IResumable {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CountResumes, override)

  CountResumes() : mCount(0) {}

  void Resume() override { mCount++; }
  uint32_t Count() const { return mCount; }

 private:
  ~CountResumes() override {}

  uint32_t mCount;
};

///////////////////////////////////////////////////////////////////////////////
// SurfacePipe Helpers
///////////////////////////////////////////////////////////////////////////////

/**
 * Creates a decoder with no data associated with, suitable for testing code
 * that requires a decoder to initialize or to allocate surfaces but doesn't
 * actually need the decoder to do any decoding.
 *
 * XXX(seth): We only need this because SurfaceSink defer to the decoder for
 * surface allocation. Once all decoders use SurfacePipe we won't need to do
 * that anymore and we can remove this function.
 */
already_AddRefed<Decoder> CreateTrivialDecoder();

/**
 * Creates a pipeline of SurfaceFilters from a list of Config structs and passes
 * it to the provided lambda @aFunc. Assertions that the pipeline is constructly
 * correctly and cleanup of any allocated surfaces is handled automatically.
 *
 * @param aDecoder The decoder to use for allocating surfaces.
 * @param aFunc The lambda function to pass the filter pipeline to.
 * @param aConfigs The configuration for the pipeline.
 */
template <typename Func, typename... Configs>
void WithFilterPipeline(Decoder* aDecoder, Func aFunc, bool aFinish,
                        const Configs&... aConfigs) {
  auto pipe =
      std::make_unique<typename detail::FilterPipeline<Configs...>::Type>();
  nsresult rv = pipe->Configure(aConfigs...);
  ASSERT_NS_SUCCEEDED(rv);

  aFunc(aDecoder, pipe.get());

  if (aFinish) {
    RawAccessFrameRef currentFrame = aDecoder->GetCurrentFrameRef();
    if (currentFrame) {
      currentFrame->Finish();
    }
  }
}

template <typename Func, typename... Configs>
void WithFilterPipeline(Decoder* aDecoder, Func aFunc,
                        const Configs&... aConfigs) {
  WithFilterPipeline(aDecoder, aFunc, true, aConfigs...);
}

/**
 * Creates a pipeline of SurfaceFilters from a list of Config structs and
 * asserts that configuring it fails. Cleanup of any allocated surfaces is
 * handled automatically.
 *
 * @param aDecoder The decoder to use for allocating surfaces.
 * @param aConfigs The configuration for the pipeline.
 */
template <typename... Configs>
void AssertConfiguringPipelineFails(Decoder* aDecoder,
                                    const Configs&... aConfigs) {
  auto pipe =
      std::make_unique<typename detail::FilterPipeline<Configs...>::Type>();
  nsresult rv = pipe->Configure(aConfigs...);

  // Callers expect configuring the pipeline to fail.
  ASSERT_NS_FAILED(rv);

  RawAccessFrameRef currentFrame = aDecoder->GetCurrentFrameRef();
  if (currentFrame) {
    currentFrame->Finish();
  }
}

/**
 * Asserts that the provided filter pipeline is in the correct final state,
 * which is to say, the entire surface has been written to (IsSurfaceFinished()
 * returns true) and the invalid rects are as expected.
 *
 * @param aFilter The filter pipeline to check.
 * @param aInputSpaceRect The expect invalid rect, in input space.
 * @param aoutputSpaceRect The expect invalid rect, in output space.
 */
void AssertCorrectPipelineFinalState(SurfaceFilter* aFilter,
                                     const gfx::IntRect& aInputSpaceRect,
                                     const gfx::IntRect& aOutputSpaceRect);

/**
 * Checks a generated image for correctness. Reports any unexpected deviation
 * from the expected image as GTest failures.
 *
 * @param aDecoder The decoder which contains the image. The decoder's current
 *                 frame will be checked.
 * @param aRect The region in the space of the output surface that the filter
 *              pipeline will actually write to. It's expected that pixels in
 *              this region are green, while pixels outside this region are
 *              transparent.
 * @param aFuzz The amount of fuzz to use in pixel comparisons.
 */
void CheckGeneratedImage(Decoder* aDecoder, const gfx::IntRect& aRect,
                         uint8_t aFuzz = 0);

/**
 * Checks a generated surface for correctness. Reports any unexpected deviation
 * from the expected image as GTest failures.
 *
 * @param aSurface The surface to check.
 * @param aRect The region in the space of the output surface that the filter
 *              pipeline will actually write to.
 * @param aInnerColor Check that pixels inside of aRect are this color.
 * @param aOuterColor Check that pixels outside of aRect are this color.
 * @param aFuzz The amount of fuzz to use in pixel comparisons.
 */
void CheckGeneratedSurface(gfx::SourceSurface* aSurface,
                           const gfx::IntRect& aRect,
                           const BGRAColor& aInnerColor,
                           const BGRAColor& aOuterColor, uint8_t aFuzz = 0);

/**
 * Tests the result of calling WritePixels() using the provided SurfaceFilter
 * pipeline. The pipeline must be a normal (i.e., non-paletted) pipeline.
 *
 * The arguments are specified in the an order intended to minimize the number
 * of arguments that most test cases need to pass.
 *
 * @param aDecoder The decoder whose current frame will be written to.
 * @param aFilter The SurfaceFilter pipeline to use.
 * @param aOutputRect The region in the space of the output surface that will be
 *                    invalidated by the filter pipeline. Defaults to
 *                    (0, 0, 100, 100).
 * @param aInputRect The region in the space of the input image that will be
 *                   invalidated by the filter pipeline. Defaults to
 *                   (0, 0, 100, 100).
 * @param aInputWriteRect The region in the space of the input image that the
 *                        filter pipeline will allow writes to. Note the
 *                        difference from @aInputRect: @aInputRect is the actual
 *                        region invalidated, while @aInputWriteRect is the
 *                        region that is written to. These can differ in cases
 *                        where the input is not clipped to the size of the
 * image. Defaults to the entire input rect.
 * @param aOutputWriteRect The region in the space of the output surface that
 *                         the filter pipeline will actually write to. It's
 *                         expected that pixels in this region are green, while
 *                         pixels outside this region are transparent. Defaults
 *                         to the entire output rect.
 */
void CheckWritePixels(Decoder* aDecoder, SurfaceFilter* aFilter,
                      const Maybe<gfx::IntRect>& aOutputRect = Nothing(),
                      const Maybe<gfx::IntRect>& aInputRect = Nothing(),
                      const Maybe<gfx::IntRect>& aInputWriteRect = Nothing(),
                      const Maybe<gfx::IntRect>& aOutputWriteRect = Nothing(),
                      uint8_t aFuzz = 0);

/**
 * Tests the result of calling WritePixels() using the provided SurfaceFilter
 * pipeline. Allows for control over the input color to write, and the expected
 * output color.
 * @see CheckWritePixels() for documentation of the arguments.
 */
void CheckTransformedWritePixels(
    Decoder* aDecoder, SurfaceFilter* aFilter, const BGRAColor& aInputColor,
    const BGRAColor& aOutputColor,
    const Maybe<gfx::IntRect>& aOutputRect = Nothing(),
    const Maybe<gfx::IntRect>& aInputRect = Nothing(),
    const Maybe<gfx::IntRect>& aInputWriteRect = Nothing(),
    const Maybe<gfx::IntRect>& aOutputWriteRect = Nothing(), uint8_t aFuzz = 0);

///////////////////////////////////////////////////////////////////////////////
// Decoder Helpers
///////////////////////////////////////////////////////////////////////////////

// Friend class of Decoder to access internals for tests.
class MOZ_STACK_CLASS DecoderTestHelper final {
 public:
  explicit DecoderTestHelper(Decoder* aDecoder) : mDecoder(aDecoder) {}

  void PostIsAnimated(FrameTimeout aTimeout) {
    mDecoder->PostIsAnimated(aTimeout);
  }

  void PostFrameStop(Opacity aOpacity) { mDecoder->PostFrameStop(aOpacity); }

 private:
  Decoder* mDecoder;
};

///////////////////////////////////////////////////////////////////////////////
// Test Data
///////////////////////////////////////////////////////////////////////////////

ImageTestCase GreenPNGTestCase();
ImageTestCase GreenGIFTestCase();
ImageTestCase GreenJPGTestCase();
ImageTestCase LosslessNonInterleavedJPGTestCase();
ImageTestCase TruncatedLosslessNonInterleavedJPGTestCase();
ImageTestCase GreenBMPTestCase();
ImageTestCase GreenICOTestCase();
ImageTestCase GreenIconTestCase();
ImageTestCase GreenWebPTestCase();
ImageTestCase GreenAVIFTestCase();

ImageTestCase NonzeroReservedAVIFTestCase();
ImageTestCase MultipleColrAVIFTestCase();
ImageTestCase Transparent10bit420AVIFTestCase();
ImageTestCase Transparent10bit422AVIFTestCase();
ImageTestCase Transparent10bit444AVIFTestCase();
ImageTestCase Transparent12bit420AVIFTestCase();
ImageTestCase Transparent12bit422AVIFTestCase();
ImageTestCase Transparent12bit444AVIFTestCase();
ImageTestCase Transparent8bit420AVIFTestCase();
ImageTestCase Transparent8bit422AVIFTestCase();
ImageTestCase Transparent8bit444AVIFTestCase();

ImageTestCase Gray8bitLimitedRangeBT601AVIFTestCase();
ImageTestCase Gray8bitLimitedRangeBT709AVIFTestCase();
ImageTestCase Gray8bitLimitedRangeBT2020AVIFTestCase();
ImageTestCase Gray8bitFullRangeBT601AVIFTestCase();
ImageTestCase Gray8bitFullRangeBT709AVIFTestCase();
ImageTestCase Gray8bitFullRangeBT2020AVIFTestCase();
ImageTestCase Gray10bitLimitedRangeBT601AVIFTestCase();
ImageTestCase Gray10bitLimitedRangeBT709AVIFTestCase();
ImageTestCase Gray10bitLimitedRangeBT2020AVIFTestCase();
ImageTestCase Gray10bitFullRangeBT601AVIFTestCase();
ImageTestCase Gray10bitFullRangeBT709AVIFTestCase();
ImageTestCase Gray10bitFullRangeBT2020AVIFTestCase();
ImageTestCase Gray12bitLimitedRangeBT601AVIFTestCase();
ImageTestCase Gray12bitLimitedRangeBT709AVIFTestCase();
ImageTestCase Gray12bitLimitedRangeBT2020AVIFTestCase();
ImageTestCase Gray12bitFullRangeBT601AVIFTestCase();
ImageTestCase Gray12bitFullRangeBT709AVIFTestCase();
ImageTestCase Gray12bitFullRangeBT2020AVIFTestCase();
ImageTestCase Gray8bitLimitedRangeGrayscaleAVIFTestCase();
ImageTestCase Gray8bitFullRangeGrayscaleAVIFTestCase();
ImageTestCase Gray10bitLimitedRangeGrayscaleAVIFTestCase();
ImageTestCase Gray10bitFullRangeGrayscaleAVIFTestCase();
ImageTestCase Gray12bitLimitedRangeGrayscaleAVIFTestCase();
ImageTestCase Gray12bitFullRangeGrayscaleAVIFTestCase();

ImageTestCase StackCheckAVIFTestCase();

ImageTestCase LargeWebPTestCase();
ImageTestCase GreenWebPIccSrgbTestCase();

ImageTestCase GreenFirstFrameAnimatedGIFTestCase();
ImageTestCase GreenFirstFrameAnimatedPNGTestCase();
ImageTestCase GreenFirstFrameAnimatedWebPTestCase();
ImageTestCase GreenFirstFrameAnimatedAVIFTestCase();
#ifdef MOZ_JXL
ImageTestCase GreenFirstFrameAnimatedJXLTestCase();
ImageTestCase LongAnimatedJXLTestCase();
#endif

ImageTestCase BlendAnimatedGIFTestCase();
ImageTestCase BlendAnimatedPNGTestCase();
ImageTestCase BlendAnimatedWebPTestCase();
ImageTestCase BlendAnimatedAVIFTestCase();
#ifdef MOZ_JXL
ImageTestCase BlendAnimatedJXLTestCase();
#endif

ImageTestCase CorruptTestCase();
ImageTestCase CorruptBMPWithTruncatedHeader();
ImageTestCase CorruptICOWithBadBMPWidthTestCase();
ImageTestCase CorruptICOWithBadBMPHeightTestCase();
ImageTestCase CorruptICOWithBadBppTestCase();

ImageTestCase TransparentPNGTestCase();
ImageTestCase TransparentGIFTestCase();
ImageTestCase TransparentWebPTestCase();
ImageTestCase TransparentNoAlphaHeaderWebPTestCase();
ImageTestCase FirstFramePaddingGIFTestCase();
ImageTestCase TransparentIfWithinICOBMPTestCase(TestCaseFlags aFlags);
ImageTestCase NoFrameDelayGIFTestCase();
ImageTestCase ExtraImageSubBlocksAnimatedGIFTestCase();

ImageTestCase TransparentBMPWhenBMPAlphaEnabledTestCase();
ImageTestCase RLE4BMPTestCase();
ImageTestCase RLE8BMPTestCase();

ImageTestCase DownscaledPNGTestCase();
ImageTestCase DownscaledGIFTestCase();
ImageTestCase DownscaledJPGTestCase();
ImageTestCase DownscaledBMPTestCase();
ImageTestCase DownscaledICOTestCase();
ImageTestCase DownscaledIconTestCase();
ImageTestCase DownscaledWebPTestCase();
ImageTestCase DownscaledLossyWebPTestCase();
ImageTestCase DownscaledTransparentICOWithANDMaskTestCase();

ImageTestCase TruncatedSmallGIFTestCase();

ImageTestCase LargeICOWithBMPTestCase();
ImageTestCase LargeICOWithPNGTestCase();
ImageTestCase GreenMultipleSizesICOTestCase();

ImageTestCase PerfGrayJPGTestCase();
ImageTestCase PerfCmykJPGTestCase();
ImageTestCase PerfYCbCrJPGTestCase();
ImageTestCase PerfRgbPNGTestCase();
ImageTestCase PerfRgbAlphaPNGTestCase();
ImageTestCase PerfGrayPNGTestCase();
ImageTestCase PerfGrayAlphaPNGTestCase();
ImageTestCase PerfRgbLosslessWebPTestCase();
ImageTestCase PerfRgbAlphaLosslessWebPTestCase();
ImageTestCase PerfRgbLossyWebPTestCase();
ImageTestCase PerfRgbAlphaLossyWebPTestCase();
ImageTestCase PerfRgbGIFTestCase();

ImageTestCase CorruptAVIFTestCase();
ImageTestCase DownscaledAVIFTestCase();
ImageTestCase LargeAVIFTestCase();
ImageTestCase MultiLayerAVIFTestCase();
ImageTestCase TransparentAVIFTestCase();

#ifdef MOZ_JXL
ImageTestCase GreenJXLTestCase();
ImageTestCase ProgressiveJXLTestCase();
ImageTestCase DownscaledJXLTestCase();
ImageTestCase LargeJXLTestCase();
ImageTestCase LargeJXLReferenceWebPTestCase();
ImageTestCase TransparentJXLTestCase();
ImageTestCase CorruptJXLTestCase();
ImageTestCase PerfRgbLosslessJXLTestCase();
ImageTestCase PerfRgbAlphaLosslessJXLTestCase();
ImageTestCase PerfRgbLossyJXLTestCase();
ImageTestCase PerfRgbAlphaLossyJXLTestCase();
ImageTestCase ProgressiveAlphaMultiGroupJXLTestCase();
#endif

ImageTestCase ExifResolutionTestCase();

ImageTestCase ExifOrientationDownscaleJPGTestCase();

RefPtr<Image> TestCaseToDecodedImage(const ImageTestCase&);

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_test_gtest_Common_h
