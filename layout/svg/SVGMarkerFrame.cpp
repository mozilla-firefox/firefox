/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGMarkerFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGContextPaint.h"
#include "mozilla/SVGGeometryFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "mozilla/dom/SVGMarkerElement.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;

nsContainerFrame* NS_NewSVGMarkerFrame(mozilla::PresShell* aPresShell,
                                       mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGMarkerFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGMarkerFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

nsresult SVGMarkerFrame::AttributeChanged(int32_t aNameSpaceID,
                                          nsAtom* aAttribute,
                                          AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::markerUnits || aAttribute == nsGkAtoms::refX ||
       aAttribute == nsGkAtoms::refY || aAttribute == nsGkAtoms::markerWidth ||
       aAttribute == nsGkAtoms::markerHeight ||
       aAttribute == nsGkAtoms::orient ||
       aAttribute == nsGkAtoms::preserveAspectRatio ||
       aAttribute == nsGkAtoms::viewBox)) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }

  return SVGContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                             aModType);
}

#ifdef DEBUG
void SVGMarkerFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                          nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::marker),
               "Content is not an SVG marker");

  SVGContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

//----------------------------------------------------------------------
// SVGContainerFrame methods:

gfxMatrix SVGMarkerFrame::GetCanvasTM() {
  NS_ASSERTION(mMarkedFrame, "null SVGGeometry frame");

  if (mInUse2) {
    // We're going to be bailing drawing the marker, so return an identity.
    return gfxMatrix();
  }

  SVGMarkerElement* content = static_cast<SVGMarkerElement*>(GetContent());

  mInUse2 = true;
  gfxMatrix markedTM = mMarkedFrame->GetCanvasTM();
  mInUse2 = false;

  Matrix viewBoxTM = content->GetViewBoxTransform();

  return ThebesMatrix(viewBoxTM * mMarkerTM) * markedTM;
}

static nsIFrame* GetAnonymousChildFrame(nsIFrame* aFrame) {
  nsIFrame* kid = aFrame->PrincipalChildList().FirstChild();
  MOZ_ASSERT(kid && kid->IsSVGMarkerAnonChildFrame(),
             "expected to find anonymous child of marker frame");
  return kid;
}

void SVGMarkerFrame::PaintMark(gfxContext& aContext,
                               const gfxMatrix& aToMarkedFrameUserSpace,
                               SVGGeometryFrame* aMarkedFrame,
                               const SVGMark& aMark, float aStrokeWidth,
                               imgDrawingParams& aImgParams) {
  // If the flag is set when we get here, it means this marker frame
  // has already been used painting the current mark, and the document
  // has a marker reference loop.
  if (mInUse) {
    return;
  }

  AutoMarkerReferencer markerRef(this, aMarkedFrame);

  SVGMarkerElement* marker = static_cast<SVGMarkerElement*>(GetContent());
  if (!marker->HasValidDimensions()) {
    return;
  }

  const SVGViewBox viewBox = marker->GetViewBox();

  if (!viewBox.IsValid()) {
    // We must disable rendering if the viewBox width or height are zero.
    return;
  }

  Matrix viewBoxTM = marker->GetViewBoxTransform();

  mMarkerTM = marker->GetMarkerTransform(aStrokeWidth, aMark);

  gfxMatrix markTM = ThebesMatrix(viewBoxTM) * ThebesMatrix(mMarkerTM) *
                     aToMarkedFrameUserSpace;

  gfxClipAutoSaveRestore autoSaveClip(&aContext);
  if (StyleDisplay()->IsScrollableOverflow()) {
    gfxRect clipRect = SVGUtils::GetClipRectForFrame(
        this, viewBox.x, viewBox.y, viewBox.width, viewBox.height);
    autoSaveClip.TransformedClip(markTM, clipRect);
  }

  nsIFrame* kid = GetAnonymousChildFrame(this);
  ISVGDisplayableFrame* SVGFrame = do_QueryFrame(kid);
  // The CTM of each frame referencing us may be different.
  SVGFrame->NotifySVGChanged(
      ISVGDisplayableFrame::ChangeFlag::TransformChanged);
  auto contextPaint = MakeRefPtr<SVGContextPaintImpl>();
  contextPaint->Init(aContext.GetDrawTarget(),
                     aToMarkedFrameUserSpace * aContext.CurrentMatrixDouble(),
                     aMarkedFrame, SVGContextPaint::GetContextPaint(marker),
                     aImgParams);
  AutoSetRestoreSVGContextPaint autoSetRestore(contextPaint,
                                               marker->OwnerDoc());
  SVGUtils::PaintFrameWithEffects(kid, aContext, markTM, aImgParams);
}

