/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SORT_OPTION_PREF = "sidebar.history.sortOption";

const LOCAL_URL = "file:///local.html";
const COM_URL = "https://example.com/shared";
const NET_URL = "https://example.net/yesterday";
const ORG_URL = "https://example.org/today";

const TODAY_URLS = [LOCAL_URL, COM_URL, ORG_URL];
const YESTERDAY_URLS = [COM_URL, NET_URL];

const OPEN_ALL = "sidebar-history-context-open-all-in-tabs";
const DELETE_CARD = "sidebar-history-context-delete-card";
const FORGET_CARD_SITE = "sidebar-history-context-forget-card-site";

registerCleanupFunction(async () => {
  SidebarController.hide();
  await PlacesUtils.history.clear();
  Services.prefs.clearUserPref(SORT_OPTION_PREF);
});

function getStartOfToday() {
  const now = new Date();
  return new Date(now.getFullYear(), now.getMonth(), now.getDate());
}

/**
 * Reset history to the test pages, then show the sidebar sorted by
 * `sortOption` once `expectedCardCount` cards have rendered.
 *
 * @param {string} sortOption
 * @param {number} expectedCardCount
 */
async function showSortedHistory(sortOption, expectedCardCount) {
  SidebarController.hide();
  await PlacesUtils.history.clear();
  const today = new Date();
  const startOfToday = getStartOfToday();
  const yesterday = new Date(
    startOfToday.getFullYear(),
    startOfToday.getMonth(),
    startOfToday.getDate() - 1
  );
  const urls = new Set([...TODAY_URLS, ...YESTERDAY_URLS]);
  await PlacesUtils.history.insertMany(
    [...urls].map(url => ({
      url,
      visits: [
        TODAY_URLS.includes(url) && { date: today },
        YESTERDAY_URLS.includes(url) && { date: yesterday },
      ].filter(Boolean),
    }))
  );
  Services.prefs.setStringPref(SORT_OPTION_PREF, sortOption);
  const { component } = await showHistorySidebar();
  await TestUtils.waitForCondition(
    () => component.cards.length === expectedCardCount,
    `${expectedCardCount} cards are shown when sorted by ${sortOption}.`
  );
  await component.updateComplete;
  return component;
}

function getVisibleMenuItemIds(contextMenu) {
  return [...contextMenu.children]
    .filter(el => el.localName === "menuitem" && !el.hidden)
    .map(el => el.id);
}

/**
 * Right-click a card heading and check which commands are offered, then close
 * the menu.
 *
 * @param {MozCard} card
 * @param {string[]} expectedIds
 * @param {string} message
 */
async function assertCardMenuItems(card, expectedIds, message) {
  const contextMenu = SidebarController.currentContextMenu;
  await openAndWaitForContextMenu(contextMenu, card.summaryEl);
  Assert.deepEqual(getVisibleMenuItemIds(contextMenu), expectedIds, message);
  const hidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.hidePopup();
  await hidden;
}

async function activateCardMenuItem(card, id) {
  const contextMenu = SidebarController.currentContextMenu;
  await openAndWaitForContextMenu(contextMenu, card.summaryEl, () =>
    contextMenu.activateItem(document.getElementById(id))
  );
}

/**
 * Check on which days each page still has visits in history.
 *
 * @param {{[url: string]: string[]}} expected
 *   Maps URLs to the days ("today", "yesterday") they should have visits on.
 */
async function assertVisitDays(expected) {
  const startOfToday = getStartOfToday();
  for (const [url, days] of Object.entries(expected)) {
    const pageInfo = await PlacesUtils.history.fetch(url, {
      includeVisits: true,
    });
    const actual = [
      ...new Set(
        (pageInfo?.visits ?? []).map(({ date }) =>
          date >= startOfToday ? "today" : "yesterday"
        )
      ),
    ].sort();
    Assert.deepEqual(actual, days, `${url} has visits on: ${days}.`);
  }
}

/**
 * Open all pages under a card heading, and return the URLs passed to
 * openTabset.
 *
 * @param {MozCard} card
 * @returns {Promise<string[]>}
 */
async function openAllInTabs(card) {
  const stub = sinon.stub(PlacesUIUtils, "openTabset");
  try {
    await activateCardMenuItem(card, OPEN_ALL);
    await TestUtils.waitForCondition(() => stub.calledOnce);
    return stub.firstCall.args[0].map(({ uri }) => uri);
  } finally {
    stub.restore();
  }
}

