/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import valueParser from "postcss-value-parser";
import { tokensTable } from "../../../../toolkit/themes/shared/design-system/dist/semantic-categories.mjs";
import { isSystemColor } from "./helpers.mjs";

/**
 * Validates whether a given CSS property value complies with allowed design token rules.
 *
 * @class
 * @property {PropertyConfig} config Configuration for the given property.
 */
export class PropertyValidator {
  static GLOBAL_WORDS = new Set([
    "inherit",
    "initial",
    "revert",
    "revert-layer",
    "unset",
  ]);

  /** @type {PropertyConfig} */
  config;
  /** @type {Set<string>} */
  allowedWords;
  /** @type {Set<string>} */
  allowedAliasWords;
  /** @type {Set<string>} */
  validTokenNames;
  /** @type {Set<string>} */
  allowedFunctions;
  /** @type {boolean} */
  allowUnits;
  /** @type {Record<string, string>} */
  customFixes;
  /** @type {Record<string, string>} */
  customSuggestions;
  /** @type {boolean} */
  warnSystemColors;

  constructor(config) {
    this.config = config;
    this.allowedWords = new Set(
      this.config.validTypes
        .flatMap(propType => propType.allow ?? [])
        .concat(...PropertyValidator.GLOBAL_WORDS)
    );
    this.allowedAliasWords = new Set(
      this.config.validTypes
        .flatMap(propType => propType.allowAlias ?? [])
        .concat(...this.allowedWords)
    );
    this.validTokenNames = new Set(
      this.config.validTypes.flatMap(propType => [
        ...(propType.allowedTokens || []),
        ...(propType.tokenTypes || []).flatMap(tokenType =>
          tokensTable[tokenType].map(token => token.name)
        ),
      ])
    );
    this.validAliasTokenNames = new Set(
      this.config.validTypes.flatMap(propType =>
        (propType.aliasTokenTypes || []).flatMap(tokenType =>
          tokensTable[tokenType].map(token => token.name)
        )
      )
    );
    this.allowedFunctions = new Set(
      this.config.validTypes.flatMap(propType => propType.allowFunctions || [])
    );
    this.allowedAliasFunctions = new Set(
      this.config.validTypes.flatMap(
        propType => propType.allowAliasFunctions || []
      )
    );
    this.allowUnits = this.config.validTypes.some(
      propType => propType.allowUnits
    );
    this.allowedUnits = new Set(
      this.config.validTypes.flatMap(propType => propType.allowedUnits || [])
    );
    this.customFixes = this.config.validTypes
      .map(type => type.customFixes)
      .filter(Boolean)
      // Reverse the list so the first specified fix is the one we use.
      .reverse()
      .reduce((acc, fixes) => ({ ...acc, ...fixes }), {});
    this.customSuggestions = this.config.validTypes
      .map(type => type.customSuggestions)
      .filter(Boolean)
      .reduce((acc, fixes) => ({ ...acc, ...fixes }), {});
    this.warnSystemColors = this.config.validTypes.some(
      propType => propType.warnSystemColors
    );
  }

  getFixedValue(value, lookupMap = {}) {
    const parsedValue = valueParser(value);
    let hasFixes = false;
    parsedValue.walk(node => {
      if (node.type == "word") {
        const token = lookupMap[node.value.trim().toLowerCase()];
        if (token) {
          hasFixes = true;
          node.value = token;
        }
      }
    });
    return hasFixes ? parsedValue.toString() : null;
  }

  getFunctionArguments(node) {
    const argGroups = [];
    let currentArg = [];
    for (const part of node.nodes) {
      if (part.type === "div") {
        argGroups.push(currentArg);
        currentArg = [];
      } else {
        currentArg.push(part);
      }
    }
    argGroups.push(currentArg);
    return argGroups;
  }

  isAllowedDiv(value) {
    if (value === ",") {
      return Boolean(this.config.multiple);
    }
    if (value === "/") {
      return Boolean(this.config.slash);
    }
    return false;
  }

  isAllowedFunction(functionType, isAlias = false) {
    if (isAlias) {
      return (
        this.allowedFunctions.has(functionType) ||
        this.allowedAliasFunctions.has(functionType)
      );
    }

    return this.allowedFunctions.has(functionType);
  }

  isAllowedSpace() {
    return Boolean(this.config.shorthand);
  }

  isAllowedWord(word, isAlias = false) {
    const lowerWord = word.toLowerCase();
    const hasAllowedWord = Array.from(this.allowedWords).some(
      allowed => allowed.toLowerCase() === lowerWord
    );
    if (hasAllowedWord) {
      return true;
    }

    if (this.allowUnits && this.isUnit(word)) {
      if (this.allowedUnits.size) {
        const parsed = valueParser.unit(word);
        return this.allowedUnits.has(parsed.unit);
      }
      return true;
    }

    if (isAlias) {
      return Array.from(this.allowedAliasWords).some(
        allowed => allowed.toLowerCase() === lowerWord
      );
    }

    return false;
  }

