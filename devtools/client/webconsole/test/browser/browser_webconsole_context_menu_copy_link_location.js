/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test the Copy Link Location menu item of the webconsole is displayed for network
// messages and copies the expected URL.

"use strict";

const TEST_URI =
  "http://example.com/browser/devtools/client/webconsole/" +
  "test/browser/test-console.html?_date=" +
  Date.now();
const CONTEXT_MENU_ID = "#console-menu-copy-url";

add_task(async function () {
  // Enable net messages in the console for this test.
  await pushPref("devtools.webconsole.filter.net", true);

  const hud = await openNewTabAndConsole(TEST_URI);
  await clearOutput(hud);

  info("Test Copy URL menu item for text log");

  info("Logging a text message in the content window");
  const onLogMessage = waitForMessageByType(hud, "stringLog", ".console-api");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.wrappedJSObject.stringLog();
  });
  let message = await onLogMessage;
  ok(message, "Text log found in the console");

  info("Open and check the context menu for the logged text message");
  let menuPopup = await openContextMenu(hud, message.node);

  let copyURLItem = menuPopup.querySelector(CONTEXT_MENU_ID);
  ok(!copyURLItem, "Copy URL menu item is hidden for a simple text message");

  info("Open and check the context menu for the logged text message");
  const locationElement = message.node.querySelector(".frame-link-source");
  menuPopup = await openContextMenu(hud, locationElement);
  copyURLItem = menuPopup.querySelector(CONTEXT_MENU_ID);
  ok(copyURLItem, "The Copy Link Location entry is displayed");

  info("Click on Copy URL menu item and wait for clipboard to be updated");
  await waitForClipboardPromise(() => copyURLItem.click(), TEST_URI);
  ok(true, "Expected text was copied to the clipboard.");

  await hideContextMenu(hud);
  await clearOutput(hud);

  info("Test Copy URL menu item for network log");

  info("Reload the content window to produce a network log");
  const onNetworkMessage = waitForMessageByType(
    hud,
    "test-console.html",
    ".network"
  );
  await reloadBrowser(gBrowser.selectedBrowser);

  message = await onNetworkMessage;
  ok(message, "Network log found in the console");

  info("Open and check the context menu for the logged network message");
  menuPopup = await openContextMenu(hud, message.node);
  copyURLItem = menuPopup.querySelector(CONTEXT_MENU_ID);
  ok(copyURLItem, "Copy url menu item is available in context menu");

  info("Click on Copy URL menu item and wait for clipboard to be updated");
  await waitForClipboardPromise(() => copyURLItem.click(), TEST_URI);
  ok(true, "Expected text was copied to the clipboard.");

  await hideContextMenu(hud);
  await clearOutput(hud);

  info("Test Copy URL menu item from [Learn More] link");

  info("Generate a Reference Error in the JS Console");
  message = await executeAndWaitForErrorMessage(
    hud,
    "area51.aliens",
    "ReferenceError:"
  );
  ok(message, "Error log found in the console");

  const learnMoreElement = message.node.querySelector(".learn-more-link");
  menuPopup = await openContextMenu(hud, learnMoreElement);
  copyURLItem = menuPopup.querySelector(CONTEXT_MENU_ID);
  ok(copyURLItem, "Copy url menu item is available in context menu");

  info("Click on Copy URL menu item and wait for clipboard to be updated");
  await waitForClipboardPromise(
    () => copyURLItem.click(),
    learnMoreElement.href
  );
  ok(true, "Expected text was copied to the clipboard.");

  await hideContextMenu(hud);
});
