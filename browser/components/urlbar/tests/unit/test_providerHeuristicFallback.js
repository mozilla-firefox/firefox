/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that visit-url and search engine heuristic results are returned by
 * UrlbarProviderHeuristicFallback.
 */

const QUICKACTIONS_PREF = "browser.urlbar.suggest.quickactions";
const SUGGEST_ENABLED_PREF = "browser.search.suggest.enabled";
const PRIVATE_SEARCH_PREF = "browser.search.separatePrivateDefault.featureGate";

// We make sure that restriction tokens and search terms are correctly
// recognized when they are separated by each of these different types of spaces
// and combinations of spaces.  U+3000 is the ideographic space in CJK and is
// commonly used by CJK speakers.
const TEST_SPACES = [" ", "\u3000", " \u3000", "\u3000 "];

testEngine_setup();

add_setup(async function () {
  // These tests expect the unified search button prefs to default to false.
  let unifiedSearchButtonDefaults = Services.prefs.getDefaultBranch(
    "browser.urlbar.unifiedSearchButton."
  );
  let originalDefaults = {};
  for (let name of ["always", "historyInSearchMode"]) {
    originalDefaults[name] = unifiedSearchButtonDefaults.getBoolPref(name);
    unifiedSearchButtonDefaults.setBoolPref(name, false);
  }
  registerCleanupFunction(async () => {
    for (let [name, value] of Object.entries(originalDefaults)) {
      unifiedSearchButtonDefaults.setBoolPref(name, value);
    }
    Services.prefs.clearUserPref(QUICKACTIONS_PREF);
    Services.prefs.clearUserPref(SUGGEST_ENABLED_PREF);
    Services.prefs.clearUserPref(PRIVATE_SEARCH_PREF);
    Services.prefs.clearUserPref("keyword.enabled");
  });
  Services.prefs.setBoolPref(QUICKACTIONS_PREF, false);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, false);
  Services.prefs.setBoolPref(PRIVATE_SEARCH_PREF, false);
});

