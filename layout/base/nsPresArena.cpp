/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* arena allocation for the frame tree and closely-related objects */

#include "nsPresArena.h"

#include <inttypes.h>

#include <algorithm>
#include <bit>

#ifdef XP_WIN
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#include "js/MemoryMetrics.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/Poison.h"
#include "mozilla/Services.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaggedAnonymousMemory.h"
#include "mozilla/mozalloc.h"
#include "nsDebug.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsITimer.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "nsWindowSizes.h"
#include "prsystem.h"

using namespace mozilla;

static constexpr size_t kSuperblockSize = 1024 * 1024;
// Each entry in the page-owner table covers 4 KiB on every platform.
static constexpr size_t kPageMapGranularity = 4096;
static constexpr size_t kMinSlotsPerSlab = 8;
static constexpr size_t kAlignment = 8;
static constexpr uint32_t kPurgeIntervalMs = 10000;

static size_t ArenaPageSize() {
  const size_t size = std::max(kPageMapGranularity, size_t(PR_GetPageSize()));
  MOZ_ASSERT(std::has_single_bit(size) && size < kSuperblockSize);
  return size;
}

// Memory layout
//
// Each arena maps 1 MiB superblocks at 1 MiB boundaries. The first arena page
// holds the canary and the page-owner table. Slabs occupy whole arena pages,
// sized to the OS page size (at least 4 KiB). For example, with 16 KiB pages:
//
//   offset (KiB)    0          16          32                      64
//                   | header   | slab A    | slab B (two pages)    | unused ...
//   mPageOwners     | [0..3]   | [4..7]    | [8..15]               |
//                     unused     all -> A    all -> B
//
// Free() masks the address to find its superblock. Dividing the byte offset
// within the superblock by 4096 gives the mPageOwners index. Each entry for a
// slab points to the same heap-allocated metadata, which survives decommit.
//
// A slab holds fixed-size slots of one type. mBumped is the index of the next
// unused slot. Below it, a set bit in mFreeBits marks a freed, poisoned slot:
//
//   slot index      0       1       2       3 = mBumped
//                   | live  | free  | live  | unused ...
//   mFreeBits         0       1       0       0
//
// Allocation picks the lowest-index slab with space, then its lowest free slot
// or the next unused slot. Purge() can decommit an empty slab and reset its
// bitmap and bump index. Its address range stays reserved for the same type.

static size_t BitmapWords(size_t aBits) { return (aBits + 63) / 64; }

static constexpr size_t kNoBit = SIZE_MAX;

// Returns the index of the first set bit at or after aStart, or kNoBit.
static size_t FindFirstSetBit(Span<const uint64_t> aBits, size_t aStart = 0) {
  for (size_t w = aStart / 64; w < aBits.Length(); ++w) {
    uint64_t bits = aBits[w];
    if (w == aStart / 64) {
      bits &= ~uint64_t(0) << (aStart % 64);
    }
    if (bits) {
      return w * 64 + std::countr_zero(bits);
    }
  }
  return kNoBit;
}

[[maybe_unused]] static bool IsBitSet(Span<const uint64_t> aBits,
                                      size_t aIndex) {
  return aBits[aIndex / 64] & (uint64_t(1) << (aIndex % 64));
}

static void SetBit(Span<uint64_t> aBits, size_t aIndex) {
  aBits[aIndex / 64] |= uint64_t(1) << (aIndex % 64);
}

static void ClearBit(Span<uint64_t> aBits, size_t aIndex) {
  aBits[aIndex / 64] &= ~(uint64_t(1) << (aIndex % 64));
}

namespace mozilla::detail {

struct PresArenaSlabMetadata {
  static PresArenaSlabMetadata* Create(char* aBase, uint32_t aIndex,
                                       uint32_t aSlots) {
    const uint32_t words = BitmapWords(aSlots);
    void* mem =
        moz_xmalloc(sizeof(PresArenaSlabMetadata) + words * sizeof(uint64_t));
    auto* slab = new (mem) PresArenaSlabMetadata(aBase, aIndex, words);
    slab->ClearFreeBits();
    return slab;
  }

