/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that search engine suggestions are returned by
 * UrlbarProviderSearchSuggestions.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const SUGGEST_PREF = "browser.urlbar.suggest.searches";
const SUGGEST_ENABLED_PREF = "browser.search.suggest.enabled";
const PRIVATE_ENABLED_PREF = "browser.search.suggest.enabled.private";
const PRIVATE_SEARCH_PREF = "browser.search.separatePrivateDefault.ui.enabled";
const TAB_TO_SEARCH_PREF = "browser.urlbar.suggest.engines";
const TRENDING_PREF = "browser.urlbar.trending.featureGate";
const QUICKACTIONS_PREF = "browser.urlbar.suggest.quickactions";
const MAX_RICH_RESULTS_PREF = "browser.urlbar.maxRichResults";
const MAX_FORM_HISTORY_PREF = "browser.urlbar.maxHistoricalSearchSuggestions";
const SHOW_SEARCH_SUGGESTIONS_FIRST_PREF =
  "browser.urlbar.showSearchSuggestionsFirst";
const SEARCH_STRING = "hello";

const MAX_RESULTS = Services.prefs.getIntPref(MAX_RICH_RESULTS_PREF, 10);

var suggestionsFn;
var previousSuggestionsFn;
let port;
let sandbox;

/**
 * Set the current suggestion funciton.
 *
 * @param {Function} fn
 *   A function that that a search string and returns an array of strings that
 *   will be used as search suggestions.
 *   Note: `fn` should return > 0 suggestions in most cases. Otherwise, you may
 *         encounter unexpected behaviour with UrlbarProviderSuggestion's
 *         _lastLowResultsSearchSuggestion safeguard.
 */
function setSuggestionsFn(fn) {
  previousSuggestionsFn = suggestionsFn;
  suggestionsFn = fn;
}

async function cleanup() {
  Services.prefs.clearUserPref("browser.urlbar.autoFill");
  Services.prefs.clearUserPref(SUGGEST_PREF);
  Services.prefs.clearUserPref(SUGGEST_ENABLED_PREF);
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  sandbox.restore();
}

async function cleanUpSuggestions() {
  await cleanup();
  if (previousSuggestionsFn) {
    suggestionsFn = previousSuggestionsFn;
    previousSuggestionsFn = null;
  }
}

function makeFormHistoryResults(
  context,
  count,
  engineName = SUGGESTIONS_ENGINE_NAME
) {
  let results = [];
  for (let i = 0; i < count; i++) {
    results.push(
      makeFormHistoryResult(context, {
        suggestion: `${SEARCH_STRING} world Form History ${i}`,
        engineName,
      })
    );
  }
  return results;
}

function makeRemoteSuggestionResults(
  context,
  { suggestionPrefix = SEARCH_STRING, query = undefined } = {}
) {
  // The suggestions function in `setup` returns:
  // [searchString, searchString + "foo", searchString + "bar"]
  // But when the heuristic is a search result, the muxer discards suggestion
  // results that match the search string, and therefore we expect only two
  // remote suggestion results, the "foo" and "bar" ones.
  return [
    makeSearchResult(context, {
      query,
      engineName: SUGGESTIONS_ENGINE_NAME,
      suggestion: suggestionPrefix + " foo",
    }),
    makeSearchResult(context, {
      query,
      engineName: SUGGESTIONS_ENGINE_NAME,
      suggestion: suggestionPrefix + " bar",
    }),
  ];
}

function setResultGroups(groups) {
  sandbox.restore();
  sandbox.stub(UrlbarPrefs, "getResultGroups").returns({
    children: [
      // heuristic
      {
        maxResultCount: 1,
        children: [
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_TEST },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_EXTENSION },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_SEARCH_TIP },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_OMNIBOX },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_AUTOFILL },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE },
          { group: UrlbarUtils.RESULT_GROUP.HEURISTIC_FALLBACK },
        ],
      },
      // extensions using the omnibox API
      {
        group: UrlbarUtils.RESULT_GROUP.OMNIBOX,
      },
      ...groups,
    ],
  });
}

