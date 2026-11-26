/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <cerrno>
#include <cstdio>

#ifdef XP_WIN
#  include <io.h>
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif
#ifdef XP_DARWIN
#  include <libkern/OSAtomic.h>
#  include <mach/mach_init.h>
#  include <mach/vm_map.h>
#endif

#include "mozjemalloc_types.h"

#if defined(XP_WIN)
#  include "mozmemory_stall.h"
#endif
#include "Mutex.h"
#include "Chunk.h"
#include "Extent.h"
#include "Globals.h"
#include "RedBlackTree.h"

#include "mozilla/Assertions.h"
#include "mozilla/HelperMacros.h"
// Note: MozTaggedAnonymousMmap() could call an LD_PRELOADed mmap
// instead of the one defined here; use only MozTagAnonymousMemory().
#include "mozilla/TaggedAnonymousMemory.h"

// For GetGeckoProcessType(), when it's used.
#if defined(XP_WIN) && !defined(JS_STANDALONE)
#  include "mozilla/ProcessType.h"
#endif

using namespace mozilla;

// On Windows, delay crashing on OOM.
#ifdef XP_WIN

// Implementation of VirtualAlloc wrapper (bug 1716727).
namespace MozAllocRetries {

// Maximum retry count on OOM.
constexpr size_t kMaxAttempts = 10;
// Minimum delay time between retries. (The actual delay time may be larger. See
// Microsoft's documentation for ::Sleep() for details.)
constexpr size_t kDelayMs = 50;

using StallSpecs = ::mozilla::StallSpecs;

static constexpr StallSpecs maxStall = {.maxAttempts = kMaxAttempts,
                                        .delayMs = kDelayMs};

static inline StallSpecs GetStallSpecs() {
#  if defined(JS_STANDALONE)
  // GetGeckoProcessType() isn't available in this configuration. (SpiderMonkey
  // on Windows mostly skips this in favor of directly calling ::VirtualAlloc(),
  // though, so it's probably not going to matter whether we stall here or not.)
  return maxStall;
#  else
  switch (GetGeckoProcessType()) {
    // For the main process, stall for the maximum permissible time period. (The
    // main process is the most important one to keep alive.)
    case GeckoProcessType::GeckoProcessType_Default:
      return maxStall;

    // For all other process types, stall for at most half as long.
    default:
      return {.maxAttempts = maxStall.maxAttempts / 2,
              .delayMs = maxStall.delayMs};
  }
#  endif
}

}  // namespace MozAllocRetries

namespace mozilla {

StallSpecs GetAllocatorStallSpecs() {
  return ::MozAllocRetries::GetStallSpecs();
}

// Drop-in wrapper around VirtualAlloc. When out of memory, may attempt to stall
// and retry rather than returning immediately, in hopes that the page file is
// about to be expanded by Windows.
//
// Ref:
// https://docs.microsoft.com/en-us/troubleshoot/windows-client/performance/slow-page-file-growth-memory-allocation-errors
// https://hacks.mozilla.org/2022/11/improving-firefox-stability-with-this-one-weird-trick/
void* MozVirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType,
                      uint32_t flProtect) {
  using namespace MozAllocRetries;

  DWORD const lastError = ::GetLastError();

  constexpr auto IsOOMError = [] {
    switch (::GetLastError()) {
      // This is the usual error result from VirtualAlloc for OOM.
      case ERROR_COMMITMENT_LIMIT:
      // Although rare, this has also been observed in low-memory situations.
      // (Presumably this means Windows can't allocate enough kernel-side space
      // for its own internal representation of the process's virtual address
      // space.)
      case ERROR_NOT_ENOUGH_MEMORY:
        return true;
    }
    return false;
  };

  {
    void* ptr = ::VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
    if (MOZ_LIKELY(ptr)) return ptr;

    // We can't do anything for errors other than OOM...
    if (!IsOOMError()) return nullptr;
    // ... or if this wasn't a request to commit memory in the first place.
    // (This function has no strategy for resolving MEM_RESERVE failures.)
    if (!(flAllocationType & MEM_COMMIT)) return nullptr;
  }

  // Retry as many times as desired (possibly zero).
  const StallSpecs stallSpecs = GetStallSpecs();

  const auto ret =
      stallSpecs.StallAndRetry(&::Sleep, [&]() -> std::optional<void*> {
        void* ptr =
            ::VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);

        if (ptr) {
          // The OOM status has been handled, and should not be reported to
          // telemetry.
          if (IsOOMError()) {
            ::SetLastError(lastError);
          }
          return ptr;
        }

        // Failure for some reason other than OOM.
        if (!IsOOMError()) {
          return nullptr;
        }

        return std::nullopt;
      });

  return ret.value_or(nullptr);
}

}  // namespace mozilla

