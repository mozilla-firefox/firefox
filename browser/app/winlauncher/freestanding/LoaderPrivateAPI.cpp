/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "LoaderPrivateAPI.h"

#include "mozilla/Assertions.h"
#include "../DllBlocklistInit.h"
#include "../ErrorHandler.h"
#include "SharedSection.h"

using GlobalInitializerFn = void(__cdecl*)(void);

// Allocation of static initialization section for the freestanding library
#pragma section(".freestd$a", read)
__declspec(allocate(
    ".freestd$a")) static const GlobalInitializerFn FreeStdStart =
    reinterpret_cast<GlobalInitializerFn>(0);

#pragma section(".freestd$z", read)
__declspec(allocate(".freestd$z")) static const GlobalInitializerFn FreeStdEnd =
    reinterpret_cast<GlobalInitializerFn>(0);

namespace mozilla {
namespace freestanding {

static RTL_RUN_ONCE gRunOnce = RTL_RUN_ONCE_INIT;

// The contract for this callback is identical to the InitOnceCallback from
// Win32 land; we're just using ntdll-layer types instead.
static ULONG NTAPI DoOneTimeInit(PRTL_RUN_ONCE aRunOnce, PVOID aParameter,
                                 PVOID* aContext) {
  // Invoke every static initializer in the .freestd section
  const GlobalInitializerFn* cur = &FreeStdStart + 1;
  while (cur < &FreeStdEnd) {
    if (*cur) {
      (*cur)();
    }

    ++cur;
  }

  return TRUE;
}

/**
 * This observer is only used until the mozglue observer connects itself.
 * All we do here is accumulate the module loads into a vector.
 * As soon as mozglue connects, we call |Forward| on mozglue's LoaderObserver
 * to pass our vector on for further processing. This object then becomes
 * defunct.
 */
class MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS DefaultLoaderObserver final
    : public nt::LoaderObserver {
 public:
  constexpr DefaultLoaderObserver() : mModuleLoads(nullptr) {}

  void OnBeginDllLoad(void** aContext,
                      PCUNICODE_STRING aRequestedDllName) final {}
  bool SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                        PHANDLE aOutHandle) final {
    return false;
  }
  void OnEndDllLoad(void* aContext, NTSTATUS aNtStatus,
                    ModuleLoadInfo&& aModuleLoadInfo) final;
  void Forward(nt::LoaderObserver* aNext) final;
  void OnForward(ModuleLoadInfoVec&& aInfo) final {
    MOZ_ASSERT_UNREACHABLE("Not valid in freestanding::DefaultLoaderObserver");
  }

 private:
  mozilla::nt::SRWLock mLock;
  ModuleLoadInfoVec* mModuleLoads;
};

class MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS LoaderPrivateAPIImp final
    : public LoaderPrivateAPI {
 public:
  // LoaderAPI
  ModuleLoadInfo ConstructAndNotifyBeginDllLoad(
      void** aContext, PCUNICODE_STRING aRequestedDllName) final;
  bool SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                        PHANDLE aOutHandle) final;
  void NotifyEndDllLoad(void* aContext, NTSTATUS aLoadNtStatus,
                        ModuleLoadInfo&& aModuleLoadInfo) final;
  nt::AllocatedUnicodeString GetSectionName(void* aSectionAddr) final;
  nt::LoaderAPI::InitDllBlocklistOOPFnPtr GetDllBlocklistInitFn() final;
  nt::LoaderAPI::HandleLauncherErrorFnPtr GetHandleLauncherErrorFn() final;
  nt::SharedSection* GetSharedSection() final;

  // LoaderPrivateAPI
  void NotifyBeginDllLoad(void** aContext,
                          PCUNICODE_STRING aRequestedDllName) final;
  void NotifyBeginDllLoad(ModuleLoadInfo& aModuleLoadInfo, void** aContext,
                          PCUNICODE_STRING aRequestedDllName) final;
  void SetObserver(nt::LoaderObserver* aNewObserver) final;
  bool IsDefaultObserver() const final;
  nt::MemorySectionNameBuf GetSectionNameBuffer(void* aSectionAddr) final;
};

