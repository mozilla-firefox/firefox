/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * About the objects defined in this file:
 * - CssLogic contains style information about a view context. It provides
 *   access to 2 sets of objects: Css[Sheet|Rule|Selector] provide access to
 *   information that does not change when the selected element changes while
 *   Css[Property|Selector]Info provide information that is dependent on the
 *   selected element.
 *   Its key methods are highlight(), getPropertyInfo() and forEachSheet(), etc
 *
 * - CssSheet provides a more useful API to a DOM CSSSheet for our purposes,
 *   including shortSource and href.
 * - CssRule a more useful API to a DOM CSSRule including access to the group
 *   of CssSelectors that the rule provides properties for
 * - CssSelector A single selector - i.e. not a selector group. In other words
 *   a CssSelector does not contain ','. This terminology is different from the
 *   standard DOM API, but more inline with the definition in the spec.
 *
 * - CssPropertyInfo contains style information for a single property for the
 *   highlighted element.
 * - CssSelectorInfo is a wrapper around CssSelector, which adds sorting with
 *   reference to the selected element.
 */

"use strict";

const nodeConstants = require("resource://devtools/shared/dom-node-constants.js");
const {
  getBindingElementAndPseudo,
  getMatchingCSSRules,
  hasVisitedState,
  isAgentStylesheet,
  isAuthorStylesheet,
  isUserStylesheet,
  shortSource,
  ELEMENT_BACKED_PSEUDO_ELEMENTS,
  FILTER,
  STATUS,
} = require("resource://devtools/shared/inspector/css-logic.js");

const COMPAREMODE = {
  BOOLEAN: "bool",
  INTEGER: "int",
};

class CssLogic {
  constructor() {
    this._propertyInfos = {};
  }

  // Both setup by highlight().
  viewedElement = null;
  viewedDocument = null;

  // The cache of the known sheets.
  _sheets = null;

  // Have the sheets been cached?
  _sheetsCached = false;

  // The total number of rules, in all stylesheets, after filtering.
  _ruleCount = 0;

  // The computed styles for the viewedElement.
  _computedStyle = null;

  // Source filter. Only display properties coming from the given source
  _sourceFilter = FILTER.USER;

  // Used for tracking unique CssSheet/CssRule/CssSelector objects, in a run of
  // processMatchedSelectors().
  _passId = 0;

  // Used for tracking matched CssSelector objects.
  _matchId = 0;

  _matchedRules = null;
  _matchedSelectors = null;

  // Cached keyframes rules in all stylesheets
  _keyframesRules = null;

  /**
   * Reset various properties
   */
  reset() {
    this._propertyInfos = {};
    this._ruleCount = 0;
    this._sheetIndex = 0;
    this._sheets = {};
    this._sheetsCached = false;
    this._matchedRules = null;
    this._matchedSelectors = null;
    this._keyframesRules = [];
  }

  /**
   * Focus on a new element - remove the style caches.
   *
   * @param {Element} aViewedElement the element the user has highlighted
   * in the Inspector.
   */
  highlight(viewedElement) {
    if (!viewedElement) {
      this.viewedElement = null;
      this.viewedDocument = null;
      this._computedStyle = null;
      this.reset();
      return;
    }

    if (viewedElement === this.viewedElement) {
      return;
    }

    this.viewedElement = viewedElement;

    const doc = this.viewedElement.ownerDocument;
    if (doc != this.viewedDocument) {
      // New document: clear/rebuild the cache.
      this.viewedDocument = doc;

      // Hunt down top level stylesheets, and cache them.
      this._cacheSheets();
    } else {
      // Clear cached data in the CssPropertyInfo objects.
      this._propertyInfos = {};
    }

    this._matchedRules = null;
    this._matchedSelectors = null;
    this._computedStyle = CssLogic.getComputedStyle(this.viewedElement);
  }

  /**
   * Get the values of all the computed CSS properties for the highlighted
   * element.
   * @returns {object} The computed CSS properties for a selected element
   */
  get computedStyle() {
    return this._computedStyle;
  }

  /**
   * Get the source filter.
   * @returns {string} The source filter being used.
   */
  get sourceFilter() {
    return this._sourceFilter;
  }

  /**
   * Source filter. Only display properties coming from the given source (web
   * address). Note that in order to avoid information overload we DO NOT show
   * unmatched system rules.
   * @see FILTER.*
   */
  set sourceFilter(value) {
    const oldValue = this._sourceFilter;
    this._sourceFilter = value;

    let ruleCount = 0;

    // Update the CssSheet objects.
    this.forEachSheet(function (sheet) {
      if (sheet.authorSheet && sheet.sheetAllowed) {
        ruleCount += sheet.ruleCount;
      }
    }, this);

    this._ruleCount = ruleCount;

    // Full update is needed because the this.processMatchedSelectors() method
    // skips UA stylesheets if the filter does not allow such sheets.
    const needFullUpdate = oldValue == FILTER.UA || value == FILTER.UA;

    if (needFullUpdate) {
      this._matchedRules = null;
      this._matchedSelectors = null;
      this._propertyInfos = {};
    } else {
      // Update the CssPropertyInfo objects.
      for (const property in this._propertyInfos) {
        this._propertyInfos[property].needRefilter = true;
      }
    }
  }

  /**
   * Return a CssPropertyInfo data structure for the currently viewed element
   * and the specified CSS property. If there is no currently viewed element we
   * return an empty object.
   *
   * @param {string} property The CSS property to look for.
   * @return {CssPropertyInfo} a CssPropertyInfo structure for the given
   * property.
   */
  getPropertyInfo(property) {
    if (!this.viewedElement) {
      return {};
    }

    let info = this._propertyInfos[property];
    if (!info) {
      info = new CssPropertyInfo(this, property);
      this._propertyInfos[property] = info;
    }

    return info;
  }

