/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ContentBlockingAllowList:
    "moz-src:///toolkit/components/antitracking/ContentBlockingAllowList.sys.mjs",
});

const TEST_PATH =
  "http://example.net/browser/browser/" +
  "components/resistfingerprinting/test/browser/";

const PERFORMANCE_TIMINGS = [
  "navigationStart",
  "unloadEventStart",
  "unloadEventEnd",
  "redirectStart",
  "redirectEnd",
  "fetchStart",
  "domainLookupStart",
  "domainLookupEnd",
  "connectStart",
  "connectEnd",
  "secureConnectionStart",
  "requestStart",
  "responseStart",
  "responseEnd",
  "domLoading",
  "domInteractive",
  "domContentLoadedEventStart",
  "domContentLoadedEventEnd",
  "domComplete",
  "loadEventStart",
  "loadEventEnd",
];

/**
 * Sets up tests for making sure that performance APIs have been correctly
 * spoofed or disabled.
 */
let setupPerformanceAPISpoofAndDisableTest = async function (
  resistFingerprinting,
  reduceTimerPrecision,
  crossOriginIsolated,
  expectedPrecision,
  runTests,
  workerCall
) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", resistFingerprinting],
      ["privacy.reduceTimerPrecision", reduceTimerPrecision],
      [
        "privacy.resistFingerprinting.reduceTimerPrecision.microseconds",
        expectedPrecision * 1000,
      ],
      ["browser.tabs.remote.useCrossOriginOpenerPolicy", crossOriginIsolated],
      ["browser.tabs.remote.useCrossOriginEmbedderPolicy", crossOriginIsolated],
      ["security.allow_eval_with_system_principal", true],
    ],
  });

  let url = crossOriginIsolated
    ? `https://example.com/browser/browser/components/resistfingerprinting` +
      `/test/browser/coop_header.sjs?crossOriginIsolated=${crossOriginIsolated}`
    : TEST_PATH + "file_dummy.html";

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let tab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);

  // No matter what we set the precision to, if we're in ResistFingerprinting
  // mode we use the larger of the precision pref and the RFP time-atom constant
  if (resistFingerprinting) {
    const RFP_TIME_ATOM_MS = 16.667;
    expectedPrecision = Math.max(RFP_TIME_ATOM_MS, expectedPrecision);
  }
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [
      {
        list: PERFORMANCE_TIMINGS,
        resistFingerprinting,
        precision: expectedPrecision,
        isRoundedFunc: isTimeValueRounded.toString(),
        workerCall,
      },
    ],
    runTests
  );

  if (crossOriginIsolated) {
    let remoteType = tab.linkedBrowser.remoteType;
    ok(
      remoteType.startsWith(E10SUtils.WEB_REMOTE_COOP_COEP_TYPE_PREFIX),
      `${remoteType} expected to be coop+coep`
    );
  }

  await BrowserTestUtils.closeWindow(win);
};

let isTimeValueRounded = (x, expectedPrecision, console) => {
  const nearestExpected = Math.round(x / expectedPrecision) * expectedPrecision;
  // First we do the perfectly normal check that should work just fine
  if (x === nearestExpected) {
    return true;
  }

  // When we're dividing by non-whole numbers, we may not get perfect
  // multiplication/division because of floating points.
  // When dealing with ms since epoch, a double's precision is on the order
  // of 1/5 of a microsecond, so we use a value a little higher than that as
  // our epsilon.
  // To be clear, this error is introduced in our re-calculation of 'rounded'
  // above in JavaScript.
  const error = Math.abs(x - nearestExpected);

  if (console) {
    console.log(
      "Additional Debugging Info: Expected Precision: " +
        expectedPrecision +
        " Measured Value: " +
        x +
        " Nearest Expected Vaue: " +
        nearestExpected +
        " Error: " +
        error
    );
  }

  if (Math.abs(error) < 0.0005) {
    return true;
  }

  // Then we handle the case where you're sub-millisecond and the timer is not
  // We check that the timer is not sub-millisecond by assuming it is not if it
  // returns an even number of milliseconds
  if (
    Math.round(expectedPrecision) != expectedPrecision &&
    Math.round(x) == x
  ) {
    let acceptableIntRounding = false;
    acceptableIntRounding |= Math.floor(nearestExpected) == x;
    acceptableIntRounding |= Math.ceil(nearestExpected) == x;
    if (acceptableIntRounding) {
      return true;
    }
  }

  return false;
};

