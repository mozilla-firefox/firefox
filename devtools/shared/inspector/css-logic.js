/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const MAX_DATA_URL_LENGTH = 40;
/**
 * Provide access to the style information in a page.
 * CssLogic uses the standard DOM API, and the Gecko InspectorUtils API to
 * access styling information in the page, and present this to the user in a way
 * that helps them understand:
 * - why their expectations may not have been fulfilled
 * - how browsers process CSS
 * @constructor
 */

loader.lazyRequireGetter(
  this,
  "InspectorCSSParserWrapper",
  "resource://devtools/shared/css/lexer.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getTabPrefs",
  "resource://devtools/shared/indentation.js",
  true
);
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const styleInspectorL10N = new LocalizationHelper(
  "devtools/shared/locales/styleinspector.properties"
);

/**
 * Special values for filter, in addition to an href these values can be used
 */
exports.FILTER = {
  // show properties for all user style sheets.
  USER: "user",
  // USER, plus user-agent (i.e. browser) style sheets
  UA: "ua",
};

/**
 * Each rule has a status, the bigger the number, the better placed it is to
 * provide styling information.
 *
 * These statuses are localized inside the styleinspector.properties
 * string bundle.
 * @see csshtmltree.js RuleView._cacheStatusNames()
 */
exports.STATUS = {
  BEST: 3,
  MATCHED: 2,
  PARENT_MATCH: 1,
  UNMATCHED: 0,
  UNKNOWN: -1,
};

/**
 * Mapping of CSS at-Rule className to CSSRule type name.
 */
exports.CSSAtRuleClassNameType = {
  CSSContainerRule: "container",
  CSSCounterStyleRule: "counter-style",
  CSSDocumentRule: "document",
  CSSFontFaceRule: "font-face",
  CSSFontFeatureValuesRule: "font-feature-values",
  CSSImportRule: "import",
  CSSKeyframeRule: "keyframe",
  CSSKeyframesRule: "keyframes",
  CSSLayerBlockRule: "layer",
  CSSMediaRule: "media",
  CSSNamespaceRule: "namespace",
  CSSPageRule: "page",
  CSSScopeRule: "scope",
  CSSStartingStyleRule: "starting-style",
  CSSSupportsRule: "supports",
};

/**
 * Get Rule type as human-readable string (ex: "@media", "@container", …)
 *
 * @param {CSSRule} cssRule
 * @returns {String}
 */
exports.getCSSAtRuleTypeName = function (cssRule) {
  const ruleClassName = ChromeUtils.getClassName(cssRule);
  const atRuleTypeName = exports.CSSAtRuleClassNameType[ruleClassName];
  if (atRuleTypeName) {
    return "@" + atRuleTypeName;
  }

  return "";
};

/**
 * Lookup a l10n string in the shared styleinspector string bundle.
 *
 * @param {String} name
 *        The key to lookup.
 * @returns {String} A localized version of the given key.
 */
exports.l10n = name => styleInspectorL10N.getStr(name);
exports.l10nFormatStr = (name, ...args) =>
  styleInspectorL10N.getFormatStr(name, ...args);

/**
 * Is the given property sheet an author stylesheet?
 *
 * @param {CSSStyleSheet} sheet a stylesheet
 * @return {boolean} true if the given stylesheet is an author stylesheet,
 * false otherwise.
 */
exports.isAuthorStylesheet = function (sheet) {
  return sheet.parsingMode === "author";
};

/**
 * Is the given property sheet a user stylesheet?
 *
 * @param {CSSStyleSheet} sheet a stylesheet
 * @return {boolean} true if the given stylesheet is a user stylesheet,
 * false otherwise.
 */
exports.isUserStylesheet = function (sheet) {
  return sheet.parsingMode === "user";
};

/**
 * Is the given property sheet a agent stylesheet?
 *
 * @param {CSSStyleSheet} sheet a stylesheet
 * @return {boolean} true if the given stylesheet is a agent stylesheet,
 * false otherwise.
 */
exports.isAgentStylesheet = function (sheet) {
  return sheet.parsingMode === "agent";
};

/**
 * Return a shortened version of a style sheet's source.
 *
 * @param {CSSStyleSheet} sheet the DOM object for the style sheet.
 */
