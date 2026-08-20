"use strict";

/* import-globals-from head_siteContainer.js */
loadTestSubscript("head_siteContainer.js");

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);
const { UrlbarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlbarTestUtils.sys.mjs"
);
UrlbarTestUtils.init(this);

// Back and forward within a container are ordinary navigations, so they may be
// served from the BFCache, which fires no load event: wait for the location
// change instead.
async function goBackTo(tab, url) {
  let locationChange = BrowserTestUtils.waitForLocationChange(gBrowser, url);
  tab.linkedBrowser.goBack();
  await locationChange;
}

async function goForwardTo(tab, url) {
  let locationChange = BrowserTestUtils.waitForLocationChange(gBrowser, url);
  tab.linkedBrowser.goForward();
  await locationChange;
}

add_task(async function test_retarget_into_new_tab() {
  let tab = await openTabIn(OTHER_CONTAINER);
  await assertContainer(
    tab,
    OTHER_CONTAINER,
    "Unbound site stays in the tab's container"
  );

  let retargeted = await loadRetargeted(tab, URL_ORG);

  await assertContainer(
    retargeted,
    BOUND_CONTAINER,
    "The load landed in a new tab in the bound container"
  );
  ok(retargeted.selected, "The new tab is selected");
  ok(
    !retargeted.linkedBrowser.canGoBack,
    "The new tab starts a fresh session history"
  );
  is(
    retargeted.linkedBrowser.browsingContext.opener,
    null,
    "The new tab has no opener"
  );

  await assertLeftBehind(tab, URL_COM, OTHER_CONTAINER, "The original tab");

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// A tab which only existed for the load is closed, so that the common flow of
// opening a tab and going to a bound site doesn't leave an empty tab behind.
// "about:blank" stands in for "about:newtab": isBlankPageURL covers both.
add_task(async function test_disposable_tab_is_closed() {
  let tab = await openTabIn(OTHER_CONTAINER, "about:blank");

  let retargeted = await loadRetargeted(tab, URL_ORG);

  // TabClose is fired before the tab is detached, so wait for the tab itself.
  await TestUtils.waitForCondition(
    () => !tab.isConnected,
    "The tab which only existed for the load was closed"
  );
  await assertContainer(retargeted, BOUND_CONTAINER, "The retargeted load");

  BrowserTestUtils.removeTab(retargeted);
});

// Choosing a container explicitly, as "Reopen in Container" does by opening the
// site in a tab of the chosen container, does not win over the association: the
// load is retargeted and the tab created for it, which showed nothing, closed.
add_task(async function test_explicit_container_choice_does_not_win() {
  let tab = BrowserTestUtils.addTab(gBrowser, URL_ORG, {
    userContextId: OTHER_CONTAINER,
  });
  gBrowser.selectedTab = tab;
  let retargeted = await BrowserTestUtils.waitForNewTab(
    gBrowser,
    URL_ORG,
    true
  );

  await assertContainer(
    retargeted,
    BOUND_CONTAINER,
    "The association, not the chosen container"
  );
  await TestUtils.waitForCondition(
    () => !tab.isConnected,
    "The tab opened for the load was closed"
  );

  BrowserTestUtils.removeTab(retargeted);
});

// A blank page reached by navigating is not disposable: the tab has history.
add_task(async function test_blank_tab_with_history_is_kept() {
  let tab = await openTabIn(OTHER_CONTAINER);
  await navigateTo(tab, "about:blank");

  let retargeted = await loadRetargeted(tab, URL_ORG);

  // Closing the tab left behind is asynchronous, so give it the chance to
  // happen before asserting that it didn't.
  await TestUtils.waitForTick();

  ok(tab.isConnected, "A blank tab with history was not closed");
  is(tab.linkedBrowser.currentURI.spec, "about:blank", "It kept its document");
  await assertContainer(retargeted, BOUND_CONTAINER, "The retargeted load");

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// The tab left behind must not keep showing the URL that ended up elsewhere.
add_task(async function test_urlbar_of_the_tab_left_behind() {
  let tab = await openTabIn(OTHER_CONTAINER);

  let retargeted = BrowserTestUtils.waitForNewTab(gBrowser, URL_ORG, true);
  await UrlbarTestUtils.inputIntoURLBar(window, URL_ORG);
  EventUtils.synthesizeKey("KEY_Enter");
  let newTab = await retargeted;

  await assertContainer(
    newTab,
    BOUND_CONTAINER,
    "A typed URL landed in a new tab in the bound container"
  );

  await BrowserTestUtils.switchTab(gBrowser, tab);
  is(
    gBrowser.selectedBrowser.currentURI.spec,
    URL_COM,
    "The tab left behind still displays its own document"
  );
  ok(
    !gURLBar.value.includes("example.org"),
    `The URL bar does not show the retargeted URL (got "${gURLBar.value}")`
  );
  ok(
    gURLBar.value.includes("example.com"),
    `The URL bar shows the tab's own URL (got "${gURLBar.value}")`
  );

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(tab);
});

// A navigation in a background tab is retargeted like any other: where the new
// tab opens is the frontend's decision, taken as for any other diverted load.
add_task(async function test_retarget_from_background_tab() {
  let tab = await openTabIn(OTHER_CONTAINER, URL_COM, { select: false });
  ok(!tab.selected, "The navigated tab is in the background");

  let retargeted = await loadRetargeted(tab, URL_ORG);

  ok(retargeted.selected, "The new tab is selected");
  await assertContainer(
    retargeted,
    BOUND_CONTAINER,
    "The retarget of a background tab landed in the bound container"
  );

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// browser.tabs.loadDivertedInBackground governs the retarget as it does the
// other loads the frontend diverts into a new tab.
add_task(async function test_retarget_in_background() {
  await withPrefs(
    [["browser.tabs.loadDivertedInBackground", true]],
    async () => {
      let tab = await openTabIn(OTHER_CONTAINER);

      let retargeted = await loadRetargeted(tab, URL_ORG);

      is(gBrowser.selectedTab, tab, "The selected tab did not change");
      ok(!retargeted.selected, "The new tab was opened in the background");
      await assertContainer(
        retargeted,
        BOUND_CONTAINER,
        "The retarget opened in the background landed in the bound container"
      );

      BrowserTestUtils.removeTab(retargeted);
      BrowserTestUtils.removeTab(tab);
    }
  );
});

// A load from session history is never retargeted: the new tab wouldn't have the
// session history being restored.
add_task(async function test_history_load_does_not_retarget() {
  let tab;

  // Build the history while the site is still unbound, restoring the
  // association the rest of the file relies on even if that part fails.
  ContextualIdentityService.removeSiteAssociation("example.org");
  try {
    tab = await openTabIn(OTHER_CONTAINER);
    await navigateTo(tab, URL_ORG);
    await assertContainer(
      tab,
      OTHER_CONTAINER,
      "An unbound site stays in the tab"
    );
  } finally {
    ContextualIdentityService.setSiteAssociation(
      "example.org",
      BOUND_CONTAINER
    );
  }

  await assertNoRetarget(
    gBrowser,
    "Back and forward to a bound site",
    async () => {
      await goBackTo(tab, URL_COM);
      await goForwardTo(tab, URL_ORG);
    }
  );

  await assertContainer(
    tab,
    OTHER_CONTAINER,
    "Back and forward kept the tab's container"
  );

  BrowserTestUtils.removeTab(tab);
});

// The retargeted tab is an ordinary container tab, and restoring it doesn't
// retarget the restored load once more.
add_task(async function test_session_restore() {
  let { tab, retargeted } = await retargetFrom(
    OTHER_CONTAINER,
    BOUND_CONTAINER,
    URL_ORG,
    "The load landed in the bound container"
  );

  await TabStateFlusher.flush(retargeted.linkedBrowser);
  let tabCount = gBrowser.tabs.length;
  await BrowserTestUtils.removeTab(retargeted);

  let restored = SessionStore.undoCloseTab(window, 0);
  await BrowserTestUtils.browserLoaded(restored.linkedBrowser, false, URL_ORG);

  await assertContainer(
    restored,
    BOUND_CONTAINER,
    "The restored tab is in the bound container"
  );
  is(gBrowser.tabs.length, tabCount, "Restoring opened no further tab");

  BrowserTestUtils.removeTab(restored);
  BrowserTestUtils.removeTab(tab);
});
