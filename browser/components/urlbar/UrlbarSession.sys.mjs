/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import {BrowserSearchTelemetry} from "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs"
 * @import {SapLocation} from "moz-src:///browser/components/urlbar/content/SmartbarInput.mjs"
 * @import {UrlbarParentController} from "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs"
 * @import {WindowMode} from "moz-src:///browser/components/urlbar/content/UrlbarInputBase.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  Interactions: "moz-src:///browser/components/places/Interactions.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderSemanticHistorySearch:
    "moz-src:///browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTelemetryUtils:
    "chrome://browser/content/urlbar/UrlbarTelemetryUtils.mjs",
  logger: () => lazy.UrlbarShared.getLogger({ prefix: "Session" }),
});

/**
 * A bounce a message-path child collector built at engagement time.
 *
 * @typedef {object} BuiltBounce
 * @property {object} built
 *   The Glean event from `UrlbarTelemetryUtils.buildEventInfo()`, minus
 *   `view_time`, which only the parent knows and only at trigger time.
 * @property {string} searchSource
 *   The engagement's search source, which the SAP is resolved from.
 * @property {?number} browserId
 *   The stable browser id of the tab the engagement happened in, or null when
 *   the input has no chrome window to read one from.
 */

/**
 * A bounce still being tracked.
 *
 * @typedef {object} TrackedBounce
 * @property {number} startTime
 *   When the engagement happened.
 * @property {(viewTime: number) => void} record
 *   Records the bounce, given the view time in milliseconds.
 * @property {boolean} awaitingLoad
 *   Whether the engagement's load has yet to claim the bounce.
 */

/**
 * Tracks an address bar search session: it records engagement, abandonment,
 * exposure and bounce telemetry, and notifies providers when a session ends.
 * To track a session, invoke start() with a starting event, then either
 * invoke record() with a final event, or discard() to drop the session.
 */
export class UrlbarSession {
  /**
   * Bounces still being tracked, keyed by the browser of the page they were
   * tracked for. Static because a bounce outlives the collector that started
   * it: the New Tab search bar's page -- and with it its actor and parent
   * controller -- is gone by the time the tab navigates back or closes, so the
   * trigger arrives from a chrome window input. `record` closes over the
   * collector that resolves the SAP and makes the Glean call, and the map is
   * weak so a tab that never sees a trigger doesn't keep that collector's
   * window alive.
   *
   * A bounce is tracked before the engagement's page loads, under the tab the
   * engagement happened in, and moves to the browser the page loads in once the
   * load claims it (see `claimBounceForLoad()`).
   *
   * @type {WeakMap<MozBrowser, TrackedBounce>}
   */
  static #trackedBounces = new WeakMap();

  /**
   * Handle a bounce event trigger.
   * These include closing the tab/window and navigating away via
   * browser chrome (this includes clicking on history or bookmark entries,
   * and engaging with the URL bar).
   *
   * @param {MozBrowser} browser
   *   The browser of the tab the trigger happened in.
   */
  static async handleBounceEventTrigger(browser) {
    let tracking = UrlbarSession.#trackedBounces.get(browser);
    if (!tracking) {
      return;
    }
    UrlbarSession.#trackedBounces.delete(browser);
    await UrlbarSession.#recordBounceIfShortVisit(browser, tracking);
  }

  /**
   * Claims the bounce an engagement in a tab is tracking for the page it is
   * about to load, so the bounce follows that page to the browser it loads in:
   * a new tab or window, or the tab itself.
   *
   * @param {MozBrowser} sourceBrowser
   *   The browser of the tab the engagement happened in.
   * @returns {(browser: MozBrowser) => void}
   *   Moves the bounce to the browser the page loaded in. A no-op when no
   *   bounce is awaiting a load from the tab.
   */
  static claimBounceForLoad(sourceBrowser) {
    let tracking = UrlbarSession.#trackedBounces.get(sourceBrowser);
    if (!tracking?.awaitingLoad) {
      return () => {};
    }
    tracking.awaitingLoad = false;
    return browser => {
      if (
        browser == sourceBrowser ||
        UrlbarSession.#trackedBounces.get(sourceBrowser) != tracking
      ) {
        return;
      }
      UrlbarSession.#trackedBounces.delete(sourceBrowser);
      UrlbarSession.#trackedBounces.set(browser, tracking);
    };
  }

