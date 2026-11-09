/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// PHC is a probabilistic heap checker. A tiny fraction of randomly chosen heap
// allocations are subject to some expensive checking via the use of OS page
// access protection. A failed check triggers a crash, whereupon useful
// information about the failure is put into the crash report. The cost and
// coverage for each user is minimal, but spread over the entire user base the
// coverage becomes significant.
//
// The idea comes from Chromium, where it is called GWP-ASAN. (Firefox uses PHC
// as the name because GWP-ASAN is long, awkward, and doesn't have any
// particular meaning.)
//
// In the current implementation up to 64 allocations per process can become
// PHC allocations. These allocations must be page-sized or smaller. Each PHC
// allocation gets its own page, and when the allocation is freed its page is
// marked inaccessible until the page is reused for another allocation. This
// means that a use-after-free defect (which includes double-frees) will be
// caught if the use occurs before the page is reused for another allocation.
// The crash report will contain stack traces for the allocation site, the free
// site, and the use-after-free site, which is often enough to diagnose the
// defect.
//
// Also, each PHC allocation is followed by a guard page. The PHC allocation is
// positioned so that its end abuts the guard page (or as close as possible,
// given alignment constraints). This means that a bounds violation at the end
// of the allocation (overflow) will be caught. The crash report will contain
// stack traces for the allocation site and the bounds violation use site,
// which is often enough to diagnose the defect.
//
// (A bounds violation at the start of the allocation (underflow) will not be
// caught, unless it is sufficiently large to hit the preceding allocation's
// guard page, which is not that likely. It would be possible to look more
// assiduously for underflow by randomly placing some allocations at the end of
// the page and some at the start of the page, and GWP-ASAN does this. PHC does
// not, however, because overflow is likely to be much more common than
// underflow in practice.)
//
// We use a simple heuristic to categorize a guard page access as overflow or
// underflow: if the address falls in the lower half of the guard page, we
// assume it is overflow, otherwise we assume it is underflow. More
// sophisticated heuristics are possible, but this one is very simple, and it is
// likely that most overflows/underflows in practice are very close to the page
// boundary.
//
// The design space for the randomization strategy is large. The current
// implementation has a large random delay before it starts operating, and a
// small random delay between each PHC allocation attempt. Each freed PHC
// allocation is quarantined for a medium random delay before being reused, in
// order to increase the chance of catching UAFs.
//
// The basic cost of PHC's operation is as follows.
//
// - The physical memory cost is 64 pages plus some metadata (including stack
//   traces) for each page. This amounts to 256 KiB per process on
//   architectures with 4 KiB pages and 1024 KiB on macOS/AArch64 which uses
//   16 KiB pages.
//
// - The virtual memory cost is the physical memory cost plus the guard pages:
//   another 64 pages. This amounts to another 256 KiB per process on
//   architectures with 4 KiB pages and 1024 KiB on macOS/AArch64 which uses
//   16 KiB pages. PHC is currently only enabled on 64-bit platforms so the
//   impact of the virtual memory usage is negligible.
//
// - Every allocation requires a size check and a decrement-and-check of an
//   atomic counter. When the counter reaches zero a PHC allocation can occur,
//   which involves marking a page as accessible and getting a stack trace for
//   the allocation site. Otherwise, mozjemalloc performs the allocation.
//
// - Every deallocation requires a range check on the pointer to see if it
//   involves a PHC allocation. (The choice to only do PHC allocations that are
//   a page or smaller enables this range check, because the 64 pages are
//   contiguous. Allowing larger allocations would make this more complicated,
//   and we definitely don't want something as slow as a hash table lookup on
//   every deallocation.) PHC deallocations involve marking a page as
//   inaccessible and getting a stack trace for the deallocation site.
//
// Note that calls to realloc(), free(), and malloc_usable_size() will
// immediately crash if the given pointer falls within a page allocation's
// page, but does not point to the start of the allocation itself.
//
//   void* p = malloc(64);
//   free(p + 1);     // p+1 doesn't point to the allocation start; crash
//
// Such crashes will not have the PHC fields in the crash report.
//
// PHC-specific tests can be run with the following commands:
// - gtests: `./mach gtest '*PHC*'`
// - xpcshell-tests: `./mach test toolkit/crashreporter/test/unit`
//   - This runs some non-PHC tests as well.

#include "PHC.h"

#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <bit>

#ifdef XP_WIN
#  include <process.h>
#else
#  include <sys/mman.h>
#  include <sys/types.h>
#  include <pthread.h>
#  include <unistd.h>
#endif

#include "mozjemalloc.h"
#include "BaseArray.h"
#include "BaseAlloc.h"
#include "Chunk.h"
#include "FdPrintf.h"
#include "Mutex.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Maybe.h"
#include "mozilla/StackWalk.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/XorShift128PlusRNG.h"

using namespace mozilla;

//---------------------------------------------------------------------------
// Utilities
//---------------------------------------------------------------------------

#ifdef ANDROID
// Android doesn't have pthread_atfork defined in pthread.h.
extern "C" MOZ_EXPORT int pthread_atfork(void (*)(void), void (*)(void),
                                         void (*)(void));
#endif

#ifndef DISALLOW_COPY_AND_ASSIGN
#  define DISALLOW_COPY_AND_ASSIGN(T) \
    T(const T&);                      \
    void operator=(const T&)
#endif

//---------------------------------------------------------------------------
// Stack traces
//---------------------------------------------------------------------------

// This code is similar to the equivalent code within DMD.

class StackTrace : public phc::StackTrace {
 public:
  StackTrace() = default;

  void Clear() { mLength = 0; }

  void Fill();

 private:
  static void StackWalkCallback(uint32_t aFrameNumber, void* aPc, void* aSp,
                                void* aClosure) {
    StackTrace* st = (StackTrace*)aClosure;
    MOZ_ASSERT(st->mLength < kMaxFrames);
    st->mPcs[st->mLength] = aPc;
    st->mLength++;
    MOZ_ASSERT(st->mLength == aFrameNumber);
  }
};

// WARNING WARNING WARNING: this function must only be called when PHC::mMutex
// is *not* locked, otherwise we might get deadlocks.
//
// How? On Windows, MozStackWalk() can lock a mutex, M, from the shared library
// loader. Another thread might call malloc() while holding M locked (when
// loading a shared library) and try to lock PHC::mMutex, causing a deadlock.
// So PHC::mMutex can't be locked during the call to MozStackWalk(). (For
// details, see https://bugzilla.mozilla.org/show_bug.cgi?id=374829#c8. On
// Linux, something similar can happen; see bug 824340. So we just disallow it
// on all platforms.)
//
// In DMD, to avoid this problem we temporarily unlock the equivalent mutex for
// the MozStackWalk() call. But that's grotty, and things are a bit different
// here, so we just require that stack traces be obtained before locking
// PHC::mMutex.
//
// Unfortunately, there is no reliable way at compile-time or run-time to ensure
// this pre-condition. Hence this large comment.
//
void StackTrace::Fill() {
  mLength = 0;

// These ifdefs should be kept in sync with the conditions in
// phc_implies_frame_pointers in build/moz.configure/memory.configure
#if defined(XP_WIN) && defined(_M_IX86)
  // This avoids MozStackWalk(), which causes unusably slow startup on Win32
  // when it is called during static initialization (see bug 1241684).
  //
  // This code is cribbed from the Gecko Profiler, which also uses
  // FramePointerStackWalk() on Win32: Registers::SyncPopulate() for the
  // frame pointer, and GetStackTop() for the stack end.
  CONTEXT context;
  RtlCaptureContext(&context);
  void** fp = reinterpret_cast<void**>(context.Ebp);

  PNT_TIB pTib = reinterpret_cast<PNT_TIB>(NtCurrentTeb());
  void* stackEnd = static_cast<void*>(pTib->StackBase);
  FramePointerStackWalk(StackWalkCallback, kMaxFrames, this, fp, stackEnd);
#elif defined(XP_DARWIN)
  // This avoids MozStackWalk(), which has become unusably slow on Mac due to
  // changes in libunwind.
  //
  // This code is cribbed from the Gecko Profiler, which also uses
  // FramePointerStackWalk() on Mac: Registers::SyncPopulate() for the frame
  // pointer, and GetStackTop() for the stack end.
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wframe-address"
  void** fp = reinterpret_cast<void**>(__builtin_frame_address(1));
#  pragma GCC diagnostic pop
  void* stackEnd = pthread_get_stackaddr_np(pthread_self());
  FramePointerStackWalk(StackWalkCallback, kMaxFrames, this, fp, stackEnd);
#else
  MozStackWalk(StackWalkCallback, nullptr, kMaxFrames, this);
#endif
}

//---------------------------------------------------------------------------
// Logging
//---------------------------------------------------------------------------

// Change this to 1 to enable some PHC logging. Useful for debugging.
#define PHC_LOGGING 0

static void Log(const char* fmt, ...);

//---------------------------------------------------------------------------
// Global state
//---------------------------------------------------------------------------

// Throughout this entire file time is measured as the number of sub-page
// allocations performed (by PHC and mozjemalloc combined). `Time` is 64-bit
// because we could have more than 2**32 allocations in a long-running session.
// `Delay` is 32-bit because the delays used within PHC are always much smaller
// than 2**32.  Delay must be unsigned so that std::has_single_bit() can work on
// some Delay values.
using Time = uint64_t;   // A moment in time.
using Delay = uint32_t;  // A time duration.
static constexpr Delay DELAY_MAX = UINT32_MAX / 2;

// PHC only runs if the page size is 4 KiB; anything more is uncommon and would
// use too much memory. So we hardwire this size for all platforms but macOS
// on ARM processors. For the latter we make an exception because the minimum
// page size supported is 16KiB so there's no way to go below that.
static const size_t kPhcPageSize =
#if defined(XP_DARWIN) && defined(__aarch64__)
    16384
#else
    4096
#endif
    ;

// We align the PHC area to a multiple of the jemalloc and JS GC chunk size
// (both use 1MB aligned chunks) so that their address computations don't lead
// from non-PHC memory into PHC memory causing misleading PHC stacks to be
// attached to a crash report.
static const size_t kPhcAlign = 1024 * 1024;

static_assert(std::has_single_bit(kPhcAlign));
static_assert((kPhcAlign % kPhcPageSize) == 0);

// PHC will reserve some address space this large, then depending on runtime
// configuration will use a smaller fraction of it.  Making
// kPhcVirtualReservation the upper-bound of PHC's memory size.
//  * On 32bit systems with less available address space we choose a more
//    moderate value.
//  * On 64bit systems we set the limit to so that there are no more than 32,768
//    mappings, half of Linux's default limit (Bug 1969856).  For 4KB pages
//    that's 128MB.
#ifdef HAVE_64BIT_BUILD
#  if defined(XP_DARWIN) && defined(__aarch64__)
static const size_t kPhcVirtualReservation = 512 * 1024 * 1024;
#  else
static const size_t kPhcVirtualReservation = 128 * 1024 * 1024;
#  endif
#else
static const size_t kPhcVirtualReservation = 2 * 1024 * 1024;
#endif

