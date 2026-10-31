// |jit-test| module; skip-if: !getBuildConfiguration("source-phase-imports") || !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-source-phase-imports; --enable-wasm-esm-integration

import source mod from "empty.wasm";
import source exportsFun from "exports-fun.wasm";

assertEq(mod instanceof WebAssembly.Module, true);

const AbstractModuleSource = getAbstractModuleSource();
assertEq(mod instanceof AbstractModuleSource, true);

assertEq(exportsFun instanceof WebAssembly.Module, true);
const instance = new WebAssembly.Instance(exportsFun);
assertEq(typeof instance.exports.fun, "function");
assertEq(instance.exports.fun(3, 4), 7);

let compileError = null;
try {
  await import.source("invalid.wasm");
} catch (e) {
  compileError = e;
}
assertEq(compileError instanceof WebAssembly.CompileError, true);
assertEq(compileError.fileName.endsWith("invalid.wasm"), true);

import.source("empty.wasm").then(mod => assertEq(mod instanceof WebAssembly.Module, true), e => assertEq(true, false));

// Import source is not currently supported for JavaScript modules
compileError = null;
try {
  await import.source("empty.js");
} catch (e) {
  compileError = e;
}
assertEq(compileError instanceof SyntaxError, true);
assertEq(compileError.message , "Source phase imports not supported for this module type");
