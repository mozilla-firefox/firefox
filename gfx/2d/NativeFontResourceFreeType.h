/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_gfx_NativeFontResourceFreeType_h
#define mozilla_gfx_NativeFontResourceFreeType_h

#include "2D.h"

#include <cairo-ft.h>

namespace mozilla {
namespace gfx {

class NativeFontResourceFreeType
    : public NativeFontResource,
      public SharedFTFaceRefCountedData<NativeFontResourceFreeType> {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(NativeFontResourceFreeType, override)

#ifdef MOZ_WIDGET_ANDROID
  static already_AddRefed<NativeFontResourceFreeType> Create(
      const uint8_t* aFontData, uint32_t aDataLength,
      FT_Library aFTLibrary = nullptr);

  already_AddRefed<UnscaledFont> CreateUnscaledFont(
      uint32_t aIndex, const uint8_t* aInstanceData,
      uint32_t aInstanceDataLength) override;
#endif

  ~NativeFontResourceFreeType();

  already_AddRefed<SharedFTFace> CloneFace() override;

 protected:
  NativeFontResourceFreeType(const uint8_t*&& aFontData, uint32_t aDataLength,
                             FT_Library aFTLibrary = nullptr);

  template <class T>
  static already_AddRefed<T> CreateInternal(const uint8_t* aFontData,
                                            uint32_t aDataLength,
                                            FT_Library aFTLibrary);

  RefPtr<FontData> mFontData;
  FT_Library mFTLibrary;
};

#ifdef MOZ_WIDGET_GTK
class NativeFontResourceFontconfig final : public NativeFontResourceFreeType {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(NativeFontResourceFontconfig,
                                          override)

  static already_AddRefed<NativeFontResourceFontconfig> Create(
      const uint8_t* aFontData, uint32_t aDataLength,
      FT_Library aFTLibrary = nullptr);

  already_AddRefed<UnscaledFont> CreateUnscaledFont(
      uint32_t aIndex, const uint8_t* aInstanceData,
      uint32_t aInstanceDataLength) final;

 private:
  friend class NativeFontResourceFreeType;

  NativeFontResourceFontconfig(const uint8_t*&& aFontData, uint32_t aDataLength,
                               FT_Library aFTLibrary = nullptr);
};
#endif

}  // namespace gfx
}  // namespace mozilla

#endif  // mozilla_gfx_NativeFontResourceFreeType_h
