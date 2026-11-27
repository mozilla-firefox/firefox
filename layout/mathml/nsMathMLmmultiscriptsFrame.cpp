/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmmultiscriptsFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "gfxMathTable.h"
#include "gfxTextRun.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;

//
// <mmultiscripts> -- attach prescripts and tensor indices to a base -
// implementation <msub> -- attach a subscript to a base - implementation
// <msubsup> -- attach a subscript-superscript pair to a base - implementation
// <msup> -- attach a superscript to a base - implementation
//

nsIFrame* NS_NewMathMLmmultiscriptsFrame(PresShell* aPresShell,
                                         ComputedStyle* aStyle) {
  return new nsMathMLmmultiscriptsFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmmultiscriptsFrame)

nsMathMLmmultiscriptsFrame::~nsMathMLmmultiscriptsFrame() = default;

uint8_t nsMathMLmmultiscriptsFrame::ScriptIncrement(nsIFrame* aFrame) {
  if (!aFrame) {
    return 0;
  }
  if (mFrames.ContainsFrame(aFrame)) {
    if (mFrames.FirstChild() == aFrame ||
        aFrame->GetContent()->IsMathMLElement(nsGkAtoms::mprescripts)) {
      return 0;  // No script increment for base frames or prescript markers
    }
    return 1;
  }
  return 0;  // not a child
}

NS_IMETHODIMP
nsMathMLmmultiscriptsFrame::TransmitAutomaticData() {
  // if our base is an embellished operator, let its state bubble to us
  mPresentationData.baseFrame = mFrames.FirstChild();
  GetEmbellishDataFrom(mPresentationData.baseFrame, mEmbellishData);

  int32_t count = 0;
  bool isSubScript = !mContent->IsMathMLElement(nsGkAtoms::msup);

  nsIFrame* childFrame = mFrames.FirstChild();
  while (childFrame) {
    if (childFrame->GetContent()->IsMathMLElement(nsGkAtoms::mprescripts)) {
      // mprescripts frame
    } else if (0 == count) {
      // base frame
    } else {
      // super/subscript block
      PropagateFrameFlagFor(childFrame, NS_FRAME_MATHML_SCRIPT_DESCENDANT);
      isSubScript = !isSubScript;
    }
    count++;
    childFrame = childFrame->GetNextSibling();
  }

  return NS_OK;
}

/* virtual */
void nsMathMLmmultiscriptsFrame::Place(DrawTarget* aDrawTarget,
                                       const PlaceFlags& aFlags,
                                       ReflowOutput& aDesiredSize) {
  nscoord subScriptShift = 0;
  nscoord supScriptShift = 0;
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);

  return PlaceMultiScript(PresContext(), aDrawTarget, aFlags, aDesiredSize,
                          this, subScriptShift, supScriptShift,
                          fontSizeInflation);
}

