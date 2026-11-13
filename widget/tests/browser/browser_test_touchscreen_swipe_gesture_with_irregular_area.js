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
      // We need this pref to allow the synthetic mouse events to propagate to APZ,
      // and to allow the MozMouseHittest event in particular to be dispatched to
      // APZ as a MouseInput so the hit result is recorded.
      ["test.events.async.enabled", true],
      // Polygon clip-paths are only irregular areas without WebRender paths
      // since bug 2088160. Disable them as the APZ tests do in
      // gfx/layers/apz/test/mochitest/test_group_touchevents-7.html.
      ["gfx.webrender.paths", false],
      ["gfx.webrender.paths.swgl", false],
    ],
  });
});

add_task(
  async function test_swipe_is_properly_handled_with_different_scrollability_of_irregular_area() {
    for (const [innerScrollLeft, outerScrollLeft, elementToBeScrolled] of [
      [100, 0, "inner"],
      [0, 0, null],
      [0, 100, "outer"],
    ]) {
      info(
        `Swipe with inner.scrollLeft=${innerScrollLeft}, outer.scrollLeft=${outerScrollLeft}`
      );
      const firstPage = "about:about";
      const URL_ROOT = getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content/",
        "http://mochi.test:8888/"
      );
      const secondPage =
        URL_ROOT +
        `helper_touchscreen_swipe_irregular_scrollers.html?inner=${innerScrollLeft}&outer=${outerScrollLeft}`;
      const tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        firstPage
      );

      let onSwipeStart;
      try {
        BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
        await BrowserTestUtils.browserLoaded(
          tab.linkedBrowser,
          false,
          secondPage
        );
        ok(
          tab.linkedBrowser.webNavigation.canGoBack,
          "Back navigation is available"
        );

        await SpecialPowers.spawn(
          tab.linkedBrowser,
          [innerScrollLeft, outerScrollLeft],
          async (innerLeft, outerLeft) => {
            is(
              content.document.getElementById("inner").scrollLeft,
              innerLeft,
              "The query sets the inner scroll position"
            );
            is(
              content.document.getElementById("outer").scrollLeft,
              outerLeft,
              "The query sets the outer scroll position"
            );
            await content.wrappedJSObject.promiseApzFlushedRepaints();
            is(
              content.document.elementFromPoint(100, 200).id,
              "target",
              "The touch starts on the irregular region inside both scrollers"
            );

            // hitTest() in apz_test_utils.js needs SimpleTest in the page, so
            // do the hit test directly.
            const utils = SpecialPowers.getDOMWindowUtils(content);
            utils.sendMozMouseHitTestEvent(100, 200);
            const hit = utils.getCompositorAPZTestData().hitResults.at(-1);
            ok(
              !!(hit.hitResult & content.APZHitResultFlags.IRREGULAR_AREA),
              "APZ must use the main-thread fallback"
            );
          }
        );

        const finished = elementToBeScrolled
          ? BrowserTestUtils.waitForContentEvent(
              tab.linkedBrowser,
              "touchend",
              true
            )
          : Promise.all([
              BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage),
              BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage),
            ]);

        let swipeStarted = false;
        onSwipeStart = () => (swipeStarted = true);
        window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

        const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
        await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
        // APZ starts panning on the first move without scrolling, so every
        // move needs to be dispatched to check the scroll below. To prevent
        // moves from being coalesced, use synthesizeNativeTouchMoveAndWait.
        await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 200, 200);
        await synthesizeNativeTouchMoveAndWait(tab.linkedBrowser, 300, 200);
        await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);
        await finished;

        if (elementToBeScrolled) {
          await waitForWhile();

          ok(!swipeStarted, "A scrollable ancestor prevents swipe tracking");

          is(
            tab.linkedBrowser.currentURI.spec,
            secondPage,
            "Scrolling an ancestor does not navigate back"
          );

          if (tab.linkedBrowser.currentURI.spec == secondPage) {
            // Confirm that the gesture was consumed as scroll.
            await SpecialPowers.spawn(
              tab.linkedBrowser,
              [elementToBeScrolled, innerScrollLeft, outerScrollLeft],
              async (id, innerLeft, outerLeft) => {
                await content.wrappedJSObject.promiseApzFlushedRepaints();
                Assert.less(
                  content.document.getElementById(id).scrollLeft,
                  id == "inner" ? innerLeft : outerLeft,
                  `${id} consumes the gesture by scrolling toward its left edge`
                );
              }
            );
          }
        } else {
          ok(swipeStarted, "swipe should be tracked");
          // Confirm that swipe succeeded.
          is(
            tab.linkedBrowser.currentURI.spec,
            firstPage,
            "Both ancestors at their left edges allow back navigation"
          );
        }
      } finally {
        if (onSwipeStart) {
          window.removeEventListener(
            "MozSwipeGestureStart",
            onSwipeStart,
            true
          );
        }
        BrowserTestUtils.removeTab(tab);
      }
    }
  }
);

