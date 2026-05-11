/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for engagement and abandonment telemetry when interacting with the
// search mode switcher.
// Opening the search mode switcher blurs the urlbar. To avoid recording an
// abandonment event, we discard the current event. This is not ideal and in the
// future, we would keep tracking the session even when the search mode switcher
// opens and this test should get replaced by telemetry_engagement_search_mode
// and telemetry_abandonment_search_mode which currently still test the legacy
// one-off buttons.

add_setup(async function () {
  await SearchTestUtils.updateRemoteSettingsConfig([{ identifier: "engine1" }]);
});

add_task(async function search_engine_serp() {
  await doTest(async () => {
    await openPopup("x");
    info("Show search mode switcher");
    let popup = await UrlbarTestUtils.openSearchModeSwitcher(window, () =>
      // The default open function calls searchModeSwitcher.focus()
      // first which would record an abandonment event.
      gURLBar.querySelector(".searchmode-switcher").click()
    );

    info("Press on the search engine to search for 'x'");
    let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
    popup.querySelector("panel-item").button.click();
    EventUtils.synthesizeKey("KEY_Enter");
    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    await popupHidden;

    // search mode is entered with a new query and a new session is started.
    assertEngagementTelemetry([{ search_mode: "search_engine" }]);
    await assertAbandonmentTelemetry([]);
  });
});

add_task(async function search_engine_searchmode() {
  await doTest(async () => {
    // Urlbar starts out empty.
    info("Show search mode switcher");
    let popup = await UrlbarTestUtils.openSearchModeSwitcher(window, () =>
      // The default open function calls searchModeSwitcher.focus()
      // first which would record an abandonment event.
      gURLBar.querySelector(".searchmode-switcher").click()
    );

    info("Press on the search engine to search for 'x'");
    let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
    popup.querySelector("panel-item").button.click();
    await popupHidden;

    EventUtils.synthesizeKey("x", {});
    await doEnter();

    // Since the query is an empty string, search mode is entered and
    // a new session is started. The first session should have been
    // discarded but the second one should be recorded as engagement.
    assertEngagementTelemetry([{ search_mode: "search_engine" }]);
    await assertAbandonmentTelemetry([]);
  });
});

add_task(async function bookmarks() {
  await doTest(async () => {
    await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      url: "https://example.com/bookmark",
      title: "bookmark",
    });
    await openPopup("bookmark");

    info("Show search mode switcher");
    let popup = await UrlbarTestUtils.openSearchModeSwitcher(window, () =>
      // The default open function calls searchModeSwitcher.focus()
      // first which would record an abandonment event.
      gURLBar.querySelector(".searchmode-switcher").click()
    );

    info("Press on the bookmarks panel item");
    let popupHidden = UrlbarTestUtils.searchModeSwitcherPopupClosed(window);
    popup.querySelector('panel-item[data-restrict="*"]').button.click();
    await popupHidden;

    await UrlbarTestUtils.promiseSearchComplete(window);
    await selectRowByURL("https://example.com/bookmark");

    await doEnter();
    assertEngagementTelemetry([{ search_mode: "bookmarks" }]);
    await assertAbandonmentTelemetry([]);
  });
});
