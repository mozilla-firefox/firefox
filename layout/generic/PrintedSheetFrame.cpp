/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Rendering object for a printed or print-previewed sheet of paper */

#include "mozilla/PrintedSheetFrame.h"

#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_print.h"
#include "nsCSSFrameConstructor.h"
#include "nsPageContentFrame.h"
#include "nsPageFrame.h"
#include "nsPageSequenceFrame.h"

using namespace mozilla;

PrintedSheetFrame* NS_NewPrintedSheetFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle) {
  return new PrintedSheetFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_QUERYFRAME_HEAD(PrintedSheetFrame)
  NS_QUERYFRAME_ENTRY(PrintedSheetFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(PrintedSheetFrame)

void PrintedSheetFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                         const nsDisplayListSet& aLists) {
  if (PresContext()->IsScreen()) {
    // Draw the background/shadow/etc. of a blank sheet of paper, for
    // print-preview.
    DisplayBorderBackgroundOutline(aBuilder, aLists);
  }

  for (auto* frame : mFrames) {
    if (!frame->HasAnyStateBits(NS_PAGE_SKIPPED_BY_CUSTOM_RANGE)) {
      BuildDisplayListForChild(aBuilder, frame, aLists);
    }
  }
}

// If the given page is included in the user's page range, this function
// returns false. Otherwise, it tags the page with the
// NS_PAGE_SKIPPED_BY_CUSTOM_RANGE state bit and returns true.
static bool TagIfSkippedByCustomRange(nsPageFrame* aPageFrame, int32_t aPageNum,
                                      nsSharedPageData* aPD) {
  if (!nsIPrintSettings::IsPageSkipped(aPageNum, aPD->mPageRanges)) {
    MOZ_ASSERT(!aPageFrame->HasAnyStateBits(NS_PAGE_SKIPPED_BY_CUSTOM_RANGE),
               "page frames NS_PAGE_SKIPPED_BY_CUSTOM_RANGE state should "
               "only be set if we actually want to skip the page");
    return false;
  }

  aPageFrame->AddStateBits(NS_PAGE_SKIPPED_BY_CUSTOM_RANGE);
  return true;
}

void PrintedSheetFrame::ClaimPageFrameFromPrevInFlow() {
  MoveOverflowToChildList();
  if (!GetPrevContinuation()) {
    // The first page content frame of each document will not yet have its page
    // style set yet. This is because normally page style is set either from
    // the previous page content frame, or using the new page name when named
    // pages cause a page break in block reflow. Ensure that, for the first
    // page, it is set here so that all nsPageContentFrames have their page
    // style set before reflow.
    auto* firstChild = PrincipalChildList().FirstChild();
    MOZ_ASSERT(firstChild && firstChild->IsPageFrame(),
               "PrintedSheetFrame only has nsPageFrame children");
    auto* pageFrame = static_cast<nsPageFrame*>(firstChild);
    pageFrame->PageContentFrame()->EnsurePageName();
  }
}

void PrintedSheetFrame::Reflow(nsPresContext* aPresContext,
                               ReflowOutput& aReflowOutput,
                               const ReflowInput& aReflowInput,
                               nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("PrintedSheetFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // If we have a prev-in-flow, take its overflowing content:
  MoveOverflowToChildList();

  const WritingMode wm = aReflowInput.GetWritingMode();

  // See the comments for GetSizeForChildren.
  // Note that nsPageFrame::ComputeSinglePPSPageSizeScale depends on this value
  // and is currently called while reflowing a single nsPageFrame child (i.e.
  // before we've finished reflowing ourself). Ideally our children wouldn't be
  // accessing our dimensions until after we've finished reflowing ourself -
  // see bug 1835782.
  mSizeForChildren =
      nsSize(aReflowInput.AvailableISize(), aReflowInput.AvailableBSize());
  if (mPD->PagesPerSheetInfo()->mNumPages == 1) {
    auto* firstChild = PrincipalChildList().FirstChild();
    MOZ_ASSERT(firstChild && firstChild->IsPageFrame(),
               "PrintedSheetFrame only has nsPageFrame children");
    if (static_cast<nsPageFrame*>(firstChild)
            ->GetPageOrientationRotation(mPD) != 0.0) {
      std::swap(mSizeForChildren.width, mSizeForChildren.height);
    }
  }

  // Count the number of pages that are displayed on this sheet (i.e. how many
  // child frames we end up laying out, excluding any pages that are skipped
  // due to not being in the user's page-range selection).
  uint32_t numPagesOnThisSheet = 0;

  // Target for numPagesOnThisSheet.
  const uint32_t desiredPagesPerSheet = mPD->PagesPerSheetInfo()->mNumPages;

  if (desiredPagesPerSheet > 1) {
    ComputePagesPerSheetGridMetrics(mSizeForChildren);
  }

  // NOTE: I'm intentionally *not* using a range-based 'for' loop here, since
  // we potentially mutate the frame list (appending to the end) during the
  // list, which is not generally safe with range-based 'for' loops.
  for (auto* childFrame = mFrames.FirstChild(); childFrame;
       childFrame = childFrame->GetNextSibling()) {
    MOZ_ASSERT(childFrame->IsPageFrame(),
               "we're only expecting page frames as children");
    auto* pageFrame = static_cast<nsPageFrame*>(childFrame);

    // Be sure our child has a pointer to the nsSharedPageData and knows its
    // page number:
    pageFrame->SetSharedPageData(mPD);
    pageFrame->DeterminePageNum();

    if (!TagIfSkippedByCustomRange(pageFrame, pageFrame->GetPageNum(), mPD)) {
      // The page is going to be displayed on this sheet. Tell it its index
      // among the displayed pages, so we can use that to compute its "cell"
      // when painting.
      pageFrame->SetIndexOnSheet(numPagesOnThisSheet);
      numPagesOnThisSheet++;
    }

    // This is the app-unit size of the page (in physical & logical units).
    // Note: The page sizes come from CSS or else from the user selected size;
    // pages are never reflowed to fit their sheet - if/when necessary they are
    // scaled to fit their sheet. Hence why we get the page's own dimensions to
    // use as its "available space"/"container size" here.
    const nsSize physPageSize = pageFrame->ComputePageSize();
    LogicalSize availSize(wm, physPageSize);
    if (aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow) {
      availSize.BSize(wm) = aReflowInput.AvailableBSize();
    }

    ReflowInput pageReflowInput(aPresContext, aReflowInput, pageFrame,
                                availSize);

    // For layout purposes, we position *all* our nsPageFrame children at our
    // origin. Then, if we have multiple pages-per-sheet, we'll shrink & shift
    // each one into the right position as a paint-time effect, in
    // BuildDisplayList.
    LogicalPoint pagePos(wm);

    // Outparams for reflow:
    ReflowOutput pageReflowOutput(pageReflowInput);
    nsReflowStatus status;

    ReflowChild(pageFrame, aPresContext, pageReflowOutput, pageReflowInput, wm,
                pagePos, physPageSize, ReflowChildFlags::Default, status);

    FinishReflowChild(pageFrame, aPresContext, pageReflowOutput,
                      &pageReflowInput, wm, pagePos, physPageSize,
                      ReflowChildFlags::Default);

    // Since we don't support incremental reflow in printed documents (see the
    // early-return in nsPageSequenceFrame::Reflow), we can assume that this
    // was the first time that pageFrame has been reflowed, and so there's no
    // way that it could already have a next-in-flow. If it *did* have a
    // next-in-flow, we would need to handle it in the 'status' logic below.
    NS_ASSERTION(!pageFrame->GetNextInFlow(), "bad child flow list");

    // Did this page complete the document, or do we need to generate
    // another page frame?
    if (status.IsFullyComplete()) {
      // The page we just reflowed is the final page! Record its page number
      // as the number of pages:
      mPD->mRawNumPages = pageFrame->GetPageNum();
    } else {
      // Create a continuation for our page frame. We add the continuation to
      // our child list, and then potentially push it to our overflow list, if
      // it really belongs on the next sheet.
      nsIFrame* continuingPage =
          PresShell()->FrameConstructor()->CreateContinuingFrame(pageFrame,
                                                                 this);
      mFrames.InsertFrame(nullptr, pageFrame, continuingPage);
      const bool isContinuingPageSkipped =
          TagIfSkippedByCustomRange(static_cast<nsPageFrame*>(continuingPage),
                                    pageFrame->GetPageNum() + 1, mPD);

      // If we've already reached the target number of pages for this sheet,
      // and this continuation page that we just created is meant to be
      // displayed (i.e. it's in the chosen page range), then we need to push it
      // to our overflow list so that it'll go onto a subsequent sheet.
      // Otherwise we leave it on this sheet. This ensures we *only* generate
      // another sheet IFF there's a displayable page that will end up on it.
      if (numPagesOnThisSheet >= desiredPagesPerSheet &&
          !isContinuingPageSkipped) {
        PushChildrenToOverflow(continuingPage, pageFrame);
        aStatus.SetIncomplete();
      }
    }
  }

  // This should hold for the first sheet, because our UI should prevent the
  // user from creating a 0-length page range; and it should hold for
  // subsequent sheets because we should only create an additional sheet when
  // we discover a displayable (i.e. non-skipped) page that we need to push
  // to that new sheet.

  // XXXdholbert In certain edge cases (e.g. after a page-orientation-flip that
  // reduces the page count), it's possible for us to be given a page range
  // that is *entirely out-of-bounds* (with "from" & "to" both being larger
  // than our actual page-number count).  This scenario produces a single
  // PrintedSheetFrame with zero displayable pages on it, which is a weird
  // state to be in. This is hopefully a scenario that the frontend code can
  // detect and recover from (e.g. by clamping the range to our reported
  // `rawNumPages`), but it can't do that until *after* we've completed this
  // problematic reflow and can reported an up-to-date `rawNumPages` to the
  // frontend.  So: to give the frontend a chance to intervene and apply some
  // correction/clamping to its print-range parameters, we soften this
  // assertion *specifically for the first printed sheet*.
  if (!GetPrevContinuation()) {
    NS_WARNING_ASSERTION(numPagesOnThisSheet > 0,
                         "Shouldn't create a sheet with no displayable pages "
                         "on it");
  } else {
    MOZ_ASSERT(numPagesOnThisSheet > 0,
               "Shouldn't create a sheet with no displayable pages on it");
  }

  MOZ_ASSERT(numPagesOnThisSheet <= desiredPagesPerSheet,
             "Shouldn't have more than desired number of displayable pages "
             "on this sheet");
  mNumPages = numPagesOnThisSheet;

  // Populate our ReflowOutput outparam -- just use up all the
  // available space, for both our desired size & overflow areas.
  aReflowOutput.ISize(wm) = aReflowInput.AvailableISize();
  if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE) {
    aReflowOutput.BSize(wm) = aReflowInput.AvailableBSize();
  }
  aReflowOutput.SetOverflowAreasToDesiredBounds();

  FinishAndStoreOverflow(&aReflowOutput);
}

nsSize PrintedSheetFrame::ComputeSheetSize(const nsPresContext* aPresContext) {
  // We use the user selected page (sheet) dimensions, and default to the
  // orientation as specified by the user.
  nsSize sheetSize = aPresContext->GetPageSize();

  // Don't waste cycles changing the orientation of a square.
  if (sheetSize.width == sheetSize.height) {
    return sheetSize;
  }

  if (!StaticPrefs::
          print_save_as_pdf_use_page_rule_size_as_paper_size_enabled()) {
    if (mPD->mPrintSettings->HasOrthogonalPagesPerSheet()) {
      std::swap(sheetSize.width, sheetSize.height);
    }
    return sheetSize;
  }

  auto* firstChild = PrincipalChildList().FirstChild();
  MOZ_ASSERT(firstChild->IsPageFrame(),
             "PrintedSheetFrame only has nsPageFrame children");
  auto* sheetsFirstPageFrame = static_cast<nsPageFrame*>(firstChild);

  nsSize pageSize = sheetsFirstPageFrame->ComputePageSize();

  // Don't waste cycles changing the orientation of a square.
  if (pageSize.width == pageSize.height) {
    return sheetSize;
  }

  const bool pageIsRotated =
      sheetsFirstPageFrame->GetPageOrientationRotation(mPD) != 0.0;

  if (pageIsRotated && pageSize.width == pageSize.height) {
    // Straighforward rotation without needing sheet orientation optimization.
    std::swap(sheetSize.width, sheetSize.height);
    return sheetSize;
  }

  // Try to orient the sheet optimally based on the physical orientation of the
  // first/sole page on the sheet. (In the multiple pages-per-sheet case, the
  // first page is the only one that exists at this point in the code, so it is
  // the only one we can reason about. Any other pages may, or may not, have
  // the same physical orientation.)

  if (pageIsRotated) {
    // Fix up for its physical orientation:
    std::swap(pageSize.width, pageSize.height);
  }

  const bool pageIsPortrait = pageSize.width < pageSize.height;
  const bool sheetIsPortrait = sheetSize.width < sheetSize.height;

  // Switch the sheet orientation if the page orientation is different, or
  // if we need to switch it because the number of pages-per-sheet demands
  // orthogonal sheet layout, but not if both are true since then we'd
  // actually need to double switch.
  if ((sheetIsPortrait != pageIsPortrait) !=
      mPD->mPrintSettings->HasOrthogonalPagesPerSheet()) {
    std::swap(sheetSize.width, sheetSize.height);
  }

  return sheetSize;
}

void PrintedSheetFrame::ComputePagesPerSheetGridMetrics(
    const nsSize& aSheetSize) {
  MOZ_ASSERT(mPD->PagesPerSheetInfo()->mNumPages > 1,
             "Unnecessary to call this in a regular 1-page-per-sheet scenario; "
             "the computed values won't ever be used in that case");

  // Compute the space available for the pages-per-sheet "page grid" (just
  // subtract the sheet's unwriteable margin area):
  nsSize availSpaceOnSheet = aSheetSize;
  nsMargin uwm = mPD->mPrintSettings->GetIgnoreUnwriteableMargins()
                     ? nsMargin{}
                     : nsPresContext::CSSTwipsToAppUnits(
                           mPD->mPrintSettings->GetUnwriteableMarginInTwips());

  // XXXjwatt Once we support heterogeneous sheet orientations, we'll also need
  // to rotate uwm if this sheet is not the primary orientation.
  if (mPD->mPrintSettings->HasOrthogonalPagesPerSheet()) {
    // aSheetSize already takes account of the switch of *sheet* orientation
    // that we do in this case (the orientation implied by the page size
    // dimensions in the nsIPrintSettings applies to *pages*). That is not the
    // case for the unwriteable margins since we got them from the
    // nsIPrintSettings object ourself, so we need to adjust `uwm` here.
    //
    // Note: In practice, sheets with an orientation that is orthogonal to the
    // physical orientation of sheets output by a printer must be rotated 90
    // degrees for/by the printer. In that case the convention seems to be that
    // the "left" edge of the orthogonally oriented sheet becomes the "top",
    // and so forth. The rotation direction will matter in the case that the
    // top and bottom unwriteable margins are different, or the left and right
    // unwriteable margins are different. So we need to match this behavior,
    // which means we must rotate the `uwm` 90 degrees *counter-clockwise*.
    nsMargin rotated(uwm.right, uwm.bottom, uwm.left, uwm.top);
    uwm = rotated;
  }

  availSpaceOnSheet.width -= uwm.LeftRight();
  availSpaceOnSheet.height -= uwm.TopBottom();

  if (MOZ_UNLIKELY(availSpaceOnSheet.IsEmpty())) {
    // This sort of thing should be rare, but it can happen if there are
    // bizarre page sizes, and/or if there's an unexpectedly large unwriteable
    // margin area.
    NS_WARNING("Zero area for pages-per-sheet grid, or zero-sized grid");
    mGridOrigin = nsPoint(0, 0);
    mGridNumCols = 1;
    return;
  }

  // If there are a different number of rows vs. cols, we'll aim to put
  // the larger number of items in the longer axis.
  const auto* ppsInfo = mPD->PagesPerSheetInfo();
  uint32_t smallerNumTracks = ppsInfo->mNumPages / ppsInfo->mLargerNumTracks;
  bool sheetIsPortraitLike = aSheetSize.width < aSheetSize.height;
  auto numCols =
      sheetIsPortraitLike ? smallerNumTracks : ppsInfo->mLargerNumTracks;
  auto numRows =
      sheetIsPortraitLike ? ppsInfo->mLargerNumTracks : smallerNumTracks;

  mGridOrigin = nsPoint(uwm.left, uwm.top);
  mGridNumCols = numCols;
  mGridCellWidth = availSpaceOnSheet.width / nscoord(numCols);
  mGridCellHeight = availSpaceOnSheet.height / nscoord(numRows);
}

gfx::IntSize PrintedSheetFrame::GetPrintTargetSizeInPoints(
    const int32_t aAppUnitsPerPhysicalInch) const {
  const auto size = GetSize();
  MOZ_ASSERT(size.width > 0 && size.height > 0);
  const float pointsPerAppUnit =
      POINTS_PER_INCH_FLOAT / float(aAppUnitsPerPhysicalInch);
  return IntSize::Ceil(float(size.width) * pointsPerAppUnit,
                       float(size.height) * pointsPerAppUnit);
}

#ifdef DEBUG_FRAME_DUMP
nsresult PrintedSheetFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"PrintedSheet"_ns, aResult);
}
#endif

}  // namespace mozilla
