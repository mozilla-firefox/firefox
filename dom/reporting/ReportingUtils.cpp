/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ReportingUtils.h"

#include "mozilla/dom/ReportingBinding.h"
#include "mozilla/dom/ReportDeliver.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsAtom.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

/* static */
void ReportingUtils::Report(nsIGlobalObject* aGlobal, nsAtom* aType,
                            const nsAString& aGroupName, const nsAString& aURL,
                            ReportBody* aBody) {
  MOZ_ASSERT(aGlobal);
  MOZ_ASSERT(aBody);

  nsDependentAtomString type(aType);

  mozilla::dom::Report report;
  report.mType.Construct(type);
  report.mUrl.Construct(aURL);
  report.mBody.Construct(*aBody);

  aGlobal->BroadcastReport(&report);

  if (!NS_IsMainThread()) {
    return;
  }

  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
  if (!window) {
    return;
  }

  // Send the report to the server.
  ReportDeliver::Record(window, type, aGroupName, aURL, aBody);
}

}  // namespace mozilla::dom
