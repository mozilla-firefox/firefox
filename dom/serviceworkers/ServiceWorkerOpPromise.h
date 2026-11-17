/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_serviceworkeroppromise_h__
#define mozilla_dom_serviceworkeroppromise_h__

#include <utility>

#include "mozilla/MozPromise.h"

#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/ServiceWorkerOpArgs.h"
#include "nsCOMPtr.h"
#include "nsIInputStream.h"

namespace mozilla::dom {

class InternalResponse;

// The body streams are extracted from the InternalResponse on the worker
// thread before this struct crosses to the Worker Launcher thread for
// serialization, instead of serializing lazily from the shared
// InternalResponse there.
struct SynthesizeResponseArgs {
  ChildToParentSynthesizeResponseArgs mIPCArgs;
  nsCOMPtr<nsIInputStream> mBody;
  nsCOMPtr<nsIInputStream> mAlternativeBody;
};

using FetchEventRespondWithResult =
    Variant<SynthesizeResponseArgs, ResetInterceptionArgs,
            CancelInterceptionArgs>;

using FetchEventRespondWithPromise =
    MozPromise<FetchEventRespondWithResult, CancelInterceptionArgs, true>;

// The reject type int is arbitrary, since this promise will never get rejected.
// Unfortunately void is not supported as a reject type.
using FetchEventPreloadResponseAvailablePromise =
    MozPromise<SafeRefPtr<InternalResponse>, int, true>;

using FetchEventPreloadResponseTimingPromise =
    MozPromise<ResponseTiming, int, true>;

using FetchEventPreloadResponseEndPromise =
    MozPromise<ResponseEndArgs, int, true>;

using ServiceWorkerOpPromise =
    MozPromise<ServiceWorkerOpResult, nsresult, true>;

using ServiceWorkerFetchEventOpPromise =
    MozPromise<ServiceWorkerFetchEventOpResult, nsresult, true>;

}  // namespace mozilla::dom

#endif  // mozilla_dom_serviceworkeroppromise_h__
