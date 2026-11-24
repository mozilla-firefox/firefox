/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Color: "resource://gre/modules/Color.sys.mjs",
  Rect: "resource://gre/modules/Geometry.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "kDebug", () => {
  const kDebugPref = "findbar.modalHighlight.debug";
  return (
    Services.prefs.getPrefType(kDebugPref) &&
    Services.prefs.getBoolPref(kDebugPref)
  );
});

const kContentChangeThresholdPx = 5;
const kBrightTextSampleSize = 5;
// This limit is arbitrary and doesn't scale for low-powered machines or
// high-powered machines. Netbooks will probably need a much lower limit, for
// example. Though getting something out there is better than nothing.
const kPageIsTooBigPx = 500000;
const kModalHighlightRepaintLoFreqMs = 100;
const kModalHighlightRepaintHiFreqMs = 16;
const kHighlightAllPref = "findbar.highlightAll";
const kModalHighlightPref = "findbar.modalHighlight";
const kFontPropsCSS = [
  "color",
  "font-family",
  "font-kerning",
  "font-size",
  "font-size-adjust",
  "font-stretch",
  "font-variant",
  "font-weight",
  "line-height",
  "letter-spacing",
  "text-emphasis",
  "text-orientation",
  "text-transform",
  "word-spacing",
];
const kFontPropsCamelCase = kFontPropsCSS.map(prop => {
  let parts = prop.split("-");
  return (
    parts.shift() +
    parts.map(part => part.charAt(0).toUpperCase() + part.slice(1)).join("")
  );
});
const kRGBRE = /^rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*/i;
// This uuid is used to prefix HTML element IDs in order to make them unique and
// hard to clash with IDs content authors come up with.
const kModalIdPrefix = "cedee4d0-74c5-4f2d-ab43-4d37c0f9d463";
const kModalOutlineId = kModalIdPrefix + "-findbar-modalHighlight-outline";
const kOutlineBoxColor = "255,197,53";
const kOutlineBoxBorderSize = 1;
const kOutlineBoxBorderRadius = 2;
const kModalStyles = {
  outlineNode: [
    ["background-color", `rgb(${kOutlineBoxColor})`],
    ["background-clip", "padding-box"],
    ["border", `${kOutlineBoxBorderSize}px solid rgba(${kOutlineBoxColor},.7)`],
    ["border-radius", `${kOutlineBoxBorderRadius}px`],
    ["box-shadow", `0 2px 0 0 rgba(0,0,0,.1)`],
    ["color", "#000"],
    ["display", "flex"],
    [
      "margin",
      `-${kOutlineBoxBorderSize}px 0 0 -${kOutlineBoxBorderSize}px !important`,
    ],
    ["overflow", "hidden"],
    ["pointer-events", "none"],
    ["position", "absolute"],
    ["white-space", "nowrap"],
    ["will-change", "transform"],
    ["z-index", 2],
  ],
  outlineNodeDebug: [["z-index", 2147483647]],
  outlineText: [
    ["margin", "0 !important"],
    ["padding", "0 !important"],
    ["vertical-align", "top !important"],
  ],
  maskNode: [
    ["background", "rgba(0,0,0,.25)"],
    ["pointer-events", "none"],
    ["position", "absolute"],
    ["z-index", 1],
  ],
  maskNodeTransition: [["transition", "background .2s ease-in"]],
  maskNodeDebug: [
    ["z-index", 2147483646],
    ["top", 0],
    ["left", 0],
  ],
  maskNodeBrightText: [["background", "rgba(255,255,255,.25)"]],
};
const kModalOutlineAnim = {
  keyframes: [
    { transform: "scaleX(1) scaleY(1)" },
    { transform: "scaleX(1.5) scaleY(1.5)", offset: 0.5, easing: "ease-in" },
    { transform: "scaleX(1) scaleY(1)" },
  ],
  duration: 50,
};
const kNSHTML = "http://www.w3.org/1999/xhtml";
const kRepaintSchedulerStopped = 1;
const kRepaintSchedulerPaused = 2;
const kRepaintSchedulerRunning = 3;

function mockAnonymousContentNode(domNode) {
  return {
    setTextContentForElement(id, text) {
      (domNode.querySelector("#" + id) || domNode).textContent = text;
    },
    getAttributeForElement(id, attrName) {
      let node = domNode.querySelector("#" + id) || domNode;
      if (!node.hasAttribute(attrName)) {
        return undefined;
      }
      return node.getAttribute(attrName);
    },
    setAttributeForElement(id, attrName, attrValue) {
      (domNode.querySelector("#" + id) || domNode).setAttribute(
        attrName,
        attrValue
      );
    },
    removeAttributeForElement(id, attrName) {
      let node = domNode.querySelector("#" + id) || domNode;
      if (!node.hasAttribute(attrName)) {
        return;
      }
      node.removeAttribute(attrName);
    },
    remove() {
      try {
        domNode.remove();
      } catch (ex) {}
    },
    setAnimationForElement(id, keyframes, duration) {
      return (domNode.querySelector("#" + id) || domNode).animate(
        keyframes,
        duration
      );
    },
    setCutoutRectsForElement() {
      // no-op for now.
    },
  };
}

let gWindows = new WeakMap();

/**
 * FinderHighlighter class that is used by Finder.sys.mjs to take care of the
 * 'Highlight All' feature, which can highlight all find occurrences in a page.
 *
 * @param {Finder} finder Finder.sys.mjs instance
 * @param {boolean} useTop check and use top-level windows for rectangle
 *                         computation, if possible.
 */
export function FinderHighlighter(finder, useTop = false) {
  this._highlightAll = Services.prefs.getBoolPref(kHighlightAllPref);
  this._modal = Services.prefs.getBoolPref(kModalHighlightPref);
  this._useSubFrames = false;
  this._useTop = useTop;
  this._marksListener = null;
  this._testing = false;
  this.finder = finder;
}