// The amount to decrement from the shared allocation delay each time a thread's
// local allocation delay reaches zero.
static const Delay kDelayDecrementAmount = 256;

// When PHC is disabled on the current thread wait this many allocations before
// accessing sAllocDelay once more.
static const Delay kDelayBackoffAmount = 64;

// When PHC is disabled globally reset the shared delay by this many allocations
// to keep code running on the fast path.
static const Delay kDelayResetWhenDisabled = 64 * 1024;

// The default state for PHC.  Either Enabled or OnlyFree.
#define DEFAULT_STATE mozilla::phc::OnlyFree

// The maximum time.
static const Time kMaxTime = ~(Time(0));

// Truncate aRnd to the range (1 .. aAvgDelay*2). If aRnd is random, this
// results in an average value of aAvgDelay + 0.5, which is close enough to
// aAvgDelay. aAvgDelay must be a power-of-two for speed.
constexpr Delay Rnd64ToDelay(Delay aAvgDelay, uint64_t aRnd) {
  MOZ_ASSERT(std::has_single_bit(aAvgDelay), "must be a power of two");

  return (aRnd & (uint64_t(aAvgDelay) * 2 - 1)) + 1;
}

static Delay CheckProbability(int64_t aProb) {
  // Limit delays calculated from prefs to 0x80000000, this is the largest
  // power-of-two that fits in a Delay since it is a uint32_t.
  // The minimum is 2 that way not every allocation goes straight to PHC.
  return RoundUpPow2(std::clamp(aProb, int64_t(2), int64_t(0x80000000)));
}

// On MacOS, the first __thread/thread_local access calls malloc, which leads
// to an infinite loop. So we use pthread-based TLS instead, which somehow
// doesn't have this problem.
#if !defined(XP_DARWIN)
#  define PHC_THREAD_LOCAL(T) MOZ_THREAD_LOCAL(T)
#else
#  define PHC_THREAD_LOCAL(T) \
    detail::ThreadLocal<T, detail::ThreadLocalKeyStorage>
#endif

enum class AllocPageState {
  NeverAllocated = 0,
  InUse = 1,
  Freed = 2,
};

// Metadata for each allocation page.
class AllocPageInfo {
 public:
  AllocPageInfo()
      : mState(AllocPageState::NeverAllocated),
        mBaseAddr(nullptr),
        mReuseTime(0) {}

  // The current allocation page state.
  AllocPageState mState;

  // The arena that the allocation is nominally from. This isn't meaningful
  // within PHC, which has no arenas. But it is necessary for reallocation of
  // page allocations as normal allocations, such as in this code:
  //
  //   p = moz_arena_malloc(arenaId, 4096);
  //   realloc(p, 8192);
  //
  // The realloc is more than one page, and thus too large for PHC to handle.
  // Therefore, if PHC handles the first allocation, it must ask mozjemalloc
  // to allocate the 8192 bytes in the correct arena, and to do that, it must
  // call MozJemalloc::moz_arena_malloc with the correct arenaId under the
  // covers. Therefore it must record that arenaId.
  //
  // This field is also needed for jemalloc_ptr_info() to work, because it
  // also returns the arena ID (but only in debug builds).
  //
  // - NeverAllocated: must be 0.
  // - InUse | Freed: can be any valid arena ID value.
  Maybe<arena_id_t> mArenaId;

  // The starting address of the allocation. Will not be the same as the page
  // address unless the allocation is a full page.
  // - NeverAllocated: must be 0.
  // - InUse | Freed: must be within the allocation page.
  uint8_t* mBaseAddr;

  // Usable size is computed as the number of bytes between the pointer and
  // the end of the allocation page. This might be bigger than the requested
  // size, especially if an outsized alignment is requested.
  size_t UsableSize() const {
    return mState == AllocPageState::NeverAllocated
               ? 0
               : kPhcPageSize - (reinterpret_cast<uintptr_t>(mBaseAddr) &
                                 (kPhcPageSize - 1));
  }

  // The internal fragmentation for this allocation.
  size_t FragmentationBytes() const {
    MOZ_ASSERT(kPhcPageSize >= UsableSize());
    return mState == AllocPageState::InUse ? kPhcPageSize - UsableSize() : 0;
  }

  // The allocation stack.
  // - NeverAllocated: Nothing.
  // - InUse | Freed: Some.
  Maybe<StackTrace> mAllocStack;

  // The free stack.
  // - NeverAllocated | InUse: Nothing.
  // - Freed: Some.
  Maybe<StackTrace> mFreeStack;

  // The time at which the page is available for reuse, as measured against
  // mNow. When the page is in use this value will be kMaxTime.
  // - NeverAllocated: must be 0.
  // - InUse: must be kMaxTime.
  // - Freed: must be > 0 and < kMaxTime.
  Time mReuseTime;

#if PHC_LOGGING
  Time mFreeTime;
#endif

  // The next index for a free list of pages.`
  Maybe<uintptr_t> mNextPage;

  void AssertInUse() const {
    MOZ_ASSERT(mState == AllocPageState::InUse);
    // There is nothing to assert about aPage.mArenaId.
    MOZ_ASSERT(mBaseAddr);
    MOZ_ASSERT(UsableSize() > 0);
    MOZ_ASSERT(mAllocStack.isSome());
    MOZ_ASSERT(mFreeStack.isNothing());
    MOZ_ASSERT(mReuseTime == kMaxTime);
    MOZ_ASSERT(!mNextPage);
  }

  void AssertNotInUse() const {
    // We can assert a lot about `NeverAllocated` pages, but not much about
    // `Freed` pages.
#ifdef DEBUG
    bool isFresh = mState == AllocPageState::NeverAllocated;
    MOZ_ASSERT(isFresh || mState == AllocPageState::Freed);
    MOZ_ASSERT_IF(isFresh, mArenaId == Nothing());
    MOZ_ASSERT(isFresh == (mBaseAddr == nullptr));
    MOZ_ASSERT(isFresh == (mAllocStack.isNothing()));
    MOZ_ASSERT(isFresh == (mFreeStack.isNothing()));
    MOZ_ASSERT(mReuseTime != kMaxTime);
#endif
  }

  bool IsPageInUse() const { return mState == AllocPageState::InUse; }
  bool IsPageFreed() const { return mState == AllocPageState::Freed; }

  bool IsPageAllocatable(Time aNow) const {
    return !IsPageInUse() && aNow >= mReuseTime;
  }

  void SetInUse(const Maybe<arena_id_t>& aArenaId, uint8_t* aBaseAddr,
                const StackTrace& aAllocStack) {
    AssertNotInUse();
    mState = AllocPageState::InUse;
    mArenaId = aArenaId;
    mBaseAddr = aBaseAddr;
    mAllocStack = Some(aAllocStack);
    mFreeStack = Nothing();
    mReuseTime = kMaxTime;

    MOZ_ASSERT(!mNextPage);
  }

  void ResizeInUse(const Maybe<arena_id_t>& aArenaId, uint8_t* aNewBaseAddr,
                   const StackTrace& aAllocStack) {
    AssertInUse();

    // page.mState is not changed.
    if (aArenaId.isSome()) {
      // Crash if the arenas don't match.
      MOZ_RELEASE_ASSERT(mArenaId == aArenaId);
    }
    mBaseAddr = aNewBaseAddr;
    // We could just keep the original alloc stack, but the realloc stack is
    // more recent and therefore seems more useful.
    mAllocStack = Some(aAllocStack);
    // mFreeStack is not changed.
    // mReuseTime is not changed.
    // mNextPage is not changed.
  }

  void SetPageFreed(const Maybe<arena_id_t>& aArenaId,
                    const StackTrace& aFreeStack, Delay aReuseDelay,
                    Time aNow) {
    AssertInUse();

    mState = AllocPageState::Freed;

    // page.mArenaId is left unchanged, for jemalloc_ptr_info() calls that
    // occur after freeing (e.g. in the PtrInfo test in TestJemalloc.cpp).
    if (aArenaId.isSome()) {
      // Crash if the arenas don't match.
      MOZ_RELEASE_ASSERT(mArenaId == aArenaId);
    }

    // page.musableSize is left unchanged, for reporting on UAF, and for
    // jemalloc_ptr_info() calls that occur after freeing (e.g. in the PtrInfo
    // test in TestJemalloc.cpp).

    // page.mAllocStack is left unchanged, for reporting on UAF.

    mFreeStack = Some(aFreeStack);
#if PHC_LOGGING
    mFreeTime = aNow;
#endif
    mReuseTime = aNow + aReuseDelay;
  }
};

// The virtual address space reserved by PHC.  It is shared, immutable global
// state. Initialized by phc_init() and never changed after that. phc_init()
// runs early enough that no synchronization is needed.
class PHCRegion {
 private:
  // The bounds of PHC's virtual address space.  These are only ever set once
  // before any threads are spawned, after that they're read only, and therefore
  // can be accessed without a lock.
  uint8_t* mPagesStart = nullptr;
  uint8_t* mPagesLimit = nullptr;

 public:
  // Allocates the allocation pages and the guard pages, contiguously.
  bool AllocVirtualAddresses() {
    MOZ_ASSERT(!mPagesStart || !mPagesLimit);

    // The memory allocated here is never freed, because it would happen at
    // process termination when it would be of little use.

    // On Windows in particular we want to control how the memory is initially
    // reserved.  Windows pages memory in immediately which creates performance
    // problems and could affect stability.
    void* pages =
        pages_mmap_aligned(kPhcVirtualReservation, kPhcAlign, ReserveOnly);
    if (!pages) {
      return false;
    }

    mPagesStart = static_cast<uint8_t*>(pages);
    mPagesLimit = mPagesStart + kPhcVirtualReservation;
    Log("AllocVirtualAddresses at %p..%p\n", mPagesStart, mPagesLimit);
    return true;
  }

  void ReleaseVirtualAddresses() {
    MOZ_ASSERT(!!mPagesStart && !!mPagesLimit);

    pages_unmap(mPagesStart, reinterpret_cast<uintptr_t>(mPagesLimit) -
                                 reinterpret_cast<uintptr_t>(mPagesStart));
    mPagesStart = nullptr;
    mPagesLimit = nullptr;
  }

  constexpr PHCRegion() {}

  bool IsInFirstGuardPage(const void* aPtr) {
    MOZ_ASSERT(mPagesStart != nullptr && mPagesLimit != nullptr);
    return mPagesStart <= aPtr && aPtr < mPagesStart + kPhcPageSize;
  }

