/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function setup() {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let originals = providersManager.providers;
  providersManager.providers = [];
  registerCleanupFunction(() => {
    providersManager.providers = originals;
  });
});

add_task(async function overflow() {
  let row = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  await assertOverflow(row.querySelector(".urlbarView-title"), true);
  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function reuse() {
  let firstRow = await openAndGetTargetRow({ title: "short" });
  await assertOverflow(firstRow.querySelector(".urlbarView-title"), false);

  let secondRow = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  Assert.equal(firstRow, secondRow, "The row should be reused");
  await assertOverflow(secondRow.querySelector(".urlbarView-title"), true);

  let thirdRow = await openAndGetTargetRow({ title: "short" });
  Assert.equal(firstRow, thirdRow, "The row should be reused");
  await assertOverflow(thirdRow.querySelector(".urlbarView-title"), false);

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function hideAndShow() {
  let row = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  let title = row.querySelector(".urlbarView-title");
  await assertOverflow(title, true);

  // Hide the row.
  row.toggleAttribute("hidden", true);
  await TestUtils.waitForCondition(
    () => !title.hasAttribute("overflow"),
    "Waiting for the overflow attribute to be dropped while hidden"
  );

  // Show it again.
  row.toggleAttribute("hidden", false);
  await TestUtils.waitForCondition(
    () => title.hasAttribute("overflow"),
    "Waiting for the overflow attribute to be added again"
  );
  await assertOverflow(title, true);

  await UrlbarTestUtils.promisePopupClose(window);
});

const TITLE_MUTATIONS = {
  attributes: true,
  characterData: true,
  childList: true,
  subtree: true,
};

add_task(async function localizedTitleInReusedRow() {
  let row = await openAndGetTargetRow({ title: "0123457890".repeat(50) });
  let title = row.querySelector(".urlbarView-title");
  await assertOverflow(title, true);

  info(
    "Show a localized title, the previous title shouldn't be shown meanwhile"
  );
  let searchTerms = "abcdefghij".repeat(50);
  let localizedTitle = `example.com: ${searchTerms}`;
  let sawPreviousTitle = false;
  let sawEmptyL10nNode = false;
  let localized = BrowserTestUtils.waitForMutationCondition(
    title,
    TITLE_MUTATIONS,
    () => {
      // The row still shows the previous result until it's updated.
      if (
        row.result.payload.titleL10n &&
        title.textContent.startsWith("0123457890")
      ) {
        sawPreviousTitle = true;
      }
      // The title should stay `:empty` until the localized title is added.
      if (title.querySelector(".urlbarView-title-l10n:empty")) {
        sawEmptyL10nNode = true;
      }
      return title.textContent == localizedTitle;
    }
  );
  let secondRow = await openAndGetTargetRow({
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    payload: keywordPayload(searchTerms),
  });
  Assert.equal(row, secondRow, "The row should be reused");
  await localized;
  Assert.ok(!sawPreviousTitle, "The previous title shouldn't be shown");
  Assert.ok(!sawEmptyL10nNode, "The title shouldn't contain an empty element");

  await assertOverflow(title, true);
  await BrowserTestUtils.waitForMutationCondition(
    title,
    { attributeFilter: ["title"] },
    () => title.getAttribute("title") == localizedTitle
  );

  info("Translating the title again shouldn't remove the tooltip");
  await document.l10n.translateFragment(title);
  Assert.equal(
    title.getAttribute("title"),
    localizedTitle,
    "The tooltip should still be the localized title"
  );

  info("Update the search terms, the title shouldn't be blank meanwhile");
  let newSearchTerms = `${searchTerms}k`;
  let newLocalizedTitle = `example.com: ${newSearchTerms}`;
  let sawBlankTitle = false;
  let updated = BrowserTestUtils.waitForMutationCondition(
    title,
    TITLE_MUTATIONS,
    () => {
      if (!title.textContent) {
        sawBlankTitle = true;
      }
      return title.textContent == newLocalizedTitle;
    }
  );
  let thirdRow = await openAndGetTargetRow({
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    payload: keywordPayload(newSearchTerms),
  });
  Assert.equal(row, thirdRow, "The row should be reused");
  await updated;
  Assert.ok(!sawBlankTitle, "The title shouldn't be blank");
  await BrowserTestUtils.waitForMutationCondition(
    title,
    { attributeFilter: ["title"] },
    () => title.getAttribute("title") == newLocalizedTitle
  );

  info(
    "Show the same string for another domain, the previous one shouldn't be shown"
  );
  let otherDomainTitle = `example.org: ${newSearchTerms}`;
  let sawPreviousDomain = false;
  let otherDomainShown = BrowserTestUtils.waitForMutationCondition(
    title,
    TITLE_MUTATIONS,
    () => {
      if (
        row.result.payload.url.startsWith("https://example.org/") &&
        title.textContent.startsWith("example.com:")
      ) {
        sawPreviousDomain = true;
      }
      return title.textContent == otherDomainTitle;
    }
  );
  let fourthRow = await openAndGetTargetRow({
    url: "https://example.org/test",
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    payload: keywordPayload(newSearchTerms, "example.org"),
  });
  Assert.equal(row, fourthRow, "The row should be reused");
  await otherDomainShown;
  Assert.ok(!sawPreviousDomain, "The previous domain shouldn't be shown");

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function localizedTitleOverflowChange() {
  let row = await openAndGetTargetRow({
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    payload: keywordPayload("short"),
  });
  let title = row.querySelector(".urlbarView-title");
  await assertOverflow(title, false);

  info("Update the same title with long search terms");
  let searchTerms = "abcdefghij".repeat(50);
  let secondRow = await openAndGetTargetRow({
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    payload: keywordPayload(searchTerms),
  });
  Assert.equal(row, secondRow, "The row should be reused");
  await assertLocalizedTitleAndTooltip(title, `example.com: ${searchTerms}`);

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function restyledTitleTooltip() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });

  let searchTerms = "abcdefghij".repeat(50);
  let searchString = `kw ${searchTerms}`;
  let localizedTitle = `example.com: ${searchTerms}`;
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let provider = new UrlbarTestUtils.TestProvider({
    name: "RestyleTestProvider",
    type: UrlbarShared.PROVIDER_TYPE.HEURISTIC,
    results: [
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.KEYWORD,
        source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
        heuristic: true,
        payload: {
          url: "https://example.com/?q=test",
          ...keywordPayload(searchTerms),
        },
      }),
    ],
  });
  providersManager.registerProvider(provider);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: searchString,
  });
  providersManager.unregisterProvider(provider);

  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
  let title = row.querySelector(".urlbarView-title");
  await assertLocalizedTitleAndTooltip(title, localizedTitle);

  info("Select a one-off, the title is restyled as the search string");
  let oneOffs = UrlbarTestUtils.getOneOffSearchButtons(window);
  oneOffs.selectedButton = oneOffs.getSelectableButtons(true)[0];
  Assert.equal(title.textContent, searchString, "The title is restyled");
  Assert.equal(
    title.getAttribute("title"),
    searchString,
    "The tooltip should be the search string"
  );

  info("Deselect the one-off, the localized title is restored");
  oneOffs.selectedButton = null;
  await assertLocalizedTitleAndTooltip(title, localizedTitle);

  await UrlbarTestUtils.promisePopupClose(window);
  await SpecialPowers.popPrefEnv();
});

