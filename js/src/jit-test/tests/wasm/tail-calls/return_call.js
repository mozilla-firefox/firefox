var ins = wasmEvalText(`(module
    (func $fac-acc (export "fac-acc") (param i64 i64) (result i64)
        (if (result i64) (i64.eqz (local.get 0))
        (then (local.get 1))
        (else
            (return_call $fac-acc
                (i64.sub (local.get 0) (i64.const 1))
                (i64.mul (local.get 0) (local.get 1))
            )
        )
        )
    )
    (func (export "main") (param i64) (result i64)
        (call $fac-acc (local.get 0) (i64.const 1))
    )
)`);

// Check return call via wasm function
assertEq(ins.exports.main(5n), 120n);

// Check return call directly via interpreter stub
const fac = ins.exports["fac-acc"];
assertEq(fac(4n, 1n), 24n);

// Check return call directly via jit stub
check_stub1: {
    let options = getJitCompilerOptions();
    if (!options["baseline.enable"]) break check_stub1;
    const check = function() {
        fac(4n, 1n);
    };
    for (let i = options["baseline.warmup.trigger"] + 1; i--;)
        check();
}

// Return call of an import
var ins0 = wasmEvalText(`(module
    (func $fac-acc (export "fac-acc") (param i64 i64) (result i64)
      (if (result i64) (i64.eqz (local.get 0))
        (then (local.get 1))
        (else
          (return_call $fac-acc
            (i64.sub (local.get 0) (i64.const 1))
            (i64.mul (local.get 0) (local.get 1))
          )
        )
      )
    )
)`);

var ins = wasmEvalText(`(module
    (import "" "fac-acc" (func $fac-acc (param i64 i64) (result i64)))
    (func (export "fac") (param i64) (result i64)
      local.get 0
      i64.const 1
      return_call $fac-acc
    )
    (func (export "main") (result i64)
      i64.const 4
      call 1
    )
)`, {"": {"fac-acc": ins0.exports["fac-acc"],}});

assertEq(ins.exports.main(), 24n);
assertEq(ins.exports.fac(3n, 1n), 6n);
check_stub2: {
    let options = getJitCompilerOptions();
    if (!options["baseline.enable"]) break check_stub2;
    const check = function() {
        ins.exports.fac(3n, 1n)
    };
    for (let i = options["baseline.warmup.trigger"] + 1; i--;)
        check();
}

// Check with parameters area growth
var ins0 = wasmEvalText(`(module
  (func $fac-acc (export "fac-acc") (param i64 i64 i64 i64 i64 i64 i64 i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (local.get 1))
      (else
        (return_call $fac-acc
          (i64.sub (local.get 0) (i64.const 1))
          (i64.mul (local.get 0) (local.get 1))
          i64.const 1 i64.const 2 i64.const 3 i64.const 4 i64.const 5 i64.const 6
        )
      )
    )
  )
)`);

var ins = wasmEvalText(`(module
  (import "" "fac-acc" (func $fac-acc (param i64 i64 i64 i64 i64 i64 i64 i64) (result i64)))
  (func (export "fac") (param i64) (result i64)
    local.get 0
    i64.const 1
    i64.const 1 i64.const 2 i64.const 3 i64.const 4 i64.const 5 i64.const 6
    return_call $fac-acc
  )
  (func (export "main") (result i64)
    i64.const 5
    call 1
  )
)`, {"": {"fac-acc": ins0.exports["fac-acc"],}});

assertEq(ins.exports.main(), 120n);
assertEq(ins.exports.fac(3n, 1n), 6n);
check_stub3: {
    let options = getJitCompilerOptions();
    if (!options["baseline.enable"]) break check_stub3;
    const check = function() {
        ins.exports.fac(4n, 1n)
    };
    for (let i = options["baseline.warmup.trigger"] + 1; i--;)
        check();
}

// Test multi-value returns.
var ins = wasmEvalText(`(module
  (memory (export "memory") 1 1)
  (func $rec (export "rec") (param i32 i32 i32 i32 i32 i32 i32) (result i32 i32 f32 f32)
    (local f32 i32)
    (if (result i32 i32 f32 f32) (i32.ge_u (local.get 0) (local.get 1))
      (then
        (local.get 5)
        (local.get 6)
        (local.tee 7 (f32.div (f32.convert_i32_u (local.get 3)) (f32.convert_i32_u (local.get 2))))
        (f32.sqrt
          (f32.sub
            (f32.div (f32.convert_i32_u (local.get 4)) (f32.convert_i32_u (local.get 2)))
            (f32.mul (local.get 7) (local.get 7))
          )
        )
      )
      (else
        (return_call $rec
          (i32.add (local.get 0) (i32.const 1))
          (local.get 1)
          (i32.add (local.get 2) (i32.const 1))
          (i32.add (local.get 3) (local.tee 8 (i32.load8_u (local.get 0))))
          (i32.add (local.get 4) (i32.mul (local.get 8) (local.get 8)))
          (if (result i32) (i32.gt_s (local.get 5) (local.get 8))
            (then (local.get 8)) (else (local.get 5))
          )
          (if (result i32) (i32.lt_s (local.get 6) (local.get 8))
            (then (local.get 8)) (else (local.get 6))
          )
        )
      )
    )
  )
  (func $main (export "main") (result i32 i32 f32 f32)
    (call $rec
      (i32.const 0)
      (i32.const 6)
      (i32.const 0)
      (i32.const 0)
      (i32.const 0)
      (i32.const 1000)
      (i32.const -1000)
    )
  )
  (data (i32.const 0) "\\02\\13\\22\\04\\08\\30")
)`);

