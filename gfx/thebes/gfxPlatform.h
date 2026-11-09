/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_PLATFORM_H
#define GFX_PLATFORM_H

#include "mozilla/gfx/FontData.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/intl/UnicodeScriptCodes.h"
#include "nsTArray.h"
#include "nsString.h"
#include "nsCOMPtr.h"

#include "gfxTelemetry.h"
#include "gfxTypes.h"
#include "gfxSkipChars.h"

#include "qcms.h"

#include "mozilla/RefPtr.h"
#include "GfxInfoCollector.h"

#include "mozilla/Maybe.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/MemoryPressureObserver.h"
#include "mozilla/layers/OverlayInfo.h"

class FontVisibilityProvider;
class gfxASurface;
class gfxFont;
class gfxFontGroup;
struct gfxFontStyle;
class gfxUserFontSet;
class gfxFontEntry;
class gfxPlatformFontList;
class gfxTextRun;
class nsIURI;
class nsAtom;
class nsIObserver;
class SRGBOverrideObserver;
class gfxTextPerfMetrics;
typedef struct FT_LibraryRec_* FT_Library;

namespace mozilla {
struct StyleFontFamilyList;
struct StyleFontFaceSourceTechFlags;
enum class StyleFontFaceSourceFormatKeyword : uint8_t;
class WeightRange;
class StretchRange;
class SlantStyleRange;
class LogModule;
class VsyncDispatcher;
namespace layers {
class FrameStats;
}
namespace gfx {
class DrawTarget;
class SourceSurface;
class DataSourceSurface;
class ScaledFont;
class VsyncSource;
class SoftwareVsyncSource;
class ContentDeviceData;
class GPUDeviceData;
class FeatureState;

inline uint32_t BackendTypeBit(BackendType b) { return 1 << uint8_t(b); }

}  // namespace gfx
namespace dom {
class SystemFontListEntry;
class SystemFontList;
}  // namespace dom
}  // namespace mozilla

#define MOZ_PERFORMANCE_WARNING(module, ...)      \
  do {                                            \
    if (gfxPlatform::PerfWarnings()) {            \
      printf_stderr("[" module "] " __VA_ARGS__); \
    }                                             \
  } while (0)

enum class CMSMode : int32_t {
  Off = 0,         // No color management
  All = 1,         // Color manage everything
  TaggedOnly = 2,  // Color manage tagged Images Only
  _ENUM_MAX = TaggedOnly
};

enum eGfxLog : uint8_t {
  // all font enumerations, localized names, fullname/psnames, cmap loads
  eGfxLog_fontlist = 0,
  // timing info on font initialization
  eGfxLog_fontinit = 1,
  // dump text runs, font matching, system fallback for content
  eGfxLog_textrun = 2,
  // dump text runs, font matching, system fallback for chrome
  eGfxLog_textrunui = 3,
  // dump cmap coverage data as they are loaded
  eGfxLog_cmapdata = 4,
  // text perf data
  eGfxLog_textperf = 5,
  // font query / font-fallback simulation
  eGfxLog_fontquery = 6
};

// Used during font matching to express a preference, if any, for whether
// to use a font that will present a color or monochrome glyph.
enum class FontPresentation : uint8_t {
  // Character does not have the emoji property, so no special heuristics
  // apply during font selection.
  Any = 0,
  // Character is potentially emoji, but its default presentation is text.
  TextDefault,
  // Character is potentially emoji, but Text-style presentation has been
  // explicitly requested using VS15.
  TextExplicit,
  // Character has Emoji-style presentation by default (but an author-
  // provided webfont may be used even if it is not color).
  EmojiDefault,
  // Character explicitly requires Emoji-style presentation due to VS16 or
  // skin-tone codepoint.
  EmojiExplicit,
};

inline bool PrefersColor(FontPresentation aPresentation) {
  return aPresentation >= FontPresentation::EmojiDefault;
}

inline bool IsExplicitPresentation(FontPresentation aPresentation) {
  return aPresentation == FontPresentation::TextExplicit ||
         aPresentation == FontPresentation::EmojiExplicit;
}

// when searching through pref langs, max number of pref langs
const uint32_t kMaxLenPrefLangList = 32;

#define UNINITIALIZED_VALUE (-1)

inline const char* GetBackendName(mozilla::gfx::BackendType aBackend) {
  switch (aBackend) {
    case mozilla::gfx::BackendType::CAIRO:
      return "cairo";
    case mozilla::gfx::BackendType::SKIA:
      return "skia";
    case mozilla::gfx::BackendType::RECORDING:
      return "recording";
    case mozilla::gfx::BackendType::WEBRENDER_TEXT:
      return "webrender text";
    case mozilla::gfx::BackendType::NONE:
      return "none";
    case mozilla::gfx::BackendType::WEBGL:
      return "webgl";
    case mozilla::gfx::BackendType::BACKEND_LAST:
      return "invalid";
  }
  MOZ_CRASH("Incomplete switch");
}

