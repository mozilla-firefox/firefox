/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "BaseAlloc.h"

#include <cstring>

#include "mozilla/Saturate.h"

#include "Globals.h"
#include "FdPrintf.h"

using namespace mozilla;

// Change this to 1 to enable some BaseAlloc logging. Useful for debugging.
#define BASE_ALLOC_LOGGING 0

// Change this to 1 to enable expensive assertions beyond normal debug
// builds.
#define BASE_ALLOC_VALIDATION 0

#if BASE_ALLOC_VALIDATION
bool TreeContains(RedBlackTree<BaseAllocCell, BaseAllocCellRBTrait>& aTree,
                  BaseAllocCell* aCell) {
  BaseAllocCell* cur = aTree.SearchOrNext(aCell->Size());
  while (cur) {
    if (cur == aCell) {
      return true;
    }

    if (cur->Size() != aCell->Size()) {
      return false;
    }
    cur = aTree.Next(cur);
  }

  return false;
}
#endif

// By using a macro "Log" won't collide with PHC's Log function in unified
// builds.
#if BASE_ALLOC_LOGGING
#  define Log BaseLog
static void BaseLog(const char* fmt, ...);
#else
#  define Log(...)
#endif

constinit BaseAlloc sBaseAlloc;

uintptr_t BaseAllocCell::Align(uintptr_t aPtr) {
  // In addition to assuming that kBaseQuantum, the cache line size and page
  // size are all powers of two.  We also assume that the quantum, cache
  // line size, and page size are each greater than the previous one.
  // Together these assumptions imply that each is a multiple of the
  // previous one.
  static_assert(BaseAlloc::kBaseQuantum <= kCacheLineSize);
  MOZ_ASSERT(kCacheLineSize <= gPageSize);

  uintptr_t address =
      ALIGNMENT_CEILING(aPtr, uintptr_t(BaseAlloc::kBaseQuantum));

  uintptr_t cache_line = address & ~uintptr_t(kCacheLineMask);

  if (cache_line + BaseAlloc::kBaseQuantum < address) {
    // This address would result in cells that share a cache line, move it
    // forward to the next cache line.
    address = cache_line + kCacheLineSize;
  }

  MOZ_ASSERT(aPtr <= address);
  MOZ_ASSERT((address % alignof(BaseAllocCell)) == 0);

  return address;
}

// Initialize base allocation data structures.
void BaseAlloc::Init() MOZ_REQUIRES(gInitLock) { mMutex.Init(); }

base_alloc_size_t BaseAlloc::size_round_up(base_alloc_size_t aSize) {
  return ALIGNMENT_CEILING(aSize, kBaseQuantum);
}

unsigned BaseAlloc::get_list_index_for_size(base_alloc_size_t aSize) {
  if constexpr (kBaseQuantum * 2 >= kCacheLineSize) {
    return aSize / kBaseQuantum - 1;
  } else {
    // The lambda template prevents the C++ compiler from checking this
    // branch when it's not used. This is used to avoid a compiler warning
    // when kBaseQuantum * == kCacheLineSize.
    return []<typename T>(T aSize) -> unsigned {
      // The base allocator will allocate all objects on their own
      // cache line, but if kBaseQuantum is less than two times smaller than
      // kCacheLineSize, then some object sizes are impossible, they're
      // always rounded up to ensure the next object begins on a cache line
      // boundary.  Naively this would lead to 1-in-4 free lists being
      // wasted (on x86_64) because no object will be created that size.
      // Instead the following code calculates the list index for a given
      // size.
      //
      // For any cache line multiple there are 3 possible sizes they are:
      //  + cache_multiple,
      //  + cache_multiple - kBaseQuantum
      //  + cache_multiple - kBaseQuantum*2
      //
      // The code here will map them to indexes for the free list array.

      // The minimum possible size is kBaseMinimumSize.  So start by
      // enforcing that using a saturating subtraction so that the minimum
      // becomes 0.
      aSize = (SaturateUint32(aSize) - kBaseMinimumSize).value();

      // After that subtraction dividing by the cache line size gives us
      // the group of 3 this size is in.
      unsigned cache_line = aSize / kCacheLineSize;

      // Find the remainder,
      unsigned offset = (aSize % kCacheLineSize) / kBaseQuantum;

      // Remainders 0, 1 and 2 are valid.  But any other remainder won't map
      // to a valid size, round up to the valid size.
      //
      // With an exception for offset = 3, the expression in the return
      // statement below will produce the same result for offset=3 wheather
      // we enter this branch or not so we can skip it in that case.
      if (offset > 3) {
        cache_line++;
        offset = 0;
      }

      // Find the index into the free list array.
      return cache_line * 3 + offset;
    }(aSize);
  }
}

