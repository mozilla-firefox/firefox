/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* arena allocation for the frame tree and closely-related objects */

#ifndef nsPresArena_h_
#define nsPresArena_h_

#include <memory>

#include "mozilla/ArenaAllocator.h"
#include "mozilla/ArenaObjectID.h"
#include "mozilla/DisplayListArenaObjectId.h"
#include "mozilla/MemoryReporting.h"
#include "nsDisplayItemTypes.h"
#include "nsQueryFrame.h"
#include "nsTArray.h"
#include "nsTHashSet.h"

struct nsArenaSizes;
namespace mozilla {
class SizeOfState;
enum class PresArenaKind { Frames, DisplayList };
}  // namespace mozilla

template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
class nsPresArena {
 public:
  constexpr nsPresArena() = default;
  ~nsPresArena();

  /**
   * Pool allocation with recycler lists indexed by object-type ID (see above).
   * Every aID must always be used with the same object size, aSize.
   */
  void* Allocate(ObjectId aCode, size_t aSize);
  void Free(ObjectId aCode, void* aPtr);

  /**
   * Increment aSizes with sizes of interesting objects allocated in this
   * arena, and put the general unclassified size in the relevant field
   * depending on the arena kind.
   */
  void AddSizeOfExcludingThis(mozilla::SizeOfState&, nsArenaSizes&,
                              mozilla::PresArenaKind) const;

  void Check() { mPool.Check(); }

  /**
   * Asserts that every allocation has been freed, and releases the debug-only
   * allocation tracking state.
   */
  void AssertNoLiveAllocations();

  /**
   * Sorts the free lists so that subsequent allocations reuse entries in
   * ascending address order.
   */
  void SortFreeLists();

 private:
  class FreeList {
   public:
    nsTArray<void*> mEntries;
    size_t mEntrySize = 0;
    size_t mEntriesEverAllocated = 0;

    constexpr FreeList() = default;

    size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
      return mEntries.ShallowSizeOfExcludingThis(aMallocSizeOf);
    }
  };

  FreeList mFreeLists[ObjectIdCount];
  mozilla::ArenaAllocator<ArenaSize, 8> mPool;
#ifdef DEBUG
  // We track allocated pointers in a diagnostic hash set, to assert against
  // missing/double frees. The set can get quite large, so we use fallible
  // allocation when inserting into it; and if these operations ever fail, then
  // we just get rid of the set and stop using this diagnostic from that point
  // on.  (There's not much else we can do, when the set grows larger than the
  // available memory.)
  std::unique_ptr<nsTHashSet<void*>> mAllocatedPointers;
  bool mAllocatedPointersInitialized = false;
#endif
};

extern template class nsPresArena<8192, mozilla::ArenaObjectID,
                                  mozilla::eArenaObjectID_COUNT>;
extern template class nsPresArena<
    32768, mozilla::DisplayListArenaObjectId,
    size_t(mozilla::DisplayListArenaObjectId::COUNT)>;

namespace mozilla {

/**
 * A process-wide, main-thread-only nsPresArena.
 */
template <size_t ArenaSize, typename ObjectId, size_t ObjectIdCount>
class StaticPresArena {
 public:
  using Arena = nsPresArena<ArenaSize, ObjectId, ObjectIdCount>;

  static void* Allocate(ObjectId aId, size_t aSize) {
    return sInstance.Allocate(aId, aSize);
  }
  static void Free(ObjectId aId, void* aPtr) { sInstance.Free(aId, aPtr); }
  static void Check() { sInstance.Check(); }
  static void AddSizeOf(SizeOfState& aState, nsArenaSizes& aSizes,
                        PresArenaKind aKind) {
    sInstance.AddSizeOfExcludingThis(aState, aSizes, aKind);
  }
  static void AssertNoLiveAllocations() { sInstance.AssertNoLiveAllocations(); }
  static void SortFreeLists() { sInstance.SortFreeLists(); }

 private:
  static Arena sInstance;
};

extern template class StaticPresArena<8192, ArenaObjectID,
                                      eArenaObjectID_COUNT>;
extern template class StaticPresArena<32768, DisplayListArenaObjectId,
                                      size_t(DisplayListArenaObjectId::COUNT)>;

/**
 * All frames are allocated from a process-wide arena, along with some other
 * closely-related objects (see nsPresArenaObjectList.inc). They are also
 * recycled using free lists. Separate free lists are maintained for each object
 * type (aID), which must always correspond to the same aSize value. Allocate is
 * infallible and will abort on out-of-memory.
 */
class FrameArena final
    : public StaticPresArena<8192, ArenaObjectID, eArenaObjectID_COUNT> {
 public:
  using StaticPresArena::Allocate;
  using StaticPresArena::Free;

  static void* Allocate(nsQueryFrame::FrameIID aID, size_t aSize) {
#define FRAME_ID(classname, ...)                                  \
  static_assert(size_t(nsQueryFrame::FrameIID::classname##_id) == \
                    size_t(eArenaObjectID_##classname),           \
                "");
#define ABSTRACT_FRAME_ID(classname)
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
    return Allocate(ArenaObjectID(size_t(aID)), aSize);
  }

  static void Free(nsQueryFrame::FrameIID aID, void* aPtr) {
    return Free(ArenaObjectID(size_t(aID)), aPtr);
  }
};

/**
 * Process-wide arena for display items, DisplayItemClipChain objects and
 * display list nodes.
 */
class DisplayListArena final
    : public StaticPresArena<32768, DisplayListArenaObjectId,
                             size_t(DisplayListArenaObjectId::COUNT)> {
 public:
  using StaticPresArena::Allocate;
  using StaticPresArena::Free;

  static void* Allocate(DisplayItemType aType, size_t aSize) {
#define DECLARE_DISPLAY_ITEM_TYPE(name_, ...)                \
  static_assert(size_t(DisplayItemType::TYPE_##name_) ==     \
                    size_t(DisplayListArenaObjectId::name_), \
                "");
#include "nsDisplayItemTypesList.inc"
#undef DECLARE_DISPLAY_ITEM_TYPE
    static_assert(size_t(DisplayItemType::TYPE_MAX) ==
                      size_t(DisplayListArenaObjectId::CLIPCHAIN),
                  "");
    return Allocate(DisplayListArenaObjectId(size_t(aType)), aSize);
  }

  static void Free(DisplayItemType aType, void* aPtr) {
    return Free(DisplayListArenaObjectId(size_t(aType)), aPtr);
  }
};

void InitPresArenas();
void ShutdownPresArenas();

}  // namespace mozilla

#endif
