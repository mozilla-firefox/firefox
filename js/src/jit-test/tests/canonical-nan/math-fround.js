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

      // We can't assert same NaN payload, because rounding modifies the payload.
    }
  }, fn);
}

testUnary(Math.fround);
testUnary(Math.f16round);
