/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPageFrame.h"

#include "mozilla/AppUnits.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/intl/Segmenter.h"
#include "gfxContext.h"
#include "nsDeviceContext.h"
#include "nsFontMetrics.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsFieldSetFrame.h"
#include "nsPageContentFrame.h"
#include "nsDisplayList.h"
#include "nsPageSequenceFrame.h"  // for nsSharedPageData
#include "nsTextFormatter.h"      // for page number localization formatting
#include "nsBidiUtils.h"
#include "nsIPrintSettings.h"
#include "PrintedSheetFrame.h"

#include "mozilla/Logging.h"
extern mozilla::LazyLogModule gLayoutPrintingLog;
#define PR_PL(_p1) MOZ_LOG(gLayoutPrintingLog, mozilla::LogLevel::Debug, _p1)

using namespace mozilla;
using namespace mozilla::gfx;

nsPageFrame* NS_NewPageFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsPageFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsPageFrame)

NS_QUERYFRAME_HEAD(nsPageFrame)
  NS_QUERYFRAME_ENTRY(nsPageFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsPageFrame::nsPageFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID) {}

nsPageFrame::~nsPageFrame() = default;

nsReflowStatus nsPageFrame::ReflowPageContent(
    nsPresContext* aPresContext, const ReflowInput& aPageReflowInput) {
  nsPageContentFrame* const frame = PageContentFrame();
  // If this is the first page, it won't have its page name and computed style
  // set yet. Before reflow, make sure that page name and computed style have
  // been applied.
  frame->EnsurePageName();
  // XXX Pay attention to the page's border and padding...
  //
  // Reflow our ::-moz-page-content frame, allowing it only to be as big as we
  // are (minus margins).
  const nsSize pageSize = ComputePageSize();
  // Scaling applied to the page in the single page-per-sheet case (used for
  // down-scaling when the page is too large to fit on the sheet we are printing
  // on). In the single page-per-sheet case, we need this here to preemptively
  // increase the margins by the same amount that the scaling will reduce them
  // in order to make sure that their physical size is unchanged (particularly
  // important for the unwriteable margins).
  const auto* ppsInfo = GetSharedPageData()->PagesPerSheetInfo();
  const float pageSizeScale =
      ppsInfo->mNumPages == 1 ? ComputeSinglePPSPageSizeScale(pageSize) : 1.0f;
  // Scaling applied to content, as given by the print UI.
  // This is an additional scale factor that is applied to the content in the
  // nsPageContentFrame.
  const float extraContentScale = aPresContext->GetPageScale();
  // Size for the page content. This will be scaled by extraContentScale, and
  // is used to calculate the computed size of the nsPageContentFrame content
  // by subtracting margins.
  nsSize availableSpace = pageSize;

  // When the reflow size is NS_UNCONSTRAINEDSIZE it means we are reflowing
  // a single page to print selection. So this means we want to use
  // NS_UNCONSTRAINEDSIZE without altering it.
  //
  // FIXME(emilio): Is this still true?
  availableSpace.width =
      NSToCoordCeil(availableSpace.width / extraContentScale);
  if (availableSpace.height != NS_UNCONSTRAINEDSIZE) {
    availableSpace.height =
        NSToCoordCeil(availableSpace.height / extraContentScale);
  }

  // Get the number of Twips per pixel from the PresContext
  const nscoord onePixel = AppUnitsPerCSSPixel();

  // insurance against infinite reflow, when reflowing less than a pixel
  // XXX Shouldn't we do something more friendly when invalid margins
  //     are set?
  if (availableSpace.width < onePixel || availableSpace.height < onePixel) {
    NS_WARNING("Reflow aborted; no space for content");
    return {};
  }

  ReflowInput kidReflowInput(
      aPresContext, aPageReflowInput, frame,
      LogicalSize(frame->GetWritingMode(), availableSpace));
  kidReflowInput.mFlags.mIsTopOfPage = true;
  kidReflowInput.mFlags.mTableIsSplittable = true;

  nsMargin defaultMargins = aPresContext->GetDefaultPageMargin();
  // The default margins are in the coordinate space of the physical paper.
  // Scale them by the pageSizeScale to convert them to the content coordinate
  // space.
  for (const auto side : mozilla::AllPhysicalSides()) {
    defaultMargins.Side(side) =
        NSToCoordRound((float)defaultMargins.Side(side) / pageSizeScale);
  }
  mPageContentMargin = defaultMargins;

  // Use the margins given in the @page rule if told to do so.
  // We clamp to the paper's unwriteable margins to avoid clipping, *except*
  // that we will respect a margin of zero if specified, assuming this means
  // the document is intended to fit the paper size exactly, and the client is
  // taking full responsibility for what happens around the edges.
  const auto positionProperty = kidReflowInput.mStyleDisplay->mPosition;
  if (mPD->mPrintSettings->GetHonorPageRuleMargins()) {
    for (const auto side : mozilla::AllPhysicalSides()) {
      if (!kidReflowInput.mStyleMargin->GetMargin(side, positionProperty)
               ->IsAuto()) {
        // Computed margins are already in the coordinate space of the content,
        // do not scale.
        const nscoord computed =
            kidReflowInput.ComputedPhysicalMargin().Side(side);
        // Respecting a zero margin is particularly important when the client
        // is PDF.js where the PDF already contains the margins.
        // User could also be asking to ignore unwriteable margins (Though
        // currently, it is impossible through the print UI to set both
        // `HonorPageRuleMargins` and `IgnoreUnwriteableMargins`).
        if (computed == 0 ||
            mPD->mPrintSettings->GetIgnoreUnwriteableMargins()) {
          mPageContentMargin.Side(side) = computed;
        } else {
          // Unwriteable margins are in the coordinate space of the physical
          // paper. Scale them by the pageSizeScale to convert them to the
          // content coordinate space.
          const int32_t unwriteableTwips =
              mPD->mPrintSettings->GetUnwriteableMarginInTwips().Side(side);
          const nscoord unwriteable = nsPresContext::CSSTwipsToAppUnits(
              (float)unwriteableTwips / pageSizeScale);
          mPageContentMargin.Side(side) = std::max(
              kidReflowInput.ComputedPhysicalMargin().Side(side), unwriteable);
        }
      }
    }
  }

  // TODO: This seems odd that we need to scale the margins by the extra
  // scale factor, but this is needed for correct margins.
  // Why are the margins already scaled? Shouldn't they be stored so that this
  // scaling factor would be redundant?
  nscoord computedWidth =
      availableSpace.width - mPageContentMargin.LeftRight() / extraContentScale;
  nscoord computedHeight;
  if (availableSpace.height == NS_UNCONSTRAINEDSIZE) {
    computedHeight = NS_UNCONSTRAINEDSIZE;
  } else {
    computedHeight = availableSpace.height -
                     mPageContentMargin.TopBottom() / extraContentScale;
  }

  // Check the width and height, if they're too small we reset the margins
  // back to the default.
  if (computedWidth < onePixel || computedHeight < onePixel) {
    mPageContentMargin = defaultMargins;
    computedWidth = availableSpace.width -
                    mPageContentMargin.LeftRight() / extraContentScale;
    if (computedHeight != NS_UNCONSTRAINEDSIZE) {
      computedHeight = availableSpace.height -
                       mPageContentMargin.TopBottom() / extraContentScale;
    }
    // And if they're still too small, we give up.
    if (computedWidth < onePixel || computedHeight < onePixel) {
      NS_WARNING("Reflow aborted; no space for content");
      return {};
    }
  }

  kidReflowInput.SetComputedWidth(computedWidth);
  kidReflowInput.SetComputedHeight(computedHeight);

  // calc location of frame
  const nscoord xc = mPageContentMargin.left;
  const nscoord yc = mPageContentMargin.top;

  // Get the child's desired size
  ReflowOutput kidOutput(kidReflowInput);
  nsReflowStatus kidStatus;
  ReflowChild(frame, aPresContext, kidOutput, kidReflowInput, xc, yc,
              ReflowChildFlags::Default, kidStatus);

  // Place and size the child
  FinishReflowChild(frame, aPresContext, kidOutput, &kidReflowInput, xc, yc,
                    ReflowChildFlags::Default);

  NS_ASSERTION(!kidStatus.IsFullyComplete() || !frame->GetNextInFlow(),
               "bad child flow list");
  return kidStatus;
}