#endif  // XP_WIN

// Begin chunk management functions.

// Some tools, such as /dev/dsp wrappers, LD_PRELOAD libraries that
// happen to override mmap() and call dlsym() from their overridden
// mmap(). The problem is that dlsym() calls malloc(), and this ends
// up in a dead lock in jemalloc.
// On these systems, we prefer to directly use the system call.
// We do that for Linux systems and kfreebsd with GNU userland.
// Note sanity checks are not done (alignment of offset, ...) because
// the uses of mmap are pretty limited, in jemalloc.
//
// On Alpha, glibc has a bug that prevents syscall() to work for system
// calls with 6 arguments.
#if (defined(XP_LINUX) && !defined(__alpha__)) || \
    (defined(__FreeBSD_kernel__) && defined(__GLIBC__))
#  include <sys/syscall.h>
#  if defined(SYS_mmap) || defined(SYS_mmap2)
static inline void* _mmap(void* addr, size_t length, int prot, int flags,
                          int fd, off_t offset) {
// S390 only passes one argument to the mmap system call, which is a
// pointer to a structure containing the arguments.
#    ifdef __s390__
  struct {
    void* addr;
    size_t length;
    long prot;
    long flags;
    long fd;
    off_t offset;
  } args = {addr, length, prot, flags, fd, offset};
  return (void*)syscall(SYS_mmap, &args);
#    else
#      if defined(ANDROID) && defined(__aarch64__) && defined(SYS_mmap2)
  // Android NDK defines SYS_mmap2 for AArch64 despite it not supporting mmap2.
#        undef SYS_mmap2
#      endif
#      ifdef SYS_mmap2
  return (void*)syscall(SYS_mmap2, addr, length, prot, flags, fd, offset >> 12);
#      else
  return (void*)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
#      endif
#    endif
}
#    define mmap _mmap
#    define munmap(a, l) syscall(SYS_munmap, a, l)
#  endif
#endif

#ifdef XP_WIN

static void* pages_map(void* aAddr, size_t aSize, ShouldCommit should_commit) {
  switch (should_commit) {
    case ReserveAndCommit:
      return MozVirtualAlloc(aAddr, aSize, MEM_RESERVE | MEM_COMMIT,
                             PAGE_READWRITE);
    case ReserveOnly:
      // When not requesting committed memory we can skip MozVirtualAlloc and
      // its stall behaviour.
      return VirtualAlloc(aAddr, aSize, MEM_RESERVE, PAGE_NOACCESS);
  }
}

void pages_unmap(void* aAddr, size_t aSize) {
  if (VirtualFree(aAddr, 0, MEM_RELEASE) == 0) {
    _malloc_message(_getprogname(), ": (malloc) Error in VirtualFree()\n");
  }
}
#else

void pages_unmap(void* aAddr, size_t aSize) {
  if (munmap(aAddr, aSize) == -1) {
    char buf[64];

    if (!strerror_r(errno, buf, sizeof(buf))) {
      _malloc_message(_getprogname(), ": (malloc) Error in munmap(): ", buf,
                      "\n");
    }
  }
}

