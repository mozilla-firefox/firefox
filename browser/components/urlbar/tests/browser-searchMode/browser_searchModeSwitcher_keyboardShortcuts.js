/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests for accel+arrow and alt+arrow shortcuts.

const CONFIG = [
  { identifier: "engine1" },
  { identifier: "engine2" },
  {
    identifier: "engine3",
    base: {
      urls: {
        search: {
          base: "https://example.com/engine3",
          searchTermParamName: "q",
        },
      },
    },
  },
];

add_setup(async function setup() {
  await SearchTestUtils.updateRemoteSettingsConfig(CONFIG);
  UrlbarTestUtils.registerZeroPrefixProvider();
});

add_task(async function open_with_alt_open_view() {
  info(
    "Open the urlbar and searchmode switcher popup with Arrow Down + Alt/Option keys while the results view is open"
  );
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowDown", { altKey: true });
  });

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeKey("KEY_Escape");
  await popupHidden;
});

add_task(async function open_with_alt_closed_view() {
  info(
    "Open the urlbar and searchmode switcher popup with Arrow Up + Alt/Option keys while the results view is closed"
  );
  await UrlbarTestUtils.openSearchModeSwitcher(window, () => {
    EventUtils.synthesizeKey("KEY_ArrowUp", { altKey: true });
  });

  let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
  EventUtils.synthesizeKey("KEY_Escape");
  await popupHidden;
});

add_task(async function change_engines_with_accel_updown() {
  info("Navigate engines with Accel+Up/Down");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  let entry = "searchbutton";
  let searchModes = [
    ...SearchService.visibleEngines.map(({ name }) => ({
      engineName: name,
      source: UrlbarShared.RESULT_SOURCE.SEARCH,
      entry,
    })),
    ...UrlbarShared.LOCAL_SEARCH_MODES.map(({ source }) => ({ source, entry })),
  ];

  info("Cycle forwards");
  for (let expectedMode of [...searchModes.slice(1), searchModes[0]]) {
    EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, window);
    await UrlbarTestUtils.assertSearchMode(window, expectedMode);
  }

  info("Cycle backwards");
  EventUtils.synthesizeKey("KEY_ArrowUp", { accelKey: true }, window);
  await UrlbarTestUtils.assertSearchMode(
    window,
    searchModes[searchModes.length - 1]
  );

  await UrlbarTestUtils.exitSearchMode(window);
});

add_task(async function search_engines_with_accel_updown() {
  info("Search engines with Accel+Up/Down");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test",
  });

  while (gURLBar.searchMode?.engineName != "engine3") {
    let searchmodeChanged = BrowserTestUtils.waitForEvent(
      gURLBar,
      "searchmodechanged"
    );
    EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, window);
    await searchmodeChanged;
  }

  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "engine3",
    source: UrlbarShared.RESULT_SOURCE.SEARCH,
    entry: "searchbutton",
  });

  let loaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    "https://example.com/engine3?q=test"
  );
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  await loaded;

  Assert.ok(true, "We navigated to the correct SERP");
});
