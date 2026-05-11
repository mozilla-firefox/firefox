/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPageSequenceFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/PrintedSheetFrame.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/intl/AppDateTimeFormat.h"
#include "nsCOMPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsDisplayList.h"
#include "nsHTMLCanvasFrame.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsIPrintSettings.h"
#include "nsPageFrame.h"
#include "nsPresContext.h"
#include "nsRegion.h"
#include "nsServiceManagerUtils.h"
#include "nsSubDocumentFrame.h"

using namespace mozilla;
using namespace mozilla::dom;

mozilla::LazyLogModule gLayoutPrintingLog("printing-layout");

#define PR_PL(_p1) MOZ_LOG(gLayoutPrintingLog, mozilla::LogLevel::Debug, _p1)

nsPageSequenceFrame* NS_NewPageSequenceFrame(PresShell* aPresShell,
                                             ComputedStyle* aStyle) {
  return new (aPresShell)
      nsPageSequenceFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsPageSequenceFrame)

static const nsPagesPerSheetInfo kSupportedPagesPerSheet[] = {
    /* Members are: {mNumPages, mLargerNumTracks} */
    // clang-format off
    {1, 1},
    {2, 2},
    {4, 2},
    {6, 3},
    {9, 3},
    {16, 4},
    // clang-format on
};

inline void SanityCheckPagesPerSheetInfo() {
#ifdef DEBUG
  // Sanity-checks:
  MOZ_ASSERT(std::size(kSupportedPagesPerSheet) > 0,
             "Should have at least one pages-per-sheet option.");
  MOZ_ASSERT(kSupportedPagesPerSheet[0].mNumPages == 1,
             "The 0th index is reserved for default 1-page-per-sheet entry");

  uint16_t prevInfoPPS = 0;
  for (const auto& info : kSupportedPagesPerSheet) {
    MOZ_ASSERT(info.mNumPages > prevInfoPPS,
               "page count field should be positive & monotonically increase");
    MOZ_ASSERT(info.mLargerNumTracks > 0,
               "page grid has to have a positive number of tracks");
    MOZ_ASSERT(info.mNumPages % info.mLargerNumTracks == 0,
               "page count field should be evenly divisible by "
               "the given track-count");
    prevInfoPPS = info.mNumPages;
  }
#endif
}

const nsPagesPerSheetInfo& nsPagesPerSheetInfo::LookupInfo(int32_t aPPS) {
  SanityCheckPagesPerSheetInfo();

  // Walk the array, looking for a match:
  for (const auto& info : kSupportedPagesPerSheet) {
    if (aPPS == info.mNumPages) {
      return info;
    }
  }

  NS_WARNING("Unsupported pages-per-sheet value");
  // If no match was found, return the first entry (for 1 page per sheet).
  return kSupportedPagesPerSheet[0];
}

const nsPagesPerSheetInfo* nsSharedPageData::PagesPerSheetInfo() {
  if (mPagesPerSheetInfo) {
    return mPagesPerSheetInfo;
  }

  int32_t pagesPerSheet;
  if (!mPrintSettings ||
      NS_FAILED(mPrintSettings->GetNumPagesPerSheet(&pagesPerSheet))) {
    // If we can't read the value from print settings, just fall back to 1.
    pagesPerSheet = 1;
  }

  mPagesPerSheetInfo = &nsPagesPerSheetInfo::LookupInfo(pagesPerSheet);
  return mPagesPerSheetInfo;
}

nsPageSequenceFrame::nsPageSequenceFrame(ComputedStyle* aStyle,
                                         nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID),
      mMaxSheetSize(mWritingMode),
      mScrollportSize(mWritingMode),
      mCalledBeginPage(false),
      mCurrentCanvasListSetup(false) {
  mPageData.mHeadFootFont =
      *PresContext()
           ->Document()
           ->GetFontPrefsForLang(aStyle->StyleFont()->mLanguage)
           ->GetDefaultFont(StyleGenericFontFamily::Serif);
  mPageData.mHeadFootFont.size =
      Length::FromPixels(CSSPixel::FromPoints(10.0f));
  mPageData.mPrintSettings = aPresContext->GetPrintSettings();
  MOZ_RELEASE_ASSERT(mPageData.mPrintSettings, "How?");

  // Doing this here so we only have to go get these formats once
  SetPageNumberFormat("pagenumber", "%1$d", true);
  SetPageNumberFormat("pageofpages", "%1$d of %2$d", false);
}

