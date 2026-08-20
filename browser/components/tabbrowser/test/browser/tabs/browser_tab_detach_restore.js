"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

add_task(async function () {
  let uri =
    // eslint-disable-next-line sdl/no-insecure-url
    "http://example.com/browser/browser/components/tabbrowser/test/browser/tabs/dummy_page.html";

  // Clear out the closed windows set to start
  while (SessionStore.getClosedWindowCount() > 0) {
    SessionStore.forgetClosedWindow(0);
  }

  let tab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, uri);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, uri);
  await TabStateFlusher.flush(tab.linkedBrowser);

  let key = tab.linkedBrowser.permanentKey;
  let win = gBrowser.replaceTabWithWindow(tab);
  await TestUtils.topicObserved(
    "browser-delayed-startup-finished",
    subject => subject == win
  );

  is(
    win.gBrowser.selectedBrowser.permanentKey,
    key,
    "Should have properly copied the permanentKey"
  );
  await BrowserTestUtils.closeWindow(win);

  is(
    SessionStore.getClosedWindowCount(),
    1,
    "Should have restore data for the closed window"
  );

  win = SessionStore.undoCloseWindow(0);
  await BrowserTestUtils.waitForEvent(win, "load");
  await BrowserTestUtils.waitForEvent(
    win.gBrowser.tabContainer,
    "SSTabRestored"
  );

  is(win.gBrowser.tabs.length, 1, "Should have restored one tab");
  is(
    win.gBrowser.selectedBrowser.currentURI.spec,
    uri,
    "Should have restored the right page"
  );

  win.close();
});
