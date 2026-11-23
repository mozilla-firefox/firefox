/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_interceptor_MMPolicies_h
#define mozilla_interceptor_MMPolicies_h

#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DynamicallyLinkedFunctionPtr.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/StackWalk_windows.h"
#include "mozilla/WindowsMapRemoteView.h"
#include "mozilla/WindowsUnwindInfo.h"
#include "mozilla/WinHeaderOnlyUtils.h"

#include <bit>
#include <windows.h>

#if (NTDDI_VERSION < NTDDI_WIN10_RS4)
WINBASEAPI
PVOID WINAPI VirtualAlloc2(HANDLE Process, PVOID BaseAddress, SIZE_T Size,
                           ULONG AllocationType, ULONG PageProtection,
                           MEM_EXTENDED_PARAMETER* ExtendedParameters,
                           ULONG ParameterCount);
WINBASEAPI
PVOID WINAPI MapViewOfFile3(HANDLE FileMapping, HANDLE Process,
                            PVOID BaseAddress, ULONG64 Offset, SIZE_T ViewSize,
                            ULONG AllocationType, ULONG PageProtection,
                            MEM_EXTENDED_PARAMETER* ExtendedParameters,
                            ULONG ParameterCount);
#endif  // (NTDDI_VERSION < NTDDI_WIN10_RS4)

// _CRT_RAND_S is not defined everywhere, but we need it.
#if !defined(_CRT_RAND_S)
extern "C" errno_t rand_s(unsigned int* randomValue);
#endif  // !defined(_CRT_RAND_S)

// Declaring only the functions we need in NativeNt.h.  To include the entire
// NativeNt.h causes circular dependency.
namespace mozilla {
namespace nt {
SIZE_T WINAPI VirtualQueryEx(HANDLE aProcess, LPCVOID aAddress,
                             PMEMORY_BASIC_INFORMATION aMemInfo,
                             SIZE_T aMemInfoLen);

SIZE_T WINAPI VirtualQuery(LPCVOID aAddress, PMEMORY_BASIC_INFORMATION aMemInfo,
                           SIZE_T aMemInfoLen);
}  // namespace nt
}  // namespace mozilla

namespace mozilla {
namespace interceptor {

// This class implements memory operations not involving any kernel32's
// functions, so that derived classes can use them.
class MOZ_TRIVIAL_CTOR_DTOR MMPolicyInProcessPrimitive {
 protected:
  bool ProtectInternal(decltype(&::VirtualProtect) aVirtualProtect,
                       void* aVAddress, size_t aSize, uint32_t aProtFlags,
                       uint32_t* aPrevProtFlags) const {
    MOZ_ASSERT(aPrevProtFlags);
    BOOL ok = aVirtualProtect(aVAddress, aSize, aProtFlags,
                              reinterpret_cast<PDWORD>(aPrevProtFlags));
    if (!ok && aPrevProtFlags) {
      // VirtualProtect can fail but still set valid protection flags.
      // Let's clear those upon failure.
      *aPrevProtFlags = 0;
    }

    return !!ok;
  }

 public:
  bool Read(void* aToPtr, const void* aFromPtr, size_t aLen) const {
    ::memcpy(aToPtr, aFromPtr, aLen);
    return true;
  }

  bool Write(void* aToPtr, const void* aFromPtr, size_t aLen) const {
    ::memcpy(aToPtr, aFromPtr, aLen);
    return true;
  }

  /**
   * @return true if the page that hosts aVAddress is accessible.
   */
  bool IsPageAccessible(uintptr_t aVAddress) const {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T result = nt::VirtualQuery(reinterpret_cast<LPCVOID>(aVAddress), &mbi,
                                     sizeof(mbi));

    return result && mbi.AllocationProtect && mbi.State == MEM_COMMIT &&
           mbi.Protect != PAGE_NOACCESS;
  }
};

class MOZ_TRIVIAL_CTOR_DTOR MMPolicyBase {
 protected:
  static uintptr_t AlignDown(const uintptr_t aUnaligned,
                             const uintptr_t aAlignTo) {
    MOZ_ASSERT(std::has_single_bit(aAlignTo));
#pragma warning(suppress : 4146)
    return aUnaligned & (-aAlignTo);
  }

  static uintptr_t AlignUp(const uintptr_t aUnaligned,
                           const uintptr_t aAlignTo) {
    MOZ_ASSERT(std::has_single_bit(aAlignTo));
#pragma warning(suppress : 4146)
    return aUnaligned + ((-aUnaligned) & (aAlignTo - 1));
  }

