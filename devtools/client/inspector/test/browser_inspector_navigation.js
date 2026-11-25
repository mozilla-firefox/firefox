/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Test that inspector updates when page is navigated.

const TEST_URL_FILE =
  "browser/devtools/client/inspector/test/doc_inspector_breadcrumbs.html";

const TEST_URL_1 = "https://test1.example.org/" + TEST_URL_FILE;
const TEST_URL_2 = "https://test2.example.org/" + TEST_URL_FILE;

// Bug 1340592: "srcset" attribute causes bfcache events (pageshow/pagehide)
// with buggy "persisted" values.
const TEST_URL_3 =
  "data:text/html;charset=utf-8," +
  encodeURIComponent('<img src="foo.png" srcset="foo.png 1.5x" />');
const TEST_URL_4 =
  "data:text/html;charset=utf-8," + encodeURIComponent("<h1>bar</h1>");

add_task(async function () {
  const { inspector } = await openInspectorForURL(TEST_URL_1);

  await selectNode("#i1", inspector);

  info("Navigating to a different page.");
  await navigateTo(TEST_URL_2);

  ok(true, "New page loaded");
  await selectNode("#i1", inspector);

  const markuploaded = inspector.once("markuploaded");
  const onUpdated = inspector.once("inspector-updated");

  info("Going back in history");
  gBrowser.goBack();

  info("Waiting for markup view to load after going back in history.");
  await markuploaded;

  info("Check that the inspector updates");
  await onUpdated;

  ok(true, "Old page loaded");
  const url = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => content.location.href
  );
  is(url, TEST_URL_1, "URL is correct.");

  await selectNode("#i1", inspector);
});

add_task(async function () {
  const { inspector } = await openInspectorForURL(TEST_URL_3);

  await selectNode("img", inspector);

  info("Navigating to a different page.");
  await navigateTo(TEST_URL_4);

  ok(true, "New page loaded");
  await selectNode("#h1", inspector);

  const markuploaded = inspector.once("markuploaded");
  const onUpdated = inspector.once("inspector-updated");

  info("Going back in history");
  gBrowser.goBack();

  info("Waiting for markup view to load after going back in history.");
  await markuploaded;

  info("Check that the inspector updates");
  await onUpdated;

  ok(true, "Old page loaded");
  const url = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => content.location.href
  );
  is(url, TEST_URL_3, "URL is correct.");

  await selectNode("img", inspector);
});
