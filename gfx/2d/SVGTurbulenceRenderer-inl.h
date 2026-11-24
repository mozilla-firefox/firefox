/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_2D_SVGTURBULENCERENDERER_INL_H_
#define GFX_2D_SVGTURBULENCERENDERER_INL_H_

#include "2D.h"
#include "Filters.h"
#include "SIMD.h"

namespace mozilla {
namespace gfx {

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
class SVGTurbulenceRenderer {
 public:
  SVGTurbulenceRenderer(const Size& aBaseFrequency, int32_t aSeed,
                        int aNumOctaves, const Rect& aTileRect);

  already_AddRefed<DataSourceSurface> Render(const IntSize& aSize,
                                             const Point& aOffset) const;

 private:
  /* The turbulence calculation code is an adapted version of what
     appears in the SVG 1.1 specification:
         http://www.w3.org/TR/SVG11/filters.html#feTurbulence
  */

  struct StitchInfo {
    int32_t width;  // How much to subtract to wrap for stitching.
    int32_t height;
    int32_t wrapX;  // Minimum value to wrap.
    int32_t wrapY;
  };

  const static int sBSize = 0x100;
  const static int sBM = 0xff;
  void InitFromSeed(int32_t aSeed);
  void AdjustBaseFrequencyForStitch(const Rect& aTileRect);
  IntPoint AdjustForStitch(IntPoint aLatticePoint,
                           const StitchInfo& aStitchInfo) const;
  StitchInfo CreateStitchInfo(const Rect& aTileRect) const;
  f32x4_t Noise2(Point aVec, const StitchInfo& aStitchInfo) const;
  i32x4_t Turbulence(const Point& aPoint) const;
  Point EquivalentNonNegativeOffset(const Point& aOffset) const;

  Size mBaseFrequency;
  int32_t mNumOctaves;
  StitchInfo mStitchInfo;
  uint8_t mLatticeSelector[sBSize];
  f32x4_t mGradient[sBSize][2];
};

namespace {

struct RandomNumberSource {
  explicit RandomNumberSource(int32_t aSeed) : mLast(SetupSeed(aSeed)) {}
  int32_t Next() {
    mLast = Random(mLast);
    return mLast;
  }

 private:
  static const int32_t RAND_M = 2147483647; /* 2**31 - 1 */
  static const int32_t RAND_A = 16807;      /* 7**5; primitive root of m */
  static const int32_t RAND_Q = 127773;     /* m / a */
  static const int32_t RAND_R = 2836;       /* m % a */

  /* Produces results in the range [1, 2**31 - 2].
     Algorithm is: r = (a * r) mod m
     where a = 16807 and m = 2**31 - 1 = 2147483647
     See [Park & Miller], CACM vol. 31 no. 10 p. 1195, Oct. 1988
     To test: the algorithm should produce the result 1043618065
     as the 10,000th generated number if the original seed is 1.
  */

  static int32_t SetupSeed(int32_t aSeed) {
    if (aSeed <= 0) aSeed = -(aSeed % (RAND_M - 1)) + 1;
    if (aSeed > RAND_M - 1) aSeed = RAND_M - 1;
    return aSeed;
  }

  static int32_t Random(int32_t aSeed) {
    int32_t result = RAND_A * (aSeed % RAND_Q) - RAND_R * (aSeed / RAND_Q);
    if (result <= 0) result += RAND_M;
    return result;
  }

