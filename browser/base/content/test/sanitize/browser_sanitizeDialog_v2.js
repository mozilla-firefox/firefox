/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Tests the sanitize dialog (a.k.a. the clear recent history dialog).
 * See bug 480169.
 *
 * The purpose of this test is not to fully flex the sanitize timespan code;
 * browser/base/content/test/sanitize/browser_sanitize-timespans.js does that.  This
 * test checks the UI of the dialog and makes sure it's correctly connected to
 * the sanitize timespan code.
 *
 * Some of this code, especially the history creation parts, was taken from
 * browser/base/content/test/sanitize/browser_sanitize-timespans.js.
 */
ChromeUtils.defineESModuleGetters(this, {
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  Timer: "resource://gre/modules/Timer.sys.mjs",
  PermissionTestUtils: "resource://testing-common/PermissionTestUtils.sys.mjs",
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
});

/**
 * Ensures that the specified URIs are either cleared or not.
 *
 * @param aURIs
 *        Array of page URIs
 * @param aShouldBeCleared
 *        True if each visit to the URI should be cleared, false otherwise
 */
async function promiseHistoryClearedState(aURIs, aShouldBeCleared) {
  for (let uri of aURIs) {
    let visited = await PlacesUtils.history.hasVisits(uri);
    Assert.equal(
      visited,
      !aShouldBeCleared,
      `history visit ${uri.spec} should ${
        aShouldBeCleared ? "no longer" : "still"
      } exist`
    );
  }
}

/**
 * Ensures that the given pref is the expected value.
 *
 * @param {String} aPrefName
 *        The pref's sub-branch under the privacy branch
 * @param {Boolean} aExpectedVal
 *        The pref's expected value
 * @param {String} aMsg
 *        Passed to is()
 */
function boolPrefIs(aPrefName, aExpectedVal, aMsg) {
  is(Services.prefs.getBoolPref("privacy." + aPrefName), aExpectedVal, aMsg);
}

/**
 * Checks to see if the download with the specified path exists.
 *
 * @param  aPath
 *         The path of the download to check
 * @return True if the download exists, false otherwise
 */
async function downloadExists(aPath) {
  let publicList = await Downloads.getList(Downloads.PUBLIC);
  let listArray = await publicList.getAll();
  return listArray.some(i => i.target.path == aPath);
}

/**
 * Ensures that the specified downloads are either cleared or not.
 *
 * @param aDownloadIDs
 *        Array of download database IDs
 * @param aShouldBeCleared
 *        True if each download should be cleared, false otherwise
 */
async function ensureDownloadsClearedState(aDownloadIDs, aShouldBeCleared) {
  let niceStr = aShouldBeCleared ? "no longer" : "still";
  for (let id of aDownloadIDs) {
    is(
      await downloadExists(id),
      !aShouldBeCleared,
      "download " + id + " should " + niceStr + " exist"
    );
  }
}

/**
 * Checks if a form entry exists.
 */
async function formNameExists(name) {
  return !!(await FormHistory.count({ fieldname: name }));
}

/**
 * Adds a form entry to history.
 *
 * @param aMinutesAgo
 *        The entry will be added this many minutes ago
 */
function promiseAddFormEntryWithMinutesAgo(aMinutesAgo) {
  let name = aMinutesAgo + "-minutes-ago";

  // Artifically age the entry to the proper vintage.
  let timestamp = nowUSec - aMinutesAgo * kUsecPerMin;

  return FormHistory.update({
    op: "add",
    fieldname: name,
    value: "dummy",
    firstUsed: timestamp,
  });
}

/**
 * Adds a download to history.
 *
 * @param aMinutesAgo
 *        The download will be downloaded this many minutes ago
 */
async function addDownloadWithMinutesAgo(aExpectedPathList, aMinutesAgo) {
  let publicList = await Downloads.getList(Downloads.PUBLIC);

  let name = "fakefile-" + aMinutesAgo + "-minutes-ago";
  let download = await Downloads.createDownload({
    source: "https://bugzilla.mozilla.org/show_bug.cgi?id=480169",
    target: name,
  });
  download.startTime = new Date(nowMSec - aMinutesAgo * kMsecPerMin);
  download.canceled = true;
  publicList.add(download);

  ok(
    await downloadExists(name),
    "Sanity check: download " + name + " should exist after creating it"
  );

  aExpectedPathList.push(name);
}