let setupAndRunCrossOriginIsolatedTest = async function (
  options,
  expectedPrecision,
  runTests,
  workerCall
) {
  let prefsToSet = [
    [
      "privacy.resistFingerprinting.reduceTimerPrecision.microseconds",
      expectedPrecision * 1000,
    ],
  ];

  if (options.resistFingerprinting !== undefined) {
    prefsToSet = prefsToSet.concat([
      ["privacy.resistFingerprinting", options.resistFingerprinting],
    ]);
  } else {
    options.resistFingerprinting = false;
  }

  if (options.resistFingerprintingPBMOnly !== undefined) {
    prefsToSet = prefsToSet.concat([
      [
        "privacy.resistFingerprinting.pbmode",
        options.resistFingerprintingPBMOnly,
      ],
    ]);
  } else {
    options.resistFingerprintingPBMOnly = false;
  }

  if (options.reduceTimerPrecision !== undefined) {
    prefsToSet = prefsToSet.concat([
      ["privacy.reduceTimerPrecision", options.reduceTimerPrecision],
    ]);
  } else {
    options.reduceTimerPrecision = false;
  }

  if (options.crossOriginIsolated !== undefined) {
    prefsToSet = prefsToSet.concat([
      [
        "browser.tabs.remote.useCrossOriginOpenerPolicy",
        options.crossOriginIsolated,
      ],
      [
        "browser.tabs.remote.useCrossOriginEmbedderPolicy",
        options.crossOriginIsolated,
      ],
    ]);
  } else {
    options.crossOriginIsolated = false;
  }

  if (options.openPrivateWindow === undefined) {
    options.openPrivateWindow = false;
  }
  if (options.shouldBeRounded === undefined) {
    options.shouldBeRounded = true;
  }

  console.log(prefsToSet);
  await SpecialPowers.pushPrefEnv({ set: prefsToSet });

  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: options.openPrivateWindow,
  });
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    `https://example.com/browser/browser/components/resistfingerprinting` +
      `/test/browser/coop_header.sjs?crossOriginIsolated=${options.crossOriginIsolated}`
  );

  // No matter what we set the precision to, if we're in ResistFingerprinting
  // mode we use the larger of the precision pref and the RFP time-atom constant
  if (
    options.resistFingerprinting ||
    (options.resistFingerprintingPBMOnly && options.openPrivateWindow)
  ) {
    const RFP_TIME_ATOM_MS = 16.667;
    expectedPrecision = Math.max(RFP_TIME_ATOM_MS, expectedPrecision);
  }
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [
      {
        precision: expectedPrecision,
        isRoundedFunc: isTimeValueRounded.toString(),
        workerCall,
        options,
      },
    ],
    runTests
  );

  if (options.crossOriginIsolated) {
    let remoteType = tab.linkedBrowser.remoteType;
    ok(
      remoteType.startsWith(E10SUtils.WEB_REMOTE_COOP_COEP_TYPE_PREFIX),
      `${remoteType} expected to be coop+coep`
    );
  }

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
};