  // Get the address of the allocation page referred to via an index. Used when
  // marking the page as accessible/inaccessible.
  uint8_t* AllocPagePtr(uintptr_t aIndex) {
    MOZ_ASSERT(mPagesStart != nullptr && mPagesLimit != nullptr);
    // Multiply by two and add one to account for allocation pages *and* guard
    // pages.
    return mPagesStart + (2 * aIndex + 1) * kPhcPageSize;
  }

  MOZ_ALWAYS_INLINE bool WithinBounds(const void* aPtr) const {
    MOZ_ASSERT(mPagesStart && mPagesLimit);
    return aPtr >= mPagesStart && aPtr < mPagesLimit;
  }

  const uint8_t* PagesStart() const { return mPagesStart; }

  size_t ReservedBytes() const {
    return mPagesStart ? kPhcVirtualReservation - kPhcPageSize : 0;
  }
};

class PtrKind;

// Shared, mutable global state.  Many fields are protected by sMutex; functions
// that access those feilds should take a PHCLock as proof that mMutex is held.
// Other fields are TLS or Atomic and don't need the lock.
class PHC : public BaseAllocClass {
 public:
  // The RNG seeds here are poor, but non-reentrant since this can be called
  // from malloc().  SetState() will reset the RNG later.
  PHC() : mRNG(RandomSeed<1>(), RandomSeed<2>()) {
    mMutex.Init();
    if (!tlsIsDisabled.init()) {
      MOZ_CRASH();
    }
    if (!tlsAllocDelay.init()) {
      MOZ_CRASH();
    }
    if (!tlsLastDelay.init()) {
      MOZ_CRASH();
    }
  }

  bool Init() {
    // This function is part of PHC's very early initialisation, see
    // phc_init(), and if PHC is default-on it'll start marking allocations
    // and we must setup the delay.  However once XPCOM starts it'll call
    // SetState() which will re-initialise the RNG and allocation delay.

#ifdef NIGHTLY_BUILD
    const size_t array_size = 16 * 1024 * 1024;
#else
    // Before Bug 1867191 PHC used no more than approximately 1.1MB when it was
    // set to a round number of 256 pages.  To keep the size the same we now
    // specify this strange total size, but will follow-up with a more sensible
    // maximum in the future.
    const size_t array_size = (1024 + 128) * 1024;
#endif
    if (!Resize(array_size)) {
      return false;
    }

    {
      MutexAutoLock lock(mMutex);
      ForceSetNewAllocDelay(Rnd64ToDelay(mAvgFirstAllocDelay, Random64()));
    }

    return true;
  }

  bool Resize(size_t aSizeBytes) {
    // -1 since the last page in the virtual address space must be a guard page.
    size_t max_pages = (kPhcVirtualReservation / kPhcPageSize / 2) - 1;
    size_t size_pages = aSizeBytes / kPhcPageSize;
    size_pages = std::min(size_pages, max_pages);

    MutexAutoLock lock(mMutex);

    size_t old_size_pages = NumAllocPages();
    if (size_pages > old_size_pages) {
      Log("Growing PHC storage from %zu to %zu\n", old_size_pages, size_pages);
      if (!mAllocPages.GrowTo(size_pages)) {
        return false;
      }

      for (size_t i = old_size_pages; i < size_pages; i++) {
        AppendPageToFreeList(i);
      }
    } else if (size_pages < old_size_pages) {
      Log("Shrink requested and ignored.");
    }

    return true;
  }

  uint64_t Random64() MOZ_REQUIRES(mMutex) { return mRNG.next(); }

  PtrKind GetPtrKind(const void* aPtr);

  // Get the address of the allocation page referred to via an index. Used
  // when checking pointers against page boundaries.
  uint8_t* AllocPageBaseAddr(uintptr_t aIndex) MOZ_REQUIRES(mMutex) {
    return mAllocPages[aIndex].mBaseAddr;
  }

  Maybe<arena_id_t> PageArena(uintptr_t aIndex) MOZ_REQUIRES(mMutex) {
    const AllocPageInfo& page = mAllocPages[aIndex];
    page.AssertInUse();

    return page.mArenaId;
  }

  size_t PageUsableSize(uintptr_t aIndex) MOZ_REQUIRES(mMutex) {
    const AllocPageInfo& page = mAllocPages[aIndex];
    page.AssertInUse();

    return page.UsableSize();
  }

  void GetMemoryUsage(phc::MemoryUsage& aInfo) MOZ_EXCLUDES(mMutex) {
    MutexAutoLock lock(mMutex);

    aInfo = phc::MemoryUsage();
    for (const auto& page : mAllocPages) {
      if (page.IsPageInUse()) {
        aInfo.mAllocatedBytes += page.UsableSize();
        aInfo.mFragmentationBytes += page.FragmentationBytes();
      }
    }

    // We know `this` is heap allocated.
    aInfo.mMetadataBytes = MallocSize() + mAllocPages.SizeOfExcludingThis();
  }

  void SetPageInUse(uintptr_t aIndex, const Maybe<arena_id_t>& aArenaId,
                    uint8_t* aBaseAddr, const StackTrace& aAllocStack)
      MOZ_REQUIRES(mMutex) {
    mAllocPages[aIndex].SetInUse(aArenaId, aBaseAddr, aAllocStack);
  }

#if PHC_LOGGING
  Time GetFreeTime(uintptr_t aIndex) const MOZ_REQUIRES(mMutex) {
    return mAllocPages[aIndex].mFreeTime;
  }
#endif

  void ResizePageInUse(uintptr_t aIndex, const Maybe<arena_id_t>& aArenaId,
                       uint8_t* aNewBaseAddr, const StackTrace& aAllocStack)
      MOZ_REQUIRES(mMutex) {
    mAllocPages[aIndex].ResizeInUse(aArenaId, aNewBaseAddr, aAllocStack);
  };

  void SetPageFreed(uintptr_t aIndex, const Maybe<arena_id_t>& aArenaId,
                    const StackTrace& aFreeStack, Delay aReuseDelay)
      MOZ_REQUIRES(mMutex) {
    AllocPageInfo& page = mAllocPages[aIndex];

    page.SetPageFreed(aArenaId, aFreeStack, aReuseDelay, Now());

    MOZ_ASSERT(!page.mNextPage);
    AppendPageToFreeList(aIndex);
  }

  static void CrashOnGuardPage(void* aPtr) {
    // An operation on a guard page? This is a bounds violation. Deliberately
    // touch the page in question to cause a crash that triggers the usual PHC
    // machinery.
    Log("CrashOnGuardPage(%p), bounds violation\n", aPtr);
    *static_cast<uint8_t*>(aPtr) = 0;
    MOZ_CRASH("unreachable");
  }

  void EnsureValidAndInUse(void* aPtr, uintptr_t aIndex) MOZ_REQUIRES(mMutex) {
    const AllocPageInfo& page = mAllocPages[aIndex];

    // The pointer must point to the start of the allocation.
    MOZ_RELEASE_ASSERT(page.mBaseAddr == aPtr);

    if (page.mState == AllocPageState::Freed) {
      Log("EnsureValidAndInUse(%p), use-after-free\n", aPtr);
      // An operation on a freed page? This is a particular kind of
      // use-after-free. Deliberately touch the page in question, in order to
      // cause a crash that triggers the usual PHC machinery. But unlock mMutex
      // first, because that self-same PHC machinery needs to re-lock it, and
      // the crash causes non-local control flow so mMutex won't be unlocked
      // the normal way in the caller.
      mMutex.Unlock();
      *static_cast<uint8_t*>(aPtr) = 0;
      MOZ_CRASH("unreachable");
    }
  }

  // This expects sPHC::mMutex to be locked but can't check it with a parameter
  // since we try-lock it.
  void FillAddrInfo(uintptr_t aIndex, const void* aBaseAddr, bool isGuardPage,
                    phc::AddrInfo& aOut) MOZ_REQUIRES(mMutex) {
    const AllocPageInfo& page = mAllocPages[aIndex];
    if (isGuardPage) {
      aOut.mKind = phc::AddrInfo::Kind::GuardPage;
    } else {
      switch (page.mState) {
        case AllocPageState::NeverAllocated:
          aOut.mKind = phc::AddrInfo::Kind::NeverAllocatedPage;
          break;

        case AllocPageState::InUse:
          aOut.mKind = phc::AddrInfo::Kind::InUsePage;
          break;

        case AllocPageState::Freed:
          aOut.mKind = phc::AddrInfo::Kind::FreedPage;
          break;

        default:
          MOZ_CRASH();
      }
    }
    aOut.mBaseAddr = page.mBaseAddr;
    aOut.mUsableSize = page.UsableSize();
    aOut.mAllocStack = page.mAllocStack;
    aOut.mFreeStack = page.mFreeStack;
  }

  void FillJemallocPtrInfo(const void* aPtr, uintptr_t aIndex,
                           jemalloc_ptr_info_t* aInfo) MOZ_REQUIRES(mMutex) {
    const AllocPageInfo& page = mAllocPages[aIndex];
    switch (page.mState) {
      case AllocPageState::NeverAllocated:
        break;

      case AllocPageState::InUse: {
        // Only return TagLiveAlloc if the pointer is within the bounds of the
        // allocation's usable size.
        uint8_t* base = page.mBaseAddr;
        uint8_t* limit = base + page.UsableSize();
        if (base <= aPtr && aPtr < limit) {
          *aInfo = {TagLiveAlloc, page.mBaseAddr, page.UsableSize(),
                    page.mArenaId.valueOr(0)};
          return;
        }
        break;
      }

      case AllocPageState::Freed: {
        // Only return TagFreedAlloc if the pointer is within the bounds of the
        // former allocation's usable size.
        uint8_t* base = page.mBaseAddr;
        uint8_t* limit = base + page.UsableSize();
        if (base <= aPtr && aPtr < limit) {
          *aInfo = {TagFreedAlloc, page.mBaseAddr, page.UsableSize(),
                    page.mArenaId.valueOr(0)};
          return;
        }
        break;
      }

      default:
        MOZ_CRASH();
    }

    // Pointers into guard pages will end up here, as will pointers into
    // allocation pages that aren't within the allocation's bounds.
    *aInfo = {TagUnknown, nullptr, 0, 0};
  }

#ifndef XP_WIN
  static void prefork() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    PHC::sPHC->mMutex.Lock();
  }
  static void postfork_parent() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    PHC::sPHC->mMutex.Unlock();
  }
  static void postfork_child() { PHC::sPHC->mMutex.Init(); }
