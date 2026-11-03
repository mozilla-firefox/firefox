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
#include "mozilla/llama/LlamaBackend.h"
#include "mozilla/llama/LlamaRuntimeLinker.h"
#include "nsThreadUtils.h"

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

static nsTArray<DeviceDescriptor> ListDevices() {
  nsTArray<DeviceDescriptor> devices;
  llama::LlamaLibWrapper* lib = llama::LlamaRuntimeLinker::Get();
  if (!lib) {
    LOGE("ListDevices - llama.cpp is not available");
    return devices;
  }
  for (const llama::Device& device : llama::Devices(lib)) {
    DeviceDescriptor* descriptor = devices.AppendElement();
    descriptor->id() = device.mId;
    descriptor->description() = device.mDescription;
    descriptor->kind() = device.mKind;
    descriptor->backends().AppendElement(dom::HWInferenceBackend::Llama_cpp);
  }
  return devices;
}

ipc::IPCResult HWInferenceChild::RecvListDevices(
    ListDevicesResolver&& aResolver) {
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  // The first call enumerates, which initialises every backend: keep that
  // off the thread the actors live on.
  nsCOMPtr<nsISerialEventTarget> actorThread = GetCurrentSerialEventTarget();
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "HWInferenceChild::ListDevices",
      [actorThread, resolve = std::move(aResolver)]() mutable {
        nsTArray<DeviceDescriptor> devices = ListDevices();
        MOZ_ALWAYS_SUCCEEDS(actorThread->Dispatch(NS_NewRunnableFunction(
            "HWInferenceChild::ListDevicesReply",
            [resolve = std::move(resolve),
             devices = std::move(devices)]() mutable { resolve(devices); })));
      })));
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