BaseAllocMetadata* BaseAllocCell::RightMetadata() {
  uintptr_t ptr = reinterpret_cast<uintptr_t>(this) + Size() +
                  BaseAlloc::kBaseQuantum - sizeof(BaseAllocMetadata);

  MOZ_ASSERT((ptr % alignof(BaseAllocMetadata)) == 0);
  return reinterpret_cast<BaseAllocMetadata*>(ptr);
}

void BaseAlloc::free(void* aPtr) MOZ_EXCLUDES(mMutex) {
  if (aPtr == nullptr) {
    return;
  }

  // base_chunk_dealloc must run outside mMutex: chunk_record allocates an
  // extent_node_t via BaseAlloc::alloc which would re-acquire it.
  void* chunkToDealloc = nullptr;
  size_t chunkSizeToDealloc = 0;

  {
    MutexAutoLock lock(mMutex);

    BaseAllocCell* cell = BaseAllocCell::GetCell(aPtr);

    // Zero the contents of the memory cell before we add it to a free list.
    // Otherwise the DoublyLinkedList code will hit an assertion because it
    // looks like it's already in a list.
    cell->ClearPayload();
    cell->SetFreed();

    Log("free(%p), size: %u\n", aPtr, cell->Size());

    // Attempt to merge backwards
    BaseAllocCell* left = cell->LeftCell();
    if (left && !left->Allocated() && left->Committed()) {
      Unlink(left);
      left->Merge(cell);
      cell = left;
    }
    // And forward
    BaseAllocCell* right = cell->RightCell();
    if (right && !right->Allocated() && right->Committed()) {
      Unlink(right);
      cell->Merge(right);
    }

    if (cell->Size() >= kChunkSize && !cell->RightCell() && !cell->LeftCell()) {
      // The cell covers a whole chunk and can be completely released.
      uintptr_t addr = reinterpret_cast<uintptr_t>(cell) & ~gRealPageSizeMask;
      size_t size = REAL_PAGE_CEILING(cell->Size());
      Log("Releasing entire chunk %p, size %d", addr, size);
      chunkToDealloc = reinterpret_cast<void*>(addr);
      chunkSizeToDealloc = size;
      mStats.mCommitted -= size;
      mStats.mMapped -= size;
    } else {
      Link(cell);
    }
  }

  if (chunkToDealloc) {
    base_chunk_dealloc(chunkToDealloc, chunkSizeToDealloc, UNKNOWN_CHUNK);
  }
}

void* BaseAlloc::alloc(size_t aSize) {
  aSize = size_round_up(aSize);

  // Allocations cannot exceed sizes greater than BASE_ALLOC_SIZE_MAX which
  // is required by BaseAlloc's heap structure.  We assert but also return
  // null for builds without assertions.
  MOZ_ASSERT(aSize <= BASE_ALLOC_SIZE_MAX);
  if (aSize > BASE_ALLOC_SIZE_MAX) {
    return nullptr;
  }

  MutexAutoLock lock(mMutex);

  BaseAllocCell* cell = alloc_cell(aSize);
  if (cell) {
    MOZ_ASSERT(cell->Size() >= aSize);
    cell->SetAllocated();
    return cell->Ptr();
  }

  return nullptr;
}