  /**
   * Cache all the stylesheets in the inspected document
   * @private
   */
  _cacheSheets() {
    this._passId++;
    this.reset();

    // styleSheets isn't an array, but forEach can work on it anyway
    const styleSheets = InspectorUtils.getAllStyleSheets(
      this.viewedDocument,
      true
    );
    Array.prototype.forEach.call(styleSheets, this._cacheSheet, this);

    this._sheetsCached = true;
  }

  /**
   * Cache a stylesheet if it falls within the requirements: if it's enabled,
   * and if the @media is allowed. This method also walks through the stylesheet
   * cssRules to find @imported rules, to cache the stylesheets of those rules
   * as well. In addition, the @keyframes rules in the stylesheet are cached.
   *
   * @private
   * @param {CSSStyleSheet} domSheet the CSSStyleSheet object to cache.
   */
  _cacheSheet(domSheet) {
    if (domSheet.disabled) {
      return;
    }

    // Only work with stylesheets that have their media allowed.
    if (!this.mediaMatches(domSheet)) {
      return;
    }

    // Cache the sheet.
    const cssSheet = this.getSheet(domSheet, this._sheetIndex++);
    if (cssSheet._passId != this._passId) {
      cssSheet._passId = this._passId;

      // Find import and keyframes rules. We loop through all the stylesheet recursively,
      // so we can go through nested rules.
      const traverseRules = ruleList => {
        for (const aDomRule of ruleList) {
          const ruleClassName = ChromeUtils.getClassName(aDomRule);
          if (
            ruleClassName === "CSSImportRule" &&
            aDomRule.styleSheet &&
            this.mediaMatches(aDomRule)
          ) {
            this._cacheSheet(aDomRule.styleSheet);
          } else if (ruleClassName === "CSSKeyframesRule") {
            this._keyframesRules.push(aDomRule);
          }

          if (aDomRule.cssRules) {
            traverseRules(aDomRule.cssRules);
          }
        }
      };

      traverseRules(cssSheet.getCssRules());
    }
  }

  /**
   * Retrieve the list of stylesheets in the document.
   *
   * @return {array} the list of stylesheets in the document.
   */
  get sheets() {
    if (!this._sheetsCached) {
      this._cacheSheets();
    }

    const sheets = [];
    this.forEachSheet(function (sheet) {
      if (sheet.authorSheet) {
        sheets.push(sheet);
      }
    }, this);

    return sheets;
  }

  /**
   * Retrieve the list of keyframes rules in the document.
   *
   * @ return {array} the list of keyframes rules in the document.
   */
  get keyframesRules() {
    if (!this._sheetsCached) {
      this._cacheSheets();
    }
    return this._keyframesRules;
  }

  /**
   * Retrieve a CssSheet object for a given a CSSStyleSheet object. If the
   * stylesheet is already cached, you get the existing CssSheet object,
   * otherwise the new CSSStyleSheet object is cached.
   *
   * @param {CSSStyleSheet} domSheet the CSSStyleSheet object you want.
   * @param {number} index the index, within the document, of the stylesheet.
   *
   * @return {CssSheet} the CssSheet object for the given CSSStyleSheet object.
   */
  getSheet(domSheet, index) {
    let cacheId = "";

    if (domSheet.href) {
      cacheId = domSheet.href;
    } else if (domSheet.associatedDocument) {
      cacheId = domSheet.associatedDocument.location;
    }

    let sheet = null;
    let sheetFound = false;

    if (cacheId in this._sheets) {
      for (sheet of this._sheets[cacheId]) {
        if (sheet.domSheet === domSheet) {
          if (index != -1) {
            sheet.index = index;
          }
          sheetFound = true;
          break;
        }
      }
    }

    if (!sheetFound) {
      if (!(cacheId in this._sheets)) {
        this._sheets[cacheId] = [];
      }

      sheet = new CssSheet(this, domSheet, index);
      if (sheet.sheetAllowed && sheet.authorSheet) {
        this._ruleCount += sheet.ruleCount;
      }

      this._sheets[cacheId].push(sheet);
    }

    return sheet;
  }

  /**
   * Process each cached stylesheet in the document using your callback.
   *
   * @param {function} callback the function you want executed for each of the
   * CssSheet objects cached.
   * @param {object} scope the scope you want for the callback function. scope
   * will be the this object when callback executes.
   */
  forEachSheet(callback, scope) {
    for (const cacheId in this._sheets) {
      const sheets = this._sheets[cacheId];
      for (let i = 0; i < sheets.length; i++) {
        // We take this as an opportunity to clean dead sheets
        try {
          const sheet = sheets[i];
          // If accessing domSheet raises an exception, then the style
          // sheet is a dead object.
          sheet.domSheet;
          callback.call(scope, sheet, i, sheets);
        } catch (e) {
          sheets.splice(i, 1);
          i--;
        }
      }
    }
  }

  /**

  /**
   * Get the number CSSRule objects in the document, counted from all of
   * the stylesheets. System sheets are excluded. If a filter is active, this
   * tells only the number of CSSRule objects inside the selected
   * CSSStyleSheet.
   *
   * WARNING: This only provides an estimate of the rule count, and the results
   * could change at a later date. Todo remove this
   *
   * @return {number} the number of CSSRule (all rules).
   */
  get ruleCount() {
    if (!this._sheetsCached) {
      this._cacheSheets();
    }

    return this._ruleCount;
  }

