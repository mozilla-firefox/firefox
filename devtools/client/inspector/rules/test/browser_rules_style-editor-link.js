/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the links from the rule-view to the styleeditor

const STYLESHEET_DATA_URL_CONTENTS = `#first {
color: blue
}`;
const STYLESHEET_DATA_URL = `data:text/css,${encodeURIComponent(
  STYLESHEET_DATA_URL_CONTENTS
)}`;

const EXTERNAL_STYLESHEET_FILE_NAME = "doc_style_editor_link.css";
const EXTERNAL_STYLESHEET_URL = URL_ROOT_SSL + EXTERNAL_STYLESHEET_FILE_NAME;

const DOCUMENT_HTML = encodeURIComponent(`
  <html>
    <head>
      <title>Rule view style editor link test</title>
      <style type="text/css">
        html { color: #000000; }
        div { font-variant: small-caps; color: #000000; }
        .nomatches {color: #ff0000;}
      </style>
      <style>
        div { font-weight: bold; }
      </style>
      <link rel="stylesheet" type="text/css" href="${STYLESHEET_DATA_URL}">
      <link rel="stylesheet" type="text/css" href="${EXTERNAL_STYLESHEET_URL}">
    </head>
    <body>
      <div id="first" style="margin: 10em;font-size: 14pt; font-family: helvetica, sans-serif; color: #AAA">
        <h1>Some header text</h1>
        <p id="salutation" style="font-size: 12pt">hi.</p>
        <p id="body" style="font-size: 12pt">I am a test-case. This text exists
        solely to provide some things to
        <span style="color: yellow" class="highlight">
        highlight</span> and <span style="font-weight: bold">count</span>
        style list-items in the box at right. If you are reading this,
        you should go do something else instead. Maybe read a book. Or better
        yet, write some test-cases for another bit of code.
        <span style="font-style: italic">some text</span></p>
        <p id="closing">more text</p>
        <p>even more text</p>
      </div>
    </body>
  </html>
`);

const DOCUMENT_DATA_URL = "data:text/html;charset=utf-8," + DOCUMENT_HTML;
const EXAMPLE_ORG_DOCUMENT_URL =
  "https://example.org/document-builder.sjs?html=" + DOCUMENT_HTML;

add_task(async function () {
  await addTab(DOCUMENT_DATA_URL);
  const { toolbox, inspector, view } = await openRuleView();

  await testAllStylesheets(inspector, view, toolbox);

  info("Navigate to the example.org document");
  await navigateTo(EXAMPLE_ORG_DOCUMENT_URL);
  await testAllStylesheets(inspector, view, toolbox);
});

add_task(async function () {
  info("Check that link to the style editor works after tab reload");
  await addTab(EXAMPLE_ORG_DOCUMENT_URL);
  const { toolbox, inspector, view } = await openRuleView();

  info("Reload the example.org document");
  // Use navigateTo as it waits for the inspector to be ready.
  await navigateTo(EXAMPLE_ORG_DOCUMENT_URL);
  await testAllStylesheets(inspector, view, toolbox);
});

async function testAllStylesheets(inspector, view, toolbox) {
  await selectNode("div", inspector);
  await testRuleViewLinkLabel(view);
  await testDisabledStyleEditor(view, toolbox);
  await testFirstInlineStyleSheet(view, toolbox);
  await testSecondInlineStyleSheet(view, toolbox);
  await testExternalStyleSheet(view, toolbox);

  info("Switch back to the inspector panel");
  await toolbox.selectTool("inspector");
  await selectNode("body", inspector);
}

async function testFirstInlineStyleSheet(view, toolbox) {
  info("Testing inline stylesheet");

  info("Listening for toolbox switch to the styleeditor");
  const onSwitch = waitForStyleEditor(toolbox);

  info("Clicking an inline stylesheet");
  clickLinkByIndex(view, 4);
  const editor = await onSwitch;

  ok(true, "Switched to the style-editor panel in the toolbox");

  await validateStyleEditorSheet(toolbox, editor, 0);
}

