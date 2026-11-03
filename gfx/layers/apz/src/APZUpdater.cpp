/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/APZUpdater.h"

#include "APZCTreeManager.h"
#include "AsyncPanZoomController.h"
#include "base/task.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/layers/WebRenderScrollDataWrapper.h"
#include "mozilla/webrender/WebRenderAPI.h"

namespace mozilla {
namespace layers {

StaticMutex APZUpdater::sWindowIdLock;
StaticAutoPtr<std::unordered_map<uint64_t, APZUpdater*>>
    APZUpdater::sWindowIdMap;

APZUpdater::APZUpdater(const RefPtr<APZCTreeManager>& aApz,
                       bool aConnectedToWebRender)
    : mApz(aApz),
      mDestroyed(false),
      mConnectedToWebRender(aConnectedToWebRender),
      mThreadIdLock("APZUpdater::ThreadIdLock"),
      mQueueLock("APZUpdater::QueueLock") {
  MOZ_ASSERT(aApz);
  mApz->SetUpdater(this);
}

APZUpdater::~APZUpdater() {
  mApz->SetUpdater(nullptr);

  StaticMutexAutoLock lock(sWindowIdLock);
  if (mWindowId) {
    MOZ_ASSERT(sWindowIdMap);
    // Ensure that ClearTree was called and the task got run
    MOZ_ASSERT(sWindowIdMap->find(wr::AsUint64(*mWindowId)) ==
               sWindowIdMap->end());
  }
}

bool APZUpdater::HasTreeManager(const RefPtr<APZCTreeManager>& aApz) {
  return aApz.get() == mApz.get();
}

void APZUpdater::SetWebRenderWindowId(const wr::WindowId& aWindowId) {
  StaticMutexAutoLock lock(sWindowIdLock);
  MOZ_ASSERT(!mWindowId);
  mWindowId = Some(aWindowId);
  if (!sWindowIdMap) {
    sWindowIdMap = new std::unordered_map<uint64_t, APZUpdater*>();
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "APZUpdater::ClearOnShutdown", [] { ClearOnShutdown(&sWindowIdMap); }));
  }
  (*sWindowIdMap)[wr::AsUint64(aWindowId)] = this;
}

/*static*/
void APZUpdater::SetUpdaterThread(const wr::WrWindowId& aWindowId) {
  if (RefPtr<APZUpdater> updater = GetUpdater(aWindowId)) {
    MutexAutoLock lock(updater->mThreadIdLock);
    updater->mUpdaterThreadId = Some(PlatformThread::CurrentId());
  }
}

// Takes a conditional lock!
/*static*/
void APZUpdater::PrepareForSceneSwap(const wr::WrWindowId& aWindowId)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  if (RefPtr<APZUpdater> updater = GetUpdater(aWindowId)) {
    updater->mApz->LockTree();
  }
}

// Assumes we took a conditional lock!
/*static*/
void APZUpdater::CompleteSceneSwap(const wr::WrWindowId& aWindowId,
                                   const wr::WrPipelineInfo& aInfo) {
  RefPtr<APZUpdater> updater = GetUpdater(aWindowId);
  if (!updater) {
    // This should only happen in cases where PrepareForSceneSwap also got a
    // null updater. No updater-thread tasks get run between PrepareForSceneSwap
    // and this function, so there is no opportunity for the updater mapping
    // to have gotten removed from sWindowIdMap in between the two calls.
    return;
  }
  updater->mApz->mTreeLock.AssertCurrentThreadIn();

  for (const auto& removedPipeline : aInfo.removed_pipelines) {
    LayersId layersId = wr::AsLayersId(removedPipeline.pipeline_id);
    updater->mEpochData.erase(layersId);
  }
  // Reset the built info for all pipelines, then put it back for the ones
  // that got built in this scene swap.
  for (auto& i : updater->mEpochData) {
    i.second.mBuilt = Nothing();
  }
  for (const auto& epoch : aInfo.epochs) {
    LayersId layersId = wr::AsLayersId(epoch.pipeline_id);
    updater->mEpochData[layersId].mBuilt = Some(epoch.epoch);
  }

  // Run any tasks that got unblocked, then unlock the tree. The order is
  // important because we want to run all the tasks up to and including the
  // UpdateHitTestingTree calls corresponding to the built epochs, and we
  // want to run those before we release the lock (i.e. atomically with the
  // scene swap). This ensures that any hit-tests always encounter a consistent
  // state between the APZ tree and the built scene in WR.
  //
  // While we could add additional information to the queued tasks to figure
  // out the minimal set of tasks we want to run here, it's easier and harmless
  // to just run all the queued and now-unblocked tasks inside the lock.
  //
  // Note that the ProcessQueue here might remove the window id -> APZUpdater
  // mapping from sWindowIdMap, but we still unlock the tree successfully to
  // leave things in a good state.
  updater->ProcessQueue();

  updater->mApz->UnlockTree();
}