// This function calculates the maximum available window dimensions and returns
// them as an object.
async function calcMaximumAvailSize(aChromeWidth, aChromeHeight) {
  let chromeUIWidth;
  let chromeUIHeight;
  let testPath =
    "http://example.net/browser/browser/" +
    "components/resistfingerprinting/test/browser/";

  // If the chrome UI dimensions is not given, we will calculate it.
  if (!aChromeWidth || !aChromeHeight) {
    let win = await BrowserTestUtils.openNewBrowserWindow();

    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      testPath + "file_dummy.html"
    );

    let contentSize = await SpecialPowers.spawn(
      tab.linkedBrowser,
      [],
      async function () {
        let result = {
          width: content.innerWidth,
          height: content.innerHeight,
        };

        return result;
      }
    );

    // Calculate the maximum available window size which is depending on the
    // available screen space.
    chromeUIWidth = win.outerWidth - contentSize.width;
    chromeUIHeight = win.outerHeight - contentSize.height;

    BrowserTestUtils.removeTab(tab);
    await BrowserTestUtils.closeWindow(win);
  } else {
    chromeUIWidth = aChromeWidth;
    chromeUIHeight = aChromeHeight;
  }

  let availWidth = window.screen.availWidth;
  let availHeight = window.screen.availHeight;

  // Ideally, we would round the window size as
  // privacy.window.maxInnerWidth x privacy.window.maxInnerHeight. But the
  // available screen space might not suffice. So, we decide the size according
  // to the available screen size.
  let maxInnerWidth = Services.prefs.getIntPref("privacy.window.maxInnerWidth");
  let maxInnerHeight = Services.prefs.getIntPref(
    "privacy.window.maxInnerHeight"
  );

  let availContentWidth = Math.min(maxInnerWidth, availWidth - chromeUIWidth);
  let availContentHeight;

  // If it is GTK window, we would consider the system decorations when we
  // calculating avail content height since the system decorations won't be
  // reported when we get available screen dimensions.
  if (AppConstants.MOZ_WIDGET_GTK) {
    availContentHeight = Math.min(
      maxInnerHeight,
      -40 + availHeight - chromeUIHeight
    );
  } else {
    availContentHeight = Math.min(maxInnerHeight, availHeight - chromeUIHeight);
  }

  // Rounded the desire size to the nearest 200x100.
  let maxAvailWidth = availContentWidth - (availContentWidth % 200);
  let maxAvailHeight = availContentHeight - (availContentHeight % 100);

  return { maxAvailWidth, maxAvailHeight };
}

async function calcPopUpWindowChromeUISize() {
  let testPath =
    "http://example.net/browser/browser/" +
    "components/resistFingerprinting/test/browser/";
  // open a popup window to acquire the chrome UI size of it.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    testPath + "file_dummy.html"
  );

  let result = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    async function () {
      let win = content.open("about:blank", "", "width=1000,height=1000");

      let res = {
        chromeWidth: win.outerWidth - win.innerWidth,
        chromeHeight: win.outerHeight - win.innerHeight,
      };

      win.close();

      return res;
    }
  );

  BrowserTestUtils.removeTab(tab);

  return result;
}

async function testWindowOpen(
  aBrowser,
  aSettingWidth,
  aSettingHeight,
  aTargetWidth,
  aTargetHeight,
  aMaxAvailWidth,
  aMaxAvailHeight
) {
  // If the target size is greater than the maximum available content size,
  // we set the target size to it.
  if (aTargetWidth > aMaxAvailWidth) {
    aTargetWidth = aMaxAvailWidth;
  }

  if (aTargetHeight > aMaxAvailHeight) {
    aTargetHeight = aMaxAvailHeight;
  }

  // Create the testing window features.
  let winFeatures = "width=" + aSettingWidth + ",height=" + aSettingHeight;

  let testParams = {
    winFeatures,
    targetWidth: aTargetWidth,
    targetHeight: aTargetHeight,
  };

  await SpecialPowers.spawn(aBrowser, [testParams], async function (input) {
    // Call window.open() with window features.
    await new Promise(resolve => {
      let win = content.open("http://example.net/", "", input.winFeatures);

      win.onload = () => {
        is(
          win.screen.width,
          input.targetWidth,
          "The screen.width has a correct rounded value"
        );
        is(
          win.screen.height,
          input.targetHeight,
          "The screen.height has a correct rounded value"
        );
        is(
          win.innerWidth,
          input.targetWidth,
          "The window.innerWidth has a correct rounded value"
        );
        is(
          win.innerHeight,
          input.targetHeight,
          "The window.innerHeight has a correct rounded value"
        );

        win.close();
        resolve();
      };
    });
  });
}

