/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * @import {BrowserSearchTelemetry} from "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs"
 * @import {ProvidersManager} from "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs"
 * @import {SearchEngine} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 * @import {SapLocation, SmartbarInput} from "moz-src:///browser/components/urlbar/content/SmartbarInput.mjs"
 * @import {UrlbarView} from "chrome://browser/content/urlbar/UrlbarView.mjs"
 * @import {WindowMode} from "moz-src:///browser/components/urlbar/content/UrlbarInputBase.mjs"
 * @import {SearchEngineInfo} from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 * @import {UrlbarLoadRequest} from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppProvidedConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  Interactions: "moz-src:///browser/components/places/Interactions.sys.mjs",
  ProvidersManager:
    "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarProviderSemanticHistorySearch:
    "moz-src:///browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs",
  UrlbarProviderTopSites:
    "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
  UrlbarQueryContext: "chrome://browser/content/urlbar/UrlbarQueryContext.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTelemetryUtils:
    "chrome://browser/content/urlbar/UrlbarTelemetryUtils.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.UrlbarShared.getLogger({ prefix: "Controller" })
);

/**
 * Serializes a search engine into a plain SearchEngineInfo object.
 *
 * @param {SearchEngine} engine
 *   The engine to serialize.
 * @returns {SearchEngineInfo}
 *   The serializable engine data.
 */
function engineToEngineInfo(engine) {
  return {
    name: engine.name,
    id: engine.id,
    isGeneralPurposeEngine: engine.isGeneralPurposeEngine,
    isConfigEngine: engine instanceof lazy.ConfigSearchEngine,
    isAppProvided: engine instanceof lazy.AppProvidedConfigEngine,
    isNewUntil: engine.isNew() ? engine.isNewUntil : "",
    hideOneOffButton: engine.hideOneOffButton,
    aliases: [...engine.aliases],
  };
}

/**
 * The address bar controller handles queries from the address bar, obtains
 * results and returns them to the UI for display.
 *
 * In the parent/child controller split, this class owns the bits that must
 * run in the parent process: the {@link ProvidersManager}, query lifecycle,
 * and parent-only telemetry. `UrlbarChildController` reaches it directly when
 * both run in the parent process (chrome `<moz-urlbar>`), or through the
 * `Urlbar` JSWindowActor pair when the child runs in a content process.
 *
 * Listeners may be added to listen for the results. They may support the
 * following methods which may be called when a query is run:
 *
 * - onQueryStarted(queryContext)
 * - onQueryResults(queryContext)
 * - onQueryCancelled(queryContext)
 * - onQueryFinished(queryContext)
 * - onQueryResultRemoved(resultId)
 * - onViewOpen()
 * - onViewClose()
 */
export class UrlbarParentController {
  /**
   * Resolves with the most recent handleAutofillReintegration() call's work,
   * including its Glean recording. The input fires re-integration without
   * awaiting it, so tests await this to sequence on the cleared block and the
   * recorded telemetry.
   *
   * @type {Promise<void>}
   */
  static _lastAutofillReintegrationPromise = Promise.resolve();

  /**
   * The paired UrlbarChildController, which registers itself via setChild().
   * Listener registration and notification dispatch live on it, keeping
   * dispatch on the side where the listeners (the view, the event bufferer)
   * live. The child is always set before any query runs.
   *
   * @type {UrlbarChildController}
   */
  #child = null;

  // The owning JSWindowActor, used to resolve the chrome window parent-side
  // (see `browserWindow`). Unlike the input and view, the browser window is a
  // parent-process object, so we don't reach through the content-side child
  // for it.
  #actor = null;

  // Whether the controller serves the address bar.
  #isAddressbar = false;

