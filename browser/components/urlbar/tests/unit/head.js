/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

var { UrlbarMuxer, UrlbarProvider, UrlbarQueryContext, UrlbarUtils } =
  ChromeUtils.importESModule("resource:///modules/UrlbarUtils.sys.mjs");

ChromeUtils.defineESModuleGetters(this, {
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  UrlbarController: "resource:///modules/UrlbarController.sys.mjs",
  UrlbarInput: "resource:///modules/UrlbarInput.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs: "resource:///modules/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarTokenizer: "resource:///modules/UrlbarTokenizer.sys.mjs",
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
        searchString,
      },
      properties
    )
  );
  UrlbarTokenizer.tokenize(context);
  return context;
}

/**
 * Waits for the given notification from the supplied controller.
 *
 * @param {UrlbarController} controller The controller to wait for a response from.
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
              controller.removeQueryListener(proxifiedObserver);
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
    controller.addQueryListener(proxifiedObserver);
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
 * @param {UrlbarUtils.PROVIDER_TYPE} type The provider type.
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
  UrlbarProvidersManager.registerProvider(provider);
  registerCleanupFunction(() =>
    UrlbarProvidersManager.unregisterProvider(provider)
  );
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
 * @returns {nsISearchEngine} The new engine.
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
  let engine = Services.search.getEngineByName(name);
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
 * @returns {nsISearchEngine} The new engine.
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
  let engine = Services.search.getEngineByName("Tail Suggestions");
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

async function addOpenPages(uri, count = 1, userContextId = 0) {
  for (let i = 0; i < count; i++) {
    await UrlbarProviderOpenTabs.registerOpenTab(
      uri.spec,
      userContextId,
      false
    );
  }
}

async function removeOpenPages(aUri, aCount = 1, aUserContextId = 0) {
  for (let i = 0; i < aCount; i++) {
    await UrlbarProviderOpenTabs.unregisterOpenTab(
      aUri.spec,
      aUserContextId,
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
    let oldDefaultEngine = await Services.search.getDefault();

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("browser.urlbar.suggest.searches");
      Services.prefs.clearUserPref("browser.urlbar.contextualSearch.enabled");
      Services.prefs.clearUserPref(
        "browser.search.separatePrivateDefault.ui.enabled"
      );
      Services.search.setDefault(
        oldDefaultEngine,
        Ci.nsISearchService.CHANGE_REASON_UNKNOWN
      );
    });

    Services.search.setDefault(
      engine,
      Ci.nsISearchService.CHANGE_REASON_UNKNOWN
    );
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
 * @param {string} options.title
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
 *   Where the results should be sourced from. See {@link UrlbarUtils.RESULT_SOURCE}.
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
    source = UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
  }
) {
  let result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.URL,
    source,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
      url: [uri, UrlbarUtils.HIGHLIGHT.TYPED],
      // Check against undefined so consumers can pass in the empty string.
      icon: [typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`],
      title: [title, UrlbarUtils.HIGHLIGHT.TYPED],
      tags: [tags, UrlbarUtils.HIGHLIGHT.TYPED],
      isBlockable:
        source == UrlbarUtils.RESULT_SOURCE.HISTORY ? true : undefined,
      blockL10n:
        source == UrlbarUtils.RESULT_SOURCE.HISTORY
          ? { id: "urlbar-result-menu-remove-from-history" }
          : undefined,
      helpUrl:
        source == UrlbarUtils.RESULT_SOURCE.HISTORY
          ? Services.urlFormatter.formatURLPref("app.support.baseURL") +
            "awesome-bar-result-menu"
          : undefined,
    })
  );

  result.heuristic = heuristic;
  return result;
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
  return new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.SEARCH,
    UrlbarUtils.RESULT_SOURCE.HISTORY,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
      engine: engineName,
      suggestion: [suggestion, UrlbarUtils.HIGHLIGHT.SUGGESTED],
      lowerCaseSuggestion: suggestion.toLocaleLowerCase(),
      isBlockable: true,
      blockL10n: { id: "urlbar-result-menu-remove-from-history" },
      helpUrl:
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
        "awesome-bar-result-menu",
    })
  );
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
  let payload = {
    title: [description, UrlbarUtils.HIGHLIGHT.TYPED],
    content: [content, UrlbarUtils.HIGHLIGHT.TYPED],
    keyword: [keyword, UrlbarUtils.HIGHLIGHT.TYPED],
    icon: [UrlbarUtils.ICON.EXTENSION],
  };
  let result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.OMNIBOX,
    UrlbarUtils.RESULT_SOURCE.ADDON,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, payload)
  );
  result.heuristic = heuristic;

  return result;
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
 *   A id of the userContext in which the tab is located.
 * @returns {UrlbarResult}
 */
