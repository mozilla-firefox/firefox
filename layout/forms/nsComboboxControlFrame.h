/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsComboboxControlFrame_h_
#define nsComboboxControlFrame_h_

#include "ButtonControlFrame.h"
#include "mozilla/Attributes.h"

namespace mozilla {
class PresShell;
class ComboboxLabelFrame;
namespace dom {
class HTMLSelectElement;
}
}  // namespace mozilla

class nsComboboxControlFrame final : public mozilla::ButtonControlFrame {
  using Element = mozilla::dom::Element;

 public:
  friend class mozilla::ComboboxLabelFrame;
  nsComboboxControlFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);
  ~nsComboboxControlFrame();

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsComboboxControlFrame)

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() final;
#endif

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) final;

  void Reflow(nsPresContext* aCX, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput, nsReflowStatus& aStatus) final;

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult HandleEvent(nsPresContext* aPresContext,
                       mozilla::WidgetGUIEvent* aEvent,
                       nsEventStatus* aEventStatus) final;

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) final;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Destroy(DestroyContext&) final;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const final {
    return MakeFrameName(u"ComboboxControl"_ns, aResult);
  }
#endif

  bool IsDroppedDown() const;
  // Return true if we should render a dropdown button.
  bool HasDropDownButton() const;

  int32_t CharCountOfLargestOptionForInflation() const;

 protected:
  nscoord DropDownButtonISize();
  nscoord GetLongestOptionISize(gfxContext*) const;

  mozilla::dom::HTMLSelectElement& Select() const;
  void GetOptionText(uint32_t aIndex, nsAString& aText) const;

  // The inline size of our display area. Used by that frame's reflow to size to
  // the full inline size except the drop-marker.
  nscoord mDisplayISize = 0;
};

#endif
