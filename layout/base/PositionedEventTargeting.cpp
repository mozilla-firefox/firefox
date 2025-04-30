/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PositionedEventTargeting.h"

#include "Units.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/ToString.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsContainerFrame.h"
#include "nsCoord.h"
#include "nsFrameList.h"  // for DEBUG_FRAME_DUMP
#include "nsHTMLParts.h"
#include "nsLayoutUtils.h"
#include "nsGkAtoms.h"
#include "nsFontMetrics.h"
#include "nsIContentInlines.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "mozilla/dom/Element.h"
#include "nsRegion.h"
#include "nsDeviceContext.h"
#include "nsIFrame.h"
#include <algorithm>

using namespace mozilla;
using namespace mozilla::dom;

// If debugging this code you may wish to enable this logging, via
// the env var MOZ_LOG="event.retarget:4". For extra logging (getting
// frame dumps, use MOZ_LOG="event.retarget:5".
static mozilla::LazyLogModule sEvtTgtLog("event.retarget");
#define PET_LOG(...) MOZ_LOG(sEvtTgtLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

/*
 * The basic goal of FindFrameTargetedByInputEvent() is to find a good
 * target element that can respond to mouse events. Both mouse events and touch
 * events are targeted at this element. Note that even for touch events, we
 * check responsiveness to mouse events. We assume Web authors
 * designing for touch events will take their own steps to account for
 * inaccurate touch events.
 *
 * GetClickableAncestor() encapsulates the heuristic that determines whether an
 * element is expected to respond to mouse events. An element is deemed
 * "clickable" if it has registered listeners for "click", "mousedown" or
 * "mouseup", or is on a whitelist of element tags (<a>, <button>, <input>,
 * <select>, <textarea>, <label>), or has role="button", or is a link, or
 * is a suitable XUL element.
 * Any descendant (in the same document) of a clickable element is also
 * deemed clickable since events will propagate to the clickable element from
 * its descendant.
 *
 * If the element directly under the event position is clickable (or
 * event radii are disabled), we always use that element. Otherwise we collect
 * all frames intersecting a rectangle around the event position (taking CSS
 * transforms into account) and choose the best candidate in GetClosest().
 * Only GetClickableAncestor() candidates are considered; if none are found,
 * then we revert to targeting the element under the event position.
 * We ignore candidates outside the document subtree rooted by the
 * document of the element directly under the event position. This ensures that
 * event listeners in ancestor documents don't make it completely impossible
 * to target a non-clickable element in a child document.
 *
 * When both a frame and its ancestor are in the candidate list, we ignore
 * the ancestor. Otherwise a large ancestor element with a mouse event listener
 * and some descendant elements that need to be individually targetable would
 * disable intelligent targeting of those descendants within its bounds.
 *
 * GetClosest() computes the transformed axis-aligned bounds of each
 * candidate frame, then computes the Manhattan distance from the event point
 * to the bounds rect (which can be zero). The frame with the
 * shortest distance is chosen. For visited links we multiply the distance
 * by a specified constant weight; this can be used to make visited links
 * more or less likely to be targeted than non-visited links.
 */

// Enum that determines which type of elements to count as targets in the
// search. Clickable elements are generally ones that respond to click events,
// like form inputs and links and things with click event listeners.
// Touchable elements are a much narrower set of elements; ones with touchstart
// and touchend listeners.
enum class SearchType {
  None,
  Clickable,
  Touchable,
  TouchableOrClickable,
};

struct EventRadiusPrefs {
  bool mEnabled;            // other fields are valid iff this field is true
  uint32_t mVisitedWeight;  // in percent, i.e. default is 100
  uint32_t mRadiusTopmm;
  uint32_t mRadiusRightmm;
  uint32_t mRadiusBottommm;
  uint32_t mRadiusLeftmm;
  bool mTouchOnly;
  bool mReposition;
  SearchType mSearchType;

