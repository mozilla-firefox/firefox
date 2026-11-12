/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test for bug 395161 that allows special searches that restrict results to
 * history/bookmark/tagged items and title/url matches.
 *
 * Test 485122 by making sure results don't have tags when restricting result
 * to just history either by default behavior or dynamic query restrict.
 */

testEngine_setup();

function setSuggestPrefsToFalse() {
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", false);
  Services.prefs.setBoolPref("browser.urlbar.suggest.bookmark", false);
}

const TRANSITION_TYPED = PlacesUtils.history.TRANSITION_TYPED;

add_task(async function test_special_searches() {
  // High weight - Typed visit.
  let uri1 = Services.io.newURI("http://url/");
  // Medium weight - Regular visit.
  let uri2 = Services.io.newURI("http://url/2");
  // Medium weight - Regular visit.
  let uri3 = Services.io.newURI("http://foo.bar/");
  // High weight - Typed visit.
  let uri4 = Services.io.newURI("http://foo.bar/2");
  // High weight - Bookmark.
  let uri5 = Services.io.newURI("http://url/star");
  // High weight - Bookmark, regular visit.
  let uri6 = Services.io.newURI("http://url/star/2");
  // High weight - Bookmark, no visit.
  let uri7 = Services.io.newURI("http://foo.bar/star");
  // High weight - Bookmark.
  let uri8 = Services.io.newURI("http://foo.bar/star/2");
  // High weight - Bookmark, no visit.
  let uri9 = Services.io.newURI("http://url/tag");
  // High weight - Bookmark, no visit.
  let uri10 = Services.io.newURI("http://url/tag/2");
  // High weight - Bookmark, typed visit.
  let uri11 = Services.io.newURI("http://foo.bar/tag");
  // High weight - Bookmark, 0 visits.
  let uri12 = Services.io.newURI("http://foo.bar/tag/2");

  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri12,
    title: "foo.bar",
    tags: ["foo.bar"],
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri11,
    title: "title",
    tags: ["foo.bar"],
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri10,
    title: "foo.bar",
    tags: ["foo.bar"],
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: uri9,
    title: "title",
    tags: ["foo.bar"],
  });
  await PlacesTestUtils.addBookmarkWithDetails({ uri: uri8, title: "foo.bar" });
  await PlacesTestUtils.addBookmarkWithDetails({ uri: uri7, title: "title" });
  await PlacesTestUtils.addBookmarkWithDetails({ uri: uri6, title: "foo.bar" });
  await PlacesTestUtils.addBookmarkWithDetails({ uri: uri5, title: "title" });

  await PlacesTestUtils.addVisits([
    { uri: uri11, title: "title", transition: TRANSITION_TYPED },
    { uri: uri6, title: "foo.bar" },
    { uri: uri4, title: "foo.bar", transition: TRANSITION_TYPED },
    { uri: uri3, title: "title" },
    { uri: uri2, title: "foo.bar" },
    { uri: uri1, title: "title", transition: TRANSITION_TYPED },
  ]);

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  // Order of frecency when not restricting, descending:
  // uri1
  // uri4
  // uri5
  // uri6
  // uri7
  // uri8
  // uri9
  // uri10
  // uri11
  // uri12
  // uri2
  // uri3
  // Because there are a lot of ties, the ordering is influenced by order in
  // which they were inserted.

  // Test restricting searches.

  info("History restrict");
  let context = createContext(UrlbarShared.RESTRICT_TOKENS.HISTORY, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri1.spec, title: "title" }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri11.spec, title: "title" }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });

  info("Star restrict");
  context = createContext(UrlbarShared.RESTRICT_TOKENS.BOOKMARK, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, { uri: uri5.spec, title: "title" }),
      makeBookmarkResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeBookmarkResult(context, { uri: uri7.spec, title: "title" }),
      makeBookmarkResult(context, { uri: uri8.spec, title: "foo.bar" }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
      }),
    ],
  });

  info("Tag restrict");
  context = createContext(UrlbarShared.RESTRICT_TOKENS.TAG, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
      }),
    ],
  });

  info("Special as first word");
  context = createContext(`${UrlbarShared.RESTRICT_TOKENS.HISTORY} foo bar`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        query: "foo bar",
        alias: UrlbarShared.RESTRICT_TOKENS.HISTORY,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri11.spec, title: "title" }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });

  info("Special as last word");
  context = createContext(`foo bar ${UrlbarShared.RESTRICT_TOKENS.HISTORY}`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri11.spec, title: "title" }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });

  // Test restricting and matching searches with a term.

  info(`foo ${UrlbarShared.RESTRICT_TOKENS.HISTORY} -> history`);
  context = createContext(`foo ${UrlbarShared.RESTRICT_TOKENS.HISTORY}`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri11.spec, title: "title" }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });

  info(`foo ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} -> is star`);
  context = createContext(`foo ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK}`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeBookmarkResult(context, { uri: uri7.spec, title: "title" }),
      makeBookmarkResult(context, { uri: uri8.spec, title: "foo.bar" }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
    ],
  });

  info(`foo ${UrlbarShared.RESTRICT_TOKENS.TAG} -> is tag`);
  context = createContext(`foo ${UrlbarShared.RESTRICT_TOKENS.TAG}`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
    ],
  });

  info(
    `conflict ${UrlbarShared.RESTRICT_TOKENS.HISTORY} ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} -> bookmark wins`
  );
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "http://bookmark.conflict.com/",
    title: `conflict ${UrlbarShared.RESTRICT_TOKENS.HISTORY}`,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  context = createContext(
    `conflict ${UrlbarShared.RESTRICT_TOKENS.HISTORY} ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK}`,
    { isPrivate: false }
  );
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: "http://bookmark.conflict.com/",
        title: `conflict ${UrlbarShared.RESTRICT_TOKENS.HISTORY}`,
      }),
    ],
  });

  info(
    `conflict ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} ${UrlbarShared.RESTRICT_TOKENS.TAG} -> tag wins`
  );
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "http://tag.conflict.com/",
    title: `conflict ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK}`,
    tags: ["one"],
  });
  await PlacesTestUtils.addBookmarkWithDetails({
    uri: "http://nontag.conflict.com/",
    title: `conflict ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK}`,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  context = createContext(
    `conflict ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} ${UrlbarShared.RESTRICT_TOKENS.TAG}`,
    { isPrivate: false }
  );
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, {
        uri: "http://tag.conflict.com/",
        title: `conflict ${UrlbarShared.RESTRICT_TOKENS.BOOKMARK}`,
      }),
    ],
  });

  // Disable autoFill for the next tests, see test_autoFill_default_behavior.js
  // for specific tests.
  Services.prefs.setBoolPref("browser.urlbar.autoFill", false);

  // Test default usage by setting certain browser.urlbar.suggest.* prefs
  info("foo -> default history");
  setSuggestPrefsToFalse();
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", true);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri11.spec, title: "title" }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });

  info("foo -> default history, is star");
  setSuggestPrefsToFalse();
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", true);
  Services.prefs.setBoolPref("browser.urlbar.suggest.bookmark", true);
  // The purpose of this test is to verify what is being sent by ProviderPlaces.
  // It will send 10 results, but the heuristic result pushes the last result
  // out of the panel. We set maxRichResults to a high value to test the full
  // output of ProviderPlaces.
  Services.prefs.setIntPref("browser.urlbar.maxRichResults", 20);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, { uri: uri4.spec, title: "foo.bar" }),
      makeBookmarkResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeBookmarkResult(context, { uri: uri7.spec, title: "title" }),
      makeBookmarkResult(context, { uri: uri8.spec, title: "foo.bar" }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
      makeVisitResult(context, { uri: uri2.spec, title: "foo.bar" }),
      makeVisitResult(context, { uri: uri3.spec, title: "title" }),
    ],
  });
  Services.prefs.clearUserPref("browser.urlbar.maxRichResults");

  info("foo -> is star");
  setSuggestPrefsToFalse();
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", false);
  Services.prefs.setBoolPref("browser.urlbar.suggest.bookmark", true);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeBookmarkResult(context, { uri: uri6.spec, title: "foo.bar" }),
      makeBookmarkResult(context, { uri: uri7.spec, title: "title" }),
      makeBookmarkResult(context, { uri: uri8.spec, title: "foo.bar" }),
      makeBookmarkResult(context, {
        uri: uri9.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri10.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri11.spec,
        title: "title",
        tags: ["foo.bar"],
      }),
      makeBookmarkResult(context, {
        uri: uri12.spec,
        title: "foo.bar",
        tags: ["foo.bar"],
      }),
    ],
  });

  await cleanupPlaces();
});