  /**
   * Process the CssSelector objects that match the highlighted element and its
   * parent elements. scope.callback() is executed for each CssSelector
   * object, being passed the CssSelector object and the match status.
   *
   * This method also includes all of the element.style properties, for each
   * highlighted element parent and for the highlighted element itself.
   *
   * Note that the matched selectors are cached, such that next time your
   * callback is invoked for the cached list of CssSelector objects.
   *
   * @param {function} callback the function you want to execute for each of
   * the matched selectors.
   * @param {object} scope the scope you want for the callback function. scope
   * will be the this object when callback executes.
   */
  processMatchedSelectors(callback, scope) {
    if (this._matchedSelectors) {
      if (callback) {
        this._passId++;
        this._matchedSelectors.forEach(function (value) {
          callback.call(scope, value[0], value[1]);
          value[0].cssRule._passId = this._passId;
        }, this);
      }
      return;
    }

    if (!this._matchedRules) {
      this._buildMatchedRules();
    }

    this._matchedSelectors = [];
    this._passId++;

    for (const matchedRule of this._matchedRules) {
      const [rule, status, distance] = matchedRule;

      rule.selectors.forEach(function (selector) {
        if (
          selector._matchId !== this._matchId &&
          (selector.inlineStyle ||
            this.selectorMatchesElement(rule.domRule, selector.selectorIndex))
        ) {
          selector._matchId = this._matchId;
          this._matchedSelectors.push([selector, status, distance]);
          if (callback) {
            callback.call(scope, selector, status, distance);
          }
        }
      }, this);

      rule._passId = this._passId;
    }
  }

  /**
   * Check if the given selector matches the highlighted element or any of its
   * parents.
   *
   * @private
   * @param {DOMRule} domRule
   *        The DOM Rule containing the selector.
   * @param {Number} idx
   *        The index of the selector within the DOMRule.
   * @return {boolean}
   *         true if the given selector matches the highlighted element or any
   *         of its parents, otherwise false is returned.
   */
  selectorMatchesElement(domRule, idx) {
    let element = this.viewedElement;
    do {
      if (domRule.selectorMatchesElement(idx, element)) {
        return true;
      }

      for (const pseudoElement of ELEMENT_BACKED_PSEUDO_ELEMENTS) {
        if (domRule.selectorMatchesElement(idx, element, pseudoElement)) {
          return true;
        }
      }
    } while (
      // Loop on flattenedTreeParentNode instead of parentNode to reach the
      // shadow host from the shadow dom.
      (element = element.flattenedTreeParentNode) &&
      element.nodeType === nodeConstants.ELEMENT_NODE
    );

    return false;
  }

  /**
   * Check if the highlighted element or its parents have matched selectors.
   *
   * @param {Array<String>} properties: The list of properties you want to check if they
   * have matched selectors or not. For CSS variables, this will check if the variable
   * is set OR used in a matching rule.
   * @return {Set<String>} A Set containing the properties that do have matched selectors.
   */
  hasMatchedSelectors(properties) {
    if (!this._matchedRules) {
      this._buildMatchedRules();
    }

    const result = new Set();

    for (const [rule, status] of this._matchedRules) {
      // Getting the rule cssText can be costly, so cache it
      let cssText;
      const getCssText = () => {
        if (cssText === undefined) {
          cssText = rule.domRule.cssText;
        }
        return cssText;
      };

      // Loop through properties in reverse as we're removing items from it and we don't
      // want to mess with the iteration.
      for (let i = properties.length - 1; i >= 0; i--) {
        const property = properties[i];
        // We just need to find if a rule has this property while it matches
        // the viewedElement (or its parents).
        if (
          // check if the property is assigned
          (rule.getPropertyValue(property) ||
            // or if this is a css variable, if it's being used in the rule.
            (property.startsWith("--") &&
              // we may have false positive for dashed ident or the variable being
              // used in comment/string, but the tradeoff seems okay, as we would have
              // to parse the value of each declaration, which could be costly.
              new RegExp(`${property}[^A-Za-z0-9_-]`).test(getCssText()))) &&
          (status == STATUS.MATCHED ||
            (status == STATUS.PARENT_MATCH &&
              InspectorUtils.isInheritedProperty(
                this.viewedDocument,
                property
              )))
        ) {
          result.add(property);
          // Once the property has a matched selector, we can remove it from the array
          properties.splice(i, 1);
        }
      }

      if (!properties.length) {
        return result;
      }
    }

    return result;
  }

  /**
   * Build the array of matched rules for the currently highlighted element.
   * The array will hold rules that match the viewedElement and its parents.
   *
   * @private
   */
  _buildMatchedRules() {
    let domRules;
    let element = this.viewedElement;
    const filter = this.sourceFilter;
    let sheetIndex = 0;

    // distance is used to tell us how close an ancestor is to an element e.g.
    //  0: The rule is directly applied to the current element.
    // -1: The rule is inherited from the current element's first parent.
    // -2: The rule is inherited from the current element's second parent.
    // etc.
    let distance = 0;

    this._matchId++;
    this._passId++;
    this._matchedRules = [];

    if (!element) {
      return;
    }

    do {
      const status =
        this.viewedElement === element ? STATUS.MATCHED : STATUS.PARENT_MATCH;

      try {
        domRules = getMatchingCSSRules(element);
      } catch (ex) {
        console.log("CL__buildMatchedRules error: " + ex);
        continue;
      }

      // Add element.style information. Order matters here, and style attribute wins over
      // other rules, so we need to add it in `this._matchesRules` before the regular rules.
      if (element.style && element.style.length) {
        const rule = new CssRule(null, { style: element.style }, element);
        rule._matchId = this._matchId;
        rule._passId = this._passId;
        this._matchedRules.push([rule, status, distance]);
      }

      // getMatchingCSSRules can return null with a shadow DOM element.
      if (domRules !== null) {
        // getMatchingCSSRules returns ordered from least-specific to most-specific,
        // but we do want them from most-specific to least specific, so we need to loop
        // through the rules backward.
        for (let i = domRules.length - 1; i >= 0; i--) {
          const domRule = domRules[i];
          const sheet = this.getSheet(domRule.parentStyleSheet, -1);
          if (sheet._passId !== this._passId) {
            sheet.index = sheetIndex++;
            sheet._passId = this._passId;
          }

          if (filter === FILTER.USER && !sheet.authorSheet) {
            continue;
          }

          const rule = sheet.getRule(domRule);
          if (rule._passId === this._passId) {
            continue;
          }

          rule._matchId = this._matchId;
          rule._passId = this._passId;
          this._matchedRules.push([rule, status, distance]);
        }
      }

      distance--;
    } while (
      // Loop on flattenedTreeParentNode instead of parentNode to reach the
      // shadow host from the shadow dom.
      (element = element.flattenedTreeParentNode) &&
      element.nodeType === nodeConstants.ELEMENT_NODE
    );
  }

