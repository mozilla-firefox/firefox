/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGGeometryFrame.h"

// Keep others in (case-insensitive) order:
#include "SVGAnimatedTransformList.h"
#include "SVGGradientFrame.h"
#include "SVGMarkerFrame.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/SVGContextPaint.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_svg.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "mozilla/dom/SVGGraphicsElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGGeometryFrame(mozilla::PresShell* aPresShell,
                                 mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGGeometryFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGGeometryFrame)

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(SVGGeometryFrame)
  NS_QUERYFRAME_ENTRY(ISVGDisplayableFrame)
  NS_QUERYFRAME_ENTRY(SVGGeometryFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsIFrame)

//----------------------------------------------------------------------
// nsIFrame methods

void SVGGeometryFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  AddStateBits(aParent->GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD);
  nsIFrame::Init(aContent, aParent, aPrevInFlow);
}

nsresult SVGGeometryFrame::AttributeChanged(int32_t aNameSpaceID,
                                            nsAtom* aAttribute, AttrModType) {
  // We don't invalidate for transform changes (the layers code does that).
  // Also note that SVGTransformableElement::GetAttributeChangeHint will
  // return nsChangeHint_UpdateOverflow for "transform" attribute changes
  // and cause DoApplyRenderingChangeToTree to make the SchedulePaint call.

  if (aNameSpaceID == kNameSpaceID_None &&
      (static_cast<SVGGeometryElement*>(GetContent())
           ->AttributeDefinesGeometry(aAttribute))) {
    nsLayoutUtils::PostRestyleEvent(mContent->AsElement(), RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
    SVGUtils::ScheduleReflowSVG(this);
  }
  return NS_OK;
}

/* virtual */
void SVGGeometryFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  nsIFrame::DidSetComputedStyle(aOldComputedStyle);
  if (StyleSVGReset()->HasNonScalingStroke() &&
      (!aOldComputedStyle ||
       !aOldComputedStyle->StyleSVGReset()->HasNonScalingStroke())) {
    SVGUtils::UpdateNonScalingStrokeStateBit(this);
  }
  auto* element = static_cast<SVGGeometryElement*>(GetContent());
  if (!aOldComputedStyle) {
    element->ClearAnyCachedPath();
    return;
  }

  const auto* oldStyleSVG = aOldComputedStyle->StyleSVG();
  if (!SVGContentUtils::ShapeTypeHasNoCorners(GetContent())) {
    if (StyleSVG()->mStrokeLinecap != oldStyleSVG->mStrokeLinecap &&
        element->IsSVGElement(nsGkAtoms::path)) {
      // If the stroke-linecap changes to or from "butt" then our element
      // needs to update its cached Moz2D Path, since SVGPathData::BuildPath
      // decides whether or not to insert little lines into the path for zero
      // length subpaths base on that property.
      element->ClearAnyCachedPath();
    } else if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
      if (StyleSVG()->mClipRule != oldStyleSVG->mClipRule) {
        // Moz2D Path objects are fill-rule specific.
        // For clipPath we use clip-rule as the path's fill-rule.
        element->ClearAnyCachedPath();
      }
    } else if (StyleSVG()->mFillRule != oldStyleSVG->mFillRule) {
      // Moz2D Path objects are fill-rule specific.
      element->ClearAnyCachedPath();
    }
  }

  if (StyleDisplay()->CalcTransformPropertyDifference(
          *aOldComputedStyle->StyleDisplay())) {
    NotifySVGChanged(ChangeFlag::TransformChanged);
  }

  if (element->IsGeometryChangedViaCSS(*Style(), *aOldComputedStyle) ||
      aOldComputedStyle->EffectiveZoom() != Style()->EffectiveZoom()) {
    element->ClearAnyCachedPath();
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }
}

bool SVGGeometryFrame::DoGetParentSVGTransforms(
    gfx::Matrix* aFromParentTransform) const {
  return SVGUtils::GetParentSVGTransforms(this, aFromParentTransform);
}

void SVGGeometryFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                        const nsDisplayListSet& aLists) {
  if (!static_cast<const SVGElement*>(GetContent())->HasValidDimensions()) {
    return;
  }

  if (aBuilder->IsForPainting()) {
    if (!IsVisibleForPainting()) {
      return;
    }
    if (StyleEffects()->IsTransparent() && SVGUtils::CanOptimizeOpacity(this)) {
      return;
    }
    const auto* styleSVG = StyleSVG();
    if (styleSVG->mFill.kind.IsNone() && styleSVG->mStroke.kind.IsNone() &&
        !styleSVG->HasMarker()) {
      return;
    }

    aBuilder->BuildCompositorHitTestInfoIfNeeded(this,
                                                 aLists.BorderBackground());
  }

  DisplayOutline(aBuilder, aLists);
  aLists.Content()->AppendNewToTop<DisplaySVGGeometry>(aBuilder, this);
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

void SVGGeometryFrame::PaintSVG(gfxContext& aContext,
                                const gfxMatrix& aTransform,
                                imgDrawingParams& aImgParams) {
  if (!StyleVisibility()->IsVisible()) {
    return;
  }

  // Matrix to the geometry's user space:
  gfxMatrix newMatrix =
      aContext.CurrentMatrixDouble().PreMultiply(aTransform).NudgeToIntegers();
  if (newMatrix.IsSingular()) {
    return;
  }

  uint32_t paintOrder = StyleSVG()->mPaintOrder;
  if (!paintOrder) {
    Render(&aContext, RenderFlags(RenderFlag::Fill, RenderFlag::Stroke),
           newMatrix, aImgParams);
    PaintMarkers(aContext, aTransform, aImgParams);
  } else {
    while (paintOrder) {
      auto component = StylePaintOrder(paintOrder & kPaintOrderMask);
      switch (component) {
        case StylePaintOrder::Fill:
          Render(&aContext, RenderFlag::Fill, newMatrix, aImgParams);
          break;
        case StylePaintOrder::Stroke:
          Render(&aContext, RenderFlag::Stroke, newMatrix, aImgParams);
          break;
        case StylePaintOrder::Markers:
          PaintMarkers(aContext, aTransform, aImgParams);
          break;
        default:
          MOZ_FALLTHROUGH_ASSERT("Unknown paint-order variant, how?");
        case StylePaintOrder::Normal:
          break;
      }
      paintOrder >>= kPaintOrderShift;
    }
  }
}

nsIFrame* SVGGeometryFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  FillRule fillRule;
  SVGHitTestFlags hitTestFlags;
  if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
    hitTestFlags = SVGHitTestFlag::Fill;
    fillRule = SVGUtils::ToFillRule(StyleSVG()->mClipRule);
  } else {
    hitTestFlags = SVGUtils::GetGeometryHitTestFlags(this);
    if (hitTestFlags.isEmpty()) {
      return nullptr;
    }
    fillRule = SVGUtils::ToFillRule(StyleSVG()->mFillRule);
  }

  bool isHit = false;

  SVGGeometryElement* content = static_cast<SVGGeometryElement*>(GetContent());

  // Using ScreenReferenceDrawTarget() opens us to Moz2D backend specific hit-
  // testing bugs. Maybe we should use a BackendType::CAIRO DT for hit-testing
  // so that we get more consistent/backwards compatible results?
  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  RefPtr<Path> path = content->GetOrBuildPath(drawTarget, fillRule);
  if (!path) {
    return nullptr;  // no path, so we don't paint anything that can be hit
  }

  if (hitTestFlags.contains(SVGHitTestFlag::Fill)) {
    isHit = path->ContainsPoint(ToPoint(aPoint), {});
  }
  if (!isHit && hitTestFlags.contains(SVGHitTestFlag::Stroke)) {
    Point point = ToPoint(aPoint);
    SVGContentUtils::AutoStrokeOptions stroke;
    SVGContentUtils::GetStrokeOptions(&stroke, content, Style(), nullptr);
    if (Maybe<gfxMatrix> userToOuterSVG =
            SVGUtils::GetNonScalingStrokeTransform(this)) {
      // We need to transform the path back into the appropriate ancestor
      // coordinate system in order for non-scaled stroke to be correct.
      // Naturally we also need to transform the point into the same
      // coordinate system in order to hit-test against the path.
      Matrix m = ToMatrix(*userToOuterSVG);
      point = m.TransformPoint(point);
      Path::TransformAndSetFillRule(path, m, fillRule);
    }
    isHit = path->StrokeContainsPoint(stroke, point, {});
  }

  if (isHit && SVGUtils::HitTestClip(this, aPoint)) {
    return this;
  }

  return nullptr;
}

bool SVGGeometryFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  if (!StyleSVG()->mStroke.kind.IsNone()) {
    // Stroke geometry is included in our overflow rect, nothing to do.
    return true;
  }
  // We might need to hit test the stroke, so need to track that as
  // ink-overflow.
  float inkOverflowInflation = SVGUtils::GetStrokeWidth(
      this, SVGContextPaint::GetContextPaint(GetContent()));
  if (inkOverflowInflation > 0.0f) {
    aOverflowAreas.InkOverflow().Inflate(
        CSSPixel::ToAppUnits(inkOverflowInflation));
  }
  return true;
}

void SVGGeometryFrame::ReflowSVG() {
  NS_ASSERTION(SVGUtils::OuterSVGIsCallingReflowSVG(this),
               "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    return;
  }

  // Speed is more important than accuracy here - we don't care if the stroke
  // bounds are slightly too large so we set EstimateStrokeBounds.
  SVGBBoxFlags flags = {SVGBBoxFlag::IncludeFillGeometry,
                        SVGBBoxFlag::IncludeStroke, SVGBBoxFlag::IncludeMarkers,
                        SVGBBoxFlag::EstimateStrokeBounds};
  if (!StyleSVG()->mStroke.kind.IsNone()) {
    flags += SVGBBoxFlag::IncludeStrokeGeometry;
  }
  SVGBBox extent = GetBBoxContribution({}, flags);
  mRect = nsLayoutUtils::RoundGfxRectToAppRect(extent.ToThebesRect(),
                                               AppUnitsPerCSSPixel());

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);
  }

  const nsRect overflowRect(nsPoint(), mRect.Size());
  OverflowAreas overflowAreas(overflowRect, overflowRect);
  ComputeCustomOverflow(overflowAreas);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);

  // Invalidate, but only if this is not our first reflow (since if it is our
  // first reflow then we haven't had our first paint yet).
  if (!GetParent()->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    InvalidateFrame();
  }
}

