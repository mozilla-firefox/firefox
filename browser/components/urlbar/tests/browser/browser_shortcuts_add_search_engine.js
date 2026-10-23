/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test adding engines through search shortcut buttons.
// A more complete coverage of the detection of engines is available in
// browser_add_search_engine.js

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);
const BASE_URL =
  "http://mochi.test:8888/browser/browser/components/urlbar/tests/browser/";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.suggest.searches", false],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
    ],
  });
  // Ensure initial state.
  UrlbarTestUtils.getOneOffSearchButtons(window).invalidateCache();
});

add_task(async function shortcuts_none() {
  info("Checks the shortcuts with a page that doesn't offer any engines.");
  let url = "http://mochi.test:8888/";
  await BrowserTestUtils.withNewTab(url, async () => {
    let shortcutButtons = UrlbarTestUtils.getOneOffSearchButtons(window);
    let rebuildPromise = BrowserTestUtils.waitForEvent(
      shortcutButtons,
      "rebuild"
    );
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
    });
    await rebuildPromise;

    Assert.ok(
      !Array.from(shortcutButtons.buttons.children).some(b =>
        b.classList.contains("searchbar-engine-one-off-add-engine")
      ),
      "Check there's no buttons to add engines"
    );
  });
});

add_task(async function test_shortcuts() {
  await do_test_shortcuts(button => {
    info("Click on button");
    EventUtils.synthesizeMouseAtCenter(button, {});
  });
  await do_test_shortcuts(button => {
    info("Enter on button");
    let shortcuts = UrlbarTestUtils.getOneOffSearchButtons(window);
    while (shortcuts.selectedButton != button) {
      EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
    }
    EventUtils.synthesizeKey("KEY_Enter");
  });
});

/**
 * Test add engine shortcuts.
 *
 * @param {Function} activateTask a function receiveing the shortcut button to
 *        activate as argument. The scope of this function is to activate the
 *        shortcut button.
 */
async function do_test_shortcuts(activateTask) {
  info("Checks the shortcuts with a page that offers two engines.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_two.html";

  let faviconPromise = new Promise(resolve => {
    let listener = {
      onLinkIconAvailable(b) {
        if (b.currentURI.spec != url) {
          return;
        }
        gBrowser.removeTabsProgressListener(listener);
        resolve();
      },
    };
    gBrowser.addTabsProgressListener(listener);
  });

  await BrowserTestUtils.withNewTab(url, async () => {
    info("Waiting for tab favicon, which will be used by the search engine");
    await faviconPromise;

    let shortcutButtons = UrlbarTestUtils.getOneOffSearchButtons(window);
    let rebuildPromise = BrowserTestUtils.waitForEvent(
      shortcutButtons,
      "rebuild"
    );
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
    });
    await rebuildPromise;

    let addEngineButtons = Array.from(shortcutButtons.buttons.children).filter(
      b => b.classList.contains("searchbar-engine-one-off-add-engine")
    );
    Assert.equal(
      addEngineButtons.length,
      2,
      "Check there's two buttons to add engines"
    );

    for (let button of addEngineButtons) {
      Assert.ok(BrowserTestUtils.isVisible(button), "button is visible");
      Assert.ok(button.hasAttribute("image"), "button has image");
      await document.l10n.translateElements([button]);
      Assert.ok(
        button.getAttribute("tooltiptext").includes("add_search_engine_")
      );
      Assert.ok(
        button.getAttribute("engine-name").startsWith("add_search_engine_")
      );
      Assert.ok(
        button.classList.contains("searchbar-engine-one-off-add-engine")
      );
    }

    info("Activate the first button");
    rebuildPromise = BrowserTestUtils.waitForEvent(shortcutButtons, "rebuild");
    let enginePromise = SearchTestUtils.promiseEngine("add_search_engine_0");
    await activateTask(addEngineButtons[0]);
    info("await engine install");
    let engine = await enginePromise;
    info("await rebuild");
    await rebuildPromise;

    Assert.ok(
      UrlbarTestUtils.isPopupOpen(window),
      "Urlbar view is still open."
    );

    addEngineButtons = Array.from(shortcutButtons.buttons.children).filter(b =>
      b.classList.contains("searchbar-engine-one-off-add-engine")
    );
    Assert.equal(
      addEngineButtons.length,
      1,
      "Check there's one button to add engines"
    );
    Assert.equal(
      addEngineButtons[0].getAttribute("engine-name"),
      "add_search_engine_1"
    );
    let installedEngineButton = addEngineButtons[0].previousElementSibling;
    Assert.equal(installedEngineButton.engine.name, "add_search_engine_0");

    info("Remove the added engine");
    rebuildPromise = BrowserTestUtils.waitForEvent(shortcutButtons, "rebuild");
    await SearchService.removeEngine(engine);
    await rebuildPromise;
    Assert.equal(
      Array.from(shortcutButtons.buttons.children).filter(b =>
        b.classList.contains("searchbar-engine-one-off-add-engine")
      ).length,
      2,
      "Check there's two buttons to add engines"
    );
    await UrlbarTestUtils.promisePopupClose(window);

    info("Switch to a new tab and check the buttons are not persisted");
    await BrowserTestUtils.withNewTab("about:robots", async () => {
      rebuildPromise = BrowserTestUtils.waitForEvent(
        shortcutButtons,
        "rebuild"
      );
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "test",
      });
      await rebuildPromise;
      Assert.ok(
        !Array.from(shortcutButtons.buttons.children).some(b =>
          b.classList.contains("searchbar-engine-one-off-add-engine")
        ),
        "Check there's no option to add engines"
      );
    });
  });
}

add_task(async function shortcuts_many() {
  info("Checks the shortcuts with a page that offers many engines.");
  let url = getRootDirectory(gTestPath) + "add_search_engine_many.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    let shortcutButtons = UrlbarTestUtils.getOneOffSearchButtons(window);
    let rebuildPromise = BrowserTestUtils.waitForEvent(
      shortcutButtons,
      "rebuild"
    );
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
    });
    await rebuildPromise;

    let addEngineButtons = Array.from(shortcutButtons.buttons.children).filter(
      b => b.classList.contains("searchbar-engine-one-off-add-engine")
    );
    Assert.equal(
      addEngineButtons.length,
      gURLBar.addSearchEngineHelper.maxInlineEngines,
      "Check there's a maximum of `maxInlineEngines` buttons to add engines"
    );
  });
});

add_task(async function shortcuts_without_other_engines() {
  info("Checks the shortcuts without other engines.");

  info("Remove search engines except default");
  const defaultEngine = SearchService.defaultEngine;
  const engines = await SearchService.getVisibleEngines();
  for (const engine of engines) {
    if (defaultEngine.name !== engine.name) {
      await SearchService.removeEngine(engine);
    }
  }

  info("Remove local engines");
  for (const { pref } of UrlbarUtils.LOCAL_SEARCH_MODES) {
    await SpecialPowers.pushPrefEnv({
      set: [[`browser.urlbar.${pref}`, false]],
    });
  }

  const url = getRootDirectory(gTestPath) + "add_search_engine_many.html";
  await BrowserTestUtils.withNewTab(url, async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
    });

    const shortcutButtons = UrlbarTestUtils.getOneOffSearchButtons(window);
    Assert.ok(shortcutButtons.container.hidden, "It should be hidden");
  });

  SearchService.restoreDefaultEngines();
});
