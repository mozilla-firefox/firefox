/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRShMem.h"

#ifdef MOZILLA_INTERNAL_API
#  include "nsString.h"
#  include "nsXULAppAPI.h"
#endif

#include "gfxVRMutex.h"

#if defined(XP_MACOSX)
#  include <fcntl.h> /* For O_* constants */
#  include <sys/mman.h>
#  include <sys/stat.h> /* For mode constants */
#elif defined(MOZ_WIDGET_ANDROID)
#  include "GeckoVRManager.h"
#endif

#if !defined(XP_WIN)
#  include <unistd.h>  // for ::sleep
#endif

using namespace mozilla::gfx;

#ifdef XP_WIN
static const char* kShmemName = "moz.gecko.vr_ext." SHMEM_VERSION;
static LPCTSTR kMutexName = TEXT("mozilla::vr::ShmemMutex" SHMEM_VERSION);
#elif defined(XP_MACOSX)
static const char* kShmemName = "/moz.gecko.vr_ext." SHMEM_VERSION;
#endif  //  XP_WIN

#if !defined(MOZ_WIDGET_ANDROID)
namespace {
void YieldThread() {
#  if defined(XP_WIN)
  ::Sleep(0);
#  else
  ::sleep(0);
#  endif
}
}  // anonymous namespace
#endif  // !defined(MOZ_WIDGET_ANDROID)

VRShMem::VRShMem(volatile VRExternalShmem* aShmem, bool aRequiresMutex)
    : mExternalShmem(aShmem),
      mIsSharedExternalShmem(aShmem != nullptr)
#if defined(XP_WIN)
      ,
      mRequiresMutex(aRequiresMutex)
#endif
#if defined(XP_MACOSX)
      ,
      mShmemFD(0)
#elif defined(XP_WIN)
      ,
      mShmemFile(nullptr),
      mMutex(nullptr)
#endif
{
  // Regarding input parameters,
  // - aShmem is null for VRManager or for VRService in multi-proc
  // - aShmem is !null for VRService in-proc (i.e., no VR proc)
}

// Note: This function should only be called for in-proc scenarios, where the
// shared memory is only shared within the same proc (rather than across
// processes). Also, this local heap memory's lifetime is tied to the class.
// Callers to this must ensure that its reference doesn't outlive the owning
// VRShMem instance.
volatile VRExternalShmem* VRShMem::GetExternalShmem() const {
#if defined(XP_MACOSX)
  MOZ_RELEASE_ASSERT(mShmemFD == 0);
#elif defined(XP_WIN)
  MOZ_RELEASE_ASSERT(mShmemFile == nullptr);
#endif
  return mExternalShmem;
}

bool VRShMem::IsDisplayStateShutdown() const {
  // adapted from VRService::Refresh
  // Does this need the mutex for getting .shutdown?
  return mExternalShmem != nullptr &&
         mExternalShmem->state.displayState.shutdown;
}

// This method returns true when there is a Shmem struct allocated and
// when there is a shmem handle from the OS. This implies that the struct
// is mapped to shared memory rather than being allocated on the heap by
// this process.
bool VRShMem::IsCreatedOnSharedMemory() const {
#if defined(XP_MACOSX)
  return HasExternalShmem() && (mShmemFD != 0);
#elif defined(XP_WIN)
  return HasExternalShmem() && (mShmemFile != nullptr);
#else
  // VRShMem does not support system shared memory on remaining platformss
  return false;
#endif
}

