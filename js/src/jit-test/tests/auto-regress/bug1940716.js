// |jit-test| --disable-main-thread-denormals; skip-if: !getBuildConfiguration("can-disable-main-thread-denormals") || !wasmIsSupported();

function a(b) {
  c = new WebAssembly.Module(b);
  return new WebAssembly.Instance(c);
}
function d(e) {
  return a(wasmTextToBinary(e));
}
f = [ , Number.MIN_VALUE ]
let { refTest } = d(`(func (export "refTest") (param externref))`).exports;
for (h of f)
  refTest(h);
