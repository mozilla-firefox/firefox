// |jit-test| --disable-main-thread-wasm-denormals; skip-if: !getBuildConfiguration("can-disable-main-thread-wasm-denormals") || !wasmIsSupported();

function a(b) {
  c = new WebAssembly.Module(b);
  return new WebAssembly.Instance(c);
}
function d(e) {
  return a(wasmTextToBinary(e));
}
let { refTest } = d(`(func (export "refTest") (param externref))`).exports;

// Ensure there are enough values to trigger a wasm JIT entry
f = Array(16).fill([Number.MIN_VALUE, -Number.MIN_VALUE]).flat();
for (h of f)
  refTest(h);
