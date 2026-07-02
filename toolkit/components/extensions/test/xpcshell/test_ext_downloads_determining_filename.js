"use strict";

const { Downloads } = ChromeUtils.importESModule(
  "resource://gre/modules/Downloads.sys.mjs"
);

const { DownloadIntegration } = ChromeUtils.importESModule(
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

const gServer = createHttpServer();
gServer.registerDirectory("/data/", do_get_file("data"));

const BASE = `http://localhost:${gServer.identity.primaryPort}/data`;
const TXT_FILE = "file_download.txt";
const TXT_URL = `${BASE}/${TXT_FILE}`;
const TXT_LEN = 46;

let downloadDir;

add_task(function setup() {
  downloadDir = FileUtils.getDir("TmpD", ["downloads"]);
  downloadDir.createUnique(
    Ci.nsIFile.DIRECTORY_TYPE,
    FileUtils.PERMS_DIRECTORY
  );
  info(`Using download directory ${downloadDir.path}`);

  Services.prefs.setIntPref("browser.download.folderList", 2);
  Services.prefs.setComplexValue(
    "browser.download.dir",
    Ci.nsIFile,
    downloadDir
  );

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.download.folderList");
    Services.prefs.clearUserPref("browser.download.dir");

    let entries = downloadDir.directoryEntries;
    while (entries.hasMoreElements()) {
      let entry = entries.nextFile;
      ok(false, `Leftover file ${entry.path} in download directory`);
      entry.remove(false);
    }

    downloadDir.remove(false);
  });
});

async function waitForDownloads() {
  let list = await Downloads.getList(Downloads.ALL);
  let downloads = await list.getAll();
  await Promise.all(
    downloads.filter(dl => !dl.stopped).map(dl => dl.whenSucceeded())
  );
  for (let dl of await list.getAll()) {
    list.remove(dl);
  }
}

function fileInDownloadDir(...parts) {
  let file = downloadDir.clone();
  for (let part of parts) {
    file.append(part);
  }
  return file;
}

// Returns a simple extension that calls downloads.download() on request.
function makeCallerExtension() {
  return ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      browser.test.onMessage.addListener(async url => {
        const id = await browser.downloads.download({ url });
        browser.test.sendMessage("id", id);
      });
      browser.test.sendMessage("ready");
    },
  });
}

// Returns an extension with an onDeterminingFilename listener that forwards
// the DownloadItem to the test harness and waits for the test to send back a
// suggestion (or null/undefined for no change).
function makeListenerExtension() {
  return ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      browser.downloads.onDeterminingFilename.addListener(async item => {
        browser.test.sendMessage("determining", item);
        return new Promise(resolve =>
          browser.test.onMessage.addListener(function once(suggestion) {
            browser.test.onMessage.removeListener(once);
            resolve(suggestion);
          })
        );
      });
      browser.test.sendMessage("ready");
    },
  });
}

