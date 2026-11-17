load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f1 = new FloatArray(1);
  var f2 = new FloatArray(1);
  var u1 = new UintArray(f1.buffer);
  var u2 = new UintArray(f2.buffer);

  for (var i = 0; i < 100; ++i) {
    u1[0] = NaNs[i % NaNs.length];
    u2[0] = NaNs[(i + 1) % NaNs.length];

    // Load tainted floating point into |v|.
    var v = (i & 1) ? f1[0] : f2[0];

    assertEq(v, NaN);
    assertEq(Number.isNaN(v), true);
  }
});

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f1 = new FloatArray(1);
  var f2 = new FloatArray(1);
  var u1 = new UintArray(f1.buffer);
  var u2 = new UintArray(f2.buffer);

  for (var i = 0; i < 100; ++i) {
    u1[0] = NaNs[i % NaNs.length];
    u2[0] = NaNs[(i + 1) % NaNs.length];

    // Load tainted floating point into |v|.
    var v = (i & 1) ? f1[0] : f2[0];

    // Apply binary operator on |v|. The result |w| must be marked as tainted.
    var w = v * 2;

    assertEq(v, NaN);
    assertEq(Number.isNaN(v), true);

    assertEq(w, NaN);
    assertEq(Number.isNaN(w), true);
  }
});

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f1 = new FloatArray(1);
  var f2 = new FloatArray(1);
  var u1 = new UintArray(f1.buffer);
  var u2 = new UintArray(f2.buffer);

  for (var i = 0; i < 100; ++i) {
    u1[0] = NaNs[i % NaNs.length];
    u2[0] = NaNs[(i + 1) % NaNs.length];

    // Load tainted floating point into |v|.
    var v = (i & 1) ? f1[0] : f2[0];

    // Call inlinable function on |v|. The result |w| must be marked as tainted.
    var w = v * 2;

    assertEq(v, NaN);
    assertEq(Number.isNaN(v), true);

    assertEq(w, NaN);
    assertEq(Number.isNaN(w), true);
  }
});
