/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/OMTASampler.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/layers/CompositorAnimationStorage.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/OMTAController.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {
namespace layers {

StaticMutex OMTASampler::sWindowIdLock;
StaticAutoPtr<std::unordered_map<uint64_t, RefPtr<OMTASampler>>>
    OMTASampler::sWindowIdMap;

OMTASampler::OMTASampler(const RefPtr<CompositorAnimationStorage>& aAnimStorage,
                         LayersId aRootLayersId)
    : mAnimStorage(aAnimStorage),
      mStorageLock("OMTASampler::mStorageLock"),
      mThreadIdLock("OMTASampler::mThreadIdLock"),
      mSampleTimeLock("OMTASampler::mSampleTimeLock"),
      mIsInTestMode(false) {
  mController = new OMTAController(aRootLayersId);
}

void OMTASampler::Destroy() {
  StaticMutexAutoLock lock(sWindowIdLock);
  if (mWindowId) {
    MOZ_ASSERT(sWindowIdMap);
    sWindowIdMap->erase(wr::AsUint64(*mWindowId));
  }
}

void OMTASampler::SetWebRenderWindowId(const wr::WrWindowId& aWindowId) {
  StaticMutexAutoLock lock(sWindowIdLock);
  MOZ_ASSERT(!mWindowId);
  mWindowId = Some(aWindowId);
  if (!sWindowIdMap) {
    sWindowIdMap = new std::unordered_map<uint64_t, RefPtr<OMTASampler>>();
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("OMTASampler::ClearOnShutdown",
                               [] { ClearOnShutdown(&sWindowIdMap); }));
  }
  (*sWindowIdMap)[wr::AsUint64(aWindowId)] = this;
}

/*static*/
void OMTASampler::SetSamplerThread(const wr::WrWindowId& aWindowId) {
  if (RefPtr<OMTASampler> sampler = GetSampler(aWindowId)) {
    MutexAutoLock lock(sampler->mThreadIdLock);
    sampler->mSamplerThreadId = Some(PlatformThread::CurrentId());
  }
}

/*static*/
void OMTASampler::Sample(const wr::WrWindowId& aWindowId,
                         wr::Transaction* aTransaction) {
  if (RefPtr<OMTASampler> sampler = GetSampler(aWindowId)) {
    wr::TransactionWrapper txn(aTransaction);
    sampler->Sample(txn);
  }
}

void OMTASampler::SetSampleTime(const TimeStamp& aSampleTime) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  const bool hasAnimations = HasAnimations();

  MutexAutoLock lock(mSampleTimeLock);

  // Reset the previous time stamp if we don't already have any running
  // animations to avoid using the time which is far behind for newly
  // started animations.
  mPreviousSampleTime = hasAnimations ? std::move(mSampleTime) : TimeStamp();
  mSampleTime = aSampleTime;
}

void OMTASampler::ResetPreviousSampleTime() {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mSampleTimeLock);

  mPreviousSampleTime = TimeStamp();
}

void OMTASampler::Sample(wr::TransactionWrapper& aTxn) {
  MOZ_RELEASE_ASSERT(IsSamplerThread());

  // If we are in test mode, don't sample with the current time stamp, it will
  // skew cached animation values.
  if (mIsInTestMode) {
    return;
  }

  TimeStamp sampleTime;
  TimeStamp previousSampleTime;
  {  // scope lock
    MutexAutoLock lock(mSampleTimeLock);

    // If mSampleTime is null we're in a startup phase where the
    // WebRenderBridgeParent hasn't yet provided us with a sample time.
    // If we're that early there probably aren't any OMTA animations happening
    // anyway, so using Timestamp::Now() should be fine.
    sampleTime = mSampleTime.IsNull() ? TimeStamp::Now() : mSampleTime;
    previousSampleTime = mPreviousSampleTime;
  }

  WrAnimations animations = SampleAnimations(previousSampleTime, sampleTime);

  aTxn.AppendDynamicProperties(animations.mOpacityArrays,
                               animations.mTransformArrays,
                               animations.mColorArrays);
}

