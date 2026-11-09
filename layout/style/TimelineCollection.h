/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TimelineCollection_h
#define mozilla_TimelineCollection_h

#include "mozilla/LinkedList.h"
#include "mozilla/PseudoStyleRequest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/TimelineName.h"
#include "nsAtomHashKeys.h"
#include "nsStyleConsts.h"
#include "nsTHashMap.h"

class nsAtom;

namespace mozilla {
namespace dom {
class Element;
}

template <class TimelineType>
struct TimelineEntry {
  RefPtr<TimelineType> mTimeline = {};
  StyleCascadeLevel mCascadeLevel = {};
};

// The collection of ScrollTimeline or ViewTimeline. We use the template class
// to share the implementation for these two timeline types.
template <class TimelineType>
class TimelineCollection final
    : public LinkedListElement<TimelineCollection<TimelineType>> {
 public:
  using SelfType = TimelineCollection<TimelineType>;

  using TimelineMap =
      nsTHashMap<RefPtr<const nsAtom>, TimelineEntry<TimelineType>>;

  TimelineCollection(dom::Element& aElement,
                     const PseudoStyleRequest& aPseudoRequest)
      : mElement(aElement), mPseudo(aPseudoRequest) {
    MOZ_COUNT_CTOR(TimelineCollection);
  }

  ~TimelineCollection();

  TimelineEntry<TimelineType> Lookup(const nsAtom* aName) const {
    auto entry = mTimelines.MaybeGet(aName);
    if (!entry) {
      return {};
    }
    return *entry;
  }

  already_AddRefed<TimelineType> Extract(const nsAtom* aName) {
    auto entry = mTimelines.Extract(aName);
    if (!entry) {
      return nullptr;
    }
    // We're about to replace this entry, don't care about cascade level.
    return entry->mTimeline.forget();
  }

  void Swap(TimelineMap& aValue) { mTimelines.SwapElements(aValue); }

  void Destroy();

  // Get the collection of timelines for the given |aElement| and or create it
  // if it does not already exist.
  static TimelineCollection* Get(const dom::Element* aElement,
                                 const PseudoStyleRequest& aPseudoRequest);
  const TimelineMap& Timelines() const { return mTimelines; }
  dom::Element& OwningElement() const { return mElement; }
  const PseudoStyleRequest& PseudoRequest() const { return mPseudo; }

 private:
  // The element. Weak reference is fine since it owns us.
  dom::Element& mElement;
  const PseudoStyleRequest mPseudo;

  TimelineMap mTimelines;
};

}  // namespace mozilla

#endif  // mozilla_TimelineCollection_h
