/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ReportDeliver_h
#define mozilla_dom_ReportDeliver_h

#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsString.h"
#include "nsTArray.h"

// XXX Avoid including this here by moving function bodies to the cpp file
#include "nsIPrincipal.h"

class nsIPrincipal;
class nsPIDOMWindowInner;

namespace mozilla::dom {

class ReportDeliver final : public nsIObserver, public nsINamed {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSINAMED

  struct ReportData {
    nsString mType;
    nsString mGroupName;
    nsString mURL;
    nsCString mEndpointURL;
    nsString mUserAgent;
    TimeStamp mCreationTime;
    nsCString mReportBodyJSON;
    nsCOMPtr<nsIPrincipal> mPrincipal;
    uint32_t mFailures;
  };

  static void Record(nsPIDOMWindowInner* aWindow, const nsAString& aType,
                     const nsAString& aGroupName, const nsAString& aURL,
                     ReportBody* aBody);

  static void Fetch(const ReportData& aReportData);

  void AppendReportData(const ReportData& aReportData);

  void Notify();

 private:
  ReportDeliver();
  ~ReportDeliver();

  nsTArray<ReportData> mReportQueue;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ReportDeliver_h
