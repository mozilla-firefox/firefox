#include <array>
#include <cmath>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ImageContainer.h"
#include "YCbCrTestHelpers.h"
#include "YCbCrUtils.h"
#include "gtest/gtest.h"
#include "nsTArray.h"

using Color = std::tuple<uint8_t, uint8_t, uint8_t>;
using namespace mozilla;

const Color BLACK(0, 0, 0);
const Color BLUE(0, 0, 255);
const Color GREEN(0, 255, 0);
const Color CYAN(0, 255, 255);
const Color RED(255, 0, 0);
const Color MAGENTA(255, 0, 255);
const Color YELLOW(255, 255, 0);
const Color WHITE(255, 255, 255);
const Color CHOCOLATE(210, 105, 30);
const Color PERU(205, 133, 63);
const Color ROSYBROWN(188, 143, 143);
const Color STEELBLUE(70, 130, 180);
const std::array<Color, 12> COLOR_LIST = {
    BLACK,  BLUE,  GREEN,     CYAN, RED,       MAGENTA,
    YELLOW, WHITE, CHOCOLATE, PERU, ROSYBROWN, STEELBLUE};

Color RGB2YUV(const Color& aRGBColor) {
  const uint8_t& r = std::get<0>(aRGBColor);
  const uint8_t& g = std::get<1>(aRGBColor);
  const uint8_t& b = std::get<2>(aRGBColor);

  const double y = r * 0.299 + g * 0.587 + b * 0.114;
  const double u = r * -0.168736 + g * -0.331264 + b * 0.5 + 128;
  const double v = r * 0.5 + g * -0.418688 + b * -0.081312 + 128;

  return Color(round(y), round(u), round(v));
}

already_AddRefed<layers::PlanarYCbCrImage> CreateI420Image(
    const Color& aRGBColor, const gfx::YUVColorSpace& aColorSpace,
    const gfx::IntSize& aSize, Maybe<uint8_t> aAlphaValue = Nothing()) {
  const int32_t halfWidth = CeilingOfHalf(aSize.width);
  const int32_t halfHeight = CeilingOfHalf(aSize.height);

  const size_t yPlaneSize = aSize.width * aSize.height;
  const size_t uPlaneSize = halfWidth * halfHeight;
  const size_t vPlaneSize = uPlaneSize;
  const size_t aPlaneSize = aAlphaValue.isSome() ? yPlaneSize : 0;
  const size_t imageSize = yPlaneSize + uPlaneSize + vPlaneSize + aPlaneSize;

  const Color yuvColor = RGB2YUV(aRGBColor);
  const uint8_t& yColor = std::get<0>(yuvColor);
  const uint8_t& uColor = std::get<1>(yuvColor);
  const uint8_t& vColor = std::get<2>(yuvColor);

  UniquePtr<uint8_t[]> buffer(new uint8_t[imageSize]);

  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect({0, 0}, aSize);

  // Y plane.
  uint8_t* yChannel = buffer.get();
  memset(yChannel, yColor, yPlaneSize);
  data.mYChannel = yChannel;
  data.mYStride = aSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* uChannel = yChannel + yPlaneSize;
  memset(uChannel, uColor, uPlaneSize);
  data.mCbChannel = uChannel;
  data.mCbSkip = 0;

  // Cr plane (aka V).
  uint8_t* vChannel = uChannel + uPlaneSize;
  memset(vChannel, vColor, vPlaneSize);
  data.mCrChannel = vChannel;
  data.mCrSkip = 0;

  // CrCb plane vectors.
  data.mCbCrStride = halfWidth;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  // Alpha plane.
  if (aPlaneSize) {
    uint8_t* aChannel = vChannel + vPlaneSize;
    memset(aChannel, *aAlphaValue, aPlaneSize);
    data.mAlpha.emplace();
    data.mAlpha->mChannel = aChannel;
    data.mAlpha->mSize = aSize;
  }

  data.mYUVColorSpace = aColorSpace;

  RefPtr<layers::PlanarYCbCrImage> image =
      new layers::RecyclingPlanarYCbCrImage(new layers::BufferRecycleBin());
  image->CopyData(data);
  return image.forget();
}

