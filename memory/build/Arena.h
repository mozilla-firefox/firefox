/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef ARENA_H
#define ARENA_H

#include "mozilla/Atomics.h"
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/fallible.h"
#include "mozilla/XorShift128PlusRNG.h"

#include "mozjemalloc_types.h"
#include "mozjemalloc_profiling.h"

#include "ArenaAvailRuns.h"
#include "Constants.h"
#include "Chunk.h"
#include "Globals.h"
#include "RedBlackTree.h"

// ***************************************************************************
// Statistics data structures.

struct arena_stats_t {
  // Number of bytes currently mapped.
  size_t mapped = 0;

  // Current number of committed pages (non madvised/decommitted)
  size_t committed = 0;

  // Per-size-category statistics.
  size_t allocated_small = 0;

  size_t allocated_large = 0;

  // The number of "memory operations" aka mallocs/frees.
  uint64_t operations = 0;
};

// Describe size classes to which allocations are rounded up to.
// TODO: add large and huge types when the arena allocation code
// changes in a way that allows it to be beneficial.
class SizeClass {
 public:
  enum ClassType {
    Quantum,
    QuantumWide,
    SubPage,
    Large,
  };

  explicit SizeClass(size_t aSize) {
    // We can skip an extra condition here if aSize > 0 and kQuantum >=
    // kMinQuantumClass.
    MOZ_ASSERT(aSize > 0);
    static_assert(kQuantum >= kMinQuantumClass);

    if (aSize <= kMaxQuantumClass) {
      mType = Quantum;
      mSize = QUANTUM_CEILING(aSize);
    } else if (aSize <= kMaxQuantumWideClass) {
      mType = QuantumWide;
      mSize = QUANTUM_WIDE_CEILING(aSize);
    } else if (aSize <= mozilla::gMaxSubPageClass) {
      mType = SubPage;
      mSize = SUBPAGE_CEILING(aSize);
    } else if (aSize <= mozilla::gMaxLargeClass) {
      mType = Large;
      mSize = PAGE_CEILING(aSize);
    } else {
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Invalid size");
    }
  }

  SizeClass& operator=(const SizeClass& aOther) = default;

  bool operator==(const SizeClass& aOther) { return aOther.mSize == mSize; }

  size_t Size() { return mSize; }

  ClassType Type() { return mType; }

  SizeClass Next() { return SizeClass(mSize + 1); }

 private:
  ClassType mType;
  size_t mSize;
};

// ***************************************************************************
// Arena data structures.

struct arena_bin_t;

namespace mozilla {

#ifdef MALLOC_DOUBLE_PURGE
struct MadvisedChunkListTrait {
  static DoublyLinkedListElement<arena_chunk_t>& Get(arena_chunk_t* aThis) {
    return aThis->mChunksMadvisedElement;
  }
  static const DoublyLinkedListElement<arena_chunk_t>& Get(
      const arena_chunk_t* aThis) {
    return aThis->mChunksMadvisedElement;
  }
};
#endif
}  // namespace mozilla

enum class purge_action_t {
  None,
  PurgeNow,
  Queue,
};

struct arena_run_t {
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  uint32_t mMagic;
#  define ARENA_RUN_MAGIC 0x384adf93

  // On 64-bit platforms, having the arena_bin_t pointer following
  // the mMagic field means there's padding between both fields, making
  // the run header larger than necessary.
  // But when MOZ_DIAGNOSTIC_ASSERT_ENABLED is not set, starting the
  // header with this field followed by the arena_bin_t pointer yields
  // the same padding. We do want the mMagic field to appear first, so
  // depending whether MOZ_DIAGNOSTIC_ASSERT_ENABLED is set or not, we
  // move some field to avoid padding.

  // Number of free regions in run.
  unsigned mNumFree;
#endif

  // Used by arena_bin_t::mNonFullRuns.
  mozilla::DoublyLinkedListElement<arena_run_t> mRunListElem;

  // Bin this run is associated with.
  arena_bin_t* mBin;

  // Index of first element that might have a free region.
  unsigned mRegionsMinElement;

#if !defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  // Number of free regions in run.
  unsigned mNumFree;
#endif