function makeTabSwitchResult(
  queryContext,
  { uri, title, iconUri, userContextId }
) {
  return new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
    UrlbarUtils.RESULT_SOURCE.TABS,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
      url: [uri, UrlbarUtils.HIGHLIGHT.TYPED],
      title: [title, UrlbarUtils.HIGHLIGHT.TYPED],
      // Check against undefined so consumers can pass in the empty string.
      icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
      userContextId: [userContextId || 0],
    })
  );
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
  let result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.KEYWORD,
    UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
      title: [title ? title : uri, UrlbarUtils.HIGHLIGHT.TYPED],
      url: [uri, UrlbarUtils.HIGHLIGHT.TYPED],
      keyword: [keyword, UrlbarUtils.HIGHLIGHT.TYPED],
      input: [queryContext.searchString, UrlbarUtils.HIGHLIGHT.TYPED],
      postData: postData || null,
      icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    })
  );

  if (heuristic) {
    result.heuristic = heuristic;
  }
  return result;
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
    url: [uri, UrlbarUtils.HIGHLIGHT.TYPED],
    device: [device, UrlbarUtils.HIGHLIGHT.TYPED],
    // Check against undefined so consumers can pass in the empty string.
    icon: typeof iconUri != "undefined" ? iconUri : `page-icon:${uri}`,
    lastUsed: lastUsed * 1000,
  };

  // Check against undefined so consumers can pass in the empty string.
  if (typeof title != "undefined") {
    payload.title = [title, UrlbarUtils.HIGHLIGHT.TYPED];
  } else {
    payload.title = [uri, UrlbarUtils.HIGHLIGHT.TYPED];
  }

  let result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.REMOTE_TAB,
    UrlbarUtils.RESULT_SOURCE.TABS,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, payload)
  );

  return result;
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
 *   The type of the search result. Defaults to UrlbarUtils.RESULT_TYPE.SEARCH.
 * @param {number} [options.source]
 *   The source of the search result. Defaults to UrlbarUtils.RESULT_SOURCE.SEARCH.
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
    type = UrlbarUtils.RESULT_TYPE.SEARCH,
    source = UrlbarUtils.RESULT_SOURCE.SEARCH,
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
    engine: [engineName, UrlbarUtils.HIGHLIGHT.TYPED],
    suggestion: [suggestion, UrlbarUtils.HIGHLIGHT.SUGGESTED],
    tailPrefix,
    tail: [tail, UrlbarUtils.HIGHLIGHT.SUGGESTED],
    tailOffsetIndex,
    keyword: [
      alias,
      providesSearchMode
        ? UrlbarUtils.HIGHLIGHT.TYPED
        : UrlbarUtils.HIGHLIGHT.NONE,
    ],
    // Check against undefined so consumers can pass in the empty string.
    query: [
      typeof query != "undefined" ? query : queryContext.trimmedSearchString,
      UrlbarUtils.HIGHLIGHT.TYPED,
    ],
    icon: engineIconUri,
    providesSearchMode,
    inPrivateWindow,
    isPrivateEngine,
  };

  // Passing even an undefined URL in the payload creates a potentially-unwanted
  // displayUrl parameter, so we add it only if specified.
  if (uri) {
    payload.url = uri;
  }
  if (providerName == "TabToSearch") {
    if (searchUrlDomainWithoutSuffix.startsWith("www.")) {
      searchUrlDomainWithoutSuffix = searchUrlDomainWithoutSuffix.substring(4);
    }
    payload.searchUrlDomainWithoutSuffix = searchUrlDomainWithoutSuffix;
    payload.satisfiesAutofillThreshold = satisfiesAutofillThreshold;
    payload.isGeneralPurposeEngine = false;
  }

  if (providerName == "TokenAliasEngines") {
    payload.keywords = alias?.toLowerCase();
  }

  let result = new UrlbarResult(
    type,
    source,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, payload)
  );

  if (typeof suggestion == "string") {
    result.payload.lowerCaseSuggestion =
      result.payload.suggestion.toLocaleLowerCase();
    result.payload.trending = trending;
    result.isRichSuggestion = isRichSuggestion;
  }

  if (isRichSuggestion) {
    result.payload.icon =
      "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==";
    result.payload.description = "description";
  }

  if (providerName) {
    result.providerName = providerName;
  }

  result.heuristic = heuristic;
  return result;
}

