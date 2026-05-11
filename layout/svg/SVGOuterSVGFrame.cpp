/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGOuterSVGFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "nsDisplayList.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsLayoutUtils.h"
#include "nsObjectLoadingContent.h"
#include "nsSubDocumentFrame.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

//----------------------------------------------------------------------
// Implementation

nsContainerFrame* NS_NewSVGOuterSVGFrame(mozilla::PresShell* aPresShell,
                                         mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGOuterSVGFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGOuterSVGFrame)

SVGOuterSVGFrame::SVGOuterSVGFrame(ComputedStyle* aStyle,
                                   nsPresContext* aPresContext)
    : SVGDisplayContainerFrame(aStyle, aPresContext, kClassID) {
  // Outer-<svg> has CSS layout, so remove this bit:
  RemoveStateBits(NS_FRAME_SVG_LAYOUT);
  AddStateBits(NS_FRAME_REFLOW_ROOT | NS_FRAME_FONT_INFLATION_CONTAINER |
               NS_FRAME_FONT_INFLATION_FLOW_ROOT);
}

// The CSS Containment spec says that size-contained replaced elements must be
// treated as having an intrinsic width and height of 0.  That's applicable to
// outer SVG frames, unless they're the outermost element (in which case
// they're not really "replaced", and there's no outer context to contain sizes
// from leaking into). Hence, we check for a parent element before we bother
// testing for 'contain:size'.
static inline ContainSizeAxes ContainSizeAxesIfApplicable(
    const SVGOuterSVGFrame* aFrame) {
  if (!aFrame->GetContent()->GetParent()) {
    return ContainSizeAxes(false, false);
  }
  return aFrame->GetContainSizeAxes();
}

// This should match ImageDocument::GetZoomLevel.
float SVGOuterSVGFrame::ComputeFullZoom() const {
  MOZ_ASSERT(mIsRootContent);
  MOZ_ASSERT(!mIsInIframe);
  if (BrowsingContext* bc = PresContext()->Document()->GetBrowsingContext()) {
    return bc->FullZoom();
  }
  return 1.0f;
}

class AsyncSendIntrinsicSizeAndRatioToEmbedder final : public Runnable {
 public:
  explicit AsyncSendIntrinsicSizeAndRatioToEmbedder(SVGOuterSVGFrame* aFrame)
      : Runnable("AsyncSendIntrinsicSizeAndRatioToEmbedder") {
    mElement = aFrame->GetContent()->AsElement();
  }
  NS_IMETHOD Run() override {
    AUTO_PROFILER_LABEL("AsyncSendIntrinsicSizeAndRatioToEmbedder::Run", OTHER);
    // Check we're still an outer svg frame. We could have been
    // moved inside another svg element and now be an SVGInnerSVGFrame.
    if (SVGOuterSVGFrame* frame = do_QueryFrame(mElement->GetPrimaryFrame())) {
      frame->MaybeSendIntrinsicSizeAndRatioToEmbedder();
    }
    return NS_OK;
  }

 private:
  RefPtr<Element> mElement;
};

void SVGOuterSVGFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::svg),
               "Content is not an SVG 'svg' element!");

  // Check for conditional processing attributes here rather than in
  // nsCSSFrameConstructor::FindSVGData because we want to avoid
  // simply giving failing outer <svg> elements an SVGContainerFrame.
  // We don't create other SVG frames if PassesConditionalProcessingTests
  // returns false, but since we do create SVGOuterSVGFrame frames we
  // prevent them from painting by [ab]use NS_FRAME_IS_NONDISPLAY. The
  // frame will be recreated via an nsChangeHint_ReconstructFrame restyle if
  // the value returned by PassesConditionalProcessingTests changes.
  auto* svg = static_cast<SVGSVGElement*>(aContent);
  if (!svg->PassesConditionalProcessingTests()) {
    AddStateBits(NS_FRAME_IS_NONDISPLAY);
  }

  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);

  Document* doc = mContent->GetUncomposedDoc();
  mIsRootContent = doc && doc->GetRootElement() == mContent;

  if (mIsRootContent) {
    if (nsCOMPtr<nsIDocShell> docShell = PresContext()->GetDocShell()) {
      RefPtr<BrowsingContext> bc = docShell->GetBrowsingContext();
      if (const Maybe<nsString>& type = bc->GetEmbedderElementType()) {
        mIsInObjectOrEmbed =
            nsGkAtoms::object->Equals(*type) || nsGkAtoms::embed->Equals(*type);
        mIsInIframe = nsGkAtoms::iframe->Equals(*type);
      }
    }
    if (!mIsInIframe) {
      mFullZoom = ComputeFullZoom();
    }
  }

  // We need to do this async in order to get the right ordering with
  // respect to `Destroy()` when reframed.
  nsContentUtils::AddScriptRunner(
      MakeAndAddRef<AsyncSendIntrinsicSizeAndRatioToEmbedder>(this));
}

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(SVGOuterSVGFrame)
  NS_QUERYFRAME_ENTRY(SVGOuterSVGFrame)
  NS_QUERYFRAME_ENTRY(ISVGSVGFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGDisplayContainerFrame)

//----------------------------------------------------------------------
// nsIFrame methods

nscoord SVGOuterSVGFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                         IntrinsicISizeType aType) {
  if (aType == IntrinsicISizeType::MinISize) {
    return GetIntrinsicSize().ISize(GetWritingMode()).valueOr(0);
  }

  nscoord result;
  SVGSVGElement* svg = static_cast<SVGSVGElement*>(GetContent());
  WritingMode wm = GetWritingMode();
  const SVGAnimatedLength& isize =
      wm.IsVertical() ? svg->mLengthAttributes[SVGSVGElement::ATTR_HEIGHT]
                      : svg->mLengthAttributes[SVGSVGElement::ATTR_WIDTH];

  if (Maybe<nscoord> containISize =
          ContainSizeAxesIfApplicable(this).ContainIntrinsicISize(*this)) {
    result = *containISize;
  } else if (isize.IsPercentage()) {
    // If we are here, our inline size attribute is a percentage either
    // explicitly (via an attribute value) or implicitly (by being unset, which
    // is treated as 100%). The following if-condition, deciding to return
    // either the fallback intrinsic size or zero, is made to match blink and
    // webkit's behavior for webcompat.
    if (isize.IsExplicitlySet() ||
        StylePosition()
            ->ISize(wm, AnchorPosResolutionParams::From(this))
            ->HasPercent() ||
        !GetAspectRatio()) {
      result = wm.IsVertical() ? kFallbackIntrinsicSize.height
                               : kFallbackIntrinsicSize.width;
    } else {
      result = nscoord(0);
    }
  } else {
    result =
        nsPresContext::CSSPixelsToAppUnits(isize.GetAnimValueWithZoom(svg));
    if (result < 0) {
      result = nscoord(0);
    }
  }

  return result;
}

/* virtual */
IntrinsicSize SVGOuterSVGFrame::GetIntrinsicSize() {
  // XXXjwatt Note that here we want to return the CSS width/height if they're
  // specified and we're embedded inside an nsIObjectLoadingContent.

  const auto containAxes = ContainSizeAxesIfApplicable(this);
  if (containAxes.IsBoth()) {
    // Intrinsic size of 'contain:size' replaced elements is determined by
    // contain-intrinsic-size, defaulting to 0x0.
    return FinishIntrinsicSize(containAxes, IntrinsicSize(0, 0));
  }

  SVGSVGElement* content = static_cast<SVGSVGElement*>(GetContent());
  const SVGAnimatedLength& width =
      content->mLengthAttributes[SVGSVGElement::ATTR_WIDTH];
  const SVGAnimatedLength& height =
      content->mLengthAttributes[SVGSVGElement::ATTR_HEIGHT];

  IntrinsicSize intrinsicSize;

  if (!width.IsPercentage()) {
    nscoord val =
        nsPresContext::CSSPixelsToAppUnits(width.GetAnimValueWithZoom(content));
    intrinsicSize.width.emplace(std::max(val, 0));
  }

  if (!height.IsPercentage()) {
    nscoord val = nsPresContext::CSSPixelsToAppUnits(
        height.GetAnimValueWithZoom(content));
    intrinsicSize.height.emplace(std::max(val, 0));
  }

  return FinishIntrinsicSize(containAxes, intrinsicSize);
}

