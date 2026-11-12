/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Test repeatedly reframing an in-process iframe. Each reframe shuts down the
 * iframe's DocAccessible and creates a new one for the same document while the
 * PDocAccessible actors for the previous DocAccessibles are still awaiting
 * deletion by the parent process.
 */
addAccessibleTask(
  `<iframe id="iframe" srcdoc="<p id='p'>p</p>"></iframe>`,
  async function testReframe(browser, docAcc) {
    await untilCacheOk(
      () => findAccessibleChildByID(docAcc, "p"),
      "Got iframe content"
    );

    // Do this all in a single content task, ticking the refresh driver
    // manually, so that the parent process can't delete any of the actors in
    // the meantime.
    await invokeContentTask(browser, [], () => {
      const iframe = content.document.getElementById("iframe");
      for (let i = 0; i < 3; ++i) {
        iframe.style.display = "none";
        iframe.getBoundingClientRect();
        iframe.style.display = "";
        iframe.getBoundingClientRect();
        content.windowUtils.advanceTimeAndRefresh(100);
        content.windowUtils.advanceTimeAndRefresh(100);
      }
      content.windowUtils.restoreNormalRefresh();
    });

    await untilCacheOk(
      () => findAccessibleChildByID(docAcc, "p"),
      "Got iframe content after reframe"
    );
  },
  { chrome: true, topLevel: true }
);
