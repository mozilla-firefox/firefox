/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Portions of this file were originally under the following license:
//
// Copyright (C) 2006-2008 Jason Evans <jasone@FreeBSD.org>.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice(s), this list of conditions and the following disclaimer as
//    the first lines of this file unmodified other than the possible
//    addition of one or more copyright notices.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice(s), this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef JEMALLOC_TYPES_H_
#define JEMALLOC_TYPES_H_

#include <stdint.h>

// grab size_t
#ifdef _MSC_VER
#  include <crtdefs.h>
#else
#  include <stddef.h>
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MALLOC_USABLE_SIZE_CONST_PTR
#  define MALLOC_USABLE_SIZE_CONST_PTR const
#endif

typedef MALLOC_USABLE_SIZE_CONST_PTR void* usable_ptr_t;

typedef size_t arena_id_t;

// A chunk allocator provides an abstraction for mapping, unmapping, committing
// and decommitting chunks of pages. This allows for greater control over the
// memory used to back an arena's allocations.
//
// The current primary use case for this is to restrict all of an arena's
// allocations to a specific memory region as is required for
// SpiderMonkey's sandbox.
typedef struct chunk_allocator_s {
  // Map aSize bytes of memory with alignment aAligment.  The first
  // aCommitSize bytes are committed with read-write permissions; any
  // remainder is reserved but left inaccessible, so a caller that wants a
  // trailing guard region never sees it mapped read-write (which would let
  // an adjacent growable stack race into it).  aCommitSize == aSize commits
  // the whole mapping.
  void* (*map)(size_t aSize, size_t aAlignment, size_t aCommitSize);

  // Unmap aSize bytes of previously mapped memory starting at aAddr.
  // The pages are returned to the allocator.
  void (*unmap)(void* aAddr, size_t aSize);

  // Commit aSize bytes of previously mapped decommitted memory starting at
  // aAddr.
  bool (*commit)(void* aAddr, size_t aSize);

  // Decommit aSize bytes of previously mapped memory starting at aAddr.
  // These pages need to be re-committed before they can be used again.
  void (*decommit)(void* aAddr, size_t aSize);
} chunk_allocator_t;

#define ARENA_FLAG_RANDOMIZE_SMALL_MASK 0x3
#define ARENA_FLAG_RANDOMIZE_SMALL_DEFAULT 0
#define ARENA_FLAG_RANDOMIZE_SMALL_ENABLED 1
#define ARENA_FLAG_RANDOMIZE_SMALL_DISABLED 2

// Arenas are usually protected by a lock (ARENA_FLAG_THREAD_SAFE) however some
// arenas are accessed by only the main thread
// (ARENA_FLAG_THREAD_MAIN_THREAD_ONLY) and their locking can be skipped.
#define ARENA_FLAG_THREAD_MASK 0x4
#define ARENA_FLAG_THREAD_MAIN_THREAD_ONLY 0x4
#define ARENA_FLAG_THREAD_SAFE 0x0

typedef struct arena_params_s {
  size_t mMaxDirty;
  // Arena specific modifiers which override the value passed to
  // moz_set_max_dirty_page_modifier. If value > 0 is passed to that function,
  // and mMaxDirtyIncreaseOverride != 0, mMaxDirtyIncreaseOverride will be used
  // instead, and similarly if value < 0 is passed and mMaxDirtyDecreaseOverride
  // != 0, mMaxDirtyDecreaseOverride will be used as the modifier.
  int32_t mMaxDirtyIncreaseOverride;
  int32_t mMaxDirtyDecreaseOverride;

  uint32_t mFlags;

  // The label will be copied into fixed-size storage (currently 128 bytes)
  // within the arena.  It may be null for unamed arenas
  const char* mLabel;

  // Chunk allocator to be used by the Arena.
  // If this is not set, the default system allocator will be used.
  chunk_allocator_t* mChunkAllocator;

#ifdef __cplusplus
  arena_params_s()
      : mMaxDirty(0),
        mMaxDirtyIncreaseOverride(0),
        mMaxDirtyDecreaseOverride(0),
        mFlags(0),
        mLabel(nullptr),
        mChunkAllocator(nullptr) {}
#endif
} arena_params_t;

// jemalloc_stats() is not a stable interface.  When using jemalloc_stats_t, be
// sure that the compiled results of mozjemalloc.cpp are in sync with this
// header file.
typedef struct {
  // Run-time configuration settings.
  bool opt_junk;             // Fill allocated memory with kAllocJunk?
  bool opt_randomize_small;  // Randomization of small allocations?
  bool opt_zero;             // Fill allocated memory with 0x0?
  size_t narenas;            // Number of arenas.
  size_t quantum;            // Allocation quantum.
  size_t quantum_max;        // Max quantum-spaced allocation size.
  size_t quantum_wide;       // Allocation quantum (QuantuWide).
  size_t quantum_wide_max;   // Max quantum-wide-spaced allocation size.
  size_t subpage_max;        // Max subpage allocation size.
  size_t large_max;          // Max sub-chunksize allocation size.
  size_t chunksize;          // Size of each virtual memory mapping.
  size_t page_size;          // Size of pages in mozjemalloc internal
                             // structures.
  size_t real_page_size;     // Size of OS/hardware pages.
  size_t dirty_max;          // Max dirty pages per arena.

  // Current memory usage statistics.
  size_t mapped;          // Bytes mapped (not necessarily committed).
  size_t allocated;       // Bytes allocated (committed, in use by application).
  size_t waste;           // Bytes committed, not in use by the
                          // application, and not intentionally left
                          // unused (i.e., not dirty).
  size_t pages_dirty;     // Committed, unused pages kept around as a cache.
  size_t pages_fresh;     // Unused pages that have never been touched.
  size_t pages_madvised;  // Unsed pages we told the kernel we don't need.
  size_t bookkeeping;     // Committed bytes used internally by the
                          // allocator.
  size_t bin_unused;      // Bytes committed to a bin but currently unused.

  size_t num_operations;  // The number of malloc()+free() calls.  Note that
                          // realloc calls
                          // count as 0, 1 or 2 operations depending on internal
                          // operations.  Which internal operations (eg in place
                          // or move, or different size classes) require
                          // different internal operations is unspecified.
  size_t arena_run_header;
} jemalloc_stats_t;

