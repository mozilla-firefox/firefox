/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsPageContentFrame.h"

#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/Document.h"
#include "nsCSSFrameConstructor.h"
#include "nsContentUtils.h"
#include "nsLayoutUtils.h"
#include "nsPageFrame.h"
#include "nsPageSequenceFrame.h"
#include "nsPresContext.h"

using namespace mozilla;

nsPageContentFrame* NS_NewPageContentFrame(
    PresShell* aPresShell, ComputedStyle* aStyle,
    already_AddRefed<const nsAtom> aPageName) {
  return new nsPageContentFrame(aStyle, aPresShell->GetPresContext(),
                                std::move(aPageName));
}

NS_IMPL_FRAMEARENA_HELPERS(nsPageContentFrame)

void nsPageContentFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aReflowOutput,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsPageContentFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(mPD, "Need a pointer to nsSharedPageData before reflow starts");

  if (GetPrevInFlow() && HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    nsresult rv =
        aPresContext->PresShell()->FrameConstructor()->ReplicateFixedFrames(
            this);
    if (NS_FAILED(rv)) {
      return;
    }
  }

  // Set our size up front, since some parts of reflow depend on it
  // being already set.  Note that the computed height may be
  // unconstrained; that's ok.  Consumers should watch out for that.
  const nsSize maxSize = aReflowInput.ComputedPhysicalSize();
  SetSize(maxSize);

  // Writing mode for the page content frame.
  const WritingMode pcfWM = aReflowInput.GetWritingMode();
  aReflowOutput.ISize(pcfWM) = aReflowInput.ComputedISize();
  if (aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE) {
    aReflowOutput.BSize(pcfWM) = aReflowInput.ComputedBSize();
  }
  aReflowOutput.SetOverflowAreasToDesiredBounds();

  // A PageContentFrame must always have one child: the canvas frame.
  // Resize our frame allowing it only to be as big as we are
  // XXX Pay attention to the page's border and padding...
  if (mFrames.NotEmpty()) {
    nsIFrame* const frame = mFrames.FirstChild();
    const WritingMode frameWM = frame->GetWritingMode();
    const LogicalSize logicalSize(frameWM, maxSize);
    LogicalSize availSize = logicalSize;
    if (aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow) {
      availSize.BSize(frameWM) = NS_UNCONSTRAINEDSIZE;
    }
    ReflowInput kidReflowInput(aPresContext, aReflowInput, frame, availSize);
    kidReflowInput.SetComputedBSize(logicalSize.BSize(frameWM));
    ReflowOutput kidReflowOutput(kidReflowInput);
    ReflowChild(frame, aPresContext, kidReflowOutput, kidReflowInput, 0, 0,
                ReflowChildFlags::Default, aStatus);

    // The document element's background should cover the entire canvas, so
    // take into account the combined area and any space taken up by
    // absolutely positioned elements
    nsMargin padding(0, 0, 0, 0);

    // XXXbz this screws up percentage padding (sets padding to zero
    // in the percentage padding case)
    frame->StylePadding()->GetPadding(padding);

    // This is for shrink-to-fit, and therefore we want to use the
    // scrollable overflow, since the purpose of shrink to fit is to
    // make the content that ought to be reachable (represented by the
    // scrollable overflow) fit in the page.
    //
    // For pdf.js documents, the shrink-to-fit ratio also takes y-axis overflow
    // into account. During a measuring reflow under an unconstrained
    // block-size, that overflow spans the entire unfragmented document with all
    // pages stacked. Therefore the computation produces an incorrect ratio that
    // tries to fit the entire document onto a single page. Since
    // mShrinkToFitRatio is only lowered (via std::min) and never reset, we skip
    // computing the shrink-to-fit ratio during a measuring reflow.
    if (!aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow &&
        frame->HasOverflowAreas()) {
      // The background covers the content area and padding area, so check
      // for children sticking outside the child frame's padding edge
      nscoord xmost = kidReflowOutput.ScrollableOverflow().XMost();
      if (xmost > kidReflowOutput.Width()) {
        const nscoord widthToFit =
            xmost + padding.right +
            kidReflowInput.mStyleBorder->GetComputedBorderWidth(eSideRight);
        const float ratio = float(maxSize.width) / float(widthToFit);
        NS_ASSERTION(ratio >= 0.0 && ratio < 1.0,
                     "invalid shrink-to-fit ratio");
        mPD->mShrinkToFitRatio = std::min(mPD->mShrinkToFitRatio, ratio);
      }
      // In the case of pdf.js documents, we also want to consider the height,
      // so that we don't clip the page in either axis if the aspect ratio of
      // the PDF doesn't match the destination.
      if (nsContentUtils::IsPDFJS(PresContext()->Document()->GetPrincipal())) {
        nscoord ymost = kidReflowOutput.ScrollableOverflow().YMost();
        if (ymost > kidReflowOutput.Height()) {
          const nscoord heightToFit =
              ymost + padding.bottom +
              kidReflowInput.mStyleBorder->GetComputedBorderWidth(eSideBottom);
          const float ratio = float(maxSize.height) / float(heightToFit);
          MOZ_ASSERT(ratio >= 0.0 && ratio < 1.0);
          mPD->mShrinkToFitRatio = std::min(mPD->mShrinkToFitRatio, ratio);
        }

        // pdf.js pages should never overflow given the scaling above.
        // nsPrintJob::SetupToPrintContent ignores some ratios close to 1.0
        // though and doesn't reflow us again in that case, so we need to clear
        // the overflow area here in case that happens. (bug 1689789)
        frame->ClearOverflowRects();
        kidReflowOutput.mOverflowAreas = aReflowOutput.mOverflowAreas;
      }
    }

    // Place and size the child
    FinishReflowChild(frame, aPresContext, kidReflowOutput, &kidReflowInput, 0,
                      0, ReflowChildFlags::Default);

    NS_ASSERTION(aPresContext->IsDynamic() || !aStatus.IsFullyComplete() ||
                     !frame->GetNextInFlow(),
                 "bad child flow list");

    aReflowOutput.mOverflowAreas.UnionWith(kidReflowOutput.mOverflowAreas);
  }

  FinishAndStoreOverflow(&aReflowOutput);

  // Reflow any fixed-pos children. Note that we don't need to call
  // PrepareAbsoluteFrames() because the fixed pos frames cannot split.
  nsReflowStatus fixedStatus;
  if (auto* absCB = GetAbsoluteContainingBlock();
      absCB && absCB->HasAbsoluteFrames()) {
    // The containing block for the fixed-pos children is formed by our padding
    // edge.
    const auto wm = GetWritingMode();
    LogicalRect cbRect(wm, LogicalPoint(wm), aReflowOutput.Size(wm));
    cbRect.Deflate(wm, GetLogicalUsedBorder(wm).ApplySkipSides(
                           PreReflowBlockLevelLogicalSkipSides()));

    // XXX: To optimize the performance, set the flags only when the CB width or
    // height actually changes.
    AbsPosReflowFlags flags{AbsPosReflowFlag::CBWidthChanged,
                            AbsPosReflowFlag::CBHeightChanged};

    // PageContentFrame replicates fixed-pos children, so we really don't want
    // them contributing to overflow areas; otherwise we'll create new pages ad
    // infinitum if one of them overflows the page.
    absCB->Reflow(this, aPresContext, aReflowInput, fixedStatus,
                  cbRect.GetPhysicalRect(wm, aReflowOutput.PhysicalSize()),
                  flags,
                  /* aOverflowAreas */ nullptr);
  }
  NS_ASSERTION(fixedStatus.IsComplete(),
               "fixed frames can be truncated, but not incomplete");

  if (StaticPrefs::layout_display_list_improve_fragmentation() &&
      mFrames.NotEmpty() &&
      !aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow) {
    auto* const previous =
        static_cast<nsPageContentFrame*>(GetPrevContinuation());
    const nscoord previousPageOverflow =
        previous ? previous->mRemainingOverflow : 0;
    const nsSize containerSize(aReflowInput.AvailableWidth(),
                               aReflowInput.AvailableHeight());
    const nscoord pageBSize = GetLogicalRect(containerSize).BSize(pcfWM);
    const nscoord overflowBSize =
        LogicalRect(pcfWM, ScrollableOverflowRect(), GetSize()).BEnd(pcfWM);
    const nscoord currentPageOverflow = overflowBSize - pageBSize;
    nscoord remainingOverflow =
        std::max(currentPageOverflow, previousPageOverflow - pageBSize);

    if (aStatus.IsFullyComplete() && remainingOverflow > 0) {
      // If we have ScrollableOverflow off the end of our page, then we report
      // ourselves as overflow-incomplete in order to produce an additional
      // content-less page, which we expect to draw our overflow on our behalf.
      aStatus.SetOverflowIncomplete();
    }

    mRemainingOverflow = std::max(remainingOverflow, 0);
  }
}

