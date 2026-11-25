// |jit-test| --no-async-stacks; --no-source-pragmas; --disable-tosource; --ion-offthread-compile=off; --baseline-offthread-compile=on; --wasm-compiler=baseline; --no-incremental-gc; --no-cgc; --no-parallel-marking; --gc-param=minEmptyChunkCount=7; --gc-param=allocationThreshold=42; --gc-param=lowFrequencyHeapGrowth=250; --gc-param=highFrequencyTimeLimit=1234; --gc-param=sliceTimeBudgetMS=9; --gc-param=smallHeapSizeMax=300; skip-if: helperThreadCount() === 0

// Check that command line options given to the shell are also respected by
// workers, by comparing what a worker observes with what the main thread
// observes. The options on the |jit-test| line above are all non-default, so
// any option that is not propagated shows up as a difference. The test reports
// all of the settings that differ.

// Each probe is an expression that evaluates to a boolean or a uint32. The
// helpers it uses are defined in |prelude|, which is evaluated in both places.
const prelude = `
  function hasAsyncStacks() {
    var result;
    async function inner() { await null; return new Error().stack; }
    async function outer() { return await inner(); }
    outer().then(s => { result = s; });
    drainJobQueue();
    return result.includes("async*");
  }
  function hasSourcePragmas() {
    return evaluate("//# sourceURL=pragmaTest\\nnew Error().stack")
        .includes("pragmaTest");
  }
`;

const probes = [
  "hasAsyncStacks()",
  "hasSourcePragmas()",
  "typeof Object.prototype.toSource === 'function'",
  "helperThreadCount()",
  "gcparam('minEmptyChunkCount')",
  "gcparam('maxBytes')",
  "gcparam('perZoneGCEnabled')",
  "gcparam('allocationThreshold')",
  "gcparam('lowFrequencyHeapGrowth')",
  "gcparam('highFrequencyTimeLimit')",
  "gcparam('sliceTimeBudgetMS')",
  "gcparam('smallHeapSizeMax')",
  "gcparam('incrementalGCEnabled')",
  "gcparam('parallelMarkingEnabled')",
  "wasmCompileMode() === 'baseline'",
  "getJitCompilerOptions()['offthread-compilation.enable']",
  "getJitCompilerOptions()['wasm.baseline']",
  "getJitCompilerOptions()['wasm.optimizing']",
];

const runProbesSource = `
  function runProbes(probes, results) {
    for (let i = 0; i < probes.length; i++) {
      results[i] = Number(eval(probes[i]));
    }
  }
`;

const STATE_RUNNING = 0;
const STATE_DONE = 1;
const STATE_FAILED = 2;

const sab = new SharedArrayBuffer(8 + probes.length * 4);
const state = new Int32Array(sab, 0, 1);
const workerResults = new Uint32Array(sab, 8, probes.length);
setSharedObject(sab);

evalInWorker(`
  ${prelude}
  ${runProbesSource}
  const sab = getSharedObject();
  const state = new Int32Array(sab, 0, 1);
  let result = ${STATE_DONE};
  try {
    runProbes(${JSON.stringify(probes)}, new Uint32Array(sab, 8));
  } catch (e) {
    print("Worker failed to run probes: " + e);
    result = ${STATE_FAILED};
  }
  Atomics.store(state, 0, result);
  Atomics.notify(state, 0);
`);

const mainResults = new Uint32Array(probes.length);
(0, eval)(prelude + runProbesSource);
runProbes(probes, mainResults);

const timeoutMS = 60 * 1000;
const start = Date.now();
while (Atomics.load(state, 0) === STATE_RUNNING &&
       Date.now() - start < timeoutMS) {
  Atomics.wait(state, 0, STATE_RUNNING, 10);
}
assertEq(Atomics.load(state, 0), STATE_DONE,
         "Worker failed or timed out running probes");

const mismatches = [];
for (let i = 0; i < probes.length; i++) {
  if (mainResults[i] !== workerResults[i]) {
    mismatches.push(`probe ${i} (${probes[i]}): ` +
                    `main=${mainResults[i]} worker=${workerResults[i]}`);
  }
}

assertEq(mismatches.length, 0,
         "Options not respected by workers:\n  " + mismatches.join("\n  "));
