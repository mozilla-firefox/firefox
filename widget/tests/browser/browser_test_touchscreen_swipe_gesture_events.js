/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_native_event_utils.js",
  this
);

// On Windows, synthesizeNativeTouch() resolves when
// nsWindow::SynthesizeNativeTouchPoint() returns after InjectTouchInput(),
// before nsWindow::OnTouch() handles the resulting WM_TOUCH. Wait until the
// page receives the touch event so that the checks below see it. Otherwise,
// sometimes touches are coalescenced.
async function synthesizeNativeTouchAndWaitForContentEvent(
  aBrowser,
  aX,
  aY,
  aType,
  aEventType
) {
  const received = BrowserTestUtils.waitForContentEvent(aBrowser, aEventType);
  await synthesizeNativeTouch(aBrowser, aX, aY, aType);
  await received;
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
      // On Windows, the OS sometimes delays delivering an injected
      // touch-down by hundreds of milliseconds somehow, and lifts an
      // injected contact that gets no further frames (see
      // InjectTouchInput() official doc on press and hold). This
      // test has to check the page's events after each touch before
      // sending the next one. So this test dispatches the touches
      // directly through nsIWidget::DispatchTouchInput() instead.
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
      ["apz.touch_start_tolerance", 0.0],
      ["apz.touch_move_tolerance", 0.0],
      ["apz.overscroll.enabled", true],
      ["apz.test.logging_enabled", true],
      // The touchstart can turn into a long-tap, and thus into a context menu
      // that cancels the pointer, if the test is slow to send the next move.
      ["ui.click_hold_context_menus.delay", 10000],
    ],
  });
});

