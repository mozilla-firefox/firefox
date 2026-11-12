/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_MACFONT_H
#define GFX_MACFONT_H

#include <CoreText/CoreText.h>

#include "gfxFont.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/gfx/UnscaledFontMac.h"

class CTFontEntry;

class gfxMacFont final : public gfxFont {
 public:
  gfxMacFont(const RefPtr<mozilla::gfx::UnscaledFontMac>& aUnscaledFont,
             CTFontEntry* aFontEntry, const gfxFontStyle* aFontStyle);

  CGFontRef GetCGFontRef() const { return mCGFont; }

  /* override Measure to add padding for antialiasing */
  RunMetrics Measure(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
                     BoundingBoxType aBoundingBoxType,
                     DrawTarget* aDrawTargetForTightBoundingBox,
                     Spacing* aSpacing, nscoord aLetterSpacing,
                     mozilla::gfx::ShapedTextFlags aOrientation) override;

  // We need to provide hinted (non-linear) glyph widths if using a font
  // with embedded color bitmaps (Apple Color Emoji), as Core Text renders
  // the glyphs with non-linear scaling at small pixel sizes.
  bool ProvidesGlyphWidths() const override {
    return mVariationFont ||
           mFontEntry->HasFontTable(TRUETYPE_TAG('s', 'b', 'i', 'x'));
  }

  int32_t GetGlyphWidth(uint16_t aGID) override;

  bool GetGlyphBounds(uint16_t aGID, gfxRect* aBounds, bool aTight) override;

  already_AddRefed<mozilla::gfx::ScaledFont> GetScaledFont(
      const TextRunDrawParams& aRunParams) override;

  bool ShouldRoundXOffset(cairo_t* aCairo) const override;

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const override;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const override;

  FontType GetType() const override { return FONT_TYPE_MAC; }

  bool UseNativeColrFontSupport() const override;

 protected:
  ~gfxMacFont() override;

  bool InitMetricsFromPlatform() override;
  void FinishInitFontMetrics() override;

  // Get width and glyph ID for a character; uses aConvFactor
  // to convert font units as returned by CG to actual dimensions
  gfxFloat GetCharWidth(CFDataRef aCmap, char16_t aUniChar, uint32_t* aGlyphID,
                        gfxFloat aConvFactor);

  // a strong reference to the CoreGraphics font
  CGFontRef mCGFont;

  // a Core Text font reference, created only if we're using CT to measure
  // glyph widths; otherwise null.
  CTFontRef mCTFont;

  bool mVariationFont;  // true if font has OpenType variations
};

#endif /* GFX_MACFONT_H */
