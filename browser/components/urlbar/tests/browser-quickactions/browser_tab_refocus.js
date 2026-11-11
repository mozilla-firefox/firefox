/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for QuickActions that re-focus tab..
 */

"use strict";

requestLongerTimeout(3);

const { AboutAddonsTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AboutAddonsTestUtils.sys.mjs"
);

add_task(async function test_about_pages() {
  const testData = [
    {
      firstInput: "downloads",
      uri: "about:downloads",
    },
    {
      firstInput: "logins",
      uri: "about:logins",
    },
    {
      firstInput: "settings",
      uri: "about:preferences",
    },
    {
      firstInput: "edit pdf",
      uri: "about:pdf",
    },
    {
      firstInput: "disable ai",
      uri: "about:preferences#ai",
    },
    {
      firstInput: "add-ons",
      uri: "about:addons",
      aboutAddonsCategory: "discover",
    },
    {
      firstInput: "extensions",
      uri: "about:addons",
      aboutAddonsCategory: "extension",
      numTabPress: 2,
    },
    {
      firstInput: "themes",
      uri: "about:addons",
      aboutAddonsCategory: "theme",
      numTabPress: 2,
    },
    {
      firstLoad: "about:preferences#home",
      secondInput: "settings",
      uri: "about:preferences#home",
    },
  ];

  for (const {
    firstInput,
    firstLoad,
    secondInput,
    uri,
    aboutAddonsCategory,
    numTabPress = 1,
  } of testData) {
    info("Setup initial state");
    let firstTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
    let onLoad = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      uri
    );
    if (firstLoad) {
      info("Load initial URI");
      BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, uri);
    } else {
      info("Open about page by quick action");
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: firstInput,
      });
      for (let i = 0; i < numTabPress; i++) {
        EventUtils.synthesizeKey("KEY_Tab", {}, window);
      }
      EventUtils.synthesizeKey("KEY_Enter", {}, window);
    }
    await onLoad;

    if (aboutAddonsCategory) {
      info("Check whether the expected about:addons category is in the page");
      Assert.ok(
        AboutAddonsTestUtils.isCategoryButtonSelected(
          gBrowser.selectedBrowser.contentWindow,
          aboutAddonsCategory
        ),
        `There is expected about:addons category ${aboutAddonsCategory}`
      );
    }

    info("Do the second quick action in second tab");
    let secondTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: secondInput || firstInput,
    });
    for (let i = 0; i < numTabPress; i++) {
      EventUtils.synthesizeKey("KEY_Tab", {}, window);
    }
    EventUtils.synthesizeKey("KEY_Enter", {}, window);
    // The refocus action runs parent-side, so on the actor message path the tab
    // switch happens asynchronously after the pick rather than synchronously.
    await TestUtils.waitForCondition(
      () => gBrowser.selectedTab == firstTab,
      "Refocused the tab that opened the about page"
    );
    Assert.equal(
      gBrowser.selectedTab,
      firstTab,
      "Switched to the tab that is opening the about page"
    );
    Assert.equal(
      gBrowser.selectedBrowser.currentURI.spec,
      uri,
      "URI is not changed"
    );
    Assert.equal(gBrowser.tabs.length, 3, "Not opened a new tab");

    if (aboutAddonsCategory) {
      info("Check whether the component is still in the page");
      Assert.ok(
        AboutAddonsTestUtils.isCategoryButtonSelected(
          gBrowser.selectedBrowser.contentWindow,
          aboutAddonsCategory
        ),
        `There is expected about:addons category ${aboutAddonsCategory}`
      );
    }

    BrowserTestUtils.removeTab(secondTab);
    BrowserTestUtils.removeTab(firstTab);
  }
});

add_task(async function test_about_addons_pages() {
  let testData = [
    {
      cmd: "add-ons",
      testFun: async () =>
        AboutAddonsTestUtils.isCategoryButtonSelected(
          gBrowser.selectedBrowser.contentWindow,
          "discover"
        ),
    },
    {
      cmd: "extensions",
      testFun: async () =>
        AboutAddonsTestUtils.isCategoryButtonSelected(
          gBrowser.selectedBrowser.contentWindow,
          "extension"
        ),
      numTabPress: 2,
    },
    {
      cmd: "themes",
      testFun: async () =>
        AboutAddonsTestUtils.isCategoryButtonSelected(
          gBrowser.selectedBrowser.contentWindow,
          "theme"
        ),
      numTabPress: 2,
    },
  ];

  info("Pick all actions related about:addons");
  let originalTab = gBrowser.selectedTab;
  for (const { cmd, testFun, numTabPress = 1 } of testData) {
    await BrowserTestUtils.openNewForegroundTab(gBrowser);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: cmd,
    });
    for (let i = 0; i < numTabPress; i++) {
      EventUtils.synthesizeKey("KEY_Tab", {}, window);
    }
    EventUtils.synthesizeKey("KEY_Enter", {}, window);
    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    Assert.ok(await testFun(), "The page content is correct");
  }
  Assert.equal(
    gBrowser.tabs.length,
    testData.length + 1,
    "Tab length is correct"
  );

  info("Pick all again");
  for (const { cmd, testFun, numTabPress = 1 } of testData) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: cmd,
    });

    for (let i = 0; i < numTabPress; i++) {
      EventUtils.synthesizeKey("KEY_Tab", {}, window);
    }
    EventUtils.synthesizeKey("KEY_Enter", {}, window);
    await TestUtils.waitForCondition(() => testFun());
    Assert.ok(true, "The tab correspondent action is selected");
  }
  Assert.equal(
    gBrowser.tabs.length,
    testData.length + 1,
    "Tab length is not changed"
  );

  for (const tab of gBrowser.tabs) {
    if (tab !== originalTab) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});