  /**
   * Records a bounce no longer tracked if the page it was tracked for was
   * viewed for less than the bounce threshold.
   *
   * @param {MozBrowser} browser
   *   The browser the page loaded in.
   * @param {TrackedBounce} tracking
   *   The bounce.
   */
  static async #recordBounceIfShortVisit(browser, tracking) {
    const interactions =
      (await lazy.Interactions.getRecentInteractionsForBrowser(browser)) ?? [];

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
      tracking.record(totalViewTime);
    }
  }

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
    if (this._controller.sapName === "smartbar") {
      // The smartbar CTA can start a session when the input is not focused.
      validEvents.push("aiwindow-input-cta:on-action");
    }
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

    // Engagements that run no query would otherwise reach the provider
    // notifications with no context at all.
    if (!this._controller._lastQueryContextWrapper) {
      this._controller.setLastQueryContextCache(queryContext);
    }
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
   * @returns {null|"urlbar"|"newtab_searchbar"|"searchbar"|"smartbar"|"handoff"|"urlbar_newtab"|"urlbar_addonpage"}
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
    if (searchSource === "newtab_searchbar") {
      return "newtab_searchbar";
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
   *   The interaction details; `event` and `element` are null on the message
   *   path.
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
      if (!queryContext) {
        // start() caches one for every session, so this means a caller ended a
        // session it never started.
        console.error(`Recording a ${method} with no query context`);
      }
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
   * @param {?string} [details.engagementSap]
   *   The search access point. The address bar's `urlbar`, `urlbar_newtab` and
   *   `urlbar_addonpage` depend on the page loaded at the time the search was
   *   made. The engagement itself loads a new page, so a recording deferred
   *   past it passes the sap resolved when the engagement happened. A recording
   *   made at engagement time omits it, so the sap resolves from `searchSource`
   *   here.
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
      engagementSap,
    }
  ) {
    let sap = engagementSap ?? this.#searchSourceToSap(searchSource);
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
   * @returns {{ chatId: string, intent: string, model: string } | null}
   *   Telemetry for the smartbar.
   */
  #getOptionalSmartbarTelemetry() {
    if (this._controller.sapName != "smartbar") {
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

  /**
   * Start tracking a potential bounce event after the user has engaged
   * with a URL bar result. The direct path's counterpart to
   * `startTrackingBuiltBounce()`, which takes an event a child collector has
   * already built.
   *
   * @param {?number} browserId
   *   The stable browser id of the tab the engagement happened in, or null when
   *   the input has no chrome window to read one from.
   * @param {event} event
   *   A DOM event.
   * @param {ActionDetails} details
   *   An object describing interaction details.
   */
  async startTrackingBounceEvent(browserId, event, details) {
    let snapshot = lazy.UrlbarTelemetryUtils.collectBounceSnapshot(
      event,
      details,
      this._startEventInfo,
      this.#engagementData.visibleResults
    );
    // There is no snapshot when the engagement has no event to classify, or
    // when its interaction has already been recorded, as when a navigation
    // picks the heuristic result the parent resolved for it after recording
    // the engagement.
    if (!snapshot) {
      return;
    }
    let sap = this.#searchSourceToSap(snapshot.searchSource);
    // There is no sap when the window is closing.
    if (!sap) {
      return;
    }
    // The input has left the search mode by the time the bounce triggers.
    snapshot.searchMode ??= this.#engagementData.searchMode;
    await this.#startTrackingBounce(browserId, viewTime =>
      this.#recordBounce(snapshot, viewTime, sap)
    );
  }

  /**
   * Tracks a potential bounce a message-path child collector built. The
   * collector builds the event at engagement time, since its page can be gone
   * by the time the bounce triggers: the New Tab search bar navigates the very
   * tab it lives in, and the trigger then reaches us through a chrome window
   * input.
   *
   * @param {BuiltBounce} payload
   *   The bounce the child collector built.
   */
  async startTrackingBuiltBounce(payload) {
    let { built, searchSource, browserId } = payload;
    let sap = this.#searchSourceToSap(searchSource);
    if (!sap) {
      return;
    }
    await this.#startTrackingBounce(browserId, viewTime => {
      // view_time is only known now, once Interactions has reported it.
      built.eventInfo.view_time = (viewTime / 1000).toString();
      this.#fillAndRecord(built, sap);
    });
  }

  /**
   * Tracks a bounce for the tab the engagement happened in, triggering any
   * bounce already tracked for that tab: another engagement there could itself
   * be a bounce. The new bounce is stored before the earlier one resolves, so
   * the engagement's load, which follows right away, finds it to claim.
   *
   * @param {?number} browserId
   *   The stable browser id of the tab the engagement happened in, or null when
   *   the input has no chrome window to read one from. The tab is then the one
   *   hosting the input.
   * @param {(viewTime: number) => void} record
   *   Records the bounce, given the view time in milliseconds.
   */
  async #startTrackingBounce(browserId, record) {
    let browser = this._controller.resolveTargetBrowser(browserId);
    if (!browser) {
      return;
    }
    let earlier = UrlbarSession.#trackedBounces.get(browser);
    UrlbarSession.#trackedBounces.set(browser, {
      startTime: Date.now(),
      record,
      awaitingLoad: true,
    });
    if (earlier) {
      await UrlbarSession.#recordBounceIfShortVisit(browser, earlier);
    }
  }

  /**
   * Records a bounce telemetry event from a bounce snapshot, the direct path's
   * recording half.
   *
   * @param {object} snapshot
   *   The bounce snapshot from `UrlbarTelemetryUtils.collectBounceSnapshot()`.
   * @param {number} viewTime
   *   The time spent on the tab before navigating away, in milliseconds.
   * @param {string} sap
   *   The sap resolved when the engagement happened. A bounce never resolves
   *   its own.
   */
  #recordBounce(snapshot, viewTime, sap) {
    this.#recordSearchEngagementTelemetry("bounce", snapshot.startEventInfo, {
      engagementSap: sap,
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
      ...this.#getOptionalSmartbarTelemetry(),
    });
  }
}
