/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef CHUNK_H
#define CHUNK_H

#include "mozilla/Atomics.h"
#include "mozilla/ThreadSafety.h"

#include "mozjemalloc_types.h"

#include "Extent.h"
#include "RadixTree.h"

#include "mozilla/DoublyLinkedList.h"

// ***************************************************************************
// Structures for chunk headers for chunks used for non-huge allocations.

struct arena_t;

enum ChunkType;

// Each element of the chunk map corresponds to one page within the chunk.
struct arena_chunk_map_t {
  // Linkage for run lists. Used for arena_t's available runs (see
  // ArenaAvailRuns.h).
  mozilla::DoublyLinkedListElement<arena_chunk_map_t> link;

  // Run address (or size) and various flags are stored together.  The bit
  // layout looks like (assuming 32-bit system):
  //
  //   ???????? ???????? ????---b fmckdzla
  //
  // ? : Unallocated: Run address for first/last pages, unset for internal
  //                  pages.
  //     Small: Run address.
  //     Large: Run size for first page, unset for trailing pages.
  // - : Unused.
  // b : Busy?
  // f : Fresh memory?
  // m : MADV_FREE/MADV_DONTNEED'ed?
  // c : decommitted?
  // k : key?
  // d : dirty?
  // z : zeroed?
  // l : large?
  // a : allocated?
  //
  // Following are example bit patterns for consecutive pages from the three
  // types of runs.
  //
  // r : run address
  // s : run size
  // x : don't care
  // - : 0
  // [cdzla] : bit set
  //
  //   Unallocated:
  //     ssssssss ssssssss ssss---- --c-----
  //     xxxxxxxx xxxxxxxx xxxx---- ----d---
  //     ssssssss ssssssss ssss---- -----z--
  //
  //     Note that the size fields are set for the first and last unallocated
  //     page only.  The pages in-between have invalid/"don't care" size fields,
  //     they're not cleared during things such as coalescing free runs.
  //
  //     Pages before the first or after the last page in a free run must be
  //     allocated or busy.  Run coalescing depends on the sizes being set in
  //     the first and last page.  Purging pages and releasing chunks require
  //     that unallocated pages are always coalesced and the first page has a
  //     correct size.
  //
  //   Small:
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //     rrrrrrrr rrrrrrrr rrrr---- -------a
  //
  //   Large:
  //     ssssssss ssssssss ssss---- ------la
  //     -------- -------- -------- ------la
  //     -------- -------- -------- ------la
  //
  //     Note that only the first page has the size set.
  //
  size_t bits;

// A page can be in one of several states.
//
// CHUNK_MAP_ALLOCATED marks allocated pages, the only other bit that can be
// combined is CHUNK_MAP_LARGE.
//
// CHUNK_MAP_LARGE may be combined with CHUNK_MAP_ALLOCATED to show that the
// allocation is a "large" allocation (see SizeClass), rather than a run of
// small allocations.  The interpretation of the gPageSizeMask bits depends onj
// this bit, see the description above.
//
// CHUNK_MAP_DIRTY is used to mark pages that were allocated and are now freed.
// They may contain their previous contents (or poison).  CHUNK_MAP_DIRTY, when
// set, must be the only set bit.
//
// CHUNK_MAP_MADVISED marks pages which are madvised (with either MADV_DONTNEED
// or MADV_FREE).  This is only valid if MALLOC_DECOMMIT is not defined.  When
// set, it must be the only bit set.
//
// CHUNK_MAP_DECOMMITTED is used if CHUNK_MAP_DECOMMITTED is defined.  Unused
// dirty pages may be decommitted and marked as CHUNK_MAP_DECOMMITTED.  They
// must be re-committed with pages_commit() before they can be touched.
//
// CHUNK_MAP_FRESH is set on pages that have never been used before (the chunk
// is newly allocated or they were decommitted and have now been recommitted.
// CHUNK_MAP_FRESH is also used for "double purged" pages meaning that they were
// madvised and later were unmapped and remapped to force them out of the
// program's resident set.  This is enabled when MALLOC_DOUBLE_PURGE is defined
// (eg on MacOS).
//
// CHUNK_MAP_BUSY is set by a thread when the thread wants to manipulate the
// pages without holding a lock. Other threads must not touch these pages
// regardless of whether they hold a lock.
//
// CHUNK_MAP_ZEROED is set on pages that are known to contain zeros.
//
// CHUNK_MAP_DIRTY, _DECOMMITED _MADVISED and _FRESH are always mutually
// exclusive.
//
#define CHUNK_MAP_BUSY ((size_t)0x100U)
#define CHUNK_MAP_FRESH ((size_t)0x80U)
#define CHUNK_MAP_MADVISED ((size_t)0x40U)
#define CHUNK_MAP_DECOMMITTED ((size_t)0x20U)
#define CHUNK_MAP_MADVISED_OR_DECOMMITTED \
  (CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED)
#define CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED \
  (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED)
#define CHUNK_MAP_FRESH_MADVISED_DECOMMITTED_OR_BUSY              \
  (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED | CHUNK_MAP_DECOMMITTED | \
   CHUNK_MAP_BUSY)
#define CHUNK_MAP_DIRTY ((size_t)0x08U)
#define CHUNK_MAP_ZEROED ((size_t)0x04U)
#define CHUNK_MAP_LARGE ((size_t)0x02U)
#define CHUNK_MAP_ALLOCATED ((size_t)0x01U)
};