BaseAllocCell* BaseAlloc::alloc_cell(base_alloc_size_t aSize) {
  BaseAllocCell* cell = alloc_from_list(aSize);
  if (cell) {
    Log("alloc(%u) = %p (from free list)\n", aSize, cell);
    return cell;
  }

  cell = oversize_alloc(aSize);
  if (cell) {
    Log("alloc(%u) = %p (from oversize)\n", aSize, cell);
    return cell;
  }

  // Try to merge decommitted cells with their committed neighbours until a
  // cell of at least aSize is created.
  if (merge_decommitted_cells(aSize)) {
    cell = oversize_alloc(aSize);
    if (cell) {
      Log("alloc(%u) = %p (from oversize after merging decommitted cells)\n",
          aSize, cell);
      return cell;
    }
  }

  cell = decommitted_alloc(aSize);
  if (cell) {
    Log("alloc(%u) = %p (from decommitted)\n", aSize, cell);
    return cell;
  }

  cell = chunk_alloc(aSize);
  if (cell) {
    Log("alloc(%u) = %p (from new chunk)\n", aSize, cell);
    return cell;
  }

  Log("alloc(%u) failed\n", aSize);
  return nullptr;
}

BaseAllocCell* BaseAlloc::alloc_from_list(base_alloc_size_t aSize) {
  unsigned start_index = get_list_index_for_size(aSize);
  for (unsigned i = start_index; i < kNumFreeLists; i++) {
    if (!mFreeLists[i].isEmpty()) {
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(mFreeLists[i].ListIsWellFormed());
#endif
      BaseAllocCell* cell = mFreeLists[i].popFront();
      MaybeTrim(cell, aSize);

      return cell;
    }
  }
  return nullptr;
}

BaseAllocCell* BaseAlloc::oversize_alloc(base_alloc_size_t aSize) {
  // Search for the best fit in the oversize tree.
  BaseAllocCell* cell = mFreeListOversize.SearchOrNext(aSize);
  if (cell) {
    mFreeListOversize.Remove(cell);

    MaybeTrim(cell, aSize);

    return cell;
  }

  return nullptr;
}

void BaseAlloc::Unlink(BaseAllocCell* cell) {
  MOZ_ASSERT(!cell->Allocated());

  if (cell->Committed()) {
    unsigned index = get_list_index_for_size(cell->Size());
    if (index < kNumFreeLists) {
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(mFreeLists[index].ListIsWellFormed());
      MOZ_ASSERT(mFreeLists[index].contains(cell));
#endif
      mFreeLists[index].remove(cell);
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(mFreeLists[index].ListIsWellFormed());
#endif
    } else {
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(TreeContains(mFreeListOversize, cell));
#endif
      mFreeListOversize.Remove(cell);
    }
  } else {
#if BASE_ALLOC_VALIDATION
    MOZ_ASSERT(TreeContains(mFreeListDecommitted, cell));
#endif
    mFreeListDecommitted.Remove(cell);
  }
}

void BaseAlloc::Link(BaseAllocCell* cell) {
  MOZ_ASSERT(!cell->Allocated());

  // the size must conform to our classes/free lists.
  MOZ_ASSERT(cell->Size() == size_round_up(cell->Size()));

  if (cell->Committed()) {
    unsigned index = get_list_index_for_size(cell->Size());
    // If a larger size would not place this entry into a different list
    // then this size is "illegal".
    MOZ_ASSERT(get_list_index_for_size(cell->Size() + kBaseQuantum) ==
               index + 1);
    if (index < kNumFreeLists) {
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(mFreeLists[index].ListIsWellFormed());
      MOZ_ASSERT(!mFreeLists[index].contains(cell));
      MOZ_ASSERT(cell->ProbablyNotInList());
#endif
      mFreeLists[index].pushFront(cell);
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(mFreeLists[index].ListIsWellFormed());
#endif
    } else {
#if BASE_ALLOC_VALIDATION
      MOZ_ASSERT(!TreeContains(mFreeListOversize, cell));
      MOZ_ASSERT(cell->ProbablyNotInList());
#endif
      mFreeListOversize.Insert(cell);
    }
  } else {
#if BASE_ALLOC_VALIDATION
    MOZ_ASSERT(!TreeContains(mFreeListDecommitted, cell));
    MOZ_ASSERT(cell->ProbablyNotInList());
#endif
    mFreeListDecommitted.Insert(cell);
  }
}

