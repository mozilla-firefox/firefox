// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// `export *` from a wasm module re-exports its exports except for "default",
// as with JS modules (GetExportedNames).

import * as ns from "export-star-wasm.js";
import { f } from "export-star-wasm.js";

assertEq(f(), 1);
assertEq(ns.f, f);
assertEq(JSON.stringify(Object.keys(ns)), '["f"]');
assertEq("default" in ns, false);
