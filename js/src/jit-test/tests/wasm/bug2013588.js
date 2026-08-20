// |jit-test| skip-if: !wasmDebuggingEnabled()
// Bug 2013588 - Accessing a saved wasm Debugger.Environment after the frame is
// popped must not crash.

var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);

g.wasmBytes = wasmTextToBinary(`(module
  (func (export "f") (param i32) (result i32)
    local.get 0
  )
)`);

var savedEnv = null;
dbg.onEnterFrame = function(frame) {
  if (frame.type === "wasmcall") {
    savedEnv = frame.environment;
  }
};

g.eval(`
  var inst = new WebAssembly.Instance(new WebAssembly.Module(wasmBytes));
  inst.exports.f(42);
  inst = null;
`);

assertEq(savedEnv !== null, true);
assertEq(savedEnv.getVariable("var0").optimizedOut, true);