struct BackendPrefsData {
  uint32_t mCanvasBitmask = 0;
  mozilla::gfx::BackendType mCanvasDefault = mozilla::gfx::BackendType::NONE;
  uint32_t mContentBitmask = 0;
  mozilla::gfx::BackendType mContentDefault = mozilla::gfx::BackendType::NONE;
};

class gfxPlatform : public mozilla::layers::MemoryPressureListener {
  friend class SRGBOverrideObserver;

 public:
  using WeightRange = mozilla::WeightRange;
  using StretchRange = mozilla::StretchRange;
  using SlantStyleRange = mozilla::SlantStyleRange;
  typedef mozilla::gfx::sRGBColor sRGBColor;
  typedef mozilla::gfx::DeviceColor DeviceColor;
  typedef mozilla::gfx::DataSourceSurface DataSourceSurface;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::IntSize IntSize;
  typedef mozilla::gfx::SourceSurface SourceSurface;
  typedef mozilla::intl::Script Script;

  /**
   * Return a pointer to the current active platform.
   * This is a singleton; it contains mostly convenience
   * functions to obtain platform-specific objects.
   */
  static gfxPlatform* GetPlatform() {
    if (MOZ_UNLIKELY(!gPlatform)) {
      Init();
    }
    return gPlatform;
  }

  /**
   * Returns whether or not graphics has been initialized yet. This is
   * intended for Telemetry where we don't necessarily want to initialize
   * graphics just to observe its state.
   */
  static bool Initialized();

  /**
   * Shut down Thebes.
   * Init() arranges for this to be called at an appropriate time.
   */
  static void Shutdown();

  /**
   * Initialize gfxPlatform (if not already done) in a child process, with
   * the provided ContentDeviceData.
   */
  static void InitChild(const mozilla::gfx::ContentDeviceData& aData);

  static void InitLayersIPC();
  static void ShutdownLayersIPC();

  /**
   * Initialize ScrollMetadata statics. Does not depend on gfxPlatform.
   */
  static void InitNullMetadata();

  static int32_t MaxTextureSize();
  static int32_t MaxAllocSize();
  static void InitMoz2DLogging();

  static void InitMemoryReportersForGPUProcess();

  static bool IsHeadless();

  static bool UseRemoteCanvas();

  static bool UseHDR();

  static bool IsBackendAccelerated(
      const mozilla::gfx::BackendType aBackendType);

  /**
   * Create an offscreen surface of the given dimensions
   * and image format.
   */
  virtual already_AddRefed<gfxASurface> CreateOffscreenSurface(
      const IntSize& aSize, gfxImageFormat aFormat) = 0;

  /**
   * Beware that this method may return DrawTargets which are not fully
   * supported on the current platform and might fail silently in subtle ways.
   * This is a massive potential footgun. You should only use these methods for
   * canvas drawing really. Use extreme caution if you use them for content
   * where you are not 100% sure we support the DrawTarget we get back. See
   * SupportsAzureContentForDrawTarget.
   */
  static already_AddRefed<DrawTarget> CreateDrawTargetForSurface(
      gfxASurface* aSurface, const mozilla::gfx::IntSize& aSize);

  /*
   * Creates a SourceSurface for a gfxASurface. This function does no caching,
   * so the caller should cache the gfxASurface if it will be used frequently.
   * The returned surface keeps a reference to aTarget, so it is OK to keep the
   * surface, even if aTarget changes.
   * aTarget should not keep a reference to the returned surface because that
   * will cause a cycle.
   *
   * This function is static so that it can be accessed from outside the main
   * process.
   *
   * aIsPlugin is used to tell the backend that they can optimize this surface
   * specifically because it's used for a plugin. This is mostly for Skia.
   */
  static already_AddRefed<SourceSurface> GetSourceSurfaceForSurface(
      RefPtr<mozilla::gfx::DrawTarget> aTarget, gfxASurface* aSurface,
      bool aIsPlugin = false);

  static void ClearSourceSurfaceForSurface(gfxASurface* aSurface);

  static already_AddRefed<DataSourceSurface> GetWrappedDataSourceSurface(
      gfxASurface* aSurface);

  already_AddRefed<DrawTarget> CreateOffscreenContentDrawTarget(
      const mozilla::gfx::IntSize& aSize, mozilla::gfx::SurfaceFormat aFormat,
      bool aFallback = false);