exports.shortSource = function (sheet) {
  if (!sheet) {
    return exports.l10n("rule.sourceInline");
  }

  if (!sheet.href) {
    return exports.l10n(
      sheet.constructed ? "rule.sourceConstructed" : "rule.sourceInline"
    );
  }

  let name = sheet.href;

  // If the sheet is a data URL, return a trimmed version of it.
  const dataUrl = sheet.href.trim().match(/^data:.*?,((?:.|\r|\n)*)$/);
  if (dataUrl) {
    name =
      dataUrl[1].length > MAX_DATA_URL_LENGTH
        ? `${dataUrl[1].substr(0, MAX_DATA_URL_LENGTH - 1)}…`
        : dataUrl[1];
  } else {
    // We try, in turn, the filename, filePath, query string, whole thing
    const url = URL.parse(sheet.href);
    if (url) {
      if (url.pathname) {
        const index = url.pathname.lastIndexOf("/");
        if (index !== -1 && index < url.pathname.length) {
          name = url.pathname.slice(index + 1);
        } else {
          name = url.pathname;
        }
      } else if (url.query) {
        name = url.query;
      }
    } // else some UA-provided stylesheets are not valid URLs.
  }

  try {
    name = decodeURIComponent(name);
  } catch (e) {
    // This may still fail if the URL contains invalid % numbers (for ex)
  }

  return name;
};

/**
 * Return the style sheet's source, handling element, inline and constructed stylesheets.
 *
 * @param {CSSStyleSheet} sheet the DOM object for the style sheet.
 */
exports.longSource = function (sheet) {
  if (!sheet) {
    return exports.l10n("rule.sourceInline");
  }

  if (!sheet.href) {
    return exports.l10n(
      sheet.constructed ? "rule.sourceConstructed" : "rule.sourceInline"
    );
  }

  return sheet.href;
};

const TAB_CHARS = "\t";
const SPACE_CHARS = " ";

