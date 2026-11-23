// Tests if the profiler (frame iterator) can unwind in the middle
// of collapse frame instructions.

enableGeckoProfiling();
try {
    enableSingleStepProfiling();
} catch (e) {
    // continue anyway if single step profiling is not supported
}

var ins = wasmEvalText(`
(module
    (func $f (param i64 i64 i64 i64 i64 i64 i64 i64 i64)
        local.get 0
        i64.eqz
        br_if 0
        local.get 0
        return_call $g
    )
    (func $g (param i64)
        local.get 0
        i64.const 1
        i64.sub
        i64.const 2
        i64.const 6
        i64.const 3
        i64.const 4
        i64.const 1
        i64.const 2
        i64.const 6
        i64.const 3
        return_call $f
    )
    (func (export "run") (param i64)
        local.get 0
        call $g
    )
)`);

for (var i = 0; i < 10; i++) {
    ins.exports.run(100n);
}

// Also when a cross-instance return call is made as a regular call.
var ins0 = wasmEvalText(`(module (func (export "t")))`);
var ins = wasmEvalText(`
(module
    (import "" "t" (func $g))
    (func $f (return_call_indirect $t (i32.const 0)))
    (table $t 1 1 funcref)
  
    (func (export "run") (param i64)
     loop
       local.get 0
       i64.eqz
       br_if 1
       call $f
       local.get 0
       i64.const 1
       i64.sub
       local.set 0
       br 0
     end
    )
    (elem (i32.const 0) $g)
)`, {"": {t: ins0.exports.t},});

ins.exports.run(10n);
