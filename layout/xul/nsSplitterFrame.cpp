/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsSplitterFrame.h"

#include "LayoutConstants.h"
#include "SimpleXULLeafFrame.h"
#include "gfxContext.h"
#include "mozilla/CSSOrderAwareFrameIterator.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/KeyboardEvent.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/MouseEvent.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsDOMCSSDeclaration.h"
#include "nsDisplayList.h"
#include "nsFlexContainerFrame.h"
#include "nsFrameList.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsIDOMEventListener.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPresContext.h"
#include "nsScrollbarButtonFrame.h"
#include "nsStyledElement.h"
#include "nsXULElement.h"

using namespace mozilla;

using mozilla::dom::Element;
using mozilla::dom::Event;

class nsSplitterInfo {
 public:
  nscoord min;
  nscoord max;
  nscoord current;
  nscoord pref;
  nscoord changed;
  nsCOMPtr<nsIContent> childElem;
};

enum class ResizeType {
  // Resize the closest sibling in a given direction.
  Closest,
  // Resize the farthest sibling in a given direction.
  Farthest,
  // Resize only flexible siblings in a given direction.
  Flex,
  // No space should be taken out of any children in that direction.
  // FIXME(emilio): This is a rather odd name...
  Grow,
  // Only resize adjacent siblings.
  Sibling,
  // Don't resize anything in a given direction.
  None,
};
static ResizeType ResizeTypeFromAttribute(const Element& aElement,
                                          nsAtom* aAttribute) {
  static Element::AttrValuesArray strings[] = {
      nsGkAtoms::farthest, nsGkAtoms::flex, nsGkAtoms::grow,
      nsGkAtoms::sibling,  nsGkAtoms::none, nullptr};
  switch (aElement.FindAttrValueIn(kNameSpaceID_None, aAttribute, strings,
                                   eCaseMatters)) {
    case 0:
      return ResizeType::Farthest;
    case 1:
      return ResizeType::Flex;
    case 2:
      // Grow only applies to resizeAfter.
      if (aAttribute == nsGkAtoms::resizeafter) {
        return ResizeType::Grow;
      }
      break;
    case 3:
      return ResizeType::Sibling;
    case 4:
      return ResizeType::None;
    default:
      break;
  }
  return ResizeType::Closest;
}

class nsSplitterFrameInner final : public nsIDOMEventListener {
 protected:
  virtual ~nsSplitterFrameInner();

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

  explicit nsSplitterFrameInner(nsSplitterFrame* aSplitter)
      : mOuter(aSplitter) {}

  void Disconnect() { mOuter = nullptr; }

  nsresult MouseDown(Event* aMouseEvent);
  nsresult MouseUp(Event* aMouseEvent);
  nsresult MouseMove(Event* aMouseEvent);
  nsresult KeyDown(Event* aKeyEvent);

  void MouseDrag(nsPresContext* aPresContext, WidgetGUIEvent* aEvent);
  void MouseUp(nsPresContext* aPresContext, WidgetGUIEvent* aEvent);

  void AdjustChildren(nsPresContext* aPresContext);
  void AdjustChildren(nsPresContext* aPresContext,
                      nsTArray<nsSplitterInfo>& aChildInfos,
                      bool aIsHorizontal);

  void AddRemoveSpace(nscoord aDiff, nsTArray<nsSplitterInfo>& aChildInfos,
                      int32_t& aSpaceLeft);

  void ResizeChildTo(nscoord& aDiff);

  void UpdateState();

  void AddListener();
  void RemoveListener();

  enum class State { Open, CollapsedBefore, CollapsedAfter, Dragging };
  enum CollapseDirection { Before, After };

  ResizeType GetResizeBefore();
  ResizeType GetResizeAfter();
  State GetState();

  bool SupportsCollapseDirection(CollapseDirection aDirection);

  void EnsureOrient();
  void SetPreferredSize(nsIFrame* aChildBox, bool aIsHorizontal, nscoord aSize);

  // Collects child information for resizing. Returns false if the splitter
  // is at a boundary or there are no valid children to resize.
  bool CollectChildInfos();

  nsSplitterFrame* mOuter;
  bool mDidDrag = false;
  nscoord mDragStart = 0;
  nsIFrame* mParentBox = nullptr;
  bool mPressed = false;
  nsTArray<nsSplitterInfo> mChildInfosBefore;
  nsTArray<nsSplitterInfo> mChildInfosAfter;
  State mState = State::Open;
  nscoord mSplitterPos = 0;
  bool mDragging = false;