add_task(async function () {
  info("visit url, no protocol");
  let query = "mozilla.org";
  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info("visit url, no protocol but with 2 dots");
  query = "www.mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info("visit url, no protocol, e-mail like");
  query = "a@b.com";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info("visit url, with protocol but with 2 dots");
  query = "https://www.mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  // info("visit url, with protocol but with 3 dots");
  query = "https://www.mozilla.org.tw";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit url, with protocol");
  query = "https://mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit url on searchbar (should search)");
  query = "https://mozilla.org";
  context = createContext(query, {
    isPrivate: false,
    sapName: "searchbar",
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
        query,
      }),
    ],
  });

  info("visit url, about: protocol (no host)");
  query = "about:nonexistent";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: query,
        title: query,
        heuristic: true,
      }),
    ],
  });

  info("visit url, with non-standard whitespace");
  query = "https://mozilla.org";
  context = createContext(`${query}\u2028`, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  // This is distinct because of how we predict being able to url autofill via
  // host lookups.
  info("visit url, host matching visited host but not visited url");
  await PlacesTestUtils.addVisits([
    {
      uri: Services.io.newURI("http://mozilla.org/wine/"),
      title: "Mozilla Wine",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ]);
  query = "mozilla.org/rum";
  context = createContext(`${query}\u2028`, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}`,
        title: `${query}`,
        iconUri: "page-icon:http://mozilla.org/",
        heuristic: true,
      }),
    ],
  });
  await PlacesUtils.history.clear();

  // And hosts with no dot in them are special, due to requiring safelisting.
  info("unknown host");
  query = "firefox";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  info("string with known host");
  query = "firefox/get";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  Services.prefs.setBoolPref("browser.fixup.domainwhitelist.firefox", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.fixup.domainwhitelist.firefox");
  });

  info("known host");
  query = "firefox";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info("url with known host");
  query = "firefox/get";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}`,
        title: `${query}`,
        iconUri: "page-icon:http://firefox/",
        heuristic: true,
      }),
    ],
  });

  info("visit url, host matching visited host but not visited url, known host");
  Services.prefs.setBoolPref("browser.fixup.domainwhitelist.mozilla", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.fixup.domainwhitelist.mozilla");
  });
  query = "mozilla/rum";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}`,
        title: `${query}`,
        iconUri: "page-icon:http://mozilla/",
        heuristic: true,
      }),
    ],
  });

  // ipv4 and ipv6 literal addresses should offer to visit.
  info("visit url, ipv4 literal");
  query = "127.0.0.1";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit url, ipv6 literal");
  query = "[2001:db8::1]";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  // Setting keyword.enabled to false should always try to visit.
  let keywordEnabled = Services.prefs.getBoolPref("keyword.enabled");
  Services.prefs.setBoolPref("keyword.enabled", false);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("keyword.enabled");
  });
  info("visit url, keyword.enabled = false");
  query = "bacon";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit two word query, keyword.enabled = false");
  query = "bacon lovers";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: query,
        title: query,
        heuristic: true,
      }),
    ],
  });

  info("Forced search through a restriction token, keyword.enabled = false");
  query = "?bacon";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
        query: "bacon",
      }),
    ],
  });

  info("Forced search through search mode, keyword.enabled = false");
  query = "bacon";
  context = createContext(query, {
    isPrivate: false,
    searchMode: {
      engineName: SUGGESTIONS_ENGINE_NAME,
      isGeneralPurposeEngine: false,
    },
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
        query: "bacon",
      }),
    ],
  });

  for (let sapName of ["searchbar", "newtab_searchbar"]) {
    info(`Forced search through ${sapName}, keyword.enabled = false`);
    query = "bacon";
    context = createContext(query, { isPrivate: false, sapName });
    await check_results({
      context,
      matches: [
        makeSearchResult(context, {
          engineName: SUGGESTIONS_ENGINE_NAME,
          heuristic: true,
          query: "bacon",
        }),
      ],
    });
  }

  info(
    "A URL is still a visit in the newtab search bar, keyword.enabled = false"
  );
  query = "https://mozilla.org";
  context = createContext(query, {
    isPrivate: false,
    sapName: "newtab_searchbar",
  });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  // The address bar gets no result at all for this; a search bar searches it.
  info("Invalid URL through the newtab search bar, keyword.enabled = false");
  query = "bacon *";
  context = createContext(query, {
    isPrivate: false,
    sapName: "newtab_searchbar",
  });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
        query,
      }),
    ],
  });

  info("Non-search restriction token and invalid URL, keyword.enabled = false");
  query = "bacon *";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [],
  });

  Services.prefs.setBoolPref("keyword.enabled", true);
  info("visit two word query, keyword.enabled = true");
  query = "bacon lovers";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  Services.prefs.setBoolPref("keyword.enabled", keywordEnabled);

  info("visit url, scheme+host");
  query = "http://example";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit url, scheme+host");
  query = "ftp://example";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("visit url, host+port");
  query = "example:8080";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  info("numerical operations that look like urls should search");
  query = "123/12";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeCalculatorResult(context, {
        value: Number((123 / 12).toFixed(8)).toString(),
      }),
    ],
  });

  info("numerical operations that look like urls should search");
  query = "123.12/12.1";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
      makeCalculatorResult(context, {
        value: Number((123.12 / 12.1).toFixed(8)).toString(),
      }),
    ],
  });

  query = "resource:///modules";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: query,
        title: query,
        heuristic: true,
      }),
    ],
  });

  info("access resource://app/modules");
  query = "resource://app/modules";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: query,
        title: query,
        heuristic: true,
      }),
    ],
  });

  info("protocol with an extra slash");
  query = "http:///";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  info("change default engine");
  let originalTestEngine = SearchService.getEngineByName(
    SUGGESTIONS_ENGINE_NAME
  );
  await SearchTestUtils.installSearchExtension({
    name: "AliasEngine",
    keyword: "alias",
  });
  let engine2 = SearchService.getEngineByName("AliasEngine");
  Assert.notEqual(
    SearchService.defaultEngine,
    engine2,
    "New engine shouldn't be the current engine yet"
  );
  await SearchService.setDefault(engine2, SearchService.CHANGE_REASON.UNKNOWN);
  query = "toronto";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: "AliasEngine",
        heuristic: true,
      }),
    ],
  });
  await SearchService.setDefault(
    originalTestEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  info(
    "Leading search-mode restriction tokens are removed from the search result."
  );
  for (let token of UrlbarShared.SEARCH_MODE_RESTRICT) {
    for (let spaces of TEST_SPACES) {
      query = token + spaces + "query";
      info("Testing: " + JSON.stringify({ query, spaces: codePoints(spaces) }));
      let expectedQuery = query.substring(1).trimStart();
      context = createContext(query, { isPrivate: false });
      info(`Searching for "${query}", expecting "${expectedQuery}"`);
      let payload = {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        heuristic: true,
        query: expectedQuery,
        alias: token,
      };
      if (token == UrlbarShared.RESTRICT_TOKENS.SEARCH) {
        payload.source = UrlbarShared.RESULT_SOURCE.SEARCH;
        payload.engineName = SUGGESTIONS_ENGINE_NAME;
      }
      await check_results({
        context,
        matches: [makeSearchResult(context, payload)],
      });
    }
  }

  info(
    "Leading search-mode restriction tokens are removed from the search result with keyword.enabled = false."
  );
  Services.prefs.setBoolPref("keyword.enabled", false);
  for (let token of UrlbarShared.SEARCH_MODE_RESTRICT) {
    for (let spaces of TEST_SPACES) {
      query = token + spaces + "query";
      info("Testing: " + JSON.stringify({ query, spaces: codePoints(spaces) }));
      let expectedQuery = query.substring(1).trimStart();
      context = createContext(query, { isPrivate: false });
      info(`Searching for "${query}", expecting "${expectedQuery}"`);
      let payload = {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        heuristic: true,
        query: expectedQuery,
        alias: token,
      };
      if (token == UrlbarShared.RESTRICT_TOKENS.SEARCH) {
        payload.source = UrlbarShared.RESULT_SOURCE.SEARCH;
        payload.engineName = SUGGESTIONS_ENGINE_NAME;
      }
      await check_results({
        context,
        matches: [makeSearchResult(context, payload)],
      });
    }
  }
  Services.prefs.clearUserPref("keyword.enabled");

  info(
    "Leading non-search-mode restriction tokens are not removed from the search result."
  );
  for (let token of Object.values(UrlbarShared.RESTRICT_TOKENS)) {
    if (UrlbarShared.SEARCH_MODE_RESTRICT.has(token)) {
      continue;
    }
    for (let spaces of TEST_SPACES) {
      query = token + spaces + "query";
      info("Testing: " + JSON.stringify({ query, spaces: codePoints(spaces) }));
      let expectedQuery = query;
      context = createContext(query, { isPrivate: false });
      info(`Searching for "${query}", expecting "${expectedQuery}"`);
      await check_results({
        context,
        matches: [
          makeSearchResult(context, {
            heuristic: true,
            query: expectedQuery,
            engineName: SUGGESTIONS_ENGINE_NAME,
          }),
        ],
      });
    }
  }

  info(
    "Test the format inputed is user@host, and the host is in domainwhitelist"
  );
  Services.prefs.setBoolPref("browser.fixup.domainwhitelist.test-host", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.fixup.domainwhitelist.test-host");
  });

  query = "any@test-host";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info(
    "Test the format inputed is user@host, but the host is not in domainwhitelist"
  );
  query = "any@not-host";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        heuristic: true,
        query,
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  info(
    "Test if the format of user:pass@host is handled as visit even if the host is not in domainwhitelist"
  );
  query = "user:pass@not-host";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://user:pass@not-host/",
        title: "user:pass@not-host/",
        heuristic: true,
      }),
    ],
  });

  info("Test if the format of user@ipaddress is handled as visit");
  query = "user@192.168.0.1";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: "http://user@192.168.0.1/",
        title: "user@192.168.0.1/",
        heuristic: true,
      }),
      makeSearchResult(context, {
        heuristic: false,
        query,
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  await PlacesUtils.history.clear();
  // Check that punycode results are properly decoded before being displayed.
  info("visit url, host matching visited host but not visited url");
  await PlacesTestUtils.addVisits([
    {
      uri: Services.io.newURI("http://test.пример.com/"),
      title: "test.пример.com",
      transition: PlacesUtils.history.TRANSITION_TYPED,
    },
  ]);
  context = createContext("test", { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.HISTORY,
        uri: `http://test.xn--e1afmkfd.com/`,
        title: "test.пример.com",
        heuristic: true,
        iconUri: "page-icon:http://test.xn--e1afmkfd.com/",
      }),
    ],
  });
  await PlacesUtils.history.clear();
});

