/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Basic UI tests for the protections panel */

"use strict";

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

ChromeUtils.defineESModuleGetters(this, {
  ContentBlockingAllowList:
    "resource://gre/modules/ContentBlockingAllowList.sys.mjs",
});

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

requestLongerTimeout(3);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Set the auto hide timing to 100ms for blocking the test less.
      ["browser.protections_panel.toast.timeout", 100],
      // Hide protections cards so as not to trigger more async messaging
      // when landing on the page.
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["privacy.trackingprotection.enabled", true],
      ["browser.urlbar.scotchBonnet.enableOverride", true],
    ],
  });
  await UrlClassifierTestUtils.addBuiltInTestTrackers();

  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;
  Services.telemetry.clearEvents();

  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
    Services.telemetry.canRecordExtended = oldCanRecord;
    Services.telemetry.clearEvents();
  });

  Services.fog.testResetFOG();
});

async function clickToggle(toggle) {
  let changed = BrowserTestUtils.waitForEvent(toggle, "toggle");
  EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {});
  await changed;
}

add_task(async function testToggleSwitch() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TRACKING_PAGE
  );

  await openProtectionsPanel();

  await TestUtils.waitForCondition(() => {
    return gProtectionsHandler._protectionsPopup.hasAttribute("blocking");
  });

  let buttonEvents =
    Glean.securityUiProtectionspopup.openProtectionsPopup.testGetValue();

  is(buttonEvents.length, 1, "recorded telemetry for opening the popup");

  let browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );

  await clickToggle(gProtectionsHandler._protectionsPopupTPSwitch);
  await popuphiddenPromise;

  is(
    Glean.securityUiProtectionspopup.clickEtpToggleOff.testGetValue().length,
    1,
    "recorded etp_toggle_off telemetry event"
  );

  // We need to wait toast's popup shown and popup hidden events. It won't fire
  // the popup shown event if we open the protections panel while the toast is
  // opening.
  let toastShown = waitForProtectionsPanelToast();

  await browserLoadedPromise;

  // Wait until the ETP state confirmation toast is shown and hides itself.
  await toastShown;

  // Re-open the protections panel and confirm that the toggle is off, then toggle it back on.
  await openProtectionsPanel();
  ok(
    !gProtectionsHandler._protectionsPopupTPSwitch.hasAttribute("pressed"),
    "TP Switch should be off"
  );

  browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );

  await clickToggle(gProtectionsHandler._protectionsPopupTPSwitch);

  // Wait for the protections panel to be hidden as the result of the ETP toggle
  // on action.
  await popuphiddenPromise;

  toastShown = waitForProtectionsPanelToast();

  await browserLoadedPromise;

  // Wait until the ETP state confirmation toast is shown and hides itself.
  await toastShown;

  is(
    Glean.securityUiProtectionspopup.clickEtpToggleOn.testGetValue().length,
    1,
    "recorded etp_toggle_on telemetry event"
  );

  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the protection settings button.
 */
add_task(async function testSettingsButton() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:preferences#privacy"
  );
  gProtectionsHandler._protectionsPopupSettingsButton.click();

  // The protection popup should be hidden after clicking settings button.
  await popuphiddenPromise;
  // Wait until the about:preferences has been opened correctly.
  let newTab = await newTabPromise;

  ok(true, "about:preferences has been opened successfully");
  is(
    Glean.securityUiProtectionspopup.clickSettings.testGetValue().length,
    1,
    "recorded settings telemetry event"
  );

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring Tracking Protection label is shown correctly
 */
