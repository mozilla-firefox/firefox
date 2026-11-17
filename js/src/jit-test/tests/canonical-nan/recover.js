// |jit-test| --ion-limit-script-size=off

// See ion/dce-with-instructions.js for the setup to test recover instructions.

setJitCompilerOption("baseline.warmup.trigger", 9);
setJitCompilerOption("ion.warmup.trigger", 20);

// Prevent GC from cancelling/discarding Ion compilations.
gczeal(0);

load(libdir + "nans.js");

testWithFloatTypedArrays((FloatArray, UintArray, NaNs) => {
  var f = new FloatArray(1);
  var u = new UintArray(f.buffer);

  // Write a NaN value (with a payload).
  u[0] = NaNs[0];

  var uceFault = function (i) {
    if (i > 98)
      uceFault = function (i) { return true; };
    return false;
  }

  var uceFault_sqrt = eval(`(${uceFault})`.replace("uceFault", "uceFault_sqrt"));
  function rsqrt_number(f, i) {
    var x = Math.sqrt(f[0]);
    if (uceFault_sqrt(i) || uceFault_sqrt(i)) {
      assertEq(x, NaN);
    }
    assertRecoveredOnBailout(x, true);
  }

  var max = 200;

  for (var j = 100 - max; j < 100; j++) {
    with ({}) ; // Do not Ion-compile this loop.

    var i = j < 2 ? (Math.abs(j) % 50) + 2 : j;
    rsqrt_number(f, i);
  }
});
