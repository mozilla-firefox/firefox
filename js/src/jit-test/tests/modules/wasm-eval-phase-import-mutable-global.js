// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A wasm module importing a mutable global from another wasm module shares the
// global itself, taken from the exporter's instance rather than its module
// environment, so writes by the exporter are visible to the importer.

import { set } from "exports-mutable-global.wasm";
import { get } from "imports-mutable-global.wasm";

assertEq(get(), 1);
set(42);
assertEq(get(), 42);
