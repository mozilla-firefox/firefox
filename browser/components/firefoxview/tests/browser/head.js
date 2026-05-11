/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const {
  getFirefoxViewURL,
  switchToWindow,
  withFirefoxView,
  assertFirefoxViewTab,
  assertFirefoxViewTabSelected,
  openFirefoxViewTab,
  closeFirefoxViewTab,
  isFirefoxViewTabSelectedInWindow,
  init: FirefoxViewTestUtilsInit,
} = ChromeUtils.importESModule(
  "resource://testing-common/FirefoxViewTestUtils.sys.mjs"
);

/* exported testVisibility */

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { FeatureCalloutMessages } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/FeatureCalloutMessages.sys.mjs"
);
const { NonPrivateTabs } = ChromeUtils.importESModule(
  "resource:///modules/OpenTabs.sys.mjs"
);
// shut down the open tabs module after each test so we don't get debounced events bleeding into the next
registerCleanupFunction(() => NonPrivateTabs.stop());

const triggeringPrincipal_base64 = E10SUtils.SERIALIZED_SYSTEMPRINCIPAL;
const { SessionStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SessionStoreTestUtils.sys.mjs"
);
SessionStoreTestUtils.init(this, window);
FirefoxViewTestUtilsInit(this, window);

ChromeUtils.defineESModuleGetters(this, {
  AboutWelcomeParent: "resource:///actors/AboutWelcomeParent.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  TabStateFlusher: "resource:///modules/sessionstore/TabStateFlusher.sys.mjs",
});

/**
 * URLs used for browser_recently_closed_tabs_keyboard and
 * browser_firefoxview_accessibility
 */
const URLs = [
  "http://mochi.test:8888/browser/",
  "https://www.example.com/",
  "https://example.net/",
  "https://example.org/",
  "about:robots",
  "https://www.mozilla.org/",
];

const syncedTabsData1 = [
  {
    id: 1,
    type: "client",
    name: "My desktop",
    clientType: "desktop",
    lastModified: 1655730486760,
    tabs: [
      {
        type: "tab",
        title: "Sandboxes - Sinon.JS",
        url: "https://sinonjs.org/releases/latest/sandbox/",
        icon: "https://sinonjs.org/assets/images/favicon.png",
        lastUsed: 1655391592, // Thu Jun 16 2022 14:59:52 GMT+0000
        client: 1,
      },
      {
        type: "tab",
        title: "Internet for people, not profits - Mozilla",
        url: "https://www.mozilla.org/",
        icon: "https://www.mozilla.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
        lastUsed: 1655730486, // Mon Jun 20 2022 13:08:06 GMT+0000
        client: 1,
      },
    ],
  },
  {
    id: 2,
    type: "client",
    name: "My iphone",
    clientType: "phone",
    lastModified: 1655727832930,
    tabs: [
      {
        type: "tab",
        title: "The Guardian",
        url: "https://www.theguardian.com/",
        icon: "page-icon:https://www.theguardian.com/",
        lastUsed: 1655291890, // Wed Jun 15 2022 11:18:10 GMT+0000
        client: 2,
      },
      {
        type: "tab",
        title: "The Times",
        url: "https://www.thetimes.co.uk/",
        icon: "page-icon:https://www.thetimes.co.uk/",
        lastUsed: 1655727485, // Mon Jun 20 2022 12:18:05 GMT+0000
        client: 2,
      },
    ],
  },
];

function testVisibility(browser, expected) {
  const { document } = browser.contentWindow;
  for (let [selector, shouldBeVisible] of Object.entries(
    expected.expectedVisible
  )) {
    const elem = document.querySelector(selector);
    if (shouldBeVisible) {
      ok(
        BrowserTestUtils.isVisible(elem),
        `Expected ${selector} to be visible`
      );
    } else {
      ok(BrowserTestUtils.isHidden(elem), `Expected ${selector} to be hidden`);
    }
  }
}