  // Bitmask of in-use regions (0: in use, 1: free).
  unsigned mRegionsMask[];  // Dynamically sized.
};

namespace mozilla {

template <>
struct GetDoublyLinkedListElement<arena_run_t> {
  static DoublyLinkedListElement<arena_run_t>& Get(arena_run_t* aThis) {
    return aThis->mRunListElem;
  }
  static const DoublyLinkedListElement<arena_run_t>& Get(
      const arena_run_t* aThis) {
    return aThis->mRunListElem;
  }
};

}  // namespace mozilla

struct arena_bin_t {
  // We use a LIFO ("last-in-first-out") policy to refill non-full runs.
  //
  // This has the following reasons:
  // 1. It is cheap, as all our non-full-runs' book-keeping is O(1), no
  //    tree-balancing or walking is needed.
  // 2. It also helps to increase the probability for CPU cache hits for the
  //    book-keeping and the reused slots themselves, as the same memory was
  //    most recently touched during free, especially when used from the same
  //    core (or via the same shared cache, depending on the architecture).
  mozilla::DoublyLinkedList<arena_run_t> mNonFullRuns;

  // Bin's size class.
  size_t mSizeClass;

  // Total number of regions in a run for this bin's size class.
  uint32_t mRunNumRegions;

  // Number of elements in a run's mRegionsMask for this bin's size class.
  uint32_t mRunNumRegionsMask;

  // Offset of first region in a run for this bin's size class.
  uint32_t mRunFirstRegionOffset;

  // Current number of runs in this bin, full or otherwise.
  uint32_t mNumRuns = 0;

  // A constant for fast division by size class.  This value is 16 bits wide so
  // it is placed last.
  FastDivisor<uint16_t> mSizeDivisor;

  // Total number of pages in a run for this bin's size class.
  uint8_t mRunSizePages;

  // Amount of overhead runs are allowed to have.
  static constexpr double kRunOverhead = 1.6_percent;
  static constexpr double kRunRelaxedOverhead = 2.4_percent;

  // Initialize a bin for the given size class.
  // The generated run sizes, for a page size of 4 KiB, are:
  //   size|run       size|run       size|run       size|run
  //  class|size     class|size     class|size     class|size
  //     4   4 KiB      8   4 KiB     16   4 KiB     32   4 KiB
  //    48   4 KiB     64   4 KiB     80   4 KiB     96   4 KiB
  //   112   4 KiB    128   8 KiB    144   4 KiB    160   8 KiB
  //   176   4 KiB    192   4 KiB    208   8 KiB    224   4 KiB
  //   240   8 KiB    256  16 KiB    272   8 KiB    288   4 KiB
  //   304  12 KiB    320  12 KiB    336   4 KiB    352   8 KiB
  //   368   4 KiB    384   8 KiB    400  20 KiB    416  16 KiB
  //   432  12 KiB    448   4 KiB    464  16 KiB    480   8 KiB
  //   496  20 KiB    512  32 KiB    768  16 KiB   1024  64 KiB
  //  1280  24 KiB   1536  32 KiB   1792  16 KiB   2048 128 KiB
  //  2304  16 KiB   2560  48 KiB   2816  36 KiB   3072  64 KiB
  //  3328  36 KiB   3584  32 KiB   3840  64 KiB
  explicit arena_bin_t(SizeClass aSizeClass);
};

// We try to keep the above structure aligned with common cache lines sizes,
// often that's 64 bytes on x86 and ARM, we don't make assumptions for other
// architectures.
#if defined(__x86_64__) || defined(__aarch64__)
// On 64bit platforms this structure is often 48 bytes
// long, which means every other array element will be properly aligned.
static_assert(sizeof(arena_bin_t) == 48);
#elif defined(__x86__) || defined(__arm__)
static_assert(sizeof(arena_bin_t) == 32);
#endif

enum PurgeCondition { PurgeIfThreshold, PurgeUnconditional };

struct arena_t : public BaseAllocClass {
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
#  define ARENA_MAGIC 0x947d3d24
  uint32_t mMagic = ARENA_MAGIC;
#endif

