"use strict";

const TEST_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "http://example.com"
);

const THIRD_PARTY_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "http://example.net"
);

const SHIMMABLE_TEST_PAGE = `${TEST_ROOT}shims_test.html`;
const SHIMMABLE_TEST_PAGE_2 = `${TEST_ROOT}shims_test_2.html`;
const SHIMMABLE_TEST_PAGE_3 = `${TEST_ROOT}shims_test_3.html`;
const EMBEDDING_TEST_PAGE = `${THIRD_PARTY_ROOT}iframe_test.html`;
const TEST_PAGE_WITH_SMARTBLOCK_COMPATIBLE_EMBED = `${TEST_ROOT}smartblock_embed_test.html`;
const TEST_PAGE_WITH_SMARTBLOCK_LINK_CONTAINER_EMBED = `${TEST_ROOT}smartblock_embed_link_container_test.html`;
const TEST_PAGE_WITH_SMARTBLOCK_COMPATIBLE_IFRAME_EMBED = `${TEST_ROOT}smartblock_embed_iframe_test.html`;
const BLOCKED_TRACKER_URL =
  "//trackertest.org/tests/toolkit/components/url-classifier/tests/mochitest/evil.js";

const DISABLE_SHIM1_PREF = "extensions.webcompat.disabled_shims.MochitestShim";
const DISABLE_SHIM2_PREF = "extensions.webcompat.disabled_shims.MochitestShim2";
const DISABLE_SHIM3_PREF = "extensions.webcompat.disabled_shims.MochitestShim3";
const DISABLE_SHIM4_PREF = "extensions.webcompat.disabled_shims.MochitestShim4";
const GLOBAL_PREF = "extensions.webcompat.enable_shims";
const TRACKING_PREF = "privacy.trackingprotection.enabled";
const SEC_DELAY_PREF = "security.notification_enable_delay";
const SMARTBLOCK_EMBEDS_ENABLED_PREF =
  "extensions.webcompat.smartblockEmbeds.enabled";

const CONTENT_CLASSIFIER_TESTING_PREF =
  "privacy.trackingprotection.content.testing";
const CONTENT_CLASSIFIER_PROTECTION_ENABLED_PREF =
  "privacy.trackingprotection.content.protection.enabled";
const CONTENT_CLASSIFIER_PROTECTION_LIST_URLS_PREF =
  "privacy.trackingprotection.content.protection.test_list_urls";
const CONTENT_CLASSIFIER_MIRROR_MODE_PREF =
  "privacy.trackingprotection.content.mirror.mode";
