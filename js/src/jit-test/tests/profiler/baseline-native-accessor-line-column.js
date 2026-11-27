// |jit-test| skip-if: !getJitCompilerOptions()['baseline.enable']
// Test that a Baseline JIT frame calling a native through an IC reports the
// line and column of the op making the call, not the location of an earlier
// call in the same frame or the start of the function.

setJitCompilerOption("baseline.warmup.trigger", 0);
setJitCompilerOption("ion.warmup.trigger", 1000);

const ITERATION_COUNT = 50;

function noop() {}

// The Error stack uses the frame's bytecode pc, so a scripted callee can
// compute the expected position of the op that called it.
function locationOfCaller() {
  return new Error().stack.split("\n")[2].match(/:(\d+:\d+)$/)[1];
}

// readGeckoProfilingStack is a native, so the IC calls it without pushing a JS
// frame and the Baseline frame's location comes from the last call site.
const nativeObj = {};
Object.defineProperty(nativeObj, "getterProp", {
  get: readGeckoProfilingStack,
});
const scriptedObj = {
  get getterProp() {
    return locationOfCaller();
  },
};
function scriptedCallee() {
  return locationOfCaller();
}

function getterNoPriorCall(o) {
  return o.getterProp;
}

function getterAfterCall(o) {
  noop();
  return o.getterProp;
}

function elemGetterAfterCall(o, key) {
  noop();
  return o[key];
}

// Call ops share the call-site instrumentation with the property accesses.
function nativeCallAfterCall(f) {
  noop();
  return f();
}

const cases = [
  { fn: getterNoPriorCall, scripted: scriptedObj, native: nativeObj },
  { fn: getterAfterCall, scripted: scriptedObj, native: nativeObj },
  { fn: elemGetterAfterCall, scripted: scriptedObj, native: nativeObj },
  {
    fn: nativeCallAfterCall,
    scripted: scriptedCallee,
    native: readGeckoProfilingStack,
  },
];

for (const { fn, scripted, native } of cases) {
  // Attach the same IC stubs from a copy of fn without the profiler first, so
  // shared stub code compiled without profiler instrumentation exists and must
  // not be reused. A copy keeps fn itself cold, so it gets Baseline JIT code
  // under the profiler.
  const fnCopy = eval(`(${fn})`);
  for (let i = 0; i < ITERATION_COUNT; i++) {
    fnCopy(scripted, "getterProp");
    fnCopy(native, "getterProp");
  }

  // Run under the profiler so that fn's Baseline code gets a profiler entry.
  enableGeckoProfiling();
  const expected = fn(scripted, "getterProp");
  const stacks = [];
  for (let i = 0; i < ITERATION_COUNT; i++) {
    stacks.push(fn(native, "getterProp"));
  }
  disableGeckoProfiling();

  let baselineFrames = 0;
  for (const stack of stacks) {
    for (const physicalFrame of stack) {
      for (const frame of physicalFrame) {
        if (
          frame.kind !== "baseline-jit" ||
          !frame.label ||
          !frame.label.startsWith(fn.name + " ")
        ) {
          continue;
        }
        baselineFrames++;
        assertEq(
          `${frame.line}:${frame.column}`,
          expected,
          `${fn.name} Baseline frame location`
        );
      }
    }
  }
  assertEq(baselineFrames > 0, true, `${fn.name} ran in Baseline`);
}
