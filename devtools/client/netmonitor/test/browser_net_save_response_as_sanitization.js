/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var MockFilePicker = SpecialPowers.MockFilePicker;
MockFilePicker.init(window.browsingContext);

/**
 * Tests that filenames are sanitized when using Save Response As
 */

function setupTestServer() {
  const httpServer = createTestHTTPServer();
  httpServer.registerContentType("html", "text/html");

  httpServer.registerPathHandler("/index.html", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(`<!DOCTYPE html>
    <html><body><h1>Test sanitization for save response as
    <script>fetch("test.url");</script>
    <script>fetch("test2.url");</script>
    `);
  });

  httpServer.registerPathHandler("/test.url", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/plain", false);
    response.write("dummy content");
  });
  httpServer.registerPathHandler("/test2.url", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/plain", false);
    response.write("dummy content 2");
  });

  return httpServer;
}

add_task(async function () {
  const httpServer = setupTestServer();
  const port = httpServer.identity.primaryPort;

  const { monitor } = await initNetMonitor(
    `http://localhost:${port}/index.html`,
    {
      requestCount: 3,
    }
  );

  info("Starting test... ");
  const { document } = monitor.panelWin;

  info("Reload the browser to show the 2 requests for the page");
  const networkEvent = waitForNetworkEvents(monitor, 3);
  await reloadBrowser();
  await networkEvent;

  // Create the folder the gzip file will be saved into
  const destDir = createTemporarySaveDirectory();
  let destFile;

  // Prepare the MockFilePicker
  MockFilePicker.displayDirectory = destDir;
  registerCleanupFunction(function () {
    MockFilePicker.cleanup();
  });

  info("Prepare a file picker mock which will use the default filename");
  let saveDialogClosedPromise = new Promise(resolve => {
    MockFilePicker.showCallback = function (fp) {
      info("MockFilePicker showCallback - preserve .download extension");
      const fileName = fp.defaultString;
      destFile = destDir.clone();
      destFile.append(fileName);
      MockFilePicker.setFiles([destFile]);

      resolve(destFile.path);
    };
  });

  info("Save response as for the test.url request");
  await triggerSaveResponseAs(
    monitor,
    document.querySelectorAll(".request-list-item")[1]
  );

  info("Wait for the save dialog to close");
  const filePickerPath = await saveDialogClosedPromise;

  const expectedFile = destDir.clone();
  expectedFile.append("test.url.download");
  is(
    filePickerPath,
    expectedFile.path,
    "File picker default filename was set to the expected value"
  );

  await waitForFileSavedToDisk(expectedFile.path);

  info("Prepare a file picker mock which will override the default filename");
  saveDialogClosedPromise = new Promise(resolve => {
    MockFilePicker.showCallback = function (fp) {
      info("MockFilePicker showCallback - strip .download extension");
      const fileName = fp.defaultString;
      destFile = destDir.clone();
      destFile.append(fileName.replace(".download", ""));
      MockFilePicker.setFiles([destFile]);

      resolve(destFile.path);
    };
  });

  info("Save response as for test2.url");
  await triggerSaveResponseAs(
    monitor,
    document.querySelectorAll(".request-list-item")[2]
  );

  info("Wait for the save dialog to close");
  const updatedFilePickerPath = await saveDialogClosedPromise;

  const invalidFile = destDir.clone();
  invalidFile.append("test2.url");

  const expectedFile2 = destDir.clone();
  expectedFile2.append("test2.url.download");

  is(
    updatedFilePickerPath,
    invalidFile.path,
    "File picker filename was updated to an invalid path while saving"
  );

  // Check that the valid path was still used to save the file.
  await waitForFileSavedToDisk(expectedFile2.path);

  ok(
    !(await IOUtils.exists(invalidFile.path)),
    "No file was saved for the invalid path"
  );

  await teardown(monitor);
});
