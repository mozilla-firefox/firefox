// |jit-test| skip-if: !wasmDebuggingEnabled()

const f = newGlobal({ newCompartment: true });
f.g = this;
f.eval(`
  Debugger(g).onEnterFrame = function(frame) { frame.environment }
`);

const types = [
  "anyref", "(ref any)",
  "eqref", "(ref eq)",
  "i31ref", "(ref i31)",
  "structref", "(ref struct)",
  "arrayref", "(ref array)",
  "nullref", "(ref none)",

  "funcref", "(ref func)",
  "nullfuncref", "(ref nofunc)",

  "externref", "(ref extern)",
  "nullexternref", "(ref noextern)",

  "exnref", "(ref exn)",
  "nullexnref", "(ref noexn)",

  "(ref null $s)", "(ref $s)",
];

for (const type of types) {
  const m = new WebAssembly.Module(wasmTextToBinary(`(module
    (type $s (struct))
    (func (export "test")
      (local ${type})
    )
  )`));
  const { test } = new WebAssembly.Instance(m).exports;
  test();
}