  explicit EventRadiusPrefs(WidgetGUIEvent* aMouseOrTouchEvent) {
    if (aMouseOrTouchEvent->mClass == eTouchEventClass) {
      mEnabled = StaticPrefs::ui_touch_radius_enabled();
      mVisitedWeight = StaticPrefs::ui_touch_radius_visitedWeight();
      mRadiusTopmm = StaticPrefs::ui_touch_radius_topmm();
      mRadiusRightmm = StaticPrefs::ui_touch_radius_rightmm();
      mRadiusBottommm = StaticPrefs::ui_touch_radius_bottommm();
      mRadiusLeftmm = StaticPrefs::ui_touch_radius_leftmm();
      mTouchOnly = false;   // Always false, unlike mouse events.
      mReposition = false;  // Always false, unlike mouse events.
      if (StaticPrefs::
              ui_touch_radius_single_touch_treat_clickable_as_touchable() &&
          aMouseOrTouchEvent->mMessage == eTouchStart &&
          aMouseOrTouchEvent->AsTouchEvent()->mTouches.Length() == 1) {
        // If it may cause a single tap, we need to refer clickable target too
        // because the touchstart target will be captured implicitly if the
        // web app does not capture the touch explicitly.
        mSearchType = SearchType::TouchableOrClickable;
      } else {
        mSearchType = SearchType::Touchable;
      }

    } else if (aMouseOrTouchEvent->mClass == eMouseEventClass) {
      mEnabled = StaticPrefs::ui_mouse_radius_enabled();
      mVisitedWeight = StaticPrefs::ui_mouse_radius_visitedWeight();
      mRadiusTopmm = StaticPrefs::ui_mouse_radius_topmm();
      mRadiusRightmm = StaticPrefs::ui_mouse_radius_rightmm();
      mRadiusBottommm = StaticPrefs::ui_mouse_radius_bottommm();
      mRadiusLeftmm = StaticPrefs::ui_mouse_radius_leftmm();
      mTouchOnly = StaticPrefs::ui_mouse_radius_inputSource_touchOnly();
      mReposition = StaticPrefs::ui_mouse_radius_reposition();
      mSearchType = SearchType::Clickable;

    } else {
      mEnabled = false;
      mVisitedWeight = 0;
      mRadiusTopmm = 0;
      mRadiusRightmm = 0;
      mRadiusBottommm = 0;
      mRadiusLeftmm = 0;
      mTouchOnly = false;
      mReposition = false;
      mSearchType = SearchType::None;
    }
  }
};

static bool HasMouseListener(const nsIContent* aContent) {
  if (EventListenerManager* elm = aContent->GetExistingListenerManager()) {
    return elm->HasListenersFor(nsGkAtoms::onclick) ||
           elm->HasListenersFor(nsGkAtoms::onmousedown) ||
           elm->HasListenersFor(nsGkAtoms::onmouseup);
  }

  return false;
}

static bool HasTouchListener(const nsIContent* aContent) {
  EventListenerManager* elm = aContent->GetExistingListenerManager();
  if (!elm) {
    return false;
  }

  // FIXME: Should this really use the pref rather than TouchEvent::PrefEnabled
  // or such?
  if (!StaticPrefs::dom_w3c_touch_events_enabled()) {
    return false;
  }

  return elm->HasNonSystemGroupListenersFor(nsGkAtoms::ontouchstart) ||
         elm->HasNonSystemGroupListenersFor(nsGkAtoms::ontouchend);
}

static bool HasPointerListener(const nsIContent* aContent) {
  EventListenerManager* elm = aContent->GetExistingListenerManager();
  if (!elm) {
    return false;
  }

  return elm->HasListenersFor(nsGkAtoms::onpointerdown) ||
         elm->HasListenersFor(nsGkAtoms::onpointerup);
}

static bool IsDescendant(nsIFrame* aFrame, nsIContent* aAncestor,
                         nsAutoString* aLabelTargetId) {
  for (nsIContent* content = aFrame->GetContent(); content;
       content = content->GetFlattenedTreeParent()) {
    if (aLabelTargetId && content->IsHTMLElement(nsGkAtoms::label)) {
      content->AsElement()->GetAttr(nsGkAtoms::_for, *aLabelTargetId);
    }
    if (content == aAncestor) {
      return true;
    }
  }
  return false;
}

