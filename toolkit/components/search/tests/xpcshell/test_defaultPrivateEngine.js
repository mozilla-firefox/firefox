/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Test that defaultEngine property can be set and yields the proper events and\
 * behavior (search results)
 */

"use strict";

const CONFIG = [
  {
    identifier: "appDefault",
    base: {
      name: "Application Default",
      urls: {
        search: { base: "https://example.org", searchTermParamName: "q1" },
      },
    },
  },
  {
    identifier: "appDefaultPrivate",
    base: {
      name: "Application Default Private",
      urls: {
        search: {
          base: "https://example.org",
          params: [{ name: "pc", value: "{partnerCode}" }],
          searchTermParamName: "q2",
        },
      },
    },
    variants: [
      {
        environment: {
          allLocalesAndRegions: true,
        },
        telemetrySuffix: "123",
        partnerCode: "foo",
      },
    ],
  },
  {
    identifier: "otherEngine1",
    base: {
      name: "Other Engine 1",
      urls: {
        search: {
          base: "https://example.org/engine1/",
          searchTermParamName: "q",
        },
      },
    },
  },
  {
    identifier: "otherEngine2",
    base: {
      name: "Other Engine 2",
      urls: {
        search: {
          base: "https://example.org/engine2/",
          searchTermParamName: "q",
        },
      },
    },
  },
  { globalDefault: "appDefault", globalDefaultPrivate: "appDefaultPrivate" },
];

const CONFIG_NO_PRIVATE = [
  { identifier: "appDefault" },
  { identifier: "other" },
  { globalDefault: "appDefault" },
];

let engine1;
let engine2;
let appDefault;
let appPrivateDefault;

add_setup(async () => {
  do_get_profile();
  Services.fog.initializeFOG();

  SearchTestUtils.setRemoteSettingsConfig(CONFIG);

  Services.prefs.setCharPref(SearchUtils.BROWSER_SEARCH_PREF + "region", "US");
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );

  useHttpServer();

  await Services.search.init();

  appDefault = Services.search.getEngineById("appDefault");
  appPrivateDefault = Services.search.getEngineById("appDefaultPrivate");
  engine1 = Services.search.getEngineById("otherEngine1");
  engine2 = Services.search.getEngineById("otherEngine2");
});

add_task(async function test_defaultPrivateEngine() {
  Assert.equal(
    Services.search.defaultPrivateEngine.identifier,
    appPrivateDefault.identifier,
    "Should have the app private default as the default private engine"
  );
  Assert.equal(
    Services.search.defaultEngine.identifier,
    appDefault.identifier,
    "Should have the app default as the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "appDefault",
      displayName: "Application Default",
      loadPath: "[app]appDefault",
      submissionUrl: "https://example.org/?q1=",
    },
    private: {
      providerId: "appDefaultPrivate",
      partnerCode: "foo",
      overriddenByThirdParty: false,
      engineId: "appDefaultPrivate-123",
      displayName: "Application Default Private",
      loadPath: "[app]appDefaultPrivate",
      submissionUrl: "https://example.org/?pc=foo&q2=",
    },
  });

  let promise = promiseDefaultNotification("private");
  Services.search.defaultPrivateEngine = engine1;
  Assert.equal(
    await promise,
    engine1,
    "Should have notified setting the private engine to the new one"
  );

  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine1,
    "Should have set the private engine to the new one"
  );
  Assert.equal(
    Services.search.defaultEngine,
    appDefault,
    "Should not have changed the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "appDefault",
      displayName: "Application Default",
      loadPath: "[app]appDefault",
      submissionUrl: "https://example.org/?q1=",
    },
    private: {
      providerId: "otherEngine1",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "otherEngine1",
      displayName: "Other Engine 1",
      loadPath: "[app]otherEngine1",
      submissionUrl: "https://example.org/engine1/?q=",
    },
  });

  promise = promiseDefaultNotification("private");
  await Services.search.setDefaultPrivate(
    engine2,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
  Assert.equal(
    await promise,
    engine2,
    "Should have notified setting the private engine to the new one using async api"
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should have set the private engine to the new one using the async api"
  );

  // We use the names here as for some reason the getDefaultPrivate promise
  // returns something which is an nsISearchEngine but doesn't compare
  // exactly to what engine2 is.
  Assert.equal(
    (await Services.search.getDefaultPrivate()).name,
    engine2.name,
    "Should have got the correct private engine with the async api"
  );
  Assert.equal(
    Services.search.defaultEngine,
    appDefault,
    "Should not have changed the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "appDefault",
      displayName: "Application Default",
      loadPath: "[app]appDefault",
      submissionUrl: "https://example.org/?q1=",
    },
    private: {
      providerId: "otherEngine2",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "otherEngine2",
      displayName: "Other Engine 2",
      loadPath: "[app]otherEngine2",
      submissionUrl: "https://example.org/engine2/?q=",
    },
  });

  promise = promiseDefaultNotification("private");
  await Services.search.setDefaultPrivate(
    engine1,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
  Assert.equal(
    await promise,
    engine1,
    "Should have notified reverting the private engine to the selected one using async api"
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine1,
    "Should have reverted the private engine to the selected one using the async api"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      engineId: "appDefault",
    },
    private: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
  });

  engine1.hidden = true;
  Assert.equal(
    Services.search.defaultPrivateEngine,
    appPrivateDefault,
    "Should reset to the app default private engine when hiding the default"
  );
  Assert.equal(
    Services.search.defaultEngine,
    appDefault,
    "Should not have changed the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      engineId: "appDefault",
    },
    private: {
      providerId: "appDefaultPrivate",
      engineId: "appDefaultPrivate-123",
    },
  });

  engine1.hidden = false;
  Services.search.defaultEngine = engine1;
  Assert.equal(
    Services.search.defaultPrivateEngine,
    appPrivateDefault,
    "Setting the default engine should not affect the private default"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
    private: {
      providerId: "appDefaultPrivate",
      engineId: "appDefaultPrivate-123",
    },
  });

  Services.search.defaultEngine = appDefault;
});

