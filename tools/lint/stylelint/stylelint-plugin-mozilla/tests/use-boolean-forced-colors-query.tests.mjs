/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// Bug 1948378: remove this exception when the eslint import plugin fully
// supports exports in package.json files
// eslint-disable-next-line import-x/no-unresolved
import { testRule } from "stylelint-test-rule-node";
import stylelint from "stylelint";
import useBooleanForcedColorsQuery from "../rules/use-boolean-forced-colors-query.mjs";

let plugin = stylelint.createPlugin(
  useBooleanForcedColorsQuery.ruleName,
  useBooleanForcedColorsQuery
);
let {
  ruleName,
  rule: { messages },
} = plugin;

testRule({
  plugins: [plugin],
  ruleName,
  config: [true],
  fix: true,
  accept: [
    {
      code: "@media (forced-colors) { a { color: red; } }",
      description: "a boolean forced-colors query is allowed",
    },
    {
      code: "@media not (forced-colors) { a { color: red; } }",
      description: "a negated boolean forced-colors query is allowed",
    },
    {
      code: "@media (forced-colors: none) { a { color: red; } }",
      description: "forced-colors: none is allowed",
    },
    {
      code: "@media (forced-colors: requested) { a { color: red; } }",
      description: "forced-colors: requested is allowed",
    },
    {
      code: "@media (prefers-contrast: more) { a { color: red; } }",
      description: "prefers-contrast is not this rule's business",
    },
    {
      code: "@supports (forced-colors: active) { a { color: red; } }",
      description: "only @media is checked",
    },
  ],
  reject: [
    {
      code: "@media (forced-colors: active) { a { color: red; } }",
      fixed: "@media (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "forced-colors: active is fixed to the boolean query",
    },
    {
      code: "@media (forced-colors:active) { a { color: red; } }",
      fixed: "@media (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "forced-colors:active without whitespace is fixed",
    },
    {
      code: "@MEDIA (Forced-Colors: ACTIVE) { a { color: red; } }",
      fixed: "@MEDIA (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "the at-rule, feature and value are matched in any case",
    },
    {
      code: "@media not (forced-colors: active) { a { color: red; } }",
      fixed: "@media not (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "a negated forced-colors: active is fixed",
    },
    {
      code: "@media ((forced-colors: active) and (min-width: 10px)) { a { color: red; } }",
      fixed:
        "@media ((forced-colors) and (min-width: 10px)) { a { color: red; } }",
      message: messages.rejected,
      description: "a feature inside nested parentheses is fixed",
    },
    {
      code: "@media (forced-colors: active) or (prefers-contrast) { a { color: red; } }",
      fixed:
        "@media (forced-colors) or (prefers-contrast) { a { color: red; } }",
      message: messages.rejected,
      description: "a feature in an or condition is fixed",
    },
    {
      code: "@media (forced-colors: active) and (prefers-reduced-motion: no-preference), (forced-colors: active) and (min-width: 10px) { a { color: red; } }",
      fixed:
        "@media (forced-colors) and (prefers-reduced-motion: no-preference), (forced-colors) and (min-width: 10px) { a { color: red; } }",
      message: messages.rejected,
      description: "every forced-colors: active in a query list is fixed",
    },
    {
      code: "a { @media (forced-colors: active) { color: red; } }",
      fixed: "a { @media (forced-colors) { color: red; } }",
      message: messages.rejected,
      description: "a nested @media is checked",
    },
    {
      code: "@media /* Windows chrome */ (forced-colors: active) { a { color: red; } }",
      fixed:
        "@media /* Windows chrome */ (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "a comment in the prelude is kept",
    },
    {
      code: "@media (forced-colors /* Windows chrome */: active) { a { color: red; } }",
      fixed: "@media (forced-colors) { a { color: red; } }",
      message: messages.rejected,
      description: "a comment inside the feature is dropped",
    },
  ],
});

testRule({
  plugins: [plugin],
  ruleName,
  config: [true],
  fix: true,
  customSyntax: "postcss-scss",
  reject: [
    {
      code: ".a {\n  // Windows chrome\n  @media (forced-colors: active) {\n    color: $red;\n  }\n}",
      fixed:
        ".a {\n  // Windows chrome\n  @media (forced-colors) {\n    color: $red;\n  }\n}",
      message: messages.rejected,
      description: "SCSS through postcss-scss is fixed",
    },
  ],
});