static void Init() {
  DebugOnly<NTSTATUS> ntStatus =
      ::RtlRunOnceExecuteOnce(&gRunOnce, &DoOneTimeInit, nullptr, nullptr);
  MOZ_ASSERT(NT_SUCCESS(ntStatus));
}

}  // namespace freestanding
}  // namespace mozilla

static mozilla::freestanding::DefaultLoaderObserver gDefaultObserver;
static mozilla::freestanding::LoaderPrivateAPIImp gPrivateAPI;

static mozilla::nt::SRWLock gLoaderObserverLock;
static mozilla::nt::LoaderObserver* gLoaderObserver = &gDefaultObserver;
static CONDITION_VARIABLE gLoaderObserverNoOngoingLoadsCV =
    CONDITION_VARIABLE_INIT;
static mozilla::Atomic<uint32_t> gLoaderObserverOngoingLoadsCount(0);

namespace mozilla {
namespace freestanding {

LoaderPrivateAPI& gLoaderPrivateAPI = gPrivateAPI;

void DefaultLoaderObserver::OnEndDllLoad(void* aContext, NTSTATUS aNtStatus,
                                         ModuleLoadInfo&& aModuleLoadInfo) {
  // If the DLL load failed, or if the DLL was loaded by a previous request
  // and thus was not mapped by this request, we do not save the ModuleLoadInfo.
  if (!aModuleLoadInfo.HasCompatibleLayout() || !NT_SUCCESS(aNtStatus) ||
      !aModuleLoadInfo.WasMapped()) {
    return;
  }

  nt::AutoExclusiveLock lock(mLock);
  if (!mModuleLoads) {
    mModuleLoads = RtlNew<ModuleLoadInfoVec>();
    if (!mModuleLoads) {
      return;
    }
  }

  (void)mModuleLoads->emplaceBack(
      std::forward<ModuleLoadInfo>(aModuleLoadInfo));
}

/**
 * Pass mModuleLoads's data off to |aNext| for further processing.
 */
void DefaultLoaderObserver::Forward(nt::LoaderObserver* aNext) {
  MOZ_ASSERT(aNext);
  if (!aNext) {
    return;
  }

  ModuleLoadInfoVec* moduleLoads = nullptr;

  {  // Scope for lock
    nt::AutoExclusiveLock lock(mLock);
    moduleLoads = mModuleLoads;
    mModuleLoads = nullptr;
  }

  if (!moduleLoads) {
    return;
  }

  aNext->OnForward(std::move(*moduleLoads));
  RtlDelete(moduleLoads);
}

ModuleLoadInfo LoaderPrivateAPIImp::ConstructAndNotifyBeginDllLoad(
    void** aContext, PCUNICODE_STRING aRequestedDllName) {
  ModuleLoadInfo loadInfo(aRequestedDllName);

  NotifyBeginDllLoad(loadInfo, aContext, aRequestedDllName);

  return loadInfo;
}

bool LoaderPrivateAPIImp::SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                                           PHANDLE aOutHandle) {
  // This method should only be called between NotifyBeginDllLoad and
  // NotifyEndDllLoad, so it is already protected against gLoaderObserver
  // change, through gLoaderObserverOngoingLoadsCount.
  MOZ_RELEASE_ASSERT(gLoaderObserverOngoingLoadsCount);

  return gLoaderObserver->SubstituteForLSP(aLSPLeafName, aOutHandle);
}

void LoaderPrivateAPIImp::NotifyEndDllLoad(void* aContext,
                                           NTSTATUS aLoadNtStatus,
                                           ModuleLoadInfo&& aModuleLoadInfo) {
  // If the ModuleLoadInfo came from an incompatible mozglue.dll then don't use
  // it.  Note that the OnEndDllLoad method is in the mozglue's DLL, so using
  // it there is fine.
  if (aModuleLoadInfo.HasCompatibleLayout()) {
    aModuleLoadInfo.SetEndLoadTimeStamp();

    if (NT_SUCCESS(aLoadNtStatus)) {
      aModuleLoadInfo.CaptureBacktrace();
    }
  }

  // This method should only be called after a matching call to
  // NotifyBeginDllLoad, so it is already protected against gLoaderObserver
  // change, through gLoaderObserverOngoingLoadsCount.

  // We need to notify the observer that the DLL load has ended even when
  // |aLoadNtStatus| indicates a failure. This is to ensure that any resources
  // acquired by the observer during OnBeginDllLoad are cleaned up.
  gLoaderObserver->OnEndDllLoad(aContext, aLoadNtStatus,
                                std::move(aModuleLoadInfo));

  // Prevent the wake being lost between SetObserver's check and sleep.
  nt::AutoSharedLock lock(gLoaderObserverLock);
  auto previousValue = gLoaderObserverOngoingLoadsCount--;
  MOZ_RELEASE_ASSERT(previousValue);
  if (previousValue == 1) {
    ::RtlWakeAllConditionVariable(&gLoaderObserverNoOngoingLoadsCV);
  }
}

nt::AllocatedUnicodeString LoaderPrivateAPIImp::GetSectionName(
    void* aSectionAddr) {
  const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);

