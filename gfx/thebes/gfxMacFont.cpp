/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxMacFont.h"

#include <algorithm>

#include "AppleUtils.h"
#include "CoreTextFontList.h"
#include "cairo-quartz.h"
#include "gfxContext.h"
#include "gfxFontConstants.h"
#include "gfxFontUtils.h"
#include "gfxHarfBuzzShaper.h"
#include "gfxPlatformMac.h"
#include "gfxTextRun.h"
#include "gfxUtils.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/ScaledFontMac.h"

using namespace mozilla;
using namespace mozilla::gfx;

struct AxisTagEquals {
  bool Equals(const gfxFontVariationAxis& aIter, uint32_t aTag) const {
    return aIter.mTag == aTag;
  }
};

struct VariationTagEquals {
  bool Equals(const gfxFontVariation& aIter, uint32_t aTag) const {
    return aIter.tag == aTag;
  }
};

gfxMacFont::gfxMacFont(const RefPtr<UnscaledFontMac>& aUnscaledFont,
                       CTFontEntry* aFontEntry, const gfxFontStyle* aFontStyle)
    : gfxFont(aUnscaledFont, aFontEntry, aFontStyle),
      mCGFont(nullptr),
      mCTFont(nullptr),
      mVariationFont(aFontEntry->HasVariations()) {
  mApplySyntheticBold = aFontStyle->NeedsSyntheticBold(aFontEntry);

  if (mVariationFont) {
    CGFontRef baseFont = aUnscaledFont->GetFont();
    if (!baseFont) {
      return;
    }

    // Get the variation settings needed to instantiate the fontEntry
    // for a particular fontStyle.
    AutoTArray<gfxFontVariation, 4> vars;
    aFontEntry->GetVariationsForStyle(vars, *aFontStyle);

    if (aFontEntry->HasOpticalSize()) {
      // Because of a Core Text bug, we need to ensure that if the font has
      // an 'opsz' axis, it is always explicitly set, and NOT to the font's
      // default value. (See bug 1457417, bug 1478720.)
      // We record the result of searching the font's axes in the font entry,
      // so that this only has to be done by the first instance created for
      // a given font resource.
      const uint32_t kOpszTag = HB_TAG('o', 'p', 's', 'z');
      const float kOpszFudgeAmount = 0.01f;

      // Record the opsz axis details in the font entry, if not already done.
      if (!aFontEntry->mOpszAxis.mTag) {
        AutoTArray<gfxFontVariationAxis, 4> axes;
        aFontEntry->GetVariationAxes(axes);
        auto index = axes.IndexOf(kOpszTag, 0, AxisTagEquals());
        MOZ_ASSERT(index != axes.NoIndex);
        if (index != axes.NoIndex) {
          AutoWriteLock lock(aFontEntry->mLock);
          if (!aFontEntry->mOpszAxis.mTag) {
            const auto& axis = axes[index];
            // Pick a slightly-adjusted version of the default that we'll
            // use to work around Core Text's habit of ignoring any attempt
            // to explicitly set the default value.
            aFontEntry->mAdjustedDefaultOpsz =
                axis.mDefaultValue == axis.mMinValue
                    ? axis.mDefaultValue + kOpszFudgeAmount
                    : axis.mDefaultValue - kOpszFudgeAmount;
            aFontEntry->mOpszAxis = axis;
          }
        }
      }

      // Add 'opsz' if not present, or tweak its value if it looks too close
      // to the default (after clamping to the font's available range).
      auto index = vars.IndexOf(kOpszTag, 0, VariationTagEquals());
      if (index == vars.NoIndex) {
        // No explicit opsz; set to the font's default.
        vars.AppendElement(
            gfxFontVariation{kOpszTag, aFontEntry->mAdjustedDefaultOpsz});
      } else {
        // An 'opsz' value was already present; use it, but adjust if necessary
        // to a "safe" value that Core Text won't ignore.
        auto& value = vars[index].value;
        auto& axis = aFontEntry->mOpszAxis;
        value = fmin(fmax(value, axis.mMinValue), axis.mMaxValue);
        if (std::abs(value - axis.mDefaultValue) < kOpszFudgeAmount) {
          value = aFontEntry->mAdjustedDefaultOpsz;
        }
      }
    }

    static_assert(
        sizeof(gfxFontVariation) == sizeof(wr::FontVariation) &&
            offsetof(gfxFontVariation, tag) ==
                offsetof(wr::FontVariation, tag) &&
            offsetof(gfxFontVariation, value) == offsetof(FontVariation, value),
        "gfxFontVariation vs Moz2D FontVariation struct mismatch!");
    mCGFont = UnscaledFontMac::CreateCGFontWithVariations(
        baseFont, aUnscaledFont->CGAxesCache(), aUnscaledFont->CTAxesCache(),
        vars.Length(),
        reinterpret_cast<const wr::FontVariation*>(vars.Elements()));
    if (!mCGFont) {
      ::CFRetain(baseFont);
      mCGFont = baseFont;
    }
  } else {
    mCGFont = aUnscaledFont->GetFont();
    if (!mCGFont) {
      return;
    }
    ::CFRetain(mCGFont);
  }
}

