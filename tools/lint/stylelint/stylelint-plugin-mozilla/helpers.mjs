/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import valueParser from "postcss-value-parser";

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
  "-moz-cellhighlight",
  "-moz-cellhighlighttext",
  "-moz-combobox",
  "-moz-default-background-color",
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
 * Return raw values of tokens for the given categories, mapped to the token
 * each one autofixes to. Only a category's own scale tokens (`--<category>`
 * and `--<category>-*`) are included, so a component token that happens to
 * share a value with the scale never becomes the fix.
 *
 * @param {string[]} tokenCategoriesArray
 * @returns {object}
 */
export const createRawValuesObject = tokenCategoriesArray =>
  tokenCategoriesArray
    .flatMap(category =>
      tokensTable[category].filter(
        token =>
          token.name === `--${category}` ||
          token.name?.startsWith(`--${category}-`)
      )
    )
    .reduce((acc, token) => {
      // Tokens with per-theme or per-platform values have no single raw value.
      if (typeof token.value !== "string") {
        return acc;
      }
      const val = token.value.trim();
      if (!val.startsWith("var(")) {
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

/**
 * Every semantic token name in the tokens table.
 */
const TOKEN_NAMES = new Set(
  Object.values(tokensTable)
    .flat()
    .map(token => token.name)
);

/**
 * Tokens that style a background and a text color for the same surface share a
 * name apart from the `background-color` / `text-color` part, e.g.
 * `--button-background-color-primary-hover` and
 * `--button-text-color-primary-hover`. A token only counts as paired when its
 * counterpart exists: plenty of background tokens have no text token (and vice
 * versa) because they are meant to combine with whatever the surface inherits.
 */
const buildColorTokenPairs = () => {
  let backgroundToText = new Map();
  let textToBackground = new Map();

  for (let tokenName of TOKEN_NAMES) {
    let match = tokenName.match(
      /^--(?<prefix>.*?)background-color(?<suffix>.*)$/
    );
    if (!match) {
      continue;
    }
    let counterpart = `--${match.groups.prefix}text-color${match.groups.suffix}`;
    if (TOKEN_NAMES.has(counterpart)) {
      backgroundToText.set(tokenName, counterpart);
      textToBackground.set(counterpart, tokenName);
    }
  }

  return { backgroundToText, textToBackground };
};

export const { backgroundToText, textToBackground } = buildColorTokenPairs();

/**
 * Whether a custom property name is a semantic design token.
 *
 * @param {string} tokenName
 * @returns {boolean}
 */
export const isDesignToken = tokenName => TOKEN_NAMES.has(tokenName);

/**
 * Whether a declaration defines a custom property (or a Sass variable) rather
 * than using one.
 *
 * @param {object} decl - A PostCSS Declaration.
 * @returns {boolean}
 */
export const isCustomPropertyDefinition = decl =>
  decl.prop.startsWith("--") || decl.prop.startsWith("$");

const BACKGROUND_PROPERTIES = new Set(["background", "background-color"]);

/**
 * Finds the background color and text color declarations of one declaration
 * block that win the cascade within it. Defining a custom property is not
 * using a color, so those declarations are skipped.
 *
 * @param {object} block - A PostCSS Rule or AtRule.
 * @returns {{background: ?object, text: ?object}}
 */
export const findColorDeclarations = block => {
  let background = null;
  let backgroundImage = null;
  let text = null;

  for (let node of block.nodes) {
    if (node.type != "decl" || isCustomPropertyDefinition(node)) {
      continue;
    }
    let property = node.prop.toLowerCase();
    if (BACKGROUND_PROPERTIES.has(property)) {
      background = node;
    } else if (property == "background-image") {
      backgroundImage = node;
    } else if (property == "color") {
      text = node;
    }
  }

  return { background: background ?? backgroundImage, text };
};

/**
 * Collects the names of the custom properties a declaration value reads,
 * including the ones a var() fallback reads.
 *
 * @param {string} value - A CSS declaration value.
 * @returns {string[]}
 */
export const customPropertiesRead = value => {
  let names = [];
  valueParser(value).walk(node => {
    if (isFunction(node) && node.value === "var") {
      let [first] = node.nodes;
      if (first?.value?.startsWith("--")) {
        names.push(first.value);
      }
    }
  });
  return names;
};

/**
 * Splits a background or text color token name into the component family it
 * belongs to and the variant within that family, e.g.
 * `--button-text-color-primary-hover` is the `button-` family's
 * `-primary-hover` variant. Returns null for a name that is neither.
 *
 * @param {string} tokenName
 * @returns {?{family: string, variant: string}}
 */
export const parseColorTokenName = tokenName => {
  let match = tokenName.match(
    /^--(?<family>.*?)(?:background|text)-color(?<variant>.*)$/
  );
  return match
    ? { family: match.groups.family, variant: match.groups.variant }
    : null;
};
