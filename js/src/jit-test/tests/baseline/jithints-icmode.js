// |jit-test| --blinterp-warmup-threshold=1; --baseline-warmup-threshold=1; --ion-offthread-compile=off

// Test that IC mode hints are lazily applied after the first stub is attached.
// When a fallback is triggered and the IC already has at least one stub, the
// hint should transition the IC to Megamorphic without waiting for the normal
// stub count threshold.

if (typeof recordIonCompilationForHints !== "function" ||
    typeof hasMegamorphicIC !== "function" ||
    typeof resetFallbackStubStates !== "function" ||
    typeof baselineCompile !== "function") {
  quit(0);
}

// Disable GC zeal to prevent GC from sweeping IC stubs during warmup.
if (typeof gczeal === "function") {
  gczeal(0);
}

var jco = getJitCompilerOptions();
if (!jco["baseline.enable"] || !jco["blinterp.enable"]) {
  quit(0);
}

// Skip when Ion is eager: ICs won't accumulate enough stubs to reach
// Megamorphic.
if (jco["ion.enable"] && jco["ion.warmup.trigger"] == 0) {
  quit(0);
}

function testIC(x) {
  return x.val;
}

// Drive the property access IC to Megamorphic with different object shapes.
for (var i = 0; i < 10; i++) {
  var obj = Object.create(null);
  for (var j = 0; j < i; j++) obj["pre" + j] = j;
  obj.val = i;
  testIC(obj);
}

assertEq(hasMegamorphicIC(testIC), true);

// Record IC mode hints (captures Megamorphic mode for this IC site).
recordIonCompilationForHints(testIC);

// Reset IC states to Specialized so we can observe the lazy hint mechanism.
resetFallbackStubStates(testIC);
assertEq(hasMegamorphicIC(testIC), false);

// Recompile to Baseline to start with fresh, empty ICs.
baselineCompile(testIC);
assertEq(hasMegamorphicIC(testIC), false);

// First call: the IC is empty (newStubIsFirstStub() is true), so the lazy
// hint doesn't fire. A specialized stub is attached instead.
testIC({val: 1});
assertEq(hasMegamorphicIC(testIC), false);

// Second call with a different shape: the fallback is triggered and the IC
// already has a stub, so the lazy hint fires and transitions to Megamorphic.
testIC({pre0: 0, val: 2});
assertEq(hasMegamorphicIC(testIC), true);
