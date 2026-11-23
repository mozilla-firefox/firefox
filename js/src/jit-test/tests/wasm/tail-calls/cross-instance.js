// A cross-instance return call whose caller cannot restore its own instance
// state (e.g. a JS caller, or a direct wasm call) is made as a regular call.
// Check that the stack stays bounded, and that the frame making the call is
// hidden from stack traces and exception handling, and is not traced by GC.

const N = getBuildConfiguration("simulator") ? 100000 : 1000000;

function makeTable() {
  return wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (table (export "tab") 2 (ref null $t)))`).exports.tab;
}

function returnCall(kind, index) {
  switch (kind) {
    case "indirect":
      return `i32.const ${index} return_call_indirect $tab (type $t)`;
    case "ref":
      return `i32.const ${index} table.get $tab return_call_ref $t`;
    case "import":
      return `return_call $callee`;
    case "import-js":
      return `return_call $jsCallee`;
  }
  throw new Error("unknown kind");
}

// Two instances that tail call each other through a shared table until the
// counter reaches zero. Each returns its own id at the end of the chain.
function makePingPong(kind) {
  const tab = makeTable();
  const make = (id, other) => wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "tab" (table $tab 2 (ref null $t)))
    (func $f (export "f") (type $t)
      local.get 0
      i32.eqz
      if (result i32)
        i32.const ${id}
      else
        local.get 0
        i32.const 1
        i32.sub
        ${returnCall(kind, other)}
      end)
    (func (export "viaDirectCall") (param i32) (result i32)
      local.get 0
      call $f
      i32.const 100
      i32.add))`, {"": {tab}}).exports;
  const a = make(0, 1);
  const b = make(1, 0);
  tab.set(0, a.f);
  tab.set(1, b.f);
  return {a, b};
}

for (const kind of ["indirect", "ref"]) {
  const {a, b} = makePingPong(kind);
  assertEq(a.f(0), 0);
  assertEq(a.f(1), 1);
  assertEq(a.f(N), N % 2);
  assertEq(b.f(N), (N + 1) % 2);
  assertEq(a.viaDirectCall(N), 100 + N % 2);
  assertEq(b.viaDirectCall(N + 1), 100 + N % 2);
}

// A chain alternating return_call to an import and a return_call_indirect.
{
  const tab = makeTable();
  const b = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "tab" (table $tab 2 (ref null $t)))
    (func (export "f") (type $t)
      local.get 0
      i32.eqz
      if (result i32)
        i32.const 1
      else
        local.get 0
        i32.const 1
        i32.sub
        ${returnCall("indirect", 0)}
      end))`, {"": {tab}}).exports;
  const a = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "next" (func $next (type $t)))
    (func (export "f") (type $t)
      local.get 0
      i32.eqz
      if (result i32)
        i32.const 0
      else
        local.get 0
        i32.const 1
        i32.sub
        return_call $next
      end))`, {"": {next: b.f}}).exports;
  tab.set(0, a.f);
  assertEq(a.f(N), N % 2);
  assertEq(b.f(N), (N + 1) % 2);
}

// Results returned in the stack results area.
for (const kind of ["indirect", "ref"]) {
  const tab = wasmEvalText(`(module
    (type $t (func (param i32) (result i64 i64 i64 i64 i64 i64 i64 i64 i64 i64)))
    (table (export "tab") 2 (ref null $t)))`).exports.tab;
  const callee = wasmEvalText(`(module
    (type $t (func (param i32) (result i64 i64 i64 i64 i64 i64 i64 i64 i64 i64)))
    (func (export "f") (type $t)
      (i64.extend_i32_u (local.get 0))
      (i64.const 1) (i64.const 2) (i64.const 3) (i64.const 4)
      (i64.const 5) (i64.const 6) (i64.const 7) (i64.const 8)
      (i64.const 9)))`).exports;
  tab.set(1, callee.f);
  const caller = wasmEvalText(`(module
    (type $t (func (param i32) (result i64 i64 i64 i64 i64 i64 i64 i64 i64 i64)))
    (import "" "tab" (table $tab 2 (ref null $t)))
    (func (export "f") (type $t)
      local.get 0
      ${returnCall(kind, 1)}))`, {"": {tab}}).exports;
  assertDeepEq(caller.f(42), [42n, 1n, 2n, 3n, 4n, 5n, 6n, 7n, 8n, 9n]);
}