void nsPageFrame::Reflow(nsPresContext* aPresContext,
                         ReflowOutput& aReflowOutput,
                         const ReflowInput& aReflowInput,
                         nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsPageFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(mPD, "Need a pointer to nsSharedPageData before reflow starts");

  // Our status is the same as our child's.
  aStatus = ReflowPageContent(aPresContext, aReflowInput);

  PR_PL(("PageFrame::Reflow %p ", this));
  PR_PL(("[%d,%d][%d,%d]\n", aReflowOutput.Width(), aReflowOutput.Height(),
         aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  // Return our desired size
  WritingMode wm = aReflowInput.GetWritingMode();
  aReflowOutput.ISize(wm) = aReflowInput.AvailableISize();
  if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE) {
    aReflowOutput.BSize(wm) = aReflowInput.AvailableBSize();
  }

  aReflowOutput.SetOverflowAreasToDesiredBounds();
  FinishAndStoreOverflow(&aReflowOutput);

  PR_PL(("PageFrame::Reflow %p ", this));
  PR_PL(("[%d,%d]\n", aReflowInput.AvailableWidth(),
         aReflowInput.AvailableHeight()));
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsPageFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Page"_ns, aResult);
}
#endif

void nsPageFrame::ProcessSpecialCodes(const nsString& aStr, nsString& aNewStr) {
  aNewStr = aStr;

  // Search to see if the &D code is in the string
  // then subst in the current date/time
  constexpr auto kDate = u"&D"_ns;
  if (aStr.Find(kDate) != kNotFound) {
    aNewStr.ReplaceSubstring(kDate, mPD->mDateTimeStr);
  }

  // NOTE: Must search for &PT before searching for &P
  //
  // Search to see if the "page number and page" total code are in the string
  // and replace the page number and page total code with the actual
  // values
  constexpr auto kPageAndTotal = u"&PT"_ns;
  if (aStr.Find(kPageAndTotal) != kNotFound) {
    nsAutoString uStr;
    nsTextFormatter::ssprintf(uStr, mPD->mPageNumAndTotalsFormat.get(),
                              mPageNum, mPD->mRawNumPages);
    aNewStr.ReplaceSubstring(kPageAndTotal, uStr);
  }

  // Search to see if the page number code is in the string
  // and replace the page number code with the actual value
  constexpr auto kPage = u"&P"_ns;
  if (aStr.Find(kPage) != kNotFound) {
    nsAutoString uStr;
    nsTextFormatter::ssprintf(uStr, mPD->mPageNumFormat.get(), mPageNum);
    aNewStr.ReplaceSubstring(kPage, uStr);
  }

  constexpr auto kTitle = u"&T"_ns;
  if (aStr.Find(kTitle) != kNotFound) {
    aNewStr.ReplaceSubstring(kTitle, mPD->mDocTitle);
  }

  constexpr auto kDocURL = u"&U"_ns;
  if (aStr.Find(kDocURL) != kNotFound) {
    aNewStr.ReplaceSubstring(kDocURL, mPD->mDocURL);
  }

  constexpr auto kPageTotal = u"&L"_ns;
  if (aStr.Find(kPageTotal) != kNotFound) {
    nsAutoString uStr;
    nsTextFormatter::ssprintf(uStr, mPD->mPageNumFormat.get(),
                              mPD->mRawNumPages);
    aNewStr.ReplaceSubstring(kPageTotal, uStr);
  }
}

//------------------------------------------------------------------------------
nscoord nsPageFrame::GetXPosition(gfxContext& aRenderingContext,
                                  nsFontMetrics& aFontMetrics,
                                  const nsRect& aRect, int32_t aJust,
                                  const nsString& aStr) {
  nscoord width = nsLayoutUtils::AppUnitWidthOfStringBidi(
      aStr, this, aFontMetrics, aRenderingContext);
  nscoord x = aRect.x;
  switch (aJust) {
    case nsIPrintSettings::kJustLeft:
      x += mPD->mEdgePaperMargin.left;
      break;

    case nsIPrintSettings::kJustCenter:
      x += (aRect.width - width) / 2;
      break;

    case nsIPrintSettings::kJustRight:
      x += aRect.width - width - mPD->mEdgePaperMargin.right;
      break;
  }  // switch

  return x;
}

// Draw a header or footer
// @param aRenderingContext - rendering content to draw into
// @param aHeaderFooter - indicates whether it is a header or footer
// @param aStrLeft - string for the left header or footer; can be empty
// @param aStrCenter - string for the center header or footer; can be empty
// @param aStrRight - string for the right header or footer; can be empty
// @param aRect - the rect of the page
// @param aAscent - the ascent of the font
// @param aHeight - the height of the font
void nsPageFrame::DrawHeaderFooter(
    gfxContext& aRenderingContext, nsFontMetrics& aFontMetrics,
    nsHeaderFooterEnum aHeaderFooter, const nsString& aStrLeft,
    const nsString& aStrCenter, const nsString& aStrRight, const nsRect& aRect,
    nscoord aAscent, nscoord aHeight) {
  int32_t numStrs = 0;
  if (!aStrLeft.IsEmpty()) {
    numStrs++;
  }
  if (!aStrCenter.IsEmpty()) {
    numStrs++;
  }
  if (!aStrRight.IsEmpty()) {
    numStrs++;
  }

  if (numStrs == 0) {
    return;
  }
  const nscoord contentWidth =
      aRect.width - (mPD->mEdgePaperMargin.left + mPD->mEdgePaperMargin.right);
  const nscoord strSpace = contentWidth / numStrs;

  if (!aStrLeft.IsEmpty()) {
    DrawHeaderFooter(aRenderingContext, aFontMetrics, aHeaderFooter,
                     nsIPrintSettings::kJustLeft, aStrLeft, aRect, aAscent,
                     aHeight, strSpace);
  }
  if (!aStrCenter.IsEmpty()) {
    DrawHeaderFooter(aRenderingContext, aFontMetrics, aHeaderFooter,
                     nsIPrintSettings::kJustCenter, aStrCenter, aRect, aAscent,
                     aHeight, strSpace);
  }
  if (!aStrRight.IsEmpty()) {
    DrawHeaderFooter(aRenderingContext, aFontMetrics, aHeaderFooter,
                     nsIPrintSettings::kJustRight, aStrRight, aRect, aAscent,
                     aHeight, strSpace);
  }
}

// Draw a header or footer string
// @param aRenderingContext - rendering context to draw into
// @param aHeaderFooter - indicates whether it is a header or footer
// @param aJust - indicates where the string is located within the header/footer
// @param aStr - the string to be drawn
// @param aRect - the rect of the page
// @param aHeight - the height of the font
// @param aAscent - the ascent of the font
// @param aWidth - available width for the string
void nsPageFrame::DrawHeaderFooter(gfxContext& aRenderingContext,
                                   nsFontMetrics& aFontMetrics,
                                   nsHeaderFooterEnum aHeaderFooter,
                                   int32_t aJust, const nsString& aStr,
                                   const nsRect& aRect, nscoord aAscent,
                                   nscoord aHeight, nscoord aWidth) {
  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  if ((aHeaderFooter == eHeader && aHeight < mPageContentMargin.top) ||
      (aHeaderFooter == eFooter && aHeight < mPageContentMargin.bottom)) {
    nsAutoString str;
    ProcessSpecialCodes(aStr, str);

    int32_t len = (int32_t)str.Length();
    if (len == 0) {
      return;  // bail is empty string
    }

    int32_t index;
    int32_t textWidth = 0;
    const char16_t* text = str.get();
    // find how much text fits, the "position" is the size of the available area
    if (nsLayoutUtils::BinarySearchForPosition(drawTarget, aFontMetrics, text,
                                               0, 0, 0, len, int32_t(aWidth),
                                               index, textWidth)) {
      if (index < len - 1) {
        // we can't fit in all the text, try to remove 3 glyphs and append
        // three "." charactrers.

        // TODO: This might not actually remove three glyphs in cases where
        // ZWJ sequences, regional indicators, etc are used.
        // We also have guarantee that removing three glyphs will make enough
        // space for the ellipse, if they are zero-width or even just narrower
        // than the "." character.
        // See https://bugzilla.mozilla.org/1765008
        mozilla::intl::GraphemeClusterBreakReverseIteratorUtf16 revIter(str);

        // Start iteration at the point where the text does properly fit.
        revIter.Seek(index);

        // Step backwards 3 times, checking if we have any string left by the
        // end.
        revIter.Next();
        revIter.Next();
        if (const Maybe<uint32_t> maybeIndex = revIter.Next()) {
          // TODO: We should consider checking for the ellipse character, or
          // possibly for another continuation indicator based on
          // localization.
          // See https://bugzilla.mozilla.org/1765007
          str.Truncate(*maybeIndex);
          str.AppendLiteral("...");
        } else {
          // We can only fit 3 or fewer chars.  Just show nothing
          str.Truncate();
        }
      }
    } else {
      return;  // bail if couldn't find the correct length
    }

    if (HasRTLChars(str)) {
      PresContext()->SetBidiEnabled();
    }

    // calc the x and y positions of the text
    nscoord x =
        GetXPosition(aRenderingContext, aFontMetrics, aRect, aJust, str);
    nscoord y;
    if (aHeaderFooter == eHeader) {
      y = aRect.y + mPD->mEdgePaperMargin.top;
    } else {
      y = aRect.YMost() - aHeight - mPD->mEdgePaperMargin.bottom;
    }

    // set up new clip and draw the text
    aRenderingContext.Save();
    aRenderingContext.Clip(NSRectToSnappedRect(
        aRect, PresContext()->AppUnitsPerDevPixel(), *drawTarget));
    aRenderingContext.SetColor(sRGBColor::OpaqueBlack());
    nsLayoutUtils::DrawString(this, aFontMetrics, &aRenderingContext, str.get(),
                              str.Length(), nsPoint(x, y + aAscent), nullptr,
                              DrawStringFlags::ForceHorizontal);
    aRenderingContext.Restore();
  }
}

class nsDisplayHeaderFooter final : public nsPaintedDisplayItem {
 public:
  nsDisplayHeaderFooter(nsDisplayListBuilder* aBuilder, nsPageFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayHeaderFooter);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayHeaderFooter)

  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     gfxContext* aCtx) override {
#ifdef DEBUG
    nsPageFrame* pageFrame = do_QueryFrame(mFrame);
    MOZ_ASSERT(pageFrame, "We should have an nsPageFrame");
#endif
    static_cast<nsPageFrame*>(mFrame)->PaintHeaderFooter(
        *aCtx, ToReferenceFrame(), false);
  }
  NS_DISPLAY_DECL_NAME("HeaderFooter", TYPE_HEADER_FOOTER)

  virtual nsRect GetComponentAlphaBounds(
      nsDisplayListBuilder* aBuilder) const override {
    bool snap;
    return GetBounds(aBuilder, &snap);
  }
};

