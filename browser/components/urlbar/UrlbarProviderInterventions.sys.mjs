/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppUpdater: "resource://gre/modules/AppUpdater.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  NLP: "resource://gre/modules/NLP.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ResetProfile: "resource://gre/modules/ResetProfile.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  UrlbarProviderGlobalActions:
    "moz-src:///browser/components/urlbar/UrlbarProviderGlobalActions.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "appUpdater", () => new lazy.AppUpdater());

const EN_LOCALE_MATCH = /^en(-.*)$/;

// The search "documents" corresponding to each tip type.
const DOCUMENTS = {
  clear: [
    "cache firefox",
    "clear cache firefox",
    "clear cache in firefox",
    "clear cookies firefox",
    "clear firefox cache",
    "clear history firefox",
    "cookies firefox",
    "delete cookies firefox",
    "delete history firefox",
    "firefox cache",
    "firefox clear cache",
    "firefox clear cookies",
    "firefox clear history",
    "firefox cookie",
    "firefox cookies",
    "firefox delete cookies",
    "firefox delete history",
    "firefox history",
    "firefox not loading pages",
    "history firefox",
    "how to clear cache",
    "how to clear history",
  ],
  refresh: [
    "firefox crashing",
    "firefox keeps crashing",
    "firefox not responding",
    "firefox not working",
    "firefox refresh",
    "firefox slow",
    "how to reset firefox",
    "refresh firefox",
    "reset firefox",
  ],
  update: [
    "download firefox",
    "download mozilla",
    "firefox browser",
    "firefox download",
    "firefox for mac",
    "firefox for windows",
    "firefox free download",
    "firefox install",
    "firefox installer",
    "firefox latest version",
    "firefox mac",
    "firefox quantum",
    "firefox update",
    "firefox version",
    "firefox windows",
    "get firefox",
    "how to update firefox",
    "install firefox",
    "mozilla download",
    "mozilla firefox 2019",
    "mozilla firefox 2020",
    "mozilla firefox download",
    "mozilla firefox for mac",
    "mozilla firefox for windows",
    "mozilla firefox free download",
    "mozilla firefox mac",
    "mozilla firefox update",
    "mozilla firefox windows",
    "mozilla update",
    "update firefox",
    "update mozilla",
    "www.firefox.com",
  ],
};

// In order to determine whether we should show an update tip, we check for app
// updates, but only once per this time period.
const UPDATE_CHECK_PERIOD_MS = 12 * 60 * 60 * 1000; // 12 hours

/**
 * A node in the QueryScorer's phrase tree.
 */
// eslint-disable-next-line no-shadow
class Node {
  constructor(word) {
    this.word = word;
    this.documents = new Set();
    this.childrenByWord = new Map();
  }
}

/**
 * This class scores a query string against sets of phrases.  To refer to a
 * single set of phrases, we borrow the term "document" from search engine
 * terminology.  To use this class, first add your documents with `addDocument`,
 * and then call `score` with a query string.  `score` returns a sorted array of
 * document-score pairs.
 *
 * The scoring method is fairly simple and is based on Levenshtein edit
 * distance.  Therefore, lower scores indicate a better match than higher
 * scores.  In summary, a query matches a phrase if the query starts with the
 * phrase.  So a query "firefox update foo bar" matches the phrase "firefox
 * update" for example.  A query matches a document if it matches any phrase in
 * the document.  The query and phrases are compared word for word, and we allow
 * fuzzy matching by computing the Levenshtein edit distance in each comparison.
 * The amount of fuzziness allowed is controlled with `distanceThreshold`.  If
 * the distance in a comparison is greater than this threshold, then the phrase
 * does not match the query.  The final score for a document is the minimum edit
 * distance between its phrases and the query.
 *
 * As mentioned, `score` returns a sorted array of document-score pairs.  It's
 * up to you to filter the array to exclude scores above a certain threshold, or
 * to take the top scorer, etc.
 */