  // Linkage for the tree of arenas by id.
  // This just provides the memory to be used by the collection tree
  // and thus needs no arena_t::mLock.
  RedBlackTreeNode<arena_t> mLink;

  // Arena id, that we keep away from the beginning of the struct so that
  // free list pointers in the base allocator don't overflow in it,
  // and it keeps the value it had after the destructor.
  arena_id_t mId = 0;

  // Operations on this arena require that lock be locked. The MaybeMutex
  // class will elude locking if the arena is accessed from a single thread
  // only (currently only the main thread can be used like this).
  // Can be acquired while holding gArenas.mLock, but must not be acquired or
  // held while holding or acquiring gArenas.mPurgeListLock.
  MaybeMutex mLock MOZ_UNANNOTATED;

  // The lock is required to write to fields of mStats, but it is not needed to
  // read them, so long as inconsistents reads are okay (fields might not make
  // sense together).
  arena_stats_t mStats MOZ_GUARDED_BY(mLock);

  // We can read the allocated counts from mStats without a lock:
  size_t AllocatedBytes() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
    return mStats.allocated_small + mStats.allocated_large;
  }

  // We can read the operations field from mStats without a lock:
  uint64_t Operations() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
    return mStats.operations;
  }

 private:
  // Queue of dirty-page-containing chunks this arena manages.  Generally it is
  // operated in FIFO order, chunks are purged from the beginning of the list
  // and newly-dirtied chunks are placed at the end.  We assume that this makes
  // finding larger runs of dirty pages easier, it probably doesn't affect the
  // chance that a new allocation has a page fault since that is controlled by
  // the order of mRunsAvail.
  mozilla::DoublyLinkedList<arena_chunk_t, mozilla::DirtyChunkListTrait>
      mChunksDirty MOZ_GUARDED_BY(mLock);

#ifdef MALLOC_DOUBLE_PURGE
  // Head of a linked list of MADV_FREE'd-page-containing chunks this
  // arena manages.
  mozilla::DoublyLinkedList<arena_chunk_t, mozilla::MadvisedChunkListTrait>
      mChunksMAdvised MOZ_GUARDED_BY(mLock);
