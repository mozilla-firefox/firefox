/** Test for Bug 766694 **/

// This is a list of all interfaces that are exposed to every webpage.
// Please only add things to this list with great care and proper review
// from the associated module peers.
//
// The test is supposed to check whether our actual exposure behavior
// matches what we expect, with the latter expressed in terms of outside
// observables like type of build (nightly, release), platform, secure
// context, etc. Testing based on prefs is thus the wrong check, as this
// means we'd also have to test whether the pref value matches what we
// expect in terms of outside observables.

// This file lists global interfaces we want exposed and verifies they
// are what we intend. Each entry in the arrays below can either be a
// simple string with the interface name, or an object with a 'name'
// property giving the interface name as a string, and additional
// properties which qualify the exposure of that interface. For example:
//
// [
//   "AGlobalInterface",
//   {name: "ExperimentalThing", release: false},
//   {name: "ReallyExperimentalThing", nightly: true},
//   {name: "DesktopOnlyThing", desktop: true},
//   {name: "DisabledEverywhere", disabled: true},
// ];
//
// See createInterfaceMap() below for a complete list of properties.
//
// The values of the properties need to be either literal true/false
// (e.g. indicating whether something is enabled on a particular
// channel/OS) or one of the is* constants below (in cases when
// exposure is affected by channel or OS in a nontrivial way).

const { AppConstants } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const isNightly = AppConstants.NIGHTLY_BUILD;
const isEarlyBetaOrEarlier = AppConstants.EARLY_BETA_OR_EARLIER;
const isRelease = AppConstants.RELEASE_OR_BETA;
const isDesktop = !/Mobile|Tablet/.test(navigator.userAgent);
const isMac = AppConstants.platform == "macosx";
const isWindows = AppConstants.platform == "win";
const isAndroid = AppConstants.platform == "android";
const isLinux = AppConstants.platform == "linux";
const isInsecureContext = !window.isSecureContext;
// Currently, MOZ_APP_NAME is always "fennec" for all mobile builds, so we can't use AppConstants for this
const isFennec =
  isAndroid &&
  SpecialPowers.Cc["@mozilla.org/android/bridge;1"].getService(
    SpecialPowers.Ci.nsIGeckoViewBridge
  ).isFennec;
const isCrossOriginIsolated = window.crossOriginIsolated;

// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let wasmGlobalEntry = {
  name: "WebAssembly",
  insecureContext: true,
  disabled:
    !SpecialPowers.Cu.getJSTestingFunctions().wasmIsSupportedByHardware(),
};
let wasmGlobalInterfaces = [
  { name: "Module", insecureContext: true },
  { name: "Instance", insecureContext: true },
  { name: "Memory", insecureContext: true },
  { name: "Table", insecureContext: true },
  { name: "Global", insecureContext: true },
  { name: "CompileError", insecureContext: true },
  { name: "LinkError", insecureContext: true },
  { name: "RuntimeError", insecureContext: true },
  { name: "Function", insecureContext: true, nightly: true },
  { name: "Exception", insecureContext: true },
  { name: "Tag", insecureContext: true },
  { name: "JSTag", insecureContext: true },
  { name: "compile", insecureContext: true },
  { name: "compileStreaming", insecureContext: true },
  { name: "instantiate", insecureContext: true },
  { name: "instantiateStreaming", insecureContext: true },
  { name: "validate", insecureContext: true },
];
// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let ecmaGlobals = [
  { name: "AggregateError", insecureContext: true },
  { name: "Array", insecureContext: true },
  { name: "ArrayBuffer", insecureContext: true },
  { name: "Atomics", insecureContext: true },
  { name: "BigInt", insecureContext: true },
  { name: "BigInt64Array", insecureContext: true },
  { name: "BigUint64Array", insecureContext: true },
  { name: "Boolean", insecureContext: true },
  { name: "DataView", insecureContext: true },
  { name: "Date", insecureContext: true },
  { name: "Error", insecureContext: true },
  { name: "EvalError", insecureContext: true },
  { name: "FinalizationRegistry", insecureContext: true },
  { name: "Float16Array", insecureContext: true },
  { name: "Float32Array", insecureContext: true },
  { name: "Float64Array", insecureContext: true },
  { name: "Function", insecureContext: true },
  { name: "Infinity", insecureContext: true },
  { name: "Int16Array", insecureContext: true },
  { name: "Int32Array", insecureContext: true },
  { name: "Int8Array", insecureContext: true },
  { name: "InternalError", insecureContext: true },
  { name: "Intl", insecureContext: true },
  { name: "Iterator", insecureContext: true },
  { name: "JSON", insecureContext: true },
  { name: "Map", insecureContext: true },
  { name: "Math", insecureContext: true },
  { name: "NaN", insecureContext: true },
  { name: "Number", insecureContext: true },
  { name: "Object", insecureContext: true },
  { name: "Promise", insecureContext: true },
  { name: "Proxy", insecureContext: true },
  { name: "RangeError", insecureContext: true },
  { name: "ReferenceError", insecureContext: true },
  { name: "Reflect", insecureContext: true },
  { name: "RegExp", insecureContext: true },
  { name: "Set", insecureContext: true },
  {
    name: "SharedArrayBuffer",
    insecureContext: true,
    crossOriginIsolated: true,
  },
  { name: "String", insecureContext: true },
  { name: "Symbol", insecureContext: true },
  { name: "SyntaxError", insecureContext: true },
  { name: "Temporal", insecureContext: true },
  { name: "TypeError", insecureContext: true },
  { name: "Uint16Array", insecureContext: true },
  { name: "Uint32Array", insecureContext: true },
  { name: "Uint8Array", insecureContext: true },
  { name: "Uint8ClampedArray", insecureContext: true },
  { name: "URIError", insecureContext: true },
  { name: "WeakMap", insecureContext: true },
  { name: "WeakRef", insecureContext: true },
  { name: "WeakSet", insecureContext: true },
  wasmGlobalEntry,
  { name: "decodeURI", insecureContext: true },
  { name: "decodeURIComponent", insecureContext: true },
  { name: "encodeURI", insecureContext: true },
  { name: "encodeURIComponent", insecureContext: true },
  { name: "escape", insecureContext: true },
  { name: "eval", insecureContext: true },
  { name: "globalThis", insecureContext: true },
  { name: "isFinite", insecureContext: true },
  { name: "isNaN", insecureContext: true },
  { name: "parseFloat", insecureContext: true },
  { name: "parseInt", insecureContext: true },
  { name: "undefined", insecureContext: true },
  { name: "unescape", insecureContext: true },
];
// IMPORTANT: Do not change the list above without review from
//            a JavaScript Engine peer!

