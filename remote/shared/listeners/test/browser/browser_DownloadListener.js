/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Note, renaming to RemoteDownloadListener for this test to avoid a naming
// clash with https://searchfox.org/mozilla-central/rev/270c20e4b063d80ce71f029b4adc4ba03a12edc0/toolkit/content/contentAreaUtils.js#166
const { DownloadListener: RemoteDownloadListener } = ChromeUtils.importESModule(
  "chrome://remote/content/shared/listeners/DownloadListener.sys.mjs"
);

const TEST_FILE =
  "https://example.com/browser/remote/shared/listeners/test/browser/sample.txt";

add_task(async function test_downloadListener() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.alwaysOpenPanel", false]],
  });

  info("Create a download before creating the listener");
  const beforeDownload = await createDownload();
  await beforeDownload.start();

  const downloadStartedEvents = [];
  const downloadStoppedEvents = [];
  const listener = new RemoteDownloadListener();
  listener.on("download-started", (eventName, data) => {
    downloadStartedEvents.push(data);
  });
  listener.on("download-stopped", (eventName, data) => {
    downloadStoppedEvents.push(data);
  });

  info("Start and stop the DownloadListener a few times in a row");
  // The DownloadListener "startListening" API is async by nature but consumers
  // should still be allowed to call it synchronously. Check that the listener
  // is resilient in case stopListening is called before startListening could
  // resolve.
  for (let i = 0; i < 3; i++) {
    listener.startListening();
    listener.stopListening();
  }

  await listener.startListening();
  is(downloadStartedEvents.length, 0, "No event recorded yet");

  info("Create and start a download");
  const download = await createDownload();
  let onDownloadComplete = download.start();

  // Wait for download to start
  await TestUtils.waitForCondition(
    () => downloadStartedEvents.length >= 1,
    "Wait for download-started event"
  );

  is(downloadStartedEvents.length, 1, "One download-started event received");
  is(
    downloadStartedEvents[0].download,
    download,
    "download-started event contains the download object"
  );

  // Wait for download to complete
  await onDownloadComplete;

  await TestUtils.waitForCondition(
    () => downloadStoppedEvents.length >= 1,
    "Wait for download-stopped event"
  );
  is(downloadStoppedEvents.length, 1, "One download-stopped event received");
  is(
    downloadStoppedEvents[0].download,
    download,
    "download-stopped event contains the download object"
  );

  info("Start another download");
  const download2 = await createDownload();
  onDownloadComplete = download2.start();

  // Wait for download to start
  await TestUtils.waitForCondition(
    () => downloadStartedEvents.length >= 2,
    "Wait for download-started event"
  );

  is(
    downloadStartedEvents.length,
    2,
    "Another download-started event received"
  );
  is(
    downloadStartedEvents[1].download,
    download2,
    "download-started event contains the download2 object"
  );

  // Wait for download to complete
  await onDownloadComplete;

  await TestUtils.waitForCondition(
    () => downloadStoppedEvents.length >= 2,
    "Wait for download-stopped event"
  );
  is(
    downloadStoppedEvents.length,
    2,
    "Another download-stopped event received"
  );
  is(
    downloadStoppedEvents[1].download,
    download2,
    "download-stopped event contains the download2 object"
  );

  listener.destroy();
});

async function createDownload() {
  const download = await Downloads.createDownload({
    source: { url: TEST_FILE },
    target: PathUtils.join(
      PathUtils.tempDir,
      `test-download-${Date.now()}.html`
    ),
  });

  const list = await Downloads.getList(Downloads.ALL);
  list.add(download);

  registerCleanupFunction(async () => {
    // Clean up the downloaded file
    if (await IOUtils.exists(download.target.path)) {
      await IOUtils.remove(download.target.path);
    }
  });

  return download;
}
