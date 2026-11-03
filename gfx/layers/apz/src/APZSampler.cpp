/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/APZSampler.h"

#include "AsyncPanZoomController.h"
#include "TreeTraversal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/APZUtils.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {
namespace layers {

StaticMutex APZSampler::sWindowIdLock;
StaticAutoPtr<std::unordered_map<uint64_t, RefPtr<APZSampler>>>
    APZSampler::sWindowIdMap;

APZSampler::APZSampler(const RefPtr<APZCTreeManager>& aApz,
                       bool aIsUsingWebRender)
    : mApz(aApz),
      mIsUsingWebRender(aIsUsingWebRender),
      mThreadIdLock("APZSampler::mThreadIdLock"),
      mSampleTimeLock("APZSampler::mSampleTimeLock") {
  MOZ_ASSERT(aApz);
  mApz->SetSampler(this);
}

APZSampler::~APZSampler() { mApz->SetSampler(nullptr); }

void APZSampler::Destroy() {
  StaticMutexAutoLock lock(sWindowIdLock);
  if (mWindowId) {
    MOZ_ASSERT(sWindowIdMap);
    sWindowIdMap->erase(wr::AsUint64(*mWindowId));
  }
}

void APZSampler::SetWebRenderWindowId(const wr::WindowId& aWindowId) {
  StaticMutexAutoLock lock(sWindowIdLock);
  MOZ_ASSERT(!mWindowId);
  mWindowId = Some(aWindowId);
  if (!sWindowIdMap) {
    sWindowIdMap = new std::unordered_map<uint64_t, RefPtr<APZSampler>>();
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "APZSampler::ClearOnShutdown", [] { ClearOnShutdown(&sWindowIdMap); }));
  }
  (*sWindowIdMap)[wr::AsUint64(aWindowId)] = this;
}

/*static*/
void APZSampler::SetSamplerThread(const wr::WrWindowId& aWindowId) {
  if (RefPtr<APZSampler> sampler = GetSampler(aWindowId)) {
    MutexAutoLock lock(sampler->mThreadIdLock);
    sampler->mSamplerThreadId = Some(PlatformThread::CurrentId());
  }
}

/*static*/
void APZSampler::SampleForWebRender(const wr::WrWindowId& aWindowId,
                                    const uint64_t* aGeneratedFrameId,
                                    wr::Transaction* aTransaction) {
  if (RefPtr<APZSampler> sampler = GetSampler(aWindowId)) {
    wr::TransactionWrapper txn(aTransaction);
    Maybe<VsyncId> vsyncId =
        aGeneratedFrameId ? Some(VsyncId{*aGeneratedFrameId}) : Nothing();
    sampler->SampleForWebRender(vsyncId, txn);
  }
}

void APZSampler::SetSampleTime(const SampleTime& aSampleTime) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mSampleTimeLock);
  // This only gets called with WR, and the time provided is going to be
  // the time at which the current vsync interval ends. i.e. it is the timestamp
  // for the next vsync that will occur.
  mSampleTime = aSampleTime;
}

void APZSampler::SampleForWebRender(const Maybe<VsyncId>& aVsyncId,
                                    wr::TransactionWrapper& aTxn) {
  AssertOnSamplerThread();
  SampleTime sampleTime;
  {  // scope lock
    MutexAutoLock lock(mSampleTimeLock);

    // If mSampleTime is null we're in a startup phase where the
    // WebRenderBridgeParent hasn't yet provided us with a sample time.
    // If we're that early there probably aren't any APZ animations happening
    // anyway, so using Timestamp::Now() should be fine.
    SampleTime now = SampleTime::FromNow();
    sampleTime = mSampleTime.IsNull() ? now : mSampleTime;
  }
  mApz->SampleForWebRender(aVsyncId, aTxn, sampleTime);
}

