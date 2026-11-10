/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Check that the browser console gets session state is set correctly, and that
// it re-opens when restore is requested.

"use strict";

add_task(async function () {
  is(
    BrowserConsoleManager.getBrowserConsoleSessionState(),
    false,
    "Session state false by default"
  );
  BrowserConsoleManager.storeBrowserConsoleSessionState();
  is(
    BrowserConsoleManager.getBrowserConsoleSessionState(),
    false,
    "Session state still not true even after setting (since Browser Console is closed)"
  );

  const hud = await BrowserConsoleManager.toggleBrowserConsole();
  BrowserConsoleManager.storeBrowserConsoleSessionState();
  is(
    BrowserConsoleManager.getBrowserConsoleSessionState(),
    true,
    "Session state true (since Browser Console is opened)"
  );

  info(
    "Closing the browser console and waiting for the session restore to reopen it"
  );
  await safeCloseBrowserConsole();

  const opened = waitForBrowserConsole(hud);
  await gDevTools.restoreDevToolsSession({
    browserConsole: true,
  });

  info("Waiting for the console to open after session restore");
  await opened;

  await safeCloseBrowserConsole();
  BrowserConsoleManager.storeBrowserConsoleSessionState();
  is(
    BrowserConsoleManager.getBrowserConsoleSessionState(),
    false,
    "Session state reset to false once the Browser Console is closed"
  );
});
