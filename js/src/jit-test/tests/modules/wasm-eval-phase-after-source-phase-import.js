// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A wasm module imported in the source phase and then in the evaluation phase
// shares a single module record, so both imports see the same
// WebAssembly.Module. Instantiating that module directly creates an instance
// that is independent of the one created by the evaluation phase import.

import source mod from "exports-mutable-global.wasm";

assertEq(mod instanceof WebAssembly.Module, true);

const namespace = await import("exports-mutable-global.wasm");
assertEq(namespace[Symbol.toStringTag], "Module");
assertEq(await import.source("exports-mutable-global.wasm"), mod);

const { get } = await import("imports-mutable-global.wasm");
const instance = new WebAssembly.Instance(mod);
instance.exports.set(7);
assertEq(instance.exports.g.value, 7);
assertEq(get(), 1);
