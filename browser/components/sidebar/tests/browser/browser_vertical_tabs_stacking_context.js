/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Check that when enabling vertical tabs, we can still receive a click on the urlbar results view
 */
add_task(async function test_click_urlbar_results() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_VISIBILITY_PREF, "always-show"],
    ],
  });

  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isVisible(document.querySelector("sidebar-main"));
  }, "The new sidebar is shown.");

  await BrowserTestUtils.withNewTab(
    {
      url: "about:blank",
      gBrowser,
    },
    async () => {
      let urlbarResultsElem = document.querySelector("#urlbar .urlbarView");

      EventUtils.synthesizeMouseAtCenter(
        document.querySelector("#urlbar .urlbar-input-box"),
        {}
      );
      await TestUtils.waitForCondition(() => {
        return BrowserTestUtils.isVisible(urlbarResultsElem);
      });

      let promiseClicked = BrowserTestUtils.waitForEvent(
        urlbarResultsElem,
        "click",
        true
      );

      EventUtils.synthesizeMouseAtCenter(urlbarResultsElem, {});

      await promiseClicked;
      Assert.ok(true, "urlbar results view received a click");
    }
  );
});