AspectRatio SVGOuterSVGFrame::GetIntrinsicRatio() const {
  if (ContainSizeAxesIfApplicable(this).IsAny()) {
    return AspectRatio();
  }

  // We only have an intrinsic size/ratio if our width and height attributes
  // are both specified and set to non-percentage values, or we have a viewBox
  // rect: https://svgwg.org/svg2-draft/coords.html#SizingSVGInCSS

  auto* content = static_cast<SVGSVGElement*>(GetContent());
  const SVGAnimatedLength& width =
      content->mLengthAttributes[SVGSVGElement::ATTR_WIDTH];
  const SVGAnimatedLength& height =
      content->mLengthAttributes[SVGSVGElement::ATTR_HEIGHT];
  if (!width.IsPercentage() && !height.IsPercentage()) {
    // Use width/height ratio only if
    // 1. it's not a degenerate ratio, and
    // 2. width and height are non-negative numbers.
    // Otherwise, we use the viewbox rect.
    // https://github.com/w3c/csswg-drafts/issues/6286
    // Note width/height may have different units and therefore be
    // affected by zoom in different ways.
    const float w = width.GetAnimValueWithZoom(content);
    const float h = height.GetAnimValueWithZoom(content);
    if (w > 0.0f && h > 0.0f) {
      return AspectRatio::FromSize(w, h);
    }
  }

  if (const auto& viewBox = content->GetViewBoxInternal(); viewBox.HasRect()) {
    float zoom = Style()->EffectiveZoom().ToFloat();
    const auto& anim = viewBox.GetAnimValue() * zoom;
    return AspectRatio::FromSize(anim.width, anim.height);
  }

  return SVGDisplayContainerFrame::GetIntrinsicRatio();
}

/* virtual */
nsIFrame::SizeComputationResult SVGOuterSVGFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWritingMode,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  if (IsRootOfImage() || mIsInObjectOrEmbed) {
    // The embedding element has sized itself using the CSS replaced element
    // sizing rules, using our intrinsic dimensions as necessary. The SVG spec
    // says that the width and height of embedded SVG is overridden by the
    // width and height of the embedding element, so we just need to size to
    // the viewport that the embedding element has established for us.
    return {aCBSize, AspectRatioUsage::None};
  }

  LogicalSize cbSize = aCBSize;
  IntrinsicSize intrinsicSize = GetIntrinsicSize();
  AspectRatio ratio = GetAspectRatio();
  if (mIsRootContent) {
    // We're the root of the outermost browsing context, so we need to scale
    // cbSize by the full-zoom so that SVGs with percentage width/height zoom:
    NS_ASSERTION(aCBSize.ISize(aWritingMode) != NS_UNCONSTRAINEDSIZE &&
                     aCBSize.BSize(aWritingMode) != NS_UNCONSTRAINEDSIZE,
                 "root should not have auto-width/height containing block");

    if (!mIsInIframe) {
      // NOTE: We can't just use mFullZoom because this can run before Reflow()
      // updates it.
      const float zoom = ComputeFullZoom();
      cbSize.ISize(aWritingMode) *= zoom;
      cbSize.BSize(aWritingMode) *= zoom;
    }
  }

  return {ComputeSizeWithIntrinsicDimensions(
              aSizingInput.mRenderingContext, aWritingMode, intrinsicSize,
              ratio, cbSize, aMargin, aBorderPadding, aSizeOverrides, aFlags),
          AspectRatioUsage::None};
}

