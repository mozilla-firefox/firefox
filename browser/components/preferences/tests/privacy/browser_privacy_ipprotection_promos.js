/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// This file tests the promos of the Privacy pane's Firefox VPN UI.

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { IPProtectionActivator } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs"
);

const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
  "resource://testing-common/ipprotection/IPPDummyAuthProvider.sys.mjs"
);
IPProtectionActivator.setAuthProvider(IPPDummyAuthProvider);

const FEATURE_PREF = "browser.ipProtection.enabled";
const SITE_EXCEPTIONS_FEATURE_PREF =
  "browser.ipProtection.features.siteExceptions";
const SITE_INCLUSIONS_FEATURE_PREF =
  "browser.ipProtection.features.siteInclusions";
const AUTOSTART_FEATURE_ENABLED_PREF =
  "browser.ipProtection.features.autoStart";
const BANDWIDTH_FEATURE_ENABLED_PREF = "browser.ipProtection.bandwidth.enabled";
const AUTOSTART_PREF = "browser.ipProtection.autoStartEnabled";
const AUTOSTART_PRIVATE_PREF = "browser.ipProtection.autoStartPrivateEnabled";
const ONBOARDING_MESSAGE_MASK_PREF =
  "browser.ipProtection.onboardingMessageMask";
const ENTITLEMENT_CACHE_PREF = "browser.ipProtection.entitlementCache";
const USAGE_CACHE_PREF = "browser.ipProtection.usageCache";
const IPP_ADDED_PREF = "browser.ipProtection.added";
const IPP_STATE_CACHE_PREF = "browser.ipProtection.stateCache";
const IPP_PANEL_HAS_OPENED_PREF = "browser.ipProtection.everOpenedPanel";
const IPP_CACHE_DISABLED_PREF = "browser.ipProtection.cacheDisabled";
const UPGRADE_NOT_AVAILABLE_PREF = "browser.ipProtection.upgradeNotAvailable";

add_setup(async function ippSetup() {
  await SpecialPowers.pushPrefEnv({
    set: [[IPP_CACHE_DISABLED_PREF, true]],
  });

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(ONBOARDING_MESSAGE_MASK_PREF);
    Services.prefs.clearUserPref(IPP_ADDED_PREF);
    Services.prefs.clearUserPref(IPP_STATE_CACHE_PREF);
    Services.prefs.clearUserPref(IPP_PANEL_HAS_OPENED_PREF);
  });
});

async function setupVpnPrefs({
  feature = false,
  siteExceptions = false,
  siteInclusions = false,
  autostartFeatureEnabled = false,
  bandwidth = false,
  autostart = false,
  autostartprivate = false,
  entitlementCache = "",
  usageCache = "",
  upgradeNotAvailable = false,
}) {
  let prefs = [
    [FEATURE_PREF, feature],
    [SITE_EXCEPTIONS_FEATURE_PREF, siteExceptions],
    [SITE_INCLUSIONS_FEATURE_PREF, siteInclusions],
    [AUTOSTART_FEATURE_ENABLED_PREF, autostartFeatureEnabled],
    [BANDWIDTH_FEATURE_ENABLED_PREF, bandwidth],
    [AUTOSTART_PREF, autostart],
    [AUTOSTART_PRIVATE_PREF, autostartprivate],
    [ENTITLEMENT_CACHE_PREF, entitlementCache],
    [USAGE_CACHE_PREF, usageCache],
    [UPGRADE_NOT_AVAILABLE_PREF, upgradeNotAvailable],
  ];

  return SpecialPowers.pushPrefEnv({
    set: prefs,
  });
}

function testSettingsGroupVisible(browser) {
  let settingGroup = browser.contentDocument.querySelector(
    `setting-group[groupid="ipprotection"]`
  );
  is_element_visible(settingGroup, "ipprotection setting group is shown");

  return settingGroup;
}

/**
 * Replaces the preferences window's shell service with a fake reporting
 * isDefault, and refreshes the VPN default-browser setting so it is re-read.
 *
 * @param {MozBrowser} browser
 * @param {object} sandbox
 * @param {boolean} isDefault
 * @returns {object} The fake shell service.
 */
