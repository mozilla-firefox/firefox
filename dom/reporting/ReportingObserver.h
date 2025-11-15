/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ReportingObserver_h
#define mozilla_dom_ReportingObserver_h

#include "js/TypeDecls.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupports.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla {
class ErrorResult;

namespace dom {
class GlobalObject;
class ReportingObserverCallback;
struct Report;
struct ReportingObserverOptions;

class ReportingObserver final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ReportingObserver)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(ReportingObserver)

  static already_AddRefed<ReportingObserver> Constructor(
      const GlobalObject& aGlobal, ReportingObserverCallback& aCallback,
      const ReportingObserverOptions& aOptions, ErrorResult& aRv);

  ReportingObserver(nsIGlobalObject* aGlobal,
                    ReportingObserverCallback& aCallback,
                    const nsTArray<nsString>& aTypes, bool aBuffered);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  void Observe();

  void Disconnect();

  void TakeRecords(nsTArray<Report>& aRecords);

  void MaybeReport(Report* aReport);

  MOZ_CAN_RUN_SCRIPT void MaybeNotify();

  void ForgetReports();

 private:
  ~ReportingObserver();

  nsTArray<Report> mReports;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<ReportingObserverCallback> mCallback;
  nsTArray<nsString> mTypes;
  bool mBuffered;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_ReportingObserver_h