  nt::MemorySectionNameBuf buf;
  NTSTATUS ntStatus =
      ::NtQueryVirtualMemory(kCurrentProcess, aSectionAddr, MemorySectionName,
                             &buf, sizeof(buf), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    return nt::AllocatedUnicodeString();
  }

  return nt::AllocatedUnicodeString(&buf.mSectionFileName);
}

nt::LoaderAPI::InitDllBlocklistOOPFnPtr
LoaderPrivateAPIImp::GetDllBlocklistInitFn() {
  return &InitializeDllBlocklistOOP;
}

nt::LoaderAPI::HandleLauncherErrorFnPtr
LoaderPrivateAPIImp::GetHandleLauncherErrorFn() {
  return &HandleLauncherError;
}

nt::SharedSection* LoaderPrivateAPIImp::GetSharedSection() {
  return &gSharedSection;
}

nt::MemorySectionNameBuf LoaderPrivateAPIImp::GetSectionNameBuffer(
    void* aSectionAddr) {
  const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);

  nt::MemorySectionNameBuf buf;
  NTSTATUS ntStatus =
      ::NtQueryVirtualMemory(kCurrentProcess, aSectionAddr, MemorySectionName,
                             &buf, sizeof(buf), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    return nt::MemorySectionNameBuf();
  }

  return buf;
}

void LoaderPrivateAPIImp::NotifyBeginDllLoad(
    void** aContext, PCUNICODE_STRING aRequestedDllName) {
  nt::AutoSharedLock lock(gLoaderObserverLock);
  ++gLoaderObserverOngoingLoadsCount;
  gLoaderObserver->OnBeginDllLoad(aContext, aRequestedDllName);
}

void LoaderPrivateAPIImp::NotifyBeginDllLoad(
    ModuleLoadInfo& aModuleLoadInfo, void** aContext,
    PCUNICODE_STRING aRequestedDllName) {
  NotifyBeginDllLoad(aContext, aRequestedDllName);
  aModuleLoadInfo.SetBeginLoadTimeStamp();
}

void LoaderPrivateAPIImp::SetObserver(nt::LoaderObserver* aNewObserver) {
  nt::LoaderObserver* prevLoaderObserver = nullptr;

  nt::AutoExclusiveLock lock(gLoaderObserverLock);
  while (gLoaderObserverOngoingLoadsCount) {
    NTSTATUS status = ::RtlSleepConditionVariableSRW(
        &gLoaderObserverNoOngoingLoadsCV, &gLoaderObserverLock, nullptr, 0);
    MOZ_RELEASE_ASSERT(NT_SUCCESS(status) && status != STATUS_TIMEOUT);
  }

  MOZ_ASSERT(aNewObserver);
  if (!aNewObserver) {
    // This is unlikely, but we always want a valid observer, so use the
    // gDefaultObserver if necessary.
    gLoaderObserver = &gDefaultObserver;
    return;
  }

  prevLoaderObserver = gLoaderObserver;
  gLoaderObserver = aNewObserver;

  MOZ_ASSERT(prevLoaderObserver);
  if (!prevLoaderObserver) {
    return;
  }

  // Now that we have a new observer, the previous observer must forward its
  // data on to the new observer for processing.
  prevLoaderObserver->Forward(aNewObserver);
}

bool LoaderPrivateAPIImp::IsDefaultObserver() const {
  nt::AutoSharedLock lock(gLoaderObserverLock);
  return gLoaderObserver == &gDefaultObserver;
}

void EnsureInitialized() { Init(); }

}  // namespace freestanding
}  // namespace mozilla
