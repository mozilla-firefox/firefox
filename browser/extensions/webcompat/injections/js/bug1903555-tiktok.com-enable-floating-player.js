/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Bug 1903555 - Enable the "Floating Player" option on TikTok
 *
 * TikTok only offers its document Picture-in-Picture "Floating Player" if
 * the browser supports documentPictureInPicture AND its user agent string
 * looks like Chrome 126 or later. This makes navigator.userAgent look like
 * Chrome only while TikTok computes `isDocumentPipSupporting`, leaving the
 * user agent unchanged everywhere else. The check first sniffs the browser
 * name inside a function (whose inferred name shows up in the stack), then
 * immediately matches the Chrome version outside of it, so the next
 * synchronous read is also spoofed.
 */

if (
  "documentPictureInPicture" in window &&
  !(function isDocumentPipSupporting() {
    return (
      navigator.userAgent.includes("Chrome/") &&
      navigator.userAgent.includes("Chrome/")
    );
  })()
) {
  const CHROME_UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36";

  const nav = Object.getPrototypeOf(navigator);
  const descriptor = Object.getOwnPropertyDescriptor(nav, "userAgent");
  const { get } = descriptor;
  let logged = false;
  let spoofNextRead = false;

  descriptor.get = function () {
    if (new Error().stack?.includes("isDocumentPipSupporting")) {
      if (!logged) {
        logged = true;
        console.info(
          "navigator.userAgent has been altered for TikTok's Floating Player check for compatibility reasons. See https://bugzil.la/1903555 for details."
        );
      }
      if (!spoofNextRead) {
        spoofNextRead = true;
        queueMicrotask(() => {
          spoofNextRead = false;
        });
      }
      return CHROME_UA;
    }
    if (spoofNextRead) {
      spoofNextRead = false;
      return CHROME_UA;
    }
    return get.call(this);
  };

  Object.defineProperty(nav, "userAgent", descriptor);
}
