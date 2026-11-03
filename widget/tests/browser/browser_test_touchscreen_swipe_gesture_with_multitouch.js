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
      // On Windows, InjectTouchInput() expects each call to contain all active
      // contacts (See the official doc), but nsWindow::InjectTouchPoint() passes
      // only the one being changed. So injecting a second finger fails. So, dispatch
      // the touches directly through nsIWidget::DispatchTouchInput() instead.
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
      ["apz.touch_start_tolerance", 0.0],
      ["apz.touch_move_tolerance", 0.0],
      ["apz.overscroll.enabled", true],
      ["apz.test.logging_enabled", true],
    ],
  });
});

add_task(async function test_two_finger_gesture_prevents_navigation() {
  const firstPage = "about:about";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const secondPage = URL_ROOT + "helper_swipe_gesture.html";
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(gBrowser.webNavigation.canGoBack);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  await synthesizeNativeTouchSequences(
    tab.linkedBrowser,
    [
      [
        { x: 100, y: 100 },
        { x: 100, y: 160 },
      ],
      [
        { x: 300, y: 100 },
        { x: 300, y: 160 },
      ],
      [
        { x: 400, y: 100 },
        { x: 400, y: 160 },
      ],
    ],
    null,
    [0, 1]
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  await waitForWhile();

  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "The multi-finger gesture did not navigate"
  );

  // Check if the widget can still process touch events properly.

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    Assert.greater(
      content.document.documentElement.scrollLeft,
      0,
      "A new touch gesture scrolls after the multi-finger gesture"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_adding_second_finger_before_movement_prevents_navigation() {
    const firstPage = "about:about";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(gBrowser.webNavigation.canGoBack);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      is(content.document.documentElement.scrollLeft, 0, "At the left edge");
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
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
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The multi-finger gesture did not navigate"
    );

    // Check if the widget can still process touch events properly.

    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      Assert.greater(
        content.document.documentElement.scrollLeft,
        0,
        "A new touch gesture scrolls after the multi-finger gesture"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_adding_second_finger_during_movement_prevents_navigation() {
    const firstPage = "about:about";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(gBrowser.webNavigation.canGoBack);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      is(content.document.documentElement.scrollLeft, 0, "At the left edge");
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    const started = BrowserTestUtils.waitForEvent(
      window,
      "MozSwipeGestureStart",
      true
    );
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await started;
    ok(true, "Swipe tracking started before adding the second finger");

    const ended = BrowserTestUtils.waitForEvent(
      window,
      "MozSwipeGestureEnd",
      true
    );
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      200,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await ended;
    ok(true, "Adding the second finger ended the swipe");

    await synthesizeNativeTouch(
      tab.linkedBrowser,
      200,
      160,
      TOUCH_REMOVE,
      null,
      1
    );
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The multi-finger gesture did not navigate"
    );

    // Check if the widget can still process touch events properly.

    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      Assert.greater(
        content.document.documentElement.scrollLeft,
        0,
        "A new touch gesture scrolls after the multi-finger gesture"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_adding_and_moving_second_finger_prevents_navigation() {
    const firstPage = "about:about";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(gBrowser.webNavigation.canGoBack);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      is(content.document.documentElement.scrollLeft, 0, "At the left edge");
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      200,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      150,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      200,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      150,
      160,
      TOUCH_REMOVE,
      null,
      1
    );
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The multi-finger gesture did not navigate"
    );

    // Check if the widget can still process touch events properly.

    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      Assert.greater(
        content.document.documentElement.scrollLeft,
        0,
        "A new touch gesture scrolls after the multi-finger gesture"
      );
    });

    BrowserTestUtils.removeTab(tab);
  }
);

add_task(
  async function test_single_finger_swipe_navigates_after_multi_finger_gesture() {
    const firstPage = "about:about";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(gBrowser.webNavigation.canGoBack);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      is(content.document.documentElement.scrollLeft, 0, "At the left edge");
    });

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      200,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      250,
      160,
      TOUCH_CONTACT,
      null,
      1
    );
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_REMOVE);
    await synthesizeNativeTouch(
      tab.linkedBrowser,
      250,
      160,
      TOUCH_REMOVE,
      null,
      1
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    await waitForWhile();

    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The multi-finger gesture did not navigate"
    );

    // Check if a new single-finger swipe can still navigate.

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
      "A single-finger swipe after the multi-finger gesture navigates back"
    );

    BrowserTestUtils.removeTab(tab);
  }
);