  already_AddRefed<DrawTarget> CreateOffscreenCanvasDrawTarget(
      const mozilla::gfx::IntSize& aSize, mozilla::gfx::SurfaceFormat aFormat,
      bool aRequireSoftwareRender = false);

  already_AddRefed<DrawTarget> CreateSimilarSoftwareDrawTarget(
      DrawTarget* aDT, const IntSize& aSize,
      mozilla::gfx::SurfaceFormat aFormat);

  static already_AddRefed<DrawTarget> CreateDrawTargetForData(
      unsigned char* aData, const mozilla::gfx::IntSize& aSize, int32_t aStride,
      mozilla::gfx::SurfaceFormat aFormat, bool aUninitialized = false,
      bool aIsClear = false);

  /**
   * Returns true if we should use Azure to render content with aTarget. For
   * example, it is possible that we are using Direct2D for rendering and thus
   * using Azure. But we want to render to a CairoDrawTarget, in which case
   * SupportsAzureContent will return true but SupportsAzureContentForDrawTarget
   * will return false.
   */
  bool SupportsAzureContentForDrawTarget(mozilla::gfx::DrawTarget* aTarget);

  bool SupportsAzureContentForType(mozilla::gfx::BackendType aType) {
    return BackendTypeBit(aType) & mContentBackendBitmask;
  }

  static bool AsyncPanZoomEnabled();

  const char* GetAzureCanvasBackend() const;
  const char* GetAzureContentBackend() const;

  void GetAzureBackendInfo(mozilla::widget::InfoObject& aObj);
  void GetApzSupportInfo(mozilla::widget::InfoObject& aObj);
  void GetFrameStats(mozilla::widget::InfoObject& aObj);
  void GetCMSSupportInfo(mozilla::widget::InfoObject& aObj);
  void GetDisplayInfo(mozilla::widget::InfoObject& aObj);
  void GetOverlayInfo(mozilla::widget::InfoObject& aObj);
  void GetSwapChainInfo(mozilla::widget::InfoObject& aObj);

  // Get the default content backend that will be used with the default
  // compositor. If the compositor is known when calling this function,
  // GetContentBackendFor() should be called instead.
  mozilla::gfx::BackendType GetDefaultContentBackend() const {
    return mContentBackend;
  }

  /// Return the software backend to use by default.
  mozilla::gfx::BackendType GetSoftwareBackend() { return mSoftwareBackend; }

  // Return the best content backend available that is compatible with the
  // given layers backend.
  virtual mozilla::gfx::BackendType GetContentBackendFor(
      mozilla::layers::LayersBackend aLayers) {
    return mContentBackend;
  }

  virtual mozilla::gfx::BackendType GetPreferredCanvasBackend() {
    return mPreferredCanvasBackend;
  }
  mozilla::gfx::BackendType GetFallbackCanvasBackend() {
    return mFallbackCanvasBackend;
  }

  /*
   * Font bits
   */

  /**
   * Fill aListOfFonts with the results of querying the list of font names
   * that correspond to the given language group or generic font family
   * (or both, or neither).
   */
  virtual nsresult GetFontList(nsAtom* aLangGroup,
                               const nsACString& aGenericFamily,
                               nsTArray<nsString>& aListOfFonts);

  /**
   * Fill aFontList with a list of SystemFontListEntry records for the
   * available fonts on the platform; used to pass the list from chrome to
   * content process. Currently implemented only on MacOSX and Linux.
   */
  virtual void ReadSystemFontList(mozilla::dom::SystemFontList*) {};

  /**
   * Rebuilds the system font lists (if aFullRebuild is true), or just notifies
   * content that the list has changed but existing memory mappings are still
   * valid (aFullRebuild is false).
   */
  nsresult UpdateFontList(bool aFullRebuild = true);

  /**
   * Create the platform font-list object (gfxPlatformFontList concrete
   * subclass). This function is responsible to create the appropriate subclass
   * of gfxPlatformFontList *and* to call its InitFontList() method.
   */
  virtual bool CreatePlatformFontList() = 0;

  /**
   * Resolving a font name to family name. The result MUST be in the result of
   * GetFontList(). If the name doesn't in the system, aFamilyName will be empty
   * string, but not failed.
   */
  void GetStandardFamilyName(const nsCString& aFontName,
                             nsACString& aFamilyName);

  /**
   * Returns default font name (localized family name) for aLangGroup and
   * aGenericFamily.  The result is typically the first font in
   * font.name-list.<aGenericFamily>.<aLangGroup>.  However, if it's not
   * available in the system, this may return second or later font in the
   * pref.  If there are no available fonts in the pref, returns empty string.
   */
  nsAutoCString GetDefaultFontName(const nsACString& aLangGroup,
                                   const nsACString& aGenericFamily);

