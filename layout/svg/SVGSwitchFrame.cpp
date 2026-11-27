/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "SVGGFrame.h"
#include "gfxRect.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGContainerFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGSwitchElement.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

nsIFrame* NS_NewSVGSwitchFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle);

namespace mozilla {

class SVGSwitchFrame final : public SVGGFrame {
  friend nsIFrame* ::NS_NewSVGSwitchFrame(mozilla::PresShell* aPresShell,
                                          ComputedStyle* aStyle);

 protected:
  explicit SVGSwitchFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : SVGGFrame(aStyle, aPresContext, kClassID) {}

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGSwitchFrame)

#ifdef DEBUG
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGSwitch"_ns, aResult);
  }
#endif

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  // ISVGDisplayableFrame interface:
  void PaintSVG(gfxContext& aContext, const gfxMatrix& aTransform,
                imgDrawingParams& aImgParams) override;
  nsIFrame* GetFrameForPoint(const gfxPoint& aPoint) override;
  void ReflowSVG() override;
  SVGBBox GetBBoxContribution(const Matrix& aToBBoxUserspace,
                              SVGBBoxFlags aFlags) override;

 private:
  nsIFrame* GetActiveChildFrame();
  void ReflowAllSVGTextFramesInsideNonActiveChildren(nsIFrame* aActiveChild);
  static void AlwaysReflowSVGTextFrameDoForOneKid(nsIFrame* aKid);
};

}  // namespace mozilla

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGSwitchFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGSwitchFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGSwitchFrame)

#ifdef DEBUG
void SVGSwitchFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                          nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::svgSwitch),
               "Content is not an SVG switch");

  SVGGFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

void SVGSwitchFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                      const nsDisplayListSet& aLists) {
  if (HidesContent()) {
    return;
  }
  if (auto* kid = GetActiveChildFrame()) {
    BuildDisplayListForChild(aBuilder, kid, aLists);
  }
}

void SVGSwitchFrame::PaintSVG(gfxContext& aContext, const gfxMatrix& aTransform,
                              imgDrawingParams& aImgParams) {
  NS_ASSERTION(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
               "Only painting of non-display SVG should take this code path");

  if (StyleEffects()->IsTransparent()) {
    return;
  }

  if (auto* kid = GetActiveChildFrame()) {
    gfxMatrix tm = aTransform;
    if (kid->GetContent()->IsSVGElement()) {
      tm = SVGUtils::GetTransformMatrixInUserSpace(kid) * tm;
    }
    SVGUtils::PaintFrameWithEffects(kid, aContext, tm, aImgParams);
  }
}

nsIFrame* SVGSwitchFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  MOZ_ASSERT_UNREACHABLE("A clipPath cannot contain an SVGSwitch element");
  return nullptr;
}

static bool ShouldReflowSVGTextFrameInside(const nsIFrame* aFrame) {
  return aFrame->IsSVGContainerFrame() || aFrame->IsSVGForeignObjectFrame() ||
         !aFrame->IsSVGFrame();
}

void SVGSwitchFrame::AlwaysReflowSVGTextFrameDoForOneKid(nsIFrame* aKid) {
  if (!aKid->IsSubtreeDirty()) {
    return;
  }

  if (aKid->IsSVGTextFrame()) {
    MOZ_ASSERT(!aKid->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
               "A non-display SVGTextFrame directly contained in a display "
               "container?");
    static_cast<SVGTextFrame*>(aKid)->ReflowSVG();
  } else if (ShouldReflowSVGTextFrameInside(aKid)) {
    if (!aKid->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
      for (nsIFrame* kid : aKid->PrincipalChildList()) {
        AlwaysReflowSVGTextFrameDoForOneKid(kid);
      }
    } else {
      // This child is in a nondisplay context, something like:
      // <switch>
      //   ...
      //   <g><mask><text></text></mask></g>
      // </switch>
      // We should not call ReflowSVG on it.
      SVGContainerFrame::ReflowSVGNonDisplayText(aKid);
    }
  }
}

void SVGSwitchFrame::ReflowAllSVGTextFramesInsideNonActiveChildren(
    nsIFrame* aActiveChild) {
  for (auto* kid : mFrames) {
    if (aActiveChild == kid) {
      continue;
    }

    AlwaysReflowSVGTextFrameDoForOneKid(kid);
  }
}

void SVGSwitchFrame::ReflowSVG() {
  NS_ASSERTION(SVGUtils::OuterSVGIsCallingReflowSVG(this),
               "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    return;
  }

  // If the NS_FRAME_FIRST_REFLOW bit has been removed from our parent frame,
  // then our outer-<svg> has previously had its initial reflow. In that case
  // we need to make sure that that bit has been removed from ourself _before_
  // recursing over our children to ensure that they know too. Otherwise, we
  // need to remove it _after_ recursing over our children so that they know
  // the initial reflow is currently underway.

  bool isFirstReflow = HasAnyStateBits(NS_FRAME_FIRST_REFLOW);

  bool outerSVGHasHadFirstReflow =
      !GetParent()->HasAnyStateBits(NS_FRAME_FIRST_REFLOW);

  if (outerSVGHasHadFirstReflow) {
    RemoveStateBits(NS_FRAME_FIRST_REFLOW);  // tell our children
  }

  OverflowAreas overflowRects;

  auto* child = GetActiveChildFrame();
  ReflowAllSVGTextFramesInsideNonActiveChildren(child);

  ISVGDisplayableFrame* svgChild = do_QueryFrame(child);
  if (svgChild && !child->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    svgChild->ReflowSVG();

    // We build up our child frame overflows here instead of using
    // nsLayoutUtils::UnionChildOverflow since SVG frame's all use the same
    // frame list, and we're iterating over that list now anyway.
    ConsiderChildOverflow(overflowRects, child);
  } else if (child && ShouldReflowSVGTextFrameInside(child)) {
    MOZ_ASSERT(
        child->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) || !child->IsSVGFrame(),
        "Check for this explicitly in the |if|, then");
    ReflowSVGNonDisplayText(child);
  }

  if (isFirstReflow) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);
  }

  FinishAndStoreOverflow(overflowRects, mRect.Size());

  // Remove state bits after FinishAndStoreOverflow so that it doesn't
  // invalidate on first reflow:
  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);
}

SVGBBox SVGSwitchFrame::GetBBoxContribution(const Matrix& aToBBoxUserspace,
                                            SVGBBoxFlags aFlags) {
  auto* kid = GetActiveChildFrame();
  if (ISVGDisplayableFrame* svgKid = do_QueryFrame(kid)) {
    nsIContent* content = kid->GetContent();
    gfxMatrix transform = ThebesMatrix(aToBBoxUserspace);
    if (content->IsSVGElement()) {
      transform =
          static_cast<SVGElement*>(content)->ChildToUserSpaceTransform() *
          SVGUtils::GetTransformMatrixInUserSpace(kid) * transform;
    }
    return svgKid->GetBBoxContribution(ToMatrix(transform), aFlags);
  }
  return SVGBBox();
}

nsIFrame* SVGSwitchFrame::GetActiveChildFrame() {
  auto* activeChild =
      static_cast<dom::SVGSwitchElement*>(GetContent())->GetActiveChild();
  return activeChild ? activeChild->GetPrimaryFrame() : nullptr;
}

}  // namespace mozilla
