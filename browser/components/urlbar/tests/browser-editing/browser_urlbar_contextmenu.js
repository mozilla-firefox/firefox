/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.contextMenu.featureGate", true]],
  });

  // Add visits so that it can be autofilled.
  await PlacesTestUtils.addVisits([
    {
      uri: "https://example.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ]);
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function basic() {
  const TEST_CASES = [
    {
      preferences: [["browser.tabs.loadInBackground", true]],
      openIn: "tab",
      expectedTarget: "tab",
      expectedOption: { background: true },
    },
    {
      preferences: [["browser.tabs.loadInBackground", false]],
      openIn: "tab",
      expectedTarget: "tab",
    },
    {
      preferences: [["browser.tabs.loadInBackground", true]],
      openIn: "container-tab",
      expectedTarget: "tab",
      expectedOption: { background: true, userContextId: 1 },
    },
    {
      preferences: [["browser.tabs.loadInBackground", false]],
      openIn: "container-tab",
      expectedTarget: "tab",
      expectedOption: { userContextId: 3 },
    },
    {
      openIn: "window",
      expectedTarget: "window",
    },
    {
      openIn: "private-window",
      expectedTarget: "window",
      expectedOption: { private: true },
    },
  ];

  for (let {
    preferences = [],
    openIn,
    expectedTarget,
    expectedOption = {},
  } of TEST_CASES) {
    info(`Test for ${JSON.stringify({ preferences, openIn, expectedOption })}`);

    info("Set preferences");
    await SpecialPowers.pushPrefEnv({ set: preferences });

    let onSuggestionOpen =
      expectedTarget == "tab"
        ? BrowserTestUtils.waitForNewTab(gBrowser, "https://example.com/")
        : BrowserTestUtils.waitForNewWindow({ url: "https://example.com/" });

    let menu = await openContextMenuOnFirstResult();
    let menuItem = menu.querySelector(`[data-open-in="${openIn}"]`);
    Assert.ok(menuItem, `Found the menu item for ${openIn}`);

    if (expectedOption.userContextId) {
      let subMenuItem = await openContainerSubMenuItem(
        menuItem,
        expectedOption.userContextId
      );
      subMenuItem.click();
    } else {
      menuItem.click();
    }

    let target = await onSuggestionOpen;
    switch (expectedTarget) {
      case "tab": {
        Assert.equal(Cu.getClassName(target, true), "XULElement");
        Assert.equal(target.localName, "tab");
        if (expectedOption.background) {
          Assert.notEqual(target, gBrowser.selectedTab);
        } else {
          await TestUtils.waitForCondition(
            () => target == gBrowser.selectedTab
          );
          Assert.equal(target, gBrowser.selectedTab);
        }

        if (expectedOption.userContextId) {
          Assert.equal(
            target.getAttribute("usercontextid"),
            expectedOption.userContextId
          );
          let openedEvent = Glean.containers.containerTabOpened
            .testGetValue()
            .at(-1);
          Assert.equal(
            openedEvent.extra.source,
            "urlbar_result_context_menu",
            "container_tab_opened reports the urlbar source"
          );
        } else {
          Assert.ok(!target.hasAttribute("usercontextid"));
        }
        BrowserTestUtils.removeTab(target);
        break;
      }
      case "window": {
        Assert.equal(Cu.getClassName(target, true), "Window");
        Assert.equal(
          PrivateBrowsingUtils.isWindowPrivate(target),
          !!expectedOption.private
        );
        target.close();
        break;
      }
    }
  }

  await PlacesUtils.history.clear();
});

// The three-dot button and a right-click open the same menu, and it holds the
// result's own commands as well as the ones that open it in a new target.
add_task(async function same_menu_from_both_triggers() {
  await PlacesTestUtils.addVisits(["https://example.com/"]);

  let resultIndex = await promiseResultWithMenuButton();
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    resultIndex
  );

  await UrlbarTestUtils.openResultMenu(window, { resultIndex, byMouse: true });
  let fromMenuButton = await promiseMenuDescription();
  gURLBar.view.resultMenu.hide(undefined, { force: true });

  let menu = await openContextMenu(element.row);
  let fromContextMenu = await promiseMenuDescription();
  menu.hide(undefined, { force: true });

  Assert.deepEqual(
    fromContextMenu,
    fromMenuButton,
    "Both triggers open the same menu"
  );
  Assert.deepEqual(
    fromMenuButton.filter(item => item.openIn),
    ["tab", "container-tab", "window", "private-window"].map(openIn => ({
      openIn,
    })),
    "The menu opens the result in a new target"
  );
  Assert.ok(
    fromMenuButton.some(item => item.command),
    "The menu keeps the result's own commands"
  );

  gURLBar.view.close();
  await PlacesUtils.history.clear();
});

// Every result the menu can open shows a menu button, even one with no
// commands of its own, except the heuristic result, so that the first Tab press
// still moves to the second row.
add_task(async function menu_button_on_openable_rows() {
  await PlacesTestUtils.addVisits(["https://example.com/"]);
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "https://example.com/bookmark",
    title: "example bookmark",
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "example bookmark",
    window,
    fireInputEvent: true,
  });

  let rowCount = UrlbarTestUtils.getResultCount(window);
  let bookmarkIndex = -1;
  for (let i = 0; i < rowCount; i++) {
    let { element, url, source, heuristic } =
      await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.equal(
      element.row.hasAttribute("has-menu-button"),
      !!url && !heuristic,
      `Menu button on the result at index ${i}`
    );
    if (source == UrlbarShared.RESULT_SOURCE.BOOKMARKS) {
      bookmarkIndex = i;
    }
  }
  Assert.greater(bookmarkIndex, 0, "The bookmark is one of the results");

  await UrlbarTestUtils.openResultMenu(window, {
    resultIndex: bookmarkIndex,
    byMouse: true,
  });
  Assert.deepEqual(
    await promiseMenuDescription(),
    [
      ...["tab", "container-tab", "window", "private-window"].map(openIn => ({
        openIn,
      })),
    ],
    "The bookmark has no commands of its own, so its menu only opens it"
  );
  gURLBar.view.resultMenu.hide(undefined, { force: true });

  Assert.equal(
    UrlbarTestUtils.getSelectedRowIndex(window),
    0,
    "The heuristic result is selected"
  );
  EventUtils.synthesizeKey("KEY_Tab");
  Assert.equal(
    UrlbarTestUtils.getSelectedRowIndex(window),
    1,
    "Tab moves to the second row"
  );
  Assert.ok(
    UrlbarTestUtils.getSelectedElement(window).classList.contains(
      "urlbarView-row-inner"
    ),
    "Tab selects the row rather than a button in it"
  );

  gURLBar.view.close();
  await PlacesUtils.bookmarks.remove(bookmark);
  await PlacesUtils.history.clear();
});