#endif

  void IncPageAllocHits() MOZ_REQUIRES(mMutex) {
#if PHC_LOGGING
    mPageAllocHits++;
#endif
  }
  void IncPageAllocMisses() MOZ_REQUIRES(mMutex) {
#if PHC_LOGGING
    mPageAllocMisses++;
#endif
  }

  phc::PHCStats GetPageStatsLocked() MOZ_REQUIRES(mMutex) {
    phc::PHCStats stats;

    for (const auto& page : mAllocPages) {
      stats.mSlotsAllocated += page.IsPageInUse() ? 1 : 0;
      stats.mSlotsFreed += page.IsPageFreed() ? 1 : 0;
    }
    stats.mSlotsUnused =
        NumAllocPages() - stats.mSlotsAllocated - stats.mSlotsFreed;

    return stats;
  }

  phc::PHCStats GetPageStats() MOZ_EXCLUDES(mMutex) {
    MutexAutoLock lock(mMutex);
    return GetPageStatsLocked();
  }

#if PHC_LOGGING
  size_t PageAllocHits() MOZ_REQUIRES(mMutex) { return mPageAllocHits; }
  size_t PageAllocAttempts() MOZ_REQUIRES(mMutex) {
    return mPageAllocHits + mPageAllocMisses;
  }

  // This is an integer because FdPrintf only supports integer printing.
  size_t PageAllocHitRate() MOZ_REQUIRES(mMutex) {
    return mPageAllocHits * 100 / (mPageAllocHits + mPageAllocMisses);
  }
#endif

  void LogNoAlloc(size_t aReqSize, size_t aAlignment, Delay newAllocDelay);

  // Should we make new PHC allocations?
  bool ShouldMakeNewAllocations() const {
    return mPhcState == mozilla::phc::Enabled;
  }

  using PHCState = mozilla::phc::PHCState;
  void SetState(PHCState aState) {
    if (mPhcState != PHCState::Enabled && aState == PHCState::Enabled) {
      MutexAutoLock lock(mMutex);
      // Reset the RNG at this point with a better seed.
      ResetRNG();
      ForceSetNewAllocDelay(Rnd64ToDelay(mAvgFirstAllocDelay, Random64()));
    }

    mPhcState = aState;
  }

  void ResetRNG() MOZ_REQUIRES(mMutex) {
    mRNG = non_crypto::XorShift128PlusRNG(RandomSeed<0>(), RandomSeed<1>());
  }

  void SetProbabilities(int64_t aAvgDelayFirst, int64_t aAvgDelayNormal,
                        int64_t aAvgDelayPageReuse) MOZ_EXCLUDES(mMutex) {
    MutexAutoLock lock(mMutex);

    mAvgFirstAllocDelay = CheckProbability(aAvgDelayFirst);
    mAvgAllocDelay = CheckProbability(aAvgDelayNormal);
    mAvgPageReuseDelay = CheckProbability(aAvgDelayPageReuse);
  }

  static void DisableOnCurrentThread() {
    MOZ_ASSERT(!tlsIsDisabled.get());
    tlsIsDisabled.set(true);
  }

  void EnableOnCurrentThread() {
    MOZ_ASSERT(tlsIsDisabled.get());
    tlsIsDisabled.set(false);
  }

  static bool IsDisabledOnCurrentThread() { return tlsIsDisabled.get(); }

  static Time Now() {
    if (!sPHC) {
      return 0;
    }

    return sPHC->mNow;
  }

  void AdvanceNow(uint32_t delay = 0) {
    mNow += tlsLastDelay.get() - delay;
    tlsLastDelay.set(delay);
  }

  // Decrements the delay and returns true if it's time to make a new PHC
  // allocation.
  static bool DecrementDelay() {
    const Delay alloc_delay = tlsAllocDelay.get();

    if (MOZ_LIKELY(alloc_delay > 0)) {
      tlsAllocDelay.set(alloc_delay - 1);
      return false;
    }
    // The local delay has expired, check the shared delay.  This path is also
    // executed on a new thread's first allocation, the result is the same: all
    // the thread's TLS fields will be initialised.

    // This accesses sPHC but we want to ensure it's still a static member
    // function so that sPHC isn't dereferenced until after the hot path above.
    MOZ_ASSERT(sPHC);
    sPHC->AdvanceNow();

    // Use an atomic fetch-and-subtract.  This uses unsigned underflow semantics
    // to avoid doing a full compare-and-swap.
    Delay new_delay = (sAllocDelay -= kDelayDecrementAmount);
    Delay old_delay = new_delay + kDelayDecrementAmount;
    if (MOZ_LIKELY(new_delay < DELAY_MAX)) {
      // Normal case, we decremented the shared delay but it's not yet
      // underflowed.
      tlsAllocDelay.set(kDelayDecrementAmount);
      tlsLastDelay.set(kDelayDecrementAmount);
      Log("Update sAllocDelay <- %zu, tlsAllocDelay <- %zu\n",
          size_t(new_delay), size_t(kDelayDecrementAmount));
      return false;
    }

    if (old_delay < new_delay) {
      // The shared delay only just underflowed, so unless we hit exactly zero
      // we should set our local counter and continue.
      Log("Update sAllocDelay <- %zu, tlsAllocDelay <- %zu\n",
          size_t(new_delay), size_t(old_delay));
      if (old_delay == 0) {
        // We don't need to set tlsAllocDelay because it's already zero, we know
        // because the condition at the beginning of this function failed.
        return true;
      }
      tlsAllocDelay.set(old_delay);
      tlsLastDelay.set(old_delay);
      return false;
    }

    // The delay underflowed on another thread or a previous failed allocation
    // by this thread.  Return true and attempt the next allocation, if the
    // other thread wins we'll check for that before committing.
    Log("Update sAllocDelay <- %zu, tlsAllocDelay <- %zu\n", size_t(new_delay),
        size_t(alloc_delay));
    return true;
  }

  static void ResetLocalAllocDelay(Delay aDelay = 0) {
    // We could take some delay from the shared delay but we'd need a
    // compare-and-swap because this is called on paths that don't make
    // allocations.  Or we can set the local delay to zero and let it get
    // initialised on the next allocation.
    tlsAllocDelay.set(aDelay);
    tlsLastDelay.set(aDelay);
  }

  static void ForceSetNewAllocDelay(Delay aNewAllocDelay) {
    Log("Setting sAllocDelay <- %zu\n", size_t(aNewAllocDelay));
    sAllocDelay = aNewAllocDelay;
    ResetLocalAllocDelay();
  }

  // Set a new allocation delay and return true if the delay was less than zero
  // (but it's unsigned so interpret it as signed) indicating that we won the
  // race to make the next allocation.
  static bool SetNewAllocDelay(Delay aNewAllocDelay) {
    bool cas_retry;
    do {
      // We read the current delay on every iteration, we consider that the PHC
      // allocation is still "up for grabs" if sAllocDelay < 0.  This is safe
      // even while other threads continuing to fetch-and-subtract sAllocDelay
      // in DecrementDelay(), up to DELAY_MAX (2^31) calls to DecrementDelay().
      Delay read_delay = sAllocDelay;
      if (read_delay < DELAY_MAX) {
        // Another thread already set a valid delay.
        Log("Observe delay %zu this thread lost the race\n",
            size_t(read_delay));
        ResetLocalAllocDelay();
        return false;
      } else {
        Log("Preparing for CAS, read sAllocDelay %zu\n", size_t(read_delay));
      }

      cas_retry = !sAllocDelay.compareExchange(read_delay, aNewAllocDelay);
      if (cas_retry) {
        Log("Lost the CAS, sAllocDelay is now %zu\n", size_t(sAllocDelay));
        cpu_pause();
        //  We raced against another thread and lost.
      }
    } while (cas_retry);
    Log("Won the CAS, set sAllocDelay = %zu\n", size_t(sAllocDelay));
    ResetLocalAllocDelay();
    return true;
  }

  static Delay LocalAllocDelay() { return tlsAllocDelay.get(); }
  static Delay SharedAllocDelay() { return sAllocDelay; }

  static Delay LastDelay() { return tlsLastDelay.get(); }

  Maybe<uintptr_t> PopNextFreeIfAllocatable(Time now) MOZ_REQUIRES(mMutex) {
    if (!mFreePageListHead) {
      return Nothing();
    }

    uintptr_t index = mFreePageListHead.value();

    MOZ_RELEASE_ASSERT(index < NumAllocPages());
    AllocPageInfo& page = mAllocPages[index];
    page.AssertNotInUse();

    if (!page.IsPageAllocatable(now)) {
      return Nothing();
    }

    mFreePageListHead = page.mNextPage;
    page.mNextPage = Nothing();
    if (!mFreePageListHead) {
      mFreePageListTail = Nothing();
    }

    return Some(index);
  }

  void UnpopNextFree(uintptr_t index) MOZ_REQUIRES(mMutex) {
    AllocPageInfo& page = mAllocPages[index];
    MOZ_ASSERT(!page.mNextPage);

    page.mNextPage = mFreePageListHead;
    mFreePageListHead = Some(index);
    if (!mFreePageListTail) {
      mFreePageListTail = Some(index);
    }
  }

  void AppendPageToFreeList(uintptr_t aIndex) MOZ_REQUIRES(mMutex) {
    MOZ_RELEASE_ASSERT(aIndex < NumAllocPages());
    AllocPageInfo& page = mAllocPages[aIndex];
    MOZ_ASSERT(!page.mNextPage);
    MOZ_ASSERT(mFreePageListHead != Some(aIndex) &&
               mFreePageListTail != Some(aIndex));

    if (!mFreePageListTail) {
      // The list is empty this page will become the beginning and end.
      MOZ_ASSERT(!mFreePageListHead);
      mFreePageListHead = Some(aIndex);
    } else {
      MOZ_ASSERT(mFreePageListTail.value() < NumAllocPages());
      AllocPageInfo& tail_page = mAllocPages[mFreePageListTail.value()];
      MOZ_ASSERT(!tail_page.mNextPage);
      tail_page.mNextPage = Some(aIndex);
    }
    page.mNextPage = Nothing();
    mFreePageListTail = Some(aIndex);
  }

 private:
  template <int N>
  uint64_t RandomSeed() {
    // An older version of this code used RandomUint64() here, but on Mac that
    // function uses arc4random(), which can allocate, which would cause
    // re-entry, which would be bad. So we just use time(), a local variable
    // address and a global variable address. These are mediocre sources of
    // entropy, but good enough for PHC.
    static_assert(N == 0 || N == 1 || N == 2, "must be 0, 1 or 2");
    uint64_t seed;
    if (N == 0) {
      time_t t = time(nullptr);
      seed = t ^ (t << 32);
    } else if (N == 1) {
      seed = uintptr_t(&seed) ^ (uintptr_t(&seed) << 32);
    } else {
      seed = uintptr_t(&sRegion) ^ (uintptr_t(&sRegion) << 32);
    }
    return seed;
  }

 public:
  // Attempt a page allocation if the time and the size are right. Allocated
  // memory is zeroed if aZero is true. On failure, the caller should attempt a
  // normal allocation via MozJemalloc. Can be called in a context where
  // PHC::mMutex is locked.
  void* MaybePageAlloc(const Maybe<arena_id_t>& aArenaId, size_t aReqSize,
                       size_t aAlignment, bool aZero);

  void FreePage(uintptr_t aIndex, const Maybe<arena_id_t>& aArenaId,
                const StackTrace& aFreeStack, Delay aReuseDelay);

  // This handles both free and moz_arena_free.
  void PageFree(const Maybe<arena_id_t>& aArenaId, void* aPtr);

  Maybe<void*> PageRealloc(const Maybe<arena_id_t>& aArenaId, void* aOldPtr,
                           size_t aNewSize);

  void PagePtrInfo(const void* aPtr, jemalloc_ptr_info_t* aInfo);

  size_t PtrUsableSize(usable_ptr_t aPtr);

  bool IsPHCAllocation(const void* aPtr, mozilla::phc::AddrInfo* aOut);

  void Crash(const char* aMessage);

 private:
  // To improve locality we try to order this file by how frequently different
  // fields are modified and place all the modified-together fields early and
  // ideally within a single cache line.
  // The mutex that protects the other members.
  alignas(kCacheLineSize) Mutex mMutex MOZ_UNANNOTATED;

  // The current time. We use ReleaseAcquire semantics since we attempt to
  // update this by larger increments and don't want to lose an entire update.
  Atomic<Time, ReleaseAcquire> mNow;

  // This will only ever be updated from one thread.  The other threads should
  // eventually get the update.
  Atomic<PHCState, Relaxed> mPhcState =
      Atomic<PHCState, Relaxed>(DEFAULT_STATE);

  // RNG for deciding which allocations to treat specially. It doesn't need to
  // be high quality.
  //
  // This is a raw pointer for the reason explained in the comment above
  // PHC's constructor. Don't change it to UniquePtr or anything like that.
  non_crypto::XorShift128PlusRNG mRNG MOZ_GUARDED_BY(mMutex);

  // A linked list of free pages. Pages are allocated from the head of the list
  // and returned to the tail. The list will naturally order itself by "last
  // freed time" so if the head of the list can't satisfy an allocation due to
  // time then none of the pages can.
  Maybe<uintptr_t> mFreePageListHead MOZ_GUARDED_BY(mMutex);
  Maybe<uintptr_t> mFreePageListTail MOZ_GUARDED_BY(mMutex);

