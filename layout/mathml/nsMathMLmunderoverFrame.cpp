/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmunderoverFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "gfxMathTable.h"
#include "gfxTextRun.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MathMLElement.h"
#include "nsIMathMLFrame.h"
#include "nsLayoutUtils.h"
#include "nsMathMLmmultiscriptsFrame.h"
#include "nsPresContext.h"

using namespace mozilla;

//
// <munderover> -- attach an underscript-overscript pair to a base
//                 implementation
// <mover> -- attach an overscript to a base - implementation
// <munder> -- attach an underscript to a base - implementation
//

nsIFrame* NS_NewMathMLmunderoverFrame(PresShell* aPresShell,
                                      ComputedStyle* aStyle) {
  return new nsMathMLmunderoverFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmunderoverFrame)

nsMathMLmunderoverFrame::~nsMathMLmunderoverFrame() = default;

nsresult nsMathMLmunderoverFrame::AttributeChanged(int32_t aNameSpaceID,
                                                   nsAtom* aAttribute,
                                                   AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (nsGkAtoms::accent == aAttribute ||
       nsGkAtoms::accentunder == aAttribute)) {
    // When we have automatic data to update within ourselves, we ask our
    // parent to re-layout its children
    return ReLayoutChildren(GetParent());
  }

  return nsMathMLContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                  aModType);
}

