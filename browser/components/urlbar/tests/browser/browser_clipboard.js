/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Browser test for clipboard suggestion.
 */

"use strict";

const { UrlbarProviderClipboard, CLIPBOARD_IMPRESSION_LIMIT } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/urlbar/UrlbarProviderClipboard.sys.mjs"
  );

const TEST_TOP_SITE = "https://example.com";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.clipboard.featureGate", true],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      ["browser.urlbar.suggest.clipboard", true],
      ["browser.urlbar.suggest.topsites", true],
      ["browser.newtabpage.activity-stream.default.sites", TEST_TOP_SITE],
    ],
  });

  await updateTopSites(sites => sites?.length == 1);

  registerCleanupFunction(async () => {
    SpecialPowers.clipboardCopyString("");
    await PlacesUtils.history.clear();
  });
});

async function searchEmptyStringAndGetFirstRow() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
  return UrlbarTestUtils.getRowAt(window, 0);
}

async function checkClipboardSuggestionAbsent(startIdx) {
  for (let i = startIdx; i < UrlbarTestUtils.getResultCount(window); i++) {
    const row = await UrlbarTestUtils.getRowAt(window, i);
    Assert.notEqual(
      row.result.providerName,
      UrlbarProviderClipboard.name,
      `Clipboard suggestion should be absent (checking index ${i})`
    );
  }
}

add_task(async function testFormattingOfClipboardSuggestion() {
  let unicodeURL = "https://пример.com/";
  let punycodeURL = "https://xn--e1afmkfd.com/";

  SpecialPowers.clipboardCopyString(unicodeURL);

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      let { result } = await searchEmptyStringAndGetFirstRow();

      Assert.equal(
        result.providerName,
        UrlbarProviderClipboard.name,
        "The first result is a clipboard valid url suggestion."
      );
      Assert.equal(
        result.payload.url,
        punycodeURL,
        "The Clipboard suggestion URL should not be decoded."
      );
      Assert.equal(
        result.payload.title,
        unicodeURL,
        "The Clipboard suggestion title should be decoded."
      );
    }
  );
});

// Verifies that a valid URL copied to the clipboard results in the
// display of a corresponding suggestion in the URL bar as the first
// suggestion with accurate URL and icon. Also ensures that engaging
// with a clipboard suggestion leads to navigation to the copied URL
// and subsequent absence of the suggestion upon refocusing the URL bar.
add_task(async function testUserEngagementWithClipboardSuggestion() {
  const validURL = "https://example.com/";
  SpecialPowers.clipboardCopyString(validURL);
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async browser => {
      let { result } = await searchEmptyStringAndGetFirstRow();
      let onLoad = BrowserTestUtils.browserLoaded(browser, false);

      Assert.equal(
        result.providerName,
        UrlbarProviderClipboard.name,
        "The first result is a clipboard valid url suggestion."
      );
      Assert.equal(
        result.payload.url,
        validURL,
        "The Clipboard suggestion URL and the valid URL should match."
      );
      Assert.equal(
        result.icon,
        "chrome://global/skin/icons/clipboard.svg",
        "Clipboard suggestion icon"
      );
      await checkClipboardSuggestionAbsent(1);

      // Focus and select the clipbaord result.
      EventUtils.synthesizeKey("KEY_ArrowDown");
      EventUtils.synthesizeKey("KEY_Enter");
      await onLoad;

      Assert.equal(
        gBrowser.selectedBrowser.currentURI.spec,
        validURL,
        "Navigated to the validURL webpage after selecting the clipboard result."
      );
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "",
      });
      await checkClipboardSuggestionAbsent(0);
    }
  );
  await PlacesUtils.history.clear();
});

// This test confirms that dismissing the result from the result menu
// button after copying a valid URL dismisses the clipboard suggestion,
// and the suggestion does not reappear upon refocusing the URL bar.
add_task(async function testDismissClipboardSuggestion() {
  SpecialPowers.clipboardCopyString("https://example.com/2");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      const resultIndex = 0;
      const command = "dismiss";
      let row = await searchEmptyStringAndGetFirstRow();

      Assert.equal(
        row.result.providerName,
        UrlbarProviderClipboard.name,
        "Clipboard suggestion should be present"
      );
      await checkClipboardSuggestionAbsent(1);
      await UrlbarTestUtils.openResultMenuAndClickItem(window, command, {
        resultIndex,
      });
      Assert.ok(
        gURLBar.view.isOpen,
        "The view should remain open after clicking the command"
      );
      Assert.ok(
        !row.hasAttribute("feedback-acknowledgement"),
        "Row should not have feedback acknowledgement after clicking command"
      );
      await UrlbarTestUtils.promisePopupClose(window, () => {
        gURLBar.blur();
      });

      // Do the same search again. The suggestion should not appear.
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "",
      });
      await checkClipboardSuggestionAbsent(0);
    }
  );
});

