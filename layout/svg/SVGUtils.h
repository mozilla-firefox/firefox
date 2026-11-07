/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LAYOUT_SVG_SVGUTILS_H_
#define LAYOUT_SVG_SVGUTILS_H_

// include math.h to pick up definition of M_ maths defines e.g. M_PI
#include <math.h>

#include <algorithm>

#include "DrawMode.h"
#include "ImgDrawResult.h"
#include "Units.h"
#include "gfx2DGlue.h"
#include "gfxMatrix.h"
#include "gfxPoint.h"
#include "gfxRect.h"
#include "mozilla/EnumSet.h"
#include "mozilla/ISVGDisplayableFrame.h"
#include "mozilla/gfx/Rect.h"
#include "nsCOMPtr.h"
#include "nsChangeHint.h"
#include "nsColor.h"
#include "nsID.h"
#include "nsIFrame.h"
#include "nsISupports.h"
#include "nsMathUtils.h"
#include "nsStyleStruct.h"

class gfxContext;
class nsFrameList;
class nsIContent;

class nsPresContext;
class nsTextFrame;

struct nsStyleSVG;
struct nsRect;

namespace mozilla {
class SVGAnimatedEnumeration;
class SVGAnimatedLength;
class SVGContextPaint;
class SVGDisplayContainerFrame;
class SVGGeometryFrame;
class SVGOuterSVGFrame;
namespace dom {
class Element;
class SVGElement;
class UserSpaceMetrics;
}  // namespace dom
namespace gfx {
class DrawTarget;
class GeneralPattern;
}  // namespace gfx
}  // namespace mozilla

bool NS_SVGNewGetBBoxEnabled();

namespace mozilla {

enum class SVGHitTestFlag { Fill, Stroke };
using SVGHitTestFlags = EnumSet<SVGHitTestFlag>;

/**
 * Sometimes we need to distinguish between an empty box and a box
 * that contains an element that has no size e.g. a point at the origin.
 */
class SVGBBox final {
  using Rect = gfx::Rect;

 public:
  SVGBBox() : mIsEmpty(true) {}

  MOZ_IMPLICIT SVGBBox(const Rect& aRect) : mBBox(aRect), mIsEmpty(false) {}

  MOZ_IMPLICIT SVGBBox(const gfxRect& aRect)
      : mBBox(ToRect(aRect)), mIsEmpty(false) {}

  operator const Rect&() { return mBBox; }

  gfxRect ToThebesRect() const { return ThebesRect(mBBox); }

  bool IsEmpty() const { return mIsEmpty; }

  bool IsFinite() const { return mBBox.IsFinite(); }

  void Scale(float aScale) { mBBox.Scale(aScale); }

  void UnionEdges(const SVGBBox& aSVGBBox) {
    if (aSVGBBox.mIsEmpty) {
      return;
    }
    mBBox = mIsEmpty ? aSVGBBox.mBBox : mBBox.UnionEdges(aSVGBBox.mBBox);
    mIsEmpty = false;
  }

  void Intersect(const SVGBBox& aSVGBBox) {
    if (!mIsEmpty && !aSVGBBox.mIsEmpty) {
      mBBox = mBBox.Intersect(aSVGBBox.mBBox);
      if (mBBox.IsEmpty()) {
        mIsEmpty = true;
        mBBox = Rect(0, 0, 0, 0);
      }
    } else {
      mIsEmpty = true;
      mBBox = Rect(0, 0, 0, 0);
    }
  }

 private:
  Rect mBBox;
  bool mIsEmpty;
};

// GRRR WINDOWS HATE HATE HATE
#undef CLIP_MASK

class MOZ_RAII SVGAutoRenderState final {
  using DrawTarget = gfx::DrawTarget;

 public:
  explicit SVGAutoRenderState(DrawTarget* aDrawTarget);
  ~SVGAutoRenderState();

  void SetPaintingToWindow(bool aPaintingToWindow);