  /**
   * Initialises the controller from standalone data; the live input/view are
   * reached at runtime through the paired `UrlbarChildController`.
   *
   * @param {object} options
   *   The initial options for UrlbarParentController.
   * @param {string} options.sapName
   *   The search access point name, e.g. `urlbar`, `searchbar`.
   * @param {boolean} [options.isPrivate]
   *   Whether the controller serves a private-browsing input.
   * @param {object} [options.actor]
   *   The owning `UrlbarParent` JSWindowActor, used to resolve the chrome
   *   window. Omitted in unit tests.
   * @param {object} [options.manager]
   *   Optional fake providers manager to override the built-in providers manager.
   *   Intended for use in unit tests only.
   */
  constructor({ sapName, isPrivate = false, actor, manager }) {
    if (!sapName) {
      throw new Error("Missing options: sapName");
    }

    this.sapName = sapName;
    this.#isAddressbar = sapName == "urlbar";
    this.isPrivate = isPrivate;
    this.#actor = actor;

    /**
     * @type {ProvidersManager}
     */
    this.manager =
      manager || lazy.ProvidersManager.getInstanceForSap(this.sapName);

    this.engagementEvent = new TelemetryEvent(this);
    lazy.UrlbarProviderTopSites.addTopSitesListener(this.#topSitesListener);
    Services.obs.addObserver(this, "intl:app-locales-changed", true);
  }

  /**
   * The platform constant.
   *
   * @type {string}
   */
  get platform() {
    return AppConstants.platform;
  }

  /**
   * The input, owned by the paired `UrlbarChildController` and read through it
   * for the query-lifecycle and telemetry call sites that need it.
   *
   * @type {UrlbarInput}
   */
  get input() {
    return this.#child?.input;
  }

  /**
   * The chrome window the urlbar lives in, resolved parent-side from the actor.
   * Parent-side providers read this (icons, speculative connect, opening help
   * links), so it can't come from the content-side child.
   *
   * @type {ChromeWindow}
   */
  get browserWindow() {
    return this.#actor?.browsingContext?.topChromeWindow;
  }

  /**
   * Resolves the `<browser>` a `browserId` refers to. A content sender always
   * targets its own tab, so its `browserId` is ignored; only a chrome sender
   * resolves a pinned id globally.
   *
   * @param {?number} browserId
   *   The browser id a chrome sender pinned, if any.
   * @returns {?MozBrowser}
   *   The target `<browser>`, or null if there's nothing to resolve.
   */
  resolveTargetBrowser(browserId) {
    let browsingContext = this.#actor?.browsingContext;
    if (browsingContext?.isContent) {
      return browsingContext.top.embedderElement;
    }
    let target = /** @type {?CanonicalBrowsingContext} */ (
      browserId ? BrowsingContext.getCurrentTopByBrowserId(browserId) : null
    );
    return target?.embedderElement ?? null;
  }

  /**
   * The view.
   *
   * @type {UrlbarView}
   */
  get view() {
    return this.#child?.view;
  }

  /**
   * Notifies a result's provider that the result is about to be selected.
   * Mediates the view's access to the (parent-process) provider.
   *
   * @param {UrlbarResult} result The result being selected.
   * @param {Element} element The selected element.
   */
  onBeforeSelection(result, element) {
    this.manager
      .getProvider(result?.providerName)
      ?.tryMethod("onBeforeSelection", result, element);
  }

  /**
   * Notifies a result's provider that the result was selected. Mediates the
   * view's access to the (parent-process) provider.
   *
   * @param {UrlbarResult} result The selected result.
   * @param {Element} element The selected element.
   */
  onSelection(result, element) {
    this.manager
      .getProvider(result?.providerName)
      ?.tryMethod("onSelection", result, element);
  }

  /**
   * Runs a one-off query and returns its heuristic result. Mediates the
   * input's access to the (parent-process) providers manager, e.g. for
   * paste-and-go and drop-and-go where the input needs the heuristic result
   * without an open view.
   *
   * @param {UrlbarQueryContext} queryContext The query context to run.
   * @returns {Promise<UrlbarResult>} The heuristic result.
   */
  async getHeuristicResult(queryContext) {
    await this.manager.startQuery(queryContext);
    return queryContext.heuristicResult;
  }

  /**
   * Resolves the navigation for an Enter with no result available to pick,
   * entirely parent-side: it fetches the heuristic result for the typed value,
   * and if that fails (a corrupt profile) falls back to `uriFixup`. Both the
   * query and `uriFixup` are parent-only, and the target browser's per-tab data
   * and navigation epoch can't be read from a content urlbar, so this owns all
   * of it and hands the content side only what it needs to pick or load.
   *
   * The epoch guards against the browser navigating while the heuristic query
   * awaited: for a `current` load, a changed epoch means the user moved on, so
   * neither the pick nor the fixup load should happen.
   *
   * @param {object} details
   * @param {string} details.searchString
   *   The typed value to resolve.
   * @param {string} details.where
   *   Where the result will open, per `openTrustedLinkIn`.
   * @param {object} [details.searchMode]
   *   The input's search mode, if any.
   * @param {number} [details.browserId]
   *   The id of the browser committed at Enter; its per-tab data and navigation
   *   epoch are read here, defaulting to the selected browser.
   * @returns {Promise<object>}
   *   `{ heuristicResult }` to pick,
   *   `{ fixup: { url, postData: ?string, keywordAsSent } }` to load, or `{}`
   *   when the browser navigated in the meanwhile.
   */
  async resolveFallbackNavigation({
    searchString,
    where,
    searchMode,
    browserId,
  }) {
    // Increment rate denominator measuring how often the fallback path is hit.
    Glean.urlbar.heuristicResultMissing.addToDenominator(1);

    let { gBrowser } = this.browserWindow;
    let browser =
      this.resolveTargetBrowser(browserId) || gBrowser.selectedBrowser;
    // Capture the location change counter before awaiting, to verify below that
    // the browser didn't navigate in the meanwhile.
    let lastLocationChange =
      where == "current" ? browser.lastLocationChange : null;
    let navigated = () =>
      where == "current" && browser.lastLocationChange != lastLocationChange;

    let heuristicResult;
    try {
      let options = {
        allowAutofill: false,
        isPrivate: this.isPrivate,
        sapName: this.sapName,
        maxResults: 1,
        searchString,
        userContextId: parseInt(browser.getAttribute("usercontextid") || 0),
        tabGroup: gBrowser.getTabForBrowser(browser)?.group?.id ?? null,
        prohibitRemoteResults: true,
        providers: [
          "UrlbarProviderAliasEngines",
          "UrlbarProviderBookmarkKeywords",
          "UrlbarProviderHeuristicFallback",
        ],
      };
      if (searchMode) {
        options.searchMode = searchMode;
        if (searchMode.source) {
          options.sources = [searchMode.source];
        }
      }
      heuristicResult = await this.getHeuristicResult(
        new lazy.UrlbarQueryContext(options)
      );
      if (!heuristicResult) {
        throw new Error("There should always be an heuristic result");
      }
    } catch (ex) {
      // Something went wrong, we should always have a heuristic result,
      // otherwise it means we're not able to search at all, maybe because some
      // parts of the profile are corrupt. The urlbar should still allow to
      // search or visit the typed string, so that the user can look for help to
      // resolve the problem.

      // Increment rate numerator measuring how often the fallback path is hit.
      Glean.urlbar.heuristicResultMissing.addToNumerator(1);

      try {
        let flags =
          Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
          Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
        if (this.isPrivate) {
          flags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
        }
        let { preferredURI, postData, keywordAsSent } =
          Services.uriFixup.getFixupURIInfo(searchString, flags);
        return navigated()
          ? {}
          : {
              fixup: {
                url: preferredURI.spec,
                // Post data only happens if the default engine is POST (rare)
                postData: postData
                  ? lazy.UrlbarUtils.getPostDataString(postData)
                  : null,
                keywordAsSent,
              },
            };
      } catch (fixupEx) {
        // uriFixup can throw; swallow it so the resolve never rejects.
        console.error(fixupEx);
        return {};
      }
    }

    return navigated() ? {} : { heuristicResult };
  }

  /**
   * Takes a query context and starts the query based on the user input.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   * @returns {Promise<UrlbarQueryContext>}
   *   The updated query context.
   */
  async startQuery(queryContext) {
    // Cancel any running query.
    this.cancelQuery();

    // Wrap the external queryContext, to track a unique object, in case
    // the external consumer reuses the same context multiple times.
    // This also allows to add properties without polluting the context.
    // Note this can't be null-ed or deleted once a query is done, because it's
    // used by #dismissSelectedResult and handleKeyNavigation, that can run after
    // a query is cancelled or finished.
    let contextWrapper = (this._lastQueryContextWrapper = { queryContext });

    queryContext.lastResultCount = 0;
    queryContext.firstTimerId =
      Glean.urlbar.autocompleteFirstResultTime.start();
    queryContext.sixthTimerId =
      Glean.urlbar.autocompleteSixthResultTime.start();

    // For proper functionality we must ensure this notification is fired
    // synchronously, as soon as startQuery is invoked, but after any
    // notifications related to the previous query.
    this.notify(lazy.UrlbarShared.NOTIFICATIONS.QUERY_STARTED, queryContext);
    await this.manager.startQuery(queryContext, this);

    // If the query has been cancelled, onQueryFinished was notified already.
    // Note this._lastQueryContextWrapper may have changed in the meanwhile.
    if (
      contextWrapper === this._lastQueryContextWrapper &&
      !contextWrapper.done
    ) {
      contextWrapper.done = true;
      // TODO (Bug 1549936) this is necessary to avoid leaks in PB tests.
      this.manager.cancelQuery(queryContext);
      this.notify(lazy.UrlbarShared.NOTIFICATIONS.QUERY_FINISHED, queryContext);
    }

    return queryContext;
  }

  /**
   * Records an engagement shipped by a message-path child collector. The
   * counterpart to the proxy's `recordEngagement()`: deserializes the payload
   * and hands it to the recorder. On the direct path the child collector isn't
   * used, so this isn't called.
   *
   * @param {object} wire
   *   The payload from `UrlbarTelemetryUtils.recordedEngagementToWire()`.
   */
  recordEngagement(wire) {
    this.engagementEvent.recordFromChild(
      lazy.UrlbarTelemetryUtils.recordedEngagementFromWire(wire)
    );
  }

  /**
   * Resets the recorder's cross-session telemetry state. The counterpart to the
   * proxy's `resetEngagement()`.
   */
  resetEngagement() {
    this.engagementEvent.reset();
  }

  /**
   * Records a bounce a message-path child collector triggered. The counterpart
   * to the proxy's `handleBounceTrigger()`.
   *
   * @param {object} payload
   *   `{snapshot, startTime, browsingContextId, contentData}`.
   * @returns {Promise<void>}
   */
  handleBounceTrigger(payload) {
    return this.engagementEvent.handleBounceTrigger(payload);
  }

  /**
   * Caches the live browser behind a bounce the message-path collector is
   * tracking, so `handleBounceTrigger()` can resolve it once the tab is gone.
   * The counterpart to the proxy's `trackBounceBrowser()`.
   *
   * @param {number} browserId
   *   The bounce browser's stable browser id.
   */
  trackBounceBrowser(browserId) {
    this.engagementEvent.trackBounceBrowser(browserId);
  }

  /**
   * Records entry into a search mode. The parent-side counterpart to the
   * content-side `BrowserSearchTelemetry.recordSearchMode()` call.
   *
   * @param {object} searchMode
   *   The search mode being entered. See `UrlbarInput.setSearchMode`.
   */
  recordSearchMode(searchMode) {
    try {
      lazy.BrowserSearchTelemetry.recordSearchMode(searchMode);
    } catch (ex) {
      console.error(ex);
    }
  }

  /**
   * Records a backspace over an autofilled URL, which past a threshold blocks
   * autofill for it. The bookkeeping and its Places write are parent-state, so
   * the input hands the URL over here.
   *
   * @param {string} url
   *   The autofill result URL whose backspace is being recorded.
   */
  recordAutofillBackspace(url) {
    lazy.UrlbarUtils.recordAutofillBackspace(url);
  }

  /**
   * Records that the user deleted a whole autofilled value.
   */
  recordAutofillDeletion() {
    Glean.urlbar.autofillDeletion.add(1);
  }

  /**
   * Dismisses an autofilled URL on the user's behalf, blocking the autofill
   * pairing or removing the URL from history. Async so callers can await the
   * write before re-running their query on either transport.
   *
   * @param {string} url
   *   The dismissed autofill result's URL.
   * @param {"dismiss" | "forget"} action
   *   "dismiss" blocks the autofill pairing for a period of time.
   *   "forget" removes the URL from history entirely.
   */
  async dismissAutofill(url, action) {
    if (action != "dismiss" && action != "forget") {
      throw new Error(`Unknown autofill dismissal action: ${action}`);
    }

    Glean.urlbarAutofill.inputContextMenuDismissal[action].add(1);

    await lazy.UrlbarUtils.dismissAutofill(url, {
      removeFromHistory: action == "forget",
    });
  }

  /**
   * Clears the backspace bookkeeping for an autofilled URL the user accepted.
   * The bookkeeping is parent state, so the input hands the URL over here.
   *
   * @param {string} url
   *   The accepted autofill result's URL.
   */
  clearAutofillBackspaceEntryForUrl(url) {
    lazy.UrlbarUtils.clearAutofillBackspaceEntryForUrl(url);
  }

  /**
   * Re-integrates an autofill URL the user navigated to anyway: clears its
   * autofill block and records how long the block had been in place. Both the
   * block state and Glean are parent-side, so the input only decides when a
   * navigation counts as a re-integration and hands the URL over here.
   *
   * @param {string} url
   *   The URL being re-integrated.
   */
  handleAutofillReintegration(url) {
    UrlbarParentController._lastAutofillReintegrationPromise =
      this.#doHandleAutofillReintegration(url).catch(console.error);
  }

  async #doHandleAutofillReintegration(url) {
    let { wasBlocked, level, backspaceBlock } =
      await lazy.UrlbarUtils.reintegrateAutofill(url);
    if (!wasBlocked) {
      return;
    }

    Glean.urlbarAutofill.reintegration[level].add(1);

    // For backspace-induced blocks, record the unblock delay: fast unblocks
    // suggest the original block was accidental.
    if (backspaceBlock?.level === level) {
      Glean.urlbarAutofill.reintegrationAfterBackspace[
        level
      ].accumulateSingleSample(Date.now() - backspaceBlock.blockedAt);
    }
  }

  /**
   * Records a visit to an engine's search form. The parent-side counterpart to
   * the content-side `BrowserSearchTelemetry.recordSearchForm()` call; the
   * engine is shipped by id and resolved here, and the source is this
   * controller's SAP.
   *
   * @param {string} engineId
   *   The id of the engine whose search form was visited.
   */
  recordSearchForm(engineId) {
    let engine = lazy.SearchService.getEngineById(engineId);
    lazy.BrowserSearchTelemetry.recordSearchForm(engine, this.sapName);
  }

  /**
   * Records that a search is being loaded: bumps the search-count prefs,
   * informs ASRouter, records search telemetry and adds the search query
   * to form history. The parent-side counterpart to the content-side
   * `_recordSearch()`.
   *
   * @param {object} options
   * @param {string} options.engineId
   *   The id of the engine handling the search.
   * @param {string} options.query
   * @param {string} options.searchSource
   *   Where the search originated from.
   * @param {object} options.details
   *   The search action details, per `BrowserSearchTelemetry.recordSearch()`.
   * @param {number} [options.browserId]
   *   The id of the browser where the search is being opened; defaults to the
   *   selected browser.
   * @param {boolean} [options.opensInPrivateWindow]
   *   Whether the search opens in a new private window, in which case it's
   *   not added to form history. If this is false but the current window
   *   is private, it's not added either.
   */
  recordSearch({
    engineId,
    query,
    searchSource,
    details,
    browserId,
    opensInPrivateWindow,
  }) {
    let browser =
      this.resolveTargetBrowser(browserId) ||
      this.browserWindow.gBrowser.selectedBrowser;

    // Record when the user uses the search bar to be used for message
    // targeting. This is arbitrarily capped at 100, only to prevent the number
    // from growing infinitely.
    const totalSearches = Services.prefs.getIntPref(
      "browser.search.totalSearches"
    );
    if (totalSearches < 100) {
      Services.prefs.setIntPref(
        "browser.search.totalSearches",
        totalSearches + 1
      );
    }

    // Record when the user uses the search bar so SearchWidgetTracker can
    // remove the search bar when it hasn't been used in a long time.
    if (this.sapName == "searchbar") {
      Services.prefs.setStringPref(
        "browser.search.widget.lastUsed",
        new Date().toISOString()
      );
    }

    lazy.ASRouter.sendTriggerMessage({
      browser,
      id: "onSearch",
      context: {
        isSuggestion: details.isSuggestion || false,
        searchSource,
        isOneOff: details.isOneOff,
      },
    });

    lazy.BrowserSearchTelemetry.recordSearch(
      browser,
      engineId,
      searchSource,
      details
    );

    let engine = lazy.SearchService.getEngineById(engineId);
    if (engine) {
      lazy.UrlbarUtils.addToFormHistory(
        this.isPrivate || opensInPrivateWindow,
        query,
        engine.name
      ).catch(console.error);
    }
  }

