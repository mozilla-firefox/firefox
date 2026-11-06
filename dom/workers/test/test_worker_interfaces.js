/* eslint-disable mozilla/no-comparison-or-assignment-inside-ok */

importScripts("../../tests/mochitest/general/interface_exposure_checker.js");

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
//   { name: "DesktopOnlyThing", desktop: true },
//   { name: "DisabledEverywhere", disabled: true },
//   { name: "ExperimentalThing", release: false },
//   { name: "ReallyExperimentalThing", nightly: true },
// ];
//
// Note that the items are alphabetically sorted. This is a requirement.
// See createInterfaceMap() in interface_exposure_checker.js for a complete
// list of properties.
//
// The values of the properties need to be literal true/false
// (e.g. indicating whether something is enabled on a particular
// channel/OS).  If we ever end up in a situation where a property
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
  { name: "CompileError", insecureContext: true },
  { name: "Exception", insecureContext: true },
  { name: "Function", insecureContext: true, nightly: true },
  { name: "Global", insecureContext: true },
  { name: "Instance", insecureContext: true },
  { name: "JSTag", insecureContext: true },
  { name: "LinkError", insecureContext: true },
  { name: "Memory", insecureContext: true },
  { name: "Module", insecureContext: true },
  { name: "RuntimeError", insecureContext: true },
  { name: "SuspendError", insecureContext: true },
  { name: "Suspending", insecureContext: true },
  { name: "Table", insecureContext: true },
  { name: "Tag", insecureContext: true },
  { name: "compile", insecureContext: true },
  { name: "compileStreaming", insecureContext: true },
  { name: "instantiate", insecureContext: true },
  { name: "instantiateStreaming", insecureContext: true },
  { name: "promising", insecureContext: true },
  { name: "validate", insecureContext: true },
];
// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let ecmaGlobals = [
  { name: "AggregateError", insecureContext: true },
  { name: "Array", insecureContext: true },
  { name: "ArrayBuffer", insecureContext: true },
  { name: "AsyncDisposableStack", insecureContext: true },
  { name: "Atomics", insecureContext: true },
  { name: "BigInt", insecureContext: true },
  { name: "BigInt64Array", insecureContext: true },
  { name: "BigUint64Array", insecureContext: true },
  { name: "Boolean", insecureContext: true },
  { name: "DataView", insecureContext: true },
  { name: "Date", insecureContext: true },
  { name: "DisposableStack", insecureContext: true },
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
  { name: "SuppressedError", insecureContext: true },
  { name: "Symbol", insecureContext: true },
  { name: "SyntaxError", insecureContext: true },
  { name: "Temporal", insecureContext: true },
  { name: "TypeError", insecureContext: true },
  { name: "URIError", insecureContext: true },
  { name: "Uint16Array", insecureContext: true },
  { name: "Uint32Array", insecureContext: true },
  { name: "Uint8Array", insecureContext: true },
  { name: "Uint8ClampedArray", insecureContext: true },
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
  { name: "CSS", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSImageValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSKeywordValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathClamp", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathInvert", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathMax", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathMin", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathNegate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathProduct", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathSum", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMathValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSMatrixComponent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSNumericArray", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSNumericValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSPerspective", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSRotate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSScale", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSSkew", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSSkewX", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSSkewY", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSStyleValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSTransformComponent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSTransformValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSTranslate", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSUnitValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSUnparsedValue", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "CSSVariableReferenceValue", insecureContext: true },
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
  { name: "DecompressionStream", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "DedicatedWorkerGlobalScope", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Directory", insecureContext: true },
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
  { name: "GPU", nightly: true },
  { name: "GPU", windows: true },
  { name: "GPU", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapter", nightly: true },
  { name: "GPUAdapter", windows: true },
  { name: "GPUAdapter", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapterInfo", nightly: true },
  { name: "GPUAdapterInfo", windows: true },
  { name: "GPUAdapterInfo", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroup", nightly: true },
  { name: "GPUBindGroup", windows: true },
  { name: "GPUBindGroup", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroupLayout", nightly: true },
  { name: "GPUBindGroupLayout", windows: true },
  { name: "GPUBindGroupLayout", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBuffer", nightly: true },
  { name: "GPUBuffer", windows: true },
  { name: "GPUBuffer", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBufferUsage", nightly: true },
  { name: "GPUBufferUsage", windows: true },
  { name: "GPUBufferUsage", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCanvasContext", nightly: true },
  { name: "GPUCanvasContext", windows: true },
  { name: "GPUCanvasContext", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUColorWrite", nightly: true },
  { name: "GPUColorWrite", windows: true },
  { name: "GPUColorWrite", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandBuffer", nightly: true },
  { name: "GPUCommandBuffer", windows: true },
  { name: "GPUCommandBuffer", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandEncoder", nightly: true },
  { name: "GPUCommandEncoder", windows: true },
  { name: "GPUCommandEncoder", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationInfo", nightly: true },
  { name: "GPUCompilationInfo", windows: true },
  { name: "GPUCompilationInfo", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationMessage", nightly: true },
  { name: "GPUCompilationMessage", windows: true },
  { name: "GPUCompilationMessage", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePassEncoder", nightly: true },
  { name: "GPUComputePassEncoder", windows: true },
  { name: "GPUComputePassEncoder", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePipeline", nightly: true },
  { name: "GPUComputePipeline", windows: true },
  { name: "GPUComputePipeline", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDevice", nightly: true },
  { name: "GPUDevice", windows: true },
  { name: "GPUDevice", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDeviceLostInfo", nightly: true },
  { name: "GPUDeviceLostInfo", windows: true },
  { name: "GPUDeviceLostInfo", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUError", nightly: true },
  { name: "GPUError", windows: true },
  { name: "GPUError", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUExternalTexture", nightly: true },
  { name: "GPUExternalTexture", windows: true },
  { name: "GPUExternalTexture", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUInternalError", nightly: true },
  { name: "GPUInternalError", windows: true },
  { name: "GPUInternalError", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUMapMode", nightly: true },
  { name: "GPUMapMode", windows: true },
  { name: "GPUMapMode", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUOutOfMemoryError", nightly: true },
  { name: "GPUOutOfMemoryError", windows: true },
  { name: "GPUOutOfMemoryError", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineError", nightly: true },
  { name: "GPUPipelineError", windows: true },
  { name: "GPUPipelineError", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineLayout", nightly: true },
  { name: "GPUPipelineLayout", windows: true },
  { name: "GPUPipelineLayout", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQuerySet", nightly: true },
  { name: "GPUQuerySet", windows: true },
  { name: "GPUQuerySet", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQueue", nightly: true },
  { name: "GPUQueue", windows: true },
  { name: "GPUQueue", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundle", nightly: true },
  { name: "GPURenderBundle", windows: true },
  { name: "GPURenderBundle", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundleEncoder", nightly: true },
  { name: "GPURenderBundleEncoder", windows: true },
  { name: "GPURenderBundleEncoder", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPassEncoder", nightly: true },
  { name: "GPURenderPassEncoder", windows: true },
  { name: "GPURenderPassEncoder", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPipeline", nightly: true },
  { name: "GPURenderPipeline", windows: true },
  { name: "GPURenderPipeline", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSampler", nightly: true },
  { name: "GPUSampler", windows: true },
  { name: "GPUSampler", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderModule", nightly: true },
  { name: "GPUShaderModule", windows: true },
  { name: "GPUShaderModule", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderStage", nightly: true },
  { name: "GPUShaderStage", windows: true },
  { name: "GPUShaderStage", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedFeatures", nightly: true },
  { name: "GPUSupportedFeatures", windows: true },
  { name: "GPUSupportedFeatures", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedLimits", nightly: true },
  { name: "GPUSupportedLimits", windows: true },
  { name: "GPUSupportedLimits", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTexture", nightly: true },
  { name: "GPUTexture", windows: true },
  { name: "GPUTexture", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureUsage", nightly: true },
  { name: "GPUTextureUsage", windows: true },
  { name: "GPUTextureUsage", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureView", nightly: true },
  { name: "GPUTextureView", windows: true },
  { name: "GPUTextureView", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUUncapturedErrorEvent", nightly: true },
  { name: "GPUUncapturedErrorEvent", windows: true },
  { name: "GPUUncapturedErrorEvent", mac: true, aarch64: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUValidationError", nightly: true },
  { name: "GPUValidationError", windows: true },
  { name: "GPUValidationError", mac: true, aarch64: true },
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
  { name: "IDBRecord", insecureContext: true },
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
  { name: "MediaCapabilities", insecureContext: true },
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
  { name: "PermissionStatus", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Permissions", insecureContext: true },
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
  { name: "QuotaExceededError", insecureContext: true, nightly: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCDataChannel", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedAudioFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCEncodedVideoFrame", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCError", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCErrorEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCRtpScriptTransformer", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "RTCTransformEvent", insecureContext: true },
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
  { name: "ReportBody", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "ReportingObserver", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Request", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Response", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Scheduler", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "Serial", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SerialPort", android: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorker",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerContainer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerRegistration",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StorageManager", fennec: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StylePropertyMapReadOnly", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "SubtleCrypto" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskPriorityChangeEvent", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TaskSignal", insecureContext: true },
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
  { name: "TransformStreamDefaultController", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrustedHTML", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrustedScript", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrustedScriptURL", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrustedTypePolicy", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "TrustedTypePolicyFactory", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URL", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "URLPattern", insecureContext: true },
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
  { name: "WGSLLanguageFeatures", nightly: true },
  { name: "WGSLLanguageFeatures", windows: true },
  { name: "WGSLLanguageFeatures", mac: true, aarch64: true },
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
  { name: "WebTransportDatagramsWritable", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportError", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportReceiveStream", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportSendGroup", insecureContext: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WebTransportSendStream", insecureContext: false },
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
  { name: "XMLHttpRequest", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestEventTarget", insecureContext: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "XMLHttpRequestUpload", insecureContext: true },
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

workerTestGetHelperData(function (data) {
  runTest("self", self, {
    data,
    testFunctions,
    interfaceGroups: [ecmaGlobals, interfaceNamesInGlobalScope],
  });
  if (WebAssembly && !entryDisabled(wasmGlobalEntry, data)) {
    runTest("WebAssembly", WebAssembly, {
      data,
      interfaceGroups: [wasmGlobalInterfaces],
    });
  }
  workerTestDone();
});
