/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerDebuggerChild_h
#define mozilla_dom_RemoteWorkerDebuggerChild_h

#include "mozilla/dom/PRemoteWorkerDebuggerChild.h"
#include "mozilla/RefPtr.h"

using mozilla::ipc::IPCResult;

namespace mozilla::dom {

class WorkerPrivate;

class RemoteWorkerDebuggerChild final : public PRemoteWorkerDebuggerChild {
  friend class PRemoteWorkerDebuggerChild;
  friend class WorkerPrivate;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerDebuggerChild, final)

  explicit RemoteWorkerDebuggerChild(WorkerPrivate* aWorkerPrivate);

  mozilla::ipc::IPCResult RecvRegisterDone();
  mozilla::ipc::IPCResult RecvUnregisterDone();

  mozilla::ipc::IPCResult RecvInitialize(const nsString& aURL);
  mozilla::ipc::IPCResult RecvPostMessage(const nsString& aMessage);
  mozilla::ipc::IPCResult RecvSetDebuggerReady(const bool& aReady);

 private:
  ~RemoteWorkerDebuggerChild();

  bool mIsInitialized{false};
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_RemoteWorkerDebuggerChild_h
