"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();

AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "42"
);

// Similar to TestUtils.topicObserved, but returns a deferred promise that
// can be resolved
function topicObservable(topic, checkFn) {
  let deferred = Promise.withResolvers();
  function observer(subject, topic, data) {
    try {
      if (checkFn && !checkFn(subject, data)) {
        return;
      }
      deferred.resolve([subject, data]);
    } catch (ex) {
      deferred.reject(ex);
    }
  }
  deferred.promise.finally(() => {
    Services.obs.removeObserver(observer, topic);
    checkFn = null;
  });
  Services.obs.addObserver(observer, topic);

  return deferred;
}

async function setupRemoteSettings() {
  const settings = await RemoteSettings("hijack-blocklists");
  sinon.stub(settings, "get").returns([
    {
      id: "homepage-urls",
      matches: ["ignore=me"],
      _status: "synced",
    },
  ]);
}

function promisePrefChanged(expectedValue) {
  return TestUtils.waitForPrefChange("browser.startup.homepage", value =>
    value.endsWith(expectedValue)
  );
}

add_task(async function setup() {
  await AddonTestUtils.promiseStartupManager();
  await setupRemoteSettings();
});

add_task(async function test_overrides_update_removal() {
  /* This tests the scenario where the manifest key for homepage and/or
   * search_provider are removed between updates and therefore the
   * settings are expected to revert.  It also tests that an extension
   * can make a builtin extension the default search without user
   * interaction.  */

  const EXTENSION_ID = "test_overrides_update@tests.mozilla.org";
  const HOMEPAGE_URI = "webext-homepage-1.html";

  let extensionInfo = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID,
        },
      },
      chrome_settings_overrides: {
        homepage: HOMEPAGE_URI,
        search_provider: {
          name: "DuckDuckGo",
          search_url: "https://example.com/?q={searchTerms}",
          is_default: true,
        },
      },
    },
  };
  let extension = ExtensionTestUtils.loadExtension(extensionInfo);

  let defaultHomepageURL = HomePage.get();
  let defaultEngineName = (await SearchService.getDefault()).name;
  Assert.notStrictEqual(
    defaultEngineName,
    "DuckDuckGo",
    "Default engine is not DuckDuckGo."
  );

  let prefPromise = promisePrefChanged(HOMEPAGE_URI);

  // When an addon is installed that overrides a config engine
  // that is the default, we do not prompt for default.
  let deferredPrompt = topicObservable(
    "webextension-defaultsearch-prompt",
    subject => {
      if (subject.wrappedJSObject.id == extension.id) {
        ok(false, "default override should not prompt");
      }
    }
  );

  await Promise.race([extension.startup(), deferredPrompt.promise]);
  deferredPrompt.resolve();
  await AddonTestUtils.waitForSearchProviderStartup(extension);
  await prefPromise;

  equal(
    extension.version,
    "1.0",
    "The installed addon has the expected version."
  );
  ok(
    HomePage.get().endsWith(HOMEPAGE_URI),
    "Home page url is overridden by the extension."
  );
  equal(
    (await SearchService.getDefault()).name,
    "DuckDuckGo",
    "Builtin default engine was set default by extension"
  );

  extensionInfo.manifest = {
    version: "2.0",
    browser_specific_settings: {
      gecko: {
        id: EXTENSION_ID,
      },
    },
  };

  prefPromise = promisePrefChanged(defaultHomepageURL);
  await extension.upgrade(extensionInfo);
  await prefPromise;

  equal(
    extension.version,
    "2.0",
    "The updated addon has the expected version."
  );
  equal(
    HomePage.get(),
    defaultHomepageURL,
    "Home page url reverted to the default after update."
  );
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine reverted to the default after update."
  );

  await extension.unload();
});

