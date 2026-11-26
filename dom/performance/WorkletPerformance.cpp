/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkletPerformance.h"

#include "mozilla/dom/WorkletGlobalScope.h"
#include "mozilla/dom/WorkletImpl.h"
#include "mozilla/dom/WorkletPerformanceBinding.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WorkletPerformance, mGlobal)

WorkletPerformance::~WorkletPerformance() = default;

WorkletPerformance::WorkletPerformance(WorkletGlobalScope* aGlobal)
    : mGlobal(aGlobal),
      mTimeOrigin(aGlobal->Impl()->TimeOrigin()),
      mRandomTimelineSeed(aGlobal->Impl()->RandomTimelineSeed()),
      mRTPCallerType(aGlobal->GetRTPCallerType()) {}

JSObject* WorkletPerformance::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return WorkletPerformance_Binding::Wrap(aCx, this, aGivenProto);
}

double WorkletPerformance::Now() const {
  // Relative high resolution time from the outside settings object's time
  // origin, coarsened with the window's timeline seed.
  // https://w3c.github.io/hr-time/#sec-workletperformance
  double rawTime = (TimeStamp::Now() - mTimeOrigin).ToMilliseconds();
  return nsRFPService::ReduceTimePrecisionAsMSecs(rawTime, mRandomTimelineSeed,
                                                  mRTPCallerType);
}

}  // namespace mozilla::dom
