// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// A module that fails to compile throws a CompileError whose fileName is the
// wasm file.
let compileError = null;
try {
  await import("invalid.wasm");
} catch (e) {
  compileError = e;
}
assertEq(compileError instanceof WebAssembly.CompileError, true);
assertEq(compileError.fileName.endsWith("invalid.wasm"), true);

// Reserved module, field and export names throw a LinkError when the module
// is compiled for ESM (FinishCompileForESM).
for (const [specifier, message] of [
  ["imports-reserved-module-name.wasm",
   "module import name 'wasm-js:foo' is reserved"],
  ["imports-reserved-field-name.wasm",
   "field import name 'wasm:a' is reserved"],
  ["exports-reserved-name.wasm", "export name 'wasm:f' is reserved"],
]) {
  let linkError = null;
  try {
    await import(specifier);
  } catch (e) {
    linkError = e;
  }
  assertEq(linkError instanceof WebAssembly.LinkError, true);
  assertEq(linkError.message, message);
}

// A function import from the imported string constants module fails
// validation, as every import from it must be a global of type (ref extern).
compileError = null;
try {
  await import("imports-string-constants-func.wasm");
} catch (e) {
  compileError = e;
}
assertEq(compileError instanceof WebAssembly.CompileError, true);
assertEq(compileError.message.includes("type mismatch"), true);
assertEq(compileError.fileName.endsWith("imports-string-constants-func.wasm"), true);
