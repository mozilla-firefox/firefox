/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef _nsRFPService_h_
#define _nsRFPService_h_

#include <cstdint>
#include <bitset>
#include "ErrorList.h"
#include "PLDHashTable.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/dom/MediaDeviceInfoBinding.h"
#include "mozilla/dom/ScreenOrientationBinding.h"
#include "js/RealmOptions.h"
#include "nsHashtablesFwd.h"
#include "nsICookieJarSettings.h"
#include "nsIFingerprintingWebCompatService.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsIRFPService.h"
#include "nsStringFwd.h"

// Defines regarding spoofed values of Navigator object. These spoofed values
// are returned when 'privacy.resistFingerprinting' is true.
// We decided to give different spoofed values according to the platform. The
// reason is that it is easy to detect the real platform. So there is no benefit
// for hiding the platform: it only brings breakages, like keyboard shortcuts
// won't work in macOS if we spoof it as a Windows platform.

// We use this value for Desktop mode in Android.
// That's why it is defined here, outside of
// the platform-specific definitions.
#define SPOOFED_UA_OS_OTHER "X11; Linux x86_64"

#ifdef XP_WIN
#  define SPOOFED_UA_OS "Windows NT 10.0; Win64; x64"
#  define SPOOFED_APPVERSION "5.0 (Windows)"
#  define SPOOFED_OSCPU "Windows NT 10.0; Win64; x64"
#  define SPOOFED_MAX_TOUCH_POINTS 10
#elif defined(XP_MACOSX)
#  define SPOOFED_UA_OS "Macintosh; Intel Mac OS X 10.15"
#  define SPOOFED_APPVERSION "5.0 (Macintosh)"
#  define SPOOFED_OSCPU "Intel Mac OS X 10.15"
#  define SPOOFED_MAX_TOUCH_POINTS 0
#elif defined(MOZ_WIDGET_ANDROID)
#  define SPOOFED_UA_OS "Android 10; Mobile"
#  define SPOOFED_APPVERSION "5.0 (Android 10)"
#  define SPOOFED_OSCPU "Linux armv81"
#  define SPOOFED_MAX_TOUCH_POINTS 5
#else
// For Linux and other platforms, like BSDs, SunOS and etc, we will use Linux
// platform.
#  define SPOOFED_UA_OS SPOOFED_UA_OS_OTHER
#  define SPOOFED_APPVERSION "5.0 (X11)"
#  define SPOOFED_OSCPU "Linux x86_64"
#  define SPOOFED_MAX_TOUCH_POINTS 5
#endif

#define LEGACY_BUILD_ID "20181001000000"
#define LEGACY_UA_GECKO_TRAIL "20100101"

#define SPOOFED_POINTER_INTERFACE MouseEvent_Binding::MOZ_SOURCE_MOUSE

struct JSContext;

class nsIChannel;

class nsICanvasRenderingContextInternal;

namespace mozilla {
class WidgetKeyboardEvent;
class OriginAttributes;
class OriginAttributesPattern;
namespace dom {
class Document;
enum class CanvasContextType : uint8_t;
}  // namespace dom
namespace gfx {
class DataSourceSurface;
}  // namespace gfx

enum KeyboardLang { EN = 0x01 };

#define RFP_KEYBOARD_LANG_STRING_EN "en"

typedef uint8_t KeyboardLangs;

enum KeyboardRegion { US = 0x01 };

#define RFP_KEYBOARD_REGION_STRING_US "US"

typedef uint8_t KeyboardRegions;

// This struct has the information about how to spoof the keyboardEvent.code,
// keyboardEvent.keycode and modifier states.
struct SpoofingKeyboardCode {
  CodeNameIndex mCode;
  uint8_t mKeyCode;
  Modifiers mModifierStates;
};

struct SpoofingKeyboardInfo {
  nsString mKey;
  KeyNameIndex mKeyIdx;
  SpoofingKeyboardCode mSpoofingCode;
};

class KeyboardHashKey : public PLDHashEntryHdr {
 public:
  typedef const KeyboardHashKey& KeyType;
  typedef const KeyboardHashKey* KeyTypePointer;

