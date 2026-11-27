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
import noUnguardedHighContrastTokenOverrides from "../rules/no-unguarded-high-contrast-token-overrides.mjs";

let plugin = stylelint.createPlugin(
  noUnguardedHighContrastTokenOverrides.ruleName,
  noUnguardedHighContrastTokenOverrides
);
let {
  ruleName,
  rule: { messages },
} = plugin;

// --button-background-color has a forced-colors value only, --text-color a
// prefers-contrast value only, and --button-border-color both.
const forcedColors = tokenName =>
  messages.rejected(tokenName, "forced-colors", "not (forced-colors)");
const prefersContrast = tokenName =>
  messages.rejected(tokenName, "prefers-contrast", "not (prefers-contrast)");
const both = tokenName =>
  messages.rejected(
    tokenName,
    "forced-colors and prefers-contrast",
    "not (prefers-contrast)"
  );

testRule({
  plugins: [plugin],
  ruleName,
  config: [true],
  fix: false,
  accept: [
    {
      code: ":root { --button-background-color: var(--button-background-color-hover); }",
      description: "a reference to a token with the same high-contrast values",
    },
    {
      code: ":root { --button-background-color: var(--button-background-color-ghost-hover); }",
      description:
        "a reference to a token whose own value reads a token with those values",
    },
    {
      code: ":root { --button-background-color: var(--my-local-color); }",
      description:
        "a local custom property cannot be judged and is not reported",
    },
    {
      code: ":root { --space-small: 4px; --my-color: #fff; }",
      description: "a token without high-contrast values is not checked",
    },
    {
      code: ":root { --button-background-color: ButtonFace; --panel-background-color: Menu; --text-color: inherit; }",
      description: "a system color and a CSS-wide keyword are not literals",
    },
    {
      code: ".a { @media not (forced-colors: active) { --button-background-color: #fff; } }",
      description: "not (forced-colors: active) excludes forced colors",
    },
    {
      code: ".a { @media not (forced-colors) { --button-background-color: #fff; } }",
      description: "a forced-colors token under not (forced-colors)",
    },
    {
      code: ".a { @media (forced-colors: none) { --button-background-color: #fff; } }",
      description: "a forced-colors token under (forced-colors: none)",
    },
    {
      code: ".a { @media not (prefers-contrast) { --text-color: #000; } }",
      description: "a prefers-contrast token under not (prefers-contrast)",
    },
    {
      code: ".a { @media (prefers-contrast: no-preference) { --text-color: #000; } }",
      description:
        "a prefers-contrast token under (prefers-contrast: no-preference)",
    },
    {
      code: "@media not (prefers-contrast) { :root { --button-border-color: #fff; } }",
      description:
        "a token with both values under the prefers-contrast guard, since forced colors imply prefers-contrast",
    },
    {
      code: "@media not (prefers-contrast) { :root { --button-background-color: #fff; } }",
      description:
        "a prefers-contrast guard also excludes forced colors for a forced-colors token",
    },
    {
      code: ".a { @media (prefers-contrast: no-preference) { --button-border-color: #fff; } }",
      description:
        "a token with both values under the equivalent prefers-contrast form",
    },
    {
      code: "@media not ((forced-colors) or (prefers-contrast)) { :root { --button-border-color: #fff; } }",
      description: "a token with both values under the combined guard",
    },
    {
      code: "@media (forced-colors: none) and (prefers-contrast: no-preference) { :root { --button-border-color: #fff; } }",
      description: "a token with both values under the equivalent and form",
    },
    {
      code: ".a { @media not (forced-colors) { @media not (prefers-contrast) { --button-border-color: #fff; } } }",
      description: "a token with both values under nested guards",
    },
    {
      code: "@media -moz-pref('browser.nova.enabled') and (not ((forced-colors) or (prefers-contrast))) { .a { --button-border-color: #fff; } }",
      description: "a guard combined with another condition",
    },
    {
      code: "@media (width >= 600px) and (not ((forced-colors) or (prefers-contrast))) { .a { --button-border-color: #fff; } }",
      description: "a guard combined with a range feature",
    },
    {
      code: "@media screen and (400px < width < 800px) and (not (prefers-contrast)) { .a { --button-border-color: #fff; } }",
      description: "a guard combined with a media type and a two-sided range",
    },
    {
      code: "@media (forced-colors) { :root { --button-background-color: ButtonFace; --button-border-color: #fff; } }",
      description: "a forced-colors block is deliberate high-contrast styling",
    },
    {
      code: "@media (prefers-contrast) { :root { --text-color: #000; } }",
      description:
        "a prefers-contrast block is deliberate high-contrast styling",
    },
    {
      code: "@media (forced-colors) { :root { --text-color: #000; } }",
      description:
        "a forced-colors block is deliberate high-contrast styling for a prefers-contrast token, since forced colors imply prefers-contrast",
    },
    {
      code: ":root[lwtheme] { --button-border-color: #fff; }",
      description: "an lwtheme rule needs no guard",
    },
    {
      code: ":root { &[lwtheme]:not([theme-in-app]) { --text-color: #000; } }",
      description: "a nested lwtheme rule needs no guard",
    },
    {
      code: ":is(:root[lwtheme], :root[lwtheme] .b) { --button-border-color: #fff; }",
      description: "a comma inside :is() does not split the selector list",
    },
    {
      code: "@layer tokens-foundation { :root { --button-border-color: #fff; } }",
      description: "a tokens-* layer is not a consumer override",
    },
    {
      code: ".a { --button-background-color: #fff; }",
      codeFilename: "toolkit/content/widgets/moz-button/moz-button.tokens.css",
      description: "a generated token sheet is not checked",
    },
  ],
  reject: [
    {
      code: ".a { --button-background-color: #fff; }",
      message: forcedColors("--button-background-color"),
      description: "a hex color",
    },
    {
      code: ".a { --link-color: white; }",
      message: forcedColors("--link-color"),
      description: "a named color",
    },
    {
      code: ".a { --link-color: rgb(0 0 255); }",
      message: forcedColors("--link-color"),
      description: "a color function",
    },
    {
      code: ".a { --badge-background-color-filled: var(--color-green-40); }",
      message: forcedColors("--badge-background-color-filled"),
      description: "a base color token",
    },
    {
      code: ".a { --button-background-color: var(--color-white-alpha-20); }",
      message: forcedColors("--button-background-color"),
      description: "an alpha base color token",
    },
    {
      code: ".a { --link-color: light-dark(var(--color-blue-60), var(--link-color-base)); }",
      message: forcedColors("--link-color"),
      description: "a literal inside light-dark()",
    },
    {
      code: ".a { --focus-outline-color: color-mix(in srgb, var(--color-violet-50) 15%, transparent); }",
      message: forcedColors("--focus-outline-color"),
      description: "a literal inside color-mix()",
    },
    {
      code: ".a { --text-color: #000; }",
      message: prefersContrast("--text-color"),
      description: "an unguarded prefers-contrast token",
    },
    {
      code: ".a { --button-border-color: #fff; }",
      message: both("--button-border-color"),
      description: "an unguarded token with both values",
    },
    {
      code: ".a { @media not (forced-colors) { --button-border-color: #fff; } }",
      message: prefersContrast("--button-border-color"),
      description: "a forced-colors guard on a token with both values",
    },
    {
      code: ".a { @media (forced-colors: none) { --text-color: #000; } }",
      message: prefersContrast("--text-color"),
      description: "a forced-colors guard on a prefers-contrast token",
    },
    {
      code: "@media (prefers-contrast) { :root { --button-background-color: #fff; } }",
      message: forcedColors("--button-background-color"),
      description:
        "a prefers-contrast block can apply under forced colors, which a forced-colors token needs excluded",
    },
    {
      code: "@media (prefers-contrast) { :root { --button-border-color: #fff; } }",
      message: forcedColors("--button-border-color"),
      description:
        "a prefers-contrast block can apply under forced colors, which a token with both values needs excluded",
    },
    {
      code: ":root:not([lwtheme]) { @media not (forced-colors) { --button-border-color: #fff; } }",
      message: prefersContrast("--button-border-color"),
      description: "a negated lwtheme selector is not the exemption",
    },
    {
      code: "@layer page { :root { --button-background-color: #fff; } }",
      message: forcedColors("--button-background-color"),
      description: "a consumer's own layer is not a guard",
    },
    {
      code: ":root { --button-opacity-disabled: 0.4; }",
      message: forcedColors("--button-opacity-disabled"),
      description: "a literal of a non-color token",
    },
    {
      code: ":root { --button-background-color: transparent; }",
      message: forcedColors("--button-background-color"),
      description: "transparent hides the token's value",
    },
    {
      code: ".a { --button-background-color: color-mix(in srgb, currentColor 10%, transparent); }",
      message: forcedColors("--button-background-color"),
      description: "a color-mix of currentColor hides the token's value",
    },
    {
      code: ".a { --button-background-color: var(--accent, #fff); }",
      message: forcedColors("--button-background-color"),
      description: "a literal in a var() fallback",
    },
    {
      code: ".a { --button-background-color: var(--toolbar-field-border-color-focus); }",
      message: forcedColors("--button-background-color"),
      description:
        "a token computed from a forced-colors token is not that token's value",
    },
    {
      code: ":root:not(:not([lwtheme])) { @media not (forced-colors) { --button-border-color: #fff; } }",
      message: prefersContrast("--button-border-color"),
      description: "a double negation of lwtheme keeps the strict check",
    },
    {
      code: ":root:not(:is([lwtheme])) { @media not (forced-colors) { --button-border-color: #fff; } }",
      message: prefersContrast("--button-border-color"),
      description: "lwtheme inside :is() inside :not() is not the exemption",
    },
    {
      code: ":root:not(:is([lwtheme], .a)), :root[lwtheme] { @media not (forced-colors) { --button-border-color: #fff; } }",
      message: prefersContrast("--button-border-color"),
      description:
        "a selector list with a negated lwtheme branch is not the exemption",
    },
    {
      code: "@media (width >= 600px) { .a { --button-border-color: #fff; } }",
      message: both("--button-border-color"),
      description: "a range feature is not a guard",
    },
    {
      code: ":root { --button-background-color: var(--text-color); }",
      message: forcedColors("--button-background-color"),
      description: "a reference to a token without a forced-colors value",
    },
    {
      code: ":root { --button-background-color: var(--button-background-color-hover, var(--text-color)); }",
      message: forcedColors("--button-background-color"),
      description: "a fallback to a token without a forced-colors value",
    },
    {
      code: ":root { --button-border-color: var(--button-background-color); }",
      message: both("--button-border-color"),
      description: "a reference to a token with only one of the two values",
    },
  ],
});

