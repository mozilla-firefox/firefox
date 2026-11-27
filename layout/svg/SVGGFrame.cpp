/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGGFrame.h"

// Keep others in (case-insensitive) order:
#include "mozilla/PresShell.h"
#include "mozilla/dom/SVGElement.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"

using namespace mozilla::dom;

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGGFrame(mozilla::PresShell* aPresShell,
                          mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGGFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGGFrame)

#ifdef DEBUG
void SVGGFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                     nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement() &&
                   static_cast<SVGElement*>(aContent)->IsTransformable(),
               "The element is not transformable");

  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

nsresult SVGGFrame::AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                                     AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::transform) {
    // We don't invalidate for transform changes (the layers code does that).
    // Also note that SVGTransformableElement::GetAttributeChangeHint will
    // return nsChangeHint_UpdateOverflow for "transform" attribute changes
    // and cause DoApplyRenderingChangeToTree to make the SchedulePaint call.
    NotifySVGChanged(ChangeFlag::TransformChanged);
  }

  return NS_OK;
}

}  // namespace mozilla