bool BaseAlloc::merge_decommitted_cells(base_alloc_size_t aSize) {
  // This might commit and merge multiple cells before creating one large
  // enough to satisfy the allocation.  Which may commit more memory than
  // necessary, but it's better than fragmentation.

  // The while loop and for loop are used together.  The for loop iterates
  // over the tree but if the code modifies the tree it needs to be
  // restarted, which is what the while loop is for - restarting that
  // iteration.
  //
  // After each item the code will either:
  //  * return true because it found a cell large enough,
  //  * possibly after
  //    merging)
  //  * return false because of an error committing memory.
  //  * Not be able to perform a merge with that cell and will go to the
  //    next cell in the tree.
  //  * Perform a merge, `break` and the while loop are used to restart the
  //    for loop.
  //  * return false because the entire tree was checked.
  bool restart;
  do {
    restart = false;
    // mFreeListDecommitted is sorted from smallest to largest so this will
    // attempt to merge smaller cells first.
    for (BaseAllocCell* cell : mFreeListDecommitted.iter()) {
      if (cell->Size() >= aSize) {
        // This cell is already large enough.  But this shouldn't happen
        // because oversize_alloc() failed before merge_decommitted_cells()
        // was called.
        return true;
      }

      BaseAllocCell* left = cell->LeftCell();
      if (left && !left->Allocated()) {
        // After unlink we can't use the iterator anymore, one way or
        // another code here must break the for loop.
        Unlink(cell);
        size_t change = cell->CommitAll();
        if (change == 0) {
          Link(cell);
          return false;
        }
        mStats.mCommitted += change;

        Unlink(left);
        if (!left->Committed()) {
          change = left->CommitAll();
          if (change == 0) {
            Link(left);
            return false;
          }
          mStats.mCommitted += change;
        }
        left->Merge(cell);
        Link(left);
        if (left->Size() >= aSize) {
          return true;
        }
        // Break the for loop restarting from the while loop.
        restart = true;
        break;
      }

      BaseAllocCell* right = cell->RightCell();
      if (right && !right->Allocated()) {
        Unlink(cell);
        size_t change = cell->CommitAll();
        if (change == 0) {
          Link(cell);
          return false;
        }
        mStats.mCommitted += change;

        Unlink(right);
        if (!right->Committed()) {
          change = right->CommitAll();
          if (change == 0) {
            Link(right);
            return false;
          }
          mStats.mCommitted += change;
        }
        cell->Merge(right);
        Link(cell);
        if (cell->Size() >= aSize) {
          return true;
        }
        restart = true;
        break;
      }
    }
  } while (restart);

  return false;
}

BaseAllocCell* BaseAlloc::chunk_alloc(base_alloc_size_t aSize)
    MOZ_REQUIRES(mMutex) {
  // aSize should be non-zero and aligned already.
  MOZ_ASSERT(aSize != 0);
  MOZ_ASSERT(aSize == size_round_up(aSize));

  // Make room for the metadata on either side of this cell and round up to
  // the chunk size.
  size_t csize = CHUNK_CEILING(kBaseQuantum * 2 + aSize);
  // Find the largest cell that fits within the chunk.
  base_alloc_size_t net_size = csize - kBaseQuantum * 2;
  MOZ_ASSERT(net_size >= aSize);

  void* base_pages = base_chunk_alloc(csize, kChunkSize);
  if (base_pages == 0) {
    return nullptr;
  }
  mStats.mCommitted += csize;
  mStats.mMapped += csize;

  BaseAllocCell* cell =
      new (reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(base_pages) +
                                   kBaseQuantum)) BaseAllocCell(net_size);
  MaybeTrim(cell, aSize, true);

  return cell;
}

