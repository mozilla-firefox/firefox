/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import {ProvidersManager} from "UrlbarProvidersManager.sys.mjs"
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  L10nCache: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderQuickSuggest:
    "moz-src:///browser/components/urlbar/UrlbarProviderQuickSuggest.sys.mjs",
  UrlbarProviderTopSites:
    "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
  UrlbarResult: "moz-src:///browser/components/urlbar/UrlbarResult.sys.mjs",
  UrlbarSearchOneOffs:
    "moz-src:///browser/components/urlbar/UrlbarSearchOneOffs.sys.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "styleSheetService",
  "@mozilla.org/content/style-sheet-service;1",
  Ci.nsIStyleSheetService
);

// Query selector for selectable elements in results.
const SELECTABLE_ELEMENT_SELECTOR = "[role=button], [selectable], a";
const KEYBOARD_SELECTABLE_ELEMENT_SELECTOR =
  "[role=button]:not([keyboard-inaccessible]), [selectable], a";

const RESULT_MENU_COMMANDS = {
  DISMISS: "dismiss",
  HELP: "help",
  MANAGE: "manage",
};

const getBoundsWithoutFlushing = element =>
  element.documentGlobal.windowUtils.getBoundsWithoutFlushing(element);

// Used to get a unique id to use for row elements, it wraps at 9999, that
// should be plenty for our needs.
let gUniqueIdSerial = 1;
function getUniqueId(prefix) {
  return prefix + (gUniqueIdSerial++ % 9999);
}

/**
 * Receives and displays address bar autocomplete results.
 */
export class UrlbarView {
  // Stale rows are removed on a timer with this timeout.
  static removeStaleRowsTimeout = 400;

  /**
   * @param {UrlbarInput} input
   *   The UrlbarInput instance belonging to this UrlbarView instance.
   */
  constructor(input) {
    this.input = input;
    this.panel = input.panel;
    this.controller = input.controller;
    this.document = this.panel.ownerDocument;
    this.window = this.document.defaultView;

    this.#rows = this.panel.querySelector(".urlbarView-results");
    this.resultMenu = this.panel.querySelector(".urlbarView-result-menu");
    this.#resultMenuCommands = new WeakMap();

    this.#rows.addEventListener("mousedown", this);

    // For the horizontal fade-out effect, set the overflow attribute on result
    // rows when they overflow.
    this.#rows.addEventListener("overflow", this);
    this.#rows.addEventListener("underflow", this);

    this.resultMenu.addEventListener("command", this);
    this.resultMenu.addEventListener("popupshowing", this);

    // `noresults` is used to style the one-offs without their usual top border
    // when no results are present.
    this.panel.setAttribute("noresults", "true");

    this.controller.setView(this);
    this.controller.addListener(this);
    // This is used by autoOpen to avoid flickering results when reopening
    // previously abandoned searches.
    this.queryContextCache = new QueryContextCache(5);

    // We cache l10n strings to avoid Fluent's async lookup.
    this.#l10nCache = new lazy.L10nCache(this.document.l10n);

    for (let viewTemplate of UrlbarView.dynamicViewTemplatesByName.values()) {
      if (viewTemplate.stylesheet) {
        addDynamicStylesheet(this.window, viewTemplate.stylesheet);
      }
    }

    let contextMenu = this.document.querySelector("#urlbarView-context-menu");
    if (contextMenu) {
      contextMenu.addEventListener("command", this);
      contextMenu.addEventListener("popupshowing", this);
      contextMenu.addEventListener("popuphiding", this);
    }
  }

