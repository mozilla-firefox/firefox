/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <type_traits>

#include "gc/GCLock.h"
#include "gc/PublicIterators.h"

#include "gc/AtomMarking-inl.h"
#include "gc/GC-inl.h"
#include "gc/Heap-inl.h"
#include "gc/PrivateIterators-inl.h"

namespace js {
namespace gc {

// [SMDOC] GC Atom Marking
//
// Things in the atoms zone (which includes atomized strings, symbols, and other
// things, all of which we will refer to as 'atoms' here) may be pointed to
// freely by things in other zones. To avoid the need to perform garbage
// collections of the entire runtime to collect atoms, we compute a separate
// atom reference bitmap for each zone that is always an overapproximation of
// the atoms that zone is using: if a zone has a reference to an atom, then it
// MUST have a reference recorded in the bitmap. If there is a reference in the
// bitmap, there (only) MAY be an actual reference from the zone. When an atom
// is not in the reference bitmap for any zone, it can be destroyed.
//
// (These bitmaps can only be calculated exactly if we collect a single zone
// since they are based on the marking state at the end of a GC which may have
// marked multiple zones.)
//
// Note that some "atoms" can be marked gray, but since the atom reference
// bitmap is not used for anything that may participate in a gray cycle it does
// not record this information. When reading or updating GC marking state things
// in the gray state are elevated to black.
//
// The atom reference bitmap is used for non-unique symbols (well known symbols,
// registered symbols and private names), which cannot participate in gray
// cycles. Unique symbols instead use local wrappers to track references between
// zones.
//
// To minimize interference with the rest of the GC, atom marking and sweeping
// is done by manipulating the reference bitmaps in the chunks holding the
// atoms. When the atoms zone is being collected, the reference bitmaps for the
// chunk(s) used by the atoms are updated normally during marking (though note
// that this marking may rely on uncollected zones' bitmaps.)
//
// After marking has finished and before sweeping begins, two things happen:
//
//  1) The atom reference bitmaps for collected zones are updated to remove
//     atoms that GC marking has found are not referenced by any collected zone
//     (see refineZoneBitmapsForCollectedZones). This improves our
//     approximation.
//
//  2) The chunk reference bitmaps are updated with any atoms that might be
//     referenced by zones which weren't collected (see
//     markAtomsUsedByUncollectedZones).
//
// GC sweeping will then release all atoms not marked by any zone.
//
// The representation of atom reference bitmaps is as follows:
//
// Each arena in the atoms zone has an atomBitmapStart() value indicating the
// word index into the mark bitmap of the first thing in the arena. Each arena
// uses ArenaBitmapWords of data to store its mark bitmap, which uses the same
// representation as chunk reference bitmaps: at least two bits per cell (see
// CellBytesPerMarkBit and MarkBitsPerCell).

size_t AtomRefRuntime::allocateIndex(GCRuntime* gc) {
  // We need to find a range of bits from the atoms bitmap for this arena.

  // Try to merge background swept free indexes if necessary.
  if (freeArenaIndexes.ref().empty()) {
    mergePendingFreeArenaIndexes(gc);
  }

  // Look for a free range of bits compatible with this arena.
  if (!freeArenaIndexes.ref().empty()) {
    return freeArenaIndexes.ref().popCopy();
  }

  // Allocate a range of bits from the end for this arena.
  size_t index = allocatedWords;
  allocatedWords += ArenaBitmapWords;
  return index;
}

void AtomRefRuntime::freeIndex(size_t index, const AutoLockGC& lock) {
  MOZ_ASSERT((index % ArenaBitmapWords) == 0);
  MOZ_ASSERT(index < allocatedWords);

  bool wasEmpty = pendingFreeArenaIndexes.ref().empty();
  MOZ_ASSERT_IF(wasEmpty, !hasPendingFreeArenaIndexes);

  if (!pendingFreeArenaIndexes.ref().append(index)) {
    // Leak these atom bits if we run out of memory.
    return;
  }

  if (wasEmpty) {
    hasPendingFreeArenaIndexes = true;
  }
}

void AtomRefRuntime::mergePendingFreeArenaIndexes(GCRuntime* gc) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(gc->rt));
  if (!hasPendingFreeArenaIndexes) {
    return;
  }

  AutoLockGC lock(gc);
  MOZ_ASSERT(!pendingFreeArenaIndexes.ref().empty());