// The test validates that the clipboard suggestion is displayed for
// the first two URL bar openings after copying a valid URL, but is
// suppressed on the third opening of URL bar.
add_task(async function testClipboardSuggestionLimit() {
  SpecialPowers.clipboardCopyString("https://example.com/3");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      for (let i = 0; i < CLIPBOARD_IMPRESSION_LIMIT; i++) {
        const { result } = await searchEmptyStringAndGetFirstRow();
        Assert.equal(
          result.providerName,
          UrlbarProviderClipboard.name,
          "Clipboard suggestion should be present as the first suggestion."
        );
        await checkClipboardSuggestionAbsent(1);
        await UrlbarTestUtils.promisePopupClose(window, () => {
          gURLBar.blur();
        });
      }
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "",
      });
      await checkClipboardSuggestionAbsent(0);
    }
  );
});

// This test ensures that copying non-URL content to the clipboard
// results in the absence of a clipboard suggestion when opening
// the URL bar.
add_task(async function testNonUrlClipboardSuggestion() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      const malformedURLs = [
        "plain text",
        "ftp://example.com",
        "https://example.com[invalid]",
        // Testing http because it is considered as a valid URL.
        // eslint-disable-next-line sdl/no-insecure-url
        "http://",
        "https://example.com some text",
        "https://example.com/ some text",
      ];
      for (let i = 0; i < malformedURLs.length; i++) {
        SpecialPowers.clipboardCopyString(malformedURLs[i]);
        await UrlbarTestUtils.promiseAutocompleteResultPopup({
          window,
          value: "",
        });
        Assert.ok(gURLBar.view.isOpen, "UrlbarView should be open.");
        await checkClipboardSuggestionAbsent(0);
        await UrlbarTestUtils.promisePopupClose(window, () => {
          gURLBar.blur();
        });
      }
    }
  );
});

// This test verifies that clipboard suggestions are displayed
// based on the toggled state of the 'clipboard.featureGate' preference.
add_task(async function testClipboardFeatureGateToggle() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.clipboard.featureGate", false],
      ["browser.urlbar.suggest.clipboard", true],
    ],
  });
  SpecialPowers.clipboardCopyString("https://example.com/4");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "",
      });
      await checkClipboardSuggestionAbsent(0);
      await UrlbarTestUtils.promisePopupClose(window, () => {
        gURLBar.blur();
      });
      await SpecialPowers.pushPrefEnv({
        set: [["browser.urlbar.clipboard.featureGate", true]],
      });
      const { result } = await searchEmptyStringAndGetFirstRow();
      Assert.equal(
        result.providerName,
        UrlbarProviderClipboard.name,
        "Clipboard suggestion should be present as the first suggestion."
      );
      await checkClipboardSuggestionAbsent(1);
    }
  );
});

// This test confirms that clipboard suggestions are presented based on
// the state of the 'suggest.clipboard' preference toggle.
add_task(async function testClipboardSuggestToggle() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.clipboard.featureGate", true],
      ["browser.urlbar.suggest.clipboard", false],
    ],
  });
  SpecialPowers.clipboardCopyString("https://example.com/5");
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:home" },
    async () => {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "",
      });
      await checkClipboardSuggestionAbsent(0);
      await UrlbarTestUtils.promisePopupClose(window, () => {
        gURLBar.blur();
      });
      await SpecialPowers.pushPrefEnv({
        set: [["browser.urlbar.suggest.clipboard", true]],
      });
      const { result } = await searchEmptyStringAndGetFirstRow();
      Assert.equal(
        result.providerName,
        UrlbarProviderClipboard.name,
        "Clipboard suggestion should be present as the first suggestion."
      );
      await checkClipboardSuggestionAbsent(1);
    }
  );
});

add_task(async function emptySearch_withClipboardEntry() {
  SpecialPowers.clipboardCopyString("https://example.com/1");
  const MAX_RESULTS = 3;
  let expectedHistoryResults = [];

  for (let i = 0; i < MAX_RESULTS; i++) {
    await PlacesTestUtils.addVisits([`http://mochi.test/${i}`]);
    expectedHistoryResults.push(`http://mochi.test/${i}`);
  }

  await BrowserTestUtils.withNewTab("about:robots", async function () {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "",
    });
    await UrlbarTestUtils.enterSearchMode(window, {
      source: UrlbarUtils.RESULT_SOURCE.HISTORY,
    });

    let urls = [];

    for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
      let url = (await UrlbarTestUtils.getDetailsOfResultAt(window, i)).url;
      urls.push(url);
    }

    urls.reverse();
    Assert.deepEqual(expectedHistoryResults, urls);

    await UrlbarTestUtils.exitSearchMode(window, { clickClose: true });
    await UrlbarTestUtils.promisePopupClose(window);
  });
  await PlacesUtils.history.clear();
});
