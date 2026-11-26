/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WorkletPerformance_h
#define mozilla_dom_WorkletPerformance_h

#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "nsRFPService.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class WorkletGlobalScope;

// https://w3c.github.io/hr-time/#sec-workletperformance
// now() is called from AudioWorkletProcessor.process() on the real-time audio
// thread: everything it needs is computed at construction.
class WorkletPerformance final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WorkletPerformance)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WorkletPerformance)

  explicit WorkletPerformance(WorkletGlobalScope* aGlobal);

  WorkletGlobalScope* GetParentObject() const { return mGlobal; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  double Now() const;

 private:
  ~WorkletPerformance();

  RefPtr<WorkletGlobalScope> mGlobal;
  const TimeStamp mTimeOrigin;
  const int64_t mRandomTimelineSeed;
  const RTPCallerType mRTPCallerType;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_WorkletPerformance_h
