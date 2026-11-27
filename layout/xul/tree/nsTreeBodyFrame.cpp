/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTreeBodyFrame.h"

#include <algorithm>

#include "ScrollbarActivity.h"
#include "SimpleXULLeafFrame.h"
#include "gfxContext.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/Likely.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/Try.h"
#include "mozilla/dom/CustomEvent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/TreeColumnBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/intl/Segmenter.h"
#include "nsCOMPtr.h"
#include "nsCSSRendering.h"
#include "nsComponentManagerUtils.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIFrameInlines.h"
#include "nsITreeView.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsStyleConsts.h"
#include "nsTreeContentView.h"
#include "nsTreeImageListener.h"
#include "nsTreeSelection.h"
#include "nsTreeUtils.h"
#include "nsWidgetsCID.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#  include "nsIWritablePropertyBag2.h"
#endif
#include "nsBidiUtils.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::layout;

enum CroppingStyle { CropNone, CropLeft, CropRight, CropCenter, CropAuto };

// FIXME: Maybe unify with MiddleCroppingBlockFrame?
static void CropStringForWidth(nsAString& aText, gfxContext& aRenderingContext,
                               nsFontMetrics& aFontMetrics, nscoord aWidth,
                               CroppingStyle aCropType) {
  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  // See if the width is even smaller than the ellipsis
  // If so, clear the text completely.
  const nsDependentString& kEllipsis = nsContentUtils::GetLocalizedEllipsis();
  aFontMetrics.SetTextRunRTL(false);
  nscoord ellipsisWidth =
      nsLayoutUtils::AppUnitWidthOfString(kEllipsis, aFontMetrics, drawTarget);

  if (ellipsisWidth > aWidth) {
    aText.Truncate(0);
    return;
  }
  if (ellipsisWidth == aWidth) {
    aText.Assign(kEllipsis);
    return;
  }

  // We will be drawing an ellipsis, thank you very much.
  // Subtract out the required width of the ellipsis.
  // This is the total remaining width we have to play with.
  aWidth -= ellipsisWidth;

  using mozilla::intl::GraphemeClusterBreakIteratorUtf16;
  using mozilla::intl::GraphemeClusterBreakReverseIteratorUtf16;

  // Now we crop. This is quite basic: it will not be really accurate in the
  // presence of complex scripts with contextual shaping, etc., as it measures
  // each grapheme cluster in isolation, not in its proper context.
  switch (aCropType) {
    case CropAuto:
    case CropNone:
    case CropRight: {
      const Span text(aText);
      GraphemeClusterBreakIteratorUtf16 iter(text);
      uint32_t pos = 0;
      nscoord totalWidth = 0;

      while (Maybe<uint32_t> nextPos = iter.Next()) {
        const nscoord charWidth = nsLayoutUtils::AppUnitWidthOfString(
            text.FromTo(pos, *nextPos), aFontMetrics, drawTarget);
        if (totalWidth + charWidth > aWidth) {
          break;
        }
        pos = *nextPos;
        totalWidth += charWidth;
      }

      if (pos < aText.Length()) {
        aText.Replace(pos, aText.Length() - pos, kEllipsis);
      }
    } break;

    case CropLeft: {
      const Span text(aText);
      GraphemeClusterBreakReverseIteratorUtf16 iter(text);
      uint32_t pos = text.Length();
      nscoord totalWidth = 0;

      // nextPos is decreasing since we use a reverse iterator.
      while (Maybe<uint32_t> nextPos = iter.Next()) {
        const nscoord charWidth = nsLayoutUtils::AppUnitWidthOfString(
            text.FromTo(*nextPos, pos), aFontMetrics, drawTarget);
        if (totalWidth + charWidth > aWidth) {
          break;
        }

        pos = *nextPos;
        totalWidth += charWidth;
      }

      if (pos > 0) {
        aText.Replace(0, pos, kEllipsis);
      }
    } break;

    case CropCenter: {
      const Span text(aText);
      nscoord totalWidth = 0;
      GraphemeClusterBreakIteratorUtf16 leftIter(text);
      GraphemeClusterBreakReverseIteratorUtf16 rightIter(text);
      uint32_t leftPos = 0;
      uint32_t rightPos = text.Length();

      while (leftPos < rightPos) {
        Maybe<uint32_t> nextPos = leftIter.Next();
        nscoord charWidth = nsLayoutUtils::AppUnitWidthOfString(
            text.FromTo(leftPos, *nextPos), aFontMetrics, drawTarget);
        if (totalWidth + charWidth > aWidth) {
          break;
        }

        leftPos = *nextPos;
        totalWidth += charWidth;

        if (leftPos >= rightPos) {
          break;
        }

        nextPos = rightIter.Next();
        charWidth = nsLayoutUtils::AppUnitWidthOfString(
            text.FromTo(*nextPos, rightPos), aFontMetrics, drawTarget);
        if (totalWidth + charWidth > aWidth) {
          break;
        }

        rightPos = *nextPos;
        totalWidth += charWidth;
      }

      if (leftPos < rightPos) {
        aText.Replace(leftPos, rightPos - leftPos, kEllipsis);
      }
    } break;
  }
}

nsTreeImageCacheEntry::nsTreeImageCacheEntry() = default;
nsTreeImageCacheEntry::nsTreeImageCacheEntry(imgIRequest* aRequest,
                                             nsTreeImageListener* aListener)
    : request(aRequest), listener(aListener) {}
nsTreeImageCacheEntry::~nsTreeImageCacheEntry() = default;

static void DoCancelImageCacheEntry(const nsTreeImageCacheEntry& aEntry,
                                    nsPresContext* aPc) {
  // If our imgIRequest object was registered with the refresh driver
  // then we need to deregister it.
  aEntry.listener->ClearFrame();
  nsLayoutUtils::DeregisterImageRequest(aPc, aEntry.request, nullptr);
  aEntry.request->UnlockImage();
  aEntry.request->CancelAndForgetObserver(NS_BINDING_ABORTED);
}

// Function that cancels all the image requests in our cache.
void nsTreeBodyFrame::CancelImageRequests() {
  auto* pc = PresContext();
  for (const nsTreeImageCacheEntry& entry : mImageCache.Values()) {
    DoCancelImageCacheEntry(entry, pc);
  }
}

//
// NS_NewTreeFrame
//
// Creates a new tree frame
//
nsIFrame* NS_NewTreeBodyFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsTreeBodyFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTreeBodyFrame)

NS_QUERYFRAME_HEAD(nsTreeBodyFrame)
  NS_QUERYFRAME_ENTRY(nsIScrollbarMediator)
  NS_QUERYFRAME_ENTRY(nsTreeBodyFrame)
NS_QUERYFRAME_TAIL_INHERITING(SimpleXULLeafFrame)

// Constructor
nsTreeBodyFrame::nsTreeBodyFrame(ComputedStyle* aStyle,
                                 nsPresContext* aPresContext)
    : SimpleXULLeafFrame(aStyle, aPresContext, kClassID),
      mTopRowIndex(0),
      mPageLength(0),
      mHorzPosition(0),
      mOriginalHorzWidth(-1),
      mHorzWidth(0),
      mRowHeight(0),
      mIndentation(0),
      mUpdateBatchNest(0),
      mRowCount(0),
      mMouseOverRow(-1),
      mFocused(false),
      mHasFixedRowCount(false),
      mVerticalOverflow(false),
      mReflowCallbackPosted(false),
      mCheckingOverflow(false) {
  mColumns = MakeRefPtr<nsTreeColumns>(this);
}

// Destructor
nsTreeBodyFrame::~nsTreeBodyFrame() { CancelImageRequests(); }

static void GetBorderPadding(ComputedStyle* aStyle, nsMargin& aMargin) {
  aMargin.SizeTo(0, 0, 0, 0);
  aStyle->StylePadding()->GetPadding(aMargin);
  aMargin += aStyle->StyleBorder()->GetComputedBorder();
}

static void AdjustForBorderPadding(ComputedStyle* aStyle, nsRect& aRect) {
  nsMargin borderPadding(0, 0, 0, 0);
  GetBorderPadding(aStyle, borderPadding);
  aRect.Deflate(borderPadding);
}

void nsTreeBodyFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                           nsIFrame* aPrevInFlow) {
  SimpleXULLeafFrame::Init(aContent, aParent, aPrevInFlow);

  mIndentation = GetIndentation();
  mRowHeight = GetRowHeight();

  // Call GetBaseElement so that mTree is assigned.
  RefPtr<XULTreeElement> tree(GetBaseElement());
  if (MOZ_LIKELY(tree)) {
    nsAutoString rows;
    if (tree->GetAttr(nsGkAtoms::rows, rows)) {
      nsresult err;
      mPageLength = rows.ToInteger(&err);
      mHasFixedRowCount = true;
    }
  }

  if (PresContext()->UseOverlayScrollbars()) {
    mScrollbarActivity =
        new ScrollbarActivity(static_cast<nsIScrollbarMediator*>(this));
  }
}

void nsTreeBodyFrame::Destroy(DestroyContext& aContext) {
  if (mScrollbarActivity) {
    mScrollbarActivity->Destroy();
    mScrollbarActivity = nullptr;
  }

  mScrollEvent.Revoke();
  // Make sure we cancel any posted callbacks.
  if (mReflowCallbackPosted) {
    PresShell()->CancelReflowCallback(this);
    mReflowCallbackPosted = false;
  }

  if (mColumns) {
    mColumns->SetTree(nullptr);
  }

  RefPtr tree = mTree;

  if (nsCOMPtr<nsITreeView> view = std::move(mView)) {
    nsCOMPtr<nsITreeSelection> sel;
    view->GetSelection(getter_AddRefs(sel));
    if (sel) {
      sel->SetTree(nullptr);
    }
    view->SetTree(nullptr);
  }

  // Make this call now because view->SetTree can run js which can undo this
  // call.
  if (tree) {
    tree->BodyDestroyed(mTopRowIndex);
  }
  if (mTree && mTree != tree) {
    mTree->BodyDestroyed(mTopRowIndex);
  }

  SimpleXULLeafFrame::Destroy(aContext);
}

void nsTreeBodyFrame::EnsureView() {
  if (mView) {
    return;
  }

  if (PresShell()->IsReflowLocked()) {
    if (!mReflowCallbackPosted) {
      mReflowCallbackPosted = true;
      PresShell()->PostReflowCallback(this);
    }
    return;
  }

  AutoWeakFrame weakFrame(this);

  RefPtr<XULTreeElement> tree = GetBaseElement();
  if (!tree) {
    return;
  }
  nsCOMPtr<nsITreeView> treeView = tree->GetView();
  if (!treeView || !weakFrame.IsAlive()) {
    return;
  }
  int32_t rowIndex = tree->GetCachedTopVisibleRow();

  // Set our view.
  SetView(treeView);
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());

  // Scroll to the given row.
  // XXX is this optimal if we haven't laid out yet?
  ScrollToRow(rowIndex);
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());
}

void nsTreeBodyFrame::ManageReflowCallback() {
  const nscoord horzWidth = mRect.width;
  if (!mReflowCallbackPosted) {
    if (!mLastReflowRect || !mLastReflowRect->IsEqualEdges(mRect) ||
        mHorzWidth != horzWidth) {
      PresShell()->PostReflowCallback(this);
      mReflowCallbackPosted = true;
      mOriginalHorzWidth = mHorzWidth;
    }
  } else if (mHorzWidth != horzWidth && mOriginalHorzWidth == horzWidth) {
    // FIXME(emilio): This doesn't seem sound to me, if the rect changes in the
    // block axis.
    PresShell()->CancelReflowCallback(this);
    mReflowCallbackPosted = false;
    mOriginalHorzWidth = -1;
  }
  mLastReflowRect = Some(mRect);
  mHorzWidth = horzWidth;
}

IntrinsicSize nsTreeBodyFrame::GetIntrinsicSize() {
  IntrinsicSize intrinsicSize;
  if (mHasFixedRowCount) {
    intrinsicSize.BSize(GetWritingMode()).emplace(mRowHeight * mPageLength);
  }
  return intrinsicSize;
}

void nsTreeBodyFrame::DidReflow(nsPresContext* aPresContext,
                                const ReflowInput* aReflowInput) {
  ManageReflowCallback();
  SimpleXULLeafFrame::DidReflow(aPresContext, aReflowInput);
}

bool nsTreeBodyFrame::ReflowFinished() {
  if (!mView) {
    AutoWeakFrame weakFrame(this);
    EnsureView();
    NS_ENSURE_TRUE(weakFrame.IsAlive(), false);
  }
  if (mView) {
    CalcInnerBox();
    ScrollParts parts = GetScrollParts();
    mHorzWidth = mRect.width;
    if (!mHasFixedRowCount) {
      mPageLength =
          (mRowHeight > 0) ? (mInnerBox.height / mRowHeight) : mRowCount;
    }

    int32_t lastPageTopRow = std::max(0, mRowCount - mPageLength);
    if (mTopRowIndex > lastPageTopRow) {
      ScrollToRowInternal(parts, lastPageTopRow);
    }

    XULTreeElement* treeContent = GetBaseElement();
    if (treeContent && treeContent->AttrValueIs(
                           kNameSpaceID_None, nsGkAtoms::keepcurrentinview,
                           nsGkAtoms::_true, eCaseMatters)) {
      // make sure that the current selected item is still
      // visible after the tree changes size.
      if (nsCOMPtr<nsITreeSelection> sel = GetSelection()) {
        int32_t currentIndex;
        sel->GetCurrentIndex(&currentIndex);
        if (currentIndex != -1) {
          EnsureRowIsVisibleInternal(parts, currentIndex);
        }
      }
    }

    if (!FullScrollbarsUpdate(false)) {
      return false;
    }
  }

  mReflowCallbackPosted = false;
  return false;
}

void nsTreeBodyFrame::ReflowCallbackCanceled() {
  mReflowCallbackPosted = false;
}

nsresult nsTreeBodyFrame::GetView(nsITreeView** aView) {
  *aView = nullptr;
  AutoWeakFrame weakFrame(this);
  EnsureView();
  NS_ENSURE_STATE(weakFrame.IsAlive());
  NS_IF_ADDREF(*aView = mView);
  return NS_OK;
}

nsresult nsTreeBodyFrame::SetView(nsITreeView* aView) {
  if (aView == mView) {
    return NS_OK;
  }

  // First clear out the old view.
  nsCOMPtr<nsITreeView> oldView = std::move(mView);
  if (oldView) {
    AutoWeakFrame weakFrame(this);

    nsCOMPtr<nsITreeSelection> sel;
    oldView->GetSelection(getter_AddRefs(sel));
    if (sel) {
      sel->SetTree(nullptr);
    }
    oldView->SetTree(nullptr);

    NS_ENSURE_STATE(weakFrame.IsAlive());

    // Only reset the top row index and delete the columns if we had an old
    // non-null view.
    mTopRowIndex = 0;
  }

  // Tree, meet the view.
  mView = aView;

  // Changing the view causes us to refetch our data.  This will
  // necessarily entail a full invalidation of the tree.
  Invalidate();

  RefPtr<XULTreeElement> treeContent = GetBaseElement();
  if (treeContent) {
#ifdef ACCESSIBILITY
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->TreeViewChanged(PresContext()->GetPresShell(), treeContent,
                                  mView);
    }
#endif  // #ifdef ACCESSIBILITY
    FireDOMEvent(u"TreeViewChanged"_ns, treeContent);
  }

  if (aView) {
    // Give the view a new empty selection object to play with, but only if it
    // doesn't have one already.
    nsCOMPtr<nsITreeSelection> sel;
    aView->GetSelection(getter_AddRefs(sel));
    if (sel) {
      sel->SetTree(treeContent);
    } else {
      NS_NewTreeSelection(treeContent, getter_AddRefs(sel));
      aView->SetSelection(sel);
    }

    // View, meet the tree.
    AutoWeakFrame weakFrame(this);
    aView->SetTree(treeContent);
    NS_ENSURE_STATE(weakFrame.IsAlive());
    aView->GetRowCount(&mRowCount);

    if (!PresShell()->IsReflowLocked()) {
      // The scrollbar will need to be updated.
      FullScrollbarsUpdate(false);
    } else if (!mReflowCallbackPosted) {
      mReflowCallbackPosted = true;
      PresShell()->PostReflowCallback(this);
    }
  }

  return NS_OK;
}

already_AddRefed<nsITreeSelection> nsTreeBodyFrame::GetSelection() const {
  nsCOMPtr<nsITreeSelection> sel;
  if (nsCOMPtr<nsITreeView> view = GetExistingView()) {
    view->GetSelection(getter_AddRefs(sel));
  }
  return sel.forget();
}

nsresult nsTreeBodyFrame::SetFocused(bool aFocused) {
  if (mFocused != aFocused) {
    mFocused = aFocused;
    if (nsCOMPtr<nsITreeSelection> sel = GetSelection()) {
      sel->InvalidateSelection();
    }
  }
  return NS_OK;
}

nsresult nsTreeBodyFrame::GetTreeBody(Element** aElement) {
  // NS_ASSERTION(mContent, "no content, see bug #104878");
  if (!mContent) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<Element> element = mContent->AsElement();
  element.forget(aElement);
  return NS_OK;
}

int32_t nsTreeBodyFrame::RowHeight() const {
  return nsPresContext::AppUnitsToIntCSSPixels(mRowHeight);
}

int32_t nsTreeBodyFrame::RowWidth() {
  return nsPresContext::AppUnitsToIntCSSPixels(mRect.width);
}

Maybe<CSSIntRegion> nsTreeBodyFrame::GetSelectionRegion() {
  if (!mView) {
    return Nothing();
  }

  AutoWeakFrame wf(this);
  nsCOMPtr<nsITreeSelection> selection = GetSelection();
  if (!selection || !wf.IsAlive()) {
    return Nothing();
  }

  nsIntRect rect = mRect.ToOutsidePixels(AppUnitsPerCSSPixel());

  nsIFrame* rootFrame = PresShell()->GetRootFrame();
  nsPoint origin = GetOffsetTo(rootFrame);

  CSSIntRegion region;

  // iterate through the visible rows and add the selected ones to the
  // drag region
  int32_t x = nsPresContext::AppUnitsToIntCSSPixels(origin.x);
  int32_t y = nsPresContext::AppUnitsToIntCSSPixels(origin.y);
  int32_t top = y;
  int32_t end = LastVisibleRow();
  int32_t rowHeight = nsPresContext::AppUnitsToIntCSSPixels(mRowHeight);
  for (int32_t i = mTopRowIndex; i <= end; i++) {
    bool isSelected;
    selection->IsSelected(i, &isSelected);
    if (isSelected) {
      region.OrWith(CSSIntRect(x, y, rect.width, rowHeight));
    }
    y += rowHeight;
  }

  // clip to the tree boundary in case one row extends past it
  region.AndWith(CSSIntRect(x, top, rect.width, rect.height));

  return Some(region);
}