const CONTENT_CLASSIFIER_BLOCK_LIST_URL = `${TEST_ROOT}content_classifier_block_list.txt`;
// Must match NS_CONTENT_CLASSIFIER_FILTER_LISTS_LOADED_TOPIC in
// nsIContentClassifierService.idl.
const CONTENT_CLASSIFIER_LISTS_LOADED_TOPIC =
  "test-content-classifier-filter-lists-loaded";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const WebCompatExtension = new (class WebCompatExtension {
  constructor() {
    const { extension } = WebExtensionPolicy.getByID("webcompat@mozilla.org");
    this.extension = extension;
  }

  async started() {
    await this.extension.promiseBackgroundStarted();
  }

  // Note that the `extensions.background.idle.timeout` pref needs to be set to a
  // a high value if using the following APIs, to avoid intermittent failures due
  // to the background being stopped for being idle.
  async #run(command, ...args) {
    return await SpecialPowers.spawn(
      this.extension.backgroundContext.xulBrowser,
      args,
      command
    );
  }

  async resetInterventionsAndShimsToDefaults() {
    return this.#run(async function () {
      await content.wrappedJSObject._downgradeForTesting();
    }).catch(_ => {});
  }

  async allOriginalInterventions() {
    return this.#run(async function () {
      const available =
        content.wrappedJSObject.interventions.getAllOriginalInterventions();
      // structured cloning won't work, so get the interesting bits for tests.
      return JSON.parse(JSON.stringify(available));
    });
  }

  async availableInterventions() {
    return this.#run(async function () {
      const available =
        content.wrappedJSObject.interventions.getAvailableInterventions();
      // structured cloning won't work, so get the interesting bits for tests.
      return JSON.parse(JSON.stringify(available));
    });
  }

  async availableShims() {
    return this.#run(async function () {
      // structured cloning won't work, so get the interesting bits for tests.
      const available = [];
      for (let shim of content.wrappedJSObject.shims.shims.values()) {
        const final = {};
        for (let [name, value] of Object.entries(shim)) {
          if (name !== "manager" && !name.startsWith("_")) {
            final[name] = value;
          }
        }
        final.enabled = shim.enabled;
        available.push(final);
      }
      return JSON.parse(JSON.stringify(available));
    });
  }

  async promiseUpdateReceived(_version) {
    return this.#run(async function (version) {
      await new Promise(updated => {
        const updateCheck = content.setInterval(() => {
          if (content.wrappedJSObject.latestReceivedUpdate == version) {
            content.clearInterval(updateCheck);
            updated();
          }
        }, 100);
      });
    }, _version);
  }

  async interventionsSettled() {
    return this.#run(async function () {
      await content.wrappedJSObject.interventions.allSettled();
    });
  }

  async overrideFirefoxVersion(_ver) {
    this.#run(async function (ver) {
      content.wrappedJSObject.interventions.appVersionOverride = ver
        ? parseFloat(ver)
        : undefined;
    }, _ver);
  }

  async getInterventionById(_id) {
    return this.#run(function (id) {
      return content.wrappedJSObject.interventions.getInterventionsByIds(
        Cu.cloneInto([id], content)
      )[0];
    }, _id);
  }

  getCheckableGlobalPrefs() {
    return this.#run(async function () {
      return content.wrappedJSObject.browser.aboutConfigPrefs.getCheckableGlobalPrefs();
    });
  }

  async updateShims(_shims) {
    return this.#run(async function (shims) {
      await content.wrappedJSObject.shims.ready();
      await content.wrappedJSObject.shims._updateShims(
        Cu.cloneInto(shims, content)
      );
    }, _shims);
  }

  async shimsReady() {
    return this.#run(async function () {
      await content.wrappedJSObject.shims.ready();
    });
  }

  // Force a shim's enabled-state transition to fully complete (including the
  // AllowList registration via browser.trackingProtection.shim/revoke).
  // Toggling extensions.webcompat.disabled_shims.<id> from the parent process
  // triggers the extension's pref-change listener which calls
  // _onEnabledStateChanged but does NOT await it, so the listener finishes
  // before _allowRequestsInETP has updated the urlclassifier-before-block-
  // channel AllowList. This explicitly re-invokes and awaits the transition.
  async settleShimStateChange(_id) {
    return this.#run(async function (id) {
      const shim = content.wrappedJSObject.shims.shims.get(id);
      if (!shim) {
        return;
      }
      await shim._onEnabledStateChanged({ alsoClearResourceCache: true });
    }, _id);
  }

  async getRegisteredContentScriptsFor(_id) {
    return this.#run(async function (id) {
      const scripts =
        await content.wrappedJSObject.browser.scripting.getRegisteredContentScripts();
      return scripts.filter(script =>
        script.id.startsWith(`webcompat intervention for ${id}`)
      );
    }, _id);
  }

  async disableInterventions(_ids) {
    return this.#run(async function (ids) {
      return await content.wrappedJSObject.interventions.disableInterventions(
        Cu.cloneInto(ids, content)
      );
    }, _ids);
  }

  async updateInterventions(_config) {
    return this.#run(async function (config) {
      return await content.wrappedJSObject.interventions.updateInterventions(
        Cu.cloneInto(config, content)
      );
    }, _config);
  }
})();

registerCleanupFunction(async () => {
  await WebCompatExtension.overrideFirefoxVersion(undefined);
  await WebCompatExtension.resetInterventionsAndShimsToDefaults();
});

