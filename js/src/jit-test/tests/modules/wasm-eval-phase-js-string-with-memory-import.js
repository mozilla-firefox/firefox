// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// Builtin (wasm:js-string) and string constant imports are provided directly,
// without a module import, alongside a memory imported from another wasm
// module.

import { store } from "exports-memory.wasm";
import { constantLength, load } from "imports-memory-and-js-string.wasm";

assertEq(constantLength(), 5);

store(0, 0x12345678);
assertEq(load(0), 0x12345678);