gfxMacFont::~gfxMacFont() {
  if (mCGFont) {
    ::CFRelease(mCGFont);
  }
  if (mCTFont) {
    ::CFRelease(mCTFont);
  }
}

gfxFont::RunMetrics gfxMacFont::Measure(const gfxTextRun* aTextRun,
                                        uint32_t aStart, uint32_t aEnd,
                                        BoundingBoxType aBoundingBoxType,
                                        DrawTarget* aRefDrawTarget,
                                        Spacing* aSpacing,
                                        nscoord aLetterSpacing,
                                        gfx::ShapedTextFlags aOrientation) {
  gfxFont::RunMetrics metrics =
      gfxFont::Measure(aTextRun, aStart, aEnd, aBoundingBoxType, aRefDrawTarget,
                       aSpacing, aLetterSpacing, aOrientation);

  // if aBoundingBoxType is not TIGHT_HINTED_OUTLINE_EXTENTS then we need to add
  // a pixel column each side of the bounding box in case of antialiasing
  // "bleed"
  if (aBoundingBoxType != TIGHT_HINTED_OUTLINE_EXTENTS &&
      metrics.mBoundingBox.width > 0) {
    metrics.mBoundingBox.x -= aTextRun->GetAppUnitsPerDevUnit();
    metrics.mBoundingBox.width += aTextRun->GetAppUnitsPerDevUnit() * 2;
  }

  return metrics;
}

gfxFloat gfxMacFont::GetCharWidth(CFDataRef aCmap, char16_t aUniChar,
                                  uint32_t* aGlyphID, gfxFloat aConvFactor) {
  CGGlyph glyph = 0;

  if (aCmap) {
    glyph = gfxFontUtils::MapCharToGlyph(::CFDataGetBytePtr(aCmap),
                                         ::CFDataGetLength(aCmap), aUniChar);
  }

  if (aGlyphID) {
    *aGlyphID = glyph;
  }

  if (glyph) {
    int advance;
    if (::CGFontGetGlyphAdvances(mCGFont, &glyph, 1, &advance)) {
      return advance * aConvFactor;
    }
  }

  return 0;
}

int32_t gfxMacFont::GetGlyphWidth(uint16_t aGID) {
  if (mVariationFont) {
    // Avoid a potential Core Text crash (bug 1450209) by using
    // CoreGraphics glyph advance API. This is inferior for 'sbix'
    // fonts, but those won't have variations, so it's OK.
    int cgAdvance;
    if (::CGFontGetGlyphAdvances(mCGFont, &aGID, 1, &cgAdvance)) {
      return cgAdvance * mFUnitsConvFactor * 0x10000;
    }
  }

  if (!mCTFont) {
    bool isInstalledFont =
        !mFontEntry->IsUserFont() || mFontEntry->IsLocalUserFont();
    mCTFont = CreateCTFontFromCGFontWithVariations(mCGFont, mAdjustedSize,
                                                   isInstalledFont);
    if (!mCTFont) {  // shouldn't happen, but let's be safe
      NS_WARNING("failed to create CTFontRef to measure glyph width");
      return 0;
    }
  }

  CGSize advance;
  ::CTFontGetAdvancesForGlyphs(mCTFont, kCTFontOrientationDefault, &aGID,
                               &advance, 1);
  return advance.width * 0x10000;
}

