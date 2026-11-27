/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsScrollbarButtonFrame.h"

#include "mozilla/LookAndFeel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "nsCOMPtr.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIScrollbarMediator.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPresContext.h"
#include "nsRepeatService.h"
#include "nsScrollbarFrame.h"
#include "nsSliderFrame.h"

using namespace mozilla;

//
// NS_NewToolbarFrame
//
// Creates a new Toolbar frame and returns it
//
nsIFrame* NS_NewScrollbarButtonFrame(PresShell* aPresShell,
                                     ComputedStyle* aStyle) {
  return new nsScrollbarButtonFrame(aStyle, aPresShell->GetPresContext());
}
NS_IMPL_FRAMEARENA_HELPERS(nsScrollbarButtonFrame)

nsresult nsScrollbarButtonFrame::HandleEvent(nsPresContext* aPresContext,
                                             WidgetGUIEvent* aEvent,
                                             nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  // If a web page calls event.preventDefault() we still want to
  // scroll when scroll arrow is clicked. See bug 511075.
  if (!mContent->IsInNativeAnonymousSubtree() &&
      nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  switch (aEvent->mMessage) {
    case eMouseDown:
      mCursorOnThis = true;
      // if we didn't handle the press ourselves, pass it on to the superclass
      if (HandleButtonPress(aPresContext, aEvent, aEventStatus)) {
        return NS_OK;
      }
      break;
    case eMouseUp:
      HandleRelease(aPresContext, aEvent, aEventStatus);
      break;
    case eMouseOut:
      mCursorOnThis = false;
      break;
    case eMouseMove: {
      nsPoint cursor = nsLayoutUtils::GetEventCoordinatesRelativeTo(
          aEvent, RelativeTo{this});
      nsRect frameRect(nsPoint(0, 0), GetSize());
      mCursorOnThis = frameRect.Contains(cursor);
      break;
    }
    default:
      break;
  }

  return SimpleXULLeafFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

bool nsScrollbarButtonFrame::HandleButtonPress(nsPresContext* aPresContext,
                                               WidgetGUIEvent* aEvent,
                                               nsEventStatus* aEventStatus) {
  // Get the desired action for the scrollbar button.
  LookAndFeel::IntID tmpAction;
  uint16_t button = aEvent->AsMouseEvent()->mButton;
  if (button == MouseButton::ePrimary) {
    tmpAction = LookAndFeel::IntID::ScrollButtonLeftMouseButtonAction;
  } else if (button == MouseButton::eMiddle) {
    tmpAction = LookAndFeel::IntID::ScrollButtonMiddleMouseButtonAction;
  } else if (button == MouseButton::eSecondary) {
    tmpAction = LookAndFeel::IntID::ScrollButtonRightMouseButtonAction;
  } else {
    return false;
  }

  // Get the button action metric from the pres. shell.
  int32_t pressedButtonAction;
  if (NS_FAILED(LookAndFeel::GetInt(tmpAction, &pressedButtonAction))) {
    return false;
  }

  // get the scrollbar control
  nsScrollbarFrame* scrollbar = GetScrollbar();
  if (!scrollbar) {
    return false;
  }

  static dom::Element::AttrValuesArray strings[] = {
      nsGkAtoms::increment, nsGkAtoms::decrement, nullptr};
  int32_t index = mContent->AsElement()->FindAttrValueIn(
      kNameSpaceID_None, nsGkAtoms::type, strings, eCaseMatters);
  int32_t direction;
  if (index == 0) {
    direction = 1;
  } else if (index == 1) {
    direction = -1;
  } else {
    return false;
  }

  const bool repeat = pressedButtonAction != 2;

  PresShell::SetCapturingContent(mContent, CaptureFlags::IgnoreAllowedState);

  AutoWeakFrame weakFrame(this);

  nsIScrollbarMediator* m = scrollbar->GetScrollbarMediator();
  switch (pressedButtonAction) {
    case 0:
      scrollbar->SetButtonScrollDirectionAndUnit(direction, ScrollUnit::LINES);
      if (m) {
        m->ScrollByLine(scrollbar, direction,
                        ScrollSnapFlags::IntendedDirection);
      }
      break;
    case 1:
      scrollbar->SetButtonScrollDirectionAndUnit(direction, ScrollUnit::PAGES);
      if (m) {
        m->ScrollByPage(scrollbar, direction,
                        ScrollSnapFlags::IntendedDirection |
                            ScrollSnapFlags::IntendedEndPosition);
      }
      break;
    case 2:
      scrollbar->SetButtonScrollDirectionAndUnit(direction, ScrollUnit::WHOLE);
      if (m) {
        m->ScrollByWhole(scrollbar, direction,
                         ScrollSnapFlags::IntendedEndPosition);
      }
      break;
    case 3:
    default:
      // We were told to ignore this click, or someone assigned a non-standard
      // value to the button's action.
      return false;
  }
  if (!weakFrame.IsAlive()) {
    return false;
  }
  if (repeat) {
    StartRepeat();
  }
  return true;
}

NS_IMETHODIMP
nsScrollbarButtonFrame::HandleRelease(nsPresContext* aPresContext,
                                      WidgetGUIEvent* aEvent,
                                      nsEventStatus* aEventStatus) {
  PresShell::ReleaseCapturingContent();
  StopRepeat();
  if (nsScrollbarFrame* scrollbar = GetScrollbar()) {
    if (nsIScrollbarMediator* m = scrollbar->GetScrollbarMediator()) {
      m->ScrollbarReleased(scrollbar);
    }
  }
  return NS_OK;
}

void nsScrollbarButtonFrame::Notify() {
  if (mCursorOnThis ||
      LookAndFeel::GetInt(LookAndFeel::IntID::ScrollbarButtonAutoRepeatBehavior,
                          0)) {
    // get the scrollbar control
    if (nsScrollbarFrame* sb = GetScrollbar()) {
      if (nsIScrollbarMediator* m = sb->GetScrollbarMediator()) {
        m->RepeatButtonScroll(sb);
      }
    }
  }
}

void nsScrollbarButtonFrame::StopRepeat() {
  nsRepeatService::GetInstance()->Stop(Notify, this);

  nsScrollbarFrame* scrollbar = GetScrollbar();
  if (!scrollbar) {
    return;
  }
  scrollbar->SetButtonScrollDirectionAndUnit(0, ScrollUnit::WHOLE);
}

nsIScrollbarMediator* nsScrollbarButtonFrame::GetMediator() {
  if (auto* sb = GetScrollbar()) {
    return sb->GetScrollbarMediator();
  }
  return nullptr;
}

nsScrollbarFrame* nsScrollbarButtonFrame::GetScrollbar() {
  for (nsIFrame* cur = GetParent(); cur; cur = cur->GetParent()) {
    if (cur->IsScrollbarFrame()) {
      return static_cast<nsScrollbarFrame*>(cur);
    }
  }
  return nullptr;
}

void nsScrollbarButtonFrame::Destroy(DestroyContext& aContext) {
  // Ensure our repeat service isn't going... it's possible that a scrollbar can
  // disappear out from under you while you're in the process of scrolling.
  StopRepeat();
  SimpleXULLeafFrame::Destroy(aContext);
}