nsPageSequenceFrame::~nsPageSequenceFrame() { ResetPrintCanvasList(); }

NS_QUERYFRAME_HEAD(nsPageSequenceFrame)
  NS_QUERYFRAME_ENTRY(nsPageSequenceFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

//----------------------------------------------------------------------

float nsPageSequenceFrame::GetPrintPreviewScale() const {
  nsPresContext* pc = PresContext();
  float scale = pc->GetPrintPreviewScaleForSequenceFrameOrScrollbars();

  WritingMode wm = GetWritingMode();
  if (pc->IsScreen() && MOZ_LIKELY(mScrollportSize.ISize(wm) > 0 &&
                                   mScrollportSize.BSize(wm) > 0)) {
    // For print preview, scale down as-needed to ensure that each of our
    // sheets will fit in the the scrollport.

    // Check if the current scale is sufficient for our sheets to fit in inline
    // axis (and if not, reduce the scale so that it will fit).
    nscoord scaledISize = NSToCoordCeil(mMaxSheetSize.ISize(wm) * scale);
    if (scaledISize > mScrollportSize.ISize(wm)) {
      scale *= float(mScrollportSize.ISize(wm)) / float(scaledISize);
    }

    // Further reduce the scale (if needed) to be sure each sheet will fit in
    // block axis, too.
    // NOTE: in general, a scrollport's BSize *could* be unconstrained,
    // i.e. sized to its contents. If that happens, then shrinking the contents
    // to fit the scrollport is not a meaningful operation in this axis, so we
    // skip over this.  But we can be pretty sure that the print-preview UI
    // will have given the scrollport a fixed size; hence the MOZ_LIKELY here.
    if (MOZ_LIKELY(mScrollportSize.BSize(wm) != NS_UNCONSTRAINEDSIZE)) {
      nscoord scaledBSize = NSToCoordCeil(mMaxSheetSize.BSize(wm) * scale);
      if (scaledBSize > mScrollportSize.BSize(wm)) {
        scale *= float(mScrollportSize.BSize(wm)) / float(scaledBSize);
      }
    }
  }
  return scale;
}

void nsPageSequenceFrame::PopulateReflowOutput(
    ReflowOutput& aReflowOutput, const ReflowInput& aReflowInput) {
  // Aim to fill the whole available space, not only so we can act as a
  // background in print preview but also handle overflow in child page frames
  // correctly.
  // Use availableISize so we don't cause a needless horizontal scrollbar.
  float scale = GetPrintPreviewScale();

  WritingMode wm = aReflowInput.GetWritingMode();
  nscoord iSize = wm.IsVertical() ? mSize.Height() : mSize.Width();
  nscoord bSize = wm.IsVertical() ? mSize.Width() : mSize.Height();

  nscoord availableISize = aReflowInput.AvailableISize();
  nscoord computedBSize = aReflowInput.ComputedBSize();
  if (MOZ_UNLIKELY(computedBSize == NS_UNCONSTRAINEDSIZE)) {
    // We have unconstrained BSize, which should only happen if someone calls
    // SizeToContent() on our window (which we don't expect to happen for
    // actual user flows, but is possible for fuzzers to trigger). We just nerf
    // the ReflowInput's contributions to the std::max() expressions below,
    // which does indeed make us "size to content", via letting std::max()
    // choose the scaled iSize/bSize expressions.
    availableISize = computedBSize = 0;
  }
  aReflowOutput.ISize(wm) =
      std::max(NSToCoordFloor(iSize * scale), availableISize);
  aReflowOutput.BSize(wm) =
      std::max(NSToCoordFloor(bSize * scale), computedBSize);
  aReflowOutput.SetOverflowAreasToDesiredBounds();
}

// Helper function to compute the offset needed to center a child
// page-frame's margin-box inside our content-box.
nscoord nsPageSequenceFrame::ComputeCenteringMargin(
    nscoord aContainerContentBoxWidth, nscoord aChildPaddingBoxWidth,
    const nsMargin& aChildPhysicalMargin) {
  // We'll be centering our child's margin-box, so get the size of that:
  nscoord childMarginBoxWidth =
      aChildPaddingBoxWidth + aChildPhysicalMargin.LeftRight();

  // When rendered, our child's rect will actually be scaled up by the
  // print-preview scale factor, via ComputePageSequenceTransform().
  // We really want to center *that scaled-up rendering* inside of
  // aContainerContentBoxWidth.  So, we scale up its margin-box here...
  float scale = GetPrintPreviewScale();
  nscoord scaledChildMarginBoxWidth =
      NSToCoordRound(childMarginBoxWidth * scale);

  // ...and see we how much space is left over, when we subtract that scaled-up
  // size from the container width:
  nscoord scaledExtraSpace =
      aContainerContentBoxWidth - scaledChildMarginBoxWidth;

  if (scaledExtraSpace <= 0) {
    // (Don't bother centering if there's zero/negative space.)
    return 0;
  }

  // To center the child, we want to give it an additional left-margin of half
  // of the extra space.  And then, we have to scale that space back down, so
  // that it'll produce the correct scaled-up amount when we render (because
  // rendering will scale it back up):
  return NSToCoordRound(scaledExtraSpace * 0.5 / scale);
}

uint32_t nsPageSequenceFrame::GetPagesInFirstSheet() const {
  nsIFrame* firstSheet = mFrames.FirstChild();
  if (!firstSheet) {
    return 0;
  }

  MOZ_DIAGNOSTIC_ASSERT(firstSheet->IsPrintedSheetFrame());
  return static_cast<PrintedSheetFrame*>(firstSheet)->GetNumPages();
}

/*
 * Note: we largely position/size out our children (page frames) using
 * \*physical\* x/y/width/height values, because the print preview UI is always
 * arranged in the same orientation, regardless of writing mode.
 */
void nsPageSequenceFrame::Reflow(nsPresContext* aPresContext,
                                 ReflowOutput& aReflowOutput,
                                 const ReflowInput& aReflowInput,
                                 nsReflowStatus& aStatus) {
  MarkInReflow();
  MOZ_ASSERT(aPresContext->IsRootPaginatedDocument(),
             "A Page Sequence is only for real pages");
  DO_GLOBAL_REFLOW_COUNT("nsPageSequenceFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE_REFLOW_IN("nsPageSequenceFrame::Reflow");

  auto CenterPages = [&] {
    for (nsIFrame* child : mFrames) {
      nsMargin pageCSSMargin = child->GetUsedMargin();
      nscoord centeringMargin =
          ComputeCenteringMargin(aReflowInput.ComputedWidth(),
                                 child->GetRect().Width(), pageCSSMargin);
      nscoord newX = pageCSSMargin.left + centeringMargin;

      // Adjust the child's x-position:
      child->MovePositionBy(nsPoint(newX - child->GetNormalPosition().x, 0));
    }
  };

  if (aPresContext->IsScreen()) {
    // When we're displayed on-screen, the computed size that we're given is
    // the size of our scrollport. We need to save this for use in
    // GetPrintPreviewScale.
    // (NOTE: It's possible but unlikely that we have an unconstrained BSize
    // here, if we're being sized to content. GetPrintPreviewScale() checks
    // for and handles this, when making use of this member-var.)
    mScrollportSize = aReflowInput.ComputedSize();
  }

  // Don't do incremental reflow until we've taught tables how to do
  // it right in paginated mode.
  if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Return our desired size
    PopulateReflowOutput(aReflowOutput, aReflowInput);
    FinishAndStoreOverflow(&aReflowOutput);

    if (GetSize() != aReflowOutput.PhysicalSize()) {
      CenterPages();
    }
    return;
  }

  const bool shouldDoMeasuringReflow = [&]() {
    if (!aPresContext->FragmentainerAwarePositioningEnabled()) {
      return false;
    }
    if (GetPrevInFlow()) {
      // A measuring reflow is only needed on first-in-flow.
      return false;
    }
    // Only do measuring reflow if there are absolutely positioned descendants
    // since the purpose is to compute their unfragmented positions, sizes, etc.
    return nsLayoutUtils::HasAbsolutelyPositionedDescendants(this);
  }();

  if (shouldDoMeasuringReflow) {
    if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
      // Mark sheets dirty for an incremental measuring reflow.
      MarkPrincipalChildrenDirty();
    }

    for (nsIFrame* kidFrame : mFrames) {
      auto* sheet = static_cast<PrintedSheetFrame*>(kidFrame);
      sheet->SetSharedPageData(&mPageData);

      // If we want to reliably access the nsPageFrame before reflowing the
      // sheet frame, we need to call this:
      sheet->ClaimPageFrameFromPrevInFlow();

      const nsSize sheetSize = sheet->ComputeSheetSize(aPresContext);

      const auto kidWM = kidFrame->GetWritingMode();
      LogicalSize availSize(kidWM, sheetSize);

      // Reflow the kid with an unconstrained available block-size, to compute
      // unfragmented positions, sizes, etc. for absolutely positioned
      // descendants.
      availSize.BSize(kidWM) = NS_UNCONSTRAINEDSIZE;

      ReflowInput kidReflowInput(aPresContext, aReflowInput, kidFrame,
                                 availSize);

      // Given the kid is reflowed under an unconstrained available block-size,
      // BreakType::Page doesn't really have any effect, but we keep it for
      // consistency with the normal reflow below.
      kidReflowInput.mBreakType = BreakType::Page;
      kidReflowInput.mFlags.mIsInFragmentainerMeasuringReflow = true;

      ReflowOutput kidReflowOutput(kidReflowInput);
      nsReflowStatus status;

      // Position doesn't matter for measuring reflow.
      const WritingMode wm = kidFrame->GetWritingMode();
      ReflowChild(kidFrame, aPresContext, kidReflowOutput, kidReflowInput, wm,
                  LogicalPoint(wm), sheetSize, ReflowChildFlags::Default,
                  status);
      FinishReflowChild(kidFrame, aPresContext, kidReflowOutput,
                        &kidReflowInput, wm, LogicalPoint(wm), sheetSize,
                        ReflowChildFlags::Default);
    }

    // Mark sheets dirty for normal reflow below.
    MarkPrincipalChildrenDirty();
  }

  nsIntMargin unwriteableTwips =
      mPageData.mPrintSettings->GetUnwriteableMarginInTwips();

  nsIntMargin edgeTwips = mPageData.mPrintSettings->GetEdgeInTwips();

  // sanity check the values. three inches are sometimes needed
  int32_t threeInches = NS_INCHES_TO_INT_TWIPS(3.0);
  edgeTwips.EnsureAtMost(
      nsIntMargin(threeInches, threeInches, threeInches, threeInches));
  edgeTwips.EnsureAtLeast(unwriteableTwips);

  mPageData.mEdgePaperMargin = nsPresContext::CSSTwipsToAppUnits(edgeTwips);

  // Get the custom page-range state:
  mPageData.mPrintSettings->GetPageRanges(mPageData.mPageRanges);

  // We use the CSS "margin" property on the -moz-printed-sheet pseudoelement
  // to determine the space between each printed sheet in print preview.
  // Keep a running y-offset for each printed sheet.
  nscoord y = 0;

  // These represent the maximum sheet size across all our sheets (in each
  // axis), inflated a bit to account for the -moz-printed-sheet 'margin'.
  nscoord maxInflatedSheetWidth = 0;
  nscoord maxInflatedSheetHeight = 0;

  // Tile the sheets vertically
  for (nsIFrame* kidFrame : mFrames) {
    // Set the shared data into the page frame before reflow
    MOZ_ASSERT(kidFrame->IsPrintedSheetFrame(),
               "we're only expecting PrintedSheetFrame as children");
    auto* sheet = static_cast<PrintedSheetFrame*>(kidFrame);
    sheet->SetSharedPageData(&mPageData);

    // If we want to reliably access the nsPageFrame before reflowing the sheet
    // frame, we need to call this:
    sheet->ClaimPageFrameFromPrevInFlow();

    const nsSize sheetSize = sheet->ComputeSheetSize(aPresContext);

    // Reflow the sheet
    ReflowInput kidReflowInput(
        aPresContext, aReflowInput, kidFrame,
        LogicalSize(kidFrame->GetWritingMode(), sheetSize));
    kidReflowInput.mBreakType = BreakType::Page;

    ReflowOutput kidReflowOutput(kidReflowInput);
    nsReflowStatus status;

    kidReflowInput.SetComputedISize(kidReflowInput.AvailableISize());
    // kidReflowInput.SetComputedHeight(kidReflowInput.AvailableHeight());
    PR_PL(("AV ISize: %d   BSize: %d\n", kidReflowInput.AvailableISize(),
           kidReflowInput.AvailableBSize()));

    nsMargin pageCSSMargin = kidReflowInput.ComputedPhysicalMargin();
    y += pageCSSMargin.top;

    nscoord x = pageCSSMargin.left;

    // Place and size the sheet.
    ReflowChild(kidFrame, aPresContext, kidReflowOutput, kidReflowInput, x, y,
                ReflowChildFlags::Default, status);

    FinishReflowChild(kidFrame, aPresContext, kidReflowOutput, &kidReflowInput,
                      x, y, ReflowChildFlags::Default);
    MOZ_ASSERT(kidFrame->GetSize() == sheetSize,
               "PrintedSheetFrame::ComputeSheetSize() gave the wrong size!");
    y += kidReflowOutput.Height();
    y += pageCSSMargin.bottom;

    maxInflatedSheetWidth =
        std::max(maxInflatedSheetWidth,
                 kidReflowOutput.Width() + pageCSSMargin.LeftRight());
    maxInflatedSheetHeight =
        std::max(maxInflatedSheetHeight,
                 kidReflowOutput.Height() + pageCSSMargin.TopBottom());

    // Is the sheet complete?
    nsIFrame* kidNextInFlow = kidFrame->GetNextInFlow();

    if (status.IsFullyComplete()) {
      NS_ASSERTION(!kidNextInFlow, "bad child flow list");
    } else if (!kidNextInFlow) {
      // The sheet isn't complete and it doesn't have a next-in-flow, so
      // create a continuing sheet.
      nsIFrame* continuingSheet =
          PresShell()->FrameConstructor()->CreateContinuingFrame(kidFrame,
                                                                 this);

      // Add it to our child list
      mFrames.InsertFrame(nullptr, kidFrame, continuingSheet);
    }
  }

  nsAutoString formattedDateString;
  PRTime now = PR_Now();
  mozilla::intl::DateTimeFormat::StyleBag style;
  style.date = Some(mozilla::intl::DateTimeFormat::Style::Short);
  style.time = Some(mozilla::intl::DateTimeFormat::Style::Short);
  if (NS_SUCCEEDED(mozilla::intl::AppDateTimeFormat::Format(
          style, now, formattedDateString))) {
    SetDateTimeStr(formattedDateString);
  }

  // cache the size so we can set the desired size for the other reflows that
  // happen.  Since we're tiling our sheets vertically: in the x axis, we are
  // as wide as our widest sheet (inflated via "margin"); and in the y axis,
  // we're as tall as the sum of our sheets' inflated heights, which the 'y'
  // variable is conveniently storing at this point.
  mSize = nsSize(maxInflatedSheetWidth, y);

  if (aPresContext->IsScreen()) {
    // Also cache the maximum size of all our sheets, to use together with the
    // scrollport size (available as our computed size, and captured higher up
    // in this function), so that we can scale to ensure that every sheet will
    // fit in the scrollport.
    WritingMode wm = aReflowInput.GetWritingMode();
    mMaxSheetSize =
        LogicalSize(wm, nsSize(maxInflatedSheetWidth, maxInflatedSheetHeight));
  }

  // Return our desired size
  // Adjust the reflow size by PrintPreviewScale so the scrollbars end up the
  // correct size
  PopulateReflowOutput(aReflowOutput, aReflowInput);

  FinishAndStoreOverflow(&aReflowOutput);

  // Now center our pages.
  CenterPages();

  NS_FRAME_TRACE_REFLOW_OUT("nsPageSequenceFrame::Reflow", aStatus);
}

