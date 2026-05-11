/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ViewTimeline.h"

#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/dom/Animation.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/ViewTimelineBinding.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(ViewTimeline, ScrollTimeline, mSubject)
NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(ViewTimeline, ScrollTimeline)

/* static */
already_AddRefed<ViewTimeline> ViewTimeline::MakeNamed(
    Document* aDocument, Element* aSubject,
    const PseudoStyleRequest& aPseudoRequest,
    const StyleViewTimeline& aStyleTimeline) {
  MOZ_ASSERT(NS_IsMainThread());

  // 1. Create an anonymous scroller, as if `scroll(nearest)`.
  auto scroller = ScrollerInfo::Anonymous(
      StyleScroller::Nearest,
      NonOwningAnimationTarget{aSubject, aPseudoRequest});

  // 2. Create timeline.
  return MakeAndAddRef<ViewTimeline>(
      aDocument, scroller, aStyleTimeline.GetAxis(), aSubject,
      aPseudoRequest.mType, aStyleTimeline.GetInset());
}

/* static */
already_AddRefed<ViewTimeline> ViewTimeline::MakeAnonymous(
    Document* aDocument, const NonOwningAnimationTarget& aTarget,
    StyleScrollAxis aAxis, const StyleViewTimelineInset& aInset) {
  // view() finds the nearest scroll container from the animation target.
  auto scroller = ScrollerInfo::Anonymous(StyleScroller::Nearest, aTarget);
  return MakeAndAddRef<ViewTimeline>(aDocument, scroller, aAxis,
                                     aTarget.mElement,
                                     aTarget.mPseudoRequest.mType, aInset);
}

JSObject* ViewTimeline::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  if (!StaticPrefs::
          layout_css_scroll_driven_animations_viewtimeline_enabled()) {
    return ScrollTimeline::WrapObject(aCx, aGivenProto);
  }
  return ViewTimeline_Binding::Wrap(aCx, this, aGivenProto);
}

Nullable<double> ViewTimeline::GetStartOffset() const {
  auto data = ComputeTimelineData();
  if (!data) {
    return nullptr;
  }
  return nsPresContext::AppUnitsToFloatCSSPixels(data->mStart);
}

Nullable<double> ViewTimeline::GetEndOffset() const {
  auto data = ComputeTimelineData();
  if (!data) {
    return nullptr;
  }
  return nsPresContext::AppUnitsToFloatCSSPixels(data->mEnd);
}

void ViewTimeline::ReplacePropertiesWith(
    Element* aSubjectElement, const PseudoStyleRequest& aPseudoRequest,
    const StyleViewTimeline& aNew) {
  mSubject = aSubjectElement;
  mSubjectPseudoType = aPseudoRequest.mType;
  mAxis = aNew.GetAxis();
  // FIXME: Bug 1817073. We assume it is a non-animatable value for now.
  mInset = aNew.GetInset();

  for (auto* anim = mAnimationOrder.getFirst(); anim;
       anim = static_cast<LinkedListElement<Animation>*>(anim)->getNext()) {
    MOZ_ASSERT(anim->GetTimeline() == this);
    MOZ_ASSERT(anim->GetTimelineName() == aNew.GetName());
    // Set this so we just PostUpdate() for this animation.
    anim->SetTimeline(this, aNew.GetName());
  }
}

static std::pair<nscoord, nscoord> ComputeInsets(
    const ScrollContainerFrame* aScrollContainerFrame,
    const layers::ScrollDirection aOrientation, const StyleScrollAxis aAxis,
    const StyleViewTimelineInset& aInset) {
  // If view-timeline-inset is auto, it indicates to use the value of
  // scroll-padding. We use logical dimension to map that start/end offset to
  // the corresponding scroll-padding-{inline|block}-{start|end} values.
  const WritingMode wm =
      aScrollContainerFrame->GetScrolledFrame()->GetWritingMode();
  const auto& scrollPadding =
      LogicalMargin(wm, aScrollContainerFrame->GetScrollPadding());
  const bool isBlockAxis = aAxis == StyleScrollAxis::Block ||
                           (aAxis == StyleScrollAxis::X && wm.IsVertical()) ||
                           (aAxis == StyleScrollAxis::Y && !wm.IsVertical());

  // The percentages of view-timelne-inset is relative to the corresponding
  // dimension of the relevant scrollport.
  // https://drafts.csswg.org/scroll-animations-1/#view-timeline-inset
  const nsRect scrollPort = aScrollContainerFrame->GetScrollPortRect();
  const nscoord percentageBasis =
      aOrientation == layers::ScrollDirection::eHorizontal ? scrollPort.width
                                                           : scrollPort.height;

  nscoord startInset =
      aInset.start.IsAuto()
          ? (isBlockAxis ? scrollPadding.BStart(wm) : scrollPadding.IStart(wm))
          : aInset.start.AsLengthPercentage().Resolve(percentageBasis);
  nscoord endInset =
      aInset.end.IsAuto()
          ? (isBlockAxis ? scrollPadding.BEnd(wm) : scrollPadding.IEnd(wm))
          : aInset.end.AsLengthPercentage().Resolve(percentageBasis);
  return {startInset, endInset};
}