function stubDefaultBrowser(browser, sandbox, isDefault) {
  let win = browser.contentWindow;
  let fakeShellService = {
    isDefaultBrowser: () => isDefault,
    setDefaultBrowser: sandbox.stub().callsFake(async () => {
      isDefault = true;
    }),
  };
  sandbox.stub(win, "getShellService").returns(fakeShellService);
  win.Preferences.getSetting("ipProtectionIsDefaultBrowser").onChange();
  return fakeShellService;
}

async function assertVpnPromosVisibility(
  settingGroup,
  { vpnPromo, setDefaultPromo }
) {
  let vpnPromoEl = settingGroup.querySelector("#ipProtectionMozVPNPromo");
  let setDefaultPromoEl = settingGroup.querySelector(
    "#ipProtectionSetToDefaultPromo"
  );
  await TestUtils.waitForCondition(
    () =>
      BrowserTestUtils.isVisible(vpnPromoEl) == vpnPromo &&
      BrowserTestUtils.isVisible(setDefaultPromoEl) == setDefaultPromo,
    "Waiting for VPN promos visibility to update"
  );
  Assert.equal(
    BrowserTestUtils.isVisible(vpnPromoEl),
    vpnPromo,
    `Mozilla VPN promo is ${vpnPromo ? "shown" : "hidden"}`
  );
  Assert.equal(
    BrowserTestUtils.isVisible(setDefaultPromoEl),
    setDefaultPromo,
    `Set to default promo is ${setDefaultPromo ? "shown" : "hidden"}`
  );
}

// Test that only the set to default promo is shown when Firefox is not the
// default browser.
add_task(async function test_vpn_promos_not_default_browser() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    entitlementCache: '{"subscribed": false}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      stubDefaultBrowser(browser, sandbox, false);
      await assertVpnPromosVisibility(settingGroup, {
        vpnPromo: false,
        setDefaultPromo: true,
      });
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

// Test that only the Mozilla VPN promo is shown when Firefox is the default
// browser.
add_task(async function test_vpn_promos_default_browser() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    entitlementCache: '{"subscribed": false}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      stubDefaultBrowser(browser, sandbox, true);
      await assertVpnPromosVisibility(settingGroup, {
        vpnPromo: true,
        setDefaultPromo: false,
      });
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

// Test that the Mozilla VPN promo is hidden when upgradeNotAvailable is true.
add_task(async function test_vpn_promo_hidden_if_upgrade_not_available() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    entitlementCache: '{"subscribed": false}',
    upgradeNotAvailable: true,
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      stubDefaultBrowser(browser, sandbox, true);
      await assertVpnPromosVisibility(settingGroup, {
        vpnPromo: false,
        setDefaultPromo: false,
      });
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

// Test that both promos are hidden when the user has upgraded.
add_task(async function test_vpn_promos_hidden_if_subscribed() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    entitlementCache: '{"subscribed": true}',
  });

  for (let isDefault of [true, false]) {
    let sandbox = sinon.createSandbox();
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);
        stubDefaultBrowser(browser, sandbox, isDefault);
        await assertVpnPromosVisibility(settingGroup, {
          vpnPromo: false,
          setDefaultPromo: false,
        });
      }
    );
    sandbox.restore();
  }

  await SpecialPowers.popPrefEnv();
});

/**
 * Returns the recorded events of a promo metric for the given promo. The page
 * may render with the real default browser state before the test stubs it, so
 * events are filtered by promo rather than asserted as a whole.
 *
 * @param {object} metric - A Glean event metric.
 * @param {string} promo - "mozillavpn" or "defaultbrowser".
 * @returns {Array<object>}
 */
function getPromoEvents(metric, promo) {
  return (metric.testGetValue() ?? []).filter(e => e.extra.promo == promo);
}

/**
 * Waits for the VPN promos to reach the expected visibility.
 *
 * @param {Element} settingGroup
 * @param {{vpn: boolean, setDefault: boolean}} expected
 * @returns {Promise<{vpn: boolean, setDefault: boolean}>}
 *   The promos visibility once it matches, or when waiting times out.
 */
