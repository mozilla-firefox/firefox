/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsSliderFrame.h"

#include <algorithm>

#include "mozilla/Assertions.h"  // for MOZ_ASSERT
#include "mozilla/ComputedStyle.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_slider.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/AsyncDragMetrics.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "nsCOMPtr.h"
#include "nsCSSRendering.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsDisplayList.h"
#include "nsHTMLParts.h"
#include "nsIContent.h"
#include "nsIScrollbarMediator.h"
#include "nsISupportsImpl.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsRefreshDriver.h"  // for nsAPostRefreshObserver
#include "nsRepeatService.h"
#include "nsScrollbarButtonFrame.h"
#include "nsScrollbarFrame.h"

using namespace mozilla;
using namespace mozilla::gfx;
using mozilla::dom::Document;
using mozilla::dom::Event;
using mozilla::layers::AsyncDragMetrics;
using mozilla::layers::InputAPZContext;
using mozilla::layers::ScrollbarData;
using mozilla::layers::ScrollDirection;

bool nsSliderFrame::gMiddlePref = false;

// Turn this on if you want to debug slider frames.
#undef DEBUG_SLIDER

nsIFrame* NS_NewSliderFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsSliderFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsSliderFrame)

NS_QUERYFRAME_HEAD(nsSliderFrame)
  NS_QUERYFRAME_ENTRY(nsSliderFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsSliderFrame::nsSliderFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID),
      mRatio(0.0f),
      mDragStart(0),
      mThumbStart(0),
      mRepeatDirection(0),
      mScrollingWithAPZ(false),
      mSuppressionActive(false),
      mThumbMinLength(0) {}

// stop timer
nsSliderFrame::~nsSliderFrame() {
  if (mSuppressionActive) {
    if (auto* presShell = PresShell()) {
      presShell->SuppressDisplayport(false);
    }
  }
}

void nsSliderFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                         nsIFrame* aPrevInFlow) {
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  static bool gotPrefs = false;
  if (!gotPrefs) {
    gotPrefs = true;

    gMiddlePref = Preferences::GetBool("middlemouse.scrollbarPosition");
  }
}

void nsSliderFrame::RemoveFrame(DestroyContext& aContext, ChildListID aListID,
                                nsIFrame* aOldFrame) {
  nsContainerFrame::RemoveFrame(aContext, aListID, aOldFrame);
  if (mFrames.IsEmpty()) {
    RemoveListener();
  }
}

void nsSliderFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                 const nsLineList::iterator* aPrevFrameLine,
                                 nsFrameList&& aFrameList) {
  bool wasEmpty = mFrames.IsEmpty();
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
  if (wasEmpty) {
    AddListener();
  }
}

void nsSliderFrame::AppendFrames(ChildListID aListID,
                                 nsFrameList&& aFrameList) {
  // If we have no children and on was added then make sure we add the
  // listener
  bool wasEmpty = mFrames.IsEmpty();
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
  if (wasEmpty) {
    AddListener();
  }
}

namespace mozilla {

// Draw any tick marks that show the position of find in page results.
class nsDisplaySliderMarks final : public nsPaintedDisplayItem {
 public:
  nsDisplaySliderMarks(nsDisplayListBuilder* aBuilder, nsSliderFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplaySliderMarks);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplaySliderMarks)

  NS_DISPLAY_DECL_NAME("SliderMarks", TYPE_SLIDER_MARKS)

  void PaintMarks(nsDisplayListBuilder* aDisplayListBuilder,
                  wr::DisplayListBuilder* aBuilder, gfxContext* aCtx);

  nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap) const override {
    *aSnap = false;
    return mFrame->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
  }

  bool CreateWebRenderCommands(
      wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override;

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
};

// This is shared between the webrender and Paint() paths. For the former,
// aBuilder should be assigned and aCtx will be null. For the latter, aBuilder
// should be null and aCtx should be the gfxContext for painting.
void nsDisplaySliderMarks::PaintMarks(nsDisplayListBuilder* aDisplayListBuilder,
                                      wr::DisplayListBuilder* aBuilder,
                                      gfxContext* aCtx) {
  DrawTarget* drawTarget = nullptr;
  if (aCtx) {
    drawTarget = aCtx->GetDrawTarget();
  } else {
    MOZ_ASSERT(aBuilder);
  }

  Document* doc = mFrame->GetContent()->GetUncomposedDoc();
  if (!doc) {
    return;
  }

  nsGlobalWindowInner* window =
      nsGlobalWindowInner::Cast(doc->GetInnerWindow());
  if (!window) {
    return;
  }

  auto* sliderFrame = static_cast<nsSliderFrame*>(mFrame);
  int32_t maxPos = sliderFrame->Scrollbar()->GetMaxPos();

  // Use the text highlight color for the tick marks.
  nscolor highlightColor =
      LookAndFeel::Color(LookAndFeel::ColorID::TextHighlightBackground, mFrame);
  DeviceColor fillColor = ToDeviceColor(highlightColor);
  fillColor.a = 0.3;  // make the mark mostly transparent

  int32_t appUnitsPerDevPixel =
      sliderFrame->PresContext()->AppUnitsPerDevPixel();
  nsRect sliderRect = sliderFrame->GetRect();

  nsPoint refPoint = aDisplayListBuilder->ToReferenceFrame(mFrame);

  // Increase the height of the tick mark rectangle by one pixel. If the
  // desktop scale is greater than 1, it should be increased more.
  // The tick marks should be drawn ignoring any page zoom that is applied.
  float increasePixels = sliderFrame->PresContext()
                             ->DeviceContext()
                             ->GetDesktopToDeviceScale()
                             .scale;
  const bool isHorizontal = sliderFrame->Scrollbar()->IsHorizontal();
  float increasePixelsX = isHorizontal ? increasePixels : 0;
  float increasePixelsY = isHorizontal ? 0 : increasePixels;
  nsSize initialSize =
      isHorizontal ? nsSize(0, sliderRect.height) : nsSize(sliderRect.width, 0);

  nsTArray<uint32_t>& marks = window->GetScrollMarks();
  for (uint32_t m = 0; m < marks.Length(); m++) {
    uint32_t markValue = marks[m];
    if (markValue > (uint32_t)maxPos) {
      markValue = maxPos;
    }

    // The values in the marks array range up to the window's
    // scrollMax{X,Y} - scrollMin{X,Y} (the same as the slider's maxpos).
    // Scale the values to fit within the slider's width or height.
    nsRect markRect(refPoint, initialSize);
    if (isHorizontal) {
      markRect.x += (nscoord)((double)markValue / maxPos * sliderRect.width);
    } else {
      markRect.y += (nscoord)((double)markValue / maxPos * sliderRect.height);
    }

    if (drawTarget) {
      Rect devPixelRect =
          NSRectToSnappedRect(markRect, appUnitsPerDevPixel, *drawTarget);
      devPixelRect.Inflate(increasePixelsX, increasePixelsY);
      drawTarget->FillRect(devPixelRect, ColorPattern(fillColor));
    } else {
      LayoutDeviceIntRect dRect = LayoutDeviceIntRect::FromAppUnitsToNearest(
          markRect, appUnitsPerDevPixel);
      dRect.Inflate(increasePixelsX, increasePixelsY);
      wr::LayoutRect layoutRect = wr::ToLayoutRect(dRect);
      aBuilder->PushRect(layoutRect, layoutRect, BackfaceIsHidden(), false,
                         false, wr::ToColorF(fillColor));
    }
  }
}