already_AddRefed<layers::PlanarYCbCrImage> CreateI444Image(
    const Color& aRGBColor, const gfx::YUVColorSpace& aColorSpace,
    const gfx::IntSize& aSize, Maybe<uint8_t> aAlphaValue = Nothing()) {
  const size_t yPlaneSize = aSize.width * aSize.height;
  const size_t uPlaneSize = yPlaneSize;
  const size_t vPlaneSize = yPlaneSize;
  const size_t aPlaneSize = aAlphaValue.isSome() ? yPlaneSize : 0;
  const size_t imageSize = yPlaneSize + uPlaneSize + vPlaneSize + aPlaneSize;

  const Color yuvColor = RGB2YUV(aRGBColor);
  const uint8_t& yColor = std::get<0>(yuvColor);
  const uint8_t& uColor = std::get<1>(yuvColor);
  const uint8_t& vColor = std::get<2>(yuvColor);

  UniquePtr<uint8_t[]> buffer(new uint8_t[imageSize]);

  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect({0, 0}, aSize);

  // Y plane.
  uint8_t* yChannel = buffer.get();
  memset(yChannel, yColor, yPlaneSize);
  data.mYChannel = yChannel;
  data.mYStride = aSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* uChannel = yChannel + yPlaneSize;
  memset(uChannel, uColor, uPlaneSize);
  data.mCbChannel = uChannel;
  data.mCbSkip = 0;

  // Cr plane (aka V).
  uint8_t* vChannel = uChannel + uPlaneSize;
  memset(vChannel, vColor, vPlaneSize);
  data.mCrChannel = vChannel;
  data.mCrSkip = 0;

  // CrCb plane vectors.
  data.mCbCrStride = data.mYStride;
  data.mChromaSubsampling = gfx::ChromaSubsampling::FULL;

  // Alpha plane.
  if (aPlaneSize) {
    uint8_t* aChannel = vChannel + vPlaneSize;
    memset(aChannel, *aAlphaValue, aPlaneSize);
    data.mAlpha.emplace();
    data.mAlpha->mChannel = aChannel;
    data.mAlpha->mSize = aSize;
  }

  data.mYUVColorSpace = aColorSpace;

  RefPtr<layers::PlanarYCbCrImage> image =
      new layers::RecyclingPlanarYCbCrImage(new layers::BufferRecycleBin());
  image->CopyData(data);
  return image.forget();
}

void IsColorEqual(uint8_t* aBGRX, uint8_t* aRGBX, size_t aSize) {
  ASSERT_EQ(aSize % 4, (size_t)0);
  for (size_t i = 0; i < aSize; i += 4) {
    ASSERT_EQ(aBGRX[i + 2], aRGBX[i]);      // R
    ASSERT_EQ(aBGRX[i + 1], aRGBX[i + 1]);  // G
    ASSERT_EQ(aBGRX[i], aRGBX[i + 2]);      // B
    ASSERT_EQ(aBGRX[i + 3], aRGBX[i + 3]);  // X or A
  }
}

uint32_t Hash(const Color& aColor) {
  const uint8_t& r = std::get<0>(aColor);
  const uint8_t& g = std::get<1>(aColor);
  const uint8_t& b = std::get<2>(aColor);
  return r << 16 | g << 8 | b;
}