// IMPORTANT: Do not change the list below without review from a DOM peer!
// (You can request review on Phabricator via r=#webidl)
let interfaceNamesInGlobalScope = [
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AbortController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AbortSignal", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AbstractRange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AnalyserNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Animation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AnimationEffect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AnimationEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AnimationPlaybackEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AnimationTimeline", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Attr", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Audio", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioBuffer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioData", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioDecoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioBufferSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioDestinationNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioEncoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioListener", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioParam", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioParamMap", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioProcessingEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioScheduledSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioWorklet", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioWorkletNode", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AuthenticatorAssertionResponse" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AuthenticatorAttestationResponse" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AuthenticatorResponse" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BarProp", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BaseAudioContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BeforeUnloadEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BiquadFilterNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Blob", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BlobEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BroadcastChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ByteLengthQueuingStrategy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Cache",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CacheStorage",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasCaptureMediaStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasGradient", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasPattern", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasRenderingContext2D", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CaretPosition", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CDATASection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ChannelMergerNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ChannelSplitterNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CharacterData", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Clipboard" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ClipboardEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ClipboardItem" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CloseEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Comment", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CompositionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CompressionStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ConstantSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ContentVisibilityAutoStateChangeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ConvolverNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CountQueuingStrategy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CookieChangeEvent", nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CookieStore", nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Credential" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CredentialsContainer" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Crypto", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CryptoKey" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSS", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSS2Properties", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSAnimation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSConditionRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSContainerRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSCounterStyleRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSFontFaceRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSFontFeatureValuesRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSFontPaletteValuesRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSGroupingRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSImportRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSKeyframeRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSKeyframesRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSLayerBlockRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSLayerStatementRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMediaRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMozDocumentRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSNamespaceRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSNestedDeclarations", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSPageDescriptors", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSPageRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSPropertyRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSPseudoElement", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSRuleList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSScopeRule", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSStartingStyleRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSStyleDeclaration", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSStyleRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSStyleSheet", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSSupportsRule", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSTransition", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CustomElementRegistry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CustomEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CustomStateSet", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DecompressionStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DataTransfer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DataTransferItem", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DataTransferItemList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DelayNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DeviceLightEvent", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DeviceMotionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DeviceOrientationEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DeviceProximityEvent", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Directory", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Document", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DocumentFragment", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DocumentTimeline", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DocumentType", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMException", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMImplementation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMMatrix", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMMatrixReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMParser", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMPoint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMPointReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMQuad", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMRect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMRectList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMRectReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMStringList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMStringMap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMTokenList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DragEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DynamicsCompressorNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Element", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ElementInternals", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EncodedAudioChunk", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EncodedVideoChunk", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ErrorEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Event", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EventCounts", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EventSource", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EventTarget", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "File", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileReader", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystem", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemDirectoryEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemDirectoryHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemDirectoryReader", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemFileEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemFileHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemWritableFileStream" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FocusEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FormData", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FormDataEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFace", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFaceSet", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFaceSetLoadEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FragmentDirective", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GainNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Gamepad", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadAxisMoveEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadButtonEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadButton", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadHapticActuator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadLightIndicator", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadPose", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GamepadTouch", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Geolocation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GeolocationCoordinates", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GeolocationPosition", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GeolocationPositionError", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPU", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapter", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapterInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroup", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroupLayout", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBuffer", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBufferUsage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCanvasContext", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUColorWrite", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandBuffer", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationMessage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePassEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePipeline", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDevice", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDeviceLostInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUExternalTexture", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUInternalError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUMapMode", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUOutOfMemoryError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineLayout", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQuerySet", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQueue", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundle", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundleEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPassEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPipeline", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSampler", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderModule", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderStage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedFeatures", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedLimits", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTexture", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureUsage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureView", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUUncapturedErrorEvent", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUValidationError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HashChangeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Headers", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "History", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Highlight", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HighlightRegistry", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLAllCollection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLAnchorElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLAreaElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLAudioElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLBaseElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLBodyElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLBRElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLButtonElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLCanvasElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLCollection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDataElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDataListElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDetailsElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDialogElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDirectoryElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDivElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDListElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLDocument", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLEmbedElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFieldSetElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFontElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFormControlsCollection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFormElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFrameElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLFrameSetElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLHeadElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLHeadingElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLHRElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLHtmlElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLIFrameElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLImageElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLInputElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLLabelElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLLegendElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLLIElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLLinkElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMapElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMarqueeElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMediaElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMenuElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMetaElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLMeterElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLModElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLObjectElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLOListElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLOptGroupElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLOptionElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLOptionsCollection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLOutputElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLParagraphElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLParamElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLPreElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLPictureElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLProgressElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLQuoteElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLScriptElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLSelectElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLSlotElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLSourceElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLSpanElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLStyleElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableCaptionElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableCellElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableColElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableRowElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTableSectionElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTemplateElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTextAreaElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTimeElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTitleElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLTrackElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLUListElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLUnknownElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "HTMLVideoElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IdleDeadline", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBCursor", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBCursorWithValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBDatabase", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBFactory", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBIndex", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBKeyRange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBObjectStore", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBOpenDBRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBTransaction", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IDBVersionChangeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IdentityCredential", nightly: true, desktop: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IIRFilterNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Image", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageBitmap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageBitmapRenderingContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageCapture", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageCaptureErrorEvent", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageData", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageDecoder" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageTrack" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageTrackList" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "InputEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "InstallTrigger", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IntersectionObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "IntersectionObserverEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "KeyEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "KeyboardEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "KeyframeEffect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "LargestContentfulPaint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Location", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Lock",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "LockManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MathMLElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaCapabilities", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaDeviceInfo", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaDevices", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaElementAudioSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaError", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeyError", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaEncryptedEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeys", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeySession", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeySystemAccess", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeyMessageEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaKeyStatusMap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaMetadata", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaQueryList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaQueryListEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaRecorder", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaRecorderErrorEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaSession", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaSource", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamAudioDestinationNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamAudioSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamTrackAudioSourceNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamTrackEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MediaStreamTrack", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "MerchantValidationEvent",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessageChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessageEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessagePort", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIAccess", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIConnectionEvent", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIInputMap", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIInput", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIMessageEvent", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIOutputMap", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIOutput", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MIDIPort", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MimeType", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MimeTypeArray", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MouseEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MouseScrollEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MutationEvent", insecureContext: true, nightly: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MutationObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MutationRecord", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NamedNodeMap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NavigateEvent", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Navigation", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NavigationActivation", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "NavigationCurrentEntryChangeEvent",
    insecureContext: true,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NavigationDestination", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NavigationHistoryEntry", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NavigationTransition", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "NavigationPreloadManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Navigator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "NavigatorLogin",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NetworkInformation", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Node", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NodeFilter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NodeIterator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NodeList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Notification", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OfflineAudioCompletionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OfflineAudioContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OffscreenCanvas", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OffscreenCanvasRenderingContext2D", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Option", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OscillatorNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PageTransitionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PaintRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PaintRequestList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PannerNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Path2D", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "PaymentAddress",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "PaymentMethodChangeEvent",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "PaymentRequest",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "PaymentRequestUpdateEvent",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "PaymentResponse",
    insecureContext: false,
    desktop: true,
    nightly: true,
    linux: false,
    disabled: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Performance", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceEventTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceMark", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceMeasure", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceNavigation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceNavigationTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceObserverEntryList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformancePaintTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceResourceTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceServerTiming", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PeriodicWave", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Permissions", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PermissionStatus", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Plugin", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PluginArray", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PointerEvent", insecureContext: true, fennec: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PopStateEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PopupBlockedEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ProcessingInstruction", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ProgressEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PromiseRejectionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PublicKeyCredential" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscription",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscriptionOptions",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RadioNodeList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Range", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableByteStreamController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableStreamBYOBReader", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableStreamBYOBRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableStreamDefaultController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReadableStreamDefaultReader", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Request", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ResizeObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ResizeObserverEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ResizeObserverSize", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Response", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCCertificate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDataChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDataChannelEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDtlsTransport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDTMFSender", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDTMFToneChangeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedAudioFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedVideoFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCIceCandidate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCIceTransport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCPeerConnection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCPeerConnectionIceEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpReceiver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpScriptTransform", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpSender", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpTransceiver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCSctpTransport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCSessionDescription", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCStatsReport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCTrackEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Sanitizer", disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Scheduler", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Screen", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ScreenOrientation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ScriptProcessorNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ScrollAreaEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SecurityPolicyViolationEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Selection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorker",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerContainer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerRegistration",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ScopedCredential", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ScopedCredentialInfo", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ShadowRoot", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SharedWorker", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SourceBuffer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SourceBufferList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpeechSynthesisErrorEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpeechSynthesisEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpeechSynthesis", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpeechSynthesisUtterance", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpeechSynthesisVoice", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SpecialPowers", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StaticRange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StereoPannerNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Storage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StorageEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StorageManager", fennec: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StyleSheet", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StyleSheetList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SubtleCrypto" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SubmitEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAngle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedAngle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedBoolean", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedEnumeration", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedInteger", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedLength", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedLengthList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedNumber", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedNumberList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedPreserveAspectRatio", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedRect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedString", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimatedTransformList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimateElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimateMotionElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimateTransformElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGAnimationElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGCircleElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGClipPathElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGComponentTransferFunctionElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGDefsElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGDescElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGDiscardElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGEllipseElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEBlendElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEColorMatrixElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEComponentTransferElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFECompositeElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEConvolveMatrixElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEDiffuseLightingElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEDisplacementMapElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEDistantLightElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEDropShadowElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEFloodElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEFuncAElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEFuncBElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEFuncGElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEFuncRElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEGaussianBlurElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEImageElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEMergeElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEMergeNodeElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEMorphologyElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEOffsetElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFEPointLightElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFESpecularLightingElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFESpotLightElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFETileElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFETurbulenceElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGFilterElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGForeignObjectElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGGElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGGeometryElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGGradientElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGGraphicsElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGImageElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGLength", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGLengthList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGLinearGradientElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGLineElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGMarkerElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGMaskElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGMatrix", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGMetadataElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGMPathElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGNumber", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGNumberList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPathElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPatternElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPoint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPointList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPolygonElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPolylineElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGPreserveAspectRatio", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGRadialGradientElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGRect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGRectElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGScriptElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGSetElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGStopElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGStringList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGStyleElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGSVGElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGSwitchElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGSymbolElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTextContentElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTextElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTextPathElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTextPositioningElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTitleElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTransform", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTransformList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGTSpanElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGUnitTypes", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGUseElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SVGViewElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskController", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskPriorityChangeEvent", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskSignal", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Text", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextDecoder", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextDecoderStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextEncoder", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextEncoderStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "TextEvent",
    insecureContext: true,
    disabled: !SpecialPowers.getBoolPref("dom.events.textevent.enabled"),
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextMetrics", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextTrack", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextTrackCue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextTrackCueList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextTrackList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TimeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TimeRanges", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ToggleEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Touch", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TouchEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TouchList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrackEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TransformStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "TransformStreamDefaultController",
    insecureContext: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TransitionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TreeWalker", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "U2F", insecureContext: false, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "UIEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URL", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URLSearchParams", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "UserActivation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "UserProximityEvent", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ValidityState", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoColorSpace", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoDecoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoEncoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoPlaybackQuality", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ViewTransition", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VisualViewport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VTTCue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VTTRegion", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WaveShaperNode", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebAuthnAssertion", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebAuthnAttestation", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebAuthentication", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLActiveInfo", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLBuffer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLContextEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLFramebuffer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLProgram", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLQuery", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLRenderbuffer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLRenderingContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGL2RenderingContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLSampler", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLShader", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLShaderPrecisionFormat", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLSync", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLTexture", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLTransformFeedback", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLUniformLocation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGLVertexArrayObject", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebKitCSSMatrix", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebSocket", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransport", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportBidirectionalStream", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportDatagramDuplexStream", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportError", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportReceiveStream", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportSendStream", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WheelEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Window", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WGSLLanguageFeatures", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Worker", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Worklet", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStreamDefaultController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStreamDefaultWriter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLDocument", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestEventTarget", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestUpload", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLSerializer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XPathEvaluator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XPathExpression", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XPathResult", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XSLTProcessor", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "alert", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "atob", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "blur", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "btoa", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "caches", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "cancelAnimationFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "cancelIdleCallback", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "captureEvents", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "clearInterval", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "clearTimeout", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "clientInformation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "close", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "closed", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "confirm", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "console", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "cookieStore", nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "createImageBitmap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "crossOriginIsolated", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "crypto", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "customElements", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "devicePixelRatio", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "document", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "dump", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "event", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "external", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "fetch", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "find", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "focus", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "frameElement", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "frames", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "fullScreen", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "getComputedStyle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "getDefaultComputedStyle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "getSelection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "history", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "indexedDB", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "innerHeight", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "innerWidth", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "isSecureContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "length", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "localStorage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "location", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "locationbar", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "matchMedia", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "menubar", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "moveBy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "moveTo", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "mozInnerScreenX", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "mozInnerScreenY", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "name", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "navigator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "netscape", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onabort", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondeviceorientationabsolute", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onafterprint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onanimationcancel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onanimationend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onanimationiteration", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onanimationstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onauxclick", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onbeforeinput", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onbeforematch", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onbeforeprint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onbeforetoggle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onbeforeunload", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onblur", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncancel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncanplay", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncanplaythrough", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onclick", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onclose", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncontentvisibilityautostatechange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncontextlost", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncontextmenu", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncontextrestored", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncopy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncuechange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oncut", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondblclick", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondevicemotion", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondeviceorientation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondrag", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragenter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragexit", insecureContext: true, nightly: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragleave", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragover", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondragstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondrop", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ondurationchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onemptied", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onended", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onerror", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onfocus", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onformdata", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ongamepadconnected", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ongamepaddisconnected", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ongotpointercapture", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onhashchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oninput", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "oninvalid", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onkeydown", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onkeypress", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onkeyup", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onlanguagechange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onload", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onloadeddata", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onloadedmetadata", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onloadstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onlostpointercapture", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmessage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmessageerror", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmousedown", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmouseenter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmouseleave", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmousemove", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmouseout", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmouseover", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmouseup", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmozfullscreenchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmozfullscreenerror", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onoffline", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ononline", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onorientationchange", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpagehide", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpageshow", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpaste", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpause", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onplay", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onplaying", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointercancel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerdown", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerenter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerleave", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointermove", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerout", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerover", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpointerup", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onpopstate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onprogress", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onratechange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onrejectionhandled", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onreset", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onresize", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onscroll", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onscrollend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onsecuritypolicyviolation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onseeked", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onseeking", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onselect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onselectionchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onselectstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onslotchange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onstalled", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onstorage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onsubmit", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onsuspend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontimeupdate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontoggle", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontouchcancel", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontouchend", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontouchmove", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontouchstart", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontransitioncancel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontransitionend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontransitionrun", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ontransitionstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onunhandledrejection", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onunload", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onvolumechange", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwaiting", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwebkitanimationend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwebkitanimationiteration", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwebkitanimationstart", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwebkittransitionend", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onwheel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "open", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "opener", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "orientation", insecureContext: true, android: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "origin", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "originAgentCluster", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "outerHeight", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "outerWidth", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "pageXOffset", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "pageYOffset", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "parent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "performance", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "personalbar", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "postMessage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "print", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "prompt", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "queueMicrotask", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "releaseEvents", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "reportError", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "requestAnimationFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "requestIdleCallback", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "resizeBy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "resizeTo", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scheduler", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "screen", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "screenLeft", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "screenTop", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "screenX", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "screenY", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scroll", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollBy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollByLines", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollByPages", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollMaxX", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollMaxY", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollTo", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollX", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollY", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "scrollbars", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "self", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "sessionStorage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "setInterval", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "setResizable", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "setTimeout", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "speechSynthesis", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "status", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "statusbar", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "stop", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "structuredClone", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "toolbar", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "top", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "u2f", insecureContext: false, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "updateCommands", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "visualViewport", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WakeLock" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WakeLockSentinel" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "webkitURL", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "window", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
];
// IMPORTANT: Do not change the list above without review from a DOM peer!

