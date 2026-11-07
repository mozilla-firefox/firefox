/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/**
 * This test verifies behavior from bug 1732375:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=1732375
 *
 * If there are multiple tabs selected, the 'Close' entry
 * under the File menu should correctly reflect the number of
 * selected tabs
 */
add_task(async function test_menu_close_tab_count() {
  // Window should have one tab open already, so we
  // just need to add one more to have a total of two
  info("Adding new tabs");
  // eslint-disable-next-line sdl/no-insecure-url
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "http://example.com");

  info("Selecting all tabs");
  await gBrowser.selectAllTabs();
  is(gBrowser.multiSelectedTabsCount, 2, "Two (2) tabs are selected");

  let fileMenu = document.getElementById("menu_FilePopup");
  await simulateMenuOpen(fileMenu);

  let closeMenuEntry = document.getElementById("menu_close");
  let closeMenuL10nArgsObject = document.l10n.getAttributes(closeMenuEntry);

  is(
    closeMenuL10nArgsObject.args.tabCount,
    2,
    "Menu bar reflects multi-tab selection number (Close 2 Tabs)"
  );
  if (document.hasPendingL10nMutations) {
    await BrowserTestUtils.waitForEvent(document, "L10nMutationsFinished");
  }
  await simulateMenuClosed(fileMenu);

  let onClose = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabClose"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await onClose;

  info("Tabs closed");
});

add_task(async function test_menu_close_items_with_vertical_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  });
  await BrowserTestUtils.waitForMutationCondition(
    gNavToolbox,
    { attributeFilter: ["tabs-hidden"] },
    () => gNavToolbox.hasAttribute("tabs-hidden")
  );

  let { closeTab, closeWindow } = await openFileMenu(window);
  ok(!closeWindow.hidden, "Close Window is shown with vertical tabs");
  is(closeTab.getAttribute("label"), "Close Tab", "Close Tab names the tab");
  await simulateMenuClosed(document.getElementById("menu_FilePopup"));

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_menu_close_items_in_popup() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.disable_open_during_load", false]],
  });
  let newWin = BrowserTestUtils.waitForNewWindow();
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.open("about:blank", "", "popup");
  });
  let win = await newWin;

  let { closeTab, closeWindow } = await openFileMenu(win);
  ok(closeWindow.hidden, "Close Window is hidden in a popup");
  is(closeTab.getAttribute("label"), "Close", "Close doesn't mention the tab");
  is(closeTab.getAttribute("accesskey"), "C", "Close has an access key");
  await simulateMenuClosed(win.document.getElementById("menu_FilePopup"));

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

async function openFileMenu(win) {
  let doc = win.document;
  await simulateMenuOpen(doc.getElementById("menu_FilePopup"));
  if (doc.hasPendingL10nMutations) {
    await BrowserTestUtils.waitForEvent(doc, "L10nMutationsFinished");
  }
  return {
    closeTab: doc.getElementById("menu_close"),
    closeWindow: doc.getElementById("menu_closeWindow"),
  };
}

async function simulateMenuOpen(menu) {
  let { MouseEvent } = menu.documentGlobal;
  return new Promise(resolve => {
    menu.addEventListener("popupshown", resolve, { once: true });
    menu.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
    menu.dispatchEvent(new MouseEvent("popupshown", { bubbles: true }));
  });
}

async function simulateMenuClosed(menu) {
  let { MouseEvent } = menu.documentGlobal;
  return new Promise(resolve => {
    menu.addEventListener("popuphidden", resolve, { once: true });
    menu.dispatchEvent(new MouseEvent("popuphiding", { bubbles: true }));
    menu.dispatchEvent(new MouseEvent("popuphidden", { bubbles: true }));
  });
}
