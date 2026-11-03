/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const fs = require("fs");
const path = require("path");
const rollouts = process.env.STYLELINT_SKIP_ROLLOUTS
  ? []
  : require("./stylelint-rollouts.config");

function readFile(filePath) {
  return fs
    .readFileSync(filePath, { encoding: "utf-8" })
    .split("\n")
    .filter(p => p && !p.startsWith("#"));
}

const ignoreFiles = [
  ...readFile(
    path.join(__dirname, "tools", "rewriting", "ThirdPartyPaths.txt")
  ),
  ...readFile(path.join(__dirname, "tools", "rewriting", "Generated.txt")),
];

module.exports = {
  extends: ["stylelint-config-recommended"],
  plugins: [
    "./tools/lint/stylelint/stylelint-plugin-mozilla/index.mjs",
    "@stylistic/stylelint-plugin",
    "stylelint-use-logical",
  ],
  ignoreFiles,
  reportNeedlessDisables: true,
  reportUnscopedDisables: true,
  rules: {
    /* Disabled because of `-moz-element(#foo)` which gets misparsed. */
    "color-no-invalid-hex": null,
    "font-family-no-missing-generic-family-keyword": [
      true,
      {
        ignoreFontFamilies: [
          "-moz-button",
          "-moz-field",
          "-moz-fixed",
          "-moz-list",
          "caption",
        ],
      },
    ],

    "function-no-unknown": [
      true,
      {
        ignoreFunctions: [
          "light-dark" /* Used for color-scheme dependent colors */,
          "add" /* Used in mathml.css */,
          "-moz-symbolic-icon" /* Used for GTK icons */,
        ],
      },
    ],

    "length-zero-no-unit": [
      true,
      {
        ignore: ["custom-properties"],
      },
    ],

    "max-nesting-depth": [
      3,
      {
        ignore: ["blockless-at-rules"],
      },
    ],

    "no-descending-specificity": null,
    "no-duplicate-selectors": null,

    "property-no-unknown": [
      true,
      {
        ignoreProperties: [
          // overflow-clip-box is Gecko-specific and not exposed to web
          // content. Might be replaced with overflow-clip-margin, see:
          // https://github.com/w3c/csswg-drafts/issues/10745
          "overflow-clip-box",
          "overflow-clip-box-block",
          "overflow-clip-box-inline",
        ],
      },
    ],

    /*
     * XXXgijs: we would like to enable this, but we can't right now.
     * This is because Gecko uses a number of custom pseudoclasses,
     * and stylelint assumes that for `:unknown-pseudoclass(foo)`,
     * `foo` should be a known type.
     * This is tedious but workable for things like `-moz-locale-dir` where
     * the set of acceptable values (ltr/rtl) is small.
     * However, for tree cells, the set of values is unlimited (ie
     * user-defined, based on atoms sent by the JS tree view APIs).
     * There does not appear to be a way to exempt the contents of these
     * unknown pseudoclasses, and as a result, this rule is not
     * usable for us. The 'type' only includes the contents of the
     * pseudoclass, not the pseudo itself, so we can't filter based on the
     * pseudoclass either.
     * Ideally, we would either create an option to the builtin rule
     * in stylelint itself, or mimic the rule but exempt these, or
     * add parser support for our custom pseudoclasses.
     *
     * For now, we just disable this rule.
     */
    "selector-type-no-unknown": null,
    /*
     * See above - if we enabled this rule, we'd have to allow for a number
     * of custom elements we use, which are listed here:
    "selector-type-no-unknown": [
      true,
      {
        ignore: ["custom-elements"],
        ignoreTypes: [
          // Modern custom element / storybooked components:
          /^moz-/,
          // moz-locale-dir trips this rule for some reason:
          "rtl",
          "ltr",
          // Migrated XBL elements not part of core XUL that we use at the moment:
          "findbar",
          "panelmultiview",
          "panelview",
          "popupnotification",
          "popupnotificationcontent",
          // Legacy XUL elements:
          // (the commented out ones used to be a thing and aren't used in-tree anymore)
          "arrowscrollbox",
          "box",
          // "broadcaster",
          // "broadcasterset",
          "button",
          "browser",
          "checkbox",
          "caption",
          // clicktoscroll
          // colorpicker
          // column
          // columns
          "commandset",
          "command",
          // conditions
          // content
          // datepicker
          "deck",
          "description",
          "dialog",
          // dialogheader
          "dropmarker",
          "editor",
          // grid
          // grippy
          "groupbox",
          "hbox",
          // iframe
          // image
          "key",
          "keyset",
          // label
          "listbox",
          // listcell
          // listcol
          // listcols
          // listhead
          // listheader
          "listitem",
          // member
          "menu",
          "menubar",
          "menucaption",
          "menuitem",
          "menulist",
          "menupopup",
          "menuseparator",
          "notification",
          "notificationbox",
          "observes",
          // overlay
          // page
          "panel",
          // param
          "popupset",
          // preference
          // preferences
          // prefpane
          // prefwindow
          // progressmeter
          // query
          // queryset
          "radio",
          "radiogroup",
          // resizer
          "richlistbox",
          "richlistitem",
          // row
          // rows
          // rule
          // scale
          // script
          "scrollbar",
          "scrollbox",
          "scrollcorner",
          "separator",
          "spacer",
          // spinbuttons
          "splitter",
          "stack",
          // statusbar
          // statusbarpanel
          "stringbundle",
          "stringbundleset",
          "tab",
          "tabbox",
          "tabpanel",
          "tabpanels",
          "tabs",
          // template
          // textnode
          "textbox",
          // timepicker
          "titlebar",
          "toolbar",
          "toolbarbutton",
          // toolbargrippy
          "toolbaritem",
          "toolbarpalette",
          "toolbarpaletteitem",
          "toolbarseparator",
          "toolbarset",
          "toolbarspacer",
          "toolbarspring",
          "toolbartabstop",
          "toolbox",
          "tooltip",
          "tree",
          "treecell",
          "treechildren",
          "treecol",
          "treecols",
          "treeitem",
          "treerow",
          "treeseparator",
          // triple
          "vbox",
          // where
          "window",
          "wizard",
          "wizardpage",
        ],
      },
    ],
    */

    "selector-pseudo-class-no-unknown": [
      true,
      {
        ignorePseudoClasses: ["popover-open"],
      },
    ],
    "selector-pseudo-element-no-unknown": [
      true,
      {
        ignorePseudoElements: ["slider-track", "slider-fill", "slider-thumb"],
      },
    ],
    // stylelint fixes for the use-logical rule will be addressed in Bug 1996168
    // Remove this line setting `csscontrols/use-logical` to null after implementing fixes
    "csstools/use-logical": null,
    // Use our fork that recognises -moz-pref(...) as valid (bug 2038250).
    // Upstream's ignoreFunctions option short-circuits the entire
    // query when -moz-pref appears anywhere in it, which would mask
    // unrelated errors in the same query.
    "media-query-no-invalid": null,
    "stylelint-plugin-mozilla/media-query-no-invalid": true,
    "stylelint-plugin-mozilla/no-base-design-tokens": true,
    "stylelint-plugin-mozilla/no-has-selector": true,
    "stylelint-plugin-mozilla/use-design-tokens": true,
    "stylelint-plugin-mozilla/use-paired-color-tokens": true,
  },

  overrides: [
    {
      files: "*.scss",
      customSyntax: "postcss-scss",
      extends: "stylelint-config-recommended-scss",
      rules: {
        // stylelint-config-recommended-scss disables the upstream
        // `media-query-no-invalid` rule for SCSS; mirror that for our
        // fork (bug 2038250) since SCSS variables/interpolations would
        // otherwise be flagged as invalid features.
        "stylelint-plugin-mozilla/media-query-no-invalid": null,
      },
    },
    {
      files: [
        "browser/components/aboutwelcome/**",
        "browser/components/asrouter/**",
        "browser/extensions/newtab/**",
      ],
      customSyntax: "postcss-scss",
      extends: "stylelint-config-standard-scss",
      rules: {
        "@stylistic/color-hex-case": "upper",
        "@stylistic/indentation": 2,
        "@stylistic/no-eol-whitespace": true,
        "@stylistic/no-missing-end-of-source-newline": true,
        "@stylistic/number-leading-zero": "always",
        "@stylistic/number-no-trailing-zeros": true,
        "@stylistic/string-quotes": [
          "single",
          {
            avoidEscape: true,
          },
        ],
        "at-rule-disallowed-list": [
          ["debug", "warn", "error"],
          {
            message: "Clean up %s directives before committing",
          },
        ],
        "at-rule-no-vendor-prefix": null,
        "color-function-notation": null,
        "comment-empty-line-before": [
          "always",
          {
            except: ["first-nested"],
            ignore: ["after-comment", "stylelint-commands"],
          },
        ],
        "custom-property-empty-line-before": null,
        "custom-property-pattern": null,
        "declaration-block-no-duplicate-properties": true,
        "declaration-block-no-redundant-longhand-properties": null,
        "declaration-no-important": true,
        "function-no-unknown": [
          true,
          {
            ignoreFunctions: ["div"],
          },
        ],
        "function-url-no-scheme-relative": true,
        "keyframes-name-pattern": null,
        "media-feature-name-no-vendor-prefix": null,
        "no-descending-specificity": null,
        "property-disallowed-list": [
          ["margin-left", "margin-right"],
          {
            message: "Use margin-inline instead of %s",
          },
        ],
        "property-no-unknown": true,
        "property-no-vendor-prefix": null,
        "scss/dollar-variable-empty-line-before": null,
        "scss/double-slash-comment-empty-line-before": [
          "always",
          {
            except: ["first-nested"],
            ignore: ["between-comments", "stylelint-commands", "inside-block"],
          },
        ],
        "selector-class-pattern": null,
        "selector-no-vendor-prefix": null,
        "value-keyword-case": null,
        "value-no-vendor-prefix": null,
      },
    },
    {
      files: ["browser/extensions/newtab/**"],
      rules: {
        "declaration-property-value-disallowed-list": [
          {
            "font-size": [
              "/^[0-9.]+(px|em|rem|%)$/",
              "/^[0-9.]+$/",
              "/^(small|medium|large|x-large|xx-large)$/",
            ],
            "border-radius": [
              "/^[0-9.]+(px|em|rem|%)$/",
              "/^(small|medium|large|x-large|xx-large)$/",
            ],
            // Validate to only allow variables and global values
            "font-weight": [
              "/^(?!var\\(|inherit$|initial$|unset$|revert$|revert-layer$).+$/",
            ],
            [/^(margin|padding|inset|gap|row-gap|column-gap|grid-row-gap|grid-column-gap|top|right|bottom|left)($|-)/]:
              ["/[0-9.]+(px|em|rem)|\\$/"],
          },
          {
            message:
              "Avoid literal values. Use variables (e.g. var(--font-size-small)) or inherit/unset/etc.",
          },
        ],
        "csstools/use-logical": [
          "always",
          {
            // Bug 2003301: Do not enforce logical properties for any height/width properties
            except: [/^(min-|max-)?width/i, /^(min-|max-)?height/i],
            severity: "error",
          },
        ],
      },
    },
    {
      name: "design-token-rules-off",
      files: [
        // CSS files under browser/branding do not use design tokens
        "browser/branding/**",
        // CSS files under browser/components/extensions are not using design tokens
        "browser/components/extensions/**",
        // Webcompat interventions are not expected to use design tokens
        // They are intended to override existing styles for specific extension
        "browser/extensions/webcompat/injections/css/**",
        // Most of devtools does not use design tokens, so turn the appropriate rules off for devtools.
        // Stylelint does not support negating the file glob within an overrides section,
        // so these rules get re-enabled for some devtools files in the section below
        "devtools/**",
        // Android does not use design tokens
        "mobile/android/**",
        "toolkit/themes/mobile/global/aboutLicense.css",
        // Docs do not use design tokens
        "docs/**",
        // DOM does not use design tokens
        "dom/**",
        // Layouts do not use design tokens
        "layout/**",
        // Testing does not use design tokens
        "testing/**",
        "**/test/**",
        "**/tests/**",
        // UA Widgets should not use design tokens
        "toolkit/themes/shared/colorpicker-common.css",
        "toolkit/themes/shared/colorpicker.css",
        "toolkit/themes/shared/media/pipToggle.css",
        "toolkit/themes/shared/media/videocontrols.css",
        "toolkit/content/widgets/datetimebox.css",
        "toolkit/content/widgets/marquee.css",
        "toolkit/themes/shared/media/textrecognition.css",
        // The contents of backup/content/archive.css are injected as inline CSS
        // into the HTML backup archive files that exist on a user's file system
        // and can be opened in any browser.
        "browser/components/backup/content/archive.css",
        // Bug 2003877 - this is a centralization of a bunch of rules that had
        // been spread across about:newtab and about:privatebrowsing. We'll
        // fix these design tokens issues in a follow-up (presuming the
        // replacement of the handoff bar doesn't land and remove this first).
        "browser/components/search/content/contentSearchHandoffUI.css",
      ],
      rules: {
        "stylelint-plugin-mozilla/use-design-tokens": null,
      },
    },
    {
      name: "design-token-rules-on",
      files: [
        // Enable design token related rules only on the parts of devtools that can use them
        "devtools/client/aboutdebugging/src/**",
      ],
      rules: {
        "stylelint-plugin-mozilla/use-design-tokens": true,
      },
    },
    {
      files: ["toolkit/**/*.css", "toolkit/**/*.scss"],
      rules: {
        "stylelint-plugin-mozilla/no-browser-refs-in-toolkit": true,
      },
    },
    {
      // These share a build and a large amount of styles, and use :has() too
      // extensively to disable it inline for now.
      name: "has-selector-rule-off",
      files: [
        "browser/components/aboutwelcome/**",
        "browser/components/asrouter/**",
        "browser/extensions/newtab/**",
      ],
      rules: {
        "stylelint-plugin-mozilla/no-has-selector": null,
      },
    },
    {
      // non-logical properties make sense in devtools/ where physical positioning always makes sense
      name: "logical-properties-rule-off",
      files: ["devtools/**"],
      rules: {
        "csstools/use-logical": null,
      },
    },
    // Rollouts should always be applied last in the overrides section
    // to ensure that they take precedence over other overrides.
    ...rollouts,
  ],
};