// ---------------------------------------------------------------------------
// Test: listener fires and receives the correct DownloadItem filename
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_fires() {
  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);

  const item = await listener.awaitMessage("determining");
  ok(
    item.filename.endsWith(TXT_FILE),
    `onDeterminingFilename received the correct filename (got ${item.filename})`
  );

  listener.sendMessage(null);

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(
    fileInDownloadDir(TXT_FILE).exists(),
    "File saved with original name when listener returns null"
  );
  fileInDownloadDir(TXT_FILE).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: the DownloadItem passed to the listener has expected properties
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_item_properties() {
  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);

  const item = await listener.awaitMessage("determining");
  ok(typeof item.id === "number", "item.id is a number");
  ok(item.url.endsWith(TXT_FILE), `item.url (${item.url}) points to the downloaded file`);
  ok(item.filename.endsWith(TXT_FILE), `item.filename (${item.filename}) ends with the expected name`);
  equal(item.state, "in_progress", "item.state is in_progress at determination time");

  listener.sendMessage(null);
  await caller.awaitMessage("id");
  await waitForDownloads();
  fileInDownloadDir(TXT_FILE).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: listener can rename the file
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_rename() {
  const RENAMED = "renamed_download.txt";

  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({ filename: RENAMED });

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(fileInDownloadDir(RENAMED).exists(), "File was saved with the suggested name");
  ok(!fileInDownloadDir(TXT_FILE).exists(), "File was not saved with the original name");
  fileInDownloadDir(RENAMED).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: listener can rename into a subdirectory
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_subdir() {
  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({ filename: "subdir/renamed.txt" });

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(
    fileInDownloadDir("subdir", "renamed.txt").exists(),
    "File was saved in the suggested subdirectory"
  );
  fileInDownloadDir("subdir", "renamed.txt").remove(false);
  fileInDownloadDir("subdir").remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: conflictAction "uniquify" when the suggested name already exists
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_conflictAction_uniquify() {
  const EXISTING = "existing.txt";
  const UNIQUE = "existing(1).txt";

  let existing = downloadDir.clone();
  existing.append(EXISTING);
  existing.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);

  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({ filename: EXISTING, conflictAction: "uniquify" });

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(fileInDownloadDir(EXISTING).exists(), "Original pre-existing file is intact");
  ok(fileInDownloadDir(UNIQUE).exists(), "Downloaded file was saved with a uniquified name");

  fileInDownloadDir(EXISTING).remove(false);
  fileInDownloadDir(UNIQUE).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: conflictAction "overwrite" when the suggested name already exists
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_conflictAction_overwrite() {
  const TARGET = "overwrite_target.txt";

  let existing = downloadDir.clone();
  existing.append(TARGET);
  existing.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  equal(existing.fileSize, 0, "pre-created file starts at zero size");

  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({ filename: TARGET, conflictAction: "overwrite" });

  await caller.awaitMessage("id");
  await waitForDownloads();

  let overwritten = fileInDownloadDir(TARGET);
  ok(overwritten.exists(), "Target file exists after download");
  equal(overwritten.fileSize, TXT_LEN, "Target file was overwritten with the downloaded content");
  overwritten.remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: an absolute path suggestion is silently rejected
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_absolute_path_rejected() {
  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({
    filename: AppConstants.platform === "win" ? "C:\\bad\\path.txt" : "/bad/path.txt",
  });

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(
    fileInDownloadDir(TXT_FILE).exists(),
    "File was saved with the original name when an absolute path was suggested"
  );
  fileInDownloadDir(TXT_FILE).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: a path-traversal suggestion (..) is silently rejected
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_traversal_rejected() {
  const listener = makeListenerExtension();
  await listener.startup();
  await listener.awaitMessage("ready");

  const caller = makeCallerExtension();
  await caller.startup();
  await caller.awaitMessage("ready");

  caller.sendMessage(TXT_URL);
  await listener.awaitMessage("determining");
  listener.sendMessage({ filename: "../traversal.txt" });

  await caller.awaitMessage("id");
  await waitForDownloads();

  ok(
    fileInDownloadDir(TXT_FILE).exists(),
    "File was saved with the original name when a path-traversal suggestion was made"
  );
  fileInDownloadDir(TXT_FILE).remove(false);

  await caller.unload();
  await listener.unload();
});

// ---------------------------------------------------------------------------
// Test: multiple listeners — the first non-null suggestion wins
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_first_suggestion_wins() {
  const FIRST = "from_first_listener.txt";
  const SECOND = "from_second_listener.txt";

  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      // This listener returns a suggestion synchronously.
      browser.downloads.onDeterminingFilename.addListener(() => ({
        filename: "from_first_listener.txt",
      }));
      // This listener also returns a suggestion, but must be ignored because
      // the first listener already provided one.
      browser.downloads.onDeterminingFilename.addListener(() => ({
        filename: "from_second_listener.txt",
      }));
      browser.test.onMessage.addListener(async url => {
        await browser.downloads.download({ url });
        browser.test.sendMessage("done");
      });
      browser.test.sendMessage("ready");
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage(TXT_URL);
  await extension.awaitMessage("done");
  await waitForDownloads();

  ok(fileInDownloadDir(FIRST).exists(), "File was saved with the first listener's suggestion");
  ok(!fileInDownloadDir(SECOND).exists(), "Second listener's suggestion was not used");
  fileInDownloadDir(FIRST).remove(false);

  await extension.unload();
});

// ---------------------------------------------------------------------------
// Test: after removeListener, the callback no longer fires
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_removeListener() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      let fired = false;
      function listener() {
        fired = true;
        return { filename: "should_not_appear.txt" };
      }
      browser.downloads.onDeterminingFilename.addListener(listener);
      browser.downloads.onDeterminingFilename.removeListener(listener);

      browser.test.onMessage.addListener(async url => {
        await browser.downloads.download({ url });
        browser.test.sendMessage("fired", fired);
      });
      browser.test.sendMessage("ready");
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage(TXT_URL);
  const fired = await extension.awaitMessage("fired");
  await waitForDownloads();

  ok(!fired, "Listener did not fire after removeListener");
  ok(
    fileInDownloadDir(TXT_FILE).exists(),
    "File was saved with the original name after the listener was removed"
  );
  fileInDownloadDir(TXT_FILE).remove(false);

  await extension.unload();
});