  /**
   * Records a search that opens in a new tab, against that tab's browser. The
   * load opens the tab right after this call; TabOpen fires synchronously, so
   * the next-opened tab is the search tab. Reaching its browser is parent-only.
   *
   * @param {Parameters<typeof this.recordSearch>[0]} searchData
   *   The data for `recordSearch`; its `browserId` is filled in here.
   */
  recordSearchInOpenedTab(searchData) {
    this.browserWindow.gBrowser.tabContainer.addEventListener(
      "TabOpen",
      tabEvent => {
        this.recordSearch({
          ...searchData,
          browserId: tabEvent.target.linkedBrowser.browserId,
        });
      },
      { once: true }
    );
  }

  /**
   * Runs the single-word keyword URI fixup DNS check for a search picked in the
   * address bar. When a single word is turned directly into a search (bypassing
   * the docShell), this reproduces the docShell's DNS lookup that offers to
   * visit the word as a host instead (see `gKeywordURIFixup`). Routed here
   * because it shows a per-window infobar the content input can't, and it needs
   * the parent's search service to fix up the string.
   *
   * @param {string} searchString
   *   The string being searched.
   * @param {?number} browserId
   *   The browser the search loads into, or null for the selected browser.
   */
  checkKeywordURIFixup(searchString, browserId) {
    let browser =
      this.resolveTargetBrowser(browserId) ||
      this.browserWindow.gBrowser.selectedBrowser;
    let fixupInfo = lazy.UrlbarUtils.getURIFixupInfo(
      searchString,
      this.isPrivate
    );
    if (fixupInfo) {
      this.browserWindow.gKeywordURIFixup.check(browser, fixupInfo);
    }
  }

  /**
   * Cancels an in-progress query. Note, queries may continue running if they
   * can't be cancelled.
   */
  cancelQuery() {
    // If the query finished already, don't handle cancel.
    if (!this._lastQueryContextWrapper || this._lastQueryContextWrapper.done) {
      return;
    }

    this._lastQueryContextWrapper.done = true;

    let { queryContext } = this._lastQueryContextWrapper;

    Glean.urlbar.autocompleteFirstResultTime.cancel(queryContext.firstTimerId);
    queryContext.firstTimerId = 0;
    Glean.urlbar.autocompleteSixthResultTime.cancel(queryContext.sixthTimerId);
    queryContext.sixthTimerId = 0;

    this.manager.cancelQuery(queryContext);
    this.notify(lazy.UrlbarShared.NOTIFICATIONS.QUERY_CANCELLED, queryContext);
    this.notify(lazy.UrlbarShared.NOTIFICATIONS.QUERY_FINISHED, queryContext);
  }

  /**
   * Receives results from a query.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   */
  receiveResults(queryContext) {
    if (queryContext.lastResultCount < 1 && queryContext.results.length >= 1) {
      Glean.urlbar.autocompleteFirstResultTime.stopAndAccumulate(
        queryContext.firstTimerId
      );
      queryContext.firstTimerId = 0;
    }
    if (queryContext.lastResultCount < 6 && queryContext.results.length >= 6) {
      Glean.urlbar.autocompleteSixthResultTime.stopAndAccumulate(
        queryContext.sixthTimerId
      );
      queryContext.sixthTimerId = 0;
    }

    if (queryContext.firstResultChanged) {
      this.notify(
        lazy.UrlbarShared.NOTIFICATIONS.QUERY_FIRST_RESULT,
        queryContext
      );
    }

    this.notify(lazy.UrlbarShared.NOTIFICATIONS.QUERY_RESULTS, queryContext);
    // Update lastResultCount after notifying, so the view can use it.
    queryContext.lastResultCount = queryContext.results.length;
  }

  /**
   * Sets the paired UrlbarChildController, which owns listener registration
   * and notification dispatch. It must be set before any query runs, since
   * the query lifecycle notifies through it.
   *
   * @param {object} child The paired UrlbarChildController.
   */
  setChild(child) {
    this.#child = child;
  }

  /**
   * Opens a search engine result page (SERP) for the specified
   * search engine and search query.
   *
   * Does not record telemetry, so it should be recorded by the caller.
   *
   * @param {string} engineId
   * @param {string} searchTerms
   * @param {string} where
   * @param {boolean} [inBackground]
   * @param {number} [browserId]
   *   The target browser's id. Only used if `where == current` and the call
   *   isn't coming from a content process. If it's not specified and
   *   `where == current`, the currently selected tab is used.
   */
  openSERP(
    engineId,
    searchTerms,
    where,
    inBackground = false,
    browserId = null
  ) {
    let searchEngine = lazy.SearchService.getEngineById(engineId);

    let [url, postData] = lazy.UrlbarUtils.getSearchQueryUrl(
      searchEngine,
      searchTerms
    );

    this.browserWindow.openTrustedLinkIn(url, where, {
      inBackground,
      postData,
      targetBrowser:
        where == "current" ? this.resolveTargetBrowser(browserId) : null,
      globalHistoryOptions: {
        triggeringSource: this.sapName,
        triggeringSearchEngine: searchEngine.name,
      },
    });
  }

  /**
   * Opens the homepage (also known as searchForm) of the
   * specified search engine and records telemetry.
   *
   * @param {string} engineId
   * @param {string} where
   * @param {boolean} [inBackground]
   * @param {number} [browserId]
   *   The target browser's id. Only used if `where == current` and the call
   *   isn't coming from a content process. If it's not specified and
   *   `where == current`, the currently selected tab is used.
   */
  openSearchForm(engineId, where, inBackground = false, browserId = null) {
    let searchEngine = lazy.SearchService.getEngineById(engineId);
    lazy.BrowserSearchTelemetry.recordSearchForm(searchEngine, this.sapName);
    let url = searchEngine.searchForm;
    this.browserWindow.openTrustedLinkIn(url, where, {
      inBackground,
      targetBrowser:
        where == "current" ? this.resolveTargetBrowser(browserId) : null,
    });
  }

  /**
   * Returns the icon URL of the engine with the given id. This can be a blob
   * URL, which only resolves in this process, so UrlbarParent serializes it
   * before handing it to another process.
   *
   * @param {string} engineId
   * @returns {Promise<?string>}
   *   The icon URL, or null if the engine or its icon could not be found.
   */
  async getEngineIconURL(engineId) {
    let engine = lazy.SearchService.getEngineById(engineId);
    if (!engine) {
      lazy.logger.warn(`No engine found for id ${engineId}`);
      return null;
    }
    return (await engine.getIconURL()) ?? null;
  }

  /**
   * Marks the engine with the given id as used if it's a config engine that
   * hasn't been used already.
   *
   * @param {string} engineId
   */
  markEngineAsUsed(engineId) {
    let engine = lazy.SearchService.getEngineById(engineId);
    if (!engine) {
      lazy.logger.warn(`No engine found for id ${engineId}`);
      return;
    }
    if (engine instanceof lazy.ConfigSearchEngine && !engine.hasBeenUsed) {
      engine.markAsUsed();
    }
  }

  /**
   * Tries to initialize a speculative connection on a result.
   * Speculative connections are only supported for a subset of all the results.
   *
   * Speculative connect to:
   *  - Search engine heuristic results
   *  - autofill results
   *  - http/https results
   *
   * @param {UrlbarResult} result The result to speculative connect to.
   * @param {UrlbarQueryContext} context The queryContext
   * @param {string} reason Reason for the speculative connect request.
   */
  speculativeConnect(result, context, reason) {
    // browserWindow is null only during teardown. Never speculative connect in
    // private contexts.
    if (!this.browserWindow || context.isPrivate || !context.results.length) {
      return;
    }

    switch (reason) {
      case "resultsadded": {
        // We should connect to an heuristic result, if it exists. The result
        // passed for this reason is always the first one, so its own flags
        // identify it.
        if (result.heuristic || result.autofill) {
          if (result.type == lazy.UrlbarShared.RESULT_TYPE.SEARCH) {
            // Speculative connect only if search suggestions are enabled.
            if (
              (lazy.UrlbarPrefs.get("suggest.searches") ||
                context.sapName == "searchbar") &&
              lazy.UrlbarPrefs.get("browser.search.suggest.enabled")
            ) {
              let engine = lazy.SearchService.getEngineByName(
                result.payload.engine
              );
              lazy.UrlbarUtils.setupSpeculativeConnection(
                engine,
                this.browserWindow
              );
            }
          } else if (result.autofill) {
            const { url } = lazy.UrlbarUtils.getUrlFromResult(result);
            if (!url) {
              return;
            }

            lazy.UrlbarUtils.setupSpeculativeConnection(
              url,
              this.browserWindow
            );
          }
        }
        return;
      }
      case "mousedown": {
        const { url } = lazy.UrlbarUtils.getUrlFromResult(result);
        if (!url) {
          return;
        }

        // On mousedown, connect only to http/https urls.
        if (url.startsWith("http")) {
          lazy.UrlbarUtils.setupSpeculativeConnection(url, this.browserWindow);
        }
        return;
      }
      default: {
        throw new Error("Invalid speculative connection reason");
      }
    }
  }