  static PVOID AlignUpToRegion(PVOID aUnaligned, uintptr_t aAlignTo,
                               size_t aLen, size_t aDesiredLen) {
    uintptr_t unaligned = reinterpret_cast<uintptr_t>(aUnaligned);
    uintptr_t aligned = AlignUp(unaligned, aAlignTo);
    MOZ_ASSERT(aligned >= unaligned);

    if (aLen < aligned - unaligned) {
      return nullptr;
    }

    aLen -= (aligned - unaligned);
    return reinterpret_cast<PVOID>((aLen >= aDesiredLen) ? aligned : 0);
  }

 public:
#if defined(NIGHTLY_BUILD)
  Maybe<DetourError> mLastError;
  const Maybe<DetourError>& GetLastDetourError() const { return mLastError; }
  template <typename... Args>
  void SetLastDetourError(Args&&... aArgs) {
    mLastError = Some(DetourError(std::forward<Args>(aArgs)...));
  }
#else
  template <typename... Args>
  void SetLastDetourError(Args&&... aArgs) {}
#endif  // defined(NIGHTLY_BUILD)

  DWORD ComputeAllocationSize(const uint32_t aRequestedSize) const {
    MOZ_ASSERT(aRequestedSize);
    DWORD result = aRequestedSize;

    const uint32_t granularity = GetAllocGranularity();

    uint32_t mod = aRequestedSize % granularity;
    if (mod) {
      result += (granularity - mod);
    }

    return result;
  }

  DWORD GetAllocGranularity() const {
    static const DWORD kAllocGranularity = []() -> DWORD {
      SYSTEM_INFO sysInfo;
      ::GetSystemInfo(&sysInfo);
      return sysInfo.dwAllocationGranularity;
    }();

    return kAllocGranularity;
  }

  DWORD GetPageSize() const {
    static const DWORD kPageSize = []() -> DWORD {
      SYSTEM_INFO sysInfo;
      ::GetSystemInfo(&sysInfo);
      return sysInfo.dwPageSize;
    }();

    return kPageSize;
  }

  uintptr_t GetMaxUserModeAddress() const {
    static const uintptr_t kMaxUserModeAddr = []() -> uintptr_t {
      SYSTEM_INFO sysInfo;
      ::GetSystemInfo(&sysInfo);
      return reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);
    }();

    return kMaxUserModeAddr;
  }

  static const uint8_t* GetLowerBound(const Span<const uint8_t>& aBounds) {
    return &(*aBounds.cbegin());
  }

  static const uint8_t* GetUpperBoundIncl(const Span<const uint8_t>& aBounds) {
    // We return an upper bound that is inclusive.
    return &(*(aBounds.cend() - 1));
  }

  static const uint8_t* GetUpperBoundExcl(const Span<const uint8_t>& aBounds) {
    // We return an upper bound that is exclusive by adding 1 to the inclusive
    // upper bound.
    return GetUpperBoundIncl(aBounds) + 1;
  }

  /**
   * It is convenient for us to provide address range information based on a
   * "pivot" and a distance from that pivot, as branch instructions operate
   * within a range of the program counter. OTOH, to actually manage the
   * regions of memory, it is easier to think about them in terms of their
   * lower and upper bounds. This function converts from the former format to
   * the latter format.
   */
  Maybe<Span<const uint8_t>> SpanFromPivotAndDistance(
      const uint32_t aSize, const uintptr_t aPivotAddr,
      const uint32_t aMaxDistanceFromPivot) const {
    if (!aPivotAddr || !aMaxDistanceFromPivot) {
      return Nothing();
    }

    // We don't allow regions below 1MB so that we're not allocating near any
    // sensitive areas in our address space.
    const uintptr_t kMinAllowableAddress = 0x100000;

    const uintptr_t kGranularity(GetAllocGranularity());

    // We subtract the max distance from the pivot to determine our lower bound.
    CheckedInt<uintptr_t> lowerBound(aPivotAddr);
    lowerBound -= aMaxDistanceFromPivot;
    if (lowerBound.isValid()) {
      // In this case, the subtraction has not underflowed, but we still want
      // the lower bound to be at least kMinAllowableAddress.
      lowerBound = std::max(lowerBound.value(), kMinAllowableAddress);
    } else {
      // In this case, we underflowed. Forcibly set the lower bound to
      // kMinAllowableAddress.
      lowerBound = CheckedInt<uintptr_t>(kMinAllowableAddress);
    }

    // Align up to the next unit of allocation granularity when necessary.
    lowerBound = AlignUp(lowerBound.value(), kGranularity);
    MOZ_ASSERT(lowerBound.isValid());
    if (!lowerBound.isValid()) {
      return Nothing();
    }

    // We must ensure that our region is below the maximum allowable user-mode
    // address, or our reservation will fail.
    const uintptr_t kMaxUserModeAddr = GetMaxUserModeAddress();

    // We add the max distance from the pivot to determine our upper bound.
    CheckedInt<uintptr_t> upperBound(aPivotAddr);
    upperBound += aMaxDistanceFromPivot;
    if (upperBound.isValid()) {
      // In this case, the addition has not overflowed, but we still want
      // the upper bound to be at most kMaxUserModeAddr.
      upperBound = std::min(upperBound.value(), kMaxUserModeAddr);
    } else {
      // In this case, we overflowed. Forcibly set the upper bound to
      // kMaxUserModeAddr.
      upperBound = CheckedInt<uintptr_t>(kMaxUserModeAddr);
    }

    // Subtract the desired allocation size so that any chunk allocated in the
    // region will be reachable.
    upperBound -= aSize;
    if (!upperBound.isValid()) {
      return Nothing();
    }

    // Align down to the next unit of allocation granularity when necessary.
    upperBound = AlignDown(upperBound.value(), kGranularity);
    if (!upperBound.isValid()) {
      return Nothing();
    }

    MOZ_ASSERT(lowerBound.value() < upperBound.value());
    if (lowerBound.value() >= upperBound.value()) {
      return Nothing();
    }

    // Return the result as a Span
    return Some(Span(reinterpret_cast<const uint8_t*>(lowerBound.value()),
                     upperBound.value() - lowerBound.value()));
  }

