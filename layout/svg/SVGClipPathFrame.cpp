/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGClipPathFrame.h"

// Keep others in (case-insensitive) order:
#include "AutoReferenceChainGuard.h"
#include "ImgDrawResult.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGGeometryFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGClipPathElement.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "nsGkAtoms.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGClipPathFrame(mozilla::PresShell* aPresShell,
                                 mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGClipPathFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGClipPathFrame)

void SVGClipPathFrame::ApplyClipPath(gfxContext& aContext,
                                     nsIFrame* aClippedFrame,
                                     const gfxMatrix& aMatrix) {
  nsIFrame* singleClipPathChild = nullptr;
  DebugOnly<bool> trivial = IsTrivial(&singleClipPathChild);
  MOZ_ASSERT(trivial, "Caller needs to use GetClipMask");

  const DrawTarget* drawTarget = aContext.GetDrawTarget();

  // No need for AutoReferenceChainGuard since simple clip paths by definition
  // don't reference another clip path.

  // Restore current transform after applying clip path:
  gfxContextMatrixAutoSaveRestore autoRestoreTransform(&aContext);

  RefPtr<Path> clipPath;

  if (singleClipPathChild) {
    SVGGeometryFrame* pathFrame = do_QueryFrame(singleClipPathChild);
    if (pathFrame && pathFrame->StyleVisibility()->IsVisible()) {
      SVGGeometryElement* pathElement =
          static_cast<SVGGeometryElement*>(pathFrame->GetContent());

      gfxMatrix toChildsUserSpace =
          SVGUtils::GetTransformMatrixInUserSpace(pathFrame) *
          (GetClipPathTransform(aClippedFrame) * aMatrix);

      gfxMatrix newMatrix = aContext.CurrentMatrixDouble()
                                .PreMultiply(toChildsUserSpace)
                                .NudgeToIntegers();
      if (!newMatrix.IsSingular()) {
        aContext.SetMatrixDouble(newMatrix);
        FillRule clipRule =
            SVGUtils::ToFillRule(pathFrame->StyleSVG()->mClipRule);
        clipPath = pathElement->GetOrBuildPath(drawTarget, clipRule);
      }
    }
  }

  if (clipPath) {
    aContext.Clip(clipPath);
  } else {
    // The spec says clip away everything if we have no children or the
    // clipping path otherwise can't be resolved:
    aContext.Clip(Rect());
  }
}

static void ComposeExtraMask(DrawTarget* aTarget, SourceSurface* aExtraMask) {
  MOZ_ASSERT(aExtraMask);

  Matrix origin = aTarget->GetTransform();
  aTarget->SetTransform(Matrix());
  aTarget->MaskSurface(ColorPattern(DeviceColor(0.0, 0.0, 0.0, 1.0)),
                       aExtraMask, Point(0, 0),
                       DrawOptions(1.0, CompositionOp::OP_IN));
  aTarget->SetTransform(origin);
}

void SVGClipPathFrame::PaintChildren(gfxContext& aMaskContext,
                                     nsIFrame* aClippedFrame,
                                     const gfxMatrix& aMatrix) {
  SVGUtils::MaskUsage maskUsage = SVGUtils::DetermineMaskUsage(this, true);
  gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aMaskContext);

  if (maskUsage.ShouldApplyClipPath()) {
    SVGClipPathFrame* clipPathThatClipsClipPath;
    // Check if this clipPath is itself clipped by another clipPath:
    SVGObserverUtils::GetAndObserveClipPath(this, &clipPathThatClipsClipPath);
    clipPathThatClipsClipPath->ApplyClipPath(aMaskContext, aClippedFrame,
                                             aMatrix);
  } else if (maskUsage.ShouldGenerateClipMaskLayer()) {
    SVGClipPathFrame* clipPathThatClipsClipPath;
    // Check if this clipPath is itself clipped by another clipPath:
    SVGObserverUtils::GetAndObserveClipPath(this, &clipPathThatClipsClipPath);
    RefPtr<SourceSurface> maskSurface = clipPathThatClipsClipPath->GetClipMask(
        aMaskContext, aClippedFrame, aMatrix);
    // We want the mask to be untransformed so use the inverse of the current
    // transform as the maskTransform to compensate.
    Matrix maskTransform = aMaskContext.CurrentMatrix().Inverse();
    autoGroupForBlend.PushGroupForBlendBack(gfxContentType::ALPHA, 1.0f,
                                            maskSurface, maskTransform);
  }

  // Paint our children into the mask:
  for (auto* kid : mFrames) {
    PaintFrameIntoMask(kid, aClippedFrame, aMaskContext);
  }

  if (maskUsage.ShouldApplyClipPath()) {
    aMaskContext.PopClip();
  }
}