  /**
   * Loads a URL in the browser window resolved from the actor. The content
   * child supplies the serializable load parameters; the parent fills in the
   * ones that reference the chrome window (the target browser and initiating
   * document) and reports back whether the load failed in a way that should
   * revert the input.
   *
   * @param {object} loadData
   * @param {UrlbarLoadRequest} loadData.loadRequest
   *   What to load.
   * @param {string} loadData.where
   *   Where to open, per `openTrustedLinkIn`.
   * @param {object} loadData.params
   *   The serializable `openTrustedLinkIn` params.
   * @param {number} [loadData.browserId]
   *   The target browser's id; defaults to the selected browser.
   * @param {string} [loadData.userTypedValue]
   *   The value to record as the browser's typed value, for a `current` load.
   * @returns {{reverted: boolean, browserId: number}}
   *   Whether the load threw without showing an error page, so the input should
   *   revert, and the id of the browser the load resolved to. The latter is not
   *   an echo of the optional `browserId` param: a default `current` load omits
   *   it and the target is resolved here, so this is how the child learns which
   *   browser to hand `focusBrowser` on the deferred-Enter keyup -- a
   *   content-process input can't resolve the selected browser itself.
   */
  loadURL({ loadRequest, where, params, browserId, userTypedValue }) {
    let browser =
      this.resolveTargetBrowser(browserId) ||
      this.browserWindow.gBrowser.selectedBrowser;

    let { url, postData } = lazy.UrlbarUtils.loadRequestToUrl(loadRequest);
    if (!url) {
      return { reverted: true, browserId: browser.browserId };
    }
    params.postData = postData;

    if (this.#isAddressbar) {
      this.#prepareAddressbarLoad({
        browser,
        url,
        where,
        params,
        userTypedValue,
      });
    }

    if (where == "current") {
      params.targetBrowser = browser;
    } else {
      params.initiatingDoc = this.browserWindow.document;
    }

    // Focus the content area before triggering loads, since if the load
    // occurs in a new tab, we want focus to be restored to the content area
    // when the current tab is re-selected.
    if (!params.avoidBrowserFocus) {
      browser.focus();
    }

    try {
      this.browserWindow.openTrustedLinkIn(url, where, params);
    } catch (ex) {
      // This load can throw in certain cases; unless an error page was shown,
      // the input should revert to the loaded URL.
      return {
        reverted: ex.result != Cr.NS_ERROR_LOAD_SHOWED_ERRORPAGE,
        browserId: browser.browserId,
      };
    }
    return { reverted: false, browserId: browser.browserId };
  }

  /**
   * Focuses the browser a deferred-Enter load targeted, once the load's keyup
   * fires, but only if it is still the selected browser. Reaching the browser
   * element and comparing it against the selection is parent-only work.
   *
   * @param {number} [browserId]
   *   The browser the load resolved to, as returned by `loadURL`.
   * @returns {{focused: boolean}}
   *   Whether the browser was focused, so the child can keep the domain name
   *   visible.
   */
  focusBrowser(browserId) {
    let browser = this.resolveTargetBrowser(browserId);
    let { selectedBrowser } = this.browserWindow.gBrowser;
    if (browser && browser == selectedBrowser) {
      selectedBrowser.focus();
      return { focused: true };
    }
    return { focused: false };
  }

  /**
   * Switches to a tab already showing the URL (or opens it), doing the
   * tabbrowser bookkeeping and the follow-up history/open-tab writes that only
   * the parent can: reading the previous tab and its split view, closing the
   * previous tab if it was left empty, recording input history on success, and
   * unregistering a stale open-tab entry on a miss.
   *
   * @param {object} loadData
   * @param {string} loadData.url
   *   The URL to switch to.
   * @param {string} loadData.searchString
   *   The search string that produced the result, for input history.
   * @param {number} [loadData.userContextId]
   *   The id of the container the target tab belongs to; 0 for the default
   *   container.
   * @param {string} [loadData.tabGroup]
   *   The id of the tab group the target tab belongs to, or null for none.
   * @param {boolean} [loadData.heuristic]
   *   Whether the result was the heuristic one.
   */
  switchToTab({
    url,
    searchString,
    userContextId = 0,
    tabGroup = null,
    heuristic = false,
  }) {
    let { gBrowser } = this.browserWindow;
    let prevTab = gBrowser.selectedTab;
    let activeSplitView = prevTab.splitview;
    let switched = this.browserWindow.switchToTabHavingURI(
      Services.io.newURI(url),
      true,
      {
        adoptIntoActiveWindow: lazy.UrlbarPrefs.get(
          "switchTabs.adoptIntoActiveWindow"
        ),
      },
      lazy.UrlbarShared.isNonPrivateUserContextId(userContextId)
        ? userContextId
        : null,
      activeSplitView
    );

    if (switched) {
      if (!activeSplitView && prevTab.isEmpty) {
        gBrowser.removeTab(prevTab);
      }
      if (!heuristic) {
        this.addToInputHistory(url, searchString);
      }
      return;
    }

    // TODO (Bug 1865757): We should not show a "switchtotab" result for tabs
    // that are not currently open. Find out why tabs are not being properly
    // unregistered when they are being closed.
    console.error(`Tried to switch to non-existent tab: ${url}`);
    lazy.UrlbarProviderOpenTabs.unregisterOpenTab(
      url,
      userContextId,
      tabGroup,
      this.isPrivate
    );
  }

  /**
   * Adds a (url, input) tuple to the input history that drives adaptive
   * results. Places writes only happen in the parent, so callers route here.
   * No-ops in private browsing.
   *
   * The write runs in the background and a failure is only reported to the
   * console.
   *
   * @param {string} url
   *   The picked URL.
   * @param {string} input
   *   The search string to associate with it.
   * @param {object} [options]
   * @param {boolean} [options.whenReady]
   *   Whether to wait for the URL to land in moz_places before writing, for a
   *   URL that only the imminent navigation will record a visit for. The
   *   observer this needs is registered synchronously, so the caller can start
   *   the navigation right after.
   */
  addToInputHistory(url, input, { whenReady = false } = {}) {
    if (this.isPrivate) {
      return;
    }
    let promise = whenReady
      ? lazy.UrlbarUtils.addToInputHistoryWhenReady(url, input)
      : lazy.UrlbarUtils.addToInputHistory(url, input);
    promise.catch(console.error);
  }

  /**
   * Records the address bar bookkeeping on the target browser before the load,
   * touching state that only exists on the chrome window.
   *
   * @param {object} loadData
   * @param {object} loadData.browser
   *   The target XUL browser.
   * @param {string} loadData.url
   *   The URL being loaded.
   * @param {string} loadData.where
   *   Where to open, per `openTrustedLinkIn`.
   * @param {object} loadData.params
   *   The `openTrustedLinkIn` params, extended here with `initiatedByURLBar`.
   * @param {string} [loadData.userTypedValue]
   *   The value to record as the browser's typed value, for a `current` load.
   */
  #prepareAddressbarLoad({ browser, url, where, params, userTypedValue }) {
    if (where == "current") {
      browser.userTypedValue = userTypedValue;
    }

    // No point in setting this if we are loading in a new window.
    if (where != "window" && this.browserWindow.gInitialPages.includes(url)) {
      browser.initialPageLoadedFromUserAction = url;
    }

    try {
      lazy.UrlbarUtils.addToUrlbarHistory(url, this.browserWindow);
    } catch (ex) {
      // Things may go wrong when adding url to session history,
      // but don't let that interfere with the loading of the url.
      console.error(ex);
    }

    // TODO: When bug 1498553 is resolved, we should be able to
    // remove the !triggeringPrincipal condition here.
    if (
      !params.triggeringPrincipal ||
      params.triggeringPrincipal.isSystemPrincipal
    ) {
      // Reset DOS mitigations for the basic auth prompt.
      delete browser.authPromptAbuseCounter;

      // Reset temporary permissions on the current tab if the user reloads
      // the tab via the urlbar.
      if (
        where == "current" &&
        browser.currentURI &&
        url === browser.currentURI.spec
      ) {
        this.browserWindow.SitePermissions.clearTemporaryBlockPermissions(
          browser
        );
      }
    }

    // Specifies that the URL load was initiated by the URL bar.
    params.initiatedByURLBar = true;
  }

  /**
   * Removes a result from the current query context and notifies listeners.
   * Heuristic results cannot be removed.
   *
   * @param {UrlbarResult} result
   *   The result to remove.
   * @param {object} [options]
   *   Options object.
   * @param {object} [options.acknowledgeDismissalL10n]
   *   When the result is being dismissed, the l10n for the acknowledgment tip
   *   that should replace its row. Passed through to the view rather than set on
   *   the result so the result stays identical on both sides of the actor
   *   boundary.
   */
  removeResult(result, { acknowledgeDismissalL10n } = {}) {
    if (!result || result.heuristic) {
      return;
    }

    if (!this._lastQueryContextWrapper) {
      console.error("Cannot remove result, last query not present");
      return;
    }
    let { queryContext } = this._lastQueryContextWrapper;

    let index = queryContext.results.findIndex(r => r.id === result.id);
    if (index < 0) {
      console.error("Failed to find the selected result in the results");
      return;
    }

    queryContext.results.splice(index, 1);
    this.notify(
      lazy.UrlbarShared.NOTIFICATIONS.QUERY_RESULT_REMOVED,
      result.id,
      acknowledgeDismissalL10n
    );
  }

  /**
   * Set the query context cache.
   *
   * @param {UrlbarQueryContext} queryContext the object to cache.
   */
  setLastQueryContextCache(queryContext) {
    this._lastQueryContextWrapper = { queryContext };
  }

  /**
   * Clear the previous query context cache.
   */
  clearLastQueryContextCache() {
    this._lastQueryContextWrapper = null;
  }

  /**
   * Notifies listeners of results, by dispatching through the paired
   * UrlbarChildController, which owns the listeners.
   *
   * @param {string} name Name of the notification.
   * @param {object} params Parameters to pass with the notification.
   */
  notify(name, ...params) {
    this.#child.notify(name, ...params);
  }

  #engineStoreInitStarted = false;

  #engineObserverRegistered = false;

  /**
   * Tears down the controller's process-wide registrations, so nothing reaches
   * it once the input it serves is gone.
   */
  destroy() {
    if (this.#engineObserverRegistered) {
      Services.obs.removeObserver(this, "browser-search-engine-modified");
      this.#engineObserverRegistered = false;
    }
    Services.obs.removeObserver(this, "intl:app-locales-changed");
  }

  /**
   * Initializes the engine store synchronously if the search service
   * is already loaded and initialized.
   *
   * Since this is only useful if  it can be called synchronously,
   * it's intentionally not exposed in UrlbarParentControllerProxy.
   *
   * @returns {boolean}
   *   Whether the engine store was initialized successfully.
   */
  maybeInitEngineStore() {
    if (
      Cu.isESModuleLoaded(
        "moz-src:///toolkit/components/search/SearchService.sys.mjs"
      ) &&
      lazy.SearchService.isInitialized
    ) {
      this.initEngineStore();
      return true;
    }
    return false;
  }

  async initEngineStore() {
    if (this.#engineStoreInitStarted) {
      return;
    }
    this.#engineStoreInitStarted = true;
    if (!lazy.SearchService.hasSuccessfullyInitialized) {
      try {
        await lazy.SearchService.init();
      } catch {
        this.#child.updateEngineStore("error");
        return;
      }
    }
    let engines = lazy.SearchService.visibleEngines;
    let engineInfos = engines.map(engineToEngineInfo);
    let defaultEngine = this.isPrivate
      ? lazy.SearchService.defaultPrivateEngine
      : lazy.SearchService.defaultEngine;
    let defaultIndex = engines.findIndex(e => e == defaultEngine);
    if (!defaultEngine || defaultIndex == -1) {
      // Something went very wrong.
      this.#child.updateEngineStore("error");
      return;
    }
    this.#child.updateEngineStore("init", engineInfos, defaultIndex);
    Services.obs.addObserver(this, "browser-search-engine-modified", true);
    this.#engineObserverRegistered = true;
  }

  #topSitesListener = () => {
    this.view.clearTopSitesCache();
  };

  QueryInterface = ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]);

  /**
   * @param {nsISupports} subject
   * @param {"browser-search-engine-modified"|"intl:app-locales-changed"} topic
   * @param {string} data
   */
  observe = (subject, topic, data) => {
    switch (topic) {
      case "browser-search-engine-modified":
        this.#onSearchEngineModified(
          /** @type {{wrappedJSObject: SearchEngine}} */ (subject),
          data
        );
        break;
      case "intl:app-locales-changed":
        this.view.clearL10nCache();
        break;
    }
  };

  /**
   * @param {{wrappedJSObject: SearchEngine}} subject
   * @param {string} data
   */
  #onSearchEngineModified(subject, data) {
    let engine = subject.wrappedJSObject;
    let sortedEngines = lazy.SearchService.visibleEngines;
    let index = sortedEngines.findIndex(e => e == engine);
    let engineInfo = engineToEngineInfo(engine);

    switch (data) {
      case "engine-icon-changed":
        if (index == -1) {
          // Engines that were already removed may still send notify updates.
          // Ignore to avoid re-adding them.
          break;
        }
      // fall-through
      case "engine-added":
      case "engine-changed":
        if (!engine.hidden) {
          this.#child.updateEngineStore("changed", engineInfo, index);
        } else {
          this.#child.updateEngineStore("removed", engineInfo, index);
        }
        break;
      case "engine-removed":
        this.#child.updateEngineStore("removed", engineInfo, index);
        break;
      case "engine-default":
        if (!this.isPrivate) {
          this.#child.updateEngineStore("default", engineInfo, index);
        }
        break;
      case "engine-default-private":
        if (this.isPrivate) {
          this.#child.updateEngineStore("default", engineInfo, index);
        }
        break;
    }
  }
}