async function testWindowSizeSetting(
  aBrowser,
  aSettingWidth,
  aSettingHeight,
  aTargetWidth,
  aTargetHeight,
  aInitWidth,
  aInitHeight,
  aTestOuter,
  aMaxAvailWidth,
  aMaxAvailHeight,
  aPopupChromeUIWidth,
  aPopupChromeUIHeight
) {
  // If the target size is greater than the maximum available content size,
  // we set the target size to it.
  if (aTargetWidth > aMaxAvailWidth) {
    aTargetWidth = aMaxAvailWidth;
  }

  if (aTargetHeight > aMaxAvailHeight) {
    aTargetHeight = aMaxAvailHeight;
  }

  let testParams = {
    initWidth: aInitWidth,
    initHeight: aInitHeight,
    settingWidth: aSettingWidth + (aTestOuter ? aPopupChromeUIWidth : 0),
    settingHeight: aSettingHeight + (aTestOuter ? aPopupChromeUIHeight : 0),
    targetWidth: aTargetWidth,
    targetHeight: aTargetHeight,
    testOuter: aTestOuter,
  };

  await SpecialPowers.spawn(aBrowser, [testParams], async function (input) {
    let win;
    // Open a new window and wait until it loads.
    await new Promise(resolve => {
      // Given a initial window size which should be different from target
      // size. We need this to trigger 'onresize' event.
      let initWinFeatures =
        "width=" + input.initWidth + ",height=" + input.initHeight;
      win = content.open("http://example.net/", "", initWinFeatures);
      win.onload = () => resolve();
    });

    // Test inner/outerWidth.
    await new Promise(resolve => {
      win.addEventListener(
        "resize",
        () => {
          is(
            win.screen.width,
            input.targetWidth,
            "The screen.width has a correct rounded value"
          );
          is(
            win.innerWidth,
            input.targetWidth,
            "The window.innerWidth has a correct rounded value"
          );

          resolve();
        },
        { once: true }
      );

      if (input.testOuter) {
        win.outerWidth = input.settingWidth;
      } else {
        win.innerWidth = input.settingWidth;
      }
    });

    win.close();
    // Open a new window and wait until it loads.
    await new Promise(resolve => {
      // Given a initial window size which should be different from target
      // size. We need this to trigger 'onresize' event.
      let initWinFeatures =
        "width=" + input.initWidth + ",height=" + input.initHeight;
      win = content.open("http://example.net/", "", initWinFeatures);
      win.onload = () => resolve();
    });

    // Test inner/outerHeight.
    await new Promise(resolve => {
      win.addEventListener(
        "resize",
        () => {
          is(
            win.screen.height,
            input.targetHeight,
            "The screen.height has a correct rounded value"
          );
          is(
            win.innerHeight,
            input.targetHeight,
            "The window.innerHeight has a correct rounded value"
          );

          resolve();
        },
        { once: true }
      );

      if (input.testOuter) {
        win.outerHeight = input.settingHeight;
      } else {
        win.innerHeight = input.settingHeight;
      }
    });

    win.close();
  });
}

class RoundedWindowTest {
  // testOuter is optional.  run() can be invoked with only 1 parameter.
  static run(testCases, testOuter) {
    // "this" is the calling class itself.
    // e.g. when invoked by RoundedWindowTest.run(), "this" is "class RoundedWindowTest".
    let test = new this(testCases);
    add_task(async () => test.setup());
    add_task(async () => {
      if (testOuter == undefined) {
        // If testOuter is not given, do tests for both inner and outer.
        await test.doTests(false);
        await test.doTests(true);
      } else {
        await test.doTests(testOuter);
      }
    });
  }

  constructor(testCases) {
    this.testCases = testCases;
  }

  async setup() {
    await SpecialPowers.pushPrefEnv({
      set: [["privacy.resistFingerprinting", true]],
    });

    // Calculate the popup window's chrome UI size for tests of outerWidth/Height.
    let popUpChromeUISize = await calcPopUpWindowChromeUISize();

    this.popupChromeUIWidth = popUpChromeUISize.chromeWidth;
    this.popupChromeUIHeight = popUpChromeUISize.chromeHeight;

    // Calculate the maximum available size.
    let maxAvailSize = await calcMaximumAvailSize(
      this.popupChromeUIWidth,
      this.popupChromeUIHeight
    );

    this.maxAvailWidth = maxAvailSize.maxAvailWidth;
    this.maxAvailHeight = maxAvailSize.maxAvailHeight;
  }

  async doTests(testOuter) {
    // Open a tab to test.
    this.tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      TEST_PATH + "file_dummy.html"
    );

    for (let test of this.testCases) {
      await this.doTest(test, testOuter);
    }

    BrowserTestUtils.removeTab(this.tab);
  }

  async doTest() {
    throw new Error("RoundedWindowTest.doTest must be overridden.");
  }
}

