/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

var { UrlbarMuxer, UrlbarProvider, UrlbarUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs"
);
var { UrlbarQueryContext } = ChromeUtils.importESModule(
  "chrome://browser/content/urlbar/UrlbarQueryContext.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  UrlbarParentController:
    "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarProviderSearchSuggestions:
    "moz-src:///browser/components/urlbar/UrlbarProviderSearchSuggestions.sys.mjs",
  ProvidersManager:
    "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "QuickSuggestTestUtils", () => {
  const { QuickSuggestTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "MerinoTestUtils", () => {
  const { MerinoTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/MerinoTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "GeolocationTestUtils", () => {
  const { GeolocationTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/GeolocationTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "PlacesFrecencyRecalculator", () => {
  return Cc["@mozilla.org/places/frecency-recalculator;1"].getService(
    Ci.nsIObserver
  ).wrappedJSObject;
});

// Most tests need a profile to be setup, so do that here.
do_get_profile();

SearchTestUtils.init(this);

const SUGGESTIONS_ENGINE_NAME = "Suggestions";
const TAIL_SUGGESTIONS_ENGINE_NAME = "Tail Suggestions";

const SEARCH_GLASS_ICON = "chrome://global/skin/icons/search-glass.svg";

/**
 * Gets the database connection.  If the Places connection is invalid it will
 * try to create a new connection.
 *
 * @param [optional] aForceNewConnection
 *        Forces creation of a new connection to the database.  When a
 *        connection is asyncClosed it cannot anymore schedule async statements,
 *        though connectionReady will keep returning true (Bug 726990).
 *
 * @returns The database connection or null if unable to get one.
 */
var gDBConn;
function DBConn(aForceNewConnection) {
  if (!aForceNewConnection) {
    let db = PlacesUtils.history.DBConnection;
    if (db.connectionReady) {
      return db;
    }
  }

  // If the Places database connection has been closed, create a new connection.
  if (!gDBConn || aForceNewConnection) {
    let file = Services.dirsvc.get("ProfD", Ci.nsIFile);
    file.append("places.sqlite");
    let dbConn = (gDBConn = Services.storage.openDatabase(file));

    TestUtils.topicObserved("profile-before-change").then(() =>
      dbConn.asyncClose()
    );
  }

  return gDBConn.connectionReady ? gDBConn : null;
}

/**
 * @param {string} searchString The search string to insert into the context.
 * @param {object} properties Overrides for the default values.
 * @returns {UrlbarQueryContext} Creates a dummy query context with pre-filled
 *          required options.
 */
function createContext(searchString = "foo", properties = {}) {
  info(`Creating new queryContext with searchString: ${searchString}`);
  let context = new UrlbarQueryContext(
    Object.assign(
      {
        allowAutofill: UrlbarPrefs.get("autoFill"),
        isPrivate: true,
        maxResults: UrlbarPrefs.get("maxRichResults"),
        sapName: "urlbar",
        searchString,
      },
      properties
    )
  );
  let tokens = UrlbarTokenizer.tokenize(context);
  context.tokens = tokens;
  return context;
}

/**
 * Waits for the given notification from the supplied controller.
 *
 * @param {UrlbarParentController} controller The controller to wait for a response from.
 * @param {string} notification The name of the notification to wait for.
 * @param {boolean} expected Wether the notification is expected.
 * @returns {Promise} A promise that is resolved with the arguments supplied to
 *   the notification.
 */
function promiseControllerNotification(
  controller,
  notification,
  expected = true
) {
  return new Promise((resolve, reject) => {
    let proxifiedObserver = new Proxy(
      {},
      {
        get: (target, name) => {
          if (name == notification) {
            return (...args) => {
              controller.removeListener(proxifiedObserver);
              if (expected) {
                resolve(args);
              } else {
                reject();
              }
            };
          }
          return () => false;
        },
      }
    );
    controller.addListener(proxifiedObserver);
  });
}

function convertToUtf8(str) {
  return String.fromCharCode(...new TextEncoder().encode(str));
}

/**
 * Helper function to clear the existing providers and register a basic provider
 * that returns only the results given.
 *
 * @param {Array} results The results for the provider to return.
 * @param {Function} [onCancel] Optional, called when the query provider
 *                              receives a cancel instruction.
 * @param {UrlbarShared.PROVIDER_TYPE} type The provider type.
 * @param {string} [name] Optional, use as the provider name.
 *                        If none, a default name is chosen.
 * @returns {UrlbarProvider} The provider
 */
function registerBasicTestProvider(results = [], onCancel, type, name) {
  let provider = new UrlbarTestUtils.TestProvider({
    results,
    onCancel,
    type,
    name,
  });
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);
  registerCleanupFunction(() => providersManager.unregisterProvider(provider));
  return provider;
}

// Creates an HTTP server for the test.
function makeTestServer(port = -1) {
  let httpServer = new HttpServer();
  httpServer.start(port);
  registerCleanupFunction(() => httpServer.stop(() => {}));
  return httpServer;
}

/**
 * Sets up a search engine that provides some suggestions by appending strings
 * onto the search query.
 *
 * @param {Function} suggestionsFn
 *   A function that returns an array of suggestion strings given a
 *   search string.  If not given, a default function is used.
 * @param {object} options
 *   Options for the check.
 * @param {string} [options.name]
 *   The name of the engine to install.
 * @returns {SearchEngine} The new engine.
 */
async function addTestSuggestionsEngine(
  suggestionsFn = null,
  { name = SUGGESTIONS_ENGINE_NAME } = {}
) {
  // This port number should match the number in engine-suggestions.xml.
  let server = makeTestServer();
  server.registerPathHandler("/suggest", (req, resp) => {
    let params = new URLSearchParams(req.queryString);
    let searchStr = params.get("q");
    let suggestions = suggestionsFn
      ? suggestionsFn(searchStr)
      : [searchStr].concat(["foo", "bar"].map(s => searchStr + " " + s));
    let data = [searchStr, suggestions];
    resp.setHeader("Content-Type", "application/json", false);
    resp.write(JSON.stringify(data));
  });
  await SearchTestUtils.installSearchExtension({
    name,
    search_url: `http://localhost:${server.identity.primaryPort}/search`,
    suggest_url: `http://localhost:${server.identity.primaryPort}/suggest`,
    suggest_url_get_params: "?q={searchTerms}",
  });
  let engine = SearchService.getEngineByName(name);
  return engine;
}

/**
 * Sets up a search engine that provides some tail suggestions by creating an
 * array that mimics Google's tail suggestion responses.
 *
 * @param {Function} suggestionsFn
 *        A function that returns an array that mimics Google's tail suggestion
 *        responses. See bug 1626897.
 *        NOTE: Consumers specifying suggestionsFn must include searchStr as a
 *              part of the array returned by suggestionsFn.
 * @returns {SearchEngine} The new engine.
 */
async function addTestTailSuggestionsEngine(suggestionsFn = null) {
  // This port number should match the number in engine-tail-suggestions.xml.
  let server = makeTestServer();
  server.registerPathHandler("/suggest", (req, resp) => {
    let params = new URLSearchParams(req.queryString);
    let searchStr = params.get("q");
    let suggestions = suggestionsFn
      ? suggestionsFn(searchStr)
      : [
          "what time is it in t",
          ["what is the time today texas"].concat(
            ["toronto", "tunisia"].map(s => searchStr + s.slice(1))
          ),
          [],
          {
            "google:irrelevantparameter": [],
            "google:suggestdetail": [{}].concat(
              ["toronto", "tunisia"].map(s => ({
                mp: "… ",
                t: s,
              }))
            ),
          },
        ];
    let data = suggestions;
    let jsonString = JSON.stringify(data);
    // This script must be evaluated as UTF-8 for this to write out the bytes of
    // the string in UTF-8.  If it's evaluated as Latin-1, the written bytes
    // will be the result of UTF-8-encoding the result-string *twice*, which
    // will break the "… " match prefixes.
    let stringOfUtf8Bytes = convertToUtf8(jsonString);
    resp.setHeader("Content-Type", "application/json", false);
    resp.write(stringOfUtf8Bytes);
  });
  await SearchTestUtils.installSearchExtension({
    name: TAIL_SUGGESTIONS_ENGINE_NAME,
    search_url: `http://localhost:${server.identity.primaryPort}/search`,
    suggest_url: `http://localhost:${server.identity.primaryPort}/suggest`,
    suggest_url_get_params: "?q={searchTerms}",
  });
  let engine = SearchService.getEngineByName("Tail Suggestions");
  return engine;
}

/**
 * Creates a function that can be provided to the new engine
 * utility function to mimic a search engine that returns
 * rich suggestions.
 *
 * @param {string} searchStr
 *        The string being searched for.
 *
 * @returns {object}
 *        A JSON object mimicing the data format returned by
 *        a search engine.
 */
function defaultRichSuggestionsFn(searchStr) {
  let suffixes = ["toronto", "tunisia", "tacoma", "taipei"];
  return [
    "what time is it in t",
    suffixes.map(s => searchStr + s.slice(1)),
    [],
    {
      "google:irrelevantparameter": [],
      "google:suggestdetail": suffixes.map((suffix, i) => {
        // Set every other suggestion as a rich suggestion so we can
        // test how they are handled and ordered when interleaved.
        if (i % 2) {
          return {};
        }
        return {
          a: "description",
          dc: "#FFFFFF",
          i: "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==",
          t: "Title",
        };
      }),
    },
  ];
}

async function addOpenPages(
  uri,
  count = 1,
  userContextId = 0,
  tabGroupId = null
) {
  for (let i = 0; i < count; i++) {
    await UrlbarProviderOpenTabs.registerOpenTab(
      uri.spec,
      userContextId,
      tabGroupId,
      false
    );
  }
}

async function removeOpenPages(
  aUri,
  aCount = 1,
  aUserContextId = 0,
  tabGroupId = null
) {
  for (let i = 0; i < aCount; i++) {
    await UrlbarProviderOpenTabs.unregisterOpenTab(
      aUri.spec,
      aUserContextId,
      tabGroupId,
      false
    );
  }
}

/**
 * Helper for tests that generate search results but aren't interested in
 * suggestions, such as autofill tests. Installs a test engine and disables
 * suggestions.
 */
function testEngine_setup() {
  add_setup(async () => {
    await cleanupPlaces();
    let engine = await addTestSuggestionsEngine();
    let oldDefaultEngine = await SearchService.getDefault();

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("browser.urlbar.suggest.searches");
      Services.prefs.clearUserPref("browser.urlbar.contextualSearch.enabled");
      Services.prefs.clearUserPref(
        "browser.search.separatePrivateDefault.ui.enabled"
      );
      SearchService.setDefault(
        oldDefaultEngine,
        SearchService.CHANGE_REASON.UNKNOWN
      );
    });

    SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
    Services.prefs.setBoolPref(
      "browser.search.separatePrivateDefault.ui.enabled",
      false
    );
    Services.prefs.setBoolPref("browser.urlbar.suggest.searches", false);
    Services.prefs.setBoolPref(
      "browser.urlbar.scotchBonnet.enableOverride",
      false
    );
  });
}

async function cleanupPlaces() {
  Services.prefs.clearUserPref("browser.urlbar.autoFill");

  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
}

/**
 * Creates a UrlbarResult for a bookmark result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} [options.title]
 *   The page title.
 * @param {string} options.uri
 *   The page URI.
 * @param {string} [options.iconUri]
 *   A URI for the page's icon.
 * @param {Array} [options.tags]
 *   An array of string tags. Defaults to an empty array.
 * @param {boolean} [options.heuristic]
 *   True if this is a heuristic result. Defaults to false.
 * @param {number} [options.source]
 *   Where the results should be sourced from. See {@link UrlbarShared.RESULT_SOURCE}.
 * @param {number} [options.bookmarkDateMs]
 *   The date the bookmark was added in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @param {number} [options.lastVisit]
 *   The date the bookmark was last visited in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @param {boolean} [options.isPinned]
 *   Whether the result is pinned. Relevant to results from
 *   UrlbarProviderTopSites.
 * @param {boolean} [options.isSponsored]
 *   Whether the result is sponsored. Relevant to results from
 *   UrlbarProviderTopSites.
 * @param {boolean} [options.sendAttributionRequest]
 *   The result's sendAttributionRequest. Relevant to results from
 *   UrlbarProviderTopSites.
 * @param {string} [options.providerName]
 *   The name of the provider offering this result. The test suite will not
 *   check which provider offered a result unless this option is specified.
 * @returns {UrlbarResult}
 */
function makeBookmarkResult(
  queryContext,
  {
    title,
    uri,
    iconUri,
    tags = [],
    heuristic = false,
    source = UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    bookmarkDateMs = undefined,
    lastVisit = undefined,
    isPinned = undefined,
    isSponsored = undefined,
    sendAttributionRequest = undefined,
    providerName = undefined,
  }
) {
  let payload = {
    url: uri,
    title,
    tags,
    // Check against undefined so consumers can pass in the empty string.
    icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    isBlockable:
      source == UrlbarShared.RESULT_SOURCE.HISTORY ? true : undefined,
    blockL10n:
      source == UrlbarShared.RESULT_SOURCE.HISTORY
        ? { id: "urlbar-result-menu-remove-from-history2" }
        : undefined,
    helpUrl:
      source == UrlbarShared.RESULT_SOURCE.HISTORY
        ? Services.urlFormatter.formatURLPref("app.support.baseURL") +
          "awesome-bar-result-menu"
        : undefined,
  };

  if (bookmarkDateMs !== undefined) {
    payload.bookmarkDateMs = bookmarkDateMs;
  }
  if (lastVisit !== undefined) {
    payload.lastVisit = lastVisit;
  }
  if (isPinned !== undefined) {
    payload.isPinned = isPinned;
  }
  if (isSponsored !== undefined) {
    payload.isSponsored = isSponsored;
  }
  if (sendAttributionRequest !== undefined) {
    payload.sendAttributionRequest = sendAttributionRequest;
  }

  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source,
    heuristic,
    payload,
    providerName,
  });
}