  static bool IsPaintingToWindow(DrawTarget* aDrawTarget);

 private:
  DrawTarget* mDrawTarget;
  void* mOriginalRenderState;
  bool mPaintingToWindow;
};

/**
 * General functions used by all of SVG layout and possibly content code.
 * If a method is used by content and depends only on other content methods
 * it should go in SVGContentUtils instead.
 */
class SVGUtils final {
 public:
  using Element = dom::Element;
  using SVGElement = dom::SVGElement;
  using AntialiasMode = gfx::AntialiasMode;
  using DrawTarget = gfx::DrawTarget;
  using FillRule = gfx::FillRule;
  using GeneralPattern = gfx::GeneralPattern;
  using Size = gfx::Size;
  using imgDrawingParams = image::imgDrawingParams;

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(ObjectBoundingBoxProperty, gfxRect)

  /**
   * Returns the frame's post-filter ink overflow rect when passed the
   * frame's pre-filter ink overflow rect. If the frame is not currently
   * being filtered, this function simply returns aUnfilteredRect.
   */
  static nsRect GetPostFilterInkOverflowRect(nsIFrame* aFrame,
                                             const nsRect& aPreFilterRect);

  /**
   * Schedules an update of the frame's bounds (which will in turn invalidate
   * the new area that the frame should paint to).
   *
   * This does nothing when passed an NS_FRAME_IS_NONDISPLAY frame.
   * In future we may want to allow ReflowSVG to be called on such frames,
   * but that would be better implemented as a ForceReflowSVG function to
   * be called synchronously while painting them without marking or paying
   * attention to dirty bits like this function.
   *
   * This is very similar to PresShell::FrameNeedsReflow. The main reason that
   * we have this function instead of using FrameNeedsReflow is because we need
   * to be able to call it under SVGOuterSVGFrame::NotifyViewportChange when
   * that function is called by SVGOuterSVGFrame::Reflow. FrameNeedsReflow
   * is not suitable for calling during reflow though, and it asserts as much.
   * The reason that we want to be callable under NotifyViewportChange is
   * because we want to synchronously notify and dirty the SVGOuterSVGFrame's
   * children so that when SVGOuterSVGFrame::DidReflow is called its children
   * will be updated for the new size as appropriate. Otherwise we'd have to
   * post an event to the event loop to mark dirty flags and request an update.
   *
   * Another reason that we don't currently want to call
   * PresShell::FrameNeedsReflow is because passing eRestyle to it to get it to
   * mark descendants dirty would cause it to descend through
   * SVGForeignObjectFrame frames to mark their children dirty, but we want to
   * handle SVGForeignObjectFrame specially. It would also do unnecessary work
   * descending into NS_FRAME_IS_NONDISPLAY frames.
   */
  static void ScheduleReflowSVG(nsIFrame* aFrame);

  /**
   * Returns true if the frame or any of its children need ReflowSVG
   * to be called on them.
   */
  static bool NeedsReflowSVG(const nsIFrame* aFrame);

  /**
   * Percentage lengths in SVG are resolved against the width/height of the
   * nearest viewport (or its viewBox, if set). This helper returns the size
   * of this "context" for the given frame so that percentage values can be
   * resolved.
   */
  static Size GetContextSize(const nsIFrame* aFrame);

  /* Computes the input length in terms of object space coordinates.
     Input: rect - bounding box
            length - length to be converted
  */
  static float ObjectSpace(const gfxRect& aRect,
                           const dom::UserSpaceMetrics& aMetrics,
                           const SVGAnimatedLength* aLength);

  /* Computes the input length in terms of user space coordinates.
     Input: content - object to be used for determining user space
     Input: length - length to be converted
  */
  static float UserSpace(nsIFrame* aNonSVGContext,
                         const SVGAnimatedLength* aLength);
  static float UserSpace(const dom::UserSpaceMetrics& aMetrics,
                         const SVGAnimatedLength* aLength);