static void* pages_map(void* aAddr, size_t aSize, ShouldCommit should_commit) {
  void* ret;
#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  // The JS engine assumes that all allocated pointers have their high 17 bits
  // clear, which ia64's mmap doesn't support directly. However, we can emulate
  // it by passing mmap an "addr" parameter with those bits clear. The mmap will
  // return that address, or the nearest available memory above that address,
  // providing a near-guarantee that those bits are clear. If they are not, we
  // return nullptr below to indicate out-of-memory.
  //
  // The addr is chosen as 0x0000070000000000, which still allows about 120TB of
  // virtual address space.
  //
  // See Bug 589735 for more information.
  bool check_placement = true;
  if (!aAddr) {
    aAddr = (void*)0x0000070000000000;
    check_placement = false;
  }
#  endif

#  if defined(__sparc__) && defined(__arch64__) && defined(__linux__)
  const uintptr_t start = 0x0000070000000000ULL;
  const uintptr_t end = 0x0000800000000000ULL;

  // Copied from js/src/gc/Memory.cpp and adapted for this source
  uintptr_t hint;
  void* region = MAP_FAILED;
  for (hint = start; region == MAP_FAILED && hint + aSize <= end;
       hint += kChunkSize) {
    region = mmap((void*)hint, aSize,
                  should_commit ? PROT_READ | PROT_WRITE : PROT_NONE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (region != MAP_FAILED) {
      if (((size_t)region + (aSize - 1)) & 0xffff800000000000) {
        if (munmap(region, aSize)) {
          MOZ_ASSERT(errno == ENOMEM);
        }
        region = MAP_FAILED;
      }
    }
  }
  ret = region;
#  else
  // We don't use MAP_FIXED here, because it can cause the *replacement*
  // of existing mappings, and we only want to create new mappings.
  ret = mmap(
      aAddr, aSize,
      should_commit == ReserveAndCommit ? PROT_READ | PROT_WRITE : PROT_NONE,
      MAP_PRIVATE | MAP_ANON, -1, 0);
  MOZ_ASSERT(ret);
#  endif
  if (ret == MAP_FAILED) {
    ret = nullptr;
  }
#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  // If the allocated memory doesn't have its upper 17 bits clear, consider it
  // as out of memory.
  else if ((long long)ret & 0xffff800000000000) {
    munmap(ret, aSize);
    ret = nullptr;
  }
  // If the caller requested a specific memory location, verify that's what mmap
  // returned.
  else if (check_placement && ret != aAddr) {
#  else
  else if (aAddr && ret != aAddr) {
#  endif
    // We succeeded in mapping memory, but not in the right place.
    pages_unmap(ret, aSize);
    ret = nullptr;
  }
  if (ret) {
    MozTagAnonymousMemory(ret, aSize, "jemalloc");
  }

#  if defined(__ia64__) || \
      (defined(__sparc__) && defined(__arch64__) && defined(__linux__))
  MOZ_ASSERT(!ret || (!check_placement && ret) ||
             (check_placement && ret == aAddr));
#  else
  MOZ_ASSERT(!ret || (!aAddr && ret != aAddr) || (aAddr && ret == aAddr));
#  endif
  return ret;
}
#endif

// ***************************************************************************

void pages_decommit(void* aAddr, size_t aSize) {
#ifdef XP_WIN
  // The region starting at addr may have been allocated in multiple calls
  // to VirtualAlloc and recycled, so decommitting the entire region in one
  // go may not be valid. However, since we allocate at least a chunk at a
  // time, we may touch any region in chunksized increments.
  size_t pages_size = std::min(aSize, kChunkSize - GetChunkOffsetForPtr(aAddr));
  while (aSize > 0) {
    // This will cause Access Violation on read and write and thus act as a
    // guard page or region as well.
    if (!VirtualFree(aAddr, pages_size, MEM_DECOMMIT)) {
      MOZ_CRASH();
    }
    aAddr = (void*)((uintptr_t)aAddr + pages_size);
    aSize -= pages_size;
    pages_size = std::min(aSize, kChunkSize);
  }
#else
  if (mmap(aAddr, aSize, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1,
           0) == MAP_FAILED) {
    // We'd like to report the OOM for our tooling, but we can't allocate
    // memory at this point, so avoid the use of printf.
    const char out_of_mappings[] =
        "[unhandlable oom] Failed to mmap, likely no more mappings "
        "available " __FILE__ " : " MOZ_STRINGIFY(__LINE__);
    if (errno == ENOMEM) {
#  ifndef ANDROID
      fputs(out_of_mappings, stderr);
      fflush(stderr);
#  endif
      MOZ_CRASH_ANNOTATE(out_of_mappings);
    }
    MOZ_REALLY_CRASH(__LINE__);
  }
  MozTagAnonymousMemory(aAddr, aSize, "jemalloc-decommitted");
#endif
}

// Commit pages. Returns whether pages were committed.
[[nodiscard]] bool pages_commit(void* aAddr, size_t aSize) {
#ifdef XP_WIN
  // The region starting at addr may have been allocated in multiple calls
  // to VirtualAlloc and recycled, so committing the entire region in one
  // go may not be valid. However, since we allocate at least a chunk at a
  // time, we may touch any region in chunksized increments.
  size_t pages_size = std::min(aSize, kChunkSize - GetChunkOffsetForPtr(aAddr));
  while (aSize > 0) {
    if (!MozVirtualAlloc(aAddr, pages_size, MEM_COMMIT, PAGE_READWRITE)) {
      return false;
    }
    aAddr = (void*)((uintptr_t)aAddr + pages_size);
    aSize -= pages_size;
    pages_size = std::min(aSize, kChunkSize);
  }
#else
  if (mmap(aAddr, aSize, PROT_READ | PROT_WRITE,
           MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0) == MAP_FAILED) {
    return false;
  }
  MozTagAnonymousMemory(aAddr, aSize, "jemalloc");
#endif
  return true;
}

#ifndef XP_WIN
// Purge and release the pages in the chunk of length `length` at `addr` to
// the OS.
// Pages are guaranteed to be full of zeroes when the
// function returns.
static void pages_purge(void* addr, size_t length) {
  pages_decommit(addr, length);
}
#endif

// pages_trim, pages_mmap_aligned_slow and pages_mmap_aligned were
// cherry-picked from upstream jemalloc 3.4.1 to fix Mozilla bug 956501.

static void* pages_trim(void* addr, size_t alloc_size, size_t leadsize,
                        size_t size, ShouldCommit should_commit) {
  void* ret = (void*)((uintptr_t)addr + leadsize);

  MOZ_ASSERT(alloc_size >= leadsize + size);
#ifdef XP_WIN
  {
    void* new_addr;

    pages_unmap(addr, alloc_size);
    new_addr = pages_map(ret, size, should_commit);
    if (new_addr == ret) {
      return ret;
    }
    if (new_addr) {
      pages_unmap(new_addr, size);
    }
    return nullptr;
  }
#else
  {
    size_t trailsize = alloc_size - leadsize - size;

    if (leadsize != 0) {
      pages_unmap(addr, leadsize);
    }
    if (trailsize != 0) {
      pages_unmap((void*)((uintptr_t)ret + size), trailsize);
    }
    return ret;
  }
#endif
}

static void* pages_mmap_aligned_slow(size_t size, size_t alignment,
                                     ShouldCommit should_commit) {
  void *ret, *pages;
  size_t alloc_size, leadsize;

  alloc_size = size + alignment - gRealPageSize;
  // Beware size_t wrap-around.
  if (alloc_size < size) {
    return nullptr;
  }
  do {
    pages = pages_map(nullptr, alloc_size, should_commit);
    if (!pages) {
      return nullptr;
    }
    leadsize =
        ALIGNMENT_CEILING((uintptr_t)pages, alignment) - (uintptr_t)pages;
    ret = pages_trim(pages, alloc_size, leadsize, size, should_commit);
  } while (!ret);

  MOZ_ASSERT(ret);
  return ret;
}

void* pages_mmap_aligned(size_t size, size_t alignment,
                         ShouldCommit should_commit) {
  void* ret;
  size_t offset;

  // Ideally, there would be a way to specify alignment to mmap() (like
  // NetBSD has), but in the absence of such a feature, we have to work
  // hard to efficiently create aligned mappings. The reliable, but
  // slow method is to create a mapping that is over-sized, then trim the
  // excess. However, that always results in one or two calls to
  // pages_unmap().
  //
  // Optimistically try mapping precisely the right amount before falling
  // back to the slow method, with the expectation that the optimistic
  // approach works most of the time.
  ret = pages_map(nullptr, size, should_commit);
  if (!ret) {
    return nullptr;
  }
  offset = ALIGNMENT_ADDR2OFFSET(ret, alignment);
  if (offset != 0) {
    pages_unmap(ret, size);
    return pages_mmap_aligned_slow(size, alignment, should_commit);
  }

  MOZ_ASSERT(ret);
  return ret;
}

constinit AddressRadixTree<(sizeof(void*) << 3) - LOG2(kChunkSize)> gChunkRTree;

#ifdef MOZ_DEBUG
void chunk_assert_zero(void* aPtr, size_t aSize) {
// Only run this expensive check in a vigilant mode.
#  ifdef MALLOC_DEBUG_VIGILANT
  size_t i;
  size_t* p = (size_t*)(uintptr_t)aPtr;

  for (i = 0; i < aSize / sizeof(size_t); i++) {
    MOZ_ASSERT(p[i] == 0);
  }
#  endif
}
#endif

// Deallocate chunks, possibly recording them for future recycling.
// Used for both base allocator chunks and arena chunks already
// removed from gChunkRTree.
void base_chunk_dealloc(void* aChunk, size_t aSize, ChunkType aType) {
  MOZ_ASSERT(aChunk);
  MOZ_ASSERT(GetChunkOffsetForPtr(aChunk) == 0);
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);
  MOZ_ASSERT(!gChunkRTree.Get(aChunk));

#ifndef XP_WIN
  if (gCache.TryRecord(aChunk, aSize, aType)) {
    return;
  }
#endif

  pages_unmap(aChunk, aSize);
}

// Deallocate chunks used for Arena allocations.
void arena_chunk_dealloc(chunk_allocator_t* aChunkAllocator, void* aChunk,
                         size_t aSize) {
  MOZ_ASSERT(aChunk);
  MOZ_ASSERT(GetChunkOffsetForPtr(aChunk) == 0);
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);

  gChunkRTree.Unset(aChunk);

  aChunkAllocator->unmap(aChunk, aSize);
}