  /**
   * This function locates a virtual memory region of |aDesiredBytesLen| that
   * resides in the interval [aRangeMin, aRangeMax). We do this by scanning the
   * virtual memory space for a block of unallocated memory that is sufficiently
   * large.
   */
  PVOID FindRegion(HANDLE aProcess, const size_t aDesiredBytesLen,
                   const uint8_t* aRangeMin, const uint8_t* aRangeMax) {
    // Convert the given pointers to uintptr_t because we should not
    // compare two pointers unless they are from the same array or object.
    uintptr_t rangeMin = reinterpret_cast<uintptr_t>(aRangeMin);
    uintptr_t rangeMax = reinterpret_cast<uintptr_t>(aRangeMax);

    const DWORD kGranularity = GetAllocGranularity();
    if (!aDesiredBytesLen) {
      SetLastDetourError(MMPOLICY_RESERVE_FINDREGION_INVALIDLEN);
      return nullptr;
    }

    MOZ_ASSERT(rangeMin < rangeMax);
    if (rangeMin >= rangeMax) {
      SetLastDetourError(MMPOLICY_RESERVE_FINDREGION_INVALIDRANGE);
      return nullptr;
    }

    // Generate a randomized base address that falls within the interval
    // [aRangeMin, aRangeMax - aDesiredBytesLen]
    unsigned int rnd = 0;
    rand_s(&rnd);

    // Reduce rnd to a value that falls within the acceptable range
    uintptr_t maxOffset =
        (rangeMax - rangeMin - aDesiredBytesLen) / kGranularity;
    // Divide by maxOffset + 1 because maxOffset * kGranularity is acceptable.
    uintptr_t offset = (uintptr_t(rnd) % (maxOffset + 1)) * kGranularity;

    // Start searching at this address
    const uintptr_t searchStart = rangeMin + offset;
    // The max address needs to incorporate the desired length
    const uintptr_t kMaxPtr = rangeMax - aDesiredBytesLen;

    MOZ_DIAGNOSTIC_ASSERT(searchStart <= kMaxPtr);

    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T len = sizeof(mbi);

    // Scan the range for a free chunk that is at least as large as
    // aDesiredBytesLen
    // Scan [searchStart, kMaxPtr]
    for (uintptr_t address = searchStart; address <= kMaxPtr;) {
      if (nt::VirtualQueryEx(aProcess, reinterpret_cast<uint8_t*>(address),
                             &mbi, len) != len) {
        SetLastDetourError(MMPOLICY_RESERVE_FINDREGION_VIRTUALQUERY_ERROR,
                           ::GetLastError());
        return nullptr;
      }

      if (mbi.State == MEM_FREE) {
        // |mbi.BaseAddress| is aligned with the page granularity, but may not
        // be aligned with the allocation granularity.  VirtualAlloc does not
        // accept such a non-aligned address unless the corresponding allocation
        // region is free.  So we get the next boundary's start address.
        PVOID regionStart = AlignUpToRegion(mbi.BaseAddress, kGranularity,
                                            mbi.RegionSize, aDesiredBytesLen);
        if (regionStart) {
          return regionStart;
        }
      }

      address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    // Scan [aRangeMin, searchStart)
    for (uintptr_t address = rangeMin; address < searchStart;) {
      if (nt::VirtualQueryEx(aProcess, reinterpret_cast<uint8_t*>(address),
                             &mbi, len) != len) {
        SetLastDetourError(MMPOLICY_RESERVE_FINDREGION_VIRTUALQUERY_ERROR,
                           ::GetLastError());
        return nullptr;
      }

      if (mbi.State == MEM_FREE) {
        PVOID regionStart = AlignUpToRegion(mbi.BaseAddress, kGranularity,
                                            mbi.RegionSize, aDesiredBytesLen);
        if (regionStart) {
          return regionStart;
        }
      }

      address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    SetLastDetourError(MMPOLICY_RESERVE_FINDREGION_NO_FREE_REGION,
                       ::GetLastError());
    return nullptr;
  }

  /**
   * This function reserves a |aSize| block of virtual memory.
   *
   * When |aBounds| is Nothing, it just calls |aReserveFn| and lets Windows
   * choose the base address.
   *
   * Otherwise, it tries to call |aReserveRangeFn| to reserve the memory within
   * the bounds provided by |aBounds|. It is advantageous to use this function
   * because the OS's VM manager has better information as to which base
   * addresses are the best to use.
   *
   * If |aReserveRangeFn| retuns Nothing, this means that the platform support
   * is not available. In that case, we fall back to manually computing a region
   * to use for reserving the memory by calling |FindRegion|.
   */
  template <typename ReserveFnT, typename ReserveRangeFnT>
  PVOID Reserve(HANDLE aProcess, const uint32_t aSize,
                const ReserveFnT& aReserveFn,
                const ReserveRangeFnT& aReserveRangeFn,
                const Maybe<Span<const uint8_t>>& aBounds) {
    if (!aBounds) {
      // No restrictions, let the OS choose the base address
      PVOID ret = aReserveFn(aProcess, nullptr, aSize);
      if (!ret) {
        SetLastDetourError(MMPOLICY_RESERVE_NOBOUND_RESERVE_ERROR,
                           ::GetLastError());
      }
      return ret;
    }

    const uint8_t* lowerBound = GetLowerBound(aBounds.ref());
    const uint8_t* upperBoundExcl = GetUpperBoundExcl(aBounds.ref());

    Maybe<PVOID> result =
        aReserveRangeFn(aProcess, aSize, lowerBound, upperBoundExcl);
    if (result) {
      return result.value();
    }

    // aReserveRangeFn is not available on this machine. We'll do a manual
    // search.

    size_t curAttempt = 0;
    const size_t kMaxAttempts = 8;

    // We loop here because |FindRegion| may return a base address that
    // is reserved elsewhere before we have had a chance to reserve it
    // ourselves.
    while (curAttempt < kMaxAttempts) {
      PVOID base = FindRegion(aProcess, aSize, lowerBound, upperBoundExcl);
      if (!base) {
        return nullptr;
      }

      result = Some(aReserveFn(aProcess, base, aSize));
      if (result.value()) {
        return result.value();
      }

      ++curAttempt;
    }

    // If we run out of attempts, we fall through to the default case where
    // the system chooses any base address it wants. In that case, the hook
    // will be set on a best-effort basis.
    PVOID ret = aReserveFn(aProcess, nullptr, aSize);
    if (!ret) {
      SetLastDetourError(MMPOLICY_RESERVE_FINAL_RESERVE_ERROR,
                         ::GetLastError());
    }
    return ret;
  }
};

class MOZ_EMPTY_BASES MOZ_TRIVIAL_CTOR_DTOR MMPolicyInProcess
    : public MMPolicyInProcessPrimitive,
      public MMPolicyBase {
 public:
  typedef MMPolicyInProcess MMPolicyT;

  constexpr MMPolicyInProcess()
      : mBase(nullptr), mReservationSize(0), mCommitOffset(0) {}

  MMPolicyInProcess(const MMPolicyInProcess&) = delete;
  MMPolicyInProcess& operator=(const MMPolicyInProcess&) = delete;

  MMPolicyInProcess(MMPolicyInProcess&& aOther)
      : mBase(nullptr), mReservationSize(0), mCommitOffset(0) {
    *this = std::move(aOther);
  }

  MMPolicyInProcess& operator=(MMPolicyInProcess&& aOther) {
    mBase = aOther.mBase;
    aOther.mBase = nullptr;

    mCommitOffset = aOther.mCommitOffset;
    aOther.mCommitOffset = 0;

    mReservationSize = aOther.mReservationSize;
    aOther.mReservationSize = 0;

    return *this;
  }

  explicit operator bool() const { return !!mBase; }

  /**
   * Should we unhook everything upon destruction?
   */
  bool ShouldUnhookUponDestruction() const { return true; }

#if defined(_M_IX86)
  bool WriteAtomic(void* aDestPtr, const uint16_t aValue) const {
    *static_cast<uint16_t*>(aDestPtr) = aValue;
    return true;
  }
#endif  // defined(_M_IX86)

  bool Protect(void* aVAddress, size_t aSize, uint32_t aProtFlags,
               uint32_t* aPrevProtFlags) const {
    return ProtectInternal(::VirtualProtect, aVAddress, aSize, aProtFlags,
                           aPrevProtFlags);
  }

  bool FlushInstructionCache() const {
    return !!::FlushInstructionCache(::GetCurrentProcess(), nullptr, 0);
  }

  static DWORD GetTrampWriteProtFlags() { return PAGE_EXECUTE_READWRITE; }

#if defined(_M_X64)
  bool IsTrampolineSpaceInLowest2GB() const {
    return (mBase + mReservationSize) <=
           reinterpret_cast<uint8_t*>(0x0000000080000000ULL);
  }

  static constexpr bool kSupportsUnwindInfo = true;

  mozilla::UniquePtr<uint8_t[]> LookupUnwindInfo(
      uintptr_t aOrigFuncAddr, uint32_t* aOffsetFromBeginAddr,
      uint32_t* aOffsetToEndAddr, uintptr_t* aOrigImageBase) const {
    DWORD64 origImageBase = 0;
    auto origFuncEntry =
        RtlLookupFunctionEntry(aOrigFuncAddr, &origImageBase, nullptr);
    if (!origFuncEntry) {
      return nullptr;
    }

    if (aOffsetFromBeginAddr) {
      *aOffsetFromBeginAddr =
          aOrigFuncAddr - (origImageBase + origFuncEntry->BeginAddress);
    }
    if (aOffsetToEndAddr) {
      *aOffsetToEndAddr =
          (origImageBase + origFuncEntry->EndAddress) - aOrigFuncAddr;
    }
    if (aOrigImageBase) {
      *aOrigImageBase = origImageBase;
    }
    return reinterpret_cast<const UnwindInfo*>(origImageBase +
                                               origFuncEntry->UnwindData)
        ->Copy();
  }

  bool AddFunctionTable(uintptr_t aFunctionTable, uint32_t aEntryCount,
                        uintptr_t aBaseAddress) const {
    // RtlAddFunctionTable acquires LdrpMrdataLock and
    // RtlpDynamicFunctionTableLock, independently.
    AutoSuppressStackWalking suppress;
    return bool(
        RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(aFunctionTable),
                            aEntryCount, aBaseAddress));
  }
#endif  // defined(_M_X64)

 protected:
  uint8_t* GetLocalView() const { return mBase; }

  uintptr_t GetRemoteView() const {
    // Same as local view for in-process
    return reinterpret_cast<uintptr_t>(mBase);
  }

  /**
   * @return the effective number of bytes reserved, or 0 on failure
   */
  uint32_t Reserve(const uint32_t aSize,
                   const Maybe<Span<const uint8_t>>& aBounds) {
    if (!aSize) {
      return 0;
    }

    if (mBase) {
      MOZ_ASSERT(mReservationSize >= aSize);
      return mReservationSize;
    }

    mReservationSize = ComputeAllocationSize(aSize);

    auto reserveFn = [](HANDLE aProcess, PVOID aBase, uint32_t aSize) -> PVOID {
      return ::VirtualAlloc(aBase, aSize, MEM_RESERVE, PAGE_NOACCESS);
    };

    auto reserveWithinRangeFn =
        [](HANDLE aProcess, uint32_t aSize, const uint8_t* aRangeMin,
           const uint8_t* aRangeMaxExcl) -> Maybe<PVOID> {
      static const StaticDynamicallyLinkedFunctionPtr<
          decltype(&::VirtualAlloc2)>
          pVirtualAlloc2(L"kernelbase.dll", "VirtualAlloc2");
      if (!pVirtualAlloc2) {
        return Nothing();
      }

      // NB: MEM_ADDRESS_REQUIREMENTS::HighestEndingAddress is *inclusive*
      MEM_ADDRESS_REQUIREMENTS memReq = {
          const_cast<uint8_t*>(aRangeMin),
          const_cast<uint8_t*>(aRangeMaxExcl - 1)};

      MEM_EXTENDED_PARAMETER memParam = {};
      memParam.Type = MemExtendedParameterAddressRequirements;
      memParam.Pointer = &memReq;

      return Some(pVirtualAlloc2(aProcess, nullptr, aSize, MEM_RESERVE,
                                 PAGE_NOACCESS, &memParam, 1));
    };

    mBase = static_cast<uint8_t*>(
        MMPolicyBase::Reserve(::GetCurrentProcess(), mReservationSize,
                              reserveFn, reserveWithinRangeFn, aBounds));

    if (!mBase) {
      return 0;
    }

    return mReservationSize;
  }

  bool MaybeCommitNextPage(const uint32_t aRequestedOffset,
                           const uint32_t aRequestedLength) {
    if (!(*this)) {
      return false;
    }

    uint32_t limit = aRequestedOffset + aRequestedLength - 1;
    if (limit < mCommitOffset) {
      // No commit required
      return true;
    }

    MOZ_DIAGNOSTIC_ASSERT(mCommitOffset < mReservationSize);
    if (mCommitOffset >= mReservationSize) {
      return false;
    }

    PVOID local = ::VirtualAlloc(mBase + mCommitOffset, GetPageSize(),
                                 MEM_COMMIT, PAGE_EXECUTE_READ);
    if (!local) {
      return false;
    }

    mCommitOffset += GetPageSize();
    return true;
  }

 private:
  uint8_t* mBase;
  uint32_t mReservationSize;
  uint32_t mCommitOffset;
};

// This class manages in-process memory access without using functions
// imported from kernel32.dll.  Instead, it uses functions in its own
// function table that are provided from outside.
class MMPolicyInProcessEarlyStage : public MMPolicyInProcessPrimitive {
 public:
  struct Kernel32Exports {
    decltype(&::FlushInstructionCache) mFlushInstructionCache;
    decltype(&::GetModuleHandleW) mGetModuleHandleW;
    decltype(&::GetSystemInfo) mGetSystemInfo;
    decltype(&::VirtualProtect) mVirtualProtect;
  };

