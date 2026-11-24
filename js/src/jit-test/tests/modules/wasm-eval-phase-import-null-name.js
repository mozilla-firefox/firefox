// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A wasm module can import an export whose name contains an embedded null character.

import { call, call2 } from "imports-null-name.wasm";
import { "a\0b" as f, a, "\u0100\0b" as f2, "\u0100" as a2 } from "exports-null-name.wasm";

assertEq(call(), 42);
assertEq(f(), 42);
assertEq(a, 1);
assertEq(call2(), 43);
assertEq(f2(), 43);
assertEq(a2, 1);
