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

const URL_ORG_2 = BASE_ORG + "blank.html?second";

// The retargeted load commits into the session history of the new tab, so the
// navigations which follow it can go back to it.
add_task(async function test_retargeted_load_is_in_session_history() {
  await withPrefs(
    [["browser.navigation.requireUserInteraction", false]],
    async () => {
      let { tab, retargeted } = await retargetFrom(
        OTHER_CONTAINER,
        BOUND_CONTAINER,
        URL_ORG,
        "The load landed in the bound container"
      );
      let browser = retargeted.linkedBrowser;
      is(
        browser.browsingContext.sessionHistory.count,
        1,
        "The retargeted load is the first entry of the new tab"
      );

      await SpecialPowers.spawn(browser, [], () => {
        content.addEventListener("pageshow", event => {
          content.document.documentElement.dataset.persisted = event.persisted;
        });
      });

      await navigateTo(retargeted, URL_ORG_2);
      is(browser.browsingContext.sessionHistory.count, 2, "Two entries");
      ok(browser.canGoBack, "The new tab can go back");

      let pageShow = BrowserTestUtils.waitForContentEvent(browser, "pageshow");
      await goBackTo(retargeted, URL_ORG);
      await pageShow;
      let persisted = await SpecialPowers.spawn(
        browser,
        [],
        () => content.document.documentElement.dataset.persisted
      );
      is(persisted, "true", "Going back restored the page from the BFCache");
      await assertContainer(
        retargeted,
        BOUND_CONTAINER,
        "Going back kept the container"
      );

      BrowserTestUtils.removeTab(retargeted);
      BrowserTestUtils.removeTab(tab);
    }
  );
});

// With browser.navigation.requireUserInteraction, back skips the entries the
// user didn't interact with: the interaction with the retargeted document must
// be recorded on its entry.
add_task(async function test_retargeted_load_records_user_interaction() {
  await withPrefs(
    [["browser.navigation.requireUserInteraction", true]],
    async () => {
      let { tab, retargeted } = await retargetFrom(
        OTHER_CONTAINER,
        BOUND_CONTAINER,
        URL_ORG,
        "The load landed in the bound container"
      );
      let browser = retargeted.linkedBrowser;

      await BrowserTestUtils.synthesizeMouseAtPoint(10, 10, {}, browser);
      await navigateTo(retargeted, URL_ORG_2);

      ok(
        browser.browsingContext.sessionHistory.getEntryAtIndex(0)
          .hasUserInteraction,
        "The interaction was recorded on the retargeted entry"
      );
      ok(browser.canGoBack, "The new tab can go back");

      BrowserTestUtils.removeTab(retargeted);
      BrowserTestUtils.removeTab(tab);
    }
  );
});

// The Navigation API of the new tab only sees the new tab's history, not the
// same-origin entries of the tab the load started in.
add_task(async function test_retargeted_load_navigation_entries() {
  let tab;
  ContextualIdentityService.removeSiteAssociation("example.org");
  try {
    tab = await openTabIn(OTHER_CONTAINER, URL_ORG);
  } finally {
    ContextualIdentityService.setSiteAssociation(
      "example.org",
      BOUND_CONTAINER
    );
  }

  let retargeted = await loadRetargeted(tab, URL_ORG_2);
  await assertContainer(retargeted, BOUND_CONTAINER, "The retargeted load");

  await SpecialPowers.spawn(retargeted.linkedBrowser, [URL_ORG_2], url => {
    let entries = content.navigation.entries();
    Assert.equal(entries.length, 1, "Only the retargeted entry");
    Assert.equal(entries[0].url, url, "It is the retargeted document");
    Assert.equal(
      content.navigation.activation.from,
      null,
      "No activation entry from the tab left behind"
    );
    Assert.equal(
      content.navigation.activation.navigationType,
      "replace",
      "The retargeted load replaced the initial about:blank"
    );
  });

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// A same-origin replace reuses the navigation API key of the entry it replaces,
// but the tab left behind is in another container: its key must not follow the
// load into the new tab.
add_task(async function test_retargeted_replace_gets_a_new_navigation_key() {
  let tab;
  ContextualIdentityService.removeSiteAssociation("example.org");
  try {
    tab = await openTabIn(OTHER_CONTAINER, URL_ORG);
  } finally {
    ContextualIdentityService.setSiteAssociation(
      "example.org",
      BOUND_CONTAINER
    );
  }

  let oldKey = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    () => content.navigation.currentEntry.key
  );

  let newTab = BrowserTestUtils.waitForNewTab(gBrowser, URL_ORG_2, true);
  await SpecialPowers.spawn(tab.linkedBrowser, [URL_ORG_2], url => {
    content.location.replace(url);
  });
  let retargeted = await newTab;
  await assertContainer(retargeted, BOUND_CONTAINER, "The retargeted replace");

  let newKey = await SpecialPowers.spawn(
    retargeted.linkedBrowser,
    [],
    () => content.navigation.currentEntry.key
  );
  isnot(newKey, oldKey, "The new tab doesn't share the left behind key");

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});

// A refresh to the same URL reuses the active entry of the tab it started in,
// which already belongs to that tab's session history: the new tab must get
// its own entry, and the tab left behind must keep its own.
add_task(async function test_retargeted_refresh_does_not_share_entries() {
  let tab;
  ContextualIdentityService.removeSiteAssociation("example.org");
  try {
    tab = await openTabIn(OTHER_CONTAINER, URL_ORG);
  } finally {
    ContextualIdentityService.setSiteAssociation(
      "example.org",
      BOUND_CONTAINER
    );
  }

  let newTab = BrowserTestUtils.waitForNewTab(gBrowser, URL_ORG, true);
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.history.replaceState({ leftBehind: true }, "");
    let meta = content.document.createElement("meta");
    meta.httpEquiv = "refresh";
    meta.content = "0";
    content.document.head.append(meta);
  });
  let retargeted = await newTab;
  await assertContainer(retargeted, BOUND_CONTAINER, "The retargeted refresh");

  let leftBehindHistory = tab.linkedBrowser.browsingContext.sessionHistory;
  let newHistory = retargeted.linkedBrowser.browsingContext.sessionHistory;
  is(leftBehindHistory.count, 1, "The tab left behind kept its entry");
  is(newHistory.count, 1, "The new tab has its own entry");
  let leftBehindEntry = leftBehindHistory.getEntryAtIndex(0);
  let newEntry = newHistory.getEntryAtIndex(0);
  isnot(newEntry.ID, leftBehindEntry.ID, "The entries are distinct");
  ok(
    !newEntry.sharesDocumentWith(leftBehindEntry),
    "The entries don't share their state"
  );
  is(newEntry.URI.spec, URL_ORG, "The new entry is the retargeted document");
  is(leftBehindEntry.URI.spec, URL_ORG, "The entry left behind is unchanged");

  await SpecialPowers.spawn(retargeted.linkedBrowser, [], () => {
    Assert.equal(
      content.history.state,
      null,
      "The state of the document left behind didn't follow the load"
    );
  });

  await navigateTo(retargeted, URL_ORG_2);
  is(newHistory.count, 2, "The new tab can add entries");
  await goBackTo(retargeted, URL_ORG);

  BrowserTestUtils.removeTab(retargeted);
  BrowserTestUtils.removeTab(tab);
});
