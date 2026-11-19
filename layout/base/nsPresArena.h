/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* arena allocation for the frame tree and closely-related objects */

#ifndef nsPresArena_h_
#define nsPresArena_h_

#include <memory>

#include "mozilla/ArenaObjectID.h"
#include "mozilla/DisplayListArenaObjectId.h"
#include "nsDisplayItemTypes.h"
#include "nsQueryFrame.h"
#include "nsTArray.h"
#include "nsTHashSet.h"

struct nsArenaSizes;
namespace mozilla {
class SizeOfState;
enum class PresArenaKind { Frames, DisplayList };
}  // namespace mozilla

namespace mozilla::detail {
struct PresArenaSlabMetadata;
struct PresArenaSuperblock;
}  // namespace mozilla::detail

/**
 * Type-segregated slab allocator for the frame tree and display lists. See the
 * cpp file for details on allocation and memory layout.
 */
template <typename ObjectId, size_t ObjectIdCount>
class nsPresArena {
 public:
  constexpr nsPresArena() = default;
  ~nsPresArena();

  /**
   * Every aCode must always be used with the same object size, aSize.
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

  void Check();

  /**
   * Asserts that every allocation has been freed, and releases the debug-only
   * allocation tracking state.
   */
  void AssertNoLiveAllocations();

  /**
   * Decommits the memory of empty slabs. If aAll is false, only slabs that
   * have been empty since the previous call are decommitted. Returns whether
   * there are empty slabs left to decommit.
   */
  bool Purge(bool aAll);

 private:
  using SlabMetadata = mozilla::detail::PresArenaSlabMetadata;
  using Superblock = mozilla::detail::PresArenaSuperblock;

  struct TypeHeap {
    nsTArray<SlabMetadata*> mSlabs;
    // Bitmap over mSlabs of the slabs which have at least one free slot.
    nsTArray<uint64_t> mSlabsWithSpace;
    uint32_t mEntrySize = 0;
    uint32_t mSlotsPerSlab = 0;
    uint32_t mSlabSize = 0;
    // No slab with a lower index than this has free space.
    uint32_t mFirstSlabWithSpace = 0;

    constexpr TypeHeap() = default;
  };

  void InitHeap(TypeHeap&, size_t aSize);
  SlabMetadata* FindSlabWithSpace(TypeHeap&);
  char* AllocatePages(size_t aSize);

  TypeHeap mHeaps[ObjectIdCount];
  nsTArray<Superblock*> mSuperblocks;
  // Byte offset of the next unused page in the last superblock.
  uint32_t mNextOffset = 0;
  uint32_t mEpoch = 0;
  uint32_t mEmptyCommittedSlabs = 0;
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

extern template class nsPresArena<mozilla::ArenaObjectID,
                                  mozilla::eArenaObjectID_COUNT>;
extern template class nsPresArena<
    mozilla::DisplayListArenaObjectId,
    size_t(mozilla::DisplayListArenaObjectId::COUNT)>;

namespace mozilla {

/**
 * A process-wide, main-thread-only nsPresArena.
 */
template <typename ObjectId, size_t ObjectIdCount>
class StaticPresArena {
 public:
  using Arena = nsPresArena<ObjectId, ObjectIdCount>;

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
  static bool Purge(bool aAll) { return sInstance.Purge(aAll); }

 private:
  static Arena sInstance;
};

extern template class StaticPresArena<ArenaObjectID, eArenaObjectID_COUNT>;
extern template class StaticPresArena<DisplayListArenaObjectId,
                                      size_t(DisplayListArenaObjectId::COUNT)>;

/**
 * All frames are allocated from a process-wide arena, along with some other
 * closely-related objects (see nsPresArenaObjectList.inc). Each object type
 * (aID) gets its own slabs, and must always correspond to the same aSize value.
 * Allocate is infallible and will abort on out-of-memory.
 */
class FrameArena final
    : public StaticPresArena<ArenaObjectID, eArenaObjectID_COUNT> {
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
    : public StaticPresArena<DisplayListArenaObjectId,
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