  const Element* SplitterElement() const {
    return mOuter->GetContent()->AsElement();
  }
};

NS_IMPL_ISUPPORTS(nsSplitterFrameInner, nsIDOMEventListener)

ResizeType nsSplitterFrameInner::GetResizeBefore() {
  return ResizeTypeFromAttribute(*SplitterElement(), nsGkAtoms::resizebefore);
}

ResizeType nsSplitterFrameInner::GetResizeAfter() {
  return ResizeTypeFromAttribute(*SplitterElement(), nsGkAtoms::resizeafter);
}

nsSplitterFrameInner::~nsSplitterFrameInner() = default;

nsSplitterFrameInner::State nsSplitterFrameInner::GetState() {
  static Element::AttrValuesArray strings[] = {nsGkAtoms::dragging,
                                               nsGkAtoms::collapsed, nullptr};
  static Element::AttrValuesArray strings_substate[] = {
      nsGkAtoms::before, nsGkAtoms::after, nullptr};
  switch (SplitterElement()->FindAttrValueIn(
      kNameSpaceID_None, nsGkAtoms::state, strings, eCaseMatters)) {
    case 0:
      return State::Dragging;
    case 1:
      switch (SplitterElement()->FindAttrValueIn(
          kNameSpaceID_None, nsGkAtoms::substate, strings_substate,
          eCaseMatters)) {
        case 0:
          return State::CollapsedBefore;
        case 1:
          return State::CollapsedAfter;
        default:
          if (SupportsCollapseDirection(After)) {
            return State::CollapsedAfter;
          }
          return State::CollapsedBefore;
      }
  }
  return State::Open;
}

//
// NS_NewSplitterFrame
//
// Creates a new Toolbar frame and returns it
//
nsIFrame* NS_NewSplitterFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsSplitterFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsSplitterFrame)

nsSplitterFrame::nsSplitterFrame(ComputedStyle* aStyle,
                                 nsPresContext* aPresContext)
    : SimpleXULLeafFrame(aStyle, aPresContext, kClassID) {}

void nsSplitterFrame::Destroy(DestroyContext& aContext) {
  if (mInner) {
    mInner->RemoveListener();
    mInner->Disconnect();
    mInner = nullptr;
  }
  SimpleXULLeafFrame::Destroy(aContext);
}

nsresult nsSplitterFrame::AttributeChanged(int32_t aNameSpaceID,
                                           nsAtom* aAttribute,
                                           AttrModType aModType) {
  nsresult rv =
      SimpleXULLeafFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
  if (aAttribute == nsGkAtoms::state) {
    mInner->UpdateState();
  }

  return rv;
}

/**
 * Initialize us. If we are in a box get our alignment so we know what direction
 * we are
 */
void nsSplitterFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                           nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(!mInner);
  mInner = new nsSplitterFrameInner(this);

  SimpleXULLeafFrame::Init(aContent, aParent, aPrevInFlow);

  mInner->AddListener();
  mInner->mParentBox = nullptr;
}

static bool IsValidParentBox(nsIFrame* aFrame) {
  return aFrame->IsFlexContainerFrame();
}

static nsIFrame* GetValidParentBox(nsIFrame* aChild) {
  return aChild->GetParent() && IsValidParentBox(aChild->GetParent())
             ? aChild->GetParent()
             : nullptr;
}

void nsSplitterFrame::Reflow(nsPresContext* aPresContext,
                             ReflowOutput& aDesiredSize,
                             const ReflowInput& aReflowInput,
                             nsReflowStatus& aStatus) {
  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    mInner->mParentBox = GetValidParentBox(this);
    mInner->UpdateState();
  }
  return SimpleXULLeafFrame::Reflow(aPresContext, aDesiredSize, aReflowInput,
                                    aStatus);
}

static bool SplitterIsHorizontal(const nsIFrame* aParentBox) {
  // If our parent is horizontal, the splitter is vertical and vice-versa.
  MOZ_ASSERT(aParentBox->IsFlexContainerFrame());
  const FlexboxAxisInfo info(aParentBox);
  return !info.mIsRowOriented;
}

NS_IMETHODIMP
nsSplitterFrame::HandlePress(nsPresContext* aPresContext,
                             WidgetGUIEvent* aEvent,
                             nsEventStatus* aEventStatus) {
  return NS_OK;
}

NS_IMETHODIMP
nsSplitterFrame::HandleMultiplePress(nsPresContext* aPresContext,
                                     WidgetGUIEvent* aEvent,
                                     nsEventStatus* aEventStatus,
                                     bool aControlHeld) {
  return NS_OK;
}

