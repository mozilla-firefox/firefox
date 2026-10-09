/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const TEST_URL = `${TEST_BASE_URL}has-a-link.html`;

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      // Tests get no default Top Sites, and the view is opened on an empty
      // string here, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async function test_button_visibility_by_escape() {
  info("Open pageproxystate valid page");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  await assertState(false, "valid");

  info("Open the result panel and select the first item");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  EventUtils.synthesizeKey("KEY_ArrowDown");
  await assertState(true, "invalid");

  info("Escape to close the result panel");
  await UrlbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeKey("KEY_Escape")
  );
  await assertState(true, "invalid");

  info("Escape to revert");
  EventUtils.synthesizeKey("KEY_Escape");
  await assertState(false, "valid");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_button_visibility_by_loaded_on_background() {
  info("Open valid page that loads slow");
  let tab1 = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening:
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        "https://www.example.com"
      ) + "slow-page.sjs",
    waitForLoad: false,
  });
  await assertState(true, "invalid");

  info("Open a new tab");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  await assertState(true, "invalid");

  info("Wait until loading the slow page on background");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(r => setTimeout(r, 5000));
  await TestUtils.waitForCondition(
    () =>
      !gURLBar.getBrowserState(tab1.linkedBrowser)
        .isUnifiedSearchButtonAvailable
  );

  info("Select the previous tab");
  gBrowser.selectedTab = tab1;
  await assertState(false, "valid");

  info("Clean up");
  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab1);
});

add_task(async function test_button_visibility_by_pageproxystate() {
  info("Open pageproxystate valid page");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  await assertState(false, "valid");

  info("Click on browser element");
  await clickOnBrowserElement();
  await assertState(false, "valid");

  info("Click on urlbar");
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  await assertState(false, "valid");

  info("Start to edit");
  EventUtils.synthesizeKey("a");
  await assertState(true, "invalid");

  info("Click on browser element");
  await clickOnBrowserElement();
  await assertState(true, "invalid");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_button_visibility_by_tab_switching() {
  info("Open pageproxystate valid page");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  await assertState(false, "valid");

  info("Focus on Unified Search Mode by key");
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  await assertState(true, "valid");

  info("Open a new tab and select it");
  let newtab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/newtab"
  );
  await assertState(false, "valid");

  info("Select the previous tab that enables Unified Search Button");
  gBrowser.selectedTab = tab;
  await assertState(true, "valid");

  info("Clean up");
  BrowserTestUtils.removeTab(newtab);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_button_visibility_by_navigate_blank_page() {
  const BLANK_PAGE_URLS = [
    "about:blank",
    "about:home",
    "about:newtab",
    "about:privatebrowsing",
  ];

  for (let isPrivateMode of [false, true]) {
    for (let url of BLANK_PAGE_URLS) {
      info(`Test for ${url} in private mode: ${isPrivateMode}`);

      info("Open pageproxystate valid page");
      let tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "https://example.com/"
      );
      await assertState(false, "valid");

      info("Navigate to blank page");
      let onLocationChange = BrowserTestUtils.waitForLocationChange(
        gBrowser,
        url
      );
      gURLBar.value = url;
      gURLBar.focus();
      EventUtils.synthesizeKey("KEY_Enter");
      await onLocationChange;
      await assertState(true, "invalid");
      BrowserTestUtils.removeTab(tab);
    }
  }
});

async function assertState(expectedVisible, expectedProxyPageState) {
  let switcher = gURLBar.querySelector(".searchmode-switcher");
  await BrowserTestUtils.waitForCondition(() => {
    // If Unified Search Button is displayed as off-screen, the position should
    // be 'fixed'.
    let isVisible = window.getComputedStyle(switcher).position != "fixed";
    return isVisible == expectedVisible;
  }, `Wait until Unified Search Button visibility will be changed to ${expectedVisible}`);
  Assert.ok(true, "Unified Search Button visibility is correct");
  Assert.equal(gURLBar.getAttribute("pageproxystate"), expectedProxyPageState);
  Assert.equal(
    switcher.getAttribute("aria-hidden"),
    expectedVisible ? null : "true",
    "aria-hidden should be set when the switcher is hidden so screen readers do not announce"
  );
}

async function clickOnBrowserElement() {
  // We intentionally turn off this a11y check, because the following click is
  // purposefully targeting a non-interactive element.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  EventUtils.synthesizeMouseAtCenter(document.getElementById("browser"), {});
  AccessibilityUtils.resetEnv();
  await BrowserTestUtils.waitForCondition(() =>
    document.activeElement.closest("#browser")
  );
}
