/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for the paste and go functionality of the urlbar.
 */

add_task(async function () {
  const kURLs = [
    "http://example.com/1",
    "http://example.org/2\n",
    "http://\nexample.com/3\n",
  ];
  for (let url of kURLs) {
    await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
      gURLBar.focus();

      await SimpleTest.promiseClipboardChange(url, () => {
        clipboardHelper.copyString(url);
      });
      let browserLoadedPromise = BrowserTestUtils.browserLoaded(
        browser,
        false,
        url.replace(/\n/g, "")
      );
      await UrlbarTestUtils.activateContextMenuItem(window, "paste-and-go");
      // Using toSource in order to get the newlines escaped:
      info("Paste and go, loading " + url.toSource());
      await browserLoadedPromise;
      ok(true, "Successfully loaded " + url);
    });
  }
});

add_task(async function test_invisible_char() {
  const url = "http://example.com/4\u2028";
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    gURLBar.focus();
    await SimpleTest.promiseClipboardChange(url, () => {
      clipboardHelper.copyString(url);
    });
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      browser,
      false,
      url.replace(/\u2028/g, "")
    );
    await UrlbarTestUtils.activateContextMenuItem(window, "paste-and-go");
    // Using toSource in order to get the newlines escaped:
    info("Paste and go, loading " + url.toSource());
    await browserLoadedPromise;
    ok(true, "Successfully loaded " + url);
  });
});

add_task(async function test_with_input_and_results() {
  // Test paste and go When there's some input and the results pane is open.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "foo",
  });
  const url = "http://example.com/";
  await SimpleTest.promiseClipboardChange(url, () => {
    clipboardHelper.copyString(url);
  });
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    url
  );
  await UrlbarTestUtils.activateContextMenuItem(window, "paste-and-go");
  // Using toSource in order to get the newlines escaped:
  info("Paste and go, loading " + url.toSource());
  await browserLoadedPromise;
  ok(true, "Successfully loaded " + url);
});

const PASTE_AND_GO_L10N_ID = "urlbar-menuitem-paste-and-go";
const PASTE_AND_SEARCH_L10N_ID = "urlbar-menuitem-paste-and-search";

add_task(async function test_label_url() {
  await assertLabelForClipboard("http://example.com/", PASTE_AND_GO_L10N_ID);
});

add_task(async function test_label_scheme_less_url() {
  await assertLabelForClipboard("example.com", PASTE_AND_GO_L10N_ID);
  await assertLabelForClipboard("example.com/path?q=1", PASTE_AND_GO_L10N_ID);
  await assertLabelForClipboard("192.168.1.1", PASTE_AND_GO_L10N_ID);
});

add_task(async function test_label_search_term() {
  await assertLabelForClipboard(
    "firefox browser",
    AppConstants.platform == "win"
      ? PASTE_AND_GO_L10N_ID
      : PASTE_AND_SEARCH_L10N_ID
  );
});

add_task(async function test_label_clipboard_too_large() {
  await assertLabelForClipboard(
    "firefox browser ".repeat(UrlbarShared.MAX_TEXT_LENGTH),
    PASTE_AND_GO_L10N_ID
  );
});

add_task(async function test_label_search_mode() {
  gURLBar.searchMode = { source: UrlbarShared.RESULT_SOURCE.HISTORY };
  Assert.ok(gURLBar.searchMode, "The urlbar is in a search mode");
  try {
    await assertLabelForClipboard(
      "http://example.com/",
      PASTE_AND_SEARCH_L10N_ID
    );
  } finally {
    gURLBar.handleRevert();
  }
});

add_task(async function test_label_keyword_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [["keyword.enabled", false]] });
  await assertLabelForClipboard("firefox browser", PASTE_AND_GO_L10N_ID);
  await SpecialPowers.popPrefEnv();
});

async function assertLabelForClipboard(clipboardText, expectedL10nId) {
  await SimpleTest.promiseClipboardChange(clipboardText, () => {
    clipboardHelper.copyString(clipboardText);
  });

  await UrlbarTestUtils.withContextMenu(window, async popup => {
    let menuitem = popup.querySelector('[anonid="paste-and-go"]:not([hidden])');
    Assert.ok(!menuitem.disabled, "Paste and go is enabled");
    await BrowserTestUtils.waitForMutationCondition(
      menuitem,
      { attributes: true, attributeFilter: ["data-l10n-id"] },
      () => menuitem.getAttribute("data-l10n-id") == expectedL10nId,
      `Label settles on ${expectedL10nId}`
    );
  });
}
