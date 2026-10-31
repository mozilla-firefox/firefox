// |jit-test| module; skip-if: !getBuildConfiguration("source-phase-imports") || getBuildConfiguration("release_or_beta"); --wasm-compiler=none; --enable-wasm-esm-integration

// When wasm is disabled, importing a wasm module fails at compile time, at
// both the source and evaluation phases.

for (const load of [() => import.source("exports-fun.wasm"),
                    () => import("exports-fun.wasm")]) {
  let error;
  try {
    await load();
  } catch (e) {
    error = e;
  }
  assertEq(error?.name, "CompileError");
  assertEq(error.message, "WebAssembly is not supported");
}
