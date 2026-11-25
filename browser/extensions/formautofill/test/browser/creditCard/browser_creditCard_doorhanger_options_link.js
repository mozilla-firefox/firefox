/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [AUTOFILL_CREDITCARDS_AVAILABLE_PREF, "on"],
      [ENABLED_AUTOFILL_CREDITCARDS_PREF, true],
    ],
  });
});

async function triggerCreditCardCaptureDoorhanger(browser) {
  const onPopupShown = waitForPopupShown();
  await focusUpdateSubmitForm(browser, {
    focusSelector: "#cc-name",
    newValues: {
      "#cc-name": "User 1",
      "#cc-number": "5038146897157463",
      "#cc-exp-month": "12",
      "#cc-exp-year": "2017",
      "#cc-type": "mastercard",
    },
  });
  await onPopupShown;
}

function getOptionsButton() {
  const button = getNotification().querySelector(
    ".payments-doorhanger-options-button"
  );
  ok(button, "Found the options button in the doorhanger");
  return button;
}

function isSettingsURL(url) {
  return (
    url.startsWith("about:preferences") || url.startsWith("about:settings")
  );
}

function countSettingsTabs() {
  return gBrowser.tabs.filter(tab =>
    isSettingsURL(tab.linkedBrowser?.currentURI?.spec ?? "")
  ).length;
}

// Clicking the options button in the credit card capture doorhanger
// should focus an already-open Settings tab rather than opening a new one each
// time (Bug 2044285).
add_task(async function test_options_link_reuses_settings_tab() {
  const formTabs = [];

  // The first click has no Settings tab to reuse, so it opens one.
  formTabs.push(
    await BrowserTestUtils.openNewForegroundTab(gBrowser, CREDITCARD_FORM_URL)
  );
  await triggerCreditCardCaptureDoorhanger(gBrowser.selectedBrowser);

  const settingsTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    isSettingsURL,
    true
  );
  getOptionsButton().click();
  const settingsTab = await settingsTabPromise;

  is(
    countSettingsTabs(),
    1,
    "A single Settings tab is open after the first click"
  );

  // A second click from a fresh doorhanger should focus the existing Settings
  // tab instead of opening another one. Reuse switches the selected tab back to
  // the original Settings tab; the buggy behavior would instead select a newly
  // opened tab, so this condition never becomes true and the test fails.
  formTabs.push(
    await BrowserTestUtils.openNewForegroundTab(gBrowser, CREDITCARD_FORM_URL)
  );
  await triggerCreditCardCaptureDoorhanger(gBrowser.selectedBrowser);
  getOptionsButton().click();
  await TestUtils.waitForCondition(
    () => gBrowser.selectedTab == settingsTab,
    "Clicking the options button again focuses the existing Settings tab"
  );

  is(
    countSettingsTabs(),
    1,
    "Only one Settings tab exists after the second click (reused, not duplicated)"
  );

  for (const tab of [settingsTab, ...formTabs]) {
    BrowserTestUtils.removeTab(tab);
  }
  await removeAllRecords();
});