/*static*/
void APZUpdater::ProcessPendingTasks(const wr::WrWindowId& aWindowId) {
  if (RefPtr<APZUpdater> updater = GetUpdater(aWindowId)) {
    updater->ProcessQueue();
  }
}

void APZUpdater::ClearTree(LayersId aRootLayersId) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZUpdater> self = this;
  RefPtr<Runnable> task =
      NS_NewRunnableFunction("APZUpdater::ClearTree", [=]() {
        self->mApz->ClearTree();
        self->mDestroyed = true;

        // Once ClearTree is called on the APZCTreeManager, we are in a
        // shutdown phase. After this point it's ok if WebRender cannot get a
        // hold of the updater via the window id, and it's a good point to
        // remove the mapping and avoid leaving a dangling pointer to this
        // object.
        StaticMutexAutoLock lock(sWindowIdLock);
        if (self->mWindowId) {
          MOZ_ASSERT(sWindowIdMap);
          sWindowIdMap->erase(wr::AsUint64(*(self->mWindowId)));
        }
      });

  // If the updater (scene builder) thread was never established then a task
  // queued via RunOnUpdaterThread can never be drained (WakeSceneBuilder has
  // no WebRenderAPI to wake), and the task holds a strong reference to this
  // APZUpdater, leaking the whole tree. Run the task directly on the compositor
  // thread in that case.
  if (!HasUpdaterThread()) {
    task->Run();
    return;
  }

  RunOnUpdaterThread(aRootLayersId, task.forget(), DuringShutdown::Yes);
}

void APZUpdater::UpdateFocusState(LayersId aRootLayerTreeId,
                                  LayersId aOriginatingLayersId,
                                  const FocusTarget& aFocusTarget) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RunOnUpdaterThread(aOriginatingLayersId,
                     NewRunnableMethod<LayersId, LayersId, FocusTarget>(
                         "APZUpdater::UpdateFocusState", mApz,
                         &APZCTreeManager::UpdateFocusState, aRootLayerTreeId,
                         aOriginatingLayersId, aFocusTarget));
}

