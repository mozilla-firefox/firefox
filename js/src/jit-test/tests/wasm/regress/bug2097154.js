const other = newGlobal({sameCompartmentAs: globalThis});
const callback = other.eval('() => { throw 1; }');

const {call, returnCall, catchAll} = new WebAssembly.Instance(
  new WebAssembly.Module(wasmTextToBinary(`(module
    (import "" "callback" (func $callback))
    (func (export "call") call $callback)
    (func (export "returnCall") return_call $callback)
    (func (export "catchAll") (result i32)
      (block $b
        (try_table (catch_all $b)
          call $callback)
        (return (i32.const 0)))
      i32.const 1))`)),
  {'': {callback}}).exports;

for (const f of [call, returnCall]) {
  gczeal(2, 1);
  let caught;
  try {
    f();
  } catch (e) {
    caught = e;
  }
  gczeal(0);
  assertEq(caught, 1);
}

gczeal(2, 1);
const res = catchAll();
gczeal(0);
assertEq(res, 1);