add_setup(async function () {
  sandbox = lazy.sinon.createSandbox();

  let engine = await addTestSuggestionsEngine(searchStr => {
    return suggestionsFn(searchStr);
  });
  port = engine.getSubmission("").uri.port;

  setSuggestionsFn(searchStr => {
    let suffixes = ["foo", "bar"];
    return [searchStr].concat(suffixes.map(s => searchStr + " " + s));
  });

  // Install the test engine.
  let oldDefaultEngine = await SearchService.getDefault();
  registerCleanupFunction(async () => {
    SearchService.setDefault(
      oldDefaultEngine,
      SearchService.CHANGE_REASON.UNKNOWN
    );
    Services.prefs.clearUserPref(PRIVATE_SEARCH_PREF);
    Services.prefs.clearUserPref(TRENDING_PREF);
    Services.prefs.clearUserPref(QUICKACTIONS_PREF);
    Services.prefs.clearUserPref(TAB_TO_SEARCH_PREF);
    sandbox.restore();
  });
  SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
  Services.prefs.setBoolPref(PRIVATE_SEARCH_PREF, false);
  Services.prefs.setBoolPref(TRENDING_PREF, false);
  Services.prefs.setBoolPref(QUICKACTIONS_PREF, false);
  // Tab-to-search engines can introduce unexpected results, espescially because
  // they depend on real en-US engines.
  Services.prefs.setBoolPref(TAB_TO_SEARCH_PREF, false);

  // Add MAX_RESULTS form history.
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  let entries = makeFormHistoryResults(context, MAX_RESULTS).map(r => ({
    value: r.payload.suggestion,
    source: SUGGESTIONS_ENGINE_NAME,
  }));
  await UrlbarTestUtils.formHistory.add(entries);
});

add_task(async function disabled_urlbarSuggestions() {
  Services.prefs.setBoolPref(SUGGEST_PREF, false);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });
  await cleanUpSuggestions();
});

add_task(async function disabled_allSuggestions() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, false);
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });
  await cleanUpSuggestions();
});

// The searchbar shows form history even when remote suggestions are disabled.
add_task(async function disabled_allSuggestions_searchbar() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, false);

  for (let isPrivate of [false, true]) {
    let context = createContext(SEARCH_STRING, {
      isPrivate,
      sapName: "searchbar",
    });
    await check_results({
      context,
      matches: [
        makeSearchResult(context, {
          engineName: SUGGESTIONS_ENGINE_NAME,
          heuristic: true,
        }),
        ...makeFormHistoryResults(context, MAX_RESULTS - 1),
      ],
    });
  }

  await cleanUpSuggestions();
});

add_task(async function disabled_privateWindow() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  Services.prefs.setBoolPref(PRIVATE_ENABLED_PREF, false);
  let context = createContext(SEARCH_STRING, { isPrivate: true });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });
  await cleanUpSuggestions();
});

add_task(async function disabled_urlbarSuggestions_withRestrictionToken() {
  Services.prefs.setBoolPref(SUGGEST_PREF, false);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  let context = createContext(
    `${UrlbarShared.RESTRICT_TOKENS.SEARCH} ${SEARCH_STRING}`,
    { isPrivate: false }
  );
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        query: SEARCH_STRING,
        alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, {
        query: SEARCH_STRING,
      }),
    ],
  });
  await cleanUpSuggestions();
});

add_task(
  async function disabled_urlbarSuggestions_withRestrictionToken_private() {
    Services.prefs.setBoolPref(SUGGEST_PREF, false);
    Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
    Services.prefs.setBoolPref(PRIVATE_ENABLED_PREF, false);
    let context = createContext(
      `${UrlbarShared.RESTRICT_TOKENS.SEARCH} ${SEARCH_STRING}`,
      { isPrivate: true }
    );
    await check_results({
      context,
      matches: [
        makeSearchResult(context, {
          query: SEARCH_STRING,
          alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
          engineName: SUGGESTIONS_ENGINE_NAME,
          heuristic: true,
        }),
      ],
    });
    await cleanUpSuggestions();
  }
);

add_task(
  async function disabled_urlbarSuggestions_withRestrictionToken_private_enabled() {
    Services.prefs.setBoolPref(SUGGEST_PREF, false);
    Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
    Services.prefs.setBoolPref(PRIVATE_ENABLED_PREF, true);
    let context = createContext(
      `${UrlbarShared.RESTRICT_TOKENS.SEARCH} ${SEARCH_STRING}`,
      { isPrivate: true }
    );
    await check_results({
      context,
      matches: [
        makeSearchResult(context, {
          query: SEARCH_STRING,
          alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
          engineName: SUGGESTIONS_ENGINE_NAME,
          heuristic: true,
        }),
        ...makeFormHistoryResults(context, MAX_RESULTS - 3),
        ...makeRemoteSuggestionResults(context, {
          query: SEARCH_STRING,
        }),
      ],
    });
    await cleanUpSuggestions();
  }
);

add_task(async function enabled_by_pref_privateWindow() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  Services.prefs.setBoolPref(PRIVATE_ENABLED_PREF, true);
  let context = createContext(SEARCH_STRING, { isPrivate: true });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context),
    ],
  });
  await cleanUpSuggestions();

  Services.prefs.clearUserPref(PRIVATE_ENABLED_PREF);
});

