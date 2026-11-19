// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { Rect } from "resource://gre/modules/Geometry.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

import { playSound } from "resource://gre/modules/FinderSound.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FinderIterator: "resource://gre/modules/FinderIterator.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SelectionUtils: "resource://gre/modules/SelectionUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ClipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  Ci.nsIClipboardHelper
);

const kSelectionMaxLen = 150;
const kMatchesCountLimitPref = "accessibility.typeaheadfind.matchesCountLimit";

const activeFinderRoots = new WeakSet();

export function Finder(docShell) {
  this._fastFind = Cc["@mozilla.org/typeaheadfind;1"].createInstance(
    Ci.nsITypeAheadFind
  );
  this._fastFind.init(docShell);

  this._currentFoundRange = null;
  this._docShell = docShell;
  this._listeners = [];
  this._previousLink = null;
  this._searchString = null;
  this._highlighter = null;

  docShell
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIWebProgress)
    .addProgressListener(this, Ci.nsIWebProgress.NOTIFY_LOCATION);
  docShell.domWindow.addEventListener(
    "unload",
    this.onLocationChange.bind(this, { isTopLevel: true })
  );
}

Finder.isFindbarVisible = function (docShell) {
  return activeFinderRoots.has(docShell.browsingContext.top);
};

Finder.prototype = {
  get iterator() {
    if (!this._iterator) {
      this._iterator = new lazy.FinderIterator();
    }
    return this._iterator;
  },

  destroy() {
    if (this._iterator) {
      this._iterator.reset();
    }
    let window = this._getWindow();
    if (this._highlighter && window) {
      // if we clear all the references before we hide the highlights (in both
      // highlighting modes), we simply can't use them to find the ranges we
      // need to clear from the selection.
      this._highlighter.hide(window);
      this._highlighter.clear(window);
      this.highlighter.removeScrollMarks();
    }
    this.listeners = [];
    this._docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress)
      .removeProgressListener(this, Ci.nsIWebProgress.NOTIFY_LOCATION);
    this._listeners = [];
    this._currentFoundRange =
      this._fastFind =
      this._docShell =
      this._previousLink =
      this._highlighter =
        null;
  },

  addResultListener(aListener) {
    if (!this._listeners.includes(aListener)) {
      this._listeners.push(aListener);
    }
  },

  removeResultListener(aListener) {
    this._listeners = this._listeners.filter(l => l != aListener);
  },

  _setResults(options) {
    if (typeof options.storeResult != "boolean") {
      options.storeResult = true;
    }

    if (options.storeResult) {
      this._searchString = options.searchString;
      this.clipboardSearchString = options.searchString;
    }

    let foundLink = this._fastFind.foundLink;
    let linkURL = null;
    if (foundLink) {
      linkURL = Services.textToSubURI.unEscapeURIForUI(foundLink.href);
    }

    options.linkURL = linkURL;
    options.rect = this._getResultRect();
    options.searchString = this._searchString;

    this._outlineLink(options.drawOutline);

    for (let l of this._listeners) {
      try {
        l.onFindResult(options);
      } catch (ex) {}
    }
  },

  get searchString() {
    if (!this._searchString && this._fastFind.searchString) {
      this._searchString = this._fastFind.searchString;
    }
    return this._searchString;
  },

  get clipboardSearchString() {
    return GetClipboardSearchString(
      this._getWindow().docShell.QueryInterface(Ci.nsILoadContext)
    );
  },

  set clipboardSearchString(aSearchString) {
    if (!lazy.PrivateBrowsingUtils.isContentWindowPrivate(this._getWindow())) {
      SetClipboardSearchString(aSearchString);
    }
  },

  set caseSensitive(aSensitive) {
    if (this._fastFind.caseSensitive === aSensitive) {
      return;
    }
    this._fastFind.caseSensitive = aSensitive;
    this.iterator.reset();
  },

  set matchDiacritics(aMatchDiacritics) {
    if (this._fastFind.matchDiacritics === aMatchDiacritics) {
      return;
    }
    this._fastFind.matchDiacritics = aMatchDiacritics;
    this.iterator.reset();
  },

  set entireWord(aEntireWord) {
    if (this._fastFind.entireWord === aEntireWord) {
      return;
    }
    this._fastFind.entireWord = aEntireWord;
    this.iterator.reset();
  },

  get highlighter() {
    if (this._highlighter) {
      return this._highlighter;
    }

    const { FinderHighlighter } = ChromeUtils.importESModule(
      "resource://gre/modules/FinderHighlighter.sys.mjs"
    );
    return (this._highlighter = new FinderHighlighter(this));
  },

  get matchesCountLimit() {
    if (typeof this._matchesCountLimit == "number") {
      return this._matchesCountLimit;
    }

    this._matchesCountLimit =
      Services.prefs.getIntPref(kMatchesCountLimitPref) || 0;
    return this._matchesCountLimit;
  },

  _lastFindResult: null,

  /**
   * Used for normal search operations, highlights the first match.
   * This method is used only for compatibility with non-remote browsers.
   *
   * @param aSearchString String to search for.
   * @param aLinksOnly Only consider nodes that are links for the search.
   * @param aDrawOutline Puts an outline around matched links.
   */
  fastFind(aSearchString, aLinksOnly, aDrawOutline) {
    let searchLengthened =
      aSearchString.length > this._fastFind.searchString.length;

    this._lastFindResult = this._fastFind.find(
      aSearchString,
      aLinksOnly,
      Ci.nsITypeAheadFind.FIND_INITIAL,
      false
    );
    let searchString = this._fastFind.searchString;

    let results = {
      searchString,
      result: this._lastFindResult,
      findBackwards: false,
      findAgain: false,
      drawOutline: aDrawOutline,
      linksOnly: aLinksOnly,
      useSubFrames: true,
    };

    this._setResults(results);
    this.updateHighlightAndMatchCount(results);

    if (
      searchLengthened &&
      this._lastFindResult.result == Ci.nsITypeAheadFind.FIND_NOTFOUND &&
      !this._fastFind.entireWord
    ) {
      playSound("not-found");
    }

    return this._lastFindResult;
  },

  /**
   * Repeat the previous search. Should only be called after a previous
   * call to Finder.fastFind.
   * This method is used only for compatibility with non-remote browsers.
   *
   * @param aSearchString String to search for.
   * @param aFindBackwards Controls the search direction:
   *    true: before current match, false: after current match.
   * @param aLinksOnly Only consider nodes that are links for the search.
   * @param aDrawOutline Puts an outline around matched links.
   */
  findAgain(aSearchString, aFindBackwards, aLinksOnly, aDrawOutline) {
    let mode = aFindBackwards
      ? Ci.nsITypeAheadFind.FIND_PREVIOUS
      : Ci.nsITypeAheadFind.FIND_NEXT;
    this._lastFindResult = this._fastFind.find(
      aFindBackwards,
      aLinksOnly,
      mode,
      false
    );
    let searchString = this._fastFind.searchString;

    let results = {
      searchString,
      result: this._lastFindResult,
      findBackwards: aFindBackwards,
      findAgain: true,
      drawOutline: aDrawOutline,
      linksOnly: aLinksOnly,
      useSubFrames: true,
    };
    this._setResults(results);
    this.updateHighlightAndMatchCount(results);

    if (this._lastFindResult.result == Ci.nsITypeAheadFind.FIND_WRAPPED) {
      playSound("wrapped");
    }

    return this._lastFindResult;
  },

  /**
   * Used for normal search operations, highlights the first or
   * subsequent match depending on the mode.
   *
   * Options are:
   *  searchString String to search for.
   *  findAgain True if this a find again operation.
   *  mode Search mode from nsITypeAheadFind.
   *  linksOnly Only consider nodes that are links for the search.
   *  drawOutline Puts an outline around matched links.
   *  useSubFrames True to iterate over subframes.
   *  caseSensitive True for case sensitive searching.
   *  entireWord True to match entire words.
   *  matchDiacritics True to match diacritics.
   */
  find(options) {
    this.caseSensitive = options.caseSensitive;
    this.entireWord = options.entireWord;
    this.matchDiacritics = options.matchDiacritics;

    this._lastFindResult = this._fastFind.find(
      options.searchString,
      options.linksOnly,
      options.mode,
      !options.useSubFrames
    );
    let searchString = this._fastFind.searchString;
    let results = {
      searchString,
      result: this._lastFindResult,
      findBackwards:
        options.mode == Ci.nsITypeAheadFind.FIND_PREVIOUS ||
        options.mode == Ci.nsITypeAheadFind.FIND_LAST,
      findAgain: options.findAgain,
      drawOutline: options.drawOutline,
      linksOnly: options.linksOnly,
      entireWord: this._fastFind.entireWord,
      useSubFrames: options.useSubFrames,
    };
    this._setResults(results, options.mode);
    return new Promise(resolve => resolve(results));
  },

  /**
   * Forcibly set the search string of the find clipboard to the currently
   * selected text in the window, on supported platforms (i.e. OSX).
   */
  setSearchStringToSelection() {
    let searchInfo = this.getActiveSelectionText();

    // If an empty string is returned or a subframe is focused, don't
    // assign the search string.
    if (searchInfo.selectedText) {
      this.clipboardSearchString = searchInfo.selectedText;
    }

    return searchInfo;
  },

  async highlight(aHighlight, aWord, aLinksOnly, aUseSubFrames = true) {
    let result = await this.highlighter.highlight(
      aHighlight,
      aWord,
      aLinksOnly,
      false,
      aUseSubFrames
    );
    if (aHighlight && !this.iterator.running) {
      this.highlighter.updateScrollMarks();
    }
    return result;
  },

  async updateHighlightAndMatchCount(aArgs) {
    this._lastFindResult = aArgs;

    if (
      !this.iterator.continueRunning({
        caseSensitive: this._fastFind.caseSensitive,
        entireWord: this._fastFind.entireWord,
        linksOnly: aArgs.linksOnly,
        matchDiacritics: this._fastFind.matchDiacritics,
        word: aArgs.searchString,
        useSubFrames: aArgs.useSubFrames,
      })
    ) {
      this.iterator.stop();
    }

    let highlightPromise = this.highlighter.update(
      aArgs,
      aArgs.useSubFrames ? false : aArgs.foundInThisFrame
    );

    let matchCountPromise = this.requestMatchesCount(aArgs.searchString, {
      linksOnly: aArgs.linksOnly,
      useSubFrames: aArgs.useSubFrames,
    });

    let results = await Promise.all([highlightPromise, matchCountPromise]);

    this.highlighter.updateScrollMarks();

    if (results[1]) {
      return Object.assign(results[1], results[0]);
    } else if (results[0]) {
      return results[0];
    }

    return null;
  },

  getInitialSelection() {
    let initialSelection = this.getActiveSelectionText().selectedText;
    this._getWindow().setTimeout(() => {
      for (let l of this._listeners) {
        try {
          l.onCurrentSelection(initialSelection, true);
        } catch (ex) {}
      }
    }, 0);
  },

  getActiveSelectionText() {
    let focusedWindow = {};
    let focusedElement = Services.focus.getFocusedElementForWindow(
      this._getWindow(),
      true,
      focusedWindow
    );
    focusedWindow = focusedWindow.value;

    let selText;

    // If this is a remote subframe, return an empty string but
    // indiciate which browsing context was focused.
    if (
      focusedElement &&
      "frameLoader" in focusedElement &&
      BrowsingContext.isInstance(focusedElement.browsingContext)
    ) {
      return {
        focusedChildBrowserContextId: focusedElement.browsingContext.id,
        selectedText: "",
      };
    }

    if (focusedElement && focusedElement.editor) {
      // The user may have a selection in an input or textarea.
      selText = focusedElement.editor.selectionController
        .getSelection(Ci.nsISelectionController.SELECTION_NORMAL)
        .toString();
    } else {
      // Look for any selected text on the actual page.
      selText = focusedWindow.getSelection().toString();
    }

    if (!selText) {
      return { selectedText: "" };
    }

    // Process our text to get rid of unwanted characters.
    selText = lazy.SelectionUtils.truncateForDisplay(
      selText,
      kSelectionMaxLen,
      { addEllipsis: false }
    );

    return { selectedText: selText };
  },

  enableSelection() {
    this._fastFind.setSelectionModeAndRepaint(
      Ci.nsISelectionController.SELECTION_ON
    );
    this._restoreOriginalOutline();
  },

  removeSelection(keepHighlight) {
    this._fastFind.collapseSelection();
    this.enableSelection();
    let window = this._getWindow();
    if (keepHighlight) {
      this.highlighter.clearCurrentOutline(window);
    } else {
      this.highlighter.clear(window);
      this.highlighter.removeScrollMarks();
    }
  },

  focusContent() {
    // Allow Finder listeners to cancel focusing the content.
    for (let l of this._listeners) {
      try {
        if ("shouldFocusContent" in l && !l.shouldFocusContent()) {
          return;
        }
      } catch (ex) {
        console.error(ex);
      }
    }

    let fastFind = this._fastFind;
    try {
      // Try to find the best possible match that should receive focus and
      // block scrolling on focus since find already scrolls. Further
      // scrolling is due to user action, so don't override this.
      if (fastFind.foundLink) {
        Services.focus.setFocus(
          fastFind.foundLink,
          Services.focus.FLAG_NOSCROLL
        );
      } else if (fastFind.foundEditable) {
        Services.focus.setFocus(
          fastFind.foundEditable,
          Services.focus.FLAG_NOSCROLL
        );
        fastFind.collapseSelection();
      } else {
        this._getWindow().focus();
      }
    } catch (e) {}
  },

  onFindbarClose() {
    this.enableSelection();
    this.highlighter.highlight(false);
    this.highlighter.removeScrollMarks();
    this.iterator.reset();
    activeFinderRoots.delete(this._docShell.browsingContext.top);
  },

  onFindbarOpen() {
    activeFinderRoots.add(this._docShell.browsingContext.top);
  },

  onModalHighlightChange(useModalHighlight) {
    if (this._highlighter) {
      this._highlighter.onModalHighlightChange(useModalHighlight);
    }
  },

  onHighlightAllChange(highlightAll) {
    if (this._highlighter) {
      this._highlighter.onHighlightAllChange(highlightAll);
    }
    if (this._iterator) {
      this._iterator.reset();
    }
  },

  keyPress(aEvent) {
    let controller = this._getSelectionController(this._getWindow());
    let accelKeyPressed =
      AppConstants.platform == "macosx" ? aEvent.metaKey : aEvent.ctrlKey;

    switch (aEvent.keyCode) {
      case aEvent.DOM_VK_RETURN:
        if (this._fastFind.foundLink) {
          let view = this._fastFind.foundLink.documentGlobal;
          this._fastFind.foundLink.dispatchEvent(
            new view.PointerEvent("click", {
              view,
              cancelable: true,
              bubbles: true,
              ctrlKey: aEvent.ctrlKey,
              altKey: aEvent.altKey,
              shiftKey: aEvent.shiftKey,
              metaKey: aEvent.metaKey,
            })
          );
        }
        break;
      case aEvent.DOM_VK_TAB: {
        let direction = Services.focus.MOVEFOCUS_FORWARD;
        if (aEvent.shiftKey) {
          direction = Services.focus.MOVEFOCUS_BACKWARD;
        }
        Services.focus.moveFocus(this._getWindow(), null, direction, 0);
        break;
      }
      case aEvent.DOM_VK_PAGE_UP:
        controller.scrollPage(false);
        break;
      case aEvent.DOM_VK_PAGE_DOWN:
        controller.scrollPage(true);
        break;
      case aEvent.DOM_VK_UP:
        if (accelKeyPressed) {
          controller.completeScroll(false);
        } else {
          controller.scrollLine(false);
        }
        break;
      case aEvent.DOM_VK_DOWN:
        if (accelKeyPressed) {
          controller.completeScroll(true);
        } else {
          controller.scrollLine(true);
        }
        break;
    }
  },

  _notifyMatchesCount(aWord, result = this._currentMatchesCountResult) {
    // The `_currentFound` property is only used for internal bookkeeping.
    delete result._currentFound;
    result.searchString = aWord;
    result.limit = this.matchesCountLimit;
    if (result.total == result.limit) {
      result.total = -1;
    }

    for (let l of this._listeners) {
      try {
        l.onMatchesCountResult(result);
      } catch (ex) {}
    }

    this._currentMatchesCountResult = null;
    return result;
  },

  async requestMatchesCount(aWord, optionalArgs) {
    if (
      this._lastFindResult == Ci.nsITypeAheadFind.FIND_NOTFOUND ||
      this.searchString == "" ||
      !aWord ||
      !this.matchesCountLimit
    ) {
      return this._notifyMatchesCount(aWord, {
        total: 0,
        current: 0,
      });
    }

    this._currentFoundRange = this._fastFind.getFoundRange();

    let params = {
      caseSensitive: this._fastFind.caseSensitive,
      entireWord: this._fastFind.entireWord,
      linksOnly: optionalArgs.linksOnly || false,
      matchDiacritics: this._fastFind.matchDiacritics,
      word: aWord,
      useSubFrames:
        optionalArgs.useSubFrames !== undefined
          ? optionalArgs.useSubFrames
          : true,
    };

    if (!this.iterator.continueRunning(params)) {
      this.iterator.stop();
    }

    await this.iterator.start(
      Object.assign(params, {
        finder: this,
        limit: this.matchesCountLimit,
        listener: this,
        useCache: true,
        useSubFrames:
          optionalArgs.useSubFrames !== undefined
            ? optionalArgs.useSubFrames
            : true,
        contextRange: optionalArgs.contextRange || 0,
      })
    );

    // Without a valid result, there's nothing to notify about. This happens
    // when the iterator was started before and won the race.
    if (!this._currentMatchesCountResult) {
      return null;
    }

    return this._notifyMatchesCount(aWord);
  },

  // FinderIterator listener implementation

  onIteratorRangeFound(range, extra) {
    let result = this._currentMatchesCountResult;
    if (!result) {
      return;
    }

    ++result.total;

    // Pull out the snippet that finderIterator attached
    if (extra?.context) {
      if (!result.snippets) {
        result.snippets = [];
      }
      result.snippets.push(extra.context);
    }

    if (!result._currentFound) {
      ++result.current;
      result._currentFound =
        this._currentFoundRange &&
        range.startContainer == this._currentFoundRange.startContainer &&
        range.startOffset == this._currentFoundRange.startOffset &&
        range.endContainer == this._currentFoundRange.endContainer &&
        range.endOffset == this._currentFoundRange.endOffset;
    }
  },

  onIteratorReset() {},

  onIteratorRestart({ word, linksOnly, useSubFrames }) {
    this.requestMatchesCount(word, {
      linksOnly,
      useSubFrames,
    });
  },

  onIteratorStart() {
    this._currentMatchesCountResult = {
      total: 0,
      current: 0,
      _currentFound: false,
    };
  },

  _getWindow() {
    if (!this._docShell) {
      return null;
    }
    return this._docShell.domWindow;
  },

  /**
   * Get the bounding selection rect in CSS px relative to the origin of the
   * top-level content document.
   */
  _getResultRect() {
    let topWin = this._getWindow();
    let win = this._fastFind.currentWindow;
    if (!win) {
      return null;
    }

    let selection = win.getSelection();
    if (!selection.rangeCount || selection.isCollapsed) {
      // The selection can be into an input or a textarea element.
      let nodes = win.document.querySelectorAll("input, textarea");
      for (let node of nodes) {
        if (node.editor) {
          try {
            let sc = node.editor.selectionController;
            selection = sc.getSelection(
              Ci.nsISelectionController.SELECTION_NORMAL
            );
            if (selection.rangeCount && !selection.isCollapsed) {
              break;
            }
          } catch (e) {
            // If this textarea is hidden, then its selection controller might
            // not be intialized. Ignore the failure.
          }
        }
      }
    }

    if (!selection.rangeCount || selection.isCollapsed) {
      return null;
    }

    let utils = topWin.windowUtils;

    let scrollX = {},
      scrollY = {};
    utils.getScrollXY(false, scrollX, scrollY);

    for (let frame = win; frame != topWin; frame = frame.parent) {
      let rect = frame.frameElement.getBoundingClientRect();
      let left = frame.getComputedStyle(frame.frameElement).borderLeftWidth;
      let top = frame.getComputedStyle(frame.frameElement).borderTopWidth;
      scrollX.value += rect.left + parseInt(left, 10);
      scrollY.value += rect.top + parseInt(top, 10);
    }
    let rect = Rect.fromRect(selection.getRangeAt(0).getBoundingClientRect());
    return rect.translate(scrollX.value, scrollY.value);
  },

  _outlineLink(aDrawOutline) {
    let foundLink = this._fastFind.foundLink;

    // Optimization: We are drawing outlines and we matched
    // the same link before, so don't duplicate work.
    if (foundLink == this._previousLink && aDrawOutline) {
      return;
    }

    this._restoreOriginalOutline();

    if (foundLink && aDrawOutline) {
      // Backup original outline
      this._tmpOutline = foundLink.style.outline;
      this._tmpOutlineOffset = foundLink.style.outlineOffset;

      // Draw pseudo focus rect
      // XXX Should we change the following style for FAYT pseudo focus?
      // XXX Shouldn't we change default design if outline is visible
      //     already?
      // Don't set the outline-color, we should always use initial value.
      foundLink.style.outline = "1px dotted";
      foundLink.style.outlineOffset = "0";

      this._previousLink = foundLink;
    }
  },

  _restoreOriginalOutline() {
    // Removes the outline around the last found link.
    if (this._previousLink) {
      this._previousLink.style.outline = this._tmpOutline;
      this._previousLink.style.outlineOffset = this._tmpOutlineOffset;
      this._previousLink = null;
    }
  },

  _getSelectionController(aWindow) {
    // display: none iframes don't have a selection controller, see bug 493658
    try {
      if (!aWindow.innerWidth || !aWindow.innerHeight) {
        return null;
      }
    } catch (e) {
      // If getting innerWidth or innerHeight throws, we can't get a selection
      // controller.
      return null;
    }

    // Yuck. See bug 138068.
    let docShell = aWindow.docShell;

    let controller = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsISelectionDisplay)
      .QueryInterface(Ci.nsISelectionController);
    return controller;
  },

  // Start of nsIWebProgressListener implementation.

  onLocationChange(aWebProgress, aRequest, aLocation, aFlags) {
    if (!aWebProgress.isTopLevel) {
      return;
    }
    // Ignore events that don't change the document.
    if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      return;
    }

    // Avoid leaking if we change the page.
    this._lastFindResult = this._previousLink = this._currentFoundRange = null;
    this.highlighter.onLocationChange();
    this.iterator.reset();
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]),
};

export function GetClipboardSearchString(aLoadContext) {
  let searchString = "";
  if (
    !Services.clipboard.isClipboardTypeSupported(
      Services.clipboard.kFindClipboard
    )
  ) {
    return searchString;
  }

  try {
    let trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    trans.init(aLoadContext);
    trans.addDataFlavor("text/plain");

    Services.clipboard.getData(trans, Ci.nsIClipboard.kFindClipboard);

    let data = {};
    trans.getTransferData("text/plain", data);
    if (data.value) {
      data = data.value.QueryInterface(Ci.nsISupportsString);
      searchString = data.toString();
    }
  } catch (ex) {}

  return searchString;
}

export function SetClipboardSearchString(aSearchString) {
  if (
    !aSearchString ||
    !Services.clipboard.isClipboardTypeSupported(
      Services.clipboard.kFindClipboard
    )
  ) {
    return;
  }

  lazy.ClipboardHelper.copyStringToClipboard(
    aSearchString,
    Ci.nsIClipboard.kFindClipboard
  );
}