nsresult nsTreeBodyFrame::Invalidate() {
  if (mUpdateBatchNest) {
    return NS_OK;
  }

  InvalidateFrame();

  return NS_OK;
}

nsresult nsTreeBodyFrame::InvalidateColumn(nsTreeColumn* aCol) {
  if (mUpdateBatchNest) {
    return NS_OK;
  }

  if (!aCol) {
    return NS_ERROR_INVALID_ARG;
  }

#ifdef ACCESSIBILITY
  if (GetAccService()) {
    FireInvalidateEvent(-1, -1, aCol, aCol);
  }
#endif  // #ifdef ACCESSIBILITY

  nsRect columnRect;
  nsresult rv = aCol->GetRect(this, mInnerBox.y, mInnerBox.height, &columnRect);
  NS_ENSURE_SUCCESS(rv, rv);

  // When false then column is out of view
  if (OffsetForHorzScroll(columnRect, true)) {
    InvalidateFrameWithRect(columnRect);
  }

  return NS_OK;
}

nsresult nsTreeBodyFrame::InvalidateRow(int32_t aIndex) {
  if (mUpdateBatchNest) {
    return NS_OK;
  }

#ifdef ACCESSIBILITY
  if (GetAccService()) {
    FireInvalidateEvent(aIndex, aIndex, nullptr, nullptr);
  }
#endif  // #ifdef ACCESSIBILITY

  aIndex -= mTopRowIndex;
  if (aIndex < 0 || aIndex > mPageLength) {
    return NS_OK;
  }

  nsRect rowRect(mInnerBox.x, mInnerBox.y + mRowHeight * aIndex,
                 mInnerBox.width, mRowHeight);
  InvalidateFrameWithRect(rowRect);

  return NS_OK;
}

nsresult nsTreeBodyFrame::InvalidateCell(int32_t aIndex, nsTreeColumn* aCol) {
  if (mUpdateBatchNest) {
    return NS_OK;
  }

#ifdef ACCESSIBILITY
  if (GetAccService()) {
    FireInvalidateEvent(aIndex, aIndex, aCol, aCol);
  }
#endif  // #ifdef ACCESSIBILITY

  aIndex -= mTopRowIndex;
  if (aIndex < 0 || aIndex > mPageLength) {
    return NS_OK;
  }

  if (!aCol) {
    return NS_ERROR_INVALID_ARG;
  }

  nsRect cellRect;
  nsresult rv = aCol->GetRect(this, mInnerBox.y + mRowHeight * aIndex,
                              mRowHeight, &cellRect);
  NS_ENSURE_SUCCESS(rv, rv);

  if (OffsetForHorzScroll(cellRect, true)) {
    InvalidateFrameWithRect(cellRect);
  }

  return NS_OK;
}

nsresult nsTreeBodyFrame::InvalidateRange(int32_t aStart, int32_t aEnd) {
  if (mUpdateBatchNest) {
    return NS_OK;
  }

  if (aStart == aEnd) {
    return InvalidateRow(aStart);
  }

  int32_t last = LastVisibleRow();
  if (aStart > aEnd || aEnd < mTopRowIndex || aStart > last) {
    return NS_OK;
  }

  if (aStart < mTopRowIndex) {
    aStart = mTopRowIndex;
  }

  if (aEnd > last) {
    aEnd = last;
  }

#ifdef ACCESSIBILITY
  if (GetAccService()) {
    int32_t end =
        mRowCount > 0 ? ((mRowCount <= aEnd) ? mRowCount - 1 : aEnd) : 0;
    FireInvalidateEvent(aStart, end, nullptr, nullptr);
  }
#endif  // #ifdef ACCESSIBILITY

  nsRect rangeRect(mInnerBox.x,
                   mInnerBox.y + mRowHeight * (aStart - mTopRowIndex),
                   mInnerBox.width, mRowHeight * (aEnd - aStart + 1));
  InvalidateFrameWithRect(rangeRect);

  return NS_OK;
}

static void FindScrollParts(nsIFrame* aCurrFrame,
                            nsTreeBodyFrame::ScrollParts* aResult) {
  if (nsScrollbarFrame* sf = do_QueryFrame(aCurrFrame)) {
    if (!sf->IsHorizontal() && !aResult->mVScrollbar) {
      aResult->mVScrollbar = sf;
    }
    // don't bother searching inside a scrollbar
    return;
  }

  nsIFrame* child = aCurrFrame->PrincipalChildList().FirstChild();
  while (child && !child->GetContent()->IsRootOfNativeAnonymousSubtree() &&
         !aResult->mVScrollbar) {
    FindScrollParts(child, aResult);
    child = child->GetNextSibling();
  }
}

nsTreeBodyFrame::ScrollParts nsTreeBodyFrame::GetScrollParts() {
  ScrollParts result;
  XULTreeElement* tree = GetBaseElement();
  if (nsIFrame* treeFrame = tree ? tree->GetPrimaryFrame() : nullptr) {
    // The way we do this, searching through the entire frame subtree, is pretty
    // dumb! We should know where these frames are.
    FindScrollParts(treeFrame, &result);
    if (result.mVScrollbar) {
      result.mVScrollbar->SetOverrideScrollbarMediator(this);
      result.mVScrollbarContent = result.mVScrollbar->GetContent()->AsElement();
    }
  }
  return result;
}

void nsTreeBodyFrame::UpdateScrollbars(const ScrollParts& aParts) {
  if (!aParts.mVScrollbar) {
    return;
  }
  CSSIntCoord rowHeightAsPixels =
      nsPresContext::AppUnitsToIntCSSPixels(mRowHeight);
  CSSIntCoord pos = mTopRowIndex * rowHeightAsPixels;
  if (!aParts.mVScrollbar->SetCurPos(pos)) {
    return;
  }
  if (mScrollbarActivity) {
    mScrollbarActivity->ActivityOccurred();
  }
}

void nsTreeBodyFrame::CheckOverflow(const ScrollParts& aParts) {
  bool verticalOverflowChanged = false;
  if (!mVerticalOverflow && mRowCount > mPageLength) {
    mVerticalOverflow = true;
    verticalOverflowChanged = true;
  } else if (mVerticalOverflow && mRowCount <= mPageLength) {
    mVerticalOverflow = false;
    verticalOverflowChanged = true;
  }

  if (!verticalOverflowChanged) {
    return;
  }

  AutoWeakFrame weakFrame(this);

  RefPtr<nsPresContext> presContext = PresContext();
  RefPtr<mozilla::PresShell> presShell = presContext->GetPresShell();
  nsCOMPtr<nsIContent> content = mContent;

  InternalScrollPortEvent event(
      true, mVerticalOverflow ? eScrollPortOverflow : eScrollPortUnderflow,
      nullptr);
  event.mOrient = InternalScrollPortEvent::eVertical;
  EventDispatcher::Dispatch(content, presContext, &event);

  // The synchronous event dispatch above can trigger reflow notifications.
  // Flush those explicitly now, so that we can guard against potential infinite
  // recursion. See bug 905909.
  if (!weakFrame.IsAlive()) {
    return;
  }
  NS_ASSERTION(!mCheckingOverflow,
               "mCheckingOverflow should not already be set");
  // Don't use AutoRestore since we want to not touch mCheckingOverflow if we
  // fail the weakFrame.IsAlive() check below
  mCheckingOverflow = true;
  presShell->FlushPendingNotifications(FlushType::Layout);
  if (!weakFrame.IsAlive()) {
    return;
  }
  mCheckingOverflow = false;
}

void nsTreeBodyFrame::InvalidateScrollbars(const ScrollParts& aParts) {
  if (mUpdateBatchNest || !mView || !aParts.mVScrollbar) {
    return;
  }
  nscoord rowHeightAsPixels = nsPresContext::AppUnitsToIntCSSPixels(mRowHeight);
  CSSIntCoord size = rowHeightAsPixels *
                     (mRowCount > mPageLength ? mRowCount - mPageLength : 0);
  CSSIntCoord pageincrement = mPageLength * rowHeightAsPixels;
  bool changed = false;
  changed |= aParts.mVScrollbar->SetMaxPos(size);
  changed |= aParts.mVScrollbar->SetPageIncrement(pageincrement);
  if (changed && mScrollbarActivity) {
    mScrollbarActivity->ActivityOccurred();
  }
}

// Takes client x/y in pixels, converts them to appunits, and converts into
// values relative to this nsTreeBodyFrame frame.
nsPoint nsTreeBodyFrame::AdjustClientCoordsToBoxCoordSpace(int32_t aX,
                                                           int32_t aY) {
  nsPoint point(nsPresContext::CSSPixelsToAppUnits(aX),
                nsPresContext::CSSPixelsToAppUnits(aY));

  nsPresContext* presContext = PresContext();
  point -= GetOffsetTo(presContext->GetPresShell()->GetRootFrame());

  // Adjust by the inner box coords, so that we're in the inner box's
  // coordinate space.
  point -= mInnerBox.TopLeft();
  return point;
}  // AdjustClientCoordsToBoxCoordSpace

int32_t nsTreeBodyFrame::GetRowAt(int32_t aX, int32_t aY) {
  if (!mView) {
    return 0;
  }

  nsPoint point = AdjustClientCoordsToBoxCoordSpace(aX, aY);

  // Check if the coordinates are above our visible space.
  if (point.y < 0) {
    return -1;
  }

  return GetRowAtInternal(point.x, point.y);
}

nsresult nsTreeBodyFrame::GetCellAt(int32_t aX, int32_t aY, int32_t* aRow,
                                    nsTreeColumn** aCol,
                                    nsACString& aChildElt) {
  if (!mView) {
    return NS_OK;
  }

  nsPoint point = AdjustClientCoordsToBoxCoordSpace(aX, aY);

  // Check if the coordinates are above our visible space.
  if (point.y < 0) {
    *aRow = -1;
    return NS_OK;
  }

  nsTreeColumn* col;
  PseudoStyleType child;
  GetCellAt(point.x, point.y, aRow, &col, &child);

  if (col) {
    NS_ADDREF(*aCol = col);
    if (child == PseudoStyleType::MozTreeCell) {
      aChildElt.AssignLiteral("cell");
    } else if (child == PseudoStyleType::MozTreeTwisty) {
      aChildElt.AssignLiteral("twisty");
    } else if (child == PseudoStyleType::MozTreeImage) {
      aChildElt.AssignLiteral("image");
    } else if (child == PseudoStyleType::MozTreeCellText) {
      aChildElt.AssignLiteral("text");
    }
  }

  return NS_OK;
}

//
// GetCoordsForCellItem
//
// Find the x/y location and width/height (all in PIXELS) of the given object
// in the given column.
//
// XXX IMPORTANT XXX:
// Hyatt says in the bug for this, that the following needs to be done:
// (1) You need to deal with overflow when computing cell rects.  See other
// column iteration examples... if you don't deal with this, you'll mistakenly
// extend the cell into the scrollbar's rect.
//
// (2) You are adjusting the cell rect by the *row" border padding.  That's
// wrong.  You need to first adjust a row rect by its border/padding, and then
// the cell rect fits inside the adjusted row rect.  It also can have
// border/padding as well as margins.  The vertical direction isn't that
// important, but you need to get the horizontal direction right.
//
// (3) GetImageSize() does not include margins (but it does include
// border/padding). You need to make sure to add in the image's margins as well.
//
nsresult nsTreeBodyFrame::GetCoordsForCellItem(int32_t aRow, nsTreeColumn* aCol,
                                               const nsACString& aElement,
                                               int32_t* aX, int32_t* aY,
                                               int32_t* aWidth,
                                               int32_t* aHeight) {
  *aX = 0;
  *aY = 0;
  *aWidth = 0;
  *aHeight = 0;

  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;
  nscoord currX = mInnerBox.x - mHorzPosition;

  // The Rect for the requested item.
  nsRect theRect;

  nsPresContext* presContext = PresContext();

  nsCOMPtr<nsITreeView> view = GetExistingView();

  for (nsTreeColumn* currCol = mColumns->GetFirstColumn(); currCol;
       currCol = currCol->GetNext()) {
    // The Rect for the current cell.
    nscoord colWidth;
#ifdef DEBUG
    nsresult rv =
#endif
        currCol->GetWidthInTwips(this, &colWidth);
    NS_ASSERTION(NS_SUCCEEDED(rv), "invalid column");

    nsRect cellRect(currX, mInnerBox.y + mRowHeight * (aRow - mTopRowIndex),
                    colWidth, mRowHeight);

    // Check the ID of the current column to see if it matches. If it doesn't
    // increment the current X value and continue to the next column.
    if (currCol != aCol) {
      currX += cellRect.width;
      continue;
    }
    // Now obtain the properties for our cell.
    PrefillPropertyArray(aRow, currCol);

    nsAutoString properties;
    view->GetCellProperties(aRow, currCol, properties);
    nsTreeUtils::TokenizeProperties(properties, mScratchArray);

    ComputedStyle* rowContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeRow);

    // We don't want to consider any of the decorations that may be present
    // on the current row, so we have to deflate the rect by the border and
    // padding and offset its left and top coordinates appropriately.
    AdjustForBorderPadding(rowContext, cellRect);

    ComputedStyle* cellContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeCell);

    constexpr auto cell = "cell"_ns;
    if (currCol->IsCycler() || cell.Equals(aElement)) {
      // If the current Column is a Cycler, then the Rect is just the cell - the
      // margins. Similarly, if we're just being asked for the cell rect,
      // provide it.

      theRect = cellRect;
      nsMargin cellMargin;
      cellContext->StyleMargin()->GetMargin(cellMargin);
      theRect.Deflate(cellMargin);
      break;
    }

    // Since we're not looking for the cell, and since the cell isn't a cycler,
    // we're looking for some subcomponent, and now we need to subtract the
    // borders and padding of the cell from cellRect so this does not
    // interfere with our computations.
    AdjustForBorderPadding(cellContext, cellRect);

    UniquePtr<gfxContext> rc =
        presContext->PresShell()->CreateReferenceRenderingContext();

    // Now we'll start making our way across the cell, starting at the edge of
    // the cell and proceeding until we hit the right edge. |cellX| is the
    // working X value that we will increment as we crawl from left to right.
    nscoord cellX = cellRect.x;
    nscoord remainWidth = cellRect.width;

    if (currCol->IsPrimary()) {
      // If the current Column is a Primary, then we need to take into account
      // the indentation and possibly a twisty.

      // The amount of indentation is the indentation width (|mIndentation|) by
      // the level.
      int32_t level;
      view->GetLevel(aRow, &level);
      if (!isRTL) {
        cellX += mIndentation * level;
      }
      remainWidth -= mIndentation * level;

      // Find the twisty rect by computing its size.
      nsRect imageRect;
      nsRect twistyRect(cellRect);
      ComputedStyle* twistyContext =
          GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);
      GetTwistyRect(aRow, currCol, imageRect, twistyRect, presContext,
                    twistyContext);

      if ("twisty"_ns.Equals(aElement)) {
        // If we're looking for the twisty Rect, just return the size
        theRect = twistyRect;
        break;
      }

      // Now we need to add in the margins of the twisty element, so that we
      // can find the offset of the next element in the cell.
      nsMargin twistyMargin;
      twistyContext->StyleMargin()->GetMargin(twistyMargin);
      twistyRect.Inflate(twistyMargin);

      // Adjust our working X value with the twisty width (image size, margins,
      // borders, padding.
      if (!isRTL) {
        cellX += twistyRect.width;
      }
    }

    // Cell Image
    ComputedStyle* imageContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeImage);

    nsRect imageSize = GetImageSize(aRow, currCol, false, imageContext);
    if ("image"_ns.Equals(aElement)) {
      theRect = imageSize;
      theRect.x = cellX;
      theRect.y = cellRect.y;
      break;
    }

    // Add in the margins of the cell image.
    nsMargin imageMargin;
    imageContext->StyleMargin()->GetMargin(imageMargin);
    imageSize.Inflate(imageMargin);

    // Increment cellX by the image width
    if (!isRTL) {
      cellX += imageSize.width;
    }

    // Cell Text
    nsAutoString cellText;
    view->GetCellText(aRow, currCol, cellText);
    // We're going to measure this text so we need to ensure bidi is enabled if
    // necessary
    CheckTextForBidi(cellText);

    // Create a scratch rect to represent the text rectangle, with the current
    // X and Y coords, and a guess at the width and height. The width is the
    // remaining width we have left to traverse in the cell, which will be the
    // widest possible value for the text rect, and the row height.
    nsRect textRect(cellX, cellRect.y, remainWidth, cellRect.height);

    // Measure the width of the text. If the width of the text is greater than
    // the remaining width available, then we just assume that the text has
    // been cropped and use the remaining rect as the text Rect. Otherwise,
    // we add in borders and padding to the text dimension and give that back.
    ComputedStyle* textContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeCellText);

    RefPtr<nsFontMetrics> fm =
        nsLayoutUtils::GetFontMetricsForComputedStyle(textContext, presContext);
    nscoord height = fm->MaxHeight();

    nsMargin textMargin;
    textContext->StyleMargin()->GetMargin(textMargin);
    textRect.Deflate(textMargin);

    // Center the text. XXX Obey vertical-align style prop?
    if (height < textRect.height) {
      textRect.y += (textRect.height - height) / 2;
      textRect.height = height;
    }

    nsMargin bp(0, 0, 0, 0);
    GetBorderPadding(textContext, bp);
    textRect.height += bp.top + bp.bottom;

    AdjustForCellText(cellText, aRow, currCol, *rc, *fm, textRect);

    theRect = textRect;
  }

  if (isRTL) {
    theRect.x = mInnerBox.width - theRect.x - theRect.width;
  }

  *aX = nsPresContext::AppUnitsToIntCSSPixels(theRect.x);
  *aY = nsPresContext::AppUnitsToIntCSSPixels(theRect.y);
  *aWidth = nsPresContext::AppUnitsToIntCSSPixels(theRect.width);
  *aHeight = nsPresContext::AppUnitsToIntCSSPixels(theRect.height);

  return NS_OK;
}