async function testShimRuns(
  testPage,
  frame,
  trackersAllowed = true,
  expectOptIn = true
) {
  await WebCompatExtension.shimsReady();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: testPage,
    waitForLoad: true,
  });

  const TrackingProtection =
    tab.documentGlobal.gProtectionsHandler.blockers.TrackingProtection;
  ok(TrackingProtection, "TP is attached to the tab");
  ok(TrackingProtection.enabled, "TP is enabled");

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [[trackersAllowed, BLOCKED_TRACKER_URL, expectOptIn], frame],
    async (args, _frame) => {
      const window = _frame === undefined ? content : content.frames[_frame];

      await SpecialPowers.spawn(
        window,
        args,
        async (_trackersAllowed, trackerUrl, _expectOptIn) => {
          const shimResult = await content.wrappedJSObject.shimPromise;
          is("shimmed", shimResult, "Shim activated");

          const optInResult = await content.wrappedJSObject.optInPromise;
          is(_expectOptIn, optInResult, "Shim allowed opt in if appropriate");

          const o = content.document.getElementById("shims");
          const cl = o.classList;
          const opts = JSON.parse(o.innerText);
          is(
            undefined,
            opts.branchValue,
            "Shim script did not receive option for other branch"
          );
          is(
            undefined,
            opts.platformValue,
            "Shim script did not receive option for other platform"
          );
          is(
            true,
            opts.simpleOption,
            "Shim script received simple option correctly"
          );
          ok(opts.complexOption, "Shim script received complex option");
          is(
            1,
            opts.complexOption.a,
            "Shim script received complex options correctly #1"
          );
          is(
            "test",
            opts.complexOption.b,
            "Shim script received complex options correctly #2"
          );
          ok(cl.contains("green"), "Shim affected page correctly");
        }
      );
    }
  );

  await BrowserTestUtils.removeTab(tab);
}

async function testShimDoesNotRun(
  trackersAllowed = false,
  testPage = SHIMMABLE_TEST_PAGE
) {
  await WebCompatExtension.shimsReady();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: testPage,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [trackersAllowed, BLOCKED_TRACKER_URL],
    async (_trackersAllowed, trackerUrl) => {
      const shimResult = await content.wrappedJSObject.shimPromise;
      is("did not shim", shimResult, "Shim did not activate");

      ok(
        !content.document.getElementById("shims").classList.contains("green"),
        "Shim script did not run"
      );

      is(
        _trackersAllowed ? "ALLOWED" : "BLOCKED",
        await new Promise(resolve => {
          const s = content.document.createElement("script");
          s.src = trackerUrl;
          s.onload = () => resolve("ALLOWED");
          s.onerror = () => resolve("BLOCKED");
          content.document.head.appendChild(s);
        }),
        "Normally-blocked resources blocked if appropriate"
      );
    }
  );

  await BrowserTestUtils.removeTab(tab);
}

function panelId() {
  return Services.prefs.getBoolPref("browser.urlbar.trustPanel.featureGate")
    ? "trustpanel-popup"
    : "protections-popup";
}

async function closeProtectionsPanel(win = window) {
  let protectionsPopup = win.document.getElementById(panelId());
  if (!protectionsPopup) {
    return;
  }
  let popuphiddenPromise = BrowserTestUtils.waitForEvent(
    protectionsPopup,
    "popuphidden"
  );

  PanelMultiView.hidePopup(protectionsPopup);
  await popuphiddenPromise;
}

async function openProtectionsPanel(win = window) {
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    win,
    "popupshown",
    true,
    e => e.target.id == panelId()
  );

  if (Services.prefs.getBoolPref("browser.urlbar.trustPanel.featureGate")) {
    win.gTrustPanelHandler.showPopup();
  } else {
    win.gProtectionsHandler.showProtectionsPopup();
  }

  await popupShownPromise;
}

async function loadSmartblockPageOnTab(
  tab,
  url = TEST_PAGE_WITH_SMARTBLOCK_COMPATIBLE_EMBED
) {
  let smartblockScriptFinished = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "smartblockEmbedScriptFinished",
    false,
    null,
    true
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, url);

  return smartblockScriptFinished;
}