add_task(async function test_completed_back_swipe_touch_events() {
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const firstPage = URL_ROOT + "helper_swipe_gesture.html";
  const secondPage = firstPage + "?second";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  const interceptNavigation = event => {
    // Keep the page alive to check events after the finger lifts.
    event.preventDefault();
    event.stopImmediatePropagation();
  };
  window.addEventListener("MozSwipeGesture", interceptNavigation, true);
  registerCleanupFunction(() =>
    window.removeEventListener("MozSwipeGesture", interceptNavigation, true)
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(
    tab.linkedBrowser.webNavigation.canGoBack,
    "History permits back navigation"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    const events = [];
    content.wrappedJSObject.swipeEvents = events;
    for (const type of ["touchstart", "touchmove", "touchend", "touchcancel"]) {
      content.document.addEventListener(type, event => {
        events.push(event.type);
      });
    }
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchstart"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart"],
      "Touching the screen starts the touch sequence"
    );
  });

  const started = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureStart",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    200,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await started;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove"],
      "The page receives touchmove when swipe tracking starts"
    );
  });

  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    300,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove", "touchmove"],
      "Touch events continue during the swipe"
    );
  });

  const ended = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureEnd",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    300,
    100,
    TOUCH_REMOVE,
    "touchend"
  );
  await ended;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove", "touchmove", "touchend"],
      "Lifting the finger ends the completed swipe with touchend"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_canceled_back_swipe_touch_events() {
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const firstPage = URL_ROOT + "helper_swipe_gesture.html";
  const secondPage = firstPage + "?second";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  const interceptNavigation = event => {
    // Keep the page alive to check events after the finger lifts.
    event.preventDefault();
    event.stopImmediatePropagation();
  };
  window.addEventListener("MozSwipeGesture", interceptNavigation, true);
  registerCleanupFunction(() =>
    window.removeEventListener("MozSwipeGesture", interceptNavigation, true)
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(
    tab.linkedBrowser.webNavigation.canGoBack,
    "History permits back navigation"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    const events = [];
    content.wrappedJSObject.swipeEvents = events;
    for (const type of ["touchstart", "touchmove", "touchend", "touchcancel"]) {
      content.document.addEventListener(type, event => {
        events.push(event.type);
      });
    }
    await content.wrappedJSObject.promiseApzFlushedRepaints();
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchstart"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart"],
      "Touching the screen starts the touch sequence"
    );
  });

  const started = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureStart",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    200,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await started;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove"],
      "The page receives touchmove when swipe tracking starts"
    );
  });

  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove", "touchmove"],
      "Touch events continue during the swipe"
    );
  });

  const ended = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureEnd",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_REMOVE,
    "touchend"
  );
  await ended;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["touchstart", "touchmove", "touchmove", "touchend"],
      "Lifting the finger ends the canceled swipe with touchend"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_completed_back_swipe_pointer_events() {
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const firstPage = URL_ROOT + "helper_swipe_gesture.html";
  const secondPage = firstPage + "?second";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  const interceptNavigation = event => {
    // Keep the page alive to check events after the finger lifts.
    event.preventDefault();
    event.stopImmediatePropagation();
  };
  window.addEventListener("MozSwipeGesture", interceptNavigation, true);
  registerCleanupFunction(() =>
    window.removeEventListener("MozSwipeGesture", interceptNavigation, true)
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(
    tab.linkedBrowser.webNavigation.canGoBack,
    "History permits back navigation"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    const events = [];
    content.wrappedJSObject.swipeEvents = events;
    for (const type of [
      "pointerdown",
      "pointermove",
      "pointercancel",
      "pointerout",
      "pointerup",
    ]) {
      content.document.addEventListener(type, event => {
        if (event.pointerType != "touch") {
          return;
        }
        events.push(event.type);
      });
    }
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchstart"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown"],
      "Touching the screen starts the pointer sequence"
    );
  });

  const started = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureStart",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    200,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await started;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "Swipe tracking cancels the pointer after pointermove"
    );
  });

  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    300,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "The canceled pointer receives no further moves"
    );
  });

  const ended = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureEnd",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    300,
    100,
    TOUCH_REMOVE,
    "touchend"
  );
  await ended;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "Lifting the finger does not dispatch pointerup after pointercancel"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_canceled_back_swipe_pointer_events() {
  const URL_ROOT = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const firstPage = URL_ROOT + "helper_swipe_gesture.html";
  const secondPage = firstPage + "?second";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, firstPage);

  const interceptNavigation = event => {
    // Keep the page alive to check events after the finger lifts.
    event.preventDefault();
    event.stopImmediatePropagation();
  };
  window.addEventListener("MozSwipeGesture", interceptNavigation, true);
  registerCleanupFunction(() =>
    window.removeEventListener("MozSwipeGesture", interceptNavigation, true)
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, secondPage);
  ok(
    tab.linkedBrowser.webNavigation.canGoBack,
    "History permits back navigation"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await content.wrappedJSObject.promiseApzFlushedRepaints();
    const events = [];
    content.wrappedJSObject.swipeEvents = events;
    for (const type of [
      "pointerdown",
      "pointermove",
      "pointercancel",
      "pointerout",
      "pointerup",
    ]) {
      content.document.addEventListener(type, event => {
        if (event.pointerType != "touch") {
          return;
        }
        events.push(event.type);
      });
    }
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchstart"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown"],
      "Touching the screen starts the pointer sequence"
    );
  });

  const started = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureStart",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    200,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await started;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "Swipe tracking cancels the pointer after pointermove"
    );
  });

  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_CONTACT,
    "touchmove"
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "The canceled pointer receives no further moves"
    );
  });

  const ended = BrowserTestUtils.waitForEvent(
    window,
    "MozSwipeGestureEnd",
    true
  );
  await synthesizeNativeTouchAndWaitForContentEvent(
    tab.linkedBrowser,
    100,
    100,
    TOUCH_REMOVE,
    "touchend"
  );
  await ended;
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    Assert.deepEqual(
      Array.from(content.wrappedJSObject.swipeEvents),
      ["pointerdown", "pointermove", "pointercancel", "pointerout"],
      "Lifting the finger does not dispatch pointerup after pointercancel"
    );
  });

  BrowserTestUtils.removeTab(tab);
});