async function testSecondInlineStyleSheet(view, toolbox) {
  info("Testing second inline stylesheet");

  const styleEditorPanel = toolbox.getCurrentPanel();
  const onEditorSelected = styleEditorPanel.UI.once("editor-selected");

  info("Switching back to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");
  const onToolSelected = toolbox.once("styleeditor-selected");

  info("Clicking on second inline stylesheet link");
  clickLinkByIndex(view, 3);

  info("Wait for the stylesheet editor to be selected");
  const editor = await onEditorSelected;
  await onToolSelected;

  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor is selected again"
  );
  await validateStyleEditorSheet(toolbox, editor, 1);
}

async function testExternalStyleSheet(view, toolbox) {
  info("Testing external stylesheet");
  const styleEditorPanel = toolbox.getCurrentPanel();
  const onEditorSelected = styleEditorPanel.UI.once("editor-selected");

  info("Switching back to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");
  const onToolSelected = toolbox.once("styleeditor-selected");

  info("Clicking on an external stylesheet link");
  clickLinkByIndex(view, 1);

  info("Wait for the stylesheet editor to be selected");
  const editor = await onEditorSelected;
  await onToolSelected;

  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor is selected again"
  );
  await validateStyleEditorSheet(toolbox, editor, 2);
}

async function validateStyleEditorSheet(toolbox, editor, expectedSheetIndex) {
  info("validating style editor stylesheet");
  is(
    editor.styleSheet.styleSheetIndex,
    expectedSheetIndex,
    "loaded stylesheet index matches document stylesheet"
  );

  const href = editor.styleSheet.href || editor.styleSheet.nodeHref;

  const expectedHref = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [expectedSheetIndex],
    _expectedSheetIndex => {
      return (
        content.document.styleSheets[_expectedSheetIndex].href ||
        content.document.location.href
      );
    }
  );

  is(href, expectedHref, "loaded stylesheet href matches document stylesheet");
}

async function testDisabledStyleEditor(view, toolbox) {
  info("Testing with the style editor disabled");

  info("Switching to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");

  info("Disabling the style editor");
  Services.prefs.setBoolPref("devtools.styleeditor.enabled", false);
  gDevTools.emit("tool-unregistered", "styleeditor");

  info("Clicking on a link");
  testUnselectableRuleViewLink(view, 1);
  clickLinkByIndex(view, 1);
  // Wait for a bit just to make sure the click didn't had an impact
  await wait(2000);

  is(toolbox.currentToolId, "inspector", "The click should have no effect");

  info("Enabling the style editor");
  Services.prefs.setBoolPref("devtools.styleeditor.enabled", true);
  gDevTools.emit("tool-registered", "styleeditor");

  Services.prefs.clearUserPref("devtools.styleeditor.enabled");
}

async function testRuleViewLinkLabel(view) {
  info("Checking the data URL link label");
  let labelElem = await waitFor(() => getRuleViewLinkByIndex(view, 1));
  let value = labelElem.textContent;
  let tooltipText = labelElem.getAttribute("title");

  is(
    value,
    STYLESHEET_DATA_URL_CONTENTS + ":1",
    "Rule view data URL stylesheet display value matches contents"
  );
  is(
    tooltipText,
    `View source in Style Editor → ${STYLESHEET_DATA_URL}:1`,
    "Rule view data URL stylesheet tooltip text matches the full URI path"
  );

  info("Checking the external link label");
  labelElem = await waitFor(() => getRuleViewLinkByIndex(view, 2));
  value = labelElem.textContent;
  tooltipText = labelElem.getAttribute("title");

  is(
    value,
    `${EXTERNAL_STYLESHEET_FILE_NAME}:1`,
    "Rule view external stylesheet display value matches filename and line number"
  );
  is(
    tooltipText,
    `View source in Style Editor → ${EXTERNAL_STYLESHEET_URL}:1`,
    "Rule view external stylesheet tooltip text matches the full URI path"
  );
}

function testUnselectableRuleViewLink(view, index) {
  const link = getRuleViewLinkByIndex(view, index);
  const unselectable = link.parentNode.hasAttribute("unselectable");

  ok(unselectable, "Rule view is unselectable");
}

function clickLinkByIndex(view, index) {
  const link = getRuleViewLinkByIndex(view, index);
  link.scrollIntoView();
  link.click();
}
