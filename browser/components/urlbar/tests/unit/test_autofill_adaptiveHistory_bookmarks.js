/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that when adaptive autofill is enabled, bookmarks no longer make a
// URL or origin an autofill candidate on their own. A bookmark with at least
// one typed visit can still autofill.

"use strict";

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref(
    "browser.urlbar.autoFill.adaptiveHistory.enabled"
  );
});
Services.prefs.setBoolPref(
  "browser.urlbar.autoFill.adaptiveHistory.enabled",
  true
);

testEngine_setup();

add_task(async function origin_unvisited_bookmark_blocked_when_adaptive_on() {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  let bookmark = await PlacesUtils.bookmarks.insert({
    url: "https://example.com/",
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });

  let context = createContext("exam", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SearchService.defaultEngine.name,
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: "https://example.com/",
        title: "",
      }),
    ],
  });

  await PlacesUtils.bookmarks.remove(bookmark);
});

add_task(async function origin_visited_bookmark_autofills_when_adaptive_on() {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  let url = "https://example.com/";
  let bookmark = await PlacesUtils.bookmarks.insert({
    url,
    title: "A bookmark",
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });
  await PlacesTestUtils.addVisits({
    uri: url,
    transition: PlacesUtils.history.TRANSITION_TYPED,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  for (let [search, autofilled] of [
    ["exam", "example.com/"],
    ["https://exam", "https://example.com/"],
  ]) {
    info("Searching for " + search);
    let context = createContext(search, { isPrivate: false });
    await check_results({
      context,
      autofilled,
      completed: url,
      matches: [
        makeVisitResult(context, {
          uri: url,
          title: "A bookmark",
          heuristic: true,
        }),
      ],
    });
  }

  await PlacesUtils.bookmarks.remove(bookmark);
  await PlacesUtils.history.clear();
});