add_task(async function dont_fixup_urls_with_at_symbol() {
  info("don't fixup search string if it contains no protocol and spaces.");
  let query = "Lorem Ipsum @mozilla.org";
  let context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        heuristic: true,
      }),
    ],
  });

  query = "http://Lorem Ipsum @mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://Lorem%20Ipsum%20@mozilla.org/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });
  query = "https://Lorem Ipsum @mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `https://Lorem%20Ipsum%20@mozilla.org/`,
        title: `${query}/`,
        heuristic: true,
      }),
    ],
  });

  query = "LoremIpsum@mozilla.org";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}/`,
        title: `${query}/`,
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });

  query = "";
  context = createContext(query, { isPrivate: false });
  await check_results({
    context,
    matches: [],
  });
});

add_task(async function url_in_search_mode() {
  Services.prefs.clearUserPref("keyword.enabled");
  let query = "mozilla.org";
  let engineSearchMode = {
    engineName: SUGGESTIONS_ENGINE_NAME,
    isGeneralPurposeEngine: false,
  };

  info("A URL is searched for in an engine search mode");
  let context = createContext(query, {
    isPrivate: false,
    searchMode: engineSearchMode,
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
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

  let checkVisit = async sapName => {
    context = createContext(query, {
      isPrivate: false,
      sapName,
      searchMode: engineSearchMode,
      sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
    });
    await check_results({
      context,
      matches: [
        makeVisitResult(context, {
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          uri: `http://${query}/`,
          title: `${query}/`,
          heuristic: true,
        }),
        makeSearchResult(context, {
          engineName: SUGGESTIONS_ENGINE_NAME,
        }),
      ],
    });
  };

  info("A URL is visited in the newtab search bar's engine search mode");
  await checkVisit("newtab_searchbar");

  Services.prefs.setBoolPref("browser.urlbar.unifiedSearchButton.always", true);

  info("A URL is visited in an engine search mode with navigation enabled");
  await checkVisit("urlbar");

  info("A URL is not visited in a local search mode");
  context = createContext(query, {
    isPrivate: false,
    searchMode: { source: UrlbarShared.RESULT_SOURCE.BOOKMARKS },
    sources: [UrlbarShared.RESULT_SOURCE.BOOKMARKS],
  });
  await check_results({ context, matches: [] });

  info("A URL is searched for in the search bar's engine search mode");
  context = createContext(query, {
    isPrivate: false,
    sapName: "searchbar",
    searchMode: engineSearchMode,
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
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

  Services.prefs.clearUserPref("browser.urlbar.unifiedSearchButton.always");
});