// CreateShMem allocates system shared memory for mExternalShmem and
// synchronization primitives to protect it.
// Callers/Processes to CreateShMem should followup with CloseShMem
void VRShMem::CreateShMem(bool aCreateOnSharedMemory) {
  if (HasExternalShmem()) {
    MOZ_RELEASE_ASSERT(mIsSharedExternalShmem && !IsCreatedOnSharedMemory());
    return;
  }
#if defined(XP_WIN)
  if (mMutex == nullptr) {
    mMutex = CreateMutex(nullptr,      // default security descriptor
                         false,        // mutex not owned
                         kMutexName);  // object name
    if (mMutex == nullptr) {
#  ifdef MOZILLA_INTERNAL_API
      nsAutoCString msg;
      msg.AppendPrintf("VRManager CreateMutex error \"%lu\".", GetLastError());
      NS_WARNING(msg.get());
#  endif
      MOZ_ASSERT(false);
      return;
    }
    // At xpcshell extension tests, it creates multiple VRManager
    // instances in plug-contrainer.exe. It causes GetLastError() return
    // `ERROR_ALREADY_EXISTS`. However, even though `ERROR_ALREADY_EXISTS`, it
    // still returns the same mutex handle.
    //
    // https://docs.microsoft.com/en-us/windows/desktop/api/synchapi/nf-synchapi-createmutexa
    MOZ_ASSERT(GetLastError() == 0 || GetLastError() == ERROR_ALREADY_EXISTS);
  }
#endif  // XP_WIN
#if !defined(MOZ_WIDGET_ANDROID)
  // The VR Service accesses all hardware from a separate process
  // and replaces the other VRManager when enabled.
  // If the VR process is not enabled, create an in-process VRService.
  if (!aCreateOnSharedMemory) {
    MOZ_RELEASE_ASSERT(mExternalShmem == nullptr);
    // If the VR process is disabled, attempt to create a
    // VR service within the current process on the heap
    mExternalShmem = new VRExternalShmem();
    ClearShMem();
    return;
  }
#endif

  MOZ_RELEASE_ASSERT(aCreateOnSharedMemory);

#if defined(XP_MACOSX)
  if (mShmemFD == 0) {
    mShmemFD =
        shm_open(kShmemName, O_RDWR, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
  }
  if (mShmemFD <= 0) {
    mShmemFD = 0;
    return;
  }

  struct stat sb;
  fstat(mShmemFD, &sb);
  off_t length = sb.st_size;
  if (length < (off_t)sizeof(VRExternalShmem)) {
    // TODO - Implement logging (Bug 1558912)
    CloseShMem();
    return;
  }

  mExternalShmem = (VRExternalShmem*)mmap(
      nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, mShmemFD, 0);
  if (mExternalShmem == MAP_FAILED) {
    // TODO - Implement logging (Bug 1558912)
    mExternalShmem = nullptr;
    CloseShMem();
    return;
  }

#elif defined(XP_WIN)
  if (mShmemFile == nullptr) {
    mShmemFile =
        CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                           sizeof(VRExternalShmem), kShmemName);
    MOZ_ASSERT(GetLastError() == 0 || GetLastError() == ERROR_ALREADY_EXISTS);
    MOZ_RELEASE_ASSERT(mShmemFile);
    if (mShmemFile == nullptr) {
      // TODO - Implement logging (Bug 1558912)
      CloseShMem();
      return;
    }
  }

  LARGE_INTEGER length;
  length.QuadPart = sizeof(VRExternalShmem);
  mExternalShmem = (VRExternalShmem*)MapViewOfFile(
      mShmemFile,           // handle to map object
      FILE_MAP_ALL_ACCESS,  // read/write permission
      0, 0, length.QuadPart);

  if (mExternalShmem == nullptr) {
    // TODO - Implement logging (Bug 1558912)
    CloseShMem();
    return;
  }
#elif defined(MOZ_WIDGET_ANDROID)
  MOZ_RELEASE_ASSERT(false,
                     "CreateShMem should not be called for Android. Use "
                     "CreateShMemForAndroid instead");
#endif
}

