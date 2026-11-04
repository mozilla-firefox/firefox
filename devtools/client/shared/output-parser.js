/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  angleUtils,
} = require("resource://devtools/client/shared/css-angle.js");
const { colorUtils } = require("resource://devtools/shared/css/color.js");
const {
  InspectorCSSParserWrapper,
} = require("resource://devtools/shared/css/lexer.js");

const STYLE_INSPECTOR_PROPERTIES =
  "devtools/shared/locales/styleinspector.properties";

loader.lazyGetter(this, "STYLE_INSPECTOR_L10N", function () {
  const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
  return new LocalizationHelper(STYLE_INSPECTOR_PROPERTIES);
});

loader.lazyGetter(this, "VARIABLE_JUMP_DEFINITION_TITLE", function () {
  return STYLE_INSPECTOR_L10N.getStr("rule.variableJumpDefinition.title");
});

// All cubic-bezier CSS timing-function names.
const BEZIER_KEYWORDS = new Set([
  "linear",
  "ease-in-out",
  "ease-in",
  "ease-out",
  "ease",
]);
// Functions that accept a color argument.
const COLOR_TAKING_FUNCTIONS = new Set([
  "linear-gradient",
  "-moz-linear-gradient",
  "repeating-linear-gradient",
  "-moz-repeating-linear-gradient",
  "radial-gradient",
  "-moz-radial-gradient",
  "repeating-radial-gradient",
  "-moz-repeating-radial-gradient",
  "conic-gradient",
  "repeating-conic-gradient",
  "drop-shadow",
  "color-mix",
  "contrast-color",
  "light-dark",
  // color functions can take a relative color after `from`
  "color",
  "hsl",
  "hwb",
  "lab",
  "lch",
  "oklab",
  "oklch",
  "rgb",
  // alpha() takes a relative color after `from`
  "alpha",
  // image(<color>) is equivalent to linear-gradient(<color>)
  "image",
]);
// Functions that accept a shape argument.
const BASIC_SHAPE_FUNCTIONS = new Set([
  "polygon",
  "circle",
  "ellipse",
  "inset",
]);

const CSS_EXPLAINERS_SUPPORTED_FUNCTIONS = new Set(
  InspectorUtils.getComputationStepsSupportedCSSFunctions()
);

const BACKDROP_FILTER_ENABLED = Services.prefs.getBoolPref(
  "layout.css.backdrop-filter.enabled"
);
const HTML_NS = "http://www.w3.org/1999/xhtml";