  hasPendingFreeArenaIndexes = false;

  if (freeArenaIndexes.ref().empty()) {
    std::swap(freeArenaIndexes.ref(), pendingFreeArenaIndexes.ref());
    return;
  }

  // Leak these atom bits if we run out of memory.
  (void)freeArenaIndexes.ref().appendAll(pendingFreeArenaIndexes.ref());
  pendingFreeArenaIndexes.ref().clear();
}

// Return whether more than one zone is being collected.
static bool MultipleNonAtomZonesAreBeingCollected(GCRuntime* gc) {
  size_t count = 0;
  for (GCZonesIter zone(gc); !zone.done(); zone.next()) {
    if (!zone->isAtomsZone()) {
      count++;
      if (count == 2) {
        return true;
      }
    }
  }

  return false;
}

void AtomRefRuntime::refineZoneBitmapsForCollectedZones(GCRuntime* gc) {
  // If there is more than one zone to update, it's more efficient to copy the
  // chunk mark bits from each arena into a single dense bitmap and then use
  // that to refine the atom reference bitmap for each zone.
  DenseBitmap marked;
  if (MultipleNonAtomZonesAreBeingCollected(gc) &&
      computeBitmapFromChunkMarkBits(gc, marked)) {
    for (GCZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
      refineZoneBitmapForCollectedZone(zone, marked);
    }
    return;
  }

  // If there's only one zone (or on OOM), refine the mark bits for each arena
  // with the zones' atom reference bitmaps directly.
  for (GCZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
    for (auto thingKind : AllAllocKinds()) {
      for (ArenaIterInGC aiter(gc->atomsZone(), thingKind); !aiter.done();
           aiter.next()) {
        refineZoneBitmapForCollectedZone(zone, aiter);
      }
    }
  }
}

// Refining atom reference bitmaps:
//
// The atom reference bitmap for a zone records an overapproximation of the set
// of atoms referenced by that zone. After collection we refine this based on
// the actual final mark state.
//
// To refine the bitmap we use bitwise AND. For trace kinds that can only be
// marked black this works on its own. For kinds that can be marked gray we must
// preprocess the mark bitmap so that the black bit is also set for cells marked
// gray.

// Masks of bit positions in a mark bitmap word that can be ColorBit::BlackBit
// or GrayOrBlackBit, which alternative throughout the word.
#if JS_BITS_PER_WORD == 32
static constexpr uintptr_t GrayOrBlackBitMask = 0xaaaaaaaa;
#else
static constexpr uintptr_t GrayOrBlackBitMask = 0xaaaaaaaaaaaaaaaa;
#endif

static void PropagateGrayOrBlackBitsToBlackBits(
    uintptr_t (&words)[ArenaBitmapWords]) {
  // This only works if the gray bit and black bits are in the same word,
  // which is true for symbols.
  for (uintptr_t& word : words) {
    word |= (word & GrayOrBlackBitMask) >> 1;
  }
}

static void PropagateGrayOrBlackBitsToBlackBits(DenseBitmap& bitmap,
                                                Arena* arena) {
  // This only works if the gray bit and black bits are in the same word,
  // which is true for symbols.
  MOZ_ASSERT(
      TraceKindCanBeMarkedGray(MapAllocToTraceKind(arena->getAllocKind())));
  MOZ_ASSERT((arena->getThingSize() / CellBytesPerMarkBit) % 2 == 0);

  bitmap.forEachWord(
      arena->atomBitmapStart(), ArenaBitmapWords,
      [](uintptr_t& word) { word |= (word & GrayOrBlackBitMask) >> 1; });
}

#ifdef DEBUG
static bool ArenaContainsGrayCells(Arena* arena) {
  for (ArenaCellIter cell(arena); !cell.done(); cell.next()) {
    if (cell->isMarkedGray()) {
      return true;
    }
  }
  return false;
}
#endif

