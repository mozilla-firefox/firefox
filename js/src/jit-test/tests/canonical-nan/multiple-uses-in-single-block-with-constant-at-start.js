// |jit-test| --fast-warmup

load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(1);
  var u = new UintArray(f.buffer);

  function g(f, out) {
    var x = f[0];
    out.p = (Number.isNaN(x), x);
    return out.p;
  }

  var out = {p: 0};
  for (var i = 0; i < 2000; ++i) {
    u[0] = NaNs[i % NaNs.length];

    var x = g(f, out);
    assertEq(x, NaN);
    assertEq(out.p, NaN);
  }
});
