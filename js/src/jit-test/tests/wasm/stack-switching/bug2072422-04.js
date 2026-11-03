// |jit-test| skip-if: !wasmStackSwitchingEnabled()
const runner = wasmEvalText(`(module
  (type $ft (func (result i32)))
  (type $ct (cont $ft))
  (tag $yield (export "yield"))
  (table $table (export "table") 1 (ref null $ft))
  (global $k (mut (ref null $ct)) (ref.null $ct))
  (func (export "capture")
    (global.set $k (cont.new $ct (table.get $table (i32.const 0))))
    (table.set $table (i32.const 0) (ref.null $ft)))
  (func (export "start") (result i32)
    (block $on (result (ref $ct))
      (resume $ct (on $yield $on) (global.get $k))
      return)
    (global.set $k)
    i32.const -1)
  (func (export "finish") (result i32) (resume $ct (global.get $k))))`).exports;
const target = wasmEvalText(`(module
  (import "" "yield" (tag $yield))
  (import "" "collect" (func $collect))
  (type $ft (func (result i32)))
  (type $bad (func (param i32) (result i32)))
  (table 1 funcref)
  (func $g (type $bad) local.get 0)
  (elem (i32.const 0) func $g)
  (func (export "f") (result i32)
    suspend $yield
    i32.const 0 return_call_indirect (type $ft)))`, {"": {yield: runner.yield, collect() { gc(); gc(); }}}).exports;
function setup() {
  let entry = wasmEvalText(`(module
    (import "" "target" (func $target (result i32)))
    (func (export "run") (result i32) return_call $target))`,
    {"": {target: target.f}}).exports;
  runner.table.set(0, entry.run);
  runner.capture();
}
setup();
assertEq(runner.start(), -1);
gc(); gc();
assertErrorMessage(() => runner.finish(), WebAssembly.RuntimeError,
                   /indirect call signature mismatch/);