BaseAllocCell* BaseAlloc::decommitted_alloc(base_alloc_size_t aSize) {
  BaseAllocCell* cell = mFreeListDecommitted.SearchOrNext(aSize);
  if (!cell) {
    return nullptr;
  }
  mFreeListDecommitted.Remove(cell);

  auto result = cell->Commit(aSize);
  if (!result) {
    mFreeListDecommitted.Insert(cell);
    return nullptr;
  }

  mStats.mCommitted += result->mChange;
  if (result->mNewCell1) {
    Link(result->mNewCell1);
  }
  if (result->mNewCell2) {
    Link(result->mNewCell2);
  }

  MaybeTrim(cell, aSize);

  return cell;
}

void* BaseAlloc::calloc(size_t aNumber, size_t aSize) {
  void* ret = alloc(aNumber * aSize);
  if (ret) {
    memset(ret, 0, aNumber * aSize);
  }
  return ret;
}

void* BaseAlloc::realloc(void* aPtr, size_t aNewSize) {
  if (aNewSize == 0) {
    free(aPtr);
    return nullptr;
  }

  if (aPtr == nullptr) {
    return alloc(aNewSize);
  }

  BaseAllocCell* cell = reinterpret_cast<BaseAllocCell*>(aPtr);
  size_t old_size = cell->Size();

  aNewSize = size_round_up(aNewSize);
  if (aNewSize < old_size) {
    // Shrinking
    MutexAutoLock lock(mMutex);

    MaybeTrim(cell, aNewSize);
    MOZ_ASSERT(cell->Size() >= aNewSize);
    Log("realloc %p (size %u) shrink to %u\n", cell, old_size, cell->Size());
    return cell->Ptr();
  } else if (aNewSize > old_size) {
    // Growing
    {
      MutexAutoLock lock(mMutex);

      BaseAllocCell* right = cell->RightCell();

      // See if this cell's neighour is free and large enough that we can
      // merge
      if (right && !right->Allocated() && right->Committed() &&
          (cell->Size() + kBaseQuantum + right->Size()) >= aNewSize) {
        Unlink(right);
        cell->Merge(right);

        // The new cell might be bigger than necessary.
        MaybeTrim(cell, aNewSize);
        MOZ_ASSERT(cell->Size() >= aNewSize);

        Log("realloc %p (size %u) grow in-place to %u\n", cell, old_size,
            cell->Size());
        MOZ_ASSERT(cell->Allocated());
        return cell->Ptr();
      }
    }  // Unlock mMutex

    // Moving realloc.
    Log("realloc beginning...\n");
    BaseAllocCell* new_cell = reinterpret_cast<BaseAllocCell*>(alloc(aNewSize));
    if (!new_cell) {
      return nullptr;
    }
    memcpy(new_cell->Ptr(), cell->Ptr(), old_size);
    free(cell);
    Log("...realloc %p (size %u) grow to %p (sizx %u)\n", cell, old_size,
        new_cell, new_cell->Size());
    return new_cell->Ptr();
  }

  // The cell stays the same size.
  MOZ_ASSERT(cell->Size() >= aNewSize);
  Log("realloc %p (size %u) no-op\n", cell, cell->Size());
  return cell->Ptr();
}

size_t BaseAlloc::usable_size(void* aPtr) {
  return reinterpret_cast<BaseAllocCell*>(aPtr)->Size();
}

void BaseAllocCell::SetSize(base_alloc_size_t aSize) {
  MOZ_ASSERT(aSize == BaseAlloc::size_round_up(aSize));

  // Set the left metadata's size first so it can be used to get the
  // right metadata's address.
  LeftMetadata()->mRightSize = aSize;

  // Now it's safe to set the right metadata's size.  Note that both the
  // old-right metadata, and the new metadata's right size are left untouched.
  RightMetadata()->mLeftSize = aSize;
}

void BaseAllocCell::ClearPayload() {
  memset(&mListElem, 0, sizeof(mListElem));
  mCommitted = true;
}

