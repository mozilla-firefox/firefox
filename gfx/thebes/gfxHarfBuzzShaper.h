/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_HARFBUZZSHAPER_H
#define GFX_HARFBUZZSHAPER_H

#include "gfxFont.h"
#include "harfbuzz/hb.h"
#include "mozilla/MruCache.h"
#include "mozilla/RecursiveMutex.h"
#include "nsUnicodeProperties.h"

class gfxHarfBuzzShaper : public gfxFontShaper {
  // private static methods for HarfBuzz callbacks:
  static hb_bool_t HBGetNominalGlyph(hb_font_t* font, void* font_data,
                                     hb_codepoint_t unicode,
                                     hb_codepoint_t* glyph, void* user_data);
  static unsigned int HBGetNominalGlyphs(
      hb_font_t* font, void* font_data, unsigned int count,
      const hb_codepoint_t* first_unicode, unsigned int unicode_stride,
      hb_codepoint_t* first_glyph, unsigned int glyph_stride, void* user_data);
  static hb_bool_t HBGetVariationGlyph(hb_font_t* font, void* font_data,
                                       hb_codepoint_t unicode,
                                       hb_codepoint_t variation_selector,
                                       hb_codepoint_t* glyph, void* user_data);
  static hb_position_t HBGetGlyphHAdvance(hb_font_t* font, void* font_data,
                                          hb_codepoint_t glyph,
                                          void* user_data);
  static void HBGetGlyphHAdvances(hb_font_t* font, void* font_data,
                                  unsigned int count,
                                  const hb_codepoint_t* first_glyph,
                                  unsigned int glyph_stride,
                                  hb_position_t* first_advance,
                                  unsigned int advance_stride, void* user_data);
  static hb_position_t HBGetGlyphVAdvance(hb_font_t* font, void* font_data,
                                          hb_codepoint_t glyph,
                                          void* user_data);
  static hb_bool_t HBGetGlyphVOrigin(hb_font_t* font, void* font_data,
                                     hb_codepoint_t glyph, hb_position_t* x,
                                     hb_position_t* y, void* user_data);
  static hb_bool_t HBGetGlyphExtents(hb_font_t* font, void* font_data,
                                     hb_codepoint_t glyph,
                                     hb_glyph_extents_t* extents,
                                     void* user_data);
  static hb_bool_t HBGetContourPoint(hb_font_t* font, void* font_data,
                                     unsigned int point_index,
                                     hb_codepoint_t glyph, hb_position_t* x,
                                     hb_position_t* y, void* user_data);
  static hb_position_t HBGetHKerning(hb_font_t* font, void* font_data,
                                     hb_codepoint_t first_glyph,
                                     hb_codepoint_t second_glyph,
                                     void* user_data);
  static hb_bool_t HBGetHExtents(hb_font_t* font, void* font_data,
                                 hb_font_extents_t* extents, void* user_data);

 public:
  explicit gfxHarfBuzzShaper(gfxFont* aFont);
  virtual ~gfxHarfBuzzShaper();

  // Returns whether the shaper has been successfully initialized.
  bool IsInitialized() const { return mHBFont != nullptr; }

  bool ShapeText(const void* aText, TextEncoding aEncoding, uint32_t aOffset,
                 uint32_t aLength, const ShapingParams& aParams,
                 gfxShapedText* aShapedText) override;

  // map unicode character to glyph ID
  hb_codepoint_t GetNominalGlyph(hb_codepoint_t unicode) const;

  hb_codepoint_t GetVariationGlyph(hb_codepoint_t unicode,
                                   hb_codepoint_t variation_selector) const;

  // get harfbuzz glyph advance, in font design units
  hb_position_t GetGlyphHAdvance(hb_codepoint_t glyph) const;

  // Get vertical glyph advance, or -1 if not available; caller should check
  // for a negative result and provide a fallback or fail, as appropriate.
  hb_position_t GetGlyphVAdvance(hb_codepoint_t glyph);

  hb_font_t* GetHBFont() const { return mHBFont; }

  static hb_script_t GetHBScriptUsedForShaping(Script aScript) {
    // Decide what harfbuzz script code will be used for shaping
    hb_script_t hbScript;
    if (aScript <= Script::INHERITED) {
      // For unresolved "common" or "inherited" runs,
      // default to Latin for now.
      hbScript = HB_SCRIPT_LATIN;
    } else {
      hbScript = hb_script_t(mozilla::unicode::GetScriptTagForCode(aScript));
    }
    return hbScript;
  }