NS_IMETHODIMP
nsSplitterFrame::HandleDrag(nsPresContext* aPresContext, WidgetGUIEvent* aEvent,
                            nsEventStatus* aEventStatus) {
  return NS_OK;
}

NS_IMETHODIMP
nsSplitterFrame::HandleRelease(nsPresContext* aPresContext,
                               WidgetGUIEvent* aEvent,
                               nsEventStatus* aEventStatus) {
  return NS_OK;
}

void nsSplitterFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  SimpleXULLeafFrame::BuildDisplayList(aBuilder, aLists);

  // if the mouse is captured always return us as the frame.
  if (mInner->mDragging && aBuilder->IsForEventDelivery()) {
    // XXX It's probably better not to check visibility here, right?
    aLists.Outlines()->AppendNewToTop<nsDisplayEventReceiver>(aBuilder, this);
    return;
  }
}

nsresult nsSplitterFrame::HandleEvent(nsPresContext* aPresContext,
                                      WidgetGUIEvent* aEvent,
                                      nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);
  if (nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  AutoWeakFrame weakFrame(this);
  RefPtr<nsSplitterFrameInner> inner(mInner);
  switch (aEvent->mMessage) {
    case eMouseMove:
      inner->MouseDrag(aPresContext, aEvent);
      break;

    case eMouseUp:
      if (aEvent->AsMouseEvent()->mButton == MouseButton::ePrimary) {
        inner->MouseUp(aPresContext, aEvent);
      }
      break;

    default:
      break;
  }

  NS_ENSURE_STATE(weakFrame.IsAlive());
  return SimpleXULLeafFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

void nsSplitterFrameInner::MouseUp(nsPresContext* aPresContext,
                                   WidgetGUIEvent* aEvent) {
  if (mDragging && mOuter) {
    AdjustChildren(aPresContext);
    AddListener();
    PresShell::ReleaseCapturingContent();  // XXXndeakin is this needed?
    mDragging = false;
    State newState = GetState();
    // if the state is dragging then make it Open.
    if (newState == State::Dragging) {
      mOuter->mContent->AsElement()->SetAttr(kNameSpaceID_None,
                                             nsGkAtoms::state, u""_ns, true);
    }

    mPressed = false;

    // if we dragged then fire a command event.
    if (mDidDrag) {
      RefPtr<nsXULElement> element =
          nsXULElement::FromNode(mOuter->GetContent());
      element->DoCommand();
    }

    // printf("MouseUp\n");
  }

  mChildInfosBefore.Clear();
  mChildInfosAfter.Clear();
}

void nsSplitterFrameInner::MouseDrag(nsPresContext* aPresContext,
                                     WidgetGUIEvent* aEvent) {
  if (!mDragging || !mOuter) {
    return;
  }

  const bool isHorizontal = !mOuter->IsHorizontal();
  nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      aEvent, RelativeTo{mParentBox});
  nscoord pos = isHorizontal ? pt.x : pt.y;

  // take our current position and subtract the start location,
  // mDragStart is in parent-box relative coordinates already.
  pos -= mDragStart;

  for (auto& info : mChildInfosBefore) {
    info.changed = info.current;
  }

  for (auto& info : mChildInfosAfter) {
    info.changed = info.current;
  }
  nscoord oldPos = pos;

  ResizeChildTo(pos);

  State currentState = GetState();
  bool supportsBefore = SupportsCollapseDirection(Before);
  bool supportsAfter = SupportsCollapseDirection(After);

  const bool isRTL =
      mOuter->StyleVisibility()->mDirection == StyleDirection::Rtl;
  bool pastEnd = oldPos > 0 && oldPos > pos;
  bool pastBegin = oldPos < 0 && oldPos < pos;
  if (isRTL) {
    // Swap the boundary checks in RTL mode
    std::swap(pastEnd, pastBegin);
  }
  const bool isCollapsedBefore = pastBegin && supportsBefore;
  const bool isCollapsedAfter = pastEnd && supportsAfter;

  // if we are in a collapsed position
  if (isCollapsedBefore || isCollapsedAfter) {
    // and we are not collapsed then collapse
    if (currentState == State::Dragging) {
      if (pastEnd) {
        // printf("Collapse right\n");
        if (supportsAfter) {
          RefPtr<Element> outer = mOuter->mContent->AsElement();
          outer->SetAttr(kNameSpaceID_None, nsGkAtoms::substate, u"after"_ns,
                         true);
          outer->SetAttr(kNameSpaceID_None, nsGkAtoms::state, u"collapsed"_ns,
                         true);
        }

      } else if (pastBegin) {
        // printf("Collapse left\n");
        if (supportsBefore) {
          RefPtr<Element> outer = mOuter->mContent->AsElement();
          outer->SetAttr(kNameSpaceID_None, nsGkAtoms::substate, u"before"_ns,
                         true);
          outer->SetAttr(kNameSpaceID_None, nsGkAtoms::state, u"collapsed"_ns,
                         true);
        }
      }
    }
  } else {
    // if we are not in a collapsed position and we are not dragging make sure
    // we are dragging.
    if (currentState != State::Dragging) {
      mOuter->mContent->AsElement()->SetAttr(
          kNameSpaceID_None, nsGkAtoms::state, u"dragging"_ns, true);
    }
    AdjustChildren(aPresContext);
  }

  mDidDrag = true;
}