  /**
   * Look up a local platform font using the full font face name.
   * (Needed to support @font-face src local().)
   * Ownership of the returned gfxFontEntry is passed to the caller.
   */
  already_AddRefed<gfxFontEntry> LookupLocalFont(
      FontVisibilityProvider* aFontVisibilityProvider,
      const nsACString& aFontName, const WeightRange& aWeightForEntry,
      const StretchRange& aStretchForEntry,
      const SlantStyleRange& aStyleForEntry);

  /**
   * Activate a platform font.  (Needed to support @font-face src url().)
   * aFontData is a NS_Malloc'ed block that must be freed by this function
   * (or responsibility passed on) when it is no longer needed; the caller
   * will NOT free it.
   * Ownership of the returned gfxFontEntry is passed to the caller.
   */
  already_AddRefed<gfxFontEntry> MakePlatformFont(
      const nsACString& aFontName, const WeightRange& aWeightForEntry,
      const StretchRange& aStretchForEntry,
      const SlantStyleRange& aStyleForEntry, FontData* aFontData);

  /**
   * Whether to allow downloadable fonts via @font-face rules
   */
  bool DownloadableFontsEnabled();

  /**
   * True when hinting should be enabled.  This setting shouldn't
   * change per gecko process, while the process is live.  If so the
   * results are not defined.
   *
   * NB: this bit is only honored by the FT2 backend, currently.
   */
  virtual bool FontHintingEnabled() { return true; }

  /**
   * True when zooming should not require reflow, so glyph metrics and
   * positioning should not be adjusted for device pixels.
   * If this is TRUE, then FontHintingEnabled() should be FALSE,
   * but the converse is not necessarily required;
   *
   * Like FontHintingEnabled (above), this setting shouldn't
   * change per gecko process, while the process is live.  If so the
   * results are not defined.
   *
   * NB: this bit is only honored by the FT2 backend, currently.
   */
  virtual bool RequiresLinearZoom() { return false; }

  /**
   * Whether the frame->StyleFont().mFont.smoothing field is respected by
   * text rendering on this platform.
   */
  virtual bool RespectsFontStyleSmoothing() const { return false; }

  /**
   * Whether to check all font cmaps during system font fallback
   */
  bool UseCmapsDuringSystemFallback();

  /**
   * Whether to render SVG glyphs within an OpenType font wrapper
   */
  bool OpenTypeSVGEnabled();

  /**
   * Max character length of words in the word cache
   */
  uint32_t WordCacheCharLimit();

  /**
   * Max number of entries in word cache
   */
  uint32_t WordCacheMaxEntries();

  /**
   * Whether to use the SIL Graphite rendering engine
   * (for fonts that include Graphite tables)
   */
  bool UseGraphiteShaping();

  // Check whether format is supported on a platform (if unclear, returns true).
  // Default implementation checks for "common" formats that we support across
  // all platforms, but individual platform implementations may override.
  virtual bool IsFontFormatSupported(
      mozilla::StyleFontFaceSourceFormatKeyword aFormatHint,
      const mozilla::StyleFontFaceSourceTechFlags& aTechFlags);

  bool IsKnownIconFontFamily(const nsAtom* aFamilyName) const;

  virtual bool DidRenderingDeviceReset(
      mozilla::gfx::DeviceResetReason* aResetReason = nullptr) {
    return false;
  }

  // returns a list of commonly used fonts for a given character
  // these are *possible* matches, no cmap-checking is done at this level
  virtual void GetCommonFallbackFonts(uint32_t /*aCh*/, Script /*aRunScript*/,
                                      FontPresentation /*aPresentation*/,
                                      nsTArray<const char*>& /*aFontList*/) {
    // platform-specific override, by default do nothing
  }

  // Are we in safe mode?
  static bool InSafeMode();

  static bool OffMainThreadCompositingEnabled();

  inline static void EnsureCMSInitialized() {
    if (MOZ_UNLIKELY(!gCMSInitialized)) {
      MaybeInitializeCMS();
      MOZ_ASSERT(gCMSInitialized);
    }
  }

  /**
   * Are we going to try color management?
   */
  static CMSMode GetCMSMode() {
    EnsureCMSInitialized();
    return gCMSMode;
  }

  /**
   * Used only for testing. Override the pref setting.
   */
  static void SetCMSModeOverride(CMSMode aMode);

  /**
   * Determines the rendering intent for color management.
   *
   * If the value in the pref gfx.color_management.rendering_intent is a
   * valid rendering intent as defined in gfx/qcms/qcms.h, that
   * value is returned. Otherwise, -1 is returned and the embedded intent
   * should be used.
   *
   * See bug 444014 for details.
   */
  static int GetRenderingIntent();