  KeyboardHashKey(const KeyboardLangs aLang, const KeyboardRegions aRegion,
                  const KeyNameIndex aKeyIdx, const nsAString& aKey);

  explicit KeyboardHashKey(KeyTypePointer aOther);

  KeyboardHashKey(KeyboardHashKey&& aOther) noexcept;

  ~KeyboardHashKey();

  bool KeyEquals(KeyTypePointer aOther) const;

  static KeyTypePointer KeyToPointer(KeyType aKey);

  static PLDHashNumber HashKey(KeyTypePointer aKey);

  enum { ALLOW_MEMMOVE = true };

  KeyboardLangs mLang;
  KeyboardRegions mRegion;
  KeyNameIndex mKeyIdx;
  nsString mKey;
};

// ============================================================================

// Reduce Timer Precision (RTP) Caller Type
enum class RTPCallerType : uint8_t {
  Normal = 0,
  SystemPrincipal = (1 << 0),
  ResistFingerprinting = (1 << 1),
  CrossOriginIsolated = (1 << 2)
};

inline JS::RTPCallerTypeToken RTPCallerTypeToToken(RTPCallerType aType) {
  return JS::RTPCallerTypeToken{uint8_t(aType)};
}

inline RTPCallerType RTPCallerTypeFromToken(JS::RTPCallerTypeToken aToken) {
  MOZ_RELEASE_ASSERT(
      aToken.value == uint8_t(RTPCallerType::Normal) ||
      aToken.value == uint8_t(RTPCallerType::SystemPrincipal) ||
      aToken.value == uint8_t(RTPCallerType::ResistFingerprinting) ||
      aToken.value == uint8_t(RTPCallerType::CrossOriginIsolated));
  return static_cast<RTPCallerType>(aToken.value);
}

enum TimerPrecisionType {
  DangerouslyNone = 1,
  UnconditionalAKAHighRes = 2,
  Normal = 3,
  RFP = 4,
};

// ============================================================================

enum class CanvasFeatureUsage : uint64_t {
  None = 0,

  KnownText_1 = 1llu << 0,
  KnownText_2 = 1llu << 1,
  KnownText_3 = 1llu << 2,
  KnownText_4 = 1llu << 3,
  KnownText_5 = 1llu << 4,
  KnownText_6 = 1llu << 5,
  KnownText_7 = 1llu << 6,
  KnownText_8 = 1llu << 7,
  KnownText_9 = 1llu << 8,
  KnownText_10 = 1llu << 9,
  KnownText_11 = 1llu << 10,
  KnownText_12 = 1llu << 11,
  KnownText_13 = 1llu << 12,
  KnownText_14 = 1llu << 13,
  KnownText_15 = 1llu << 14,
  KnownText_16 = 1llu << 15,
  KnownText_17 = 1llu << 16,
  KnownText_18 = 1llu << 17,
  KnownText_19 = 1llu << 18,
  KnownText_20 = 1llu << 19,
  KnownText_21 = 1llu << 20,
  KnownText_22 = 1llu << 21,
  KnownText_23 = 1llu << 22,
  KnownText_24 = 1llu << 23,
  KnownText_25 = 1llu << 24,
  KnownText_26 = 1llu << 25,
  KnownText_27 = 1llu << 26,
  KnownText_28 = 1llu << 27,
  KnownText_29 = 1llu << 28,
  KnownText_30 = 1llu << 29,
  KnownText_31 = 1llu << 30,
  KnownText_32 = 1llu << 31,

