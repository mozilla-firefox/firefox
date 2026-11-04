/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// In a window too narrow for the input's intrinsic width, the input shrinks to
// the space the go button leaves it, so a long value doesn't run under the
// button.

"use strict";

const WINDOW_WIDTH = 500;
const LONG_VALUE = "a long query that is wider than the input ".repeat(3);

add_setup(async function () {
  useEngineWithoutSuggestions();
});

/**
 * Opens a narrow window on about:newtab, types a long value into its address
 * bar and checks that the input ends before the go button.
 *
 * @param {object} scope
 *   The test scope, which the utils take to reach `SimpleTest`.
 * @param {boolean} variantB
 *   Whether to use layout variant B instead of the default layout.
 */
async function checkInputEndsBeforeGoButton(scope, variantB) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.newtab.variantA", false],
      ["browser.urlbar.newtab.variantB", variantB],
    ],
  });

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let resized = BrowserTestUtils.waitForEvent(
    win,
    "resize",
    false,
    () => win.innerWidth <= WINDOW_WIDTH
  );
  win.resizeTo(WINDOW_WIDTH, 800);
  await resized;

  NewtabSearchbarTestUtils.init(scope, win);
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let browser = tab.linkedBrowser;
  await NewtabSearchbarTestUtils.promiseAutocompleteResultPopup({
    browser,
    value: LONG_VALUE,
  });

  await NewtabSearchbarTestUtils.spawn(
    browser,
    [variantB],
    async isVariantB => {
      let bar = NewtabSearchbarContentTestUtils.getUrlbar(content);
      Assert.equal(
        bar.hasAttribute("variant-b"),
        isVariantB,
        "The bar is in the expected layout"
      );
      let input = bar.querySelector(".urlbar-input");
      let inputBox = bar.querySelector(".urlbar-input-box");
      let goButton = bar.querySelector(".urlbar-go-button");
      Assert.greater(
        goButton.getBoundingClientRect().width,
        0,
        "The go button is shown"
      );
      Assert.greater(
        input.scrollWidth,
        input.clientWidth,
        "sanity check: the value overflows the input"
      );

      let inputRect = input.getBoundingClientRect();
      Assert.lessOrEqual(
        inputRect.right,
        inputBox.getBoundingClientRect().right,
        "The input fits in its box"
      );
      Assert.greater(
        goButton.getBoundingClientRect().left - inputRect.right,
        0,
        "There is space between the input and the go button"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
  NewtabSearchbarTestUtils.init(scope, window);
  await SpecialPowers.popPrefEnv();
}

add_task(async function defaultLayout() {
  await checkInputEndsBeforeGoButton(this, false);
});

add_task(async function variantB() {
  await checkInputEndsBeforeGoButton(this, true);
});
