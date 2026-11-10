/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the Unified Search Button with app provided engines.
 */

"use strict";

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", true]],
  });
});

add_task(async function test_search_mode_app_provided_engines() {
  let cleanup = await installPersistTestEngines();

  let switcher = gURLBar.querySelector(".searchmode-switcher");
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(switcher),
    `Wait until unified search button is visible`
  );

  info("Press on the example panel item to enter search mode");
  await UrlbarTestUtils.activateSearchModeSwitcherItem(
    window,
    "panel-item[data-engine-id=Example]"
  );

  info("Search mode has been changed");
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "Example",
    entry: "searchbutton",
    source: 3,
  });

  info("Press the close button and escape search mode");
  gURLBar.querySelector(".searchmode-switcher-close").click();
  await UrlbarTestUtils.assertSearchMode(window, null);

  cleanup();
  await resetApplicationProvidedEngines();
});