function entryDisabled(entry) {
  return (
    entry.nightly === !isNightly ||
    (entry.nightlyAndroid === !(isAndroid && isNightly) && isAndroid) ||
    entry.desktop === !isDesktop ||
    entry.windows === !isWindows ||
    entry.mac === !isMac ||
    entry.linux === !isLinux ||
    (entry.android === !isAndroid && !entry.nightlyAndroid) ||
    entry.fennecOrDesktop === (isAndroid && !isFennec) ||
    entry.fennec === !isFennec ||
    entry.release === !isRelease ||
    entry.releaseNonWindows === !(isRelease && !isWindows) ||
    // The insecureContext test is very purposefully converting
    // entry.insecureContext to boolean, so undefined will convert to
    // false.  That way entries without an insecureContext annotation
    // will get treated as "insecureContext: false", which means exposed
    // only in secure contexts.
    (isInsecureContext && !entry.insecureContext) ||
    entry.earlyBetaOrEarlier === !isEarlyBetaOrEarlier ||
    entry.crossOriginIsolated === !isCrossOriginIsolated ||
    entry.disabled
  );
}

function createInterfaceMap(...interfaceGroups) {
  var interfaceMap = {};

  function addInterfaces(interfaces) {
    for (var entry of interfaces) {
      if (typeof entry === "string") {
        ok(!(entry in interfaceMap), "duplicate entry for " + entry);
        interfaceMap[entry] = !isInsecureContext;
      } else {
        ok(!(entry.name in interfaceMap), "duplicate entry for " + entry.name);
        ok(!("pref" in entry), "Bogus pref annotation for " + entry.name);
        interfaceMap[entry.name] = !entryDisabled(entry);
      }
    }
  }

  for (let interfaceGroup of interfaceGroups) {
    addInterfaces(interfaceGroup);
  }

  return interfaceMap;
}

