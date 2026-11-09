/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FT2FONTBASE_H
#define GFX_FT2FONTBASE_H

#include "gfxContext.h"
#include "gfxFont.h"
#include "gfxFontEntry.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/UnscaledFontFreeType.h"
#include "nsTHashMap.h"
#include "nsHashKeys.h"

class gfxFT2FontBase;

namespace mozilla {
namespace gfx {
class FTUserFontData;
}
}  // namespace mozilla

class gfxFT2FontEntryBase : public gfxFontEntry {
 public:
  explicit gfxFT2FontEntryBase(const nsACString& aName) : gfxFontEntry(aName) {}

  uint32_t GetGlyph(uint32_t aCharCode, gfxFT2FontBase* aFont);

  static bool FaceHasTable(mozilla::gfx::SharedFTFace*, uint32_t aTableTag);
  static nsresult CopyFaceTable(mozilla::gfx::SharedFTFace*, uint32_t aTableTag,
                                nsTArray<uint8_t>&);

  virtual FontData* GetUserFontData() = 0;

  size_t ComputedSizeOfExcludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) override;

 private:
  enum { kNumCmapCacheSlots = 256 };

  struct CmapCacheSlot {
    CmapCacheSlot() : mCharCode(0), mGlyphIndex(0) {}

    uint32_t mCharCode;
    uint32_t mGlyphIndex;
  };

  mozilla::UniquePtr<CmapCacheSlot[]> mCmapCache MOZ_GUARDED_BY(mLock);
};

class gfxFT2FontBase : public gfxFont {
 public:
  gfxFT2FontBase(
      const RefPtr<mozilla::gfx::UnscaledFontFreeType>& aUnscaledFont,
      RefPtr<mozilla::gfx::SharedFTFace>&& aFTFace, gfxFontEntry* aFontEntry,
      const gfxFontStyle* aFontStyle, int aLoadFlags, bool aEmbolden);

  uint32_t GetGlyph(uint32_t aCharCode) {
    auto* entry = static_cast<gfxFT2FontEntryBase*>(mFontEntry.get());
    return entry->GetGlyph(aCharCode, this);
  }

  bool ProvidesGetGlyph() const override { return true; }
  virtual uint32_t GetGlyph(uint32_t unicode,
                            uint32_t variation_selector) override;

  bool ProvidesGlyphWidths() const override { return true; }
  int32_t GetGlyphWidth(uint16_t aGID) override {
    return GetCachedGlyphMetrics(aGID).mAdvance;
  }

  bool GetGlyphBounds(uint16_t aGID, gfxRect* aBounds, bool aTight) override;

  FontType GetType() const override { return FONT_TYPE_FT2; }

  bool ShouldRoundXOffset(cairo_t* aCairo) const override;

  static void SetupVarCoords(FT_MM_Var* aMMVar,
                             const nsTArray<gfxFontVariation>& aVariations,
                             FT_Face aFTFace);

  // Callers must always pair lock and unlock, regardless of return value.
  FT_Face LockFTFace() const;
  void UnlockFTFace() const;

 private:
  uint32_t GetCharExtents(uint32_t aChar, gfxFloat* aWidth,
                          gfxRect* aBounds = nullptr);

  // Get advance (and optionally bounds) of a single glyph from FreeType,
  // and return true, or return false if we failed.
  bool GetFTGlyphExtents(uint16_t aGID, int32_t* aWidth,
                         mozilla::gfx::IntRect* aBounds = nullptr);

 protected:
  ~gfxFT2FontBase() override;
  void InitMetrics();
  const Metrics& GetHorizontalMetrics() const override { return mMetrics; }
  FT_Vector GetEmboldenStrength(FT_Face aFace) const;

  RefPtr<mozilla::gfx::SharedFTFace> mFTFace;

  Metrics mMetrics;
  int mFTLoadFlags;
  bool mEmbolden;
  gfxFloat mFTSize;

  // For variation/multiple-master fonts, this will be an array of the values
  // for each axis, as specified by mStyle.variationSettings (or the font's
  // default for axes not present in variationSettings). Values here are in
  // freetype's 16.16 fixed-point format, and clamped to the valid min/max
  // range reported by the face.
  nsTArray<FT_Fixed> mCoords;

  // Store cached glyph metrics for reasonably small glyph sizes. The bounds
  // are stored unscaled to losslessly compress 26.6 fixed point to an int16_t.
  // Larger glyphs are handled directly via GetFTGlyphExtents.
  struct GlyphMetrics {
    // Set the X coord to INT16_MIN to signal the bounds are invalid, or
    // INT16_MAX to signal that the bounds would overflow an int16_t.
    enum { INVALID = INT16_MIN, LARGE = INT16_MAX };

    GlyphMetrics() : mAdvance(0), mX(INVALID), mY(0), mWidth(0), mHeight(0) {}

    bool HasValidBounds() const { return mX != INVALID; }
    bool HasCachedBounds() const { return mX != LARGE; }

    // If the bounds can fit in an int16_t, set them. Otherwise, leave the
    // bounds invalid to signal that GetFTGlyphExtents should be queried
    // directly.
    void SetBounds(const mozilla::gfx::IntRect& aBounds) {
      if (aBounds.x > INT16_MIN && aBounds.x < INT16_MAX &&
          aBounds.y > INT16_MIN && aBounds.y < INT16_MAX &&
          aBounds.width <= UINT16_MAX && aBounds.height <= UINT16_MAX) {
        mX = aBounds.x;
        mY = aBounds.y;
        mWidth = aBounds.width;
        mHeight = aBounds.height;
      } else {
        mX = LARGE;
      }
    }

    mozilla::gfx::IntRect GetBounds() const {
      return mozilla::gfx::IntRect(mX, mY, mWidth, mHeight);
    }

    int32_t mAdvance;
    int16_t mX;
    int16_t mY;
    uint16_t mWidth;
    uint16_t mHeight;
  };

  GlyphMetrics GetCachedGlyphMetrics(uint16_t aGID,
                                     mozilla::gfx::IntRect* aBounds = nullptr);

  mozilla::UniquePtr<nsTHashMap<nsUint32HashKey, GlyphMetrics>> mGlyphMetrics
      MOZ_GUARDED_BY(mLock);
};

#endif /* GFX_FT2FONTBASE_H */
