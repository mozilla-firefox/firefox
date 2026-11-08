/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PDMFactorySupport.h"

#include <mutex>

#include "mozilla/AppShutdown.h"
#include "mozilla/Atomics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsIXULRuntime.h"
#include "nsThreadUtils.h"

namespace mozilla {

namespace {

// Prefs read by `PDMFactory::CreatePDMs` and the per-process `Create*PDMs`
// helpers. Each `#ifdef` gate matches the corresponding wrapper in
// `StaticPrefList.yaml`. The trailing `nullptr` in each array is a sentinel
// required by `Preferences::RegisterCallbacks`, which walks the array until
// it sees a null pointer.

constexpr const char* kInvalidatingPrefs_CrossPlatform[] = {
    "media.use-blank-decoder",
    "media.gpu-process-decoder",
    "media.rdd-process.enabled",
    "media.utility-process.enabled",
    "media.allow-audio-non-utility",
    "media.prefer-non-ffvpx",
    "media.ffvpx-hw.enabled",
    "media.av1.enabled",
    "media.hevc.enabled",
    "media.gmp.decoder.enabled",
    "media.gmp.decoder.preferred",
    nullptr,
};

#ifdef MOZ_WMF
constexpr const char* kInvalidatingPrefs_WMF[] = {
    "media.wmf.enabled",
    "media.rdd-wmf.enabled",
    "media.wmf.media-engine.enabled",
    nullptr,
};
#endif

#ifdef MOZ_APPLEMEDIA
constexpr const char* kInvalidatingPrefs_AppleMedia[] = {
    "media.rdd-applemedia.enabled",
    nullptr,
};
#endif

#ifdef ANDROID
constexpr const char* kInvalidatingPrefs_Android[] = {
    "media.android-media-codec.preferred",
    "media.utility-android-media-codec.enabled",
    nullptr,
};
#endif

#ifdef MOZ_FFMPEG
constexpr const char* kInvalidatingPrefs_FFmpeg[] = {
    "media.ffmpeg.enabled",
    "media.rdd-ffmpeg.enabled",
    "media.hardware-video-decoding-vulkan.prefer-system-ffmpeg",
    nullptr,
};
#endif

// `sInstanceMutex` is `MOZ_UNANNOTATED` because the `ClearOnShutdown` clearer
// nulls `sInstance` without taking the lock. `Instance()` returns null past
// `AppShutdownConfirmed` so no new instance is created after the clearer
// runs.
static StaticMutex sInstanceMutex MOZ_UNANNOTATED;
static StaticRefPtr<PDMFactorySupport> sInstance;

// `sStale` must be atomic because `Invalidate()` writes it from arbitrary
// threads without taking the lock (see the comment on `Invalidate()`). The
// once-only registration gates live as function-static `bool`s in their
// callers.
static Atomic<bool> sStale{false};

}  // namespace

PDMFactorySupport::PDMFactorySupport() : mFactory(new PDMFactory()) {}

/* static */
media::DecodeSupportSet PDMFactorySupport::IsTypeSupported(
    const nsACString& aMimeType) {
  RefPtr<PDMFactorySupport> support = Instance();
  if (!support) {
    // Past `AppShutdownConfirmed`; report unsupported.
    return media::DecodeSupportSet{};
  }
  return support->SupportsMimeType(aMimeType);
}

/* static */
media::DecodeSupportSet PDMFactorySupport::IsSupported(
    const SupportDecoderParams& aParams,
    DecoderDoctorDiagnostics* aDiagnostics) {
  RefPtr<PDMFactorySupport> support = Instance();
  if (!support) {
    // Past `AppShutdownConfirmed`; report unsupported.
    return media::DecodeSupportSet{};
  }
  return support->Supports(aParams, aDiagnostics);
}

/* static */
RefPtr<PDMSupportsDecoderPromise> PDMFactorySupport::IsSupportedAsync(
    const SupportDecoderParams& aParams) {
  RefPtr<PDMFactorySupport> support = Instance();
  if (!support) {
    // Past `AppShutdownConfirmed`; report unsupported.
    return PDMSupportsDecoderPromise::CreateAndResolve(
        media::DecodeSupportSet{}, __func__);
  }
  return support->SupportsAsync(aParams);
}

/* static */
RefPtr<PDMFactorySupport> PDMFactorySupport::Instance() {
  // Refuse to build (or return) an instance once shutdown begins.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return nullptr;
  }

  // Install pref/gfxVar listeners before building the inner PDMFactory
  // so any change during construction sets sStale. Do this without holding
  // sInstanceMutex since the setup involves main thread initialization.
  if (!EnsureInvalidationListenersRegistered()) {
    return nullptr;
  }