#endif

  // A per-arena cache of recently used but now empty chunks.
  //
  // Currently it may have 0, 1 or briefly 2 members.
  //
  // It is a list of chunks that operate as a cache (for small and large
  // allocations only), and a mechanism to delay releasing memory until idle
  // time.`
  //
  // Spare chunks will not appear in mChunksDirty and cannot be in purging
  // (they are removed from mSpares when purging begins).  Their pages are
  // counted in mNumDirty and other counters.
  mozilla::DoublyLinkedList<arena_chunk_t, mozilla::DirtyChunkListTrait> mSpares
      MOZ_GUARDED_BY(mLock);

  // A per-arena opt-in to randomize the offset of small allocations
  // Needs no lock, read-only.
  bool mRandomizeSmallAllocations;

  // A pseudorandom number generator. Initially null, it gets initialized
  // on first use to avoid recursive malloc initialization (e.g. on OSX
  // arc4random allocates memory).
  mozilla::non_crypto::XorShift128PlusRNG* mPRNG MOZ_GUARDED_BY(mLock) =
      nullptr;
  bool mIsPRNGInitializing MOZ_GUARDED_BY(mLock) = false;

 public:
  // Whether this is a private arena. Multiple public arenas are just a
  // performance optimization and not a safety feature.
  //
  // Since, for example, we don't want thread-local arenas to grow too much, we
  // use the default arena for bigger allocations. We use this member to allow
  // realloc() to switch out of our arena if needed (which is not allowed for
  // private arenas for security).
  // Needs no lock, read-only.
  bool mIsPrivate;

  // Current count of pages within unused runs that are potentially
  // dirty, and for which madvise(... MADV_FREE) has not been called.  By
  // tracking this, we can institute a limit on how much dirty unused
  // memory is mapped for each arena.
  size_t mNumDirty MOZ_GUARDED_BY(mLock) = 0;

  // Precalculated value for faster checks.
  size_t mMaxDirty MOZ_GUARDED_BY(mLock);

  // The current number of pages that are available without a system call (but
  // probably a page fault).
  size_t mNumMAdvised MOZ_GUARDED_BY(mLock) = 0;
  size_t mNumFresh MOZ_GUARDED_BY(mLock) = 0;

  // Maximum value allowed for mNumDirty.
  // Needs no lock, read-only.
  size_t mMaxDirtyBase;

  // Needs no lock, read-only.
  int32_t mMaxDirtyIncreaseOverride = 0;
  int32_t mMaxDirtyDecreaseOverride = 0;

  // The link to gArenas.mOutstandingPurges.
  // Note that this must only be accessed while holding gArenas.mPurgeListLock
  // (but not arena_t.mLock !) through gArenas.mOutstandingPurges.
  mozilla::DoublyLinkedListElement<arena_t> mPurgeListElem;

  // A page reuse is when a dirty page is used for a new allocation, it has the
  // CHUNK_MAP_DIRTY bit cleared and CHUNK_MAP_ALLOCATED set.
  //
  // Timestamp of the last time we saw a page reuse (in ns).
  // Note that this variable is written very often from many threads and read
  // only sparsely on the main thread, but when we read it we need to see the
  // chronologically latest write asap (so we cannot use Relaxed).
  mozilla::Atomic<uint64_t> mLastPageReuseNS;

 public:
  // A flag that indicates if arena will be Purge()'d.
  //
  // It is set either when a thread commits to adding it to mOutstandingPurges
  // or when imitating a Purge.  Cleared only by Purge when we know we are
  // completely done.  This is used to avoid accessing the list (and list lock)
  // on every call to ShouldStartPurge() and to avoid deleting arenas that
  // another thread is purging.
  bool mIsPurgePending MOZ_GUARDED_BY(mLock) = false;

  // A mirror of ArenaCollection::mIsDeferredPurgeEnabled, here only to
  // optimize memory reads in ShouldStartPurge().
  bool mIsDeferredPurgeEnabled MOZ_GUARDED_BY(mLock) = false;

  // True if the arena is in the process of being destroyed, and needs to be
  // released after a concurrent purge completes.
  bool mMustDeleteAfterPurge MOZ_GUARDED_BY(mLock) = false;

  // mLabel describes the label for the firefox profiler.  It's stored in a
  // fixed size area including a null terminating byte.  The actual maximum
  // length of the string is one less than LABEL_MAX_CAPACITY;
  static constexpr size_t LABEL_MAX_CAPACITY = 128;
  char mLabel[LABEL_MAX_CAPACITY] = {};

  // Chunk allocator used for all of this arena's allocations.
  chunk_allocator_t* mChunkAllocator;

 private:
  // Collection of this arena's available runs.  This is used for
  // first-best-fit run allocation.
  ArenaAvailRuns mRunsAvail MOZ_GUARDED_BY(mLock);

 public:
  // mBins is used to store rings of free regions of the following sizes,
  // assuming a 16-byte quantum, 4kB pagesize, and default MALLOC_OPTIONS.
  //
  //  | mBins[i] | size |
  //  +----------+------+
  //  |       0  |    2 |
  //  |       1  |    4 |
  //  |       2  |    8 |
  //  +----------+------+
  //  |       3  |   16 |
  //  |       4  |   32 |
  //  |       5  |   48 |
  //  |       6  |   64 |
  //  |          :      :
  //  |          :      :
  //  |      33  |  496 |
  //  |      34  |  512 |
  //  +----------+------+
  //  |      35  |  768 |
  //  |      36  | 1024 |
  //  |          :      :
  //  |          :      :
  //  |      46  | 3584 |
  //  |      47  | 3840 |
  //  +----------+------+
  arena_bin_t mBins[] MOZ_GUARDED_BY(mLock);  // Dynamically sized.

  explicit arena_t(arena_params_t* aParams, bool aIsPrivate);
  ~arena_t();

  void ResetSmallAllocRandomization();

  void InitPRNG() MOZ_REQUIRES(mLock);

 private:
  void InitChunk(arena_chunk_t* aChunk, size_t aMinCommittedPages)
      MOZ_REQUIRES(mLock);

  // Remove the chunk from the arena.  This removes it from all the page counts.
  // It assumes its run has already been removed.  The chunk must not be
  // in either mSpares or mChunksDirty, or be actively purging.
  void RemoveChunk(arena_chunk_t* aChunk) MOZ_REQUIRES(mLock);

  void DemoteChunkToSpare(arena_chunk_t* aChunk) MOZ_REQUIRES(mLock);

  // Try to merge the run with its neighbours. Returns the new index of the run
  // (since it may have merged with an earlier one).
  size_t TryCoalesce(arena_chunk_t* aChunk, size_t run_ind, size_t run_pages,
                     size_t size) MOZ_REQUIRES(mLock);

  arena_run_t* AllocRun(size_t aSize, bool aLarge, bool aZero)
      MOZ_REQUIRES(mLock);

  void DallocRun(arena_run_t* aRun, bool aDirty) MOZ_REQUIRES(mLock);