  get oneOffSearchButtons() {
    if (this.input.sapName != "urlbar") {
      return null;
    }
    if (!this.#oneOffSearchButtons) {
      this.#oneOffSearchButtons = new lazy.UrlbarSearchOneOffs(this);
      this.#oneOffSearchButtons.addEventListener(
        "SelectedOneOffButtonChanged",
        this
      );
    }
    return this.#oneOffSearchButtons;
  }

  /**
   * Whether the panel is open.
   *
   * @returns {boolean}
   */
  get isOpen() {
    return this.input.hasAttribute("open");
  }

  get allowEmptySelection() {
    let { heuristicResult } = this.#queryContext || {};
    return !heuristicResult || !this.#shouldShowHeuristic(heuristicResult);
  }

  get selectedRowIndex() {
    if (!this.isOpen) {
      return -1;
    }

    let selectedRow = this.#getSelectedRow();

    if (!selectedRow) {
      return -1;
    }

    return selectedRow.result.rowIndex;
  }

  set selectedRowIndex(val) {
    if (!this.isOpen) {
      throw new Error(
        "UrlbarView: Cannot select an item if the view isn't open."
      );
    }

    if (val < 0) {
      this.#selectElement(null);
      return;
    }

    let items = Array.from(this.#rows.children).filter(r =>
      this.#isElementVisible(r)
    );
    if (val >= items.length) {
      throw new Error(`UrlbarView: Index ${val} is out of bounds.`);
    }

    // Select the first selectable element inside the row. If it doesn't
    // contain a selectable element, clear the selection.
    let row = items[val];
    let element = this.#getNextSelectableElement(row);
    if (this.#getRowFromElement(element) != row) {
      element = null;
    }

    this.#selectElement(element);
  }

  get selectedElementIndex() {
    if (!this.isOpen || !this.#selectedElement) {
      return -1;
    }

    return this.#selectedElement.elementIndex;
  }

  /**
   * @returns {UrlbarResult}
   *   The currently selected result.
   */
  get selectedResult() {
    if (!this.isOpen) {
      return null;
    }

    return this.#getSelectedRow()?.result;
  }

  /**
   * @returns {HTMLElement}
   *   The currently selected element.
   */
  get selectedElement() {
    if (!this.isOpen) {
      return null;
    }

    return this.#selectedElement;
  }

  /**
   * @returns {ProvidersManager}
   */
  get #providersManager() {
    return this.controller.manager;
  }

  /**
   * @returns {boolean}
   *   Whether the SPACE key should activate the selected element (if any)
   *   instead of adding to the input value.
   */
  shouldSpaceActivateSelectedElement() {
    // We want SPACE to activate result menu always.
    if (this.selectedElement?.dataset.name == "result-menu") {
      return true;
    }

    // We want SPACE to activate buttons only.
    if (this.selectedElement?.getAttribute("role") != "button") {
      return false;
    }

    // Make sure the input field is empty, otherwise the user might want to add
    // a space to the current search string. As it stands, selecting a button
    // should always clear the input field, so this is just an extra safeguard.
    if (this.input.value) {
      return false;
    }
    return true;
  }

  /**
   * Clears selection, regardless of view status.
   */
  clearSelection() {
    this.#selectElement(null, { updateInput: false });
  }

  /**
   * @returns {number}
   *   The number of visible results in the view.  Note that this may be larger
   *   than the number of results in the current query context since the view
   *   may be showing stale results.
   */
  get visibleRowCount() {
    let sum = 0;
    for (let row of this.#rows.children) {
      sum += Number(this.#isElementVisible(row));
    }
    return sum;
  }

  /**
   * Returns the result of the row containing the given element, or the result
   * of the element if it itself is a row.
   *
   * @param {Element} element
   *   An element in the view.
   * @returns {UrlbarResult}
   *   The result of the element's row.
   */
  getResultFromElement(element) {
    return element?.classList.contains("urlbarView-result-menuitem")
      ? this.#resultMenuResult
      : this.#getRowFromElement(element)?.result;
  }

  /**
   * @param {number} index
   *   The index from which to fetch the result.
   * @returns {UrlbarResult}
   *   The result at `index`. Null if the view is closed or if there are no
   *   results.
   */
  getResultAtIndex(index) {
    if (
      !this.isOpen ||
      !this.#rows.children.length ||
      index >= this.#rows.children.length
    ) {
      return null;
    }

    return this.#rows.children[index].result;
  }

  /**
   * @param {UrlbarResult} result A result.
   * @returns {boolean} True if the given result is selected.
   */
  resultIsSelected(result) {
    if (this.selectedRowIndex < 0) {
      return false;
    }

    return result.rowIndex == this.selectedRowIndex;
  }

  /**
   * Moves the view selection forward or backward.
   *
   * @param {number} amount
   *   The number of steps to move.
   * @param {object} options Options object
   * @param {boolean} [options.reverse]
   *   Set to true to select the previous item. By default the next item
   *   will be selected.
   * @param {boolean} [options.userPressedTab]
   *   Set to true if the user pressed Tab to select a result. Default false.
   */
  selectBy(amount, { reverse = false, userPressedTab = false } = {}) {
    if (!this.isOpen) {
      throw new Error(
        "UrlbarView: Cannot select an item if the view isn't open."
      );
    }

    // Freeze results as the user is interacting with them, unless we are
    // deferring events while waiting for critical results.
    if (!this.input.eventBufferer.isDeferringEvents) {
      this.controller.cancelQuery();
    }

    if (!userPressedTab) {
      let { selectedRowIndex } = this;
      let end = this.visibleRowCount - 1;
      if (selectedRowIndex == -1) {
        this.selectedRowIndex = reverse ? end : 0;
        return;
      }
      let endReached = selectedRowIndex == (reverse ? 0 : end);
      if (endReached) {
        if (this.allowEmptySelection) {
          this.#selectElement(null);
        } else {
          this.selectedRowIndex = reverse ? end : 0;
        }
        return;
      }

      let index = Math.min(end, selectedRowIndex + amount * (reverse ? -1 : 1));
      // When navigating with arrow keys we skip rows that contain
      // global actions.
      if (
        this.#rows.children[index]?.result.providerName ==
          "UrlbarProviderGlobalActions" &&
        this.#rows.children.length > 2
      ) {
        index = index + (reverse ? -1 : 1);
      }
      this.selectedRowIndex = Math.max(0, index);
      return;
    }

    // Tab key handling below.

    // Do not set aria-activedescendant if the user is moving to a
    // tab-to-search result with the Tab key. If
    // accessibility.tabToSearch.announceResults is set, the tab-to-search
    // result was announced to the user as they typed. We don't set
    // aria-activedescendant so the user doesn't think they have to press
    // Enter to enter search mode. See bug 1647929.
    const isSkippableTabToSearchAnnounce = selectedElt => {
      let result = this.getResultFromElement(selectedElt);
      let skipAnnouncement =
        result?.providerName == "UrlbarProviderTabToSearch" &&
        !this.#announceTabToSearchOnSelection &&
        lazy.UrlbarPrefs.get("accessibility.tabToSearch.announceResults");
      if (skipAnnouncement) {
        // Once we skip setting aria-activedescendant once, we should not skip
        // it again if the user returns to that result.
        this.#announceTabToSearchOnSelection = true;
      }
      return skipAnnouncement;
    };

    let selectedElement = this.#selectedElement;

    // We cache the first and last rows since they will not change while
    // selectBy is running.
    let firstSelectableElement = this.getFirstSelectableElement();
    // getLastSelectableElement will not return an element that is over
    // maxResults and thus may be hidden and not selectable.
    let lastSelectableElement = this.getLastSelectableElement();

    if (!selectedElement) {
      selectedElement = reverse
        ? lastSelectableElement
        : firstSelectableElement;
      this.#selectElement(selectedElement, {
        setAccessibleFocus: !isSkippableTabToSearchAnnounce(selectedElement),
      });
      return;
    }
    let endReached = reverse
      ? selectedElement == firstSelectableElement
      : selectedElement == lastSelectableElement;
    if (endReached) {
      if (this.allowEmptySelection) {
        selectedElement = null;
      } else {
        selectedElement = reverse
          ? lastSelectableElement
          : firstSelectableElement;
      }
      this.#selectElement(selectedElement, {
        setAccessibleFocus: !isSkippableTabToSearchAnnounce(selectedElement),
      });
      return;
    }

    while (amount-- > 0) {
      let next = reverse
        ? this.#getPreviousSelectableElement(selectedElement)
        : this.#getNextSelectableElement(selectedElement);
      if (!next) {
        break;
      }
      selectedElement = next;
    }
    this.#selectElement(selectedElement, {
      setAccessibleFocus: !isSkippableTabToSearchAnnounce(selectedElement),
    });
  }

  async acknowledgeFeedback(result) {
    let row = this.#rows.children[result.rowIndex];
    if (!row) {
      return;
    }

    let l10n = { id: "urlbar-feedback-acknowledgment" };
    await this.#l10nCache.ensure(l10n);
    if (row.result != result) {
      return;
    }

    let { value } = this.#l10nCache.get(l10n);
    row.setAttribute("feedback-acknowledgment", value);
    row._content.closest("[role=option]").ariaNotify(value);
  }

  /**
   * Replaces the given result's row with a dismissal-acknowledgment tip.
   *
   * @param {UrlbarResult} result
   *   The result that was dismissed.
   * @param {object} titleL10n
   *   The localization object shown as dismissed feedback.
   */
  #acknowledgeDismissal(result, titleL10n) {
    let row = this.#rows.children[result.rowIndex];
    if (!row || row.result != result) {
      return;
    }

    // The row is no longer selectable. It's necessary to clear the selection
    // before replacing the row because replacement will likely create a new
    // `urlbarView-row-inner`, which will interfere with the ability of
    // `#selectElement()` to clear the old selection after replacement, below.
    let isSelected = this.#getSelectedRow() == row;
    if (isSelected) {
      this.#selectElement(null, { updateInput: false });
    }
    this.#setRowSelectable(row, false);

    // Replace the row with a dismissal acknowledgment tip.
    let tip = new lazy.UrlbarResult({
      type: lazy.UrlbarUtils.RESULT_TYPE.TIP,
      source: lazy.UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
      payload: {
        type: "dismissalAcknowledgment",
        titleL10n,
        buttons: [{ l10n: { id: "urlbar-search-tips-confirm-short" } }],
        icon: "chrome://branding/content/icon32.png",
      },
      rowLabel: !result.hideRowLabel && this.#rowLabel(row),
      hideRowLabel: result.hideRowLabel,
      richSuggestionIconSize: 32,
    });
    this.#updateRow(row, tip);
    this.#updateIndices();

    // If the row was selected, move the selection to the tip button.
    if (isSelected) {
      this.#selectElement(this.#getNextSelectableElement(row), {
        updateInput: false,
      });
    }
  }

  removeAccessibleFocus() {
    this.#setAccessibleFocus(null);
  }

  clear() {
    this.#rows.textContent = "";
    this.panel.setAttribute("noresults", "true");
    this.clearSelection();
    this.visibleResults = [];
  }

  /**
   * Closes the view, cancelling the query if necessary.
   *
   * @param {object} options Options object
   * @param {boolean} [options.elementPicked]
   *   True if the view is being closed because a result was picked.
   * @param {boolean} [options.showFocusBorder]
   *   True if the Urlbar focus border should be shown after the view is closed.
   */
  close({ elementPicked = false, showFocusBorder = true } = {}) {
    const isShowingZeroPrefix =
      this.#queryContext && !this.#queryContext.searchString;
    this.controller.cancelQuery();
    // We do not show the focus border when an element is picked because we'd
    // flash it just before the input is blurred. The focus border is removed
    // in UrlbarInput._on_blur.
    if (!elementPicked && showFocusBorder) {
      this.input.removeAttribute("suppress-focus-border");
    }

    if (!this.isOpen) {
      return;
    }

    this.#stopTail150();

    this.#inputWidthOnLastClose = getBoundsWithoutFlushing(this.input).width;

    // We exit search mode preview on close since the result previewing it is
    // implicitly unselected.
    if (this.input.searchMode?.isPreview) {
      this.input.searchMode = null;
      this.window.gBrowser.userTypedValue = null;
    }

    this.resultMenu.hidePopup();
    this.removeAccessibleFocus();
    this.input.inputField.setAttribute("aria-expanded", "false");
    this.#openPanelInstance = null;
    this.#previousTabToSearchEngine = null;

    this.input.removeAttribute("open");
    this.input.endLayoutExtend();

    // Search Tips can open the view without the Urlbar being focused. If the
    // tip is ignored (e.g. the page content is clicked or the window loses
    // focus) we should discard the telemetry event created when the view was
    // opened.
    if (!this.input.focused && !elementPicked) {
      this.controller.engagementEvent.discard();
      this.controller.engagementEvent.record(null, {});
    }

    this.window.removeEventListener("resize", this);
    this.window.removeEventListener("blur", this);

    this.controller.notify(this.controller.NOTIFICATIONS.VIEW_CLOSE);

    // Revoke icon blob URLs that were created while the view was open.
    if (this.#blobUrlsByResultUrl) {
      for (let blobUrl of this.#blobUrlsByResultUrl.values()) {
        URL.revokeObjectURL(blobUrl);
      }
      this.#blobUrlsByResultUrl.clear();
    }

    if (isShowingZeroPrefix) {
      if (elementPicked) {
        Glean.urlbarZeroprefix.engagement.add(1);
      } else {
        Glean.urlbarZeroprefix.abandonment.add(1);
      }
    }
  }

  startTail150() {
    if (this.#tail150) {
      return;
    }

    let doc = this.document;
    let ns = "http://www.w3.org/1999/xhtml";
    let overlay = doc.createElementNS(ns, "div");
    overlay.className = "urlbarView-tail150-overlay";

    let closeBtn = doc.createElementNS(ns, "div");
    closeBtn.className = "close-button";
    closeBtn.setAttribute("role", "button");
    closeBtn.addEventListener("click", () => this.close());

    let canvas = doc.createElementNS(ns, "canvas");
    let dpr = this.window.devicePixelRatio || 1;
    canvas.width = 400 * dpr;
    canvas.height = 400 * dpr;
    canvas.getContext("2d").scale(dpr, dpr);
    canvas.className = "urlbarView-tail150-canvas";
    overlay.append(closeBtn, canvas);

    this.input.appendChild(overlay);
    this.#tail150 = { overlay, keyHandler: null };
    this.#runTail150(canvas);
  }

  #stopTail150() {
    if (!this.#tail150) {
      return;
    }
    if (this.#tail150.keyHandler) {
      this.window.removeEventListener(
        "keydown",
        this.#tail150.keyHandler,
        true
      );
    }
    this.#tail150.overlay.remove();
    this.#tail150 = null;
  }

  #runTail150(canvas) {
    let S = this.window.getComputedStyle(canvas);
    let AC = S.getPropertyValue("--color-gray-05");
    let FD = S.getPropertyValue("--color-yellow-30");
    let SP = new this.window.Image();
    SP.src = "chrome://branding/content/icon48.png";
    // prettier-ignore
    (() => { let c=canvas,W=this.window,A=t=>W.requestAnimationFrame(t),X=c.getContext("2d"),CA=(x,y,r)=>{X.beginPath();X.arc(x,y,r,0,7);X.fill()},g=()=>20*Math.random()|0,V=[,[-1,0],[0,-1],[1,0],[0,1]],s,d,n,f,e,r=0,l=0,GO=m=>{r=0,X.shadowColor="#000",X.shadowBlur=8,X.fillStyle=AC,X.fillText(m,200,180),X.fillText(e,200,230)},PF=()=>{let a=[];for(let x=0;x<20;x++)for(let y=0;y<20;y++)s.every($=>$.x!=x||$.y!=y)&&a.push({x,y});a.length?f=a[a.length*Math.random()|0]:GO("GG")},I=()=>{s=[...Array(8)].map(($,t)=>({x:10-t,y:10})),d=n=V[3],e=0,f={x:15,y:15},r=1,A(L)},L=$=>{if(!this.#tail150||!r)return;A(L);let p=($-l)/100;if(p>=1){l=$,d=n,p=0;let t={x:s[0].x+d[0],y:s[0].y+d[1]};if(t.x<0||t.x>19||t.y<0||t.y>19||s.some($=>$.x==t.x&&$.y==t.y)){GO("GAME OVER");return}s.unshift(t),t.x==f.x&&t.y==f.y?(e++,PF()):s.pop();if(!r)return}X.clearRect(0,0,400,400),X.fillStyle=FD,CA(20*f.x+10,20*f.y+10,6),s.map(($,t)=>{if(X.save(),X.translate(Math.min(390,Math.max(10,20*($.x+(!t&&d[0]*p))+10)),Math.min(390,Math.max(10,20*($.y+(!t&&d[1]*p))+10))),t){let i=t/s.length;X.fillStyle=`oklch(${62+17*i}% ${.21-.01*i} ${90*i})`,CA(0,0,8)}else SP.complete&&X.drawImage(SP,-10,-10,20,20);X.restore()})};X.fillStyle=AC;X.textAlign="center";X.font="30px Arial";X.fillText("← ↑ ↓ →",200,200);W.addEventListener("keydown",this.#tail150.keyHandler=$=>{$.keyCode!=27&&($.preventDefault(),$.stopPropagation());let t=V[$.keyCode-36];!r&&t&&I(),t&&(t[0]!=-d[0]||t[1]!=-d[1])&&(n=t)},true); })(); // eslint-disable-line
  }

  /**
   * This can be used to open the view automatically as a consequence of
   * specific user actions. For Top Sites searches (without a search string)
   * the view is opened only for mouse or keyboard interactions.
   * If the user abandoned a search (there is a search string) the view is
   * reopened, and we try to use cached results to reduce flickering, then a new
   * query is started to refresh results.
   *
   * @param {object} options Options object
   * @param {Event} options.event The event associated with the call to autoOpen.
   * @param {boolean} [options.suppressFocusBorder] If true, we hide the focus border
   *        when the panel is opened. This is true by default to avoid flashing
   *        the border when the unfocused address bar is clicked.
   * @returns {boolean} Whether the view was opened.
   */
  autoOpen({ event, suppressFocusBorder = true }) {
    if (!event) {
      return false;
    }
    if (this.input.readOnly) {
      return false;
    }
    if (this.#pickSearchTipIfPresent(event)) {
      return false;
    }
    if (this.input.inOverflowPanel) {
      // The results panel is currently disabled in the overflow panel.
      return false;
    }

    let queryOptions = { event };
    if (
      !this.input.value ||
      this.input.getAttribute("pageproxystate") == "valid"
    ) {
      if (!this.isOpen && ["mousedown", "command"].includes(event.type)) {
        // Try to reuse the cached top-sites context. If it's not cached, then
        // there will be a gap of time between when the input is focused and
        // when the view opens that can be perceived as flicker.
        if (!this.input.searchMode && this.queryContextCache.topSitesContext) {
          this.onQueryResults(this.queryContextCache.topSitesContext);
        }
        this.input.startQuery(queryOptions);
        if (suppressFocusBorder) {
          this.input.toggleAttribute("suppress-focus-border", true);
        }
        return true;
      }
      return false;
    }

    // Reopen abandoned searches only if the input is focused.
    if (!this.input.focused) {
      return false;
    }

    // Tab switch is the only case where we requery if the view is open, because
    // switching tabs doesn't necessarily close the view.
    if (this.isOpen && event.type != "tabswitch") {
      return false;
    }

    // We can reuse the current rows as they are if the input value and width
    // haven't changed since the view was closed. The width check is related to
    // row overflow: If we reuse the current rows, overflow and underflow events
    // won't fire even if the view's width has changed and there are rows that
    // do actually overflow or underflow. That means previously overflowed rows
    // may unnecessarily show the overflow gradient, for example.
    if (
      this.#rows.firstElementChild &&
      this.#queryContext.searchString == this.input.value &&
      this.#inputWidthOnLastClose == getBoundsWithoutFlushing(this.input).width
    ) {
      // We can reuse the current rows.
      queryOptions.allowAutofill = this.#queryContext.allowAutofill;
    } else {
      // To reduce flickering, try to reuse a cached UrlbarQueryContext. The
      // overflow problem is addressed in this case because `onQueryResults()`
      // starts the regular view-update process, during which the overflow state
      // is reset on all rows.
      let cachedQueryContext = this.queryContextCache.get(this.input.value);
      if (cachedQueryContext) {
        this.onQueryResults(cachedQueryContext);
      }
    }

    // Disable autofill when search terms persist, as users are likely refining
    // their search rather than navigating to a website matching the search
    // term. If they do want to navigate directly, users can modify their
    // search, which resets persistence and re-enables autofill.
    let state = this.input.getBrowserState(
      this.window.gBrowser.selectedBrowser
    );
    if (state.persist?.shouldPersist) {
      queryOptions.allowAutofill = false;
    }

    this.controller.engagementEvent.discard();
    queryOptions.searchString = this.input.value;
    queryOptions.autofillIgnoresSelection = true;
    queryOptions.event.interactionType = "returned";

    // Opening the panel now will show the rows from the previous query, so to
    // avoid flicker, open it only if the search string hasn't changed. Also
    // check for a tip to avoid search tip flicker (bug 1812261). If we don't
    // open the panel here, we'll open it when the view receives results from
    // the new query.
    if (
      this.#queryContext?.results?.length &&
      this.#queryContext.searchString == this.input.value &&
      this.#queryContext.results[0].type != lazy.UrlbarUtils.RESULT_TYPE.TIP
    ) {
      this.#openPanel();
    }

    // If we had cached results, this will just refresh them, avoiding results
    // flicker, otherwise there may be some noise.
    this.input.startQuery(queryOptions);
    if (suppressFocusBorder) {
      this.input.toggleAttribute("suppress-focus-border", true);
    }
    return true;
  }

  // UrlbarController listener methods.
  onQueryStarted(queryContext) {
    this.#queryWasCancelled = false;
    this.#queryUpdatedResults = false;
    this.#openPanelInstance = null;
    if (!queryContext.searchString) {
      this.#previousTabToSearchEngine = null;
    }
    this.#startRemoveStaleRowsTimer();

    // Cache l10n strings so they're available when we update the view as
    // results arrive. This is a no-op for strings that are already cached.
    // `#cacheL10nStrings` is async but we don't await it because doing so would
    // require view updates to be async. Instead we just opportunistically cache
    // and if there's a cache miss we fall back to `l10n.setAttributes`.
    this.#cacheL10nStrings();
  }

  onQueryCancelled() {
    this.#queryWasCancelled = true;
    this.#cancelRemoveStaleRowsTimer();
  }

  onQueryFinished(queryContext) {
    this.#cancelRemoveStaleRowsTimer();
    if (this.#queryWasCancelled) {
      return;
    }

    // At this point the query finished successfully. If it returned some
    // results, remove stale rows. Otherwise remove all rows.
    if (this.#queryUpdatedResults) {
      this.#removeStaleRows();
    } else {
      this.clear();
    }

    // Now that the view has finished updating for this query, record the exposure.
    if (!queryContext.searchString) {
      Glean.urlbarZeroprefix.exposure.add(1);
    }

    // If the query returned results, we're done.
    if (this.#queryUpdatedResults) {
      return;
    }

    // If search mode isn't active, close the view.
    if (!this.input.searchMode) {
      this.close();
      return;
    }

    // Search mode is active.  If the one-offs should be shown, make sure they
    // are enabled and show the view.
    let openPanelInstance = (this.#openPanelInstance = {});
    this.oneOffSearchButtons?.willHide().then(willHide => {
      if (!willHide && openPanelInstance == this.#openPanelInstance) {
        this.oneOffSearchButtons.enable(true);
        this.#openPanel();
      }
    });
  }

  onQueryResults(queryContext) {
    this.queryContextCache.put(queryContext);
    this.#queryContext = queryContext;

    if (!this.isOpen) {
      this.clear();
    }

    // Set the actionmode atttribute if we are in actions search mode.
    // We do this before updating the result rows so that there is no flicker
    // after the actions are initially displayed.
    if (
      this.input.searchMode?.source == lazy.UrlbarUtils.RESULT_SOURCE.ACTIONS
    ) {
      this.#rows.toggleAttribute("actionmode", true);
    }

    this.#queryUpdatedResults = true;
    this.#updateResults();

    let firstResult = queryContext.results[0];

    if (queryContext.lastResultCount == 0) {
      // Clear the selection when we get a new set of results.
      this.#selectElement(null, {
        updateInput: false,
      });

      // Show the one-off search buttons unless any of the following are true:
      //  * The first result is a search tip
      //  * The search string is empty
      //  * The search string starts with an `@` or a search restriction
      //    character
      this.oneOffSearchButtons?.enable(
        (firstResult.providerName != "UrlbarProviderSearchTips" ||
          queryContext.trimmedSearchString) &&
          queryContext.trimmedSearchString[0] != "@" &&
          (queryContext.trimmedSearchString[0] !=
            lazy.UrlbarTokenizer.RESTRICT.SEARCH ||
            queryContext.trimmedSearchString.length != 1)
      );
    }

    if (!this.#selectedElement && !this.oneOffSearchButtons?.selectedButton) {
      if (firstResult.heuristic) {
        // Select the heuristic result.  The heuristic may not be the first
        // result added, which is why we do this check here when each result is
        // added and not above.
        if (this.#shouldShowHeuristic(firstResult)) {
          this.#selectElement(this.getFirstSelectableElement(), {
            updateInput: false,
            setAccessibleFocus:
              this.controller._userSelectionBehavior == "arrow",
          });
        } else {
          this.input.setResultForCurrentValue(firstResult);
        }
      } else if (
        firstResult.payload.providesSearchMode &&
        queryContext.trimmedSearchString != "@"
      ) {
        // Filtered keyword offer results can be in the first position but not
        // be heuristic results. We do this so the user can press Tab to select
        // them, resembling tab-to-search. In that case, the input value is
        // still associated with the first result.
        this.input.setResultForCurrentValue(firstResult);
      }
    }

    // Announce tab-to-search results to screen readers as the user types.
    // Check to make sure we don't announce the same engine multiple times in
    // a row.
    let secondResult = queryContext.results[1];
    if (
      secondResult?.providerName == "UrlbarProviderTabToSearch" &&
      lazy.UrlbarPrefs.get("accessibility.tabToSearch.announceResults") &&
      this.#previousTabToSearchEngine != secondResult.payload.engine
    ) {
      let engine = secondResult.payload.engine;
      let stringId = secondResult.payload.isGeneralPurposeEngine
        ? "urlbar-result-action-before-tabtosearch-web"
        : "urlbar-result-action-before-tabtosearch-other";
      this.#ariaNotifyLocalizedString(this.#rows.children[1], stringId, {
        engine,
      });
      this.#previousTabToSearchEngine = engine;
      // Do not set aria-activedescendant when the user tabs to the result
      // because we already announced it.
      this.#announceTabToSearchOnSelection = false;
    }

    // If we update the selected element, a new unique ID is generated for it.
    // We need to ensure that aria-activedescendant reflects this new ID.
    if (this.#selectedElement && !this.oneOffSearchButtons?.selectedButton) {
      let aadID = this.input.inputField.getAttribute("aria-activedescendant");
      if (aadID && !this.document.getElementById(aadID)) {
        this.#setAccessibleFocus(this.#selectedElement);
      }
    }

    this.#openPanel();

    if (firstResult.heuristic) {
      // The heuristic result may be a search alias result, so apply formatting
      // if necessary.  Conversely, the heuristic result of the previous query
      // may have been an alias, so remove formatting if necessary.
      this.input.formatValue();
    }

    if (queryContext.deferUserSelectionProviders.size) {
      // DeferUserSelectionProviders block user selection until the result is
      // shown, so it's the view's duty to remove them.
      // Doing it sooner, like when the results are added by the provider,
      // would not suffice because there's still a delay before those results
      // reach the view.
      queryContext.results.forEach(r => {
        queryContext.deferUserSelectionProviders.delete(r.providerName);
      });
    }

    if (lazy.UrlbarPrefs.get("unifiedSearchButton.always")) {
      // Update the search mode switcher icon to reflect what pressing Enter will do after new results show.
      this.input.searchModeSwitcher?.updateSearchIcon();
    }
  }

  /**
   * Handles removing a result from the view when it is removed from the query,
   * and attempts to select the new result on the same row.
   *
   * This assumes that the result rows are in index order.
   *
   * @param {number} index The index of the result that has been removed.
   */
  onQueryResultRemoved(index) {
    let rowToRemove = this.#rows.children[index];

    let { result } = rowToRemove;
    if (result.acknowledgeDismissalL10n) {
      // Replace the result's row with a dismissal acknowledgment tip.
      this.#acknowledgeDismissal(result, result.acknowledgeDismissalL10n);
      return;
    }

    let updateSelection = rowToRemove == this.#getSelectedRow();
    rowToRemove.remove();
    this.#updateIndices();

    if (!updateSelection) {
      return;
    }
    // Select the row at the same index, if possible.
    let newSelectionIndex = index;
    if (index >= this.#queryContext.results.length) {
      newSelectionIndex = this.#queryContext.results.length - 1;
    }
    if (newSelectionIndex >= 0) {
      this.selectedRowIndex = newSelectionIndex;
    }
  }

  openResultMenu(result, anchor) {
    this.#resultMenuResult = result;

    let event = new CustomEvent("ResultMenuTriggered", {
      detail: { target: anchor },
    });

    this.resultMenu.openPopup(anchor, {
      position: "bottomright topright",
      triggerEvent: event,
    });

    anchor.toggleAttribute("open", true);
    let listener = event => {
      if (event.target == this.resultMenu) {
        anchor.removeAttribute("open");
        this.resultMenu.removeEventListener("popuphidden", listener);
      }
    };
    this.resultMenu.addEventListener("popuphidden", listener);
  }

  /**
   * Clears the result menu commands cache, removing the cached commands for all
   * results. This is useful when the commands for one or more results change
   * while the results remain in the view.
   */
  invalidateResultMenuCommands() {
    this.#resultMenuCommands = new WeakMap();
  }

  /**
   * Passes DOM events for the view to the on_<event type> methods.
   *
   * @param {Event} event
   *   DOM event from the <view>.
   */
  handleEvent(event) {
    let methodName = "on_" + event.type;
    if (methodName in this) {
      this[methodName](event);
    } else {
      throw new Error("Unrecognized UrlbarView event: " + event.type);
    }
  }

  static dynamicViewTemplatesByName = new Map();

  /**
   * Registers the view template for a dynamic result type.  A view template is
   * a plain object that describes the DOM subtree for a dynamic result type.
   * When a dynamic result is shown in the urlbar view, its type's view template
   * is used to construct the part of the view that represents the result.
   *
   * The specified view template will be available to the urlbars in all current
   * and future browser windows until it is unregistered.  A given dynamic
   * result type has at most one view template.  If this method is called for a
   * dynamic result type more than once, the view template in the last call
   * overrides those in previous calls.
   *
   * @param {string} name
   *   The view template will be registered for the dynamic result type with
   *   this name.
   * @param {object} viewTemplate
   *   This object describes the DOM subtree for the given dynamic result type.
   *   It should be a tree-like nested structure with each object in the nesting
   *   representing a DOM element to be created.  This tree-like structure is
   *   achieved using the `children` property described below.  Each object in
   *   the structure may include the following properties:
   *
   *   {string} tag
   *     The tag name of the object.  It is required for all objects in the
   *     structure except the root object and declares the kind of element that
   *     will be created for the object: span, div, img, etc.
   *   {string} [name]
   *     The name of the object. This value is required if you need to update
   *     the object's DOM element at query time. It's also helpful but not
   *     required if you need to style the element. When defined, it serves two
   *     important functions:
   *
   *     (1) The element created for the object will automatically have a class
   *         named `urlbarView-dynamic-${dynamicType}-${name}`, where
   *         `dynamicType` is the name of the dynamic result type.  The element
   *         will also automatically have an attribute "name" whose value is
   *         this name.  The class and attribute allow the element to be styled
   *         in CSS.
   *     (2) The name is used when updating the view.  See
   *         UrlbarProvider.getViewUpdate().
   *
   *     Names must be unique within a view template, but they don't need to be
   *     globally unique.  i.e., two different view templates can use the same
   *     names, and other DOM elements can use the same names in their IDs and
   *     classes.  The name also suffixes the dynamic element's ID: an element
   *     with name `data` will get the ID `urlbarView-row-{unique number}-data`.
   *     If there is no name provided for the root element, the root element
   *     will not get an ID.
   *   {object} [attributes]
   *     An optional mapping from attribute names to values.  For each
   *     name-value pair, an attribute is added to the element created for the
   *     object. The `id` attribute is reserved and cannot be set by the
   *     provider. Element IDs are passed back to the provider in getViewUpdate
   *     if they are needed.
   *   {array} [children]
   *     An optional list of children.  Each item in the array must be an object
   *     as described here.  For each item, a child element as described by the
   *     item is created and added to the element created for the parent object.
   *   {array} [classList]
   *     An optional list of classes.  Each class will be added to the element
   *     created for the object by calling element.classList.add().
   *   {boolean} [overflowable]
   *     If true, the element's overflow status will be tracked in order to
   *     fade it out when needed.
   *   {string} [stylesheet]
   *     An optional stylesheet URL.  This property is valid only on the root
   *     object in the structure.  The stylesheet will be loaded in all browser
   *     windows so that the dynamic result type view may be styled.
   */
  static addDynamicViewTemplate(name, viewTemplate) {
    this.dynamicViewTemplatesByName.set(name, viewTemplate);
    if (viewTemplate.stylesheet) {
      for (let window of lazy.BrowserWindowTracker.orderedWindows) {
        addDynamicStylesheet(window, viewTemplate.stylesheet);
      }
    }
  }

  /**
   * Unregisters the view template for a dynamic result type.
   *
   * @param {string} name
   *   The view template will be unregistered for the dynamic result type with
   *   this name.
   */
  static removeDynamicViewTemplate(name) {
    let viewTemplate = this.dynamicViewTemplatesByName.get(name);
    if (!viewTemplate) {
      return;
    }
    this.dynamicViewTemplatesByName.delete(name);
    if (viewTemplate.stylesheet) {
      for (let window of lazy.BrowserWindowTracker.orderedWindows) {
        removeDynamicStylesheet(window, viewTemplate.stylesheet);
      }
    }
  }

  // Private properties and methods below.
  #announceTabToSearchOnSelection;
  #blobUrlsByResultUrl = null;
  #tail150 = null;
  #inputWidthOnLastClose = 0;
  #l10nCache;
  #mousedownSelectedElement;
  #openPanelInstance;
  #oneOffSearchButtons;
  #previousTabToSearchEngine;
  #queryContext;
  #queryUpdatedResults;
  #queryWasCancelled;
  #removeStaleRowsTimer;
  #resultMenuResult;
  #resultMenuCommands;
  #rows;
  #rawSelectedElement;

  /**
   * #rawSelectedElement may be disconnected from the DOM (e.g. it was remove()d)
   * but we want a connected #selectedElement usually. We don't use a WeakRef
   * because it would depend too much on GC timing.
   *
   * @returns {HTMLElement} the selected element.
   */
  get #selectedElement() {
    return this.#rawSelectedElement?.isConnected
      ? this.#rawSelectedElement
      : null;
  }

  #createElement(name) {
    return this.document.createElementNS("http://www.w3.org/1999/xhtml", name);
  }

  #openPanel() {
    if (this.isOpen) {
      return;
    }
    this.controller.userSelectionBehavior = "none";

    this.panel.removeAttribute("action-override");

    this.#enableOrDisableRowWrap();

    this.input.inputField.setAttribute("aria-expanded", "true");

    this.input.toggleAttribute("suppress-focus-border", true);
    this.input.toggleAttribute("open", true);
    this.input.startLayoutExtend();

    this.window.addEventListener("resize", this);
    this.window.addEventListener("blur", this);

    this.controller.notify(this.controller.NOTIFICATIONS.VIEW_OPEN);

    this.maybeRollupPopups();
  }

  /**
   * Depending on the pref, rolls up all popups in the window.
   * If the moz-urlbar is in the overflow panel, it does nothing
   * to avoid closing the overflow panel.
   */
  maybeRollupPopups() {
    if (
      lazy.UrlbarPrefs.get("closeOtherPanelsOnOpen") &&
      !this.input.inOverflowPanel
    ) {
      this.window.docShell.treeOwner
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIAppWindow)
        .rollupAllPopups();
    }
  }

  #shouldShowHeuristic(result) {
    if (!result?.heuristic) {
      throw new Error("A heuristic result must be given");
    }
    return (
      !lazy.UrlbarPrefs.get("experimental.hideHeuristic") ||
      result.type == lazy.UrlbarUtils.RESULT_TYPE.TIP
    );
  }

  /**
   * Whether a result is a search suggestion.
   *
   * @param {UrlbarResult} result The result to examine.
   * @returns {boolean} Whether the result is a search suggestion.
   */
  #resultIsSearchSuggestion(result) {
    return Boolean(
      result &&
      result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
      result.payload.suggestion
    );
  }

  /**
   * Checks whether the given row index can be update to the result we want
   * to apply. This is used in #updateResults to avoid flickering of results, by
   * reusing existing rows.
   *
   * @param {number} rowIndex Index of the row to examine.
   * @param {UrlbarResult} result The result we'd like to apply.
   * @param {boolean} seenSearchSuggestion Whether the view update has
   *        encountered an existing row with a search suggestion result.
   * @returns {boolean} Whether the row can be updated to this result.
   */
  #rowCanUpdateToResult(rowIndex, result, seenSearchSuggestion) {
    // The heuristic result must always be current, thus it's always compatible.
    // Note that the `updateResults` code, when updating the selection, relies
    // on the fact the heuristic is the first selectable row.
    if (result.heuristic) {
      return true;
    }
    let row = this.#rows.children[rowIndex];
    // Don't replace a suggestedIndex result with a non-suggestedIndex result
    // or vice versa.
    if (result.hasSuggestedIndex != row.result.hasSuggestedIndex) {
      return false;
    }
    // Don't replace a suggestedIndex result with another suggestedIndex
    // result if the suggestedIndex values are different.
    if (
      result.hasSuggestedIndex &&
      result.suggestedIndex != row.result.suggestedIndex
    ) {
      return false;
    }
    // To avoid flickering results while typing, don't try to reuse results from
    // different providers.
    // For example user types "moz", provider A returns results much earlier
    // than provider B, but results from provider B stabilize in the view at the
    // end of the search. Typing the next letter "i" results from the faster
    // provider A would temporarily replace old results from provider B, just
    // to be replaced as soon as provider B returns its results.
    if (result.providerName != row.result.providerName) {
      return false;
    }
    let resultIsSearchSuggestion = this.#resultIsSearchSuggestion(result);
    // If the row is same type, just update it.
    if (
      resultIsSearchSuggestion == this.#resultIsSearchSuggestion(row.result)
    ) {
      return true;
    }
    // If the row has a different type, update it if we are in a compatible
    // index range.
    // In practice we don't want to overwrite a search suggestion with a non
    // search suggestion, but we allow the opposite.
    return resultIsSearchSuggestion && seenSearchSuggestion;
  }

  #updateResults() {
    // TODO: For now this just compares search suggestions to the rest, in the
    // future we should make it support any type of result. Or, even better,
    // results should be grouped, thus we can directly update groups.

    // Discard tentative exposures. This is analogous to marking the
    // hypothetical hidden rows of hidden-exposure results as stale.
    this.controller.engagementEvent.discardTentativeExposures();

    // Walk rows and find an insertion index for results. To avoid flicker, we
    // skip rows until we find one compatible with the result we want to apply.
    // If we couldn't find a compatible range, we'll just update.
    let results = this.#queryContext.results;
    if (results[0]?.heuristic && !this.#shouldShowHeuristic(results[0])) {
      // Exclude the heuristic.
      results = results.slice(1);
    }
    let rowIndex = 0;
    // Make a copy of results, as we'll consume it along the way.
    let resultsToInsert = results.slice();
    let visibleSpanCount = 0;
    let seenMisplacedResult = false;
    let seenSearchSuggestion = false;

    // Update each row with the next new result until we either encounter a row
    // that can't be updated or run out of new results. At that point, mark
    // remaining rows as stale.
    while (rowIndex < this.#rows.children.length && resultsToInsert.length) {
      let row = this.#rows.children[rowIndex];
      if (this.#isElementVisible(row)) {
        visibleSpanCount += lazy.UrlbarUtils.getSpanForResult(row.result);
      }

      if (!seenMisplacedResult) {
        let result = resultsToInsert[0];
        seenSearchSuggestion =
          seenSearchSuggestion ||
          (!row.result.heuristic && this.#resultIsSearchSuggestion(row.result));
        if (
          this.#rowCanUpdateToResult(rowIndex, result, seenSearchSuggestion)
        ) {
          // We can replace the row's current result with the new one.
          resultsToInsert.shift();

          if (result.isHiddenExposure) {
            // Don't increment `rowIndex` because we're not actually updating
            // the row. We'll visit it again in the next iteration.
            this.controller.engagementEvent.addExposure(
              result,
              this.#queryContext
            );
            continue;
          }

          this.#updateRow(row, result);
          rowIndex++;
          continue;
        }

        if (
          (result.hasSuggestedIndex || row.result.hasSuggestedIndex) &&
          !result.isHiddenExposure
        ) {
          seenMisplacedResult = true;
        }
      }

      row.setAttribute("stale", "true");
      rowIndex++;
    }

    // Mark all the remaining rows as stale and update the visible span count.
    // We include stale rows in the count because we should never show more than
    // maxResults spans at one time.  Later we'll remove stale rows and unhide
    // excess non-stale rows.
    for (; rowIndex < this.#rows.children.length; ++rowIndex) {
      let row = this.#rows.children[rowIndex];
      row.setAttribute("stale", "true");
      if (this.#isElementVisible(row)) {
        visibleSpanCount += lazy.UrlbarUtils.getSpanForResult(row.result);
      }
    }

    // Add remaining results, if we have fewer rows than results.
    for (let result of resultsToInsert) {
      if (
        !seenMisplacedResult &&
        result.hasSuggestedIndex &&
        !result.isHiddenExposure
      ) {
        if (result.isSuggestedIndexRelativeToGroup) {
          // We can't know at this point what the right index of a group-
          // relative suggestedIndex result will be. To avoid all all possible
          // flicker, don't make it (and all rows after it) visible until stale
          // rows are removed.
          seenMisplacedResult = true;
        } else {
          // We need to check whether the new suggestedIndex result will end up
          // at its right index if we append it here. The "right" index is the
          // final index the result will occupy once the update is done and all
          // stale rows have been removed. We could use a more flexible
          // definition, but we use this strict one in order to avoid all
          // perceived flicker and movement of suggestedIndex results. Once
          // stale rows are removed, the final number of rows in the view will
          // be the new result count, so we base our arithmetic here on it.
          let finalIndex =
            result.suggestedIndex >= 0
              ? Math.min(results.length - 1, result.suggestedIndex)
              : Math.max(0, results.length + result.suggestedIndex);
          if (this.#rows.children.length != finalIndex) {
            seenMisplacedResult = true;
          }
        }
      }
      let newSpanCount =
        visibleSpanCount +
        lazy.UrlbarUtils.getSpanForResult(result, {
          includeHiddenExposures: true,
        });
      let canBeVisible =
        newSpanCount <= this.#queryContext.maxResults && !seenMisplacedResult;
      if (result.isHiddenExposure) {
        if (canBeVisible) {
          this.controller.engagementEvent.addExposure(
            result,
            this.#queryContext
          );
        } else {
          // Add a tentative exposure: The hypothetical row for this
          // hidden-exposure result can't be visible now, but as long as it were
          // not marked stale in a later update, it would be shown when stale
          // rows are removed.
          this.controller.engagementEvent.addTentativeExposure(
            result,
            this.#queryContext
          );
        }
        continue;
      }
      let row = this.#createRow();
      this.#updateRow(row, result);
      if (canBeVisible) {
        visibleSpanCount = newSpanCount;
      } else {
        // The new row must be hidden at first because the view is already
        // showing maxResults spans, or we encountered a new suggestedIndex
        // result that couldn't be placed in the right spot. We'll show it when
        // stale rows are removed.
        this.#setRowVisibility(row, false);
      }
      this.#rows.appendChild(row);
    }

    this.#updateIndices();
  }

  #createRow() {
    let item = this.#createElement("div");
    item.className = "urlbarView-row";
    item._elements = new Map();
    item._buttons = new Map();

    // A note about row selection. Any element in a row that can be selected
    // will have the `selectable` attribute set on it. For typical rows, the
    // selectable element is not the `.urlbarView-row` itself but rather the
    // `.urlbarView-row-inner` inside it. That's because the `.urlbarView-row`
    // also contains the row's buttons, which should not be selected when the
    // main part of the row -- `.urlbarView-row-inner` -- is selected.
    //
    // Since it's the row itself and not the row-inner that is a child of the
    // `role=listbox` element (the rows container, `this.#rows`), screen readers
    // will not automatically recognize the row-inner as a listbox option. To
    // compensate, we set `role=option` on the row-inner and `role=presentation`
    // on the row itself so that screen readers ignore it.
    item.setAttribute("role", "presentation");

    // These are used to cleanup result specific entities when row contents are
    // cleared to reuse the row for a different result.
    item._sharedAttributes = new Set(
      [...item.attributes].map(v => v.name).concat(["stale", "id", "hidden"])
    );
    item._sharedClassList = new Set(item.classList);

    return item;
  }

  #createRowContent(item) {
    // The url is the only element that can wrap, thus all the other elements
    // are child of noWrap.
    let noWrap = this.#createElement("span");
    noWrap.className = "urlbarView-no-wrap";
    item._content.appendChild(noWrap);

    let favicon = this.#createElement("img");
    favicon.className = "urlbarView-favicon";
    noWrap.appendChild(favicon);
    item._elements.set("favicon", favicon);

    let typeIcon = this.#createElement("span");
    typeIcon.className = "urlbarView-type-icon";
    noWrap.appendChild(typeIcon);

    let tailPrefix = this.#createElement("span");
    tailPrefix.className = "urlbarView-tail-prefix";
    noWrap.appendChild(tailPrefix);
    item._elements.set("tailPrefix", tailPrefix);
    // tailPrefix holds text only for alignment purposes so it should never be
    // read to screen readers.
    tailPrefix.toggleAttribute("aria-hidden", true);

    let tailPrefixStr = this.#createElement("span");
    tailPrefixStr.className = "urlbarView-tail-prefix-string";
    tailPrefix.appendChild(tailPrefixStr);
    item._elements.set("tailPrefixStr", tailPrefixStr);

    let tailPrefixChar = this.#createElement("span");
    tailPrefixChar.className = "urlbarView-tail-prefix-char";
    tailPrefix.appendChild(tailPrefixChar);
    item._elements.set("tailPrefixChar", tailPrefixChar);

    let title = this.#createElement("span");
    title.classList.add("urlbarView-title", "urlbarView-overflowable");
    noWrap.appendChild(title);
    item._elements.set("title", title);

    let tagsContainer = this.#createElement("span");
    tagsContainer.classList.add("urlbarView-tags", "urlbarView-overflowable");
    noWrap.appendChild(tagsContainer);
    item._elements.set("tagsContainer", tagsContainer);

    let titleSeparator = this.#createElement("span");
    titleSeparator.className = "urlbarView-title-separator";
    noWrap.appendChild(titleSeparator);
    item._elements.set("titleSeparator", titleSeparator);

    let action = this.#createElement("span");
    action.className = "urlbarView-action";
    noWrap.appendChild(action);
    item._elements.set("action", action);

    let url = this.#createElement("span");
    url.className = "urlbarView-url";
    item._content.appendChild(url);
    item._elements.set("url", url);

    if (lazy.UrlbarPrefs.get("resultExplanationsFeatureGate")) {
      let explanation = this.#createElement("span");
      explanation.classList.add(
        "urlbarView-explanation",
        "urlbarView-overflowable"
      );
      item._content.appendChild(explanation);
      item._elements.set("explanation", explanation);
    }
  }

  /**
   * Updates different aspects of an element given an update object. This method
   * is designed to be used for elements in dynamic result type rows, but it can
   * can be used for any element.
   *
   * @param {Element} element
   *   The element to update.
   * @param {object} update
   *   An object that describes how the element should be updated. It can have
   *   the following optional properties:
   *
   *   {object} attributes
   *     Attribute names to values mapping. For each name-value pair, an
   *     attribute is set on the element, except for `null` as a value which
   *     signals an attribute should be removed, and `undefined` in which case
   *     the attribute won't be set nor removed. The `id` attribute is reserved
   *     and cannot be set here.
   *   {object} dataset
   *     Maps element dataset keys to values. Values should be strings with the
   *     following exceptions: `undefined` is ignored, and `null` causes the key
   *     to be removed from the dataset.
   *   {Array} classList
   *     An array of CSS classes to set on the element. If this is defined, the
   *     element's previous classes will be cleared first!
   *
   * @param {Element} item
   *   The row element.
   * @param {UrlbarResult} result
   *   The UrlbarResult displayed to the node. This is optional.
   */
  #updateElementForDynamicType(element, update, item, result = null) {
    if (update.attributes) {
      for (let [name, value] of Object.entries(update.attributes)) {
        if (name == "id") {
          // IDs are managed externally to ensure they are unique.
          console.error(
            `Not setting id="${value}", as dynamic attributes may not include IDs.`
          );
          continue;
        }
        if (value === undefined) {
          continue;
        }
        if (value === null) {
          element.removeAttribute(name);
        } else if (typeof value == "boolean") {
          element.toggleAttribute(name, value);
        } else if (Blob.isInstance(value) && result) {
          element.setAttribute(name, this.#getBlobUrlForResult(result, value));
        } else {
          element.setAttribute(name, value);
        }
      }
    }

    if (update.dataset) {
      for (let [name, value] of Object.entries(update.dataset)) {
        if (value === null) {
          delete element.dataset[name];
        } else if (value !== undefined) {
          if (typeof value != "string") {
            console.error(
              `Trying to set a dataset value that is not a string`,
              { element, value }
            );
          } else {
            element.dataset[name] = value;
          }
        }
      }
    }

    if (update.classList) {
      if (element == item._content) {
        element.className = "urlbarView-row-inner";
      } else {
        element.className = "";
      }
      element.classList.add(...update.classList);
    }
  }

  #createRowContentForDynamicType(item, result) {
    let { dynamicType } = result.payload;
    let provider = this.#providersManager.getProvider(result.providerName);
    let viewTemplate =
      provider.getViewTemplate?.(result) ||
      UrlbarView.dynamicViewTemplatesByName.get(dynamicType);
    if (!viewTemplate) {
      console.error(`No viewTemplate found for ${result.providerName}`);
      return;
    }
    let classes = this.#buildViewForDynamicType(
      dynamicType,
      item._content,
      item._elements,
      viewTemplate,
      item
    );
    item.toggleAttribute("has-url", classes.has("urlbarView-url"));
    item.toggleAttribute("has-action", classes.has("urlbarView-action"));
    this.#setRowSelectable(item, item._content.hasAttribute("selectable"));
  }

  /**
   * Recursively builds a row's DOM for a dynamic result type.
   *
   * @param {string} type
   *   The name of the dynamic type.
   * @param {Element} parentNode
   *   The element being recursed into. Pass `row._content`
   *   (i.e., the row's `.urlbarView-row-inner`) to start with.
   * @param {Map} elementsByName
   *   The `row._elements` map.
   * @param {object} template
   *   The template object being recursed into. Pass the top-level template
   *   object to start with.
   * @param {Element} item
   *   The row element.
   * @param {Set} classes
   *   The CSS class names of all elements in the row's subtree are recursively
   *   collected in this set. Don't pass anything to start with so that the
   *   default argument, a new Set, is used.
   * @returns {Set}
   *   The `classes` set, which on return will contain the CSS class names of
   *   all elements in the row's subtree.
   */
  #buildViewForDynamicType(
    type,
    parentNode,
    elementsByName,
    template,
    item,
    classes = new Set()
  ) {
    this.#updateElementForDynamicType(parentNode, template, item);

    if (template.classList) {
      for (let c of template.classList) {
        classes.add(c);
      }
    }
    if (template.overflowable) {
      parentNode.classList.add("urlbarView-overflowable");
    }

    if (template.name) {
      parentNode.setAttribute("name", template.name);
      parentNode.classList.add(`urlbarView-dynamic-${type}-${template.name}`);
      elementsByName.set(template.name, parentNode);
    }

    // Recurse into children.
    for (let childTemplate of template.children || []) {
      let child = this.#createElement(childTemplate.tag);
      parentNode.appendChild(child);
      this.#buildViewForDynamicType(
        type,
        child,
        elementsByName,
        childTemplate,
        item,
        classes
      );
    }

    return classes;
  }

  #createRowContentForRichSuggestion(item, result) {
    item._content.toggleAttribute("selectable", true);

    let favicon = this.#createElement("img");
    favicon.className = "urlbarView-favicon";
    item._content.appendChild(favicon);
    item._elements.set("favicon", favicon);

    let typeIcon = this.#createElement("span");
    typeIcon.className = "urlbarView-type-icon";
    item._content.appendChild(typeIcon);

    let body = this.#createElement("span");
    body.className = "urlbarView-row-body";
    item._content.appendChild(body);

    let top = this.#createElement("div");
    top.className = "urlbarView-row-body-top";
    body.appendChild(top);

    let noWrap = this.#createElement("div");
    noWrap.className = "urlbarView-row-body-top-no-wrap";
    top.appendChild(noWrap);
    item._elements.set("noWrap", noWrap);

    let tailPrefix = this.#createElement("span");
    tailPrefix.className = "urlbarView-tail-prefix";
    noWrap.appendChild(tailPrefix);
    item._elements.set("tailPrefix", tailPrefix);
    // tailPrefix holds text only for alignment purposes so it should never be
    // read to screen readers.
    tailPrefix.toggleAttribute("aria-hidden", true);

    let tailPrefixStr = this.#createElement("span");
    tailPrefixStr.className = "urlbarView-tail-prefix-string";
    tailPrefix.appendChild(tailPrefixStr);
    item._elements.set("tailPrefixStr", tailPrefixStr);

    let tailPrefixChar = this.#createElement("span");
    tailPrefixChar.className = "urlbarView-tail-prefix-char";
    tailPrefix.appendChild(tailPrefixChar);
    item._elements.set("tailPrefixChar", tailPrefixChar);

    let title = this.#createElement("span");
    title.classList.add("urlbarView-title", "urlbarView-overflowable");
    noWrap.appendChild(title);
    item._elements.set("title", title);

    let tagsContainer = this.#createElement("span");
    tagsContainer.classList.add("urlbarView-tags", "urlbarView-overflowable");
    noWrap.appendChild(tagsContainer);
    item._elements.set("tagsContainer", tagsContainer);

    let titleSeparator = this.#createElement("span");
    titleSeparator.className = "urlbarView-title-separator";
    noWrap.appendChild(titleSeparator);
    item._elements.set("titleSeparator", titleSeparator);

    let action = this.#createElement("span");
    action.className = "urlbarView-action";
    noWrap.appendChild(action);
    item._elements.set("action", action);

    let url = this.#createElement("span");
    url.className = "urlbarView-url";
    top.appendChild(url);
    item._elements.set("url", url);

    if (lazy.UrlbarPrefs.get("resultExplanationsFeatureGate")) {
      let explanation = this.#createElement("span");
      explanation.classList.add(
        "urlbarView-explanation",
        "urlbarView-overflowable"
      );
      top.appendChild(explanation);
      item._elements.set("explanation", explanation);
    }

    let description = this.#createElement("div");
    description.classList.add("urlbarView-row-body-description");
    body.appendChild(description);
    item._elements.set("description", description);

    if (result.payload.descriptionLearnMoreTopic) {
      let learnMoreLink = this.#createElement("a");
      learnMoreLink.setAttribute("data-l10n-name", "learn-more-link");
      description.appendChild(learnMoreLink);
    }

    let bottom = this.#createElement("div");
    bottom.className = "urlbarView-row-body-bottom";
    body.appendChild(bottom);
    item._elements.set("bottom", bottom);
  }

  #createRowContentForBottomUrl(item, _result) {
    item._content.toggleAttribute("selectable", true);

    let favicon = this.#createElement("img");
    favicon.className = "urlbarView-favicon";
    item._content.appendChild(favicon);
    item._elements.set("favicon", favicon);

    let body = this.#createElement("span");
    body.className = "urlbarView-row-body";
    item._content.appendChild(body);

    let top = this.#createElement("div");
    top.className = "urlbarView-row-body-top";
    body.appendChild(top);

    let noWrap = this.#createElement("div");
    noWrap.className = "urlbarView-row-body-top-no-wrap";
    top.appendChild(noWrap);
    item._elements.set("noWrap", noWrap);

    let title = this.#createElement("span");
    title.classList.add("urlbarView-title", "urlbarView-overflowable");
    noWrap.appendChild(title);
    item._elements.set("title", title);

    let subtitleSeparator = this.#createElement("span");
    subtitleSeparator.className = "urlbarView-subtitle-separator";
    noWrap.appendChild(subtitleSeparator);
    item._elements.set("subtitleSeparator", subtitleSeparator);

    let subtitle = this.#createElement("span");
    subtitle.className = "urlbarView-subtitle";
    noWrap.appendChild(subtitle);
    item._elements.set("subtitle", subtitle);

    let description = this.#createElement("div");
    description.classList.add("urlbarView-row-body-description");
    body.appendChild(description);
    item._elements.set("description", description);

    let bottom = this.#createElement("div");
    bottom.className = "urlbarView-row-body-bottom";
    body.appendChild(bottom);

    let bottomLabel = this.#createElement("span");
    bottomLabel.className = "urlbarView-bottom-label";
    bottom.appendChild(bottomLabel);
    item._elements.set("bottomLabel", bottomLabel);

    let bottomSeparator = this.#createElement("span");
    bottomSeparator.className = "urlbarView-bottom-separator";
    bottom.appendChild(bottomSeparator);
    item._elements.set("bottomSeparator", bottomSeparator);

    let url = this.#createElement("span");
    url.className = "urlbarView-url";
    bottom.appendChild(url);
    item._elements.set("url", url);
  }

  #needsNewButtons(item, oldResult, newResult) {
    if (!oldResult) {
      return true;
    }

    if (
      !!this.#getResultMenuCommands(newResult) !=
      item._buttons.has("result-menu")
    ) {
      return true;
    }

    if (!!oldResult.showFeedbackMenu != !!newResult.showFeedbackMenu) {
      return true;
    }

    if (
      oldResult.payload.buttons?.length != newResult.payload.buttons?.length ||
      !lazy.ObjectUtils.deepEqual(
        oldResult.payload.buttons,
        newResult.payload.buttons
      )
    ) {
      return true;
    }

    return newResult.testForceNewContent;
  }

  #updateRowButtons(item, oldResult, result) {
    for (let i = 0; i < result.payload.buttons?.length; i++) {
      // We hold the name to each button data in payload to enable to get the
      // data from button element by the name. This name is mainly used for
      // button that has menu (Split Button).
      let button = result.payload.buttons[i];
      button.name ??= i.toString();
    }

    if (!this.#needsNewButtons(item, oldResult, result)) {
      return;
    }

    let container = item._elements.get("buttons");
    if (container) {
      container.innerHTML = "";
    } else {
      container = this.#createElement("div");
      container.className = "urlbarView-row-buttons";
      item.appendChild(container);
      item._elements.set("buttons", container);
    }

    item._buttons.clear();

    if (result.payload.buttons) {
      for (let button of result.payload.buttons) {
        this.#addRowButton(item, button);
      }
    }

    // TODO: `buttonText` is intended only for WebExtensions. We should remove
    // it and the WebExtensions urlbar API since we're no longer using it.
    if (result.payload.buttonText) {
      this.#addRowButton(item, {
        name: "tip",
        url: result.payload.buttonUrl,
      });
      item._buttons.get("tip").textContent = result.payload.buttonText;
    }

    if (this.#getResultMenuCommands(result)) {
      this.#addRowButton(item, {
        name: "result-menu",
        classList: ["urlbarView-button-menu"],
        l10n: result.showFeedbackMenu
          ? { id: "urlbar-result-menu-button-feedback" }
          : { id: "urlbar-result-menu-button" },
        attributes: lazy.UrlbarPrefs.get("resultMenu.keyboardAccessible")
          ? null
          : {
              "keyboard-inaccessible": true,
            },
      });
    }
  }

  #addRowButton(
    item,
    {
      name,
      command,
      l10n,
      url,
      classList = [],
      attributes = {},
      menu = null,
      input = null,
    }
  ) {
    let button = this.#createElement("span");
    this.#updateElementForDynamicType(
      button,
      {
        attributes: {
          ...attributes,
          role: "button",
        },
        classList: [
          ...classList,
          "urlbarView-button",
          "urlbarView-button-" + name,
        ],
        dataset: {
          name,
          command,
          url,
          input,
        },
      },
      item
    );

    button.id = `${item.id}-button-${name}`;
    if (l10n) {
      this.#l10nCache.setElementL10n(button, l10n);
    }

    item._buttons.set(name, button);

    if (!menu) {
      item._elements.get("buttons").appendChild(button);
      return;
    }

    // Split Button.
    let container = this.#createElement("span");
    container.classList.add("urlbarView-splitbutton");

    button.classList.add("urlbarView-splitbutton-main");
    container.appendChild(button);

    let dropmarker = this.#createElement("span");
    dropmarker.classList.add(
      "urlbarView-button",
      "urlbarView-button-menu",
      "urlbarView-splitbutton-dropmarker"
    );
    this.#l10nCache.setElementL10n(dropmarker, {
      id: "urlbar-splitbutton-dropmarker",
    });
    dropmarker.setAttribute("role", "button");
    container.appendChild(dropmarker);

    item._elements.get("buttons").appendChild(container);
  }

  #createSecondaryAction(action, global = false) {
    let actionContainer = this.#createElement("div");
    actionContainer.classList.add("urlbarView-actions-container");

    let button = this.#createElement("span");
    button.classList.add("urlbarView-action-btn");
    if (global) {
      button.classList.add("urlbarView-global-action-btn");
    }
    if (action.classList) {
      button.classList.add(...action.classList);
    }
    button.setAttribute("role", "button");
    if (action.icon) {
      let icon = this.#createElement("img");
      icon.src = action.icon;
      button.appendChild(icon);
    }
    for (let key in action.dataset ?? {}) {
      button.dataset[key] = action.dataset[key];
    }
    button.dataset.action = action.key;
    button.dataset.providerName = action.providerName;

    let label = this.#createElement("span");
    if (action.l10nId) {
      this.#l10nCache.setElementL10n(label, {
        id: action.l10nId,
        args: action.l10nArgs,
      });
    } else {
      this.document.l10n.setAttributes(label, action.label, action.l10nArgs);
    }
    button.appendChild(label);
    actionContainer.appendChild(button);
    return actionContainer;
  }

  #needsNewContent(item, oldResult, newResult) {
    if (!oldResult) {
      return true;
    }

    if (
      (oldResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC) !=
      (newResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC)
    ) {
      return true;
    }

    if (
      oldResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC &&
      newResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC &&
      oldResult.payload.dynamicType != newResult.payload.dynamicType
    ) {
      return true;
    }

    if (oldResult.isRichSuggestion != newResult.isRichSuggestion) {
      return true;
    }

    // Reusing a non-heuristic as a heuristic is risky as it may have DOM
    // nodes/attributes/classes that are normally not present in a heuristic
    // result. This may happen for example when switching from a zero-prefix
    // search not having a heuristic to a search string one.
    if (oldResult.heuristic != newResult.heuristic) {
      return true;
    }

    // TAB_SWITCH rows have tab-group chiclets and container icons that are not
    // present in other result types, so reusing them has higher risk of leaving
    // stale DOM.
    if (
      oldResult.type == lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH &&
      newResult.type != oldResult.type
    ) {
      return true;
    }

    if (
      newResult.providerName == lazy.UrlbarProviderQuickSuggest.name &&
      // Check if the `RESULT_TYPE` is `DYNAMIC` because otherwise the
      // `suggestionType` and `items` checks aren't relevant.
      newResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC &&
      (oldResult.payload.suggestionType != newResult.payload.suggestionType ||
        oldResult.payload.items?.length != newResult.payload.items?.length)
    ) {
      return true;
    }

    if (newResult.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC) {
      if (oldResult.providerName != newResult.providerName) {
        return true;
      }

      let provider = this.#providersManager.getProvider(newResult.providerName);
      if (
        !lazy.ObjectUtils.deepEqual(
          provider.getViewTemplate?.(oldResult),
          provider.getViewTemplate?.(newResult)
        )
      ) {
        return true;
      }
    }

    if (oldResult.isBottomUrlSuggestion != newResult.isBottomUrlSuggestion) {
      return true;
    }

    return newResult.testForceNewContent;
  }

  // eslint-disable-next-line complexity
  #updateRow(item, result) {
    let oldResult = item.result;
    item.result = result;
    item.removeAttribute("stale");
    item.id = getUniqueId("urlbarView-row-");

    if (this.#needsNewContent(item, oldResult, result)) {
      // Recreate the row content except the buttons, which we'll reuse below.
      let buttons = item._elements.get("buttons");
      while (item.lastChild) {
        item.lastChild.remove();
      }
      item._elements.clear();

      item._content = this.#createElement("span");
      item._content.className = "urlbarView-row-inner";
      item.appendChild(item._content);

      // Clear previously set attributes and classes that may refer to a
      // different result type.
      for (const attribute of [...item.attributes]) {
        if (!item._sharedAttributes.has(attribute.name)) {
          item.removeAttribute(attribute.name);
        }
      }
      for (const className of item.classList) {
        if (!item._sharedClassList.has(className)) {
          item.classList.remove(className);
        }
      }

      if (item.result.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC) {
        this.#createRowContentForDynamicType(item, result);
      } else if (result.isBottomUrlSuggestion) {
        this.#createRowContentForBottomUrl(item, result);
      } else if (
        result.isRichSuggestion ||
        Services.prefs.getBoolPref("browser.nova.enabled", false)
      ) {
        this.#createRowContentForRichSuggestion(item, result);
      } else {
        this.#createRowContent(item, result);
      }

      if (buttons) {
        item.appendChild(buttons);
        item._elements.set("buttons", buttons);
      }
    }

    this.#updateRowButtons(item, oldResult, result);

    item._content.id = item.id + "-inner";

    item.toggleAttribute("is-top-pick", !!result.isBestMatch);

    if (result.isBottomUrlSuggestion) {
      this.#updateRowContentForBottomUrl(item, result);
      return;
    }

    let isFirstChild = item === this.#rows.children[0];
    let secAction = result.payload.action;
    let actionsContainer = item.querySelector(".urlbarView-actions-container");
    item.toggleAttribute("secondary-action", !!secAction);
    if (secAction && !actionsContainer) {
      item.appendChild(this.#createSecondaryAction(secAction, isFirstChild));
    } else if (
      secAction &&
      secAction.key != actionsContainer.firstChild.dataset.action
    ) {
      item.replaceChild(
        this.#createSecondaryAction(secAction, isFirstChild),
        actionsContainer
      );
    } else if (!secAction && actionsContainer) {
      item.removeChild(actionsContainer);
    }

    item.removeAttribute("feedback-acknowledgment");

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
      !result.payload.providesSearchMode &&
      !result.payload.inPrivateWindow &&
      result.providerName != lazy.UrlbarProviderQuickSuggest.name
    ) {
      item.setAttribute(
        "type",
        result.isRichSuggestion ? "rich-search" : "search"
      );
    } else if (result.type == lazy.UrlbarUtils.RESULT_TYPE.REMOTE_TAB) {
      item.setAttribute("type", "remotetab");
    } else if (result.type == lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH) {
      item.setAttribute("type", "switchtab");
    } else if (result.type == lazy.UrlbarUtils.RESULT_TYPE.TIP) {
      item.setAttribute("type", "tip");
      item.setAttribute("tip-type", result.payload.type);

      // Due to role=button, the button and help icon can sometimes become
      // focused. We want to prevent that because the input should always be
      // focused instead. (This happens when input.search("", { focus: false })
      // is called, a tip is the first result but not heuristic, and the user
      // tabs the into the button from the navbar buttons. The input is skipped
      // and the focus goes straight to the tip button.)
      item.addEventListener("focus", () => this.input.focus(), true);

      if (
        result.providerName == "UrlbarProviderSearchTips" ||
        result.payload.type == "dismissalAcknowledgment"
      ) {
        // For a11y, we treat search tips as alerts. We use ariaNotify
        // instead of role="alert" because role="alert" will only fire an alert
        // event when the alert (or something inside it) is the root of an
        // insertion. In this case, the entire tip result gets inserted into the
        // a11y tree as a single insertion, so no alert event would be fired.
        this.#ariaNotifyLocalizedString(
          item,
          result.payload.titleL10n.id,
          result.payload.titleL10n.args
        );
      }
    } else if (result.source == lazy.UrlbarUtils.RESULT_SOURCE.BOOKMARKS) {
      item.setAttribute("type", "bookmark");
    } else if (result.type == lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC) {
      item.setAttribute("type", "dynamic");
      this.#updateRowForDynamicType(item, result);
      return;
    } else if (result.providerName == "UrlbarProviderTabToSearch") {
      item.setAttribute("type", "tabtosearch");
    } else if (result.providerName == "UrlbarProviderSemanticHistorySearch") {
      item.setAttribute("type", "semantic-history");
    } else if (result.providerName == "UrlbarProviderInputHistory") {
      item.setAttribute("type", "adaptive-history");
    } else {
      item.setAttribute(
        "type",
        lazy.UrlbarUtils.searchEngagementTelemetryType(result)
      );
    }

    let favicon = item._elements.get("favicon");
    favicon.src = this.#iconForResult(result);

    let title = item._elements.get("title");
    this.#setResultTitle(result, title);

    if (result.payload.tail && result.payload.tailOffsetIndex > 0) {
      this.#fillTailSuggestionPrefix(item, result);
      title.setAttribute("aria-label", result.payload.suggestion);
      item.toggleAttribute("tail-suggestion", true);
    } else {
      item.removeAttribute("tail-suggestion");
      title.removeAttribute("aria-label");
    }

    this.#updateOverflowTooltip(
      title,
      result.getDisplayableValueAndHighlights("title").value
    );

    let tagsContainer = item._elements.get("tagsContainer");
    if (tagsContainer) {
      tagsContainer.textContent = "";

      let { value: tags, highlights } = result.getDisplayableValueAndHighlights(
        "tags",
        {
          tokens: this.#queryContext.tokens,
        }
      );

      if (tags?.length) {
        tagsContainer.append(
          ...tags.map((tag, i) => {
            const element = this.#createElement("span");
            element.className = "urlbarView-tag";
            lazy.UrlbarUtils.addTextContentWithHighlights(
              element,
              tag,
              highlights[i]
            );
            return element;
          })
        );
      }
    }

    let action = item._elements.get("action");
    let actionSetter = null;
    let isVisitAction = false;
    let setURL = false;
    let isRowSelectable = true;
    switch (result.type) {
      case lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
        // Hide chiclet when showing secondaryActions.
        if (!lazy.UrlbarPrefs.get("secondaryActions.switchToTab")) {
          actionSetter = () => {
            this.#setSwitchTabActionChiclet(result, action);
          };
        }
        setURL = true;
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
        actionSetter = () => {
          this.#l10nCache.removeElementL10n(action);
          action.textContent = result.payload.device;
        };
        setURL = true;
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.AI_CHAT:
        actionSetter = () => {
          this.#l10nCache.setElementL10n(action, {
            id: "urlbar-result-action-ai-chat",
          });
        };
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.SEARCH:
        if (
          result.payload.suggestionObject?.suggestionType == "important_dates"
        ) {
          // Don't show action for important date results because clicking them
          // searches for the name of the event which is in the description and
          // not the title.
          break;
        }
        if (result.payload.inPrivateWindow) {
          if (result.payload.isPrivateEngine) {
            actionSetter = () => {
              this.#l10nCache.setElementL10n(action, {
                id: "urlbar-result-action-search-in-private-w-engine",
                args: { engine: result.payload.engine },
              });
            };
          } else {
            actionSetter = () => {
              this.#l10nCache.setElementL10n(action, {
                id: "urlbar-result-action-search-in-private",
              });
            };
          }
        } else if (result.providerName == "UrlbarProviderTabToSearch") {
          actionSetter = () => {
            this.#l10nCache.setElementL10n(action, {
              id: result.payload.isGeneralPurposeEngine
                ? "urlbar-result-action-tabtosearch-web"
                : "urlbar-result-action-tabtosearch-other-engine",
              args: { engine: result.payload.engine },
            });
          };
        } else if (!result.payload.providesSearchMode) {
          actionSetter = () => {
            this.#l10nCache.setElementL10n(action, {
              id: "urlbar-result-action-search-w-engine",
              args: { engine: result.payload.engine },
            });
          };
        }
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.KEYWORD:
        isVisitAction = result.payload.input.trim() == result.payload.keyword;
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.OMNIBOX:
        actionSetter = () => {
          this.#l10nCache.removeElementL10n(action);
          action.textContent = result.payload.content;
        };
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.TIP:
        isRowSelectable = false;
        break;
      case lazy.UrlbarUtils.RESULT_TYPE.URL:
        if (result.providerName == "UrlbarProviderClipboard") {
          actionSetter = () => {
            this.#l10nCache.setElementL10n(action, {
              id: "urlbar-result-action-visit-from-clipboard",
            });
          };
          title.toggleAttribute("is-url", true);

          let label = { id: "urlbar-result-action-visit-from-clipboard" };
          this.#l10nCache.ensure(label).then(() => {
            let { value } = this.#l10nCache.get(label);

            // We don't have to unset these attributes because, excluding heuristic results,
            // we never reuse results from different providers. Thus clipboard results can
            // only be reused by other clipboard results.
            title.setAttribute("aria-label", `${value}, ${title.innerText}`);
            action.setAttribute("aria-hidden", "true");
          });
          break;
        }
      // fall-through
      default:
        if (
          result.heuristic &&
          result.payload.url &&
          result.providerName != "UrlbarProviderHistoryUrlHeuristic" &&
          !result.autofill?.noVisitAction
        ) {
          isVisitAction = true;
        } else if (
          (result.providerName != lazy.UrlbarProviderQuickSuggest.name ||
            result.payload.shouldShowUrl) &&
          !result.payload.providesSearchMode
        ) {
          setURL = true;
        }
        break;
    }

    this.#setRowSelectable(item, isRowSelectable);

    action?.toggleAttribute(
      "slide-in",
      result.providerName == "UrlbarProviderTabToSearch"
    );

    item.toggleAttribute("pinned", !!result.payload.isPinned);

    let sponsored =
      result.payload.isSponsored &&
      result.type != lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH &&
      result.providerName != lazy.UrlbarProviderQuickSuggest.name;
    item.toggleAttribute("sponsored", !!sponsored);
    if (sponsored) {
      actionSetter = () => {
        this.#l10nCache.setElementL10n(action, {
          id: "urlbar-result-action-sponsored",
        });
      };
    }

    if (
      result.isRichSuggestion ||
      Services.prefs.getBoolPref("browser.nova.enabled", false)
    ) {
      this.#updateRowForRichSuggestion(item, result);
    }

    item.toggleAttribute("has-url", setURL);
    let url = item._elements.get("url");
    if (setURL) {
      let { value: displayedUrl, highlights } =
        result.getDisplayableValueAndHighlights("url", {
          tokens: this.#queryContext.tokens,
          isURL: true,
        });
      this.#updateOverflowTooltip(url, displayedUrl);

      if (lazy.UrlbarUtils.isTextDirectionRTL(displayedUrl, this.window)) {
        // Stripping the url prefix may change the initial text directionality,
        // causing parts of it to jump to the end. To prevent that we insert a
        // LRM character in place of the prefix.
        displayedUrl = "\u200e" + displayedUrl;
        highlights = this.#offsetHighlights(highlights, 1);
      }
      lazy.UrlbarUtils.addTextContentWithHighlights(
        url,
        displayedUrl,
        highlights
      );
    } else {
      url.textContent = "";
      this.#updateOverflowTooltip(url, "");
    }

    let explanation = item._elements.get("explanation");
    if (explanation && setURL && result.payload.lastVisit) {
      item.toggleAttribute("has-explanation", true);
      let { isRelative, formattedDate } = lazy.UrlbarUtils.formatDate(
        new Date(result.payload.lastVisit)
      );
      if (isRelative) {
        this.document.l10n.setAttributes(
          explanation,
          "urlbar-result-explanation-last-visited-relative",
          { date: formattedDate }
        );
      } else {
        this.document.l10n.setAttributes(
          explanation,
          "urlbar-result-explanation-last-visited-absolute",
          { date: formattedDate }
        );
      }
    } else {
      if (explanation) {
        explanation.removeAttribute("data-l10n-id");
        explanation.removeAttribute("data-l10n-args");
        explanation.textContent = "";
      }
      item.toggleAttribute("has-explanation", false);
    }

    title.toggleAttribute("is-url", isVisitAction);
    if (isVisitAction) {
      actionSetter = () => {
        this.#l10nCache.setElementL10n(action, {
          id: "urlbar-result-action-visit",
        });
      };
    }

    item.toggleAttribute("has-action", actionSetter);
    if (actionSetter) {
      actionSetter();
      item._originalActionSetter = actionSetter;
    } else {
      item._originalActionSetter = () => {
        this.#l10nCache.removeElementL10n(action);
        action.textContent = "";
      };
      item._originalActionSetter();
    }

    if (!title.hasAttribute("is-url")) {
      title.setAttribute("dir", "auto");
    } else {
      title.removeAttribute("dir");
    }
  }

  #setRowSelectable(item, isRowSelectable) {
    item.toggleAttribute("row-selectable", isRowSelectable);
    item._content.toggleAttribute("selectable", isRowSelectable);

    // Set or remove role="option" on the inner. "option" should be set iff the
    // row is selectable. Some providers may set a different role if the inner
    // is not selectable, so when removing it, only do so if it's "option".
    if (isRowSelectable) {
      item._content.setAttribute("role", "option");
    } else if (item._content.getAttribute("role") == "option") {
      item._content.removeAttribute("role");
    }
  }

  #iconForResult(result, iconUrlOverride = null) {
    if (
      result.source == lazy.UrlbarUtils.RESULT_SOURCE.HISTORY &&
      (result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH ||
        result.type == lazy.UrlbarUtils.RESULT_TYPE.KEYWORD)
    ) {
      return lazy.UrlbarUtils.ICON.HISTORY;
    }

    if (iconUrlOverride) {
      return iconUrlOverride;
    }

    if (result.payload.icon) {
      return result.payload.icon;
    }
    if (result.payload.iconBlob) {
      let blobUrl = this.#getBlobUrlForResult(result, result.payload.iconBlob);
      if (blobUrl) {
        return blobUrl;
      }
    }

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
      result.payload.trending
    ) {
      return lazy.UrlbarUtils.ICON.TRENDING;
    }

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH ||
      result.type == lazy.UrlbarUtils.RESULT_TYPE.KEYWORD
    ) {
      return lazy.UrlbarUtils.ICON.SEARCH_GLASS;
    }

    return lazy.UrlbarUtils.ICON.DEFAULT;
  }

  #getBlobUrlForResult(result, blob) {
    // For some Suggest results, `url` is a value that is modified at query time
    // and that is potentially unique per query. For example, it might contain
    // timestamps or query-related search params. Those results will also have
    // an `originalUrl` that is the unmodified URL, and it should be used as the
    // map key.
    let resultUrl = result.payload.originalUrl || result.payload.url;
    if (resultUrl) {
      let blobUrl = this.#blobUrlsByResultUrl?.get(resultUrl);
      if (!blobUrl) {
        blobUrl = URL.createObjectURL(blob);
        // Since most users will not trigger results with blob icons, we
        // create this map lazily.
        this.#blobUrlsByResultUrl ||= new Map();
        this.#blobUrlsByResultUrl.set(resultUrl, blobUrl);
      }
      return blobUrl;
    }
    return null;
  }

  async #updateRowForDynamicType(item, result) {
    item.setAttribute("dynamicType", result.payload.dynamicType);

    let idsByName = new Map();
    for (let [name, node] of item._elements) {
      node.id = `${item.id}-${name}`;
      idsByName.set(name, node.id);
    }

    // Get the view update from the result's provider.
    let provider = this.#providersManager.getProvider(result.providerName);
    let viewUpdate = await provider.getViewUpdate(result, idsByName);
    if (item.result != result) {
      return;
    }

    // Update each node in the view by name.
    for (let [nodeName, update] of Object.entries(viewUpdate)) {
      if (!update) {
        continue;
      }
      let node = item.querySelector(`#${item.id}-${nodeName}`);
      this.#updateElementForDynamicType(node, update, item, result);
      if (update.style) {
        for (let [styleName, value] of Object.entries(update.style)) {
          if (styleName.includes("-")) {
            // Expect hyphen-case. e.g. "background-image", "--a-variable".
            node.style.setProperty(styleName, value);
          } else {
            // Expect camel-case. e.g. "backgroundImage"
            // NOTE: If want to define the variable, please use hyphen-case.
            node.style[styleName] = value;
          }
        }
      }
      if (update.l10n) {
        this.#l10nCache.setElementL10n(node, update.l10n);
      } else if (update.hasOwnProperty("textContent")) {
        this.#l10nCache.removeElementL10n(node);
        lazy.UrlbarUtils.addTextContentWithHighlights(
          node,
          update.textContent,
          update.highlights
        );
      }
    }
  }

  #updateRowForRichSuggestion(item, result) {
    // The "rich-suggestion" attribute isn't used in Nova.
    item.toggleAttribute(
      "rich-suggestion",
      !Services.prefs.getBoolPref("browser.nova.enabled", false)
    );

    this.#setRowSelectable(
      item,
      result.type != lazy.UrlbarUtils.RESULT_TYPE.TIP
    );

    let favicon = item._elements.get("favicon");
    if (result.richSuggestionIconSize) {
      item.setAttribute("icon-size", result.richSuggestionIconSize);
      favicon.setAttribute("icon-size", result.richSuggestionIconSize);
    } else {
      item.removeAttribute("icon-size");
      favicon.removeAttribute("icon-size");
    }

    if (result.richSuggestionIconVariation) {
      favicon.setAttribute(
        "icon-variation",
        result.richSuggestionIconVariation
      );
    } else {
      favicon.removeAttribute("icon-variation");
    }

    let description = item._elements.get("description");
    if (result.payload.descriptionL10n) {
      this.#l10nCache.setElementL10n(
        description,
        result.payload.descriptionL10n
      );

      if (result.payload.descriptionLearnMoreTopic) {
        let learnMoreLink = description.querySelector(
          "[data-l10n-name=learn-more-link]"
        );
        if (learnMoreLink) {
          learnMoreLink.dataset.url = this.window.getHelpLinkURL(
            result.payload.descriptionLearnMoreTopic
          );
        } else {
          console.warn("learn-more-link was not found");
        }
      }
    } else {
      this.#l10nCache.removeElementL10n(description);
      if (result.payload.description) {
        description.textContent = result.payload.description;
      }
    }

    let bottom = item._elements.get("bottom");
    if (result.payload.bottomTextL10n) {
      this.#l10nCache.setElementL10n(bottom, result.payload.bottomTextL10n);
    } else {
      this.#l10nCache.removeElementL10n(bottom);
    }
  }

  #updateRowContentForBottomUrl(item, result) {
    item.classList.add("with-bottom-url");

    // The "rich-suggestion" attribute isn't used in Nova.
    item.toggleAttribute(
      "rich-suggestion",
      !Services.prefs.getBoolPref("browser.nova.enabled", false)
    );

    item.setAttribute(
      "type",
      lazy.UrlbarUtils.searchEngagementTelemetryType(result)
    );
    item.toggleAttribute("sponsored", result.payload.isSponsored);

    this.#setRowSelectable(item, true);

    let favicon = item._elements.get("favicon");
    favicon.src = this.#iconForResult(result);
    if (result.richSuggestionIconSize) {
      item.setAttribute("icon-size", result.richSuggestionIconSize);
      favicon.setAttribute("icon-size", result.richSuggestionIconSize);
    } else {
      item.removeAttribute("icon-size");
      favicon.removeAttribute("icon-size");
    }

    let title = item._elements.get("title");
    this.#setResultTitle(result, title);

    let subtitle = item._elements.get("subtitle");
    if (result.payload.subtitleL10n) {
      this.#l10nCache.setElementL10n(subtitle, result.payload.subtitleL10n);
    } else {
      this.#l10nCache.removeElementL10n(subtitle);
      if (result.payload.subtitle) {
        subtitle.textContent = result.payload.subtitle;
      }
    }

    let description = item._elements.get("description");
    description.textContent = result.payload.description;

    let bottomLabel = item._elements.get("bottomLabel");
    this.#l10nCache.setElementL10n(bottomLabel, result.payload.bottomTextL10n);

    let url = item._elements.get("url");
    url.textContent = lazy.UrlbarUtils.prepareUrlForDisplay(result.payload.url);
  }

  /**
   * Performs a final pass over all rows in the view after a view update, stale
   * rows are removed, and other changes to the number of rows. Sets `rowIndex`
   * on each result, updates row labels, and performs other tasks that must be
   * deferred until all rows have been updated.
   */
  #updateIndices() {
    this.visibleResults = [];

    // `lastVisibleLabel` is the l10n object of the last-seen visible row label
    // as we iterate through the rows. When we encounter a row whose label is
    // different from `lastVisibleLabel`, we make that row's label visible and
    // it becomes the new `lastVisibleLabel`. We hide the labels for all other
    // rows, so no label will appear adjacent to itself. (A label may appear
    // more than once, but there will be at least one different label in
    // between.) Each row's label is determined by `#rowLabel()`.
    let lastVisibleLabel = null;

    // Keeps track of whether we've seen only the heuristic or search suggestions.
    let seenOnlyHeuristicOrSearchSuggestions = true;

    for (let i = 0; i < this.#rows.children.length; i++) {
      let item = this.#rows.children[i];
      let { result } = item;
      result.rowIndex = i;

      let visible = this.#isElementVisible(item);
      if (visible) {
        this.visibleResults.push(result);
        seenOnlyHeuristicOrSearchSuggestions &&=
          result.heuristic ||
          (result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
            result.payload.suggestion);
        if (result.exposureTelemetry) {
          this.controller.engagementEvent.addExposure(
            result,
            this.#queryContext
          );
        }
      }

      lastVisibleLabel = this.#updateRowLabel(
        item,
        visible,
        lastVisibleLabel,
        seenOnlyHeuristicOrSearchSuggestions
      );
    }

    let selectableElement = this.getFirstSelectableElement();
    let uiIndex = 0;
    while (selectableElement) {
      selectableElement.elementIndex = uiIndex++;
      selectableElement = this.#getNextSelectableElement(selectableElement);
    }

    if (this.visibleResults.length) {
      this.panel.removeAttribute("noresults");
    } else {
      this.panel.setAttribute("noresults", "true");
    }
  }

  /**
   * Sets or removes the group label from a row. Designed to be called
   * iteratively over each row.
   *
   * @param {Element} item
   *   A row in the view.
   * @param {boolean} isItemVisible
   *   Whether the row is visible. This can be computed by the method itself,
   *   but it's a parameter as an optimization since the caller is expected to
   *   know it.
   * @param {object} lastVisibleLabel
   *   The last-seen visible group label during row iteration.
   * @param {boolean} seenOnlyHeuristicOrSearchSuggestions
   *   Whether the iteration has encountered only the heuristic or search
   *   suggestions so far.
   * @returns {object}
   *   The l10n object for the new last-visible label. If the row's label should
   *   be visible, this will be that label. Otherwise it will be the passed-in
   *   `lastVisibleLabel`.
   */
  #updateRowLabel(
    item,
    isItemVisible,
    lastVisibleLabel,
    seenOnlyHeuristicOrSearchSuggestions
  ) {
    let label = null;
    if (
      isItemVisible &&
      // Show the search suggestions label only if there are other visible
      // results before this one that aren't the heuristic or suggestions.
      !(
        item.result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
        item.result.payload.suggestion &&
        seenOnlyHeuristicOrSearchSuggestions
      )
    ) {
      label = this.#rowLabel(item);
    }

    // When the row-inner is selected, screen readers won't naturally read the
    // label because it's a pseudo-element of the row, not the row-inner. To
    // compensate, for rows that have labels we add an element to the row-inner
    // with `aria-label` and no text content. Rows that don't have labels won't
    // have this element.
    let groupAriaLabel = item._elements.get("groupAriaLabel");

    if (
      !label ||
      item.result.hideRowLabel ||
      lazy.ObjectUtils.deepEqual(label, lastVisibleLabel)
    ) {
      this.#l10nCache.removeElementL10n(item, { attribute: "label" });
      if (groupAriaLabel) {
        groupAriaLabel.remove();
        item._elements.delete("groupAriaLabel");
      }
      return lastVisibleLabel;
    }

    this.#l10nCache.setElementL10n(item, {
      attribute: "label",
      id: label.id,
      args: label.args,
    });

    if (!groupAriaLabel) {
      groupAriaLabel = this.#createElement("span");
      groupAriaLabel.className = "urlbarView-group-aria-label";
      item._content.insertBefore(groupAriaLabel, item._content.firstChild);
      item._elements.set("groupAriaLabel", groupAriaLabel);
    }

    // `aria-label` must be a string, not an l10n ID, so first fetch the
    // localized value and then set it as the attribute. There's no relevant
    // aria attribute that uses l10n IDs.
    this.#l10nCache.ensure(label).then(() => {
      let message = this.#l10nCache.get(label);
      groupAriaLabel.setAttribute("aria-label", message?.attributes.label);
    });

    return label;
  }

  /**
   * Returns the group label to use for a row. Designed to be called iteratively
   * over each row.
   *
   * @param {Element} row
   *   A row in the view.
   * @returns {object}
   *   If the current row should not have a label, returns null. Otherwise
   *   returns an l10n object for the label's l10n string: `{ id, args }`
   */
  #rowLabel(row) {
    if (!lazy.UrlbarPrefs.get("groupLabels.enabled")) {
      return null;
    }

    if (row.result.rowLabel) {
      return row.result.rowLabel;
    }

    let engineName =
      row.result.payload.engine || lazy.SearchService.defaultEngine.name;

    if (row.result.payload.trending) {
      return {
        id: "urlbar-group-trending",
        args: { engine: engineName },
      };
    }

    if (row.result.providerName == "UrlbarProviderRecentSearches") {
      return { id: "urlbar-group-recent-searches" };
    }

    if (row.result.providerName == lazy.UrlbarProviderQuickSuggest.name) {
      return row.result.isBestMatch
        ? null
        : { id: "urlbar-group-firefox-suggest" };
    }

    if (row.result.isBestMatch) {
      return { id: "urlbar-group-best-match" };
    }

    // Show "Shortcuts" if there's another result before that group.
    if (
      row.result.providerName == "UrlbarProviderTopSites" &&
      this.#queryContext.results[0].providerName != "UrlbarProviderTopSites"
    ) {
      return { id: "urlbar-group-shortcuts" };
    }

    if (!this.#queryContext?.searchString || row.result.heuristic) {
      return null;
    }

    switch (row.result.type) {
      case lazy.UrlbarUtils.RESULT_TYPE.KEYWORD:
      case lazy.UrlbarUtils.RESULT_TYPE.REMOTE_TAB:
      case lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
      case lazy.UrlbarUtils.RESULT_TYPE.URL:
        return { id: "urlbar-group-firefox-suggest" };
      case lazy.UrlbarUtils.RESULT_TYPE.SEARCH:
        return {
          id: "urlbar-group-search-suggestions",
          args: { engine: engineName },
        };
      case lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC:
        if (row.result.providerName == "quickactions") {
          return { id: "urlbar-group-quickactions" };
        }
        break;
    }

    return null;
  }

  #setRowVisibility(row, visible) {
    row.toggleAttribute("hidden", !visible);

    if (
      !visible &&
      row.result.type != lazy.UrlbarUtils.RESULT_TYPE.TIP &&
      row.result.type != lazy.UrlbarUtils.RESULT_TYPE.DYNAMIC
    ) {
      // Reset the overflow state of elements that can overflow in case their
      // content changes while they're hidden. When making the row visible
      // again, we'll get new overflow events if needed.
      this.#setElementOverflowing(row._elements.get("title"), false);
      this.#setElementOverflowing(row._elements.get("url"), false);
      let tagsContainer = row._elements.get("tagsContainer");
      if (tagsContainer) {
        this.#setElementOverflowing(tagsContainer, false);
      }
    }
  }

  async #ariaNotifyLocalizedString(element, l10nId, l10nArgs) {
    let message = await this.document.l10n.formatValue(l10nId, l10nArgs);
    element.ariaNotify(message);
  }

  /**
   * Returns true if the given element and its row are both visible.
   *
   * @param {Element} element
   *   An element in the view.
   * @returns {boolean}
   *   True if the given element and its row are both visible.
   */
  #isElementVisible(element) {
    if (!element || element.style.display == "none") {
      return false;
    }
    let row = this.#getRowFromElement(element);
    return row && !row.hasAttribute("hidden");
  }

  #removeStaleRows() {
    let row = this.#rows.lastElementChild;
    while (row) {
      let next = row.previousElementSibling;
      if (row.hasAttribute("stale")) {
        row.remove();
      } else {
        this.#setRowVisibility(row, true);
      }
      row = next;
    }
    this.#updateIndices();

    // Reset actionmode if we left the actions search mode.
    // We do this after updating the result rows to ensure the attribute stays
    // active the entire time the actions list is visible.

    // this.input.searchMode updates early, so only checking it would cause a
    // flicker, and the first visible result's source being an action doesn't
    // necessarily imply we are in actions mode, therefore we should check both.
    if (
      this.input.searchMode?.source != lazy.UrlbarUtils.RESULT_SOURCE.ACTIONS &&
      this.visibleResults[0]?.source != lazy.UrlbarUtils.RESULT_SOURCE.ACTIONS
    ) {
      this.#rows.toggleAttribute("actionmode", false);
    }

    // Accept tentative exposures. This is analogous to unhiding the
    // hypothetical non-stale hidden rows of hidden-exposure results.
    this.controller.engagementEvent.acceptTentativeExposures();
  }

  #startRemoveStaleRowsTimer() {
    this.#removeStaleRowsTimer = this.window.setTimeout(() => {
      this.#removeStaleRowsTimer = null;
      this.#removeStaleRows();
    }, UrlbarView.removeStaleRowsTimeout);
  }

  #cancelRemoveStaleRowsTimer() {
    if (this.#removeStaleRowsTimer) {
      this.window.clearTimeout(this.#removeStaleRowsTimer);
      this.#removeStaleRowsTimer = null;
    }
  }

  #selectElement(
    element,
    { updateInput = true, setAccessibleFocus = true } = {}
  ) {
    if (element && !element.matches(KEYBOARD_SELECTABLE_ELEMENT_SELECTOR)) {
      throw new Error("Element is not keyboard-selectable");
    }

    if (this.#selectedElement) {
      this.#selectedElement.toggleAttribute("selected", false);
      this.#selectedElement.removeAttribute("aria-selected");
      let row = this.#getSelectedRow();
      row?.toggleAttribute("selected", false);
      row?.toggleAttribute("descendant-selected", false);
    }
    let row = this.#getRowFromElement(element);
    if (element) {
      element.toggleAttribute("selected", true);
      element.setAttribute("aria-selected", "true");
      if (row?.hasAttribute("row-selectable")) {
        row?.toggleAttribute("selected", true);
      }
      if (element != row) {
        row?.toggleAttribute("descendant-selected", true);
      }
    }

    let result = row?.result;
    let provider = this.#providersManager.getProvider(result?.providerName);
    if (provider) {
      provider.tryMethod("onBeforeSelection", result, element);
    }

    this.#setAccessibleFocus(setAccessibleFocus && element);
    this.#rawSelectedElement = element;

    if (updateInput) {
      let urlOverride = null;
      if (element?.classList?.contains("urlbarView-button")) {
        // Clear the input when a button is selected.
        urlOverride = "";
      }
      this.input.setValueFromResult({ result, urlOverride, element });
    } else {
      this.input.setResultForCurrentValue(result);
    }

    if (provider) {
      provider.tryMethod("onSelection", result, element);
    }
  }

  /**
   * Returns the element closest to the given element that can be
   * selected/picked.  If the element itself can be selected, it's returned.  If
   * there is no such element, null is returned.
   *
   * @param {Element} element
   *   An element in the view.
   * @param {object} [options]
   *   Options object.
   * @param {boolean} [options.byMouse]
   *   If true, include elements that are only selectable by mouse.
   * @returns {Element}
   *   The closest element that can be picked including the element itself, or
   *   null if there is no such element.
   */
  #getClosestSelectableElement(element, { byMouse = false } = {}) {
    let closest = element.closest(
      byMouse
        ? SELECTABLE_ELEMENT_SELECTOR
        : KEYBOARD_SELECTABLE_ELEMENT_SELECTOR
    );
    if (closest && this.#isElementVisible(closest)) {
      return closest;
    }
    // When clicking on a gap within a row or on its border or padding, treat
    // this as if the main part was clicked.
    if (
      element.classList.contains("urlbarView-row") &&
      element.hasAttribute("row-selectable")
    ) {
      return element._content;
    }
    return null;
  }

  /**
   * Returns true if the given element is keyboard-selectable.
   *
   * @param {Element} element
   *   The element to test.
   * @returns {boolean}
   *   True if the element is selectable and false if not.
   */
  #isSelectableElement(element) {
    return this.#getClosestSelectableElement(element) == element;
  }

  /**
   * Returns the first keyboard-selectable element in the view.
   *
   * @returns {Element}
   *   The first selectable element in the view.
   */
  getFirstSelectableElement() {
    let element = this.#rows.firstElementChild;
    if (element && !this.#isSelectableElement(element)) {
      element = this.#getNextSelectableElement(element);
    }
    return element;
  }

  /**
   * Returns the last keyboard-selectable element in the view.
   *
   * @returns {Element}
   *   The last selectable element in the view.
   */
  getLastSelectableElement() {
    let element = this.#rows.lastElementChild;
    if (element && !this.#isSelectableElement(element)) {
      element = this.#getPreviousSelectableElement(element);
    }
    return element;
  }

  /**
   * Returns the next keyboard-selectable element after the given element.  If
   * the element is the last selectable element, returns null.
   *
   * @param {Element} element
   *   An element in the view.
   * @returns {Element}
   *   The next selectable element after `element` or null if `element` is the
   *   last selectable element.
   */
  #getNextSelectableElement(element) {
    let row = this.#getRowFromElement(element);
    if (!row) {
      return null;
    }

    let next = row.nextElementSibling;
    let selectables = this.#getKeyboardSelectablesInRow(row);
    if (selectables.length) {
      let index = selectables.indexOf(element);
      if (index < selectables.length - 1) {
        next = selectables[index + 1];
      }
    }

    if (next && !this.#isSelectableElement(next)) {
      next = this.#getNextSelectableElement(next);
    }

    return next;
  }

  /**
   * Returns the previous keyboard-selectable element before the given element.
   * If the element is the first selectable element, returns null.
   *
   * @param {Element} element
   *   An element in the view.
   * @returns {Element}
   *   The previous selectable element before `element` or null if `element` is
   *   the first selectable element.
   */
  #getPreviousSelectableElement(element) {
    let row = this.#getRowFromElement(element);
    if (!row) {
      return null;
    }

    let previous = row.previousElementSibling;
    let selectables = this.#getKeyboardSelectablesInRow(row);
    if (selectables.length) {
      let index = selectables.indexOf(element);
      if (index < 0) {
        previous = selectables[selectables.length - 1];
      } else if (index > 0) {
        previous = selectables[index - 1];
      }
    }

    if (previous && !this.#isSelectableElement(previous)) {
      previous = this.#getPreviousSelectableElement(previous);
    }

    return previous;
  }

  #getKeyboardSelectablesInRow(row) {
    let selectables = [
      ...row.querySelectorAll(KEYBOARD_SELECTABLE_ELEMENT_SELECTOR),
    ];

    // Sort links last. This assumes that any links in the row are informational
    // and should be deprioritized with regard to selection compared to buttons
    // and other elements.
    selectables.sort(
      (a, b) => Number(a.localName == "a") - Number(b.localName == "a")
    );

    return selectables;
  }

  /**
   * Returns the currently selected row. Useful when this.#selectedElement may
   * be a non-row element, such as a descendant element of RESULT_TYPE.TIP.
   *
   * @returns {Element}
   *   The currently selected row, or ancestor row of the currently selected
   *   item.
   */
  #getSelectedRow() {
    return this.#getRowFromElement(this.#selectedElement);
  }

  /**
   * @param {Element} element
   *   An element that is potentially a row or descendant of a row.
   * @returns {Element}
   *   The row containing `element`, or `element` itself if it is a row.
   */
  #getRowFromElement(element) {
    return element?.closest(".urlbarView-row");
  }

  #setAccessibleFocus(item) {
    if (item) {
      if (!item.id) {
        // Assign an id to dynamic actions as required by aria-activedescendant.
        item.id = getUniqueId("aria-activedescendant-target-");
      }
      this.input.inputField.setAttribute("aria-activedescendant", item.id);
    } else {
      this.input.inputField.removeAttribute("aria-activedescendant");
    }
  }

  /**
   * Sets `result`'s title in `titleNode`'s DOM.
   *
   * @param {UrlbarResult} result
   *   The result for which the title is being set.
   * @param {Element} titleNode
   *   The DOM node for the result's tile.
   */
  #setResultTitle(result, titleNode) {
    if (result.payload.titleL10n) {
      this.#l10nCache.setElementL10n(titleNode, result.payload.titleL10n);
      return;
    }

    // TODO: `text` is intended only for WebExtensions. We should remove it and
    // the WebExtensions urlbar API since we're no longer using it.
    if (result.payload.text) {
      titleNode.textContent = result.payload.text;
      return;
    }

    if (result.payload.providesSearchMode) {
      if (result.type == lazy.UrlbarUtils.RESULT_TYPE.RESTRICT) {
        let localSearchMode =
          result.payload.l10nRestrictKeywords[0].toLowerCase();
        let keywords = result.payload.l10nRestrictKeywords
          .map(keyword => `@${keyword.toLowerCase()}`)
          .join(", ");

        this.#l10nCache.setElementL10n(titleNode, {
          id: "urlbar-result-search-with-local-search-mode",
          args: {
            keywords,
            localSearchMode,
          },
        });
      } else if (
        result.providerName == "UrlbarProviderTokenAliasEngines" &&
        lazy.UrlbarPrefs.getScotchBonnetPref(
          "searchRestrictKeywords.featureGate"
        )
      ) {
        this.#l10nCache.setElementL10n(titleNode, {
          id: "urlbar-result-search-with-engine-keywords",
          args: {
            keywords: result.payload.keywords,
            engine: result.payload.engine,
          },
        });
      } else {
        // Keyword offers are the only result that require a localized title.
        // We localize the title instead of using the action text as a title
        // because some keyword offer results use both a title and action text
        // (e.g., tab-to-search).
        this.#l10nCache.setElementL10n(titleNode, {
          id: "urlbar-result-action-search-w-engine",
          args: { engine: result.payload.engine },
        });
      }

      return;
    }

    this.#l10nCache.removeElementL10n(titleNode);

    let titleAndHighlights = result.getDisplayableValueAndHighlights("title", {
      tokens: this.#queryContext.tokens,
    });
    lazy.UrlbarUtils.addTextContentWithHighlights(
      titleNode,
      titleAndHighlights.value,
      titleAndHighlights.highlights
    );
  }

  /**
   * Offsets all highlight ranges by a given amount.
   *
   * @param {Array} highlights The highlights which should be offset.
   * @param {int} startOffset
   *    The number by which we want to offset the highlights range starts.
   * @returns {Array} The offset highlights.
   */
  #offsetHighlights(highlights, startOffset) {
    return highlights.map(highlight => [
      highlight[0] + startOffset,
      highlight[1],
    ]);
  }

  /**
   * Sets the content of the 'Switch To Tab' chiclet.
   *
   * @param {UrlbarResult} result
   *   The result for which the content is being set.
   * @param {Element} actionNode
   *   The DOM node for the result's action.
   */
  #setSwitchTabActionChiclet(result, actionNode) {
    actionNode.classList.add("urlbarView-switchToTab");

    let contextualIdentityAction = actionNode.parentNode.querySelector(
      ".action-contextualidentity"
    );

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH &&
      lazy.UrlbarProviderOpenTabs.isContainerUserContextId(
        result.payload.userContextId
      )
    ) {
      if (!contextualIdentityAction) {
        contextualIdentityAction = actionNode.cloneNode(true);
        contextualIdentityAction.classList.add("action-contextualidentity");
        this.#l10nCache.removeElementL10n(contextualIdentityAction);
        actionNode.parentNode.insertBefore(
          contextualIdentityAction,
          actionNode
        );
      }

      this.#addContextualIdentityToSwitchTabChiclet(
        result,
        contextualIdentityAction
      );
    } else {
      contextualIdentityAction?.remove();
    }

    let tabGroupAction = actionNode.parentNode.querySelector(
      ".urlbarView-tabGroup"
    );

    if (
      result.type == lazy.UrlbarUtils.RESULT_TYPE.TAB_SWITCH &&
      result.payload.tabGroup
    ) {
      if (!tabGroupAction) {
        tabGroupAction = actionNode.cloneNode(true);
        this.#l10nCache.removeElementL10n(tabGroupAction);
        actionNode.parentNode.insertBefore(tabGroupAction, actionNode);
      }

      this.#addGroupToSwitchTabChiclet(result, tabGroupAction);
    } else {
      tabGroupAction?.remove();
    }
    let splitview = this.window.gBrowser.selectedTab.splitview;
    let shouldMoveTabToSplitView =
      splitview &&
      !splitview.tabs.some(
        tab => tab.linkedBrowser.currentURI.spec === result.payload.url
      );
    this.#l10nCache.setElementL10n(actionNode, {
      id: shouldMoveTabToSplitView
        ? "urlbar-result-action-move-tab-to-split-view"
        : "urlbar-result-action-switch-tab",
    });
  }

  #addContextualIdentityToSwitchTabChiclet(result, actionNode) {
    let label = lazy.ContextualIdentityService.getUserContextLabel(
      result.payload.userContextId
    );
    // To avoid flicker don't update the label unless necessary.
    if (
      actionNode.classList.contains("urlbarView-userContext") &&
      label &&
      actionNode == label
    ) {
      return;
    }
    actionNode.innerHTML = "";
    let identity = lazy.ContextualIdentityService.getPublicIdentityFromId(
      result.payload.userContextId
    );
    if (identity) {
      actionNode.classList.add("urlbarView-userContext");
      actionNode.classList.remove("urlbarView-switchToTab");
      if (identity.color) {
        actionNode.className = actionNode.className.replace(
          /identity-color-\w*/g,
          ""
        );
        actionNode.classList.add("identity-color-" + identity.color);
      }

      let textModeLabel = this.#createElement("div");
      textModeLabel.classList.add("urlbarView-userContext-textMode");

      if (label) {
        textModeLabel.innerText = label;
        actionNode.appendChild(textModeLabel);

        let iconModeLabel = this.#createElement("div");
        iconModeLabel.classList.add("urlbarView-userContext-iconMode");
        actionNode.appendChild(iconModeLabel);
        if (identity.icon) {
          let userContextIcon = this.#createElement("img");
          userContextIcon.classList.add("urlbarView-userContext-icon");
          userContextIcon.setAttribute("alt", label);
          userContextIcon.src =
            "resource://usercontext-content/" + identity.icon + ".svg";
          iconModeLabel.appendChild(userContextIcon);
        }
        actionNode.setAttribute("tooltiptext", label);
      }
    }
  }

  #addGroupToSwitchTabChiclet(result, actionNode) {
    const group = this.window.gBrowser.getTabGroupById(result.payload.tabGroup);
    if (!group) {
      actionNode.remove();
      return;
    }

    actionNode.classList.add("urlbarView-tabGroup");
    actionNode.classList.remove("urlbarView-switchToTab");

    actionNode.innerHTML = "";
    let fullWidthModeLabel = this.#createElement("div");
    fullWidthModeLabel.classList.add("urlbarView-tabGroup-fullWidthMode");

    let narrowWidthModeLabel = this.#createElement("div");
    narrowWidthModeLabel.classList.add("urlbarView-tabGroup-narrowWidthMode");

    if (group.label) {
      fullWidthModeLabel.textContent = group.label;
      narrowWidthModeLabel.textContent = group.label[0];
    } else {
      this.#l10nCache.setElementL10n(fullWidthModeLabel, {
        id: `urlbar-result-action-tab-group-unnamed`,
      });
    }

    actionNode.appendChild(fullWidthModeLabel);
    actionNode.appendChild(narrowWidthModeLabel);

    actionNode.style.setProperty(
      "--tab-group-color",
      group.style.getPropertyValue("--tab-group-color")
    );
    actionNode.style.setProperty(
      "--tab-group-color-invert",
      group.style.getPropertyValue("--tab-group-color-invert")
    );
    actionNode.style.setProperty(
      "--tab-group-color-pale",
      group.style.getPropertyValue("--tab-group-color-pale")
    );
  }

  /**
   * Adds markup for a tail suggestion prefix to a row.
   *
   * @param {Element} item
   *   The node for the result row.
   * @param {UrlbarResult} result
   *   A UrlbarResult representing a tail suggestion.
   */
  #fillTailSuggestionPrefix(item, result) {
    let tailPrefixStrNode = item._elements.get("tailPrefixStr");
    let tailPrefixStr = result.payload.suggestion.substring(
      0,
      result.payload.tailOffsetIndex
    );
    tailPrefixStrNode.textContent = tailPrefixStr;

    let tailPrefixCharNode = item._elements.get("tailPrefixChar");
    tailPrefixCharNode.textContent = result.payload.tailPrefix;
  }

  #enableOrDisableRowWrap() {
    let wrap = getBoundsWithoutFlushing(this.input).width < 650;
    this.#rows.toggleAttribute("wrap", wrap);
    this.oneOffSearchButtons?.container.toggleAttribute("wrap", wrap);
  }

  /**
   * @param {Element} element
   *   The element
   * @returns {boolean}
   *   Whether we track this element's overflow status in order to fade it out
   *   and add a tooltip when needed.
   */
  #canElementOverflow(element) {
    let { classList } = element;
    return (
      classList.contains("urlbarView-overflowable") ||
      classList.contains("urlbarView-url")
    );
  }

  /**
   * Marks an element as overflowing or not overflowing.
   *
   * @param {Element} element
   *   The element
   * @param {boolean} overflowing
   *   Whether the element is overflowing
   */
  #setElementOverflowing(element, overflowing) {
    element.toggleAttribute("overflow", overflowing);
    this.#updateOverflowTooltip(element);
  }

  /**
   * Sets an overflowing element's tooltip, or removes the tooltip if the
   * element isn't overflowing. Also optionally updates the string that should
   * be used as the tooltip in case of overflow.
   *
   * @param {Element} element
   *   The element
   * @param {string} [tooltip]
   *   The string that should be used in the tooltip. This will be stored and
   *   re-used next time the element overflows.
   */
  #updateOverflowTooltip(element, tooltip) {
    if (typeof tooltip == "string") {
      element._tooltip = tooltip;
    }
    if (element.hasAttribute("overflow") && element._tooltip) {
      element.setAttribute("title", element._tooltip);
    } else {
      element.removeAttribute("title");
    }
  }

  /**
   * If the view is open and showing a single search tip, this method picks it
   * and closes the view.  This counts as an engagement, so this method should
   * only be called due to user interaction.
   *
   * @param {event} event
   *   The user-initiated event for the interaction.  Should not be null.
   * @returns {boolean}
   *   True if this method picked a tip, false otherwise.
   */
  #pickSearchTipIfPresent(event) {
    if (
      !this.isOpen ||
      !this.#queryContext ||
      this.#queryContext.results.length != 1
    ) {
      return false;
    }
    let result = this.#queryContext.results[0];
    if (result.type != lazy.UrlbarUtils.RESULT_TYPE.TIP) {
      return false;
    }
    let buttons = this.#rows.firstElementChild._buttons;
    let tipButton = buttons.get("tip") || buttons.get("0");
    if (!tipButton) {
      throw new Error("Expected a tip button");
    }
    this.input.pickElement(tipButton, event);
    return true;
  }

  /**
   * Caches some l10n strings used by the view. Strings that are already cached
   * are not cached again.
   *
   * Note:
   *   Currently strings are never evicted from the cache, so do not cache
   *   strings whose arguments include the search string or other values that
   *   can cause the cache to grow unbounded. Suitable strings include those
   *   without arguments or those whose arguments depend on a small set of
   *   static values like search engine names.
   */
  async #cacheL10nStrings() {
    let idArgs = [
      ...this.#cacheL10nIDArgsForSearchService(),
      { id: "urlbar-result-action-search-bookmarks" },
      { id: "urlbar-result-action-search-history" },
      { id: "urlbar-result-action-search-in-private" },
      { id: "urlbar-result-action-search-tabs" },
      { id: "urlbar-result-action-switch-tab" },
      { id: "urlbar-result-action-visit" },
      { id: "urlbar-result-action-visit-from-clipboard" },
      { id: "urlbar-result-action-move-tab-to-split-view" },
    ];

    if (lazy.UrlbarPrefs.get("groupLabels.enabled")) {
      idArgs.push({ id: "urlbar-group-firefox-suggest" });
      idArgs.push({ id: "urlbar-group-best-match" });
    }

    let suggestSponsoredEnabled =
      lazy.UrlbarPrefs.get("quickSuggestEnabled") &&
      lazy.UrlbarPrefs.get("suggest.quicksuggest.sponsored");
    if (suggestSponsoredEnabled) {
      idArgs.push({ id: "urlbar-result-action-sponsored" });
    }

    await this.#l10nCache.ensureAll(idArgs);
  }

  /**
   * A helper for l10n string caching that returns `{ id, args }` objects for
   * strings that depend on the search service.
   *
   * @returns {Array}
   *   Array of `{ id, args }` objects, possibly empty.
   */
  #cacheL10nIDArgsForSearchService() {
    // The search service may not be initialized if the user opens the view very
    // quickly after startup. Skip caching related strings in that case. Strings
    // are cached opportunistically every time the view opens, so they'll be
    // cached soon. We could use the search service's async methods, which
    // internally await initialization, but that would allow previously cached
    // out-of-date strings to appear in the view while the async calls are
    // ongoing. Generally there's no reason for our string-caching paths to be
    // async and it may even be a bad idea (except for the final necessary
    // `this.#l10nCache.ensureAll()` call).
    if (!lazy.SearchService.hasSuccessfullyInitialized) {
      return [];
    }

    let idArgs = [];

    let { defaultEngine, defaultPrivateEngine } = lazy.SearchService;
    let engineNames = [defaultEngine?.name, defaultPrivateEngine?.name].filter(
      name => name
    );

    if (defaultPrivateEngine) {
      idArgs.push({
        id: "urlbar-result-action-search-in-private-w-engine",
        args: { engine: defaultPrivateEngine.name },
      });
    }

    let engineStringIDs = [
      "urlbar-result-action-tabtosearch-web",
      "urlbar-result-action-tabtosearch-other-engine",
      "urlbar-result-action-search-w-engine",
    ];
    for (let id of engineStringIDs) {
      idArgs.push(...engineNames.map(name => ({ id, args: { engine: name } })));
    }

    if (lazy.UrlbarPrefs.get("groupLabels.enabled")) {
      idArgs.push(
        ...engineNames.map(name => ({
          id: "urlbar-group-search-suggestions",
          args: { engine: name },
        }))
      );
    }

    return idArgs;
  }

  /**
   * @param {UrlbarResult} result
   *   The result to get menu commands for.
   * @returns {Array}
   *   Array of menu commands available for the result, null if there are none.
   */
  #getResultMenuCommands(result) {
    if (this.#resultMenuCommands.has(result)) {
      return this.#resultMenuCommands.get(result);
    }

    /**
     * @type {?UrlbarResultCommand[]}
     */
    let commands = this.#providersManager
      .getProvider(result.providerName)
      ?.tryMethod("getResultCommands", result, this.#queryContext?.isPrivate);
    if (commands) {
      this.#resultMenuCommands.set(result, commands);
      return commands;
    }

    commands = [];
    if (result.payload.isBlockable) {
      commands.push({
        name: RESULT_MENU_COMMANDS.DISMISS,
        l10n: result.payload.blockL10n || {
          id: "urlbar-result-menu-dismiss-suggestion",
        },
      });
    }
    if (result.payload.helpUrl) {
      commands.push({
        name: RESULT_MENU_COMMANDS.HELP,
        l10n: result.payload.helpL10n || {
          id: "urlbar-result-menu-learn-more",
        },
      });
    }
    if (result.payload.isManageable) {
      commands.push({
        name: RESULT_MENU_COMMANDS.MANAGE,
        l10n: {
          id: "urlbar-result-menu-manage-firefox-suggest",
        },
      });
    }

    let rv = commands.length ? commands : null;
    this.#resultMenuCommands.set(result, rv);
    return rv;
  }

  /**
   * Popuplates the result menu with commands.
   *
   * @param {object} options
   * @param {XULTextElement} options.menupopup
   * @param {UrlbarResultCommand[]} options.commands
   */
  #populateResultMenu({ menupopup = this.resultMenu, commands }) {
    menupopup.textContent = "";
    for (let data of commands) {
      if (data.children) {
        let popup = this.document.createXULElement("menupopup");
        this.#populateResultMenu({
          menupopup: popup,
          commands: data.children,
        });
        let menu = this.document.createXULElement("menu");
        this.#l10nCache.setElementL10n(menu, data.l10n);
        menu.appendChild(popup);
        menupopup.appendChild(menu);
        continue;
      }
      if (data.name == "separator") {
        menupopup.appendChild(this.document.createXULElement("menuseparator"));
        continue;
      }
      let menuitem = this.document.createXULElement("menuitem");
      menuitem.dataset.command = data.name;
      menuitem.classList.add("urlbarView-result-menuitem");
      this.#l10nCache.setElementL10n(menuitem, data.l10n);
      menupopup.appendChild(menuitem);
    }
  }

  // Event handlers below.

  on_SelectedOneOffButtonChanged() {
    if (!this.isOpen || !this.#queryContext) {
      return;
    }

    let engine = this.oneOffSearchButtons.selectedButton?.engine;
    let source = this.oneOffSearchButtons.selectedButton?.source;
    let icon = this.oneOffSearchButtons.selectedButton?.image;

    let localSearchMode;
    if (source) {
      localSearchMode = lazy.UrlbarUtils.LOCAL_SEARCH_MODES.find(
        m => m.source == source
      );
    }

    for (let item of this.#rows.children) {
      let result = item.result;

      let isPrivateSearchWithoutPrivateEngine =
        result.payload.inPrivateWindow && !result.payload.isPrivateEngine;
      let isSearchHistory =
        result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH &&
        result.source == lazy.UrlbarUtils.RESULT_SOURCE.HISTORY;
      let isSearchSuggestion = result.payload.suggestion && !isSearchHistory;

      // For one-off buttons having a source, we update the action for the
      // heuristic result, or for any non-heuristic that is a remote search
      // suggestion or a private search with no private engine.
      if (
        !result.heuristic &&
        !isSearchSuggestion &&
        !isPrivateSearchWithoutPrivateEngine
      ) {
        continue;
      }

      // If there is no selected button and we are in full search mode, it is
      // because the user just confirmed a one-off button, thus starting a new
      // query. Don't change the heuristic result because it would be
      // immediately replaced with the search mode heuristic, causing flicker.
      if (
        result.heuristic &&
        !engine &&
        !localSearchMode &&
        this.input.searchMode &&
        !this.input.searchMode.isPreview
      ) {
        continue;
      }

      let action = item._elements.get("action");
      let favicon = item._elements.get("favicon");
      let title = item._elements.get("title");

      // If a one-off button is the only selection, force the heuristic result
      // to show its action text, so the engine name is visible.
      if (
        result.heuristic &&
        !this.selectedElement &&
        (localSearchMode || engine)
      ) {
        item.setAttribute("show-action-text", "true");
      } else {
        item.removeAttribute("show-action-text");
      }

      // If an engine is selected, update search results to use that engine.
      // Otherwise, restore their original engines.
      if (result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH) {
        if (engine) {
          if (!result.payload.originalEngine) {
            result.payload.originalEngine = result.payload.engine;
          }
          result.payload.engine = engine.name;
        } else if (result.payload.originalEngine) {
          result.payload.engine = result.payload.originalEngine;
          delete result.payload.originalEngine;
        }
      }

      // If the result is the heuristic and a one-off is selected (i.e.,
      // localSearchMode || engine), then restyle it to look like a search
      // result; otherwise, remove such styling. For restyled results, we
      // override the usual result-picking behaviour in UrlbarInput.pickResult.
      if (result.heuristic) {
        title.textContent =
          localSearchMode || engine
            ? this.#queryContext.searchString
            : result.getDisplayableValueAndHighlights("title").value;

        // Set the restyled-search attribute so the action text and title
        // separator are shown or hidden via CSS as appropriate.
        if (localSearchMode || engine) {
          item.setAttribute("restyled-search", "true");
        } else {
          item.removeAttribute("restyled-search");
        }
      }

      // Update result action text.
      if (localSearchMode) {
        // Update the result action text for a local one-off.
        const messageIDs = {
          actions: "urlbar-result-action-search-actions",
          bookmarks: "urlbar-result-action-search-bookmarks",
          history: "urlbar-result-action-search-history",
          tabs: "urlbar-result-action-search-tabs",
        };
        let name = lazy.UrlbarUtils.getResultSourceName(localSearchMode.source);
        this.#l10nCache.setElementL10n(action, {
          id: messageIDs[name],
        });
        if (result.heuristic) {
          item.setAttribute("source", name);
        }
      } else if (engine && !result.payload.inPrivateWindow) {
        // Update the result action text for an engine one-off.
        this.#l10nCache.setElementL10n(action, {
          id: "urlbar-result-action-search-w-engine",
          args: { engine: engine.name },
        });
      } else {
        // No one-off is selected. If we replaced the action while a one-off
        // button was selected, it should be restored.
        if (item._originalActionSetter) {
          item._originalActionSetter();
          if (result.heuristic) {
            favicon.src = result.payload.icon || lazy.UrlbarUtils.ICON.DEFAULT;
          }
        } else {
          console.error("An item is missing the action setter");
        }
        item.removeAttribute("source");
      }

      // Update result favicons.
      let iconOverride = localSearchMode?.icon;
      // If the icon is the default one-off search placeholder, assume we
      // don't have an icon for the engine.
      if (
        !iconOverride &&
        icon != "chrome://browser/skin/search-engine-placeholder.png"
      ) {
        iconOverride = icon;
      }
      if (!iconOverride && (localSearchMode || engine)) {
        // For one-offs without an icon, do not allow restyled URL results to
        // use their own icons.
        iconOverride = lazy.UrlbarUtils.ICON.SEARCH_GLASS;
      }
      if (
        result.heuristic ||
        (result.payload.inPrivateWindow && !result.payload.isPrivateEngine)
      ) {
        // If we just changed the engine from the original engine and it had an
        // icon, then make sure the result now uses the new engine's icon or
        // failing that the default icon.  If we changed it back to the original
        // engine, go back to the original or default icon.
        favicon.src = this.#iconForResult(result, iconOverride);
      }
    }
  }

  on_blur() {
    // If the view is open without the input being focused, it will not close
    // automatically when the window loses focus. We might be in this state
    // after a Search Tip is shown on an engine homepage.
    if (!lazy.UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.close();
    }
  }

  on_mousedown(event) {
    if (event.button == 2) {
      // Ignore right clicks.
      return;
    }

    let element = this.#getClosestSelectableElement(event.target, {
      byMouse: true,
    });
    if (!element) {
      // Ignore clicks on elements that can't be selected/picked.
      return;
    }

    // Attaching the event listener to the window so we can capture `mouseup`
    // outside of the panel when the mouse is dragged.
    this.panel.documentGlobal.addEventListener("mouseup", this);

    // Select the element and open a speculative connection unless it's a
    // button. Buttons are special in the two ways listed below. Some buttons
    // may be exceptions to these two criteria, but to provide a consistent UX
    // and avoid complexity, we apply this logic to all of them.
    //
    // (1) Some buttons do not close the view when clicked, like the block and
    // menu buttons. Clicking these buttons should not have any side effects in
    // the view or input beyond their primary purpose. For example, the block
    // button should remove the row but it should not change the input value or
    // page proxy state, and ideally it shouldn't change the input's selection
    // or caret either. It probably also shouldn't change the view's selection
    // (if the removed row isn't selected), but that may be more debatable.
    //
    // It may be possible to select buttons on mousedown and then clear the
    // selection on mouseup as usual while meeting these requirements. However,
    // it's not simple because clearing the selection has surprising side
    // effects in the input like the ones mentioned above.
    //
    // (2) Most buttons don't have URLs, so there's nothing to speculatively
    // connect to. If a button does have a URL, it's typically different from
    // the primary URL of its related result, so it's not critical to open a
    // speculative connection anyway.
    if (!element.classList.contains("urlbarView-button")) {
      this.#mousedownSelectedElement = element;
      this.#selectElement(element, { updateInput: false });
      this.controller.speculativeConnect(
        this.selectedResult,
        this.#queryContext,
        "mousedown"
      );
    }
  }

  on_mouseup(event) {
    if (event.button == 2) {
      // Ignore right clicks.
      return;
    }

    this.panel.documentGlobal.removeEventListener("mouseup", this);

    // Since the listener must be on the window use `event.composedPath()`
    // instead of `event.target` to handle shadow DOM encapsulation while
    // `event.target` may be retargeted to a shadow host.
    const eventTarget = event.composedPath()[0];
    // When mouseup outside of browser, as the target will not be element,
    // ignore it.
    let element =
      eventTarget.nodeType === eventTarget.ELEMENT_NODE
        ? this.#getClosestSelectableElement(eventTarget, {
            byMouse: true,
          })
        : null;
    if (element) {
      this.input.pickElement(element, event);
    }

    // If the element that was selected on mousedown is still in the view, clear
    // the selection. Do it after calling `pickElement()` above since code that
    // reacts to picks may assume the selected element is the picked element.
    //
    // If the element is no longer in the view, then it must be because its row
    // was removed in response to the pick. If the element was not a button, we
    // selected it on mousedown and then `onQueryResultRemoved()` selected the
    // next row; we shouldn't unselect it here. If the element was a button,
    // then we didn't select anything on mousedown; clearing the selection seems
    // like it would be harmless, but it has side effects in the input we want
    // to avoid (see `on_mousedown()`).
    if (this.#mousedownSelectedElement?.isConnected) {
      this.clearSelection();
    }
    this.#mousedownSelectedElement = null;
  }

  #isRelevantOverflowEvent(event) {
    // We're interested only in the horizontal axis.
    // 0 - vertical, 1 - horizontal, 2 - both
    return event.detail != 0;
  }

  on_overflow(event) {
    if (
      this.#isRelevantOverflowEvent(event) &&
      this.#canElementOverflow(event.target)
    ) {
      this.#setElementOverflowing(event.target, true);
    }
  }

  on_underflow(event) {
    if (
      this.#isRelevantOverflowEvent(event) &&
      this.#canElementOverflow(event.target)
    ) {
      this.#setElementOverflowing(event.target, false);
    }
  }

  on_resize() {
    this.#enableOrDisableRowWrap();
  }

  on_command(event) {
    let contextMenu;
    if (event.currentTarget == this.resultMenu) {
      let result = this.#resultMenuResult;
      this.#resultMenuResult = null;
      let menuitem = event.target;
      switch (menuitem.dataset.command) {
        case RESULT_MENU_COMMANDS.HELP:
          menuitem.dataset.url =
            result.payload.helpUrl ||
            Services.urlFormatter.formatURLPref("app.support.baseURL") +
              "awesome-bar-result-menu";
          break;
      }
      this.input.pickResult(result, event, menuitem);
    } else if (
      (contextMenu = event.target.closest("#urlbarView-context-menu"))
    ) {
      let row = contextMenu.triggerNode.closest(".urlbarView-row");
      this.input.pickResult(row.result, event, event.target);
    }
  }

  on_popupshowing(event) {
    if (event.target == this.resultMenu) {
      let commands;

      let splitButton = event.triggerEvent?.detail.target?.closest(
        ".urlbarView-splitbutton"
      );
      if (splitButton) {
        // Show the commands the are defined in its Split Button.
        let mainButton = splitButton.firstElementChild;
        let name = mainButton.dataset.name;
        commands = this.#resultMenuResult.payload.buttons.find(
          b => b.name == name
        ).menu;
      } else {
        commands = this.#getResultMenuCommands(this.#resultMenuResult);
      }

      this.#populateResultMenu({ commands });
    } else if (event.target.id == "urlbarView-context-menu") {
      if (!lazy.UrlbarPrefs.get("contextMenu.featureGate")) {
        event.preventDefault();
        return;
      }

      //  Don't show the context menu if the trigger is not on a result row.
      let row = event.triggerEvent?.target.closest(".urlbarView-row");
      if (!row) {
        event.preventDefault();
        return;
      }

      // Set the context-menu-trigger attribute on the row so it can be styled
      // as if it were hovered while the context menu is open.
      row.toggleAttribute("context-menu-trigger", true);

      // Disable the context menu if the result does not return url.
      let url = lazy.UrlbarUtils.getUrlFromResult(row.result, {
        element: row,
      })?.url;
      event.target.toggleAttribute("disabled", !url);
    } else if (
      event.target.id == "urlbarView-context-menu-open-in-container-tab-popup"
    ) {
      event.target.documentGlobal.createUserContextMenu(event, {
        isContextMenu: true,
      });
    }
  }

  on_popuphiding(event) {
    if (event.target.id == "urlbarView-context-menu") {
      event.target.triggerNode
        .closest(".urlbarView-row")
        ?.toggleAttribute("context-menu-trigger", false);
    }
  }
}

