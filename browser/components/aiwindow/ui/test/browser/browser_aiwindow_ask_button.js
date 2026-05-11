/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);
// AI chat content loads Fluent strings asynchronously, which may not complete
// before the test finishes. This is expected and doesn't affect test behavior.
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Missing message.*smartwindow-messages-document-title/
);

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.urlbar.suggest.searches", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.firstrun.hasCompleted", true],
    ],
  });
});

async function testImmersiveView(isVerticalTabs) {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", isVerticalTabs]],
  });
  const win = await openAIWindow();
  const chromeRoot = win.document.documentElement;

  Assert.ok(
    chromeRoot.hasAttribute("aiwindow-immersive-view"),
    "Chrome window has the aiwindow-immersive-view attribute"
  );

  const askButton = win.document.getElementById("smartwindow-ask-button-inner");
  Assert.ok(askButton, "Ask button exists in the toolbar");
  Assert.ok(
    BrowserTestUtils.isHidden(askButton),
    "Ask button is not visible for a new tab (immersive view) in Smart Window"
  );
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
}

/**
 * Test if the toolbar ask button exists and toggles the assistant sidebar
 */
add_task(async function test_ask_button() {
  let win;
  try {
    win = await openAIWindow();
    const exampleUrl = "https://example.com/";
    const browser = win.gBrowser.selectedTab.linkedBrowser;
    const loaded = BrowserTestUtils.browserLoaded(browser, false, exampleUrl);
    BrowserTestUtils.startLoadingURIString(browser, exampleUrl);
    await loaded;

    Assert.equal(
      browser.currentURI.spec,
      exampleUrl,
      "Example url tab should be open"
    );

    const askButton = win.document.getElementById(
      "smartwindow-ask-button-inner"
    );
    Assert.ok(askButton, "Ask button exists in the toolbar");
    Assert.ok(
      BrowserTestUtils.isVisible(askButton),
      "Ask button is initially visible for AI Window"
    );

    // Navigation away from aiWindow.html may auto-open the sidebar via
    // AIWindowTabStatesManager, wait for it to settle then ensure closed.
    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Wait for sidebar to auto-open after navigation"
    );
    AIWindowUI.closeSidebar(win);
    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Wait for sidebar to close"
    );

    /* the window switcher feature callout gets in front of the ask button
    if it exists, we must close before clicking on the ask button */
    const switcherFeatureCallout = win.document.querySelector(
      "#feature-callout .SMARTWINDOW_SWITCHER_BUTTON_CALLOUT"
    );

    if (switcherFeatureCallout) {
      const closeBtn = switcherFeatureCallout.querySelector(".dismiss-button");
      EventUtils.synthesizeMouseAtCenter(closeBtn, {}, win);
    }

    EventUtils.synthesizeMouseAtCenter(askButton, {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "true"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "true",
      "Ask button has aria-expanded=true after click"
    );

    const sidebar = win.document.getElementById("ai-window-box");
    if (sidebar) {
      Assert.ok(!sidebar.collapsed, "AI Sidebar exists and is not hidden");
    }
    EventUtils.synthesizeMouseAtCenter(askButton, {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "false"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "false",
      "Ask button has aria-expanded=false after second click"
    );
    Assert.ok(sidebar.collapsed, "AI Sidebar is hidden after second click");

    askButton.setAttribute("tabindex", "-1");
    askButton.focus();
    Services.focus.setFocus(askButton, Services.focus.FLAG_BYKEY);
    EventUtils.synthesizeKey("KEY_Enter", {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "true"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "true",
      "Ask button has aria-expanded=true after tab enter"
    );
    Assert.ok(!sidebar.hidden, "AI Sidebar is not hidden after tab enter");

    EventUtils.synthesizeKey("KEY_Enter", {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "false"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "false",
      "Ask button has aria-expanded=false after second tab enter"
    );
    Assert.ok(sidebar.collapsed, "AI Sidebar is hidden after second tab enter");

    EventUtils.synthesizeKey(" ", {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "true"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "true",
      "Ask button has aria-expanded=true after space"
    );
    Assert.ok(!sidebar.hidden, "AI Sidebar is not hidden after space");

    EventUtils.synthesizeKey(" ", {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      askButton,
      { attributes: true, attributeFilter: ["aria-expanded"] },
      () => askButton.getAttribute("aria-expanded") === "false"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "false",
      "Ask button has aria-expanded=false after second space"
    );
    Assert.ok(sidebar.collapsed, "AI Sidebar is hidden after second space");
    askButton.removeAttribute("tabindex");
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Test if the toolbar ask button is not visible in Classic Window.
 */
add_task(async function test_classic_window() {
  let win;
  try {
    win = await BrowserTestUtils.openNewBrowserWindow({
      openerWindow: null,
    });
  } catch (e) {
    win = await BrowserTestUtils.openNewBrowserWindow();
    win.document.documentElement.setAttribute(
      "windowtype",
      "classicwindow-test"
    );
  }

  try {
    const askButton = win.document.getElementById(
      "smartwindow-ask-button-inner"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(askButton),
      "Ask button is not visible in the toolbar for classic window"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Test if the toolbar ask button is not visible in immersive view (new tab)
 */
add_task(async function test_ask_button_immersive_view() {
  await testImmersiveView(false);
});

/**
 * Test if the toolbar ask button is not visible in immersive view on vertical tabs (new tab)
 */
add_task(async function test_ask_button_immersive_view_vertical_tabs() {
  await testImmersiveView(true);
});

/**
 * Test if the close button inside the sidebar chat closes the sidebar
 */
add_task(async function test_sidebar_close_button() {
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });
  let win;
  try {
    win = await openAIWindow();
    const exampleUrl = "https://example.com/";
    const browser = win.gBrowser.selectedTab.linkedBrowser;
    const loaded = BrowserTestUtils.browserLoaded(browser, false, exampleUrl);
    BrowserTestUtils.startLoadingURIString(browser, exampleUrl);
    await loaded;

    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Wait for sidebar to auto-open after navigation"
    );

    const askButton = win.document.getElementById(
      "smartwindow-ask-button-inner"
    );
    const sidebar = win.document.getElementById("ai-window-box");

    Assert.ok(!sidebar.collapsed, "Sidebar is open");

    const aiBrowser = win.document.getElementById("ai-window-browser");

    await TestUtils.waitForCondition(
      () => aiBrowser.contentDocument.querySelector("ai-window")?.shadowRoot,
      "Wait for ai-window shadow root to be ready"
    );

    const aiWindow = aiBrowser.contentDocument.querySelector("ai-window");
    const closeButton = aiWindow.shadowRoot.querySelector(
      ".close-sidebar-button"
    );
    Assert.ok(closeButton, "Close button exists in the sidebar header");

    closeButton.click();

    await TestUtils.waitForCondition(
      () => !AIWindowUI.isSidebarOpen(win),
      "Wait for sidebar to close after clicking close button"
    );

    Assert.ok(
      sidebar.collapsed,
      "Sidebar is closed after clicking close button"
    );
    Assert.ok(
      !askButton.hasAttribute("checked"),
      "Ask button is unchecked after sidebar close"
    );
    Assert.equal(
      askButton.getAttribute("aria-expanded"),
      "false",
      "Ask button has aria-expanded=false after sidebar close"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await restore();
  }
});
