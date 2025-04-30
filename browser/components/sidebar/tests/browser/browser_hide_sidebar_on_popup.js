/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const DOMAIN = "https://example.com/";
const PATH = "browser/browser/components/privatebrowsing/test/browser/";
const TOP_PAGE = DOMAIN + PATH + "empty_file.html";

async function test_sidebar_hidden_on_popup() {
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, true]],
  });
  await waitForTabstripOrientation("vertical");
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;

  const sidebar = document.getElementById("sidebar-main");
  info("Waiting for sidebar to be visible");
  await BrowserTestUtils.waitForMutationCondition(
    sidebar,
    { attributes: true, attributeFilter: ["hidden"] },
    () => BrowserTestUtils.isVisible(sidebar)
  );
  is(sidebar.hidden, false, "Sidebar is shown initially");

  let privateTab = win.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(privateTab, TOP_PAGE);
  await BrowserTestUtils.browserLoaded(privateTab);

  // Open a popup window
  let popup = BrowserTestUtils.waitForNewWindow();
  await SpecialPowers.spawn(privateTab, [], () => {
    content.window.open("empty_file.html", "_blank", "width=300,height=300");
  });
  popup = await popup;
  ok(!!popup, "Popup shown");

  // Give popup window a chance to display the sidebar (which it shouldn't).
  await new Promise(resolve => ChromeUtils.idleDispatch(resolve));

  const popupSidebar = popup.document.getElementById("sidebar-main");
  ok(popupSidebar.hidden, "Sidebar is hidden on popup window");

  const menubar = popup.document.getElementById("viewSidebarMenu");
  ok(
    Array.from(menubar.childNodes).every(
      menuItem => menuItem.getAttribute("disabled") == "true"
    ),
    "All View > Sidebar menu items are disabled on popup"
  );

  // Try to show the sidebar launcher using the keyboard shortcut
  popup.document.getElementById("toggleSidebarKb").doCommand();
  // Give popup window a chance to display the sidebar (which it shouldn't).
  await new Promise(resolve => ChromeUtils.idleDispatch(resolve));
  ok(popupSidebar.hidden, "Sidebar is still hidden on popup window");

  // Bug 1925451 - Check that vertical tabs are visible in new window after opening popup
  await BrowserTestUtils.closeWindow(popup);
  const win2 = await BrowserTestUtils.openNewBrowserWindow();
  let win2VerticalTabsContainer = win2.document.getElementById("vertical-tabs");
  ok(
    win2VerticalTabsContainer.children.length,
    "The #vertical-tabs container element has been popuplated with vertical tabs"
  );

  await BrowserTestUtils.closeWindow(win);
  await BrowserTestUtils.closeWindow(win2);
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_sidebar_hidden_on_popup_no_backup_state() {
  await test_sidebar_hidden_on_popup();
});

add_task(async function test_sidebar_hidden_on_popup_with_backup_state() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.backupState", `{"hidden": false}`]],
  });
  await test_sidebar_hidden_on_popup();
  await SpecialPowers.popPrefEnv();
});
