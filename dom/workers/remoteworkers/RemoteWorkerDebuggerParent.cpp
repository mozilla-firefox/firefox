/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerDebuggerParent.h"
#include "mozilla/dom/WorkerDebuggerManager.h"
#include "mozilla/dom/WorkerPrivate.h"

namespace mozilla::dom {

RemoteWorkerDebuggerParent::RemoteWorkerDebuggerParent(
    const RemoteWorkerDebuggerInfo& aWorkerDebuggerInfo,
    Endpoint<PRemoteWorkerDebuggerParent>&& aParentEp)
    : mWorkerDebuggerInfo(std::move(aWorkerDebuggerInfo)) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  MOZ_ASSERT_DEBUG_OR_FUZZING(aParentEp.IsValid());
  aParentEp.Bind(this);
  if (mWorkerDebuggerInfo.type() == WorkerKindDedicated) {
    mWindowIDs.AppendElement(mWorkerDebuggerInfo.windowID());
  }
}

RemoteWorkerDebuggerParent::~RemoteWorkerDebuggerParent() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
}

// PRemoteWorkerDebugger IPC interface
mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvUnregister() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());

  RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::Get();

  MOZ_ASSERT_DEBUG_OR_FUZZING(manager);
  manager->UnregisterDebugger(this);
  for (const auto& listener : mListeners.Clone()) {
    listener->OnClose();
  }

  if (CanSend()) {
    Unused << SendUnregisterDone();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvReportErrorToDebugger(
    const RemoteWorkerDebuggerErrorInfo& aErrorInfo) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());

  for (const auto& listener : mListeners.Clone()) {
    listener->OnError(aErrorInfo.fileName(), aErrorInfo.lineNo(),
                      aErrorInfo.message());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvPostMessageToDebugger(
    const nsString& aMessage) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  for (const auto& listener : mListeners.Clone()) {
    listener->OnMessage(aMessage);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvSetAsInitialized() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  mIsInitialized = true;
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvSetAsClosed() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  mIsClosed = true;
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvAddWindowID(
    const uint64_t& aWindowID) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  mWindowIDs.AppendElement(aWindowID);
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerParent::RecvRemoveWindowID(
    const uint64_t& aWindowID) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
  mWindowIDs.RemoveElement(aWindowID);
  return IPC_OK();
}

// nsIWorkerDebugger interface
NS_IMPL_ISUPPORTS(RemoteWorkerDebuggerParent, nsIWorkerDebugger)

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetIsClosed(bool* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = mIsClosed;
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetIsChrome(bool* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = mWorkerDebuggerInfo.isChrome();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetIsRemote(bool* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = true;
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetIsInitialized(bool* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = mIsInitialized;
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetParent(nsIWorkerDebugger** aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  nsCOMPtr<nsIWorkerDebugger> parent;
  if (!mWorkerDebuggerInfo.parentId().IsEmpty()) {
    RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::Get();
    MOZ_ASSERT_DEBUG_OR_FUZZING(manager);

    parent = manager->GetDebuggerById(mWorkerDebuggerInfo.parentId());
  }
  parent.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetType(uint32_t* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = mWorkerDebuggerInfo.type();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetUrl(nsAString& aResult) {
  AssertIsOnMainThread();

  aResult = mWorkerDebuggerInfo.url();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetWindow(mozIDOMWindow** aResult) {
  AssertIsOnMainThread();
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetWindowIDs(nsTArray<uint64_t>& aResult) {
  AssertIsOnMainThread();

  aResult = mWindowIDs.Clone();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetPrincipal(nsIPrincipal** aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  nsCOMPtr<nsIPrincipal> principal = mWorkerDebuggerInfo.principal();
  principal.forget(aResult);

  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetServiceWorkerID(uint32_t* aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(aResult);

  *aResult = mWorkerDebuggerInfo.serviceWorkerID();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetId(nsAString& aResult) {
  AssertIsOnMainThread();

  aResult = mWorkerDebuggerInfo.Id();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::GetName(nsAString& aResult) {
  AssertIsOnMainThread();

  aResult = mWorkerDebuggerInfo.name();
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::Initialize(const nsAString& aURL) {
  AssertIsOnMainThread();
  if (CanSend()) {
    nsAutoString url(aURL);
    Unused << SendInitialize(url);
  }
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::PostMessageMoz(const nsAString& aMessage) {
  AssertIsOnMainThread();
  if (CanSend()) {
    nsAutoString message(aMessage);
    Unused << SendPostMessage(message);
  }
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::AddListener(nsIWorkerDebuggerListener* aListener) {
  AssertIsOnMainThread();

  if (mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }
  mListeners.AppendElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::RemoveListener(
    nsIWorkerDebuggerListener* aListener) {
  AssertIsOnMainThread();

  if (!mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }
  mListeners.RemoveElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
RemoteWorkerDebuggerParent::SetDebuggerReady(bool aReady) {
  AssertIsOnMainThread();
  if (CanSend()) {
    Unused << SendSetDebuggerReady(aReady);
  }
  return NS_OK;
}

}  // namespace mozilla::dom