/**
 * Creates a UrlbarResult for a form history result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.suggestion
 *   The form history suggestion.
 * @param {string} options.engineName
 *   The name of the engine that will do the search when the result is picked.
 * @returns {UrlbarResult}
 */
function makeFormHistoryResult(queryContext, { suggestion, engineName }) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.SEARCH,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: {
      engine: engineName,
      suggestion,
      title: suggestion,
      lowerCaseSuggestion: suggestion.toLocaleLowerCase(),
      isBlockable: true,
      blockL10n: { id: "urlbar-result-menu-remove-from-history2" },
      helpUrl:
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
        "awesome-bar-result-menu",
    },
  });
}

/**
 * Creates a UrlbarResult for an omnibox extension result. For more information,
 * see the documentation for omnibox.SuggestResult:
 * https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/omnibox/SuggestResult
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.content
 *   The string displayed when the result is highlighted.
 * @param {string} options.description
 *   The string displayed in the address bar dropdown.
 * @param {string} options.keyword
 *   The keyword associated with the extension returning the result.
 * @param {boolean} [options.heuristic]
 *   True if this is a heuristic result. Defaults to false.
 * @returns {UrlbarResult}
 */
function makeOmniboxResult(
  queryContext,
  { content, description, keyword, heuristic = false }
) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.OMNIBOX,
    source: UrlbarShared.RESULT_SOURCE.ADDON,
    heuristic,
    payload: {
      title: description,
      content,
      keyword,
      icon: UrlbarShared.ICON.EXTENSION,
    },
  });
}

