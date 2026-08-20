/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCECHILD_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCECHILD_H_

#include "mozilla/ipc/Endpoint.h"
#include "mozilla/hwinference/PHWInferenceChild.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/UtilityMediaService.h"
#include "mozilla/dom/ipc/IdType.h"

namespace mozilla::hwinference {

class HWInferenceChild final : public PHWInferenceChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HWInferenceChild, override);

  HWInferenceChild() = default;

  void Shutdown();

  mozilla::ipc::IPCResult RecvNewContentHWInferenceManager(
      Endpoint<hwinference::PHWInferenceManagerParent>&& aEndpoint,
      const dom::ContentParentId& aContentId);

  RefPtr<IsModelAvailablePromise> SendIsModelAvailable(const nsCString& aTask,
                                                       const nsCString& aId);

  RefPtr<IsModelInstalledPromise> SendIsModelInstalled(const nsCString& aTask,
                                                       const nsCString& aId);

  RefPtr<InstallModelPromise> SendInstallModel(
      const nsCString& aTask, const nsCString& aId, uint64_t aInnerWindowId,
      const dom::ContentParentId& aContentId);

  RefPtr<GetModelFilePromise> SendGetModelFile(const nsCString& aTask,
                                               const nsCString& aId);

  ipc::UtilityActorName GetActorName() {
    return ipc::UtilityActorName::HwInference;
  }

 private:
  friend PHWInferenceChild;
  ~HWInferenceChild() = default;
};

}  // namespace mozilla::hwinference

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCECHILD_H_