async function waitForVpnPromos(settingGroup, expected) {
  let promos = {
    vpn: settingGroup.querySelector("#ipProtectionMozVPNPromo"),
    setDefault: settingGroup.querySelector("#ipProtectionSetToDefaultPromo"),
  };
  await TestUtils.waitForCondition(() => {
    let states = getPromoStates(promos);
    return (
      states.vpn == expected.vpn && states.setDefault == expected.setDefault
    );
  }, "Waiting for VPN promos visibility to update").catch(() => {});
  return getPromoStates(promos);
}

/**
 * Scrolls a promo into view and waits for its promo_seen event, which is only
 * recorded once the promo is on screen.
 *
 * @param {Element} settingGroup
 * @param {string} promoId - The id of the moz-promo element.
 * @param {string} promo - "mozillavpn" or "defaultbrowser".
 */
async function scrollPromoIntoViewAndWaitForSeen(settingGroup, promoId, promo) {
  settingGroup.querySelector(`#${promoId}`).scrollIntoView();
  await TestUtils.waitForCondition(
    () => getPromoEvents(Glean.ipprotection.promoSeen, promo).length,
    `Waiting for promo_seen for the ${promo} promo`
  ).catch(() => {});
}

// Test that promo_seen isn't recorded when the promo becomes visible while the
// privacy pane isn't shown, and is recorded once the user opens the privacy
// pane and sees it.
add_task(async function test_promo_seen_waits_for_promo_on_screen() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteInclusions: true,
    entitlementCache: '{"subscribed": false}',
  });
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let win = browser.contentWindow;
      let doc = browser.contentDocument;
      let settingGroup = testSettingsGroupVisible(browser);
      let fakeShellService = stubDefaultBrowser(browser, sandbox, false);
      await waitForVpnPromos(settingGroup, { vpn: false, setDefault: true });

      let paneShown = BrowserTestUtils.waitForEvent(doc, "paneshown");
      win.gotoPref("paneGeneral");
      await paneShown;

      await fakeShellService.setDefaultBrowser();
      let promoSetting = win.Preferences.getSetting("ipProtectionMozVPNPromo");
      win.Preferences.getSetting("ipProtectionIsDefaultBrowser").onChange();
      await TestUtils.waitForCondition(
        () => promoSetting.visible,
        "Waiting for the Mozilla VPN promo setting to be visible"
      );
      await settingGroup.updateComplete;
      await new Promise(resolve =>
        win.requestAnimationFrame(() => win.requestAnimationFrame(resolve))
      );
      Assert.ok(
        !BrowserTestUtils.isVisible(
          settingGroup.querySelector("#ipProtectionMozVPNPromo")
        ),
        "Mozilla VPN promo isn't on screen while the privacy pane is hidden"
      );
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoSeen, "mozillavpn").length,
        0,
        "No promo_seen event recorded while the privacy pane isn't shown"
      );

      paneShown = BrowserTestUtils.waitForEvent(doc, "paneshown");
      win.gotoPref("panePrivacy");
      await paneShown;
      await scrollPromoIntoViewAndWaitForSeen(
        settingGroup,
        "ipProtectionMozVPNPromo",
        "mozillavpn"
      );
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoSeen, "mozillavpn").length,
        1,
        "Recorded one promo_seen event once the promo is on screen"
      );
    }
  );

  sandbox.restore();
  Services.fog.testResetFOG();
  await SpecialPowers.popPrefEnv();
});