#if PHC_LOGGING
  // How many allocations that could have been page allocs actually were? As
  // constrained kNumAllocPages. If the hit ratio isn't close to 100% it's
  // likely that the global constants are poorly chosen.
  size_t mPageAllocHits MOZ_GUARDED_BY(mMutex) = 0;
  size_t mPageAllocMisses MOZ_GUARDED_BY(mMutex) = 0;
#endif

  // The remaining fields are updated much less often, place them on the next
  // cache line.

  // The average delay before doing any page allocations at the start of a
  // process. Note that roughly 1 million allocations occur in the main process
  // while starting the browser. The delay range is 1..gAvgFirstAllocDelay*2.
  alignas(kCacheLineSize) Delay mAvgFirstAllocDelay
      MOZ_GUARDED_BY(mMutex) = 64 * 1024;

  // The average delay until the next attempted page allocation, once we get
  // past the first delay. The delay range is 1..kAvgAllocDelay*2.
  Delay mAvgAllocDelay MOZ_GUARDED_BY(mMutex) = 16 * 1024;

  // The average delay before reusing a freed page. Should be significantly
  // larger than kAvgAllocDelay, otherwise there's not much point in having it.
  // The delay range is (kAvgAllocDelay / 2)..(kAvgAllocDelay / 2 * 3). This is
  // different to the other delay ranges in not having a minimum of 1, because
  // that's such a short delay that there is a high likelihood of bad stacks in
  // any crash report.
  Delay mAvgPageReuseDelay MOZ_GUARDED_BY(mMutex) = 256 * 1024;

  // When true, PHC does as little as possible.
  //
  // (a) It does not allocate any new page allocations.
  //
  // (b) It avoids doing any operations that might call malloc/free/etc., which
  //     would cause re-entry into PHC. (In practice, MozStackWalk() is the
  //     only such operation.) Note that calls to the functions in MozJemalloc
  //     are ok.
  //
  // For example, replace_malloc() will just fall back to mozjemalloc. However,
  // operations involving existing allocations are more complex, because those
  // existing allocations may be page allocations. For example, if
  // replace_free() is passed a page allocation on a PHC-disabled thread, it
  // will free the page allocation in the usual way, but it will get a dummy
  // freeStack in order to avoid calling MozStackWalk(), as per (b) above.
  //
  // This single disabling mechanism has two distinct uses.
  //
  // - It's used to prevent re-entry into PHC, which can cause correctness
  //   problems. For example, consider this sequence.
  //
  //   1. enter replace_free()
  //   2. which calls PageFree()
  //   3. which calls MozStackWalk()
  //   4. which locks a mutex M, and then calls malloc
  //   5. enter replace_malloc()
  //   6. which calls MaybePageAlloc()
  //   7. which calls MozStackWalk()
  //   8. which (re)locks a mutex M --> deadlock
  //
  //   We avoid this sequence by "disabling" the thread in PageFree() (at step
  //   2), which causes MaybePageAlloc() to fail, avoiding the call to
  //   MozStackWalk() (at step 7).
  //
  //   In practice, realloc or free of a PHC allocation is unlikely on a thread
  //   that is disabled because of this use: MozStackWalk() will probably only
  //   realloc/free allocations that it allocated itself, but those won't be
  //   page allocations because PHC is disabled before calling MozStackWalk().
  //
  //   (Note that MaybePageAlloc() could safely do a page allocation so long as
  //   it avoided calling MozStackWalk() by getting a dummy allocStack. But it
  //   wouldn't be useful, and it would prevent the second use below.)
  //
  // - It's used to prevent PHC allocations in some tests that rely on
  //   mozjemalloc's exact allocation behaviour, which PHC does not replicate
  //   exactly. (Note that (b) isn't necessary for this use -- MozStackWalk()
  //   could be safely called -- but it is necessary for the first use above.)
  //
  static PHC_THREAD_LOCAL(bool) tlsIsDisabled;

  // Delay until the next attempt at a page allocation.  The delay is made up of
  // two parts the global delay and each thread's local portion of that delay:
  //
  //  delay = sDelay + sum_all_threads(tlsAllocDelay)
  //
  // Threads use their local delay to reduce contention on the shared delay.
  //
  // See the comment in MaybePageAlloc() for an explanation of why it uses
  // ReleaseAcquire semantics.
  static Atomic<Delay, ReleaseAcquire> sAllocDelay;
  static PHC_THREAD_LOCAL(Delay) tlsAllocDelay;

  // The last value we set tlsAllocDelay to before starting to count down.
  static PHC_THREAD_LOCAL(Delay) tlsLastDelay;

  // Using mfbt/Array.h makes MOZ_GUARDED_BY more reliable than a C array.
  BaseArray<AllocPageInfo> mAllocPages MOZ_GUARDED_BY(mMutex);

 public:
  // There are two kinds of page.
  // - Allocation pages, from which allocations are made.
  // - Guard pages, which are never touched by PHC.
  //
  size_t NumAllocPages() const MOZ_REQUIRES(mMutex) {
    return mAllocPages.Capacity();
  }

  // These page kinds are interleaved; each allocation page has a guard page on
  // either side.
  size_t NumAllPages() const MOZ_REQUIRES(mMutex) {
    return NumAllocPages() * 2 + 1;
  }

  Delay GetAvgAllocDelay() MOZ_REQUIRES(mMutex) { return mAvgAllocDelay; }
  Delay GetAvgFirstAllocDelay() MOZ_REQUIRES(mMutex) {
    return mAvgFirstAllocDelay;
  }
  Delay GetAvgPageReuseDelay() MOZ_REQUIRES(mMutex) {
    return mAvgPageReuseDelay;
  }
  Delay ReuseDelay() MOZ_REQUIRES(mMutex) {
    Delay avg_reuse_delay = GetAvgPageReuseDelay();
    return (avg_reuse_delay / 2) +
           Rnd64ToDelay(avg_reuse_delay / 2, Random64());
  }

  // Both of these are accessed early on hot code paths.  We make them both
  // static variables rathan making sRegion a member of sPHC to keep these hot
  // code paths as fast as possible.  They're both "write once" so they can
  // share a cache line.
  static PHCRegion sRegion;
  static PHC* sPHC;
};

// Maps a pointer to a PHC-specific structure:
// - A guard page (it is unspecified which one)
// - An allocation page (with an index < kNumAllocPages)
//
// PtrKind should only be used on pointers that are within PHC's virtual address
// range.  Callers should usually check sRegion.WithinBounds() first, if
// successful then PHC::GetPtrKind() can be used safely.
//
// The standard way of handling a PtrKind is to check sRegion.WithinBounds()
// first, and if that succeeds, to call GetPtrKind and check IsGuardPage(), and
// if that fails, then this is a PHC pointer.
class PtrKind {
 private:
  enum class Tag : uint8_t {
    GuardPage,
    AllocPage,
  };

  Tag mTag = Tag::GuardPage;
  uintptr_t mIndex = 0;  // Only used if mTag == Tag::AllocPage.

 protected:
  // Detect what a pointer points to. This constructor must be fast because it
  // is called for every call to free(), realloc(), malloc_usable_size(), and
  // jemalloc_ptr_info().
  PtrKind(const void* aPtr, const uint8_t* aPagesStart) {
    uintptr_t offset = static_cast<const uint8_t*>(aPtr) - aPagesStart;
    uintptr_t allPageIndex = offset / kPhcPageSize;

    if (allPageIndex & 1) {
      // Odd-indexed pages are allocation pages.
      uintptr_t allocPageIndex = allPageIndex / 2;
      mTag = Tag::AllocPage;
      mIndex = allocPageIndex;
    } else {
      // Even-numbered pages are guard pages.
      mTag = Tag::GuardPage;
    }
  }
  friend PtrKind PHC::GetPtrKind(const void* aPtr);

 public:
  bool IsGuardPage() const { return mTag == Tag::GuardPage; }

  // This should only be called after IsGuardPage() has returned false.
  Maybe<uintptr_t> AllocPageIndex(uintptr_t aNumPages) const {
    MOZ_RELEASE_ASSERT(mTag == Tag::AllocPage);

    if (mIndex < aNumPages) {
      return Some(mIndex);
    } else {
      return Nothing();
    }
  }
};

PtrKind PHC::GetPtrKind(const void* aPtr) {
  MOZ_ASSERT(sRegion.WithinBounds(aPtr));
  return PtrKind(aPtr, sRegion.PagesStart());
}

// These globals are read together and hardly ever written.  They should be on
// the same cache line.  They should be in a different cache line to data that
// is manipulated often (sMutex and mNow are members of sPHC for that reason) so
// that this cache line can be shared amoung cores.
alignas(kCacheLineSize) PHCRegion PHC::sRegion;
PHC* PHC::sPHC;

