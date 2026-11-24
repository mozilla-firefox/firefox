/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function getPersistentStoragePermStatus(origin) {
  let uri = Services.io.newURI(origin);
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );
  return Services.perms.testExactPermissionFromPrincipal(
    principal,
    "persistent-storage"
  );
}

// Test listing site using quota usage or site using appcache
// This is currently disabled because of bug 1414751.
add_task(async function test_list_sites() {
  // Open a test site which would save into appcache
  await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_OFFLINE_URL);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  // Open a test site which would save into quota manager
  BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_QUOTA_USAGE_URL);
  await BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedBrowser,
    "test-indexedDB-done",
    false,
    null,
    true
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  let updatedPromise = promiseSiteDataManagerSitesUpdated();
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatedPromise;
  await openSiteDataSettingsDialog();
  let dialog = content.gSubDialog._topDialog;
  let dialogFrame = dialog._frame;
  let frameDoc = dialogFrame.contentDocument;

  let siteItems = frameDoc.getElementsByTagName("richlistitem");
  is(siteItems.length, 2, "Should list sites using quota usage or appcache");

  let appcacheSite = frameDoc.querySelector(
    `richlistitem[host="${TEST_OFFLINE_HOST}"]`
  );
  ok(appcacheSite, "Should list site using appcache");

  let qoutaUsageSite = frameDoc.querySelector(
    `richlistitem[host="${TEST_QUOTA_USAGE_HOST}"]`
  );
  ok(qoutaUsageSite, "Should list site using quota usage");

  // Always remember to clean up
  await new Promise(resolve => {
    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        TEST_QUOTA_USAGE_ORIGIN
      );
    let request = Services.qms.clearStoragesForOriginPrefix(principal);
    request.callback = resolve;
  });

  await SiteDataManager.removeAll();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
}).skip(); // Bug 1414751

