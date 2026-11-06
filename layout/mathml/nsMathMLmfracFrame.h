/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMathMLmfracFrame_h_
#define nsMathMLmfracFrame_h_

#include "nsMathMLContainerFrame.h"

namespace mozilla {
class PresShell;
}  // namespace mozilla

//
// <mfrac> -- form a fraction from two subexpressions
//

/*
The MathML REC describes:

The <mfrac> element is used for fractions. It can also be used to mark up
fraction-like objects such as binomial coefficients and Legendre symbols.
The syntax for <mfrac> is:
  <mfrac> numerator denominator </mfrac>

Attributes of <mfrac>:
     Name                      values                     default
  linethickness  number [ v-unit ] | thin | medium | thick  1

E.g.,
linethickness=2 actually means that linethickness=2*DEFAULT_THICKNESS
(DEFAULT_THICKNESS is not specified by MathML, see below.)

The linethickness attribute indicates the thickness of the horizontal
"fraction bar", or "rule", typically used to render fractions. A fraction
with linethickness="0" renders without the bar, and might be used within
binomial coefficients. A linethickness greater than one might be used with
nested fractions.

In general, the value of linethickness can be a number, as a multiplier
of the default thickness of the fraction bar (the default thickness is
not specified by MathML), or a number with a unit of vertical length (see
Section 2.3.3), or one of the keywords medium (same as 1), thin (thinner
than 1, otherwise up to the renderer), or thick (thicker than 1, otherwise
up to the renderer).

The <mfrac> element sets displaystyle to "false", or if it was already
false increments scriptlevel by 1, within numerator and denominator.
These attributes are inherited by every element from its rendering
environment, but can be set explicitly only on the <mstyle>
element.
*/

class nsMathMLmfracFrame final : public nsMathMLContainerFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsMathMLmfracFrame)

  friend nsIFrame* NS_NewMathMLmfracFrame(mozilla::PresShell* aPresShell,
                                          ComputedStyle* aStyle);

  MathMLFrameType GetMathMLFrameType() override;

  void Place(DrawTarget* aDrawTarget, const PlaceFlags& aFlags,
             ReflowOutput& aDesiredSize) override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            AttrModType aModType) override;

  NS_IMETHOD
  TransmitAutomaticData() override;

  // override the base method so that we can deal with the fraction line
  nscoord FixInterFrameSpacing(ReflowOutput& aDesiredSize) override;

  // helper to translate the thickness attribute into a usable form
  nscoord CalcLineThickness(nsString& aThicknessAttribute, nscoord onePixel,
                            nscoord aDefaultRuleThickness,
                            float aFontSizeInflation);

  uint8_t ScriptIncrement(nsIFrame* aFrame) override;

 protected:
  explicit nsMathMLmfracFrame(ComputedStyle* aStyle,
                              nsPresContext* aPresContext)
      : nsMathMLContainerFrame(aStyle, aPresContext, kClassID),
        mSlashChar(nullptr),
        mLineThickness(0) {}
  virtual ~nsMathMLmfracFrame();

  bool IsMathContentBoxHorizontallyCentered() const final { return true; }

  nsRect mLineRect;
  nsMathMLChar* mSlashChar;
  nscoord mLineThickness;
};

#endif /* nsMathMLmfracFrame_h_ */
