// |jit-test| skip-if: !wasmDebuggingEnabled()
// A cross-instance return call whose caller cannot restore its own instance
// state is made as a regular call. The debugger must see the calling frame as
// left once the return call is made.

const callerBodies = {
  "indirect": "local.get 0 i32.const 1 return_call_indirect $tab (type $t)",
  "ref": "local.get 0 i32.const 1 table.get $tab return_call_ref $t",
  "import": "local.get 0 return_call $callee",
  "import-js": "local.get 0 return_call $jsCallee",
};

for (const kind of Object.keys(callerBodies)) {
  // With a JS callee, only the caller is a wasm frame.
  const wasmFrames = kind == "import-js" ? 1 : 2;
  for (const throws of [false, true]) {
    const g = newGlobal({newCompartment: true});
    const dbg = new Debugger(g);

    let enters = 0;
    let pops = 0;
    let unwinds = 0;
    let wasmFramesInCallee;
    let callerFrame;
    dbg.onEnterFrame = frame => {
      if (frame.type != "wasmcall") {
        return;
      }
      enters++;
      callerFrame ??= frame;
      frame.onPop = () => {
        pops++;
      };
    };
    dbg.onExceptionUnwind = frame => {
      if (frame.type == "wasmcall") {
        unwinds++;
      }
    };
    g.hook = () => {
      // The caller's frame is still on the machine stack, but it has no
      // stackmap and must not be visible as a live debugger frame.
      gc();
      assertEq(callerFrame.onStack, false);
      wasmFramesInCallee = 0;
      for (let f = dbg.getNewestFrame(); f; f = f.older) {
        if (f.type == "wasmcall") {
          wasmFramesInCallee++;
        }
      }
      if (throws) {
        throw "boom";
      }
    };

    g.eval(`
      function probe() { hook(); }
      function jsCallee(x) { hook(); return x; }
      var tab =new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
        (type $t (func (param i32) (result i32)))
        (table (export "tab") 2 (ref null $t)))\`))).exports.tab;
      var callee = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
        (type $t (func (param i32) (result i32)))
        (import "" "probe" (func $probe))
        (func (export "f") (type $t)
          call $probe
          local.get 0))\`)), {"": {probe}}).exports.f;
      tab.set(1, callee);
      var caller = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
        (type $t (func (param i32) (result i32)))
        (import "" "tab" (table $tab 2 (ref null $t)))
        (import "" "callee" (func $callee (type $t)))
        (import "" "jsCallee" (func $jsCallee (type $t)))
        (func (export "f") (type $t)
          ${callerBodies[kind]}))\`)), {"": {tab, callee, jsCallee}}).exports.f;
    `);

    let result;
    let caught;
    try {
      result = g.caller(7);
    } catch (e) {
      caught = e;
    }
    if (throws) {
      assertEq(caught, "boom");
    } else {
      assertEq(result, 7);
    }
    assertEq(wasmFramesInCallee, wasmFrames - 1);
    assertEq(enters, wasmFrames);
    assertEq(pops, wasmFrames);
    // Only the wasm callee unwinds; the caller has already been left.
    assertEq(unwinds, throws ? wasmFrames - 1 : 0);
  }
}

// An exception thrown by the callee is caught by a try in the caller's caller,
// which made a direct call to the caller. The caller is skipped while
// unwinding.
for (const kind of Object.keys(callerBodies)) {
  const wasmFrames = kind == "import-js" ? 2 : 3;
  const g = newGlobal({newCompartment: true});
  const dbg = new Debugger(g);

  let enters = 0;
  let pops = 0;
  let unwinds = 0;
  dbg.onEnterFrame = frame => {
    if (frame.type != "wasmcall") {
      return;
    }
    enters++;
    frame.onPop = () => {
      pops++;
    };
  };
  dbg.onExceptionUnwind = frame => {
    if (frame.type == "wasmcall") {
      unwinds++;
    }
  };

  g.eval(`
    var tag = new WebAssembly.Tag({parameters: []});
    function jsCallee(x) { throw new WebAssembly.Exception(tag, []); }
    var tab = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
      (type $t (func (param i32) (result i32)))
      (table (export "tab") 2 (ref null $t)))\`))).exports.tab;
    var callee = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
      (type $t (func (param i32) (result i32)))
      (import "" "tag" (tag $e))
      (func (export "f") (type $t)
        throw $e))\`)), {"": {tag}}).exports.f;
    tab.set(1, callee);
    var outer = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
      (type $t (func (param i32) (result i32)))
      (import "" "tab" (table $tab 2 (ref null $t)))
      (import "" "callee" (func $callee (type $t)))
      (import "" "jsCallee" (func $jsCallee (type $t)))
      (import "" "tag" (tag $e))
      (func $caller (type $t)
        ${callerBodies[kind]})
      (func (export "f") (param i32) (result i32)
        (block $caught
          (try_table (catch $e $caught)
            (return (call $caller (local.get 0)))))
        i32.const -1))\`)), {"": {tab, callee, jsCallee, tag}}).exports.f;
  `);

  assertEq(g.outer(7), -1);
  assertEq(enters, wasmFrames);
  assertEq(pops, wasmFrames);
  // Only the throwing wasm callee unwinds: the caller has already been left,
  // and the outer function catches.
  assertEq(unwinds, wasmFrames - 2);
}

// A signature mismatch in the callee of the regular call unwinds the callee's
// frame and traps in the caller, which has already been left.
{
  const g = newGlobal({newCompartment: true});
  const dbg = new Debugger(g);
  let enters = 0;
  let pops = 0;
  let unwinds = 0;
  dbg.onEnterFrame = frame => {
    if (frame.type != "wasmcall") {
      return;
    }
    enters++;
    frame.onPop = () => {
      pops++;
    };
  };
  dbg.onExceptionUnwind = frame => {
    if (frame.type == "wasmcall") {
      unwinds++;
    }
  };
  g.eval(`
    var callee = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
      (func (export "f") (param i64) (result i32) (i32.const 1)))\`))).exports.f;
    var tab = new WebAssembly.Table({initial: 1, element: "anyfunc"});
    tab.set(0, callee);
    var outer = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(\`(module
      (type $t (func (param i32) (result i32)))
      (import "" "tab" (table 1 funcref))
      (func $caller (result i32)
        (return_call_indirect (type $t) (i32.const 7) (i32.const 0)))
      (func (export "f") (result i32) (call $caller)))\`)), {"": {tab}}).exports.f;
  `);
  let caught;
  try {
    g.outer();
  } catch (e) {
    caught = e;
  }
  assertEq(caught instanceof g.WebAssembly.RuntimeError, true);
  assertEq(enters, 2);
  assertEq(pops, 2);
  // Only the outer function unwinds.
  assertEq(unwinds, 1);
}