PHC_THREAD_LOCAL(bool) PHC::tlsIsDisabled;
PHC_THREAD_LOCAL(Delay) PHC::tlsAllocDelay;
Atomic<Delay, ReleaseAcquire> PHC::sAllocDelay;
PHC_THREAD_LOCAL(Delay) PHC::tlsLastDelay;

// When PHC wants to crash we first have to unlock so that the crash reporter
// can call into PHC to lockup its pointer. That also means that before calling
// PHCCrash please ensure that state is consistent.  Because this can report an
// arbitrary string, use of it must be reviewed by Firefox data stewards.
void PHC::Crash(const char* aMessage) MOZ_REQUIRES(mMutex) {
  mMutex.Unlock();
  MOZ_CRASH_UNSAFE(aMessage);
}

class AutoDisableOnCurrentThread {
 public:
  AutoDisableOnCurrentThread(const AutoDisableOnCurrentThread&) = delete;

  const AutoDisableOnCurrentThread& operator=(
      const AutoDisableOnCurrentThread&) = delete;

  explicit AutoDisableOnCurrentThread() { PHC::DisableOnCurrentThread(); }
  ~AutoDisableOnCurrentThread() { PHC::sPHC->EnableOnCurrentThread(); }
};

//---------------------------------------------------------------------------
// Initialisation
//---------------------------------------------------------------------------

// WARNING: this function runs *very* early -- before all static initializers
// have run. For this reason, non-scalar globals (sPHC) are allocated
// dynamically (so we can guarantee their construction in this function) rather
// than statically.  sRegion is allocated statically to avoid an extra
// dereference.
//
// If initialisation fails sPHC will be null.  Returning bool won't help the
// caller as there's nothing they can do.
void phc_init() {
  // We must only initialise once.
  MOZ_ASSERT(!PHC::sPHC);

  if (GetKernelPageSize() != kPhcPageSize) {
    return;
  }

  if (!PHC::sRegion.AllocVirtualAddresses()) {
    return;
  }

  // sPHC is never freed. It lives for the life of the process.
  PHC::sPHC = new (fallible) PHC();
  if (!PHC::sPHC) {
    PHC::sRegion.ReleaseVirtualAddresses();
    return;
  }
  if (!PHC::sPHC->Init()) {
    delete PHC::sPHC;
    PHC::sPHC = nullptr;
    PHC::sRegion.ReleaseVirtualAddresses();
    return;
  }

#ifndef XP_WIN
  // Avoid deadlocks when forking by acquiring our state lock prior to forking
  // and releasing it after forking. See |LogAlloc|'s |phc_init| for
  // in-depth details.
  pthread_atfork(PHC::prefork, PHC::postfork_parent, PHC::postfork_child);
#endif
}

//---------------------------------------------------------------------------
// Page allocation operations
//---------------------------------------------------------------------------

// This is the hot-path for testing if we should make a PHC allocation, it
// should be inlined into the caller while the remainder of the tests that are
// in MaybePageAlloc need not be inlined.
static MOZ_ALWAYS_INLINE bool ShouldPageAllocHot(size_t aReqSize) {
  if (MOZ_UNLIKELY(!PHC::sPHC)) {
    return false;
  }

  if (MOZ_UNLIKELY(aReqSize > kPhcPageSize)) {
    return false;
  }

  // Decrement the delay. If it's zero, we do a page allocation and reset the
  // delay to a random number.
  if (MOZ_LIKELY(!PHC::DecrementDelay())) {
    return false;
  }

  return true;
}

void PHC::LogNoAlloc(size_t aReqSize, size_t aAlignment, Delay newAllocDelay)
    MOZ_REQUIRES(mMutex) {
  // No pages are available, or VirtualAlloc/mprotect failed.
#if PHC_LOGGING
  phc::PHCStats stats = GetPageStatsLocked();
  Log("No PageAlloc(%zu, %zu), sAllocDelay <- %zu, fullness %zu/%zu/%zu, "
      "hits %zu/%zu (%zu%%)\n",
      aReqSize, aAlignment, size_t(newAllocDelay), stats.mSlotsAllocated,
      stats.mSlotsFreed, NumAllocPages(), PageAllocHits(), PageAllocAttempts(),
      PageAllocHitRate());
#endif
}

void* PHC::MaybePageAlloc(const Maybe<arena_id_t>& aArenaId, size_t aReqSize,
                          size_t aAlignment, bool aZero) {
  MOZ_ASSERT(std::has_single_bit(aAlignment));
  if (!ShouldMakeNewAllocations()) {
    // Reset the allocation delay so that we take the fast path most of the
    // time.  Rather than take the lock and use the RNG which are unnecessary
    // when PHC is disabled, instead set the delay to a reasonably high number,
    // the default average first allocation delay.  This is reset when PHC is
    // re-enabled anyway.
    ForceSetNewAllocDelay(kDelayResetWhenDisabled);
    return nullptr;
  }

  if (IsDisabledOnCurrentThread()) {
    // We don't reset sAllocDelay since that might affect other threads.  We
    // assume this is okay because either this thread will be re-enabled after
    // less than DELAY_MAX allocations or that there are other active threads
    // that will reset sAllocDelay.  We do reset our local delay which will
    // cause this thread to "back off" from updating sAllocDelay on future
    // allocations.
    ResetLocalAllocDelay(kDelayBackoffAmount);
    return nullptr;
  }

  // Disable on this thread *before* getting the stack trace.
  AutoDisableOnCurrentThread disable;

  // Get the stack trace *before* locking the mutex. If we return nullptr then
  // it was a waste, but it's not so frequent, and doing a stack walk while
  // the mutex is locked is problematic (see the big comment on
  // StackTrace::Fill() for details).
  StackTrace allocStack;
  allocStack.Fill();

  MutexAutoLock lock(mMutex);

  Time now = Now();

  Delay newAllocDelay = Rnd64ToDelay(GetAvgAllocDelay(), Random64());
  if (!SetNewAllocDelay(newAllocDelay)) {
    return nullptr;
  }

  // Pages are allocated from a free list populated in order of when they're
  // freed.  If the page at the head of the list is too recently freed to be
  // reused then no other pages on the list will be either.

  Maybe<uintptr_t> mb_index = PopNextFreeIfAllocatable(now);
  if (!mb_index) {
    IncPageAllocMisses();
    LogNoAlloc(aReqSize, aAlignment, newAllocDelay);
    return nullptr;
  }
  uintptr_t index = mb_index.value();

#if PHC_LOGGING
  Time lifetime = 0;
#endif
  uint8_t* pagePtr = sRegion.AllocPagePtr(index);
  MOZ_ASSERT(pagePtr);
  bool ok =
#ifdef XP_WIN
      !!VirtualAlloc(pagePtr, kPhcPageSize, MEM_COMMIT, PAGE_READWRITE);
#else
      mprotect(pagePtr, kPhcPageSize, PROT_READ | PROT_WRITE) == 0;
#endif

  if (!ok) {
    UnpopNextFree(index);
    IncPageAllocMisses();
    LogNoAlloc(aReqSize, aAlignment, newAllocDelay);
    return nullptr;
  }

  size_t usableSize = MozJemalloc::malloc_good_size(aReqSize);
  MOZ_ASSERT(usableSize > 0);

  // Put the allocation as close to the end of the page as possible,
  // allowing for alignment requirements.
  uint8_t* ptr = pagePtr + kPhcPageSize - usableSize;
  if (aAlignment != 1) {
    ptr = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(ptr) & ~(aAlignment - 1)));
  }

#if PHC_LOGGING
  Time then = GetFreeTime(index);
  lifetime = then != 0 ? now - then : 0;
#endif

  SetPageInUse(index, aArenaId, ptr, allocStack);

  if (aZero) {
    memset(ptr, 0, usableSize);
  } else {
#ifdef DEBUG
    memset(ptr, kAllocJunk, usableSize);
#endif
  }

  IncPageAllocHits();
#if PHC_LOGGING
  phc::PHCStats stats = GetPageStatsLocked();
  Log("PageAlloc(%zu, %zu) -> %p[%zu]/%p (%zu) (z%zu), sAllocDelay <- %zu, "
      "fullness %zu/%zu/%zu, hits %zu/%zu (%zu%%), lifetime %zu\n",
      aReqSize, aAlignment, pagePtr, index, ptr, usableSize,
      size_t(newAllocDelay), size_t(SharedAllocDelay()), stats.mSlotsAllocated,
      stats.mSlotsFreed, NumAllocPages(), PageAllocHits(), PageAllocAttempts(),
      PageAllocHitRate(), lifetime);
#endif

  return ptr;
}

void PHC::FreePage(uintptr_t aIndex, const Maybe<arena_id_t>& aArenaId,
                   const StackTrace& aFreeStack, Delay aReuseDelay)
    MOZ_REQUIRES(mMutex) {
  void* pagePtr = sRegion.AllocPagePtr(aIndex);

#ifdef XP_WIN
  if (!VirtualFree(pagePtr, kPhcPageSize, MEM_DECOMMIT)) {
    Crash("VirtualFree failed");
  }
#else
  if (mmap(pagePtr, kPhcPageSize, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON,
           -1, 0) == MAP_FAILED) {
    Crash("mmap failed");
  }
#endif

  SetPageFreed(aIndex, aArenaId, aFreeStack, aReuseDelay);
}

//---------------------------------------------------------------------------
// replace-malloc machinery
//---------------------------------------------------------------------------

// This handles malloc, moz_arena_malloc, and realloc-with-a-nullptr.
MOZ_ALWAYS_INLINE static void* PageMalloc(const Maybe<arena_id_t>& aArenaId,
                                          size_t aReqSize) {
  void* ptr =
      ShouldPageAllocHot(aReqSize)
          // The test on aArenaId here helps the compiler optimise away
          // the construction of Nothing() in the caller.
          ? PHC::sPHC->MaybePageAlloc(aArenaId.isSome() ? aArenaId : Nothing(),
                                      aReqSize, /* aAlignment */ 1,
                                      /* aZero */ false)
          : nullptr;
  return ptr ? ptr
             : (aArenaId.isSome()
                    ? MozJemalloc::moz_arena_malloc(*aArenaId, aReqSize)
                    : MozJemalloc::malloc(aReqSize));
}

inline void* MozJemallocPHC::malloc(size_t aReqSize) {
  return PageMalloc(Nothing(), aReqSize);
}