using PageAndOffset = std::pair<nsPageContentFrame*, nscoord>;

// Returns the previous continuation PageContentFrames that have overflow areas,
// and their offsets to the top of the given PageContentFrame |aPage|. Since the
// iteration is done backwards, the returned pages are arranged in descending
// order of page number.
static nsTArray<PageAndOffset> GetPreviousPagesWithOverflow(
    nsPageContentFrame* aPage) {
  nsTArray<PageAndOffset> pages(8);

  auto GetPreviousPageContentFrame = [](nsPageContentFrame* aPageCF) {
    nsIFrame* prevCont = aPageCF->GetPrevContinuation();
    MOZ_ASSERT(!prevCont || prevCont->IsPageContentFrame(),
               "Expected nsPageContentFrame or nullptr");

    return static_cast<nsPageContentFrame*>(prevCont);
  };

  nsPageContentFrame* pageCF = aPage;
  // The collective height of all prev-continuations we've traversed so far:
  nscoord offsetToCurrentPageBStart = 0;
  const auto wm = pageCF->GetWritingMode();
  while ((pageCF = GetPreviousPageContentFrame(pageCF))) {
    offsetToCurrentPageBStart += pageCF->BSize(wm);

    if (pageCF->HasOverflowAreas()) {
      pages.EmplaceBack(pageCF, offsetToCurrentPageBStart);
    }
  }

  return pages;
}

