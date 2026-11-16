/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const FAKE_HUB =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data";
const FAKE_URL_TEMPLATE = "{model}/resolve/{revision}";

const MODEL_ARGS = {
  model: "acme/bert",
  revision: "main",
  taskName: "task_opfs_private_window",
};

add_task(async function test_opfs_load_with_private_window_focused() {
  const hub = new ModelHub({
    rootUrl: FAKE_HUB,
    urlTemplate: FAKE_URL_TEMPLATE,
  });
  hub.cache = await initializeCache();

  const nonPrivateWindow = Services.wm.getMostRecentBrowserWindow();
  let privateWindow;

  registerCleanupFunction(async () => {
    if (privateWindow) {
      await BrowserTestUtils.closeWindow(privateWindow);
    }
    await deleteCache(hub.cache);
  });

  const [configFile] = await hub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    file: "config.json",
  });

  Assert.greater(
    configFile.byteLength,
    0,
    "A model file loads through OPFS with only a non-private window open."
  );

  privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  await TestUtils.waitForCondition(
    () => Services.wm.getMostRecentBrowserWindow() === privateWindow,
    "The private window became the most recent browser window."
  );

  Assert.ok(
    PrivateBrowsingUtils.isWindowPrivate(
      Services.wm.getMostRecentBrowserWindow()
    ),
    "The most recent browser window is private, while a non-private window is still open."
  );

  let caughtError;
  try {
    await hub.getModelFileAsArrayBuffer({
      ...MODEL_ARGS,
      file: "tokenizer_config.json",
    });
  } catch (error) {
    caughtError = error;
  }

  info(
    `Loading a model file rejected with: ${caughtError?.name}: ${caughtError?.message}`
  );

  Assert.ok(
    caughtError,
    "Loading a model file fails while the most recent browser window is private."
  );

  Assert.equal(
    caughtError?.name,
    "SecurityError",
    "The private window is refused access to OPFS."
  );

  await BrowserTestUtils.closeWindow(privateWindow);
  privateWindow = null;

  await TestUtils.waitForCondition(
    () => Services.wm.getMostRecentBrowserWindow() === nonPrivateWindow,
    "The non-private window became the most recent browser window again."
  );

  const [vocabFile] = await hub.getModelFileAsArrayBuffer({
    ...MODEL_ARGS,
    file: "vocab.txt",
  });

  Assert.greater(
    vocabFile.byteLength,
    0,
    "The same load succeeds again once the private window is closed."
  );
});
