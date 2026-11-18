/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Aliased, as ESLint knows of the EyeDropper Web API as a browser global.
const { EyeDropper: EyeDropperOverlay, getActiveEyeDropperForTests } =
  ChromeUtils.importESModule("moz-src:///toolkit/modules/EyeDropper.sys.mjs");

const RED = { r: 255, g: 0, b: 0 };
const GREEN = { r: 0, g: 200, b: 0 };
const BLUE = { r: 0, g: 128, b: 255 };
const YELLOW = { r: 255, g: 200, b: 0 };

// The page is split in four at 200px from the left and from the top: red at the
// top left, green at the top right, blue at the bottom left and yellow at the
// bottom right.
const PAGE_URL =
  "data:text/html," +
  encodeURIComponent(`<!doctype html>
  <body style="margin:0; display:grid; grid-template: 200px 1fr / 200px 1fr;
               height:100vh">
    <div style="background:rgb(255, 0, 0)"></div>
    <div style="background:rgb(0, 200, 0)"></div>
    <div style="background:rgb(0, 128, 255)"></div>
    <div style="background:rgb(255, 200, 0)"></div>
  </body>`);

async function waitForEyeDropperOpen(aDoc) {
  let eyeDropperHost = null;
  await TestUtils.waitForCondition(() => {
    eyeDropperHost = aDoc.querySelector(".eyedropper-host");
    return eyeDropperHost?.matches(":popover-open");
  }, "Wait for EyeDropper open");
  return eyeDropperHost?.openOrClosedShadowRoot?.querySelector(
    ".eye-dropper-root"
  );
}

/**
 * Waits for the magnifier to be shown and returns where its center is, in CSS
 * pixels relative to the browser window.
 *
 * @param {Element} aEyeDropper The root element of the eyedropper.
 * @returns {Promise<{x: number, y: number}>}
 */
async function getMagnifierCenter(aEyeDropper) {
  await TestUtils.waitForCondition(
    () => !aEyeDropper.hidden,
    "Wait for the magnifier to be shown"
  );
  const rect = aEyeDropper
    .querySelector(".eye-dropper-canvas")
    .getBoundingClientRect();
  return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
}

/**
 * Opens the eyedropper over the page shown in the browser and waits for its
 * element to be inserted in the browser window.
 *
 * @param {XULBrowserElement} aBrowser
 * @param {object} [aOptions] Passed to EyeDropper.open().
 * @returns {Promise<{eyeDropper: Element, pickedResult: Promise}>}
 *          The root element of the eyedropper, and the promise returned by
 *          open(), wrapped so that it is not awaited along.
 */
async function openEyeDropper(aBrowser, aOptions) {
  const pickedResult = new EyeDropperOverlay(aBrowser).open(aOptions);
  const eyeDropper = await waitForEyeDropperOpen(aBrowser.ownerDocument);
  return {
    eyeDropper,
    pickedResult,
  };
}

/**
 * Records the input events the page receives.
 *
 * @param {XULBrowserElement} aBrowser
 */
function watchPageInput(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    content.__seen = [];
    for (const type of [
      "mousemove",
      "mousedown",
      "mouseup",
      "click",
      "keydown",
      "keyup",
    ]) {
      content.addEventListener(type, () => content.__seen.push(type), true);
    }
  });
}

/**
 * @param {XULBrowserElement} aBrowser
 * @returns {Promise<string[]>} The input events seen since watchPageInput().
 */
function readPageInput(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => content.__seen.slice());
}
