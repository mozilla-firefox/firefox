/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ModuleLoadInfo_h
#define mozilla_ModuleLoadInfo_h

#include "mozilla/NativeNt.h"
#include "mozilla/Vector.h"

class TestDllServices_RejectIncompatibleModuleLoadInfo_Test;

namespace mozilla {

struct ModuleLoadInfo final {
  enum class Status : uint32_t {
    Loaded = 0,
    Blocked,
    Redirected,
  };

  // Identifies this layout to the other binaries that a ModuleLoadInfo passes
  // between (firefox.exe, mozglue.dll and xul.dll), which an interrupted update
  // can leave at different versions.  Bump kVersion whenever the layout
  // changes.  mMagic can never be mistaken for the QueryPerformanceCounter
  // value that earlier layouts started with.
  static constexpr uint64_t kMagic = 0xF14D6F644C6F6164ull;
  static constexpr uint32_t kVersion = 2;

  // Each of those binaries exports a function of this name that returns its
  // kVersion, so that a sender can check a receiver before handing one over.
  static constexpr const char kLayoutVersionExport[] =
      "ModuleLoadInfoLayoutVersion";

  /**
   * Returns true if aModule exports kLayoutVersionExport and reports our
   * layout version.  Not for freestanding code.
   */
  static bool IsLayoutCompatible(HMODULE aModule) {
    if (!aModule) {
      return false;
    }

    auto getVersion = reinterpret_cast<uint32_t (*)()>(
        ::GetProcAddress(aModule, kLayoutVersionExport));
    return getVersion && getVersion() == kVersion;
  }

  // We do not provide these methods inside Gecko proper.
#if !defined(MOZILLA_INTERNAL_API)

  /**
   * This constructor is for use by the LdrLoadDll hook.
   */
  explicit ModuleLoadInfo(PCUNICODE_STRING aRequestedDllName)
      : mLoadTimeInfo(),
        mThreadId(nt::RtlGetCurrentThreadId()),
        mRequestedDllName(aRequestedDllName),
        mBaseAddr(nullptr),
        mStatus(Status::Loaded),
        mIsDependent(false) {
#  if defined(IMPL_MFBT)
    ::QueryPerformanceCounter(&mBeginTimestamp);
#  else
    ::RtlQueryPerformanceCounter(&mBeginTimestamp);
#  endif  // defined(IMPL_MFBT)
  }

  /**
   * This constructor is used by the NtMapViewOfSection hook IF AND ONLY IF
   * the LdrLoadDll hook did not already construct a ModuleLoadInfo for the
   * current DLL load. This may occur while the loader is loading dependencies
   * of another library.
   */
  ModuleLoadInfo(nt::AllocatedUnicodeString&& aSectionName,
                 const void* aBaseAddr, Status aLoadStatus, bool aIsDependent)
      : mLoadTimeInfo(),
        mThreadId(nt::RtlGetCurrentThreadId()),
        mSectionName(std::move(aSectionName)),
        mBaseAddr(aBaseAddr),
        mStatus(aLoadStatus),
        mIsDependent(aIsDependent) {
#  if defined(IMPL_MFBT)
    ::QueryPerformanceCounter(&mBeginTimestamp);
#  else
    ::RtlQueryPerformanceCounter(&mBeginTimestamp);
#  endif  // defined(IMPL_MFBT)
  }

  /**
   * Marks the time that LdrLoadDll began loading this library.
   */
  void SetBeginLoadTimeStamp() {
#  if defined(IMPL_MFBT)
    ::QueryPerformanceCounter(&mLoadTimeInfo);
#  else
    ::RtlQueryPerformanceCounter(&mLoadTimeInfo);
#  endif  // defined(IMPL_MFBT)
  }

  /**
   * Marks the time that LdrLoadDll finished loading this library.
   */
  void SetEndLoadTimeStamp() {
    LARGE_INTEGER endTimeStamp;
#  if defined(IMPL_MFBT)
    ::QueryPerformanceCounter(&endTimeStamp);
#  else
    ::RtlQueryPerformanceCounter(&endTimeStamp);
#  endif  // defined(IMPL_MFBT)

    LONGLONG& timeInfo = mLoadTimeInfo.QuadPart;
    if (!timeInfo) {
      return;
    }

    timeInfo = endTimeStamp.QuadPart - timeInfo;
  }

