/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TimelineCollection.h"

#include <type_traits>

#include "mozilla/ElementAnimationData.h"
#include "mozilla/TimelineManager.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ScrollTimeline.h"
#include "mozilla/dom/ViewTimeline.h"
#include "nsContentUtils.h"
#include "nsPresContext.h"

namespace mozilla {

template <class TimelineType>
TimelineCollection<TimelineType>::~TimelineCollection() {
  MOZ_COUNT_DTOR(TimelineCollection);

  // The TimelineManager refers to our timelines by name, whether we are
  // destroyed through it or with our element (e.g. when it is unbound from the
  // tree).
  if (auto* presContext = nsContentUtils::GetContextForContent(&mElement)) {
    if (auto* manager = presContext->TimelineManager()) {
      manager->UnregisterTimelines(*this);
    }
  }

  LinkedListElement<SelfType>::remove();
}

template <class TimelineType>
void TimelineCollection<TimelineType>::Destroy() {
  auto* data = mElement.GetAnimationData();
  MOZ_ASSERT(data);
  // Copy the request because mPseudo may be deleted when clearing the
  // collection.
  const PseudoStyleRequest request = mPseudo;
  if constexpr (std::is_same_v<TimelineType, dom::ScrollTimeline>) {
    MOZ_ASSERT(data->GetScrollTimelineCollection(request) == this);
    data->ClearScrollTimelineCollectionFor(request);
  } else if constexpr (std::is_same_v<TimelineType, dom::ViewTimeline>) {
    MOZ_ASSERT(data->GetViewTimelineCollection(request) == this);
    data->ClearViewTimelineCollectionFor(request);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unsupported TimelienType");
  }
}

template <class TimelineType>
/* static */ TimelineCollection<TimelineType>*
TimelineCollection<TimelineType>::Get(
    const dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest) {
  MOZ_ASSERT(aElement);
  auto* data = aElement->GetAnimationData();
  if (!data) {
    return nullptr;
  }

  if constexpr (std::is_same_v<TimelineType, dom::ScrollTimeline>) {
    return data->GetScrollTimelineCollection(aPseudoRequest);
  }

  if constexpr (std::is_same_v<TimelineType, dom::ViewTimeline>) {
    return data->GetViewTimelineCollection(aPseudoRequest);
  }

  return nullptr;
}

// Explicit class instantiations
template class TimelineCollection<dom::ScrollTimeline>;
template class TimelineCollection<dom::ViewTimeline>;

}  // namespace mozilla
