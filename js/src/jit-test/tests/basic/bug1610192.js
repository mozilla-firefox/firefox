// |jit-test| slow; skip-if: getBuildConfiguration("debug")

try {
  var s = '';
  s += new Uint8Array(2 ** 23 + 2);
  eval("[" + s + "1]");
} catch (e) {
  assertEq(e, "out of memory");
}