// Test that promo_seen is recorded once per page for the Mozilla VPN promo,
// not on every re-render, and that clicking it records promo_clicked.
add_task(async function test_vpn_promo_seen_and_clicked_telemetry() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteInclusions: true,
    entitlementCache: '{"subscribed": false}',
  });
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let win = browser.contentWindow;
      let settingGroup = testSettingsGroupVisible(browser);
      stubDefaultBrowser(browser, sandbox, true);
      let promos = await waitForVpnPromos(settingGroup, {
        vpn: true,
        setDefault: false,
      });
      Assert.ok(promos.vpn, "Mozilla VPN promo is shown");
      Assert.ok(!promos.setDefault, "Set to default promo is hidden");

      await scrollPromoIntoViewAndWaitForSeen(
        settingGroup,
        "ipProtectionMozVPNPromo",
        "mozillavpn"
      );
      let seenEvents = getPromoEvents(
        Glean.ipprotection.promoSeen,
        "mozillavpn"
      );
      Assert.equal(
        seenEvents.length,
        1,
        "Recorded one promo_seen event for the Mozilla VPN promo"
      );
      Assert.deepEqual(
        seenEvents[0].extra,
        { promo: "mozillavpn", entrypoint: "settings" },
        "promo_seen records the Mozilla VPN promo and the settings entrypoint"
      );

      let isDefaultSetting = win.Preferences.getSetting(
        "ipProtectionIsDefaultBrowser"
      );
      isDefaultSetting.onChange();
      await settingGroup.updateComplete;
      isDefaultSetting.onChange();
      await settingGroup.updateComplete;
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoSeen, "mozillavpn").length,
        1,
        "Re-rendering does not record promo_seen again"
      );

      let openLinkStub = sandbox.stub(window, "openTrustedLinkIn");
      let button = settingGroup.querySelector("#mozVPNPromoButton");
      button.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(button, {}, win);
      await TestUtils.waitForCondition(
        () => openLinkStub.called,
        "Waiting for the Mozilla VPN promo link to open"
      );

      let clickedEvents = getPromoEvents(
        Glean.ipprotection.promoClicked,
        "mozillavpn"
      );
      Assert.equal(
        clickedEvents.length,
        1,
        "Recorded one promo_clicked event for the Mozilla VPN promo"
      );
      Assert.deepEqual(
        clickedEvents[0].extra,
        { promo: "mozillavpn", entrypoint: "settings" },
        "promo_clicked records the Mozilla VPN promo and the settings entrypoint"
      );
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoClicked, "defaultbrowser")
          .length,
        0,
        "No promo_clicked event recorded for the set to default promo"
      );
    }
  );

  sandbox.restore();
  Services.fog.testResetFOG();
  await SpecialPowers.popPrefEnv();
});

// Test that promo_seen and promo_clicked are recorded for the set to default
// promo, and that the Mozilla VPN promo records its own promo_seen when it
// replaces the set to default promo.
add_task(async function test_set_default_promo_seen_and_clicked_telemetry() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteInclusions: true,
    entitlementCache: '{"subscribed": false}',
  });
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let win = browser.contentWindow;
      let settingGroup = testSettingsGroupVisible(browser);
      let fakeShellService = stubDefaultBrowser(browser, sandbox, false);
      let promos = await waitForVpnPromos(settingGroup, {
        vpn: false,
        setDefault: true,
      });
      Assert.ok(!promos.vpn, "Mozilla VPN promo is hidden");
      Assert.ok(promos.setDefault, "Set to default promo is shown");

      await scrollPromoIntoViewAndWaitForSeen(
        settingGroup,
        "ipProtectionSetToDefaultPromo",
        "defaultbrowser"
      );
      let seenEvents = getPromoEvents(
        Glean.ipprotection.promoSeen,
        "defaultbrowser"
      );
      Assert.equal(
        seenEvents.length,
        1,
        "Recorded one promo_seen event for the set to default promo"
      );
      Assert.deepEqual(
        seenEvents[0].extra,
        { promo: "defaultbrowser", entrypoint: "settings" },
        "promo_seen records the set to default promo and the settings entrypoint"
      );

      let button = settingGroup.querySelector("#setToDefaultPromoButton");
      button.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(button, {}, win);
      await TestUtils.waitForCondition(
        () => fakeShellService.setDefaultBrowser.called,
        "Waiting for the set to default promo to set the default browser"
      );

      let clickedEvents = getPromoEvents(
        Glean.ipprotection.promoClicked,
        "defaultbrowser"
      );
      Assert.equal(
        clickedEvents.length,
        1,
        "Recorded one promo_clicked event for the set to default promo"
      );
      Assert.deepEqual(
        clickedEvents[0].extra,
        { promo: "defaultbrowser", entrypoint: "settings" },
        "promo_clicked records the set to default promo and the settings entrypoint"
      );

      promos = await waitForVpnPromos(settingGroup, {
        vpn: true,
        setDefault: false,
      });
      Assert.ok(
        promos.vpn,
        "Mozilla VPN promo is shown once Firefox is default"
      );
      Assert.ok(
        !promos.setDefault,
        "Set to default promo is hidden once Firefox is default"
      );
      await scrollPromoIntoViewAndWaitForSeen(
        settingGroup,
        "ipProtectionMozVPNPromo",
        "mozillavpn"
      );
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoSeen, "mozillavpn").length,
        1,
        "Recorded one promo_seen event for the Mozilla VPN promo"
      );
      Assert.equal(
        getPromoEvents(Glean.ipprotection.promoSeen, "defaultbrowser").length,
        1,
        "promo_seen for the set to default promo is still recorded once"
      );
    }
  );

  sandbox.restore();
  Services.fog.testResetFOG();
  await SpecialPowers.popPrefEnv();
});

