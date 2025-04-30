/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  Finder: "resource://gre/modules/Finder.sys.mjs",
  FinderHighlighter: "resource://gre/modules/FinderHighlighter.sys.mjs",
  FinderIterator: "resource://gre/modules/FinderIterator.sys.mjs",
});

export class FindContent {
  constructor(docShell) {
    this.finder = new lazy.Finder(docShell);
  }

  get iterator() {
    if (!this._iterator) {
      this._iterator = new lazy.FinderIterator();
    }
    return this._iterator;
  }

  get highlighter() {
    if (!this._highlighter) {
      this._highlighter = new lazy.FinderHighlighter(this.finder, true);
    }
    return this._highlighter;
  }

  /**
   * findRanges
   *
   * Performs a search which will cache found ranges in `iterator._previousRanges`.  Cached
   * data can then be used by `highlightResults`, `_collectRectData` and `_serializeRangeData`.
   *
   * @param {object} params - the params.
   * @param {string} params.queryphrase - the text to search for.
   * @param {boolean} params.caseSensitive - whether to use case sensitive matches.
   * @param {boolean} params.includeRangeData - whether to collect and return range data.
   * @param {boolean} params.matchDiacritics - whether diacritics must match.
   * @param {boolean} params.searchString - whether to collect and return rect data.
   * @param {boolean} params.entireWord - whether to match entire words.
   * @param {boolean} params.includeRectData - collect and return rect data.
   *
   * @returns {object} that includes:
   *   {number} count - number of results found.
   *   {array} rangeData (if opted) - serialized representation of ranges found.
   *   {array} rectData (if opted) - rect data of ranges found.
   */
  findRanges(params) {
    return new Promise(resolve => {
      let {
        queryphrase,
        caseSensitive,
        entireWord,
        includeRangeData,
        includeRectData,
        matchDiacritics,
      } = params;

      this.iterator.reset();

      // Cast `caseSensitive` and `entireWord` to boolean, otherwise _iterator.start will throw.
      let iteratorPromise = this.iterator.start({
        word: queryphrase,
        caseSensitive: !!caseSensitive,
        entireWord: !!entireWord,
        finder: this.finder,
        listener: this.finder,
        matchDiacritics: !!matchDiacritics,
        useSubFrames: false,
      });

      iteratorPromise.then(() => {
        let rangeData;
        let rectData;
        if (includeRangeData) {
          rangeData = this._serializeRangeData();
        }
        if (includeRectData) {
          rectData = this._collectRectData();
        }

        resolve({
          count: this.iterator._previousRanges.length,
          rangeData,
          rectData,
        });
      });
    });
  }

  /**
   * _serializeRangeData
   *
   * Optionally returned by `findRanges`.
   * Collects DOM data from ranges found on the most recent search made by `findRanges`
   * and encodes it into a serializable form.  Useful to extensions for custom UI presentation
   * of search results, eg, getting surrounding context of results.
   *
   * @returns {Array} - serializable range data.
   */
  _serializeRangeData() {
    let ranges = this.iterator._previousRanges;

    let rangeData = [];
    let nodeCountWin = 0;
    let lastDoc;
    let walker;
    let node;

    for (let range of ranges) {
      let startContainer = range.startContainer;
      let doc = startContainer.ownerDocument;

      if (lastDoc !== doc) {
        walker = doc.createTreeWalker(
          doc,
          doc.defaultView.NodeFilter.SHOW_TEXT,
          null,
          false
        );
        // Get first node.
        node = walker.nextNode();
        // Reset node count.
        nodeCountWin = 0;
      }
      lastDoc = doc;

      // The framePos will be set by the parent process later.
      let data = { framePos: 0, text: range.toString() };
      rangeData.push(data);

      if (node != range.startContainer) {
        node = walker.nextNode();
        while (node) {
          nodeCountWin++;
          if (node == range.startContainer) {
            break;
          }
          node = walker.nextNode();
        }
      }
      data.startTextNodePos = nodeCountWin;
      data.startOffset = range.startOffset;

      if (range.startContainer != range.endContainer) {
        node = walker.nextNode();
        while (node) {
          nodeCountWin++;
          if (node == range.endContainer) {
            break;
          }
          node = walker.nextNode();
        }
      }
      data.endTextNodePos = nodeCountWin;
      data.endOffset = range.endOffset;
    }

    return rangeData;
  }

  /**
   * _collectRectData
   *
   * Optionally returned by `findRanges`.
   * Collects rect data of ranges found by most recent search made by `findRanges`.
   * Useful to extensions for custom highlighting of search results.
   *
   * @returns {Array} rectData - serializable rect data.
   */
  _collectRectData() {
    let rectData = [];

    let ranges = this.iterator._previousRanges;
    for (let range of ranges) {
      let rectsAndTexts = this.highlighter._getRangeRectsAndTexts(range);
      rectData.push({ text: range.toString(), rectsAndTexts });
    }

    return rectData;
  }

  /**
   * highlightResults
   *
   * Highlights range(s) found in previous browser.find.find.
   *
   * @param {object} params - may contain any of the following properties:
   *   all of which are optional:
   *   {number} rangeIndex -
   *            Found range to be highlighted held in API's ranges array for the tabId.
   *            Default highlights all ranges.
   *   {number} tabId - Tab to highlight.  Defaults to the active tab.
   *   {boolean} noScroll - Don't scroll to highlighted item.
   *
   * @returns {string} - a string describing the resulting status of the highlighting,
   *   which will be used as criteria for resolving or rejecting the promise.
   *   This can be:
   *   "Success" - Highlighting succeeded.
   *   "OutOfRange" - The index supplied was out of range.
   *   "NoResults" - There were no search results to highlight.
   */
  highlightResults(params) {
    let { rangeIndex, noScroll } = params;

    this.highlighter.highlight(false);
    let ranges = this.iterator._previousRanges;

    let status = "Success";

    if (ranges.length) {
      if (typeof rangeIndex == "number") {
        if (rangeIndex < ranges.length) {
          let foundRange = ranges[rangeIndex];
          this.highlighter.highlightRange(foundRange);

          if (!noScroll) {
            let node = foundRange.startContainer;
            let editableNode = this.highlighter._getEditableNode(node);
            let controller = editableNode
              ? editableNode.editor.selectionController
              : this.finder._getSelectionController(node.ownerGlobal);

            controller.scrollSelectionIntoView(
              controller.SELECTION_FIND,
              controller.SELECTION_ON,
              controller.SCROLL_VERTICAL_CENTER
            );
          }
        } else {
          status = "OutOfRange";
        }
      } else {
        for (let range of ranges) {
          this.highlighter.highlightRange(range);
        }
      }
    } else {
      status = "NoResults";
    }

    return status;
  }
}
