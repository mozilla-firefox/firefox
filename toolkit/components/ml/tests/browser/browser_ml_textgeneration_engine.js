/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { Progress } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

add_task(async function test_engine_download_notifications() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  const sandbox = sinon.createSandbox();
  const sentinel = new Error("stop after download");
  mockModelHubModel(sandbox, {
    id: "https://example.com/model.gguf",
    model: TINYSTORIES_ENGINE_OPTIONS.modelId,
    error: sentinel,
  });

  const notifications = [];
  try {
    await Assert.rejects(
      createEngine(
        { ...TINYSTORIES_ENGINE_OPTIONS, engineId: "hwi-wiring-notifications" },
        data => notifications.push(data)
      ),
      error => error === sentinel,
      "Engine creation surfaces the download failure"
    );

    Assert.equal(
      notifications.length,
      3,
      "Every hub progress payload reached the notifications callback"
    );
    for (const notification of notifications) {
      Assert.equal(
        notification.type,
        Progress.ProgressType.DOWNLOAD,
        "The payload carries the download progress type"
      );
    }
    const [initiate, inProgress, done] = notifications;
    Assert.equal(initiate.statusText, Progress.ProgressStatusText.INITIATE);
    Assert.equal(
      inProgress.statusText,
      Progress.ProgressStatusText.IN_PROGRESS
    );
    Assert.equal(inProgress.totalLoaded, 256);
    Assert.equal(inProgress.total, 1024);
    Assert.equal(
      Math.round((100 * inProgress.totalLoaded) / inProgress.total),
      25,
      "The LinkPreviewModel.onDownload percentage math works on the payload"
    );
    Assert.equal(
      done.statusText,
      Progress.ProgressStatusText.DONE,
      "The terminal payload carries the DONE status LinkPreviewModel keys on"
    );
    Assert.equal(done.totalLoaded, 1024);
  } finally {
    sandbox.restore();
    await cleanup();
  }
});

add_task(async function test_engine_abort_signal() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  const sandbox = sinon.createSandbox();
  const hubStub = sandbox.stub(ModelHub.prototype, "getModelDataAsFile");
  try {
    const preAborted = new AbortController();
    preAborted.abort();
    await Assert.rejects(
      createEngine(
        { ...TINYSTORIES_ENGINE_OPTIONS, engineId: "hwi-wiring-abort" },
        null,
        preAborted.signal
      ),
      error => error.name === "AbortError",
      "An already-aborted signal rejects engine creation"
    );
    Assert.equal(
      hubStub.callCount,
      0,
      "No download is attempted for an aborted signal"
    );

    const sentinel = new Error("stop after download");
    let receivedSignal = null;
    hubStub.callsFake(async function ({ abortSignal }) {
      receivedSignal = abortSignal;
      throw sentinel;
    });
    const live = new AbortController();
    await Assert.rejects(
      createEngine(
        { ...TINYSTORIES_ENGINE_OPTIONS, engineId: "hwi-wiring-abort-live" },
        null,
        live.signal
      ),
      error => error === sentinel,
      "Engine creation surfaces the download failure"
    );
    Assert.equal(
      receivedSignal,
      live.signal,
      "The caller's abort signal is the one handed to the model download"
    );
  } finally {
    sandbox.restore();
    await cleanup();
  }
});

add_task(async function test_engine_device() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  let engine;
  try {
    // Falls back to the CPU on a machine with no accelerator.
    engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-device-gpu",
      device: { kinds: ["gpu", "cpu"] },
    });
    const text = await collectGeneratedText(
      engine.runWithGenerator({
        prompt: TINYSTORIES_STORYTELLER_PROMPT,
        nPredict: 4,
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
      })
    );
    Assert.greater(text.length, 0, "A GPU-first placement generates");

    await Assert.rejects(
      createEngine({
        ...TINYSTORIES_ENGINE_OPTIONS,
        engineId: "hwi-device-none",
        device: { devices: ["no-such-device"] },
      }),
      /no device matches/,
      "A placement no device matches rejects"
    );
  } finally {
    await engine?.terminate();
    await cleanup();
  }
});

