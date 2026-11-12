/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarQueryContext } from "chrome://browser/content/urlbar/UrlbarQueryContext.mjs";
import { UrlbarResult } from "chrome://browser/content/urlbar/UrlbarResult.mjs";

/**
 * @import {UrlbarActorPort} from "moz-src:///browser/components/urlbar/actors/UrlbarChild.sys.mjs"
 * @import {BuiltBounce} from "moz-src:///browser/components/urlbar/UrlbarSession.sys.mjs"
 */

/**
 * Stand-in for a `UrlbarParentController` on the Urlbar actor's message path. A
 * `UrlbarChildController` whose `<moz-urlbar>` runs in
 * a content process (or in chrome with
 * `browser.urlbar.ipc.chromeMessagePassing`) holds one of these instead of a
 * direct controller reference: it forwards the child->parent query-lifecycle
 * calls to the parent process as actor messages, where `UrlbarParent` routes
 * them to the real controller keyed by `instanceId`.
 */
export class UrlbarParentControllerProxy {
  /** @type {UrlbarActorPort} */
  #port = window.UrlbarActorPort;

  /** @type {number} */
  #instanceId;

  // The last query context, mirrored locally so the content side can read it
  // (e.g. for key navigation and dismissals) without a round-trip. The parent
  // controller keeps its own; the relevant set/clear points are forwarded here.
  #lastQueryContextWrapper = null;

