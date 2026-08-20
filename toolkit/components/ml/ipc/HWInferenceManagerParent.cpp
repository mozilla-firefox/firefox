/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInferenceManagerParent.h"
#include "mozilla/Logging.h"
#include "mozilla/ipc/Endpoint.h"

namespace mozilla::hwinference {

extern LazyLogModule gHWInferenceLog;
#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Error, fmt, ##__VA_ARGS__)

HWInferenceManagerParent::HWInferenceManagerParent(
    dom::ContentParentId aContentId)
    : mContentId(aContentId) {
  LOGD("{} for content {}", __func__, static_cast<int>(mContentId));
}

/* static */
bool HWInferenceManagerParent::CreateForContent(
    Endpoint<PHWInferenceManagerParent>&& aEndpoint,
    dom::ContentParentId aContentId) {
  LOGD("HWInferenceManagerParent::CreateForContent for content {}",
       static_cast<int>(aContentId));

  RefPtr<HWInferenceManagerParent> parent =
      new HWInferenceManagerParent(aContentId);

  if (!aEndpoint.Bind(parent)) {
    LOGE(
        "HWInferenceManagerParent::CreateForContent - Failed to bind endpoint");
    return false;
  }

  return true;
}

void HWInferenceManagerParent::ActorDestroy(ActorDestroyReason aReason) {
  LOGD("[{}] HWInferenceManagerParent::ActorDestroy reason={}", (void*)this,
       static_cast<int>(aReason));
}

}  // namespace mozilla::hwinference

#undef LOGD
#undef LOGE