  /**
   * Tells if the given DOM CSS object matches the current view media.
   *
   * @param {object} domObject The DOM CSS object to check.
   * @return {boolean} True if the DOM CSS object matches the current view
   * media, or false otherwise.
   */
  mediaMatches(domObject) {
    const mediaText = domObject.media.mediaText;
    return (
      !mediaText ||
      this.viewedDocument.defaultView.matchMedia(mediaText).matches
    );
  }
}

/**
 * If the element has an id, return '#id'. Otherwise return 'tagname[n]' where
 * n is the index of this element in its siblings.
 * <p>A technically more 'correct' output from the no-id case might be:
 * 'tagname:nth-of-type(n)' however this is unlikely to be more understood
 * and it is longer.
 *
 * @param {Element} element the element for which you want the short name.
 * @return {string} the string to be displayed for element.
 */
CssLogic.getShortName = function (element) {
  if (!element) {
    return "null";
  }
  if (element.id) {
    return "#" + element.id;
  }
  let priorSiblings = 0;
  let temp = element;
  while ((temp = temp.previousElementSibling)) {
    priorSiblings++;
  }
  return element.tagName + "[" + priorSiblings + "]";
};

/**
 * Get a string list of selectors for a given DOMRule.
 *
 * @param {DOMRule} domRule
 *        The DOMRule to parse.
 * @param {Boolean} desugared
 *        Set to true to get the desugared selector (see https://drafts.csswg.org/css-nesting-1/#nest-selector)
 * @return {Array}
 *         An array of string selectors.
 */
CssLogic.getSelectors = function (domRule, desugared = false) {
  if (ChromeUtils.getClassName(domRule) !== "CSSStyleRule") {
    // Return empty array since CSSRule#selectorCount assumes only STYLE_RULE type.
    return [];
  }

  const selectors = [];

  const len = domRule.selectorCount;
  for (let i = 0; i < len; i++) {
    selectors.push(domRule.selectorTextAt(i, desugared));
  }
  return selectors;
};

/**
 * Given a node, check to see if it is a ::before or ::after element.
 * If so, return the node that is accessible from within the document
 * (the parent of the anonymous node), along with which pseudo element
 * it was.  Otherwise, return the node itself.
 *
 * @returns {Object}
 *            - {DOMNode} node The non-anonymous node
 *            - {string} pseudo One of ':marker', ':before', ':after', or null.
 */
CssLogic.getBindingElementAndPseudo = getBindingElementAndPseudo;

/**
 * Get the computed style on a node.  Automatically handles reading
 * computed styles on a ::before/::after element by reading on the
 * parent node with the proper pseudo argument.
 *
 * @param {Node}
 * @returns {CSSStyleDeclaration}
 */
CssLogic.getComputedStyle = function (node) {
  if (
    !node ||
    Cu.isDeadWrapper(node) ||
    node.nodeType !== nodeConstants.ELEMENT_NODE ||
    !node.ownerGlobal
  ) {
    return null;
  }

  const { bindingElement, pseudo } = CssLogic.getBindingElementAndPseudo(node);

  // For reasons that still escape us, pseudo-elements can sometimes be "unattached" (i.e.
  // not have a parentNode defined). This seems to happen when a page is reloaded while
  // the inspector is open. Bailing out here ensures that the inspector does not fail at
  // presenting DOM nodes and CSS styles when this happens. This is a temporary measure.
  // See bug 1506792.
  if (!bindingElement) {
    return null;
  }

  return node.ownerGlobal.getComputedStyle(bindingElement, pseudo);
};

/**
 * Get a source for a stylesheet, taking into account embedded stylesheets
 * for which we need to use document.defaultView.location.href rather than
 * sheet.href
 *
 * @param {CSSStyleSheet} sheet the DOM object for the style sheet.
 * @return {string} the address of the stylesheet.
 */
CssLogic.href = function (sheet) {
  return sheet.href || sheet.associatedDocument.location;
};

/**
 * Returns true if the given node has visited state.
 */
CssLogic.hasVisitedState = hasVisitedState;

class CssSheet {
  /**
   * A safe way to access cached bits of information about a stylesheet.
   *
   * @constructor
   * @param {CssLogic} cssLogic pointer to the CssLogic instance working with
   * this CssSheet object.
   * @param {CSSStyleSheet} domSheet reference to a DOM CSSStyleSheet object.
   * @param {number} index tells the index/position of the stylesheet within the
   * main document.
   */
  constructor(cssLogic, domSheet, index) {
    this._cssLogic = cssLogic;
    this.domSheet = domSheet;
    this.index = this.authorSheet ? index : -100 * index;

    // Cache of the sheets href. Cached by the getter.
    this._href = null;
    // Short version of href for use in select boxes etc. Cached by getter.
    this._shortSource = null;

    // null for uncached.
    this._sheetAllowed = null;

    // Cached CssRules from the given stylesheet.
    this._rules = {};

    this._ruleCount = -1;
  }

  _passId = null;
  _agentSheet = null;
  _authorSheet = null;
  _userSheet = null;

