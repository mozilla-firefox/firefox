/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals browser, exportFunction */

var UAHelpers = {
  _deviceAppropriateChromeUAs: {},
  changeBrowserVersion(ua, config = {}) {
    const which = config.browser;
    let new_ver = config.version;
    if (
      !which ||
      !ua?.includes(`${which}/`) ||
      new_ver == undefined ||
      new_ver == null
    ) {
      return ua;
    }
    const cur_ver = ua.match(`${which}/(\\d.*?)( |$)`)?.[1];
    if (!cur_ver) {
      return ua;
    }
    if (typeof new_ver != "string") {
      new_ver = String(new_ver);
    }
    if (new_ver.startsWith("+") || new_ver.startsWith("-")) {
      const factor = new_ver.startsWith("-") ? -1 : 1;
      const ver_segs = cur_ver.split(".").map(seg => parseInt(seg));
      const new_ver_segs = new_ver
        .substr(1)
        .split(".")
        .map(seg => parseInt(seg));
      for (const [index, seg] of ver_segs.entries()) {
        const new_val = seg + factor * (new_ver_segs[index] || 0);
        ver_segs[index] = new_val < 0 ? 0 : new_val;
      }
      new_ver = ver_segs.join(".");
    }
    return ua.replaceAll(cur_ver, new_ver);
  },
  getRunningFirefoxVersion() {
    return (navigator.userAgent.match(/Firefox\/([0-9.]+)/) || ["", "58.0"])[1];
  },
  getFxQuantumSegment() {
    return `FxQuantum/${UAHelpers.getRunningFirefoxVersion()} `;
  },
  _defaultChromeVersion: "157.0.0.0",
  getDeviceAppropriateChromeUA(config = {}) {
    // see https://www.chromium.org/updates/ua-reduction/#reduced-user-agent-string-reference
    let {
      androidVersion = "10",
      version = UAHelpers._defaultChromeVersion,
      phone,
      tablet,
      OS,
    } = config;
    const key = JSON.stringify(config);
    if (config.noCache || !UAHelpers._deviceAppropriateChromeUAs[key]) {
      const userAgent =
        config.ua ||
        (typeof navigator !== "undefined" ? navigator.userAgent : "");
      const fxQuantum = config.noFxQuantum
        ? ""
        : UAHelpers.getFxQuantumSegment();
      const noOSGiven = !OS || OS === "nonLinux";
      if (OS === "android" || (noOSGiven && userAgent.includes("Android"))) {
        let device = "K";
        if (typeof tablet === "string") {
          device = tablet;
        }
        if (typeof phone === "string") {
          device = phone;
        }
        const deviceCompat =
          phone || (!tablet && userAgent.includes("Mobile")) ? "Mobile " : "";
        UAHelpers._deviceAppropriateChromeUAs[key] =
          `Mozilla/5.0 (Linux; Android ${androidVersion}; ${device}) ${fxQuantum}AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version} ${deviceCompat}Safari/537.36`;
      } else {
        const WIN_SEGMENT = "Windows NT 10.0; Win64; x64";
        let unifiedPlatform;
        if (OS === "macOS" || (noOSGiven && userAgent.includes("Macintosh"))) {
          unifiedPlatform = "Macintosh; Intel Mac OS X 10_15_7";
        } else if (
          OS === "linux" ||
          (noOSGiven && userAgent.includes("Linux"))
        ) {
          if (OS !== "nonLinux") {
            unifiedPlatform = "X11; Linux x86_64";
          } else {
            unifiedPlatform = WIN_SEGMENT;
          }
        } else {
          unifiedPlatform = WIN_SEGMENT;
        }
        UAHelpers._deviceAppropriateChromeUAs[key] =
          `Mozilla/5.0 (${unifiedPlatform}) ${fxQuantum}AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version} Safari/537.36`;
      }
    }
    return UAHelpers._deviceAppropriateChromeUAs[key];
  },
  addGecko(
    ua = navigator.userAgent,
    version = UAHelpers.getRunningFirefoxVersion()
  ) {
    return `${ua} Gecko/${version}`;
  },
  addChrome(
    ua = navigator.userAgent,
    version = UAHelpers._defaultChromeVersion
  ) {
    const isMobile =
      navigator.userAgent.includes("Mobile") ||
      navigator.userAgent.includes("Tablet");
    return `${ua} AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version} ${isMobile ? "Mobile " : ""}Safari/537.36`;
  },
  safari(config = {}) {
    const version = config.version || "26.1";
    const webkitVersion = config.webkitVersion || "605.1.15";
    const osVersion = config.osVersion?.replace(".", "_") || "10_15_7";
    const arch = config.arch || "Intel";
    let firefox = "";
    if (config.withFirefox) {
      firefox = `Firefox/${UAHelpers.getRunningFirefoxVersion()} `;
    } else if (config.withFxQuantum) {
      firefox = UAHelpers.getFxQuantumSegment();
    }
    return `Mozilla/5.0 (Macintosh; ${arch} Mac OS X ${osVersion}) ${firefox}AppleWebKit/${webkitVersion} (KHTML, like Gecko) Version/${version} Safari/${webkitVersion}`;
  },
  androidHotspot2Device(originalUA) {
    return originalUA.replace(/\(.+?\)/, "(Linux; Android 10; K)");
  },
  changeFirefoxToFireFox(ua = navigator.userAgent) {
    return ua.replace("Firefox", "FireFox");
  },
  windows(ua = navigator.userAgent) {
    const rv = navigator.userAgent.match("rv:[0-9]+.[0-9]+")[0];
    return ua.replace(/\(.+?\)/, `(Windows NT 10.0; Win64; x64; ${rv})`);
  },
  desktopUA(ua = navigator.userAgent) {
    return ua.replace(/ (Mobile|Tablet);/, "");
  },
  addSamsungForSamsungDevices(ua = navigator.userAgent) {
    if (!browser.systemManufacturer) {
      return ua;
    }

    const manufacturer = browser.systemManufacturer.getManufacturer();
    if (manufacturer && manufacturer.toLowerCase() === "samsung") {
      return ua.replace("Mobile;", "Mobile; Samsung;");
    }

    return ua;
  },
  getPrefix(originalUA) {
    return originalUA.substr(0, originalUA.indexOf(")") + 1);
  },
  overrideWithDeviceAppropriateChromeUA(config) {
    const chromeUA = UAHelpers.getDeviceAppropriateChromeUA(config);
    const nav = Object.getPrototypeOf(navigator.wrappedJSObject);
    const ua = Object.getOwnPropertyDescriptor(nav, "userAgent");
    ua.get = exportFunction(() => chromeUA, window);
    Object.defineProperty(nav, "userAgent", ua);
  },
  capVersionTo99(originalUA) {
    const ver = originalUA.match(/Firefox\/(\d+\.\d+)/);
    if (!ver || parseFloat(ver[1]) < 100) {
      return originalUA;
    }
    return originalUA
      .replace(`Firefox/${ver[1]}`, "Firefox/99.0")
      .replace(`rv:${ver[1]}`, "rv:99.0");
  },
  capRvTo109(originalUA) {
    const ver = originalUA.match(/rv:(\d+\.\d+)/);
    if (!ver || parseFloat(ver[1]) <= 109) {
      return originalUA;
    }
    return originalUA.replace(`rv:${ver[1]}`, "rv:109.0");
  },
  capVersionToNumber(originalUA, cap = 120) {
    const ver = originalUA.match(/Firefox\/(\d+\.\d+)/);
    if (!ver || parseFloat(ver[1]) <= cap) {
      return originalUA;
    }
    const capped = `Firefox/${cap}.0`;
    return originalUA.replace(`Firefox/${ver[1]}`, capped);
  },
  getMacOSXUA(originalUA, arch = "Intel", version = "10.15") {
    return originalUA.replace(
      /\(.+?\)/,
      `(Macintosh; ${arch} Mac OS X ${version})`
    );
  },
  getWindowsUA(originalUA) {
    const rv = originalUA.match("rv:[0-9]+.[0-9]+")[0];
    const ver = originalUA.match("Firefox/[0-9]+.[0-9]+")[0];
    return `Mozilla/5.0 (Windows NT 10.0; Win64; x64; ${rv}) Gecko/20100101 ${ver}`;
  },
};
