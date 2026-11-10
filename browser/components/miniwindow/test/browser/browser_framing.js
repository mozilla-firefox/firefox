/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A cropped mini window holds the page at the layout viewport size it was
// captured against (Document.setViewportSizeOverrideForMiniWindow), and zooms the visual
// viewport, which follows the popup, onto the crop. These tests check the
// content side: the zoom, where the visual viewport sits, and that the page
// keeps its layout. The pure sizing math is in xpcshell/test_MiniWindowUtils.js.

// #crop-target in mini-window-crop-target.html: a 300x200 box at (100, 60).
const CROP = { left: 100, top: 60, width: 300, height: 200 };

/**
 * cropInfo for `crop`, filled against `browser`'s viewport.
 *
 * @param {XULBrowserElement} browser
 * @param {object} [crop]
 */
function cropInfoFor(browser, crop = CROP) {
  return {
    ...crop,
    viewportWidth: browser.clientWidth,
    viewportHeight: browser.clientHeight,
    fullZoom: 1,
  };
}

/**
 * Wait until `browser`'s visual viewport sits on `crop` and contains it, with
 * the crop filling it on at least one axis. Classic root scrollbars, which
 * aren't scaled by the zoom, may take their size off the visual viewport.
 *
 * @param {XULBrowserElement} browser
 * @param {object} crop
 */
function waitForFramed(browser, crop) {
  return SpecialPowers.spawn(browser, [crop], async c => {
    let vv = content.visualViewport;
    await ContentTaskUtils.waitForCondition(() => {
      let scrollbarWidth = {};
      let scrollbarHeight = {};
      content.windowUtils.getScrollbarSizes(
        content.document.documentElement,
        scrollbarWidth,
        scrollbarHeight
      );
      return (
        Math.abs(vv.pageLeft - c.left) < 2 &&
        Math.abs(vv.pageTop - c.top) < 2 &&
        vv.width >= c.width - 2 - scrollbarWidth.value / vv.scale &&
        vv.height >= c.height - 2 - scrollbarHeight.value / vv.scale &&
        Math.min(vv.width - c.width, vv.height - c.height) < 2
      );
    }, "the visual viewport frames the crop");
  });
}

/**
 * The page's layout viewport width, which the override should hold.
 *
 * @param {XULBrowserElement} browser
 */
function layoutWidth(browser) {
  return SpecialPowers.spawn(
    browser,
    [],
    () => content.document.documentElement.clientWidth
  );
}

add_task(async function test_crop_is_framed_without_reflow() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    CROP_TARGET_URL
  );
  let info = cropInfoFor(tab.linkedBrowser);
  let originalLayoutWidth = await layoutWidth(tab.linkedBrowser);

  let miniWin = await popTabForTest(tab, info);
  let popup = [...MiniWindowManager._miniwindows][0];

  await waitForFramed(popup.browser, CROP);
  Assert.less(
    Math.abs((await layoutWidth(popup.browser)) - originalLayoutWidth),
    1,
    "the page keeps the layout width it was cropped at"
  );

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_crop_at_page_edge_is_not_clamped() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    CROP_TARGET_URL
  );
  let browser = tab.linkedBrowser;
  // A square crop flush with the right edge of a page that doesn't scroll
  // sideways. A visual viewport with the page's wide aspect couldn't sit on it.
  let crop = {
    left: browser.clientWidth - 250,
    top: 0,
    width: 250,
    height: 250,
  };

  let miniWin = await popTabForTest(tab, cropInfoFor(browser, crop));
  let popup = [...MiniWindowManager._miniwindows][0];

  await waitForFramed(popup.browser, crop);

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_returning_restores_layout_and_zoom() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    CROP_TARGET_URL
  );
  let originalLayoutWidth = await layoutWidth(tab.linkedBrowser);

  let miniWin = await popTabForTest(tab, cropInfoFor(tab.linkedBrowser));
  let popup = [...MiniWindowManager._miniwindows][0];
  await waitForFramed(popup.browser, CROP);
  await SpecialPowers.spawn(popup.browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () =>
        Services.els
          .getListenerInfoFor(content)
          .some(l => l.type === "mozvisualscroll" && l.inSystemEventGroup),
      "the scroll reveal listener is added in the mini window"
    );
  });

  popup.returnToOriginWin(false);
  await BrowserTestUtils.domWindowClosed(miniWin);

  let movedTab = gBrowser.tabs.find(
    t => t.linkedBrowser?.currentURI?.spec === CROP_TARGET_URL
  );
  Assert.ok(movedTab, "the tab returned to the origin window");
  await SpecialPowers.spawn(
    movedTab.linkedBrowser,
    [originalLayoutWidth],
    async width => {
      await ContentTaskUtils.waitForCondition(
        () =>
          !Services.els
            .getListenerInfoFor(content)
            .some(l => l.type === "mozvisualscroll" && l.inSystemEventGroup),
        "the scroll reveal listener is removed"
      );
      await ContentTaskUtils.waitForCondition(
        () => content.windowUtils.getResolution() === 1,
        "the zoom is reset"
      );
      await ContentTaskUtils.waitForCondition(
        () =>
          Math.abs(content.document.documentElement.clientWidth - width) < 1,
        "the page is laid out at the browser's size again"
      );
    }
  );

  removeTestTabs();
  assertNoMiniWindowsOpen();
});

add_task(async function test_full_tab_opens_at_default_size() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let expectedWidth = 640;
  let expectedHeight = 400;

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  let popPromise = MiniWindowManager.popTab(tab);
  let miniWin = await opened;
  let popup = await popPromise;

  Assert.ok(!popup.isCropped, "popTab makes a full-tab mini window");
  Assert.equal(miniWin.outerWidth, expectedWidth, "width is the 640px default");
  if (AppConstants.platform == "linux") {
    // The window manager adds titlebar decorations that inflate outerHeight
    Assert.greaterOrEqual(
      miniWin.outerHeight,
      expectedHeight,
      "height is at least the 400px default"
    );
  } else {
    Assert.equal(
      miniWin.outerHeight,
      expectedHeight,
      "height is the 400px default"
    );
  }

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});