  /**
   * Convert a pixel using a cms transform in an endian-aware manner.
   */
  static DeviceColor TransformPixel(const sRGBColor& in,
                                    qcms_transform* transform);

  /**
   * Return the output device ICC profile.
   */
  static qcms_profile* GetCMSOutputProfile() {
    return GetPlatform()->mCMSOutputProfile;
  }

  static const mozilla::Maybe<nsTArray<uint8_t>>& GetCMSOutputICCProfileData() {
    // This data only represents mCMSOutputProfile if it is not the sRGB
    // profile, so this should not be called unless that is the case as there is
    // no need for that data otherwise.
    MOZ_ASSERT(qcms_profile_is_sRGB(GetPlatform()->mCMSsRGBProfile));
    MOZ_ASSERT(GetPlatform()->mCMSsRGBProfile !=
               GetPlatform()->mCMSOutputProfile);
    return GetPlatform()->mCMSOutputProfileData;
  }

  /**
   * Return the sRGB ICC profile.
   */
  static qcms_profile* GetCMSsRGBProfile() {
    return GetPlatform()->mCMSsRGBProfile;
  }

  /**
   * Return sRGB -> output device transform.
   */
  static qcms_transform* GetCMSRGBTransform() {
    return GetPlatform()->mCMSRGBTransform;
  }

  /**
   * Return output -> sRGB device transform.
   */
  static qcms_transform* GetCMSInverseRGBTransform() {
    return GetPlatform()->mCMSInverseRGBTransform;
  }

  /**
   * Return sRGBA -> output device transform.
   */
  static qcms_transform* GetCMSRGBATransform() {
    return GetPlatform()->mCMSRGBATransform;
  }

  /**
   * Return sBGRA -> output device transform.
   */
  static qcms_transform* GetCMSBGRATransform() {
    return GetPlatform()->mCMSBGRATransform;
  }

  /**
   * Return OS RGBA -> output device transform.
   */
  static qcms_transform* GetCMSOSRGBATransform();

  /**
   * Return OS RGBA QCMS type.
   */
  static qcms_data_type GetCMSOSRGBAType();

  virtual void FontsPrefsChanged(const char* aPref);

  uint32_t GetBidiNumeralOption();

  /**
   * Force all presContexts to reflow (and reframe if needed).
   *
   * This is used when something about platform settings changes that might have
   * an effect on layout, such as font rendering settings that influence
   * metrics, or installed fonts.
   *
   * Normally this is initiated in the parent process, and also broadcast to
   * child processes, but some callers might not need this if they implement
   * their own notification.
   */
  enum class GlobalReflowFlags : uint8_t {
    None = 0,
    // Font data has been updated such that new fonts and/or character coverage
    // may be available; existing font-lookup caches should be invalidated and
    // font selection re-done.
    FontsChanged = (1 << 0),
    // Content should be fully reframed; this is used when font entry records
    // may have been invalidated such that existing textruns in the frame tree
    // are no longer safe to work with. Rather than reflow the existing frame
    // tree we will discard and re-create it.
    NeedsReframe = (1 << 1),
    // (If this is the parent process), broadcast this reflow request to all
    // child processes.
    BroadcastToChildren = (1 << 2),
    // For serialization.
    ALL_BITS = FontsChanged | NeedsReframe | BroadcastToChildren,
  };
  static void ForceGlobalReflow(GlobalReflowFlags aFlags);

  static void FlushFontAndWordCaches();

  /**
   * Returns a 1x1 DrawTarget that can be used for measuring text etc. as
   * it would measure if rendered on-screen.  Guaranteed to return a
   * non-null and valid DrawTarget.
   */
  RefPtr<mozilla::gfx::DrawTarget> ScreenReferenceDrawTarget();

  static RefPtr<mozilla::gfx::DrawTarget>
  ThreadLocalScreenReferenceDrawTarget();

  virtual mozilla::gfx::SurfaceFormat Optimal2DFormatForContent(
      gfxContentType aContent);

  virtual gfxImageFormat OptimalFormatForContent(gfxContentType aContent);

  virtual gfxImageFormat GetOffscreenFormat() {
    return mozilla::gfx::SurfaceFormat::X8R8G8B8_UINT32;
  }

  /**
   * Returns a logger if one is available and logging is enabled
   */
  static mozilla::LogModule* GetLog(eGfxLog aWhichLog);

  static void PurgeSkiaFontCache();

  static bool UsesOffMainThreadCompositing();

  /**
   * Returns the global vsync dispatcher. There is only one global vsync
   * dispatcher and it stays around for the entire lifetime of the process.
   * Must only be called in the parent process.
   */
  RefPtr<mozilla::VsyncDispatcher> GetGlobalVsyncDispatcher();