  /* Find the outermost SVG frame of the passed frame */
  static SVGOuterSVGFrame* GetOuterSVGFrame(nsIFrame* aFrame);

  /**
   * Get the covered region for a frame. Return null if it's not an SVG frame.
   * @param aRect gets a rectangle in app units
   * @return the outer SVG frame which aRect is relative to
   */
  static nsIFrame* GetOuterSVGFrameAndCoveredRegion(nsIFrame* aFrame,
                                                    nsRect* aRect);

  /* Paint SVG frame with SVG effects
   */
  static void PaintFrameWithEffects(nsIFrame* aFrame, gfxContext& aContext,
                                    const gfxMatrix& aTransform,
                                    imgDrawingParams& aImgParams);

  /* Hit testing - check if point hits the clipPath of indicated
   * frame.  Returns true if no clipPath set. */
  static bool HitTestClip(nsIFrame* aFrame, const gfxPoint& aPoint);

  /*
   * Returns the CanvasTM of the indicated frame, whether it's a
   * child SVG frame, container SVG frame, or a regular frame.
   * For regular frames, we just return an identity matrix.
   */
  static gfxMatrix GetCanvasTM(nsIFrame* aFrame);

  /*
   * Returns whether the frame is transformed and what those transforms are.
   */
  static bool GetParentSVGTransforms(const nsIFrame* aFrame,
                                     gfx::Matrix* aFromParentTransform);

  /**
   * Notify the descendants of aFrame of a change to one of their ancestors
   * that might affect them.
   */
  static void NotifyChildrenOfSVGChange(
      nsIFrame* aFrame, ISVGDisplayableFrame::ChangeFlags aFlags);

  /*
   * Convert a surface size to an integer for use by thebes
   * possibly making it smaller in the process so the surface does not
   * use excessive memory.
   *
   * @param aSize the desired surface size
   * @param aResultOverflows true if the desired surface size is too big
   * @return the surface size to use
   */
  static gfx::IntSize ConvertToSurfaceSize(const gfxSize& aSize,
                                           bool* aResultOverflows);

  /*
   * Hit test a given rectangle/matrix.
   */
  static bool HitTestRect(const gfx::Matrix& aMatrix, float aRX, float aRY,
                          float aRWidth, float aRHeight, float aX, float aY);

  /**
   * Get the clip rect for the given frame, taking into account the CSS 'clip'
   * property. See:
   * http://www.w3.org/TR/SVG11/masking.html#OverflowAndClipProperties
   * The arguments for aX, aY, aWidth and aHeight should be the dimensions of
   * the viewport established by aFrame.
   */
  static gfxRect GetClipRectForFrame(const nsIFrame* aFrame, float aX, float aY,
                                     float aWidth, float aHeight,
                                     SVGBBoxFlags aFlags = {});

  /* Using group opacity instead of fill or stroke opacity on a
   * geometry object seems to be a common authoring mistake.  If we're
   * not applying filters and not both stroking and filling, we can
   * generate the same result without going through the overhead of a
   * push/pop group. */
  static bool CanOptimizeOpacity(const nsIFrame* aFrame);

  /**
   * Take the CTM to userspace for an element, and adjust it to a CTM to its
   * object bounding box space if aUnits is SVG_UNIT_TYPE_OBJECTBOUNDINGBOX.
   * (I.e. so that [0,0] is at the top left of its bbox, and [1,1] is at the
   * bottom right of its bbox).
   *
   * If the bbox is empty, this will return a singular matrix.
   *
   * @param aFlags One or more of the SVGBBoxFlags values defined below.
   */
  static gfxMatrix AdjustMatrixForUnits(const gfxMatrix& aMatrix,
                                        const SVGAnimatedEnumeration* aUnits,
                                        nsIFrame* aFrame, SVGBBoxFlags aFlags);