// exported routine that both munderover and mmultiscripts share.
// munderover uses this when movablelimits is set.
void nsMathMLmmultiscriptsFrame::PlaceMultiScript(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget,
    const PlaceFlags& aFlags, ReflowOutput& aDesiredSize,
    nsMathMLContainerFrame* aFrame, nscoord aUserSubScriptShift,
    nscoord aUserSupScriptShift, float aFontSizeInflation) {
  nsAtom* tag = aFrame->GetContent()->NodeInfo()->NameAtom();

  // This function deals with both munderover etc. as well as msubsup etc.
  // As the former behaves identically to the later, we treat it as such
  // to avoid additional checks later.
  if (aFrame->GetContent()->IsMathMLElement(nsGkAtoms::mover)) {
    tag = nsGkAtoms::msup;
  } else if (aFrame->GetContent()->IsMathMLElement(nsGkAtoms::munder)) {
    tag = nsGkAtoms::msub;
  } else if (aFrame->GetContent()->IsMathMLElement(nsGkAtoms::munderover)) {
    tag = nsGkAtoms::msubsup;
  }

  nsBoundingMetrics bmFrame;

  nscoord minShiftFromXHeight, subDrop, supDrop;

  ////////////////////////////////////////
  // Initialize super/sub shifts that
  // depend only on the current font
  ////////////////////////////////////////

  nsIFrame* baseFrame = aFrame->PrincipalChildList().FirstChild();

  if (!baseFrame) {
    if (tag == nsGkAtoms::mmultiscripts) {
      aFrame->ReportErrorToConsole("NoBase");
    } else {
      aFrame->ReportChildCountError();
    }
    return aFrame->PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
  }

  // get x-height (an ex)
  const nsStyleFont* font = aFrame->StyleFont();
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(baseFrame, aFontSizeInflation);

  nscoord xHeight = fm->XHeight();

  nscoord oneDevPixel = fm->AppUnitsPerDevPixel();
  RefPtr<gfxFont> mathFont = fm->GetThebesFontGroup()->GetFirstMathFont();
  // scriptspace from TeX for extra spacing after sup/subscript
  nscoord scriptSpace;
  if (mathFont) {
    scriptSpace = mathFont->MathTable()->Constant(
        gfxMathTable::SpaceAfterScript, oneDevPixel);
  } else {
    // (0.5pt in plain TeX)
    scriptSpace = nsPresContext::CSSPointsToAppUnits(0.5f);
  }

  // Try and read sub and sup drops from the MATH table.
  if (mathFont) {
    subDrop = mathFont->MathTable()->Constant(
        gfxMathTable::SubscriptBaselineDropMin, oneDevPixel);
    supDrop = mathFont->MathTable()->Constant(
        gfxMathTable::SuperscriptBaselineDropMax, oneDevPixel);
  }

  // force the scriptSpace to be at least 1 pixel
  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);
  scriptSpace = std::max(onePixel, scriptSpace);

  /////////////////////////////////////
  // first the shift for the subscript

  nscoord subScriptShift;
  if (mathFont) {
    // Try and get the sub script shift from the MATH table. Note that contrary
    // to TeX we only have one parameter.
    subScriptShift = mathFont->MathTable()->Constant(
        gfxMathTable::SubscriptShiftDown, oneDevPixel);
  } else {
    // subScriptShift{1,2}
    // = minimum amount to shift the subscript down
    // = sub{1,2} in TeXbook
    // subScriptShift1 = subscriptshift attribute * x-height
    nscoord subScriptShift1, subScriptShift2;
    // Get subScriptShift{1,2} default from font
    GetSubScriptShifts(fm, subScriptShift1, subScriptShift2);
    if (tag == nsGkAtoms::msub) {
      subScriptShift = subScriptShift1;
    } else {
      subScriptShift = std::max(subScriptShift1, subScriptShift2);
    }
  }

  if (0 < aUserSubScriptShift) {
    // the user has set the subscriptshift attribute
    subScriptShift = std::max(subScriptShift, aUserSubScriptShift);
  }

  /////////////////////////////////////
  // next the shift for the superscript

  nscoord supScriptShift;
  nsPresentationData presentationData;
  aFrame->GetPresentationData(presentationData);
  bool compressed = font->mMathShift == StyleMathShift::Compact;
  if (mathFont) {
    // Try and get the super script shift from the MATH table. Note that
    // contrary to TeX we only have two parameters.
    supScriptShift = mathFont->MathTable()->Constant(
        compressed ? gfxMathTable::SuperscriptShiftUpCramped
                   : gfxMathTable::SuperscriptShiftUp,
        oneDevPixel);
  } else {
    // supScriptShift{1,2,3}
    // = minimum amount to shift the supscript up
    // = sup{1,2,3} in TeX
    // supScriptShift1 = superscriptshift attribute * x-height
    // Note that there are THREE values for supscript shifts depending
    // on the current style
    nscoord supScriptShift1, supScriptShift2, supScriptShift3;
    // Set supScriptShift{1,2,3} default from font
    GetSupScriptShifts(fm, supScriptShift1, supScriptShift2, supScriptShift3);

    // get sup script shift depending on current script level and display style
    // Rule 18c, App. G, TeXbook
    if (font->mMathDepth == 0 && font->mMathStyle == StyleMathStyle::Normal &&
        !compressed) {
      // Style D in TeXbook
      supScriptShift = supScriptShift1;
    } else if (compressed) {
      // Style C' in TeXbook = D',T',S',SS'
      supScriptShift = supScriptShift3;
    } else {
      // everything else = T,S,SS
      supScriptShift = supScriptShift2;
    }
  }

  if (0 < aUserSupScriptShift) {
    // the user has set the supscriptshift attribute
    supScriptShift = std::max(supScriptShift, aUserSupScriptShift);
  }

  ////////////////////////////////////
  // Get the children's sizes
  ////////////////////////////////////

  const WritingMode wm(aDesiredSize.GetWritingMode());
  nscoord width = 0, prescriptsWidth = 0, rightBearing = 0;
  nscoord minSubScriptShift = 0, minSupScriptShift = 0;
  nscoord trySubScriptShift = subScriptShift;
  nscoord trySupScriptShift = supScriptShift;
  nscoord maxSubScriptShift = subScriptShift;
  nscoord maxSupScriptShift = supScriptShift;
  ReflowOutput baseSize(wm);
  ReflowOutput subScriptSize(wm);
  ReflowOutput supScriptSize(wm);
  ReflowOutput multiSubSize(wm), multiSupSize(wm);
  baseFrame = nullptr;
  nsIFrame* subScriptFrame = nullptr;
  nsIFrame* supScriptFrame = nullptr;
  nsIFrame* prescriptsFrame = nullptr;  // frame of <mprescripts/>, if there.

  bool firstPrescriptsPair = false;
  nsBoundingMetrics bmBase, bmSubScript, bmSupScript, bmMultiSub, bmMultiSup;
  nsMargin baseMargin, subScriptMargin, supScriptMargin;
  multiSubSize.SetBlockStartAscent(-0x7FFFFFFF);
  multiSupSize.SetBlockStartAscent(-0x7FFFFFFF);
  bmMultiSub.ascent = bmMultiSup.ascent = -0x7FFFFFFF;
  bmMultiSub.descent = bmMultiSup.descent = -0x7FFFFFFF;
  nscoord italicCorrection = 0;
  nscoord largeOpItalicCorrection = 0;

  nsBoundingMetrics boundingMetrics;
  boundingMetrics.width = 0;
  boundingMetrics.ascent = boundingMetrics.descent = -0x7FFFFFFF;
  aDesiredSize.Width() = aDesiredSize.Height() = 0;

  int32_t count = 0;

  // Boolean to determine whether the current child is a subscript.
  // Note that only msup starts with a superscript.
  bool isSubScript = (tag != nsGkAtoms::msup);

  nsIFrame* childFrame = aFrame->PrincipalChildList().FirstChild();
  while (childFrame) {
    if (childFrame->GetContent()->IsMathMLElement(nsGkAtoms::mprescripts)) {
      if (tag != nsGkAtoms::mmultiscripts) {
        if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
          aFrame->ReportInvalidChildError(nsGkAtoms::mprescripts);
        }
        return aFrame->PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
      }
      if (prescriptsFrame) {
        // duplicate <mprescripts/> found
        // report an error, encourage people to get their markups in order
        if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
          aFrame->ReportErrorToConsole("DuplicateMprescripts");
        }
        return aFrame->PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
      }
      if (!isSubScript) {
        if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
          aFrame->ReportErrorToConsole("SubSupMismatch");
        }
        return aFrame->PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
      }

      prescriptsFrame = childFrame;
      firstPrescriptsPair = true;
    } else if (0 == count) {
      // base
      baseFrame = childFrame;
      GetReflowAndBoundingMetricsFor(baseFrame, baseSize, bmBase);
      baseMargin = GetMarginForPlace(aFlags, baseFrame);

      if (tag != nsGkAtoms::msub) {
        // Apply italics correction if there is the potential for a
        // postsupscript.
        GetItalicCorrection(bmBase, italicCorrection);
        // If italics correction is applied, we always add "a little to spare"
        // (see TeXbook Ch.11, p.64), as we estimate the italic creation
        // ourselves and it isn't the same as TeX.
        italicCorrection += onePixel;
      }

      if (tag != nsGkAtoms::msup) {
        // If the base is a largeop, determine its italic correction.
        // https://w3c.github.io/mathml-core/#base-with-subscript
        if (nsIMathMLFrame* mathMLFrame = do_QueryFrame(baseFrame)) {
          nsEmbellishData baseFrameEmbellishData;
          mathMLFrame->GetEmbellishData(baseFrameEmbellishData);
          if (baseFrameEmbellishData.flags.contains(
                  MathMLEmbellishFlag::LargeOp)) {
            largeOpItalicCorrection = mathMLFrame->ItalicCorrection();
          }
        }
      }

      // we update boundingMetrics.{ascent,descent} with that
      // of the baseFrame only after processing all the sup/sub pairs
      boundingMetrics.width = bmBase.width + baseMargin.LeftRight();
      boundingMetrics.rightBearing =
          bmBase.rightBearing + baseMargin.LeftRight();
      boundingMetrics.leftBearing = bmBase.leftBearing;  // until overwritten
    } else {
      // super/subscript block
      if (isSubScript) {
        // subscript
        subScriptFrame = childFrame;
        GetReflowAndBoundingMetricsFor(subScriptFrame, subScriptSize,
                                       bmSubScript);
        subScriptMargin = GetMarginForPlace(aFlags, subScriptFrame);

        if (!mathFont) {
          // get the subdrop from the subscript font
          GetSubDropFromChild(subScriptFrame, subDrop, aFontSizeInflation);
        }

        // parameter v, Rule 18a, App. G, TeXbook
        minSubScriptShift = bmBase.descent + baseMargin.bottom + subDrop;
        trySubScriptShift = std::max(minSubScriptShift, subScriptShift);
        multiSubSize.SetBlockStartAscent(
            std::max(multiSubSize.BlockStartAscent(),
                     subScriptSize.BlockStartAscent() + subScriptMargin.top));
        bmMultiSub.ascent = std::max(bmMultiSub.ascent,
                                     bmSubScript.ascent + subScriptMargin.top);
        bmMultiSub.descent = std::max(
            bmMultiSub.descent, bmSubScript.descent + subScriptMargin.bottom);
        multiSubSize.Height() =
            std::max(multiSubSize.Height(),
                     subScriptSize.Height() - subScriptSize.BlockStartAscent() +
                         subScriptMargin.bottom);
        if (bmSubScript.width) {
          width = bmSubScript.width + subScriptMargin.LeftRight() + scriptSpace;
        }
        rightBearing = bmSubScript.rightBearing + subScriptMargin.LeftRight();

        if (tag == nsGkAtoms::msub) {
          boundingMetrics.rightBearing = boundingMetrics.width + rightBearing;
          boundingMetrics.width += width;

          nscoord subscriptTopMax;
          if (mathFont) {
            subscriptTopMax = mathFont->MathTable()->Constant(
                gfxMathTable::SubscriptTopMax, oneDevPixel);
          } else {
            // get min subscript shift limit from x-height
            // = h(x) - 4/5 * sigma_5, Rule 18b, App. G, TeXbook
            subscriptTopMax = NSToCoordRound((4.0f / 5.0f) * xHeight);
          }
          nscoord minShiftFromXHeight =
              bmSubScript.ascent + subScriptMargin.top - subscriptTopMax;
          maxSubScriptShift = std::max(trySubScriptShift, minShiftFromXHeight);

          maxSubScriptShift = std::max(maxSubScriptShift, trySubScriptShift);
          trySubScriptShift = subScriptShift;
        }
      } else {
        // supscript
        supScriptFrame = childFrame;
        GetReflowAndBoundingMetricsFor(supScriptFrame, supScriptSize,
                                       bmSupScript);
        supScriptMargin = GetMarginForPlace(aFlags, supScriptFrame);
        if (!mathFont) {
          // get the supdrop from the supscript font
          GetSupDropFromChild(supScriptFrame, supDrop, aFontSizeInflation);
        }
        // parameter u, Rule 18a, App. G, TeXbook
        minSupScriptShift = bmBase.ascent + baseMargin.top - supDrop;
        nscoord superscriptBottomMin;
        if (mathFont) {
          superscriptBottomMin = mathFont->MathTable()->Constant(
              gfxMathTable::SuperscriptBottomMin, oneDevPixel);
        } else {
          // get min supscript shift limit from x-height
          // = d(x) + 1/4 * sigma_5, Rule 18c, App. G, TeXbook
          superscriptBottomMin = NSToCoordRound((1.0f / 4.0f) * xHeight);
        }
        minShiftFromXHeight =
            bmSupScript.descent + supScriptMargin.bottom + superscriptBottomMin;
        trySupScriptShift = std::max(
            minSupScriptShift, std::max(minShiftFromXHeight, supScriptShift));
        multiSupSize.SetBlockStartAscent(
            std::max(multiSupSize.BlockStartAscent(),
                     supScriptSize.BlockStartAscent() + supScriptMargin.top));
        bmMultiSup.ascent = std::max(bmMultiSup.ascent,
                                     bmSupScript.ascent + supScriptMargin.top);
        bmMultiSup.descent = std::max(
            bmMultiSup.descent, bmSupScript.descent + supScriptMargin.bottom);
        multiSupSize.Height() =
            std::max(multiSupSize.Height(),
                     supScriptSize.Height() - supScriptSize.BlockStartAscent() +
                         supScriptMargin.bottom);

        if (bmSupScript.width) {
          width =
              std::max(width, bmSupScript.width + supScriptMargin.LeftRight() +
                                  scriptSpace);
        }

        if (!prescriptsFrame) {  // we are still looping over base & postscripts
          rightBearing = std::max(rightBearing,
                                  italicCorrection + bmSupScript.rightBearing +
                                      supScriptMargin.LeftRight());
          boundingMetrics.rightBearing = boundingMetrics.width + rightBearing;
          boundingMetrics.width += width;
        } else {
          prescriptsWidth += width;
          if (firstPrescriptsPair) {
            firstPrescriptsPair = false;
            boundingMetrics.leftBearing =
                std::min(bmSubScript.leftBearing, bmSupScript.leftBearing);
          }
        }
        width = rightBearing = 0;

        // negotiate between the various shifts so that
        // there is enough gap between the sup and subscripts
        // Rule 18e, App. G, TeXbook
        if (tag == nsGkAtoms::mmultiscripts || tag == nsGkAtoms::msubsup) {
          nscoord subSuperscriptGapMin;
          if (mathFont) {
            subSuperscriptGapMin = mathFont->MathTable()->Constant(
                gfxMathTable::SubSuperscriptGapMin, oneDevPixel);
          } else {
            nscoord ruleSize;
            GetRuleThickness(aDrawTarget, fm, ruleSize);
            subSuperscriptGapMin = 4 * ruleSize;
          }
          nscoord gap =
              (trySupScriptShift - bmSupScript.descent -
               supScriptMargin.bottom) -
              (subScriptMargin.top + bmSubScript.ascent - trySubScriptShift);
          if (gap < subSuperscriptGapMin) {
            // adjust trySubScriptShift to get a gap of subSuperscriptGapMin
            trySubScriptShift += subSuperscriptGapMin - gap;
          }

          // next we want to ensure that the bottom of the superscript
          // will be > superscriptBottomMaxWithSubscript
          nscoord superscriptBottomMaxWithSubscript;
          if (mathFont) {
            superscriptBottomMaxWithSubscript = mathFont->MathTable()->Constant(
                gfxMathTable::SuperscriptBottomMaxWithSubscript, oneDevPixel);
          } else {
            superscriptBottomMaxWithSubscript =
                NSToCoordRound((4.0f / 5.0f) * xHeight);
          }
          gap = superscriptBottomMaxWithSubscript -
                (trySupScriptShift - bmSupScript.descent -
                 supScriptMargin.bottom);
          if (gap > 0) {
            trySupScriptShift += gap;
            trySubScriptShift -= gap;
          }
        }

        maxSubScriptShift = std::max(maxSubScriptShift, trySubScriptShift);
        maxSupScriptShift = std::max(maxSupScriptShift, trySupScriptShift);

        trySubScriptShift = subScriptShift;
        trySupScriptShift = supScriptShift;
      }

      isSubScript = !isSubScript;
    }
    count++;
    childFrame = childFrame->GetNextSibling();
  }

  // NoBase error may also have been reported above
  if ((count != 2 && (tag == nsGkAtoms::msup || tag == nsGkAtoms::msub)) ||
      (count != 3 && tag == nsGkAtoms::msubsup) || !baseFrame ||
      (!isSubScript && tag == nsGkAtoms::mmultiscripts)) {
    // report an error, encourage people to get their markups in order
    if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
      if ((count != 2 && (tag == nsGkAtoms::msup || tag == nsGkAtoms::msub)) ||
          (count != 3 && tag == nsGkAtoms::msubsup)) {
        aFrame->ReportChildCountError();
      } else if (!baseFrame) {
        aFrame->ReportErrorToConsole("NoBase");
      } else {
        aFrame->ReportErrorToConsole("SubSupMismatch");
      }
    }
    return aFrame->PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
  }

  // we left out the width of prescripts, so ...
  boundingMetrics.rightBearing += prescriptsWidth;
  boundingMetrics.width += prescriptsWidth;

  // Zero out the shifts in where a frame isn't present to avoid the potential
  // for overflow.
  if (!subScriptFrame) {
    maxSubScriptShift = 0;
  }
  if (!supScriptFrame) {
    maxSupScriptShift = 0;
  }

  // we left out the base during our bounding box updates, so ...
  if (tag == nsGkAtoms::msub) {
    boundingMetrics.ascent = std::max(bmBase.ascent + baseMargin.top,
                                      bmMultiSub.ascent - maxSubScriptShift);
  } else {
    boundingMetrics.ascent = std::max(bmBase.ascent + baseMargin.top,
                                      (bmMultiSup.ascent + maxSupScriptShift));
  }
  if (tag == nsGkAtoms::msup) {
    boundingMetrics.descent = std::max(bmBase.descent + baseMargin.bottom,
                                       bmMultiSup.descent - maxSupScriptShift);
  } else {
    boundingMetrics.descent =
        std::max(bmBase.descent + baseMargin.bottom,
                 (bmMultiSub.descent + maxSubScriptShift));
  }

  // get the reflow metrics ...
  aDesiredSize.SetBlockStartAscent(
      std::max(baseSize.BlockStartAscent() + baseMargin.top,
               std::max(multiSubSize.BlockStartAscent() - maxSubScriptShift,
                        multiSupSize.BlockStartAscent() + maxSupScriptShift)));
  aDesiredSize.Height() =
      aDesiredSize.BlockStartAscent() +
      std::max(
          baseSize.Height() - baseSize.BlockStartAscent() + baseMargin.bottom,
          std::max(multiSubSize.Height() + maxSubScriptShift,
                   multiSupSize.Height() - maxSupScriptShift));
  aDesiredSize.Width() = boundingMetrics.width;
  aDesiredSize.mBoundingMetrics = boundingMetrics;

  // Apply width/height to math content box.
  auto sizes = aFrame->GetWidthAndHeightForPlaceAdjustment(aFlags);
  aFrame->ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                           boundingMetrics);

  // Add padding+border.
  auto borderPadding = aFrame->GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize, boundingMetrics);

  aFrame->SetBoundingMetrics(boundingMetrics);
  aFrame->SetReference(nsPoint(0, aDesiredSize.BlockStartAscent()));

  //////////////////
  // Place Children

  // Place prescripts, followed by base, and then postscripts.
  // The list of frames is in the order: {base} {postscripts} {prescripts}
  // We go over the list in a circular manner, starting at <prescripts/>

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    const bool isRTL = aFrame->GetWritingMode().IsBidiRTL();
    nscoord dx = isRTL ? borderPadding.right : borderPadding.left;
    nscoord dy = 0;

    // With msub and msup there is only one element and
    // subscriptFrame/supScriptFrame have already been set above where
    // relevant.  In these cases we skip to the reflow part.
    if (tag == nsGkAtoms::msub || tag == nsGkAtoms::msup) {
      count = 1;
    } else {
      count = 0;
    }
    childFrame = prescriptsFrame;
    bool isPreScript = true;
    do {
      if (!childFrame) {  // end of prescripts,
        isPreScript = false;
        // place the base ...
        childFrame = baseFrame;
        dy = aDesiredSize.BlockStartAscent() - baseSize.BlockStartAscent();
        baseMargin = GetMarginForPlace(aFlags, baseFrame);
        nscoord dx_base = dx + (isRTL ? baseMargin.right : baseMargin.left);
        FinishReflowChild(baseFrame, aPresContext, baseSize, nullptr,
                          aFrame->MirrorIfRTL(aDesiredSize.Width(),
                                              baseSize.Width(), dx_base),
                          dy, ReflowChildFlags::Default);
        if (prescriptsFrame) {
          // place the <mprescripts/>
          ReflowOutput prescriptsSize(wm);
          nsBoundingMetrics unusedBm;
          GetReflowAndBoundingMetricsFor(prescriptsFrame, prescriptsSize,
                                         unusedBm);
          nsMargin prescriptsMargin =
              GetMarginForPlace(aFlags, prescriptsFrame);
          nscoord dx_prescripts =
              dx + (isRTL ? prescriptsMargin.right : prescriptsMargin.left);
          dy = aDesiredSize.BlockStartAscent() -
               prescriptsSize.BlockStartAscent();
          FinishReflowChild(
              prescriptsFrame, aPresContext, prescriptsSize, nullptr,
              aFrame->MirrorIfRTL(aDesiredSize.Width(), prescriptsSize.Width(),
                                  dx_prescripts),
              dy, ReflowChildFlags::Default);
        }
        dx += bmBase.width + baseMargin.LeftRight();
      } else if (childFrame != prescriptsFrame) {
        // process each sup/sub pair
        if (0 == count) {
          subScriptFrame = childFrame;
          count = 1;
        } else if (1 == count) {
          if (tag != nsGkAtoms::msub) {
            supScriptFrame = childFrame;
          }
          count = 0;

          // get the ascent/descent of sup/subscripts stored in their rects
          // rect.x = descent, rect.y = ascent
          if (subScriptFrame) {
            GetReflowAndBoundingMetricsFor(subScriptFrame, subScriptSize,
                                           bmSubScript);
            subScriptMargin = GetMarginForPlace(aFlags, subScriptFrame);
          }
          if (supScriptFrame) {
            GetReflowAndBoundingMetricsFor(supScriptFrame, supScriptSize,
                                           bmSupScript);
            supScriptMargin = GetMarginForPlace(aFlags, supScriptFrame);
          }

          width = std::max(subScriptSize.Width() + subScriptMargin.LeftRight(),
                           supScriptSize.Width() + supScriptMargin.LeftRight());

          if (subScriptFrame) {
            nscoord x =
                dx + (isRTL ? subScriptMargin.right : subScriptMargin.left);
            // prescripts should be right aligned
            // https://bugzilla.mozilla.org/show_bug.cgi?id=928675
            if (isPreScript) {
              x += width - subScriptSize.Width() - subScriptMargin.LeftRight();
            } else {
              // post subscripts are shifted by the largeOpItalicCorrection
              // value.
              x -= largeOpItalicCorrection;
            }
            dy = aDesiredSize.BlockStartAscent() -
                 subScriptSize.BlockStartAscent() + maxSubScriptShift;
            FinishReflowChild(subScriptFrame, aPresContext, subScriptSize,
                              nullptr,
                              aFrame->MirrorIfRTL(aDesiredSize.Width(),
                                                  subScriptSize.Width(), x),
                              dy, ReflowChildFlags::Default);
          }

          if (supScriptFrame) {
            nscoord x =
                dx + (isRTL ? supScriptMargin.right : supScriptMargin.left);
            if (isPreScript) {
              x += width - supScriptSize.Width() - supScriptMargin.LeftRight();
            } else {
              // post superscripts are shifted by the italic correction value
              x += italicCorrection;
            }
            dy = aDesiredSize.BlockStartAscent() -
                 supScriptSize.BlockStartAscent() - maxSupScriptShift;
            FinishReflowChild(supScriptFrame, aPresContext, supScriptSize,
                              nullptr,
                              aFrame->MirrorIfRTL(aDesiredSize.Width(),
                                                  supScriptSize.Width(), x),
                              dy, ReflowChildFlags::Default);
          }
          dx += width + scriptSpace;
        }
      }
      childFrame = childFrame->GetNextSibling();
    } while (prescriptsFrame != childFrame);
  }
}
