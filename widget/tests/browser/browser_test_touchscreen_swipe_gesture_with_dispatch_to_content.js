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
      // prevent content response timeout.
      ["apz.content_response_timeout", 60000],
      // Touches are processed late in these tests, so prevent them from
      // turning into long-taps.
      ["ui.click_hold_context_menus.delay", 10000],
    ],
  });
});

///// listener on touchstart. /////

add_task(async function test_prevent_default_on_touchstart_prevents_swipe() {
  await SpecialPowers.pushPrefEnv({
    // This test awaits between touches. On Windows, InjectTouchInput() sometimes
    // fails with ERROR_TIMEOUT, so dispatch the touches directly through
    // nsIWidget::DispatchTouchInput() instead.
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
    content.wrappedJSObject.preventSwipe = event => {
      event.preventDefault();
      content.wrappedJSObject.touchStartPrevented = event.defaultPrevented;
    };

    content.document.addEventListener(
      "touchstart",
      content.wrappedJSObject.preventSwipe,
      { passive: false }
    );

    content.wrappedJSObject.touchStarted = new content.Promise(resolve => {
      content.document.addEventListener("touchstart", resolve, { once: true });
    });
    content.wrappedJSObject.touchEnded = new content.Promise(resolve => {
      content.document.addEventListener("touchend", resolve, { once: true });
    });

    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  // Let APZ get the content response before the touchmoves, so that the
  // widget is likely to get the result while no input is queued.
  // The case in which the inputs are queued is tested in
  // test_delayed_prevent_default_rejects_queued_swipe in the below.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchStarted;
  });
  // Give the widget some time to receive and process the result from APZ.
  await waitForWhile();
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchEnded;
    ok(
      content.wrappedJSObject.touchStartPrevented,
      "The touchstart listener prevented the default action"
    );
  });

  ok(!swipeStarted, "Preventing touchstart prevents swipe tracking");
  window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "Preventing touchstart prevents back navigation"
  );

  // Remove the event listener and try to swipe.

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.removeEventListener(
      "touchstart",
      content.wrappedJSObject.preventSwipe
    );
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "Removing the touchstart listener allows a new gesture to navigate back"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_non_passive_touchstart_allows_swipe() {
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
    content.document.addEventListener("touchstart", () => {}, {
      passive: false,
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "An uncanceled touchstart allows back navigation"
  );

  BrowserTestUtils.removeTab(tab);
});

///// listener on touchmove. /////