add_task(async function test_touch_action_in_irregular_area() {
  for (const touchAction of ["pan-y", "pan-x"]) {
    info(`Swipe inside the clipped element with touch-action: ${touchAction}`);
    const firstPage = "about:about";
    const URL_ROOT = getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content/",
      "http://mochi.test:8888/"
    );
    const secondPage =
      URL_ROOT + `helper_touchscreen_swipe_irregular_area.html#${touchAction}`;
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      firstPage
    );

    let onSwipeStart;
    try {
      BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, secondPage);
      await BrowserTestUtils.browserLoaded(
        tab.linkedBrowser,
        false,
        secondPage
      );
      ok(
        tab.linkedBrowser.webNavigation.canGoBack,
        "Back navigation is available"
      );

      await SpecialPowers.spawn(
        tab.linkedBrowser,
        [touchAction],
        async action => {
          const target = content.document.getElementById(action);
          await content.wrappedJSObject.promiseApzFlushedRepaints();
          is(
            content.document.elementFromPoint(100, 200),
            target,
            "The touch starts inside the nonrectangular region"
          );

          // hitTest() in apz_test_utils.js needs SimpleTest in the page, so
          // do the hit test directly.
          const utils = SpecialPowers.getDOMWindowUtils(content);
          utils.sendMozMouseHitTestEvent(100, 200);
          const hit = utils.getCompositorAPZTestData().hitResults.at(-1);
          ok(
            !!(hit.hitResult & content.APZHitResultFlags.IRREGULAR_AREA),
            "APZ must use the main-thread touch-action fallback"
          );
        }
      );

      const allowsSwipe = touchAction == "pan-x";

      const finished = allowsSwipe
        ? Promise.all([
            BrowserTestUtils.browserStarted(tab.linkedBrowser, firstPage),
            BrowserTestUtils.browserStopped(tab.linkedBrowser, firstPage),
          ])
        : BrowserTestUtils.waitForContentEvent(
            tab.linkedBrowser,
            "touchend",
            true
          );

      let swipeStarted = false;
      onSwipeStart = () => (swipeStarted = true);
      window.addEventListener("MozSwipeGestureStart", onSwipeStart, true);

      const { TOUCH_CONTACT, TOUCH_REMOVE } = SpecialPowers.DOMWindowUtils;
      await synthesizeNativeTouch(tab.linkedBrowser, 100, 200, TOUCH_CONTACT);
      await synthesizeNativeTouch(tab.linkedBrowser, 200, 200, TOUCH_CONTACT);
      await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_CONTACT);
      await synthesizeNativeTouch(tab.linkedBrowser, 300, 200, TOUCH_REMOVE);

      await finished;

      if (allowsSwipe) {
        ok(swipeStarted, `touch-action: ${touchAction} allows swipe tracking`);

        is(
          tab.linkedBrowser.currentURI.spec,
          firstPage,
          `touch-action: ${touchAction} allows back navigation after fallback`
        );
      } else {
        await waitForWhile();

        ok(
          !swipeStarted,
          `touch-action: ${touchAction} prevents swipe tracking`
        );

        is(
          tab.linkedBrowser.currentURI.spec,
          secondPage,
          `touch-action: ${touchAction} prevents back navigation after fallback`
        );
      }
    } finally {
      if (onSwipeStart) {
        window.removeEventListener("MozSwipeGestureStart", onSwipeStart, true);
      }
      BrowserTestUtils.removeTab(tab);
    }
  }
});