 private:
  static DWORD GetPageSize(const Kernel32Exports& aK32Exports) {
    SYSTEM_INFO sysInfo;
    aK32Exports.mGetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
  }

  const Kernel32Exports& mK32Exports;
  const DWORD mPageSize;

 public:
  explicit MMPolicyInProcessEarlyStage(const Kernel32Exports& aK32Exports)
      : mK32Exports(aK32Exports), mPageSize(GetPageSize(mK32Exports)) {}

  // The pattern of constructing a local static variable with a lambda,
  // which can be seen in MMPolicyBase, is compiled into code with the
  // critical section APIs like EnterCriticalSection imported from kernel32.dll.
  // Because this class needs to be able to run in a process's early stage
  // when IAT is not yet resolved, we cannot use that patten, thus simply
  // caching a value as a local member in the class.
  DWORD GetPageSize() const { return mPageSize; }

  bool Protect(void* aVAddress, size_t aSize, uint32_t aProtFlags,
               uint32_t* aPrevProtFlags) const {
    return ProtectInternal(mK32Exports.mVirtualProtect, aVAddress, aSize,
                           aProtFlags, aPrevProtFlags);
  }

  bool FlushInstructionCache() const {
    const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);
    return !!mK32Exports.mFlushInstructionCache(kCurrentProcess, nullptr, 0);
  }
};

class MMPolicyOutOfProcess : public MMPolicyBase {
 public:
  typedef MMPolicyOutOfProcess MMPolicyT;

