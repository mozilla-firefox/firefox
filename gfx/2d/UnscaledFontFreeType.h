/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_
#define MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_

#include <cairo-ft.h>

#include "2D.h"

namespace mozilla {
namespace gfx {

class ScaledFontFreeType;
class ScaledFontFontconfig;

class UnscaledFontFreeType : public UnscaledFont {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(UnscaledFontFreeType, override)
  explicit UnscaledFontFreeType(const RefPtr<SharedFTFace>& aFace)
      : mFace(aFace) {
    if (mFace) {
      if (mFace->GetFilename()) {
        mFile = mFace->GetFilename();
      }
      mIndex = mFace->GetFaceIndex();
    }
  }
  explicit UnscaledFontFreeType(const char* aFile, uint32_t aIndex = 0,
                                RefPtr<SharedFTFace> aFace = nullptr)
      : mFace(std::move(aFace)), mFile(aFile), mIndex(aIndex) {}
  explicit UnscaledFontFreeType(std::string&& aFile, uint32_t aIndex = 0,
                                RefPtr<SharedFTFace> aFace = nullptr)
      : mFace(std::move(aFace)), mFile(std::move(aFile)), mIndex(aIndex) {}

  FontType GetType() const override { return FontType::FREETYPE; }

  const RefPtr<SharedFTFace>& GetFace() const { return mFace; }
  const std::string& GetFile() const { return mFile; }
  uint32_t GetIndex() const { return mIndex; }

  bool GetFontFileData(FontFileDataOutput aDataCallback, void* aBaton) override;

  bool GetFontDescriptor(FontDescriptorOutput aCb, void* aBaton) override;

  RefPtr<SharedFTFace> InitFace();

#ifdef MOZ_WIDGET_ANDROID
  static already_AddRefed<UnscaledFont> CreateFromFontDescriptor(
      const uint8_t* aData, uint32_t aDataLength, uint32_t aIndex);

  already_AddRefed<ScaledFont> CreateScaledFont(
      Float aGlyphSize, const uint8_t* aInstanceData,
      uint32_t aInstanceDataLength, const wr::FontVariation* aVariations,
      uint32_t aNumVariations) override;

  already_AddRefed<ScaledFont> CreateScaledFontFromWRFont(
      Float aGlyphSize, const wr::FontInstanceOptions* aOptions,
      const wr::FontInstancePlatformOptions* aPlatformOptions,
      const wr::FontVariation* aVariations, uint32_t aNumVariations) override;
#endif

 protected:
  RefPtr<SharedFTFace> mFace;
  std::string mFile;
  uint32_t mIndex = 0;

  friend class ScaledFontFreeType;
  friend class ScaledFontFontconfig;

  static void GetVariationSettingsFromFace(
      std::vector<wr::FontVariation>* aVariations, SharedFTFace* aFace);

  static void ApplyVariationsToFace(const wr::FontVariation* aVariations,
                                    uint32_t aNumVariations, FT_Face aFace);
};

#ifdef MOZ_WIDGET_GTK
class UnscaledFontFontconfig : public UnscaledFontFreeType {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(UnscaledFontFontconfig, override)
  explicit UnscaledFontFontconfig(const RefPtr<SharedFTFace>& aFace)
      : UnscaledFontFreeType(aFace) {}
  explicit UnscaledFontFontconfig(const char* aFile, uint32_t aIndex = 0,
                                  RefPtr<SharedFTFace> aFace = nullptr)
      : UnscaledFontFreeType(aFile, aIndex, std::move(aFace)) {}
  explicit UnscaledFontFontconfig(std::string&& aFile, uint32_t aIndex = 0,
                                  RefPtr<SharedFTFace> aFace = nullptr)
      : UnscaledFontFreeType(std::move(aFile), aIndex, std::move(aFace)) {}

  FontType GetType() const override { return FontType::FONTCONFIG; }

  static already_AddRefed<UnscaledFont> CreateFromFontDescriptor(
      const uint8_t* aData, uint32_t aDataLength, uint32_t aIndex);

  already_AddRefed<ScaledFont> CreateScaledFont(
      Float aGlyphSize, const uint8_t* aInstanceData,
      uint32_t aInstanceDataLength, const wr::FontVariation* aVariations,
      uint32_t aNumVariations) override;

  already_AddRefed<ScaledFont> CreateScaledFontFromWRFont(
      Float aGlyphSize, const wr::FontInstanceOptions* aOptions,
      const wr::FontInstancePlatformOptions* aPlatformOptions,
      const wr::FontVariation* aVariations, uint32_t aNumVariations) override;
};

extern bool FcPatternAllowsBitmaps(FcPattern* aPattern, bool aAntialias,
                                   bool aHinting);
#endif

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_ */
