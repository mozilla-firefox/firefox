/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxFT2FontBase.h"
#include "gfxFT2Utils.h"
#include "harfbuzz/hb.h"
#include "mozilla/Likely.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "gfxFontConstants.h"
#include "gfxFontUtils.h"
#include "gfxHarfBuzzShaper.h"
#include <algorithm>
#include <dlfcn.h>
#include <limits>

#include FT_TRUETYPE_TAGS_H
#include FT_TRUETYPE_TABLES_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H

#ifndef FT_LOAD_COLOR
#  define FT_LOAD_COLOR (1L << 20)
#endif
#ifndef FT_FACE_FLAG_COLOR
#  define FT_FACE_FLAG_COLOR (1L << 14)
#endif

using namespace mozilla;
using namespace mozilla::gfx;

gfxFT2FontBase::gfxFT2FontBase(
    const RefPtr<UnscaledFontFreeType>& aUnscaledFont,
    RefPtr<mozilla::gfx::SharedFTFace>&& aFTFace, gfxFontEntry* aFontEntry,
    const gfxFontStyle* aFontStyle, int aLoadFlags, bool aEmbolden)
    : gfxFont(aUnscaledFont, aFontEntry, aFontStyle, kAntialiasDefault),
      mFTFace(std::move(aFTFace)),
      mFTLoadFlags(aLoadFlags | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH |
                   FT_LOAD_COLOR),
      mEmbolden(aEmbolden),
      mFTSize(0.0) {}

gfxFT2FontBase::~gfxFT2FontBase() { mFTFace->ForgetLockOwner(this); }

FT_Face gfxFT2FontBase::LockFTFace() const
    MOZ_CAPABILITY_ACQUIRE(mFTFace) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  if (!mFTFace->Lock(this)) {
    FT_Set_Transform(mFTFace->GetFace(), nullptr, nullptr);

    // This more closely matches the conversion to fixed-point happening in
    // Cairo and Skia. Avoid overflow during the conversion. FT_Set_Char_Size
    // may still decide the value is out of range and reject the scale as being
    // too large or not matching a strike.
    FT_F26Dot6 charSize =
        (int32_t)std::min(std::max(mFTSize * 64.0 + 0.5, 0.0),
                          (double)std::numeric_limits<int32_t>::max());
    FT_Error error =
        FT_Set_Char_Size(mFTFace->GetFace(), charSize, charSize, 0, 0);
    if (error) {
      // Even if this returns null, caller must ensure UnlockFTFace is called.
      return nullptr;
    }
  }
  return mFTFace->GetFace();
}

void gfxFT2FontBase::UnlockFTFace() const
    MOZ_CAPABILITY_RELEASE(mFTFace) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mFTFace->Unlock();
}

static FT_ULong GetTableSizeFromFTFace(SharedFTFace* aFace,
                                       uint32_t aTableTag) {
  if (!aFace) {
    return 0;
  }
  FT_ULong len = 0;
  if (FT_Load_Sfnt_Table(aFace->GetFace(), aTableTag, 0, nullptr, &len) != 0) {
    return 0;
  }
  return len;
}

bool gfxFT2FontEntryBase::FaceHasTable(SharedFTFace* aFace,
                                       uint32_t aTableTag) {
  return GetTableSizeFromFTFace(aFace, aTableTag) > 0;
}