bool nsDisplaySliderMarks::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  PaintMarks(aDisplayListBuilder, &aBuilder, nullptr);
  return true;
}

void nsDisplaySliderMarks::Paint(nsDisplayListBuilder* aBuilder,
                                 gfxContext* aCtx) {
  PaintMarks(aBuilder, nullptr, aCtx);
}

}  // namespace mozilla

void nsSliderFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                     const nsDisplayListSet& aLists) {
  if (aBuilder->IsForEventDelivery() && IsDraggingThumb()) {
    // This is EVIL, we shouldn't be messing with event delivery just to get
    // thumb mouse drag events to arrive at the slider!
    aLists.Outlines()->AppendNewToTop<nsDisplayEventReceiver>(aBuilder, this);
    return;
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  if (nsIFrame* thumb = mFrames.FirstChild()) {
    BuildDisplayListForThumb(aBuilder, thumb, aLists);
  }

  // If this is an scrollbar for the root frame, draw any markers.
  // Markers are not drawn for other scrollbars.
  // XXX seems like this should be done in nsScrollbarFrame instead perhaps?
  if (!aBuilder->IsForEventDelivery()) {
    nsScrollbarFrame* scrollbar = Scrollbar();
    if (ScrollContainerFrame* scrollContainerFrame =
            do_QueryFrame(scrollbar->GetParent())) {
      if (scrollContainerFrame->IsRootScrollFrameOfDocument()) {
        nsGlobalWindowInner* window = nsGlobalWindowInner::Cast(
            PresContext()->Document()->GetInnerWindow());
        if (window &&
            window->GetScrollMarksOnHScrollbar() == scrollbar->IsHorizontal() &&
            window->GetScrollMarks().Length() > 0) {
          aLists.Content()->AppendNewToTop<nsDisplaySliderMarks>(aBuilder,
                                                                 this);
        }
      }
    }
  }
}

static bool UsesCustomScrollbarMediator(nsIFrame* scrollbarBox) {
  if (nsScrollbarFrame* scrollbarFrame = do_QueryFrame(scrollbarBox)) {
    if (nsIScrollbarMediator* mediator =
            scrollbarFrame->GetScrollbarMediator()) {
      // Note we can't queryframe from nsIScrollbarMediator to
      // ScrollContainerFrame directly due to an optimization in the queryframe
      // implementation for ScrollContainerFrame.
      nsIFrame* mediatorAsFrame = do_QueryFrame(mediator);
      ScrollContainerFrame* scrollContainerFrame =
          do_QueryFrame(mediatorAsFrame);
      // The scrollbar mediator is not the scroll container frame.
      // That means this scroll container frame has a custom scrollbar mediator.
      if (!scrollContainerFrame) {
        return true;
      }
    }
  }
  return false;
}

void nsSliderFrame::BuildDisplayListForThumb(nsDisplayListBuilder* aBuilder,
                                             nsIFrame* aThumb,
                                             const nsDisplayListSet& aLists) {
  nsRect thumbRect(aThumb->GetRect());

  nsRect sliderTrack = GetRect();
  if (sliderTrack.width < thumbRect.width ||
      sliderTrack.height < thumbRect.height) {
    return;
  }

  // If this scrollbar is the scrollbar of an actively scrolled scroll frame,
  // layerize the scrollbar thumb, wrap it in its own ContainerLayer and
  // attach scrolling information to it.
  // We do this here and not in the thumb's BuildDisplayList so that the event
  // region that gets created for the thumb is included in the nsDisplayOwnLayer
  // contents.

  const layers::ScrollableLayerGuid::ViewID scrollTargetId =
      aBuilder->GetCurrentScrollbarTarget();
  const bool thumbGetsLayer =
      scrollTargetId != layers::ScrollableLayerGuid::NULL_SCROLL_ID;

  if (thumbGetsLayer) {
    const Maybe<ScrollDirection> scrollDirection =
        aBuilder->GetCurrentScrollbarDirection();
    MOZ_ASSERT(scrollDirection.isSome());
    const bool isHorizontal = *scrollDirection == ScrollDirection::eHorizontal;
    const OuterCSSCoord thumbLength = OuterCSSPixel::FromAppUnits(
        isHorizontal ? thumbRect.width : thumbRect.height);
    const OuterCSSCoord minThumbLength =
        OuterCSSPixel::FromAppUnits(mThumbMinLength);

    nsIFrame* scrollbarBox = Scrollbar();
    bool isAsyncDraggable = !UsesCustomScrollbarMediator(scrollbarBox);

    nsPoint scrollPortOrigin;
    if (ScrollContainerFrame* scrollContainerFrame =
            do_QueryFrame(scrollbarBox->GetParent())) {
      scrollPortOrigin = scrollContainerFrame->GetScrollPortRect().TopLeft();
    } else {
      isAsyncDraggable = false;
    }

    // This rect is the range in which the scroll thumb can slide in.
    sliderTrack = sliderTrack + scrollbarBox->GetPosition() - scrollPortOrigin;
    const OuterCSSCoord sliderTrackStart = OuterCSSPixel::FromAppUnits(
        isHorizontal ? sliderTrack.x : sliderTrack.y);
    const OuterCSSCoord sliderTrackLength = OuterCSSPixel::FromAppUnits(
        isHorizontal ? sliderTrack.width : sliderTrack.height);
    const OuterCSSCoord thumbStart =
        OuterCSSPixel::FromAppUnits(isHorizontal ? thumbRect.x : thumbRect.y);

    const nsRect overflow = aThumb->InkOverflowRectRelativeToParent();
    nsSize refSize = aBuilder->RootReferenceFrame()->GetSize();
    nsRect dirty = aBuilder->GetVisibleRect().Intersect(thumbRect);
    dirty = nsLayoutUtils::ComputePartialPrerenderArea(
        aThumb, aBuilder->GetVisibleRect(), overflow, refSize);

    nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
        aBuilder, this, dirty, dirty);

    // Clip the thumb layer to the slider track. This is necessary to ensure
    // FrameLayerBuilder is able to merge content before and after the
    // scrollframe into the same layer (otherwise it thinks the thumb could
    // potentially move anywhere within the existing clip).
    DisplayListClipState::AutoSaveRestore thumbClipState(aBuilder);
    thumbClipState.ClipContainingBlockDescendants(
        GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this));

    // Have the thumb's container layer capture the current clip, so
    // it doesn't apply to the thumb's contents. This allows the contents
    // to be fully rendered even if they're partially or fully offscreen,
    // so async scrolling can still bring it into view.
    DisplayListClipState::AutoSaveRestore thumbContentsClipState(aBuilder);
    thumbContentsClipState.Clear();

    nsDisplayListBuilder::AutoContainerASRTracker contASRTracker(aBuilder);
    nsDisplayListCollection tempLists(aBuilder);
    BuildDisplayListForChild(aBuilder, aThumb, tempLists);

    // This is a bit of a hack. Collect up all descendant display items
    // and merge them into a single Content() list.
    nsDisplayList masterList(aBuilder);
    masterList.AppendToTop(tempLists.BorderBackground());
    masterList.AppendToTop(tempLists.BlockBorderBackgrounds());
    masterList.AppendToTop(tempLists.Floats());
    masterList.AppendToTop(tempLists.Content());
    masterList.AppendToTop(tempLists.PositionedDescendants());
    masterList.AppendToTop(tempLists.Outlines());

    // Restore the saved clip so it applies to the thumb container layer.
    thumbContentsClipState.Restore();

    // Wrap the list to make it its own layer.
    const ActiveScrolledRoot* ownLayerASR = contASRTracker.GetContainerASR();
    aLists.Content()->AppendNewToTopWithIndex<nsDisplayOwnLayer>(
        aBuilder, this,
        /* aIndex = */ nsDisplayOwnLayer::OwnLayerForScrollThumb, &masterList,
        ownLayerASR, nsDisplayItem::ContainerASRType::AncestorOfContained,
        nsDisplayOwnLayerFlags::None,
        ScrollbarData::CreateForThumb(*scrollDirection, GetThumbRatio(),
                                      thumbStart, thumbLength, minThumbLength,
                                      isAsyncDraggable, sliderTrackStart,
                                      sliderTrackLength, scrollTargetId),
        true, false);

    return;
  }

  BuildDisplayListForChild(aBuilder, aThumb, aLists);
}