std::unordered_map<uint32_t, std::array<Color, 3>> GetExpectedConvertedRGB() {
  static std::unordered_map<uint32_t, std::array<Color, 3>> map;
  map.emplace(Hash(BLACK), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(0, 0, 0),
                                                // gfx::YUVColorSpace::BT709
                                                Color(0, 0, 0),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(0, 0, 0)});
  map.emplace(Hash(BLUE), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(0, 82, 0),
                                               // gfx::YUVColorSpace::BT709
                                               Color(0, 54, 0),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(0, 53, 0)});
  map.emplace(Hash(GREEN), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(0, 255, 0),
                                                // gfx::YUVColorSpace::BT709
                                                Color(0, 231, 0),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(0, 242, 0)});
  map.emplace(Hash(CYAN), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(0, 255, 255),
                                               // gfx::YUVColorSpace::BT709
                                               Color(0, 248, 255),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(0, 255, 255)});
  map.emplace(Hash(RED), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                              Color(0, 191, 0),
                                              // gfx::YUVColorSpace::BT709
                                              Color(0, 147, 0),
                                              // gfx::YUVColorSpace::BT2020
                                              Color(0, 162, 0)});
  map.emplace(Hash(MAGENTA), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                  Color(255, 0, 255),
                                                  // gfx::YUVColorSpace::BT709
                                                  Color(255, 28, 255),
                                                  // gfx::YUVColorSpace::BT2020
                                                  Color(255, 18, 255)});
  map.emplace(Hash(YELLOW), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                 Color(255, 255, 0),
                                                 // gfx::YUVColorSpace::BT709
                                                 Color(255, 255, 0),
                                                 // gfx::YUVColorSpace::BT2020
                                                 Color(255, 255, 0)});
  map.emplace(Hash(WHITE), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(255, 255, 255),
                                                // gfx::YUVColorSpace::BT709
                                                Color(255, 255, 255),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(255, 255, 255)});
  map.emplace(Hash(CHOCOLATE),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(224, 104, 20),
                                   // gfx::YUVColorSpace::BT709
                                   Color(236, 111, 20),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(229, 102, 20)});
  map.emplace(Hash(PERU), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(219, 137, 58),
                                               // gfx::YUVColorSpace::BT709
                                               Color(228, 140, 58),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(223, 134, 59)});
  map.emplace(Hash(ROSYBROWN),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(200, 147, 147),
                                   // gfx::YUVColorSpace::BT709
                                   Color(204, 152, 147),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(201, 149, 147)});
  map.emplace(Hash(STEELBLUE),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(65, 133, 189),
                                   // gfx::YUVColorSpace::BT709
                                   Color(58, 129, 189),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(62, 135, 189)});
  return map;
}

void IsColorMatched(const Color& aColor, uint8_t* aRGBX, size_t aSize,
                    Maybe<uint8_t> aAlphaValue = Nothing()) {
  const uint8_t& r = std::get<0>(aColor);
  const uint8_t& g = std::get<1>(aColor);
  const uint8_t& b = std::get<2>(aColor);
  for (size_t i = 0; i < aSize; i += 4) {
    ASSERT_EQ(r, aRGBX[i]);      // R
    ASSERT_EQ(g, aRGBX[i + 1]);  // G
    ASSERT_EQ(b, aRGBX[i + 2]);  // B
    if (aAlphaValue) {
      ASSERT_EQ(*aAlphaValue, aRGBX[i + 3]);  // A
    }
  }
}