void nsSplitterFrameInner::AddListener() {
  mOuter->GetContent()->AddEventListener(u"mouseup"_ns, this, false, false);
  mOuter->GetContent()->AddEventListener(u"mousedown"_ns, this, false, false);
  mOuter->GetContent()->AddEventListener(u"mousemove"_ns, this, false, false);
  mOuter->GetContent()->AddEventListener(u"mouseout"_ns, this, false, false);
  mOuter->GetContent()->AddEventListener(u"keydown"_ns, this, false, false);
}

void nsSplitterFrameInner::RemoveListener() {
  NS_ENSURE_TRUE_VOID(mOuter);
  mOuter->GetContent()->RemoveEventListener(u"mouseup"_ns, this, false);
  mOuter->GetContent()->RemoveEventListener(u"mousedown"_ns, this, false);
  mOuter->GetContent()->RemoveEventListener(u"mousemove"_ns, this, false);
  mOuter->GetContent()->RemoveEventListener(u"mouseout"_ns, this, false);
  mOuter->GetContent()->RemoveEventListener(u"keydown"_ns, this, false);
}

nsresult nsSplitterFrameInner::HandleEvent(dom::Event* aEvent) {
  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("mouseup")) {
    return MouseUp(aEvent);
  }
  if (eventType.EqualsLiteral("mousedown")) {
    return MouseDown(aEvent);
  }
  if (eventType.EqualsLiteral("mousemove") ||
      eventType.EqualsLiteral("mouseout")) {
    return MouseMove(aEvent);
  }
  if (eventType.EqualsLiteral("keydown")) {
    return KeyDown(aEvent);
  }

  MOZ_ASSERT_UNREACHABLE("Unexpected eventType");
  return NS_OK;
}

nsresult nsSplitterFrameInner::MouseUp(Event* aMouseEvent) {
  NS_ENSURE_TRUE(mOuter, NS_OK);
  mPressed = false;

  PresShell::ReleaseCapturingContent();

  return NS_OK;
}

template <typename LengthLike>
static nscoord ToLengthWithFallback(const LengthLike& aLengthLike,
                                    nscoord aFallback) {
  if (aLengthLike.ConvertsToLength()) {
    return aLengthLike.ToLength();
  }
  return aFallback;
}

template <typename LengthLike>
static nsSize ToLengthWithFallback(const LengthLike& aWidth,
                                   const LengthLike& aHeight,
                                   nscoord aFallback = 0) {
  return {ToLengthWithFallback(aWidth, aFallback),
          ToLengthWithFallback(aHeight, aFallback)};
}

static void ApplyMargin(nsSize& aSize, const nsMargin& aMargin) {
  if (aSize.width != NS_UNCONSTRAINEDSIZE) {
    aSize.width += aMargin.LeftRight();
  }
  if (aSize.height != NS_UNCONSTRAINEDSIZE) {
    aSize.height += aMargin.TopBottom();
  }
}