add_task(async function url_search_in_search_mode() {
  Services.prefs.clearUserPref("keyword.enabled");
  Services.prefs.setBoolPref("browser.urlbar.unifiedSearchButton.always", true);
  let engineSearchMode = {
    engineName: SUGGESTIONS_ENGINE_NAME,
    isGeneralPurposeEngine: false,
  };

  let checkUrl = async (query, { uri, title, iconUri, sources }) => {
    let context = createContext(query, {
      isPrivate: false,
      searchMode: engineSearchMode,
      ...(sources && { sources }),
    });
    await check_results({
      context,
      matches: [
        makeVisitResult(context, {
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          uri,
          title,
          iconUri,
          heuristic: true,
        }),
        makeSearchResult(context, {
          engineName: SUGGESTIONS_ENGINE_NAME,
          suggestedIndex: 1,
        }),
      ],
    });
  };

  info("A search is offered for an origin");
  await checkUrl("mozilla.org", {
    uri: "http://mozilla.org/",
    title: "mozilla.org/",
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
  });

  info("A search is offered for a URL with a path");
  await checkUrl("mozilla.org/foo", {
    uri: "http://mozilla.org/foo",
    title: "mozilla.org/foo",
    iconUri: "page-icon:http://mozilla.org/",
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
  });

  info("A search is offered for a URL with a scheme");
  await checkUrl("https://mozilla.org/foo", {
    uri: "https://mozilla.org/foo",
    title: "https://mozilla.org/foo",
    iconUri: "page-icon:https://mozilla.org/",
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
  });

  info("A search is offered with historyInSearchMode enabled");
  Services.prefs.setBoolPref(
    "browser.urlbar.unifiedSearchButton.historyInSearchMode",
    true
  );
  await checkUrl("mozilla.org/foo", {
    uri: "http://mozilla.org/foo",
    title: "mozilla.org/foo",
    iconUri: "page-icon:http://mozilla.org/",
  });
  Services.prefs.clearUserPref(
    "browser.urlbar.unifiedSearchButton.historyInSearchMode"
  );

  info("Only the heuristic search is shown for a non-URL");
  let context = createContext("hello world", {
    isPrivate: false,
    searchMode: engineSearchMode,
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
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

  info("A URL is searched for when unifiedSearchButton.always is disabled");
  Services.prefs.clearUserPref("browser.urlbar.unifiedSearchButton.always");
  context = createContext("mozilla.org/foo", {
    isPrivate: false,
    searchMode: engineSearchMode,
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
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
});

add_task(async function url_search_in_search_mode_dedupes_form_history() {
  Services.prefs.setBoolPref("browser.urlbar.unifiedSearchButton.always", true);
  Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, true);
  let query = "mozilla.org/foo";
  await UrlbarTestUtils.formHistory.add([
    { value: query, source: SUGGESTIONS_ENGINE_NAME },
    { value: `${query}bar`, source: SUGGESTIONS_ENGINE_NAME },
  ]);
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("browser.urlbar.unifiedSearchButton.always");
    Services.prefs.setBoolPref(SUGGEST_ENABLED_PREF, false);
    await UrlbarTestUtils.formHistory.clear();
  });

  let context = createContext(query, {
    isPrivate: false,
    prohibitRemoteResults: true,
    searchMode: {
      engineName: SUGGESTIONS_ENGINE_NAME,
      isGeneralPurposeEngine: false,
    },
    sources: [UrlbarShared.RESULT_SOURCE.SEARCH],
  });
  await check_results({
    context,
    matches: [
      makeVisitResult(context, {
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        uri: `http://${query}`,
        title: query,
        iconUri: "page-icon:http://mozilla.org/",
        heuristic: true,
      }),
      makeSearchResult(context, {
        engineName: SUGGESTIONS_ENGINE_NAME,
        suggestedIndex: 1,
      }),
      makeFormHistoryResult(context, {
        suggestion: `${query}bar`,
        engineName: SUGGESTIONS_ENGINE_NAME,
      }),
    ],
  });
});

/**
 * Returns an array of code points in the given string.  Each code point is
 * returned as a hexidecimal string.
 *
 * @param {string} str
 *   The code points of this string will be returned.
 * @returns {Array}
 *   Array of code points in the string, where each is a hexidecimal string.
 */
function codePoints(str) {
  return str.split("").map(s => s.charCodeAt(0).toString(16));
}
