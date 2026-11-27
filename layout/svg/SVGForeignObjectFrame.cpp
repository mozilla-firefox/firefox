/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGForeignObjectFrame.h"

// Keep others in (case-insensitive) order:
#include "ImgDrawResult.h"
#include "SVGGeometryProperty.h"
#include "gfxContext.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/SVGContainerFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGForeignObjectElement.h"
#include "nsDisplayList.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsRegion.h"

using namespace mozilla::dom;
using namespace mozilla::image;
namespace SVGT = SVGGeometryProperty::Tags;

//----------------------------------------------------------------------
// Implementation

nsContainerFrame* NS_NewSVGForeignObjectFrame(mozilla::PresShell* aPresShell,
                                              mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGForeignObjectFrame(aStyle,
                                            aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGForeignObjectFrame)

SVGForeignObjectFrame::SVGForeignObjectFrame(ComputedStyle* aStyle,
                                             nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID) {
  AddStateBits(NS_FRAME_REFLOW_ROOT | NS_FRAME_MAY_BE_TRANSFORMED |
               NS_FRAME_SVG_LAYOUT | NS_FRAME_FONT_INFLATION_CONTAINER |
               NS_FRAME_FONT_INFLATION_FLOW_ROOT);
}

//----------------------------------------------------------------------
// nsIFrame methods

NS_QUERYFRAME_HEAD(SVGForeignObjectFrame)
  NS_QUERYFRAME_ENTRY(ISVGDisplayableFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

void SVGForeignObjectFrame::Init(nsIContent* aContent,
                                 nsContainerFrame* aParent,
                                 nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::foreignObject),
               "Content is not an SVG foreignObject!");

  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);
  AddStateBits(aParent->GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD);
}

nsresult SVGForeignObjectFrame::AttributeChanged(int32_t aNameSpaceID,
                                                 nsAtom* aAttribute,
                                                 AttrModType) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::transform) {
      // We don't invalidate for transform changes (the layers code does that).
      // Also note that SVGTransformableElement::GetAttributeChangeHint will
      // return nsChangeHint_UpdateOverflow for "transform" attribute changes
      // and cause DoApplyRenderingChangeToTree to make the SchedulePaint call.
      mCanvasTM = nullptr;
    } else if (aAttribute == nsGkAtoms::viewBox ||
               aAttribute == nsGkAtoms::preserveAspectRatio) {
      nsLayoutUtils::PostRestyleEvent(
          mContent->AsElement(), RestyleHint{0},
          nsChangeHint_InvalidateRenderingObservers);
    }
  }

  return NS_OK;
}

void SVGForeignObjectFrame::DidSetComputedStyle(
    ComputedStyle* aOldComputedStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  if (aOldComputedStyle) {
    if (StyleSVGReset()->mX != aOldComputedStyle->StyleSVGReset()->mX ||
        StyleSVGReset()->mY != aOldComputedStyle->StyleSVGReset()->mY) {
      // Invalidate cached transform matrix.
      mCanvasTM = nullptr;
      SVGUtils::ScheduleReflowSVG(this);
    }
  }
}

void SVGForeignObjectFrame::Reflow(nsPresContext* aPresContext,
                                   ReflowOutput& aDesiredSize,
                                   const ReflowInput& aReflowInput,
                                   nsReflowStatus& aStatus) {
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "Should not have been called");

  // Only InvalidateAndScheduleBoundsUpdate marks us with NS_FRAME_IS_DIRTY,
  // so if that bit is still set we still have a resize pending. If we hit
  // this assertion, then we should get the presShell to skip reflow roots
  // that have a dirty parent since a reflow is going to come via the
  // reflow root's parent anyway.
  NS_ASSERTION(!HasAnyStateBits(NS_FRAME_IS_DIRTY),
               "Reflowing while a resize is pending is wasteful");

  // ReflowSVG makes sure mRect is up to date before we're called.

  NS_ASSERTION(!aReflowInput.mParentReflowInput,
               "should only get reflow from being reflow root");
  NS_ASSERTION(aReflowInput.ComputedSize() == GetLogicalSize(),
               "reflow roots should be reflowed at existing size and "
               "svg.css should ensure we have no padding/border/margin");

  DoReflow();

  WritingMode wm = aReflowInput.GetWritingMode();
  LogicalSize finalSize(wm, aReflowInput.ComputedISize(),
                        aReflowInput.ComputedBSize());
  aDesiredSize.SetSize(wm, finalSize);
  aDesiredSize.SetOverflowAreasToDesiredBounds();
}

void SVGForeignObjectFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                             const nsDisplayListSet& aLists) {
  if (!static_cast<const SVGElement*>(GetContent())->HasValidDimensions()) {
    return;
  }
  nsDisplayList newList(aBuilder);
  nsDisplayListSet set(&newList, &newList, &newList, &newList, &newList,
                       &newList);
  DisplayOutline(aBuilder, set);
  BuildDisplayListForNonBlockChildren(aBuilder, set);
  aLists.Content()->AppendNewToTop<nsDisplayForeignObject>(aBuilder, this,
                                                           &newList);
}

bool SVGForeignObjectFrame::DoGetParentSVGTransforms(
    Matrix* aFromParentTransform) const {
  return SVGUtils::GetParentSVGTransforms(this, aFromParentTransform);
}

void SVGForeignObjectFrame::PaintSVG(gfxContext& aContext,
                                     const gfxMatrix& aTransform,
                                     imgDrawingParams& aImgParams) {
  NS_ASSERTION(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
               "Only painting of non-display SVG should take this code path");

  if (IsDisabled()) {
    return;
  }

  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  if (aTransform.IsSingular()) {
    NS_WARNING("Can't render foreignObject element!");
    return;
  }

  gfxClipAutoSaveRestore autoSaveClip(&aContext);

  if (StyleDisplay()->IsScrollableOverflow()) {
    float x, y, width, height;
    SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width,
                                    SVGT::Height>(
        static_cast<SVGElement*>(GetContent()), &x, &y, &width, &height);

    gfxRect clipRect =
        SVGUtils::GetClipRectForFrame(this, 0.0f, 0.0f, width, height);
    autoSaveClip.TransformedClip(aTransform, clipRect);
  }

  // SVG paints in CSS px, but normally frames paint in dev pixels. Here we
  // multiply a CSS-px-to-dev-pixel factor onto aTransform so our children
  // paint correctly.
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      PresContext()->AppUnitsPerDevPixel());
  gfxMatrix canvasTMForChildren = aTransform;
  canvasTMForChildren.PreScale(cssPxPerDevPx, cssPxPerDevPx);

  aContext.Multiply(canvasTMForChildren);

  using PaintFrameFlags = nsLayoutUtils::PaintFrameFlags;
  PaintFrameFlags flags = PaintFrameFlags::InTransform;
  if (SVGAutoRenderState::IsPaintingToWindow(aContext.GetDrawTarget())) {
    flags |= PaintFrameFlags::ToWindow;
  }
  if (aImgParams.imageFlags & imgIContainer::FLAG_SYNC_DECODE) {
    flags |= PaintFrameFlags::SyncDecodeImages;
  }
  if (aImgParams.imageFlags & imgIContainer::FLAG_HIGH_QUALITY_SCALING) {
    flags |= PaintFrameFlags::UseHighQualityScaling;
  }
  nsLayoutUtils::PaintFrame(&aContext, kid, nsRegion(kid->InkOverflowRect()),
                            NS_RGBA(0, 0, 0, 0),
                            nsDisplayListBuilderMode::Painting, flags);
}

nsIFrame* SVGForeignObjectFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  MOZ_ASSERT_UNREACHABLE(
      "A clipPath cannot contain an SVGForeignObject element");
  return nullptr;
}

