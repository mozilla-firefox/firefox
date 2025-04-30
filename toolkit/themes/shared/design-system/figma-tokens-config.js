/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env node */

const StyleDictionary = require("style-dictionary");

const COLLECTIONS = {
  colors: "Colors",
  primitives: "Primitives",
  theme: "Theme",
  hcmTheme: "HCM Theme",
};

const HCM_VALUES = [
  "ActiveText",
  "ButtonBorder",
  "ButtonFace",
  "ButtonText",
  "Canvas",
  "CanvasText",
  "Field",
  "FieldText",
  "GrayText",
  "Highlight",
  "HighlightText",
  "LinkText",
  "Mark",
  "MarkText",
  "SelectedItem",
  "SelectedItemText",
  "AccentColor",
  "AccentColorText",
  "VisitedText",
];

StyleDictionary.registerTransform({
  type: "name",
  name: "name/figma",
  transformer: token => token.path.join("/").replace("/@base", ""),
});

/**
 * Determines if a given value is an arbitrarily deeply nested object
 * that satisfies the following conditions:
 * - Each object in the nesting hierarchy has exactly one key-value pair.
 * - The last key in the nesting hierarchy is "default".
 * - The value associated with the "default" key is a primitive (not an object).
 * - The value associated with the "default" key is not "currentColor".
 *
 * @param {object} value - The value to check, expected to be an object.
 * @returns {boolean} Returns `true` if the object matches the criteria,
 */
function isNestedDefaultObject(value) {
  let current = value;
  while (typeof current === "object") {
    const keys = Object.keys(current);
    if (keys.length !== 1) {
      return false;
    }
    const key = keys[0];
    if (key === "default") {
      if (typeof current[key] === "object") {
        return false;
      }
      if (current[key] === "currentColor") {
        return false;
      }
      return true;
    }
    current = current[key];
  }
  return false;
}

StyleDictionary.registerTransform({
  type: "attribute",
  name: "attribute/figma",
  transformer: token => {
    // Collection attribute
    let collection = COLLECTIONS.theme;

    if (
      (typeof token.value !== "object" || isNestedDefaultObject(token.value)) &&
      token.value !== "currentColor"
    ) {
      if (token.path[0] === "color") {
        collection = COLLECTIONS.colors;
      } else {
        collection = COLLECTIONS.primitives;
      }
    }

    // willBeDestructured attribute
    let willBeDestructured = false;
    const originalVal = token.original.value;
    if (
      attemptShadowDestructuring(token, originalVal) ||
      attemptPaddingMarginDestructuring(token, originalVal)
    ) {
      willBeDestructured = true;
    }

    return { collection, willBeDestructured };
  },
});

// ---------
// Style Dictionary Custom Format
// ---------

/**
 * Formats design tokens based on the provided arguments and options.
 *
 * @param {string} collection - The name of the token collection to filter by.
 * @returns {Function} A function that takes an object with `dictionary` and `options` properties
 * and returns a formatted JSON string of the tokens.
 */
const formatTokens = collection => args => {
  let dictionary = Object.assign({}, args.dictionary);
  let tokens = [];

  const filter = mergeFilters(
    defaultFilter,
    token => token.attributes?.collection === collection
  );

  dictionary.allTokens.forEach(token => {
    let originalVal = token.original.value;

    if (originalVal === undefined) {
      throw new Error(
        `[formatTokens] Token ${token.name} has an undefined original value. Please check your tokens.`
      );
    }

    // If the current token references another token that will be destructured,
    // we skip it altogether
    if (dictionary.usesReference(originalVal)) {
      const references = dictionary.getReferences(originalVal);
      if (references.some(ref => ref.attributes.willBeDestructured)) {
        console.warn(
          `[formatTokens] Skipping token ${token.name} because it references a token that will be destructured`
        );
        return;
      }
    }

    // If the token is a CSS box-shadow shorthand, attempt to destructure it
    // into its subtokens and process each subtoken
    const potentialShadowTokens = attemptShadowDestructuring(
      token,
      originalVal
    );
    if (potentialShadowTokens) {
      potentialShadowTokens.forEach(
        ({ token: sToken, originalVal: sOriginalVal }) => {
          // Check if the subtoken should be filtered out
          if (!filter(sToken)) {
            return;
          }
          // Transform the subtoken value and add it to the tokens array
          let formattedToken = transformTokenValue(
            sToken,
            sOriginalVal,
            dictionary
          );
          tokens.push(formattedToken);
        }
      );
      return;
    }

    // If the token is a padding/margin shorthand, attempt to destructure it
    const potentialPaddingMarginTokens = attemptPaddingMarginDestructuring(
      token,
      originalVal
    );
    if (potentialPaddingMarginTokens) {
      potentialPaddingMarginTokens.forEach(
        ({ token: sToken, originalVal: sOriginalVal }) => {
          // Check if the subtoken should be filtered out
          if (!filter(sToken)) {
            return;
          }
          // Transform the subtoken value and add it to the tokens array
          let formattedToken = transformTokenValue(
            sToken,
            sOriginalVal,
            dictionary
          );
          tokens.push(formattedToken);
        }
      );
      return;
    }

    // Check if the token should be filtered out
    if (!filter(token)) {
      return;
    }
    // Otherwise transform the original token value and add it to the tokens array
    let formattedToken = transformTokenValue(token, originalVal, dictionary);
    tokens.push(formattedToken);
  });

  if (!tokens.length) {
    return "";
  }

  dictionary.allTokens = dictionary.allProperties = tokens;
  return (
    "{\n" +
    dictionary.allTokens
      .map(function (token) {
        return `  "${token.name}": ${JSON.stringify(
          args.options.usesDtcg ? token.$value : token.value,
          null,
          2
        ).replace(/\n/g, "\n  ")}`;
      })
      .join(",\n") +
    "\n}" +
    "\n"
  );
};