TEST(YCbCrUtils, RejectsEmptyPictureRects)
{
  const gfx::IntRect pictureRects[] = {gfx::IntRect(0, 0, 0, 2),
                                       gfx::IntRect(0, 0, 2, 0)};
  const gfx::ColorDepth colorDepths[] = {
      gfx::ColorDepth::COLOR_8, gfx::ColorDepth::COLOR_10,
      gfx::ColorDepth::COLOR_12, gfx::ColorDepth::COLOR_16};
  uint16_t y[4] = {};
  uint16_t cb[4] = {};
  uint16_t cr[4] = {};
  std::array<uint8_t, 16> output;
  output.fill(0xA5);
  const auto unchanged = output;

  layers::PlanarYCbCrData data;
  data.mYChannel = reinterpret_cast<uint8_t*>(y);
  data.mCbChannel = reinterpret_cast<uint8_t*>(cb);
  data.mCrChannel = reinterpret_cast<uint8_t*>(cr);
  data.mYStride = data.mCbCrStride = 4;

  for (auto depth : colorDepths) {
    data.mColorDepth = depth;
    for (const auto& rect : pictureRects) {
      data.mPictureRect = rect;
      EXPECT_EQ(gfx::ConvertYCbCrToRGB(data, gfx::SurfaceFormat::B8G8R8X8,
                                       gfx::IntSize(2, 2), output.data(), 8),
                NS_ERROR_INVALID_ARG);
      EXPECT_EQ(output, unchanged);
      EXPECT_EQ(gfx::ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::B8G8R8X8,
                                         output.data(), 8, nullptr),
                NS_ERROR_INVALID_ARG);
      EXPECT_EQ(output, unchanged);
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8X8);
  const size_t bufferSize = stride * imgSize.Height();

  const std::array<gfx::YUVColorSpace, 3> colorSpaces{
      gfx::YUVColorSpace::BT601, gfx::YUVColorSpace::BT709,
      gfx::YUVColorSpace::BT2020};

  std::unordered_map<uint32_t, std::array<Color, 3>> expectations =
      GetExpectedConvertedRGB();

  for (const Color& color : COLOR_LIST) {
    const std::array<Color, 3>& expectedColors = expectations[Hash(color)];
    for (const gfx::YUVColorSpace& colorSpace : colorSpaces) {
      RefPtr<layers::PlanarYCbCrImage> img =
          CreateI420Image(color, colorSpace, imgSize);

      auto BGRX = std::make_unique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8X8,
                          BGRX.get(), stride, nullptr);

      auto RGBX = std::make_unique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8X8,
                          RGBX.get(), stride, nullptr);

      IsColorEqual(BGRX.get(), RGBX.get(), bufferSize);

      Color expectation = expectedColors[static_cast<size_t>(colorSpace)];
      IsColorMatched(expectation, RGBX.get(), bufferSize);
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32WithAlpha)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8A8);
  const size_t bufferSize = stride * imgSize.Height();

  const std::array<gfx::YUVColorSpace, 3> colorSpaces{
      gfx::YUVColorSpace::BT601, gfx::YUVColorSpace::BT709,
      gfx::YUVColorSpace::BT2020};

  std::unordered_map<uint32_t, std::array<Color, 3>> expectations =
      GetExpectedConvertedRGB();

  for (const Color& color : COLOR_LIST) {
    const std::array<Color, 3>& expectedColors = expectations[Hash(color)];
    for (const gfx::YUVColorSpace& colorSpace : colorSpaces) {
      Maybe<uint8_t> alpha = Some(128);
      RefPtr<layers::PlanarYCbCrImage> img =
          CreateI420Image(color, colorSpace, imgSize, alpha);

      auto BGRA = std::make_unique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8A8,
                          BGRA.get(), stride, nullptr);

      auto RGBA = std::make_unique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8A8,
                          RGBA.get(), stride, nullptr);

      IsColorEqual(BGRA.get(), RGBA.get(), bufferSize);

      Color expectation = expectedColors[static_cast<size_t>(colorSpace)];
      IsColorMatched(expectation, RGBA.get(), bufferSize, alpha);
    }
  }
}

