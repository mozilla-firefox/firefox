/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsScrollbarFrame.h"

#include "mozilla/LookAndFeel.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/dom/Element.h"
#include "nsContentCreatorFunctions.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIScrollbarMediator.h"
#include "nsLayoutUtils.h"
#include "nsScrollbarButtonFrame.h"
#include "nsSliderFrame.h"
#include "nsStyleConsts.h"

using namespace mozilla;
using mozilla::dom::Element;

static mozilla::LazyLogModule sScrollBarLog("apz.scrollbar");
#define SCROLLBAR_LOG(...) \
  MOZ_LOG(sScrollBarLog, LogLevel::Debug, (__VA_ARGS__));

//
// NS_NewScrollbarFrame
//
// Creates a new scrollbar frame and returns it
//
nsIFrame* NS_NewScrollbarFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsScrollbarFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsScrollbarFrame)

NS_QUERYFRAME_HEAD(nsScrollbarFrame)
  NS_QUERYFRAME_ENTRY(nsScrollbarFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

void nsScrollbarFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  // We want to be a reflow root since we use reflows to move the
  // slider.  Any reflow inside the scrollbar frame will be a reflow to
  // move the slider and will thus not change anything outside of the
  // scrollbar or change the size of the scrollbar frame.
  AddStateBits(NS_FRAME_REFLOW_ROOT);
}

nsScrollbarFrame* nsScrollbarFrame::GetOppositeScrollbar() const {
  ScrollContainerFrame* sc = do_QueryFrame(GetParent());
  if (!sc) {
    return nullptr;
  }
  auto* vScrollbar = sc->GetScrollbarBox(/* aVertical= */ true);
  if (vScrollbar == this) {
    return sc->GetScrollbarBox(/* aVertical= */ false);
  }
  MOZ_ASSERT(sc->GetScrollbarBox(/* aVertical= */ false) == this,
             "Which scrollbar are we?");
  return vScrollbar;
}

void nsScrollbarFrame::InvalidateForHoverChange(bool aIsNowHovered) {
  // Hover state on the scrollbar changes both the scrollbar and potentially
  // descendants too, so invalidate when it changes.
  InvalidateFrameSubtree();
  if (!aIsNowHovered) {
    return;
  }
  mHasBeenHovered = true;
  // When hovering over one scrollbar, remove the sticky hover effect from the
  // opposite scrollbar, if needed.
  if (auto* opposite = GetOppositeScrollbar();
      opposite && opposite->mHasBeenHovered) {
    opposite->mHasBeenHovered = false;
    opposite->InvalidateFrameSubtree();
  }
}

void nsScrollbarFrame::ActivityChanged(bool aIsNowActive) {
  if (ScrollContainerFrame* sc = do_QueryFrame(GetParent())) {
    if (aIsNowActive) {
      sc->ScrollbarActivityStarted();
    } else {
      sc->ScrollbarActivityStopped();
    }
  }
}

void nsScrollbarFrame::ElementStateChanged(dom::ElementState aStates) {
  if (aStates.HasState(dom::ElementState::HOVER)) {
    const bool hovered =
        mContent->AsElement()->State().HasState(dom::ElementState::HOVER);
    InvalidateForHoverChange(hovered);
    ActivityChanged(hovered);
  }
}

void nsScrollbarFrame::WillBecomeActive() {
  // Reset our sticky hover state before becoming active.
  mHasBeenHovered = false;
}

void nsScrollbarFrame::Destroy(DestroyContext& aContext) {
  aContext.AddAnonymousContent(mUpTopButton.forget());
  aContext.AddAnonymousContent(mDownTopButton.forget());
  aContext.AddAnonymousContent(mSlider.forget());
  aContext.AddAnonymousContent(mUpBottomButton.forget());
  aContext.AddAnonymousContent(mDownBottomButton.forget());
  nsContainerFrame::Destroy(aContext);
}

