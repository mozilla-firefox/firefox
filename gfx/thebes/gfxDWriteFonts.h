/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WINDOWSDWRITEFONTS_H
#define GFX_WINDOWSDWRITEFONTS_H

#include <dwrite_1.h>

#include "gfxDWriteCommon.h"
#include "gfxFont.h"
#include "gfxUserFontSet.h"
#include "mozilla/Atomics.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/UnscaledFontDWrite.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsHashKeys.h"
#include "nsTHashMap.h"

/**
 * \brief Class representing a font face for a font entry.
 */
class gfxDWriteFont final : public gfxFont {
 public:
  gfxDWriteFont(const RefPtr<mozilla::gfx::UnscaledFontDWrite>& aUnscaledFont,
                gfxFontEntry* aFontEntry, const gfxFontStyle* aFontStyle,
                RefPtr<IDWriteFontFace> aFontFace = nullptr,
                AntialiasOption = kAntialiasDefault);

  static bool InitDWriteSupport();

  // These Update functions update gfxVars with font settings, they must only be
  // called in the parent process.
  static void UpdateSystemTextVars();
  static void UpdateClearTypeVars();

  static void SystemTextQualityChanged();

  bool InitMetricsFromPlatform() override;
  void FinishInitFontMetrics() override;

  gfxFont* CopyWithAntialiasOption(AntialiasOption anAAOption) const override;

  bool AllowSubpixelAA() const override { return mAllowManualShowGlyphs; }

  bool IsValid() const;

  IDWriteFontFace* GetFontFace();

  /* override Measure to add padding for antialiasing */
  RunMetrics Measure(const gfxTextRun* aTextRun, uint32_t aStart, uint32_t aEnd,
                     BoundingBoxType aBoundingBoxType,
                     DrawTarget* aDrawTargetForTightBoundingBox,
                     Spacing* aSpacing, nscoord aLetterSpacing,
                     mozilla::gfx::ShapedTextFlags aOrientation) override;

  bool ProvidesGlyphWidths() const override;

  int32_t GetGlyphWidth(uint16_t aGID) override;

  bool GetGlyphBounds(uint16_t aGID, gfxRect* aBounds, bool aTight) override;

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const override;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontCacheSizes* aSizes) const override;

  FontType GetType() const override { return FONT_TYPE_DWRITE; }

  already_AddRefed<mozilla::gfx::ScaledFont> GetScaledFont(
      const TextRunDrawParams& aRunParams) override;

  bool ShouldRoundXOffset(cairo_t* aCairo) const override;

 protected:
  ~gfxDWriteFont() override;

  bool GetFakeMetricsForArialBlack(DWRITE_FONT_METRICS* aFontMetrics);

  // Marks the font entry as unusable after DirectWrite raised an exception
  // while using the font face for aOperation.
  void HandleFontFaceException(const char* aOperation);

  bool HasBitmapStrikeForSize(uint32_t aSize);

  gfxFloat MeasureGlyphWidth(uint16_t aGlyph);

  DWRITE_MEASURING_MODE GetMeasuringMode() const;

  static mozilla::Atomic<bool> sForceGDIClassicEnabled;
  bool GetForceGDIClassic() const;

  RefPtr<IDWriteFontFace> mFontFace;
  RefPtr<IDWriteFontFace1> mFontFace1;  // may be unavailable on older DWrite

  // cache of glyph widths in 16.16 fixed-point pixels
  mozilla::UniquePtr<nsTHashMap<nsUint32HashKey, int32_t>> mGlyphWidths;

  bool mUseSubpixelPositions;
  bool mAllowManualShowGlyphs;

  // Cache the GDI version of the ScaledFont so that font keys and other
  // meta-data can remain stable even if there is thrashing between GDI and
  // non-GDI usage.
  mozilla::Atomic<mozilla::gfx::ScaledFont*> mAzureScaledFontGDI;

  // Cache the ClearType-enabled versions of ScaledFonts if setting is toggled.
  mozilla::Atomic<mozilla::gfx::ScaledFont*> mAzureScaledFontClearType;
  mozilla::Atomic<mozilla::gfx::ScaledFont*> mAzureScaledFontGDIClearType;

  bool UsingClearType() {
    return mozilla::gfx::gfxVars::SystemTextQuality() == CLEARTYPE_QUALITY;
  }
};

#endif
