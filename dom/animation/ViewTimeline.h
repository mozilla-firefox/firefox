/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ViewTimeline_h
#define mozilla_dom_ViewTimeline_h

#include "mozilla/dom/ScrollTimeline.h"

class nsIFrame;

namespace mozilla {
class ScrollContainerFrame;
}  // namespace mozilla

namespace mozilla::dom {
class CSSNumericValue;
struct ViewTimelineOptions;

/*
 * A view progress timeline is a segment of a scroll progress timeline that are
 * scoped to the scroll positions in which any part of the associated element’s
 * principal box intersects its nearest ancestor scrollport. So ViewTimeline
 * is a special case of ScrollTimeline.
 */
class ViewTimeline final : public ScrollTimeline {
  template <typename T, typename... Args>
  friend already_AddRefed<T> mozilla::MakeAndAddRef(Args&&... aArgs);

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ViewTimeline, ScrollTimeline)

  ViewTimeline() = delete;

  // Note: |aSubject| is used as the subject which specifies view-timeline-name
  // property, and we use this subject to look up its nearest scroll container.
  static already_AddRefed<ViewTimeline> MakeNamed(
      Document* aDocument, Element* aSubject,
      const PseudoStyleRequest& aPseudoRequest, StyleScrollAxis aAxis,
      const StyleViewTimelineInset& aInset);