bool nsSplitterFrameInner::CollectChildInfos() {
  if (!mParentBox) {
    return false;
  }

  EnsureOrient();
  const bool isHorizontal = !mOuter->IsHorizontal();

  const nsIContent* outerContent = mOuter->GetContent();

  const ResizeType resizeBefore = GetResizeBefore();
  const ResizeType resizeAfter = GetResizeAfter();
  const int32_t childCount = mParentBox->PrincipalChildList().GetLength();

  mChildInfosBefore.Clear();
  mChildInfosAfter.Clear();
  int32_t count = 0;

  bool foundOuter = false;
  CSSOrderAwareFrameIterator iter(
      mParentBox, FrameChildListID::Principal,
      CSSOrderAwareFrameIterator::ChildFilter::IncludeAll,
      CSSOrderAwareFrameIterator::OrderState::Unknown,
      CSSOrderAwareFrameIterator::OrderingProperty::Order);
  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* childBox = iter.get();
    if (childBox == mOuter) {
      foundOuter = true;
      if (!count) {
        // We're at the beginning, nothing to do.
        return false;
      }
      if (count == childCount - 1 && resizeAfter != ResizeType::Grow) {
        // If it's the last index then we need to allow for resizeafter="grow"
        return false;
      }
    }
    count++;

    nsIContent* content = childBox->GetContent();
    // XXX flex seems untested, as it uses mBoxFlex rather than actual flexbox
    // flex.
    const nscoord flex = childBox->StyleXUL()->mBoxFlex;
    const bool isBefore = !foundOuter;
    const bool isResizable = [&] {
      if (auto* element = nsXULElement::FromNode(content)) {
        if (element->NodeInfo()->NameAtom() == nsGkAtoms::splitter) {
          // skip over any splitters
          return false;
        }

        // We need to check for hidden attribute too, since treecols with
        // the hidden attribute are not really hidden, just collapsed
        if (element->GetBoolAttr(nsGkAtoms::fixed) ||
            element->GetBoolAttr(nsGkAtoms::hidden)) {
          return false;
        }
      }

      // We need to check this here rather than in the switch before because we
      // want `sibling` to work in the DOM order, not frame tree order.
      if (resizeBefore == ResizeType::Sibling &&
          content->GetNextElementSibling() == outerContent) {
        return true;
      }
      if (resizeAfter == ResizeType::Sibling &&
          content->GetPreviousElementSibling() == outerContent) {
        return true;
      }

      const ResizeType resizeType = isBefore ? resizeBefore : resizeAfter;
      switch (resizeType) {
        case ResizeType::Grow:
        case ResizeType::None:
        case ResizeType::Sibling:
          return false;
        case ResizeType::Flex:
          return flex > 0;
        case ResizeType::Closest:
        case ResizeType::Farthest:
          break;
      }
      return true;
    }();

    if (!isResizable) {
      continue;
    }

    nsSize curSize = childBox->GetSize();
    const auto& pos = *childBox->StylePosition();
    const auto anchorResolutionParams =
        AnchorPosResolutionParams::From(childBox);
    nsSize minSize =
        ToLengthWithFallback(*pos.GetMinWidth(anchorResolutionParams),
                             *pos.GetMinHeight(anchorResolutionParams));
    nsSize maxSize = ToLengthWithFallback(
        *pos.GetMaxWidth(anchorResolutionParams),
        *pos.GetMaxHeight(anchorResolutionParams), NS_UNCONSTRAINEDSIZE);
    nsSize prefSize(ToLengthWithFallback(*pos.GetWidth(anchorResolutionParams),
                                         curSize.width),
                    ToLengthWithFallback(*pos.GetHeight(anchorResolutionParams),
                                         curSize.height));

    maxSize.width = std::max(maxSize.width, minSize.width);
    maxSize.height = std::max(maxSize.height, minSize.height);
    prefSize.width = CSSMinMax(prefSize.width, minSize.width, maxSize.width);
    prefSize.height =
        CSSMinMax(prefSize.height, minSize.height, maxSize.height);

    nsMargin m;
    childBox->StyleMargin()->GetMargin(m);

    ApplyMargin(curSize, m);
    ApplyMargin(minSize, m);
    ApplyMargin(maxSize, m);
    ApplyMargin(prefSize, m);

    auto& list = isBefore ? mChildInfosBefore : mChildInfosAfter;
    nsSplitterInfo& info = *list.AppendElement();
    info.childElem = content;
    info.min = isHorizontal ? minSize.width : minSize.height;
    info.max = isHorizontal ? maxSize.width : maxSize.height;
    info.pref = isHorizontal ? prefSize.width : prefSize.height;
    info.current = info.changed = isHorizontal ? curSize.width : curSize.height;
  }

  if (!foundOuter) {
    return false;
  }

  const bool reverseDirection = [&] {
    MOZ_ASSERT(mParentBox->IsFlexContainerFrame());
    const FlexboxAxisInfo info(mParentBox);
    if (!info.mIsRowOriented) {
      return info.mIsMainAxisReversed;
    }
    const bool rtl =
        mParentBox->StyleVisibility()->mDirection == StyleDirection::Rtl;
    return info.mIsMainAxisReversed != rtl;
  }();

  if (reverseDirection) {
    // The before array is really the after array, and the order needs to be
    // reversed. First reverse both arrays.
    mChildInfosBefore.Reverse();
    mChildInfosAfter.Reverse();

    // Now swap the two arrays.
    std::swap(mChildInfosBefore, mChildInfosAfter);
  }

  // if resizebefore is not Farthest, reverse the list because the first child
  // in the list is the farthest, and we want the first child to be the closest.
  if (resizeBefore != ResizeType::Farthest) {
    mChildInfosBefore.Reverse();
  }

  // if the resizeafter is the Farthest we must reverse the list because the
  // first child in the list is the closest we want the first child to be the
  // Farthest.
  if (resizeAfter == ResizeType::Farthest) {
    mChildInfosAfter.Reverse();
  }

  return true;
}

