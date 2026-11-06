/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontFontconfig.h"

#include <fontconfig/fcfreetype.h>

#include "HelpersSkia.h"
#include "Logging.h"
#include "UnscaledFontFreeType.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "skia/include/ports/SkTypeface_cairo.h"

#include FT_LCD_FILTER_H
#include FT_MULTIPLE_MASTERS_H

namespace mozilla::gfx {

ScaledFontFontconfig::ScaledFontFontconfig(
    RefPtr<SharedFTFace>&& aFace, FcPattern* aPattern,
    const RefPtr<UnscaledFont>& aUnscaledFont, Float aSize)
    : ScaledFontBase(aUnscaledFont, aSize),
      mFace(std::move(aFace)),
      mInstanceData(aPattern) {}

ScaledFontFontconfig::ScaledFontFontconfig(
    RefPtr<SharedFTFace>&& aFace, const InstanceData& aInstanceData,
    const RefPtr<UnscaledFont>& aUnscaledFont, Float aSize)
    : ScaledFontBase(aUnscaledFont, aSize),
      mFace(std::move(aFace)),
      mInstanceData(aInstanceData) {}

bool ScaledFontFontconfig::UseSubpixelPosition() const {
  return !MOZ_UNLIKELY(
             StaticPrefs::
                 gfx_text_subpixel_position_force_disabled_AtStartup()) &&
         mInstanceData.mAntialias != AntialiasMode::NONE &&
         FT_IS_SCALABLE(mFace->GetFace()) &&
         (mInstanceData.mHinting == FontHinting::NONE ||
          mInstanceData.mHinting == FontHinting::LIGHT ||
          MOZ_UNLIKELY(
              StaticPrefs::
                  gfx_text_subpixel_position_force_enabled_AtStartup()));
}

SkTypeface* ScaledFontFontconfig::CreateSkTypeface() {
  return SkCreateTypefaceFromCairoFTFont(mFace->GetFace(), mFace.get(),
                                         mInstanceData.mLcdFilter);
}

void ScaledFontFontconfig::SetupSkFontDrawOptions(SkFont& aFont) {
  aFont.setSubpixel(UseSubpixelPosition());

  if (mInstanceData.mFlags & InstanceData::AUTOHINT) {
    aFont.setForceAutoHinting(true);
  }
  if (mInstanceData.mFlags & InstanceData::EMBEDDED_BITMAP) {
    aFont.setEmbeddedBitmaps(true);
  }
  if (mInstanceData.mFlags & InstanceData::EMBOLDEN) {
    aFont.setEmbolden(true);
  }

  aFont.setHinting(GfxHintingToSkiaHinting(mInstanceData.mHinting));
}

bool ScaledFontFontconfig::MayUseBitmaps() {
  return mInstanceData.mFlags & InstanceData::EMBEDDED_BITMAP &&
         !FT_IS_SCALABLE(mFace->GetFace());
}

cairo_font_face_t* ScaledFontFontconfig::CreateCairoFontFace(
    cairo_font_options_t* aFontOptions) {
  int loadFlags;
  unsigned int synthFlags;
  mInstanceData.SetupFontOptions(aFontOptions, &loadFlags, &synthFlags);

  return cairo_ft_font_face_create_for_ft_face(mFace->GetFace(), loadFlags,
                                               synthFlags, mFace.get());
}

AntialiasMode ScaledFontFontconfig::GetDefaultAAMode() {
  return mInstanceData.mAntialias;
}

bool FcPatternAllowsBitmaps(FcPattern* aPattern, bool aAntialias,
                            bool aHinting) {
  if (!aAntialias) {
    // Always allow bitmaps when antialiasing is disabled
    return true;
  }
  FcBool bitmap;
  if (FcPatternGetBool(aPattern, FC_EMBEDDED_BITMAP, 0, &bitmap) !=
          FcResultMatch ||
      !bitmap) {
    // If bitmaps were explicitly disabled, then disallow them
    return false;
  }
  if (aHinting) {
    // If hinting is used and bitmaps were enabled, then allow them
    return true;
  }
  // When hinting is disabled, then avoid loading bitmaps from outline
  // fonts. However, emoji fonts may have no outlines while containing
  // bitmaps intended to be scaled, so still allow those.
  FcBool outline;
  if (FcPatternGetBool(aPattern, FC_OUTLINE, 0, &outline) == FcResultMatch &&
      outline) {
    return false;
  }
  FcBool scalable;
  if (FcPatternGetBool(aPattern, FC_SCALABLE, 0, &scalable) != FcResultMatch ||
      !scalable) {
    return false;
  }
  return true;
}

ScaledFontFontconfig::InstanceData::InstanceData(FcPattern* aPattern)
    : mFlags(0),
      mAntialias(AntialiasMode::NONE),
      mHinting(FontHinting::NONE),
      mLcdFilter(FT_LCD_FILTER_LEGACY) {
  // Record relevant Fontconfig properties into instance data.
  FcBool autohint;
  if (FcPatternGetBool(aPattern, FC_AUTOHINT, 0, &autohint) == FcResultMatch &&
      autohint) {
    mFlags |= AUTOHINT;
  }
  FcBool embolden;
  if (FcPatternGetBool(aPattern, FC_EMBOLDEN, 0, &embolden) == FcResultMatch &&
      embolden) {
    mFlags |= EMBOLDEN;
  }

  // For printer fonts, Cairo hint metrics and hinting will be disabled.
  // For other fonts, allow hint metrics and hinting.
  FcBool printing;
  if (FcPatternGetBool(aPattern, "gfx.printing", 0, &printing) !=
          FcResultMatch ||
      !printing) {
    mFlags |= HINT_METRICS;

    FcBool hinting;
    if (FcPatternGetBool(aPattern, FC_HINTING, 0, &hinting) != FcResultMatch ||
        hinting) {
      int hintstyle;
      if (FcPatternGetInteger(aPattern, FC_HINT_STYLE, 0, &hintstyle) !=
          FcResultMatch) {
        hintstyle = FC_HINT_FULL;
      }
      switch (hintstyle) {
        case FC_HINT_SLIGHT:
          mHinting = FontHinting::LIGHT;
          break;
        case FC_HINT_MEDIUM:
          mHinting = FontHinting::NORMAL;
          break;
        case FC_HINT_FULL:
          mHinting = FontHinting::FULL;
          break;
        case FC_HINT_NONE:
        default:
          break;
      }
    }
  }

  FcBool antialias;
  if (FcPatternGetBool(aPattern, FC_ANTIALIAS, 0, &antialias) ==
          FcResultMatch &&
      !antialias) {
    // If AA is explicitly disabled, leave bitmaps enabled.
    mFlags |= EMBEDDED_BITMAP;
  } else {
    mAntialias = AntialiasMode::GRAY;

    // Otherwise, if AA is enabled, disable embedded bitmaps unless explicitly
    // enabled.
    if (FcPatternAllowsBitmaps(aPattern, true, mHinting != FontHinting::NONE)) {
      mFlags |= EMBEDDED_BITMAP;
    }

    // Only record subpixel order and lcd filtering if antialiasing is enabled.
    int rgba;
    if (mFlags & HINT_METRICS &&
        FcPatternGetInteger(aPattern, FC_RGBA, 0, &rgba) == FcResultMatch) {
      switch (rgba) {
        case FC_RGBA_RGB:
        case FC_RGBA_BGR:
        case FC_RGBA_VRGB:
        case FC_RGBA_VBGR:
          mAntialias = AntialiasMode::SUBPIXEL;
          if (rgba == FC_RGBA_VRGB || rgba == FC_RGBA_VBGR) {
            mFlags |= LCD_VERTICAL;
          }
          if (rgba == FC_RGBA_BGR || rgba == FC_RGBA_VBGR) {
            mFlags |= SUBPIXEL_BGR;
          }
          break;
        case FC_RGBA_NONE:
        case FC_RGBA_UNKNOWN:
        default:
          break;
      }
    }

    int filter;
    if (mAntialias == AntialiasMode::SUBPIXEL &&
        FcPatternGetInteger(aPattern, FC_LCD_FILTER, 0, &filter) ==
            FcResultMatch) {
      switch (filter) {
        case FC_LCD_NONE:
          mLcdFilter = FT_LCD_FILTER_NONE;
          break;
        case FC_LCD_DEFAULT:
          mLcdFilter = FT_LCD_FILTER_DEFAULT;
          break;
        case FC_LCD_LIGHT:
          mLcdFilter = FT_LCD_FILTER_LIGHT;
          break;
        case FC_LCD_LEGACY:
        default:
          break;
      }
    }
  }
}

ScaledFontFontconfig::InstanceData::InstanceData(
    const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions)
    : mFlags(HINT_METRICS),
      mAntialias(AntialiasMode::NONE),
      mHinting(FontHinting::FULL),
      mLcdFilter(FT_LCD_FILTER_LEGACY) {
  if (aOptions) {
    if (aOptions->flags & wr::FontInstanceFlags::FORCE_AUTOHINT) {
      mFlags |= AUTOHINT;
    }
    if (aOptions->flags & wr::FontInstanceFlags::EMBEDDED_BITMAPS) {
      mFlags |= EMBEDDED_BITMAP;
    }
    if (aOptions->flags & wr::FontInstanceFlags::SYNTHETIC_BOLD) {
      mFlags |= EMBOLDEN;
    }
    if (aOptions->render_mode == wr::FontRenderMode::Subpixel) {
      mAntialias = AntialiasMode::SUBPIXEL;
      if (aOptions->flags & wr::FontInstanceFlags::SUBPIXEL_BGR) {
        mFlags |= SUBPIXEL_BGR;
      }
      if (aOptions->flags & wr::FontInstanceFlags::LCD_VERTICAL) {
        mFlags |= LCD_VERTICAL;
      }
    } else if (aOptions->render_mode != wr::FontRenderMode::Mono) {
      mAntialias = AntialiasMode::GRAY;
    }
  }
  if (aPlatformOptions) {
    switch (aPlatformOptions->hinting) {
      case wr::FontHinting::None:
        mHinting = FontHinting::NONE;
        break;
      case wr::FontHinting::Light:
        mHinting = FontHinting::LIGHT;
        break;
      case wr::FontHinting::Normal:
        mHinting = FontHinting::NORMAL;
        break;
      default:
        break;
    }
    switch (aPlatformOptions->lcd_filter) {
      case wr::FontLCDFilter::None:
        mLcdFilter = FT_LCD_FILTER_NONE;
        break;
      case wr::FontLCDFilter::Default:
        mLcdFilter = FT_LCD_FILTER_DEFAULT;
        break;
      case wr::FontLCDFilter::Light:
        mLcdFilter = FT_LCD_FILTER_LIGHT;
        break;
      default:
        break;
    }
  }
}

void ScaledFontFontconfig::InstanceData::SetupFontOptions(
    cairo_font_options_t* aFontOptions, int* aOutLoadFlags,
    unsigned int* aOutSynthFlags) const {
  // For regular (non-printer) fonts, enable hint metrics as well as hinting
  // and (possibly subpixel) antialiasing.
  cairo_font_options_set_hint_metrics(
      aFontOptions,
      mFlags & HINT_METRICS ? CAIRO_HINT_METRICS_ON : CAIRO_HINT_METRICS_OFF);

  cairo_hint_style_t hinting;
  switch (mHinting) {
    case FontHinting::NONE:
      hinting = CAIRO_HINT_STYLE_NONE;
      break;
    case FontHinting::LIGHT:
      hinting = CAIRO_HINT_STYLE_SLIGHT;
      break;
    case FontHinting::NORMAL:
      hinting = CAIRO_HINT_STYLE_MEDIUM;
      break;
    case FontHinting::FULL:
      hinting = CAIRO_HINT_STYLE_FULL;
      break;
  }
  cairo_font_options_set_hint_style(aFontOptions, hinting);

  switch (mAntialias) {
    case AntialiasMode::NONE:
      cairo_font_options_set_antialias(aFontOptions, CAIRO_ANTIALIAS_NONE);
      break;
    case AntialiasMode::GRAY:
    default:
      cairo_font_options_set_antialias(aFontOptions, CAIRO_ANTIALIAS_GRAY);
      break;
    case AntialiasMode::SUBPIXEL: {
      cairo_font_options_set_antialias(aFontOptions, CAIRO_ANTIALIAS_SUBPIXEL);
      cairo_font_options_set_subpixel_order(
          aFontOptions,
          mFlags & SUBPIXEL_BGR
              ? (mFlags & LCD_VERTICAL ? CAIRO_SUBPIXEL_ORDER_VBGR
                                       : CAIRO_SUBPIXEL_ORDER_BGR)
              : (mFlags & LCD_VERTICAL ? CAIRO_SUBPIXEL_ORDER_VRGB
                                       : CAIRO_SUBPIXEL_ORDER_RGB));
      cairo_lcd_filter_t lcdFilter = CAIRO_LCD_FILTER_DEFAULT;
      switch (mLcdFilter) {
        case FT_LCD_FILTER_NONE:
          lcdFilter = CAIRO_LCD_FILTER_NONE;
          break;
        case FT_LCD_FILTER_DEFAULT:
          lcdFilter = CAIRO_LCD_FILTER_FIR5;
          break;
        case FT_LCD_FILTER_LIGHT:
          lcdFilter = CAIRO_LCD_FILTER_FIR3;
          break;
        case FT_LCD_FILTER_LEGACY:
          lcdFilter = CAIRO_LCD_FILTER_INTRA_PIXEL;
          break;
      }
      cairo_font_options_set_lcd_filter(aFontOptions, lcdFilter);
      break;
    }
  }

  // Try to build a sane initial set of Cairo font options based on the
  // Fontconfig pattern.
  int loadFlags = FT_LOAD_DEFAULT;
  unsigned int synthFlags = 0;

  if (!(mFlags & EMBEDDED_BITMAP)) {
    loadFlags |= FT_LOAD_NO_BITMAP;
  }
  if (mFlags & AUTOHINT) {
    loadFlags |= FT_LOAD_FORCE_AUTOHINT;
  }
  if (mFlags & EMBOLDEN) {
    synthFlags |= CAIRO_FT_SYNTHESIZE_BOLD;
  }

  *aOutLoadFlags = loadFlags;
  *aOutSynthFlags = synthFlags;
}

bool ScaledFontFontconfig::GetFontInstanceData(FontInstanceDataOutput aCb,
                                               void* aBaton) {
  std::vector<wr::FontVariation> variations;
  if (HasVariationSettings()) {
    UnscaledFontFreeType::GetVariationSettingsFromFace(&variations, mFace);
  }

  aCb(reinterpret_cast<uint8_t*>(&mInstanceData), sizeof(mInstanceData),
      variations.data(), variations.size(), aBaton);
  return true;
}

bool ScaledFontFontconfig::GetWRFontInstanceOptions(
    Maybe<wr::FontInstanceOptions>* aOutOptions,
    Maybe<wr::FontInstancePlatformOptions>* aOutPlatformOptions,
    std::vector<wr::FontVariation>* aOutVariations) {
  wr::FontInstanceOptions options = {};
  options.render_mode = wr::FontRenderMode::Alpha;
  options.flags = wr::FontInstanceFlags{0};
  if (UseSubpixelPosition()) {
    options.flags |= wr::FontInstanceFlags::SUBPIXEL_POSITION;
  }
  options.synthetic_italics =
      wr::DegreesToSyntheticItalics(GetSyntheticObliqueAngle());

  wr::FontInstancePlatformOptions platformOptions;
  platformOptions.lcd_filter = wr::FontLCDFilter::Legacy;
  platformOptions.hinting = wr::FontHinting::Normal;

  if (mInstanceData.mFlags & InstanceData::AUTOHINT) {
    options.flags |= wr::FontInstanceFlags::FORCE_AUTOHINT;
  }
  if (mInstanceData.mFlags & InstanceData::EMBOLDEN) {
    options.flags |= wr::FontInstanceFlags::SYNTHETIC_BOLD;
  }
  if (mInstanceData.mFlags & InstanceData::EMBEDDED_BITMAP) {
    options.flags |= wr::FontInstanceFlags::EMBEDDED_BITMAPS;
  }
  if (mInstanceData.mAntialias != AntialiasMode::NONE) {
    if (mInstanceData.mAntialias == AntialiasMode::SUBPIXEL) {
      options.render_mode = wr::FontRenderMode::Subpixel;
      platformOptions.hinting = wr::FontHinting::LCD;
      if (mInstanceData.mFlags & InstanceData::LCD_VERTICAL) {
        options.flags |= wr::FontInstanceFlags::LCD_VERTICAL;
      }
      if (mInstanceData.mFlags & InstanceData::SUBPIXEL_BGR) {
        options.flags |= wr::FontInstanceFlags::SUBPIXEL_BGR;
      }
    }

    switch (mInstanceData.mLcdFilter) {
      case FT_LCD_FILTER_NONE:
        platformOptions.lcd_filter = wr::FontLCDFilter::None;
        break;
      case FT_LCD_FILTER_DEFAULT:
        platformOptions.lcd_filter = wr::FontLCDFilter::Default;
        break;
      case FT_LCD_FILTER_LIGHT:
        platformOptions.lcd_filter = wr::FontLCDFilter::Light;
        break;
      case FT_LCD_FILTER_LEGACY:
      default:
        break;
    }

    switch (mInstanceData.mHinting) {
      case FontHinting::NONE:
        platformOptions.hinting = wr::FontHinting::None;
        break;
      case FontHinting::LIGHT:
        platformOptions.hinting = wr::FontHinting::Light;
        break;
      case FontHinting::NORMAL:
        platformOptions.hinting = wr::FontHinting::Normal;
        break;
      case FontHinting::FULL:
        break;
    }
  } else {
    options.render_mode = wr::FontRenderMode::Mono;

    switch (mInstanceData.mHinting) {
      case FontHinting::NONE:
        platformOptions.hinting = wr::FontHinting::None;
        break;
      default:
        platformOptions.hinting = wr::FontHinting::Mono;
        break;
    }
  }

  platformOptions.gamma =
      int16_t(StaticPrefs::gfx_font_rendering_freetype_gamma());
  platformOptions.enhanced_contrast =
      int16_t(StaticPrefs::gfx_font_rendering_freetype_enhanced_contrast());

  *aOutOptions = Some(options);
  *aOutPlatformOptions = Some(platformOptions);

  if (HasVariationSettings()) {
    UnscaledFontFreeType::GetVariationSettingsFromFace(aOutVariations, mFace);
  }

  return true;
}

already_AddRefed<ScaledFont> UnscaledFontFontconfig::CreateScaledFont(
    Float aSize, const uint8_t* aInstanceData, uint32_t aInstanceDataLength,
    const wr::FontVariation* aVariations, uint32_t aNumVariations) {
  if (aInstanceDataLength < sizeof(ScaledFontFontconfig::InstanceData)) {
    gfxWarning() << "Fontconfig scaled font instance data is truncated.";
    return nullptr;
  }
  const ScaledFontFontconfig::InstanceData& instanceData =
      *reinterpret_cast<const ScaledFontFontconfig::InstanceData*>(
          aInstanceData);

  RefPtr<SharedFTFace> face(InitFace());
  if (!face) {
    gfxWarning() << "Attempted to deserialize Fontconfig scaled font without "
                    "FreeType face";
    return nullptr;
  }

  if (aNumVariations > 0) {
    if (RefPtr<SharedFTFace> varFace = face->CloneFace()) {
      face = varFace;
    }
    // Only apply variations if we have an explicitly cloned face.
    if (face != GetFace()) {
      ApplyVariationsToFace(aVariations, aNumVariations, face->GetFace());
    }
  }

  RefPtr<ScaledFontFontconfig> scaledFont =
      new ScaledFontFontconfig(std::move(face), instanceData, this, aSize);

  return scaledFont.forget();
}

already_AddRefed<ScaledFont> UnscaledFontFontconfig::CreateScaledFontFromWRFont(
    Float aGlyphSize, const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions,
    const wr::FontVariation* aVariations, uint32_t aNumVariations) {
  ScaledFontFontconfig::InstanceData instanceData(aOptions, aPlatformOptions);
  return CreateScaledFont(aGlyphSize, reinterpret_cast<uint8_t*>(&instanceData),
                          sizeof(instanceData), aVariations, aNumVariations);
}

bool ScaledFontFontconfig::HasVariationSettings() {
  // Check if the FT face has been cloned.
  return mFace &&
         mFace->GetFace()->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS &&
         mFace != static_cast<UnscaledFontFontconfig*>(mUnscaledFont.get())
                      ->GetFace();
}

already_AddRefed<UnscaledFont> UnscaledFontFontconfig::CreateFromFontDescriptor(
    const uint8_t* aData, uint32_t aDataLength, uint32_t aIndex) {
  if (aDataLength == 0) {
    gfxWarning() << "Fontconfig font descriptor is truncated.";
    return nullptr;
  }
  nsCString path((const char*)aData, aDataLength);

  // Use the given pathname/index in a fontconfig lookup to confirm this is a
  // known font file; if not, we'll use whatever fallback it provides instead.
  FcPattern* pattern = FcPatternCreate();
  FcPatternAddString(pattern, FC_FILE, (const FcChar8*)path.BeginReading());
  FcPatternAddInteger(pattern, FC_INDEX, aIndex);
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result = FcResultMatch;
  FcPattern* match = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);
  if (!match || result != FcResultMatch) {
    gfxWarning() << "FcFontMatch failed.";
    return nullptr;
  }

  // Get the file that fontconfig returned.
  // If it didn't use the requested file, log a warning and update path/index.
  char* file = nullptr;
  result = FcPatternGetString(match, FC_FILE, 0, (FcChar8**)&file);
  if (result == FcResultMatch && !path.Equals(file)) {
    gfxWarning() << "Font path " << path.get() << " invalid, using " << file;
    path = file;
    int index = 0;
    result = FcPatternGetInteger(match, FC_INDEX, 0, &index);
    if (result == FcResultMatch) {
      // Mask out possible variation-instance index stashed by fontconfig; we
      // just want the face index within a collection file.
      aIndex = index & 0xffff;
    }
  }

  return MakeAndAddRef<UnscaledFontFontconfig>(path.BeginReading(), aIndex);
}

}  // namespace mozilla::gfx
