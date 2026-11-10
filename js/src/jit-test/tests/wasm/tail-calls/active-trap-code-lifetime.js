// |jit-test| skip-if: getBuildConfiguration("simulator")

// Regression test: a trap reached through a tail call must keep its code
// segment alive.
//
// instanceC tail-calls (return_call_ref) into instanceA's "a", which traps on a
// return_call_indirect. Both frames are unwound and every other reference to
// instanceA is dropped, so only the trap machinery keeps its code segment alive.
// Building the RuntimeError allocates the oversized filename, triggering a GC
// under gczeal.
// If the code segment is freed, the use-after-free is observable.

const source = `
  const instanceC = wasmEvalText(\`(module
    (type $void (func))
    (import "m" "pressure" (func $pressure))
    (global $callee (mut funcref) (ref.null func))
    (func (export "set") (param $value funcref)
      local.get $value
      global.set $callee
    )
    (func (export "trigger")
      (local $target (ref null $void))
      global.get $callee
      ref.cast (ref null $void)
      local.set $target
      ref.null func
      global.set $callee
      call $pressure
      local.get $target
      return_call_ref $void
    )
  )\`, {m: {pressure() {}}}).exports;

  let instanceB = wasmEvalText(\`(module
    (func (export "g") (param i32) (result i32)
      local.get 0
    )
  )\`);

  let instanceA = wasmEvalText(\`(module
    (type $badTarget (func (param i32 i32)))
    (table 1 funcref)
    (func (export "setTarget") (param $target funcref)
      i32.const 0
      local.get $target
      table.set
    )
    (func (export "a")
      i32.const 1
      i32.const 2
      i32.const 0
      return_call_indirect (type $badTarget)
    )
  )\`);

  instanceA.exports.setTarget(instanceB.exports.g);
  instanceC.set(instanceA.exports.a);
  instanceA = null;
  instanceB = null;

  assertErrorMessage(
    () => instanceC.trigger(),
    WebAssembly.RuntimeError,
    /indirect call signature mismatch/
  );
`;
gczeal(2, 1);
evaluate(source, {fileName: "P".repeat(16 * 1024 * 1024)});
