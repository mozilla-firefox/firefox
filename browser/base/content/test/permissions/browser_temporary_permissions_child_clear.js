/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Browser-scoped permission removals must reach the content process. The
// parent's own table is cleared synchronously, so these tests deliberately
// assert the *child's* view, read from a tab that shares the content process
// and outlives the tab (or the document) the permission belonged to.

const ORIGIN = "https://example.com/";
const OTHER_ORIGIN = "https://example.org/";

let gTabs = new Set();

function getPrincipal(origin) {
  return Services.scriptSecurityManager.createContentPrincipalFromOrigin(
    origin
  );
}

// Reads the content process' copy of a browser-scoped permission. `browser`
// only selects which process to ask; `browserId` is the tab being asked about
// and need not belong to `browser`.
async function testForBrowserInChild(browser, origin, type, browserId) {
  return SpecialPowers.spawn(browser, [origin, type, browserId], (o, t, id) => {
    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(o);
    return Services.perms.testForBrowser(principal, t, id);
  });
}

function getContentProcessID(browser) {
  return browser.browsingContext.currentWindowGlobal.domProcess.childID;
}

async function openTab(url = ORIGIN) {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  gTabs.add(tab);
  return tab;
}

function closeTab(tab) {
  gTabs.delete(tab);
  return BrowserTestUtils.removeTab(tab);
}

// Opens two same-origin tabs pinned to a single content process. The first is
// returned as the observer: it stays open so its process survives whatever
// happens to the second tab, letting us inspect what that process still
// believes.
async function openObserverAndSubjectTabs() {
  let observerTab = await openTab();
  let subjectTab = await openTab();

  Assert.equal(
    getContentProcessID(observerTab.linkedBrowser),
    getContentProcessID(subjectTab.linkedBrowser),
    "Both tabs should share one content process"
  );

  return { observerTab, subjectTab };
}

add_setup(async function () {
  // Force same-origin tabs to share a process so one can outlive the other.
  await SpecialPowers.pushPrefEnv({
    set: [["dom.ipc.processCount.webIsolated", 1]],
  });
});

registerCleanupFunction(() => {
  for (let tab of gTabs) {
    BrowserTestUtils.removeTab(tab);
  }
  gTabs.clear();
});

// Bug 2064718: on tab close the BrowserId is unregistered from the
// BrowsingContext map before "browsing-context-discarded" fires, so the clear
// could not be routed to the tab's content process and the entry leaked there.
add_task(async function testTabCloseClearsPermissionInChild() {
  let { observerTab, subjectTab } = await openObserverAndSubjectTabs();
  let browser = subjectTab.linkedBrowser;
  let principal = getPrincipal(ORIGIN);
  let browserId = browser.browserId;

  Services.perms.addFromPrincipalForBrowser(
    principal,
    "geo",
    Services.perms.ALLOW_ACTION,
    browserId,
    0
  );

  await waitForPermissionState(browser, "geolocation", "granted");
  Assert.equal(
    await testForBrowserInChild(browser, ORIGIN, "geo", browserId),
    Services.perms.ALLOW_ACTION,
    "Content process should see the permission before tab close"
  );

  await closeTab(subjectTab);

  Assert.equal(
    Services.perms.testForBrowser(principal, "geo", browserId),
    Services.perms.UNKNOWN_ACTION,
    "Parent should have dropped the permission on tab close"
  );

  await TestUtils.waitForCondition(
    async () =>
      (await testForBrowserInChild(
        observerTab.linkedBrowser,
        ORIGIN,
        "geo",
        browserId
      )) == Services.perms.UNKNOWN_ACTION,
    "Waiting for the content process to drop the closed tab's permission"
  );

  await closeTab(observerTab);
});

// Bug 2064718: a removal was only routed to the tab's *current* content
// process, so a process left behind by a process switch kept serving the
// permission after it had been revoked.
add_task(async function testRemovalReachesProcessLeftBehindByProcessSwitch() {
  let { observerTab, subjectTab } = await openObserverAndSubjectTabs();
  let browser = subjectTab.linkedBrowser;
  let principal = getPrincipal(ORIGIN);
  let browserId = browser.browserId;

  Services.perms.addFromPrincipalForBrowser(
    principal,
    "geo",
    Services.perms.ALLOW_ACTION,
    browserId,
    0
  );
  await waitForPermissionState(browser, "geolocation", "granted");

  // Navigate cross-site so the tab switches process. The original process stays
  // alive because observerTab is still in it, still holding the entry.
  let loaded = BrowserTestUtils.browserLoaded(browser, false, OTHER_ORIGIN);
  BrowserTestUtils.startLoadingURIString(browser, OTHER_ORIGIN);
  await loaded;

  Assert.notEqual(
    getContentProcessID(browser),
    getContentProcessID(observerTab.linkedBrowser),
    "Tab should have switched away from the original process"
  );
  Assert.equal(
    await testForBrowserInChild(
      observerTab.linkedBrowser,
      ORIGIN,
      "geo",
      browserId
    ),
    Services.perms.ALLOW_ACTION,
    "Original process should still hold the entry after the process switch"
  );

  Services.perms.removeFromPrincipalForBrowser(principal, "geo", browserId);

  await TestUtils.waitForCondition(
    async () =>
      (await testForBrowserInChild(
        observerTab.linkedBrowser,
        ORIGIN,
        "geo",
        browserId
      )) == Services.perms.UNKNOWN_ACTION,
    "Waiting for the process left behind to drop the revoked permission"
  );

  await closeTab(subjectTab);
  await closeTab(observerTab);
});

// The same delivery path is used for a single removal and for a bulk clear on
// a live tab. Both are covered here because they take different IPC messages.
add_task(async function testExplicitRemovalClearsPermissionInChild() {
  let { observerTab, subjectTab } = await openObserverAndSubjectTabs();
  let browser = subjectTab.linkedBrowser;
  let principal = getPrincipal(ORIGIN);
  let browserId = browser.browserId;

  for (let removal of [
    () =>
      Services.perms.removeFromPrincipalForBrowser(principal, "geo", browserId),
    () => Services.perms.removeAllForBrowser(browserId),
    () =>
      Services.perms.removeByActionForBrowser(
        browserId,
        Services.perms.ALLOW_ACTION
      ),
  ]) {
    Services.perms.addFromPrincipalForBrowser(
      principal,
      "geo",
      Services.perms.ALLOW_ACTION,
      browserId,
      0
    );
    await waitForPermissionState(browser, "geolocation", "granted");

    removal();

    await TestUtils.waitForCondition(
      async () =>
        (await testForBrowserInChild(browser, ORIGIN, "geo", browserId)) ==
        Services.perms.UNKNOWN_ACTION,
      "Waiting for the content process to drop the removed permission"
    );
    Assert.equal(
      await testForBrowserInChild(
        observerTab.linkedBrowser,
        ORIGIN,
        "geo",
        browserId
      ),
      Services.perms.UNKNOWN_ACTION,
      "Permission should be gone from the shared content process"
    );
  }

  await closeTab(subjectTab);
  await closeTab(observerTab);
});
