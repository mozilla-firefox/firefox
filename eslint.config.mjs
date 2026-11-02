/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import sdl from "eslint-plugin-sdl";
import eslintConfigPrettier from "eslint-config-prettier/flat";
import html from "eslint-plugin-html";
import { importX } from "eslint-plugin-import-x";
import json from "@eslint/json";
import lit from "eslint-plugin-lit";
import mozilla from "eslint-plugin-mozilla";
import reactHooks from "eslint-plugin-react-hooks";

import fs from "fs";
import globals from "globals";
import path from "path";

import globalIgnores from "./eslint-ignores.config.mjs";
import testPathsConfig from "./eslint-test-paths.config.mjs";
import repositoryGlobals from "./eslint-file-globals.config.mjs";
import rollouts from "./eslint-rollouts.config.mjs";
import subdirConfigs from "./eslint-subdirs.config.mjs";

const testPaths = testPathsConfig.testPaths;

function readFile(filePath) {
  return fs
    .readFileSync(filePath, { encoding: "utf-8" })
    .split("\n")
    .filter(p => p && !p.startsWith("#"));
}

const httpTestingPaths = [
  `**/*mixedcontent*.{${mozilla.allFileExtensions.join(",")}}`,
  `**/*CrossOrigin*.{${mozilla.allFileExtensions.join(",")}}`,
  `**/*crossorigin*.{${mozilla.allFileExtensions.join(",")}}`,
  `**/*cors*.{${mozilla.allFileExtensions.join(",")}}`,
  `**/*downgrade*.{${mozilla.allFileExtensions.join(",")}}`,
  `**/*Downgrade*.{${mozilla.allFileExtensions.join(",")}}`,
];

/**
 * Takes each path in the paths array, and expands it with the list of extensions
 * that ESLint is watching.
 *
 * @param {object} options
 * @param {string[]} options.paths
 *   The list of paths to wrap.
 * @param {string[]} [options.excludedExtensions]
 *   The list of extensions to be excluded from the wrapping.
 */
function wrapPaths({ paths, excludedExtensions }) {
  let extensions = excludedExtensions
    ? mozilla.allFileExtensions.filter(f => !excludedExtensions.includes(f))
    : mozilla.allFileExtensions;
  return paths.map(p => {
    if (p.endsWith("**")) {
      return p + `/*.{${extensions.join(",")}}`;
    }
    if (p.endsWith("/")) {
      return p + `**/*.{${extensions.join(",")}}`;
    }
    if (p.endsWith("*")) {
      return p + `.{${extensions.join(",")}}`;
    }
    return p;
  });
}

/**
 * Wraps the paths listed in the files section of a configuration with the
 * file extensions that ESLint is watching.
 *
 * @param {object} configs
 */
function wrapPathsInConfig(configs) {
  for (let config of configs) {
    config.files = wrapPaths({ paths: config.files });
  }
  return configs;
}