/**
 * Creates a UrlbarResult for an switch-to-tab result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.uri
 *   The page URI.
 * @param {string} [options.title]
 *   The page title.
 * @param {string} [options.iconUri]
 *   A URI for the page icon.
 * @param {number} [options.userContextId]
 *   An id of the userContext in which the tab is located.
 * @param {string} [options.tabGroup]
 *   An id of the tab group in which the tab is located.
 * @param {number} [options.bookmarkDateMs]
 *   If the URL is bookmarked, the date the bookmark was added in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @param {number} [options.lastVisit]
 *   The date the URL was last visited in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @returns {UrlbarResult}
 */
function makeTabSwitchResult(
  queryContext,
  {
    uri,
    title,
    iconUri,
    userContextId,
    tabGroup,
    bookmarkDateMs = undefined,
    lastVisit = undefined,
  }
) {
  let payload = {
    url: uri,
    title,
    // Check against undefined so consumers can pass in the empty string.
    icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    userContext: UrlbarUtils.getUserContextData(userContextId || 0),
    tabGroup,
  };

  if (bookmarkDateMs !== undefined) {
    payload.bookmarkDateMs = bookmarkDateMs;
  }
  if (lastVisit !== undefined) {
    payload.lastVisit = lastVisit;
  }

  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TAB_SWITCH,
    source: UrlbarShared.RESULT_SOURCE.TABS,
    payload,
  });
}

