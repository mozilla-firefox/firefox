/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerDebuggerManagerParent_h
#define mozilla_dom_RemoteWorkerDebuggerManagerParent_h

#include "mozilla/dom/PRemoteWorkerDebuggerManagerParent.h"
#include "mozilla/dom/PRemoteWorkerDebuggerParent.h"
#include "mozilla/ipc/Endpoint.h"

namespace mozilla::dom {

class ContentParent;
class ThreadsafeContentParentHandle;
class WorkerDebuggerManager;

class RemoteWorkerDebuggerManagerParent final
    : public PRemoteWorkerDebuggerManagerParent {
  friend class PRemoteWorkerDebuggerManagerParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerDebuggerManagerParent,
                                        final)

  // aProcess is null when the child endpoint lives in the parent process.
  static RefPtr<RemoteWorkerDebuggerManagerParent> CreateForProcess(
      ContentParent* aProcess,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>* aChildEp);

  explicit RemoteWorkerDebuggerManagerParent(
      ThreadsafeContentParentHandle* aProcess);

  mozilla::ipc::IPCResult RecvRegister(
      const RemoteWorkerDebuggerInfo& aDebuggerInfo,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent>&& aParentEp);

 private:
  ~RemoteWorkerDebuggerManagerParent();

  bool ValidateDebuggerInfo(const RemoteWorkerDebuggerInfo& aDebuggerInfo);
  bool IsIdUsableByThisProcess(WorkerDebuggerManager* aManager,
                               const nsString& aId);

  // Null when the child endpoint lives in the parent process.
  const RefPtr<ThreadsafeContentParentHandle> mProcess;
};

}  // end of namespace mozilla::dom

#endif
