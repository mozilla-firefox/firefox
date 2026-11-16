/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { computeHash, MultiProgressAggregator } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const MODEL_FILE = {
  model: "acme/bert",
  revision: "main",
  file: "config.json",
};

const MODEL_ARGS = { ...MODEL_FILE, taskName: "test_model_hub_download" };

const CONFIG_JSON = JSON.stringify({ hidden_size: 32 });

/**
 * @param {ArrayBuffer} buffer
 * @returns {string}
 */
function decode(buffer) {
  return new TextDecoder().decode(new Uint8Array(buffer));
}

/**
 * The hex SHA-256 of a buffer, to compare against the `contentHash` the mock
 * hub reports for what it served.
 *
 * @param {ArrayBuffer} buffer
 * @returns {Promise<string>}
 */
function sha256(buffer) {
  return computeHash(new Blob([buffer]), "sha256", "hex");
}

/**
 * Ensure all of the model hubs are cleaned up when a test file finishes.
 */
let gOpenModelHubs = 0;
registerCleanupFunction(() => {
  Assert.equal(
    gOpenModelHubs,
    0,
    "Every withModelHub() context was cleaned up by the task that created it."
  );
});

/**
 * Stand up a mock hub serving one model file, plus a ModelHub pointed at it.
 * The caller is responsible for calling the returned cleanup.
 *
 * @param {object} [options]
 * @param {string} [options.content]
 * @returns {Promise<object>}
 */
async function withModelHub({ content = CONFIG_JSON } = {}) {
  const hub = MLTestUtils.createMockModelHub();
  const served = hub.setFile({ ...MODEL_FILE, content });

  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
    reset: true,
  });

  let hasCleanedUp = false;
  gOpenModelHubs++;

  return {
    hub,
    modelHub,
    served,
    async cleanup() {
      if (hasCleanedUp) {
        throw new Error("Attempted to cleanup twice.");
      }
      hasCleanedUp = true;
      gOpenModelHubs--;
      if (modelHub.cache) {
        await deleteCache(modelHub.cache);
      }
      await hub.cleanup();
    },
  };
}

/**
 * The happy path: a file the hub serves arrives intact, and arrives over HTTP
 * rather than out of a fixture directory.
 */
add_task(async function test_downloads_file_from_hub() {
  const { hub, modelHub, served, cleanup } = await withModelHub();

  const [buffer, headers] =
    await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    decode(buffer),
    CONFIG_JSON,
    "The bytes the hub served arrived intact."
  );
  Assert.equal(
    JSON.parse(decode(buffer)).hidden_size,
    32,
    "The file parses as the JSON that was served."
  );
  Assert.equal(
    headers.ETag,
    served.etag,
    "The stored headers carry the ETag the hub served."
  );
  Assert.equal(
    headers.fileSize,
    served.size,
    "The stored file size matches the body length."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    1,
    "The file was downloaded exactly once."
  );

  await cleanup();
});

/**
 * A second read is served from the cache. Asserted against the hub's request
 * log, so this fails if anything silently refetches the body.
 */
add_task(async function test_second_read_does_not_refetch_body() {
  const { hub, modelHub, cleanup } = await withModelHub();

  const [first] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);
  const getsAfterFirst = hub.requestCount({ ...MODEL_FILE, method: "GET" });

  const [second] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    decode(second),
    decode(first),
    "The cached read returns the same bytes."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    getsAfterFirst,
    "The body was not downloaded a second time."
  );

  await cleanup();
});

/**
 * A file missing from the hub surfaces as a failure rather than caching an
 * error page as if it were model data.
 */
add_task(async function test_missing_file_rejects() {
  const { hub, modelHub, cleanup } = await withModelHub();
  hub.removeFile(MODEL_FILE);

  await Assert.rejects(
    modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
    /404/,
    "Requesting a file the hub does not have rejects."
  );

  await cleanup();
});

/**
 * Two callers asking for the same file at the same time share one revalidation
 * and one transfer, and both get the whole file.
 */
