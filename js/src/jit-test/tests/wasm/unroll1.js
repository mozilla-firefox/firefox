// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test: 2 exiting values, 1 exit target => can be unrolled.

let t = `
(module
  (func (export "f1") (param $limit i32) (result i32)
    (local $x i32)
    (local $y i32)
    (local.set $x (i32.const 1))
    (local.set $y (i32.const 1000))
    (loop $cont
       ;; x = x + 1
       (local.set $x (i32.add (local.get $x) (i32.const 1)))
       ;; y = y - 7
       (local.set $y (i32.sub (local.get $x) (i32.const 7)))
       ;;
       (br_if $cont (i32.lt_u (local.get $x) (local.get $limit)))
    )
    (i32.mul (local.get $x) (local.get $y))
  )
)`;

let i = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

// Run the loop some varying number of times, with the aim of exiting it at
// each copy of the loop, so as to check that the post-loop values of `x` and
// `y` are correct, regardless of which copy of the original loop exited.

assertEq(i.exports.f1(100), 9300);
assertEq(i.exports.f1(101), 9494);
assertEq(i.exports.f1(102), 9690);
assertEq(i.exports.f1(103), 9888);
assertEq(i.exports.f1(104), 10088);
assertEq(i.exports.f1(105), 10290);
assertEq(i.exports.f1(106), 10494);
assertEq(i.exports.f1(107), 10700);
assertEq(i.exports.f1(108), 10908);
assertEq(i.exports.f1(109), 11118);