void SVGForeignObjectFrame::ReflowSVG() {
  NS_ASSERTION(SVGUtils::OuterSVGIsCallingReflowSVG(this),
               "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    return;
  }

  // We update mRect before the DoReflow call so that DoReflow uses the
  // correct dimensions:

  float x, y, w, h;
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      static_cast<SVGElement*>(GetContent()), &x, &y, &w, &h);

  // If mRect's width or height are negative, reflow blows up! We must clamp!
  if (w < 0.0f) {
    w = 0.0f;
  }
  if (h < 0.0f) {
    h = 0.0f;
  }

  mRect = nsLayoutUtils::RoundGfxRectToAppRect(gfxRect(x, y, w, h),
                                               AppUnitsPerCSSPixel());

  // Fully mark our kid dirty so that it gets resized if necessary
  // (NS_FRAME_HAS_DIRTY_CHILDREN isn't enough in that case):
  nsIFrame* kid = PrincipalChildList().FirstChild();
  kid->MarkSubtreeDirty();

  // Make sure to not allow interrupts if we're not being reflown as a root:
  nsPresContext::InterruptPreventer noInterrupts(PresContext());

  DoReflow();

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);
  }

  // If we have a filter, we need to invalidate ourselves because filter
  // output can change even if none of our descendants need repainting.
  if (StyleEffects()->HasFilters()) {
    InvalidateFrame();
  }

  auto* anonKid = PrincipalChildList().FirstChild();
  nsRect overflow = anonKid->InkOverflowRect();

  OverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  // Now unset the various reflow bits:
  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);
}

void SVGForeignObjectFrame::NotifySVGChanged(ChangeFlags aFlags) {
  MOZ_ASSERT(aFlags.contains(ChangeFlag::TransformChanged) ||
                 aFlags.contains(ChangeFlag::CoordContextChanged),
             "Invalidation logic may need adjusting");

  bool needNewBounds = false;  // i.e. mRect or ink overflow rect
  bool needReflow = false;
  bool needNewCanvasTM = false;

  if (aFlags.contains(ChangeFlag::CoordContextChanged)) {
    // Coordinate context changes affect mCanvasTM if we have a
    // percentage 'x' or 'y'
    if (StyleSVGReset()->mX.HasPercent() || StyleSVGReset()->mY.HasPercent()) {
      needNewBounds = true;
      needNewCanvasTM = true;
    }

    const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);
    // Our coordinate context's width/height has changed. If we have a
    // percentage width/height our dimensions will change so we must reflow.
    if (StylePosition()->GetWidth(anchorResolutionParams)->HasPercent() ||
        StylePosition()->GetHeight(anchorResolutionParams)->HasPercent()) {
      needNewBounds = true;
      needReflow = true;
    }
  }

  if (aFlags.contains(ChangeFlag::TransformChanged)) {
    if (mCanvasTM && mCanvasTM->IsSingular()) {
      needNewBounds = true;  // old bounds are bogus
    }
    needNewCanvasTM = true;
    // In an ideal world we would reflow when our CTM changes. This is because
    // glyph metrics do not necessarily scale uniformly with change in scale
    // and, as a result, CTM changes may require text to break at different
    // points. The problem would be how to keep performance acceptable when
    // e.g. the transform of an ancestor is animated.
    // We also seem to get some sort of infinite loop post bug 421584 if we
    // reflow.
  }

  if (needNewBounds) {
    // Ancestor changes can't affect how we render from the perspective of
    // any rendering observers that we may have, so we don't need to
    // invalidate them. We also don't need to invalidate ourself, since our
    // changed ancestor will have invalidated its entire area, which includes
    // our area.
    SVGUtils::ScheduleReflowSVG(this);
  }

  // If we're called while the PresShell is handling reflow events then we
  // must have been called as a result of the NotifyViewportChange() call in
  // our SVGOuterSVGFrame's Reflow() method. We must not call RequestReflow
  // at this point (i.e. during reflow) because it could confuse the
  // PresShell and prevent it from reflowing us properly in future. Besides
  // that, SVGOuterSVGFrame::DidReflow will take care of reflowing us
  // synchronously, so there's no need.
  if (needReflow && !PresShell()->IsReflowLocked()) {
    RequestReflow(IntrinsicDirty::None);
  }

  if (needNewCanvasTM) {
    // Do this after calling InvalidateAndScheduleBoundsUpdate in case we
    // change the code and it needs to use it.
    mCanvasTM = nullptr;
  }
}

