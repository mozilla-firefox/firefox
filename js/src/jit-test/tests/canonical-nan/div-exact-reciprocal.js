load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // Float division by a constant.
    var x = f[0] / 2;

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});
