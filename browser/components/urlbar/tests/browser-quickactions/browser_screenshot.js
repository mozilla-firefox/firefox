/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * QuickActions tests that touch screenshot functionality.
 */

"use strict";

requestLongerTimeout(3);

const DUMMY_PAGE =
  "https://example.com/browser/browser/base/content/test/general/dummy_page.html";

async function isScreenshotInitialized() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    let screenshotsChild = content.windowGlobalChild.getActor(
      "ScreenshotsComponent"
    );
    return screenshotsChild?.overlay?.initialized;
  });
}

async function clickQuickActionOneoffButton() {
  const oneOffButton = await TestUtils.waitForCondition(() =>
    window.document.getElementById("urlbar-engine-one-off-item-actions")
  );
  Assert.ok(oneOffButton, "One off button is available when preffed on");

  EventUtils.synthesizeMouseAtCenter(oneOffButton, {}, window);
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.ACTIONS,
    entry: "oneoff",
  });
}

const assertAction = async name => {
  await BrowserTestUtils.waitForCondition(() =>
    window.document.querySelector(`.urlbarView-action-btn[data-action=${name}]`)
  );
  Assert.ok(true, `We found action "${name}`);
};

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.urlbar.secondaryActions.featureGate", true],
      ["browser.urlbar.shortcuts.quickactions", true],
    ],
  });
});

add_task(async function test_screenshot() {
  await SpecialPowers.pushPrefEnv({
    set: [["screenshots.browser.component.enabled", true]],
  });

  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, DUMMY_PAGE);
  await BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    DUMMY_PAGE
  );

  info("The action is matched when at end of input");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "screenshot",
  });
  await assertAction("screenshot");

  info("Trigger the screenshot mode");
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  await TestUtils.waitForCondition(
    isScreenshotInitialized,
    "Screenshot component is active",
    200,
    100
  );

  info("Press Escape to exit screenshot mode");
  EventUtils.synthesizeKey("KEY_Escape", {}, window);
  await TestUtils.waitForCondition(
    async () => !(await isScreenshotInitialized()),
    "Screenshot component has been dismissed"
  );

  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeKey("KEY_Escape");
  });
});
