/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCodeCoverage.h"
#include "mozilla/CodeCoverageHandler.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/MessageChannel.h"
#include "nsThreadUtils.h"

using mozilla::dom::ContentParent;
using mozilla::dom::Promise;
using mozilla::ipc::ResponseRejectReason;

NS_IMPL_ISUPPORTS(nsCodeCoverage, nsICodeCoverage)

NS_IMETHODIMP nsCodeCoverage::FlushCounters(JSContext* cx, Promise** aPromise) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(cx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  mozilla::ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(global, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  mozilla::CodeCoverageHandler::FlushCounters();

  using FlushPromise = ContentParent::FlushCodeCoverageCountersPromise;
  auto* target = mozilla::GetCurrentSerialEventTarget();
  nsTArray<RefPtr<FlushPromise>> requests;
  for (auto* process : ContentParent::AllProcesses(ContentParent::eLive)) {
    auto logRejection = [pid = process->Pid(),
                         kind = process->GetRemoteType().StringifyKind(),
                         shuttingDown = process->IsSignaledImpendingShutdown()](
                            ResponseRejectReason aReason) {
      printf_stderr(
          "[CodeCoverage] Coverage flush request for process %d (%s%s) "
          "rejected (%s).\n",
          pid, kind.get(), shuttingDown ? ", shutting down" : "",
          EnumValueToString(aReason));
      return aReason;
    };
    requests.AppendElement(process->SendFlushCodeCoverageCounters()->MapErr(
        target, __func__, std::move(logRejection)));
  }

  FlushPromise::AllSettled(target, requests)->Then(target, __func__, [promise] {
    promise->MaybeResolveWithUndefined();
  });

  promise.forget(aPromise);
  return NS_OK;
}
