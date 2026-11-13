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
      ["apz.test.logging_enabled", true],
    ],
  });
});

// If APZ times out waiting for the content response, swipe is rejected rather
// than started based on APZ's guess, because navigation discards the current
// page.
add_task(async function test_content_response_timeout_rejects_swipe() {
  const URL_ROOT =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    ) + "helper_touchscreen_swipe_navigation.html";

  const firstPage = URL_ROOT + "?first";
  const secondPage = URL_ROOT + "?second";

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

  // A non-passive listener makes APZ wait for the content response.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.addEventListener("touchstart", () => {}, {
      passive: false,
    });
    content.wrappedJSObject.touchEnded = new content.Promise(resolve => {
      content.document.addEventListener("touchend", resolve, { once: true });
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  await SpecialPowers.pushPrefEnv({
    // Make APZ time out right after touchstart without waiting for the
    // main thread.
    set: [["apz.content_response_timeout", 0]],
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchEnded;
  });
  await waitForWhile();
  window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

  ok(!swipeStarted, "The content response timeout prevents swipe tracking");
  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "The content response timeout prevents back navigation"
  );

  await SpecialPowers.popPrefEnv();

  // Try again without the timeout and it will successfully navigate.

  await SpecialPowers.pushPrefEnv({
    set: [["apz.content_response_timeout", 60000]],
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "A swipe navigates back if the content response arrives in time"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// In an irregular area, APZ needs the main thread to tell the target and
// touch-action. If they do not arrive in time, swipe is rejected rather than
// started based on APZ's guess.
add_task(
  async function test_main_thread_timeout_in_irregular_area_rejects_swipe() {
    await SpecialPowers.pushPrefEnv({
      set: [
        // Needed to check the hit result below.
        ["test.events.async.enabled", true],
      ],
    });

    const URL_ROOT =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content/",
        "http://mochi.test:8888/"
      ) + "helper_touchscreen_swipe_navigation.html";

    const firstPage = URL_ROOT + "?first";
    const secondPage = URL_ROOT + "?second";

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

    // Add an irregular area.
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const masked = content.document.createElement("img");
      masked.src =
        "data:image/svg+xml," +
        encodeURIComponent(
          "<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'>" +
            "<rect width='1' height='1' fill='green'/>" +
            "</svg>"
        );
      masked.style.cssText =
        "position: absolute; top: 0; left: 0; width: 400px; height: 400px;" +
        "mask-image: linear-gradient(black, transparent);";
      content.document.body.prepend(masked);
      content.wrappedJSObject.touchEnded = new content.Promise(resolve => {
        content.document.addEventListener("touchend", resolve, { once: true });
      });
      await content.wrappedJSObject.promiseApzFlushedRepaints();

      // hitTest() in apz_test_utils.js needs SimpleTest in the page, so
      // do the hit test directly.
      const utils = SpecialPowers.getDOMWindowUtils(content);
      utils.sendMozMouseHitTestEvent(100, 200);
      const hit = utils.getCompositorAPZTestData().hitResults.at(-1);
      ok(
        !!(hit.hitResult & content.APZHitResultFlags.IRREGULAR_AREA),
        "The touch starts in an irregular area"
      );
    });

    await SpecialPowers.pushPrefEnv({
      // Make APZ time out right after touchstart without waiting for the
      // main thread.
      set: [["apz.content_response_timeout", 0]],
    });

    let swipeStarted = false;
    const onSwipeStart = () => (swipeStarted = true);
    window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.touchEnded;
    });
    await waitForWhile();
    window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

    ok(!swipeStarted, "The main thread timeout prevents swipe tracking");
    is(
      tab.linkedBrowser.currentURI.spec,
      secondPage,
      "The main thread timeout prevents back navigation"
    );

    await SpecialPowers.popPrefEnv();

    // Try again without the timeout and it will successfully navigate.

    await SpecialPowers.pushPrefEnv({
      set: [["apz.content_response_timeout", 60000]],
    });

    const started = BrowserTestUtils.browserStarted(
      tab.linkedBrowser,
      firstPage
    );
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      firstPage
    );

    await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

    await Promise.all([started, stopped]);

    is(
      tab.linkedBrowser.currentURI.spec,
      firstPage,
      "A swipe navigates back if the main thread responds in time"
    );

    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
);

// The content holds its response for the touchstart until it receives
// the touchmove (bug 2043764). So, while the touch stays within slop,
// i.e., while the content doesn't receive the touchmove, content doesn't
// respond. So, until the content response times out, APZ cannot process
// the touchstart and a long tap cannot happen.
// Even if the long delay (ui_click_hold_context_menus_delay) is shorter
// than the content response duration (apz_content_response_timeout), the
// timeout is forced by InputQueue::MaybeLongTapTimeout().
// Therefore, considering that the content response timeout rejects swipe,
// swipe is always rejected after a long tap. That doesn't depend on
// whether the cotext menu is opened or not.
add_task(async function test_long_tap_rejects_swipe() {
  if (AppConstants.platform == "win") {
    // On Windows, the contextmenu event, which this test uses to know when the
    // long tap happens, fires after lifting the finger.
    ok(true, "Skip this test on Windows.");
    return;
  }

  await SpecialPowers.pushPrefEnv({
    set: [
      ["apz.content_response_timeout", 60000],
      ["ui.click_hold_context_menus.delay", 100],
    ],
  });

  const URL_ROOT =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    ) + "helper_touchscreen_swipe_navigation.html";

  const firstPage = URL_ROOT + "?first";
  const secondPage = URL_ROOT + "?second";

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

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    // A non-passive listener makes APZ wait for the content response.
    content.document.addEventListener("touchstart", () => {}, {
      passive: false,
    });
    // Prevent the context menu from opening, otherwise APZ treats the touch
    // block as prevented.
    content.wrappedJSObject.longTapped = new content.Promise(resolve => {
      content.document.addEventListener(
        "contextmenu",
        event => {
          event.preventDefault();
          resolve();
        },
        { once: true }
      );
    });
    content.wrappedJSObject.touchEnded = new content.Promise(resolve => {
      content.document.addEventListener("touchend", resolve, { once: true });
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.longTapped;
  });
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchEnded;
  });
  await waitForWhile();
  window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

  ok(!swipeStarted, "A long tap prevents swipe tracking");
  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "A long tap prevents back navigation"
  );

  await SpecialPowers.popPrefEnv();

  // Just in case, try again without a long tap and it will successfully navigate.
  // Set the longer ui.click_hold_context_menus.delay just in case.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["apz.content_response_timeout", 60000],
      ["ui.click_hold_context_menus.delay", 10000],
    ],
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "A swipe navigates back without a long tap"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
