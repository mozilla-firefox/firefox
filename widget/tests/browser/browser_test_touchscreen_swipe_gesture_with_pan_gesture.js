/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

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
      ["apz.test.logging_enabled", true],
    ],
  });
});

add_task(
  async function test_pan_scroll_after_touchscreen_swipe_ends_in_the_middle() {
    const firstPage = "about:about";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );

    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      URL_ROOT + "helper_swipe_gesture.html"
    );
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      URL_ROOT + "helper_swipe_gesture.html"
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      ok(
        content.document.documentElement.scrollLeft == 0 &&
          content.document.documentElement.scrollLeftMax > 0,
        "the page is on the left edge and it is scrollable in the right"
      );
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

    // End the swipe before it reaches the navigation threshold.
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 105, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 110, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 110, 100, TOUCH_REMOVE);

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      URL_ROOT + "helper_swipe_gesture.html",
      "The canceled swipe does not navigate"
    );

    await panRightToLeftBegin(tab.linkedBrowser, 100, 100, 1);
    await panRightToLeftEnd(tab.linkedBrowser, 100, 100);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      Assert.greater(
        content.document.documentElement.scrollLeft,
        0,
        "The touchpad scrolls after the touchscreen swipe should scroll the page"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
);

// Verify that canceling a touchscreen swipe releases the swipe tracker.
// Otherwise, it consumes the next touchpad pan as part of the canceled swipe
// instead of scrolling. See nsIWidget::HandleTouchInputAsSwipe().
add_task(async function test_pan_scroll_after_touchscreen_swipe_cancellation() {
  await SpecialPowers.pushPrefEnv({
    set: [
      // On Windows, nsWindow::SynthesizeNativeTouchPoint() injects touches via
      // InjectTouchInput(), and nsWindow::OnTouch() converts the resulting
      // WM_TOUCH into MultiTouchInput. WM_TOUCH has no cancel flag (POINTER_FLAG_CANCELED),
      // so OnTouch() turns TOUCH_CANCEL into MULTITOUCH_END.
      // Dispatch the touches directly through nsIWidget::DispatchTouchInput() instead.
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
    ],
  });

  const firstPage = "about:about";

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    firstPage,
    true /* waitForLoad */
  );

  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    URL_ROOT + "helper_swipe_gesture.html"
  );
  await BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false /* includeSubFrames */,
    URL_ROOT + "helper_swipe_gesture.html"
  );

  ok(gBrowser.webNavigation.canGoBack);
  ok(!gBrowser.webNavigation.canGoForward);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    ok(
      content.document.documentElement.scrollLeft == 0 &&
        content.document.documentElement.scrollLeftMax > 0,
      "the page is on the left edge and it is scrollable in the right"
    );
  });

  const { TOUCH_CONTACT, TOUCH_CANCEL } = SpecialPowers.DOMWindowUtils;

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CANCEL);

  await waitForWhile();

  is(
    tab.linkedBrowser.currentURI.spec,
    URL_ROOT + "helper_swipe_gesture.html",
    "The canceled swipe does not navigate"
  );

  await panRightToLeftBegin(tab.linkedBrowser, 100, 100, 1);
  await panRightToLeftEnd(tab.linkedBrowser, 100, 100);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    Assert.greater(
      content.document.documentElement.scrollLeft,
      0,
      "The touchpad scrolls after the touchscreen swipe should scroll the page"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// Verify that touchpad input during an ongoing touchscreen swipe neither
// interrupts nor scrolls, and the touchscreen swipe still completes.
add_task(
  async function test_touchpad_input_does_not_interrupt_ongoing_touchscreen_swipe() {
    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    const firstPage = secondPage + "?previous";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );
    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(tab.linkedBrowser.webNavigation.canGoBack);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      ok(
        content.document.documentElement.scrollLeft == 0 &&
          content.document.documentElement.scrollLeftMax > 0,
        "The page is at the left edge and can scroll right"
      );
    });

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await waitForWhile();

    await panLeftToRight(tab.linkedBrowser, 100, 100, 1);
    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "Touchpad input does not navigate while the finger remains on the screen"
    );

    await panRightToLeft(tab.linkedBrowser, 100, 100, 1);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      is(
        content.document.documentElement.scrollLeft,
        0,
        "Touchpad input does not scroll during the touchscreen swipe"
      );
    });

    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    const navigated = Promise.all([
      BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage),
      BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage),
    ]);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);
    await navigated;

    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "Lifting the finger completes the touchscreen swipe"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

// The reverse of the test above: touchscreen input during an ongoing touchpad
// swipe does not interrupt it, and the touchpad swipe still completes.
// Unlike touchpad input, touchscreen input can still scroll the page during the
// swipe. See bug 2088133.
add_task(
  async function test_touchscreen_input_does_not_interrupt_ongoing_touchpad_swipe() {
    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    const firstPage = secondPage + "?previous";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );
    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(tab.linkedBrowser.webNavigation.canGoBack);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      ok(
        content.document.documentElement.scrollLeft == 0 &&
          content.document.documentElement.scrollLeftMax > 0,
        "The page is at the left edge and can scroll right"
      );
    });

    await panLeftToRightBegin(tab.linkedBrowser, 100, 100, 1);
    await panLeftToRightUpdate(tab.linkedBrowser, 100, 100, 1);
    await waitForWhile();

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);
    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "Touchscreen input does not navigate while the fingers remain on the touchpad"
    );

    const navigated = Promise.all([
      BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage),
      BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage),
    ]);
    await panLeftToRightEnd(tab.linkedBrowser, 100, 100);
    await navigated;

    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "Lifting the fingers from the touchpad completes the touchpad swipe"
    );
    BrowserTestUtils.removeTab(tab);
  }
);