static void PaintMarginGuides(nsIFrame* aFrame, DrawTarget* aDrawTarget,
                              const nsRect& aDirtyRect, nsPoint aPt) {
  // Set up parameters needed to draw the guides: we draw them in blue,
  // using 2px-long dashes with 2px separation and a line width of 0.5px.
  // Drawing is antialiased, so on a non-hidpi screen where the line width is
  // less than one device pixel, it doesn't disappear but renders fainter
  // than a solid 1px-wide line would be.
  // (In many cases, the entire preview is scaled down so that the guides
  // will be nominally less than 1 dev px even on a hidpi screen, resulting
  // in lighter antialiased rendering so they don't dominate the page.)
  ColorPattern pattern(ToDeviceColor(sRGBColor(0.0f, 0.0f, 1.0f)));
  Float dashes[] = {2.0f, 2.0f};
  StrokeOptions stroke(/* line width (in CSS px) */ 0.5f,
                       JoinStyle::MITER_OR_BEVEL, CapStyle::BUTT,
                       /* mitre limit (default, not used) */ 10.0f,
                       /* set dash pattern of 2px stroke, 2px gap */
                       std::size(dashes), dashes,
                       /* dash offset */ 0.0f);
  DrawOptions options;

  MOZ_RELEASE_ASSERT(aFrame->IsPageFrame());
  const nsMargin& margin =
      static_cast<nsPageFrame*>(aFrame)->GetUsedPageContentMargin();
  int32_t appUnitsPerDevPx = aFrame->PresContext()->AppUnitsPerDevPixel();

  // Get the frame's rect and inset by the margins to get the edges of the
  // content area, where we want to draw the guides.
  // We draw in two stages, first applying the top/bottom margins and drawing
  // the horizontal guides across the full width of the page.
  nsRect rect(aPt, aFrame->GetSize());
  rect.Deflate(nsMargin(margin.top, 0, margin.bottom, 0));
  Rect r = NSRectToRect(rect, appUnitsPerDevPx);
  aDrawTarget->StrokeLine(r.TopLeft(), r.TopRight(), pattern, stroke, options);
  aDrawTarget->StrokeLine(r.BottomLeft(), r.BottomRight(), pattern, stroke,
                          options);

  // Then reset rect, apply the left/right margins, and draw vertical guides
  // extending the full height of the page.
  rect = nsRect(aPt, aFrame->GetSize());
  rect.Deflate(nsMargin(0, margin.right, 0, margin.left));
  r = NSRectToRect(rect, appUnitsPerDevPx);
  aDrawTarget->StrokeLine(r.TopLeft(), r.BottomLeft(), pattern, stroke,
                          options);
  aDrawTarget->StrokeLine(r.TopRight(), r.BottomRight(), pattern, stroke,
                          options);
}

