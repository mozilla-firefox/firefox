/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInferenceChild.h"
#include "HWInferenceManagerParent.h"
#include "mozilla/Logging.h"

namespace mozilla::hwinference {

LazyLogModule gHWInferenceLog("HWInference");
#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Error, fmt, ##__VA_ARGS__)

void HWInferenceChild::Shutdown() { PHWInferenceChild::Close(); }

ipc::IPCResult HWInferenceChild::RecvNewContentHWInferenceManager(
    Endpoint<hwinference::PHWInferenceManagerParent>&& aEndpoint,
    const dom::ContentParentId& aContentId) {
  LOGD("[{} - {}] Received connection request from content {}", fmt::ptr(this),
       __func__, static_cast<uint64_t>(aContentId));

  if (!HWInferenceManagerParent::CreateForContent(std::move(aEndpoint),
                                                  aContentId)) {
    LOGE(
        "[{} - {}]"
        "Error: Failed to create HWInferenceManagerParent, content id: {}",
        fmt::ptr(this), __func__, static_cast<uint64_t>(aContentId));
    return IPC_FAIL(this, "Failed to create HWInferenceManagerParent");
  }

  LOGD("[{} - {}] Successfully created HWInferenceManagerParent for content {}",
       fmt::ptr(this), __func__, static_cast<uint64_t>(aContentId));
  return IPC_OK();
}

RefPtr<HWInferenceChild::IsModelAvailablePromise>
HWInferenceChild::SendIsModelAvailable(const nsCString& aTask,
                                       const nsCString& aId) {
  LOGD(
      "[{} - {}] Sending model availability request to parent process: "
      "task={} id={}",
      fmt::ptr(this), __func__, aTask, aId);

  return PHWInferenceChild::SendIsModelAvailable(aTask, aId);
}

RefPtr<HWInferenceChild::IsModelInstalledPromise>
HWInferenceChild::SendIsModelInstalled(const nsCString& aTask,
                                       const nsCString& aId) {
  LOGD(
      "[{} - {}] Sending model installed check to parent process: task={} "
      "id={}",
      fmt::ptr(this), __func__, aTask, aId);

  return PHWInferenceChild::SendIsModelInstalled(aTask, aId);
}

RefPtr<HWInferenceChild::InstallModelPromise>
HWInferenceChild::SendInstallModel(const nsCString& aTask, const nsCString& aId,
                                   uint64_t aInnerWindowId,
                                   const dom::ContentParentId& aContentId) {
  LOGD(
      "[{} - {}] Sending model installation request to parent process: "
      "task={} id={}",
      fmt::ptr(this), __func__, aTask, aId);

  return PHWInferenceChild::SendInstallModel(aTask, aId, aInnerWindowId,
                                             aContentId);
}

RefPtr<HWInferenceChild::GetModelFilePromise>
HWInferenceChild::SendGetModelFile(const nsCString& aTask,
                                   const nsCString& aId) {
  LOGD(
      "[{} - {}] Sending model file request to parent process: task={} "
      "id={}",
      fmt::ptr(this), __func__, aTask.get(), aId.get());

  return PHWInferenceChild::SendGetModelFile(aTask, aId);
}
}  // namespace mozilla::hwinference

#undef LOG
