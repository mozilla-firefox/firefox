/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for touch input on the screenshots overlay, in both the screenshots
 * and mini window modes.
 *
 * Synthesized touch events produce touch and pointer events, but unlike a tap
 * on a real touchscreen they never produce a click. These tests therefore
 * cover the overlay's touch and pointer event handling, and can't catch a
 * regression in how a real device turns a tap into a click.
 */

ChromeUtils.defineESModuleGetters(this, {
  MiniWindowManager:
    "moz-src:///browser/components/miniwindow/MiniWindowManager.sys.mjs",
  ScreenshotsUtils:
    "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
});

const { SELECTION_MODES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/screenshots/ScreenshotsSelectionModes.sys.mjs"
);

const CONTENT_EVENTS = [
  "touchstart",
  "touchmove",
  "touchend",
  "pointerdown",
  "pointermove",
  "pointerup",
  "mousedown",
  "mousemove",
  "mouseup",
  "click",
];

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.mini-window.enabled", true]],
  });
});

async function startOverlay(browser, mode) {
  let helper = new ScreenshotsHelper(browser);
  let reason =
    mode === SELECTION_MODES.SCREENSHOTS ? "ToolbarButton" : "MiniWindowTest";
  ScreenshotsUtils.start(browser, reason, { mode });
  await TestUtils.waitForCondition(
    async () =>
      (await helper.isOverlayInitialized()) &&
      (await SpecialPowers.spawn(browser, [], () => {
        return content.windowGlobalChild.getActor("ScreenshotsComponent")
          .overlay.mode;
      })) === mode,
    `Waiting for the overlay to be showing in ${mode} mode`
  );
  return helper;
}

async function exitOverlay(browser, helper) {
  ScreenshotsUtils.exit(browser);
  await TestUtils.waitForCondition(
    async () => !(await helper.isOverlayInitialized()),
    "Waiting for the overlay to be torn down"
  );
}

function recordContentEvents(browser) {
  return SpecialPowers.spawn(browser, [CONTENT_EVENTS], eventTypes => {
    content.touchTestEvents = [];
    for (let type of eventTypes) {
      content.addEventListener(
        type,
        event => content.touchTestEvents.push(event.type),
        true
      );
    }
  });
}

async function assertNoContentEvents(browser) {
  let received = await SpecialPowers.spawn(
    browser,
    [],
    () => content.touchTestEvents
  );
  Assert.deepEqual(
    received,
    [],
    "No touch, pointer or mouse events reached the content document"
  );
}

for (let mode of [SELECTION_MODES.SCREENSHOTS, SELECTION_MODES.MINI_WINDOW]) {
  add_task(async function test_touch_region_gestures() {
    info(`Testing touch gestures in ${mode} mode`);
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: TEST_PAGE },
      async browser => {
        await recordContentEvents(browser);
        let helper = await startOverlay(browser, mode);

        await helper.touchDragOverlay(10, 10, 500, 500);
        let dimensions = await helper.getSelectionRegionDimensions();
        // Synthesized touch points snap to device pixels, so at a fractional
        // devicePixelRatio the region can be off by a fraction of a CSS pixel.
        isfuzzy(
          dimensions.width,
          490,
          1,
          "Dragging selected a 490px wide region"
        );
        isfuzzy(
          dimensions.height,
          490,
          1,
          "Dragging selected a 490px tall region"
        );

        await helper.touchDragOverlay(255, 255, 305, 305, "selected");
        dimensions = await helper.getSelectionRegionDimensions();
        isfuzzy(
          dimensions.left,
          60,
          1,
          "Dragging the highlight moved the region"
        );
        isfuzzy(
          dimensions.top,
          60,
          1,
          "Dragging the highlight moved the region"
        );
        isfuzzy(dimensions.width, 490, 1, "Moving the region kept its width");

        await helper.touchDragOverlay(550, 550, 450, 450, "selected");
        dimensions = await helper.getSelectionRegionDimensions();
        isfuzzy(
          dimensions.width,
          390,
          1,
          "Dragging a corner resized the region"
        );
        isfuzzy(
          dimensions.height,
          390,
          1,
          "Dragging a corner resized the region"
        );

        await exitOverlay(browser, helper);
        await assertNoContentEvents(browser);
      }
    );
  });
}

