/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

if (!navigator.userAgent.includes("Chrome/")) {
  const { appVersion, userAgent } = navigator;

  let actualPlatform = "linux";
  if (appVersion.includes("Android")) {
    actualPlatform = "android";
  } else if (appVersion.includes("Macintosh")) {
    actualPlatform = "mac";
  } else if (appVersion.includes("Windows")) {
    actualPlatform = "windows";
  }

  const wantedPlatform = window.__webcompat_spoof_platform ?? actualPlatform;

  if (wantedPlatform) {
    let mobile = "";
    let osSegment = `Windows NT 10; Win64; x64`;
    if (wantedPlatform == "android") {
      mobile = userAgent.includes("Mobile") ? "Mobile " : "";
      osSegment = `Linux; Android 10; K`;
    } else if (wantedPlatform == "mac") {
      osSegment = "Macintosh; Intel Mac OS X 10_15_7";
    } else if (wantedPlatform == "linux") {
      osSegment = "X11; Linux x86_64";
    }
    const final_ua = `Mozilla/5.0 (${osSegment}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 ${mobile}Safari/537.36`;

    const nav = Object.getPrototypeOf(navigator);

    const ua = Object.getOwnPropertyDescriptor(nav, "userAgent");
    ua.get = () => final_ua;
    Object.defineProperty(nav, "userAgent", ua);

    window.__webcompat = (window.__webcompat ?? new Set()).add(
      "navigator.userAgent"
    );
  }
}