//----------------------------------------------------------------------

#ifdef DEBUG_FRAME_DUMP
nsresult nsPageSequenceFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"PageSequence"_ns, aResult);
}
#endif

// Helper Function
void nsPageSequenceFrame::SetPageNumberFormat(const char* aPropName,
                                              const char* aDefPropVal,
                                              bool aPageNumOnly) {
  // Doing this here so we only have to go get these formats once
  nsAutoString pageNumberFormat;
  // Now go get the Localized Page Formating String
  nsresult rv = nsContentUtils::GetLocalizedString(
      PropertiesFile::PRINTING_PROPERTIES, aPropName, pageNumberFormat);
  if (NS_FAILED(rv)) {  // back stop formatting
    pageNumberFormat.AssignASCII(aDefPropVal);
  }

  SetPageNumberFormat(pageNumberFormat, aPageNumOnly);
}

nsresult nsPageSequenceFrame::StartPrint(nsPresContext* aPresContext,
                                         nsIPrintSettings* aPrintSettings,
                                         const nsAString& aDocTitle,
                                         const nsAString& aDocURL) {
  NS_ENSURE_ARG_POINTER(aPresContext);
  NS_ENSURE_ARG_POINTER(aPrintSettings);

  if (!mPageData.mPrintSettings) {
    mPageData.mPrintSettings = aPrintSettings;
  }

  if (!aDocTitle.IsEmpty()) {
    mPageData.mDocTitle = aDocTitle;
  }
  if (!aDocURL.IsEmpty()) {
    mPageData.mDocURL = aDocURL;
  }

  // Begin printing of the document
  mCurrentSheetIdx = 0;
  return NS_OK;
}

