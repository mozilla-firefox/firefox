/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* arena allocation for the frame tree and closely-related objects */

#include "nsPresArena.h"

#include <inttypes.h>

#include <algorithm>
#include <functional>

#include "js/MemoryMetrics.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/Poison.h"
#include "nsDebug.h"
#include "nsIMemoryReporter.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "nsWindowSizes.h"

using namespace mozilla;

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
nsPresArena<ArenaSize, ObjectId, ObjectIdCount>::~nsPresArena() {
#if defined(MOZ_HAVE_MEM_CHECKS)
  for (FreeList* entry = mFreeLists; entry != std::end(mFreeLists); ++entry) {
    for (void* result : entry->mEntries) {
      MOZ_MAKE_MEM_UNDEFINED(result, entry->mEntrySize);
    }
    entry->mEntries.Clear();
  }
#endif
}

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
void* nsPresArena<ArenaSize, ObjectId, ObjectIdCount>::Allocate(ObjectId aCode,
                                                                size_t aSize) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSize > 0, "PresArena cannot allocate zero bytes");
  MOZ_ASSERT(size_t(aCode) < std::size(mFreeLists));

  // We only hand out aligned sizes
  aSize = mPool.AlignedSize(aSize);

  FreeList* list = &mFreeLists[size_t(aCode)];

  nsTArray<void*>::index_type len = list->mEntries.Length();
  if (list->mEntrySize == 0) {
    MOZ_ASSERT(len == 0, "list with entries but no recorded size");
    list->mEntrySize = aSize;
  } else {
    MOZ_ASSERT(list->mEntrySize == aSize,
               "different sizes for same object type code");
  }

  void* result;
  if (len > 0) {
    // Remove from the end of the mEntries array to avoid memmoving entries,
    // and use SetLengthAndRetainStorage to avoid a lot of malloc/free
    // from ShrinkCapacity on smaller sizes.  500 pointers means the malloc size
    // for the array is 4096 bytes or more on a 64-bit system.  The next smaller
    // size is 2048 (with jemalloc), which we consider not worth compacting.
    result = list->mEntries.Elements()[len - 1];
    if (list->mEntries.Capacity() > 500) {
      list->mEntries.RemoveElementAtUnsafe(len - 1);
    } else {
      list->mEntries.SetLengthAndRetainStorage(len - 1);
    }
#ifdef DEBUG
    {
      MOZ_MAKE_MEM_DEFINED(result, list->mEntrySize);
      char* p = reinterpret_cast<char*>(result);
      char* limit = p + list->mEntrySize;
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
    MOZ_MAKE_MEM_UNDEFINED(result, list->mEntrySize);
  } else {
    // Allocate a new chunk from the arena
    list->mEntriesEverAllocated++;
    result = mPool.Allocate(aSize);
  }

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

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ArenaSize, ObjectId,
                 ObjectIdCount>::AssertNoLiveAllocations() {
#ifdef DEBUG
  MOZ_ASSERT(!mAllocatedPointers || mAllocatedPointers->IsEmpty(),
             "Leaked arena allocations");
  // Stop tracking from here on, so that the set doesn't outlive XPCOM.
  mAllocatedPointers = nullptr;
#endif
}

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ArenaSize, ObjectId, ObjectIdCount>::SortFreeLists() {
  for (FreeList& list : mFreeLists) {
    // Entries are popped from the end, so sort in descending address order.
    std::sort(list.mEntries.begin(), list.mEntries.end(),
              std::greater<void*>());
  }
}

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ArenaSize, ObjectId, ObjectIdCount>::Free(ObjectId aCode,
                                                           void* aPtr) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(size_t(aCode) < std::size(mFreeLists));
#ifdef DEBUG
  if (mAllocatedPointers) {
    MOZ_ASSERT(mAllocatedPointers->Contains(aPtr));
    mAllocatedPointers->Remove(aPtr);
  }
#endif

  // Try to recycle this entry.
  FreeList* list = &mFreeLists[size_t(aCode)];
  MOZ_ASSERT(list->mEntrySize > 0, "object of this type was never allocated");

  mozWritePoison(aPtr, list->mEntrySize);

  MOZ_MAKE_MEM_NOACCESS(aPtr, list->mEntrySize);
  list->mEntries.AppendElement(aPtr);
}

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
void nsPresArena<ArenaSize, ObjectId, ObjectIdCount>::AddSizeOfExcludingThis(
    SizeOfState& aState, nsArenaSizes& aSizes, PresArenaKind aKind) const {
  // We do a complicated dance here because we want to measure the
  // space taken up by the different kinds of objects in the arena,
  // but we don't have pointers to those objects.  And even if we did,
  // we wouldn't be able to use mMallocSizeOf on them, since they were
  // allocated out of malloc'd chunks of memory.  So we compute the
  // size of the arena as known by malloc and we add up the sizes of
  // all the objects that we care about.  Subtracting these two
  // quantities gives us a catch-all "other" number, which includes
  // slop in the arena itself as well as the size of objects that
  // we've not measured explicitly.

  size_t mallocSize = mPool.SizeOfExcludingThis(aState.mMallocSizeOf);

  size_t totalSizeInFreeLists = 0;
  for (const FreeList* entry = mFreeLists; entry != std::end(mFreeLists);
       ++entry) {
    mallocSize += entry->SizeOfExcludingThis(aState.mMallocSizeOf);

    // Note that we're not measuring the size of the entries on the free
    // list here.  The free list knows how many objects we've allocated
    // ever (which includes any objects that may be on the FreeList's
    // |mEntries| at this point) and we're using that to determine the
    // total size of objects allocated with a given ID.
    size_t totalSize = entry->mEntrySize * entry->mEntriesEverAllocated;

    if (aKind == PresArenaKind::Frames) {
      switch (entry - mFreeLists) {
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
      switch (DisplayListArenaObjectId(entry - mFreeLists)) {
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

    totalSizeInFreeLists += totalSize;
  }

  auto& field =
      aKind == PresArenaKind::Frames ? aSizes.mFrames : aSizes.mDisplayList;
  field += mallocSize - totalSizeInFreeLists;
}

template class nsPresArena<8192, ArenaObjectID, eArenaObjectID_COUNT>;
template class nsPresArena<32768, DisplayListArenaObjectId,
                           size_t(DisplayListArenaObjectId::COUNT)>;

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
constinit typename StaticPresArena<ArenaSize, ObjectId, ObjectIdCount>::Arena
    StaticPresArena<ArenaSize, ObjectId, ObjectIdCount>::sInstance;

template class mozilla::StaticPresArena<8192, ArenaObjectID,
                                        eArenaObjectID_COUNT>;
template class mozilla::StaticPresArena<
    32768, DisplayListArenaObjectId, size_t(DisplayListArenaObjectId::COUNT)>;

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

void InitPresArenas() {
  RegisterStrongMemoryReporter(MakeAndAddRef<PresArenaReporter>());
}

void ShutdownPresArenas() {
  FrameArena::AssertNoLiveAllocations();
  DisplayListArena::AssertNoLiveAllocations();
}

}  // namespace mozilla