  /**
   * Check if the stylesheet is an agent stylesheet (provided by the browser).
   *
   * @return {boolean} true if this is an agent stylesheet, false otherwise.
   */
  get agentSheet() {
    if (this._agentSheet === null) {
      this._agentSheet = isAgentStylesheet(this.domSheet);
    }
    return this._agentSheet;
  }

  /**
   * Check if the stylesheet is an author stylesheet (provided by the content page).
   *
   * @return {boolean} true if this is an author stylesheet, false otherwise.
   */
  get authorSheet() {
    if (this._authorSheet === null) {
      this._authorSheet = isAuthorStylesheet(this.domSheet);
    }
    return this._authorSheet;
  }

  /**
   * Check if the stylesheet is a user stylesheet (provided by userChrome.css or
   * userContent.css).
   *
   * @return {boolean} true if this is a user stylesheet, false otherwise.
   */
  get userSheet() {
    if (this._userSheet === null) {
      this._userSheet = isUserStylesheet(this.domSheet);
    }
    return this._userSheet;
  }

  /**
   * Check if the stylesheet is disabled or not.
   * @return {boolean} true if this stylesheet is disabled, or false otherwise.
   */
  get disabled() {
    return this.domSheet.disabled;
  }

  /**
   * Get a source for a stylesheet, using CssLogic.href
   *
   * @return {string} the address of the stylesheet.
   */
  get href() {
    if (this._href) {
      return this._href;
    }

    this._href = CssLogic.href(this.domSheet);
    return this._href;
  }

  /**
   * Create a shorthand version of the href of a stylesheet.
   *
   * @return {string} the shorthand source of the stylesheet.
   */
  get shortSource() {
    if (this._shortSource) {
      return this._shortSource;
    }

    this._shortSource = shortSource(this.domSheet);
    return this._shortSource;
  }

  /**
   * Tells if the sheet is allowed or not by the current CssLogic.sourceFilter.
   *
   * @return {boolean} true if the stylesheet is allowed by the sourceFilter, or
   * false otherwise.
   */
  get sheetAllowed() {
    if (this._sheetAllowed !== null) {
      return this._sheetAllowed;
    }

    this._sheetAllowed = true;

    const filter = this._cssLogic.sourceFilter;
    if (filter === FILTER.USER && !this.authorSheet) {
      this._sheetAllowed = false;
    }
    if (filter !== FILTER.USER && filter !== FILTER.UA) {
      this._sheetAllowed = filter === this.href;
    }

    return this._sheetAllowed;
  }

  /**
   * Retrieve the number of rules in this stylesheet.
   *
   * @return {number} the number of CSSRule objects in this stylesheet.
   */
  get ruleCount() {
    try {
      return this._ruleCount > -1 ? this._ruleCount : this.getCssRules().length;
    } catch (e) {
      return 0;
    }
  }

  /**
   * Retrieve the array of css rules for this stylesheet.
   *
   * Accessing cssRules on a stylesheet that is not completely loaded can throw a
   * DOMException (Bug 625013). This wrapper will return an empty array instead.
   *
   * @return {Array} array of css rules.
   **/
  getCssRules() {
    try {
      return this.domSheet.cssRules;
    } catch (e) {
      return [];
    }
  }

  /**
   * Retrieve a CssRule object for the given CSSStyleRule. The CssRule object is
   * cached, such that subsequent retrievals return the same CssRule object for
   * the same CSSStyleRule object.
   *
   * @param {CSSStyleRule} aDomRule the CSSStyleRule object for which you want a
   * CssRule object.
   * @return {CssRule} the cached CssRule object for the given CSSStyleRule
   * object.
   */
  getRule(domRule) {
    const cacheId = domRule.type + domRule.selectorText;

    let rule = null;
    let ruleFound = false;

    if (cacheId in this._rules) {
      for (rule of this._rules[cacheId]) {
        if (rule.domRule === domRule) {
          ruleFound = true;
          break;
        }
      }
    }

    if (!ruleFound) {
      if (!(cacheId in this._rules)) {
        this._rules[cacheId] = [];
      }

      rule = new CssRule(this, domRule);
      this._rules[cacheId].push(rule);
    }

    return rule;
  }

  toString() {
    return "CssSheet[" + this.shortSource + "]";
  }
}

class CssRule {
  /**
   * Information about a single CSSStyleRule.
   *
   * @param {CSSStyleSheet|null} cssSheet the CssSheet object of the stylesheet that
   * holds the CSSStyleRule. If the rule comes from element.style, set this
   * argument to null.
   * @param {CSSStyleRule|object} domRule the DOM CSSStyleRule for which you want
   * to cache data. If the rule comes from element.style, then provide
   * an object of the form: {style: element.style}.
   * @param {Element} [element] If the rule comes from element.style, then this
   * argument must point to the element.
   * @constructor
   */
  constructor(cssSheet, domRule, element) {
    this._cssSheet = cssSheet;
    this.domRule = domRule;

    if (this._cssSheet) {
      // parse domRule.selectorText on call to this.selectors
      this._selectors = null;
      this.line = InspectorUtils.getRelativeRuleLine(this.domRule);
      this.column = InspectorUtils.getRuleColumn(this.domRule);
      this.href = this._cssSheet.href;
      this.authorRule = this._cssSheet.authorSheet;
      this.userRule = this._cssSheet.userSheet;
      this.agentRule = this._cssSheet.agentSheet;
    } else if (element) {
      this._selectors = [new CssSelector(this, "@element.style", 0)];
      this.line = -1;
      this.href = "#";
      this.authorRule = true;
      this.userRule = false;
      this.agentRule = false;
      this.sourceElement = element;
    }
  }

  _passId = null;