  isUnit(word) {
    const parsed = valueParser.unit(word);
    return parsed !== false && parsed.number !== "" && parsed.unit !== "";
  }

  static isCalcOperand(node) {
    if (node.type === "space") {
      return true;
    }
    if (node.type === "word") {
      return (
        /^[\+\-\*\/]$/.test(node.value) || /^-?\d+(\.\d+)?$/.test(node.value)
      );
    }
    return false;
  }

  isValidCalcFunction(node, isAlias = false) {
    const calcNodes = node.nodes.filter(
      n => !PropertyValidator.isCalcOperand(n)
    );
    const hasDesignToken = calcNodes.some(n => {
      if (n.type === "function" && n.value === "var") {
        return this.isValidVarFunction(n, isAlias);
      }
      return false;
    });
    return hasDesignToken || calcNodes.every(n => this.isValidNode(n, isAlias));
  }

  isValidColorMixFunction(node, isAlias = false) {
    // ignore the first argument (color space)
    let [, ...colors] = this.getFunctionArguments(node);
    return colors.every(color =>
      color.every(
        part =>
          part.type == "space" ||
          (part.type == "word" && part.value.endsWith("%")) ||
          this.isValidNode(part, isAlias)
      )
    );
  }

  isValidOklchFunction(node, isAlias = false) {
    let [colors] = this.getFunctionArguments(node);

    // we expect relative color syntax if using oklch() to adjust colors from a token
    if (!colors.some(part => part.type === "word" && part.value === "from")) {
      return false;
    }

    return colors.every(
      part =>
        part.type == "space" ||
        part.type == "word" ||
        this.isValidNode(part, isAlias)
    );
  }

  isValidFunction(node, isAlias = false) {
    switch (node.value) {
      case "var":
        return this.isValidVarFunction(node, isAlias);
      case "calc":
        return this.isValidCalcFunction(node, isAlias);
      case "light-dark":
        return this.isValidLightDarkFunction(node, isAlias);
      case "color-mix":
        return this.isValidColorMixFunction(node, isAlias);
      case "oklch":
        return this.isValidOklchFunction(node, isAlias);
      default:
        return this.isAllowedFunction(node.value, isAlias);
    }
  }

  isValidLightDarkFunction(node, isAlias = false) {
    return node.nodes.every(
      n => n.type == "div" || this.isValidNode(n, isAlias)
    );
  }

  isValidNode(node, isAlias = false) {
    switch (node.type) {
      case "space":
        return this.isAllowedSpace();
      case "div":
        return this.isAllowedDiv(node.value);
      case "word":
        return this.isAllowedWord(node.value, isAlias);
      case "function":
        return this.isValidFunction(node, isAlias);
      default:
        return false;
    }
  }

  isValidPropertyValue(parsedValue, localVars) {
    this.localVars = localVars;
    return parsedValue.nodes.every(node => this.isValidNode(node));
  }

  isValidToken(tokenName) {
    return this.validTokenNames.has(tokenName);
  }

  isValidAliasToken(tokenName) {
    return this.validAliasTokenNames.has(tokenName);
  }

  getTokenCategories() {
    if (!this._categories) {
      const categories = new Set();
      this.config.validTypes.forEach(propType => {
        if (propType.tokenTypes) {
          propType.tokenTypes.forEach(category => categories.add(category));
        }
      });
      this._categories = Array.from(categories);
    }
    return this._categories;
  }

  isValidVarFunction(node, isAlias = false) {
    const [varNameNode, , fallback] = node.nodes;
    const varName = varNameNode.value;
    if (this.isValidToken(varName)) {
      return true;
    }

    if (isAlias && this.isValidAliasToken(varName)) {
      return true;
    }

    // allow system colors as var() fallback values
    if (
      fallback?.type === "word" &&
      this.warnSystemColors &&
      isSystemColor(fallback.value)
    ) {
      return true;
    }

    const localVar = this.localVars[varName];
    return (
      (localVar &&
        valueParser(localVar).nodes.every(n => this.isValidNode(n, true))) ||
      (fallback && this.isValidNode(fallback))
    );
  }

  // Find local var uses and replace them with the locally defined value
  getResolvedValue(value) {
    let resolvedValue = value;
    const pattern = /var\(([\-a-z\d]+)(,\s?var\([\-a-z\d]+\))?\)/gi;
    const matches = [...value.matchAll(pattern)];
    matches.forEach(([match, varName, fallbackVar]) => {
      const localVar = this.localVars[varName];
      if (localVar) {
        resolvedValue = resolvedValue.replace(match, localVar);
        return;
      }

      if (fallbackVar) {
        const fallbackVarValue = this.getResolvedValue(
          fallbackVar.replace(/,\s+/, "")
        );
        if (fallbackVarValue) {
          resolvedValue = resolvedValue.replace(match, fallbackVarValue);
        }
      }
    });

    return resolvedValue;
  }
}