add_task(async function test_touch_copy_button() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      await recordContentEvents(browser);
      let helper = await startOverlay(browser, SELECTION_MODES.SCREENSHOTS);
      let expected = Math.floor(
        490 * (await getContentDevicePixelRatio(browser))
      );

      await helper.touchDragOverlay(10, 10, 500, 500);

      let clipboardChanged = helper.waitForRawClipboardChange(
        expected,
        expected
      );
      await helper.tapOverlayButton("copy");
      let result = await clipboardChanged;
      is(result.width, expected, "Tapping copy copied the selected region");
      is(result.height, expected, "Tapping copy copied the selected region");

      await helper.waitForOverlayClosed();
      await assertNoContentEvents(browser);
    }
  );
});

add_task(async function test_touch_cancel_buttons() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      await recordContentEvents(browser);
      let helper = await startOverlay(browser, SELECTION_MODES.SCREENSHOTS);

      await helper.touchDragOverlay(10, 10, 500, 500);
      await helper.tapOverlayButton("cancel");
      await helper.assertStateChange("crosshairs");

      await helper.tapOverlayButton("screenshots-cancel-button");
      await helper.waitForOverlayClosed();

      await assertNoContentEvents(browser);
    }
  );
});

add_task(async function test_touch_mini_window_buttons() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      await recordContentEvents(browser);
      let helper = await startOverlay(browser, SELECTION_MODES.MINI_WINDOW);

      await helper.touchDragOverlay(10, 10, 500, 500);
      await helper.tapOverlayButton("reselect-button");
      await helper.assertStateChange("crosshairs");

      await helper.touchDragOverlay(10, 10, 500, 500);
      await helper.tapOverlayButton("mini-window-cancel-button");
      await TestUtils.waitForCondition(
        async () => !(await helper.isOverlayInitialized()),
        "Waiting for the dismiss button to tear the overlay down"
      );

      await assertNoContentEvents(browser);
    }
  );
});

add_task(async function test_touch_pop_button() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  let helper = await startOverlay(
    tab.linkedBrowser,
    SELECTION_MODES.MINI_WINDOW
  );

  await helper.touchDragOverlay(10, 10, 500, 500);

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  await helper.tapOverlayButton("pop");
  let miniWin = await opened;
  ok(miniWin, "Tapping pop opened a mini window");

  await TestUtils.waitForCondition(
    () => [...MiniWindowManager._miniwindows][0]?.state === "active",
    "Waiting for the mini window to become active"
  );
  let mini = [...MiniWindowManager._miniwindows][0];
  mini.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  is(MiniWindowManager._miniwindows.size, 0, "Mini window unregistered");
});

add_task(async function test_touch_tap_is_not_handled_twice() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: TEST_PAGE },
    async browser => {
      let helper = await startOverlay(browser, SELECTION_MODES.SCREENSHOTS);
      await helper.touchDragOverlay(10, 10, 500, 500);

      await SpecialPowers.spawn(browser, [], () => {
        let { overlay } = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        overlay.testCopyCount = 0;
        overlay.copySelectedRegion = () => overlay.testCopyCount++;
      });

      await helper.tapOverlayButton("copy");

      let copyCount = await SpecialPowers.spawn(browser, [], async () => {
        let { overlay } = content.windowGlobalChild.getActor(
          "ScreenshotsComponent"
        );
        await ContentTaskUtils.waitForCondition(
          () => overlay.testCopyCount > 0,
          "Waiting for the tap to activate the copy button"
        );

        // On a real touchscreen the tap is followed by a click with a
        // pointerType of "touch", which synthesized touch events don't
        // produce, so pass the overlay one directly.
        overlay.handleEvent({
          type: "click",
          pointerType: "touch",
          button: 0,
          buttons: 0,
          originalTarget: overlay.copyButton,
        });

        delete overlay.copySelectedRegion;
        return overlay.testCopyCount;
      });
      is(copyCount, 1, "A touch tap on copy is only handled once");

      await exitOverlay(browser, helper);
    }
  );
});