  Span<uint64_t> FreeBits() { return Span(mFreeBits, mBitmapWords); }
  void ClearFreeBits() { std::fill_n(mFreeBits, mBitmapWords, 0); }

  char* const mBase;
  const uint32_t mIndex;
  const uint32_t mBitmapWords;
  uint32_t mLive = 0;
  // Number of slots that have been handed out at least once since the slab
  // was (re)committed. Free slots below this index are poisoned, and have their
  // bit set in mFreeBits.
  uint32_t mBumped = 0;
  uint32_t mEmptyEpoch = 0;
  bool mDecommitted = false;
  // Allocated in the same buffer as the slab.
  uint64_t mFreeBits[0];

 private:
  PresArenaSlabMetadata(char* aBase, uint32_t aIndex, uint32_t aBitmapWords)
      : mBase(aBase), mIndex(aIndex), mBitmapWords(aBitmapWords) {}
};

}  // namespace mozilla::detail

using SlabMetadata = mozilla::detail::PresArenaSlabMetadata;

static char* MapPages(size_t aSize) {
#ifdef XP_WIN
  void* base = VirtualAlloc(nullptr, aSize, MEM_RESERVE, PAGE_NOACCESS);
  if (!base) {
#else
  void* base =
      MozTaggedAnonymousMmap(nullptr, aSize, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON, -1, 0, "pres-arena");
  if (base == MAP_FAILED) {
#endif
    NS_ABORT_OOM(aSize);
  }
  return static_cast<char*>(base);
}

static void UnmapPages(void* aBase, size_t aSize) {
#ifdef XP_WIN
  MOZ_RELEASE_ASSERT(VirtualFree(aBase, 0, MEM_RELEASE));
#else
  MOZ_RELEASE_ASSERT(munmap(aBase, aSize) == 0);
#endif
}

static void* MapSuperblock() {
  // First we try to map a single super-block. If that is aligned, we're done.
  char* base = MapPages(kSuperblockSize);
  if (uintptr_t(base) % kSuperblockSize == 0) {
    return base;
  }
  // If not, we need to find a contiguous address range that works for us. So we
  // try with 2 * kSuperblockSize, which guarantees that, then get the address
  // we want, and reserve it explicitly. Note that Windows can only release a
  // whole reservation, so we need to retry if another thread takes the aligned
  // range before we can reserve it.
  UnmapPages(base, kSuperblockSize);

  // This is the address we're going to use from the 2 * kSuperblockSize range.
  auto alignedAddress = [](char* aBase) {
    return aBase + (-uintptr_t(aBase) % kSuperblockSize);
  };
#ifdef XP_WIN
  for (;;) {
    base = MapPages(2 * kSuperblockSize);
    char* aligned = alignedAddress(base);
    UnmapPages(base, 2 * kSuperblockSize);
    if (VirtualAlloc(aligned, kSuperblockSize, MEM_RESERVE, PAGE_NOACCESS)) {
      return aligned;
    }
    if (GetLastError() != ERROR_INVALID_ADDRESS) {
      NS_ABORT_OOM(kSuperblockSize);
    }
  }
#else
  base = MapPages(2 * kSuperblockSize);
  char* aligned = alignedAddress(base);
  // Now unmap the (up to) two regions of memory we don't care about, the prefix
  // [base, aligned), and the suffix (aligned + kSuperblockSize, end].
  size_t prefix = aligned - base;
  if (prefix) {
    UnmapPages(base, prefix);
  }
  UnmapPages(aligned + kSuperblockSize, kSuperblockSize - prefix);
  return aligned;
#endif
}

static void CommitPages(void* aPtr, size_t aSize) {
#ifdef XP_WIN
  if (!VirtualAlloc(aPtr, aSize, MEM_COMMIT, PAGE_READWRITE)) {
    NS_ABORT_OOM(aSize);
  }
#elif defined(XP_DARWIN)
  madvise(aPtr, aSize, MADV_FREE_REUSE);
#endif
}

namespace mozilla::detail {

// A kSuperblockSize-aligned allocation, from whose pages slabs are carved.
struct PresArenaSuperblock {
  static PresArenaSuperblock* Create() {
    void* mem = MapSuperblock();
    CommitPages(mem, ArenaPageSize());
    return new (mem) PresArenaSuperblock();
  }

  static void Destroy(PresArenaSuperblock* aSb) {
    MOZ_MAKE_MEM_UNDEFINED(aSb, kSuperblockSize);
    aSb->~PresArenaSuperblock();
    UnmapPages(aSb, kSuperblockSize);
  }

  static PresArenaSuperblock* For(const void* aPtr) {
    return reinterpret_cast<PresArenaSuperblock*>(uintptr_t(aPtr) &
                                                  ~(kSuperblockSize - 1));
  }

  // The slab that owns the page containing aPtr.
  SlabMetadata*& PageOwner(const void* aPtr) {
    return mPageOwners[(uintptr_t(aPtr) - uintptr_t(this)) /
                       kPageMapGranularity];
  }

  CorruptionCanary mCanary;
  SlabMetadata* mPageOwners[kSuperblockSize / kPageMapGranularity] = {};
};

}  // namespace mozilla::detail

using Superblock = mozilla::detail::PresArenaSuperblock;

static_assert(sizeof(Superblock) <= kPageMapGranularity);

static SlabMetadata* SlabFor(const void* aPtr) {
  return Superblock::For(aPtr)->PageOwner(aPtr);
}

static void DecommitPages(void* aPtr, size_t aSize) {
#if defined(XP_WIN)
  if (!VirtualFree(aPtr, aSize, MEM_DECOMMIT)) {
    MOZ_CRASH("VirtualFree failed");
  }
#elif defined(XP_DARWIN)
  madvise(aPtr, aSize, MADV_FREE_REUSABLE);
#else
  madvise(aPtr, aSize, MADV_DONTNEED);
#endif
}

static void SchedulePresArenaPurge();

template <typename ObjectId, size_t ObjectIdCount>
nsPresArena<ObjectId, ObjectIdCount>::~nsPresArena() {
  for (TypeHeap& heap : mHeaps) {
    for (SlabMetadata* slab : heap.mSlabs) {
      free(slab);
    }
  }
  for (Superblock* sb : mSuperblocks) {
    Superblock::Destroy(sb);
  }
}

template <typename ObjectId, size_t ObjectIdCount>
char* nsPresArena<ObjectId, ObjectIdCount>::AllocatePages(size_t aSize) {
  if (mSuperblocks.IsEmpty() || mNextOffset + aSize > kSuperblockSize) {
    mSuperblocks.AppendElement(Superblock::Create());
    mNextOffset = ArenaPageSize();
  }
  char* result =
      reinterpret_cast<char*>(mSuperblocks.LastElement()) + mNextOffset;
  CommitPages(result, aSize);
  mNextOffset += aSize;
  return result;
}

template <typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ObjectId, ObjectIdCount>::InitHeap(TypeHeap& aHeap,
                                                    size_t aSize) {
  const size_t pageSize = ArenaPageSize();
  MOZ_RELEASE_ASSERT(aSize <= (kSuperblockSize - pageSize) / kMinSlotsPerSlab);
  aHeap.mEntrySize = aSize;
  aHeap.mSlabSize = (kMinSlotsPerSlab * aSize + pageSize - 1) & ~(pageSize - 1);
  aHeap.mSlotsPerSlab = aHeap.mSlabSize / aSize;
}

template <typename ObjectId, size_t ObjectIdCount>
auto nsPresArena<ObjectId, ObjectIdCount>::FindSlabWithSpace(TypeHeap& aHeap)
    -> SlabMetadata* {
  size_t index =
      FindFirstSetBit(aHeap.mSlabsWithSpace, aHeap.mFirstSlabWithSpace);
  if (index != kNoBit) {
    aHeap.mFirstSlabWithSpace = index;
    return aHeap.mSlabs[index];
  }

  index = aHeap.mSlabs.Length();
  SlabMetadata* slab = SlabMetadata::Create(AllocatePages(aHeap.mSlabSize),
                                            index, aHeap.mSlotsPerSlab);
  std::fill_n(&Superblock::For(slab->mBase)->PageOwner(slab->mBase),
              aHeap.mSlabSize / kPageMapGranularity, slab);
  aHeap.mSlabs.AppendElement(slab);
  if (aHeap.mSlabsWithSpace.Length() < BitmapWords(index + 1)) {
    aHeap.mSlabsWithSpace.AppendElement(0);
  }
  SetBit(aHeap.mSlabsWithSpace, index);
  aHeap.mFirstSlabWithSpace = index;
  return slab;
}

template <typename ObjectId, size_t ObjectIdCount>
void* nsPresArena<ObjectId, ObjectIdCount>::Allocate(ObjectId aCode,
                                                     size_t aSize) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSize > 0, "PresArena cannot allocate zero bytes");
  MOZ_ASSERT(size_t(aCode) < std::size(mHeaps));

  aSize = (aSize + kAlignment - 1) & ~(kAlignment - 1);

  TypeHeap& heap = mHeaps[size_t(aCode)];
  if (heap.mEntrySize == 0) {
    InitHeap(heap, aSize);
  } else {
    MOZ_ASSERT(heap.mEntrySize == aSize,
               "different sizes for same object type code");
  }

  SlabMetadata* slab = FindSlabWithSpace(heap);
  if (slab->mDecommitted) {
    CommitPages(slab->mBase, heap.mSlabSize);
    slab->mDecommitted = false;
  } else if (slab->mLive == 0 && slab->mBumped) {
    MOZ_ASSERT(mEmptyCommittedSlabs);
    mEmptyCommittedSlabs--;
  }

  size_t slot = FindFirstSetBit(slab->FreeBits());
  const bool reused = slot != kNoBit;
  if (reused) {
    ClearBit(slab->FreeBits(), slot);
  } else {
    slot = slab->mBumped++;
    MOZ_ASSERT(slot < heap.mSlotsPerSlab);
  }

  if (++slab->mLive == heap.mSlotsPerSlab) {
    ClearBit(heap.mSlabsWithSpace, slab->mIndex);
  }

  void* result = slab->mBase + slot * aSize;