static void GetPrintCanvasElementsInFrame(
    nsIFrame* aFrame, nsTArray<RefPtr<HTMLCanvasElement>>* aArr) {
  if (!aFrame) {
    return;
  }
  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      // Check if child is a nsHTMLCanvasFrame.
      nsHTMLCanvasFrame* canvasFrame = do_QueryFrame(child);

      // If there is a canvasFrame, try to get actual canvas element.
      if (canvasFrame) {
        HTMLCanvasElement* canvas =
            HTMLCanvasElement::FromNodeOrNull(canvasFrame->GetContent());
        if (canvas && canvas->GetMozPrintCallback()) {
          aArr->AppendElement(canvas);
          continue;
        }
      }

      if (!child->PrincipalChildList().FirstChild()) {
        nsSubDocumentFrame* subdocumentFrame = do_QueryFrame(child);
        if (subdocumentFrame) {
          // Descend into the subdocument
          nsIFrame* root = subdocumentFrame->GetSubdocumentRootFrame();
          child = root;
        }
      }
      // The current child is not a nsHTMLCanvasFrame OR it is but there is
      // no HTMLCanvasElement on it. Check if children of `child` might
      // contain a HTMLCanvasElement.
      GetPrintCanvasElementsInFrame(child, aArr);
    }
  }
}