/**
 * Transforms the value of a design token by resolving references, handling `calc()` expressions,
 * and applying specific transformations for light, dark, and forced color modes.
 *
 * @param {object} token - The token object containing metadata and the value to transform.
 * @param {string|object} originalVal - The original value of the token, which can be a string or an object.
 * @param {object} dictionary - Dictionary object from Style Dictionary
 * @returns {object} - A new token object with the transformed value.
 */
function transformTokenValue(token, originalVal, dictionary) {
  let newValue = originalVal;

  if (typeof token.value === "object") {
    const brandValue = getNestedBrandColor(originalVal);
    const forcedColorsValue = getNestedSystemColor(originalVal);

    // If this token got assigned to the primitive collection, we know it
    // only contains a single value, so we can just the light value
    if (token.attributes?.collection === COLLECTIONS.primitives) {
      newValue = brandValue?.light;
    } else {
      newValue = {
        light: brandValue?.light
          ? replaceReferences(dictionary, brandValue?.light)
          : "transparent",
        dark: brandValue?.dark
          ? replaceReferences(dictionary, brandValue?.dark)
          : "transparent",
        forcedColors: replaceReferences(dictionary, forcedColorsValue),
      };

      if (
        newValue.forcedColors === undefined &&
        newValue.light === newValue.dark
      ) {
        newValue.forcedColors = newValue.light;
      } else if (newValue.forcedColors === undefined) {
        newValue.forcedColors = "transparent";
      }
    }
  } else if (dictionary.usesReference(newValue)) {
    newValue = replaceReferences(dictionary, newValue);
  }

  if (typeof newValue === "object") {
    Object.keys(newValue).forEach(key => {
      newValue[key] = potentiallyTransformValue(token, newValue[key]);
    });
  } else {
    newValue = potentiallyTransformValue(token, newValue);
  }

  return { ...token, value: newValue };
}

function potentiallyTransformValue(token, value) {
  // We convert number strings without units to numbers and since Figma's
  // spaces everything in pixels, we convert pixel values to numbers too
  const numberOrPxRegex = /^-?\d*\.?\d+(px)?$/;
  if (typeof value === "string" && numberOrPxRegex.test(value)) {
    const numberValue = parseFloat(value);
    if (isNaN(numberValue)) {
      throw new Error(`Failed to parse value: ${value}`);
    }
    return numberValue;
  }
  // Figma expects opacity values to be in the range of 0-100
  if (
    typeof value === "number" &&
    value >= 0 &&
    value <= 1 &&
    token.path.includes("opacity")
  ) {
    return Math.round(value * 100);
  }
  return value;
}

/**
 * Replaces references and resolves `calc()` expressions in a given value.
 *
 * This function processes a value to replace references with their actual values
 * and resolves any `calc()` expressions by evaluating them. It also handles
 * specific cases for high contrast mode (HCM) values.
 *
 * @param {object} dictionary - The Style Dictionary instance containing the token references.
 * @param {string | undefined} value - The value to process.
 */