  SetFont = 1llu << 32,
  FillRect = 1llu << 33,
  LineTo = 1llu << 34,
  Stroke = 1llu << 35,
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(CanvasFeatureUsage);

// We try to classify observed canvas fingerprinting scripts into different
// classes, but we don't usually know the source/vendor of those scripts. The
// classification is based on a behavioral analysis covering things like the
// functions called and size of the canvas. The alias given is a guess and
// should not be considered definitive. The entity identified may not be doing
// this behavior at all, they may be doing a diferent or additional behaviors.
enum CanvasFingerprinterAlias {
  eNoneIdentified = 0,
  eFingerprintJS = 1,
  eAkamai = 2,
  eOzoki = 3,
  ePerimeterX = 4,
  eSignifyd = 5,
  eClaydar = 6,
  eForter = 7,

  // Variants are unknown but distinct types of fingerprinters
  eVariant1 = 8,
  eVariant2 = 9,
  eVariant3 = 10,
  eVariant4 = 11,
  eVariant5 = 12,
  eVariant6 = 13,
  eVariant7 = 14,
  eVariant8 = 15,

  eClientGear = 16,
  eImperva = 17,
  eLastAlias = eImperva
};

enum CanvasExtractionAPI : uint8_t {
  ToDataURL = 0,
  ToBlob = 1,
  GetImageData = 2,
  ReadPixels = 3
};

enum CanvasUsageSource : uint64_t {
  Unknown = 0,
  Impossible =
      1llu << 0,  // This represents API calls that I don't believe are possible
  MainThread_Canvas_ImageBitmap_toDataURL = 1llu << 1,
  MainThread_Canvas_ImageBitmap_toBlob = 1llu << 2,
  MainThread_Canvas_ImageBitmap_getImageData = 1llu << 3,

  MainThread_Canvas_Canvas2D_toDataURL = 1llu << 4,
  MainThread_Canvas_Canvas2D_toBlob = 1llu << 5,
  MainThread_Canvas_Canvas2D_getImageData = 1llu << 6,

  MainThread_Canvas_WebGL_toDataURL = 1llu << 7,
  MainThread_Canvas_WebGL_toBlob = 1llu << 8,
  MainThread_Canvas_WebGL_getImageData = 1llu << 9,
  MainThread_Canvas_WebGL_readPixels = 1llu << 10,

  MainThread_Canvas_WebGPU_toDataURL = 1llu << 11,
  MainThread_Canvas_WebGPU_toBlob = 1llu << 12,
  MainThread_Canvas_WebGPU_getImageData = 1llu << 13,

  MainThread_OffscreenCanvas_ImageBitmap_toDataURL = 1llu << 14,
  MainThread_OffscreenCanvas_ImageBitmap_toBlob = 1llu << 15,
  MainThread_OffscreenCanvas_ImageBitmap_getImageData = 1llu << 16,

  MainThread_OffscreenCanvas_Canvas2D_toDataURL = 1llu << 17,
  MainThread_OffscreenCanvas_Canvas2D_toBlob = 1llu << 18,
  MainThread_OffscreenCanvas_Canvas2D_getImageData = 1llu << 19,

  MainThread_OffscreenCanvas_WebGL_toDataURL = 1llu << 20,
  MainThread_OffscreenCanvas_WebGL_toBlob = 1llu << 21,
  MainThread_OffscreenCanvas_WebGL_getImageData = 1llu << 22,
  MainThread_OffscreenCanvas_WebGL_readPixels = 1llu << 23,

  MainThread_OffscreenCanvas_WebGPU_toDataURL = 1llu << 24,
  MainThread_OffscreenCanvas_WebGPU_toBlob = 1llu << 25,
  MainThread_OffscreenCanvas_WebGPU_getImageData = 1llu << 26,

  Worker_OffscreenCanvas_ImageBitmap_toBlob = 1llu << 27,
  Worker_OffscreenCanvas_ImageBitmap_getImageData = 1llu << 28,

  Worker_OffscreenCanvas_Canvas2D_toBlob = 1llu << 29,
  Worker_OffscreenCanvas_Canvas2D_getImageData = 1llu << 30,