void nsSliderFrame::Reflow(nsPresContext* aPresContext,
                           ReflowOutput& aDesiredSize,
                           const ReflowInput& aReflowInput,
                           nsReflowStatus& aStatus) {
  MarkInReflow();
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_ASSERTION(aReflowInput.AvailableWidth() != NS_UNCONSTRAINEDSIZE,
               "Bogus avail width");
  NS_ASSERTION(aReflowInput.AvailableHeight() != NS_UNCONSTRAINEDSIZE,
               "Bogus avail height");

  const auto wm = GetWritingMode();

  // We always take all the space we're given.
  aDesiredSize.SetSize(wm, aReflowInput.ComputedSize(wm));
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  // Get the thumb, should be our only child.
  nsIFrame* thumbBox = mFrames.FirstChild();
  if (NS_WARN_IF(!thumbBox)) {
    return;
  }

  const bool horizontal = Scrollbar()->IsHorizontal();
  nsSize availSize = aDesiredSize.PhysicalSize();
  ReflowInput thumbRI(aPresContext, aReflowInput, thumbBox,
                      aReflowInput.AvailableSize(wm));

  // Get the thumb's pref size.
  nsSize thumbSize = thumbRI.ComputedMinSize(wm).GetPhysicalSize(wm);
  if (horizontal) {
    thumbSize.height = availSize.height;
  } else {
    thumbSize.width = availSize.width;
  }

  int32_t curPos = Scrollbar()->GetCurPos();
  int32_t maxPos = Scrollbar()->GetMaxPos();
  int32_t pageIncrement = Scrollbar()->GetPageIncrement();

  curPos = std::min(curPos, maxPos);

  // If modifying the logic here, be sure to modify the corresponding
  // compositor-side calculation in ScrollThumbUtils::ApplyTransformForAxis().
  nscoord& availableLength = horizontal ? availSize.width : availSize.height;
  nscoord& thumbLength = horizontal ? thumbSize.width : thumbSize.height;
  mThumbMinLength = thumbLength;

  if (pageIncrement + maxPos > 0) {
    float ratio = float(pageIncrement) / float(maxPos + pageIncrement);
    thumbLength =
        std::max(thumbLength, NSToCoordRound(availableLength * ratio));
  }

  // Round the thumb's length to device pixels.
  nsPresContext* presContext = PresContext();
  thumbLength = presContext->DevPixelsToAppUnits(
      presContext->AppUnitsToDevPixels(thumbLength));

  // mRatio translates the thumb position in app units to the value.
  mRatio = maxPos ? float(availableLength - thumbLength) / float(maxPos) : 1;

  // set the thumb's coord to be the current pos * the ratio.
  nsPoint thumbPos;
  if (horizontal) {
    thumbPos.x = NSToCoordRound(curPos * mRatio);
  } else {
    thumbPos.y = NSToCoordRound(curPos * mRatio);
  }

  // Same to `snappedThumbLocation` in `nsSliderFrame::CurrentPositionChanged`,
  // to avoid putting the scroll thumb at subpixel positions which cause
  // needless invalidations
  nscoord appUnitsPerPixel = PresContext()->AppUnitsPerDevPixel();
  thumbPos =
      ToAppUnits(thumbPos.ToNearestPixels(appUnitsPerPixel), appUnitsPerPixel);

  const LogicalPoint logicalPos(wm, thumbPos, availSize);
  // TODO: It seems like a lot of this stuff should really belong in the thumb's
  // reflow code rather than here, but since we rely on the frame tree structure
  // heavily this matches the previous code more closely for now.
  ReflowOutput thumbDesiredSize(wm);
  const auto flags = ReflowChildFlags::Default;
  nsReflowStatus status;
  thumbRI.SetComputedISize(thumbSize.width);
  thumbRI.SetComputedBSize(thumbSize.height);
  ReflowChild(thumbBox, aPresContext, thumbDesiredSize, thumbRI, wm, logicalPos,
              availSize, flags, status);
  FinishReflowChild(thumbBox, aPresContext, thumbDesiredSize, &thumbRI, wm,
                    logicalPos, availSize, flags);
}

