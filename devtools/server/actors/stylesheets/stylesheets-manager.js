/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const {
  getSourcemapBaseURL,
} = require("resource://devtools/server/actors/utils/source-map-utils.js");

loader.lazyRequireGetter(
  this,
  ["addPseudoClassLock", "removePseudoClassLock"],
  "resource://devtools/server/actors/highlighters/utils/markup.js",
  true
);
loader.lazyRequireGetter(
  this,
  "loadSheet",
  "resource://devtools/shared/layout/utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["getStyleSheetOwnerNode", "getStyleSheetText"],
  "resource://devtools/server/actors/stylesheets/stylesheet-utils.js",
  true
);

const TRANSITION_PSEUDO_CLASS = ":-moz-styleeditor-transitioning";
const TRANSITION_DURATION_MS = 500;
const TRANSITION_BUFFER_MS = 1000;
const TRANSITION_RULE_SELECTOR = `:root${TRANSITION_PSEUDO_CLASS}, :root${TRANSITION_PSEUDO_CLASS} *:not(:-moz-native-anonymous)`;
const TRANSITION_SHEET =
  "data:text/css;charset=utf-8," +
  encodeURIComponent(`
  ${TRANSITION_RULE_SELECTOR} {
    transition-duration: ${TRANSITION_DURATION_MS}ms !important;
    transition-delay: 0ms !important;
    transition-timing-function: ease-out !important;
    transition-property: all !important;
  }
`);

// The possible kinds of style-applied events.
// UPDATE_PRESERVING_RULES means that the update is guaranteed to
// preserve the number and order of rules on the style sheet.
// UPDATE_GENERAL covers any other kind of change to the style sheet.
const UPDATE_PRESERVING_RULES = 0;
const UPDATE_GENERAL = 1;

// If the user edits a stylesheet, we stash a copy of the edited text
// here, keyed by the stylesheet.  This way, if the tools are closed
// and then reopened, the edited text will be available. A weak map
// is used so that navigation by the user will eventually cause the
// edited text to be collected.
const modifiedStyleSheets = new WeakMap();

/**
 * Manage stylesheets related to a given Target Actor.
 *
 * @fires stylesheet-updated: emitted when there was changes in a stylesheet
 *        First arg is an object with the following properties:
 *        - resourceId {String}: The id that was assigned to the stylesheet
 *        - updateKind {String}: Which kind of update it is ("style-applied",
 *          "at-rules-changed", "matches-change", "property-change")
 *        - updates {Object}: The update data
 */
