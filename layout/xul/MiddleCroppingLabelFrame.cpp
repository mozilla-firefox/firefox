/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MiddleCroppingLabelFrame.h"

#include "MiddleCroppingBlockFrame.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/Element.h"

nsIFrame* NS_NewMiddleCroppingLabelFrame(mozilla::PresShell* aPresShell,
                                         mozilla::ComputedStyle* aStyle) {
  return new mozilla::MiddleCroppingLabelFrame(aStyle,
                                               aPresShell->GetPresContext());
}

namespace mozilla {

void MiddleCroppingLabelFrame::GetUncroppedValue(nsAString& aValue) {
  mContent->AsElement()->GetAttr(nsGkAtoms::value, aValue);
}

nsresult MiddleCroppingLabelFrame::AttributeChanged(int32_t aNameSpaceID,
                                                    nsAtom* aAttribute,
                                                    AttrModType) {
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::value) {
    UpdateDisplayedValueToUncroppedValue(true);
  }
  return NS_OK;
}

NS_QUERYFRAME_HEAD(MiddleCroppingLabelFrame)
  NS_QUERYFRAME_ENTRY(MiddleCroppingLabelFrame)
NS_QUERYFRAME_TAIL_INHERITING(MiddleCroppingBlockFrame)
NS_IMPL_FRAMEARENA_HELPERS(MiddleCroppingLabelFrame)

}  // namespace mozilla