NS_IMETHODIMP
nsMathMLmunderoverFrame::UpdatePresentationData(
    MathMLPresentationFlags aFlagsValues,
    MathMLPresentationFlags aFlagsToUpdate) {
  nsMathMLContainerFrame::UpdatePresentationData(aFlagsValues, aFlagsToUpdate);
  // disable the stretch-all flag if we are going to act like a
  // subscript-superscript pair
  if (mEmbellishData.flags.contains(MathMLEmbellishFlag::MovableLimits) &&
      StyleFont()->mMathStyle == StyleMathStyle::Compact) {
    mPresentationData.flags -=
        MathMLPresentationFlag::StretchAllChildrenHorizontally;
  } else {
    mPresentationData.flags +=
        MathMLPresentationFlag::StretchAllChildrenHorizontally;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMathMLmunderoverFrame::InheritAutomaticData(nsIFrame* aParent) {
  // let the base class get the default from our parent
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  mPresentationData.flags +=
      MathMLPresentationFlag::StretchAllChildrenHorizontally;

  return NS_OK;
}

void nsMathMLmunderoverFrame::Destroy(DestroyContext& aContext) {
  if (!mPostReflowIncrementScriptLevelCommands.IsEmpty()) {
    PresShell()->CancelReflowCallback(this);
  }
  nsMathMLContainerFrame::Destroy(aContext);
}

uint8_t nsMathMLmunderoverFrame::ScriptIncrement(nsIFrame* aFrame) {
  nsIFrame* child = mFrames.FirstChild();
  if (!aFrame || aFrame == child) {
    return 0;
  }
  child = child->GetNextSibling();
  if (aFrame == child) {
    if (mContent->IsMathMLElement(nsGkAtoms::mover)) {
      return mIncrementOver ? 1 : 0;
    }
    return mIncrementUnder ? 1 : 0;
  }
  if (child && aFrame == child->GetNextSibling()) {
    // must be a over frame of munderover
    return mIncrementOver ? 1 : 0;
  }
  return 0;  // frame not found
}

void nsMathMLmunderoverFrame::SetIncrementScriptLevel(uint32_t aChildIndex,
                                                      bool aIncrement) {
  nsIFrame* child = PrincipalChildList().FrameAt(aChildIndex);
  if (!child || !child->GetContent()->IsMathMLElement() ||
      child->GetContent()->GetPrimaryFrame() != child) {
    return;
  }

  auto element = dom::MathMLElement::FromNode(child->GetContent());
  if (element->GetIncrementScriptLevel() == aIncrement) {
    return;
  }

  if (mPostReflowIncrementScriptLevelCommands.IsEmpty()) {
    PresShell()->PostReflowCallback(this);
  }

  mPostReflowIncrementScriptLevelCommands.AppendElement(
      SetIncrementScriptLevelCommand{aChildIndex, aIncrement});
}

bool nsMathMLmunderoverFrame::ReflowFinished() {
  SetPendingPostReflowIncrementScriptLevel();
  return true;
}

void nsMathMLmunderoverFrame::ReflowCallbackCanceled() {
  // Do nothing, at this point our work will just be useless.
  mPostReflowIncrementScriptLevelCommands.Clear();
}

void nsMathMLmunderoverFrame::SetPendingPostReflowIncrementScriptLevel() {
  MOZ_ASSERT(!mPostReflowIncrementScriptLevelCommands.IsEmpty());

  nsTArray<SetIncrementScriptLevelCommand> commands =
      std::move(mPostReflowIncrementScriptLevelCommands);

  for (const auto& command : commands) {
    nsIFrame* child = PrincipalChildList().FrameAt(command.mChildIndex);
    if (!child || !child->GetContent()->IsMathMLElement()) {
      continue;
    }

    auto element = dom::MathMLElement::FromNode(child->GetContent());
    element->SetIncrementScriptLevel(command.mDoIncrement, true);
  }
}

NS_IMETHODIMP
nsMathMLmunderoverFrame::TransmitAutomaticData() {
  // At this stage, all our children are in sync and we can fully
  // resolve our own mEmbellishData struct
  //---------------------------------------------------------------------

  /*
  The REC says:

  As regards munder (respectively mover) :
  The default value of accentunder is false, unless underscript
  is an <mo> element or an embellished operator.  If underscript is
  an <mo> element, the value of its accent attribute is used as the
  default value of accentunder. If underscript is an embellished
  operator, the accent attribute of the <mo> element at its
  core is used as the default value. As with all attributes, an
  explicitly given value overrides the default.

XXX The winner is the outermost setting in conflicting settings like these:
<munder accentunder='true'>
  <mi>...</mi>
  <mo accentunder='false'> ... </mo>
</munder>

  As regards munderover:
  The accent and accentunder attributes have the same effect as
  the attributes with the same names on <mover>  and <munder>,
  respectively. Their default values are also computed in the
  same manner as described for those elements, with the default
  value of accent depending on overscript and the default value
  of accentunder depending on underscript.
  */

  nsIFrame* overscriptFrame = nullptr;
  nsIFrame* underscriptFrame = nullptr;
  nsIFrame* baseFrame = mFrames.FirstChild();

  if (baseFrame) {
    if (mContent->IsAnyOfMathMLElements(nsGkAtoms::munder,
                                        nsGkAtoms::munderover)) {
      underscriptFrame = baseFrame->GetNextSibling();
    } else {
      NS_ASSERTION(mContent->IsMathMLElement(nsGkAtoms::mover),
                   "mContent->NodeInfo()->NameAtom() not recognized");
      overscriptFrame = baseFrame->GetNextSibling();
    }
  }
  if (underscriptFrame && mContent->IsMathMLElement(nsGkAtoms::munderover)) {
    overscriptFrame = underscriptFrame->GetNextSibling();
  }

  // if our base is an embellished operator, let its state bubble to us (in
  // particular, this is where we get the flag for
  // MovableLimits). Our flags are reset to the default
  // values of false if the base frame isn't embellished.
  mPresentationData.baseFrame = baseFrame;
  GetEmbellishDataFrom(baseFrame, mEmbellishData);

  // The default value of accentunder is false, unless the underscript is
  // embellished and its core <mo> is an accent
  nsEmbellishData embellishData;
  nsAutoString value;
  if (mContent->IsAnyOfMathMLElements(nsGkAtoms::munder,
                                      nsGkAtoms::munderover)) {
    GetEmbellishDataFrom(underscriptFrame, embellishData);
    if (embellishData.flags.contains(MathMLEmbellishFlag::Accent)) {
      mEmbellishData.flags += MathMLEmbellishFlag::AccentUnder;
    } else {
      mEmbellishData.flags -= MathMLEmbellishFlag::AccentUnder;
    }

    // if we have an accentunder attribute, it overrides what the underscript
    // said
    if (mContent->AsElement()->GetAttr(nsGkAtoms::accentunder, value)) {
      if (value.LowerCaseEqualsLiteral("true")) {
        mEmbellishData.flags += MathMLEmbellishFlag::AccentUnder;
      } else if (value.LowerCaseEqualsLiteral("false")) {
        mEmbellishData.flags -= MathMLEmbellishFlag::AccentUnder;
      }
    } else if (mEmbellishData.flags.contains(
                   MathMLEmbellishFlag::AccentUnder)) {
      AutoTArray<nsString, 1> params;
      params.AppendElement(mContent->NodeInfo()->NodeName());
      PresContext()->Document()->WarnOnceAbout(
          dom::DeprecatedOperations::
              eMathML_DeprecatedMunderNonExplicitAccentunder,
          false, params);
    }
  }

  // The default value of accent is false, unless the overscript is embellished
  // and its core <mo> is an accent
  if (mContent->IsAnyOfMathMLElements(nsGkAtoms::mover,
                                      nsGkAtoms::munderover)) {
    GetEmbellishDataFrom(overscriptFrame, embellishData);
    if (embellishData.flags.contains(MathMLEmbellishFlag::Accent)) {
      mEmbellishData.flags += MathMLEmbellishFlag::AccentOver;
    } else {
      mEmbellishData.flags -= MathMLEmbellishFlag::AccentOver;
    }

    // if we have an accent attribute, it overrides what the overscript said
    if (mContent->AsElement()->GetAttr(nsGkAtoms::accent, value)) {
      if (value.LowerCaseEqualsLiteral("true")) {
        mEmbellishData.flags += MathMLEmbellishFlag::AccentOver;
      } else if (value.LowerCaseEqualsLiteral("false")) {
        mEmbellishData.flags -= MathMLEmbellishFlag::AccentOver;
      }
    } else if (mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentOver)) {
      AutoTArray<nsString, 1> params;
      params.AppendElement(mContent->NodeInfo()->NodeName());
      PresContext()->Document()->WarnOnceAbout(
          dom::DeprecatedOperations::eMathML_DeprecatedMoverNonExplicitAccent,
          false, params);
    }
  }

  bool subsupDisplay =
      mEmbellishData.flags.contains(MathMLEmbellishFlag::MovableLimits) &&
      StyleFont()->mMathStyle == StyleMathStyle::Compact;

  // disable the stretch-all flag if we are going to act like a superscript
  if (subsupDisplay) {
    mPresentationData.flags -=
        MathMLPresentationFlag::StretchAllChildrenHorizontally;
  }

  // Now transmit any change that we want to our children so that they
  // can update their mPresentationData structs
  //---------------------------------------------------------------------

  /* The REC says:
     Within underscript, <munderover> always sets displaystyle to "false",
     but increments scriptlevel by 1 only when accentunder is "false".

     Within overscript, <munderover> always sets displaystyle to "false",
     but increments scriptlevel by 1 only when accent is "false".

     Within subscript and superscript it increments scriptlevel by 1, and
     sets displaystyle to "false", but leaves both attributes unchanged within
     base.
  */
  if (mContent->IsAnyOfMathMLElements(nsGkAtoms::mover,
                                      nsGkAtoms::munderover)) {
    mIncrementOver =
        !mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentOver) ||
        subsupDisplay;
    SetIncrementScriptLevel(mContent->IsMathMLElement(nsGkAtoms::mover) ? 1 : 2,
                            mIncrementOver);
    if (mIncrementOver) {
      PropagateFrameFlagFor(overscriptFrame, NS_FRAME_MATHML_SCRIPT_DESCENDANT);
    }
  }
  if (mContent->IsAnyOfMathMLElements(nsGkAtoms::munder,
                                      nsGkAtoms::munderover)) {
    mIncrementUnder =
        !mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentUnder) ||
        subsupDisplay;
    SetIncrementScriptLevel(1, mIncrementUnder);
    if (mIncrementUnder) {
      PropagateFrameFlagFor(underscriptFrame,
                            NS_FRAME_MATHML_SCRIPT_DESCENDANT);
    }
  }

  /* Set flags for dtls font feature settings.

     dtls
     Dotless Forms
     This feature provides dotless forms for Math Alphanumeric
     characters, such as U+1D422 MATHEMATICAL BOLD SMALL I,
     U+1D423 MATHEMATICAL BOLD SMALL J, U+1D456
     U+MATHEMATICAL ITALIC SMALL I, U+1D457 MATHEMATICAL ITALIC
     SMALL J, and so on.
     The dotless forms are to be used as base forms for placing
     mathematical accents over them.

     To opt out of this change, add the following to the stylesheet:
     "font-feature-settings: 'dtls' 0"
   */
  if (overscriptFrame &&
      mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentOver) &&
      !mEmbellishData.flags.contains(MathMLEmbellishFlag::MovableLimits)) {
    PropagatePresentationDataFor(baseFrame, MathMLPresentationFlag::Dtls,
                                 MathMLPresentationFlag::Dtls);
  }

  return NS_OK;
}

