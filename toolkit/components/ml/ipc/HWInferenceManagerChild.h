/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERCHILD_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERCHILD_H_

#include "mozilla/hwinference/PHWInferenceManagerChild.h"
#include "nsRefPtrHashtable.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/EventTargetCapability.h"

namespace mozilla::hwinference {

// Content process side
class HWInferenceManagerChild final : public PHWInferenceManagerChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HWInferenceManagerChild, override);

  HWInferenceManagerChild() = default;

  // Binds aEndpoint as the singleton connection. Each endpoint corresponds to a
  // keepalive for the HWInference process, which will be released here if the
  // bind fails (e.g. due to a concurrent request already connected), or in
  // ActorDestroy.
  static void OpenForProcess(Endpoint<PHWInferenceManagerChild>&& aEndpoint);

  static RefPtr<HWInferenceManagerChild> GetSingleton();

  void ActorDestroy(ActorDestroyReason aReason) override;

 private:
  ~HWInferenceManagerChild() = default;

  // Binds aEndpoint as the singleton connection unless there already is a live
  // one. Returns whether it was adopted.
  static bool AdoptEndpoint(Endpoint<PHWInferenceManagerChild>&& aEndpoint);

  static void ReleaseConnectionReference();

  static StaticRefPtr<HWInferenceManagerChild> sSingleton
      MOZ_GUARDED_BY(sSingletonMutex);
  static StaticMutex sSingletonMutex;
};

}  // namespace mozilla::hwinference

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERCHILD_H_