add_task(async function test_telemetry_private_empty_submission_url() {
  await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/simple.xml`,
    setAsDefaultPrivate: true,
    // We don't want it to reset to the default at the test end, as we
    // reset the search service in a later test in this file.
    skipReset: true,
  });

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: appDefault.telemetryId,
    },
    private: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-simple",
      displayName: "simple",
      loadPath: "[http]localhost/simple.xml",
      submissionUrl: "blank:",
    },
  });

  Services.search.defaultEngine = appDefault;
});

add_task(async function test_defaultPrivateEngine_turned_off() {
  Services.search.defaultEngine = appDefault;
  Services.search.defaultPrivateEngine = engine1;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      engineId: "appDefault",
    },
    private: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
  });

  let promise = promiseDefaultNotification("private");
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    false
  );
  Assert.equal(
    await promise,
    appDefault,
    "Should have notified setting the first engine correctly."
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      engineId: "appDefault",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });

  promise = promiseDefaultNotification("normal");
  let privatePromise = promiseDefaultNotification("private");
  Services.search.defaultEngine = engine1;
  Assert.equal(
    await promise,
    engine1,
    "Should have notified setting the first engine correctly."
  );
  Assert.equal(
    await privatePromise,
    engine1,
    "Should have notified setting of the private engine as well."
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine1,
    "Should be set to the first engine correctly"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine1,
    "Should keep the default engine in sync with the pref off"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });

  promise = promiseDefaultNotification("private");
  Services.search.defaultPrivateEngine = engine2;
  Assert.equal(
    await promise,
    engine2,
    "Should have notified setting the second engine correctly."
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should be set to the second engine correctly"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine1,
    "Should not change the normal mode default engine"
  );
  Assert.equal(
    Services.prefs.getBoolPref(
      SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
      false
    ),
    true,
    "Should have set the separate private default pref to true"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
    private: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
  });

  promise = promiseDefaultNotification("private");
  Services.search.defaultPrivateEngine = engine1;
  Assert.equal(
    await promise,
    engine1,
    "Should have notified resetting to the first engine again"
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine1,
    "Should be reset to the first engine again"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine1,
    "Should keep the default engine in sync with the pref off"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
    private: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
  });
});

add_task(async function test_defaultPrivateEngine_ui_turned_off() {
  engine1.hidden = false;
  engine2.hidden = false;
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );

  Services.search.defaultEngine = engine2;
  Services.search.defaultPrivateEngine = engine1;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
  });

  let promise = promiseDefaultNotification("private");
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    false
  );
  Assert.equal(
    await promise,
    engine2,
    "Should have notified for resetting of the private pref."
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });

  promise = promiseDefaultNotification("normal");
  Services.search.defaultPrivateEngine = engine1;
  Assert.equal(
    await promise,
    engine1,
    "Should have notified setting the first engine correctly."
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine1,
    "Should be set to the first engine correctly"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine1",
      engineId: "otherEngine1",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });
});

add_task(async function test_defaultPrivateEngine_same_engine_toggle_pref() {
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );

  // Set the normal and private engines to be the same
  Services.search.defaultEngine = engine2;
  Services.search.defaultPrivateEngine = engine2;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
  });

  // Disable pref
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    false
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should not change the default private engine"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine2,
    "Should not change the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });

  // Re-enable pref
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should not change the default private engine"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine2,
    "Should not change the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
  });
});

add_task(async function test_defaultPrivateEngine_same_engine_toggle_ui_pref() {
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );

  // Set the normal and private engines to be the same
  Services.search.defaultEngine = engine2;
  Services.search.defaultPrivateEngine = engine2;

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
  });

  // Disable UI pref
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    false
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should not change the default private engine"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine2,
    "Should not change the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "",
      engineId: "",
    },
  });

  // Re-enable UI pref
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );
  Assert.equal(
    Services.search.defaultPrivateEngine,
    engine2,
    "Should not change the default private engine"
  );
  Assert.equal(
    Services.search.defaultEngine,
    engine2,
    "Should not change the default engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
    private: {
      providerId: "otherEngine2",
      engineId: "otherEngine2",
    },
  });
});

add_task(async function test_no_private_default_falls_back_to_normal_default() {
  SearchTestUtils.setRemoteSettingsConfig(CONFIG_NO_PRIVATE);
  Services.search.wrappedJSObject.reset();
  await Services.search.init();

  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault.ui.enabled",
    true
  );
  Services.prefs.setBoolPref(
    SearchUtils.BROWSER_SEARCH_PREF + "separatePrivateDefault",
    true
  );
  Services.prefs.setCharPref(SearchUtils.BROWSER_SEARCH_PREF + "region", "US");

  await Services.search.init();

  Assert.ok(Services.search.isInitialized, "search initialized");

  Assert.equal(
    Services.search.appDefaultEngine.name,
    "appDefault",
    "Should have the expected engine as app default"
  );
  Assert.equal(
    Services.search.defaultEngine.name,
    "appDefault",
    "Should have the expected engine as default"
  );
  Assert.equal(
    Services.search.appPrivateDefaultEngine.name,
    "appDefault",
    "Should have the same engine for the app private default"
  );
  Assert.equal(
    Services.search.defaultPrivateEngine.name,
    "appDefault",
    "Should have the same engine for the private default"
  );
});
