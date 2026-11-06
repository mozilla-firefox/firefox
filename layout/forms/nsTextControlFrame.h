/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTextControlFrame_h_
#define nsTextControlFrame_h_

#include "mozilla/Attributes.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/TextControlElement.h"

namespace mozilla {
enum class PseudoStyleType : uint8_t;
namespace dom {
class Element;
}  // namespace dom
}  // namespace mozilla

class nsTextControlFrame final : public mozilla::ScrollContainerFrame {
  using Element = mozilla::dom::Element;

 public:
  NS_DECL_FRAMEARENA_HELPERS(nsTextControlFrame)

  nsTextControlFrame(ComputedStyle*, nsPresContext*);

  // nsIAnonymousContentCreator
  nsresult CreateAnonymousContent(nsTArray<ContentInfo>&) override;
  void AppendAnonymousContentTo(nsTArray<nsIContent*>&,
                                uint32_t aFilter) override;

  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  virtual ~nsTextControlFrame();

  /**
   * Destroy() causes preparing to destroy editor and that may cause running
   * selection listeners of spellchecker selection and document state listeners.
   * Not sure whether the former does something or not, but nobody should run
   * content script.  The latter is currently only FinderHighlighter to clean up
   * its fields at destruction.  Thus, the latter won't run content script too.
   * Therefore, this won't run unsafe script.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Destroy(DestroyContext&) override;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext aExportContext) const override;

  BaselineSharingGroup GetDefaultBaselineSharingGroup() const override {
    return BaselineSharingGroup::Last;
  }

  static Maybe<nscoord> GetSingleLineTextControlBaseline(
      const nsIFrame* aFrame, nscoord aFirstBaseline, mozilla::WritingMode aWM,
      BaselineSharingGroup aBaselineGroup) {
    if (aFrame->StyleDisplay()->IsContainLayout()) {
      return Nothing{};
    }
    NS_ASSERTION(aFirstBaseline != NS_INTRINSIC_ISIZE_UNKNOWN,
                 "please call Reflow before asking for the baseline");
    return mozilla::Some(aBaselineGroup == BaselineSharingGroup::First
                             ? aFirstBaseline
                             : aFrame->BSize(aWM) - aFirstBaseline);
  }

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    aResult.AssignLiteral("nsTextControlFrame");
    return NS_OK;
  }
#endif

  nsFrameSelection* GetOwnedFrameSelection() {
    return ControlElement()->GetIndependentFrameSelection();
  }

  void InitPrimaryFrame() override;

  void ElementStateChanged(mozilla::dom::ElementState aStates) override;

  nsresult PeekOffset(mozilla::PeekOffsetStruct* aPos) override;

  NS_DECL_QUERYFRAME

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void HandleReadonlyOrDisabledChange();

 public:
  static Maybe<nscoord> ComputeBaseline(const nsIFrame*, const ReflowInput&,
                                        bool aForSingleLineControl);

  Element* GetButton() const;
  nsIFrame* GetButtonBoxFrame() const override;

  bool IsButtonBox(const nsIFrame* aFrame) const {
    return mozilla::TextControlElement::IsButtonPseudoElement(
        aFrame->Style()->GetPseudoType());
  }

  mozilla::TextControlElement* ControlElement() const {
    MOZ_ASSERT(mozilla::TextControlElement::FromNode(GetContent()));
    return static_cast<mozilla::TextControlElement*>(GetContent());
  }

#define DEFINE_TEXTCTRL_CONST_FORWARDER(type, name) \
  type name() const { return ControlElement()->name(); }

  DEFINE_TEXTCTRL_CONST_FORWARDER(bool, IsSingleLineTextControl)
  DEFINE_TEXTCTRL_CONST_FORWARDER(bool, IsTextArea)
  DEFINE_TEXTCTRL_CONST_FORWARDER(bool, IsPasswordTextControl)
  DEFINE_TEXTCTRL_CONST_FORWARDER(Maybe<int32_t>, GetCols)
  DEFINE_TEXTCTRL_CONST_FORWARDER(int32_t, GetColsOrDefault)
  DEFINE_TEXTCTRL_CONST_FORWARDER(int32_t, GetRows)

#undef DEFINE_TEXTCTRL_CONST_FORWARDER

 protected:
  // Compute our fixed size (our intrinsic size if we have field-sizing: fixed,
  // i.e. not considering our content itself).
  mozilla::LogicalSize CalcFixedSize(gfxContext*, mozilla::WritingMode) const;

  // Our first baseline, or NS_INTRINSIC_ISIZE_UNKNOWN if we have a pending
  // Reflow (or if we're contain:layout, which means we have no baseline).
  nscoord mFirstBaseline = NS_INTRINSIC_ISIZE_UNKNOWN;

  // The button element (spin-box, reveal, clear) created as anonymous content.
  RefPtr<Element> mButtonContent;
};

#endif
