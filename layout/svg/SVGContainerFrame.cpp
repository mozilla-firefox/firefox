/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGContainerFrame.h"

// Keep others in (case-insensitive) order:
#include "ImgDrawResult.h"
#include "SVGAnimatedTransformList.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGElement.h"
#include "nsCSSFrameConstructor.h"

using namespace mozilla::dom;
using namespace mozilla::image;

nsIFrame* NS_NewSVGContainerFrame(mozilla::PresShell* aPresShell,
                                  mozilla::ComputedStyle* aStyle) {
  nsIFrame* frame =
      new mozilla::SVGContainerFrame(aStyle, aPresShell->GetPresContext(),
                                     mozilla::SVGContainerFrame::kClassID);
  // If we were called directly, then the frame is for a <defs> or
  // an unknown element type. In both cases we prevent the content
  // from displaying directly.
  frame->AddStateBits(NS_FRAME_IS_NONDISPLAY);
  return frame;
}

namespace mozilla {

NS_QUERYFRAME_HEAD(SVGContainerFrame)
  NS_QUERYFRAME_ENTRY(SVGContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_QUERYFRAME_HEAD(SVGDisplayContainerFrame)
  NS_QUERYFRAME_ENTRY(SVGDisplayContainerFrame)
  NS_QUERYFRAME_ENTRY(ISVGDisplayableFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(SVGContainerFrame)

void SVGContainerFrame::AppendFrames(ChildListID aListID,
                                     nsFrameList&& aFrameList) {
  nsContainerFrame::AppendFrames(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)
                                     ? FrameChildListID::NoReflowPrincipal
                                     : aListID,
                                 std::move(aFrameList));
}

void SVGContainerFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                     const nsLineList::iterator* aPrevFrameLine,
                                     nsFrameList&& aFrameList) {
  nsContainerFrame::InsertFrames(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)
                                     ? FrameChildListID::NoReflowPrincipal
                                     : aListID,
                                 aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
}

bool SVGContainerFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // We don't maintain overflow rects.
    // XXX It would have be better if the restyle request hadn't even happened.
    return false;
  }
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

/**
 * Traverses a frame tree, marking any SVGTextFrame frames as dirty
 * and calling InvalidateRenderingObservers() on it.
 *
 * The reason that this helper exists is because SVGTextFrame is special.
 * None of the other SVG frames ever need to be reflowed when they have the
 * NS_FRAME_IS_NONDISPLAY bit set on them because their PaintSVG methods
 * (and those of any containers that they can validly be contained within) do
 * not make use of mRect or overflow rects. "em" lengths, etc., are resolved
 * as those elements are painted.
 *
 * SVGTextFrame is different because its anonymous block and inline frames
 * need to be reflowed in order to get the correct metrics when things like
 * inherited font-size of an ancestor changes, or a delayed webfont loads and
 * applies.
 *
 * However, we only need to do this work if we were reflowed with
 * NS_FRAME_IS_DIRTY, which implies that all descendants are dirty.  When
 * that reflow reaches an NS_FRAME_IS_NONDISPLAY frame it would normally
 * stop, but this helper looks for any SVGTextFrame descendants of such
 * frames and marks them NS_FRAME_IS_DIRTY so that the next time that they
 * are painted their anonymous kid will first get the necessary reflow.
 */
/* static */
void SVGContainerFrame::ReflowSVGNonDisplayText(nsIFrame* aContainer) {
  if (!aContainer->HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    return;
  }
  MOZ_ASSERT(aContainer->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) ||
                 !aContainer->IsSVGFrame(),
             "it is wasteful to call ReflowSVGNonDisplayText on a container "
             "frame that is not NS_FRAME_IS_NONDISPLAY or not SVG");
  for (nsIFrame* kid : aContainer->PrincipalChildList()) {
    LayoutFrameType type = kid->Type();
    if (type == LayoutFrameType::SVGText) {
      static_cast<SVGTextFrame*>(kid)->ReflowSVGNonDisplayText();
    } else if (kid->IsSVGContainerFrame() ||
               type == LayoutFrameType::SVGForeignObject ||
               !kid->IsSVGFrame()) {
      ReflowSVGNonDisplayText(kid);
    }
  }
}

void SVGDisplayContainerFrame::Init(nsIContent* aContent,
                                    nsContainerFrame* aParent,
                                    nsIFrame* aPrevInFlow) {
  if (!IsSVGOuterSVGFrame()) {
    AddStateBits(aParent->GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD);
  }
  SVGContainerFrame::Init(aContent, aParent, aPrevInFlow);
}

