/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_PLATFORM_MAC_H
#define GFX_PLATFORM_MAC_H

#include "gfxPlatform.h"
#include "mozilla/LookAndFeel.h"
#include "nsTArrayForwardDeclare.h"

namespace mozilla {
namespace gfx {
class DrawTarget;
class VsyncSource;
}  // namespace gfx
}  // namespace mozilla

class gfxPlatformMac : public gfxPlatform {
 public:
  gfxPlatformMac();
  virtual ~gfxPlatformMac() {}

  struct SupplementalFontThread {
    ~SupplementalFontThread() { WaitForFontRegistration(); }
  };

  // Call early in startup to register the macOS supplemental language fonts
  // so that they're usable by the browser. This is intended to be called as
  // early as possible, before most services etc are initialized; it starts
  // a separate thread to register the fonts, because this is quite slow.
  // Return an RAII object to ensure that the thread is joined before shutdown.
  static SupplementalFontThread RegisterSupplementalFonts();

  // Call from the main thread at the point where we need to start using the
  // font list; this will wait (if necessary) for the registration thread to
  // finish.
  static void WaitForFontRegistration();

  static gfxPlatformMac* GetPlatform() {
    return (gfxPlatformMac*)gfxPlatform::GetPlatform();
  }

  already_AddRefed<gfxASurface> CreateOffscreenSurface(
      const IntSize& aSize, gfxImageFormat aFormat) override;

  bool CreatePlatformFontList() override;

  void ReadSystemFontList(mozilla::dom::SystemFontList* aFontList) override;

  void GetCommonFallbackFonts(uint32_t aCh, Script aRunScript,
                              FontPresentation aPresentation,
                              nsTArray<const char*>& aFontList) override;

  // lookup the system font for a particular system font type and set
  // the name and style characteristics
  static void LookupSystemFont(mozilla::LookAndFeel::FontID aSystemFontID,
                               nsACString& aSystemFontName,
                               gfxFontStyle& aFontStyle);

  bool SupportsApzWheelInput() const override { return true; }

  bool RespectsFontStyleSmoothing() const override {
    // gfxMacFont respects the font smoothing hint.
    return true;
  }

  bool RequiresAcceleratedGLContextForCompositorOGL() const override {
    // On OS X in a VM, unaccelerated CompositorOGL shows black flashes, so we
    // require accelerated GL for CompositorOGL but allow unaccelerated GL for
    // BasicCompositor.
    return true;
  }

  already_AddRefed<mozilla::gfx::VsyncSource> CreateGlobalHardwareVsyncSource()
      override;

  // lower threshold on font anti-aliasing
  uint32_t GetAntiAliasingThreshold() { return mFontAntiAliasingThreshold; }

  static bool CheckVariationFontSupport();

 protected:
  bool AccelerateLayersByDefault() override;

  BackendPrefsData GetBackendPrefs() const override;

 private:
  nsTArray<uint8_t> GetPlatformCMSOutputProfileData() override;

  // read in the pref value for the lower threshold on font anti-aliasing
  static uint32_t ReadAntiAliasingThreshold();

  static void FontRegistrationCallback(void* aUnused);

  uint32_t mFontAntiAliasingThreshold;

  static PRThread* sFontRegistrationThread;
};

#endif /* GFX_PLATFORM_MAC_H */