add_task(async function test_overrides_update_adding() {
  /* This tests the scenario where an addon adds support for
   * a homepage or search service when upgrading. Neither
   * should override existing entries for those when added
   * in an upgrade. Also, a search_provider being added
   * with is_default should not prompt the user or override
   * the current default engine. */

  const EXTENSION_ID = "test_overrides_update@tests.mozilla.org";
  const HOMEPAGE_URI = "webext-homepage-1.html";

  let extensionInfo = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID,
        },
      },
    },
  };
  let extension = ExtensionTestUtils.loadExtension(extensionInfo);

  let defaultHomepageURL = HomePage.get();
  let defaultEngineName = (await SearchService.getDefault()).name;
  Assert.notStrictEqual(
    defaultEngineName,
    "DuckDuckGo",
    "Home page url is not DuckDuckGo."
  );

  await extension.startup();

  equal(
    extension.version,
    "1.0",
    "The installed addon has the expected version."
  );
  equal(
    HomePage.get(),
    defaultHomepageURL,
    "Home page url is the default after startup."
  );
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine is the default after startup."
  );

  extensionInfo.manifest = {
    version: "2.0",
    browser_specific_settings: {
      gecko: {
        id: EXTENSION_ID,
      },
    },
    chrome_settings_overrides: {
      homepage: HOMEPAGE_URI,
      search_provider: {
        name: "DuckDuckGo",
        search_url: "https://example.com/?q={searchTerms}",
        is_default: true,
      },
    },
  };

  let prefPromise = promisePrefChanged(HOMEPAGE_URI);

  let deferredUpgradePrompt = topicObservable(
    "webextension-defaultsearch-prompt",
    subject => {
      if (subject.wrappedJSObject.id == extension.id) {
        ok(false, "should not prompt on update");
      }
    }
  );

  await Promise.race([
    extension.upgrade(extensionInfo),
    deferredUpgradePrompt.promise,
  ]);
  deferredUpgradePrompt.resolve();
  await AddonTestUtils.waitForSearchProviderStartup(extension);
  await prefPromise;

  equal(
    extension.version,
    "2.0",
    "The updated addon has the expected version."
  );
  ok(
    HomePage.get().endsWith(HOMEPAGE_URI),
    "Home page url is overridden by the extension during upgrade."
  );
  // An upgraded extension adding a search engine cannot override
  // the default engine.
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine is still the default after startup."
  );

  await extension.unload();
});

add_task(async function test_overrides_update_homepage_change() {
  /* This tests the scenario where an addon changes
   * a homepage url when upgrading. */

  const EXTENSION_ID = "test_overrides_update@tests.mozilla.org";
  const HOMEPAGE_URI = "webext-homepage-1.html";
  const HOMEPAGE_URI_2 = "webext-homepage-2.html";

  let extensionInfo = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID,
        },
      },
      chrome_settings_overrides: {
        homepage: HOMEPAGE_URI,
      },
    },
  };
  let extension = ExtensionTestUtils.loadExtension(extensionInfo);

  let prefPromise = promisePrefChanged(HOMEPAGE_URI);
  await extension.startup();
  await prefPromise;

  equal(
    extension.version,
    "1.0",
    "The installed addon has the expected version."
  );
  ok(
    HomePage.get().endsWith(HOMEPAGE_URI),
    "Home page url is the extension url after startup."
  );

  extensionInfo.manifest = {
    version: "2.0",
    browser_specific_settings: {
      gecko: {
        id: EXTENSION_ID,
      },
    },
    chrome_settings_overrides: {
      homepage: HOMEPAGE_URI_2,
    },
  };

  prefPromise = promisePrefChanged(HOMEPAGE_URI_2);
  await extension.upgrade(extensionInfo);
  await prefPromise;

  equal(
    extension.version,
    "2.0",
    "The updated addon has the expected version."
  );
  ok(
    HomePage.get().endsWith(HOMEPAGE_URI_2),
    "Home page url is by the extension after upgrade."
  );

  await extension.unload();
});