add_task(async function test_date_card_context_menu() {
  const component = await showSortedHistory("date", 2);
  const [todayCard, yesterdayCard] = component.cards;

  await assertCardMenuItems(
    todayCard,
    [OPEN_ALL, DELETE_CARD],
    "Date cards offer Open All in Tabs and Delete."
  );

  info("Right-clicking a row still shows the row commands.");
  const contextMenu = SidebarController.currentContextMenu;
  const [row] = component.lists[0].rowEls;
  await openAndWaitForContextMenu(contextMenu, row.mainEl);
  const rowItems = getVisibleMenuItemIds(contextMenu);
  Assert.ok(
    rowItems.includes("sidebar-history-context-delete-page"),
    "Row commands are shown."
  );
  Assert.ok(
    !rowItems.includes(DELETE_CARD) && !rowItems.includes(OPEN_ALL),
    "Card commands are hidden for rows."
  );
  const hidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.hidePopup();
  await hidden;

  Assert.deepEqual(
    (await openAllInTabs(todayCard)).sort(),
    [...TODAY_URLS].sort(),
    "Open All in Tabs opens every page visited today."
  );

  info("Delete yesterday's visits.");
  await activateCardMenuItem(yesterdayCard, DELETE_CARD);
  await TestUtils.waitForCondition(
    () => component.cards.length === 1,
    "Only today's card remains."
  );
  Assert.equal(
    component.cards[0].dataset.l10nId,
    "sidebar-history-date-today",
    "Today's card was not removed."
  );
  await assertVisitDays({
    [LOCAL_URL]: ["today"],
    [COM_URL]: ["today"],
    [NET_URL]: [],
    [ORG_URL]: ["today"],
  });
});

add_task(async function test_site_card_context_menu() {
  const component = await showSortedHistory("site", 4);
  const [fileCard, comCard, , orgCard] = component.cards;
  Assert.equal(comCard.heading, "example.com", "Cards are sorted by site.");

  await assertCardMenuItems(
    comCard,
    [OPEN_ALL, DELETE_CARD, FORGET_CARD_SITE],
    "Site cards offer Open All in Tabs, Delete and Clear All Data for Website."
  );
  await assertCardMenuItems(
    fileCard,
    [OPEN_ALL, DELETE_CARD],
    "Clear All Data for Website is hidden for local files."
  );

  Assert.deepEqual(
    await openAllInTabs(comCard),
    [COM_URL],
    "Open All in Tabs opens the pages of that site."
  );

  info("Clear All Data for Website asks about the site of the card.");
  const dialogOpened = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    "chrome://browser/content/places/clearDataForSite.xhtml",
    { isSubDialog: true }
  );
  await activateCardMenuItem(comCard, FORGET_CARD_SITE);
  const dialog = await dialogOpened;
  const { site } = JSON.parse(
    dialog.document
      .getElementById("clear-data-for-site-list")
      .getAttribute("data-l10n-args")
  );
  Assert.equal(site, "example.com", "The dialog is for the card's site.");
  const dialogClosed = BrowserTestUtils.waitForEvent(dialog, "unload");
  dialog.document.querySelector("dialog").cancelDialog();
  await dialogClosed;

  info("Delete the pages of one site.");
  await activateCardMenuItem(orgCard, DELETE_CARD);
  await TestUtils.waitForCondition(
    () => component.cards.length === 3,
    "The example.org card was removed."
  );
  await assertVisitDays({
    [LOCAL_URL]: ["today"],
    [COM_URL]: ["today", "yesterday"],
    [NET_URL]: ["yesterday"],
    [ORG_URL]: [],
  });

  info("Deleting a site card removes its pages on every day.");
  await activateCardMenuItem(component.cards[1], DELETE_CARD);
  await TestUtils.waitForCondition(
    () => component.cards.length === 2,
    "The example.com card was removed."
  );
  await assertVisitDays({ [COM_URL]: [] });
});

add_task(async function test_date_and_site_card_context_menu() {
  // Today and its three site cards, then yesterday and its two site cards.
  const component = await showSortedHistory("datesite", 7);
  const [todayCard, , comCard] = component.cards;
  Assert.ok(comCard.classList.contains("nested-card"), "Got a nested card.");
  Assert.equal(comCard.heading, "example.com", "Got the example.com card.");

  await assertCardMenuItems(
    todayCard,
    [OPEN_ALL, DELETE_CARD],
    "Date cards offer Open All in Tabs and Delete."
  );
  await assertCardMenuItems(
    comCard,
    [OPEN_ALL, DELETE_CARD, FORGET_CARD_SITE],
    "Nested site cards offer the site commands."
  );

  Assert.deepEqual(
    (await openAllInTabs(todayCard)).sort(),
    [...TODAY_URLS].sort(),
    "Open All in Tabs on a date card includes the pages of every site."
  );

  info("Delete today's visits to one site.");
  await activateCardMenuItem(comCard, DELETE_CARD);
  await TestUtils.waitForCondition(
    () => component.cards.length === 6,
    "The nested site card was removed."
  );
  await assertVisitDays({
    [LOCAL_URL]: ["today"],
    [COM_URL]: ["yesterday"],
    [NET_URL]: ["yesterday"],
    [ORG_URL]: ["today"],
  });

  info("Delete everything visited today.");
  await activateCardMenuItem(component.cards[0], DELETE_CARD);
  await TestUtils.waitForCondition(
    () => component.cards.length === 3,
    "Only yesterday's cards remain."
  );
  await assertVisitDays({
    [LOCAL_URL]: [],
    [COM_URL]: ["yesterday"],
    [NET_URL]: ["yesterday"],
    [ORG_URL]: [],
  });
});
