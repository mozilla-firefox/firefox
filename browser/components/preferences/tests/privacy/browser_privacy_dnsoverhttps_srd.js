"use strict";

requestLongerTimeout(4);

ChromeUtils.defineESModuleGetters(this, {
  DoHConfigController: "moz-src:///toolkit/components/doh/DoHConfig.sys.mjs",
  DoHController: "moz-src:///toolkit/components/doh/DoHController.sys.mjs",
  DoHTestUtils: "resource://testing-common/DoHTestUtils.sys.mjs",
});

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

const gDNSOverride = Cc[
  "@mozilla.org/network/native-dns-override;1"
].getService(Ci.nsINativeDNSResolverOverride);

const TRR_MODE_PREF = "network.trr.mode";
const TRR_URI_PREF = "network.trr.uri";
const TRR_CUSTOM_URI_PREF = "network.trr.custom_uri";
const FIRST_RESOLVER_VALUE = DoHTestUtils.providers[0].uri;

// See bug 1741554. Override the IP to a local address so that any background
// connection attempts to the DoH endpoint don't try to reach a real server.
gDNSOverride.addIPOverride("mozilla.cloudflare-dns.com", "127.0.0.1");

Services.prefs.setStringPref("network.trr.confirmationNS", "skip");

// Mock parental controls service in order to enable it
let parentalControlsService = {
  parentalControlsEnabled: true,
  QueryInterface: ChromeUtils.generateQI(["nsIParentalControlsService"]),
};
let mockParentalControlsServiceCid = undefined;

async function setMockParentalControlEnabled(aEnabled) {
  parentalControlsService.parentalControlsEnabled = aEnabled;
}

async function resetPrefs() {
  await DoHTestUtils.resetRemoteSettingsConfig();
  await DoHController._uninit();
  Services.prefs.clearUserPref(TRR_MODE_PREF);
  Services.prefs.clearUserPref(TRR_URI_PREF);
  Services.prefs.clearUserPref(TRR_CUSTOM_URI_PREF);
  Services.prefs.getChildList("doh-rollout.").forEach(pref => {
    Services.prefs.clearUserPref(pref);
  });
  Services.fog.testResetFOG();
  await DoHController.init();
}

registerCleanupFunction(async () => {
  await resetPrefs();
  Services.prefs.clearUserPref("network.trr.confirmationNS");
  if (mockParentalControlsServiceCid != undefined) {
    MockRegistrar.unregister(mockParentalControlsServiceCid);
    mockParentalControlsServiceCid = undefined;
    Services.dns.reloadParentalControlEnabled();
  }
});

add_setup(async function setup() {
  mockParentalControlsServiceCid = MockRegistrar.register(
    "@mozilla.org/parental-controls-service;1",
    parentalControlsService
  );
  Services.dns.reloadParentalControlEnabled();

  await SpecialPowers.pushPrefEnv({
    set: [["toolkit.telemetry.testing.overrideProductsCheck", true]],
  });

  await DoHTestUtils.resetRemoteSettingsConfig();

  gDNSOverride.addIPOverride("use-application-dns.net.", "4.1.1.1");

  setMockParentalControlEnabled(false);
});

// Regression test for Bug 2037133: the dohStatusBox in the settings-redesign
// DoH advanced sub-pane was falsely showing the "bad URL" error because a
// missed `name` -> `displayName` rename caused `!name` to always be true.
add_task(async function testStatusBoxRedesignPane() {
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "example-1, example-2",
    rolloutEnabled: true,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });

  async function withStatusBox(fn) {
    await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
      leaveOpen: true,
    });
    let doc = gBrowser.selectedBrowser.contentDocument;
    let statusBox = await TestUtils.waitForCondition(() =>
      doc.getElementById("dohStatusBox")
    );
    await fn(statusBox, doc);
    gBrowser.removeCurrentTab();
  }

  info("Active DoH should show the active status with the provider name");
  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRFIRST);
  Services.prefs.setStringPref(TRR_URI_PREF, FIRST_RESOLVER_VALUE);
  await withStatusBox(async statusBox => {
    let expectedName = DoHConfigController.currentConfig.providerList[0].UIName;
    await TestUtils.waitForCondition(
      () =>
        statusBox.getAttribute("data-l10n-id") ==
          "preferences-doh-status-item-active" &&
        JSON.parse(statusBox.getAttribute("data-l10n-args") || "{}").name ==
          expectedName,
      "waiting for the status box to be marked active with the provider name"
    );
    is(
      statusBox.getAttribute("data-l10n-id"),
      "preferences-doh-status-item-active",
      "Status should be active, not the bad-url error (Bug 2037133)"
    );
    let args = JSON.parse(statusBox.getAttribute("data-l10n-args"));
    is(args.name, expectedName, "Active status carries the provider name");
  });

  info("Parental controls should produce a populated name in the l10n args");
  await setMockParentalControlEnabled(true);
  await withStatusBox(async statusBox => {
    let expectedName = DoHConfigController.currentConfig.providerList[0].UIName;
    await TestUtils.waitForCondition(
      () =>
        statusBox.getAttribute("data-l10n-id") ==
        "preferences-doh-status-item-not-active"
    );
    let args = JSON.parse(statusBox.getAttribute("data-l10n-args"));
    is(
      args.name,
      expectedName,
      "Parental-controls status carries the provider name (Bug 2037133)"
    );
    is(
      args.reason,
      "TRR_PARENTAL_CONTROL",
      "Parental-controls status carries the expected reason"
    );
  });
  await setMockParentalControlEnabled(false);

  await resetPrefs();
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "",
    rolloutEnabled: false,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });
  await SpecialPowers.popPrefEnv();
});