void SVGDisplayContainerFrame::BuildDisplayList(
    nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists) {
  // content could be a XUL element so check for an SVG element
  if (auto* svg = SVGElement::FromNode(GetContent())) {
    if (!svg->HasValidDimensions()) {
      return;
    }
  }
  DisplayOutline(aBuilder, aLists);
  return BuildDisplayListForNonBlockChildren(aBuilder, aLists);
}

void SVGDisplayContainerFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  // memorize first old frame after insertion point
  // XXXbz once again, this would work a lot better if the nsIFrame
  // methods returned framelist iterators....
  nsIFrame* nextFrame = aPrevFrame ? aPrevFrame->GetNextSibling()
                                   : GetChildList(aListID).FirstChild();
  nsIFrame* firstNewFrame = aFrameList.FirstChild();

  // Insert the new frames
  SVGContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                  std::move(aFrameList));

  // If we are not a non-display SVG frame and we do not have a bounds update
  // pending, then we need to schedule one for our new children:
  if (!HasAnyStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN |
                       NS_FRAME_IS_NONDISPLAY)) {
    for (nsIFrame* kid = firstNewFrame; kid != nextFrame;
         kid = kid->GetNextSibling()) {
      ISVGDisplayableFrame* SVGFrame = do_QueryFrame(kid);
      if (SVGFrame && !kid->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
        bool isFirstReflow = kid->HasAnyStateBits(NS_FRAME_FIRST_REFLOW);
        // Remove bits so that ScheduleBoundsUpdate will work:
        kid->RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                             NS_FRAME_HAS_DIRTY_CHILDREN);
        // No need to invalidate the new kid's old bounds, so we just use
        // SVGUtils::ScheduleBoundsUpdate.
        SVGUtils::ScheduleReflowSVG(kid);
        if (isFirstReflow) {
          // Add back the NS_FRAME_FIRST_REFLOW bit:
          kid->AddStateBits(NS_FRAME_FIRST_REFLOW);
        }
      }
    }
  }
}

bool SVGDisplayContainerFrame::DoGetParentSVGTransforms(
    gfx::Matrix* aFromParentTransform) const {
  return SVGUtils::GetParentSVGTransforms(this, aFromParentTransform);
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

void SVGDisplayContainerFrame::PaintSVG(gfxContext& aContext,
                                        const gfxMatrix& aTransform,
                                        imgDrawingParams& aImgParams) {
  NS_ASSERTION(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) ||
                   PresContext()->Document()->IsSVGGlyphsDocument(),
               "Only painting of non-display SVG should take this code path");

  if (StyleEffects()->IsTransparent()) {
    return;
  }

  gfxMatrix matrix = aTransform;
  if (auto* svg = SVGElement::FromNode(GetContent())) {
    matrix = svg->ChildToUserSpaceTransform() * matrix;
    if (matrix.IsSingular()) {
      return;
    }
  }

  for (auto* kid : mFrames) {
    gfxMatrix m = matrix;
    // PaintFrameWithEffects() expects the transform that is passed to it to
    // include the transform to the passed frame's user space, so add it:
    const nsIContent* content = kid->GetContent();
    if (const SVGElement* element = SVGElement::FromNode(content)) {
      if (!element->HasValidDimensions()) {
        continue;  // nothing to paint for kid
      }

      m = SVGUtils::GetTransformMatrixInUserSpace(kid) * m;
      if (m.IsSingular()) {
        continue;
      }
    }
    SVGUtils::PaintFrameWithEffects(kid, aContext, m, aImgParams);
  }
}

nsIFrame* SVGDisplayContainerFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  NS_ASSERTION(HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD),
               "Only hit-testing of a clipPath's contents should take this "
               "code path");
  // First we transform aPoint into the coordinate space established by aFrame
  // for its children (e.g. take account of any 'viewBox' attribute):
  gfxPoint point = aPoint;
  if (const auto* svg = SVGElement::FromNode(GetContent())) {
    gfxMatrix m = svg->ChildToUserSpaceTransform();
    if (!m.IsIdentity()) {
      if (!m.Invert()) {
        return nullptr;
      }
      point = m.TransformPoint(point);
    }
  }

  // Traverse the list in reverse order, so that if we get a hit we know that's
  // the topmost frame that intersects the point; then we can just return it.
  nsIFrame* result = nullptr;
  for (nsIFrame* current = PrincipalChildList().LastChild(); current;
       current = current->GetPrevSibling()) {
    ISVGDisplayableFrame* SVGFrame = do_QueryFrame(current);
    if (!SVGFrame) {
      continue;
    }
    const nsIContent* content = current->GetContent();
    if (const auto* svg = SVGElement::FromNode(content)) {
      if (!svg->HasValidDimensions()) {
        continue;
      }
    }
    result = SVGFrame->GetFrameForPoint(point);
    if (result) {
      break;
    }
  }

  if (result && !SVGUtils::HitTestClip(this, aPoint)) {
    result = nullptr;
  }

  return result;
}