  StaticMutexAutoLock lock(sInstanceMutex);
  // The registration hop above can cross into shutdown.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return nullptr;
  }
  // A pref or gfxVar change since the last build marks the snapshot stale.
  if (sStale.exchange(false)) {
    sInstance = nullptr;
  }
  if (!sInstance) {
    MOZ_ASSERT(gfx::gfxVars::IsInitialized());
    sInstance = new PDMFactorySupport();

    // Register `ClearOnShutdown` exactly once per process.
    static std::once_flag sShutdownRegistered;
    std::call_once(sShutdownRegistered, []() {
      if (NS_IsMainThread()) {
        ClearOnShutdown(&sInstance);
      } else {
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("PDMFactorySupport::ClearOnShutdown",
                                   []() { ClearOnShutdown(&sInstance); }));
      }
    });
  }
  return sInstance.get();
}

/* static */
void PDMFactorySupport::Invalidate() {
  // Lock-free on purpose: this is called from arbitrary threads (pref
  // callbacks and gfxVar listeners run on the thread that triggered the
  // change, which may already hold locks the gfx or prefs subsystems care
  // about). Acquiring `sInstanceMutex` here would risk deadlock against the
  // listener-registration path. The atomic store is the only synchronisation
  // needed: the next `Instance()` caller atomically exchanges the flag under
  // `sInstanceMutex` and rebuilds. RefPtrs already returned from earlier
  // `Instance()` calls remain valid and continue to answer with their
  // pre-invalidation snapshot for the lifetime of the reference.
  sStale = true;
}

/* static */
void PDMFactorySupport::OnInvalidatingPrefChanged(const char* /* aPref */,
                                                  void* /* aData */) {
  Invalidate();
}

/* static */
void PDMFactorySupport::OnInvalidatingGfxVarChanged() { Invalidate(); }

/* static */
bool PDMFactorySupport::EnsureInvalidationListenersRegistered() {
  static Atomic<bool> sListenersRegisteredComplete{false};
  if (sListenersRegisteredComplete) {
    return true;
  }

  static std::once_flag sListenersRegistered;

  auto registerOnMain = []() {
    MOZ_ASSERT(NS_IsMainThread());

    // Shutdown may have begun before this ran on the main thread.
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      return;
    }

    // Keep this synced with PDMFactory::EnsureInit(). Initialize gfxVars before
    // taking sInstanceMutex, so constructing the PDMFactory won't dispatch to
    // the main thread while the mutex is held.
    if (!gfx::gfxVars::IsInitialized()) {
      gfx::gfxVars::Initialize();
      (void)BrowserTabsRemoteAutostart();
    }

    Preferences::RegisterCallbacks(OnInvalidatingPrefChanged,
                                   kInvalidatingPrefs_CrossPlatform);
#ifdef MOZ_WMF
    Preferences::RegisterCallbacks(OnInvalidatingPrefChanged,
                                   kInvalidatingPrefs_WMF);
#endif
#ifdef MOZ_APPLEMEDIA
    Preferences::RegisterCallbacks(OnInvalidatingPrefChanged,
                                   kInvalidatingPrefs_AppleMedia);
#endif
#ifdef ANDROID
    Preferences::RegisterCallbacks(OnInvalidatingPrefChanged,
                                   kInvalidatingPrefs_Android);
#endif
#ifdef MOZ_FFMPEG
    Preferences::RegisterCallbacks(OnInvalidatingPrefChanged,
                                   kInvalidatingPrefs_FFmpeg);
#endif
    gfx::gfxVars::SetCanUseHardwareVideoDecodingListener(
        OnInvalidatingGfxVarChanged);
    gfx::gfxVars::SetUseAV1HwDecodeListener(OnInvalidatingGfxVarChanged);
    gfx::gfxVars::SetUseVP8HwDecodeListener(OnInvalidatingGfxVarChanged);
    gfx::gfxVars::SetUseVP9HwDecodeListener(OnInvalidatingGfxVarChanged);
    // `UseH264HwDecode` / `UseHEVCHwDecode` are only set on Linux/Android/
    // Windows today; registering elsewhere is a harmless no-op.
    gfx::gfxVars::SetUseH264HwDecodeListener(OnInvalidatingGfxVarChanged);
    gfx::gfxVars::SetUseHEVCHwDecodeListener(OnInvalidatingGfxVarChanged);

    sListenersRegisteredComplete = true;
  };

  if (NS_IsMainThread()) {
    std::call_once(sListenersRegistered, registerOnMain);
    return sListenersRegisteredComplete;
  }

  nsCOMPtr<nsIEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  if (!mainTarget) {
    return sListenersRegisteredComplete;
  }
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "PDMFactorySupport::EnsureInvalidationListenersRegistered",
      [registerOnMain]() {
        std::call_once(sListenersRegistered, registerOnMain);
      });
  SyncRunnable::DispatchToThread(mainTarget, runnable);
  return sListenersRegisteredComplete;
}

}  // namespace mozilla