// ---------------------------------------------------------------------------
// Test: markFilenameAlreadyDetermined prevents listener from firing a second time
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_double_fire_guard() {
  const target = PathUtils.join(downloadDir.path, "guard_test.txt");

  let callCount = 0;
  const callback = async () => {
    callCount++;
  };
  DownloadIntegration._determineFilenameCallbacks.add(callback);

  const download = await Downloads.createDownload({
    source: TXT_URL,
    target,
  });

  // Simulate what HelperAppDlg / the saveAs path does before Download.start().
  DownloadIntegration.markFilenameAlreadyDetermined(download);

  await download.start();
  await download.whenSucceeded();

  equal(callCount, 0, "Listener not called when download is already marked as determined");

  DownloadIntegration._determineFilenameCallbacks.delete(callback);

  const list = await Downloads.getList(Downloads.ALL);
  list.remove(download);
  await IOUtils.remove(target);
});

// ---------------------------------------------------------------------------
// Test: uniquify counter increments past (1) when the first candidate is taken
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_uniquify_increments_counter() {
  for (const name of ["file.txt", "file(1).txt"]) {
    let f = downloadDir.clone();
    f.append(name);
    f.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  }

  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      browser.downloads.onDeterminingFilename.addListener(() => ({
        filename: "file.txt",
        conflictAction: "uniquify",
      }));
      browser.test.onMessage.addListener(async url => {
        await browser.downloads.download({ url });
        browser.test.sendMessage("done");
      });
      browser.test.sendMessage("ready");
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage(TXT_URL);
  await extension.awaitMessage("done");
  await waitForDownloads();

  ok(
    fileInDownloadDir("file(2).txt").exists(),
    "Download skipped to (2) when file.txt and file(1).txt were already present"
  );
  equal(
    fileInDownloadDir("file(2).txt").fileSize,
    TXT_LEN,
    "Uniquified file has the correct downloaded content"
  );

  for (const name of ["file.txt", "file(1).txt", "file(2).txt"]) {
    fileInDownloadDir(name).remove(false);
  }

  await extension.unload();
});

// ---------------------------------------------------------------------------
// Test: omitting conflictAction defaults to "uniquify"
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_default_conflict_action_is_uniquify() {
  let existing = downloadDir.clone();
  existing.append("default_conflict.txt");
  existing.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);

  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      browser.downloads.onDeterminingFilename.addListener(() => ({
        filename: "default_conflict.txt",
      }));
      browser.test.onMessage.addListener(async url => {
        await browser.downloads.download({ url });
        browser.test.sendMessage("done");
      });
      browser.test.sendMessage("ready");
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage(TXT_URL);
  await extension.awaitMessage("done");
  await waitForDownloads();

  ok(
    fileInDownloadDir("default_conflict(1).txt").exists(),
    "File was uniquified when conflictAction was not specified"
  );
  equal(
    fileInDownloadDir("default_conflict.txt").fileSize,
    0,
    "Original pre-existing file was not overwritten"
  );

  fileInDownloadDir("default_conflict.txt").remove(false);
  fileInDownloadDir("default_conflict(1).txt").remove(false);

  await extension.unload();
});

// ---------------------------------------------------------------------------
// Test: no listener registered — download proceeds normally
// ---------------------------------------------------------------------------
add_task(async function test_onDeterminingFilename_no_listener() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["downloads"] },
    background() {
      browser.test.onMessage.addListener(async url => {
        await browser.downloads.download({ url });
        browser.test.sendMessage("done");
      });
      browser.test.sendMessage("ready");
    },
  });
  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage(TXT_URL);
  await extension.awaitMessage("done");
  await waitForDownloads();

  ok(
    fileInDownloadDir(TXT_FILE).exists(),
    "File was saved normally with no onDeterminingFilename listener registered"
  );
  fileInDownloadDir(TXT_FILE).remove(false);

  await extension.unload();
});