void nsScrollbarFrame::Reflow(nsPresContext* aPresContext,
                              ReflowOutput& aDesiredSize,
                              const ReflowInput& aReflowInput,
                              nsReflowStatus& aStatus) {
  MarkInReflow();
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // We always take all the space we're given, and our track size in the other
  // axis.
  const bool horizontal = IsHorizontal();
  const auto wm = GetWritingMode();
  const auto minSize = aReflowInput.ComputedMinSize();

  aDesiredSize.ISize(wm) = aReflowInput.ComputedISize();
  aDesiredSize.BSize(wm) = [&] {
    if (aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE) {
      return aReflowInput.ComputedBSize();
    }
    // We don't want to change our size during incremental reflow, see the
    // reflow root comment in init.
    if (!aReflowInput.mParentReflowInput) {
      return GetLogicalSize(wm).BSize(wm);
    }
    return minSize.BSize(wm);
  }();

  const nsSize containerSize = aDesiredSize.PhysicalSize();
  const LogicalSize totalAvailSize = aDesiredSize.Size(wm);
  LogicalPoint nextKidPos(wm);

  MOZ_ASSERT(!wm.IsVertical());
  const bool movesInInlineDirection = horizontal;

  // Layout our kids left to right / top to bottom.
  for (nsIFrame* kid : mFrames) {
    MOZ_ASSERT(!kid->GetWritingMode().IsOrthogonalTo(wm),
               "We don't expect orthogonal scrollbar parts");
    const bool isSlider = kid->GetContent() == mSlider;
    LogicalSize availSize = totalAvailSize;
    {
      // Assume we'll consume the same size before and after the slider. This is
      // not a technically correct assumption if we have weird scrollbar button
      // setups, but those will be going away, see bug 1824254.
      const int32_t factor = isSlider ? 2 : 1;
      if (movesInInlineDirection) {
        availSize.ISize(wm) =
            std::max(0, totalAvailSize.ISize(wm) - nextKidPos.I(wm) * factor);
      } else {
        availSize.BSize(wm) =
            std::max(0, totalAvailSize.BSize(wm) - nextKidPos.B(wm) * factor);
      }
    }

    ReflowInput kidRI(aPresContext, aReflowInput, kid, availSize);
    if (isSlider) {
      // We want for the slider to take all the remaining available space.
      kidRI.SetComputedISize(availSize.ISize(wm));
      kidRI.SetComputedBSize(availSize.BSize(wm));
    } else if (movesInInlineDirection) {
      // Otherwise we want all the space in the axis we're not advancing in, and
      // the default / minimum size on the other axis.
      kidRI.SetComputedBSize(availSize.BSize(wm));
    } else {
      kidRI.SetComputedISize(availSize.ISize(wm));
    }

    ReflowOutput kidDesiredSize(wm);
    nsReflowStatus status;
    const auto flags = ReflowChildFlags::Default;
    ReflowChild(kid, aPresContext, kidDesiredSize, kidRI, wm, nextKidPos,
                containerSize, flags, status);
    // We haven't seen the slider yet, we can advance
    FinishReflowChild(kid, aPresContext, kidDesiredSize, &kidRI, wm, nextKidPos,
                      containerSize, flags);
    if (movesInInlineDirection) {
      nextKidPos.I(wm) += kidDesiredSize.ISize(wm);
    } else {
      nextKidPos.B(wm) += kidDesiredSize.BSize(wm);
    }
  }

  aDesiredSize.SetOverflowAreasToDesiredBounds();
}

bool nsScrollbarFrame::SetCurPos(CSSIntCoord aCurPos) {
  if (mCurPos == aCurPos) {
    return false;
  }
  mCurPos = aCurPos;
  if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(GetParent())) {
    scrollContainerFrame->ScrollbarCurPosChanged();
  }
  if (nsSliderFrame* slider = do_QueryFrame(mSlider->GetPrimaryFrame())) {
    slider->CurrentPositionChanged();
  }
  return true;
}

void nsScrollbarFrame::RequestSliderReflow() {
  // These affect the slider.
  if (nsSliderFrame* slider = do_QueryFrame(mSlider->GetPrimaryFrame())) {
    PresShell()->FrameNeedsReflow(slider, IntrinsicDirty::None,
                                  NS_FRAME_IS_DIRTY);
  }
}

bool nsScrollbarFrame::SetMaxPos(CSSIntCoord aMaxPos) {
  if (mMaxPos == aMaxPos) {
    return false;
  }
  RequestSliderReflow();
  mMaxPos = aMaxPos;
  return true;
}

bool nsScrollbarFrame::SetPageIncrement(CSSIntCoord aPageIncrement) {
  if (mPageIncrement == aPageIncrement) {
    return false;
  }
  RequestSliderReflow();
  mPageIncrement = aPageIncrement;
  return true;
}

bool nsScrollbarFrame::IsEnabled() const {
  return !mContent->AsElement()->State().HasState(dom::ElementState::DISABLED);
}

bool nsScrollbarFrame::SetEnabled(bool aEnabled) {
  if (IsEnabled() == aEnabled) {
    return false;
  }
  mContent->AsElement()->SetStates(dom::ElementState::DISABLED, !aEnabled);
  return true;
}

