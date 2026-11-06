/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTreeBodyFrame_h
#define nsTreeBodyFrame_h

#include "SimpleXULLeafFrame.h"
#include "imgINotificationObserver.h"
#include "imgIRequest.h"
#include "mozilla/AtomArray.h"
#include "mozilla/Attributes.h"
#include "mozilla/LookAndFeel.h"
#include "nsIReflowCallback.h"
#include "nsIScrollbarMediator.h"
#include "nsITimer.h"
#include "nsITreeView.h"
#include "nsScrollbarFrame.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsThreadUtils.h"
#include "nsTreeColumns.h"
#include "nsTreeStyleCache.h"

class nsFontMetrics;
class nsOverflowChecker;
class nsTreeImageListener;

namespace mozilla {
class PresShell;
class ScrollContainerFrame;
namespace layout {
class ScrollbarActivity;
}  // namespace layout
}  // namespace mozilla

// An entry in the tree's image cache
struct nsTreeImageCacheEntry {
  nsTreeImageCacheEntry();
  nsTreeImageCacheEntry(imgIRequest* aRequest, nsTreeImageListener* aListener);
  ~nsTreeImageCacheEntry();

  nsCOMPtr<imgIRequest> request;
  RefPtr<nsTreeImageListener> listener;
};

// The actual frame that paints the cells and rows.
class nsTreeBodyFrame final : public mozilla::SimpleXULLeafFrame,
                              public nsIScrollbarMediator,
                              public nsIReflowCallback {
  typedef mozilla::layout::ScrollbarActivity ScrollbarActivity;
  typedef mozilla::image::ImgDrawResult ImgDrawResult;

 public:
  explicit nsTreeBodyFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);
  ~nsTreeBodyFrame();

  mozilla::IntrinsicSize GetIntrinsicSize() override;

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsTreeBodyFrame)

  // Callback handler methods for refresh driver based animations.
  // Calls to these functions are forwarded from nsTreeImageListener. These
  // mirror how nsImageFrame works.
  void OnImageIsAnimated(imgIRequest* aRequest);

  // non-virtual signatures like nsITreeBodyFrame
  already_AddRefed<nsTreeColumns> Columns() const {
    RefPtr<nsTreeColumns> cols = mColumns;
    return cols.forget();
  }
  already_AddRefed<nsITreeView> GetExistingView() const {
    nsCOMPtr<nsITreeView> view = mView;
    return view.forget();
  }
  already_AddRefed<nsITreeSelection> GetSelection() const;
  nsresult GetView(nsITreeView** aView);
  nsresult SetView(nsITreeView* aView);
  bool GetFocused() const { return mFocused; }
  nsresult SetFocused(bool aFocused);
  nsresult GetTreeBody(mozilla::dom::Element** aElement);
  int32_t RowHeight() const;
  int32_t RowWidth();
  mozilla::Maybe<mozilla::CSSIntRegion> GetSelectionRegion();
  int32_t FirstVisibleRow() const { return mTopRowIndex; }
  int32_t LastVisibleRow() const { return mTopRowIndex + mPageLength; }
  int32_t PageLength() const { return mPageLength; }
  nsresult EnsureRowIsVisible(int32_t aRow);
  nsresult EnsureCellIsVisible(int32_t aRow, nsTreeColumn* aCol);
  void ScrollToRow(int32_t aRow);
  void ScrollByLines(int32_t aNumLines);
  void ScrollByPages(int32_t aNumPages);
  nsresult Invalidate();
  nsresult InvalidateColumn(nsTreeColumn* aCol);
  nsresult InvalidateRow(int32_t aRow);
  nsresult InvalidateCell(int32_t aRow, nsTreeColumn* aCol);
  nsresult InvalidateRange(int32_t aStart, int32_t aEnd);
  int32_t GetRowAt(int32_t aX, int32_t aY);
  nsresult GetCellAt(int32_t aX, int32_t aY, int32_t* aRow, nsTreeColumn** aCol,
                     nsACString& aChildElt);
  nsresult GetCoordsForCellItem(int32_t aRow, nsTreeColumn* aCol,
                                const nsACString& aElt, int32_t* aX,
                                int32_t* aY, int32_t* aWidth, int32_t* aHeight);
  nsresult IsCellCropped(int32_t aRow, nsTreeColumn* aCol, bool* aResult);
  nsresult RowCountChanged(int32_t aIndex, int32_t aCount);
  nsresult BeginUpdateBatch();
  nsresult EndUpdateBatch();
  nsresult ClearStyleAndImageCaches();
  void RemoveImageCacheEntry(int32_t aRowIndex, nsTreeColumn* aCol);

  void CancelImageRequests();

  void ManageReflowCallback();

  void DidReflow(nsPresContext*, const ReflowInput*) override;

  // nsIReflowCallback
  bool ReflowFinished() override;
  void ReflowCallbackCanceled() override;

  // nsIScrollbarMediator
  void ScrollByPage(nsScrollbarFrame* aScrollbar, int32_t aDirection,
                    mozilla::ScrollSnapFlags aSnapFlags =
                        mozilla::ScrollSnapFlags::Disabled) override;
  void ScrollByWhole(nsScrollbarFrame* aScrollbar, int32_t aDirection,
                     mozilla::ScrollSnapFlags aSnapFlags =
                         mozilla::ScrollSnapFlags::Disabled) override;
  void ScrollByLine(nsScrollbarFrame* aScrollbar, int32_t aDirection,
                    mozilla::ScrollSnapFlags aSnapFlags =
                        mozilla::ScrollSnapFlags::Disabled) override;
  void ScrollByUnit(nsScrollbarFrame* aScrollbar, mozilla::ScrollMode aMode,
                    int32_t aDirection, mozilla::ScrollUnit aUnit,
                    mozilla::ScrollSnapFlags aSnapFlags =
                        mozilla::ScrollSnapFlags::Disabled) override;
  void RepeatButtonScroll(nsScrollbarFrame* aScrollbar) override;
  void ThumbMoved(nsScrollbarFrame* aScrollbar, nscoord aOldPos,
                  nscoord aNewPos) override;
  void ScrollbarReleased(nsScrollbarFrame* aScrollbar) override {}
  void VisibilityChanged(bool aVisible) override { Invalidate(); }
  nsScrollbarFrame* GetScrollbarBox(bool aVertical) override {
    ScrollParts parts = GetScrollParts();
    return aVertical ? parts.mVScrollbar : nullptr;
  }
  void ScrollbarActivityStarted() const override;
  void ScrollbarActivityStopped() const override;
  bool IsScrollbarOnRight() const override {
    return StyleVisibility()->mDirection == mozilla::StyleDirection::Ltr;
  }
  bool ShouldSuppressScrollbarRepaints() const override { return false; }

  // Overridden from nsIFrame to cache our pres context.
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
  void Destroy(DestroyContext&) override;

  Cursor GetCursor(const nsPoint&) override;

  nsresult HandleEvent(nsPresContext* aPresContext,
                       mozilla::WidgetGUIEvent* aEvent,
                       nsEventStatus* aEventStatus) override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  friend nsIFrame* NS_NewTreeBodyFrame(mozilla::PresShell* aPresShell);
  friend class nsTreeColumn;

  struct ScrollParts {
    nsScrollbarFrame* mVScrollbar = nullptr;
    RefPtr<mozilla::dom::Element> mVScrollbarContent;
  };

  ImgDrawResult PaintTreeBody(gfxContext& aRenderingContext,
                              const nsRect& aDirtyRect, nsPoint aPt,
                              nsDisplayListBuilder* aBuilder);

  // Get the base element, <tree>
  mozilla::dom::XULTreeElement* GetBaseElement();

  bool GetVerticalOverflow() const { return mVerticalOverflow; }

  // This returns the property array where atoms are stored for style during
  // draw, whether the row currently being drawn is selected, hovered, etc.
  const mozilla::AtomArray& GetPropertyArrayForCurrentDrawingItem() {
    return mScratchArray;
  }

 protected:
  friend class nsOverflowChecker;

  // This method paints a specific column background of the tree.
  ImgDrawResult PaintColumn(nsTreeColumn* aColumn, const nsRect& aColumnRect,
                            nsPresContext* aPresContext,
                            gfxContext& aRenderingContext,
                            const nsRect& aDirtyRect);

  // This method paints a single row in the tree.
  ImgDrawResult PaintRow(int32_t aRowIndex, const nsRect& aRowRect,
                         nsPresContext* aPresContext,
                         gfxContext& aRenderingContext,
                         const nsRect& aDirtyRect, nsPoint aPt,
                         nsDisplayListBuilder* aBuilder);

  // This method paints a single separator in the tree.
  ImgDrawResult PaintSeparator(int32_t aRowIndex, const nsRect& aSeparatorRect,
                               nsPresContext* aPresContext,
                               gfxContext& aRenderingContext,
                               const nsRect& aDirtyRect);

  // This method paints a specific cell in a given row of the tree.
  ImgDrawResult PaintCell(int32_t aRowIndex, nsTreeColumn* aColumn,
                          const nsRect& aCellRect, nsPresContext* aPresContext,
                          gfxContext& aRenderingContext,
                          const nsRect& aDirtyRect, nscoord& aCurrX,
                          nsPoint aPt, nsDisplayListBuilder* aBuilder);

  // This method paints the twisty inside a cell in the primary column of an
  // tree.
  ImgDrawResult PaintTwisty(int32_t aRowIndex, nsTreeColumn* aColumn,
                            const nsRect& aTwistyRect,
                            nsPresContext* aPresContext,
                            gfxContext& aRenderingContext,
                            const nsRect& aDirtyRect, nscoord& aRemainingWidth,
                            nscoord& aCurrX);

  // This method paints the image inside the cell of an tree.
  ImgDrawResult PaintImage(int32_t aRowIndex, nsTreeColumn* aColumn,
                           const nsRect& aImageRect,
                           nsPresContext* aPresContext,
                           gfxContext& aRenderingContext,
                           const nsRect& aDirtyRect, nscoord& aRemainingWidth,
                           nscoord& aCurrX, nsDisplayListBuilder* aBuilder);

  // This method paints the text string inside a particular cell of the tree.
  ImgDrawResult PaintText(int32_t aRowIndex, nsTreeColumn* aColumn,
                          const nsRect& aTextRect, nsPresContext* aPresContext,
                          gfxContext& aRenderingContext,
                          const nsRect& aDirtyRect, nscoord& aCurrX);

  // This method paints the checkbox inside a particular cell of the tree.
  ImgDrawResult PaintCheckbox(int32_t aRowIndex, nsTreeColumn* aColumn,
                              const nsRect& aCheckboxRect,
                              nsPresContext* aPresContext,
                              gfxContext& aRenderingContext,
                              const nsRect& aDirtyRect);

  // This method paints a drop feedback of the tree.
  ImgDrawResult PaintDropFeedback(const nsRect& aDropFeedbackRect,
                                  nsPresContext* aPresContext,
                                  gfxContext& aRenderingContext,
                                  const nsRect& aDirtyRect, nsPoint aPt);

  // This method is called with a specific ComputedStyle and rect to
  // paint the background rect as if it were a full-blown frame.
  ImgDrawResult PaintBackgroundLayer(ComputedStyle* aComputedStyle,
                                     nsPresContext* aPresContext,
                                     gfxContext& aRenderingContext,
                                     const nsRect& aRect,
                                     const nsRect& aDirtyRect);

  // An internal hit test.  aX and aY are expected to be in twips in the
  // coordinate system of this frame.
  int32_t GetRowAtInternal(nscoord aX, nscoord aY);

  // Check for bidi characters in the text, and if there are any, ensure
  // that the prescontext is in bidi mode.
  void CheckTextForBidi(nsAutoString& aText);

  void AdjustForCellText(nsAutoString& aText, int32_t aRowIndex,
                         nsTreeColumn* aColumn, gfxContext& aRenderingContext,
                         nsFontMetrics& aFontMetrics, nsRect& aTextRect);

  // A helper used when hit testing.
  mozilla::PseudoStyleType GetItemWithinCellAt(nscoord aX,
                                               const nsRect& aCellRect,
                                               int32_t aRowIndex,
                                               nsTreeColumn* aColumn);

  // An internal hit test.  aX and aY are expected to be in twips in the
  // coordinate system of this frame.
  void GetCellAt(nscoord aX, nscoord aY, int32_t* aRow, nsTreeColumn** aCol,
                 mozilla::PseudoStyleType* aChildElt);

  // Retrieve the area for the twisty for a cell.
  void GetTwistyRect(int32_t aRowIndex, nsTreeColumn* aColumn,
                     nsRect& aImageRect, nsRect& aTwistyRect,
                     nsPresContext* aPresContext,
                     ComputedStyle* aTwistyContext);

  // Fetch an image from the image cache, or request it.
  already_AddRefed<imgIContainer> GetImage(int32_t aRowIndex,
                                           nsTreeColumn* aCol, bool aUseContext,
                                           ComputedStyle* aComputedStyle);

  // Returns the size of a given image.   This size *includes* border and
  // padding.  It does not include margins.
  nsRect GetImageSize(int32_t aRowIndex, nsTreeColumn* aCol, bool aUseContext,
                      ComputedStyle* aComputedStyle);

  // Returns the destination size of the image, not including borders and
  // padding.
  nsSize GetImageDestSize(ComputedStyle*, imgIContainer*);

  // Returns the source rectangle of the image to be displayed.
  nsRect GetImageSourceRect(ComputedStyle*, imgIContainer*);

  // Returns the height of rows in the tree.
  int32_t GetRowHeight();

  // Returns our indentation width.
  int32_t GetIndentation();

  // Calculates our width/height once border and padding have been removed.
  void CalcInnerBox();

  // Looks up a ComputedStyle in the style cache.  On a cache miss we resolve
  // the pseudo-styles passed in and place them into the cache.
  ComputedStyle* GetPseudoComputedStyle(
      mozilla::PseudoStyleType aPseudoElement);

  // Retrieves the scrollbars and scrollview relevant to this treebody. We
  // traverse the frame tree under our base element, in frame order, looking
  // for the first relevant vertical scrollbar, horizontal scrollbar, and
  // scrollable frame (with associated content and scrollable view). These
  // are all volatile and should not be retained.
  ScrollParts GetScrollParts();

  // Update the curpos of the scrollbar.
  void UpdateScrollbars(const ScrollParts& aParts);

  // Update the maxpos of the scrollbar.
  void InvalidateScrollbars(const ScrollParts& aParts);

  // Check overflow and generate events.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void CheckOverflow(const ScrollParts& aParts);

  // Calls UpdateScrollbars, Invalidate aNeedsFullInvalidation if true,
  // InvalidateScrollbars and finally CheckOverflow.
  // returns true if the frame is still alive after the method call.
  bool FullScrollbarsUpdate(bool aNeedsFullInvalidation);

  // Use to auto-fill some of the common properties without the view having to
  // do it. Examples include container, open, selected, and focus.
  void PrefillPropertyArray(int32_t aRowIndex, nsTreeColumn* aCol);

  // Our internal scroll method, used by all the public scroll methods.
  nsresult ScrollInternal(const ScrollParts& aParts, int32_t aRow);
  nsresult ScrollToRowInternal(const ScrollParts& aParts, int32_t aRow);
  nsresult EnsureRowIsVisibleInternal(const ScrollParts& aParts, int32_t aRow);

  // Convert client pixels into appunits in our coordinate space.
  nsPoint AdjustClientCoordsToBoxCoordSpace(int32_t aX, int32_t aY);

  void EnsureView();

  nsresult GetCellWidth(int32_t aRow, nsTreeColumn* aCol,
                        gfxContext* aRenderingContext, nscoord& aDesiredSize,
                        nscoord& aCurrentSize);

  // Translate the given rect horizontally from tree coordinates into the
  // coordinate system of our nsTreeBodyFrame.  If clip is true, then clip the
  // rect to its intersection with mInnerBox in the horizontal direction.
  // Return whether the result has a nonempty intersection with mInnerBox
  // after projecting both onto the horizontal coordinate axis.
  bool OffsetForHorzScroll(nsRect& rect, bool clip);

  bool CanAutoScroll(int32_t aRowIndex);

  // Calc the row and above/below/on status given where the mouse currently is
  // hovering. Also calc if we're in the region in which we want to auto-scroll
  // the tree. A positive value of |aScrollLines| means scroll down, a negative
  // value means scroll up, a zero value means that we aren't in drag scroll
  // region.
  void ComputeDropPosition(mozilla::WidgetGUIEvent* aEvent, int32_t* aRow,
                           int16_t* aOrient, int16_t* aScrollLines);

  void InvalidateDropFeedback(int32_t aRow, int16_t aOrientation) {
    InvalidateRow(aRow);
    if (aOrientation != nsITreeView::DROP_ON) {
      InvalidateRow(aRow + aOrientation);
    }
  }

 protected:
  // Create a new timer. This method is used to delay various actions like
  // opening/closing folders or tree scrolling.
  // aID is type of the action, aFunc is the function to be called when
  // the timer fires and aType is type of timer - one shot or repeating.
  nsresult CreateTimer(const mozilla::LookAndFeel::IntID aID,
                       nsTimerCallbackFunc aFunc, int32_t aType,
                       nsITimer** aTimer, const nsACString& aName);

  static void OpenCallback(nsITimer* aTimer, void* aClosure);

  static void CloseCallback(nsITimer* aTimer, void* aClosure);

  static void LazyScrollCallback(nsITimer* aTimer, void* aClosure);

  static void ScrollCallback(nsITimer* aTimer, void* aClosure);

  class ScrollEvent : public mozilla::Runnable {
   public:
    NS_DECL_NSIRUNNABLE
    explicit ScrollEvent(nsTreeBodyFrame* aInner)
        : mozilla::Runnable("nsTreeBodyFrame::ScrollEvent"), mInner(aInner) {}
    void Revoke() { mInner = nullptr; }

   private:
    nsTreeBodyFrame* mInner;
  };

  void PostScrollEvent();
  MOZ_CAN_RUN_SCRIPT void FireScrollEvent();

