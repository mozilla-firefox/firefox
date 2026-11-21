/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { TextGenerationEngine } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ml/textgeneration/TextGenerationEngine.sys.mjs"
);
const { loadMemoriesWorkload, MEMORIES_ENGINE_ID, MEMORIES_MODEL } =
  ChromeUtils.importESModule(
    "moz-src:///toolkit/components/aboutinference/content/MemoriesWorkload.sys.mjs"
  );

const PREFS = [["browser.ml.llama.hwInference", true]];
const events = name =>
  (Glean.firefoxAiRuntime[name].testGetValue() ?? []).map(event => event.extra);

/**
 * Reads what the memories view currently shows.
 *
 * @param {MozBrowser} browser
 * @returns {Promise<object>} The status string suffix, the row statuses, the
 *   setup values, the error, the download progress arguments and the ids of
 *   the enabled buttons.
 */
function getState(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    await content.customElements.whenDefined("memories-view");
    const view = content.document.querySelector("memories-view");
    await view.updateComplete;
    const root = view.shadowRoot;
    const progress = root.querySelector(
      '[data-l10n-id="about-inference-memories-download-progress"]'
    );
    return {
      status: root
        .getElementById("status")
        .dataset.l10nId.replace("about-inference-memories-", ""),
      rows: [...root.querySelectorAll("tbody tr")].map(
        row => row.dataset.status ?? null
      ),
      specs: [...root.querySelectorAll(".specs dd")].map(dd =>
        dd.textContent.trim()
      ),
      error: root.querySelector(".error")?.textContent ?? null,
      progress: progress && JSON.parse(progress.dataset.l10nArgs),
      enabled: ["run", "cancel", "cleanup"].filter(
        id => root.getElementById(id) && !root.getElementById(id).disabled
      ),
    };
  });
}

function click(browser, id) {
  return SpecialPowers.spawn(browser, [id], async id => {
    await content.customElements.whenDefined("memories-view");
    const view = content.document.querySelector("memories-view");
    await view.updateComplete;
    view.shadowRoot.getElementById(id).click();
  });
}

function waitForStatus(browser, status) {
  return SpecialPowers.spawn(browser, [status], status => {
    const view = content.document.querySelector("memories-view");
    return ContentTaskUtils.waitForCondition(
      () => view.status === status,
      `The status becomes ${status}`
    );
  });
}

async function finishRun(browser) {
  const run = await SpecialPowers.spawn(browser, [], async () => {
    const view = content.document.querySelector("memories-view");
    await ContentTaskUtils.waitForCondition(
      () => view.run && !view.running,
      "The workload finishes",
      100,
      1200
    );
    return { ...view.run, status: view.status, error: view.error };
  });
  Assert.deepEqual(
    (await getState(browser)).enabled,
    ["run", "cleanup"],
    "Run replaces Cancel"
  );
  return run;
}

async function withMockPanel(test) {
  const { cleanup } = await setup({ prefs: PREFS });
  const sandbox = sinon.createSandbox();
  sandbox.stub(ModelHub.prototype, "getOwnerIcon").resolves(null);
  const engine = {
    deviceKind: "cpu",
    run: sandbox.stub().resolves({
      finalOutput: "This is deliberately not JSON.",
      finishReason: "stop",
      metrics: { inputTokens: 100, outputTokens: 10, inferenceTime: 20 },
      resourcesBefore: { memory: 512 },
      resourcesAfter: { memory: 1024 },
    }),
    cancel: sandbox.stub(),
    terminate: sandbox.stub().resolves(),
  };
  const create = sandbox.stub(TextGenerationEngine, "create").resolves(engine);
  try {
    await BrowserTestUtils.withNewTab("about:inference#memories", browser =>
      test({ browser, engine, create, sandbox })
    );
  } finally {
    sandbox.restore();
    await cleanup();
  }
}

add_task(async function test_sidebar_link() {
  const { cleanup } = await setup({ prefs: PREFS });
  await BrowserTestUtils.withNewTab("about:inference", async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const link = content.document.getElementById("memories-link");
      await ContentTaskUtils.waitForCondition(
        () => !link.hidden,
        "The memories link is shown"
      );
      link.click();
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector("#memories > memories-view"),
        "The memories view opens"
      );
      Assert.ok(
        content.document.body.classList.contains("memories"),
        "The page switches to the memories view"
      );
    });
  });
  await cleanup();
});