// This function sets mExternalShmem in the Android/GeckoView
// scenarios where the host creates it in-memory and VRShMem
// accesses it via GeckVRManager.
void VRShMem::CreateShMemForAndroid() {
#if defined(MOZ_WIDGET_ANDROID) && defined(MOZILLA_INTERNAL_API)
  mExternalShmem =
      (VRExternalShmem*)mozilla::GeckoVRManager::GetExternalContext();
  if (!mExternalShmem) {
    return;
  } else {
    mIsSharedExternalShmem = true;
  }

  int32_t version = -1;
  int32_t size = 0;
  if (pthread_mutex_lock((pthread_mutex_t*)&(mExternalShmem->systemMutex)) ==
      0) {
    version = mExternalShmem->version;
    size = mExternalShmem->size;
    pthread_mutex_unlock((pthread_mutex_t*)&(mExternalShmem->systemMutex));
  } else {
    return;
  }
  if (version != kVRExternalVersion) {
    mExternalShmem = nullptr;
    return;
  }
  if (size != sizeof(VRExternalShmem)) {
    mExternalShmem = nullptr;
    return;
  }
#endif
}

void VRShMem::ClearShMem() {
  if (mExternalShmem != nullptr) {
#ifdef MOZILLA_INTERNAL_API
    // VRExternalShmem is asserted to be POD
    mExternalShmem->Clear();
#else
    memset((void*)mExternalShmem, 0, sizeof(VRExternalShmem));
#endif
  }
}

// The cleanup corresponding to CreateShMem
void VRShMem::CloseShMem() {
#if !defined(MOZ_WIDGET_ANDROID)
  if (!IsCreatedOnSharedMemory()) {
    MOZ_RELEASE_ASSERT(!mIsSharedExternalShmem);
    if (mExternalShmem) {
      delete mExternalShmem;
      mExternalShmem = nullptr;
    }
    return;
  }
#endif
#if defined(XP_MACOSX)
  if (mExternalShmem) {
    munmap((void*)mExternalShmem, sizeof(VRExternalShmem));
    mExternalShmem = nullptr;
  }
  if (mShmemFD) {
    close(mShmemFD);
    mShmemFD = 0;
  }
#elif defined(XP_WIN)
  if (mExternalShmem) {
    UnmapViewOfFile((void*)mExternalShmem);
    mExternalShmem = nullptr;
  }
  if (mShmemFile) {
    CloseHandle(mShmemFile);
    mShmemFile = nullptr;
  }
#elif defined(MOZ_WIDGET_ANDROID)
  mExternalShmem = NULL;
#endif

#if defined(XP_WIN)
  if (mMutex) {
    MOZ_RELEASE_ASSERT(mRequiresMutex);
    CloseHandle(mMutex);
    mMutex = nullptr;
  }
#endif
}

// Called to use an existing shmem instance created by another process
// Callers to JoinShMem should call LeaveShMem for cleanup
bool VRShMem::JoinShMem() {
#if defined(XP_WIN)
  if (!mMutex && mRequiresMutex) {
    // Check that there are no errors before making system calls
    MOZ_ASSERT(GetLastError() == 0);

    mMutex = OpenMutex(MUTEX_ALL_ACCESS,  // request full access
                       false,             // handle not inheritable
                       kMutexName);       // object name

    if (mMutex == nullptr) {
#  ifdef MOZILLA_INTERNAL_API
      nsAutoCString msg;
      msg.AppendPrintf("VRService OpenMutex error \"%lu\".", GetLastError());
      NS_WARNING(msg.get());
#  endif
      return false;
    }
    MOZ_ASSERT(GetLastError() == 0);
  }
#endif

  if (HasExternalShmem()) {
    // An ExternalShmem is already set. No need to override and rejoin
    return true;
  }

#if defined(XP_WIN)
  // Opening a file-mapping object by name
  base::ProcessHandle targetHandle =
      OpenFileMappingA(FILE_MAP_ALL_ACCESS,  // read/write access
                       FALSE,                // do not inherit the name
                       kShmemName);          // name of mapping object

  MOZ_ASSERT(GetLastError() == 0);

  LARGE_INTEGER length;
  length.QuadPart = sizeof(VRExternalShmem);
  mExternalShmem = (VRExternalShmem*)MapViewOfFile(
      reinterpret_cast<base::ProcessHandle>(
          targetHandle),    // handle to map object
      FILE_MAP_ALL_ACCESS,  // read/write permission
      0, 0, length.QuadPart);
  MOZ_ASSERT(GetLastError() == 0);

  // TODO - Implement logging (Bug 1558912)
  mShmemFile = targetHandle;
  if (!mExternalShmem) {
    MOZ_RELEASE_ASSERT(mExternalShmem);
    return false;
  }
#else
  // TODO: Implement shmem for other platforms.
  //
  // TODO: ** Does this mean that ShMem only works in Windows for now? If so,
  // let's delete the code from other platforms (Bug 1563234)
  MOZ_RELEASE_ASSERT(false, "JoinShMem not implemented");
#endif
  return true;
}