add_task(async function singleWordQuery() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  let context = createContext(SEARCH_STRING, { isPrivate: false });

  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function multiWordQuery() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  const query = `${SEARCH_STRING} world`;
  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: query,
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function suffixMatch() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  setSuggestionsFn(searchStr => {
    let prefixes = ["baz", "quux"];
    return prefixes.map(p => p + " " + searchStr);
  });

  let context = createContext(SEARCH_STRING, { isPrivate: false });

  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "baz " + SEARCH_STRING,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "quux " + SEARCH_STRING,
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function remoteSuggestionsDupeSearchString() {
  Services.prefs.setIntPref(MAX_FORM_HISTORY_PREF, 0);

  // Return remote suggestions with the trimmed search string, the uppercased
  // search string, and the search string with a trailing space, plus the usual
  // "foo" and "bar" suggestions.
  setSuggestionsFn(searchStr => {
    let suffixes = ["foo", "bar"];
    return [searchStr.trim(), searchStr.toUpperCase(), searchStr + " "].concat(
      suffixes.map(s => searchStr + " " + s)
    );
  });

  // Do a search with a trailing space.  All the variations of the search string
  // with regard to spaces and case should be discarded from the remote
  // suggestions, leaving only the usual "foo" and "bar" suggestions.
  let query = SEARCH_STRING + " ";
  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        query,
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  await cleanUpSuggestions();
  Services.prefs.clearUserPref(MAX_FORM_HISTORY_PREF);
});

