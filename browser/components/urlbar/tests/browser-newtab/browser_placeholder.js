/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// The newtab search bar takes an address as well as a search term, so its
// placeholder names the default engine the way the address bar's does.

"use strict";

const SEARCH_CONFIG = [{ identifier: "engine1" }, { identifier: "engine2" }];

add_setup(async function () {
  await SearchTestUtils.updateRemoteSettingsConfig(SEARCH_CONFIG);
});

/**
 * Waits for the newtab bar's placeholder and asserts what it is set from.
 *
 * @param {MozBrowser} browser
 * @param {object} expected
 *   The Fluent id and arguments expected, as `getAttributes` returns them.
 */
async function assertPlaceholder(browser, expected) {
  let l10n;
  await TestUtils.waitForCondition(async () => {
    l10n = await NewtabSearchbarTestUtils.getPlaceholderL10n(browser);
    return l10n.id == expected.id && l10n.args?.name == expected.args?.name;
  }, `waiting for the placeholder to come from ${expected.id}`);
  Assert.deepEqual(l10n, expected, "Expected placeholder");
}

add_task(async function namesTheDefaultEngine() {
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();

  await assertPlaceholder(tab.linkedBrowser, {
    id: "urlbar-placeholder-with-name",
    args: { name: "engine1" },
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function keywordDisabled() {
  // The bar searches whatever the pref says, so the placeholder still offers
  // both a search and an address.
  await SpecialPowers.pushPrefEnv({ set: [["keyword.enabled", false]] });
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();

  await assertPlaceholder(tab.linkedBrowser, {
    id: "urlbar-placeholder-with-name",
    args: { name: "engine1" },
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function nonConfigEngine() {
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();
  let configEngine = await SearchService.getDefault();

  // An engine an add-on installed can have a name too long for the
  // placeholder, so only a config engine's name goes in it.
  await SearchTestUtils.installSearchExtension({
    name: "addonEngine",
    search_url: "https://example.com/",
  });
  await SearchService.setDefault(
    SearchService.getEngineByName("addonEngine"),
    SearchService.CHANGE_REASON.UNKNOWN
  );
  await assertPlaceholder(tab.linkedBrowser, {
    id: "urlbar-placeholder",
    args: null,
  });

  await SearchService.setDefault(
    configEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );
  await assertPlaceholder(tab.linkedBrowser, {
    id: "urlbar-placeholder-with-name",
    args: { name: "engine1" },
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function stalePreSavedName() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.placeholderName", "engine2"]],
  });
  // The preloaded page may have read the pref before it changed.
  NewTabPagePreloading.removePreloadedBrowser(window);
  let tab = await NewtabSearchbarTestUtils.openNewTabPage();

  await assertPlaceholder(tab.linkedBrowser, {
    id: "urlbar-placeholder-with-name",
    args: { name: "engine1" },
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