// Regression test for Bug 2043551: the dohFallbackIfCustom checkbox and the
// TRR mode pref had their "fallback" semantics inverted, so picking strict
// mode in the redesigned DoH advanced pane stored TRRFIRST (and vice versa).
add_task(async function testFallbackIfCustomMatchesTRRMode() {
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "example-1, example-2",
    rolloutEnabled: true,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });

  async function withFallbackSetting(fn) {
    await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
      leaveOpen: true,
    });
    let win = gBrowser.selectedBrowser.contentWindow;
    let setting = await TestUtils.waitForCondition(() =>
      win.Preferences.getSetting("dohFallbackIfCustom")
    );
    await fn(setting);
    gBrowser.removeCurrentTab();
  }

  info("TRRFIRST (mode 2) is the with-fallback mode; checkbox should be off");
  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRFIRST);
  await withFallbackSetting(setting => {
    is(
      setting.value,
      false,
      "dohFallbackIfCustom reads false when TRR is in TRRFIRST"
    );
  });

  info(
    "TRRONLY (mode 3) is the strict, no-fallback mode; checkbox should be on"
  );
  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRONLY);
  await withFallbackSetting(setting => {
    is(
      setting.value,
      true,
      "dohFallbackIfCustom reads true when TRR is in TRRONLY"
    );
  });

  info("Checking the fallback checkbox from TRRFIRST should switch to TRRONLY");
  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRFIRST);
  await withFallbackSetting(setting => {
    setting.userChange(true);
    is(
      Services.prefs.getIntPref(TRR_MODE_PREF),
      Ci.nsIDNSService.MODE_TRRONLY,
      "Checking dohFallbackIfCustom moves mode from TRRFIRST to TRRONLY"
    );
  });

  info(
    "Unchecking the fallback checkbox from TRRONLY should switch to TRRFIRST"
  );
  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRONLY);
  await withFallbackSetting(setting => {
    setting.userChange(false);
    is(
      Services.prefs.getIntPref(TRR_MODE_PREF),
      Ci.nsIDNSService.MODE_TRRFIRST,
      "Unchecking dohFallbackIfCustom moves mode from TRRONLY to TRRFIRST"
    );
  });

  Services.prefs.clearUserPref("network.trr_ui.fallback_was_checked");
  await resetPrefs();
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "",
    rolloutEnabled: false,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });
});

// Regression test for Bug 2043714: when network.trr.uri points at a URL that
// isn't in the provider list and the dohProviderSelect setting hasn't already
// flagged itself as custom (so getControlConfig hasn't classified it), the
// resolver dropdown was returning the raw URI - which matches no menu option
// - instead of selecting "custom".
add_task(async function testProviderSelectFallsBackToCustom() {
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "example-1, example-2",
    rolloutEnabled: true,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });

  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRFIRST);
  Services.prefs.setStringPref(TRR_URI_PREF, FIRST_RESOLVER_VALUE);

  await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
    leaveOpen: true,
  });
  let win = gBrowser.selectedBrowser.contentWindow;
  let setting = await TestUtils.waitForCondition(() =>
    win.Preferences.getSetting("dohProviderSelect")
  );

  is(
    setting.value,
    FIRST_RESOLVER_VALUE,
    "Sanity check: a provider URI maps to itself in the dropdown"
  );

  gBrowser.removeCurrentTab();

  // Reopen the dialog with a TRR URI that isn't in the provider list. The
  // dropdown must select "custom" instead of returning the raw URI (which
  // matches no menu option).
  Services.prefs.setStringPref(
    TRR_URI_PREF,
    "https://unknown-provider.example/dns-query"
  );

  await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
    leaveOpen: true,
  });
  win = gBrowser.selectedBrowser.contentWindow;
  setting = await TestUtils.waitForCondition(() =>
    win.Preferences.getSetting("dohProviderSelect")
  );

  is(
    setting.value,
    "custom",
    "Non-provider URI must select 'custom' rather than returning the raw URI"
  );

  gBrowser.removeCurrentTab();
  await resetPrefs();
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "",
    rolloutEnabled: false,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });
});