// Arena chunk header.
struct arena_chunk_t {
  // Arena that owns the chunk.
  arena_t* mArena;

  // Linkage for the arena's list of dirty chunks.
  mozilla::DoublyLinkedListElement<arena_chunk_t> mChunksDirtyElement;

#ifdef MALLOC_DOUBLE_PURGE
  // If we're double-purging, we maintain a linked list of chunks which
  // have pages which have been madvise(MADV_FREE)'d but not explicitly
  // purged.
  //
  // We're currently lazy and don't remove a chunk from this list when
  // all its madvised pages are recommitted.
  mozilla::DoublyLinkedListElement<arena_chunk_t> mChunksMadvisedElement;
#endif

  // Number of dirty pages that may be purged, the header is never counted
  // here.
  uint16_t mNumDirty = 0;

  // This will point to the page index of the first run that may have dirty
  // pages.
  uint16_t mDirtyRunHint = 0;

  bool mIsPurging = false;

  // Map of pages within chunk that keeps track of free/large/small.
  arena_chunk_map_t mPageMap[];  // Dynamically sized.

  explicit arena_chunk_t(arena_t* aArena);

  bool IsEmpty();
};

namespace mozilla {
struct DirtyChunkListTrait {
  static DoublyLinkedListElement<arena_chunk_t>& Get(arena_chunk_t* aThis) {
    return aThis->mChunksDirtyElement;
  }

  static const DoublyLinkedListElement<arena_chunk_t>& Get(
      const arena_chunk_t* aThis) {
    return aThis->mChunksDirtyElement;
  }

  using SearchKey = arena_chunk_t*;
};
}  // namespace mozilla

[[nodiscard]] bool pages_commit(void* aAddr, size_t aSize);

void pages_decommit(void* aAddr, size_t aSize);

void* base_chunk_alloc(size_t aSize, size_t aAlignment);

void base_chunk_dealloc(void* aChunk, size_t aSize, ChunkType aType);

void* arena_chunk_alloc(chunk_allocator_t* aChunkAllocator, size_t aSize,
                        size_t aAlignment, size_t aCommitSize);

void arena_chunk_dealloc(chunk_allocator_t* aChunkAllocator, void* aChunk,
                         size_t aSize);
#ifdef MOZ_DEBUG
void chunk_assert_zero(void* aPtr, size_t aSize);
#endif

extern AddressRadixTree<(sizeof(void*) << 3) - LOG2(kChunkSize)> gChunkRTree;

// Default chunk allocator for arena's that uses pages from anywhere in the
// process address space.
extern chunk_allocator_t gSystemChunkAllocator;

enum ShouldCommit {
  // Reserve address space only, accessing the mapping will crash.
  ReserveOnly,

  // Reserve the address space and populate it with "valid" page mappings.
  // On windows this will commit memory, on Linux it populate memory as its
  // accessed (with overcommit behaviour).
  ReserveAndCommit,
};

void* pages_mmap_aligned(size_t size, size_t alignment,
                         ShouldCommit should_commit);

void pages_unmap(void* aAddr, size_t aSize);

// On Windows, calls to VirtualAlloc and VirtualFree must be matched, making
// it awkward to recycle allocations of varying sizes.
// Therefore the chunk cache is disabled on windows since arenas have a
// chunk list for arena chunks, and non-arena chunks arbitrary sizes
// don't cache well without splitting.
#ifndef XP_WIN

class ChunkCache {
 private:
  Mutex mMutex;

  // Trees of chunks that were previously allocated (trees differ only in node
  // ordering).  These are used when allocating chunks, in an attempt to re-use
  // address space.  Depending on function, different tree orderings are needed,
  // which is why there are two trees with the same contents.
  RedBlackTree<extent_node_t, ExtentTreeSzTrait> gChunksBySize
      MOZ_GUARDED_BY(mMutex);
  RedBlackTree<extent_node_t, ExtentTreeTrait> gChunksByAddress
      MOZ_GUARDED_BY(mMutex);

  // The current amount of recycled bytes, updated atomically.
  mozilla::Atomic<size_t> mRecycledSize;

 public:
  constexpr ChunkCache() = default;

  void Init() { mMutex.Init(); }

  // Try to put this chunk in the cache, false if the cache is full.
  bool TryRecord(void* aChunk, size_t aSize, ChunkType aType);

 private:
  // Put this chunk in the cache.
  void Record(void* aChunk, size_t aSize, ChunkType aType);

 public:
  // Retrive a chunk from the cache.  Only the first aCommitSize bytes are
  // committed read-write; any remainder is left inaccessible as a guard.
  void* Recycle(size_t aSize, size_t aAlignment, size_t aCommitSize);
};

extern ChunkCache gCache;

#endif /* ! XP_WIN */

#endif /* ! CHUNK_H */
