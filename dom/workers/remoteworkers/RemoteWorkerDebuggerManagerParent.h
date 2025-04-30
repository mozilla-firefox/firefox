/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerDebuggerManagerParent_h
#define mozilla_dom_RemoteWorkerDebuggerManagerParent_h

#include "mozilla/dom/PRemoteWorkerDebuggerManagerParent.h"
#include "mozilla/dom/PRemoteWorkerDebuggerParent.h"
#include "mozilla/ipc/Endpoint.h"

namespace mozilla::dom {

class RemoteWorkerDebuggerManagerParent final
    : public PRemoteWorkerDebuggerManagerParent {
  friend class PRemoteWorkerDebuggerManagerParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerDebuggerManagerParent,
                                        final)

  static RefPtr<RemoteWorkerDebuggerManagerParent> CreateForProcess(
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>* aChildEp);

  RemoteWorkerDebuggerManagerParent();

  mozilla::ipc::IPCResult RecvRegister(
      const RemoteWorkerDebuggerInfo& aDebuggerInfo,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent>&& aParentEp);

 private:
  ~RemoteWorkerDebuggerManagerParent();
};

}  // end of namespace mozilla::dom

#endif