// This handles both calloc and moz_arena_calloc.
MOZ_ALWAYS_INLINE static void* PageCalloc(const Maybe<arena_id_t>& aArenaId,
                                          size_t aNum, size_t aReqSize) {
  CheckedInt<size_t> checkedSize = CheckedInt<size_t>(aNum) * aReqSize;
  if (!checkedSize.isValid()) {
    return nullptr;
  }

  void* ptr =
      ShouldPageAllocHot(checkedSize.value())
          // The test on aArenaId here helps the compiler optimise away
          // the construction of Nothing() in the caller.
          ? PHC::sPHC->MaybePageAlloc(aArenaId.isSome() ? aArenaId : Nothing(),
                                      checkedSize.value(), /* aAlignment */ 1,
                                      /* aZero */ true)
          : nullptr;
  return ptr ? ptr
             : (aArenaId.isSome()
                    ? MozJemalloc::moz_arena_calloc(*aArenaId, aNum, aReqSize)
                    : MozJemalloc::calloc(aNum, aReqSize));
}

inline void* MozJemallocPHC::calloc(size_t aNum, size_t aReqSize) {
  return PageCalloc(Nothing(), aNum, aReqSize);
}

MOZ_ALWAYS_INLINE static bool FastIsPHCPtr(const void* aPtr) {
  if (MOZ_UNLIKELY(!PHC::sPHC)) {
    return false;
  }

  return PHC::sRegion.WithinBounds(aPtr);
}

// This function handles both realloc and moz_arena_realloc.
//
// As always, realloc is complicated, and doubly so when there are two
// different kinds of allocations in play. Here are the possible transitions,
// and what we do in practice.
//
// - normal-to-normal: This is straightforward and obviously necessary.
//
// - normal-to-page: This is disallowed because it would require getting the
//   arenaId of the normal allocation, which isn't possible in non-DEBUG builds
//   for security reasons.
//
// - page-to-page: This is done whenever possible, i.e. whenever the new size
//   is less than or equal to 4 KiB. This choice counterbalances the
//   disallowing of normal-to-page allocations, in order to avoid biasing
//   towards or away from page allocations. It always occurs in-place.
//
// - page-to-normal: this is done only when necessary, i.e. only when the new
//   size is greater than 4 KiB. This choice naturally flows from the
//   prior choice on page-to-page transitions.
//
// In summary: realloc doesn't change the allocation kind unless it must.
//
// This function may return:
// - Some(pointer) when PHC handled the reallocation.
// - Some(nullptr) when PHC should have handled a page-to-normal transition
//   but couldn't because of OOM.
// - Nothing() when PHC is disabled or the original allocation was not
//   under PHC.
MOZ_ALWAYS_INLINE static Maybe<void*> MaybePageRealloc(
    const Maybe<arena_id_t>& aArenaId, void* aOldPtr, size_t aNewSize) {
  if (!aOldPtr) {
    // Null pointer. Treat like malloc(aNewSize).
    return Some(PageMalloc(aArenaId, aNewSize));
  }

  if (MOZ_UNLIKELY(!FastIsPHCPtr(aOldPtr))) {
    // A normal-to-normal transition.
    return Nothing();
  }

  return PHC::sPHC->PageRealloc(aArenaId, aOldPtr, aNewSize);
}

Maybe<void*> PHC::PageRealloc(const Maybe<arena_id_t>& aArenaId, void* aOldPtr,
                              size_t aNewSize) MOZ_EXCLUDES(mMutex) {
  PtrKind pk = GetPtrKind(aOldPtr);

  if (pk.IsGuardPage()) {
    CrashOnGuardPage(aOldPtr);
  }

  // A page-to-something transition.
  AdvanceNow(LocalAllocDelay());

  // Note that `disable` has no effect unless it is emplaced below.
  Maybe<AutoDisableOnCurrentThread> disable;
  // Get the stack trace *before* locking the mutex.
  StackTrace stack;
  if (IsDisabledOnCurrentThread()) {
    // PHC is disabled on this thread. Leave the stack empty.
  } else {
    // Disable on this thread *before* getting the stack trace.
    disable.emplace();
    stack.Fill();
  }

  MutexAutoLock lock(mMutex);

  Maybe<uintptr_t> mb_index = pk.AllocPageIndex(NumAllocPages());
  if (!mb_index) {
    Crash("Realloc of invalid pointer");
  }
  // At this point we know we have an allocation page.
  uintptr_t index = mb_index.value();

  // Check for realloc() of a freed block.
  EnsureValidAndInUse(aOldPtr, index);

  if (aNewSize <= kPhcPageSize && ShouldMakeNewAllocations()) {
    // A page-to-page transition. Just keep using the page allocation. We do
    // this even if the thread is disabled, because it doesn't create a new
    // page allocation. Note that ResizePageInUse() checks aArenaId.
    //
    // Move the bytes with memmove(), because the old allocation and the new
    // allocation overlap. Move the usable size rather than the requested size,
    // because the user might have used malloc_usable_size() and filled up the
    // usable size.
    size_t oldUsableSize = PageUsableSize(index);
    size_t newUsableSize = MozJemalloc::malloc_good_size(aNewSize);
    uint8_t* pagePtr = sRegion.AllocPagePtr(index);
    uint8_t* newPtr = pagePtr + kPhcPageSize - newUsableSize;
    memmove(newPtr, aOldPtr, std::min(oldUsableSize, aNewSize));
    ResizePageInUse(index, aArenaId, newPtr, stack);
    Log("PageRealloc-Reuse(%p, %zu) -> %p\n", aOldPtr, aNewSize, newPtr);
    return Some(newPtr);
  }

  // A page-to-normal transition (with the new size greater than page-sized).
  // (Note that aArenaId is checked below.)
  void* newPtr;
  if (aArenaId.isSome()) {
    newPtr = MozJemalloc::moz_arena_malloc(*aArenaId, aNewSize);
  } else {
    Maybe<arena_id_t> oldArenaId = PageArena(index);
    newPtr = (oldArenaId.isSome()
                  ? MozJemalloc::moz_arena_malloc(*oldArenaId, aNewSize)
                  : MozJemalloc::malloc(aNewSize));
  }
  if (!newPtr) {
    return Some(nullptr);
  }

  Delay reuseDelay = ReuseDelay();

  // Copy the usable size rather than the requested size, because the user
  // might have used malloc_usable_size() and filled up the usable size. Note
  // that FreePage() checks aArenaId (via SetPageFreed()).
  size_t oldUsableSize = PageUsableSize(index);
  memcpy(newPtr, aOldPtr, std::min(oldUsableSize, aNewSize));
  FreePage(index, aArenaId, stack, reuseDelay);
  Log("PageRealloc-Free(%p[%zu], %zu) -> %p, %zu delay, reuse at ~%zu\n",
      aOldPtr, index, aNewSize, newPtr, size_t(reuseDelay),
      size_t(Now()) + reuseDelay);

  return Some(newPtr);
}

MOZ_ALWAYS_INLINE static void* PageRealloc(const Maybe<arena_id_t>& aArenaId,
                                           void* aOldPtr, size_t aNewSize) {
  Maybe<void*> ptr = MaybePageRealloc(aArenaId, aOldPtr, aNewSize);

  return ptr.isSome()
             ? *ptr
             : (aArenaId.isSome() ? MozJemalloc::moz_arena_realloc(
                                        *aArenaId, aOldPtr, aNewSize)
                                  : MozJemalloc::realloc(aOldPtr, aNewSize));
}

inline void* MozJemallocPHC::realloc(void* aOldPtr, size_t aNewSize) {
  return PageRealloc(Nothing(), aOldPtr, aNewSize);
}

void PHC::PageFree(const Maybe<arena_id_t>& aArenaId, void* aPtr)
    MOZ_EXCLUDES(mMutex) {
  PtrKind pk = GetPtrKind(aPtr);

  if (pk.IsGuardPage()) {
    PHC::CrashOnGuardPage(aPtr);
  }

  AdvanceNow(LocalAllocDelay());

  // Note that `disable` has no effect unless it is emplaced below.
  Maybe<AutoDisableOnCurrentThread> disable;
  // Get the stack trace *before* locking the mutex.
  StackTrace freeStack;
  if (IsDisabledOnCurrentThread()) {
    // PHC is disabled on this thread. Leave the stack empty.
  } else {
    // Disable on this thread *before* getting the stack trace.
    disable.emplace();
    freeStack.Fill();
  }

  MutexAutoLock lock(mMutex);

  Maybe<uintptr_t> mb_index = pk.AllocPageIndex(NumAllocPages());
  if (!mb_index) {
    Crash("free of invalid pointer");
  }
  // At this point we know we have an allocation page.
  uintptr_t index = mb_index.value();

  // Check for a double-free.
  EnsureValidAndInUse(aPtr, index);

  // Note that FreePage() checks aArenaId (via SetPageFreed()).
  Delay reuseDelay = ReuseDelay();
  FreePage(index, aArenaId, freeStack, reuseDelay);

#if PHC_LOGGING
  phc::PHCStats stats = GetPageStatsLocked();
  Log("PageFree(%p[%zu]), %zu delay, reuse at ~%zu, fullness %zu/%zu/%zu\n",
      aPtr, index, size_t(reuseDelay), size_t(Now()) + reuseDelay,
      stats.mSlotsAllocated, stats.mSlotsFreed, NumAllocPages());
#endif
}

MOZ_ALWAYS_INLINE static void PageFree(const Maybe<arena_id_t>& aArenaId,
                                       void* aPtr) {
  if (MOZ_UNLIKELY(FastIsPHCPtr(aPtr))) {
    // The tenery expression here helps the compiler optimise away the
    // construction of Nothing() in the caller.
    PHC::sPHC->PageFree(aArenaId.isSome() ? aArenaId : Nothing(), aPtr);
    return;
  }

  aArenaId.isSome() ? MozJemalloc::moz_arena_free(*aArenaId, aPtr)
                    : MozJemalloc::free(aPtr);
}

inline void MozJemallocPHC::free(void* aPtr) { PageFree(Nothing(), aPtr); }

// This handles memalign and moz_arena_memalign.
MOZ_ALWAYS_INLINE static void* PageMemalign(const Maybe<arena_id_t>& aArenaId,
                                            size_t aAlignment,
                                            size_t aReqSize) {
  MOZ_RELEASE_ASSERT(std::has_single_bit(aAlignment));

  // PHC can't satisfy an alignment greater than a page size, so fall back to
  // mozjemalloc in that case.
  void* ptr = nullptr;
  if (ShouldPageAllocHot(aReqSize) && aAlignment <= kPhcPageSize) {
    // The test on aArenaId here helps the compiler optimise away
    // the construction of Nothing() in the caller.
    ptr = PHC::sPHC->MaybePageAlloc(aArenaId.isSome() ? aArenaId : Nothing(),
                                    aReqSize, aAlignment, /* aZero */ false);
  }
  return ptr ? ptr
             : (aArenaId.isSome()
                    ? MozJemalloc::moz_arena_memalign(*aArenaId, aAlignment,
                                                      aReqSize)
                    : MozJemalloc::memalign(aAlignment, aReqSize));
}