typedef struct {
  size_t size;               // The size of objects in this bin, zero if this
                             // bin stats array entry is unused (no more bins).
  size_t num_non_full_runs;  // The number of non-full runs
  size_t num_runs;           // The number of runs in this bin
  size_t bytes_unused;       // The unallocated bytes across all these bins
  size_t bytes_total;        // The total storage area for runs in this bin,
  size_t bytes_per_run;      // The number of bytes per run, including headers.
  size_t regions_per_run;    // The number of regions (aka cells) per run.
} jemalloc_bin_stats_t;

// jemalloc_stats_lite() is not a stable interface.  When using
// jemalloc_stats_lite_t, be sure that the compiled results of mozjemalloc.cpp
// are in sync with this header file.
typedef struct {
  size_t allocated_bytes;

  // The number of malloc()+free() calls.  realloc calls count as 0, 1 or 2
  // operations depending on whether they do nothing, resize in-place, or move
  // the memory.
  uint64_t num_operations;
} jemalloc_stats_lite_t;

enum PtrInfoTag {
  // The pointer is not currently known to the allocator.
  // 'addr', 'size', and 'arenaId' are always 0.
  TagUnknown,

  // The pointer is within a live allocation.
  // 'addr', 'size', and 'arenaId' describe the allocation.
  TagLiveAlloc,

  // The pointer is within a small freed allocation.
  // 'addr', 'size', and 'arenaId' describe the allocation.
  TagFreedAlloc,

  // The pointer is within a freed page. Details about the original
  // allocation, including its size, are not available.
  // 'addr', 'size', and 'arenaId' describe the page.
  TagFreedPage,
};

// The information in jemalloc_ptr_info_t could be represented in a variety of
// ways. The chosen representation has the following properties.
// - The number of fields is minimized.
// - The 'tag' field unambiguously defines the meaning of the subsequent fields.
// Helper functions are used to group together related categories of tags.
typedef struct jemalloc_ptr_info_s {
#ifdef __cplusplus
#  define MOZ_FIELD_INIT(s, v) s = v
#else
#  define MOZ_FIELD_INIT(s, v) s
#endif

  enum PtrInfoTag MOZ_FIELD_INIT(tag, TagUnknown);
  void* MOZ_FIELD_INIT(addr, nullptr);  // meaning depends on tag; see above
  size_t MOZ_FIELD_INIT(size, 0);       // meaning depends on tag; see above

#ifdef MOZ_DEBUG
  arena_id_t MOZ_FIELD_INIT(arenaId, 0);  // meaning depends on tag; see above
#endif

#ifdef __cplusplus
  jemalloc_ptr_info_s() = default;
  jemalloc_ptr_info_s(enum PtrInfoTag aTag, void* aAddr, size_t aSize,
                      arena_id_t aArenaId)
      : tag(aTag),
        addr(aAddr),
        size(aSize)
#  ifdef MOZ_DEBUG
        ,
        arenaId(aArenaId)
#  endif
  {
  }
#endif

#undef MOZ_FIELD_INIT
} jemalloc_ptr_info_t;

static inline bool jemalloc_ptr_is_live(jemalloc_ptr_info_t* info) {
  return info->tag == TagLiveAlloc;
}

static inline bool jemalloc_ptr_is_freed(jemalloc_ptr_info_t* info) {
  return info->tag == TagFreedAlloc || info->tag == TagFreedPage;
}

static inline bool jemalloc_ptr_is_freed_page(jemalloc_ptr_info_t* info) {
  return info->tag == TagFreedPage;
}

// The result of purging memory from a sigle arena
enum ArenaPurgeResult {
  // The stop threshold of dirty pages was reached or all the remaining chunks
  // that may have dirty pages are busy.  The allocator can't always tell the
  // difference between these conditions.
  ReachedThresholdOrBusy,

  // There's more chunks in this arena that could be purged.
  NotDone,

  // The arena needs to be destroyed by the caller.
  Dying,
};

// The result of calling moz_may_purge_now().
enum may_purge_now_result_t {
  // Done: No more purge requests are pending.
  Done,

  // There may be one or more arenas whose reuse grace period expired and
  // needs purging asap.
  NeedsMore,

  // There is at least one arena that waits either for its reuse grace to
  // expire or for significant reuse to happen. As we cannot foresee the
  // future, whatever schedules the purges should come back later to check
  // if we need a purge.
  WantsLater,
};

// Asked whether a deferred purge should carry on. Gets the closure that was
// passed to moz_may_purge_now().
typedef bool (*purge_keep_going_t)(void* aClosure);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // JEMALLOC_TYPES_H_
