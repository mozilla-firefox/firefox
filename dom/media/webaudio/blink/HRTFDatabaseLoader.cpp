/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "HRTFDatabaseLoader.h"

#include <mutex>

#include "GeckoProfiler.h"
#include "HRTFDatabase.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "nsThreadUtils.h"

using namespace mozilla;

namespace WebCore {

// Singleton
nsTHashtable<HRTFDatabaseLoader::LoaderByRateEntry>*
    HRTFDatabaseLoader::s_loaderMap = nullptr;

size_t HRTFDatabaseLoader::sizeOfLoaders(mozilla::MallocSizeOf aMallocSizeOf) {
  return s_loaderMap ? s_loaderMap->SizeOfIncludingThis(aMallocSizeOf) : 0;
}

already_AddRefed<HRTFDatabaseLoader>
HRTFDatabaseLoader::createAndLoadAsynchronouslyIfNecessary(float sampleRate) {
  MOZ_ASSERT(NS_IsMainThread());

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads)) {
    NS_ERROR("HRTFDatabaseLoader: script running after shutdown");
    return nullptr;
  }

  // The loader threads release their loader to the main thread, so they must
  // be joined before the main thread goes away.
  static std::once_flag sShutdownRegistered;
  std::call_once(sShutdownRegistered, [] {
    RunOnShutdown(shutdown, ShutdownPhase::XPCOMShutdownThreads);
  });

  RefPtr<HRTFDatabaseLoader> loader;

  if (!s_loaderMap) {
    s_loaderMap = new nsTHashtable<LoaderByRateEntry>();
  }

  LoaderByRateEntry* entry = s_loaderMap->PutEntry(sampleRate);
  loader = entry->mLoader;
  if (loader) {  // existing entry
    MOZ_ASSERT(sampleRate == loader->databaseSampleRate());
    return loader.forget();
  }

  loader = new HRTFDatabaseLoader(sampleRate);
  entry->mLoader = loader;

  loader->loadAsynchronously();

  return loader.forget();
}

HRTFDatabaseLoader::HRTFDatabaseLoader(float sampleRate)
    : m_refCnt(0),
      m_threadLock("HRTFDatabaseLoader"),
      m_databaseLoaderThread(nullptr),
      m_databaseSampleRate(sampleRate),
      m_databaseLoaded(false) {
  MOZ_ASSERT(NS_IsMainThread());
}

HRTFDatabaseLoader::~HRTFDatabaseLoader() {
  MOZ_ASSERT(NS_IsMainThread());

  waitForLoaderThreadCompletion();
  m_hrtfDatabase.reset();

  if (s_loaderMap) {
    // Remove ourself from the map.
    s_loaderMap->RemoveEntry(m_databaseSampleRate);
    if (s_loaderMap->Count() == 0) {
      delete s_loaderMap;
      s_loaderMap = nullptr;
    }
  }
}

size_t HRTFDatabaseLoader::sizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t amount = aMallocSizeOf(this);

  // NB: Need to make sure we're not competing with the loader thread.
  const_cast<HRTFDatabaseLoader*>(this)->waitForLoaderThreadCompletion();

  if (m_hrtfDatabase) {
    amount += m_hrtfDatabase->sizeOfIncludingThis(aMallocSizeOf);
  }

  return amount;
}

class HRTFDatabaseLoader::ProxyReleaseEvent final : public Runnable {
 public:
  explicit ProxyReleaseEvent(HRTFDatabaseLoader* loader)
      : mozilla::Runnable("WebCore::HRTFDatabaseLoader::ProxyReleaseEvent"),
        mLoader(loader) {}
  NS_IMETHOD Run() override {
    mLoader->MainThreadRelease();
    return NS_OK;
  }

 private:
  // Ownership transferred by ProxyRelease
  HRTFDatabaseLoader* MOZ_OWNING_REF mLoader;
};

void HRTFDatabaseLoader::ProxyRelease() {
  nsCOMPtr<nsIEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  if (MOZ_LIKELY(mainTarget)) {
    RefPtr<ProxyReleaseEvent> event = new ProxyReleaseEvent(this);
    DebugOnly<nsresult> rv = mainTarget->Dispatch(event, NS_DISPATCH_NORMAL);
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Failed to dispatch release event");
  } else {
    // Should be in XPCOM shutdown.
    MOZ_ASSERT(NS_IsMainThread(), "Main thread is not available for dispatch.");
    MainThreadRelease();
  }
}

void HRTFDatabaseLoader::MainThreadRelease() {
  MOZ_ASSERT(NS_IsMainThread());
  int count = --m_refCnt;
  MOZ_ASSERT(count >= 0, "extra release");
  NS_LOG_RELEASE(this, count, "HRTFDatabaseLoader");
  if (count == 0) {
    // It is safe to delete here as the first reference can only be added
    // on this (main) thread.
    delete this;
  }
}

// Asynchronously load the database in this thread.
static void databaseLoaderEntry(void* threadData) {
  AUTO_PROFILER_REGISTER_THREAD("HRTFDatabaseLdr");
  NS_SetCurrentThreadName("HRTFDatabaseLdr");

  HRTFDatabaseLoader* loader =
      reinterpret_cast<HRTFDatabaseLoader*>(threadData);
  MOZ_ASSERT(loader);
  loader->load();
}

void HRTFDatabaseLoader::load() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!m_hrtfDatabase.get(), "Called twice");
  // Load the default HRTF database.
  m_hrtfDatabase = HRTFDatabase::create(m_databaseSampleRate);
  m_databaseLoaded = true;
  // Notifies the main thread of completion.  See loadAsynchronously().
  Release();
}

void HRTFDatabaseLoader::loadAsynchronously() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(m_refCnt, "Must not be called before a reference is added");

  // Add a reference so that the destructor won't run and wait for the
  // loader thread, until load() has completed.
  AddRef();

  MutexAutoLock locker(m_threadLock);

  MOZ_ASSERT(!m_hrtfDatabase.get() && !m_databaseLoaderThread, "Called twice");
  // Start the asynchronous database loading process.
  m_databaseLoaderThread = PR_CreateThread(
      PR_USER_THREAD, databaseLoaderEntry, this, PR_PRIORITY_NORMAL,
      PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
}

bool HRTFDatabaseLoader::isLoaded() const { return m_hrtfDatabase.get(); }

void HRTFDatabaseLoader::waitForLoaderThreadCompletion() {
  MutexAutoLock locker(m_threadLock);

  // waitForThreadCompletion() should not be called twice for the same thread.
  if (m_databaseLoaderThread) {
    DebugOnly<PRStatus> status = PR_JoinThread(m_databaseLoaderThread);
    MOZ_ASSERT(status == PR_SUCCESS, "PR_JoinThread failed");
  }
  m_databaseLoaderThread = nullptr;
}

void HRTFDatabaseLoader::shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  if (s_loaderMap) {
    // Set s_loaderMap to nullptr so that the hashtable is not modified on
    // reference release during enumeration.
    nsTHashtable<LoaderByRateEntry>* loaderMap = s_loaderMap;
    s_loaderMap = nullptr;
    for (auto iter = loaderMap->Iter(); !iter.Done(); iter.Next()) {
      iter.Get()->mLoader->waitForLoaderThreadCompletion();
    }
    delete loaderMap;
  }
}

}  // namespace WebCore