  /**
   * Check if the parent stylesheet is allowed by the CssLogic.sourceFilter.
   *
   * @return {boolean} true if the parent stylesheet is allowed by the current
   * sourceFilter, or false otherwise.
   */
  get sheetAllowed() {
    return this._cssSheet ? this._cssSheet.sheetAllowed : true;
  }

  /**
   * Retrieve the parent stylesheet index/position in the viewed document.
   *
   * @return {number} the parent stylesheet index/position in the viewed
   * document.
   */
  get sheetIndex() {
    return this._cssSheet ? this._cssSheet.index : 0;
  }

  /**
   * Retrieve the style property value from the current CSSStyleRule.
   *
   * @param {string} property the CSS property name for which you want the
   * value.
   * @return {string} the property value.
   */
  getPropertyValue(property) {
    return this.domRule.style.getPropertyValue(property);
  }

  /**
   * Retrieve the style property priority from the current CSSStyleRule.
   *
   * @param {string} property the CSS property name for which you want the
   * priority.
   * @return {string} the property priority.
   */
  getPropertyPriority(property) {
    return this.domRule.style.getPropertyPriority(property);
  }

  /**
   * Retrieve the list of CssSelector objects for each of the parsed selectors
   * of the current CSSStyleRule.
   *
   * @return {array} the array hold the CssSelector objects.
   */
  get selectors() {
    if (this._selectors) {
      return this._selectors;
    }

    // Parse the CSSStyleRule.selectorText string.
    this._selectors = [];

    if (!this.domRule.selectorText) {
      return this._selectors;
    }

    const selectors = CssLogic.getSelectors(this.domRule);

    for (let i = 0, len = selectors.length; i < len; i++) {
      this._selectors.push(new CssSelector(this, selectors[i], i));
    }

    return this._selectors;
  }

  toString() {
    return "[CssRule " + this.domRule.selectorText + "]";
  }
}

class CssSelector {
  /**
   * The CSS selector class allows us to document the ranking of various CSS
   * selectors.
   *
   * @constructor
   * @param {CssRule} cssRule the CssRule instance from where the selector comes.
   * @param {string} selector The selector that we wish to investigate.
   * @param {Number} index The index of the selector within it's rule.
   */
  constructor(cssRule, selector, index) {
    this.cssRule = cssRule;
    this.text = selector;
    this.inlineStyle = this.text == "@element.style";
    this._specificity = null;
    this.selectorIndex = index;
  }

  _matchId = null;

  /**
   * Retrieve the CssSelector source element, which is the source of the CssRule
   * owning the selector. This is only available when the CssSelector comes from
   * an element.style.
   *
   * @return {string} the source element selector.
   */
  get sourceElement() {
    return this.cssRule.sourceElement;
  }

  /**
   * Retrieve the address of the CssSelector. This points to the address of the
   * CssSheet owning this selector.
   *
   * @return {string} the address of the CssSelector.
   */
  get href() {
    return this.cssRule.href;
  }

  /**
   * Check if the selector comes from an agent stylesheet (provided by the browser).
   *
   * @return {boolean} true if this is an agent stylesheet, false otherwise.
   */
  get agentRule() {
    return this.cssRule.agentRule;
  }

  /**
   * Check if the selector comes from an author stylesheet (provided by the content page).
   *
   * @return {boolean} true if this is an author stylesheet, false otherwise.
   */
  get authorRule() {
    return this.cssRule.authorRule;
  }

  /**
   * Check if the selector comes from a user stylesheet (provided by userChrome.css or
   * userContent.css).
   *
   * @return {boolean} true if this is a user stylesheet, false otherwise.
   */
  get userRule() {
    return this.cssRule.userRule;
  }

  /**
   * Check if the parent stylesheet is allowed by the CssLogic.sourceFilter.
   *
   * @return {boolean} true if the parent stylesheet is allowed by the current
   * sourceFilter, or false otherwise.
   */
  get sheetAllowed() {
    return this.cssRule.sheetAllowed;
  }

  /**
   * Retrieve the parent stylesheet index/position in the viewed document.
   *
   * @return {number} the parent stylesheet index/position in the viewed
   * document.
   */
  get sheetIndex() {
    return this.cssRule.sheetIndex;
  }

  /**
   * Retrieve the line of the parent CSSStyleRule in the parent CSSStyleSheet.
   *
   * @return {number} the line of the parent CSSStyleRule in the parent
   * stylesheet.
   */
  get ruleLine() {
    return this.cssRule.line;
  }

  /**
   * Retrieve the column of the parent CSSStyleRule in the parent CSSStyleSheet.
   *
   * @return {number} the column of the parent CSSStyleRule in the parent
   * stylesheet.
   */
  get ruleColumn() {
    return this.cssRule.column;
  }

  /**
   * Retrieve specificity information for the current selector.
   *
   * @see http://www.w3.org/TR/css3-selectors/#specificity
   * @see http://www.w3.org/TR/CSS2/selector.html
   *
   * @return {Number} The selector's specificity.
   */
  get specificity() {
    if (this.inlineStyle) {
      // We can't ask specificity from DOMUtils as element styles don't provide
      // CSSStyleRule interface DOMUtils expect. However, specificity of element
      // style is constant, 1,0,0,0 or 0x40000000, just return the constant
      // directly. @see http://www.w3.org/TR/CSS2/cascade.html#specificity
      return 0x40000000;
    }

    if (typeof this._specificity !== "number") {
      this._specificity = this.cssRule.domRule.selectorSpecificityAt(
        this.selectorIndex
      );
    }

    return this._specificity;
  }

  toString() {
    return this.text;
  }
}