  Worker_OffscreenCanvasCanvas2D_Canvas2D_toBlob = 1llu << 31,
  Worker_OffscreenCanvasCanvas2D_Canvas2D_getImageData = 1llu << 32,

  Worker_OffscreenCanvas_WebGL_toBlob = 1llu << 33,
  Worker_OffscreenCanvas_WebGL_getImageData = 1llu << 34,
  Worker_OffscreenCanvas_WebGL_readPixels = 1llu << 35,

  Worker_OffscreenCanvas_WebGPU_toBlob = 1llu << 36,
  Worker_OffscreenCanvas_WebGPU_getImageData = 1llu << 37,

  // --- New entires added here to preserve original values (no reordering!)
  MainThread_Canvas_OffscreenCanvas2D_getImageData = 1llu << 38,
  MainThread_Canvas_OffscreenCanvas2D_toBlob = 1llu << 39,

  // After you go past 40 - metrics.yaml will need to be updated

};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(CanvasUsageSource);
nsCString CanvasUsageSourceToString(CanvasUsageSource aSource);

class CanvasUsage {
 public:
  CSSIntSize mSize;
  dom::CanvasContextType mType;
  CanvasUsageSource mUsageSource;
  CanvasFeatureUsage mFeatureUsage;

  CanvasUsage(CSSIntSize aSize, dom::CanvasContextType aType,
              CanvasUsageSource aUsageSource, CanvasFeatureUsage aFeatureUsage)
      : mSize(aSize),
        mType(aType),
        mUsageSource(aUsageSource),
        mFeatureUsage(aFeatureUsage) {}

  static CanvasUsage CreateUsage(
      bool aIsOffscreen, dom::CanvasContextType aContextType,
      CanvasExtractionAPI aApi, CSSIntSize aSize,
      const nsICanvasRenderingContextInternal* aContext);

  static inline CanvasUsageSource GetCanvasUsageSource(
      bool isOffscreen, dom::CanvasContextType contextType,
      CanvasExtractionAPI api);
};
struct CanvasFingerprintingEvent {
  // The identity or alias of the entity doing the fingerprinting.
  CanvasFingerprinterAlias alias;
  // A bitmask of all of the known canvas fingerprinting texts
  //   non-zero indicates some known fingerprinting text was used, making the
  //   event highly likely to be fingerprinting.
  uint32_t knownTextBitmask;
  // A bitmap of all the sources that were used to extract canvas data
  uint64_t sourcesBitmask;

  CanvasFingerprintingEvent()
      : alias(CanvasFingerprinterAlias::eNoneIdentified),
        knownTextBitmask(0),
        sourcesBitmask(0) {}

  CanvasFingerprintingEvent(CanvasFingerprinterAlias aAlias,
                            uint32_t aKnownTextBitmask,
                            uint64_t aSourcesBitmask)
      : alias(aAlias),
        knownTextBitmask(aKnownTextBitmask),
        sourcesBitmask(aSourcesBitmask) {}

  bool operator==(const CanvasFingerprintingEvent& other) const = default;
};

// ============================================================================

// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define ITEM_VALUE(name, val) name = val,

// The definition for fingerprinting protections. Each enum represents one
// fingerprinting protection that targets one specific WebAPI our fingerprinting
// surface. The enums can be found in RFPTargets.inc.
enum class RFPTarget : uint64_t {
#include "RFPTargets.inc"
};

#undef ITEM_VALUE

using RFPTargetSet = EnumSet<RFPTarget, std::bitset<128>>;

template <>
struct MaxEnumValue<RFPTarget> {
  static constexpr unsigned int value = 127;
};

// ============================================================================

class nsRFPService final : public nsIObserver, public nsIRFPService {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIRFPSERVICE

  static already_AddRefed<nsRFPService> GetOrCreate();

