import * as mod from "cycle-wasm.wasm";

export let jsGlob = new WebAssembly.Global({ value: "i32", mutable: true }, 42);

// Read the wasm export before the wasm module has executed, while its
// bindings are still uninitialized.
export let early;
try {
  early = "value:" + mod.wasmGlob;
} catch (e) {
  early = "throw:" + e.name;
}

export function readWasmGlob() {
  return mod.wasmGlob;
}
