/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that TIP type should be isRichSuggestion.

"use strict";

add_task(async function autosettings() {
  let result = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TIP,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      type: "test",
      titleL10n: { id: "urlbar-search-tips-confirm" },
    },
  });

  info("Check the following properties are set automatically if TIP result");
  Assert.ok(result.isRichSuggestion);
  Assert.equal(result.richSuggestionIconSize, 24);
});

add_task(async function ui() {
  let result = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TIP,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      type: "test",
      icon: "chrome://global/skin/icons/search-glass.svg",
      titleL10n: { id: "urlbar-search-tips-confirm" },
      descriptionL10n: { id: "urlbar-dismissal-acknowledgment-weather" },
      buttons: [
        {
          url: "https://example.com/tab",
          l10n: { id: "urlbar-search-mode-tabs" },
        },
        {
          url: "https://example.com/bookmarks",
          l10n: { id: "urlbar-search-mode-bookmarks" },
        },
      ],
      helpUrl: "https://example.com/help",
      helpL10n: {
        id: "urlbar-result-menu-tip-get-help2",
      },
    },
  });

  let provider = new UrlbarTestUtils.TestProvider({
    results: [result],
    priority: 1,
  });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    value: "test",
    window,
    fireInputEvent: true,
  });

  info("Check the container");
  let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
  Assert.ok(!row.hasAttribute("rich-suggestion"));

  info("Check the icon");
  let icon = row.querySelector(".urlbarView-favicon");
  Assert.equal(icon.src, "chrome://global/skin/icons/search-glass.svg");
  Assert.ok(BrowserTestUtils.isVisible(icon));

  info("Check the title");
  let title = row.querySelector(".urlbarView-title");
  Assert.equal(title.dataset.l10nId, "urlbar-search-tips-confirm");
  Assert.ok(BrowserTestUtils.isVisible(title));

  info("Check the description");
  let description = row.querySelector(".urlbarView-row-body-description");
  Assert.equal(
    description.dataset.l10nId,
    "urlbar-dismissal-acknowledgment-weather"
  );
  Assert.ok(BrowserTestUtils.isVisible(description));

  info("Check the first button");
  let firstButton = row.querySelector(".urlbarView-button-0");
  Assert.equal(firstButton.dataset.l10nId, "urlbar-search-mode-tabs");
  Assert.equal(firstButton.dataset.url, "https://example.com/tab");
  Assert.ok(BrowserTestUtils.isVisible(firstButton));

  info("Check the second button");
  let secondButton = row.querySelector(".urlbarView-button-1");
  Assert.equal(secondButton.dataset.l10nId, "urlbar-search-mode-bookmarks");
  Assert.equal(secondButton.dataset.url, "https://example.com/bookmarks");
  Assert.ok(BrowserTestUtils.isVisible(secondButton));

  info("Check the help");
  let help = await UrlbarTestUtils.openResultMenuAndGetItem({
    window,
    command: "help",
    resultIndex: 0,
    openByMouse: true,
  });
  Assert.ok(help);
  Assert.deepEqual(document.l10n.getAttributes(help), {
    id: "urlbar-result-menu-tip-get-help2",
    args: null,
  });
  gURLBar.view.resultMenu.hide();

  info("Check the hidden components");
  let url = row.querySelector(".urlbarView-url");
  Assert.ok(url);
  Assert.ok(BrowserTestUtils.isHidden(url));
  let titleSeparator = row.querySelector(".urlbarView-title-separator");
  Assert.ok(titleSeparator);
  Assert.ok(BrowserTestUtils.isHidden(titleSeparator));
  let action = row.querySelector(".urlbarView-action");
  Assert.ok(action);
  Assert.ok(BrowserTestUtils.isHidden(action));

  await UrlbarTestUtils.promisePopupClose(window);
  providersManager.unregisterProvider(provider);
});

add_task(async function learn_more() {
  for (let topic of ["learn_more_topic_1", "learn_more_topic_2", undefined]) {
    info(`Setup learn more link for ${topic} topic`);
    let provider = new UrlbarTestUtils.TestProvider({
      results: [
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.TIP,
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          payload: {
            type: "test",
            titleL10n: { id: "urlbar-search-tips-confirm" },
            descriptionL10n: {
              id: "firefox-suggest-onboarding-main-accept-option-label",
              parseMarkup: true,
            },
            descriptionLearnMoreTopic: topic,
          },
        }),
      ],
      priority: 1,
    });
    let providersManager = ProvidersManager.getInstanceForSap("urlbar");
    providersManager.registerProvider(provider);

    info("Open urlbar view and find learn more link from 1st row");
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      value: "any",
      window,
      fireInputEvent: true,
    });
    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
    let learnMoreLink = row.querySelector(
      ".urlbarView-row-body-description > a"
    );
    Assert.ok(
      learnMoreLink,
      "The descriptionL10n contains a learn-more link, so the element should have a learn-more link"
    );

    if (topic) {
      info("Activate learn more link and check");
      let expectedURL =
        Services.urlFormatter.formatURLPref("app.support.baseURL") + topic;
      let newTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, expectedURL);

      EventUtils.synthesizeKey("KEY_Tab");
      Assert.equal(
        UrlbarTestUtils.getSelectedElement(window),
        learnMoreLink,
        "The learn-more link should be selected after pressing Tab"
      );
      Assert.equal(
        gURLBar.value,
        expectedURL,
        "The input value should be the learn-more link URL"
      );

      EventUtils.synthesizeKey("KEY_Enter");
      info("Wait until expected url is loaded in the current tab");
      let newTab = await newTabOpened;
      Assert.ok(true, "Expected page is loaded");

      await BrowserTestUtils.removeTab(newTab);
      await PlacesUtils.history.clear();
    }

    await UrlbarTestUtils.promisePopupClose(window);
    providersManager.unregisterProvider(provider);
  }
});
