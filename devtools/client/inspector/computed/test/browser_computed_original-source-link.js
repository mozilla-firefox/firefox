/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the computed view shows the original source link when source maps
// are enabled.

const TESTCASE_URI = URL_ROOT_SSL + "doc_sourcemaps.html";
const PREF = "devtools.source-map.client-service.enabled";
const SCSS_LOC = "doc_sourcemaps.scss:4";
const CSS_LOC = "doc_sourcemaps.css:1";

add_task(async function () {
  info("Turning the pref " + PREF + " on");
  Services.prefs.setBoolPref(PREF, true);

  await addTab(TESTCASE_URI);
  const { toolbox, inspector, view } = await openComputedView();
  let onLinksUpdated = inspector.once("computed-view-sourcelinks-updated");
  await selectNode("div", inspector);

  info("Expanding the first property");
  await expandComputedViewPropertyByIndex(view, 0);

  info("Verifying the link text");
  await onLinksUpdated;
  verifyLinkText(view, SCSS_LOC);

  info("Toggling the pref");
  onLinksUpdated = inspector.once("computed-view-sourcelinks-updated");
  Services.prefs.setBoolPref(PREF, false);
  await onLinksUpdated;

  info("Verifying that the link text has changed after the pref change");
  await verifyLinkText(view, CSS_LOC);

  info("Toggling the pref again");
  onLinksUpdated = inspector.once("computed-view-sourcelinks-updated");
  Services.prefs.setBoolPref(PREF, true);
  await onLinksUpdated;

  info("Testing that clicking on the link works");
  await testClickingLink(toolbox, view);

  info("Turning the pref " + PREF + " off");
  Services.prefs.clearUserPref(PREF);
});

async function testClickingLink(toolbox, view) {
  const onEditor = waitForStyleEditor(toolbox, "doc_sourcemaps.scss");

  info("Clicking the computedview stylesheet link");
  const link = getComputedViewLinkByIndex(view, 0);
  link.scrollIntoView();
  link.click();

  const editor = await onEditor;

  const { line } = editor.sourceEditor.getCursor();
  is(line, 3, "cursor is at correct line number in original source");
}

function verifyLinkText(view, text) {
  const link = getComputedViewLinkByIndex(view, 0);
  is(
    link.textContent,
    text,
    "Linked text changed to display the correct location"
  );
}