add_task(async function test_fixed_workload() {
  const workload = await loadMemoriesWorkload();
  Assert.deepEqual(
    workload.map(call => call.id),
    ["generate", "merge", "filter"]
  );
  Assert.ok(
    !JSON.stringify(workload).match(
      /\{(?:categoriesList|intentsList|profileRecordsRenderedStr|memoriesList)\}/
    ),
    "Every placeholder is filled in"
  );
  await withMockPanel(async ({ browser, engine, create }) => {
    for (let attempt = 0; attempt < 2; attempt++) {
      await click(browser, "run");
      const run = await finishRun(browser);
      Assert.equal(run.status, "done", run.error);
      Assert.deepEqual(
        run.calls.map(call => call.id),
        ["generate", "merge", "filter"]
      );
    }
    const { rows, specs } = await getState(browser);
    Assert.deepEqual(rows, ["done", "done", "done"]);
    Assert.equal(specs[1], "CPU", "Shows the kind of an unlisted device");
    Assert.notEqual(specs[3], "—", "Shows the system memory");
    Assert.equal(create.callCount, 2, "Each attempt creates one engine");
    Assert.equal(engine.terminate.callCount, 2, "Each engine is released");
    Assert.equal(create.firstCall.args[0].engineId, MEMORIES_ENGINE_ID);
    Assert.equal(create.firstCall.args[0].featureId, MEMORIES_MODEL.featureId);
    Assert.deepEqual(
      engine.run.getCalls().map(call => call.args[0]),
      [...workload, ...workload].map(({ featureId, prompt }) => ({
        args: prompt,
        featureId,
        inferenceParams: { temperature: 0 },
      })),
      "Every task uses fixed input, regardless of earlier output"
    );
    Assert.deepEqual(
      events("sessionEnd").map(event => event.status),
      ["done", "done"]
    );
  });
});

add_task(async function test_failures_allow_retry() {
  for (const phase of ["create", "run", "terminate"]) {
    info(`Failure during ${phase}`);
    await withMockPanel(async ({ browser, engine, create }) => {
      const failing = phase === "create" ? create : engine[phase];
      failing.onFirstCall().rejects(new Error(`${phase} failed`));
      await click(browser, "run");
      const run = await finishRun(browser);
      Assert.equal(run.status, "failed");
      Assert.ok(run.error.includes(`${phase} failed`));
      Assert.equal(
        engine.run.callCount,
        { create: 0, run: 1, terminate: 3 }[phase]
      );
      Assert.equal(engine.terminate.callCount, phase === "create" ? 0 : 1);
      await click(browser, "run");
      Assert.equal((await finishRun(browser)).status, "done", "Retry succeeds");
      Assert.deepEqual(
        events("sessionEnd").map(event => event.status),
        ["failed", "done"]
      );
    });
  }
});

add_task(async function test_cancellation() {
  for (const action of ["cancel", "leave"]) {
    await withMockPanel(async ({ browser, engine }) => {
      const pending = Promise.withResolvers();
      engine.run.returns(pending.promise);
      engine.cancel.callsFake(() =>
        pending.reject(new DOMException("Cancelled", "AbortError"))
      );
      await click(browser, "run");
      await TestUtils.waitForCondition(
        () => engine.run.calledOnce,
        "Inference started"
      );
      Assert.deepEqual((await getState(browser)).enabled, ["cancel"]);
      if (action === "cancel") {
        await click(browser, "cancel");
      } else {
        await SpecialPowers.spawn(browser, [], async () => {
          const view = content.document.querySelector("memories-view");
          content.location.hash = "";
          await ContentTaskUtils.waitForCondition(() => !view.isConnected);
          Assert.ok(!content.document.body.classList.contains("memories"));
        });
      }
      await TestUtils.waitForCondition(() => events("sessionEnd").length === 1);
      Assert.equal(events("sessionEnd")[0].status, "cancelled");
      Assert.ok(engine.cancel.calledOnce);
      Assert.ok(engine.terminate.calledOnce);
      Assert.ok(
        engine.run.calledOnce,
        "Cancellation skips the remaining tasks"
      );
      if (action === "cancel") {
        Assert.equal((await finishRun(browser)).status, "cancelled");
      }
    });
  }
});