add_task(async function test_concurrent_reads_share_one_download() {
  // Large enough that the two transfers and their OPFS writes genuinely
  // overlap, rather than the first finishing before the second starts.
  const content = "x".repeat(1024 * 1024);
  const { hub, modelHub, served, cleanup } = await withModelHub({ content });

  const [[firstBuffer, firstHeaders], [secondBuffer, secondHeaders]] =
    await Promise.all([
      modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
      modelHub.getModelFileAsArrayBuffer(MODEL_ARGS),
    ]);

  Assert.deepEqual(
    {
      head: hub.requestCount({ ...MODEL_FILE, method: "HEAD" }),
      get: hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    },
    { head: 1, get: 1 },
    "The second caller joined the first one's revalidation and transfer."
  );

  Assert.equal(
    firstBuffer.byteLength,
    served.size,
    "The first caller got the whole file."
  );
  Assert.equal(
    secondBuffer.byteLength,
    served.size,
    "The second caller got the whole file."
  );

  // Compared by digest so a failure does not print a megabyte of diff.
  Assert.equal(
    await sha256(firstBuffer),
    served.contentHash,
    "The first caller's bytes are intact despite the overlapping write."
  );
  Assert.equal(
    await sha256(secondBuffer),
    served.contentHash,
    "The second caller's bytes are intact despite the overlapping write."
  );

  Assert.equal(firstHeaders.ETag, served.etag, "The first caller got an ETag.");
  Assert.equal(
    secondHeaders.ETag,
    served.etag,
    "The second caller got an ETag."
  );
  Assert.equal(
    secondHeaders.fileSize,
    served.size,
    "The stored file size is the full size."
  );

  const [after] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    await sha256(after),
    served.contentHash,
    "The file left in the cache is intact."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    1,
    "The cached file is good enough to serve a later read without refetching."
  );

  await cleanup();
});

/**
 * Two callers asking for the same file, each with its own abortSignal, still
 * share one revalidation and one transfer.
 */
add_task(async function test_one_abort_does_not_cancel_a_shared_download() {
  const content = "x".repeat(1024 * 1024);
  const { hub, modelHub, served, cleanup } = await withModelHub({ content });

  const abortedController = new AbortController();
  const survivingController = new AbortController();

  const abortedPromise = modelHub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    abortSignal: abortedController.signal,
  });
  const survivingPromise = modelHub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    abortSignal: survivingController.signal,
  });

  abortedController.abort();

  await Assert.rejects(
    abortedPromise,
    error => error.name === "AbortError",
    "The caller that aborted its own signal is rejected."
  );

  const [survivingBuffer] = await survivingPromise;

  Assert.equal(
    survivingBuffer.byteLength,
    served.size,
    "The other caller's download still completed in full."
  );
  Assert.equal(
    await sha256(survivingBuffer),
    served.contentHash,
    "The other caller's bytes are intact, unaffected by the first caller's abort."
  );
  Assert.deepEqual(
    {
      head: hub.requestCount({ ...MODEL_FILE, method: "HEAD" }),
      get: hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    },
    { head: 1, get: 1 },
    "Both callers shared the same revalidation and transfer."
  );

  const [after] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  Assert.equal(
    await sha256(after),
    served.contentHash,
    "The file left in the cache is the completed download, not a partial one."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    1,
    "The shared download was never interrupted, so the cache serves this read without refetching."
  );

  await cleanup();
});

/**
 * Canceling one waiter must preserve another waiter's registration even when
 * both supplied the same progress callback.
 */
add_task(async function test_shared_callback_survives_one_callers_abort() {
  const { modelHub, served, cleanup } = await withModelHub({
    content: "x".repeat(1024 * 1024),
  });
  const controller = new AbortController();
  let survivingProgress = 0;
  const progressCallback = progress => {
    if (!progress.totalLoaded) {
      return;
    }
    if (!controller.signal.aborted) {
      controller.abort();
      return;
    }
    survivingProgress++;
  };

  try {
    const first = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      progressCallback,
      abortSignal: controller.signal,
    });
    const second = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      progressCallback,
    });
    await Promise.allSettled([first, second]);
    await Assert.rejects(
      first,
      error => error.name === "AbortError",
      "The first caller cancels."
    );
    const [buffer] = await second;
    Assert.greater(
      survivingProgress,
      0,
      "The remaining caller still gets progress."
    );
    Assert.equal(
      await sha256(buffer),
      served.contentHash,
      "The file is intact."
    );
  } finally {
    await cleanup();
  }
});