async function waitForElementVisible(browser, selector, isVisible = true) {
  const { document } = browser.contentWindow;
  const elem = document.querySelector(selector);
  if (!isVisible && !elem) {
    return;
  }
  ok(elem, `Got element with selector: ${selector}`);

  await BrowserTestUtils.waitForMutationCondition(
    elem,
    {
      attributeFilter: ["hidden"],
    },
    () => {
      return isVisible
        ? BrowserTestUtils.isVisible(elem)
        : BrowserTestUtils.isHidden(elem);
    }
  );
}

async function waitForVisibleSetupStep(browser, expected) {
  const { document } = browser.contentWindow;

  const deck = document.querySelector(".sync-setup-container");
  const nextStepElem = deck.querySelector(expected.expectedVisible);
  const stepElems = deck.querySelectorAll(".setup-step");

  await BrowserTestUtils.waitForMutationCondition(
    deck,
    {
      attributeFilter: ["selected-view"],
    },
    () => {
      return BrowserTestUtils.isVisible(nextStepElem);
    }
  );

  for (let elem of stepElems) {
    if (elem == nextStepElem) {
      ok(
        BrowserTestUtils.isVisible(elem),
        `Expected ${elem.id || elem.className} to be visible`
      );
    } else {
      ok(
        BrowserTestUtils.isHidden(elem),
        `Expected ${elem.id || elem.className} to be hidden`
      );
    }
  }
}

var gMockFxaDevices = null;
var gUIStateStatus;
var gSandbox;
function setupSyncFxAMocks({ fxaDevices = null, state, syncEnabled = true }) {
  gUIStateStatus = state || UIState.STATUS_SIGNED_IN;
  if (gSandbox) {
    gSandbox.restore();
  }
  const sandbox = (gSandbox = sinon.createSandbox());
  gMockFxaDevices = fxaDevices;
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => fxaDevices);
  sandbox.stub(UIState, "get").callsFake(() => {
    return {
      status: gUIStateStatus,
      syncEnabled,
      email:
        gUIStateStatus === UIState.STATUS_NOT_CONFIGURED
          ? undefined
          : "email@example.com",
    };
  });

  return sandbox;
}

function setupRecentDeviceListMocks() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => [
    {
      id: 1,
      name: "My desktop",
      isCurrentDevice: true,
      type: "desktop",
      availableCommands: {},
    },
    {
      id: 2,
      name: "My iphone",
      type: "mobile",
      availableCommands: {},
    },
  ]);

  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "email@example.com",
  });

  // whatever was passed in was the "found" client
  sandbox
    .stub(SyncedTabs._internal, "_getClientFxaDeviceId")
    .callsFake(clientId => clientId);

  return sandbox;
}

function getMockTabData(clients) {
  return SyncedTabs._internal._createRecentTabsList(clients, 10);
}

async function setupListState(browser) {
  // Skip the synced tabs sign up flow to get to a loaded list state
  await SpecialPowers.pushPrefEnv({
    set: [["services.sync.engine.tabs", true]],
  });

  UIState.refresh();
  const recentFetchTime = Math.floor(Date.now() / 1000);
  info("updating lastFetch:" + recentFetchTime);
  Services.prefs.setIntPref("services.sync.lastTabFetch", recentFetchTime);

  Services.obs.notifyObservers(null, UIState.ON_UPDATE);

  await waitForElementVisible(browser, "#tabpickup-steps", false);
  await waitForElementVisible(browser, "#tabpickup-tabs-container", true);

  const tabsContainer = browser.contentWindow.document.querySelector(
    "#tabpickup-tabs-container"
  );
  await tabsContainer.tabListAdded;
  await BrowserTestUtils.waitForMutationCondition(
    tabsContainer,
    { attributeFilter: ["class"], attributes: true },
    () => {
      return !tabsContainer.classList.contains("loading");
    }
  );
  info("tabsContainer isn't loading anymore, returning");
}

async function touchLastTabFetch() {
  // lastTabFetch stores a timestamp in *seconds*.
  const nowSeconds = Math.floor(Date.now() / 1000);
  info("updating lastFetch:" + nowSeconds);
  Services.prefs.setIntPref("services.sync.lastTabFetch", nowSeconds);
  // wait so all pref observers can complete
  await TestUtils.waitForTick();
}