nsresult gfxFT2FontEntryBase::CopyFaceTable(SharedFTFace* aFace,
                                            uint32_t aTableTag,
                                            nsTArray<uint8_t>& aBuffer) {
  FT_ULong length = GetTableSizeFromFTFace(aFace, aTableTag);
  if (!length) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (!aBuffer.SetLength(length, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  if (FT_Load_Sfnt_Table(aFace->GetFace(), aTableTag, 0, aBuffer.Elements(),
                         &length) != 0) {
    aBuffer.Clear();
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

uint32_t gfxFT2FontEntryBase::GetGlyph(uint32_t aCharCode,
                                       gfxFT2FontBase* aFont) {
  const uint32_t slotIndex = aCharCode % kNumCmapCacheSlots;
  {
    // Try to read a cached entry without taking an exclusive lock.
    AutoReadLock lock(mLock);
    if (mCmapCache) {
      const auto& slot = mCmapCache[slotIndex];
      if (slot.mCharCode == aCharCode) {
        return slot.mGlyphIndex;
      }
    }
  }

  // Create/update the charcode-to-glyphid cache.
  AutoWriteLock lock(mLock);

  // This cache algorithm and size is based on what is done in
  // cairo_scaled_font_text_to_glyphs and pango_fc_font_real_get_glyph.  I
  // think the concept is that adjacent characters probably come mostly from
  // one Unicode block.  This assumption is probably not so valid with
  // scripts with large character sets as used for East Asian languages.
  if (!mCmapCache) {
    mCmapCache = mozilla::MakeUnique<CmapCacheSlot[]>(kNumCmapCacheSlots);

    // Invalidate slot 0 by setting its char code to something that would
    // never end up in slot 0.  All other slots are already invalid
    // because they have mCharCode = 0 and a glyph for char code 0 will
    // always be in the slot 0.
    mCmapCache[0].mCharCode = 1;
  }

  auto& slot = mCmapCache[slotIndex];
  if (slot.mCharCode != aCharCode) {
    slot.mCharCode = aCharCode;
    slot.mGlyphIndex = gfxFT2LockedFace(aFont).GetGlyph(aCharCode);
  }
  return slot.mGlyphIndex;
}

size_t gfxFT2FontEntryBase::ComputedSizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) {
  size_t result = gfxFontEntry::ComputedSizeOfExcludingThis(aMallocSizeOf);

  if (const auto* data = GetUserFontData()) {
    if (data->FontData()) {
      result += aMallocSizeOf(data->FontData());
    }
  }

  return result;
}

// aScale is intended for a 16.16 x/y_scale of an FT_Size_Metrics
static inline FT_Long ScaleRoundDesignUnits(FT_Short aDesignMetric,
                                            FT_Fixed aScale) {
  FT_Long fixed26dot6 = FT_MulFix(aDesignMetric, aScale);
  return ROUND_26_6_TO_INT(fixed26dot6);
}

// Snap a line to pixels while keeping the center and size of the line as
// close to the original position as possible.
//
// Pango does similar snapping for underline and strikethrough when fonts are
// hinted, but nsCSSRendering::GetTextDecorationRectInternal always snaps the
// top and size of lines.  Optimizing the distance between the line and
// baseline is probably good for the gap between text and underline, but
// optimizing the center of the line is better for positioning strikethough.
static void SnapLineToPixels(gfxFloat& aOffset, gfxFloat& aSize) {
  gfxFloat snappedSize = std::max(floor(aSize + 0.5), 1.0);
  // Correct offset for change in size
  gfxFloat offset = aOffset - 0.5 * (aSize - snappedSize);
  // Snap offset
  aOffset = floor(offset + 0.5);
  aSize = snappedSize;
}

static inline gfxRect ScaleGlyphBounds(const IntRect& aBounds,
                                       gfxFloat aScale) {
  return gfxRect(FLOAT_FROM_26_6(aBounds.x) * aScale,
                 FLOAT_FROM_26_6(aBounds.y) * aScale,
                 FLOAT_FROM_26_6(aBounds.width) * aScale,
                 FLOAT_FROM_26_6(aBounds.height) * aScale);
}

/**
 * Get extents for a simple character representable by a single glyph.
 * The return value is the glyph id of that glyph or zero if no such glyph
 * exists.  aWidth/aBounds is only set when this returns a non-zero glyph id.
 * This is just for use during initialization, and doesn't use the width cache.
 */
uint32_t gfxFT2FontBase::GetCharExtents(uint32_t aChar, gfxFloat* aWidth,
                                        gfxRect* aBounds) {
  FT_UInt gid = GetGlyph(aChar);
  int32_t width;
  IntRect bounds;
  if (gid && GetFTGlyphExtents(gid, aWidth ? &width : nullptr,
                               aBounds ? &bounds : nullptr)) {
    if (aWidth) {
      *aWidth = FLOAT_FROM_16_16(width);
    }
    if (aBounds) {
      *aBounds = ScaleGlyphBounds(bounds, GetAdjustedSize() / mFTSize);
    }
    return gid;
  } else {
    return 0;
  }
}

/**
 * Find the closest available fixed strike size, if applicable, to the
 * desired font size.
 */
static double FindClosestSize(FT_Face aFace, double aSize) {
  // FT size selection does not actually support sizes smaller than 1 and will
  // clamp this internally, regardless of what is requested. Do the clamp here
  // instead so that glyph extents/font matrix scaling will compensate it, as
  // Cairo normally would.
  if (aSize < 1.0) {
    aSize = 1.0;
  }
  if (FT_IS_SCALABLE(aFace)) {
    return aSize;
  }
  double bestDist = DBL_MAX;
  FT_Int bestSize = -1;
  for (FT_Int i = 0; i < aFace->num_fixed_sizes; i++) {
    double dist = aFace->available_sizes[i].y_ppem / 64.0 - aSize;
    // If the previous best is smaller than the desired size, prefer
    // a bigger size. Otherwise, just choose whatever size is closest.
    if (bestDist < 0 ? dist >= bestDist : fabs(dist) <= bestDist) {
      bestDist = dist;
      bestSize = i;
    }
  }
  if (bestSize < 0) {
    return aSize;
  }
  return aFace->available_sizes[bestSize].y_ppem / 64.0;
}

void gfxFT2FontBase::InitMetrics() {
  mFUnitsConvFactor = 0.0;

  if (MOZ_UNLIKELY(mStyle.AdjustedSizeMustBeZero())) {
    memset(&mMetrics, 0, sizeof(mMetrics));  // zero initialize
    mSpaceGlyph = GetGlyph(' ');
    return;
  }

  if (FontSizeAdjust::Tag(mStyle.sizeAdjustBasis) !=
          FontSizeAdjust::Tag::None &&
      mStyle.sizeAdjust >= 0.0 && GetAdjustedSize() > 0.0 && mFTSize == 0.0) {
    // If font-size-adjust is in effect, we need to get metrics in order to
    // determine the aspect ratio, then compute the final adjusted size and
    // re-initialize metrics.
    // Setting mFTSize nonzero here ensures we will not recurse again; the
    // actual value will be overridden by FindClosestSize below.
    mFTSize = 1.0;
    InitMetrics();
    // Now do the font-size-adjust calculation and set the final size.
    gfxFloat aspect;
    switch (FontSizeAdjust::Tag(mStyle.sizeAdjustBasis)) {
      default:
        MOZ_ASSERT_UNREACHABLE("unhandled sizeAdjustBasis?");
        aspect = 0.0;
        break;
      case FontSizeAdjust::Tag::ExHeight:
        aspect = mMetrics.xHeight / mAdjustedSize;
        break;
      case FontSizeAdjust::Tag::CapHeight:
        aspect = mMetrics.capHeight / mAdjustedSize;
        break;
      case FontSizeAdjust::Tag::ChWidth:
        aspect =
            mMetrics.zeroWidth > 0.0 ? mMetrics.zeroWidth / mAdjustedSize : 0.5;
        break;
      case FontSizeAdjust::Tag::IcWidth:
      case FontSizeAdjust::Tag::IcHeight: {
        bool vertical = FontSizeAdjust::Tag(mStyle.sizeAdjustBasis) ==
                        FontSizeAdjust::Tag::IcHeight;
        gfxFloat advance = GetCharAdvance(kWaterIdeograph, vertical);
        aspect = advance > 0.0 ? advance / mAdjustedSize : 1.0;
        break;
      }
    }
    if (aspect > 0.0) {
      // If we created a shaper above (to measure glyphs), discard it so we
      // get a new one for the adjusted scaling.
      delete mHarfBuzzShaper.exchange(nullptr);
      mAdjustedSize = mStyle.GetAdjustedSize(aspect);
      // Ensure the FT_Face will be reconfigured for the new size next time we
      // need to use it.
      mFTFace->ForgetLockOwner(this);
    }
  }

  // Set mAdjustedSize if it hasn't already been set by a font-size-adjust
  // computation.
  mAdjustedSize = GetAdjustedSize();

  // Cairo metrics are normalized to em-space, so that whatever fixed size
  // might actually be chosen is factored out. They are then later scaled by
  // the font matrix to the target adjusted size. Stash the chosen closest
  // size here for later scaling of the metrics.
  mFTSize = FindClosestSize(mFTFace->GetFace(), GetAdjustedSize());

  // Explicitly lock the face so we can release it early before calling
  // back into Cairo below.
  FT_Face face = LockFTFace();

  if (MOZ_UNLIKELY(!face)) {
    // No face.  This unfortunate situation might happen if the font
    // file is (re)moved at the wrong time.
    const gfxFloat emHeight = GetAdjustedSize();
    mMetrics.emHeight = emHeight;
    mMetrics.maxAscent = mMetrics.emAscent = 0.8 * emHeight;
    mMetrics.maxDescent = mMetrics.emDescent = 0.2 * emHeight;
    mMetrics.maxHeight = emHeight;
    mMetrics.internalLeading = 0.0;
    mMetrics.externalLeading = 0.2 * emHeight;
    const gfxFloat spaceWidth = 0.5 * emHeight;
    mMetrics.spaceWidth = spaceWidth;
    mMetrics.maxAdvance = spaceWidth;
    mMetrics.aveCharWidth = spaceWidth;
    mMetrics.zeroWidth = spaceWidth;
    mMetrics.ideographicWidth = emHeight;
    const gfxFloat xHeight = 0.5 * emHeight;
    mMetrics.xHeight = xHeight;
    mMetrics.capHeight = mMetrics.maxAscent;
    const gfxFloat underlineSize = emHeight / 14.0;
    mMetrics.underlineSize = underlineSize;
    mMetrics.underlineOffset = -underlineSize;
    mMetrics.strikeoutOffset = 0.25 * emHeight;
    mMetrics.strikeoutSize = underlineSize;

    SanitizeMetrics(&mMetrics, false);
    UnlockFTFace();
    return;
  }

  const FT_Size_Metrics& ftMetrics = face->size->metrics;

  mMetrics.maxAscent = FLOAT_FROM_26_6(ftMetrics.ascender);
  mMetrics.maxDescent = -FLOAT_FROM_26_6(ftMetrics.descender);
  mMetrics.maxAdvance = FLOAT_FROM_26_6(ftMetrics.max_advance);
  gfxFloat lineHeight = FLOAT_FROM_26_6(ftMetrics.height);

  // Negative maxDescent here almost certainly indicates a font with a sign
  // error in the descent field of the 'hhea' table; invert it.
  if (mMetrics.maxDescent < 0.0) {
    mMetrics.maxDescent = -mMetrics.maxDescent;
  }

  gfxFloat emHeight;
  // Scale for vertical design metric conversion: pixels per design unit.
  // If this remains at 0.0, we can't use metrics from OS/2 etc.
  gfxFloat yScale = 0.0;
  if (FT_IS_SCALABLE(face)) {
    // Prefer FT_Size_Metrics::x_scale to x_ppem as x_ppem does not
    // have subpixel accuracy.
    //
    // FT_Size_Metrics::y_scale is in 16.16 fixed point format.  Its
    // (fractional) value is a factor that converts vertical metrics from
    // design units to units of 1/64 pixels, so that the result may be
    // interpreted as pixels in 26.6 fixed point format.
    mFUnitsConvFactor = FLOAT_FROM_26_6(FLOAT_FROM_16_16(ftMetrics.x_scale));
    yScale = FLOAT_FROM_26_6(FLOAT_FROM_16_16(ftMetrics.y_scale));
    emHeight = face->units_per_EM * yScale;
  } else {  // Not scalable.
    emHeight = ftMetrics.y_ppem;
    // FT_Face doc says units_per_EM and a bunch of following fields
    // are "only relevant to scalable outlines". If it's an sfnt,
    // we can get units_per_EM from the 'head' table instead; otherwise,
    // we don't have a unitsPerEm value so we can't compute/use yScale or
    // mFUnitsConvFactor (x scale).
    if (const TT_Header* head =
            static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, ft_sfnt_head))) {
      gfxFloat emUnit = head->Units_Per_EM;
      mFUnitsConvFactor = ftMetrics.x_ppem / emUnit;
      // Bug 1267909 - Even if the font is not explicitly scalable,
      // if the face has color bitmaps, it should be treated as scalable
      // and scaled to the desired size. Metrics based on y_ppem need
      // to be rescaled for the adjusted size. This makes metrics agree
      // with the scales we pass to Cairo for Fontconfig fonts.
      if (face->face_flags & FT_FACE_FLAG_COLOR) {
        emHeight = GetAdjustedSize();
        gfxFloat adjustScale = emHeight / ftMetrics.y_ppem;
        mMetrics.maxAscent *= adjustScale;
        mMetrics.maxDescent *= adjustScale;
        mMetrics.maxAdvance *= adjustScale;
        lineHeight *= adjustScale;
        mFUnitsConvFactor *= adjustScale;
      }
      yScale = emHeight / emUnit;
    }
  }

  TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));

  if (os2 && os2->sTypoAscender && yScale > 0.0) {
    mMetrics.emAscent = os2->sTypoAscender * yScale;
    mMetrics.emDescent = -os2->sTypoDescender * yScale;
    FT_Short typoHeight =
        os2->sTypoAscender - os2->sTypoDescender + os2->sTypoLineGap;
    lineHeight = typoHeight * yScale;

    // If the OS/2 fsSelection USE_TYPO_METRICS bit is set,
    // set maxAscent/Descent from the sTypo* fields instead of hhea.
    const uint16_t kUseTypoMetricsMask = 1 << 7;
    if ((os2->fsSelection & kUseTypoMetricsMask) ||
        // maxAscent/maxDescent get used for frame heights, and some fonts
        // don't have the HHEA table ascent/descent set (bug 279032).
        (mMetrics.maxAscent == 0.0 && mMetrics.maxDescent == 0.0)) {
      // We use NS_round here to parallel the pixel-rounded values that
      // freetype gives us for ftMetrics.ascender/descender.
      mMetrics.maxAscent = NS_round(mMetrics.emAscent);
      mMetrics.maxDescent = NS_round(mMetrics.emDescent);
    }
  } else {
    mMetrics.emAscent = mMetrics.maxAscent;
    mMetrics.emDescent = mMetrics.maxDescent;
  }

  // gfxFont::Metrics::underlineOffset is the position of the top of the
  // underline.
  //
  // FT_FaceRec documentation describes underline_position as "the
  // center of the underlining stem".  This was the original definition
  // of the PostScript metric, but in the PostScript table of OpenType
  // fonts the metric is "the top of the underline"
  // (http://www.microsoft.com/typography/otspec/post.htm), and FreeType
  // (up to version 2.3.7) doesn't make any adjustment.
  //
  // Therefore get the underline position directly from the table
  // ourselves when this table exists.  Use FreeType's metrics for
  // other (including older PostScript) fonts.
  if (face->underline_position && face->underline_thickness && yScale > 0.0) {
    mMetrics.underlineSize = face->underline_thickness * yScale;
    TT_Postscript* post =
        static_cast<TT_Postscript*>(FT_Get_Sfnt_Table(face, ft_sfnt_post));
    if (post && post->underlinePosition) {
      mMetrics.underlineOffset = post->underlinePosition * yScale;
    } else {
      mMetrics.underlineOffset =
          face->underline_position * yScale + 0.5 * mMetrics.underlineSize;
    }
  } else {  // No underline info.
    // Imitate Pango.
    mMetrics.underlineSize = emHeight / 14.0;
    mMetrics.underlineOffset = -mMetrics.underlineSize;
  }

  if (os2 && os2->yStrikeoutSize && os2->yStrikeoutPosition && yScale > 0.0) {
    mMetrics.strikeoutSize = os2->yStrikeoutSize * yScale;
    mMetrics.strikeoutOffset = os2->yStrikeoutPosition * yScale;
  } else {  // No strikeout info.
    mMetrics.strikeoutSize = mMetrics.underlineSize;
    // Use OpenType spec's suggested position for Roman font.
    mMetrics.strikeoutOffset =
        emHeight * 409.0 / 2048.0 + 0.5 * mMetrics.strikeoutSize;
  }
  SnapLineToPixels(mMetrics.strikeoutOffset, mMetrics.strikeoutSize);

  if (os2 && os2->sxHeight && yScale > 0.0) {
    mMetrics.xHeight = os2->sxHeight * yScale;
  } else {
    // CSS 2.1, section 4.3.2 Lengths: "In the cases where it is
    // impossible or impractical to determine the x-height, a value of
    // 0.5em should be used."
    mMetrics.xHeight = 0.5 * emHeight;
  }

  // aveCharWidth is used for the width of text input elements so be
  // liberal rather than conservative in the estimate.
  if (os2 && os2->xAvgCharWidth) {
    // Round to pixels as this is compared with maxAdvance to guess
    // whether this is a fixed width font.
    mMetrics.aveCharWidth =
        ScaleRoundDesignUnits(os2->xAvgCharWidth, ftMetrics.x_scale);
  } else {
    mMetrics.aveCharWidth = 0.0;  // updated below
  }

  if (os2 && os2->sCapHeight && yScale > 0.0) {
    mMetrics.capHeight = os2->sCapHeight * yScale;
  } else {
    mMetrics.capHeight = mMetrics.maxAscent;
  }

  // Release the face lock to safely load glyphs with GetCharExtents if
  // necessary without recursively locking.
  UnlockFTFace();

  gfxFloat width;
  mSpaceGlyph = GetCharExtents(' ', &width);
  if (mSpaceGlyph) {
    mMetrics.spaceWidth = width;
  } else {
    mMetrics.spaceWidth = mMetrics.maxAdvance;  // guess
  }

  if (GetCharExtents('0', &width)) {
    mMetrics.zeroWidth = width;
  } else {
    mMetrics.zeroWidth = -1.0;  // indicates not found
  }

  if (GetCharExtents(kWaterIdeograph, &width)) {
    mMetrics.ideographicWidth = width;
  } else {
    mMetrics.ideographicWidth = -1.0;
  }

  // If we didn't get a usable x-height or cap-height above, try measuring
  // specific glyphs. This can be affected by hinting, leading to erratic
  // behavior across font sizes and system configuration, so we prefer to
  // use the metrics directly from the font if possible.
  // Using glyph bounds for x-height or cap-height may not really be right,
  // if fonts have fancy swashes etc. For x-height, CSS 2.1 suggests possibly
  // using the height of an "o", which may be more consistent across fonts,
  // but then curve-overshoot should also be accounted for.
  gfxFloat xWidth;
  gfxRect xBounds;
  if (mMetrics.xHeight == 0.0) {
    if (GetCharExtents('x', &xWidth, &xBounds) && xBounds.y < 0.0) {
      mMetrics.xHeight = -xBounds.y;
      mMetrics.aveCharWidth = std::max(mMetrics.aveCharWidth, xWidth);
    }
  }

  if (mMetrics.capHeight == 0.0) {
    if (GetCharExtents('H', nullptr, &xBounds) && xBounds.y < 0.0) {
      mMetrics.capHeight = -xBounds.y;
    }
  }

  mMetrics.aveCharWidth = std::max(mMetrics.aveCharWidth, mMetrics.zeroWidth);
  if (mMetrics.aveCharWidth == 0.0) {
    mMetrics.aveCharWidth = mMetrics.spaceWidth;
  }
  // Apparently hinting can mean that max_advance is not always accurate.
  mMetrics.maxAdvance = std::max(mMetrics.maxAdvance, mMetrics.aveCharWidth);

  mMetrics.maxHeight = mMetrics.maxAscent + mMetrics.maxDescent;

  // Make the line height an integer number of pixels so that lines will be
  // equally spaced (rather than just being snapped to pixels, some up and
  // some down).  Layout calculates line height from the emHeight +
  // internalLeading + externalLeading, but first each of these is rounded
  // to layout units.  To ensure that the result is an integer number of
  // pixels, round each of the components to pixels.
  mMetrics.emHeight = floor(emHeight + 0.5);

  // maxHeight will normally be an integer, but round anyway in case
  // FreeType is configured differently.
  mMetrics.internalLeading =
      floor(mMetrics.maxHeight - mMetrics.emHeight + 0.5);

  // Text input boxes currently don't work well with lineHeight
  // significantly less than maxHeight (with Verdana, for example).
  lineHeight = floor(std::max(lineHeight, mMetrics.maxHeight) + 0.5);
  mMetrics.externalLeading =
      lineHeight - mMetrics.internalLeading - mMetrics.emHeight;

  // Ensure emAscent + emDescent == emHeight
  gfxFloat sum = mMetrics.emAscent + mMetrics.emDescent;
  mMetrics.emAscent =
      sum > 0.0 ? mMetrics.emAscent * mMetrics.emHeight / sum : 0.0;
  mMetrics.emDescent = mMetrics.emHeight - mMetrics.emAscent;

  SanitizeMetrics(&mMetrics, false);