void APZUpdater::UpdateScrollDataAndTreeState(
    LayersId aRootLayerTreeId, LayersId aOriginatingLayersId,
    const wr::Epoch& aEpoch, WebRenderScrollData&& aScrollData) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZUpdater> self = this;
  // Insert an epoch requirement update into the queue, so that
  // tasks inserted into the queue after this point only get executed
  // once the epoch requirement is satisfied. In particular, the
  // UpdateHitTestingTree call below needs to wait until the epoch requirement
  // is satisfied, which is why it is a separate task in the queue.
  RunOnUpdaterThread(
      aOriginatingLayersId,
      NS_NewRunnableFunction("APZUpdater::UpdateEpochRequirement", [=]() {
        if (aRootLayerTreeId == aOriginatingLayersId) {
          self->mEpochData[aOriginatingLayersId].mIsRoot = true;
        }
        self->mEpochData[aOriginatingLayersId].mRequired = aEpoch;
      }));
  RunOnUpdaterThread(
      aOriginatingLayersId,
      NS_NewRunnableFunction(
          "APZUpdater::UpdateHitTestingTree",
          [=, aScrollData = std::move(aScrollData)]() mutable {
            auto isFirstPaint = aScrollData.IsFirstPaint();
            auto paintSequenceNumber = aScrollData.GetPaintSequenceNumber();

            auto previous = self->mScrollData.find(aOriginatingLayersId);
            // If there's the previous scroll data which hasn't yet been
            // processed, we need to merge the previous scroll position updates
            // into the latest one.
            if (previous != self->mScrollData.end()) {
              WebRenderScrollData& previousData = previous->second;
              if (previousData.GetWasUpdateSkipped()) {
                MOZ_ASSERT(previousData.IsFirstPaint());
                aScrollData.PrependUpdates(previousData);
              }
            }

            self->mScrollData[aOriginatingLayersId] = std::move(aScrollData);
            auto root = self->mScrollData.find(aRootLayerTreeId);
            if (root == self->mScrollData.end()) {
              return;
            }

            auto updatedIds = self->mApz->UpdateHitTestingTree(
                WebRenderScrollDataWrapper(*self, &(root->second)),
                aOriginatingLayersId, paintSequenceNumber);
            bool originatingLayersIdWasSkipped = true;
            for (auto id : updatedIds) {
              if (id == aOriginatingLayersId) {
                originatingLayersIdWasSkipped = false;
              }

              // Reset `mWasUpdateSkipped` flag forcibly here even if the
              // `aOriginatingLayersId` is different from the layersId for the
              // data, otherwise the skipped scroll position updates will be
              // re-processed again.
              self->mScrollData[id].SetWasUpdateSkipped(false);
            }

            if (isFirstPaint) {
              if (originatingLayersIdWasSkipped) {
                // If the given |aOriginatingLayersId| data wasn't used for
                // updating, it's likely that the parent process hasn't yet
                // received the LayersId as "ReferentId", thus we need to
                // process it in a subsequent update where we got the
                // "ReferentId".
                //
                // NOTE: We restrict the above previous scroll data prepending
                // to the first paint case, otherwise the cumulative scroll data
                // may be exploded if we have never received the "ReferenceId".
                self->mScrollData[aOriginatingLayersId].SetWasUpdateSkipped(
                    true);
              } else {
                // Clobber the first-paint flag so that we will never run into
                // the first-paint branch in
                // AsyncPanZoomController::NotifyMainThreadTransaction even if
                // the next transaction is a paint-skip transaction.
                self->mScrollData[aOriginatingLayersId].SetIsFirstPaint(false);
              }
            }
          }));
}

void APZUpdater::UpdateScrollOffsets(LayersId aRootLayerTreeId,
                                     LayersId aOriginatingLayersId,
                                     ScrollUpdatesMap&& aUpdates,
                                     uint32_t aPaintSequenceNumber) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZUpdater> self = this;
  RunOnUpdaterThread(
      aOriginatingLayersId,
      NS_NewRunnableFunction(
          "APZUpdater::UpdateScrollOffsets",
          [=, updates = std::move(aUpdates)]() mutable {
            self->mScrollData[aOriginatingLayersId].ApplyUpdates(
                std::move(updates), aPaintSequenceNumber);
            auto root = self->mScrollData.find(aRootLayerTreeId);
            if (root == self->mScrollData.end()) {
              return;
            }
            self->mApz->UpdateHitTestingTree(
                WebRenderScrollDataWrapper(*self, &(root->second)),
                aOriginatingLayersId, aPaintSequenceNumber);
          }));
}

void APZUpdater::NotifyLayerTreeAdopted(LayersId aLayersId,
                                        const RefPtr<APZUpdater>& aOldUpdater) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RunOnUpdaterThread(aLayersId,
                     NewRunnableMethod<LayersId, RefPtr<APZCTreeManager>>(
                         "APZUpdater::NotifyLayerTreeAdopted", mApz,
                         &APZCTreeManager::NotifyLayerTreeAdopted, aLayersId,
                         aOldUpdater ? aOldUpdater->mApz : nullptr));
}

void APZUpdater::NotifyLayerTreeRemoved(LayersId aLayersId) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZUpdater> self = this;
  RunOnUpdaterThread(
      aLayersId,
      NS_NewRunnableFunction("APZUpdater::NotifyLayerTreeRemoved", [=]() {
        self->mEpochData.erase(aLayersId);
        self->mScrollData.erase(aLayersId);
        self->mApz->NotifyLayerTreeRemoved(aLayersId);
      }));
}