#ifdef DEBUG
  if (reused) {
    MOZ_MAKE_MEM_DEFINED(result, aSize);
    char* p = reinterpret_cast<char*>(result);
    char* limit = p + aSize;
    for (; p < limit; p += sizeof(uintptr_t)) {
      uintptr_t val = *reinterpret_cast<uintptr_t*>(p);
      if (val != mozPoisonValue()) {
        MOZ_ReportAssertionFailure(
            nsPrintfCString("PresArena: poison overwritten; "
                            "wanted %.16" PRIx64 " "
                            "found %.16" PRIx64 " "
                            "errors in bits %.16" PRIx64 " ",
                            uint64_t(mozPoisonValue()), uint64_t(val),
                            uint64_t(mozPoisonValue() ^ val))
                .get(),
            __FILE__, __LINE__);
        MOZ_CRASH();
      }
    }
  }
#endif
  MOZ_MAKE_MEM_UNDEFINED(result, aSize);

#ifdef DEBUG
  if (!mAllocatedPointersInitialized) {
    mAllocatedPointersInitialized = true;
    mAllocatedPointers = std::make_unique<nsTHashSet<void*>>();
  }
  if (mAllocatedPointers && !mAllocatedPointers->Insert(result, fallible)) {
    // Yikes! We're nearly out of memory, and this insertion would've pushed
    // us over the ledge. At this point, we discard & stop using this set,
    // since we don't have enough memory to keep it accurate from this point
    // onwards. Hopefully this helps relieve the memory pressure a bit, too.
    mAllocatedPointers = nullptr;
  }