static void BuildPreviousPageOverflow(nsDisplayListBuilder* aBuilder,
                                      nsPageFrame* aPageFrame,
                                      nsPageContentFrame* aCurrentPageCF,
                                      const nsDisplayListSet& aLists) {
  const auto previousPagesAndOffsets =
      GetPreviousPagesWithOverflow(aCurrentPageCF);

  const auto wm = aCurrentPageCF->GetWritingMode();
  for (const PageAndOffset& pair : Reversed(previousPagesAndOffsets)) {
    auto* prevPageCF = pair.first;
    const nscoord offsetToCurrentPageBStart = pair.second;
    // Only scrollable overflow create new pages, not ink overflow.
    const LogicalRect scrollableOverflow(
        wm, prevPageCF->ScrollableOverflowRectRelativeToSelf(),
        prevPageCF->GetSize());
    const auto remainingOverflow =
        scrollableOverflow.BEnd(wm) - offsetToCurrentPageBStart;
    if (remainingOverflow <= 0) {
      continue;
    }

    // This rect represents the piece of prevPageCF's overflow that ends up on
    // the current pageContentFrame (in prevPageCF's coordinate system).
    // Note that we use InkOverflow here since this is for painting.
    LogicalRect overflowRect(wm, prevPageCF->InkOverflowRectRelativeToSelf(),
                             prevPageCF->GetSize());
    overflowRect.BStart(wm) = offsetToCurrentPageBStart;
    overflowRect.BSize(wm) = std::min(remainingOverflow, prevPageCF->BSize(wm));

    {
      // Convert the overflowRect to the coordinate system of aPageFrame, and
      // set it as the visible rect for display list building.
      const nsRect visibleRect =
          overflowRect.GetPhysicalRect(wm, prevPageCF->GetSize()) +
          prevPageCF->GetOffsetTo(aPageFrame);
      nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
          aBuilder, aPageFrame, visibleRect, visibleRect);

      // This part is tricky. Because display items are positioned based on the
      // frame tree, building a display list for the previous page yields
      // display items that are outside of the current page bounds.
      // To fix that, an additional reference frame offset is added, which
      // shifts the display items down (block axis) as if the current and
      // previous page were one long page in the same coordinate system.
      const nsSize containerSize = aPageFrame->GetSize();
      LogicalPoint pageOffset(wm, aCurrentPageCF->GetOffsetTo(prevPageCF),
                              containerSize);
      pageOffset.B(wm) -= offsetToCurrentPageBStart;
      buildingForChild.SetAdditionalOffset(
          pageOffset.GetPhysicalPoint(wm, containerSize));

      aPageFrame->BuildDisplayListForChild(aBuilder, prevPageCF, aLists);
    }
  }
}