  // _Rarely_ you will need to know if RFP is enabled, or if FPP is enabled.
  // 98% of the time you should use nsContentUtils::ShouldResistFingerprinting
  // as the difference will not matter to you.
  static bool IsRFPPrefEnabled(bool aIsPrivateMode);

  static bool IsRFPEnabledFor(
      bool aIsPrivateMode, RFPTarget aTarget,
      const Maybe<RFPTargetSet>& aOverriddenFingerprintingSettings);

  static bool IsSystemPrincipalOrAboutFingerprintingProtection(JSContext*,
                                                               JSObject*);

  // --------------------------------------------------------------------------
  static double TimerResolution(RTPCallerType aRTPCallerType);

  enum TimeScale { Seconds = 1, MilliSeconds = 1000, MicroSeconds = 1000000 };

  // Initializes the process-wide state used by the ReduceTimePrecision*
  // methods, so that they don't lock or allocate when later called on a
  // real-time thread. Main thread only.
  static void InitTimePrecisionReduction();

  // The following Reduce methods can be called off main thread.
  static double ReduceTimePrecisionAsUSecs(double aTime, int64_t aContextMixin,
                                           RTPCallerType aRTPCallerType);
  static double ReduceTimePrecisionAsMSecs(double aTime, int64_t aContextMixin,
                                           RTPCallerType aRTPCallerType);
  static double ReduceTimePrecisionAsMSecsRFPOnly(double aTime,
                                                  int64_t aContextMixin,
                                                  RTPCallerType aRTPCallerType);
  static double ReduceTimePrecisionAsSecs(double aTime, int64_t aContextMixin,
                                          RTPCallerType aRTPCallerType);
  static double ReduceTimePrecisionAsSecsRFPOnly(double aTime,
                                                 int64_t aContextMixin,
                                                 RTPCallerType aRTPCallerType);
  // Public only for testing purposes
  static double ReduceTimePrecisionImpl(double aTime, TimeScale aTimeScale,
                                        double aResolutionUSec,
                                        int64_t aContextMixin,
                                        TimerPrecisionType aType);
  static nsresult RandomMidpoint(long long aClampedTimeUSec,
                                 long long aResolutionUSec,
                                 int64_t aContextMixin, long long* aMidpointOut,
                                 uint8_t* aSecretSeed = nullptr);

  // --------------------------------------------------------------------------

  // This method calculates the video resolution (i.e. height x width) based
  // on the video quality (480p, 720p, etc).
  static uint32_t CalculateTargetVideoResolution(uint32_t aVideoQuality);

  // Methods for getting spoofed media statistics and the return value will
  // depend on the video resolution.
  static uint32_t GetSpoofedTotalFrames(double aTime);
  static uint32_t GetSpoofedDroppedFrames(double aTime, uint32_t aWidth,
                                          uint32_t aHeight);
  static uint32_t GetSpoofedPresentedFrames(double aTime, uint32_t aWidth,
                                            uint32_t aHeight);

  // --------------------------------------------------------------------------

  // This method generates the spoofed value of User Agent.
  static void GetSpoofedUserAgent(nsACString& userAgent,
                                  bool aAndroidDesktopMode = false);

  // --------------------------------------------------------------------------

  // This method generates the locale string (e.g. "en-US") that should be
  // spoofed by the JavaScript engine.
  static nsCString GetSpoofedJSLocale();

  // --------------------------------------------------------------------------

  // This method generates the time zone string (e.g. "Atlantic/Reykjavik") that
  // should be spoofed by the JavaScript engine.
  static nsCString GetSpoofedJSTimeZone();

  // --------------------------------------------------------------------------

  /**
   * This method for getting spoofed modifier states for the given keyboard
   * event.
   *
   * @param aDoc           [in]  the owner's document for getting content
   *                             language.
   * @param aKeyboardEvent [in]  the keyboard event that needs to be spoofed.
   * @param aModifier      [in]  the modifier that needs to be spoofed.
   * @param aOut           [out] the spoofed state for the given modifier.
   * @return               true if there is a spoofed state for the modifier.
   */
  static bool GetSpoofedModifierStates(
      const mozilla::dom::Document* aDoc,
      const WidgetKeyboardEvent* aKeyboardEvent, const Modifiers aModifier,
      bool& aOut);

