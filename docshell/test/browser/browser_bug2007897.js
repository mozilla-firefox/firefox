/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// Restore a page containing a cross-origin about:blank iframe
add_task(async function test_restore_with_cross_origin_blank_iframe() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/browser/docshell/test/browser/file_2007897.html"
  );

  const blankLoaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
    wantLoad: "about:blank",
    includeSubFrames: true,
  });
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const ifr = content.document.querySelector("iframe");
    await SpecialPowers.spawn(ifr, [], () => {
      // Must use wrappedJSObject so we use the right content principal
      content.wrappedJSObject.location = "about:blank";
    });
  });
  await blankLoaded;

  await TabStateFlusher.flush(tab.linkedBrowser);

  const sessionStoreClosedObjectsChanged = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  gBrowser.removeTab(tab);
  await sessionStoreClosedObjectsChanged;
  const restoredTab = SessionStore.undoCloseTab(window, 0);
  await BrowserTestUtils.waitForEvent(restoredTab, "SSTabRestored");

  // The crash doesn't cause the test to fail, so check if the iframe was restored
  await SpecialPowers.spawn(restoredTab.linkedBrowser, [], async () => {
    const ifr = content.document.querySelector("iframe");
    Assert.ok(ifr, "did not crash");
    Assert.ok(!ifr.contentDocument && !ifr.document, "iframe is restricted");
  });

  BrowserTestUtils.removeTab(restoredTab);
});