#if 0
    //    printf("font name: %s %f\n", NS_ConvertUTF16toUTF8(GetName()).get(), GetStyle()->size);
    //    printf ("pango font %s\n", pango_font_description_to_string (pango_font_describe (font)));

    fprintf (stderr, "Font: %s\n", GetName().get());
    fprintf (stderr, "    emHeight: %f emAscent: %f emDescent: %f\n", mMetrics.emHeight, mMetrics.emAscent, mMetrics.emDescent);
    fprintf (stderr, "    maxAscent: %f maxDescent: %f\n", mMetrics.maxAscent, mMetrics.maxDescent);
    fprintf (stderr, "    internalLeading: %f externalLeading: %f\n", mMetrics.externalLeading, mMetrics.internalLeading);
    fprintf (stderr, "    spaceWidth: %f aveCharWidth: %f xHeight: %f\n", mMetrics.spaceWidth, mMetrics.aveCharWidth, mMetrics.xHeight);
    fprintf (stderr, "    ideographicWidth: %f\n", mMetrics.ideographicWidth);
    fprintf (stderr, "    uOff: %f uSize: %f stOff: %f stSize: %f\n", mMetrics.underlineOffset, mMetrics.underlineSize, mMetrics.strikeoutOffset, mMetrics.strikeoutSize);
