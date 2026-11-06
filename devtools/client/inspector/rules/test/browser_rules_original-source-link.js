/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the stylesheet links in the rule view are correct when source maps
// are defined using the sourceMappingURL link comment in the css file.

const TESTCASE_URI = URL_ROOT_SSL + "doc_sourcemaps.html";
const PREF = "devtools.source-map.client-service.enabled";
const SCSS_FILENAME = "doc_sourcemaps.scss";
const SCSS_LOC_LINE = 4;
const CSS_FILENAME = "doc_sourcemaps.css";
const CSS_LOC_LINE = 1;

add_task(async function () {
  // Open styles in the debugger
  await testLinksInRuleView(true);
  // Open styles in the style editor
  await testLinksInRuleView(false);
});

async function testLinksInRuleView(styleSheetInDebuggerEnabled) {
  await pushPref(
    "devtools.debugger.features.stylesheets-in-debugger",
    styleSheetInDebuggerEnabled
  );

  info("Setting the " + PREF + " pref to true");
  Services.prefs.setBoolPref(PREF, true);

  await addTab(TESTCASE_URI);
  const { toolbox, inspector, view } = await openRuleView();

  info("Selecting the test node");
  await selectNode("div", inspector);

  await verifyStyleSheetLink(
    view,
    SCSS_FILENAME,
    SCSS_LOC_LINE,
    styleSheetInDebuggerEnabled
  );

  info("Setting the " + PREF + " pref to false");
  Services.prefs.setBoolPref(PREF, false);
  await verifyStyleSheetLink(
    view,
    CSS_FILENAME,
    CSS_LOC_LINE,
    styleSheetInDebuggerEnabled
  );

  info("Setting the " + PREF + " pref to true again");
  Services.prefs.setBoolPref(PREF, true);

  await testClickStylesheetLink(toolbox, view, 1);
  if (!styleSheetInDebuggerEnabled) {
    const selectedEditor =
      await waitForOriginalStyleSheetEditorSelection(toolbox);

    const href = selectedEditor.styleSheet.href;
    ok(
      href.endsWith("doc_sourcemaps.scss"),
      "selected stylesheet is correct one"
    );

    await selectedEditor.getSourceEditor();
    const { line } = selectedEditor.sourceEditor.getCursor();
    is(line, 3, "cursor is at correct line number in original source");
  } else {
    const debuggerPanel = toolbox.getPanel("jsdebugger");
    // Wait until the source location is selected
    const location = await waitFor(() => {
      return debuggerPanel._selectors.getSelectedLocation(
        debuggerPanel._getState()
      );
    });
    ok(
      location.source.url.endsWith("doc_sourcemaps.scss"),
      "selected stylesheet is correct one"
    );
    is(location.line, 4, "cursor is at correct line number in original source");
  }
}

async function verifyStyleSheetLink(
  view,
  fileName,
  lineNumber,
  styleSheetInDebuggerEnabled
) {
  const expectedLocation = `${fileName}:${lineNumber}`;
  const expectedUrl = URL_ROOT_SSL + fileName;
  const panel = styleSheetInDebuggerEnabled ? "Debugger" : "Style Editor";
  const expectedTitle = `View source in ${panel} → ${URL_ROOT_SSL}${expectedLocation}`;

  info("Verifying that the rule-view stylesheet link is " + expectedLocation);
  const label = getRuleViewLinkByIndex(view, 1);
  await waitForSuccess(function () {
    return (
      label.textContent == expectedLocation &&
      label.getAttribute("title") === expectedTitle
    );
  }, "Link text changed to display correct location: " + expectedLocation);

  const copyLocationMenuItem = openStyleContextMenuAndGetAllItems(
    view,
    label
  ).find(
    item =>
      item.label ===
      STYLE_INSPECTOR_L10N.getStr("styleinspector.contextmenu.copyLocation")
  );

  try {
    await waitForClipboardPromise(
      () => copyLocationMenuItem.click(),
      () => SpecialPowers.getClipboardData("text/plain") === expectedUrl
    );
    ok(true, "Expected URL was copied to clipboard");
  } catch (e) {
    ok(false, `Clipboard text does not match expected "${expectedUrl}" url`);
  }
}
