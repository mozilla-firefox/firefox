// |jit-test| skip-if: !getJitCompilerOptions()['baseline.enable']
// Test that a Baseline JIT frame whose callee is a scripted getter reports the
// position of the property access. The frame is resolved from the IC's return
// address, which can be the first instruction of the next op.

setJitCompilerOption("baseline.warmup.trigger", 0);
setJitCompilerOption("ion.warmup.trigger", 1000);

const ITERATION_COUNT = 50;

const results = [];
const obj = {
  get scriptedProp() {
    // The Error stack uses the frame's bytecode pc, so it gives the expected
    // position of the caller's property access.
    const callerFrame = new Error().stack.split("\n")[1];
    const callerLocation = callerFrame.match(/:(\d+:\d+)$/)[1];
    results.push({ stack: readGeckoProfilingStack(), callerLocation });
    return 1;
  },
  other: 2,
};

function returnAddressCaller(o) {
  let v = o.scriptedProp / o.other;
  return v;
}

enableGeckoProfiling();
for (let i = 0; i < ITERATION_COUNT; i++) {
  returnAddressCaller(obj);
}
disableGeckoProfiling();

let baselineFrames = 0;
for (const { stack, callerLocation } of results) {
  for (const physicalFrame of stack) {
    for (const frame of physicalFrame) {
      if (
        frame.kind !== "baseline-jit" ||
        !frame.label ||
        !frame.label.startsWith("returnAddressCaller ")
      ) {
        continue;
      }
      baselineFrames++;
      assertEq(
        `${frame.line}:${frame.column}`,
        callerLocation,
        "returnAddressCaller Baseline frame location"
      );
    }
  }
}
assertEq(baselineFrames > 0, true, "returnAddressCaller ran in Baseline");
