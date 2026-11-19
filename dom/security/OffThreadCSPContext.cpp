/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffThreadCSPContext.h"

#include "MainThreadUtils.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/nsCSPParser.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "nsNetUtil.h"

namespace mozilla::dom {

const nsTArray<UniquePtr<const nsCSPPolicy>>& OffThreadCSPContext::Policies() {
  EnsureIPCPoliciesRead();
  return mPolicies;
}

bool OffThreadCSPContext::IsEvalAllowed(bool& aReportViolation) {
  MOZ_ASSERT(!aReportViolation);

  bool trustedTypesRequired =
      (mCSPInfo.requireTrustedTypesForDirectiveState() ==
       RequireTrustedTypesForDirectiveState::ENFORCE);

  for (const UniquePtr<const nsCSPPolicy>& policy : Policies()) {
    if (!(trustedTypesRequired &&
          policy->allows(nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE,
                         CSP_TRUSTED_TYPES_EVAL)) &&
        !policy->allows(nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE,
                        CSP_UNSAFE_EVAL)) {
      aReportViolation = true;
      if (!policy->getReportOnlyFlag()) {
        return false;
      }
    }
  }
  return true;
}

bool OffThreadCSPContext::IsWasmEvalAllowed(bool& aReportViolation) {
  MOZ_ASSERT(!aReportViolation);
  for (const UniquePtr<const nsCSPPolicy>& policy : Policies()) {
    // Either 'unsafe-eval' or 'wasm-unsafe-eval' can allow this
    if (!policy->allows(nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE,
                        CSP_WASM_UNSAFE_EVAL) &&
        !policy->allows(nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE,
                        CSP_UNSAFE_EVAL)) {
      aReportViolation = true;
      if (!policy->getReportOnlyFlag()) {
        return false;
      }
    }
  }
  return true;
}

void OffThreadCSPContext::EnsureIPCPoliciesRead() {
  MOZ_ASSERT(!NS_IsMainThread());

  if (!mPolicies.IsEmpty() || mCSPInfo.policyInfos().IsEmpty()) {
    return;
  }

  nsCOMPtr<nsIURI> selfURI;
  if (NS_WARN_IF(NS_FAILED(
          NS_NewURI(getter_AddRefs(selfURI), mCSPInfo.selfURISpec())))) {
    return;
  }

  for (const auto& policy : mCSPInfo.policyInfos()) {
    UniquePtr<const nsCSPPolicy> cspPolicy(
        nsCSPParser::parseContentSecurityPolicy(
            policy.policy(), selfURI, policy.reportOnlyFlag(), nullptr,
            policy.deliveredViaMetaTagFlag(),
            /* aSuppressLogMessages */ true));
    if (cspPolicy) {
      mPolicies.AppendElement(std::move(cspPolicy));
    }
  }
}

}  // namespace mozilla::dom