add_task(async function queryIsNotASubstring() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);

  setSuggestionsFn(() => {
    return ["aaa", "bbb"];
  });

  let context = createContext(SEARCH_STRING, { isPrivate: false });

  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "aaa",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "bbb",
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function restrictToken() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  // Add a visit and a bookmark.  Actually, make the bookmark visited too so
  // that it's guaranteed, with its higher frecency, to appear above the search
  // suggestions.
  await PlacesTestUtils.addVisits([
    {
      uri: Services.io.newURI(`http://example.com/${SEARCH_STRING}-visit`),
      title: `${SEARCH_STRING} visit`,
    },
    {
      uri: Services.io.newURI(`http://example.com/${SEARCH_STRING}-bookmark`),
      title: `${SEARCH_STRING} bookmark`,
    },
  ]);

  await PlacesTestUtils.addBookmarkWithDetails({
    uri: Services.io.newURI(`http://example.com/${SEARCH_STRING}-bookmark`),
    title: `${SEARCH_STRING} bookmark`,
  });

  let context = createContext(SEARCH_STRING, { isPrivate: false });

  // Do an unrestricted search to make sure everything appears in it, including
  // the visit and bookmark.
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 5),
      ...makeRemoteSuggestionResults(context),
      makeBookmarkResult(context, {
        uri: `http://example.com/${SEARCH_STRING}-bookmark`,
        title: `${SEARCH_STRING} bookmark`,
      }),
      makeVisitResult(context, {
        uri: `http://example.com/${SEARCH_STRING}-visit`,
        title: `${SEARCH_STRING} visit`,
      }),
    ],
  });

  // Now do a restricted search to make sure only suggestions appear.
  context = createContext(
    `${UrlbarShared.RESTRICT_TOKENS.SEARCH} ${SEARCH_STRING}`,
    { isPrivate: false }
  );
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
        query: SEARCH_STRING,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: SEARCH_STRING,
        query: SEARCH_STRING,
      }),
    ],
  });

  // Typing the search restriction char shows the Search Engine entry and local
  // results.
  context = createContext(UrlbarShared.RESTRICT_TOKENS.SEARCH, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        query: "",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 1),
    ],
  });

  // Also if followed by multiple spaces.
  context = createContext(`${UrlbarShared.RESTRICT_TOKENS.SEARCH}  `, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
        query: "",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 1),
    ],
  });

  // If followed by any char we should fetch suggestions.
  // Note this uses "h" to match form history.
  context = createContext(`${UrlbarShared.RESTRICT_TOKENS.SEARCH}h`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        query: "h",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "h",
        query: "h",
      }),
    ],
  });

  // Also if followed by a space and single char.
  context = createContext(`${UrlbarShared.RESTRICT_TOKENS.SEARCH} h`, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        alias: UrlbarShared.RESTRICT_TOKENS.SEARCH,
        query: "h",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "h",
        query: "h",
      }),
    ],
  });

  // Leading search-mode restriction tokens are removed.
  context = createContext(
    `${UrlbarShared.RESTRICT_TOKENS.BOOKMARK} ${SEARCH_STRING}`,
    { isPrivate: false }
  );
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        heuristic: true,
        query: SEARCH_STRING,
        alias: UrlbarShared.RESTRICT_TOKENS.BOOKMARK,
      }),
      makeBookmarkResult(context, {
        uri: `http://example.com/${SEARCH_STRING}-bookmark`,
        title: `${SEARCH_STRING} bookmark`,
      }),
    ],
  });

  // Non-search-mode restriction tokens remain in the query and heuristic search
  // result.
  let token;
  for (let t of Object.values(UrlbarShared.RESTRICT_TOKENS)) {
    if (!UrlbarShared.SEARCH_MODE_RESTRICT.has(t)) {
      token = t;
      break;
    }
  }
  Assert.ok(
    token,
    "Non-search-mode restrict token exists -- if not, you can probably remove me!"
  );
  context = createContext(token, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function mixup_frecency() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);

  // At most, we should have 22 results in this subtest. We set this to 30 to
  // make we're not cutting off any results and we are actually getting 22.
  Services.prefs.setIntPref(MAX_RICH_RESULTS_PREF, 30);

  // Add a visit and a bookmark.  Actually, make the bookmark visited too so
  // that it's guaranteed, with its higher frecency, to appear above the search
  // suggestions.
  await PlacesTestUtils.addVisits([
    {
      uri: Services.io.newURI("http://example.com/lo0"),
      title: `${SEARCH_STRING} low frecency 0`,
    },
    {
      uri: Services.io.newURI("http://example.com/lo1"),
      title: `${SEARCH_STRING} low frecency 1`,
    },
    {
      uri: Services.io.newURI("http://example.com/lo2"),
      title: `${SEARCH_STRING} low frecency 2`,
    },
    {
      uri: Services.io.newURI("http://example.com/lo3"),
      title: `${SEARCH_STRING} low frecency 3`,
    },
    {
      uri: Services.io.newURI("http://example.com/lo4"),
      title: `${SEARCH_STRING} low frecency 4`,
    },
  ]);

  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits([
      {
        uri: Services.io.newURI("http://example.com/hi0"),
        title: `${SEARCH_STRING} high frecency 0`,
        transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
      },
      {
        uri: Services.io.newURI("http://example.com/hi1"),
        title: `${SEARCH_STRING} high frecency 1`,
        transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
      },
      {
        uri: Services.io.newURI("http://example.com/hi2"),
        title: `${SEARCH_STRING} high frecency 2`,
        transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
      },
      {
        uri: Services.io.newURI("http://example.com/hi3"),
        title: `${SEARCH_STRING} high frecency 3`,
        transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
      },
    ]);
  }

  for (let i = 0; i < 4; i++) {
    let href = `http://example.com/hi${i}`;
    await PlacesTestUtils.addBookmarkWithDetails({
      uri: href,
      title: `${SEARCH_STRING} high frecency ${i}`,
    });
  }

  // Do an unrestricted search to make sure everything appears in it, including
  // the visit and bookmark.
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS),
      ...makeRemoteSuggestionResults(context),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi3",
        title: `${SEARCH_STRING} high frecency 3`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi2",
        title: `${SEARCH_STRING} high frecency 2`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi1",
        title: `${SEARCH_STRING} high frecency 1`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi0",
        title: `${SEARCH_STRING} high frecency 0`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo4",
        title: `${SEARCH_STRING} low frecency 4`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo3",
        title: `${SEARCH_STRING} low frecency 3`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo2",
        title: `${SEARCH_STRING} low frecency 2`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo1",
        title: `${SEARCH_STRING} low frecency 1`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo0",
        title: `${SEARCH_STRING} low frecency 0`,
      }),
    ],
  });

  // Change the mixup.
  setResultGroups([
    // 1 suggestion
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarUtils.RESULT_GROUP.FORM_HISTORY },
        { group: UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION },
      ],
    },
    // 5 general
    {
      maxResultCount: 5,
      group: UrlbarUtils.RESULT_GROUP.GENERAL,
    },
    // 1 suggestion
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarUtils.RESULT_GROUP.FORM_HISTORY },
        { group: UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION },
      ],
    },
    // remaining general
    { group: UrlbarUtils.RESULT_GROUP.GENERAL },
    // remaining suggestions
    { group: UrlbarUtils.RESULT_GROUP.FORM_HISTORY },
    { group: UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION },
  ]);

  // Do an unrestricted search to make sure everything appears in it, including
  // the visits and bookmarks.
  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, 1),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi3",
        title: `${SEARCH_STRING} high frecency 3`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi2",
        title: `${SEARCH_STRING} high frecency 2`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi1",
        title: `${SEARCH_STRING} high frecency 1`,
      }),
      makeBookmarkResult(context, {
        uri: "http://example.com/hi0",
        title: `${SEARCH_STRING} high frecency 0`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo4",
        title: `${SEARCH_STRING} low frecency 4`,
      }),
      ...makeFormHistoryResults(context, 2).slice(1),
      makeVisitResult(context, {
        uri: "http://example.com/lo3",
        title: `${SEARCH_STRING} low frecency 3`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo2",
        title: `${SEARCH_STRING} low frecency 2`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo1",
        title: `${SEARCH_STRING} low frecency 1`,
      }),
      makeVisitResult(context, {
        uri: "http://example.com/lo0",
        title: `${SEARCH_STRING} low frecency 0`,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS).slice(2),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  Services.prefs.clearUserPref(MAX_RICH_RESULTS_PREF);
  await cleanUpSuggestions();
});