NS_IMETHODIMP
nsScrollbarFrame::HandlePress(nsPresContext* aPresContext,
                              WidgetGUIEvent* aEvent,
                              nsEventStatus* aEventStatus) {
  return NS_OK;
}

NS_IMETHODIMP
nsScrollbarFrame::HandleMultiplePress(nsPresContext* aPresContext,
                                      WidgetGUIEvent* aEvent,
                                      nsEventStatus* aEventStatus,
                                      bool aControlHeld) {
  return NS_OK;
}

NS_IMETHODIMP
nsScrollbarFrame::HandleDrag(nsPresContext* aPresContext,
                             WidgetGUIEvent* aEvent,
                             nsEventStatus* aEventStatus) {
  return NS_OK;
}

NS_IMETHODIMP
nsScrollbarFrame::HandleRelease(nsPresContext* aPresContext,
                                WidgetGUIEvent* aEvent,
                                nsEventStatus* aEventStatus) {
  return NS_OK;
}

void nsScrollbarFrame::SetOverrideScrollbarMediator(
    nsIScrollbarMediator* aMediator) {
  mOverriddenScrollbarMediator = do_QueryFrame(aMediator);
}

nsIScrollbarMediator* nsScrollbarFrame::GetScrollbarMediator() {
  if (auto* override = mOverriddenScrollbarMediator.GetFrame()) {
    return do_QueryFrame(override);
  }
  return do_QueryFrame(GetParent());
}

bool nsScrollbarFrame::IsHorizontal() const {
  auto appearance = StyleDisplay()->EffectiveAppearance();
  MOZ_ASSERT(appearance == StyleAppearance::ScrollbarHorizontal ||
             appearance == StyleAppearance::ScrollbarVertical);
  return appearance == StyleAppearance::ScrollbarHorizontal;
}

nsSize nsScrollbarFrame::ScrollbarMinSize() const {
  nsPresContext* pc = PresContext();
  const LayoutDeviceIntSize widget =
      pc->Theme()->GetMinimumWidgetSize(pc, const_cast<nsScrollbarFrame*>(this),
                                        StyleDisplay()->EffectiveAppearance());
  return LayoutDeviceIntSize::ToAppUnits(widget, pc->AppUnitsPerDevPixel());
}

StyleScrollbarWidth nsScrollbarFrame::ScrollbarWidth() const {
  return nsLayoutUtils::ScrollbarWidthFor(this);
}

nscoord nsScrollbarFrame::ScrollbarTrackSize() const {
  auto overlay = nsLayoutUtils::UseOverlayScrollbars(this)
                     ? nsITheme::Overlay::Yes
                     : nsITheme::Overlay::No;

  nsPresContext* pc = PresContext();
  return LayoutDevicePixel::ToAppUnits(
      pc->Theme()->GetScrollbarSize(pc, ScrollbarWidth(), overlay),
      pc->AppUnitsPerDevPixel());
}

void nsScrollbarFrame::MoveToNewPosition() {
  nsIScrollbarMediator* m = GetScrollbarMediator();
  if (!m) {
    return;
  }
  // Note that this `MoveToNewPosition` is used for scrolling triggered by
  // repeating scrollbar button press, so we'd use an intended-direction
  // scroll snap flag.
  m->ScrollByUnit(this, ScrollMode::Smooth, mButtonScrollDirection,
                  mButtonScrollUnit, ScrollSnapFlags::IntendedDirection);
}

static already_AddRefed<Element> MakeScrollbarButton(
    dom::NodeInfo* aNodeInfo, bool aVertical, bool aBottom, bool aDown,
    AnonymousContentKey& aKey) {
  MOZ_ASSERT(aNodeInfo);
  MOZ_ASSERT(
      aNodeInfo->Equals(nsGkAtoms::scrollbarbutton, nullptr, kNameSpaceID_XUL));

  static constexpr nsLiteralString kSbattrValues[2][2] = {
      {
          u"scrollbar-up-top"_ns,
          u"scrollbar-up-bottom"_ns,
      },
      {
          u"scrollbar-down-top"_ns,
          u"scrollbar-down-bottom"_ns,
      },
  };

  static constexpr nsLiteralString kTypeValues[2] = {
      u"decrement"_ns,
      u"increment"_ns,
  };

  aKey = AnonymousContentKey::Type_ScrollbarButton;
  if (aVertical) {
    aKey |= AnonymousContentKey::Flag_Vertical;
  }
  if (aBottom) {
    aKey |= AnonymousContentKey::Flag_ScrollbarButton_Bottom;
  }
  if (aDown) {
    aKey |= AnonymousContentKey::Flag_ScrollbarButton_Down;
  }

  RefPtr<Element> e;
  NS_TrustedNewXULElement(getter_AddRefs(e), do_AddRef(aNodeInfo));
  e->SetAttr(kNameSpaceID_None, nsGkAtoms::sbattr,
             kSbattrValues[aDown][aBottom], false);
  e->SetAttr(kNameSpaceID_None, nsGkAtoms::type, kTypeValues[aDown], false);
  return e.forget();
}

