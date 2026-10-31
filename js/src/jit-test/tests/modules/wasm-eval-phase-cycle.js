// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// In a cycle where the JS module executes first, its early read of a wasm
// export throws a ReferenceError, since wasm export bindings are
// uninitialized until ExecuteModule. Afterwards, both modules can read each
// other's exports.

import * as wasm from "cycle-wasm.wasm";
import { early, readWasmGlob } from "cycle-js.js";

assertEq(early, "throw:ReferenceError");
assertEq(wasm.wasmGlob, 24);
assertEq(readWasmGlob(), 24);
assertEq(wasm.readJsGlob(), 42);