add_task(async function prohibit_suggestions() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(
    `browser.fixup.domainwhitelist.${SEARCH_STRING}`,
    false
  );

  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  Services.prefs.setBoolPref(
    `browser.fixup.domainwhitelist.${SEARCH_STRING}`,
    true
  );
  registerCleanupFunction(() => {
    Services.prefs.setBoolPref(
      `browser.fixup.domainwhitelist.${SEARCH_STRING}`,
      false
    );
  });
  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${SEARCH_STRING}/`,
        title: `${SEARCH_STRING}/`,
        iconUri: "",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 2),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: false,
      }),
    ],
  });

  // When using multiple words, we should still get suggestions:
  let query = `${SEARCH_STRING} world`;
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, { suggestionPrefix: query }),
    ],
  });

  // Clear the whitelist for SEARCH_STRING and try preferring DNS for any single
  // word instead:
  Services.prefs.setBoolPref(
    `browser.fixup.domainwhitelist.${SEARCH_STRING}`,
    false
  );
  Services.prefs.setBoolPref("browser.fixup.dns_first_for_single_words", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.fixup.dns_first_for_single_words");
  });

  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${SEARCH_STRING}/`,
        title: `${SEARCH_STRING}/`,
        iconUri: "",
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 2),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: false,
      }),
    ],
  });

  context = createContext("somethingelse", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://somethingelse/",
        title: "somethingelse/",
        iconUri: "",
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: false,
      }),
    ],
  });

  // When using multiple words, we should still get suggestions:
  query = `${SEARCH_STRING} world`;
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context, { suggestionPrefix: query }),
    ],
  });

  Services.prefs.clearUserPref("browser.fixup.dns_first_for_single_words");

  context = createContext("http://1.2.3.4/", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://1.2.3.4/",
        title: "http://1.2.3.4/",
        iconUri: "page-icon:http://1.2.3.4/",
        heuristic: true,
      }),
    ],
  });

  context = createContext("[2001::1]:30", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://[2001::1]:30/",
        title: "[2001::1]:30/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("user:pass@test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://user:pass@test/",
        title: "user:pass@test/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("user:pass@mozilla.org", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://user:pass@mozilla.org/",
        title: "user:pass@mozilla.org/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("mozilla.org:1234", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://mozilla.org:1234/",
        title: "mozilla.org:1234/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("data:text/plain,Content", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "data:text/plain,Content",
        title: "data:text/plain,Content",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("a", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function simple_origin_queries() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  // Queries that are actual simple origins should return search suggestions
  // only when `allowSearchSuggestionsForSimpleOrigins` is true.
  let queries = ["mozilla.org", "example.com", "example.co", "readme.md"];

  for (let allow of [false, true]) {
    Services.prefs.setBoolPref(
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
      allow
    );

    for (let query of queries) {
      info("Testing: " + JSON.stringify({ allow, query }));

      let context = createContext(query, { isPrivate: false });
      let expected = [
        makeVisitResult(context, {
          source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
          title: `${query}/`,
          uri: `http://${query}/`,
          iconUri: "",
          heuristic: true,
        }),
      ];
      if (allow) {
        expected.push(
          ...makeRemoteSuggestionResults(context, { suggestionPrefix: query })
        );
      }
      expected.push(
        makeSearchResult(context, {
          query,
          engineName: SUGGESTIONS_ENGINE_NAME,
        })
      );

      await check_results({
        context,
        matches: expected,
      });
    }
  }

  await cleanUpSuggestions();
  Services.prefs.clearUserPref(
    "browser.urlbar.allowSearchSuggestionsForSimpleOrigins"
  );
});

add_task(async function simple_origin_like_queries() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  // Queries that aren't simple origins but look like simple origins should
  // return search suggestions only when
  // `allowSearchSuggestionsForSimpleOrigins` is true.
  let queries = ["mozilla.o", "mozilla."];

  for (let allow of [false, true]) {
    Services.prefs.setBoolPref(
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
      allow
    );

    for (let query of queries) {
      info("Testing: " + JSON.stringify({ allow, query }));

      let context = createContext(query, { isPrivate: false });
      let expected = [
        makeSearchResult(context, {
          engineName: SUGGESTIONS_ENGINE_NAME,
          heuristic: true,
        }),
      ];
      if (allow) {
        expected.push(
          ...makeRemoteSuggestionResults(context, { suggestionPrefix: query })
        );
      }

      await check_results({
        context,
        matches: expected,
      });
    }
  }

  await cleanUpSuggestions();
  Services.prefs.clearUserPref(
    "browser.urlbar.allowSearchSuggestionsForSimpleOrigins"
  );
});