static std::tuple<uint32_t, uint32_t> GetRowAndColFromIdx(uint32_t aIdxOnSheet,
                                                          uint32_t aNumCols) {
  // Compute the row index by *dividing* the item's ordinal position by how
  // many items fit in each row (i.e. the number of columns), and flooring.
  // Compute the column index by getting the remainder of that division:
  // Notably, mNumRows is irrelevant to this computation; that's because
  // we're adding new items column-by-column rather than row-by-row.
  return {aIdxOnSheet / aNumCols, aIdxOnSheet % aNumCols};
}

// The minimum ratio for which we will center the page on the sheet when using
// auto-detect logic.
// Note that this ratio is of the content's size to the sheet size scaled to be
// in content space, and so the actual ratio will always be from 0.0 to 1.0,
// with this marking the smallest ratio we consider a near-miss.
// The ratio of A4 on Letter is 0.915034. A threshold of 0.9 will ensure that
// A4 on Letter works, as well as other near-misses.
//
// The ratio is computed as so:
// scale = min(1, sheetHeight / pageHeight, sheetWidth / pageWidth)
//
// Where pageSize is pageWidth or pageHeight, and sheetSize is sheetWidth or
// sheetHeight, respectively:
// scaledPageSize = pageSize * scale
// ratio = scaledPageSize / sheetSize
//
// A4 (210mm x 297mm) on US Letter (215.9mm x 279.4mm) is derived as so:
// scale = min(1, 215.9 / 210, 279.4 / 297) = 0.9407407407..
//
// Using the widths:
// scaledPageSize = (210 * 0.940741) = 197.556
// ratio = 197.556 / 215.9 = 0.915034
//
// See nsPageFrame::ComputeSinglePPSPageSizeScale for scale calculation, and
// OffsetToCenterPage for ratio calculation.
constexpr float kCenterPageRatioThreshold = 0.9f;