SVGBBox SVGMarkerFrame::GetMarkBBoxContribution(const Matrix& aToBBoxUserspace,
                                                SVGBBoxFlags aFlags,
                                                SVGGeometryFrame* aMarkedFrame,
                                                const SVGMark& aMark,
                                                float aStrokeWidth) {
  SVGBBox bbox;

  // If the flag is set when we get here, it means this marker frame
  // has already been used in calculating the current mark bbox, and
  // the document has a marker reference loop.
  if (mInUse) {
    return bbox;
  }

  AutoMarkerReferencer markerRef(this, aMarkedFrame);

  SVGMarkerElement* content = static_cast<SVGMarkerElement*>(GetContent());
  if (!content->HasValidDimensions()) {
    return bbox;
  }

  const SVGViewBox viewBox = content->GetViewBox();

  if (!viewBox.IsValid()) {
    return bbox;
  }

  mMarkerTM = content->GetMarkerTransform(aStrokeWidth, aMark);
  Matrix viewBoxTM = content->GetViewBoxTransform();

  Matrix tm = viewBoxTM * mMarkerTM * aToBBoxUserspace;

  ISVGDisplayableFrame* child = do_QueryFrame(GetAnonymousChildFrame(this));
  // When we're being called to obtain the invalidation area, we need to
  // pass down all the flags so that stroke is included. However, once DOM
  // getBBox() accepts flags, maybe we should strip some of those here?

  // We need to include zero width/height vertical/horizontal lines, so we have
  // to use UnionEdges.
  bbox.UnionEdges(child->GetBBoxContribution(tm, aFlags));

  return bbox;
}

void SVGMarkerFrame::SetParentCoordCtxProvider(SVGViewportElement* aContext) {
  SVGMarkerElement* marker = static_cast<SVGMarkerElement*>(GetContent());
  marker->SetParentCoordCtxProvider(aContext);
}

void SVGMarkerFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  aResult.AppendElement(OwnedAnonBox(GetAnonymousChildFrame(this)));
}

//----------------------------------------------------------------------
// helper class

SVGMarkerFrame::AutoMarkerReferencer::AutoMarkerReferencer(
    SVGMarkerFrame* aFrame, SVGGeometryFrame* aMarkedFrame)
    : mFrame(aFrame) {
  mFrame->mInUse = true;
  mFrame->mMarkedFrame = aMarkedFrame;

  SVGViewportElement* ctx =
      static_cast<SVGElement*>(aMarkedFrame->GetContent())->GetCtx();
  mFrame->SetParentCoordCtxProvider(ctx);
}

SVGMarkerFrame::AutoMarkerReferencer::~AutoMarkerReferencer() {
  mFrame->SetParentCoordCtxProvider(nullptr);

  mFrame->mMarkedFrame = nullptr;
  mFrame->mInUse = false;
}

}  // namespace mozilla

//----------------------------------------------------------------------
// Implementation of SVGMarkerAnonChildFrame

nsContainerFrame* NS_NewSVGMarkerAnonChildFrame(
    mozilla::PresShell* aPresShell, mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGMarkerAnonChildFrame(aStyle,
                                              aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGMarkerAnonChildFrame)

#ifdef DEBUG
void SVGMarkerAnonChildFrame::Init(nsIContent* aContent,
                                   nsContainerFrame* aParent,
                                   nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(aParent->IsSVGMarkerFrame(), "Unexpected parent");
  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif

}  // namespace mozilla
