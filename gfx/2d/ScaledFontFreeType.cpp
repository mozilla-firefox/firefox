/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontFreeType.h"

#include "Logging.h"
#include "NativeFontResourceFreeType.h"
#include "UnscaledFontFreeType.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "skia/include/ports/SkTypeface_cairo.h"

#include FT_MULTIPLE_MASTERS_H

namespace mozilla {
namespace gfx {

ScaledFontFreeType::ScaledFontFreeType(
    RefPtr<SharedFTFace>&& aFace, const RefPtr<UnscaledFont>& aUnscaledFont,
    Float aSize, bool aApplySyntheticBold)
    : ScaledFontBase(aUnscaledFont, aSize),
      mFace(std::move(aFace)),
      mApplySyntheticBold(aApplySyntheticBold) {}

bool ScaledFontFreeType::UseSubpixelPosition() const {
  return !MOZ_UNLIKELY(
             StaticPrefs::
                 gfx_text_subpixel_position_force_disabled_AtStartup()) &&
         FT_IS_SCALABLE(mFace->GetFace());
}

SkTypeface* ScaledFontFreeType::CreateSkTypeface() {
  return SkCreateTypefaceFromCairoFTFont(mFace->GetFace(), mFace.get());
}

void ScaledFontFreeType::SetupSkFontDrawOptions(SkFont& aFont) {
  aFont.setSubpixel(UseSubpixelPosition());

  if (mApplySyntheticBold) {
    aFont.setEmbolden(true);
  }

  aFont.setEmbeddedBitmaps(true);
}

bool ScaledFontFreeType::MayUseBitmaps() {
  return !FT_IS_SCALABLE(mFace->GetFace());
}

cairo_font_face_t* ScaledFontFreeType::CreateCairoFontFace(
    cairo_font_options_t* aFontOptions) {
  cairo_font_options_set_hint_metrics(aFontOptions, CAIRO_HINT_METRICS_OFF);

  int loadFlags = FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
  if (mFace->GetFace()->face_flags & FT_FACE_FLAG_TRICKY) {
    loadFlags &= ~FT_LOAD_NO_AUTOHINT;
  }

  unsigned int synthFlags = 0;
  if (mApplySyntheticBold) {
    synthFlags |= CAIRO_FT_SYNTHESIZE_BOLD;
  }

  return cairo_ft_font_face_create_for_ft_face(mFace->GetFace(), loadFlags,
                                               synthFlags, mFace.get());
}

bool ScaledFontFreeType::GetFontInstanceData(FontInstanceDataOutput aCb,
                                             void* aBaton) {
  std::vector<wr::FontVariation> variations;
  if (HasVariationSettings()) {
    UnscaledFontFreeType::GetVariationSettingsFromFace(&variations, mFace);
  }

  InstanceData instance(this);
  return aCb(reinterpret_cast<uint8_t*>(&instance), sizeof(instance),
             variations.data(), variations.size(), aBaton);
}

bool ScaledFontFreeType::GetWRFontInstanceOptions(
    Maybe<wr::FontInstanceOptions>* aOutOptions,
    Maybe<wr::FontInstancePlatformOptions>* aOutPlatformOptions,
    std::vector<wr::FontVariation>* aOutVariations) {
  wr::FontInstanceOptions options = {};
  options.render_mode = wr::FontRenderMode::Alpha;
  options.flags = wr::FontInstanceFlags{0};
  if (UseSubpixelPosition()) {
    options.flags |= wr::FontInstanceFlags::SUBPIXEL_POSITION;
  }
  options.flags |= wr::FontInstanceFlags::EMBEDDED_BITMAPS;
  options.synthetic_italics =
      wr::DegreesToSyntheticItalics(GetSyntheticObliqueAngle());

  if (mApplySyntheticBold) {
    options.flags |= wr::FontInstanceFlags::SYNTHETIC_BOLD;
  }

  wr::FontInstancePlatformOptions platformOptions;
  platformOptions.lcd_filter = wr::FontLCDFilter::None;
  platformOptions.hinting = wr::FontHinting::None;

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

ScaledFontFreeType::InstanceData::InstanceData(
    const wr::FontInstanceOptions* aOptions,
    const wr::FontInstancePlatformOptions* aPlatformOptions)
    : mApplySyntheticBold(false) {
  if (aOptions) {
    if (aOptions->flags & wr::FontInstanceFlags::SYNTHETIC_BOLD) {
      mApplySyntheticBold = true;
    }
  }
}

bool ScaledFontFreeType::HasVariationSettings() {
  // Check if the FT face has been cloned.
  return mFace &&
         mFace->GetFace()->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS &&
         mFace !=
             static_cast<UnscaledFontFreeType*>(mUnscaledFont.get())->GetFace();
}

}  // namespace gfx
}  // namespace mozilla