// Note: this isn't quite a full tree traversal, since we exclude any
// nsPageFame children that have the NS_PAGE_SKIPPED_BY_CUSTOM_RANGE state-bit.
static void GetPrintCanvasElementsInSheet(
    PrintedSheetFrame* aSheetFrame, nsTArray<RefPtr<HTMLCanvasElement>>* aArr) {
  MOZ_ASSERT(aSheetFrame, "Caller should've null-checked for us already");
  for (nsIFrame* child : aSheetFrame->PrincipalChildList()) {
    // Exclude any pages that are technically children but are skipped by a
    // custom range; they're not meant to be printed, so we don't want to
    // waste time rendering their canvas descendants.
    MOZ_ASSERT(child->IsPageFrame(),
               "PrintedSheetFrame's children must all be nsPageFrames");
    auto* pageFrame = static_cast<nsPageFrame*>(child);
    if (!pageFrame->HasAnyStateBits(NS_PAGE_SKIPPED_BY_CUSTOM_RANGE)) {
      GetPrintCanvasElementsInFrame(pageFrame, aArr);
    }
  }
}

PrintedSheetFrame* nsPageSequenceFrame::GetCurrentSheetFrame() {
  uint32_t i = 0;
  for (nsIFrame* child : mFrames) {
    MOZ_ASSERT(child->IsPrintedSheetFrame(),
               "Our children must all be PrintedSheetFrame");
    if (i == mCurrentSheetIdx) {
      return static_cast<PrintedSheetFrame*>(child);
    }
    ++i;
  }
  return nullptr;
}