  /**
   * This function in primarily for implementing the SVG DOM function getBBox()
   * and the SVG attribute value 'objectBoundingBox'.  However, it has been
   * extended with various extra parameters in order to become more of a
   * general purpose getter of all sorts of bounds that we might need to obtain
   * for SVG elements, or even for other elements that have SVG effects applied
   * to them.
   *
   * @param aFrame The frame of the element for which the bounds are to be
   *   obtained.
   * @param aFlags One or more of the SVGBBoxFlags values defined above.
   * @param aToBoundsSpace If not specified the returned rect is in aFrame's
   *   element's "user space". A matrix can optionally be pass to specify a
   *   transform from aFrame's user space to the bounds space of interest
   *   (typically this will be the ancestor SVGOuterSVGFrame, but it could be
   *   to any other coordinate space).
   */
  static gfxRect GetBBox(nsIFrame* aFrame,
                         // If the default arg changes, update the handling for
                         // ObjectBoundingBoxProperty() in the implementation.
                         SVGBBoxFlags aFlags = SVGBBoxFlag::IncludeFillGeometry,
                         const gfxMatrix* aToBoundsSpace = nullptr);

  /*
   * "User space" is the space that the frame's BBox (as calculated by
   * SVGUtils::GetBBox) is in. "Frame space" is the space that has its origin
   * at the top left of the union of the frame's border-box rects over all
   * continuations.
   * This function returns the offset one needs to add to something in frame
   * space in order to get its coordinates in user space.
   */
  static CSSPoint FrameSpaceInCSSPxToUserSpaceOffset(const nsIFrame* aFrame);

  /**
   * Convert a userSpaceOnUse/objectBoundingBoxUnits rectangle that's specified
   * using four SVGAnimatedLength values into a user unit rectangle in user
   * space.
   *
   * @param aXYWH pointer to 4 consecutive SVGAnimatedLength objects containing
   * the x, y, width and height values in that order
   * @param aBBox the bounding box of the object the rect is relative to;
   * may be null if aUnits is not SVG_UNIT_TYPE_OBJECTBOUNDINGBOX
   * @param aFrame the object in which to interpret user-space units;
   * may be null if aUnits is SVG_UNIT_TYPE_OBJECTBOUNDINGBOX
   */
  static gfxRect GetRelativeRect(uint16_t aUnits,
                                 const SVGAnimatedLength* aXYWH,
                                 const gfxRect& aBBox, nsIFrame* aFrame);

  static gfxRect GetRelativeRect(uint16_t aUnits,
                                 const SVGAnimatedLength* aXYWH,
                                 const gfxRect& aBBox,
                                 const SVGElement* aElement,
                                 const dom::UserSpaceMetrics& aMetrics);

  static bool OuterSVGIsCallingReflowSVG(nsIFrame* aFrame);
  static bool AnyOuterSVGIsCallingReflowSVG(nsIFrame* aFrame);

  /**
   * See https://svgwg.org/svg2-draft/painting.html#NonScalingStroke
   *
   * If the computed value of the 'vector-effect' property on aFrame is
   * 'non-scaling-stroke', then this function will return the
   * transform from aFrame's SVG user space to the initial coordinate system
   * established by the viewport of aFrame's outer-<svg>'s (the coordinate
   * system in which the stroke is fixed). If the transform is an identity
   * matrix or singular then Nothing() is returned.
   */
  static Maybe<gfxMatrix> GetNonScalingStrokeTransform(const nsIFrame* aFrame);

  /**
   * We need to track whether content has non-scaling-stroke because we can't
   * asynchronously animate it with a scaling transform.
   */
  static void UpdateNonScalingStrokeStateBit(nsIFrame* aFrame);