void ViewTimeline::UpdateCachedCurrentTime() {
  const auto prevCachedCurrentTime = std::move(mCachedCurrentTime);

  mCachedCurrentTime.reset();

  const auto state = GetState();
  // If no layout box, this timeline is inactive.
  if (const auto* e = state.mSource.mElement; !e || !e->GetPrimaryFrame()) {
    return;
  }

  // if this is not a scroller container, this timeline is inactive.
  const ScrollContainerFrame* scrollContainerFrame =
      state.GetScrollContainerFrame();
  if (!scrollContainerFrame) {
    return;
  }

  // If there is no scrollable overflow, then the ScrollTimeline is inactive.
  // https://drafts.csswg.org/scroll-animations-1/#scrolltimeline-interface
  const auto orientation = state.Axis();
  if (!scrollContainerFrame->GetAvailableScrollingDirections().contains(
          orientation)) {
    return;
  }

  // Note: We may fail to get the pseudo element (or its primary frame) if it is
  // not generated yet or just get destroyed, while we are sampling this view
  // timeline.
  // FIXME: Bug 1954230. It's probably a case we need to discard this timeline.
  // For now, this is just a hot fix.
  MOZ_ASSERT(mSubject, "We should have a subject to create this view timeline");
  const Element* subjectElement =
      mSubject->GetPseudoElement(PseudoStyleRequest(mSubjectPseudoType));
  const nsIFrame* subject =
      subjectElement ? subjectElement->GetPrimaryFrame() : nullptr;
  if (!subject) {
    // No principal box of the subject, so we cannot compute the offset. This
    // may happen when we clear all animation collections during unbinding from
    // the tree.
    return;
  }

  // The current scroll position and scroll range.
  const nsPoint& scrollPosition = scrollContainerFrame->GetScrollPosition();
  const nsRect& scrollRange = scrollContainerFrame->GetScrollRange();

  // In order to get the distance between the subject and the scrollport
  // properly, we use the position based on the domain of the scrolled frame,
  // instead of the scroll container frame.
  const nsIFrame* scrolledFrame = scrollContainerFrame->GetScrolledFrame();
  MOZ_ASSERT(scrolledFrame);
  const nsRect subjectRect(subject->GetOffsetTo(scrolledFrame),
                           subject->GetSize());

  // Use scrollport size (i.e. padding box size - scrollbar size), which is used
  // for calculating the view progress visibility range.
  // https://drafts.csswg.org/scroll-animations/#view-progress-visibility-range
  const nsRect scrollPort = scrollContainerFrame->GetScrollPortRect();

  // |sideInsets.mEnd| is used to adjust the start offset, and
  // |sideInsets.mStart| is used to adjust the end offset. This is because
  // |sideInsets.mStart| refers to logical start side [1] of the source box
  // (i.e. the box of the scrollport), where as |startOffset| refers to the
  // start of the timeline, and similarly for end side/offset. [1]
  // https://drafts.csswg.org/css-writing-modes-4/#css-start
  const auto sideInsets =
      ComputeInsets(scrollContainerFrame, orientation, mAxis, mInset);

  // Adjuct the positions and sizes based on the physical axis.
  switch (orientation) {
    case layers::ScrollDirection::eVertical:
      mCachedCurrentTime.emplace(CurrentTimeData{
          ScrollTimeline::CurrentTimeData{scrollPosition.y, scrollRange.height},
          scrollPort.height, subjectRect.y, subjectRect.height,
          sideInsets.first, sideInsets.second});
      break;
    case layers::ScrollDirection::eHorizontal:
      mCachedCurrentTime.emplace(CurrentTimeData{
          ScrollTimeline::CurrentTimeData{scrollPosition.x, scrollRange.width},
          scrollPort.width,
          // |mSubjectPosition| should be the position of the start border edge
          // of the subject, so for R-L case, we have to use XMost() as the
          // start border edge of the subject, and compute its position by using
          // the x-most side of the scrolled frame as the origin on the
          // horizontal axis.
          scrolledFrame->GetWritingMode().IsPhysicalRTL()
              ? scrolledFrame->GetSize().width - subjectRect.XMost()
              : subjectRect.x,
          subjectRect.width, sideInsets.first, sideInsets.second});
      break;
  }

  if (!prevCachedCurrentTime ||
      prevCachedCurrentTime->IsChanged(*mCachedCurrentTime)) {
    TimelineDataDidChange();
  }
}

