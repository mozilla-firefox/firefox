/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LAYOUT_SVG_FILTERINSTANCE_H_
#define LAYOUT_SVG_FILTERINSTANCE_H_

#include "FilterDescription.h"
#include "gfxMatrix.h"
#include "gfxPoint.h"
#include "gfxRect.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsSize.h"
#include "nsTArray.h"

class gfxContext;
class nsIContent;
class nsIFrame;
struct WrFiltersHolder;

namespace mozilla {
class ISVGFilterObserverList;
class SVGFilterFrame;

namespace dom {
class UserSpaceMetrics;
}  // namespace dom

namespace image {
struct imgDrawingParams;
}

/**
 * This class performs all filter processing.
 *
 * We build a graph of the filter image data flow, essentially
 * converting the filter graph to SSA. This lets us easily propagate
 * analysis data (such as bounding-boxes) over the filter primitive graph.
 *
 * Definition of "filter space": filter space is a coordinate system that is
 * aligned with the user space of the filtered element, with its origin located
 * at the top left of the filter region, and with one unit equal in size to one
 * pixel of the offscreen surface into which the filter output would/will be
 * painted.
 *
 * The definition of "filter region" can be found here:
 * http://www.w3.org/TR/SVG11/filters.html#FilterEffectsRegion
 */
class FilterInstance {
  using IntRect = gfx::IntRect;
  using SourceSurface = gfx::SourceSurface;
  using DrawTarget = gfx::DrawTarget;
  using FilterPrimitiveDescription = gfx::FilterPrimitiveDescription;
  using FilterDescription = gfx::FilterDescription;
  using UserSpaceMetrics = dom::UserSpaceMetrics;
  using imgDrawingParams = image::imgDrawingParams;
  using SVGFilterPaintCallback = SVGIntegrationUtils::SVGFilterPaintCallback;

 public:
  /**
   * Create a FilterDescription for the supplied filter. All coordinates in
   * the description are in filter space.
   * @param aFilterInputIsTainted Describes whether the SourceImage /
   *   SourceAlpha input is tainted. This affects whether feDisplacementMap
   *   will respect the filter input as its map input, and it affects the
   *   IsTainted() state on the filter primitives in the FilterDescription.
   *   "Tainted" is a term from the filters spec and means security-sensitive
   *   content, i.e. pixels that JS should not be able to read in any way.
   * @param aOutAdditionalImages Will contain additional images needed to
   *   render the filter (from feImage primitives).
   * @return A FilterDescription describing the filter.
   */
  static FilterDescription GetFilterDescription(
      nsIContent* aFilteredElement, Span<const StyleFilter> aFilterChain,
      ISVGFilterObserverList* aFiltersObserverList, bool aFilterInputIsTainted,
      const UserSpaceMetrics& aMetrics, const gfxRect& aBBox,
      nsTArray<RefPtr<SourceSurface>>& aOutAdditionalImages);

  /**
   * Paint the given filtered frame.
   * @param aDirtyArea The area than needs to be painted, in aFilteredFrame's
   *   frame space (i.e. relative to its origin, the top-left corner of its
   *   border box).
   */
  static void PaintFilteredFrame(
      nsIFrame* aFilteredFrame, Span<const StyleFilter> aFilterChain,
      const nsTArray<SVGFilterFrame*>& aFilterFrames, gfxContext* aCtx,
      const SVGFilterPaintCallback& aPaintCallback, const nsRegion* aDirtyArea,
      imgDrawingParams& aImgParams, float aOpacity = 1.0f,
      const gfxRect* aOverrideBBox = nullptr);

  /**
   * Returns the pre-filter area that is needed from aFilteredFrame when the
   * given post-filter area needs to be repainted.
   * @param aPostFilterDirtyRegion The post-filter area that is dirty, relative
   *   to aFilteredFrame, in app units.
   */
  static nsRegion GetPreFilterNeededArea(
      nsIFrame* aFilteredFrame, const nsTArray<SVGFilterFrame*>& aFilterFrames,
      const nsRegion& aPostFilterDirtyRegion);

