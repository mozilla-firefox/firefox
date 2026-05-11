/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { tokensTable } from "../../../../toolkit/themes/shared/design-system/dist/semantic-categories.mjs";

/**
 * The list of system colors that are valid and intended to be used for high contrast/forced colors mode situations.
 */
export const SYSTEM_COLORS = [
  "accentcolor",
  "accentcolortext",
  "activetext",
  "buttonborder",
  "buttonface",
  "buttontext",
  "canvas",
  "canvastext",
  "field",
  "fieldtext",
  "graytext",
  "highlight",
  "highlighttext",
  "linktext",
  "mark",
  "marktext",
  "selecteditem",
  "selecteditemtext",
  "visitedtext",
  // -moz- prefixed colors, used rarely but still valid
  "-moz-buttonactivetext",
  "-moz-buttonhovertext",
  "-moz-combobox",
  "-moz-dialog",
  "-moz-dialogtext",
  "-moz-menuhover",
  "-moz-menuhoverdisabled",
  "-moz-menuhovertext",
  "-moz-menubarhovertext",
  "-moz-headerbar",
  "-moz-headerbarinactive",
  "-moz-headerbarinactivetext",
  "-moz-headerbartext",
];

/**
 * Our namespace used to prefix Mozilla stylelint rules.
 */
const MOZILLA_NAMESPACE = "stylelint-plugin-mozilla";

/**
 * Namespaces Mozilla's stylelint rules.
 *
 * @param {string} ruleName the name of the stylelint rule.
 * @returns {string}
 */
export function namespace(ruleName) {
  return `${MOZILLA_NAMESPACE}/${ruleName}`;
}

/**
 * Collects local (in the same file) CSS properties from a
 * PostCSS object and returns those in object syntax.
 *
 * @param {Record<string, string>} root - A PostCSS value parser root
 * @returns {Record<string, string>}
 */
export const getLocalCustomProperties = root => {
  const cssCustomProperties = {};

  root.walkDecls(decl => {
    if (decl.prop && decl.prop.startsWith("--")) {
      cssCustomProperties[decl.prop] = decl.value;
    }
  });

  return cssCustomProperties;
};

/**
 * Return raw values of tokens for the given categories.
 *
 * @param {string[]} tokenCategoriesArray
 * @returns {object}
 */
export const createRawValuesObject = tokenCategoriesArray =>
  tokenCategoriesArray
    .flatMap(category => tokensTable[category])
    .reduce((acc, token) => {
      const val = String(token.value || "").trim();
      if (token.name && !val.startsWith("var(")) {
        // some tokens refer to tokens in the table,
        // let's move those out so our auto-fixes work
        return { ...acc, [val]: `var(${token.name})` };
      }
      return acc;
    }, {});

/**
 * Various checks for common design token and CSS content.
 *
 * @param {object} node object from PostCSS value-parser
 * @returns {boolean}
 */

// checks if a node is a word
export const isWord = node => node.type === "word";

// checks if a node is a function
export const isFunction = node => node.type === "function";

// checks if a node is a url() function
export const isUrlFunction = node => isFunction(node) && node.value === "url";

/**
 * Trims a value for easier checking.
 *
 * @param {string} value some CSS declaration to match
 * @returns {string}
 */
export const trimValue = value => String(value).trim();

/**
 * Checks whether a value is a system color (e.g. ButtonText, Canvas)
 *
 * @param {string} value
 * @returns {boolean}
 */
export const isSystemColor = value =>
  SYSTEM_COLORS.includes(value.toLowerCase());