function getLineCountInComments(text) {
  let count = 0;

  for (const comment of text.match(/\/\*(?:.|\n)*?\*\//gm) || []) {
    count += comment.split("\n").length + 1;
  }

  return count;
}

/**
 * Prettify minified CSS text.
 * This prettifies CSS code where there is no indentation in usual places while
 * keeping original indentation as-is elsewhere.
 *
 * Returns an object with the resulting prettified source and a list of mappings of
 * token positions between the original and the prettified source. Each single mapping
 * is an object that looks like this:
 *
 * {
 *  original: {line: {Number}, column: {Number}},
 *  generated: {line: {Number}, column: {Number}},
 * }
 *
 * @param  {String} text
 *         The CSS source to prettify.
 * @param  {Number} ruleCount
 *         The number of CSS rules expected in the CSS source.
 *         Set to null to force the text to be pretty-printed.
 *
 * @return {Object}
 *         Object with the prettified source and source mappings.
 *          {
 *            result: {String}  // Prettified source
 *            mappings: {Array} // List of objects with mappings for lines and columns
 *                              // between the original source and prettified source
 *          }
 */
// eslint-disable-next-line complexity
function prettifyCSS(text, ruleCount) {
  if (prettifyCSS.LINE_SEPARATOR == null) {
    const os = Services.appinfo.OS;
    prettifyCSS.LINE_SEPARATOR = os === "WINNT" ? "\r\n" : "\n";
  }

  // Stylesheets may start and end with HTML comment tags (possibly with whitespaces
  // before and after). Remove those first. Don't do anything there aren't any.
  const trimmed = text.trim();
  if (trimmed.startsWith("<!--")) {
    text = trimmed.replace(/^<!--/, "").replace(/-->$/, "").trim();
  }

  const originalText = text;
  text = text.trim();

  // don't attempt to prettify if there's more than one line per rule, excluding comments.
  const lineCount = text.split("\n").length - 1 - getLineCountInComments(text);
  if (ruleCount !== null && lineCount >= ruleCount) {
    return { result: originalText, mappings: [] };
  }

  // We reformat the text using a simple state machine.  The
  // reformatting preserves most of the input text, changing only
  // whitespace.  The rules are:
  //
  // * After a "{" or ";" symbol, ensure there is a newline and
  //   indentation before the next non-comment, non-whitespace token.
  // * Additionally after a "{" symbol, increase the indentation.
  // * A "}" symbol ensures there is a preceding newline, and
  //   decreases the indentation level.
  // * Ensure there is whitespace before a "{".
  //
  // This approach can be confused sometimes, but should do ok on a
  // minified file.
  let indent = "";
  let indentLevel = 0;
  const lexer = new InspectorCSSParserWrapper(text);
  // List of mappings of token positions from original source to prettified source.
  const mappings = [];
  // Line and column offsets used to shift the token positions after prettyfication.
  let lineOffset = 0;
  let columnOffset = 0;
  let indentOffset = 0;
  let result = "";
  let pushbackToken = undefined;

  // A helper function that reads tokens, looking for the next
  // non-comment, non-whitespace token.  Comment and whitespace tokens
  // are appended to |result|.  If this encounters EOF, it returns
  // null.  Otherwise it returns the last whitespace token that was
  // seen.  This function also updates |pushbackToken|.
  const readUntilSignificantToken = () => {
    while (true) {
      const token = lexer.nextToken();
      if (!token || token.tokenType !== "WhiteSpace") {
        pushbackToken = token;
        return token;
      }
      // Saw whitespace.  Before committing to it, check the next
      // token.
      const nextToken = lexer.nextToken();
      if (!nextToken || nextToken.tokenType !== "Comment") {
        pushbackToken = nextToken;
        return token;
      }
      // Saw whitespace + comment.  Update the result and continue.
      result = result + text.substring(token.startOffset, nextToken.endOffset);
    }
  };

  // State variables for readUntilNewlineNeeded.
  //
  // Starting index of the accumulated tokens.
  let startIndex;
  // Ending index of the accumulated tokens.
  let endIndex;
  // True if any non-whitespace token was seen.
  let anyNonWS;
  // True if the terminating token is "}".
  let isCloseBrace;
  // True if the token just before the terminating token was
  // whitespace.
  let lastWasWS;
  // True if the current token is inside a CSS selector.
  let isInSelector = true;
  // True if the current token is inside an at-rule definition.
  let isInAtRuleDefinition = false;

  // A helper function that reads tokens until there is a reason to
  // insert a newline.  This updates the state variables as needed.
  // If this encounters EOF, it returns null.  Otherwise it returns
  // the final token read.  Note that if the returned token is "{",
  // then it will not be included in the computed start/end token
  // range.  This is used to handle whitespace insertion before a "{".
  const readUntilNewlineNeeded = () => {
    let token;
    while (true) {
      if (pushbackToken) {
        token = pushbackToken;
        pushbackToken = undefined;
      } else {
        token = lexer.nextToken();
      }
      if (!token) {
        endIndex = text.length;
        break;
      }

      const line = lexer.lineNumber;
      const column = lexer.columnNumber;
      mappings.push({
        original: {
          line,
          column,
        },
        generated: {
          line: lineOffset + line,
          column: columnOffset,
        },
      });
      // Shift the column offset for the next token by the current token's length.
      columnOffset += token.endOffset - token.startOffset;

      if (token.tokenType === "AtKeyword") {
        isInAtRuleDefinition = true;
      }

      // A "}" symbol must be inserted later, to deal with indentation
      // and newline.
      if (token.tokenType === "CloseCurlyBracket") {
        isInSelector = true;
        isCloseBrace = true;
        break;
      } else if (token.tokenType === "CurlyBracketBlock") {
        if (isInAtRuleDefinition) {
          isInAtRuleDefinition = false;
        } else {
          isInSelector = false;
        }
        break;
      }

      if (token.tokenType !== "WhiteSpace") {
        anyNonWS = true;
      }

      if (startIndex === undefined) {
        startIndex = token.startOffset;
      }
      endIndex = token.endOffset;

      if (token.tokenType === "Semicolon") {
        break;
      }

      if (
        token.tokenType === "Comma" &&
        isInSelector &&
        !isInAtRuleDefinition
      ) {
        break;
      }

      lastWasWS = token.tokenType === "WhiteSpace";
    }
    return token;
  };

  // Get preference of the user regarding what to use for indentation,
  // spaces or tabs.
  const tabPrefs = getTabPrefs();
  const baseIndentString = tabPrefs.indentWithTabs
    ? TAB_CHARS
    : SPACE_CHARS.repeat(tabPrefs.indentUnit);

  while (true) {
    // Set the initial state.
    startIndex = undefined;
    endIndex = undefined;
    anyNonWS = false;
    isCloseBrace = false;
    lastWasWS = false;

    // Read tokens until we see a reason to insert a newline.
    let token = readUntilNewlineNeeded();

    // Append any saved up text to the result, applying indentation.
    if (startIndex !== undefined) {
      if (isCloseBrace && !anyNonWS) {
        // If we saw only whitespace followed by a "}", then we don't
        // need anything here.
      } else {
        result = result + indent + text.substring(startIndex, endIndex);
        if (isCloseBrace) {
          result += prettifyCSS.LINE_SEPARATOR;
          lineOffset = lineOffset + 1;
        }
      }
    }

    if (isCloseBrace) {
      // Even if the stylesheet contains extra closing braces, the indent level should
      // remain > 0.
      indentLevel = Math.max(0, indentLevel - 1);
      indent = baseIndentString.repeat(indentLevel);

      // FIXME: This is incorrect and should be fixed in Bug 1839297
      if (tabPrefs.indentWithTabs) {
        indentOffset = 4 * indentLevel;
      } else {
        indentOffset = 1 * indentLevel;
      }
      result = result + indent + "}";
    }

    if (!token) {
      break;
    }

    if (token.tokenType === "CurlyBracketBlock") {
      if (!lastWasWS) {
        result += " ";
        columnOffset++;
      }
      result += "{";
      indentLevel++;
      indent = baseIndentString.repeat(indentLevel);
      indentOffset = indent.length;

      // FIXME: This is incorrect and should be fixed in Bug 1839297
      if (tabPrefs.indentWithTabs) {
        indentOffset = 4 * indentLevel;
      } else {
        indentOffset = 1 * indentLevel;
      }
    }

    // Now it is time to insert a newline.  However first we want to
    // deal with any trailing comments.
    token = readUntilSignificantToken();

    // "Early" bail-out if the text does not appear to be minified.
    // Here we ignore the case where whitespace appears at the end of
    // the text.
    if (
      ruleCount !== null &&
      pushbackToken &&
      token &&
      token.tokenType === "WhiteSpace" &&
      /\n/g.test(text.substring(token.startOffset, token.endOffset))
    ) {
      return { result: originalText, mappings: [] };
    }

    // Finally time for that newline.
    result = result + prettifyCSS.LINE_SEPARATOR;

    // Update line and column offsets for the new line.
    lineOffset = lineOffset + 1;
    columnOffset = 0 + indentOffset;

    // Maybe we hit EOF.
    if (!pushbackToken) {
      break;
    }
  }

  return { result, mappings };
}

exports.prettifyCSS = prettifyCSS;

/**
 * Given a node, check to see if it is a ::marker, ::before, or ::after element.
 * If so, return the node that is accessible from within the document
 * (the parent of the anonymous node), along with which pseudo element
 * it was.  Otherwise, return the node itself.
 *
 * @returns {Object}
 *            - {DOMNode} node The non-anonymous node
 *            - {string} pseudo One of '::marker', '::before', '::after', or null.
 */
function getBindingElementAndPseudo(node) {
  let bindingElement = node;
  let pseudo = null;
  if (node.nodeName == "_moz_generated_content_marker") {
    bindingElement = node.parentNode;
    pseudo = "::marker";
  } else if (node.nodeName == "_moz_generated_content_before") {
    bindingElement = node.parentNode;
    pseudo = "::before";
  } else if (node.nodeName == "_moz_generated_content_after") {
    bindingElement = node.parentNode;
    pseudo = "::after";
  }
  return {
    bindingElement,
    pseudo,
  };
}
exports.getBindingElementAndPseudo = getBindingElementAndPseudo;

/**
 * Returns css rules for a given a node.
 * This function can handle ::before or ::after pseudo element as well as
 * normal element.
 */
function getMatchingCSSRules(node) {
  const { bindingElement, pseudo } = getBindingElementAndPseudo(node);
  const rules = InspectorUtils.getMatchingCSSRules(bindingElement, pseudo);
  return rules;
}
exports.getMatchingCSSRules = getMatchingCSSRules;

/**
 * Returns true if the given node has visited state.
 */
function hasVisitedState(node) {
  if (!node) {
    return false;
  }

  // ElementState::VISITED
  const ELEMENT_STATE_VISITED = 1 << 18;

  return (
    !!(InspectorUtils.getContentState(node) & ELEMENT_STATE_VISITED) ||
    InspectorUtils.hasPseudoClassLock(node, ":visited")
  );
}
exports.hasVisitedState = hasVisitedState;

/**
 * Find the position of [element] in [nodeList].
 * @returns an index of the match, or -1 if there is no match
 */
function positionInNodeList(element, nodeList) {
  for (let i = 0; i < nodeList.length; i++) {
    if (element === nodeList[i]) {
      return i;
    }
  }
  return -1;
}

/**
 * For a provided node, find the appropriate container/node couple so that
 * container.contains(node) and a CSS selector can be created from the
 * container to the node.
 */
function findNodeAndContainer(node) {
  const shadowRoot = node.containingShadowRoot;
  while (node?.isNativeAnonymous) {
    node = node.parentNode;
  }

  if (shadowRoot) {
    // If the node is under a shadow root, the shadowRoot contains the node and
    // we can find the node via shadowRoot.querySelector(path).
    return {
      containingDocOrShadow: shadowRoot,
      node,
    };
  }

  // Otherwise, get the root binding parent to get a non anonymous element that
  // will be accessible from the ownerDocument.
  return {
    containingDocOrShadow: node.ownerDocument,
    node,
  };
}

/**
 * Find a unique CSS selector for a given element
 * @returns a string such that:
 *   - ele.containingDocOrShadow.querySelector(reply) === ele
 *   - ele.containingDocOrShadow.querySelectorAll(reply).length === 1
 */
const findCssSelector = function (ele) {
  const { node, containingDocOrShadow } = findNodeAndContainer(ele);
  ele = node;

  if (!containingDocOrShadow || !containingDocOrShadow.contains(ele)) {
    // findCssSelector received element not inside container.
    return "";
  }

  const cssEscape = ele.ownerGlobal.CSS.escape;

  // document.querySelectorAll("#id") returns multiple if elements share an ID
  if (
    ele.id &&
    containingDocOrShadow.querySelectorAll("#" + cssEscape(ele.id)).length === 1
  ) {
    return "#" + cssEscape(ele.id);
  }

  // Inherently unique by tag name
  const tagName = ele.localName;
  if (tagName === "html") {
    return "html";
  }
  if (tagName === "head") {
    return "head";
  }
  if (tagName === "body") {
    return "body";
  }

  // We might be able to find a unique class name
  let selector, index, matches;
  for (let i = 0; i < ele.classList.length; i++) {
    // Is this className unique by itself?
    selector = "." + cssEscape(ele.classList.item(i));
    matches = containingDocOrShadow.querySelectorAll(selector);
    if (matches.length === 1) {
      return selector;
    }
    // Maybe it's unique with a tag name?
    selector = cssEscape(tagName) + selector;
    matches = containingDocOrShadow.querySelectorAll(selector);
    if (matches.length === 1) {
      return selector;
    }
    // Maybe it's unique using a tag name and nth-child
    index = positionInNodeList(ele, ele.parentNode.children) + 1;
    selector = selector + ":nth-child(" + index + ")";
    matches = containingDocOrShadow.querySelectorAll(selector);
    if (matches.length === 1) {
      return selector;
    }
  }

  // Not unique enough yet.
  index = positionInNodeList(ele, ele.parentNode.children) + 1;
  selector = cssEscape(tagName) + ":nth-child(" + index + ")";
  if (ele.parentNode !== containingDocOrShadow) {
    selector = findCssSelector(ele.parentNode) + " > " + selector;
  }
  return selector;
};
exports.findCssSelector = findCssSelector;

/**
 * Get the full CSS path for a given element.
 *
 * @returns a string that can be used as a CSS selector for the element. It might not
 * match the element uniquely. It does however, represent the full path from the root
 * node to the element.
 */
function getCssPath(ele) {
  const { node, containingDocOrShadow } = findNodeAndContainer(ele);
  ele = node;
  if (!containingDocOrShadow || !containingDocOrShadow.contains(ele)) {
    // getCssPath received element not inside container.
    return "";
  }

  const nodeGlobal = ele.ownerGlobal.Node;

  const getElementSelector = element => {
    if (!element.localName) {
      return "";
    }

    let label =
      element.nodeName == element.nodeName.toUpperCase()
        ? element.localName.toLowerCase()
        : element.localName;

    if (element.id) {
      label += "#" + element.id;
    }

    if (element.classList) {
      for (const cl of element.classList) {
        label += "." + cl;
      }
    }

    return label;
  };

  const paths = [];

  while (ele) {
    if (!ele || ele.nodeType !== nodeGlobal.ELEMENT_NODE) {
      break;
    }

    paths.splice(0, 0, getElementSelector(ele));
    ele = ele.parentNode;
  }

  return paths.length ? paths.join(" ") : "";
}
exports.getCssPath = getCssPath;

/**
 * Get the xpath for a given element.
 *
 * @param {DomNode} ele
 * @returns a string that can be used as an XPath to find the element uniquely.
 */
function getXPath(ele) {
  const { node, containingDocOrShadow } = findNodeAndContainer(ele);
  ele = node;
  if (!containingDocOrShadow || !containingDocOrShadow.contains(ele)) {
    // getXPath received element not inside container.
    return "";
  }

  // Create a short XPath for elements with IDs.
  if (ele.id) {
    return `//*[@id="${ele.id}"]`;
  }

  // Otherwise walk the DOM up and create a part for each ancestor.
  const parts = [];

  const nodeGlobal = ele.ownerGlobal.Node;
  // Use nodeName (instead of localName) so namespace prefix is included (if any).
  while (ele && ele.nodeType === nodeGlobal.ELEMENT_NODE) {
    let nbOfPreviousSiblings = 0;
    let hasNextSiblings = false;

    // Count how many previous same-name siblings the element has.
    let sibling = ele.previousSibling;
    while (sibling) {
      // Ignore document type declaration.
      if (
        sibling.nodeType !== nodeGlobal.DOCUMENT_TYPE_NODE &&
        sibling.nodeName == ele.nodeName
      ) {
        nbOfPreviousSiblings++;
      }

      sibling = sibling.previousSibling;
    }

    // Check if the element has at least 1 next same-name sibling.
    sibling = ele.nextSibling;
    while (sibling) {
      if (sibling.nodeName == ele.nodeName) {
        hasNextSiblings = true;
        break;
      }
      sibling = sibling.nextSibling;
    }

    const prefix = ele.prefix ? ele.prefix + ":" : "";
    const nth =
      nbOfPreviousSiblings || hasNextSiblings
        ? `[${nbOfPreviousSiblings + 1}]`
        : "";

    parts.push(prefix + ele.localName + nth);

    ele = ele.parentNode;
  }

  return parts.length ? "/" + parts.reverse().join("/") : "";
}
exports.getXPath = getXPath;

/**
 * Build up a regular expression that matches a CSS variable token. This is an
 * ident token that starts with two dashes "--".
 *
 * https://www.w3.org/TR/css-syntax-3/#ident-token-diagram
 */
var NON_ASCII = "[^\\x00-\\x7F]";
var ESCAPE = "\\\\[^\n\r]";
var VALID_CHAR = ["[_a-z0-9-]", NON_ASCII, ESCAPE].join("|");
var IS_VARIABLE_TOKEN = new RegExp(`^--(${VALID_CHAR})*$`, "i");

/**
 * Check that this is a CSS variable.
 *
 * @param {String} input
 * @return {Boolean}
 */
function isCssVariable(input) {
  return !!input.match(IS_VARIABLE_TOKEN);
}
exports.isCssVariable = isCssVariable;

/**
 * This is a list of all the element backed pseudo elements.
 *
 * From https://drafts.csswg.org/css-pseudo-4/#element-backed :
 * > The element-backed pseudo-elements, interact with most CSS and other platform features
 * > as if they were real elements (and, in fact, often are real elements that are
 * > not otherwise selectable).
 *
 * Those pseudo elements are not displayed in the markup view, but declarations in rules
 * targetting them can then be inherited by their "children", and so we need to retrieve
 * those rules to surface them in the Inspector (e.g. in "Inherited" sections in the Rules
 * view, in the matched selectors section in the Computed panel, …).
 *
 * Any new element-backed pseudo elements should be added into this Set.
 */
exports.ELEMENT_BACKED_PSEUDO_ELEMENTS = new Set([
  "::details-content",
  "::file-selector-button",
]);