static nsIContent* GetTouchableAncestor(nsIFrame* aFrame,
                                        nsAtom* aStopAt = nullptr) {
  // Input events propagate up the content tree so we'll follow the content
  // ancestors to look for elements accepting the touch event.
  for (nsIContent* content = aFrame->GetContent(); content;
       content = content->GetFlattenedTreeParent()) {
    if (aStopAt && content->IsHTMLElement(aStopAt)) {
      break;
    }
    if (HasTouchListener(content)) {
      return content;
    }
  }
  return nullptr;
}

static bool IsClickableContent(const nsIContent* aContent,
                               nsAutoString* aLabelTargetId = nullptr) {
  if (HasTouchListener(aContent) || HasMouseListener(aContent) ||
      HasPointerListener(aContent)) {
    return true;
  }
  if (aContent->IsAnyOfHTMLElements(nsGkAtoms::button, nsGkAtoms::input,
                                    nsGkAtoms::select, nsGkAtoms::textarea)) {
    return true;
  }
  if (aContent->IsHTMLElement(nsGkAtoms::label)) {
    if (aLabelTargetId) {
      aContent->AsElement()->GetAttr(nsGkAtoms::_for, *aLabelTargetId);
    }
    return aContent;
  }

  // See nsCSSFrameConstructor::FindXULTagData. This code is not
  // really intended to be used with XUL, though.
  if (aContent->IsAnyOfXULElements(
          nsGkAtoms::button, nsGkAtoms::checkbox, nsGkAtoms::radio,
          nsGkAtoms::menu, nsGkAtoms::menuitem, nsGkAtoms::menulist,
          nsGkAtoms::scrollbarbutton, nsGkAtoms::resizer)) {
    return true;
  }

  static Element::AttrValuesArray clickableRoles[] = {nsGkAtoms::button,
                                                      nsGkAtoms::key, nullptr};
  if (const auto* element = Element::FromNode(*aContent)) {
    if (element->IsLink()) {
      return true;
    }
    if (element->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::role,
                                 clickableRoles, eIgnoreCase) >= 0) {
      return true;
    }
  }
  return aContent->IsEditable();
}

static nsIContent* GetClickableAncestor(
    nsIFrame* aFrame, nsAtom* aStopAt = nullptr,
    nsAutoString* aLabelTargetId = nullptr) {
  // If the frame is `cursor:pointer` or inherits `cursor:pointer` from an
  // ancestor, treat it as clickable. This is a heuristic to deal with pages
  // where the click event listener is on the <body> or <html> element but it
  // triggers an action on some specific element. We want the specific element
  // to be considered clickable, and at least some pages that do this indicate
  // the clickability by setting `cursor:pointer`, so we use that here.
  // Note that descendants of `cursor:pointer` elements that override the
  // inherited `pointer` to `auto` or any other value are NOT treated as
  // clickable, because it seems like the content author is trying to express
  // non-clickability on that sub-element.
  // In the future depending on real-world cases it might make sense to expand
  // this check to any non-auto cursor. Such a change would also pick up things
  // like contenteditable or input fields, which can then be removed from the
  // loop below, and would have better performance.
  if (aFrame->StyleUI()->Cursor().keyword == StyleCursorKind::Pointer) {
    // XXX Shouldn't we set aLabelTargetId if aFrame is for a <label>?
    return aFrame->GetContent();
  }

  // Input events propagate up the content tree so we'll follow the content
  // ancestors to look for elements accepting the click.
  for (nsIContent* content = aFrame->GetContent(); content;
       content = content->GetFlattenedTreeParent()) {
    if (aStopAt && content->IsHTMLElement(aStopAt)) {
      break;
    }
    if (IsClickableContent(content, aLabelTargetId)) {
      return content;
    }
  }
  return nullptr;
}

static nsIContent* GetTouchableOrClickableAncestor(
    nsIFrame* aFrame, nsAtom* aStopAt = nullptr,
    nsAutoString* aLabelTargetId = nullptr) {
  nsIContent* deepestClickableTarget = nullptr;
  // See comment in GetClickableAncestor for the detail of referring CSS
  // `cursor`.
  if (aFrame->StyleUI()->Cursor().keyword == StyleCursorKind::Pointer) {
    deepestClickableTarget = aFrame->GetContent();
  }
  for (nsIContent* content = aFrame->GetContent(); content;
       content = content->GetFlattenedTreeParent()) {
    if (aStopAt && content->IsHTMLElement(aStopAt)) {
      break;
    }
    // If we find a touchable content, let's target it.
    if (HasTouchListener(content)) {
      if (aLabelTargetId) {
        aLabelTargetId->Truncate();
      }
      return content;
    }
    // If we find a clickable content, let's store it and use it as the last
    // resort if there is no touchable ancestor.
    if (!deepestClickableTarget &&
        IsClickableContent(content, aLabelTargetId)) {
      deepestClickableTarget = content;
    }
  }
  return deepestClickableTarget;
}