/**
 * Tracks and records telemetry events for the given category, if provided,
 * otherwise it's a no-op.
 * It is currently designed around the "urlbar" category, even if it can
 * potentially be extended to other categories.
 * To record an event, invoke start() with a starting event, then either
 * invoke record() with a final event, or discard() to drop the recording.
 *
 * @see Events.yaml
 */
export class TelemetryEvent {
  /**
   * @param {UrlbarParentController} controller
   *  The associated UrlbarParentController.
   */
  constructor(controller) {
    this._controller = controller;
    lazy.UrlbarPrefs.addObserver(this);
    this.#readPingPrefs();
    this._lastSearchDetailsForDisableSuggestTracking = null;
  }

  /**
   * Start measuring the elapsed time from a user-generated event.
   * After this has been invoked, any subsequent calls to start() are ignored,
   * until either record() or discard() are invoked. Thus, it is safe to keep
   * invoking this on every input event as the user is typing, for example.
   *
   * @param {event} event A DOM event.
   * @param {UrlbarQueryContext} queryContext A queryContext.
   * @param {string} [searchString] Pass a search string related to the event if
   *        you have one.  The event by itself sometimes isn't enough to
   *        determine the telemetry details we should record.
   * @param {string} [interactionType] An explicit interaction type for the
   *        session, used in preference to one derived from the event. The view
   *        passes this when reopening a prior search ("returned").
   * @throws This should never throw, or it may break the urlbar.
   * @see {@link https://firefox-source-docs.mozilla.org/browser/urlbar/telemetry.html}
   */
  start(event, queryContext, searchString = null, interactionType = null) {
    if (this._startEventInfo) {
      if (this._startEventInfo.interactionType == "topsites") {
        // If the most recent event came from opening the results pane with an
        // empty string replace the interactionType (that would be "topsites")
        // with one for the current event to better measure the user flow.
        this._startEventInfo.interactionType =
          interactionType ||
          lazy.UrlbarTelemetryUtils.startInteractionType(event, searchString);
        this._startEventInfo.searchString = searchString;
      } else if (
        this._startEventInfo.interactionType == "returned" &&
        (!searchString ||
          this._startEventInfo.searchString[0] != searchString[0])
      ) {
        // In case of a "returned" interaction ongoing, the user may either
        // continue the search, or restart with a new search string. In that case
        // we want to change the interaction type to "restarted".
        // Detecting all the possible ways of clearing the input would be tricky,
        // thus this makes a guess by just checking the first char matches; even if
        // the user backspaces a part of the string, we still count that as a
        // "returned" interaction.
        this._startEventInfo.interactionType = "restarted";
      }

      // start is invoked on a user-generated event, but we only count the first
      // one.  Once an engagement or abandonment happens, we clear _startEventInfo.
      return;
    }

    if (!event) {
      console.error("Must always provide an event");
      return;
    }
    const validEvents = [
      "click",
      "command",
      "drop",
      "input",
      "keydown",
      "mousedown",
      "paste",
      "tabswitch",
      "focus",
    ];
    if (!validEvents.includes(event.type)) {
      console.error("Can't start recording from event type: ", event.type);
      return;
    }

    this._startEventInfo = {
      timeStamp: event.timeStamp,
      interactionType:
        interactionType ||
        lazy.UrlbarTelemetryUtils.startInteractionType(event, searchString),
      searchString,
    };
  }

  /**
   * @typedef {object} ActionDetails
   *   An object describing action details that are recorded in an event.
   * @property {HTMLElement} [element]
   *   The picked view element.
   * @property {UrlbarResult} [result]
   *   The engaged result. This should be set to the result related to the
   *   picked element.
   * @property {UrlbarResult[]} [visibleResults]
   *   The visible results captured when a deferred (disable/bounce) recording
   *   is tracked, so `selIndex` indexes into the same set the recording reports.
   * @property {boolean} [isSessionOngoing]
   *   Set to true if the search session is still ongoing.
   * @property {object} [searchMode]
   *   The searchMode object to record.
   * @property {string} searchSource
   *   The source of the search event.
   * @property {string} [searchString]
   *   The user's search string. Note that this string is not sent with telemetry
   *   data. It is only used locally to discern other data, such as the number
   *   of characters and words in the string.
   * @property {string} [selType]
   *   The Type of the selected element, undefined for "blur".
   *   One of "unknown", "autofill", "visiturl", "bookmark", "help", "history",
   *   "keyword", "searchengine", "searchsuggestion", "switchtab", "remotetab",
   *   "extension", "oneoff", "dismiss".
   * @property {SapLocation} [location]
   *   The location where the interaction occurred.
   *   Required when sap is "smartbar".
   * @property {WindowMode} [windowMode]
   *   The window mode: classic, private, or smartwindow.
   */

  /**
   * @typedef {object} AdditionalActionDetails
   * @property {string} provider
   *   The name of the `UrlbarProvider` that provided the selected result.
   * @property {number} selIndex
   *   The index of the selected result.
   */

  /**
   * @typedef {ActionDetails & AdditionalActionDetails} InternalActionDetails
   */

  /**
   * Record an engagement telemetry event.
   * When the user picks a result from a search through the mouse or keyboard,
   * an engagement event is recorded. If instead the user abandons a search, by
   * blurring the input field, an abandonment event is recorded.
   *
   * On return, `details.isSessionOngoing` will be set to true if the engagement
   * did not end the search session. Not all engagements end the session. The
   * session remains ongoing when certain commands are picked (like dismissal)
   * and results that enter search mode are picked.
   *
   * @param {?event} event
   *   A DOM event. Note: event can be null, that usually happens for paste&go
   *   or drop&go. If there's no _startEventInfo this is a no-op.
   * @param {ActionDetails} details
   */
  record(event, details) {
    // Prevent re-entering `record()`. This can happen because
    // `#internalRecord()` will notify an engagement to the provider, that may
    // execute an action blurring the input field. Then both an engagement
    // and an abandonment would be recorded for the same session.
    // Nulling out `_startEventInfo` doesn't save us in this case, because it
    // happens after `#internalRecord()`, and `isSessionOngoing` must be
    // calculated inside it.
    if (this.#handlingRecord) {
      return;
    }

    // This should never throw, or it may break the urlbar.
    try {
      this.#handlingRecord = true;
      this.#internalRecord(event, details);
    } catch (ex) {
      console.error("Could not record event: ", ex);
    } finally {
      this.#handlingRecord = false;

      // Reset the start event info except for engagements that do not end the
      // search session. In that case, the view stays open and further
      // engagements are possible and should be recorded when they occur.
      // (`details.isSessionOngoing` is not a param; rather, it's set by
      // `#internalRecord()`.)
      if (!details.isSessionOngoing) {
        this._startEventInfo = null;
        this._discarded = false;
      }
    }
  }

  /**
   * Internal record method, see the record function. Builds the engagement the
   * same way the content-side collector does, then records it through the shared
   * `recordFromChild()`; the only in-process difference is that exposures are
   * resolved here from the parent's own queue rather than shipped.
   *
   * @param {Event} event
   * @param {ActionDetails} details
   */
  #internalRecord(event, details) {
    let snapshot = lazy.UrlbarTelemetryUtils.collectSnapshot(
      event,
      details,
      this._startEventInfo
    );
    if (!snapshot) {
      return;
    }

    // `details.isSessionOngoing` was set by collectSnapshot() above.
    let engagementData = this.#engagementData;
    let { built, previousSearchWords } =
      lazy.UrlbarTelemetryUtils.buildRecordedEngagement(
        snapshot,
        engagementData,
        this.#smartbarData,
        this.#previousSearchWordsSet
      );
    this.#previousSearchWordsSet = previousSearchWords;