add_task(async function testTrackingProtectionLabel() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let trackingProtectionLabel = document.getElementById(
    "protections-popup-footer-protection-type-label"
  );

  is(
    trackingProtectionLabel.textContent,
    "Custom",
    "The label is correctly set to Custom."
  );
  await closeProtectionsPanel();

  Services.prefs.setStringPref("browser.contentblocking.category", "standard");
  await openProtectionsPanel();

  is(
    trackingProtectionLabel.textContent,
    "Standard",
    "The label is correctly set to Standard."
  );
  await closeProtectionsPanel();

  Services.prefs.setStringPref("browser.contentblocking.category", "strict");
  await openProtectionsPanel();

  is(
    trackingProtectionLabel.textContent,
    "Strict",
    "The label is correctly set to Strict."
  );

  await closeProtectionsPanel();
  Services.prefs.setStringPref("browser.contentblocking.category", "custom");
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the 'Show Full Report' button in the footer section.
 */
add_task(async function testShowFullReportButton() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
  let newTabPromise = waitForAboutProtectionsTab();
  let showFullReportButton = document.getElementById(
    "protections-popup-show-report-button"
  );

  showFullReportButton.click();

  // The protection popup should be hidden after clicking the link.
  await popuphiddenPromise;
  // Wait until the 'about:protections' has been opened correctly.
  let newTab = await newTabPromise;

  ok(true, "about:protections has been opened successfully");

  is(
    Glean.securityUiProtectionspopup.clickFullReport.testGetValue().length,
    1,
    "recorded full_report telemetry event"
  );

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(tab);
});

function checkMiniPanel() {
  // Check that only the header is displayed.
  let mainView = document.getElementById("protections-popup-mainView");
  for (let item of mainView.childNodes) {
    if (item.id !== "protections-popup-mainView-panel-header-section") {
      ok(
        !BrowserTestUtils.isVisible(item),
        `The section '${item.id}' is hidden in the toast.`
      );
    } else {
      ok(
        BrowserTestUtils.isVisible(item),
        "The panel header is displayed as the content of the toast."
      );
    }
  }
}

/**
 * A test for ensuring the mini panel closes automatically
 */
add_task(async function testMiniPanel() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  // Open the mini panel.
  await openProtectionsPanel(true);
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );

  checkMiniPanel();

  // Wait until the auto hide is happening.
  await popuphiddenPromise;

  ok(true, "The mini panel hides automatically.");

  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring that clicking the mini panel opens the big panel
 */
add_task(async function testMiniPanelClick() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  // Open the mini panel.
  await openProtectionsPanel(true);
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );

  checkMiniPanel();

  let popupShownPromise = BrowserTestUtils.waitForEvent(
    window,
    "popupshown",
    true,
    e => e.target.id == "protections-popup"
  );

  // Simulate clicking on the mini panel text
  let buttonEl = document.getElementById(
    "protections-popup-toast-panel-tp-on-desc"
  );
  EventUtils.synthesizeMouseAtCenter(buttonEl, {});

  info("Waiting for mini panel to close");
  await popuphiddenPromise;

  info("Waiting for big popup to be shown");
  await popupShownPromise;

  let header = document.getElementById(
    "protections-popup-mainView-panel-header-section"
  );
  ok(BrowserTestUtils.isVisible(header), "Header is visible");

  let body = document.getElementById("protections-popup-main-body");
  ok(BrowserTestUtils.isVisible(body), "Main body is visible");

  let footer = document.getElementById("protections-popup-footer");
  ok(BrowserTestUtils.isVisible(footer), "Footer is visible");

  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for the toggle switch flow
 */
add_task(async function testToggleSwitchFlow() {
  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popupshown"
  );
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  // Click the TP switch, from On -> Off.
  await clickToggle(gProtectionsHandler._protectionsPopupTPSwitch);

  // Check that the icon state has been changed.
  ok(
    gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "The tracking protection icon state has been changed to disabled."
  );

  // The panel should be closed and the mini panel will show up after refresh.
  await popuphiddenPromise;
  await browserLoadedPromise;
  await popupShownPromise;

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The protections popup should have the 'toast' attribute."
  );

  // Click on the mini panel and making sure the protection popup shows up.
  popupShownPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popupshown"
  );
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );

  // Simulate clicking on the mini panel text
  let buttonEl = document.getElementById(
    "protections-popup-toast-panel-tp-off-desc"
  );
  EventUtils.synthesizeMouseAtCenter(buttonEl, {});
  await popuphiddenPromise;
  await popupShownPromise;

  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The 'toast' attribute should be cleared on the protections popup."
  );

  // Click the TP switch again, from Off -> On.
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
  popupShownPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popupshown"
  );
  browserLoadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await clickToggle(gProtectionsHandler._protectionsPopupTPSwitch);

  // Check that the icon state has been changed.
  ok(
    !gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "The tracking protection icon state has been changed to enabled."
  );

  // Protections popup hidden -> Page refresh -> Mini panel shows up.
  await popuphiddenPromise;
  popuphiddenPromise = BrowserTestUtils.waitForEvent(
    gProtectionsHandler._protectionsPopup,
    "popuphidden"
  );
  await browserLoadedPromise;
  await popupShownPromise;

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("toast"),
    "The protections popup should have the 'toast' attribute."
  );

  // Wait until the auto hide is happening.
  await popuphiddenPromise;

  // Clean up the TP state.
  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring the tracking protection icon will show a correct
 * icon according to the TP enabling state.
 */