#endif
  return result;
}

template <typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ObjectId, ObjectIdCount>::AssertNoLiveAllocations() {
#ifdef DEBUG
  MOZ_ASSERT(!mAllocatedPointers || mAllocatedPointers->IsEmpty(),
             "Leaked arena allocations");
  // Stop tracking from here on, so that the set doesn't outlive XPCOM.
  mAllocatedPointers = nullptr;
#endif
}

template <typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ObjectId, ObjectIdCount>::Check() {
  if (!mSuperblocks.IsEmpty()) {
    mSuperblocks.LastElement()->mCanary.Check();
  }
}

template <typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ObjectId, ObjectIdCount>::Free(ObjectId aCode, void* aPtr) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(size_t(aCode) < std::size(mHeaps));
#ifdef DEBUG
  if (mAllocatedPointers) {
    MOZ_ASSERT(mAllocatedPointers->Contains(aPtr));
    mAllocatedPointers->Remove(aPtr);
  }
#endif

  TypeHeap& heap = mHeaps[size_t(aCode)];
  MOZ_ASSERT(heap.mEntrySize > 0, "object of this type was never allocated");

  SlabMetadata* slab = SlabFor(aPtr);
  MOZ_DIAGNOSTIC_ASSERT(
      slab->mIndex < heap.mSlabs.Length() && heap.mSlabs[slab->mIndex] == slab,
      "Freeing object into the wrong type heap");
  const size_t offset = static_cast<char*>(aPtr) - slab->mBase;
  const size_t slot = offset / heap.mEntrySize;
  MOZ_ASSERT(offset % heap.mEntrySize == 0);
  MOZ_ASSERT(slot < slab->mBumped);

  mozWritePoison(aPtr, heap.mEntrySize);
  MOZ_MAKE_MEM_NOACCESS(aPtr, heap.mEntrySize);

  MOZ_ASSERT(!IsBitSet(slab->FreeBits(), slot), "Double free");
  SetBit(slab->FreeBits(), slot);

  if (slab->mLive == heap.mSlotsPerSlab) {
    SetBit(heap.mSlabsWithSpace, slab->mIndex);
  }
  heap.mFirstSlabWithSpace = std::min(heap.mFirstSlabWithSpace, slab->mIndex);

  if (--slab->mLive == 0) {
    slab->mEmptyEpoch = mEpoch;
    mEmptyCommittedSlabs++;
    SchedulePresArenaPurge();
  }
}