nsresult nsSplitterFrameInner::MouseDown(Event* aMouseEvent) {
  NS_ENSURE_TRUE(mOuter, NS_OK);
  dom::MouseEvent* mouseEvent = aMouseEvent->AsMouseEvent();
  if (!mouseEvent) {
    return NS_OK;
  }

  // only if left button
  if (mouseEvent->Button() != 0) {
    return NS_OK;
  }

  if (SplitterElement()->GetBoolAttr(nsGkAtoms::disabled)) {
    return NS_OK;
  }

  mParentBox = GetValidParentBox(mOuter);
  if (!mParentBox) {
    return NS_OK;
  }

  // get our index
  mDidDrag = false;

  // Collect child information for resizing
  if (!CollectChildInfos()) {
    return NS_OK;
  }

  mPressed = true;

  const bool isHorizontal = !mOuter->IsHorizontal();
  int32_t c;
  nsPoint pt =
      nsLayoutUtils::GetDOMEventCoordinatesRelativeTo(mouseEvent, mParentBox);
  if (isHorizontal) {
    c = pt.x;
    mSplitterPos = mOuter->mRect.x;
  } else {
    c = pt.y;
    mSplitterPos = mOuter->mRect.y;
  }

  mDragStart = c;

  // printf("Pressed mDragStart=%d\n",mDragStart);

  PresShell::SetCapturingContent(mOuter->GetContent(),
                                 CaptureFlags::IgnoreAllowedState);

  return NS_OK;
}

nsresult nsSplitterFrameInner::MouseMove(Event* aMouseEvent) {
  NS_ENSURE_TRUE(mOuter, NS_OK);
  if (!mPressed) {
    return NS_OK;
  }

  if (mDragging) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMEventListener> kungfuDeathGrip(this);
  mOuter->mContent->AsElement()->SetAttr(kNameSpaceID_None, nsGkAtoms::state,
                                         u"dragging"_ns, true);

  RemoveListener();
  mDragging = true;

  return NS_OK;
}

nsresult nsSplitterFrameInner::KeyDown(Event* aKeyEvent) {
  NS_ENSURE_TRUE(mOuter, NS_OK);

  dom::KeyboardEvent* keyEvent = aKeyEvent->AsKeyboardEvent();
  if (!keyEvent) {
    return NS_OK;
  }

  // Check if splitter is disabled
  if (SplitterElement()->GetBoolAttr(nsGkAtoms::disabled)) {
    return NS_OK;
  }

  mParentBox = GetValidParentBox(mOuter);
  if (!mParentBox) {
    return NS_OK;
  }

  uint32_t keyCode = keyEvent->KeyCode();

  // Determine orientation to map arrow keys correctly
  EnsureOrient();
  const bool isHorizontal = !mOuter->IsHorizontal();

  // Use 5px movement per keypress
  const nscoord kKeyboardDelta = nsPresContext::CSSPixelsToAppUnits(5);
  nscoord delta = 0;

  switch (keyCode) {
    case dom::KeyboardEvent_Binding::DOM_VK_LEFT:
      if (isHorizontal) {
        delta = -kKeyboardDelta;
      }
      break;

    case dom::KeyboardEvent_Binding::DOM_VK_RIGHT:
      if (isHorizontal) {
        delta = kKeyboardDelta;
      }
      break;

    case dom::KeyboardEvent_Binding::DOM_VK_UP:
      if (!isHorizontal) {
        delta = -kKeyboardDelta;
      }
      break;

    case dom::KeyboardEvent_Binding::DOM_VK_DOWN:
      if (!isHorizontal) {
        delta = kKeyboardDelta;
      }
      break;

    default:
      // Other keys - don't consume
      return NS_OK;
  }

  if (delta == 0) {
    return NS_OK;
  }

  keyEvent->PreventDefault();

  // Collect child information for resizing
  if (!CollectChildInfos()) {
    return NS_OK;
  }

  for (auto& info : mChildInfosBefore) {
    // If pref > current, its width/height attribute says it should be larger
    // than it actually is (due to container constraints). Use current value so
    // AdjustChildren() calculations start from reality.
    if (info.pref > info.current) {
      info.pref = info.current;
    }

    info.changed = info.current;
  }

  for (auto& info : mChildInfosAfter) {
    // If pref > current, sync them (see comment above)
    if (info.pref > info.current) {
      info.pref = info.current;
    }

    info.changed = info.current;
  }

  // Apply the delta to resize children
  ResizeChildTo(delta);

  AdjustChildren(mOuter->PresContext());

  mChildInfosBefore.Clear();
  mChildInfosAfter.Clear();

  // Fire command event to notify of change
  RefPtr<nsXULElement> element = nsXULElement::FromNode(mOuter->GetContent());
  if (element) {
    element->DoCommand();
  }

  return NS_OK;
}

