// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A wasm export named ".generator" must not be mistaken for the internal
// binding holding a module's top-level-await generator object.

import { ".generator" as gen } from "exports-dot-generator.wasm";
import * as ns from "exports-dot-generator.wasm";

assertEq(gen(), 42);
assertEq(ns[".generator"], gen);

const dyn = await import("exports-dot-generator.wasm");
assertEq(dyn[".generator"](), 42);