nsresult nsScrollbarFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  nsNodeInfoManager* nodeInfoManager = mContent->NodeInfo()->NodeInfoManager();
  Element* el = GetContent()->AsElement();

  // If there are children already in the node, don't create any anonymous
  // content (this only apply to crashtests/369038-1.xhtml)
  if (el->HasChildren()) {
    return NS_OK;
  }

  const bool vertical = el->HasAttr(nsGkAtoms::vertical);
  RefPtr<dom::NodeInfo> sbbNodeInfo =
      nodeInfoManager->GetNodeInfo(nsGkAtoms::scrollbarbutton, nullptr,
                                   kNameSpaceID_XUL, nsINode::ELEMENT_NODE);

  const int32_t buttons =
      PresContext()->Theme()->ThemeSupportsScrollbarButtons()
          ? LookAndFeel::GetInt(LookAndFeel::IntID::ScrollArrowStyle)
          : 0;

  if (buttons & LookAndFeel::eScrollArrow_StartBackward) {
    AnonymousContentKey key;
    mUpTopButton =
        MakeScrollbarButton(sbbNodeInfo, vertical, /* aBottom */ false,
                            /* aDown */ false, key);
    aElements.AppendElement(ContentInfo(mUpTopButton, key));
  }

  if (buttons & LookAndFeel::eScrollArrow_StartForward) {
    AnonymousContentKey key;
    mDownTopButton =
        MakeScrollbarButton(sbbNodeInfo, vertical, /* aBottom */ false,
                            /* aDown */ true, key);
    aElements.AppendElement(ContentInfo(mDownTopButton, key));
  }

  {
    AnonymousContentKey key = AnonymousContentKey::Type_Slider;
    if (vertical) {
      key |= AnonymousContentKey::Flag_Vertical;
    }

    NS_TrustedNewXULElement(
        getter_AddRefs(mSlider),
        nodeInfoManager->GetNodeInfo(nsGkAtoms::slider, nullptr,
                                     kNameSpaceID_XUL, nsINode::ELEMENT_NODE));

    aElements.AppendElement(ContentInfo(mSlider, key));

    NS_TrustedNewXULElement(
        getter_AddRefs(mThumb),
        nodeInfoManager->GetNodeInfo(nsGkAtoms::thumb, nullptr,
                                     kNameSpaceID_XUL, nsINode::ELEMENT_NODE));
    mSlider->AppendChildTo(mThumb, false, IgnoreErrors());
  }

  if (buttons & LookAndFeel::eScrollArrow_EndBackward) {
    AnonymousContentKey key;
    mUpBottomButton =
        MakeScrollbarButton(sbbNodeInfo, vertical, /* aBottom */ true,
                            /* aDown */ false, key);
    aElements.AppendElement(ContentInfo(mUpBottomButton, key));
  }

  if (buttons & LookAndFeel::eScrollArrow_EndForward) {
    AnonymousContentKey key;
    mDownBottomButton =
        MakeScrollbarButton(sbbNodeInfo, vertical, /* aBottom */ true,
                            /* aDown */ true, key);
    aElements.AppendElement(ContentInfo(mDownBottomButton, key));
  }

  return NS_OK;
}

void nsScrollbarFrame::AppendAnonymousContentTo(
    nsTArray<nsIContent*>& aElements, uint32_t aFilter) {
  if (mUpTopButton) {
    aElements.AppendElement(mUpTopButton);
  }

  if (mDownTopButton) {
    aElements.AppendElement(mDownTopButton);
  }

  if (mSlider) {
    aElements.AppendElement(mSlider);
  }

  if (mUpBottomButton) {
    aElements.AppendElement(mUpBottomButton);
  }

  if (mDownBottomButton) {
    aElements.AppendElement(mDownBottomButton);
  }
}

void nsScrollbarFrame::SetButtonScrollDirectionAndUnit(
    int32_t aDirection, mozilla::ScrollUnit aUnit) {
  SCROLLBAR_LOG("nsScrollbarFrame(%p) setting button scroll direction=%d", this,
                aDirection);
  mButtonScrollDirection = aDirection;
  mButtonScrollUnit = aUnit;
}