nsresult nsSliderFrame::HandleEvent(nsPresContext* aPresContext,
                                    WidgetGUIEvent* aEvent,
                                    nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  if (mAPZDragInitiated &&
      *mAPZDragInitiated == InputAPZContext::GetInputBlockId() &&
      aEvent->mMessage == eMouseDown) {
    // If we get the mousedown after the APZ notification, then immediately
    // switch into the state corresponding to an APZ thumb-drag. Note that
    // we can't just do this in AsyncScrollbarDragInitiated() directly because
    // the handling for this mousedown event in the presShell will reset the
    // capturing content which makes isDraggingThumb() return false. We check
    // the input block here to make sure that we correctly handle any ordering
    // of {eMouseDown arriving, AsyncScrollbarDragInitiated() being called}.
    mAPZDragInitiated = Nothing();
    DragThumb(true);
    mScrollingWithAPZ = true;
    return NS_OK;
  }

  // If a web page calls event.preventDefault() we still want to
  // scroll when scroll arrow is clicked. See bug 511075.
  if (!mContent->IsInNativeAnonymousSubtree() &&
      nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  if (mDragInProgress && !IsDraggingThumb()) {
    StopDrag();
    return NS_OK;
  }

  nsScrollbarFrame* scrollbarBox = Scrollbar();
  bool isHorizontal = scrollbarBox->IsHorizontal();

  if (IsDraggingThumb()) {
    switch (aEvent->mMessage) {
      case eTouchMove:
      case eMouseMove: {
        if (mScrollingWithAPZ) {
          break;
        }
        nsPoint eventPoint;
        if (!GetEventPoint(aEvent, eventPoint)) {
          break;
        }
        if (mRepeatDirection) {
          // On Linux the destination point is determined by the initial click
          // on the scrollbar track and doesn't change until the mouse button
          // is released.
#ifndef MOZ_WIDGET_GTK
          // On the other platforms we need to update the destination point now.
          mDestinationPoint = eventPoint;
          StopRepeat();
          StartRepeat();
#endif
          break;
        }

        nscoord pos = isHorizontal ? eventPoint.x : eventPoint.y;

        nsIFrame* thumbFrame = mFrames.FirstChild();
        if (!thumbFrame) {
          return NS_OK;
        }

        // take our current position and subtract the start location
        pos -= mDragStart;
        bool isMouseOutsideThumb = false;
        const int32_t snapMultiplier = StaticPrefs::slider_snapMultiplier();
        if (snapMultiplier) {
          nsSize thumbSize = thumbFrame->GetSize();
          if (isHorizontal) {
            // horizontal scrollbar - check if mouse is above or below thumb
            // XXXbz what about looking at the .y of the thumb's rect?  Is that
            // always zero here?
            if (eventPoint.y < -snapMultiplier * thumbSize.height ||
                eventPoint.y >
                    thumbSize.height + snapMultiplier * thumbSize.height) {
              isMouseOutsideThumb = true;
            }
          } else {
            // vertical scrollbar - check if mouse is left or right of thumb
            if (eventPoint.x < -snapMultiplier * thumbSize.width ||
                eventPoint.x >
                    thumbSize.width + snapMultiplier * thumbSize.width) {
              isMouseOutsideThumb = true;
            }
          }
        }
        if (aEvent->mClass == eTouchEventClass) {
          *aEventStatus = nsEventStatus_eConsumeNoDefault;
        }
        if (isMouseOutsideThumb) {
          SetCurrentThumbPosition(mThumbStart);
          return NS_OK;
        }

        // set it
        SetCurrentThumbPosition(pos);
      } break;

      case eTouchEnd:
      case eMouseUp:
        if (ShouldScrollForEvent(aEvent)) {
          StopDrag();
          // we MUST call nsIFrame HandleEvent for mouse ups to maintain the
          // selection state and capture state.
          return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
        }
        break;

      default:
        break;
    }

    // return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
    return NS_OK;
  }

  if (ShouldScrollToClickForEvent(aEvent)) {
    nsPoint eventPoint;
    if (!GetEventPoint(aEvent, eventPoint)) {
      return NS_OK;
    }
    nscoord pos = isHorizontal ? eventPoint.x : eventPoint.y;

    // adjust so that the middle of the thumb is placed under the click
    nsIFrame* thumbFrame = mFrames.FirstChild();
    if (!thumbFrame) {
      return NS_OK;
    }
    nsSize thumbSize = thumbFrame->GetSize();
    nscoord thumbLength = isHorizontal ? thumbSize.width : thumbSize.height;

    // set it
    AutoWeakFrame weakFrame(this);
    SetCurrentThumbPosition(pos - thumbLength / 2);
    NS_ENSURE_TRUE(weakFrame.IsAlive(), NS_OK);

    DragThumb(true);

    if (aEvent->mClass == eTouchEventClass) {
      *aEventStatus = nsEventStatus_eConsumeNoDefault;
    }

    SetupDrag(aEvent, thumbFrame, pos, isHorizontal);
  }
#ifdef MOZ_WIDGET_GTK
  else if (ShouldScrollForEvent(aEvent) && aEvent->mClass == eMouseEventClass &&
           aEvent->AsMouseEvent()->mButton == MouseButton::eSecondary) {
    // HandlePress and HandleRelease are usually called via
    // nsIFrame::HandleEvent, but only for the left mouse button.
    if (aEvent->mMessage == eMouseDown) {
      HandlePress(aPresContext, aEvent, aEventStatus);
    } else if (aEvent->mMessage == eMouseUp) {
      HandleRelease(aPresContext, aEvent, aEventStatus);
    }

    return NS_OK;
  }
#endif

  // XXX hack until handle release is actually called in nsframe.
  //  if (aEvent->mMessage == eMouseOut ||
  //      aEvent->mMessage == NS_MOUSE_RIGHT_BUTTON_UP ||
  //      aEvent->mMessage == NS_MOUSE_LEFT_BUTTON_UP) {
  //    HandleRelease(aPresContext, aEvent, aEventStatus);
  //  }

  if (aEvent->mMessage == eMouseOut && mRepeatDirection) {
    HandleRelease(aPresContext, aEvent, aEventStatus);
  }

  return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

// Helper function to collect the "scroll to click" metric. Beware of
// caching this, users expect to be able to change the system preference
// and see the browser change its behavior immediately.
bool nsSliderFrame::GetScrollToClick() {
  return LookAndFeel::GetInt(LookAndFeel::IntID::ScrollToClick, false);
}

nsScrollbarFrame* nsSliderFrame::Scrollbar() const {
  MOZ_ASSERT(GetParent());
  MOZ_DIAGNOSTIC_ASSERT(
      static_cast<nsScrollbarFrame*>(do_QueryFrame(GetParent())));
  return static_cast<nsScrollbarFrame*>(GetParent());
}

// called when the current position changed and we need to update the thumb's
// location
void nsSliderFrame::CurrentPositionChanged() {
  // get the current position
  // get our current min and max position from our content node
  int32_t curPos = Scrollbar()->GetCurPos();
  int32_t maxPos = Scrollbar()->GetMaxPos();

  curPos = std::min(curPos, maxPos);

  // get the thumb's rect
  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    return;
  }

  const bool horizontal = Scrollbar()->IsHorizontal();

  // figure out the new rect
  nsRect thumbRect = thumbFrame->GetRect();
  nsRect newThumbRect(thumbRect);
  if (horizontal) {
    newThumbRect.x = NSToCoordRound(curPos * mRatio);
  } else {
    newThumbRect.y = NSToCoordRound(curPos * mRatio);
  }

  // avoid putting the scroll thumb at subpixel positions which cause needless
  // invalidations
  nscoord appUnitsPerPixel = PresContext()->AppUnitsPerDevPixel();
  nsPoint snappedThumbLocation =
      ToAppUnits(newThumbRect.TopLeft().ToNearestPixels(appUnitsPerPixel),
                 appUnitsPerPixel);
  if (horizontal) {
    newThumbRect.x = snappedThumbLocation.x;
  } else {
    newThumbRect.y = snappedThumbLocation.y;
  }

  // set the rect
  // XXX This out-of-band update of the frame tree is rather fishy!
  thumbFrame->SetRect(newThumbRect);

  // When the thumb changes position, the mThumbStart value stored in
  // ScrollbarData for the purpose of telling APZ about the thumb
  // position painted by the main thread is invalidated. The ScrollbarData
  // is stored on the nsDisplayOwnLayer item built by *this* frame, so
  // we need to mark this frame as needing its fisplay item rebuilt.
  MarkNeedsDisplayItemRebuild();

  // Request a repaint of the scrollbar
  nsIScrollbarMediator* mediator = Scrollbar()->GetScrollbarMediator();
  if (!mediator || !mediator->ShouldSuppressScrollbarRepaints()) {
    SchedulePaint();
  }
}

// Use this function when you want to set the scroll position via the position
// of the scrollbar thumb, e.g. when dragging the slider. This function scrolls
// the content in such a way that thumbRect.x/.y becomes aNewThumbPos.
void nsSliderFrame::SetCurrentThumbPosition(nscoord aNewPos) {
  nsScrollbarFrame* sb = Scrollbar();
  int32_t newPos = NSToIntRound(aNewPos / mRatio);
  // get min and max position from our content node
  int32_t maxpos = sb->GetMaxPos();

  // get the new position and make sure it is in bounds
  if (newPos < 0) {
    newPos = 0;
  } else if (newPos > maxpos) {
    newPos = maxpos;
  }
  AutoWeakFrame weakFrame(this);

  nsIScrollbarMediator* mediator = sb->GetScrollbarMediator();
  if (!mediator) {
    return;
  }
  mediator->ThumbMoved(sb, CSSPixel::ToAppUnits(sb->GetCurPos()),
                       CSSPixel::ToAppUnits(newPos));
  if (!weakFrame.IsAlive()) {
    return;
  }
  sb->SetCurPos(newPos);
}

void nsSliderFrame::SetInitialChildList(ChildListID aListID,
                                        nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  if (aListID == FrameChildListID::Principal) {
    AddListener();
  }
}

nsresult nsSliderMediator::HandleEvent(dom::Event* aEvent) {
  // Only process the event if the thumb is not being dragged.
  if (mSlider && !mSlider->IsDraggingThumb()) {
    return mSlider->StartDrag(aEvent);
  }
  return NS_OK;
}

static bool ScrollFrameWillBuildScrollInfoLayer(nsIFrame* aScrollFrame) {
  /*
   * Note: if changing the conditions in this function, make a corresponding
   * change to nsDisplayListBuilder::ShouldBuildScrollInfoItemsForHoisting()
   * in nsDisplayList.cpp.
   */
  nsIFrame* current = aScrollFrame;
  while (current) {
    if (SVGIntegrationUtils::UsesSVGEffectsNotSupportedInCompositor(current)) {
      return true;
    }
    current = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(current);
  }
  return false;
}

ScrollContainerFrame* nsSliderFrame::GetScrollContainerFrame() {
  return do_QueryFrame(Scrollbar()->GetParent());
}

void nsSliderFrame::StartAPZDrag(WidgetGUIEvent* aEvent) {
  if (!aEvent->mFlags.mHandledByAPZ) {
    return;
  }

  if (!gfxPlatform::GetPlatform()->SupportsApzDragInput()) {
    return;
  }

  if (aEvent->AsMouseEvent() &&
      aEvent->AsMouseEvent()->mButton != MouseButton::ePrimary) {
    return;
  }

  nsIFrame* scrollbarBox = Scrollbar();
  nsContainerFrame* scrollFrame = scrollbarBox->GetParent();
  if (!scrollFrame) {
    return;
  }

  nsIContent* scrollableContent = scrollFrame->GetContent();
  if (!scrollableContent) {
    return;
  }

  // APZ dragging requires the scrollbar to be layerized, which doesn't
  // happen for scroll info layers.
  if (ScrollFrameWillBuildScrollInfoLayer(scrollFrame)) {
    return;
  }

  // Custom scrollbar mediators are not supported in the APZ codepath.
  if (UsesCustomScrollbarMediator(scrollbarBox)) {
    return;
  }

  bool isHorizontal = Scrollbar()->IsHorizontal();

  layers::ScrollableLayerGuid::ViewID scrollTargetId;
  bool hasID = nsLayoutUtils::FindIDFor(scrollableContent, &scrollTargetId);
  bool hasAPZView =
      hasID && scrollTargetId != layers::ScrollableLayerGuid::NULL_SCROLL_ID;

  if (!hasAPZView) {
    return;
  }

  if (!DisplayPortUtils::HasNonMinimalDisplayPort(scrollableContent)) {
    return;
  }

  auto* presShell = PresShell();
  uint64_t inputblockId = InputAPZContext::GetInputBlockId();
  uint32_t presShellId = presShell->GetPresShellId();
  AsyncDragMetrics dragMetrics(
      scrollTargetId, presShellId, inputblockId,
      OuterCSSPixel::FromAppUnits(mDragStart),
      isHorizontal ? ScrollDirection::eHorizontal : ScrollDirection::eVertical);

  // It's important to set this before calling
  // nsIWidget::StartAsyncScrollbarDrag(), because in some configurations, that
  // can call AsyncScrollbarDragRejected() synchronously, which clears the flag
  // (and we want it to stay cleared in that case).
  mScrollingWithAPZ = true;

  // When we start an APZ drag, we wont get mouse events for the drag.
  // APZ will consume them all and only notify us of the new scroll position.
  bool waitForRefresh = InputAPZContext::HavePendingLayerization();
  nsIWidget* widget = this->GetNearestWidget();
  if (waitForRefresh) {
    waitForRefresh = false;
    if (nsPresContext* presContext = presShell->GetPresContext()) {
      presContext->RegisterManagedPostRefreshObserver(
          new ManagedPostRefreshObserver(
              presContext, [widget = RefPtr<nsIWidget>(widget),
                            dragMetrics](bool aWasCanceled) {
                if (!aWasCanceled) {
                  widget->StartAsyncScrollbarDrag(dragMetrics);
                }
                return ManagedPostRefreshObserver::Unregister::Yes;
              }));
      waitForRefresh = true;
    }
  }
  if (!waitForRefresh) {
    widget->StartAsyncScrollbarDrag(dragMetrics);
  }
}

nsresult nsSliderFrame::StartDrag(Event* aEvent) {
#ifdef DEBUG_SLIDER
  printf("Begin dragging\n");
#endif
  if (Scrollbar()->IsDisabled()) {
    return NS_OK;
  }

  WidgetGUIEvent* event = aEvent->WidgetEventPtr()->AsGUIEvent();

  if (!ShouldScrollForEvent(event)) {
    return NS_OK;
  }

  nsPoint pt;
  if (!GetEventPoint(event, pt)) {
    return NS_OK;
  }
  bool isHorizontal = Scrollbar()->IsHorizontal();
  nscoord pos = isHorizontal ? pt.x : pt.y;

  // If we should scroll-to-click, first place the middle of the slider thumb
  // under the mouse.
  nscoord newpos = pos;
  bool scrollToClick = ShouldScrollToClickForEvent(event);
  if (scrollToClick) {
    // adjust so that the middle of the thumb is placed under the click
    nsIFrame* thumbFrame = mFrames.FirstChild();
    if (!thumbFrame) {
      return NS_OK;
    }
    nsSize thumbSize = thumbFrame->GetSize();
    nscoord thumbLength = isHorizontal ? thumbSize.width : thumbSize.height;

    newpos -= (thumbLength / 2);
  }

  DragThumb(true);

  if (scrollToClick) {
    SetCurrentThumbPosition(newpos);
  }

  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    return NS_OK;
  }

  SetupDrag(event, thumbFrame, pos, isHorizontal);

  return NS_OK;
}

