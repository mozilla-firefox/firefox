/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_TimelineManager_h
#define mozilla_TimelineManager_h

#include "mozilla/Assertions.h"
#include "mozilla/LinkedList.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimelineCollection.h"
#include "mozilla/TimelineScope.h"
#include "mozilla/dom/Element.h"
#include "nsStyleAutoArray.h"
#include "nsStyleStruct.h"

class nsPresContext;

namespace mozilla {
class ComputedStyle;
struct PseudoStyleRequest;

namespace dom {
class AnimationTimeline;
class ScrollTimeline;
class ViewTimeline;
}  // namespace dom

class TimelineManager {
 public:
  explicit TimelineManager(nsPresContext* aPresContext);

  ~TimelineManager() {
    MOZ_ASSERT(!mPresContext, "Disconnect should have been called");
  }

  void Disconnect() {
    mScrollTimelineNameMap.Clear();
    while (auto* head = mScrollTimelineCollections.getFirst()) {
      head->Destroy();
    }
    mViewTimelineNameMap.Clear();
    while (auto* head = mViewTimelineCollections.getFirst()) {
      head->Destroy();
    }
    while (auto* head = mTimelineScopes.getFirst()) {
      head->Destroy();
    }

    mPresContext = nullptr;
  }

  enum class ProgressTimelineType : uint8_t {
    Scroll,
    View,
  };
  nsTArray<RefPtr<const nsAtom>> UpdateTimelines(
      dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
      const ComputedStyle* aComputedStyle, ProgressTimelineType aType);

  void UpdateTimelineScopes(dom::Element* aElement,
                            const ComputedStyle* aComputedStyle);
  static bool TimelineNameScopedByElement(const dom::Element* aElement,
                                          const nsAtom* aName);
  static RefPtr<dom::AnimationTimeline> GetNamedTimelineForThisElement(
      const dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
      const nsAtom* aName, const dom::ShadowRoot* aTargetShadowRoot);
  already_AddRefed<dom::AnimationTimeline> GetNamedTimelineInSubtree(
      const dom::Element* aRoot, const nsAtom* aName,
      const dom::ShadowRoot* aTargetShadowRoot, dom::Document* aDocument) const;

 private:
  // Array of timelines. Should be maintained in frame tree order.
  template <typename TimelineType>
  using Timelines = nsTArray<TimelineEntry<TimelineType>>;
  // Mapping from timeline names to timelines of that name. Depending on
  // the use of `timeline-scope`, may or may not be visible from the
  // element specifying `animation-timeline`.
  template <typename TimelineType>
  using TimelineNameMap =
      nsTHashMap<RefPtr<const nsAtom>, Timelines<TimelineType>>;
  template <typename TimelineType>
  using TimelineTargetsIter =
      TimelineManager::Timelines<TimelineType>::const_iterator;

  template <typename TimelineType>
  TimelineType* DoGetNamedTimelineInSubtree(
      const dom::Element* aRoot, const nsAtom* aName,
      const TimelineNameMap<TimelineType>& aTimelineNameMap,
      const dom::ShadowRoot* aTargetShadowRoot) const;

  template <typename TimelineType>
  nsTArray<RefPtr<const nsAtom>> DoUpdateTimelines(
      nsPresContext* aPresContext, dom::Element* aElement,
      const PseudoStyleRequest& aPseudoRequest, const nsStyleUIReset* aUIReset,
      TimelineNameMap<TimelineType>& aTimelineNameMap);

  template <typename T>
  void AddTimelineCollection(TimelineCollection<T>* aCollection);

  // Find a timeline target in the given timeline collection matching
  // the (pseudo) element.
  template <typename TimelineType>
  static TimelineTargetsIter<TimelineType> FindInTimelineTargets(
      Timelines<TimelineType>& aTimelineTargets, const dom::Element* aElement,
      const PseudoStyleRequest& aPseudoRequest);

  // Remove given (pseudo) element's named timeline from the name map.
  template <typename TimelineType>
  static void RemoveTimelineTargetByName(
      const nsAtom* aName, const dom::Element* aElement,
      const PseudoStyleRequest& aPseudoRequest,
      TimelineNameMap<TimelineType>& aTimelineNameMap);

  // Destroy all timelines relating to the given (pseudo) element.
  template <typename TimelineType>
  nsTArray<RefPtr<const nsAtom>> TryDestroyTimeline(
      dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
      TimelineNameMap<TimelineType>& aTimelineNameMap);

#ifdef DEBUG
  // Assert that we do not have a timeline target for the given (pseudo) element
  // in the given range.
  template <typename TimelineType>
  static void EnsureNoTimelineTarget(
      const TimelineTargetsIter<TimelineType>& aStart,
      const TimelineTargetsIter<TimelineType>& aEnd,
      const dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest);
#endif

  // The linked lists hold every live collection and scope, each owned by its
  // element's ElementAnimationData, so that Disconnect() can clean them up when
  // the pres shell is detached without the elements unbinding from the tree,
  // e.g. for print preview. Lookups go through the name maps.
  LinkedList<TimelineCollection<dom::ScrollTimeline>>
      mScrollTimelineCollections;
  // Map containing all named scroll timelines, keyed by their names.
  TimelineNameMap<dom::ScrollTimeline> mScrollTimelineNameMap;
  LinkedList<TimelineCollection<dom::ViewTimeline>> mViewTimelineCollections;
  // Map containing all named view timelines, keyed by their names.
  TimelineNameMap<dom::ViewTimeline> mViewTimelineNameMap;
  LinkedList<TimelineScope> mTimelineScopes;
  nsPresContext* mPresContext;
};

template <>
inline void TimelineManager::AddTimelineCollection(
    TimelineCollection<dom::ScrollTimeline>* aCollection) {
  mScrollTimelineCollections.insertBack(aCollection);
}

template <>
inline void TimelineManager::AddTimelineCollection(
    TimelineCollection<dom::ViewTimeline>* aCollection) {
  mViewTimelineCollections.insertBack(aCollection);
}

}  // namespace mozilla

#endif  // mozilla_TimelineManager_h
