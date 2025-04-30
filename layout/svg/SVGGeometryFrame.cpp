/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGGeometryFrame.h"

// Keep others in (case-insensitive) order:
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "mozilla/dom/SVGGraphicsElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_svg.h"
#include "mozilla/SVGContextPaint.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "SVGAnimatedTransformList.h"
#include "SVGMarkerFrame.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGGeometryFrame(mozilla::PresShell* aPresShell,
                                 mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGGeometryFrame(aStyle, aPresShell->GetPresContext());
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
                                            nsAtom* aAttribute,
                                            int32_t aModType) {
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
    NotifySVGChanged(TRANSFORM_CHANGED);
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
    Render(&aContext, eRenderFill | eRenderStroke, newMatrix, aImgParams);
    PaintMarkers(aContext, aTransform, aImgParams);
  } else {
    while (paintOrder) {
      auto component = StylePaintOrder(paintOrder & kPaintOrderMask);
      switch (component) {
        case StylePaintOrder::Fill:
          Render(&aContext, eRenderFill, newMatrix, aImgParams);
          break;
        case StylePaintOrder::Stroke:
          Render(&aContext, eRenderStroke, newMatrix, aImgParams);
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
  uint16_t hitTestFlags;
  if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
    hitTestFlags = SVG_HIT_TEST_FILL;
    fillRule = SVGUtils::ToFillRule(StyleSVG()->mClipRule);
  } else {
    hitTestFlags = SVGUtils::GetGeometryHitTestFlags(this);
    if (!hitTestFlags) {
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

  if (hitTestFlags & SVG_HIT_TEST_FILL) {
    isHit = path->ContainsPoint(ToPoint(aPoint), {});
  }
  if (!isHit && (hitTestFlags & SVG_HIT_TEST_STROKE)) {
    Point point = ToPoint(aPoint);
    SVGContentUtils::AutoStrokeOptions stroke;
    SVGContentUtils::GetStrokeOptions(&stroke, content, Style(), nullptr);
    gfxMatrix userToOuterSVG;
    if (SVGUtils::GetNonScalingStrokeTransform(this, &userToOuterSVG)) {
      // We need to transform the path back into the appropriate ancestor
      // coordinate system in order for non-scaled stroke to be correct.
      // Naturally we also need to transform the point into the same
      // coordinate system in order to hit-test against the path.
      point = ToMatrix(userToOuterSVG).TransformPoint(point);
      Path::TransformAndSetFillRule(path, ToMatrix(userToOuterSVG), fillRule);
    }
    isHit = path->StrokeContainsPoint(stroke, point, {});
  }

  if (isHit && SVGUtils::HitTestClip(this, aPoint)) {
    return this;
  }

  return nullptr;
}

void SVGGeometryFrame::ReflowSVG() {
  NS_ASSERTION(SVGUtils::OuterSVGIsCallingReflowSVG(this),
               "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    return;
  }

  uint32_t flags = SVGUtils::eBBoxIncludeFill | SVGUtils::eBBoxIncludeStroke |
                   SVGUtils::eBBoxIncludeMarkers;
  // Our "visual" overflow rect needs to be valid for building display lists
  // for hit testing, which means that for certain values of 'pointer-events'
  // it needs to include the geometry of the fill or stroke even when the fill/
  // stroke don't actually render (e.g. when stroke="none" or
  // stroke-opacity="0"). GetGeometryHitTestFlags() accounts for
  // 'pointer-events'.
  uint16_t hitTestFlags = SVGUtils::GetGeometryHitTestFlags(this);
  if (hitTestFlags & SVG_HIT_TEST_FILL) {
    flags |= SVGUtils::eBBoxIncludeFillGeometry;
  }
  if (hitTestFlags & SVG_HIT_TEST_STROKE) {
    flags |= SVGUtils::eBBoxIncludeStrokeGeometry;
  }

  gfxRect extent = GetBBoxContribution({}, flags).ToThebesRect();
  mRect = nsLayoutUtils::RoundGfxRectToAppRect(extent, AppUnitsPerCSSPixel());

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);
  }

  nsRect overflow = nsRect(nsPoint(0, 0), mRect.Size());
  OverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);

  // Invalidate, but only if this is not our first reflow (since if it is our
  // first reflow then we haven't had our first paint yet).
  if (!GetParent()->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    InvalidateFrame();
  }
}