bool AtomRefRuntime::computeBitmapFromChunkMarkBits(GCRuntime* gc,
                                                    DenseBitmap& bitmap) {
  MOZ_ASSERT(CurrentThreadIsPerformingGC());

  if (!bitmap.ensureSpace(allocatedWords)) {
    return false;
  }

  Zone* atomsZone = gc->atomsZone();
  for (auto thingKind : AllAllocKinds()) {
    for (ArenaIterInGC aiter(atomsZone, thingKind); !aiter.done();
         aiter.next()) {
      Arena* arena = aiter.get();
      AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);
      bitmap.copyBitsFrom(arena->atomBitmapStart(), ArenaBitmapWords,
                          chunkWords);

      if (thingKind == AllocKind::JITCODE) {
        // The atoms zone can contain JitCode used for compiled self-hosted JS,
        // however these cells are never marked gray so we can skip this step.
        MOZ_ASSERT(!ArenaContainsGrayCells(arena));
      } else if (TraceKindCanBeMarkedGray(MapAllocToTraceKind(thingKind))) {
        // Ensure the black mark bit is set for any gray cells.
        PropagateGrayOrBlackBitsToBlackBits(bitmap, arena);
      }
    }
  }

  return true;
}

void AtomRefRuntime::refineZoneBitmapForCollectedZone(
    Zone* zone, const DenseBitmap& bitmap) {
  MOZ_ASSERT(zone->isCollectingFromAnyThread());
  MOZ_ASSERT(!zone->isAtomsZone());

  // Compute the intersection of the given mark bitmap and the zone's reference
  // set to get the best new overapproximation we can. |bitmap| might include
  // bits that are not in the zone's reference set, if additional zones were
  // collected by the GC.
  zone->referencedAtoms().bitwiseAndWith(bitmap);
}

void AtomRefRuntime::refineZoneBitmapForCollectedZone(Zone* zone,
                                                      Arena* arena) {
  MOZ_ASSERT(zone->isCollectingFromAnyThread());
  MOZ_ASSERT(!zone->isAtomsZone());

  AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);

  AllocKind kind = arena->getAllocKind();
  if (kind == AllocKind::JITCODE) {
    // The atoms zone can contain JitCode used for compiled self-hosted JS,
    // however these cells are never marked gray so we can skip this step.
    MOZ_ASSERT(!ArenaContainsGrayCells(arena));
  } else if (TraceKindCanBeMarkedGray(MapAllocToTraceKind(kind))) {
    uintptr_t words[ArenaBitmapWords];
    memcpy(words, chunkWords, sizeof(words));
    PropagateGrayOrBlackBitsToBlackBits(words);
    zone->referencedAtoms().bitwiseAndRangeWith(arena->atomBitmapStart(),
                                                ArenaBitmapWords, words);
    return;
  }

  zone->referencedAtoms().bitwiseAndRangeWith(arena->atomBitmapStart(),
                                              ArenaBitmapWords, chunkWords);
}

// Set any bits in the chunk mark bitmaps for atoms in a reference bitmap.
template <typename Bitmap>
static void BitwiseOrIntoChunkMarkBits(Zone* atomsZone, const Bitmap& bitmap) {
  // Make sure that by copying the mark bits for one arena in word sizes we
  // do not affect the mark bits for other arenas.
  static_assert(ArenaBitmapBits == ArenaBitmapWords * JS_BITS_PER_WORD,
                "ArenaBitmapWords must evenly divide ArenaBitmapBits");

  for (auto thingKind : AllAllocKinds()) {
    for (ArenaIterInGC aiter(atomsZone, thingKind); !aiter.done();
         aiter.next()) {
      Arena* arena = aiter.get();
      AtomicBitmapWord* chunkWords = arena->chunk()->markBits.arenaBits(arena);
      bitmap.bitwiseOrRangeInto(arena->atomBitmapStart(), ArenaBitmapWords,
                                chunkWords);
    }
  }
}

UniquePtr<DenseBitmap> AtomRefRuntime::getOrMarkAtomsUsedByUncollectedZones(
    GCRuntime* gc) {
  MOZ_ASSERT(CurrentThreadIsPerformingGC());

  UniquePtr<DenseBitmap> markedUnion = MakeUnique<DenseBitmap>();
  if (!markedUnion || !markedUnion->ensureSpace(allocatedWords)) {
    // On failure, mark the atoms immediately.
    for (ZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
      if (!zone->isCollecting()) {
        BitwiseOrIntoChunkMarkBits(gc->atomsZone(), zone->referencedAtoms());
      }
    }
    return nullptr;
  }

  for (ZonesIter zone(gc, SkipAtoms); !zone.done(); zone.next()) {
    if (!zone->isCollecting()) {
      zone->referencedAtoms().bitwiseOrInto(*markedUnion);
    }
  }

  return markedUnion;
}