// FIXME: Bug 2018678. Need to be adjusted for sticky positioning element.
// https://drafts.csswg.org/scroll-animations-1/#view-timelines-ranges
std::pair<nscoord, nscoord> ViewTimeline::IntervalForTimelineRangeName(
    const StyleTimelineRangeName aName,
    const ScrollTimeline::ComputedTimelineData& aData) const {
  MOZ_ASSERT(mCachedCurrentTime, "We should have a cached current time");

  // The following variable names are based on the vertical scrolling direction
  // and the subject becomes visible from the bottom of the scroll port.

  // The scroll offset when we align the start border edge of the subject with
  // the end edge of the scroll port.
  const nscoord alignedSubjectStartViewEnd = aData.mStart;
  // The scroll offset when we align the end border edge of the subject with
  // the start edge of the scroll port.
  const nscoord alignedSubjectEndViewStart = aData.mEnd;
  // The scroll offset when we align the start border edge of the subject with
  // the start edge of the scroll port.
  const nscoord alignedSubjectStartViewStart =
      alignedSubjectEndViewStart - mCachedCurrentTime->mSubjectSize;
  // The scroll offset when we align the end border edge of the subject with the
  // end edge of the scroll port.
  const nscoord alignedSubjectEndViewEnd =
      alignedSubjectStartViewEnd + mCachedCurrentTime->mSubjectSize;

  // Precompute the range of `contain` to avoid the code duplication. See below
  // for more details.
  const nscoord containStart =
      std::min(alignedSubjectStartViewStart, alignedSubjectEndViewEnd);
  const nscoord containEnd =
      std::max(alignedSubjectStartViewStart, alignedSubjectEndViewEnd);

  // FIXME: Bug 2030453. Check the case for RTL for horizontal axis. Perhaps we
  // have to swap these two values.
  switch (aName) {
    case StyleTimelineRangeName::None:
    case StyleTimelineRangeName::Normal:
      // The default behavior is equalivant to `cover` for view timeline.
    case StyleTimelineRangeName::Cover:
      // Represents the full range of the view progress timeline:
      // * 0% progress represents the latest position at which the start border
      //   edge of the element’s principal box coincides with the end edge of
      //   its view progress visibility range.
      // * 100% progress represents the earliest position at which the end
      //   border edge of the element’s principal box coincides with the start
      //   edge of its view progress visibility range.
      return {alignedSubjectStartViewEnd, alignedSubjectEndViewStart};

    case StyleTimelineRangeName::Contain:
      // Represents the range during which the principal box is either fully
      // contained by, or fully covers, its view progress visibility range
      // within the scrollport.
      // 0% progress represents the earliest position at which either:
      //   1. the start border edge of the element’s principal box coincides
      //      with the start edge of its view progress visibility range.
      //   2. the end border edge of the element’s principal box coincides with
      //      the end edge of its view progress visibility range.
      // 100% progress represents the latest position at which either:
      //   1. the start border edge of the element’s principal box coincides
      //      with the start edge of its view progress visibility range.
      //   2. the end border edge of the element’s principal box coincides with
      //      the end edge of its view progress visibility range.
      //
      // Note that we swap the values if the subject size is larger than the
      // scrollport size. That's why there are 2 options for 0% and 2 options
      // for 100% in the spec.
      //
      // For more visual explanation, see:
      // https://github.com/w3c/csswg-drafts/issues/7973#issuecomment-1427150014
      return {containStart, containEnd};

    case StyleTimelineRangeName::Entry:
      // Represents the range during which the principal box is entering the
      // view progress visibility range.
      // * 0% is equivalent to 0% of the cover range.
      // * 100% is equivalent to 0% of the contain range.
      return {alignedSubjectStartViewEnd, containStart};

    case StyleTimelineRangeName::Exit:
      // Represents the range during which the principal box is exiting the view
      // progress visibility range.
      // * 0% is equivalent to 100% of the contain range.
      // * 100% is equivalent to 100% of the cover range.
      return {containEnd, alignedSubjectEndViewStart};

    case StyleTimelineRangeName::EntryCrossing:
      // Represents the range during which the principal box crosses the end
      // border edge.
      // * 0% is equivalent to 0% of the cover range.
      //
      // Note that the duration of the entry-crossing range is equal to the
      // subject size, so this is equivalent to
      // `{alignedSubjectStartViewEnd,
      //   alignedSubjectStartViewEnd + mCachedCurrentTime->mSubjectSize}`.
      return {alignedSubjectStartViewEnd, alignedSubjectEndViewEnd};

    case StyleTimelineRangeName::ExitCrossing:
      // Represents the range during which the principal box crosses the start
      // border edge.
      // * 100% is equivalent to 100% of the cover range.
      //
      // Note that the duration of the exit-crossing range is equal to the
      // subject size, so this is equivalent to
      // `{alignedSubjectEndViewStart - mCachedCurrentTime->mSubjectSize,
      //   alignedSubjectEndViewStart}`.
      return {alignedSubjectStartViewStart, alignedSubjectEndViewStart};

    case StyleTimelineRangeName::Scroll:
      // Represents the full range of the scroll container on which the view
      // progress timeline is defined.
      //
      // So this is equivalent to scroll timeline's full range.
      return {0, mCachedCurrentTime->mScrollData.mMaxScrollOffset};
  }

  MOZ_ASSERT_UNREACHABLE("All cases should be hanlded.");
  // Use cover as the default value. However, we shouldn't be here.
  return {alignedSubjectStartViewEnd, alignedSubjectEndViewStart};
}