void SVGGeometryFrame::NotifySVGChanged(ChangeFlags aFlags) {
  MOZ_ASSERT(aFlags.contains(ChangeFlag::TransformChanged) ||
                 aFlags.contains(ChangeFlag::CoordContextChanged),
             "Invalidation logic may need adjusting");

  // Changes to our ancestors may affect how we render when we are rendered as
  // part of our ancestor (specifically, if our coordinate context changes size
  // and we have percentage lengths defining our geometry, then we need to be
  // reflowed). However, ancestor changes cannot affect how we render when we
  // are rendered as part of any rendering observers that we may have.
  // Therefore no need to notify rendering observers here.

  // Don't try to be too smart trying to avoid the ScheduleReflowSVG calls
  // for the stroke properties examined below. Checking HasStroke() is not
  // enough, since what we care about is whether we include the stroke in our
  // overflow rects or not, and we sometimes deliberately include stroke
  // when it's not visible. See the complexities of GetBBoxContribution.

  if (aFlags.contains(ChangeFlag::CoordContextChanged)) {
    auto* geom = static_cast<SVGGeometryElement*>(GetContent());
    // Stroke currently contributes to our mRect, which is why we have to take
    // account of stroke-width here.
    if (geom->GeometryDependsOnCoordCtx()) {
      geom->ClearAnyCachedPath();
      SVGUtils::ScheduleReflowSVG(this);
    } else if (SVGContentUtils::HasPercentageDependentStroke(
                   Style(), SVGContextPaint::GetContextPaint(geom)) ||
               (StyleSVG()->HasMarker() && geom->IsMarkable()) ||
               SVGIntegrationUtils::UsingEffectsForFrame(this)) {
      // Stroke, effects and markers may have percentage dependent units.
      SVGUtils::ScheduleReflowSVG(this);
    }
  }

  if (aFlags.contains(ChangeFlag::TransformChanged) &&
      StyleSVGReset()->HasNonScalingStroke()) {
    // Stroke currently contributes to our mRect, and our stroke depends on
    // the transform to our outer-<svg> if |vector-effect:non-scaling-stroke|.
    SVGUtils::ScheduleReflowSVG(this);
  }
}

