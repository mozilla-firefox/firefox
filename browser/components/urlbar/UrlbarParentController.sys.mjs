/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { UrlbarSession } from "moz-src:///browser/components/urlbar/UrlbarSession.sys.mjs";

/**
 * @import {BrowserSearchTelemetry} from "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs"
 * @import {ProvidersManager} from "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs"
 * @import {SearchEngine, SearchSubmissionData} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 * @import {UrlbarView} from "chrome://browser/content/urlbar/UrlbarView.mjs"
 * @import {SearchEngineInfo} from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 * @import {UrlbarLoadRequest, LoadURLParams} from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 * @import {UrlbarChildControllerProxy, UrlbarInputProxy, UrlbarParent, UrlbarViewProxy} from "moz-src:///browser/components/urlbar/actors/UrlbarParent.sys.mjs"
 * @import {BuiltBounce} from "moz-src:///browser/components/urlbar/UrlbarSession.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AppProvidedConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  ContainerCreationPanel:
    "chrome://browser/content/usercontext/ContainerCreationPanel.mjs",
  ProvidersManager:
    "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarProviderTopSites:
    "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
  UrlbarQueryContext: "chrome://browser/content/urlbar/UrlbarQueryContext.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTelemetryUtils:
    "chrome://browser/content/urlbar/UrlbarTelemetryUtils.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  logger: () => lazy.UrlbarShared.getLogger({ prefix: "Controller" }),
});

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
   * The paired UrlbarChildController or an object that can forward calls to it.
   * Listener registration and notification dispatch live on it, keeping
   * dispatch on the side where the listeners (the view, the event bufferer)
   * live. The child is always set before any query runs.
   *
   * @type {UrlbarChildControllerProxy | UrlbarChildController}
   */
  #child = null;

  /**
   * The associated UrlbarParent actor.
   * Null in unit tests.
   *
   * @type {?UrlbarParent}
   */
  #actor = null;

  // Whether the controller serves the address bar.
  #isAddressbar = false;

  /** @type {BrowserWindow} */
  #browserWindow;

  /**
   * Initialises the controller from standalone data; the live input/view are
   * reached at runtime through the paired `UrlbarChildController`.
   *
   * @param {object} options
   *   The initial options for UrlbarParentController.
   * @param {UrlbarSapName} options.sapName
   *   The search access point name.
   * @param {boolean} [options.isPrivate]
   *   Whether the controller serves a private-browsing input.
   * @param {UrlbarParent} [options.actor]
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
    this.#browserWindow = /** @type {BrowserWindow} */ (
      actor?.browsingContext.topChromeWindow
    );

    /**
     * @type {ProvidersManager}
     */
    this.manager =
      manager || lazy.ProvidersManager.getInstanceForSap(this.sapName);

    this.session = new UrlbarSession(this);
    lazy.UrlbarProviderTopSites.addTopSitesListener(this.#topSitesListener);
    Services.obs.addObserver(this, "intl:app-locales-changed", true);
  }

  /**
   * The input or an object that can forward calls to the input.
   *
   * @type {UrlbarInputProxy | UrlbarInput}
   */
  get input() {
    return this.#child?.input;
  }

  /**
   * The chrome window the urlbar lives in.
   *
   * @type {BrowserWindow}
   */
  get browserWindow() {
    return this.#browserWindow;
  }

  /**
   * Whether the view showing these results renders in a content process, which
   * decodes what it displays itself. For an in-page urlbar that is the
   * privileged about process.
   *
   * @type {boolean}
   */
  get rendersInContentProcess() {
    return !!this.#actor?.browsingContext?.isContent;
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
   * The view or an object that can forward calls to the view.
   *
   * @type {UrlbarViewProxy | UrlbarView}
   */
  get view() {
    return this.#child?.view;
  }

  /**
   * Notifies a result's provider that the result is about to be selected.
   * Mediates the view's access to the (parent-process) provider.
   *
   * @param {UrlbarResult} result
   *   The result being selected.
   */
  onBeforeSelection(result) {
    this.manager
      .getProvider(result?.providerName)
      ?.tryMethod("onBeforeSelection", result);
  }

  /**
   * Notifies a result's provider that the result was selected. Mediates the
   * view's access to the (parent-process) provider.
   *
   * @param {UrlbarResult} result The selected result.
   */
  onSelection(result) {
    this.manager
      .getProvider(result?.providerName)
      ?.tryMethod("onSelection", result);
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
        userContextId: parseInt(browser.getAttribute("usercontextid") || "0"),
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
    this.session.recordFromChild(
      lazy.UrlbarTelemetryUtils.recordedEngagementFromWire(
        wire,
        this.liveResults
      )
    );
  }

  /**
   * Resets the recorder's cross-session telemetry state. The counterpart to the
   * proxy's `resetEngagement()`.
   */
  resetEngagement() {
    this.session.reset();
  }

  /**
   * Tracks a potential bounce a message-path child collector built. The
   * counterpart to the proxy's `startTrackingBuiltBounce()`.
   *
   * @param {BuiltBounce} payload
   *   The bounce the child collector built.
   * @returns {Promise<void>}
   */
  startTrackingBuiltBounce(payload) {
    return this.session.startTrackingBuiltBounce(payload);
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
    if (backspaceBlock) {
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
   * to form history.
   *
   * @param {UrlbarSearchData & {browser: MozBrowser, submission?: SearchSubmissionData}} searchData
   *   The search data, the browser where the search is loading and, if known,
   *   the submission that is being loaded.
   */
  #recordSearch({
    browser,
    engineId,
    query,
    searchSource,
    details,
    opensInPrivateWindow,
    submission,
  }) {
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

    if (this.sapName == "newtab_searchbar") {
      let newtabBrowser = this.#actor.browsingContext.top.embedderElement;
      details.newtabSessionId = lazy.AboutNewTab.getVisitId(newtabBrowser);
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
      submission ? { ...details, submission } : details
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
   * @param {UrlbarSearchData & {submission?: SearchSubmissionData}} searchData
   *   The search data and, if known, the submission being loaded.
   * @returns {() => void}
   *   Removes the listener, for when the load fails before opening the tab.
   */
  #recordSearchInOpenedTab(searchData) {
    let { tabContainer } = this.browserWindow.gBrowser;
    let listener = tabEvent => {
      this.#recordSearch({
        ...searchData,
        browser: tabEvent.target.linkedBrowser,
      });
    };
    tabContainer.addEventListener("TabOpen", listener, { once: true });
    return () => tabContainer.removeEventListener("TabOpen", listener);
  }

  /**
   * Opens a URL via `openTrustedLinkIn` and, for a search, records it only once
   * the load has gone ahead.
   *
   * @param {string} url
   * @param {string} where
   * @param {object} params
   *   The `openTrustedLinkIn` params.
   * @param {object} search
   * @param {MozBrowser} search.browser
   *   The browser the search is recorded against, unless it opens in a new tab.
   * @param {?UrlbarSearchData} [search.searchData]
   *   The search data, or null if this isn't a search to record.
   * @param {?SearchSubmissionData} [search.submission]
   *   The submission being loaded.
   */
  #openTrustedLinkAndRecordSearch(
    url,
    where,
    params,
    { browser, searchData = null, submission = null }
  ) {
    let removeTabOpenListener;
    if (searchData && where.startsWith("tab")) {
      removeTabOpenListener = this.#recordSearchInOpenedTab({
        ...searchData,
        submission,
      });
    }
    try {
      lazy.URILoadingHelper.openTrustedLinkIn(
        this.browserWindow,
        url,
        where,
        params
      );
    } catch (ex) {
      removeTabOpenListener?.();
      throw ex;
    }
    if (searchData && !removeTabOpenListener) {
      this.#recordSearch({ ...searchData, browser, submission });
    }
  }

  /**
   * Counts a zero-prefix view event under this input's SAP.
   *
   * @param {"abandonment"|"engagement"|"exposure"} kind
   *   The zero-prefix event to count.
   */
  recordZeroPrefix(kind) {
    Glean.urlbarZeroprefix2[kind][this.sapName].add(1);
  }

  /**
   * Counts a URL canonization, unless this input is in a private window.
   */
  recordCanonization() {
    if (this.isPrivate) {
      return;
    }
    lazy.UrlbarPrefs.set(
      "canonizedURLCount",
      lazy.UrlbarPrefs.get("canonizedURLCount") + 1
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
   * @param {UrlbarChildControllerProxy | UrlbarChildController} child
   */
  setChild(child) {
    this.#child = child;
  }

  /**
   * Opens a search engine result page (SERP) for the specified
   * search engine and search query.
   *
   * Only records search telemetry if `searchData` is passed.
   *
   * @param {string} engineId
   * @param {string} searchTerms
   * @param {string} where
   * @param {boolean} [inBackground]
   * @param {number} [browserId]
   *   The target browser's id. Only used if `where == current` and the call
   *   isn't coming from a content process. If it's not specified and
   *   `where == current`, the currently selected tab is used.
   * @param {?UrlbarSearchData} [searchData]
   *   The search data, recorded with the submission's partner
   *   code once the SERP is loading.
   */
  openSERP(
    engineId,
    searchTerms,
    where,
    inBackground = false,
    browserId = null,
    searchData = null
  ) {
    let searchEngine = lazy.SearchService.getEngineById(engineId);

    let [url, postData, submission] = lazy.UrlbarUtils.getSearchQueryUrl(
      searchEngine,
      searchTerms,
      searchData?.searchSource
    );

    let browser =
      this.resolveTargetBrowser(browserId) ??
      this.browserWindow.gBrowser.selectedBrowser;
    this.#openTrustedLinkAndRecordSearch(
      url,
      where,
      {
        inBackground,
        postData,
        resolveOnContentBrowserCreated:
          UrlbarSession.claimBounceForLoad(browser),
        targetBrowser: where == "current" ? browser : null,
        globalHistoryOptions: {
          triggeringSource: this.sapName,
          triggeringSearchEngine: searchEngine.name,
        },
      },
      { browser, searchData, submission }
    );
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
    lazy.URILoadingHelper.openTrustedLinkIn(this.browserWindow, url, where, {
      inBackground,
      targetBrowser:
        where == "current" ? this.resolveTargetBrowser(browserId) : null,
    });
  }

  /**
   * Opens the preferences page. The chrome window's `openPreferences` is a
   * script global of the browser window, out of reach of an input hosted in a
   * content page, so the call is made here.
   *
   * @param {string} paneID
   *   The preferences pane to open, per `openPreferences`.
   * @param {object} [extraArgs]
   *   Extra arguments, per `openPreferences`.
   */
  openPreferences(paneID, extraArgs) {
    this.browserWindow.openPreferences(paneID, extraArgs);
  }

  /**
   * Opens the panel that adds a container, anchored in the browser window for
   * the same reason `openPreferences` is called here.
   *
   * @param {string} entrypoint
   *   The UI entry point the request came from.
   */
  openContainerCreationPanel(entrypoint) {
    lazy.ContainerCreationPanel.open(this.browserWindow, entrypoint);
  }

  /**
   * Returns the icon URL of the engine with the given id, in a form the view
   * can load.
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
    return (await lazy.UrlbarUtils.getEngineIconUrl(engine, this)) ?? null;
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
    // Never speculative connect in private contexts.
    if (context.isPrivate || !context.results.length) {
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
                context.isSearchbarSAP) &&
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
   * @param {LoadURLParams} loadData.params
   *   The serializable `openTrustedLinkIn` params.
   * @param {number} [loadData.browserId]
   *   The target browser's id; defaults to the selected browser. A content
   *   sender always targets its own tab. A `current` load is dropped if its
   *   target browser has gone away.
   * @param {string} [loadData.userTypedValue]
   *   The value to record as the browser's typed value, for a `current` load.
   * @param {?UrlbarSearchData} [loadData.searchData]
   *   For an engine search, the search data. It is recorded with
   *   the submission's partner code once the load has gone ahead.
   * @returns {{reverted: boolean, browserId: number}}
   *   Whether the load threw without showing an error page, so the input should
   *   revert, and the id of the browser the load resolved to. The latter is not
   *   an echo of the optional `browserId` param: a default `current` load omits
   *   it and the target is resolved here, so this is how the child learns which
   *   browser to hand `focusBrowser` on the deferred-Enter keyup -- a
   *   content-process input can't resolve the selected browser itself.
   */
  loadURL({
    loadRequest,
    where,
    params,
    browserId,
    userTypedValue,
    searchData = null,
  }) {
    // TODO(bug 2076823): Simplify how the target browser is picked.
    let browser = this.resolveTargetBrowser(browserId);
    if (!browser) {
      // The tab was closed before the load got here; don't hand its load to
      // whichever tab is selected now.
      if ((browserId || this.rendersInContentProcess) && where == "current") {
        return { reverted: false, browserId };
      }
      browser = this.browserWindow.gBrowser.selectedBrowser;
    }
    params.resolveOnContentBrowserCreated =
      UrlbarSession.claimBounceForLoad(browser);

    let { url, postData, submission } = lazy.UrlbarUtils.loadRequestToUrl(
      loadRequest,
      searchData?.searchSource
    );
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

    // Specifies that the URL load was initiated by a URL bar input.
    params.initiatedByURLBar = true;

    // Focus the content area before triggering loads, since if the load
    // occurs in a new tab, we want focus to be restored to the content area
    // when the current tab is re-selected.
    if (!params.avoidBrowserFocus) {
      browser.focus();
    }

    try {
      this.#openTrustedLinkAndRecordSearch(url, where, params, {
        browser,
        searchData,
        submission,
      });
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
    let switched = lazy.URILoadingHelper.switchToTabHavingURI(
      this.browserWindow,
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
   * @param {LoadURLParams} loadData.params
   *   The `openTrustedLinkIn` params.
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
        lazy.SitePermissions.clearTemporaryBlockPermissions(browser);
      }
    }
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
    // Marked done: no query is running behind a context cached this way, so
    // cancelQuery() must not treat it as one.
    this._lastQueryContextWrapper = { queryContext, done: true };
  }

  /**
   * Clear the previous query context cache.
   */
  clearLastQueryContextCache() {
    this._lastQueryContextWrapper = null;
  }

  /**
   * The last query's results, which are the authoritative objects a result
   * reconstructed from the wire resolves back to. See `UrlbarResult.fromWire()`.
   *
   * @type {UrlbarResult[]}
   */
  get liveResults() {
    return this._lastQueryContextWrapper?.queryContext.results ?? [];
  }

  /**
   * Notifies listeners of results, by dispatching through the paired
   * UrlbarChildController, which owns the listeners.
   *
   * @param {string} name Name of the notification.
   * @param {object} params Parameters to pass with the notification.
   */
  notify(name, ...params) {
    if (this.#child.isProxy === true) {
      this.#child.notifyFromWire(
        name,
        ...params.map(param =>
          param instanceof lazy.UrlbarQueryContext
            ? { serializedQueryContext: param.toWire() }
            : param
        )
      );
    } else {
      this.#child.notify(name, ...params);
    }
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
 * The data for recording a search, which the parent records once the search's
 * URL is being loaded.
 *
 * @typedef {object} UrlbarSearchData
 * @property {string} engineId
 *   The id of the engine handling the search.
 * @property {string} query
 *   The search query, which is added to form history.
 * @property {keyof typeof lazy.BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES} searchSource
 *   Where the search originated from.
 * @property {Parameters<typeof lazy.BrowserSearchTelemetry.recordSearch>[3]} details
 *   The search action details, per `BrowserSearchTelemetry.recordSearch()`.
 * @property {boolean} [opensInPrivateWindow]
 *   Whether the search opens in a new private window, in which case it's
 *   not added to form history. If this is false but the current window
 *   is private, it's not added either.
 */