// TODO: Bug 2020822. We have to align the start time of animation with this
// attachment range. Otherwise, the animation-range-start doesn't work.
std::pair<double, double> ViewTimeline::IntervalForAttachmentRange(
    const AnimationRange& aStyleRange) const {
  const auto& data = ComputeTimelineData();
  if (!data) {
    // Return the default, [0%, 100%].
    return {0, 1.0};
  }

  // Returns the percentage (in double) for this StyleAnimationValue based on
  // the full timeline range (i.e. `cover` for view-timeline).
  auto computeNamedRangeEdgeAsPercentage =
      [&](const StyleGenericAnimationRangeValue<StyleLengthPercentage>&
              aValue) {
        const auto [nameStart, nameEnd] =
            IntervalForTimelineRangeName(aValue.name, *data);
        const auto timelineRange = data->mEnd - data->mStart;
        const auto nameRange = nameEnd - nameStart;
        const auto positionInNameRange =
            nameStart + aValue.lp.Resolve(nameRange);
        const auto positionInTimeline = positionInNameRange - data->mStart;
        return static_cast<double>(positionInTimeline) /
               static_cast<double>(timelineRange);
      };
  return {computeNamedRangeEdgeAsPercentage(aStyleRange.mStart),
          computeNamedRangeEdgeAsPercentage(aStyleRange.mEnd)};
}

Maybe<ScrollTimeline::ComputedTimelineData> ViewTimeline::ComputeTimelineData()
    const {
  if (!mCachedCurrentTime) {
    return Nothing();
  }

  const CurrentTimeData& data = mCachedCurrentTime.ref();

  // We use "cover" timeline range as the default full range for view
  // timeline.
  // https://drafts.csswg.org/scroll-animations-1/#view-timeline-progress

  // Note: `mSubjectPosition - mScrollPortSize` means the distance between the
  // start border edge of the subject and the end edge of the scrollport.
  const nscoord startOffset =
      data.mSubjectPosition - data.mScrollPortSize + data.mInsetEnd;
  // Note: `mSubjectPosition + mSubjectSize` means the position of the end
  // border edge of the subject. When it touches the start edge of the
  // scrollport, it is 100%.
  const nscoord endOffset =
      data.mSubjectPosition + data.mSubjectSize - data.mInsetStart;

  return Some(ComputedTimelineData{
      data.mScrollData.mPosition,
      startOffset,
      endOffset,
  });
}

}  // namespace mozilla::dom