static Scale2D AppUnitsToMMScale(RelativeTo aFrame) {
  nsPresContext* presContext = aFrame.mFrame->PresContext();

  const int32_t appUnitsPerInch =
      presContext->DeviceContext()->AppUnitsPerPhysicalInch();
  const float appUnits =
      static_cast<float>(appUnitsPerInch) / MM_PER_INCH_FLOAT;

  // Visual coordinates are only used for quantities relative to the
  // cross-process root content document's root frame. There should
  // not be an enclosing resolution or transform scale above that.
  if (aFrame.mViewportType != ViewportType::Layout) {
    const nscoord scale = NSToCoordRound(appUnits);
    return Scale2D{static_cast<float>(scale), static_cast<float>(scale)};
  }

  Scale2D localResolution{1.0f, 1.0f};
  Scale2D enclosingResolution{1.0f, 1.0f};

  if (auto* pc = presContext->GetInProcessRootContentDocumentPresContext()) {
    PresShell* presShell = pc->PresShell();
    localResolution = {presShell->GetResolution(), presShell->GetResolution()};
    enclosingResolution = ViewportUtils::TryInferEnclosingResolution(presShell);
  }

  const gfx::MatrixScales parentScale =
      nsLayoutUtils::GetTransformToAncestorScale(aFrame.mFrame);
  const Scale2D resolution =
      localResolution * parentScale * enclosingResolution;

  const nscoord scaleX = NSToCoordRound(appUnits / resolution.xScale);
  const nscoord scaleY = NSToCoordRound(appUnits / resolution.yScale);

  return {static_cast<float>(scaleX), static_cast<float>(scaleY)};
}

/**
 * Clip aRect with the bounds of aFrame in the coordinate system of
 * aRootFrame. aRootFrame is an ancestor of aFrame.
 */
static nsRect ClipToFrame(RelativeTo aRootFrame, const nsIFrame* aFrame,
                          nsRect& aRect) {
  nsRect bound = nsLayoutUtils::TransformFrameRectToAncestor(
      aFrame, nsRect(nsPoint(0, 0), aFrame->GetSize()), aRootFrame);
  nsRect result = bound.Intersect(aRect);
  return result;
}

static nsRect GetTargetRect(RelativeTo aRootFrame,
                            const nsPoint& aPointRelativeToRootFrame,
                            const nsIFrame* aRestrictToDescendants,
                            const EventRadiusPrefs& aPrefs, uint32_t aFlags) {
  const Scale2D scale = AppUnitsToMMScale(aRootFrame);
  nsMargin m(aPrefs.mRadiusTopmm * scale.yScale,
             aPrefs.mRadiusRightmm * scale.xScale,
             aPrefs.mRadiusBottommm * scale.yScale,
             aPrefs.mRadiusLeftmm * scale.xScale);
  nsRect r(aPointRelativeToRootFrame, nsSize(0, 0));
  r.Inflate(m);
  if (!(aFlags & INPUT_IGNORE_ROOT_SCROLL_FRAME)) {
    // Don't clip this rect to the root scroll frame if the flag to ignore the
    // root scroll frame is set. Note that the GetClosest code will still
    // enforce that the target found is a descendant of aRestrictToDescendants.
    r = ClipToFrame(aRootFrame, aRestrictToDescendants, r);
  }
  return r;
}

static float ComputeDistanceFromRect(const nsPoint& aPoint,
                                     const nsRect& aRect) {
  nscoord dx =
      std::max(0, std::max(aRect.x - aPoint.x, aPoint.x - aRect.XMost()));
  nscoord dy =
      std::max(0, std::max(aRect.y - aPoint.y, aPoint.y - aRect.YMost()));
  return float(NS_hypot(dx, dy));
}

