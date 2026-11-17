load(libdir + "nans.js");

function testUnary(fn) {
  testWithFloatTypedArrays((FloatArray, UintArray, NaNs, UnaryMath) => {
    var f = new FloatArray(2);
    var u = new UintArray(f.buffer);

    for (var i = 0; i < 100; ++i) {
      u[0] = NaNs[i % NaNs.length];

      var x = UnaryMath(f[0]);

      // Store possibly tainted value.
      f[1] = x;

      assertEq(f[1], NaN);
      assertEq(x, NaN);
      assertSameNaNPayload(FloatArray, u[0], u[1], `Math.${UnaryMath.name}`);
    }
  }, fn);
}

testUnary(Math.abs);
testUnary(Math.acos);
testUnary(Math.asin);
testUnary(Math.atan);
testUnary(Math.ceil);
testUnary(Math.cos);
testUnary(Math.exp);
testUnary(Math.floor);
testUnary(Math.log);
testUnary(Math.round);
testUnary(Math.sin);
testUnary(Math.sqrt);
testUnary(Math.tan);
testUnary(Math.log10);
testUnary(Math.log2);
testUnary(Math.log1p);
testUnary(Math.expm1);
testUnary(Math.cosh);
testUnary(Math.sinh);
testUnary(Math.tanh);
testUnary(Math.acosh);
testUnary(Math.asinh);
testUnary(Math.atanh);
testUnary(Math.trunc);
testUnary(Math.sign);
testUnary(Math.cbrt);
testUnary(Math.hypot);