#endif
}

uint32_t gfxFT2FontBase::GetGlyph(uint32_t unicode,
                                  uint32_t variation_selector) {
  if (variation_selector) {
    uint32_t id =
        gfxFT2LockedFace(this).GetUVSGlyph(unicode, variation_selector);
    if (id) {
      return id;
    }
    unicode = gfxFontUtils::GetUVSFallback(unicode, variation_selector);
    if (unicode) {
      return GetGlyph(unicode);
    }
    return 0;
  }

  return GetGlyph(unicode);
}

bool gfxFT2FontBase::ShouldRoundXOffset(cairo_t* aCairo) const {
  // Force rounding if outputting to a Cairo context or if requested by pref to
  // disable subpixel positioning. Otherwise, allow subpixel positioning (no
  // rounding) if rendering a scalable outline font with anti-aliasing.
  // Monochrome rendering or some bitmap fonts can become too distorted with
  // subpixel positioning, so force rounding in those cases. Also be careful not
  // to use subpixel positioning if the user requests full hinting via
  // Fontconfig, which we detect by checking that neither hinting was disabled
  // nor light hinting was requested. Allow pref to force subpixel positioning
  // on even if full hinting was requested.
  return MOZ_UNLIKELY(
             StaticPrefs::
                 gfx_text_subpixel_position_force_disabled_AtStartup()) ||
         aCairo != nullptr || !mFTFace || !FT_IS_SCALABLE(mFTFace->GetFace()) ||
         (mFTLoadFlags & FT_LOAD_MONOCHROME) ||
         !((mFTLoadFlags & FT_LOAD_NO_HINTING) ||
           FT_LOAD_TARGET_MODE(mFTLoadFlags) == FT_RENDER_MODE_LIGHT ||
           MOZ_UNLIKELY(
               StaticPrefs::
                   gfx_text_subpixel_position_force_enabled_AtStartup()));
}