/**
 * Implements a QueryContext cache, working as a circular buffer, when a new
 * entry is added at the top, the last item is remove from the bottom.
 */
class QueryContextCache {
  #cache;
  #size;
  #topSitesContext;
  #topSitesListener;

  /**
   * Constructor.
   *
   * @param {number} size The number of entries to keep in the cache.
   */
  constructor(size) {
    this.#size = size;
    this.#cache = [];

    // We store the top-sites context separately since it will often be needed
    // and therefore shouldn't be evicted except when the top sites change.
    this.#topSitesContext = null;
    this.#topSitesListener = () => (this.#topSitesContext = null);
    lazy.UrlbarProviderTopSites.addTopSitesListener(this.#topSitesListener);
  }

  /**
   * @returns {number} The number of entries to keep in the cache.
   */
  get size() {
    return this.#size;
  }

  /**
   * @returns {UrlbarQueryContext} The cached top-sites context or null if none.
   */
  get topSitesContext() {
    return this.#topSitesContext;
  }

  /**
   * Adds a new entry to the cache.
   *
   * @param {UrlbarQueryContext} queryContext The UrlbarQueryContext to add.
   * Note: QueryContexts without results are ignored and not added. Contexts
   *       with an empty searchString that are not the top-sites context are
   *       also ignored.
   */
  put(queryContext) {
    if (!queryContext.results.length) {
      return;
    }

    let searchString = queryContext.searchString;
    if (!searchString) {
      // Cache the context if it's the top-sites context. An empty search string
      // doesn't necessarily imply top sites since there are other queries that
      // use it too, like search mode. If any result is from the top-sites
      // provider, assume the context is top sites.
      // However, if contextual opt-in message is shown, disable the cache. The
      // message might hide when beginning of query, this cache will be shown
      // for a moment.
      if (
        queryContext.results?.some(
          r => r.providerName == "UrlbarProviderTopSites"
        ) &&
        !queryContext.results.some(
          r => r.providerName == "UrlbarProviderQuickSuggestContextualOptIn"
        )
      ) {
        this.#topSitesContext = queryContext;
      }
      return;
    }

    let index = this.#cache.findIndex(e => e.searchString == searchString);
    if (index != -1) {
      if (this.#cache[index] == queryContext) {
        return;
      }
      this.#cache.splice(index, 1);
    }
    if (this.#cache.unshift(queryContext) > this.size) {
      this.#cache.length = this.size;
    }
  }

  get(searchString) {
    return this.#cache.find(e => e.searchString == searchString);
  }
}

/**
 * Adds a dynamic result type stylesheet to a specified window.
 *
 * @param {Window} window
 *   The window to which to add the stylesheet.
 * @param {string} stylesheetURL
 *   The stylesheet's URL.
 */
async function addDynamicStylesheet(window, stylesheetURL) {
  // Try-catch all of these so that failing to load a stylesheet doesn't break
  // callers and possibly the urlbar.  If a stylesheet does fail to load, the
  // dynamic results that depend on it will appear broken, but at least we
  // won't break the whole urlbar.
  try {
    let uri = Services.io.newURI(stylesheetURL);
    let sheet = await lazy.styleSheetService.preloadSheetAsync(
      uri,
      Ci.nsIStyleSheetService.AGENT_SHEET
    );
    window.windowUtils.addSheet(sheet, Ci.nsIDOMWindowUtils.AGENT_SHEET);
  } catch (ex) {
    console.error("Error adding dynamic stylesheet:", ex);
  }
}

/**
 * Removes a dynamic result type stylesheet from the view's window.
 *
 * @param {Window} window
 *   The window from which to remove the stylesheet.
 * @param {string} stylesheetURL
 *   The stylesheet's URL.
 */
function removeDynamicStylesheet(window, stylesheetURL) {
  // Try-catch for the same reason as desribed in addDynamicStylesheet.
  try {
    window.windowUtils.removeSheetUsingURIString(
      stylesheetURL,
      Ci.nsIDOMWindowUtils.AGENT_SHEET
    );
  } catch (ex) {
    console.error("Error removing dynamic stylesheet:", ex);
  }
}