FinderHighlighter.prototype = {
  get iterator() {
    return this.finder.iterator;
  },

  enableTesting(enable) {
    this._testing = enable;
  },

  // Get the top-most window when allowed. When out-of-process frames are used,
  // this will usually be the same as the passed-in window. The checkUseTop
  // argument can be used to instead check the _useTop flag which can be used
  // to enable rectangle coordinate checks.
  getTopWindow(window, checkUseTop) {
    if (this._useSubFrames || (checkUseTop && this._useTop)) {
      try {
        return window.top;
      } catch (ex) {}
    }

    return window;
  },

  useModal() {
    // Modal highlighting is currently only enabled when there are no
    // out-of-process subframes.
    return this._modal && this._useSubFrames;
  },

  /**
   * Each window is unique, globally, and the relation between an active
   * highlighting session and a window is 1:1.
   * For each window we track a number of properties which _at least_ consist of
   *  - {Boolean} detectedGeometryChange Whether the geometry of the found ranges'
   *                                     rectangles has changed substantially
   *  - {Set}     dynamicRangesSet       Set of ranges that may move around, depending
   *                                     on page layout changes and user input
   *  - {Map}     frames                 Collection of frames that were encountered
   *                                     when inspecting the found ranges
   *  - {Map}     modalHighlightRectsMap Collection of ranges and their corresponding
   *                                     Rects and texts
   *
   * @param  {nsIDOMWindow} window
   * @return {object}
   */
  getForWindow(window) {
    if (!gWindows.has(window)) {
      gWindows.set(window, {
        detectedGeometryChange: false,
        dynamicRangesSet: new Set(),
        frames: new Map(),
        lastWindowDimensions: { width: 0, height: 0 },
        modalHighlightRectsMap: new Map(),
        previousRangeRectsAndTexts: { rectList: [], textList: [] },
        repaintSchedulerState: kRepaintSchedulerStopped,
      });
    }
    return gWindows.get(window);
  },

  /**
   * Notify all registered listeners that the 'Highlight All' operation finished.
   *
   * @param {boolean} highlight Whether highlighting was turned on
   */
  notifyFinished(highlight) {
    for (let l of this.finder._listeners) {
      try {
        l.onHighlightFinished(highlight);
      } catch (ex) {}
    }
  },

  /**
   * Toggle highlighting all occurrences of a word in a page. This method will
   * be called recursively for each (i)frame inside a page.
   *
   * @param {Booolean} highlight      Whether highlighting should be turned on
   * @param {string}   [word]         Needle to search for and highlight when found
   * @param {boolean}  [linksOnly]    Only consider nodes that are links for the search
   * @param {boolean}  [drawOutline]  Whether found links should be outlined.
   * @param {boolean}  [useSubFrames] Whether to iterate over subframes.
   * @yield {Promise}  that resolves once the operation has finished
   */
  async highlight(highlight, word, linksOnly, drawOutline, useSubFrames) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    let controller = this.finder._getSelectionController(window);
    let doc = window.document;
    this._found = false;
    this._useSubFrames = useSubFrames;

    let result = { searchString: word, highlight, found: false };

    if (!controller || !doc || !doc.documentElement) {
      // Without the selection controller,
      // we are unable to (un)highlight any matches
      return result;
    }

    if (highlight) {
      let params = {
        allowDistance: 1,
        caseSensitive: this.finder._fastFind.caseSensitive,
        entireWord: this.finder._fastFind.entireWord,
        linksOnly,
        word,
        finder: this.finder,
        listener: this,
        matchDiacritics: this.finder._fastFind.matchDiacritics,
        useCache: true,
        useSubFrames,
        window,
      };
      if (
        this.iterator.isAlreadyRunning(params) ||
        (this.useModal() &&
          this.iterator._areParamsEqual(params, dict.lastIteratorParams))
      ) {
        return result;
      }

      if (!this.useModal()) {
        dict.visible = true;
      }
      await this.iterator.start(params);
      if (this._found) {
        this.finder._outlineLink(drawOutline);
      }
    } else {
      this.hide(window);

      // Removing the highlighting always succeeds, so return true.
      this._found = true;
    }

    result.found = this._found;
    this.notifyFinished(result);
    return result;
  },

  // FinderIterator listener implementation

  onIteratorRangeFound(range) {
    this.highlightRange(range);
    this._found = true;
  },

  onIteratorReset() {},

  onIteratorRestart() {
    this.clear(this.finder._getWindow());
  },

  onIteratorStart(params) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    // Save a clean params set for use later in the `update()` method.
    dict.lastIteratorParams = params;
    if (!this.useModal()) {
      this.hide(window, !!this.finder._fastFind.getFoundRange());
    }
    this.clear(window);
  },

  /**
   * Add a range to the find selection, i.e. highlight it, and if it's inside an
   * editable node, track it.
   *
   * @param {Range} range Range object to be highlighted
   */
  highlightRange(range) {
    let node = range.startContainer;
    let editableNode = this._getEditableNode(node);
    let window = node.documentGlobal;
    let controller = this.finder._getSelectionController(window);
    if (editableNode) {
      controller = editableNode.editor.selectionController;
    }

    if (this.useModal()) {
      this._modalHighlight(range, controller, window);
    } else {
      let findSelection = controller.getSelection(
        Ci.nsISelectionController.SELECTION_FIND
      );
      findSelection.addRange(range);
      // Check if the range is inside an (i)frame.
      if (window != this.getTopWindow(window)) {
        let dict = this.getForWindow(this.getTopWindow(window));
        // Add this frame to the list, so that we'll be able to find it later
        // when we need to clear its selection(s).
        dict.frames.set(window, {});
      }
    }

    if (editableNode) {
      // Highlighting added, so cache this editor, and hook up listeners
      // to ensure we deal properly with edits within the highlighting
      this._addEditorListeners(editableNode.editor);
    }
  },

  /**
   * If modal highlighting is enabled, show the dimmed background that will overlay
   * the page.
   *
   * @param {nsIDOMWindow} window The dimmed background will overlay this window.
   *                              Optional, defaults to the finder window.
   */
  show(window = null) {
    window = this.getTopWindow(window || this.finder._getWindow());
    let dict = this.getForWindow(window);
    if (!this.useModal() || dict.visible) {
      return;
    }

    dict.visible = true;

    this._maybeCreateModalHighlightNodes(window);
    this._addModalHighlightListeners(window);
  },

  /**
   * Clear all highlighted matches. If modal highlighting is enabled and
   * the outline + dimmed background is currently visible, both will be hidden.
   *
   * @param {nsIDOMWindow} window The dimmed background will overlay this window.
   *                             Optional, defaults to the finder window.
   * @param {boolean} displayAttention Whether to keep the current find result
   *                                   displayed in attention mode.
   * @param {Event} event When called from an event handler, this will be the
   *                     triggering event.
   */
  hide(window, displayAttention = false, event = null) {
    try {
      window = this.getTopWindow(window);
    } catch (ex) {
      console.error(ex);
      return;
    }
    let dict = this.getForWindow(window);

    let isBusySelecting = dict.busySelecting;
    dict.busySelecting = false;
    // Do not hide on anything but a left-click.
    if (
      event &&
      event.type == "click" &&
      (event.button !== 0 ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.shiftKey ||
        event.relatedTarget ||
        isBusySelecting ||
        (event.target.localName == "a" && event.target.href))
    ) {
      return;
    }

    this._clearSelection(
      this.finder._getSelectionController(window),
      displayAttention
    );
    for (let frame of dict.frames.keys()) {
      this._clearSelection(
        this.finder._getSelectionController(frame),
        displayAttention
      );
    }

    // Next, check our editor cache, for editors belonging to this
    // document
    if (this._editors) {
      let doc = window.document;
      for (let x = this._editors.length - 1; x >= 0; --x) {
        if (this._editors[x].document == doc) {
          this._clearSelection(
            this._editors[x].selectionController,
            displayAttention
          );
          // We don't need to listen to this editor any more
          this._unhookListenersAtIndex(x);
        }
      }
    }

    if (dict.modalRepaintScheduler) {
      window.clearTimeout(dict.modalRepaintScheduler);
      dict.modalRepaintScheduler = null;
      dict.repaintSchedulerState = kRepaintSchedulerStopped;
    }
    dict.lastWindowDimensions = { width: 0, height: 0 };

    this._removeRangeOutline(window);
    this._removeHighlightAllMask(window);
    this._removeModalHighlightListeners(window);

    dict.visible = false;
  },

  /**
   * Called by the Finder after a find result comes in; update the position and
   * content of the outline to the newly found occurrence.
   * To make sure that the outline covers the found range completely, all the
   * CSS styles that influence the text are copied and applied to the outline.
   *
   * @param {object} data Dictionary coming from Finder that contains the
   *                      following properties:
   *   {Number}  result        One of the nsITypeAheadFind.FIND_* constants
   *                           indicating the result of a search operation.
   *   {Boolean} findBackwards If TRUE, the search was performed backwards,
   *                           FALSE if forwards.
   *   {Boolean} findAgain     If TRUE, the search was performed using the same
   *                           search string as before.
   *   {String}  linkURL       If a link was hit, this will contain a URL string.
   *   {Rect}    rect          An object with top, left, width and height
   *                           coordinates of the current selection.
   *   {String}  searchString  The string the search was performed with.
   *   {Boolean} storeResult   Indicator if the search string should be stored
   *                           by the consumer of the Finder.
   */
  async update(data, foundInThisFrame) {
    let window = this.finder._getWindow();
    let dict = this.getForWindow(window);
    let foundRange = this.finder._fastFind.getFoundRange();

    if (
      data.result == Ci.nsITypeAheadFind.FIND_NOTFOUND ||
      !data.searchString ||
      (foundInThisFrame && !foundRange)
    ) {
      this.hide(window);
      return;
    }

    this._useSubFrames = data.useSubFrames;
    if (!this.useModal()) {
      if (this._highlightAll) {
        dict.previousFoundRange = dict.currentFoundRange;
        dict.currentFoundRange = foundRange;
        let params = this.iterator.params;
        if (
          dict.visible &&
          this.iterator._areParamsEqual(params, dict.lastIteratorParams)
        ) {
          return;
        }
        if (!dict.visible && !params) {
          params = { word: data.searchString, linksOnly: data.linksOnly };
        }
        if (params) {
          await this.highlight(
            true,
            params.word,
            params.linksOnly,
            params.drawOutline,
            data.useSubFrames
          );
        }
      }
      return;
    }

    dict.animateOutline = true;
    // Immediately finish running animations, if any.
    this._finishOutlineAnimations(dict);

    if (foundRange !== dict.currentFoundRange || data.findAgain) {
      dict.previousFoundRange = dict.currentFoundRange;
      dict.currentFoundRange = foundRange;

      if (!dict.visible) {
        this.show(window);
      } else {
        this._maybeCreateModalHighlightNodes(window);
      }
    }

    if (this._highlightAll) {
      await this.highlight(
        true,
        data.searchString,
        data.linksOnly,
        data.drawOutline,
        data.useSubFrames
      );
    }
  },

  /**
   * Invalidates the list by clearing the map of highlighted ranges that we
   * keep to build the mask for.
   */
  clear(window = null) {
    if (!window || !this.getTopWindow(window)) {
      return;
    }

    let dict = this.getForWindow(this.getTopWindow(window));
    this._finishOutlineAnimations(dict);
    dict.dynamicRangesSet.clear();
    dict.frames.clear();
    dict.modalHighlightRectsMap.clear();
    dict.brightText = null;
  },

  /**
   * Removes the outline from a single window. This is done when
   * switching the current search to a new frame.
   */
  clearCurrentOutline(window = null) {
    let dict = this.getForWindow(this.getTopWindow(window));
    this._finishOutlineAnimations(dict);
    this._removeRangeOutline(window);
  },

  // Update the tick marks that should appear on the page's scrollbar(s).
  updateScrollMarks() {
    // Only show scrollbar marks when normal highlighting is enabled.
    if (this.useModal() || !this._highlightAll) {
      this.removeScrollMarks();
      return;
    }

    let marks = new Set(); // Use a set so duplicate values are removed.
    let window = this.finder._getWindow();
    // Show the marks on the horizontal scrollbar for vertical writing modes.
    let onHorizontalScrollbar = !window
      .getComputedStyle(window.document.body || window.document.documentElement)
      .writingMode.startsWith("horizontal");
    let yStart = window.scrollY - window.scrollMinY;
    let xStart = window.scrollX - window.scrollMinX;
    let pageDimensions = null;

    let hasRanges = false;
    if (window) {
      let controllers = [this.finder._getSelectionController(window)];
      let editors = this._editors;
      if (editors) {
        // Add the selection controllers from any input fields.
        controllers.push(...editors.map(editor => editor.selectionController));
      }

      for (let controller of controllers) {
        let findSelection = controller.getSelection(
          Ci.nsISelectionController.SELECTION_FIND
        );

        let rangeCount = findSelection.rangeCount;
        if (!rangeCount) {
          continue;
        }
        hasRanges = true;

        // No need to calculate the mark positions if there is no visible scrollbar.
        if (window.scrollMaxY > window.scrollMinY && !onHorizontalScrollbar) {
          pageDimensions ??= this._getWindowDimensions(window);
          let yAdj =
            (window.scrollMaxY - window.scrollMinY) / pageDimensions.height;

          for (let r = 0; r < rangeCount; r++) {
            let rect = findSelection.getRangeAt(r).getBoundingClientRect();
            let yPos = Math.round((yStart + rect.y + rect.height / 2) * yAdj); // use the midpoint
            marks.add(yPos);
          }
        } else if (
          window.scrollMaxX > window.scrollMinX &&
          onHorizontalScrollbar
        ) {
          pageDimensions ??= this._getWindowDimensions(window);
          let xAdj =
            (window.scrollMaxX - window.scrollMinX) / pageDimensions.width;

          for (let r = 0; r < rangeCount; r++) {
            let rect = findSelection.getRangeAt(r).getBoundingClientRect();
            let xPos = Math.round((xStart + rect.x + rect.width / 2) * xAdj);
            marks.add(xPos);
          }
        }
      }
    }

    if (hasRanges) {
      // Assign the marks to the window and add a listener for the MozScrolledAreaChanged
      // event which fires whenever the scrollable area's size is updated.
      this.setScrollMarks(window, Array.from(marks), onHorizontalScrollbar);

      if (!this._marksListener) {
        this._marksListener = () => {
          this.updateScrollMarks();
        };

        window.addEventListener(
          "MozScrolledAreaChanged",
          this._marksListener,
          true
        );
        window.addEventListener("resize", this._marksListener);
      }
    } else if (this._marksListener) {
      // No results were found so remove any existing ones and the MozScrolledAreaChanged listener.
      this.removeScrollMarks();
    }
  },

  removeScrollMarks() {
    let window;
    try {
      window = this.finder._getWindow();
    } catch (ex) {
      // An exception can happen after changing remoteness but this
      // would have deleted the marks anyway.
      return;
    }

    if (this._marksListener) {
      window.removeEventListener(
        "MozScrolledAreaChanged",
        this._marksListener,
        true
      );
      window.removeEventListener("resize", this._marksListener);
      this._marksListener = null;
    }
    this.setScrollMarks(window, []);
  },

  /**
   * Set the scrollbar marks for a current search. If testing mode is enabled, fire a
   * find-scrollmarks-changed event at the window.
   *
   * @param window window to set the scrollbar marks on
   * @param marks array of integer scrollbar mark positions
   * @param onHorizontalScrollbar whether to display the marks on the horizontal scrollbar
   */
  setScrollMarks(window, marks, onHorizontalScrollbar = false) {
    window.setScrollMarks(marks, onHorizontalScrollbar);

    // Fire an event containing the found mark values if testing mode is enabled.
    if (this._testing) {
      window.dispatchEvent(
        new CustomEvent("find-scrollmarks-changed", {
          detail: {
            marks: Array.from(marks),
            onHorizontalScrollbar,
          },
        })
      );
    }
  },

  /**
   * When the current page is refreshed or navigated away from, the CanvasFrame
   * contents is not valid anymore, i.e. all anonymous content is destroyed.
   * We need to clear the references we keep, which'll make sure we redraw
   * everything when the user starts to find in page again.
   */
  onLocationChange() {
    let window = this.finder._getWindow();
    if (!window || !this.getTopWindow(window)) {
      return;
    }
    this.hide(window);
    this.clear(window);
    this._removeRangeOutline(window);

    gWindows.delete(this.getTopWindow(window));
  },

  /**
   * When `kModalHighlightPref` pref changed during a session, this callback is
   * invoked. When modal highlighting is turned off, we hide the CanvasFrame
   * contents.
   *
   * @param {boolean} useModalHighlight
   */
  onModalHighlightChange(useModalHighlight) {
    let window = this.finder._getWindow();
    if (window && this.useModal() && !useModalHighlight) {
      this.hide(window);
      this.clear(window);
    }
    this._modal = useModalHighlight;
    this.updateScrollMarks();
  },

  /**
   * When 'Highlight All' is toggled during a session, this callback is invoked
   * and when it's turned off, the found occurrences will be removed from the mask.
   *
   * @param {boolean} highlightAll
   */
  onHighlightAllChange(highlightAll) {
    this._highlightAll = highlightAll;
    if (!highlightAll) {
      let window = this.finder._getWindow();
      if (!this.useModal()) {
        this.hide(window);
      }
      this.clear(window);
      this._scheduleRepaintOfMask(window);
    }

    this.updateScrollMarks();
  },

  /**
   * Utility; removes all ranges from the find selection that belongs to a
   * controller. Optionally keeps the normal selection, which holds the current
   * find result, displayed as such.
   *
   * @param  {nsISelectionController} controller
   * @param  {boolean} displayAttention Whether to display the normal selection
   *                                    in attention mode.
   */
  _clearSelection(controller, displayAttention = false) {
    if (!controller) {
      return;
    }
    let sel = controller.getSelection(Ci.nsISelectionController.SELECTION_FIND);
    sel.removeAllRanges();
    if (displayAttention) {
      controller.setDisplaySelection(
        Ci.nsISelectionController.SELECTION_ATTENTION
      );
      controller.repaintSelection(Ci.nsISelectionController.SELECTION_NORMAL);
    }
  },

  /**
   * Utility; get the nsIDOMWindowUtils for a window.
   *
   * @param  {nsIDOMWindow} window Optional, defaults to the finder window.
   * @return {nsIDOMWindowUtils}
   */
  _getDWU(window = null) {
    return (window || this.finder._getWindow()).windowUtils;
  },

  /**
   * Utility; returns the bounds of the page relative to the viewport.
   * If the pages is part of a frameset or inside an iframe of any kind, its
   * offset is accounted for.
   * Geometry.sys.mjs takes care of the DOMRect calculations.
   *
   * @param  {nsIDOMWindow} window          Window to read the boundary rect from
   * @param  {boolean}      [includeScroll] Whether to ignore the scroll offset,
   *                                        which is useful for comparing DOMRects.
   *                                        Optional, defaults to `true`
   * @return {Rect}
   */
  _getRootBounds(window, includeScroll = true) {
    let dwu = this._getDWU(this.getTopWindow(window, true));
    let cssPageRect = lazy.Rect.fromRect(dwu.getRootBounds());
    let scrollX = {};
    let scrollY = {};
    if (includeScroll && window == this.getTopWindow(window, true)) {
      dwu.getScrollXY(false, scrollX, scrollY);
      cssPageRect.translate(scrollX.value, scrollY.value);
    }

    // If we're in a frame, update the position of the rect (top/ left).
    let currWin = window;
    while (currWin != this.getTopWindow(window, true)) {
      let frameOffsets = this._getFrameElementOffsets(currWin);
      cssPageRect.translate(frameOffsets.x, frameOffsets.y);

      // Since the frame is an element inside a parent window, we'd like to
      // learn its position relative to it.
      let el = currWin.browsingContext.embedderElement;
      currWin = currWin.parent;
      dwu = this._getDWU(currWin);
      let parentRect = lazy.Rect.fromRect(dwu.getBoundsWithoutFlushing(el));

      if (includeScroll) {
        dwu.getScrollXY(false, scrollX, scrollY);
        parentRect.translate(scrollX.value, scrollY.value);
        // If the current window is an iframe with scrolling="no" and its parent
        // is also an iframe the scroll offsets from the parents' documentElement
        // (inverse scroll position) needs to be subtracted from the parent
        // window rect.
        if (
          el.getAttribute("scrolling") == "no" &&
          currWin != this.getTopWindow(window, true)
        ) {
          let docEl = currWin.document.documentElement;
          parentRect.translate(-docEl.scrollLeft, -docEl.scrollTop);
        }
      }

      cssPageRect.translate(parentRect.left, parentRect.top);
    }
    let frameOffsets = this._getFrameElementOffsets(currWin);
    cssPageRect.translate(frameOffsets.x, frameOffsets.y);

    return cssPageRect;
  },

  /**
   * (I)Frame elements may have a border and/ or padding set, which is not
   * included in the bounds returned by nsDOMWindowUtils#getRootBounds() for the
   * window it hosts.
   * This method fetches this offset of the frame element to the respective window.
   *
   * @param  {nsIDOMWindow} window          Window to read the boundary rect from
   * @return {object}       Simple object that contains the following two properties:
   *                        - {Number} x Offset along the horizontal axis.
   *                        - {Number} y Offset along the vertical axis.
   */
  _getFrameElementOffsets(window) {
    let frame = window.frameElement;
    if (!frame) {
      return { x: 0, y: 0 };
    }

    // Getting style info is super expensive, causing reflows, so let's cache
    // frame border widths and padding values aggressively.
    let dict = this.getForWindow(this.getTopWindow(window, true));
    let frameData = dict.frames.get(window);
    if (!frameData) {
      dict.frames.set(window, (frameData = {}));
    }
    if (frameData.offset) {
      return frameData.offset;
    }

    let style = frame.documentGlobal.getComputedStyle(frame);
    // We only need to left sides, because ranges are offset from point 0,0 in
    // the top-left corner.
    let borderOffset = [
      parseInt(style.borderLeftWidth, 10) || 0,
      parseInt(style.borderTopWidth, 10) || 0,
    ];
    let paddingOffset = [
      parseInt(style.paddingLeft, 10) || 0,
      parseInt(style.paddingTop, 10) || 0,
    ];
    return (frameData.offset = {
      x: borderOffset[0] + paddingOffset[0],
      y: borderOffset[1] + paddingOffset[1],
    });
  },

  /**
   * Utility; fetch the full width and height of the current window, excluding
   * scrollbars.
   *
   * @param  {nsiDOMWindow} window The current finder window.
   * @return {object} The current full page dimensions with `width` and `height`
   *                  properties
   */
  _getWindowDimensions(window) {
    // First we'll try without flushing layout, because it's way faster.
    let dwu = this._getDWU(window);
    let { width, height } = dwu.getRootBounds();

    if (!width || !height) {
      // We need a flush after all :'(
      width = window.innerWidth + window.scrollMaxX - window.scrollMinX;
      height = window.innerHeight + window.scrollMaxY - window.scrollMinY;

      let scrollbarHeight = {};
      let scrollbarWidth = {};
      dwu.getScrollbarSize(false, scrollbarWidth, scrollbarHeight);
      width -= scrollbarWidth.value;
      height -= scrollbarHeight.value;
    }

    return { width, height };
  },

  /**
   * Utility; get all available font styles as applied to the content of a given
   * range. The CSS properties we look for can be found in `kFontPropsCSS`.
   *
   * @param  {Range} range Range to fetch style info from.
   * @return {object} Dictionary consisting of the styles that were found.
   */
  _getRangeFontStyle(range) {
    let node = range.startContainer;
    while (node.nodeType != 1) {
      node = node.parentNode;
    }
    let style = node.documentGlobal.getComputedStyle(node);
    let props = {};
    for (let prop of kFontPropsCamelCase) {
      if (prop in style && style[prop]) {
        props[prop] = style[prop];
      }
    }
    return props;
  },

  /**
   * Utility; transform a dictionary object as returned by `_getRangeFontStyle`
   * above into a HTML style attribute value.
   *
   * @param  {object} fontStyle
   * @return {string}
   */
  _getHTMLFontStyle(fontStyle) {
    let style = [];
    for (let prop of Object.getOwnPropertyNames(fontStyle)) {
      let idx = kFontPropsCamelCase.indexOf(prop);
      if (idx == -1) {
        continue;
      }
      style.push(`${kFontPropsCSS[idx]}: ${fontStyle[prop]}`);
    }
    return style.join("; ");
  },

  /**
   * Transform a style definition array as defined in `kModalStyles` into a CSS
   * string that can be used to set the 'style' property of a DOM node.
   *
   * @param  {Array}    stylePairs         Two-dimensional array of style pairs
   * @param  {...Array} [additionalStyles] Optional set of style pairs that will
   *                                       augment or override the styles defined
   *                                       by `stylePairs`
   * @return {string}
   */
  _getStyleString(stylePairs, ...additionalStyles) {
    let baseStyle = new Map(stylePairs);
    for (let additionalStyle of additionalStyles) {
      for (let [prop, value] of additionalStyle) {
        baseStyle.set(prop, value);
      }
    }
    return [...baseStyle]
      .map(([cssProp, cssVal]) => `${cssProp}: ${cssVal}`)
      .join("; ");
  },

  /**
   * Checks whether a CSS RGB color value can be classified as being 'bright'.
   *
   * @param  {string} cssColor RGB color value in the default format rgb[a](r,g,b)
   * @return {boolean}
   */
  _isColorBright(cssColor) {
    cssColor = cssColor.match(kRGBRE);
    if (!cssColor || !cssColor.length) {
      return false;
    }
    cssColor.shift();
    return !new lazy.Color(...cssColor).useBrightText;
  },

  /**
   * Detects if the overall text color in the page can be described as bright.
   * This is done according to the following algorithm:
   *  1. With the entire set of ranges that we have found thusfar;
   *  2. Get an odd-numbered `sampleSize`, with a maximum of `kBrightTextSampleSize`
   *     ranges,
   *  3. Slice the set of ranges into `sampleSize` number of equal parts,
   *  4. Grab the first range for each slice and inspect the brightness of the
   *     color of its text content.
   *  5. When the majority of ranges are counted as contain bright colored text,
   *     the page is considered to contain bright text overall.
   *
   * @param {object} dict Dictionary of properties belonging to the
   *                      currently active window. The page text color property
   *                      will be recorded in `dict.brightText` as `true` or `false`.
   */
  _detectBrightText(dict) {
    let sampleSize = Math.min(
      dict.modalHighlightRectsMap.size,
      kBrightTextSampleSize
    );
    let ranges = [...dict.modalHighlightRectsMap.keys()];
    let rangesCount = ranges.length;
    // Make sure the sample size is an odd number.
    if (sampleSize % 2 == 0) {
      // Make the previously or currently found range weigh heavier.
      if (dict.previousFoundRange || dict.currentFoundRange) {
        ranges.push(dict.previousFoundRange || dict.currentFoundRange);
        ++sampleSize;
        ++rangesCount;
      } else {
        --sampleSize;
      }
    }
    let brightCount = 0;
    for (let i = 0; i < sampleSize; ++i) {
      let range = ranges[Math.floor((rangesCount / sampleSize) * i)];
      let fontStyle = this._getRangeFontStyle(range);
      if (this._isColorBright(fontStyle.color)) {
        ++brightCount;
      }
    }

    dict.brightText = brightCount >= Math.ceil(sampleSize / 2);
  },

  /**
   * Checks if a range is inside a DOM node that's positioned in a way that it
   * doesn't scroll along when the document is scrolled and/ or zoomed. This
   * is the case for 'fixed' and 'sticky' positioned elements, elements inside
   * (i)frames and elements that have their overflow styles set to 'auto' or
   * 'scroll'.
   *
   * @param  {Range} range Range that be enclosed in a dynamic container
   * @return {boolean}
   */
  _isInDynamicContainer(range) {
    const kFixed = new Set(["fixed", "sticky", "scroll", "auto"]);
    let node = range.startContainer;
    while (node.nodeType != 1) {
      node = node.parentNode;
    }
    let document = node.ownerDocument;
    let window = document.defaultView;
    let dict = this.getForWindow(this.getTopWindow(window));

    // Check if we're in a frameset (including iframes).
    if (window != this.getTopWindow(window)) {
      if (!dict.frames.has(window)) {
        dict.frames.set(window, {});
      }
      return true;
    }

    do {
      let style = window.getComputedStyle(node);
      if (
        kFixed.has(style.position) ||
        kFixed.has(style.overflow) ||
        kFixed.has(style.overflowX) ||
        kFixed.has(style.overflowY)
      ) {
        return true;
      }
      node = node.parentNode;
    } while (node && node != document.documentElement);

    return false;
  },

  /**
   * Read and store the rectangles that encompass the entire region of a range
   * for use by the drawing function of the highlighter.
   *
   * @param  {Range}  range  Range to fetch the rectangles from
   * @param  {object} [dict] Dictionary of properties belonging to
   *                         the currently active window
   * @return {Set}    Set of rects that were found for the range
   */
  _getRangeRectsAndTexts(range, dict = null) {
    let window = range.startContainer.documentGlobal;
    let bounds;
    // If the window is part of a frameset, try to cache the bounds query.
    if (dict && dict.frames.has(window)) {
      let frameData = dict.frames.get(window);
      bounds = frameData.bounds;
      if (!bounds) {
        bounds = frameData.bounds = this._getRootBounds(window);
      }
    } else {
      bounds = this._getRootBounds(window);
    }

    let topBounds = this._getRootBounds(this.getTopWindow(window, true), false);
    let rects = [];
    // A range may consist of multiple rectangles, we can also do these kind of
    // precise cut-outs. range.getBoundingClientRect() returns the fully
    // encompassing rectangle, which is too much for our purpose here.
    let { rectList, textList } = range.getClientRectsAndTexts();
    for (let rect of rectList) {
      rect = lazy.Rect.fromRect(rect);
      rect.x += bounds.x;
      rect.y += bounds.y;
      // If the rect is not even visible from the top document, we can ignore it.
      if (rect.intersects(topBounds)) {
        rects.push(rect);
      }
    }
    return { rectList: rects, textList };
  },

  /**
   * Read and store the rectangles that encompass the entire region of a range
   * for use by the drawing function of the highlighter and store them in the
   * cache.
   *
   * @param  {Range}   range            Range to fetch the rectangles from
   * @param  {boolean} [checkIfDynamic] Whether we should check if the range
   *                                    is dynamic as per the rules in
   *                                    `_isInDynamicContainer()`. Optional,
   *                                    defaults to `true`
   * @param  {object}  [dict]           Dictionary of properties belonging to
   *                                    the currently active window
   * @return {Set}     Set of rects that were found for the range
   */
  _updateRangeRects(range, checkIfDynamic = true, dict = null) {
    let window = range.startContainer.documentGlobal;
    let rectsAndTexts = this._getRangeRectsAndTexts(range, dict);

    // Only fetch the rect at this point, if not passed in as argument.
    dict = dict || this.getForWindow(this.getTopWindow(window));
    let oldRectsAndTexts = dict.modalHighlightRectsMap.get(range);
    dict.modalHighlightRectsMap.set(range, rectsAndTexts);
    // Check here if we suddenly went down to zero rects from more than zero before,
    // which indicates that we should re-iterate the document.
    if (
      oldRectsAndTexts &&
      oldRectsAndTexts.rectList.length &&
      !rectsAndTexts.rectList.length
    ) {
      dict.detectedGeometryChange = true;
    }
    if (checkIfDynamic && this._isInDynamicContainer(range)) {
      dict.dynamicRangesSet.add(range);
    }
    return rectsAndTexts;
  },

  /**
   * Re-read the rectangles of the ranges that we keep track of separately,
   * because they're enclosed by a position: fixed container DOM node or (i)frame.
   *
   * @param {object} dict Dictionary of properties belonging to the currently
   *                      active window
   */
  _updateDynamicRangesRects(dict) {
    // Reset the frame bounds cache.
    for (let frameData of dict.frames.values()) {
      frameData.bounds = null;
    }
    for (let range of dict.dynamicRangesSet) {
      this._updateRangeRects(range, false, dict);
    }
  },

  /**
   * Update the content, position and style of the yellow current found range
   * outline that floats atop the mask with the dimmed background.
   * Rebuild it, if necessary, This will deactivate the animation between
   * occurrences.
   *
   * @param {object} dict Dictionary of properties belonging to the currently
   *                      active window
   */
  _updateRangeOutline(dict) {
    let range = dict.currentFoundRange;
    if (!range) {
      return;
    }

    let fontStyle = this._getRangeFontStyle(range);
    // Text color in the outline is determined by kModalStyles.
    delete fontStyle.color;

    let rectsAndTexts = this._updateRangeRects(range, true, dict);
    let outlineAnonNode = dict.modalHighlightOutline;
    let rectCount = rectsAndTexts.rectList.length;
    let previousRectCount = dict.previousRangeRectsAndTexts.rectList.length;
    // (re-)Building the outline is conditional and happens when one of the
    // following conditions is met:
    // 1. No outline nodes were built before, or
    // 2. When the amount of rectangles to draw is different from before, or
    // 3. When there's more than one rectangle to draw, because it's impossible
    //    to animate that consistently with AnonymousContent nodes.
    let rebuildOutline =
      !outlineAnonNode || rectCount !== previousRectCount || rectCount != 1;
    dict.previousRangeRectsAndTexts = rectsAndTexts;

    let window = this.getTopWindow(range.startContainer.documentGlobal);
    let document = window.document;
    // First see if we need to and can remove the previous outline nodes.
    if (rebuildOutline) {
      this._removeRangeOutline(window);
    }

    // Abort when there's no text to highlight OR when it's the exact same range
    // as the previous call and isn't inside a dynamic container.
    if (
      !rectsAndTexts.textList.length ||
      (!rebuildOutline &&
        dict.previousUpdatedRange == range &&
        !dict.dynamicRangesSet.has(range))
    ) {
      return;
    }

    let outlineBox;
    if (rebuildOutline) {
      // Create the main (yellow) highlight outline box.
      outlineBox = document.createElementNS(kNSHTML, "div");
      outlineBox.setAttribute("id", kModalOutlineId);
    }

    const kModalOutlineTextId = kModalOutlineId + "-text";
    let i = 0;
    for (let rect of rectsAndTexts.rectList) {
      let text = rectsAndTexts.textList[i];

      // Next up is to check of the outline box' borders will not overlap with
      // rects that we drew before or will draw after this one.
      // We're taking the width of the border into account, which is
      // `kOutlineBoxBorderSize` pixels.
      // When left and/ or right sides will overlap with the current, previous
      // or next rect, make sure to make the necessary adjustments to the style.
      // These adjustments will override the styles as defined in `kModalStyles.outlineNode`.
      let intersectingSides = new Set();
      let previous = rectsAndTexts.rectList[i - 1];
      if (previous && rect.left - previous.right <= 2 * kOutlineBoxBorderSize) {
        intersectingSides.add("left");
      }
      let next = rectsAndTexts.rectList[i + 1];
      if (next && next.left - rect.right <= 2 * kOutlineBoxBorderSize) {
        intersectingSides.add("right");
      }
      let borderStyles = [...intersectingSides].map(side => [
        "border-" + side,
        0,
      ]);
      if (intersectingSides.size) {
        borderStyles.push([
          "margin",
          `-${kOutlineBoxBorderSize}px 0 0 ${
            intersectingSides.has("left") ? 0 : -kOutlineBoxBorderSize
          }px !important`,
        ]);
        borderStyles.push([
          "border-radius",
          (intersectingSides.has("left") ? 0 : kOutlineBoxBorderRadius) +
            "px " +
            (intersectingSides.has("right") ? 0 : kOutlineBoxBorderRadius) +
            "px " +
            (intersectingSides.has("right") ? 0 : kOutlineBoxBorderRadius) +
            "px " +
            (intersectingSides.has("left") ? 0 : kOutlineBoxBorderRadius) +
            "px",
        ]);
      }

      let outlineStyle = this._getStyleString(
        kModalStyles.outlineNode,
        [
          ["top", rect.top + "px"],
          ["left", rect.left + "px"],
          ["height", rect.height + "px"],
          ["width", rect.width + "px"],
        ],
        borderStyles,
        lazy.kDebug ? kModalStyles.outlineNodeDebug : []
      );
      fontStyle.lineHeight = rect.height + "px";
      let textStyle =
        this._getStyleString(kModalStyles.outlineText) +
        "; " +
        this._getHTMLFontStyle(fontStyle);

      if (rebuildOutline) {
        let textBoxParent = outlineBox.appendChild(
          document.createElementNS(kNSHTML, "div")
        );
        textBoxParent.setAttribute("id", kModalOutlineId + i);
        textBoxParent.setAttribute("style", outlineStyle);

        let textBox = document.createElementNS(kNSHTML, "span");
        textBox.setAttribute("id", kModalOutlineTextId + i);
        textBox.setAttribute("style", textStyle);
        textBox.textContent = text;
        textBoxParent.appendChild(textBox);
      } else {
        // Set the appropriate properties on the existing nodes, which will also
        // activate the transitions.
        outlineAnonNode.setAttributeForElement(
          kModalOutlineId + i,
          "style",
          outlineStyle
        );
        outlineAnonNode.setAttributeForElement(
          kModalOutlineTextId + i,
          "style",
          textStyle
        );
        outlineAnonNode.setTextContentForElement(kModalOutlineTextId + i, text);
      }

      ++i;
    }

    if (rebuildOutline) {
      dict.modalHighlightOutline = lazy.kDebug
        ? mockAnonymousContentNode(
            (document.body || document.documentElement).appendChild(outlineBox)
          )
        : document.insertAnonymousContent(outlineBox);
    }

    if (dict.animateOutline && !this._isPageTooBig(dict)) {
      let animation;
      dict.animations = new Set();
      for (let i = rectsAndTexts.rectList.length - 1; i >= 0; --i) {
        animation = dict.modalHighlightOutline.setAnimationForElement(
          kModalOutlineId + i,
          Cu.cloneInto(kModalOutlineAnim.keyframes, window),
          kModalOutlineAnim.duration
        );
        animation.onfinish = function () {
          dict.animations.delete(this);
        };
        dict.animations.add(animation);
      }
    }
    dict.animateOutline = false;
    dict.ignoreNextContentChange = true;

    dict.previousUpdatedRange = range;
  },

  /**
   * Finish any currently playing animations on the found range outline node.
   *
   * @param {object} dict Dictionary of properties belonging to the currently
   *                      active window
   */
  _finishOutlineAnimations(dict) {
    if (!dict.animations) {
      return;
    }
    for (let animation of dict.animations) {
      animation.finish();
    }
  },

  /**
   * Safely remove the outline AnoymousContent node from the CanvasFrame.
   *
   * @param {nsIDOMWindow} window
   */
  _removeRangeOutline(window) {
    let dict = this.getForWindow(window);
    if (!dict.modalHighlightOutline) {
      return;
    }

    if (lazy.kDebug) {
      dict.modalHighlightOutline.remove();
    } else {
      try {
        window.document.removeAnonymousContent(dict.modalHighlightOutline);
      } catch (ex) {}
    }

    dict.modalHighlightOutline = null;
  },

  /**
   * Add a range to the list of ranges to highlight on, or cut out of, the dimmed
   * background.
   *
   * @param {Range}        range  Range object that should be inspected
   * @param {nsIDOMWindow} window Window object, whose DOM tree is being traversed
   */
  _modalHighlight(range, controller, window) {
    this._updateRangeRects(range);

    this.show(window);
    // We don't repaint the mask right away, but pass it off to a render loop of
    // sorts.
    this._scheduleRepaintOfMask(window);
  },

  /**
   * Lazily insert the nodes we need as anonymous content into the CanvasFrame
   * of a window.
   *
   * @param {nsIDOMWindow} window Window to draw in.
   */
  _maybeCreateModalHighlightNodes(window) {
    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);
    if (dict.modalHighlightOutline) {
      if (!dict.modalHighlightAllMask) {
        // Make sure to at least show the dimmed background.
        this._repaintHighlightAllMask(window, false);
        this._scheduleRepaintOfMask(window);
      } else {
        this._scheduleRepaintOfMask(window, { contentChanged: true });
      }
      return;
    }

    let document = window.document;
    // A hidden document doesn't accept insertAnonymousContent calls yet.
    if (document.hidden) {
      let onVisibilityChange = () => {
        document.removeEventListener("visibilitychange", onVisibilityChange);
        this._maybeCreateModalHighlightNodes(window);
      };
      document.addEventListener("visibilitychange", onVisibilityChange);
      return;
    }

    // Make sure to at least show the dimmed background.
    this._repaintHighlightAllMask(window, false);
  },

  /**
   * Build and draw the mask that takes care of the dimmed background that
   * overlays the current page and the mask that cuts out all the rectangles of
   * the ranges that were found.
   *
   * @param {nsIDOMWindow} window Window to draw in.
   * @param {boolean} [paintContent]
   */
  _repaintHighlightAllMask(window, paintContent = true) {
    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);

    const kMaskId = kModalIdPrefix + "-findbar-modalHighlight-outlineMask";
    if (!dict.modalHighlightAllMask) {
      let document = window.document;
      let maskNode = document.createElementNS(kNSHTML, "div");
      maskNode.setAttribute("id", kMaskId);
      dict.modalHighlightAllMask = lazy.kDebug
        ? mockAnonymousContentNode(
            (document.body || document.documentElement).appendChild(maskNode)
          )
        : document.insertAnonymousContent(maskNode);
    }

    // Make sure the dimmed mask node takes the full width and height that's available.
    let { width, height } = (dict.lastWindowDimensions =
      this._getWindowDimensions(window));
    if (typeof dict.brightText != "boolean" || dict.updateAllRanges) {
      this._detectBrightText(dict);
    }
    let maskStyle = this._getStyleString(
      kModalStyles.maskNode,
      [
        ["width", width + "px"],
        ["height", height + "px"],
      ],
      dict.brightText ? kModalStyles.maskNodeBrightText : [],
      paintContent ? kModalStyles.maskNodeTransition : [],
      lazy.kDebug ? kModalStyles.maskNodeDebug : []
    );
    dict.modalHighlightAllMask.setAttributeForElement(
      kMaskId,
      "style",
      maskStyle
    );

    this._updateRangeOutline(dict);

    let allRects = [];
    // When the user's busy scrolling the document, don't bother cutting out rectangles,
    // because they're not going to keep up with scrolling speed anyway.
    if (!dict.busyScrolling && (paintContent || dict.modalHighlightAllMask)) {
      // No need to update dynamic ranges separately when we already about to
      // update all of them anyway.
      if (!dict.updateAllRanges) {
        this._updateDynamicRangesRects(dict);
      }

      let DOMRect = window.DOMRect;
      for (let [range, rectsAndTexts] of dict.modalHighlightRectsMap) {
        if (!this.finder._fastFind.isRangeVisible(range, false)) {
          continue;
        }

        if (dict.updateAllRanges) {
          rectsAndTexts = this._updateRangeRects(range);
        }

        // If a geometry change was detected, we bail out right away here, because
        // the current set of ranges has been invalidated.
        if (dict.detectedGeometryChange) {
          return;
        }

        for (let rect of rectsAndTexts.rectList) {
          allRects.push(new DOMRect(rect.x, rect.y, rect.width, rect.height));
        }
      }
      dict.updateAllRanges = false;
    }

    // We may also want to cut out zero rects, which effectively clears out the mask.
    dict.modalHighlightAllMask.setCutoutRectsForElement(kMaskId, allRects);

    // The reflow observer may ignore the reflow we cause ourselves here.
    dict.ignoreNextContentChange = true;
  },

  /**
   * Safely remove the mask AnoymousContent node from the CanvasFrame.
   *
   * @param {nsIDOMWindow} window
   */
  _removeHighlightAllMask(window) {
    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);
    if (!dict.modalHighlightAllMask) {
      return;
    }

    // If the current window isn't the one the content was inserted into, this
    // will fail, but that's fine.
    if (lazy.kDebug) {
      dict.modalHighlightAllMask.remove();
    } else {
      try {
        window.document.removeAnonymousContent(dict.modalHighlightAllMask);
      } catch (ex) {}
    }
    dict.modalHighlightAllMask = null;
  },

  /**
   * Check if the width or height of the current document is too big to handle
   * for certain operations. This allows us to degrade gracefully when we expect
   * the performance to be negatively impacted due to drawing-intensive operations.
   *
   * @param  {object} dict Dictionary of properties belonging to the currently
   *                       active window
   * @return {boolean}
   */
  _isPageTooBig(dict) {
    let { height, width } = dict.lastWindowDimensions;
    return height >= kPageIsTooBigPx || width >= kPageIsTooBigPx;
  },

  /**
   * Doing a full repaint each time a range is delivered by the highlight iterator
   * is way too costly, thus we pipe the frequency down to every
   * `kModalHighlightRepaintLoFreqMs` milliseconds. If there are dynamic ranges
   * found (see `_isInDynamicContainer()` for the definition), the frequency
   * will be upscaled to `kModalHighlightRepaintHiFreqMs`.
   *
   * @param {nsIDOMWindow} window
   * @param {object}       options Dictionary of painter hints that contains the
   *                               following properties:
   *   {Boolean} contentChanged  Whether the documents' content changed in the
   *                             meantime. This happens when the DOM is updated
   *                             whilst the page is loaded.
   *   {Boolean} scrollOnly      TRUE when the page has scrolled in the meantime,
   *                             which means that the dynamically positioned
   *                             elements need to be repainted.
   *   {Boolean} updateAllRanges Whether to recalculate the rects of all ranges
   *                             that were found up until now.
   */
  _scheduleRepaintOfMask(
    window,
    { contentChanged = false, scrollOnly = false, updateAllRanges = false } = {}
  ) {
    if (!this.useModal()) {
      return;
    }

    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);
    // Bail out early if the repaint scheduler is paused or when we're supposed
    // to ignore the next paint (i.e. content change).
    if (
      dict.repaintSchedulerState == kRepaintSchedulerPaused ||
      (contentChanged && dict.ignoreNextContentChange)
    ) {
      dict.ignoreNextContentChange = false;
      return;
    }

    let hasDynamicRanges = !!dict.dynamicRangesSet.size;
    let pageIsTooBig = this._isPageTooBig(dict);
    let repaintDynamicRanges =
      (scrollOnly || contentChanged) && hasDynamicRanges && !pageIsTooBig;

    // Determine scroll behavior and keep that state around.
    let startedScrolling = !dict.busyScrolling && scrollOnly;
    // When the user started scrolling the document, hide the other highlights.
    if (startedScrolling) {
      dict.busyScrolling = startedScrolling;
      this._repaintHighlightAllMask(window);
    }
    // Whilst scrolling, suspend the repaint scheduler, but only when the page is
    // too big or the find results contains ranges that are inside dynamic
    // containers.
    if (dict.busyScrolling && (pageIsTooBig || hasDynamicRanges)) {
      dict.ignoreNextContentChange = true;
      this._updateRangeOutline(dict);
      // NB: we're not using `kRepaintSchedulerPaused` on purpose here, otherwise
      // we'd break the `busyScrolling` detection (re-)using the timer.
      if (dict.modalRepaintScheduler) {
        window.clearTimeout(dict.modalRepaintScheduler);
        dict.modalRepaintScheduler = null;
      }
    }

    // When we request to repaint unconditionally, we mean to call
    // `_repaintHighlightAllMask()` right after the timeout.
    if (!dict.unconditionalRepaintRequested) {
      dict.unconditionalRepaintRequested =
        !contentChanged || repaintDynamicRanges;
    }
    // Some events, like a resize, call for recalculation of all the rects of all ranges.
    if (!dict.updateAllRanges) {
      dict.updateAllRanges = updateAllRanges;
    }

    if (dict.modalRepaintScheduler) {
      return;
    }

    let timeoutMs =
      hasDynamicRanges && !dict.busyScrolling
        ? kModalHighlightRepaintHiFreqMs
        : kModalHighlightRepaintLoFreqMs;
    dict.modalRepaintScheduler = window.setTimeout(() => {
      dict.modalRepaintScheduler = null;
      dict.repaintSchedulerState = kRepaintSchedulerStopped;
      dict.busyScrolling = false;

      let pageContentChanged = dict.detectedGeometryChange;
      if (!pageContentChanged && !pageIsTooBig) {
        let { width: previousWidth, height: previousHeight } =
          dict.lastWindowDimensions;
        let { width, height } = (dict.lastWindowDimensions =
          this._getWindowDimensions(window));
        pageContentChanged =
          dict.detectedGeometryChange ||
          Math.abs(previousWidth - width) > kContentChangeThresholdPx ||
          Math.abs(previousHeight - height) > kContentChangeThresholdPx;
      }
      dict.detectedGeometryChange = false;
      // When the page has changed significantly enough in size, we'll restart
      // the iterator with the same parameters as before to find us new ranges.
      if (pageContentChanged && !pageIsTooBig) {
        this.iterator.restart(this.finder);
      }

      if (
        dict.unconditionalRepaintRequested ||
        (dict.modalHighlightRectsMap.size && pageContentChanged)
      ) {
        dict.unconditionalRepaintRequested = false;
        this._repaintHighlightAllMask(window);
      }
    }, timeoutMs);
    dict.repaintSchedulerState = kRepaintSchedulerRunning;
  },

  /**
   * Add event listeners to the content which will cause the modal highlight
   * AnonymousContent to be re-painted or hidden.
   *
   * @param {nsIDOMWindow} window
   */
  _addModalHighlightListeners(window) {
    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);
    if (dict.highlightListeners) {
      return;
    }

    dict.highlightListeners = [
      this._scheduleRepaintOfMask.bind(this, window, { contentChanged: true }),
      this._scheduleRepaintOfMask.bind(this, window, { updateAllRanges: true }),
      this._scheduleRepaintOfMask.bind(this, window, { scrollOnly: true }),
      this.hide.bind(this, window, false),
      () => (dict.busySelecting = true),
      () => {
        if (window.document.hidden) {
          dict.repaintSchedulerState = kRepaintSchedulerPaused;
        } else if (dict.repaintSchedulerState == kRepaintSchedulerPaused) {
          dict.repaintSchedulerState = kRepaintSchedulerRunning;
          this._scheduleRepaintOfMask(window);
        }
      },
    ];
    let target = this.iterator._getDocShell(window).chromeEventHandler;
    target.addEventListener("MozAfterPaint", dict.highlightListeners[0]);
    target.addEventListener("resize", dict.highlightListeners[1]);
    target.addEventListener("scroll", dict.highlightListeners[2], {
      capture: true,
      passive: true,
    });
    target.addEventListener("click", dict.highlightListeners[3]);
    target.addEventListener("selectstart", dict.highlightListeners[4]);
    window.document.addEventListener(
      "visibilitychange",
      dict.highlightListeners[5]
    );
  },

  /**
   * Remove event listeners from content.
   *
   * @param {nsIDOMWindow} window
   */
  _removeModalHighlightListeners(window) {
    window = this.getTopWindow(window);
    let dict = this.getForWindow(window);
    if (!dict.highlightListeners) {
      return;
    }

    let target = this.iterator._getDocShell(window).chromeEventHandler;
    target.removeEventListener("MozAfterPaint", dict.highlightListeners[0]);
    target.removeEventListener("resize", dict.highlightListeners[1]);
    target.removeEventListener("scroll", dict.highlightListeners[2], {
      capture: true,
      passive: true,
    });
    target.removeEventListener("click", dict.highlightListeners[3]);
    target.removeEventListener("selectstart", dict.highlightListeners[4]);
    window.document.removeEventListener(
      "visibilitychange",
      dict.highlightListeners[5]
    );

    dict.highlightListeners = null;
  },

  /**
   * For a given node returns its editable parent or null if there is none.
   * It's enough to check if node is a text node and it's containing shadow
   * host is an input or textarea.
   *
   * @param node the node we want to check
   * @returns the first node in the parent chain that is editable,
   *          null if there is no such node
   */
  _getEditableNode(node) {
    if (node.nodeType !== node.TEXT_NODE) {
      return null;
    }
    let host = node.getRootNode().host;
    if (!host) {
      return null;
    }
    let className = ChromeUtils.getClassName(host);
    if (
      className !== "HTMLInputElement" &&
      className !== "HTMLTextAreaElement"
    ) {
      return null;
    }
    return host;
  },

  /**
   * Add ourselves as an nsIEditActionListener and nsIDocumentStateListener for
   * a given editor
   *
   * @param editor the editor we'd like to listen to
   */
  _addEditorListeners(editor) {
    if (!this._editors) {
      this._editors = [];
      this._stateListeners = [];
    }

    let existingIndex = this._editors.indexOf(editor);
    if (existingIndex == -1) {
      let x = this._editors.length;
      this._editors[x] = editor;
      this._stateListeners[x] = this._createStateListener();
      this._editors[x].addEditActionListener(this);
      this._editors[x].addDocumentStateListener(this._stateListeners[x]);
    }
  },

  /**
   * Helper method to unhook listeners, remove cached editors
   * and keep the relevant arrays in sync
   *
   * @param idx the index into the array of editors/state listeners
   *        we wish to remove
   */
  _unhookListenersAtIndex(idx) {
    this._editors[idx].removeEditActionListener(this);
    this._editors[idx].removeDocumentStateListener(this._stateListeners[idx]);
    this._editors.splice(idx, 1);
    this._stateListeners.splice(idx, 1);
    if (!this._editors.length) {
      delete this._editors;
      delete this._stateListeners;
    }
  },

  /**
   * Remove ourselves as an nsIEditActionListener and
   * nsIDocumentStateListener from a given cached editor
   *
   * @param editor the editor we no longer wish to listen to
   */
  _removeEditorListeners(editor) {
    // editor is an editor that we listen to, so therefore must be
    // cached. Find the index of this editor
    let idx = this._editors.indexOf(editor);
    if (idx == -1) {
      return;
    }
    // Now unhook ourselves, and remove our cached copy
    this._unhookListenersAtIndex(idx);
  },

  /*
   * nsIEditActionListener logic follows
   *
   * We implement this interface to allow us to catch the case where
   * the findbar found a match in a HTML <input> or <textarea>. If the
   * user adjusts the text in some way, it will no longer match, so we
   * want to remove the highlight, rather than have it expand/contract
   * when letters are added or removed.
   */

  /**
   * Helper method used to check whether a selection intersects with
   * some highlighting
   *
   * @param selectionRange the range from the selection to check
   * @param findRange the highlighted range to check against
   * @returns true if they intersect, false otherwise
   */
  _checkOverlap(selectionRange, findRange) {
    if (!selectionRange || !findRange) {
      return false;
    }
    // The ranges overlap if one of the following is true:
    // 1) At least one of the endpoints of the deleted selection
    //    is in the find selection
    // 2) At least one of the endpoints of the find selection
    //    is in the deleted selection
    if (
      findRange.isPointInRange(
        selectionRange.startContainer,
        selectionRange.startOffset
      )
    ) {
      return true;
    }
    if (
      findRange.isPointInRange(
        selectionRange.endContainer,
        selectionRange.endOffset
      )
    ) {
      return true;
    }
    if (
      selectionRange.isPointInRange(
        findRange.startContainer,
        findRange.startOffset
      )
    ) {
      return true;
    }
    if (
      selectionRange.isPointInRange(findRange.endContainer, findRange.endOffset)
    ) {
      return true;
    }

    return false;
  },

  /**
   * Helper method to determine if an edit occurred within a highlight
   *
   * @param selection the selection we wish to check
   * @param node the node we want to check is contained in selection
   * @param offset the offset into node that we want to check
   * @returns the range containing (node, offset) or null if no ranges
   *          in the selection contain it
   */
  _findRange(selection, node, offset) {
    let rangeCount = selection.rangeCount;
    let rangeidx = 0;
    let foundContainingRange = false;
    let range = null;

    // Check to see if this node is inside one of the selection's ranges
    while (!foundContainingRange && rangeidx < rangeCount) {
      range = selection.getRangeAt(rangeidx);
      if (range.isPointInRange(node, offset)) {
        foundContainingRange = true;
        break;
      }
      rangeidx++;
    }

    if (foundContainingRange) {
      return range;
    }

    return null;
  },

  // Start of nsIEditActionListener implementations

  WillDeleteText(textNode, offset) {
    let editor = this._getEditableNode(textNode).editor;
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(
      Ci.nsISelectionController.SELECTION_FIND
    );
    let range = this._findRange(fSelection, textNode, offset);

    if (range) {
      // Don't remove the highlighting if the deleted text is at the
      // end of the range
      if (textNode != range.endContainer || offset != range.endOffset) {
        // Text within the highlight is being removed - the text can
        // no longer be a match, so remove the highlighting
        fSelection.removeRange(range);
        if (fSelection.rangeCount == 0) {
          this._removeEditorListeners(editor);
        }
      }
    }
  },

  DidInsertText(textNode, offset, aString) {
    let editor = this._getEditableNode(textNode).editor;
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(
      Ci.nsISelectionController.SELECTION_FIND
    );
    let range = this._findRange(fSelection, textNode, offset);

    if (range) {
      // If the text was inserted before the highlight
      // adjust the highlight's bounds accordingly
      if (textNode == range.startContainer && offset == range.startOffset) {
        range.setStart(
          range.startContainer,
          range.startOffset + aString.length
        );
      } else if (textNode != range.endContainer || offset != range.endOffset) {
        // The edit occurred within the highlight - any addition of text
        // will result in the text no longer being a match,
        // so remove the highlighting
        fSelection.removeRange(range);
        if (fSelection.rangeCount == 0) {
          this._removeEditorListeners(editor);
        }
      }
    }
  },

  WillDeleteRanges(rangesToDelete) {
    let { editor } = this._getEditableNode(rangesToDelete[0].startContainer);
    let controller = editor.selectionController;
    let fSelection = controller.getSelection(
      Ci.nsISelectionController.SELECTION_FIND
    );

    let shouldDelete = {};
    let numberOfDeletedSelections = 0;
    let numberOfMatches = fSelection.rangeCount;

    // We need to test if any ranges to be deleted
    // are in any of the ranges of the find selection
    // Usually both selections will only contain one range, however
    // either may contain more than one.

    for (let fIndex = 0; fIndex < numberOfMatches; fIndex++) {
      shouldDelete[fIndex] = false;
      let fRange = fSelection.getRangeAt(fIndex);

      for (let selRange of rangesToDelete) {
        if (shouldDelete[fIndex]) {
          continue;
        }

        let doesOverlap = this._checkOverlap(selRange, fRange);
        if (doesOverlap) {
          shouldDelete[fIndex] = true;
          numberOfDeletedSelections++;
        }
      }
    }

    // OK, so now we know what matches (if any) are in the selection
    // that is being deleted. Time to remove them.
    if (!numberOfDeletedSelections) {
      return;
    }

    for (let i = numberOfMatches - 1; i >= 0; i--) {
      if (shouldDelete[i]) {
        fSelection.removeRange(fSelection.getRangeAt(i));
      }
    }

    // Remove listeners if no more highlights left
    if (!fSelection.rangeCount) {
      this._removeEditorListeners(editor);
    }
  },

  /*
   * nsIDocumentStateListener logic follows
   *
   * When attaching nsIEditActionListeners, there are no guarantees
   * as to whether the findbar or the documents in the browser will get
   * destructed first. This leads to the potential to either leak, or to
   * hold on to a reference an editable element's editor for too long,
   * preventing it from being destructed.
   *
   * However, when an editor's owning node is being destroyed, the editor
   * sends out a DocumentWillBeDestroyed notification. We can use this to
   * clean up our references to the object, to allow it to be destroyed in a
   * timely fashion.
   */

  /**
   * Unhook ourselves when one of our state listeners has been called.
   * This can happen in 4 cases:
   *  1) The document the editor belongs to is navigated away from, and
   *     the document is not being cached
   *
   *  2) The document the editor belongs to is expired from the cache
   *
   *  3) The tab containing the owning document is closed
   *
   *  4) The <input> or <textarea> that owns the editor is explicitly
   *     removed from the DOM
   *
   * @param the listener that was invoked
   */
  _onEditorDestruction(aListener) {
    // First find the index of the editor the given listener listens to.
    // The listeners and editors arrays must always be in sync.
    // The listener will be in our array of cached listeners, as this
    // method could not have been called otherwise.
    let idx = 0;
    while (this._stateListeners[idx] != aListener) {
      idx++;
    }

    // Unhook both listeners
    this._unhookListenersAtIndex(idx);
  },

  /**
   * Creates a unique document state listener for an editor.
   *
   * It is not possible to simply have the findbar implement the
   * listener interface itself, as it wouldn't have sufficient information
   * to work out which editor was being destroyed. Therefore, we create new
   * listeners on the fly, and cache them in sync with the editors they
   * listen to.
   */
  _createStateListener() {
    return {
      findbar: this,

      QueryInterface: ChromeUtils.generateQI(["nsIDocumentStateListener"]),

      NotifyDocumentWillBeDestroyed() {
        this.findbar._onEditorDestruction(this);
      },

      // Unimplemented
      notifyDocumentStateChanged() {},
    };
  },
};
