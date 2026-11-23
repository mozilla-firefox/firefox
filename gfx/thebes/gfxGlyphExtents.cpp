/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxGlyphExtents.h"

#include "gfxTextRun.h"

using namespace mozilla;

#ifdef DEBUG_roc
#  define DEBUG_TEXT_RUN_STORAGE_METRICS
#endif

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
extern uint32_t gTextRunStorageHighWaterMark;
extern uint32_t gTextRunStorage;
extern uint32_t gFontCount;
extern uint32_t gGlyphExtentsCount;
extern uint32_t gGlyphExtentsWidthsTotalSize;
extern uint32_t gGlyphExtentsSetupEagerSimple;
extern uint32_t gGlyphExtentsSetupEagerTight;
extern uint32_t gGlyphExtentsSetupLazyTight;
extern uint32_t gGlyphExtentsSetupFallBackToTight;
#endif

gfxGlyphExtents::~gfxGlyphExtents() {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
  gGlyphExtentsWidthsTotalSize +=
      mContainedGlyphWidths.SizeOfExcludingThis(&FontCacheMallocSizeOf);
  gGlyphExtentsCount++;
#endif
  MOZ_COUNT_DTOR(gfxGlyphExtents);
}

bool gfxGlyphExtents::GetTightGlyphExtentsAppUnitsLocked(
    gfxFont* aFont, DrawTarget* aDrawTarget, uint32_t aGlyphID,
    gfxRect* aExtents) {
  HashEntry* entry = mTightGlyphExtents.GetEntry(aGlyphID);
  if (!entry) {
    // Some functions higher up in the call chain deliberately pass in a
    // nullptr DrawTarget, e.g. GetBaselinePosition() passes nullptr to
    // gfxTextRun::MeasureText() and that nullptr reaches here.
    if (!aDrawTarget) {
      NS_WARNING("Could not get glyph extents (no aDrawTarget)");
      return false;
    }

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    ++gGlyphExtentsSetupLazyTight;
#endif
    // We need to temporarily release the read lock, as SetupGlyphExtents will
    // take a write lock internally when it wants to set the new entry.
    MOZ_PUSH_IGNORE_THREAD_SAFETY
    mLock.ReadUnlock();
    aFont->SetupGlyphExtents(aDrawTarget, aGlyphID, true, this);
    mLock.ReadLock();
    MOZ_POP_THREAD_SAFETY

    entry = mTightGlyphExtents.GetEntry(aGlyphID);
    if (!entry) {
      NS_WARNING("Could not get glyph extents");
      return false;
    }
  }

  *aExtents = gfxRect(entry->x, entry->y, entry->width, entry->height);
  return true;
}

gfxGlyphExtents::GlyphWidths::~GlyphWidths() {
  uint32_t i, count = mBlocks.Length();
  for (i = 0; i < count; ++i) {
    uintptr_t bits = mBlocks[i];
    if (bits && !(bits & 0x1)) {
      delete[] reinterpret_cast<uint16_t*>(bits);
    }
  }
}

uint32_t gfxGlyphExtents::GlyphWidths::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  uint32_t i;
  uint32_t size = mBlocks.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (i = 0; i < mBlocks.Length(); ++i) {
    uintptr_t bits = mBlocks[i];
    if (bits && !(bits & 0x1)) {
      size += aMallocSizeOf(reinterpret_cast<void*>(bits));
    }
  }
  return size;
}

void gfxGlyphExtents::GlyphWidths::Set(uint32_t aGlyphID, uint16_t aWidth) {
  uint32_t block = aGlyphID >> BLOCK_SIZE_BITS;
  uint32_t len = mBlocks.Length();
  if (block >= len) {
    uintptr_t* elems = mBlocks.AppendElements(block + 1 - len);
    if (!elems) return;
    memset(elems, 0, sizeof(uintptr_t) * (block + 1 - len));
  }

  uintptr_t bits = mBlocks[block];
  uint32_t glyphOffset = aGlyphID & (BLOCK_SIZE - 1);
  if (!bits) {
    mBlocks[block] = MakeSingle(glyphOffset, aWidth);
    return;
  }

  uint16_t* newBlock;
  if (bits & 0x1) {
    // Expand the block to a real block. We could avoid this by checking
    // glyphOffset == GetGlyphOffset(bits), but that never happens so don't
    // bother
    newBlock = new uint16_t[BLOCK_SIZE];
    if (!newBlock) return;
    uint32_t i;
    for (i = 0; i < BLOCK_SIZE; ++i) {
      newBlock[i] = INVALID_WIDTH;
    }
    newBlock[GetGlyphOffset(bits)] = GetWidth(bits);
    mBlocks[block] = reinterpret_cast<uintptr_t>(newBlock);
  } else {
    newBlock = reinterpret_cast<uint16_t*>(bits);
  }
  newBlock[glyphOffset] = aWidth;
}

void gfxGlyphExtents::SetTightGlyphExtents(uint32_t aGlyphID,
                                           const gfxRect& aExtentsAppUnits) {
  AutoWriteLock lock(mLock);
  HashEntry* entry = mTightGlyphExtents.PutEntry(aGlyphID);
  if (!entry) {
    return;
  }
  entry->x = aExtentsAppUnits.X();
  entry->y = aExtentsAppUnits.Y();
  entry->width = aExtentsAppUnits.Width();
  entry->height = aExtentsAppUnits.Height();
}

size_t gfxGlyphExtents::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  AutoReadLock lock(mLock);
  return mContainedGlyphWidths.SizeOfExcludingThis(aMallocSizeOf) +
         mTightGlyphExtents.ShallowSizeOfExcludingThis(aMallocSizeOf);
}

size_t gfxGlyphExtents::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}