void SVGClipPathFrame::PaintClipMask(gfxContext& aMaskContext,
                                     nsIFrame* aClippedFrame,
                                     const gfxMatrix& aMatrix,
                                     SourceSurface* aExtraMask) {
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;

  // A clipPath can reference another clipPath, creating a chain of clipPaths
  // that must all be applied.  We re-enter this method for each clipPath in a
  // chain, so we need to protect against reference chain related crashes etc.:
  AutoReferenceChainGuard refChainGuard(this, &mIsBeingProcessed,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    return;  // Break reference chain
  }
  if (!IsValid()) {
    return;
  }

  DrawTarget* maskDT = aMaskContext.GetDrawTarget();
  MOZ_ASSERT(maskDT->GetFormat() == SurfaceFormat::A8);

  // Paint this clipPath's contents into aMaskDT:
  // We need to set mMatrixForChildren here so that under the PaintSVG calls
  // on our children (below) our GetCanvasTM() method will return the correct
  // transform.
  mMatrixForChildren = GetClipPathTransform(aClippedFrame) * aMatrix;

  PaintChildren(aMaskContext, aClippedFrame, aMatrix);

  if (aExtraMask) {
    ComposeExtraMask(maskDT, aExtraMask);
  }
}

void SVGClipPathFrame::PaintFrameIntoMask(nsIFrame* aFrame,
                                          nsIFrame* aClippedFrame,
                                          gfxContext& aTarget) {
  ISVGDisplayableFrame* frame = do_QueryFrame(aFrame);
  if (!frame) {
    return;
  }

  // The CTM of each frame referencing us can be different.
  frame->NotifySVGChanged(ISVGDisplayableFrame::ChangeFlag::TransformChanged);

  // Children of this clipPath may themselves be clipped.
  SVGClipPathFrame* clipPathThatClipsChild;
  // XXX check return value?
  if (SVGObserverUtils::GetAndObserveClipPath(aFrame,
                                              &clipPathThatClipsChild) ==
      SVGObserverUtils::ReferenceState::HasRefsSomeInvalid) {
    return;
  }

  SVGUtils::MaskUsage maskUsage = SVGUtils::DetermineMaskUsage(aFrame, true);
  gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aTarget);
  if (maskUsage.ShouldApplyClipPath()) {
    clipPathThatClipsChild->ApplyClipPath(
        aTarget, aClippedFrame,
        SVGUtils::GetTransformMatrixInUserSpace(aFrame) * mMatrixForChildren);
  } else if (maskUsage.ShouldGenerateClipMaskLayer()) {
    RefPtr<SourceSurface> maskSurface = clipPathThatClipsChild->GetClipMask(
        aTarget, aClippedFrame,
        SVGUtils::GetTransformMatrixInUserSpace(aFrame) * mMatrixForChildren);

    // We want the mask to be untransformed so use the inverse of the current
    // transform as the maskTransform to compensate.
    Matrix maskTransform = aTarget.CurrentMatrix().Inverse();
    autoGroupForBlend.PushGroupForBlendBack(gfxContentType::ALPHA, 1.0f,
                                            maskSurface, maskTransform);
  }

  gfxMatrix toChildsUserSpace = mMatrixForChildren;
  nsIFrame* child = do_QueryFrame(frame);
  nsIContent* childContent = child->GetContent();
  if (childContent->IsSVGElement()) {
    toChildsUserSpace =
        SVGUtils::GetTransformMatrixInUserSpace(child) * mMatrixForChildren;
  }

  // clipPath does not result in any image rendering, so we just use a dummy
  // imgDrawingParams instead of requiring our caller to pass one.
  image::imgDrawingParams imgParams;

  // Our children have NS_STATE_SVG_CLIPPATH_CHILD set on them, and
  // SVGGeometryFrame::Render checks for that state bit and paints
  // only the geometry (opaque black) if set.
  frame->PaintSVG(aTarget, toChildsUserSpace, imgParams);

  if (maskUsage.ShouldApplyClipPath()) {
    aTarget.PopClip();
  }
}

already_AddRefed<SourceSurface> SVGClipPathFrame::GetClipMask(
    gfxContext& aReferenceContext, nsIFrame* aClippedFrame,
    const gfxMatrix& aMatrix, SourceSurface* aExtraMask) {
  RefPtr<DrawTarget> maskDT =
      aReferenceContext.GetDrawTarget()->CreateClippedDrawTarget(
          Rect(), SurfaceFormat::A8);
  if (!maskDT) {
    return nullptr;
  }

  gfxContext maskContext(maskDT, /* aPreserveTransform */ true);
  PaintClipMask(maskContext, aClippedFrame, aMatrix, aExtraMask);

  RefPtr<SourceSurface> surface = maskDT->Snapshot();
  return surface.forget();
}

