/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessImpl.h"

#include "mozilla/GeckoArgs.h"

#if defined(XP_WIN)
#  include "nsExceptionHandler.h"
#endif

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "mozilla/CpuInfo.h"
#  include "mozilla/llama/LlamaRuntimeLinker.h"
#  include "mozilla/sandboxTarget.h"
#  include "WMF.h"
#  include "WMFDecoderModule.h"
#endif

#if defined(XP_OPENBSD) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#endif

#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX)
#  include "mozilla/MFCDMParent.h"
#endif

namespace mozilla::ipc {

UtilityProcessImpl::~UtilityProcessImpl() = default;

#if defined(XP_WIN)
/* static */
void UtilityProcessImpl::LoadLibraryOrCrash(LPCWSTR aLib) {
  // re-try a few times depending on the error we get ; inspired by both our
  // results on content process allocations as well as msys2:
  // https://github.com/git-for-windows/msys2-runtime/blob/b4fed42af089ab955286343835a97e287496b3f8/winsup/cygwin/autoload.cc#L323-L339

  const int kMaxRetries = 10;
  DWORD err;

  for (int i = 0; i < kMaxRetries; i++) {
    HMODULE module = ::LoadLibraryW(aLib);
    if (module) {
      return;
    }

    err = ::GetLastError();

    if (err != ERROR_NOACCESS && err != ERROR_DLL_INIT_FAILED) {
      break;
    }

    PR_Sleep(0);
  }

  switch (err) {
    /* case ERROR_ACCESS_DENIED: */
    /* case ERROR_BAD_EXE_FORMAT: */
    /* case ERROR_SHARING_VIOLATION: */
    case ERROR_MOD_NOT_FOUND:
    case ERROR_COMMITMENT_LIMIT:
      // We want to make it explicit in telemetry that this was in fact an
      // OOM condition, even though we could not detect it on our own
      CrashReporter::AnnotateOOMAllocationSize(1);
      break;

    default:
      break;
  }

  MOZ_CRASH_UNSAFE_PRINTF("Unable to preload module: 0x%lx", err);
}
#endif  // defined(XP_WIN)

bool UtilityProcessImpl::Init(int aArgc, char* aArgv[]) {
  Maybe<uint64_t> sandboxingKind = geckoargs::sSandboxingKind.Get(aArgc, aArgv);
  if (sandboxingKind.isNothing()) {
    return false;
  }

  if (*sandboxingKind >= SandboxingKind::COUNT) {
    return false;
  }

#if defined(MOZ_SANDBOX) && defined(XP_WIN)
  // We delay load winmm.dll so that its dependencies don't interfere with COM
  // initialization when win32k is locked down. We need to load it before we
  // lower the sandbox in processes where the policy will prevent loading.
  LoadLibraryOrCrash(L"winmm.dll");

  // Call this once before enabling the sandbox, it will cache its result
  // in a static variable.
  GetCpuFrequencyMHz();

  if (*sandboxingKind == SandboxingKind::GENERIC_UTILITY) {
    // Preload audio generic libraries required for ffmpeg only
    UtilityMediaServiceParent::GenericPreloadForSandbox();
  }

  if (*sandboxingKind == SandboxingKind::UTILITY_AUDIO_DECODING_WMF
#  ifdef MOZ_WMF_MEDIA_ENGINE
      || *sandboxingKind == SandboxingKind::MF_MEDIA_ENGINE_CDM
#  endif
  ) {
    UtilityMediaServiceParent::WMFPreloadForSandbox();
  }

  if (*sandboxingKind == SandboxingKind::HW_INFERENCE) {
    mozilla::llama::LlamaRuntimeLinker::Init();
  }

  // Go for it
  mozilla::SandboxTarget::Instance()->StartSandbox();
#elif defined(__OpenBSD__) && defined(MOZ_SANDBOX)
  if (*sandboxingKind != SandboxingKind::GENERIC_UTILITY) {
    StartOpenBSDSandbox(GeckoProcessType_Utility,
                        (SandboxingKind)*sandboxingKind);
  }
#endif

#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX)
  if (*sandboxingKind == MF_MEDIA_ENGINE_CDM) {
    Maybe<const char*> pluginPath = geckoargs::sPluginPath.Get(aArgc, aArgv);
    if (pluginPath) {
      MFCDMParent::SetWidevineL1Path(*pluginPath);
    } else {
      NS_WARNING("No Widevine L1 plugin for the utility process!");
    }
  }
#endif

  Maybe<const char*> parentBuildID =
      geckoargs::sParentBuildID.Get(aArgc, aArgv);
  if (parentBuildID.isNothing()) {
    return false;
  }

  if (!ProcessChild::InitPrefs(aArgc, aArgv)) {
    return false;
  }

#if defined(MOZ_MEMORY) && defined(DEBUG)
  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  MOZ_ASSERT(stats.opt_randomize_small,
             "Utility process should randomize small allocations");
#endif

  return mUtility->Init(TakeInitialEndpoint(), nsCString(*parentBuildID),
                        SandboxingKind(*sandboxingKind));
}

void UtilityProcessImpl::CleanUp() { NS_ShutdownXPCOM(nullptr); }

}  // namespace mozilla::ipc