SVGBBox SVGForeignObjectFrame::GetBBoxContribution(
    const Matrix& aToBBoxUserspace, SVGBBoxFlags aFlags) {
  SVGForeignObjectElement* element =
      static_cast<SVGForeignObjectElement*>(GetContent());

  float x, y, w, h;
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      element, &x, &y, &w, &h);

  if (w < 0.0f) {
    w = 0.0f;
  }
  if (h < 0.0f) {
    h = 0.0f;
  }
  gfx::Rect rect(0.0f, 0.0f, w, h);

  if (aFlags.contains(SVGBBoxFlag::DisregardCSSZoom)) {
    rect.Scale(1 / Style()->EffectiveZoom().ToFloat());
  }

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return SVGBBox();
  }
  return aToBBoxUserspace.TransformBounds(rect);
}

//----------------------------------------------------------------------

gfxMatrix SVGForeignObjectFrame::GetCanvasTM() {
  if (!mCanvasTM) {
    NS_ASSERTION(GetParent(), "null parent");
    auto* parent = static_cast<SVGContainerFrame*>(GetParent());
    auto* content = static_cast<SVGForeignObjectElement*>(GetContent());
    mCanvasTM = std::make_unique<gfxMatrix>(
        content->ChildToUserSpaceTransform() * parent->GetCanvasTM());
  }
  return *mCanvasTM;
}

//----------------------------------------------------------------------
// Implementation helpers

void SVGForeignObjectFrame::RequestReflow(IntrinsicDirty aType) {
  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // If we haven't had a ReflowSVG() yet, nothing to do.
    return;
  }

  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  PresShell()->FrameNeedsReflow(kid, aType, NS_FRAME_IS_DIRTY);
}

void SVGForeignObjectFrame::DoReflow() {
  MarkInReflow();
  // Skip reflow if we're zero-sized, unless this is our first reflow.
  if (IsDisabled() && !HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    return;
  }

  nsPresContext* presContext = PresContext();
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  // initiate a synchronous reflow here and now:
  std::unique_ptr<gfxContext> renderingContext =
      presContext->PresShell()->CreateReferenceRenderingContext();

  WritingMode wm = kid->GetWritingMode();
  ReflowInput reflowInput(presContext, kid, renderingContext.get(),
                          LogicalSize(wm, ISize(wm), NS_UNCONSTRAINEDSIZE));
  ReflowOutput desiredSize(reflowInput);
  nsReflowStatus status;

  // We don't use mRect.height above because that tells the child to do
  // page/column breaking at that height.
  NS_ASSERTION(
      reflowInput.ComputedPhysicalBorderPadding() == nsMargin(0, 0, 0, 0) &&
          reflowInput.ComputedPhysicalMargin() == nsMargin(0, 0, 0, 0),
      "style system should ensure that :-moz-svg-foreign-content "
      "does not get styled");
  NS_ASSERTION(reflowInput.ComputedISize() == ISize(wm),
               "reflow input made child wrong size");
  reflowInput.SetComputedBSize(BSize(wm));

  ReflowChild(kid, presContext, desiredSize, reflowInput, 0, 0,
              ReflowChildFlags::NoMoveFrame, status);
  NS_ASSERTION(mRect.width == desiredSize.Width() &&
                   mRect.height == desiredSize.Height(),
               "unexpected size");
  FinishReflowChild(kid, presContext, desiredSize, &reflowInput, 0, 0,
                    ReflowChildFlags::NoMoveFrame);
}

void SVGForeignObjectFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  MOZ_ASSERT(PrincipalChildList().FirstChild(), "Must have our anon box");
  aResult.AppendElement(OwnedAnonBox(PrincipalChildList().FirstChild()));
}

}  // namespace mozilla
