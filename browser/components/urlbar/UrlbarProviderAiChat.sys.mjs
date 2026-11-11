/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {AISmartBarParent} from "moz-src:///browser/components/aiwindow/ui/actors/AISmartBarParent.sys.mjs"
 * @import {SmartbarInput} from "chrome://browser/content/urlbar/SmartbarInput.mjs"
 */
import {
  SkippableTimer,
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  AIWINDOW_URL:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  IntentClassifier:
    "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderHeuristicFallback:
    "moz-src:///browser/components/urlbar/UrlbarProviderHeuristicFallback.sys.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
});

// Debounce parameters for intent evaluation.
const MAX_SEARCHSTRING_DIFF_FOR_DEBOUNCE = 3;
const MAX_TIME_FOR_DEBOUNCE_MS = 200;

/**
 * Determine if two strings are unrelated for debounce purposes.
 *
 * @param {string} str1
 * @param {string} str2
 * @returns {boolean} True if the strings are unrelated enough.
 */
function stringsAreUnrelated(str1, str2) {
  // Not a substring of each other.
  if (str1.includes(str2) || str2.includes(str1)) {
    return false;
  }
  // Length difference sufficient to debounce.
  return (
    Math.abs(str1.length - str2.length) > MAX_SEARCHSTRING_DIFF_FOR_DEBOUNCE
  );
}

/**
 * A provider that returns a chat result for the smart window Chat feature.
 */
export class UrlbarProviderAiChat extends UrlbarProvider {
  constructor() {
    super();
  }

  // TODO (GENAI-3376): Verify if this is the expected icon for search results.
  static CHAT_ICON_URL =
    "chrome://browser/content/aiwindow/assets/ask-icon.svg";

  // Don't offer chat results for very short queries.
  static MIN_CHARS_FOR_CHAT = 3;

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    // The behavior depends on the SAP and the user intent, thus we treat this
    // as an immediate heuristic provider and eventually delay later.
    return lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   * @param {UrlbarParentController} [controller] The current controller.
   * @returns {Promise<boolean>} True if the provider should be invoked.
   */
  async isActive(queryContext, controller) {
    return (
      lazy.AIWindow.isAIWindowActiveAndEnabled(controller.browserWindow) &&
      queryContext.trimmedSearchString.length >=
        UrlbarProviderAiChat.MIN_CHARS_FOR_CHAT &&
      !queryContext.restrictInSearchMode()
    );
  }

