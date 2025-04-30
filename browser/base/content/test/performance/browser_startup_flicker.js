/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * This test ensures that there is no unexpected flicker
 * on the first window opened during startup.
 */

add_task(async function () {
  let startupRecorder =
    Cc["@mozilla.org/test/startuprecorder;1"].getService().wrappedJSObject;
  await startupRecorder.done;

  // Ensure all the frame data is in the test compartment to avoid traversing
  // a cross compartment wrapper for each pixel.
  let frames = Cu.cloneInto(startupRecorder.data.frames, {});
  ok(!!frames.length, "Should have captured some frames.");

  let unexpectedRects = 0;
  let alreadyFocused = false;
  let inRange = (val, min, max) => min <= val && val <= max;
  let tabBoundingRect = undefined;
  for (let i = 1; i < frames.length; ++i) {
    let frame = frames[i],
      previousFrame = frames[i - 1];
    let rects = compareFrames(frame, previousFrame);
    if (!alreadyFocused && isLikelyFocusChange(rects, frame)) {
      todo(
        false,
        "bug 1445161 - the window should be focused at first paint, " +
          rects.toSource()
      );
      continue;
    }
    alreadyFocused = true;

    rects = rects.filter(rect => {
      let width = frame.width;

      let exceptions = [
        /**
         * Please don't add anything new unless justified!
         */
        {
          name: "Shadow around active tab should not flicker on macOS (bug 1960967)",
          condition(r) {
            const tabRect = tabBoundingRect
              ? tabBoundingRect
              : (tabBoundingRect = gBrowser.tabContainer
                  .querySelector("tab[selected=true] .tab-background")
                  .getBoundingClientRect());
            return (
              inRange(r.x1, tabRect.x - 2, tabRect.x + 2) &&
              inRange(r.y1, tabRect.y - 2, tabRect.y + 2) &&
              inRange(r.w, tabRect.width - 4, tabRect.width + 4) &&
              inRange(r.h, tabRect.height - 4, tabRect.height + 4)
            );
          },
        },
      ];

      let rectText = `${rect.toSource()}, window width: ${width}`;
      for (let e of exceptions) {
        if (e.condition(rect)) {
          todo(false, e.name + ", " + rectText);
          return false;
        }
      }

      ok(false, "unexpected changed rect: " + rectText);
      return true;
    });
    if (!rects.length) {
      info("ignoring identical frame");
      continue;
    }

    // Before dumping a frame with unexpected differences for the first time,
    // ensure at least one previous frame has been logged so that it's possible
    // to see the differences when examining the log.
    if (!unexpectedRects) {
      dumpFrame(previousFrame);
    }
    unexpectedRects += rects.length;
    dumpFrame(frame);
  }
  is(unexpectedRects, 0, "should have 0 unknown flickering areas");
});