  /**
   * Returns the post-filter ink overflow rect (paint bounds) of
   * aFilteredFrame.
   * @param aOverrideBBox A user space rect, in user units, that should be used
   *   as aFilteredFrame's bbox ('bbox' is a specific SVG term), if non-null.
   * @param aPreFilterBounds The pre-filter ink overflow rect of
   *   aFilteredFrame, if non-null.
   */
  static Maybe<nsRect> GetPostFilterBounds(
      nsIFrame* aFilteredFrame, const nsTArray<SVGFilterFrame*>& aFilterFrames,
      const gfxRect* aOverrideBBox = nullptr,
      const nsRect* aPreFilterBounds = nullptr);

  /**
   * Try to build WebRender filters for a frame if the filters applied to it are
   * supported, returns a status that indicates which code path will handle the
   * filters on this frame, or if we must fall back to blob image.
   */
  static WrFiltersStatus BuildWebRenderFilters(
      nsIFrame* aFilteredFrame,
      mozilla::Span<const mozilla::StyleFilter> aFilters,
      StyleFilterType aStyleFilterType, WrFiltersHolder& aWrFilters,
      const nsPoint& aOffsetForSVGFilters);

  /**
   * Try to build WebRender SVG filter graph for a frame based on SVG and CSS
   * filters.  If given an unreasonably costly set of filters this can reject
   * the entire filter graph (a behavior permitted by SVG spec).
   *
   * See WrFiltersStatus for possible outcomes.
   * Prefs such as gfx.webrender.svg-filter-effects affect this.
   */
  static WrFiltersStatus BuildWebRenderSVGFiltersImpl(
      nsIFrame* aFilteredFrame,
      mozilla::Span<const mozilla::StyleFilter> aFilters,
      StyleFilterType aStyleFilterType, WrFiltersHolder& aWrFilters,
      const nsPoint& aOffsetForSVGFilters);

 private:
  /**
   * @param aTargetFrame The frame of the filtered element under consideration,
   *   may be null.
   * @param aTargetContent The filtered element itself.
   * @param aMetrics The metrics to resolve SVG lengths against.
   * @param aFilterChain The list of filters to apply.
   * @param aFilterFrames The frames for the filters in the chain.
   * @param aFilterInputIsTainted Describes whether the SourceImage /
   *   SourceAlpha input is tainted. This affects whether feDisplacementMap
   *   will respect the filter input as its map input.
   * @param aPaintCallback [optional] The callback that Render() should use to
   *   paint. Only required if you will call Render().
   * @param aPaintTransform The transform to apply to convert to
   *   aTargetFrame's SVG user space. Only used when painting.
   * @param aPostFilterDirtyRegion [optional] The post-filter area
   *   that has to be repainted, in app units. Only required if you will
   *   call ComputeSourceNeededRect() or Render().
   * @param aPreFilterDirtyRegion [optional] The pre-filter area of
   *   the filtered element that changed, in app units. Only required if you
   *   will call ComputePostFilterDirtyRegion().
   * @param aPreFilterInkOverflowRectOverride [optional] Use a different
   *   ink overflow rect for the target element.
   * @param aOverrideBBox [optional] Use a different SVG bbox for the target
   *   element. Must be non-null if aTargetFrame is null.
   * @param aFilterSpaceBoundsNotSnapped [optional] The calculated bbox in
   *   userspace can be returend in the provided outparam.
   */
  FilterInstance(
      nsIFrame* aTargetFrame, nsIContent* aTargetContent,
      const UserSpaceMetrics& aMetrics, Span<const StyleFilter> aFilterChain,
      const nsTArray<SVGFilterFrame*>& aFilterFrames,
      bool aFilterInputIsTainted,
      const SVGIntegrationUtils::SVGFilterPaintCallback& aPaintCallback,
      const gfxMatrix& aPaintTransform,
      const nsRegion* aPostFilterDirtyRegion = nullptr,
      const nsRegion* aPreFilterDirtyRegion = nullptr,
      const nsRect* aPreFilterInkOverflowRectOverride = nullptr,
      const gfxRect* aOverrideBBox = nullptr,
      gfxRect* aFilterSpaceBoundsNotSnapped = nullptr);

  static WrFiltersStatus BuildWebRenderFiltersImpl(
      nsIFrame* aFilteredFrame,
      mozilla::Span<const mozilla::StyleFilter> aFilters,
      StyleFilterType aStyleFilterType, WrFiltersHolder& aWrFilters);