// Test that promo_seen is not recorded when the user has upgraded.
add_task(async function test_no_promo_seen_telemetry_if_subscribed() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteInclusions: true,
    entitlementCache: '{"subscribed": true}',
  });
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      stubDefaultBrowser(browser, sandbox, false);
      let promos = await waitForVpnPromos(settingGroup, {
        vpn: false,
        setDefault: false,
      });
      Assert.ok(!promos.vpn, "Mozilla VPN promo is hidden");
      Assert.ok(!promos.setDefault, "Set to default promo is hidden");
      Assert.equal(
        Glean.ipprotection.promoSeen.testGetValue(),
        null,
        "No promo_seen event recorded when no promo is shown"
      );
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Holds back only the shared DefaultBrowserHelper poll so the test decides
 * when each tick runs. The captured callbacks are the helper's own, so the
 * hash and visibility guard, the shell read and the listener notification all
 * run unmodified. All other window timers pass through.
 *
 * @param {Window} win
 * @param {object} sandbox
 * @returns {{ readonly armed: boolean, tick: () => void }}
 */
function interceptDefaultBrowserPoll(win, sandbox) {
  let nativeSetTimeout = win.setTimeout;
  let nativeClearTimeout = win.clearTimeout;
  let nativeRequestIdleCallback = win.requestIdleCallback;
  let timer = null;
  let idle = null;
  let lastTimerId = 1000000;

  sandbox.stub(win, "setTimeout").callsFake((cb, delay, ...args) => {
    if (!String(cb).includes("pollForDefaultBrowser")) {
      return nativeSetTimeout.call(win, cb, delay, ...args);
    }
    timer = { id: ++lastTimerId, cb };
    return timer.id;
  });
  sandbox.stub(win, "clearTimeout").callsFake(id => {
    if (timer?.id === id) {
      timer = null;
      return;
    }
    nativeClearTimeout.call(win, id);
  });
  sandbox.stub(win, "requestIdleCallback").callsFake((cb, options) => {
    if (cb.name !== "pollForDefaultBrowser") {
      return nativeRequestIdleCallback.call(win, cb, options);
    }
    idle = cb;
    return 0;
  });

  return {
    get armed() {
      return !!(timer || idle);
    },
    tick() {
      if (timer) {
        let { cb } = timer;
        timer = null;
        cb();
      }
      let poll = idle;
      idle = null;
      if (!poll) {
        throw new Error("The shared default-browser poll has no idle callback");
      }
      poll();
    },
  };
}

function getPromoStates({ vpn, setDefault }) {
  return {
    vpn: BrowserTestUtils.isVisible(vpn),
    setDefault: BrowserTestUtils.isVisible(setDefault),
  };
}

async function settlePromos(win, settingGroup, promos, want) {
  for (let i = 0; i < 60; i++) {
    await settingGroup.updateComplete;
    await new Promise(resolve => win.requestAnimationFrame(resolve));
    let states = getPromoStates(promos);
    if (states.vpn == want.vpn && states.setDefault == want.setDefault) {
      break;
    }
  }
  return getPromoStates(promos);
}