void SVGGeometryFrame::NotifySVGChanged(uint32_t aFlags) {
  MOZ_ASSERT(aFlags & (TRANSFORM_CHANGED | COORD_CONTEXT_CHANGED),
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

  if (aFlags & COORD_CONTEXT_CHANGED) {
    auto* geom = static_cast<SVGGeometryElement*>(GetContent());
    // Stroke currently contributes to our mRect, which is why we have to take
    // account of stroke-width here. Note that we do not need to take account
    // of stroke-dashoffset since, although that can have a percentage value
    // that is resolved against our coordinate context, it does not affect our
    // mRect.
    const auto& strokeWidth = StyleSVG()->mStrokeWidth;
    if (geom->GeometryDependsOnCoordCtx() ||
        (strokeWidth.IsLengthPercentage() &&
         strokeWidth.AsLengthPercentage().HasPercent())) {
      geom->ClearAnyCachedPath();
      SVGUtils::ScheduleReflowSVG(this);
    }
  }

  if ((aFlags & TRANSFORM_CHANGED) && StyleSVGReset()->HasNonScalingStroke()) {
    // Stroke currently contributes to our mRect, and our stroke depends on
    // the transform to our outer-<svg> if |vector-effect:non-scaling-stroke|.
    SVGUtils::ScheduleReflowSVG(this);
  }
}

SVGBBox SVGGeometryFrame::GetBBoxContribution(const Matrix& aToBBoxUserspace,
                                              uint32_t aFlags) {
  SVGBBox bbox;

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return bbox;
  }

  if ((aFlags & SVGUtils::eForGetClientRects) &&
      aToBBoxUserspace.PreservesAxisAlignedRectangles()) {
    Rect rect = NSRectToRect(mRect, AppUnitsPerCSSPixel());
    bbox = aToBBoxUserspace.TransformBounds(rect);
    return bbox;
  }

  SVGGeometryElement* element = static_cast<SVGGeometryElement*>(GetContent());

  const bool getFill = (aFlags & SVGUtils::eBBoxIncludeFillGeometry) ||
                       ((aFlags & SVGUtils::eBBoxIncludeFill) &&
                        !StyleSVG()->mFill.kind.IsNone());

  const bool getStroke =
      ((aFlags & SVGUtils::eBBoxIncludeStrokeGeometry) ||
       ((aFlags & SVGUtils::eBBoxIncludeStroke) &&
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
        (aFlags & SVGUtils::eAvoidCycleIfNonScalingStroke));

  SVGContentUtils::AutoStrokeOptions strokeOptions;
  if (getStroke) {
    SVGContentUtils::GetStrokeOptions(&strokeOptions, element, Style(), nullptr,
                                      SVGContentUtils::eIgnoreStrokeDashing);
  } else {
    // Override the default line width of 1.f so that when we call
    // GetGeometryBounds below the result doesn't include stroke bounds.
    strokeOptions.mLineWidth = 0.f;
  }

  Rect simpleBounds;
  bool gotSimpleBounds = false;
  gfxMatrix userToOuterSVG;
  if (getStroke &&
      SVGUtils::GetNonScalingStrokeTransform(this, &userToOuterSVG)) {
    Matrix moz2dUserToOuterSVG = ToMatrix(userToOuterSVG);
    if (moz2dUserToOuterSVG.IsSingular()) {
      return bbox;
    }
    gotSimpleBounds = element->GetGeometryBounds(
        &simpleBounds, strokeOptions, aToBBoxUserspace, &moz2dUserToOuterSVG);
  } else {
    gotSimpleBounds = element->GetGeometryBounds(&simpleBounds, strokeOptions,
                                                 aToBBoxUserspace);
  }

  if (gotSimpleBounds) {
    bbox = simpleBounds;
  } else {
    // Get the bounds using a Moz2D Path object (more expensive):
    RefPtr<DrawTarget> tmpDT;
    tmpDT = gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();

    FillRule fillRule = SVGUtils::ToFillRule(
        HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD) ? StyleSVG()->mClipRule
                                                     : StyleSVG()->mFillRule);
    RefPtr<Path> pathInUserSpace = element->GetOrBuildPath(tmpDT, fillRule);
    if (!pathInUserSpace) {
      return bbox;
    }
    RefPtr<Path> pathInBBoxSpace;
    if (aToBBoxUserspace.IsIdentity()) {
      pathInBBoxSpace = pathInUserSpace;
    } else {
      RefPtr<PathBuilder> builder =
          pathInUserSpace->TransformedCopyToBuilder(aToBBoxUserspace, fillRule);
      pathInBBoxSpace = builder->Finish();
      if (!pathInBBoxSpace) {
        return bbox;
      }
    }

    // Account for fill:
    if (getFill && !getStroke) {
      Rect pathBBoxExtents = pathInBBoxSpace->GetBounds();
      if (!pathBBoxExtents.IsFinite()) {
        // This can happen in the case that we only have a move-to command in
        // the path commands, in which case we know nothing gets rendered.
        return bbox;
      }
      bbox = pathBBoxExtents;
    }

    // Account for stroke:
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

      Rect strokeBBoxExtents;
      if (StaticPrefs::svg_Moz2D_strokeBounds_enabled()) {
        gfxMatrix userToOuterSVG;
        if (SVGUtils::GetNonScalingStrokeTransform(this, &userToOuterSVG)) {
          Matrix outerSVGToUser = ToMatrix(userToOuterSVG);
          outerSVGToUser.Invert();
          Matrix outerSVGToBBox = aToBBoxUserspace * outerSVGToUser;
          RefPtr<PathBuilder> builder =
              pathInUserSpace->TransformedCopyToBuilder(
                  ToMatrix(userToOuterSVG));
          RefPtr<Path> pathInOuterSVGSpace = builder->Finish();
          strokeBBoxExtents = pathInOuterSVGSpace->GetStrokedBounds(
              strokeOptions, outerSVGToBBox);
        } else {
          strokeBBoxExtents = pathInUserSpace->GetStrokedBounds(
              strokeOptions, aToBBoxUserspace);
        }
        if (strokeBBoxExtents.IsEmpty() && getFill) {
          strokeBBoxExtents = pathInBBoxSpace->GetBounds();
          if (!strokeBBoxExtents.IsFinite()) {
            return bbox;
          }
        }
      } else {
        Rect pathBBoxExtents = pathInBBoxSpace->GetBounds();
        if (!pathBBoxExtents.IsFinite()) {
          return bbox;
        }
        strokeBBoxExtents = ToRect(SVGUtils::PathExtentsToMaxStrokeExtents(
            ThebesRect(pathBBoxExtents), this, ThebesMatrix(aToBBoxUserspace)));
      }
      MOZ_ASSERT(strokeBBoxExtents.IsFinite(), "bbox is about to go bad");
      bbox.UnionEdges(strokeBBoxExtents);
    }
  }

  // Account for markers:
  if ((aFlags & SVGUtils::eBBoxIncludeMarkers) && element->IsMarkable()) {
    SVGMarkerFrame* markerFrames[SVGMark::eTypeCount];
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
            MOZ_ASSERT(mbbox.IsFinite(), "bbox is about to go bad");
            bbox.UnionEdges(mbbox);
          }
        }
      }
    }
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

