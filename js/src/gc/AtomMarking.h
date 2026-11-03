/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_AtomMarking_h
#define gc_AtomMarking_h

#include "mozilla/Atomics.h"

#include "NamespaceImports.h"

#include "gc/Cell.h"
#include "js/Vector.h"
#include "threading/ProtectedData.h"

namespace js {

class AutoLockGC;
class DenseBitmap;

namespace gc {

class Arena;
class GCRuntime;

// This class manages state used for marking atoms during GCs.
// See AtomMarking.cpp for details.
class AtomRefRuntime {
  // Unused arena atom bitmap indexes.
  js::MainThreadData<Vector<size_t, 0, SystemAllocPolicy>> freeArenaIndexes;

  // Background sweep state for |freeArenaIndexes|.
  js::GCLockData<Vector<size_t, 0, SystemAllocPolicy>> pendingFreeArenaIndexes;
  mozilla::Atomic<bool, mozilla::Relaxed> hasPendingFreeArenaIndexes;

  // Synchronize access during concurrent marking.
  AtomRefLock atomRefLock;

 public:
  // The extent of all allocated and free words in atom reference bitmaps. This
  // monotonically increases and may be read from without locking.
  mozilla::Atomic<size_t, mozilla::SequentiallyConsistent> allocatedWords{0};

  AtomRefRuntime() = default;

  // Allocate an index in the atom reference bitmap for a new arena.
  size_t allocateIndex(GCRuntime* gc);

  // Free an index in the atom reference bitmap.
  void freeIndex(size_t index, const AutoLockGC& lock);

  void mergePendingFreeArenaIndexes(GCRuntime* gc);

  // Update the atom reference bitmaps in all collected zones according to the
  // atoms zone mark bits.
  void refineZoneBitmapsForCollectedZones(GCRuntime* gc);

  // Get a bitmap of all atoms referenced by zones that are not being collected
  // by the current GC. On failure, mark the atoms instead.
  UniquePtr<DenseBitmap> getOrMarkAtomsUsedByUncollectedZones(GCRuntime* gc);

  // Set any bits in the chunk mark bitmaps for atoms which are referenced by
  // uncollected zones, using the bitmap returned from the previous method.
  void markAtomsUsedByUncollectedZones(GCRuntime* gc,
                                       UniquePtr<DenseBitmap> markedUnion);

  // Get the index into the atom reference bitmaps for the first bit associated
  // with an atom.
  // This is public for testing access.
  static size_t getAtomBit(TenuredCell* thing);

 private:
  // Fill |bitmap| with an atom reference bitmap based on the things that are
  // currently marked in the chunks used by atoms zone arenas. This returns
  // false on an allocation failure (but does not report an exception).
  bool computeBitmapFromChunkMarkBits(GCRuntime* gc, DenseBitmap& bitmap);

  // Update the overapproximation of the reachable atoms in |zone| in one go
  // according to the marking state after GC.
  void refineZoneBitmapForCollectedZone(Zone* zone, const DenseBitmap& bitmap);

  // As above but called per-arena and using the mark bits directly.
  void refineZoneBitmapForCollectedZone(Zone* zone, Arena* arena);

  template <typename T>
  MOZ_ALWAYS_INLINE bool inlinedRecordRefInternal(Zone* zone, T* thing,
                                                  const AutoMarkingLock& lock);

 public:
  // Record a reference from the context's zone to an atom or id.
  template <typename T>
  void recordRef(JSContext* cx, T* thing);

  // Versions of recordRef that are always inlined, for performance-sensitive
  // callers.
  template <typename T>
  MOZ_ALWAYS_INLINE void inlinedRecordRefInfallible(Zone* zone, T* thing);
  template <typename T>
  [[nodiscard]] MOZ_ALWAYS_INLINE bool inlinedRecordRefFallible(Zone* zone,
                                                                T* thing);
  // Return whether |zone| has a reference to the |thing/id|.
  template <typename T>
  bool hasRef(Zone* zone, T* thing);

  // For testing purposes, check whether the bit at |bitIndex| in the atom
  // reference bitmap is set for |zone|.
  bool getBitByIndex(Zone* zone, size_t bitIndex);
};

}  // namespace gc
}  // namespace js

#endif  // gc_AtomMarking_h
