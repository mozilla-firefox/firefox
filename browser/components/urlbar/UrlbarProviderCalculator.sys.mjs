/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  l10n: () => new Localization(["browser/browser.ftl"], true),
  ClipboardHelper: {
    service: "@mozilla.org/widget/clipboardhelper;1",
    iid: Ci.nsIClipboardHelper,
  },
});

// This pref is relative to the `browser.urlbar` branch.
const ENABLED_PREF = "suggest.calculator";

const DYNAMIC_RESULT_TYPE = "calculator";

const VIEW_TEMPLATE = {
  attributes: {
    selectable: true,
  },
  children: [
    {
      name: "content",
      tag: "span",
      attributes: { class: "urlbarView-no-wrap" },
      children: [
        {
          name: "icon",
          tag: "img",
          attributes: { class: "urlbarView-favicon" },
        },
        {
          name: "tail150",
          tag: "img",
          attributes: {
            class: "urlbarView-dynamic-calculator-tail150",
            role: "button",
            "aria-hidden": "true",
            "keyboard-inaccessible": "true",
            "data-command": "tail150",
            src: "chrome://branding/content/icon48.png",
          },
        },
        {
          name: "input",
          tag: "strong",
        },
        {
          name: "action",
          tag: "span",
        },
      ],
    },
  ],
};

// Minimum number of parts of the expression before we show a result.
const MIN_EXPRESSION_LENGTH = 3;
const UNDEFINED_VALUE = "undefined";
// Minimum and maximum value of result before it switches to scientific
// notation. Displaying numbers longer than 10 digits long or a decimal
// containing 5 or more leading zeroes in scientific notation improves
// readability.
const FULL_NUMBER_MAX_THRESHOLD = 1 * 10 ** 10;
const FULL_NUMBER_MIN_THRESHOLD = 10 ** -5;

/**
 * A provider that returns a suggested url to the user based on what
 * they have currently typed so they can navigate directly.
 */
export class UrlbarProviderCalculator extends UrlbarProvider {
  // Caching the sapName is safe because each supported SAP has its own instance
  // of this provider.
  #sapName;

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    return (
      queryContext.trimmedSearchString &&
      !queryContext.restrictInSearchMode() &&
      lazy.UrlbarPrefs.get(ENABLED_PREF)
    );
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    this.#sapName = queryContext.sapName;
    try {
      // Calculator will throw when given an invalid expression, therefore
      // addCallback will never be called.
      let postfix = Calculator.infix2postfix(queryContext.searchString);
      if (postfix.length < MIN_EXPRESSION_LENGTH) {
        return;
      }
      let value = Calculator.evaluatePostfix(postfix);
      const result = new lazy.UrlbarResult({
        type: lazy.UrlbarShared.RESULT_TYPE.DYNAMIC,
        source: lazy.UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        suggestedIndex: 1,
        payload: {
          value,
          input: queryContext.searchString,
          dynamicType: DYNAMIC_RESULT_TYPE,
        },
      });
      addCallback(this, result);
    } catch (e) {}
  }

  getViewTemplate(_result) {
    return VIEW_TEMPLATE;
  }

  getViewUpdate(result) {
    const { value } = result.payload;

    return {
      icon: {
        attributes: {
          src: "chrome://global/skin/icons/edit-copy.svg",
        },
      },
      input:
        value == UNDEFINED_VALUE
          ? {
              l10n: { id: "urlbar-result-action-undefined-calculator-result" },
            }
          : {
              textContent: `= ${value}`,
              attributes: { dir: "ltr" },
            },
      action: {
        l10n: { id: "urlbar-result-action-copy-to-clipboard" },
      },
      tail150: {
        style: {
          display: value === "150" && this.#sapName === "urlbar" ? "" : "none",
        },
      },
    };
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   */
  onEngagement(queryContext, controller, details) {
    if (details.selType === "tail150") {
      controller.view.startTail150();
      return;
    }

    const { result } = details;
    const input = this.getViewUpdate(result).input;
    let localizedResult;
    if ("l10n" in input) {
      const args = input.l10n.args || {};
      localizedResult = lazy.l10n.formatValueSync(input.l10n.id, args);
    } else {
      localizedResult = input.textContent.replace(/^=\s*/, "");
    }

    lazy.ClipboardHelper.copyString(localizedResult);
  }
}

/**
 * Base implementation of a basic calculator.
 */