/**
 * Creates a UrlbarResult for a keyword search result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.uri
 *   The page URI.
 * @param {string} options.keyword
 *   The page's search keyword.
 * @param {string} [options.title]
 *   The title for the bookmarked keyword page.
 * @param {string} [options.iconUri]
 *   A URI for the engine's icon.
 * @param {string} [options.postData]
 *   The search POST data.
 * @param {boolean} [options.heuristic]
 *   True if this is a heuristic result. Defaults to false.
 * @returns {UrlbarResult}
 */
function makeKeywordSearchResult(
  queryContext,
  { uri, keyword, title, iconUri, postData, heuristic = false }
) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.KEYWORD,
    source: UrlbarShared.RESULT_SOURCE.BOOKMARKS,
    heuristic,
    payload: {
      title: title || uri,
      url: uri,
      keyword,
      input: queryContext.searchString,
      postData: postData || null,
      icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    },
  });
}

/**
 * Creates a UrlbarResult for a remote tab result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.uri
 *   The page URI.
 * @param {string} options.device
 *   The name of the device that the remote tab comes from.
 * @param {string} [options.title]
 *   The page title.
 * @param {number} [options.lastUsed]
 *   The last time the remote tab was visited, in epoch seconds. Defaults
 *   to 0.
 * @param {string} [options.iconUri]
 *   A URI for the page's icon.
 * @returns {UrlbarResult}
 */
function makeRemoteTabResult(
  queryContext,
  { uri, device, title, iconUri, lastUsed = 0 }
) {
  let payload = {
    url: uri,
    device,
    // Check against undefined so consumers can pass in the empty string.
    icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    lastUsed: lastUsed * 1000,
  };

  // Check against undefined so consumers can pass in the empty string.
  if (typeof title != "undefined") {
    payload.title = title;
  } else {
    payload.title = uri;
  }

  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.REMOTE_TAB,
    source: UrlbarShared.RESULT_SOURCE.TABS,
    payload,
  });
}

