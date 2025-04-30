/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
"use strict";

const TEST_URI =
  "https://example.com/browser/dom/geolocation/test/browser/position.html";

add_task(async function testDismissHanger() {
  info(
    "Check that location is not shared when dismissing the geolocation hanger"
  );

  let promisePanelShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown",
    true
  );
  await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URI);
  await promisePanelShown;

  // We intentionally turn off this a11y check, because the following click
  // is sent on the <toolbar> to dismiss the Geolocation hanger using an
  // alternative way of the popup dismissal, while the other way like `Esc` key
  // is available for assistive technology users, thus this test can be ignored.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  // click outside the Geolocation hanger to dismiss it
  window.document.getElementById("nav-bar").click();
  AccessibilityUtils.resetEnv();
  info("Clicked outside the Geolocation panel to dismiss it");

  let hasLocation = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async function () {
      return content.document.body.innerHTML.includes("location...");
    }
  );

  ok(hasLocation, "Location is not shared");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
