// PoC for stale alias-analysis dependency() surviving wasm loop unrolling
// (js/src/jit/UnrollLoops.cpp), letting the post-unroll GVN run merge a
// reload of a mutable global across the cloned aliasing stores.
//
// Pipeline walkthrough:
//  1. Alias analysis: the load L (global.get $g) picks the *last aliasing
//     store* as its dependency. The branch arm containing S1
//     (global.set $g 7777) precedes L, so dep(L) = S1.
//  2. Range analysis proves (i & 7) >= 8 is always false (beta node with
//     empty range), marks the arm unreachable, and prepareForUCE rewrites
//     the branch condition to a constant.
//  3. The UCE GVN run executes with DontUpdateAliasAnalysis: it folds the
//     test, removes the arm block and DISCARDS S1. dep(L) now dangles, and
//     because this GVN run must not update alias analysis, the dangling
//     pointer is deliberately kept ("it's still valid for the purposes of
//     detecting congruent loads", ValueNumbering.cpp).
//  4. UnrollLoops clones the loop body (peel + unroll x3 by default). Its
//     dependency fixup (UnrollLoops.cpp:1170-1188) remaps a clone's
//     dependency only if valueTable.findInRow(0, originalDep) finds the
//     dependency among the *current* instructions of the original body.
//     S1 was discarded, so the lookup fails and EVERY copy of L keeps the
//     SAME stale dependency pointer.
//  5. The post-unroll GVN run (also DontUpdateAliasAnalysis) now sees the
//     four copies of L as congruent (same operand, identical dependency())
//     and replaces the loop copies with the peeled copy -- even though a
//     cloned aliasing store S2 (global.set $g i) executes between them.
//
// Result: with the Ion/optimized wasm compiler (loop unrolling on by
// default) every iteration accumulates the stale value of $g instead of
// reloading it.

const wat = `(module
  (global $g (export "g") (mut i32) (i32.const 1000))
  (func (export "f") (param $n i32) (param $x i32) (result i32)
    (local $i i32)
    (local $acc i32)
    (loop $cont
      ;; Dead-at-runtime store; provably dead only via range analysis
      ;; ((x % 7) is in [0,6], so >= 8 is always false; i32.rem_u maps to
      ;; MMod which is range-aware, unlike the Wasm-specific bitwise ops).
      ;; It becomes the alias-analysis dependency of the global.get below.
      (if (i32.ge_s (i32.rem_u (local.get $x) (i32.const 7)) (i32.const 8))
        (then (global.set $g (i32.const 7777)))
      )
      ;; L: reload of $g -- dep = S1 (the store above)
      (local.set $acc (i32.add (local.get $acc) (global.get $g)))
      ;; S2: aliasing store after L
      (global.set $g (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $cont (i32.lt_s (local.get $i) (local.get $n)))
    )
    (local.get $acc)
  )
)`;

const inst = wasmEvalText(wat);

const actual = inst.exports.f(10, 3);

// Reference (interpreter semantics):
//   iter 0: acc += 1000, g = 0
//   iter k: acc += k-1,  g = k     (k = 1..9)
//   acc = 1000 + (0+1+...+8) = 1036
const expected = 1036;

assertEq(actual, expected);