const main = ins.exports["main"];
assertEq(""+ main(), "2,48,19.16666603088379,16.836633682250977");
assertEq("" + ins.exports.rec(1, 5, 0, 0, 0, 1000, -1000), "4,34,16.25,11.627016067504883");
check_stub3: {
    let options = getJitCompilerOptions();
    if (!options["baseline.enable"]) break check_stub3;
    const check = function() {
        ins.exports.rec(1, 5, 0, 0, 0, 1000, -1000);
    };
    for (let i = options["baseline.warmup.trigger"] + 1; i--;)
        check();
}

// Handling trap.
var ins = wasmEvalText(`(module
  (func $fac-acc (export "fac") (param i64 i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
    (then (unreachable))
    (else
        (return_call $fac-acc
            (i64.sub (local.get 0) (i64.const 1))
            (i64.mul (local.get 0) (local.get 1))
        )
    )
    )
  )
  (func (export "main") (param i64) (result i64)
    (call $fac-acc (local.get 0) (i64.const 1))
  )
)`);

assertErrorMessage(() => ins.exports.main(4n), WebAssembly.RuntimeError, /unreachable executed/);
assertErrorMessage(() => ins.exports.fac(3n, 1n), WebAssembly.RuntimeError, /unreachable executed/);

// Performance and stack growth: calculating sum of numbers 1..40000000
var ins = wasmEvalText(`(module
  (func $sum (param i32 i64) (result i64)
    local.get 0
    i32.eqz
    if
      local.get 1
      return
    else
      local.get 0
      i32.const 1
      i32.sub
      local.get 1
      local.get 0
      i64.extend_i32_s
      i64.add
      return_call $sum
    end
    unreachable
  )

  (func (export "main") (param i32) (result i64)
    local.get 0
    i64.const 0
    call $sum
  )
)`);

if (getBuildConfiguration("simulator")) {
  assertEq(ins.exports.main(400000), 80000200000n);
} else {
  assertEq(ins.exports.main(40000000), 800000020000000n);
}

// GC/externref shall not cling to the frame of a cross-instance `return_call`.
// The `return_call` caller will make a regular call because the callee is
// an import. The caller will create a GC object and will hold in its frame
// and a WeakMap.
// Test if the created object is in the WeakMap even after gc().
var wm = new WeakMap();
var ins = wasmEvalText(`(module
  (import "" "test" (func $test))
  (func $sum (param i32 i64) (result i64)
    local.get 0
    i32.eqz
    if
      call $test
      local.get 1
      return
    else
      local.get 0
      i32.const 1
      i32.sub
      local.get 1
      local.get 0
      i64.extend_i32_s
      i64.add
      return_call $sum
    end
    unreachable
  )
  (export "sum" (func $sum))
)`, {"": {
  test() {
    gc();
    assertEq(nondeterministicGetWeakMapKeys(wm).length, 0);
  }
}});

var ins2 = wasmEvalText(`(module
  (import "" "add_ref" (func $add_ref (result externref)))
  (import "" "use_ref" (func $use_ref (param externref)))
  (import "" "sum" (func $sum (param i32 i64) (result i64)))
  (global $g1 (mut i32) (i32.const 0))
  (func (export "main_gc") (param i32) (result i64)
    (local $ref externref)
    call $add_ref
    local.set $ref

    local.get $ref
    call $use_ref

    block
      global.get $g1
      br_if 0
      local.get 0
      i64.const 0
      return_call $sum
    end

    local.get $ref
    call $use_ref
    i64.const -1
  )
)`, {"": {
  sum: ins.exports.sum,
  add_ref() {
    const obj = {}; wm.set(obj, 'foo'); return obj;
  },
  use_ref(obj) {
    assertEq(nondeterministicGetWeakMapKeys(wm).length, 1);
  },
}});

assertEq(ins2.exports.main_gc(400000), 80000200000n);
assertEq(nondeterministicGetWeakMapKeys(wm).length, 0);
