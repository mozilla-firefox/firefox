load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // Copy NaN value, possibly include a NaN payload.
    f[1] = f[0];

    // If the NaN payload was copied, then it must be the same.
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});