  /**
   * True if layout rendering should use ASAP mode, which means
   * the refresh driver and compositor should render ASAP.
   * Used for talos testing purposes
   */
  static bool IsInLayoutAsapMode();

  /**
   * Returns whether or not a custom vsync rate is set.
   */
  static bool ForceSoftwareVsync();

  /**
   * Returns the software vsync rate to use.
   */
  static int GetSoftwareVsyncRate();

  /**
   * Returns the default frame rate for the refresh driver / software vsync.
   */
  static int GetDefaultFrameRate();

  /**
   * Update the frame rate (called e.g. after pref changes).
   */
  static void ReInitFrameRate(const char* aPrefIgnored, void* aDataIgnored);

  /**
   * Reset the global hardware vsync source. The next call to ReInitFrameRate
   * will attempt to reestablish it, and fall back to software if needed.
   */
  static void ResetHardwareVsyncSource();

  /**
   * Update force subpixel AA quality setting (called after pref
   * changes).
   */
  void UpdateForceSubpixelAAWherePossible();

  /**
   * Used to test which input types are handled via APZ.
   */
  virtual bool SupportsApzWheelInput() const { return false; }
  bool SupportsApzTouchInput() const;
  bool SupportsApzDragInput() const;
  bool SupportsApzKeyboardInput() const;
  bool SupportsApzAutoscrolling() const;
  bool SupportsApzZooming() const;

  // If a device reset has occurred, schedule any necessary paints in the
  // widget. This should only be used within nsRefreshDriver.
  virtual void SchedulePaintIfDeviceReset() {}

  /**
   * Helper method, creates a draw target for a specific Azure backend.
   * Used by CreateOffscreenDrawTarget.
   */
  already_AddRefed<DrawTarget> CreateDrawTargetForBackend(
      mozilla::gfx::BackendType aBackend, const mozilla::gfx::IntSize& aSize,
      mozilla::gfx::SurfaceFormat aFormat);

  /**
   * Wrapper around StaticPrefs::gfx_perf_warnings_enabled().
   * Extracted into a function to avoid including StaticPrefs_gfx.h from this
   * file.
   */
  static bool PerfWarnings();

  static void DisableAcceleratedCanvasForFallback(
      mozilla::gfx::FeatureStatus aStatus, const char* aMessage,
      const nsACString& aFailureId);

  static void DisableAllCanvasForFallback(mozilla::gfx::FeatureStatus aStatus,
                                          const char* aMessage,
                                          const nsACString& aFailureId);
  static void DisableGPUProcess();

  void NotifyCompositorCreated(mozilla::layers::LayersBackend aBackend);
  mozilla::layers::LayersBackend GetCompositorBackend() const {
    return mCompositorBackend;
  }

  virtual void CompositorUpdated() {}

  // Plugin async drawing support.
  virtual bool SupportsPluginDirectBitmapDrawing() { return false; }

  // Some platforms don't support CompositorOGL in an unaccelerated OpenGL
  // context. These platforms should return true here.
  virtual bool RequiresAcceleratedGLContextForCompositorOGL() const {
    return false;
  }

  /**
   * Check the blocklist for a feature. Returns false if the feature is blocked
   * with an appropriate message and failure ID.
   * */
  static bool IsGfxInfoStatusOkay(int32_t aFeature, nsCString* aOutMessage,
                                  nsCString& aFailureId);

  const gfxSkipChars& EmptySkipChars() const { return kEmptySkipChars; }

  /**
   * Returns a buffer containing the CMS output profile data. The way this
   * is obtained is platform-specific.
   */
  virtual nsTArray<uint8_t> GetPlatformCMSOutputProfileData() {
    return GetPrefCMSOutputProfileData();
  }

  /**
   * Return information on how child processes should initialize graphics
   * devices.
   */
  virtual void BuildContentDeviceData(mozilla::gfx::ContentDeviceData* aOut);

  /**
   * Imports settings from the GPU process. This should only be called through
   * GPUProcessManager, in the UI process.
   */
  virtual void ImportGPUDeviceData(const mozilla::gfx::GPUDeviceData& aData);

  void SetOverlayInfo(const mozilla::layers::OverlayInfo& aInfo) {
    mOverlayInfo = mozilla::Some(aInfo);
  }

  void SetSwapChainInfo(const mozilla::layers::SwapChainInfo& aInfo) {
    mSwapChainInfo = mozilla::Some(aInfo);
  }

  static void DisableRemoteCanvas();

  static bool HasVariationFontSupport();

  // you probably want to use gfxVars::UseWebRender() instead of this
  static bool WebRenderPrefEnabled();
  // you probably want to use gfxVars::UseWebRender() instead of this
  static bool WebRenderEnvvarEnabled();

