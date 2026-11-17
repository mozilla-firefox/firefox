/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionWidget:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
});

/**
 * Tests that setting the optedOut pref removes the toolbar button
 */
add_task(async function test_opt_out() {
  let buttonBefore = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  Assert.ok(
    BrowserTestUtils.isVisible(buttonBefore),
    "ipprotection toolbar button should be present"
  );

  Services.prefs.setBoolPref("browser.ipProtection.optedOut", true);

  let buttonAfter = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  Assert.ok(!buttonAfter, "ipprotection toolbar button should not be present");

  Services.prefs.clearUserPref("browser.ipProtection.optedOut");
});

/**
 * Tests that adding the widget back to the toolbar clears the optedOut pref
 */
add_task(async function test_opt_in_when_widget_added() {
  Services.prefs.setBoolPref("browser.ipProtection.optedOut", true);

  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(lazy.IPProtectionWidget.WIDGET_ID),
    "ipprotection widget should not be placed"
  );

  CustomizableUI.addWidgetToArea(
    lazy.IPProtectionWidget.WIDGET_ID,
    CustomizableUI.AREA_NAVBAR
  );

  Assert.ok(
    !Services.prefs.getBoolPref("browser.ipProtection.optedOut"),
    "optedOut pref should be cleared after adding the widget"
  );

  Services.prefs.clearUserPref("browser.ipProtection.optedOut");
});

/**
 * Tests that restoring the toolbar defaults clears the optedOut pref
 */
add_task(async function test_opt_in_when_toolbar_reset() {
  Services.prefs.setBoolPref("browser.ipProtection.optedOut", true);

  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(lazy.IPProtectionWidget.WIDGET_ID),
    "ipprotection widget should not be placed"
  );

  CustomizableUI.reset();

  Assert.ok(
    CustomizableUI.getPlacementOfWidget(lazy.IPProtectionWidget.WIDGET_ID),
    "ipprotection widget should be placed after reset"
  );
  Assert.ok(
    !Services.prefs.getBoolPref("browser.ipProtection.optedOut"),
    "optedOut pref should be cleared after restoring defaults"
  );

  Services.prefs.clearUserPref("browser.ipProtection.optedOut");
});