// With the feature gate off, the three-dot menu holds only the result's own
// commands and a right-click opens nothing.
add_task(async function feature_gate_off() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.contextMenu.featureGate", false]],
  });
  await PlacesTestUtils.addVisits(["https://example.com/"]);

  let resultIndex = await promiseResultWithMenuButton();
  await UrlbarTestUtils.openResultMenu(window, { resultIndex, byMouse: true });
  let items = await promiseMenuDescription();
  Assert.ok(
    items.some(item => item.command),
    "The menu holds the result's own commands"
  );
  Assert.ok(
    items.every(item => !item.openIn),
    "The menu doesn't open the result in a new target"
  );
  gURLBar.view.resultMenu.hide(undefined, { force: true });

  gURLBar.view.close();
  await PlacesUtils.history.clear();
  await SpecialPowers.popPrefEnv();
});

add_task(async function toolbar_context_menu() {
  let TEST_TARGETS = [
    ".searchmode-switcher",
    "#trust-icon-container",
    "#identity-box",
  ];

  await BrowserTestUtils.withNewTab("https://example.com/", async () => {
    // Make search mode switcher visible.
    document.querySelector(".searchmode-switcher").focus();

    for (let target of TEST_TARGETS) {
      info(`Test for ${target}`);
      let element = document.querySelector(target);
      let onPopupShown = BrowserTestUtils.waitForEvent(document, "popupshown");
      EventUtils.synthesizeMouseAtCenter(element, {
        type: "contextmenu",
        button: 2,
      });
      let { target: popup } = await onPopupShown;
      Assert.equal(popup.id, "toolbar-context-menu");
      popup.hidePopup();
    }
  });
});

