/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInferenceChild.h"
#include "mozilla/hwinference/TextGenerationChild.h"
#include "mozilla/Logging.h"
#ifdef MOZ_WEBSPEECH
#  include "mozilla/hwinference/SpeechRecognitionParent.h"
#endif

namespace mozilla::hwinference {

LazyLogModule gHWInferenceLog("HWInference");
#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Error, fmt, ##__VA_ARGS__)

#ifdef MOZ_WEBSPEECH
ipc::IPCResult HWInferenceChild::RecvNewContentSpeechRecognition(
    Endpoint<hwinference::PSpeechRecognitionParent>&& aEndpoint,
    const dom::ContentParentId& aContentId) {
  LOGD("[{} - {}] Received connection request from content {}", fmt::ptr(this),
       __func__, static_cast<uint64_t>(aContentId));

  RefPtr<SpeechRecognitionParent> actor =
      new SpeechRecognitionParent(aContentId);
  if (!aEndpoint.Bind(actor)) {
    LOGE("[{} - {}] Failed to bind SpeechRecognitionParent for content {}",
         fmt::ptr(this), __func__, static_cast<uint64_t>(aContentId));
    return IPC_FAIL(this, "Failed to bind SpeechRecognitionParent");
  }

  LOGD("[{} - {}] Created SpeechRecognitionParent for content {}",
       fmt::ptr(this), __func__, static_cast<uint64_t>(aContentId));
  return IPC_OK();
}
#endif  // MOZ_WEBSPEECH

ipc::IPCResult HWInferenceChild::RecvNewTextGeneration(
    Endpoint<hwinference::PTextGenerationChild>&& aEndpoint,
    const ipc::FileDescriptor& aModel, const TextGenerationOptions& aOptions) {
  LOGD("[{} - {}]", fmt::ptr(this), __func__);

  RefPtr<TextGenerationChild> actor = new TextGenerationChild(aModel, aOptions);
  if (!aEndpoint.Bind(actor)) {
    LOGE("[{} - {}] Failed to bind TextGenerationChild", fmt::ptr(this),
         __func__);
    return IPC_FAIL(this, "Failed to bind TextGenerationChild");
  }
  actor->Initialize();
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

#undef LOGE
#undef LOGD