  /**
   * Starts querying.
   *
   * Note: Extended classes should return a Promise resolved when the provider
   *       is done searching AND returning results.
   *
   * @param {UrlbarQueryContext} queryContext
   *   The query context object
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   * @param {UrlbarParentController} controller The controller instance.
   * @returns {Promise<void>}
   * @abstract
   */
  async startQuery(queryContext, addCallback, controller) {
    let instance = this.queryInstance;
    let canReturnHeuristicResult = queryContext.sapName != "urlbar";

    if (!canReturnHeuristicResult) {
      // Add a delay as chat is never a heuristic result.
      this.logger.info("Add delay before resolving intent");
      await new SkippableTimer({
        name: "ProviderAiChat",
        time: lazy.UrlbarPrefs.get("delay"),
        logger: this.logger,
      }).promise;
      if (instance != this.queryInstance) {
        // The query was canceled while we were waiting.
        return;
      }
    }

    let intent = await this.#determineIntent(queryContext);

    // No chat row when the query was canceled, the input is a URL, or this is
    // the urlbar and the intent is not chat. Pinning search still shows Ask.
    if (
      instance != this.queryInstance ||
      intent == "navigate" ||
      (intent != "chat" &&
        queryContext.sapName == "urlbar" &&
        lazy.IntentClassifier.smartbarIntent != "search")
    ) {
      return;
    }

    let heuristic = canReturnHeuristicResult && intent == "chat";
    let result = new lazy.UrlbarResult({
      heuristic,
      type: lazy.UrlbarShared.RESULT_TYPE.AI_CHAT,
      source: lazy.UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
      suggestedIndex: heuristic ? undefined : 1,
      payload: {
        icon: UrlbarProviderAiChat.CHAT_ICON_URL,
        query: queryContext.searchString,
        title: queryContext.searchString,
      },
    });
    addCallback(this, result);

    // If the result is heuristic, we also want a following non-heuristic
    // Search result.
    if (heuristic) {
      let engine = lazy.UrlbarSearchUtils.getDefaultEngine(
        queryContext.isPrivate
      );
      let icon = await UrlbarUtils.getEngineIconUrl(engine, controller);
      if (instance != this.queryInstance) {
        return;
      }

      let searchResult = new lazy.UrlbarResult({
        type: lazy.UrlbarShared.RESULT_TYPE.SEARCH,
        source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
        // Pin below the heuristic result.
        suggestedIndex: 1,
        payload: {
          engine: engine.name,
          query: queryContext.searchString,
          title: queryContext.searchString,
          icon,
        },
        highlights: {
          engine: lazy.UrlbarShared.HIGHLIGHT.TYPED,
        },
      });
      addCallback(this, searchResult);
    }
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController & {input: SmartbarInput}} controller
   * @param {object} details
   */
  async onEngagement(queryContext, controller, details) {
    let win = controller.input.inputField.documentGlobal;
    /** @type {AISmartBarParent} */
    let actor;
    if (queryContext.sapName == "urlbar") {
      let selectedBrowser = win.gBrowser?.selectedBrowser;
      if (
        selectedBrowser &&
        lazy.AIWindow.isAIWindowNewTabPage(selectedBrowser.currentURI)
      ) {
        actor =
          selectedBrowser.browsingContext?.currentWindowGlobal?.getActor(
            "AISmartBar"
          );
      } else {
        let browser = await this.#getSidebarBrowser(win);
        if (win.closed) {
          return;
        }
        actor =
          browser.browsingContext?.currentWindowGlobal?.getActor("AISmartBar");
      }
    } else {
      // @ts-expect-error bug 1957626
      actor = win.browsingContext?.currentWindowGlobal?.getActor("AISmartBar");
    }
    if (!actor) {
      this.logger.error("AISmartBar actor not found");
      return;
    }

    const isCtaButtonClick = details.event?.type.startsWith(
      "aiwindow-input-cta:"
    );
    actor.ask({
      contextMentions: controller.input.getResolvedContextWebsites?.() ?? [],
      contextPageUrl: controller.input.getContextPageUrl?.()?.href ?? null,
      detectedIntent: this.#lastIntentEvaluation.intent ?? "chat",
      location: controller.input.sapLocation ?? "urlbar",
      submitType: isCtaButtonClick ? "button" : "enter",
      value: queryContext.searchString,
    });
  }

  async #getSidebarBrowser(win) {
    if (!lazy.AIWindowUI.isSidebarOpen(win)) {
      lazy.AIWindowUI.openSidebar(win);
    }
    let browser = win.document.getElementById(lazy.AIWindowUI.BROWSER_ID);
    if (browser.currentURI?.spec !== lazy.AIWINDOW_URL) {
      await new Promise(resolve => {
        browser.addEventListener(
          "load",
          function () {
            if (browser.currentURI.spec === lazy.AIWINDOW_URL) {
              resolve();
            }
          },
          { once: true, capture: true }
        );
      });
    }
    return browser;
  }

  /**
   * Determine the user intent for the given query context.
   *
   * @param {UrlbarQueryContext} queryContext
   * @returns {Promise<string>} "chat", "search", or "navigate"
   */
  async #determineIntent(queryContext) {
    // If the string looks like a URL, don't show the chat result.
    if (lazy.UrlbarProviderHeuristicFallback.matchUnknownUrl(queryContext)) {
      return "navigate";
    }

    // For performance reasons we want to debounce here, so only re-evaluate
    // intent after a meaningful delta from the last evaluation, otherwise keep
    // the last value.
    let intent = this.#lastIntentEvaluation.intent;
    if (
      !intent ||
      Date.now() - this.#lastIntentEvaluation.timestamp >
        MAX_TIME_FOR_DEBOUNCE_MS ||
      stringsAreUnrelated(
        queryContext.searchString,
        this.#lastIntentEvaluation.queryString
      )
    ) {
      try {
        intent = await lazy.IntentClassifier.getPromptIntent(
          queryContext.searchString
        );
      } catch (e) {
        this.logger.error(
          "Error determining intent, defaulting to 'search': " + e
        );
        intent = "search";
      }
      this.#lastIntentEvaluation = {
        timestamp: Date.now(),
        queryString: queryContext.searchString,
        intent,
      };
    }
    return intent;
  }

  // Store the last intent evaluation to debounce.
  #lastIntentEvaluation = {
    timestamp: 0,
    queryString: "",
    /** @type {?string} */
    intent: null,
  };
}