// The caller of the return call is not in stack traces.
for (const kind of ["indirect", "ref", "import", "import-js"]) {
  const tab = makeTable();
  let stack;
  const capture = () => { stack = new Error().stack; };
  const callee = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "capture" (func $capture))
    (func $callee (export "f") (type $t)
      call $capture
      local.get 0))`, {"": {capture}}).exports;
  tab.set(1, callee.f);
  const jsCallee = x => { capture(); return x; };
  const caller = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "tab" (table $tab 2 (ref null $t)))
    (import "" "callee" (func $callee (type $t)))
    (import "" "jsCallee" (func $jsCallee (type $t)))
    (func $caller (export "f") (type $t)
      local.get 0
      ${returnCall(kind, 1)}))`, {"": {tab, callee: callee.f, jsCallee}}).exports;
  assertEq(caller.f(3), 3);
  assertEq(stack.includes("callee@"), kind != "import-js");
  assertEq(stack.includes("caller@"), false);
}

// An exception thrown by the callee is not caught by the caller.
for (const kind of ["indirect", "ref", "import", "import-js"]) {
  for (const tryKind of ["try_table", "legacy"]) {
    const tab = makeTable();
    const boom = () => { throw "boom"; };
    const callee = wasmEvalText(`(module
      (type $t (func (param i32) (result i32)))
      (import "" "boom" (func $boom))
      (func (export "f") (type $t)
        call $boom
        local.get 0))`, {"": {boom}}).exports;
    tab.set(1, callee.f);
    const body = `local.get 0 ${returnCall(kind, 1)}`;
    const tryBlock = tryKind === "try_table"
      ? `(block $caught
           (try_table (catch_all $caught) ${body}))`
      : `try ${body} catch_all end`;
    const caller = wasmEvalText(`(module
      (type $t (func (param i32) (result i32)))
      (import "" "tab" (table $tab 2 (ref null $t)))
      (import "" "callee" (func $callee (type $t)))
      (import "" "jsCallee" (func $jsCallee (type $t)))
      (func (export "f") (type $t)
        ${tryBlock}
        i32.const -1))`, {"": {tab, callee: callee.f, jsCallee: boom}}).exports;
    let caught;
    try {
      caller.f(0);
    } catch (e) {
      caught = e;
    }
    assertEq(caught, "boom");
  }
}

// References held by the caller are not kept alive during the callee.
for (const kind of ["indirect", "ref", "import", "import-js"]) {
  const tab = makeTable();
  const wm = new WeakMap();
  const check = () => {
    gc();
    assertEq(nondeterministicGetWeakMapKeys(wm).length, 0);
  };
  const callee = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "check" (func $check))
    (func (export "f") (type $t)
      call $check
      local.get 0))`, {"": {check}}).exports;
  tab.set(1, callee.f);
  const caller = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "tab" (table $tab 2 (ref null $t)))
    (import "" "callee" (func $callee (type $t)))
    (import "" "jsCallee" (func $jsCallee (type $t)))
    (import "" "make" (func $make (result externref)))
    (import "" "use" (func $use (param externref)))
    (func (export "f") (type $t)
      (local $ref externref)
      call $make
      local.set $ref
      local.get $ref
      call $use
      local.get 0
      ${returnCall(kind, 1)}))`, {"": {
    tab,
    make() {
      const obj = {};
      wm.set(obj, true);
      return obj;
    },
    use(obj) {
      assertEq(nondeterministicGetWeakMapKeys(wm).length, 1);
    },
    callee: callee.f,
    jsCallee: x => { check(); return x; },
  }}).exports;
  assertEq(caller.f(5), 5);
}

// A signature mismatch in the callee of the regular call unwinds the callee's
// frame and traps in the caller, which is still hidden.
{
  const callee = wasmEvalText(`(module
    (func (export "f") (param i64) (result i32) (i32.const 1)))`).exports;
  const tab = new WebAssembly.Table({initial: 2, element: "anyfunc"});
  tab.set(1, callee.f);
  const {outer} = wasmEvalText(`(module
    (type $t (func (param i32) (result i32)))
    (import "" "tab" (table $tab 2 funcref))
    (func $caller (type $t) local.get 0 ${returnCall("indirect", 1)})
    (func $outer (export "outer") (result i32) (call $caller (i32.const 7))))`,
    {"": {tab}}).exports;
  let caught;
  try {
    outer();
  } catch (e) {
    caught = e;
  }
  assertEq(caught instanceof WebAssembly.RuntimeError, true);
  assertEq(caught.stack.includes("outer@"), true);
  assertEq(caught.stack.includes("caller@"), false);
}