nsresult nsPageSequenceFrame::PrePrintNextSheet(nsITimerCallback* aCallback,
                                                bool* aDone) {
  PrintedSheetFrame* currentSheet = GetCurrentSheetFrame();
  if (!currentSheet) {
    *aDone = true;
    return NS_ERROR_FAILURE;
  }

  if (!PresContext()->IsRootPaginatedDocument()) {
    // XXXdholbert I don't think this clause is ever actually visited in
    // practice... Maybe we should warn & return a failure code?  There used to
    // be a comment here explaining why we don't need to proceed past this
    // point for print preview, but in fact, this function isn't even called for
    // print preview.
    *aDone = true;
    return NS_OK;
  }

  // If the canvasList is null, then generate it and start the render
  // process for all the canvas.
  if (!mCurrentCanvasListSetup) {
    mCurrentCanvasListSetup = true;
    GetPrintCanvasElementsInSheet(currentSheet, &mCurrentCanvasList);

    if (!mCurrentCanvasList.IsEmpty()) {
      nsresult rv = NS_OK;

      // Begin printing of the document
      nsDeviceContext* dc = PresContext()->DeviceContext();
      PR_PL(("\n"));
      PR_PL(("***************** BeginPage *****************\n"));
      const gfx::IntSize sizeInPoints =
          currentSheet->GetPrintTargetSizeInPoints(
              dc->AppUnitsPerPhysicalInch());
      rv = dc->BeginPage(sizeInPoints);
      NS_ENSURE_SUCCESS(rv, rv);

      mCalledBeginPage = true;

      UniquePtr<gfxContext> renderingContext = dc->CreateRenderingContext();
      NS_ENSURE_TRUE(renderingContext, NS_ERROR_OUT_OF_MEMORY);

      DrawTarget* drawTarget = renderingContext->GetDrawTarget();
      if (NS_WARN_IF(!drawTarget)) {
        return NS_ERROR_FAILURE;
      }

      for (HTMLCanvasElement* canvas : Reversed(mCurrentCanvasList)) {
        CSSIntSize size = canvas->GetSize();

        RefPtr<DrawTarget> canvasTarget = drawTarget->CreateSimilarDrawTarget(
            size.ToUnknownSize(), drawTarget->GetFormat());
        if (!canvasTarget) {
          continue;
        }

        nsICanvasRenderingContextInternal* ctx = canvas->GetCurrentContext();
        if (!ctx) {
          continue;
        }

        // Initialize the context with the new DrawTarget.
        ctx->InitializeWithDrawTarget(nullptr, WrapNotNull(canvasTarget));

        // Start the rendering process.
        // Note: Other than drawing to our CanvasRenderingContext2D, the
        // callback cannot access or mutate our static clone document.  It is
        // evaluated in its original context (the window of the original
        // document) of course, and our canvas has a strong ref to the
        // original HTMLCanvasElement (in mOriginalCanvas) so that if the
        // callback calls GetCanvas() on our CanvasRenderingContext2D (passed
        // to it via a MozCanvasPrintState argument) it will be given the
        // original 'canvas' element.
        AutoWeakFrame weakFrame = this;
        canvas->DispatchPrintCallback(aCallback);
        NS_ENSURE_STATE(weakFrame.IsAlive());
      }
    }
  }
  uint32_t doneCounter = 0;
  for (HTMLCanvasElement* canvas : mCurrentCanvasList) {
    if (canvas->IsPrintCallbackDone()) {
      doneCounter++;
    }
  }
  // If all canvas have finished rendering, return true, otherwise false.
  *aDone = doneCounter == mCurrentCanvasList.Length();

  return NS_OK;
}