bool nsSplitterFrameInner::SupportsCollapseDirection(
    nsSplitterFrameInner::CollapseDirection aDirection) {
  static Element::AttrValuesArray strings[] = {
      nsGkAtoms::before, nsGkAtoms::after, nsGkAtoms::both, nullptr};

  switch (SplitterElement()->FindAttrValueIn(
      kNameSpaceID_None, nsGkAtoms::collapse, strings, eCaseMatters)) {
    case 0:
      return (aDirection == Before);
    case 1:
      return (aDirection == After);
    case 2:
      return true;
  }

  return false;
}

static nsIFrame* SlowOrderAwareSibling(nsIFrame* aBox, bool aNext) {
  nsIFrame* parent = aBox->GetParent();
  if (!parent) {
    return nullptr;
  }
  CSSOrderAwareFrameIterator iter(
      parent, FrameChildListID::Principal,
      CSSOrderAwareFrameIterator::ChildFilter::IncludeAll,
      CSSOrderAwareFrameIterator::OrderState::Unknown,
      CSSOrderAwareFrameIterator::OrderingProperty::Order);

  nsIFrame* prevSibling = nullptr;
  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* current = iter.get();
    if (!aNext && current == aBox) {
      return prevSibling;
    }
    if (aNext && prevSibling == aBox) {
      return current;
    }
    prevSibling = current;
  }
  return nullptr;
}

void nsSplitterFrameInner::UpdateState() {
  // State Transitions:
  //   Open            -> Dragging
  //   Open            -> CollapsedBefore
  //   Open            -> CollapsedAfter
  //   CollapsedBefore -> Open
  //   CollapsedBefore -> Dragging
  //   CollapsedAfter  -> Open
  //   CollapsedAfter  -> Dragging
  //   Dragging        -> Open
  //   Dragging        -> CollapsedBefore (auto collapse)
  //   Dragging        -> CollapsedAfter (auto collapse)

  State newState = GetState();

  if (newState == mState) {
    // No change.
    return;
  }

  if ((SupportsCollapseDirection(Before) || SupportsCollapseDirection(After)) &&
      IsValidParentBox(mOuter->GetParent())) {
    // Find the splitter's immediate sibling.
    const bool prev =
        newState == State::CollapsedBefore || mState == State::CollapsedBefore;
    nsIFrame* splitterSibling = SlowOrderAwareSibling(mOuter, !prev);
    if (splitterSibling) {
      nsCOMPtr<nsIContent> sibling = splitterSibling->GetContent();
      if (sibling && sibling->IsElement()) {
        if (mState == State::CollapsedBefore ||
            mState == State::CollapsedAfter) {
          // CollapsedBefore -> Open
          // CollapsedBefore -> Dragging
          // CollapsedAfter -> Open
          // CollapsedAfter -> Dragging
          nsContentUtils::AddScriptRunner(MakeAndAddRef<nsUnsetAttrRunnable>(
              sibling->AsElement(), nsGkAtoms::collapsed));
        } else if ((mState == State::Open || mState == State::Dragging) &&
                   (newState == State::CollapsedBefore ||
                    newState == State::CollapsedAfter)) {
          // Open -> CollapsedBefore / CollapsedAfter
          // Dragging -> CollapsedBefore / CollapsedAfter
          nsContentUtils::AddScriptRunner(MakeAndAddRef<nsSetAttrRunnable>(
              sibling->AsElement(), nsGkAtoms::collapsed, u"true"_ns));
        }
      }
    }
  }
  mState = newState;
}