int32_t nsTreeBodyFrame::GetRowAtInternal(nscoord aX, nscoord aY) {
  if (mRowHeight <= 0) {
    return -1;
  }

  // Now just mod by our total inner box height and add to our top row index.
  int32_t row = (aY / mRowHeight) + mTopRowIndex;

  // Check if the coordinates are below our visible space (or within our visible
  // space but below any row).
  if (row > mTopRowIndex + mPageLength || row >= mRowCount) {
    return -1;
  }

  return row;
}

void nsTreeBodyFrame::CheckTextForBidi(nsAutoString& aText) {
  // We could check to see whether the prescontext already has bidi enabled,
  // but usually it won't, so it's probably faster to avoid the call to
  // GetPresContext() when it's not needed.
  if (HasRTLChars(aText)) {
    PresContext()->SetBidiEnabled();
  }
}

void nsTreeBodyFrame::AdjustForCellText(nsAutoString& aText, int32_t aRowIndex,
                                        nsTreeColumn* aColumn,
                                        gfxContext& aRenderingContext,
                                        nsFontMetrics& aFontMetrics,
                                        nsRect& aTextRect) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  nscoord maxWidth = aTextRect.width;
  bool widthIsGreater = nsLayoutUtils::StringWidthIsGreaterThan(
      aText, aFontMetrics, drawTarget, maxWidth);

  nsCOMPtr<nsITreeView> view = GetExistingView();
  if (aColumn->Overflow()) {
    DebugOnly<nsresult> rv;
    nsTreeColumn* nextColumn = aColumn->GetNext();
    while (nextColumn && widthIsGreater) {
      while (nextColumn) {
        nscoord width;
        rv = nextColumn->GetWidthInTwips(this, &width);
        NS_ASSERTION(NS_SUCCEEDED(rv), "nextColumn is invalid");

        if (width != 0) {
          break;
        }

        nextColumn = nextColumn->GetNext();
      }

      if (nextColumn) {
        nsAutoString nextText;
        view->GetCellText(aRowIndex, nextColumn, nextText);
        // We don't measure or draw this text so no need to check it for
        // bidi-ness

        if (nextText.Length() == 0) {
          nscoord width;
          rv = nextColumn->GetWidthInTwips(this, &width);
          NS_ASSERTION(NS_SUCCEEDED(rv), "nextColumn is invalid");

          maxWidth += width;
          widthIsGreater = nsLayoutUtils::StringWidthIsGreaterThan(
              aText, aFontMetrics, drawTarget, maxWidth);

          nextColumn = nextColumn->GetNext();
        } else {
          nextColumn = nullptr;
        }
      }
    }
  }

  CroppingStyle cropType = CroppingStyle::CropRight;
  if (aColumn->GetCropStyle() == 1) {
    cropType = CroppingStyle::CropCenter;
  } else if (aColumn->GetCropStyle() == 2) {
    cropType = CroppingStyle::CropLeft;
  }
  CropStringForWidth(aText, aRenderingContext, aFontMetrics, maxWidth,
                     cropType);

  nscoord width = nsLayoutUtils::AppUnitWidthOfStringBidi(
      aText, this, aFontMetrics, aRenderingContext);

  switch (aColumn->GetTextAlignment()) {
    case mozilla::StyleTextAlign::Right:
      aTextRect.x += aTextRect.width - width;
      break;
    case mozilla::StyleTextAlign::Center:
      aTextRect.x += (aTextRect.width - width) / 2;
      break;
    default:
      break;
  }

  aTextRect.width = width;
}

PseudoStyleType nsTreeBodyFrame::GetItemWithinCellAt(nscoord aX,
                                                     const nsRect& aCellRect,
                                                     int32_t aRowIndex,
                                                     nsTreeColumn* aColumn) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  // Obtain the properties for our cell.
  PrefillPropertyArray(aRowIndex, aColumn);
  nsAutoString properties;
  nsCOMPtr<nsITreeView> view = GetExistingView();
  view->GetCellProperties(aRowIndex, aColumn, properties);
  nsTreeUtils::TokenizeProperties(properties, mScratchArray);

  // Resolve style for the cell.
  ComputedStyle* cellContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCell);

  // Obtain the margins for the cell and then deflate our rect by that
  // amount.  The cell is assumed to be contained within the deflated rect.
  nsRect cellRect(aCellRect);
  nsMargin cellMargin;
  cellContext->StyleMargin()->GetMargin(cellMargin);
  cellRect.Deflate(cellMargin);

  // Adjust the rect for its border and padding.
  AdjustForBorderPadding(cellContext, cellRect);

  if (aX < cellRect.x || aX >= cellRect.x + cellRect.width) {
    // The user clicked within the cell's margins/borders/padding.  This
    // constitutes a click on the cell.
    return PseudoStyleType::MozTreeCell;
  }

  nscoord currX = cellRect.x;
  nscoord remainingWidth = cellRect.width;

  // Handle right alignment hit testing.
  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;

  nsPresContext* presContext = PresContext();
  UniquePtr<gfxContext> rc =
      presContext->PresShell()->CreateReferenceRenderingContext();

  if (aColumn->IsPrimary()) {
    // If we're the primary column, we have indentation and a twisty.
    int32_t level;
    view->GetLevel(aRowIndex, &level);

    if (!isRTL) {
      currX += mIndentation * level;
    }
    remainingWidth -= mIndentation * level;

    if ((isRTL && aX > currX + remainingWidth) || (!isRTL && aX < currX)) {
      // The user clicked within the indentation.
      return PseudoStyleType::MozTreeCell;
    }

    // Always leave space for the twisty.
    nsRect twistyRect(currX, cellRect.y, remainingWidth, cellRect.height);
    bool hasTwisty = false;
    bool isContainer = false;
    view->IsContainer(aRowIndex, &isContainer);
    if (isContainer) {
      bool isContainerEmpty = false;
      view->IsContainerEmpty(aRowIndex, &isContainerEmpty);
      if (!isContainerEmpty) {
        hasTwisty = true;
      }
    }

    // Resolve style for the twisty.
    ComputedStyle* twistyContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);

    nsRect imageSize;
    GetTwistyRect(aRowIndex, aColumn, imageSize, twistyRect, presContext,
                  twistyContext);

    // We will treat a click as hitting the twisty if it happens on the margins,
    // borders, padding, or content of the twisty object.  By allowing a "slop"
    // into the margin, we make it a little bit easier for a user to hit the
    // twisty.  (We don't want to be too picky here.)
    nsMargin twistyMargin;
    twistyContext->StyleMargin()->GetMargin(twistyMargin);
    twistyRect.Inflate(twistyMargin);
    if (isRTL) {
      twistyRect.x = currX + remainingWidth - twistyRect.width;
    }

    // Now we test to see if aX is actually within the twistyRect.  If it is,
    // and if the item should have a twisty, then we return "twisty".  If it is
    // within the rect but we shouldn't have a twisty, then we return "cell".
    if (aX >= twistyRect.x && aX < twistyRect.x + twistyRect.width) {
      if (hasTwisty) {
        return PseudoStyleType::MozTreeTwisty;
      }
      return PseudoStyleType::MozTreeCell;
    }

    if (!isRTL) {
      currX += twistyRect.width;
    }
    remainingWidth -= twistyRect.width;
  }

  // Now test to see if the user hit the icon for the cell.
  nsRect iconRect(currX, cellRect.y, remainingWidth, cellRect.height);

  // Resolve style for the image.
  ComputedStyle* imageContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeImage);

  nsRect iconSize = GetImageSize(aRowIndex, aColumn, false, imageContext);
  nsMargin imageMargin;
  imageContext->StyleMargin()->GetMargin(imageMargin);
  iconSize.Inflate(imageMargin);
  iconRect.width = iconSize.width;
  if (isRTL) {
    iconRect.x = currX + remainingWidth - iconRect.width;
  }

  if (aX >= iconRect.x && aX < iconRect.x + iconRect.width) {
    // The user clicked on the image.
    return PseudoStyleType::MozTreeImage;
  }

  if (!isRTL) {
    currX += iconRect.width;
  }
  remainingWidth -= iconRect.width;

  nsAutoString cellText;
  view->GetCellText(aRowIndex, aColumn, cellText);
  // We're going to measure this text so we need to ensure bidi is enabled if
  // necessary
  CheckTextForBidi(cellText);

  nsRect textRect(currX, cellRect.y, remainingWidth, cellRect.height);

  ComputedStyle* textContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCellText);

  nsMargin textMargin;
  textContext->StyleMargin()->GetMargin(textMargin);
  textRect.Deflate(textMargin);

  AdjustForBorderPadding(textContext, textRect);

  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForComputedStyle(textContext, presContext);
  AdjustForCellText(cellText, aRowIndex, aColumn, *rc, *fm, textRect);

  if (aX >= textRect.x && aX < textRect.x + textRect.width) {
    return PseudoStyleType::MozTreeCellText;
  }
  return PseudoStyleType::MozTreeCell;
}

void nsTreeBodyFrame::GetCellAt(nscoord aX, nscoord aY, int32_t* aRow,
                                nsTreeColumn** aCol,
                                PseudoStyleType* aChildElt) {
  *aCol = nullptr;
  *aChildElt = PseudoStyleType::NotPseudo;

  *aRow = GetRowAtInternal(aX, aY);
  if (*aRow < 0) {
    return;
  }

  // Determine the column hit.
  for (nsTreeColumn* currCol = mColumns->GetFirstColumn(); currCol;
       currCol = currCol->GetNext()) {
    nsRect cellRect;
    nsresult rv = currCol->GetRect(
        this, mInnerBox.y + mRowHeight * (*aRow - mTopRowIndex), mRowHeight,
        &cellRect);
    if (NS_FAILED(rv)) {
      MOZ_ASSERT_UNREACHABLE("column has no frame");
      continue;
    }

    if (!OffsetForHorzScroll(cellRect, false)) {
      continue;
    }

    if (aX >= cellRect.x && aX < cellRect.x + cellRect.width) {
      // We know the column hit now.
      *aCol = currCol;

      if (currCol->IsCycler()) {
        // Cyclers contain only images.  Fill this in immediately and return.
        *aChildElt = PseudoStyleType::MozTreeImage;
      } else {
        *aChildElt = GetItemWithinCellAt(aX, cellRect, *aRow, currCol);
      }
      break;
    }
  }
}

nsresult nsTreeBodyFrame::GetCellWidth(int32_t aRow, nsTreeColumn* aCol,
                                       gfxContext* aRenderingContext,
                                       nscoord& aDesiredSize,
                                       nscoord& aCurrentSize) {
  MOZ_ASSERT(aCol, "aCol must not be null");
  MOZ_ASSERT(aRenderingContext, "aRenderingContext must not be null");

  // The rect for the current cell.
  nscoord colWidth;
  nsresult rv = aCol->GetWidthInTwips(this, &colWidth);
  NS_ENSURE_SUCCESS(rv, rv);

  nsRect cellRect(0, 0, colWidth, mRowHeight);

  int32_t overflow =
      cellRect.x + cellRect.width - (mInnerBox.x + mInnerBox.width);
  if (overflow > 0) {
    cellRect.width -= overflow;
  }

  // Adjust borders and padding for the cell.
  ComputedStyle* cellContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCell);
  nsMargin bp(0, 0, 0, 0);
  GetBorderPadding(cellContext, bp);

  aCurrentSize = cellRect.width;
  aDesiredSize = bp.left + bp.right;
  nsCOMPtr<nsITreeView> view = GetExistingView();

  if (aCol->IsPrimary()) {
    // If the current Column is a Primary, then we need to take into account
    // the indentation and possibly a twisty.

    // The amount of indentation is the indentation width (|mIndentation|) by
    // the level.
    int32_t level;
    view->GetLevel(aRow, &level);
    aDesiredSize += mIndentation * level;

    // Find the twisty rect by computing its size.
    ComputedStyle* twistyContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);

    nsRect imageSize;
    nsRect twistyRect(cellRect);
    GetTwistyRect(aRow, aCol, imageSize, twistyRect, PresContext(),
                  twistyContext);

    // Add in the margins of the twisty element.
    nsMargin twistyMargin;
    twistyContext->StyleMargin()->GetMargin(twistyMargin);
    twistyRect.Inflate(twistyMargin);

    aDesiredSize += twistyRect.width;
  }

  ComputedStyle* imageContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeImage);

  // Account for the width of the cell image.
  nsRect imageSize = GetImageSize(aRow, aCol, false, imageContext);
  // Add in the margins of the cell image.
  nsMargin imageMargin;
  imageContext->StyleMargin()->GetMargin(imageMargin);
  imageSize.Inflate(imageMargin);

  aDesiredSize += imageSize.width;

  // Get the cell text.
  nsAutoString cellText;
  view->GetCellText(aRow, aCol, cellText);
  // We're going to measure this text so we need to ensure bidi is enabled if
  // necessary
  CheckTextForBidi(cellText);

  ComputedStyle* textContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCellText);

  // Get the borders and padding for the text.
  GetBorderPadding(textContext, bp);

  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForComputedStyle(textContext, PresContext());
  // Get the width of the text itself
  nscoord width = nsLayoutUtils::AppUnitWidthOfStringBidi(cellText, this, *fm,
                                                          *aRenderingContext);
  nscoord totalTextWidth = width + bp.left + bp.right;
  aDesiredSize += totalTextWidth;
  return NS_OK;
}

nsresult nsTreeBodyFrame::IsCellCropped(int32_t aRow, nsTreeColumn* aCol,
                                        bool* _retval) {
  nscoord currentSize, desiredSize;
  nsresult rv;

  if (!aCol) {
    return NS_ERROR_INVALID_ARG;
  }

  UniquePtr<gfxContext> rc = PresShell()->CreateReferenceRenderingContext();

  rv = GetCellWidth(aRow, aCol, rc.get(), desiredSize, currentSize);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = desiredSize > currentSize;

  return NS_OK;
}

nsresult nsTreeBodyFrame::CreateTimer(const LookAndFeel::IntID aID,
                                      nsTimerCallbackFunc aFunc, int32_t aType,
                                      nsITimer** aTimer,
                                      const nsACString& aName) {
  // Get the delay from the look and feel service.
  int32_t delay = LookAndFeel::GetInt(aID, 0);

  nsCOMPtr<nsITimer> timer;

  // Create a new timer only if the delay is greater than zero.
  // Zero value means that this feature is completely disabled.
  if (delay > 0) {
    timer = MOZ_TRY(NS_NewTimerWithFuncCallback(
        aFunc, this, delay, aType, aName, GetMainThreadSerialEventTarget()));
  }

  timer.forget(aTimer);
  return NS_OK;
}

nsresult nsTreeBodyFrame::RowCountChanged(int32_t aIndex, int32_t aCount) {
  if (aCount == 0 || !mView) {
    return NS_OK;  // Nothing to do.
  }

#ifdef ACCESSIBILITY
  if (GetAccService()) {
    FireRowCountChangedEvent(aIndex, aCount);
  }
#endif  // #ifdef ACCESSIBILITY

  AutoWeakFrame weakFrame(this);

  // Adjust our selection.
  if (nsCOMPtr<nsITreeSelection> sel = GetSelection()) {
    sel->AdjustSelection(aIndex, aCount);
  }

  NS_ENSURE_STATE(weakFrame.IsAlive());

  if (mUpdateBatchNest) {
    return NS_OK;
  }

  mRowCount += aCount;
#ifdef DEBUG
  int32_t rowCount = mRowCount;
  mView->GetRowCount(&rowCount);
  NS_ASSERTION(
      rowCount == mRowCount,
      "row count did not change by the amount suggested, check caller");
#endif

  int32_t count = Abs(aCount);
  int32_t last = LastVisibleRow();
  if (aIndex >= mTopRowIndex && aIndex <= last) {
    InvalidateRange(aIndex, last);
  }

  ScrollParts parts = GetScrollParts();

  if (mTopRowIndex == 0) {
    // Just update the scrollbar and return.
    FullScrollbarsUpdate(false);
    return NS_OK;
  }

  bool needsInvalidation = false;
  // Adjust our top row index.
  if (aCount > 0) {
    if (mTopRowIndex > aIndex) {
      // Rows came in above us.  Augment the top row index.
      mTopRowIndex += aCount;
    }
  } else if (aCount < 0) {
    if (mTopRowIndex > aIndex + count - 1) {
      // No need to invalidate. The remove happened
      // completely above us (offscreen).
      mTopRowIndex -= count;
    } else if (mTopRowIndex >= aIndex) {
      // This is a full-blown invalidate.
      if (mTopRowIndex + mPageLength > mRowCount - 1) {
        mTopRowIndex = std::max(0, mRowCount - 1 - mPageLength);
      }
      needsInvalidation = true;
    }
  }

  int32_t lastPageTopRow = std::max(0, mRowCount - mPageLength);
  if (mTopRowIndex > lastPageTopRow) {
    mTopRowIndex = lastPageTopRow;
    needsInvalidation = true;
  }

  FullScrollbarsUpdate(needsInvalidation);
  return NS_OK;
}

nsresult nsTreeBodyFrame::BeginUpdateBatch() {
  ++mUpdateBatchNest;

  return NS_OK;
}

nsresult nsTreeBodyFrame::EndUpdateBatch() {
  NS_ASSERTION(mUpdateBatchNest > 0, "badly nested update batch");

  if (--mUpdateBatchNest != 0) {
    return NS_OK;
  }

  nsCOMPtr<nsITreeView> view = GetExistingView();
  if (!view) {
    return NS_OK;
  }

  Invalidate();
  int32_t countBeforeUpdate = mRowCount;
  view->GetRowCount(&mRowCount);
  if (countBeforeUpdate != mRowCount) {
    if (mTopRowIndex + mPageLength > mRowCount - 1) {
      mTopRowIndex = std::max(0, mRowCount - 1 - mPageLength);
    }
    FullScrollbarsUpdate(false);
  }

  return NS_OK;
}

