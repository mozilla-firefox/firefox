// |jit-test| skip-if: !wasmStackSwitchingEnabled()

var bin = wasmTextToBinary(`(module
  (type $ft (func))
  (type $ct (cont $ft))
  (import "m" "js" (func $js))
  (func $f (type $ft) (return_call $js))
  (elem declare func $f)
  (func (export "run") (resume $ct (cont.new $ct (ref.func $f))))
)`);
var ins = new WebAssembly.Instance(new WebAssembly.Module(bin),
                                   {m: {js() { throw new Error("x"); }}});
try { ins.exports.run(); } catch (e) {}