function replaceReferences(dictionary, value) {
  if (value === undefined) {
    return value;
  }

  // Replace calc() expressions with their computed values
  if (typeof value === "string") {
    value = value.replace(/calc\(([^()]+)\)/g, (_, calcContent) => {
      // Replace references inside the calc() content with their actual values
      dictionary.getReferences(calcContent).forEach(ref => {
        calcContent = calcContent.replace(
          `{${ref.path.join(".")}}`, // Reference path in the format {path.to.reference}
          ref.value // Replace with the actual value of the reference
        );
      });
      // Resolve the calc() expression to its computed value
      const calcValue = resolveCssCalc(calcContent);
      return calcValue;
    });
  }

  // Check if the value contains any references
  if (dictionary.usesReference(value)) {
    // Replace the style dictionary references with
    // the format expected by the figma import script
    dictionary.getReferences(value).forEach(ref => {
      value = value.replace(
        `{${ref.path.join(".")}}`,
        `{${ref.attributes.collection}$${ref.name}}`
      );
    });
  }

  // If the value matches any of the predefined HCM values, replace whith
  // reference expected by the figma import script
  if (HCM_VALUES.includes(value)) {
    value = `{${COLLECTIONS.hcmTheme}$${value}}`;
  }

  return value;
}

/**
 * Retrieves the nested system color value from a token object.
 * The function traverses through the token's properties (`forcedColors`, `prefersContrast`, `default`)
 * until it resolves to a non-object value.
 *
 * @param {object} token - The token object containing nested system color definitions.
 * @returns {string|number|boolean|null|undefined} - The resolved system color value.
 */
function getNestedSystemColor(token) {
  let current = token;
  while (typeof current === "object") {
    current =
      current.forcedColors ?? current.prefersContrast ?? current.default;
  }
  return current;
}

/**
 * Retrieves the nested brand color from a token object. The function traverses
 * the token structure to find and return an object containing `light` and `dark`
 * color values. If the token is a primitive value, it returns the same value
 * for both `light` and `dark`. If no valid color object is found, it returns `undefined`.
 *
 * @param {object|string|number} token - The token object or value to extract the brand color from.
 * @returns {{light: string|number, dark: string|number}|undefined} An object containing `light` and `dark`
 * color values, or `undefined` if no valid color is found.
 */
function getNestedBrandColor(token) {
  const stack = [token];
  while (stack.length) {
    const node = stack.pop();
    if (typeof node !== "object") {
      return { light: node, dark: node };
    }
    if (typeof node === "object") {
      if (node.light && node.dark) {
        return node;
      }
      if (node.brand) {
        stack.push(node.brand);
      }
      if (node.default) {
        stack.push(node.default);
      }
    }
  }
  return undefined;
}

/**
 * Attempts to destructure a shadow token into its individual components.
 * This function processes a token with a potential shadow value,
 * and attempts to parse it into subtokens with destructured shadow properties.
 *
 * @param {object} token - The token object to process.
 * @param {string} originalVal - The original value of the token, expected to be a string.
 * @returns {Array<object>|undefined} An array of new with destructured shadow properties,
 * or `undefined` if the token is not a shadow or cannot be parsed.
 */
function attemptShadowDestructuring(token, originalVal) {
  if (!token.path.includes("shadow") || typeof originalVal !== "string") {
    return undefined;
  }

  // check if originalVal contains at least a space
  if (!originalVal.includes(" ")) {
    return undefined;
  }

  let shadows;

  try {
    shadows = parseBoxShadow(originalVal);
  } catch (e) {
    console.warn("[attemptShadowDestructuring] Error parsing shadow:", e);
    return undefined;
  }

  if (shadows.length === 0) {
    return undefined;
  }

  const subtokens = shadows.flatMap((shadow, index) => {
    const path =
      shadows.length > 1 ? [...token.path, `shadow-${index + 1}`] : token.path;
    return Object.entries(shadow).map(([key, value]) => {
      // Every part of the shadow but the color can be put
      // in the primitive collection
      let collection = token.attributes?.collection;
      if (key !== "color") {
        collection = COLLECTIONS.primitives;
      }

      const copy = {
        ...token,
        path: [...path, key].filter(filterBase),
        name: [...path, key].filter(filterBase).join("/"),
        value,
        original: { ...token.original, value },
        attributes: {
          ...token.attributes,
          collection,
        },
      };
      return { token: copy, originalVal: value };
    });
  });

  return subtokens;
}