add_task(async function uri_like_queries() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  // Queries that don't look like origins but that might be confused for URLs
  // should always return search suggestions regardless of
  // `allowSearchSuggestionsForSimpleOrigins`.
  const uriLikeQueries = [
    "mozilla.org is a great website",
    "I like mozilla.org",
    "a/b testing",
    "he/him",
    "Google vs.",
    "5.8 cm",
  ];

  for (let allow of [false, true]) {
    Services.prefs.setBoolPref(
      "browser.urlbar.allowSearchSuggestionsForSimpleOrigins",
      allow
    );

    for (let query of uriLikeQueries) {
      info("Testing: " + JSON.stringify({ allow, query }));

      let context = createContext(query, { isPrivate: false });
      await check_results({
        context,
        matches: [
          makeSearchResult(context, {
            engineName: SUGGESTIONS_ENGINE_NAME,
            heuristic: true,
          }),
          ...makeRemoteSuggestionResults(context, {
            suggestionPrefix: query,
          }),
        ],
      });
    }
  }

  await cleanUpSuggestions();
  Services.prefs.clearUserPref(
    "browser.urlbar.allowSearchSuggestionsForSimpleOrigins"
  );
});

add_task(async function avoid_remote_url_suggestions_1() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  setSuggestionsFn(searchStr => {
    let suffixes = [".com", "/test", ":1]", "@test", ". com"];
    return suffixes.map(s => searchStr + s);
  });

  const query = "test";

  await UrlbarTestUtils.formHistory.add([`${query}.com`]);

  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: `${query}.com`,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: `${query}. com`,
      }),
    ],
  });

  await cleanUpSuggestions();
  await UrlbarTestUtils.formHistory.remove([`${query}.com`]);
});

add_task(async function avoid_remote_url_suggestions_2() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref("browser.urlbar.autoFill", false);

  setSuggestionsFn(searchStr => {
    let suffixes = ["ed", "eds"];
    return suffixes.map(s => searchStr + s);
  });

  let context = createContext("htt", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "htted",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "htteds",
      }),
    ],
  });

  context = createContext("ftp", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "ftped",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "ftpeds",
      }),
    ],
  });

  context = createContext("http", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httped",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httpeds",
      }),
    ],
  });

  context = createContext("http:", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("https", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httpsed",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httpseds",
      }),
    ],
  });

  context = createContext("https:", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("httpd", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httpded",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "httpdeds",
      }),
    ],
  });

  // Check FTP disabled
  context = createContext("ftp:", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("ftp:/", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("ftp://", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("ftp://test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "ftp://test/",
        title: "ftp://test/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("http:/", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("https:/", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("http://", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("https://", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("http://www", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://www/",
        title: "http://www/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("https://www", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "https://www/",
        title: "https://www/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("http://test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://test/",
        title: "http://test/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("https://test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "https://test/",
        title: "https://test/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("http://www.test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://www.test/",
        title: "http://www.test/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("http://www.test.com", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://www.test.com/",
        title: "http://www.test.com/",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("file", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "fileed",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "fileeds",
      }),
    ],
  });

  context = createContext("file:", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("file:///Users", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        uri: "file:///Users",
        title: "file:///Users",
        iconUri: "",
        heuristic: true,
      }),
    ],
  });

  context = createContext("moz-test://", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("moz+test://", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  context = createContext("about", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "abouted",
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: "abouteds",
      }),
    ],
  });

  await cleanUpSuggestions();
});

add_task(async function restrict_remote_suggestions_after_no_results() {
  // We don't fetch remote suggestions if a query with a length over
  // maxCharsForSearchSuggestions returns 0 results. We set it to 4 here to
  // avoid constructing a 100+ character string.
  Services.prefs.setIntPref("browser.urlbar.maxCharsForSearchSuggestions", 4);
  setSuggestionsFn(() => {
    return [];
  });

  const query = SEARCH_STRING.substring(0, SEARCH_STRING.length - 1);
  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 1),
    ],
  });

  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 1),
      // Because the previous search returned no suggestions, we will not fetch
      // remote suggestions for this query that is just a longer version of the
      // previous query.
    ],
  });

  // Do one more search before resetting maxCharsForSearchSuggestions to reset
  // the search suggestion provider's _lastLowResultsSearchSuggestion property.
  // Otherwise it will be stuck at SEARCH_STRING, which interferes with
  // subsequent tests.
  context = createContext("not the search string", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  Services.prefs.clearUserPref("browser.urlbar.maxCharsForSearchSuggestions");

  await cleanUpSuggestions();
});