FT_Vector gfxFT2FontBase::GetEmboldenStrength(FT_Face aFace) const {
  FT_Vector strength = {0, 0};
  if (!mEmbolden) {
    return strength;
  }

  // If it's an outline glyph, we'll be using mozilla_glyphslot_embolden_less
  // (see gfx/wr/webrender/src/platform/unix/font.rs), so we need to match its
  // emboldening strength here.
  if (aFace->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    strength.x =
        FT_MulFix(aFace->units_per_EM, aFace->size->metrics.y_scale) / 48;
    strength.y = strength.x;
    return strength;
  }

  // This is the embolden "strength" used by FT_GlyphSlot_Embolden.
  strength.x =
      FT_MulFix(aFace->units_per_EM, aFace->size->metrics.y_scale) / 24;
  strength.y = strength.x;
  if (aFace->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    strength.x &= -64;
    if (!strength.x) {
      strength.x = 64;
    }
    strength.y &= -64;
  }
  return strength;
}

bool gfxFT2FontBase::GetFTGlyphExtents(uint16_t aGID, int32_t* aAdvance,
                                       IntRect* aBounds) {
  gfxFT2LockedFace face(this);
  MOZ_ASSERT(face.get());
  if (!face.get()) {
    // Failed to get the FT_Face? Give up already.
    NS_WARNING("failed to get FT_Face!");
    return false;
  }

  FT_Int32 flags = mFTLoadFlags;
  if (!aBounds) {
    flags |= FT_LOAD_ADVANCE_ONLY;
  }

  // Whether to disable subpixel positioning
  bool roundX = ShouldRoundXOffset(nullptr);

  // Workaround for FT_Load_Glyph not setting linearHoriAdvance for SVG glyphs.
  // See https://gitlab.freedesktop.org/freetype/freetype/-/issues/1156.
  if (!roundX &&
      GetFontEntry()->HasFontTable(TRUETYPE_TAG('S', 'V', 'G', ' '))) {
    flags &= ~FT_LOAD_COLOR;
  }

  if (Factory::LoadFTGlyph(face.get(), aGID, flags) != FT_Err_Ok) {
    // FT_Face was somehow broken/invalid? Don't try to access glyph slot.
    // This probably shouldn't happen, but does: see bug 1440938.
    NS_WARNING("failed to load glyph!");
    return false;
  }

  // Whether to interpret hinting settings (i.e. not printing)
  bool hintMetrics = ShouldHintMetrics();
  // No hinting disables X and Y hinting. Light disables only X hinting.
  bool unhintedY = (mFTLoadFlags & FT_LOAD_NO_HINTING) != 0;
  bool unhintedX =
      unhintedY || FT_LOAD_TARGET_MODE(mFTLoadFlags) == FT_RENDER_MODE_LIGHT;

  // Normalize out the loaded FT glyph size and then scale to the actually
  // desired size, in case these two sizes differ.
  gfxFloat extentsScale = GetAdjustedSize() / mFTSize;

  FT_Vector bold = GetEmboldenStrength(face.get());

  // Due to freetype bug 52683 we MUST use the linearHoriAdvance field when
  // dealing with a variation font; also use it for scalable fonts when not
  // applying hinting. Otherwise, prefer hinted width from glyph->advance.x.
  if (aAdvance) {
    FT_Fixed advance;
    if (!roundX || FT_HAS_MULTIPLE_MASTERS(face.get())) {
      advance = face.get()->glyph->linearHoriAdvance;
    } else {
      advance = face.get()->glyph->advance.x << 10;  // convert 26.6 to 16.16
    }
    if (advance) {
      advance += bold.x << 10;  // convert 26.6 to 16.16
    }
    // Hinting was requested, but FT did not apply any hinting to the metrics.
    // Round the advance here to approximate hinting as Cairo does. This must
    // happen BEFORE we apply the glyph extents scale, just like FT hinting
    // would.
    if (hintMetrics && roundX && unhintedX) {
      advance = (advance + 0x8000) & 0xffff0000u;
    }
    *aAdvance = NS_lround(advance * extentsScale);
  }

  if (aBounds) {
    const FT_Glyph_Metrics& metrics = face.get()->glyph->metrics;
    FT_F26Dot6 x = metrics.horiBearingX;
    FT_F26Dot6 y = -metrics.horiBearingY;
    FT_F26Dot6 x2 = x + metrics.width;
    FT_F26Dot6 y2 = y + metrics.height;
    // Synthetic bold moves the glyph top and right boundaries.
    y -= bold.y;
    x2 += bold.x;
    if (hintMetrics) {
      if (roundX && unhintedX) {
        x &= -64;
        x2 = (x2 + 63) & -64;
      }
      if (unhintedY) {
        y &= -64;
        y2 = (y2 + 63) & -64;
      }
    }
    *aBounds = IntRect(x, y, x2 - x, y2 - y);

    // Color fonts may not have reported the right bounds here, if there wasn't
    // an outline for the nominal glyph ID.
    // In principle we could use COLRFonts::GetColorGlyphBounds to retrieve the
    // true bounds of the rendering, but that's more expensive; probably better
    // to just use the font-wide ascent/descent as a heuristic that will
    // generally ensure everything gets rendered.
    if (aBounds->IsEmpty() &&
        GetFontEntry()->HasFontTable(TRUETYPE_TAG('C', 'O', 'L', 'R'))) {
      const auto& fm = GetMetrics(nsFontMetrics::eHorizontal);
      // aBounds is stored as FT_F26Dot6, so scale values from `fm` by 64.
      aBounds->y = int32_t(-NS_round(fm.maxAscent * 64.0));
      aBounds->height =
          int32_t(NS_round((fm.maxAscent + fm.maxDescent) * 64.0));
      aBounds->x = 0;
      aBounds->width =
          int32_t(aAdvance ? *aAdvance : NS_round(fm.maxAdvance * 64.0));
    }
  }

  return true;
}