void SVGDisplayContainerFrame::ReflowSVG() {
  MOZ_ASSERT(SVGUtils::AnyOuterSVGIsCallingReflowSVG(this),
             "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  MOZ_ASSERT(!IsSVGOuterSVGFrame(), "Do not call on outer-<svg>");

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

  for (auto* kid : mFrames) {
    ISVGDisplayableFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame && !kid->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
      SVGFrame->ReflowSVG();

      // We build up our child frame overflows here instead of using
      // nsLayoutUtils::UnionChildOverflow since SVG frame's all use the same
      // frame list, and we're iterating over that list now anyway.
      ConsiderChildOverflow(overflowRects, kid);
    } else {
      // Inside a non-display container frame, we might have some
      // SVGTextFrames.  We need to cause those to get reflowed in
      // case they are the target of a rendering observer.
      MOZ_ASSERT(
          kid->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) || !kid->IsSVGFrame(),
          "expected kid to be a NS_FRAME_IS_NONDISPLAY frame or not SVG");
      if (kid->HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
        SVGContainerFrame* container = do_QueryFrame(kid);
        if (container && container->GetContent()->IsSVGElement()) {
          ReflowSVGNonDisplayText(container);
        }
      }
    }
  }

  // <svg> can create an SVG viewport with an offset due to its
  // x/y/width/height attributes, and <use> can introduce an offset with an
  // empty mRect (any width/height is copied to an anonymous <svg> child).
  // Other than that containers should not set mRect since all other offsets
  // come from transforms, which are accounted for by nsDisplayTransform.
  // Note that we rely on |overflow:visible| to allow display list items to be
  // created for our children.
  MOZ_ASSERT(mContent->IsAnyOfSVGElements(nsGkAtoms::svg, nsGkAtoms::symbol) ||
                 (mContent->IsSVGElement(nsGkAtoms::use) &&
                  mRect.Size() == nsSize(0, 0)) ||
                 mRect.IsEqualEdges(nsRect()),
             "Only inner-<svg>/<use> is expected to have mRect set");

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

void SVGDisplayContainerFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldStyle);
  if (!aOldStyle) {
    return;
  }
  if (StyleDisplay()->CalcTransformPropertyDifference(
          *aOldStyle->StyleDisplay())) {
    NotifySVGChanged(ChangeFlag::TransformChanged);
  }
}

void SVGDisplayContainerFrame::NotifySVGChanged(ChangeFlags aFlags) {
  MOZ_ASSERT(aFlags.contains(ChangeFlag::TransformChanged) ||
                 aFlags.contains(ChangeFlag::CoordContextChanged),
             "Invalidation logic may need adjusting");

  if (aFlags.contains(ChangeFlag::TransformChanged)) {
    // make sure our cached transform matrix gets (lazily) updated
    mCanvasTM = nullptr;
  }

  SVGUtils::NotifyChildrenOfSVGChange(this, aFlags);
}

SVGBBox SVGDisplayContainerFrame::GetBBoxContribution(
    const Matrix& aToBBoxUserspace, SVGBBoxFlags aFlags) {
  SVGBBox bboxUnion;

  for (nsIFrame* kid : mFrames) {
    ISVGDisplayableFrame* svgKid = do_QueryFrame(kid);
    if (!svgKid) {
      continue;
    }
    // content could be a XUL element
    auto* svg = SVGElement::FromNode(kid->GetContent());
    if (svg && !svg->HasValidDimensions()) {
      continue;
    }
    gfxMatrix transform = gfx::ThebesMatrix(aToBBoxUserspace);
    if (svg) {
      transform = svg->ChildToUserSpaceTransform() *
                  SVGUtils::GetTransformMatrixInUserSpace(kid) * transform;
    }
    // We need to include zero width/height vertical/horizontal lines, so we
    // have to use UnionEdges.
    bboxUnion.UnionEdges(
        svgKid->GetBBoxContribution(gfx::ToMatrix(transform), aFlags));
  }

  return bboxUnion;
}

gfxMatrix SVGDisplayContainerFrame::GetCanvasTM() {
  if (!mCanvasTM) {
    NS_ASSERTION(GetParent(), "null parent");
    auto* parent = static_cast<SVGContainerFrame*>(GetParent());
    auto* content = static_cast<SVGElement*>(GetContent());
    mCanvasTM = std::make_unique<gfxMatrix>(
        content->ChildToUserSpaceTransform() * parent->GetCanvasTM());
  }

  return *mCanvasTM;
}

}  // namespace mozilla
