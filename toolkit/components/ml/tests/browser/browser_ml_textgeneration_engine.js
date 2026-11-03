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
