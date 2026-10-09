/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This tests that other panels close when the urlbar panel opens.
 */

"use strict";

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);
let gCUITestUtils = new CustomizableUITestUtils(window);

add_setup(async function () {
  // Tests get no default Top Sites, and the view is opened on an empty string
  // here, which would otherwise leave it closed with no results.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
});

add_task(async function () {
  for (let openFn of [
    () => {
      EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
    },
    () => {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
  ]) {
    await gCUITestUtils.openMainMenu();
    Assert.equal(
      PanelUI.panel.state,
      "open",
      "Check that panel state is 'open'"
    );
    let promiseHidden = new Promise(resolve => {
      PanelUI.panel.addEventListener("popuphidden", resolve, { once: true });
    });
    await UrlbarTestUtils.promisePopupOpen(window, openFn);
    await promiseHidden;
    Assert.equal(
      PanelUI.panel.state,
      "closed",
      "Check that panel state is 'closed'"
    );
    await UrlbarTestUtils.promisePopupClose(window);
  }
});