// The cleanup corresponding to JoinShMem
void VRShMem::LeaveShMem() {
#if defined(XP_WIN)
  // Check that there are no errors before making system calls
  MOZ_ASSERT(GetLastError() == 0);

  if (mShmemFile) {
    ::CloseHandle(mShmemFile);
    mShmemFile = nullptr;
  }
#endif

  if (mExternalShmem != nullptr) {
#if defined(XP_WIN)
    if (IsCreatedOnSharedMemory()) {
      UnmapViewOfFile((void*)mExternalShmem);
      MOZ_ASSERT(GetLastError() == 0);
    }
    // Otherwise, if not created on shared memory, simply null the shared
    // reference to the heap object. The call to CloseShMem will appropriately
    // free the allocation.
#endif
    mExternalShmem = nullptr;
  }
#if defined(XP_WIN)
  if (mMutex) {
    MOZ_RELEASE_ASSERT(mRequiresMutex);
    CloseHandle(mMutex);
    mMutex = nullptr;
  }
#endif
}

void VRShMem::PushBrowserState(VRBrowserState& aBrowserState,
                               bool aNotifyCond) {
  if (!mExternalShmem) {
    return;
  }
#if defined(MOZ_WIDGET_ANDROID)
  if (pthread_mutex_lock((pthread_mutex_t*)&(mExternalShmem->geckoMutex)) ==
      0) {
    memcpy((void*)&(mExternalShmem->geckoState), (void*)&aBrowserState,
           sizeof(VRBrowserState));
    if (aNotifyCond) {
      pthread_cond_signal((pthread_cond_t*)&(mExternalShmem->geckoCond));
    }
    pthread_mutex_unlock((pthread_mutex_t*)&(mExternalShmem->geckoMutex));
  }
#else
  bool status = true;

#  if defined(XP_WIN)
  WaitForMutex lock(mMutex);
  status = lock.GetStatus();
#  endif  // defined(XP_WIN)

  if (status) {
    mExternalShmem->geckoGenerationA = mExternalShmem->geckoGenerationA + 1;
    memcpy((void*)&(mExternalShmem->geckoState), (void*)&aBrowserState,
           sizeof(VRBrowserState));
    mExternalShmem->geckoGenerationB = mExternalShmem->geckoGenerationB + 1;
  }
#endif    // defined(MOZ_WIDGET_ANDROID)
}