class StyleSheetsManager extends EventEmitter {
  #abortController;
  // Map<resourceId, Promise<string>>
  #initialExternalStyleSheetTextMap = new Map();
  // Map<resourceId, AbortController>
  #mqlChangeAbortControllerMap = new Map();
  #styleSheetCount = 0;
  #styleSheetMap = new Map();
  #styleSheetCreationData;
  #targetActor;
  #transitionSheetLoaded;
  #transitionTimeout;
  #watchListeners = {
    onAvailable: [],
    onUpdated: [],
    onDestroyed: [],
  };

  /**
   * @param TargetActor targetActor
   *        The target actor from which we should observe stylesheet changes.
   */
  constructor(targetActor) {
    super();

    this.#targetActor = targetActor;
  }

  #setEventListenersIfNeeded() {
    if (this.#abortController) {
      return;
    }

    this.#abortController = new AbortController();
    const { signal } = this.#abortController;

    // Listen for new stylesheet being added via StyleSheetApplicableStateChanged
    if (this.#targetActor.chromeEventHandler) {
      this.#targetActor.chromeEventHandler.addEventListener(
        "StyleSheetApplicableStateChanged",
        this.#onApplicableStateChanged,
        { capture: true, signal }
      );
      this.#targetActor.chromeEventHandler.addEventListener(
        "StyleSheetRemoved",
        this.#onStylesheetRemoved,
        { capture: true, signal }
      );
    }

    this.#watchStyleSheetChangeEvents();
    this.#targetActor.on("window-ready", this.#onTargetActorWindowReady, {
      signal,
    });
  }

  /**
   * Calling this function will make the StyleSheetsManager start the event listeners needed
   * to watch for stylesheet additions and modifications.
   * This resolves once it notified about existing stylesheets.
   *
   * @param {object} options
   * @param {Function} onAvailable: Function that will be called when a stylesheet is
   *                   registered, but also with already registered stylesheets
   *                   if ignoreExisting is not set to true.
   *                   This is called with a single object parameter with the following properties:
   *                   - {String} resourceId: The id that was assigned to the stylesheet
   *                   - {StyleSheet} styleSheet: The actual stylesheet object
   *                   - {Object} creationData: An object with:
   *                              - {boolean} isCreatedByDevTools: Was the stylesheet created
   *                                by DevTools (e.g. by the user clicking the new stylesheet
   *                                button in the styleeditor)
   *                              - {String} fileName
   * @param {Function} onUpdated: Function that will be called when a stylesheet is updated
   *                   This is called with a single object parameter with the following properties:
   *                   - {String} resourceId: The id that was assigned to the stylesheet
   *                   - {String} updateKind: Which kind of update it is ("style-applied",
   *                     "at-rules-changed", "matches-change", "property-change")
   *                   - {Object} updates : The update data
   * @param {Function} onDestroyed: Function that will be called when a stylesheet is removed
   *                   This is called with a single object parameter with the following properties:
   *                   - {String} resourceId: The id that was assigned to the stylesheet
   * @param {boolean} ignoreExisting: Pass to true to avoid onAvailable to be called with
   *                  already registered stylesheets.
   */
  async watch({ onAvailable, onUpdated, onDestroyed, ignoreExisting = false }) {
    if (!onAvailable && !onUpdated && !onDestroyed) {
      throw new Error("Expect onAvailable, onUpdated or onDestroyed");
    }

    if (onAvailable) {
      if (typeof onAvailable !== "function") {
        throw new Error("onAvailable should be a function");
      }

      // Don't register the listener yet if we're ignoring existing stylesheets, we'll do
      // that at the end of the function, after we processed existing stylesheets.
    }

    if (onUpdated) {
      if (typeof onUpdated !== "function") {
        throw new Error("onUpdated should be a function");
      }
      this.#watchListeners.onUpdated.push(onUpdated);
    }

    if (onDestroyed) {
      if (typeof onDestroyed !== "function") {
        throw new Error("onDestroyed should be a function");
      }
      this.#watchListeners.onDestroyed.push(onDestroyed);
    }

    // Process existing stylesheets
    const promises = [];
    for (const window of this.#targetActor.windows) {
      promises.push(this.#getStyleSheetsForWindow(window));
    }

    this.#setEventListenersIfNeeded();

    // Finally, notify about existing stylesheets
    const styleSheets = await Promise.all(promises);
    const styleSheetsData = styleSheets.flat().map(styleSheet => ({
      styleSheet,
      resourceId: this.#registerStyleSheet(styleSheet),
    }));

    let registeredStyleSheetsPromises;
    if (onAvailable && ignoreExisting !== true) {
      registeredStyleSheetsPromises = styleSheetsData.map(
        ({ resourceId, styleSheet }) => onAvailable({ resourceId, styleSheet })
      );
    }

    // Only register the listener after we went over the list of existing stylesheets
    // so the listener is not triggered by possible calls to #registerStyleSheet earlier.
    if (onAvailable) {
      this.#watchListeners.onAvailable.push(onAvailable);
    }

    if (registeredStyleSheetsPromises) {
      await Promise.all(registeredStyleSheetsPromises);
    }
  }

  /**
   * Remove the passed listeners
   *
   * @param {object} options: See this.watch
   */
  unwatch({ onAvailable, onUpdated, onDestroyed }) {
    if (!this.#watchListeners) {
      return;
    }

    if (onAvailable) {
      const index = this.#watchListeners.onAvailable.indexOf(onAvailable);
      if (index !== -1) {
        this.#watchListeners.onAvailable.splice(index, 1);
      }
    }

    if (onUpdated) {
      const index = this.#watchListeners.onUpdated.indexOf(onUpdated);
      if (index !== -1) {
        this.#watchListeners.onUpdated.splice(index, 1);
      }
    }

    if (onDestroyed) {
      const index = this.#watchListeners.onDestroyed.indexOf(onDestroyed);
      if (index !== -1) {
        this.#watchListeners.onDestroyed.splice(index, 1);
      }
    }
  }

  #watchStyleSheetChangeEvents() {
    for (const window of this.#targetActor.windows) {
      this.#watchStyleSheetChangeEventsForWindow(window);
    }
  }

  #onTargetActorWindowReady = ({ window }) => {
    this.#watchStyleSheetChangeEventsForWindow(window);
  };

  #watchStyleSheetChangeEventsForWindow(window) {
    // We have to set this flag in order to get the
    // StyleSheetApplicableStateChanged and StyleSheetRemoved events. See Document.webidl.
    window.document.styleSheetChangeEventsEnabled = true;
  }

  #unwatchStyleSheetChangeEvents() {
    for (const window of this.#targetActor.windows) {
      window.document.styleSheetChangeEventsEnabled = false;
    }
  }

  /**
   * Create a new style sheet in the document with the given text.
   *
   * @param  {Document} document
   *         Document that the new style sheet belong to.
   * @param  {Element} parent
   *         The element into which we'll append the <style> element
   * @param  {string} text
   *         Content of style sheet.
   * @param  {string} fileName
   *         If the stylesheet adding is from file, `fileName` indicates the path.
   */
  async addStyleSheet(document, parent, text, fileName) {
    const style = document.createElementNS(
      "http://www.w3.org/1999/xhtml",
      "style"
    );
    style.setAttribute("type", "text/css");
    style.setDevtoolsAsTriggeringPrincipal();

    if (text) {
      style.appendChild(document.createTextNode(text));
    }

    // This triggers StyleSheetApplicableStateChanged event.
    parent.appendChild(style);

    // This promise will be resolved when the resource for this stylesheet is available.
    let resolve = null;
    const promise = new Promise(r => {
      resolve = r;
    });

    if (!this.#styleSheetCreationData) {
      this.#styleSheetCreationData = new WeakMap();
    }
    this.#styleSheetCreationData.set(style.sheet, {
      isCreatedByDevTools: true,
      fileName,
      resolve,
    });

    await promise;

    return style.sheet;
  }

  /**
   * Return resourceId of the given style sheet or create one if the stylesheet wasn't
   * registered yet.
   *
   * @param {StyleSheet} styleSheet
   * @returns {string} resourceId
   */
  getStyleSheetResourceId(styleSheet) {
    const existingResourceId = this.#findStyleSheetResourceId(styleSheet);
    if (existingResourceId) {
      return existingResourceId;
    }

    // If we couldn't find an associated resourceId, that means the stylesheet isn't
    // registered yet. Calling #registerStyleSheet will register it and return the
    // associated resourceId it computed for it.
    return this.#registerStyleSheet(styleSheet);
  }

  /**
   * Return the associated resourceId of the given registered style sheet, or null if the
   * stylesheet wasn't registered yet.
   *
   * @param {StyleSheet} styleSheet
   * @returns {string} resourceId
   */
  #findStyleSheetResourceId(styleSheet) {
    for (const [
      resourceId,
      existingStyleSheet,
    ] of this.#styleSheetMap.entries()) {
      if (styleSheet === existingStyleSheet) {
        return resourceId;
      }
    }

    return null;
  }

  /**
   * Return owner node of the style sheet of the given resource id.
   *
   * @param {string} resourceId
   *                  The id associated with the stylesheet
   * @returns {Element|null}
   */
  getOwnerNode(resourceId) {
    const styleSheet = this.#styleSheetMap.get(resourceId);
    return styleSheet.ownerNode;
  }

  /**
   * Return the index of given stylesheet of the given resource id.
   *
   * @param {string} resourceId
   *                  The id associated with the stylesheet
   * @returns {number}
   */
  getStyleSheetIndex(resourceId) {
    const styleSheet = this.#styleSheetMap.get(resourceId);

    const styleSheets = InspectorUtils.getAllStyleSheets(
      this.#targetActor.window.document,
      true
    );
    let i = 0;
    for (const sheet of styleSheets) {
      if (!this.#shouldListSheet(sheet)) {
        continue;
      }
      if (sheet == styleSheet) {
        return i;
      }
      i++;
    }
    return -1;
  }

  /**
   * Get the text of a stylesheet given its resourceId.
   *
   * @param {string} resourceId
   *                  The id associated with the stylesheet
   * @returns {string}
   */
  async getText(resourceId) {
    if (!this.#styleSheetMap.has(resourceId)) {
      return "";
    }

    const styleSheet = this.#styleSheetMap.get(resourceId);

    const modifiedText = modifiedStyleSheets.get(styleSheet);

    // modifiedText is the content of the stylesheet updated by update function.
    // In case not updating, this is undefined.
    if (modifiedText !== undefined) {
      return modifiedText;
    }

    if (
      styleSheet.href &&
      this.#initialExternalStyleSheetTextMap.has(resourceId)
    ) {
      return this.#initialExternalStyleSheetTextMap.get(resourceId);
    }

    const getTextPromise = getStyleSheetText(styleSheet);
    // If this is an external stylesheet, cache the promise that gets its text so we
    // avoid having (potentially a lot of) concurrent network requests that would cause
    // a spike in memory usage (see Bug 2034445)
    if (
      styleSheet.href &&
      !this.#initialExternalStyleSheetTextMap.has(resourceId)
    ) {
      this.#initialExternalStyleSheetTextMap.set(resourceId, getTextPromise);
      // Remove the promise from the Map if it rejects
      getTextPromise.catch(() => {
        this.#initialExternalStyleSheetTextMap.delete(resourceId);
      });
    }

    return getTextPromise;
  }

  /**
   * Toggle the disabled property of the stylesheet
   *
   * @param {string} resourceId
   *                  The id associated with the stylesheet
   * @return {boolean} the disabled state after toggling.
   */
  toggleDisabled(resourceId) {
    const styleSheet = this.#styleSheetMap.get(resourceId);
    styleSheet.disabled = !styleSheet.disabled;

    this.#notifyPropertyChanged(resourceId, "disabled", styleSheet.disabled);

    return styleSheet.disabled;
  }

  /**
   * Update the style sheet in place with new text.
   *
   * @param  {string} resourceId
   * @param  {string} text
   *         New text.
   * @param  {object} options
   * @param  {boolean} options.transition
   *         Whether to do CSS transition for change. Defaults to false.
   * @param  {number} options.kind
   *         Either UPDATE_PRESERVING_RULES or UPDATE_GENERAL. Defaults to UPDATE_GENERAL.
   * @param {string} options.cause
   *         Indicates the cause of this update (e.g. "styleeditor") if this was called
   *         from the stylesheet to be edited by the user from the StyleEditor.
   */
  async setStyleSheetText(
    resourceId,
    text,
    { transition = false, kind = UPDATE_GENERAL, cause = "" } = {}
  ) {
    const styleSheet = this.#styleSheetMap.get(resourceId);
    InspectorUtils.parseStyleSheet(styleSheet, text);
    modifiedStyleSheets.set(styleSheet, text);

    // getStyleSheetRuleCountAndAtRules can be costly, so only call it when needed,
    // i.e. when the whole stylesheet is modified, not when a rule body is.
    let atRules, ruleCount;
    if (kind !== UPDATE_PRESERVING_RULES) {
      ({ atRules, ruleCount } =
        this.getStyleSheetRuleCountAndAtRules(styleSheet));
      this.#notifyPropertyChanged(resourceId, "ruleCount", ruleCount, cause);
    }

    if (transition) {
      this.#startTransition(resourceId, kind, cause);
    } else {
      this.#onStyleSheetUpdated({
        resourceId,
        updateKind: "style-applied",
        updates: {
          event: { kind, cause },
        },
      });
    }

    if (kind !== UPDATE_PRESERVING_RULES) {
      this.#onStyleSheetUpdated({
        resourceId,
        updateKind: "at-rules-changed",
        updates: {
          resourceUpdates: { atRules },
        },
      });
    }
  }

  /**
   * Applies a transition to the stylesheet document so any change made by the user in the
   * client will be animated so it's more visible.
   *
   * @param {string} resourceId
   *        The id associated with the stylesheet
   * @param {number} kind
   *        Either UPDATE_PRESERVING_RULES or UPDATE_GENERAL
   * @param {string} cause
   *         Indicates the cause of this update (e.g. "styleeditor") if this was called
   *         from the stylesheet to be edited by the user from the StyleEditor.
   */
  #startTransition(resourceId, kind, cause) {
    const styleSheet = this.#styleSheetMap.get(resourceId);
    const document = styleSheet.associatedDocument;
    const window = document.documentGlobal;

    if (!this.#transitionSheetLoaded) {
      this.#transitionSheetLoaded = true;
      // We don't remove this sheet. It uses an internal selector that
      // we only apply via locks, so there's no need to load and unload
      // it all the time.
      loadSheet(window, TRANSITION_SHEET);
    }

    addPseudoClassLock(document.documentElement, TRANSITION_PSEUDO_CLASS);

    // Set up clean up and commit after transition duration (+buffer)
    // @see #onTransitionEnd
    window.clearTimeout(this.#transitionTimeout);
    this.#transitionTimeout = window.setTimeout(
      this.#onTransitionEnd.bind(this, resourceId, kind, cause),
      TRANSITION_DURATION_MS + TRANSITION_BUFFER_MS
    );
  }

  /**
   * @param {string} resourceId
   *        The id associated with the stylesheet
   * @param {number} kind
   *        Either UPDATE_PRESERVING_RULES or UPDATE_GENERAL
   * @param {string} cause
   *         Indicates the cause of this update (e.g. "styleeditor") if this was called
   *         from the stylesheet to be edited by the user from the StyleEditor.
   */
  #onTransitionEnd(resourceId, kind, cause) {
    const styleSheet = this.#styleSheetMap.get(resourceId);
    const document = styleSheet.associatedDocument;

    this.#transitionTimeout = null;
    removePseudoClassLock(document.documentElement, TRANSITION_PSEUDO_CLASS);

    this.#onStyleSheetUpdated({
      resourceId,
      updateKind: "style-applied",
      updates: {
        event: { kind, cause },
      },
    });
  }

  /**
   * Retrieve the CSSRuleList of a given stylesheet
   *
   * @param {StyleSheet} styleSheet
   * @returns {CSSRuleList}
   */
  #getCSSRules(styleSheet) {
    try {
      return styleSheet.cssRules;
    } catch (e) {
      // sheet isn't loaded yet
    }

    if (!styleSheet.ownerNode) {
      return Promise.resolve([]);
    }

    return new Promise(resolve => {
      styleSheet.ownerNode.addEventListener(
        "load",
        () => resolve(styleSheet.cssRules),
        { once: true }
      );
    });
  }

  /**
   * Get the stylesheets imported by a given stylesheet (via @import)
   *
   * @param {Document} document
   * @param {StyleSheet} styleSheet
   * @returns Array<StyleSheet>
   */
  async #getImportedStyleSheets(document, styleSheet) {
    const importedStyleSheets = [];

    for (const rule of await this.#getCSSRules(styleSheet)) {
      const ruleClassName = ChromeUtils.getClassName(rule);
      if (ruleClassName == "CSSImportRule") {
        // With the Gecko style system, the associated styleSheet may be null
        // if it has already been seen because an import cycle for the same
        // URL.  With Stylo, the styleSheet will exist (which is correct per
        // the latest CSSOM spec), so we also need to check ancestors for the
        // same URL to avoid cycles.
        if (
          !rule.styleSheet ||
          this.#haveAncestorWithSameURL(rule.styleSheet) ||
          !this.#shouldListSheet(rule.styleSheet)
        ) {
          continue;
        }

        importedStyleSheets.push(rule.styleSheet);

        // recurse imports in this stylesheet as well
        const children = await this.#getImportedStyleSheets(
          document,
          rule.styleSheet
        );
        importedStyleSheets.push(...children);
      } else if (ruleClassName != "CSSCharsetRule") {
        // @import rules must precede all others except @charset
        break;
      }
    }

    return importedStyleSheets;
  }

  /**
   * Retrieve the total number of rules (including nested ones) and
   * all the at-rules of a given stylesheet.
   *
   * @param {StyleSheet} styleSheet
   * @returns {object} An object of the following shape:
   *          - {Integer} ruleCount: The total number of rules in the stylesheet
   *          - {Array<Object>} atRules: An array of object of the following shape:
   *            - type {String}
   *            - conditionText {String}
   *            - matches {Boolean}: true if the media rule matches the current state of the document
   *            - layerName {String}
   *            - line {Number}
   *            - column {Number}
   */
  getStyleSheetRuleCountAndAtRules(styleSheet) {
    const resourceId = this.#findStyleSheetResourceId(styleSheet);
    if (!resourceId) {
      return [];
    }

    if (this.#mqlChangeAbortControllerMap.has(resourceId)) {
      this.#mqlChangeAbortControllerMap.get(resourceId).abort();
      this.#mqlChangeAbortControllerMap.delete(resourceId);
    }

    // Accessing the stylesheet associated window might be slow due to cross compartment
    // wrappers, so only retrieve it if it's needed.
    let win;
    const getStyleSheetAssociatedWindow = () => {
      if (!win) {
        win = styleSheet.associatedDocument?.documentGlobal;
      }
      return win;
    };

    // This returns the following type of at-rules:
    // - CSSMediaRule
    // - CSSContainerRule
    // - CSSSupportsRule
    // - CSSLayerBlockRule
    // New types can be added from InpsectorUtils.cpp `CollectAtRules`
    const { atRules: styleSheetRules, ruleCount } =
      InspectorUtils.getStyleSheetRuleCountAndAtRules(styleSheet);
    const atRules = [];
    for (const rule of styleSheetRules) {
      const className = ChromeUtils.getClassName(rule);
      if (className === "CSSMediaRule") {
        let matches = false;

        try {
          const associatedWin = getStyleSheetAssociatedWindow();
          const mql = associatedWin.matchMedia(rule.media.mediaText);
          matches = mql.matches;

          let ac = this.#mqlChangeAbortControllerMap.get(resourceId);
          if (!ac) {
            ac = new associatedWin.AbortController();
            this.#mqlChangeAbortControllerMap.set(resourceId, ac);
          }

          const index = atRules.length;
          mql.addEventListener(
            "change",
            () => this.#onMatchesChange(resourceId, index, mql),
            {
              signal: ac.signal,
            }
          );
        } catch (e) {
          // Ignored
        }

        atRules.push({
          type: "media",
          conditionText: rule.conditionText,
          matches,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSContainerRule") {
        atRules.push({
          type: "container",
          conditionText: rule.conditionText,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSSupportsRule") {
        atRules.push({
          type: "support",
          conditionText: rule.conditionText,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSLayerBlockRule") {
        atRules.push({
          type: "layer",
          layerName: rule.name,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSPropertyRule") {
        atRules.push({
          type: "property",
          propertyName: rule.name,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSPositionTryRule") {
        atRules.push({
          type: "position-try",
          positionTryName: rule.name,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      } else if (className === "CSSCustomMediaRule") {
        const customMediaQuery = [];
        if (typeof rule.query === "boolean") {
          customMediaQuery.push({
            text: rule.query.toString(),
            matches: rule.query === true,
          });
        } else {
          // if query is not a boolean, it's a MediaList
          for (let i = 0, len = rule.query.length; i < len; i++) {
            customMediaQuery.push({
              text: rule.query[i],
              // For now always consider the media query as matching.
              // This should be changed as part of Bug 2006379
              matches: true,
            });
          }
        }
        atRules.push({
          type: "custom-media",
          customMediaName: rule.name,
          customMediaQuery,
          line: InspectorUtils.getRelativeRuleLine(rule),
          column: InspectorUtils.getRuleColumn(rule),
        });
      }
    }
    return {
      ruleCount,
      atRules,
    };
  }

  /**
   * Called when the status of a media query support changes (i.e. it now matches, or it
   * was matching but isn't anymore)
   *
   * @param {string} resourceId
   *        The id associated with the stylesheet
   * @param {number} index
   *        The index of the media rule relatively to all the other at-rules of the stylesheet
   * @param {MediaQueryList} mql
   *        The result of matchMedia for the given media rule
   */
  #onMatchesChange(resourceId, index, mql) {
    this.#onStyleSheetUpdated({
      resourceId,
      updateKind: "matches-change",
      updates: {
        nestedResourceUpdates: [
          {
            path: ["atRules", index, "matches"],
            value: mql.matches,
          },
        ],
      },
    });
  }

  /**
   * Get the node href of a given stylesheet
   *
   * @param {StyleSheet} styleSheet
   * @returns {string}
   */
  getNodeHref(styleSheet) {
    const { associatedDocument, constructed, ownerNode } = styleSheet;

    if (constructed && associatedDocument) {
      return styleSheet.associatedDocument.location.href;
    }

    if (!ownerNode) {
      return null;
    }

    if (ownerNode.nodeType == ownerNode.DOCUMENT_NODE) {
      return ownerNode.location.href;
    }

    if (ownerNode.ownerDocument?.location) {
      return ownerNode.ownerDocument.location.href;
    }

    return null;
  }

  /**
   * Get the sourcemap base url of a given stylesheet
   *
   * @param {StyleSheet} styleSheet
   * @returns {string}
   */
  getSourcemapBaseURL(styleSheet) {
    // When the style is injected via nsIDOMWindowUtils.loadSheet, even
    // the parent style sheet has no owner, so default back to target actor
    // document
    const ownerNode = getStyleSheetOwnerNode(styleSheet);
    const ownerDocument = ownerNode
      ? ownerNode.ownerDocument
      : this.#targetActor.window;

    return getSourcemapBaseURL(
      // Technically resolveSourceURL should be used here alongside
      // "this.rawSheet.sourceURL", but the style inspector does not support
      // /*# sourceURL=*/ in CSS, so we're omitting it here (bug 880831).
      styleSheet.href || this.getNodeHref(styleSheet),
      ownerDocument
    );
  }

  /**
   * Get all the stylesheets for a given window
   *
   * @param {Window} window
   * @returns {Array<StyleSheet>}
   */
  async #getStyleSheetsForWindow(window) {
    const { document } = window;
    const documentOnly = !document.nodePrincipal.isSystemPrincipal;

    const styleSheets = [];

    for (const styleSheet of InspectorUtils.getAllStyleSheets(
      document,
      documentOnly
    )) {
      if (!this.#shouldListSheet(styleSheet)) {
        continue;
      }

      styleSheets.push(styleSheet);

      // Get all sheets, including imported ones
      const importedStyleSheets = await this.#getImportedStyleSheets(
        document,
        styleSheet
      );
      styleSheets.push(...importedStyleSheets);
    }

    return styleSheets;
  }

  /**
   * Returns true if a given stylesheet has an ancestor with the same url it has
   *
   * @param {StyleSheet} styleSheet
   * @returns {boolean}
   */
  #haveAncestorWithSameURL(styleSheet) {
    const href = styleSheet.href;
    while (styleSheet.parentStyleSheet) {
      if (styleSheet.parentStyleSheet.href == href) {
        return true;
      }
      styleSheet = styleSheet.parentStyleSheet;
    }
    return false;
  }

  /**
   * Helper function called when a property changed in a given stylesheet
   *
   * @param {string} resourceId
   *        The id of the stylesheet the change occured in
   * @param {string} property
   *        The property that was changed
   * @param {string} value
   *        The value of the property
   * @param {cause} value
   *        The panel which initiated the change notification. This is optional.
   */
  #notifyPropertyChanged(resourceId, property, value, cause = "") {
    this.#onStyleSheetUpdated({
      resourceId,
      updateKind: "property-change",
      updates: { resourceUpdates: { [property]: value, event: { cause } } },
    });
  }

  /**
   * Event handler that is called when the state of applicable of style sheet is changed.
   *
   * For now, StyleSheetApplicableStateChanged event will be called at following timings.
   * - Append <link> of stylesheet to document
   * - Append <style> to document
   * - Change disable attribute of stylesheet object
   * - Change disable attribute of <link> to false
   * - Stylesheet is constructed.
   * When appending <link>, <style> or changing `disabled` attribute to false,
   * `applicable` is passed as true. The other hand, when changing `disabled`
   * to true, this will be false.
   *
   * NOTE: StyleSheetApplicableStateChanged is _not_ called when removing the <link>/<style>,
   *       but a StyleSheetRemovedEvent is emitted in such case (see #onStyleSheetRemoved)
   *
   * @param {StyleSheetApplicableStateChangedEvent}
   *        The triggering event.
   */
  #onApplicableStateChanged = ({ applicable, stylesheet: styleSheet }) => {
    if (
      // Have interest in applicable stylesheet only.
      applicable &&
      styleSheet.associatedDocument &&
      (!this.#targetActor.ignoreSubFrames ||
        styleSheet.associatedDocument.documentGlobal ===
          this.#targetActor.window) &&
      this.#shouldListSheet(styleSheet) &&
      !this.#haveAncestorWithSameURL(styleSheet)
    ) {
      this.#registerStyleSheet(styleSheet);
    }
  };

  /**
   * Event handler that is called when a style sheet is removed.
   *
   * @param {StyleSheetRemovedEvent}
   *        The triggering event.
   */
  #onStylesheetRemoved = event => {
    this.#unregisterStyleSheet(event.stylesheet);
  };

  /**
   * If the stylesheet isn't registered yet, this function will generate an associated
   * resourceId and call registered `onAvailable` listeners.
   *
   * @param {StyleSheet} styleSheet
   * @returns {string} the associated resourceId
   */
  #registerStyleSheet(styleSheet) {
    const existingResourceId = this.#findStyleSheetResourceId(styleSheet);
    // If the stylesheet is already registered, there's no need to notify about it again.
    if (existingResourceId) {
      return existingResourceId;
    }

    // It's important to prefix the resourceId with the target actorID so we can't have
    // duplicated resource ids when the client connects to multiple targets.
    const resourceId = `${this.#targetActor.actorID}:stylesheet:${this
      .#styleSheetCount++}`;
    this.#styleSheetMap.set(resourceId, styleSheet);

    const creationData = this.#styleSheetCreationData?.get(styleSheet);
    this.#styleSheetCreationData?.delete(styleSheet);

    const onAvailablePromises = [];
    for (const onAvailable of this.#watchListeners.onAvailable) {
      onAvailablePromises.push(
        onAvailable({
          resourceId,
          styleSheet,
          creationData,
        })
      );
    }

    // creationData exists if this stylesheet was created via `addStyleSheet`.
    if (creationData) {
      //  We resolve the promise once the watcher sent the resources to the client,
      // so `addStyleSheet` calls can be fullfilled.
      Promise.all(onAvailablePromises).then(() => creationData?.resolve());
    }
    return resourceId;
  }

  /**
   * If the stylesheet is registered, this function will call registered `onDestroyed`
   * listeners with the stylesheet resourceId.
   *
   * @param {StyleSheet} styleSheet
   */
  #unregisterStyleSheet(styleSheet) {
    const existingResourceId = this.#findStyleSheetResourceId(styleSheet);
    if (!existingResourceId) {
      return;
    }

    this.#styleSheetMap.delete(existingResourceId);
    this.#styleSheetCreationData?.delete(styleSheet);
    if (this.#mqlChangeAbortControllerMap.has(existingResourceId)) {
      this.#mqlChangeAbortControllerMap.get(existingResourceId).abort();
      this.#mqlChangeAbortControllerMap.delete(existingResourceId);
    }
    if (this.#initialExternalStyleSheetTextMap.has(existingResourceId)) {
      this.#initialExternalStyleSheetTextMap.delete(existingResourceId);
    }

    for (const onDestroyed of this.#watchListeners.onDestroyed) {
      onDestroyed({
        resourceId: existingResourceId,
      });
    }
  }

  #onStyleSheetUpdated(data) {
    this.emit("stylesheet-updated", data);

    for (const onUpdated of this.#watchListeners.onUpdated) {
      onUpdated(data);
    }
  }

  /**
   * Returns true if the passed styleSheet should be handled.
   *
   * @param {StyleSheet} styleSheet
   * @returns {boolean}
   */
  #shouldListSheet(styleSheet) {
    const href = styleSheet.href?.toLowerCase();
    // FIXME(bug 1826538): Make accessiblecaret.css and similar UA-widget
    // sheets system sheets, then remove this special-case.
    if (
      href === "resource://gre-resources/accessiblecaret.css" ||
      href === "resource://gre-resources/details.css" ||
      (href === "resource://devtools-highlighter-styles/highlighters.css" &&
        this.#targetActor.sessionContext.type !== "all")
    ) {
      return false;
    }
    return true;
  }

  /**
   * The StyleSheetsManager instance is managed by the target, so this will be called when
   * the target gets destroyed.
   */
  destroy() {
    // Cleanup
    if (this.#abortController) {
      this.#abortController.abort();
    }
    if (this.#mqlChangeAbortControllerMap) {
      for (const ac of this.#mqlChangeAbortControllerMap.values()) {
        ac.abort();
      }
    }

    try {
      this.#unwatchStyleSheetChangeEvents();
    } catch (e) {
      console.error(
        "Error when destroying StyleSheet manager for",
        this.#targetActor,
        ": ",
        e
      );
    }

    this.#styleSheetMap.clear();
    this.#abortController = null;
    this.#mqlChangeAbortControllerMap = null;
    this.#styleSheetCreationData = null;
    this.#styleSheetMap = null;
    this.#targetActor = null;
    this.#watchListeners = null;
  }
}

module.exports = {
  StyleSheetsManager,
  UPDATE_GENERAL,
  UPDATE_PRESERVING_RULES,
};
