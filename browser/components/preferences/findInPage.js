/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */

// A tweak to the standard <button> CE to use textContent on the <label>
// inside the button, which allows the text to be highlighted when the user
// is searching.

/** @import MozInputSearch from "chrome://global/content/elements/moz-input-search.mjs" */

const MozButtonClass = customElements.get("button");
class HighlightableButton extends MozButtonClass {
  static get inheritedAttributes() {
    // @ts-expect-error super is MozButton from toolkit/content/widgets/button.js
    return Object.assign({}, super.inheritedAttributes, {
      ".button-text": "text=label,accesskey,crop",
    });
  }
}
customElements.define("highlightable-button", HighlightableButton, {
  extends: "button",
});

var gSearchResultsPane = {
  /** @type {string} */
  query: undefined,
  listSearchTooltips: new Set(),
  listSearchMenuitemIndicators: new Set(),
  /** @type {MozInputSearch} */
  searchInput: null,
  /** @type {HTMLDivElement} */
  searchTooltipContainer: null,
  // A map of DOM Elements to a string of keywords used in search
  // XXX: We should invalidate this cache on `intl:app-locales-changed`
  searchKeywords: new WeakMap(),
  inited: false,

  // A (node -> boolean) map of subitems to be made visible or hidden.
  subItems: new Map(),

  searchResultsHighlighted: false,

  searchableNodes: new Set([
    "button",
    "label",
    "description",
    "menulist",
    "menuitem",
    "checkbox",
  ]),

  init() {
    if (this.inited) {
      return;
    }
    this.inited = true;
    this.searchInput = /** @type {MozInputSearch} */ (
      document.getElementById("searchInput")
    );
    this.searchTooltipContainer = /** @type {HTMLDivElement} */ (
      document.getElementById("search-tooltip-container")
    );

    window.addEventListener("resize", () => {
      this._recomputeTooltipPositions();
    });

    if (!this.searchInput.hidden) {
      this.searchInput.addEventListener("input", this);
      window.addEventListener("DOMContentLoaded", () => {
        this.searchInput.updateComplete.then(() => {
          this.searchInput.focus();
        });
        // Initialize other panes in an idle callback.
        window.requestIdleCallback(() => this.initializeCategories());
      });
    }
  },

  /** @param {InputEvent} event */
  async handleEvent(event) {
    // Ensure categories are initialized if idle callback didn't run sooo enough.
    await this.initializeCategories();
    this.searchFunction(event);
  },

  /**
   * Check that the text content contains the query string.
   *
   * @param String content
   *    the text content to be searched
   * @param String query
   *    the query string
   * @returns boolean
   *    true when the text content contains the query string else false
   */
  queryMatchesContent(content, query) {
    if (!content || !query) {
      return false;
    }
    return content.toLowerCase().includes(query.toLowerCase());
  },

  categoriesInitialized: false,

  /**
   * Will attempt to initialize all uninitialized categories
   */
  async initializeCategories() {
    //  Initializing all the JS for all the tabs
    if (!this.categoriesInitialized) {
      this.categoriesInitialized = true;
      // Each element of gCategoryInits is a name
      for (let category of gCategoryInits.values()) {
        category.init();
      }
      if (document.hasPendingL10nMutations) {
        await new Promise(r =>
          document.addEventListener("L10nMutationsFinished", r, { once: true })
        );
      }
    }
  },

  /**
   * Finds and returns text nodes within node and all descendants.
   * Iterates through all the siblings of the node object and adds each sibling to an
   * array if it's a TEXT_NODE, and otherwise recurses to check text nodes within it.
   * Source - http://stackoverflow.com/questions/10730309/find-all-text-nodes-in-html-page
   *
   * @param Node nodeObject
   *    DOM element
   * @returns array of text nodes
   */
  textNodeDescendants(node) {
    if (!node) {
      return [];
    }
    let all = [];
    for (node = node.firstChild; node; node = node.nextSibling) {
      if (node.nodeType === node.TEXT_NODE) {
        all.push(node);
      } else {
        all = all.concat(this.textNodeDescendants(node));
      }
    }
    return all;
  },

  /**
   * This function is used to find words contained within the text nodes.
   * We pass in the textNodes because they contain the text to be highlighted.
   * We pass in the nodeSizes to tell exactly where highlighting need be done.
   * When creating the range for highlighting, if the nodes are section is split
   * by an access key, it is important to have the size of each of the nodes summed.
   *
   * @param Array textNodes
   *    List of DOM elements
   * @param Array nodeSizes
   *    Running size of text nodes. This will contain the same number of elements as textNodes.
   *    The first element is the size of first textNode element.
   *    For any nodes after, they will contain the summation of the nodes thus far in the array.
   *    Example:
   *    textNodes = [[This is ], [a], [n example]]
   *    nodeSizes = [[8], [9], [18]]
   *    This is used to determine the offset when highlighting
   * @param String textSearch
   *    Concatination of textNodes's text content
   *    Example:
   *    textNodes = [[This is ], [a], [n example]]
   *    nodeSizes = "This is an example"
   *    This is used when executing the regular expression
   * @param String searchPhrase
   *    word or words to search for
   * @returns boolean
   *      Returns true when atleast one instance of search phrase is found, otherwise false
   */
  highlightMatches(textNodes, nodeSizes, textSearch, searchPhrase) {
    if (!searchPhrase) {
      return false;
    }

    let indices = [];
    let i = -1;
    while ((i = textSearch.indexOf(searchPhrase, i + 1)) >= 0) {
      indices.push(i);
    }

    // Looping through each spot the searchPhrase is found in the concatenated string
    for (let startValue of indices) {
      let endValue = startValue + searchPhrase.length;
      let startNode = null;
      let endNode = null;
      let nodeStartIndex = null;

      // Determining the start and end node to highlight from
      for (let index = 0; index < nodeSizes.length; index++) {
        let lengthNodes = nodeSizes[index];
        // Determining the start node
        if (!startNode && lengthNodes >= startValue) {
          startNode = textNodes[index];
          nodeStartIndex = index;
          // Calculating the offset when found query is not in the first node
          if (index > 0) {
            startValue -= nodeSizes[index - 1];
          }
        }
        // Determining the end node
        if (!endNode && lengthNodes >= endValue) {
          endNode = textNodes[index];
          // Calculating the offset when endNode is different from startNode
          // or when endNode is not the first node
          if (index != nodeStartIndex || index > 0) {
            endValue -= nodeSizes[index - 1];
          }
        }
      }
      let range = document.createRange();
      range.setStart(startNode, startValue);
      range.setEnd(endNode, endValue);
      this.getFindSelection(startNode.documentGlobal).addRange(range);

      this.searchResultsHighlighted = true;
    }

    return !!indices.length;
  },

  /**
   * Get the selection instance from given window
   *
   * @param Object win
   *   The window object points to frame's window
   */
  getFindSelection(win) {
    // Yuck. See bug 138068.
    let docShell = win.docShell;

    let controller = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsISelectionDisplay)
      .QueryInterface(Ci.nsISelectionController);

    let selection = controller.getSelection(
      Ci.nsISelectionController.SELECTION_FIND
    );
    selection.setColors("currentColor", "#ffe900", "currentColor", "#003eaa");

    return selection;
  },

  /**
   * Shows or hides content according to search input
   *
   * @param String event
   *    to search for filted query in
   */
  async searchFunction(event) {
    let query = event.target.value.trim().toLowerCase();
    if (this.query == query) {
      return;
    }

    let subQuery = this.query && query.includes(this.query);
    this.query = query;

    // If there is a query, don't reshow the existing hidden subitems yet
    // to avoid them flickering into view only to be hidden again by
    // this next search.
    this.removeAllSearchIndicators(window, !query.length);

    let srHeader = document.getElementById("header-searchResults");
    let noResultsEl = document.getElementById("no-results-message");
    if (this.query) {
      // Showing the Search Results Tag
      await gotoPref("paneSearchResults");
      srHeader.hidden = false;

      let resultsFound = false;

      // Building the range for highlighted areas
      let rootPreferencesChildren = [
        ...document.querySelectorAll(
          "#mainPrefPane > *:not([data-hidden-from-search], script, stringbundle)"
        ),
      ];

      if (subQuery) {
        // Since the previous query is a subset of the current query,
        // there is no need to check elements that is hidden already.
        rootPreferencesChildren = rootPreferencesChildren.filter(
          el => !el.hidden
        );
      }

      // Attach the bindings for all children if they were not already visible.
      for (let child of rootPreferencesChildren) {
        if (child.hidden) {
          child.classList.add("visually-hidden");
          child.hidden = false;
        }
      }

      let ts = performance.now();
      let FRAME_THRESHOLD = 1000 / 60;

      // Showing or Hiding specific section depending on if words in query are found
      for (let child of rootPreferencesChildren) {
        if (performance.now() - ts > FRAME_THRESHOLD) {
          // Creating tooltips for all the instances found
          for (let anchorNode of this.listSearchTooltips) {
            this.createSearchTooltip(anchorNode, this.query);
          }
          ts = await new Promise(resolve =>
            window.requestAnimationFrame(resolve)
          );
          if (query !== this.query) {
            return;
          }
        }

        if (
          !child.classList.contains("header") &&
          (!child.classList.contains("subcategory") ||
            child.localName == "setting-group") &&
          (await this.searchWithinNode(child, this.query))
        ) {
          child.classList.remove("visually-hidden");

          // Show the preceding search-header if one exists.
          let groupbox =
            child.closest("groupbox") || child.closest("[data-category]");
          let groupHeader =
            groupbox && groupbox.querySelector(".search-header");
          if (groupHeader) {
            groupHeader.hidden = false;
          }

          resultsFound = true;
        } else {
          child.classList.add("visually-hidden");
        }
      }

      // Hide any subitems that don't match the search term and show
      // only those that do.
      if (this.subItems.size) {
        for (let [subItem, matches] of this.subItems) {
          subItem.classList.toggle("visually-hidden", !matches);
        }
      }

      noResultsEl.hidden = !!resultsFound;
      noResultsEl.setAttribute("query", this.query);
      // XXX: This is potentially racy in case where Fluent retranslates the
      // message and ereases the query within.
      // The feature is not yet supported, but we should fix for it before
      // we enable it. See bug 1446389 for details.
      let msgQueryElem = document.getElementById("sorry-message-query");
      msgQueryElem.textContent = this.query;
      if (resultsFound) {
        // Creating tooltips for all the instances found
        for (let anchorNode of this.listSearchTooltips) {
          this.createSearchTooltip(anchorNode, this.query);
        }
      }
    } else {
      noResultsEl.hidden = true;
      document.getElementById("sorry-message-query").textContent = "";
      // Going back to Account and sync or General when cleared
      let redesignEnabled = Services.prefs.getBoolPref(
        "browser.settings-redesign.enabled"
      );
      let defaultPane = redesignEnabled ? "paneSync" : "paneGeneral";
      await gotoPref(defaultPane);
      srHeader.hidden = true;

      // Hide some special second level headers in normal view
      for (let element of document.querySelectorAll(".search-header")) {
        element.hidden = true;
      }
    }

    window.dispatchEvent(
      new CustomEvent("PreferencesSearchCompleted", { detail: query })
    );
  },

  /**
   * Determine if the given element is an anchor tag.
   *
   * @param {HTMLElement} el The element.
   * @returns {boolean} Whether or not the element is an anchor tag.
   */
  _isAnchor(el) {
    return (el.prefix === null || el.prefix === "html") && el.localName === "a";
  },

  /**
   * Finding leaf nodes and checking their content for words to search,
   * It is a recursive function
   *
   * @param Node nodeObject
   *    DOM Element
   * @param String searchPhrase
   * @returns boolean
   *    Returns true when found in at least one childNode, false otherwise
   */
  async searchWithinNode(nodeObject, searchPhrase) {
    let matchesFound = false;
    if (
      nodeObject.childElementCount == 0 ||
      (typeof nodeObject.children !== "undefined" &&
        Array.prototype.every.call(nodeObject.children, this._isAnchor)) ||
      this.searchableNodes.has(nodeObject.localName) ||
      (nodeObject.localName?.startsWith("moz-") &&
        nodeObject.localName !== "moz-input-box")
    ) {
      let simpleTextNodes = this.textNodeDescendants(nodeObject);
      if (nodeObject.shadowRoot) {
        simpleTextNodes.push(
          ...this.textNodeDescendants(nodeObject.shadowRoot)
        );
      }
      for (let node of simpleTextNodes) {
        let result = this.highlightMatches(
          [node],
          [node.length],
          node.textContent.toLowerCase(),
          searchPhrase
        );
        matchesFound = matchesFound || result;
      }

      // Collecting data from anonymous content / label / description
      let nodeSizes = [];
      let allNodeText = "";
      let runningSize = 0;

      let accessKeyTextNodes = [];

      if (
        nodeObject.localName == "label" ||
        nodeObject.localName == "description" ||
        nodeObject.localName.startsWith("moz-")
      ) {
        accessKeyTextNodes.push(...simpleTextNodes);
      }

      for (let node of accessKeyTextNodes) {
        runningSize += node.textContent.length;
        allNodeText += node.textContent;
        nodeSizes.push(runningSize);
      }

      // Access key are presented
      let complexTextNodesResult = this.highlightMatches(
        accessKeyTextNodes,
        nodeSizes,
        allNodeText.toLowerCase(),
        searchPhrase
      );

      // Searching some elements, such as xul:button, have a 'label' attribute that contains the user-visible text.
      let labelResult = this.queryMatchesContent(
        nodeObject.getAttribute("label"),
        searchPhrase
      );

      // Searching some elements, such as xul:label, store their user-visible text in a "value" attribute.
      // Value will be skipped for menuitem since value in menuitem could represent index number to distinct each item.
      let valueResult =
        nodeObject.localName !== "menuitem" && nodeObject.localName !== "radio"
          ? this.queryMatchesContent(
              nodeObject.getAttribute("value"),
              searchPhrase
            )
          : false;

      // Searching some elements, such as xul:button, buttons to open subdialogs
      // using l10n ids.
      let keywordsResult =
        nodeObject.hasAttribute("search-l10n-ids") &&
        (await this.matchesSearchL10nIDs(nodeObject, searchPhrase));

      if (!keywordsResult) {
        // Searching some elements, such as xul:button, buttons to open subdialogs
        // using searchkeywords attribute.
        keywordsResult =
          !keywordsResult &&
          nodeObject.hasAttribute("searchkeywords") &&
          this.queryMatchesContent(
            nodeObject.getAttribute("searchkeywords"),
            searchPhrase
          );
      }

      // Creating tooltips for buttons
      if (
        keywordsResult &&
        // eslint-disable-next-line mozilla/use-isInstance
        (nodeObject instanceof HTMLElement ||
          nodeObject.localName === "button" ||
          nodeObject.localName == "menulist")
      ) {
        this.listSearchTooltips.add(nodeObject);
      }

      if (keywordsResult && nodeObject.localName === "menuitem") {
        nodeObject.setAttribute("indicator", "true");
        this.listSearchMenuitemIndicators.add(nodeObject);
        let menulist = nodeObject.closest("menulist");

        menulist.setAttribute("indicator", "true");
        this.listSearchMenuitemIndicators.add(menulist);
      }

      if (
        (nodeObject.localName == "menulist" ||
          nodeObject.localName == "menuitem") &&
        (labelResult || valueResult || keywordsResult)
      ) {
        nodeObject.setAttribute("highlightable", "true");
      }

      matchesFound =
        matchesFound ||
        complexTextNodesResult ||
        labelResult ||
        valueResult ||
        keywordsResult;
    }

    // Should not search unselected child nodes of a <xul:deck> element
    // except the "historyPane" <xul:deck> element.
    if (nodeObject.localName == "deck" && nodeObject.id != "historyPane") {
      let index = nodeObject.selectedIndex;
      if (index != -1) {
        let result = await this.searchChildNodeIfVisible(
          nodeObject,
          index,
          searchPhrase
        );
        matchesFound = matchesFound || result;
      }
    } else {
      for (let i = 0; i < nodeObject.childNodes.length; i++) {
        let result = await this.searchChildNodeIfVisible(
          nodeObject,
          i,
          searchPhrase
        );
        matchesFound = matchesFound || result;
      }
    }
    return matchesFound;
  },

  /**
   * Search for a phrase within a child node if it is visible.
   *
   * @param Node nodeObject
   *    The parent DOM Element
   * @param Number index
   *    The index for the childNode
   * @param String searchPhrase
   * @returns boolean
   *    Returns true when found the specific childNode, false otherwise
   */
  async searchChildNodeIfVisible(nodeObject, index, searchPhrase) {
    let result = false;
    let child = nodeObject.childNodes[index];
    if (
      !child.hidden &&
      nodeObject.getAttribute("data-hidden-from-search") !== "true"
    ) {
      result = await this.searchWithinNode(child, searchPhrase);
      // Creating tooltips for menulist element
      if (result && nodeObject.localName === "menulist") {
        this.listSearchTooltips.add(nodeObject);
      }

      // If this is a node for an experimental feature option or a Mozilla product item,
      // add it to the list of subitems. The items that don't match the search term
      // will be hidden.
      if (
        Element.isInstance(child) &&
        (child.classList.contains("featureGate") ||
          child.classList.contains("mozilla-product-item"))
      ) {
        this.subItems.set(child, result);
      }
    }
    return result;
  },

  /**
   * Search for a phrase in l10n messages associated with the element.
   *
   * @param Node nodeObject
   *    The parent DOM Element
   * @param String searchPhrase
   * @returns boolean
   *    true when the text content contains the query string else false
   */
  async matchesSearchL10nIDs(nodeObject, searchPhrase) {
    if (!this.searchKeywords.has(nodeObject)) {
      // The `search-l10n-ids` attribute is a comma-separated list of
      // l10n ids. It may also uses a dot notation to specify an attribute
      // of the message to be used.
      //
      // Example: "containers-add-button.label, user-context-personal"
      //
      // The result is an array of arrays of l10n ids and optionally attribute names.
      //
      // Example: [["containers-add-button", "label"], ["user-context-personal"]]
      const refs = nodeObject
        .getAttribute("search-l10n-ids")
        .split(",")
        .map(s => s.trim().split("."))
        .filter(s => !!s[0].length);

      const messages = await document.l10n.formatMessages(
        refs.map(ref => ({ id: ref[0] }))
      );

      // Map the localized messages taking value or a selected attribute and
      // building a string of concatenated translated strings out of it.
      let keywords = messages
        .map((msg, i) => {
          let [refId, refAttr] = refs[i];
          if (!msg) {
            console.error(`Missing search l10n id "${refId}"`);
            return null;
          }
          if (refAttr) {
            let attr =
              msg.attributes && msg.attributes.find(a => a.name === refAttr);
            if (!attr) {
              console.error(`Missing search l10n id "${refId}.${refAttr}"`);
              return null;
            }
            if (attr.value === "") {
              console.error(
                `Empty value added to search-l10n-ids "${refId}.${refAttr}"`
              );
            }
            return attr.value;
          }
          if (msg.value === "") {
            console.error(`Empty value added to search-l10n-ids "${refId}"`);
          }
          return msg.value;
        })
        .filter(keyword => keyword !== null)
        .join(" ");

      this.searchKeywords.set(nodeObject, keywords);
      return this.queryMatchesContent(keywords, searchPhrase);
    }

    return this.queryMatchesContent(
      this.searchKeywords.get(nodeObject),
      searchPhrase
    );
  },

  /**
   * Inserting a div structure infront of the DOM element matched textContent.
   * Then calculation the offsets to position the tooltip in the correct place.
   *
   * @param Node anchorNode
   *    DOM Element
   * @param String query
   *    Word or words that are being searched for
   */
  createSearchTooltip(anchorNode, query) {
    if (anchorNode.tooltipNode) {
      return;
    }
    let searchTooltip = anchorNode.ownerDocument.createElement("span");
    let searchTooltipText = anchorNode.ownerDocument.createElement("span");
    searchTooltip.className = "search-tooltip";
    searchTooltipText.textContent = query;
    searchTooltip.appendChild(searchTooltipText);

    // Set tooltipNode property to track corresponded tooltip node.
    anchorNode.tooltipNode = searchTooltip;
    anchorNode.parentElement.classList.add("search-tooltip-parent");
    this.searchTooltipContainer.append(searchTooltip);

    this._applyTooltipPosition(
      searchTooltip,
      this._computeTooltipPosition(anchorNode, searchTooltip)
    );
  },

  _recomputeTooltipPositions() {
    let positions = [];
    for (let anchorNode of this.listSearchTooltips) {
      let searchTooltip = anchorNode.tooltipNode;
      if (!searchTooltip) {
        continue;
      }
      let position = this._computeTooltipPosition(anchorNode, searchTooltip);
      positions.push({ searchTooltip, position });
    }
    for (let { searchTooltip, position } of positions) {
      this._applyTooltipPosition(searchTooltip, position);
    }
  },

  _applyTooltipPosition(searchTooltip, position) {
    searchTooltip.style.left = position.left + "px";
    searchTooltip.style.top = position.top + "px";
  },

  _computeTooltipPosition(anchorNode, searchTooltip) {
    // In order to get the up-to-date position of each of the nodes that we're
    // putting tooltips on, we have to flush layout intentionally. Once
    // menulists don't use XUL layout we can remove this and use plain CSS to
    // position them, see bug 1363730.
    let anchorRect = anchorNode.getBoundingClientRect();
    let tooltipContainerRect =
      this.searchTooltipContainer.getBoundingClientRect();
    let tooltipRect = searchTooltip.getBoundingClientRect();

    let top = anchorRect.top - tooltipContainerRect.top;

    let left;
    if (anchorRect.left <= tooltipContainerRect.left + 20) {
      // Left align on anchors that are close to the side of the main content
      left = 8;
    } else {
      // Center tooltips if their anchor is floating off somewhere else
      left =
        anchorRect.left -
        tooltipContainerRect.left +
        anchorRect.width / 2 -
        tooltipRect.width / 2;
    }
    return { left, top };
  },

  /**
   * Remove all search indicators. This would be called when switching away from
   * a search to another preference category.
   */
  removeAllSearchIndicators(window, showSubItems) {
    if (this.searchResultsHighlighted) {
      this.getFindSelection(window).removeAllRanges();
      this.searchResultsHighlighted = false;
    }
    this.removeAllSearchTooltips();
    this.removeAllSearchMenuitemIndicators();

    // Make any previously hidden subitems visible again for the next search.
    if (showSubItems && this.subItems.size) {
      for (let subItem of this.subItems.keys()) {
        subItem.classList.remove("visually-hidden");
      }
      this.subItems.clear();
    }
  },

  /**
   * Remove all search tooltips.
   */
  removeAllSearchTooltips() {
    for (let anchorNode of this.listSearchTooltips) {
      anchorNode.parentElement.classList.remove("search-tooltip-parent");
      if (anchorNode.tooltipNode) {
        anchorNode.tooltipNode.remove();
      }
      anchorNode.tooltipNode = null;
    }
    this.listSearchTooltips.clear();
  },

  /**
   * Remove all indicators on menuitem.
   */
  removeAllSearchMenuitemIndicators() {
    for (let node of this.listSearchMenuitemIndicators) {
      node.removeAttribute("indicator");
    }
    this.listSearchMenuitemIndicators.clear();
  },
};