/**
 * Once every caller sharing a download has aborted, the shared transfer is
 * actually canceled.
 */
add_task(async function test_all_aborts_cancel_a_shared_download() {
  const content = "x".repeat(1024 * 1024);
  const { hub, modelHub, served, cleanup } = await withModelHub({ content });

  const firstController = new AbortController();
  const secondController = new AbortController();

  const firstPromise = modelHub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    abortSignal: firstController.signal,
  });
  const secondPromise = modelHub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    abortSignal: secondController.signal,
  });

  // Let the shared fetch actually start before canceling it, rather than
  // aborting before it exists.
  await new Promise(resolve => setTimeout(resolve, 0));

  firstController.abort();
  secondController.abort();

  await Assert.rejects(
    firstPromise,
    error => error.name === "AbortError",
    "The first caller is rejected."
  );
  await Assert.rejects(
    secondPromise,
    error => error.name === "AbortError",
    "The second caller is rejected."
  );

  const getsWhileAborting = hub.requestCount({ ...MODEL_FILE, method: "GET" });

  const [after] = await modelHub.getModelFileAsArrayBuffer(MODEL_ARGS);

  // Compared by digest so a failure does not print a megabyte of diff.
  Assert.equal(
    await sha256(after),
    served.contentHash,
    "A later read gets the full file rather than whatever the canceled transfer left behind."
  );
  Assert.equal(
    hub.requestCount({ ...MODEL_FILE, method: "GET" }),
    getsWhileAborting + 1,
    "The later read had to redo the transfer, since the shared one was actually canceled rather than finishing unobserved."
  );

  await cleanup();
});

/**
 * A new caller must not inherit cancellation from callers that have all left
 * the shared download.
 */
add_task(async function test_immediate_read_after_all_callers_abort() {
  const content = "x".repeat(1024 * 1024);
  const { modelHub, served, cleanup } = await withModelHub({ content });
  const firstController = new AbortController();
  const secondController = new AbortController();
  const retryController = new AbortController();
  const retry = Promise.withResolvers();
  let retryStarted = false;

  try {
    info(
      "Start two shared reads, then cancel both when the body makes progress."
    );
    const firstPromise = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      abortSignal: firstController.signal,
      progressCallback: progress => {
        if (retryStarted || !progress.totalLoaded) {
          return;
        }
        retryStarted = true;
        firstController.abort();
        secondController.abort();

        info(
          "Immediately request the same file with a fresh, un-aborted signal."
        );
        modelHub
          .getModelFileAsArrayBuffer({
            ...MODEL_ARGS,
            abortSignal: retryController.signal,
          })
          .then(retry.resolve, retry.reject);
      },
    });
    const secondPromise = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      abortSignal: secondController.signal,
    });

    await Promise.allSettled([firstPromise, secondPromise, retry.promise]);
    await Assert.rejects(
      firstPromise,
      error => error.name === "AbortError",
      "The first caller is canceled."
    );
    await Assert.rejects(
      secondPromise,
      error => error.name === "AbortError",
      "The second caller is canceled."
    );
    const [buffer] = await retry.promise;

    Assert.ok(
      !retryController.signal.aborted,
      "The new caller never canceled."
    );
    Assert.equal(
      await sha256(buffer),
      served.contentHash,
      "The immediate retry gets the complete file without a retry loop."
    );
  } finally {
    await cleanup();
  }
});

/**
 * Sharing a transfer must still record each caller's engine reference, so
 * deleting one engine's files preserves the other engine's cached copy.
 */