/**
 * Remove all leaf display items that are not for descendants of
 * aBuilder->GetReferenceFrame() from aList.
 * @param aPage the page we're constructing the display list for
 * @param aList the list that is modified in-place
 */
static void PruneDisplayListForExtraPage(nsDisplayListBuilder* aBuilder,
                                         nsPageFrame* aPage,
                                         nsDisplayList* aList) {
  for (nsDisplayItem* i : aList->TakeItems()) {
    if (!i) {
      break;
    }
    nsDisplayList* subList = i->GetSameCoordinateSystemChildren();
    if (subList) {
      PruneDisplayListForExtraPage(aBuilder, aPage, subList);
      i->UpdateBounds(aBuilder);
    } else {
      nsIFrame* f = i->Frame();
      if (!nsLayoutUtils::IsProperAncestorFrameCrossDocInProcess(aPage, f)) {
        // We're throwing this away so call its destructor now. The memory
        // is owned by aBuilder which destroys all items at once.
        i->Destroy(aBuilder);
        continue;
      }
    }
    aList->AppendToTop(i);
  }
}

static void BuildDisplayListForExtraPage(nsDisplayListBuilder* aBuilder,
                                         nsPageFrame* aPage,
                                         nsIFrame* aExtraPage,
                                         nsDisplayList* aList) {
  // The only content in aExtraPage we care about is out-of-flow content from
  // aPage, whose placeholders have occurred in aExtraPage. If
  // NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO is not set, then aExtraPage has
  // no such content.
  if (!aExtraPage->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO)) {
    return;
  }
  nsDisplayList list(aBuilder);
  aExtraPage->BuildDisplayListForStackingContext(aBuilder, &list);
  PruneDisplayListForExtraPage(aBuilder, aPage, &list);
  aList->AppendToTop(&list);
}

static gfx::Matrix4x4 ComputePageContentTransform(const nsIFrame* aFrame,
                                                  float aAppUnitsPerPixel) {
  float scale = aFrame->PresContext()->GetPageScale();
  return gfx::Matrix4x4::Scaling(scale, scale, 1);
}

nsIFrame::ComputeTransformFunction nsPageContentFrame::GetTransformGetter()
    const {
  return ComputePageContentTransform;
}

void nsPageContentFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  MOZ_ASSERT(GetParent());
  MOZ_ASSERT(GetParent()->IsPageFrame());
  auto* pageFrame = static_cast<nsPageFrame*>(GetParent());

  if (auto pageNum = aBuilder->GetBuildingPageNum()) {
    // We're an extra page, avoid building duplicate OOFs that are going to be
    // built already.
    nsDisplayListBuilder::AutoPageNumberSetter p(
        aBuilder, pageNum, /* aAvoidBuildingDuplicateOofs = */ true);
    return mozilla::ViewportFrame::BuildDisplayList(aBuilder, aLists);
  }

  nsDisplayListCollection set(aBuilder);

  nsDisplayList content(aBuilder);
  {
    nsDisplayListBuilder::AutoPageNumberSetter p(aBuilder,
                                                 pageFrame->GetPageNum());
    NS_ASSERTION(!aBuilder->AvoidBuildingDuplicateOofs(),
                 "Too many pages to handle OOFs");
    const nsRect clipRect(aBuilder->ToReferenceFrame(this), GetSize());
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);

    // Overwrite current clip, since we're going to wrap in a transform and the
    // current clip is no longer meaningful.
    clipState.Clear();
    clipState.ClipContentDescendants(clipRect);

    if (StaticPrefs::layout_display_list_improve_fragmentation() &&
        !aBuilder->AvoidBuildingDuplicateOofs()) {
      BuildPreviousPageOverflow(aBuilder, pageFrame, this, set);
    }
    mozilla::ViewportFrame::BuildDisplayList(aBuilder, set);

    set.SerializeWithCorrectZOrder(&content, GetContent());

    // We may need to paint out-of-flow frames whose placeholders are on other
    // pages. Add those pages to our display list. Note that out-of-flow frames
    // can't be placed after their placeholders so
    // we don't have to process earlier pages. The display lists for
    // these extra pages are pruned so that only display items for the
    // page we currently care about (which we would have reached by
    // following placeholders to their out-of-flows) end up on the list.
    //
    // Stacking context frames that wrap content on their normal page,
    // as well as OOF content for this page will have their container
    // items duplicated. We tell the builder to include our page number
    // in the unique key for any extra page items so that they can be
    // differentiated from the ones created on the normal page.
    if (!aBuilder->AvoidBuildingDuplicateOofs()) {
      const nsRect overflowRect = ScrollableOverflowRectRelativeToSelf();
      // The static_cast here is technically unnecessary, but it helps
      // devirtualize the GetNextContinuation() function call if pcf has a
      // concrete type (with an inherited `final` GetNextContinuation() impl).
      auto* pageCF = this;
      while ((pageCF = static_cast<nsPageContentFrame*>(
                  pageCF->GetNextContinuation()))) {
        nsRect childVisible = overflowRect + GetOffsetTo(pageCF);

        nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
            aBuilder, pageCF, childVisible, childVisible);
        BuildDisplayListForExtraPage(aBuilder, pageFrame, pageCF, &content);
      }
    }
  }

  content.AppendNewToTop<nsDisplayTransform>(
      aBuilder, this, &content, content.GetBuildingRect(),
      nsDisplayTransform::WithTransformGetter);

  aLists.Content()->AppendToTop(&content);
}

void nsPageContentFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  MOZ_ASSERT(mFrames.FirstChild(),
             "pageContentFrame must have a canvasFrame child");
  aResult.AppendElement(mFrames.FirstChild());
}

void nsPageContentFrame::EnsurePageName() {
  if (mPageName) {
    return;
  }
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_FIRST_REFLOW),
             "Should only have been called on first reflow");

  MOZ_ASSERT(!GetPrevInFlow(),
             "Only the first page should initially have a null page name.");
  // This was the first page, we need to find our own page name and then set
  // our computed style based on that.
  mPageName = ComputePageValue();

  MOZ_ASSERT(mPageName, "Page name should never be null");
  // Resolve the computed style given this page-name and the :first pseudo.
  RefPtr<ComputedStyle> pageContentPseudoStyle =
      PresShell()->StyleSet()->ResolvePageContentStyle(
          mPageName, StylePagePseudoClassFlags::FIRST);
  SetComputedStyleWithoutNotification(pageContentPseudoStyle);
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsPageContentFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"PageContent"_ns, aResult);
}
void nsPageContentFrame::ExtraContainerFrameInfo(nsACString& aTo, bool) const {
  if (mPageName) {
    aTo += " [page=";
    aTo += nsAtomCString(mPageName);
    aTo += "]";
  }
}
#endif
