/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// test selects and options
addAccessibleTask(
  `<select id="select">
    <option id="o1">hello</option>
    <option id="o2">world</option>
  </select>`,
  async function (browser, accDoc) {
    const select = findAccessibleChildByID(accDoc, "select");
    ok(
      isAccessible(select.firstChild, [nsIAccessibleSelectable]),
      "No selectable accessible for combobox"
    );
    await untilCacheOk(
      () => testVisibility(select, false, false),
      "select should be on screen and visible"
    );

    if (!browser.isRemoteBrowser) {
      await untilCacheOk(
        () => testVisibility(select.firstChild, false, true),
        "combobox list should be on screen and invisible"
      );
    } else {
      // XXX: When the cache is used, states::INVISIBLE is
      // incorrect. Test OFFSCREEN anyway.
      await untilCacheOk(() => {
        const [states] = getStates(select.firstChild);
        return (states & STATE_OFFSCREEN) == 0;
      }, "combobox list should be on screen");
    }

    const o1 = findAccessibleChildByID(accDoc, "o1");
    const o2 = findAccessibleChildByID(accDoc, "o2");

    await untilCacheOk(
      () => testVisibility(o1, false, false),
      "option one should be on screen and visible"
    );
    await untilCacheOk(
      () => testVisibility(o2, true, false),
      "option two should be off screen and visible"
    );

    // Select the second option (drop-down collapsed).
    const p = waitForEvents({
      expected: [
        [EVENT_SELECTION, "o2"],
        [EVENT_TEXT_VALUE_CHANGE, "select"],
      ],
      unexpected: [
        stateChangeEventArgs("o2", EXT_STATE_ACTIVE, true, true),
        stateChangeEventArgs("o1", EXT_STATE_ACTIVE, false, true),
      ],
    });
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("select").selectedIndex = 1;
    });
    await p;

    await untilCacheOk(() => {
      const [states] = getStates(o1);
      return (states & STATE_OFFSCREEN) != 0;
    }, "option 1 should be off screen");
    await untilCacheOk(() => {
      const [states] = getStates(o2);
      return (states & STATE_OFFSCREEN) == 0;
    }, "option 2 should be on screen");
  },
  { chrome: true, iframe: true, remoteIframe: true }
);
