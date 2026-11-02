/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_setup(async function () {
  // Disable other tracking protection feature to avoid interfering with the
  // current test. This also setup prefs for testing email tracking.
  await SpecialPowers.pushPrefEnv({
    set: [
      // Email tracking data collection is url-classifier only, so keep it as
      // the channel classifier where the content classifier is the default.
      ["privacy.trackingprotection.content.mirror.mode", 0],
      [
        "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier",
        false,
      ],
      ["privacy.trackingprotection.enabled", false],
      ["privacy.trackingprotection.pbmode.enabled", false],
      ["privacy.trackingprotection.annotate_channels", false],
      ["privacy.trackingprotection.cryptomining.enabled", false],
      ["privacy.trackingprotection.emailtracking.enabled", true],
      ["privacy.trackingprotection.fingerprinting.enabled", false],
      ["privacy.trackingprotection.socialtracking.enabled", false],
      [
        "urlclassifier.features.emailtracking.blocklistTables",
        "mochitest5-track-simple",
      ],
      ["urlclassifier.features.emailtracking.allowlistTables", ""],
      [
        "urlclassifier.features.emailtracking.datacollection.blocklistTables",
        "mochitest5-track-simple",
      ],
      [
        "urlclassifier.features.emailtracking.datacollection.allowlistTables",
        "",
      ],
    ],
  });

  await UrlClassifierTestUtils.addTestTrackers();

  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

function runTest(obj) {
  add_task(async _ => {
    info("Test: " + obj.testName);

    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "privacy.trackingprotection.emailtracking.enabled",
          obj.protectionEnabled,
        ],
        [
          "privacy.trackingprotection.emailtracking.pbmode.enabled",
          obj.protectionPrivateEnabled,
        ],
      ],
    });

    let win;

    if (obj.testPrivate) {
      win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
    } else {
      win = window;
    }

    info("Creating a non-tracker top-level context");
    let tab = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE);
    let browser = tab.linkedBrowser;
    await BrowserTestUtils.browserLoaded(browser);

    info("The non-tracker page opens an email tracker iframe");
    await SpecialPowers.spawn(
      browser,
      [
        {
          image: TEST_EMAIL_TRACKER_DOMAIN + TEST_PATH + "raptor.jpg",
          script: TEST_EMAIL_TRACKER_DOMAIN + TEST_PATH + "empty.js",
          loading: obj.loading,
        },
      ],
      async obj => {
        info("Image loading ...");
        let loading = await new content.Promise(resolve => {
          let image = new content.Image();
          image.src = obj.image + "?" + Math.random();
          image.onload = _ => resolve(true);
          image.onerror = _ => resolve(false);
        });

        is(loading, obj.loading, "Image loading expected");

        let script = content.document.createElement("script");
        script.setAttribute("src", obj.script);

        info("Script loading ...");
        loading = await new content.Promise(resolve => {
          script.onload = _ => resolve(true);
          script.onerror = _ => resolve(false);
          content.document.body.appendChild(script);
        });

        is(loading, obj.loading, "Script loading expected");
      }
    );

    info("Checking content blocking log.");
    let contentBlockingLog = JSON.parse(await browser.getContentBlockingLog());
    let origins = Object.keys(contentBlockingLog);
    is(origins.length, 1, "There should be one origin entry in the log.");
    for (let origin of origins) {
      is(
        origin + "/",
        TEST_EMAIL_TRACKER_DOMAIN,
        "Correct tracker origin must be reported"
      );
      Assert.deepEqual(
        contentBlockingLog[origin],
        obj.expectedLogItems,
        "Content blocking log should be as expected"
      );
    }

    BrowserTestUtils.removeTab(tab);
    if (obj.testPrivate) {
      await BrowserTestUtils.closeWindow(win);
    }
    await SpecialPowers.popPrefEnv();
  });
}

runTest({
  testName:
    "EmailTracking-dataCollection feature enabled but not considered for tracking detection.",
  protectionEnabled: false,
  protectionPrivateEnabled: false,
  loading: true,
  expectedLogItems: [
    [
      Ci.nsIWebProgressListener.STATE_LOADED_EMAILTRACKING_LEVEL_1_CONTENT,
      true,
      2,
    ],
  ],
});

runTest({
  testName: "Emailtracking-protection feature enabled.",
  protectionEnabled: true,
  protectionPrivateEnabled: true,
  loading: false,
  expectedLogItems: [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_EMAILTRACKING_CONTENT, true, 2],
  ],
});

runTest({
  testName:
    "Emailtracking-protection feature enabled for private windows and doesn't block in normal windows",
  protectionEnabled: false,
  protectionPrivateEnabled: true,
  loading: true,
  expectedLogItems: [
    [
      Ci.nsIWebProgressListener.STATE_LOADED_EMAILTRACKING_LEVEL_1_CONTENT,
      true,
      2,
    ],
  ],
});

runTest({
  testName:
    "Emailtracking-protection feature enabled for private windows and block in private windows",
  testPrivate: true,
  protectionEnabled: true,
  protectionPrivateEnabled: true,
  loading: false,
  expectedLogItems: [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_EMAILTRACKING_CONTENT, true, 1],
  ],
});
