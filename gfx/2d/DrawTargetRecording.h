/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWTARGETRECORDING_H_
#define MOZILLA_GFX_DRAWTARGETRECORDING_H_

#include "2D.h"
#include "DrawEventRecorder.h"

class nsICanvasRenderingContextInternal;

namespace mozilla {

namespace ipc {
class IProtocol;
}  // namespace ipc

namespace layers {
class CanvasChild;
class CanvasDrawEventRecorder;
class RecordedTextureData;
struct RemoteTextureOwnerId;
}  // namespace layers

namespace gfx {

class DrawTargetRecording final : public DrawTarget {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DrawTargetRecording, override)
  DrawTargetRecording(DrawEventRecorder* aRecorder, DrawTarget* aDT,
                      IntRect aRect, bool aHasData = false);
  DrawTargetRecording(layers::CanvasDrawEventRecorder* aRecorder,
                      const layers::RemoteTextureOwnerId& aTextureOwnerId,
                      DrawTarget* aDT, const IntSize& aSize,
                      bool aPreferAcceleration = false);

  ~DrawTargetRecording();

  DrawTargetType GetType() const override { return mFinalDT->GetType(); }
  BackendType GetBackendType() const override { return BackendType::RECORDING; }
  bool IsRecording() const override { return true; }

  // mFinalDT isn't necessarily the target this will eventually be replayed
  // against, so we ask the recorder instead.
  bool SupportsGlyphSourceText() const override {
    return mRecorder->SupportsGlyphSourceText();
  }

  void Link(const char* aLocalDest, const char* aURI,
            const Rect& aRect) override;
  void Destination(const char* aDestination, const Point& aPoint) override;

  void AccessibleId(uint64_t aInnerWindowId, uint64_t aAccId) final;

  already_AddRefed<SourceSurface> Snapshot() override;
  already_AddRefed<SourceSurface> IntoLuminanceSource(
      LuminanceType aLuminanceType, float aOpacity) override;

  void DetachAllSnapshots() override;

  IntSize GetSize() const override { return mRect.Size(); }
  IntRect GetRect() const override { return mRect; }

  void Flush() override;

  void FlushItem(const IntRect& aBounds) override;

  /*
   * Draw a surface to the draw target. Possibly doing partial drawing or
   * applying scaling. No sampling happens outside the source.
   *
   * aSurface Source surface to draw
   * aDest Destination rectangle that this drawing operation should draw to
   * aSource Source rectangle in aSurface coordinates, this area of aSurface
   *         will be stretched to the size of aDest.
   * aOptions General draw options that are applied to the operation
   * aSurfOptions DrawSurface options that are applied
   */
  void DrawSurface(
      SourceSurface* aSurface, const Rect& aDest, const Rect& aSource,
      const DrawSurfaceOptions& aSurfOptions = DrawSurfaceOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;

  void DrawSurfaceDescriptor(
      const layers::SurfaceDescriptor& aDesc,
      const RefPtr<layers::Image>& aImageOfSurfaceDescriptor, const Rect& aDest,
      const Rect& aSource,
      const DrawSurfaceOptions& aSurfOptions = DrawSurfaceOptions(),
      const DrawOptions& aOptions = DrawOptions()) override;

  void DrawDependentSurface(uint64_t aId, const Rect& aDest) override;

  void DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                  const Point& aDestPoint,
                  const DrawOptions& aOptions = DrawOptions()) override;

  void DrawSurfaceWithShadow(SourceSurface* aSurface, const Point& aDest,
                             const ShadowOptions& aShadow,
                             CompositionOp aOperator) override;

  void DrawShadow(const Path* aPath, const Pattern& aPattern,
                  const ShadowOptions& aShadow, const DrawOptions& aOptions,
                  const StrokeOptions* aStrokeOptions) override;

  /*
   * Clear a rectangle on the draw target to transparent black. This will
   * respect the clipping region and transform.
   *
   * aRect Rectangle to clear
   */
  void ClearRect(const Rect& aRect) override;

  /*
   * This is essentially a 'memcpy' between two surfaces. It moves a pixel
   * aligned area from the source surface unscaled directly onto the
   * drawtarget. This ignores both transform and clip.
   *
   * aSurface Surface to copy from
   * aSourceRect Source rectangle to be copied
   * aDest Destination point to copy the surface to
   */
  void CopySurface(SourceSurface* aSurface, const IntRect& aSourceRect,
                   const IntPoint& aDestination) override;