void nsSplitterFrameInner::EnsureOrient() {
  mOuter->mIsHorizontal = SplitterIsHorizontal(mParentBox);
}

void nsSplitterFrameInner::AdjustChildren(nsPresContext* aPresContext) {
  EnsureOrient();
  const bool isHorizontal = !mOuter->IsHorizontal();

  AdjustChildren(aPresContext, mChildInfosBefore, isHorizontal);
  AdjustChildren(aPresContext, mChildInfosAfter, isHorizontal);
}

static nsIFrame* GetChildBoxForContent(nsIFrame* aParentBox,
                                       nsIContent* aContent) {
  // XXX Can this use GetPrimaryFrame?
  for (nsIFrame* f : aParentBox->PrincipalChildList()) {
    if (f->GetContent() == aContent) {
      return f;
    }
  }
  return nullptr;
}

void nsSplitterFrameInner::AdjustChildren(nsPresContext* aPresContext,
                                          nsTArray<nsSplitterInfo>& aChildInfos,
                                          bool aIsHorizontal) {
  /// printf("------- AdjustChildren------\n");

  for (auto& info : aChildInfos) {
    nscoord newPref = info.pref + (info.changed - info.current);
    if (nsIFrame* childBox =
            GetChildBoxForContent(mParentBox, info.childElem)) {
      SetPreferredSize(childBox, aIsHorizontal, newPref);
    }
  }
}

void nsSplitterFrameInner::SetPreferredSize(nsIFrame* aChildBox,
                                            bool aIsHorizontal, nscoord aSize) {
  nsMargin margin;
  aChildBox->StyleMargin()->GetMargin(margin);
  if (aIsHorizontal) {
    aSize -= (margin.left + margin.right);
  } else {
    aSize -= (margin.top + margin.bottom);
  }

  RefPtr element = nsStyledElement::FromNode(aChildBox->GetContent());
  if (!element) {
    return;
  }

  // We set both the attribute and the CSS value, so that XUL persist="" keeps
  // working, see bug 1790712.

  int32_t pixels = aSize / AppUnitsPerCSSPixel();
  nsAutoString attrValue;
  attrValue.AppendInt(pixels);
  element->SetAttr(aIsHorizontal ? nsGkAtoms::width : nsGkAtoms::height,
                   attrValue, IgnoreErrors());

  nsCOMPtr<nsDOMCSSDeclaration> decl = element->Style();

  nsAutoCString cssValue;
  cssValue.AppendInt(pixels);
  cssValue.AppendLiteral("px");
  decl->SetProperty(aIsHorizontal ? "width"_ns : "height"_ns, cssValue, ""_ns,
                    IgnoreErrors());
}

void nsSplitterFrameInner::AddRemoveSpace(nscoord aDiff,
                                          nsTArray<nsSplitterInfo>& aChildInfos,
                                          int32_t& aSpaceLeft) {
  aSpaceLeft = 0;

  for (auto& info : aChildInfos) {
    nscoord min = info.min;
    nscoord max = info.max;
    nscoord& c = info.changed;

    // figure our how much space to add or remove
    if (c + aDiff < min) {
      aDiff += (c - min);
      c = min;
    } else if (c + aDiff > max) {
      aDiff -= (max - c);
      c = max;
    } else {
      c += aDiff;
      aDiff = 0;
    }

    // there is not space left? We are done
    if (aDiff == 0) {
      break;
    }
  }

  aSpaceLeft = aDiff;
}

/**
 * Ok if we want to resize a child we will know the actual size in pixels we
 * want it to be. This is not the preferred size. But the only way we can change
 * a child is by manipulating its preferred size. So give the actual pixel size
 * this method will figure out the preferred size and set it.
 */

void nsSplitterFrameInner::ResizeChildTo(nscoord& aDiff) {
  nscoord spaceLeft = 0;

  if (!mChildInfosBefore.IsEmpty()) {
    AddRemoveSpace(aDiff, mChildInfosBefore, spaceLeft);
    // If there is any space left over remove it from the diff we were
    // originally given.
    aDiff -= spaceLeft;
  }

  AddRemoveSpace(-aDiff, mChildInfosAfter, spaceLeft);

  if (spaceLeft != 0 && !mChildInfosAfter.IsEmpty()) {
    aDiff += spaceLeft;
    AddRemoveSpace(spaceLeft, mChildInfosBefore, spaceLeft);
  }
}
