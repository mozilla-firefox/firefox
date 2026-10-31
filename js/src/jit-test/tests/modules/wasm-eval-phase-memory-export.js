// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A memory exported by a wasm module is bound to a WebAssembly.Memory in JS,
// and writes made by the wasm module are visible through its buffer.

import { mem, store } from "exports-memory.wasm";

assertEq(mem instanceof WebAssembly.Memory, true);

store(0, 0x12345678);
assertEq(new Int32Array(mem.buffer)[0], 0x12345678);
