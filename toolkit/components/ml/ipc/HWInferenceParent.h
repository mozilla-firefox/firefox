/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPARENT_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPARENT_H_

#include "mozilla/MozPromise.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/hwinference/PHWInferenceParent.h"
#include "mozilla/ipc/UtilityMediaService.h"

namespace mozilla::hwinference {

class HWInferenceProcess;

// Main-process side of PHWInference, bound to one process for its lifetime.
class HWInferenceParent final : public PHWInferenceParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HWInferenceParent, override);

  HWInferenceParent() = default;

  void ActorDestroy(ActorDestroyReason aReason) override;

  mozilla::ipc::IPCResult RecvIsModelAvailable(
      nsCString&& aTask, nsCString&& aId, IsModelAvailableResolver&& aResolver);

  mozilla::ipc::IPCResult RecvIsModelInstalled(
      nsCString&& aTask, nsCString&& aId, IsModelInstalledResolver&& aResolver);

  mozilla::ipc::IPCResult RecvInstallModel(
      nsCString&& aTask, nsCString&& aId, uint64_t aInnerWindowId,
      const dom::ContentParentId& aContentId, InstallModelResolver&& aResolver);

  mozilla::ipc::IPCResult RecvGetModelFile(nsCString&& aTask, nsCString&& aId,
                                           GetModelFileResolver&& aResolver);

  ipc::UtilityActorName GetActorName() {
    return ipc::UtilityActorName::HwInference;
  }

  // Called by UtilityProcessManager once the process is up, or with null if it
  // failed to launch.
  nsresult BindToUtilityProcess(
      const RefPtr<ipc::UtilityProcessParent>& aUtilityParent);

  // Resolved once bound, rejected if the process dies or never comes up.
  RefPtr<GenericNonExclusivePromise> WhenReady() { return mReadyPromise; }

#ifdef MOZ_WEBSPEECH
  // Sends aEndpoint once bound. The caller must hold a keep-alive.
  void StartContentSpeechRecognition(
      Endpoint<PSpeechRecognitionParent>&& aEndpoint,
      dom::ContentParentId aChildId);
#endif

  // Same for a text generator; aModel is the model file the parent opened.
  void StartTextGeneration(Endpoint<PTextGenerationChild>&& aEndpoint,
                           const ipc::FileDescriptor& aModel,
                           const TextGenerationOptions& aOptions);

 private:
  friend PHWInferenceParent;
  friend class HWInferenceProcess;

  ~HWInferenceParent() = default;

  // Runs aSend(*this) once bound, drops it if the process never comes up.
  template <typename Send>
  void SendWhenReady(Send&& aSend);

  // Cleared once the owner has taken this actor's last report.
  HWInferenceProcess* mOwner = nullptr;

  const RefPtr<GenericNonExclusivePromise::Private> mReadyPromise =
      new GenericNonExclusivePromise::Private(
          "HWInferenceParent::mReadyPromise");
};

}  // namespace mozilla::hwinference

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPARENT_H_