  /**
   * This method for getting spoofed code for the given keyboard event.
   *
   * @param aDoc           [in]  the owner's document for getting content
   *                             language.
   * @param aKeyboardEvent [in]  the keyboard event that needs to be spoofed.
   * @param aOut           [out] the spoofed code.
   * @return               true if there is a spoofed code in the fake keyboard
   *                       layout.
   */
  static bool GetSpoofedCode(const dom::Document* aDoc,
                             const WidgetKeyboardEvent* aKeyboardEvent,
                             nsAString& aOut);

  /**
   * This method for getting spoofed keyCode for the given keyboard event.
   *
   * @param aDoc           [in]  the owner's document for getting content
   *                             language.
   * @param aKeyboardEvent [in]  the keyboard event that needs to be spoofed.
   * @param aOut           [out] the spoofed keyCode.
   * @return               true if there is a spoofed keyCode in the fake
   *                       keyboard layout.
   */
  static bool GetSpoofedKeyCode(const mozilla::dom::Document* aDoc,
                                const WidgetKeyboardEvent* aKeyboardEvent,
                                uint32_t& aOut);

  // --------------------------------------------------------------------------

  // The method to generate the key for randomization. It can return nothing if
  // the session key is not available due to the randomization is disabled.
  static Maybe<nsTArray<uint8_t>> GenerateKey(nsIChannel* aChannel);
  static Maybe<nsTArray<uint8_t>> GenerateKeyForServiceWorker(
      nsIURI* aFirstPartyURI, nsIPrincipal* aPrincipal,
      bool aForeignByAncestorContext);

  static void PotentiallyDumpImage(nsIPrincipal* aPrincipal,
                                   gfx::DataSourceSurface* aSurface);
  static void PotentiallyDumpImage(nsIPrincipal* aPrincipal, uint8_t* aData,
                                   uint32_t aWidth, uint32_t aHeight,
                                   uint32_t aSize);

  // This function is plumbed to RandomizeElements function.
  static nsresult RandomizePixels(nsICookieJarSettings* aCookieJarSettings,
                                  nsIPrincipal* aPrincipal, uint8_t* aData,
                                  uint32_t aWidth, uint32_t aHeight,
                                  uint32_t aSize,
                                  mozilla::gfx::SurfaceFormat aSurfaceFormat);
  // This function is used to randomize the elements in the given data
  // according to the given parameters. For example, for an RGBA pixel, by group
  // in this context, we refer to a single pixel and by element, we refer to
  // channels. So, if we have an RGBA pixel, we have 4 elements per group, i.e.
  // 4 channels per pixel. We use aElementsPerGroup to randomize at most one of
  // elements per group, i.e. one channel per pixel.
  static nsresult RandomizeElements(
      nsICookieJarSettings* aCookieJarSettings, nsIPrincipal* aPrincipal,
      uint8_t* aData, uint32_t aSizeInBytes, uint8_t aElementsPerGroup,
      uint8_t aBytesPerElement, uint8_t aElementOffset, bool aSkipLastElement);

  // --------------------------------------------------------------------------

  // The method for getting the granular fingerprinting protection override of
  // the given channel. Due to WebCompat reason, there can be a granular
  // overrides to replace default enabled RFPTargets for the context of the
  // channel. The method will return Nothing() to indicate using the default
  // RFPTargets
  static Maybe<RFPTargetSet> GetOverriddenFingerprintingSettingsForChannel(
      nsIChannel* aChannel);