bool gfxMacFont::GetGlyphBounds(uint16_t aGID, gfxRect* aBounds, bool aTight) {
  CGRect bb;
  if (!::CGFontGetGlyphBBoxes(mCGFont, &aGID, 1, &bb)) {
    return false;
  }

  // broken fonts can return incorrect bounds for some null characters,
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=534260
  if (bb.origin.x == -32767 && bb.origin.y == -32767 &&
      bb.size.width == 65534 && bb.size.height == 65534) {
    *aBounds = gfxRect(0, 0, 0, 0);
    return true;
  }

  gfxRect bounds(bb.origin.x, -(bb.origin.y + bb.size.height), bb.size.width,
                 bb.size.height);
  bounds.Scale(mFUnitsConvFactor);

  // For bitmap fonts (like Apple Color Emoji), CoreGraphics does not return
  // accurate bounds, so to try and avoid clipping when the bounds are used
  // to determine the area to render (e.g. when implementing canvas2d filters),
  // we inflate the bounds based on global metrics from the font.
  if (GetFontEntry()->HasColorBitmapTable()) {
    aBounds->x = std::min(bounds.x, 0.0);
    aBounds->width = std::max(bounds.width, mMetrics.maxAdvance);
    // Note that y-coordinates are downwards here, and bounds.y is MINUS the
    // glyph ascent as it measures from the baseline.
    aBounds->y = std::min(bounds.y, -mMetrics.maxAscent);
    aBounds->height =
        std::max(bounds.YMost(), mMetrics.maxDescent) - aBounds->y;
  } else {
    *aBounds = bounds;
  }

  return true;
}

bool gfxMacFont::InitMetricsFromPlatform() {
  if (InitMetricsFromSfntTables()) {
    return true;
  }

  // For downloaded webfonts, we expect the sfnt-based code to always work;
  // only the (rare) case of locally-installed, non-sfnt fonts should come here.
  if (mFontEntry->IsUserFont() && !mFontEntry->IsLocalUserFont()) {
    return false;
  }

  AutoCFTypeRef<CTFontRef> ctFont(
      ::CTFontCreateWithGraphicsFont(mCGFont, mAdjustedSize, nullptr, nullptr));
  if (!ctFont) {
    return false;
  }

  mMetrics.underlineOffset = ::CTFontGetUnderlinePosition(ctFont);
  mMetrics.underlineSize = ::CTFontGetUnderlineThickness(ctFont);

  mMetrics.externalLeading = ::CTFontGetLeading(ctFont);

  mMetrics.maxAscent = ::CTFontGetAscent(ctFont);
  mMetrics.maxDescent = ::CTFontGetDescent(ctFont);

  // this is not strictly correct, but neither CTFont nor CGFont seems to
  // provide maxAdvance, unless we were to iterate over all the glyphs
  // (which isn't worth the cost here)
  CGRect r = ::CTFontGetBoundingBox(ctFont);
  mMetrics.maxAdvance = r.size.width;

  mMetrics.xHeight = ::CTFontGetXHeight(ctFont);
  mMetrics.capHeight = ::CTFontGetCapHeight(ctFont);

  // For CFF fonts, when scaling values read from CGFont* APIs, we need to
  // use CG's idea of unitsPerEm, which may differ from the "true" value in
  // the head table of the font (see bug 580863)
  gfxFloat cgConvFactor;
  if (static_cast<CTFontEntry*>(mFontEntry.get())->IsCFF()) {
    cgConvFactor = mAdjustedSize / ::CGFontGetUnitsPerEm(mCGFont);
  } else {
    cgConvFactor = mFUnitsConvFactor;
  }

  if (mMetrics.xHeight == 0.0) {
    mMetrics.xHeight = ::CGFontGetXHeight(mCGFont) * cgConvFactor;
  }
  if (mMetrics.capHeight == 0.0) {
    mMetrics.capHeight = ::CGFontGetCapHeight(mCGFont) * cgConvFactor;
  }
  AutoCFTypeRef<CFDataRef> cmap(
      ::CGFontCopyTableForTag(mCGFont, TRUETYPE_TAG('c', 'm', 'a', 'p')));
  uint32_t glyphID;
  mMetrics.zeroWidth = GetCharWidth(cmap, '0', &glyphID, cgConvFactor);
  if (glyphID == 0) {
    mMetrics.zeroWidth = -1.0;  // indicates not found
  }
  mMetrics.ideographicWidth =
      GetCharWidth(cmap, kWaterIdeograph, &glyphID, cgConvFactor);
  if (glyphID == 0) {
    // Indicate "not found".
    mMetrics.ideographicWidth = -1.0;
  }
  // aveCharWidth is not provided via CTFont API, so try measuring 'x';
  // this could lead to less-than-"perfect" text field sizing when width is
  // specified as a number of characters, and the font in use is a non-sfnt
  // legacy font, but that's a sufficiently obscure edge case that we can
  // ignore the potential discrepancy.
  mMetrics.aveCharWidth = GetCharWidth(cmap, 'x', &glyphID, cgConvFactor);

  // Measure the space glyph, or use aveCharWidth as fallback if absent.
  mMetrics.spaceWidth = GetCharWidth(cmap, ' ', &glyphID, cgConvFactor);
  if (glyphID == 0) {
    mMetrics.spaceWidth = mMetrics.aveCharWidth;
  } else {
    mSpaceGlyph = glyphID;
  }

  return true;
}

