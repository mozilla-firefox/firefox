"use strict";

const TOPIC = "webNavigation-createdNavigationTarget";
const TEST_LINK = "https://example.com/";
const TOP_SITE_SELECTOR =
  ".top-sites-list:not(.search-shortcut, .placeholder) .top-site-button";

/**
 * Right-clicks an element and activates an item from the native context menu,
 * resolving with the first navigation target notification it produced.
 *
 * @param {MozBrowser} browser The browser to right-click in.
 * @param {string} selector Selector of the element to right-click.
 * @param {string} itemId Id of the context menu item to activate.
 * @returns {Promise<object>} The notified subject.
 */
async function openFromContextMenu(browser, selector, itemId) {
  Services.fog.testResetFOG();
  let notified = TestUtils.topicObserved(TOPIC).then(
    ([subject]) => subject.wrappedJSObject
  );

  let contextMenu = document.getElementById("contentAreaContextMenu");
  let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    selector,
    { type: "contextmenu", button: 2 },
    browser
  );
  await popupShown;

  let popupHidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.activateItem(document.getElementById(itemId));
  await popupHidden;

  return notified;
}

async function openNewtabWithTopSites() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );
  await waitForPreloaded(tab.linkedBrowser);
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [TOP_SITE_SELECTOR],
    async selector => {
      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector(selector),
        "Top sites have loaded"
      );
    }
  );
  return tab;
}

/**
 * Checks the topsites.click recorded for an open. Train-hopped onto a Firefox
 * whose schema lacks the new extras, newtab records no click at all.
 *
 * @param {string} background The expected background extra.
 */
function checkTopSiteClick(background) {
  let clicks = Glean.topsites.click.testGetValue() ?? [];
  is(clicks.length, 1, "Recorded 1 topsites.click");
  is(clicks[0].extra.background, background, "Recorded the background extra");
}

add_setup(async function () {
  await setTestTopSites();
  registerCleanupFunction(async () => {
    await popPrefs();
  });
});

add_task(async function test_open_link_in_tab_notifies() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.loadInBackground", true]],
  });
  let tab = await openNewtabWithTopSites();
  let browser = tab.linkedBrowser;

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    TEST_LINK,
    false
  );
  let notified = await openFromContextMenu(
    browser,
    TOP_SITE_SELECTOR,
    "context-openlinkintab"
  );
  let openedTab = await newTabPromise;

  is(notified.url, TEST_LINK, "Notified the top site url");
  is(notified.sourceTabBrowser, browser, "Notified the newtab browser");
  is(
    notified.createdTabBrowser,
    openedTab.linkedBrowser,
    "Notified the browser the link opened in"
  );
  isnot(
    gBrowser.selectedBrowser,
    notified.createdTabBrowser,
    "A background open is not selected when notified"
  );
  checkTopSiteClick("true");

  BrowserTestUtils.removeTab(openedTab);
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_open_link_in_foreground_tab_is_selected() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.loadInBackground", false]],
  });
  let tab = await openNewtabWithTopSites();

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    TEST_LINK,
    false
  );
  let notified = await openFromContextMenu(
    tab.linkedBrowser,
    TOP_SITE_SELECTOR,
    "context-openlinkintab"
  );
  let openedTab = await newTabPromise;

  is(
    gBrowser.selectedBrowser,
    notified.createdTabBrowser,
    "A foreground open is already selected when notified"
  );
  checkTopSiteClick("false");

  BrowserTestUtils.removeTab(openedTab);
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_open_link_in_window_notifies() {
  let tab = await openNewtabWithTopSites();

  let windowPromise = BrowserTestUtils.waitForNewWindow({ url: TEST_LINK });
  let notified = await openFromContextMenu(
    tab.linkedBrowser,
    TOP_SITE_SELECTOR,
    "context-openlink"
  );
  let newWindow = await windowPromise;

  is(notified.url, TEST_LINK, "Notified the top site url");
  is(
    notified.sourceTabBrowser,
    tab.linkedBrowser,
    "Notified the newtab browser"
  );
  is(
    notified.createdTabBrowser,
    newWindow.gBrowser.selectedBrowser,
    "Notified the new window's browser"
  );
  checkTopSiteClick("false");

  await BrowserTestUtils.closeWindow(newWindow);
  BrowserTestUtils.removeTab(tab);
});

const STORY_SELECTOR = "[data-section-id='topstories'] .ds-card-link";
let storyTab;

/**
 * @returns {object|undefined} Extras of the recorded pocket.click.
 */
function recordedStoryClick() {
  let clicks = Glean.pocket.click.testGetValue() ?? [];
  is(clicks.length, 1, "Recorded 1 pocket.click");
  return clicks.at(-1)?.extra;
}

test_newtab({
  async before({ pushPrefs, tab }) {
    storyTab = tab;
    sinon
      .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
      .returns(
        "https://example.com/browser/browser/extensions/newtab/test/browser/link_open_stories.json"
      );
    await pushPrefs(
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({
          collapsible: true,
          enabled: true,
          personalized: true,
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ]
    );
  },
  test: async function test_story_open_from_context_menu_and_middle_click() {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          "[data-section-id='topstories'] .ds-card-link"
        ),
      "Story has rendered"
    );
  },
  async after() {
    let browser = storyTab.linkedBrowser;
    const STORY_LINK = "https://example.com/story";

    let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, STORY_LINK);
    await openFromContextMenu(browser, STORY_SELECTOR, "context-openlinkintab");
    BrowserTestUtils.removeTab(await newTabPromise);
    let extra = recordedStoryClick();
    is(
      extra?.event_source,
      "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK",
      "Recorded the context menu open"
    );
    is(extra?.position, "0", "Recorded the position the card reported");

    Services.fog.testResetFOG();
    newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, STORY_LINK);
    await BrowserTestUtils.synthesizeMouseAtCenter(
      STORY_SELECTOR,
      { button: 1 },
      browser
    );
    BrowserTestUtils.removeTab(await newTabPromise);
    extra = recordedStoryClick();
    is(
      extra?.event_source,
      "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK",
      "Recorded the middle-click open"
    );

    sinon.restore();
  },
});