function attemptPaddingMarginDestructuring(token, originalVal) {
  if (
    typeof originalVal !== "string" ||
    (!token.path.includes("padding") && !token.path.includes("margin"))
  ) {
    return undefined;
  }
  const parts = originalVal.split(/(?<!\([^\s]*)\s+/);

  // return undefined if only 1 part
  if (parts.length === 1) {
    return undefined;
  }
  // throw if more than 4
  if (parts.length > 4) {
    throw new Error(
      `[attemptPaddingMarginDestructuring] Too many parts in ${originalVal}`
    );
  }

  // if 2 parts make object with block and inline keys
  // if 3 parts with block-start, inline and block-end keys
  // if 4 parts with block-start, inline-start, block-end and inline-end keys
  const result = {};
  if (parts.length === 2) {
    result.block = parts[0];
    result.inline = parts[1];
  } else if (parts.length === 3) {
    result.blockStart = parts[0];
    result.inline = parts[1];
    result.blockEnd = parts[2];
  } else if (parts.length === 4) {
    result.blockStart = parts[0];
    result.inlineStart = parts[1];
    result.blockEnd = parts[2];
    result.inlineEnd = parts[3];
  }

  return Object.entries(result).map(([key, value]) => {
    const copy = {
      ...token,
      path: [...token.path, key].filter(filterBase),
      name: [...token.path, key].filter(filterBase).join("/"),
      value,
      original: { ...token.original, value },
    };
    return { token: copy, originalVal: value };
  });
}

// ---------
// CSS Parsing Functions
// ---------

/**
 * Attempts to evaluate a CSS calc expression
 *
 * @param {string} expression - The CSS `calc()` expression to evaluate.
 *                              It can include numbers, units (e.g., px, %, rem), and operators (+, -, *, /).
 *
 * @returns {string} - The evaluated result of the expression as a string, including the unit (if any).
 *
 * @throws {Error} - Throws an error if the expression is invalid or if multiple units are mixed.
 *
 * @example
 * resolveCssCalc("2 * 5rem"); // returns "10rem"
 * resolveCssCalc("8 / 2"); // returns "4"
 * resolveCssCalc("2px + 5px"); // returns "7px"
 * resolveCssCalc("1% + 5px"); // throws Error: Mixing units is not allowed
 */
function resolveCssCalc(expression) {
  const unitRegex = /[a-zA-Z%]+/g;
  const precedence = { "+": 1, "-": 1, "*": 2, "/": 2 };

  // Tokenize the expression into numbers, units, and operators
  const tokens = expression.match(/\d*\.?\d+[a-zA-Z%]*|[-+*/()]/g);
  if (!tokens) {
    throw new Error("[resolveCssCalc] Invalid expression");
  }

  // Collect all unique units found in the expression
  const units = new Set();
  const parsedTokens = tokens.map(token => {
    const unit = token.match(unitRegex)?.[0] || ""; // Extract the unit from the token
    if (unit) {
      units.add(unit); // Add the unit to the set
    }
    return parseFloat(token) || token; // Parse the numeric value or keep the operator
  });

  // Ensure that only one type of unit is used in the expression
  // If two units eliminate each other (e.g. 5px - 5px + 1rem) then that also fails, but why should we make such calculations.
  if (units.size > 1) {
    throw new Error("[resolveCssCalc] Mixing units is not allowed");
  }
  const resultUnit = units.size ? [...units][0] : "";

  const output = [];
  const operators = [];

  // Function to compute the result of the top two numbers in the output stack
  const compute = () => {
    const b = output.pop();
    const a = output.pop();
    switch (operators.pop()) {
      case "+":
        output.push(a + b);
        break;
      case "-":
        output.push(a - b);
        break;
      case "*":
        output.push(a * b);
        break;
      case "/":
        output.push(a / b);
        break;
    }
  };

  // Process each token in the expression
  for (let i = 0; i < parsedTokens.length; i++) {
    let token = parsedTokens[i];
    // Push numbers to the output stack
    if (typeof token === "number") {
      output.push(token);
    }
    // Push opening parenthesis to the operators stack
    else if (token === "(") {
      operators.push(token);
    }
    // Compute closing parenthesis, until the matching opening parenthesis is found
    else if (token === ")") {
      while (operators.at(-1) !== "(") {
        compute();
      }
      // Remove the opening parenthesis from the stack
      operators.pop();
    }
    // If the token is an operator (+, -, *, /)
    else {
      // Handle signed numbers (e.g., "-5" or "+3")
      if (
        (token === "-" || token === "+") &&
        (i === 0 || parsedTokens[i - 1] === "(")
      ) {
        output.push(0); // Treat it as "0 - 5" or "0 + 3"
      }
      // While the precedence of the current operator is less than or equal to the operator on top of the stack,
      // compute the result of the top two numbers in the output stack
      while (
        operators.length &&
        precedence[token] <= precedence[operators.at(-1)]
      ) {
        compute();
      }
      // Push the current operator to the operators stack
      operators.push(token);
    }
  }
  // Compute any remaining operations in the stacks
  while (operators.length) {
    compute();
  }

  if (isNaN(output[0])) {
    throw new Error(
      "[resolveCssCalc] Resolving math expression resulted in NaN"
    );
  }
  return output[0] + resultUnit;
}

const DEFAULT_SHADOW = {
  x: "0",
  y: "0",
  blur: "0",
  spread: "0",
  color: "transparent",
};

/**
 * Parses a CSS box-shadow string and returns an array of shadow objects.
 *
 * @param {string} input - The box-shadow string to parse. Must be a valid CSS box-shadow value.
 * @returns {Array<object>} An array of objects representing the parsed box-shadow values.
 * @throws {Error} Throws an error if the input is not a string or if the box-shadow syntax is invalid.
 */
function parseBoxShadow(input) {
  if (typeof input !== "string") {
    throw new Error("Input must be a string");
  }
  // Regex to split multiple box-shadow definitions, ignoring commas inside parentheses
  const shadowSplitRegex = /,(?![^(]*\))/;
  // Regex to match individual parts of a box-shadow definition, ignoring spaces inside parentheses
  const shadowPartsRegex = /(?:[^\s()]+|\([^)]*\))+/g;
  // Regex to match valid length values (e.g., px, em, rem, %)
  const lengthValueRegex = /^-?\d*\.?\d+(px|em|rem|%)?$/;

  return input.split(shadowSplitRegex).map(shadow => {
    shadow = shadow.trim();
    const parts = shadow.match(shadowPartsRegex);
    let x, y, blur, spread, color;
    let lengthValues = [];

    for (let i = 0; i < parts.length; i++) {
      const part = parts[i];

      if (lengthValueRegex.test(part)) {
        lengthValues.push(part);
      } else {
        color = parts.slice(i).join(" ");
        break;
      }
    }

    if (lengthValues.length < 2 || lengthValues.length > 4) {
      throw new Error("Invalid box-shadow syntax");
    }

    [x, y, blur, spread] = lengthValues;

    if (color) {
      const colorParts = color.split(" ");
      if (colorParts.includes("inset")) {
        colorParts.splice(colorParts.indexOf("inset"), 1);
      }
      if (colorParts.includes("outset")) {
        colorParts.splice(colorParts.indexOf("outset"), 1);
      }
      color = colorParts.length ? colorParts.join(" ") : undefined;
    }

    return {
      x: x || DEFAULT_SHADOW.x,
      y: y || DEFAULT_SHADOW.y,
      blur: blur || DEFAULT_SHADOW.blur,
      spread: spread || DEFAULT_SHADOW.spread,
      color: color || DEFAULT_SHADOW.color,
    };
  });
}

