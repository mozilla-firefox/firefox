/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_GLYPHEXTENTS_H
#define GFX_GLYPHEXTENTS_H

#include "gfxFont.h"
#include "gfxRect.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RWLock.h"
#include "nsHashKeys.h"
#include "nsTArray.h"
#include "nsTHashtable.h"

class gfxContext;

namespace mozilla {
namespace gfx {
class DrawTarget;
}  // namespace gfx
}  // namespace mozilla

/**
 * This stores glyph bounds information for a particular gfxFont, at
 * a particular appunits-per-dev-pixel ratio (because the compressed glyph
 * width array is stored in appunits).
 *
 * We store a hashtable from glyph IDs to float bounding rects. For the
 * common case where the glyph has no horizontal left bearing, and no
 * y overflow above the font ascent or below the font descent, and tight
 * bounding boxes are not required, we avoid storing the glyph ID in the
 * hashtable and instead consult an array of 16-bit glyph XMost values (in
 * appunits). This array always has an entry for the font's space glyph --- the
 * width is assumed to be zero.
 */
class gfxGlyphExtents {
  typedef mozilla::gfx::DrawTarget DrawTarget;

 public:
  explicit gfxGlyphExtents(int32_t aAppUnitsPerDevUnit)
      : mAppUnitsPerDevUnit(aAppUnitsPerDevUnit),
        mLock("gfxGlyphExtents lock") {
    MOZ_COUNT_CTOR(gfxGlyphExtents);
  }
  ~gfxGlyphExtents();

  gfxGlyphExtents(const gfxGlyphExtents& aOther) = delete;
  gfxGlyphExtents& operator=(const gfxGlyphExtents& aOther) = delete;

  enum { INVALID_WIDTH = 0xFFFF };

  void NotifyGlyphsChanged() {
    mozilla::AutoWriteLock lock(mLock);
    mTightGlyphExtents.Clear();
  }

  // returns INVALID_WIDTH => not a contained glyph
  // Otherwise the glyph has no before-bearing or vertical bearings,
  // and the result is its width measured from the baseline origin, in
  // appunits.
  uint16_t GetContainedGlyphWidthAppUnitsLocked(uint32_t aGlyphID) const
      MOZ_REQUIRES_SHARED(mLock) {
    return mContainedGlyphWidths.Get(aGlyphID);
  }

  bool IsGlyphKnownLocked(uint32_t aGlyphID) const MOZ_REQUIRES_SHARED(mLock) {
    return mContainedGlyphWidths.Get(aGlyphID) != INVALID_WIDTH ||
           mTightGlyphExtents.GetEntry(aGlyphID) != nullptr;
  }

  bool IsGlyphKnownWithTightExtentsLocked(uint32_t aGlyphID) const
      MOZ_REQUIRES_SHARED(mLock) {
    return mTightGlyphExtents.GetEntry(aGlyphID) != nullptr;
  }

  // Get glyph extents; a rectangle relative to the left baseline origin
  // Returns true on success. Can fail on OOM or when aContext is null
  // and extents were not (successfully) prefetched.
  bool GetTightGlyphExtentsAppUnitsLocked(gfxFont* aFont,
                                          DrawTarget* aDrawTarget,
                                          uint32_t aGlyphID, gfxRect* aExtents)
      MOZ_REQUIRES_SHARED(mLock);
  bool GetTightGlyphExtentsAppUnits(gfxFont* aFont, DrawTarget* aDrawTarget,
                                    uint32_t aGlyphID, gfxRect* aExtents) {
    mozilla::AutoReadLock lock(mLock);
    return GetTightGlyphExtentsAppUnitsLocked(aFont, aDrawTarget, aGlyphID,
                                              aExtents);
  }

  void SetContainedGlyphWidthAppUnits(uint32_t aGlyphID, uint16_t aWidth) {
    mozilla::AutoWriteLock lock(mLock);
    mContainedGlyphWidths.Set(aGlyphID, aWidth);
  }
  void SetTightGlyphExtents(uint32_t aGlyphID, const gfxRect& aExtentsAppUnits);

  int32_t GetAppUnitsPerDevUnit() { return mAppUnitsPerDevUnit; }

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 private:
  class HashEntry : public nsUint32HashKey {
   public:
    // When constructing a new entry in the hashtable, we'll leave this
    // blank. The caller of Put() will fill this in.
    explicit HashEntry(KeyTypePointer aPtr)
        : nsUint32HashKey(aPtr), x(0.0), y(0.0), width(0.0), height(0.0) {}
    HashEntry(HashEntry&& aOther)
        : nsUint32HashKey(std::move(aOther)),
          x(aOther.x),
          y(aOther.y),
          width(aOther.width),
          height(aOther.height) {}

    float x, y, width, height;
  };

  enum {
    BLOCK_SIZE_BITS = 7,
    BLOCK_SIZE = 1 << BLOCK_SIZE_BITS
  };  // 128-glyph blocks

  class GlyphWidths {
   public:
    void Set(uint32_t aIndex, uint16_t aValue);
    uint16_t Get(uint32_t aIndex) const {
      uint32_t block = aIndex >> BLOCK_SIZE_BITS;
      if (block >= mBlocks.Length()) return INVALID_WIDTH;
      uintptr_t bits = mBlocks[block];
      if (!bits) return INVALID_WIDTH;
      uint32_t indexInBlock = aIndex & (BLOCK_SIZE - 1);
      if (bits & 0x1) {
        if (GetGlyphOffset(bits) != indexInBlock) return INVALID_WIDTH;
        return GetWidth(bits);
      }
      uint16_t* widths = reinterpret_cast<uint16_t*>(bits);
      return widths[indexInBlock];
    }

    uint32_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

    ~GlyphWidths();

   private:
    static uint32_t GetGlyphOffset(uintptr_t aBits) {
      NS_ASSERTION(aBits & 0x1, "This is really a pointer...");
      return (aBits >> 1) & ((1 << BLOCK_SIZE_BITS) - 1);
    }
    static uint32_t GetWidth(uintptr_t aBits) {
      NS_ASSERTION(aBits & 0x1, "This is really a pointer...");
      return aBits >> (1 + BLOCK_SIZE_BITS);
    }
    static uintptr_t MakeSingle(uint32_t aGlyphOffset, uint16_t aWidth) {
      return (aWidth << (1 + BLOCK_SIZE_BITS)) + (aGlyphOffset << 1) + 1;
    }

    nsTArray<uintptr_t> mBlocks;
  };

  GlyphWidths mContainedGlyphWidths MOZ_GUARDED_BY(mLock);
  nsTHashtable<HashEntry> mTightGlyphExtents MOZ_GUARDED_BY(mLock);
  const int32_t mAppUnitsPerDevUnit;

 public:
  mutable mozilla::RWLock mLock;
};

#endif