export class QueryScorer {
  /**
   * @param {object} options
   *   Constructor options.
   * @param {number} [options.distanceThreshold]
   *   Edit distances no larger than this value are considered matches.
   * @param {Map} [options.variations]
   *   For convenience, the scorer can augment documents by replacing certain
   *   words with other words and phrases. This mechanism is called variations.
   *   This keys of this map are words that should be replaced, and the values
   *   are the replacement words or phrases.  For example, if you add a document
   *   whose only phrase is "firefox update", normally the scorer will register
   *   only this single phrase for the document.  However, if you pass the value
   *   `new Map(["firefox", ["fire fox", "fox fire", "foxfire"]])` for this
   *   parameter, it will register 4 total phrases for the document: "fire fox
   *   update", "fox fire update", "foxfire update", and the original "firefox
   *   update".
   */
  constructor({ distanceThreshold = 1, variations = new Map() } = {}) {
    this._distanceThreshold = distanceThreshold;
    this._variations = variations;
    this._documents = new Set();
    this._rootNode = new Node();
  }

  /**
   * Adds a document to the scorer.
   *
   * @param {object} doc
   *   The document.
   * @param {string} doc.id
   *   The document's ID.
   * @param {Array} doc.phrases
   *   The set of phrases in the document.  Each phrase should be a string.
   */
  addDocument(doc) {
    this._documents.add(doc);

    for (let phraseStr of doc.phrases) {
      // Split the phrase and lowercase the words.
      let phrase = phraseStr
        .trim()
        .split(/\s+/)
        .map(word => word.toLocaleLowerCase());

      // Build a phrase list that contains the original phrase plus its
      // variations, if any.
      let phrases = [phrase];
      for (let [triggerWord, variations] of this._variations) {
        let index = phrase.indexOf(triggerWord);
        if (index >= 0) {
          for (let variation of variations) {
            let variationPhrase = Array.from(phrase);
            variationPhrase.splice(index, 1, ...variation.split(/\s+/));
            phrases.push(variationPhrase);
          }
        }
      }

      // Finally, add the phrases to the phrase tree.
      for (let completedPhrase of phrases) {
        this._buildPhraseTree(this._rootNode, doc, completedPhrase, 0);
      }
    }
  }

  /**
   * Scores a query string against the documents in the scorer.
   *
   * @param {string} queryString
   *   The query string to score.
   * @returns {Array}
   *   An array of objects: { document, score }.  Each element in the array is a
   *   a document and its score against the query string.  The elements are
   *   ordered by score from low to high.  Scores represent edit distance, so
   *   lower scores are better.
   */
  score(queryString) {
    let queryWords = queryString
      .trim()
      .split(/\s+/)
      .map(word => word.toLocaleLowerCase());
    let minDistanceByDoc = this._traverse({ queryWords });
    let results = [];
    for (let doc of this._documents) {
      let distance = minDistanceByDoc.get(doc);
      results.push({
        document: doc,
        score: distance === undefined ? Infinity : distance,
      });
    }
    results.sort((a, b) => a.score - b.score);
    return results;
  }

  /**
   * Builds the phrase tree based on the current documents.
   *
   * The phrase tree lets us efficiently match queries against phrases.  Each
   * path through the tree starting from the root and ending at a leaf
   * represents a complete phrase in a document (or more than one document, if
   * the same phrase is present in multiple documents).  Each node in the path
   * represents a word in the phrase.  To match a query, we start at the root,
   * and in the root we look up the query's first word.  If the word matches the
   * first word of any phrase, then the root will have a child node representing
   * that word, and we move on to the child node.  Then we look up the query's
   * second word in the child node, and so on, until either a lookup fails or we
   * reach a leaf node.
   *
   * @param {Node} node
   *   The current node being visited.
   * @param {object} doc
   *   The document whose phrases are being added to the tree.
   * @param {Array} phrase
   *   The phrase to add to the tree.
   * @param {number} wordIndex
   *   The index in the phrase of the current word.
   */
  _buildPhraseTree(node, doc, phrase, wordIndex) {
    if (phrase.length == wordIndex) {
      // We're done with this phrase.
      return;
    }

    let word = phrase[wordIndex].toLocaleLowerCase();
    let child = node.childrenByWord.get(word);
    if (!child) {
      child = new Node(word);
      node.childrenByWord.set(word, child);
    }
    child.documents.add(doc);

    // Recurse with the next word in the phrase.
    this._buildPhraseTree(child, doc, phrase, wordIndex + 1);
  }