// Numeric values for the pref "print.center_page_on_sheet"
enum {
  kPrintCenterPageOnSheetNever = 0,
  kPrintCenterPageOnSheetAlways = 1,
  kPrintCenterPageOnSheetAuto = 2
};

// Returns an offset to center the page on the sheet, with a given scale.
// When no centering can/should happen, this will avoid extra calculations and
// return 0.0f.
// This takes into account the value of the pref "print.center_page_on_sheet".
static float OffsetToCenterPage(nscoord aContentSize, nscoord aSheetSize,
                                float aScale, float aAppUnitsPerPixel) {
  MOZ_ASSERT(aScale <= 1.0f && aScale > 0.0f,
             "Scale must be in the range (0,1]");
  const unsigned centerPagePref = StaticPrefs::print_center_page_on_sheet();
  if (centerPagePref == kPrintCenterPageOnSheetNever) {
    return 0.0f;
  }

  // Determine the ratio of scaled page to the sheet size.
  const float sheetSize =
      NSAppUnitsToFloatPixels(aSheetSize, aAppUnitsPerPixel);
  const float scaledContentSize =
      NSAppUnitsToFloatPixels(aContentSize, aAppUnitsPerPixel) * aScale;
  const float ratio = scaledContentSize / sheetSize;

  // If the ratio is within the threshold, or the pref indicates we should
  // always center the page, return half the difference to form the offset.
  if (centerPagePref == kPrintCenterPageOnSheetAlways ||
      ratio >= kCenterPageRatioThreshold) {
    return (sheetSize - scaledContentSize) * 0.5f;
  }
  return 0.0f;
}

