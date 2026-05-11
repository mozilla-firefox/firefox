/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref(
    "browser.urlbar.autoFill.adaptiveHistory.enabled"
  );
  Services.prefs.clearUserPref(
    "browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold"
  );
  Services.prefs.clearUserPref(
    "browser.urlbar.autoFill.adaptiveHistory.useCountThreshold"
  );
});

Services.prefs.setBoolPref(
  "browser.urlbar.autoFill.adaptiveHistory.enabled",
  true
);
Services.prefs.setIntPref(
  "browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold",
  0
);
Services.prefs.setIntPref(
  "browser.urlbar.autoFill.adaptiveHistory.useCountThreshold",
  0
);

testEngine_setup();

// With history disabled, a bookmarked origin (e.g. https://example.com/) is
// autofilled. _matchKnownUrl runs the adaptive query first, but with no
// moz_inputhistory rows the query returns nothing. _getOriginQuery then
// routes through QUERY_ORIGIN_BOOKMARK because effectiveSources treats
// HISTORY as unavailable.
add_task(async function test_history_disabled_bookmarked_origin() {
  Services.prefs.setBoolPref("places.history.enabled", false);

  let url = "https://example.com/";
  await PlacesUtils.bookmarks.insert({
    url,
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });

  let context = createContext("exam", { isPrivate: false });
  await check_results({
    context,
    autofilled: "example.com/",
    completed: url,
    matches: [
      makeVisitResult(context, {
        uri: url,
        title: "https://example.com",
        heuristic: true,
      }),
    ],
  });

  Services.prefs.clearUserPref("places.history.enabled");
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_history_disabled_bookmarked_page_url() {
  Services.prefs.setBoolPref("places.history.enabled", false);

  let url = "https://example.com/path";
  let originUrl = "https://example.com/";
  await PlacesUtils.bookmarks.insert({
    title: "Sample Bookmark",
    url,
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });

  let context = createContext("exam", { isPrivate: false });
  await check_results({
    context,
    autofilled: "example.com/",
    completed: originUrl,
    matches: [
      makeVisitResult(context, {
        uri: originUrl,
        title: "https://example.com",
        heuristic: true,
      }),
      makeVisitResult(context, {
        uri: url,
        title: "Sample Bookmark",
        source: UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
      }),
    ],
  });

  Services.prefs.clearUserPref("places.history.enabled");
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
});
