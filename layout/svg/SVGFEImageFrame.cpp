/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/SVGFEImageElement.h"
#include "nsContainerFrame.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsLiteralString.h"

using namespace mozilla::dom;

nsIFrame* NS_NewSVGFEImageFrame(mozilla::PresShell* aPresShell,
                                mozilla::ComputedStyle* aStyle);

namespace mozilla {

class SVGFEImageFrame final : public nsIFrame {
  friend nsIFrame* ::NS_NewSVGFEImageFrame(mozilla::PresShell* aPresShell,
                                           ComputedStyle* aStyle);

 protected:
  explicit SVGFEImageFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {
    AddStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY);

    // This frame isn't actually displayed, but it contains an image and we want
    // to use the nsImageLoadingContent machinery for managing images, which
    // requires visibility tracking, so we enable visibility tracking and
    // forcibly mark it visible below.
    EnableVisibilityTracking();
  }

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGFEImageFrame)

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
  void Destroy(DestroyContext&) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGFEImage"_ns, aResult);
  }
#endif

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            AttrModType aModType) override;

  void OnVisibilityChange(
      Visibility aNewVisibility,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing()) override;

  bool ComputeCustomOverflow(OverflowAreas& aOverflowAreas) override {
    // We don't maintain a ink overflow rect
    return false;
  }
};

}  // namespace mozilla

nsIFrame* NS_NewSVGFEImageFrame(mozilla::PresShell* aPresShell,
                                mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGFEImageFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGFEImageFrame)

/* virtual */
void SVGFEImageFrame::Destroy(DestroyContext& aContext) {
  DecApproximateVisibleCount();

  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  if (imageLoader) {
    imageLoader->FrameDestroyed(this);
  }

  nsIFrame::Destroy(aContext);
}

void SVGFEImageFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                           nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::feImage),
               "Trying to construct an SVGFEImageFrame for a "
               "content element that doesn't support the right interfaces");

  nsIFrame::Init(aContent, aParent, aPrevInFlow);

  // We assume that feImage's are always visible.
  // This call must happen before the FrameCreated. This is because the
  // primary frame pointer on our content node isn't set until after this
  // function ends, so there is no way for the resulting OnVisibilityChange
  // notification to get a frame. FrameCreated has a workaround for this in
  // that it passes our frame around so it can be accessed. OnVisibilityChange
  // doesn't have that workaround.
  IncApproximateVisibleCount();

  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  if (imageLoader) {
    imageLoader->FrameCreated(this);
  }
}

nsresult SVGFEImageFrame::AttributeChanged(int32_t aNameSpaceID,
                                           nsAtom* aAttribute,
                                           AttrModType aModType) {
  SVGFEImageElement* element = static_cast<SVGFEImageElement*>(GetContent());
  if (element->AttributeAffectsRendering(aNameSpaceID, aAttribute)) {
    MOZ_ASSERT(
        GetParent()->IsSVGFilterFrame(),
        "Observers observe the filter, so that's what we must invalidate");
    SVGObserverUtils::InvalidateRenderingObservers(GetParent());
  }

  return nsIFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

void SVGFEImageFrame::OnVisibilityChange(
    Visibility aNewVisibility, const Maybe<OnNonvisible>& aNonvisibleAction) {
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);
  if (imageLoader) {
    imageLoader->OnVisibilityChange(aNewVisibility, aNonvisibleAction);
  }

  nsIFrame::OnVisibilityChange(aNewVisibility, aNonvisibleAction);
}

}  // namespace mozilla
