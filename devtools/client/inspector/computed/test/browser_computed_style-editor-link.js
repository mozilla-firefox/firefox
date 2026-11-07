/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests the links from the computed view to the style editor.

const STYLESHEET_URL =
  "data:text/css," + encodeURIComponent(".highlight {color: blue}");

const DOCUMENT_URL =
  "data:text/html;charset=utf-8," +
  encodeURIComponent(
    `<html>
   <head>
   <title>Computed view style editor link test</title>
   <style type="text/css">
   html { color: #000000; }
   span { font-variant: small-caps; color: #000000; }
   .nomatches {color: #ff0000;}</style> <div id="first" style="margin: 10em;
   font-size: 14pt; font-family: helvetica, sans-serif; color: #AAA">
   </style>
   <style>
   div { color: #f06; }
   </style>
   <link rel="stylesheet" type="text/css" href="${STYLESHEET_URL}">
   <script>
     const sheet = new CSSStyleSheet();
     sheet.replaceSync(".highlight { color: tomato; }");
     document.adoptedStyleSheets.push(sheet);
   </script>
   </head>
   <body>
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
   </html>`
  );

add_task(async function () {
  await addTab(DOCUMENT_URL);
  const { toolbox, inspector, view } = await openComputedView();
  await selectNode("span", inspector);

  await testInlineStyle(view);
  await testFirstInlineStyleSheet(view, toolbox);
  await testSecondInlineStyleSheet(view, toolbox);
  await testExternalStyleSheet(view, toolbox);
  await testConstructedStyleSheet(view, toolbox);
});

async function testInlineStyle(view) {
  info("Testing inline style");

  await expandComputedViewPropertyByIndex(view, 0);

  info(
    "Check that we don't display a link for style attribute declaration in the computed-view"
  );
  checkComputedViewLink(view, {
    index: 0,
    expectedText: null,
  });
}

async function testFirstInlineStyleSheet(view, toolbox) {
  info("Testing inline stylesheet");

  info("Listening for toolbox switch to the styleeditor");
  const onSwitch = waitForStyleEditor(toolbox);

  info("Clicking an inline stylesheet");
  checkComputedViewLink(view, {
    index: 3,
    expectedText: "inline:3",
    expectedTitle: "inline:3",
  });
  const editor = await onSwitch;

  ok(true, "Switched to the style-editor panel in the toolbox");

  await validateStyleEditorSheet(editor, 0);
}

async function testSecondInlineStyleSheet(view, toolbox) {
  info("Testing second inline stylesheet");

  const panel = toolbox.getCurrentPanel();
  const onSelected = panel.UI.once("editor-selected");

  info("Switching back to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");

  info("Clicking on second inline stylesheet link");
  checkComputedViewLink(view, {
    index: 5,
    expectedText: "inline:2",
    expectedTitle: "inline:2",
  });

  info("Waiting for an editor to be selected in StyleEditor");
  const editor = await onSelected;

  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor is selected again"
  );
  await validateStyleEditorSheet(editor, 1);
}

async function testExternalStyleSheet(view, toolbox) {
  info("Testing external stylesheet");

  const panel = toolbox.getCurrentPanel();
  const onSelected = panel.UI.once("editor-selected");

  info("Switching back to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");

  info("Clicking on an external stylesheet link");
  checkComputedViewLink(view, {
    index: 2,
    expectedText: `.highlight {color: blue}:1`,
    expectedTitle: `${STYLESHEET_URL}:1`,
  });

  info("Waiting for an editor to be selected in StyleEditor");
  const editor = await onSelected;

  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor is selected again"
  );
  await validateStyleEditorSheet(editor, 2);
}

async function testConstructedStyleSheet(view, toolbox) {
  info("Testing constructed stylesheet");

  const panel = toolbox.getCurrentPanel();
  const onSelected = panel.UI.once("editor-selected");

  info("Switching back to the inspector panel in the toolbox");
  await toolbox.selectTool("inspector");

  info("Clicking on an constructed stylesheet link");

  checkComputedViewLink(view, {
    index: 1,
    expectedText: "constructed:1",
    expectedTitle: "constructed:1",
  });

  info("Waiting for an editor to be selected in StyleEditor");
  const editor = await onSelected;

  is(
    toolbox.currentToolId,
    "styleeditor",
    "The style editor is selected again"
  );
  ok(editor.styleSheet.constructed, "The constructed stylesheet is selected");
}

async function validateStyleEditorSheet(editor, expectedSheetIndex) {
  info("Validating style editor stylesheet");
  const expectedHref = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [expectedSheetIndex],
    _expectedSheetIndex =>
      content.document.styleSheets[_expectedSheetIndex].href
  );
  is(
    editor.styleSheet.href,
    expectedHref,
    "loaded stylesheet matches document stylesheet"
  );
}

function checkComputedViewLink(view, { index, expectedText, expectedTitle }) {
  const link = getComputedViewLinkByIndex(view, index);

  if (expectedText === null) {
    is(link, null, `There's no link for matched selector #${index}`);
    return;
  }

  is(link.innerText, expectedText, `Link #${index} has expected label`);
  is(
    link.getAttribute("title"),
    expectedTitle,
    `Link #${index} has expected title attribute`
  );
  link.scrollIntoView();
  link.click();
}