add_task(async function testTrackingProtectionIcon() {
  // Open a tab and its protection panel.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  let TPIcon = document.getElementById("tracking-protection-icon");
  // Check the icon url. It will show a shield icon if TP is enabled.
  is(
    gBrowser.documentGlobal
      .getComputedStyle(TPIcon)
      .getPropertyValue("list-style-image"),
    `url("chrome://browser/skin/tracking-protection.svg")`,
    "The tracking protection icon shows a shield icon."
  );

  // Disable the tracking protection.
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://example.com/"
  );
  gProtectionsHandler.disableForCurrentPage();
  await browserLoadedPromise;

  // Check that the tracking protection icon should show a strike-through shield
  // icon after page is reloaded.
  is(
    gBrowser.documentGlobal
      .getComputedStyle(TPIcon)
      .getPropertyValue("list-style-image"),
    `url("chrome://browser/skin/tracking-protection-disabled.svg")`,
    "The tracking protection icon shows a strike through shield icon."
  );

  // Clean up the TP state.
  ContentBlockingAllowList.remove(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test for ensuring the number of blocked trackers is displayed properly.
 */
add_task(async function testNumberOfBlockedTrackers() {
  // First, clear the tracking database.
  await TrackingDBService.clearAll();

  // Open a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  await openProtectionsPanel();

  let trackerCounterBox = document.getElementById(
    "protections-popup-trackers-blocked-counter-box"
  );
  let trackerCounterDesc = document.getElementById(
    "protections-popup-trackers-blocked-counter-description"
  );

  // Check that whether the counter is not shown if the number of blocked
  // trackers is zero.
  ok(
    BrowserTestUtils.isHidden(trackerCounterBox),
    "The blocked tracker counter is hidden if there is no blocked tracker."
  );

  await closeProtectionsPanel();

  // Add one tracker into the database and check that the tracker counter is
  // properly shown.
  await addTrackerDataIntoDB(1);

  // A promise for waiting the `showing` attributes has been set to the counter
  // box. This means the database access is finished.
  let counterShownPromise = BrowserTestUtils.waitForAttribute(
    "showing",
    trackerCounterBox
  );

  await openProtectionsPanel();
  await counterShownPromise;

  // Check that the number of blocked trackers is shown.
  ok(
    BrowserTestUtils.isVisible(trackerCounterBox),
    "The blocked tracker counter is shown if there is one blocked tracker."
  );
  is(
    trackerCounterDesc.textContent,
    "1 Blocked",
    "The blocked tracker counter is correct."
  );

  await closeProtectionsPanel();
  await TrackingDBService.clearAll();

  // Add trackers into the database and check that the tracker counter is
  // properly shown as well as whether the pre-fetch is triggered by the
  // keyboard navigation.
  await addTrackerDataIntoDB(10);

  // We cannot wait for the change of "showing" attribute here since this
  // attribute will only be set if the previous counter is zero. Instead, we
  // wait for the change of the text content of the counter.
  let updateCounterPromise = new Promise(resolve => {
    let mut = new MutationObserver(() => {
      resolve();
      mut.disconnect();
    });

    mut.observe(trackerCounterDesc, {
      childList: true,
    });
  });

  await openProtectionsPanelWithKeyNav();
  await updateCounterPromise;

  // Check that the number of blocked trackers is shown.
  ok(
    BrowserTestUtils.isVisible(trackerCounterBox),
    "The blocked tracker counter is shown if there are more than one blocked tracker."
  );
  is(
    trackerCounterDesc.textContent,
    "10 Blocked",
    "The blocked tracker counter is correct."
  );

  await closeProtectionsPanel();
  await TrackingDBService.clearAll();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testSubViewTelemetry() {
  let items = [
    [
      "protections-popup-category-trackers",
      Glean.securityUiProtectionspopup.clickTrackers,
      "trackers",
    ],
    [
      "protections-popup-category-socialblock",
      Glean.securityUiProtectionspopup.clickSocial,
      "social",
    ],
    [
      "protections-popup-category-cookies",
      Glean.securityUiProtectionspopup.clickCookies,
      "cookies",
    ],
    [
      "protections-popup-category-cryptominers",
      Glean.securityUiProtectionspopup.clickCryptominers,
      "cryptominers",
    ],
    [
      "protections-popup-category-fingerprinters",
      Glean.securityUiProtectionspopup.clickFingerprinters,
      "fingerprinters",
    ],
  ].map(item => [document.getElementById(item[0]), item[1], item[2]]);

  for (let [item, telemetryMetric, telemetryValue] of items) {
    // eslint-disable-next-line sdl/no-insecure-url
    await BrowserTestUtils.withNewTab("http://www.example.com", async () => {
      await Services.fog.testFlushAllChildren();
      Services.fog.testResetFOG();

      await openProtectionsPanel();

      item.classList.remove("notFound"); // Force visible for test
      gProtectionsHandler._categoryItemOrderInvalidated = true;
      gProtectionsHandler.reorderCategoryItems();

      let viewShownEvent = BrowserTestUtils.waitForEvent(
        gProtectionsHandler._protectionsPopupMultiView,
        "ViewShown"
      );
      item.click();
      let panelView = (await viewShownEvent).originalTarget;
      is(
        telemetryMetric.testGetValue().length,
        1,
        `recorded ${item.id} telemetry event`
      );
      let prefsTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:preferences#privacy"
      );
      panelView.querySelector(".panel-subview-footer-button").click();
      let prefsTab = await prefsTabPromise;
      BrowserTestUtils.removeTab(prefsTab);
      let subviewEvents =
        Glean.securityUiProtectionspopup.clickSubviewSettings.testGetValue();
      is(subviewEvents.length, 1, "recorded subview_settings telemetry event");
      is(
        subviewEvents[0].extra.value,
        telemetryValue,
        "subview_settings event has the expected value"
      );
    });
  }
});

/**
 * A test to make sure the TP state won't apply incorrectly if we quickly switch
 * tab after toggling the TP switch.
 */
add_task(async function testQuickSwitchTabAfterTogglingTPSwitch() {
  const FIRST_TEST_SITE = "https://example.com/";
  const SECOND_TEST_SITE = "https://example.org/";

  // First, clear the tracking database.
  await TrackingDBService.clearAll();

  // Open two tabs with different origins.
  let tabOne = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    FIRST_TEST_SITE
  );
  let tabTwo = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    SECOND_TEST_SITE
  );

  // Open the protection panel of the second tab.
  await openProtectionsPanel();

  // A promise to check the reload happens on the second tab.
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tabTwo.linkedBrowser,
    false,
    SECOND_TEST_SITE
  );

  // Toggle the TP state and switch tab without waiting it to be finished.
  await clickToggle(gProtectionsHandler._protectionsPopupTPSwitch);
  gBrowser.selectedTab = tabOne;

  // Wait for the second tab to be reloaded.
  await browserLoadedPromise;

  // Check that the first tab is still with ETP enabled.
  ok(
    !ContentBlockingAllowList.includes(gBrowser.selectedBrowser),
    "The ETP state of the first tab is still enabled."
  );

  // Check the ETP is disabled on the second origin.
  ok(
    ContentBlockingAllowList.includes(tabTwo.linkedBrowser),
    "The ETP state of the second tab has been changed to disabled."
  );

  // Clean up the state of the allow list for the second tab.
  ContentBlockingAllowList.remove(tabTwo.linkedBrowser);

  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);

  // Finally, clear the tracking database.
  await TrackingDBService.clearAll();
});
