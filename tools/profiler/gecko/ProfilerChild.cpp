/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProfilerChild.h"

#include "GeckoProfiler.h"
#include "platform.h"
#include "ProfilerCodeAddressService.h"
#include "ProfilerControl.h"
#include "ProfilerParent.h"

#include "chrome/common/ipc_channel.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

#include <memory>

namespace mozilla {

MOZ_RUNINIT /* static */ DataMutexBase<ProfilerChild::ProfilerChildAndUpdate,
                                       baseprofiler::detail::BaseProfilerMutex>
    ProfilerChild::sPendingChunkManagerUpdate{
        "ProfilerChild::sPendingChunkManagerUpdate"};

ProfilerChild::ProfilerChild()
    : mThread(NS_GetCurrentThread()), mDestroyed(false) {
  MOZ_COUNT_CTOR(ProfilerChild);
}

ProfilerChild::~ProfilerChild() { MOZ_COUNT_DTOR(ProfilerChild); }

void ProfilerChild::ResolveChunkUpdate(
    PProfilerChild::AwaitNextChunkManagerUpdateResolver& aResolve) {
  MOZ_ASSERT(!!aResolve,
             "ResolveChunkUpdate should only be called when there's a pending "
             "resolver");
  MOZ_ASSERT(
      !mChunkManagerUpdate.IsNotUpdate(),
      "ResolveChunkUpdate should only be called with a real or final update");
  MOZ_ASSERT(
      !mDestroyed,
      "ResolveChunkUpdate should not be called if the actor was destroyed");
  if (mChunkManagerUpdate.IsFinal()) {
    // Final update, send a special "unreleased value", but don't clear the
    // local copy so we know we got the final update.
    std::move(aResolve)(ProfilerParent::MakeFinalUpdate());
  } else {
    // Optimization note: The ProfileBufferChunkManagerUpdate constructor takes
    // the newly-released chunks nsTArray by reference-to-const, therefore
    // constructing and then moving the array here would make a copy. So instead
    // we first give it an empty array, and then we can write the data directly
    // into the update's array.
    ProfileBufferChunkManagerUpdate update{
        mChunkManagerUpdate.UnreleasedBytes(),
        mChunkManagerUpdate.ReleasedBytes(),
        mChunkManagerUpdate.OldestDoneTimeStamp(),
        {}};
    update.newlyReleasedChunks().SetCapacity(
        mChunkManagerUpdate.NewlyReleasedChunksRef().size());
    for (const ProfileBufferControlledChunkManager::ChunkMetadata& chunk :
         mChunkManagerUpdate.NewlyReleasedChunksRef()) {
      update.newlyReleasedChunks().EmplaceBack(chunk.mDoneTimeStamp,
                                               chunk.mBufferBytes);
    }

    std::move(aResolve)(update);

    // Clear the update we just sent, so it's ready for later updates to be
    // folded into it.
    mChunkManagerUpdate.Clear();
  }

  // Discard the resolver, so it's empty next time there's a new request.
  aResolve = nullptr;
}

void ProfilerChild::ProcessChunkManagerUpdate(
    ProfileBufferControlledChunkManager::Update&& aUpdate) {
  if (mDestroyed) {
    return;
  }
  // Always store the data, it could be the final update.
  mChunkManagerUpdate.Fold(std::move(aUpdate));
  if (mAwaitNextChunkManagerUpdateResolver) {
    // There is already a pending resolver, give it the info now.
    ResolveChunkUpdate(mAwaitNextChunkManagerUpdateResolver);
  }
}

/* static */ void ProfilerChild::ProcessPendingUpdate() {
  // sPendingChunkManagerUpdate must not be held while dispatching or while
  // processing the update: Dispatch() takes the target thread's event queue
  // mutex, and ProcessChunkManagerUpdate() can resolve an await-next-update
  // request, which sends an IPC reply and takes the IPC channel lock. Both of
  // those locks can be held while a profiler marker is recorded (which takes
  // the profiler buffer lock), and that buffer lock is held when the chunk
  // manager update callback takes sPendingChunkManagerUpdate. Holding it across
  // either operation would close that cycle and deadlock.
  nsCOMPtr<nsIThread> thread;
  {
    auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
    if (!lockedUpdate->mProfilerChild || lockedUpdate->mUpdate.IsNotUpdate()) {
      return;
    }
    thread = lockedUpdate->mProfilerChild->mThread;
  }
  RefPtr<Runnable> runnable = NS_NewRunnableFunction(
      "ProfilerChild::ProcessPendingUpdate", []() mutable {
        RefPtr<ProfilerChild> profilerChild;
        ProfileBufferControlledChunkManager::Update update;
        {
          auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
          if (!lockedUpdate->mProfilerChild ||
              lockedUpdate->mUpdate.IsNotUpdate()) {
            return;
          }
          profilerChild = lockedUpdate->mProfilerChild;
          update = std::move(lockedUpdate->mUpdate);
          lockedUpdate->mUpdate.Clear();
        }
        profilerChild->ProcessChunkManagerUpdate(std::move(update));
      });
  // The ProfilerChild thread may have been shut down since the lock was
  // released. Dispatch fallibly so the runnable is released instead of leaked.
  thread->Dispatch(runnable.forget(), NS_DISPATCH_FALLIBLE);
}

/* static */ bool ProfilerChild::IsLockedOnCurrentThread() {
  return sPendingChunkManagerUpdate.Mutex().IsLockedOnCurrentThread();
}

void ProfilerChild::SetupChunkManager() {
  mChunkManager = profiler_get_controlled_chunk_manager();
  if (NS_WARN_IF(!mChunkManager)) {
    return;
  }

  // Make sure there are no updates (from a previous run).
  mChunkManagerUpdate.Clear();
  {
    auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
    lockedUpdate->mProfilerChild = this;
    lockedUpdate->mUpdate.Clear();
  }

  mChunkManager->SetUpdateCallback(
      [](ProfileBufferControlledChunkManager::Update&& aUpdate) {
        // Updates from the chunk manager are stored for later processing.
        // We avoid dispatching a task, as this could deadlock (if the queueing
        // mutex is held elsewhere).
        auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
        if (!lockedUpdate->mProfilerChild) {
          return;
        }
        lockedUpdate->mUpdate.Fold(std::move(aUpdate));
      });
}

/* static */ void ProfilerChild::ClearPendingUpdate() {
  auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
  lockedUpdate->mProfilerChild = nullptr;
  lockedUpdate->mUpdate.Clear();
}

void ProfilerChild::ResetChunkManager() {
  if (!mChunkManager) {
    return;
  }

  // We have a chunk manager, reset the callback, which will add a final
  // pending update.
  mChunkManager->SetUpdateCallback({});

  // Clear the pending update.
  ClearPendingUpdate();
  // And process a final update right now.
  ProcessChunkManagerUpdate(
      ProfileBufferControlledChunkManager::Update(nullptr));

  mChunkManager = nullptr;
  mAwaitNextChunkManagerUpdateResolver = nullptr;
}

mozilla::ipc::IPCResult ProfilerChild::RecvStart(
    const ProfilerInitParams& params, StartResolver&& aResolve) {
  nsTArray<const char*> filterArray;
  for (size_t i = 0; i < params.filters().Length(); ++i) {
    filterArray.AppendElement(params.filters()[i].get());
  }

  profiler_start(PowerOfTwo32(params.entries()), params.interval(),
                 params.features(), filterArray.Elements(),
                 filterArray.Length(), params.activeTabID(), params.duration());

  SetupChunkManager();

  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvEnsureStarted(
    const ProfilerInitParams& params, EnsureStartedResolver&& aResolve) {
  nsTArray<const char*> filterArray;
  for (size_t i = 0; i < params.filters().Length(); ++i) {
    filterArray.AppendElement(params.filters()[i].get());
  }

  profiler_ensure_started(PowerOfTwo32(params.entries()), params.interval(),
                          params.features(), filterArray.Elements(),
                          filterArray.Length(), params.activeTabID(),
                          params.duration());

  SetupChunkManager();

  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvStop(StopResolver&& aResolve) {
  ResetChunkManager();
  profiler_stop();
  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvPause(PauseResolver&& aResolve) {
  profiler_pause();
  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvResume(ResumeResolver&& aResolve) {
  profiler_resume();
  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvPauseSampling(
    PauseSamplingResolver&& aResolve) {
  profiler_pause_sampling();
  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvResumeSampling(
    ResumeSamplingResolver&& aResolve) {
  profiler_resume_sampling();
  aResolve(/* unused */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvWaitOnePeriodicSampling(
    WaitOnePeriodicSamplingResolver&& aResolve) {
  std::shared_ptr<WaitOnePeriodicSamplingResolver> resolve =
      std::make_shared<WaitOnePeriodicSamplingResolver>(std::move(aResolve));
  if (!profiler_callback_after_sampling(
          [self = RefPtr(this), resolve](SamplingState aSamplingState) mutable {
            if (self->mDestroyed) {
              return;
            }
            MOZ_RELEASE_ASSERT(self->mThread);
            self->mThread->Dispatch(NS_NewRunnableFunction(
                "nsProfiler::WaitOnePeriodicSampling result on main thread",
                [resolve = std::move(resolve), aSamplingState]() {
                  (*resolve)(aSamplingState ==
                             SamplingState::SamplingCompleted);
                }));
          })) {
    // Callback was not added (e.g., profiler is not running) and will never be
    // invoked, so we need to resolve the promise here.
    (*resolve)(false);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvClearAllPages() {
  profiler_clear_all_pages();
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvAwaitNextChunkManagerUpdate(
    AwaitNextChunkManagerUpdateResolver&& aResolve) {
  MOZ_ASSERT(!mDestroyed,
             "Recv... should not be called if the actor was destroyed");
  // Pick up pending updates if any.
  {
    auto lockedUpdate = sPendingChunkManagerUpdate.Lock();
    if (lockedUpdate->mProfilerChild && !lockedUpdate->mUpdate.IsNotUpdate()) {
      mChunkManagerUpdate.Fold(std::move(lockedUpdate->mUpdate));
      lockedUpdate->mUpdate.Clear();
    }
  }
  if (mChunkManagerUpdate.IsNotUpdate()) {
    // No data yet, store the resolver for later.
    mAwaitNextChunkManagerUpdateResolver = std::move(aResolve);
  } else {
    // We have data, send it now.
    ResolveChunkUpdate(aResolve);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvDestroyReleasedChunksAtOrBefore(
    const TimeStamp& aTimeStamp) {
  if (mChunkManager) {
    mChunkManager->DestroyChunksAtOrBefore(aTimeStamp);
  }
  return IPC_OK();
}

struct GatherProfileThreadParameters
    : public external::AtomicRefCounted<GatherProfileThreadParameters> {
  MOZ_DECLARE_REFCOUNTED_TYPENAME(GatherProfileThreadParameters)

  GatherProfileThreadParameters(
      RefPtr<ProfilerChild> aProfilerChild,
      RefPtr<ProgressLogger::SharedProgress> aProgress,
      ProfilerChild::GatherProfileResolver&& aResolver)
      : profilerChild(std::move(aProfilerChild)),
        progress(std::move(aProgress)),
        resolver(std::move(aResolver)) {}

  RefPtr<ProfilerChild> profilerChild;

  FailureLatchSource failureLatchSource;

  // Separate RefPtr used when working on separate thread. This way, if the
  // "ProfilerChild" thread decides to overwrite its mGatherProfileProgress with
  // a new one, the work done here will still only use the old one.
  RefPtr<ProgressLogger::SharedProgress> progress;

  // Resolver for the GatherProfile promise. Must only be called on the
  // "ProfilerChild" thread.
  ProfilerChild::GatherProfileResolver resolver;
};

/* static */
void ProfilerChild::GatherProfileThreadFunction(
    void* already_AddRefedParameters) {
  PR_SetCurrentThreadName("GatherProfileThread");

  RefPtr<GatherProfileThreadParameters> parameters =
      already_AddRefed<GatherProfileThreadParameters>{
          static_cast<GatherProfileThreadParameters*>(
              already_AddRefedParameters)};

  ProgressLogger progressLogger(
      parameters->progress, "Gather-profile thread started", "Profile sent");
  using namespace mozilla::literals::ProportionValue_literals;  // For `1_pc`.

  auto writer = std::make_unique<SpliceableChunkedJSONWriter>(
      parameters->failureLatchSource);
  auto rv =
      profiler_get_profile_json(*writer,
                                /* aSinceTime */ 0,
                                /* aIsShuttingDown */ false,
                                progressLogger.CreateSubLoggerFromTo(
                                    1_pc, "profiler_get_profile_json started",
                                    99_pc, "profiler_get_profile_json done"));
  if (rv.isErr()) {
    // Failed to get a profile, reset the writer pointer, so that we'll send a
    // failure message.
    writer.reset();
  }

  if (NS_WARN_IF(NS_FAILED(
          parameters->profilerChild->mThread->Dispatch(NS_NewRunnableFunction(
              "ProfilerChild::ProcessPendingUpdate",
              [parameters,
               // Forward progress logger to on-ProfilerChild-thread task, so
               // that it doesn't get marked as 100% done when this off-thread
               // function ends.
               progressLogger = std::move(progressLogger),
               writer = std::move(writer), rv = std::move(rv)]() mutable {
                // We are now on the ProfilerChild thread, about to send the
                // completed profile. Any incoming progress request will now be
                // handled after this task ends, so updating the progress is now
                // useless and we can just get rid of the progress storage.
                if (parameters->profilerChild->mGatherProfileProgress ==
                    parameters->progress) {
                  // The ProfilerChild progress is still the one we know.
                  parameters->profilerChild->mGatherProfileProgress = nullptr;
                }

                // Shmem allocation and promise resolution must be made on the
                // ProfilerChild thread, that's why this task was needed here.
                mozilla::ipc::Shmem shmem;
                if (writer) {
                  if (const size_t len = writer->ChunkedWriteFunc().Length();
                      len < UINT32_MAX) {
                    bool shmemSuccess = true;
                    const bool copySuccess =
                        writer->ChunkedWriteFunc()
                            .CopyDataIntoLazilyAllocatedBuffer(
                                [&](size_t allocationSize) -> char* {
                                  MOZ_ASSERT(allocationSize == len + 1);
                                  if (parameters->profilerChild->AllocShmem(
                                          allocationSize, &shmem)) {
                                    return shmem.get<char>();
                                  }
                                  shmemSuccess = false;
                                  return nullptr;
                                });
                    if (!shmemSuccess || !copySuccess) {
                      const nsPrintfCString message(
                          (!shmemSuccess)
                              ? "*Could not create shmem for profile from pid "
                                "%u (%zu B)"
                              : "*Could not write profile from pid %u (%zu B)",
                          unsigned(profiler_current_process_id().ToNumber()),
                          len);
                      if (parameters->profilerChild->AllocShmem(
                              message.Length() + 1, &shmem)) {
                        strcpy(shmem.get<char>(), message.get());
                      }
                    }
                  } else {
                    const nsPrintfCString message(
                        "*Profile from pid %u bigger (%zu) than shmem max "
                        "(%zu)",
                        unsigned(profiler_current_process_id().ToNumber()), len,
                        size_t(UINT32_MAX));
                    if (parameters->profilerChild->AllocShmem(
                            message.Length() + 1, &shmem)) {
                      strcpy(shmem.get<char>(), message.get());
                    }
                  }
                  writer = nullptr;
                } else {
                  // No profile.
                  const char* failure =
                      parameters->failureLatchSource.GetFailure();
                  const nsPrintfCString message(
                      "*Could not generate profile from pid %u%s%s",
                      unsigned(profiler_current_process_id().ToNumber()),
                      failure ? ", failure: " : "", failure ? failure : "");
                  if (parameters->profilerChild->AllocShmem(
                          message.Length() + 1, &shmem)) {
                    strcpy(shmem.get<char>(), message.get());
                  }
                }

                Maybe<ProfileGenerationAdditionalInformation> additionalInfo =
                    rv.isOk() ? Some(rv.unwrap()) : Nothing();
                parameters->resolver(IPCProfileAndAdditionalInformation{
                    std::move(shmem), std::move(additionalInfo)});
                // Let's join the gather profile thread now since it's done.
                // Note that this gets called inside the ProfilerChild thread
                // and not inside the gather profile thread itself.
                parameters->profilerChild->JoinGatherProfileThread();
              }))))) {
    // Failed to dispatch the task to the ProfilerChild thread. The IPC cannot
    // be resolved on this thread, so it will never be resolved!
    // And it would be unsafe to modify mGatherProfileProgress; But the parent
    // should notice that's it's not advancing anymore.
    // If this fails, it usually means that the process is shutting down. This
    // thread is not joined yet and we can't join it here since we are still in
    // the thread, but it will happen inside ProfilerChild::ActorDestroy.
  }
}

mozilla::ipc::IPCResult ProfilerChild::RecvGatherProfile(
    GatherProfileResolver&& aResolve) {
  mGatherProfileProgress = MakeRefPtr<ProgressLogger::SharedProgress>();
  mGatherProfileProgress->SetProgress(ProportionValue{0.0},
                                      "Received gather-profile request");

  auto parameters = MakeRefPtr<GatherProfileThreadParameters>(
      this, mGatherProfileProgress, std::move(aResolve));

  // The GatherProfileThreadFunction thread function will cast its void*
  // argument to already_AddRefed<GatherProfileThreadParameters>.
  parameters.get()->AddRef();
  mGatherProfileThread = PR_CreateThread(
      PR_SYSTEM_THREAD, GatherProfileThreadFunction, parameters.get(),
      PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);

  if (!mGatherProfileThread) {
    // Failed to create and start worker thread, resolve with an empty profile.
    mozilla::ipc::Shmem shmem;
    if (AllocShmem(1, &shmem)) {
      shmem.get<char>()[0] = '\0';
    }
    parameters->resolver(IPCProfileAndAdditionalInformation{shmem, Nothing()});
    // And clean up.
    parameters.get()->Release();
    mGatherProfileProgress = nullptr;
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult ProfilerChild::RecvGetGatherProfileProgress(
    GetGatherProfileProgressResolver&& aResolve) {
  if (mGatherProfileProgress) {
    aResolve(GatherProfileProgress{
        mGatherProfileProgress->Progress().ToUnderlyingType(),
        nsCString(mGatherProfileProgress->LastLocation())});
  } else {
    aResolve(
        GatherProfileProgress{ProportionValue::MakeInvalid().ToUnderlyingType(),
                              nsCString("No gather-profile in progress")});
  }
  return IPC_OK();
}

void ProfilerChild::JoinGatherProfileThread() {
  if (!mGatherProfileThread) {
    // There is no thread running to join.
    return;
  }

  PR_JoinThread(mGatherProfileThread);
  mGatherProfileThread = nullptr;
}

void ProfilerChild::ActorDestroy(ActorDestroyReason /*aActorDestroyReason*/) {
  mDestroyed = true;
  if (mAwaitNextChunkManagerUpdateResolver) {
    // The reply can't be sent anymore (IPC rejects the parent's request);
    // consuming the resolver just avoids the dropped-resolver warning.
    std::move(mAwaitNextChunkManagerUpdateResolver)(
        ProfilerParent::MakeFinalUpdate());
    mAwaitNextChunkManagerUpdateResolver = nullptr;
  }
  // Join the profile gathering thread in case it's not exited yet to prevent
  // any leaks during shutdown.
  JoinGatherProfileThread();
}

void ProfilerChild::Destroy() {
  ClearPendingUpdate();
  if (!mDestroyed) {
    // Resolve the parent's pending update request while the channel is open;
    // Close() would drop the reply. Not ResetChunkManager(), which would
    // dereference mChunkManager during shutdown.
    ProcessChunkManagerUpdate(
        ProfileBufferControlledChunkManager::Update(nullptr));
    Close();
  }
}

ProfileAndAdditionalInformation ProfilerChild::GrabShutdownProfile() {
  LOG("GrabShutdownProfile");

  UniquePtr<ProfilerCodeAddressService> service =
      profiler_code_address_service_for_presymbolication();
  FailureLatchSource failureLatch;
  SpliceableChunkedJSONWriter writer{failureLatch};
  writer.Start();
  auto rv = profiler_stream_json_for_this_process(
      writer, /* aSinceTime */ 0,
      /* aIsShuttingDown */ true, service.get(), ProgressLogger{});
  if (rv.isErr()) {
    const char* failure = writer.GetFailure();
    return ProfileAndAdditionalInformation(
        nsPrintfCString("*Profile unavailable for pid %u%s%s",
                        unsigned(profiler_current_process_id().ToNumber()),
                        failure ? ", failure: " : "", failure ? failure : ""));
  }

  auto additionalInfo = rv.unwrap();

  writer.StartArrayProperty("processes");
  writer.EndArray();
  writer.End();

  const size_t len = writer.ChunkedWriteFunc().Length();
  // This string and information are destined to be sent as a shutdown profile,
  // which is limited by the maximum IPC message size.
  // TODO: IPC to change to shmem (bug 1780330), raising this limit to
  // JS::MaxStringLength.
  if (len + additionalInfo.InlineIPCSize() >=
      size_t(IPC::Channel::kMaximumMessageSize)) {
    return ProfileAndAdditionalInformation(
        nsPrintfCString("*Profile from pid %u bigger (%zu) than IPC max (%zu)",
                        unsigned(profiler_current_process_id().ToNumber()), len,
                        size_t(IPC::Channel::kMaximumMessageSize)));
  }

  nsCString profileCString;
  if (!profileCString.SetLength(len, fallible)) {
    return ProfileAndAdditionalInformation(nsPrintfCString(
        "*Could not allocate %zu bytes for profile from pid %u", len,
        unsigned(profiler_current_process_id().ToNumber())));
  }
  MOZ_ASSERT(*(profileCString.Data() + len) == '\0',
             "We expected a null at the end of the string buffer, to be "
             "rewritten by CopyDataIntoLazilyAllocatedBuffer");

  char* const profileBeginWriting = profileCString.BeginWriting();
  if (!profileBeginWriting) {
    return ProfileAndAdditionalInformation(
        nsPrintfCString("*Could not write profile from pid %u",
                        unsigned(profiler_current_process_id().ToNumber())));
  }

  // Here, we have enough space reserved in `profileCString`, starting at
  // `profileBeginWriting`, copy the JSON profile there.
  if (!writer.ChunkedWriteFunc().CopyDataIntoLazilyAllocatedBuffer(
          [&](size_t aBufferLen) -> char* {
            MOZ_RELEASE_ASSERT(aBufferLen == len + 1);
            return profileBeginWriting;
          })) {
    return ProfileAndAdditionalInformation(
        nsPrintfCString("*Could not copy profile from pid %u",
                        unsigned(profiler_current_process_id().ToNumber())));
  }
  MOZ_ASSERT(*(profileCString.Data() + len) == '\0',
             "We still expected a null at the end of the string buffer");

  return ProfileAndAdditionalInformation{std::move(profileCString),
                                         std::move(additionalInfo)};
}

}  // namespace mozilla