  int32_t mLast;
};

}  // unnamed namespace

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::
    SVGTurbulenceRenderer(const Size& aBaseFrequency, int32_t aSeed,
                          int aNumOctaves, const Rect& aTileRect)
    : mBaseFrequency(aBaseFrequency), mNumOctaves(aNumOctaves), mStitchInfo() {
  InitFromSeed(aSeed);
  if (Stitch) {
    AdjustBaseFrequencyForStitch(aTileRect);
    mStitchInfo = CreateStitchInfo(aTileRect);
  }
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
void SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t,
                           u8x16_t>::InitFromSeed(int32_t aSeed) {
  RandomNumberSource rand(aSeed);

  float gradient[4][sBSize][2];
  for (auto& g_k : gradient) {
    for (auto& g_k_i : g_k) {
      float a, b;
      do {
        a = float((rand.Next() % (sBSize + sBSize)) - sBSize) / sBSize;
        b = float((rand.Next() % (sBSize + sBSize)) - sBSize) / sBSize;
      } while (a == 0 && b == 0);
      float s = sqrt(a * a + b * b);
      g_k_i[0] = a / s;
      g_k_i[1] = b / s;
    }
  }

  for (int32_t i = 0; i < sBSize; i++) {
    mLatticeSelector[i] = i;
  }
  for (int32_t i1 = sBSize - 1; i1 > 0; i1--) {
    int32_t i2 = rand.Next() % sBSize;
    std::swap(mLatticeSelector[i1], mLatticeSelector[i2]);
  }

  for (int32_t i = 0; i < sBSize; i++) {
    // Contrary to the code in the spec, we build the first lattice selector
    // lookup into mGradient so that we don't need to do it again for every
    // pixel.
    // We also change the order of the gradient indexing so that we can process
    // all four color channels at the same time.
    uint8_t j = mLatticeSelector[i];
    mGradient[i][0] =
        simd::FromF32<f32x4_t>(gradient[2][j][0], gradient[1][j][0],
                               gradient[0][j][0], gradient[3][j][0]);
    mGradient[i][1] =
        simd::FromF32<f32x4_t>(gradient[2][j][1], gradient[1][j][1],
                               gradient[0][j][1], gradient[3][j][1]);
  }
}

// Adjust aFreq such that aLength * AdjustForLength(aFreq, aLength) is integer
// and as close to aLength * aFreq as possible.
static inline float AdjustForLength(float aFreq, float aLength) {
  float lowFreq = floor(aLength * aFreq) / aLength;
  float hiFreq = ceil(aLength * aFreq) / aLength;
  if (aFreq / lowFreq < hiFreq / aFreq) {
    return lowFreq;
  }
  return hiFreq;
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
void SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::
    AdjustBaseFrequencyForStitch(const Rect& aTileRect) {
  mBaseFrequency =
      Size(AdjustForLength(mBaseFrequency.width, aTileRect.Width()),
           AdjustForLength(mBaseFrequency.height, aTileRect.Height()));
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
typename SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t,
                               u8x16_t>::StitchInfo
SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t,
                      u8x16_t>::CreateStitchInfo(const Rect& aTileRect) const {
  StitchInfo stitch;
  stitch.width =
      int32_t(floorf(aTileRect.Width() * mBaseFrequency.width + 0.5f));
  stitch.height =
      int32_t(floorf(aTileRect.Height() * mBaseFrequency.height + 0.5f));
  stitch.wrapX = int32_t(aTileRect.X() * mBaseFrequency.width) + stitch.width;
  stitch.wrapY = int32_t(aTileRect.Y() * mBaseFrequency.height) + stitch.height;
  return stitch;
}

static MOZ_ALWAYS_INLINE Float SCurve(Float t) { return t * t * (3 - 2 * t); }

static MOZ_ALWAYS_INLINE Point SCurve(Point t) {
  return Point(SCurve(t.x), SCurve(t.y));
}

template <typename f32x4_t>
static MOZ_ALWAYS_INLINE f32x4_t BiMix(const f32x4_t& aa, const f32x4_t& ab,
                                       const f32x4_t& ba, const f32x4_t& bb,
                                       Point s) {
  return simd::MixF32(simd::MixF32(aa, ab, s.x), simd::MixF32(ba, bb, s.x),
                      s.y);
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
IntPoint
SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::AdjustForStitch(
    IntPoint aLatticePoint, const StitchInfo& aStitchInfo) const {
  if (Stitch) {
    if (aLatticePoint.x >= aStitchInfo.wrapX) {
      aLatticePoint.x -= aStitchInfo.width;
    }
    if (aLatticePoint.y >= aStitchInfo.wrapY) {
      aLatticePoint.y -= aStitchInfo.height;
    }
  }
  return aLatticePoint;
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
f32x4_t SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::Noise2(
    Point aVec, const StitchInfo& aStitchInfo) const {
  // aVec is guaranteed to be non-negative, so casting to int32_t always
  // rounds towards negative infinity.
  IntPoint topLeftLatticePoint(int32_t(aVec.x), int32_t(aVec.y));
  Point r = aVec - topLeftLatticePoint;  // fractional offset

  IntPoint b0 = AdjustForStitch(topLeftLatticePoint, aStitchInfo);
  IntPoint b1 = AdjustForStitch(b0 + IntPoint(1, 1), aStitchInfo);

  uint8_t i = mLatticeSelector[b0.x & sBM];
  uint8_t j = mLatticeSelector[b1.x & sBM];

  const f32x4_t* qua = mGradient[(i + b0.y) & sBM];
  const f32x4_t* qub = mGradient[(i + b1.y) & sBM];
  const f32x4_t* qva = mGradient[(j + b0.y) & sBM];
  const f32x4_t* qvb = mGradient[(j + b1.y) & sBM];

  return BiMix(simd::WSumF32(qua[0], qua[1], r.x, r.y),
               simd::WSumF32(qva[0], qva[1], r.x - 1.f, r.y),
               simd::WSumF32(qub[0], qub[1], r.x, r.y - 1.f),
               simd::WSumF32(qvb[0], qvb[1], r.x - 1.f, r.y - 1.f), SCurve(r));
}

template <typename f32x4_t, typename i32x4_t, typename u8x16_t>
static inline i32x4_t ColorToBGRA(f32x4_t aUnscaledUnpreFloat) {
  // Color is an unpremultiplied float vector where 1.0f means white. We will
  // convert it into an integer vector where 255 means white.
  f32x4_t alpha = simd::SplatF32<3>(aUnscaledUnpreFloat);
  f32x4_t scaledUnpreFloat =
      simd::MulF32(aUnscaledUnpreFloat, simd::FromF32<f32x4_t>(255));
  i32x4_t scaledUnpreInt = simd::F32ToI32(scaledUnpreFloat);

  // Multiply all channels with alpha.
  i32x4_t scaledPreInt = simd::F32ToI32(simd::MulF32(scaledUnpreFloat, alpha));

  // Use the premultiplied color channels and the unpremultiplied alpha channel.
  i32x4_t alphaMask = simd::From32<i32x4_t>(0, 0, 0, -1);
  return simd::Pick(alphaMask, scaledPreInt, scaledUnpreInt);
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
i32x4_t SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t,
                              u8x16_t>::Turbulence(const Point& aPoint) const {
  StitchInfo stitchInfo = mStitchInfo;
  f32x4_t sum = simd::FromF32<f32x4_t>(0);
  Point vec(aPoint.x * mBaseFrequency.width, aPoint.y * mBaseFrequency.height);
  f32x4_t ratio = simd::FromF32<f32x4_t>(1);

  for (int octave = 0; octave < mNumOctaves; octave++) {
    f32x4_t thisOctave = Noise2(vec, stitchInfo);
    if (Type == TURBULENCE_TYPE_TURBULENCE) {
      thisOctave = simd::AbsF32(thisOctave);
    }
    sum = simd::AddF32(sum, simd::DivF32(thisOctave, ratio));
    vec = vec * 2;
    ratio = simd::MulF32(ratio, simd::FromF32<f32x4_t>(2));

    if (Stitch) {
      stitchInfo.width *= 2;
      stitchInfo.wrapX *= 2;
      stitchInfo.height *= 2;
      stitchInfo.wrapY *= 2;
    }
  }

  if (Type == TURBULENCE_TYPE_FRACTAL_NOISE) {
    sum = simd::DivF32(simd::AddF32(sum, simd::FromF32<f32x4_t>(1)),
                       simd::FromF32<f32x4_t>(2));
  }
  return ColorToBGRA<f32x4_t, i32x4_t, u8x16_t>(sum);
}

static inline Float MakeNonNegative(Float aValue, Float aIncrementSize) {
  if (aIncrementSize == 0) {
    return 0;
  }
  if (aValue >= 0) {
    return aValue;
  }
  return aValue + ceilf(-aValue / aIncrementSize) * aIncrementSize;
}

static inline Float FiniteDivide(Float aValue, Float aDivisor) {
  if (aDivisor == 0) {
    return 0;
  }
  return aValue / aDivisor;
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
Point SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::
    EquivalentNonNegativeOffset(const Point& aOffset) const {
  Size basePeriod = Stitch ? Size(mStitchInfo.width, mStitchInfo.height)
                           : Size(sBSize, sBSize);
  Size repeatingSize(FiniteDivide(basePeriod.width, mBaseFrequency.width),
                     FiniteDivide(basePeriod.height, mBaseFrequency.height));
  return Point(MakeNonNegative(aOffset.x, repeatingSize.width),
               MakeNonNegative(aOffset.y, repeatingSize.height));
}

template <TurbulenceType Type, bool Stitch, typename f32x4_t, typename i32x4_t,
          typename u8x16_t>
already_AddRefed<DataSourceSurface>
SVGTurbulenceRenderer<Type, Stitch, f32x4_t, i32x4_t, u8x16_t>::Render(
    const IntSize& aSize, const Point& aOffset) const {
  RefPtr<DataSourceSurface> target =
      Factory::CreateDataSourceSurface(aSize, SurfaceFormat::B8G8R8A8);
  if (!target) {
    return nullptr;
  }

  DataSourceSurface::ScopedMap map(target, DataSourceSurface::READ_WRITE);
  uint8_t* targetData = map.GetData();
  uint32_t stride = map.GetStride();

  Point startOffset = EquivalentNonNegativeOffset(aOffset);

  for (int32_t y = 0; y < aSize.height; y++) {
    for (int32_t x = 0; x < aSize.width; x += 4) {
      int32_t targIndex = y * stride + x * 4;
      i32x4_t a = Turbulence(startOffset + Point(x, y));
      i32x4_t b = Turbulence(startOffset + Point(x + 1, y));
      i32x4_t c = Turbulence(startOffset + Point(x + 2, y));
      i32x4_t d = Turbulence(startOffset + Point(x + 3, y));
      u8x16_t result1234 = simd::PackAndSaturate32To8(a, b, c, d);
      simd::Store8(&targetData[targIndex], result1234);
    }
  }

  return target.forget();
}

}  // namespace gfx
}  // namespace mozilla

#endif  // GFX_2D_SVGTURBULENCERENDERER_INL_H_
