/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var MockFilePicker = SpecialPowers.MockFilePicker;
MockFilePicker.init(window.browsingContext);

/**
 * Tests if saving a response to a file works..
 */

add_task(async function () {
  const { tab, monitor } = await initNetMonitor(
    CONTENT_TYPE_WITHOUT_CACHE_URL,
    { requestCount: 1 }
  );
  info("Starting test... ");

  const { document } = monitor.panelWin;

  // Execute requests.
  await performRequests(monitor, tab, CONTENT_TYPE_WITHOUT_CACHE_REQUESTS);

  // Create the folder the gzip file will be saved into
  const destDir = createTemporarySaveDirectory();
  let destFile;

  MockFilePicker.displayDirectory = destDir;
  const saveDialogClosedPromise = new Promise(resolve => {
    MockFilePicker.showCallback = function (fp) {
      info("MockFilePicker showCallback");
      const fileName = fp.defaultString;
      destFile = destDir.clone();
      destFile.append(fileName);
      MockFilePicker.setFiles([destFile]);

      resolve(destFile.path);
    };
  });

  registerCleanupFunction(function () {
    MockFilePicker.cleanup();
  });

  // Select gzip request.

  await triggerSaveResponseAs(
    monitor,
    document.querySelectorAll(".request-list-item")[6]
  );

  info("Wait for the save dialog to close");
  const savedPath = await saveDialogClosedPromise;

  const expectedFile = destDir.clone();
  expectedFile.append("sjs_content-type-test-server.sjs");

  is(savedPath, expectedFile.path, "Response was saved to correct path");

  await waitForFileSavedToDisk(savedPath);

  const buffer = await IOUtils.read(savedPath);
  const savedFileContent = new TextDecoder().decode(buffer);

  // The content is set by https://searchfox.org/mozilla-central/source/devtools/client/netmonitor/test/sjs_content-type-test-server.sjs#360
  // (the "gzip" case)
  is(
    savedFileContent,
    new Array(1000).join("Hello gzip!"),
    "Saved response has the correct text"
  );

  await teardown(monitor);
});