async function clickOnPagePlaceholder(tab) {
  // Setup promise for listening for protections panel open
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    window,
    "popupshown",
    true,
    e => e.target.id == panelId()
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    // Check that the "embed" was replaced with a placeholder
    let placeholder = content.document.querySelector(
      ".shimmed-embedded-content"
    );
    ok(placeholder, "Embed is replaced with a placeholder");

    // Get the button element from the placeholder
    let shadowRoot = placeholder.openOrClosedShadowRoot;
    ok(shadowRoot, "Shadow root exists");

    // Check that all elements are present
    let placeholderButton = shadowRoot.querySelector(
      "#smartblock-placeholder-button"
    );
    ok(placeholderButton, "Placeholder button exists");

    let placeholderTitle = shadowRoot.querySelector(
      "#smartblock-placeholder-title"
    );
    ok(placeholderTitle, "Placeholder title exists");

    let placeholderLabel = shadowRoot.querySelector(
      "#smartblock-placeholder-desc"
    );
    ok(placeholderLabel, "Placeholder description exists");

    let placeholderImage = shadowRoot.querySelector(
      "#smartblock-placeholder-image"
    );
    ok(placeholderImage, "Placeholder image exists");

    // Click button to open protections panel
    EventUtils.synthesizeMouseAtCenter(placeholderButton, {}, content);
  });

  // If this await finished, then protections panel is open
  return popupShownPromise;
}

async function enableContentClassifierBlockList(blockListUrl) {
  let wasEnabled = Services.prefs.getBoolPref(
    CONTENT_CLASSIFIER_PROTECTION_ENABLED_PREF,
    false
  );
  let currentUrl = Services.prefs.getStringPref(
    CONTENT_CLASSIFIER_PROTECTION_LIST_URLS_PREF,
    ""
  );
  let needsLoad = !wasEnabled || currentUrl !== blockListUrl;
  let listsLoaded = needsLoad
    ? TestUtils.topicObserved(CONTENT_CLASSIFIER_LISTS_LOADED_TOPIC)
    : null;

  // Take the content prefs back from the ETP pref mirror, which would
  // otherwise overwrite the engines pref the test list relies on.
  Services.prefs.setIntPref(CONTENT_CLASSIFIER_MIRROR_MODE_PREF, 0);
  Services.prefs.setBoolPref(CONTENT_CLASSIFIER_TESTING_PREF, true);
  Services.prefs.setBoolPref(CONTENT_CLASSIFIER_PROTECTION_ENABLED_PREF, true);
  Services.prefs.setStringPref(
    CONTENT_CLASSIFIER_PROTECTION_LIST_URLS_PREF,
    blockListUrl
  );

  if (listsLoaded) {
    await listsLoaded;
  }
}

function disableContentClassifier() {
  Services.prefs.clearUserPref(CONTENT_CLASSIFIER_TESTING_PREF);
  Services.prefs.clearUserPref(CONTENT_CLASSIFIER_PROTECTION_ENABLED_PREF);
  Services.prefs.clearUserPref(CONTENT_CLASSIFIER_PROTECTION_LIST_URLS_PREF);
  Services.prefs.clearUserPref(CONTENT_CLASSIFIER_MIRROR_MODE_PREF);
}

// Wait for the webcompat extension to settle a shim's effective enabled
// state. Toggling `extensions.webcompat.disabled_shims.<id>` triggers an
// async pref-change listener in the extension that updates the shim and
// re-registers its matches with the urlclassifier-before-block-channel
// AllowList; polling shim.enabled alone only confirms the listener has
// observed the new pref value, not that the registration has completed,
// so we follow up with settleShimStateChange.
async function waitForShimEnabledState(shimId, expectedEnabled) {
  await TestUtils.waitForCondition(
    async () => {
      const shims = await WebCompatExtension.availableShims();
      const shim = shims.find(s => s.id === shimId);
      return shim?.enabled === expectedEnabled;
    },
    `Shim ${shimId} should be ${expectedEnabled ? "enabled" : "disabled"}`
  );
  await WebCompatExtension.settleShimStateChange(shimId);
}