add_setup(async function () {
  requestLongerTimeout(3);
  await blankSlate();
  registerCleanupFunction(async function () {
    await blankSlate();
    await PlacesTestUtils.promiseAsyncUpdates();
  });
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.sanitize.useOldClearHistoryDialog", false]],
  });

  // open preferences to trigger an updateSites()
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

/**
 * Ensures that the given pref is the expected value.
 *
 * @param aPrefName
 *        The pref's sub-branch under the privacy branch
 * @param aExpectedVal
 *        The pref's expected value
 * @param aMsg
 *        Passed to is()
 */
function intPrefIs(aPrefName, aExpectedVal, aMsg) {
  is(Services.prefs.getIntPref("privacy." + aPrefName), aExpectedVal, aMsg);
}

/**
 * Creates a visit time.
 *
 * @param aMinutesAgo
 *        The visit will be visited this many minutes ago
 */
function visitTimeForMinutesAgo(aMinutesAgo) {
  return nowUSec - aMinutesAgo * kUsecPerMin;
}

/**
 *
 * Opens dialog in the provided context and selects the checkboxes
 * as sent in the parameters
 *
 * @param {Object} context the dialog is opened in, timespan to select,
 *  if browsingHistoryAndDownloads, cookiesAndStorage, cache or siteSettings
 *  are checked
 */
