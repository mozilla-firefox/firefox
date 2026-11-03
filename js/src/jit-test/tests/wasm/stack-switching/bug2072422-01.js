// |jit-test| skip-if: !wasmStackSwitchingEnabled() || !('newGlobal' in this)
const runner = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(`(module
  (type $ft (func))
  (type $ct (cont $ft))
  (table $t (export "t") 1 (ref null $ft))
  (global $k (mut (ref null $ct)) (ref.null $ct))
  (func (export "capture")
    (global.set $k (cont.new $ct (table.get $t (i32.const 0))))
    (table.set $t (i32.const 0) (ref.null $ft)))
  (func (export "run") global.get $k resume $ct))`))).exports;
let other = newGlobal({sameCompartmentAs: this});
let entry = new other.WebAssembly.Instance(new other.WebAssembly.Module(wasmTextToBinary(`(module
  (type $ft (func))
  (type $bad (func (param i32)))
  (table 1 funcref)
  (func $callee (type $bad))
  (elem (i32.const 0) func $callee)
  (func (export "f") i32.const 0 return_call_indirect (type $ft)))`))).exports;
runner.t.set(0, entry.f);
runner.capture();
entry = null; other = null;
gczeal(14, 1);
let err;
try { runner.run(); } catch (e) { err = e; }
assertEq(/indirect call signature mismatch/.test(String(err)), true);
