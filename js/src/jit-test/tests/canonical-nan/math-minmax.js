load(libdir + "nans.js");

// MMinMax::foldsTo, case 1:
// --------------------------------------------------
// Try to fold the following patterns when |x| and |y| are constants.
//
// min(min(x, z), min(y, z)) = min(min(x, y), z)
// max(max(x, z), max(y, z)) = max(max(x, y), z)
// max(min(x, z), min(y, z)) = min(max(x, y), z)
// min(max(x, z), max(y, z)) = max(min(x, y), z)
// --------------------------------------------------
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // min(min(x, z), min(y, z)) = min(min(x, y), z)
    var x = Math.min(Math.min(0, f[0]), Math.min(1, f[0]));

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});

// MMinMax::foldsTo, case 2:
// --------------------------------------------------
// Fold min/max operations with same inputs.
//
// min(x, min(x, y)) = min(x, y)
// max(x, max(x, y)) = max(x, y)
// --------------------------------------------------
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // max(x, max(x, y)) = max(x, y)
    var x = Math.max(f[0], Math.max(f[0], 0));

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});

// MMinMax::foldsTo, case 3:
// --------------------------------------------------
// Fold min(x, min(y, z)) to min(min(x, y), z) with constant min(x, y).
// Fold max(x, max(y, z)) to max(max(x, y), z) with constant max(x, y).
// --------------------------------------------------
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // min(x, min(y, z)) = min(min(x, y), z)
    var x = Math.min(0, Math.min(1, f[0]));

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});

// MMinMax::foldsTo, case 4:
// --------------------------------------------------
// Fold min(x, max(y, z)) to max(min(x, y), min(x, z)).
// Fold max(x, min(y, z)) to min(max(x, y), max(x, z)).
//
// But only do this when min(x, z) can also be simplified.
// --------------------------------------------------
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // max(x, min(y, z)) to min(max(x, y), max(x, z))
    var x = Math.max(0, Math.min(1, f[0]));

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});