// Test buttons are disabled and loading message shown while updating sites
add_task(async function test_ui_loading_state() {
  let updatedPromise = promiseSiteDataManagerSitesUpdated();
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatedPromise;
  let cacheSize = await SiteDataManager.getCacheSize();

  let doc = gBrowser.selectedBrowser.contentDocument;
  let clearBtn = doc.getElementById("clearSiteDataButton");
  let settingsButton = doc.getElementById("siteDataSettings");
  let totalSiteDataSizeLabel = doc.getElementById("siteDataSize");
  is(
    clearBtn.disabled,
    false,
    "Should enable clear button after sites updated"
  );
  is(
    settingsButton.disabled,
    false,
    "Should enable settings button after sites updated"
  );
  await SiteDataManager.getTotalUsage().then(usage => {
    let [value, unit] = DownloadUtils.convertByteUnits(usage + cacheSize);
    Assert.deepEqual(
      doc.l10n.getAttributes(totalSiteDataSizeLabel),
      {
        id: "sitedata-total-size3",
        args: { value, unit },
      },
      "Should show the right total site data size"
    );
  });

  Services.obs.notifyObservers(null, "sitedatamanager:updating-sites");
  // Wait a tick for the UI to update.
  await new Promise(resolve => requestAnimationFrame(resolve));

  is(
    clearBtn.disabled,
    true,
    "Should disable clear button while updating sites"
  );
  is(
    settingsButton.disabled,
    true,
    "Should disable settings button while updating sites"
  );
  Assert.equal(
    doc.l10n.getAttributes(totalSiteDataSizeLabel).id,
    "sitedata-total-size-calculating",
    "Should show the loading message while updating"
  );

  Services.obs.notifyObservers(null, "sitedatamanager:sites-updated");
  // Wait a tick for the UI to update.
  await new Promise(resolve => setTimeout(resolve, 0));

  is(
    clearBtn.disabled,
    false,
    "Should enable clear button after sites updated"
  );
  is(
    settingsButton.disabled,
    false,
    "Should enable settings button after sites updated"
  );
  cacheSize = await SiteDataManager.getCacheSize();
  await SiteDataManager.getTotalUsage().then(usage => {
    let [value, unit] = DownloadUtils.convertByteUnits(usage + cacheSize);
    Assert.deepEqual(
      doc.l10n.getAttributes(totalSiteDataSizeLabel),
      {
        id: "sitedata-total-size3",
        args: { value, unit },
      },
      "Should show the right total site data size"
    );
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test clearing service worker through the settings panel
add_task(async function test_clear_service_worker() {
  // Register a test service worker
  await loadServiceWorkerTestPage(TEST_SERVICE_WORKER_URL);
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  // Test the initial states
  await promiseServiceWorkerRegisteredFor(TEST_SERVICE_WORKER_URL);
  // Open the Site Data Settings panel and remove the site
  await openSiteDataSettingsDialog();
  let acceptRemovePromise = BrowserTestUtils.promiseAlertDialogOpen("accept");
  let updatePromise = promiseSiteDataManagerSitesUpdated();
  SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [{ TEST_OFFLINE_HOST }],
    args => {
      let host = args.TEST_OFFLINE_HOST;
      let frameDoc = content.gSubDialog._topDialog._frame.contentDocument;
      let sitesList = frameDoc.getElementById("sitesList");
      let site = sitesList.querySelector(`richlistitem[host="${host}"]`);
      if (site) {
        let removeBtn = frameDoc.getElementById("removeSelected");
        let saveBtn = frameDoc.querySelector("dialog").getButton("accept");
        site.click();
        removeBtn.doCommand();
        saveBtn.doCommand();
      } else {
        ok(false, `Should have one site of ${host}`);
      }
    }
  );
  await acceptRemovePromise;
  await updatePromise;
  await promiseServiceWorkersCleared();
  await SiteDataManager.removeAll();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// The site data dialog formats the "Last Used" column relative to when it
// built its list, which happened at some point between `since` and now.
function relativeDatesSince(lastAccessed, since) {
  let formatter = new Services.intl.RelativeTimeFormat(undefined, {});
  let dates = new Set();
  for (
    let referenceTime = Math.floor(since / 1000) * 1000;
    referenceTime <= Date.now();
    referenceTime += 1000
  ) {
    dates.add(
      formatter.formatBestUnit(new Date(lastAccessed), {
        now: new Date(referenceTime),
      })
    );
  }
  return [...dates];
}

// Test showing and removing sites with cookies.
add_task(async function test_manage_sites_with_cookies() {
  // Add some test cookies.
  let uri = Services.io.newURI("https://example.com");
  let uri2 = Services.io.newURI("https://example.org");
  let cv = Services.cookies.add(
    uri.host,
    uri.pathQueryRef,
    "test1",
    "1",
    false,
    false,
    false,
    Date.now() + 1000 * 60 * 60,
    {},
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK);

  cv = Services.cookies.add(
    uri.host,
    uri.pathQueryRef,
    "test2",
    "2",
    false,
    false,
    false,
    Date.now() + 1000 * 60 * 60,
    {},
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK);

  cv = Services.cookies.add(
    uri2.host,
    uri2.pathQueryRef,
    "test1",
    "1",
    false,
    false,
    false,
    Date.now() + 1000 * 60 * 60,
    {},
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK);

  // Ensure that private browsing cookies are ignored.
  cv = Services.cookies.add(
    uri.host,
    uri.pathQueryRef,
    "test3",
    "3",
    false,
    false,
    false,
    Date.now() + 1000 * 60 * 60,
    { privateBrowsingId: 1 },
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK);

  // Get the exact creation date from the cookies (to avoid intermittents
  // from minimal time differences, since we round up to minutes).
  let cookies1 = Services.cookies.getCookiesFromHost(uri.host, {});
  let cookies2 = Services.cookies.getCookiesFromHost(uri2.host, {});
  // We made two valid cookies for example.com.
  let cookie1 = cookies1[1];
  let cookie2 = cookies2[0];
  // The dialog shows the most recent access across the host's cookies.
  let lastAccessed1 = Math.max(...cookies1.map(c => c.lastAccessed)) / 1000;
  let lastAccessed2 = cookie2.lastAccessed / 1000;

  let fullFormatter = new Services.intl.DateTimeFormat(undefined, {
    dateStyle: "short",
    timeStyle: "short",
  });

  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });

  // Open the site data manager and remove one site.
  let dialogOpenTime = Date.now();
  await openSiteDataSettingsDialog();
  let relativeDates1 = relativeDatesSince(lastAccessed1, dialogOpenTime);
  let relativeDates2 = relativeDatesSince(lastAccessed2, dialogOpenTime);
  let creationDate1 = new Date(cookie1.lastAccessed / 1000);
  let creationDate1Formatted = fullFormatter.format(creationDate1);
  let creationDate2 = new Date(cookie2.lastAccessed / 1000);
  let creationDate2Formatted = fullFormatter.format(creationDate2);
  let removeDialogOpenPromise = BrowserTestUtils.promiseAlertDialogOpen(
    "accept",
    REMOVE_DIALOG_URL
  );
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [
      {
        creationDate1Formatted,
        creationDate2Formatted,
        relativeDates1,
        relativeDates2,
      },
    ],
    function (args) {
      let frameDoc = content.gSubDialog._topDialog._frame.contentDocument;

      let siteItems = frameDoc.getElementsByTagName("richlistitem");
      is(siteItems.length, 2, "Should list two sites with cookies");
      let sitesList = frameDoc.getElementById("sitesList");
      let site1 = sitesList.querySelector(`richlistitem[host="example.com"]`);
      let site2 = sitesList.querySelector(`richlistitem[host="example.org"]`);

      let columns = site1.querySelectorAll(".item-box > label");
      let boxes = site1.querySelectorAll(".item-box");
      is(columns[0].value, "example.com", "Should show the correct host.");
      is(columns[1].value, "2", "Should show the correct number of cookies.");
      is(columns[2].value, "", "Should show no site data.");
      ok(
        args.relativeDates1.includes(columns[3].value),
        "Should show the relative date."
      );
      is(
        boxes[3].getAttribute("tooltiptext"),
        args.creationDate1Formatted,
        "Should show the correct date."
      );

      columns = site2.querySelectorAll(".item-box > label");
      boxes = site2.querySelectorAll(".item-box");
      is(columns[0].value, "example.org", "Should show the correct host.");
      is(columns[1].value, "1", "Should show the correct number of cookies.");
      is(columns[2].value, "", "Should show no site data.");
      ok(
        args.relativeDates2.includes(columns[3].value),
        "Should show the relative date."
      );
      is(
        boxes[3].getAttribute("tooltiptext"),
        args.creationDate2Formatted,
        "Should show the correct date."
      );

      let removeBtn = frameDoc.getElementById("removeSelected");
      let saveBtn = frameDoc.querySelector("dialog").getButton("accept");
      site2.click();
      removeBtn.doCommand();
      saveBtn.doCommand();
    }
  );
  await removeDialogOpenPromise;

  await TestUtils.waitForCondition(
    () => !Services.cookies.getCookiesFromHost(uri2.host, {}).length,
    "Cookies from the first host should be cleared"
  );
  is(
    Services.cookies.getCookiesFromHost(uri.host, {}).length,
    2,
    "Cookies from the second host should not be cleared"
  );

  // Open the site data manager and remove another site.
  dialogOpenTime = Date.now();
  await openSiteDataSettingsDialog();
  relativeDates1 = relativeDatesSince(lastAccessed1, dialogOpenTime);
  let acceptRemovePromise = BrowserTestUtils.promiseAlertDialogOpen("accept");
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [{ creationDate1Formatted, relativeDates1 }],
    function (args) {
      let frameDoc = content.gSubDialog._topDialog._frame.contentDocument;

      let siteItems = frameDoc.getElementsByTagName("richlistitem");
      is(siteItems.length, 1, "Should list one site with cookies");
      let sitesList = frameDoc.getElementById("sitesList");
      let site1 = sitesList.querySelector(`richlistitem[host="example.com"]`);

      let columns = site1.querySelectorAll(".item-box > label");
      let boxes = site1.querySelectorAll(".item-box");
      is(columns[0].value, "example.com", "Should show the correct host.");
      is(columns[1].value, "2", "Should show the correct number of cookies.");
      is(columns[2].value, "", "Should show no site data.");
      ok(
        args.relativeDates1.includes(columns[3].value),
        "Should show the relative date."
      );
      is(
        boxes[3].getAttribute("tooltiptext"),
        args.creationDate1Formatted,
        "Should show the correct date."
      );

      let removeBtn = frameDoc.getElementById("removeSelected");
      let saveBtn = frameDoc.querySelector("dialog").getButton("accept");
      site1.click();
      removeBtn.doCommand();
      saveBtn.doCommand();
    }
  );
  await acceptRemovePromise;

  await TestUtils.waitForCondition(
    () => !Services.cookies.getCookiesFromHost(uri.host, {}).length,
    "Cookies from the second host should be cleared"
  );

  await openSiteDataSettingsDialog();

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    let frameDoc = content.gSubDialog._topDialog._frame.contentDocument;

    let siteItems = frameDoc.getElementsByTagName("richlistitem");
    is(siteItems.length, 0, "Should list no sites with cookies");
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