class BaseCalculator {
  // Holds the current symbols for calculation
  stack = [];
  numberSystems = [];

  addNumberSystem(system) {
    this.numberSystems.push(system);
  }

  isNumeric(value) {
    return value - 0 == value && value.length;
  }

  isOperator(value) {
    return this.numberSystems.some(sys => sys.isOperator(value));
  }

  isNumericToken(char) {
    return this.numberSystems.some(sys => sys.isNumericToken(char));
  }

  /**
   * Parses a string into a float accounting for different localisations.
   *
   * @param {string} num
   */
  parsel10nFloat(num) {
    for (const system of this.numberSystems) {
      num = system.transformNumber(num);
    }
    return parseFloat(num);
  }

  precedence(val) {
    if (["-", "+"].includes(val)) {
      return 2;
    }
    if (["*", "/", "÷", "×"].includes(val)) {
      return 3;
    }
    if ("^" === val) {
      return 4;
    }

    return null;
  }

  isLeftAssociative(val) {
    if (["-", "+", "*", "/", "÷", "×"].includes(val)) {
      return true;
    }
    if ("^" === val) {
      return false;
    }

    return null;
  }

  // This is a basic implementation of the shunting yard algorithm
  // described http://en.wikipedia.org/wiki/Shunting-yard_algorithm
  // Currently functions are unimplemented
  infix2postfix(infix) {
    let parser = new Parser(infix, this);
    let tokens = parser.parse();
    let output = [];
    let stack = [];

    tokens.forEach(token => {
      if (token.number) {
        output.push(this.parsel10nFloat(token.value));
      }

      if (this.isOperator(token.value)) {
        let i = this.precedence;
        while (
          stack.length &&
          this.isOperator(stack[stack.length - 1]) &&
          (i(token.value) < i(stack[stack.length - 1]) ||
            (i(token.value) == i(stack[stack.length - 1]) &&
              this.isLeftAssociative(token.value)))
        ) {
          output.push(stack.pop());
        }
        stack.push(token.value);
      }

      if (token.value === "(") {
        stack.push(token.value);
      }

      if (token.value === ")") {
        while (stack.length && stack[stack.length - 1] !== "(") {
          output.push(stack.pop());
        }
        // This is the (
        stack.pop();
      }
    });

    while (stack.length) {
      output.push(stack.pop());
    }
    return output;
  }

  evaluate = {
    "*": (a, b) => a * b,
    "×": (a, b) => a * b,
    "+": (a, b) => a + b,
    "-": (a, b) => a - b,
    "/": (a, b) => a / b,
    "÷": (a, b) => a / b,
    "^": (a, b) => a ** b,
  };

  evaluatePostfix(postfix) {
    let stack = [];

    for (const token of postfix) {
      if (!this.isOperator(token)) {
        stack.push(token);
      } else {
        let op2 = stack.pop();
        let op1 = stack.pop();
        let result = this.evaluate[token](op1, op2);
        if ((token == "/" || token == "÷") && op2 == 0) {
          return UNDEFINED_VALUE;
        }
        if (isNaN(result) || !isFinite(result)) {
          throw new Error("Value is " + result);
        }
        stack.push(result);
      }
    }
    let finalResult = stack.pop();
    if (isNaN(finalResult) || !isFinite(finalResult)) {
      throw new Error("Value is " + finalResult);
    }

    let locale = Services.locale.appLocaleAsBCP47;

    if (
      Math.abs(finalResult) >= FULL_NUMBER_MAX_THRESHOLD ||
      (Math.abs(finalResult) <= FULL_NUMBER_MIN_THRESHOLD && finalResult != 0)
    ) {
      return new Intl.NumberFormat(locale, {
        style: "decimal",
        notation: "scientific",
        minimumFractionDigits: 1,
        maximumFractionDigits: 8,
        numberingSystem: "latn",
      })
        .format(finalResult)
        .toLowerCase();
    } else if (Math.abs(finalResult) < 1) {
      return new Intl.NumberFormat(locale, {
        style: "decimal",
        maximumSignificantDigits: 9,
        numberingSystem: "latn",
      }).format(finalResult);
    }
    return new Intl.NumberFormat(locale, {
      style: "decimal",
      useGrouping: false,
      maximumFractionDigits: 8,
      numberingSystem: "latn",
    }).format(finalResult);
  }
}

function Parser(input, calculator) {
  this.calculator = calculator;
  this.init(input);
}