void SVGGeometryFrame::Render(gfxContext* aContext, uint32_t aRenderComponents,
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
    // We don't complicate this code with GetAsSimplePath since the cost of
    // masking will dwarf Path creation overhead anyway.
    RefPtr<Path> path = element->GetOrBuildPath(drawTarget, fillRule);
    if (path) {
      ColorPattern white(ToDeviceColor(sRGBColor(1.0f, 1.0f, 1.0f, 1.0f)));
      drawTarget->Fill(path, white,
                       DrawOptions(1.0f, CompositionOp::OP_OVER, aaMode));
    }
    return;
  }

  SVGGeometryElement::SimplePath simplePath;
  RefPtr<Path> path;

  element->GetAsSimplePath(&simplePath);
  if (!simplePath.IsPath()) {
    path = element->GetOrBuildPath(drawTarget, fillRule);
    if (!path) {
      return;
    }
  }

  SVGContextPaint* contextPaint =
      SVGContextPaint::GetContextPaint(GetContent());

  if (aRenderComponents & eRenderFill) {
    GeneralPattern fillPattern;
    SVGUtils::MakeFillPatternFor(this, aContext, &fillPattern, aImgParams,
                                 contextPaint);

    if (fillPattern.GetPattern()) {
      DrawOptions drawOptions(1.0f, CompositionOp::OP_OVER, aaMode);
      if (simplePath.IsRect()) {
        drawTarget->FillRect(simplePath.AsRect(), fillPattern, drawOptions);
      } else if (path) {
        drawTarget->Fill(path, fillPattern, drawOptions);
      }
    }
  }

  if ((aRenderComponents & eRenderStroke) &&
      SVGUtils::HasStroke(this, contextPaint)) {
    // Account for vector-effect:non-scaling-stroke:
    gfxMatrix userToOuterSVG;
    if (SVGUtils::GetNonScalingStrokeTransform(this, &userToOuterSVG)) {
      // A simple Rect can't be transformed with rotate/skew, so let's switch
      // to using a real path:
      if (!path) {
        path = element->GetOrBuildPath(drawTarget, fillRule);
        if (!path) {
          return;
        }
        simplePath.Reset();
      }
      // We need to transform the path back into the appropriate ancestor
      // coordinate system, and paint it it that coordinate system, in order
      // for non-scaled stroke to paint correctly.
      gfxMatrix outerSVGToUser = userToOuterSVG;
      outerSVGToUser.Invert();
      aContext->Multiply(outerSVGToUser);
      Path::TransformAndSetFillRule(path, ToMatrix(userToOuterSVG), fillRule);
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
      if (simplePath.IsRect()) {
        drawTarget->StrokeRect(simplePath.AsRect(), strokePattern,
                               strokeOptions, drawOptions);
      } else if (simplePath.IsLine()) {
        drawTarget->StrokeLine(simplePath.Point1(), simplePath.Point2(),
                               strokePattern, strokeOptions, drawOptions);
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

bool SVGGeometryFrame::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, DisplaySVGGeometry* aItem,
    bool aDryRun) {
  MOZ_ASSERT(StyleVisibility()->IsVisible());

  SVGGeometryElement* element = static_cast<SVGGeometryElement*>(GetContent());

  SVGGeometryElement::SimplePath simplePath;
  element->GetAsSimplePath(&simplePath);

  if (!simplePath.IsRect()) {
    return false;
  }

  const nsStyleSVG* style = StyleSVG();
  MOZ_ASSERT(style);

  if (!style->mFill.kind.IsColor()) {
    return false;
  }

  switch (style->mFill.kind.tag) {
    case StyleSVGPaintKind::Tag::Color:
      break;
    default:
      return false;
  }

  if (!style->mStroke.kind.IsNone()) {
    return false;
  }

  if (StyleEffects()->HasMixBlendMode()) {
    // FIXME: not implemented
    return false;
  }

  if (style->HasMarker() && element->IsMarkable()) {
    // Markers aren't suppported yet.
    return false;
  }

  if (!aDryRun) {
    auto appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
    float scale = (float)AppUnitsPerCSSPixel() / (float)appUnitsPerDevPx;

    auto rect = simplePath.AsRect();
    rect.Scale(scale);

    auto offset = LayoutDevicePoint::FromAppUnits(
        aItem->ToReferenceFrame() - GetPosition(), appUnitsPerDevPx);
    rect.MoveBy(offset.x, offset.y);

    auto wrRect = wr::ToLayoutRect(rect);

    SVGContextPaint* contextPaint =
        SVGContextPaint::GetContextPaint(GetContent());
    // At the moment this code path doesn't support strokes so it fine to
    // combine the rectangle's opacity (which has to be applied on the result)
    // of (filling + stroking) with the fill opacity.

    float elemOpacity = 1.0f;
    if (SVGUtils::CanOptimizeOpacity(this)) {
      elemOpacity = StyleEffects()->mOpacity;
    }

    float fillOpacity = SVGUtils::GetOpacity(style->mFillOpacity, contextPaint);
    float opacity = elemOpacity * fillOpacity;

    auto color = wr::ToColorF(
        ToDeviceColor(StyleSVG()->mFill.kind.AsColor().CalcColor(this)));
    color.a *= opacity;
    aBuilder.PushRect(wrRect, wrRect, !aItem->BackfaceIsHidden(), true, false,
                      color);
  }

  return true;
}

void SVGGeometryFrame::PaintMarkers(gfxContext& aContext,
                                    const gfxMatrix& aTransform,
                                    imgDrawingParams& aImgParams) {
  auto* element = static_cast<SVGGeometryElement*>(GetContent());
  if (!element->IsMarkable()) {
    return;
  }
  SVGMarkerFrame* markerFrames[SVGMark::eTypeCount];
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
  gfxMatrix userToOuterSVG;
  if (SVGUtils::GetNonScalingStrokeTransform(this, &userToOuterSVG)) {
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

    strokeWidth /= float(sqrt(userToOuterSVG._11 * userToOuterSVG._11 +
                              userToOuterSVG._12 * userToOuterSVG._12 +
                              userToOuterSVG._21 * userToOuterSVG._21 +
                              userToOuterSVG._22 * userToOuterSVG._22) /
                         M_SQRT2);
  }
  return strokeWidth;
}

}  // namespace mozilla
