/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that the sidebar is hidden for all methods of closing it.

"use strict";

const TEST_URI = "data:text/html;charset=utf8,<!DOCTYPE html>";

add_task(async function () {
  // Should be removed when sidebar work is complete
  await pushPref("devtools.webconsole.sidebarToggle", true);

  const hud = await openNewTabAndConsole(TEST_URI);
  await showSidebar(hud);

  info("Click the clear console button");
  const clearButton = hud.ui.document.querySelector(".devtools-button");
  clearButton.click();
  await waitFor(() => !findAllMessages(hud).length);
  let sidebar = hud.ui.document.querySelector(".sidebar");
  ok(!sidebar, "Sidebar hidden after clear console button clicked");

  await showSidebar(hud);

  info("Send a console.clear()");
  const onMessagesCleared = waitForMessageByType(
    hud,
    "Console was cleared",
    ".console-api"
  );
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    content.wrappedJSObject.console.clear();
  });
  await onMessagesCleared;
  sidebar = hud.ui.document.querySelector(".sidebar");
  ok(!sidebar, "Sidebar hidden after console.clear()");

  await showSidebar(hud);

  info("Send ctrl-l to clear console");
  let clearShortcut;
  if (Services.appinfo.OS === "Darwin") {
    clearShortcut = WCUL10n.getStr("webconsole-clear-key-macos");
  } else {
    clearShortcut = WCUL10n.getStr("webconsole-clear-key");
  }
  synthesizeKeyShortcut(clearShortcut);
  await waitFor(() => !findAllMessages(hud).length);
  sidebar = hud.ui.document.querySelector(".sidebar");
  ok(!sidebar, "Sidebar hidden after ctrl-l");

  await showSidebar(hud);

  info("Click the close button");
  const closeButton = hud.ui.document.querySelector(".sidebar-close-button");
  const appNode = hud.ui.document.querySelector(".webconsole-app");
  let onSidebarShown = waitForNodeMutation(appNode, { childList: true });
  closeButton.click();
  await onSidebarShown;
  sidebar = hud.ui.document.querySelector(".sidebar");
  ok(!sidebar, "Sidebar hidden after clicking on close button");

  await showSidebar(hud);

  info("Send escape to hide sidebar");
  onSidebarShown = waitForNodeMutation(appNode, { childList: true });
  EventUtils.synthesizeKey("KEY_Escape");
  await onSidebarShown;
  sidebar = hud.ui.document.querySelector(".sidebar");
  ok(!sidebar, "Sidebar hidden after sending esc");
  ok(isInputFocused(hud), "console input is focused after closing the sidebar");
});

async function showSidebar(hud) {
  const onMessage = waitForMessageByType(hud, "Object", ".console-api");
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    content.wrappedJSObject.console.log({ a: 1 });
  });
  await onMessage;

  const objectNode = hud.ui.outputNode.querySelector(
    ".object-inspector .objectBox"
  );
  const appNode = hud.ui.document.querySelector(".webconsole-app");
  const onSidebarShown = waitForNodeMutation(appNode, { childList: true });

  const contextMenu = await openContextMenu(hud, objectNode);
  const openInSidebar = contextMenu.querySelector("#console-menu-open-sidebar");
  openInSidebar.click();
  await onSidebarShown;
  await hideContextMenu(hud);

  // Let's wait for the object inside the sidebar to be expanded.
  await waitFor(
    () => appNode.querySelectorAll(".sidebar .tree-node").length > 1,
    null,
    100
  );
}