void nsTreeBodyFrame::PrefillPropertyArray(int32_t aRowIndex,
                                           nsTreeColumn* aCol) {
  MOZ_ASSERT(!aCol || aCol->GetFrame(), "invalid column passed");
  mScratchArray.Clear();

  // focus
  if (mFocused) {
    mScratchArray.AppendElement(nsGkAtoms::focus);
  } else {
    mScratchArray.AppendElement(nsGkAtoms::blur);
  }

  // sort
  bool sorted = false;
  mView->IsSorted(&sorted);
  if (sorted) {
    mScratchArray.AppendElement(nsGkAtoms::sorted);
  }

  // drag session
  if (mSlots && mSlots->mIsDragging) {
    mScratchArray.AppendElement(nsGkAtoms::dragSession);
  }

  if (aRowIndex != -1) {
    if (aRowIndex == mMouseOverRow) {
      mScratchArray.AppendElement(nsGkAtoms::hover);
    }

    nsCOMPtr<nsITreeSelection> selection = GetSelection();
    if (selection) {
      // selected
      bool isSelected;
      selection->IsSelected(aRowIndex, &isSelected);
      if (isSelected) {
        mScratchArray.AppendElement(nsGkAtoms::selected);
      }

      // current
      int32_t currentIndex;
      selection->GetCurrentIndex(&currentIndex);
      if (aRowIndex == currentIndex) {
        mScratchArray.AppendElement(nsGkAtoms::current);
      }
    }

    // container or leaf
    bool isContainer = false;
    mView->IsContainer(aRowIndex, &isContainer);
    if (isContainer) {
      mScratchArray.AppendElement(nsGkAtoms::container);

      // open or closed
      bool isOpen = false;
      mView->IsContainerOpen(aRowIndex, &isOpen);
      if (isOpen) {
        mScratchArray.AppendElement(nsGkAtoms::open);
      } else {
        mScratchArray.AppendElement(nsGkAtoms::closed);
      }
    } else {
      mScratchArray.AppendElement(nsGkAtoms::leaf);
    }

    // drop orientation
    if (mSlots && mSlots->mDropAllowed && mSlots->mDropRow == aRowIndex) {
      if (mSlots->mDropOrient == nsITreeView::DROP_BEFORE) {
        mScratchArray.AppendElement(nsGkAtoms::dropBefore);
      } else if (mSlots->mDropOrient == nsITreeView::DROP_ON) {
        mScratchArray.AppendElement(nsGkAtoms::dropOn);
      } else if (mSlots->mDropOrient == nsITreeView::DROP_AFTER) {
        mScratchArray.AppendElement(nsGkAtoms::dropAfter);
      }
    }

    // odd or even
    if (aRowIndex % 2) {
      mScratchArray.AppendElement(nsGkAtoms::odd);
    } else {
      mScratchArray.AppendElement(nsGkAtoms::even);
    }

    XULTreeElement* tree = GetBaseElement();
    if (tree && tree->HasAttr(nsGkAtoms::editing)) {
      mScratchArray.AppendElement(nsGkAtoms::editing);
    }

    // multiple columns
    if (mColumns->GetColumnAt(1)) {
      mScratchArray.AppendElement(nsGkAtoms::multicol);
    }
  }

  if (aCol) {
    mScratchArray.AppendElement(aCol->GetAtom());

    if (aCol->IsPrimary()) {
      mScratchArray.AppendElement(nsGkAtoms::primary);
    }

    if (aCol->GetType() == TreeColumn_Binding::TYPE_CHECKBOX) {
      mScratchArray.AppendElement(nsGkAtoms::checkbox);

      if (aRowIndex != -1) {
        nsAutoString value;
        mView->GetCellValue(aRowIndex, aCol, value);
        if (value.EqualsLiteral("true")) {
          mScratchArray.AppendElement(nsGkAtoms::checked);
        }
      }
    }

    if (aCol->mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::ordinal,
                                    u"1"_ns, eIgnoreCase)) {
      mScratchArray.AppendElement(nsGkAtoms::firstColumn);
    }

    // Read special properties from attributes on the column content node
    if (aCol->mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::insertbefore,
                                    nsGkAtoms::_true, eCaseMatters)) {
      mScratchArray.AppendElement(nsGkAtoms::insertbefore);
    }
    if (aCol->mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::insertafter,
                                    nsGkAtoms::_true, eCaseMatters)) {
      mScratchArray.AppendElement(nsGkAtoms::insertafter);
    }
  }
}

void nsTreeBodyFrame::GetTwistyRect(int32_t aRowIndex, nsTreeColumn* aColumn,
                                    nsRect& aImageRect, nsRect& aTwistyRect,
                                    nsPresContext* aPresContext,
                                    ComputedStyle* aTwistyContext) {
  // The twisty rect extends all the way to the end of the cell.  This is
  // incorrect.  We need to determine the twisty rect's true width.  This is
  // done by examining the ComputedStyle for a width first.  If it has one, we
  // use that.  If it doesn't, we use the image's natural width. If the image
  // hasn't loaded and if no width is specified, then we just bail. If there is
  // a -moz-appearance involved, adjust the rect by the minimum widget size
  // provided by the theme implementation.
  aImageRect = GetImageSize(aRowIndex, aColumn, true, aTwistyContext);
  if (aImageRect.height > aTwistyRect.height) {
    aImageRect.height = aTwistyRect.height;
  }
  if (aImageRect.width > aTwistyRect.width) {
    aImageRect.width = aTwistyRect.width;
  } else {
    aTwistyRect.width = aImageRect.width;
  }
}

already_AddRefed<imgIContainer> nsTreeBodyFrame::GetImage(
    int32_t aRowIndex, nsTreeColumn* aCol, bool aUseContext,
    ComputedStyle* aComputedStyle) {
  Document* doc = PresContext()->Document();
  nsAutoString imageSrc;
  mView->GetImageSrc(aRowIndex, aCol, imageSrc);
  RefPtr<imgRequestProxy> styleRequest;
  nsCOMPtr<nsIURI> uri;
  if (aUseContext || imageSrc.IsEmpty()) {
    // Obtain the URL from the ComputedStyle.
    styleRequest =
        aComputedStyle->StyleList()->mListStyleImage.GetImageRequest();
    if (!styleRequest) {
      return nullptr;
    }
    styleRequest->GetURI(getter_AddRefs(uri));
  } else {
    nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(uri), imageSrc,
                                              doc, mContent->GetBaseURI());
  }
  if (!uri) {
    return nullptr;
  }
  // Look the image up in our cache.
  nsTreeImageCacheEntry entry;
  if (mImageCache.Get(uri, &entry)) {
    nsCOMPtr<imgIContainer> result;
    entry.request->GetImage(getter_AddRefs(result));
    entry.listener->AddCell(aRowIndex, aCol);
    return result.forget();
  }

  // Create a new nsTreeImageListener object and pass it our row and column
  // information.
  auto listener = MakeRefPtr<nsTreeImageListener>(this);
  listener->AddCell(aRowIndex, aCol);

  RefPtr<imgRequestProxy> imageRequest;
  if (styleRequest) {
    styleRequest->SyncClone(listener, doc, getter_AddRefs(imageRequest));
  } else {
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
    // XXXbz what's the origin principal for this stuff that comes from our
    // view?  I guess we should assume that it's the node's principal...
    nsContentUtils::LoadImage(uri, mContent, doc, mContent->NodePrincipal(), 0,
                              referrerInfo, listener, nsIRequest::LOAD_NORMAL,
                              u""_ns, getter_AddRefs(imageRequest));
  }
  listener->UnsuppressInvalidation();
  if (!imageRequest) {
    return nullptr;
  }

  // NOTE(heycam): If it's an SVG image, and we need to want the image to
  // able to respond to media query changes, it needs to be added to the
  // document's tracked image set.  For now, assume we don't need this.
  // We don't want discarding/decode-on-draw for xul images
  imageRequest->StartDecoding(imgIContainer::FLAG_ASYNC_NOTIFY);
  imageRequest->LockImage();

  // In a case it was already cached.
  mImageCache.InsertOrUpdate(uri,
                             nsTreeImageCacheEntry(imageRequest, listener));
  nsCOMPtr<imgIContainer> result;
  imageRequest->GetImage(getter_AddRefs(result));
  return result.forget();
}

nsRect nsTreeBodyFrame::GetImageSize(int32_t aRowIndex, nsTreeColumn* aCol,
                                     bool aUseContext,
                                     ComputedStyle* aComputedStyle) {
  // XXX We should respond to visibility rules for collapsed vs. hidden.

  // This method returns the width of the twisty INCLUDING borders and padding.
  // It first checks the ComputedStyle for a width.  If none is found, it tries
  // to use the default image width for the twisty.  If no image is found, it
  // defaults to border+padding.
  nsRect r(0, 0, 0, 0);
  nsMargin bp(0, 0, 0, 0);
  GetBorderPadding(aComputedStyle, bp);
  r.Inflate(bp);

  // Now r contains our border+padding info.  We now need to get our width and
  // height.
  bool needWidth = false;
  bool needHeight = false;

  // We have to load image even though we already have a size.
  // Don't change this, otherwise things start to go awry.
  nsCOMPtr<imgIContainer> image =
      GetImage(aRowIndex, aCol, aUseContext, aComputedStyle);

  const nsStylePosition* myPosition = aComputedStyle->StylePosition();
  const AnchorPosResolutionParams anchorResolutionParams{
      this, aComputedStyle->StyleDisplay()->mPosition};
  const auto width = myPosition->GetWidth(anchorResolutionParams);
  if (width->ConvertsToLength()) {
    int32_t val = width->ToLength();
    r.width += val;
  } else {
    needWidth = true;
  }

  const auto height = myPosition->GetHeight(anchorResolutionParams);
  if (height->ConvertsToLength()) {
    int32_t val = height->ToLength();
    r.height += val;
  } else {
    needHeight = true;
  }

  if (image) {
    if (needWidth || needHeight) {
      // Get the natural image size.

      if (needWidth) {
        // Get the size from the image.
        nscoord width;
        image->GetWidth(&width);
        r.width += nsPresContext::CSSPixelsToAppUnits(width);
      }

      if (needHeight) {
        nscoord height;
        image->GetHeight(&height);
        r.height += nsPresContext::CSSPixelsToAppUnits(height);
      }
    }
  }

  return r;
}

// GetImageDestSize returns the destination size of the image.
// The width and height do not include borders and padding.
// The width and height have not been adjusted to fit in the row height
// or cell width.
// The width and height reflect the destination size specified in CSS,
// or the image region specified in CSS, or the natural size of the
// image.
// If only the destination width has been specified in CSS, the height is
// calculated to maintain the aspect ratio of the image.
// If only the destination height has been specified in CSS, the width is
// calculated to maintain the aspect ratio of the image.
nsSize nsTreeBodyFrame::GetImageDestSize(ComputedStyle* aComputedStyle,
                                         imgIContainer* image) {
  nsSize size(0, 0);

  // We need to get the width and height.
  bool needWidth = false;
  bool needHeight = false;

  // Get the style position to see if the CSS has specified the
  // destination width/height.
  const nsStylePosition* myPosition = aComputedStyle->StylePosition();
  const AnchorPosResolutionParams anchorResolutionParams{
      this, aComputedStyle->StyleDisplay()->mPosition};
  const auto width = myPosition->GetWidth(anchorResolutionParams);
  if (width->ConvertsToLength()) {
    // CSS has specified the destination width.
    size.width = width->ToLength();
  } else {
    // We'll need to get the width of the image/region.
    needWidth = true;
  }

  const auto height = myPosition->GetHeight(anchorResolutionParams);
  if (height->ConvertsToLength()) {
    // CSS has specified the destination height.
    size.height = height->ToLength();
  } else {
    // We'll need to get the height of the image/region.
    needHeight = true;
  }

  if (needWidth || needHeight) {
    // We need to get the size of the image/region.
    nsSize imageSize(0, 0);
    if (image) {
      nscoord width;
      image->GetWidth(&width);
      imageSize.width = nsPresContext::CSSPixelsToAppUnits(width);
      nscoord height;
      image->GetHeight(&height);
      imageSize.height = nsPresContext::CSSPixelsToAppUnits(height);
    }

    if (needWidth) {
      if (!needHeight && imageSize.height != 0) {
        // The CSS specified the destination height, but not the destination
        // width. We need to calculate the width so that we maintain the
        // image's aspect ratio.
        size.width = imageSize.width * size.height / imageSize.height;
      } else {
        size.width = imageSize.width;
      }
    }

    if (needHeight) {
      if (!needWidth && imageSize.width != 0) {
        // The CSS specified the destination width, but not the destination
        // height. We need to calculate the height so that we maintain the
        // image's aspect ratio.
        size.height = imageSize.height * size.width / imageSize.width;
      } else {
        size.height = imageSize.height;
      }
    }
  }

  return size;
}

// GetImageSourceRect returns the source rectangle of the image to be
// displayed.
// The width and height reflect the image region specified in CSS, or
// the natural size of the image.
// The width and height do not include borders and padding.
// The width and height do not reflect the destination size specified
// in CSS.
nsRect nsTreeBodyFrame::GetImageSourceRect(ComputedStyle* aComputedStyle,
                                           imgIContainer* image) {
  if (!image) {
    return nsRect();
  }

  nsRect r;
  // Use the actual image size.
  nscoord coord;
  if (NS_SUCCEEDED(image->GetWidth(&coord))) {
    r.width = nsPresContext::CSSPixelsToAppUnits(coord);
  }
  if (NS_SUCCEEDED(image->GetHeight(&coord))) {
    r.height = nsPresContext::CSSPixelsToAppUnits(coord);
  }
  return r;
}

int32_t nsTreeBodyFrame::GetRowHeight() {
  // Look up the correct height.  It is equal to the specified height
  // + the specified margins.
  mScratchArray.Clear();
  ComputedStyle* rowContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeRow);
  if (rowContext) {
    const nsStylePosition* myPosition = rowContext->StylePosition();
    const AnchorPosResolutionParams anchorResolutionParams{
        this, rowContext->StyleDisplay()->mPosition};

    nscoord minHeight = 0;
    const auto styleMinHeight =
        myPosition->GetMinHeight(anchorResolutionParams);
    if (styleMinHeight->ConvertsToLength()) {
      minHeight = styleMinHeight->ToLength();
    }

    nscoord height = 0;
    const auto styleHeight = myPosition->GetHeight(anchorResolutionParams);
    if (styleHeight->ConvertsToLength()) {
      height = styleHeight->ToLength();
    }

    if (height < minHeight) {
      height = minHeight;
    }

    if (height > 0) {
      height = nsPresContext::AppUnitsToIntCSSPixels(height);
      height += height % 2;
      height = nsPresContext::CSSPixelsToAppUnits(height);

      // XXX Check box-sizing to determine if border/padding should augment the
      // height Inflate the height by our margins.
      nsRect rowRect(0, 0, 0, height);
      nsMargin rowMargin;
      rowContext->StyleMargin()->GetMargin(rowMargin);
      rowRect.Inflate(rowMargin);
      height = rowRect.height;
      return height;
    }
  }

  return nsPresContext::CSSPixelsToAppUnits(18);  // As good a default as any.
}

int32_t nsTreeBodyFrame::GetIndentation() {
  // Look up the correct indentation.  It is equal to the specified indentation
  // width.
  mScratchArray.Clear();
  ComputedStyle* indentContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeIndentation);
  if (indentContext) {
    const nsStylePosition* myPosition = indentContext->StylePosition();
    const AnchorPosResolutionParams anchorResolutionParams{
        this, indentContext->StyleDisplay()->mPosition};
    const auto width = myPosition->GetWidth(anchorResolutionParams);
    if (width->ConvertsToLength()) {
      return width->ToLength();
    }
  }

  return nsPresContext::CSSPixelsToAppUnits(16);  // As good a default as any.
}

void nsTreeBodyFrame::CalcInnerBox() {
  mInnerBox.SetRect(0, 0, mRect.width, mRect.height);
  AdjustForBorderPadding(mComputedStyle, mInnerBox);
}

nsIFrame::Cursor nsTreeBodyFrame::GetCursor(const nsPoint& aPoint) {
  // Check the GetScriptHandlingObject so we don't end up running code when
  // the document is a zombie.
  bool dummy;
  if (mView && GetContent()->GetComposedDoc()->GetScriptHandlingObject(dummy)) {
    int32_t row;
    nsTreeColumn* col;
    PseudoStyleType child;
    GetCellAt(aPoint.x, aPoint.y, &row, &col, &child);

    if (child != PseudoStyleType::NotPseudo) {
      // Our scratch array is already prefilled.
      RefPtr<ComputedStyle> childContext = GetPseudoComputedStyle(child);
      StyleCursorKind kind = childContext->StyleUI()->Cursor().keyword;
      if (kind == StyleCursorKind::Auto) {
        kind = StyleCursorKind::Default;
      }
      return Cursor{kind, AllowCustomCursorImage::Yes, std::move(childContext)};
    }
  }
  return SimpleXULLeafFrame::GetCursor(aPoint);
}

static uint32_t GetDropEffect(WidgetGUIEvent* aEvent) {
  NS_ASSERTION(aEvent->mClass == eDragEventClass, "wrong event type");
  WidgetDragEvent* dragEvent = aEvent->AsDragEvent();
  nsContentUtils::SetDataTransferInEvent(dragEvent);

  uint32_t action = 0;
  if (dragEvent->mDataTransfer) {
    action = dragEvent->mDataTransfer->DropEffectInt();
  }
  return action;
}