bool APZUpdater::GetAPZTestData(LayersId aLayersId, APZTestData* aOutData) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  RefPtr<APZCTreeManager> apz = mApz;
  bool ret = false;
  SynchronousTask waiter("APZUpdater::GetAPZTestData");
  RunOnUpdaterThread(
      aLayersId, NS_NewRunnableFunction("APZUpdater::GetAPZTestData", [&]() {
        AutoCompleteTask notifier(&waiter);
        ret = apz->GetAPZTestData(aLayersId, aOutData);
      }));

  // Wait until the task posted above has run and populated aOutData and ret
  waiter.Wait();

  return ret;
}

void APZUpdater::SetTestAsyncScrollOffset(
    LayersId aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const CSSPoint& aOffset) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZCTreeManager> apz = mApz;
  RunOnUpdaterThread(
      aLayersId,
      NS_NewRunnableFunction("APZUpdater::SetTestAsyncScrollOffset", [=]() {
        RefPtr<AsyncPanZoomController> apzc =
            apz->GetTargetAPZC(aLayersId, aScrollId);
        if (apzc) {
          apzc->SetTestAsyncScrollOffset(aOffset);
        } else {
          NS_WARNING("Unable to find APZC in SetTestAsyncScrollOffset");
        }
      }));
}

void APZUpdater::SetTestAsyncZoom(LayersId aLayersId,
                                  const ScrollableLayerGuid::ViewID& aScrollId,
                                  const LayerToParentLayerScale& aZoom) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  RefPtr<APZCTreeManager> apz = mApz;
  RunOnUpdaterThread(
      aLayersId, NS_NewRunnableFunction("APZUpdater::SetTestAsyncZoom", [=]() {
        RefPtr<AsyncPanZoomController> apzc =
            apz->GetTargetAPZC(aLayersId, aScrollId);
        if (apzc) {
          apzc->SetTestAsyncZoom(aZoom);
        } else {
          NS_WARNING("Unable to find APZC in SetTestAsyncZoom");
        }
      }));
}

const WebRenderScrollData* APZUpdater::GetScrollData(LayersId aLayersId) const {
  AssertOnUpdaterThread();
  auto it = mScrollData.find(aLayersId);
  return (it == mScrollData.end() ? nullptr : &(it->second));
}

void APZUpdater::AssertOnUpdaterThread() const {
  if (APZThreadUtils::GetThreadAssertionsEnabled()) {
    MOZ_ASSERT(IsUpdaterThread());
  }
}

