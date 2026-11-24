"use strict";

// Test not displaying sites which store 0 byte and don't have persistent storage.
add_task(async function test_exclusions() {
  let hosts = await addTestData([
    {
      usage: 0,
      origin: "https://account.xyz.com",
      persisted: true,
    },
    {
      usage: 0,
      origin: "https://shopping.xyz.com",
      persisted: false,
    },
    {
      usage: 1024,
      origin: "http://cinema.bar.com",
      persisted: true,
    },
    {
      usage: 1024,
      origin: "http://email.bar.com",
      persisted: false,
    },
    {
      usage: 0,
      origin: "http://cookies.bar.com",
      cookies: 5,
      persisted: false,
    },
  ]);

  let updatePromise = promiseSiteDataManagerSitesUpdated();
  let doc = gBrowser.selectedBrowser.contentDocument;
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatePromise;
  await openSiteDataSettingsDialog();
  assertSitesListed(
    doc,
    hosts.filter(host => host != "shopping.xyz.com")
  );

  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test grouping and listing sites across scheme, port and origin attributes by base domain.
add_task(async function test_grouping() {
  let quotaUsage = 7000000;
  let testData = [
    {
      usage: quotaUsage,
      origin: "https://account.xyz.com^userContextId=1",
      cookies: 2,
      persisted: true,
    },
    {
      usage: quotaUsage,
      origin: "https://account.xyz.com",
      cookies: 1,
      persisted: false,
    },
    {
      usage: quotaUsage,
      origin: "https://account.xyz.com:123",
      cookies: 1,
      persisted: false,
    },
    {
      usage: quotaUsage,
      origin: "http://account.xyz.com",
      cookies: 1,
      persisted: false,
    },
    {
      usage: quotaUsage,
      origin: "http://search.xyz.com",
      cookies: 3,
      persisted: false,
    },
    {
      usage: quotaUsage,
      origin: "http://advanced.search.xyz.com",
      cookies: 3,
      persisted: true,
    },
    {
      usage: quotaUsage,
      origin: "http://xyz.com",
      cookies: 1,
      persisted: false,
    },
  ];
  await addTestData(testData);

  let updatedPromise = promiseSiteDataManagerSitesUpdated();
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatedPromise;
  await openSiteDataSettingsDialog();
  let win = gBrowser.selectedBrowser.contentWindow;
  let dialogFrame = win.gSubDialog._topDialog._frame;
  let frameDoc = dialogFrame.contentDocument;

  let siteItems = frameDoc.getElementsByTagName("richlistitem");
  is(
    siteItems.length,
    1,
    "Should group sites across scheme, port and origin attributes"
  );

  let columns = siteItems[0].querySelectorAll(".item-box > label");

  let expected = "xyz.com";
  is(columns[0].value, expected, "Should group and list sites by host");

  let cookieCount = testData.reduce((count, { cookies }) => count + cookies, 0);
  is(
    columns[1].value,
    cookieCount.toString(),
    "Should group cookies across scheme, port and origin attributes"
  );

  let [value, unit] = DownloadUtils.convertByteUnits(quotaUsage * 4);
  let l10nAttributes = frameDoc.l10n.getAttributes(columns[2]);
  is(
    l10nAttributes.id,
    "site-storage-persistent",
    "Should show the site as persistent if one origin is persistent."
  );
  // The shown quota can be slightly larger than the raw data we put in (though it should
  // never be smaller), but that doesn't really matter to us since we only want to test that
  // the site data dialog accumulates this into a single column.
  Assert.greaterOrEqual(
    parseFloat(l10nAttributes.args.value),
    parseFloat(value),
    "Should show the correct accumulated quota size."
  );
  is(
    l10nAttributes.args.unit,
    unit,
    "Should show the correct quota size unit."
  );

  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test sorting
add_task(async function test_sorting() {
  // These sizes are large because the reported usage includes SQLite files
  // that shrink by up to ~16KB when IndexedDB cleans up an idle database,
  // which made smaller differences sort intermittently (bug 2096671).
  let testData = [
    {
      usage: 1024 * 100,
      origin: "https://account.xyz.com",
      cookies: 6,
      persisted: true,
    },
    {
      usage: 1024 * 200,
      origin: "https://books.foo.com",
      cookies: 0,
      persisted: false,
    },
    {
      usage: 1024 * 300,
      origin: "http://cinema.bar.com",
      cookies: 3,
      persisted: true,
    },
    {
      usage: 1024 * 300,
      origin: "http://vod.bar.com",
      cookies: 2,
      persisted: false,
    },
  ];

  await addTestData(testData);

  let updatePromise = promiseSiteDataManagerSitesUpdated();

  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatePromise;
  await openSiteDataSettingsDialog();

  let dialog = content.gSubDialog._topDialog;
  let dialogFrame = dialog._frame;
  let frameDoc = dialogFrame.contentDocument;
  let hostCol = frameDoc.getElementById("hostCol");
  let usageCol = frameDoc.getElementById("usageCol");
  let cookiesCol = frameDoc.getElementById("cookiesCol");
  let sitesList = frameDoc.getElementById("sitesList");

  function getHostOrder() {
    let siteItems = sitesList.getElementsByTagName("richlistitem");
    return Array.from(siteItems).map(item => item.getAttribute("host"));
  }

  // Test default sorting by usage, descending.
  Assert.deepEqual(
    getHostOrder(),
    ["bar.com", "foo.com", "xyz.com"],
    "Has sorted descending by usage"
  );

  // Test sorting on the usage column
  usageCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["xyz.com", "foo.com", "bar.com"],
    "Has sorted ascending by usage"
  );
  usageCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["bar.com", "foo.com", "xyz.com"],
    "Has sorted descending by usage"
  );

  // Test sorting on the host column
  hostCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["bar.com", "foo.com", "xyz.com"],
    "Has sorted ascending by base domain"
  );
  hostCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["xyz.com", "foo.com", "bar.com"],
    "Has sorted descending by base domain"
  );

  // Test sorting on the cookies column
  cookiesCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["foo.com", "bar.com", "xyz.com"],
    "Has sorted ascending by cookies"
  );
  cookiesCol.click();
  Assert.deepEqual(
    getHostOrder(),
    ["xyz.com", "bar.com", "foo.com"],
    "Has sorted descending by cookies"
  );

  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test single entry removal
add_task(async function test_single_entry_removal() {
  let testData = await addTestData([
    {
      usage: 1024,
      origin: "https://xyz.com",
      cookies: 6,
      persisted: true,
    },
    {
      usage: 1024 * 3,
      origin: "http://bar.com",
      cookies: 2,
      persisted: false,
    },
  ]);

  let updatePromise = promiseSiteDataManagerSitesUpdated();
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  await updatePromise;
  await openSiteDataSettingsDialog();

  let dialog = content.gSubDialog._topDialog;
  let dialogFrame = dialog._frame;
  let frameDoc = dialogFrame.contentDocument;

  let sitesList = frameDoc.getElementById("sitesList");
  let host = testData[0];
  let site = sitesList.querySelector(`richlistitem[host="${host}"]`);
  sitesList.addItemToSelection(site);
  frameDoc.getElementById("removeSelected").doCommand();
  let saveChangesButton = frameDoc.querySelector("dialog").getButton("accept");
  let dialogOpened = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    REMOVE_DIALOG_URL
  );
  setTimeout(() => saveChangesButton.doCommand(), 0);
  let dialogWin = await dialogOpened;
  let rootElement = dialogWin.document.getElementById(
    "SiteDataRemoveSelectedDialog"
  );
  is(rootElement.classList.length, 1, "There should only be one class set");
  is(
    rootElement.classList[0],
    "single-entry",
    "The only class set should be single-entry (to hide the list)"
  );
  let description = dialogWin.document.getElementById("removing-description");
  is(
    description.getAttribute("data-l10n-id"),
    "site-data-removing-single-desc",
    "The description for single site should be selected"
  );

  let removalList = dialogWin.document.getElementById("removalList");
  is(
    BrowserTestUtils.isVisible(removalList),
    false,
    "The removal list should be invisible"
  );
  let removeButton = dialogWin.document
    .querySelector("dialog")
    .getButton("accept");
  let dialogClosed = BrowserTestUtils.waitForEvent(dialogWin, "unload");
  updatePromise = promiseSiteDataManagerSitesUpdated();
  removeButton.doCommand();
  await dialogClosed;
  await updatePromise;
  await openSiteDataSettingsDialog();

  dialog = content.gSubDialog._topDialog;
  dialogFrame = dialog._frame;
  frameDoc = dialogFrame.contentDocument;
  assertSitesListed(frameDoc, testData.slice(1));
  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
