/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// A link to the Window Density section of about:preferences is shown instead of the density dropdown.
add_task(async function test_uidensity_link() {
  await startCustomizing();

  let button = document.getElementById("customization-uidensity-button");
  let link = document.getElementById("customization-uidensity-link");

  ok(button.hidden, "The density dropdown button is hidden");
  ok(!link.hidden, "The density link is visible");

  let waitForNewTab = BrowserTestUtils.waitForNewTab(gBrowser, url =>
    url.startsWith("about:preferences")
  );
  link.click();
  let prefsTab = await waitForNewTab;

  ok(
    gBrowser.currentURI.spec.startsWith("about:preferences#appearance"),
    "The link opened about:preferences#appearance"
  );
  BrowserTestUtils.removeTab(prefsTab);

  // Wait for customize mode to be re-entered now that the customize tab is
  // active. This is needed for endCustomizing() to work properly.
  await TestUtils.waitForCondition(() =>
    document.documentElement.hasAttribute("customizing")
  );
  await endCustomizing();
});

add_task(async function cleanup() {
  await endCustomizing();
});