bool SVGClipPathFrame::PointIsInsideClipPath(nsIFrame* aClippedFrame,
                                             const gfxPoint& aPoint) {
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;

  // A clipPath can reference another clipPath, creating a chain of clipPaths
  // that must all be applied.  We re-enter this method for each clipPath in a
  // chain, so we need to protect against reference chain related crashes etc.:
  AutoReferenceChainGuard refChainGuard(this, &mIsBeingProcessed,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    return false;  // Break reference chain
  }
  if (!IsValid()) {
    return false;
  }

  gfxMatrix matrix = GetClipPathTransform(aClippedFrame);
  if (!matrix.Invert()) {
    return false;
  }
  gfxPoint point = matrix.TransformPoint(aPoint);

  // clipPath elements can themselves be clipped by a different clip path. In
  // that case the other clip path further clips away the element that is being
  // clipped by the original clipPath. If this clipPath is being clipped by a
  // different clip path we need to check if it prevents the original element
  // from receiving events at aPoint:
  SVGClipPathFrame* clipPathFrame;
  // XXX check return value?
  SVGObserverUtils::GetAndObserveClipPath(this, &clipPathFrame);
  if (clipPathFrame &&
      !clipPathFrame->PointIsInsideClipPath(aClippedFrame, aPoint)) {
    return false;
  }

  for (auto* kid : mFrames) {
    ISVGDisplayableFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      gfxPoint pointForChild = point;

      gfxMatrix m = SVGUtils::GetTransformMatrixInUserSpace(kid);
      if (!m.IsIdentity()) {
        if (!m.Invert()) {
          return false;
        }
        pointForChild = m.TransformPoint(point);
      }
      if (SVGFrame->GetFrameForPoint(pointForChild)) {
        return true;
      }
    }
  }

  return false;
}

bool SVGClipPathFrame::IsTrivial(nsIFrame** aSingleChild) {
  // If the clip path is clipped then it's non-trivial
  if (SVGObserverUtils::GetAndObserveClipPath(this, nullptr) ==
      SVGObserverUtils::ReferenceState::HasRefsAllValid) {
    return false;
  }

  if (aSingleChild) {
    *aSingleChild = nullptr;
  }

  nsIFrame* foundChild = nullptr;
  for (auto* kid : mFrames) {
    ISVGDisplayableFrame* svgChild = do_QueryFrame(kid);
    if (!svgChild) {
      continue;
    }
    // An invisible leaf contributes nothing to the clip, so it does not make
    // the clipPath non-trivial. Containers are not skipped because a
    // descendant can set visibility back to visible.
    if (!svgChild->IsDisplayContainer() &&
        !kid->StyleVisibility()->IsVisible()) {
      continue;
    }
    // We consider a non-trivial clipPath to be one containing
    // either more than one svg child and/or a svg container
    if (foundChild || svgChild->IsDisplayContainer()) {
      return false;
    }

    // or where the child is itself clipped
    if (SVGObserverUtils::GetAndObserveClipPath(kid, nullptr) ==
        SVGObserverUtils::ReferenceState::HasRefsAllValid) {
      return false;
    }

    foundChild = kid;
  }
  if (aSingleChild) {
    *aSingleChild = foundChild;
  }
  return true;
}

bool SVGClipPathFrame::IsValid() {
  if (SVGObserverUtils::GetAndObserveClipPath(this, nullptr) ==
      SVGObserverUtils::ReferenceState::HasRefsSomeInvalid) {
    return false;
  }

  for (auto* kid : mFrames) {
    LayoutFrameType kidType = kid->Type();

    if (kidType == LayoutFrameType::SVGUse) {
      for (nsIFrame* grandKid : kid->PrincipalChildList()) {
        LayoutFrameType grandKidType = grandKid->Type();

        if (grandKidType != LayoutFrameType::SVGGeometry &&
            grandKidType != LayoutFrameType::SVGText) {
          return false;
        }
      }
      continue;
    }

    if (kidType != LayoutFrameType::SVGGeometry &&
        kidType != LayoutFrameType::SVGText) {
      return false;
    }
  }

  return true;
}

nsresult SVGClipPathFrame::AttributeChanged(int32_t aNameSpaceID,
                                            nsAtom* aAttribute,
                                            AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::clipPathUnits) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }
  return SVGContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                             aModType);
}

#ifdef DEBUG
void SVGClipPathFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::clipPath),
               "Content is not an SVG clipPath!");

  SVGContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif

gfxMatrix SVGClipPathFrame::GetCanvasTM() { return mMatrixForChildren; }