  explicit MMPolicyOutOfProcess(HANDLE aProcess)
      : mProcess(nullptr),
        mMapping(nullptr),
        mLocalView(nullptr),
        mRemoteView(nullptr),
        mReservationSize(0),
        mCommitOffset(0) {
    MOZ_ASSERT(aProcess);
    ::DuplicateHandle(::GetCurrentProcess(), aProcess, ::GetCurrentProcess(),
                      &mProcess, kAccessFlags, FALSE, 0);
    MOZ_ASSERT(mProcess);
  }

  explicit MMPolicyOutOfProcess(DWORD aPid)
      : mProcess(::OpenProcess(kAccessFlags, FALSE, aPid)),
        mMapping(nullptr),
        mLocalView(nullptr),
        mRemoteView(nullptr),
        mReservationSize(0),
        mCommitOffset(0) {
    MOZ_ASSERT(mProcess);
  }

  ~MMPolicyOutOfProcess() { Destroy(); }

  MMPolicyOutOfProcess(MMPolicyOutOfProcess&& aOther)
      : mProcess(nullptr),
        mMapping(nullptr),
        mLocalView(nullptr),
        mRemoteView(nullptr),
        mReservationSize(0),
        mCommitOffset(0) {
    *this = std::move(aOther);
  }

  MMPolicyOutOfProcess(const MMPolicyOutOfProcess& aOther) = delete;
  MMPolicyOutOfProcess& operator=(const MMPolicyOutOfProcess&) = delete;