nsresult nsSliderFrame::StopDrag() {
  AddListener();
  DragThumb(false);

  mScrollingWithAPZ = false;

  UnsuppressDisplayport();

  if (mRepeatDirection) {
    StopRepeat();
    mRepeatDirection = 0;
  }
  return NS_OK;
}

bool nsSliderFrame::ClickAndHoldActive() const {
  return mCurrentClickHoldDestination.isSome();
}

void nsSliderFrame::DragThumb(bool aGrabMouseEvents) {
  if (mDragInProgress != aGrabMouseEvents) {
    Scrollbar()->ActivityChanged(aGrabMouseEvents);
  }
  mDragInProgress = aGrabMouseEvents;

  if (aGrabMouseEvents) {
    PresShell::SetCapturingContent(
        GetContent(),
        CaptureFlags::IgnoreAllowedState | CaptureFlags::PreventDragStart);
  } else {
    PresShell::ReleaseCapturingContent();
  }
}

bool nsSliderFrame::IsDraggingThumb() const {
  return PresShell::GetCapturingContent() == GetContent();
}

void nsSliderFrame::AddListener() {
  if (!mMediator) {
    mMediator = new nsSliderMediator(this);
  }

  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    return;
  }
  thumbFrame->GetContent()->AddSystemEventListener(u"mousedown"_ns, mMediator,
                                                   false, false);
  thumbFrame->GetContent()->AddSystemEventListener(u"touchstart"_ns, mMediator,
                                                   false, false);
}