add_task(async function formHistory() {
  Services.prefs.setBoolPref(SUGGEST_PREF, true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);

  // `maxHistoricalSearchSuggestions` is no longer treated as a max count but as
  // a boolean: If it's zero, then the user has opted out of form history so we
  // shouldn't include any at all; if it's non-zero, then we include form
  // history according to the limits specified in the muxer's result groups.

  // zero => no form history
  Services.prefs.setIntPref(MAX_FORM_HISTORY_PREF, 0);
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  // non-zero => allow form history
  Services.prefs.setIntPref(MAX_FORM_HISTORY_PREF, 1);
  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  // non-zero => allow form history
  Services.prefs.setIntPref(MAX_FORM_HISTORY_PREF, 2);
  context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      ...makeRemoteSuggestionResults(context),
    ],
  });

  Services.prefs.clearUserPref(MAX_FORM_HISTORY_PREF);

  // Do a search for exactly the suggestion of the first form history result.
  // The heuristic's query should be the suggestion; the first form history
  // result should not be included since it dupes the heuristic; the other form
  // history results should not be included since they don't match; and both
  // remote suggestions should be included.
  let firstSuggestion = makeFormHistoryResults(context, 1)[0].payload
    .suggestion;
  context = createContext(firstSuggestion, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: firstSuggestion,
      }),
    ],
  });

  // Do the same search but in uppercase with a trailing space.  We should get
  // the same results, i.e., the form history result dupes the trimmed search
  // string so it shouldn't be included.
  let query = firstSuggestion.toUpperCase() + " ";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        query,
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: firstSuggestion.toUpperCase(),
      }),
    ],
  });

  // Add a form history entry that dupes the first remote suggestion and do a
  // search that triggers both.  The form history should be included but the
  // remote suggestion should not since it dupes the form history.
  let suggestionPrefix = "dupe";
  let dupeSuggestion = makeRemoteSuggestionResults(context, {
    suggestionPrefix,
  })[0].payload.suggestion;
  Assert.ok(dupeSuggestion, "Sanity check: dupeSuggestion is defined");
  await UrlbarTestUtils.formHistory.add([dupeSuggestion]);

  context = createContext(suggestionPrefix, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: dupeSuggestion,
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, { suggestionPrefix }).slice(1),
    ],
  });

  await UrlbarTestUtils.formHistory.remove([dupeSuggestion]);

  // Add these form history strings to use below.
  let formHistoryStrings = ["foo", "FOO ", "foobar", "fooquux"];
  await UrlbarTestUtils.formHistory.add(formHistoryStrings);

  // Search for "foo".  "foo" and "FOO " shouldn't be included since they dupe
  // the heuristic.  Both "foobar" and "fooquux" should be included even though
  // the max form history count is only two and there are four matching form
  // history results (including the discarded "foo" and "FOO ").
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
    ],
  });

  // Add a visit that matches "foo" and will autofill so that the heuristic is
  // not a search result.  Now the "foo" and "foobar" form history should be
  // included.  The "foo" remote suggestion should not be included since it
  // dupes the "foo" form history.
  await PlacesTestUtils.addVisits({
    url: "http://foo.example.com/",
    transition: PlacesUtils.history.TRANSITION_TYPED,
  });
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarUtils.RESULT_SOURCE.HISTORY,
        uri: "http://foo.example.com/",
        title: "test visit for http://foo.example.com/",
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foo",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
    ],
  });
  await PlacesUtils.history.clear();

  // Add SERPs for "foobar", "fooBAR ", and "food", and search for "foo".  The
  // "foo" form history should be excluded since it dupes the heuristic; the
  // "foobar" and "fooquux" form history should be included; the "food" SERP
  // should be included since it doesn't dupe either form history result; and
  // the "foobar" and "fooBAR " SERPs depend on the result groups, see below.
  let engine = await SearchService.getDefault();
  let serpURLs = ["foobar", "fooBAR ", "food"].map(
    term => UrlbarUtils.getSearchQueryUrl(engine, term)[0]
  );
  await PlacesTestUtils.addVisits(serpURLs);

  // First set showSearchSuggestionsFirst = false so that general results appear
  // before suggestions, which means that the muxer visits the "foobar" and
  // "fooBAR " SERPs before visiting the "foobar" form history, and so it
  // doesn't see that these two SERPs dupe the form history.  They are therefore
  // included.
  Services.prefs.setBoolPref(SHOW_SEARCH_SUGGESTIONS_FIRST_PREF, false);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=food`,
        title: `test visit for http://localhost:${port}/search?q=food`,
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=fooBAR+`,
        title: `test visit for http://localhost:${port}/search?q=fooBAR+`,
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=foobar`,
        title: `test visit for http://localhost:${port}/search?q=foobar`,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
    ],
  });

  // Now clear showSearchSuggestionsFirst so that suggestions appear before
  // general results.  Now the muxer will see that the "foobar" and "fooBAR "
  // SERPs dupe the "foobar" form history, so it will exclude them.
  Services.prefs.clearUserPref(SHOW_SEARCH_SUGGESTIONS_FIRST_PREF);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=food`,
        title: `test visit for http://localhost:${port}/search?q=food`,
      }),
    ],
  });

  await UrlbarTestUtils.formHistory.remove(formHistoryStrings);
});