  MMPolicyOutOfProcess& operator=(MMPolicyOutOfProcess&& aOther) {
    Destroy();

    mProcess = aOther.mProcess;
    aOther.mProcess = nullptr;

    mMapping = aOther.mMapping;
    aOther.mMapping = nullptr;

    mLocalView = aOther.mLocalView;
    aOther.mLocalView = nullptr;

    mRemoteView = aOther.mRemoteView;
    aOther.mRemoteView = nullptr;

    mReservationSize = aOther.mReservationSize;
    aOther.mReservationSize = 0;

    mCommitOffset = aOther.mCommitOffset;
    aOther.mCommitOffset = 0;

    return *this;
  }

  explicit operator bool() const {
    return mProcess && mMapping && mLocalView && mRemoteView;
  }

  bool ShouldUnhookUponDestruction() const {
    // We don't clean up hooks for remote processes; they are expected to
    // outlive our process.
    return false;
  }

  // This function reads as many bytes as |aLen| from the target process and
  // succeeds only when the entire area to be read is accessible.
  bool Read(void* aToPtr, const void* aFromPtr, size_t aLen) const {
    MOZ_ASSERT(mProcess);
    if (!mProcess) {
      return false;
    }

    SIZE_T numBytes = 0;
    BOOL ok = ::ReadProcessMemory(mProcess, aFromPtr, aToPtr, aLen, &numBytes);
    return ok && numBytes == aLen;
  }

