/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ReportingObserver.h"

#include "mozilla/dom/Report.h"
#include "mozilla/dom/ReportingBinding.h"
#include "nsContentUtils.h"
#include "nsIGlobalObject.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ReportingObserver)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ReportingObserver)
  tmp->Disconnect();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReports)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCallback)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ReportingObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReports)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCallback)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

/* static */
already_AddRefed<ReportingObserver> ReportingObserver::Constructor(
    const GlobalObject& aGlobal, ReportingObserverCallback& aCallback,
    const ReportingObserverOptions& aOptions, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_ASSERT(global);

  nsTArray<nsString> types;
  if (aOptions.mTypes.WasPassed()) {
    types = aOptions.mTypes.Value();
  }

  RefPtr<ReportingObserver> ro =
      new ReportingObserver(global, aCallback, types, aOptions.mBuffered);

  return ro.forget();
}

ReportingObserver::ReportingObserver(nsIGlobalObject* aGlobal,
                                     ReportingObserverCallback& aCallback,
                                     const nsTArray<nsString>& aTypes,
                                     bool aBuffered)
    : mGlobal(aGlobal),
      mCallback(&aCallback),
      mTypes(aTypes.Clone()),
      mBuffered(aBuffered) {
  MOZ_ASSERT(aGlobal);
}

ReportingObserver::~ReportingObserver() { Disconnect(); }

JSObject* ReportingObserver::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return ReportingObserver_Binding::Wrap(aCx, this, aGivenProto);
}

void ReportingObserver::Observe() {
  mGlobal->RegisterReportingObserver(this, mBuffered);
  mBuffered = false;
}

void ReportingObserver::Disconnect() {
  if (mGlobal) {
    mGlobal->UnregisterReportingObserver(this);
  }
}

void ReportingObserver::TakeRecords(nsTArray<RefPtr<Report>>& aRecords) {
  mReports.SwapElements(aRecords);
}

namespace {

class ReportRunnable final : public DiscardableRunnable {
 public:
  explicit ReportRunnable(nsIGlobalObject* aGlobal)
      : DiscardableRunnable("ReportRunnable"), mGlobal(aGlobal) {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    MOZ_KnownLive(mGlobal)->NotifyReportingObservers();
    return NS_OK;
  }

 private:
  const nsCOMPtr<nsIGlobalObject> mGlobal;
};

}  // namespace

void ReportingObserver::MaybeReport(Report* aReport) {
  MOZ_ASSERT(aReport);

  if (!mTypes.IsEmpty() &&
      !mTypes.Contains(nsDependentAtomString(aReport->Type()))) {
    return;
  }

  bool wasEmpty = mReports.IsEmpty();

  RefPtr<Report> report = aReport->Clone();
  MOZ_ASSERT(report);

  if (NS_WARN_IF(!mReports.AppendElement(report, fallible))) {
    return;
  }

  if (!wasEmpty) {
    return;
  }

  RefPtr<ReportRunnable> r = new ReportRunnable(mGlobal);
  NS_DispatchToCurrentThread(r);
}

void ReportingObserver::MaybeNotify() {
  if (mReports.IsEmpty()) {
    return;
  }

  // Let's take the ownership of the reports.
  nsTArray<RefPtr<Report>> list = std::move(mReports);

  Sequence<OwningNonNull<Report>> reports;
  for (Report* report : list) {
    if (NS_WARN_IF(!reports.AppendElement(*report, fallible))) {
      return;
    }
  }

  // We should report if this throws exception. But where?
  RefPtr<ReportingObserverCallback> callback(mCallback);
  callback->Call(this, reports, *this);
}

void ReportingObserver::ForgetReports() { mReports.Clear(); }

}  // namespace mozilla::dom