void VRShMem::PullBrowserState(mozilla::gfx::VRBrowserState& aState) {
  if (!mExternalShmem) {
    return;
  }
  // Copying the browser state from the shmem is non-blocking
  // on x86/x64 architectures.  Arm requires a mutex that is
  // locked for the duration of the memcpy to and from shmem on
  // both sides.
  // On x86/x64 It is fallable -- If a dirty copy is detected by
  // a mismatch of geckoGenerationA and geckoGenerationB,
  // the copy is discarded and will not replace the last known
  // browser state.

#if defined(MOZ_WIDGET_ANDROID)
  // TODO: This code is out-of-date and fails to compile, as
  // VRService isn't compiled for Android (Bug 1563234)
  /*
  if (pthread_mutex_lock((pthread_mutex_t*)&(mExternalShmem->geckoMutex)) ==
      0) {
    memcpy(&aState, &tmp.geckoState, sizeof(VRBrowserState));
    pthread_mutex_unlock((pthread_mutex_t*)&(mExternalShmem->geckoMutex));
  }
  */
  MOZ_RELEASE_ASSERT(false, "PullBrowserState not implemented");
#else
  bool status = true;
#  if defined(XP_WIN)
  if (mRequiresMutex) {
    // TODO: Is this scoped lock okay? Seems like it should allow some
    // race condition (Bug 1563234)
    WaitForMutex lock(mMutex);
    status = lock.GetStatus();
  }
#  endif  // defined(XP_WIN)
  if (status) {
    VRExternalShmem tmp;
    if (mExternalShmem->geckoGenerationA != mBrowserGeneration) {
      // TODO - (void *) cast removes volatile semantics.
      // The memcpy is not likely to be optimized out, but is theoretically
      // possible.  Suggest refactoring to either explicitly enforce memory
      // order or to use locks.
      memcpy(&tmp, (void*)mExternalShmem, sizeof(VRExternalShmem));
      if (tmp.geckoGenerationA == tmp.geckoGenerationB &&
          tmp.geckoGenerationA != 0) {
        memcpy(&aState, &tmp.geckoState, sizeof(VRBrowserState));
        mBrowserGeneration = tmp.geckoGenerationA;
      }
    }
  }
#endif    // defined(MOZ_WIDGET_ANDROID)
}

void VRShMem::PushSystemState(const mozilla::gfx::VRSystemState& aState) {
  if (!mExternalShmem) {
    return;
  }
  // Copying the VR service state to the shmem is atomic, infallable,
  // and non-blocking on x86/x64 architectures.  Arm requires a mutex
  // that is locked for the duration of the memcpy to and from shmem on
  // both sides.

#if defined(MOZ_WIDGET_ANDROID)
  // TODO: This code is out-of-date and fails to compile, as
  // VRService isn't compiled for Android (Bug 1563234)
  MOZ_RELEASE_ASSERT(false, "JoinShMem not implemented");
  /*
  if (pthread_mutex_lock((pthread_mutex_t*)&(mExternalShmem->systemMutex)) ==
      0) {
    // We are casting away the volatile keyword, which is not accepted by
    // memcpy.  It is possible (although very unlikely) that the compiler
    // may optimize out the memcpy here as memcpy isn't explicitly safe for
    // volatile memory in the C++ standard.
    memcpy((void*)&mExternalShmem->state, &aState, sizeof(VRSystemState));
    pthread_mutex_unlock((pthread_mutex_t*)&(mExternalShmem->systemMutex));
  }
  */
#else
  bool lockState = true;

#  if defined(XP_WIN)
  if (mRequiresMutex) {
    // TODO: Is this scoped lock okay? Seems like it should allow some
    // race condition (Bug 1563234)
    WaitForMutex lock(mMutex);
    lockState = lock.GetStatus();
  }
#  endif  // defined(XP_WIN)

  if (lockState) {
    mExternalShmem->generationA = mExternalShmem->generationA + 1;
    memcpy((void*)&mExternalShmem->state, &aState, sizeof(VRSystemState));
    mExternalShmem->generationB = mExternalShmem->generationB + 1;
  }
#endif    // defined(MOZ_WIDGET_ANDROID)
}

