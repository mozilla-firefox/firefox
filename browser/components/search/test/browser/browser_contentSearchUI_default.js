/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_ENGINE_NAME = "searchSuggestionEngine";
let extension;
let defaultEngine;
let addedEngine;

add_setup(async function () {
  // Disable window occlusion. Bug 1733955
  if (navigator.platform.indexOf("Win") == 0) {
    await SpecialPowers.pushPrefEnv({
      set: [["widget.windows.window_occlusion_tracking.enabled", false]],
    });
  }

  defaultEngine = await SearchService.getDefault();

  extension = await SearchTestUtils.installSearchExtension({
    id: TEST_ENGINE_NAME,
    name: TEST_ENGINE_NAME,
    suggest_url:
      "https://example.com/browser/browser/components/search/test/browser/searchSuggestionEngine.sjs",
    suggest_url_get_params: "query={searchTerms}",
  });

  addedEngine = await SearchService.getEngineByName(TEST_ENGINE_NAME);

  // Enable suggestions in this test. Otherwise, the string in the content
  // search box changes. The feature gate stays off because this file tests the
  // handoff search bar, which the newtab <moz-urlbar> supersedes.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", true],
      ["browser.urlbar.newtab.featureGate", false],
    ],
  });

  registerCleanupFunction(async () => {
    await SearchService.setDefault(
      defaultEngine,
      SearchService.CHANGE_REASON.UNKNOWN
    );
  });
});

async function ensureIcon(tab, expectedIcon) {
  let response = await fetch(expectedIcon);
  const expectedType = response.headers.get("content-type");
  let blobURL = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [expectedIcon],
    async function (icon) {
      await ContentTaskUtils.waitForCondition(() => !content.document.hidden);

      let computedStyle = content.window.getComputedStyle(
        content.document.body
      );
      await ContentTaskUtils.waitForCondition(() => {
        let value = computedStyle.getPropertyValue("--newtab-search-icon");
        return icon.startsWith("blob:")
          ? value.startsWith("url(blob:")
          : value == `url(${icon})`;
      }, "Search icon not updated");

      if (icon.startsWith("blob:")) {
        // We don't check the data here as `browser_contentSearch.js` performs
        // those checks.
        Assert.ok(
          computedStyle
            .getPropertyValue("--newtab-search-icon")
            .startsWith("url(blob:"),
          "Should have a blob URL"
        );
        return computedStyle
          .getPropertyValue("--newtab-search-icon")
          .slice(4, -1);
      }
      Assert.equal(
        computedStyle.getPropertyValue("--newtab-search-icon"),
        `url(${icon})`,
        "Should have the expected icon"
      );
      return icon;
    }
  );
  // Ensure that the icon has the correct MIME type.
  if (blobURL) {
    try {
      response = await fetch(blobURL);
      const type = response.headers.get("content-type");
      Assert.equal(type, expectedType, "Should have the correct MIME type");
    } catch (error) {
      console.error("Error retrieving url: ", error);
    }
  }
}

async function ensurePlaceholder(tab, expectedId, expectedEngine) {
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [expectedId, expectedEngine],
    async function (id, engine) {
      await ContentTaskUtils.waitForCondition(() => !content.document.hidden);

      await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector("content-search-handoff-ui"),
        "content-search-handoff-ui not found."
      );
      let handoffUI = content.document.querySelector(
        "content-search-handoff-ui"
      );
      await handoffUI.updateComplete;
      let buttonNode = handoffUI.shadowRoot.querySelector(
        ".search-handoff-button"
      );
      let expectedAttributes = { id, args: engine ? { engine } : null };
      // A remounted element is only labelled once its own Engine reply
      // arrives, which can come after ensureIcon passes.
      await ContentTaskUtils.waitForMutationCondition(
        buttonNode,
        { attributeFilter: ["data-l10n-id", "data-l10n-args"] },
        () => {
          let { id: l10nId, args } =
            content.document.l10n.getAttributes(buttonNode);
          return (
            l10nId == expectedAttributes.id &&
            JSON.stringify(args) == JSON.stringify(expectedAttributes.args)
          );
        }
      );
      Assert.deepEqual(
        content.document.l10n.getAttributes(buttonNode),
        expectedAttributes,
        "Expected updated l10n ID and args."
      );
    }
  );
}

async function runNewTabTest() {
  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:newtab",
    gBrowser,
    waitForLoad: false,
  });

  let engineIcon = await defaultEngine.getIconURL(16);

  await ensureIcon(tab, engineIcon);
  await ensurePlaceholder(
    tab,
    "newtab-search-box-handoff-input",
    SearchService.defaultEngine.name
  );

  await SearchService.setDefault(
    addedEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  // We only show the engine's own icon for config engines, otherwise show
  // a default. xref https://bugzilla.mozilla.org/show_bug.cgi?id=1449338#c19
  await ensureIcon(tab, "chrome://global/skin/icons/search-glass.svg");
  await ensurePlaceholder(tab, "newtab-search-box-handoff-input-no-engine");

  // Disable suggestions in the Urlbar. This should update the placeholder
  // string since handoff will now enter search mode.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", false]],
  });
  await ensurePlaceholder(tab, "newtab-search-box-input");
  await SpecialPowers.popPrefEnv();

  await SearchService.setDefault(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  BrowserTestUtils.removeTab(tab);
}

add_task(async function test_content_search_attributes() {
  await runNewTabTest();
});

add_task(async function test_content_search_attributes_in_private_window() {
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
    waitForTabURL: "about:privatebrowsing",
  });
  let tab = win.gBrowser.selectedTab;

  let engineIcon = await defaultEngine.getIconURL(16);

  await ensureIcon(tab, engineIcon);
  await ensurePlaceholder(
    tab,
    "about-private-browsing-handoff",
    SearchService.defaultEngine.name
  );

  await SearchService.setDefault(
    addedEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  // We only show the engine's own icon for config engines, otherwise show
  // a default. xref https://bugzilla.mozilla.org/show_bug.cgi?id=1449338#c19
  await ensureIcon(tab, "chrome://global/skin/icons/search-glass.svg");
  await ensurePlaceholder(tab, "about-private-browsing-handoff-no-engine");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.searches", false]],
  });
  await ensurePlaceholder(tab, "about-private-browsing-search-btn");
  await SpecialPowers.popPrefEnv();

  await SearchService.setDefault(
    defaultEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_content_search_permanent_private_browsing() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.autostart", true]],
  });

  let win = await BrowserTestUtils.openNewBrowserWindow();
  await runNewTabTest();
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});