void nsSliderFrame::RemoveListener() {
  NS_ASSERTION(mMediator, "No listener was ever added!!");

  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    return;
  }

  thumbFrame->GetContent()->RemoveSystemEventListener(u"mousedown"_ns,
                                                      mMediator, false);
  thumbFrame->GetContent()->RemoveSystemEventListener(u"touchstart"_ns,
                                                      mMediator, false);
}

bool nsSliderFrame::ShouldScrollForEvent(WidgetGUIEvent* aEvent) {
  switch (aEvent->mMessage) {
    case eTouchStart:
    case eTouchEnd:
      return true;
    case eMouseDown:
    case eMouseUp: {
      uint16_t button = aEvent->AsMouseEvent()->mButton;
#ifdef MOZ_WIDGET_GTK
      return (button == MouseButton::ePrimary) ||
             (button == MouseButton::eSecondary && GetScrollToClick()) ||
             (button == MouseButton::eMiddle && gMiddlePref &&
              !GetScrollToClick());
#else
      return (button == MouseButton::ePrimary) ||
             (button == MouseButton::eMiddle && gMiddlePref);
#endif
    }
    default:
      return false;
  }
}

bool nsSliderFrame::ShouldScrollToClickForEvent(WidgetGUIEvent* aEvent) {
  if (!ShouldScrollForEvent(aEvent)) {
    return false;
  }

  if (aEvent->mMessage != eMouseDown && aEvent->mMessage != eTouchStart) {
    return false;
  }

#if defined(XP_MACOSX) || defined(MOZ_WIDGET_GTK)
  // On Mac and Linux, clicking the scrollbar thumb should never scroll to
  // click.
  if (IsEventOverThumb(aEvent)) {
    return false;
  }
#endif

  if (aEvent->mMessage == eTouchStart) {
    return GetScrollToClick();
  }

  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  if (mouseEvent->mButton == MouseButton::ePrimary) {
#ifdef XP_MACOSX
    bool invertPref = mouseEvent->IsAlt();
#else
    bool invertPref = mouseEvent->IsShift();
#endif
    return GetScrollToClick() != invertPref;
  }

#ifdef MOZ_WIDGET_GTK
  if (mouseEvent->mButton == MouseButton::eSecondary) {
    return !GetScrollToClick();
  }
#endif

  return true;
}

