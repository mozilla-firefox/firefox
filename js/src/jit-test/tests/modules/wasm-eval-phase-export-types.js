// |jit-test| module; skip-if: !wasmIsSupported() || !wasmSimdEnabled() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// Exported globals are bound to their values converted to JS (ToJSValue).
// Globals of types that cannot be exposed to JS, such as v128 and exnref, are
// left uninitialized, so reading them throws a ReferenceError.

import { g0, g1, g2, g3, g4, g5, g6, g7, g8, g9, f10, f11 } from "exports-many.wasm";
import { e as exnrefGlobal } from "exports-exnref.wasm";

assertEq(g0, 0);
assertEq(g1, 1);
assertEq(g2, 2n);
assertEq(g3, 3);
assertEq(g4, 4);
assertEq(g5, 5);
assertEq(g6, 6n);
assertEq(g7, null);
assertEq(f10(), 10);
assertEq(f11(), 11);

for (const tdzExport of [() => g8, () => g9, () => exnrefGlobal]) {
  let caught = null;
  try {
    tdzExport();
  } catch (e) {
    caught = e;
  }
  assertEq(caught instanceof ReferenceError, true);
}