void SVGOuterSVGFrame::Reflow(nsPresContext* aPresContext,
                              ReflowOutput& aDesiredSize,
                              const ReflowInput& aReflowInput,
                              nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("SVGOuterSVGFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter SVGOuterSVGFrame::Reflow: availSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_IN_REFLOW), "frame is not in reflow");

  const auto wm = GetWritingMode();
  aDesiredSize.SetSize(wm, aReflowInput.ComputedSizeWithBorderPadding(wm));

  NS_ASSERTION(!GetPrevInFlow(), "SVG can't currently be broken across pages.");

  SVGSVGElement* svgElem = static_cast<SVGSVGElement*>(GetContent());

  auto* anonKid = static_cast<SVGOuterSVGAnonChildFrame*>(
      PrincipalChildList().FirstChild());

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Initialize
    svgElem->UpdateHasChildrenOnlyTransform();
  }

  // If our SVG viewport has changed, update our content and notify.
  // http://www.w3.org/TR/SVG11/coords.html#ViewportSpace

  gfx::Size newViewportSize(
      nsPresContext::AppUnitsToFloatCSSPixels(aReflowInput.ComputedWidth()),
      nsPresContext::AppUnitsToFloatCSSPixels(aReflowInput.ComputedHeight()));

  ChangeFlags changeBits;
  if (newViewportSize != svgElem->GetViewportSize()) {
    // When our viewport size changes, we may need to update the overflow rects
    // of our child frames. This is the case if:
    //
    //  * We have a real/synthetic viewBox (a children-only transform), since
    //    the viewBox transform will change as the viewport dimensions change.
    //
    //  * We do not have a real/synthetic viewBox, but the last time we
    //    reflowed (or the last time UpdateOverflow() was called) we did.
    //
    // We only handle the former case here, in which case we mark all our child
    // frames as dirty so that we reflow them below and update their overflow
    // rects.
    //
    // In the latter case, updating of overflow rects is handled for removal of
    // real viewBox (the viewBox attribute) in AttributeChanged. Synthetic
    // viewBox "removal" (e.g. a document references the same SVG via both an
    // <svg:image> and then as a CSS background image (a synthetic viewBox is
    // used when painting the former, but not when painting the latter)) is
    // handled in SVGSVGElement::FlushImageTransformInvalidation.
    //
    if (svgElem->HasViewBoxOrSyntheticViewBox()) {
      nsIFrame* anonChild = PrincipalChildList().FirstChild();
      anonChild->MarkSubtreeDirty();
      for (nsIFrame* child : anonChild->PrincipalChildList()) {
        child->MarkSubtreeDirty();
      }
    }
    changeBits += ChangeFlag::CoordContextChanged;
    svgElem->SetViewportSize(newViewportSize);
  }
  if (mIsRootContent && !mIsInIframe) {
    const auto oldZoom = mFullZoom;
    mFullZoom = ComputeFullZoom();
    if (oldZoom != mFullZoom) {
      changeBits += ChangeFlag::FullZoomChanged;
    }
  }
  if (!changeBits.isEmpty() && !HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    NotifyViewportOrTransformChanged(changeBits);
  }

  // Now that we've marked the necessary children as dirty, call
  // ReflowSVG() or ReflowSVGNonDisplayText() on them, depending
  // on whether we are non-display.
  mCallingReflowSVG = true;
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    ReflowSVGNonDisplayText(this);
  } else {
    // Update the mRects and ink overflow rects of all our descendants,
    // including our anonymous wrapper kid:
    anonKid->ReflowSVG();
    MOZ_ASSERT(!anonKid->GetNextSibling(),
               "We should have one anonymous child frame wrapping our real "
               "children");
  }
  mCallingReflowSVG = false;

  // Set our anonymous kid's offset from our border box:
  anonKid->SetPosition(GetContentRectRelativeToSelf().TopLeft());

  // Including our size in our overflow rects regardless of the value of
  // 'background', 'border', etc. makes sure that we usually (when we clip to
  // our content area) don't have to keep changing our overflow rects as our
  // descendants move about (see perf comment below). Including our size in our
  // scrollable overflow rect also makes sure that we scroll if we're too big
  // for our viewport.
  //
  // <svg> never allows scrolling to anything outside its mRect (only panning),
  // so we must always keep our scrollable overflow set to our size.
  //
  // With regards to ink overflow, we always clip root-<svg> (see our
  // BuildDisplayList method) regardless of the value of the 'overflow'
  // property since that is per-spec, even for the initial 'visible' value. For
  // that reason there's no point in adding descendant ink overflow to our
  // own when this frame is for a root-<svg>. That said, there's also a very
  // good performance reason for us wanting to avoid doing so. If we did, then
  // the frame's overflow would often change as descendants that are partially
  // or fully outside its rect moved (think animation on/off screen), and that
  // would cause us to do a full NS_FRAME_IS_DIRTY reflow and repaint of the
  // entire document tree each such move (see bug 875175).
  //
  // So it's only non-root outer-<svg> that has the ink overflow of its
  // descendants added to its own. (Note that the default user-agent style
  // sheet makes 'hidden' the default value for :not(root(svg)), so usually
  // FinishAndStoreOverflow will still clip this back to the frame's rect.)
  //
  // WARNING!! Keep UpdateBounds below in sync with whatever we do for our
  // overflow rects here! (Again, see bug 875175.)
  //
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  // An outer SVG will be here as a nondisplay if it fails the conditional
  // processing test. In that case, we don't maintain its overflow.
  if (!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    if (!mIsRootContent) {
      aDesiredSize.mOverflowAreas.InkOverflow().UnionRect(
          aDesiredSize.mOverflowAreas.InkOverflow(),
          anonKid->InkOverflowRect() + anonKid->GetPosition());
    }
    FinishAndStoreOverflow(&aDesiredSize);
  }

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                 ("exit SVGOuterSVGFrame::Reflow: size=%d,%d",
                  aDesiredSize.Width(), aDesiredSize.Height()));
}