// Allocates `size` bytes of system memory aligned for `alignment` for the
// base allocator.
void* base_chunk_alloc(size_t aSize, size_t aAlignment) {
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);
  MOZ_ASSERT(aAlignment != 0);
  MOZ_ASSERT((aAlignment & kChunkSizeMask) == 0);

  // Base allocations can't be fulfilled by recycling because of
  // possible deadlock or infinite recursion.
  void* ret = pages_mmap_aligned(aSize, aAlignment, ReserveAndCommit);
  MOZ_ASSERT(GetChunkOffsetForPtr(ret) == 0);

  return ret;
}

// Allocates `size` bytes of system memory aligned for `alignment` for
// arena allocations.
void* arena_chunk_alloc(chunk_allocator_t* aChunkAllocator, size_t aSize,
                        size_t aAlignment, size_t aCommitSize) {
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT((aSize & kChunkSizeMask) == 0);
  MOZ_ASSERT(aAlignment != 0);
  MOZ_ASSERT((aAlignment & kChunkSizeMask) == 0);
  MOZ_ASSERT(aCommitSize <= aSize);

  void* ret = aChunkAllocator->map(aSize, aAlignment, aCommitSize);
  if (ret) {
    if (!gChunkRTree.Set(ret, ret)) {
      aChunkAllocator->unmap(ret, aSize);
      return nullptr;
    }
  }

  MOZ_ASSERT(GetChunkOffsetForPtr(ret) == 0);
  return ret;
}