BaseAllocCell* BaseAllocCell::LeftCell() {
  base_alloc_size_t left_cell_size = LeftMetadata()->mLeftSize;
  if (!left_cell_size) {
    return nullptr;
  }

  BaseAllocCell* left = reinterpret_cast<BaseAllocCell*>(
      reinterpret_cast<uintptr_t>(this) - BaseAlloc::kBaseQuantum -
      left_cell_size);

  MOZ_ASSERT(left->RightMetadata() == LeftMetadata());

  return left;
}

BaseAllocCell* BaseAllocCell::RightCell() {
  base_alloc_size_t right_size = RightMetadata()->mRightSize;
  if (right_size == 0) {
    return nullptr;
  }

  BaseAllocCell* right = reinterpret_cast<BaseAllocCell*>(RightCellRaw());

  MOZ_ASSERT(RightMetadata() == right->LeftMetadata());

  return right;
}

uintptr_t BaseAllocCell::RightCellRaw() {
  return reinterpret_cast<uintptr_t>(this) + Size() + BaseAlloc::kBaseQuantum;
}

void BaseAllocCell::Merge(BaseAllocCell* aOther) {
  // aOther must be after this, we can check by comparing what they each
  // think their metadata is.
  MOZ_ASSERT(RightMetadata() == aOther->LeftMetadata());
  base_alloc_size_t new_size =
      Size() + aOther->Size() + BaseAlloc::kBaseQuantum;

  Log("Merge %p (size %u) with %p (size %u) -> size %u\n", this, Size(), aOther,
      aOther->Size(), new_size);

#ifdef MOZ_DEBUG
  BaseAllocMetadata* right_metadata = aOther->RightMetadata();
#endif
  // Check for overflow.
  MOZ_ASSERT(new_size > this->Size() && new_size > aOther->Size());

  BaseAllocMetadata* old_metadata = RightMetadata();
  SetSize(new_size);

  MOZ_ASSERT(RightMetadata() == right_metadata);

  // Clearing the old metadata may make debugging easier.
  old_metadata->Clear();
}

uintptr_t BaseAllocCell::CanSplit(base_alloc_size_t aSizeReq) {
  if (aSizeReq + BaseAlloc::kBaseQuantum + sizeof(BaseAllocCell) >= Size()) {
    // Insufficient size.
    return 0;
  }

  // Rather than use the requested size directly for the first cell, start
  // with the requested size then align the next cell and check if it still
  // leaves enough room after alignment.

  uintptr_t next_addr = Align(reinterpret_cast<uintptr_t>(this) + aSizeReq +
                              sizeof(BaseAllocMetadata));

  if (next_addr + BaseAlloc::kBaseMinimumSize >
      reinterpret_cast<uintptr_t>(RightMetadata())) {
    return 0;
  }

  return next_addr;
}

void BaseAlloc::MaybeTrim(BaseAllocCell* aCell, base_alloc_size_t aSizeRequest,
                          bool aDecommit) {
  uintptr_t new_addr = aCell->CanSplit(aSizeRequest);
  if (!new_addr) {
    return;
  }

  BaseAllocCell* next = aCell->Split(new_addr);
  MOZ_ASSERT(next);

  if (aDecommit && (next->Size() >= kDecommitThreshold)) {
    auto result = next->Decommit();
    mStats.mCommitted -= result.mChange;
    if (result.mNewCell1) {
      Link(result.mNewCell1);
    }
    if (result.mNewCell2) {
      Link(result.mNewCell2);
    }
  }

  Link(next);
}

bool BaseAllocCell::CanSplitHere(uintptr_t aNextAddr) {
  MOZ_ASSERT(Align(aNextAddr) == aNextAddr);

  if (Align(reinterpret_cast<uintptr_t>(this) + BaseAlloc::kBaseQuantum +
            sizeof(BaseAllocMetadata)) > aNextAddr) {
    // Not enough size for metadata before the beginning of the new cell.
    return false;
  }

  if (aNextAddr + BaseAlloc::kBaseQuantum >
      reinterpret_cast<uintptr_t>(this) + Size()) {
    // Not enough size in the new cell.
    return false;
  }

  return true;
}

