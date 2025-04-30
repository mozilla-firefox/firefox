/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1956165 - Fix picture-in-picture mode on Mobile YouTube
 *
 * YouTube does not play well with our picture in picture implementation, and
 * effectively cancels it. We can work around this conflict with this site patch.
 */

/* globals exportFunction */

console.info(
  "exitFullscreen and window.outerWidth|Height have been overridden for compatibility reasons. See https://bugzilla.mozilla.org/show_bug.cgi?id=1956165 for details."
);

const win = window.wrappedJSObject;
const outerWidthDesc = Object.getOwnPropertyDescriptor(win, "outerWidth");
const outerHeightDesc = Object.getOwnPropertyDescriptor(win, "outerHeight");
const originalOuterWidth = outerWidthDesc.get;
const originalOuterHeight = outerHeightDesc.get;
outerWidthDesc.get = exportFunction(() => {
  const actual = originalOuterWidth();
  if (actual < screen.width / 2) {
    return screen.width;
  }
  return actual;
}, window);
outerHeightDesc.get = exportFunction(() => {
  const actual = originalOuterHeight();
  if (actual < screen.height / 2) {
    return screen.height;
  }
  return actual;
}, window);
Object.defineProperty(win, "outerWidth", outerWidthDesc);
Object.defineProperty(win, "outerHeight", outerHeightDesc);

const originalExitFullscreen = win.Document.prototype.exitFullscreen;

const newExitFullscreen = exportFunction(function () {
  if (this.inAndroidPipMode) {
    return undefined;
  }
  return originalExitFullscreen.apply(this);
}, window);

Object.defineProperty(win.Document.prototype, "exitFullscreen", {
  value: newExitFullscreen,
  writable: true,
  configurable: true,
});
