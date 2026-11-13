/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_utils.js",
  this
);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_native_event_utils.js",
  this
);

async function waitForWhile() {
  await new Promise(resolve => {
    requestIdleCallback(resolve, { timeout: 300 });
  });
  await new Promise(r => requestAnimationFrame(r));
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.gesture.swipe.left", "Browser:BackOrBackDuplicate"],
      ["browser.gesture.swipe.right", "Browser:ForwardOrForwardDuplicate"],
      ["widget.disable-swipe-tracker", false],
      ["widget.swipe.touchscreen.enabled", true],
      ["widget.swipe.velocity-twitch-tolerance", 0.0000001],
      ["widget.swipe.success-velocity-contribution", 0.0],
      ["widget.swipe.pixel-size", 100.0],
      ["apz.touch_start_tolerance", 0.0],
      ["apz.touch_move_tolerance", 0.0],
      ["apz.overscroll.enabled", true],
      // prevent content response timeout.
      ["apz.content_response_timeout", 60000],
      // Touches are processed late in these tests, so prevent them from
      // turning into long-taps.
      ["ui.click_hold_context_menus.delay", 10000],
    ],
  });
});

// A tap whose touchstart listener keeps the content main thread busy for 3 sec.
// While the listener runs, content cannot handle later touch events, so APZ
// cannot confirm the target APZC of the touch blocks that follow the tap until
// then.
// This tap is needed because content sends the target of a block before
// dispatching its touchstart to DOM (See BrowserChild::RecvRealTouchEvent), so
// a busy listener cannot delay the target of its own block.
async function touchToKeepContentBusy(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    content.document.addEventListener(
      "touchstart",
      () => {
        const start = content.performance.now();
        while (content.performance.now() - start < 3000) {
          // Keep the content process busy while the parent sends inputs.
        }
      },
      { passive: false, once: true }
    );
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(browser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(browser, 100, 100, TOUCH_REMOVE);
}

// The first swipe is still queued in the widget when the second swipe starts,
// because the busy main thread cannot confirm the target of the first swipe.
// The second swipe discards the first one, so the first one doesn't navigate
// back, and the second one navigates forward once its own target is confirmed.
add_task(async function test_new_block_discards_waiting_block() {
  const firstPage = "about:about";
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const secondPage = URL_ROOT + "helper_touchscreen_swipe_navigation.html";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);

  const thirdPage = secondPage + "?third=true";
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, thirdPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, thirdPage);

  // Go back to the second page.
  const backStarted = BrowserTestUtils.browserStarted(
    tab.linkedBrowser,
    secondPage
  );
  const backStopped = BrowserTestUtils.browserStopped(
    tab.linkedBrowser,
    secondPage
  );
  tab.linkedBrowser.goBack();
  await Promise.all([backStarted, backStopped]);

  // We can go both back and forward.
  ok(tab.linkedBrowser.webNavigation.canGoBack, "Back navigation is available");
  ok(
    tab.linkedBrowser.webNavigation.canGoForward,
    "Forward navigation is available"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
    is(content.document.documentElement.scrollLeftMax, 0, "At the right edge");
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, thirdPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, thirdPage);

  // Non-passive listener makes target unconfirmed in APZ.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.addEventListener("touchstart", () => {}, {
      passive: false,
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  await touchToKeepContentBusy(tab.linkedBrowser);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

  // The first swipe, which is queued while the widget waits for APZ.
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  // The second swipe. It cancels the first one by replacing the queue.
  // This should succeed to navigate forward.
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    thirdPage,
    "The second swipe navigates forward"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_second_finger_while_waiting_prevents_swipe() {
  await SpecialPowers.pushPrefEnv({
    // Without the pref, this test fails. On Windows, InjectTouchInput() expects
    // each call to contain all active contacts (See the official doc), but
    // nsWindow::InjectTouchPoint() passes only the one being changed. So
    // injecting a second finger might fail. So, dispatch the touches directly
    // through nsIWidget::DispatchTouchInput() instead.
    set: [
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
    ],
  });

  const firstPage = "about:about";
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const secondPage = URL_ROOT + "helper_swipe_gesture.html";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(tab.linkedBrowser.webNavigation.canGoBack, "Back navigation is available");

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    // Resolved when all fingers are lifted for the second time (the first time
    // is touchend triggered by touchToKeepContentBusy()).
    content.wrappedJSObject.touchFinished = new content.Promise(resolve => {
      let count = 0;
      const finish = event => {
        if (!event.touches.length && ++count == 2) {
          content.document.removeEventListener("touchend", finish);
          resolve();
        }
      };
      content.document.addEventListener("touchend", finish);
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();

    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  // Non-passive listener makes target unconfirmed in APZ.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.addEventListener("touchstart", () => {}, {
      passive: false,
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  await touchToKeepContentBusy(tab.linkedBrowser);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  // Put and remove the second finger. It will cancel the swipe.
  await synthesizeNativeTouch(
    tab.linkedBrowser,
    100,
    160,
    TOUCH_CONTACT,
    null,
    1
  );
  await synthesizeNativeTouch(
    tab.linkedBrowser,
    100,
    160,
    TOUCH_REMOVE,
    null,
    1
  );
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  // Confirm the swipe didn't occur.
  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.touchFinished;
    });
    await waitForWhile();
    ok(
      !swipeStarted,
      `Putting second finger prevents the queued swipe from starting`
    );
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      `Putting second finger prevents navigation`
    );
  } finally {
    window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);
  }

  // Confirm that scroll succeeds. When the widget rejects the swipe by receiving the
  // second finger touch during waiting for APZ, if the widget didn't send the browser
  // gesture response to APZ, APZ couldn't start a scroll.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    Assert.greater(
      content.document.documentElement.scrollLeft,
      0,
      "A new one-finger gesture scrolls after the queued rejection"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