  static const char* WebRenderResourcePathOverride();

  // Returns true if we would like to keep the GPU process if possible.
  // If aCrashAfterFinalFallback is true then crash if we have already
  // exhausted all of our fallback options. Otherwise we remain on the final
  // fallback configuration.
  static bool FallbackFromAcceleration(mozilla::gfx::FeatureStatus aStatus,
                                       const char* aMessage,
                                       const nsACString& aFailureId,
                                       bool aCrashAfterFinalFallback = false);

  void NotifyFrameStats(nsTArray<mozilla::layers::FrameStats>&& aFrameStats);

  virtual void OnMemoryPressure(
      mozilla::layers::MemoryPressureReason aWhy) override;

  virtual void EnsureDevicesInitialized() {};
  virtual bool DevicesInitialized() { return true; };

  virtual bool IsWaylandDisplay() { return false; }

  static uint32_t TargetFrameRate();

  static bool UseDesktopZoomingScrollbars();

 protected:
  gfxPlatform();
  virtual ~gfxPlatform();

  virtual void InitAcceleration();
  virtual void InitWebRenderConfig();
  void InitHardwareVideoConfig();
  virtual void InitWebGLConfig();
  virtual void InitWebGPUConfig();
  virtual void InitWindowOcclusionConfig();
  void InitBackdropFilterConfig();
  void InitAcceleratedCanvas2DConfig();

  virtual void GetPlatformDisplayInfo(mozilla::widget::InfoObject& aObj) {}

  /**
   * Called immediately before deleting the gfxPlatform object.
   */
  virtual void WillShutdown();

  // Return a hardware vsync source for this platform.
  already_AddRefed<mozilla::gfx::VsyncSource> GetGlobalHardwareVsyncSource();

  // Return a software vsync source (which uses a timer internally).
  // Can be used as a fallback for platforms without hardware vsync,
  // and when the layout.frame_rate pref is set to a non-negative value.
  already_AddRefed<mozilla::gfx::VsyncSource> GetSoftwareVsyncSource();

  // Create the platform-specific global vsync source. Can fall back to
  // GetSoftwareVsyncSource().
  virtual already_AddRefed<mozilla::gfx::VsyncSource>
  CreateGlobalHardwareVsyncSource() = 0;

  // Returns whether or not layers should be accelerated by default on this
  // platform.
  virtual bool AccelerateLayersByDefault();

  // Returns preferences of canvas and content backends.
  virtual BackendPrefsData GetBackendPrefs() const;

  /**
   * Initialise the preferred and fallback canvas backends
   * aBackendBitmask specifies the backends which are acceptable to the caller.
   * The backend used is determined by aBackendBitmask and the order specified
   * by the gfx.canvas.azure.backends pref.
   */
  void InitBackendPrefs(BackendPrefsData&& aPrefsData);

  /**
   * Content-process only. Updates device preferences from the parent process,
   * if we've received any.
   */
  void ImportCachedContentDeviceData();
  virtual void ImportContentDeviceData(
      const mozilla::gfx::ContentDeviceData& aData);

 public:
  /**
   * Returns the contents of the file pointed to by the
   * gfx.color_management.display_profile pref, if set.
   * Returns an empty array if not set, or if an error occurs
   */
  static nsTArray<uint8_t> GetPrefCMSOutputProfileData();

 protected:
  /**
   * If inside a child process and have ever received a
   * SetXPCOMProcessAttributes message, this contains the cmsOutputProfileData
   * from that message.
   */
  mozilla::Maybe<nsTArray<uint8_t>>& GetCMSOutputProfileData();

  /**
   * Increase the global device counter after a device has been removed/reset.
   */
  void BumpDeviceCounter();

  /**
   * returns the first backend named in the pref gfx.canvas.azure.backends
   * which is a component of aBackendBitmask, a bitmask of backend types
   */
  static mozilla::gfx::BackendType GetCanvasBackendPref(
      uint32_t aBackendBitmask);

  /**
   * returns the first backend named in the pref gfx.content.azure.backend
   * which is a component of aBackendBitmask, a bitmask of backend types
   */
  static mozilla::gfx::BackendType GetContentBackendPref(
      uint32_t& aBackendBitmask);

  /**
   * Will return the first backend named in aBackendPrefName
   * allowed by aBackendBitmask, a bitmask of backend types.
   * It also modifies aBackendBitmask to only include backends that are
   * allowed given the prefs.
   */
  static mozilla::gfx::BackendType GetBackendPref(const char* aBackendPrefName,
                                                  uint32_t& aBackendBitmask);
  /**
   * Decode the backend enumberation from a string.
   */
  static mozilla::gfx::BackendType BackendTypeForName(const nsCString& aName);