void nsPageSequenceFrame::ResetPrintCanvasList() {
  for (int32_t i = mCurrentCanvasList.Length() - 1; i >= 0; i--) {
    HTMLCanvasElement* canvas = mCurrentCanvasList[i];
    canvas->ResetPrintCallback();
  }

  mCurrentCanvasList.Clear();
  mCurrentCanvasListSetup = false;
}

nsresult nsPageSequenceFrame::PrintNextSheet() {
  // Note: When print al the pages or a page range the printed page shows the
  // actual page number, when printing selection it prints the page number
  // starting with the first page of the selection. For example if the user has
  // a selection that starts on page 2 and ends on page 3, the page numbers when
  // print are 1 and then two (which is different than printing a page range,
  // where the page numbers would have been 2 and then 3)

  PrintedSheetFrame* currentSheetFrame = GetCurrentSheetFrame();
  if (!currentSheetFrame) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = NS_OK;

  nsDeviceContext* dc = PresContext()->DeviceContext();

  if (PresContext()->IsRootPaginatedDocument()) {
    if (!mCalledBeginPage) {
      // We must make sure BeginPage() has been called since some printing
      // backends can't give us a valid rendering context for a [physical]
      // page otherwise.
      PR_PL(("\n"));
      PR_PL(("***************** BeginPage *****************\n"));
      const gfx::IntSize sizeInPoints =
          currentSheetFrame->GetPrintTargetSizeInPoints(
              dc->AppUnitsPerPhysicalInch());
      rv = dc->BeginPage(sizeInPoints);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  PR_PL(("SeqFr::PrintNextSheet -> %p SheetIdx: %d", currentSheetFrame,
         mCurrentSheetIdx));

  // CreateRenderingContext can fail
  UniquePtr<gfxContext> gCtx = dc->CreateRenderingContext();
  NS_ENSURE_TRUE(gCtx, NS_ERROR_OUT_OF_MEMORY);

  nsRect drawingRect(nsPoint(0, 0), currentSheetFrame->GetSize());
  nsRegion drawingRegion(drawingRect);
  nsLayoutUtils::PaintFrame(gCtx.get(), currentSheetFrame, drawingRegion,
                            NS_RGBA(0, 0, 0, 0),
                            nsDisplayListBuilderMode::PaintForPrinting,
                            nsLayoutUtils::PaintFrameFlags::SyncDecodeImages);
  return rv;
}

nsresult nsPageSequenceFrame::DoPageEnd() {
  nsresult rv = NS_OK;
  if (PresContext()->IsRootPaginatedDocument()) {
    PR_PL(("***************** End Page (DoPageEnd) *****************\n"));
    rv = PresContext()->DeviceContext()->EndPage();
    // Fall through to clean up resources/state below even if EndPage failed.
  }

  ResetPrintCanvasList();
  mCalledBeginPage = false;

  mCurrentSheetIdx++;

  return rv;
}

static gfx::Matrix4x4 ComputePageSequenceTransform(const nsIFrame* aFrame,
                                                   float aAppUnitsPerPixel) {
  MOZ_ASSERT(aFrame->IsPageSequenceFrame());
  float scale =
      static_cast<const nsPageSequenceFrame*>(aFrame)->GetPrintPreviewScale();
  return gfx::Matrix4x4::Scaling(scale, scale, 1);
}

nsIFrame::ComputeTransformFunction nsPageSequenceFrame::GetTransformGetter()
    const {
  return ComputePageSequenceTransform;
}

void nsPageSequenceFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                           const nsDisplayListSet& aLists) {
  aBuilder->SetDisablePartialUpdates(true);
  DisplayBorderBackgroundOutline(aBuilder, aLists);

  nsDisplayList content(aBuilder);

  {
    // Clear clip state while we construct the children of the
    // nsDisplayTransform, since they'll be in a different coordinate system.
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    clipState.Clear();

    nsIFrame* child = PrincipalChildList().FirstChild();
    nsRect visible = aBuilder->GetVisibleRect();
    visible.ScaleInverseRoundOut(GetPrintPreviewScale());

    while (child) {
      if (child->InkOverflowRectRelativeToParent().Intersects(visible)) {
        nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
            aBuilder, child, visible - child->GetPosition(),
            visible - child->GetPosition());
        child->BuildDisplayListForStackingContext(aBuilder, &content);
        aBuilder->ResetMarkedFramesForDisplayList(this);
      }
      child = child->GetNextSibling();
    }
  }

  content.AppendNewToTop<nsDisplayTransform>(
      aBuilder, this, &content, content.GetBuildingRect(),
      nsDisplayTransform::WithTransformGetter);

  aLists.Content()->AppendToTop(&content);
}

//------------------------------------------------------------------------------
void nsPageSequenceFrame::SetPageNumberFormat(const nsAString& aFormatStr,
                                              bool aForPageNumOnly) {
  if (aForPageNumOnly) {
    mPageData.mPageNumFormat = aFormatStr;
  } else {
    mPageData.mPageNumAndTotalsFormat = aFormatStr;
  }
}

//------------------------------------------------------------------------------
void nsPageSequenceFrame::SetDateTimeStr(const nsAString& aDateTimeStr) {
  mPageData.mDateTimeStr = aDateTimeStr;
}
