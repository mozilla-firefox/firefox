load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(1);
  var u = new UintArray(f.buffer);

  // Write a NaN value (with a payload).
  u[0] = NaNs[0];

  function test(ta, c) {
    // Load from typed array.
    var y = ta[0];

    // Array with scalar replacement.
    var r = [y];

    if (c) {
      // Load from scalar replaced array.
      assertEq(r[0], NaN);
    }
  }

  // Don't inline |test|.
  with ({});

  for (var i = 0; i <= 100; i++) {
    test(f, i == 100);
  }
});