    let disableBuilt = engagementData.visibleResults.some(
      r => r.providerName == "UrlbarProviderQuickSuggest"
    )
      ? lazy.UrlbarTelemetryUtils.buildRecordedDisableCandidate(
          snapshot,
          engagementData,
          this.#smartbarData,
          this.#previousSearchWordsSet
        )
      : null;

    let { queryContext } = this._controller._lastQueryContextWrapper || {};
    let exposures = details.isSessionOngoing
      ? null
      : this.#resolveExposureList(queryContext, engagementData.visibleResults);

    this.recordFromChild({
      built,
      disableBuilt,
      method: snapshot.method,
      searchSource: snapshot.internalDetails.searchSource,
      internalDetails: snapshot.internalDetails,
      exposures,
      visibleResults: engagementData.visibleResults,
    });
  }

  /**
   * Converts a search source string to a sap string.
   * Both are the same concept but they have slightly different values.
   * The sap string is used in urlbar.* telemetry.
   *
   * @see {BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES} For an overview
   * of the available values of search source and which telemetry uses it.
   *
   * @param {string} searchSource
   *   The search source string to convert.
   * @returns {null|"urlbar"|"searchbar"|"smartbar"|"handoff"|"urlbar_newtab"|"urlbar_addonpage"}
   *   The sap value for urlbar.* telemetry or null if the browser window
   *   already started closing. In that case, no telemetry should be recorded.
   */
  #searchSourceToSap(searchSource) {
    if (searchSource === "urlbar_handoff") {
      return "handoff";
    }
    // TODO (bug 2024630): Ideally, we would not add every new SAP here.
    if (searchSource === "searchbar") {
      return "searchbar";
    }
    if (searchSource === "smartbar") {
      return "smartbar";
    }
    let browserWindow = this._controller.browserWindow;
    if (browserWindow.closed) {
      // If the browser window has already started closing, then we bail-out.
      // We would rather return no telemetry than have telemetry with an
      // incorrect SAP. Generally, this should only happen in tests, since
      // the timing would need to be very close for the code not to have got
      // here before the user started closing the window.
      return null;
    }
    if (browserWindow.isBlankPageURL(browserWindow.gBrowser.currentURI.spec)) {
      return "urlbar_newtab";
    }
    if (lazy.ExtensionUtils.isExtensionUrl(browserWindow.gBrowser.currentURI)) {
      return "urlbar_addonpage";
    }
    return "urlbar";
  }

  /**
   * The input/view state the engagement-telemetry recording needs. Read from
   * the live input/view: only the direct path and the parent-side bounce/disable
   * recording reach this getter, and both have the live objects. (Message-path
   * engagement builds its event content-side, so nothing is injected here.)
   *
   * @type {{searchMode: object, visibleResults: UrlbarResult[], viewIsOpen: boolean, searchSource: string}}
   */
  get #engagementData() {
    return lazy.UrlbarTelemetryUtils.engagementData(
      this._controller.input,
      this._controller.view
    );
  }

  /**
   * The smartbar-only telemetry fields, from the live input.
   *
   * @type {{chatId: string, intent: string, model: string}}
   */
  get #smartbarData() {
    return lazy.UrlbarTelemetryUtils.smartbarData(this._controller.input);
  }

  /**
   * Records an engagement a message-path child collector built and shipped via
   * `RecordEngagement`. The child already built the Glean event; the parent
   * fills the fields that need parent-only services and makes the `Glean` call,
   * records the shipped exposures, and runs the parent-side provider steps
   * (disable-tracking and `notifyEngagementChange`) from the shipped result.
   *
   * @param {object} data
   *   The deserialized `recordedEngagementToWire()` payload.
   * @param {?{metric: string, eventInfo: object}} data.built
   *   The built Glean event (metric + partial event), or null.
   * @param {?object} data.disableBuilt
   *   The built disable-suggest candidate event when a Suggest result showed, or
   *   null.
   * @param {"engagement"|"abandonment"} data.method
   *   The engagement method.
   * @param {string} data.searchSource
   *   The search source.
   * @param {object} data.internalDetails
   *   The interaction details (picked result reconstructed; event/element null).
   * @param {?object[]} data.exposures
   *   The resolved exposure list, or null when the session stays open.
   * @param {?UrlbarResult[]} data.visibleResults
   *   The results shown at engagement, for the provider impression/abandonment
   *   hooks (the parent's own view has none on the message path).
   */
  recordFromChild({
    built,
    disableBuilt,
    method,
    searchSource,
    internalDetails,
    exposures,
    visibleResults,
  }) {
    try {
      let { queryContext } = this._controller._lastQueryContextWrapper || {};
      let sap = this.#searchSourceToSap(searchSource);

      if (built && sap) {
        this.#fillAndRecord(built, sap);
      }

      if (sap && exposures?.length) {
        this.#recordExposureList(exposures, sap);
      }

      // Start tracking for a disable event if a Suggest result showed during
      // this engagement or abandonment (the candidate was built content-side).
      if (disableBuilt) {
        this.startTrackingDisableSuggest(disableBuilt, searchSource);
      }

      // On the message path internalDetails.result was reconstructed from
      // structured clone, which strips data that doesn't survive it (e.g. a Rust
      // suggestion's UniFFI class) and yields an object distinct from the
      // parent's authoritative result. Resolve it back to the live result by id
      // so provider engagement handling -- notably dismissal against the Rust
      // store -- operates on the live object, and carry the view-assigned
      // rowIndex the wire preserves so the selection ping's position is right
      // (the live result never went through a view). visibleResults stay as the
      // wire results; the impression/abandonment hooks match them by id.
      // TODO(bug 2055935): remove this or bake the resolution into the actor
      // result deserialization.
      let liveResult = queryContext?.results?.find(
        r => r.id === internalDetails.result?.id
      );
      if (liveResult) {
        if (internalDetails.result.rowIndex != null) {
          liveResult.rowIndex = internalDetails.result.rowIndex;
        }
        internalDetails.result = liveResult;
      }

      this._controller.manager.notifyEngagementChange(
        method,
        queryContext,
        internalDetails,
        this._controller,
        visibleResults
      );
    } catch (ex) {
      console.error("Could not record engagement: ", ex);
    }
  }

  /**
   * Fills the fields a built Glean event needs from parent-only services and
   * records it. Shared by the engagement/abandonment recorder, the disable and
   * bounce recorders, and the message-path bounce trigger.
   *
   * @param {{metric: string, eventInfo: object}} built
   *   The built event from `UrlbarTelemetryUtils.buildEventInfo()`.
   * @param {string} sap
   *   The already-resolved search access point.
   */
  #fillAndRecord(built, sap) {
    let { metric, eventInfo } = built;
    eventInfo.sap = sap;
    eventInfo.search_engine_default_id =
      lazy.SearchService.defaultEngine.telemetryId;
    if (metric === "engagement" || metric === "abandonment") {
      eventInfo.available_semantic_sources =
        this.#getAvailableSemanticSources().join();
    }
    lazy.logger.info(`${metric} event:`, eventInfo);
    Glean.urlbar[metric].record(eventInfo);

    if (metric === "engagement" && eventInfo.search_mode) {
      this.#maybeRecordSearchModeUrlLikeQuery();
    }
  }

  /**
   * Records the `urlbar.searchmode.url_like_query` rate for a search-mode
   * engagement. The denominator counts engagements whose heuristic result is a
   * search result -- the only case where changing the behavior to navigate
   * instead of search could take effect -- and the numerator counts those whose
   * typed string parses as a URL per URIFixup. Local search modes have no search
   * heuristic result and are therefore excluded.
   */
  #maybeRecordSearchModeUrlLikeQuery() {
    let { queryContext } = this._controller._lastQueryContextWrapper || {};
    let heuristicResult = queryContext?.heuristicResult;
    if (
      !heuristicResult?.heuristic ||
      heuristicResult.type !== lazy.UrlbarShared.RESULT_TYPE.SEARCH
    ) {
      return;
    }
    Glean.urlbarSearchmode.urlLikeQuery.addToDenominator(1);
    let { fixupInfo } = queryContext;
    if (fixupInfo?.href && !fixupInfo.isSearch) {
      Glean.urlbarSearchmode.urlLikeQuery.addToNumerator(1);
    }
  }

  /**
   * Records the relevant telemetry information for the given parameters.
   *
   * @param {"abandonment" | "engagement" | "disable" | "bounce"} method
   * @param {{interactionType: any, searchString: string }} startEventInfo
   * @param {object} details
   * @param {string} details.action
   *   The type of action that caused this event. This may be recorded in the
   *   engagement_type field of the event.
   * @param {number} details.numWords
   *   The length of words used for the search.
   * @param {number} details.numChars
   *   The length of string used for the search. It includes whitespaces.
   * @param {string} details.provider
   *   The name of the `UrlbarProvider` that provided the selected result.
   * @param {string[]} details.searchWords
   *   The search words entered, used to determine if the search has been refined.
   * @param {string} details.searchSource
   *   The source of the search event.
   * @param {object} details.searchMode
   *   The searchMode object to record.
   * @param {number} details.selIndex
   *   The index of the selected result.
   * @param {UrlbarResult[]} details.visibleResults
   *   The results shown when the engagement was captured. Passed in (rather than
   *   read fresh here) so a deferred recording indexes `selIndex` into the same
   *   results it was captured against.
   * @param {string} details.selType
   *   The Type of the selected element, undefined for "blur".
   *   One of "unknown", "autofill", "visiturl", "bookmark", "help", "history",
   *   "keyword", "searchengine", "searchsuggestion", "switchtab", "remotetab",
   *   "extension", "oneoff", "dismiss".
   * @param {string|null} [details.pickedActionKey]
   *   The `data-action` of the action button the user picked, when selType is
   *   "action". Used to disambiguate which action was picked in the global
   *   actions row, which can contain multiple action buttons.
   * @param {number} [details.viewTime]
   *   The length of the view time in milliseconds.
   * @param {SapLocation} [details.location]
   *   The location where the interaction occurred.
   *   Required when sap is "smartbar".
   * @param {WindowMode} [details.windowMode]
   *   The window mode: classic, private, or smartwindow.
   * @param {string} [details.chatId]
   *   UUID for this smart window session. Unique identifier for each chat
   *   conversation. Only set when sap is `smartbar`.
   * @param {string} [details.intent]
   *   The detected intent for the input. Only set when sap is `smartbar`.
   * @param {string} [details.model]
   *   Model selected by the user. Only set when sap is `smartbar`.
   */
  #recordSearchEngagementTelemetry(
    method,
    startEventInfo,
    {
      action,
      numWords,
      numChars,
      provider,
      searchWords,
      searchSource,
      searchMode,
      selIndex,
      visibleResults,
      selType,
      pickedActionKey = null,
      viewTime = 0,
      location = null,
      chatId = "",
      intent = "",
      model = "",
      windowMode,
    }
  ) {
    let sap = this.#searchSourceToSap(searchSource);
    if (!sap) {
      return;
    }
    let engagementData = this.#engagementData;
    // The extra_key `location` is optional, but required for the smartbar.
    // TODO (bug 2024631): Support location for all SAPs.
    if (this._controller.sapName === "smartbar" && !location) {
      throw new Error(
        "Telemetry extra_key `location` is required for smartbar"
      );
    }
    searchMode = searchMode ?? engagementData.searchMode;

    // Distinguish user typed search strings from persisted search terms. The
    // "refined" check compares against the previous session's search words, so
    // thread that state through and store what comes back.
    let { interaction, previousSearchWords } =
      lazy.UrlbarTelemetryUtils.getInteractionType(
        method,
        startEventInfo,
        searchSource,
        searchWords,
        searchMode,
        this.#previousSearchWordsSet
      );
    this.#previousSearchWordsSet = previousSearchWords;

    let built = lazy.UrlbarTelemetryUtils.buildEventInfo({
      method,
      action,
      interaction,
      numChars,
      numWords,
      provider,
      searchSource,
      searchMode,
      selIndex,
      visibleResults,
      viewIsOpen: engagementData.viewIsOpen,
      selType,
      pickedActionKey,
      viewTime,
      location,
      chatId,
      intent,
      model,
      windowMode,
    });
    if (built) {
      this.#fillAndRecord(built, sap);
    }
  }

  /**
   * Returns Smart Window telemetry data for SAP `smartbar`.
   *
   * @param {string} searchSource
   *   The search source identifier.
   * @returns {{ chatId: string, intent: string, model: string } | null}
   *   Telemetry for the smartbar.
   */
  #getOptionalSmartbarTelemetry(searchSource) {
    const isSmartbar = this.#searchSourceToSap(searchSource) === "smartbar";
    if (!isSmartbar) {
      return null;
    }
    return this.#smartbarData;
  }

  /**
   * Retrieves available semantic search sources.
   * Ensure it is the provider initializing the semantic manager, since it
   * provides the right configuration for the singleton.
   *
   * @returns {Array<string>} Array of found sources, will contain just "none"
   *   if no sources were found.
   */
  #getAvailableSemanticSources() {
    let sources = [];
    try {
      const semanticManager =
        lazy.UrlbarProviderSemanticHistorySearch.semanticManager;
      const isSmartbar = this._controller.sapName === "smartbar";
      if (
        isSmartbar
          ? semanticManager.isEnabledForSmartWindow
          : semanticManager.canUseSemanticSearch
      ) {
        sources.push("history");
      }
    } catch (e) {
      lazy.logger.error("Error getting the semantic manager:", e);
    }
    if (!sources.length) {
      sources.push("none");
    }
    return sources;
  }

  /**
   * Resolves the queued exposures to their recordable form (computing the
   * terminal flag against the live results and query context) and clears the
   * queues. `recordFromChild()` records the returned list, so the direct and
   * message paths record exposures the same way.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query the exposures belong to.
   * @param {UrlbarResult[]} visibleResults
   *   The results shown at session end.
   * @returns {Array<{resultType: string, keyword: ?string, terminal: boolean}>}
   *   The resolved exposures (empty when none were queued).
   */
  #resolveExposureList(queryContext, visibleResults) {
    let exposures = this.#exposures;
    this.#exposures = [];
    this.#tentativeExposures = [];
    return exposures.map(({ weakResult, resultType, keyword }) => {
      let result = weakResult.get();
      let terminal = false;
      if (result) {
        this.#exposureResults.delete(result);
        terminal = lazy.UrlbarTelemetryUtils.exposureTerminal(
          result,
          queryContext,
          visibleResults
        );
      }
      return { resultType, keyword, terminal };
    });
  }

  /**
   * Records the `exposure` Glean event (and any `keyword_exposure` events and
   * the `urlbar-keyword-exposure` ping) from a resolved exposure list. The
   * parent-side recording half of exposure telemetry; fed either by
   * `#recordExposures()` on the direct path or by the list a child collector
   * ships on the message path.
   *
   * @param {Array<{resultType: string, keyword: ?string, terminal: boolean}>} list
   *   The resolved exposures.
   * @param {string} sap
   *   The search access point.
   */
  #recordExposureList(list, sap) {
    let terminalByType = new Map();
    let keywordExposureRecorded = false;
    for (let { resultType, keyword, terminal } of list) {
      terminalByType.set(resultType, terminal);

      // Record the `keyword_exposure` event if there's a keyword.
      if (keyword) {
        let data = {
          keyword,
          terminal: terminal.toString(),
          result: resultType,
          sap,
        };
        lazy.logger.debug("Recording keyword_exposure event", data);
        Glean.urlbar.keywordExposure.record(data);
        keywordExposureRecorded = true;
      }
    }

    // Record the `exposure` event.
    let tuples = [...terminalByType].sort((a, b) => a[0].localeCompare(b[0]));
    let exposure = {
      results: tuples.map(t => t[0]).join(","),
      terminal: tuples.map(t => t[1]).join(","),
      sap,
    };
    lazy.logger.debug("Recording exposure event", exposure);
    Glean.urlbar.exposure.record(exposure);

    // Submit the `urlbar-keyword-exposure` ping if any keyword exposure events
    // were recorded above.
    if (keywordExposureRecorded) {
      GleanPings.urlbarKeywordExposure.submit();
    }
  }

  /**
   * Registers an exposure for a result in the current urlbar session, if the
   * result should record exposure telemetry. All exposures that are added
   * during a session are recorded in the `exposure` event at the end of the
   * session. If keyword exposures are enabled, they will be recorded in the
   * `urlbar-keyword-exposure` ping at the end of the session as well. Exposures
   * are cleared at the end of each session and do not carry over.
   *
   * @param {UrlbarResult} result An exposure will be added for this result if
   *        exposures are enabled for its result type.
   * @param {UrlbarQueryContext} queryContext The query context associated with
   *        the result.
   */
  addExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#addExposureInternal(result, queryContext);
    }
  }

  /**
   * Registers a tentative exposure for a result in the current urlbar session.
   * Exposures that remain tentative at the end of the session are discarded and
   * are not recorded in the exposure event.
   *
   * @param {UrlbarResult} result A tentative exposure will be added for this
   *        result if exposures are enabled for its result type.
   * @param {UrlbarQueryContext} queryContext The query context associated with
   *        the result.
   */
  addTentativeExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#tentativeExposures.push({
        weakResult: Cu.getWeakReference(result),
        weakQueryContext: Cu.getWeakReference(queryContext),
      });
    }
  }

  /**
   * Converts all tentative exposures that were added and not yet discarded
   * during the current urlbar session into actual exposures that will be
   * recorded at the end of the session.
   */
  acceptTentativeExposures() {
    if (this.#tentativeExposures.length) {
      for (let { weakResult, weakQueryContext } of this.#tentativeExposures) {
        let result = weakResult.get();
        let queryContext = weakQueryContext.get();
        if (result && queryContext) {
          this.#addExposureInternal(result, queryContext);
        }
      }
      this.#tentativeExposures = [];
    }
  }

  /**
   * Discards all tentative exposures that were added and not yet accepted
   * during the current urlbar session.
   */
  discardTentativeExposures() {
    if (this.#tentativeExposures.length) {
      this.#tentativeExposures = [];
    }
  }

  #addExposureInternal(result, queryContext) {
    // If we haven't added an exposure for this result, add it now. The view can
    // add exposures for the same results again and again due to the nature of
    // its update process, but we should record at most one exposure per result.
    if (!this.#exposureResults.has(result)) {
      this.#exposureResults.add(result);
      let { resultType, keyword } = lazy.UrlbarTelemetryUtils.exposureEntry(
        result,
        queryContext
      );
      this.#exposures.push({
        resultType,
        keyword,
        weakResult: Cu.getWeakReference(result),
      });
    }
  }

  /**
   * Resets the currently tracked user-generated event that was registered via
   * start(), so it won't be recorded.  If there's no tracked event, this is a
   * no-op.
   */
  discard() {
    if (this._startEventInfo) {
      this._startEventInfo = null;
      this._discarded = true;
    }
  }

  /**
   * Reset the internal state. This function is used for only when testing.
   */
  reset() {
    this.#previousSearchWordsSet = null;
    this._lastSearchDetailsForDisableSuggestTracking = null;
  }

  /**
   * Prefs to record in telemetry.
   *
   * If a pref is a `fallbackPref` for a Nimbus variable, list the variable
   * instead of the pref. That way, the metric will record the variable value
   * when the variable is defined and the pref value otherwise.
   */
  #PING_PREFS = {
    maxRichResults: Glean.urlbar.prefMaxResults,
    quickSuggestOnlineAvailable: Glean.urlbar.prefSuggestOnlineAvailable,
    "quicksuggest.online.enabled": Glean.urlbar.prefSuggestOnlineEnabled,
    "suggest.quicksuggest.all": Glean.urlbar.prefSuggestAll,
    "suggest.quicksuggest.sponsored": Glean.urlbar.prefSuggestSponsored,
    "suggest.topsites": Glean.urlbar.prefSuggestTopsites,
  };

  #readPingPrefs() {
    for (const p of Object.keys(this.#PING_PREFS)) {
      this.#recordPref(p);
    }
  }

  #recordPref(pref) {
    const metric = this.#PING_PREFS[pref];
    if (metric) {
      metric.set(lazy.UrlbarPrefs.get(pref));
    }

    switch (pref) {
      case "suggest.quicksuggest.all":
      case "suggest.quicksuggest.sponsored":
      case "quicksuggest.enabled":
        if (!lazy.UrlbarPrefs.get(pref)) {
          this.handleDisableSuggest();
        }
        break;
    }
  }

  onPrefChanged(pref) {
    this.#recordPref(pref);
  }

  onNimbusChanged(variable) {
    this.#recordPref(variable);
  }

  // Used to avoid re-entering `record()`.
  #handlingRecord = false;

  #previousSearchWordsSet = null;

  // These properties are used to record exposure telemetry. For general info on
  // exposures, see [1]. For keyword exposures, see [2] and [3]. Here's a
  // summary of how a result flows through the exposure telemetry code path:
  //
  // 1. The view makes the result's row visible and calls `addExposure()` for
  //    it. (Or, if the result is a hidden exposure, the view would have made
  //    its row visible.)
  // 2. If exposure telemetry should be recorded for the result, we push its
  //    telemetry type and some other data onto `#exposures`. If keyword
  //    exposures are enabled, we also include the search string in the data. We
  //    use `#exposureResults` to efficiently make sure we add at most one
  //    exposure per result to `#exposures`.
  // 3. At the end of a session, we record a single `exposure` event that
  //    includes all unique telemetry types in the `#exposures` data. We also
  //    record one `keyword_exposure` event per search string in the data, with
  //    each search string recorded as the `keyword` for that exposure. We clear
  //    `#exposures` so that the data does not carry over into the next session.
  //
  // `#tentativeExposures` supports hidden exposures and is necessary due to how
  // the view updates itself. When the view creates a row for a normal result,
  // the row can start out hidden, and it's only unhidden if the query finishes
  // without being canceled. When the view encounters a hidden-exposure result,
  // it doesn't actually create a row for it, but if the hypothetical row would
  // have started out visible, the view will call `addExposure()`. If the
  // hypothetical row would have started out hidden, the view will call
  // `addTentativeExposure()` and we'll add the result to `#tentativeExposures`.
  // Once the query finishes and the view unhides its rows, it will call
  // `acceptTentativeExposures()`, finally registering exposures for all such
  // hidden-exposure results in the query. If instead the query is canceled, the
  // view will remove its hidden rows and call `discardTentativeExposures()`.
  //
  // [1] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/metrics/urlbar_exposure
  // [2] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/pings/urlbar-keyword-exposure
  // [3] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/metrics/urlbar_keyword_exposure
  #exposures = [];
  #tentativeExposures = [];
  #exposureResults = new WeakSet();

  /**
   * Start tracking a potential disable suggest event after the user has seen a
   * suggest result. The candidate event is built content-side at engagement
   * time, so the deferred recording needs neither the DOM event nor an
   * in-process input.
   *
   * @param {{metric: string, eventInfo: object}} built
   *   The built disable event from `buildRecordedDisableCandidate()`.
   * @param {string} searchSource
   *   The search source, resolved to a sap when the event is recorded.
   */
  startTrackingDisableSuggest(built, searchSource) {
    this._lastSearchDetailsForDisableSuggestTracking = {
      // The time when a user interacts a suggest result, either through
      // an engagement or an abandonment.
      interactionTime: this.getCurrentTime(),
      built,
      searchSource,
    };
  }

  handleDisableSuggest() {
    let state = this._lastSearchDetailsForDisableSuggestTracking;
    this._lastSearchDetailsForDisableSuggestTracking = null;
    if (
      !state ||
      this.getCurrentTime() - state.interactionTime >
        lazy.UrlbarPrefs.get("events.disableSuggest.maxSecondsFromLastSearch") *
          1000
    ) {
      return;
    }

    let sap = this.#searchSourceToSap(state.searchSource);
    if (sap) {
      this.#fillAndRecord(state.built, sap);
    }
  }

  getCurrentTime() {
    return ChromeUtils.now();
  }

  // Bounces still being tracked on the direct path, keyed by the tab's stable
  // browser id. The browser element is captured while alive so a tab-close
  // trigger can still reach Interactions for it after the tab is gone.
  #directBounces = new Map();

  /**
   * Start tracking a potential bounce event after the user has engaged
   * with a URL bar result.
   *
   * @param {number} browserId
   *   The stable browser id of the tab the engagement happened in.
   * @param {event} event
   *   A DOM event.
   * @param {ActionDetails} details
   *   An object describing interaction details.
   */
  async startTrackingBounceEvent(browserId, event, details) {
    let startEventInfo = this._startEventInfo;

    // If we are already tracking a bounce, then another engagement
    // could possibly lead to a bounce.
    if (this.#directBounces.has(browserId)) {
      await this.handleBounceEventTrigger(browserId);
    }

    let browser = this._controller.resolveTargetBrowser(browserId);
    if (!browser) {
      return;
    }

    this.#directBounces.set(browserId, {
      startTime: Date.now(),
      snapshot: lazy.UrlbarTelemetryUtils.collectBounceSnapshot(
        event,
        details,
        startEventInfo,
        this.#engagementData.visibleResults
      ),
      browser,
    });
  }

  /**
   * Handle a bounce event trigger.
   * These include closing the tab/window and navigating away via
   * browser chrome (this includes clicking on history or bookmark entries,
   * and engaging with the URL bar).
   *
   * @param {number} browserId
   *   The stable browser id of the tab the trigger happened in.
   */
  async handleBounceEventTrigger(browserId) {
    let tracking = this.#directBounces.get(browserId);
    if (!tracking) {
      return;
    }

    const interactions =
      (await lazy.Interactions.getRecentInteractionsForBrowser(
        tracking.browser
      )) ?? [];

    // handleBounceEventTrigger() can run concurrently, so we bail out
    // if a prior async invocation has already cleared the tracking.
    if (!this.#directBounces.has(browserId)) {
      return;
    }

    let totalViewTime = 0;
    for (let interaction of interactions) {
      if (interaction.created_at >= tracking.startTime) {
        totalViewTime += interaction.totalViewTime || 0;
      }
    }

    // If the total view time when the user navigates away after a
    // URL bar interaction is less than the threshold of
    // events.bounce.maxSecondsFromLastSearch, we record a bounce event.
    // If totalViewTime is 0, that means the page didn't load yet, so
    // we wouldn't record a bounce event.
    if (
      totalViewTime != 0 &&
      totalViewTime <
        lazy.UrlbarPrefs.get("events.bounce.maxSecondsFromLastSearch") * 1000
    ) {
      this.recordBounceEvent(browserId, totalViewTime);
    }

    this.#directBounces.delete(browserId);
  }

  /**
   * Record a bounce event
   *
   * @param {number} browserId
   *   The stable browser id of the tab the engagement happened in.
   * @param {number} viewTime
   *  The time spent on a tab after a URL bar engagement before
   *  navigating away via browser chrome or closing the tab.
   */
  recordBounceEvent(browserId, viewTime) {
    let { snapshot } = this.#directBounces.get(browserId);
    this.#recordBounce(snapshot, viewTime);
  }

  /**
   * Records a bounce telemetry event from a bounce snapshot. The parent-side
   * recording half; fed either by `recordBounceEvent()` on the direct path or
   * by the snapshot a child collector ships on the message path.
   *
   * @param {?object} snapshot
   *   The bounce snapshot from `UrlbarTelemetryUtils.collectBounceSnapshot()`.
   * @param {number} viewTime
   *   The time spent on the tab before navigating away, in milliseconds.
   */
  #recordBounce(snapshot, viewTime) {
    if (!snapshot) {
      return;
    }

    this.#recordSearchEngagementTelemetry("bounce", snapshot.startEventInfo, {
      action: snapshot.action,
      numChars: snapshot.numChars,
      numWords: snapshot.numWords,
      searchWords: snapshot.searchWords,
      provider: snapshot.provider,
      searchSource: snapshot.searchSource,
      searchMode: snapshot.searchMode,
      selIndex: snapshot.selIndex,
      visibleResults: snapshot.visibleResults,
      selType: snapshot.selType,
      viewTime: viewTime / 1000,
      location: snapshot.location,
      windowMode: snapshot.windowMode,
      ...this.#getOptionalSmartbarTelemetry(snapshot.searchSource),
    });
  }

  // Browsers behind message-path bounces still being tracked, keyed by their
  // stable browser id, captured while alive so a tab-close trigger can still
  // resolve one after the tab is gone.
  #bounceBrowsers = new Map();

  /**
   * Caches the browser behind a bounce a message-path collector is tracking,
   * keyed by its stable browser id, while the tab is still alive. On a tab-close
   * trigger the browser is gone before the async trigger message is handled, so
   * it can no longer be resolved then; the preserved reference lets
   * `handleBounceTrigger()` record the bounce anyway. Keyed by browser id rather
   * than browsing context id because a navigation between tracking and the
   * trigger can replace the browsing context.
   *
   * @param {number} browserId
   *   The bounce browser's stable browser id.
   */
  trackBounceBrowser(browserId) {
    let browser = this._controller.resolveTargetBrowser(browserId);
    if (browser) {
      this.#bounceBrowsers.set(browserId, browser);
    }
  }

  /**
   * Records a bounce shipped by a message-path child collector. The collector
   * owns the bounce tracking content-side; on a trigger it sends the resolved
   * snapshot, the tracking start time, the embedder browser's id, and the
   * content the recording reads. Here we resolve the browser, ask
   * `Interactions` how long the tab was viewed, and record a bounce if it falls
   * under the threshold.
   *
   * @param {object} payload
   *   `{built, searchSource, startTime, browserId}` from the child collector,
   *   where `built` is the Glean event minus `view_time`.
   */
  async handleBounceTrigger(payload) {
    let { built, searchSource, startTime, browserId } = payload;
    let browser =
      this.#bounceBrowsers.get(browserId) ??
      this._controller.resolveTargetBrowser(browserId);
    this.#bounceBrowsers.delete(browserId);
    if (!browser || !built) {
      return;
    }

    const interactions =
      (await lazy.Interactions.getRecentInteractionsForBrowser(browser)) ?? [];
    let totalViewTime = 0;
    for (let interaction of interactions) {
      if (interaction.created_at >= startTime) {
        totalViewTime += interaction.totalViewTime || 0;
      }
    }

    if (
      totalViewTime != 0 &&
      totalViewTime <
        lazy.UrlbarPrefs.get("events.bounce.maxSecondsFromLastSearch") * 1000
    ) {
      let sap = this.#searchSourceToSap(searchSource);
      if (!sap) {
        return;
      }
      // view_time is only known now, once Interactions has reported it.
      built.eventInfo.view_time = (totalViewTime / 1000).toString();
      this.#fillAndRecord(built, sap);
    }
  }
}
