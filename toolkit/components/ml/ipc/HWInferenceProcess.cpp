/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInferenceProcess.h"

#include "HWInferenceParent.h"
#include "mozilla/ClearOnShutdown.h"
#include "HWInferenceLog.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsThreadUtils.h"

namespace mozilla::hwinference {

#define LOGD(...) MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, __VA_ARGS__)

/* static */
HWInferenceProcess& HWInferenceProcess::Content() {
  AssertIsOnMainThread();
  static StaticAutoPtr<HWInferenceProcess> process;
  if (!process) {
    process = new HWInferenceProcess();
    ClearOnShutdown(&process);
  }
  return *process;
}

/* static */
HWInferenceProcess& HWInferenceProcess::Browser() {
  AssertIsOnMainThread();
  static StaticAutoPtr<HWInferenceProcess> process;
  if (!process) {
    process = new HWInferenceProcess();
    ClearOnShutdown(&process);
  }
  return *process;
}

HWInferenceProcess::~HWInferenceProcess() {
  if (mActor) {
    RetireActor(mActor);
  }
}

RefPtr<HWInferenceParent> HWInferenceProcess::Actor() const {
  AssertIsOnMainThread();
  return mActor;
}

bool HWInferenceProcess::IsUp() const {
  AssertIsOnMainThread();
  ipc::UtilityProcessKeepAlive* keepAlive = mKeepAlive.get();
  return keepAlive && keepAlive->IsAlive() && mActor;
}

already_AddRefed<ipc::UtilityProcessKeepAlive> HWInferenceProcess::Acquire() {
  AssertIsOnMainThread();

  if (IsUp()) {
    return do_AddRef(mKeepAlive.get());
  }

  // The keep-alive died before the actor heard about it. A crash destroys the
  // actor first, so an actor that can still send is going through a clean
  // shutdown; an unbound one is a launch that failed or was cancelled, whose
  // notification has not arrived yet.
  if (mActor) {
    LOGD("{} - retiring the actor", __func__);
    if (mActor->CanSend()) {
      mRestarts = 0;
      RetireActor(mActor);
    } else {
      OnLaunchFailed(mActor);
    }
  }

  // Only a relaunch is refused: a spent budget must not take a working process
  // away from its consumers.
  if (mRestarts >= StaticPrefs::browser_ml_hwinference_max_restarts()) {
    LOGD("{} - restart budget spent, not relaunching", __func__);
    return nullptr;
  }

  RefPtr<ipc::UtilityProcessManager> manager =
      ipc::UtilityProcessManager::GetSingleton();
  if (!manager) {
    return nullptr;
  }

  RefPtr<HWInferenceParent> actor = new HWInferenceParent();
  actor->mOwner = this;
  RefPtr<ipc::UtilityProcessKeepAlive> keepAlive =
      manager->LaunchIndependentHWInferenceProcess(actor);
  if (!keepAlive) {
    LOGD("{} - launch refused", __func__);
    actor->mOwner = nullptr;
    mRestarts++;
    return nullptr;
  }

  mActor = actor;
  mKeepAlive = keepAlive;
  return keepAlive.forget();
}

void HWInferenceProcess::OnActorDestroyed(
    HWInferenceParent* aActor, ipc::IProtocol::ActorDestroyReason aReason) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aActor == mActor);

  // The reason is PUtilityProcess's: NormalShutdown is a clean shutdown.
  mRestarts = aReason == ipc::IProtocol::NormalShutdown ? 0 : mRestarts + 1;
  RetireActor(aActor);
}

void HWInferenceProcess::OnLaunchFailed(HWInferenceParent* aActor) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aActor == mActor);

  // Without a keep-alive, every consumer let go: the launch was cancelled.
  if (mKeepAlive) {
    mRestarts++;
  }
  RetireActor(aActor);
}

void HWInferenceProcess::RetireActor(HWInferenceParent* aActor) {
  aActor->mOwner = nullptr;
  if (mActor == aActor) {
    mActor = nullptr;
  }
}

#undef LOGD

}  // namespace mozilla::hwinference