static void* system_pages_map(size_t aSize, size_t aAlignment,
                              size_t aCommitSize) {
  void* ret = nullptr;

#ifndef XP_WIN
  ret = gCache.Recycle(aSize, aAlignment, aCommitSize);
  if (!ret) {
#endif
    if (aCommitSize == aSize) {
      ret = pages_mmap_aligned(aSize, aAlignment, ReserveAndCommit);
    } else {
      // Reserve the whole range inaccessible, then commit only the usable
      // portion so the remainder stays an inaccessible guard throughout.
      ret = pages_mmap_aligned(aSize, aAlignment, ReserveOnly);
      if (ret && !pages_commit(ret, aCommitSize)) {
        pages_unmap(ret, aSize);
        ret = nullptr;
      }
    }
#ifndef XP_WIN
  }
#endif

  return ret;
}

static void system_pages_unmap(void* aAddr, size_t aSize) {
  base_chunk_dealloc(aAddr, aSize, ARENA_CHUNK);
}

chunk_allocator_t gSystemChunkAllocator{
    .map = system_pages_map,
    .unmap = system_pages_unmap,
    .commit = pages_commit,
    .decommit = pages_decommit,
};

arena_chunk_t::arena_chunk_t(arena_t* aArena)
    : mArena(aArena), mDirtyRunHint(gChunkHeaderNumPages) {}