nsresult nsTreeBodyFrame::HandleEvent(nsPresContext* aPresContext,
                                      WidgetGUIEvent* aEvent,
                                      nsEventStatus* aEventStatus) {
  if (aEvent->mMessage == eMouseOver || aEvent->mMessage == eMouseMove) {
    nsPoint pt =
        nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, RelativeTo{this});
    int32_t xTwips = pt.x - mInnerBox.x;
    int32_t yTwips = pt.y - mInnerBox.y;
    int32_t newrow = GetRowAtInternal(xTwips, yTwips);
    if (mMouseOverRow != newrow) {
      // redraw the old and the new row
      if (mMouseOverRow != -1) {
        InvalidateRow(mMouseOverRow);
      }
      mMouseOverRow = newrow;
      if (mMouseOverRow != -1) {
        InvalidateRow(mMouseOverRow);
      }
    }
  } else if (aEvent->mMessage == eMouseOut) {
    if (mMouseOverRow != -1) {
      InvalidateRow(mMouseOverRow);
      mMouseOverRow = -1;
    }
  } else if (aEvent->mMessage == eDragEnter) {
    if (!mSlots) {
      mSlots = MakeUnique<Slots>();
    }

    // Cache several things we'll need throughout the course of our work. These
    // will all get released on a drag exit.

    if (mSlots->mTimer) {
      mSlots->mTimer->Cancel();
      mSlots->mTimer = nullptr;
    }

    // Cache the drag session.
    mSlots->mIsDragging = true;
    mSlots->mDropRow = -1;
    mSlots->mDropOrient = -1;
    mSlots->mDragAction = GetDropEffect(aEvent);
  } else if (aEvent->mMessage == eDragOver) {
    // The mouse is hovering over this tree. If we determine things are
    // different from the last time, invalidate the drop feedback at the old
    // position, query the view to see if the current location is droppable,
    // and then invalidate the drop feedback at the new location if it is.
    // The mouse may or may not have changed position from the last time
    // we were called, so optimize out a lot of the extra notifications by
    // checking if anything changed first. For drop feedback we use drop,
    // dropBefore and dropAfter property.
    if (!mView || !mSlots) {
      return NS_OK;
    }

    // Save last values, we will need them.
    int32_t lastDropRow = mSlots->mDropRow;
    int16_t lastDropOrient = mSlots->mDropOrient;
#ifndef XP_MACOSX
    int16_t lastScrollLines = mSlots->mScrollLines;
#endif

    // Find out the current drag action
    uint32_t lastDragAction = mSlots->mDragAction;
    mSlots->mDragAction = GetDropEffect(aEvent);

    // Compute the row mouse is over and the above/below/on state.
    // Below we'll use this to see if anything changed.
    // Also check if we want to auto-scroll.
    ComputeDropPosition(aEvent, &mSlots->mDropRow, &mSlots->mDropOrient,
                        &mSlots->mScrollLines);

    // While we're here, handle tracking of scrolling during a drag.
    if (mSlots->mScrollLines) {
      if (mSlots->mDropAllowed) {
        // Invalidate primary cell at old location.
        mSlots->mDropAllowed = false;
        InvalidateDropFeedback(lastDropRow, lastDropOrient);
      }
#ifdef XP_MACOSX
      ScrollByLines(mSlots->mScrollLines);
#else
      if (!lastScrollLines) {
        // Cancel any previously initialized timer.
        if (mSlots->mTimer) {
          mSlots->mTimer->Cancel();
          mSlots->mTimer = nullptr;
        }

        // Set a timer to trigger the tree scrolling.
        CreateTimer(LookAndFeel::IntID::TreeLazyScrollDelay, LazyScrollCallback,
                    nsITimer::TYPE_ONE_SHOT, getter_AddRefs(mSlots->mTimer),
                    "nsTreeBodyFrame::LazyScrollCallback"_ns);
      }
#endif
      // Bail out to prevent spring loaded timer and feedback line settings.
      return NS_OK;
    }

    // If changed from last time, invalidate primary cell at the old location
    // and if allowed, invalidate primary cell at the new location. If nothing
    // changed, just bail.
    if (mSlots->mDropRow != lastDropRow ||
        mSlots->mDropOrient != lastDropOrient ||
        mSlots->mDragAction != lastDragAction) {
      // Invalidate row at the old location.
      if (mSlots->mDropAllowed) {
        mSlots->mDropAllowed = false;
        InvalidateDropFeedback(lastDropRow, lastDropOrient);
      }

      if (mSlots->mTimer) {
        // Timer is active but for a different row than the current one, kill
        // it.
        mSlots->mTimer->Cancel();
        mSlots->mTimer = nullptr;
      }

      if (mSlots->mDropRow >= 0) {
        if (!mSlots->mTimer && mSlots->mDropOrient == nsITreeView::DROP_ON) {
          // Either there wasn't a timer running or it was just killed above.
          // If over a folder, start up a timer to open the folder.
          bool isContainer = false;
          mView->IsContainer(mSlots->mDropRow, &isContainer);
          if (isContainer) {
            bool isOpen = false;
            mView->IsContainerOpen(mSlots->mDropRow, &isOpen);
            if (!isOpen) {
              // This node isn't expanded, set a timer to expand it.
              CreateTimer(LookAndFeel::IntID::TreeOpenDelay, OpenCallback,
                          nsITimer::TYPE_ONE_SHOT,
                          getter_AddRefs(mSlots->mTimer),
                          "nsTreeBodyFrame::OpenCallback"_ns);
            }
          }
        }

        // The dataTransfer was initialized by the call to GetDropEffect above.
        bool canDropAtNewLocation = false;
        mView->CanDrop(mSlots->mDropRow, mSlots->mDropOrient,
                       aEvent->AsDragEvent()->mDataTransfer,
                       &canDropAtNewLocation);

        if (canDropAtNewLocation) {
          // Invalidate row at the new location.
          mSlots->mDropAllowed = canDropAtNewLocation;
          InvalidateDropFeedback(mSlots->mDropRow, mSlots->mDropOrient);
        }
      }
    }

    // Indicate that the drop is allowed by preventing the default behaviour.
    if (mSlots->mDropAllowed) {
      *aEventStatus = nsEventStatus_eConsumeNoDefault;
    }
  } else if (aEvent->mMessage == eDrop) {
    // this event was meant for another frame, so ignore it
    if (!mSlots) {
      return NS_OK;
    }

    // Tell the view where the drop happened.

    // Remove the drop folder and all its parents from the array.
    int32_t parentIndex;
    nsresult rv = mView->GetParentIndex(mSlots->mDropRow, &parentIndex);
    while (NS_SUCCEEDED(rv) && parentIndex >= 0) {
      mSlots->mArray.RemoveElement(parentIndex);
      rv = mView->GetParentIndex(parentIndex, &parentIndex);
    }

    NS_ASSERTION(aEvent->mClass == eDragEventClass, "wrong event type");
    WidgetDragEvent* dragEvent = aEvent->AsDragEvent();
    nsContentUtils::SetDataTransferInEvent(dragEvent);

    mView->Drop(mSlots->mDropRow, mSlots->mDropOrient,
                dragEvent->mDataTransfer);
    mSlots->mDropRow = -1;
    mSlots->mDropOrient = -1;
    mSlots->mIsDragging = false;
    *aEventStatus =
        nsEventStatus_eConsumeNoDefault;  // already handled the drop
  } else if (aEvent->mMessage == eDragExit) {
    // this event was meant for another frame, so ignore it
    if (!mSlots) {
      return NS_OK;
    }

    // Clear out all our tracking vars.

    if (mSlots->mDropAllowed) {
      mSlots->mDropAllowed = false;
      InvalidateDropFeedback(mSlots->mDropRow, mSlots->mDropOrient);
    } else {
      mSlots->mDropAllowed = false;
    }
    mSlots->mIsDragging = false;
    mSlots->mScrollLines = 0;
    // If a drop is occuring, the exit event will fire just before the drop
    // event, so don't reset mDropRow or mDropOrient as these fields are used
    // by the drop event.
    if (mSlots->mTimer) {
      mSlots->mTimer->Cancel();
      mSlots->mTimer = nullptr;
    }

    if (!mSlots->mArray.IsEmpty()) {
      // Close all spring loaded folders except the drop folder.
      CreateTimer(LookAndFeel::IntID::TreeCloseDelay, CloseCallback,
                  nsITimer::TYPE_ONE_SHOT, getter_AddRefs(mSlots->mTimer),
                  "nsTreeBodyFrame::CloseCallback"_ns);
    }
  }

  return NS_OK;
}

namespace mozilla {

class nsDisplayTreeBody final : public nsPaintedDisplayItem {
 public:
  nsDisplayTreeBody(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayTreeBody);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayTreeBody)

  nsDisplayItemGeometry* AllocateGeometry(
      nsDisplayListBuilder* aBuilder) override {
    return new nsDisplayTreeBodyGeometry(this, aBuilder, IsWindowActive());
  }

  void Destroy(nsDisplayListBuilder* aBuilder) override {
    aBuilder->UnregisterThemeGeometry(this);
    nsPaintedDisplayItem::Destroy(aBuilder);
  }

  bool IsWindowActive() const {
    return !mFrame->PresContext()->Document()->IsTopLevelWindowInactive();
  }

  void ComputeInvalidationRegion(nsDisplayListBuilder* aBuilder,
                                 const nsDisplayItemGeometry* aGeometry,
                                 nsRegion* aInvalidRegion) const override {
    auto geometry = static_cast<const nsDisplayTreeBodyGeometry*>(aGeometry);

    if (IsWindowActive() != geometry->mWindowIsActive) {
      bool snap;
      aInvalidRegion->Or(*aInvalidRegion, GetBounds(aBuilder, &snap));
    }

    nsPaintedDisplayItem::ComputeInvalidationRegion(aBuilder, aGeometry,
                                                    aInvalidRegion);
  }

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override {
    MOZ_ASSERT(aBuilder);
    (void)static_cast<nsTreeBodyFrame*>(mFrame)->PaintTreeBody(
        *aCtx, GetPaintRect(aBuilder, aCtx), ToReferenceFrame(), aBuilder);
  }

  NS_DISPLAY_DECL_NAME("XULTreeBody", TYPE_XUL_TREE_BODY)

  nsRect GetComponentAlphaBounds(
      nsDisplayListBuilder* aBuilder) const override {
    bool snap;
    return GetBounds(aBuilder, &snap);
  }
};

}  // namespace mozilla

// Painting routines
void nsTreeBodyFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  // REVIEW: why did we paint if we were collapsed? that makes no sense!
  if (!IsVisibleForPainting()) {
    return;  // We're invisible.  Don't paint.
  }

  // Handles painting our background, border, and outline.
  SimpleXULLeafFrame::BuildDisplayList(aBuilder, aLists);

  // Bail out now if there's no view or we can't run script because the
  // document is a zombie
  if (!mView || !GetContent()->GetComposedDoc()->GetWindow()) {
    return;
  }

  nsDisplayItem* item = MakeDisplayItem<nsDisplayTreeBody>(aBuilder, this);
  aLists.Content()->AppendToTop(item);
}

ImgDrawResult nsTreeBodyFrame::PaintTreeBody(gfxContext& aRenderingContext,
                                             const nsRect& aDirtyRect,
                                             nsPoint aPt,
                                             nsDisplayListBuilder* aBuilder) {
  // Update our available height and our page count.
  CalcInnerBox();

  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  aRenderingContext.Save();
  aRenderingContext.Clip(NSRectToSnappedRect(
      mInnerBox + aPt, PresContext()->AppUnitsPerDevPixel(), *drawTarget));
  int32_t oldPageCount = mPageLength;
  if (!mHasFixedRowCount) {
    mPageLength =
        (mRowHeight > 0) ? (mInnerBox.height / mRowHeight) : mRowCount;
  }

  if (oldPageCount != mPageLength || mHorzWidth != mRect.width) {
    // Schedule a ResizeReflow that will update our info properly.
    PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                  NS_FRAME_IS_DIRTY);
  }
#ifdef DEBUG
  int32_t rowCount = mRowCount;
  mView->GetRowCount(&rowCount);
  NS_WARNING_ASSERTION(mRowCount == rowCount, "row count changed unexpectedly");
#endif

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  // Loop through our columns and paint them (e.g., for sorting).  This is only
  // relevant when painting backgrounds, since columns contain no content.
  // Content is contained in the rows.
  for (nsTreeColumn* currCol = mColumns->GetFirstColumn(); currCol;
       currCol = currCol->GetNext()) {
    nsRect colRect;
    nsresult rv =
        currCol->GetRect(this, mInnerBox.y, mInnerBox.height, &colRect);
    // Don't paint hidden columns.
    if (NS_FAILED(rv) || colRect.width == 0) {
      continue;
    }

    if (OffsetForHorzScroll(colRect, false)) {
      nsRect dirtyRect;
      colRect += aPt;
      if (dirtyRect.IntersectRect(aDirtyRect, colRect)) {
        result &= PaintColumn(currCol, colRect, PresContext(),
                              aRenderingContext, aDirtyRect);
      }
    }
  }
  // Loop through our on-screen rows.
  for (int32_t i = mTopRowIndex;
       i < mRowCount && i <= mTopRowIndex + mPageLength; i++) {
    nsRect rowRect(mInnerBox.x, mInnerBox.y + mRowHeight * (i - mTopRowIndex),
                   mInnerBox.width, mRowHeight);
    nsRect dirtyRect;
    if (dirtyRect.IntersectRect(aDirtyRect, rowRect + aPt) &&
        rowRect.y < (mInnerBox.y + mInnerBox.height)) {
      result &= PaintRow(i, rowRect + aPt, PresContext(), aRenderingContext,
                         aDirtyRect, aPt, aBuilder);
    }
  }

  if (mSlots && mSlots->mDropAllowed &&
      (mSlots->mDropOrient == nsITreeView::DROP_BEFORE ||
       mSlots->mDropOrient == nsITreeView::DROP_AFTER)) {
    nscoord yPos = mInnerBox.y +
                   mRowHeight * (mSlots->mDropRow - mTopRowIndex) -
                   mRowHeight / 2;
    nsRect feedbackRect(mInnerBox.x, yPos, mInnerBox.width, mRowHeight);
    if (mSlots->mDropOrient == nsITreeView::DROP_AFTER) {
      feedbackRect.y += mRowHeight;
    }

    nsRect dirtyRect;
    feedbackRect += aPt;
    if (dirtyRect.IntersectRect(aDirtyRect, feedbackRect)) {
      result &= PaintDropFeedback(feedbackRect, PresContext(),
                                  aRenderingContext, aDirtyRect, aPt);
    }
  }
  aRenderingContext.Restore();

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintColumn(nsTreeColumn* aColumn,
                                           const nsRect& aColumnRect,
                                           nsPresContext* aPresContext,
                                           gfxContext& aRenderingContext,
                                           const nsRect& aDirtyRect) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  // Now obtain the properties for our cell.
  PrefillPropertyArray(-1, aColumn);
  nsAutoString properties;

  nsCOMPtr<nsITreeView> view = GetExistingView();
  view->GetColumnProperties(aColumn, properties);
  nsTreeUtils::TokenizeProperties(properties, mScratchArray);

  // Resolve style for the column.  It contains all the info we need to lay
  // ourselves out and to paint.
  ComputedStyle* colContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeColumn);

  // Obtain the margins for the cell and then deflate our rect by that
  // amount.  The cell is assumed to be contained within the deflated rect.
  nsRect colRect(aColumnRect);
  nsMargin colMargin;
  colContext->StyleMargin()->GetMargin(colMargin);
  colRect.Deflate(colMargin);

  return PaintBackgroundLayer(colContext, aPresContext, aRenderingContext,
                              colRect, aDirtyRect);
}