function keywordPayload(searchTerms, domain = "example.com") {
  return {
    keyword: "kw",
    input: `kw ${searchTerms}`,
    titleL10n: {
      id: "urlbar-result-bookmark-keyword-search",
      args: { domain, searchTerms },
    },
  };
}

async function assertLocalizedTitleAndTooltip(title, localizedTitle) {
  await BrowserTestUtils.waitForMutationCondition(
    title,
    TITLE_MUTATIONS,
    () => title.textContent == localizedTitle
  );
  await assertOverflow(title, true);
  await BrowserTestUtils.waitForMutationCondition(
    title,
    { attributeFilter: ["title"] },
    () => title.getAttribute("title") == localizedTitle
  );
}

async function openAndGetTargetRow({
  url = "https://example.com/test",
  title,
  type = UrlbarShared.RESULT_TYPE.URL,
  payload = { title },
}) {
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  let provider = new UrlbarTestUtils.TestProvider({
    name: "OverflowTestProvider",
    results: [
      new UrlbarResult({
        type,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        suggestedIndex: 0,
        payload: { url, ...payload },
      }),
    ],
  });
  providersManager.registerProvider(provider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "any",
  });
  providersManager.unregisterProvider(provider);

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (details.url == url) {
      return details.element.row;
    }
  }

  throw new Error("Result not found unexpectedly");
}

async function assertOverflow(element, expected) {
  await TestUtils.waitForCondition(
    () => element.hasAttribute("overflow") == expected,
    `Waiting for the overflow attribute to be ${expected}`
  );
  if (expected) {
    Assert.greater(element.scrollWidth, element.clientWidth);
  } else {
    Assert.lessOrEqual(element.scrollWidth, element.clientWidth);
  }
}
