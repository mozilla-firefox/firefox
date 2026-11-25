/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "2D.h"
#include "Blur.h"
#include "BufferEdgePad.h"
#include "BufferUnrotate.h"
#include "FilterSupport.h"
#include "Logging.h"
#include "PathHelpers.h"
#include "SourceSurfaceRawData.h"
#include "Tools.h"

#ifdef USE_NEON
#  include "LuminanceNEON.h"
#  include "mozilla/arm.h"
#endif

namespace mozilla {
namespace gfx {

/**
 * Byte offsets of channels in a native packed gfxColor or cairo image surface.
 */
#ifdef IS_BIG_ENDIAN
#  define GFX_ARGB32_OFFSET_A 0
#  define GFX_ARGB32_OFFSET_R 1
#  define GFX_ARGB32_OFFSET_G 2
#  define GFX_ARGB32_OFFSET_B 3
#else
#  define GFX_ARGB32_OFFSET_A 3
#  define GFX_ARGB32_OFFSET_R 2
#  define GFX_ARGB32_OFFSET_G 1
#  define GFX_ARGB32_OFFSET_B 0
#endif

// c = n / 255
// c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)) * 255 + 0.5
static const uint8_t gsRGBToLinearRGBMap[256] = {
    0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,
    3,   3,   4,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   6,
    7,   7,   7,   8,   8,   8,   8,   9,   9,   9,   10,  10,  10,  11,  11,
    12,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  17,
    18,  18,  19,  19,  20,  20,  21,  22,  22,  23,  23,  24,  24,  25,  25,
    26,  27,  27,  28,  29,  29,  30,  30,  31,  32,  32,  33,  34,  35,  35,
    36,  37,  37,  38,  39,  40,  41,  41,  42,  43,  44,  45,  45,  46,  47,
    48,  49,  50,  51,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,
    62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  76,  77,
    78,  79,  80,  81,  82,  84,  85,  86,  87,  88,  90,  91,  92,  93,  95,
    96,  97,  99,  100, 101, 103, 104, 105, 107, 108, 109, 111, 112, 114, 115,
    116, 118, 119, 121, 122, 124, 125, 127, 128, 130, 131, 133, 134, 136, 138,
    139, 141, 142, 144, 146, 147, 149, 151, 152, 154, 156, 157, 159, 161, 163,
    164, 166, 168, 170, 171, 173, 175, 177, 179, 181, 183, 184, 186, 188, 190,
    192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220,
    222, 224, 226, 229, 231, 233, 235, 237, 239, 242, 244, 246, 248, 250, 253,
    255};

static void ComputesRGBLuminanceMask(const uint8_t* aSourceData,
                                     int32_t aSourceStride, uint8_t* aDestData,
                                     int32_t aDestStride, const IntSize& aSize,
                                     float aOpacity) {
#ifdef USE_NEON
  if (mozilla::supports_neon()) {
    ComputesRGBLuminanceMask_NEON(aSourceData, aSourceStride, aDestData,
                                  aDestStride, aSize, aOpacity);
    return;
  }
#endif

  int32_t redFactor = 55 * aOpacity;     // 255 * 0.2125 * opacity
  int32_t greenFactor = 183 * aOpacity;  // 255 * 0.7154 * opacity
  int32_t blueFactor = 18 * aOpacity;    // 255 * 0.0721
  int32_t sourceOffset = aSourceStride - 4 * aSize.width;
  const uint8_t* sourcePixel = aSourceData;
  int32_t destOffset = aDestStride - aSize.width;
  uint8_t* destPixel = aDestData;

  for (int32_t y = 0; y < aSize.height; y++) {
    for (int32_t x = 0; x < aSize.width; x++) {
      uint8_t a = sourcePixel[GFX_ARGB32_OFFSET_A];

      if (a) {
        *destPixel = (redFactor * sourcePixel[GFX_ARGB32_OFFSET_R] +
                      greenFactor * sourcePixel[GFX_ARGB32_OFFSET_G] +
                      blueFactor * sourcePixel[GFX_ARGB32_OFFSET_B]) >>
                     8;
      } else {
        *destPixel = 0;
      }
      sourcePixel += 4;
      destPixel++;
    }
    sourcePixel += sourceOffset;
    destPixel += destOffset;
  }
}

static void ComputeLinearRGBLuminanceMask(
    const uint8_t* aSourceData, int32_t aSourceStride, uint8_t* aDestData,
    int32_t aDestStride, const IntSize& aSize, float aOpacity) {
  int32_t redFactor = 55 * aOpacity;     // 255 * 0.2125 * opacity
  int32_t greenFactor = 183 * aOpacity;  // 255 * 0.7154 * opacity
  int32_t blueFactor = 18 * aOpacity;    // 255 * 0.0721
  int32_t sourceOffset = aSourceStride - 4 * aSize.width;
  const uint8_t* sourcePixel = aSourceData;
  int32_t destOffset = aDestStride - aSize.width;
  uint8_t* destPixel = aDestData;

  for (int32_t y = 0; y < aSize.height; y++) {
    for (int32_t x = 0; x < aSize.width; x++) {
      uint8_t a = sourcePixel[GFX_ARGB32_OFFSET_A];

      // unpremultiply
      if (a) {
        if (a == 255) {
          /* sRGB -> linearRGB -> intensity */
          *destPixel = static_cast<uint8_t>(
              (gsRGBToLinearRGBMap[sourcePixel[GFX_ARGB32_OFFSET_R]] *
                   redFactor +
               gsRGBToLinearRGBMap[sourcePixel[GFX_ARGB32_OFFSET_G]] *
                   greenFactor +
               gsRGBToLinearRGBMap[sourcePixel[GFX_ARGB32_OFFSET_B]] *
                   blueFactor) >>
              8);
        } else {
          uint8_t tempPixel[4];
          tempPixel[GFX_ARGB32_OFFSET_B] =
              (255 * sourcePixel[GFX_ARGB32_OFFSET_B]) / a;
          tempPixel[GFX_ARGB32_OFFSET_G] =
              (255 * sourcePixel[GFX_ARGB32_OFFSET_G]) / a;
          tempPixel[GFX_ARGB32_OFFSET_R] =
              (255 * sourcePixel[GFX_ARGB32_OFFSET_R]) / a;

          /* sRGB -> linearRGB -> intensity */
          *destPixel = static_cast<uint8_t>(
              ((gsRGBToLinearRGBMap[tempPixel[GFX_ARGB32_OFFSET_R]] *
                    redFactor +
                gsRGBToLinearRGBMap[tempPixel[GFX_ARGB32_OFFSET_G]] *
                    greenFactor +
                gsRGBToLinearRGBMap[tempPixel[GFX_ARGB32_OFFSET_B]] *
                    blueFactor) >>
               8) *
              (a / 255.0f));
        }
      } else {
        *destPixel = 0;
      }
      sourcePixel += 4;
      destPixel++;
    }
    sourcePixel += sourceOffset;
    destPixel += destOffset;
  }
}

RefPtr<DrawTarget> DrawTarget::CreateClippedDrawTargetForRect(
    const IntRect& aRect, SurfaceFormat aFormat) {
  RefPtr<DrawTarget> similar = CreateSimilarDrawTarget(aRect.Size(), aFormat);
  if (!similar) {
    return nullptr;
  }
  return Factory::CreateOffsetDrawTarget(similar, aRect.TopLeft());
}

void DrawTarget::PushDeviceSpaceClipRects(const IntRect* aRects,
                                          uint32_t aCount) {
  Matrix oldTransform = GetTransform();
  SetTransform(Matrix());

  RefPtr<PathBuilder> pathBuilder = CreatePathBuilder();
  for (uint32_t i = 0; i < aCount; i++) {
    AppendRectToPath(pathBuilder, Rect(aRects[i]));
  }
  RefPtr<Path> path = pathBuilder->Finish();
  PushClip(path);

  SetTransform(oldTransform);
}

void DrawTarget::FillRoundedRect(const RoundedRect& aRect,
                                 const Pattern& aPattern,
                                 const DrawOptions& aOptions) {
  RefPtr<Path> path = MakePathForRoundedRect(*this, aRect.rect, aRect.corners);
  Fill(path, aPattern, aOptions);
}

void DrawTarget::StrokeCircle(const Point& aOrigin, float radius,
                              const Pattern& aPattern,
                              const StrokeOptions& aStrokeOptions,
                              const DrawOptions& aOptions) {
  RefPtr<Path> path = MakePathForCircle(*this, aOrigin, radius);
  Stroke(path, aPattern, aStrokeOptions, aOptions);
}

void DrawTarget::FillCircle(const Point& aOrigin, float radius,
                            const Pattern& aPattern,
                            const DrawOptions& aOptions) {
  RefPtr<Path> path = MakePathForCircle(*this, aOrigin, radius);
  Fill(path, aPattern, aOptions);
}

void DrawTarget::StrokeGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                              const Pattern& aPattern,
                              const StrokeOptions& aStrokeOptions,
                              const DrawOptions& aOptions) {
  if (RefPtr<Path> path = aFont->GetPathForGlyphs(aBuffer, this)) {
    Stroke(path, aPattern, aStrokeOptions, aOptions);
  }
}

already_AddRefed<SourceSurface> DrawTarget::IntoLuminanceSource(
    LuminanceType aMaskType, float aOpacity) {
  // The default IntoLuminanceSource implementation needs a format of B8G8R8A8.
  if (mFormat != SurfaceFormat::B8G8R8A8) {
    return nullptr;
  }

  RefPtr<SourceSurface> surface = Snapshot();
  if (!surface) {
    return nullptr;
  }

  IntSize size = surface->GetSize();

  RefPtr<DataSourceSurface> maskSurface = surface->GetDataSurface();
  if (!maskSurface) {
    return nullptr;
  }

  DataSourceSurface::MappedSurface map;
  if (!maskSurface->Map(DataSourceSurface::MapType::READ, &map)) {
    return nullptr;
  }

  // Create alpha channel mask for output
  RefPtr destMaskSurface = MakeRefPtr<SourceSurfaceAlignedRawData>();
  if (!destMaskSurface->Init(size, SurfaceFormat::A8, false, 0)) {
    return nullptr;
  }
  DataSourceSurface::MappedSurface destMap;
  if (!destMaskSurface->Map(DataSourceSurface::MapType::WRITE, &destMap)) {
    return nullptr;
  }

  switch (aMaskType) {
    case LuminanceType::LUMINANCE: {
      ComputesRGBLuminanceMask(map.mData, map.mStride, destMap.mData,
                               destMap.mStride, size, aOpacity);
      break;
    }
    case LuminanceType::LINEARRGB: {
      ComputeLinearRGBLuminanceMask(map.mData, map.mStride, destMap.mData,
                                    destMap.mStride, size, aOpacity);
      break;
    }
  }

  maskSurface->Unmap();
  destMaskSurface->Unmap();

  return destMaskSurface.forget();
}

void DrawTarget::Blur(const GaussianBlur& aBlur) {
  uint8_t* data;
  IntSize size;
  int32_t stride;
  SurfaceFormat format;
  if (!LockBits(&data, &size, &stride, &format)) {
    gfxWarning() << "Cannot perform in-place blur on non-data DrawTarget";
    return;
  }

  aBlur.Blur(data, stride, size, format);

  ReleaseBits(data);
}

void DrawTarget::PadEdges(const IntRegion& aRegion) {
  PadDrawTargetOutFromRegion(this, aRegion);
}

bool DrawTarget::Unrotate(IntPoint aRotation) {
  unsigned char* data;
  IntSize size;
  int32_t stride;
  SurfaceFormat format;

  if (LockBits(&data, &size, &stride, &format)) {
    uint8_t bytesPerPixel = BytesPerPixel(format);
    BufferUnrotate(data, size.width * bytesPerPixel, size.height, stride,
                   aRotation.x * bytesPerPixel, aRotation.y);
    ReleaseBits(data);
    return true;
  }
  return false;
}

int32_t ShadowOptions::BlurRadius() const {
  return GaussianBlur::CalculateBlurRadius(Point(mSigma, mSigma)).width;
}

void DrawTarget::DrawShadow(const Path* aPath, const Pattern& aPattern,
                            const ShadowOptions& aShadow,
                            const DrawOptions& aOptions,
                            const StrokeOptions* aStrokeOptions) {
  // Get the approximate bounds of the source path
  Rect bounds = aPath->GetFastBounds(GetTransform(), aStrokeOptions);
  if (bounds.IsEmpty()) {
    return;
  }
  // Inflate the bounds by the blur radius
  bounds += aShadow.mOffset;
  int32_t blurRadius = aShadow.BlurRadius();
  bounds.Inflate(blurRadius);
  bounds.RoundOut();
  // Check if the bounds intersect the viewport
  Rect viewport(GetRect());
  viewport.Inflate(blurRadius);
  bounds = bounds.Intersect(viewport);
  IntRect intBounds;
  if (bounds.IsEmpty() || !bounds.ToIntRect(&intBounds) ||
      !CanCreateSimilarDrawTarget(intBounds.Size(), SurfaceFormat::A8)) {
    return;
  }
  // Create a draw target for drawing the shadow mask with enough room for blur
  RefPtr<DrawTarget> shadowTarget = CreateShadowDrawTarget(
      intBounds.Size(), SurfaceFormat::A8, aShadow.mSigma);
  if (shadowTarget) {
    // See bug 1524554.
    shadowTarget->ClearRect(Rect());
  }
  if (!shadowTarget || !shadowTarget->IsValid()) {
    return;
  }
  // Draw the path into the target for the initial shadow mask
  Point offset = Point(intBounds.TopLeft()) - aShadow.mOffset;
  shadowTarget->SetTransform(GetTransform().PostTranslate(-offset));
  DrawOptions shadowDrawOptions(
      aOptions.mAlpha, CompositionOp::OP_OVER,
      blurRadius > 1 ? AntialiasMode::NONE : aOptions.mAntialiasMode);
  if (aStrokeOptions) {
    shadowTarget->Stroke(aPath, aPattern, *aStrokeOptions, shadowDrawOptions);
  } else {
    shadowTarget->Fill(aPath, aPattern, shadowDrawOptions);
  }
  RefPtr<SourceSurface> snapshot = shadowTarget->Snapshot();
  // Finally, hand a snapshot of the mask to DrawSurfaceWithShadow for the
  // final shadow blur
  if (snapshot) {
    DrawSurfaceWithShadow(snapshot, offset, aShadow, aOptions.mCompositionOp);
  }
}

already_AddRefed<SourceSurface> DrawTarget::ResolveFilterInput(
    const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
    const Matrix& aDestTransform, const DrawOptions& aOptions,
    const StrokeOptions* aStrokeOptions, SurfaceFormat aFormat) {
  if (!CanCreateSimilarDrawTarget(aSourceRect.Size(), aFormat)) {
    return nullptr;
  }
  RefPtr<DrawTarget> dt = CreateSimilarDrawTarget(aSourceRect.Size(), aFormat);
  if (dt) {
    // See bug 1524554.
    dt->ClearRect(Rect());
  }
  if (!dt || !dt->IsValid()) {
    return nullptr;
  }
  dt->SetTransform(
      Matrix(aDestTransform).PostTranslate(-aSourceRect.TopLeft()));
  if (aStrokeOptions) {
    dt->Stroke(aPath, aPattern, *aStrokeOptions, aOptions);
  } else {
    dt->Fill(aPath, aPattern, aOptions);
  }
  return dt->Snapshot();
}

already_AddRefed<FilterNode> DrawTarget::DeferFilterInput(
    const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
    const IntPoint& aDestOffset, const DrawOptions& aOptions,
    const StrokeOptions* aStrokeOptions) {
  RefPtr<SourceSurface> surface = ResolveFilterInput(
      aPath, aPattern, aSourceRect, GetTransform().PostTranslate(aDestOffset),
      aOptions, aStrokeOptions);
  if (!surface) {
    return nullptr;
  }
  return FilterWrappers::ForSurface(this, surface, aSourceRect.TopLeft());
}

}  // namespace gfx
}  // namespace mozilla