  /**
   * Compute the maximum possible device space stroke extents of a path given
   * the path's device space path extents, its stroke style and its ctm.
   *
   * This is a workaround for the lack of suitable cairo API for getting the
   * tight device space stroke extents of a path. This basically gives us the
   * tightest extents that we can guarantee fully enclose the inked stroke
   * without doing the calculations for the actual tight extents. We exploit
   * the fact that cairo does have an API for getting the tight device space
   * fill/path extents.
   *
   * This should die once bug 478152 is fixed.
   */
  static gfxRect PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                                               const nsTextFrame* aFrame,
                                               const gfxMatrix& aMatrix);
  static gfxRect PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                                               const SVGGeometryFrame* aFrame,
                                               const gfxMatrix& aMatrix);

  /**
   * Convert a floating-point value to a 32-bit integer value, clamping to
   * the range of valid integers.
   */
  static int32_t ClampToInt(double aVal) {
    return NS_lround(std::clamp(aVal, double(INT32_MIN), double(INT32_MAX)));
  }

  /**
   * Convert a floating-point value to a 64-bit integer value, clamping to
   * the lowest and highest integers that can be safely compared to a double.
   */
  static int64_t ClampToInt64(double aVal) {
    return static_cast<int64_t>(
        std::clamp<double>(aVal, INT64_MIN, std::nexttoward(INT64_MAX, 0)));
  }

  static nscolor GetFallbackOrPaintColor(
      const ComputedStyle&, StyleSVGPaint nsStyleSVG::* aFillOrStroke,
      nscolor aDefaultContextFallbackColor);

  static void MakeFillPatternFor(nsIFrame* aFrame, gfxContext* aContext,
                                 GeneralPattern* aOutPattern,
                                 imgDrawingParams& aImgParams,
                                 SVGContextPaint* aContextPaint = nullptr);

  static void MakeStrokePatternFor(nsIFrame* aFrame, gfxContext* aContext,
                                   GeneralPattern* aOutPattern,
                                   imgDrawingParams& aImgParams,
                                   SVGContextPaint* aContextPaint = nullptr);

  static float GetOpacity(const StyleSVGOpacity&, const SVGContextPaint*);

  /*
   * @return false if there is no stroke
   */
  static bool HasStroke(const nsIFrame* aFrame,
                        const SVGContextPaint* aContextPaint = nullptr);

  static float GetStrokeWidth(const nsIFrame* aFrame,
                              const SVGContextPaint* aContextPaint = nullptr);

  /*
   * Set up a context for a stroked path (including any dashing that applies).
   */
  static void SetupStrokeGeometry(nsIFrame* aFrame, gfxContext* aContext,
                                  SVGContextPaint* aContextPaint = nullptr);

  /**
   * This function returns a set of bit flags indicating which parts of the
   * element (fill, stroke, bounds) should intercept pointer events. It takes
   * into account the type of element and the value of the 'pointer-events'
   * property on the element.
   */
  static SVGHitTestFlags GetGeometryHitTestFlags(const nsIFrame* aFrame);

  static FillRule ToFillRule(StyleFillRule aFillRule) {
    return aFillRule == StyleFillRule::Evenodd ? FillRule::FILL_EVEN_ODD
                                               : FillRule::FILL_WINDING;
  }

  static AntialiasMode ToAntialiasMode(StyleTextRendering aTextRendering) {
    return aTextRendering == StyleTextRendering::Optimizespeed
               ? AntialiasMode::NONE
               : AntialiasMode::SUBPIXEL;
  }

  static AntialiasMode ToAntialiasMode(StyleShapeRendering aShapeRendering) {
    return (aShapeRendering == StyleShapeRendering::Optimizespeed ||
            aShapeRendering == StyleShapeRendering::Crispedges)
               ? AntialiasMode::NONE
               : AntialiasMode::SUBPIXEL;
  }

  /**
   * Render a SVG glyph.
   * @param aElement the SVG glyph element to render
   * @param aContext the thebes aContext to draw to
   * @param aImgParams imagelib parameters that may be used when painting
   * @return true if rendering succeeded
   */
  static void PaintSVGGlyph(Element* aElement, gfxContext* aContext,
                            imgDrawingParams& aImgParams);

  /**
   * Get the extents of a SVG glyph.
   * @param aElement the SVG glyph element
   * @param aSVGToAppSpace the matrix mapping the SVG glyph space to the
   *   target context space
   * @param aResult the result (valid when true is returned)
   * @return true if calculating the extents succeeded
   */
  static bool GetSVGGlyphExtents(const Element* aElement,
                                 const gfxMatrix& aSVGToAppSpace,
                                 gfxRect* aResult);

  /**
   * Returns the app unit canvas bounds of a userspace rect.
   *
   * @param aToCanvas Transform from userspace to canvas device space.
   */
  static nsRect ToCanvasBounds(const gfxRect& aUserspaceRect,
                               const gfxMatrix& aToCanvas,
                               const nsPresContext* presContext);

  struct MaskUsage;
  static MaskUsage DetermineMaskUsage(const nsIFrame* aFrame,
                                      bool aHandleOpacity);

  struct MOZ_STACK_CLASS MaskUsage {
    friend MaskUsage SVGUtils::DetermineMaskUsage(const nsIFrame* aFrame,
                                                  bool aHandleOpacity);

    bool ShouldGenerateMaskLayer() const { return mShouldGenerateMaskLayer; }

    bool ShouldGenerateClipMaskLayer() const {
      return mShouldGenerateClipMaskLayer;
    }

    bool ShouldGenerateLayer() const {
      return mShouldGenerateMaskLayer || mShouldGenerateClipMaskLayer;
    }

    bool ShouldGenerateMask() const {
      return mShouldGenerateMaskLayer || mShouldGenerateClipMaskLayer ||
             !IsOpaque();
    }

    bool ShouldApplyClipPath() const { return mShouldApplyClipPath; }

    bool HasSVGClip() const {
      return mShouldGenerateClipMaskLayer || mShouldApplyClipPath;
    }

    bool ShouldApplyBasicShapeOrPath() const {
      return mShouldApplyBasicShapeOrPath;
    }

    bool IsSimpleClipShape() const { return mIsSimpleClipShape; }

    bool IsOpaque() const { return mOpacity == 1.0f; }

    bool IsTransparent() const { return mOpacity == 0.0f; }

    float Opacity() const { return mOpacity; }

    bool UsingMaskOrClipPath() const {
      return mShouldGenerateMaskLayer || mShouldGenerateClipMaskLayer ||
             mShouldApplyClipPath || mShouldApplyBasicShapeOrPath;
    }

    bool ShouldDoSomething() const {
      return mShouldGenerateMaskLayer || mShouldGenerateClipMaskLayer ||
             mShouldApplyClipPath || mShouldApplyBasicShapeOrPath ||
             mOpacity != 1.0f;
    }

   private:
    MaskUsage() = default;

    float mOpacity = 0.0f;
    bool mShouldGenerateMaskLayer = false;
    bool mShouldGenerateClipMaskLayer = false;
    bool mShouldApplyClipPath = false;
    bool mShouldApplyBasicShapeOrPath = false;
    bool mIsSimpleClipShape = false;
  };

  static float ComputeOpacity(const nsIFrame* aFrame, bool aHandleOpacity);

  // Whether clip paths can be rendered with WebRender path clips.
  static bool UseWebRenderPaths();

  /**
   * SVG frames expect to paint in SVG user units, which are equal to CSS px
   * units. This method provides a transform matrix to multiply onto a
   * gfxContext's current transform to convert the context's current units from
   * its usual dev pixels to SVG user units/CSS px to keep the SVG code happy.
   */
  static gfxMatrix GetCSSPxToDevPxMatrix(const nsIFrame* aNonSVGFrame);
  static gfxMatrix GetTransformMatrixInUserSpace(const nsIFrame* aFrame);
};

}  // namespace mozilla

#endif  // LAYOUT_SVG_SVGUTILS_H_