  static hb_codepoint_t GetVerticalPresentationForm(hb_codepoint_t aUnicode);

  // Create an hb_font corresponding to the given gfxFont instance, with size
  // and variations set appropriately. If aFontFuncs and aCallbackData are
  // provided, they may be used as harfbuzz font callbacks for advances, glyph
  // bounds, etc; if not, the built-in hb_ot font functions will be used.
  static hb_font_t* CreateHBFont(gfxFont* aFont,
                                 hb_font_funcs_t* aFontFuncs = nullptr,
                                 void* aCallbackData = nullptr,
                                 bool aCreateSubfont = false);

 protected:
  // Initializes the shaper and returns whether this was successful.
  bool Initialize();

  // get a given font table in harfbuzz blob form
  hb_blob_t* GetFontTable(hb_tag_t aTag) const;

  unsigned int GetNominalGlyphs(unsigned int count,
                                const hb_codepoint_t* first_unicode,
                                unsigned int unicode_stride,
                                hb_codepoint_t* first_glyph,
                                unsigned int glyph_stride) const
      MOZ_REQUIRES(mMutex);

  void GetGlyphHAdvances(unsigned int count, const hb_codepoint_t* first_glyph,
                         unsigned int glyph_stride,
                         hb_position_t* first_advance,
                         unsigned int advance_stride) const
      MOZ_REQUIRES(mMutex);

  void GetGlyphVOrigin(hb_codepoint_t aGlyph, hb_position_t* aX,
                       hb_position_t* aY) const;

  hb_position_t GetHKerning(uint16_t aFirstGlyph, uint16_t aSecondGlyph) const;

  hb_bool_t GetGlyphExtents(hb_codepoint_t aGlyph,
                            hb_glyph_extents_t* aExtents) const;

  bool UseVerticalPresentationForms() const {
    return mUseVerticalPresentationForms;
  }

  hb_codepoint_t GetGlyphUncached(hb_codepoint_t unicode) const;

  hb_position_t GetGlyphHAdvanceUncached(hb_codepoint_t gid) const;

  nsresult SetGlyphsFromRun(gfxShapedText* aShapedText, uint32_t aOffset,
                            uint32_t aLength, const void* aText,
                            TextEncoding aEncoding,
                            const ShapingParams& aParams) MOZ_REQUIRES(mMutex);

  // retrieve glyph positions, applying advance adjustments and attachments
  // returns results in appUnits
  nscoord GetGlyphPositions(gfxContext* aContext, nsTArray<nsPoint>& aPositions,
                            uint32_t aAppUnitsPerDevUnit);

  void InitializeVertical() MOZ_REQUIRES(mMutex);
  bool LoadHmtxTable();

  struct Glyf {  // we only need the bounding-box at the beginning
                 // of the glyph record, not the actual outline data
    mozilla::AutoSwap_PRInt16 numberOfContours;
    mozilla::AutoSwap_PRInt16 xMin;
    mozilla::AutoSwap_PRInt16 yMin;
    mozilla::AutoSwap_PRInt16 xMax;
    mozilla::AutoSwap_PRInt16 yMax;
  };

  const Glyf* FindGlyf(hb_codepoint_t aGlyph, bool* aEmptyGlyf) const;

  // size-specific font object, owned by the gfxHarfBuzzShaper
  hb_font_t* mHBFont = nullptr;

  // Held for the duration of ShapeText(): the shaper (and its hb_buffer_t)
  // is shared across threads via the global gfxFontCache, so concurrent
  // ShapeText() calls must be serialized to avoid racing on mBuffer and
  // other mutable per-call state.
  // GetNominalGlyph() and GetGlyphHAdvance() also need to lock the mutex
  // because they may be called directly from thebes code, as well as via
  // harfbuzz callbacks.
  mutable mozilla::RecursiveMutex mMutex =
      mozilla::RecursiveMutex("gfxHarfBuzzShaper::mMutex");

  // harfbuzz buffer for the shaping process.
  hb_buffer_t* mBuffer MOZ_GUARDED_BY(mMutex) = nullptr;

  struct CmapCacheData {
    uint32_t mCodepoint;
    uint32_t mGlyphId;
  };

