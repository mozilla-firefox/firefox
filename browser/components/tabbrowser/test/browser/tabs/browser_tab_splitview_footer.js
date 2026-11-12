/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_first", false]],
  });
});

async function setupSplitView() {
  info("Create new tabs.");
  const tabs = [
    BrowserTestUtils.addTab(gBrowser, "https://example.com"),
    BrowserTestUtils.addTab(gBrowser, "https://example.com"),
  ];
  await Promise.all(
    tabs.map(({ linkedBrowser }) =>
      BrowserTestUtils.browserLoaded(linkedBrowser)
    )
  );

  info("Add tabs into an active split view.");
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  const splitView = gBrowser.addTabSplitView(tabs);
  const tabpanels = document.getElementById("tabbrowser-tabpanels");
  await BrowserTestUtils.waitForMutationCondition(
    tabpanels,
    { attributes: true },
    () => tabpanels.hasAttribute("splitview")
  );
  for (const tab of tabs) {
    const tabPanel = document.getElementById(tab.linkedPanel);
    await BrowserTestUtils.waitForMutationCondition(
      tabPanel,
      { attributes: true },
      () => tabPanel.classList.contains("split-view-panel-active")
    );
  }

  return { tabs, splitView };
}

async function activateCommand(panel, command) {
  const footerMenu = document.getElementById("split-view-menu");
  const promiseShown = BrowserTestUtils.waitForPopupEvent(footerMenu, "shown");
  const footer = panel.querySelector("address-footer");
  // Only the urlbar menu is focusable, not the footer menu.
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(footer.menuButtonElement, {});
  AccessibilityUtils.resetEnv();
  await promiseShown;
  Assert.ok(
    BrowserTestUtils.isVisible(footer),
    "Footer remains present within the panel."
  );
  const item = footerMenu.querySelector(`menuitem[command="${command}"]`);
  footerMenu.activateItem(item);
}

add_task(async function test_displayed_over_inactive_panel() {
  const { tabs, splitView } = await setupSplitView();
  const [tab1, tab2] = tabs;

  const panel1 = document.getElementById(tab1.linkedPanel);
  const panel2 = document.getElementById(tab2.linkedPanel);
  const panel1Footer = panel1.querySelector("address-footer");
  const panel2Footer = panel2.querySelector("address-footer");

  info("Focus the first panel.");
  await SimpleTest.promiseFocus(tab1.linkedBrowser);
  Assert.ok(
    BrowserTestUtils.isHidden(panel1Footer),
    "First (active) panel does not contain a footer."
  );
  Assert.ok(
    BrowserTestUtils.isVisible(panel2Footer),
    "Second (inactive) panel contains a footer."
  );
  Assert.equal(
    panel2Footer.uriElement.textContent,
    "example.com",
    "Footer displays the domain name of the site."
  );

  info("Focus the second panel.");
  await SimpleTest.promiseFocus(tab2.linkedBrowser);
  Assert.ok(
    BrowserTestUtils.isVisible(panel1Footer),
    "First panel now contains a footer."
  );
  Assert.ok(
    BrowserTestUtils.isHidden(panel2Footer),
    "Second panel no longer contains a footer."
  );

  info("Navigate to a different location.");
  const promiseLoaded = BrowserTestUtils.browserLoaded(tab1.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab1.linkedBrowser, "about:robots");
  await promiseLoaded;
  Assert.equal(
    panel1Footer.uriElement.textContent,
    "about:robots",
    "Footer displays the new location."
  );

  splitView.close();
});

add_task(async function test_security_warning() {
  const { tabs, splitView } = await setupSplitView();
  const [tab1, tab2] = tabs;
  await SimpleTest.promiseFocus(tab1.linkedBrowser);

  const inactivePanel = document.getElementById(tab2.linkedPanel);
  const footer = inactivePanel.querySelector("address-footer");
  Assert.ok(
    BrowserTestUtils.isHidden(footer.securityElement),
    "No security warning for HTTPS."
  );

  info("Load a site with an icon.");
  let promiseLoaded = BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab2.linkedBrowser,
    TEST_PATH + "file_withicon.html"
  );
  await promiseLoaded;
  if (!BrowserTestUtils.isVisible(footer.tabImageIconElement)) {
    await BrowserTestUtils.waitForEvent(footer.tabImageIconElement, "load");
  }
  Assert.ok(
    BrowserTestUtils.isVisible(footer.tabImageIconElement),
    "Show favicon for secure sites."
  );

  info("Load an insecure website.");
  promiseLoaded = BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab2.linkedBrowser,
    "http://example.com/" // eslint-disable-line sdl/no-insecure-url
  );
  await promiseLoaded;
  Assert.ok(
    BrowserTestUtils.isVisible(footer.securityElement),
    "Security warning for HTTP."
  );
  Assert.ok(
    footer.tabImageIconElement.hidden,
    "Icon is deliberately hidden for insecure sites."
  );
  Assert.ok(
    !footer.tabImageIconElement.src,
    "Icon has no src for insecure sites."
  );

  info("Load a local site.");
  promiseLoaded = BrowserTestUtils.browserLoaded(tab2.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab2.linkedBrowser, "about:robots");
  await promiseLoaded;
  Assert.ok(
    BrowserTestUtils.isHidden(footer.securityElement),
    "No security warning for local sites."
  );

  splitView.close();
});

add_task(async function test_menu_separate_tabs() {
  const { tabs, splitView } = await setupSplitView();
  const [tab1, tab2] = tabs;
  await SimpleTest.promiseFocus(tab1.linkedBrowser);
  const inactivePanel = document.getElementById(tab2.linkedPanel);

  info("Separate split view tabs.");
  await activateCommand(inactivePanel, "splitViewCmd_separateTabs");
  await BrowserTestUtils.waitForMutationCondition(
    document.getElementById("tabbrowser-tabs"),
    { childList: true, subtree: true },
    () => !splitView.isConnected
  );

  for (const tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_menu_reverse_tabs() {
  const { tabs, splitView } = await setupSplitView();
  await SimpleTest.promiseFocus(tabs[0].linkedBrowser);
  const [panel1, panel2] = tabs.map(({ linkedPanel }) =>
    document.getElementById(linkedPanel)
  );

  info("Reverse split view tabs.");
  await activateCommand(panel2, "splitViewCmd_reverseTabs");
  await BrowserTestUtils.waitForMutationCondition(
    panel1,
    { attributes: true },
    () => panel1.getAttribute("column") == 1
  );
  await BrowserTestUtils.waitForMutationCondition(
    panel2,
    { attributes: true },
    () => panel2.getAttribute("column") == 0
  );

  splitView.close();
});

add_task(async function test_menu_close_tabs() {
  const { tabs } = await setupSplitView();
  const [tab1, tab2] = tabs;
  await SimpleTest.promiseFocus(tab1.linkedBrowser);
  const inactivePanel = document.getElementById(tab2.linkedPanel);

  info("Close split view tabs.");
  const promiseTabsClosed = Promise.all(
    tabs.map(t => BrowserTestUtils.waitForTabClosing(t))
  );
  await activateCommand(inactivePanel, "splitViewCmd_closeTabs");
  await promiseTabsClosed;
});
