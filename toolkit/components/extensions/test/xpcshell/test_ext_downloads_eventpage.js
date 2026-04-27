"use strict";

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

const server = createHttpServer();
server.registerDirectory("/data/", do_get_file("data"));

const BASE = `http://localhost:${server.identity.primaryPort}/data`;
const TXT_FILE = "file_download.txt";
const TXT_URL = BASE + "/" + TXT_FILE;

add_task(function setup() {
  let downloadDir = FileUtils.getDir("TmpD", ["downloads"]);
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

add_task(
  { pref_set: [["extensions.eventPages.enabled", true]] },
  async function test_downloads_event_page() {
    await AddonTestUtils.promiseStartupManager();

    // A simple download driving extension
    let dl_extension = ExtensionTestUtils.loadExtension({
      manifest: {
        browser_specific_settings: { gecko: { id: "downloader@mochitest" } },
        permissions: ["downloads"],
        background: { persistent: false },
      },
      background() {
        let downloadId;
        browser.downloads.onChanged.addListener(async info => {
          if (info.state && info.state.current === "complete") {
            browser.test.sendMessage("downloadComplete");
          }
        });
        browser.test.onMessage.addListener(async (msg, opts) => {
          if (msg == "download") {
            downloadId = await browser.downloads.download(opts);
          }
          if (msg == "erase") {
            await browser.downloads.removeFile(downloadId);
            await browser.downloads.erase({ id: downloadId });
          }
        });
      },
    });
    await dl_extension.startup();

    let extension = ExtensionTestUtils.loadExtension({
      useAddonManager: "permanent",
      manifest: {
        permissions: ["downloads"],
        background: { persistent: false },
      },
      background() {
        browser.downloads.onChanged.addListener(() => {
          browser.test.sendMessage("onChanged");
        });
        browser.downloads.onCreated.addListener(() => {
          browser.test.sendMessage("onCreated");
        });
        browser.downloads.onErased.addListener(() => {
          browser.test.sendMessage("onErased");
        });
        browser.test.sendMessage("ready");
      },
    });

    const EVENTS = ["onChanged", "onCreated", "onErased"];

    await extension.startup();
    await extension.awaitMessage("ready");
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "downloads", event, {
        primed: false,
      });
    }

    await extension.terminateBackground({ disableResetIdleForTest: true });
    ok(
      !extension.extension.backgroundContext,
      "Background Extension context should have been destroyed"
    );

    for (let event of EVENTS) {
      assertPersistentListeners(extension, "downloads", event, {
        primed: true,
      });
    }

    // test download events waken background
    dl_extension.sendMessage("download", {
      url: TXT_URL,
      filename: TXT_FILE,
    });
    await extension.awaitMessage("ready");
    await extension.awaitMessage("onCreated");
    for (let event of EVENTS) {
      assertPersistentListeners(extension, "downloads", event, {
        primed: false,
      });
    }
    await extension.awaitMessage("onChanged");

    await extension.terminateBackground({ disableResetIdleForTest: true });
    ok(
      !extension.extension.backgroundContext,
      "Background Extension context should have been destroyed"
    );

    await dl_extension.awaitMessage("downloadComplete");
    dl_extension.sendMessage("erase");
    await extension.awaitMessage("ready");
    await extension.awaitMessage("onErased");
    await dl_extension.unload();

    // check primed listeners after startup
    await AddonTestUtils.promiseRestartManager();
    await extension.awaitStartup();

    for (let event of EVENTS) {
      assertPersistentListeners(extension, "downloads", event, {
        primed: true,
      });
    }

    await extension.unload();
  }
);