testRule({
  plugins: [plugin],
  ruleName,
  config: [true],
  fix: true,
  accept: [
    {
      code: ".a {\n  @media not (forced-colors) {\n    --button-background-color: #fff;\n  }\n}",
      description: "a fixed declaration is not reported again",
    },
    {
      code: ".a {\n  /* stylelint-disable-next-line stylelint-plugin-mozilla/no-unguarded-high-contrast-token-overrides */\n  --button-background-color: #fff;\n}",
      description: "a disabled declaration is neither reported nor fixed",
    },
    {
      code: ".a {\n  @media not (forced-colors: active) {\n    --button-background-color: #fff;\n  }\n}",
      description:
        "the fix nests no second guard in not (forced-colors: active)",
    },
  ],
  reject: [
    {
      code: ".a {\n  color: red;\n  --button-background-color: #fff;\n}",
      fixed:
        ".a {\n  color: red;\n  @media not (forced-colors) {\n    --button-background-color: #fff;\n  }\n}",
      message: forcedColors("--button-background-color"),
      description: "a forced-colors token gets the forced-colors guard",
    },
    {
      code: ".a {\n  --text-color: #000;\n}",
      fixed:
        ".a {\n  @media not (prefers-contrast) {\n    --text-color: #000;\n  }\n}",
      message: prefersContrast("--text-color"),
      description: "a prefers-contrast token gets the prefers-contrast guard",
    },
    {
      code: ".a {\n  --button-border-color: #fff;\n}",
      fixed:
        ".a {\n  @media not (prefers-contrast) {\n    --button-border-color: #fff;\n  }\n}",
      message: both("--button-border-color"),
      description: "a token with both values gets the prefers-contrast guard",
    },
    {
      code: ".a { --button-background-color: #fff; }",
      fixed:
        ".a { @media not (forced-colors) { --button-background-color: #fff; } }",
      message: forcedColors("--button-background-color"),
      description: "a single-line block stays on one line",
    },
    {
      code: ".a {\n  --button-background-color: #fff;\n  --button-background-color-hover: #eee;\n  color: red;\n}",
      fixed:
        ".a {\n  @media not (forced-colors) {\n    --button-background-color: #fff;\n    --button-background-color-hover: #eee;\n  }\n  color: red;\n}",
      warnings: [
        { message: forcedColors("--button-background-color") },
        { message: forcedColors("--button-background-color-hover") },
      ],
      description: "adjacent declarations that need the same guard share it",
    },
    {
      code: ".a {\n  --text-color: light-dark(\n    #000,\n    #fff\n  );\n}",
      fixed:
        ".a {\n  @media not (prefers-contrast) {\n    --text-color: light-dark(\n      #000,\n      #fff\n    );\n  }\n}",
      message: prefersContrast("--text-color"),
      description: "a multi-line value is reindented",
    },
    {
      code: ".a {\n  --text-color: light-dark(\n    #000, /* dark */\n    #fff\n  );\n}",
      fixed:
        ".a {\n  @media not (prefers-contrast) {\n    --text-color: light-dark(\n      #000, /* dark */\n      #fff\n    );\n  }\n}",
      message: prefersContrast("--text-color"),
      description: "a comment inside a multi-line value is kept",
    },
    {
      code: ".a {\n  @media (prefers-contrast) {\n    --button-border-color: #fff;\n  }\n}",
      fixed:
        ".a {\n  @media (prefers-contrast) {\n    @media not (forced-colors) {\n      --button-border-color: #fff;\n    }\n  }\n}",
      message: forcedColors("--button-border-color"),
      description:
        "a token with both values in a prefers-contrast block gets the forced-colors guard",
    },
    {
      code: ".a {\n  --button-background-color: #fff; /* why */\n}",
      unfixable: true,
      message: forcedColors("--button-background-color"),
      description: "a declaration with a trailing comment is not moved",
    },
  ],
});