  struct CmapCache
      : public mozilla::MruCache<uint32_t, CmapCacheData, CmapCache, 256> {
    static bool IsEmpty(const CmapCacheData& aData) {
      return !aData.mCodepoint && !aData.mGlyphId;
    }
    static mozilla::HashNumber Hash(const uint32_t& aKey) { return aKey; }
    static bool Match(const uint32_t& aKey, const CmapCacheData& aData) {
      return aKey == aData.mCodepoint;
    }
  };

  mutable mozilla::UniquePtr<CmapCache> mCmapCache MOZ_GUARDED_BY(mMutex);

  struct WidthCacheData {
    hb_codepoint_t mGlyphId;
    hb_position_t mAdvance;
  };

  struct WidthCache
      : public mozilla::MruCache<uint32_t, WidthCacheData, WidthCache, 256> {
    static bool IsEmpty(const WidthCacheData& aData) {
      return !aData.mGlyphId && !aData.mAdvance;
    }
    static mozilla::HashNumber Hash(const hb_codepoint_t& aKey) { return aKey; }
    static bool Match(const uint32_t& aKey, const WidthCacheData& aData) {
      return aKey == aData.mGlyphId;
    }
  };

  mutable mozilla::UniquePtr<WidthCache> mWidthCache MOZ_GUARDED_BY(mMutex);

  // Following table references etc are declared "mutable" because the
  // harfbuzz callback functions take a const ptr to the shaper, but
  // wish to cache tables here to avoid repeatedly looking them up
  // in the font.

  // Old-style TrueType kern table, if we're not doing GPOS kerning
  mutable hb_blob_t* mKernTable = nullptr;

  // Cached copy of the hmtx table.
  mutable hb_blob_t* mHmtxTable = nullptr;

  // For vertical fonts, cached vmtx and VORG table, if present.
  mutable hb_blob_t* mVmtxTable = nullptr;
  mutable hb_blob_t* mVORGTable = nullptr;
  // And for vertical TrueType (not CFF) fonts that have vmtx,
  // we also use loca and glyf to get glyph bounding boxes.
  mutable hb_blob_t* mLocaTable = nullptr;
  mutable hb_blob_t* mGlyfTable = nullptr;

  // Cached pointer to cmap subtable to be used for char-to-glyph mapping.
  // This comes from GetFontTablePtr; if it is non-null, our destructor
  // must call ReleaseFontTablePtr to avoid permanently caching the table.
  // These are set during Initialize(), before any use of the shaper.
  hb_blob_t* mCmapTable = nullptr;
  int32_t mCmapFormat = -1;
  uint32_t mSubtableOffset = 0;
  uint32_t mUVSTableOffset = 0;

  // Cached copy of numLongMetrics field from the hhea table,
  // for use when looking up glyph metrics; initialized to 0 by the
  // constructor so we can tell it hasn't been set yet.
  // This is a signed value so that we can use -1 to indicate
  // an error (if the hhea table was not available).
  mutable int32_t mNumLongHMetrics = 0;
  // Similarly for vhea if it's a vertical font.
  mutable int32_t mNumLongVMetrics = 0;

  // Default y-coordinate for glyph vertical origin, used if the font
  // does not actually have vertical-layout metrics.
  mutable gfxFloat mDefaultVOrg = -1.0;

  // Number of glyphs in the font (set from 'maxp' during initialization).
  uint32_t mNumGlyphs = 0;

  // Whether the font implements GetGlyph, or we should read tables
  // directly
  bool mUseFontGetGlyph = false;

  // Whether the font is an MS Symbol-encoded font, in which case we will
  // try remapping U+0020..00FF to U+F020..F0FF for characters in the U+00xx
  // range that are otherwise unsupported.
  bool mIsSymbolFont = false;

  // Whether the font implements GetGlyphWidth, or we should read tables
  // directly to get ideal widths
  bool mUseFontGlyphWidths = false;

  // Whether to use vertical presentation forms for CJK characters
  // when available (only set if the 'vert' feature is not available).
  bool mUseVerticalPresentationForms = false;

  // these are set from the FindGlyf callback on first use of the glyf data
  mutable bool mLoadedLocaGlyf = false;
  mutable bool mLocaLongOffsets = false;

  // This is atomic so that we can check it without necessarily holding the
  // lock.
  std::atomic<bool> mVerticalInitialized = false;
};

#endif /* GFX_HARFBUZZSHAPER_H */
