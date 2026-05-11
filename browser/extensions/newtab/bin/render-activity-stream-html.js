/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable no-console */
const fs = require("fs");
const { mkdir } = require("shelljs");
const path = require("path");
const { pathToFileURL } = require("url");
const chalk = require("chalk");

const DEFAULT_OPTIONS = {
  // Glob leading from CWD to the parent of the intended prerendered directory.
  // Starting in newtab/bin/ and we want to write to newtab/prerendered/ so we
  // go up one level.
  addonPath: "..",
  // depends on the registration in browser/extensions/newtab/jar.mn
  baseUrl: "resource://newtab/",
};

/**
 * templateHTML - Generates HTML for activity stream, given some options and
 * prerendered HTML if necessary.
 *
 * @param  {obj} options
 *         {str} options.baseUrl        The base URL for all local assets
 *         {bool} options.noscripts     Should we include scripts in the prerendered files?
 * @return {str}         An HTML document as a string
 */
function templateHTML(options) {
  const scripts = [
    "chrome://browser/content/contentTheme.js",
    `${options.baseUrl}data/content/vendor.bundle.js`,
    `${options.baseUrl}data/content/activity-stream.bundle.js`,
    `${options.baseUrl}data/content/newtab-render.js`,
  ];

  // Add spacing and script tags
  const scriptRender = `\n${scripts
    .map(script => `    <script src="${script}"></script>`)
    .join("\n")}`;

  // The markup below needs to be formatted by Prettier. But any diff after
  // running this script should be caught by try-runnner.js
  return `
<!-- This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this file,
   - You can obtain one at http://mozilla.org/MPL/2.0/. -->

<!-- Note: This page is pre-rendered by render-activity-stream-html.js -->

<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <meta
      http-equiv="Content-Security-Policy"
      content="default-src 'none'; object-src 'none'; script-src resource: chrome:; connect-src https:; img-src https: data: blob: chrome:; style-src 'unsafe-inline';"
    />
    <meta name="color-scheme" content="light dark" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title data-l10n-id="newtab-page-title"></title>
    <link
      rel="icon"
      type="image/png"
      href="chrome://branding/content/icon32.png"
    />
    <link rel="localization" href="branding/brand.ftl" />
    <link rel="localization" href="toolkit/branding/brandings.ftl" />
    <link rel="localization" href="browser/newtab/newtab.ftl" />
    <link rel="localization" href="toolkit/global/mozMessageBar.ftl" />
    <link
      rel="stylesheet"
      href="chrome://global/skin/design-system/tokens-brand.css"
    />
    <link
      rel="stylesheet"
      href="chrome://newtab/content/css/activity-stream.css"
    />
  </head>
  <body class="activity-stream">
    <div id="root"></div>${options.noscripts ? "" : scriptRender}
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-toggle.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-badge.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-button.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-button-group.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-input-text.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-input-search.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-box-button.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-message-bar.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-radio-group.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-select.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-reorderable-list.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/panel-list.js"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-support-link.mjs"
    ></script>
    <script
      async
      type="module"
      src="chrome://global/content/elements/moz-checkbox.mjs"
    ></script>
  </body>
</html>
`.trimLeft();
}

/**
 * writeFiles - Writes to the desired files the result of a template given
 * various prerendered data and options.
 *
 * @param {string} destPath      Path to write the files to
 * @param {Map}    filesMap      Mapping of a string file name to templater
 * @param {object} options       Various options for the templater
 */
function writeFiles(destPath, filesMap, options) {
  for (const [file, templater] of filesMap) {
    fs.writeFileSync(path.join(destPath, file), templater({ options }));
    console.log(chalk.green(`✓ ${file}`));
  }
}

const STATIC_FILES = new Map([
  ["activity-stream.html", ({ options }) => templateHTML(options)],
  // With vendor bundling there is no separate dev/prod vendor build, so the
  // debug variant is intentionally identical to the release variant.
  // TODO: remove the activityStreamDebug pref from AboutNewTabRedirector.sys.mjs
  // (non-trainhoppable, must ride the release train).
  ["activity-stream-debug.html", ({ options }) => templateHTML(options)],
  [
    "activity-stream-noscripts.html",
    ({ options }) =>
      templateHTML(Object.assign({}, options, { noscripts: true })),
  ],
]);

/**
 * main - Parses command line arguments, generates html and js with templates,
 *        and writes files to their specified locations.
 */
async function main() {
  const { default: meow } = await import("meow");
  const fileUrl = pathToFileURL(__filename);
  const cli = meow(
    `
    Usage
      $ node ./bin/render-activity-stream-html.js [options]

    Options
      -a PATH, --addon-path PATH   Path to the parent of the target directory.
                                   default: "${DEFAULT_OPTIONS.addonPath}"
      -b URL, --base-url URL       Base URL for assets.
                                   default: "${DEFAULT_OPTIONS.baseUrl}"
      --help                       Show this help message.
`,
    {
      description: false,
      // `pkg` is a tiny optimization. It prevents meow from looking for a package
      // that doesn't technically exist. meow searches for a package and changes
      // the process name to the package name. It resolves to the newtab
      // package.json, which would give a confusing name and be wasteful.
      pkg: {
        name: "render-activity-stream-html",
        version: "0.0.0",
      },
      // `importMeta` is required by meow 10+. It was added to support ESM, but
      // meow now requires it, and no longer supports CJS style imports. But it
      // only uses import.meta.url, which can be polyfilled like this:
      importMeta: { url: fileUrl },
      flags: {
        addonPath: {
          type: "string",
          shortFlag: "a",
          default: DEFAULT_OPTIONS.addonPath,
        },
        baseUrl: {
          type: "string",
          shortFlag: "b",
          default: DEFAULT_OPTIONS.baseUrl,
        },
      },
    }
  );

  const options = cli.flags || {};
  const addonPath = path.resolve(__dirname, options.addonPath);
  const prerenderedPath = path.join(addonPath, "prerendered");
  console.log(`Writing prerendered files to ${prerenderedPath}:`);

  mkdir("-p", prerenderedPath);
  writeFiles(prerenderedPath, STATIC_FILES, options);
}

main();
