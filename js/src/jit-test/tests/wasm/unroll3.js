// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test: 1 exiting value, 1 exit target => can be unrolled.
// The value defined in the loop, which is used after,
// is also phi'd with a value defined before the loop.

let t = `
(module
  (func (export "f1") (param $enterloopP i32) (param $limit i32) (result i32)
    (local $merge i32)
    (local $x i32)
    (local.set $merge (i32.const 5000))
    (local.set $x (i32.const 1))
    (if (i32.ne (local.get $enterloopP) (i32.const 0))
      (then
        (loop $cont
          ;; x = x + 1
          (local.set $x (i32.add (local.get $x) (i32.const 1)))
          ;; merge = x
          (local.set $merge (i32.add (local.get $x) (i32.const 1111)))
          ;; continue if x < limit
          (br_if $cont (i32.lt_u (local.get $x) (local.get $limit)))
        )
      )
    )
    (i32.mul (local.get $merge) (i32.const 17))
  )
)`;

let i = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

assertEq(i.exports.f1(0, 100), 85000); // loop not entered
assertEq(i.exports.f1(1, 100), 20587) // loop entered
