/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import { UrlbarQueryContext } from "chrome://browser/content/urlbar/UrlbarQueryContext.mjs";
import { UrlbarChildTelemetry } from "chrome://browser/content/urlbar/UrlbarChildTelemetry.mjs";
import { UrlbarParentControllerProxy } from "chrome://browser/content/urlbar/UrlbarParentControllerProxy.mjs";
import * as UrlbarContentUtils from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";

const lazy = typeof ChromeUtils != "undefined" ? {} : null;

if (lazy) {
  ChromeUtils.defineESModuleGetters(lazy, {
    UrlbarParentController:
      "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs",
  });
}

/**
 * @import {UrlbarChild} from "../../../actors/UrlbarChild.sys.mjs"
 * @import {UrlbarInput} from "chrome://browser/content/urlbar/UrlbarInput.mjs"
 * @import {UrlbarParentController} from "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs"
 * @import {UrlbarView} from "chrome://browser/content/urlbar/UrlbarView.mjs"
 * @import {SmartbarInput} from "moz-src:///browser/components/urlbar/content/SmartbarInput.mjs"
 */

import { SearchEngineStore } from "chrome://browser/content/urlbar/SearchEngineStore.mjs";

/**
 * The in-process face of the address bar controller. Lives next to the
 * `<moz-urlbar>` custom element and forwards work that has to happen in
 * the parent process to a paired `UrlbarParentController` via the
 * `UrlbarChild`/`UrlbarParent` JSWindowActor pair. The actor owns the
 * per-instance bookkeeping (instance id, lifetime); this wrapper just
 * holds the controller it hands back.
 *
 * The wrapper abstracts the transport: on the direct path (chrome `<moz-urlbar>`)
 * the actor pair hands back the real `UrlbarParentController` and calls happen
 * synchronously, while on the message path (a content-process `<moz-urlbar>`, or
 * chrome with the pref) it gets a proxy implementing the same surface. Callers
 * (`UrlbarInput`, `UrlbarView`) don't distinguish them.
 */
export class UrlbarChildController {
  /** @type {Console} */
  static #logger;

