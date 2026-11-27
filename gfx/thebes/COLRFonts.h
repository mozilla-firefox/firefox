/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COLR_FONTS_H
#define COLR_FONTS_H

#include "harfbuzz/hb.h"
#include "mozilla/gfx/2D.h"
#include "nsAtom.h"
#include "nsColor.h"
#include "nsTArray.h"
#include "nsTHashtable.h"

namespace mozilla {

namespace layout {
class TextDrawTarget;
}

namespace gfx {

class FontPaletteValueSet {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontPaletteValueSet)

  struct OverrideColor {
    uint32_t mIndex = 0;
    nscolor mColor;
  };

  struct PaletteValues {
    enum { kLight = -1, kDark = -2 };
    int32_t mBasePalette = 0;  // 0-based index, or kLight/kDark constants
    nsTArray<OverrideColor> mOverrides;
  };

  const PaletteValues* Lookup(nsAtom* aName, const nsACString& aFamily) const;

  PaletteValues* Insert(nsAtom* aName, const nsACString& aFamily);

 private:
  ~FontPaletteValueSet() = default;

  struct PaletteHashKey {
    RefPtr<nsAtom> mName;
    nsCString mFamily;

    PaletteHashKey() = delete;
    PaletteHashKey(nsAtom* aName, const nsACString& aFamily)
        : mName(aName), mFamily(aFamily) {}
    PaletteHashKey(const PaletteHashKey& aKey) = default;
  };

  class HashEntry : public PLDHashEntryHdr {
   public:
    using KeyType = const PaletteHashKey&;
    using KeyTypePointer = const PaletteHashKey*;

    HashEntry() = delete;
    explicit HashEntry(KeyTypePointer aKey) : mKey(*aKey) {}
    HashEntry(HashEntry&& other) noexcept
        : PLDHashEntryHdr(std::move(other)),
          mKey(other.mKey),
          mValue(std::move(other.mValue)) {
      NS_ERROR("Should not be called");
    }
    ~HashEntry() = default;

    bool KeyEquals(const KeyTypePointer aKey) const {
      return mKey.mName == aKey->mName && mKey.mFamily.Equals(aKey->mFamily);
    }
    static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
    static PLDHashNumber HashKey(const KeyTypePointer aKey) {
      return aKey->mName->hash() +
             HashString(aKey->mFamily.get(), aKey->mFamily.Length());
    }
    enum { ALLOW_MEMMOVE = true };

    PaletteHashKey mKey;
    PaletteValues mValue;
  };

  nsTHashtable<HashEntry> mValues;
};

class COLRFonts {
 public:
  static bool ValidateColorGlyphs(hb_blob_t* aCOLR, hb_blob_t* aCPAL);

  // COLRv0: color glyph is represented as a simple list of colored layers.
  // (This is used only as an opaque pointer; the internal type is private.)
  struct GlyphLayers;

  static const GlyphLayers* GetGlyphLayers(hb_blob_t* aCOLR, uint32_t aGlyphId);

  // For both paint functions, *aUsedCurrentColor (if given) is set to true if
  // the rendering depended on aCurrentColor.
  static bool PaintGlyphLayers(
      hb_blob_t* aCOLR, hb_face_t* aFace, const GlyphLayers* aLayers,
      DrawTarget* aDrawTarget, layout::TextDrawTarget* aTextDrawer,
      ScaledFont* aScaledFont, DrawOptions aDrawOptions, const Point& aPoint,
      const sRGBColor& aCurrentColor, const nsTArray<hb_color_t>* aColors,
      bool* aUsedCurrentColor = nullptr);

  // COLRv1 support: color glyph is represented by a directed acyclic graph of
  // paint records.
  // (This is used only as an opaque pointer; the internal type is private.)
  struct GlyphPaintGraph;

  static const GlyphPaintGraph* GetGlyphPaintGraph(hb_blob_t* aCOLR,
                                                   uint32_t aGlyphId);

  static bool PaintGlyphGraph(
      hb_blob_t* aCOLR, hb_font_t* aFont, const GlyphPaintGraph* aPaintGraph,
      DrawTarget* aDrawTarget, layout::TextDrawTarget* aTextDrawer,
      ScaledFont* aScaledFont, DrawOptions aDrawOptions, const Point& aPoint,
      const sRGBColor& aCurrentColor, const nsTArray<hb_color_t>* aColors,
      uint32_t aGlyphId, float aFontUnitsToPixels,
      bool* aUsedCurrentColor = nullptr);

  static Rect GetColorGlyphBounds(hb_blob_t* aCOLR, hb_font_t* aFont,
                                  uint32_t aGlyphId, DrawTarget* aDrawTarget,
                                  ScaledFont* aScaledFont,
                                  float aFontUnitsToPixels);

  static uint16_t GetColrTableVersion(hb_blob_t* aCOLR);

  static nsTArray<hb_color_t> CreateColorPalette(
      hb_face_t* aFace, const FontPaletteValueSet* aPaletteValueSet,
      nsAtom* aFontPalette, const nsACString& aFamilyName);
};

}  // namespace gfx

}  // namespace mozilla

#endif  // COLR_FONTS_H