/**
 * Get the cached glyph metrics for the glyph id if available. Otherwise, query
 * FreeType for the glyph extents and initialize the glyph metrics.
 */
gfxFT2FontBase::GlyphMetrics gfxFT2FontBase::GetCachedGlyphMetrics(
    uint16_t aGID, IntRect* aBounds) {
  {
    // Try to read cached metrics without exclusive locking.
    AutoReadLock lock(mLock);
    if (mGlyphMetrics) {
      if (auto metrics = mGlyphMetrics->Lookup(aGID)) {
        return metrics.Data();
      }
    }
  }

  // We need to create/update the cache.
  AutoWriteLock lock(mLock);
  if (!mGlyphMetrics) {
    mGlyphMetrics =
        mozilla::MakeUnique<nsTHashMap<nsUint32HashKey, GlyphMetrics>>(128);
  }

  return mGlyphMetrics->LookupOrInsertWith(aGID, [&] {
    GlyphMetrics metrics;
    IntRect bounds;
    if (GetFTGlyphExtents(aGID, &metrics.mAdvance, &bounds)) {
      metrics.SetBounds(bounds);
      if (aBounds) {
        *aBounds = bounds;
      }
    }
    return metrics;
  });
}

bool gfxFT2FontBase::GetGlyphBounds(uint16_t aGID, gfxRect* aBounds,
                                    bool aTight) {
  IntRect bounds;
  const GlyphMetrics metrics = GetCachedGlyphMetrics(aGID, &bounds);
  if (!metrics.HasValidBounds()) {
    return false;
  }
  // Check if there are cached bounds and use those if available. Otherwise,
  // fall back to directly querying the glyph extents.
  if (metrics.HasCachedBounds()) {
    bounds = metrics.GetBounds();
  } else if (bounds.IsEmpty() && !GetFTGlyphExtents(aGID, nullptr, &bounds)) {
    return false;
  }
  // The bounds are stored unscaled, so must be scaled to the adjusted size.
  *aBounds = ScaleGlyphBounds(bounds, GetAdjustedSize() / mFTSize);
  return true;
}