  /**
   * Traverses a path in the phrase tree in order to score a query.  See
   * `_buildPhraseTree` for a description of how this works.
   *
   * @param {object} options
   *   Options.
   * @param {Array} options.queryWords
   *   The query being scored, split into words.
   * @param {Node} [options.node]
   *   The node currently being visited.
   * @param {Map} [options.minDistanceByDoc]
   *   Keeps track of the minimum edit distance for each document as the
   *   traversal continues.
   * @param {number} [options.queryWordsIndex]
   *   The current index in the query words array.
   * @param {number} [options.phraseDistance]
   *   The total edit distance between the query and the path in the tree that's
   *   been traversed so far.
   * @returns {Map} minDistanceByDoc
   */
  _traverse({
    queryWords,
    node = this._rootNode,
    minDistanceByDoc = new Map(),
    queryWordsIndex = 0,
    phraseDistance = 0,
  }) {
    if (!node.childrenByWord.size) {
      // We reached a leaf node.  The query has matched a phrase.  If the query
      // and the phrase have the same number of words, then queryWordsIndex ==
      // queryWords.length also.  Otherwise the query contains more words than
      // the phrase.  We still count that as a match.
      for (let doc of node.documents) {
        minDistanceByDoc.set(
          doc,
          Math.min(
            phraseDistance,
            minDistanceByDoc.has(doc) ? minDistanceByDoc.get(doc) : Infinity
          )
        );
      }
      return minDistanceByDoc;
    }

    if (queryWordsIndex == queryWords.length) {
      // We exhausted all the words in the query but have not reached a leaf
      // node.  No match; the query has matched a phrase(s) up to this point,
      // but it doesn't have enough words.
      return minDistanceByDoc;
    }

    // Compare each word in the node to the current query word.
    let queryWord = queryWords[queryWordsIndex];
    for (let [childWord, child] of node.childrenByWord) {
      let distance = lazy.NLP.levenshtein(queryWord, childWord);
      if (distance <= this._distanceThreshold) {
        // The word represented by this child node matches the current query
        // word.  Recurse into the child node.
        this._traverse({
          node: child,
          queryWords,
          queryWordsIndex: queryWordsIndex + 1,
          phraseDistance: phraseDistance + distance,
          minDistanceByDoc,
        });
      }
      // Else, the path that continues at the child node can't possibly match
      // the query, so don't recurse into it.
    }

    return minDistanceByDoc;
  }
}

/**
 * Gets appropriate values for each tip's payload.
 *
 * @param {string} tip a value from UrlbarShared.INTERVENTION_TIP_TYPE
 * @returns {object} Properties to include in the payload
 */