add_task(
  async function test_concurrent_reads_preserve_each_callers_engine_reference() {
    const { hub, modelHub, cleanup } = await withModelHub();
    const firstArgs = {
      ...MODEL_ARGS,
      engineId: "engine-one",
      taskName: "task-one",
    };
    const secondArgs = {
      ...MODEL_ARGS,
      engineId: "engine-two",
      taskName: "task-two",
    };
    const thirdArgs = {
      ...MODEL_ARGS,
      engineId: "engine-three",
      taskName: "task-three",
    };
    const qualified = `${new URL(hub.rootUrl).hostname}/${MODEL_FILE.model}`;

    try {
      info("Request the same file concurrently from three engines and tasks.");
      const [[first], [second], [third]] = await Promise.all([
        modelHub.getModelFileAsArrayBuffer(firstArgs),
        modelHub.getModelFileAsArrayBuffer(secondArgs),
        modelHub.getModelFileAsArrayBuffer(thirdArgs),
      ]);
      Assert.equal(
        decode(first),
        CONFIG_JSON,
        "The first caller gets the file."
      );
      Assert.equal(
        decode(second),
        CONFIG_JSON,
        "The second caller gets the file."
      );
      Assert.equal(
        decode(third),
        CONFIG_JSON,
        "The third caller gets the file."
      );
      Assert.equal(
        hub.requestCount({ ...MODEL_FILE, method: "GET" }),
        1,
        "All three callers share one body transfer."
      );

      const models = await modelHub.listModels();
      Assert.equal(models.length, 1, "The shared model has one listing.");
      Assert.deepEqual(
        [...models[0].tasks].sort(),
        ["task-one", "task-three", "task-two"],
        "All three callers' tasks are recorded."
      );
      const { files } = await modelHub.listFiles({
        model: qualified,
        revision: MODEL_FILE.revision,
      });
      Assert.equal(files.length, 1, "The shared file has one listing.");
      Assert.deepEqual(
        [...files[0].engineIds].sort(),
        ["engine-one", "engine-three", "engine-two"],
        "All three callers' engines are recorded."
      );

      info("Delete the first engine's files and make the hub unavailable.");
      await modelHub.deleteFilesByEngine({ engineId: firstArgs.engineId });
      hub.removeFile(MODEL_FILE);

      const [after] = await modelHub.getModelFileAsArrayBuffer(secondArgs);
      Assert.equal(
        decode(after),
        CONFIG_JSON,
        "The second engine can still read its cached file."
      );
      Assert.equal(
        hub.requestCount({ ...MODEL_FILE, method: "GET" }),
        1,
        "Deleting the first engine does not force the second to download again."
      );
    } finally {
      await cleanup();
    }
  }
);

/**
 * Sharing a file lookup across tasks should use one underlying GET request.
 */
add_task(async function test_concurrent_cache_hits_use_single_get() {
  const { hub, modelHub, cleanup } = await withModelHub();
  try {
    await modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      engineId: "seed-engine",
      taskName: "seed-task",
    });

    info("Three new consumers read the cached file concurrently.");
    const names = ["alpha", "bravo", "charlie"];
    const results = await Promise.all(
      names.map(name =>
        modelHub.getModelFileAsArrayBuffer({
          ...MODEL_ARGS,
          engineId: name,
          taskName: name,
        })
      )
    );
    for (const [buffer] of results) {
      Assert.equal(decode(buffer), CONFIG_JSON, "The cached bytes are intact.");
    }
    const [model] = await modelHub.listModels();
    Assert.deepEqual(
      [...model.tasks].sort(),
      [...names, "seed-task"],
      "Every task is registered on a cache hit."
    );
    const { files } = await modelHub.listFiles({
      model: model.name,
      revision: model.revision,
    });
    Assert.deepEqual(
      [...files[0].engineIds].sort(),
      [...names, "seed-engine"],
      "Every engine is registered on a cache hit."
    );
    Assert.equal(
      hub.requestCount({ ...MODEL_FILE, method: "GET" }),
      1,
      "All new consumers use the cached body."
    );
  } finally {
    await cleanup();
  }
});

/**
 * An already-aborted caller must not initiate a request or acquire ownership.
 */
