/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWTARGETSKIA_H
#define MOZILLA_GFX_DRAWTARGETSKIA_H

#include <sstream>
#include <vector>

#include "2D.h"

#ifdef XP_DARWIN
#  include <CoreGraphics/CGColorSpace.h>
#endif

class SkCanvas;
class SkShader;
class SkSurface;

namespace mozilla {

template <>
class RefPtrTraits<SkSurface> {
 public:
  static void Release(SkSurface* aSurface);
  static void AddRef(SkSurface* aSurface);
};

namespace gfx {

class DataSourceSurface;
class SourceSurfaceSkia;
class BorrowedCGContext;

class DrawTargetSkia : public DrawTarget {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawTargetSkia, override)
  DrawTargetSkia();
  virtual ~DrawTargetSkia();

  void Link(const char* aDest, const char* aURI, const Rect& aRect) override;
  void Destination(const char* aDestination, const Point& aPoint) override;

  DrawTargetType GetType() const override;
  BackendType GetBackendType() const override { return BackendType::SKIA; }
  already_AddRefed<SourceSurface> Snapshot(SurfaceFormat aFormat);
  already_AddRefed<SourceSurface> Snapshot() override {
    return Snapshot(mFormat);
  }
  already_AddRefed<SourceSurface> GetBackingSurface() override;
  IntSize GetSize() const override { return mSize; };
  bool LockBits(uint8_t** aData, IntSize* aSize, int32_t* aStride,
                SurfaceFormat* aFormat, IntPoint* aOrigin = nullptr) override;
  void ReleaseBits(uint8_t* aData) override;
  void Flush() override;
  void DrawSurface(
      SourceSurface* aSurface, const Rect& aDest, const Rect& aSource,
      const DrawSurfaceOptions& aSurfOptions = DrawSurfaceOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;
  void DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                  const Point& aDestPoint,
                  const DrawOptions& aOptions = DrawOptions()) override;
  void DrawSurfaceWithShadow(SourceSurface* aSurface, const Point& aDest,
                             const ShadowOptions& aShadow,
                             CompositionOp aOperator) override;
  void Blur(const GaussianBlur& aBlur) override;
  void ClearRect(const Rect& aRect) override;
  void CopySurface(SourceSurface* aSurface, const IntRect& aSourceRect,
                   const IntPoint& aDestination) override;
  void FillRect(const Rect& aRect, const Pattern& aPattern,
                const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeRect(const Rect& aRect, const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeLine(const Point& aStart, const Point& aEnd,
                  const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;
  void Stroke(const Path* aPath, const Pattern& aPattern,
              const StrokeOptions& aStrokeOptions = StrokeOptions(),
              const DrawOptions& aOptions = DrawOptions()) override;
  void Fill(const Path* aPath, const Pattern& aPattern,
            const DrawOptions& aOptions = DrawOptions()) override;

  void FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                  const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions()) override;
  void StrokeGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                    const Pattern& aPattern,
                    const StrokeOptions& aStrokeOptions = StrokeOptions(),
                    const DrawOptions& aOptions = DrawOptions()) override;
  void Mask(const Pattern& aSource, const Pattern& aMask,
            const DrawOptions& aOptions = DrawOptions()) override;
  void MaskSurface(const Pattern& aSource, SourceSurface* aMask, Point aOffset,
                   const DrawOptions& aOptions = DrawOptions()) override;
  bool Draw3DTransformedSurface(SourceSurface* aSurface,
                                const Matrix4x4& aMatrix) override;
  void PushClip(const Path* aPath) override;
  void PushClipRect(const Rect& aRect) override;
  void PushDeviceSpaceClipRects(const IntRect* aRects,
                                uint32_t aCount) override;
  void PopClip() override;
  bool RemoveAllClips() override;
  void PushLayer(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                 const Matrix& aMaskTransform,
                 const IntRect& aBounds = IntRect(),
                 bool aCopyBackground = false) override;
  void PushLayerWithBlend(
      bool aOpaque, Float aOpacity, SourceSurface* aMask,
      const Matrix& aMaskTransform, const IntRect& aBounds = IntRect(),
      bool aCopyBackground = false,
      CompositionOp aCompositionOp = CompositionOp::OP_OVER) override;
  void PopLayer() override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromData(
      unsigned char* aData, const IntSize& aSize, int32_t aStride,
      SurfaceFormat aFormat) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurface(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> OptimizeSourceSurfaceForUnknownAlpha(
      SourceSurface* aSurface) const override;
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromNativeSurface(
      const NativeSurface& aSurface) const override;
  already_AddRefed<DrawTarget> CreateSimilarDrawTarget(
      const IntSize& aSize, SurfaceFormat aFormat) const override;
  bool CanCreateSimilarDrawTarget(const IntSize& aSize,
                                  SurfaceFormat aFormat) const override;
  RefPtr<DrawTarget> CreateClippedDrawTarget(const Rect& aBounds,
                                             SurfaceFormat aFormat) override;

  already_AddRefed<PathBuilder> CreatePathBuilder(
      FillRule aFillRule = FillRule::FILL_WINDING) const override;

  already_AddRefed<GradientStops> CreateGradientStops(
      GradientStop* aStops, uint32_t aNumStops,
      ExtendMode aExtendMode = ExtendMode::CLAMP) const override;
  already_AddRefed<FilterNode> CreateFilter(FilterType aType) override;
  void SetTransform(const Matrix& aTransform) override;
  void* GetNativeSurface(NativeSurfaceType aType) override;
  void DetachAllSnapshots() override;

  bool Init(const IntSize& aSize, SurfaceFormat aFormat);
  bool Init(unsigned char* aData, const IntSize& aSize, int32_t aStride,
            SurfaceFormat aFormat, bool aUninitialized = false,
            bool aIsClear = false);
  bool Init(SkCanvas* aCanvas, bool aSupportsGlyphSourceText = false);
  bool Init(RefPtr<DataSourceSurface>&& aSurface);

  // Skia assumes that texture sizes fit in 16-bit integers.
  static size_t GetMaxSurfaceSize() { return 65535; }
  // Skia assumes the surface area will fit in a 32-bit signed integer.
  static size_t GetMaxSurfaceArea() { return 0x7FFFFFFF; }

  operator std::string() const {
    std::stringstream stream;
    stream << "DrawTargetSkia(" << this << ")";
    return stream.str();
  }

  Maybe<IntRect> GetDeviceClipRect(bool aAllowComplex = false) const;
  bool IsClipEmpty() const;

  Maybe<Rect> GetGlyphLocalBounds(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                                  const Pattern& aPattern,
                                  const StrokeOptions* aStrokeOptions,
                                  const DrawOptions& aOptions);

  void DrawGlyphMask(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                     const DeviceColor& aColor,
                     const StrokeOptions* aStrokeOptions = nullptr,
                     const DrawOptions& aOptions = DrawOptions());

  void AccessibleId(uint64_t aInnerWindowId, uint64_t aAccId) final;

  bool SupportsGlyphSourceText() const final {
    return mSupportsGlyphSourceText;
  }

 private:
  friend class SourceSurfaceSkia;

  static void ReleaseMappedSkSurface(void* aPixels, void* aContext);

  void MarkChanged();

  void DrawGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                  const Pattern& aPattern,
                  const StrokeOptions* aStrokeOptions = nullptr,
                  const DrawOptions& aOptions = DrawOptions(),
                  SkShader* aShader = nullptr);

  struct PushedLayer {
    RefPtr<SourceSurface> mMask;
    Matrix mMaskToDevice;
    CompositionOp mCompositionOp = CompositionOp::OP_OVER;
    bool mOldPermitSubpixelAA = false;
    int mSaveCount = 0;
  };
  std::vector<PushedLayer> mPushedLayers;

  IntSize mSize;
  RefPtr<SkSurface> mSurface;
  SkCanvas* mCanvas = nullptr;
  RefPtr<DataSourceSurface> mBackingSurface;
  RefPtr<SourceSurfaceSkia> mSnapshot;
  Mutex mSnapshotLock MOZ_UNANNOTATED;
  bool mIsClear = false;
  bool mSupportsGlyphSourceText = false;

#ifdef XP_DARWIN
  friend class BorrowedCGContext;

  CGContextRef BorrowCGContext(const DrawOptions& aOptions);
  void ReturnCGContext(CGContextRef);
  bool FillGlyphsWithCG(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                        const Pattern& aPattern,
                        const DrawOptions& aOptions = DrawOptions());

  CGContextRef mCG;
  CGColorSpaceRef mColorSpace;
  uint8_t* mCanvasData;
  IntSize mCGSize;
  bool mNeedLayer;
#endif
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _MOZILLA_GFX_SOURCESURFACESKIA_H