async function withHandlingDefaultSearchPrompt({ extensionId, respond }, cb) {
  const promptResponseHandled = TestUtils.topicObserved(
    "webextension-defaultsearch-prompt-response"
  );
  const prompted = TestUtils.topicObserved(
    "webextension-defaultsearch-prompt",
    subject => {
      if (subject.wrappedJSObject.id == extensionId) {
        return subject.wrappedJSObject.respond(respond);
      }
    }
  );

  await Promise.all([cb(), prompted, promptResponseHandled]);
}

async function assertUpdateDoNotPrompt(extension, updateExtensionInfo) {
  let deferredUpgradePrompt = topicObservable(
    "webextension-defaultsearch-prompt",
    subject => {
      if (subject.wrappedJSObject.id == extension.id) {
        ok(false, "should not prompt on update");
      }
    }
  );

  await Promise.race([
    extension.upgrade(updateExtensionInfo),
    deferredUpgradePrompt.promise,
  ]);
  deferredUpgradePrompt.resolve();

  await AddonTestUtils.waitForSearchProviderStartup(extension);

  equal(
    extension.version,
    updateExtensionInfo.manifest.version,
    "The updated addon has the expected version."
  );
}

add_task(async function test_default_search_prompts() {
  /* This tests the scenario where an addon did not gain
   * default search during install, and later upgrades.
   * The addon should not gain default in updates.
   * If the addon is disabled, it should prompt again when
   * enabled.
   */

  const EXTENSION_ID = "test_default_update@tests.mozilla.org";

  let extensionInfo = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID,
        },
      },
      chrome_settings_overrides: {
        search_provider: {
          name: "Example",
          search_url: "https://example.com/?q={searchTerms}",
          is_default: true,
        },
      },
    },
  };

  let extension = ExtensionTestUtils.loadExtension(extensionInfo);

  let defaultEngineName = (await SearchService.getDefault()).name;
  Assert.notStrictEqual(defaultEngineName, "Example", "Search is not Example.");

  // Mock a response from the default search prompt where we
  // say no to setting this as the default when installing.
  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID, respond: false },
    () => extension.startup()
  );

  equal(
    extension.version,
    "1.0",
    "The installed addon has the expected version."
  );
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine is the default after startup."
  );

  info(
    "Verify that updating the extension does not prompt and does not take over the default engine"
  );

  extensionInfo.manifest.version = "2.0";
  await assertUpdateDoNotPrompt(extension, extensionInfo);
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine is still the default after update."
  );

  info("Verify that disable/enable the extension does prompt the user");

  let addon = await AddonManager.getAddonByID(EXTENSION_ID);

  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID, respond: false },
    async () => {
      await addon.disable();
      await addon.enable();
    }
  );

  // we still said no.
  equal(
    (await SearchService.getDefault()).name,
    defaultEngineName,
    "Default engine is the default after being disabling/enabling."
  );

  await extension.unload();
});