SVGBBox SVGGeometryFrame::GetBBoxContribution(const Matrix& aToBBoxUserspace,
                                              SVGBBoxFlags aFlags) {
  SVGBBox bbox;

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return bbox;
  }

  if (aFlags.contains(SVGBBoxFlag::ForGetClientRects) &&
      aToBBoxUserspace.PreservesAxisAlignedRectangles()) {
    if (!mRect.IsEmpty()) {
      Rect rect = NSRectToRect(mRect, AppUnitsPerCSSPixel());
      bbox = aToBBoxUserspace.TransformBounds(rect);
    }
    return bbox;
  }

  SVGGeometryElement* element = static_cast<SVGGeometryElement*>(GetContent());

  const bool getFill = aFlags.contains(SVGBBoxFlag::IncludeFillGeometry);

  const bool getStroke =
      (aFlags.contains(SVGBBoxFlag::IncludeStrokeGeometry) ||
       (aFlags.contains(SVGBBoxFlag::IncludeStroke) &&
        SVGUtils::HasStroke(this))) &&
      // If this frame has non-scaling-stroke and we would like to compute its
      // stroke, it may cause a potential cyclical dependency if the caller is
      // for transform. In this case, we have to fall back to fill-box, so make
      // |getStroke| be false.
      // https://github.com/w3c/csswg-drafts/issues/9640
      //
      // Note:
      // 1. We don't care about the computation of the markers below in this
      //    function because we know the callers don't set
      //    SVGUtils::eBBoxIncludeMarkers.
      //    See nsStyleTransformMatrix::GetSVGBox() and
      //    MotionPathUtils::GetRayContainReferenceSize() for more details.
      // 2. We have to break the dependency here *again* because the geometry
      //    frame may be in the subtree of a SVGContainerFrame, which may not
      //    set non-scaling-stroke.
      !(StyleSVGReset()->HasNonScalingStroke() &&
        aFlags.contains(SVGBBoxFlag::AvoidCycleIfNonScalingStroke));

  Maybe<gfxMatrix> userToOuterSVG;
  SVGContentUtils::AutoStrokeOptions strokeOptions;
  if (getStroke) {
    userToOuterSVG = SVGUtils::GetNonScalingStrokeTransform(this);
    SVGContentUtils::GetStrokeOptions(
        &strokeOptions, element, Style(), nullptr,
        SVGContentUtils::StrokeOptionFlag::IgnoreStrokeDashing);
  } else {
    // Override the default line width of 1.f so that when we call
    // GetGeometryBounds below the result doesn't include stroke bounds.
    strokeOptions.mLineWidth = 0.f;
  }

  Maybe<Rect> simpleBounds;
  if (getStroke && userToOuterSVG) {
    Matrix m = ToMatrix(*userToOuterSVG);
    if (m.IsSingular()) {
      return bbox;
    }
    simpleBounds =
        element->GetGeometryBounds(strokeOptions, aToBBoxUserspace, &m);
  } else if (getFill || getStroke) {
    simpleBounds = element->GetGeometryBounds(strokeOptions, aToBBoxUserspace);
  }

  if (simpleBounds) {
    bbox = *simpleBounds;
  } else {
    if (getFill && !getStroke) {
      Maybe<Rect> pathBBoxExtents = element->GetBounds(aToBBoxUserspace);
      if (!pathBBoxExtents) {
        // This can happen in the case that we only have a move-to command in
        // the path commands, in which case we know nothing gets rendered.
        return bbox;
      }
      bbox = *pathBBoxExtents;
    }

    if (getStroke) {
      // Be careful when replacing the following logic to get the fill and
      // stroke extents independently.
      // You may think that you can just use the stroke extents if
      // there is both a fill and a stroke. In reality it may be necessary to
      // calculate both the fill and stroke extents.
      // There are two reasons for this:
      //
      // # Due to stroke dashing, in certain cases the fill extents could
      //   actually extend outside the stroke extents.
      // # If the stroke is very thin, cairo won't paint any stroke, and so the
      //   stroke bounds that it will return will be empty.

      Maybe<Rect> strokeBBoxExtents;
      if (!aFlags.contains(SVGBBoxFlag::EstimateStrokeBounds) &&
          StaticPrefs::svg_Moz2D_strokeBounds_enabled()) {
        if (userToOuterSVG) {
          Matrix m = ToMatrix(*userToOuterSVG);
          Matrix outerSVGToBBox = aToBBoxUserspace * m.Inverse();
          strokeBBoxExtents =
              element->GetStrokedBounds(strokeOptions, m, outerSVGToBBox);
        } else {
          strokeBBoxExtents = element->GetStrokedBounds(strokeOptions, Matrix(),
                                                        aToBBoxUserspace);
        }
        if (!strokeBBoxExtents) {
          return bbox;
        }
        if (strokeBBoxExtents->IsEmpty() && getFill) {
          strokeBBoxExtents = element->GetBounds(aToBBoxUserspace);
          if (!strokeBBoxExtents) {
            return bbox;
          }
        }
      } else {
        Maybe<Rect> pathBBoxExtents = element->GetBounds(aToBBoxUserspace);
        if (!pathBBoxExtents) {
          return bbox;
        }
        strokeBBoxExtents = Some(ToRect(SVGUtils::PathExtentsToMaxStrokeExtents(
            ThebesRect(*pathBBoxExtents), this,
            ThebesMatrix(aToBBoxUserspace))));
        if (!strokeBBoxExtents->IsFinite()) {
          return bbox;
        }
      }
      bbox.UnionEdges(*strokeBBoxExtents);
    }
  }

  // Account for markers:
  if (aFlags.contains(SVGBBoxFlag::IncludeMarkers) && element->IsMarkable()) {
    SVGMarkerFrames markerFrames;
    if (SVGObserverUtils::GetAndObserveMarkers(this, &markerFrames)) {
      nsTArray<SVGMark> marks;
      element->GetMarkPoints(&marks);
      if (uint32_t num = marks.Length()) {
        float strokeWidth = SVGUtils::GetStrokeWidth(this);
        for (uint32_t i = 0; i < num; i++) {
          const SVGMark& mark = marks[i];
          SVGMarkerFrame* frame = markerFrames[mark.type];
          if (frame) {
            SVGBBox mbbox = frame->GetMarkBBoxContribution(
                aToBBoxUserspace, aFlags, this, mark, strokeWidth);
            if (!mbbox.IsFinite()) {
              return bbox;
            }
            bbox.UnionEdges(mbbox);
          }
        }
      }
    }
  }

  if (aFlags.contains(SVGBBoxFlag::DisregardCSSZoom)) {
    bbox.Scale(1 / Style()->EffectiveZoom().ToFloat());
  }

  return bbox;
}

//----------------------------------------------------------------------
// SVGGeometryFrame methods:

gfxMatrix SVGGeometryFrame::GetCanvasTM() {
  NS_ASSERTION(GetParent(), "null parent");

  auto* parent = static_cast<SVGContainerFrame*>(GetParent());
  auto* content = static_cast<SVGGraphicsElement*>(GetContent());
  return content->ChildToUserSpaceTransform() * parent->GetCanvasTM();
}

