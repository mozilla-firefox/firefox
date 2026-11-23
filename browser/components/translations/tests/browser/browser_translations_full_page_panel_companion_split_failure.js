/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/**
 * This test case ensures a failed Split View creation logs an error and
 * opens the companion with the chosen languages in a standalone tab.
 */
add_task(async function test_companion_split_creation_failure() {
  const { cleanup, tab: sourceTab } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["browser.tabs.splitView.enabled", true]],
  });
  const sandbox = sinon.createSandbox();
  const consoleStorage = Cc["@mozilla.org/consoleAPI-storage;1"].getService(
    Ci.nsIConsoleAPIStorage
  );
  const errors = [];
  const logListener = event => {
    if (
      event.level === "error" &&
      event.arguments[0] ===
        "Failed to create a Split View for the translations companion. Opening standalone."
    ) {
      errors.push(event);
    }
  };
  let translationsTab;

  consoleStorage.addLogEventListener(
    logListener,
    Services.scriptSecurityManager.getSystemPrincipal()
  );

  try {
    await FullPageTranslationsTestUtils.openPanel({
      expectedFromLanguage: "es",
      expectedToLanguage: "en",
    });

    info("Click the companion button with a failing Split View creation");
    const createSplitView = sandbox
      .stub(gBrowser, "addTabSplitView")
      .callThrough();
    createSplitView.onFirstCall().returns(null);
    const tabCount = gBrowser.tabs.length;
    const translationsOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      url => url.startsWith("about:translations"),
      true
    );
    await FullPageTranslationsTestUtils.clickCompanionButton();

    Assert.equal(createSplitView.callCount, 1, "The click attempts one split");
    [translationsTab] = createSplitView.firstCall.args[0];
    if (gBrowser.selectedTab !== translationsTab) {
      throw new Error("The tab created for the companion should be selected");
    }
    Assert.equal(gBrowser.tabs.length, tabCount + 1, "Only one tab opens");
    Assert.ok(!translationsTab.splitview, "The translation tab is standalone");
    Assert.ok(!sourceTab.splitview, "The source page is still standalone");
    Assert.equal(errors.length, 1, "The split failure logs one console error");
    Assert.equal(
      await translationsOpened,
      translationsTab,
      "The selected tab loads about:translations"
    );
    await AboutTranslationsTestUtils.assertSelectedLanguages(
      translationsTab.linkedBrowser,
      { sourceLanguage: "es", targetLanguage: "en" }
    );
  } finally {
    sandbox.restore();
    consoleStorage.removeLogEventListener(logListener);
    if (translationsTab?.isConnected && !translationsTab.closing) {
      await BrowserTestUtils.removeTab(translationsTab);
    }
    await cleanup();
  }
});