  static already_AddRefed<ViewTimeline> MakeAnonymous(
      Document* aDocument, const NonOwningAnimationTarget& aTarget,
      StyleScrollAxis aAxis, const StyleViewTimelineInset& aInset);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // ViewTimeline methods.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static already_AddRefed<ViewTimeline> Constructor(
      const GlobalObject& aGlobal, const ViewTimelineOptions& aOptions,
      ErrorResult& aRv);
  Element* Subject() const { return mSubject; }
  already_AddRefed<CSSNumericValue> GetStartOffset(ErrorResult& aRv) const;
  already_AddRefed<CSSNumericValue> GetEndOffset(ErrorResult& aRv) const;

  bool IsViewTimeline() const override { return true; }
  const ViewTimeline* AsViewTimeline() const override { return this; }

  void ReplacePropertiesWith(Element* aSubjectElement,
                             const PseudoStyleRequest& aPseudoRequest,
                             const dom::ScopedTimelineName& aName,
                             StyleScrollAxis aAxis,
                             const StyleViewTimelineInset& aInset);

  bool UpdateCachedCurrentTime() override;

  std::pair<double, double> IntervalForAttachmentRange(
      const AnimationRange& aStyleRange) const override;

  Maybe<double> MapKeyframeOffsetToOffset(const StyleTimelineRangeName aName,
                                          const double aPercentage) const;

  NonOwningAnimationTarget TimelineTarget() const override {
    return NonOwningAnimationTarget{mSubject, mSubjectPseudoRequest};
  }

  bool IsReusableAnonymousTimeline(
      const StyleGenericViewFunction<StyleLengthPercentage>& aView) const;

 private:
  ~ViewTimeline() = default;
  ViewTimeline(Document* aDocument, const ScrollerInfo& aScrollerInfo,
               StyleScrollAxis aAxis, Element* aSubject,
               const PseudoStyleRequest& aSubjectPseudoRequest,
               const StyleViewTimelineInset& aInset, bool aIsAnonymous)
      : ScrollTimeline(aDocument, aScrollerInfo, aAxis),
        mSubject(aSubject),
        mSubjectPseudoRequest(aSubjectPseudoRequest),
        mIsAnonymous(aIsAnonymous),
        mInset(aInset) {}

  Maybe<ComputedTimelineData> ComputeTimelineData() const override;

  // The displacement that sticky positioning applies to the subject along this
  // timeline's axis, as a function of the scroll offset s:
  //
  //   d(s) = clamp(s - mStartSideStuckAt, 0, mStartSideMax) +
  //          clamp(s - mEndSideUnstuckAt, -mEndSideMax, 0)
  //
  // A zero max means the subject never sticks that way, so a
  // default-constructed StickyDisplacement is the non-sticky case.
  struct StickyDisplacement {
    // The scroll offset at which start-side sticking begins. Over the range
    // [mStartSideStuckAt, mStartSideStuckAt + mStartSideMax], the displacement
    // increases from 0 to |mStartSideMax|.
    nscoord mStartSideStuckAt = 0;
    // The maximum positive displacement caused by start-side sticking. Never
    // negative; 0 means there is no start-side sticking.
    nscoord mStartSideMax = 0;
    // The scroll offset at which end-side sticking ends. Over the range
    // [mEndSideUnstuckAt - mEndSideMax, mEndSideUnstuckAt], the displacement
    // increases from -mEndSideMax to 0.
    nscoord mEndSideUnstuckAt = 0;
    // The maximum magnitude of the negative displacement caused by end-side
    // sticking. Never negative; 0 means there is no end-side sticking.
    nscoord mEndSideMax = 0;

    // While stuck, the subject moves with the scrollport, so the alignment
    // that would occur at a single scroll offset if the subject never stuck
    // can instead persist over a range of actual scroll offsets. These
    // functions do not compute d(s) itself; they invert the mapping from an
    // actual scroll offset |s| to its corresponding offset without sticky
    // positioning:
    //
    //   aOffsetIgnoringSticky = s - d(s)
    //
    // returning the smallest (Earliest) resp. largest (Latest) solution for
    // |s|, which lets timeline range calculations pick out the endpoints of
    // the interval over which the alignment is maintained.
    //
    // |aOffsetIgnoringSticky| is the scroll offset at which the subject would
    // reach the alignment defining a timeline range boundary if sticky
    // positioning were not applied.
    nscoord Earliest(nscoord aOffsetIgnoringSticky) const;
    nscoord Latest(nscoord aOffsetIgnoringSticky) const;

    // The same displacement model expressed for negated scroll offsets, as
    // used for RTL / bottom-to-top axes where GetScrollPosition() is zero or
    // negative. The roles of the two sides swap.
    StickyDisplacement Reversed() const {
      return {-mEndSideUnstuckAt, mEndSideMax, -mStartSideStuckAt,
              mStartSideMax};
    }

    bool operator==(const StickyDisplacement&) const = default;
  };

  // The scroll offsets at which the subject's edges would coincide with the
  // edges of its view progress visibility range if the subject never stuck.
  struct AlignmentOffsetsIgnoringSticky {
    // The scroll offset at which the start border edge of the subject would
    // align with the end edge of the view progress visibility range.
    nscoord mSubjectStartAtViewEnd = 0;
    // The scroll offset at which the end border edge of the subject would
    // align with the start edge of the view progress visibility range.
    nscoord mSubjectEndAtViewStart = 0;
    // The scroll offset at which the start border edge of the subject would
    // align with the start edge of the view progress visibility range.
    nscoord mSubjectStartAtViewStart = 0;
    // The scroll offset at which the end border edge of the subject would
    // align with the end edge of the view progress visibility range.
    nscoord mSubjectEndAtViewEnd = 0;
  };
  AlignmentOffsetsIgnoringSticky ComputeAlignmentOffsetsIgnoringSticky() const;

  // Returns the sticky displacement currently baked into |aSubject|'s offset
  // to the scrolled frame, along with the model of how it varies with the
  // scroll offset. Nothing() if we can't model the subject's stickiness (e.g.
  // more than one sticky ancestor contributes to its offset), in which case
  // the caller should not remove the baked-in displacement from the subject's
  // position: treating the current displacement as permanent is our best
  // approximation.
  static Maybe<std::pair<nscoord, StickyDisplacement>>
  ComputeStickyDisplacement(const nsIFrame* aSubject,
                            const ScrollContainerFrame* aScrollContainerFrame,
                            layers::ScrollDirection aAxis);

  std::pair<nscoord, nscoord> IntervalForTimelineRangeName(
      const StyleTimelineRangeName aName) const;

  template <typename F>
  double ComputeOffsetToTimelineRange(
      const StyleTimelineRangeName& aName,
      const ScrollTimeline::ComputedTimelineData& aData,
      F&& aFuncToResolveValue) const;

  // The subject element.
  // 1. For view(), the subject element is the animation target.
  // 2. For view-timeline property, the subject element is the element who
  //    defines this property.
  RefPtr<Element> mSubject;
  PseudoStyleRequest mSubjectPseudoRequest;
  bool mIsAnonymous;

  // FIXME: Bug 1817073. view-timeline-inset is an animatable property. However,
  // the inset from view() is not animatable, so for named view timeline, this
  // value depends on the animation style. Therefore, we have to check its style
  // value when using it. For now, in order to simplify the implementation, we
  // make |mInset| be fixed.
  StyleViewTimelineInset mInset;

  struct CurrentTimeData {
    // The basic scroll info.
    ScrollTimeline::CurrentTimeData mScrollData;
    // The size of the scrollport.
    nscoord mScrollPortSize = 0;
    // The position and size of the subject.
    nscoord mSubjectPosition = 0;
    nscoord mSubjectSize = 0;
    // The used view-timeline-inset.
    nscoord mInsetStart = 0;
    nscoord mInsetEnd = 0;
    // How sticky positioning displaces the subject. |mSubjectPosition| has this
    // displacement removed, so both are scroll-invariant - except when we
    // can't model the stickiness (see ComputeStickyDisplacement()), where the
    // current displacement is treated as permanent.
    StickyDisplacement mSticky;

    // Returns true if any of the metrics are changed, except for |mPosition|.
    bool IsChanged(const CurrentTimeData& aOther) const {
      return mScrollData.mMaxScrollOffset !=
                 aOther.mScrollData.mMaxScrollOffset ||
             mScrollPortSize != aOther.mScrollPortSize ||
             mSubjectPosition != aOther.mSubjectPosition ||
             mSubjectSize != aOther.mSubjectSize ||
             mInsetStart != aOther.mInsetStart ||
             mInsetEnd != aOther.mInsetEnd || mSticky != aOther.mSticky;
    }
    bool operator==(const CurrentTimeData& aOther) const {
      return mScrollData.mPosition == aOther.mScrollData.mPosition &&
             !IsChanged(aOther);
    }
  };
  Maybe<CurrentTimeData> mCachedCurrentTime;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ViewTimeline_h
