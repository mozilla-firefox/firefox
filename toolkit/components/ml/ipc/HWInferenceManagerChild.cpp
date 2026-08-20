/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInferenceManagerChild.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/StaticPtr.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla::hwinference {

extern LazyLogModule gHWInferenceLog;
#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Error, fmt, ##__VA_ARGS__)

StaticRefPtr<HWInferenceManagerChild> HWInferenceManagerChild::sSingleton;
StaticMutex HWInferenceManagerChild::sSingletonMutex;

/* static */
void HWInferenceManagerChild::ReleaseConnectionReference() {
  // Only a content process ever asks for a connection over PContent, so only a
  // content process has a keep-alive to hand back.
  MOZ_ASSERT(!XRE_IsParentProcess());

  auto release = []() {
    if (dom::ContentChild* contentChild = dom::ContentChild::GetSingleton()) {
      (void)contentChild->SendReleaseHWInferenceConnection();
    }
  };

  if (NS_IsMainThread()) {
    release();
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "HWInferenceManagerChild::ReleaseConnectionReference", release));
  }
}

/* static */
bool HWInferenceManagerChild::AdoptEndpoint(
    Endpoint<PHWInferenceManagerChild>&& aEndpoint) {
  StaticMutexAutoLock lock(sSingletonMutex);

  if (sSingleton && sSingleton->CanSend()) {
    LOGD("OpenForProcess - Already have active singleton, reusing");
    return false;
  }

  sSingleton = nullptr;

  if (!aEndpoint.IsValid()) {
    LOGE("OpenForProcess - ERROR: Invalid endpoint received");
    return false;
  }

  LOGD("Creating new manager and binding endpoint");
  RefPtr<HWInferenceManagerChild> manager = new HWInferenceManagerChild();
  if (!aEndpoint.Bind(manager)) {
    LOGE("OpenForProcess - ERROR: Failed to bind endpoint");
    return false;
  }

  sSingleton = manager;
  LOGD("Successfully bound endpoint, connection ready");
  return true;
}

/* static */
void HWInferenceManagerChild::OpenForProcess(
    Endpoint<PHWInferenceManagerChild>&& aEndpoint) {
  LOGD("{} - Opening connection to utility process", __func__);

  // Requesting this endpoint made the parent take a keep-alive on the
  // HWInference process for this content process. The endpoint we adopt holds
  // it until ActorDestroy; any other has to hand it back now.
  if (!AdoptEndpoint(std::move(aEndpoint))) {
    ReleaseConnectionReference();
  }
}

/* static */
RefPtr<HWInferenceManagerChild> HWInferenceManagerChild::GetSingleton() {
  StaticMutexAutoLock lock(sSingletonMutex);
  return sSingleton;
}

void HWInferenceManagerChild::ActorDestroy(ActorDestroyReason aReason) {
  LOGD("{} reason={}, clearing singleton", __func__, static_cast<int>(aReason));

  {
    StaticMutexAutoLock lock(sSingletonMutex);
    // AdoptEndpoint() replaces a singleton that can no longer send, so an
    // ActorDestroy arriving after that must not clear its replacement.
    if (sSingleton == this) {
      sSingleton = nullptr;
    }
  }

  // Outside the lock: this dispatches to the main thread.
  ReleaseConnectionReference();
}

}  // namespace mozilla::hwinference

#undef LOGD
#undef LOGE
