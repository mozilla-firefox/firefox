// |jit-test| skip-if: !wasmStackSwitchingEnabled()

function inst(text, imports) {
  return new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(text)), imports).exports;
}
const target = inst(`(module
  (import "" "collect" (func $collect))
  (tag $t (export "t"))
  (func (export "suspend") (result i32)
    suspend $t
    call $collect
    i32.const 43))`, {"": {collect() { gc(); }}});
const runner = inst(`(module
  (import "" "t" (tag $t))
  (type $ft (func (result i32)))
  (type $ct (cont $ft))
  (table $table (export "table") 1 (ref null $ft))
  (global $inner (mut (ref null $ct)) (ref.null $ct))
  (global $k (mut (ref null $ct)) (ref.null $ct))
  (func $outer (type $ft)
    (resume $ct (global.get $inner)))
  (elem declare func $outer)
  (func (export "capture")
    (global.set $inner (cont.new $ct (table.get $table (i32.const 0))))
    (table.set $table (i32.const 0) (ref.null $ft)))
  (func (export "run") (result i32)
    (block $h (result (ref $ct))
      (return (resume $ct (on $t $h) (cont.new $ct (ref.func $outer)))))
    global.set $k
    (global.set $inner (ref.null $ct))
    i32.const -1)
  (func (export "finish") (result i32)
    (resume $ct (global.get $k))))`, {"": {t: target.t}});
function setup() {
  const entry = inst(`(module
    (import "" "target" (func $target (result i32)))
    (func (export "run") (result i32) return_call $target))`, {"": {target: target.suspend}});
  runner.table.set(0, entry.run);
  runner.capture();
}
setup();
assertEq(runner.run(), -1);
gc();
gc();
assertEq(runner.finish(), 43);

(function () {
  let aRef;
  const runner = inst(`(module
    (type $ft (func (result i32)))
    (type $ct (cont $ft))
    (table $table (export "table") 1 (ref null $ft))
    (func (export "run") (result i32)
      (local $c (ref null $ct))
      (local.set $c (cont.new $ct (table.get $table (i32.const 0))))
      (table.set $table (i32.const 0) (ref.null $ft))
      (resume $ct (local.get $c))))`);
  const b = inst(`(module
    (import "" "js" (func $js (result i32)))
    (func (export "b") (result i32) (return_call $js)))`, {"": {js() {
      clearKeptObjects();
      gc();
      gc();
      assertEq(aRef.deref() !== undefined, true);
      return 43;
    }}});
  (function () {
    const a = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(`(module
      (import "" "b" (func $b (result i32)))
      (func (export "a") (result i32) (return_call $b)))`)), {"": {b: b.b}});
    aRef = new WeakRef(a);
    runner.table.set(0, a.exports.a);
  })();
  assertEq(runner.run(), 43);
})();