add_task(async function test_already_aborted_read_does_not_start_download() {
  const { hub, modelHub, cleanup } = await withModelHub();
  try {
    await Assert.rejects(
      modelHub.getModelFileAsArrayBuffer({
        ...MODEL_ARGS,
        abortSignal: AbortSignal.abort(),
      }),
      error => error.name === "AbortError",
      "An already-aborted request rejects."
    );
    Assert.equal(
      hub.requestCount({ ...MODEL_FILE, method: "HEAD" }),
      0,
      "The canceled caller does not start revalidation."
    );
    Assert.equal(
      hub.requestCount({ ...MODEL_FILE, method: "GET" }),
      0,
      "The canceled caller does not start a transfer."
    );
    Assert.ok(
      !modelHub.cache,
      "The canceled caller does not initialize a cache."
    );
  } finally {
    await cleanup();
  }
});

/**
 * A caller's progress callback can throw without interrupting either
 * caller's read or the other caller's progress updates.
 */
add_task(async function test_progress_callback_exceptions_are_isolated() {
  for (const statusText of [
    "initiate",
    "size_estimate",
    "in_progress",
    "done",
  ]) {
    const { hub, modelHub, served, cleanup } = await withModelHub({
      content: "x".repeat(1024 * 1024),
    });
    let threw = false;
    const events = [];

    try {
      info(`Share a download whose first callback throws at ${statusText}.`);
      const results = await Promise.allSettled([
        modelHub.getModelFileAsArrayBuffer({
          ...MODEL_ARGS,
          progressCallback: progress => {
            if (!threw && progress.statusText === statusText) {
              threw = true;
              throw new Error(`Test callback failure at ${statusText}`);
            }
          },
        }),
        modelHub.getModelFileAsArrayBuffer({
          ...MODEL_ARGS,
          progressCallback: progress => events.push(progress.statusText),
        }),
      ]);

      info("Check that both callers received the file and progress continued.");
      Assert.ok(threw, `The callback threw at ${statusText}.`);
      Assert.deepEqual(
        results.map(result => result.status),
        ["fulfilled", "fulfilled"],
        `A callback exception at ${statusText} does not fail either read.`
      );
      for (const result of results) {
        if (result.status === "fulfilled") {
          Assert.equal(
            await sha256(result.value[0]),
            served.contentHash,
            "The complete file is returned."
          );
        }
      }
      Assert.deepEqual(
        [events[0], events[1], events.at(-1)],
        ["initiate", "size_estimate", "done"],
        "The other caller receives its progress lifecycle."
      );
      Assert.equal(
        hub.requestCount({ ...MODEL_FILE, method: "GET" }),
        1,
        "The callers still share one transfer."
      );
    } finally {
      await cleanup();
    }
  }
});

/**
 * A caller joining after bytes have arrived gets its own progress lifecycle,
 * including those bytes in its aggregated total.
 */
add_task(async function test_late_joiner_receives_progress_lifecycle() {
  const { hub, modelHub, cleanup } = await withModelHub({
    content: "x".repeat(1024 * 1024),
  });
  const events = [];
  const combined = [];
  const aggregator = new MultiProgressAggregator({
    progressCallback: progress => combined.push(progress),
  });
  let late;
  let loadedAtJoin;

  try {
    info("Start downloading before a second caller requests the same file.");
    const first = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      taskName: "first-task",
      progressCallback: progress => {
        if (late || !progress.totalLoaded) {
          return;
        }

        info("Join the transfer after it has reported downloaded bytes.");
        loadedAtJoin = progress.totalLoaded;
        late = modelHub.getModelFileAsArrayBuffer({
          ...MODEL_ARGS,
          taskName: "late-task",
          progressCallback: data => {
            events.push(data);
            aggregator.aggregateCallback(data);
          },
        });
      },
    });
    await first;
    await late;

    info("Check the late caller's progress lifecycle and aggregated total.");
    Assert.deepEqual(
      [events[0]?.statusText, events[1]?.statusText, events.at(-1)?.statusText],
      ["initiate", "size_estimate", "done"],
      "The late caller receives initialization before progress and completion."
    );
    Assert.equal(
      events[1]?.currentLoaded,
      loadedAtJoin,
      "The size estimate accounts for the bytes transferred before joining."
    );
    Assert.equal(combined.at(-1)?.statusText, "done", "Aggregation completes.");
    Assert.equal(combined.at(-1)?.progress, 100, "Aggregation reaches 100%.");
    Assert.equal(
      hub.requestCount({ ...MODEL_FILE, method: "GET" }),
      1,
      "The late caller joins the existing transfer."
    );
  } finally {
    await cleanup();
  }
});

