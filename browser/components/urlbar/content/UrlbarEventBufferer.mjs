/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import * as UrlbarContentUtils from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";

/**
 * Array of keyCodes to defer.
 *
 * @type {Set<number>}
 */
const DEFERRED_KEY_CODES = new Set([
  KeyboardEvent.DOM_VK_RETURN,
  KeyboardEvent.DOM_VK_DOWN,
  KeyboardEvent.DOM_VK_TAB,
]);

/**
 * Status of the current or last query.
 */
const QUERY_STATUS = Object.freeze({
  UKNOWN: 0,
  RUNNING: 1,
  RUNNING_GOT_ALL_HEURISTIC_RESULTS: 2,
  COMPLETE: 3,
});

/**
 * The UrlbarEventBufferer can queue up events and replay them later, to make
 * the urlbar results more predictable.
 *
 * Search results arrive asynchronously, which means that keydown events may
 * arrive before results do, and therefore not have the effect the user intends.
 * That's especially likely to happen with the down arrow and enter keys, due to
 * the one-off search buttons: if the user very quickly pastes something in the
 * input, presses the down arrow key, and then hits enter, they are probably
 * expecting to visit the first result.  But if there are no results, then
 * pressing down and enter will trigger the first one-off button.
 * To prevent that undesirable behavior, certain keys are buffered and deferred
 * until more results arrive, at which time they're replayed.
 */
