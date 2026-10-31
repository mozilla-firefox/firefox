// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// An export name that is an array index ("0") works both as a named import
// and as a property of the namespace object.

import { "0" as zero } from "exports-index-name.wasm";
import * as ns from "exports-index-name.wasm";

assertEq(zero(), 7);
assertEq(ns[0], zero);
assertEq(JSON.stringify(Object.keys(ns)), '["0"]');