static float ComputeDistanceFromRegion(const nsPoint& aPoint,
                                       const nsRegion& aRegion) {
  MOZ_ASSERT(!aRegion.IsEmpty(),
             "can't compute distance between point and empty region");
  float minDist = -1;
  for (auto iter = aRegion.RectIter(); !iter.Done(); iter.Next()) {
    float dist = ComputeDistanceFromRect(aPoint, iter.Get());
    if (dist < minDist || minDist < 0) {
      minDist = dist;
    }
  }
  return minDist;
}

// Subtract aRegion from aExposedRegion as long as that doesn't make the
// exposed region get too complex or removes a big chunk of the exposed region.
static void SubtractFromExposedRegion(nsRegion* aExposedRegion,
                                      const nsRegion& aRegion) {
  if (aRegion.IsEmpty()) {
    return;
  }

  nsRegion tmp;
  tmp.Sub(*aExposedRegion, aRegion);
  // Don't let *aExposedRegion get too complex, but don't let it fluff out to
  // its bounds either. Do let aExposedRegion get more complex if by doing so
  // we reduce its area by at least half.
  if (tmp.GetNumRects() <= 15 || tmp.Area() <= aExposedRegion->Area() / 2) {
    *aExposedRegion = tmp;
  }
}

static nsIFrame* GetClosest(RelativeTo aRoot,
                            const nsPoint& aPointRelativeToRootFrame,
                            const nsRect& aTargetRect,
                            const EventRadiusPrefs& aPrefs,
                            const nsIFrame* aRestrictToDescendants,
                            nsIContent* aClickableAncestor,
                            nsTArray<nsIFrame*>& aCandidates) {
  nsIFrame* bestTarget = nullptr;
  // Lower is better; distance is in appunits
  float bestDistance = 1e6f;
  nsRegion exposedRegion(aTargetRect);
  for (uint32_t i = 0; i < aCandidates.Length(); ++i) {
    nsIFrame* f = aCandidates[i];

    bool preservesAxisAlignedRectangles = false;
    nsRect borderBox = nsLayoutUtils::TransformFrameRectToAncestor(
        f, nsRect(nsPoint(0, 0), f->GetSize()), aRoot,
        &preservesAxisAlignedRectangles);
    PET_LOG("Checking candidate %p with border box %s\n", f,
            ToString(borderBox).c_str());
    nsRegion region;
    region.And(exposedRegion, borderBox);
    if (region.IsEmpty()) {
      PET_LOG("  candidate %p had empty hit region\n", f);
      continue;
    }

    if (preservesAxisAlignedRectangles) {
      // Subtract from the exposed region if we have a transform that won't make
      // the bounds include a bunch of area that we don't actually cover.
      SubtractFromExposedRegion(&exposedRegion, region);
    }

    nsAutoString labelTargetId;
    if (aClickableAncestor &&
        !IsDescendant(f, aClickableAncestor, &labelTargetId)) {
      PET_LOG("  candidate %p is not a descendant of required ancestor\n", f);
      continue;
    }

    switch (aPrefs.mSearchType) {
      case SearchType::Clickable: {
        nsIContent* clickableContent =
            GetClickableAncestor(f, nsGkAtoms::body, &labelTargetId);
        if (!aClickableAncestor && !clickableContent) {
          PET_LOG("  candidate %p was not clickable\n", f);
          continue;
        }
        break;
      }
      case SearchType::Touchable: {
        nsIContent* touchableContent = GetTouchableAncestor(f, nsGkAtoms::body);
        if (!touchableContent) {
          PET_LOG("  candidate %p was not touchable\n", f);
          continue;
        }
        break;
      }
      case SearchType::TouchableOrClickable: {
        nsIContent* touchableOrClickableContent =
            GetTouchableOrClickableAncestor(f, nsGkAtoms::body, &labelTargetId);
        if (!touchableOrClickableContent) {
          PET_LOG("  candidate %p was not touchable nor clickable\n", f);
          continue;
        }
        break;
      }
      case SearchType::None:
        break;
    }

    // If our current closest frame is a descendant of 'f', skip 'f' (prefer
    // the nested frame).
    if (bestTarget && nsLayoutUtils::IsProperAncestorFrameCrossDoc(
                          f, bestTarget, aRoot.mFrame)) {
      PET_LOG("  candidate %p was ancestor for bestTarget %p\n", f, bestTarget);
      continue;
    }
    if (!aClickableAncestor && !nsLayoutUtils::IsAncestorFrameCrossDoc(
                                   aRestrictToDescendants, f, aRoot.mFrame)) {
      PET_LOG("  candidate %p was not descendant of restrictroot %p\n", f,
              aRestrictToDescendants);
      continue;
    }

    // distance is in appunits
    float distance =
        ComputeDistanceFromRegion(aPointRelativeToRootFrame, region);
    nsIContent* content = f->GetContent();
    if (content && content->IsElement() &&
        content->AsElement()->State().HasState(
            ElementState(ElementState::VISITED))) {
      distance *= aPrefs.mVisitedWeight / 100.0f;
    }
    // XXX When we look for a touchable or clickable target, should we give
    // lower weight for clickable target?
    if (distance < bestDistance) {
      PET_LOG("  candidate %p is the new best\n", f);
      bestDistance = distance;
      bestTarget = f;
    }
  }
  return bestTarget;
}

