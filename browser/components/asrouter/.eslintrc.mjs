/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { importX } from "eslint-plugin-import-x";
import jsxA11y from "eslint-plugin-jsx-a11y";
import react from "eslint-plugin-react";
import globals from "globals";

export default [
  {
    ...jsxA11y.flatConfigs.recommended,
    // When adding items to this file please check for effects on sub-directories.
    plugins: { "import-x": importX, react, "jsx-a11y": jsxA11y },
    settings: {
      react: {
        version: "16.2.0",
      },
    },
    rules: {
      "react/jsx-boolean-value": ["error", "always"],
      "react/jsx-key": "error",
      "react/jsx-no-bind": [
        "error",
        { allowArrowFunctions: true, allowFunctions: true },
      ],
      "react/jsx-no-comment-textnodes": "error",
      "react/jsx-no-duplicate-props": "error",
      "react/jsx-no-target-blank": "error",
      "react/jsx-no-undef": "error",
      "react/jsx-pascal-case": "error",
      "react/jsx-uses-react": "error",
      "react/jsx-uses-vars": "error",
      "react/no-access-state-in-setstate": "error",
      "react/no-danger": "error",
      "react/no-deprecated": "error",
      "react/no-did-mount-set-state": "error",
      "react/no-did-update-set-state": "error",
      "react/no-direct-mutation-state": "error",
      "react/no-is-mounted": "error",
      "react/no-unknown-property": [
        "error",
        {
          // Custom HTML attributes used in aboutwelcome React components.
          ignore: [
            "flow",
            "alignment",
            "button-size",
            "layout",
            "pos",
            "hide-secondary-section",
            "reverse-split",
            "no-rdm",
            "above-button",
            "fullscreen",
            "narrow",
            "srcset",
          ],
        },
      ],
      "react/require-render-return": "error",

      "accessor-pairs": [
        "error",
        { setWithoutGet: true, getWithoutSet: false },
      ],
      "array-callback-return": "error",
      "block-scoped-var": "error",
      "consistent-this": ["error", "use-bind"],
      eqeqeq: "error",
      "func-name-matching": "error",
      "guard-for-in": "error",
      "max-nested-callbacks": ["error", 4],
      "max-params": ["error", 6],
      "new-cap": ["error", { newIsCap: true, capIsNew: false }],
      "no-alert": "error",
      "no-div-regex": "error",
      "no-duplicate-imports": "error",
      "no-eq-null": "error",
      "no-extend-native": "error",
      "no-extra-label": "error",
      "no-implicit-coercion": ["error", { allow: ["!!"] }],
      "no-implicit-globals": "error",
      "no-loop-func": "error",
      "no-multi-assign": "error",
      "no-multi-str": "error",
      "no-new": "error",
      "no-new-func": "error",
      "no-octal-escape": "error",
      "no-param-reassign": "error",
      "no-proto": "error",
      "no-prototype-builtins": "error",
      "no-return-assign": ["error", "except-parens"],
      "no-script-url": "error",
      "no-template-curly-in-string": "error",
      "no-undef-init": "error",
      "no-unmodified-loop-condition": "error",
      "no-unused-expressions": "error",
      "no-use-before-define": "error",
      "no-useless-computed-key": "error",
      "no-useless-constructor": "error",
      "no-useless-rename": "error",
      "no-var": "error",
      "no-void": ["error", { allowAsStatement: true }],
      "one-var": ["error", "never"],
      "operator-assignment": ["error", "always"],
      "prefer-destructuring": [
        "error",
        {
          AssignmentExpression: { array: true },
          VariableDeclarator: { array: true, object: true },
        },
      ],
      "prefer-numeric-literals": "error",
      "prefer-promise-reject-errors": "error",
      "prefer-rest-params": "error",
      "prefer-spread": "error",
      "prefer-template": "error",
      radix: ["error", "always"],
      "sort-vars": "error",
      "symbol-description": "error",
      "vars-on-top": "error",
      yoda: ["error", "never"],
    },
  },
  {
    // TODO: Bug 1773467 - Move these to .mjs or figure out a generic way
    // to identify these as modules.
    files: ["tests/unit/**/*.js"],
    languageOptions: {
      sourceType: "module",
    },
  },
  {
    files: ["bin/**", "content-src/**", "tests/unit/**"],
    languageOptions: { globals: globals.node },
  },
  {
    // Use a configuration that's appropriate for modules, workers and
    // non-production files.
    files: [
      "bin/*.js",
      "content-src/schemas/extract-test-corpus.js",
      "tests/**",
      "modules/**",
    ],
    rules: {
      "no-implicit-globals": "off",
    },
  },
  {
    files: ["content-src/**", "tests/unit/**"],
    rules: {
      // Disallow commonjs in these directories.
      "import-x/no-commonjs": 2,
    },
  },
  {
    // These tests simulate the browser environment.
    files: "tests/unit/**",
    languageOptions: {
      globals: {
        ...globals.browser,
        ...globals.mocha,
        assert: "readonly",
        expect: "readonly",
        chai: "readonly",
        sinon: "readonly",
      },
    },
  },
  {
    files: "tests/**",
    rules: {
      "func-name-matching": 0,
      "lines-between-class-members": 0,
    },
  },
];
