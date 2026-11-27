/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import stylelint from "stylelint";
import { namespace } from "../helpers.mjs";

const {
  utils: { report, ruleMessages, validateOptions },
} = stylelint;

const ruleName = namespace("use-boolean-forced-colors-query");

const messages = ruleMessages(ruleName, {
  rejected:
    'Use "(forced-colors)" instead of "(forced-colors: active)". Under ' +
    'Windows high contrast mode, browser chrome is in the "requested" state, ' +
    "which only the boolean query matches.",
});

const meta = {
  url: "https://firefox-source-docs.mozilla.org/code-quality/lint/linters/stylelint-plugin-mozilla/rules/use-boolean-forced-colors-query.html",
  fixable: true,
};

const FORCED_COLORS_ACTIVE = /\(\s*forced-colors\s*:\s*active\s*\)/i;

const toBoolean = params =>
  params.replace(new RegExp(FORCED_COLORS_ACTIVE, "gi"), "(forced-colors)");

const ruleFunction = primaryOption => {
  return (root, result) => {
    const validOptions = validateOptions(result, ruleName, {
      actual: primaryOption,
      possible: [true],
    });

    if (!validOptions) {
      return;
    }

    root.walkAtRules(/^media$/i, atRule => {
      if (!FORCED_COLORS_ACTIVE.test(atRule.params)) {
        return;
      }
      report({
        message: messages.rejected,
        node: atRule,
        result,
        ruleName,
        fix: () => {
          // PostCSS keeps a prelude with comments in raws.params, and drops it
          // once params changes, so rewrite the raw text too. A comment inside
          // the feature itself defeats the pattern, and is dropped.
          let raw = atRule.raws.params?.raw;
          atRule.params = toBoolean(atRule.params);
          if (raw && toBoolean(raw) != raw) {
            atRule.raws.params = { value: atRule.params, raw: toBoolean(raw) };
          } else {
            delete atRule.raws.params;
          }
        },
      });
    });
  };
};

ruleFunction.ruleName = ruleName;
ruleFunction.messages = messages;
ruleFunction.meta = meta;

export default ruleFunction;