/* virtual */
void SVGOuterSVGFrame::UnionChildOverflow(OverflowAreas& aOverflowAreas,
                                          bool aAsIfScrolled) {
  // See the comments in Reflow above.

  // WARNING!! Keep this in sync with Reflow above!

  if (!mIsRootContent) {
    nsIFrame* anonKid = PrincipalChildList().FirstChild();
    aOverflowAreas.InkOverflow().UnionRect(
        aOverflowAreas.InkOverflow(),
        anonKid->InkOverflowRect() + anonKid->GetPosition());
  }
}

//----------------------------------------------------------------------
// container methods

nsresult SVGOuterSVGFrame::AttributeChanged(int32_t aNameSpaceID,
                                            nsAtom* aAttribute, AttrModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      !HasAnyStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_NONDISPLAY)) {
    if (aAttribute == nsGkAtoms::viewBox ||
        aAttribute == nsGkAtoms::preserveAspectRatio) {
      // make sure our cached transform matrix gets (lazily) updated
      mCanvasTM = nullptr;

      SVGUtils::NotifyChildrenOfSVGChange(
          PrincipalChildList().FirstChild(),
          aAttribute == nsGkAtoms::viewBox
              ? ChangeFlags(ChangeFlag::TransformChanged,
                            ChangeFlag::CoordContextChanged)
              : ChangeFlag::TransformChanged);

      if (aAttribute != nsGkAtoms::transform) {
        static_cast<SVGSVGElement*>(GetContent())
            ->ChildrenOnlyTransformChanged();
      }
    }
    if (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height ||
        aAttribute == nsGkAtoms::viewBox) {
      // Don't call ChildrenOnlyTransformChanged() here, since we call it
      // under Reflow if the width/height/viewBox actually changed.

      MaybeSendIntrinsicSizeAndRatioToEmbedder();

      if (!mIsInObjectOrEmbed) {
        // We are not embedded by reference, so our 'width' and 'height'
        // attributes are not overridden (and viewBox may influence our
        // intrinsic aspect ratio).  We need to reflow.
        PresShell()->FrameNeedsReflow(
            this, IntrinsicDirty::FrameAncestorsAndDescendants,
            NS_FRAME_IS_DIRTY);
      }
    }
  }

  return NS_OK;
}