/**
 * Creates a UrlbarResult for a search result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} [options.suggestion]
 *   The suggestion offered by the search engine.
 * @param {string} [options.tailPrefix]
 *   The characters placed at the end of a Google "tail" suggestion. See
 *   {@link https://firefox-source-docs.mozilla.org/browser/urlbar/nontechnical-overview.html#search-suggestions}
 * @param {*} [options.tail]
 *   The details of the URL bar tail
 * @param {number} [options.tailOffsetIndex]
 *   The index of the first character in the tail suggestion that should be
 * @param {string} [options.engineName]
 *   The name of the engine providing the suggestion. Leave blank if there
 *   is no suggestion.
 * @param {string} [options.uri]
 *   The URI that the search result will navigate to.
 * @param {string} [options.query]
 *   The query that started the search. This overrides
 *   `queryContext.searchString`. This is useful when the query that will show
 *   up in the result object will be different from what was typed. For example,
 *   if a leading restriction token will be used.
 * @param {string} [options.alias]
 *   The alias for the search engine, if the search is an alias search.
 * @param {string} [options.engineIconUri]
 *   A URI for the engine's icon.
 * @param {boolean} [options.heuristic]
 *   True if this is a heuristic result. Defaults to false.
 * @param {boolean} [options.providesSearchMode]
 *   Whether search mode is entered when this result is selected.
 * @param {string} [options.providerName]
 *   The name of the provider offering this result. The test suite will not
 *   check which provider offered a result unless this option is specified.
 * @param {boolean} [options.inPrivateWindow]
 *   If the window to test is a private window.
 * @param {boolean} [options.isPrivateEngine]
 *   If the engine is a private engine.
 * @param {string} [options.searchUrlDomainWithoutSuffix]
 *   For tab-to-search results, the search engine domain without the public
 *   suffix.
 * @param {number} [options.type]
 *   The type of the search result. Defaults to UrlbarShared.RESULT_TYPE.SEARCH.
 * @param {number} [options.source]
 *   The source of the search result. Defaults to UrlbarShared.RESULT_SOURCE.SEARCH.
 * @param {boolean} [options.satisfiesAutofillThreshold]
 *   If this search should appear in the autofill section of the box
 * @param {boolean} [options.trending]
 *    If the search result is a trending result. `Defaults to false`.
 * @param {boolean} [options.isRichSuggestion]
 *    If the search result is a rich result. `Defaults to false`.
 * @returns {UrlbarResult}
 */
function makeSearchResult(
  queryContext,
  {
    suggestion,
    tailPrefix,
    tail,
    tailOffsetIndex,
    engineName,
    alias,
    uri,
    query,
    engineIconUri,
    providesSearchMode,
    providerName,
    inPrivateWindow,
    isPrivateEngine,
    searchUrlDomainWithoutSuffix,
    heuristic = false,
    trending = false,
    isRichSuggestion = false,
    type = UrlbarShared.RESULT_TYPE.SEARCH,
    source = UrlbarShared.RESULT_SOURCE.SEARCH,
    satisfiesAutofillThreshold = false,
  }
) {
  // Tail suggestion common cases, handled here to reduce verbosity in tests.
  if (tail) {
    if (!tailPrefix && !isRichSuggestion) {
      tailPrefix = "… ";
    }
    if (!tailOffsetIndex) {
      tailOffsetIndex = suggestion.indexOf(tail);
    }
  }

  let payload = {
    engine: engineName,
    suggestion,
    tailPrefix,
    tail,
    tailOffsetIndex,
    keyword: alias,
    // Check against undefined so consumers can pass in the empty string.
    query:
      typeof query != "undefined" ? query : queryContext.trimmedSearchString,
    icon: engineIconUri,
    providesSearchMode,
    inPrivateWindow,
    isPrivateEngine,
  };

  if (providesSearchMode) {
    // No title.
  } else if (payload.tail && payload.tailOffsetIndex >= 0) {
    payload.title = payload.tail;
  } else if (payload.suggestion != undefined) {
    payload.title = payload.suggestion;
  } else if (payload.query != undefined) {
    payload.title = payload.query;
  }

  if (uri) {
    payload.url = uri;
  }

  if (providerName == "UrlbarProviderTabToSearch") {
    if (searchUrlDomainWithoutSuffix.startsWith("www.")) {
      searchUrlDomainWithoutSuffix = searchUrlDomainWithoutSuffix.substring(4);
    }
    payload.searchUrlDomainWithoutSuffix = searchUrlDomainWithoutSuffix;
    payload.satisfiesAutofillThreshold = satisfiesAutofillThreshold;
    payload.isGeneralPurposeEngine = false;
  }

  if (providerName == "UrlbarProviderTokenAliasEngines") {
    payload.keywords = alias?.toLowerCase();
  }

  if (typeof suggestion == "string") {
    payload.lowerCaseSuggestion = suggestion.toLocaleLowerCase();
    payload.trending = trending;
  }

  if (isRichSuggestion) {
    payload.icon = UrlbarUtils.getRemoteIconUrl(
      "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==",
      UrlbarProviderSearchSuggestions.RICH_ICON_SIZE
    );
    payload.description = "description";
  }

  return new UrlbarResult({
    type,
    source,
    heuristic,
    isRichSuggestion,
    providerName,
    payload,
  });
}