  get logger() {
    if (!UrlbarChildController.#logger) {
      UrlbarChildController.#logger = UrlbarShared.getLogger({
        prefix: "ChildController",
      });
    }
    return UrlbarChildController.#logger;
  }

  #parentController;

  #input;

  /** @type {UrlbarView} */
  #view = null;

  // Listeners (the view, the event bufferer, the search one-offs) live here, on
  // the child's side; the parent delegates its notifications to us via
  // setChild(). Keeping dispatch where the listeners are is what lets
  // `<moz-urlbar>` run in a content process.
  #listeners = new Set();

  #userSelectionBehavior = /** @type {"arrow"|"tab"|"none"} */ ("none");

  // The id of the query the listeners are still hearing about. Notifications
  // carrying an older id belong to a query nobody is waiting for anymore.
  #queryId = 0;

  // Whether the query identified by `#queryId` was cancelled. Only meaningful
  // while it's held back waiting for the engine store; a cancel can't be taken
  // as a statement about in-flight results, since the view cancels for reasons
  // of its own (freezing the list, closing on a stale query's completion).
  #queryCancelled = false;

  // The content-side engagement-telemetry collector, created lazily on the
  // message path (where the parent stand-in has no `engagementEvent`).
  #childTelemetry = null;

  /**
   * @param {object} options
   * @param {UrlbarInput} options.input
   */
  constructor(options) {
    if (!options.input) {
      throw new Error("Missing options: input");
    }
    this.#input = options.input;
    // A privileged (parent-process/chrome) input reaches the actor directly. A
    // content-realm input can't: `windowGlobalChild.getActor` is `[ChromeOnly]`
    // and the actor is a system-principal object it can't hold, so it uses the
    // stand-in the actor exposes on the window (see `UrlbarChild.exposePort`).
    // `ChromeUtils` is the same discriminator `UrlbarContentPrefs.mjs` uses:
    // defined in a privileged realm, absent in content.
    let inChromeRealm = typeof ChromeUtils != "undefined";
    let actor = inChromeRealm
      ? /** @type {UrlbarChild} */ (
          /** @type {unknown} */ (
            options.input.window.windowGlobalChild.getActor("Urlbar")
          )
        )
      : /** @type {UrlbarChild} */ (options.input.window.UrlbarActorPort);
    let { sapName, isPrivate } = options.input;
    // A content-realm input has no in-process parent, so it always takes the
    // message path; a privileged one asks the actor (it honors the
    // chrome-message-passing pref). The message path builds a proxy that trades
    // actor messages with the parent-side controller; the direct path builds
    // the real controller in place.
    let usesMessagePath = !inChromeRealm || actor.usesMessagePath;
    this.#parentController = /** @type {UrlbarParentController} */ (
      usesMessagePath
        ? new UrlbarParentControllerProxy(
            actor,
            actor.registerMessagePathInput(options.input),
            { sapName, isPrivate }
          )
        : new lazy.UrlbarParentController({ sapName, isPrivate, actor })
    );
    this.#parentController.setChild(this);

    this.engineStore = new SearchEngineStore(this);
  }

  /**
   * @type {typeof SearchEngineStore.prototype.receive}
   */
  updateEngineStore(...args) {
    this.engineStore.receive(...args);
  }

  get input() {
    return this.#input;
  }
  // The window the input lives in. For a chrome `<moz-urlbar>` this is the
  // browser window; for a content-process one it's the content window.
  get window() {
    return this.#input.window;
  }
  get view() {
    return this.#view;
  }

  /**
   * The paired parent controller -- the real `UrlbarParentController` on the
   * direct path, or the `UrlbarParentControllerProxy` on the message path.
   *
   * @type {UrlbarParentController}
   */
  get parentController() {
    return this.#parentController;
  }
  get engagementEvent() {
    // Direct path: the real parent controller's recorder. Message path: the
    // parent stand-in has none, so use a content-side collector that ships
    // engagements to the parent recorder.
    return (
      this.#parentController.engagementEvent ??
      (this.#childTelemetry ??= new UrlbarChildTelemetry(this))
    );
  }
  /**
   * The selection behavior that the user has used to select a result. The
   * setter ignores a change to "arrow" once "tab" has been recorded, since we
   * want to know that tab was used first.
   *
   * @type {"arrow"|"tab"|"none"}
   */
  get userSelectionBehavior() {
    return this.#userSelectionBehavior;
  }

  set userSelectionBehavior(behavior) {
    // Don't change the behavior to arrow if tab has already been recorded,
    // as we want to know that the tab was used first.
    if (behavior == "arrow" && this.#userSelectionBehavior == "tab") {
      return;
    }
    this.#userSelectionBehavior = behavior;
  }
  get _lastQueryContextWrapper() {
    return this.#parentController._lastQueryContextWrapper;
  }

  setView(view) {
    this.#view = view;
  }
  onBeforeSelection(result, element) {
    return this.#parentController.onBeforeSelection(result, element);
  }
  onSelection(result, element) {
    return this.#parentController.onSelection(result, element);
  }
  getHeuristicResult(queryContext) {
    return this.#parentController.getHeuristicResult(queryContext);
  }
  async resolveFallbackNavigation(details) {
    // The result this hands back is picked like a query's, and paste-and-go
    // suppresses the query that would otherwise have waited for the store.
    await this.#engineStoreReady();
    return this.#parentController.resolveFallbackNavigation(details);
  }
  addListener(listener) {
    if (!listener || typeof listener != "object") {
      throw new TypeError("Expected listener to be an object");
    }
    this.#listeners.add(listener);
  }
  removeListener(listener) {
    this.#listeners.delete(listener);
  }
  /**
   * Takes a notification off the wire, building the query context in this realm
   * before anything reads it.
   *
   * @param {string} notification
   *   The notification, one of `UrlbarShared.NOTIFICATIONS`.
   * @param {...any} params
   *   The notification's arguments, the query context in its wire form.
   */
  notifyFromWire(notification, ...params) {
    this.notify(
      notification,
      ...params.map(param =>
        param?.serializedQueryContext
          ? UrlbarQueryContext.fromWire(param.serializedQueryContext)
          : param
      )
    );
  }
  /**
   * Hands a notification to the listeners, dropping the results and the end of
   * a query they have moved on from.
   *
   * @param {string} notification
   *   The notification, one of `UrlbarShared.NOTIFICATIONS`.
   * @param {...any} params
   *   The notification's arguments. The query lifecycle ones take the query
   *   context.
   */
  notify(notification, ...params) {
    // Drop the results and the end of a query nobody is waiting for anymore.
    // Its end, read against results the listeners never saw, would tell the
    // view that the query produced nothing: the view would clear its rows and
    // close, cancelling the query that took over. QUERY_STARTED still goes
    // through -- it carries the per-query state the listeners reset, and the
    // query they do track resets it again when it starts.
    if (
      (notification === UrlbarShared.NOTIFICATIONS.QUERY_FIRST_RESULT ||
        notification === UrlbarShared.NOTIFICATIONS.QUERY_RESULTS ||
        notification === UrlbarShared.NOTIFICATIONS.QUERY_FINISHED) &&
      params[0].id < this.#queryId
    ) {
      return;
    }
    // When the first results arrive, pre-warm a connection to the heuristic
    // result.
    if (
      notification === UrlbarShared.NOTIFICATIONS.QUERY_RESULTS &&
      params[0].firstResultChanged
    ) {
      this.speculativeConnect(params[0].results[0], params[0], "resultsadded");
    }
    for (let listener of this.#listeners) {
      // Can't use "in" because some tests proxify these.
      if (typeof listener[notification] != "undefined") {
        try {
          listener[notification](...params);
        } catch (ex) {
          console.error(ex);
        }
      }
    }
  }
  recordEngagement(wire) {
    return this.#parentController.recordEngagement(wire);
  }
  resetEngagement() {
    return this.#parentController.resetEngagement();
  }
  handleBounceTrigger(payload) {
    return this.#parentController.handleBounceTrigger(payload);
  }
  trackBounceBrowser(browserId) {
    return this.#parentController.trackBounceBrowser(browserId);
  }
  recordAutofillBackspace(url) {
    return this.#parentController.recordAutofillBackspace(url);
  }
  clearAutofillBackspaceEntryForUrl(url) {
    return this.#parentController.clearAutofillBackspaceEntryForUrl(url);
  }
  dismissAutofill(url, action) {
    return this.#parentController.dismissAutofill(url, action);
  }
  recordAutofillDeletion() {
    return this.#parentController.recordAutofillDeletion();
  }
  handleAutofillReintegration(url) {
    return this.#parentController.handleAutofillReintegration(url);
  }
  recordSearchMode(searchMode) {
    return this.#parentController.recordSearchMode(searchMode);
  }
  recordSearchForm(engineName) {
    return this.#parentController.recordSearchForm(engineName);
  }
  recordSearch(options) {
    return this.#parentController.recordSearch(options);
  }
  recordSearchInOpenedTab(searchData) {
    return this.#parentController.recordSearchInOpenedTab(searchData);
  }

  checkKeywordURIFixup(searchString, browserId) {
    return this.#parentController.checkKeywordURIFixup(searchString, browserId);
  }
  /**
   * Starts a query and returns the parent controller's promise so callers (the
   * input's `lastQueryContextPromise`, which tests await) can track completion.
   *
   * A query that would run before the engine store is populated is held back
   * until it is. Results are produced by the providers in the parent, which use
   * the search service directly, so a query dispatched before the store is
   * ready can deliver results to a UI that has no engines to look up. Holding
   * the query back is what keeps every result-handling path downstream of a
   * populated store.
   *
   * @param {UrlbarQueryContext} queryContext
   * @returns {Promise<UrlbarQueryContext>}
   *   Resolves with the finished context, or with the untouched one if the
   *   query was cancelled or superseded while waiting for the engine store.
   */
  startQuery(queryContext) {
    this.#queryId = queryContext.id;
    this.#queryCancelled = false;

    if (this.engineStore.initialized || this.engineStore.failed) {
      return this.#dispatchQuery(queryContext);
    }

    // Arm the bufferer up front so an Enter typed during the wait is deferred
    // too. Nothing can tear down the previous query in the meantime, which is
    // the only thing #dispatchQuery's arm-after-dispatch ordering guards
    // against.
    this.#input.eventBufferer.queryStarting(queryContext);

    return this.#engineStoreReady().then(() =>
      this.#queryId == queryContext.id && !this.#queryCancelled
        ? this.#dispatchQuery(queryContext)
        : queryContext
    );
  }
  /**
   * Hands a query to the parent controller.
   *
   * @param {UrlbarQueryContext} queryContext
   * @returns {Promise<UrlbarQueryContext>} Resolves with the finished context.
   */
  #dispatchQuery(queryContext) {
    let queryContextPromise = this.#parentController.startQuery(queryContext);
    // Arm the event bufferer as the query starts so a just-typed Enter is
    // deferred until results arrive; it can't wait for the QUERY_STARTED
    // notification, which arrives a round-trip late over the message path, after
    // the key event. Arm after dispatching so the parent's synchronous teardown
    // of the previous query (in-process) can't clobber the freshly-armed state.
    this.#input.eventBufferer.queryStarting(queryContext);
    return queryContextPromise;
  }
  /**
   * Waits for the engine store to be populated.
   *
   * @returns {Promise<void>}
   *   Resolves once the store holds engines, or immediately if it never will
   *   because the search service failed. It doesn't reject: waiting on a store
   *   that can't be populated would keep the input from producing results at
   *   all.
   */
  async #engineStoreReady() {
    if (this.engineStore.initialized || this.engineStore.failed) {
      return;
    }
    try {
      await this.engineStore.init();
    } catch {
      // The search service failed.
    }
  }
  cancelQuery() {
    // A query still waiting for the engine store must not be dispatched at all.
    this.#queryCancelled = true;
    return this.#parentController.cancelQuery();
  }
  /**
   * Takes the running query away from the listeners, reporting it to them as
   * cancelled: nothing more of it reaches them, results or end. The input calls
   * this when it takes the query over after the first result -- entering search
   * mode and restarting it -- since the results are about to be replaced. The
   * query keeps running until the restart cancels it, which over the message
   * path takes a round trip.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The context of the query being discarded.
   */
  discardResults(queryContext) {
    this.#queryId++;
    this.notify(UrlbarShared.NOTIFICATIONS.QUERY_CANCELLED, queryContext);
  }
  receiveResults(queryContext) {
    return this.#parentController.receiveResults(queryContext);
  }
  removeResult(result, options) {
    return this.#parentController.removeResult(result, options);
  }
  setLastQueryContextCache(queryContext) {
    return this.#parentController.setLastQueryContextCache(queryContext);
  }
  clearLastQueryContextCache() {
    return this.#parentController.clearLastQueryContextCache();
  }
  /**
   * Receives keyboard events from the input and handles those that should
   * navigate within the view or pick the currently selected item.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   * @param {boolean} executeAction
   *   Whether the event should actually execute the associated action, or just
   *   be managed (at a preventDefault() level). This is used when the event
   *   will be deferred by the event bufferer, but preventDefault() and friends
   *   should still happen synchronously.
   */
  // eslint-disable-next-line complexity
  handleKeyNavigation(event, executeAction = true) {
    // If the resultMenu is open then let them handle any key events.
    if (this.view.resultMenu.hasAttribute("open")) {
      return;
    }

    const isMac = UrlbarContentUtils.getPlatform() == "macosx";
    // Handle readline/emacs-style navigation bindings on Mac.
    if (
      isMac &&
      this.view.isOpen &&
      event.ctrlKey &&
      (event.key == "n" || event.key == "p")
    ) {
      if (executeAction) {
        this.view.selectBy(1, { reverse: event.key == "p" });
      }
      event.preventDefault();
      return;
    }

    if (executeAction) {
      // In native inputs on most platforms, Shift+Up/Down moves the caret to the
      // start/end of the input and changes its selection, so in that case defer
      // handling to the input instead of changing the view's selection.
      if (
        event.shiftKey &&
        (event.keyCode === KeyEvent.DOM_VK_UP ||
          event.keyCode === KeyEvent.DOM_VK_DOWN)
      ) {
        return;
      }

      let handled = false;
      if (UrlbarPrefs.get("scotchBonnet.enableOverride")) {
        handled = this.input.searchModeSwitcher.handleKeyDown(event);
      } else if (this.view.isOpen && this._lastQueryContextWrapper) {
        let { queryContext } = this._lastQueryContextWrapper;
        handled = this.view.oneOffSearchButtons?.handleKeyDown(
          event,
          this.view.visibleRowCount,
          this.view.allowEmptySelection,
          queryContext.searchString
        );
      }
      if (handled) {
        return;
      }
    }

    switch (event.keyCode) {
      case KeyEvent.DOM_VK_ESCAPE:
        if (executeAction) {
          if (this.view.isOpen) {
            this.view.close();
          } else if (
            // Moving focus into the content document only makes sense for a
            // chrome moz-urlbar; a content-process one already has focus in
            // content. Only a browser window has `gBrowser`.
            this.window.gBrowser &&
            UrlbarPrefs.get("focusContentDocumentOnEsc") &&
            !this.input.searchMode &&
            (this.input.sapName == "searchbar"
              ? this.input.value == ""
              : this.input.getAttribute("pageproxystate") == "valid" ||
                (this.input.value == "" &&
                  this.window.isBlankPageURL(
                    this.window.gBrowser.currentURI.spec
                  )))
          ) {
            this.window.gBrowser.selectedBrowser.focus();
          } else {
            this.input.handleRevert();
          }
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_SPACE:
        if (!this.view.shouldSpaceActivateSelectedElement()) {
          break;
        }
      // Fall through, we want the SPACE key to activate this element.
      case KeyEvent.DOM_VK_RETURN:
        this.logger.debug(`Enter pressed${executeAction ? "" : " delayed"}`);
        if (executeAction) {
          this.input.handleCommand(event);
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_TAB: {
        if (!this.view.visibleRowCount) {
          // Leave it to the default behaviour if there are not results.
          break;
        }

        // In smartbar mode, mirror the urlbar's circular Tab pattern: cycle
        // through results, then continue into the action buttons (Add Tab,
        // memories, Submit), then wrap back to the first result. Shift+Tab
        // mirrors the cycle in reverse. The view stays open the whole time;
        // Tab from the action buttons back into the result list is handled
        // by SmartbarInput.#onActionButtonsKeyDown.
        if (
          this.input.sapName == "smartbar" &&
          this.view.isOpen &&
          !event.ctrlKey &&
          !event.altKey
        ) {
          const atEnd =
            !event.shiftKey &&
            this.view.selectedElement == this.view.getLastSelectableElement();
          const atStart =
            event.shiftKey &&
            this.view.selectedElement == this.view.getFirstSelectableElement();

          if (atEnd || atStart) {
            if (executeAction) {
              this.view.selectedRowIndex = -1;
              // SAP is `smartbar`, so we can safely cast to SmartbarInput.
              const smartbar = /** @type {SmartbarInput} */ (
                /** @type {unknown} */ (this.input)
              );
              if (atEnd) {
                smartbar.focusFirstActionButton();
              } else {
                smartbar.focusLastActionButton();
              }
            }
            event.preventDefault();
            break;
          }
          // Otherwise, fall through to the default cycling behaviour.
        }

        // Change the tab behavior when urlbar view is open.
        if (
          UrlbarPrefs.get("scotchBonnet.enableOverride") &&
          this.view.isOpen &&
          !event.ctrlKey &&
          !event.altKey
        ) {
          if (
            (event.shiftKey &&
              this.view.selectedElement ==
                this.view.getFirstSelectableElement()) ||
            (!event.shiftKey &&
              this.view.selectedElement == this.view.getLastSelectableElement())
          ) {
            // If pressing tab + shift when the first or pressing tab when last
            // element has been selected, move the focus to the Unified Search
            // Button. Then make urlbar results selectable by tab + shift.
            event.preventDefault();
            this.view.selectedRowIndex = -1;
            this.focusOnUnifiedSearchButton();
            break;
          } else if (
            !this.view.selectedElement &&
            this.input.focusedViaMousedown
          ) {
            if (event.shiftKey) {
              this.focusOnUnifiedSearchButton();
            } else {
              this.view.selectBy(1, {
                userPressedTab: true,
              });
            }
            event.preventDefault();
            break;
          }
        }

        // It's always possible to tab through results when the urlbar was
        // focused with the mouse or has a search string, or when the view
        // already has a selection.
        // We allow tabbing without a search string when in search mode preview,
        // since that means the user has interacted with the Urlbar since
        // opening it.
        // When there's no search string and no view selection, we want to focus
        // the next toolbar item instead, for accessibility reasons.
        let allowTabbingThroughResults =
          this.input.focusedViaMousedown ||
          this.input.searchMode?.isPreview ||
          this.input.searchMode?.source == UrlbarShared.RESULT_SOURCE.ACTIONS ||
          this.view.selectedElement ||
          (this.input.value &&
            this.input.getAttribute("pageproxystate") != "valid");
        if (
          // Even if the view is closed, we may be waiting results, and in
          // such a case we don't want to tab out of the urlbar.
          (this.view.isOpen || !executeAction) &&
          !event.ctrlKey &&
          !event.altKey &&
          allowTabbingThroughResults
        ) {
          if (executeAction) {
            this.userSelectionBehavior = "tab";
            this.view.selectBy(1, {
              reverse: event.shiftKey,
              userPressedTab: true,
            });
          }
          event.preventDefault();
        }
        break;
      }
      case KeyEvent.DOM_VK_PAGE_DOWN:
      case KeyEvent.DOM_VK_PAGE_UP:
        if (event.ctrlKey) {
          break;
        }
      // eslint-disable-next-lined no-fallthrough
      case KeyEvent.DOM_VK_DOWN:
      case KeyEvent.DOM_VK_UP:
        if (event.altKey) {
          break;
        }
        if (this.view.isOpen) {
          if (executeAction) {
            this.userSelectionBehavior = "arrow";
            this.view.selectBy(
              event.keyCode == KeyEvent.DOM_VK_PAGE_DOWN ||
                event.keyCode == KeyEvent.DOM_VK_PAGE_UP
                ? UrlbarShared.PAGE_UP_DOWN_DELTA
                : 1,
              {
                reverse:
                  event.keyCode == KeyEvent.DOM_VK_UP ||
                  event.keyCode == KeyEvent.DOM_VK_PAGE_UP,
              }
            );
          }
        } else {
          if (this.keyEventMovesCaret(event)) {
            break;
          }
          if (executeAction) {
            this.userSelectionBehavior = "arrow";
            this.input.startQuery({
              searchString: this.input.value,
              event,
            });
          }
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_RIGHT:
      case KeyEvent.DOM_VK_END:
        this.input.maybeConfirmSearchModeFromResult({
          entry: "typed",
          startQuery: true,
        });
      // Fall through.
      case KeyEvent.DOM_VK_LEFT:
      case KeyEvent.DOM_VK_HOME:
        this.view.removeAccessibleFocus();
        break;
      case KeyEvent.DOM_VK_BACK_SPACE:
        if (
          this.input.searchMode &&
          this.input.selectionStart == 0 &&
          this.input.selectionEnd == 0 &&
          !event.shiftKey
        ) {
          this.input.searchMode = null;
          if (this.input.view.oneOffSearchButtons) {
            this.input.view.oneOffSearchButtons.selectedButton = null;
          }
          this.input.startQuery({
            allowAutofill: false,
            event,
          });
        }
      // Fall through.
      case KeyEvent.DOM_VK_DELETE:
        if (!this.view.isOpen) {
          break;
        }
        if (event.shiftKey) {
          if (!executeAction || this.#dismissSelectedResult(event)) {
            event.preventDefault();
          }
        } else if (executeAction) {
          this.userSelectionBehavior = "none";
        }
        break;
    }
  }

  /**
   * Triggers a "dismiss" engagement for the selected result if one is selected.
   * Providers that can respond to dismissals of their results should implement
   * `onEngagement()`, handle the dismissal, and call `controller.removeResult()`.
   *
   * @param {Event} event
   *   The event that triggered dismissal.
   * @returns {boolean}
   *   Whether providers were notified about the engagement. Providers will not
   *   be notified if there is no selected result or the selected result is the
   *   heuristic, since the heuristic result cannot be dismissed.
   */
  #dismissSelectedResult(event) {
    if (!this._lastQueryContextWrapper) {
      console.error("Cannot dismiss selected result, last query not present");
      return false;
    }
    let { queryContext } = this._lastQueryContextWrapper;

    let { selectedElement } = this.input.view;
    if (selectedElement?.classList.contains("urlbarView-button")) {
      // For results with buttons, delete them only when the main part of the
      // row is selected, not a button.
      return false;
    }

    let result = this.input.view.selectedResult;
    if (!result) {
      return false;
    }
    if (result.heuristic && !result.autofill) {
      return false;
    }

    this.engagementEvent.record(event, {
      result,
      selType: "dismiss",
      searchString: queryContext.searchString,
      searchSource: this.input.getSearchSource(event),
    });

    return true;
  }

  /**
   * Checks whether a keyboard event that would normally open the view should
   * instead be handled natively by the input field.
   * On certain platforms, the up and down keys can be used to move the caret,
   * in which case we only want to open the view if the caret is at the
   * start or end of the input.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   * @returns {boolean}
   *   Returns true if the event should move the caret instead of opening the
   *   view.
   */
  keyEventMovesCaret(event) {
    if (this.view.isOpen) {
      return false;
    }
    if (
      UrlbarContentUtils.getPlatform() != "macosx" &&
      UrlbarContentUtils.getPlatform() != "linux"
    ) {
      return false;
    }
    let isArrowUp = event.keyCode == KeyEvent.DOM_VK_UP;
    let isArrowDown = event.keyCode == KeyEvent.DOM_VK_DOWN;
    if (!isArrowUp && !isArrowDown) {
      return false;
    }
    let start = this.input.selectionStart;
    let end = this.input.selectionEnd;
    if (
      end != start ||
      (isArrowUp && start > 0) ||
      (isArrowDown && end < this.input.value.length)
    ) {
      return true;
    }
    return false;
  }

  speculativeConnect(result, context, reason) {
    return this.#parentController.speculativeConnect(result, context, reason);
  }

  loadURL(loadData) {
    return this.#parentController.loadURL(loadData);
  }

  /**
   * @param {number} [browserId] The browser the load resolved to, as returned by `loadURL`.
   * @returns {Promise<{focused: boolean}> | {focused: boolean}} Whether the browser was focused.
   */
  focusBrowser(browserId) {
    return this.#parentController.focusBrowser(browserId);
  }

  switchToTab(loadData) {
    return this.#parentController.switchToTab(loadData);
  }

  addToInputHistory(url, input, options) {
    return this.#parentController.addToInputHistory(url, input, options);
  }

  /**
   * Returns whether the passed-in event represents a canonization request.
   *
   * @param {Event} event
   *   An Event to examine.
   * @returns {boolean}
   *   Whether the event is a KeyboardEvent that triggers canonization.
   */
  isCanonizeKeyboardEvent(event) {
    if (this.#input.sapName == "searchbar") {
      return false;
    }
    if (!UrlbarShared.isInstance(event, KeyboardEvent)) {
      return false;
    }
    let keyEvent = /** @type {KeyboardEvent} */ (event);
    return (
      keyEvent.keyCode == KeyEvent.DOM_VK_RETURN &&
      (UrlbarContentUtils.getPlatform() == "macosx"
        ? keyEvent.metaKey
        : keyEvent.ctrlKey) &&
      !(/** @type {any} */ (event)._disableCanonization) &&
      UrlbarPrefs.get("ctrlCanonizesURLs")
    );
  }

  /**
   * Determines where a URL/page picked in `<moz-urlbar>` should be opened. Only
   * the call to `BrowserUtils.whereToOpenLink` (a system module the content-web
   * scope can't import) goes through `UrlbarContentUtils`; everything else,
   * including the guarded empty-tab read, is content-safe and stays here.
   *
   * @param {KeyboardEvent | MouseEvent} event
   *   The event that triggered the opening.
   * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
   */
  whereToOpen(event) {
    let isKeyboardEvent = UrlbarShared.isInstance(event, KeyboardEvent);
    let reuseEmpty = isKeyboardEvent;
    /** @type {"current" | "tabshifted" | "tab" | "save" | "window"} */
    let where;
    if (
      isKeyboardEvent &&
      (event.altKey || event.getModifierState("AltGraph"))
    ) {
      // We support using 'alt' to open in a tab, because ctrl/shift
      // might be used for canonizing URLs:
      where = event.shiftKey ? "tabshifted" : "tab";
    } else if (this.isCanonizeKeyboardEvent(event)) {
      // If we're allowing canonization, and this is a canonization key event,
      // open in current tab to avoid handling as new tab modifier.
      where = "current";
    } else {
      where = UrlbarContentUtils.whereToOpenLink(event);
    }
    let openInTabPref =
      this.#input.sapName == "searchbar"
        ? UrlbarPrefs.get("browser.search.openintab")
        : UrlbarPrefs.get("openintab");
    if (openInTabPref) {
      if (where == "current") {
        where = "tab";
      } else if (where == "tab") {
        where = "current";
      }
      reuseEmpty = true;
    }
    // The browser window exists only in chrome; a content-process input has no
    // tab to reuse, so `gBrowser` is absent and the reuse is skipped.
    if (
      where == "tab" &&
      reuseEmpty &&
      this.window.gBrowser?.selectedTab.isEmpty
    ) {
      where = "current";
    }
    return where;
  }

  focusOnUnifiedSearchButton() {
    this.input.setUnifiedSearchButtonAvailability(true);

    /** @type {HTMLElement} */
    const switcher = this.input.querySelector(".searchmode-switcher");
    // Remove blur listener to avoid closing urlbar view panel.
    this.input.inputField.removeEventListener("blur", this.input);
    // Move the focus.
    switcher.focus();
    // Restore all.
    this.input.inputField.addEventListener("blur", this.input);
    switcher.addEventListener(
      "blur",
      /** @type {(e: FocusEvent) => void} */
      e => {
        let relatedTarget = /** @type {HTMLElement} */ (e.relatedTarget);
        if (
          this.input.hasAttribute("focused") &&
          !this.input.contains(relatedTarget)
        ) {
          // If the focus is not back to urlbar, fire blur event explicitly to
          // clear the urlbar. Because the input field has been losing an
          // opportunity to lose the focus since we removed blur listener once.
          this.input.inputField.dispatchEvent(
            new FocusEvent("blur", {
              relatedTarget: e.relatedTarget,
            })
          );
        }
      },
      { once: true }
    );
  }

  /** @type {typeof UrlbarParentController.prototype.initEngineStore} */
  initEngineStore() {
    return this.#parentController.initEngineStore();
  }

  /** @type {typeof UrlbarParentController.prototype.maybeInitEngineStore} */
  maybeInitEngineStore() {
    if (this.#parentController.maybeInitEngineStore) {
      return this.#parentController.maybeInitEngineStore();
    }
    // Synchronous initialization isn't supported in the message path.
    return false;
  }

  /** @type {typeof UrlbarParentController.prototype.openSERP} */
  openSERP(engineId, searchTerms, where, inBackground, browserId) {
    this.#parentController.openSERP(
      engineId,
      searchTerms,
      where,
      inBackground,
      browserId
    );
  }

  /** @type {typeof UrlbarParentController.prototype.openSearchForm} */
  openSearchForm(engineId, where, inBackground, browserId) {
    this.#parentController.openSearchForm(
      engineId,
      where,
      inBackground,
      browserId
    );
  }

  /** @type {typeof UrlbarParentController.prototype.getEngineIconURL} */
  getEngineIconURL(engineId) {
    return this.#parentController.getEngineIconURL(engineId);
  }

  /** @type {typeof UrlbarParentController.prototype.markEngineAsUsed} */
  markEngineAsUsed(engineId) {
    this.#parentController.markEngineAsUsed(engineId);
  }
}
