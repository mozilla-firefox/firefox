// Wasm Ion loop unrolling (js/src/jit/UnrollLoops.cpp): stale memory
// dependency on loads placed after an unrolled+peeled loop.
//
// Run:    js --fuzzing-safe --wasm-compiler=optimized poc.js   -> FAIL lines
//         js --fuzzing-safe --wasm-compiler=baseline  poc.js   -> all PASS
// Also reproduces with default flags after warmup (lazy tiering to Ion),
// and with -P wasm_unroll_loops=false the optimized run becomes correct.
//
// The loop alternates a/b between two structs each iteration and stores an
// incrementing counter into the current struct's field, then immediately
// reloads it:
//     S: a.f = c        (store)
//     L: v = a.f        (load, dependency() == S)
// After the loop, the field of the *first* struct is reloaded:
//     A: r = s0.f       (load, dependency() == S as well: S is the last
//                        aliasing store in RPO order)
//
// UnrollAndOrPeelLoop clones the loop body (peel + unroll factor 3) and
// remaps dependency() fields only for the *cloned* instructions
// (UnrollLoops.cpp, "For cloned instructions that have a (load) dependency
// field ..."). The dependency of A, which lies outside the loop, is left
// pointing at the original store S, which after peeling is the store of the
// once-executed peeled iteration. The cloned aliasing stores S1..S3 that
// now execute between S and A are not reflected in A's dependency.
//
// Peeling collapses the copy-0 header phi for `a` to its initial value s0,
// so in the GVN rerun after unrolling (which runs even with --ion-gvn=off)
// the peeled-iteration load L0 (= load s0.f, dep S) becomes congruent with
// A (= load s0.f, dep S) and A is replaced by L0. The function then returns
// the value of the *first* iteration's store instead of the last aliasing
// store: stores from all cloned loop iterations are lost.

let inst = wasmEvalText(`(module
  (type $S (struct (field (mut i32))))
  (func (export "f") (param $n i32) (result i32)
    (local $a (ref null $S)) (local $b (ref null $S)) (local $t (ref null $S))
    (local $s0 (ref null $S))
    (local $i i32) (local $c i32) (local $sum i32) (local $v i32)
    (local.set $s0 (struct.new_default $S))
    (local.set $a (local.get $s0))
    (local.set $b (struct.new_default $S))
    (local.set $c (i32.const 100))
    (loop $l
      ;; S: a.f = c
      (struct.set $S 0 (local.get $a) (local.get $c))
      ;; L: v = a.f   (same-iteration reload; dependency = S)
      (local.set $v (struct.get $S 0 (local.get $a)))
      (local.set $sum (i32.add (local.get $sum) (local.get $v)))
      ;; swap a <-> b
      (local.set $t (local.get $a))
      (local.set $a (local.get $b))
      (local.set $b (local.get $t))
      (local.set $c (i32.add (local.get $c) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n)))
    )
    ;; A: reload s0.f after the loop (stale dependency after unrolling)
    (i32.add (struct.get $S 0 (local.get $s0))
             (i32.mul (local.get $sum) (i32.const 65536)))))`);

// Reference implementation of the wasm function above.
function ref(n) {
  let s0 = 0, s1 = 0, cur = 0, c = 100, sum = 0;
  for (let i = 0; i < n; i++) {
    if (cur === 0) s0 = c; else s1 = c;
    sum += (cur === 0 ? s0 : s1);
    cur ^= 1;
    c++;
  }
  return ((sum << 16) | s0) >>> 0;
}

let failures = 0;
for (let n = 1; n <= 10; n++) {
  let got = inst.exports.f(n) >>> 0;
  let want = ref(n);
  let ok = got === want;
  if (!ok) failures++;
}

assertEq(failures, 0);
