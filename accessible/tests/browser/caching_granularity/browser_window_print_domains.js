/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/printing/tests/head.js",
  this
);

/**
 * Test that the PDF output cache domains aren't applied to a document while
 * window.print() is showing a print preview for it. The source document isn't
 * a print document, so it should continue to use the global cache domains.
 */
addAccessibleTask(
  `<div id="slider" role="slider" aria-valuemin="0" aria-valuemax="10"
    aria-valuenow="5">s</div>`,
  async function testWindowPrintSourceCacheDomains(browser, docAcc) {
    const slider = findAccessibleChildByID(docAcc, "slider", [
      nsIAccessibleValue,
    ]);
    is(slider.currentValue, 5, "slider value correct before print");

    const helper = new PrintHelper(browser);
    // window.print() doesn't return until the print dialog closes, so call it
    // asynchronously.
    await SpecialPowers.spawn(browser, [], () => {
      content.setTimeout(() => content.print(), 0);
    });
    await helper.waitForDialog();
    // The Value cache domain isn't included in the PDF output cache domains.
    is(slider.currentValue, 5, "slider value correct during print preview");
    await helper.closeDialog();
  },
  { topLevel: true, iframe: true, remoteIframe: true }
);