// Converts I420A frames whose alpha samples are all distinct, with the picture
// rect at several origins and right edges and with tight and padded output
// rows, and checks that each output pixel's alpha comes from the picture
// position. aDepth > 8 bits uses 16-bit samples holding the 8-bit value shifted
// up, so the expected 8-bit alpha is exact.
template <typename Sample>
static void RunAlphaPicOffsetTest(gfx::ColorDepth aDepth) {
  // High-bit-depth planes are narrowed to 8 bits with strides rounded up to a
  // multiple of 32. The planes are wider than 32 samples, so a picture that
  // ends within the first 32 columns gets a narrowed luma stride smaller than
  // the whole alpha plane's.
  constexpr int32_t kNarrowedRowAlignment = 32;
  constexpr int32_t kWidth = kNarrowedRowAlignment + 8;
  constexpr int32_t kHeight = 4;
  // Rows extend past kWidth, so that the planes are read by stride. The alpha
  // plane has no stride of its own and uses the luma stride.
  constexpr int32_t kYStride = kWidth + 8;
  constexpr int32_t kCbCrStride = kYStride / 2;
  constexpr int32_t kCbCrHeight = kHeight / 2;
  // Mid-range luma and neutral chroma, so that only alpha varies.
  constexpr int32_t kMidSample = 128;
  // The alpha at (col, row) is kMinAlpha + row * kWidth + col: it counts up
  // in raster order, so it is distinct for every pixel, and it is never 0, the
  // value of an unwritten output.
  constexpr int32_t kMinAlpha = 1;
  static_assert(kMinAlpha + kWidth * kHeight - 1 <= UINT8_MAX);
  // Alpha is the last byte of an R8G8B8A8 or B8G8R8A8 pixel.
  constexpr int32_t kAlphaByte = 3;
  const int32_t shift = gfx::BitDepthForColorDepth(aDepth) -
                        gfx::BitDepthForColorDepth(gfx::ColorDepth::COLOR_8);

  std::array<Sample, kYStride * kHeight> y;
  y.fill(static_cast<Sample>(kMidSample << shift));
  std::array<Sample, kCbCrStride * kCbCrHeight> cb;
  cb.fill(static_cast<Sample>(kMidSample << shift));
  std::array<Sample, kCbCrStride * kCbCrHeight> cr;
  cr.fill(static_cast<Sample>(kMidSample << shift));
  std::array<uint8_t, kYStride * kHeight> expectedAlpha{};
  std::array<Sample, kYStride * kHeight> alpha{};
  for (int32_t row = 0; row < kHeight; ++row) {
    for (int32_t col = 0; col < kWidth; ++col) {
      const int32_t i = row * kYStride + col;
      expectedAlpha[i] = kMinAlpha + row * kWidth + col;
      alpha[i] = static_cast<Sample>(expectedAlpha[i] << shift);
    }
  }

  layers::PlanarYCbCrData data;
  data.mYChannel = reinterpret_cast<uint8_t*>(y.data());
  data.mYStride = kYStride * sizeof(Sample);
  data.mCbChannel = reinterpret_cast<uint8_t*>(cb.data());
  data.mCrChannel = reinterpret_cast<uint8_t*>(cr.data());
  data.mCbCrStride = kCbCrStride * sizeof(Sample);
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  data.mColorDepth = aDepth;
  data.mAlpha.emplace();
  data.mAlpha->mChannel = reinterpret_cast<uint8_t*>(alpha.data());
  data.mAlpha->mSize = gfx::IntSize(kWidth, kHeight);
  data.mAlpha->mDepth = aDepth;

  // The whole planes, pictures from an odd origin to their right or bottom
  // edge, and pictures that end before the right edge: one just past the first
  // kNarrowedRowAlignment columns and two within them.
  constexpr int32_t kEndPastAlignment = kNarrowedRowAlignment + 1;
  constexpr int32_t kEndWithinAlignment = kNarrowedRowAlignment - 1;
  const gfx::IntRect pictureRects[] = {
      {0, 0, kWidth, kHeight},
      {1, 0, kWidth - 1, kHeight},
      {0, 1, kWidth, kHeight - 1},
      {3, 1, kEndPastAlignment - 3, kHeight - 1},
      {2, 2, 4, kHeight - 2},
      {1, 1, kEndWithinAlignment - 1, kHeight - 2}};
  const gfx::SurfaceFormat formats[] = {gfx::SurfaceFormat::R8G8B8A8,
                                        gfx::SurfaceFormat::B8G8R8A8};
  const int32_t rowPaddings[] = {0, 8};
  for (const gfx::IntRect& rect : pictureRects) {
    ASSERT_TRUE(gfx::IntRect(0, 0, kWidth, kHeight).Contains(rect))
    << rect;
    data.mPictureRect = rect;
    for (const int32_t padding : rowPaddings) {
      for (const gfx::SurfaceFormat& format : formats) {
        const int32_t bpp = gfx::BytesPerPixel(format);
        const int32_t stride = rect.Width() * bpp + padding;
        auto output = std::make_unique<uint8_t[]>(stride * rect.Height());
        ASSERT_EQ(gfx::ConvertYCbCrToRGB32(data, format, output.get(), stride,
                                           nullptr),
                  NS_OK);
        for (int32_t row = 0; row < rect.Height(); ++row) {
          for (int32_t col = 0; col < rect.Width(); ++col) {
            EXPECT_EQ(output[row * stride + col * bpp + kAlphaByte],
                      expectedAlpha[(rect.y + row) * kYStride + rect.x + col])
                << static_cast<int>(gfx::BitDepthForColorDepth(aDepth))
                << "-bit, picture " << rect << ", stride " << stride
                << ", pixel (" << col << ", " << row << ")";
          }
        }
      }
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32WithAlphaPicOffset)
{
  RunAlphaPicOffsetTest<uint8_t>(gfx::ColorDepth::COLOR_8);
  RunAlphaPicOffsetTest<uint16_t>(gfx::ColorDepth::COLOR_10);
  RunAlphaPicOffsetTest<uint16_t>(gfx::ColorDepth::COLOR_12);
  RunAlphaPicOffsetTest<uint16_t>(gfx::ColorDepth::COLOR_16);
}

TEST(YCbCrUtils, ConvertYCbCrToRGBWithPictureOutsideAlpha)
{
  constexpr gfx::IntSize kYSize(8, 4);
  constexpr gfx::IntSize kCbCrSize(kYSize.width / 2, kYSize.height / 2);
  // Smaller than the luma plane on both axes, so that a picture can lie within
  // the luma plane but not within the alpha plane.
  constexpr gfx::IntSize kAlphaSize(4, 2);
  static_assert(kAlphaSize.width < kYSize.width &&
                kAlphaSize.height < kYSize.height);
  constexpr uint8_t kMidSample = 128;
  constexpr uint8_t kOpaque = 255;

  std::array<uint8_t, kYSize.width * kYSize.height> y;
  y.fill(kMidSample);
  std::array<uint8_t, kCbCrSize.width * kCbCrSize.height> cb;
  cb.fill(kMidSample);
  std::array<uint8_t, kCbCrSize.width * kCbCrSize.height> cr;
  cr.fill(kMidSample);
  // The alpha plane has no stride of its own and uses the luma stride.
  std::array<uint8_t, kYSize.width * kAlphaSize.height> alpha;
  alpha.fill(kOpaque);

  layers::PlanarYCbCrData data;
  data.mYChannel = y.data();
  data.mYStride = kYSize.width;
  data.mCbChannel = cb.data();
  data.mCrChannel = cr.data();
  data.mCbCrStride = kCbCrSize.width;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  data.mAlpha.emplace();
  data.mAlpha->mChannel = alpha.data();
  data.mAlpha->mSize = kAlphaSize;

  // Every format here has 4 bytes per pixel. The output holds any picture
  // within the luma plane, in rows of the picture's width.
  const int32_t bpp = gfx::BytesPerPixel(gfx::SurfaceFormat::R8G8B8A8);
  constexpr uint8_t kUnwritten = 0xA5;
  std::vector<uint8_t> output(kYSize.width * kYSize.height * bpp, kUnwritten);
  const auto unchanged = output;

  data.mPictureRect = gfx::IntRect(gfx::IntPoint(), kAlphaSize);
  ASSERT_EQ(
      gfx::ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::R8G8B8A8,
                               output.data(), kAlphaSize.width * bpp, nullptr),
      NS_OK);
  const uint8_t grey = output[0];
  ASSERT_NE(grey, kUnwritten);

  // The planes are uniform, so a conversion gives each pixel of the picture
  // the same grey, and with alpha the alpha plane's value, and writes nothing
  // past the picture.
  const auto expectConverted = [&](const gfx::IntSize& aSize,
                                   Maybe<uint8_t> aAlpha) {
    const int32_t end = aSize.width * aSize.height * bpp;
    for (int32_t i = 0; i < end; i += bpp) {
      EXPECT_EQ(output[i], grey) << "byte " << i;
      EXPECT_EQ(output[i + 1], grey) << "byte " << i + 1;
      EXPECT_EQ(output[i + 2], grey) << "byte " << i + 2;
      if (aAlpha) {
        EXPECT_EQ(output[i + 3], *aAlpha) << "byte " << i + 3;
      }
    }
    EXPECT_TRUE(std::equal(output.begin() + end, output.end(),
                           unchanged.begin() + end));
  };
  expectConverted(kAlphaSize, Some(kOpaque));

  // Pictures within the luma plane that extend past the alpha plane: wider and
  // taller, one row taller, one column to the right, and below it.
  const gfx::IntRect pictureRects[] = {
      {0, 0, kAlphaSize.width + 2, kAlphaSize.height + 2},
      {0, 0, kAlphaSize.width, kAlphaSize.height + 1},
      {1, 0, kAlphaSize.width, kAlphaSize.height},
      {2, kAlphaSize.height, 2, kYSize.height - kAlphaSize.height}};
  for (const gfx::IntRect& rect : pictureRects) {
    ASSERT_TRUE(gfx::IntRect(gfx::IntPoint(), kYSize).Contains(rect))
    << rect;
    ASSERT_FALSE(gfx::IntRect(gfx::IntPoint(), kAlphaSize).Contains(rect))
    << rect;
    data.mPictureRect = rect;
    const int32_t stride = rect.Width() * bpp;

    output = unchanged;
    EXPECT_EQ(gfx::ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::R8G8B8A8,
                                       output.data(), stride, nullptr),
              NS_ERROR_INVALID_ARG)
        << "picture " << rect;
    EXPECT_EQ(output, unchanged) << "picture " << rect;

    EXPECT_EQ(gfx::ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::R8G8B8X8,
                                       output.data(), stride, nullptr),
              NS_OK)
        << "picture " << rect;
    expectConverted(rect.Size(), Nothing());

    output = unchanged;
    EXPECT_EQ(gfx::ConvertYCbCrToRGB(data, gfx::SurfaceFormat::B8G8R8X8,
                                     rect.Size(), output.data(), stride),
              NS_OK)
        << "picture " << rect;
    expectConverted(rect.Size(), Nothing());
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32WithIdentityColorSpace)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8X8);
  const size_t bufferSize = stride * imgSize.Height();

  for (const Color& color : COLOR_LIST) {
    RefPtr<layers::PlanarYCbCrImage> img =
        CreateI444Image(color, gfx::YUVColorSpace::Identity, imgSize);

    auto BGRX = std::make_unique<uint8_t[]>(bufferSize);
    ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8X8,
                        BGRX.get(), stride, nullptr);

    auto RGBX = std::make_unique<uint8_t[]>(bufferSize);
    ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8X8,
                        RGBX.get(), stride, nullptr);

    IsColorEqual(BGRX.get(), RGBX.get(), bufferSize);

    const Color yuvColor = RGB2YUV(color);
    const uint8_t& y = std::get<0>(yuvColor);
    const uint8_t& u = std::get<1>(yuvColor);
    const uint8_t& v = std::get<2>(yuvColor);
    const Color expectation(v, y, u);
    IsColorMatched(expectation, RGBX.get(), bufferSize);
  }
}