function runTest(parentName, parent, ...interfaceGroups) {
  var interfaceMap = createInterfaceMap(...interfaceGroups);
  for (var name of Object.getOwnPropertyNames(parent)) {
    ok(
      interfaceMap[name],
      "If this is failing: DANGER, are you sure you want to expose the new interface " +
        name +
        " to all webpages as a property on '" +
        parentName +
        "'? Do not make a change to this file without a " +
        " review from a DOM peer for that specific change!!! (or a JS peer for changes to ecmaGlobals)"
    );

    ok(
      name in parent,
      `${name} is exposed as an own property on '${parentName}' but tests false for "in" in the global scope`
    );
    ok(
      Object.getOwnPropertyDescriptor(parent, name),
      `${name} is exposed as an own property on '${parentName}' but has no property descriptor in the global scope`
    );

    delete interfaceMap[name];
  }
  for (var name of Object.keys(interfaceMap)) {
    ok(
      name in parent === interfaceMap[name],
      name +
        " should " +
        (interfaceMap[name] ? "" : " NOT") +
        " be defined on '" +
        parentName +
        "' scope"
    );
    if (!interfaceMap[name]) {
      delete interfaceMap[name];
    }
  }
  is(
    Object.keys(interfaceMap).length,
    0,
    "The following interface(s) are not enumerated: " +
      Object.keys(interfaceMap).join(", ")
  );
}

// Use an iframe because the test harness pollutes the global object with a lot
// of functions.
let iframeWindow = document.getElementById("testframe").contentWindow;
is(
  window.isSecureContext,
  iframeWindow.isSecureContext,
  "iframe isSecureContext must match"
);
runTest("window", iframeWindow, ecmaGlobals, interfaceNamesInGlobalScope);

if (window.WebAssembly && !entryDisabled(wasmGlobalEntry)) {
  runTest("WebAssembly", window.WebAssembly, wasmGlobalInterfaces);
}
