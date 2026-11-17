load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // Float addition with a NaN constant. GVN will constant fold the result
    // to compile-time constant NaN.
    var x = f[0] + NaN;

    // Never a tainted value, except when GVN is disabled.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
  }
});
