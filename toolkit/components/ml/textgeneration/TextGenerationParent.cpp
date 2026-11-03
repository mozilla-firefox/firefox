/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextGenerationParent.h"

#include "mozilla/StaticPrefs_browser.h"
#include "nsThreadUtils.h"

#include "mozilla/hwinference/HWInferenceProcess.h"
#include "mozilla/hwinference/HWInferenceLog.h"
#include "mozilla/hwinference/HWInferenceParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/ml/MLProfilerMarkers.h"

namespace mozilla::hwinference {

#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, fmt, ##__VA_ARGS__)

TextGenerationParent::TextGenerationParent(
    RefPtr<ipc::UtilityProcessKeepAlive> aKeepAlive, bool aProcessReused)
    : mKeepAlive(std::move(aKeepAlive)), mProcessReused(aProcessReused) {}

TextGenerationParent::~TextGenerationParent() = default;

/* static */
already_AddRefed<TextGenerationParent> TextGenerationParent::Create(
    const ipc::FileDescriptor& aModel, const TextGenerationOptions& aOptions) {
  AssertIsOnMainThread();
  const bool processReused = HWInferenceProcess::Browser().IsUp();
  const TimeStamp spawnStart = TimeStamp::Now();
  RefPtr<ipc::UtilityProcessKeepAlive> keepAlive =
      HWInferenceProcess::Browser().Acquire();
  if (!keepAlive) {
    LOGD("{} - the browser HWInference process failed to launch", __func__);
    PROFILER_MARKER(ML_HWINFERENCE_PROCESS_TRACK, ML_SETUP,
                    MarkerTiming::IntervalUntilNowFrom(spawnStart),
                    MLFailedMarker, "process spawn"_ns, "launch refused"_ns);
    return nullptr;
  }
  RefPtr<HWInferenceParent> process = HWInferenceProcess::Browser().Actor();
  if (!processReused) {
    process->WhenReady()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [spawnStart]() {
          PROFILER_MARKER(ML_HWINFERENCE_PROCESS_TRACK, ML_SETUP,
                          MarkerTiming::IntervalUntilNowFrom(spawnStart),
                          MLProcessSpawnMarker,
                          (TimeStamp::Now() - spawnStart).ToMilliseconds());
        },
        [spawnStart]() {
          PROFILER_MARKER(ML_HWINFERENCE_PROCESS_TRACK, ML_SETUP,
                          MarkerTiming::IntervalUntilNowFrom(spawnStart),
                          MLFailedMarker, "process spawn"_ns,
                          "process never came up"_ns);
        });
  }

  ipc::Endpoint<PTextGenerationParent> parentEnd;
  ipc::Endpoint<PTextGenerationChild> childEnd;
  MOZ_ALWAYS_SUCCEEDS(PTextGeneration::CreateEndpoints(&parentEnd, &childEnd));
  RefPtr<TextGenerationParent> generator =
      new TextGenerationParent(std::move(keepAlive), processReused);
  MOZ_ALWAYS_TRUE(parentEnd.Bind(generator));
  process->StartTextGeneration(std::move(childEnd), aModel, aOptions);
  return generator.forget();
}

RefPtr<TextGenerationParent::ReadyPromise> TextGenerationParent::WhenReady() {
  if (mLoadMs) {
    return ReadyPromise::CreateAndResolve(*mLoadMs, __func__);
  }
  if (mLoadFailure) {
    return ReadyPromise::CreateAndReject(*mLoadFailure, __func__);
  }
  return mReadyPromise.Ensure(__func__);
}

ipc::IPCResult TextGenerationParent::RecvReady(const LoadResult& aResult) {
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  if (aResult.type() == LoadResult::TLoadSuccess) {
    mLoadMs = Some(aResult.get_LoadSuccess().loadMs());
    mReadyPromise.ResolveIfExists(*mLoadMs, __func__);
  } else {
    mLoadFailure = Some(LoadFailure{LoadFailure::Cause::Backend,
                                    aResult.get_LoadError().message()});
    mReadyPromise.RejectIfExists(*mLoadFailure, __func__);
  }
  return IPC_OK();
}

ipc::IPCResult TextGenerationParent::RecvDelta(const nsCString& aText) {
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  // Keep the callable alive while it's being used.
  const DeltaHandler handler = mDeltaHandler;
  if (handler) {
    handler(aText);
  }
  return IPC_OK();
}

static void DropKeepAlive(RefPtr<ipc::UtilityProcessKeepAlive>&& aKeepAlive,
                          TimeStamp aReleased, uint32_t aGraceMs) {
  WeakPtr<ipc::UtilityProcessKeepAlive> weak = aKeepAlive.get();
  aKeepAlive = nullptr;
  const bool retired = !weak.get();
  LOGD("DropKeepAlive - keep-alive dropped, process {}",
       retired ? "retired" : "still in use");
  PROFILER_MARKER(ML_HWINFERENCE_PROCESS_TRACK, ML_SETUP,
                  MarkerTiming::IntervalUntilNowFrom(aReleased),
                  MLProcessReleaseMarker, aGraceMs, retired);
}

static void ReleaseAfterIdleGrace(
    RefPtr<ipc::UtilityProcessKeepAlive>&& aKeepAlive) {
  AssertIsOnMainThread();

  const TimeStamp released = TimeStamp::Now();
  const uint32_t graceMs =
      StaticPrefs::browser_ml_hwinference_browser_idle_shutdown_grace_ms();
  if (!graceMs) {
    DropKeepAlive(std::move(aKeepAlive), released, graceMs);
    return;
  }

  LOGD("{} - dropping the keep-alive in {}ms", __func__, graceMs);
  // The runnable owns the keep-alive until the grace expires or shutdown
  // discards pending main-thread work.
  NS_DelayedDispatchToCurrentThread(
      NS_NewRunnableFunction(
          "hwinference::TextGenerationParent::ReleaseAfterIdleGrace",
          [keepAlive = std::move(aKeepAlive), released, graceMs]() mutable {
            DropKeepAlive(std::move(keepAlive), released, graceMs);
          }),
      graceMs);
}

void TextGenerationParent::ActorDestroy(ActorDestroyReason aReason) {
  LOGD("[{} - {}]", fmt::ptr(this), __func__);
  if (mLoadMs.isNothing() && mLoadFailure.isNothing()) {
    mLoadFailure = Some(LoadFailure{LoadFailure::Cause::ActorGone,
                                    "TextGenerator: actor destroyed"_ns});
    mReadyPromise.RejectIfExists(*mLoadFailure, __func__);
  }
  ReleaseAfterIdleGrace(std::move(mKeepAlive));
}

#undef LOGD

}  // namespace mozilla::hwinference
