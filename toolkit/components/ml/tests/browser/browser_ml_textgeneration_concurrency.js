/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global TextGenerator */

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

// The concurrency contracts of TextGenerator, all enforced in the parent: one
// generate() per generator at a time, clear() only between generations, and
// generators independent of each other. They share one browser HWInference
// process, where each runs on a thread of its own, so two generations run in
// parallel and contend for the CPU: each backend has its own threadpool, so
// callers sizing them for the whole machine oversubscribe it.

// Still running at every check that follows its first delta: one-token
// chunks and no stop on end of text. Chaos mode slows each token's round
// trip by an order of magnitude, so the budget stays small.
const LONG_REQUEST = {
  messages: TINYSTORIES_STORYTELLER_PROMPT,
  maxTokens: 256,
  bufferLength: 1,
  samplers: TINYSTORIES_GREEDY_SAMPLERS,
  stopOnEndOfGenerationTokens: false,
};

const SHORT_REQUEST = {
  messages: TINYSTORIES_STORYTELLER_PROMPT,
  maxTokens: 16,
  bufferLength: 1,
  samplers: TINYSTORIES_GREEDY_SAMPLERS,
};

async function hwInferencePids() {
  const procInfo = await ChromeUtils.requestProcInfo();
  return procInfo.children
    .filter(
      child =>
        child.type.startsWith("utility") &&
        child.utilityActors.some(actor => actor.actorName === "hwInference")
    )
    .map(child => child.pid);
}

async function waitForProcessExit() {
  await TestUtils.waitForCondition(
    async () => !(await hwInferencePids()).length,
    "The HWInference process exits with its last generator"
  );
}

/**
 * Starts a generation and exposes its progress: `firstDelta` resolves once
 * the generation is known to be running, `done` is the generate() promise,
 * and `settled` flips as soon as it settles.
 *
 * @param {TextGenerator} generator
 * @param {object} request
 * @param {?Array} log - When given, every delta is appended as { run, text }.
 */
function startGeneration(generator, request, log = null) {
  const run = { streamed: "", settled: false };
  let onFirstDelta;
  run.firstDelta = new Promise(resolve => {
    onFirstDelta = resolve;
  });
  run.done = generator.generate(request, text => {
    run.streamed += text;
    log?.push({ run, text });
    onFirstDelta();
  });
  const settle = () => {
    run.settled = true;
  };
  run.done.then(settle, settle);
  return run;
}

function events(name) {
  return Glean.firefoxAiRuntime[name].testGetValue() ?? [];
}

add_task(async function test_generate_overlap_rejects() {
  const generator = await createTinyStoriesGenerator();
  try {
    const first = generator.generate(LONG_REQUEST);
    await Assert.rejects(
      generator.generate(SHORT_REQUEST),
      err => err.name === "InvalidStateError" && /in flight/.test(err.message),
      "A second generate() while one is pending rejects with InvalidStateError"
    );

    const result = await first;
    Assert.greater(
      result.content.length,
      0,
      "The first generation still resolves normally after the rejected overlap"
    );
    Assert.equal(
      result.reason,
      "length",
      "The first generation ran to its budget"
    );
  } finally {
    generator.terminate();
  }
});

add_task(async function test_clear_during_generate_rejects() {
  const generator = await createTinyStoriesGenerator();
  try {
    const run = startGeneration(generator, LONG_REQUEST);
    await run.firstDelta;
    Assert.throws(
      () => generator.clear(),
      err => err.name === "InvalidStateError",
      "clear() while a generate() is pending throws InvalidStateError"
    );

    const result = await run.done;
    Assert.notEqual(
      result.reason,
      "cancelled",
      "The rejected clear() did not disturb the generation"
    );
    Assert.equal(result.content, run.streamed, "The streamed deltas match");

    generator.clear();
    const afterClear = await generator.generate(SHORT_REQUEST);
    Assert.equal(
      afterClear.usage.promptTokens,
      result.usage.promptTokens,
      "clear() between generations empties the history: the next prompt is " +
        "the first one's size again"
    );
  } finally {
    generator.terminate();
  }
});

add_task(async function test_cancel_is_scoped_to_its_generation() {
  const generator = await createTinyStoriesGenerator();
  try {
    // Cancelled as soon as it is sent: waiting for a delta lets a lagging
    // utility main thread finish a short budget before the cancel lands.
    const pending = generator.generate({ ...LONG_REQUEST, maxTokens: 4000 });
    generator.cancel();
    const cancelled = await pending;
    Assert.equal(cancelled.reason, "cancelled", "The running generation ended");

    const next = await generator.generate(SHORT_REQUEST);
    Assert.notEqual(
      next.reason,
      "cancelled",
      "The earlier cancel() does not apply to the next generate()"
    );
    Assert.greater(next.usage.generatedTokens, 0, "The next generation ran");
  } finally {
    generator.terminate();
  }
});