  /**
   * @param {UrlbarInput} input
   *   Identifies the paired parent-side controller in `UrlbarParent`'s map.
   */
  constructor(input) {
    this.#instanceId = this.#port.registerMessagePathInput(input);

    this.#port.sendAsyncMessage("Init", {
      instanceId: this.#instanceId,
      sapName: input.sapName,
      isPrivate: input.isPrivate,
    });
  }

  /**
   * Registers the paired child controller with the actor so parent->child
   * notifications for this instance can be dispatched to it. The parent
   * controller itself never holds the child on this path (cross-process it
   * can't, and a strong ref would pin the input and defeat cleanup).
   *
   * @param {UrlbarChildController} child
   *   The paired `UrlbarChildController`.
   */
  setChild(child) {
    this.#port.registerChildController(this.#instanceId, child);
  }

  /**
   * Ships an engagement (already built and serialized by the child collector
   * with `UrlbarTelemetryUtils.recordedEngagementToWire()`) to the parent
   * recorder, which fills the parent-only fields and makes the `Glean` call.
   *
   * @param {object} wire The serialized engagement payload.
   */
  recordEngagement(wire) {
    this.#port.sendAsyncMessage("RecordEngagement", {
      instanceId: this.#instanceId,
      wire,
    });
  }

  /**
   * Resets the parent recorder's cross-session telemetry state.
   */
  resetEngagement() {
    this.#port.sendAsyncMessage("ResetEngagement", {
      instanceId: this.#instanceId,
    });
  }

  /**
   * Hands the parent recorder a bounce to track: the child collector builds the
   * event and the parent owns the tracking, since the collector's page may be
   * gone by the time the bounce triggers.
   *
   * @param {BuiltBounce} payload
   *   The bounce this collector built.
   */
  startTrackingBuiltBounce(payload) {
    this.#port.sendAsyncMessage("StartTrackingBuiltBounce", {
      instanceId: this.#instanceId,
      payload,
    });
  }

  /**
   * Ships a search-mode entry to the parent recorder. The counterpart to the
   * controller's `recordSearchMode()`.
   *
   * @param {object} searchMode The search mode being entered.
   */
  recordSearchMode(searchMode) {
    this.#port.sendAsyncMessage("RecordSearchMode", {
      instanceId: this.#instanceId,
      searchMode,
    });
  }

  /**
   * Ships an autofill backspace to the parent. The counterpart to the
   * controller's `recordAutofillBackspace()`.
   *
   * @param {string} url The autofill result URL that was backspaced over.
   */
  recordAutofillBackspace(url) {
    this.#port.sendAsyncMessage("RecordAutofillBackspace", {
      instanceId: this.#instanceId,
      url,
    });
  }

  /**
   * Ships an autofill deletion to the parent recorder. The counterpart to the
   * controller's `recordAutofillDeletion()`.
   */
  recordAutofillDeletion() {
    this.#port.sendAsyncMessage("RecordAutofillDeletion", {
      instanceId: this.#instanceId,
    });
  }

  /** @type {UrlbarParentController["dismissAutofill"]} */
  dismissAutofill(url, action) {
    return this.#port.sendQuery("DismissAutofill", {
      instanceId: this.#instanceId,
      url,
      action,
    });
  }

  /**
   * Ships an accepted autofill to the parent, which clears its backspace
   * bookkeeping. The counterpart to the controller's
   * `clearAutofillBackspaceEntryForUrl()`.
   *
   * @param {string} url The accepted autofill result's URL.
   */
  clearAutofillBackspaceEntryForUrl(url) {
    this.#port.sendAsyncMessage("ClearAutofillBackspaceEntryForUrl", {
      instanceId: this.#instanceId,
      url,
    });
  }

  /**
   * Ships an autofill re-integration to the parent. The counterpart to the
   * controller's `handleAutofillReintegration()`.
   *
   * @param {string} url The URL being re-integrated.
   */
  handleAutofillReintegration(url) {
    this.#port.sendAsyncMessage("HandleAutofillReintegration", {
      instanceId: this.#instanceId,
      url,
    });
  }

  /**
   * Ships a search-form visit to the parent recorder, which resolves the engine
   * by name. The counterpart to the controller's `recordSearchForm()`.
   *
   * @param {string} engineName The name of the engine whose form was visited.
   */
  recordSearchForm(engineName) {
    this.#port.sendAsyncMessage("RecordSearchForm", {
      instanceId: this.#instanceId,
      engineName,
    });
  }

  /**
   * Ships a zero-prefix view event to the parent recorder. The counterpart to
   * the controller's `recordZeroPrefix()`.
   *
   * @param {Parameters<UrlbarParentController["recordZeroPrefix"]>[0]} kind
   *   The zero-prefix event to count.
   */
  recordZeroPrefix(kind) {
    this.#port.sendAsyncMessage("RecordZeroPrefix", {
      instanceId: this.#instanceId,
      kind,
    });
  }

  /**
   * Ships a URL canonization to the parent. The counterpart to the
   * controller's `recordCanonization()`.
   */
  recordCanonization() {
    this.#port.sendAsyncMessage("RecordCanonization", {
      instanceId: this.#instanceId,
    });
  }

  /**
   * Runs the address bar's single-word keyword URI fixup DNS check parent-side.
   * The counterpart to the controller's `checkKeywordURIFixup()`.
   *
   * @param {string} searchString
   *   The string being searched.
   * @param {?number} browserId
   *   The browser the search loads into, or null for the selected browser.
   */
  checkKeywordURIFixup(searchString, browserId) {
    this.#port.sendAsyncMessage("CheckKeywordURIFixup", {
      instanceId: this.#instanceId,
      searchString,
      browserId,
    });
  }

  // Named to match the controller property the child controller forwards to.
  get _lastQueryContextWrapper() {
    return this.#lastQueryContextWrapper;
  }

  /**
   * Starts a query parent-side and resolves when it finishes, mirroring the
   * real controller's `Promise<UrlbarQueryContext>` contract. Callers store the
   * promise as `lastQueryContextPromise` to await search completion, so it must
   * resolve at true completion (not when the message is sent) and with the
   * final, results-bearing context.
   *
   * @param {UrlbarQueryContext} queryContext The query context to run.
   * @returns {Promise<UrlbarQueryContext>} The finished query context.
   */
  startQuery(queryContext) {
    this.#lastQueryContextWrapper = { queryContext };
    return this.#port
      .sendQuery("StartQuery", {
        instanceId: this.#instanceId,
        queryContext: queryContext.toWire(),
      })
      .then(
        wire => UrlbarQueryContext.fromWire(wire),
        error => {
          if (error?.name == "AbortError") {
            // The actor was destroyed before the query finished (the window or
            // tab was torn down mid-query). The query is moot; resolve with the
            // context we started rather than leaving an unhandled rejection,
            // mirroring the direct path.
            return queryContext;
          }
          throw error;
        }
      );
  }

  /**
   * Runs a one-off query parent-side and returns its heuristic result. Async on
   * the caller's side already, so it round-trips through the parent.
   *
   * @param {object} queryContext The query context to run.
   * @returns {Promise<?object>} The heuristic result, or null.
   */
  async getHeuristicResult(queryContext) {
    let wire = await this.#port.sendQuery("GetHeuristicResult", {
      instanceId: this.#instanceId,
      queryContext: queryContext.toWire(),
    });
    return wire ? UrlbarResult.fromWire(wire) : null;
  }

  /**
   * Resolves an Enter with no result available to pick parent-side, returning
   * either a heuristic result to pick or a fixup URL to load.
   *
   * @param {object} details The serializable resolve parameters.
   * @returns {Promise<object>} `{ heuristicResult }`, `{ fixup }`, or `{}`.
   */
  async resolveFallbackNavigation(details) {
    let outcome = await this.#port.sendQuery("ResolveFallbackNavigation", {
      instanceId: this.#instanceId,
      details,
    });
    return outcome.heuristicResult
      ? { heuristicResult: UrlbarResult.fromWire(outcome.heuristicResult) }
      : outcome;
  }

  cancelQuery() {
    this.#port.sendAsyncMessage("CancelQuery", {
      instanceId: this.#instanceId,
    });
  }

  /**
   * Forwards a speculative-connect request to the parent, which resolves the
   * chrome window from the actor and pre-warms the connection. Fire-and-forget:
   * it's a latency optimization with no result to await.
   *
   * @param {UrlbarResult} result The result to speculative connect to.
   * @param {UrlbarQueryContext} context The query context.
   * @param {string} reason The speculative-connect reason.
   */
  speculativeConnect(result, context, reason) {
    this.#port.sendAsyncMessage("SpeculativeConnect", {
      instanceId: this.#instanceId,
      result: result.toWire(),
      queryContext: context.toWire(),
      reason,
    });
  }

  /**
   * Loads a URL in the embedder browser. The params are structured-cloned to
   * the parent; the target browser is resolved there from `loadData.browserId`.
   *
   * @param {Parameters<UrlbarParentController["loadURL"]>[0]} loadData
   * @returns {Promise<ReturnType<UrlbarParentController["loadURL"]>>}
   */
  loadURL(loadData) {
    return this.#port.sendQuery("LoadURL", {
      instanceId: this.#instanceId,
      loadData,
    });
  }

  /**
   * Focuses the browser a deferred-Enter load targeted, resolved parent-side
   * from `browserId`.
   *
   * @param {number} [browserId] The browser the load resolved to, as returned by `loadURL`.
   * @returns {Promise<{focused: boolean}>} Whether the browser was focused.
   */
  focusBrowser(browserId) {
    return this.#port.sendQuery("FocusBrowser", {
      instanceId: this.#instanceId,
      browserId,
    });
  }

  /**
   * Switches to a tab already showing the URL (or opens it), resolved
   * parent-side, along with the follow-up history/open-tab writes.
   *
   * @param {object} loadData The serializable switch parameters.
   */
  switchToTab(loadData) {
    this.#port.sendAsyncMessage("SwitchToTab", {
      instanceId: this.#instanceId,
      loadData,
    });
  }

  /**
   * Records input history parent-side, where the Places write belongs.
   *
   * @param {string} url The picked URL.
   * @param {string} input The search string to associate with it.
   * @param {object} [options]
   * @param {boolean} [options.whenReady]
   *   Whether to defer the write until the URL lands in moz_places.
   */
  addToInputHistory(url, input, { whenReady = false } = {}) {
    this.#port.sendAsyncMessage("AddToInputHistory", {
      instanceId: this.#instanceId,
      url,
      input,
      whenReady,
    });
  }

  /**
   * @param {UrlbarResult} result The result to remove.
   * @param {object} [options] Options forwarded to the parent controller's
   *   removeResult.
   * @param {object} [options.acknowledgeDismissalL10n]
   *   When the result is being dismissed, the l10n for the acknowledgment tip
   *   that replaces its row.
   */
  removeResult(result, options) {
    this.#port.sendAsyncMessage("RemoveResult", {
      instanceId: this.#instanceId,
      result: result.toWire(),
      options,
    });
  }

  /**
   * @param {UrlbarQueryContext} queryContext The context to cache.
   */
  setLastQueryContextCache(queryContext) {
    this.#lastQueryContextWrapper = { queryContext, done: true };
    this.#port.sendAsyncMessage("SetLastQueryContextCache", {
      instanceId: this.#instanceId,
      queryContext: queryContext.toWire(),
    });
  }

  clearLastQueryContextCache() {
    this.#lastQueryContextWrapper = null;
    this.#port.sendAsyncMessage("ClearLastQueryContextCache", {
      instanceId: this.#instanceId,
    });
  }

  /**
   * @param {UrlbarResult} result The result about to be selected.
   */
  onBeforeSelection(result) {
    this.#port.sendAsyncMessage("OnBeforeSelection", {
      instanceId: this.#instanceId,
      result: result.toWire(),
    });
  }

  /**
   * @param {UrlbarResult} result The selected result.
   */
  onSelection(result) {
    this.#port.sendAsyncMessage("OnSelection", {
      instanceId: this.#instanceId,
      result: result.toWire(),
    });
  }

  /**
   * {@link UrlbarParentController#initEngineStore}
   */
  initEngineStore() {
    return this.#port.sendAsyncMessage("InitEngineStore", {
      instanceId: this.#instanceId,
    });
  }

  /**
   * @type {UrlbarParentController["getEngineIconURL"]}
   */
  getEngineIconURL(engineId) {
    return this.#port.sendQuery("GetEngineIconURL", {
      instanceId: this.#instanceId,
      engineId,
    });
  }

  /** @type {UrlbarParentController["markEngineAsUsed"]} */
  markEngineAsUsed(engineId) {
    this.#port.sendAsyncMessage("MarkEngineAsUsed", {
      instanceId: this.#instanceId,
      engineId,
    });
  }

  /** @type {UrlbarParentController["openSERP"]} */
  openSERP(engineId, searchTerms, where, inBackground, browserId, searchData) {
    this.#port.sendAsyncMessage("OpenSERP", {
      instanceId: this.#instanceId,
      engineId,
      searchTerms,
      where,
      inBackground,
      browserId,
      searchData,
    });
  }

  /** @type {UrlbarParentController["openSearchForm"]} */
  openSearchForm(engineId, where, inBackground, browserId) {
    this.#port.sendAsyncMessage("OpenSearchForm", {
      instanceId: this.#instanceId,
      engineId,
      where,
      inBackground,
      browserId,
    });
  }

  /** @type {UrlbarParentController["openPreferences"]} */
  openPreferences(paneID, extraArgs) {
    this.#port.sendAsyncMessage("OpenPreferences", {
      instanceId: this.#instanceId,
      paneID,
      extraArgs,
    });
  }

  /** @type {UrlbarParentController["openContainerCreationPanel"]} */
  openContainerCreationPanel(entrypoint) {
    this.#port.sendAsyncMessage("OpenContainerCreationPanel", {
      instanceId: this.#instanceId,
      entrypoint,
    });
  }
}