// This regexp matches a URL token.  It puts the "url(", any
// leading whitespace, and any opening quote into |leader|; the
// URL text itself into |body|, and any trailing quote, trailing
// whitespace, and the ")" into |trailer|.
const URL_REGEX =
  /^(?<leader>url\([ \t\r\n\f]*(["']?))(?<body>.*?)(?<trailer>\2[ \t\r\n\f]*\))$/i;

// Very long text properties should be truncated using CSS to avoid creating
// extremely tall propertyvalue containers. 5000 characters is an arbitrary
// limit. Assuming an average ruleview can hold 50 characters per line, this
// should start truncating properties which would otherwise be 100 lines long.
const TRUNCATE_LENGTH_THRESHOLD = 5000;
const TRUNCATE_NODE_CLASSNAME = "propertyvalue-long-text";

// This symbol is used in stack entries for the `tokenType` property of the object we set
// as a key in `tokensByPart`, for token/part pairs that were already processed in child
// stack entries and which shouldn't be processed as individual entries.
const AGGREGATED_TOKEN_TYPE = Symbol("AGGREGATED_TOKEN_TYPE");

/**
 * This module is used to process CSS text declarations and output DOM fragments (to be
 * appended to panels in DevTools) for CSS values decorated with additional UI and
 * functionality.
 *
 * For example:
 * - attaching swatches for values instrumented with specialized tools: colors, timing
 * functions (cubic-bezier), filters, shapes, display values (flex/grid), etc.
 * - adding previews where possible (images, fonts, CSS transforms).
 * - converting between color types on Shift+click on their swatches.
 *
 * Usage:
 *   const OutputParser = require("devtools/client/shared/output-parser");
 *   const parser = new OutputParser(document, cssProperties);
 *   parser.parseCssProperty("color", "red"); // Returns document fragment.
 *
 */
class OutputParser {
  /**
   * @param {Document} document
   *        Used to create DOM nodes.
   * @param {CssProperties} cssProperties
   *        Instance of CssProperties, an object which provides an interface for
   *        working with the database of supported CSS properties and values.
   */
  constructor(document, cssProperties) {
    this.#doc = document;
    this.#cssProperties = cssProperties;
  }

  #angleSwatches = new WeakMap();
  #colorSwatches = new WeakMap();
  #cssProperties;
  #doc;
  #parsed = [];
  #stack = [];

  /**
   * Parse a CSS property value given a property name.
   *
   * @param  {string} name
   *         CSS Property Name
   * @param  {string} value
   *         CSS Property value
   * @param  {object} [options]
   *         Options object. For valid options and default values see
   *         #mergeOptions().
   * @return {DocumentFragment}
   *         A document fragment containing color swatches etc.
   */
  parseCssProperty(name, value, options = {}) {
    options = this.#mergeOptions(options);

    options.expectTimingFunction = this.#cssProperties.supportsType(
      name,
      "timing-function"
    );
    this.parsedPropertyName = name;
    options.expectDisplay = name === "display";
    options.expectFilter =
      name === "filter" ||
      (BACKDROP_FILTER_ENABLED && name === "backdrop-filter");
    options.expectShape =
      name === "clip-path" ||
      name === "shape-outside" ||
      name === "offset-path";
    options.expectFont = name === "font-family";
    options.isVariable = name.startsWith("--");
    options.supportsColor =
      this.#cssProperties.supportsType(name, "color") ||
      this.#cssProperties.supportsType(name, "gradient") ||
      // Parse colors for CSS variables declaration if the declaration value or the computed
      // value are valid colors.
      (options.isVariable &&
        (InspectorUtils.isValidCSSColor(value) ||
          InspectorUtils.isValidCSSColor(
            options.getVariableData?.(name).computedValue
          )));

    if (this.#cssPropertySupportsValue(name, value, options)) {
      return this.#parse(value, options);
    }
    this.#appendTextNode(value);

    return this.#toDOM();
  }

  /**
   * The workhorse for @see #parse. This parses some CSS text, stopping at EOF
   *
   * @param  {string} text
   *         The original input text.
   * @param  {object} options
   *         The options object in use; @see #mergeOptions.
   * @param  {CSSLexer} tokenStream
   *         The token stream from which to read
   * @return {DocumentFragment}
   *         A document fragment.
   */
  // eslint-disable-next-line complexity
  #doParse(text, options, tokenStream) {
    let fontFamilyNameIndex = null;
    let previousWasBang = false;

    const colorOK = () => {
      return (
        options.supportsColor ||
        ((options.expectFilter || options.isVariable) &&
          this.#stack.length !== 0 &&
          this.#stack.at(-1).isColorTakingFunction)
      );
    };

    const angleOK = function (angle) {
      return new angleUtils.CssAngle(angle).valid;
    };

    let spaceNeeded = false;

    let token;
    while ((token = tokenStream.nextToken())) {
      const tokenType = token.tokenType;
      if (tokenType === "Comment") {
        // This doesn't change spaceNeeded, because we didn't emit
        // anything to the output.
        continue;
      }

      const tokenText = text.substring(token.startOffset, token.endOffset);
      const lowerCaseTokenText = tokenText.toLowerCase();

      if (
        this.#stack.length &&
        // Don't add the token text to the current stack if we have a function or an
        // opening parenthesis, as we're going to create a new stack entry for those (with
        // the tokenText being the initial text value in it)
        tokenType !== "Function" &&
        tokenType !== "ParenthesisBlock"
      ) {
        const stackEntry = this.#stack.at(-1);
        stackEntry.text += tokenText;
        // We only want to add the token text to substituted text when there was one
        // deeper subtitution function (see #onCloseParenthesis)
        if (stackEntry.substitutedText !== null) {
          stackEntry.substitutedText += tokenText;
        }
      }

      switch (tokenType) {
        case "Function": {
          const functionName = token.value;
          const lowerCaseFunctionName = functionName.toLowerCase();
          const isColorTakingFunction = COLOR_TAKING_FUNCTIONS.has(
            lowerCaseFunctionName
          );

          this.#createStackEntry({
            lowerCaseFunctionName,
            functionName,
            isColorTakingFunction,
            text: tokenText,
          });

          if (
            options.cssExplainersEnabled &&
            CSS_EXPLAINERS_SUPPORTED_FUNCTIONS.has(lowerCaseFunctionName)
          ) {
            this.#appendNode(
              "span",
              { class: "css-explainers-function-name" },
              functionName,
              token
            );
            this.#appendTextNode("(", token);
          } else {
            this.#appendTextNode(tokenText, token);
          }

          break;
        }

        case "Ident":
          if (
            options.expectTimingFunction &&
            BEZIER_KEYWORDS.has(lowerCaseTokenText)
          ) {
            this.#append(
              this.#createCubicBezierContainer({
                children: [token.text],
                parseOptions: options,
              }) || token.text,
              token
            );
          } else if (this.#isDisplayFlex(text, token, options)) {
            this.#appendDisplayWithHighlighterToggle(
              token.text,
              options.flexClass
            );
          } else if (this.#isDisplayGrid(text, token, options)) {
            this.#appendDisplayWithHighlighterToggle(
              token.text,
              options.gridClass
            );
          } else if (colorOK() && InspectorUtils.isValidCSSColor(token.text)) {
            const colorFunctionEntry = this.#stack.findLast(
              entry => entry.isColorTakingFunction
            );
            this.#appendColor(
              token.text,
              {
                ...options,
                colorFunction: colorFunctionEntry?.functionName,
              },
              token
            );
          } else if (angleOK(token.text)) {
            this.#appendAngle(token.text, options, token);
          } else {
            const idx = this.#appendTextNode(tokenText, token);
            if (
              options.expectFont &&
              // We don't append the identifier if the previous token
              // was equal to '!', since in that case we expect the
              // identifier to be equal to 'important'.
              !previousWasBang &&
              fontFamilyNameIndex == null &&
              // And if we're in a stack, we only expect a font-family after a comma (e.g.
              // in the fallback params for `var()`/`attr()`
              (!this.#stack.length || this.#stack.at(-1).sawComma)
            ) {
              fontFamilyNameIndex = idx;
            }
          }
          break;

        case "IDHash":
        case "Hash": {
          const original = tokenText;
          if (colorOK() && InspectorUtils.isValidCSSColor(original)) {
            if (spaceNeeded) {
              // Insert a space to prevent token pasting when a #xxx
              // color is changed to something like rgb(...).
              this.#appendTextNode(" ", token);
            }
            const colorFunctionEntry = this.#stack.findLast(
              entry => entry.isColorTakingFunction
            );
            this.#appendColor(
              original,
              {
                ...options,
                colorFunction: colorFunctionEntry?.functionName,
              },
              token
            );
          } else {
            this.#appendTextNode(original, token);
          }
          break;
        }
        case "Dimension": {
          if (angleOK(tokenText)) {
            this.#appendAngle(tokenText, options, token);
          } else if (
            options.cssExplainersEnabled &&
            token.unit !== "px" &&
            // we only want to handle lengths (e.g. not angles, frequency, …)
            InspectorUtils.valueMatchesSyntax(
              this.#doc,
              this.parsedPropertyName,
              `1${token.unit}`,
              "<length>"
            )
          ) {
            this.#appendNode(
              "span",
              {
                "data-length-expression": tokenText,
              },
              tokenText,
              token
            );
          } else {
            this.#appendTextNode(tokenText, token);
          }
          break;
        }
        case "Percentage": {
          if (options.cssExplainersEnabled) {
            this.#appendNode(
              "span",
              {
                "data-length-expression": tokenText,
              },
              tokenText,
              token
            );
          } else {
            this.#appendTextNode(tokenText, token);
          }
          break;
        }
        case "UnquotedUrl":
        case "BadUrl":
          for (const part of this.#createURLElements(
            tokenText,
            token.value,
            options
          )) {
            this.#append(part, token);
          }
          break;

        case "QuotedString":
          {
            const idx = this.#appendTextNode(tokenText, token);
            if (options.expectFont && fontFamilyNameIndex == null) {
              fontFamilyNameIndex = idx;
            }
          }
          break;

        case "WhiteSpace":
          this.#appendTextNode(tokenText, token);
          break;

        case "ParenthesisBlock":
          this.#createStackEntry({ text: tokenText });
          this.#appendTextNode(tokenText, token);
          break;

        case "CloseParenthesis": {
          if (options.expectFont && fontFamilyNameIndex !== null) {
            this.#wrapFontFamilyName(fontFamilyNameIndex, options);
            // reset the variable so we can handle following names
            fontFamilyNameIndex = null;
          }
          this.#appendTextNode(")", token);
          this.#onCloseParenthesis(options);
          break;
        }

        case "Comma":
        case "Delim":
          if (
            (token.tokenType === "Comma" || token.text === "!") &&
            options.expectFont &&
            fontFamilyNameIndex !== null
          ) {
            this.#wrapFontFamilyName(fontFamilyNameIndex, options);
            // reset the variable so we can handle following names
            fontFamilyNameIndex = null;
          }

          if (tokenType === "Comma" && this.#stack.length) {
            this.#stack.at(-1).sawComma = true;
          }

          this.#appendTextNode(tokenText, token);
          break;

        // falls through
        default:
          this.#appendTextNode(tokenText, token);
          break;
      }

      // If this token might possibly introduce token pasting when
      // color-cycling, require a space.
      spaceNeeded =
        token.tokenType === "Ident" ||
        token.tokenType === "AtKeyword" ||
        token.tokenType === "IDHash" ||
        token.tokenType === "Hash" ||
        token.tokenType === "Number" ||
        token.tokenType === "Dimension" ||
        token.tokenType === "Percentage" ||
        token.tokenType === "Dimension";
      previousWasBang = token.tokenType === "Delim" && token.text === "!";
    }

    if (options.expectFont && fontFamilyNameIndex !== null) {
      this.#wrapFontFamilyName(fontFamilyNameIndex, options);
    }

    // We might never encounter a matching closing parenthesis for a function and still
    // have a "valid" value (e.g. `background: linear-gradient(90deg, red, blue"`)
    // In such case, go through the stack and handle each items until we have nothing left.
    if (this.#stack.length) {
      while (this.#stack.length !== 0) {
        this.#onCloseParenthesis(options);
      }
    }

    let result = this.#toDOM();

    if (options.expectFilter && !options.filterSwatch) {
      result = this.#wrapFilter(text, options, result);
    }

    return result;
  }

  /**
   * Add a stack entry in this.#stack
   *
   * @param {object} entryData: An object that will be spread into the stack entry.
   */
  #createStackEntry(entryData) {
    const stackEntry = {
      // The parsed parts of the function that will be rendered on screen.
      // This can hold Element or Text instances
      parts: [],
      // A <(Element|Text),object> Map, whose keys are element in `parts`,
      // and values are usually the token they represents (multiple part can represent
      // a single token).
      // When a set of tokens (e.g. a stack entry, a font family name, …) was already
      // handled (e.g. in #onCloseParenthesis or in #wrapFontFamilyName) the value will,
      // be an object with an AGGREGATED_TOKEN_TYPE tokenType and a `data` property that will
      // hold all or a subset of the properties that can be found in a stack entry
      tokensByPart: new WeakMap(),
      // Function name if token is a function, null otherwise.
      functionName: null,
      // Lowercase function name if token is a function, null otherwise.
      // Precomputed because this can be a hot path.
      lowerCaseFunctionName: null,
      // Will hold the names of the functions that are used inside the current one
      nestedFunctions: [],
      // Boolean indicating if the function accepts color parameters
      // if token is a function, null otherwise.
      isColorTakingFunction: null,
      // Will hold the text for the stack entry, i.e. the whole function call
      // (e.g. `min(10px, max(1em, var(--w, 20w)))`),
      text: "",
      // Will hold the substituted text for the stack entry, i.e. the whole function call
      // with subtitution functions (for now `var()`, but later `attr()` and `env()`)
      // being replaced by their returned value
      // (e.g. for `min(10px, max(1em, var(--w, 20w)))` with `--w:30%`, this will be
      // `min(10px, max(1em, 30%))`),
      // Initial value is null so it can properly be replaced by `text` when there
      // isn't any subtitution functions in the stack (we can't just use an empty string
      // as some function can subtitute to an empty string)
      substitutedText: null,
      // Used to know if a comma was found in the entry. Useful for functions like `var()`
      // or `attr()` to know if they have a fallback.
      sawComma: false,
      ...entryData,
    };
    this.#stack.push(stackEntry);
  }

  // eslint-disable-next-line complexity
  #onCloseParenthesis(options) {
    if (!this.#stack.length) {
      return;
    }

    const stackEntry = this.#stack.pop();
    let { lowerCaseFunctionName, parts, text } = stackEntry;
    if (lowerCaseFunctionName === "attr") {
      parts = this.#onCloseParenthesisForAttr(stackEntry, options);
    } else if (lowerCaseFunctionName === "cubic-bezier") {
      parts = this.#onCloseParenthesisForCubicBezier(stackEntry, options);
    } else if (lowerCaseFunctionName === "light-dark") {
      parts = this.#onCloseParenthesisForLightDark(stackEntry, options);
    } else if (lowerCaseFunctionName === "linear") {
      parts = this.#onCloseParenthesisForLinear(stackEntry, options);
    } else if (
      lowerCaseFunctionName === "sibling-count" &&
      Number.isInteger(options.siblingCount)
    ) {
      stackEntry.substitutedText = options.siblingCount;
    } else if (
      lowerCaseFunctionName === "sibling-index" &&
      Number.isInteger(options.siblingIndex)
    ) {
      stackEntry.substitutedText = options.siblingIndex;
    } else if (lowerCaseFunctionName === "url") {
      parts = this.#onCloseParenthesisForUrl(stackEntry, options);
    } else if (lowerCaseFunctionName === "var") {
      parts = this.#onCloseParenthesisForVar(stackEntry, options);
    } else if (BASIC_SHAPE_FUNCTIONS.has(lowerCaseFunctionName)) {
      parts = this.#onCloseParenthesisForBasicShape(stackEntry, options);
    } else if (
      (options.supportsColor ||
        ((options.expectFilter || options.isVariable) &&
          this.#stack.length !== 0 &&
          this.#stack.at(-1).isColorTakingFunction)) &&
      InspectorUtils.isValidCSSColor(
        // use the substituted text when we have one, as it allows us to still get the
        // color swatch when we have CSS variables parameters
        stackEntry.substitutedText ?? stackEntry.text
      )
    ) {
      const colorFunctionEntry = this.#stack.findLast(
        entry => entry.isColorTakingFunction
      );
      const colorObj =
        options.colorObj ||
        new colorUtils.CssColor(stackEntry.substitutedText ?? stackEntry.text);
      const colorContainerEl = this.#createColorContainerElement(
        colorObj,
        {
          ...options,
          colorFunction: colorFunctionEntry?.functionName,
        },
        stackEntry.parts
      );
      parts = [colorContainerEl];
    }

    if (
      options.cssExplainersEnabled &&
      CSS_EXPLAINERS_SUPPORTED_FUNCTIONS.has(lowerCaseFunctionName) &&
      stackEntry.nestedFunctions.every(
        fn =>
          CSS_EXPLAINERS_SUPPORTED_FUNCTIONS.has(fn) ||
          // type() isn't directly supported for explainers (i.e. we won't have it underlined)
          // but it shouldn't prevent functions using it to have explainers
          // (e.g. in `attr(x type(*))`, `attr` should be underlined).
          fn === "type"
      )
    ) {
      const functionNode = this.#createNode("span", {
        class: options.functionClass,
        "data-function-expression": stackEntry.text,
      });
      functionNode.append(...parts);
      parts = [functionNode];
    }

    // Put all the parts in the "new" last stack, or the main parsed array if there
    // is no more entry in the stack
    this.#getCurrentStackParts().push(...parts);

    if (this.#stack.length) {
      const lastStackEntry = this.#stack.at(-1);
      // This needs to be done before we update lastStackEntry.text
      if (
        // Only compute the substituted text if a stack entry has substituted text…
        stackEntry.substitutedText ||
        // …or if substituted text was already consumed in a "child" stack entry
        lastStackEntry.substitutedText
      ) {
        // if that's the first substituted function we encounter, we need to initialize
        // the value
        if (lastStackEntry.substitutedText === null) {
          lastStackEntry.substitutedText = lastStackEntry.text;
        }

        // substitutedText is only computed for some functions, so fall back to text when
        // it doesn't exist
        const textToAdd = stackEntry.substitutedText ?? text;
        lastStackEntry.substitutedText += textToAdd;
      }
      // Then update the authored text
      lastStackEntry.text += text;

      if (stackEntry.lowerCaseFunctionName) {
        // Set the nested functions by adding the one for the stack entry we just handled
        lastStackEntry.nestedFunctions = [
          stackEntry.lowerCaseFunctionName,
          ...stackEntry.nestedFunctions,
        ];
      } else {
        // If we closed a parenthesis block, just copy the nested functions we had
        lastStackEntry.nestedFunctions = Array.from(stackEntry.nestedFunctions);
      }

      const compoundEntryToken = {
        // Associate AGGREGATED_TOKEN_TYPE to the part so consumers can know the part was for
        // a previous stack entry and shouldn't be considered.
        tokenType: AGGREGATED_TOKEN_TYPE,
        data: stackEntry,
      };
      for (const part of parts) {
        lastStackEntry.tokensByPart.set(part, compoundEntryToken);
      }
    }
  }

  /**
   * Called when we got the closing bracket for `light-dark()`
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  #onCloseParenthesisForLightDark(stackEntry, options) {
    const stackEntryParts = stackEntry.parts;
    if (typeof options.isDarkColorScheme !== "boolean") {
      return stackEntryParts;
    }

    let separatorIndex = null;
    for (let i = 0; i < stackEntryParts.length; i++) {
      const token = stackEntry.tokensByPart.get(stackEntryParts[i]);
      if (token?.tokenType === "Comma") {
        if (separatorIndex === null) {
          separatorIndex = i;
        } else {
          // light-dark takes exactly two parameters, so if we don't get exactly 1 separator
          // at this point, that means that the value is valid at parse time, but is invalid
          // at computed value time.
          // TODO: We might want to add a class to indicate that this is invalid at computed
          // value time (See Bug 1910845)
          return stackEntryParts;
        }
      }
    }

    if (separatorIndex === null) {
      return stackEntryParts;
    }

    let startIndex;
    let endIndex;
    if (options.isDarkColorScheme) {
      // If we're using a dark color scheme, we want to mark the first param as
      // not used.

      // The first "part" is `light-dark(`, so we can start after that.
      // We want to filter out white space character before the first parameter
      for (let i = 1; i < separatorIndex; i++) {
        const token = stackEntry.tokensByPart.get(stackEntryParts[i]);
        if (token?.tokenType !== "WhiteSpace") {
          startIndex = i;
          break;
        }
      }

      // same for the end of the parameter, we want to filter out whitespaces
      // after the parameter and before the comma
      endIndex = separatorIndex - 1;
      for (let i = endIndex; i >= startIndex; i--) {
        const token = stackEntry.tokensByPart.get(stackEntryParts[i]);
        if (token?.tokenType !== "WhiteSpace") {
          // We found a non-whitespace part, we need to include it, so increment the endIndex
          endIndex = i + 1;
          break;
        }
      }
    } else {
      // If we're not using a dark color scheme, we want to mark the second param as
      // not used.

      // We want to filter out white space character after the comma and before the
      // second parameter
      for (let i = separatorIndex + 1; i < stackEntryParts.length; i++) {
        const token = stackEntry.tokensByPart.get(stackEntryParts[i]);
        if (token?.tokenType !== "WhiteSpace") {
          startIndex = i;
          break;
        }
      }

      // same for the end of the parameter, we want to filter out whitespaces
      // after the parameter and before the closing parenthesis (which is not yet
      // included in stackEntryParts)
      for (
        // we don't start at the last part, but the one before that, as the last part will
        // always be the closing parenthesis for the function, and it shouldn't be included
        // in the unmatched span.
        let i = stackEntryParts.length - 2;
        i > separatorIndex;
        i--
      ) {
        const token = stackEntry.tokensByPart.get(stackEntryParts[i]);
        if (token?.tokenType !== "WhiteSpace") {
          // We found a non-whitespace part, we need to include it, so increment the endIndex
          endIndex = i + 1;
          break;
        }
      }
    }

    const parts = stackEntryParts.slice(startIndex, endIndex);

    // If the item we need to mark is already an element (e.g. a parsed color),
    // just add a class to it.
    if (parts.length === 1 && Element.isInstance(parts[0])) {
      parts[0].classList.add(options.unmatchedClass);
    } else {
      // Otherwise, we need to wrap our parts into a specific element so we can
      // style them
      const node = this.#createNode("span", {
        class: options.unmatchedClass,
      });
      node.append(...parts);
      stackEntryParts.splice(startIndex, parts.length, node);
    }

    return stackEntryParts;
  }

  /**
   * Called when we got the closing bracket for `cubic-bezier()`
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  #onCloseParenthesisForCubicBezier(stackEntry, options) {
    if (!options.expectTimingFunction) {
      return stackEntry.parts;
    }

    const container = this.#createCubicBezierContainer({
      children: stackEntry.parts,
      parseOptions: options,
    });

    return container ? [container] : stackEntry.parts;
  }

  /**
   * Called when we got the closing bracket for `linear()`
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  #onCloseParenthesisForLinear(stackEntry, options) {
    if (!options.expectTimingFunction) {
      return stackEntry.parts;
    }

    const linear = stackEntry.text;

    if (linear.includes("var(")) {
      // For now, we don't support cubic-bezier with CSS variables (see Bug 2031696)
      return stackEntry.parts;
    }

    const container = this.#createNode("span", {
      "data-linear": linear,
    });

    if (options.linearEasingSwatchClass) {
      const swatch = this.#createNode("span", {
        class: options.linearEasingSwatchClass,
        tabindex: "0",
        role: "button",
        "data-linear": linear,
      });
      container.appendChild(swatch);
    }

    const valueEl = this.#createNode("span", {
      class: options.linearEasingClass,
    });
    valueEl.append(...stackEntry.parts);
    container.appendChild(valueEl);
    return [container];
  }

  /**
   * Called when we got the closing bracket for `attr()`
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  // eslint-disable-next-line complexity
  #onCloseParenthesisForAttr(stackEntry, options) {
    if (typeof options.getAttributeValue !== "function") {
      return stackEntry.parts;
    }

    let attrNameIndex = null;
    let attrTypeIndex = null;
    let commaIndex = null;
    for (let i = 0; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      if (!stackEntry.tokensByPart.has(part)) {
        continue;
      }
      const token = stackEntry.tokensByPart.get(part);

      // The attribute name is the first Ident
      if (token.tokenType === "Ident" && attrNameIndex === null) {
        attrNameIndex = i;
      } else if (
        // If we have another Ident or a closed stack entry before the comma, then that's
        // the attr type.
        attrNameIndex !== null &&
        attrTypeIndex === null &&
        // Here we're looking for <attr-type> which might be an Ident (raw-string,
        // number, px, …), a % (Delim) or the `type()` function (which will be represented
        // as an aggregated token at this point)
        (token.tokenType === "Ident" ||
          (token.tokenType === "Delim" && token.text === "%") ||
          token.tokenType === AGGREGATED_TOKEN_TYPE)
      ) {
        attrTypeIndex = i;
      } else if (token.tokenType === "Comma") {
        commaIndex = i;
        break;
      }
    }

    // This shouldn't happen, but let's be safe
    if (attrNameIndex === null) {
      return stackEntry.parts;
    }

    // Get the attribute name part, which should be the first Ident
    const attrNamePart = stackEntry.parts[attrNameIndex];
    const attrName = attrNamePart.textContent;
    // and its value
    const attrValue = options.getAttributeValue(attrName);

    // as well as the first attribute (might contain attribute name + typing information),
    // with specific style if the attribute isn't set
    const attrFirstParamNode = this.#createNode("span", {
      class: "inspector-attr-param",
    });

    // > When an <attr-type> is set, attr() will try to parse the attribute into that
    // > specified <attr-type> and return it.
    // > If the attribute cannot be parsed into the given <attr-type>, the <fallback-value>
    // > will be returned instead.
    // > When no <attr-type> is set, the attribute will be parsed into a CSS string.
    // > If no <fallback-value> is set, the return value will default to an empty string
    // > when no <attr-type> is set or the guaranteed-invalid value when an <attr-type> is set.
    let fallbackValueIsUsed = attrValue === null;
    let attrTypeMismatchText;
    if (attrTypeIndex !== null && attrValue !== null) {
      const part = stackEntry.parts[attrTypeIndex];
      const token = stackEntry.tokensByPart.get(part);
      // First, we want to handle <attr-type> other than `type()`, i.e. Idents (`raw-string`,
      // `number`, `px`, …) and `%`
      if (
        token.tokenType === "Ident" ||
        (token.tokenType === "Delim" && token.text === "%")
      ) {
        // For `number` and units, the spec says:
        // > If given as the number keyword, it causes the attribute’s literal value […]
        // to be parsed as a <number-token>.
        // > Values that fail to parse trigger fallback.
        // […]
        // > If given as an <attr-unit> value, the value is first parsed as if number
        // > keyword was specified, then the resulting numeric value is turned into a
        // > dimension with the corresponding unit, or a percentage if % was given.
        // > Same as for number <attr-type>, values that do not correspond to the
        // > <number-token> production trigger fallback.

        // So we need to check that the attribute value is actually a number. And that's
        // pretty much it: for <attr-unit>, if the given unit is not known, the declaration
        // is invalid and won't be parsed anyway

        if (
          token.text !== "raw-string" &&
          !InspectorUtils.valueMatchesSyntax(
            this.#doc,
            this.parsedPropertyName,
            attrValue,
            "<number>"
          )
        ) {
          fallbackValueIsUsed = true;
          attrTypeMismatchText = STYLE_INSPECTOR_L10N.getFormatStr(
            "rule.attributeNotNumber",
            `"${attrValue}"`
          );
        }
      } else if (
        token.tokenType === AGGREGATED_TOKEN_TYPE &&
        token.data.lowerCaseFunctionName === "type"
      ) {
        // Here we have a type() function. We need to extract its content to see if
        // the attribute value can be parsed with this type.
        // We can take a small shortcut here: we have the text of the type() function so…
        const syntax = token.data.text
          .slice(
            // …we can just remove the leading "type("
            5,
            // …as well as the  trailing ")"
            -1
          )
          .trim();
        if (
          // For now, let's skip attribute value using substitution functions.
          // Those guards should be removed as part of Bug 2070443.
          !attrValue.includes("var(") &&
          !attrValue.includes("attr(") &&
          !attrValue.includes("env(") &&
          !InspectorUtils.valueMatchesSyntax(
            this.#doc,
            this.parsedPropertyName,
            attrValue,
            syntax
          )
        ) {
          fallbackValueIsUsed = true;
          attrTypeMismatchText = STYLE_INSPECTOR_L10N.getFormatStr(
            "rule.attributeUnmatchedType",
            `"${attrValue}"`,
            `"${syntax}"`
          );
        }
      }
    }

    // First, we want to render the attribute name on its own element
    const attrNameNode = this.#createNode(
      "span",
      {
        class: "inspector-attr-name",
      },
      attrName
    );
    stackEntry.parts[attrNameIndex] = attrNameNode;

    if (fallbackValueIsUsed) {
      attrFirstParamNode.classList.add(options.unmatchedClass);
    }

    if (attrValue === null) {
      attrFirstParamNode.setAttribute(
        "data-attribute",
        STYLE_INSPECTOR_L10N.getFormatStr("rule.attributeUnset", attrName)
      );
    } else if (attrTypeMismatchText) {
      attrFirstParamNode.setAttribute("data-attribute", attrTypeMismatchText);
    } else {
      // Otherwise we set it on the attribute name only
      attrNameNode.setAttribute("data-attribute", `"${attrValue}"`);
    }

    // Let's put all the parts starting with the attribute name until the comma
    let attrFirstParamChildCount = 0;
    let attrFirstParamEndIndex;
    if (commaIndex === null) {
      // if we didn't found a comma, we want to get all the items until the closing
      // parenthesis, which is the last item in parts
      attrFirstParamEndIndex = stackEntry.parts.length - 1;
    } else if (
      // if the token before the comma is a whitespace, don't include it in the first param node
      stackEntry.tokensByPart.get(stackEntry.parts[commaIndex - 1])
        ?.tokenType === "WhiteSpace"
    ) {
      attrFirstParamEndIndex = commaIndex - 1;
    } else {
      attrFirstParamEndIndex = commaIndex;
    }

    for (let i = attrNameIndex; i < attrFirstParamEndIndex; i++) {
      attrFirstParamNode.append(stackEntry.parts[i]);
      attrFirstParamChildCount++;
    }

    stackEntry.parts.splice(
      attrNameIndex,
      attrFirstParamChildCount,
      attrFirstParamNode
    );

    // We don't have to do anything more when there's no fallback value, i.e. if we didn't
    // found a comma
    if (commaIndex === null) {
      return stackEntry.parts;
    }

    // we need to update the comma index, as we added attrFirstParamNode in parts and
    // removed all the elements we put in it.
    commaIndex = commaIndex + 1 - attrFirstParamChildCount;
    let fallbackStartIndex = null;
    // Then we want to find the part that correspond to the first non whitespace token,
    // which will be the start of the fallback param
    for (let i = commaIndex + 1; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      if (!stackEntry.tokensByPart.has(part)) {
        continue;
      }
      const token = stackEntry.tokensByPart.get(part);
      if (
        // we might get into a part that was already handled, for example a nested function,
        // and in such case, it should be part of the fallback element
        token.tokenType === AGGREGATED_TOKEN_TYPE ||
        token.tokenType !== "WhiteSpace"
      ) {
        fallbackStartIndex = i;
        break;
      }
    }

    // This shouldn't happen, but let's be safe an bail if we didn't find the fallback part
    if (fallbackStartIndex === null) {
      return stackEntry.parts;
    }

    // The last part is the closing bracket, so let's put the index before it.
    let fallbackEndTokenIndex = stackEntry.parts.length - 2;
    for (let i = fallbackEndTokenIndex; i >= fallbackStartIndex; i--) {
      const part = stackEntry.parts[i];
      if (!stackEntry.tokensByPart.has(part)) {
        continue;
      }
      const token = stackEntry.tokensByPart.get(part);
      if (
        // we might get into a part that was already handled, for example a nested function,
        // and in such case, it should be part of the fallback element
        token.tokenType === AGGREGATED_TOKEN_TYPE ||
        token.tokenType !== "WhiteSpace"
      ) {
        fallbackEndTokenIndex = i;
        break;
      }
    }

    // So, at this point, we have the fallback parts that we want to put in their own elements
    const partsToWrap = stackEntry.parts.splice(
      fallbackStartIndex,
      fallbackEndTokenIndex - fallbackStartIndex + 1
    );

    const fallbackEl = this.#createNode("span", {
      class: `inspector-attr-fallback${fallbackValueIsUsed ? "" : " " + options.unmatchedClass}`,
    });
    fallbackEl.append(...partsToWrap);
    stackEntry.parts.splice(fallbackStartIndex, 0, fallbackEl);
    return stackEntry.parts;
  }

  /**
   * Called when we got the closing bracket for any function in BASIC_SHAPE_FUNCTIONS.
   * It will append a CSS shapes highlighter toggle next to the value, and parse the value
   * into spans, each containing a point that can be hovered over.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  #onCloseParenthesisForBasicShape(stackEntry, options) {
    if (!options.expectShape) {
      return stackEntry.parts;
    }

    const container = this.#createNode("span", {});
    const valContainer = this.#createNode("span", {
      class: options.shapeClass,
    });

    // Let's retrieve the index in `parts` where the coordinates start
    let coordStartIdx = null;
    let previousToken;
    for (let i = 0; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);
      // Multiple consecutive parts can reference the same token, so let's find the first
      // part that refers to a token that is not the initial function.
      if (
        token.tokenType === "Function" &&
        (!previousToken || token === previousToken)
      ) {
        coordStartIdx = i + 1;
        previousToken = token;
        valContainer.append(part);
      } else if (coordStartIdx !== null) {
        // we already found the coordinate, and the token does not represent the initial
        // function, so we can stop looping
        break;
      }
    }

    // That shouldn't happen, but let's be safe
    if (coordStartIdx === null) {
      return stackEntry.parts;
    }

    if (stackEntry.lowerCaseFunctionName === "polygon") {
      valContainer.append(
        ...this.#onCloseParenthesisForPolygonShape(stackEntry, coordStartIdx)
      );
    } else if (stackEntry.lowerCaseFunctionName === "circle") {
      valContainer.append(
        ...this.#onCloseParenthesisForCircleShape(stackEntry, coordStartIdx)
      );
    } else if (stackEntry.lowerCaseFunctionName === "ellipse") {
      valContainer.append(
        ...this.#onCloseParenthesisForEllipseShape(stackEntry, coordStartIdx)
      );
    } else if (stackEntry.lowerCaseFunctionName === "inset") {
      valContainer.append(
        ...this.#onCloseParenthesisForInsetShape(stackEntry, coordStartIdx)
      );
    }

    if (options.shapeSwatchClass) {
      const toggleButton = this.#createNode("button", {
        class: options.shapeSwatchClass,
      });
      container.appendChild(toggleButton);
    }

    container.appendChild(valContainer);
    return [container];
  }

  /**
   * Called when we got the closing bracket for the `polygon()` function.
   * It will append a CSS shapes highlighter toggle next to the value, and parse the value
   * into spans, each containing a point that can be hovered over.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {number} coordsStartIdx
   *        The index in stackEntry.parts at which the coordinates for the polygon start
   * @returns {Array<Element|Text>} The parts that were handled
   */
  // eslint-disable-next-line complexity
  #onCloseParenthesisForPolygonShape(stackEntry, coordsStartIdx) {
    const points = [];
    let previousToken;
    for (let i = coordsStartIdx; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);

      if (
        token.tokenType !== "Number" &&
        token.tokenType !== "Dimension" &&
        token.tokenType !== "Percentage" &&
        // a collapsed function call (e.g. `var(…)`) counts as a single argument
        token.tokenType !== AGGREGATED_TOKEN_TYPE
      ) {
        continue;
      }

      const lastPoint = points.at(-1);
      if (previousToken !== token) {
        if (!lastPoint || lastPoint.y) {
          points.push({
            x: [i],
          });
        } else {
          lastPoint.y = [i];
        }
      } else if (lastPoint.y) {
        lastPoint.y.push(i);
      } else {
        lastPoint.x.push(i);
      }

      previousToken = token;
    }

    // Let's iterate through points in reverse as we're going to mutate stackEntry.parts
    // and the indexes in `points` refer to the original indexes
    for (let i = points.length - 1; i >= 0; i--) {
      const point = points[i];
      const xNode = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": i,
        "data-pair": "x",
      });
      for (const idx of point.x) {
        xNode.append(stackEntry.parts[idx]);
      }
      const yNode = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": i,
        "data-pair": "y",
      });
      for (const idx of point.y) {
        yNode.append(stackEntry.parts[idx]);
      }
      const coordNode = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": i,
      });
      coordNode.append(xNode);
      // Put the parts between the x and y points
      for (let j = point.x.at(-1) + 1; j < point.y[0]; j++) {
        coordNode.append(stackEntry.parts[j]);
      }
      coordNode.append(yNode);
      stackEntry.parts.splice(
        point.x[0],
        point.y.at(-1) - point.x[0] + 1,
        coordNode
      );
    }

    return stackEntry.parts;
  }

  /**
   * Called when we got the closing bracket for the `circle()` function.
   * It will append a CSS shapes highlighter toggle next to the value, and parse the value
   * into spans, each containing a point that can be hovered over.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {number} coordsStartIdx
   *        The index in stackEntry.parts at which the coordinates for the circle start
   * @returns {Array<Element|Text>} The parts that were handled
   */
  // eslint-disable-next-line complexity
  #onCloseParenthesisForCircleShape(stackEntry, coordsStartIdx) {
    const radiusPartsIndexes = [];
    const positionsPartsIndexes = [];
    let seenAtKeyword = false;
    let previousToken;
    for (let i = coordsStartIdx; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);

      if (token.tokenType === "Ident" && token.text === "at") {
        seenAtKeyword = true;
        continue;
      }

      // circle() can take a radius which is before `at`, which can be a length, percentage,
      // or a keyword (closest-corner, closest-side, farthest-corner, farthest-side)
      if (
        !seenAtKeyword &&
        (token.tokenType === "Number" ||
          token.tokenType === "Dimension" ||
          token.tokenType === "Percentage" ||
          token.tokenType === "Ident" ||
          // a collapsed function call (e.g. `var(…)`) counts as a single argument
          token.tokenType === AGGREGATED_TOKEN_TYPE)
      ) {
        // we have a single radius, the array will contain all the indexes of parts that
        // refer to it.
        radiusPartsIndexes.push(i);
      }

      // after that `at` keyword, the position of the circle is defined. It can be represented
      // by 1, 2 or 4 length, percentage or keyword (e.g. start, center, …)
      // So let's collect all those here
      if (
        seenAtKeyword &&
        (token.tokenType === "Number" ||
          token.tokenType === "Dimension" ||
          token.tokenType === "Percentage" ||
          token.tokenType === "Ident" ||
          // a collapsed function call (e.g. `var(…)`) counts as a single argument
          token.tokenType === AGGREGATED_TOKEN_TYPE)
      ) {
        if (token !== previousToken) {
          positionsPartsIndexes.push([i]);
        } else {
          // if the token for the current part is the same one as the previous part, then
          // it represent the same position, so we add the part index to the last position
          // item we added.
          positionsPartsIndexes.at(-1).push(i);
        }
      }

      previousToken = token;
    }

    // We're going to mutate stackEntry.parts, so let's go through the parts in reverse
    // as the indexes in radiusIndexes and positionIndexes refer to the original indexes
    // So first, let's handle positions if there are some
    if (positionsPartsIndexes.length) {
      const centerEl = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": "center",
      });
      for (let i = positionsPartsIndexes.length - 1; i >= 0; i--) {
        const pointEl = this.#createNode("span", {
          class: "inspector-shape-point",
          "data-point": "center",
        });
        if (i === 0) {
          pointEl.setAttribute("data-pair", "x");
        } else if (positionsPartsIndexes.length === 2) {
          // Here we're not handling the first item, and there's only 2 items, so we know
          // we have the y coord
          pointEl.setAttribute("data-pair", "y");
        } else if (i === 2) {
          // If there's more than 2 position, that means we have a <position-four> type,
          // where there's both x,y positions + offsets (e.g. `left 10px top 15px`)
          // In such case, the first item is x (already handled in the first if block),
          // and the third item is y
          pointEl.setAttribute("data-pair", "y");
        }

        const indexes = positionsPartsIndexes[i];
        for (const idx of indexes) {
          pointEl.append(stackEntry.parts[idx]);
        }

        centerEl.prepend(pointEl);
        stackEntry.parts.splice(indexes[0], indexes.length);

        // append any parts between this point and the previous one into centerEl
        const previousIndexes = positionsPartsIndexes[i - 1];
        if (previousIndexes) {
          for (let j = indexes[0] - 1; j > previousIndexes.at(-1); j--) {
            centerEl.prepend(stackEntry.parts[j]);
            stackEntry.parts.splice(j, 1);
          }
        }
      }
      stackEntry.parts.splice(positionsPartsIndexes[0][0], 0, centerEl);
    }

    // Handle radius size if there's one
    if (radiusPartsIndexes.length) {
      const radiusEl = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": "radius",
      });
      for (let i = radiusPartsIndexes.length - 1; i >= 0; i--) {
        const idx = radiusPartsIndexes[i];
        radiusEl.prepend(stackEntry.parts[idx]);
        stackEntry.parts.splice(idx, 1);
      }
      stackEntry.parts.splice(radiusPartsIndexes[0], 0, radiusEl);
    }

    return stackEntry.parts;
  }

  /**
   * Called when we got the closing bracket for the `ellipse()` function.
   * It will append a CSS shapes highlighter toggle next to the value, and parse the value
   * into spans, each containing a point that can be hovered over.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {number} coordsStartIdx
   *        The index in stackEntry.parts at which the coordinates for the ellipse start
   * @returns {Array<Element|Text>} The parts that were handled
   */
  // eslint-disable-next-line complexity
  #onCloseParenthesisForEllipseShape(stackEntry, coordsStartIdx) {
    const radiiPartsIndexes = [];
    const positionsPartsIndexes = [];
    let seenAtKeyword = false;
    let previousToken;
    for (let i = coordsStartIdx; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);

      if (token.tokenType === "Ident" && token.text === "at") {
        seenAtKeyword = true;
        continue;
      }

      // ellipse() can take two radii before `at`, which can be a lengths, percentages,
      // or a keywords (closest-corner, closest-side, farthest-corner, farthest-side)
      if (
        !seenAtKeyword &&
        (token.tokenType === "Number" ||
          token.tokenType === "Dimension" ||
          token.tokenType === "Percentage" ||
          token.tokenType === "Ident" ||
          // a collapsed function call (e.g. `var(…)`) counts as a single argument
          token.tokenType === AGGREGATED_TOKEN_TYPE)
      ) {
        if (token !== previousToken) {
          radiiPartsIndexes.push([i]);
        } else {
          // if the token for the current part is the same one as the previous part, then
          // it represent the same radius, so we add the part index to the last radius
          // item we added.
          radiiPartsIndexes.at(-1).push(i);
        }
      }

      // after that `at` keyword, the position of the ellipse is defined. It can be represented
      // by 1, 2 or 4 length, percentage or keyword (e.g. start, center, …)
      // So let's collect all those here
      if (
        seenAtKeyword &&
        (token.tokenType === "Number" ||
          token.tokenType === "Dimension" ||
          token.tokenType === "Percentage" ||
          token.tokenType === "Ident" ||
          // a collapsed function call (e.g. `var(…)`) counts as a single argument
          token.tokenType === AGGREGATED_TOKEN_TYPE)
      ) {
        if (token !== previousToken) {
          positionsPartsIndexes.push([i]);
        } else {
          // if the token for the current part is the same one as the previous part, then
          // it represent the same position, so we add the part index to the last position
          // item we added.
          positionsPartsIndexes.at(-1).push(i);
        }
      }

      previousToken = token;
    }

    // We're going to mutate stackEntry.parts, so let's go through the parts in reverse
    // as the indexes in radiusIndexes and positionIndexes refer to the original indexes
    // So first, let's handle positions if there are some
    if (positionsPartsIndexes.length) {
      const centerEl = this.#createNode("span", {
        class: "inspector-shape-point",
        "data-point": "center",
      });
      for (let i = positionsPartsIndexes.length - 1; i >= 0; i--) {
        const pointEl = this.#createNode("span", {
          class: "inspector-shape-point",
          "data-point": "center",
        });
        if (i === 0) {
          pointEl.setAttribute("data-pair", "x");
        } else if (positionsPartsIndexes.length === 2) {
          // Here we're not handling the first item, and there's only 2 items, so we know
          // we have the y coord
          pointEl.setAttribute("data-pair", "y");
        } else if (i === 2) {
          // If there's more than 2 position, that means we have a <position-four> type,
          // where there's both x,y positions + offsets (e.g. `left 10px top 15px`)
          // In such case, the first item is x (already handled in the first if block),
          // and the third item is y
          pointEl.setAttribute("data-pair", "y");
        }

        const indexes = positionsPartsIndexes[i];
        for (const idx of indexes) {
          pointEl.append(stackEntry.parts[idx]);
        }
        // we're iterating the parts in reverse, so we need to prepend in centerEl
        centerEl.prepend(pointEl);
        // We can remove as many items as we have indexes here, because if we have
        // multiple parts refering to the same position, their indexes should be consecutive.
        stackEntry.parts.splice(indexes[0], indexes.length);

        // prepend any parts (e.g. whitespaces) between this point and the previous one
        // into centerEl
        const previousIndexes = positionsPartsIndexes[i - 1];
        if (previousIndexes) {
          for (let j = indexes[0] - 1; j > previousIndexes.at(-1); j--) {
            centerEl.prepend(stackEntry.parts[j]);
            stackEntry.parts.splice(j, 1);
          }
        }
      }
      stackEntry.parts.splice(positionsPartsIndexes[0][0], 0, centerEl);
    }

    // Handle radius size if there are some
    if (radiiPartsIndexes.length) {
      for (let i = radiiPartsIndexes.length - 1; i >= 0; i--) {
        const radiusEl = this.#createNode("span", {
          class: "inspector-shape-point",
          // we should only have 2 radii, the first one being rx and the second one ry
          "data-point": i === 0 ? "rx" : "ry",
        });

        const indexes = radiiPartsIndexes[i];
        for (const idx of indexes) {
          radiusEl.append(stackEntry.parts[idx]);
        }
        // We can remove as many items as we have indexes here, because if we have
        // multiple parts refering to the same radius, their indexes should be consecutive.
        stackEntry.parts.splice(indexes[0], indexes.length, radiusEl);
      }
    }

    return stackEntry.parts;
  }

  /**
   * Called when we got the closing bracket for the `inset()` function.
   * It will append a CSS shapes highlighter toggle next to the value, and parse the value
   * into spans, each containing a point that can be hovered over.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {number} coordsStartIdx
   *        The index in stackEntry.parts at which the coordinates for the inset start
   * @returns {Array<Element|Text>} The parts that were handled
   */
  #onCloseParenthesisForInsetShape(stackEntry, coordsStartIdx) {
    const insetPointsPartsIndexes = [];
    let previousToken;
    for (let i = coordsStartIdx; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);

      if (token.tokenType === "Ident" && token.text === "round") {
        // Once we see the `round` keyword, we can stop looping, we have all the coordinates
        // we need
        break;
      }

      if (
        token.tokenType !== "Number" &&
        token.tokenType !== "Dimension" &&
        token.tokenType !== "Percentage" &&
        // a collapsed function call (e.g. `var(…)`) counts as a single argument
        token.tokenType !== AGGREGATED_TOKEN_TYPE
      ) {
        continue;
      }

      const lastPoint = insetPointsPartsIndexes.at(-1);
      if (!lastPoint || previousToken !== token) {
        insetPointsPartsIndexes.push([i]);
      } else if (lastPoint) {
        lastPoint.push(i);
      }

      previousToken = token;
    }

    const insetPoints = ["top", "right", "bottom", "left"];

    // Let's iterate through points in reverse as we're going to mutate stackEntry.parts
    // and the indexes in `points` refer to the original indexes
    for (let i = insetPointsPartsIndexes.length - 1; i >= 0; i--) {
      const pointPartsIndexes = insetPointsPartsIndexes[i];
      const shapePointNode = this.#createNode("span", {
        class: "inspector-shape-point",
      });

      // insetPoints contains the 4 different possible inset points in the order they are
      // defined. By taking the modulo of the index in insetPoints with the number of nodes,
      // we can get which node represents each point (e.g. if there is only 1 node, it
      // represents all 4 points). The exception is "left" when there are 3 nodes. In that
      // case, it is nodes[1] that represents the left point rather than nodes[0].
      if (insetPointsPartsIndexes.length === 1) {
        shapePointNode.classList.add(...insetPoints);
      } else if (insetPointsPartsIndexes.length === 2) {
        if (i === 0) {
          shapePointNode.classList.add(insetPoints[0], insetPoints[2]);
        } else {
          shapePointNode.classList.add(insetPoints[1], insetPoints[3]);
        }
      } else if (insetPointsPartsIndexes.length === 3) {
        if (i === 1) {
          shapePointNode.classList.add(insetPoints[1], insetPoints[3]);
        } else {
          shapePointNode.classList.add(insetPoints[i]);
        }
      } else if (insetPointsPartsIndexes.length === 4) {
        shapePointNode.classList.add(insetPoints[i]);
      }

      for (const idx of pointPartsIndexes) {
        shapePointNode.append(stackEntry.parts[idx]);
      }

      stackEntry.parts.splice(
        pointPartsIndexes[0],
        pointPartsIndexes.at(-1) - pointPartsIndexes[0] + 1,
        shapePointNode
      );
    }

    return stackEntry.parts;
  }

  /**
   * Called when we got the closing parenthesis for `url()`.
   * It will wrap the URL into a proper <a> element.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  #onCloseParenthesisForUrl(stackEntry, options) {
    if (!options.urlClass) {
      return stackEntry.parts;
    }

    // url() with quoted strings are not mapped as UnquotedUrl, instead, we get a "Function"
    // token with "url" (the one we're closing here), and later, a "QuotedString" token
    // which contains the actual URL.
    // So here, we only need to loop through the parts to find the one which holds the
    // QuotedString token and wrap it in an anchor.
    let url;
    for (let i = 0; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);
      if (token?.tokenType !== "QuotedString") {
        continue;
      }

      // url() only takes a string, so we'll only have a single part refering to the url token
      url = token.value;
      break;
    }

    if (!url) {
      return stackEntry.parts;
    }

    return this.#createURLElements(stackEntry.text, url, options);
  }

  /**
   * Called when we got the closing parenthesis for `var()`.
   *
   * @param {object} stackEntry
   *        The last item in this.#stack
   * @param {object} options
   *        options passed to the parse function. @see #mergeOptions for valid options
   *        and default values
   * @returns {Array<string|Element>} The updated parts for the stack entry that is being closed.
   */
  // eslint-disable-next-line complexity
  #onCloseParenthesisForVar(stackEntry, options) {
    if (!options.getVariableData) {
      return stackEntry.parts;
    }

    let varNameIndex = null;
    let varName = null;
    let fallbackStartIndex = null;
    for (let i = 0; i < stackEntry.parts.length; i++) {
      const part = stackEntry.parts[i];
      const token = stackEntry.tokensByPart.get(part);

      // The variable name is the first Ident we find
      if (varNameIndex === null && token.tokenType === "Ident") {
        varNameIndex = i;
        varName = token.text;
      } else if (token.tokenType === "Comma") {
        // Anything between the first comma and the end of the function is considered a
        // fallback value.
        fallbackStartIndex = i + 1;
        break;
      }
    }

    // Shouldn't happen, but let's be safe
    if (varNameIndex === null) {
      return stackEntry.parts;
    }

    const varData = options.getVariableData(varName);
    const varValue =
      typeof varData.value === "string"
        ? varData.value
        : varData.registeredProperty?.initialValue;
    let varStartingStyleValue;
    if (options.inStartingStyleRule) {
      varStartingStyleValue =
        typeof varData.startingStyle === "string"
          ? varData.startingStyle
          : // If the variable is not set in starting style, then it will default to either:
            // - a declaration in a "regular" rule
            // - or if there's no declaration in regular rule, to the registered property initial-value.
            varValue;
    }

    let varSubstitutedValue = options.inStartingStyleRule
      ? varStartingStyleValue
      : varValue;
    const variableExists = typeof varSubstitutedValue === "string";
    // TODO: we should also check if the variable is not guaranteed invalid (see Bug 1904013)
    const shouldUseFallback = !variableExists;
    const varComputedValue = varData.computedValue;
    const varNameNodeOptions = {};
    const varFallbackNodeOptions = {};

    if (variableExists) {
      // The variable value is valid, store the substituted value in a data attribute to
      // be reused by the variable tooltip.
      varNameNodeOptions["data-variable"] = varSubstitutedValue;
      varNameNodeOptions.class = options.matchedVariableClass;
      varFallbackNodeOptions.class = options.unmatchedClass;

      // Display computed value when it exists, is different from the substituted value
      // we computed, and we're not inside a starting-style rule
      if (
        !options.inStartingStyleRule &&
        typeof varComputedValue === "string" &&
        varComputedValue !== varSubstitutedValue
      ) {
        varNameNodeOptions["data-variable-computed"] = varComputedValue;
      }

      // Display starting-style value when not in a starting style rule
      if (
        !options.inStartingStyleRule &&
        typeof varData.startingStyle === "string"
      ) {
        varNameNodeOptions["data-starting-style-variable"] =
          varData.startingStyle;
      }

      if (varData.registeredProperty) {
        const { initialValue, syntax, inherits } = varData.registeredProperty;
        varNameNodeOptions["data-registered-property-initial-value"] =
          initialValue;
        varNameNodeOptions["data-registered-property-syntax"] = syntax;
        // createNode does not handle `false`, let's stringify the boolean.
        varNameNodeOptions["data-registered-property-inherits"] = `${inherits}`;
      }
    } else {
      // The variable is not set and does not have an initial value, mark it unmatched.
      varNameNodeOptions.class = options.unmatchedClass;
      varNameNodeOptions["data-variable"] = STYLE_INSPECTOR_L10N.getFormatStr(
        "rule.variableUnset",
        varName
      );
    }

    const varNameNode = this.#createNode("span", varNameNodeOptions);
    varNameNode.append(stackEntry.parts[varNameIndex]);
    stackEntry.parts.splice(varNameIndex, 1, varNameNode);

    if (variableExists && options.showJumpToVariableButton) {
      varNameNode.append(
        this.#createNode("button", {
          class: "ruleview-variable-link jump-definition",
          "data-variable-name": varName,
          title: VARIABLE_JUMP_DEFINITION_TITLE,
        })
      );
    }

    // From https://drafts.csswg.org/css-variables/#using-variables:
    // > var(--a,) is a valid function, specifying that if the --a custom property is
    // > invalid or missing, the var() should be replaced with nothing.
    //
    // So if we saw a comma, initialize the value with an empty string
    let fallbackSubstitutedValue = fallbackStartIndex !== null ? "" : null;

    if (fallbackStartIndex !== null) {
      // We want to wrap the fallback into a span, so let's find the last non whitespace
      // token before the closing parenthesis now
      let fallbackEndIndex = null;
      for (
        // we can start at the part before the last one, as the last one will always be
        // the closing parenthesis
        let i = stackEntry.parts.length - 2;
        i >= fallbackStartIndex;
        i--
      ) {
        const part = stackEntry.parts[i];
        const token = stackEntry.tokensByPart.get(part);
        if (token.tokenType !== "WhiteSpace") {
          fallbackEndIndex = i;
          break;
        }
      }

      const fallbackNode = this.#createNode("span", varFallbackNodeOptions);
      let previousToken;
      for (let i = fallbackStartIndex; i <= fallbackEndIndex; i++) {
        const part = stackEntry.parts[i];
        const token = stackEntry.tokensByPart.get(part);
        fallbackNode.append(part);
        if (previousToken === token) {
          continue;
        }
        if (token?.tokenType === AGGREGATED_TOKEN_TYPE) {
          fallbackSubstitutedValue +=
            token.data.substitutedText ?? token.data.text;
        } else {
          fallbackSubstitutedValue += part.textContent;
        }
        previousToken = token;
      }
      stackEntry.parts.splice(
        fallbackStartIndex,
        fallbackEndIndex - fallbackStartIndex + 1,
        fallbackNode
      );
    }

    // Now that we went through the fallback, we can re-compute varSubstitutedValue
    // to potentially include the fallback value.
    if (shouldUseFallback) {
      // If the fallback should be used (i.e. the variable value is guaranteed invalid)
      // but none was found, then the substituted value should be an empty string, as
      // defined in https://drafts.csswg.org/css-variables/#guaranteed-invalid:
      // > The guaranteed-invalid value serializes as the empty string
      if (fallbackSubstitutedValue === null) {
        varSubstitutedValue = "";
      } else {
        varSubstitutedValue = fallbackSubstitutedValue;
      }
    }

    // TODO: We should handle the following case (see Bug 2006565)
    // From https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Values/var#invalid_values:
    // > var() functions can resolve to invalid values if:
    // > - […]
    // > - The custom property is defined but its value is an invalid value for the
    //     property it is used in.
    // > When this happens, the property is treated as if it has value unset

    const varComputedOrSubstitutedValue = options.inStartingStyleRule
      ? varSubstitutedValue
      : (varComputedValue ?? varSubstitutedValue);

    // Put the substitutedText in the entry so it can then be consumed in onCloseParenthesis
    stackEntry.substitutedText = varComputedOrSubstitutedValue;

    if (
      options.supportsColor ||
      ((options.expectFilter || options.isVariable) &&
        this.#stack.length !== 0 &&
        this.#stack.at(-1).isColorTakingFunction)
    ) {
      // InspectorUtils.isValidCSSColor returns true for `light-dark()` function,
      // but `#isValidColor` returns false. As the latter is used in #appendColor,
      // we need to check that both functions return true.
      const colorObj =
        varSubstitutedValue &&
        InspectorUtils.isValidCSSColor(varComputedOrSubstitutedValue)
          ? new colorUtils.CssColor(varComputedOrSubstitutedValue)
          : null;
      if (colorObj && this.#isValidColor(colorObj)) {
        const colorFunctionEntry = this.#stack.findLast(
          entry => entry.isColorTakingFunction
        );
        const colorContainerEl = this.#createColorContainerElement(
          colorObj,
          {
            ...options,
            colorFunction: colorFunctionEntry?.functionName,
          },
          stackEntry.parts
        );
        return [colorContainerEl];
      }
    }

    const variableNode = this.#createNode("span", {});
    variableNode.append(...stackEntry.parts);
    return [variableNode];
  }

  /**
   * Parse a string.
   *
   * @param  {string} text
   *         Text to parse.
   * @param  {object} [options]
   *         Options object. For valid options and default values see
   *         #mergeOptions().
   * @return {DocumentFragment}
   *         A document fragment.
   */
  #parse(text, options = {}) {
    text = text.trim();
    this.#parsed.length = 0;
    this.#stack.length = 0;

    const tokenStream = new InspectorCSSParserWrapper(text);
    return this.#doParse(text, options, tokenStream);
  }

  /**
   * Returns true if it's a "display: [inline-]flex" token.
   *
   * @param  {string} text
   *         The parsed text.
   * @param  {object} token
   *         The parsed token.
   * @param  {object} options
   *         The options given to #parse.
   */
  #isDisplayFlex(text, token, options) {
    return (
      options.expectDisplay &&
      (token.text === "flex" || token.text === "inline-flex")
    );
  }

  /**
   * Returns true if it's a "display: [inline-]grid" token.
   *
   * @param  {string} text
   *         The parsed text.
   * @param  {object} token
   *         The parsed token.
   * @param  {object} options
   *         The options given to #parse.
   */
  #isDisplayGrid(text, token, options) {
    return (
      options.expectDisplay &&
      (token.text === "grid" || token.text === "inline-grid")
    );
  }

  /**
   * Create an element for a cubic-bezier timing function.
   * Returns null if the element couldn't be created
   *
   * @param {object} options
   * @param {Array<string|Node>} options.children
   *        Children (strings or node) of the container that will be created.
   * @param {object} options.parseOptions
   *        Options object. For valid options and default values see
   *        #mergeOptions()
   * @return {Node|null}
   */
  #createCubicBezierContainer({ children, parseOptions }) {
    let bezier = "";
    for (const child of children) {
      bezier += child.textContent ?? child;
    }

    if (bezier.includes("var(")) {
      // For now, we don't support cubic-bezier with CSS variables (see Bug 2031695)
      return null;
    }

    const container = this.#createNode("span", {
      "data-bezier": bezier,
    });

    if (parseOptions.bezierSwatchClass) {
      const swatch = this.#createNode("span", {
        class: parseOptions.bezierSwatchClass,
        tabindex: "0",
        role: "button",
      });
      container.appendChild(swatch);
    }

    const valueEl = this.#createNode("span", {
      class: parseOptions.bezierClass,
    });
    valueEl.append(...children);

    container.appendChild(valueEl);
    return container;
  }

  /**
   * Append a Flexbox|Grid highlighter toggle icon next to the value in a
   * "display: [inline-]flex" or "display: [inline-]grid" declaration.
   *
   * @param {string} text
   *        The text value to append
   * @param {string} toggleButtonClassName
   *        The class name for the toggle button.
   *        If not passed/empty, the toggle button won't be created.
   */
  #appendDisplayWithHighlighterToggle(text, toggleButtonClassName) {
    const container = this.#createNode("span", {});

    if (toggleButtonClassName) {
      const toggleButton = this.#createNode("button", {
        class: toggleButtonClassName,
      });
      container.append(toggleButton);
    }

    const value = this.#createNode("span", {}, text);
    container.append(value);
    this.#append(container);
  }

  /**
   * Append a angle value to the output
   *
   * @param {string} angle
   *        angle to append
   * @param {object} options
   *        Options object. For valid options and default values see
   *        #mergeOptions()
   * @param {object} token
   */
  #appendAngle(angle, options, token) {
    const angleObj = new angleUtils.CssAngle(angle);
    const container = this.#createNode("span", {
      "data-angle": angle,
    });

    if (options.angleSwatchClass) {
      const swatch = this.#createNode("span", {
        class: options.angleSwatchClass,
        tabindex: "0",
        role: "button",
      });
      this.#angleSwatches.set(swatch, angleObj);
      swatch.addEventListener("mousedown", this.#onAngleSwatchMouseDown);

      // Add click listener to stop event propagation when shift key is pressed
      // in order to prevent the value input to be focused.
      // Bug 711942 will add a tooltip to edit angle values and we should
      // be able to move this listener to Tooltip.js when it'll be implemented.
      swatch.addEventListener("click", function (event) {
        if (event.shiftKey) {
          event.stopPropagation();
        }
      });
      container.appendChild(swatch);
    }

    const value = this.#createNode(
      "span",
      {
        class: options.angleClass,
      },
      angle
    );

    container.appendChild(value);
    this.#append(container, token);
  }

  /**
   * Check if a CSS property supports a specific value.
   *
   * @param  {string} name
   *         CSS Property name to check
   * @param  {string} value
   *         CSS Property value to check
   * @param  {object} options
   *         Options object. For valid options and default values see #mergeOptions().
   */
  #cssPropertySupportsValue(name, value, options = {}) {
    if (
      options.isValid ||
      // The filter property is special in that we want to show the swatch even if the
      // value is invalid, because this way the user can easily use the editor to fix it.
      options.expectFilter
    ) {
      return true;
    }

    // Checking pair as a CSS declaration string to account for "!important" in value.
    const declaration = `${name}:${value}`;
    return this.#doc.defaultView.CSS.supports(declaration);
  }

  /**
   * Tests if a given colorObject output by CssColor is valid for parsing.
   * Valid means it's really a color, not any of the CssColor SPECIAL_VALUES
   * except transparent
   */
  #isValidColor(colorObj) {
    return (
      colorObj.valid &&
      (!colorObj.specialValue || colorObj.specialValue === "transparent")
    );
  }

  /**
   * Append a color to the output.
   *
   * @param {string} color
   *         Color to append
   * @param {object} [options]
   * @param {CSSColor} options.colorObj: A css color for the passed color. Will be computed
   *         if not passed.
   * @param {string} options.colorFunction: The color function that is used to produce this color
   * @param {*} For all the other valid options and default values see #mergeOptions().
   * @param {object} token
   */
  #appendColor(color, options, token) {
    const colorObj = options.colorObj || new colorUtils.CssColor(color);

    if (this.#isValidColor(colorObj)) {
      const colorContainerEl = this.#createColorContainerElement(
        colorObj,
        options
      );

      this.#append(colorContainerEl, token);
    } else {
      this.#appendTextNode(color, token);
    }
  }

  #createColorContainerElement(colorObj, options, children) {
    let color = colorObj.authored;
    const containerEl = this.#createNode("span", {
      "data-color": color,
    });

    if (options.colorSwatchClass) {
      let attributes = {
        class: options.colorSwatchClass,
        style: "background-color:" + color,
      };

      // Color swatches next to values trigger the color editor everywhere aside from
      // the Computed panel where values are read-only.
      if (!options.colorSwatchReadOnly) {
        attributes = { ...attributes, tabindex: "0", role: "button" };
      }

      // The swatch is a <span> instead of a <button> intentionally. See Bug 1597125.
      // It is made keyboard accessible via `tabindex` and has keydown handlers
      // attached for pressing SPACE and RETURN in SwatchBasedEditorTooltip.js
      const swatch = this.#createNode("span", attributes);
      this.#colorSwatches.set(swatch, colorObj);
      if (options.colorFunction) {
        swatch.dataset.colorFunction = options.colorFunction;
      }
      swatch.addEventListener("mousedown", this.#onColorSwatchMouseDown);
      containerEl.appendChild(swatch);
      containerEl.classList.add("color-swatch-container");
    }

    let colorUnit = options.defaultColorUnit;
    if (!options.useDefaultColorUnit) {
      // If we're not being asked to convert the color to the default color type
      // specified by the user, then force the CssColor instance to be set to the type
      // of the current color.
      // Not having a type means that the default color type will be automatically used.
      colorUnit = colorUtils.classifyColor(color);
    }
    color = colorObj.toString(colorUnit);
    containerEl.dataset.color = color;

    const valueEl = this.#createNode("span", {
      class: options.colorClass,
    });
    if (children) {
      valueEl.append(...children);
    } else {
      valueEl.append(color);
    }
    containerEl.append(valueEl);
    return containerEl;
  }

  /**
   * Wrap some existing nodes in a filter editor.
   *
   * @param {string} filters
   *        The full text of the "filter" property.
   * @param {object} options
   *        The options object passed to parseCssProperty().
   * @param {object} nodes
   *        Nodes created by #toDOM().
   *
   * @returns {object}
   *        A new node that supplies a filter swatch and that wraps |nodes|.
   */
  #wrapFilter(filters, options, nodes) {
    const container = this.#createNode("span", {
      "data-filters": filters,
    });

    if (options.filterSwatchClass) {
      const swatch = this.#createNode("span", {
        class: options.filterSwatchClass,
        tabindex: "0",
        role: "button",
      });
      container.appendChild(swatch);
    }

    const value = this.#createNode("span", {
      class: options.filterClass,
    });
    value.appendChild(nodes);
    container.appendChild(value);

    return container;
  }

  #onColorSwatchMouseDown = event => {
    if (!event.shiftKey) {
      return;
    }

    // Prevent click event to be fired to not show the tooltip
    event.stopPropagation();
    // Prevent text selection but switch the focus
    event.preventDefault();
    event.target.focus({ focusVisible: false });

    const swatch = event.target;
    const color = this.#colorSwatches.get(swatch);
    const val = color.nextColorUnit();

    swatch.nextElementSibling.textContent = val;
    swatch.parentNode.dataset.color = val;

    const unitChangeEvent = new swatch.documentGlobal.CustomEvent(
      "unit-change"
    );
    swatch.dispatchEvent(unitChangeEvent);
  };

  #onAngleSwatchMouseDown = event => {
    if (!event.shiftKey) {
      return;
    }

    event.stopPropagation();

    const swatch = event.target;
    const angle = this.#angleSwatches.get(swatch);
    const val = angle.nextAngleUnit();

    swatch.nextElementSibling.textContent = val;

    const unitChangeEvent = new swatch.documentGlobal.CustomEvent(
      "unit-change"
    );
    swatch.dispatchEvent(unitChangeEvent);
  };

  /**
   * A helper function that sanitizes a possibly-unterminated URL.
   */
  #sanitizeURL(url) {
    // Re-lex the URL and add any needed termination characters.
    const urlTokenizer = new InspectorCSSParserWrapper(url, {
      trackEOFChars: true,
    });
    // Just read until EOF; there will only be a single token.
    while (urlTokenizer.nextToken()) {
      // Nothing.
    }

    return urlTokenizer.performEOFFixup(url);
  }

  /**
   * Returns the elements representing a URL.
   *
   * @param  {string} match
   *         Complete match that may include "url(xxx)"
   * @param  {string} url
   *         Actual URL
   * @param  {object} options
   *         Options object. For valid options and default values see #mergeOptions().
   * @returns {Array<Node>}
   */
  #createURLElements(match, url, options) {
    if (!options.urlClass) {
      return [this.#createTextElement(match)];
    }

    // Sanitize the URL. Note that if we modify the URL, we just
    // leave the termination characters. This isn't strictly
    // "as-authored", but it makes a bit more sense.
    match = this.#sanitizeURL(match);
    const urlParts = URL_REGEX.exec(match);

    // Bail out if that didn't match anything.
    if (!urlParts) {
      return [this.#doc.createTextNode(match)];
    }

    const { leader, body, trailer } = urlParts.groups;

    return [
      this.#doc.createTextNode(leader),
      this.#createNode(
        "a",
        {
          target: "_blank",
          class: options.urlClass,
          href: options.baseURI
            ? (URL.parse(url, options.baseURI)?.href ?? url)
            : url,
        },
        body
      ),
      this.#doc.createTextNode(trailer),
    ];
  }

  /**
   * Wrap a font family in a special element
   *
   * @param  {number} fontFamilyStartPartIndex
   *         The index in `parts` at which the font-family starts
   * @param  {object} options
   *         Options object. For valid options and default values see
   *         #mergeOptions().
   */
  #wrapFontFamilyName(fontFamilyStartPartIndex, options) {
    if (!options.expectFont) {
      return;
    }

    const parts = this.#getCurrentStackParts();
    // We have the beginning of the font family, we need to find the end.
    // Loop through the parts in reverse to find the first non whitespace character.
    // By default, let's consider the last part as the end of the font name
    let fontFamilyEndPartIndex = parts.length - 1;
    for (let i = parts.length - 1; i >= fontFamilyStartPartIndex; i--) {
      const part = parts[i];
      if (part.textContent.trim() !== "") {
        fontFamilyEndPartIndex = i;
        break;
      }
    }

    // Wrap the parts in a dedicated element
    const fontFamilyNode = this.#createNode("span", {
      class: options.fontFamilyClass,
    });

    // If the family name is quoted, we need to put the quotes outside of the family node
    // So first let's compute the family name (it might be made out of multiple parts at
    // the moment, e.g. if we have `Helvetica Black`)
    let familyName = "";
    for (let i = fontFamilyStartPartIndex; i <= fontFamilyEndPartIndex; i++) {
      familyName += parts[i].textContent;
    }

    // We'll associate the new part we create with this aggregated token, so other functions
    // know those shouldn't be processed.
    const aggregatedToken = this.#stack.length
      ? {
          tokenType: AGGREGATED_TOKEN_TYPE,
          data: {
            text: familyName,
          },
        }
      : null;
    const stackEntry = this.#stack.length ? this.#stack.at(-1) : null;
    if (stackEntry) {
      stackEntry.tokensByPart.set(fontFamilyNode, aggregatedToken);
    }

    // Extracting opening and closing quotes, as well as the font name
    const quoteRegex = /^(?<open>['"])(?<name>[^'"]*)(?<close>['"])$/g;
    const regexResult = quoteRegex.exec(familyName);
    // If it's actually wrapped in quote
    if (regexResult !== null) {
      // Then, first append the closing quote, as we're going to modify parts and we rely
      // on the indexes to insert the name at the right spot
      const part = this.#doc.createTextNode(regexResult.groups.close);
      parts.splice(fontFamilyEndPartIndex + 1, 0, part);

      if (stackEntry) {
        stackEntry.tokensByPart.set(part, aggregatedToken);
      }
      // Update the family name with the non-quoted one
      familyName = regexResult.groups.name;
    }

    fontFamilyNode.append(familyName);

    // Then we want to insert our container, and remove the parts that are representing it
    const fontFamilyNodeChildCount =
      fontFamilyEndPartIndex - fontFamilyStartPartIndex + 1;
    parts.splice(
      fontFamilyStartPartIndex,
      fontFamilyNodeChildCount,
      fontFamilyNode
    );

    // Finally we insert the opening quote
    if (regexResult !== null) {
      const part = this.#doc.createTextNode(regexResult.groups.open);
      parts.splice(fontFamilyStartPartIndex, 0, part);

      if (stackEntry) {
        stackEntry.tokensByPart.set(part, aggregatedToken);
      }
    }
  }

  /**
   * Create a node.
   *
   * @param  {string} tagName
   *         Tag type e.g. "div"
   * @param  {object} attributes
   *         e.g. {class: "someClass", style: "cursor:pointer"};
   * @param  {string} [value]
   *         If a value is included it will be appended as a text node inside
   *         the tag. This is useful e.g. for span tags.
   * @return {Node} Newly created Node.
   */
  #createNode(tagName, attributes, value = "") {
    const node = this.#doc.createElementNS(HTML_NS, tagName);
    const attrs = Object.getOwnPropertyNames(attributes);

    for (const attr of attrs) {
      const attrValue = attributes[attr];
      if (attrValue !== null && attrValue !== undefined) {
        node.setAttribute(attr, attributes[attr]);
      }
    }

    if (value) {
      const textNode = this.#doc.createTextNode(value);
      node.appendChild(textNode);
      const truncated = value.length > TRUNCATE_LENGTH_THRESHOLD;
      node.classList.toggle(TRUNCATE_NODE_CLASSNAME, truncated);
    }

    return node;
  }

  /**
   * Create an element representing a simple text.
   *
   * @param  {string} text
   *         Text to append
   * @returns {Text|Element} Returns a Text, or, if the text is greater than the truncate
   *          threshold, a Node with a specific class to trigger CSS "truncation".
   */
  #createTextElement(text) {
    if (text.length > TRUNCATE_LENGTH_THRESHOLD) {
      // If the text is too long, force creating a node, which will add the
      // necessary classname to truncate the property correctly.
      return this.#createNode("span", {}, text);
    }

    return this.#doc.createTextNode(text);
  }

  /**
   * Create and append a node to the output.
   *
   * @param  {string} tagName
   *         Tag type e.g. "div"
   * @param  {object} attributes
   *         e.g. {class: "someClass", style: "cursor:pointer"};
   * @param  {string} [value]
   *         If a value is included it will be appended as a text node inside
   *         the tag. This is useful e.g. for span tags.
   * @param  {object} token
   * @return {number} The index of the new part in the parts array
   */
  #appendNode(tagName, attributes, value, token) {
    const node = this.#createNode(tagName, attributes, value);
    return this.#append(node, token);
  }

  /**
   * Append an element or a text node to the output.
   *
   * @param {Element|Text} item
   * @param {object} token
   * @return {number} The index of the new part in the parts array
   */
  #append(item, token = null) {
    const len = this.#getCurrentStackParts().push(item);

    if (token !== null && this.#stack.length) {
      const stackEntry = this.#stack.at(-1);
      stackEntry.tokensByPart.set(item, token);
    }

    return len - 1;
  }

  /**
   * Append a text node to the output. If the previously output item was a text
   * node then we append the text to that node.
   *
   * @param  {string} text
   *         Text to append
   * @param  {object} token
   * @return {number} The index of the new part in the parts array
   */
  #appendTextNode(text, token) {
    if (text.length > TRUNCATE_LENGTH_THRESHOLD) {
      // If the text is too long, force creating a node, which will add the
      // necessary classname to truncate the property correctly.
      return this.#appendNode("span", {}, text, token);
    }

    return this.#append(this.#doc.createTextNode(text), token);
  }

  #getCurrentStackParts() {
    return this.#stack.at(-1)?.parts || this.#parsed;
  }

  /**
   * Take all output and append it into a single DocumentFragment.
   *
   * @return {DocumentFragment}
   *         Document Fragment
   */
  #toDOM() {
    const frag = this.#doc.createDocumentFragment();

    for (const item of this.#parsed) {
      if (typeof item === "string") {
        frag.appendChild(this.#doc.createTextNode(item));
      } else {
        frag.appendChild(item);
      }
    }

    this.#parsed.length = 0;
    this.#stack.length = 0;
    return frag;
  }

  /**
   * Merges options objects. Default values are set here.
   *
   * @param  {object} overrides
   *         The option values to override e.g. #mergeOptions({colors: false})
   * @param {boolean} overrides.useDefaultColorUnit: Convert colors to the default type
   *                                                 selected in the options panel.
   * @param {string} overrides.angleClass: The class to use for the angle value that follows
   *                                       the swatch.
   * @param {string} overrides.angleSwatchClass: The class to use for angle swatches.
   * @param {string} overrides.bezierClass: The class to use for the bezier value that
   *        follows the swatch.
   * @param {string} overrides.bezierSwatchClass: The class to use for bezier swatches.
   * @param {string} overrides.colorClass: The class to use for the color value that
   *        follows the swatch.
   * @param {string} overrides.colorSwatchClass: The class to use for color swatches.
   * @param {boolean} overrides.colorSwatchReadOnly: Whether the resulting color swatch
   *        should be read-only or not. Defaults to false.
   * @param {boolean} overrides.filterSwatch: A special case for parsing a "filter" property,
   *        causing the parser to skip the call to #wrapFilter. Used only for previewing
   *        with the filter swatch.
   * @param {string} overrides.flexClass: The class to use for the flex icon.
   * @param {string} overrides.gridClass: The class to use for the grid icon.
   * @param {string} overrides.shapeClass: The class to use for the shape value that
   *         follows the swatch.
   * @param {string} overrides.shapeSwatchClass: The class to use for the shape swatch.
   * @param {string} overrides.urlClass: The class to be used for url() links.
   * @param {string} overrides.fontFamilyClass: The class to be used for font families.
   * @param {string} overrides.unmatchedClass: The class to use for a component of
   *        a `var(…)` or `attr(…)` that is not in use.
   * @param {boolean} overrides.supportsColor: Does the CSS property support colors?
   * @param {string} overrides.baseURI: A string used to resolve relative links.
   * @param {Function} overrides.getVariableData: A function taking a single argument,
   *        the name of a variable. This should return an object with the following properties:
   *          - {String|undefined} value: The variable's value. Undefined if variable is
   *            not set.
   *          - {RegisteredPropertyResource|undefined} registeredProperty: The registered
   *            property data (syntax, initial value, inherits). Undefined if the variable
   *            is not a registered property.
   * @param {Function} overrides.getAttributeValue: A function taking a single argument,
   *        the name of an attribute. This should return the value of the attribute, or
   *        null if the attribute doesn't exist.
   * @param {boolean} overrides.showJumpToVariableButton: Should we show a jump to
   *        definition for CSS variables. Defaults to true.
   * @param {boolean} overrides.isDarkColorScheme: Is the currently applied color scheme dark.
   * @param {boolean} overrides.isValid: Is the name+value valid.
   * @param {boolean} overrides.cssExplainersEnabled: Are CSS explainers enabled
   * @param {number} overrides.siblingCount: Optional number that should match the result
   *        of `sibling-count()` (https://developer.mozilla.org/docs/Web/CSS/Reference/Values/sibling-count)
   *        for the selected element.
   * @param {number} overrides.siblingIndex: Optional number that should match the result
   *        of `sibling-index()` (https://developer.mozilla.org/docs/Web/CSS/Reference/Values/sibling-index)
   *        for the selected element.
   * @return {object} Overridden options object
   */
  #mergeOptions(overrides) {
    const defaults = {
      useDefaultColorUnit: true,
      defaultColorUnit: "authored",
      angleClass: null,
      angleSwatchClass: null,
      bezierClass: null,
      bezierSwatchClass: null,
      colorClass: null,
      colorSwatchClass: null,
      colorSwatchReadOnly: false,
      cssExplainersEnabled: false,
      filterSwatch: false,
      flexClass: null,
      gridClass: null,
      shapeClass: null,
      shapeSwatchClass: null,
      supportsColor: false,
      urlClass: null,
      fontFamilyClass: null,
      baseURI: undefined,
      getVariableData: null,
      getAttributeValue: null,
      showJumpToVariableButton: true,
      unmatchedClass: null,
      inStartingStyleRule: false,
      isDarkColorScheme: null,
      siblingCount: null,
      siblingIndex: null,
    };

    for (const item in overrides) {
      defaults[item] = overrides[item];
    }
    return defaults;
  }
}

module.exports = OutputParser;