void APZUpdater::RunOnUpdaterThread(LayersId aLayersId,
                                    already_AddRefed<Runnable> aTask,
                                    DuringShutdown aDuringShutdown) {
  RefPtr<Runnable> task = aTask;

  // In the scenario where IsConnectedToWebRender() is true, this function
  // might get called early (before mUpdaterThreadId is set). In that case
  // IsUpdaterThread() will return false and we'll queue the task onto
  // mUpdaterQueue. This is fine; the task is still guaranteed to run (barring
  // catastrophic failure) because the WakeSceneBuilder call will still trigger
  // the callback to run tasks.

  if (IsUpdaterThread()) {
    // This function should only be called from the updater thread in test
    // scenarios where we are not connected to WebRender. If it were called from
    // the updater thread when we are connected to WebRender, running the task
    // right away would be incorrect (we'd need to check that |aLayersId|
    // isn't blocked, and if it is then enqueue the task instead).
    MOZ_ASSERT(!IsConnectedToWebRender());
    task->Run();
    return;
  }

  if (IsConnectedToWebRender()) {
    // If the updater thread is a WebRender thread, and we're not on it
    // right now, save the task in the queue. We will run tasks from the queue
    // during the callback from the updater thread, which we trigger by the
    // call to WakeSceneBuilder.

    bool sendWakeMessage = aDuringShutdown == DuringShutdown::No;
    {  // scope lock
      MutexAutoLock lock(mQueueLock);
      if (sendWakeMessage) {
        for (const auto& queuedTask : mUpdaterQueue) {
          if (queuedTask.mLayersId == aLayersId) {
            // If there's already a task in the queue with this layers id, then
            // we must have previously sent a WakeSceneBuilder message (when
            // adding the first task with this layers id to the queue). Either
            // that hasn't been fully processed yet, or the layers id is blocked
            // waiting for an epoch - in either case there's no point in sending
            // another WakeSceneBuilder message.
            sendWakeMessage = false;
            break;
          }
        }
      }
      mUpdaterQueue.push_back(QueuedTask{aLayersId, task, aDuringShutdown});
    }
    if (sendWakeMessage) {
      RefPtr<wr::WebRenderAPI> api = mApz->GetWebRenderAPI();
      if (api) {
        api->WakeSceneBuilder();
      } else {
        // Not sure if this can happen, but it might be possible. If it does,
        // the task is in the queue, but if we didn't get a WebRenderAPI it
        // might never run, or it might run later if we manage to get a
        // WebRenderAPI later. For now let's just emit a warning, this can
        // probably be upgraded to an assert later.
        NS_WARNING("Possibly dropping task posted to updater thread");
      }
    }
    return;
  }

  if (CompositorThread()) {
    CompositorThread()->Dispatch(task.forget());
  } else {
    // Could happen during startup
    NS_WARNING("Dropping task posted to updater thread");
  }
}

bool APZUpdater::IsUpdaterThread() const {
  if (IsConnectedToWebRender()) {
    // If the updater thread id isn't set yet then we cannot be running on the
    // updater thread (because we will have the thread id before we run any
    // C++ code on it, and this function is only ever invoked from C++ code),
    // so return false in that scenario.
    MutexAutoLock lock(mThreadIdLock);
    return mUpdaterThreadId && PlatformThread::CurrentId() == *mUpdaterThreadId;
  }
  return CompositorThreadHolder::IsInCompositorThread();
}

bool APZUpdater::HasUpdaterThread() const {
  MutexAutoLock lock(mThreadIdLock);
  return mUpdaterThreadId.isSome();
}

void APZUpdater::AssertOnUpdaterThreadOrNotInitialized() const {
  if (APZThreadUtils::GetThreadAssertionsEnabled()) {
    MOZ_ASSERT(IsUpdaterThread() || !HasUpdaterThread());
  }
}

void APZUpdater::RunOnControllerThread(LayersId aLayersId,
                                       already_AddRefed<Runnable> aTask) {
  MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  RefPtr<Runnable> task = aTask;

  RunOnUpdaterThread(
      aLayersId,
      NewRunnableFunction("APZUpdater::RunOnControllerThread",
                          &APZThreadUtils::RunOnControllerThread,
                          std::move(task), nsIThread::DISPATCH_NORMAL));
}

bool APZUpdater::IsConnectedToWebRender() const {
  return mConnectedToWebRender;
}

/*static*/
already_AddRefed<APZUpdater> APZUpdater::GetUpdater(
    const wr::WrWindowId& aWindowId) {
  RefPtr<APZUpdater> updater;
  StaticMutexAutoLock lock(sWindowIdLock);
  if (sWindowIdMap) {
    auto it = sWindowIdMap->find(wr::AsUint64(aWindowId));
    if (it != sWindowIdMap->end()) {
      updater = it->second;
    }
  }
  return updater.forget();
}

