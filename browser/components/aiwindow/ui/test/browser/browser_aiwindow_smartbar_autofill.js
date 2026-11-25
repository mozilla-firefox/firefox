/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the Smartbar autofill suffix and highlight behavior.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

add_setup(async function () {
  await PlacesTestUtils.addVisits([
    {
      url: "https://example.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
    {
      url: "https://test.com/",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ]);

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

const PREFIX = "exa";

async function openWindowWithAutofill() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await typeInSmartbar(browser, PREFIX);

  const autofilledValue = await SpecialPowers.spawn(
    browser,
    [PREFIX],
    async typedPrefix => {
      const aiWindow = content.document.querySelector("ai-window");
      const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");

      await ContentTaskUtils.waitForCondition(
        () =>
          smartbar.value === "example.com/" &&
          smartbar.selectionStart === typedPrefix.length &&
          smartbar.selectionEnd === smartbar.value.length,
        "Autofill suffix is correctly set and highlighted"
      );
      return smartbar.value;
    }
  );

  return { win, browser, autofilledValue };
}

add_task(async function test_autofill_suffix_present() {
  const { win } = await openWindowWithAutofill();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_autofill_suffix_reapply() {
  const { win, browser, autofilledValue } = await openWindowWithAutofill();

  const nextChar = autofilledValue[PREFIX.length];
  const typedValue = `${PREFIX}${nextChar}`;
  await typeInSmartbar(browser, autofilledValue[PREFIX.length]);

  await SpecialPowers.spawn(
    browser,
    [autofilledValue, typedValue],
    async (autofilled, typed) => {
      const aiWindow = content.document.querySelector("ai-window");
      const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");

      await ContentTaskUtils.waitForCondition(
        () =>
          smartbar.value === autofilled &&
          smartbar.selectionStart === typed.length &&
          smartbar.selectionEnd === autofilled.length,
        "Autofill suffix is correctly set and highlighted"
      );

      const queryContext = await smartbar.lastQueryContextPromise;
      Assert.equal(
        queryContext.searchString,
        typed,
        "Query matches the typed prefix and not the autofilled value"
      );
      Assert.equal(
        smartbar.userTypedValue,
        typed,
        "userTypedValue matches the typed prefix and not the autofilled value"
      );
    }
  );

  await BrowserTestUtils.closeWindow(win);
});
