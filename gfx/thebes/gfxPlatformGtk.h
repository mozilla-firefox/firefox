/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_PLATFORM_GTK_H
#define GFX_PLATFORM_GTK_H

#include "gfxPlatform.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsAutoRef.h"
#include "nsTArray.h"

#ifdef MOZ_X11
struct _XDisplay;
typedef struct _XDisplay Display;
#endif  // MOZ_X11

class gfxPlatformGtk final : public gfxPlatform {
  friend class gfxPlatform;

 public:
  gfxPlatformGtk();
  virtual ~gfxPlatformGtk();

  static gfxPlatformGtk* GetPlatform() {
    return (gfxPlatformGtk*)gfxPlatform::GetPlatform();
  }

  void ReadSystemFontList(mozilla::dom::SystemFontList* retValue) override;

  already_AddRefed<gfxASurface> CreateOffscreenSurface(
      const IntSize& aSize, gfxImageFormat aFormat) override;

  nsresult GetFontList(nsAtom* aLangGroup, const nsACString& aGenericFamily,
                       nsTArray<nsString>& aListOfFonts) override;

  void GetCommonFallbackFonts(uint32_t aCh, Script aRunScript,
                              FontPresentation aPresentation,
                              nsTArray<const char*>& aFontList) override;

  bool CreatePlatformFontList() override;

  gfxImageFormat GetOffscreenFormat() override;

  bool SupportsApzWheelInput() const override { return true; }

  void FontsPrefsChanged(const char* aPref) override;

  bool SupportsPluginDirectBitmapDrawing() override { return true; }

  bool AccelerateLayersByDefault() override;

  already_AddRefed<mozilla::gfx::VsyncSource> CreateGlobalHardwareVsyncSource()
      override;

  bool IsX11Display() { return mIsX11Display; }
  bool IsWaylandDisplay() override {
    return !mIsX11Display && !gfxPlatform::IsHeadless();
  }

  static bool CheckVariationFontSupport();

 protected:
  void InitAcceleration() override;
  void InitX11EGLConfig();
  void InitDmabufConfig();
  void InitPlatformHardwareVideoConfig() override;
  void InitWebRenderConfig() override;
  void InitMesaThreading();
  void BuildContentDeviceData(mozilla::gfx::ContentDeviceData* aOut) override;

 private:
  nsTArray<uint8_t> GetPlatformCMSOutputProfileData() override;

  bool mIsX11Display;
};

// Wrapper for third party code (WebRTC for instance) where
// gfxVars can't be included.
namespace mozilla::gfx {
bool IsDMABufEnabled();
}

#endif /* GFX_PLATFORM_GTK_H */