testRule({
  plugins: [plugin],
  ruleName,
  config: [true],
  fix: true,
  customSyntax: "postcss-scss",
  codeFilename: "browser/extensions/newtab/content-src/styles/_widget.scss",
  reject: [
    {
      code: ".a {\n  --button-background-color: #fff;\n}",
      fixed:
        ".a {\n  @media (forced-colors: none) {\n    --button-background-color: #fff;\n  }\n}",
      message: messages.rejected(
        "--button-background-color",
        "forced-colors",
        "(forced-colors: none)"
      ),
      description: "New Tab SCSS gets the Sass-safe forced-colors guard",
    },
    {
      code: ".a {\n  --text-color: #000;\n}",
      fixed:
        ".a {\n  @media (prefers-contrast: no-preference) {\n    --text-color: #000;\n  }\n}",
      message: messages.rejected(
        "--text-color",
        "prefers-contrast",
        "(prefers-contrast: no-preference)"
      ),
      description: "New Tab SCSS gets the Sass-safe prefers-contrast guard",
    },
    {
      code: ".a {\n  --button-border-color: #fff;\n}",
      fixed:
        ".a {\n  @media (prefers-contrast: no-preference) {\n    --button-border-color: #fff;\n  }\n}",
      message: messages.rejected(
        "--button-border-color",
        "forced-colors and prefers-contrast",
        "(prefers-contrast: no-preference)"
      ),
      description:
        "New Tab SCSS gets the Sass-safe prefers-contrast guard for a token with both values",
    },
  ],
});

testRule({
  plugins: [plugin],
  ruleName,
  config: [true, { ignoreForcedColors: true }],
  fix: false,
  accept: [
    {
      code: ".a { --button-background-color: #fff; }",
      description:
        "ignoreForcedColors skips a token with only a forced-colors value",
    },
    {
      code: ".a { @media not (prefers-contrast) { --button-border-color: #fff; } }",
      description:
        "ignoreForcedColors accepts the prefers-contrast guard for a token with both values",
    },
  ],
  reject: [
    {
      code: ".a { --text-color: #000; }",
      message: prefersContrast("--text-color"),
      description:
        "ignoreForcedColors still reports a prefers-contrast token without a guard",
    },
    {
      code: ".a { --button-border-color: #fff; }",
      message: prefersContrast("--button-border-color"),
      description:
        "ignoreForcedColors reports a token with both values for its prefers-contrast value",
    },
  ],
});