function getPayloadForTip(tip) {
  const baseURL = Services.urlFormatter.formatURLPref("app.support.baseURL");
  switch (tip) {
    case UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR:
      return {
        titleL10n: { id: "intervention-clear-data" },
        buttons: [{ l10n: { id: "intervention-clear-data-confirm" } }],
        helpUrl: baseURL + "delete-browsing-search-download-history-firefox",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH:
      return {
        titleL10n: { id: "intervention-refresh-profile" },
        buttons: [{ l10n: { id: "intervention-refresh-profile-confirm" } }],
        helpUrl: baseURL + "refresh-firefox-reset-add-ons-and-settings",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_ASK:
      return {
        titleL10n: { id: "intervention-update-ask" },
        buttons: [{ l10n: { id: "intervention-update-ask-confirm" } }],
        helpUrl: baseURL + "update-firefox-latest-release",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_REFRESH:
      return {
        titleL10n: { id: "intervention-update-refresh" },
        buttons: [{ l10n: { id: "intervention-update-refresh-confirm" } }],
        helpUrl: baseURL + "refresh-firefox-reset-add-ons-and-settings",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_RESTART:
      return {
        titleL10n: { id: "intervention-update-restart" },
        buttons: [{ l10n: { id: "intervention-update-restart-confirm" } }],
        helpUrl: baseURL + "update-firefox-latest-release",
      };
    case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_WEB:
      return {
        titleL10n: { id: "intervention-update-web" },
        buttons: [{ l10n: { id: "intervention-update-web-confirm" } }],
        helpUrl: baseURL + "update-firefox-latest-release",
      };
    default:
      throw new Error("Unknown TIP type.");
  }
}

/**
 * A provider that returns actionable tip results when the user is performing
 * a search related to those actions.
 */
export class UrlbarProviderInterventions extends UrlbarProvider {
  static lazy = XPCOMUtils.declareLazy({
    // This object is used to match the user's queries to tips.
    queryScorer: () => {
      let queryScorer = new QueryScorer({
        variations: new Map([
          // Recognize "fire fox", "fox fire", and "foxfire" as "firefox".
          ["firefox", ["fire fox", "fox fire", "foxfire"]],
          // Recognize "mozila" as "mozilla".  This will catch common mispellings
          // "mozila", "mozzila", and "mozzilla" (among others) due to the edit
          // distance threshold of 1.
          ["mozilla", ["mozila"]],
        ]),
      });
      for (let [id, phrases] of Object.entries(DOCUMENTS)) {
        queryScorer.addDocument({ id, phrases });
      }
      return queryScorer;
    },
  });

  /**
   * The tip we should currently show.
   *
   * @type {Values<typeof UrlbarShared.INTERVENTION_TIP_TYPE>}
   */
  currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.NONE;

  /**
   * @returns {Values<typeof UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    if (
      !queryContext.searchString ||
      queryContext.searchString.length > UrlbarShared.MAX_TEXT_LENGTH ||
      lazy.UrlUtils.REGEXP_LIKE_PROTOCOL.test(queryContext.searchString) ||
      !EN_LOCALE_MATCH.test(Services.locale.appLocaleAsBCP47) ||
      !Services.policies.isAllowed("urlbarinterventions") ||
      (await this.queryInstance
        .getProvider(lazy.UrlbarProviderGlobalActions.name)
        ?.isActive(queryContext))
    ) {
      return false;
    }

    this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.NONE;

    // Get the scores and the top score.
    let docScores = UrlbarProviderInterventions.lazy.queryScorer.score(
      queryContext.searchString
    );
    let topDocScore = docScores[0];

    // Multiple docs may have the top score, so collect them all.
    let topDocIDs = new Set();
    if (topDocScore.score != Infinity) {
      for (let { score, document } of docScores) {
        if (score != topDocScore.score) {
          break;
        }
        topDocIDs.add(document.id);
      }
    }

    // Determine the tip to show, if any. If there are multiple top-score docs,
    // prefer them in the following order.
    if (topDocIDs.has("update")) {
      this._setCurrentTipFromAppUpdaterStatus();
    } else if (topDocIDs.has("clear")) {
      let window = lazy.BrowserWindowTracker.getTopWindow();
      if (!lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR;
      }
    } else if (topDocIDs.has("refresh")) {
      // Note that the "update" case can set currentTip to UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH too.
      this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH;
    }

    return (
      this.currentTip != UrlbarShared.INTERVENTION_TIP_TYPE.NONE &&
      (this.currentTip != UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH ||
        Services.policies.isAllowed("profileRefresh"))
    );
  }

  async _setCurrentTipFromAppUpdaterStatus() {
    // The update tips depend on the app's update status, so check for updates
    // now (if we haven't already checked within the update-check period).  If
    // we're running in an xpcshell test, then checkForBrowserUpdate's attempt
    // to use appUpdater will throw an exception because it won't be available.
    // In that case, return false to disable the provider.
    //
    // This causes synchronous IO within the updater the first time it's called
    // (at least) so be careful not to do it the first time the urlbar is used.
    try {
      UrlbarProviderInterventions.checkForBrowserUpdate();
    } catch (ex) {
      return;
    }

    // There are several update tips. Figure out which one to show.
    switch (lazy.appUpdater.status) {
      case lazy.AppUpdater.STATUS.READY_FOR_RESTART:
        // Prompt the user to restart.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_RESTART;
        break;
      case lazy.AppUpdater.STATUS.DOWNLOAD_AND_INSTALL:
        // There's an update available, but the user's pref says we should ask
        // them to download and apply it.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_ASK;
        break;
      case lazy.AppUpdater.STATUS.NO_UPDATES_FOUND:
        // We show a special refresh tip when the browser is up to date.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_REFRESH;
        break;
      case lazy.AppUpdater.STATUS.CHECKING:
        // This will be the case the first time we check.  See startQuery for
        // how this special tip is handled.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING;
        break;
      case lazy.AppUpdater.STATUS.NO_UPDATER:
      case lazy.AppUpdater.STATUS.UPDATE_DISABLED_BY_POLICY:
        // If the updater is disabled at build time or at runtime, either by
        // policy or because we're in a package, do not select any update tips.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.NONE;
        break;
      default:
        // Give up and ask the user to download the latest version from the
        // web. We default to this case when the update is still downloading
        // because an update doesn't actually occur if the user were to
        // restart the browser. See bug 1625241.
        this.currentTip = UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_WEB;
        break;
    }
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let instance = this.queryInstance;

    // UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING is special, and we never actually show a tip that
    // reflects a "checking" status.  Instead it's handled like this.  We call
    // appUpdater.check() to start an update check.  If we haven't called it
    // before, then when it returns, appUpdater.status will be
    // AppUpdater.STATUS.CHECKING, and it will remain CHECKING until the check
    // finishes.  We can add a listener to appUpdater to be notified when the
    // check finishes.  We don't want to wait for it to finish in isActive
    // because that would block other providers from adding their results, so
    // instead we wait here in startQuery.  The results from other providers
    // will be added while we're waiting.  When the check finishes, we call
    // addCallback and add our result.  It doesn't matter how long the check
    // takes because if another query starts, the view is closed, or the user
    // changes the selection, the query will be canceled.
    if (this.currentTip == UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING) {
      // First check the status because it may have changed between the time
      // isActive was called and now.
      this._setCurrentTipFromAppUpdaterStatus();
      if (
        this.currentTip == UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING
      ) {
        // The updater is still checking, so wait for it to finish.
        await new Promise(resolve => {
          this._appUpdaterListener = () => {
            lazy.appUpdater.removeListener(this._appUpdaterListener);
            delete this._appUpdaterListener;
            resolve();
          };
          lazy.appUpdater.addListener(this._appUpdaterListener);
        });
        if (instance != this.queryInstance) {
          // The query was canceled before the check finished.
          return;
        }
        // Finally, set the tip from the updater status.  The updater should no
        // longer be checking, but guard against it just in case by returning
        // early.
        this._setCurrentTipFromAppUpdaterStatus();
        if (
          this.currentTip == UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING
        ) {
          return;
        }
      }
    }
    // At this point, this.currentTip != UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_CHECKING because we
    // returned early above if it was.

    let result = new lazy.UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.TIP,
      source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
      suggestedIndex: 1,
      payload: {
        ...getPayloadForTip(this.currentTip),
        type: this.currentTip,
        icon: UrlbarShared.ICON.TIP,
        helpL10n: {
          id: "urlbar-result-menu-tip-get-help2",
        },
      },
    });
    addCallback(this, result);
  }

  /**
   * Cancels a running query,
   */
  cancelQuery() {
    // If we're waiting for appUpdater to finish its update check,
    // this._appUpdaterListener will be defined.  We can stop listening now.
    if (this._appUpdaterListener) {
      lazy.appUpdater.removeListener(this._appUpdaterListener);
      delete this._appUpdaterListener;
    }
  }

  #pickResult(result, window) {
    let tip = result.payload.type;

    // Do the tip action.
    switch (tip) {
      case UrlbarShared.INTERVENTION_TIP_TYPE.CLEAR:
        openClearHistoryDialog(window);
        break;
      case UrlbarShared.INTERVENTION_TIP_TYPE.REFRESH:
      case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_REFRESH:
        resetBrowser(window);
        break;
      case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_ASK:
        installBrowserUpdateAndRestart();
        break;
      case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_RESTART:
        restartBrowser();
        break;
      case UrlbarShared.INTERVENTION_TIP_TYPE.UPDATE_WEB:
        window.gBrowser.selectedTab = window.gBrowser.addWebTab(
          "https://www.mozilla.org/firefox/new/"
        );
        break;
    }
  }

  onEngagement(queryContext, controller, details) {
    // `selType` is "tip" when the tip's main button is picked. Ignore clicks on
    // the help command ("help"), which is handled by UrlbarInput since we
    // set `helpUrl` on the result payload. Currently there aren't any other
    // buttons or commands but this will ignore clicks on them too.
    if (details.selType == "tip") {
      this.#pickResult(details.result, controller.browserWindow);
    }
  }

  static _lastUpdateCheckTime;
  /**
   * Checks for app updates.
   *
   * @param {boolean} force If false, this only checks for updates if we haven't
   *        already checked within the update-check period.  If true, we check
   *        regardless.
   */
  static checkForBrowserUpdate(force = false) {
    if (
      force ||
      !UrlbarProviderInterventions._lastUpdateCheckTime ||
      Date.now() - UrlbarProviderInterventions._lastUpdateCheckTime >=
        UPDATE_CHECK_PERIOD_MS
    ) {
      UrlbarProviderInterventions._lastUpdateCheckTime = Date.now();
      lazy.appUpdater.check();
    }
  }

  /**
   * Resets the provider's app updater state by making a new app updater.  This
   * is intended to be used by tests.
   */
  static resetAppUpdater() {
    // Reset only if the object has already been initialized.
    if (!Object.getOwnPropertyDescriptor(lazy, "appUpdater").get) {
      lazy.appUpdater = new lazy.AppUpdater();
    }
  }
}

/**
 * Tip callbacks follow.
 */

function installBrowserUpdateAndRestart() {
  if (lazy.appUpdater.status != lazy.AppUpdater.STATUS.DOWNLOAD_AND_INSTALL) {
    return Promise.resolve();
  }
  return new Promise(resolve => {
    let listener = () => {
      // Once we call allowUpdateDownload, there are two possible end
      // states: DOWNLOAD_FAILED and READY_FOR_RESTART.
      if (
        lazy.appUpdater.status != lazy.AppUpdater.STATUS.READY_FOR_RESTART &&
        lazy.appUpdater.status != lazy.AppUpdater.STATUS.DOWNLOAD_FAILED
      ) {
        return;
      }
      lazy.appUpdater.removeListener(listener);
      if (lazy.appUpdater.status == lazy.AppUpdater.STATUS.READY_FOR_RESTART) {
        restartBrowser();
      }
      resolve();
    };
    lazy.appUpdater.addListener(listener);
    lazy.appUpdater.allowUpdateDownload();
  });
}

function openClearHistoryDialog(window) {
  // The behaviour of the Clear Recent History dialog in PBM does
  // not have the expected effect (bug 463607).
  if (lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
    return;
  }
  lazy.Sanitizer.showUI(window);
}

function restartBrowser() {
  // Notify all windows that an application quit has been requested.
  let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
    Ci.nsISupportsPRBool
  );
  Services.obs.notifyObservers(
    cancelQuit,
    "quit-application-requested",
    "restart"
  );
  // Something aborted the quit process.
  if (cancelQuit.data) {
    return;
  }
  // If already in safe mode restart in safe mode.
  if (Services.appinfo.inSafeMode) {
    Services.startup.restartInSafeMode(Ci.nsIAppStartup.eAttemptQuit);
  } else {
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
  }
}

function resetBrowser(window) {
  if (!lazy.ResetProfile.resetSupported()) {
    return;
  }
  lazy.ResetProfile.openConfirmationDialog(window);
}
