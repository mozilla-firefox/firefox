/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_native_event_utils.js",
  this
);

add_task(async () => {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: TEST_PAGE,
    },
    async browser => {
      let pipWin = await triggerPictureInPicture(browser, "with-controls");

      let receivedSwipeGestureEvents = false;
      pipWin.addEventListener("MozSwipeGestureMayStart", () => {
        receivedSwipeGestureEvents = true;
      });

      const wheelEventPromise = BrowserTestUtils.waitForEvent(pipWin, "wheel");

      // Try swiping left to right.
      await panLeftToRightBegin(pipWin, 100, 100, 100);
      await panLeftToRightEnd(pipWin, 100, 100, 100);

      // Wait a wheel event and a couple of frames to give a chance to receive
      // the MozSwipeGestureMayStart event.
      await wheelEventPromise;
      await new Promise(resolve => requestAnimationFrame(resolve));
      await new Promise(resolve => requestAnimationFrame(resolve));

      Assert.ok(
        !receivedSwipeGestureEvents,
        "No swipe gesture events observed"
      );

      let pipClosed = BrowserTestUtils.domWindowClosed(pipWin);
      pipWin.close();
      await pipClosed;
    }
  );
});

add_task(async () => {
  if (AppConstants.platform == "linux") {
    Assert.ok(
      true,
      "Touchscreen swipe is not supported on Linux, see bug 2085132."
    );
    return;
  }

  await SpecialPowers.pushPrefEnv({
    set: [
      ["widget.swipe.touchscreen.enabled", true],
      // On Windows, injected touches somehow move the PiP window, which interfere with swipes.
      // So, dispatch the touch events directly instead of injecting them through the OS.
      ["apz.test.fails_with_native_injection", AppConstants.platform == "win"],
    ],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: TEST_PAGE,
    },
    async browser => {
      let pipWin = await triggerPictureInPicture(browser, "with-controls");

      let receivedSwipeGestureEvents = false;
      pipWin.addEventListener("MozSwipeGestureMayStart", () => {
        receivedSwipeGestureEvents = true;
      });

      info("PiP window is ready for touchscreen input");

      const touchEndEventPromise = BrowserTestUtils.waitForEvent(
        pipWin,
        "touchend"
      );

      const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
      const utils = SpecialPowers.getDOMWindowUtils(pipWin);
      const scale = pipWin.devicePixelRatio;

      // Native touch input needs screen coordinates in device pixels, with
      // the origin taken from the PiP window rather than the browser window.
      function sendTouch(x, y, state) {
        utils.sendNativeTouchPoint(
          0,
          state,
          (pipWin.mozInnerScreenX + x) * scale,
          (pipWin.mozInnerScreenY + y) * scale,
          1,
          90
        );
      }

      // Try swiping left to right.
      sendTouch(100, 100, TOUCH_CONTACT);
      sendTouch(200, 100, TOUCH_CONTACT);
      sendTouch(300, 100, TOUCH_CONTACT);
      sendTouch(300, 100, TOUCH_REMOVE);

      // Wait a touchend event and a couple of frames to give a chance to receive
      // the MozSwipeGestureMayStart event.
      await touchEndEventPromise;
      info("PiP window received touchend");
      await new Promise(resolve => requestAnimationFrame(resolve));
      await new Promise(resolve => requestAnimationFrame(resolve));

      Assert.ok(
        !receivedSwipeGestureEvents,
        "No swipe gesture events observed"
      );

      let pipClosed = BrowserTestUtils.domWindowClosed(pipWin);
      pipWin.close();
      await pipClosed;
    }
  );
});