inline void* MozJemallocPHC::memalign(size_t aAlignment, size_t aReqSize) {
  return PageMemalign(Nothing(), aAlignment, aReqSize);
}

inline size_t MozJemallocPHC::malloc_usable_size(usable_ptr_t aPtr) {
  if (MOZ_LIKELY(!FastIsPHCPtr(aPtr))) {
    // Not a page allocation. Measure it normally.
    return MozJemalloc::malloc_usable_size(aPtr);
  }

  return PHC::sPHC->PtrUsableSize(aPtr);
}

size_t PHC::PtrUsableSize(usable_ptr_t aPtr) MOZ_EXCLUDES(mMutex) {
  PtrKind pk = GetPtrKind(aPtr);

  if (pk.IsGuardPage()) {
    CrashOnGuardPage(const_cast<void*>(aPtr));
  }

  MutexAutoLock lock(mMutex);

  Maybe<uintptr_t> index = pk.AllocPageIndex(NumAllocPages());
  if (!index) {
    Crash("PtrUsableSize() of invalid pointer");
  }

  // At this point we know aPtr lands within an allocation page.  But if aPtr
  // points to memory before the base address of the allocation, we return 0.
  void* pageBaseAddr = AllocPageBaseAddr(index.value());

  if (MOZ_UNLIKELY(aPtr < pageBaseAddr)) {
    return 0;
  }

  return PageUsableSize(index.value());
}

inline void MozJemallocPHC::jemalloc_stats_internal(
    jemalloc_stats_t* aStats, jemalloc_bin_stats_t* aBinStats) {
  MozJemalloc::jemalloc_stats_internal(aStats, aBinStats);

  if (!PHC::sPHC) {
    // If we're not initialised, then we're not using any additional memory and
    // have nothing to add to the report.
    return;
  }

  // Add PHC's memory usage to the allocator's.
  phc::MemoryUsage mem_info;
  PHC::sPHC->GetMemoryUsage(mem_info);
  aStats->allocated += mem_info.mAllocatedBytes;
  aStats->waste += mem_info.mFragmentationBytes;
  aStats->mapped += PHC::sRegion.ReservedBytes() - mem_info.mAllocatedBytes -
                    mem_info.mFragmentationBytes;

  // guards is the gap between `allocated` and `mapped`. In some ways this
  // almost fits into aStats->wasted since it feels like wasted memory. However
  // wasted should only include committed memory and these guard pages are
  // uncommitted. Therefore we don't include it anywhere.
  // size_t guards = mapped - allocated;

  // aStats.page_cache and aStats.bin_unused are left unchanged because PHC
  // doesn't have anything corresponding to those.

  // The metadata is `bookkeeping`.
  aStats->bookkeeping += mem_info.mMetadataBytes;
}

inline void MozJemallocPHC::jemalloc_stats_lite(jemalloc_stats_lite_t* aStats) {
  MozJemalloc::jemalloc_stats_lite(aStats);
}

inline void MozJemallocPHC::jemalloc_ptr_info(const void* aPtr,
                                              jemalloc_ptr_info_t* aInfo) {
  if (MOZ_LIKELY(!FastIsPHCPtr(aPtr))) {
    // Not a page allocation.
    MozJemalloc::jemalloc_ptr_info(aPtr, aInfo);
    return;
  }

  PHC::sPHC->PagePtrInfo(aPtr, aInfo);
}

void PHC::PagePtrInfo(const void* aPtr, jemalloc_ptr_info_t* aInfo)
    MOZ_EXCLUDES(mMutex) {
  // We need to implement this properly, because various code locations do
  // things like checking that allocations are in the expected arena.

  PtrKind pk = GetPtrKind(aPtr);

  if (pk.IsGuardPage()) {
    // Treat a guard page as unknown because there's no better alternative.
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  MutexAutoLock lock(mMutex);

  // At this point we know we have an allocation page.
  Maybe<uintptr_t> index = pk.AllocPageIndex(NumAllocPages());

  if (!index) {
    Crash("JemallocPtrInfo of invalid pointer");
  }

  FillJemallocPtrInfo(aPtr, index.value(), aInfo);
#if DEBUG
  Log("JemallocPtrInfo(%p[%zu]) -> {%zu, %p, %zu, %zu}\n", aPtr, index.value(),
      size_t(aInfo->tag), aInfo->addr, aInfo->size, aInfo->arenaId);
#else
  Log("JemallocPtrInfo(%p[%zu]) -> {%zu, %p, %zu}\n", aPtr, index.value(),
      size_t(aInfo->tag), aInfo->addr, aInfo->size);
#endif
}

inline void* MozJemallocPHC::moz_arena_malloc(arena_id_t aArenaId,
                                              size_t aReqSize) {
  return PageMalloc(Some(aArenaId), aReqSize);
}

inline void* MozJemallocPHC::moz_arena_calloc(arena_id_t aArenaId, size_t aNum,
                                              size_t aReqSize) {
  return PageCalloc(Some(aArenaId), aNum, aReqSize);
}

inline void* MozJemallocPHC::moz_arena_realloc(arena_id_t aArenaId,
                                               void* aOldPtr, size_t aNewSize) {
  return PageRealloc(Some(aArenaId), aOldPtr, aNewSize);
}

inline void MozJemallocPHC::moz_arena_free(arena_id_t aArenaId, void* aPtr) {
  return PageFree(Some(aArenaId), aPtr);
}

inline void* MozJemallocPHC::moz_arena_memalign(arena_id_t aArenaId,
                                                size_t aAlignment,
                                                size_t aReqSize) {
  return PageMemalign(Some(aArenaId), aAlignment, aReqSize);
}

bool PHC::IsPHCAllocation(const void* aPtr, mozilla::phc::AddrInfo* aOut) {
  PtrKind pk = GetPtrKind(aPtr);

  bool isGuardPage = false;
  if (pk.IsGuardPage()) {
    if ((uintptr_t(aPtr) % kPhcPageSize) < (kPhcPageSize / 2)) {
      // The address is in the lower half of a guard page, so it's probably an
      // overflow. But first check that it is not on the very first guard
      // page, in which case it cannot be an overflow, and we ignore it.
      if (sRegion.IsInFirstGuardPage(aPtr)) {
        return false;
      }

      // Get the allocation page preceding this guard page.
      pk = GetPtrKind(static_cast<const uint8_t*>(aPtr) - kPhcPageSize);

    } else {
      // The address is in the upper half of a guard page, so it's probably an
      // underflow. Get the allocation page following this guard page.
      pk = GetPtrKind(static_cast<const uint8_t*>(aPtr) + kPhcPageSize);
    }

    // Make a note of the fact that we hit a guard page.
    isGuardPage = true;
  }

  if (aOut) {
    if (mMutex.TryLock()) {
      // At this point we know we have an allocation page.
      Maybe<uintptr_t> index = pk.AllocPageIndex(NumAllocPages());
      if (!index) {
        mMutex.Unlock();
        return false;
      }
      FillAddrInfo(index.value(), aPtr, isGuardPage, *aOut);
      Log("IsPHCAllocation: %zu, %p, %zu, %zu, %zu\n", size_t(aOut->mKind),
          aOut->mBaseAddr, aOut->mUsableSize,
          aOut->mAllocStack.isSome() ? aOut->mAllocStack->mLength : 0,
          aOut->mFreeStack.isSome() ? aOut->mFreeStack->mLength : 0);
      mMutex.Unlock();
    } else {
      Log("IsPHCAllocation: PHC is locked\n");
      aOut->mPhcWasLocked = true;
    }
  }
  return true;
}

namespace mozilla::phc {

bool IsPHCAllocation(const void* aPtr, AddrInfo* aOut) {
  if (MOZ_LIKELY(!FastIsPHCPtr(aPtr))) {
    return false;
  }

  return PHC::sPHC->IsPHCAllocation(aPtr, aOut);
}

void DisablePHCOnCurrentThread() {
  PHC::DisableOnCurrentThread();
  Log("DisablePHCOnCurrentThread: %zu\n", 0ul);
}

void ReenablePHCOnCurrentThread() {
  PHC::sPHC->EnableOnCurrentThread();
  Log("ReenablePHCOnCurrentThread: %zu\n", 0ul);
}

bool IsPHCEnabledOnCurrentThread() {
  bool enabled = !PHC::IsDisabledOnCurrentThread();
  Log("IsPHCEnabledOnCurrentThread: %zu\n", size_t(enabled));
  return enabled;
}

void PHCMemoryUsage(MemoryUsage& aMemoryUsage) {
  aMemoryUsage = MemoryUsage();
  if (PHC::sPHC) {
    PHC::sPHC->GetMemoryUsage(aMemoryUsage);
  }
}

void SetPHCSize(size_t aSizeBytes) {
  if (PHC::sPHC) {
    PHC::sPHC->Resize(aSizeBytes);
  }
}

void GetPHCStats(PHCStats& aStats) {
  if (!PHC::sPHC) {
    aStats = PHCStats();
    return;
  }

  aStats = PHC::sPHC->GetPageStats();
}

// Enable or Disable PHC at runtime.  If PHC is disabled it will still trap
// bad uses of previous allocations, but won't track any new allocations.
void SetPHCState(PHCState aState) {
  if (!PHC::sPHC) {
    return;
  }

  PHC::sPHC->SetState(aState);
}

void SetPHCProbabilities(int64_t aAvgDelayFirst, int64_t aAvgDelayNormal,
                         int64_t aAvgDelayPageReuse) {
  if (!PHC::sPHC) {
    return;
  }

  PHC::sPHC->SetProbabilities(aAvgDelayFirst, aAvgDelayNormal,
                              aAvgDelayPageReuse);
}

}  // namespace mozilla::phc

#if PHC_LOGGING
static size_t GetPid() { return size_t(getpid()); }

static size_t GetTid() {
#  if defined(XP_WIN)
  return size_t(GetCurrentThreadId());
#  else
  return size_t(pthread_self());
#  endif
}
#endif  // PHC_LOGGING

static void Log(const char* fmt, ...) {
#if PHC_LOGGING
#  if defined(XP_WIN)
#    define LOG_STDERR \
      reinterpret_cast<intptr_t>(GetStdHandle(STD_ERROR_HANDLE))
#  else
#    define LOG_STDERR 2
#  endif

  char buf[256];
  size_t pos = SNPrintf(buf, sizeof(buf), "PHC[%zu,%zu,~%zu] ", GetPid(),
                        GetTid(), size_t(PHC::Now()));
  va_list vargs;
  va_start(vargs, fmt);
  pos += VSNPrintf(&buf[pos], sizeof(buf) - pos, fmt, vargs);
  MOZ_ASSERT(pos < sizeof(buf));
  va_end(vargs);

  FdPuts(LOG_STDERR, buf, pos);
#endif  // PHC_LOGGING
}