bool nsSliderFrame::IsEventOverThumb(WidgetGUIEvent* aEvent) {
  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    return false;
  }

  nsPoint eventPoint;
  if (!GetEventPoint(aEvent, eventPoint)) {
    return false;
  }

  const nsRect thumbRect = thumbFrame->GetRect();
  const bool isHorizontal = Scrollbar()->IsHorizontal();
  nscoord eventPos = isHorizontal ? eventPoint.x : eventPoint.y;
  nscoord thumbStart = isHorizontal ? thumbRect.x : thumbRect.y;
  nscoord thumbEnd = isHorizontal ? thumbRect.XMost() : thumbRect.YMost();
  return eventPos >= thumbStart && eventPos < thumbEnd;
}

NS_IMETHODIMP
nsSliderFrame::HandlePress(nsPresContext* aPresContext, WidgetGUIEvent* aEvent,
                           nsEventStatus* aEventStatus) {
  if (!ShouldScrollForEvent(aEvent) || ShouldScrollToClickForEvent(aEvent)) {
    return NS_OK;
  }

  if (IsEventOverThumb(aEvent)) {
    return NS_OK;
  }

  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {  // display:none?
    return NS_OK;
  }

  if (Scrollbar()->IsDisabled()) {
    return NS_OK;
  }

  nsRect thumbRect = thumbFrame->GetRect();

  nscoord change = 1;
  nsPoint eventPoint;
  if (!GetEventPoint(aEvent, eventPoint)) {
    return NS_OK;
  }

  if (Scrollbar()->IsHorizontal() ? eventPoint.x < thumbRect.x
                                  : eventPoint.y < thumbRect.y) {
    change = -1;
  }

  mRepeatDirection = change;
  DragThumb(true);
  if (StaticPrefs::layout_scrollbars_click_and_hold_track_continue_to_end()) {
    // Set the destination point to the very end of the scrollbar so that
    // scrolling doesn't stop halfway through.
    if (change > 0) {
      mDestinationPoint = nsPoint(GetRect().width, GetRect().height);
    } else {
      mDestinationPoint = nsPoint(0, 0);
    }
  } else {
    mDestinationPoint = eventPoint;
  }
  StartRepeat();
  PageScroll(false);

  return NS_OK;
}

NS_IMETHODIMP
nsSliderFrame::HandleRelease(nsPresContext* aPresContext,
                             WidgetGUIEvent* aEvent,
                             nsEventStatus* aEventStatus) {
  StopRepeat();

  nsScrollbarFrame* sb = Scrollbar();
  if (nsIScrollbarMediator* m = sb->GetScrollbarMediator()) {
    m->ScrollbarReleased(sb);
  }
  return NS_OK;
}

void nsSliderFrame::Destroy(DestroyContext& aContext) {
  // tell our mediator if we have one we are gone.
  if (mMediator) {
    mMediator->SetSlider(nullptr);
    mMediator = nullptr;
  }
  StopRepeat();

  // call base class Destroy()
  nsContainerFrame::Destroy(aContext);
}

void nsSliderFrame::StartRepeat() {
  ScrollContainerFrame* sf = GetScrollContainerFrame();
  if (sf) {
    mCurrentClickHoldDestination = Some(sf->GetScrollPosition());
  }
  nsRepeatService::GetInstance()->Start(Notify, this, mContent->OwnerDoc(),
                                        "nsSliderFrame"_ns);
}

void nsSliderFrame::Notify() {
  bool stop = false;

  nsIFrame* thumbFrame = mFrames.FirstChild();
  if (!thumbFrame) {
    StopRepeat();
    return;
  }
  nsRect thumbRect = thumbFrame->GetRect();

  const bool isHorizontal = Scrollbar()->IsHorizontal();

  // See if the thumb has moved past our destination point.
  // if it has we want to stop.
  if (isHorizontal) {
    if (mRepeatDirection < 0) {
      if (thumbRect.x < mDestinationPoint.x) {
        stop = true;
      }
    } else {
      if (thumbRect.x + thumbRect.width > mDestinationPoint.x) {
        stop = true;
      }
    }
  } else {
    if (mRepeatDirection < 0) {
      if (thumbRect.y < mDestinationPoint.y) {
        stop = true;
      }
    } else {
      if (thumbRect.y + thumbRect.height > mDestinationPoint.y) {
        stop = true;
      }
    }
  }

  if (stop) {
    StopRepeat();
  } else {
    PageScroll(true);
  }
}