// Fills a 4×4 Y plane and chroma planes for a frame whose luma is divided
// into four 2×2 blocks. aColors[blockRow][blockCol] gives the color for each
// block. Chroma plane dimensions depend on aSubsampling:
//   FULL (YV24): 4×4 chroma, each pixel maps 1:1 to a luma pixel.
//   HALF_WIDTH (YV16): 2×4 chroma, half-width but full height.
//   HALF_WIDTH_AND_HEIGHT (YV12): 2×2 chroma.
// Callers must provide at least 16 bytes for aUBuf/aVBuf to cover the FULL
// case; smaller subsamplings use only a prefix of that.
static void FillTwoByTwoFrame(const Color aColors[2][2],
                              gfx::ChromaSubsampling aSubsampling,
                              uint8_t* aYBuf, uint8_t* aUBuf, uint8_t* aVBuf) {
  // Give each luma pixel a unique Y by adding a small per-pixel offset based
  // on its position within its 2x2 chroma block: +0/+2/+4/+6 for
  // (top-left/top-right/bottom-left/bottom-right). This makes luma sampling
  // bugs detectable without meaningfully shifting the color.
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      uint8_t baseY = std::get<0>(RGB2YUV(aColors[r / 2][c / 2]));
      aYBuf[r * 4 + c] = baseY + (r % 2) * 4 + (c % 2) * 2;
    }
  }
  int chromaWidth = (aSubsampling == gfx::ChromaSubsampling::FULL) ? 4 : 2;
  int chromaHeight =
      (aSubsampling == gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT) ? 2 : 4;
  for (int chromaRow = 0; chromaRow < chromaHeight; chromaRow++) {
    int blockRow = chromaRow * 2 / chromaHeight;
    for (int chromaCol = 0; chromaCol < chromaWidth; chromaCol++) {
      int blockCol = chromaCol * 2 / chromaWidth;
      aUBuf[chromaRow * chromaWidth + chromaCol] =
          std::get<1>(RGB2YUV(aColors[blockRow][blockCol]));
      aVBuf[chromaRow * chromaWidth + chromaCol] =
          std::get<2>(RGB2YUV(aColors[blockRow][blockCol]));
    }
  }
}

