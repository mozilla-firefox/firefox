// |jit-test| skip-if: !wasmDebuggingEnabled()

// A GC while a return_call to an import is in progress, in a debug-enabled
// module.  The frame being traced is then at a ReturnCallFallback call site.

var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);
g.bin = wasmTextToBinary(`(module
  (import "m" "f" (func $f (param externref) (result i32)))
  (func $mid (param externref) (result i32)
    (return_call $f (local.get 0)))
  (func (export "run") (param externref) (result i32)
    (call $mid (local.get 0))))`);
g.eval(`
  var ins = new WebAssembly.Instance(new WebAssembly.Module(bin),
                                     {m: {f: (x) => { gc(); return 1; }}});
  var result = ins.exports.run({});
`);
assertEq(g.result, 1);
