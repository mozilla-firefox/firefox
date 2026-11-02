/**
 * Bug 1663992 - Testing the 'Save Image As' works in an image document if the
 *               image is blocked by social tracker.
 */
"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/content/tests/browser/common/mockTransfer.js",
  this
);

const TEST_IMAGE_URL =
  "http://social-tracking.example.org/browser/toolkit/components/antitracking/test/browser/raptor.jpg";

let MockFilePicker = SpecialPowers.MockFilePicker;
MockFilePicker.init();

const tempDir = createTemporarySaveDirectory();
MockFilePicker.displayDirectory = tempDir;

function createTemporarySaveDirectory() {
  let saveDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  saveDir.append("testsavedir");
  saveDir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  return saveDir;
}

function createPromiseForTransferComplete() {
  return new Promise(resolve => {
    MockFilePicker.showCallback = fp => {
      info("MockFilePicker showCallback");

      let fileName = fp.defaultString;
      let destFile = tempDir.clone();
      destFile.append(fileName);

      MockFilePicker.setFiles([destFile]);
      MockFilePicker.filterIndex = 0; // kSaveAsType_Complete

      MockFilePicker.showCallback = null;
      mockTransferCallback = function (downloadSuccess) {
        ok(downloadSuccess, "Image should have been downloaded successfully");
        mockTransferCallback = () => {};
        resolve();
      };
    };
  });
}

add_setup(async function () {
  info("Setting up the prefs.");

  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.socialtracking.enabled", true],
      [
        "urlclassifier.features.socialtracking.blacklistHosts",
        "social-tracking.example.org",
      ],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();

  info("Setting MockFilePicker.");
  mockTransferRegisterer.register();

  registerCleanupFunction(async function () {
    await UrlClassifierTestUtils.cleanupTestTrackers();
    mockTransferRegisterer.unregister();
    MockFilePicker.cleanup();
    tempDir.remove(true);
  });
});

add_task(async function () {
  info("Open a new tab for testing");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_IMAGE_URL
  );

  let popupShownPromise = BrowserTestUtils.waitForEvent(document, "popupshown");

  let browser = gBrowser.selectedBrowser;

  info("Open the context menu.");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "img",
    {
      type: "contextmenu",
      button: 2,
    },
    browser
  );

  await popupShownPromise;

  let transferCompletePromise = createPromiseForTransferComplete();
  let saveElement = document.getElementById(`context-saveimage`);
  info("Triggering the save process.");
  saveElement.doCommand();

  info("Wait until the save is finished.");
  await transferCompletePromise;

  info("Close the context menu.");
  let contextMenu = document.getElementById("contentAreaContextMenu");
  let popupHiddenPromise = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popuphidden"
  );
  contextMenu.hidePopup();
  await popupHiddenPromise;

  BrowserTestUtils.removeTab(tab);
});