// Helper for BuildDisplayList:
static gfx::Matrix4x4 ComputePagesPerSheetAndPageSizeTransform(
    const nsIFrame* aFrame, float aAppUnitsPerPixel) {
  MOZ_ASSERT(aFrame->IsPageFrame());
  auto* pageFrame = static_cast<const nsPageFrame*>(aFrame);
  const nsSize contentPageSize = pageFrame->ComputePageSize();
  MOZ_ASSERT(contentPageSize.width > 0 && contentPageSize.height > 0);
  nsSharedPageData* pd = pageFrame->GetSharedPageData();
  const auto* ppsInfo = pd->PagesPerSheetInfo();

  const nsContainerFrame* const parentFrame = pageFrame->GetParent();
  MOZ_ASSERT(parentFrame->IsPrintedSheetFrame(),
             "Parent of nsPageFrame should be PrintedSheetFrame");
  const auto* sheetFrame = static_cast<const PrintedSheetFrame*>(parentFrame);

  const double rotation =
      pageFrame->GetPageOrientationRotation(pageFrame->GetSharedPageData());

  gfx::Matrix4x4 transform;

  if (ppsInfo->mNumPages == 1) {
    const nsSize sheetSize = sheetFrame->GetSizeForChildren();
    if (rotation != 0.0) {
      const bool sheetIsPortrait = sheetSize.width < sheetSize.height;
      const bool rotatingClockwise = rotation > 0.0;

      // rotation point:
      int32_t x, y;
      if (rotatingClockwise != sheetIsPortrait) {
        // rotating portrait clockwise, or landscape counterclockwise
        x = y = std::min(sheetSize.width, sheetSize.height) / 2;
      } else {
        // rotating portrait counterclockwise, or landscape clockwise
        x = y = std::max(sheetSize.width, sheetSize.height) / 2;
      }

      transform = gfx::Matrix4x4::Translation(
          NSAppUnitsToFloatPixels(x, aAppUnitsPerPixel),
          NSAppUnitsToFloatPixels(y, aAppUnitsPerPixel), 0);
      transform.RotateZ(rotation);
      transform.PreTranslate(NSAppUnitsToFloatPixels(-x, aAppUnitsPerPixel),
                             NSAppUnitsToFloatPixels(-y, aAppUnitsPerPixel), 0);
    }

    // If the difference in horizontal size, after scaling, is relatively small
    // then center the page on the sheet.
    const float scale =
        pageFrame->ComputeSinglePPSPageSizeScale(contentPageSize);
    const float centeringOffset = OffsetToCenterPage(
        contentPageSize.width, sheetSize.width, scale, aAppUnitsPerPixel);

    // Only bother with the translation if it is at least one pixel.
    // It's possible for a mismatch in the paper size reported by the print
    // server and the paper size from Gecko to lead to small offsets, or
    // even (in combination with floating point error) a very small negative
    // offset. Do not apply an offset in those cases.
    if (centeringOffset >= 1.0f) {
      transform.PreTranslate(centeringOffset, 0, 0);
    }
    transform.PreScale(scale, scale, 1);
    return transform;
  }

  // The multiple pages-per-sheet case.

  // Begin with the translation of the page to its pages-per-sheet grid "cell"
  // (the grid origin accounts for the sheet's unwriteable margins):
  const nsPoint gridOrigin = sheetFrame->GetGridOrigin();
  const nscoord cellWidth = sheetFrame->GetGridCellWidth();
  const nscoord cellHeight = sheetFrame->GetGridCellHeight();
  uint32_t rowIdx, colIdx;
  std::tie(rowIdx, colIdx) = GetRowAndColFromIdx(pageFrame->IndexOnSheet(),
                                                 sheetFrame->GetGridNumCols());
  transform = gfx::Matrix4x4::Translation(
      NSAppUnitsToFloatPixels(gridOrigin.x + nscoord(colIdx) * cellWidth,
                              aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(gridOrigin.y + nscoord(rowIdx) * cellHeight,
                              aAppUnitsPerPixel),
      0.0f);

  // Scale the page to fit, centered, in the grid cell:
  float scaleX = float(cellWidth) / float(contentPageSize.width);
  float scaleY = float(cellHeight) / float(contentPageSize.height);
  MOZ_ASSERT(scaleX > 0.0f && scaleX <= 1.0f && scaleY > 0.0f &&
             scaleY <= 1.0f);
  float scale;
  float dx = 0.0f, dy = 0.0f;
  if (scaleX < scaleY) {
    scale = scaleX;
    // We need to scale down more for the width than the height, so we'll have
    // some spare space in the page's vertical direction. We offset the page
    // to share that space equally above and below the page to center it.
    nscoord extraSpace =
        cellHeight - NSToCoordRound(float(contentPageSize.height) * scale);
    dy = NSAppUnitsToFloatPixels(extraSpace / 2, aAppUnitsPerPixel);
  } else {
    scale = scaleY;
    nscoord extraSpace =
        cellWidth - NSToCoordRound(float(contentPageSize.width) * scale);
    dx = NSAppUnitsToFloatPixels(extraSpace / 2, aAppUnitsPerPixel);
  }
  transform.PreTranslate(dx, dy, 0.0f);
  transform.PreScale(scale, scale, 1.0f);

  // Apply 'page-orientation' rotation, if applicable:
  if (rotation != 0.0) {
    // We've already translated and scaled the page to fit the cell, ignoring
    // rotation. Here we rotate the page around its center and, if necessary,
    // also scale it to fit it to its cell for its orientation change.

    float fitScale = 1.0f;
    if (MOZ_LIKELY(cellWidth != cellHeight &&
                   contentPageSize.width != contentPageSize.height)) {
      // If neither the cell nor the page are square, the scale must change.
      float cellRatio = float(cellWidth) / float(cellHeight);
      float pageRatio =
          float(contentPageSize.width) / float(contentPageSize.height);
      const bool orientationWillMatchAfterRotation =
          floor(cellRatio) != floor(pageRatio);
      if (cellRatio > 1.0f) {
        cellRatio = 1.0f / cellRatio;  // normalize
      }
      if (pageRatio > 1.0f) {
        pageRatio = 1.0f / pageRatio;  // normalize
      }
      fitScale = std::max(cellRatio, pageRatio);
      if (orientationWillMatchAfterRotation) {
        // Scale up, not down
        fitScale = 1.0f / fitScale;
      }
    }

    transform.PreTranslate(
        NSAppUnitsToFloatPixels(contentPageSize.width / 2, aAppUnitsPerPixel),
        NSAppUnitsToFloatPixels(contentPageSize.height / 2, aAppUnitsPerPixel),
        0);
    if (MOZ_LIKELY(fitScale != 1.0f)) {
      transform.PreScale(fitScale, fitScale, 1.0f);
    }
    transform.RotateZ(rotation);
    transform.PreTranslate(
        NSAppUnitsToFloatPixels(-contentPageSize.width / 2, aAppUnitsPerPixel),
        NSAppUnitsToFloatPixels(-contentPageSize.height / 2, aAppUnitsPerPixel),
        0);
  }

  return transform;
}

nsIFrame::ComputeTransformFunction nsPageFrame::GetTransformGetter() const {
  return ComputePagesPerSheetAndPageSizeTransform;
}

nsPageContentFrame* nsPageFrame::PageContentFrame() const {
  nsIFrame* const frame = mFrames.FirstChild();
  MOZ_ASSERT(frame, "pageFrame must have one child");
  MOZ_ASSERT(frame->IsPageContentFrame(),
             "pageFrame must have pageContentFrame as the first child");
  return static_cast<nsPageContentFrame*>(frame);
}

nsSize nsPageFrame::ComputePageSize() const {
  // Compute the expected page-size.
  const StylePageSize& pageSize = PageContentFrame()->StylePage()->mSize;
  nsSize size = PresContext()->GetPageSize();
  if (pageSize.IsSize()) {
    // Use the specified size,
    // ignoring sizes that include a zero width or height.
    // These are also ignored in ServoStyleSet::GetPageSizeForPageName()
    // when getting the paper size.
    nscoord cssPageWidth = pageSize.AsSize().width.ToAppUnits();
    nscoord cssPageHeight = pageSize.AsSize().height.ToAppUnits();
    if (cssPageWidth > 0 && cssPageHeight > 0) {
      return nsSize{cssPageWidth, cssPageHeight};
    }
    // Invalid size; just return the default
    return size;
  }

  if (pageSize.IsOrientation()) {
    // Ensure the correct orientation is applied.
    if (pageSize.AsOrientation() == StylePageSizeOrientation::Portrait) {
      if (size.width > size.height) {
        std::swap(size.width, size.height);
      }
    } else {
      MOZ_ASSERT(pageSize.AsOrientation() ==
                 StylePageSizeOrientation::Landscape);
      if (size.width < size.height) {
        std::swap(size.width, size.height);
      }
    }
  } else {
    MOZ_ASSERT(pageSize.IsAuto(), "Impossible page-size value?");
  }
  return size;
}

float nsPageFrame::ComputeSinglePPSPageSizeScale(
    const nsSize aContentPageSize) const {
  MOZ_ASSERT(GetSharedPageData()->PagesPerSheetInfo()->mNumPages == 1,
             "Only intended for the pps==1 case");
  MOZ_ASSERT(aContentPageSize == ComputePageSize(),
             "Incorrect content page size");

  if (PageContentFrame()->StylePage()->mSize.IsAuto()) {
    return 1.0f;
  }

  const nsContainerFrame* const parent = GetParent();
  MOZ_ASSERT(parent && parent->IsPrintedSheetFrame(),
             "Parent of nsPageFrame should be PrintedSheetFrame");
  const auto* sheet = static_cast<const PrintedSheetFrame*>(parent);

  // Compute scaling due to a possible mismatch in the paper size we are
  // printing to (from the pres context) and the specified page size when the
  // content uses "@page {size: ...}" to specify a page size for the content.
  float scale = 1.0f;

  const nsSize sheetSize = sheet->GetSizeForChildren();
  nscoord contentPageHeight = aContentPageSize.height;
  // Scale down if the target is too wide.
  if (aContentPageSize.width > sheetSize.width) {
    scale *= float(sheetSize.width) / float(aContentPageSize.width);
    contentPageHeight = NSToCoordRound(contentPageHeight * scale);
  }
  // Scale down if the target is too tall.
  if (contentPageHeight > sheetSize.height) {
    scale *= float(sheetSize.height) / float(contentPageHeight);
  }
  MOZ_ASSERT(
      scale <= 1.0f,
      "Page-size mismatches should only have caused us to scale down, not up.");
  return scale;
}

double nsPageFrame::GetPageOrientationRotation(nsSharedPageData* aPD) const {
  if (aPD->PagesPerSheetInfo()->mNumPages == 1 && !PresContext()->IsScreen() &&
      aPD->mPrintSettings->GetOutputFormat() !=
          nsIPrintSettings::kOutputFormatPDF) {
    // In the single page-per-sheet case we rotate the page by essentially
    // rotating the entire sheet. But we can't do that when the output device
    // doesn't support mixed sheet orientations.
    return 0.0;
  }

  const StylePageOrientation& orientation =
      PageContentFrame()->StylePage()->mPageOrientation;

  if (orientation == StylePageOrientation::RotateLeft) {
    return -M_PI / 2.0;
  }
  if (orientation == StylePageOrientation::RotateRight) {
    return M_PI / 2.0;
  }
  return 0.0;
}

void nsPageFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                   const nsDisplayListSet& aLists) {
  nsDisplayList content(aBuilder);
  nsDisplayListSet set(&content, &content, &content, &content, &content,
                       &content);
  {
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    clipState.Clear();

    nsPresContext* const pc = PresContext();
    {
      // We need to extend the building rect to include the specified page size
      // (scaled by the print scaling factor), in case it is larger than the
      // physical page size. In that case the nsPageFrame will be the size of
      // the physical page, but the child nsPageContentFrame will be the larger
      // specified page size. The more correct way to do this would be to fully
      // reverse the result of ComputePagesPerSheetAndPageSizeTransform to
      // handle this scaling, but this should have the same result and is
      // easier.
      const float scale = pc->GetPageScale();
      const nsSize pageSize = ComputePageSize();
      const nsRect scaledPageRect{0, 0, NSToCoordCeil(pageSize.width / scale),
                                  NSToCoordCeil(pageSize.height / scale)};
      nsDisplayListBuilder::AutoBuildingDisplayList buildingForPageContentFrame(
          aBuilder, this, scaledPageRect, scaledPageRect);

      nsContainerFrame::BuildDisplayList(aBuilder, set);
    }

    if (pc->IsRootPaginatedDocument()) {
      content.AppendNewToTop<nsDisplayHeaderFooter>(aBuilder, this);

      // For print-preview, show margin guides if requested in the settings.
      if (pc->Type() == nsPresContext::eContext_PrintPreview &&
          mPD->mPrintSettings->GetShowMarginGuides()) {
        content.AppendNewToTop<nsDisplayGeneric>(
            aBuilder, this, PaintMarginGuides, "MarginGuides",
            DisplayItemType::TYPE_MARGIN_GUIDES);
      }
    }
  }

  // We'll be drawing the page with a (usually-trivial)
  // N-pages-per-sheet transform applied, so our passed-in visible rect
  // isn't meaningful while we're drawing our children, because the
  // transform could scale down content whose coordinates are off-screen
  // such that it ends up on-screen. So: we temporarily update the visible
  // rect to be the child nsPageFrame's whole frame-rect (represented in
  // this PrintedSheetFrame's coordinate space.
  content.AppendNewToTop<nsDisplayTransform>(
      aBuilder, this, &content, content.GetBuildingRect(),
      nsDisplayTransform::WithTransformGetter);

  set.MoveTo(aLists);
}