add_task(async function test_shared_inference_timeout() {
  await withMockPanel(async ({ browser, engine, create }) => {
    const tick = ms =>
      SpecialPowers.spawn(browser, [ms], ms => content.clock.tick(ms));
    const countTimers = () =>
      SpecialPowers.spawn(browser, [], () => content.clock.countTimers());
    const loading = Promise.withResolvers();
    const generating = Promise.withResolvers();
    const merging = Promise.withResolvers();
    const result = await engine.run();
    engine.run.resetHistory();
    create.returns(loading.promise);
    engine.run.onFirstCall().returns(generating.promise);
    engine.run.onSecondCall().returns(merging.promise);
    engine.cancel.callsFake(() =>
      merging.resolve({ ...result, finishReason: "cancelled" })
    );
    await SpecialPowers.spawn(browser, [], () => {
      const { sinon } = ChromeUtils.importESModule(
        "resource://testing-common/Sinon.sys.mjs"
      );
      content.clock = sinon.createSandbox().useFakeTimers({
        global: content.wrappedJSObject,
        toFake: ["setTimeout", "clearTimeout"],
      });
    });
    try {
      await click(browser, "run");
      await TestUtils.waitForCondition(() => create.calledOnce);
      await tick(10 * 60 * 1000);
      Assert.ok(!create.firstCall.args[2].aborted, "Loading has no deadline");
      loading.resolve(engine);
      await TestUtils.waitForCondition(() => engine.run.calledOnce);
      await tick(3 * 60 * 1000);
      Assert.ok(engine.cancel.notCalled);
      generating.resolve(result);
      await TestUtils.waitForCondition(() => engine.run.calledTwice);
      Assert.equal((await getState(browser)).status, "running-merge");
      await tick(2 * 60 * 1000 - 1);
      Assert.ok(engine.cancel.notCalled, "The shared budget has not expired");
      await tick(1);
      const run = await finishRun(browser);
      Assert.equal(run.status, "timed-out");
      Assert.equal(run.calls[1].result.finishReason, "cancelled");
      Assert.equal(run.calls[1].result.metrics.outputTokens, 10);
      Assert.ok(engine.run.calledTwice, "The remaining step is skipped");
      Assert.ok(engine.cancel.calledOnce);
      Assert.ok(engine.terminate.calledOnce);
      const { status, rows } = await getState(browser);
      Assert.equal(status, "timed-out");
      Assert.deepEqual(rows, ["done", "timed-out", null]);
      Assert.equal(await countTimers(), 0, "The timer is cleared");
      await click(browser, "run");
      Assert.equal((await finishRun(browser)).status, "done");
      Assert.equal(await countTimers(), 0);
      await tick(5 * 60 * 1000);
      Assert.ok(engine.cancel.calledOnce, "Completed runs cannot time out");
      Assert.deepEqual(
        events("sessionEnd").map(event => event.status),
        ["timed-out", "done"]
      );
    } finally {
      await SpecialPowers.spawn(browser, [], () => {
        content.clock.restore();
        delete content.clock;
      });
    }
  });
});

add_task(async function test_cancel_during_creation() {
  await withMockPanel(async ({ browser, engine, create }) => {
    const { ProgressType, ProgressStatusText } = ChromeUtils.importESModule(
      "chrome://global/content/ml/Utils.sys.mjs"
    );
    const pending = Promise.withResolvers();
    create.returns(pending.promise);
    await click(browser, "run");
    await TestUtils.waitForCondition(() => create.calledOnce);
    create.firstCall.args[1]({
      type: ProgressType.DOWNLOAD,
      statusText: ProgressStatusText.INITIATE,
      totalLoaded: 256,
      total: 1024,
    });
    const { status, progress } = await getState(browser);
    Assert.equal(status, "downloading");
    Assert.equal(progress.percent, 25);
    await click(browser, "cancel");
    Assert.ok(
      create.firstCall.args[2].aborted,
      "Download receives the abort signal"
    );
    pending.resolve(engine);
    Assert.equal((await finishRun(browser)).status, "cancelled");
    Assert.ok(engine.run.notCalled);
    Assert.ok(
      engine.terminate.calledOnce,
      "An engine created after cancellation is released"
    );
  });
});