// ---------
// Filter Functions
// ---------

/**
 * Combines multiple filter functions into a single filter function.
 *
 * @param {...Function} filters - One or more filter functions to combine.
 * @returns {Function} A new filter function that applies all provided filters.
 */
function mergeFilters(...filters) {
  return token => {
    for (const filter of filters) {
      if (!filter(token)) {
        return false;
      }
    }
    return true;
  };
}

function defaultFilter(token) {
  // discard tokens starting with "font/"
  if (token.path.includes("font")) {
    return false;
  }
  return true;
}

function filterBase(pathItem) {
  return pathItem !== "@base";
}

// ---------
// Style Dictionary Configuration
// ---------

const platform = {
  options: {
    outputReferences: true,
    showFileHeader: false,
  },
  transforms: ["name/figma", "attribute/figma"],
  files: [
    {
      destination: "tokens-figma-colors.json",
      format: "json/figma/colors",
    },
    {
      destination: "tokens-figma-primitives.json",
      format: "json/figma/primitives",
    },
    {
      destination: "tokens-figma-theme.json",
      format: "json/figma/theme",
    },
  ],
};

module.exports = {
  platform,
  formats: {
    "json/figma/colors": formatTokens(COLLECTIONS.colors),
    "json/figma/primitives": formatTokens(COLLECTIONS.primitives),
    "json/figma/theme": formatTokens(COLLECTIONS.theme),
  },
};