void nsSliderFrame::PageScroll(bool aClickAndHold) {
  int32_t changeDirection = mRepeatDirection;
  nsScrollbarFrame* sb = Scrollbar();

  ScrollContainerFrame* sf = GetScrollContainerFrame();
  const ScrollSnapFlags scrollSnapFlags =
      ScrollSnapFlags::IntendedDirection | ScrollSnapFlags::IntendedEndPosition;

  // If our nsIScrollbarMediator implementation is a ScrollContainerFrame,
  // use ScrollTo() to ensure we do not scroll past the intended
  // destination. Otherwise, the combination of smooth scrolling and
  // ScrollBy() semantics (which adds the delta to the current destination
  // if there is a smooth scroll in progress) can lead to scrolling too far
  // (bug 1331390).
  // Only do this when the page scroll is triggered by the repeat timer
  // when the mouse is being held down. For multiple clicks in
  // succession, we want to make sure we scroll by a full page for
  // each click, so we use ScrollByPage().
  if (aClickAndHold && sf) {
    const bool isHorizontal = sb->IsHorizontal();

    nsIFrame* thumbFrame = mFrames.FirstChild();
    if (!thumbFrame) {
      return;
    }

    nsRect thumbRect = thumbFrame->GetRect();

    nscoord maxDistanceAlongTrack;
    if (isHorizontal) {
      maxDistanceAlongTrack =
          mDestinationPoint.x - thumbRect.x - thumbRect.width / 2;
    } else {
      maxDistanceAlongTrack =
          mDestinationPoint.y - thumbRect.y - thumbRect.height / 2;
    }

    // Convert distance along scrollbar track to amount of scrolled content.
    nscoord maxDistanceToScroll = maxDistanceAlongTrack / GetThumbRatio();

    const CSSIntCoord pageLength = Scrollbar()->GetPageIncrement();

    nsPoint pos = sf->GetScrollPosition();

    if (mCurrentClickHoldDestination) {
      // We may not have arrived at the destination of the scroll from the
      // previous repeat timer tick, some of that scroll may still be pending.
      nsPoint pendingScroll =
          *mCurrentClickHoldDestination - sf->GetScrollPosition();

      // Scroll by one page relative to the previous destination, so that we
      // scroll at a rate of a full page per repeat timer tick.
      pos += pendingScroll;

      // Make a corresponding adjustment to the maxium distance we can scroll,
      // so we successfully avoid overshoot.
      maxDistanceToScroll -= (isHorizontal ? pendingScroll.x : pendingScroll.y);
    }

    nscoord distanceToScroll =
        std::min(abs(maxDistanceToScroll),
                 CSSPixel::ToAppUnits(CSSCoord(pageLength))) *
        changeDirection;

    if (isHorizontal) {
      pos.x += distanceToScroll;
    } else {
      pos.y += distanceToScroll;
    }

    mCurrentClickHoldDestination = Some(pos);
    sf->ScrollTo(pos,
                 nsLayoutUtils::IsSmoothScrollingEnabled() &&
                         StaticPrefs::general_smoothScroll_pages()
                     ? ScrollMode::Smooth
                     : ScrollMode::Instant,
                 nullptr, scrollSnapFlags);

    return;
  }

  if (nsIScrollbarMediator* m = sb->GetScrollbarMediator()) {
    m->ScrollByPage(sb, changeDirection, scrollSnapFlags);
  }
}

void nsSliderFrame::SetupDrag(WidgetGUIEvent* aEvent, nsIFrame* aThumbFrame,
                              nscoord aPos, bool aIsHorizontal) {
  if (aIsHorizontal) {
    mThumbStart = aThumbFrame->GetPosition().x;
  } else {
    mThumbStart = aThumbFrame->GetPosition().y;
  }

  mDragStart = aPos - mThumbStart;

  mScrollingWithAPZ = false;
  StartAPZDrag(aEvent);  // sets mScrollingWithAPZ=true if appropriate

#ifdef DEBUG_SLIDER
  printf("Pressed mDragStart=%d\n", mDragStart);
#endif

  if (!mScrollingWithAPZ) {
    SuppressDisplayport();
  }
}

float nsSliderFrame::GetThumbRatio() const {
  // mRatio is in thumb app units per scrolled css pixels. Convert it to a
  // ratio of the thumb's CSS pixels per scrolled CSS pixels. (Note the thumb
  // is in the scrollframe's parent's space whereas the scrolled CSS pixels
  // are in the scrollframe's space).
  return mRatio / AppUnitsPerCSSPixel();
}

void nsSliderFrame::AsyncScrollbarDragInitiated(uint64_t aDragBlockId) {
  mAPZDragInitiated = Some(aDragBlockId);
}

void nsSliderFrame::AsyncScrollbarDragRejected() {
  mScrollingWithAPZ = false;
  // Only suppress the displayport if we're still dragging the thumb.
  // Otherwise, no one will unsuppress it.
  if (IsDraggingThumb()) {
    SuppressDisplayport();
  }
}

void nsSliderFrame::SuppressDisplayport() {
  if (!mSuppressionActive) {
    PresShell()->SuppressDisplayport(true);
    mSuppressionActive = true;
  }
}

void nsSliderFrame::UnsuppressDisplayport() {
  if (mSuppressionActive) {
    PresShell()->SuppressDisplayport(false);
    mSuppressionActive = false;
  }
}

bool nsSliderFrame::OnlySystemGroupDispatch(EventMessage aMessage) const {
  // If we are in a native anonymous subtree, do not dispatch mouse-move or
  // pointer-move events targeted at this slider frame to web content. This
  // matches the behaviour of other browsers.
  return (aMessage == eMouseMove || aMessage == ePointerMove) &&
         IsDraggingThumb() && GetContent()->IsInNativeAnonymousSubtree();
}

bool nsSliderFrame::GetEventPoint(WidgetGUIEvent* aEvent, nsPoint& aPoint) {
  LayoutDeviceIntPoint refPoint;
  if (!GetEventPoint(aEvent, refPoint)) {
    return false;
  }
  aPoint = nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, refPoint,
                                                        RelativeTo{this});
  return true;
}

bool nsSliderFrame::GetEventPoint(WidgetGUIEvent* aEvent,
                                  LayoutDeviceIntPoint& aPoint) {
  NS_ENSURE_TRUE(aEvent, false);
  WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
  if (touchEvent) {
    // return false if there is more than one touch on the page, or if
    // we can't find a touch point
    if (touchEvent->mTouches.Length() != 1) {
      return false;
    }

    dom::Touch* touch = touchEvent->mTouches.SafeElementAt(0);
    if (!touch) {
      return false;
    }
    aPoint = touch->mRefPoint;
  } else {
    aPoint = aEvent->mRefPoint;
  }
  return true;
}

NS_IMPL_ISUPPORTS(nsSliderMediator, nsIDOMEventListener)
