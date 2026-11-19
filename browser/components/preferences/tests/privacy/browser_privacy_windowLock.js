/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PREF = "browser.privatebrowsing.lockOnScreenLock.enabled";

async function openPane() {
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.contentDocument;
  let checkbox = doc.getElementById("windowLockEnabled");
  checkbox.scrollIntoView();
  await checkbox.updateComplete;
  return checkbox;
}

add_task(async function test_window_lock_setting() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  let checkbox = await openPane();

  ok(BrowserTestUtils.isVisible(checkbox), "The setting is visible");
  ok(!checkbox.checked, "The checkbox is unchecked");

  checkbox.click();
  await TestUtils.waitForCondition(
    () => Services.prefs.getBoolPref(PREF),
    "Enabling locks private windows"
  );

  checkbox.click();
  await TestUtils.waitForCondition(
    () => !Services.prefs.getBoolPref(PREF),
    "Disabling updates the pref"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});