bool SVGClipPathFrame::GetSimpleClipShape(nsIFrame* aClippedFrame,
                                          CSSRect* aRect, CSSSize* aRadii) {
  nsIFrame* singleClipPathChild = nullptr;
  if (!IsTrivial(&singleClipPathChild)) {
    return false;
  }
  if (!singleClipPathChild) {
    // The spec says to clip away everything when there is nothing to clip
    // with, which matches what ApplyClipPath does.
    *aRect = CSSRect();
    *aRadii = CSSSize();
    return true;
  }
  SVGGeometryFrame* pathFrame = do_QueryFrame(singleClipPathChild);
  if (!pathFrame) {
    return false;
  }

  auto* element = static_cast<SVGGeometryElement*>(pathFrame->GetContent());
  SVGGeometryElement::SimpleShape shape;
  element->GetAsSimpleShape(&shape);
  if (!shape.IsRoundedRect()) {
    return false;
  }

  gfxMatrix toClippedUserSpace =
      SVGUtils::GetTransformMatrixInUserSpace(pathFrame) *
      GetClipPathTransform(aClippedFrame);
  if (toClippedUserSpace.IsSingular() ||
      toClippedUserSpace.HasNonAxisAlignedTransform() ||
      toClippedUserSpace._11 < 0 || toClippedUserSpace._22 < 0) {
    return false;
  }

  gfxRect rect = toClippedUserSpace.TransformBounds(
      ThebesRect(shape.AsRect().ToUnknownRect()));
  *aRect = CSSRect::FromUnknownRect(ToRect(rect));
  *aRadii = CSSSize(float(shape.Radii().width * toClippedUserSpace._11),
                    float(shape.Radii().height * toClippedUserSpace._22));
  return true;
}

gfxMatrix SVGClipPathFrame::GetClipPathTransform(nsIFrame* aClippedFrame) {
  gfxMatrix tm = SVGUtils::GetTransformMatrixInUserSpace(this);

  auto* content = static_cast<SVGClipPathElement*>(GetContent());
  SVGAnimatedEnumeration* clipPathUnits =
      &content->mEnumAttributes[SVGClipPathElement::CLIPPATHUNITS];

  SVGBBoxFlags flags = SVGBBoxFlag::IncludeFillGeometry;

  if (aClippedFrame->StyleBorder()->mBoxDecorationBreak ==
      StyleBoxDecorationBreak::Clone) {
    flags += SVGBBoxFlag::IncludeOnlyCurrentFrameForNonSVGElement;
  }

  return SVGUtils::AdjustMatrixForUnits(tm, clipPathUnits, aClippedFrame,
                                        flags);
}

SVGBBox SVGClipPathFrame::GetBBoxForClipPathFrame(const SVGBBox& aBBox,
                                                  const gfxMatrix& aMatrix,
                                                  SVGBBoxFlags aFlags) {
  SVGClipPathFrame* clipPathThatClipsClipPath;
  if (SVGObserverUtils::GetAndObserveClipPath(this,
                                              &clipPathThatClipsClipPath) ==
      SVGObserverUtils::ReferenceState::HasRefsSomeInvalid) {
    return SVGBBox();
  }

  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mIsBeingProcessed,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    return SVGBBox();  // Break reference chain
  }

  nsIContent* node = GetContent()->GetFirstChild();
  SVGBBox unionBBox;
  for (; node; node = node->GetNextSibling()) {
    if (nsIFrame* frame = node->GetPrimaryFrame()) {
      ISVGDisplayableFrame* svg = do_QueryFrame(frame);
      if (svg) {
        gfxMatrix matrix =
            SVGUtils::GetTransformMatrixInUserSpace(frame) * aMatrix;
        SVGBBox tmpBBox = svg->GetBBoxContribution(
            gfx::ToMatrix(matrix), SVGBBoxFlag::IncludeFillGeometry);
        SVGClipPathFrame* clipPathFrame;
        if (SVGObserverUtils::GetAndObserveClipPath(frame, &clipPathFrame) !=
                SVGObserverUtils::ReferenceState::HasRefsSomeInvalid &&
            clipPathFrame) {
          tmpBBox =
              clipPathFrame->GetBBoxForClipPathFrame(tmpBBox, aMatrix, aFlags);
        }
        if (!aFlags.contains(
                SVGBBoxFlag::DoNotClipToBBoxOfContentInsideClipPath)) {
          tmpBBox.Intersect(aBBox);
        }
        unionBBox.UnionEdges(tmpBBox);
      }
    }
  }

  if (clipPathThatClipsClipPath) {
    unionBBox.Intersect(clipPathThatClipsClipPath->GetBBoxForClipPathFrame(
        aBBox, aMatrix, aFlags));
  }
  return unionBBox;
}

}  // namespace mozilla