async function performActionsOnDialog({
  context = "browser",
  timespan = Sanitizer.TIMESPAN_HOUR,
  browsingHistoryAndDownloads = true,
  cookiesAndStorage = true,
  cache = false,
  siteSettings = false,
  formData = false,
}) {
  let dh = new ClearHistoryDialogHelper({ mode: context });
  dh.onload = function () {
    this.selectDuration(timespan);
    this.checkPrefCheckbox(
      "browsingHistoryAndDownloads",
      browsingHistoryAndDownloads
    );
    this.checkPrefCheckbox("cookiesAndStorage", cookiesAndStorage);
    this.checkPrefCheckbox("cache", cache);
    this.checkPrefCheckbox("siteSettings", siteSettings);
    this.checkPrefCheckbox("formdata", formData);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;
}

/**
 * Initializes the dialog to its default state.
 */
add_task(async function default_state() {
  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    // Select "Last Hour"
    this.selectDuration(Sanitizer.TIMESPAN_HOUR);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Cancels the dialog, makes sure history not cleared.
 */
add_task(async function test_cancel() {
  // Add history (within the past hour)
  let uris = [];
  let places = [];
  let pURI;
  for (let i = 0; i < 30; i++) {
    pURI = makeURI("https://" + i + "-minutes-ago.com/");
    places.push({ uri: pURI, visitDate: visitTimeForMinutesAgo(i) });
    uris.push(pURI);
  }
  await PlacesTestUtils.addVisits(places);

  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    this.selectDuration(Sanitizer.TIMESPAN_HOUR);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", false);
    this.cancelDialog();
  };
  dh.onunload = async function () {
    await promiseHistoryClearedState(uris, false);
    await blankSlate();
    await promiseHistoryClearedState(uris, true);
  };
  dh.open();
  await dh.promiseClosed;
});

// test remembering user options for various entry points
add_task(async function test_pref_remembering() {
  let dh = new ClearHistoryDialogHelper({ mode: "clearSiteData" });
  dh.onload = function () {
    this.checkPrefCheckbox("cookiesAndStorage", false);
    this.checkPrefCheckbox("siteSettings", true);

    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  // validate if prefs are remembered
  dh = new ClearHistoryDialogHelper({ mode: "clearSiteData" });
  dh.onload = function () {
    this.validateCheckbox("cookiesAndStorage", false);
    this.validateCheckbox("siteSettings", true);

    this.checkPrefCheckbox("cookiesAndStorage", true);
    this.checkPrefCheckbox("siteSettings", false);

    // we will test cancelling the dialog, to make sure it doesn't remember
    // the prefs when cancelled
    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;

  // validate if prefs did not change since we cancelled the dialog
  dh = new ClearHistoryDialogHelper({ mode: "clearSiteData" });
  dh.onload = function () {
    this.validateCheckbox("cookiesAndStorage", false);
    this.validateCheckbox("siteSettings", true);

    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;

  // test rememebering prefs from the clear history context
  // since clear history and clear site data have seperate remembering
  // of prefs
  dh = new ClearHistoryDialogHelper({ mode: "clearHistory" });
  dh.onload = function () {
    this.checkPrefCheckbox("cookiesAndStorage", true);
    this.checkPrefCheckbox("siteSettings", false);
    this.checkPrefCheckbox("cache", false);

    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  // validate if prefs are remembered across both clear history and browser
  dh = new ClearHistoryDialogHelper({ mode: "browser" });
  dh.onload = function () {
    this.validateCheckbox("cookiesAndStorage", true);
    this.validateCheckbox("siteSettings", false);
    this.validateCheckbox("cache", false);

    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Ensures that the "Everything" duration option works.
 */
add_task(async function test_everything() {
  // Add history.
  let uris = [];
  let places = [];
  let pURI;
  // within past hour, within past two hours, within past four hours and
  // outside past four hours
  [10, 70, 130, 250].forEach(function (aValue) {
    pURI = makeURI("https://" + aValue + "-minutes-ago.com/");
    places.push({ uri: pURI, visitDate: visitTimeForMinutesAgo(aValue) });
    uris.push(pURI);
  });

  let promiseSanitized = promiseSanitizationComplete();

  await PlacesTestUtils.addVisits(places);
  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    is(
      this.isWarningPanelVisible(),
      false,
      "Warning panel should be hidden after previously accepting dialog " +
        "with a predefined timespan"
    );
    this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", true);
    this.acceptDialog();
  };
  dh.onunload = async function () {
    await promiseSanitized;
    intPrefIs(
      "sanitize.timeSpan",
      Sanitizer.TIMESPAN_EVERYTHING,
      "timeSpan pref should be everything after accepting dialog " +
        "with everything selected"
    );

    await promiseHistoryClearedState(uris, true);
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Ensures that the "Everything" warning is visible on dialog open after
 * the previous test.
 */
add_task(async function test_everything_warning() {
  // Add history.
  let uris = [];
  let places = [];
  let pURI;
  // within past hour, within past two hours, within past four hours and
  // outside past four hours
  [10, 70, 130, 250].forEach(function (aValue) {
    pURI = makeURI("https://" + aValue + "-minutes-ago.com/");
    places.push({ uri: pURI, visitDate: visitTimeForMinutesAgo(aValue) });
    uris.push(pURI);
  });

  let promiseSanitized = promiseSanitizationComplete();

  await PlacesTestUtils.addVisits(places);
  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    is(
      this.isWarningPanelVisible(),
      true,
      "Warning panel should be visible after previously accepting dialog " +
        "with clearing everything"
    );
    this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", true);
    this.acceptDialog();
  };
  dh.onunload = async function () {
    intPrefIs(
      "sanitize.timeSpan",
      Sanitizer.TIMESPAN_EVERYTHING,
      "timeSpan pref should be everything after accepting dialog " +
        "with everything selected"
    );

    await promiseSanitized;

    await promiseHistoryClearedState(uris, true);
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Tests that the clearing button gets disabled if no checkboxes are checked
 * and enabled when at least one checkbox is checked
 */
add_task(async function testAcceptButtonDisabled() {
  let dh = new ClearHistoryDialogHelper();
  dh.onload = async function () {
    let clearButton = this.win.document
      .querySelector("dialog")
      .getButton("accept");
    this.uncheckAllCheckboxes();
    await new Promise(resolve => SimpleTest.executeSoon(resolve));
    is(clearButton.disabled, true, "Clear button should be disabled");

    this.checkPrefCheckbox("cache", true);
    await new Promise(resolve => SimpleTest.executeSoon(resolve));
    is(clearButton.disabled, false, "Clear button should not be disabled");

    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Tests to see if the warning box is hidden when opened in the clear on shutdown context
 */
add_task(async function testWarningBoxInClearOnShutdown() {
  let dh = new ClearHistoryDialogHelper({ mode: "clearSiteData" });
  dh.onload = function () {
    this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);
    is(
      BrowserTestUtils.isVisible(this.getWarningPanel()),
      true,
      `warning panel should be visible`
    );
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  dh = new ClearHistoryDialogHelper({ mode: "clearOnShutdown" });
  dh.onload = function () {
    is(
      BrowserTestUtils.isVisible(this.getWarningPanel()),
      false,
      `warning panel should not be visible`
    );

    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * Checks if clearing history and downloads for the simple timespan
 * behaves as expected
 */
add_task(async function test_history_downloads_checked() {
  // Add downloads (within the past hour).
  let downloadIDs = [];
  for (let i = 0; i < 5; i++) {
    await addDownloadWithMinutesAgo(downloadIDs, i);
  }
  // Add downloads (over an hour ago).
  let olderDownloadIDs = [];
  for (let i = 0; i < 5; i++) {
    await addDownloadWithMinutesAgo(olderDownloadIDs, 61 + i);
  }

  // Add history (within the past hour).
  let uris = [];
  let places = [];
  let pURI;
  for (let i = 0; i < 30; i++) {
    pURI = makeURI("https://" + i + "-minutes-ago.com/");
    places.push({ uri: pURI, visitDate: visitTimeForMinutesAgo(i) });
    uris.push(pURI);
  }
  // Add history (over an hour ago).
  let olderURIs = [];
  for (let i = 0; i < 5; i++) {
    pURI = makeURI("https://" + (61 + i) + "-minutes-ago.com/");
    places.push({ uri: pURI, visitDate: visitTimeForMinutesAgo(61 + i) });
    olderURIs.push(pURI);
  }
  let promiseSanitized = promiseSanitizationComplete();

  await PlacesTestUtils.addVisits(places);

  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    this.selectDuration(Sanitizer.TIMESPAN_HOUR);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", true);
    this.acceptDialog();
  };
  dh.onunload = async function () {
    intPrefIs(
      "sanitize.timeSpan",
      Sanitizer.TIMESPAN_HOUR,
      "timeSpan pref should be hour after accepting dialog with " +
        "hour selected"
    );

    await promiseSanitized;

    // History visits and downloads within one hour should be cleared.
    await promiseHistoryClearedState(uris, true);
    await ensureDownloadsClearedState(downloadIDs, true);

    // Visits and downloads > 1 hour should still exist.
    await promiseHistoryClearedState(olderURIs, false);
    await ensureDownloadsClearedState(olderDownloadIDs, false);

    // OK, done, cleanup after ourselves.
    await blankSlate();
    await promiseHistoryClearedState(olderURIs, true);
    await ensureDownloadsClearedState(olderDownloadIDs, true);
  };
  dh.open();
  await dh.promiseClosed;
});

/**
 * The next three tests checks that when a certain history item cannot be
 * cleared then the checkbox should be both disabled and unchecked.
 * In addition, we ensure that this behavior does not modify the preferences.
 */
add_task(async function test_cannot_clear_history() {
  // Add form entries
  let formEntries = [await promiseAddFormEntryWithMinutesAgo(10)];

  let promiseSanitized = promiseSanitizationComplete();

  // Add history.
  let pURI = makeURI("https://" + 10 + "-minutes-ago.com/");
  await PlacesTestUtils.addVisits({
    uri: pURI,
    visitDate: visitTimeForMinutesAgo(10),
  });
  let uris = [pURI];

  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    var cb = this.win.document.querySelectorAll(
      "checkbox[id='browsingHistoryAndDownloads']"
    );
    ok(
      cb.length == 1 && !cb[0].disabled,
      "There is history, checkbox to clear history should be enabled."
    );

    this.checkPrefCheckbox("formdata", true);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", true);
    this.acceptDialog();
  };
  dh.onunload = async function () {
    await promiseSanitized;

    await promiseHistoryClearedState(uris, true);

    let exists = await formNameExists(formEntries[0]);
    ok(!exists, "form entry " + formEntries[0] + " should no longer exist");
  };
  dh.open();
  await dh.promiseClosed;
});

add_task(async function test_no_history_to_clear() {
  let promiseSanitized = promiseSanitizationComplete();
  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    var cb = this.win.document.querySelectorAll(
      "checkbox[id='browsingHistoryAndDownloads']"
    );
    ok(
      cb.length == 1 && !cb[0].disabled && cb[0].checked,
      "There is no history, but history checkbox should always be enabled " +
        "and will be checked from previous preference."
    );

    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;
  await promiseSanitized;
});

add_task(async function test_form_entries() {
  let formEntry = await promiseAddFormEntryWithMinutesAgo(10);

  let promiseSanitized = promiseSanitizationComplete();

  let dh = new ClearHistoryDialogHelper();
  dh.onload = function () {
    var cb = this.win.document.querySelectorAll("checkbox[id='formdata']");
    is(cb.length, 1, "There is only one checkbox for form data");
    ok(!cb[0].disabled, "The checkbox is enabled");
    ok(cb[0].checked, "The checkbox is checked");

    this.acceptDialog();
  };
  dh.onunload = async function () {
    await promiseSanitized;
    let exists = await formNameExists(formEntry);
    ok(!exists, "form entry " + formEntry + " should no longer exist");
  };
  dh.open();
  await dh.promiseClosed;
});

// test the case when we open the dialog through the clear on shutdown settings
add_task(async function test_clear_on_shutdown() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.sanitize.sanitizeOnShutdown", true]],
  });

  let dh = new ClearHistoryDialogHelper({ mode: "clearOnShutdown" });
  dh.onload = async function () {
    this.uncheckAllCheckboxes();
    this.checkPrefCheckbox("browsingHistoryAndDownloads", false);
    this.checkPrefCheckbox("cookiesAndStorage", true);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  // Add downloads (within the past hour).
  let downloadIDs = [];
  for (let i = 0; i < 5; i++) {
    await addDownloadWithMinutesAgo(downloadIDs, i);
  }
  // Add downloads (over an hour ago).
  let olderDownloadIDs = [];
  for (let i = 0; i < 5; i++) {
    await addDownloadWithMinutesAgo(olderDownloadIDs, 61 + i);
  }

  boolPrefIs(
    "clearOnShutdown_v2.browsingHistoryAndDownloads",
    false,
    "clearOnShutdown_v2 history should be false"
  );

  boolPrefIs(
    "clearOnShutdown_v2.cookiesAndStorage",
    true,
    "clearOnShutdown_v2 cookies should be true"
  );

  boolPrefIs(
    "clearOnShutdown_v2.cache",
    false,
    "clearOnShutdown_v2 cache should be false"
  );

  await createDummyDataForHost("example.org");
  await createDummyDataForHost("example.com");

  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.org"),
    "We have indexedDB data for example.org"
  );
  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.com"),
    "We have indexedDB data for example.com"
  );

  // Cleaning up
  await Sanitizer.runSanitizeOnShutdown();

  // Data for example.org should be cleared
  ok(
    !(await SiteDataTestUtils.hasIndexedDB("https://example.org")),
    "We don't have indexedDB data for example.org"
  );
  // Data for example.com should be cleared
  ok(
    !(await SiteDataTestUtils.hasIndexedDB("https://example.com")),
    "We don't have indexedDB data for example.com"
  );

  // Downloads shouldn't have cleared
  await ensureDownloadsClearedState(downloadIDs, false);
  await ensureDownloadsClearedState(olderDownloadIDs, false);

  dh = new ClearHistoryDialogHelper({ mode: "clearOnShutdown" });
  dh.onload = async function () {
    this.uncheckAllCheckboxes();
    this.checkPrefCheckbox("browsingHistoryAndDownloads", true);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  boolPrefIs(
    "clearOnShutdown_v2.browsingHistoryAndDownloads",
    true,
    "clearOnShutdown_v2 history should be true"
  );

  boolPrefIs(
    "clearOnShutdown_v2.cookiesAndStorage",
    false,
    "clearOnShutdown_v2 cookies should be false"
  );

  boolPrefIs(
    "clearOnShutdown_v2.cache",
    false,
    "clearOnShutdown_v2 cache should be false"
  );

  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.org"),
    "We have indexedDB data for example.org"
  );
  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.com"),
    "We have indexedDB data for example.com"
  );

  // Cleaning up
  await Sanitizer.runSanitizeOnShutdown();

  // Data for example.org should not be cleared
  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.org"),
    "We have indexedDB data for example.org"
  );
  // Data for example.com should not be cleared
  ok(
    await SiteDataTestUtils.hasIndexedDB("https://example.com"),
    "We have indexedDB data for example.com"
  );

  // downloads should have cleared
  await ensureDownloadsClearedState(downloadIDs, true);
  await ensureDownloadsClearedState(olderDownloadIDs, true);

  // Clean up
  await SiteDataTestUtils.clear();
});

add_task(async function testClearHistoryCheckboxStatesAfterMigration() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.cpd.history", false],
      ["privacy.cpd.formdata", true],
      ["privacy.cpd.cookies", true],
      ["privacy.cpd.offlineApps", false],
      ["privacy.cpd.sessions", false],
      ["privacy.cpd.siteSettings", false],
      ["privacy.cpd.cache", true],
      // Set cookiesAndStorage to verify that the pref is flipped in the test
      ["privacy.clearHistory.cookiesAndStorage", false],
      // We set the old migrate pref to false to simulate a user who has not migrated to the new dialog.
      // we should follow the user's old prefs with "cpd." prefix in this case.
      ["privacy.sanitize.cpd.hasMigratedToNewPrefs2", false],
      ["privacy.sanitize.cpd.hasMigratedToNewPrefs3", false],
    ],
  });

  let dh = new ClearHistoryDialogHelper({ mode: "clearHistory" });
  dh.onload = function () {
    this.validateCheckbox("cookiesAndStorage", true);
    this.validateCheckbox("browsingHistoryAndDownloads", false);
    this.validateCheckbox("formdata", true);
    this.validateCheckbox("cache", true);
    this.validateCheckbox("siteSettings", false);

    this.checkPrefCheckbox("siteSettings", true);
    this.checkPrefCheckbox("cookiesAndStorage", false);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  is(
    Services.prefs.getBoolPref("privacy.sanitize.cpd.hasMigratedToNewPrefs3"),
    true,
    "Migration is complete for cpd branch"
  );

  // make sure the migration doesn't run again
  dh = new ClearHistoryDialogHelper({ mode: "clearHistory" });
  dh.onload = function () {
    this.validateCheckbox("siteSettings", true);
    this.validateCheckbox("cookiesAndStorage", false);
    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;
});

add_task(async function testClearHistoryCheckboxStatesAfterMigration3() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.cpd.history", false],
      ["privacy.cpd.formdata", true],
      ["privacy.cpd.cookies", true],
      ["privacy.cpd.offlineApps", false],
      ["privacy.cpd.sessions", false],
      ["privacy.cpd.siteSettings", true],
      ["privacy.cpd.cache", true],
      // Verify that prefs not in not touched in migration from v2
      ["privacy.clearHistory.cookiesAndStorage", false],
      ["privacy.clearHistory.siteSettings", false],
      ["privacy.clearHistory.cache", false],
      // Verify that formData and browsingHistoryAndDownloads inherit this value
      ["privacy.clearHistory.historyFormDataAndDownloads", true],
      // migrate from v2 to v3, dont redo the v1 to v2 migration
      ["privacy.sanitize.cpd.hasMigratedToNewPrefs2", true],
      ["privacy.sanitize.cpd.hasMigratedToNewPrefs3", false],
    ],
  });

  let dh = new ClearHistoryDialogHelper({ mode: "clearHistory" });
  dh.onload = function () {
    // migration to v3 shouldn't modify these values
    this.validateCheckbox("cookiesAndStorage", false);
    this.validateCheckbox("siteSettings", false);
    this.validateCheckbox("cache", false);

    // migration to v3 should set them initially to true from historyFormDataAndDownloads pref
    this.validateCheckbox("browsingHistoryAndDownloads", true);
    this.validateCheckbox("formdata", true);

    // flip two prefs to open to verify migration doesn't happen again and checkboxes retain their value
    this.checkPrefCheckbox("siteSettings", true);
    this.checkPrefCheckbox("browsingHistoryAndDownloads", false);
    this.acceptDialog();
  };
  dh.open();
  await dh.promiseClosed;

  is(
    Services.prefs.getBoolPref("privacy.sanitize.cpd.hasMigratedToNewPrefs3"),
    true,
    "Migration is complete for cpd branch"
  );

  // make sure the migration doesn't run again
  dh = new ClearHistoryDialogHelper({ mode: "clearHistory" });
  dh.onload = function () {
    this.validateCheckbox("siteSettings", true);
    this.validateCheckbox("browsingHistoryAndDownloads", false);
    this.cancelDialog();
  };
  dh.open();
  await dh.promiseClosed;
});
