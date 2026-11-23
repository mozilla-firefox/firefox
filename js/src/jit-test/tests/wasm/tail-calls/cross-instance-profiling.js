// A cross-instance return call whose caller cannot restore its own instance
// state is made as a regular call. The frame that made it stays on the stack
// until the call returns, and the profiler reports it.

enableGeckoProfiling();

let stack;
let throws = false;
function probe() {
  stack = JSON.stringify(readGeckoProfilingStack());
  if (throws) {
    throw "boom";
  }
  return 7;
}

function check(f) {
  stack = undefined;
  assertEq(f(1), 107);
  assertEq(typeof stack, "string");
  assertEq(stack.includes("aye"), true);
  assertEq(stack.includes("bee"), true);
  assertEq(stack.indexOf("bee") < stack.indexOf("aye"), true);
}

// Unwinding an exception through the frame that made the return call while
// profiling.
function checkThrows(f) {
  stack = undefined;
  throws = true;
  let caught;
  try {
    f(1);
  } catch (e) {
    caught = e;
  }
  throws = false;
  assertEq(caught, "boom");
  assertEq(stack.includes("bee"), true);
}

const tab = new WebAssembly.Table({initial: 1, element: "anyfunc"});
const callee = wasmEvalText(`(module
  (import "" "probe" (func $probe (param i32) (result i32)))
  (func $cee (export "cee") (param i32) (result i32)
    (call $probe (i32.const 1))))`, {"": {probe}}).exports;
tab.set(0, callee.cee);

const ayes = wasmEvalText(`(module
  (type $t (func (param i32) (result i32)))
  (import "" "tab" (table 1 funcref))
  (import "" "probe" (func $probe (param i32) (result i32)))
  (import "" "cee" (func $cee (type $t)))
  (elem declare func $cee)
  (global $ceeRef (ref $t) (ref.func $cee))
  (func $beeIndirect (param i32) (result i32)
    (return_call_indirect (type $t) (local.get 0) (i32.const 0)))
  (func $beeRef (param i32) (result i32)
    (return_call_ref $t (local.get 0) (global.get $ceeRef)))
  (func $beeWasmImport (param i32) (result i32)
    (return_call $cee (local.get 0)))
  (func $beeImport (param i32) (result i32)
    (return_call $probe (local.get 0)))
  (func $ayeIndirect (export "ayeIndirect") (param i32) (result i32)
    (i32.add (call $beeIndirect (local.get 0)) (i32.const 100)))
  (func $ayeRef (export "ayeRef") (param i32) (result i32)
    (i32.add (call $beeRef (local.get 0)) (i32.const 100)))
  (func $ayeWasmImport (export "ayeWasmImport") (param i32) (result i32)
    (i32.add (call $beeWasmImport (local.get 0)) (i32.const 100)))
  (func $ayeImport (export "ayeImport") (param i32) (result i32)
    (i32.add (call $beeImport (local.get 0)) (i32.const 100))))`,
  {"": {tab, probe, cee: callee.cee}}).exports;

// From the wasm callee, profiling walks through the frame that made the return
// call to `aye`, for each kind of cross-instance return call.
for (const name of ["ayeIndirect", "ayeRef", "ayeWasmImport"]) {
  check(ayes[name]);
  checkThrows(ayes[name]);
}
// The import exit stub is called directly by the frame that made the return
// call, so profiling starts from that frame.
check(ayes.ayeImport);
checkThrows(ayes.ayeImport);

// Single-step profiling samples the stack at every instruction, including the
// callee's prologue and epilogue, the import exit stub, and the instance
// restore after the call returns. Every sample must be a well-formed stack
// (innermost first): the frame that made the return call is always below `aye`.
if (WasmHelpers.isSingleStepProfilingEnabled) {
  for (const [name, inner] of [["ayeIndirect", "cee"], ["ayeRef", "cee"],
                               ["ayeWasmImport", "cee"], ["ayeImport", "<"]]) {
    // A throwing callee also samples the throw stub and the unwinding through
    // the frame that made the return call.
    for (const t of [false, true]) {
      throws = t;
      enableSingleStepProfiling();
      let caught;
      try {
        assertEq(ayes[name](1), 107);
      } catch (e) {
        caught = e;
      }
      throws = false;
      const stacks = WasmHelpers.normalizeStacks(disableSingleStepProfiling());
      assertEq(caught, t ? "boom" : undefined);
      let sawInner = false;
      for (const s of stacks) {
        const frames = s.split(",");
        const bee = frames.findIndex(f => f.startsWith("bee"));
        const aye = frames.findIndex(f => f.startsWith("aye"));
        if (bee >= 0 && aye >= 0 && bee > aye) {
          throw new Error(`${name}: frame order broken in ${s}`);
        }
        sawInner ||= frames[0] == inner;
      }
      assertEq(sawInner, true);
    }
  }
}
