load(libdir + "nans.js");

function testBinary(fn) {
  testWithFloatTypedArrays((FloatArray, UintArray, NaNs, BinaryMath) => {
    var f = new FloatArray(2);
    var u = new UintArray(f.buffer);

    for (var i = 0; i < 100; ++i) {
      u[0] = NaNs[i % NaNs.length];

      var x = BinaryMath(f[0], f[0]);

      // Store possibly tainted value.
      f[1] = x;

      assertEq(f[1], NaN);
      assertEq(x, NaN);
      assertSameNaNPayload(FloatArray, u[0], u[1]);
    }

    for (var i = 0; i < 100; ++i) {
      u[0] = NaNs[i % NaNs.length];

      // With constant non-NaN operand.
      var x = BinaryMath(0, f[0]);

      // Store possibly tainted value.
      f[1] = x;

      assertEq(f[1], NaN);
      assertEq(x, NaN);
      assertSameNaNPayload(FloatArray, u[0], u[1]);
    }

    for (var i = 0; i < 100; ++i) {
      u[0] = NaNs[i % NaNs.length];

      // With constant NaN operand.
      var x = BinaryMath(NaN, f[0]);

      // Store possibly tainted value.
      f[1] = x;

      assertEq(f[1], NaN);
      assertEq(x, NaN);
      assertSameNaNPayload(FloatArray, u[0], u[1]);
    }
  }, fn);
}

testBinary(Math.max);
testBinary(Math.min);
testBinary(Math.pow);
testBinary(Math.atan2);
testBinary(Math.hypot);
