/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Test that defaultEngine property can be set and yields the proper events and
 * behavior (search results). Also tests the correct telemetry is set in Glean.
 */

"use strict";

let engine1;
let engine2;

const CONFIG = [
  {
    identifier: "appDefault",
    base: {
      name: "Application Default",
      urls: {
        search: {
          base: "https://www.google.com/search",
          searchTermParamName: "q1",
        },
      },
    },
  },
  {
    identifier: "alternateEngine",
    base: {
      name: "Alternate Engine",
      urls: {
        search: {
          base: "https://duckduckgo.com/search",
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
];

add_setup(async () => {
  do_get_profile();
  Services.fog.initializeFOG();

  useHttpServer();

  SearchTestUtils.setRemoteSettingsConfig(CONFIG);
  await SearchTestUtils.initXPCShellAddonManager();
  await Services.search.init();

  engine1 = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/generic1.xml`,
  });
  engine2 = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/generic2.xml`,
  });
});

function promiseDefaultNotification() {
  return SearchTestUtils.promiseSearchNotification(
    SearchUtils.MODIFIED_TYPE.DEFAULT,
    SearchUtils.TOPIC_ENGINE_MODIFIED
  );
}

add_task(async function test_appDefaultEngine() {
  // As we have started up, we should have recorded the default engine telemetry.
  await assertGleanDefaultEngine({
    normal: {
      providerId: "appDefault",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "appDefault",
      displayName: "Application Default",
      loadPath: "[app]appDefault",
      submissionUrl: "https://www.google.com/search?q1=",
    },
  });
});

add_task(async function test_alternateAppDefaultEngine_with_partnerCode() {
  let promise = promiseDefaultNotification();
  let alternateEngine =
    Services.search.getEngineById("alternateEngine").wrappedJSObject;
  Services.search.defaultEngine = alternateEngine;

  Assert.equal((await promise).wrappedJSObject, alternateEngine);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, alternateEngine);

  await assertGleanDefaultEngine({
    normal: {
      providerId: "alternateEngine",
      partnerCode: "foo",
      overriddenByThirdParty: false,
      engineId: "alternateEngine-123",
      displayName: "Alternate Engine",
      loadPath: "[app]alternateEngine",
      submissionUrl: "https://duckduckgo.com/search?pc=foo&q2=",
    },
  });
});

add_task(async function test_alternateAppDefaultEngine_with_override() {
  // Reset the engine back to the default engine.
  let promise = promiseDefaultNotification();
  Services.search.defaultEngine = Services.search.getEngineById("appDefault");
  await promise;

  // Set up our override allowlist.
  const settings = await RemoteSettings(SearchUtils.SETTINGS_ALLOWLIST_KEY);
  sinon.stub(settings, "get").returns([
    {
      thirdPartyId: "test@thirdparty.example.com",
      overridesAppIdv2: "alternateEngine",
      urls: [
        {
          search_url:
            "https://duckduckgo.com/search?q={searchTerms}&foo=myparams",
        },
      ],
    },
  ]);

  let alternateEngine =
    Services.search.getEngineById("alternateEngine").wrappedJSObject;

  promise = promiseDefaultNotification();
  let ext = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "test@thirdparty.example.com",
        },
      },
      chrome_settings_overrides: {
        search_provider: {
          is_default: true,
          name: "Alternate Engine",
          keyword: "MozSearch",
          search_url:
            "https://duckduckgo.com/search?q={searchTerms}&foo=myparams",
        },
      },
    },
    useAddonManager: "permanent",
  });

  await ext.startup();
  registerCleanupFunction(async () => {
    await ext.unload();
  });

  await AddonTestUtils.waitForSearchProviderStartup(ext);
  Assert.equal((await promise).wrappedJSObject, alternateEngine);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, alternateEngine);
  Assert.equal(
    alternateEngine.overriddenById,
    "test@thirdparty.example.com",
    "Should have correctly overridden the engine"
  );

  await assertGleanDefaultEngine({
    normal: {
      providerId: "alternateEngine",
      // No partner code should be given.
      partnerCode: "",
      // This is overridden.
      overriddenByThirdParty: true,
      engineId: "alternateEngine-123-addon",
      displayName: "Alternate Engine",
      loadPath: "[app]alternateEngine",
      submissionUrl: "https://duckduckgo.com/search?q=&foo=myparams",
    },
  });
});

add_task(async function test_thirdPartyDefaultEngine() {
  let promise = promiseDefaultNotification();
  Services.search.defaultEngine = engine1;
  Assert.equal((await promise).wrappedJSObject, engine1);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, engine1);

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-Test search engine",
      displayName: "Test search engine",
      loadPath: "[http]localhost/test-search-engine.xml",
      submissionUrl: "https://www.google.com/search?q=",
    },
  });

  promise = promiseDefaultNotification();
  Services.search.defaultEngine = engine2;
  Assert.equal((await promise).wrappedJSObject, engine2);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, engine2);

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-A second test engine",
      displayName: "A second test engine",
      loadPath: "[http]localhost/a-second-test-engine.xml",
      submissionUrl: "https://duckduckgo.com/?q=",
    },
  });

  promise = promiseDefaultNotification();
  Services.search.defaultEngine = engine1;
  Assert.equal((await promise).wrappedJSObject, engine1);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, engine1);

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-Test search engine",
      displayName: "Test search engine",
      loadPath: "[http]localhost/test-search-engine.xml",
      submissionUrl: "https://www.google.com/search?q=",
    },
  });
});

add_task(async function test_telemetry_empty_submission_url() {
  await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/opensearch/simple.xml`,
    setAsDefaultPrivate: true,
  });

  await assertGleanDefaultEngine({
    normal: {
      providerId: "other",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "other-simple",
      displayName: "simple",
      loadPath: "[http]localhost/simple.xml",
      submissionUrl: "blank:",
    },
    private: {
      providerId: "",
      partnerCode: "",
      overriddenByThirdParty: false,
      engineId: "",
      displayName: "",
      loadPath: "",
      submissionUrl: "blank:",
    },
  });
});

add_task(async function test_switch_with_invalid_overriddenBy() {
  engine1.wrappedJSObject.setAttr("overriddenBy", "random@id");

  consoleAllowList.push(
    "Test search engine had overriddenBy set, but no _overriddenData"
  );

  let promise = promiseDefaultNotification();
  Services.search.defaultEngine = engine2;
  Assert.equal((await promise).wrappedJSObject, engine2);
  Assert.equal(Services.search.defaultEngine.wrappedJSObject, engine2);
});
