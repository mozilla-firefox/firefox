load(libdir + "nans.js");

// Math.hypot with 3 arguments.
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    var x = Math.hypot(f[0], f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // With constant non-NaN operand.
    var x = Math.hypot(0, f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // With constant NaN operand.
    var x = Math.hypot(NaN, f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});

// Math.hypot with 4 arguments.
testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(2);
  var u = new UintArray(f.buffer);

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    var x = Math.hypot(f[0], f[0], f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // With constant non-NaN operand.
    var x = Math.hypot(0, f[0], f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }

  for (var i = 0; i < 100; ++i) {
    u[0] = NaNs[i % NaNs.length];

    // With constant NaN operand.
    var x = Math.hypot(NaN, f[0], f[0], f[0]);

    // Store possibly tainted value.
    f[1] = x;

    assertEq(f[1], NaN);
    assertEq(x, NaN);
    assertSameNaNPayload(FloatArray, u[0], u[1]);
  }
});