// For variation fonts, figure out the variation coordinates to be applied
// for each axis, in freetype's order (which may not match the order of
// axes in mStyle.variationSettings, so we need to search by axis tag).
/*static*/
void gfxFT2FontBase::SetupVarCoords(
    FT_MM_Var* aMMVar, const nsTArray<gfxFontVariation>& aVariations,
    FT_Face aFTFace) {
  if (!aMMVar) {
    return;
  }

  nsTArray<FT_Fixed> coords;
  for (unsigned i = 0; i < aMMVar->num_axis; ++i) {
    coords.AppendElement(aMMVar->axis[i].def);
    for (const auto& v : aVariations) {
      if (aMMVar->axis[i].tag == v.mTag) {
        FT_Fixed val = v.mValue * 0x10000;
        val = std::min(val, aMMVar->axis[i].maximum);
        val = std::max(val, aMMVar->axis[i].minimum);
        coords[i] = val;
        break;
      }
    }
  }

  if (!coords.IsEmpty()) {
#if MOZ_TREE_FREETYPE
    FT_Set_Var_Design_Coordinates(aFTFace, coords.Length(), coords.Elements());
#else
    typedef FT_Error (*SetCoordsFunc)(FT_Face, FT_UInt, FT_Fixed*);
    static SetCoordsFunc setCoords;
    static bool firstTime = true;
    if (firstTime) {
      firstTime = false;
      setCoords =
          (SetCoordsFunc)dlsym(RTLD_DEFAULT, "FT_Set_Var_Design_Coordinates");
    }
    if (setCoords) {
      (*setCoords)(aFTFace, coords.Length(), coords.Elements());
    }
#endif
  }
}
