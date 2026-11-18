/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_task(async function test_open_link_item_has_beta_badge() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    let a = content.document.createElement("a");
    a.id = "link";
    a.href = "https://example.org/";
    a.textContent = "link";
    content.document.body.prepend(a);
  });

  let menu = document.getElementById("contentAreaContextMenu");
  let shown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    { type: "contextmenu" },
    tab.linkedBrowser
  );
  await shown;

  let item = document.getElementById("context-openlinkminiwindow");
  Assert.ok(!item.hidden, "Open Link in Mini Window is shown");
  let expected = await document.l10n.formatValue(
    "main-context-menu-beta-feature-badge"
  );
  await TestUtils.waitForCondition(
    () => item.getAttribute("badge") == expected,
    "Waiting for the beta badge"
  );
  Assert.equal(item.getAttribute("badge"), expected, "Item has a beta badge");

  let hidden = BrowserTestUtils.waitForEvent(menu, "popuphidden");
  menu.hidePopup();
  await hidden;
  BrowserTestUtils.removeTab(tab);
});
