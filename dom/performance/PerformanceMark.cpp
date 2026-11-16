/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PerformanceMark.h"

#include "MainThreadUtils.h"
#include "Performance.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/PerformanceBinding.h"
#include "mozilla/dom/PerformanceMarkBinding.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"

using namespace mozilla::dom;

PerformanceMark::PerformanceMark(nsISupports* aParent, const nsAString& aName,
                                 DOMHighResTimeStamp aStartTime,
                                 const JS::Handle<JS::Value>& aDetail,
                                 DOMHighResTimeStamp aUnclampedStartTime)
    : PerformanceEntry(aParent, aName, nsGkAtoms::mark),
      mStartTime(aStartTime),
      mDetail(aDetail),
      mUnclampedStartTime(aUnclampedStartTime) {
  mozilla::HoldJSObjects(this);
}

already_AddRefed<PerformanceMark> PerformanceMark::Constructor(
    const GlobalObject& aGlobal, const nsAString& aMarkName,
    const PerformanceMarkOptions& aMarkOptions, ErrorResult& aRv) {
  const nsCOMPtr<nsIGlobalObject> global =
      do_QueryInterface(aGlobal.GetAsSupports());
  return PerformanceMark::Constructor(aGlobal.Context(), global, aMarkName,
                                      aMarkOptions, aRv);
}

already_AddRefed<PerformanceMark> PerformanceMark::Constructor(
    JSContext* aCx, nsIGlobalObject* aGlobal, const nsAString& aMarkName,
    const PerformanceMarkOptions& aMarkOptions, ErrorResult& aRv) {
  RefPtr<Performance> performance = Performance::Get(aCx, aGlobal);
  if (!performance) {
    // This is similar to the message that occurs when accessing `performance`
    // from outside a valid global.
    aRv.ThrowTypeError(
        "can't access PerformanceMark constructor, performance is null");
    return nullptr;
  }

  if (performance->IsGlobalObjectWindow() &&
      performance->IsPerformanceTimingAttribute(aMarkName)) {
    aRv.ThrowSyntaxError("markName cannot be a performance timing attribute");
    return nullptr;
  }

  DOMHighResTimeStamp startTime = aMarkOptions.mStartTime.WasPassed()
                                      ? aMarkOptions.mStartTime.Value()
                                      : performance->Now();
  // We need to get the unclamped start time to be able to add profiler markers
  // with precise time/duration. This is not exposed to web and only used by the
  // profiler.
  // If a mStartTime is passed by the user, we will always have a clamped value.
  DOMHighResTimeStamp unclampedStartTime = aMarkOptions.mStartTime.WasPassed()
                                               ? startTime
                                               : performance->NowUnclamped();
  if (startTime < 0) {
    aRv.ThrowTypeError("Expected startTime >= 0");
    return nullptr;
  }

  JS::Rooted<JS::Value> detail(aCx);
  if (aMarkOptions.mDetail.isNullOrUndefined()) {
    detail.setNull();
  } else {
    StructuredSerializeOptions serializeOptions;
    JS::Rooted<JS::Value> valueToClone(aCx, aMarkOptions.mDetail);
    nsContentUtils::StructuredClone(aCx, aGlobal, valueToClone,
                                    serializeOptions, &detail, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  return do_AddRef(new PerformanceMark(aGlobal, aMarkName, startTime, detail,
                                       unclampedStartTime));
}

PerformanceMark::~PerformanceMark() { mozilla::DropJSObjects(this); }

NS_IMPL_CYCLE_COLLECTION_CLASS(PerformanceMark)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PerformanceMark,
                                                PerformanceEntry)
  tmp->mDetail.setUndefined();
  mozilla::DropJSObjects(tmp);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PerformanceMark,
                                                  PerformanceEntry)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PerformanceMark,
                                               PerformanceEntry)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mDetail)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(PerformanceMark,
                                               PerformanceEntry)

JSObject* PerformanceMark::WrapObject(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return PerformanceMark_Binding::Wrap(aCx, this, aGivenProto);
}

void PerformanceMark::GetDetail(JSContext* aCx,
                                JS::MutableHandle<JS::Value> aRetval) {
  // Return a copy so that this method always returns the value it is set to
  // (i.e. it'll return the same value even if the caller assigns to it). Note
  // that if detail is an object, its contents can be mutated and this is
  // expected.
  aRetval.set(mDetail);
}

size_t PerformanceMark::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}
