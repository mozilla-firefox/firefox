/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  DownloadSpamProtection:
    "moz-src:///browser/components/downloads/DownloadSpamProtection.sys.mjs",
  PermissionTestUtils: "resource://testing-common/PermissionTestUtils.sys.mjs",
});

const TEST_URI = "https://example.com";

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  TEST_URI
);

add_setup(async function () {
  // Create temp directory
  let time = new Date().getTime();
  let tempDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  tempDir.append(time);
  Services.prefs.setIntPref("browser.download.folderList", 2);
  Services.prefs.setComplexValue("browser.download.dir", Ci.nsIFile, tempDir);

  PermissionTestUtils.add(
    TEST_URI,
    "automatic-download",
    Services.perms.UNKNOWN_ACTION
  );
  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.enable_spam_prevention", true]],
    clear: [
      ["browser.download.alwaysOpenPanel"],
      ["browser.download.always_ask_before_handling_new_types"],
    ],
  });

  registerCleanupFunction(async () => {
    PermissionTestUtils.remove(TEST_URI, "automatic-download");
    Services.prefs.clearUserPref("browser.download.folderList");
    Services.prefs.clearUserPref("browser.download.dir");
    await IOUtils.remove(tempDir.path, { recursive: true });
  });
});

add_task(async function check_user_activated_download_is_not_spam() {
  await task_resetState();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PATH + "test_user_activated_download_page.html"
  );

  let publicList = await Downloads.getList(Downloads.PUBLIC);
  let downloadFinished = promiseDownloadFinished(publicList);
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#download-button",
    {},
    tab.linkedBrowser
  );
  let download = await downloadFinished;

  ok(download.succeeded, "Download started after a user click succeeded");
  is(
    PermissionTestUtils.testPermission(TEST_URI, "automatic-download"),
    Services.perms.UNKNOWN_ACTION,
    "A download started during user activation does not make the site prompt"
  );

  await publicList.removeFinished();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function check_download_spam_ui() {
  await task_resetState();

  let browserWin = BrowserWindowTracker.getTopWindow();
  registerCleanupFunction(async () => {
    for (let win of [browserWin, browserWin2]) {
      win.DownloadsPanel.hidePanel();
      DownloadIntegration.downloadSpamProtection.removeDownloadSpamForWindow(
        TEST_URI,
        win
      );
    }
    let publicList = await Downloads.getList(Downloads.PUBLIC);
    await publicList.removeFinished();
    BrowserTestUtils.removeTab(newTab);
    await BrowserTestUtils.closeWindow(browserWin2);
  });
  let observedBlockedDownloads = 0;
  let gotAllBlockedDownloads = TestUtils.topicObserved(
    "blocked-automatic-download",
    () => {
      return ++observedBlockedDownloads >= 99;
    }
  );

  let newTab = await BrowserTestUtils.openNewForegroundTab(
    browserWin.gBrowser,
    TEST_PATH + "test_spammy_page.html"
  );

  info("Waiting on all blocked downloads");
  await gotAllBlockedDownloads;

  let { downloadSpamProtection } = DownloadIntegration;
  let spamList = downloadSpamProtection.getSpamListForWindow(browserWin);
  is(
    spamList._downloads[0].blockedDownloadsCount,
    99,
    "99 blocked downloads recorded"
  );
  ok(
    spamList._downloads[0].error.becauseBlockedByReputationCheck,
    "Download blocked because of reputation"
  );
  is(
    spamList._downloads[0].error.reputationCheckVerdict,
    "DownloadSpam",
    "Verdict is DownloadSpam"
  );

  browserWin.focus();
  await BrowserTestUtils.waitForPopupEvent(
    browserWin.DownloadsPanel.panel,
    "shown"
  );

  ok(browserWin.DownloadsPanel.isPanelShowing, "Download panel should open");
  await Downloads.getList(Downloads.PUBLIC);

  let listbox = browserWin.document.getElementById("downloadsListBox");
  ok(listbox, "Download list box present");

  await TestUtils.waitForCondition(() => {
    return listbox.childElementCount == 2 && !listbox.getAttribute("disabled");
  }, "2 downloads = 1 allowed download and 1 for 99 downloads blocked");

  let spamElement = listbox.itemChildren[0].classList.contains(
    "temporary-block"
  )
    ? listbox.itemChildren[0]
    : listbox.itemChildren[1];

  ok(spamElement.classList.contains("temporary-block"), "Download is blocked");

  info("Testing spam protection in a second window");

  browserWin.DownloadsPanel.hidePanel();
  DownloadIntegration.downloadSpamProtection.removeDownloadSpamForWindow(
    TEST_URI,
    browserWin
  );

  ok(
    !browserWin.DownloadsPanel.isPanelShowing,
    "Download panel should be closed in first window"
  );
  is(
    listbox.childElementCount,
    1,
    "First window's download list should have one item - the download that wasn't blocked"
  );

  let browserWin2 = await BrowserTestUtils.openNewBrowserWindow();
  let observedBlockedDownloads2 = 0;
  let gotAllBlockedDownloads2 = TestUtils.topicObserved(
    "blocked-automatic-download",
    () => {
      return ++observedBlockedDownloads2 >= 100;
    }
  );

  await BrowserTestUtils.openNewForegroundTab(
    browserWin2.gBrowser,
    TEST_PATH + "test_spammy_page.html"
  );

  info("Waiting on all blocked downloads in second window");
  await gotAllBlockedDownloads2;

  let spamList2 = downloadSpamProtection.getSpamListForWindow(browserWin2);
  is(
    spamList2._downloads[0].blockedDownloadsCount,
    100,
    "100 blocked downloads recorded in second window"
  );
  ok(
    !spamList._downloads[0]?.blockedDownloadsCount,
    "No blocked downloads in first window"
  );

  browserWin2.focus();
  await BrowserTestUtils.waitForPopupEvent(
    browserWin2.DownloadsPanel.panel,
    "shown"
  );

  ok(
    browserWin2.DownloadsPanel.isPanelShowing,
    "Download panel should open in second window"
  );

  ok(
    !browserWin.DownloadsPanel.isPanelShowing,
    "Download panel should not open in first window"
  );

  let listbox2 = browserWin2.document.getElementById("downloadsListBox");
  ok(listbox2, "Download list box present");

  await TestUtils.waitForCondition(() => {
    return (
      listbox2.childElementCount == 2 && !listbox2.getAttribute("disabled")
    );
  }, "2 downloads = 1 allowed download from first window, and 1 for 100 downloads blocked in second window");

  is(
    listbox.childElementCount,
    1,
    "First window's download list should still have one item - the download that wasn't blocked"
  );

  let spamElement2 = listbox2.itemChildren[0].classList.contains(
    "temporary-block"
  )
    ? listbox2.itemChildren[0]
    : listbox2.itemChildren[1];

  ok(spamElement2.classList.contains("temporary-block"), "Download is blocked");
});