  /*
   * Fill a rectangle on the DrawTarget with a certain source pattern.
   *
   * aRect Rectangle that forms the mask of this filling operation
   * aPattern Pattern that forms the source of this filling operation
   * aOptions Options that are applied to this operation
   */
  void FillRect(const Rect& aRect, const Pattern& aPattern,
                const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Stroke a rectangle on the DrawTarget with a certain source pattern.
   *
   * aRect Rectangle that forms the mask of this stroking operation
   * aPattern Pattern that forms the source of this stroking operation
   * aOptions Options that are applied to this operation
   */
  void StrokeRect(const Rect& aRect, const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Stroke a line on the DrawTarget with a certain source pattern.
   *
   * aStart Starting point of the line
   * aEnd End point of the line
   * aPattern Pattern that forms the source of this stroking operation
   * aOptions Options that are applied to this operation
   */
  void StrokeLine(const Point& aStart, const Point& aEnd,
                  const Pattern& aPattern,
                  const StrokeOptions& aStrokeOptions = StrokeOptions(),
                  const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Stroke a circle on the DrawTarget with a certain source pattern.
   *
   * aOrigin Center point of the circle
   * aRadius Radius of the circle
   * aPattern Pattern that forms the source of this stroking operation
   * aOptions Options that are applied to this operation
   */
  void StrokeCircle(const Point& aOrigin, float aRadius,
                    const Pattern& aPattern,
                    const StrokeOptions& aStrokeOptions = StrokeOptions(),
                    const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Stroke a path on the draw target with a certain source pattern.
   *
   * aPath Path that is to be stroked
   * aPattern Pattern that should be used for the stroke
   * aStrokeOptions Stroke options used for this operation
   * aOptions Draw options used for this operation
   */
  void Stroke(const Path* aPath, const Pattern& aPattern,
              const StrokeOptions& aStrokeOptions = StrokeOptions(),
              const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Fill a path on the draw target with a certain source pattern.
   *
   * aPath Path that is to be filled
   * aPattern Pattern that should be used for the fill
   * aOptions Draw options used for this operation
   */
  void Fill(const Path* aPath, const Pattern& aPattern,
            const DrawOptions& aOptions = DrawOptions()) override;

  void FillCircle(const Point& aOrigin, float aRadius, const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Fill a series of glyphs on the draw target with a certain source pattern.
   */
  void FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                  const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions()) override;

  /**
   * Stroke a series of glyphs on the draw target with a certain source pattern.
   */
  void StrokeGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                    const Pattern& aPattern,
                    const StrokeOptions& aStrokeOptions = StrokeOptions(),
                    const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * This takes a source pattern and a mask, and composites the source pattern
   * onto the destination surface using the alpha channel of the mask pattern
   * as a mask for the operation.
   *
   * aSource Source pattern
   * aMask Mask pattern
   * aOptions Drawing options
   */
  void Mask(const Pattern& aSource, const Pattern& aMask,
            const DrawOptions& aOptions = DrawOptions()) override;

  void MaskSurface(const Pattern& aSource, SourceSurface* aMask, Point aOffset,
                   const DrawOptions& aOptions = DrawOptions()) override;

  /*
   * Push a clip to the DrawTarget.
   *
   * aPath The path to clip to
   */
  void PushClip(const Path* aPath) override;

  /*
   * Push an axis-aligned rectangular clip to the DrawTarget. This rectangle
   * is specified in user space.
   *
   * aRect The rect to clip to
   */
  void PushClipRect(const Rect& aRect) override;

  /* Pop a clip from the DrawTarget. A pop without a corresponding push will
   * be ignored.
   */
  void PopClip() override;

  /* Remove all applied clips. */
  bool RemoveAllClips() override;

  /**
   * Push a 'layer' to the DrawTarget, a layer is a temporary surface that all
   * drawing will be redirected to, this is used for example to support group
   * opacity or the masking of groups. Clips must be balanced within a layer,
   * i.e. between a matching PushLayer/PopLayer pair there must be as many
   * PushClip(Rect) calls as there are PopClip calls.
   *
   * @param aOpaque Whether the layer will be opaque
   * @param aOpacity Opacity of the layer
   * @param aMask Mask applied to the layer
   * @param aMaskTransform Transform applied to the layer mask
   * @param aBounds Optional bounds in device space to which the layer is
   *                limited in size.
   * @param aCopyBackground Whether to copy the background into the layer, this
   *                        is only supported when aOpaque is true.
   */
  void PushLayer(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                 const Matrix& aMaskTransform,
                 const IntRect& aBounds = IntRect(),
                 bool aCopyBackground = false) override;

  /**
   * Push a 'layer' to the DrawTarget, a layer is a temporary surface that all
   * drawing will be redirected to, this is used for example to support group
   * opacity or the masking of groups. Clips must be balanced within a layer,
   * i.e. between a matching PushLayer/PopLayer pair there must be as many
   * PushClip(Rect) calls as there are PopClip calls.
   *
   * @param aOpaque Whether the layer will be opaque
   * @param aOpacity Opacity of the layer
   * @param aMask Mask applied to the layer
   * @param aMaskTransform Transform applied to the layer mask
   * @param aBounds Optional bounds in device space to which the layer is
   *                limited in size.
   * @param aCopyBackground Whether to copy the background into the layer, this
   *                        is only supported when aOpaque is true.a
   * @param aCompositionOp The CompositionOp to use when blending the layer into
   *                       the destination
   */
  void PushLayerWithBlend(
      bool aOpaque, Float aOpacity, SourceSurface* aMask,
      const Matrix& aMaskTransform, const IntRect& aBounds = IntRect(),
      bool aCopyBackground = false,
      CompositionOp aCompositionOp = CompositionOp::OP_OVER) override;

  /**
   * This balances a call to PushLayer and proceeds to blend the layer back
   * onto the background. This blend will blend the temporary surface back
   * onto the target in device space using POINT sampling and operator over.
   */
  void PopLayer() override;

  /*
   * Create a SourceSurface optimized for use with this DrawTarget from
   * existing bitmap data in memory.
   *
   * The SourceSurface does not take ownership of aData, and may be freed at any
   * time.
   */
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromData(
      unsigned char* aData, const IntSize& aSize, int32_t aStride,
      SurfaceFormat aFormat) const override;

  /*
   * Create a SourceSurface optimized for use with this DrawTarget from
   * an arbitrary other SourceSurface. This may return aSourceSurface or some
   * other existing surface.
   */
  already_AddRefed<SourceSurface> OptimizeSourceSurface(
      SourceSurface* aSurface) const override;

  /*
   * Create a SourceSurface for a type of NativeSurface. This may fail if the
   * draw target does not know how to deal with the type of NativeSurface passed
   * in.
   */
  already_AddRefed<SourceSurface> CreateSourceSurfaceFromNativeSurface(
      const NativeSurface& aSurface) const override;

  /*
   * Create a DrawTarget whose snapshot is optimized for use with this
   * DrawTarget.
   */
  already_AddRefed<DrawTarget> CreateSimilarDrawTarget(
      const IntSize& aSize, SurfaceFormat aFormat) const override;

  /**
   * Create a DrawTarget whose backing surface is optimized for use with this
   * DrawTarget.
   */
  already_AddRefed<DrawTarget> CreateSimilarDrawTargetWithBacking(
      const IntSize& aSize, SurfaceFormat aFormat) const override;

  bool CanCreateSimilarDrawTarget(const IntSize& aSize,
                                  SurfaceFormat aFormat) const override;
  /**
   * Create a similar DrawTarget whose requested size may be clipped based
   * on this DrawTarget's rect transformed to the new target's space.
   */
  RefPtr<DrawTarget> CreateClippedDrawTarget(const Rect& aBounds,
                                             SurfaceFormat aFormat) override;

  RefPtr<DrawTarget> CreateClippedDrawTargetForRect(
      const IntRect& aRect, SurfaceFormat aFormat) override;

  already_AddRefed<DrawTarget> CreateSimilarDrawTargetForFilter(
      const IntSize& aSize, SurfaceFormat aFormat, FilterNode* aFilter,
      FilterNode* aSource, const Rect& aSourceRect,
      const Point& aDestPoint) override;
  /*
   * Create a path builder with the specified fillmode.
   *
   * We need the fill mode up front because of Direct2D.
   * ID2D1SimplifiedGeometrySink requires the fill mode
   * to be set before calling BeginFigure().
   */
  already_AddRefed<PathBuilder> CreatePathBuilder(
      FillRule aFillRule = FillRule::FILL_WINDING) const override;

  /*
   * Create a GradientStops object that holds information about a set of
   * gradient stops, this object is required for linear or radial gradient
   * patterns to represent the color stops in the gradient.
   *
   * aStops An array of gradient stops
   * aNumStops Number of stops in the array aStops
   * aExtendNone This describes how to extend the stop color outside of the
   *             gradient area.
   */
  already_AddRefed<GradientStops> CreateGradientStops(
      GradientStop* aStops, uint32_t aNumStops,
      ExtendMode aExtendMode = ExtendMode::CLAMP) const override;

  already_AddRefed<FilterNode> CreateFilter(FilterType aType) override;

  already_AddRefed<FilterNode> DeferFilterInput(
      const Path* aPath, const Pattern& aPattern, const IntRect& aSourceRect,
      const IntPoint& aDestOffset, const DrawOptions& aOptions = DrawOptions(),
      const StrokeOptions* aStrokeOptions = nullptr) override;

  /*
   * Set a transform on the surface, this transform is applied at drawing time
   * to both the mask and source of the operation.
   */
  void SetTransform(const Matrix& aTransform) override;

  void SetPermitSubpixelAA(bool aPermitSubpixelAA) override;

  /* Tries to get a native surface for a DrawTarget, this may fail if the
   * draw target cannot convert to this surface type.
   */
  void* GetNativeSurface(NativeSurfaceType aType) override {
    return mFinalDT->GetNativeSurface(aType);
  }

  bool IsCurrentGroupOpaque() override {
    return mFinalDT->IsCurrentGroupOpaque();
  }

  void SetOptimizeTransform(bool aOptimizeTransform) {
    mOptimizeTransform = aOptimizeTransform;
  }

 protected:
  friend class layers::RecordedTextureData;

  void AttachTextureData(layers::RecordedTextureData* aTextureData) {
    mTextureData = aTextureData;
  }
  void DetachTextureData(layers::RecordedTextureData*) {
    mTextureData = nullptr;
  }

  layers::RecordedTextureData* mTextureData = nullptr;

  friend class layers::CanvasChild;

  already_AddRefed<SourceSurface> CreateExternalSourceSurface(
      const IntSize& aSize, SurfaceFormat aFormat);

  friend class ::nsICanvasRenderingContextInternal;

  already_AddRefed<SourceSurface> SnapshotExternalCanvas(
      nsICanvasRenderingContextInternal* aCanvas,
      mozilla::ipc::IProtocol* aActor);

 private:
  /**
   * Used for creating a DrawTargetRecording for a CreateSimilarDrawTarget call.
   *
   * @param aDT DrawTargetRecording on which CreateSimilarDrawTarget was called
   * @param aSize size of the the similar DrawTarget
   * @param aFormat format of the similar DrawTarget
   */
  DrawTargetRecording(const DrawTargetRecording* aDT, IntRect aRect,
                      SurfaceFormat aFormat);

  void RecordTransform(const Matrix& aTransform) const;

  Rect DeviceToUserLayerBounds(const IntRect& aBounds) const;

  void FlushTransform() const {
    if (mTransformDirty) {
      if (!mRecordedTransform.ExactlyEquals(mTransform)) {
        RecordTransform(mTransform);
      }
      mTransformDirty = false;
    }
  }

  void RecordEvent(const RecordedEvent& aEvent) const {
    FlushTransform();
    mRecorder->RecordEvent(aEvent);
  }

  void RecordEventSelf(const RecordedEvent& aEvent) const {
    FlushTransform();
    mRecorder->RecordEvent(this, aEvent);
  }

  void RecordEventSkipFlushTransform(const RecordedEvent& aEvent) const {
    mRecorder->RecordEvent(aEvent);
  }

  void RecordEventSelfSkipFlushTransform(const RecordedEvent& aEvent) const {
    mRecorder->RecordEvent(this, aEvent);
  }

  Path* GetPathForPathRecording(const Path* aPath) const;
  already_AddRefed<PathRecording> EnsurePathStored(const Path* aPath);
  void EnsurePatternDependenciesStored(const Pattern& aPattern);

  void DrawGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                  const Pattern& aPattern,
                  const DrawOptions& aOptions = DrawOptions(),
                  const StrokeOptions* aStrokeOptions = nullptr);

  void MarkChanged();

  RefPtr<DrawEventRecorderPrivate> mRecorder;
  RefPtr<DrawTarget> mFinalDT;
  IntRect mRect;

  struct PushedLayer {
    explicit PushedLayer(bool aOldPermitSubpixelAA)
        : mOldPermitSubpixelAA(aOldPermitSubpixelAA) {}
    bool mOldPermitSubpixelAA;
  };
  std::vector<PushedLayer> mPushedLayers;

  bool mOptimizeTransform = false;

  // Last transform that was used in the recording.
  mutable Matrix mRecordedTransform;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_DRAWTARGETRECORDING_H_ */