/**
 * Creates a UrlbarResult for a history result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options Options for the result.
 * @param {string} options.title
 *   The page title.
 * @param {string} options.uri
 *   The page URI.
 * @param {Array} [options.tags]
 *   An array of string tags. Defaults to an empty array.
 * @param {string} [options.iconUri]
 *   A URI for the page's icon.
 * @param {boolean} [options.heuristic]
 *   True if this is a heuristic result. Defaults to false.
 * @param {string} options.providerName
 *   The name of the provider offering this result. The test suite will not
 *   check which provider offered a result unless this option is specified.
 * @param {number} [options.source]
 *   The source of the result
 * @param {boolean} [options.isAutofillFallback]
 *   Whether it's a result of being a fallback for the autofill result.
 * @param {number} [options.bookmarkDateMs]
 *   If the URL is bookmarked, the date the bookmark was added in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @param {number} [options.lastVisit]
 *   The date the URL was last visited in ms since epoch.
 *   For `check_results()`, leave this undefined to ignore the actual value.
 *   Pass zero to assert that the actual value is falsey.
 * @param {boolean} [options.isPinned]
 *   Whether the result is pinned. Relevant to results from
 *   UrlbarProviderTopSites.
 * @param {boolean} [options.isSponsored]
 *   Whether the result is sponsored. Relevant to results from
 *   UrlbarProviderTopSites.
 * @param {boolean} [options.sendAttributionRequest]
 *   The result's sendAttributionRequest. Relevant to results from
 *   UrlbarProviderTopSites.
 * @returns {UrlbarResult}
 */
function makeVisitResult(
  queryContext,
  {
    title,
    uri,
    iconUri,
    providerName,
    tags = [],
    heuristic = false,
    source = UrlbarShared.RESULT_SOURCE.HISTORY,
    isAutofillFallback = false,
    bookmarkDateMs = undefined,
    lastVisit = undefined,
    isPinned = undefined,
    isSponsored = undefined,
    sendAttributionRequest = undefined,
  }
) {
  let payload = {
    url: uri,
  };

  if (title != undefined) {
    payload.title = title;
  }
  if (bookmarkDateMs !== undefined) {
    payload.bookmarkDateMs = bookmarkDateMs;
  }
  if (lastVisit !== undefined) {
    payload.lastVisit = lastVisit;
  }
  if (isPinned !== undefined) {
    payload.isPinned = isPinned;
  }
  if (isSponsored !== undefined) {
    payload.isSponsored = isSponsored;
  }
  if (sendAttributionRequest !== undefined) {
    payload.sendAttributionRequest = sendAttributionRequest;
  }

  if (
    !heuristic &&
    providerName != "UrlbarProviderAboutPages" &&
    providerName != "UrlbarProviderTopSites" &&
    source == UrlbarShared.RESULT_SOURCE.HISTORY
  ) {
    payload.isBlockable = true;
    payload.blockL10n = { id: "urlbar-result-menu-remove-from-history2" };
    payload.helpUrl =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "awesome-bar-result-menu";
  }

  if (iconUri) {
    payload.icon = iconUri;
  } else if (
    iconUri === undefined &&
    source != UrlbarShared.RESULT_SOURCE.OTHER_LOCAL
  ) {
    payload.icon = `page-icon:${uri}`;
  }

  if (!heuristic && tags) {
    payload.tags = tags;
  }

  if (isAutofillFallback) {
    payload.isAutofillFallback = true;
  }

  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source,
    heuristic,
    providerName,
    payload,
  });
}

/**
 * Creates a UrlbarResult for a calculator result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options
 *   Options for the result.
 * @param {string} options.value
 *   The value of the calculator result.
 * @returns {UrlbarResult}
 */
function makeCalculatorResult(queryContext, { value }) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      value,
      input: queryContext.searchString,
      dynamicType: "calculator",
    },
  });
}

/**
 * Creates a UrlbarResult for a global action result.
 *
 * @param {object} options
 * @param {Array} [options.actionsResults]
 *   An array of action descriptors.
 * @param {string} [options.query]
 *  The query passed to actions when they run.
 *  It is "" for contextual search, otherwise it is queryContext.searchString.
 * @param {number} [options.inputLength]
 *  Original input length.
 * @param {boolean} [options.showOnboardingLabel]
 *   Whether the “press Tab” hint should appear.
 * @returns {UrlbarResult}
 */