//------------------------------------------------------------------------------
void nsPageFrame::DeterminePageNum() {
  // If we have no previous continuation, we're page 1. Otherwise, we're
  // just one more than our previous continuation's page number.
  auto* prevContinuation = static_cast<nsPageFrame*>(GetPrevContinuation());
  mPageNum = prevContinuation ? prevContinuation->GetPageNum() + 1 : 1;
}

void nsPageFrame::PaintHeaderFooter(gfxContext& aRenderingContext, nsPoint aPt,
                                    bool aDisableSubpixelAA) {
  nsPresContext* pc = PresContext();

  nsRect rect(aPt, ComputePageSize());
  aRenderingContext.SetColor(sRGBColor::OpaqueBlack());

  DrawTargetAutoDisableSubpixelAntialiasing disable(
      aRenderingContext.GetDrawTarget(), aDisableSubpixelAA);

  // Get the FontMetrics to determine width.height of strings
  nsFontMetrics::Params params;
  params.userFontSet = pc->GetUserFontSet();
  params.textPerf = pc->GetTextPerfMetrics();
  params.featureValueLookup = pc->GetFontFeatureValuesLookup();
  RefPtr<nsFontMetrics> fontMet = pc->GetMetricsFor(mPD->mHeadFootFont, params);

  nscoord ascent = fontMet->MaxAscent();
  nscoord visibleHeight = fontMet->MaxHeight();

  // print document headers and footers
  nsString headerLeft, headerCenter, headerRight;
  mPD->mPrintSettings->GetHeaderStrLeft(headerLeft);
  mPD->mPrintSettings->GetHeaderStrCenter(headerCenter);
  mPD->mPrintSettings->GetHeaderStrRight(headerRight);
  DrawHeaderFooter(aRenderingContext, *fontMet, eHeader, headerLeft,
                   headerCenter, headerRight, rect, ascent, visibleHeight);

  nsString footerLeft, footerCenter, footerRight;
  mPD->mPrintSettings->GetFooterStrLeft(footerLeft);
  mPD->mPrintSettings->GetFooterStrCenter(footerCenter);
  mPD->mPrintSettings->GetFooterStrRight(footerRight);
  DrawHeaderFooter(aRenderingContext, *fontMet, eFooter, footerLeft,
                   footerCenter, footerRight, rect, ascent, visibleHeight);
}