bool arena_chunk_t::IsEmpty() {
  return (mPageMap[gChunkHeaderNumPages].bits &
          (~gPageSizeMask | CHUNK_MAP_ALLOCATED)) == gMaxLargeClass;
}

#ifndef XP_WIN

bool ChunkCache::TryRecord(void* aChunk, size_t aSize, ChunkType aType) {
  size_t recycled_so_far = mRecycledSize;

  // In case some race condition put us above the limit.
  if (recycled_so_far >= gRecycleLimit) {
    return false;
  }

  size_t recycle_remaining = gRecycleLimit - recycled_so_far;
  size_t to_recycle;
  if (aSize > recycle_remaining) {
    to_recycle = recycle_remaining;
    // Drop pages that would overflow the recycle limit
    pages_trim(aChunk, aSize, 0, to_recycle, ReserveAndCommit);
  } else {
    to_recycle = aSize;
  }
  Record(aChunk, to_recycle, aType);
  return true;
}

void ChunkCache::Record(void* aChunk, size_t aSize, ChunkType aType) {
  if (aType != ZEROED_CHUNK) {
    pages_purge(aChunk, aSize);
    aType = ZEROED_CHUNK;
  }

  // Allocate a node before acquiring mMutex even though it might not be
  // needed, otherwise the base allocator may cause a new base chunk to be
  // allocated, which could cause deadlock if mMutex were already held.
  UniqueBaseNode xnode(new (fallible) extent_node_t());
  // Use xprev to implement conditional deferred deallocation of prev.
  UniqueBaseNode xprev;

  // RAII deallocates xnode and xprev defined above after unlocking
  // in order to avoid potential dead-locks
  MutexAutoLock lock(mMutex);
  void* addr = (void*)((uintptr_t)aChunk + aSize);
  extent_node_t* node = gChunksByAddress.SearchOrNext(addr);
  // Try to coalesce forward.
  if (node && node->mAddr == addr) {
    // Coalesce chunk with the following address range.  This does
    // not change the position within gChunksByAddress, so only
    // remove/insert from/into gChunksBySize.
    gChunksBySize.Remove(node);
    node->mAddr = aChunk;
    node->mSize += aSize;
    if (node->mChunkType != aType) {
      node->mChunkType = RECYCLED_CHUNK;
    }
    gChunksBySize.Insert(node);
  } else {
    // Coalescing forward failed, so insert a new node.
    if (!xnode) {
      // BaseAlloc::alloc failed, which is an exceedingly unlikely failure.
      // Leak the chunk; its pages have already been purged, so this is only
      // a virtual memory leak.
      return;
    }
    node = xnode.release();
    node->mAddr = aChunk;
    node->mSize = aSize;
    node->mChunkType = aType;
    gChunksByAddress.Insert(node);
    gChunksBySize.Insert(node);
  }

  // Try to coalesce backward.
  extent_node_t* prev = gChunksByAddress.Prev(node);
  if (prev && (void*)((uintptr_t)prev->mAddr + prev->mSize) == aChunk) {
    // Coalesce chunk with the previous address range.  This does
    // not change the position within gChunksByAddress, so only
    // remove/insert node from/into gChunksBySize.
    gChunksBySize.Remove(prev);
    gChunksByAddress.Remove(prev);

    gChunksBySize.Remove(node);
    node->mAddr = prev->mAddr;
    node->mSize += prev->mSize;
    if (node->mChunkType != prev->mChunkType) {
      node->mChunkType = RECYCLED_CHUNK;
    }
    gChunksBySize.Insert(node);

    xprev.reset(prev);
  }

  mRecycledSize += aSize;
}