function makeGlobalActionsResult({
  actionsResults,
  query,
  inputLength,
  showOnboardingLabel = false,
}) {
  const payload = {
    actionsResults,
    dynamicType: "actions",
    query,
    input: query,
    inputLength,
    showOnboardingLabel,
  };

  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.DYNAMIC,
    source: UrlbarShared.RESULT_SOURCE.ACTIONS,
    providerName: "UrlbarProviderGlobalActions",
    payload,
  });
}

/**
 * Checks that the results returned by a UrlbarParentController match those in
 * the param `matches`.
 *
 * @param {object} options Options for the check.
 * @param {UrlbarQueryContext} options.context
 *   The context for this query.
 * @param {string} [options.incompleteSearch]
 *   A search will be fired for this string and then be immediately canceled by
 *   the query in `context`.
 * @param {string} [options.autofilled]
 *   The autofilled value in the first result.
 * @param {string} [options.completed]
 *   The value that would be filled if the autofill result was confirmed.
 *   Has no effect if `autofilled` is not specified.
 * @param {object} [options.conditionalPayloadProperties]
 *   An object mapping payload property names to objects
 *   { optional, ignore, custom }.
 *   See the code below.
 * @param {Array} options.matches
 *   An array of UrlbarResults.
 */
async function check_results({
  context,
  incompleteSearch,
  autofilled,
  completed,
  matches = [],
  conditionalPayloadProperties = {},
} = {}) {
  if (!context) {
    return;
  }

  // At this point frecency could still be updating due to latest pages
  // updates.
  // This is not a problem in real life, but autocomplete tests should
  // return reliable resultsets, thus we have to wait.
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  const controller = UrlbarTestUtils.newMockController({
    input: {
      isPrivate: context.isPrivate,
      getSearchSource() {
        return "dummy-search-source";
      },
      window: {
        location: {
          href: AppConstants.BROWSER_CHROME_URL,
        },
      },
    },
  });
  controller.setView({
    get visibleResults() {
      return context.results;
    },
    controller: {
      removeResult() {},
    },
  });

  if (incompleteSearch) {
    let incompleteContext = createContext(incompleteSearch, {
      isPrivate: context.isPrivate,
    });
    controller.startQuery(incompleteContext);
  }
  await controller.startQuery(context);

  if (autofilled) {
    Assert.ok(context.results[0], "There is a first result.");
    Assert.ok(
      context.results[0].autofill,
      "The first result is an autofill result"
    );
    Assert.equal(
      context.results[0].autofill.value,
      autofilled,
      "The correct value was autofilled."
    );
    if (completed) {
      Assert.equal(
        context.results[0].payload.url,
        completed,
        "The completed autofill value is correct."
      );
    }
  }
  if (context.results.length != matches.length) {
    info("Actual results: " + JSON.stringify(context.results));
  }
  Assert.equal(
    context.results.length,
    matches.length,
    "Found the expected number of results."
  );

  let propertiesToCheck = {
    type: {},
    source: {},
    heuristic: {},
    isBestMatch: { map: v => !!v },
    providerName: { optional: true },
    suggestedIndex: { optional: true },
    isSuggestedIndexRelativeToGroup: { optional: true, map: v => !!v },
    exposureTelemetry: { optional: true },
    isRichSuggestion: { optional: true },
    richSuggestionIconVariation: { optional: true },
  };

  // A validator object to check optional date payload properties. If the
  // expected date is undefined, the actual date won't be checked. If the
  // expected date is zero, the actual date is asserted to be falsey.
  let optionalDateValidator = {
    optional: true,
    custom(resultIndex, actualResult, payloadKey) {
      if (matches[resultIndex].payload[payloadKey] === 0) {
        Assert.ok(
          !actualResult.payload[payloadKey],
          `result.payload.${payloadKey} should be falsey at result index ${resultIndex}`
        );
        return true;
      }
      return false;
    },
  };

  // Payload properties to conditionally check. Properties not specified here
  // will always be checked. For each entry in this object, the key is the
  // payload property name and the value is a validator object that can have the
  // following keys, each optional:
  //
  // {Function} custom
  //   A function called to validate the payload property. It will be called
  //   like: `custom(resultIndex, actualResult, payloadKey)`
  //   It will be called before any other validation is performed for the given
  //   payload property. It should return true if validation should stop or
  //   false if it should continue as usual.
  // {boolean} ignore
  //   Whether the payload property should always be ignored.
  // {boolean} optional
  //   When true, the payload property will be ignored if it's not in the
  //   payload of the expected result.
  conditionalPayloadProperties = {
    bookmarkDateMs: optionalDateValidator,
    frecency: { optional: true },
    lastVisit: optionalDateValidator,
    // `suggestionObject` is only used for dismissing Suggest Rust results, and
    // important properties in this object are reflected in the top-level
    // payload object, so ignore it. There are Suggest tests specifically for
    // dismissals that indirectly test the important aspects of this property.
    suggestionObject: { ignore: true },
    // Set by the providers manager on dynamic results, not by their provider.
    viewTemplate: { optional: true },
    viewUpdate: { optional: true },
    ...conditionalPayloadProperties,
  };

  for (let i = 0; i < matches.length; i++) {
    let actual = context.results[i];

    let actualJsonable = { payload: actual.payload };
    for (let key of Object.keys(propertiesToCheck)) {
      actualJsonable[key] = actual[key];
    }

    let expected = matches[i];
    info(
      `Comparing results at index ${i}:` +
        " actual=" +
        JSON.stringify(actualJsonable) +
        " expected=" +
        JSON.stringify(expected)
    );

    for (let [key, { optional, map }] of Object.entries(propertiesToCheck)) {
      if (!optional || expected[key] !== undefined) {
        map ??= v => v;
        Assert.equal(
          map(actual[key]),
          map(expected[key]),
          `result.${key} at result index ${i}`
        );
      }
    }

    if (
      actual.type == UrlbarShared.RESULT_TYPE.SEARCH &&
      actual.source == UrlbarShared.RESULT_SOURCE.SEARCH &&
      actual.providerName == "UrlbarProviderHeuristicFallback"
    ) {
      expected.payload.icon = SEARCH_GLASS_ICON;
    }

    if (actual.payload?.url) {
      try {
        const payloadUrlProtocol = new URL(actual.payload.url).protocol;
        if (
          !UrlbarShared.PROTOCOLS_WITH_ICONS.includes(payloadUrlProtocol) &&
          actual.source != UrlbarShared.RESULT_SOURCE.OTHER_LOCAL
        ) {
          expected.payload.icon = UrlbarShared.ICON.DEFAULT;
        }
      } catch (e) {
        console.error(e);
      }
    }

    if (expected.payload) {
      let expectedKeys = new Set(Object.keys(expected.payload));
      let actualKeys = new Set(Object.keys(actual.payload));

      for (let key of actualKeys.union(expectedKeys)) {
        let condition = conditionalPayloadProperties[key];

        if (condition?.custom?.(i, actual, key)) {
          // The custom assertion consumed this assertion.
          continue;
        }

        if (
          condition?.ignore ||
          (condition?.optional && !expected.payload.hasOwnProperty(key))
        ) {
          continue;
        }

        Assert.deepEqual(
          actual.payload[key],
          expected.payload[key],
          `result.payload.${key} at result index ${i}`
        );
      }
    }
  }
}

