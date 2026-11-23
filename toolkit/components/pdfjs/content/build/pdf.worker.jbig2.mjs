/**
 * @licstart The following is the entire license notice for the
 * JavaScript code in this page
 *
 * Copyright 2024 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * @licend The above is the entire license notice for the
 * JavaScript code in this page
 */

/**
 * pdfjsVersion = 7.0.213
 * pdfjsBuild = 541d197e6
 */
export const __webpack_esm_ids__ = ["jbig2"];
export const __webpack_esm_modules__ = {

/***/ 2708
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

__webpack_require__.r(__webpack_exports__);
async function JBig2(moduleArg = {}) {
  var Module = moduleArg;
  var ENVIRONMENT_IS_WEB = true;
  var ENVIRONMENT_IS_WORKER = false;
  var programArgs = [];
  var thisProgram = "./this.program";
  var quit_ = (status, toThrow) => {
    throw toThrow;
  };
  var _scriptName = import.meta.url;
  var scriptDirectory = "";
  var readAsync, readBinary;
  if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
    try {
      scriptDirectory = new URL(".", _scriptName).href;
    } catch {}
    readAsync = async url => {
      var response = await fetch(url, {
        credentials: "same-origin"
      });
      if (response.ok) {
        return response.arrayBuffer();
      }
      throw new Error(response.status + " : " + response.url);
    };
  } else {}
  var out = console.log.bind(console);
  var err = console.error.bind(console);
  var wasmBinary;
  var ABORT = false;
  var EXITSTATUS;
  class EmscriptenEH {}
  class EmscriptenSjLj extends EmscriptenEH {}
  var runtimeInitialized = false;
  function getMemoryBuffer() {
    return wasmMemory.buffer;
  }
  function updateMemoryViews() {
    if (HEAP8?.buffer?.resizable) return;
    var b = getMemoryBuffer();
    HEAP8 = new Int8Array(b);
    HEAPU8 = new Uint8Array(b);
  }
  function preRun() {
    var preRun = Module["preRun"];
    if (preRun) {
      if (typeof preRun == "function") preRun = [preRun];
      onPreRuns.push(...preRun);
    }
    callRuntimeCallbacks(onPreRuns);
  }
  function initRuntime() {
    runtimeInitialized = true;
    wasmExports["j"]();
  }
  function postRun() {
    var postRun = Module["postRun"];
    if (postRun) {
      if (typeof postRun == "function") postRun = [postRun];
      onPostRuns.push(...postRun);
    }
    callRuntimeCallbacks(onPostRuns);
  }
  function abort(what) {
    Module["onAbort"]?.(what);
    what = `Aborted(${what})`;
    err(what);
    ABORT = true;
    what += ". Build with -sASSERTIONS for more info.";
    var e = new WebAssembly.RuntimeError(what);
    throw e;
  }
  var wasmBinaryFile;
  function getWasmImports() {
    var imports = {
      a: wasmImports
    };
    return imports;
  }
  async function createWasm() {
    function receiveInstance(instance) {
      wasmExports = instance.exports;
      assignWasmExports(wasmExports);
      updateMemoryViews();
      return wasmExports;
    }
    var info = getWasmImports();
    var instantiateWasm = Module["instantiateWasm"];
    return new Promise(resolve => {
      instantiateWasm(info, inst => resolve(receiveInstance(inst)));
    });
  }
  class ExitStatus {
    name = "ExitStatus";
    constructor(status) {
      this.message = `Program terminated with exit(${status})`;
      this.status = status;
    }
  }
  var HEAP8;
  var callRuntimeCallbacks = callbacks => {
    while (callbacks.length > 0) {
      callbacks.shift()(Module);
    }
  };
  var onPostRuns = [];
  var onPreRuns = [];
  var noExitRuntime = true;
  var __abort_js = () => abort("");
  var runtimeKeepaliveCounter = 0;
  var __emscripten_runtime_keepalive_clear = () => {
    noExitRuntime = false;
    runtimeKeepaliveCounter = 0;
  };
  var timers = {};
  var handleException = e => {
    if (e instanceof ExitStatus || e == "unwind") {
      return EXITSTATUS;
    }
    quit_(1, e);
  };
  var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;
  var _proc_exit = code => {
    EXITSTATUS = code;
    if (!keepRuntimeAlive()) {
      Module["onExit"]?.(code);
      ABORT = true;
    }
    quit_(code, new ExitStatus(code));
  };
  var exitJS = (status, implicit) => {
    EXITSTATUS = status;
    _proc_exit(status);
  };
  var _exit = exitJS;
  var maybeExit = () => {
    if (!keepRuntimeAlive()) {
      try {
        _exit(EXITSTATUS);
      } catch (e) {
        handleException(e);
      }
    }
  };
  var callUserCallback = func => {
    if (ABORT) {
      return;
    }
    try {
      return func();
    } catch (e) {
      handleException(e);
    } finally {
      maybeExit();
    }
  };
  var _emscripten_get_now = () => performance.now();
  var __setitimer_js = (which, timeout_ms) => {
    if (timers[which]) {
      clearTimeout(timers[which].id);
      delete timers[which];
    }
    if (!timeout_ms) return 0;
    var id = setTimeout(() => {
      delete timers[which];
      callUserCallback(() => __emscripten_timeout(which, _emscripten_get_now()));
    }, timeout_ms);
    timers[which] = {
      id,
      timeout_ms
    };
    return 0;
  };
  function _createImageData(size) {
    Module.imageData = new Uint8Array(size);
  }
  var getHeapMax = () => 2147483648;
  var alignMemory = (size, alignment) => Math.ceil(size / alignment) * alignment;
  var growMemory = size => {
    var oldHeapSize = wasmMemory.buffer.byteLength;
    var pages = (size - oldHeapSize + 65535) / 65536 | 0;
    try {
      wasmMemory.grow(pages);
      updateMemoryViews();
      return 1;
    } catch (e) {}
  };
  var HEAPU8;
  var _emscripten_resize_heap = requestedSize => {
    var oldSize = HEAPU8.length;
    requestedSize >>>= 0;
    var maxHeapSize = getHeapMax();
    if (requestedSize > maxHeapSize) {
      return false;
    }
    for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
      var overGrownHeapSize = oldSize * (1 + .2 / cutDown);
      overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296);
      var newSize = Math.min(maxHeapSize, alignMemory(Math.max(requestedSize, overGrownHeapSize), 65536));
      var replacement = growMemory(newSize);
      if (replacement) {
        return true;
      }
    }
    return false;
  };
  function _setImageData(array_ptr, pitch8, pitch32, height) {
    if (pitch32 === pitch8) {
      Module.imageData = new Uint8ClampedArray(HEAPU8.subarray(array_ptr, array_ptr + pitch32 * height));
      return;
    }
    const destSize = pitch8 * height;
    const imageData = Module.imageData = new Uint8ClampedArray(destSize);
    for (let srcStart = array_ptr, destStart = 0; destStart < destSize; srcStart += pitch32, destStart += pitch8) {
      imageData.set(HEAPU8.subarray(srcStart, srcStart + pitch8), destStart);
    }
  }
  function _setLineData(line_ptr, pitch8, offset) {
    Module.imageData.set(HEAPU8.subarray(line_ptr, line_ptr + pitch8), offset);
  }
  var writeArrayToMemory = (array, buffer) => {
    HEAP8.set(array, buffer);
  };
  if (Module["noExitRuntime"]) noExitRuntime = Module["noExitRuntime"];
  if (Module["print"]) out = Module["print"];
  if (Module["printErr"]) err = Module["printErr"];
  if (Module["arguments"]) programArgs = Module["arguments"];
  if (Module["thisProgram"]) thisProgram = Module["thisProgram"];
  var preInit = Module["preInit"];
  if (preInit) {
    if (typeof preInit == "function") Module["preInit"] = preInit = [preInit];
    while (preInit.length > 0) {
      preInit.shift()();
    }
  }
  Module["writeArrayToMemory"] = writeArrayToMemory;
  var _malloc, _free, _jbig2_decode, _ccitt_decode, __emscripten_timeout, memory, __indirect_function_table, wasmMemory;
  function assignWasmExports(wasmExports) {
    _malloc = Module["_malloc"] = wasmExports["k"];
    _free = Module["_free"] = wasmExports["l"];
    _jbig2_decode = Module["_jbig2_decode"] = wasmExports["m"];
    _ccitt_decode = Module["_ccitt_decode"] = wasmExports["n"];
    __emscripten_timeout = wasmExports["o"];
    memory = wasmMemory = wasmExports["i"];
    __indirect_function_table = wasmExports["__indirect_function_table"];
  }
  var wasmImports = {
    e: __abort_js,
    d: __emscripten_runtime_keepalive_clear,
    a: __setitimer_js,
    g: _createImageData,
    b: _emscripten_resize_heap,
    c: _proc_exit,
    h: _setImageData,
    f: _setLineData
  };
  async function run() {
    preRun();
    var setStatus = Module["setStatus"];
    if (setStatus) {
      setStatus("Running...");
      await new Promise(resolve => setTimeout(resolve, 1));
      setTimeout(setStatus, 1, "");
    }
    if (ABORT) return;
    initRuntime();
    Module["onRuntimeInitialized"]?.();
    postRun();
  }
  var wasmExports;
  wasmExports = await createWasm();
  await run();
  return Module;
}
/* harmony default export */ const __WEBPACK_DEFAULT_EXPORT__ = (JBig2);
/* harmony export */ __webpack_require__.d(__webpack_exports__, [
/* harmony export */   "default", 0, __WEBPACK_DEFAULT_EXPORT__
/* harmony export */ ]);


/***/ }

};