template <typename ObjectId, size_t ObjectIdCount>
bool nsPresArena<ObjectId, ObjectIdCount>::Purge(bool aAll) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mEmptyCommittedSlabs) {
    for (TypeHeap& heap : mHeaps) {
      for (SlabMetadata* slab : heap.mSlabs) {
        if (slab->mLive || slab->mDecommitted || !slab->mBumped ||
            (!aAll && slab->mEmptyEpoch == mEpoch)) {
          continue;
        }
        DecommitPages(slab->mBase, heap.mSlabSize);
        MOZ_MAKE_MEM_NOACCESS(slab->mBase, heap.mSlabSize);
        slab->mDecommitted = true;
        slab->mBumped = 0;
        slab->ClearFreeBits();
        mEmptyCommittedSlabs--;
      }
    }
  }
  mEpoch++;
  return mEmptyCommittedSlabs;
}

template <typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ObjectId, ObjectIdCount>::AddSizeOfExcludingThis(
    SizeOfState& aState, nsArenaSizes& aSizes, PresArenaKind aKind) const {
  // Committed slab memory is attributed to the object type that owns the slab.
  // Everything else that is committed (superblock headers, slab metadata, free
  // slot bitmaps) is reported as overhead. Decommitted slabs and never-used
  // superblock pages are not reported.
  size_t overhead =
      mSuperblocks.ShallowSizeOfExcludingThis(aState.mMallocSizeOf);
  overhead += mSuperblocks.Length() * ArenaPageSize();

  for (const TypeHeap* heap = mHeaps; heap != std::end(mHeaps); ++heap) {
    overhead += heap->mSlabs.ShallowSizeOfExcludingThis(aState.mMallocSizeOf);
    overhead +=
        heap->mSlabsWithSpace.ShallowSizeOfExcludingThis(aState.mMallocSizeOf);

    size_t totalSize = 0;
    for (const SlabMetadata* slab : heap->mSlabs) {
      overhead += aState.mMallocSizeOf(slab);
      if (!slab->mDecommitted) {
        totalSize += heap->mSlabSize;
      }
    }

    if (aKind == PresArenaKind::Frames) {
      switch (heap - mHeaps) {
#define PRES_ARENA_OBJECT(name_)                     \
  case eArenaObjectID_##name_:                       \
    aSizes.NS_ARENA_SIZES_FIELD(name_) += totalSize; \
    break;
#include "nsPresArenaObjectList.inc"
#undef PRES_ARENA_OBJECT
        default:
          MOZ_ASSERT_UNREACHABLE("Unknown arena object type");
      }
    } else {
      MOZ_ASSERT(aKind == PresArenaKind::DisplayList);
      switch (DisplayListArenaObjectId(heap - mHeaps)) {
#define DISPLAY_LIST_ARENA_OBJECT(name_)             \
  case DisplayListArenaObjectId::name_:              \
    aSizes.NS_ARENA_SIZES_FIELD(name_) += totalSize; \
    break;
#include "nsDisplayListArenaTypes.inc"
#undef DISPLAY_LIST_ARENA_OBJECT
        default:
          MOZ_ASSERT_UNREACHABLE("Unknown display item arena type");
      }
    }
  }

  auto& field =
      aKind == PresArenaKind::Frames ? aSizes.mFrames : aSizes.mDisplayList;
  field += overhead;
}