async function test_default_search_on_updating_addons_installed_before_bug1757760({
  builtinAsInitialDefault,
}) {
  /* This tests covers a scenario similar to the previous test but with an extension-settings.json file
     content like the one that would be available in the profile if the add-on was installed on firefox
     versions that didn't include the changes from Bug 1757760 (See Bug 1767550).
  */

  const EXTENSION_ID = `test_old_addon@tests.mozilla.org`;
  const EXTENSION_ID2 = `test_old_addon2@tests.mozilla.org`;

  const extensionInfo = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.1",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID,
        },
      },
      chrome_settings_overrides: {
        search_provider: {
          name: "Test SearchEngine",
          search_url: "https://example.com/?q={searchTerms}",
          is_default: true,
        },
      },
    },
  };

  const extensionInfo2 = {
    useAddonManager: "permanent",
    manifest: {
      version: "1.2",
      browser_specific_settings: {
        gecko: {
          id: EXTENSION_ID2,
        },
      },
      chrome_settings_overrides: {
        search_provider: {
          name: "Test SearchEngine2",
          search_url: "https://example.com/?q={searchTerms}",
          is_default: true,
        },
      },
    },
  };

  const { ExtensionSettingsStore } = ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs"
  );

  async function assertExtensionSettingsStore(
    extensionInfo,
    expectedLevelOfControl
  ) {
    const { id } = extensionInfo.manifest.browser_specific_settings.gecko;
    info(`Asserting ExtensionSettingsStore for ${id}`);
    const item = ExtensionSettingsStore.getSetting(
      "default_search",
      "defaultSearch",
      id
    );
    equal(
      item.value,
      extensionInfo.manifest.chrome_settings_overrides.search_provider.name,
      "Got the expected item returned by ExtensionSettingsStore.getSetting"
    );
    const control = await ExtensionSettingsStore.getLevelOfControl(
      id,
      "default_search",
      "defaultSearch"
    );
    equal(
      control,
      expectedLevelOfControl,
      `Got expected levelOfControl for ${id}`
    );
  }

  info("Install test extensions without opt-in to the related search engines");

  let extension = ExtensionTestUtils.loadExtension(extensionInfo);
  let extension2 = ExtensionTestUtils.loadExtension(extensionInfo2);

  // Mock a response from the default search prompt where we
  // say no to setting this as the default when installing.
  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID, respond: false },
    () => extension.startup()
  );

  equal(
    extension.version,
    "1.1",
    "first installed addon has the expected version."
  );

  // Mock a response from the default search prompt where we
  // say no to setting this as the default when installing.
  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID2, respond: false },
    () => extension2.startup()
  );

  equal(
    extension2.version,
    "1.2",
    "second installed addon has the expected version."
  );

  info("Setup preconditions (set the initial default search engine)");

  // Sanity check to be sure the initial engine expected as precondition
  // for the scenario covered by the current test case.
  let initialEngine;
  if (builtinAsInitialDefault) {
    initialEngine = SearchService.appDefaultEngine;
  } else {
    initialEngine = SearchService.getEngineByName(
      extensionInfo.manifest.chrome_settings_overrides.search_provider.name
    );
  }
  await SearchService.setDefault(
    initialEngine,
    SearchService.CHANGE_REASON.UNKNOWN
  );

  let defaultEngineName = (await SearchService.getDefault()).name;
  Assert.equal(
    defaultEngineName,
    initialEngine.name,
    `initial default search engine expected to be ${
      builtinAsInitialDefault ? "app-provided" : EXTENSION_ID
    }`
  );
  Assert.notEqual(
    defaultEngineName,
    extensionInfo2.manifest.chrome_settings_overrides.search_provider.name,
    "initial default search engine name should not be the same as the second extension search_provider"
  );

  equal(
    (await SearchService.getDefault()).name,
    initialEngine.name,
    `Default engine should still be set to the ${
      builtinAsInitialDefault ? "app-provided" : EXTENSION_ID
    }.`
  );

  // Mock an update from settings stored as in an older Firefox version where Bug 1757760 was not landed yet.
  info(
    "Setup preconditions (inject mock extension-settings.json data and assert on the expected setting and levelOfControl)"
  );

  let addon = await AddonManager.getAddonByID(EXTENSION_ID);
  let addon2 = await AddonManager.getAddonByID(EXTENSION_ID2);

  const extensionSettingsData = {
    version: 2,
    url_overrides: {},
    prefs: {},
    homepageNotification: {},
    tabHideNotification: {},
    default_search: {
      defaultSearch: {
        initialValue: SearchService.appDefaultEngine.name,
        precedenceList: [
          {
            id: EXTENSION_ID2,
            // The install dates are used in ExtensionSettingsStore.getLevelOfControl
            // and to recreate the expected preconditions the last extension installed
            // should have a installDate timestamp > then the first one.
            installDate: addon2.installDate.getTime() + 1000,
            value:
              extensionInfo2.manifest.chrome_settings_overrides.search_provider
                .name,
            // When an addon with a default search engine override is installed in Firefox versions
            // without the changes landed from Bug 1757760, `enabled` will be set to true in all cases
            // (Prompt never answered, or when No or Yes is selected by the user).
            enabled: true,
          },
          {
            id: EXTENSION_ID,
            installDate: addon.installDate.getTime(),
            value:
              extensionInfo.manifest.chrome_settings_overrides.search_provider
                .name,
            enabled: true,
          },
        ],
      },
    },
    newTabNotification: {},
    commands: {},
  };

  const file = Services.dirsvc.get("ProfD", Ci.nsIFile);
  file.append("extension-settings.json");

  // Write the settings store's pending changes now, as otherwise its deferred
  // save could land after the mock data and overwrite it.
  await ExtensionSettingsStore._reloadFile();

  info(`writing mock settings data into ${file.path}`);
  await IOUtils.writeJSON(file.path, extensionSettingsData);
  await ExtensionSettingsStore._reloadFile(false);

  equal(
    (await SearchService.getDefault()).name,
    initialEngine.name,
    "Default engine is still set to the initial one."
  );

  // The following assertions verify that the migration applied from ExtensionSettingsStore
  // fixed the inconsistent state and kept the search engine unchanged.
  //
  // - With the fixed settings we expect both to be resolved to "controllable_by_this_extension".
  // - Without the fix applied during the migration the levelOfControl resolved would be:
  //   - for the last installed: "controlled_by_this_extension"
  //   - for the first installed: "controlled_by_other_extensions"
  await assertExtensionSettingsStore(
    extensionInfo2,
    "controlled_by_this_extension"
  );
  await assertExtensionSettingsStore(
    extensionInfo,
    "controlled_by_other_extensions"
  );

  info(
    "Verify that updating the extension does not prompt and does not take over the default engine"
  );

  extensionInfo2.manifest.version = "2.2";
  await assertUpdateDoNotPrompt(extension2, extensionInfo2);

  extensionInfo.manifest.version = "2.1";
  await assertUpdateDoNotPrompt(extension, extensionInfo);

  equal(
    (await SearchService.getDefault()).name,
    initialEngine.name,
    "Default engine is still the same after updating both the test extensions."
  );

  // After both the extensions have been updated and their inconsistent state
  // updated internally, both extensions should have levelOfControl "controllable_*".
  await assertExtensionSettingsStore(
    extensionInfo2,
    "controllable_by_this_extension"
  );
  await assertExtensionSettingsStore(
    extensionInfo,
    // We expect levelOfControl to be controlled_by_this_extension if the test case
    // is expecting the third party extension to stay set as default.
    builtinAsInitialDefault
      ? "controllable_by_this_extension"
      : "controlled_by_this_extension"
  );

  info("Verify that disable/enable the extension does prompt the user");

  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID2, respond: false },
    async () => {
      await addon2.disable();
      await addon2.enable();
    }
  );

  // we said no.
  equal(
    (await SearchService.getDefault()).name,
    initialEngine.name,
    `Default engine should still be the same after disabling/enabling ${EXTENSION_ID2}.`
  );

  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID, respond: false },
    async () => {
      await addon.disable();
      await addon.enable();
    }
  );

  // we said no.
  equal(
    (await SearchService.getDefault()).name,
    SearchService.appDefaultEngine.name,
    `Default engine should be set to the app default after disabling/enabling ${EXTENSION_ID}.`
  );

  await withHandlingDefaultSearchPrompt(
    { extensionId: EXTENSION_ID, respond: true },
    async () => {
      await addon.disable();
      await addon.enable();
    }
  );

  // we responded yes.
  equal(
    (await SearchService.getDefault()).name,
    extensionInfo.manifest.chrome_settings_overrides.search_provider.name,
    "Default engine should be set to the one opted-in from the last prompt."
  );

  await extension.unload();
  await extension2.unload();
}

add_task(function test_builtin_default_search_after_updating_old_addons() {
  return test_default_search_on_updating_addons_installed_before_bug1757760({
    builtinAsInitialDefault: true,
  });
});

add_task(function test_third_party_default_search_after_updating_old_addons() {
  return test_default_search_on_updating_addons_installed_before_bug1757760({
    builtinAsInitialDefault: false,
  });
});
