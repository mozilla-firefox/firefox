/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// The newtab search bar's result menu opens a result in a new tab, window or
// container tab. The containers come from the parent process, since
// `ContextualIdentityService` isn't reachable from the page.

"use strict";

const CONFIG = [{ identifier: "engine1" }];

// A form history entry, which gives its row a result menu.
const SUGGESTION = "example suggestion";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.contextMenu.featureGate", true],
      ["browser.urlbar.resultTargetMenu.featureGate", true],
      ["privacy.userContext.enabled", true],
    ],
  });
  await SearchTestUtils.updateRemoteSettingsConfig(CONFIG);
  await NewtabSearchbarTestUtils.formHistory.add([SUGGESTION]);
});

add_task(async function offersTheOpenInCommands() {
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let resultIndex = await promiseRowWithMenu(tab.linkedBrowser);

  await openMenu(tab.linkedBrowser, resultIndex);
  let items = await NewtabSearchbarTestUtils.spawn(tab.linkedBrowser, [], () =>
    [
      ...NewtabSearchbarContentTestUtils.getUrlbar(content).view.resultMenu
        .children,
    ].map(item => item.dataset.openIn ?? item.dataset.command ?? item.localName)
  );

  Assert.deepEqual(
    items.slice(0, 5),
    ["tab", "container-tab", "window", "private-window", "hr"],
    "The menu opens the result in a new target"
  );
  Assert.deepEqual(
    items.slice(5),
    ["dismiss", "help", "hr", "toggle-keyboard-accessible"],
    "The menu keeps the row's own commands, followed by the tab-skipping checkbox"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function opensInANewTab() {
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let resultIndex = await promiseRowWithMenu(tab.linkedBrowser);
  let { url } = await NewtabSearchbarTestUtils.getDetailsOfResultAt(
    tab.linkedBrowser,
    resultIndex
  );

  let opened = BrowserTestUtils.waitForNewTab(gBrowser, url, true);
  await openMenu(tab.linkedBrowser, resultIndex);
  await pickMenuItem(tab.linkedBrowser, '[data-open-in="tab"]');
  let openedTab = await opened;

  Assert.equal(
    tab.linkedBrowser.currentURI.spec,
    "about:newtab",
    "The page the bar is on stayed where it was"
  );

  BrowserTestUtils.removeTab(openedTab);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function opensInAContainerTab() {
  Services.fog.testResetFOG();

  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let resultIndex = await promiseRowWithMenu(tab.linkedBrowser);
  let { url } = await NewtabSearchbarTestUtils.getDetailsOfResultAt(
    tab.linkedBrowser,
    resultIndex
  );

  await openMenu(tab.linkedBrowser, resultIndex);
  let submenu = await promiseSubmenuItems(tab.linkedBrowser);

  let containers = ContextualIdentityService.getPublicIdentities();
  Assert.deepEqual(
    submenu.filter(item => item.userContextId).map(item => item.label),
    containers.map(identity =>
      ContextualIdentityService.getUserContextLabel(identity.userContextId)
    ),
    "The submenu lists the containers, labeled in the parent process"
  );
  Assert.equal(
    submenu.length,
    containers.length + 3,
    "The submenu also holds a separator and the add and manage items"
  );

  let { userContextId } = containers.at(-1);
  let opened = BrowserTestUtils.waitForNewTab(gBrowser, url, true);
  await pickMenuItem(
    tab.linkedBrowser,
    `[data-usercontextid="${userContextId}"]`,
    true
  );
  let openedTab = await opened;

  Assert.equal(
    openedTab.getAttribute("usercontextid"),
    String(userContextId),
    "The result opened in the picked container"
  );
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.containers.containerTabOpened.testGetValue().at(-1).extra.source,
    "urlbar_result_context_menu",
    "container_tab_opened reports the bar's source"
  );

  BrowserTestUtils.removeTab(openedTab);
  BrowserTestUtils.removeTab(tab);
});

// The submenu asks the parent for the containers each time it is shown, so it
// offers one that was added while the page was open.
add_task(async function offersANewContainer() {
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let resultIndex = await promiseRowWithMenu(tab.linkedBrowser);

  registerCleanupFunction(() => ContextualIdentityService.resetDefault());
  ContextualIdentityService.create("Test container", "briefcase", "blue");
  let { userContextId } =
    ContextualIdentityService.getPublicIdentities().at(-1);

  await openMenu(tab.linkedBrowser, resultIndex);
  let submenu = await promiseSubmenuItems(tab.linkedBrowser);

  Assert.deepEqual(
    submenu.find(item => item.userContextId == userContextId),
    { userContextId: String(userContextId), label: "Test container" },
    "The submenu offers the container that was just added"
  );

  BrowserTestUtils.removeTab(tab);
});

/**
 * Searches for the seeded form history entry and returns the index of its row,
 * which is the row with a result menu.
 *
 * @param {MozBrowser} browser
 *   The browser the page is in.
 * @returns {Promise<number>}
 *   The row's index.
 */
async function promiseRowWithMenu(browser) {
  await NewtabSearchbarTestUtils.promiseAutocompleteResultPopup({
    browser,
    value: "example",
  });
  return NewtabSearchbarTestUtils.spawn(browser, [], () => {
    let utils = NewtabSearchbarContentTestUtils;
    utils.disableResultMenuAutohide(content);
    for (let i = 0; i < utils.getResultCount(content); i++) {
      if (utils.getButtonForResultIndex(content, "result-menu", i)) {
        return i;
      }
    }
    throw new Error("No row with a result menu");
  });
}

/**
 * Opens a row's result menu.
 *
 * @param {MozBrowser} browser
 *   The browser the page is in.
 * @param {number} resultIndex
 *   The index of the row whose menu to open.
 */
async function openMenu(browser, resultIndex) {
  await NewtabSearchbarTestUtils.spawn(browser, [resultIndex], index =>
    NewtabSearchbarContentTestUtils.openResultMenu(content, {
      resultIndex: index,
    })
  );
}

/**
 * Opens the submenu that lists the containers, the same way hovering it does,
 * and describes what it holds.
 *
 * @param {MozBrowser} browser
 *   The browser the page is in, with the result menu already open.
 * @returns {Promise<object[]>}
 *   The submenu's items, in the order they are shown.
 */
function promiseSubmenuItems(browser) {
  return NewtabSearchbarTestUtils.spawn(browser, [], async () => {
    let menuItem = NewtabSearchbarContentTestUtils.getUrlbar(
      content
    ).view.resultMenu.querySelector('[data-open-in="container-tab"]');

    // The panel's events are untrusted, this listener being a privileged one.
    let shown = ContentTaskUtils.waitForEvent(
      menuItem.submenuPanel,
      "shown",
      false,
      null,
      true
    );
    menuItem.dispatchEvent(
      new content.MouseEvent("mouseenter", { view: content })
    );
    await shown;

    let items = () => [...menuItem.submenuPanel.children];
    await ContentTaskUtils.waitForCondition(
      () =>
        items().every(
          item => item.localName != "panel-item" || item.textContent
        ),
      "waiting for the submenu's items to be labeled"
    );
    return items().map(item => ({
      userContextId: item.dataset.usercontextid,
      label: item.textContent,
    }));
  });
}

/**
 * Picks an item of the open result menu.
 *
 * @param {MozBrowser} browser
 *   The browser the page is in.
 * @param {string} selector
 *   Selects the item to pick.
 * @param {boolean} [inSubmenu]
 *   Whether the item is one of the containers, in the submenu.
 */
async function pickMenuItem(browser, selector, inSubmenu = false) {
  await NewtabSearchbarTestUtils.spawn(
    browser,
    [selector, inSubmenu],
    (itemSelector, fromSubmenu) => {
      let menu =
        NewtabSearchbarContentTestUtils.getUrlbar(content).view.resultMenu;
      // A submenu lives in its item's shadow root, out of the menu's reach.
      let root = fromSubmenu
        ? menu.querySelector('[data-open-in="container-tab"]').submenuPanel
        : menu;
      root.querySelector(itemSelector).click();
    }
  );
}