/**
 * Canceling a late caller during initialization stops its progress updates
 * while the original caller finishes its read.
 */
add_task(async function test_late_joiner_can_abort_during_replayed_progress() {
  const { modelHub, cleanup } = await withModelHub({
    content: "x".repeat(1024 * 1024),
  });
  const controller = new AbortController();
  const events = [];
  let late;

  try {
    info("Start a transfer that will continue after the late caller cancels.");
    const first = modelHub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      progressCallback: progress => {
        if (late || !progress.totalLoaded) {
          return;
        }

        info("Join the transfer and cancel when initialization is reported.");
        late = modelHub.getModelFileAsArrayBuffer({
          ...MODEL_ARGS,
          abortSignal: controller.signal,
          progressCallback: data => {
            events.push(data.statusText);
            if (data.statusText === "initiate") {
              controller.abort();
            }
          },
        });
        late.catch(() => {});
      },
    });
    await first;

    info("Check that the late caller canceled and received no more progress.");
    await Assert.rejects(
      late,
      error => error.name === "AbortError",
      "The late caller cancels while its initialization is replayed."
    );
    Assert.deepEqual(events, ["initiate"], "No progress follows cancellation.");
  } finally {
    await cleanup();
  }
});

/**
 * Callers sharing a transfer retain their session attribution in download
 * telemetry, while cache hits add no download events.
 */
add_task(async function test_shared_download_records_each_sessions_telemetry() {
  const { hub, modelHub, cleanup } = await withModelHub({
    content: "x".repeat(1024 * 1024),
  });
  const configs = ["first", "second", "late"].map(name => ({
    ...MODEL_ARGS,
    engineId: `${name}-engine`,
    taskName: `${name}-task`,
    featureId: `${name}-feature`,
    sessionId: `${name}-session`,
    telemetryData: { modelId: `${name}-model` },
  }));
  let late;

  Services.fog.testResetFOG();
  try {
    info(
      "Request the same file from two sessions, with a third joining later."
    );
    const first = modelHub.getModelDataAsFile({
      ...configs[0],
      progressCallback: progress => {
        if (!late && progress.totalLoaded) {
          info("Join the active transfer from a third session.");
          late = modelHub.getModelDataAsFile(configs[2]);
        }
      },
    });
    await Promise.all([first, modelHub.getModelDataAsFile(configs[1])]);
    await late;

    info(
      "Complete each session and check its download events and attribution."
    );
    for (const config of configs) {
      await modelHub.notifyModelDownloadComplete(config);
    }
    const events = Glean.firefoxAiRuntime.modelDownload.testGetValue() ?? [];
    for (const config of configs) {
      const sessionEvents = events.filter(
        event => event.extra.modelDownloadId === config.sessionId
      );
      Assert.deepEqual(
        sessionEvents.map(event => event.extra.step),
        [
          "start_download",
          "start_file_download",
          "end_file_download_success",
          "end_download_success",
        ],
        `The complete lifecycle is recorded for ${config.sessionId}.`
      );
      for (const { extra } of sessionEvents) {
        Assert.equal(
          extra.engineId,
          config.engineId,
          `${config.sessionId} records its engine for ${extra.step}.`
        );
        Assert.equal(
          extra.featureId,
          config.featureId,
          `${config.sessionId} records its feature for ${extra.step}.`
        );

        if (extra.step === "end_download_success") {
          Assert.equal(
            extra.modelId,
            config.model,
            "Session completion records the model supplied by the caller."
          );
        } else {
          Assert.equal(
            extra.modelId,
            config.telemetryData.modelId,
            "Download events retain the caller's telemetry model ID."
          );
        }
      }
    }
    Assert.equal(
      hub.requestCount({ ...MODEL_FILE, method: "GET" }),
      1,
      "All sessions share one transfer."
    );

    info("Read the cached file from a new session without download telemetry.");
    await modelHub.getModelDataAsFile({
      ...configs[0],
      sessionId: "cache-session",
    });
    await modelHub.notifyModelDownloadComplete({
      ...configs[0],
      sessionId: "cache-session",
    });
    Assert.equal(
      Glean.firefoxAiRuntime.modelDownload.testGetValue().length,
      events.length,
      "A cache hit does not record download telemetry."
    );
  } finally {
    Services.fog.testResetFOG();
    await cleanup();
  }
});

