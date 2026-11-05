/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const DELETE_PRIVATE_PREF = "browser.download.deletePrivate";
const PRIVATE_CHOSEN_PREF = "browser.download.deletePrivate.chosen";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.enableDeletePrivate", true]],
  });
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(DELETE_PRIVATE_PREF);
  Services.prefs.clearUserPref(PRIVATE_CHOSEN_PREF);
});

async function openPrivateChoiceSubview(privateWindow) {
  let subViewShown = BrowserTestUtils.waitForEvent(
    privateWindow.PrivateDownloadsSubview.subview,
    "ViewShown"
  );
  privateWindow.DownloadsPanel.showPanel();
  await subViewShown;
}

async function waitForPrivateChoiceSubviewDismissed(privateWindow) {
  await TestUtils.waitForCondition(
    () =>
      !privateWindow.PrivateDownloadsSubview.mainView.hasAttribute(
        "showing-private-browsing-choice"
      ),
    "Waiting for Private Browsing file deletion choice subview to be dismissed."
  );
}

add_task(async function test_delete_button_sets_both_prefs() {
  Services.prefs.clearUserPref(DELETE_PRIVATE_PREF);
  Services.prefs.clearUserPref(PRIVATE_CHOSEN_PREF);

  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  await task_addDownloads([{ state: DownloadsCommon.DOWNLOAD_FINISHED }]);
  await openPrivateChoiceSubview(privateWindow);

  EventUtils.synthesizeMouseAtCenter(
    privateWindow.PrivateDownloadsSubview.deleteButton,
    {},
    privateWindow
  );
  await waitForPrivateChoiceSubviewDismissed(privateWindow);

  ok(
    Services.prefs.getBoolPref(DELETE_PRIVATE_PREF, false),
    "Delete button should set browser.download.deletePrivate"
  );
  ok(
    Services.prefs.getBoolPref(PRIVATE_CHOSEN_PREF, false),
    "Delete button should set browser.download.deletePrivate.chosen"
  );

  await BrowserTestUtils.closeWindow(privateWindow);
  await task_resetState();
});

add_task(async function test_accept_button_only_sets_chosen_pref() {
  Services.prefs.clearUserPref(DELETE_PRIVATE_PREF);
  Services.prefs.clearUserPref(PRIVATE_CHOSEN_PREF);

  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  await task_addDownloads([{ state: DownloadsCommon.DOWNLOAD_FINISHED }]);
  await openPrivateChoiceSubview(privateWindow);

  EventUtils.synthesizeMouseAtCenter(
    privateWindow.PrivateDownloadsSubview.acceptButton,
    {},
    privateWindow
  );
  await waitForPrivateChoiceSubviewDismissed(privateWindow);

  ok(
    !Services.prefs.getBoolPref(DELETE_PRIVATE_PREF, false),
    "Accept button should not set browser.download.deletePrivate"
  );
  ok(
    Services.prefs.getBoolPref(PRIVATE_CHOSEN_PREF, false),
    "Accept button should set browser.download.deletePrivate.chosen"
  );

  await BrowserTestUtils.closeWindow(privateWindow);
  await task_resetState();
});