add_task(async function no_context_menu() {
  let TEST_DATA = [
    {
      featureGate: false,
      target: ".urlbarView-row",
    },
    {
      featureGate: false,
      target: ".urlbar-background",
    },
    {
      featureGate: true,
      target: ".urlbar-background",
    },
  ];

  for (let { featureGate, target } of TEST_DATA) {
    info(`Test for ${JSON.stringify({ featureGate, target })}`);
    await SpecialPowers.pushPrefEnv({
      set: [["browser.urlbar.contextMenu.featureGate", featureGate]],
    });

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      value: "exa",
      window,
      fireInputEvent: true,
    });

    let onContextMenu = BrowserTestUtils.waitForEvent(window, "contextmenu");
    let menuShown = false;
    let menuListener = () => {
      menuShown = true;
    };
    window.addEventListener("showing", menuListener, true);

    document.querySelector(target).dispatchEvent(
      new PointerEvent("contextmenu", {
        bubbles: true,
        cancelable: true,
        button: 2,
        view: window,
      })
    );

    info("Waiting for context menu");
    let event = await onContextMenu;
    Assert.ok(event.defaultPrevented);

    Assert.ok(!menuShown);
    window.removeEventListener("showing", menuListener, true);

    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function keep_view_open_on_context_menu_mousedown() {
  await PlacesTestUtils.addVisits([
    {
      uri: "https://example.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ]);
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  let menu = await openContextMenuOnFirstResult();
  Assert.ok(
    gURLBar.view.isOpen,
    "The view should remain open after the context menu is shown"
  );

  info("Mouse down on a context menu item");
  let menuItem = menu.querySelector('[data-open-in="tab"]');
  EventUtils.synthesizeMouseAtCenter(menuItem, { type: "mousedown" });

  Assert.ok(
    gURLBar.view.isOpen,
    "The view stays open after a mousedown on the context menu"
  );

  let onNewTab = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "https://example.com/"
  );
  info("Release the mouse to complete the click");
  EventUtils.synthesizeMouseAtCenter(menuItem, { type: "mouseup" });
  BrowserTestUtils.removeTab(await onNewTab);
  gURLBar.view.close();
  await PlacesUtils.history.clear();
});

add_task(async function on_switch_to_tab() {
  let firstTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  let secondTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "example",
    window,
    fireInputEvent: true,
  });

  let targetElement;
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let { element, result } = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      i
    );
    if (result.type == UrlbarShared.RESULT_TYPE.TAB_SWITCH) {
      targetElement = element;
      break;
    }
  }
  Assert.ok(targetElement, "Switch-to-tab suggestion is found");
  Assert.ok(
    !targetElement.row.hasAttribute("has-menu-button"),
    "The switch-to-tab has no menu button"
  );

  let onContextMenu = BrowserTestUtils.waitForEvent(window, "contextmenu");
  let menuShown = false;
  let menuListener = () => {
    menuShown = true;
  };
  window.addEventListener("showing", menuListener, true);
  EventUtils.synthesizeMouseAtCenter(targetElement.row, {
    type: "contextmenu",
    button: 2,
  });
  await onContextMenu;
  Assert.ok(!menuShown, "A right-click on the switch-to-tab row opens no menu");
  window.removeEventListener("showing", menuListener, true);

  gURLBar.view.close();
  BrowserTestUtils.removeTab(firstTab);
  BrowserTestUtils.removeTab(secondTab);
  await PlacesUtils.history.clear();
});

add_task(async function menu_trigger_on_reopen() {
  await PlacesTestUtils.addVisits([
    "https://example.com/1",
    "https://example.com/2",
    "https://example.com/3",
    "https://example.com/4",
  ]);
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "example.com/",
    window,
    fireInputEvent: true,
  });
  Assert.greaterOrEqual(
    UrlbarTestUtils.getResultCount(window),
    4,
    "There are enough results"
  );
  // Row B is above row A so that the menu opened on row A doesn't cover it.
  let rowA = (await UrlbarTestUtils.getDetailsOfResultAt(window, 3)).element
    .row;
  let rowB = (await UrlbarTestUtils.getDetailsOfResultAt(window, 1)).element
    .row;

  info("Open the context menu on A");
  await openContextMenu(rowA);
  Assert.ok(rowA.hasAttribute("menu-trigger"), "Row A has menu-trigger");
  info("Then, open the context menu on B");
  await openContextMenu(rowB);
  await new Promise(resolve => requestAnimationFrame(resolve));
  Assert.ok(rowB.hasAttribute("menu-trigger"), "Row B has menu-trigger");
  Assert.ok(
    !rowA.hasAttribute("menu-trigger"),
    "Row A no longer has menu-trigger"
  );

  info("Reopen on the same row, away from where the menu was opened");
  await openContextMenu(rowB);
  await new Promise(resolve => requestAnimationFrame(resolve));
  Assert.ok(
    rowB.hasAttribute("menu-trigger"),
    "Row B keeps menu-trigger after the menu is reopened on it"
  );
  Assert.ok(
    !rowA.hasAttribute("menu-trigger"),
    "Row A does not have menu-trigger"
  );

  let onHidden = BrowserTestUtils.waitForEvent(
    gURLBar.view.resultMenu,
    "hidden"
  );
  gURLBar.view.resultMenu.hide(undefined, { force: true });
  await onHidden;
  Assert.ok(
    !rowB.hasAttribute("menu-trigger"),
    "Row B no longer has menu-trigger after the menu is hidden"
  );

  gURLBar.view.close();
  await PlacesUtils.history.clear();
});