add_task(async function test_concurrent_creates_share_one_process() {
  await waitForProcessExit();
  const [a, b] = await Promise.all([
    createTinyStoriesGenerator(),
    createTinyStoriesGenerator(),
  ]);
  try {
    const pids = await hwInferencePids();
    Assert.equal(pids.length, 1, "Racing creates share one process");

    a.terminate();
    const result = await b.generate(SHORT_REQUEST);
    Assert.greater(result.content.length, 0, "The survivor still generates");
    Assert.deepEqual(
      await hwInferencePids(),
      pids,
      "The surviving generator keeps the process"
    );
  } finally {
    a.terminate();
    b.terminate();
  }
  await waitForProcessExit();
});

add_task(async function test_sibling_terminated_mid_generation() {
  const doomed = await createTinyStoriesGenerator();
  const survivor = await createTinyStoriesGenerator();
  try {
    const run = startGeneration(survivor, LONG_REQUEST);
    await run.firstDelta;
    Assert.ok(!run.settled, "The survivor is still generating");

    doomed.terminate();

    const result = await run.done;
    Assert.notEqual(
      result.reason,
      "cancelled",
      "Terminating a sibling does not cancel this generation"
    );
    Assert.equal(result.content, run.streamed, "The streamed deltas match");
  } finally {
    doomed.terminate();
    survivor.terminate();
  }
});

// Avoid oversubscription by sizing the thread counts for each generator to
// half of the core count. On Windows in CI, oversubscription was measured to
// lead to timeouts.
const CONCURRENT_THREADS = {
  numThreads: Math.max(1, Math.floor(navigator.hardwareConcurrency / 2)),
  ...TINYSTORIES_CHAOS_THREADS,
};

add_task(async function test_generators_use_separate_threads() {
  const a = await createTinyStoriesGenerator(CONCURRENT_THREADS);
  const b = await createTinyStoriesGenerator(CONCURRENT_THREADS);
  await ProfilerTestUtils.startProfiler({ threads: ["TextGenerator"] });
  try {
    const [resultA, resultB] = await Promise.all([
      a.generate(SHORT_REQUEST),
      b.generate(SHORT_REQUEST),
    ]);
    Assert.greater(resultA.usage.generatedTokens, 0, "a generated");
    Assert.greater(resultB.usage.generatedTokens, 0, "b generated");

    const profile = await ProfilerTestUtils.stopNowAndGetProfile();
    const generationThreads = new Set();
    function collectThreads(process) {
      for (const thread of process.threads) {
        const { markers } = thread;
        if (
          markers.data.some(
            row => row[markers.schema.data]?.type === "MLModelPrefill"
          )
        ) {
          generationThreads.add(thread.tid);
        }
      }
      process.processes.forEach(collectThreads);
    }
    collectThreads(profile);
    Assert.equal(
      generationThreads.size,
      2,
      "Each generator performs model work on its own thread"
    );
  } finally {
    Services.profiler.StopProfiler();
    a.terminate();
    b.terminate();
  }
});

add_task(async function test_create_while_generation_pending() {
  const a = await createTinyStoriesGenerator(CONCURRENT_THREADS);
  let b = null;
  try {
    const runA = startGeneration(a, LONG_REQUEST);
    await runA.firstDelta;
    Assert.ok(!runA.settled, "a is still generating");

    b = await createTinyStoriesGenerator(CONCURRENT_THREADS);
    const [resultA, resultB] = await Promise.all([
      runA.done,
      b.generate(SHORT_REQUEST),
    ]);
    Assert.greater(resultA.usage.generatedTokens, 0, "a generated");
    Assert.greater(resultB.usage.generatedTokens, 0, "The new generator works");
  } finally {
    a.terminate();
    b?.terminate();
  }
});

add_task(async function test_engine_overlap_rejects() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  try {
    const engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-concurrency-overlap",
    });
    const request = {
      prompt: TINYSTORIES_STORYTELLER_PROMPT,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
      nPredict: 16,
    };
    const failuresBefore = events("runInferenceFailure").length;
    const successesBefore = events("runInferenceSuccessFlow").length;

    const first = engine.run(request);
    await Assert.rejects(
      engine.run(request),
      /already in progress/,
      "A second run() while one is pending rejects"
    );
    await first;

    const failures = events("runInferenceFailure");
    Assert.equal(failures.length, failuresBefore + 1, "One run failure");
    Assert.equal(
      failures.at(-1).extra.error,
      "A generation is already in progress",
      "The run failure says the caller overlapped its runs"
    );
    Assert.equal(
      events("runInferenceSuccessFlow").length,
      successesBefore + 1,
      "The first run succeeded"
    );
    await engine.terminate();
  } finally {
    await cleanup();
  }
});
