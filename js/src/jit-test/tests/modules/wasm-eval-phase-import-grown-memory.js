// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration

// When a wasm module imports from another wasm module, the import type is
// checked against the exporter's declared type, not the current state of the
// exported memory. The exporter grows its memory from 1 to 2 pages, but an
// import of (memory 2) still fails with a LinkError.

const { mem } = await import("exports-grown-memory.wasm");
assertEq(mem.buffer.byteLength, 2 * 65536);

let linkError = null;
try {
  await import("imports-grown-memory.wasm");
} catch (e) {
  linkError = e;
}
assertEq(linkError instanceof WebAssembly.LinkError, true);
assertEq(linkError.message,
         "incompatible type for import 'exports-grown-memory.wasm.mem'");
