/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_WorkerCSPContext_h__
#define mozilla_dom_workers_WorkerCSPContext_h__

#include "mozilla/Result.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nscore.h"

class nsIContentSecurityPolicy;

namespace mozilla::dom {

// A minimal version of nsCSPContext that can run on Worker threads.
class WorkerCSPContext final {
 public:
  explicit WorkerCSPContext(mozilla::ipc::CSPInfo&& aInfo) : mCSPInfo(aInfo) {}

  static Result<UniquePtr<WorkerCSPContext>, nsresult> CreateFromCSP(
      nsIContentSecurityPolicy* aCSP);

  const mozilla::ipc::CSPInfo& CSPInfo() const { return mCSPInfo; }
  const nsTArray<UniquePtr<const nsCSPPolicy>>& Policies();

  bool IsEvalAllowed(bool& aReportViolation);
  bool IsWasmEvalAllowed(bool& aReportViolation);

 private:
  void EnsureIPCPoliciesRead();

  // Thread boundaries require us to not only store a CSP object, but also a
  // serialized version of the CSP. Reason being: Serializing a CSP to a CSPInfo
  // needs to happen on the main thread, but storing the CSPInfo needs to happen
  // on the worker thread. We move the CSPInfo into the Client within
  // ScriptExecutorRunnable::PreRun().
  mozilla::ipc::CSPInfo mCSPInfo;

  // This is created lazily by parsing the policies in CSPInfo on the worker
  // thread.
  nsTArray<UniquePtr<const nsCSPPolicy>> mPolicies;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_workers_WorkerCSPContext_h__