ImgDrawResult nsTreeBodyFrame::PaintRow(int32_t aRowIndex,
                                        const nsRect& aRowRect,
                                        nsPresContext* aPresContext,
                                        gfxContext& aRenderingContext,
                                        const nsRect& aDirtyRect, nsPoint aPt,
                                        nsDisplayListBuilder* aBuilder) {
  // We have been given a rect for our row.  We treat this row like a full-blown
  // frame, meaning that it can have borders, margins, padding, and a
  // background.

  // Without a view, we have no data. Check for this up front.
  nsCOMPtr<nsITreeView> view = GetExistingView();
  if (!view) {
    return ImgDrawResult::SUCCESS;
  }

  nsresult rv;

  // Now obtain the properties for our row.
  // XXX Automatically fill in the following props: open, closed, container,
  // leaf, selected, focused
  PrefillPropertyArray(aRowIndex, nullptr);

  nsAutoString properties;
  view->GetRowProperties(aRowIndex, properties);
  nsTreeUtils::TokenizeProperties(properties, mScratchArray);

  // Resolve style for the row.  It contains all the info we need to lay
  // ourselves out and to paint.
  ComputedStyle* rowContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeRow);

  // Obtain the margins for the row and then deflate our rect by that
  // amount.  The row is assumed to be contained within the deflated rect.
  nsRect rowRect(aRowRect);
  nsMargin rowMargin;
  rowContext->StyleMargin()->GetMargin(rowMargin);
  rowRect.Deflate(rowMargin);

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  // Paint our borders and background for our row rect.
  result &= PaintBackgroundLayer(rowContext, aPresContext, aRenderingContext,
                                 rowRect, aDirtyRect);

  // Adjust the rect for its border and padding.
  nsRect originalRowRect = rowRect;
  AdjustForBorderPadding(rowContext, rowRect);

  bool isSeparator = false;
  view->IsSeparator(aRowIndex, &isSeparator);
  if (isSeparator) {
    // The row is a separator.

    nscoord primaryX = rowRect.x;
    nsTreeColumn* primaryCol = mColumns->GetPrimaryColumn();
    if (primaryCol) {
      // Paint the primary cell.
      nsRect cellRect;
      rv = primaryCol->GetRect(this, rowRect.y, rowRect.height, &cellRect);
      if (NS_FAILED(rv)) {
        MOZ_ASSERT_UNREACHABLE("primary column is invalid");
        return result;
      }

      if (OffsetForHorzScroll(cellRect, false)) {
        cellRect.x += aPt.x;
        nsRect dirtyRect;
        nsRect checkRect(cellRect.x, originalRowRect.y, cellRect.width,
                         originalRowRect.height);
        if (dirtyRect.IntersectRect(aDirtyRect, checkRect)) {
          result &=
              PaintCell(aRowIndex, primaryCol, cellRect, aPresContext,
                        aRenderingContext, aDirtyRect, primaryX, aPt, aBuilder);
        }
      }

      // Paint the left side of the separator.
      nscoord currX;
      nsTreeColumn* previousCol = primaryCol->GetPrevious();
      if (previousCol) {
        nsRect prevColRect;
        rv = previousCol->GetRect(this, 0, 0, &prevColRect);
        if (NS_SUCCEEDED(rv)) {
          currX = (prevColRect.x - mHorzPosition) + prevColRect.width + aPt.x;
        } else {
          MOZ_ASSERT_UNREACHABLE(
              "The column before the primary column is "
              "invalid");
          currX = rowRect.x;
        }
      } else {
        currX = rowRect.x;
      }

      int32_t level;
      view->GetLevel(aRowIndex, &level);
      if (level == 0) {
        currX += mIndentation;
      }

      if (currX > rowRect.x) {
        nsRect separatorRect(rowRect);
        separatorRect.width -= rowRect.x + rowRect.width - currX;
        result &= PaintSeparator(aRowIndex, separatorRect, aPresContext,
                                 aRenderingContext, aDirtyRect);
      }
    }

    // Paint the right side (whole) separator.
    nsRect separatorRect(rowRect);
    if (primaryX > rowRect.x) {
      separatorRect.width -= primaryX - rowRect.x;
      separatorRect.x += primaryX - rowRect.x;
    }
    result &= PaintSeparator(aRowIndex, separatorRect, aPresContext,
                             aRenderingContext, aDirtyRect);
  } else {
    // Now loop over our cells. Only paint a cell if it intersects with our
    // dirty rect.
    for (nsTreeColumn* currCol = mColumns->GetFirstColumn(); currCol;
         currCol = currCol->GetNext()) {
      nsRect cellRect;
      rv = currCol->GetRect(this, rowRect.y, rowRect.height, &cellRect);
      // Don't paint cells in hidden columns.
      if (NS_FAILED(rv) || cellRect.width == 0) {
        continue;
      }

      if (OffsetForHorzScroll(cellRect, false)) {
        cellRect.x += aPt.x;

        // for primary columns, use the row's vertical size so that the
        // lines get drawn properly
        nsRect checkRect = cellRect;
        if (currCol->IsPrimary()) {
          checkRect = nsRect(cellRect.x, originalRowRect.y, cellRect.width,
                             originalRowRect.height);
        }

        nsRect dirtyRect;
        nscoord dummy;
        if (dirtyRect.IntersectRect(aDirtyRect, checkRect)) {
          result &=
              PaintCell(aRowIndex, currCol, cellRect, aPresContext,
                        aRenderingContext, aDirtyRect, dummy, aPt, aBuilder);
        }
      }
    }
  }

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintSeparator(int32_t aRowIndex,
                                              const nsRect& aSeparatorRect,
                                              nsPresContext* aPresContext,
                                              gfxContext& aRenderingContext,
                                              const nsRect& aDirtyRect) {
  // Resolve style for the separator.
  ComputedStyle* separatorContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeSeparator);

  const nsStylePosition* stylePosition = separatorContext->StylePosition();
  const AnchorPosResolutionParams anchorResolutionParams{
      this, separatorContext->StyleDisplay()->mPosition};

  // Obtain the height for the separator or use the default value.
  nscoord height;
  const auto styleHeight = stylePosition->GetHeight(anchorResolutionParams);
  if (styleHeight->ConvertsToLength()) {
    height = styleHeight->ToLength();
  } else {
    // Use default height 2px.
    height = nsPresContext::CSSPixelsToAppUnits(2);
  }

  // Obtain the margins for the separator and then deflate our rect by that
  // amount. The separator is assumed to be contained within the deflated
  // rect.
  nsRect separatorRect(aSeparatorRect.x, aSeparatorRect.y, aSeparatorRect.width,
                       height);
  nsMargin separatorMargin;
  separatorContext->StyleMargin()->GetMargin(separatorMargin);
  separatorRect.Deflate(separatorMargin);

  // Center the separator.
  separatorRect.y += (aSeparatorRect.height - height) / 2;

  return PaintBackgroundLayer(separatorContext, aPresContext, aRenderingContext,
                              separatorRect, aDirtyRect);
}

ImgDrawResult nsTreeBodyFrame::PaintCell(
    int32_t aRowIndex, nsTreeColumn* aColumn, const nsRect& aCellRect,
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDirtyRect, nscoord& aCurrX, nsPoint aPt,
    nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  // Now obtain the properties for our cell.
  // XXX Automatically fill in the following props: open, closed, container,
  // leaf, selected, focused, and the col ID.
  PrefillPropertyArray(aRowIndex, aColumn);
  nsAutoString properties;
  nsCOMPtr<nsITreeView> view = GetExistingView();
  view->GetCellProperties(aRowIndex, aColumn, properties);
  nsTreeUtils::TokenizeProperties(properties, mScratchArray);

  // Resolve style for the cell.  It contains all the info we need to lay
  // ourselves out and to paint.
  ComputedStyle* cellContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCell);

  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;

  // Obtain the margins for the cell and then deflate our rect by that
  // amount.  The cell is assumed to be contained within the deflated rect.
  nsRect cellRect(aCellRect);
  nsMargin cellMargin;
  cellContext->StyleMargin()->GetMargin(cellMargin);
  cellRect.Deflate(cellMargin);

  // Paint our borders and background for our row rect.
  ImgDrawResult result = PaintBackgroundLayer(
      cellContext, aPresContext, aRenderingContext, cellRect, aDirtyRect);

  // Adjust the rect for its border and padding.
  AdjustForBorderPadding(cellContext, cellRect);

  nscoord currX = cellRect.x;
  nscoord remainingWidth = cellRect.width;

  // Now we paint the contents of the cells.
  // Directionality of the tree determines the order in which we paint.
  // StyleDirection::Ltr means paint from left to right.
  // StyleDirection::Rtl means paint from right to left.

  if (aColumn->IsPrimary()) {
    // If we're the primary column, we need to indent and paint the twisty and
    // any connecting lines between siblings.

    int32_t level;
    view->GetLevel(aRowIndex, &level);

    if (!isRTL) {
      currX += mIndentation * level;
    }
    remainingWidth -= mIndentation * level;

    // Resolve the style to use for the connecting lines.
    ComputedStyle* lineContext =
        GetPseudoComputedStyle(PseudoStyleType::MozTreeLine);

    if (mIndentation && level &&
        lineContext->StyleVisibility()->IsVisibleOrCollapsed()) {
      // Paint the thread lines.

      // Get the size of the twisty. We don't want to paint the twisty
      // before painting of connecting lines since it would paint lines over
      // the twisty. But we need to leave a place for it.
      ComputedStyle* twistyContext =
          GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);

      nsRect imageSize;
      nsRect twistyRect(aCellRect);
      GetTwistyRect(aRowIndex, aColumn, imageSize, twistyRect, aPresContext,
                    twistyContext);

      nsMargin twistyMargin;
      twistyContext->StyleMargin()->GetMargin(twistyMargin);
      twistyRect.Inflate(twistyMargin);

      const nsStyleBorder* borderStyle = lineContext->StyleBorder();
      // Resolve currentcolor values against the treeline context
      nscolor color = borderStyle->mBorderLeftColor.CalcColor(*lineContext);
      ColorPattern colorPatt(ToDeviceColor(color));

      StyleBorderStyle style = borderStyle->GetBorderStyle(eSideLeft);
      StrokeOptions strokeOptions;
      nsLayoutUtils::InitDashPattern(strokeOptions, style);

      nscoord srcX = currX + twistyRect.width - mIndentation / 2;
      nscoord lineY = (aRowIndex - mTopRowIndex) * mRowHeight + aPt.y;

      DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();
      nsPresContext* pc = PresContext();

      // Don't paint off our cell.
      if (srcX <= cellRect.x + cellRect.width) {
        nscoord destX = currX + twistyRect.width;
        if (destX > cellRect.x + cellRect.width) {
          destX = cellRect.x + cellRect.width;
        }
        if (isRTL) {
          srcX = currX + remainingWidth - (srcX - cellRect.x);
          destX = currX + remainingWidth - (destX - cellRect.x);
        }
        Point p1(pc->AppUnitsToGfxUnits(srcX),
                 pc->AppUnitsToGfxUnits(lineY + mRowHeight / 2));
        Point p2(pc->AppUnitsToGfxUnits(destX),
                 pc->AppUnitsToGfxUnits(lineY + mRowHeight / 2));
        SnapLineToDevicePixelsForStroking(p1, p2, *drawTarget,
                                          strokeOptions.mLineWidth);
        drawTarget->StrokeLine(p1, p2, colorPatt, strokeOptions);
      }

      int32_t currentParent = aRowIndex;
      for (int32_t i = level; i > 0; i--) {
        if (srcX <= cellRect.x + cellRect.width) {
          // Paint full vertical line only if we have next sibling.
          bool hasNextSibling;
          view->HasNextSibling(currentParent, aRowIndex, &hasNextSibling);
          if (hasNextSibling || i == level) {
            Point p1(pc->AppUnitsToGfxUnits(srcX),
                     pc->AppUnitsToGfxUnits(lineY));
            Point p2;
            p2.x = pc->AppUnitsToGfxUnits(srcX);

            if (hasNextSibling) {
              p2.y = pc->AppUnitsToGfxUnits(lineY + mRowHeight);
            } else if (i == level) {
              p2.y = pc->AppUnitsToGfxUnits(lineY + mRowHeight / 2);
            }

            SnapLineToDevicePixelsForStroking(p1, p2, *drawTarget,
                                              strokeOptions.mLineWidth);
            drawTarget->StrokeLine(p1, p2, colorPatt, strokeOptions);
          }
        }

        int32_t parent;
        if (NS_FAILED(view->GetParentIndex(currentParent, &parent)) ||
            parent < 0) {
          break;
        }
        currentParent = parent;
        srcX -= mIndentation;
      }
    }

    // Always leave space for the twisty.
    nsRect twistyRect(currX, cellRect.y, remainingWidth, cellRect.height);
    result &= PaintTwisty(aRowIndex, aColumn, twistyRect, aPresContext,
                          aRenderingContext, aDirtyRect, remainingWidth, currX);
  }

  // Now paint the icon for our cell.
  nsRect iconRect(currX, cellRect.y, remainingWidth, cellRect.height);
  nsRect dirtyRect;
  if (dirtyRect.IntersectRect(aDirtyRect, iconRect)) {
    result &= PaintImage(aRowIndex, aColumn, iconRect, aPresContext,
                         aRenderingContext, aDirtyRect, remainingWidth, currX,
                         aBuilder);
  }

  // Now paint our element, but only if we aren't a cycler column.
  // XXX until we have the ability to load images, allow the view to
  // insert text into cycler columns...
  if (!aColumn->IsCycler()) {
    nsRect elementRect(currX, cellRect.y, remainingWidth, cellRect.height);
    nsRect dirtyRect;
    if (dirtyRect.IntersectRect(aDirtyRect, elementRect)) {
      switch (aColumn->GetType()) {
        case TreeColumn_Binding::TYPE_TEXT:
          result &= PaintText(aRowIndex, aColumn, elementRect, aPresContext,
                              aRenderingContext, aDirtyRect, currX);
          break;
        case TreeColumn_Binding::TYPE_CHECKBOX:
          result &= PaintCheckbox(aRowIndex, aColumn, elementRect, aPresContext,
                                  aRenderingContext, aDirtyRect);
          break;
      }
    }
  }

  aCurrX = currX;

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintTwisty(
    int32_t aRowIndex, nsTreeColumn* aColumn, const nsRect& aTwistyRect,
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDirtyRect, nscoord& aRemainingWidth, nscoord& aCurrX) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;
  nscoord rightEdge = aCurrX + aRemainingWidth;
  // Paint the twisty, but only if we are a non-empty container.
  bool shouldPaint = false;
  bool isContainer = false;
  nsCOMPtr<nsITreeView> view = GetExistingView();
  view->IsContainer(aRowIndex, &isContainer);
  if (isContainer) {
    bool isContainerEmpty = false;
    view->IsContainerEmpty(aRowIndex, &isContainerEmpty);
    if (!isContainerEmpty) {
      shouldPaint = true;
    }
  }

  // Resolve style for the twisty.
  ComputedStyle* twistyContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);

  // Obtain the margins for the twisty and then deflate our rect by that
  // amount.  The twisty is assumed to be contained within the deflated rect.
  nsRect twistyRect(aTwistyRect);
  nsMargin twistyMargin;
  twistyContext->StyleMargin()->GetMargin(twistyMargin);
  twistyRect.Deflate(twistyMargin);

  nsRect imageSize;
  GetTwistyRect(aRowIndex, aColumn, imageSize, twistyRect, aPresContext,
                twistyContext);

  // Subtract out the remaining width.  This is done even when we don't actually
  // paint a twisty in this cell, so that cells in different rows still line up.
  nsRect copyRect(twistyRect);
  copyRect.Inflate(twistyMargin);
  aRemainingWidth -= copyRect.width;
  if (!isRTL) {
    aCurrX += copyRect.width;
  }

  auto result = ImgDrawResult::SUCCESS;
  if (!shouldPaint) {
    return result;
  }
  // Paint our borders and background for our image rect.
  result &= PaintBackgroundLayer(twistyContext, aPresContext, aRenderingContext,
                                 twistyRect, aDirtyRect);

  // Time to paint the twisty.
  // Adjust the rect for its border and padding.
  nsMargin bp;
  GetBorderPadding(twistyContext, bp);
  twistyRect.Deflate(bp);
  if (isRTL) {
    twistyRect.x = rightEdge - twistyRect.width;
  }
  imageSize.Deflate(bp);

  // Get the image for drawing.
  nsCOMPtr<imgIContainer> image =
      GetImage(aRowIndex, aColumn, true, twistyContext);
  if (!image) {
    return result;
  }
  nsPoint anchorPoint = twistyRect.TopLeft();

  // Center the image. XXX Obey vertical-align style prop?
  if (imageSize.height < twistyRect.height) {
    anchorPoint.y += (twistyRect.height - imageSize.height) / 2;
  }

  // Apply context paint if applicable
  SVGImageContext svgContext;
  SVGImageContext::MaybeStoreContextPaint(svgContext, *aPresContext,
                                          *twistyContext, image);

  // Paint the image.
  result &= nsLayoutUtils::DrawSingleUnscaledImage(
      aRenderingContext, aPresContext, image, SamplingFilter::POINT,
      anchorPoint, &aDirtyRect, svgContext, imgIContainer::FLAG_NONE,
      &imageSize);
  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintImage(
    int32_t aRowIndex, nsTreeColumn* aColumn, const nsRect& aImageRect,
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDirtyRect, nscoord& aRemainingWidth, nscoord& aCurrX,
    nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;
  nscoord rightEdge = aCurrX + aRemainingWidth;
  // Resolve style for the image.
  ComputedStyle* imageContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeImage);

  // Obtain the margins for the image and then deflate our rect by that
  // amount.  The image is assumed to be contained within the deflated rect.
  nsRect imageRect(aImageRect);
  nsMargin imageMargin;
  imageContext->StyleMargin()->GetMargin(imageMargin);
  imageRect.Deflate(imageMargin);

  // Get the image.
  nsCOMPtr<imgIContainer> image =
      GetImage(aRowIndex, aColumn, false, imageContext);

  // Get the image destination size.
  nsSize imageDestSize = GetImageDestSize(imageContext, image);
  if (!imageDestSize.width || !imageDestSize.height) {
    return ImgDrawResult::SUCCESS;
  }

  // Get the borders and padding.
  nsMargin bp(0, 0, 0, 0);
  GetBorderPadding(imageContext, bp);

  // destRect will be passed as the aDestRect argument in the DrawImage method.
  // Start with the imageDestSize width and height.
  nsRect destRect(0, 0, imageDestSize.width, imageDestSize.height);
  // Inflate destRect for borders and padding so that we can compare/adjust
  // with respect to imageRect.
  destRect.Inflate(bp);

  // The destRect width and height have not been adjusted to fit within the
  // cell width and height.
  // We must adjust the width even if image is null, because the width is used
  // to update the aRemainingWidth and aCurrX values.
  // Since the height isn't used unless the image is not null, we will adjust
  // the height inside the if (image) block below.

  if (destRect.width > imageRect.width) {
    // The destRect is too wide to fit within the cell width.
    // Adjust destRect width to fit within the cell width.
    destRect.width = imageRect.width;
  } else {
    // The cell is wider than the destRect.
    // In a cycler column, the image is centered horizontally.
    if (!aColumn->IsCycler()) {
      // If this column is not a cycler, we won't center the image horizontally.
      // We adjust the imageRect width so that the image is placed at the start
      // of the cell.
      imageRect.width = destRect.width;
    }
  }

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  if (image) {
    if (isRTL) {
      imageRect.x = rightEdge - imageRect.width;
    }
    // Paint our borders and background for our image rect
    result &= PaintBackgroundLayer(imageContext, aPresContext,
                                   aRenderingContext, imageRect, aDirtyRect);

    // The destRect x and y have not been set yet. Let's do that now.
    // Initially, we use the imageRect x and y.
    destRect.x = imageRect.x;
    destRect.y = imageRect.y;

    if (destRect.width < imageRect.width) {
      // The destRect width is smaller than the cell width.
      // Center the image horizontally in the cell.
      // Adjust the destRect x accordingly.
      destRect.x += (imageRect.width - destRect.width) / 2;
    }

    // Now it's time to adjust the destRect height to fit within the cell
    // height.
    if (destRect.height > imageRect.height) {
      // The destRect height is larger than the cell height.
      // Adjust destRect height to fit within the cell height.
      destRect.height = imageRect.height;
    } else if (destRect.height < imageRect.height) {
      // The destRect height is smaller than the cell height.
      // Center the image vertically in the cell.
      // Adjust the destRect y accordingly.
      destRect.y += (imageRect.height - destRect.height) / 2;
    }

    // It's almost time to paint the image.
    // Deflate destRect for the border and padding.
    destRect.Deflate(bp);

    // Compute the area where our whole image would be mapped, to get the
    // desired subregion onto our actual destRect:
    nsRect wholeImageDest;
    CSSIntSize rawImageCSSIntSize;
    if (NS_SUCCEEDED(image->GetWidth(&rawImageCSSIntSize.width)) &&
        NS_SUCCEEDED(image->GetHeight(&rawImageCSSIntSize.height))) {
      // Get the image source rectangle - the rectangle containing the part of
      // the image that we are going to display.  sourceRect will be passed as
      // the aSrcRect argument in the DrawImage method.
      nsRect sourceRect = GetImageSourceRect(imageContext, image);

      // Let's say that the image is 100 pixels tall and that the CSS has
      // specified that the destination height should be 50 pixels tall. Let's
      // say that the cell height is only 20 pixels. So, in those 20 visible
      // pixels, we want to see the top 20/50ths of the image.  So, the
      // sourceRect.height should be 100 * 20 / 50, which is 40 pixels.
      // Essentially, we are scaling the image as dictated by the CSS
      // destination height and width, and we are then clipping the scaled
      // image by the cell width and height.
      nsSize rawImageSize(CSSPixel::ToAppUnits(rawImageCSSIntSize));
      wholeImageDest = nsLayoutUtils::GetWholeImageDestination(
          rawImageSize, sourceRect, nsRect(destRect.TopLeft(), imageDestSize));
    } else {
      // GetWidth/GetHeight failed, so we can't easily map a subregion of the
      // source image onto the destination area.
      // * If this happens with a RasterImage, it probably means the image is
      // in an error state, and we shouldn't draw anything. Hence, we leave
      // wholeImageDest as an empty rect (its initial state).
      // * If this happens with a VectorImage, it probably means the image has
      // no explicit width or height attribute -- but we can still proceed and
      // just treat the destination area as our whole SVG image area. Hence, we
      // set wholeImageDest to the full destRect.
      if (image->GetType() == imgIContainer::TYPE_VECTOR) {
        wholeImageDest = destRect;
      }
    }

    const auto* styleEffects = imageContext->StyleEffects();
    gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aRenderingContext);
    if (!styleEffects->IsOpaque()) {
      autoGroupForBlend.PushGroupForBlendBack(gfxContentType::COLOR_ALPHA,
                                              styleEffects->mOpacity);
    }

    uint32_t drawFlags = aBuilder && aBuilder->UseHighQualityScaling()
                             ? imgIContainer::FLAG_HIGH_QUALITY_SCALING
                             : imgIContainer::FLAG_NONE;
    result &= nsLayoutUtils::DrawImage(
        aRenderingContext, imageContext, aPresContext, image,
        nsLayoutUtils::GetSamplingFilterForFrame(this), wholeImageDest,
        destRect, destRect.TopLeft(), aDirtyRect, drawFlags);
  }

  // Update the aRemainingWidth and aCurrX values.
  imageRect.Inflate(imageMargin);
  aRemainingWidth -= imageRect.width;
  if (!isRTL) {
    aCurrX += imageRect.width;
  }

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintText(
    int32_t aRowIndex, nsTreeColumn* aColumn, const nsRect& aTextRect,
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDirtyRect, nscoord& aCurrX) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  bool isRTL = StyleVisibility()->mDirection == StyleDirection::Rtl;

  // Now obtain the text for our cell.
  nsAutoString text;
  nsCOMPtr<nsITreeView> view = GetExistingView();
  view->GetCellText(aRowIndex, aColumn, text);

  // We're going to paint this text so we need to ensure bidi is enabled if
  // necessary
  CheckTextForBidi(text);

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  if (text.Length() == 0) {
    // Don't paint an empty string. XXX What about background/borders? Still
    // paint?
    return result;
  }

  int32_t appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
  DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

  // Resolve style for the text.  It contains all the info we need to lay
  // ourselves out and to paint.
  ComputedStyle* textContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCellText);

  // Obtain the margins for the text and then deflate our rect by that
  // amount.  The text is assumed to be contained within the deflated rect.
  nsRect textRect(aTextRect);
  nsMargin textMargin;
  textContext->StyleMargin()->GetMargin(textMargin);
  textRect.Deflate(textMargin);

  // Adjust the rect for its border and padding.
  nsMargin bp(0, 0, 0, 0);
  GetBorderPadding(textContext, bp);
  textRect.Deflate(bp);

  // Compute our text size.
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetFontMetricsForComputedStyle(textContext, PresContext());

  nscoord height = fontMet->MaxHeight();
  nscoord baseline = fontMet->MaxAscent();

  // Center the text. XXX Obey vertical-align style prop?
  if (height < textRect.height) {
    textRect.y += (textRect.height - height) / 2;
    textRect.height = height;
  }

  // Set our font.
  AdjustForCellText(text, aRowIndex, aColumn, aRenderingContext, *fontMet,
                    textRect);
  textRect.Inflate(bp);

  // Subtract out the remaining width.
  if (!isRTL) {
    aCurrX += textRect.width + textMargin.LeftRight();
  }

  result &= PaintBackgroundLayer(textContext, aPresContext, aRenderingContext,
                                 textRect, aDirtyRect);

  // Time to paint our text.
  textRect.Deflate(bp);

  // Set our color.
  ColorPattern color(ToDeviceColor(textContext->StyleText()->mColor));

  // Draw decorations.
  StyleTextDecorationLine decorations =
      textContext->StyleTextReset()->mTextDecorationLine;

  nscoord offset;
  nscoord size;
  if (decorations & (StyleTextDecorationLine::OVERLINE |
                     StyleTextDecorationLine::UNDERLINE)) {
    fontMet->GetUnderline(offset, size);
    if (decorations & StyleTextDecorationLine::OVERLINE) {
      nsRect r(textRect.x, textRect.y, textRect.width, size);
      Rect devPxRect = NSRectToSnappedRect(r, appUnitsPerDevPixel, *drawTarget);
      drawTarget->FillRect(devPxRect, color);
    }
    if (decorations & StyleTextDecorationLine::UNDERLINE) {
      nsRect r(textRect.x, textRect.y + baseline - offset, textRect.width,
               size);
      Rect devPxRect = NSRectToSnappedRect(r, appUnitsPerDevPixel, *drawTarget);
      drawTarget->FillRect(devPxRect, color);
    }
  }
  if (decorations & StyleTextDecorationLine::LINE_THROUGH) {
    fontMet->GetStrikeout(offset, size);
    nsRect r(textRect.x, textRect.y + baseline - offset, textRect.width, size);
    Rect devPxRect = NSRectToSnappedRect(r, appUnitsPerDevPixel, *drawTarget);
    drawTarget->FillRect(devPxRect, color);
  }
  ComputedStyle* cellContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCell);

  const auto* styleEffects = textContext->StyleEffects();
  gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aRenderingContext);
  if (!styleEffects->IsOpaque()) {
    autoGroupForBlend.PushGroupForBlendBack(gfxContentType::COLOR_ALPHA,
                                            styleEffects->mOpacity);
  }

  aRenderingContext.SetColor(
      sRGBColor::FromABGR(textContext->StyleText()->mColor.ToColor()));
  nsLayoutUtils::DrawString(
      this, *fontMet, &aRenderingContext, text.get(), text.Length(),
      textRect.TopLeft() + nsPoint(0, baseline), cellContext);

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintCheckbox(int32_t aRowIndex,
                                             nsTreeColumn* aColumn,
                                             const nsRect& aCheckboxRect,
                                             nsPresContext* aPresContext,
                                             gfxContext& aRenderingContext,
                                             const nsRect& aDirtyRect) {
  MOZ_ASSERT(aColumn && aColumn->GetFrame(), "invalid column passed");

  // Resolve style for the checkbox.
  ComputedStyle* checkboxContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeCheckbox);

  nscoord rightEdge = aCheckboxRect.XMost();

  // Obtain the margins for the checkbox and then deflate our rect by that
  // amount.  The checkbox is assumed to be contained within the deflated rect.
  nsRect checkboxRect(aCheckboxRect);
  nsMargin checkboxMargin;
  checkboxContext->StyleMargin()->GetMargin(checkboxMargin);
  checkboxRect.Deflate(checkboxMargin);

  nsRect imageSize = GetImageSize(aRowIndex, aColumn, true, checkboxContext);

  if (imageSize.height > checkboxRect.height) {
    imageSize.height = checkboxRect.height;
  }
  if (imageSize.width > checkboxRect.width) {
    imageSize.width = checkboxRect.width;
  }

  if (StyleVisibility()->mDirection == StyleDirection::Rtl) {
    checkboxRect.x = rightEdge - checkboxRect.width;
  }

  // Paint our borders and background for our image rect.
  ImgDrawResult result =
      PaintBackgroundLayer(checkboxContext, aPresContext, aRenderingContext,
                           checkboxRect, aDirtyRect);

  // Time to paint the checkbox.
  // Adjust the rect for its border and padding.
  nsMargin bp(0, 0, 0, 0);
  GetBorderPadding(checkboxContext, bp);
  checkboxRect.Deflate(bp);

  // Get the image for drawing.
  nsCOMPtr<imgIContainer> image =
      GetImage(aRowIndex, aColumn, true, checkboxContext);
  if (image) {
    nsPoint pt = checkboxRect.TopLeft();

    if (imageSize.height < checkboxRect.height) {
      pt.y += (checkboxRect.height - imageSize.height) / 2;
    }

    if (imageSize.width < checkboxRect.width) {
      pt.x += (checkboxRect.width - imageSize.width) / 2;
    }

    // Apply context paint if applicable
    SVGImageContext svgContext;
    SVGImageContext::MaybeStoreContextPaint(svgContext, *aPresContext,
                                            *checkboxContext, image);
    // Paint the image.
    result &= nsLayoutUtils::DrawSingleUnscaledImage(
        aRenderingContext, aPresContext, image, SamplingFilter::POINT, pt,
        &aDirtyRect, svgContext, imgIContainer::FLAG_NONE, &imageSize);
  }

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintDropFeedback(
    const nsRect& aDropFeedbackRect, nsPresContext* aPresContext,
    gfxContext& aRenderingContext, const nsRect& aDirtyRect, nsPoint aPt) {
  // Paint the drop feedback in between rows.

  nscoord currX;

  // Adjust for the primary cell.
  nsTreeColumn* primaryCol = mColumns->GetPrimaryColumn();

  if (primaryCol) {
#ifdef DEBUG
    nsresult rv =
#endif
        primaryCol->GetXInTwips(this, &currX);
    NS_ASSERTION(NS_SUCCEEDED(rv), "primary column is invalid?");

    currX += aPt.x - mHorzPosition;
  } else {
    currX = aDropFeedbackRect.x;
  }

  PrefillPropertyArray(mSlots->mDropRow, primaryCol);

  // Resolve the style to use for the drop feedback.
  ComputedStyle* feedbackContext =
      GetPseudoComputedStyle(PseudoStyleType::MozTreeDropFeedback);

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  // Paint only if it is visible.
  nsCOMPtr<nsITreeView> view = GetExistingView();
  if (feedbackContext->StyleVisibility()->IsVisibleOrCollapsed()) {
    int32_t level;
    view->GetLevel(mSlots->mDropRow, &level);

    // If our previous or next row has greater level use that for
    // correct visual indentation.
    if (mSlots->mDropOrient == nsITreeView::DROP_BEFORE) {
      if (mSlots->mDropRow > 0) {
        int32_t previousLevel;
        view->GetLevel(mSlots->mDropRow - 1, &previousLevel);
        if (previousLevel > level) {
          level = previousLevel;
        }
      }
    } else {
      if (mSlots->mDropRow < mRowCount - 1) {
        int32_t nextLevel;
        view->GetLevel(mSlots->mDropRow + 1, &nextLevel);
        if (nextLevel > level) {
          level = nextLevel;
        }
      }
    }

    currX += mIndentation * level;

    if (primaryCol) {
      ComputedStyle* twistyContext =
          GetPseudoComputedStyle(PseudoStyleType::MozTreeTwisty);
      nsRect imageSize;
      nsRect twistyRect;
      GetTwistyRect(mSlots->mDropRow, primaryCol, imageSize, twistyRect,
                    aPresContext, twistyContext);
      nsMargin twistyMargin;
      twistyContext->StyleMargin()->GetMargin(twistyMargin);
      twistyRect.Inflate(twistyMargin);
      currX += twistyRect.width;
    }

    const nsStylePosition* stylePosition = feedbackContext->StylePosition();

    // Obtain the width for the drop feedback or use default value.
    nscoord width;
    const AnchorPosResolutionParams anchorResolutionParams{
        this, feedbackContext->StyleDisplay()->mPosition};
    const auto styleWidth = stylePosition->GetWidth(anchorResolutionParams);
    if (styleWidth->ConvertsToLength()) {
      width = styleWidth->ToLength();
    } else {
      // Use default width 50px.
      width = nsPresContext::CSSPixelsToAppUnits(50);
    }

    // Obtain the height for the drop feedback or use default value.
    nscoord height;
    const auto styleHeight = stylePosition->GetHeight(anchorResolutionParams);
    if (styleHeight->ConvertsToLength()) {
      height = styleHeight->ToLength();
    } else {
      // Use default height 2px.
      height = nsPresContext::CSSPixelsToAppUnits(2);
    }

    // Obtain the margins for the drop feedback and then deflate our rect
    // by that amount.
    nsRect feedbackRect(currX, aDropFeedbackRect.y, width, height);
    nsMargin margin;
    feedbackContext->StyleMargin()->GetMargin(margin);
    feedbackRect.Deflate(margin);

    feedbackRect.y += (aDropFeedbackRect.height - height) / 2;

    // Finally paint the drop feedback.
    result &= PaintBackgroundLayer(feedbackContext, aPresContext,
                                   aRenderingContext, feedbackRect, aDirtyRect);
  }

  return result;
}