export class UrlbarEventBufferer {
  /** @type {Console|undefined} */
  #logger;
  get logger() {
    this.#logger ??= UrlbarShared.getLogger({
      prefix: "EventBufferer",
    });
    return this.#logger;
  }

  // Maximum time events can be deferred for.
  static get DEFERRING_TIMEOUT_MS() {
    return UrlbarPrefs.get("eventBufferer.deferringTimeoutMs");
  }

  /**
   * Initialises the class.
   *
   * @param {UrlbarInput} input
   *   The urlbar input object.
   */
  constructor(input) {
    this.input = input;
    this.input.inputField.addEventListener("blur", this);

    this.#lastQuery = {
      // The time at which the current or last search was started. This is used
      // to check how much time passed while deferring the user's actions. Must
      // be set using the monotonic performance.now() helper.
      startDate: performance.now(),
      // Status of the query; one of QUERY_STATUS.*
      status: QUERY_STATUS.UKNOWN,
      // The query context.
      context: null,
    };

    // Start listening for queries.
    this.input.controller.addListener(this);
  }

  /**
   * Arms the bufferer to defer subsequent events until results arrive. Called by
   * the controller as it starts a query, before the query is dispatched --
   * unlike the QUERY_STARTED notification, which the bufferer would otherwise
   * observe a round-trip late over the actor message path, after a just-typed
   * Enter had already been handled.
   *
   * @param {UrlbarQueryContext} queryContext
   */
  queryStarting(queryContext) {
    this.#lastQuery = {
      startDate: performance.now(),
      status: QUERY_STATUS.RUNNING,
      context: queryContext,
    };
    if (this.#deferringTimeout) {
      clearTimeout(this.#deferringTimeout);
      this.#deferringTimeout = null;
    }
  }

  // UrlbarParentController listener methods.

  onQueryCancelled() {
    this.#lastQuery.status = QUERY_STATUS.COMPLETE;
  }

  onQueryFinished() {
    this.#lastQuery.status = QUERY_STATUS.COMPLETE;
  }

  /**
   * Handles results of the query.
   *
   * @param {UrlbarQueryContext} queryContext
   */
  onQueryResults(queryContext) {
    if (queryContext.pendingHeuristicProviders.size) {
      return;
    }
    this.#lastQuery.status = QUERY_STATUS.RUNNING_GOT_ALL_HEURISTIC_RESULTS;
    // Refresh the stored context with the results-bearing one: queryStarting
    // armed the bufferer with the pre-query context, whose results are
    // populated on the parent's own copy over the message path.
    this.#lastQuery.context = queryContext;
    // Ensure this runs after other results handling code.
    setTimeout(() => {
      this.replayDeferredEvents(true);
    }, 0);
  }

  /**
   * Handles DOM events.
   *
   * @param {Event} event
   *   DOM event from the input.
   */
  handleEvent(event) {
    if (event.type == "blur") {
      this.logger.debug("Clearing queue on blur");
      // The input field was blurred, pending events don't matter anymore.
      // Clear the timeout and the queue.
      this.#eventsQueue.length = 0;
      if (this.#deferringTimeout) {
        clearTimeout(this.#deferringTimeout);
        this.#deferringTimeout = null;
      }
    }
  }

  /**
   * Receives DOM events, eventually queues them up, and calls back when it's
   * the right time to handle the event.
   *
   * @param {KeyboardEvent} event DOM event from the input.
   * @param {() => void} callback to be invoked when it's the right time to handle
   *        the event.
   */
  maybeDeferEvent(event, callback) {
    if (!callback) {
      throw new Error("Must provide a callback");
    }
    if (this.shouldDeferEvent(event)) {
      this.deferEvent(event, callback);
      return;
    }
    // If it has not been deferred, handle the callback immediately.
    callback();
  }

  /**
   * Adds a deferrable event to the deferred event queue.
   *
   * @param {KeyboardEvent} event The event to defer.
   * @param {() => void} callback to be invoked when it's the right time to handle
   *        the event.
   */
  deferEvent(event, callback) {
    // Check we don't try to defer events more than once.
    if (this.#eventsQueue.find(item => item.event == event)) {
      throw new Error(`Event ${event.type}:${event.keyCode} already deferred!`);
    }
    this.logger.debug(`Deferring ${event.type}:${event.keyCode} event`);
    this.#eventsQueue.push({
      event,
      callback,
      // Also store the current search string, as an added safety check. If the
      // string will differ later, the event is stale and should be dropped.
      searchString: this.#lastQuery.context.searchString,
    });

    if (!this.#deferringTimeout) {
      let elapsed = performance.now() - this.#lastQuery.startDate;
      let remaining = UrlbarEventBufferer.DEFERRING_TIMEOUT_MS - elapsed;
      this.#deferringTimeout = setTimeout(
        () => {
          this.replayDeferredEvents(false);
          this.#deferringTimeout = null;
        },
        Math.max(0, remaining)
      );
    }
  }

  /**
   * Replays deferred key events.
   *
   * @param {boolean} onlyIfSafe replays only if it's a safe time to do so.
   *        Setting this to false will replay all the queue events, without any
   *        checks, that is something we want to do only if the deferring
   *        timeout elapsed, and we don't want to appear ignoring user's input.
   */
  replayDeferredEvents(onlyIfSafe) {
    if (typeof onlyIfSafe != "boolean") {
      throw new Error("Must provide a boolean argument");
    }
    if (!this.#eventsQueue.length) {
      return;
    }

    let { event, callback, searchString } = this.#eventsQueue[0];
    if (onlyIfSafe && !this.isSafeToPlayDeferredEvent(event)) {
      return;
    }

    // Remove the event from the queue and play it.
    this.#eventsQueue.shift();
    // Safety check: handle only if the search string didn't change meanwhile.
    if (searchString == this.#lastQuery.context.searchString) {
      callback();
    }
    setTimeout(() => {
      this.replayDeferredEvents(onlyIfSafe);
    }, 0);
  }

  /**
   * Checks whether a given event should be deferred
   *
   * @param {KeyboardEvent} event The event that should maybe be deferred.
   * @returns {boolean} Whether the event should be deferred.
   */
  shouldDeferEvent(event) {
    // If any event has been deferred for this search, then defer all subsequent
    // events so that the user does not experience them out of order.
    // All events will be replayed when #deferringTimeout fires.
    if (this.#eventsQueue.length) {
      return true;
    }

    // At this point, no events have been deferred for this search; we must
    // figure out if this event should be deferred.
    let isMacNavigation =
      UrlbarContentUtils.getPlatform() == "macosx" &&
      event.ctrlKey &&
      this.input.view.isOpen &&
      (event.key === "n" || event.key === "p");
    if (!DEFERRED_KEY_CODES.has(event.keyCode) && !isMacNavigation) {
      return false;
    }

    if (DEFERRED_KEY_CODES.has(event.keyCode)) {
      // Defer while the user is composing.
      if (this.input.isComposing) {
        return true;
      }
      if (this.input.controller.keyEventMovesCaret(event)) {
        return false;
      }
    }

    // This is an event that we'd defer, but if enough time has passed since the
    // start of the search, we don't want to block the user's workflow anymore.
    if (
      this.#lastQuery.startDate + UrlbarEventBufferer.DEFERRING_TIMEOUT_MS <=
      performance.now()
    ) {
      return false;
    }

    if (
      event.keyCode == KeyEvent.DOM_VK_TAB &&
      !this.input.view.isOpen &&
      !this.waitingDeferUserSelectionProviders
    ) {
      // The view is closed and the user pressed the Tab key.  The focus should
      // move out of the urlbar immediately.
      return false;
    }

    return !this.isSafeToPlayDeferredEvent(event);
  }

  /**
   * Checks if the bufferer is deferring events.
   *
   * @returns {boolean} Whether the bufferer is deferring events.
   */
  get isDeferringEvents() {
    return !!this.#eventsQueue.length;
  }

  /**
   * Checks if any of the current query provider asked to defer user selection
   * events.
   *
   * @returns {boolean} Whether a provider asked to defer events.
   */
  get waitingDeferUserSelectionProviders() {
    return !!this.#lastQuery.context?.deferUserSelectionProviders.size;
  }

  /**
   * Returns true if the given deferred event can be played now without possibly
   * surprising the user.  This depends on the state of the view, the results,
   * and the type of event.
   * Use this method only after determining that the event should be deferred,
   * or after it has been deferred and you want to know if it can be played now.
   *
   * @param {KeyboardEvent} event The event.
   * @returns {boolean} Whether the event can be played.
   */
  isSafeToPlayDeferredEvent(event) {
    if (
      this.#lastQuery.status == QUERY_STATUS.COMPLETE ||
      this.#lastQuery.status == QUERY_STATUS.UKNOWN
    ) {
      // The view can't get any more results, so there's no need to further
      // defer events.
      return true;
    }
    let waitingHeuristicResults =
      this.#lastQuery.status == QUERY_STATUS.RUNNING;
    if (event.keyCode == KeyEvent.DOM_VK_RETURN) {
      // Check if we're waiting for providers that requested deferring.
      if (this.waitingDeferUserSelectionProviders) {
        return false;
      }
      // Play a deferred Enter if the heuristic result is not selected, or we
      // are not waiting for heuristic results yet.
      let selectedResult = this.input.view.selectedResult;
      return (
        (selectedResult && !selectedResult.heuristic) ||
        !waitingHeuristicResults
      );
    }

    if (
      waitingHeuristicResults ||
      !this.input.view.isOpen ||
      this.waitingDeferUserSelectionProviders
    ) {
      // We're still waiting on some results, or the popup hasn't opened yet.
      return false;
    }

    let isMacDownNavigation =
      UrlbarContentUtils.getPlatform() == "macosx" &&
      event.ctrlKey &&
      this.input.view.isOpen &&
      event.key === "n";
    if (event.keyCode == KeyEvent.DOM_VK_DOWN || isMacDownNavigation) {
      // Don't play the event if the last result is selected so that the user
      // doesn't accidentally arrow down into the one-off buttons when they
      // didn't mean to. Note TAB is unaffected because it only navigates
      // results, not one-offs.
      return !this.lastResultIsSelected;
    }

    return true;
  }

  get lastResultIsSelected() {
    // TODO Bug 1536818: Once one-off buttons are fully implemented, it would be
    // nice to have a better way to check if the next down will focus one-off buttons.
    let results = this.#lastQuery.context.results;
    return (
      results.length &&
      results[results.length - 1] == this.input.view.selectedResult
    );
  }

  /**
   * A queue of deferred events.
   * The callback is invoked when it's the right time to handle the event,
   * but it may also never be invoked, if the context changed and the event
   * became obsolete.
   *
   * @type {{event: KeyboardEvent, callback: () => void, searchString: string}[]}
   */
  #eventsQueue = [];

  /**
   * If this timer fires, we will unconditionally replay all the deferred
   * events so that, after a certain point, we don't keep blocking the user's
   * actions, when nothing else has caused the events to be replayed.
   * At that point we won't check whether it's safe to replay the events,
   * because otherwise it may look like we ignored the user's actions.
   *
   * @type {?number}
   */
  #deferringTimeout = null;

  /**
   * Tracks the current or last query status.
   *
   * @type {{ startDate: number, status: Values<typeof QUERY_STATUS>, context: UrlbarQueryContext}}
   */
  #lastQuery;
}
