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

const PLAIN_PAGE_FOR_SWIPE_NAVIGATION =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  ) + "helper_touchscreen_swipe_navigation.html";

// On Windows, nsWindow::SynthesizeNativeTouchPoint() somehow coalesces
// a move with the next one. So, wait until the move is dispatched when
// the test depends on every intermediate move.
async function synthesizeNativeTouchMoveAndWait(aBrowser, aX, aY) {
  const touchmove = BrowserTestUtils.waitForEvent(window, "touchmove", true);
  await synthesizeNativeTouch(
    aBrowser,
    aX,
    aY,
    SpecialPowers.DOMWindowUtils.TOUCH_CONTACT
  );
  await touchmove;
}

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

add_task(async function test_touchscreen_swipe_navigates_back_and_forward() {
  const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
  const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    firstPage,
    true /* waitForLoad */
  );
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false /* includeSubFrames */,
    secondPage
  );

  // Test back navigation.

  ok(gBrowser.webNavigation.canGoBack);
  ok(!gBrowser.webNavigation.canGoForward);

  const started1 = BrowserTestUtils.browserStarted(
    tab.linkedBrowser,
    firstPage
  );
  const stopped1 = BrowserTestUtils.browserStopped(
    tab.linkedBrowser,
    firstPage
  );

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started1, stopped1]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "A rightward touchscreen swipe navigates back to the previous page"
  );

  // Test forward navigation.

  ok(!gBrowser.webNavigation.canGoBack);
  ok(gBrowser.webNavigation.canGoForward);

  const started2 = BrowserTestUtils.browserStarted(
    tab.linkedBrowser,
    secondPage
  );
  const stopped2 = BrowserTestUtils.browserStopped(
    tab.linkedBrowser,
    secondPage
  );

  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

  await Promise.all([started2, stopped2]);

  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "A leftward touchscreen swipe navigates forward to the next page"
  );
  BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_touchscreen_swipe_navigation_happens_in_non_scrollable_direction_when_another_direction_is_scrollable() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";

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

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await Promise.all([started, stopped]);

    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "A touchscreen swipe navigates back to the previous page"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_does_not_happen_in_scrollable_direction() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";

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

    // Scroll slightly to the right so the page can scroll back to the left.
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      content.document.documentElement.scrollLeft = 1;
      content.document.documentElement.getBoundingClientRect();
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      URL_ROOT + "helper_swipe_gesture.html",
      "Any navigation didn't happen"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_does_not_happen_after_reversal() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";
    const thirdPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?third";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, thirdPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      thirdPage
    );

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      secondPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      secondPage
    );
    tab.linkedBrowser.goBack();
    await Promise.all([started, stopped]);

    ok(gBrowser.webNavigation.canGoBack);
    ok(gBrowser.webNavigation.canGoForward);

    // Start a forward swipe, then reverse past the starting point.
    // The reversal must cancel the swipe rather than switch to back navigation.
    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 100, 100);
    await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 300, 100);
    await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 400, 100);
    await synthesizeNativeTouch(tab.linkedBrowser, 400, 100, TOUCH_REMOVE);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "Reversing a swipe does not navigate in either direction"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_happen_after_stopping_swipe_in_the_opposite_direction() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";
    const thirdPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?third";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, thirdPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      thirdPage
    );

    const started1 = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      secondPage
    );
    const stopped1 = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      secondPage
    );
    tab.linkedBrowser.goBack();
    await Promise.all([started1, stopped1]);

    ok(gBrowser.webNavigation.canGoBack);
    ok(gBrowser.webNavigation.canGoForward);

    const started2 = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      thirdPage
    );
    const stopped2 = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      thirdPage
    );

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

    // End the swipe before it reaches the navigation threshold...
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 105, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 110, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 110, 100, TOUCH_REMOVE);

    // ...then swipe in the opposite direction.
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

    await Promise.all([started2, stopped2]);

    is(
      tab.linkedBrowser.currentURI.spec,
      thirdPage,
      "A touchscreen swipe navigates forward to the next page"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_happen_after_reversal_within_slop() {
    await SpecialPowers.pushPrefEnv({
      set: [
        // Tolerances are in inches: keep the initial 5px move within slop,
        // while allowing the subsequent 100px move to start the swipe.
        ["apz.touch_start_tolerance", 0.1],
        ["apz.touch_move_tolerance", 0.1],
      ],
    });

    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);

    // Move left within slop, so this initial movement does not prevent
    // the subsequent rightward swipe from navigating back.
    await synthesizeNativeTouch(tab.linkedBrowser, 95, 100, TOUCH_CONTACT);

    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await Promise.all([started, stopped]);

    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "A touchscreen swipe navigates back to the previous page"
    );
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_does_not_happen_after_vertical_movement() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    // Vertical movement outside slop must reject navigation for the whole block,
    // even if the finger then moves far enough horizontally to navigate back.
    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "Vertical movement prevents navigation after subsequent horizontal movement"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigates_back_after_rejected_touch_block() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    // Reject a leftward gesture with no forward history...
    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The rejected gesture does not navigate"
    );

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    // ...then verify that rejection does not carry over into the
    // next touch block.
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await Promise.all([started, stopped]);
    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "A new touch block can navigate back"
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_does_not_happen_when_touch_is_canceled() {
    await SpecialPowers.pushPrefEnv({
      set: [
        // On Windows, nsWindow::SynthesizeNativeTouchPoint() injects touches via
        // InjectTouchInput(), and nsWindow::OnTouch() converts the resulting
        // WM_TOUCH into MultiTouchInput. WM_TOUCH has no cancel flag (POINTER_FLAG_CANCELED),
        // so OnTouch() turns TOUCH_CANCEL into MULTITOUCH_END.
        // Dispatch the touches directly through nsIWidget::DispatchTouchInput() instead.
        [
          "apz.test.fails_with_native_injection",
          AppConstants.platform == "win",
        ],
      ],
    });

    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    const { TOUCH_CONTACT, TOUCH_CANCEL } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CANCEL);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "Any navigation didn't happen"
    );
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_touchscreen_swipe_navigates_back_after_cancellation() {
    await SpecialPowers.pushPrefEnv({
      set: [
        // On Windows, nsWindow::SynthesizeNativeTouchPoint() injects touches via
        // InjectTouchInput(), and nsWindow::OnTouch() converts the resulting
        // WM_TOUCH into MultiTouchInput. WM_TOUCH has no cancel flag (POINTER_FLAG_CANCELED),
        // so OnTouch() turns TOUCH_CANCEL into MULTITOUCH_END.
        // Dispatch the touches directly through nsIWidget::DispatchTouchInput() instead.
        [
          "apz.test.fails_with_native_injection",
          AppConstants.platform == "win",
        ],
      ],
    });

    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    // Cancellation must reset the touch state so a new touchscreen swipe can
    // navigate, even after the canceled gesture crossed the success threshold.
    const { TOUCH_CONTACT, TOUCH_CANCEL, TOUCH_REMOVE } =
      SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CANCEL);

    await waitForWhile();
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The canceled gesture does not navigate"
    );

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await Promise.all([started, stopped]);
    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "A new touch block can navigate back"
    );
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_scroll_page_after_stopping_swipe_in_the_middle() {
  const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";

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
    "Any navigation didn't happen"
  );

  // Scroll the page to the right. APZ starts panning on the first move and
  // scrolls from the second one, so make sure both moves are dispatched.
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 150, 100);
  await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 100, 100);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    Assert.greater(
      content.document.documentElement.scrollLeft,
      0,
      "the page is scrolled in the right"
    );
  });
  BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_touchscreen_swipe_can_be_disabled_by_touchscreen_pref() {
    await SpecialPowers.pushPrefEnv({
      set: [["widget.swipe.touchscreen.enabled", false]],
    });

    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    let swipeMayStartFired = false;
    const onSwipeMayStart = () => {
      swipeMayStartFired = true;
    };
    window.addEventListener("MozSwipeGestureMayStart", onSwipeMayStart);

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await waitForWhile();
    window.removeEventListener("MozSwipeGestureMayStart", onSwipeMayStart);

    ok(!swipeMayStartFired, "Swipe-to-nav is not considered");
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "A touchscreen swipe does not navigate when the pref is disabled"
    );
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_touchscreen_swipe_happens_only_with_a_proper_direction() {
    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;

    // When dx <= dy * 3.7, swipe-to-nav isn't triggered. See SwipeTracker::CanTriggerSwipe().

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 130, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 160, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 160, TOUCH_REMOVE);

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The gesture does not navigate."
    );

    // When dx > dy * 3.7, swipe-to-nav is triggered.

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 120, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 140, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 140, TOUCH_REMOVE);

    await Promise.all([started, stopped]);
    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "The gesture does navigate."
    );
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_touchscreen_swipe_can_be_disabled_by_swipe_tracker_pref() {
    await SpecialPowers.pushPrefEnv({
      set: [
        // Disable pref.
        ["widget.disable-swipe-tracker", true],
      ],
    });

    const firstPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?first";
    const secondPage = PLAIN_PAGE_FOR_SWIPE_NAVIGATION + "?second";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false /* includeSubFrames */,
      secondPage
    );

    ok(gBrowser.webNavigation.canGoBack);
    ok(!gBrowser.webNavigation.canGoForward);

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The gesture does not navigate because of the disabled pref"
    );
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);
