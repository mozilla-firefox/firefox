/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Names should not be re-used.

ITEM_VALUE(TouchEvents,                 1)
ITEM_VALUE(PointerEvents,               2)
ITEM_VALUE(KeyboardEvents,              3)
ITEM_VALUE(ScreenOrientation,           4)
// SpeechSynthesis part of the Web Speech API
ITEM_VALUE(SpeechSynthesis,             5)
// `prefers-color-scheme` CSS media feature
ITEM_VALUE(CSSPrefersColorScheme,       6)
// `prefers-reduced-motion` CSS media feature
ITEM_VALUE(CSSPrefersReducedMotion,     7)
// `prefers-contrast` CSS media feature
ITEM_VALUE(CSSPrefersContrast,          8)
// Add random noises to image data extracted from canvas.
ITEM_VALUE(CanvasRandomization,         9)
// Canvas targets: For unusual combinations of these, see comments
// in IsImageExtractionAllowed
ITEM_VALUE(CanvasImageExtractionPrompt, 10)
ITEM_VALUE(CanvasExtractionFromThirdPartiesIsBlocked, 11)
ITEM_VALUE(CanvasExtractionBeforeUserInputIsBlocked,  12)
// See also EfficientCanvasRandomization = 76 below

ITEM_VALUE(JSLocale,                13)

// Various "client identification" values of the navigator object
ITEM_VALUE(NavigatorAppVersion,         14)
ITEM_VALUE(NavigatorBuildID,            15)
ITEM_VALUE(NavigatorHWConcurrency,      16)
ITEM_VALUE(NavigatorOscpu,              17)
ITEM_VALUE(NavigatorPlatform,           18)
ITEM_VALUE(NavigatorUserAgent,          19)
// If the user disables PDFJS via a pref, we do not expose that to the web
ITEM_VALUE(PdfjsSpoof,                  20)
ITEM_VALUE(StreamVideoFacingMode,       21)
ITEM_VALUE(JSDateTimeUTC,               22)
ITEM_VALUE(JSMathFdlibm,                23)
ITEM_VALUE(Gamepad,                     24)
ITEM_VALUE(HttpUserAgent,               25)
ITEM_VALUE(WindowOuterSize,             26)
ITEM_VALUE(WindowScreenXY,              27)
ITEM_VALUE(WindowInnerScreenXY,         28)
ITEM_VALUE(ScreenPixelDepth,            29)
ITEM_VALUE(ScreenRect,                  30)
ITEM_VALUE(ScreenAvailRect,             31)
// HTMLVideoElement
// mozParsedFrames, mozDecodedFrames, mozPresentedFrames, mozPaintedFrames
ITEM_VALUE(VideoElementMozFrames,       32)
// mozFrameDelay
ITEM_VALUE(VideoElementMozFrameDelay,   33)
// getVideoPlaybackQuality()
ITEM_VALUE(VideoElementPlaybackQuality, 34)
// See also Reduce Timer Precision (RTP) Caller Type
ITEM_VALUE(ReduceTimerPrecision,        35)
// Hide keyboard and pointer WidgetEvents
ITEM_VALUE(WidgetEvents,                36)
ITEM_VALUE(MediaDevices,                37)
ITEM_VALUE(MediaCapabilities,           38)
ITEM_VALUE(AudioSampleRate,             39)
ITEM_VALUE(NetworkConnection,           40)
ITEM_VALUE(WindowDevicePixelRatio,      41)
ITEM_VALUE(MouseEventScreenPoint,       42)
// Visibility level of font families available to CSS font-matching
ITEM_VALUE(FontVisibilityBaseSystem,    43)
ITEM_VALUE(FontVisibilityLangPack,      44)
ITEM_VALUE(DeviceSensors,               45)
ITEM_VALUE(FrameRate,                   46)
ITEM_VALUE(RoundWindowSize,             47)
ITEM_VALUE(UseStandinsForNativeColors,  48)
ITEM_VALUE(AudioContext,                49)
ITEM_VALUE(MediaError,                  50)
ITEM_VALUE(DOMStyleOsxFontSmoothing,    51)
// `device-height`/`device-width` CSS media features
ITEM_VALUE(CSSDeviceSize,               52)
// `color`/`color-gamut` CSS media features
ITEM_VALUE(CSSColorInfo,                53)
// `resolution` CSS media feature
ITEM_VALUE(CSSResolution,               54)
// `prefers-reduced-transparency` CSS media feature
ITEM_VALUE(CSSPrefersReducedTransparency, 55)
// `inverted-colors` CSS media feature
ITEM_VALUE(CSSInvertedColors,           56)
// `video-dynamic-range` CSS media feature
ITEM_VALUE(CSSVideoDynamicRange,        57)
ITEM_VALUE(CSSPointerCapabilities,      58)
// WebGL
ITEM_VALUE(WebGLRenderCapability,       59)
ITEM_VALUE(WebGLRenderInfo,             60)
ITEM_VALUE(SiteSpecificZoom,            61)
// Are font visibility restrictions applied when resolving a CSS <generic-family>?
// (This may block the fonts selected in Preferences from actually being used.)
ITEM_VALUE(FontVisibilityRestrictGenerics, 62)
ITEM_VALUE(WebVTT,                      63)
ITEM_VALUE(WebGPULimits,                64)
ITEM_VALUE(WebGPUIsFallbackAdapter,     65)
ITEM_VALUE(WebGPUSubgroupSizes,         66)
ITEM_VALUE(JSLocalePrompt,              67)
ITEM_VALUE(ScreenAvailToResolution,     68)
ITEM_VALUE(UseHardcodedFontSubstitutes, 69)
ITEM_VALUE(DiskStorageLimit,            70)
ITEM_VALUE(WebCodecs,                   71)
ITEM_VALUE(MaxTouchPoints,              72)
ITEM_VALUE(MaxTouchPointsCollapse,      73)
ITEM_VALUE(NavigatorHWConcurrencyTiered,74)
ITEM_VALUE(WebGLRandomization,          75)
ITEM_VALUE(EfficientCanvasRandomization, 76)
ITEM_VALUE(WebGLVendorSanitize,         77)
ITEM_VALUE(WebGLVendorConstant,         78)
ITEM_VALUE(WebGLVendorRandomize,        79)
ITEM_VALUE(WebGLRendererConstant,       80)
// How IME software in use chooses to style composition (exposed via EditContext)
ITEM_VALUE(IMEStyle,                    81)

/*
 * In certain cases, we precompute the value of ShouldRFP for e.g. a Document.
 * (This saves us more computation and casting later.)  This document will still
 * need to check whether an individual target is allowed, but the initial
 * question of "Does this document have any RFP applied to it ever?" can still
 * be precomputed. This enum value will always be included in FPP, so when a
 * document asks if they might have RFP enabled, it will return true. (Putting
 * this value in the overrides pref is undefined behavior and may do anything.)
 */
ITEM_VALUE(IsAlwaysEnabledForPrecompute, 0)

/*
 * Some users desperately want the entire ResistFingerprinting experience, except
 * one particular behavior (usually TimeZone or ColorScheme.)  This value enables
 * them to specify an override list that will include or exclude everything,
 * allowing them to opt-in or opt-out of new RFPTargets we add in Firefox or to
 * the default set enabled.  It should come first, otherwise behavior is undefined.
 * Examples:
 *    +AllTargets,-CSSPrefersColorScheme
 *    -AllTargets,+Gamepad
 */
ITEM_VALUE(AllTargets, 127)