template class nsPresArena<ArenaObjectID, eArenaObjectID_COUNT>;
template class nsPresArena<DisplayListArenaObjectId,
                           size_t(DisplayListArenaObjectId::COUNT)>;

template <typename ObjectId, size_t ObjectIdCount>
constinit typename StaticPresArena<ObjectId, ObjectIdCount>::Arena
    StaticPresArena<ObjectId, ObjectIdCount>::sInstance;

template class mozilla::StaticPresArena<ArenaObjectID, eArenaObjectID_COUNT>;
template class mozilla::StaticPresArena<
    DisplayListArenaObjectId, size_t(DisplayListArenaObjectId::COUNT)>;

static StaticRefPtr<nsITimer> sPurgeTimer;
static bool sPurgeTimerArmed = false;
static bool sPurgeShutdown = false;

static bool PurgePresArenas(bool aAll) {
  bool frames = FrameArena::Purge(aAll);
  bool displayList = DisplayListArena::Purge(aAll);
  return frames || displayList;
}

static void PurgeTimerCallback(nsITimer*, void*) {
  sPurgeTimerArmed = false;
  if (PurgePresArenas(/* aAll = */ false)) {
    SchedulePresArenaPurge();
  }
}

static void SchedulePresArenaPurge() {
  if (sPurgeTimerArmed || sPurgeShutdown) {
    return;
  }
  if (!sPurgeTimer) {
    sPurgeTimer = NS_NewTimer();
    if (!sPurgeTimer) {
      return;
    }
  }
  sPurgeTimerArmed = NS_SUCCEEDED(sPurgeTimer->InitWithNamedFuncCallback(
      PurgeTimerCallback, nullptr, kPurgeIntervalMs,
      nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY, "PresArenaPurge"_ns));
}

