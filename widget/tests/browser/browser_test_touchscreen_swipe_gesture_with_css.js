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

// These values are based on `helper_touchscreen_swipe_overscroll.html` used in this test.
const ROOT_SWIPE_Y = 360;
const INNER_SWIPE_Y = 100;

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

async function waitForWhile() {
  await new Promise(resolve => {
    requestIdleCallback(resolve, { timeout: 300 });
  });
  await new Promise(r => requestAnimationFrame(r));
}

add_task(
  async function test_swipe_does_not_trigger_when_touch_action_ban_horizontal_pan() {
    const firstPage = "about:about";

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage,
      true /* waitForLoad */
    );

    try {
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

      // With touch-action, swipe-to-nav doesn't happen.

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        // Set `touch-action: pan-y` and flush it.
        content.document.documentElement.style.touchAction = "pan-y";
        content.document.documentElement.getBoundingClientRect();
        await content.wrappedJSObject.promiseApzFlushedRepaints();
        is(
          content.document.documentElement.scrollLeft,
          0,
          "the page is on the left edge"
        );
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

      // Without touch-action, swipe-to-nav happens.

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        // Remove `touch-action: pan-y` and flush it.
        content.document.documentElement.style.touchAction = "auto";
        content.document.documentElement.getBoundingClientRect();
        await content.wrappedJSObject.promiseApzFlushedRepaints();
        is(
          content.document.documentElement.scrollLeft,
          0,
          "the page is on the left edge"
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
    } finally {
      BrowserTestUtils.removeTab(tab);
    }
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_happens_with_overscroll_behavior_auto_and_chain() {
    for (const [elemWithOverscrollBehavior, touchedElement] of [
      ["html", "root-content"],
      ["#inner", "inner-content"],
      ["#outer", "inner-content"],
    ]) {
      const touchY =
        touchedElement == "root-content" ? ROOT_SWIPE_Y : INNER_SWIPE_Y;
      for (const behavior of ["auto", "chain"]) {
        info(
          `Swipe on ${touchedElement} with ${elemWithOverscrollBehavior} overscroll-behavior-x: ${behavior}`
        );

        const URL_ROOT = getRootDirectory(gTestPath).replace(
          "chrome://mochitests/content/",
          "http://mochi.test:8888/"
        );
        const secondPage =
          URL_ROOT + "helper_touchscreen_swipe_overscroll.html";
        const firstPage = secondPage + "?previous";

        const tab = await BrowserTestUtils.openNewForegroundTab(
          gBrowser,
          firstPage,
          true /* waitForLoad */
        );
        try {
          BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
          await BrowserTestUtils.browserLoaded(
            tab.linkedBrowser,
            false /* includeSubFrames */,
            secondPage
          );

          ok(gBrowser.webNavigation.canGoBack);
          ok(!gBrowser.webNavigation.canGoForward);

          await SpecialPowers.spawn(
            tab.linkedBrowser,
            [elemWithOverscrollBehavior, behavior, touchY, touchedElement],
            async (target, value, y, touchedElementId) => {
              const scroller = content.document.querySelector(target);
              scroller.classList.add(`${value}-x`);

              is(
                content.getComputedStyle(scroller).overscrollBehaviorX,
                value,
                "The overscroll behavior is applied"
              );
              ok(
                scroller.scrollLeft == 0 && scroller.scrollLeftMax > 0,
                "The scroller is at its left edge and can scroll to the right"
              );
              is(
                content.document.elementFromPoint(100, y).id,
                touchedElementId,
                "The swipe starts inside the intended scroller"
              );
              await content.wrappedJSObject.promiseApzFlushedRepaints();
            }
          );

          const started = BrowserTestUtils.browserStarted(
            tab.linkedBrowser,
            firstPage
          );
          const stopped = BrowserTestUtils.browserStopped(
            tab.linkedBrowser,
            firstPage
          );

          const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            100,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            200,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            300,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            300,
            touchY,
            TOUCH_REMOVE
          );

          await Promise.all([started, stopped]);
          is(
            tab.linkedBrowser.currentURI.spec,
            firstPage,
            `${behavior} on ${elemWithOverscrollBehavior} allows touchscreen swipe navigation`
          );
        } finally {
          BrowserTestUtils.removeTab(tab);
        }
      }
    }
  }
);

add_task(
  async function test_touchscreen_swipe_navigation_does_not_happen_with_overscroll_behavior_contain_and_none() {
    for (const [elemWithOverscrollBehavior, touchedElement] of [
      ["html", "root-content"],
      ["#inner", "inner-content"],
      ["#outer", "inner-content"],
    ]) {
      const touchY =
        touchedElement == "root-content" ? ROOT_SWIPE_Y : INNER_SWIPE_Y;
      for (const behavior of ["contain", "none"]) {
        info(
          `Swipe on ${touchedElement} with ${elemWithOverscrollBehavior} overscroll-behavior-x: ${behavior}`
        );
        const URL_ROOT = getRootDirectory(gTestPath).replace(
          "chrome://mochitests/content/",
          "http://mochi.test:8888/"
        );
        const secondPage =
          URL_ROOT + "helper_touchscreen_swipe_overscroll.html";
        const firstPage = secondPage + "?previous";

        const tab = await BrowserTestUtils.openNewForegroundTab(
          gBrowser,
          firstPage,
          true /* waitForLoad */
        );
        try {
          BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
          await BrowserTestUtils.browserLoaded(
            tab.linkedBrowser,
            false /* includeSubFrames */,
            secondPage
          );

          ok(gBrowser.webNavigation.canGoBack);
          ok(!gBrowser.webNavigation.canGoForward);

          await SpecialPowers.spawn(
            tab.linkedBrowser,
            [elemWithOverscrollBehavior, behavior, touchY, touchedElement],
            async (target, value, y, touchedElementId) => {
              const scroller = content.document.querySelector(target);
              scroller.classList.add(`${value}-x`);

              is(
                content.getComputedStyle(scroller).overscrollBehaviorX,
                value,
                "The overscroll behavior is applied"
              );
              ok(
                scroller.scrollLeft == 0 && scroller.scrollLeftMax > 0,
                "The scroller is at its left edge and can scroll to the right"
              );
              is(
                content.document.elementFromPoint(100, y).id,
                touchedElementId,
                "The swipe starts inside the intended scroller"
              );
              await content.wrappedJSObject.promiseApzFlushedRepaints();
            }
          );

          const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            100,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            200,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            300,
            touchY,
            TOUCH_CONTACT
          );
          await synthesizeNativeTouch(
            tab.linkedBrowser,
            300,
            touchY,
            TOUCH_REMOVE
          );

          await waitForWhile();
          is(
            tab.linkedBrowser.currentURI.spec,
            secondPage,
            `${behavior} on ${elemWithOverscrollBehavior} prevents touchscreen swipe navigation`
          );
        } finally {
          BrowserTestUtils.removeTab(tab);
        }
      }
    }
  }
);
