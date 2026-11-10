/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object that is the root of the frame tree, which contains
 * contains fixed-positioned elements
 */

#ifndef mozilla_ViewportFrame_h
#define mozilla_ViewportFrame_h

#include "nsContainerFrame.h"

class nsPresContext;

namespace mozilla {

class nsDisplayWrapList;
class ServoRestyleState;

/**
 * ViewportFrame is the parent of a single child -- the root canvas frame or a
 * scroll container frame containing the root canvas frame. See
 * nsCSSFrameConstructor::SetUpDocElementContainingBlock() for the root frame
 * hierarchy. ViewportFrame stores this child in its primary child list.
 */
class ViewportFrame : public nsContainerFrame {
 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(ViewportFrame)

  explicit ViewportFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : ViewportFrame(aStyle, aPresContext, kClassID) {}

  virtual ~ViewportFrame() = default;  // useful for debugging

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;

#ifdef DEBUG
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;
#endif

  void Destroy(DestroyContext&) override;
  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  nsDisplayWrapList* BuildDisplayListForContentTopLayer(
      nsDisplayListBuilder* aBuilder, bool* aIsOpaque = nullptr);

  nsDisplayWrapList* BuildDisplayListForViewTransitionsAndNACTopLayer(
      nsDisplayListBuilder* aBuilder);

  nscoord IntrinsicISize(const IntrinsicSizeInput& aInput,
                         IntrinsicISizeType aType) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  bool ComputeCustomOverflow(mozilla::OverflowAreas&) override { return false; }

  /**
   * Return true if aKidFrame is a fixed-pos frame that should cover the root
   * scrollbars and scrollbar-gutter.
   */
  static bool ShouldCoverScrollbars(const nsIFrame* aKidFrame);

  /**
   * Get the containing block rect when ViewportFrame serves as a containing
   * block for aKidFrame. This method accounts for scrollbars, visual viewport,
   * and dynamic toolbar sizes.
   */
  nsRect GetContainingBlockAdjustedForScrollbars(
      const ReflowInput& aReflowInput, const nsIFrame* aKidFrame) const;

  /**
   * Return our single anonymous box child.
   */
  void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) override;

  // Returns adjusted viewport size to reflect the positions that position:fixed
  // elements are attached.
  nsSize AdjustViewportSizeForFixedPosition(const nsRect& aViewportRect) const;

#ifdef DEBUG_FRAME_DUMP
  virtual nsresult GetFrameName(nsAString& aResult) const override;
#endif

 protected:
  ViewportFrame(ComputedStyle* aStyle, nsPresContext* aPresContext, ClassID aID)
      : nsContainerFrame(aStyle, aPresContext, aID) {}

 private:
  nsDisplayWrapList* MaybeWrapTopLayerList(nsDisplayListBuilder*,
                                           uint16_t aIndex, nsDisplayList&);
};

}  // namespace mozilla

#endif  // mozilla_ViewportFrame_h