let config = [
  {
    name: "import-plugin-settings",
    settings: {
      "import-x/extensions": [".mjs"],
      "import-x/resolver": {
        [path.resolve(import.meta.dirname, "srcdir-resolver.js")]: {},
        node: {},
      },
    },
  },
  {
    name: "ignores",
    ignores: [
      ...globalIgnores,
      ...readFile(
        path.join(
          import.meta.dirname,
          "tools",
          "rewriting",
          "ThirdPartyPaths.txt"
        )
      ),
      ...readFile(
        path.join(import.meta.dirname, "tools", "rewriting", "Generated.txt")
      ),
      ...readFile(
        path.join(
          import.meta.dirname,
          "devtools",
          "client",
          "debugger",
          "src",
          ".eslintignore"
        )
      ).map(p => `devtools/client/debugger/src/${p}`),
    ],
  },
  {
    name: "all-files",
    files: wrapPaths({ paths: ["**"] }),
    linterOptions: {
      // With this option on, if an inline comment disables a rule, and the
      // rule is able to be automatically fixed, then ESLint will remove the
      // inline comment and apply the fix. We don't want this because we have
      // some rules that intentionally need to be turned off in specific cases,
      // e.g. sdl/no-insecure-url.
      reportUnusedDisableDirectives: "off",
    },
    plugins: { lit },
    rules: {
      "lit/quoted-expressions": ["error", "never"],
      "lit/no-invalid-html": "error",
      "lit/no-value-attribute": "error",
    },
  },
  {
    name: "source-type-script",
    files: ["**/*.{js,json,html,sjs,xhtml}"],
    languageOptions: {
      sourceType: "script",
    },
  },
  ...mozilla.configs["flat/recommended"],
  {
    name: "json-recommended-with-comments",
    files: ["**/*.json"],
    language: "json/jsonc",
    ...json.configs.recommended,
  },
  {
    name: "json-recommended-no-comments",
    files: ["**/package.json"],
    language: "json/json",
    ...json.configs.recommended,
  },
  {
    name: "json-empty-keys-off-for-image_builder",
    files: ["taskcluster/docker/image_builder/policy.json"],
    rules: {
      "json/no-empty-keys": "off",
    },
  },
  {
    name: "eslint-plugin-html",
    files: ["**/*.html", "**/*.xhtml"],
    plugins: { html },
  },

  {
    name: "define-globals-for-browser-env",
    // Not available for sjs files.
    files: wrapPaths({ paths: ["**"], excludedExtensions: ["sjs"] }),
    ignores: [
      // Also not available for various other scopes and tools.
      "**/*.sys.mjs",
      "**/?(*.)worker.?(m)js",
      "**/?(*.)serviceworker.?(m)js",
      ...wrapPaths({
        paths: testPaths.xpcshell,
        excludedExtensions: ["mjs", "sjs"],
      }),
      "tools/lint/eslint/**",
    ],
    languageOptions: {
      // `when` is a global that has been added by Google Chrome as a result of
      // adding it to `EventTarget.prototype`. `window` is an `EventTarget`, hence
      // it is available in the global scope.
      // This is part of the Observable proposal
      // https://wicg.github.io/observable/#event-target-integration
      // If Firefox implements the proposal (bug 1871732), then we may need to
      // reconsider this exclusion.
      // For now, it is conflicting with Lit's definition of `when`, and hence
      // to avoid having to disable no-shadow on a lot of files using lit, we
      // instead skip it here for now.
      globals: Object.fromEntries(
        Object.entries(globals.browser).filter(([key]) => key != "when")
      ),
    },
  },
  {
    // Generally we assume that all files, except mjs ones are in our
    // privileged and specific environment. mjs are handled separately by
    // the recommended configuration in eslint-plugin-mozilla.
    name: "define-privileged-and-specific-globals-for-most-files",
    files: wrapPaths({ paths: ["**"], excludedExtensions: ["json"] }),
    ignores: ["browser/components/storybook/**", "tools"],
    languageOptions: {
      globals: {
        ...mozilla.environments.privileged.globals,
        ...mozilla.environments.specific.globals,
      },
    },
  },
  {
    name: "define-globals-for-node-files",
    files: [
      // All .eslintrc.mjs files are in the node environment, so turn that
      // on here.
      "**/.eslintrc*.mjs",
      // .js files in the top-level are generally assumed to be node.
      "\.*.js",
      // *.config.js files are generally assumed to be configuration files
      // based for node.
      "**/*.config.js",
      // The resolver for moz-src for eslint, vscode etc.
      "srcdir-resolver.js",
    ],
    languageOptions: {
      globals: { ...globals.node, ...mozilla.turnOff(globals.browser) },
    },
  },

  {
    name: "browser-no-more-globals",
    files: ["browser/base/content/browser.js"],
    rules: {
      "mozilla/no-more-globals": "error",
    },
  },
  {
    name: "jsx-files",
    files: ["**/*.jsx", "browser/components/storybook/.storybook/**/*.mjs"],
    languageOptions: {
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
      },
    },
  },
  {
    name: "eslint-plugin-import-rules",
    files: ["**/*.mjs"],
    plugins: { "import-x": importX },
    rules: {
      "import-x/default": "error",
      "import-x/export": "error",
      "import-x/named": "error",
      "import-x/namespace": "error",
      "import-x/newline-after-import": "error",
      "import-x/no-duplicates": "error",
      "import-x/no-absolute-path": "error",
      "import-x/no-named-default": "error",
      "import-x/no-named-as-default": "error",
      "import-x/no-named-as-default-member": "error",
      "import-x/no-self-import": "error",
      "import-x/no-unassigned-import": "error",
      "import-x/no-unresolved": [
        "error",
        // Bug 1773473 - Ignore resolver URLs for chrome and resource as we
        // do not yet have a resolver for them.
        { ignore: ["chrome://", "resource://"] },
      ],
      "import-x/no-useless-path-segments": "error",
    },
  },
  {
    name: "turn-off-unassigned-import-for-stories",
    // Turn off no-unassigned-import for files that typically test our
    // custom elements, which are imported for the side effects (ie
    // the custom element being registered) rather than any particular
    // export:
    files: ["**/*.stories.mjs"],
    plugins: { "import-x": importX },
    rules: {
      "import-x/no-unassigned-import": "off",
    },
  },
  {
    ...mozilla.configs["flat/general-test"],
    files: wrapPaths({ paths: ["**/test/**", "**/tests/**"] }),
    plugins: { sdl },
    rules: {
      // No using of insecure url, so no http urls.
      // Note: This is turned off for xpcshell-tests as it is not considered
      // necessary for xpcshell level tests.
      "sdl/no-insecure-url": [
        "error",
        {
          exceptions: [
            "^http:\\/\\/mochi\\.test?.*",
            "^http:\\/\\/mochi\\.xorigin-test?.*",
            "^http:\\/\\/localhost?.*",
            "^http:\\/\\/127\\.0\\.0\\.1?.*",
            // Exempt xmlns urls
            "^http:\\/\\/www\\.w3\\.org?.*",
            "^http:\\/\\/www\\.mozilla\\.org\\/keymaster\\/gatekeeper?.*",
            // Exempt urls that start with ftp or ws.
            "^ws:?.*",
            "^ftp:?.*",
          ],
          varExceptions: ["insecure?.*"],
        },
      ],
      ...mozilla.configs["flat/general-test"].rules,
    },
  },
  {
    ...mozilla.configs["flat/xpcshell-test"],
    files: wrapPaths({
      paths: testPaths.xpcshell,
      excludedExtensions: ["mjs", "sjs"],
    }),
    plugins: { sdl },
    rules: {
      // No using of insecure url, so no http urls.
      // Note: This is turned off for xpcshell-tests as it is not considered
      // necessary for xpcshell level tests.
      "sdl/no-insecure-url": "off",
      ...mozilla.configs["flat/xpcshell-test"].rules,
    },
  },
  {
    name: "no-unused-vars-disable-on-headjs",
    // If it is an xpcshell head file, we turn off global unused variable checks, as it
    // would require searching the other test files to know if they are used or not.
    // This would be expensive and slow, and it isn't worth it for head files.
    // We could get developers to declare as exported, but that doesn't seem worth it.
    files: testPaths.xpcshell.map(filePath => `${filePath}head*.js`),
    rules: {
      "no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          caughtErrors: "none",
          vars: "local",
        },
      ],
    },
  },
  {
    name: "no-unused-vars-for-xpcshell",
    // This section enables errors of no-unused-vars globally for all test*.js
    // files in xpcshell test paths.
    // This is not done in the xpcshell-test configuration as we cannot pull
    // in overrides from there. We should at some stage, aim to enable this
    // for all files in xpcshell-tests.
    files: testPaths.xpcshell.map(filePath => `${filePath}test*.js`),
    rules: {
      // No declaring variables that are never used
      "no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          caughtErrors: "none",
          vars: "all",
        },
      ],
    },
  },
  {
    ...mozilla.configs["flat/browser-test"],
    files: wrapPaths({
      paths: testPaths.browser,
      excludedExtensions: ["mjs", "sjs"],
    }),
  },
  {
    ...mozilla.configs["flat/mochitest-test"],
    files: wrapPaths({
      paths: testPaths.mochitest,
      excludedExtensions: ["mjs"],
    }),
    ignores: ["security/manager/ssl/tests/mochitest/browser/**"],
  },
  {
    ...mozilla.configs["flat/chrome-test"],
    files: wrapPaths({
      paths: testPaths.chrome,
      excludedExtensions: ["mjs", "sjs"],
    }),
  },
  {
    name: "simpletest",
    languageOptions: {
      globals: {
        ...mozilla.environments.simpletest.globals,
      },
    },
    files: [
      ...testPaths.mochitest.map(filePath => `${filePath}/**/*.js`),
      ...testPaths.chrome.map(filePath => `${filePath}/**/*.js`),
    ],
  },
  {
    name: "multiple-test-kinds",
    // Some directories have multiple kinds of tests, and some rules
    // don't work well for HTML-based mochitests, so disable those.
    files: testPaths.xpcshell
      .concat(testPaths.browser)
      .map(filePath => [`${filePath}/**/*.html`, `${filePath}/**/*.xhtml`])
      .flat(),
    rules: {
      // plain/chrome mochitests don't automatically include Assert, so
      // autofixing `ok()` to Assert.something is bad.
      "mozilla/no-comparison-or-assignment-inside-ok": "off",
    },
  },
  {
    name: "test-file-reuse",
    // Some directories reuse `test_foo.js` files between mochitest-plain and
    // unit tests, or use custom postMessage-based assertion propagation into
    // browser tests. Ignore those too:
    files: wrapPaths({
      paths: [
        // Reuses xpcshell unit test scripts in mochitest-plain HTML files.
        "dom/indexedDB/test/**",
        // Dispatches functions to the webpage in ways that are hard to detect.
        "toolkit/components/antitracking/test/**",
      ],
    }),
    rules: {
      "mozilla/no-comparison-or-assignment-inside-ok": "off",
    },
  },
  {
    // Rules of Hooks broadly checks for camelCase "use" identifiers, so
    // enable only for paths actually using React to avoid false positives.
    name: "react-hooks",
    files: [
      "browser/components/aboutwelcome/**",
      "browser/components/asrouter/**",
      "browser/extensions/newtab/**",
      "devtools/**",
    ],
    ...reactHooks.configs["recommended-latest"],
    plugins: { "react-hooks": reactHooks },
    rules: {
      // react-hooks/recommended has exhaustive-deps as a warning, we prefer
      // errors, so that raised issues get addressed one way or the other.
      "react-hooks/exhaustive-deps": "error",
    },
  },
  {
    name: "disable-no-insecure-url-for-http-testing",
    // Exempt files with these paths since they have to use http for full coverage
    files: httpTestingPaths,
    plugins: { sdl },
    rules: {
      "sdl/no-insecure-url": "off",
    },
  },
  {
    name: "mozilla/valid-jsdoc",
    files: wrapPaths({ paths: ["**"] }),
    ...mozilla.configs["flat/valid-jsdoc"],
  },
  {
    name: "mozilla/require-jsdoc",
    files: wrapPaths({ paths: ["**"] }),
    ...mozilla.configs["flat/require-jsdoc"],
  },
  {
    name: "rollout-no-browser-refs-in-toolkit",
    files: ["toolkit/**"],
    ignores: ["toolkit/**/test/**", "toolkit/**/tests/**"],
    plugins: { mozilla },
    rules: {
      "mozilla/no-browser-refs-in-toolkit": "error",
    },
  },
  {
    name: "no-newtab-refs-outside-newtab",
    files: ["**/*.mjs", "**/*.js", "**/*.sys.mjs"],
    ignores: [
      "tools/@types/generated/**",
      "browser/base/content/test/browser-static/browser_all_files_referenced.js",
      "tools/lint/eslint/eslint-plugin-mozilla/lib/rules/no-newtab-refs-outside-newtab.mjs",
      "tools/lint/eslint/eslint-plugin-mozilla/tests/no-newtab-refs-outside-newtab.mjs",
    ],
    plugins: { mozilla },
    rules: {
      "mozilla/no-newtab-refs-outside-newtab": "error",
    },
  },
  {
    name: "preferences-heading-level-via-config",
    // Remove once typechecking is enabled and violations fail patches
    files: [
      "browser/components/preferences/config/**/*.mjs",
      "browser/extensions/newtab/lib/AboutPreferences.sys.mjs",
    ],
    rules: {
      "no-restricted-syntax": [
        "error",
        {
          selector:
            ":matches(Property[key.name='controlAttrs'], Property[key.value='controlAttrs']) > ObjectExpression > :matches(Property[key.name=/^(?:headinglevel|headingLevel)$/], Property[key.value=/^\\.?(?:headinglevel|headingLevel)$/])",
          message:
            "Set heading levels via the top-level `headingLevel` property on the setting config, not through `controlAttrs`.",
        },
      ],
    },
  },

  ...wrapPathsInConfig(subdirConfigs),
  ...wrapPathsInConfig(repositoryGlobals),

  /**
   * The items below should always be the last items in this order:
   *
   * - Enable eslint-config-prettier.
   * - Enable curly.
   * - Rollouts
   */

  // Turn off rules that conflict with Prettier.
  { name: "eslint-config-prettier", ...eslintConfigPrettier },
  {
    name: "enable-curly",
    files: wrapPaths({ paths: ["**/"] }),
    rules: {
      // Require braces around blocks that start a new line. This must be
      // configured after eslint-config-prettier is included, as otherwise
      // eslint-config-prettier disables the curly rule. Hence, we do
      // not include it in
      // `tools/lint/eslint/eslint-plugin-mozilla/lib/configs/recommended.js`.
      curly: ["error", "all"],
    },
  },
  ...wrapPathsInConfig(rollouts),
];

// The various places we get our globals from use true/false rather than
// the strings required by ESLint, so translate those here.
config.map(entry => {
  if (entry.languageOptions?.globals) {
    let newGlobals = {};
    for (let [key, value] of Object.entries(entry.languageOptions.globals)) {
      if (typeof entry.languageOptions.globals[key] == "boolean") {
        newGlobals[key] = value ? "writable" : "readonly";
      } else {
        newGlobals[key] = value;
      }
    }
  }
  return entry;
});

export default config;