void nsPageFrame::SetSharedPageData(nsSharedPageData* aPD) {
  mPD = aPD;
  // Set the shared data into the page frame before reflow
  PageContentFrame()->SetSharedPageData(mPD);
}

nsIFrame* NS_NewPageBreakFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  MOZ_ASSERT(aPresShell, "null PresShell");
  // check that we are only creating page break frames when printing
  NS_ASSERTION(aPresShell->GetPresContext()->IsPaginated(),
               "created a page break frame while not printing");

  return new (aPresShell)
      nsPageBreakFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsPageBreakFrame)

nsPageBreakFrame::nsPageBreakFrame(ComputedStyle* aStyle,
                                   nsPresContext* aPresContext)
    : nsLeafFrame(aStyle, aPresContext, kClassID) {}

nsPageBreakFrame::~nsPageBreakFrame() = default;

IntrinsicSize nsPageBreakFrame::GetIntrinsicSize() {
  IntrinsicSize intrinsicSize;
  intrinsicSize.ISize(GetWritingMode())
      .emplace(nsPresContext::CSSPixelsToAppUnits(1));
  return intrinsicSize;
}

void nsPageBreakFrame::Reflow(nsPresContext* aPresContext,
                              ReflowOutput& aReflowOutput,
                              const ReflowInput& aReflowInput,
                              nsReflowStatus& aStatus) {
  DO_GLOBAL_REFLOW_COUNT("nsPageBreakFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // Override reflow, since we don't want to deal with what our
  // computed values are.
  const WritingMode wm = aReflowInput.GetWritingMode();
  nscoord bSize = aReflowInput.AvailableBSize();
  if (aReflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
    bSize = nscoord(0);
  } else if (GetContent()->IsHTMLElement(nsGkAtoms::legend)) {
    // If this is a page break frame for a _rendered legend_ then it should be
    // ignored since these frames are inserted inside the fieldset's inner
    // frame and thus "misplaced".  nsFieldSetFrame::Reflow deals with these
    // forced breaks explicitly instead.
    const nsContainerFrame* parent = GetParent();
    if (parent &&
        parent->Style()->GetPseudoType() == PseudoStyleType::fieldsetContent) {
      while ((parent = parent->GetParent())) {
        if (const nsFieldSetFrame* const fieldset = do_QueryFrame(parent)) {
          const auto* const legend = fieldset->GetLegend();
          if (legend && legend->GetContent() == GetContent()) {
            bSize = nscoord(0);
          }
          break;
        }
      }
    }
  }
  LogicalSize finalSize(wm, *GetIntrinsicSize().ISize(wm), bSize);
  // round the height down to the nearest pixel
  // XXX(mats) why???
  finalSize.BSize(wm) -=
      finalSize.BSize(wm) % nsPresContext::CSSPixelsToAppUnits(1);
  aReflowOutput.SetSize(wm, finalSize);
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsPageBreakFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"PageBreak"_ns, aResult);
}
#endif
