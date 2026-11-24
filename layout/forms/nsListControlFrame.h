/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsListControlFrame_h_
#define nsListControlFrame_h_

#include "mozilla/Attributes.h"
#include "mozilla/ScrollContainerFrame.h"

class nsComboboxControlFrame;
class nsPresContext;

namespace mozilla {
class PresShell;

namespace dom {
class HTMLOptionElement;
class HTMLSelectElement;
}  // namespace dom
}  // namespace mozilla

/**
 * Frame-based listbox.
 */

class nsListControlFrame final : public mozilla::ScrollContainerFrame {
 public:
  using HTMLOptionElement = mozilla::dom::HTMLOptionElement;

  friend nsListControlFrame* NS_NewListControlFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle);

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsListControlFrame)

  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext) const override;

  // nsIFrame
  nsresult HandleEvent(nsPresContext* aPresContext,
                       mozilla::WidgetGUIEvent* aEvent,
                       nsEventStatus* aEventStatus) final;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) final;

  void Reflow(nsPresContext* aCX, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput, nsReflowStatus& aStatus) final;

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) final;

  mozilla::dom::HTMLOptionElement* GetCurrentOption() const;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const final;
#endif

  void ElementStateChanged(mozilla::dom::ElementState aStates) final;

  // for accessibility purposes
#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() final;
#endif

  /**
   * Gets the text of the currently selected item.
   * If the there are zero items then an empty string is returned
   * If there is nothing selected, then the 0th item's text is returned.
   */
  void GetOptionText(uint32_t aIndex, nsAString& aStr);

  nscoord GetBSizeOfARow();

  /**
   * Returns the HTMLOptionElement for a given index in mContent's collection.
   */
  HTMLOptionElement* GetOption(uint32_t aIndex) const;

  // Helper
  bool IsFocused() const;

  /**
   * Must be called every time the focused option changes.
   */
  void InvalidateFocus();

  /**
   * Function to calculate the block size of a row, for use with the
   * "size" attribute.
   * Can't be const because GetNumberOfOptions() isn't const.
   */
  nscoord CalcBSizeOfARow();

  /**
   * Function to ask whether we're currently in what might be the
   * first pass of a two-pass reflow.
   */
  bool MightNeedSecondPass() const { return mMightNeedSecondPass; }

  /**
   * Return the number of displayed rows in the list.
   */
  uint32_t GetNumDisplayRows() const { return mNumDisplayRows; }

 protected:
  mozilla::dom::HTMLSelectElement& Select() const;
  MOZ_CAN_RUN_SCRIPT void ScrollToFrame(HTMLOptionElement& aOptElement);
  nsListControlFrame(ComputedStyle*, nsPresContext*);
  virtual ~nsListControlFrame();

  // guess at a row block size based on our own style.
  nscoord CalcFallbackRowBSize(float aFontSizeInflation);

  // CalcIntrinsicBSize computes our intrinsic block size (taking the
  // "size" attribute into account).  This should only be called in
  // non-dropdown mode.
  nscoord CalcIntrinsicBSize(nscoord aBSizeOfARow, int32_t aNumberOfOptions);

  nscoord BSizeOfARow() const { return mBSizeOfARow; }

  /**
   * @return how many displayable options/optgroups this frame has.
   */
  uint32_t GetNumberOfRows();

  // Data Members
  uint32_t mNumDisplayRows = 0;
  nscoord mBSizeOfARow = -1;

  // True if we're in the middle of a reflow and might need a second
  // pass.  This only happens for auto heights.
  bool mMightNeedSecondPass : 1 = false;
};

#endif /* nsListControlFrame_h_ */