// Bug 2052678: switching the resolver dropdown away from
// "custom" to a built-in provider used to lose the previously entered custom
// URI.
add_task(async function testCustomProviderTextbox() {
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "example-1, example-2",
    rolloutEnabled: true,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });

  const CUSTOM_URI = "https://custom-provider.example/dns-query";

  Services.prefs.setIntPref(TRR_MODE_PREF, Ci.nsIDNSService.MODE_TRRFIRST);
  Services.prefs.setStringPref(TRR_CUSTOM_URI_PREF, CUSTOM_URI);
  Services.prefs.setStringPref(TRR_URI_PREF, CUSTOM_URI);

  await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
    leaveOpen: true,
  });
  let win = gBrowser.selectedBrowser.contentWindow;

  let providerSelect = await TestUtils.waitForCondition(() =>
    win.Preferences.getSetting("dohProviderSelect")
  );
  let customProvider = await TestUtils.waitForCondition(() =>
    win.Preferences.getSetting("dohCustomProvider")
  );

  is(
    providerSelect.value,
    "custom",
    "URI outside the provider list starts the dropdown on 'custom'"
  );
  is(
    customProvider.value,
    CUSTOM_URI,
    "dohCustomProvider is backed by network.trr.custom_uri"
  );
  ok(
    customProvider.visible,
    "Custom textbox is visible while 'custom' is selected"
  );

  info("Switch dropdown to a built-in provider");
  providerSelect.userChange(FIRST_RESOLVER_VALUE);

  is(
    Services.prefs.getStringPref(TRR_URI_PREF),
    FIRST_RESOLVER_VALUE,
    "Selecting a built-in provider updates network.trr.uri"
  );
  is(
    Services.prefs.getStringPref(TRR_CUSTOM_URI_PREF),
    CUSTOM_URI,
    "network.trr.custom_uri is not clobbered by switching to a built-in"
  );
  ok(
    !customProvider.visible,
    "Custom textbox is hidden while a built-in provider is selected"
  );

  info("Switch back to 'custom'; the last custom URI must be restored");
  providerSelect.userChange("custom");

  is(
    Services.prefs.getStringPref(TRR_URI_PREF),
    CUSTOM_URI,
    "Re-selecting 'custom' restores network.trr.uri from network.trr.custom_uri"
  );
  is(
    customProvider.value,
    CUSTOM_URI,
    "dohCustomProvider still exposes the stored custom URI"
  );
  ok(customProvider.visible, "Custom textbox is visible again");

  gBrowser.removeCurrentTab();

  info("With an empty custom_uri, switching to 'custom' falls back to a space");
  Services.prefs.setStringPref(TRR_URI_PREF, FIRST_RESOLVER_VALUE);
  Services.prefs.clearUserPref(TRR_CUSTOM_URI_PREF);

  await openPreferencesViaOpenPreferencesAPI("dnsOverHttps", {
    leaveOpen: true,
  });
  win = gBrowser.selectedBrowser.contentWindow;
  providerSelect = await TestUtils.waitForCondition(() =>
    win.Preferences.getSetting("dohProviderSelect")
  );

  providerSelect.userChange("custom");
  is(
    Services.prefs.getStringPref(TRR_URI_PREF),
    FIRST_RESOLVER_VALUE,
    "Empty custom_uri when switching to custom keeps the current resolver in the TRR URI pref"
  );
  is(
    Services.prefs.getStringPref(TRR_CUSTOM_URI_PREF),
    FIRST_RESOLVER_VALUE,
    "Empty custom_uri when switching to custom moves the current resolver into the custom TRR URI pref"
  );

  gBrowser.removeCurrentTab();
  await resetPrefs();
  await DoHTestUtils.loadRemoteSettingsConfig({
    providers: "",
    rolloutEnabled: false,
    steeringEnabled: false,
    steeringProviders: "",
    autoDefaultEnabled: false,
    autoDefaultProviders: "",
    id: "global",
  });
});