#if defined(MOZ_WIDGET_ANDROID)
void VRShMem::PullSystemState(
    VRDisplayState& aDisplayState, VRHMDSensorState& aSensorState,
    std::array<VRControllerState, kVRControllerMaxCount>* const
        aControllerState,
    bool& aEnumerationCompleted,
    const std::function<bool()>& aWaitCondition /* = nullptr */) {
  if (!mExternalShmem) {
    return;
  }
  bool done = false;
  while (!done) {
    if (pthread_mutex_lock((pthread_mutex_t*)&(mExternalShmem->systemMutex)) ==
        0) {
      while (true) {
        memcpy(&aDisplayState, (void*)&(mExternalShmem->state.displayState),
               sizeof(VRDisplayState));
        memcpy(&aSensorState, (void*)&(mExternalShmem->state.sensorState),
               sizeof(VRHMDSensorState));
        memcpy(aControllerState->data(),
               (void*)&(mExternalShmem->state.controllerState),
               sizeof(aControllerState->at(0)) * aControllerState->size());
        aEnumerationCompleted = mExternalShmem->state.enumerationCompleted;
        if (!aWaitCondition || aWaitCondition()) {
          done = true;
          break;
        }
        // Block current thead using the condition variable until data
        // changes
        pthread_cond_wait((pthread_cond_t*)&mExternalShmem->systemCond,
                          (pthread_mutex_t*)&mExternalShmem->systemMutex);
      }  // while (true)
      pthread_mutex_unlock((pthread_mutex_t*)&(mExternalShmem->systemMutex));
    } else if (!aWaitCondition) {
      // pthread_mutex_lock failed and we are not waiting for a condition to
      // exit from PullState call.
      return;
    }
  }  // while (!done) {
}
#else
void VRShMem::PullSystemState(
    VRDisplayState& aDisplayState, VRHMDSensorState& aSensorState,
    std::array<VRControllerState, kVRControllerMaxCount>* const
        aControllerState,
    bool& aEnumerationCompleted,
    const std::function<bool()>& aWaitCondition /* = nullptr */) {
  MOZ_RELEASE_ASSERT(mExternalShmem);
  if (!mExternalShmem) {
    return;
  }
  while (true) {
    {  // Scope for WaitForMutex
#  if defined(XP_WIN)
      bool status = true;
      WaitForMutex lock(mMutex);
      status = lock.GetStatus();
      if (status) {
#  endif  // defined(XP_WIN)
        VRExternalShmem tmp;
        memcpy(&tmp, (void*)mExternalShmem, sizeof(VRExternalShmem));
        bool isCleanCopy =
            tmp.generationA == tmp.generationB && tmp.generationA != 0;
        if (isCleanCopy) {
          memcpy(&aDisplayState, &tmp.state.displayState,
                 sizeof(VRDisplayState));
          memcpy(&aSensorState, &tmp.state.sensorState,
                 sizeof(VRHMDSensorState));
          *aControllerState = tmp.state.controllerState;
          aEnumerationCompleted = tmp.state.enumerationCompleted;
          // Check for wait condition
          if (!aWaitCondition || aWaitCondition()) {
            return;
          }
        } else if (!aWaitCondition) {
          // We did not get a clean copy, and we are not waiting for a condition
          // to exit from PullState call.
          return;
        }
        // Yield the thread while polling
        YieldThread();
#  if defined(XP_WIN)
      } else if (!aWaitCondition) {
        // WaitForMutex failed and we are not waiting for a condition to
        // exit from PullState call.
        return;
      }
#  endif  // defined(XP_WIN)
    }  // End: Scope for WaitForMutex
    // Yield the thread while polling
    YieldThread();
  }  // while (!true)
}
#endif    // defined(MOZ_WIDGET_ANDROID)

void VRShMem::PushTelemetryState(VRTelemetryState& aState) {
#if defined(XP_WIN)
  if (!mExternalShmem) {
    return;
  }

  bool status = true;
  WaitForMutex lock(mMutex);
  status = lock.GetStatus();
  if (status) {
    memcpy((void*)&(mExternalShmem->telemetryState), (void*)&aState,
           sizeof(VRTelemetryState));
  }
#endif  // defined(XP_WIN)
}
void VRShMem::PullTelemetryState(VRTelemetryState& aState) {
#if defined(XP_WIN)
  if (!mExternalShmem) {
    return;
  }

  bool status = true;
  WaitForMutex lock(mMutex);
  status = lock.GetStatus();
  if (status) {
    memcpy((void*)&aState, (void*)&(mExternalShmem->telemetryState),
           sizeof(VRTelemetryState));
  }
#endif  // defined(XP_WIN)
}