BaseAllocCell* BaseAllocCell::Split(uintptr_t aNewAddr) {
#ifdef MOZ_DEBUG
  BaseAllocMetadata* last_metadata = RightMetadata();
#endif
  base_alloc_size_t old_size = Size();
  base_alloc_size_t new_size =
      aNewAddr - BaseAlloc::kBaseQuantum - reinterpret_cast<uintptr_t>(this);
  SetSize(new_size);

  // This must use NextCellRaw and cast the result, using NextCell would run
  // assertions that would fail.
  BaseAllocCell* right = new (reinterpret_cast<BaseAllocCell*>(RightCellRaw()))
      BaseAllocCell(old_size - new_size - BaseAlloc::kBaseQuantum);

  Log("Split %p (size %u) -> (size %u) and %p (size %u)\n", this, old_size,
      Size(), right, right->Size());

  // Prove that the alignment code above is correct.
  MOZ_ASSERT(new_size == BaseAlloc::size_round_up(new_size));
  MOZ_ASSERT(right->Size() == BaseAlloc::size_round_up(right->Size()));
  MOZ_ASSERT(this->RightMetadata() == right->LeftMetadata());
  MOZ_ASSERT(right->RightMetadata() == last_metadata);

  return right;
}

BaseAllocCell::DeCommitResult BaseAllocCell::Decommit() {
  // Decommit pages within the "next" chunk.
  uintptr_t start = REAL_PAGE_CEILING(reinterpret_cast<uintptr_t>(this) +
                                      sizeof(BaseAllocCell));
  uintptr_t end = REAL_PAGE_FLOOR(reinterpret_cast<uintptr_t>(RightMetadata()));
  if (start >= end) {
    return DeCommitResult(0);
  }

  uintptr_t nbytes = end - start;

  // Try to split this cell so that more of the resident memory is usable.
  uintptr_t boundary = Align(end + BaseAlloc::kBaseQuantum);
  BaseAllocCell* end_cell = CanSplitHere(boundary) ? Split(boundary) : nullptr;

  boundary = Align(start - kCacheLineSize + BaseAlloc::kBaseQuantum);
  BaseAllocCell* cell = CanSplitHere(boundary) ? Split(boundary) : nullptr;

  if (cell) {
    cell->DoDecommit(start, nbytes);
  } else {
    DoDecommit(start, nbytes);
  }

  return DeCommitResult(nbytes, cell, end_cell);
}

void BaseAllocCell::DoDecommit(uintptr_t aFirstDecommit, uintptr_t aNBytes) {
  MOZ_ASSERT(reinterpret_cast<uintptr_t>(this) + sizeof(BaseAllocCell) <=
             aFirstDecommit);
  MOZ_ASSERT(aFirstDecommit + aNBytes <=
             reinterpret_cast<uintptr_t>(this) + Size());

  pages_decommit(reinterpret_cast<void*>(aFirstDecommit), aNBytes);
  mCommitted = false;

  Log("Decommitting in cell %p: %p - %p, %zu bytes\n", this, aFirstDecommit,
      aFirstDecommit + aNBytes, aNBytes);
}