void APZUpdater::ProcessQueue() {
  MOZ_ASSERT(!mDestroyed);

  {  // scope lock to check for emptiness
    MutexAutoLock lock(mQueueLock);
    if (mUpdaterQueue.empty()) {
      return;
    }
  }

  std::deque<QueuedTask> blockedTasks;
  while (true) {
    QueuedTask task;

    {  // scope lock to extract a task
      MutexAutoLock lock(mQueueLock);
      if (mUpdaterQueue.empty()) {
        // If we're done processing mUpdaterQueue, swap the tasks that are
        // still blocked back in and finish
        std::swap(mUpdaterQueue, blockedTasks);
        break;
      }
      task = mUpdaterQueue.front();
      mUpdaterQueue.pop_front();
    }

    // We check the task to see if it is blocked. Note that while this
    // ProcessQueue function is executing, a particular layers id cannot go
    // from blocked to unblocked, because only CompleteSceneSwap can unblock
    // a layers id, and that also runs on the updater thread. If somehow
    // a layers id gets unblocked while we're processing the queue, then it
    // might result in tasks getting executed out of order.

    auto it = mEpochData.find(task.mLayersId);
    if (task.mDuringShutdown == DuringShutdown::No && it != mEpochData.end() &&
        it->second.IsBlocked()) {
      // If this task is blocked, put it into the blockedTasks queue that
      // we will replace mUpdaterQueue with
      blockedTasks.push_back(task);
    } else {
      // Run and discard the task
      task.mRunnable->Run();
    }
  }

  if (mDestroyed) {
    // If we get here, then we must have just run the ClearTree task for
    // this updater. There might be tasks in the queue from content subtrees
    // of this window that are blocked due to stale epochs. This can happen
    // if the tasks were queued after the root pipeline was removed in
    // WebRender, which prevents scene builds (and therefore prevents us
    // from getting updated epochs via CompleteSceneSwap). See bug 1465658
    // comment 43 for some more context. There might also be tasks for the
    // root layers id that were queued before ClearTree, which runs ahead of
    // them if the root's last epoch was never built.
    // To avoid leaking these tasks, we discard the contents of the queue.
    // This happens during window shutdown so if we don't run the tasks it's
    // not going to matter much.
    MutexAutoLock lock(mQueueLock);
    if (!mUpdaterQueue.empty()) {
      mUpdaterQueue.clear();
    }
  }
}

void APZUpdater::MarkAsDetached(LayersId aLayersId) {
  mApz->MarkAsDetached(aLayersId);
}

APZUpdater::EpochState::EpochState() : mRequired{0}, mIsRoot(false) {}

bool APZUpdater::EpochState::IsBlocked() const {
  // The root is a special case because we basically assume it is "visible"
  // even before it is built for the first time. This is because building the
  // scene automatically makes it visible, and we need to make sure the APZ
  // scroll data gets applied atomically with that happening.
  //
  // Layer subtrees on the other hand do not automatically become visible upon
  // being built, because there must be a another layer tree update to change
  // the visibility (i.e. an ancestor layer tree update that adds the necessary
  // reflayer to complete the chain of reflayers).
  //
  // So in the case of non-visible subtrees, we know that no hit-test will
  // actually end up hitting that subtree either before or after the scene swap,
  // because the subtree will remain non-visible. That in turns means that we
  // can apply the APZ scroll data for that subtree epoch before the scene is
  // built, because it's not going to get used anyway. And that means we don't
  // need to block the queue for non-visible subtrees. Which is a good thing,
  // because in practice it seems like we often have non-visible subtrees sent
  // to the compositor from content.
  if (mIsRoot && !mBuilt) {
    return true;
  }
  return mBuilt && (*mBuilt < mRequired);
}

}  // namespace layers
}  // namespace mozilla

// Rust callback implementations

void apz_register_updater(mozilla::wr::WrWindowId aWindowId) {
  mozilla::layers::APZUpdater::SetUpdaterThread(aWindowId);
}

void apz_pre_scene_swap(mozilla::wr::WrWindowId aWindowId) {
  mozilla::layers::APZUpdater::PrepareForSceneSwap(aWindowId);
}

void apz_post_scene_swap(mozilla::wr::WrWindowId aWindowId,
                         const mozilla::wr::WrPipelineInfo* aInfo) {
  mozilla::layers::APZUpdater::CompleteSceneSwap(aWindowId, *aInfo);
}

void apz_run_updater(mozilla::wr::WrWindowId aWindowId) {
  mozilla::layers::APZUpdater::ProcessPendingTasks(aWindowId);
}

void apz_deregister_updater(mozilla::wr::WrWindowId aWindowId) {
  // Run anything that's still left.
  mozilla::layers::APZUpdater::ProcessPendingTasks(aWindowId);
}
