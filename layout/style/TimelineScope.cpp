/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TimelineScope.h"

#include "mozilla/ElementAnimationData.h"
#include "mozilla/dom/Element.h"

namespace mozilla {

TimelineScope::~TimelineScope() {
  MOZ_COUNT_DTOR(TimelineScope);
  remove();
}

void TimelineScope::Destroy() {
  auto* data = mElement.GetAnimationData();
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->GetTimelineScope(mPseudo) == this);
  // Copy the request because mPseudo may be deleted when clearing the scope.
  const PseudoStyleRequest request = mPseudo;
  data->ClearTimelineScopeFor(request);
}

/* static */
TimelineScope* TimelineScope::Get(const dom::Element* aElement,
                                  const PseudoStyleRequest& aPseudoRequest) {
  MOZ_ASSERT(aElement);
  auto* data = aElement->GetAnimationData();
  return data ? data->GetTimelineScope(aPseudoRequest) : nullptr;
}

}  // namespace mozilla
