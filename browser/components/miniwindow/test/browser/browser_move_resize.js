/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PAGE_HTML = `
  <script>
    function tryMoveResize(width, height, x, y) {
      window.resizeTo(width, height);
      window.moveTo(x, y);
    }
  </script>
  <button id="open" onclick="window.open(location.href)">Open</button>`;
const MOVE_RESIZE_URL =
  "https://example.com/document-builder.sjs?html=" +
  encodeURIComponent(PAGE_HTML);
const COOP_URL =
  "https://example.com/document-builder.sjs?headers=Cross-Origin-Opener-Policy:same-origin&html=" +
  encodeURIComponent(PAGE_HTML);

function geometry(win) {
  return {
    x: win.screenX,
    y: win.screenY,
    width: win.outerWidth,
    height: win.outerHeight,
  };
}

/**
 * Ask the page itself to move and resize its window, so that the caller is
 * web content rather than chrome.
 *
 * @param {XULBrowserElement} browser
 * @param {object} target - {x, y, width, height} in screen pixels.
 */
async function pageMoveResize(browser, target) {
  await SpecialPowers.spawn(browser, [target], ({ x, y, width, height }) => {
    content.wrappedJSObject.tryMoveResize(width, height, x, y);
  });
}

function targetFor(win) {
  return {
    x: win.screen.availLeft + 10,
    y: win.screen.availTop + 10,
    width: win.outerWidth - 60,
    height: win.outerHeight - 60,
  };
}

async function assertMoveResizeRefused(win, browser, msg) {
  let before = geometry(win);
  await pageMoveResize(browser, targetFor(win));
  for (let i = 0; i < 5; i++) {
    await new Promise(r => win.requestAnimationFrame(r));
  }
  Assert.deepEqual(geometry(win), before, msg);
}

add_task(async function test_page_cannot_move_or_resize_mini_window() {
  let originWin = await BrowserTestUtils.openNewBrowserWindow();
  let openerBrowser = originWin.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(openerBrowser, MOVE_RESIZE_URL);
  await BrowserTestUtils.browserLoaded(openerBrowser);

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    originWin.gBrowser,
    MOVE_RESIZE_URL,
    true
  );
  await BrowserTestUtils.synthesizeMouseAtCenter("#open", {}, openerBrowser);
  let tab = await newTabPromise;
  Assert.equal(originWin.gBrowser.tabs.length, 2, "Origin window has 2 tabs");

  let miniWin = await popWholeTabForTest(tab);
  let popup = [...MiniWindowManager._miniwindows][0];
  let browser = miniWin.gBrowser.selectedBrowser;
  let bc = browser.browsingContext;
  Assert.ok(bc.isMiniWindow, "Browsing context is flagged as a mini window");
  Assert.ok(!bc.hasSiblings, "Tab has no siblings in the mini window");

  await assertMoveResizeRefused(
    miniWin,
    browser,
    "Page can't move or resize the mini window"
  );

  // Now load a page with COOP instead, which forces a new browsing context and checks
  // that the flag gets copied over to it.
  let loaded = BrowserTestUtils.browserLoaded(browser, false, COOP_URL);
  BrowserTestUtils.startLoadingURIString(browser, COOP_URL);
  await loaded;
  Assert.notEqual(
    browser.browsingContext,
    bc,
    "COOP navigation replaced the browsing context"
  );
  bc = browser.browsingContext;
  Assert.ok(bc.isMiniWindow, "Flag survives browsing context replacement");
  await assertMoveResizeRefused(
    miniWin,
    browser,
    "Page can't move or resize the mini window after navigating"
  );

  bc.isMiniWindow = false;
  let target = targetFor(miniWin);
  await pageMoveResize(browser, target);
  await TestUtils.waitForCondition(
    () =>
      miniWin.outerWidth == target.width &&
      miniWin.outerHeight == target.height,
    "Without the flag the page can resize its window"
  );
  bc.isMiniWindow = true;

  let closed = BrowserTestUtils.domWindowClosed(miniWin);
  popup.returnToOriginWin(false);
  await closed;
  let returned = originWin.gBrowser.tabs.find(
    t => t.linkedBrowser.currentURI.spec == COOP_URL
  );
  Assert.ok(returned, "Tab is back in its origin window");
  Assert.ok(
    !returned.linkedBrowser.browsingContext.isMiniWindow,
    "Flag is cleared once the tab is back in its origin window"
  );

  await BrowserTestUtils.closeWindow(originWin);
  await waitForNoMiniWindowsOpen();
});
