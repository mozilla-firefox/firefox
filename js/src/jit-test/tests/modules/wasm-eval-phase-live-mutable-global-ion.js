// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration; --fast-warmup; --no-threads

// Wasm export bindings are non-writable, so Warp must not bake in a mutable
// global's binding as a constant import.

import { g, set } from "exports-mutable-global.wasm";

function readG() {
  return g;
}

function readGInlined() {
  return readG() + 1;
}

for (let i = 0; i < 200; i++) {
  if (i === 100) {
    set(2);
  }
  const expected = i < 100 ? 1 : 2;
  assertEq(readG(), expected);
  assertEq(readGInlined(), expected + 1);
}
