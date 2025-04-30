/* eslint-disable mozilla/no-comparison-or-assignment-inside-ok */

// This is a list of all interfaces that are exposed to workers.
// Please only add things to this list with great care and proper review
// from the associated module peers.

// This file lists global interfaces we want exposed and verifies they
// are what we intend. Each entry in the arrays below can either be a
// simple string with the interface name, or an object with a 'name'
// property giving the interface name as a string, and additional
// properties which qualify the exposure of that interface. For example:
//
// [
//   "AGlobalInterface", // secure context only
//   { name: "ExperimentalThing", release: false },
//   { name: "ReallyExperimentalThing", nightly: true },
//   { name: "DesktopOnlyThing", desktop: true },
//   { name: "FancyControl", xbl: true },
//   { name: "DisabledEverywhere", disabled: true },
// ];
//
// See createInterfaceMap() below for a complete list of properties.
//
// The values of the properties need to be literal true/false
// (e.g. indicating whether something is enabled on a particular
// channel/OS).  If we ever end up in a situation where a propert
// value needs to depend on channel or OS, we will need to make sure
// we have that information before setting up the property lists.

// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let wasmGlobalEntry = {
  name: "WebAssembly",
  insecureContext: true,
  disabled: !getJSTestingFunctions().wasmIsSupportedByHardware(),
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
  { name: "MediaCapabilities", insecureContext: true },
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
let interfaceNamesInGlobalScope = [
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AbortController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AbortSignal", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioData", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioDecoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "AudioEncoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Blob", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "BroadcastChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ByteLengthQueuingStrategy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Cache",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CacheStorage",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasGradient", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CanvasPattern", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CloseEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CompressionStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CountQueuingStrategy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Crypto", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CryptoKey" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CustomEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DecompressionStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DedicatedWorkerGlobalScope", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Directory", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMException", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMMatrix", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMMatrixReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMPoint", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMPointReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMQuad", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMRect", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMRectReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DOMStringList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EncodedAudioChunk", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "EncodedVideoChunk", insecureContext: true, nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ErrorEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Event", insecureContext: true },
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
  { name: "FileReaderSync", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemDirectoryHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemFileHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemSyncAccessHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemWritableFileStream" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFace", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFaceSet", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FontFaceSetLoadEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FormData", insecureContext: true },
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
  { name: "Headers", insecureContext: true },
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
  { name: "ImageBitmap", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageBitmapRenderingContext", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageData", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageDecoder" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageTrack" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ImageTrackList" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Lock",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "LockManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessageChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessageEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "MessagePort", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "NavigationPreloadManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NetworkInformation", insecureContext: true, disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Notification", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OffscreenCanvas", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "OffscreenCanvasRenderingContext2D", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Path2D", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Performance", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceEntry", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceMark", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceMeasure", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceObserverEntryList", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceResourceTiming", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PerformanceServerTiming", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Permissions", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PermissionStatus", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ProgressEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "PromiseRejectionEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscription",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscriptionOptions",
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
  { name: "Response", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedAudioFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedVideoFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpScriptTransformer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCTransformEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Scheduler", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorker",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerContainer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerRegistration",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StorageManager", fennec: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SubtleCrypto" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskController", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskPriorityChangeEvent", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskSignal", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextDecoder", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextDecoderStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextEncoder", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextEncoderStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TextMetrics", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TransformStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  {
    name: "TransformStreamDefaultController",
    insecureContext: true,
  },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestEventTarget", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestUpload", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URL", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URLSearchParams", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoColorSpace", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoDecoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoEncoder", nightlyAndroid: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "VideoFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebGL2RenderingContext", insecureContext: true },
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
  { name: "WGSLLanguageFeatures", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Worker", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WorkerGlobalScope", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WorkerLocation", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WorkerNavigator", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStreamDefaultController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WritableStreamDefaultWriter", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "cancelAnimationFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "close", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "console", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "name", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmessage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onmessageerror", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "onrtctransform", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "postMessage", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "requestAnimationFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
];
// IMPORTANT: Do not change the list above without review from a DOM peer!

// List of functions defined on the global by the test harness or this test
// file.
let testFunctions = [
  "ok",
  "is",
  "workerTestArrayEquals",
  "workerTestDone",
  "workerTestGetPermissions",
  "workerTestGetHelperData",
  "entryDisabled",
  "createInterfaceMap",
  "runTest",
];

function entryDisabled(
  entry,
  {
    isNightly,
    isEarlyBetaOrEarlier,
    isRelease,
    isDesktop,
    isAndroid,
    isInsecureContext,
    isFennec,
    isCrossOringinIsolated,
  }
) {
  return (
    entry.nightly === !isNightly ||
    (entry.nightlyAndroid === !(isAndroid && isNightly) && isAndroid) ||
    entry.desktop === !isDesktop ||
    (entry.android === !isAndroid && !entry.nightlyAndroid) ||
    entry.fennecOrDesktop === (isAndroid && !isFennec) ||
    entry.fennec === !isFennec ||
    entry.release === !isRelease ||
    // The insecureContext test is very purposefully converting
    // entry.insecureContext to boolean, so undefined will convert to
    // false.  That way entries without an insecureContext annotation
    // will get treated as "insecureContext: false", which means exposed
    // only in secure contexts.
    (isInsecureContext && !entry.insecureContext) ||
    entry.earlyBetaOrEarlier === !isEarlyBetaOrEarlier ||
    entry.crossOriginIsolated === !isCrossOringinIsolated ||
    entry.disabled
  );
}

function createInterfaceMap(data, ...interfaceGroups) {
  var interfaceMap = {};

  function addInterfaces(interfaces) {
    for (var entry of interfaces) {
      if (typeof entry === "string") {
        ok(!(entry in interfaceMap), "duplicate entry for " + entry);
        interfaceMap[entry] = !data.isInsecureContext;
      } else {
        ok(!(entry.name in interfaceMap), "duplicate entry for " + entry.name);
        ok(!("pref" in entry), "Bogus pref annotation for " + entry.name);
        interfaceMap[entry.name] = !entryDisabled(entry, data);
      }
    }
  }

  for (let interfaceGroup of interfaceGroups) {
    addInterfaces(interfaceGroup);
  }

  return interfaceMap;
}

function runTest(parentName, parent, data, ...interfaceGroups) {
  var interfaceMap = createInterfaceMap(data, ...interfaceGroups);
  for (var name of Object.getOwnPropertyNames(parent)) {
    // Ignore functions on the global that are part of the test (harness).
    if (parent === self && testFunctions.includes(name)) {
      continue;
    }
    ok(
      interfaceMap[name],
      "If this is failing: DANGER, are you sure you want to expose the new interface " +
        name +
        " to all webpages as a property of " +
        parentName +
        "? Do not make a change to this file without a " +
        " review from a DOM peer for that specific change!!! (or a JS peer for changes to ecmaGlobals)"
    );
    delete interfaceMap[name];
  }
  for (var name of Object.keys(interfaceMap)) {
    ok(
      name in parent === interfaceMap[name],
      name +
        " should " +
        (interfaceMap[name] ? "" : " NOT") +
        " be defined on " +
        parentName
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

workerTestGetHelperData(function (data) {
  runTest("self", self, data, ecmaGlobals, interfaceNamesInGlobalScope);
  if (WebAssembly && !entryDisabled(wasmGlobalEntry, data)) {
    runTest("WebAssembly", WebAssembly, data, wasmGlobalInterfaces);
  }
  workerTestDone();
});
