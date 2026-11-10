/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
  PermissionTestUtils: "resource://testing-common/PermissionTestUtils.sys.mjs",
});

let testURL =
  "https://example.com/browser/" +
  "uriloader/exthandler/tests/mochitest/FTPprotocolHandler.html";

add_task(async function () {
  // Preload permission to bypass permission dialog.
  PermissionTestUtils.add(
    "https://example.com",
    "open-protocol-handler^ftp",
    Services.perms.ALLOW_ACTION
  );

  // Load a page registering a protocol handler.
  let browser = gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(browser, testURL);
  await BrowserTestUtils.browserLoaded(browser, false, testURL);

  // Register the protocol handler by clicking the notificationbar button.
  let notificationValue = "Protocol Registration: ftp";
  let getNotification = () =>
    gBrowser.getNotificationBox().getNotificationWithValue(notificationValue);
  await TestUtils.waitForCondition(getNotification);
  let notification = getNotification();
  let button = notification.buttonContainer.querySelector("button");
  ok(button, "got registration button");
  button.click();

  // Set the new handler as default.
  const protoSvc = Cc[
    "@mozilla.org/uriloader/external-protocol-service;1"
  ].getService(Ci.nsIExternalProtocolService);
  let protoInfo = protoSvc.getProtocolHandlerInfo("ftp");
  ok(!protoInfo.preferredApplicationHandler, "no preferred handler is set");
  let handlers = protoInfo.possibleApplicationHandlers;
  is(1, handlers.length, "only one handler registered for ftp");
  let handler = handlers.queryElementAt(0, Ci.nsIHandlerApp);
  ok(handler instanceof Ci.nsIWebHandlerApp, "the handler is a web handler");
  is(
    handler.uriTemplate,
    "https://example.com/browser/uriloader/exthandler/tests/mochitest/blank.html?uri=%s",
    "correct url template"
  );
  protoInfo.preferredAction = protoInfo.useHelperApp;
  protoInfo.preferredApplicationHandler = handler;
  protoInfo.alwaysAskBeforeHandling = false;
  const handlerSvc = Cc["@mozilla.org/uriloader/handler-service;1"].getService(
    Ci.nsIHandlerService
  );
  handlerSvc.store(protoInfo);

  const expectedURL =
    "https://example.com/browser/uriloader/exthandler/tests/mochitest/blank.html?uri=ftp%3A%2F%2Fdomain.com%2Fpath";

  // Middle-click a testprotocol link and check the new tab is correct
  let link = "#link";

  let promiseTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, expectedURL);
  await BrowserTestUtils.synthesizeMouseAtCenter(link, { button: 1 }, browser);
  let tab = await promiseTabOpened;
  gBrowser.selectedTab = tab;
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(expectedURL),
    "the expected URL is displayed in the location bar"
  );
  BrowserTestUtils.removeTab(tab);

  // Shift-click the testprotocol link and check the new window.
  let newWindowPromise = BrowserTestUtils.waitForNewWindow({
    url: expectedURL,
  });
  await BrowserTestUtils.synthesizeMouseAtCenter(
    link,
    { shiftKey: true },
    browser
  );
  let win = await newWindowPromise;
  await TestUtils.waitForCondition(
    () => win.gBrowser.currentURI.spec == expectedURL
  );
  is(
    win.gURLBar.value,
    UrlbarTestUtils.trimURL(expectedURL),
    "the expected URL is displayed in the location bar"
  );
  await BrowserTestUtils.closeWindow(win);

  // Click the testprotocol link and check the url in the current tab.
  let loadPromise = BrowserTestUtils.browserLoaded(browser);
  await BrowserTestUtils.synthesizeMouseAtCenter(link, {}, browser);
  await loadPromise;
  await TestUtils.waitForCondition(
    () => gURLBar.value != UrlbarTestUtils.trimURL(testURL)
  );
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(expectedURL),
    "the expected URL is displayed in the location bar"
  );

  // Cleanup.
  protoInfo.preferredApplicationHandler = null;
  handlers.removeElementAt(0);
  handlerSvc.store(protoInfo);
  // Clear preloaded permission.
  Services.perms.removeAll();
});