// Fills and converts a 4×4 test frame, writing the result into aOutput.
// aStride is in bytes. See FillTwoByTwoFrame for the aColors layout.
static void ConvertTestFrame(const Color aColors[2][2],
                             gfx::ChromaSubsampling aSubsampling,
                             const gfx::IntRect& aPictureRect, uint8_t* aOutput,
                             int32_t aStride) {
  uint8_t yBuf[16], uBuf[16], vBuf[16];
  FillTwoByTwoFrame(aColors, aSubsampling, yBuf, uBuf, vBuf);
  layers::PlanarYCbCrData data;
  data.mYChannel = yBuf;
  data.mYStride = 4;
  data.mYSkip = 0;
  data.mCbChannel = uBuf;
  data.mCrChannel = vBuf;
  data.mCbCrStride = (aSubsampling == gfx::ChromaSubsampling::FULL) ? 4 : 2;
  data.mCbSkip = 0;
  data.mCrSkip = 0;
  data.mChromaSubsampling = aSubsampling;
  data.mYUVColorSpace = gfx::YUVColorSpace::BT709;
  data.mColorRange = gfx::ColorRange::LIMITED;
  data.mPictureRect = aPictureRect;
  ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::R8G8B8X8, aOutput, aStride,
                      nullptr);
}

// Tests for odd pic_x / pic_y offsets in YV12, YV16, and YV24.
//
// The 4x4 frame has four 2x2 chroma blocks with distinct mid-range colors.
// Within each block each luma pixel has a unique Y value (offset +0/+2/+4/+6),
// so both chroma and luma misalignment are detectable. The reference is a full
// even-aligned 4x4 conversion; each odd-crop output pixel is checked against
// its corresponding source position in that reference.
static const gfx::ChromaSubsampling kTestSubsamplings[] = {
    gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT,
    gfx::ChromaSubsampling::HALF_WIDTH, gfx::ChromaSubsampling::FULL};