void SVGGeometryFrame::Render(gfxContext* aContext,
                              RenderFlags aRenderComponents,
                              const gfxMatrix& aTransform,
                              imgDrawingParams& aImgParams) {
  MOZ_ASSERT(!aTransform.IsSingular());

  DrawTarget* drawTarget = aContext->GetDrawTarget();

  MOZ_ASSERT(drawTarget);
  if (!drawTarget->IsValid()) {
    return;
  }

  FillRule fillRule = SVGUtils::ToFillRule(
      HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD) ? StyleSVG()->mClipRule
                                                   : StyleSVG()->mFillRule);

  SVGGeometryElement* element = static_cast<SVGGeometryElement*>(GetContent());

  AntialiasMode aaMode = SVGUtils::ToAntialiasMode(StyleSVG()->mShapeRendering);

  // We wait as late as possible before setting the transform so that we don't
  // set it unnecessarily if we return early (it's an expensive operation for
  // some backends).
  gfxContextMatrixAutoSaveRestore autoRestoreTransform(aContext);
  aContext->SetMatrixDouble(aTransform);

  if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
    // We don't complicate this code with GetAsSimpleShape since the cost of
    // masking will dwarf Path creation overhead anyway.
    RefPtr<Path> path = element->GetOrBuildPath(drawTarget, fillRule);
    if (path) {
      ColorPattern white(ToDeviceColor(sRGBColor(1.0f, 1.0f, 1.0f, 1.0f)));
      drawTarget->Fill(path, white,
                       DrawOptions(1.0f, CompositionOp::OP_OVER, aaMode));
    }
    return;
  }

  SVGGeometryElement::SimpleShape shape;
  RefPtr<Path> path;

  // DrawTarget has dedicated methods for rects and lines. Other shapes go
  // through a Path.
  element->GetAsSimpleShape(&shape);
  if (!shape.IsRect() && !shape.IsLine()) {
    shape.Reset();
    path = element->GetOrBuildPath(drawTarget, fillRule);
    if (!path) {
      return;
    }
  }

  SVGContextPaint* contextPaint =
      SVGContextPaint::GetContextPaint(GetContent());

  if (aRenderComponents.contains(RenderFlag::Fill)) {
    GeneralPattern fillPattern;
    SVGUtils::MakeFillPatternFor(this, aContext, &fillPattern, aImgParams,
                                 contextPaint);

    if (fillPattern.GetPattern()) {
      DrawOptions drawOptions(1.0f, CompositionOp::OP_OVER, aaMode);
      if (shape.IsRect()) {
        drawTarget->FillRect(shape.AsRect().ToUnknownRect(), fillPattern,
                             drawOptions);
      } else if (path) {
        drawTarget->Fill(path, fillPattern, drawOptions);
      }
    }
  }

  if (aRenderComponents.contains(RenderFlag::Stroke) &&
      SVGUtils::HasStroke(this, contextPaint)) {
    // Account for vector-effect:non-scaling-stroke:
    if (Maybe<gfxMatrix> userToOuterSVG =
            SVGUtils::GetNonScalingStrokeTransform(this)) {
      // A simple Rect can't be transformed with rotate/skew, so let's switch
      // to using a real path:
      if (!path) {
        path = element->GetOrBuildPath(drawTarget, fillRule);
        if (!path) {
          return;
        }
        shape.Reset();
      }
      // We need to transform the path back into the appropriate ancestor
      // coordinate system, and paint it it that coordinate system, in order
      // for non-scaled stroke to paint correctly.
      aContext->Multiply(userToOuterSVG->Inverse());
      Path::TransformAndSetFillRule(path, ToMatrix(*userToOuterSVG), fillRule);
    }
    GeneralPattern strokePattern;
    SVGUtils::MakeStrokePatternFor(this, aContext, &strokePattern, aImgParams,
                                   contextPaint);

    if (strokePattern.GetPattern()) {
      SVGContentUtils::AutoStrokeOptions strokeOptions;
      SVGContentUtils::GetStrokeOptions(&strokeOptions,
                                        static_cast<SVGElement*>(GetContent()),
                                        Style(), contextPaint);
      // GetStrokeOptions may set the line width to zero as an optimization
      if (strokeOptions.mLineWidth <= 0) {
        return;
      }
      DrawOptions drawOptions(1.0f, CompositionOp::OP_OVER, aaMode);
      if (shape.IsRect()) {
        drawTarget->StrokeRect(shape.AsRect().ToUnknownRect(), strokePattern,
                               strokeOptions, drawOptions);
      } else if (shape.IsLine()) {
        drawTarget->StrokeLine(shape.Point1().ToUnknownPoint(),
                               shape.Point2().ToUnknownPoint(), strokePattern,
                               strokeOptions, drawOptions);
      } else {
        drawTarget->Stroke(path, strokePattern, strokeOptions, drawOptions);
      }
    }
  }
}

bool SVGGeometryFrame::IsInvisible() const {
  if (!StyleVisibility()->IsVisible()) {
    return true;
  }

  // Anything below will round to zero later down the pipeline.
  constexpr float opacity_threshold = 1.0 / 128.0;

  if (StyleEffects()->mOpacity <= opacity_threshold &&
      SVGUtils::CanOptimizeOpacity(this)) {
    return true;
  }

  const nsStyleSVG* style = StyleSVG();
  SVGContextPaint* contextPaint =
      SVGContextPaint::GetContextPaint(GetContent());

  if (!style->mFill.kind.IsNone()) {
    float opacity = SVGUtils::GetOpacity(style->mFillOpacity, contextPaint);
    if (opacity > opacity_threshold) {
      return false;
    }
  }

  if (!style->mStroke.kind.IsNone()) {
    float opacity = SVGUtils::GetOpacity(style->mStrokeOpacity, contextPaint);
    if (opacity > opacity_threshold) {
      return false;
    }
  }

  if (style->HasMarker()) {
    return false;
  }

  return true;
}

// Whether aMatrix maps a rect to a parallelogram that is not a rectangle.
static bool HasSkew(const Matrix& aMatrix) {
  // The images of the x and y axes are not orthogonal.
  double x1 = aMatrix._11, y1 = aMatrix._12, x2 = aMatrix._21, y2 = aMatrix._22;
  double dot = x1 * x2 + y1 * y2;
  return dot * dot > 1e-8 * (x1 * x1 + y1 * y1) * (x2 * x2 + y2 * y2);
}