  int8_t mAllowDownloadableFonts;

  // Whether the platform supports rendering OpenType font variations
  static std::atomic<int8_t> sHasVariationFontSupport;

  // The global vsync dispatcher. Only non-null in the parent process.
  // Its underlying VsyncSource is either mGlobalHardwareVsyncSource
  // or mSoftwareVsyncSource.
  RefPtr<mozilla::VsyncDispatcher> mVsyncDispatcher;

  // Cached software vsync source. Only non-null in the parent process,
  // and only after the first time GetHardwareVsyncSource has been called.
  RefPtr<mozilla::gfx::VsyncSource> mGlobalHardwareVsyncSource;

  // Cached software vsync source. Only non-null in the parent process,
  // and only after the first time GetSoftwareVsyncSource has been called.
  // Used as a fallback source if hardware vsync is not available,
  // or when the layout.frame_rate pref is set.
  RefPtr<mozilla::gfx::SoftwareVsyncSource> mSoftwareVsyncSource;

  RefPtr<mozilla::gfx::DrawTarget> mScreenReferenceDrawTarget;

 private:
  /**
   * Start up Thebes.
   */
  static void Init();

  static void InitOpenGLConfig();

  static void VideoDecodingFailedChangedCallback(const char* aPref, void*);

  static void HWDRMFailedChangedCallback(const char* aPref, void*);

  static gfxPlatform* gPlatform;

  void InitializeCMS();
  void ShutdownCMS();

  /**
   * This uses nsIScreenManager to determine the primary screen color depth
   */
  void PopulateScreenInfo();

  void InitCompositorAccelerationPrefs();
  void InitGPUProcessPrefs();
  virtual void InitPlatformGPUProcessPrefs() {}
  virtual void InitPlatformHardwareVideoConfig() {}
  virtual void InitPlatformHardwarDRMConfig() {}

  // Gather telemetry data about the Gfx Platform and send it
  static void ReportTelemetry();

  static bool IsDXInterop2Blocked();
  static bool IsDXNV12Blocked();
  static bool IsDXP010Blocked();
  static bool IsDXP016Blocked();

  static void MaybeInitializeCMS();

  // We need these to be static because we might call them in the GPU process,
  // even if we don't do color management there.
  static mozilla::Atomic<bool, mozilla::ReleaseAcquire> gCMSInitialized;
  static CMSMode gCMSMode;

  // These two may point to the same profile
  qcms_profile* mCMSOutputProfile = nullptr;
  qcms_profile* mCMSsRGBProfile = nullptr;

  qcms_transform* mCMSRGBTransform = nullptr;
  qcms_transform* mCMSInverseRGBTransform = nullptr;
  qcms_transform* mCMSRGBATransform = nullptr;
  qcms_transform* mCMSBGRATransform = nullptr;
  mozilla::Maybe<nsTArray<uint8_t>> mCMSOutputProfileData;

  RefPtr<gfxASurface> mScreenReferenceSurface;
  RefPtr<mozilla::layers::MemoryPressureObserver> mMemoryPressureObserver;

  // The preferred draw target backend to use for canvas
  mozilla::gfx::BackendType mPreferredCanvasBackend;
  // The fallback draw target backend to use for canvas, if the preferred
  // backend fails
  mozilla::gfx::BackendType mFallbackCanvasBackend;
  // The backend to use for content
  mozilla::gfx::BackendType mContentBackend;
  // The backend to use when we need it not to be accelerated.
  mozilla::gfx::BackendType mSoftwareBackend;
  // Bitmask of backend types we can use to render content
  uint32_t mContentBackendBitmask;

  mozilla::widget::GfxInfoCollector<gfxPlatform> mAzureCanvasBackendCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mApzSupportCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mFrameStatsCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mCMSInfoCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mDisplayInfoCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mOverlayInfoCollector;
  mozilla::widget::GfxInfoCollector<gfxPlatform> mSwapChainInfoCollector;

  nsTArray<mozilla::layers::FrameStats> mFrameStats;

  // Backend that we are compositing with. NONE, if no compositor has been
  // created yet.
  mozilla::layers::LayersBackend mCompositorBackend;

  mozilla::Maybe<mozilla::layers::OverlayInfo> mOverlayInfo;
  mozilla::Maybe<mozilla::layers::SwapChainInfo> mSwapChainInfo;

  // An instance of gfxSkipChars which is empty. It is used as the
  // basis for error-case iterators.
  const gfxSkipChars kEmptySkipChars;
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(gfxPlatform::GlobalReflowFlags)

CMSMode GfxColorManagementMode();

#endif /* GFX_PLATFORM_H */