  // The method for getting the granular fingerprinting protection override of
  // the given first-party and third-party URIs. It will return the granular
  // overrides if there is one defined for the context of the first-party URI
  // and third-party URI. Otherwise, it will return Nothing() to indicate using
  // the default RFPTargets.
  static Maybe<RFPTargetSet> GetOverriddenFingerprintingSettingsForURI(
      nsIURI* aFirstPartyURI, nsIURI* aThirdPartyURI, bool aIsPrivate);

  // --------------------------------------------------------------------------

  static void MaybeReportCanvasFingerprinter(nsTArray<CanvasUsage>& aUses,
                                             nsIChannel* aChannel, nsIURI* aURI,
                                             const nsACString& aOriginNoSuffix);

  static void MaybeReportFontFingerprinter(nsIChannel* aChannel, nsIURI* aURI,
                                           const nsACString& aOriginNoSuffix);

  // --------------------------------------------------------------------------

  // Generates a fake media device name with given kind and index.
  // Example: Internal Microphone
  static void GetMediaDeviceName(nsString& aName,
                                 mozilla::dom::MediaDeviceKind aKind);

  // Generates a fake media device group name with given kind and index.
  // Example: Audio Group
  static void GetMediaDeviceGroup(nsString& aGroup,
                                  mozilla::dom::MediaDeviceKind aKind);

  // Converts the viewport size to the angle.
  static uint16_t ViewportSizeToAngle(int32_t aWidth, int32_t aHeight);

  // Converts the viewport size to the orientation type.
  static dom::OrientationType ViewportSizeToOrientationType(int32_t aWidth,
                                                            int32_t aHeight);

  // Returns the default orientation type for the given platform.
  static dom::OrientationType GetDefaultOrientationType();

  // Returns the default pixel density for RFP.
  static float GetDefaultPixelDensity();

  // Returns the device pixel ratio at the given zoom level.
  static double GetDevicePixelRatioAtZoom(float aZoom);

  // Returns the value of privacy.resistFingerprinting.exemptedDomains pref
  static void GetExemptedDomainsLowercase(nsCString& aExemptedDomains);

  static CSSIntRect GetSpoofedScreenAvailSize(const nsRect& aRect, float aScale,
                                              bool aIsFullscreen);

  static int64_t GetSpoofedStorageLimit();

  static bool ExposeWebCodecsAPI(JSContext* aCx, JSObject* aObj);
  static bool ExposeWebCodecsAPIImageDecoder(JSContext* aCx, JSObject* aObj);
  static bool IsWebCodecsRFPTargetEnabled(JSContext* aCx);

  static uint32_t CollapseMaxTouchPoints(uint32_t aMaxTouchPoints);

  static void CalculateFontLocaleAllowlist();
  static bool FontIsAllowedByLocale(const nsACString& aName);

  static Maybe<RFPTarget> TextToRFPTarget(const nsAString& aText);

  static void GetFingerprintingRandomizationKeyAsString(
      nsICookieJarSettings* aCookieJarSettings,
      nsACString& aRandomizationKeyStr);

  static nsresult GenerateRandomizationKeyFromHash(
      const nsACString& aRandomizationKeyStr, uint32_t aContentHash,
      nsACString& aHex);

 private:
  nsresult Init();

  nsRFPService() = default;

  ~nsRFPService() = default;

  void UpdateFPPOverrideList();
  void StartShutdown();

  void PrefChanged(const char* aPref);
  static void PrefChanged(const char* aPref, void* aSelf);

  // --------------------------------------------------------------------------

  static void MaybeCreateSpoofingKeyCodes(const KeyboardLangs aLang,
                                          const KeyboardRegions aRegion);
  static void MaybeCreateSpoofingKeyCodesForEnUS();

  static void GetKeyboardLangAndRegion(const nsAString& aLanguage,
                                       KeyboardLangs& aLocale,
                                       KeyboardRegions& aRegion);
  static bool GetSpoofedKeyCodeInfo(const mozilla::dom::Document* aDoc,
                                    const WidgetKeyboardEvent* aKeyboardEvent,
                                    SpoofingKeyboardCode& aOut);