class WindowSettingTest extends RoundedWindowTest {
  async doTest(test, testOuter) {
    await testWindowSizeSetting(
      this.tab.linkedBrowser,
      test.settingWidth,
      test.settingHeight,
      test.targetWidth,
      test.targetHeight,
      test.initWidth,
      test.initHeight,
      testOuter,
      this.maxAvailWidth,
      this.maxAvailHeight,
      this.popupChromeUIWidth,
      this.popupChromeUIHeight
    );
  }
}

class OpenTest extends RoundedWindowTest {
  async doTest(test) {
    await testWindowOpen(
      this.tab.linkedBrowser,
      test.settingWidth,
      test.settingHeight,
      test.targetWidth,
      test.targetHeight,
      this.maxAvailWidth,
      this.maxAvailHeight,
      this.popupChromeUIWidth,
      this.popupChromeUIHeight
    );
  }
}

// ============================================================
const FRAMER_DOMAIN = "example.com";
const IFRAME_DOMAIN = "example.org";
const CROSS_ORIGIN_DOMAIN = "example.net";

async function runActualTest(uri, testFunction, expectedResults, extraData) {
  let browserWin = gBrowser;
  let openedWin = null;
  let pbmWindow =
    "private_window" in extraData && extraData.private_window === true;

  if (pbmWindow) {
    openedWin = await BrowserTestUtils.openNewBrowserWindow({
      private: true,
    });
    browserWin = openedWin.gBrowser;
  }

  let tab = await BrowserTestUtils.openNewForegroundTab(browserWin, uri);

  if ("etp_reload" in extraData) {
    ContentBlockingAllowList.add(tab.linkedBrowser);
    await BrowserTestUtils.reloadTab(tab);
  }

  /*
   * We expect that `runTheTest` is going to be able to communicate with the iframe
   * or tab that it opens, but if it cannot (because we are using noopener), we kind
   * of hack around and get the data directly.
   */
  if ("noopener" in extraData) {
    var popupTabPromise = BrowserTestUtils.waitForNewTab(
      browserWin,
      extraData.await_uri
    );
  }

  // In SpecialPowers.spawn, extraData goes through a structuredClone, which cannot clone
  // functions. await_uri is sometimes a function.  This filters out keys that are used by
  // this function (runActualTest) and not by runTheTest or testFunction. It avoids the
  // cloning issue, and avoids polluting the object in those called functions.
  let filterExtraData = function (x) {
    let banned_keys = ["private_window", "etp_reload", "noopener", "await_uri"];
    return Object.fromEntries(
      Object.entries(x).filter(([k]) => !banned_keys.includes(k))
    );
  };

  let result = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [IFRAME_DOMAIN, CROSS_ORIGIN_DOMAIN, filterExtraData(extraData)],
    async function (iframe_domain_, cross_origin_domain_, extraData_) {
      return content.wrappedJSObject.runTheTest(
        iframe_domain_,
        cross_origin_domain_,
        extraData_
      );
    }
  );

  if ("noopener" in extraData) {
    await popupTabPromise;
    if (Services.appinfo.OS === "WINNT") {
      await new Promise(r => setTimeout(r, 1000));
    }

    let popup_tab = browserWin.tabs[browserWin.tabs.length - 1];
    result = await SpecialPowers.spawn(
      popup_tab.linkedBrowser,
      [],
      async function () {
        let r = content.wrappedJSObject.give_result();
        return r;
      }
    );
    BrowserTestUtils.removeTab(popup_tab);
  }

  testFunction(result, expectedResults, extraData);

  if ("etp_reload" in extraData) {
    ContentBlockingAllowList.remove(tab.linkedBrowser);
  }
  BrowserTestUtils.removeTab(tab);
  if (pbmWindow) {
    await BrowserTestUtils.closeWindow(openedWin);
  }
}

async function defaultsTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "default";
  expectedResults.shouldRFPApply = false;
  if (extraPrefs != undefined) {
    await SpecialPowers.pushPrefEnv({
      set: extraPrefs,
    });
  }
  await runActualTest(uri, testFunction, expectedResults, extraData);
  if (extraPrefs != undefined) {
    await SpecialPowers.popPrefEnv();
  }
}

async function defaultsPBMTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.private_window = true;
  extraData.testDesc = extraData.testDesc || "default PBM window";
  expectedResults.shouldRFPApply = false;
  if (extraPrefs != undefined) {
    await SpecialPowers.pushPrefEnv({
      set: extraPrefs,
    });
  }
  await runActualTest(uri, testFunction, expectedResults, extraData);
  if (extraPrefs != undefined) {
    await SpecialPowers.popPrefEnv();
  }
}

