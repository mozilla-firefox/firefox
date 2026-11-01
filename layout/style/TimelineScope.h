/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TimelineScope_h
#define mozilla_TimelineScope_h

#include <utility>

#include "mozilla/LinkedList.h"
#include "mozilla/PseudoStyleRequest.h"
#include "mozilla/RefPtr.h"
#include "nsAtom.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla {
namespace dom {
class Element;
}

// The timeline names that `timeline-scope` on a (pseudo-)element limits to its
// subtree. Mirrors TimelineCollection: owned by the element's
// ElementAnimationData, and linked into a TimelineManager list so that it can
// be destroyed with the pres context.
// https://drafts.csswg.org/scroll-animations-1/#timeline-scope
class TimelineScope final : public LinkedListElement<TimelineScope> {
 public:
  TimelineScope(dom::Element& aElement,
                const PseudoStyleRequest& aPseudoRequest)
      : mElement(aElement), mPseudo(aPseudoRequest) {
    MOZ_COUNT_CTOR(TimelineScope);
  }

  ~TimelineScope();

  void SetNames(nsTArray<RefPtr<const nsAtom>>&& aNames) {
    mNames = std::move(aNames);
  }
  // An empty list represents `timeline-scope: all`.
  bool ScopesAll() const { return mNames.IsEmpty(); }
  bool Scopes(const nsAtom* aName) const {
    return ScopesAll() || mNames.Contains(aName);
  }

  void Destroy();

  static TimelineScope* Get(const dom::Element* aElement,
                            const PseudoStyleRequest& aPseudoRequest);

 private:
  // The element. Weak reference is fine since it owns us.
  dom::Element& mElement;
  const PseudoStyleRequest mPseudo;
  // Could become AutoTArray<RefPtr<const nsAtom>, 1> if a single name turns
  // out to be the common case.
  nsTArray<RefPtr<const nsAtom>> mNames;
};

}  // namespace mozilla

#endif  // mozilla_TimelineScope_h
