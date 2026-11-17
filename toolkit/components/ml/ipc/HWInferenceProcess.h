/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPROCESS_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPROCESS_H_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/ipc/ProtocolUtils.h"

namespace mozilla::ipc {
class UtilityProcessKeepAlive;
}  // namespace mozilla::ipc

namespace mozilla::hwinference {

class HWInferenceParent;

// One HWInference process shared by a class of consumers. Acquire() launches
// it or returns the live keep-alive; the process dies with the last keep-alive.
// Main thread only.
class HWInferenceProcess final {
 public:
  // Separate processes, so that content cannot reach browser data.
  static HWInferenceProcess& Content();
  static HWInferenceProcess& Browser();

  HWInferenceProcess() = default;
  ~HWInferenceProcess();

  // Null past the restart budget and during shutdown. Actor() is set on
  // success.
  already_AddRefed<ipc::UtilityProcessKeepAlive> Acquire();

  // Null when no process is up or launching.
  RefPtr<HWInferenceParent> Actor() const;

  bool IsUp() const;

 private:
  friend class HWInferenceParent;

  void OnActorDestroyed(HWInferenceParent* aActor,
                        ipc::IProtocol::ActorDestroyReason aReason);

  void OnLaunchFailed(HWInferenceParent* aActor);

  void RetireActor(HWInferenceParent* aActor);

  RefPtr<HWInferenceParent> mActor;
  WeakPtr<ipc::UtilityProcessKeepAlive> mKeepAlive;

  // Crashes and failed launches since the last clean shutdown.
  uint32_t mRestarts = 0;
};

}  // namespace mozilla::hwinference

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEPROCESS_H_