// No container item carries an accesskey, so the initial of a name the user
// chose selects it, cycling among every container that shares the letter.
add_task(async function container_first_letter_selection() {
  let custom = ["Bakery", "Bagels"].map(name =>
    ContextualIdentityService.create(name, "circle", "purple")
  );
  registerCleanupFunction(() => {
    for (let { userContextId } of custom) {
      ContextualIdentityService.remove(userContextId);
    }
  });

  let menu = await openContextMenuOnFirstResult();
  let subMenu = await openContainerSubMenu(
    menu.querySelector('[data-open-in="container-tab"]')
  );
  await TestUtils.waitForCondition(
    () => subMenu.querySelector("[data-usercontextid]")?.textContent,
    "Waiting for the container items to be labeled"
  );

  // First-letter selection needs focus inside the panel, where opening the
  // submenu by keyboard puts it.
  subMenu.querySelector("panel-item").focus();
  let focusedLabel = () =>
    subMenu.getRootNode().activeElement?.textContent.trim();

  for (let expected of ["Banking", "Bakery", "Bagels", "Banking"]) {
    EventUtils.synthesizeKey("b", {});
    Assert.equal(focusedLabel(), expected, `B selected ${expected}`);
  }

  menu.hide(undefined, { force: true });
  gURLBar.view.close();
});

// Returns the menu's items as the command or open-in target each one picks, in
// the order they are shown, separators included.
async function promiseMenuDescription() {
  let menu = gURLBar.view.resultMenu;
  await TestUtils.waitForCondition(
    () => menu.children.length,
    "Waiting for the menu to be populated"
  );
  return [...menu.children].map(item => {
    if (item.localName == "hr") {
      return "separator";
    }
    let { command, openIn } = item.dataset;
    return command ? { command } : { openIn };
  });
}

// Searches for "example" and returns the index of a result that has a menu
// button and commands of its own, and that the menu can open in a new target.
async function promiseResultWithMenuButton() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "example",
    window,
    fireInputEvent: true,
  });
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let { element, url, result } = await UrlbarTestUtils.getDetailsOfResultAt(
      window,
      i
    );
    if (
      url &&
      result.payload.isBlockable &&
      element.row.hasAttribute("has-menu-button")
    ) {
      return i;
    }
  }
  throw new Error("No result with a menu button");
}

// Right-clicks the row at its center.
async function openContextMenu(row) {
  info("Open the context menu");
  let menu = gURLBar.view.resultMenu;

  let onShown = BrowserTestUtils.waitForEvent(menu, "shown");
  for (let type of ["mousedown", "contextmenu", "mouseup"]) {
    EventUtils.synthesizeMouseAtCenter(row, { button: 2, type });
  }
  await onShown;

  return menu;
}

async function openContextMenuOnFirstResult() {
  info("Open urlbar results");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "exa",
    window,
    fireInputEvent: true,
  });
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  return openContextMenu(element.row);
}

// Opens the submenu of the given item, the same way hovering it does, and
// returns the submenu panel.
async function openContainerSubMenu(menuItem) {
  let onShown = BrowserTestUtils.waitForEvent(menuItem.submenuPanel, "shown");
  menuItem.dispatchEvent(
    new MouseEvent("mouseenter", { view: menuItem.ownerGlobal })
  );
  await onShown;
  return menuItem.submenuPanel;
}

async function openContainerSubMenuItem(menuItem, userContextId) {
  let subMenu = await openContainerSubMenu(menuItem);
  let subMenuItem = subMenu.querySelector(
    `[data-usercontextid="${userContextId}"]`
  );
  Assert.ok(subMenuItem, `Found the container item for ${userContextId}`);
  await TestUtils.waitForCondition(
    () => subMenuItem.textContent,
    "Waiting for the container item to be labeled"
  );
  Assert.ok(
    !subMenuItem.hasAttribute("accesskey"),
    `The container item for ${userContextId} should have no access key`
  );
  return subMenuItem;
}