/*
The REC says:
*  If the base is an operator with movablelimits="true" (or an embellished
   operator whose <mo> element core has movablelimits="true"), and
   displaystyle="false", then underscript and overscript are drawn in
   a subscript and superscript position, respectively. In this case,
   the accent and accentunder attributes are ignored. This is often
   used for limits on symbols such as &sum;.

i.e.,:
 if (mEmbellishDataflags.contains(MathMLEmbellishFlag::MovableLimits) &&
     StyleFont()->mMathStyle == StyleMathStyle::Compact) {
  // place like subscript-superscript pair
 }
 else {
  // place like underscript-overscript pair
 }
*/

/* virtual */
void nsMathMLmunderoverFrame::Place(DrawTarget* aDrawTarget,
                                    const PlaceFlags& aFlags,
                                    ReflowOutput& aDesiredSize) {
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);
  if (mEmbellishData.flags.contains(MathMLEmbellishFlag::MovableLimits) &&
      StyleFont()->mMathStyle == StyleMathStyle::Compact) {
    // place like sub sup or subsup
    if (mContent->IsMathMLElement(nsGkAtoms::munderover)) {
      return nsMathMLmmultiscriptsFrame::PlaceMultiScript(
          PresContext(), aDrawTarget, aFlags, aDesiredSize, this, 0, 0,
          fontSizeInflation);
    } else if (mContent->IsMathMLElement(nsGkAtoms::munder)) {
      return nsMathMLmmultiscriptsFrame::PlaceMultiScript(
          PresContext(), aDrawTarget, aFlags, aDesiredSize, this, 0, 0,
          fontSizeInflation);
    } else {
      NS_ASSERTION(mContent->IsMathMLElement(nsGkAtoms::mover),
                   "mContent->NodeInfo()->NameAtom() not recognized");
      return nsMathMLmmultiscriptsFrame::PlaceMultiScript(
          PresContext(), aDrawTarget, aFlags, aDesiredSize, this, 0, 0,
          fontSizeInflation);
    }
  }

  ////////////////////////////////////
  // Get the children's desired sizes

  nsBoundingMetrics bmBase, bmUnder, bmOver;
  ReflowOutput baseSize(aDesiredSize.GetWritingMode());
  ReflowOutput underSize(aDesiredSize.GetWritingMode());
  ReflowOutput overSize(aDesiredSize.GetWritingMode());
  nsIFrame* overFrame = nullptr;
  nsIFrame* underFrame = nullptr;
  nsIFrame* baseFrame = mFrames.FirstChild();
  underSize.SetBlockStartAscent(0);
  overSize.SetBlockStartAscent(0);
  bool haveError = false;
  if (baseFrame) {
    if (mContent->IsAnyOfMathMLElements(nsGkAtoms::munder,
                                        nsGkAtoms::munderover)) {
      underFrame = baseFrame->GetNextSibling();
    } else if (mContent->IsMathMLElement(nsGkAtoms::mover)) {
      overFrame = baseFrame->GetNextSibling();
    }
  }
  if (underFrame && mContent->IsMathMLElement(nsGkAtoms::munderover)) {
    overFrame = underFrame->GetNextSibling();
  }

  if (mContent->IsMathMLElement(nsGkAtoms::munder)) {
    if (!baseFrame || !underFrame || underFrame->GetNextSibling()) {
      // report an error, encourage people to get their markups in order
      haveError = true;
    }
  }
  if (mContent->IsMathMLElement(nsGkAtoms::mover)) {
    if (!baseFrame || !overFrame || overFrame->GetNextSibling()) {
      // report an error, encourage people to get their markups in order
      haveError = true;
    }
  }
  if (mContent->IsMathMLElement(nsGkAtoms::munderover)) {
    if (!baseFrame || !underFrame || !overFrame ||
        overFrame->GetNextSibling()) {
      // report an error, encourage people to get their markups in order
      haveError = true;
    }
  }
  if (haveError) {
    if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
      ReportChildCountError();
    }
    return PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
  }
  GetReflowAndBoundingMetricsFor(baseFrame, baseSize, bmBase);
  nsMargin baseMargin = GetMarginForPlace(aFlags, baseFrame);
  nsMargin underMargin, overMargin;
  if (underFrame) {
    GetReflowAndBoundingMetricsFor(underFrame, underSize, bmUnder);
    underMargin = GetMarginForPlace(aFlags, underFrame);
  }
  if (overFrame) {
    GetReflowAndBoundingMetricsFor(overFrame, overSize, bmOver);
    overMargin = GetMarginForPlace(aFlags, overFrame);
  }

  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);

  ////////////////////
  // Place Children

  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);

  nscoord xHeight = fm->XHeight();
  nscoord oneDevPixel = fm->AppUnitsPerDevPixel();
  RefPtr<gfxFont> mathFont = fm->GetThebesFontGroup()->GetFirstMathFont();

  nscoord ruleThickness;
  GetRuleThickness(aDrawTarget, fm, ruleThickness);

  nscoord correction = 0;
  GetItalicCorrection(bmBase, correction);

  // there are 2 different types of placement depending on
  // whether we want an accented under or not

  nscoord underDelta1 = 0;  // gap between base and underscript
  nscoord underDelta2 = 0;  // extra space beneath underscript

  if (!mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentUnder)) {
    // Rule 13a, App. G, TeXbook
    nscoord bigOpSpacing2, bigOpSpacing4, bigOpSpacing5, dummy;
    GetBigOpSpacings(fm, dummy, bigOpSpacing2, dummy, bigOpSpacing4,
                     bigOpSpacing5);
    if (mathFont) {
      // XXXfredw The Open Type MATH table has some StretchStack* parameters
      // that we may use when the base is a stretchy horizontal operator. See
      // bug 963131.
      bigOpSpacing2 = mathFont->MathTable()->Constant(
          gfxMathTable::LowerLimitGapMin, oneDevPixel);
      bigOpSpacing4 = mathFont->MathTable()->Constant(
          gfxMathTable::LowerLimitBaselineDropMin, oneDevPixel);
      bigOpSpacing5 = 0;
    }
    underDelta1 = std::max(
        bigOpSpacing2, (bigOpSpacing4 - bmUnder.ascent - underMargin.bottom));
    underDelta2 = bigOpSpacing5;
  } else {
    // No corresponding rule in TeXbook - we are on our own here
    // XXX tune the gap delta between base and underscript
    // XXX Should we use Rule 10 like \underline does?
    // XXXfredw Perhaps use the Underbar* parameters of the MATH table. See
    // bug 963125.
    underDelta1 = ruleThickness + onePixel / 2;
    underDelta2 = ruleThickness;
  }
  // empty under?
  if (bmUnder.ascent + bmUnder.descent + underMargin.TopBottom() <= 0) {
    underDelta1 = 0;
    underDelta2 = 0;
  }

  nscoord overDelta1 = 0;  // gap between base and overscript
  nscoord overDelta2 = 0;  // extra space above overscript

  if (!mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentOver)) {
    // Rule 13a, App. G, TeXbook
    // XXXfredw The Open Type MATH table has some StretchStack* parameters
    // that we may use when the base is a stretchy horizontal operator. See
    // bug 963131.
    nscoord bigOpSpacing1, bigOpSpacing3, bigOpSpacing5, dummy;
    GetBigOpSpacings(fm, bigOpSpacing1, dummy, bigOpSpacing3, dummy,
                     bigOpSpacing5);
    if (mathFont) {
      // XXXfredw The Open Type MATH table has some StretchStack* parameters
      // that we may use when the base is a stretchy horizontal operator. See
      // bug 963131.
      bigOpSpacing1 = mathFont->MathTable()->Constant(
          gfxMathTable::UpperLimitGapMin, oneDevPixel);
      bigOpSpacing3 = mathFont->MathTable()->Constant(
          gfxMathTable::UpperLimitBaselineRiseMin, oneDevPixel);
      bigOpSpacing5 = 0;
    }
    overDelta1 = std::max(bigOpSpacing1,
                          (bigOpSpacing3 - bmOver.descent - overMargin.bottom));
    overDelta2 = bigOpSpacing5;

    // XXX This is not a TeX rule...
    // delta1 (as computed abvove) can become really big when bmOver.descent is
    // negative,  e.g., if the content is &OverBar. In such case, we use the
    // height
    if (bmOver.descent + overMargin.bottom < 0) {
      overDelta1 = std::max(bigOpSpacing1,
                            (bigOpSpacing3 - (bmOver.ascent + bmOver.descent +
                                              overMargin.TopBottom())));
    }
  } else {
    // Rule 12, App. G, TeXbook
    // We are going to modify this rule to make it more general.
    // The idea behind Rule 12 in the TeXBook is to keep the accent
    // as close to the base as possible, while ensuring that the
    // distance between the *baseline* of the accent char and
    // the *baseline* of the base is atleast x-height.
    // The idea is that for normal use, we would like all the accents
    // on a line to line up atleast x-height above the baseline
    // if possible.
    // When the ascent of the base is >= x-height,
    // the baseline of the accent char is placed just above the base
    // (specifically, the baseline of the accent char is placed
    // above the baseline of the base by the ascent of the base).
    // For ease of implementation,
    // this assumes that the font-designer designs accents
    // in such a way that the bottom of the accent is atleast x-height
    // above its baseline, otherwise there will be collisions
    // with the base. Also there should be proper padding between
    // the bottom of the accent char and its baseline.
    // The above rule may not be obvious from a first
    // reading of rule 12 in the TeXBook !!!
    // The mathml <mover> tag can use accent chars that
    // do not follow this convention. So we modify TeX's rule
    // so that TeX's rule gets subsumed for accents that follow
    // TeX's convention,
    // while also allowing accents that do not follow the convention :
    // we try to keep the *bottom* of the accent char atleast x-height
    // from the baseline of the base char. we also slap on an extra
    // padding between the accent and base chars.
    overDelta1 = ruleThickness + onePixel / 2;
    nscoord accentBaseHeight = xHeight;
    if (mathFont) {
      accentBaseHeight = mathFont->MathTable()->Constant(
          gfxMathTable::AccentBaseHeight, oneDevPixel);
    }
    if (bmBase.ascent + baseMargin.top < accentBaseHeight) {
      // also ensure at least accentBaseHeight above the baseline of the base
      overDelta1 += accentBaseHeight - bmBase.ascent - baseMargin.top;
    }
    overDelta2 = ruleThickness;
  }
  // empty over?
  if (bmOver.ascent + bmOver.descent + overMargin.TopBottom() <= 0) {
    overDelta1 = 0;
    overDelta2 = 0;
  }

  nscoord dxBase = 0, dxOver = 0, dxUnder = 0;
  nsAutoString valueAlign;

  //////////
  // pass 1, do what <mover> does: attach the overscript on the base

  // Ad-hoc - This is to override fonts which have ready-made _accent_
  // glyphs with negative lbearing and rbearing. We want to position
  // the overscript ourselves
  nscoord overWidth = bmOver.width + overMargin.LeftRight();
  if (overWidth <= 0 && (bmOver.rightBearing - bmOver.leftBearing > 0)) {
    overWidth = bmOver.rightBearing - bmOver.leftBearing;
    dxOver = -bmOver.leftBearing;
  }

  if (mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentOver)) {
    mBoundingMetrics.width = bmBase.width + baseMargin.LeftRight();
    dxOver += correction;
  } else {
    mBoundingMetrics.width =
        std::max(bmBase.width + baseMargin.LeftRight(), overWidth);
    dxOver += correction / 2;
  }

  dxOver += (mBoundingMetrics.width - overWidth) / 2;
  dxBase = (mBoundingMetrics.width - bmBase.width - baseMargin.LeftRight()) / 2;

  mBoundingMetrics.ascent = baseMargin.top + bmBase.ascent + overDelta1 +
                            bmOver.ascent + bmOver.descent +
                            overMargin.TopBottom();
  mBoundingMetrics.descent = bmBase.descent + baseMargin.bottom;
  mBoundingMetrics.leftBearing =
      std::min(dxBase + bmBase.leftBearing, dxOver + bmOver.leftBearing);
  mBoundingMetrics.rightBearing =
      std::max(dxBase + bmBase.rightBearing + baseMargin.LeftRight(),
               dxOver + bmOver.rightBearing + overMargin.LeftRight());

  //////////
  // pass 2, do what <munder> does: attach the underscript on the previous
  // result. We conceptually view the previous result as an "anynomous base"
  // from where to attach the underscript. Hence if the underscript is empty,
  // we should end up like <mover>. If the overscript is empty, we should
  // end up like <munder>.

  nsBoundingMetrics bmAnonymousBase = mBoundingMetrics;
  nscoord ascentAnonymousBase = std::max(
      mBoundingMetrics.ascent + overDelta2,
      overMargin.TopBottom() + overSize.BlockStartAscent() + bmOver.descent +
          overDelta1 + baseMargin.top + bmBase.ascent);
  ascentAnonymousBase = std::max(ascentAnonymousBase,
                                 baseSize.BlockStartAscent() + baseMargin.top);

  // Width of non-spacing marks is zero so use left and right bearing.
  nscoord underWidth = bmUnder.width + underMargin.LeftRight();
  if (underWidth <= 0) {
    underWidth =
        bmUnder.rightBearing + underMargin.LeftRight() - bmUnder.leftBearing;
    dxUnder = -bmUnder.leftBearing;
  }

  nscoord maxWidth = std::max(bmAnonymousBase.width, underWidth);
  if (!mEmbellishData.flags.contains(MathMLEmbellishFlag::AccentUnder)) {
    GetItalicCorrection(bmAnonymousBase, correction);
    dxUnder += -correction / 2;
  }
  nscoord dxAnonymousBase = 0;
  dxUnder += (maxWidth - underWidth) / 2;
  dxAnonymousBase = (maxWidth - bmAnonymousBase.width) / 2;

  // adjust the offsets of the real base and overscript since their
  // final offsets should be relative to us...
  dxOver += dxAnonymousBase;
  dxBase += dxAnonymousBase;

  mBoundingMetrics.width =
      std::max(dxAnonymousBase + bmAnonymousBase.width,
               dxUnder + bmUnder.width + underMargin.LeftRight());
  // At this point, mBoundingMetrics.ascent = bmAnonymousBase.ascent
  mBoundingMetrics.descent = bmAnonymousBase.descent + underDelta1 +
                             bmUnder.ascent + bmUnder.descent +
                             underMargin.TopBottom();
  mBoundingMetrics.leftBearing =
      std::min(dxAnonymousBase + bmAnonymousBase.leftBearing,
               dxUnder + bmUnder.leftBearing);
  mBoundingMetrics.rightBearing =
      std::max(dxAnonymousBase + bmAnonymousBase.rightBearing,
               dxUnder + bmUnder.rightBearing + underMargin.LeftRight());

  aDesiredSize.SetBlockStartAscent(ascentAnonymousBase);
  aDesiredSize.Height() =
      aDesiredSize.BlockStartAscent() +
      std::max(mBoundingMetrics.descent + underDelta2,
               bmAnonymousBase.descent + underDelta1 + underMargin.top +
                   bmUnder.ascent + underSize.Height() -
                   underSize.BlockStartAscent() + underMargin.bottom);
  aDesiredSize.Height() =
      std::max(aDesiredSize.Height(),
               aDesiredSize.BlockStartAscent() + baseSize.Height() -
                   baseSize.BlockStartAscent() + baseMargin.bottom);
  aDesiredSize.Width() = mBoundingMetrics.width;
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  auto shiftX = ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                                 mBoundingMetrics);
  dxOver += shiftX;
  dxBase += shiftX;
  dxUnder += shiftX;

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);
  dxOver += borderPadding.left + overMargin.left;
  dxBase += borderPadding.left + baseMargin.left;
  dxUnder += borderPadding.left + underMargin.left;

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    nscoord dy;
    // place overscript
    if (overFrame) {
      dy = aDesiredSize.BlockStartAscent() - mBoundingMetrics.ascent +
           overMargin.top + bmOver.ascent - overSize.BlockStartAscent();
      FinishReflowChild(overFrame, PresContext(), overSize, nullptr, dxOver, dy,
                        ReflowChildFlags::Default);
    }
    // place base
    dy = aDesiredSize.BlockStartAscent() - baseSize.BlockStartAscent();
    FinishReflowChild(baseFrame, PresContext(), baseSize, nullptr, dxBase, dy,
                      ReflowChildFlags::Default);
    // place underscript
    if (underFrame) {
      dy = aDesiredSize.BlockStartAscent() + mBoundingMetrics.descent -
           bmUnder.descent - underMargin.bottom - underSize.BlockStartAscent();
      FinishReflowChild(underFrame, PresContext(), underSize, nullptr, dxUnder,
                        dy, ReflowChildFlags::Default);
    }
  }
}

bool nsMathMLmunderoverFrame::IsMathContentBoxHorizontallyCentered() const {
  bool subsupDisplay =
      mEmbellishData.flags.contains(MathMLEmbellishFlag::MovableLimits) &&
      StyleFont()->mMathStyle == StyleMathStyle::Compact;
  return !subsupDisplay;
}