/**
 * Creates a UrlbarResult for a history result.
 *
 * @param {UrlbarQueryContext} queryContext
 *   The context that this result will be displayed in.
 * @param {object} options Options for the result.
 * @param {string} options.title
 *   The page title.
 * @param {string} [options.fallbackTitle]
 *   The provider has capability to use the actual page title though,
 *   when the provider can’t get the page title, use this value as the fallback.
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
 * @returns {UrlbarResult}
 */
function makeVisitResult(
  queryContext,
  {
    title,
    fallbackTitle,
    uri,
    iconUri,
    providerName,
    tags = [],
    heuristic = false,
    source = UrlbarUtils.RESULT_SOURCE.HISTORY,
  }
) {
  let payload = {
    url: [uri, UrlbarUtils.HIGHLIGHT.TYPED],
  };

  if (title) {
    payload.title = [title, UrlbarUtils.HIGHLIGHT.TYPED];
  }

  if (fallbackTitle) {
    payload.fallbackTitle = [fallbackTitle, UrlbarUtils.HIGHLIGHT.TYPED];
  }

  if (
    !heuristic &&
    providerName != "AboutPages" &&
    providerName != "PreloadedSites" &&
    source == UrlbarUtils.RESULT_SOURCE.HISTORY
  ) {
    payload.isBlockable = true;
    payload.blockL10n = { id: "urlbar-result-menu-remove-from-history" };
    payload.helpUrl =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "awesome-bar-result-menu";
  }

  if (iconUri) {
    payload.icon = iconUri;
  } else if (
    iconUri === undefined &&
    source != UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL
  ) {
    payload.icon = `page-icon:${uri}`;
  }

  if (!heuristic && tags) {
    payload.tags = [tags, UrlbarUtils.HIGHLIGHT.TYPED];
  }

  let result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.URL,
    source,
    ...UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, payload)
  );

  if (providerName) {
    result.providerName = providerName;
  }

  result.heuristic = heuristic;
  return result;
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
  const result = new UrlbarResult(
    UrlbarUtils.RESULT_TYPE.DYNAMIC,
    UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
    {
      value,
      input: queryContext.searchString,
      dynamicType: "calculator",
    }
  );
  return result;
}

/**
 * Checks that the results returned by a UrlbarController match those in
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
 * @param {Array} options.matches
 *   An array of UrlbarResults.
 */
async function check_results({
  context,
  incompleteSearch,
  autofilled,
  completed,
  matches = [],
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
      onFirstResult() {
        return false;
      },
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

  // Payload properties to conditionally check. Properties not specified here
  // will always be checked.
  //
  // ignore:
  //   Always ignore the property.
  // optional:
  //   Ignore the property if it's not in the expected result.
  let conditionalPayloadProperties = {
    lastVisit: { optional: true },
    // `suggestionObject` is only used for dismissing Suggest Rust results, and
    // important properties in this object are reflected in the top-level
    // payload object, so ignore it. There are Suggest tests specifically for
    // dismissals that indirectly test the important aspects of this property.
    suggestionObject: { ignore: true },
  };

  for (let i = 0; i < matches.length; i++) {
    let actual = context.results[i];
    let expected = matches[i];
    info(
      `Comparing results at index ${i}:` +
        " actual=" +
        JSON.stringify(actual) +
        " expected=" +
        JSON.stringify(expected)
    );

    for (let [key, { optional, map }] of Object.entries(propertiesToCheck)) {
      if (!optional || expected.hasOwnProperty(key)) {
        map ??= v => v;
        Assert.equal(
          map(actual[key]),
          map(expected[key]),
          `result.${key} at result index ${i}`
        );
      }
    }

    if (
      actual.type == UrlbarUtils.RESULT_TYPE.SEARCH &&
      actual.source == UrlbarUtils.RESULT_SOURCE.SEARCH &&
      actual.providerName == "HeuristicFallback"
    ) {
      expected.payload.icon = SEARCH_GLASS_ICON;
    }

    if (actual.payload?.url) {
      try {
        const payloadUrlProtocol = new URL(actual.payload.url).protocol;
        if (
          !UrlbarUtils.PROTOCOLS_WITH_ICONS.includes(payloadUrlProtocol) &&
          actual.source != UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL
        ) {
          expected.payload.icon = UrlbarUtils.ICON.DEFAULT;
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
        if (
          condition?.ignore ||
          (condition?.optional && !expected.hasOwnProperty(key))
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
