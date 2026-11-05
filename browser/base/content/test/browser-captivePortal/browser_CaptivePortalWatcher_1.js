"use strict";

add_task(setupPrefsAndRecentWindowBehavior);

let testcases = [
  /**
   * A portal is detected when there's no browser window,
   * then a browser window is opened, and the portal is logged into
   * and redirects to a different page. The portal tab should be added
   * and focused when the window is opened, and left open after login
   * since it redirected.
   */
  async function test_detectedWithNoBrowserWindow_Redirect() {
    await portalDetected();
    let win = await focusWindowAndWaitForPortalUI();
    let browser = win.gBrowser.selectedTab.linkedBrowser;
    let loadPromise = BrowserTestUtils.browserLoaded(
      browser,
      false,
      CANONICAL_URL_REDIRECTED
    );
    BrowserTestUtils.startLoadingURIString(browser, CANONICAL_URL_REDIRECTED);
    await loadPromise;
    await freePortal(true);
    ensurePortalTab(win);
    ensureNoPortalNotification(win);
    await closeWindowAndWaitForWindowActivate(win);
  },

  /**
   * Test the various expected behaviors of the "Show Login Page" button
   * in the captive portal notification. The button should be visible for
   * all tabs except the captive portal tab, and when clicked, should
   * ensure a captive portal tab is open and select it.
   */
  async function test_showLoginPageButton() {
    let win = await openWindowAndWaitForFocus();
    await portalDetected();
    let notification = await ensurePortalNotification(win);
    await testShowLoginPageButtonVisibility(notification, "visible");

    async function testPortalTabSelectedAndButtonNotVisible() {
      is(
        win.gBrowser.selectedTab,
        tab,
        "The captive portal tab should be selected."
      );
      await testShowLoginPageButtonVisibility(notification, "hidden");
    }

    let button = notification.buttonContainer.querySelector(
      "button.notification-button"
    );
    async function clickButtonAndExpectNewPortalTab() {
      let p = BrowserTestUtils.waitForNewTab(win.gBrowser, CANONICAL_URL);
      button.click();
      let tab = await p;
      is(
        win.gBrowser.selectedTab,
        tab,
        "The captive portal tab should be selected."
      );
      return tab;
    }

    // Simulate clicking the button. The portal tab should be opened and
    // selected and the button should hide.
    let tab = await clickButtonAndExpectNewPortalTab();
    await testPortalTabSelectedAndButtonNotVisible();

    // Close the tab. The button should become visible.
    BrowserTestUtils.removeTab(tab);
    ensureNoPortalTab(win);
    await testShowLoginPageButtonVisibility(notification, "visible");

    // When the button is clicked, a new portal tab should be opened and
    // selected.
    tab = await clickButtonAndExpectNewPortalTab();

    // Open another arbitrary tab. The button should become visible. When it's clicked,
    // the portal tab should be selected.
    let anotherTab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
    await testShowLoginPageButtonVisibility(notification, "visible");
    button.click();
    is(
      win.gBrowser.selectedTab,
      tab,
      "The captive portal tab should be selected."
    );

    // Close the portal tab and select the arbitrary tab. The button should become
    // visible and when it's clicked, a new portal tab should be opened.
    BrowserTestUtils.removeTab(tab);
    win.gBrowser.selectedTab = anotherTab;
    await testShowLoginPageButtonVisibility(notification, "visible");
    tab = await clickButtonAndExpectNewPortalTab();

    BrowserTestUtils.removeTab(anotherTab);
    await freePortal(true);
    ensureNoPortalTab(win);
    ensureNoPortalNotification(win);
    await closeWindowAndWaitForWindowActivate(win);
  },
];

for (let testcase of testcases) {
  add_task(testcase);
}