add_task(async function test_prevent_default_on_touchmove_prevents_swipe() {
  await SpecialPowers.pushPrefEnv({
    // This test awaits between touches. On Windows, InjectTouchInput() sometimes
    // fails with ERROR_TIMEOUT, so dispatch the touches directly through
    // nsIWidget::DispatchTouchInput() instead.
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
    content.wrappedJSObject.preventSwipe = event => {
      event.preventDefault();
      content.wrappedJSObject.touchMovePrevented = event.defaultPrevented;
    };

    content.document.addEventListener(
      "touchmove",
      content.wrappedJSObject.preventSwipe,
      { passive: false }
    );

    content.wrappedJSObject.touchMoved = new content.Promise(resolve => {
      content.document.addEventListener("touchmove", resolve, { once: true });
    });
    content.wrappedJSObject.touchEnded = new content.Promise(resolve => {
      content.document.addEventListener("touchend", resolve, { once: true });
    });

    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  // Let APZ get the content response before the following touchmoves, so
  // that the widget is likely to get the result while only the first
  // touchmove is queued. Content holds its response until the first
  // touchmove, so the first touchmove is always queued.
  // The case in which the more inputs are queued is tested in
  // test_delayed_prevent_default_rejects_queued_swipe in the below.
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchMoved;
  });
  // Give the widget some time to receive and process the result from APZ.
  await waitForWhile();
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchEnded;
    ok(
      content.wrappedJSObject.touchMovePrevented,
      "The touchmove listener prevented the default action"
    );
  });

  ok(!swipeStarted, "Preventing the first touchmove prevents swipe tracking");
  window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "Preventing the first touchmove prevents back navigation"
  );

  // Remove the event listener and try to swipe.

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.removeEventListener(
      "touchmove",
      content.wrappedJSObject.preventSwipe
    );
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "Removing the touchmove listener allows a new gesture to navigate back"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_non_passive_touchmove_allows_swipe() {
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
    content.document.addEventListener("touchmove", () => {}, {
      passive: false,
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "An uncanceled touchmove allows back navigation"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_touchmove_listener_added_on_passive_touchstart() {
  await SpecialPowers.pushPrefEnv({
    // This test awaits between touches. On Windows, InjectTouchInput() sometimes
    // fails with ERROR_TIMEOUT, so dispatch the touches directly through
    // nsIWidget::DispatchTouchInput() instead.
    set: [
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
    ],
  });

  for (const preventDefault of [true, false]) {
    info(`Test with preventDefault: ${preventDefault}`);

    const firstPage = "about:about";
    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage = URL_ROOT + "helper_swipe_gesture.html";
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
    ok(
      tab.linkedBrowser.webNavigation.canGoBack,
      "Back navigation is available"
    );

    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [preventDefault],
      async aPreventDefault => {
        content.wrappedJSObject.touchMoved = new content.Promise(resolve => {
          const onTouchMove = event => {
            if (aPreventDefault) {
              event.preventDefault();
            }
            content.wrappedJSObject.touchMovePrevented = event.defaultPrevented;
            resolve();
          };
          content.wrappedJSObject.touchStarted = new content.Promise(
            resolveStart => {
              content.document.addEventListener(
                "touchstart",
                () => {
                  content.document.addEventListener("touchmove", onTouchMove, {
                    passive: false,
                    once: true,
                  });
                  resolveStart();
                },
                { passive: true, once: true } // Note passive is true here.
              );
            }
          );
        });
        await content.wrappedJSObject.promiseApzFlushedRepaints();
        is(content.document.documentElement.scrollLeft, 0, "At the left edge");
      }
    );

    let swipeStarted = false;
    const onSwipeStart = () => (swipeStarted = true);
    window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

    const navigated = preventDefault
      ? null
      : Promise.all([
          BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage),
          BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage),
        ]);

    const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
    await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await content.wrappedJSObject.touchStarted;
      // Make sure APZ knows about the listener added on touchstart before the
      // first touchmove arrives.
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });
    await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [preventDefault],
      async aPreventDefault => {
        await content.wrappedJSObject.touchMoved;
        is(
          content.wrappedJSObject.touchMovePrevented,
          aPreventDefault,
          aPreventDefault
            ? "The first touchmove was canceled"
            : "The first touchmove was not canceled"
        );
      }
    );
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
    await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

    if (preventDefault) {
      await waitForWhile();
    } else {
      await navigated;
    }
    window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

    is(
      swipeStarted,
      !preventDefault,
      preventDefault
        ? "Preventing the first touchmove prevents swipe tracking"
        : "An uncanceled touchmove starts swipe tracking after waiting again"
    );
    is(
      tab.linkedBrowser.currentURI.spec,
      preventDefault ? secondPage : firstPage,
      preventDefault
        ? "Preventing the first touchmove prevents back navigation"
        : "An uncanceled touchmove allows back navigation after waiting again"
    );

    BrowserTestUtils.removeTab(tab);
  }
  await SpecialPowers.popPrefEnv();
});

///// delayed response /////

// Unlike test_prevent_default_on_touchstart_prevents_swipe, the inputs are
// queued in the widget before the content response arrives.
add_task(async function test_delayed_prevent_default_rejects_queued_swipe() {
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
    content.document.addEventListener(
      "touchstart",
      event => {
        event.preventDefault();
        const start = content.performance.now();
        while (content.performance.now() - start < 3000) {
          // Keep the content response pending while the parent sends inputs.
        }
      },
      { passive: false, once: true }
    );
    content.wrappedJSObject.touchFinished = new content.Promise(resolve => {
      content.document.addEventListener("touchend", resolve, { once: true });
    });
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  let swipeStarted = false;
  const onSwipeStart = () => (swipeStarted = true);
  window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.touchFinished;
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });
  await waitForWhile();
  ok(!swipeStarted, "Replaying canceled inputs does not start swipe tracking");
  is(
    tab.linkedBrowser.currentURI.spec,
    secondPage,
    "The queued gesture does not navigate"
  );
  window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);

  // Try again and it will successfully navigate.

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "A new gesture navigates after the queued rejection"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_delayed_response_allows_queued_swipe() {
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
    content.document.addEventListener(
      "touchstart",
      () => {
        const start = content.performance.now();
        while (content.performance.now() - start < 3000) {
          // Keep the content response pending while the parent sends inputs.
        }
      },
      { passive: false, once: true }
    );
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    is(content.document.documentElement.scrollLeft, 0, "At the left edge");
  });

  const started = BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage);
  const stopped = BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage);

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouch(tab.linkedBrowser, 100, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 200, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_CONTACT);
  await synthesizeNativeTouch(tab.linkedBrowser, 300, 100, TOUCH_REMOVE);

  await Promise.all([started, stopped]);

  is(
    tab.linkedBrowser.currentURI.spec,
    firstPage,
    "Replaying the queued gesture navigates back"
  );

  BrowserTestUtils.removeTab(tab);
});