AsyncTransform APZSampler::GetCurrentAsyncTransform(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    AsyncTransformComponents aComponents,
    const MutexAutoLock& aProofOfMapLock) const {
  MOZ_RELEASE_ASSERT(!CompositorThreadHolder::IsInCompositorThread());
  AssertOnSamplerThread();

  RefPtr<AsyncPanZoomController> apzc =
      mApz->GetTargetAPZC(aLayersId, aScrollId, aProofOfMapLock);
  if (!apzc) {
    // It's possible that this function can get called even after the target
    // APZC has been already destroyed because destroying the animation which
    // triggers this function call is basically processed later than the APZC,
    // i.e. queue mCompositorAnimationsToDelete in WebRenderBridgeParent and
    // then remove in WebRenderBridgeParent::RemoveEpochDataPriorTo.
    return AsyncTransform{};
  }

  return apzc->GetCurrentAsyncTransform(AsyncPanZoomController::eForCompositing,
                                        aComponents);
}

ParentLayerRect APZSampler::GetCompositionBounds(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const MutexAutoLock& aProofOfMapLock) const {
  AssertOnSamplerThread();

  RefPtr<AsyncPanZoomController> apzc =
      mApz->GetTargetAPZC(aLayersId, aScrollId, aProofOfMapLock);
  if (!apzc) {
    // It's possible that this function can get called even after the target
    // APZC has been already destroyed because destroying the animation which
    // triggers this function call is basically processed later than the APZC
    // one, i.e. queue mCompositorAnimationsToDelete in WebRenderBridgeParent
    // and then remove them in WebRenderBridgeParent::RemoveEpochDataPriorTo.
    return ParentLayerRect();
  }

  return apzc->GetCompositionBounds();
}

Maybe<APZSampler::ScrollOffsetAndRange>
APZSampler::GetCurrentScrollOffsetAndRange(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const MutexAutoLock& aProofOfMapLock) const {
  // Note: This is called from OMTA Sampler thread, or Compositor thread for
  // testing.

  RefPtr<AsyncPanZoomController> apzc =
      mApz->GetTargetAPZC(aLayersId, aScrollId, aProofOfMapLock);
  if (!apzc) {
    return Nothing();
  }

  return Some(ScrollOffsetAndRange{
      // FIXME: Use the one-frame delayed offset now. This doesn't take
      // scroll-linked effets into accounts, so we have to fix this in the
      // future.
      apzc->GetCurrentAsyncVisualViewport(
              AsyncTransformConsumer::eForCompositing)
          .TopLeft(),
      apzc->GetCurrentScrollRangeInCssPixels()});
}

void APZSampler::AssertOnSamplerThread() const {
  if (APZThreadUtils::GetThreadAssertionsEnabled()) {
    MOZ_ASSERT(IsSamplerThread());
  }
}

bool APZSampler::IsSamplerThread() const {
  if (mIsUsingWebRender) {
    // If the sampler thread id isn't set yet then we cannot be running on the
    // sampler thread (because we will have the thread id before we run any
    // other C++ code on it, and this function is only ever invoked from C++
    // code), so return false in that scenario.
    MutexAutoLock lock(mThreadIdLock);
    return mSamplerThreadId && PlatformThread::CurrentId() == *mSamplerThreadId;
  }
  return CompositorThreadHolder::IsInCompositorThread();
}

/*static*/
already_AddRefed<APZSampler> APZSampler::GetSampler(
    const wr::WrWindowId& aWindowId) {
  RefPtr<APZSampler> sampler;
  StaticMutexAutoLock lock(sWindowIdLock);
  if (sWindowIdMap) {
    auto it = sWindowIdMap->find(wr::AsUint64(aWindowId));
    if (it != sWindowIdMap->end()) {
      sampler = it->second;
    }
  }
  return sampler.forget();
}

}  // namespace layers
}  // namespace mozilla

void apz_register_sampler(mozilla::wr::WrWindowId aWindowId) {
  mozilla::layers::APZSampler::SetSamplerThread(aWindowId);
}

void apz_sample_transforms(mozilla::wr::WrWindowId aWindowId,
                           const uint64_t* aGeneratedFrameId,
                           mozilla::wr::Transaction* aTransaction) {
  mozilla::layers::APZSampler::SampleForWebRender(aWindowId, aGeneratedFrameId,
                                                  aTransaction);
}

void apz_deregister_sampler(mozilla::wr::WrWindowId aWindowId) {}