add_task(async function test_wait_for_engine_release() {
  await withMockPanel(async ({ browser, engine }) => {
    const pending = Promise.withResolvers();
    engine.terminate.returns(pending.promise);
    await click(browser, "run");
    await TestUtils.waitForCondition(() => engine.terminate.calledOnce);
    Assert.deepEqual(
      (await getState(browser)).enabled,
      [],
      "Releasing the engine cannot be cancelled"
    );
    pending.resolve();
    Assert.equal((await finishRun(browser)).status, "done");
  });
});

add_task(async function test_progress_and_partial_results() {
  await withMockPanel(async ({ browser, engine }) => {
    const pending = Promise.withResolvers();
    engine.run.onSecondCall().returns(pending.promise);
    engine.cancel.callsFake(() =>
      pending.reject(new DOMException("Cancelled", "AbortError"))
    );
    await click(browser, "run");
    await TestUtils.waitForCondition(() => engine.run.calledTwice);
    const { status, rows, enabled } = await getState(browser);
    Assert.equal(status, "running-merge");
    Assert.deepEqual(rows, ["done", "running", null]);
    Assert.deepEqual(enabled, ["cancel"]);
    await click(browser, "cancel");
    const run = await finishRun(browser);
    Assert.equal(run.status, "cancelled");
    Assert.equal(run.calls[0].result.metrics.inputTokens, 100);
    Assert.deepEqual((await getState(browser)).rows, [
      "done",
      "cancelled",
      null,
    ]);
  });
});

add_task(async function test_cleanup_after_reopening() {
  await withMockPanel(async ({ browser }) => {
    const cache = await IndexedDBCache.init();
    const model = `${new URL(MEMORIES_MODEL.modelHubRootUrl).hostname}/${MEMORIES_MODEL.modelId}`;
    const revision = MEMORIES_MODEL.modelRevision;
    const file = MEMORIES_MODEL.modelFile;
    const entries = [
      { model, revision, file },
      { model, revision, file: "another.gguf" },
      { model, revision: "another-revision", file },
      { model: "example.com/another/model", revision, file },
    ];
    for (const entry of entries) {
      await cache.put({
        ...entry,
        engineId: MEMORIES_ENGINE_ID,
        taskName: MEMORIES_MODEL.taskName,
        data: new Blob(["model fixture"]),
      });
    }
    await BrowserTestUtils.reloadTab(gBrowser.getTabForBrowser(browser));
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("memories-view");
      const view = content.document.querySelector("memories-view");
      await ContentTaskUtils.waitForCondition(() => view.cacheSize === 13);
      Assert.equal(view.run, null, "Cleanup is available before a run");
    });
    await click(browser, "cleanup");
    await waitForStatus(browser, "idle");
    await SpecialPowers.spawn(browser, [], () => {
      Assert.equal(
        content.document.querySelector("memories-view").cacheSize,
        null,
        "Cleanup clears the downloaded size"
      );
    });
    for (const [index, entry] of entries.entries()) {
      const { files } = await cache.listFiles(entry);
      Assert.equal(
        files.some(item => item.path === entry.file),
        index !== 0,
        "Only the configured model file and revision are removed"
      );
    }
    const deletion = events("modelDeletion").at(-1);
    Assert.equal(deletion.deletedBy, MEMORIES_ENGINE_ID);
    Assert.equal(deletion.modelId, model);
    await click(browser, "run");
    Assert.equal((await finishRun(browser)).status, "done");
  });
});

