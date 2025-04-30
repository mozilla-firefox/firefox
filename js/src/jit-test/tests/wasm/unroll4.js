// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test: 2 exiting values, 1 exit target => can be unrolled.
// `a` and `b` are swapped every iteration and correct unrolling depends on
// the phi nodes being treated as parallel assignments.

let t = `
(module
  (func (export "f1") (param $limit i32) (result i64)
    (local $a i64)
    (local $b i64)
    (local $tmp i64)
    (local $x i32)
    (local.set $a (i64.const 12345))
    (local.set $b (i64.const 67890))
    (loop $cont
       ;; swap a and b
       (local.set $tmp (local.get $a))
       (local.set $a (local.get $b))
       (local.set $b (local.get $tmp))
       ;; x = x + 1
       (local.set $x (i32.add (local.get $x) (i32.const 1)))
       ;;
       (br_if $cont (i32.lt_u (local.get $x) (local.get $limit)))
    )
    (i64.sub (local.get $a) (local.get $b))
  )
)`;

let i = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

assertEq(i.exports.f1(100), -55545n);
assertEq(i.exports.f1(101), 55545n);