static void RunOddPicTest(const Color aColors[2][2],
                          const gfx::IntRect& aRect) {
  const int32_t stride = aRect.Width() * 4;
  auto output = std::make_unique<uint8_t[]>(aRect.Height() * stride);
  auto exp = GetExpectedConvertedRGB();
  const size_t bt709 = static_cast<size_t>(gfx::YUVColorSpace::BT709);

  for (gfx::ChromaSubsampling subsampling : kTestSubsamplings) {
    // fullRef: even-aligned 4x4 reference (no odd-offset ambiguity).
    uint8_t fullRef[4 * 4 * 4];
    ConvertTestFrame(aColors, subsampling, gfx::IntRect(0, 0, 4, 4), fullRef,
                     4 * 4);

    // Sanity-check the reference: top-left of each 2x2 block (Y offset 0)
    // must match GetExpectedConvertedRGB; the other three pixels in the block
    // must be distinct but close (Y delta ≤6 → channel delta ~2-7).
    for (int br = 0; br < 2; br++) {
      for (int bc = 0; bc < 2; bc++) {
        uint8_t* base = fullRef + (br * 2) * 4 * 4 + (bc * 2) * 4;
        IsColorMatched(exp[Hash(aColors[br][bc])][bt709], base, 4);
        for (int dr = 0; dr < 2; dr++) {
          for (int dc = 0; dc < 2; dc++) {
            if (dr == 0 && dc == 0) continue;
            uint8_t* other =
                fullRef + (br * 2 + dr) * 4 * 4 + (bc * 2 + dc) * 4;
            for (int ch = 0; ch < 3; ch++) {
              ASSERT_NE(base[ch], other[ch]);
              ASSERT_NEAR(base[ch], other[ch], 10);
            }
          }
        }
      }
    }

    // output: the odd-crop conversion under test.
    ConvertTestFrame(aColors, subsampling, aRect, output.get(), stride);

    // Each output pixel must match its source position in the full reference.
    for (int row = 0; row < aRect.Height(); row++) {
      for (int col = 0; col < aRect.Width(); col++) {
        uint8_t* ref = fullRef + (aRect.y + row) * 4 * 4 + (aRect.x + col) * 4;
        Color expected(ref[0], ref[1], ref[2]);
        IsColorMatched(expected, output.get() + row * stride + col * 4, 4);
      }
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32OddPicOffset)
{
  const Color colors[2][2] = {{CHOCOLATE, PERU}, {ROSYBROWN, STEELBLUE}};
  RunOddPicTest(colors, gfx::IntRect(1, 1, 3, 3));  // both odd
  RunOddPicTest(colors, gfx::IntRect(1, 0, 3, 4));  // odd pic_x only
  RunOddPicTest(colors, gfx::IntRect(0, 1, 4, 3));  // odd pic_y only
}