#ifdef ACCESSIBILITY
  /**
   * Fires 'treeRowCountChanged' event asynchronously. The event is a
   * CustomEvent that is used to expose the following information structures
   * via a property bag.
   *
   * @param aIndex  the row index rows are added/removed from
   * @param aCount  the number of added/removed rows (the sign points to
   *                an operation, plus - addition, minus - removing)
   */
  void FireRowCountChangedEvent(int32_t aIndex, int32_t aCount);

  /**
   * Fires 'treeInvalidated' event asynchronously. The event is a CustomEvent
   * that is used to expose the information structures described by method
   * arguments via a property bag.
   *
   * @param aStartRow  the start index of invalidated rows, -1 means that
   *                   columns have been invalidated only
   * @param aEndRow    the end index of invalidated rows, -1 means that columns
   *                   have been invalidated only
   * @param aStartCol  the start invalidated column, nullptr means that only
   *                   rows have been invalidated
   * @param aEndCol    the end invalidated column, nullptr means that rows have
   *                   been invalidated only
   */
  void FireInvalidateEvent(int32_t aStartRow, int32_t aEndRow,
                           nsTreeColumn* aStartCol, nsTreeColumn* aEndCol);
#endif

 protected:  // Data Members
  class Slots {
   public:
    Slots() = default;

    ~Slots() {
      if (mTimer) {
        mTimer->Cancel();
      }
    }

    friend class nsTreeBodyFrame;

   protected:
    // If the drop is actually allowed here or not.
    bool mDropAllowed = false;

    // True while dragging over the tree.
    bool mIsDragging = false;

    // The row the mouse is hovering over during a drop.
    int32_t mDropRow = -1;

    // Where we want to draw feedback (above/on this row/below) if allowed.
    int16_t mDropOrient = -1;

    // Number of lines to be scrolled.
    int16_t mScrollLines = 0;

    // The drag action that was received for this slot
    uint32_t mDragAction = 0;

    // Timer for opening/closing spring loaded folders or scrolling the tree.
    nsCOMPtr<nsITimer> mTimer;

    // An array used to keep track of all spring loaded folders.
    nsTArray<int32_t> mArray;
  };

  mozilla::UniquePtr<Slots> mSlots;

  nsRevocableEventPtr<ScrollEvent> mScrollEvent;

  RefPtr<ScrollbarActivity> mScrollbarActivity;

  // The <tree> element containing this treebody.
  RefPtr<mozilla::dom::XULTreeElement> mTree;

  // Cached column information.
  RefPtr<nsTreeColumns> mColumns;

  // The current view for this tree widget.  We get all of our row and cell data
  // from the view.
  nsCOMPtr<nsITreeView> mView;

  // A cache of all the ComputedStyles we have seen for rows and cells of the
  // tree.  This is a mapping from a list of atoms to a corresponding
  // ComputedStyle.  This cache stores every combination that occurs in the
  // tree, so for n distinct properties, this cache could have 2 to the n
  // entries (the power set of all row properties).
  nsTreeStyleCache mStyleCache;

  // A hashtable that maps from URLs to image request/listener pairs.  The URL
  // is provided by the view or by the ComputedStyle. The ComputedStyle
  // represents a resolved :-moz-tree-cell-image (or twisty) pseudo-element.
  nsTHashMap<nsURIHashKey, nsTreeImageCacheEntry> mImageCache;

  // A scratch array used when looking up cached ComputedStyles.
  mozilla::AtomArray mScratchArray;

  // The index of the first visible row and the # of rows visible onscreen.
  // The tree only examines onscreen rows, starting from
  // this index and going up to index+pageLength.
  int32_t mTopRowIndex;
  int32_t mPageLength;

  // The horizontal scroll position
  nscoord mHorzPosition;

  // The original desired horizontal width before changing it and posting a
  // reflow callback. In some cases, the desired horizontal width can first be
  // different from the current desired horizontal width, only to return to
  // the same value later during the same reflow. In this case, we can cancel
  // the posted reflow callback and prevent an unnecessary reflow.
  nscoord mOriginalHorzWidth;
  // Our desired horizontal width (the width for which we actually have tree
  // columns).
  nscoord mHorzWidth;

  // Our last reflowed rect, used for invalidation, see ManageReflowCallback().
  Maybe<nsRect> mLastReflowRect;

  // Cached heights and indent info.
  nsRect mInnerBox;  // 4-byte aligned
  int32_t mRowHeight;
  int32_t mIndentation;

  int32_t mUpdateBatchNest;

  // Cached row count.
  int32_t mRowCount;

  // The row the mouse is hovering over.
  int32_t mMouseOverRow;

  // Whether or not we're currently focused.
  bool mFocused;

  // Do we have a fixed number of onscreen rows?
  bool mHasFixedRowCount;

  bool mVerticalOverflow;

  bool mReflowCallbackPosted;

  // Set while we flush layout to take account of effects of overflow/underflow
  // event handlers
  bool mCheckingOverflow;
};  // class nsTreeBodyFrame

#endif