namespace mozilla {

MOZ_DEFINE_MALLOC_SIZE_OF(PresArenaMallocSizeOf)

class PresArenaReporter final : public nsIMemoryReporter {
  ~PresArenaReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    SizeOfState state(PresArenaMallocSizeOf);
    nsArenaSizes sizes;
    FrameArena::AddSizeOf(state, sizes, PresArenaKind::Frames);
    DisplayListArena::AddSizeOf(state, sizes, PresArenaKind::DisplayList);

    // There are many different kinds of frames, but it is very likely
    // that only a few matter.  Implement a cutoff so we don't bloat
    // about:memory with many uninteresting entries.
    const size_t sundriesThreshold = js::MemoryReportingSundriesThreshold();

    auto report = [&](const nsACString& aPath, size_t aAmount,
                      const nsACString& aDesc) {
      aHandleReport->Callback(""_ns, aPath, KIND_HEAP, UNITS_BYTES, aAmount,
                              aDesc, aData);
    };

    size_t presArenaSundries = 0;
#define PRES_ARENA_OBJECT(name_)                               \
  {                                                            \
    size_t size = sizes.NS_ARENA_SIZES_FIELD(name_);           \
    if (size < sundriesThreshold) {                            \
      presArenaSundries += size;                               \
    } else {                                                   \
      report("explicit/layout/pres-arena/" #name_ ""_ns, size, \
             "Memory used by objects of type " #name_          \
             " within the pres arena."_ns);                    \
    }                                                          \
  }
#include "nsPresArenaObjectList.inc"
#undef PRES_ARENA_OBJECT

    if (presArenaSundries) {
      report("explicit/layout/pres-arena/sundries"_ns, presArenaSundries,
             "The sum of all memory used by objects in the pres arena which "
             "were too small to be shown individually."_ns);
    }

    report("explicit/layout/pres-arena/overhead"_ns, sizes.mFrames,
           "Memory used by the pres arena that isn't attributed to any "
           "particular kind of object."_ns);

    size_t displayListArenaSundries = 0;
#define DISPLAY_LIST_ARENA_OBJECT(name_)                               \
  {                                                                    \
    size_t size = sizes.NS_ARENA_SIZES_FIELD(name_);                   \
    if (size < sundriesThreshold) {                                    \
      displayListArenaSundries += size;                                \
    } else {                                                           \
      report("explicit/layout/display-list-arena/" #name_ ""_ns, size, \
             "Memory used by objects of type " #name_                  \
             " within the display list arena."_ns);                    \
    }                                                                  \
  }
#include "nsDisplayListArenaTypes.inc"
#undef DISPLAY_LIST_ARENA_OBJECT

    if (displayListArenaSundries) {
      report("explicit/layout/display-list-arena/sundries"_ns,
             displayListArenaSundries,
             "The sum of all memory used by objects in the display list "
             "arena which were too small to be shown individually."_ns);
    }

    report("explicit/layout/display-list-arena/overhead"_ns, sizes.mDisplayList,
           "Memory used by the display list arena that isn't attributed to "
           "any particular kind of object."_ns);

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(PresArenaReporter, nsIMemoryReporter)

class PresArenaMemoryPressureObserver final : public nsIObserver {
  ~PresArenaMemoryPressureObserver() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD Observe(nsISupports*, const char* aTopic,
                     const char16_t*) override {
    MOZ_ASSERT(!strcmp(aTopic, "memory-pressure"));
    PurgePresArenas(/* aAll = */ true);
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(PresArenaMemoryPressureObserver, nsIObserver)

void InitPresArenas() {
  RegisterStrongMemoryReporter(MakeAndAddRef<PresArenaReporter>());
  if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
    obs->AddObserver(new PresArenaMemoryPressureObserver(), "memory-pressure",
                     false);
  }
}

void ShutdownPresArenas() {
  sPurgeShutdown = true;
  if (sPurgeTimer) {
    sPurgeTimer->Cancel();
    sPurgeTimer = nullptr;
  }
  FrameArena::AssertNoLiveAllocations();
  DisplayListArena::AssertNoLiveAllocations();
}

}  // namespace mozilla