class CssPropertyInfo {
  /**
   * A cache of information about the matched rules, selectors and values attached
   * to a CSS property, for the highlighted element.
   *
   * The heart of the CssPropertyInfo object is the _findMatchedSelectors()
   * method. This are invoked when the PropertyView tries to access the
   * .matchedSelectors array.
   * Results are cached, for later reuse.
   *
   * @param {CssLogic} cssLogic Reference to the parent CssLogic instance
   * @param {string} property The CSS property we are gathering information for
   * @constructor
   */
  constructor(cssLogic, property) {
    this._cssLogic = cssLogic;
    this.property = property;
    this._value = "";

    // An array holding CssSelectorInfo objects for each of the matched selectors
    // that are inside a CSS rule. Only rules that hold the this.property are
    // counted. This includes rules that come from filtered stylesheets (those
    // that have sheetAllowed = false).
    this._matchedSelectors = null;
  }

  /**
   * Retrieve the computed style value for the current property, for the
   * highlighted element.
   *
   * @return {string} the computed style value for the current property, for the
   * highlighted element.
   */
  get value() {
    if (!this._value && this._cssLogic.computedStyle) {
      try {
        this._value = this._cssLogic.computedStyle.getPropertyValue(
          this.property
        );
      } catch (ex) {
        console.log("Error reading computed style for " + this.property);
        console.log(ex);
      }
    }
    return this._value;
  }

  /**
   * Retrieve the array holding CssSelectorInfo objects for each of the matched
   * selectors, from each of the matched rules. Only selectors coming from
   * allowed stylesheets are included in the array.
   *
   * @return {array} the list of CssSelectorInfo objects of selectors that match
   * the highlighted element and its parents.
   */
  get matchedSelectors() {
    if (!this._matchedSelectors) {
      this._findMatchedSelectors();
    } else if (this.needRefilter) {
      this._refilterSelectors();
    }

    return this._matchedSelectors;
  }

  /**
   * Find the selectors that match the highlighted element and its parents.
   * Uses CssLogic.processMatchedSelectors() to find the matched selectors,
   * passing in a reference to CssPropertyInfo._processMatchedSelector() to
   * create CssSelectorInfo objects, which we then sort
   * @private
   */
  _findMatchedSelectors() {
    this._matchedSelectors = [];
    this.needRefilter = false;

    this._cssLogic.processMatchedSelectors(this._processMatchedSelector, this);

    // Sort the selectors by how well they match the given element.
    this._matchedSelectors.sort((selectorInfo1, selectorInfo2) =>
      selectorInfo1.compareTo(selectorInfo2, this._matchedSelectors)
    );

    // Now we know which of the matches is best, we can mark it BEST_MATCH.
    if (
      this._matchedSelectors.length &&
      this._matchedSelectors[0].status > STATUS.UNMATCHED
    ) {
      this._matchedSelectors[0].status = STATUS.BEST;
    }
  }

  /**
   * Process a matched CssSelector object.
   *
   * @private
   * @param {CssSelector} selector: the matched CssSelector object.
   * @param {STATUS} status: the CssSelector match status.
   * @param {Int} distance: See CssLogic._buildMatchedRules for definition.
   */
  _processMatchedSelector(selector, status, distance) {
    const cssRule = selector.cssRule;
    const value = cssRule.getPropertyValue(this.property);
    if (
      value &&
      (status == STATUS.MATCHED ||
        (status == STATUS.PARENT_MATCH &&
          InspectorUtils.isInheritedProperty(
            this._cssLogic.viewedDocument,
            this.property
          )))
    ) {
      const selectorInfo = new CssSelectorInfo(
        selector,
        this.property,
        value,
        status,
        distance
      );
      this._matchedSelectors.push(selectorInfo);
    }
  }

  /**
   * Refilter the matched selectors array when the CssLogic.sourceFilter
   * changes. This allows for quick filter changes.
   * @private
   */
  _refilterSelectors() {
    const passId = ++this._cssLogic._passId;

    const iterator = function (selectorInfo) {
      const cssRule = selectorInfo.selector.cssRule;
      if (cssRule._passId != passId) {
        cssRule._passId = passId;
      }
    };

    if (this._matchedSelectors) {
      this._matchedSelectors.forEach(iterator);
    }

    this.needRefilter = false;
  }

  toString() {
    return "CssPropertyInfo[" + this.property + "]";
  }
}

class CssSelectorInfo {
  /**
   * A class that holds information about a given CssSelector object.
   *
   * Instances of this class are given to CssHtmlTree in the array of matched
   * selectors. Each such object represents a displayable row in the PropertyView
   * objects. The information given by this object blends data coming from the
   * CssSheet, CssRule and from the CssSelector that own this object.
   *
   * @param {CssSelector} selector The CssSelector object for which to
   *        present information.
   * @param {string} property The property for which information should
   *        be retrieved.
   * @param {string} value The property value from the CssRule that owns
   *        the selector.
   * @param {STATUS} status The selector match status.
   * @param {number} distance See CssLogic._buildMatchedRules for definition.
   * @constructor
   */
  constructor(selector, property, value, status, distance) {
    this.selector = selector;
    this.property = property;
    this.status = status;
    this.distance = distance;
    this.value = value;
    const priority = this.selector.cssRule.getPropertyPriority(this.property);
    this.important = priority === "important";

    // Array<string|CSSLayerBlockRule>
    this.parentLayers = [];

    // Go through all parent rules to populate this.parentLayers
    let rule = selector.cssRule.domRule;
    while (rule) {
      const className = ChromeUtils.getClassName(rule);
      if (className == "CSSLayerBlockRule") {
        // If the layer has a name, it's enough to uniquely identify it
        // If the layer does not have a name. We put the actual rule here, so we'll
        // be able to compare actual rule instances in `compareTo`
        this.parentLayers.push(rule.name || rule);
      } else if (className == "CSSImportRule" && rule.layerName !== null) {
        // Same reasoning for @import rule + layer
        this.parentLayers.push(rule.layerName || rule);
      }

      // Get the parent rule (could be the parent stylesheet owner rule
      // for `@import url(path/to/file.css) layer`)
      rule = rule.parentRule || rule.parentStyleSheet?.ownerRule;
    }
  }