add_task(async function test_engine_chat_completion_request() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  let engine;
  try {
    engine = await createEngine({
      ...TINYSTORIES_ENGINE_OPTIONS,
      engineId: "hwi-chat-completion",
    });

    const result = await engine.run({
      args: TINYSTORIES_STORYTELLER_PROMPT,
      token: "unused",
      chatId: "unused",
      inferenceParams: { temperature: 0, max_completion_tokens: 4 },
    });
    Assert.greater(result.finalOutput.length, 0, "A completion run generates");
    Assert.lessOrEqual(
      result.metrics.outputTokens,
      4,
      "max_completion_tokens bounds the output"
    );
    Assert.equal(
      result.finishReason,
      "length",
      "A run that hits its budget says so"
    );

    const text = await collectGeneratedText(
      engine.runWithGenerator({
        args: TINYSTORIES_STORYTELLER_PROMPT,
        streamOptions: { enabled: true },
        inferenceParams: {
          temperature: 0.7,
          top_p: 0.9,
          max_tokens: 4,
        },
      })
    );
    Assert.greater(text.length, 0, "A streamed completion run yields text");

    const greedy = await engine.run({
      args: TINYSTORIES_STORYTELLER_PROMPT,
      inferenceParams: { temperature: 0, max_completion_tokens: 8 },
    });
    const chained = await engine.run({
      args: TINYSTORIES_STORYTELLER_PROMPT,
      inferenceParams: {
        samplers: TINYSTORIES_GREEDY_SAMPLERS,
        max_completion_tokens: 8,
      },
    });
    Assert.equal(
      chained.finalOutput,
      greedy.finalOutput,
      "A samplers chain in the parameters drives the run"
    );
    const adjusted = await engine.run({
      args: TINYSTORIES_STORYTELLER_PROMPT,
      inferenceParams: {
        samplers: [{ type: "temperature", temp: 1 }, { type: "dist" }],
        temperature: 0,
        max_completion_tokens: 8,
      },
    });
    Assert.equal(
      adjusted.finalOutput,
      greedy.finalOutput,
      "temperature adjusts the chain"
    );

    await Assert.rejects(
      engine.run({
        args: TINYSTORIES_STORYTELLER_PROMPT,
        tools: [{ type: "function", function: { name: "search" } }],
      }),
      /tools are not supported/,
      "A request with tools rejects"
    );
    await Assert.rejects(
      engine.run({
        args: [
          { role: "assistant", content: "", tool_calls: [{ id: "call" }] },
        ],
      }),
      /tool calls are not supported/,
      "A message with tool calls rejects"
    );
    await Assert.rejects(
      engine.run({ args: [{ role: "tool", content: "result" }] }),
      /unsupported message role "tool"/,
      "A tool role rejects"
    );
    await Assert.rejects(
      engine.run({ args: [{ role: "user", content: [{ type: "text" }] }] }),
      /content must be a string/,
      "Non-string content rejects"
    );

    const controller = new AbortController();
    const cancel = sinon.spy(engine, "cancel");
    const aborted = engine.run(
      { args: TINYSTORIES_STORYTELLER_PROMPT },
      { signal: controller.signal }
    );
    controller.abort();
    await Assert.rejects(
      aborted,
      error => error.name === "AbortError",
      "An aborted run rejects with the signal's reason"
    );
    Assert.ok(cancel.calledOnce, "The abort stops the generation");
    await Assert.rejects(
      engine.run(
        { args: TINYSTORIES_STORYTELLER_PROMPT },
        { signal: controller.signal }
      ),
      error => error.name === "AbortError",
      "An aborted signal rejects before generating"
    );
    Assert.ok(cancel.calledOnce, "No generation started");
    cancel.restore();

    const after = await engine.run({
      prompt: TINYSTORIES_STORYTELLER_PROMPT,
      nPredict: 4,
      samplers: TINYSTORIES_GREEDY_SAMPLERS,
    });
    Assert.greater(
      after.finalOutput.length,
      0,
      "The llama request shape still runs after rejected requests"
    );
  } finally {
    await engine?.terminate();
    await cleanup();
  }
});