/**
 * An interrupted shared transfer reports the original stream error type in
 * each caller's download telemetry.
 */
add_task(async function test_shared_failure_preserves_telemetry_error_type() {
  const { hub, modelHub, cleanup } = await withModelHub();
  hub.setFile({
    ...MODEL_FILE,
    content: "x".repeat(1024 * 1024),
    failAfterBytes: 16 * 1024,
  });
  const configs = ["first", "second"].map(name => ({
    ...MODEL_ARGS,
    engineId: `${name}-engine`,
    featureId: `${name}-feature`,
    sessionId: `${name}-session`,
  }));

  Services.fog.testResetFOG();
  try {
    info(
      "Share a transfer that the hub interrupts before the file is complete."
    );
    const results = await Promise.allSettled(
      configs.map(config => modelHub.getModelDataAsFile(config))
    );
    Assert.deepEqual(
      results.map(result => result.status),
      ["rejected", "rejected"],
      "Both callers observe the interrupted transfer."
    );

    info(
      "Complete both sessions and check that each records the stream error."
    );
    for (const config of configs) {
      await modelHub.notifyModelDownloadComplete(config);
    }
    const events = Glean.firefoxAiRuntime.modelDownload.testGetValue() ?? [];
    for (const config of configs) {
      const sessionEvents = events.filter(
        event => event.extra.modelDownloadId === config.sessionId
      );
      Assert.deepEqual(
        sessionEvents.map(event => event.extra.step),
        [
          "start_download",
          "start_file_download",
          "end_file_download_failed",
          "end_download_failed",
        ],
        "Both sessions record the failed download."
      );
      Assert.equal(
        sessionEvents.find(
          event => event.extra.step === "end_file_download_failed"
        )?.extra.error,
        "TypeError",
        "Telemetry retains the stream error type rather than the wrapper's Error type."
      );
    }
  } finally {
    Services.fog.testResetFOG();
    await cleanup();
  }
});

/**
 * A caller canceling its shared download records failure while the
 * remaining caller records a successful download.
 */
add_task(
  async function test_shared_download_telemetry_tracks_caller_cancellation() {
    const { modelHub, cleanup } = await withModelHub({
      content: "x".repeat(1024 * 1024),
    });
    const controller = new AbortController();
    const configs = ["canceled", "surviving"].map(name => ({
      ...MODEL_ARGS,
      engineId: `${name}-engine`,
      featureId: `${name}-feature`,
      sessionId: `${name}-session`,
    }));

    Services.fog.testResetFOG();
    try {
      info("Share a transfer and cancel the first caller when bytes arrive.");
      const results = await Promise.allSettled([
        modelHub.getModelDataAsFile({
          ...configs[0],
          abortSignal: controller.signal,
          progressCallback: progress => {
            if (progress.totalLoaded) {
              controller.abort();
            }
          },
        }),
        modelHub.getModelDataAsFile(configs[1]),
      ]);
      Assert.deepEqual(
        results.map(result => result.status),
        ["rejected", "fulfilled"],
        "Only the caller that canceled rejects."
      );

      info("Complete both sessions and check their independent outcomes.");
      for (const config of configs) {
        await modelHub.notifyModelDownloadComplete(config);
      }
      const events = Glean.firefoxAiRuntime.modelDownload.testGetValue() ?? [];
      for (const [index, config] of configs.entries()) {
        const outcome = index === 0 ? "failed" : "success";
        Assert.deepEqual(
          events
            .filter(event => event.extra.modelDownloadId === config.sessionId)
            .map(event => event.extra.step),
          [
            "start_download",
            "start_file_download",
            `end_file_download_${outcome}`,
            `end_download_${outcome}`,
          ],
          "Each session records its caller's outcome."
        );
      }
    } finally {
      Services.fog.testResetFOG();
      await cleanup();
    }
  }
);