#ifndef MALLOC_DECOMMIT
  // Mark an madvised page as dirty, this is required when a allocating a
  // neighbouring page that is part of the same real page.
  void TouchMadvisedPage(arena_chunk_t* aChunk, size_t aPage)
      MOZ_REQUIRES(mLock);
#endif

  // Split an unallocated run into two parts, allocate the first part and
  // make the 2nd part available for future allocations.
  //
  // Before calling:
  //   aRun must not be allocated or available for allocation in mAvailRuns,
  //   it may be fresh, decommitted, dirty etc.
  // On return:
  //   aRun is not in mAvailRuns, the caller may immediately use it.  It
  //   will be marked as allocated, and not dirty/decommitted etc.
  //
  //   The other half of the original run will be added to mAvailRuns, it
  //   may have been partially un-decommitted (MALLOC_DECOMMIT) or touched
  //   (when gPageSize < gRealPageSize).
  //
  // This can only fail if committing memory failed.
  //
  [[nodiscard]] bool SplitAndAllocRun(arena_run_t* aRun, size_t aSize,
                                      bool aLarge, bool aZero)
      MOZ_REQUIRES(mLock);

  void TrimRunHead(arena_chunk_t* aChunk, arena_run_t* aRun, size_t aOldSize,
                   size_t aNewSize) MOZ_REQUIRES(mLock);

  void TrimRunTail(arena_chunk_t* aChunk, arena_run_t* aRun, size_t aOldSize,
                   size_t aNewSize, bool dirty) MOZ_REQUIRES(mLock);

  arena_run_t* GetNewEmptyBinRun(arena_bin_t* aBin) MOZ_REQUIRES(mLock);

  inline arena_run_t* GetNonFullBinRun(arena_bin_t* aBin) MOZ_REQUIRES(mLock);

  inline uint8_t FindFreeBitInMask(uint32_t aMask, uint32_t& aRng)
      MOZ_REQUIRES(mLock);

  inline void* ArenaRunRegAlloc(arena_run_t* aRun, arena_bin_t* aBin)
      MOZ_REQUIRES(mLock);

  inline void* MallocSmall(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* MallocLarge(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* MallocHuge(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* PallocLarge(size_t aAlignment, size_t aSize, size_t aAllocSize)
      MOZ_EXCLUDES(mLock);

  void* PallocHuge(size_t aSize, size_t aAlignment, bool aZero)
      MOZ_EXCLUDES(mLock);

  void RallocShrinkLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                         size_t aOldSize) MOZ_EXCLUDES(mLock);

  bool RallocGrowLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                       size_t aOldSize) MOZ_EXCLUDES(mLock);

  void* RallocSmallOrLarge(void* aPtr, size_t aSize, size_t aOldSize)
      MOZ_EXCLUDES(mLock);

  void* RallocHuge(void* aPtr, size_t aSize, size_t aOldSize)
      MOZ_EXCLUDES(mLock);

 public:
  inline void* Malloc(size_t aSize, bool aZero) MOZ_EXCLUDES(mLock);

  void* Palloc(size_t aAlignment, size_t aSize) MOZ_EXCLUDES(mLock);

  inline void DallocSmall(arena_chunk_t* aChunk, void* aPtr,
                          arena_chunk_map_t* aMapElm) MOZ_REQUIRES(mLock);

  void DallocLarge(arena_chunk_t* aChunk, void* aPtr) MOZ_REQUIRES(mLock);

  void* Ralloc(void* aPtr, size_t aSize, size_t aOldSize) MOZ_EXCLUDES(mLock);

  void UpdateMaxDirty() MOZ_EXCLUDES(mLock);

#ifdef MALLOC_DECOMMIT
  // During a commit operation (for aReqPages) we have the opportunity of
  // commiting at most aRemPages additional pages.  How many should we commit to
  // amortise system calls?
  size_t ExtraCommitPages(size_t aReqPages, size_t aRemainingPages)
      MOZ_REQUIRES(mLock);
#endif

  // Purge some dirty pages.
  //
  // When this is called the caller has already tested ShouldStartPurge()
  // (possibly on another thread asychronously) or is passing
  // PurgeUnconditional.  However because it's called without the lock it will
  // recheck ShouldContinuePurge() before doing any work.
  //
  // It may purge a number of runs within a single chunk before returning.  It
  // will return Continue if there's more work to do in other chunks
  // (ShouldContinuePurge()).
  //
  // To release more pages from other chunks then it's best to call Purge
  // in a loop, looping when it returns Continue.
  //
  // This must be called without the mLock held (it'll take the lock).
  //
  ArenaPurgeResult Purge(PurgeCondition aCond, mozilla::PurgeStats& aStats,
                         purge_keep_going_t aKeepGoing = nullptr,
                         void* aClosure = nullptr) MOZ_EXCLUDES(mLock);

  // Run Purge() in a loop. If sCallback is non-null then collect statistics and
  // publish them through the callback,  aCaller should be used to identify the
  // caller in the profiling data.
  //
  // aCond         - when to stop purging
  // aCaller       - a string representing the caller, this is used for
  //                 profiling
  // aReuseGraceMS - Stop purging the arena if it was used within this many
  //                 milliseconds.  Or 0 to ignore recent reuse.
  // aKeepGoing    - Optional predicate, purging stops when it returns false.
  // aClosure      - Handed back to aKeepGoing on every call.
  //
  ArenaPurgeResult PurgeLoop(PurgeCondition aCond, const char* aCaller,
                             uint32_t aReuseGraceMS = 0,
                             purge_keep_going_t aKeepGoing = nullptr,
                             void* aClosure = nullptr) MOZ_EXCLUDES(mLock);

  class PurgeInfo {
   private:
    // The dirty memory begins at mDirtyInd and is mDirtyLen pages long.
    // However it may have clean memory within it.
    size_t mDirtyInd = 0;
    size_t mDirtyLen = 0;

    // mDirtyNPages is the actual number of dirty pages within the span above.
    size_t mDirtyNPages = 0;

    // This is the run containing the dirty memory, the entire run is
    // unallocated.
    size_t mFreeRunInd = 0;
    size_t mFreeRunLen = 0;

   public:
    arena_t& mArena;

    arena_chunk_t* mChunk = nullptr;

   private:
    mozilla::PurgeStats& mPurgeStats;

   public:
    size_t FreeRunLenBytes() const {
      return mFreeRunLen << mozilla::gPageSize2Pow;
    }

    // The last index of the free run.
    size_t FreeRunLastInd() const { return mFreeRunInd + mFreeRunLen - 1; }

    void* DirtyPtr() const {
      return (void*)(uintptr_t(mChunk) + (mDirtyInd << mozilla::gPageSize2Pow));
    }

    size_t DirtyLenBytes() const { return mDirtyLen << mozilla::gPageSize2Pow; }

    // Purging memory is seperated into 3 phases.
    //  * FindDirtyPages() which find the dirty pages in a chunk and marks the
    //    run and chunk as busy while holding the lock.
    //  * Release the pages (without the lock)
    //  * UpdatePagesAndCounts() which marks the dirty pages as not-dirty and
    //    updates other counters (while holding the lock).
    //
    // FindDirtyPages() will return false purging should not continue purging in
    // this chunk.  Either because it has no dirty pages or is dying.
    bool FindDirtyPages(bool aPurgedOnce) MOZ_REQUIRES(mArena.mLock);

    // This is used internally by FindDirtyPages to actually perform scanning
    // within a chunk's page tables.  It finds the first dirty page within the
    // chunk.
    bool ScanForFirstDirtyPage() MOZ_REQUIRES(mArena.mLock);

    // After ScanForFirstDirtyPage() returns true, this may be used to find the
    // last dirty page within the same run.
    bool ScanForLastDirtyPage() MOZ_REQUIRES(mArena.mLock);

    // Returns true if there are more dirty pages remaining in the current
    // chunk.
    bool UpdatePagesAndCounts() MOZ_REQUIRES(mArena.mLock);

    // FinishPurgingInChunk() is used whenever we decide to stop purging in a
    // chunk, This could be because there are no more dirty pages, or we hit
    // the arena-level threshold.
    void FinishPurgingInChunk(bool aAddToMAdvised, bool aAddToDirty)
        MOZ_REQUIRES(mArena.mLock);

    explicit PurgeInfo(arena_t& arena, arena_chunk_t* chunk,
                       mozilla::PurgeStats& stats)
        : mArena(arena), mChunk(chunk), mPurgeStats(stats) {}
  };

 private:
  arena_chunk_t* PurgeGetSpareChunk(mozilla::PurgeStats& aStats);
  arena_chunk_t* PurgeGetDirtyChunk(PurgeCondition aCond,
                                    mozilla::PurgeStats& aStats);

  ArenaPurgeResult PurgeDirtyPages(arena_chunk_t* aChunk, PurgeCondition aCond,
                                   mozilla::PurgeStats& aStats,
                                   purge_keep_going_t aKeepGoing,
                                   void* aClosure);

 public:
  // If there are more than one spare chunks, then remove and return the one
  // from the back of the list.
  arena_chunk_t* GetExcessSpareChunk();

  void HardPurge();

  // Check mNumDirty against EffectiveMaxDirty and return the appropriate
  // action to be taken by MayDoOrQueuePurge (outside mLock's scope).
  //
  // None:     Nothing to do.
  // PurgeNow: Immediate synchronous purge.
  // Queue:    Add a new purge request.
  //
  // Note that in the case of deferred purge this function takes into account
  // mIsDeferredPurgeNeeded to avoid useless operations on the purge list
  // that would require gArenas.mPurgeListLock.
  inline purge_action_t ShouldStartPurge() MOZ_REQUIRES(mLock);

  // Take action according to ShouldStartPurge.
  inline void MayDoOrQueuePurge(purge_action_t aAction, const char* aCaller)
      MOZ_EXCLUDES(mLock);

  // Check the EffectiveHalfMaxDirty threshold to decide if we continue purge.
  // This threshold is lower than ShouldStartPurge to have some hysteresis.
  bool ShouldContinuePurge(PurgeCondition aCond) MOZ_REQUIRES(mLock) {
    return (mNumDirty > ((aCond == PurgeUnconditional) ? 0 : mMaxDirty >> 1));
  }

  // Update the last page reuse timestamp.
  void NotifyPageReuse() MOZ_EXCLUDES(mLock);

  bool IsMainThreadOnly() const { return !mLock.LockIsEnabled(); }

  // Overload new to customise the size.
  void* operator new(size_t aCount, const mozilla::fallible_t&) noexcept;

  // Fallible allocation is unused and an array of arena_t is impossible.
  void* operator new(size_t aCount) noexcept = delete;
  void* operator new[](size_t aCount) noexcept = delete;
  void* operator new[](size_t aCount,
                       const mozilla::fallible_t&) noexcept = delete;
  void operator delete[](void* aPtr) = delete;
};

#endif /* ! ARENA_H */