// Walk from aTarget up to aRoot, and return the first frame found with an
// explicit z-index set on it. If no such frame is found, aRoot is returned.
static const nsIFrame* FindZIndexAncestor(const nsIFrame* aTarget,
                                          const nsIFrame* aRoot) {
  const nsIFrame* candidate = aTarget;
  while (candidate && candidate != aRoot) {
    if (candidate->ZIndex().valueOr(0) > 0) {
      PET_LOG("Restricting search to z-index root %p\n", candidate);
      return candidate;
    }
    candidate = candidate->GetParent();
  }
  return aRoot;
}

nsIFrame* FindFrameTargetedByInputEvent(
    WidgetGUIEvent* aEvent, RelativeTo aRootFrame,
    const nsPoint& aPointRelativeToRootFrame, uint32_t aFlags) {
  using FrameForPointOption = nsLayoutUtils::FrameForPointOption;
  EnumSet<FrameForPointOption> options;
  if (aFlags & INPUT_IGNORE_ROOT_SCROLL_FRAME) {
    options += FrameForPointOption::IgnoreRootScrollFrame;
  }
  nsIFrame* target = nsLayoutUtils::GetFrameForPoint(
      aRootFrame, aPointRelativeToRootFrame, options);
  nsIFrame* initialTarget = target;
  PET_LOG(
      "Found initial target %p for event class %s message %s point %s "
      "relative to root frame %s\n",
      target, ToChar(aEvent->mClass), ToChar(aEvent->mMessage),
      ToString(aPointRelativeToRootFrame).c_str(),
      ToString(aRootFrame).c_str());

  EventRadiusPrefs prefs(aEvent);
  if (!prefs.mEnabled || EventRetargetSuppression::IsActive()) {
    PET_LOG("Retargeting disabled\n");
    return target;
  }

  // Do not modify targeting for actual mouse hardware; only for mouse
  // events generated by touch-screen hardware.
  if (aEvent->mClass == eMouseEventClass && prefs.mTouchOnly &&
      aEvent->AsMouseEvent()->mInputSource !=
          MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
    PET_LOG("Mouse input event is not from a touch source\n");
    return target;
  }

  // If the exact target is non-null, only consider candidate targets in the
  // same document as the exact target. Otherwise, if an ancestor document has
  // a mouse event handler for example, targets that are !GetClickableAncestor
  // can never be targeted --- something nsSubDocumentFrame in an ancestor
  // document would be targeted instead.
  const nsIFrame* restrictToDescendants = [&]() -> const nsIFrame* {
    if (target && target->PresContext() != aRootFrame.mFrame->PresContext()) {
      return target->PresShell()->GetRootFrame();
    }
    return aRootFrame.mFrame;
  }();

  // Ignore retarget if target is editable.
  nsIContent* targetContent = target ? target->GetContent() : nullptr;
  if (targetContent && targetContent->IsEditable()) {
    PET_LOG("Target %p is editable\n", target);
    return target;
  }

  // If the target element inside an element with a z-index, restrict the
  // search to other elements inside that z-index. This is a heuristic
  // intended to help with a class of scenarios involving web modals or
  // web popup type things. In particular it helps alleviate bug 1666792.
  restrictToDescendants = FindZIndexAncestor(target, restrictToDescendants);

  nsRect targetRect = GetTargetRect(aRootFrame, aPointRelativeToRootFrame,
                                    restrictToDescendants, prefs, aFlags);
  PET_LOG("Expanded point to target rect %s\n", ToString(targetRect).c_str());
  AutoTArray<nsIFrame*, 8> candidates;
  nsresult rv = nsLayoutUtils::GetFramesForArea(aRootFrame, targetRect,
                                                candidates, options);
  if (NS_FAILED(rv)) {
    return target;
  }

  nsIContent* clickableAncestor = nullptr;
  if (target) {
    clickableAncestor = GetClickableAncestor(target, nsGkAtoms::body);
    if (clickableAncestor) {
      PET_LOG("Target %p is clickable\n", target);
      // If the target that was directly hit has a clickable ancestor, that
      // means it too is clickable. And since it is the same as or a
      // descendant of clickableAncestor, it should become the root for the
      // GetClosest search.
      clickableAncestor = target->GetContent();
    }
  }

  nsIFrame* closest =
      GetClosest(aRootFrame, aPointRelativeToRootFrame, targetRect, prefs,
                 restrictToDescendants, clickableAncestor, candidates);
  if (closest) {
    target = closest;
  }

  PET_LOG("Final target is %p\n", target);

#ifdef DEBUG_FRAME_DUMP
  // At verbose logging level, dump the frame tree to help with debugging.
  // Note that dumping the frame tree at the top of the function may flood
  // logcat on Android devices and cause the PET_LOGs to get dropped.
  if (MOZ_LOG_TEST(sEvtTgtLog, LogLevel::Verbose)) {
    if (target) {
      target->DumpFrameTree();
    } else {
      aRootFrame.mFrame->DumpFrameTree();
    }
  }
#endif

  if (!target || !prefs.mReposition || target == initialTarget) {
    // No repositioning required for this event
    return target;
  }

  // Take the point relative to the root frame, make it relative to the target,
  // clamp it to the bounds, and then make it relative to the root frame again.
  nsPoint point = aPointRelativeToRootFrame;
  if (nsLayoutUtils::TRANSFORM_SUCCEEDED !=
      nsLayoutUtils::TransformPoint(aRootFrame, RelativeTo{target}, point)) {
    return target;
  }
  point = target->GetRectRelativeToSelf().ClampPoint(point);
  if (nsLayoutUtils::TRANSFORM_SUCCEEDED !=
      nsLayoutUtils::TransformPoint(RelativeTo{target}, aRootFrame, point)) {
    return target;
  }
  // Now we basically undo the operations in GetEventCoordinatesRelativeTo, to
  // get back the (now-clamped) coordinates in the event's widget's space.
  nsRootPresContext* rootPresContext =
      aRootFrame.mFrame->PresContext()->GetRootPresContext();
  nsView* view = rootPresContext->PresShell()->GetRootFrame()->GetView();
  if (!view) {
    return target;
  }
  // TODO: Consider adding an optimization similar to the one in
  // GetEventCoordinatesRelativeTo, where we detect cases where
  // there is no transform to apply and avoid calling
  // TransformFramePointToRoot() in those cases.
  point = nsLayoutUtils::TransformFramePointToRoot(ViewportType::Visual,
                                                   aRootFrame, point);
  LayoutDeviceIntPoint widgetPoint = nsLayoutUtils::TranslateViewToWidget(
      rootPresContext, view, point, ViewportType::Visual, aEvent->mWidget);
  if (widgetPoint.x != NS_UNCONSTRAINEDSIZE) {
    // If that succeeded, we update the point in the event
    aEvent->mRefPoint = widgetPoint;
  }
  return target;
}

uint32_t EventRetargetSuppression::sSuppressionCount = 0;

EventRetargetSuppression::EventRetargetSuppression() { sSuppressionCount++; }

EventRetargetSuppression::~EventRetargetSuppression() { sSuppressionCount--; }

bool EventRetargetSuppression::IsActive() { return sSuppressionCount > 0; }

}  // namespace mozilla
