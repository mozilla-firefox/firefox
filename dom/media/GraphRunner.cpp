/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "GraphRunner.h"

#include "GraphDriver.h"
#include "MediaTrackGraph.h"
#include "MediaTrackGraphImpl.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPriority.h"
#include "prthread.h"
#include "Tracing.h"
#include "mozilla/dom/WorkletThread.h"
#include "audio_thread_priority.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidProcess.h"
#endif  // MOZ_WIDGET_ANDROID

namespace mozilla {

GraphRunner::GraphRunner(MediaTrackGraphImpl* aGraph,
                         already_AddRefed<nsIThread> aThread)
    : Runnable("GraphRunner"),
      mMonitor("GraphRunner::mMonitor"),
      mGraph(aGraph),
      mThreadState(ThreadState::Wait),
      mThread(aThread) {
  mThread->Dispatch(do_AddRef(this));
}

GraphRunner::~GraphRunner() {
  MOZ_ASSERT(mThreadState == ThreadState::Shutdown);
}

/* static */
already_AddRefed<GraphRunner> GraphRunner::Create(MediaTrackGraphImpl* aGraph) {
  nsCOMPtr<nsIThread> thread;
  nsIThreadManager::ThreadCreationOptions options = {
      .stackSize = mozilla::dom::WorkletThread::StackSize()};
  if (NS_WARN_IF(NS_FAILED(NS_NewNamedThread(
          "GraphRunner", getter_AddRefs(thread), nullptr, options)))) {
    return nullptr;
  }
  nsCOMPtr<nsISupportsPriority> supportsPriority = do_QueryInterface(thread);
  MOZ_ASSERT(supportsPriority);
  MOZ_ALWAYS_SUCCEEDS(
      supportsPriority->SetPriority(nsISupportsPriority::PRIORITY_HIGHEST));

  return do_AddRef(new GraphRunner(aGraph, thread.forget()));
}

void GraphRunner::Shutdown() {
  {
    MonitorAutoLock lock(mMonitor);
    MOZ_ASSERT(mThreadState == ThreadState::Wait);
    mThreadState = ThreadState::Shutdown;
    mMonitor.Notify();
  }
  mThread->Shutdown();
}

auto GraphRunner::OneIteration(GraphTime aStateTime, GraphTime aIterationEnd,
                               AudioMixer* aMixer) -> IterationResult {
  TRACE("GraphRunner::OneIteration");

  MonitorAutoLock lock(mMonitor);
  MOZ_ASSERT(mThreadState == ThreadState::Wait);
  mIterationState = Some(IterationState(aStateTime, aIterationEnd, aMixer));

#ifdef DEBUG
  if (const auto* audioDriver =
          mGraph->CurrentDriver()->AsAudioCallbackDriver()) {
    mAudioDriverThreadId = audioDriver->ThreadId();
  } else if (const auto* clockDriver =
                 mGraph->CurrentDriver()->AsSystemClockDriver()) {
    mClockDriverThread = clockDriver->Thread();
  } else {
    MOZ_CRASH("Unknown GraphDriver");
  }
#endif
  // Signal that mIterationState was updated
  mThreadState = ThreadState::Run;
  mMonitor.Notify();
  // Wait for mIterationResult to update
  do {
    mMonitor.Wait();
  } while (mThreadState == ThreadState::Run);

#ifdef DEBUG
  mAudioDriverThreadId = std::thread::id();
  mClockDriverThread = nullptr;
#endif

  mIterationState = Nothing();

  IterationResult result = std::move(mIterationResult);
  mIterationResult = IterationResult();
  return result;
}

#ifdef MOZ_WIDGET_ANDROID
namespace {
void PromoteRenderingThreadAndroid() {
  MOZ_LOG(gMediaTrackGraphLog, LogLevel::Debug,
          ("GraphRunner default thread priority: %d",
           java::sdk::Process::GetThreadPriority(java::sdk::Process::MyTid())));
  java::sdk::Process::SetThreadPriority(
      java::sdk::Process::THREAD_PRIORITY_URGENT_AUDIO);
  MOZ_LOG(gMediaTrackGraphLog, LogLevel::Debug,
          ("GraphRunner promoted thread priority: %d",
           java::sdk::Process::GetThreadPriority(java::sdk::Process::MyTid())));
}
};      // namespace
#endif  // MOZ_WIDGET_ANDROID

NS_IMETHODIMP GraphRunner::Run() {
#ifndef XP_LINUX
  atp_handle* handle =
      atp_promote_current_thread_to_real_time(0, mGraph->GraphRate());
#endif

#ifdef MOZ_WIDGET_ANDROID
  PromoteRenderingThreadAndroid();
#endif  // MOZ_WIDGET_ANDROID

  nsCOMPtr<nsIThreadInternal> threadInternal = do_QueryInterface(mThread);
  threadInternal->SetObserver(mGraph);

  MonitorAutoLock lock(mMonitor);
  while (true) {
    while (mThreadState == ThreadState::Wait) {
      mMonitor.Wait();  // Wait for mIterationState to update or for shutdown
    }
    if (mThreadState == ThreadState::Shutdown) {
      break;
    }
    MOZ_DIAGNOSTIC_ASSERT(mIterationState.isSome());
    TRACE("GraphRunner::Run");
    mIterationResult = mGraph->OneIterationImpl(mIterationState->StateTime(),
                                                mIterationState->IterationEnd(),
                                                mIterationState->Mixer());
    // Signal that mIterationResult was updated
    mThreadState = ThreadState::Wait;
    mMonitor.Notify();
  }

#ifndef XP_LINUX
  if (handle) {
    atp_demote_current_thread_from_real_time(handle);
  }
#endif

  return NS_OK;
}

bool GraphRunner::OnThread() const { return mThread->IsOnCurrentThread(); }

#ifdef DEBUG
bool GraphRunner::InDriverIteration(const GraphDriver* aDriver) const {
  if (!OnThread()) {
    return false;
  }

  if (const auto* audioDriver = aDriver->AsAudioCallbackDriver()) {
    return audioDriver->ThreadId() == mAudioDriverThreadId;
  }

  if (const auto* clockDriver = aDriver->AsSystemClockDriver()) {
    return clockDriver->Thread() == mClockDriverThread;
  }

  MOZ_CRASH("Unknown driver");
}
#endif

}  // namespace mozilla