  // This function reads as many bytes as possible from the target process up
  // to |aLen| bytes and returns the number of bytes which was actually read.
  size_t TryRead(void* aToPtr, const void* aFromPtr, size_t aLen) const {
    MOZ_ASSERT(mProcess);
    if (!mProcess) {
      return 0;
    }

    uint32_t pageSize = GetPageSize();
    uintptr_t pageMask = pageSize - 1;

    auto rangeStart = reinterpret_cast<uintptr_t>(aFromPtr);
    auto rangeEnd = rangeStart + aLen;

    while (rangeStart < rangeEnd) {
      SIZE_T numBytes = 0;
      BOOL ok = ::ReadProcessMemory(mProcess, aFromPtr, aToPtr,
                                    rangeEnd - rangeStart, &numBytes);
      if (ok) {
        return numBytes;
      }

      // If ReadProcessMemory fails, try to read up to each page boundary from
      // the end of the requested area one by one.
      if (rangeEnd & pageMask) {
        rangeEnd &= ~pageMask;
      } else {
        rangeEnd -= pageSize;
      }
    }

    return 0;
  }

  bool Write(void* aToPtr, const void* aFromPtr, size_t aLen) const {
    MOZ_ASSERT(mProcess);
    if (!mProcess) {
      return false;
    }

    SIZE_T numBytes = 0;
    BOOL ok = ::WriteProcessMemory(mProcess, aToPtr, aFromPtr, aLen, &numBytes);
    return ok && numBytes == aLen;
  }

  bool Protect(void* aVAddress, size_t aSize, uint32_t aProtFlags,
               uint32_t* aPrevProtFlags) const {
    MOZ_ASSERT(mProcess);
    if (!mProcess) {
      return false;
    }

    MOZ_ASSERT(aPrevProtFlags);
    BOOL ok = ::VirtualProtectEx(mProcess, aVAddress, aSize, aProtFlags,
                                 reinterpret_cast<PDWORD>(aPrevProtFlags));
    if (!ok && aPrevProtFlags) {
      // VirtualProtectEx can fail but still set valid protection flags.
      // Let's clear those upon failure.
      *aPrevProtFlags = 0;
    }

    return !!ok;
  }

  /**
   * @return true if the page that hosts aVAddress is accessible.
   */
  bool IsPageAccessible(uintptr_t aVAddress) const {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T result = nt::VirtualQueryEx(
        mProcess, reinterpret_cast<LPCVOID>(aVAddress), &mbi, sizeof(mbi));

    return result && mbi.AllocationProtect && mbi.State == MEM_COMMIT &&
           mbi.Protect != PAGE_NOACCESS;
  }

  bool FlushInstructionCache() const {
    return !!::FlushInstructionCache(mProcess, nullptr, 0);
  }

  static DWORD GetTrampWriteProtFlags() { return PAGE_READWRITE; }

#if defined(_M_X64)
  bool IsTrampolineSpaceInLowest2GB() const {
    return (GetRemoteView() + mReservationSize) <= 0x0000000080000000ULL;
  }

  // TODO: We should also implement unwind info for our out-of-process policy.
  static constexpr bool kSupportsUnwindInfo = false;

  inline mozilla::UniquePtr<uint8_t[]> LookupUnwindInfo(
      uintptr_t aOrigFuncAddr, uint32_t* aOffsetFromBeginAddr,
      uint32_t* aOffsetToEndAddr, uintptr_t* aOrigImageBase) const {
    return nullptr;
  }

  inline bool AddFunctionTable(uintptr_t aNewTable, uint32_t aEntryCount,
                               uintptr_t aBaseAddress) const {
    return false;
  }
#endif  // defined(_M_X64)

 protected:
  uint8_t* GetLocalView() const { return mLocalView; }