ImgDrawResult nsTreeBodyFrame::PaintBackgroundLayer(
    ComputedStyle* aComputedStyle, nsPresContext* aPresContext,
    gfxContext& aRenderingContext, const nsRect& aRect,
    const nsRect& aDirtyRect) {
  const nsStyleBorder* myBorder = aComputedStyle->StyleBorder();
  nsCSSRendering::PaintBGParams params =
      nsCSSRendering::PaintBGParams::ForAllLayers(
          *aPresContext, aDirtyRect, aRect, this,
          nsCSSRendering::PAINTBG_SYNC_DECODE_IMAGES);
  ImgDrawResult result = nsCSSRendering::PaintStyleImageLayerWithSC(
      params, aRenderingContext, aComputedStyle, *myBorder);

  result &= nsCSSRendering::PaintBorderWithStyleBorder(
      aPresContext, aRenderingContext, this, aDirtyRect, aRect, *myBorder,
      mComputedStyle, PaintBorderFlags::SyncDecodeImages);

  nsCSSRendering::PaintNonThemedOutline(aPresContext, aRenderingContext, this,
                                        aDirtyRect, aRect, aComputedStyle);

  return result;
}

// Scrolling
nsresult nsTreeBodyFrame::EnsureRowIsVisible(int32_t aRow) {
  ScrollParts parts = GetScrollParts();
  nsresult rv = EnsureRowIsVisibleInternal(parts, aRow);
  NS_ENSURE_SUCCESS(rv, rv);
  UpdateScrollbars(parts);
  return rv;
}

nsresult nsTreeBodyFrame::EnsureRowIsVisibleInternal(const ScrollParts& aParts,
                                                     int32_t aRow) {
  if (!mView || !mPageLength) {
    return NS_OK;
  }

  if (mTopRowIndex <= aRow && mTopRowIndex + mPageLength > aRow) {
    return NS_OK;
  }

  if (aRow < mTopRowIndex) {
    ScrollToRowInternal(aParts, aRow);
  } else {
    // Bring it just on-screen.
    int32_t distance = aRow - (mTopRowIndex + mPageLength) + 1;
    ScrollToRowInternal(aParts, mTopRowIndex + distance);
  }

  return NS_OK;
}

nsresult nsTreeBodyFrame::EnsureCellIsVisible(int32_t aRow,
                                              nsTreeColumn* aCol) {
  if (!aCol) {
    return NS_ERROR_INVALID_ARG;
  }

  ScrollParts parts = GetScrollParts();
  nsresult rv = EnsureRowIsVisibleInternal(parts, aRow);
  NS_ENSURE_SUCCESS(rv, rv);
  UpdateScrollbars(parts);
  return rv;
}

void nsTreeBodyFrame::ScrollToRow(int32_t aRow) {
  ScrollParts parts = GetScrollParts();
  ScrollToRowInternal(parts, aRow);
  UpdateScrollbars(parts);
}

nsresult nsTreeBodyFrame::ScrollToRowInternal(const ScrollParts& aParts,
                                              int32_t aRow) {
  ScrollInternal(aParts, aRow);

  return NS_OK;
}

void nsTreeBodyFrame::ScrollByLines(int32_t aNumLines) {
  if (!mView) {
    return;
  }
  int32_t newIndex = mTopRowIndex + aNumLines;
  ScrollToRow(newIndex);
}

void nsTreeBodyFrame::ScrollByPages(int32_t aNumPages) {
  if (!mView) {
    return;
  }
  int32_t newIndex = mTopRowIndex + aNumPages * mPageLength;
  ScrollToRow(newIndex);
}

nsresult nsTreeBodyFrame::ScrollInternal(const ScrollParts& aParts,
                                         int32_t aRow) {
  if (!mView) {
    return NS_OK;
  }

  // Note that we may be "over scrolled" at this point; that is the
  // current mTopRowIndex may be larger than mRowCount - mPageLength.
  // This can happen when items are removed for example. (bug 1085050)

  int32_t maxTopRowIndex = std::max(0, mRowCount - mPageLength);
  aRow = std::clamp(aRow, 0, maxTopRowIndex);
  if (aRow == mTopRowIndex) {
    return NS_OK;
  }
  mTopRowIndex = aRow;
  Invalidate();
  PostScrollEvent();
  return NS_OK;
}

void nsTreeBodyFrame::ScrollByPage(nsScrollbarFrame* aScrollbar,
                                   int32_t aDirection,
                                   ScrollSnapFlags aSnapFlags) {
  // CSS Scroll Snapping is not enabled for XUL, aSnap is ignored
  MOZ_ASSERT(aScrollbar != nullptr);
  ScrollByPages(aDirection);
}

