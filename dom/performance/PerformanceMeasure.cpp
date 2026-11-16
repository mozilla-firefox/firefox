/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PerformanceMeasure.h"

#include "MainThreadUtils.h"
#include "mozilla/dom/PerformanceMeasureBinding.h"
#include "nsGkAtoms.h"

using namespace mozilla::dom;

PerformanceMeasure::PerformanceMeasure(nsISupports* aParent,
                                       const nsAString& aName,
                                       DOMHighResTimeStamp aStartTime,
                                       DOMHighResTimeStamp aEndTime,
                                       const JS::Handle<JS::Value>& aDetail)
    : PerformanceEntry(aParent, aName, nsGkAtoms::measure),
      mStartTime(aStartTime),
      mDuration(aEndTime - aStartTime),
      mDetail(aDetail) {
  mozilla::HoldJSObjects(this);
}

PerformanceMeasure::~PerformanceMeasure() { mozilla::DropJSObjects(this); }

NS_IMPL_CYCLE_COLLECTION_CLASS(PerformanceMeasure)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PerformanceMeasure,
                                                PerformanceEntry)
  tmp->mDetail.setUndefined();
  mozilla::DropJSObjects(tmp);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PerformanceMeasure,
                                                  PerformanceEntry)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PerformanceMeasure,
                                               PerformanceEntry)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mDetail)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(PerformanceMeasure,
                                               PerformanceEntry)

JSObject* PerformanceMeasure::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return PerformanceMeasure_Binding::Wrap(aCx, this, aGivenProto);
}

void PerformanceMeasure::GetDetail(JSContext* aCx,
                                   JS::MutableHandle<JS::Value> aRetval) {
  // Return a copy so that this method always returns the value it is set to
  // (i.e. it'll return the same value even if the caller assigns to it). Note
  // that if detail is an object, its contents can be mutated and this is
  // expected.
  aRetval.set(mDetail);
}

size_t PerformanceMeasure::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}