WebRenderCommandsResult SVGGeometryFrame::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, DisplaySVGGeometry* aItem,
    bool aDryRun, const Matrix& aTransform) {
  MOZ_ASSERT(StyleVisibility()->IsVisible());

  SVGGeometryElement* element = static_cast<SVGGeometryElement*>(GetContent());

  SVGGeometryElement::SimpleShape shape;
  element->GetAsSimpleShape(&shape);

  if (!shape.IsRoundedRect()) {
    return Err("shape is not a rect or an ellipse");
  }

  // Rounded corners and borders are rendered by WebRender from axis aligned
  // cached images; under a rotation or a skew their edges come out blurry, so
  // only plain rect fills are drawn in that case.
  const Matrix transform = aTransform * aSc.GetInheritedTransform();
  const bool axisAligned = !transform.HasNonAxisAlignedTransform();

  // WebRender anti-aliases each edge separately, which bleeds past the acute
  // corners that a skew gives to a rect.
  if (!axisAligned && HasSkew(transform)) {
    return Err("shape under a skew");
  }

  if (!shape.IsRect()) {
    if (!StaticPrefs::gfx_webrender_svg_shapes_ellipses()) {
      return Err("shape is not a simple rect");
    }
    // WebRender always anti-aliases rounded corners.
    if (HasCrispEdges()) {
      return Err("rounded shape with shape-rendering: crispEdges");
    }
    if (!axisAligned) {
      return Err("rounded shape under a rotation or skew");
    }
  }

  const nsStyleSVG* style = StyleSVG();
  MOZ_ASSERT(style);

  SVGContextPaint* contextPaint =
      SVGContextPaint::GetContextPaint(GetContent());

  const bool hasFill = !style->mFill.kind.IsNone();
  SVGGradientFrame* fillGradient = nullptr;
  if (hasFill && !style->mFill.kind.IsColor()) {
    if (!StaticPrefs::gfx_webrender_svg_shapes_gradients() ||
        !style->mFill.kind.IsPaintServer()) {
      return Err("fill is not a plain color");
    }
    fillGradient = do_QueryFrame(
        SVGObserverUtils::GetAndObservePaintServer(this, &nsStyleSVG::mFill));
    if (!fillGradient) {
      return Err("fill paint server is not a gradient");
    }
  }

  // Stroke parameters, resolved up front so that the dry run rejects the same
  // shapes as the real run.
  const bool hasStroke = SVGUtils::HasStroke(this, contextPaint);
  SVGContentUtils::AutoStrokeOptions strokeOptions;
  if (hasStroke) {
    if (!StaticPrefs::gfx_webrender_svg_shapes_strokes()) {
      return Err("stroke is not supported");
    }
    if (!axisAligned) {
      return Err("stroke under a rotation or skew");
    }
    if (!style->mStroke.kind.IsColor()) {
      return Err("stroke is not a plain color");
    }
    if (SVGUtils::GetNonScalingStrokeTransform(this)) {
      return Err("vector-effect: non-scaling-stroke is not supported");
    }
    SVGContentUtils::GetStrokeOptions(&strokeOptions, element, Style(),
                                      contextPaint);
    if (strokeOptions.mDashPattern) {
      return Err("dashed strokes are not supported");
    }
    if (shape.IsRect() && strokeOptions.mLineJoin == JoinStyle::BEVEL) {
      return Err("stroke-linejoin: bevel is not supported");
    }
    if (shape.IsRect() &&
        strokeOptions.mLineJoin == JoinStyle::MITER_OR_BEVEL &&
        strokeOptions.mMiterLimit < M_SQRT2) {
      // A miter limit below sqrt(2) turns the right angle joins of a rect
      // into bevels.
      return Err("stroke-miterlimit below sqrt(2) is not supported");
    }
    if (HasCrispEdges()) {
      // WebRender borders are always anti-aliased.
      return Err("stroke with shape-rendering: crispEdges is not supported");
    }
  } else if (shape.IsLine()) {
    // A line without a stroke draws nothing, but the item is still kept in
    // the blob to keep this code simple.
    return Err("line without a stroke");
  }

  if (shape.IsLine()) {
    CSSPoint p1 = shape.Point1();
    CSSPoint p2 = shape.Point2();
    if (p1.x != p2.x && p1.y != p2.y) {
      return Err("line is not axis aligned");
    }
  }

  // The blend itself is performed by the enclosing nsDisplayBlendMode item.
  if (StyleEffects()->HasMixBlendMode() &&
      !StaticPrefs::gfx_webrender_svg_shapes_blend()) {
    return Err("mix-blend-mode is not supported");
  }

  if (style->HasMarker() && element->IsMarkable()) {
    // Markers aren't suppported yet.
    return Err("markers are not supported");
  }

  if (fillGradient && !fillGradient->GetWebRenderGradient(this, 1.0f)) {
    return Err("gradient is not expressible with WebRender");
  }

  if (aDryRun) {
    return Ok();
  }

  auto appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
  const CSSToLayoutDeviceScale cssToDev = PresContext()->CSSToDevPixelScale();
  auto offset = LayoutDevicePoint::FromAppUnits(
      aItem->ToReferenceFrame() - GetPosition(), appUnitsPerDevPx);

  // Converts a rect from this frame's user space to device space relative to
  // the reference frame.
  auto toDevice = [&](const CSSRect& aRect) {
    return wr::ToLayoutRect(aRect * cssToDev + offset);
  };

  // When the shape has both a fill and a stroke, CanOptimizeOpacity is false
  // and the element opacity is applied by an enclosing nsDisplayOpacity, so
  // folding it into the colors here is only done when there is a single
  // paint component.
  float elemOpacity = 1.0f;
  if (SVGUtils::CanOptimizeOpacity(this)) {
    elemOpacity = StyleEffects()->mOpacity;
  }

  const bool backfaceVisible = !aItem->BackfaceIsHidden();
  const bool antialias = ShouldAntiAlias();

  // Fills the shape with a solid color. A rounded shape is a WebRender border
  // whose curve the border shader already anti-aliases, and it keeps the
  // device pixel snapping that makes its cached corner textures composite
  // exactly, so only the rect case carries the anti-aliasing flag.
  auto pushSolidFill = [&](wr::ColorF aColor) {
    wr::LayoutRect wrRect = toDevice(shape.AsRect());
    if (shape.IsRect()) {
      aBuilder.PushRect(wrRect, wrRect, backfaceVisible, antialias, false,
                        aColor);
    } else {
      aBuilder.PushRoundedRect(wrRect, wrRect, backfaceVisible,
                               wr::ToLayoutSize(shape.Radii() * cssToDev),
                               aColor);
    }
  };

  // Fills the shape with a gradient. Rounded shapes clip the gradient with a
  // rounded rect clip.
  auto pushGradientFill = [&](const SVGGradientFrame::WebRenderGradient& aG) {
    wr::LayoutRect wrRect = toDevice(shape.AsRect());
    Maybe<wr::SpaceAndClipChainHelper> clipHelper;
    if (!shape.IsRect()) {
      wr::ComplexClipRegion region;
      region.rect = wrRect;
      region.radii = wr::EmptyBorderRadius();
      wr::LayoutSize r = wr::ToLayoutSize(shape.Radii() * cssToDev);
      region.radii.top_left = r;
      region.radii.top_right = r;
      region.radii.bottom_left = r;
      region.radii.bottom_right = r;
      region.inset = wr::EmptyLayoutSideOffsets();
      region.mode = wr::ClipMode::Clip;
      wr::WrClipId clipId = aBuilder.DefineRoundedRectClip(Nothing(), region);
      wr::WrClipChainId chain = aBuilder.DefineClipChain(
          {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());
      clipHelper.emplace(aBuilder, chain);
    }

    // WebRender gradient points are relative to the item's origin.
    CSSPoint origin = shape.AsRect().TopLeft();
    auto toItemVector = [&](const CSSPoint& aPoint) {
      return wr::ToLayoutVector2D((aPoint - origin) * cssToDev);
    };
    wr::LayoutSize tileSize{wrRect.width(), wrRect.height()};
    wr::LayoutSize tileSpacing{0.0f, 0.0f};
    switch (aG.mType) {
      case SVGGradientFrame::GradientType::Linear:
        aBuilder.PushLinearGradient(
            wrRect, wrRect, backfaceVisible, toItemVector(aG.mStart),
            toItemVector(aG.mEnd), aG.mStops, aG.mExtendMode, tileSize,
            tileSpacing, antialias, wr::AlphaMode::Unpremultiplied);
        break;
      case SVGGradientFrame::GradientType::Radial:
        aBuilder.PushRadialGradient(
            wrRect, wrRect, backfaceVisible, toItemVector(aG.mCenter),
            wr::ToLayoutSize(aG.mRadii * cssToDev), aG.mStops, aG.mExtendMode,
            tileSize, tileSpacing, antialias, wr::AlphaMode::Unpremultiplied);
        break;
    }
  };

  auto paintFill = [&]() {
    if (!hasFill) {
      return;
    }
    float fillOpacity = SVGUtils::GetOpacity(style->mFillOpacity, contextPaint);
    float opacity = elemOpacity * fillOpacity;
    if (opacity <= 0.0f) {
      return;
    }

    if (fillGradient) {
      Maybe<SVGGradientFrame::WebRenderGradient> gradient =
          fillGradient->GetWebRenderGradient(this, opacity);
      if (!gradient) {
        return;
      }
      if (gradient->mSolidColor) {
        if (gradient->mSolidColor->a > 0.0f) {
          pushSolidFill(wr::ToColorF(*gradient->mSolidColor));
        }
      } else {
        pushGradientFill(*gradient);
      }
      return;
    }

    auto color = wr::ToColorF(
        ToDeviceColor(style->mFill.kind.AsColor().CalcColor(this)));
    color.a *= opacity;
    pushSolidFill(color);
  };

  auto paintStroke = [&]() {
    if (!hasStroke) {
      return;
    }
    float strokeOpacity =
        SVGUtils::GetOpacity(style->mStrokeOpacity, contextPaint);
    auto color = wr::ToColorF(
        ToDeviceColor(style->mStroke.kind.AsColor().CalcColor(this)));
    color.a *= elemOpacity * strokeOpacity;
    if (color.a <= 0.0f) {
      return;
    }

    const CSSCoord w = strokeOptions.mLineWidth;
    const CSSCoord halfW = w / 2;

    if (shape.IsLine()) {
      // An axis aligned line is a rectangle of thickness w. Square caps
      // extend it by half the width at each end; round caps do the same and
      // round the corners.
      CSSPoint p1 = shape.Point1();
      CSSPoint p2 = shape.Point2();
      CSSRect rect(std::min(p1.x, p2.x), std::min(p1.y, p2.y),
                   std::abs(p2.x - p1.x), std::abs(p2.y - p1.y));
      const bool horizontal = rect.height == 0;
      if (horizontal) {
        rect.Inflate(0, halfW);
      } else {
        rect.Inflate(halfW, 0);
      }
      if (strokeOptions.mLineCap != CapStyle::BUTT) {
        if (horizontal) {
          rect.Inflate(halfW, 0);
        } else {
          rect.Inflate(0, halfW);
        }
      }
      if (rect.IsEmpty()) {
        return;
      }
      wr::LayoutRect wrRect = toDevice(rect);
      if (strokeOptions.mLineCap == CapStyle::ROUND) {
        aBuilder.PushRoundedRect(
            wrRect, wrRect, backfaceVisible,
            wr::ToLayoutSize(CSSSize(halfW, halfW) * cssToDev), color);
      } else {
        aBuilder.PushRect(wrRect, wrRect, backfaceVisible, antialias, false,
                          color);
      }
      return;
    }

    // The stroke of a rounded rect is a border of width w around the rect
    // inflated by w / 2. Its outer corner radius is the shape's radius plus
    // half the stroke width. Sharp corners follow stroke-linejoin: miter
    // joins stay square, round joins get a radius of half the stroke width.
    CSSRect outer = shape.AsRect();
    outer.Inflate(halfW);
    CSSSize radii = shape.Radii();
    if (radii.width > 0 || radii.height > 0) {
      radii.width += halfW;
      radii.height += halfW;
    } else if (strokeOptions.mLineJoin == JoinStyle::ROUND) {
      radii = CSSSize(halfW, halfW);
    }

    wr::LayoutRect wrRect = toDevice(outer);
    LayoutDeviceCoord dw = w * cssToDev;
    wr::LayoutSideOffsets widths = {dw, dw, dw, dw};
    wr::LayoutSize r = wr::ToLayoutSize(radii * cssToDev);
    wr::BorderRadius wrRadii = {r, r, r, r, 1.0f, 1.0f, 1.0f, 1.0f};
    wr::BorderSide side = {color, wr::BorderStyle::Solid};
    wr::BorderSide sides[4] = {side, side, side, side};
    aBuilder.PushBorder(wrRect, wrRect, backfaceVisible, widths,
                        Range<const wr::BorderSide>(sides, 4), wrRadii,
                        wr::EmptyLayoutSideOffsets(), wr::AntialiasBorder::Yes,
                        antialias);
  };

  uint32_t paintOrder = style->mPaintOrder;
  if (!paintOrder) {
    paintFill();
    paintStroke();
  } else {
    while (paintOrder) {
      auto component = StylePaintOrder(paintOrder & kPaintOrderMask);
      switch (component) {
        case StylePaintOrder::Fill:
          paintFill();
          break;
        case StylePaintOrder::Stroke:
          paintStroke();
          break;
        default:
          break;
      }
      paintOrder >>= kPaintOrderShift;
    }
  }

  return Ok();
}

bool SVGGeometryFrame::HasCrispEdges() const {
  switch (StyleSVG()->mShapeRendering) {
    case StyleShapeRendering::Optimizespeed:
    case StyleShapeRendering::Crispedges:
      return true;
    default:
      return false;
  }
}

// shape-rendering: crispEdges / optimizeSpeed disables anti-aliasing, the same
// way it does for the gfxContext path in Render().
bool SVGGeometryFrame::ShouldAntiAlias() const { return !HasCrispEdges(); }

void SVGGeometryFrame::PaintMarkers(gfxContext& aContext,
                                    const gfxMatrix& aTransform,
                                    imgDrawingParams& aImgParams) {
  auto* element = static_cast<SVGGeometryElement*>(GetContent());
  if (!element->IsMarkable()) {
    return;
  }
  SVGMarkerFrames markerFrames;
  if (!SVGObserverUtils::GetAndObserveMarkers(this, &markerFrames)) {
    return;
  }
  nsTArray<SVGMark> marks;
  element->GetMarkPoints(&marks);
  if (marks.IsEmpty()) {
    return;
  }
  float strokeWidth = GetStrokeWidthForMarkers();
  for (const SVGMark& mark : marks) {
    if (auto* frame = markerFrames[mark.type]) {
      frame->PaintMark(aContext, aTransform, this, mark, strokeWidth,
                       aImgParams);
    }
  }
}

float SVGGeometryFrame::GetStrokeWidthForMarkers() {
  float strokeWidth = SVGUtils::GetStrokeWidth(
      this, SVGContextPaint::GetContextPaint(GetContent()));
  if (Maybe<gfxMatrix> userToOuterSVG =
          SVGUtils::GetNonScalingStrokeTransform(this)) {
    // We're not interested in any translation here so we can treat this as
    // Singular Value Decomposition (SVD) of a 2 x 2 matrix. That would give us
    // sx and sy values as the X and Y scales. The value we want is the XY
    // scale i.e. the normalised hypotenuse, which is sqrt(sx^2 + sy^2) /
    // sqrt(2). If we use the formulae from
    // https://scicomp.stackexchange.com/a/14103, we discover that the
    // normalised hypotenuse is simply the square root of the sum of the squares
    // of all the 2D matrix elements divided by sqrt(2).
    //
    // Note that this may need adjusting to support 3D transforms properly.

    const gfxMatrix& m = *userToOuterSVG;
    strokeWidth /= float(
        sqrt(m._11 * m._11 + m._12 * m._12 + m._21 * m._21 + m._22 * m._22) /
        M_SQRT2);
  }
  return strokeWidth;
}

}  // namespace mozilla