  /**
   * Returns true if the filter instance was created successfully.
   */
  bool IsInitialized() const { return mInitialized; }

  /**
   * Draws the filter output into aDrawTarget. The area that
   * needs to be painted must have been specified before calling this method
   * by passing it as the aPostFilterDirtyRegion argument to the
   * FilterInstance constructor.
   */
  void Render(gfxContext* aCtx, imgDrawingParams& aImgParams,
              float aOpacity = 1.0f);

  const FilterDescription& ExtractDescriptionAndAdditionalImages(
      nsTArray<RefPtr<SourceSurface>>& aOutAdditionalImages) {
    aOutAdditionalImages = std::move(mInputImages);
    return mFilterDescription;
  }

  /**
   * Sets the aPostFilterDirtyRegion outparam to the post-filter area in frame
   * space that would be dirtied by mTargetFrame when a given
   * pre-filter area of mTargetFrame is dirtied. The pre-filter area must have
   * been specified before calling this method by passing it as the
   * aPreFilterDirtyRegion argument to the FilterInstance constructor.
   */
  nsRegion ComputePostFilterDirtyRegion();

  /**
   * Sets the aPostFilterExtents outparam to the post-filter bounds in frame
   * space for the whole filter output. This is not necessarily equivalent to
   * the area that would be dirtied in the result when the entire pre-filter
   * area is dirtied, because some filter primitives can generate output
   * without any input.
   */
  nsRect ComputePostFilterExtents();

  /**
   * Sets the aDirty outparam to the pre-filter bounds in frame space of the
   * area of mTargetFrame that is needed in order to paint the filtered output
   * for a given post-filter dirtied area. The post-filter area must have been
   * specified before calling this method by passing it as the
   * aPostFilterDirtyRegion argument to the FilterInstance constructor.
   */
  nsRect ComputeSourceNeededRect();

  struct SourceInfo {
    // Specifies which parts of the source need to be rendered.
    // Set by ComputeNeededBoxes().
    nsIntRect mNeededBounds;

    // The surface that contains the input rendering.
    // Set by BuildSourceImage / BuildSourcePaint.
    RefPtr<SourceSurface> mSourceSurface;

    // The position and size of mSourceSurface in filter space.
    // Set by BuildSourceImage / BuildSourcePaint.
    IntRect mSurfaceRect;
  };

  /**
   * Creates a SourceSurface for either the FillPaint or StrokePaint graph
   * nodes
   */
  void BuildSourcePaint(SourceInfo* aSource, imgDrawingParams& aImgParams);

  /**
   * Creates a SourceSurface for either the FillPaint and StrokePaint graph
   * nodes, fills its contents and assigns it to mFillPaint.mSourceSurface and
   * mStrokePaint.mSourceSurface respectively.
   */
  void BuildSourcePaints(imgDrawingParams& aImgParams);

  /**
   * Creates the SourceSurface for the SourceGraphic graph node, paints its
   * contents, and assigns it to mSourceGraphic.mSourceSurface.
   */
  void BuildSourceImage(DrawTarget* aDest, imgDrawingParams& aImgParams,
                        mozilla::gfx::FilterNode* aFilter,
                        mozilla::gfx::FilterNode* aSource,
                        const mozilla::gfx::Rect& aSourceRect);

  /**
   * Build the list of FilterPrimitiveDescriptions that describes the filter's
   * filter primitives and their connections. This populates
   * mPrimitiveDescriptions and mInputImages. aFilterInputIsTainted describes
   * whether the SourceGraphic is tainted.
   */
  nsresult BuildPrimitives(Span<const StyleFilter> aFilterChain,
                           const nsTArray<SVGFilterFrame*>& aFilterFrames,
                           bool aFilterInputIsTainted);

  /**
   * Add to the list of FilterPrimitiveDescriptions for a particular SVG
   * reference filter or CSS filter. This populates mPrimitiveDescriptions and
   * mInputImages. aInputIsTainted describes whether the input to aFilter is
   * tainted.
   */
  nsresult BuildPrimitivesForFilter(
      const StyleFilter& aFilter, SVGFilterFrame* aFilterFrame,
      bool aInputIsTainted,
      nsTArray<FilterPrimitiveDescription>& aPrimitiveDescriptions);