void AtomRefRuntime::markAtomsUsedByUncollectedZones(
    GCRuntime* gc, UniquePtr<DenseBitmap> markedUnion) {
  BitwiseOrIntoChunkMarkBits(gc->atomsZone(), *markedUnion);
}

template void AtomRefRuntime::recordRef(JSContext* cx, JSAtom* thing);
template void AtomRefRuntime::recordRef(JSContext* cx, JS::Symbol* thing);

template <typename T>
bool AtomRefRuntime::hasRef(Zone* zone, T* thing) {
  static_assert(std::is_same_v<T, JSAtom> || std::is_same_v<T, JS::Symbol>,
                "Should only be called with JSAtom* or JS::Symbol* argument");

  MOZ_ASSERT(thing);
  MOZ_ASSERT(!IsInsideNursery(thing));
  MOZ_ASSERT(thing->zoneFromAnyThread()->isAtomsZone());

  if (!zone->runtimeFromAnyThread()->permanentAtomsPopulated()) {
    return true;
  }

  if (thing->isPermanentAndMayBeShared()) {
    return true;
  }

  if constexpr (std::is_same_v<T, JSAtom>) {
    if (thing->isPinned()) {
      return true;
    }
  }

  AutoMarkingLock lock(zone, atomRefLock);

  size_t bit = getAtomBit(&thing->asTenured());
  size_t blackBit = bit + size_t(ColorBit::BlackBit);
#ifdef DEBUG
  size_t grayOrBlackBit = bit + size_t(ColorBit::GrayOrBlackBit);
#endif

  SparseBitmap& bitmap = zone->referencedAtoms();

  MOZ_ASSERT(!bitmap.readonlyThreadsafeGetBit(grayOrBlackBit));
  return bitmap.readonlyThreadsafeGetBit(blackBit);
}

template bool AtomRefRuntime::hasRef(Zone* zone, JSAtom* thing);
template bool AtomRefRuntime::hasRef(Zone* zone, JS::Symbol* thing);

bool AtomRefRuntime::getBitByIndex(Zone* zone, size_t bitIndex) {
  MOZ_ASSERT(zone->runtimeFromAnyThread()->permanentAtomsPopulated());

  SparseBitmap& bitmap = zone->referencedAtoms();
  return bitmap.readonlyThreadsafeGetBit(bitIndex);
}

#ifdef DEBUG

template <>
bool AtomRefRuntime::hasRef(Zone* zone, TenuredCell* thing) {
  MOZ_ASSERT(thing);
  MOZ_ASSERT(thing->zoneFromAnyThread()->isAtomsZone());

  if (thing->is<JSString>()) {
    JSString* str = thing->as<JSString>();
    return hasRef(zone, &str->asAtom());
  }

  if (thing->is<JS::Symbol>()) {
    return hasRef(zone, thing->as<JS::Symbol>());
  }

  MOZ_CRASH("Unexpected atom kind");
}

#endif  // DEBUG

}  // namespace gc

#ifdef DEBUG

bool ZoneHasRef(Zone* zone, jsid id) {
  if (id.isAtom()) {
    return ZoneHasRef(zone, id.toAtom());
  }
  if (id.isSymbol()) {
    return ZoneHasRef(zone, id.toSymbol());
  }
  return true;
}

bool ZoneHasRef(Zone* zone, const Value& value) {
  if (value.isString()) {
    if (value.toString()->isAtom()) {
      return ZoneHasRef(zone, &value.toString()->asAtom());
    }
    return true;
  }
  if (value.isSymbol()) {
    return ZoneHasRef(zone, value.toSymbol());
  }
  return true;
}

bool ZoneHasRef(Zone* zone, JSAtom* atom) {
  return zone->runtimeFromAnyThread()->gc.atomReferences.hasRef(zone, atom);
}

bool ZoneHasRef(Zone* zone, JS::Symbol* symbol) {
  MOZ_ASSERT(symbol->isUnique() == symbol->isLocal());

  if (symbol->isLocal()) {
    MOZ_ASSERT(symbol->zone() == zone);
    MOZ_ASSERT(zone->hasLocalSymbol(symbol));
    return true;
  }

  return zone->runtimeFromAnyThread()->gc.atomReferences.hasRef(zone, symbol);
}

#endif  // DEBUG

}  // namespace js