  /**
   * Retrieve the CssSelector source element, which is the source of the CssRule
   * owning the selector. This is only available when the CssSelector comes from
   * an element.style.
   *
   * @return {string} the source element selector.
   */
  get sourceElement() {
    return this.selector.sourceElement;
  }

  /**
   * Retrieve the address of the CssSelector. This points to the address of the
   * CssSheet owning this selector.
   *
   * @return {string} the address of the CssSelector.
   */
  get href() {
    return this.selector.href;
  }

  /**
   * Check if the CssSelector comes from element.style or not.
   *
   * @return {boolean} true if the CssSelector comes from element.style, or
   * false otherwise.
   */
  get inlineStyle() {
    return this.selector.inlineStyle;
  }

  /**
   * Retrieve specificity information for the current selector.
   *
   * @return {object} an object holding specificity information for the current
   * selector.
   */
  get specificity() {
    return this.selector.specificity;
  }

  /**
   * Retrieve the parent stylesheet index/position in the viewed document.
   *
   * @return {number} the parent stylesheet index/position in the viewed
   * document.
   */
  get sheetIndex() {
    return this.selector.sheetIndex;
  }

  /**
   * Check if the parent stylesheet is allowed by the CssLogic.sourceFilter.
   *
   * @return {boolean} true if the parent stylesheet is allowed by the current
   * sourceFilter, or false otherwise.
   */
  get sheetAllowed() {
    return this.selector.sheetAllowed;
  }

  /**
   * Retrieve the line of the parent CSSStyleRule in the parent CSSStyleSheet.
   *
   * @return {number} the line of the parent CSSStyleRule in the parent
   * stylesheet.
   */
  get ruleLine() {
    return this.selector.ruleLine;
  }

  /**
   * Retrieve the column of the parent CSSStyleRule in the parent CSSStyleSheet.
   *
   * @return {number} the column of the parent CSSStyleRule in the parent
   * stylesheet.
   */
  get ruleColumn() {
    return this.selector.ruleColumn;
  }

  /**
   * Check if the selector comes from a browser-provided stylesheet.
   *
   * @return {boolean} true if the selector comes from a browser-provided
   * stylesheet, or false otherwise.
   */
  get agentRule() {
    return this.selector.agentRule;
  }

  /**
   * Check if the selector comes from a webpage-provided stylesheet.
   *
   * @return {boolean} true if the selector comes from a webpage-provided
   * stylesheet, or false otherwise.
   */
  get authorRule() {
    return this.selector.authorRule;
  }

  /**
   * Check if the selector comes from a user stylesheet (userChrome.css or
   * userContent.css).
   *
   * @return {boolean} true if the selector comes from a webpage-provided
   * stylesheet, or false otherwise.
   */
  get userRule() {
    return this.selector.userRule;
  }

  /**
   * Compare the current CssSelectorInfo instance to another instance.
   * Since selectorInfos is computed from `InspectorUtils.getMatchingCSSRules`,
   * it's already sorted for regular cases. We only need to handle important values.
   *
   * @param  {CssSelectorInfo} that
   *         The instance to compare ourselves against.
   * @param  {Array<CssSelectorInfo>} selectorInfos
   *         The list of CssSelectorInfo we are currently ordering
   * @return {Number}
   *         -1, 0, 1 depending on how that compares with this.
   */
  compareTo(that, selectorInfos) {
    const originalOrder =
      selectorInfos.indexOf(this) < selectorInfos.indexOf(that) ? -1 : 1;

    // If both properties are not important, we can keep the original order
    if (!this.important && !that.important) {
      return originalOrder;
    }

    // If one of the property is important and the other is not, the important one wins
    if (this.important !== that.important) {
      return this.important ? -1 : 1;
    }

    // At this point, this and that are both important

    const thisIsInLayer = !!this.parentLayers.length;
    const thatIsInLayer = !!that.parentLayers.length;

    // If they're not in layers, we can keep the original rule order
    if (!thisIsInLayer && !thatIsInLayer) {
      return originalOrder;
    }

    // If one of the rule is the style attribute, it wins
    if (this.selector.inlineStyle || that.selector.inlineStyle) {
      return this.selector.inlineStyle ? -1 : 1;
    }

    // If one of the rule is not in a layer, then the rule in a layer wins.
    if (!thisIsInLayer || !thatIsInLayer) {
      return thisIsInLayer ? -1 : 1;
    }

    const inSameLayers =
      this.parentLayers.length === that.parentLayers.length &&
      this.parentLayers.every((layer, i) => layer === that.parentLayers[i]);
    // If both rules are in the same layer, we keep the original order
    if (inSameLayers) {
      return originalOrder;
    }

    // When comparing declarations that belong to different layers, then for
    // important rules the declaration whose cascade layer is first wins.
    // We get the rules in the most-specific to least-specific order, meaning we'll have
    // rules in layers in the reverse order of the order of declarations of layers.
    // We can reverse that again to get the order of declarations of layers.
    return originalOrder * -1;
  }

  compare(that, propertyName, type) {
    switch (type) {
      case COMPAREMODE.BOOLEAN:
        if (this[propertyName] && !that[propertyName]) {
          return -1;
        }
        if (!this[propertyName] && that[propertyName]) {
          return 1;
        }
        break;
      case COMPAREMODE.INTEGER:
        if (this[propertyName] > that[propertyName]) {
          return -1;
        }
        if (this[propertyName] < that[propertyName]) {
          return 1;
        }
        break;
    }
    return 0;
  }

  toString() {
    return this.selector + " -> " + this.value;
  }
}

exports.CssLogic = CssLogic;
exports.CssSelector = CssSelector;