  uintptr_t GetRemoteView() const {
    return reinterpret_cast<uintptr_t>(mRemoteView);
  }

  /**
   * @return the effective number of bytes reserved, or 0 on failure
   */
  uint32_t Reserve(const uint32_t aSize,
                   const Maybe<Span<const uint8_t>>& aBounds) {
    if (!aSize || !mProcess) {
      SetLastDetourError(MMPOLICY_RESERVE_INVALIDARG);
      return 0;
    }

    if (mRemoteView) {
      MOZ_ASSERT(mReservationSize >= aSize);
      SetLastDetourError(MMPOLICY_RESERVE_ZERO_RESERVATIONSIZE);
      return mReservationSize;
    }

    mReservationSize = ComputeAllocationSize(aSize);

    mMapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                    PAGE_EXECUTE_READWRITE | SEC_RESERVE, 0,
                                    mReservationSize, nullptr);
    if (!mMapping) {
      SetLastDetourError(MMPOLICY_RESERVE_CREATEFILEMAPPING, ::GetLastError());
      return 0;
    }

    mLocalView = static_cast<uint8_t*>(
        ::MapViewOfFile(mMapping, FILE_MAP_WRITE, 0, 0, 0));
    if (!mLocalView) {
      SetLastDetourError(MMPOLICY_RESERVE_MAPVIEWOFFILE, ::GetLastError());
      return 0;
    }

    auto reserveFn = [mapping = mMapping](HANDLE aProcess, PVOID aBase,
                                          uint32_t aSize) -> PVOID {
      return mozilla::MapRemoteViewOfFile(mapping, aProcess, 0ULL, aBase, 0, 0,
                                          PAGE_EXECUTE_READ);
    };

    auto reserveWithinRangeFn =
        [mapping = mMapping](HANDLE aProcess, uint32_t aSize,
                             const uint8_t* aRangeMin,
                             const uint8_t* aRangeMaxExcl) -> Maybe<PVOID> {
      static const StaticDynamicallyLinkedFunctionPtr<
          decltype(&::MapViewOfFile3)>
          pMapViewOfFile3(L"kernelbase.dll", "MapViewOfFile3");
      if (!pMapViewOfFile3) {
        return Nothing();
      }

      // NB: MEM_ADDRESS_REQUIREMENTS::HighestEndingAddress is *inclusive*
      MEM_ADDRESS_REQUIREMENTS memReq = {
          const_cast<uint8_t*>(aRangeMin),
          const_cast<uint8_t*>(aRangeMaxExcl - 1)};

      MEM_EXTENDED_PARAMETER memParam = {};
      memParam.Type = MemExtendedParameterAddressRequirements;
      memParam.Pointer = &memReq;

      return Some(pMapViewOfFile3(mapping, aProcess, nullptr, 0, aSize, 0,
                                  PAGE_EXECUTE_READ, &memParam, 1));
    };

    mRemoteView = MMPolicyBase::Reserve(mProcess, mReservationSize, reserveFn,
                                        reserveWithinRangeFn, aBounds);
    if (!mRemoteView) {
      return 0;
    }

    return mReservationSize;
  }

  bool MaybeCommitNextPage(const uint32_t aRequestedOffset,
                           const uint32_t aRequestedLength) {
    if (!(*this)) {
      return false;
    }

    uint32_t limit = aRequestedOffset + aRequestedLength - 1;
    if (limit < mCommitOffset) {
      // No commit required
      return true;
    }

    MOZ_DIAGNOSTIC_ASSERT(mCommitOffset < mReservationSize);
    if (mCommitOffset >= mReservationSize) {
      return false;
    }

    // Committing RW pages in the parent also commits the corresponding RX pages
    // in the child (see TestSharedMappingCommit).
    PVOID local = ::VirtualAlloc(mLocalView + mCommitOffset, GetPageSize(),
                                 MEM_COMMIT, PAGE_READWRITE);
    if (!local) {
      return false;
    }

    mCommitOffset += GetPageSize();
    return true;
  }

 private:
  void Destroy() {
    // We always leak the remote view
    if (mLocalView) {
      ::UnmapViewOfFile(mLocalView);
      mLocalView = nullptr;
    }

    if (mMapping) {
      ::CloseHandle(mMapping);
      mMapping = nullptr;
    }

    if (mProcess) {
      ::CloseHandle(mProcess);
      mProcess = nullptr;
    }
  }

 private:
  HANDLE mProcess;
  HANDLE mMapping;
  uint8_t* mLocalView;
  PVOID mRemoteView;
  uint32_t mReservationSize;
  uint32_t mCommitOffset;

  static const DWORD kAccessFlags = PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                    PROCESS_VM_WRITE;
};

}  // namespace interceptor
}  // namespace mozilla

#endif  // mozilla_interceptor_MMPolicies_h