let gUIStateSyncEnabled;
function setupMocks({ fxaDevices = null, state, syncEnabled = true }) {
  gUIStateStatus = state || UIState.STATUS_SIGNED_IN;
  gUIStateSyncEnabled = syncEnabled;
  if (gSandbox) {
    gSandbox.restore();
  }
  const sandbox = (gSandbox = sinon.createSandbox());
  gMockFxaDevices = fxaDevices;
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => fxaDevices);
  sandbox.stub(UIState, "get").callsFake(() => {
    return {
      status: gUIStateStatus,
      // Sometimes syncEnabled is not present on UIState, for example when the user signs
      // out the state is just { status: "not_configured" }
      ...(gUIStateSyncEnabled != undefined && {
        syncEnabled: gUIStateSyncEnabled,
      }),
    };
  });

  // whatever was passed in was the "found" client
  sandbox
    .stub(SyncedTabs._internal, "_getClientFxaDeviceId")
    .callsFake(clientId => clientId);
  // This is converting the device list to a client list.
  // There are two primary differences:
  // 1. The client list doesn't return the current device.
  // 2. It uses clientType instead of type.
  let tabClients = fxaDevices ? [...fxaDevices] : [];
  for (let client of tabClients) {
    client.clientType = client.type;
  }
  tabClients = tabClients.filter(device => !device.isCurrentDevice);
  sandbox.stub(SyncedTabs, "getTabClients").callsFake(() => {
    return Promise.resolve(tabClients);
  });
  return sandbox;
}

async function tearDown(sandbox) {
  sandbox?.restore();
  Services.prefs.clearUserPref("services.sync.lastTabFetch");
}

/**
 * Helper function to open and close a tab so the recently
 * closed tabs list can have data.
 *
 * @param {string} url
 * @returns {Promise} Promise that resolves when the session store
 * has been updated after closing the tab.
 */
async function open_then_close(url, win = window) {
  return SessionStoreTestUtils.openAndCloseTab(win, url);
}

/**
 * Clears session history. Used to clear out the recently closed tabs list.
 *
 */
function clearHistory() {
  Services.obs.notifyObservers(null, "browser:purge-session-history");
}

/**
 * Cleanup function for tab pickup tests.
 *
 */
function cleanup_tab_pickup() {
  Services.prefs.clearUserPref("services.sync.engine.tabs");
  Services.prefs.clearUserPref("services.sync.lastTabFetch");
}

function isFirefoxViewTabSelected(win = window) {
  return isFirefoxViewTabSelectedInWindow(win);
}

function promiseAllButPrimaryWindowClosed() {
  let windows = [];
  for (let win of BrowserWindowTracker.orderedWindows) {
    if (win != window) {
      windows.push(win);
    }
  }
  return Promise.all(windows.map(BrowserTestUtils.closeWindow));
}

registerCleanupFunction(() => {
  // ensure all the stubs are restored, regardless of any exceptions
  // that might have prevented it
  gSandbox?.restore();
});

async function navigateToViewAndWait(document, view) {
  info(`navigateToViewAndWait, for ${view}`);
  const navigation = document.querySelector("moz-page-nav");
  const win = document.documentGlobal;
  SimpleTest.promiseFocus(win);
  let navButton = Array.from(navigation.pageNavButtons).find(pageNavButton => {
    return pageNavButton.view === view;
  });
  const namedDeck = document.querySelector("named-deck");

  await BrowserTestUtils.waitForCondition(
    () => navButton.getBoundingClientRect().height,
    `Waiting for ${view} button to be clickable`
  );

  EventUtils.synthesizeMouseAtCenter(navButton, {}, win);

  await BrowserTestUtils.waitForCondition(() => {
    let selectedView = Array.from(namedDeck.children).find(
      child => child.slot == "selected"
    );
    return (
      namedDeck.selectedViewName == view &&
      selectedView?.getBoundingClientRect().height
    );
  }, `Waiting for ${view} to be visible`);
}

/**
 * Switch to the Firefox View tab.
 *
 * @param {Window} [win]
 *   The window to use, if specified. Defaults to the global window instance.
 * @returns {Promise<MozTabbrowserTab>}
 *   The tab switched to.
 */
