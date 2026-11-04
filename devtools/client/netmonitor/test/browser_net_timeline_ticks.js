/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if timeline correctly displays interval divisions.
 */

add_task(async function () {
  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { monitor } = await initNetMonitor(HTTPS_SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test... ");

  const { $, $all, NetMonitorView, NetMonitorController } = monitor.panelWin;
  const { RequestsMenu } = NetMonitorView;

  // Disable transferred size column support for this test.
  // Without this, the waterfall only has enough room for one division, which
  // would remove most of the value of this test.
  // $("#requests-list-transferred-header-box").hidden = true;
  // $("#requests-list-item-template .requests-list-transferred").hidden = true;

  RequestsMenu.lazyUpdate = false;

  ok(
    $("#requests-list-waterfall-label"),
    "An timeline label should be displayed when the frontend is opened."
  );
  ok(
    !$all(".requests-list-timings-division").length,
    "No tick labels should be displayed when the frontend is opened."
  );

  ok(
    !RequestsMenu._canvas,
    "No canvas should be created when the frontend is opened."
  );
  ok(
    !RequestsMenu._ctx,
    "No 2d context should be created when the frontend is opened."
  );

  const onNetworkEvents = waitForNetworkEvents(monitor, 1);
  await reloadSelectedTab();
  await onNetworkEvents;

  // Make sure the DOMContentLoaded and load markers don't interfere with
  // this test by removing them and redrawing the waterfall (bug 1224088).
  NetMonitorController.NetworkEventsHandler.clearMarkers();
  RequestsMenu._flushWaterfallViews(true);

  ok(
    !$("#requests-list-waterfall-label"),
    "The timeline label should be hidden after the first request."
  );
  Assert.greaterOrEqual(
    $all(".requests-list-timings-division").length,
    3,
    "There should be at least 3 tick labels in the network requests header."
  );

  const timingDivisionEls = $all(".requests-list-timings-division");
  is(
    timingDivisionEls[0].textContent,
    L10N.getFormatStr("networkMenu.millisecond", 0),
    "The first tick label has correct value"
  );
  is(
    timingDivisionEls[1].textContent,
    L10N.getFormatStr("networkMenu.millisecond", 80),
    "The second tick label has correct value"
  );
  is(
    timingDivisionEls[2].textContent,
    L10N.getFormatStr("networkMenu.millisecond", 160),
    "The third tick label has correct value"
  );

  is(
    timingDivisionEls[0].style.width,
    "78px",
    "The first tick label has correct width"
  );
  is(
    timingDivisionEls[1].style.width,
    "80px",
    "The second tick label has correct width"
  );
  is(
    timingDivisionEls[2].style.width,
    "80px",
    "The third tick label has correct width"
  );

  ok(
    RequestsMenu._canvas,
    "A canvas should be created after the first request."
  );
  ok(
    RequestsMenu._ctx,
    "A 2d context should be created after the first request."
  );

  const imageData = RequestsMenu._ctx.getImageData(0, 0, 161, 1);
  ok(imageData, "The image data should have been created.");

  const { data } = imageData;
  ok(data, "The image data should contain a pixel array.");

  ok(hasPixelAt(0), "The tick at 0 is should not be empty.");
  ok(!hasPixelAt(1), "The tick at 1 is should be empty.");
  ok(!hasPixelAt(19), "The tick at 19 is should be empty.");
  ok(hasPixelAt(20), "The tick at 20 is should not be empty.");
  ok(!hasPixelAt(21), "The tick at 21 is should be empty.");
  ok(!hasPixelAt(39), "The tick at 39 is should be empty.");
  ok(hasPixelAt(40), "The tick at 40 is should not be empty.");
  ok(!hasPixelAt(41), "The tick at 41 is should be empty.");
  ok(!hasPixelAt(59), "The tick at 59 is should be empty.");
  ok(hasPixelAt(60), "The tick at 60 is should not be empty.");
  ok(!hasPixelAt(61), "The tick at 61 is should be empty.");
  ok(!hasPixelAt(79), "The tick at 79 is should be empty.");
  ok(hasPixelAt(80), "The tick at 80 is should not be empty.");
  ok(!hasPixelAt(81), "The tick at 81 is should be empty.");
  ok(!hasPixelAt(159), "The tick at 159 is should be empty.");
  ok(hasPixelAt(160), "The tick at 160 is should not be empty.");
  ok(!hasPixelAt(161), "The tick at 161 is should be empty.");

  ok(
    isPixelBrighterAtThan(0, 20),
    "The tick at 0 should be brighter than the one at 20"
  );
  ok(
    isPixelBrighterAtThan(40, 20),
    "The tick at 40 should be brighter than the one at 20"
  );
  ok(
    isPixelBrighterAtThan(40, 60),
    "The tick at 40 should be brighter than the one at 60"
  );
  ok(
    isPixelBrighterAtThan(80, 60),
    "The tick at 80 should be brighter than the one at 60"
  );

  ok(
    isPixelBrighterAtThan(80, 100),
    "The tick at 80 should be brighter than the one at 100"
  );
  ok(
    isPixelBrighterAtThan(120, 100),
    "The tick at 120 should be brighter than the one at 100"
  );
  ok(
    isPixelBrighterAtThan(120, 140),
    "The tick at 120 should be brighter than the one at 140"
  );
  ok(
    isPixelBrighterAtThan(160, 140),
    "The tick at 160 should be brighter than the one at 140"
  );

  ok(
    isPixelEquallyBright(20, 60),
    "The tick at 20 should be equally bright to the one at 60"
  );
  ok(
    isPixelEquallyBright(100, 140),
    "The tick at 100 should be equally bright to the one at 140"
  );

  ok(
    isPixelEquallyBright(40, 120),
    "The tick at 40 should be equally bright to the one at 120"
  );

  ok(
    isPixelEquallyBright(0, 80),
    "The tick at 80 should be equally bright to the one at 160"
  );
  ok(
    isPixelEquallyBright(80, 160),
    "The tick at 80 should be equally bright to the one at 160"
  );

  function hasPixelAt(x) {
    const i = (x | 0) * 4;
    return data[i] && data[i + 1] && data[i + 2] && data[i + 3];
  }

  function isPixelBrighterAtThan(x1, x2) {
    const i = (x1 | 0) * 4;
    const j = (x2 | 0) * 4;
    return data[i + 3] > data[j + 3];
  }

  function isPixelEquallyBright(x1, x2) {
    const i = (x1 | 0) * 4;
    const j = (x2 | 0) * 4;
    return data[i + 3] == data[j + 3];
  }

  return teardown(monitor);
});
