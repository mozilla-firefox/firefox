/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BASEALLOC_H
#define BASEALLOC_H

#include <algorithm>

#include "Constants.h"
#include "Mutex.h"
#include "RedBlackTree.h"
#include "Utils.h"

#include "mozilla/DoublyLinkedList.h"
#include "mozilla/fallible.h"

#include "BaseAllocInternals.h"

// The base allocator is a simple memory allocator used internally by
// mozjemalloc for its own structures.
class BaseAlloc {
 public:
  constexpr BaseAlloc() = default;

  void Init() MOZ_REQUIRES(gInitLock);

  // These functions are exposed with MFBT_API so they can be called from
  // gtests.

  MFBT_API void* alloc(size_t aSize) MOZ_EXCLUDES(mMutex);

  MFBT_API void* calloc(size_t aNumber, size_t aSize) MOZ_EXCLUDES(mMutex);

  // usable_size is safe both with and without the lock.
  MFBT_API size_t usable_size(void* aPtr);

  MFBT_API void free(void* aPtr) MOZ_EXCLUDES(mMutex);

  MFBT_API void* realloc(void* aPtr, size_t aNewSize) MOZ_EXCLUDES(mMutex);

  Mutex mMutex;

  struct Stats {
    size_t mMapped = 0;
    size_t mCommitted = 0;
  };
  Stats GetStats() MOZ_EXCLUDES(mMutex) {
    MutexAutoLock lock(mMutex);

    MOZ_ASSERT(mStats.mMapped >= mStats.mCommitted);
    return mStats;
  }

 private:
  // A power-of-two that's at least 16 bytes and no more than the size of
  // the cell or metadata, so probably 16 bytes. This "quantum" is
  // used for the object size plus metadata.
  constexpr static base_alloc_size_t kBaseQuantum = mozilla::RoundUpPow2(
      std::max({size_t(16), sizeof(BaseAllocCell), sizeof(BaseAllocMetadata)}));
  constexpr static unsigned kBaseQuantumMask = kBaseQuantum - 1;
  constexpr static unsigned kBaseQuantumLog2 =
      mozilla::CeilingLog2(kBaseQuantum);

  // The minimum possible allocation size.  See get_list_index_for_size().
  constexpr static unsigned kBaseMinimumSize =
      (kCacheLineSize > kBaseQuantum * 2) ? (kCacheLineSize - kBaseQuantum * 2)
                                          : kBaseQuantum;

  // The maximum object size handled by the regular free lists (before
  // deferring to the oversize rbtree).  This should fit arena_t.
  constexpr static base_alloc_size_t kMaxSizeForLists = 4096;
  static_assert(std::has_single_bit(kMaxSizeForLists));

  // There are no more than 3 size classes ber cache line. See
  // get_list_index_for_size().
  constexpr static unsigned kNumFreeLists =
      kMaxSizeForLists / kCacheLineSize *
      std::min(kCacheLineSize / kBaseQuantum, size_t(3));

  static base_alloc_size_t size_round_up(base_alloc_size_t aSize);

  static unsigned get_list_index_for_size(base_alloc_size_t aSize);

  BaseAllocCell* alloc_cell(base_alloc_size_t aSize) MOZ_REQUIRES(mMutex);

  // Allocate from a free list.
  BaseAllocCell* alloc_from_list(base_alloc_size_t aSize) MOZ_REQUIRES(mMutex);

  // Allocate from the oversize tree.
  BaseAllocCell* oversize_alloc(base_alloc_size_t aSize) MOZ_REQUIRES(mMutex);

  // Allocate from the decommitted tree, this will recommit memory as
  // needed.
  BaseAllocCell* decommitted_alloc(base_alloc_size_t aSize)
      MOZ_REQUIRES(mMutex);

  // Remove the cell from its free list.
  void Unlink(BaseAllocCell* cell) MOZ_REQUIRES(mMutex);

  // Add the cell to the free list.
  void Link(BaseAllocCell* cell) MOZ_REQUIRES(mMutex);

  mozilla::DoublyLinkedList<BaseAllocCell>
      mFreeLists[kNumFreeLists] MOZ_GUARDED_BY(mMutex);
  RedBlackTree<BaseAllocCell, BaseAllocCellRBTrait> mFreeListOversize
      MOZ_GUARDED_BY(mMutex);

  // Cells with some memory pages decommitted.
  RedBlackTree<BaseAllocCell, BaseAllocCellRBTrait> mFreeListDecommitted
      MOZ_GUARDED_BY(mMutex);

  // Allocate a new chunk and attempt to split it to return a cell at least
  // minsize.  The other half of the split is added to the appropriate free
  // list.
  BaseAllocCell* chunk_alloc(base_alloc_size_t aSize) MOZ_REQUIRES(mMutex);

  // Attempt to merge any decommitted cells smaller than aSize committing
  // them in the process.  If it forms at least one cell at least aSize
  // large then it returns true.  This is used during allocation before
  // checking the decommitted cells.
  bool merge_decommitted_cells(base_alloc_size_t aSize) MOZ_REQUIRES(mMutex);

  void MaybeTrim(BaseAllocCell* aCell, base_alloc_size_t aSizeRequest,
                 bool aDecommit = false) MOZ_REQUIRES(mMutex);

  Stats mStats MOZ_GUARDED_BY(mMutex);

  friend BaseAllocCell;
};

MFBT_API extern BaseAlloc sBaseAlloc;

// Other classes may inherit from BaseAllocClass to get new and delete
// methods that use the base allocator.
struct BaseAllocClass {
  void* operator new(size_t aSize) noexcept {
    void* ret = sBaseAlloc.alloc(aSize);
    if (!ret) {
      _malloc_message(_getprogname(), ": (malloc) Out of memory\n");
      MOZ_CRASH();
    }
    return ret;
  }
  void* operator new[](size_t aSize) noexcept {
    void* ret = sBaseAlloc.alloc(aSize);
    if (!ret) {
      _malloc_message(_getprogname(), ": (malloc) Out of memory\n");
      MOZ_CRASH();
    }
    return ret;
  }
  void* operator new(size_t aCount, const mozilla::fallible_t&) noexcept {
    return sBaseAlloc.alloc(aCount);
  }
  void* operator new[](size_t aCount, const mozilla::fallible_t&) noexcept {
    return sBaseAlloc.alloc(aCount);
  }

  void operator delete(void* aPtr) { sBaseAlloc.free(aPtr); }
  void operator delete[](void* aPtr) { sBaseAlloc.free(aPtr); }

  size_t MallocSize() { return sBaseAlloc.usable_size(this); }
};

#endif /* ! BASEALLOC_H */