async function switchToFxViewTab(win = window) {
  await switchToWindow(win);
  return BrowserTestUtils.switchTab(win.gBrowser, win.FirefoxViewHandler.tab);
}

function isElInViewport(element) {
  const boundingRect = element.getBoundingClientRect();
  return (
    boundingRect.top >= 0 &&
    boundingRect.left >= 0 &&
    boundingRect.bottom <=
      (window.innerHeight || document.documentElement.clientHeight) &&
    boundingRect.right <=
      (window.innerWidth || document.documentElement.clientWidth)
  );
}

// TODO once we port over old tests, helpers and cleanup old firefox view
// we should decide whether to keep this or openFirefoxViewTab.
async function clickFirefoxViewButton(win) {
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#firefox-view-button",
    { type: "mousedown" },
    win.browsingContext
  );
}

function setSortOption(component, value) {
  info(`Sort by ${value}.`);
  const el = component.optionsContainer.querySelector(
    `input[value='${value}']`
  );
  EventUtils.synthesizeMouseAtCenter(el, {}, el.documentGlobal);
}

/**
 * Select the Open Tabs view-page in the Firefox View tab.
 */
async function navigateToOpenTabs(browser) {
  const document = browser.contentDocument;
  if (document.querySelector("named-deck").selectedViewName != "opentabs") {
    await navigateToViewAndWait(browser.contentDocument, "opentabs");
  }
}

function getOpenTabsCards(openTabs) {
  return openTabs.shadowRoot.querySelectorAll("view-opentabs-card");
}

function getOpenTabsComponent(browser) {
  return browser.contentDocument.querySelector("named-deck > view-opentabs");
}

async function getTabRowsForCard(card) {
  await TestUtils.waitForCondition(
    () => card.tabList.rowEls.length,
    "Wait for the card's tab list to have rows"
  );
  return card.tabList.rowEls;
}

async function click_recently_closed_tab_item(itemElem, itemProperty = "") {
  // Make sure the firefoxview tab still has focus
  is(
    itemElem.ownerDocument.location.href,
    "about:firefoxview#recentlyclosed",
    "about:firefoxview is the selected tab and showing the Recently closed view page"
  );

  // Scroll to the tab element to ensure dismiss button is visible
  itemElem.scrollIntoView();
  is(isElInViewport(itemElem), true, "Tab is visible in viewport");
  let clickTarget;
  switch (itemProperty) {
    case "dismiss":
      clickTarget = itemElem.secondaryButtonEl;
      break;
    default:
      clickTarget = itemElem.mainEl;
      break;
  }

  const closedObjectsChangePromise = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  EventUtils.synthesizeMouseAtCenter(clickTarget, {}, itemElem.documentGlobal);
  await closedObjectsChangePromise;
}

async function waitForRecentlyClosedTabsList(doc) {
  let recentlyClosedComponent = doc.querySelector(
    "view-recentlyclosed:not([slot=recentlyclosed])"
  );
  // Check that the tabs list is rendered
  await TestUtils.waitForCondition(() => {
    return recentlyClosedComponent.cardEl;
  }, "Waiting for the recently closed component to have a card element");
  let cardContainer = recentlyClosedComponent.cardEl;
  let cardMainSlotNode = Array.from(
    cardContainer?.mainSlot?.assignedNodes()
  )[0];
  await TestUtils.waitForCondition(() => {
    return cardMainSlotNode.rowEls.length;
  }, "Waiting for the card main slot node to have row elements");
  return [cardMainSlotNode, cardMainSlotNode.rowEls];
}

async function add_new_tab(URL) {
  let tabChangeRaised = BrowserTestUtils.waitForEvent(
    NonPrivateTabs,
    "TabChange"
  );
  let tab = BrowserTestUtils.addTab(gBrowser, URL);
  // wait so we can reliably compare the tab URL
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await tabChangeRaised;
  return tab;
}

function isActiveElement(expectedLinkEl) {
  return expectedLinkEl.getRootNode().activeElement == expectedLinkEl;
}

function cleanupTabs() {
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs[0]);
  }
}
