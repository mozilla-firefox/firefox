/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerDebuggerParent_h
#define mozilla_dom_RemoteWorkerDebuggerParent_h

#include "mozilla/dom/PRemoteWorkerDebuggerChild.h"
#include "mozilla/dom/PRemoteWorkerDebuggerParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsIWorkerDebugger.h"
#include "nsTArray.h"

namespace mozilla::dom {

class RemoteWorkerDebuggerParent final : public PRemoteWorkerDebuggerParent,
                                         public nsIWorkerDebugger {
  friend class PRemoteWorkerDebuggerParent;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWORKERDEBUGGER

 public:
  RemoteWorkerDebuggerParent(
      const RemoteWorkerDebuggerInfo& aWorkerDebuggerInfo,
      Endpoint<PRemoteWorkerDebuggerParent>&& aParentEp);

  mozilla::ipc::IPCResult RecvUnregister();

  mozilla::ipc::IPCResult RecvReportErrorToDebugger(
      const RemoteWorkerDebuggerErrorInfo& aErrorInfo);

  mozilla::ipc::IPCResult RecvPostMessageToDebugger(const nsString& aMessage);

  mozilla::ipc::IPCResult RecvSetAsInitialized();
  mozilla::ipc::IPCResult RecvSetAsClosed();

  mozilla::ipc::IPCResult RecvAddWindowID(const uint64_t& aWindowID);
  mozilla::ipc::IPCResult RecvRemoveWindowID(const uint64_t& aWindowID);

 private:
  ~RemoteWorkerDebuggerParent();

  bool mIsInitialized{false};
  bool mIsClosed{false};
  RemoteWorkerDebuggerInfo mWorkerDebuggerInfo;

  nsTArray<uint64_t> mWindowIDs;

  nsTArray<nsCOMPtr<nsIWorkerDebuggerListener>> mListeners;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_RemoteWorkerDebuggerParent_h
