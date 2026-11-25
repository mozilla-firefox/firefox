/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

async function setDisplayAndWaitForReorder(browser, value) {
  let onReorder = waitForEvent(EVENT_REORDER, "ul");
  await invokeSetStyle(browser, "li", "display", value);
  return onReorder;
}

addAccessibleTask(
  `
  <ul id="ul">
    <li id="li">item1</li>
  </ul>`,
  async function (browser, accDoc) {
    let li = findAccessibleChildByID(accDoc, "li");
    let bullet = li.firstChild;
    let accTree = {
      role: ROLE_LISTITEM,
      children: [
        {
          role: ROLE_LISTITEM_MARKER,
          children: [],
        },
        {
          role: ROLE_TEXT_LEAF,
          children: [],
        },
      ],
    };
    testAccessibleTree(li, accTree);

    await setDisplayAndWaitForReorder(browser, "none");

    ok(isDefunct(li), "Check that li is defunct.");
    ok(isDefunct(bullet), "Check that bullet is defunct.");

    let event = await setDisplayAndWaitForReorder(browser, "list-item");

    testAccessibleTree(
      findAccessibleChildByID(event.accessible, "li"),
      accTree
    );
  },
  { iframe: true, remoteIframe: true }
);

/**
 * Test removing content from a list item while style is dirty. Computing the
 * text offset of the removed content requires the text of the bullet.
 */
addAccessibleTask(
  `
  <style>
    @counter-style a { system: cyclic; symbols: "a"; speak-as: b; }
  </style>
  <style id="b">
    @counter-style b { system: cyclic; symbols: "b"; }
  </style>
  <style id="dirty"></style>
  <ul>
    <li id="li" style="list-style-type: a;"><marquee id="marquee"></marquee></li>
  </ul>
  `,
  async function testRemoveWithDirtyStyle(browser) {
    // Replace counter style b. This evicts it from the counter style cache
    // and resets the cached speak-as of counter style a, which refers to b.
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("b").textContent =
        `@counter-style b { system: cyclic; symbols: "c"; }`;
      content.document.body.offsetTop;
    });
    const reordered = waitForEvent(EVENT_REORDER, "li");
    await invokeContentTask(browser, [], () => {
      // Make the stylist dirty without flushing. Counter style b is no longer
      // cached, so if accessibility incorrectly tried to get the bullet's
      // spoken text before the next flush, the lookup would miss the cache
      // while the stylist is dirty, failing an assertion.
      content.document.getElementById("dirty").remove();
      content.document.getElementById("marquee").remove();
    });
    const li = (await reordered).accessible;
    testAccessibleTree(li, {
      role: ROLE_LISTITEM,
      children: [{ role: ROLE_LISTITEM_MARKER, children: [] }],
    });
  },
  { chrome: true, topLevel: true, iframe: true, remoteIframe: true }
);

/**
 * Test recreating a list item while style is dirty and rendering is
 * suppressed. The refresh driver doesn't flush style for a document where
 * rendering is suppressed, so accessibility shouldn't create the bullet, which
 * requires the bullet's text from layout, until rendering is no longer
 * suppressed.
 */
addAccessibleTask(
  `
  <style>
    @counter-style a { system: cyclic; symbols: "a"; speak-as: b; }
  </style>
  <style id="b">
    @counter-style b { system: cyclic; symbols: "b"; }
  </style>
  <style id="dirty"></style>
  <ul id="ul">
    <li id="li" style="list-style-type: a;">item</li>
  </ul>
  `,
  async function testRecreateWithRenderingSuppressed(browser) {
    // Replace counter style b. This evicts it from the counter style cache
    // and resets the cached speak-as of counter style a, which refers to b.
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("b").textContent =
        `@counter-style b { system: cyclic; symbols: "c"; }`;
      content.document.body.offsetTop;
    });
    const reordered = waitForEvent(EVENT_REORDER, "ul");
    await invokeContentTask(browser, [], () => {
      const utils = content.windowUtils;
      // Suppressing event handling suppresses rendering.
      utils.suppressEventHandling(true);
      // Make the stylist dirty without flushing. Counter style b is no longer
      // cached, so if accessibility incorrectly tried to get the bullet's
      // spoken text before the next flush, the lookup would miss the cache
      // while the stylist is dirty, failing an assertion.
      content.document.getElementById("dirty").remove();
      // Changing the role recreates the list item and its bullet.
      content.document.getElementById("li").setAttribute("role", "listitem");
      // Tick the refresh driver while rendering is suppressed.
      utils.advanceTimeAndRefresh(0);
      utils.restoreNormalRefresh();
      utils.suppressEventHandling(false);
    });
    const ul = (await reordered).accessible;
    testAccessibleTree(ul, {
      role: ROLE_LIST,
      children: [
        {
          role: ROLE_LISTITEM,
          children: [
            { role: ROLE_LISTITEM_MARKER, children: [] },
            { role: ROLE_TEXT_LEAF, children: [] },
          ],
        },
      ],
    });
  },
  { topLevel: true }
);
