/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSOracleParent.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/PJSOracle.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessManager.h"

using namespace mozilla;
using namespace mozilla::dom;

static StaticRefPtr<JSOracleParent> sOracleSingleton;

/* static */
void JSOracleParent::WithJSOracle(
    const std::function<void(JSOracleParent* aParent)>& aCallback) {
  GetSingleton()->StartJSOracle()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [aCallback](const JSOraclePromise::ResolveOrRejectValue& aResult) {
        aCallback(aResult.IsReject() || !aResult.ResolveValue()
                      ? nullptr
                      : GetSingleton());
      });
}

void JSOracleParent::ActorDestroy(ActorDestroyReason aReason) {
  // Given an actor can only be bound to one process,
  // if the utility process crashes and we create a new one,
  // we can't reuse the same JSOracleParent instance for it,
  // so we always create a new JSOracleParent to replace
  // the existing one.
  if (aReason == ActorDestroyReason::AbnormalShutdown) {
    sOracleSingleton = new JSOracleParent();
  }
}

/* static */
JSOracleParent* JSOracleParent::GetSingleton() {
  if (!sOracleSingleton) {
    sOracleSingleton = new JSOracleParent();
    ClearOnShutdown(&sOracleSingleton);
  }

  return sOracleSingleton;
}

RefPtr<JSOracleParent::JSOraclePromise> JSOracleParent::StartJSOracle() {
  using namespace mozilla::ipc;
  RefPtr<JSOracleParent> parent = JSOracleParent::GetSingleton();
  return UtilityProcessManager::GetSingleton()->StartJSOracle(parent);
}

nsresult JSOracleParent::BindToUtilityProcess(
    const RefPtr<mozilla::ipc::UtilityProcessParent>& aUtilityParent) {
  if (!aUtilityParent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  Endpoint<PJSOracleParent> parentEnd;
  Endpoint<PJSOracleChild> childEnd;
  if (NS_FAILED(PJSOracle::CreateEndpoints(
          mozilla::ipc::EndpointProcInfo::Current(),
          aUtilityParent->OtherEndpointProcInfo(), &parentEnd, &childEnd))) {
    return NS_ERROR_FAILURE;
  }

  if (!aUtilityParent->SendStartJSOracleService(std::move(childEnd))) {
    return NS_ERROR_FAILURE;
  }

  Bind(std::move(parentEnd));

  return NS_OK;
}

void JSOracleParent::Bind(Endpoint<PJSOracleParent>&& aEndpoint) {
  DebugOnly<bool> ok = aEndpoint.Bind(this);
  MOZ_ASSERT(ok);
}