Parser.prototype = {
  init(input) {
    // No spaces.
    input = input.replace(/[ \t\v\n]/g, "");

    // String to array:
    this._chars = [];
    for (let i = 0; i < input.length; ++i) {
      this._chars.push(input[i]);
    }

    this._tokens = [];
  },

  // This method returns an array of objects with these properties:
  // - number: true/false
  // - value:  the token value
  parse() {
    // The input must be a "block" without any digit left.
    if (!this._tokenizeBlock() || this._chars.length) {
      throw new Error("Wrong input");
    }

    return this._tokens;
  },

  _tokenizeBlock() {
    if (!this._chars.length) {
      return false;
    }

    // "(" + something + ")"
    if (this._chars[0] == "(") {
      this._tokens.push({ number: false, value: this._chars[0] });
      this._chars.shift();

      if (!this._tokenizeBlock()) {
        return false;
      }

      if (!this._chars.length || this._chars[0] != ")") {
        return false;
      }

      this._chars.shift();

      this._tokens.push({ number: false, value: ")" });
    } else if (!this._tokenizeNumber()) {
      // number + ...
      return false;
    }

    if (!this._chars.length || this._chars[0] == ")") {
      return true;
    }

    while (this._chars.length && this._chars[0] != ")") {
      if (!this._tokenizeOther()) {
        return false;
      }

      if (!this._tokenizeBlock()) {
        return false;
      }
    }

    return true;
  },

  // This is a simple float parser.
  _tokenizeNumber() {
    if (!this._chars.length) {
      return false;
    }

    // {+,-}something
    let number = [];
    if (/[+-]/.test(this._chars[0])) {
      number.push(this._chars.shift());
    }

    let tokenizeNumberInternal = () => {
      if (
        !this._chars.length ||
        !this.calculator.isNumericToken(this._chars[0])
      ) {
        return false;
      }

      while (
        this._chars.length &&
        this.calculator.isNumericToken(this._chars[0])
      ) {
        number.push(this._chars.shift());
      }

      return true;
    };

    if (!tokenizeNumberInternal()) {
      return false;
    }

    // 123{e...}
    if (!this._chars.length || this._chars[0] != "e") {
      this._tokens.push({ number: true, value: number.join("") });
      return true;
    }

    number.push(this._chars.shift());

    // 123e{+,-}
    if (/[+-]/.test(this._chars[0])) {
      number.push(this._chars.shift());
    }

    if (!this._chars.length) {
      return false;
    }

    // the number
    if (!tokenizeNumberInternal()) {
      return false;
    }

    this._tokens.push({ number: true, value: number.join("") });
    return true;
  },

  _tokenizeOther() {
    if (!this._chars.length) {
      return false;
    }

    if (this.calculator.isOperator(this._chars[0])) {
      this._tokens.push({ number: false, value: this._chars.shift() });
      return true;
    }

    return false;
  },
};

export let Calculator = new BaseCalculator();

Calculator.addNumberSystem({
  isOperator: char => ["÷", "×", "-", "+", "*", "/", "^"].includes(char),
  isNumericToken: char => /^[0-9\.,]/.test(char),
  /**
   * parseFloat will only handle numbers that use periods as decimal
   * separators, various countries use commas. This function attempts
   * to fixup the number so parseFloat will accept it.
   *
   * @param {string} num
   */
  transformNumber: num => {
    let firstComma = num.indexOf(",");
    let firstPeriod = num.indexOf(".");

    if (firstPeriod != -1 && firstComma != -1 && firstPeriod < firstComma) {
      // Contains both a period and a comma and the period came first
      // so using comma as decimal seperator, strip . and replace , with .
      // (ie 1.999,5).
      num = num.replace(/\./g, "");
      num = num.replace(/,/g, ".");
    } else if (firstPeriod != -1 && firstComma != -1) {
      // Contains both a period and a comma and the comma came first
      // so strip the comma (ie 1,999.5).
      num = num.replace(/,/g, "");
    } else if (firstComma != -1 && num.includes(",", firstComma + 1)) {
      // Contains multiple commas and no periods, strip commas
      num = num.replace(/,/g, "");
    } else if (firstPeriod != -1 && num.includes(".", firstPeriod + 1)) {
      // Contains multiple periods and no commas, strip periods
      num = num.replace(/\./g, "");
    } else if (firstComma != -1) {
      // Has a single comma and no periods, treat comma as decimal seperator
      num = num.replace(/,/g, ".");
    }
    return num;
  },
});