  /**
   * Saves the current thread's call stack.
   */
  void CaptureBacktrace() {
    const DWORD kMaxBacktraceSize = 512;

    if (!mBacktrace.resize(kMaxBacktraceSize)) {
      return;
    }

    // We don't use a Win32 variant here because Win32's CaptureStackBackTrace
    // is just a macro that resolve to this function anyway.
    WORD numCaptured = ::RtlCaptureStackBackTrace(2, kMaxBacktraceSize,
                                                  mBacktrace.begin(), nullptr);
    (void)mBacktrace.resize(numCaptured);
    // These backtraces might stick around for a while, so let's trim any
    // excess memory.
    mBacktrace.shrinkStorageToFit();
  }

#endif  // !defined(MOZILLA_INTERNAL_API)

  ModuleLoadInfo(ModuleLoadInfo&&) = default;
  ModuleLoadInfo& operator=(ModuleLoadInfo&&) = default;

  ModuleLoadInfo(const ModuleLoadInfo&) = delete;
  ModuleLoadInfo& operator=(const ModuleLoadInfo&) = delete;

  /**
   * A "bare" module load is one that was mapped without the code passing
   * through a call to ntdll!LdrLoadDll.
   */
  bool IsBare() const {
    // SetBeginLoadTimeStamp() and SetEndLoadTimeStamp() are only called by the
    // LdrLoadDll hook, so when mLoadTimeInfo == 0, we know that we are bare.
    return !mLoadTimeInfo.QuadPart;
  }

  /**
   * Returns true for DLL loads where LdrLoadDll was called but
   * NtMapViewOfSection was not. This will happen for DLL requests where the DLL
   * was already mapped into memory by a previous request.
   */
  bool WasMapped() const { return !mSectionName.IsEmpty(); }

  /**
   * Returns true for DLL load which was denied by our blocklist.
   */
  bool WasDenied() const {
    return mStatus == ModuleLoadInfo::Status::Blocked ||
           mStatus == ModuleLoadInfo::Status::Redirected;
  }

  /**
   * Returns true for a DLL load which was blocked by our blocklist.
   */
  bool WasBlocked() const { return mStatus == ModuleLoadInfo::Status::Blocked; }

  /**
   * Returns false if this object was built by a binary whose ModuleLoadInfo
   * layout differs from ours.  Only mMagic and mVersion are read, so this is
   * safe to call on an object of any layout.
   */
  bool HasCompatibleLayout() const {
    return mMagic == kMagic && mVersion == kVersion;
  }

  // These must stay the first members.  See kMagic.
  uint64_t mMagic = kMagic;
  uint32_t mVersion = kVersion;
  // Timestamp for the creation of this event
  LARGE_INTEGER mBeginTimestamp;
  // Duration of the LdrLoadDll call
  LARGE_INTEGER mLoadTimeInfo;
  // Thread ID of this DLL load
  DWORD mThreadId;
  // The name requested of LdrLoadDll by its caller
  nt::AllocatedUnicodeString mRequestedDllName;
  // The name of the DLL that backs section that was mapped by the loader. This
  // string is the effective name of the DLL that was resolved by the loader's
  // path search algorithm.
  nt::AllocatedUnicodeString mSectionName;
  // The base address of the module's mapped section
  const void* mBaseAddr;
  // A duplicate of the handle the loader opened this module's file with, taken
  // by the NtCreateSection hook.  It grants no access to the file's contents.
  //
  // Null for any load the NtCreateSection hook did not take a handle for.
  nt::AutoHandle mFileHandle;
  // If the module was successfully loaded, stack trace of the DLL load request
  Vector<PVOID, 0, nt::RtlAllocPolicy> mBacktrace;
  // The status of DLL load
  Status mStatus;
  // Whether the module is one of the executables's dependent modules or not
  bool mIsDependent;

 private:
  // Builds an empty object whose layout version is deliberately wrong, for
  // tests of the layout checks only.
  ModuleLoadInfo()
      : mVersion(kVersion + 1),
        mBeginTimestamp(),
        mLoadTimeInfo(),
        mThreadId(0),
        mBaseAddr(nullptr),
        mStatus(Status::Loaded),
        mIsDependent(false) {}

  friend class ::TestDllServices_RejectIncompatibleModuleLoadInfo_Test;
};

using ModuleLoadInfoVec = Vector<ModuleLoadInfo, 0, nt::RtlAllocPolicy>;

}  // namespace mozilla

#endif  // mozilla_ModuleLoadInfo_h