/**
 * Reads a single column from moz_origins for the origin that matches the url.
 *
 * @param {string} url
 *   A URL whose origin row should be looked up.
 * @param {string} column
 *   The column name to read from moz_origins.
 */
async function getOriginColumn(url, column) {
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.executeCached(
    `SELECT o.${column}
     FROM moz_origins o
     JOIN moz_places h ON h.origin_id = o.id
     WHERE h.url_hash = hash(:url) AND h.url = :url
     LIMIT 1`,
    { url }
  );
  if (!rows.length) {
    return undefined;
  }
  return rows[0].getResultByIndex(0);
}

/**
 * Returns the frecency of an origin.
 *
 * @param   {string} prefix
 *          The origin's prefix, e.g., "http://".
 * @param   {string} aHost
 *          The origin's host.
 * @returns {number} The origin's frecency.
 */
async function getOriginFrecency(prefix, aHost) {
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(
    `
    SELECT frecency
    FROM moz_origins
    WHERE prefix = :prefix AND host = :host
  `,
    { prefix, host: aHost }
  );
  Assert.equal(rows.length, 1);
  return rows[0].getResultByIndex(0);
}

/**
 * Returns the origin autofill frecency threshold.
 *
 * @returns {number}
 *          The threshold.
 */
async function getOriginAutofillThreshold() {
  return PlacesUtils.metadata.get("origin_frecency_threshold", 2.0);
}

/**
 * Checks that origins appear in a given order in the database.
 *
 * @param {string} host The "fixed" host, without "www."
 * @param {Array} prefixOrder The prefixes (scheme + www.) sorted appropriately.
 */
async function checkOriginsOrder(host, prefixOrder) {
  await PlacesUtils.withConnectionWrapper("checkOriginsOrder", async db => {
    let prefixes = (
      await db.execute(
        `SELECT prefix || iif(instr(host, "www.") = 1, "www.", "")
         FROM moz_origins
         WHERE host = :host OR host = "www." || :host
         ORDER BY ROWID ASC
        `,
        { host }
      )
    ).map(r => r.getResultByIndex(0));
    Assert.deepEqual(prefixes, prefixOrder);
  });
}

function daysAgo(days) {
  let date = new Date();
  date.setDate(date.getDate() - days);
  return date;
}