  static nsTHashMap<KeyboardHashKey, const SpoofingKeyboardCode*>*
      sSpoofingKeyboardCodes;

  // --------------------------------------------------------------------------

  // Used by the JS Engine
  static double ReduceTimePrecisionAsUSecsWrapper(
      double aTime, JS::RTPCallerTypeToken aCallerType, JSContext* aCx);

  static TimerPrecisionType GetTimerPrecisionType(RTPCallerType aRTPCallerType);

  static TimerPrecisionType GetTimerPrecisionTypeRFPOnly(
      RTPCallerType aRTPCallerType);

  static void TypeToText(TimerPrecisionType aType, nsACString& aText);

  // --------------------------------------------------------------------------

  // A helper function to generate canvas key from the given image data and
  // randomization key.
  static nsresult GenerateCanvasKeyFromImageData(
      nsICookieJarSettings* aCookieJarSettings, uint8_t* aImageData,
      uint32_t aSize, nsTArray<uint8_t>& aCanvasKey);

  // Generate the session key if it hasn't been generated.
  nsresult GetBrowsingSessionKey(const OriginAttributes& aOriginAttributes,
                                 nsID& aBrowsingSessionKey);
  void ClearBrowsingSessionKey(const OriginAttributesPattern& aPattern);
  void ClearBrowsingSessionKey(const OriginAttributes& aOriginAttributes);

  // The keys that represent the browsing session. The lifetime of the key ties
  // to the browsing session. For normal windows, the key is generated when
  // loading the first http channel after the browser startup and persists until
  // the browser shuts down. For private windows, the key is generated when
  // opening a http channel on a private window and reset after all private
  // windows close, i.e. private browsing session ends.
  //
  // The key will be used to generate the randomization noise used to fiddle the
  // browser fingerprints. Note that this key lives and can only be accessed in
  // the parent process.
  nsTHashMap<nsCStringHashKey, nsID> mBrowsingSessionKeys;

  nsCOMPtr<nsIFingerprintingWebCompatService> mWebCompatService;
  nsTHashMap<nsCStringHashKey, RFPTargetSet> mFingerprintingOverrides;

  // A helper function to create the domain key for the fingerprinting
  // overrides. The key can be in the following five formats.
  // 1. {first-party domain}: The override only apply to the first-party domain.
  // 2. {first-party domain, *}: The overrides apply to every contexts under the
  //    top-level domain, including itself.
  // 3. {*, third-party domain}: The overrides apply to the third-party domain
  //    under any top-level domain.
  // 4. {first-party domain, third-party domain}: the overrides apply to the
  //    specific third-party domain under the given first-party domain.
  // 5. {*}: A global overrides that will apply to every context.
  static nsresult CreateOverrideDomainKey(nsIFingerprintingOverride* aOverride,
                                          nsACString& aDomainKey);

  // A helper function to create the RFPTarget bitfield based on the given
  // overrides text and the based overrides bitfield. The function will parse
  // the text and update the based overrides bitfield accordingly. Then, it will
  // return the updated bitfield.
  static RFPTargetSet CreateOverridesFromText(
      const nsString& aOverridesText,
      RFPTargetSet aBaseOverrides = RFPTargetSet());

  enum FingerprintingProtectionType : uint8_t {
    RFP,
    FPP,
    Baseline,
    None,
  };

  static FingerprintingProtectionType GetFingerprintingProtectionType(
      bool aIsPrivateMode);

  static Maybe<bool> HandleExceptionalRFPTargets(
      RFPTarget aTarget, bool aIsPrivateMode,
      FingerprintingProtectionType aMode);

  static bool IsTargetActiveForMode(RFPTarget aTarget,
                                    FingerprintingProtectionType aMode);

  static nsCString* sExemptedDomainsLowercase;
};

}  // namespace mozilla

#endif /* _nsRFPService_h_ */
