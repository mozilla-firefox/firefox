/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that messages are logged and observed with the correct category. See Bug 595934.
const { MESSAGE_CATEGORY } = require("resource://devtools/shared/constants.js");

const TEST_URI =
  "data:text/html;charset=utf-8,<!DOCTYPE html>Web Console test for " +
  "bug 595934 - message categories coverage.";
const TESTS_PATH =
  "https://example.com/browser/devtools/client/webconsole/test/browser/";
const TESTS = [
  {
    // #0
    file: "test-message-categories-css-loader.html",
    category: "CSS Loader",
    matchString: "text/css",
    typeSelector: ".error",
  },
  {
    // #1
    file: "test-message-categories-imagemap.html",
    category: "Layout: ImageMap",
    matchString: 'shape="rect"',
    typeSelector: ".warn",
  },
  {
    // #2
    file: "test-message-categories-html.html",
    category: "HTML",
    matchString: "multipart/form-data",
    typeSelector: ".warn",
    onload() {
      SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
        const form = content.document.querySelector("form");
        form.submit();
      });
    },
  },
  {
    // #3
    file: "test-message-categories-workers.html",
    category: "Web Worker",
    matchString: "fooBarWorker",
    typeSelector: ".error",
  },
  {
    // #4
    file: "test-message-categories-malformedxml.xhtml",
    category: "malformed-xml",
    matchString: "no root element found",
    typeSelector: ".error",
  },
  {
    // #5
    file: "test-message-categories-svg.xhtml",
    category: "SVG",
    matchString: "fooBarSVG",
    typeSelector: ".warn",
  },
  {
    // #6
    file: "test-message-categories-css-parser.html",
    category: MESSAGE_CATEGORY.CSS_PARSER,
    matchString: "foobarCssParser",
    typeSelector: ".warn",
  },
  {
    // #7
    file: "test-message-categories-malformedxml-external.html",
    category: "malformed-xml",
    matchString: "</html>",
    typeSelector: ".error",
  },
  {
    // #8
    file: "test-message-categories-empty-getelementbyid.html",
    category: "DOM",
    matchString: "getElementById",
    typeSelector: ".warn",
  },
  {
    // #9
    file: "test-message-categories-canvas-css.html",
    category: MESSAGE_CATEGORY.CSS_PARSER,
    matchString: "foobarCanvasCssParser",
    typeSelector: ".warn",
  },
  {
    // #10
    file: "test-message-categories-image.html",
    category: "Image",
    matchString: "corrupt",
    typeSelector: ".error",
  },
];

add_task(async function () {
  requestLongerTimeout(2);

  // Disable bfcache for Fission for now.
  // If Fission is disabled, the pref is no-op.
  await pushPref("fission.bfcacheInParent", false);
  await pushPref("devtools.webconsole.filter.css", true);
  await pushPref("devtools.webconsole.filter.net", true);
  await pushPref("devtools.webconsole.persistlog", true);

  const hud = await openNewTabAndConsole(TEST_URI);
  for (let i = 0; i < TESTS.length; i++) {
    const test = TESTS[i];
    info("Running test #" + i);
    await runTest(test, hud);
  }

  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});

async function runTest(test, hud) {
  const { file, category, matchString, typeSelector, onload } = test;

  const onMessageLogged = waitForMessageByType(hud, matchString, typeSelector);

  const onMessageObserved = new Promise(resolve => {
    Services.console.registerListener(function listener(subject) {
      if (!(subject instanceof Ci.nsIScriptError)) {
        return;
      }

      if (subject.category != category) {
        return;
      }

      ok(true, "Expected category [" + category + "] received in observer");
      Services.console.unregisterListener(listener);
      resolve();
    });
  });

  info("Load test file " + file);
  await navigateTo(TESTS_PATH + file);

  // Call test specific callback if defined
  if (onload) {
    onload();
  }

  info("Wait for log message to be observed with the correct category");
  await onMessageObserved;

  info(`Wait for log message ("${matchString}") to be displayed in the hud`);
  await onMessageLogged;
  ok(true, `Message "${matchString}" displayed`);
}
