/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("apz_test_native_event_utils.js", gTestPath).href,
  this
);

// A stale APZ touch count after preventDefault() on touchmove can leave the
// long-press timer running for a gesture on a different APZC, firing contextmenu
// after the finger lifts. This test reproduces this bug.
//
// Repeat in another tab to use a different APZC. A stale touch count makes APZ
// reset the previous APZC and mark the new touch block as already reset. This
// skips the reset needed to cancel the new long-press timer when touchmove is
// prevented, causing contextmenu to fire after the finger is lifted.
//
// Add the touchmove listener during touchstart so APZ processes touchstart before
// waiting for the page's response to the first touchmove. If we add the touchmove
// listener before touchstart, APZ may also delay processing touchstart until the
// page responds. Then, preventing touchmove could drop touchstart before it
// increments the touch count, so the bug would not reproduce.
add_task(async function test_prevented_touchmove_does_not_leave_a_long_press() {
  const longPressDelay = 1000;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["apz.fastpath_apz_aware_listener.enabled", true],
      ["apz.touch_start_tolerance", 0.0],
      ["apz.touch_move_tolerance", 0.0],
      ["apz.content_response_timeout", 100000],
      // On Windows, the OS sometimes delays delivering an injected
      // touch-down by hundreds of milliseconds somehow, and lifts an
      // injected contact that gets no further frames (see
      // InjectTouchInput() official doc on press and hold). This
      // test has to check the page's events after each touch before
      // sending the next one. So this test dispatches the touches
      // directly through nsIWidget::DispatchTouchInput() instead.
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
      ["ui.click_hold_context_menus.delay", longPressDelay],
    ],
  });

  const root = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    "http://mochi.test:8888/"
  );
  const tabs = [];
  registerCleanupFunction(() => {
    for (const tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  });

  const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
  for (let i = 0; i < 2; i++) {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      root + "helper_prevent_default_touchmove.html"
    );
    tabs.push(tab);
    const browser = tab.linkedBrowser;
    await SpecialPowers.spawn(browser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
      content.wrappedJSObject.contextMenus = 0;
      content.addEventListener("contextmenu", event => {
        event.preventDefault();
        content.wrappedJSObject.contextMenus++;
      });
    });

    await SpecialPowers.spawn(browser, [], () => {
      content.addEventListener(
        "touchstart",
        () => {
          content.addEventListener(
            "touchmove",
            event => {
              ok(event.cancelable, "The first touchmove is cancelable");
              event.preventDefault();
              ok(event.defaultPrevented, "The page prevents the touchmove");
            },
            { passive: false, once: true }
          );
        },
        { passive: true, once: true }
      );
    });

    const started = BrowserTestUtils.waitForContentEvent(browser, "touchstart");
    await synthesizeNativeTouch(browser, 100, 200, TOUCH_CONTACT);
    await started;

    await SpecialPowers.spawn(browser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    const moved = BrowserTestUtils.waitForContentEvent(browser, "touchmove");
    await synthesizeNativeTouch(browser, 100, 150, TOUCH_CONTACT);
    await moved;

    await SpecialPowers.spawn(browser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    const ended = BrowserTestUtils.waitForContentEvent(browser, "touchend");
    await synthesizeNativeTouch(browser, 100, 150, TOUCH_REMOVE);
    await ended;

    await SpecialPowers.spawn(browser, [], async () => {
      await content.wrappedJSObject.promiseApzFlushedRepaints();
    });

    await SpecialPowers.spawn(browser, [longPressDelay], async delay => {
      // Wait beyond the long-press deadline to catch an uncanceled timer.
      await new Promise(resolve => content.setTimeout(resolve, delay * 2));
      is(
        content.wrappedJSObject.contextMenus,
        0,
        "No contextmenu fires after the finger is lifted"
      );
    });
  }
});
