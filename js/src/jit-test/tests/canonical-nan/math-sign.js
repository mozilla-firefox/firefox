load(libdir + "nans.js");

// Math.sign with Float input, but specialized to return Int32.
testWithFloatTypedArrays(FloatArray => {
  // Int32 specialization for Math.sign is possible if the input contains
  // neither NaN nor -0.
  var f = new FloatArray([
    -Infinity,
    -1,
    -0.5,
    0,
    0.5,
    1,
    Infinity,
  ]);

  for (var i = 0; i < 100; ++i) {
    var x = Math.sign(f[i % f.length]);
    if (x < 0) {
      assertEq(x, -1);
    } else if (x > 0) {
      assertEq(x, +1);
    } else {
      assertEq(x, 0);
    }
  }
});