void nsTreeBodyFrame::ScrollByWhole(nsScrollbarFrame* aScrollbar,
                                    int32_t aDirection,
                                    ScrollSnapFlags aSnapFlags) {
  // CSS Scroll Snapping is not enabled for XUL, aSnap is ignored
  MOZ_ASSERT(aScrollbar != nullptr);
  int32_t newIndex = aDirection < 0 ? 0 : mTopRowIndex;
  ScrollToRow(newIndex);
}

void nsTreeBodyFrame::ScrollByLine(nsScrollbarFrame* aScrollbar,
                                   int32_t aDirection,
                                   ScrollSnapFlags aSnapFlags) {
  // CSS Scroll Snapping is not enabled for XUL, aSnap is ignored
  MOZ_ASSERT(aScrollbar != nullptr);
  ScrollByLines(aDirection);
}

void nsTreeBodyFrame::ScrollByUnit(nsScrollbarFrame* aScrollbar,
                                   ScrollMode aMode, int32_t aDirection,
                                   ScrollUnit aUnit,
                                   ScrollSnapFlags aSnapFlags) {
  MOZ_ASSERT_UNREACHABLE("Can't get here, we don't call MoveToNewPosition");
}

void nsTreeBodyFrame::RepeatButtonScroll(nsScrollbarFrame* aScrollbar) {
  MOZ_ASSERT(!aScrollbar->IsHorizontal());
  ScrollParts parts = GetScrollParts();
  int32_t direction = aScrollbar->GetButtonScrollDirection();
  AutoWeakFrame weakFrame(this);
  ScrollToRowInternal(parts, mTopRowIndex + direction);

  if (weakFrame.IsAlive() && mScrollbarActivity) {
    mScrollbarActivity->ActivityOccurred();
  }
  if (weakFrame.IsAlive()) {
    UpdateScrollbars(parts);
  }
}

void nsTreeBodyFrame::ThumbMoved(nsScrollbarFrame* aScrollbar, nscoord aOldPos,
                                 nscoord aNewPos) {
  ScrollParts parts = GetScrollParts();

  if (aOldPos == aNewPos) {
    return;
  }

  AutoWeakFrame weakFrame(this);

  // Vertical Scrollbar
  if (parts.mVScrollbar == aScrollbar) {
    nscoord rh = nsPresContext::AppUnitsToIntCSSPixels(mRowHeight);
    nscoord newIndex = nsPresContext::AppUnitsToIntCSSPixels(aNewPos);
    nscoord newrow = (rh > 0) ? (newIndex / rh) : 0;
    ScrollInternal(parts, newrow);
  }
  if (weakFrame.IsAlive()) {
    UpdateScrollbars(parts);
  }
}

// The style cache.
ComputedStyle* nsTreeBodyFrame::GetPseudoComputedStyle(
    PseudoStyleType aPseudoElement) {
  return mStyleCache.GetComputedStyle(PresContext(), mContent, mComputedStyle,
                                      aPseudoElement, mScratchArray);
}

XULTreeElement* nsTreeBodyFrame::GetBaseElement() {
  if (!mTree) {
    nsIFrame* parent = GetParent();
    while (parent) {
      nsIContent* content = parent->GetContent();
      if (content && content->IsXULElement(nsGkAtoms::tree)) {
        mTree = XULTreeElement::FromNodeOrNull(content->AsElement());
        break;
      }

      parent = parent->GetInFlowParent();
    }
  }

  return mTree;
}

nsresult nsTreeBodyFrame::ClearStyleAndImageCaches() {
  mStyleCache.Clear();
  CancelImageRequests();
  mImageCache.Clear();
  return NS_OK;
}

void nsTreeBodyFrame::RemoveImageCacheEntry(int32_t aRowIndex,
                                            nsTreeColumn* aCol) {
  nsAutoString imageSrc;
  nsCOMPtr<nsITreeView> view = GetExistingView();
  if (!view || NS_FAILED(view->GetImageSrc(aRowIndex, aCol, imageSrc))) {
    return;
  }
  nsCOMPtr<nsIURI> uri;
  auto* pc = PresContext();
  nsContentUtils::NewURIWithDocumentCharset(
      getter_AddRefs(uri), imageSrc, pc->Document(), mContent->GetBaseURI());
  if (!uri) {
    return;
  }
  auto lookup = mImageCache.Lookup(uri);
  if (!lookup) {
    return;
  }
  DoCancelImageCacheEntry(*lookup, pc);
  lookup.Remove();
}

/* virtual */
void nsTreeBodyFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  SimpleXULLeafFrame::DidSetComputedStyle(aOldComputedStyle);

  // Clear the style cache; the pointers are no longer even valid
  mStyleCache.Clear();
  // XXX The following is hacky, but it's not incorrect,
  // and appears to fix a few bugs with style changes, like text zoom and
  // dpi changes
  mIndentation = GetIndentation();
  mRowHeight = GetRowHeight();
}

bool nsTreeBodyFrame::OffsetForHorzScroll(nsRect& rect, bool clip) {
  rect.x -= mHorzPosition;

  // Scrolled out before
  if (rect.XMost() <= mInnerBox.x) {
    return false;
  }

  // Scrolled out after
  if (rect.x > mInnerBox.XMost()) {
    return false;
  }

  if (clip) {
    nscoord leftEdge = std::max(rect.x, mInnerBox.x);
    nscoord rightEdge = std::min(rect.XMost(), mInnerBox.XMost());
    rect.x = leftEdge;
    rect.width = rightEdge - leftEdge;

    // Should have returned false above
    NS_ASSERTION(rect.width >= 0, "horz scroll code out of sync");
  }

  return true;
}

bool nsTreeBodyFrame::CanAutoScroll(int32_t aRowIndex) {
  // Check first for partially visible last row.
  if (aRowIndex == mRowCount - 1) {
    nscoord y = mInnerBox.y + (aRowIndex - mTopRowIndex) * mRowHeight;
    if (y < mInnerBox.height && y + mRowHeight > mInnerBox.height) {
      return true;
    }
  }

  if (aRowIndex > 0 && aRowIndex < mRowCount - 1) {
    return true;
  }

  return false;
}

// Given a dom event, figure out which row in the tree the mouse is over,
// if we should drop before/after/on that row or we should auto-scroll.
// Doesn't query the content about if the drag is allowable, that's done
// elsewhere.
//
// For containers, we break up the vertical space of the row as follows: if in
// the topmost 25%, the drop is _before_ the row the mouse is over; if in the
// last 25%, _after_; in the middle 50%, we consider it a drop _on_ the
// container.
//
// For non-containers, if the mouse is in the top 50% of the row, the drop is
// _before_ and the bottom 50% _after_
void nsTreeBodyFrame::ComputeDropPosition(WidgetGUIEvent* aEvent, int32_t* aRow,
                                          int16_t* aOrient,
                                          int16_t* aScrollLines) {
  *aOrient = -1;
  *aScrollLines = 0;

  // Convert the event's point to our coordinates.  We want it in
  // the coordinates of our inner box's coordinates.
  nsPoint pt =
      nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, RelativeTo{this});
  int32_t xTwips = pt.x - mInnerBox.x;
  int32_t yTwips = pt.y - mInnerBox.y;

  nsCOMPtr<nsITreeView> view = GetExistingView();
  *aRow = GetRowAtInternal(xTwips, yTwips);
  if (*aRow >= 0) {
    // Compute the top/bottom of the row in question.
    int32_t yOffset = yTwips - mRowHeight * (*aRow - mTopRowIndex);

    bool isContainer = false;
    view->IsContainer(*aRow, &isContainer);
    if (isContainer) {
      // for a container, use a 25%/50%/25% breakdown
      if (yOffset < mRowHeight / 4) {
        *aOrient = nsITreeView::DROP_BEFORE;
      } else if (yOffset > mRowHeight - (mRowHeight / 4)) {
        *aOrient = nsITreeView::DROP_AFTER;
      } else {
        *aOrient = nsITreeView::DROP_ON;
      }
    } else {
      // for a non-container use a 50%/50% breakdown
      if (yOffset < mRowHeight / 2) {
        *aOrient = nsITreeView::DROP_BEFORE;
      } else {
        *aOrient = nsITreeView::DROP_AFTER;
      }
    }
  }

  if (CanAutoScroll(*aRow)) {
    // Get the max value from the look and feel service.
    int32_t scrollLinesMax =
        LookAndFeel::GetInt(LookAndFeel::IntID::TreeScrollLinesMax, 0);
    scrollLinesMax--;
    if (scrollLinesMax < 0) {
      scrollLinesMax = 0;
    }

    // Determine if we're w/in a margin of the top/bottom of the tree during a
    // drag. This will ultimately cause us to scroll, but that's done elsewhere.
    nscoord height = (3 * mRowHeight) / 4;
    if (yTwips < height) {
      // scroll up
      *aScrollLines =
          NSToIntRound(-scrollLinesMax * (1 - (float)yTwips / height) - 1);
    } else if (yTwips > mRect.height - height) {
      // scroll down
      *aScrollLines = NSToIntRound(
          scrollLinesMax * (1 - (float)(mRect.height - yTwips) / height) + 1);
    }
  }
}  // ComputeDropPosition

void nsTreeBodyFrame::OpenCallback(nsITimer* aTimer, void* aClosure) {
  auto* self = static_cast<nsTreeBodyFrame*>(aClosure);
  if (!self) {
    return;
  }

  aTimer->Cancel();
  self->mSlots->mTimer = nullptr;

  nsCOMPtr<nsITreeView> view = self->GetExistingView();
  if (self->mSlots->mDropRow >= 0) {
    self->mSlots->mArray.AppendElement(self->mSlots->mDropRow);
    view->ToggleOpenState(self->mSlots->mDropRow);
  }
}

void nsTreeBodyFrame::CloseCallback(nsITimer* aTimer, void* aClosure) {
  auto* self = static_cast<nsTreeBodyFrame*>(aClosure);
  if (!self) {
    return;
  }

  aTimer->Cancel();
  self->mSlots->mTimer = nullptr;

  nsCOMPtr<nsITreeView> view = self->GetExistingView();
  auto array = std::move(self->mSlots->mArray);
  if (!view) {
    return;
  }
  for (auto elem : Reversed(array)) {
    view->ToggleOpenState(elem);
  }
}

void nsTreeBodyFrame::LazyScrollCallback(nsITimer* aTimer, void* aClosure) {
  nsTreeBodyFrame* self = static_cast<nsTreeBodyFrame*>(aClosure);
  if (self) {
    aTimer->Cancel();
    self->mSlots->mTimer = nullptr;

    if (self->mView) {
      // Set a new timer to scroll the tree repeatedly.
      self->CreateTimer(LookAndFeel::IntID::TreeScrollDelay, ScrollCallback,
                        nsITimer::TYPE_REPEATING_SLACK,
                        getter_AddRefs(self->mSlots->mTimer),
                        "nsTreeBodyFrame::ScrollCallback"_ns);
      self->ScrollByLines(self->mSlots->mScrollLines);
      // ScrollByLines may have deleted |self|.
    }
  }
}

void nsTreeBodyFrame::ScrollCallback(nsITimer* aTimer, void* aClosure) {
  nsTreeBodyFrame* self = static_cast<nsTreeBodyFrame*>(aClosure);
  if (self) {
    // Don't scroll if we are already at the top or bottom of the view.
    if (self->mView && self->CanAutoScroll(self->mSlots->mDropRow)) {
      self->ScrollByLines(self->mSlots->mScrollLines);
    } else {
      aTimer->Cancel();
      self->mSlots->mTimer = nullptr;
    }
  }
}

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP nsTreeBodyFrame::ScrollEvent::Run() {
  if (mInner) {
    mInner->FireScrollEvent();
  }
  return NS_OK;
}

void nsTreeBodyFrame::FireScrollEvent() {
  mScrollEvent.Forget();
  WidgetGUIEvent event(true, eScroll, nullptr);
  // scroll events fired at elements don't bubble
  event.mFlags.mBubbles = false;
  RefPtr<nsIContent> content = GetContent();
  RefPtr<nsPresContext> presContext = PresContext();
  EventDispatcher::Dispatch(content, presContext, &event);
}

void nsTreeBodyFrame::PostScrollEvent() {
  if (mScrollEvent.IsPending()) {
    return;
  }

  auto event = MakeRefPtr<ScrollEvent>(this);
  nsresult rv = mContent->OwnerDoc()->Dispatch(do_AddRef(event));
  if (NS_FAILED(rv)) {
    NS_WARNING("failed to dispatch ScrollEvent");
  } else {
    mScrollEvent = std::move(event);
  }
}

void nsTreeBodyFrame::ScrollbarActivityStarted() const {
  if (mScrollbarActivity) {
    mScrollbarActivity->ActivityStarted();
  }
}

void nsTreeBodyFrame::ScrollbarActivityStopped() const {
  if (mScrollbarActivity) {
    mScrollbarActivity->ActivityStopped();
  }
}

#ifdef ACCESSIBILITY
static void InitCustomEvent(CustomEvent* aEvent, const nsAString& aType,
                            nsIWritablePropertyBag2* aDetail) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(aEvent->GetParentObject())) {
    return;
  }

  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> detail(cx);
  if (!ToJSValue(cx, aDetail, &detail)) {
    jsapi.ClearException();
    return;
  }

  aEvent->InitCustomEvent(cx, aType, /* aCanBubble = */ true,
                          /* aCancelable = */ false, detail);
}

void nsTreeBodyFrame::FireRowCountChangedEvent(int32_t aIndex, int32_t aCount) {
  RefPtr<XULTreeElement> tree(GetBaseElement());
  if (!tree) {
    return;
  }

  RefPtr<Document> doc = tree->OwnerDoc();
  MOZ_ASSERT(doc);

  RefPtr<Event> event =
      doc->CreateEvent(u"customevent"_ns, CallerType::System, IgnoreErrors());

  CustomEvent* treeEvent = event->AsCustomEvent();
  if (!treeEvent) {
    return;
  }

  nsCOMPtr<nsIWritablePropertyBag2> propBag(
      do_CreateInstance("@mozilla.org/hash-property-bag;1"));
  if (!propBag) {
    return;
  }

  // Set 'index' data - the row index rows are changed from.
  propBag->SetPropertyAsInt32(u"index"_ns, aIndex);

  // Set 'count' data - the number of changed rows.
  propBag->SetPropertyAsInt32(u"count"_ns, aCount);

  InitCustomEvent(treeEvent, u"TreeRowCountChanged"_ns, propBag);

  event->SetTrusted(true);

  auto asyncDispatcher = MakeRefPtr<AsyncEventDispatcher>(tree, event.forget());
  asyncDispatcher->PostDOMEvent();
}

void nsTreeBodyFrame::FireInvalidateEvent(int32_t aStartRowIdx,
                                          int32_t aEndRowIdx,
                                          nsTreeColumn* aStartCol,
                                          nsTreeColumn* aEndCol) {
  RefPtr<XULTreeElement> tree(GetBaseElement());
  if (!tree) {
    return;
  }

  RefPtr<Document> doc = tree->OwnerDoc();

  RefPtr<Event> event =
      doc->CreateEvent(u"customevent"_ns, CallerType::System, IgnoreErrors());

  CustomEvent* treeEvent = event->AsCustomEvent();
  if (!treeEvent) {
    return;
  }

  nsCOMPtr<nsIWritablePropertyBag2> propBag(
      do_CreateInstance("@mozilla.org/hash-property-bag;1"));
  if (!propBag) {
    return;
  }

  if (aStartRowIdx != -1 && aEndRowIdx != -1) {
    // Set 'startrow' data - the start index of invalidated rows.
    propBag->SetPropertyAsInt32(u"startrow"_ns, aStartRowIdx);

    // Set 'endrow' data - the end index of invalidated rows.
    propBag->SetPropertyAsInt32(u"endrow"_ns, aEndRowIdx);
  }

  if (aStartCol && aEndCol) {
    // Set 'startcolumn' data - the start index of invalidated rows.
    int32_t startColIdx = aStartCol->GetIndex();

    propBag->SetPropertyAsInt32(u"startcolumn"_ns, startColIdx);

    // Set 'endcolumn' data - the start index of invalidated rows.
    int32_t endColIdx = aEndCol->GetIndex();
    propBag->SetPropertyAsInt32(u"endcolumn"_ns, endColIdx);
  }

  InitCustomEvent(treeEvent, u"TreeInvalidated"_ns, propBag);

  event->SetTrusted(true);

  auto asyncDispatcher = MakeRefPtr<AsyncEventDispatcher>(tree, event.forget());
  asyncDispatcher->PostDOMEvent();
}
#endif

class nsOverflowChecker : public Runnable {
 public:
  explicit nsOverflowChecker(nsTreeBodyFrame* aFrame)
      : mozilla::Runnable("nsOverflowChecker"), mFrame(aFrame) {}
  NS_IMETHOD Run() override {
    if (mFrame.IsAlive()) {
      nsTreeBodyFrame* tree = static_cast<nsTreeBodyFrame*>(mFrame.GetFrame());
      nsTreeBodyFrame::ScrollParts parts = tree->GetScrollParts();
      tree->CheckOverflow(parts);
    }
    return NS_OK;
  }

 private:
  WeakFrame mFrame;
};

bool nsTreeBodyFrame::FullScrollbarsUpdate(bool aNeedsFullInvalidation) {
  ScrollParts parts = GetScrollParts();
  AutoWeakFrame weakFrame(this);
  UpdateScrollbars(parts);
  NS_ENSURE_TRUE(weakFrame.IsAlive(), false);
  if (aNeedsFullInvalidation) {
    Invalidate();
  }
  InvalidateScrollbars(parts);
  NS_ENSURE_TRUE(weakFrame.IsAlive(), false);

  // Overflow checking dispatches synchronous events, which can cause infinite
  // recursion during reflow. Do the first overflow check synchronously, but
  // force any nested checks to round-trip through the event loop. See bug
  // 905909.
  auto checker = MakeRefPtr<nsOverflowChecker>(this);
  if (!mCheckingOverflow) {
    nsContentUtils::AddScriptRunner(checker);
  } else {
    mContent->OwnerDoc()->Dispatch(checker.forget());
  }
  return weakFrame.IsAlive();
}

void nsTreeBodyFrame::OnImageIsAnimated(imgIRequest* aRequest) {
  nsLayoutUtils::RegisterImageRequest(PresContext(), aRequest, nullptr);
}