async function generateTestShims() {
  await WebCompatExtension.updateShims([
    {
      id: "MochitestShim",
      platform: "all",
      branch: ["all:ignoredOtherPlatform"],
      name: "Test shim for Mochitests",
      bug: "mochitest",
      file: "mochitest-shim-1.js",
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test.js",
      ],
      needsShimHelpers: ["getOptions", "optIn"],
      options: {
        simpleOption: true,
        complexOption: { a: 1, b: "test" },
        branchValue: { value: true, branches: [] },
        platformValue: { value: true, platform: "neverUsed" },
      },
      unblocksOnOptIn: ["*://trackertest.org/*"],
    },
    {
      disabled: true,
      id: "MochitestShim2",
      platform: "all",
      name: "Test shim for Mochitests (disabled by default)",
      bug: "mochitest",
      file: "mochitest-shim-2.js",
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test_2.js",
      ],
      needsShimHelpers: ["getOptions", "optIn"],
      options: {
        simpleOption: true,
        complexOption: { a: 1, b: "test" },
        branchValue: { value: true, branches: [] },
        platformValue: { value: true, platform: "neverUsed" },
      },
      unblocksOnOptIn: ["*://trackertest.org/*"],
    },
    {
      id: "MochitestShim3",
      platform: "all",
      name: "Test shim for Mochitests (host)",
      bug: "mochitest",
      file: "mochitest-shim-3.js",
      notHosts: ["example.com"],
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test_3.js",
      ],
    },
    {
      id: "MochitestShim4",
      platform: "all",
      name: "Test shim for Mochitests (notHost)",
      bug: "mochitest",
      file: "mochitest-shim-3.js",
      hosts: ["example.net"],
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test_3.js",
      ],
    },
    {
      id: "MochitestShim5",
      platform: "all",
      name: "Test shim for Mochitests (branch)",
      bug: "mochitest",
      file: "mochitest-shim-3.js",
      branches: ["never matches"],
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test_3.js",
      ],
    },
    {
      id: "MochitestShim6",
      platform: "never matches",
      name: "Test shim for Mochitests (platform)",
      bug: "mochitest",
      file: "mochitest-shim-3.js",
      matches: [
        "*://example.com/browser/browser/extensions/webcompat/tests/browser/shims_test_3.js",
      ],
    },
    {
      id: "MochitestShimXHR",
      platform: "all",
      name: "Test shim for fetch blocking",
      bug: "mochitest",
      file: "empty-shim.txt",
      matches: [
        "*://itisatracker.org/browser/browser/extensions/webcompat/tests/browser/shims_test_fetch.txt",
      ],
      onlyIfBlockedByETP: true,
    },
    {
      // Shim used by browser_shims.js content-classifier tests. Registers
      // a match for the fetch URL so the urlclassifier-before-block-channel
      // listener will call channel.replace(). No file/target/runFirst so
      // there is no webRequest redirect and the content classifier sees
      // the original URL.
      disabled: true,
      id: "MochitestShimContent",
      platform: "all",
      name: "Test shim for content classifier replace path",
      bug: "mochitest",
      matches: [
        {
          patterns: [
            "*://itisatracker.org/browser/browser/extensions/webcompat/tests/browser/shims_test_fetch.txt",
          ],
          types: ["xmlhttprequest"],
        },
      ],
    },
    {
      id: "EmbedTestShim",
      platform: "desktop",
      name: "Test shim for smartblock embed unblocking",
      bug: "1892175",
      runFirst: "embed-test-shim.js",
      // Blank stub file just so we run the script above when the matched script
      // files get blocked.
      file: "empty-script.js",
      matches: [
        "https://itisatracker.org/browser/browser/extensions/webcompat/tests/browser/embed_test.js",
        {
          patterns: [
            "https://itisatracker.org/browser/browser/extensions/webcompat/tests/browser/embed_test_iframe.html",
          ],
          types: ["sub_frame"],
          target: "empty-page.html",
        },
      ],
      // Use instagram logo as an example
      logos: ["instagram.svg"],
      needsShimHelpers: [
        "embedClicked",
        "smartblockGetFluentString",
        "shouldShowEmbedContentInPlaceholders",
      ],
      isSmartblockEmbedShim: true,
      onlyIfBlockedByETP: true,
      unblocksOnOptIn: ["*://itisatracker.org/*"],
    },
  ]);
  registerCleanupFunction(async () => {
    await WebCompatExtension.resetInterventionsAndShimsToDefaults();
  });
}