WrAnimations OMTASampler::SampleAnimations(const TimeStamp& aPreviousSampleTime,
                                           const TimeStamp& aSampleTime) {
  MOZ_RELEASE_ASSERT(IsSamplerThread());

  MutexAutoLock lock(mStorageLock);

  mAnimStorage->SampleAnimations(mController, aPreviousSampleTime, aSampleTime);

  return mAnimStorage->CollectWebRenderAnimations();
}

OMTAValue OMTASampler::GetOMTAValue(const uint64_t& aId) const {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mStorageLock);

  return mAnimStorage->GetOMTAValue(aId);
}

void OMTASampler::SampleForTesting(const Maybe<TimeStamp>& aTestingSampleTime) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  TimeStamp sampleTime;
  TimeStamp previousSampleTime;
  {  // scope lock
    MutexAutoLock timeLock(mSampleTimeLock);
    if (aTestingSampleTime) {
      // If we are on testing refresh mode, use the testing time stamp for both
      // of the previous sample time and the current sample time since unlike
      // normal refresh mode, the testing mode animations on the compositor are
      // synchronously composed, so we don't need to worry about the time gap
      // between the main thread and compositor thread.
      sampleTime = *aTestingSampleTime;
      previousSampleTime = *aTestingSampleTime;
    } else {
      sampleTime = mSampleTime;
      previousSampleTime = mPreviousSampleTime;
    }
  }

  MutexAutoLock storageLock(mStorageLock);
  mAnimStorage->SampleAnimations(mController, previousSampleTime, sampleTime);
}

void OMTASampler::SetAnimations(
    uint64_t aId, const LayersId& aLayersId,
    const nsTArray<layers::Animation>& aAnimations) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mStorageLock);
  MutexAutoLock timeLock(mSampleTimeLock);
  mAnimStorage->SetAnimations(aId, aLayersId, aAnimations, mPreviousSampleTime);
}

bool OMTASampler::HasAnimations() const {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mStorageLock);

  return mAnimStorage->HasAnimations();
}

void OMTASampler::ClearActiveAnimations(
    std::unordered_map<uint64_t, wr::WrEpoch>& aActiveAnimations) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mStorageLock);
  for (const auto& id : aActiveAnimations) {
    mAnimStorage->ClearById(id.first);
  }
}

void OMTASampler::RemoveEpochDataPriorTo(
    std::queue<CompositorAnimationIdsForEpoch>& aCompositorAnimationsToDelete,
    std::unordered_map<uint64_t, wr::WrEpoch>& aActiveAnimations,
    const wr::WrEpoch& aRenderedEpoch) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MutexAutoLock lock(mStorageLock);

  while (!aCompositorAnimationsToDelete.empty()) {
    if (aRenderedEpoch < aCompositorAnimationsToDelete.front().mEpoch) {
      break;
    }
    for (uint64_t id : aCompositorAnimationsToDelete.front().mIds) {
      const auto activeAnim = aActiveAnimations.find(id);
      if (activeAnim == aActiveAnimations.end()) {
        NS_ERROR("Tried to delete invalid animation");
        continue;
      }
      // Check if animation delete request is still valid.
      if (activeAnim->second <= aCompositorAnimationsToDelete.front().mEpoch) {
        mAnimStorage->ClearById(id);
        aActiveAnimations.erase(activeAnim);
      }
    }
    aCompositorAnimationsToDelete.pop();
  }
}

bool OMTASampler::IsSamplerThread() const {
  MutexAutoLock lock(mThreadIdLock);
  return mSamplerThreadId && PlatformThread::CurrentId() == *mSamplerThreadId;
}

/*static*/
already_AddRefed<OMTASampler> OMTASampler::GetSampler(
    const wr::WrWindowId& aWindowId) {
  RefPtr<OMTASampler> sampler;
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

void omta_register_sampler(mozilla::wr::WrWindowId aWindowId) {
  mozilla::layers::OMTASampler::SetSamplerThread(aWindowId);
}

void omta_sample(mozilla::wr::WrWindowId aWindowId,
                 mozilla::wr::Transaction* aTransaction) {
  mozilla::layers::OMTASampler::Sample(aWindowId, aTransaction);
}

void omta_deregister_sampler(mozilla::wr::WrWindowId aWindowId) {}