void* ChunkCache::Recycle(size_t aSize, size_t aAlignment, size_t aCommitSize) {
  MOZ_ASSERT(aCommitSize <= aSize);
  size_t alloc_size = aSize + aAlignment - kChunkSize;
  // Beware size_t wrap-around.
  if (alloc_size < aSize) {
    return nullptr;
  }

  // new_node is used when splitting node creates a second node, it is
  // allocated here before taking mMutex to avoid a deadlock.
  UniqueBaseNode new_node(new (fallible) extent_node_t());

  // unused_node is used to defer deallocation of the node that described the
  // recycled range until after mMutex is released.  It can overlap with
  // new_node so needs to be a separate variable.
  UniqueBaseNode unused_node;

  void* ret;
  {
    MutexAutoLock lock(mMutex);
    extent_node_t* node = gChunksBySize.SearchOrNext(alloc_size);
    if (!node) {
      return nullptr;
    }
    size_t lead_size = ALIGNMENT_CEILING((uintptr_t)node->mAddr, aAlignment) -
                       (uintptr_t)node->mAddr;
    MOZ_ASSERT(node->mSize >= lead_size + aSize);
    size_t trail_size = node->mSize - lead_size - aSize;
    if (lead_size != 0 && trail_size != 0 && !new_node) {
      // Splitting on both sides requires a second node but
      // BaseAlloc::alloc() failed to allocate one (although unlikely).
      // Abort here and maybe the caller can map fresh pages.
      return nullptr;
    }
    ret = (void*)((uintptr_t)node->mAddr + lead_size);

    // All recycled chunks are zeroed (because they're purged) before being
    // recycled.
    MOZ_ASSERT(node->mChunkType == ZEROED_CHUNK);

    // Remove node from the tree.
    gChunksBySize.Remove(node);
    gChunksByAddress.Remove(node);
    if (lead_size != 0) {
      // Insert the leading space as a smaller chunk.
      node->mSize = lead_size;
      gChunksBySize.Insert(node);
      gChunksByAddress.Insert(node);
      node = nullptr;
    }
    if (trail_size != 0) {
      // Insert the trailing space as a smaller chunk.
      if (!node) {
        node = new_node.release();
      }
      node->mAddr = (void*)((uintptr_t)(ret) + aSize);
      node->mSize = trail_size;
      node->mChunkType = ZEROED_CHUNK;
      gChunksBySize.Insert(node);
      gChunksByAddress.Insert(node);
      node = nullptr;
    }

    mRecycledSize -= aSize;

    // node will be freed when unused_node goes out of scope, which is after
    // the lock is released.
    unused_node.reset(node);
  }

  if (!pages_commit(ret, aCommitSize)) {
    return nullptr;
  }

  return ret;
}

// The global chunk cache.
ChunkCache gCache;

#endif /* ! XP_WIN */
