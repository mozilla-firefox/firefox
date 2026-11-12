// |jit-test| --blinterp-warmup-threshold=1000; --baseline-warmup-threshold=1000
// An eager baseline hint must not apply to a different script that has the
// same filename and start offset.

if (typeof getJitCompilerOptions == "function" &&
    typeof inJit == "function" &&
    typeof setBaselineHint == "function" &&
    typeof hasBaselineHint == "function") {

  var jco = getJitCompilerOptions();
  if (jco["baseline.enable"] && jco["blinterp.enable"] &&
    jco["blinterp.warmup.trigger"] == 1000 &&
    jco["baseline.warmup.trigger"] == 1000) {

    var opts = {fileName: "baseline-hints-3-shared.js"};
    var src = "function testFunction() { return inJit(); }";

    var g1 = newGlobal();
    g1.evaluate(src, opts);
    setBaselineHint(g1.testFunction);
    assertEq(hasBaselineHint(g1.testFunction), true);

    // Different length, so a different script.
    var g2 = newGlobal();
    g2.evaluate(src.replace("inJit();", "inJit();  "), opts);
    assertEq(hasBaselineHint(g2.testFunction), false);
    assertEq(g2.testFunction(), false);

    // Identical source still gets the hint.
    var g3 = newGlobal();
    g3.evaluate(src, opts);
    assertEq(hasBaselineHint(g3.testFunction), true);
    assertEq(g3.testFunction(), true);
  }
}