//----------------------------------------------------------------------
// painting

void SVGOuterSVGFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                        const nsDisplayListSet& aLists) {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    return;
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  nsRect visibleRect = aBuilder->GetVisibleRect();
  nsRect dirtyRect = aBuilder->GetDirtyRect();

  // Per-spec, we always clip root-<svg> even when 'overflow' has its initial
  // value of 'visible'. See also the "ink overflow" comments in Reflow.
  DisplayListClipState::AutoSaveRestore autoSR(aBuilder);
  if (mIsRootContent || StyleDisplay()->IsScrollableOverflow()) {
    autoSR.ClipContainingBlockDescendantsToContentBox(aBuilder, this);
    visibleRect = visibleRect.Intersect(GetContentRectRelativeToSelf());
    dirtyRect = dirtyRect.Intersect(GetContentRectRelativeToSelf());
  }

  nsDisplayListBuilder::AutoBuildingDisplayList building(
      aBuilder, this, visibleRect, dirtyRect);

  nsDisplayList* contentList = aLists.Content();
  nsDisplayListSet set(contentList, contentList, contentList, contentList,
                       contentList, contentList);
  BuildDisplayListForNonBlockChildren(aBuilder, set);
}

//----------------------------------------------------------------------
// ISVGSVGFrame methods:

void SVGOuterSVGFrame::NotifyViewportOrTransformChanged(ChangeFlags aFlags) {
  auto* content = static_cast<SVGSVGElement*>(GetContent());
  if (aFlags.contains(ChangeFlag::CoordContextChanged)) {
    if (content->HasViewBox()) {
      // Percentage lengths on children resolve against the viewBox rect so we
      // don't need to notify them of the viewport change, but the viewBox
      // transform will have changed, so we need to notify them of that instead.
      aFlags = ChangeFlag::TransformChanged;
    } else if (content->ShouldSynthesizeViewBox()) {
      // In the case of a synthesized viewBox, the synthetic viewBox's rect
      // changes as the viewport changes. As a result we need to maintain the
      // COORD_CONTEXT_CHANGED flag.
      aFlags += ChangeFlag::TransformChanged;
    } else if (mCanvasTM && mCanvasTM->IsSingular()) {
      // A width/height of zero will result in us having a singular mCanvasTM
      // even when we don't have a viewBox. So we also want to recompute our
      // mCanvasTM for this width/height change even though we don't have a
      // viewBox.
      aFlags += ChangeFlag::TransformChanged;
    }
  }

  bool haveNonFullZoomTransformChange =
      aFlags.contains(ChangeFlag::TransformChanged);

  if (aFlags.contains(ChangeFlag::FullZoomChanged)) {
    // Convert FullZoomChanged to TransformChanged.
    aFlags -= ChangeFlag::FullZoomChanged;
    aFlags += ChangeFlag::TransformChanged;
  }

  if (aFlags.contains(ChangeFlag::TransformChanged)) {
    // Make sure our canvas transform matrix gets (lazily) recalculated:
    mCanvasTM = nullptr;

    if (haveNonFullZoomTransformChange &&
        !HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
      SVGViewportElement::ChildrenOnlyTransformChangedFlags flags;
      if (HasAnyStateBits(NS_FRAME_IN_REFLOW)) {
        flags +=
            SVGViewportElement::ChildrenOnlyTransformChangedFlag::DuringReflow;
      }
      content->ChildrenOnlyTransformChanged(flags);
    }
  }

  SVGUtils::NotifyChildrenOfSVGChange(PrincipalChildList().FirstChild(),
                                      aFlags);
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods:

void SVGOuterSVGFrame::PaintSVG(gfxContext& aContext,
                                const gfxMatrix& aTransform,
                                imgDrawingParams& aImgParams) {
  NS_ASSERTION(
      PrincipalChildList().FirstChild()->IsSVGOuterSVGAnonChildFrame() &&
          !PrincipalChildList().FirstChild()->GetNextSibling(),
      "We should have a single, anonymous, child");
  auto* anonKid = static_cast<SVGOuterSVGAnonChildFrame*>(
      PrincipalChildList().FirstChild());
  anonKid->PaintSVG(aContext, aTransform, aImgParams);
}

SVGBBox SVGOuterSVGFrame::GetBBoxContribution(
    const gfx::Matrix& aToBBoxUserspace, SVGBBoxFlags aFlags) {
  NS_ASSERTION(
      PrincipalChildList().FirstChild()->IsSVGOuterSVGAnonChildFrame() &&
          !PrincipalChildList().FirstChild()->GetNextSibling(),
      "We should have a single, anonymous, child");
  // We must defer to our child so that we don't include our
  // content->ChildToUserSpaceTransform() transform.
  auto* anonKid = static_cast<SVGOuterSVGAnonChildFrame*>(
      PrincipalChildList().FirstChild());
  return anonKid->GetBBoxContribution(aToBBoxUserspace, aFlags);
}

//----------------------------------------------------------------------
// SVGContainerFrame methods:

gfxMatrix SVGOuterSVGFrame::GetCanvasTM() {
  if (!mCanvasTM) {
    auto* content = static_cast<SVGSVGElement*>(GetContent());
    float devPxPerCSSPx = 1.0f / nsPresContext::AppUnitsToFloatCSSPixels(
                                     PresContext()->AppUnitsPerDevPixel());

    gfxMatrix tm = content->ChildToUserSpaceTransform().PostScale(
        devPxPerCSSPx, devPxPerCSSPx);
    mCanvasTM = std::make_unique<gfxMatrix>(tm);
  }
  return *mCanvasTM;
}

//----------------------------------------------------------------------
// Implementation helpers

bool SVGOuterSVGFrame::IsRootOfImage() {
  if (!mContent->GetParent()) {
    // Our content is the document element
    Document* doc = mContent->GetUncomposedDoc();
    if (doc && doc->IsBeingUsedAsImage()) {
      // Our document is being used as an image
      return true;
    }
  }

  return false;
}

bool SVGOuterSVGFrame::VerticalScrollbarNotNeeded() const {
  const SVGAnimatedLength& height =
      static_cast<SVGSVGElement*>(GetContent())
          ->mLengthAttributes[SVGSVGElement::ATTR_HEIGHT];
  return height.IsPercentage() && height.GetBaseValInSpecifiedUnits() <= 100;
}

void SVGOuterSVGFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  nsIFrame* anonKid = PrincipalChildList().FirstChild();
  MOZ_ASSERT(anonKid->IsSVGOuterSVGAnonChildFrame());
  aResult.AppendElement(OwnedAnonBox(anonKid));
}

void SVGOuterSVGFrame::MaybeSendIntrinsicSizeAndRatioToEmbedder() {
  MaybeSendIntrinsicSizeAndRatioToEmbedder(Some(GetIntrinsicSize()),
                                           Some(GetIntrinsicRatio()));
}

void SVGOuterSVGFrame::MaybeSendIntrinsicSizeAndRatioToEmbedder(
    Maybe<IntrinsicSize> aIntrinsicSize, Maybe<AspectRatio> aIntrinsicRatio) {
  if (!mIsInObjectOrEmbed) {
    return;
  }

  nsCOMPtr<nsIDocShell> docShell = PresContext()->GetDocShell();
  if (!docShell) {
    return;
  }

  BrowsingContext* bc = docShell->GetBrowsingContext();
  MOZ_ASSERT(bc->IsContentSubframe());

  if (bc->GetParent()->IsInProcess()) {
    if (Element* embedder = bc->GetEmbedderElement()) {
      if (nsCOMPtr<nsIObjectLoadingContent> olc = do_QueryInterface(embedder)) {
        static_cast<nsObjectLoadingContent*>(olc.get())
            ->SubdocumentIntrinsicSizeOrRatioChanged(aIntrinsicSize,
                                                     aIntrinsicRatio);
      }
      return;
    }
  }

  if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
    (void)browserChild->SendIntrinsicSizeOrRatioChanged(aIntrinsicSize,
                                                        aIntrinsicRatio);
  }
}

void SVGOuterSVGFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  SVGDisplayContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  if (!aOldComputedStyle) {
    return;
  }

  if (aOldComputedStyle->StylePosition()->mAspectRatio !=
      StylePosition()->mAspectRatio) {
    // Our aspect-ratio property value changed, and an embedding <object> or
    // <embed> might care about that.
    MaybeSendIntrinsicSizeAndRatioToEmbedder();
  }
}

void SVGOuterSVGFrame::Destroy(DestroyContext& aContext) {
  // This handles both the case when the root <svg> element is made display:none
  // (and thus loses its intrinsic size and aspect ratio), and when the frame
  // is navigated elsewhere & we need to reset parent <object>/<embed>'s
  // recorded intrinsic size/ratio values.
  MaybeSendIntrinsicSizeAndRatioToEmbedder(Nothing(), Nothing());

  SVGDisplayContainerFrame::Destroy(aContext);
}

}  // namespace mozilla

//----------------------------------------------------------------------
// Implementation of SVGOuterSVGAnonChildFrame

nsContainerFrame* NS_NewSVGOuterSVGAnonChildFrame(
    mozilla::PresShell* aPresShell, mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGOuterSVGAnonChildFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGOuterSVGAnonChildFrame)

#ifdef DEBUG
void SVGOuterSVGAnonChildFrame::Init(nsIContent* aContent,
                                     nsContainerFrame* aParent,
                                     nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(aParent->IsSVGOuterSVGFrame(), "Unexpected parent");
  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif

void SVGOuterSVGAnonChildFrame::BuildDisplayList(
    nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists) {
  // Wrap our contents into an nsDisplaySVGWrapper.
  // We wrap this frame instead of the SVGOuterSVGFrame so that the wrapper
  // doesn't contain the <svg> element's CSS styles, like backgrounds or
  // borders. Creating the nsDisplaySVGWrapper here also means that it'll be
  // inside the nsDisplayTransform for our viewbox transform. The
  // nsDisplaySVGWrapper's reference frame is this frame, because this frame
  // always returns true from IsSVGTransformed.
  nsDisplayList newList(aBuilder);
  nsDisplayListSet set(&newList, &newList, &newList, &newList, &newList,
                       &newList);
  BuildDisplayListForNonBlockChildren(aBuilder, set);
  aLists.Content()->AppendNewToTop<nsDisplaySVGWrapper>(aBuilder, this,
                                                        &newList);
}

bool SVGOuterSVGFrame::HasChildrenOnlyTransform(Matrix* aTransform) const {
  auto* content = static_cast<SVGSVGElement*>(GetContent());
  if (!content->HasChildrenOnlyTransform()) {
    return false;
  }
  if (aTransform) {
    // Outer-<svg> doesn't use x/y, so we can use the child-to-user-space
    // transform here.
    *aTransform = gfx::ToMatrix(content->ChildToUserSpaceTransform());
  }
  return true;
}

bool SVGOuterSVGAnonChildFrame::DoGetParentSVGTransforms(
    Matrix* aFromParentTransform) const {
  // We want this frame to be a reference frame. An easy way to achieve that is
  // to always return true from this method, even for identity transforms.
  // This frame being a reference frame ensures that the offset between this
  // <svg> element and the parent reference frame is completely absorbed by the
  // nsDisplayTransform that's created for this frame, and that this offset does
  // not affect our descendants' transforms. Consequently, if the <svg> element
  // moves, e.g. during scrolling, the transform matrices of our contents are
  // unaffected. This simplifies invalidation.
  // TODO(emilio): Is the comment above true for WebRender nowadays?
  SVGUtils::GetParentSVGTransforms(this, aFromParentTransform);
  return true;
}

}  // namespace mozilla