add_task(async function test_cleanup_failure_and_retry() {
  await withMockPanel(async ({ browser, engine, sandbox }) => {
    await click(browser, "run");
    await finishRun(browser);
    const pending = Promise.withResolvers();
    const remove = sandbox.stub(ModelHub.prototype, "deleteModels");
    remove.onFirstCall().rejects(new Error("Deletion failed"));
    remove.onSecondCall().returns(pending.promise);
    await click(browser, "cleanup");
    await waitForStatus(browser, "failed");
    let state = await getState(browser);
    Assert.deepEqual(
      state.rows,
      ["done", "done", "done"],
      "Failed cleanup preserves results"
    );
    Assert.ok(state.error.includes("Deletion failed"));
    await click(browser, "cleanup");
    Assert.deepEqual((await getState(browser)).enabled, []);
    await click(browser, "run");
    Assert.equal(engine.run.callCount, 3, "A run cannot start during cleanup");
    pending.resolve();
    await waitForStatus(browser, "idle");
    state = await getState(browser);
    Assert.deepEqual(
      state.rows,
      [null, null, null],
      "Successful cleanup clears results"
    );
    Assert.equal(state.error, null);
    Assert.deepEqual(state.enabled, ["run", "cleanup"]);
    await click(browser, "run");
    Assert.equal((await finishRun(browser)).status, "done");
  });
});

add_task(async function test_native_run_and_cache() {
  requestLongerTimeout(4);
  const { cleanup } = await setup({ prefs: PREFS });
  await IndexedDBCache.init({ reset: true });
  const testHub =
    "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data";
  const createHub = sinon
    .stub(MLEngineParent, "createModelHub")
    .callsFake(async () => {
      const hub = await createHub.wrappedMethod.call(MLEngineParent, {
        rootUrl: testHub,
        urlTemplate: "{model}/{revision}",
      });
      const getBlob = hub.getModelFileAsBlob.bind(hub);
      hub.getModelFileAsBlob = args =>
        getBlob({
          ...args,
          model: "Mozilla/test-llama",
          revision: "main",
          file: "byte-fallback.gguf",
          modelHubRootUrl: testHub,
          modelHubUrlTemplate: "{model}/{revision}",
        });
      return hub;
    });
  // byte-fallback.gguf's 8-wide heads do not fit q8_0 blocks.
  const { kvCacheDtype } = MEMORIES_MODEL;
  MEMORIES_MODEL.kvCacheDtype = "f16";
  try {
    await BrowserTestUtils.withNewTab(
      "about:inference#memories",
      async browser => {
        for (let attempt = 0; attempt < 2; attempt++) {
          await click(browser, "run");
          const run = await finishRun(browser);
          Assert.equal(run.status, "done", run.error);
          Assert.equal(
            Boolean(run.model.cached),
            attempt === 1,
            "The second run reuses the download"
          );
          Assert.equal(run.calls.length, 3);
          for (const { result } of run.calls) {
            Assert.greater(
              result.metrics.inputTokens,
              512,
              "The prompt spans multiple decode batches"
            );
            Assert.greater(result.metrics.outputTokens, 0);
            Assert.greater(result.resourcesAfter.memory, 0);
          }
        }
      }
    );
    const downloads = events("modelDownload");
    Assert.equal(
      downloads.filter(event => event.step === "end_file_download_success")
        .length,
      1
    );
    Assert.ok(
      downloads.every(event => event.featureId === MEMORIES_MODEL.featureId)
    );
    const features = [
      "memories-initial-generation-system",
      "memories-merge",
      "memories-quality-and-sensitivity-filter-system",
    ];
    const runs = events("engineRun");
    Assert.deepEqual(
      runs.map(run => run.feature_id),
      [...features, ...features]
    );
    Assert.ok(runs.every(run => run.engine_id === MEMORIES_ENGINE_ID));
    Assert.deepEqual(
      events("runInferenceSuccessFlow").map(run => run.flow_id),
      runs.map(run => run.flow_id),
      "Inference timings and engine runs share the flow ID"
    );
    const starts = events("sessionStart");
    Assert.equal(starts.length, 2);
    Assert.notEqual(starts[0].flow_id, starts[1].flow_id);
    Assert.ok(
      starts.every(event => event.feature_id === MEMORIES_MODEL.featureId)
    );
    Assert.deepEqual(
      events("sessionEnd").map(event => event.flow_id),
      starts.map(event => event.flow_id)
    );
  } finally {
    MEMORIES_MODEL.kvCacheDtype = kvCacheDtype;
    createHub.restore();
    await cleanup();
  }
});
