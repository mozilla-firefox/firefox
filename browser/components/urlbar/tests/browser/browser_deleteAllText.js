/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// This test makes sure that deleting all text in the input doesn't mess up
// subsequent searches.

"use strict";

const TEST_TOP_SITE = "https://example.com";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.default.sites", TEST_TOP_SITE]],
  });

  await updateTopSites(sites => sites?.length == 1);
});

add_task(async function test() {
  await runTest();
  // Setting suggest.topsites to false disables the view's autoOpen behavior,
  // which changes this test's outcomes.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.suggest.topsites", false]],
  });
  info("Running the test with autoOpen disabled.");
  await runTest();
  await SpecialPowers.popPrefEnv();
});

async function runTest() {
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits([
    "http://example.com/",
    "http://mozilla.org/",
  ]);

  // Do an initial search for "x".
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "x",
    fireInputEvent: true,
  });
  await checkResults();

  await deleteInput();

  // Type "x".  A new search should start.  Don't use
  // promiseAutocompleteResultPopup, which has some logic that starts the search
  // manually in certain conditions.  We want to specifically check that the
  // input event causes UrlbarInput to start a new search on its own.  If it
  // doesn't, then the test will hang here on promiseSearchComplete.
  EventUtils.synthesizeKey("x");
  await UrlbarTestUtils.promiseSearchComplete(window);
  await checkResults();

  // Now repeat the backspace + x two more times.  Same thing should happen.
  for (let i = 0; i < 2; i++) {
    await deleteInput();
    EventUtils.synthesizeKey("x");
    await UrlbarTestUtils.promiseSearchComplete(window);
    await checkResults();
  }

  await deleteInput();
  // autoOpen opened the panel, so we need to close it.
  gURLBar.view.close();
}

async function checkResults() {
  Assert.equal(await UrlbarTestUtils.getResultCount(window), 2);
  let details = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(details.type, UrlbarUtils.RESULT_TYPE.SEARCH);
  Assert.equal(details.searchParams.query, "x");
  details = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  Assert.equal(details.type, UrlbarUtils.RESULT_TYPE.URL);
  Assert.equal(details.url, "http://example.com/");
}

async function deleteInput() {
  if (UrlbarPrefs.get("suggest.topsites")) {
    // The popup should remain open and show top sites.
    while (gURLBar.value.length) {
      EventUtils.synthesizeKey("KEY_Backspace");
    }
    Assert.ok(
      gURLBar.view.isOpen,
      "View should remain open when deleting all input text"
    );
    let queryContext = await UrlbarTestUtils.promiseSearchComplete(window);
    Assert.notEqual(
      queryContext.results.length,
      0,
      "View should show results when deleting all input text"
    );
    Assert.equal(
      queryContext.searchString,
      "",
      "Results should be for the empty search string (i.e. top sites) when deleting all input text"
    );
  } else {
    // Deleting all text should close the view.
    await UrlbarTestUtils.promisePopupClose(window, () => {
      while (gURLBar.value.length) {
        EventUtils.synthesizeKey("KEY_Backspace");
      }
    });
  }
}