async function simpleRFPTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "simple RFP enabled";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.resistFingerprinting", true]].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

async function simplePBMRFPTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.private_window = true;
  extraData.testDesc = extraData.testDesc || "simple RFP in PBM enabled";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.resistFingerprinting.pbmode", true]].concat(
      extraPrefs || []
    ),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

async function simpleFPPTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "simple FPP enabled";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.fingerprintingProtection", true]].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

async function simplePBMFPPTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.private_window = true;
  extraData.testDesc = extraData.testDesc || "simple FPP in PBM enabled";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.fingerprintingProtection.pbmode", true]].concat(
      extraPrefs || []
    ),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

async function RFPPBMFPP_NormalMode_NoProtectionsTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.private_window = false;
  extraData.testDesc =
    extraData.testDesc ||
    "RFP Enabled in PBM and FPP enabled in Normal Browsing Mode, Protections Disabled";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", false],
      ["privacy.resistFingerprinting.pbmode", true],
      ["privacy.fingerprintingProtection", true],
      ["privacy.fingerprintingProtection.overrides", "-AllTargets"],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

async function RFPPBMFPP_NormalMode_ProtectionsTest(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.private_window = false;
  extraData.testDesc =
    extraData.testDesc ||
    "RFP Enabled in PBM and FPP enabled in Normal Browsing Mode, Protections Enabled";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", false],
      ["privacy.resistFingerprinting.pbmode", true],
      ["privacy.fingerprintingProtection", true],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (A) RFP is exempted on the framer and framee and (if needed) on another cross-origin domain
async function testA(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (A)";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      [
        "privacy.resistFingerprinting.exemptedDomains",
        `${FRAMER_DOMAIN}, ${IFRAME_DOMAIN}, ${CROSS_ORIGIN_DOMAIN}`,
      ],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (B) RFP is exempted on the framer and framee but is not on another (if needed) cross-origin domain
async function testB(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (B)";
  expectedResults.shouldRFPApply = false;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      [
        "privacy.resistFingerprinting.exemptedDomains",
        `${FRAMER_DOMAIN}, ${IFRAME_DOMAIN}`,
      ],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (C) RFP is exempted on the framer and (if needed) on another cross-origin domain, but not the framee
async function testC(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (C)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      [
        "privacy.resistFingerprinting.exemptedDomains",
        `${FRAMER_DOMAIN}, ${CROSS_ORIGIN_DOMAIN}`,
      ],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (D) RFP is exempted on the framer but not the framee nor another (if needed) cross-origin domain
async function testD(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (D)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      ["privacy.resistFingerprinting.exemptedDomains", `${FRAMER_DOMAIN}`],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (E) RFP is not exempted on the framer nor the framee but (if needed) is exempted on another cross-origin domain
async function testE(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (E)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      [
        "privacy.resistFingerprinting.exemptedDomains",
        `${CROSS_ORIGIN_DOMAIN}`,
      ],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (F) RFP is not exempted on the framer nor the framee nor another (if needed) cross-origin domain
async function testF(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (F)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      ["privacy.resistFingerprinting.exemptedDomains", ""],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (G) RFP is not exempted on the framer but is on the framee and (if needed) on another cross-origin domain
async function testG(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (G)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      [
        "privacy.resistFingerprinting.exemptedDomains",
        `${IFRAME_DOMAIN}, ${CROSS_ORIGIN_DOMAIN}`,
      ],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}

// (H) RFP is not exempted on the framer nor another (if needed) cross-origin domain but is on the framee
async function testH(
  uri,
  testFunction,
  expectedResults,
  extraData,
  extraPrefs
) {
  if (extraData == undefined) {
    extraData = {};
  }
  extraData.testDesc = extraData.testDesc || "test (H)";
  expectedResults.shouldRFPApply = true;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.resistFingerprinting", true],
      ["privacy.resistFingerprinting.exemptedDomains", `${IFRAME_DOMAIN}`],
    ].concat(extraPrefs || []),
  });

  await runActualTest(uri, testFunction, expectedResults, extraData);

  await SpecialPowers.popPrefEnv();
}
