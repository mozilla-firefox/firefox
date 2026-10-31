// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// In a cycle, the wasm module executes before the JS module that it imports
// from, so reading the uninitialized binding `fn` in ExecuteModule
// (GetBindingValue) throws a ReferenceError.

let error = null;
try {
  await import("cycle-tdz-js.js");
} catch (e) {
  error = e;
}
assertEq(error instanceof ReferenceError, true);
assertEq(error.message.includes("fn"), true);