  /**
   * Computes the filter space bounds of the areas that we actually *need* from
   * the filter sources, based on the value of mPostFilterDirtyRegion.
   * This sets mNeededBounds on the corresponding SourceInfo structs.
   */
  void ComputeNeededBoxes();

  /**
   * Returns the output bounds of the final FilterPrimitiveDescription.
   */
  nsIntRect OutputFilterSpaceBounds() const;

  /**
   * Compute the scale factors between user space and filter space.
   */
  bool ComputeUserSpaceToFilterSpaceScale();

  /**
   * Transform a rect between user space and filter space.
   */
  gfxRect UserSpaceToFilterSpace(const gfxRect& aUserSpace) const;
  gfxRect FilterSpaceToUserSpace(const gfxRect& aFilterSpaceRect) const;

  /**
   * Converts an nsRect or an nsRegion that is relative to a filtered frame's
   * origin (i.e. the top-left corner of its border box) into filter space,
   * rounding out.
   * Returns the entire filter region if aRect / aRegion is null, or if the
   * result is too large to be stored in an nsIntRect.
   */
  nsIntRect FrameSpaceToFilterSpace(const nsRect* aRect) const;
  nsIntRegion FrameSpaceToFilterSpace(const nsRegion* aRegion) const;

  /**
   * Converts an nsIntRect or an nsIntRegion from filter space into the space
   * that is relative to a filtered frame's origin (i.e. the top-left corner
   * of its border box) in app units, rounding out.
   */
  nsRect FilterSpaceToFrameSpace(const nsIntRect& aRect) const;
  nsRegion FilterSpaceToFrameSpace(const nsIntRegion& aRegion) const;

  /**
   * Returns the transform from frame space to the coordinate space that
   * GetCanvasTM transforms to. "Frame space" is the origin of a frame, aka the
   * top-left corner of its border box, aka the top left corner of its mRect.
   */
  gfxMatrix GetUserSpaceToFrameSpaceInCSSPxTransform() const;

  bool ComputeTargetBBoxInFilterSpace();

  /**
   * The frame for the element that is currently being filtered.
   */
  nsIFrame* mTargetFrame;

  /**
   * The filtered element.
   */
  nsIContent* mTargetContent;

  /**
   * The user space metrics of the filtered frame.
   */
  const UserSpaceMetrics& mMetrics;

  const SVGFilterPaintCallback& mPaintCallback;

  /**
   * The SVG bbox of the element that is being filtered, in user space.
   */
  gfxRect mTargetBBox;

  /**
   * The SVG bbox of the element that is being filtered, in filter space.
   */
  nsIntRect mTargetBBoxInFilterSpace;

  /**
   * The SVG filter element rect, in filter space, may be non-integer.
   */
  gfxRect mFilterSpaceBoundsNotSnapped;

  /**
   * Transform rects between filter space and frame space in CSS pixels.
   */
  gfxMatrix mFilterSpaceToFrameSpaceInCSSPxTransform;
  gfxMatrix mFrameSpaceInCSSPxToFilterSpaceTransform;

  /**
   * The scale factors between user space and filter space.
   */
  gfx::MatrixScalesDouble mUserSpaceToFilterSpaceScale;
  gfx::MatrixScalesDouble mFilterSpaceToUserSpaceScale;

  /**
   * Pre-filter paint bounds of the element that is being filtered, in filter
   * space.
   */
  nsIntRect mTargetBounds;

  /**
   * The dirty area that needs to be repainted, in filter space.
   */
  nsIntRegion mPostFilterDirtyRegion;

  /**
   * The pre-filter area of the filtered element that changed, in filter space.
   */
  nsIntRegion mPreFilterDirtyRegion;

  SourceInfo mSourceGraphic;
  SourceInfo mFillPaint;
  SourceInfo mStrokePaint;

  /**
   * The transform to the SVG user space of mTargetFrame.
   */
  gfxMatrix mPaintTransform;

  nsTArray<RefPtr<SourceSurface>> mInputImages;
  FilterDescription mFilterDescription;
  bool mInitialized;
};

}  // namespace mozilla

#endif  // LAYOUT_SVG_FILTERINSTANCE_H_
