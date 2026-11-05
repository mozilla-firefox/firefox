// |jit-test| --emit-interpreter-entry

// Exercises the case where the JitScript for a script is created early,
// by eager Baseline compilation from a JIT hint, so the JitScript already
// exists by the time we first attempt to run it in the Baseline interpreter.
//
// This test runs with --emit-interpreter-entry, and we have asserts which
// check that we've created the interpreter trampoline.

function leaf(x) {
  return x + 1;
}
function middle(x) {
  return leaf(x) + leaf(x + 1);
}
function outer(n) {
  var sum = 0;
  for (var i = 0; i < n; i++) {
    sum += middle(i);
  }
  return sum;
}

setBaselineHint(leaf);
setBaselineHint(middle);
setBaselineHint(outer);

for (var i = 0; i < 20; i++) {
  assertEq(outer(100), 10200);
  gc();
}