Maybe<BaseAllocCell::DeCommitResult> BaseAllocCell::Commit(
    base_alloc_size_t aSizeReq) {
  MOZ_ASSERT(!mCommitted);
  MOZ_ASSERT(Size() >= aSizeReq);

  // The address after the last decommitted byte.
  uintptr_t last_decommitted =
      REAL_PAGE_FLOOR(reinterpret_cast<uintptr_t>(RightMetadata()));

  // The first currently-decommitted address.
  uintptr_t first_decommitted = REAL_PAGE_CEILING(
      reinterpret_cast<uintptr_t>(this) + sizeof(BaseAllocCell));

  MOZ_ASSERT(first_decommitted < last_decommitted);

  // A partly decommitted cell will require at least sizeof(BaseAllocCell)
  // bytes of its payload in committed memory.  But it also needs to be
  // properly aligned so that its payload isn't in the cache line of the
  // previous cell.  The minimum committed bytes of a decommitted cell is:
  base_alloc_size_t min_committed_bytes =
      std::max(base_alloc_size_t(kCacheLineSize) - BaseAlloc::kBaseQuantum,
               BaseAlloc::kBaseQuantum);

  // The end of the range that needs to be committed.
  // PAGE_CEILING(Align(this + aSizeReq + quantum)) is the lowest address
  // that may be decommitted and still satisfy an allocation on aSizeReq.
  // But because the cell to the right also needs to have the fields of
  // BaseAllocCell within committed memory then we need to add
  // min_committed_bytes before rounding up to the page boundary.
  uintptr_t new_first_decommitted =
      REAL_PAGE_CEILING(Align(reinterpret_cast<uintptr_t>(this) + aSizeReq +
                              BaseAlloc::kBaseQuantum) +
                        min_committed_bytes);

  // new_first_decommitted may be after last_decommitted when aSizeReq is large
  // enough that the committed memory at the end of the cell is also required to
  // satisfy the allocation.  But it will never be larger than the page
  // after the payload fo the next cell.
  MOZ_ASSERT(new_first_decommitted <=
             REAL_PAGE_CEILING(RightCellRaw() + sizeof(BaseAllocCell)));
  new_first_decommitted = std::min(new_first_decommitted, last_decommitted);

  MOZ_ASSERT(first_decommitted <= new_first_decommitted);

  if (first_decommitted == new_first_decommitted) {
    // Nothing needs committing to satisfy the allocation since it can be
    // satisfied from the first part of the cell.  This shouldn't happen
    // because the cell should have been split when it was decommitted.
    uintptr_t split_addr = CanSplit(aSizeReq);
    if (split_addr == 0) {
      return Nothing();
    }
    MOZ_ASSERT(split_addr < first_decommitted);
    BaseAllocCell* cell = Split(split_addr);
    mCommitted = true;
    cell->mCommitted = false;
    return Some(DeCommitResult(0, cell));
  }

  bool whole_cell = new_first_decommitted == last_decommitted;
  Log("Committing %s cell %p: %p - %p, %zu bytes\n",
      whole_cell ? "whole" : "part", this, first_decommitted,
      new_first_decommitted, new_first_decommitted - first_decommitted);

  // Do the commit before the split so that the new boundary is writable.
  if (!pages_commit(reinterpret_cast<void*>(first_decommitted),
                    new_first_decommitted - first_decommitted)) {
    return Nothing();
  }
  mCommitted = true;

  if (whole_cell) {
    return Some(DeCommitResult(new_first_decommitted - first_decommitted));
  }

  BaseAllocCell* cell = Split(new_first_decommitted - min_committed_bytes);
  cell->mCommitted = false;

  return Some(DeCommitResult(new_first_decommitted - first_decommitted, cell));
}

size_t BaseAllocCell::CommitAll() {
  Maybe<BaseAllocCell::DeCommitResult> commit_res = Commit(Size());
  if (!commit_res) {
    return 0;
  }
  MOZ_ASSERT(!commit_res->mNewCell1);
  MOZ_ASSERT(!commit_res->mNewCell2);
  return commit_res->mChange;
}

#if BASE_ALLOC_LOGGING
static size_t GetPid() { return size_t(getpid()); }

static void BaseLog(const char* fmt, ...) {
#  ifdef _WIN32
#    define LOG_STDERR GetStdHandle(STD_ERROR_HANDLE)
#  else
#    define LOG_STDERR 2
#  endif

  char buf[256];
  size_t pos = SNPrintf(buf, sizeof(buf), "BaseAlloc[%zu] ", GetPid());
  va_list vargs;
  va_start(vargs, fmt);
  pos += VSNPrintf(&buf[pos], sizeof(buf) - pos, fmt, vargs);
  MOZ_ASSERT(pos < sizeof(buf));
  va_end(vargs);

  FdPuts(LOG_STDERR, buf, pos);
}
#endif  // BASE_ALLOC_LOGGING

#undef Log