void gfxMacFont::FinishInitFontMetrics() {
  // turn off font anti-aliasing based on user pref setting
  if (mAdjustedSize <=
      (gfxFloat)gfxPlatformMac::GetPlatform()->GetAntiAliasingThreshold()) {
    mAntialiasOption = kAntialiasNone;
  } else if (mStyle.useGrayscaleAntialiasing) {
    mAntialiasOption = kAntialiasGrayscale;
  }
}

already_AddRefed<ScaledFont> gfxMacFont::GetScaledFont(
    const TextRunDrawParams& aRunParams) {
  if (ScaledFont* scaledFont = mAzureScaledFont) {
    return do_AddRef(scaledFont);
  }

  gfxFontEntry* fe = GetFontEntry();
  bool hasColorGlyphs = fe->HasColorBitmapTable() || fe->TryGetColorGlyphs();
  RefPtr<ScaledFont> newScaledFont = Factory::CreateScaledFontForMacFont(
      GetCGFontRef(), GetUnscaledFont(), GetAdjustedSize(),
      !mStyle.useGrayscaleAntialiasing, ApplySyntheticBold(), hasColorGlyphs);
  if (!newScaledFont) {
    return nullptr;
  }

  InitializeScaledFont(newScaledFont);

  if (mAzureScaledFont.compareExchange(nullptr, newScaledFont.get())) {
    newScaledFont.forget().leak();
  }
  ScaledFont* scaledFont = mAzureScaledFont;
  return do_AddRef(scaledFont);
}

bool gfxMacFont::ShouldRoundXOffset(cairo_t* aCairo) const {
  // Quartz surfaces implement show_glyphs for Quartz fonts
  return aCairo && cairo_surface_get_type(cairo_get_target(aCairo)) !=
                       CAIRO_SURFACE_TYPE_QUARTZ;
}

bool gfxMacFont::UseNativeColrFontSupport() const {
  /*
    auto* colr = GetFontEntry()->GetCOLR();
    if (colr && COLRFonts::GetColrTableVersion(colr) == 0) {
      return true;
    }
  */
  return false;
}

void gfxMacFont::AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                        FontCacheSizes* aSizes) const {
  gfxFont::AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
  // mCGFont is shared with the font entry, so not counted here;
}

void gfxMacFont::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                        FontCacheSizes* aSizes) const {
  aSizes->mFontInstances += aMallocSizeOf(this);
  AddSizeOfExcludingThis(aMallocSizeOf, aSizes);
}