// Test that a default-browser change made outside Settings updates the VPN
// promos while the Privacy pane stays selected.
add_task(async function test_promos_follow_external_default_browser_change() {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    siteInclusions: true,
    bandwidth: true,
    entitlementCache: '{"subscribed": false}',
  });

  let tab;
  try {
    tab = await openPrefsTab("privacy");
    let win = tab.linkedBrowser.contentWindow;
    let settingGroup = testSettingsGroupVisible(tab.linkedBrowser);
    let promos = {
      vpn: settingGroup.querySelector("#ipProtectionMozVPNPromo"),
      setDefault: settingGroup.querySelector("#ipProtectionSetToDefaultPromo"),
    };
    ok(promos.vpn, "Mozilla VPN promo is rendered in the DOM");
    ok(promos.setDefault, "Set to Default promo is rendered in the DOM");
    is(win.location.hash, "#privacy", "Privacy is the selected pane");
    is(win.document.visibilityState, "visible", "Settings document is visible");

    let isDefault = false;
    sandbox.stub(win, "getShellService").returns({
      isDefaultBrowser: () => isDefault,
    });
    let poll = interceptDefaultBrowserPoll(win, sandbox);
    win.Preferences.getSetting("ipProtectionIsDefaultBrowser").onChange();

    await TestUtils.waitForCondition(
      () => poll.armed,
      "The shared default-browser poll never fired within its 10 second maximum interval",
      100,
      200
    );
    poll.tick();
    let before = await settlePromos(win, settingGroup, promos, {
      vpn: false,
      setDefault: true,
    });
    is(
      before.setDefault,
      true,
      "Set to Default promo is shown while Firefox is not the default browser"
    );
    is(
      before.vpn,
      false,
      "Mozilla VPN promo is hidden while Firefox is not the default browser"
    );

    isDefault = true;
    poll.tick();

    let after = await settlePromos(win, settingGroup, promos, {
      vpn: true,
      setDefault: false,
    });
    is(win.location.hash, "#privacy", "Privacy is still the selected pane");
    is(
      after.setDefault,
      false,
      "Set to Default promo is hidden once Firefox becomes the default browser on #privacy; a visible promo is stale"
    );
    is(
      after.vpn,
      true,
      "Mozilla VPN promo is shown once Firefox becomes the default browser on #privacy; a hidden promo is stale"
    );
  } finally {
    sandbox.restore();
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    await SpecialPowers.popPrefEnv();
  }
});

async function withVpnPrivacyPane(fn) {
  let sandbox = sinon.createSandbox();
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    siteInclusions: true,
    bandwidth: true,
    entitlementCache: '{"subscribed": false}',
  });
  try {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);
        stubDefaultBrowser(browser, sandbox, false);
        await fn(browser.contentWindow, settingGroup);
      }
    );
  } finally {
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
}

async function assertSetDefaultPromo(win, settingGroup, expectVisible) {
  let promo = settingGroup.querySelector("#ipProtectionSetToDefaultPromo");
  ok(promo, "Set to default promo is rendered in the DOM");
  await settingGroup.updateComplete;
  await promo.updateComplete;
  await new Promise(resolve => win.requestAnimationFrame(resolve));
  await promo.updateComplete;
  is(
    BrowserTestUtils.isVisible(promo),
    expectVisible,
    `Set to default promo is ${expectVisible ? "shown" : "hidden"}`
  );
}

// Test that the set to default promo is shown when no policy is set.
add_task(async function test_set_default_promo_visible_without_policy() {
  ok(
    Services.policies.isAllowed("setDefaultBrowser"),
    "setDefaultBrowser is allowed with no policy"
  );
  await withVpnPrivacyPane(async (win, settingGroup) => {
    await assertSetDefaultPromo(win, settingGroup, true);
  });
});

// Test that DefaultBrowserSettingEnabled:false hides the set to default promo.
add_task(async function test_set_default_promo_hidden_by_policy() {
  PoliciesPrefTracker.start();
  try {
    await EnterprisePolicyTesting.setupPolicyEngineWithJson({
      policies: { DefaultBrowserSettingEnabled: false },
    });
    ok(
      !Services.policies.isAllowed("setDefaultBrowser"),
      "The policy disallows setDefaultBrowser"
    );
    ok(
      Services.prefs.prefIsLocked("browser.shell.checkDefaultBrowser"),
      "The policy locked browser.shell.checkDefaultBrowser"
    );
    await withVpnPrivacyPane(async (win, settingGroup) => {
      await assertSetDefaultPromo(win, settingGroup, false);
    });
  } finally {
    await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
    PoliciesPrefTracker.stop();
  }
  ok(
    Services.policies.isAllowed("setDefaultBrowser"),
    "setDefaultBrowser is allowed again after policy cleanup"
  );
});