add_task(async function formHistoryRestrictToEngine() {
  let engineName = "engine123";
  // The extension will be cleaned up automatically.
  await SearchTestUtils.installSearchExtension({ name: engineName });

  info("Shouldn't restrict form history to search mode engine on searchbar");
  let context = createContext(SEARCH_STRING, {
    isPrivate: false,
    searchMode: { engineName },
    sapName: "searchbar",
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 1, engineName),
    ],
  });

  info("Should restrict form history to search mode engine on urlbar");
  context = createContext(SEARCH_STRING, {
    isPrivate: false,
    searchMode: { engineName },
    sapName: "urlbar",
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName,
        heuristic: true,
      }),
    ],
  });

  await cleanUpSuggestions();
  await PlacesUtils.history.clear();
});

// When the heuristic is hidden, search results that match the heuristic should
// be included and not deduped.
add_task(async function hideHeuristic() {
  UrlbarPrefs.set("experimental.hideHeuristic", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      ...makeFormHistoryResults(context, MAX_RESULTS - 3),
      makeSearchResult(context, {
        query: SEARCH_STRING,
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestion: SEARCH_STRING,
      }),
      ...makeRemoteSuggestionResults(context),
    ],
  });
  await cleanUpSuggestions();
  UrlbarPrefs.clear("experimental.hideHeuristic");
});

// When the heuristic is hidden, form history results that match the heuristic
// should be included and not deduped.
add_task(async function hideHeuristic_formHistory() {
  UrlbarPrefs.set("experimental.hideHeuristic", true);
  UrlbarPrefs.set("browser.search.suggest.enabled", true);
  UrlbarPrefs.set("suggest.searches", true);

  // Search for exactly the suggestion of the first form history result.
  // Expected results:
  //
  // * First form history should be included even though it dupes the heuristic
  // * Other form history should not be included because they don't match the
  //   search string
  // * The first remote suggestion that just echoes the search string should not
  //   be included because it dupes the first form history
  // * The remaining remote suggestions should be included because they don't
  //   dupe anything
  let context = createContext(SEARCH_STRING, { isPrivate: false });
  let firstFormHistory = makeFormHistoryResults(context, 1)[0];
  context = createContext(firstFormHistory.payload.suggestion, {
    isPrivate: false,
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      firstFormHistory,
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: firstFormHistory.payload.suggestion,
      }),
    ],
  });

  // Add these form history strings to use below.
  let formHistoryStrings = ["foo", "FOO ", "foobar", "fooquux"];
  await UrlbarTestUtils.formHistory.add(formHistoryStrings);

  // Search for "foo". Expected results:
  //
  // * "foo" form history should be included even though it dupes the heuristic
  // * "FOO " form history should not be included because it dupes the "foo"
  //   form history
  // * "foobar" and "fooqux" form history should be included because they don't
  //   dupe anything
  // * "foo" remote suggestion should not be included because it dupes the "foo"
  //   form history
  // * "foo foo" and "foo bar" remote suggestions should be included because
  //   they don't dupe anything
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foo",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
    ],
  });

  // Add SERPs for "foo" and "food", and search for "foo". Expected results:
  //
  // * "foo" form history should be included even though it dupes the heuristic
  // * "foobar" and "fooqux" form history should be included because they don't
  //   dupe anything
  // * "foo" SERP depends on `showSearchSuggestionsFirst`, see below
  // * "food" SERP should be include because it doesn't dupe anything
  // * "foo" remote suggestion should not be included because it dupes the "foo"
  //   form history
  // * "foo foo" and "foo bar" remote suggestions should be included because
  //   they don't dupe anything
  let engine = await SearchService.getDefault();
  let serpURLs = ["foo", "food"].map(
    term => UrlbarUtils.getSearchQueryUrl(engine, term)[0]
  );
  await PlacesTestUtils.addVisits(serpURLs);

  // With `showSearchSuggestionsFirst = false` so that general results appear
  // before suggestions, the muxer visits the "foo" (and "food") SERPs before
  // visiting the "foo" form history, and so it doesn't see that the "foo" SERP
  // dupes the form history. The SERP is therefore included.
  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=food`,
        title: `test visit for http://localhost:${port}/search?q=food`,
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=foo`,
        title: `test visit for http://localhost:${port}/search?q=foo`,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foo",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
    ],
  });

  // Now clear `showSearchSuggestionsFirst` so that suggestions appear before
  // general results. Now the muxer will see that the "foo" SERP dupes the "foo"
  // form history, so it will exclude it.
  UrlbarPrefs.clear("showSearchSuggestionsFirst");
  context = createContext("foo", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foo",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "foobar",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      makeFormHistoryResult(context, {
        suggestion: "fooquux",
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
      ...makeRemoteSuggestionResults(context, {
        suggestionPrefix: "foo",
      }),
      makeVisitResult(context, {
        uri: `http://localhost:${port}/search?q=food`,
        title: `test visit for http://localhost:${port}/search?q=food`,
      }),
    ],
  });

  await UrlbarTestUtils.formHistory.remove(formHistoryStrings);

  await cleanUpSuggestions();
  UrlbarPrefs.clear("experimental.hideHeuristic");
});
