ChromeUtils.defineESModuleGetters(this, {
  AboutNewTabResourceMapping:
    "resource:///modules/AboutNewTabResourceMapping.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonTestUtils: "resource://testing-common/AddonTestUtils.sys.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ASRouterTargeting: "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
  AttributionCode:
    "moz-src:///browser/components/attribution/AttributionCode.sys.mjs",
  BrowserInitState: "resource:///modules/BrowserGlue.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  BuiltInThemes: "resource:///modules/BuiltInThemes.sys.mjs",
  CFRMessageProvider: "resource:///modules/asrouter/CFRMessageProvider.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  InfoBar: "resource:///modules/asrouter/InfoBar.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  QueryCache: "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  SessionStartup:
    "moz-src:///browser/components/sessionstore/SessionStartup.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  Spotlight: "resource:///modules/asrouter/Spotlight.sys.mjs",
  TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
  TargetingContext: "resource://messaging-system/targeting/Targeting.sys.mjs",
  TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsPin: "resource:///modules/taskbartabs/TaskbarTabsPin.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
  TelemetrySession: "resource://gre/modules/TelemetrySession.sys.mjs",
});

const { DefaultBrowserCheck } = ChromeUtils.importESModule(
  "moz-src:///browser/components/DefaultBrowserCheck.sys.mjs"
);

const FirefoxViewTestUtils = ChromeUtils.importESModule(
  "resource://testing-common/FirefoxViewTestUtils.sys.mjs"
);
FirefoxViewTestUtils.init(this);

const testFeatureCallout = {
  id: "TEST_MESSAGE",
  template: "feature_callout",
  content: {
    id: "TEST_MESSAGE",
    template: "multistage",
    backdrop: "transparent",
    transitions: false,
    screens: [
      {
        id: "TEST_MESSAGE_1",
        anchors: [
          { selector: "#PanelUI-menu-button", arrow_position: "top-end" },
        ],
        content: {
          position: "callout",
          title: {
            raw: "Test title",
          },
          subtitle: {
            raw: "Test subtitle",
          },
          primary_button: {
            label: {
              raw: "Done",
            },
            action: {
              navigate: true,
            },
          },
        },
      },
    ],
  },
  priority: 1,
  targeting: "true",
  trigger: { id: "defaultBrowserCheck" },
};

const testCrashDumpFiles = [
  {
    path: "/path/to/crash1.dmp",
    id: "crash1",
    date: Date.now() - 30 * 24 * 60 * 60 * 1000, // 30 days ago
  },
  {
    path: "/path/to/crash2.dmp",
    id: "crash2",
    date: Date.now() - 30 * 24 * 60 * 60 * 1000, // 30 days ago
  },
  {
    path: "/path/to/crash3.dmp",
    id: "crash3",
    date: new Date(new Date().getTime() - 10 * 24 * 60 * 60 * 1000), // 10 days before current date
  },
];

function sendFormAutofillMessage(name, data) {
  let actor =
    gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
      "FormAutofill"
    );
  return actor.receiveMessage({ name, data });
}

async function removeAutofillRecords() {
  let addresses = (
    await sendFormAutofillMessage("FormAutofill:GetRecords", {
      collectionName: "addresses",
    })
  ).records;
  if (addresses.length) {
    let observePromise = TestUtils.topicObserved(
      "formautofill-storage-changed"
    );
    await sendFormAutofillMessage("FormAutofill:RemoveAddresses", {
      guids: addresses.map(address => address.guid),
    });
    await observePromise;
  }
  let creditCards = (
    await sendFormAutofillMessage("FormAutofill:GetRecords", {
      collectionName: "creditCards",
    })
  ).records;
  if (creditCards.length) {
    let observePromise = TestUtils.topicObserved(
      "formautofill-storage-changed"
    );
    await sendFormAutofillMessage("FormAutofill:RemoveCreditCards", {
      guids: creditCards.map(cc => cc.guid),
    });
    await observePromise;
  }
}

add_task(async function setup_pref_env() {
  // Let the harness know these prefs are test-managed so it won't warn about
  // changes
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.region", ""],
      ["distribution.iniFile.exists.value", false],
      ["distribution.iniFile.exists.appversion", ""],
      ["extensions.webextensions.uuids", "{}"],
      ["browser.shell.mostRecentDefaultPromptSeen", 0],
    ],
  });
});

// ASRouterTargeting.findMatchingMessage
add_task(async function find_matching_message() {
  const messages = [
    { id: "foo", targeting: "FOO" },
    { id: "bar", targeting: "!FOO" },
  ];
  const context = { FOO: true };

  const match = await ASRouterTargeting.findMatchingMessage({
    messages,
    context,
  });

  is(match, messages[0], "should match and return the correct message");
});

add_task(async function return_nothing_for_no_matching_message() {
  const messages = [{ id: "bar", targeting: "!FOO" }];
  const context = { FOO: true };

  const match = await ASRouterTargeting.findMatchingMessage({
    messages,
    context,
  });

  ok(!match, "should return nothing since no matching message exists");
});

add_task(async function check_other_error_handling() {
  let called = false;
  function onError() {
    called = true;
  }

  const messages = [{ id: "foo", targeting: "foo" }];
  const context = {
    get foo() {
      throw new Error("test error");
    },
  };
  const match = await ASRouterTargeting.findMatchingMessage({
    messages,
    context,
    onError,
  });

  ok(!match, "should return nothing since no valid matching message exists");

  Assert.ok(called, "Attribute error caught");
});

// ASRouterTargeting.Environment
add_task(async function check_locale() {
  ok(
    Services.locale.appLocaleAsBCP47,
    "Services.locale.appLocaleAsBCP47 exists"
  );
  const message = {
    id: "foo",
    targeting: `locale == "${Services.locale.appLocaleAsBCP47}"`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by locale"
  );
});
add_task(async function check_localeLanguageCode() {
  const currentLanguageCode = Services.locale.appLocaleAsBCP47.substr(0, 2);
  is(
    Services.locale.negotiateLanguages(
      [currentLanguageCode],
      [Services.locale.appLocaleAsBCP47]
    )[0],
    Services.locale.appLocaleAsBCP47,
    "currentLanguageCode should resolve to the current locale (e.g en => en-US)"
  );
  const message = {
    id: "foo",
    targeting: `localeLanguageCode == "${currentLanguageCode}"`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by localeLanguageCode"
  );
});

add_task(async function checkProfileAgeCreated() {
  let profileAccessor = await ProfileAge();
  is(
    await ASRouterTargeting.Environment.profileAgeCreated,
    await profileAccessor.created,
    "should return correct profile age creation date"
  );

  const message = {
    id: "foo",
    targeting: `profileAgeCreated > ${(await profileAccessor.created) - 100}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by profile age created"
  );
});

add_task(async function checkProfileAgeReset() {
  let profileAccessor = await ProfileAge();
  is(
    await ASRouterTargeting.Environment.profileAgeReset,
    await profileAccessor.reset,
    "should return correct profile age reset"
  );

  const message = {
    id: "foo",
    targeting: `profileAgeReset == ${await profileAccessor.reset}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by profile age reset"
  );
});

add_task(async function checkCurrentDate() {
  let message = {
    id: "foo",
    targeting: `currentDate < '${new Date(Date.now() + 5000)}'|date`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select message based on currentDate < timestamp"
  );

  message = {
    id: "foo",
    targeting: `currentDate > '${new Date(Date.now() - 5000)}'|date`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select message based on currentDate > timestamp"
  );
});

add_task(async function check_canCreateSelectableProfiles() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  // Reset profiles prefs
  await pushPrefs(
    ["browser.profiles.enabled", false],
    ["browser.profiles.created", false]
  );

  is(
    await ASRouterTargeting.Environment.canCreateSelectableProfiles,
    false,
    "The new profiles feature doesn't support standalone profiles which are used in automation."
  );

  // We have to fake there being a real profile available and enable the profiles feature
  await pushPrefs(
    ["browser.profiles.enabled", true],
    ["browser.profiles.created", false]
  );
  await ProfilesDatastoreService.resetProfileService({ currentProfile: {} });
  await SelectableProfileService.uninit();
  await SelectableProfileService.init();

  is(
    await ASRouterTargeting.Environment.canCreateSelectableProfiles,
    true,
    "should return true if the current profile is valid for use with SelectableProfileService"
  );

  const message = { id: "foo", targeting: "canCreateSelectableProfiles" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by canCreateSelectableProfiles"
  );

  await ProfilesDatastoreService.resetProfileService(null);
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_hasSelectableProfiles() {
  is(
    await ASRouterTargeting.Environment.hasSelectableProfiles,
    false,
    "should return false before the pref is set"
  );

  await pushPrefs(["browser.profiles.created", true]);
  is(
    await ASRouterTargeting.Environment.hasSelectableProfiles,
    true,
    "should return true if the pref is set"
  );

  const message = { id: "foo", targeting: "hasSelectableProfiles" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by hasSelectableProfiles"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_usesFirefoxSync() {
  await pushPrefs(["services.sync.username", "someone@foo.com"]);
  is(
    await ASRouterTargeting.Environment.usesFirefoxSync,
    true,
    "should return true if a fx account is set"
  );

  const message = { id: "foo", targeting: "usesFirefoxSync" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by usesFirefoxSync"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_isFxAEnabled() {
  await pushPrefs(["identity.fxaccounts.enabled", false]);
  is(
    await ASRouterTargeting.Environment.isFxAEnabled,
    false,
    "should return false if fxa is disabled"
  );

  const message = { id: "foo", targeting: "isFxAEnabled" };
  ok(
    !(await ASRouterTargeting.findMatchingMessage({ messages: [message] })),
    "should not select a message if fxa is disabled"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_isFxAEnabled() {
  await pushPrefs(["identity.fxaccounts.enabled", true]);
  is(
    await ASRouterTargeting.Environment.isFxAEnabled,
    true,
    "should return true if fxa is enabled"
  );

  const message = { id: "foo", targeting: "isFxAEnabled" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select the correct message"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_isFxASignedIn_false() {
  await pushPrefs(
    ["identity.fxaccounts.enabled", true],
    ["services.sync.username", ""]
  );
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(async () => sandbox.restore());
  sandbox.stub(FxAccounts.prototype, "getSignedInUser").resolves(null);
  is(
    await ASRouterTargeting.Environment.isFxASignedIn,
    false,
    "user should not appear signed in"
  );

  const message = { id: "foo", targeting: "isFxASignedIn" };
  isnot(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should not select the message since user is not signed in"
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_isFxASignedIn_true() {
  await pushPrefs(
    ["identity.fxaccounts.enabled", true],
    ["services.sync.username", ""]
  );
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(async () => sandbox.restore());
  sandbox.stub(FxAccounts.prototype, "getSignedInUser").resolves({});
  is(
    await ASRouterTargeting.Environment.isFxASignedIn,
    true,
    "user should appear signed in"
  );

  const message = { id: "foo", targeting: "isFxASignedIn" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select the correct message"
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_totalBookmarksCount() {
  // Make sure we remove default bookmarks so they don't interfere
  await clearHistoryAndBookmarks();
  const message = { id: "foo", targeting: "totalBookmarksCount > 0" };

  const results = await ASRouterTargeting.findMatchingMessage({
    messages: [message],
  });
  ok(
    !(results ? JSON.stringify(results) : results),
    "Should not select any message because bookmarks count is not 0"
  );

  const bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    title: "foo",
    url: "https://mozilla1.com/nowNew",
  });

  QueryCache.queries.TotalBookmarksCount.expire();

  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "Should select correct item after bookmarks are added."
  );

  // Cleanup
  await PlacesUtils.bookmarks.remove(bookmark.guid);
});

add_task(async function check_needsUpdate() {
  QueryCache.queries.CheckBrowserNeedsUpdate.setUp(true);

  const message = { id: "foo", targeting: "needsUpdate" };

  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "Should select message because update count > 0"
  );

  QueryCache.queries.CheckBrowserNeedsUpdate.setUp(false);

  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    null,
    "Should not select message because update count == 0"
  );
});

add_task(async function checksearchEngines() {
  const result = await ASRouterTargeting.Environment.searchEngines;
  const appProvidedEngines = await SearchService.getAppProvidedEngines();
  const expectedInstalled = appProvidedEngines
    .map(engine => engine.id)
    .sort()
    .join(",");
  const expectedHasEnteredSearchMode = {};
  for (let engine of appProvidedEngines) {
    expectedHasEnteredSearchMode[engine.id] = engine.hasBeenUsed;
  }

  ok(
    result.installed.length,
    "searchEngines.installed should be a non-empty array"
  );
  is(
    result.installed.sort().join(","),
    expectedInstalled,
    "searchEngines.installed should be an array of visible search engines"
  );
  ok(
    result.current && typeof result.current === "string",
    "searchEngines.current should be a truthy string"
  );
  is(
    result.current,
    (await SearchService.getDefault()).id,
    "searchEngines.current should be the current engine name"
  );
  Assert.deepEqual(
    result.hasEnteredSearchMode,
    expectedHasEnteredSearchMode,
    "searchEngines.hasEnteredSearchmode should map engine ids to hasBeenUsed values"
  );

  const message = {
    id: "foo",
    targeting: `searchEngines[.current == ${
      (await SearchService.getDefault()).id
    }]`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by searchEngines.current"
  );
  const [firstEngine] = appProvidedEngines;

  const message2 = {
    id: "foo",
    targeting: `searchEngines[${firstEngine.id} in .installed]`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message2] }),
    message2,
    "should select correct item by searchEngines.installed"
  );

  const message3 = {
    id: "foo",
    targeting: `searchEngines[.hasEnteredSearchMode.${firstEngine.id} == ${firstEngine.hasBeenUsed}]`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message3] }),
    message3,
    "should select correct item by searchEngines.hasEnteredSearchMode"
  );
});

add_task(async function checkisDefaultBrowser() {
  const expected = ShellService.isDefaultBrowser();
  const result = await ASRouterTargeting.Environment.isDefaultBrowser;
  is(typeof result, "boolean", "isDefaultBrowser should be a boolean value");
  is(
    result,
    expected,
    "isDefaultBrowser should be equal to ShellService.isDefaultBrowser()"
  );
  const message = {
    id: "foo",
    targeting: `isDefaultBrowser == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isDefaultBrowser"
  );
});

add_task(async function checkisDefaultHandler_pdf() {
  const expected = ShellService.isDefaultHandlerFor(".pdf");
  const result = await ASRouterTargeting.Environment.isDefaultHandler.pdf;
  is(
    typeof result,
    "boolean",
    "isDefaultHandler.pdf should be a boolean value"
  );
  is(
    result,
    expected,
    "isDefaultHandler.pdf should equal ShellService.isDefaultHandlerFor('.pdf')"
  );
  const message = {
    id: "pdf_test",
    targeting: `isDefaultHandler.pdf == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isDefaultHandler.pdf"
  );
});

add_task(async function checkisDefaultHandler_html() {
  const expected = ShellService.isDefaultHandlerFor(".html");
  const result = await ASRouterTargeting.Environment.isDefaultHandler.html;
  is(
    typeof result,
    "boolean",
    "isDefaultHandler.html should be a boolean value"
  );
  is(
    result,
    expected,
    "isDefaultHandler.html should equal ShellService.isDefaultHandlerFor('.html')"
  );
  const message = {
    id: "html_test",
    targeting: `isDefaultHandler.html == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isDefaultHandler.html"
  );
});

add_task(async function checkisDefaultHandler_mailto() {
  const expected = ShellService.isDefaultHandlerFor("mailto");
  const result = await ASRouterTargeting.Environment.isDefaultHandler.mailto;
  is(
    typeof result,
    "boolean",
    "isDefaultHandler.mailto should be a boolean value"
  );
  is(
    result,
    expected,
    "isDefaultHandler.mailto should equal ShellService.isDefaultHandlerFor('mailto')"
  );
  const message = {
    id: "mailto_test",
    targeting: `isDefaultHandler.mailto == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isDefaultHandler.mailto"
  );
});

add_task(async function checkMailtoHandlerHost() {
  const result = await ASRouterTargeting.Environment.mailtoHandlerHost;
  ok(
    result === null || typeof result === "string",
    "mailtoHandlerHost should be null or a host string"
  );

  const nullMessage = {
    id: "mailto_host_null_test",
    targeting: "mailtoHandlerHost == null",
  };
  const nullMatch = await ASRouterTargeting.findMatchingMessage({
    messages: [nullMessage],
  });
  ok(
    nullMatch === nullMessage || nullMatch === null,
    "should be able to target on mailtoHandlerHost == null"
  );

  if (typeof result === "string") {
    ok(result.includes("."), `should return a host (got "${result}")`);
    const message = {
      id: "mailto_host_test",
      targeting: `mailtoHandlerHost == "${result}"`,
    };
    is(
      await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
      message,
      "should select correct item by mailtoHandlerHost"
    );
  }
});

add_task(async function checkisPrivateWindow_false() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const expected = PrivateBrowsingUtils.isWindowPrivate(win);
  const result = await ASRouterTargeting.Environment.isPrivateWindow;
  is(typeof result, "boolean", "isPrivateWindow should be a boolean value");
  is(
    result,
    expected,
    "isPrivateWindow should be equal to PrivateBrowsingUtils.isWindowPrivate()"
  );
  const message = {
    id: "foo",
    targeting: `isPrivateWindow == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isPrivateWindow"
  );
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function checkisPrivateWindow_true() {
  // Open a new private window
  const privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  const expected = PrivateBrowsingUtils.isWindowPrivate(privateWin);
  const result = await ASRouterTargeting.Environment.isPrivateWindow;
  is(typeof result, "boolean", "isPrivateWindow should be a boolean value");
  is(
    result,
    expected,
    "isPrivateWindow should be equal to PrivateBrowsingUtils.isWindowPrivate()"
  );
  const message = {
    id: "foo",
    targeting: `isPrivateWindow == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isPrivateWindow"
  );
  await BrowserTestUtils.closeWindow(privateWin);
});

add_task(async function checkdevToolsOpenedCount() {
  await pushPrefs(["devtools.selfxss.count", 5]);
  is(
    ASRouterTargeting.Environment.devToolsOpenedCount,
    5,
    "devToolsOpenedCount should be equal to devtools.selfxss.count pref value"
  );
  const message = { id: "foo", targeting: "devToolsOpenedCount >= 5" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by devToolsOpenedCount"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_platformName() {
  const message = {
    id: "foo",
    targeting: `platformName == "${AppConstants.platform}"`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by platformName"
  );
});

AddonTestUtils.initMochitest(this);

add_task(async function checkAddonsInfo() {
  const FAKE_ID = "testaddon@tests.mozilla.org";
  const FAKE_NAME = "Test Addon";
  const FAKE_VERSION = "0.5.7";

  let { hasInstalledAddons: installedAddons } =
    await ASRouterTargeting.Environment.addonsInfo;

  Assert.strictEqual(
    installedAddons,
    false,
    "should correctly return hasInstalledAddons before"
  );

  const xpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      browser_specific_settings: { gecko: { id: FAKE_ID } },
      name: FAKE_NAME,
      version: FAKE_VERSION,
    },
  });

  await Promise.all([
    AddonTestUtils.promiseWebExtensionStartup(FAKE_ID),
    AddonManager.installTemporaryAddon(xpi),
  ]);

  const { addons } = await AddonManager.getActiveAddons([
    "extension",
    "service",
  ]);

  const {
    addons: asRouterAddons,
    isFullData,
    hasInstalledAddons,
  } = await ASRouterTargeting.Environment.addonsInfo;

  ok(
    addons.every(({ id }) => asRouterAddons[id]),
    "should contain every addon"
  );

  ok(
    Object.getOwnPropertyNames(asRouterAddons).every(id =>
      addons.some(addon => addon.id === id)
    ),
    "should contain no incorrect addons"
  );

  const testAddon = asRouterAddons[FAKE_ID];

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "version") &&
      testAddon.version === FAKE_VERSION,
    "should correctly provide `version` property"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "type") &&
      testAddon.type === "extension",
    "should correctly provide `type` property"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "isSystem") &&
      testAddon.isSystem === false,
    "should correctly provide `isSystem` property"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "isWebExtension") &&
      testAddon.isWebExtension === true,
    "should correctly provide `isWebExtension` property"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "hidden") &&
      testAddon.hidden === false,
    "should correctly provide `hidden` property"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "isBuiltin") &&
      testAddon.isBuiltin === false,
    "should correctly provide `isBuiltin` property"
  );

  // As we installed our test addon the addons database must be initialised, so
  // (in this test environment) we expect to receive "full" data

  ok(isFullData, "should receive full data");

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "name") &&
      testAddon.name === FAKE_NAME,
    "should correctly provide `name` property from full data"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "userDisabled") &&
      testAddon.userDisabled === false,
    "should correctly provide `userDisabled` property from full data"
  );

  ok(
    Object.prototype.hasOwnProperty.call(testAddon, "installDate") &&
      Math.abs(Date.now() - new Date(testAddon.installDate)) < 60 * 1000,
    "should correctly provide `installDate` property from full data"
  );

  ok(hasInstalledAddons, "should correctly return hasInstalledAddons after");

  // Clean up the installed test addon
  let testAddonObj = await AddonManager.getAddonByID(FAKE_ID);
  if (testAddonObj) {
    await testAddonObj.uninstall();
  }
});

add_task(async function checkFrecentSites() {
  const now = Date.now();
  const timeDaysAgo = numDays => now - numDays * 24 * 60 * 60 * 1000;

  const visits = [];
  // Create test data with varying frecency scores: high (frequent visits),
  // medium (less frequent), and low (infrequent).
  for (const [uri, count, visitDate] of [
    ["https://mozilla1.com/", 10, timeDaysAgo(0)],
    ["https://mozilla2.com/", 5, timeDaysAgo(1)],
    ["https://mozilla3.com/", 1, timeDaysAgo(2)],
  ]) {
    [...Array(count).keys()].forEach(() =>
      visits.push({
        uri,
        visitDate: visitDate * 1000, // Places expects microseconds
      })
    );
  }

  await PlacesTestUtils.addVisits(visits);

  let message = {
    id: "foo",
    targeting: "'mozilla3.com' in topFrecentSites|mapToProperty('host')",
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by host in topFrecentSites"
  );

  message = {
    id: "foo",
    targeting: "'non-existent.com' in topFrecentSites|mapToProperty('host')",
  };
  ok(
    !(await ASRouterTargeting.findMatchingMessage({ messages: [message] })),
    "should not select incorrect item by host in topFrecentSites"
  );

  // Frecency threshold for 5 visits to mozilla2.com, 1 day ago.
  let threshold = PlacesUtils.history.pageFrecencyThreshold(1, 5, false);
  message = {
    id: "foo",
    targeting: `'mozilla2.com' in topFrecentSites[.frecency >= ${
      threshold
    }]|mapToProperty('host')`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by frecency"
  );

  let higherThreshold = PlacesUtils.history.pageFrecencyThreshold(1, 6, false);
  Assert.greater(higherThreshold, threshold, "Threshold is higher.");
  message = {
    id: "foo",
    targeting: `'mozilla2.com' in topFrecentSites[.frecency >= ${
      higherThreshold
    }]|mapToProperty('host')`,
  };
  ok(
    !(await ASRouterTargeting.findMatchingMessage({ messages: [message] })),
    "should not select incorrect item when filtering by frecency"
  );

  message = {
    id: "foo",
    targeting: `'mozilla2.com' in topFrecentSites[.lastVisitDate >= ${
      timeDaysAgo(1) - 1
    }]|mapToProperty('host')`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by lastVisitDate"
  );

  message = {
    id: "foo",
    targeting: `'mozilla2.com' in topFrecentSites[.lastVisitDate >= ${
      timeDaysAgo(0) - 1
    }]|mapToProperty('host')`,
  };
  ok(
    !(await ASRouterTargeting.findMatchingMessage({ messages: [message] })),
    "should not select incorrect item when filtering by lastVisitDate"
  );

  message = {
    id: "foo",
    targeting: `(topFrecentSites[.frecency >= 900 && .lastVisitDate >= ${
      timeDaysAgo(1) - 1
    }]|mapToProperty('host') intersect ['mozilla3.com', 'mozilla2.com', 'mozilla1.com'])|length > 0`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by frecency and lastVisitDate with multiple candidate domains"
  );

  // Cleanup
  await clearHistoryAndBookmarks();
});

add_task(async function check_pinned_sites() {
  // Fresh profiles come with an empty set of pinned websites (pref doesn't
  // exist). Search shortcut topsites make this test more complicated because
  // the feature pins a new website on startup. Behaviour can vary when running
  // with --verify so it's more predictable to clear pins entirely.
  Services.prefs.clearUserPref("browser.newtabpage.pinned");
  NewTabUtils.pinnedLinks.resetCache();
  const originalPin = JSON.stringify(NewTabUtils.pinnedLinks.links);
  const sitesToPin = [
    { url: "https://foo.com" },
    { url: "https://bloo.com" },
    { url: "https://floogle.com", searchTopSite: true },
  ];
  sitesToPin.forEach(site =>
    NewTabUtils.pinnedLinks.pin(site, NewTabUtils.pinnedLinks.links.length)
  );

  // Unpinning adds null to the list of pinned sites, which we should test that we handle gracefully for our targeting
  NewTabUtils.pinnedLinks.unpin(sitesToPin[1]);
  ok(
    NewTabUtils.pinnedLinks.links.includes(null),
    "should have set an item in pinned links to null via unpinning for testing"
  );

  let message;

  message = {
    id: "foo",
    targeting: "'https://foo.com' in pinnedSites|mapToProperty('url')",
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by url in pinnedSites"
  );

  message = {
    id: "foo",
    targeting: "'foo.com' in pinnedSites|mapToProperty('host')",
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by host in pinnedSites"
  );

  message = {
    id: "foo",
    targeting:
      "'floogle.com' in pinnedSites[.searchTopSite == true]|mapToProperty('host')",
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by host and searchTopSite in pinnedSites"
  );

  // Cleanup
  sitesToPin.forEach(site => NewTabUtils.pinnedLinks.unpin(site));

  await clearHistoryAndBookmarks();
  Services.prefs.clearUserPref("browser.newtabpage.pinned");
  NewTabUtils.pinnedLinks.resetCache();
  is(
    JSON.stringify(NewTabUtils.pinnedLinks.links),
    originalPin,
    "should restore pinned sites to its original state"
  );
});

add_task(async function check_firefox_version() {
  const message = { id: "foo", targeting: "firefoxVersion > 0" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by firefox version"
  );
});

add_task(async function check_region() {
  Region._setHomeRegion("DE", false);
  const message = { id: "foo", targeting: "region in ['DE']" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by firefox geo"
  );
});

add_task(async function check_browserSettings() {
  is(
    await JSON.stringify(ASRouterTargeting.Environment.browserSettings.update),
    JSON.stringify(TelemetryEnvironment.currentEnvironment.settings.update),
    "should return correct update info"
  );
});

add_task(async function check_sync() {
  is(
    await ASRouterTargeting.Environment.sync.desktopDevices,
    Services.prefs.getIntPref("services.sync.clients.devices.desktop", 0),
    "should return correct desktopDevices info"
  );
  is(
    await ASRouterTargeting.Environment.sync.mobileDevices,
    Services.prefs.getIntPref("services.sync.clients.devices.mobile", 0),
    "should return correct mobileDevices info"
  );
  is(
    await ASRouterTargeting.Environment.sync.totalDevices,
    Services.prefs.getIntPref("services.sync.numClients", 0),
    "should return correct mobileDevices info"
  );
});

add_task(async function check_provider_cohorts() {
  await pushPrefs([
    "browser.newtabpage.activity-stream.asrouter.providers.onboarding",
    JSON.stringify({
      id: "onboarding",
      messages: [],
      enabled: true,
      cohort: "foo",
    }),
  ]);
  await pushPrefs([
    "browser.newtabpage.activity-stream.asrouter.providers.cfr",
    JSON.stringify({ id: "cfr", enabled: true, cohort: "bar" }),
  ]);
  is(
    await ASRouterTargeting.Environment.providerCohorts.onboarding,
    "foo",
    "should have cohort foo for onboarding"
  );
  is(
    await ASRouterTargeting.Environment.providerCohorts.cfr,
    "bar",
    "should have cohort bar for cfr"
  );
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_xpinstall_enabled() {
  // should default to true if pref doesn't exist
  is(await ASRouterTargeting.Environment.xpinstallEnabled, true);
  // flip to false, check targeting reflects that
  await pushPrefs(["xpinstall.enabled", false]);
  is(await ASRouterTargeting.Environment.xpinstallEnabled, false);
  // flip to true, check targeting reflects that
  await pushPrefs(["xpinstall.enabled", true]);
  is(await ASRouterTargeting.Environment.xpinstallEnabled, true);
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_current_tab_installed_as_web_app() {
  // By default, Taskbar Tabs will try and pin this to the taskbar, but we
  // don't want to do that in this test.
  const sandbox = sinon.createSandbox();
  sandbox.stub(TaskbarTabsPin, "pinTaskbarTab");
  sandbox.stub(TaskbarTabsPin, "unpinTaskbarTab");

  const kUri = "https://example.com";

  await BrowserTestUtils.withNewTab(kUri, async () => {
    is(
      await ASRouterTargeting.Environment.currentTabInstalledAsWebApp,
      false,
      "no taskbar tab exists yet"
    );

    const { taskbarTab } = await TaskbarTabs.findOrCreateTaskbarTab(
      Services.io.newURI(kUri),
      0
    );
    is(
      await ASRouterTargeting.Environment.currentTabInstalledAsWebApp,
      true,
      "should be true when a Taskbar Tab exists for this tab"
    );

    await BrowserTestUtils.withNewTab("mochi.test:8888", async () => {
      is(
        await ASRouterTargeting.Environment.currentTabInstalledAsWebApp,
        false,
        "should be false even if a Taskbar Tab exists for a different tab"
      );
    });

    await TaskbarTabs.removeTaskbarTab(taskbarTab.id);
    is(
      await ASRouterTargeting.Environment.currentTabInstalledAsWebApp,
      false,
      "should be false after removing the Taskbar Tab"
    );
  });

  sandbox.restore();
});

add_task(async function check_installed_web_apps_count() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(TaskbarTabsPin, "pinTaskbarTab");
  sandbox.stub(TaskbarTabsPin, "unpinTaskbarTab");

  const kUri1 = "https://example.com";
  const kUri2 = "https://example.org";

  is(
    await ASRouterTargeting.Environment.installedWebAppsCount,
    0,
    "should be 0 with no Taskbar Tabs"
  );

  const { taskbarTab: tab1 } = await TaskbarTabs.findOrCreateTaskbarTab(
    Services.io.newURI(kUri1),
    0
  );
  is(
    await ASRouterTargeting.Environment.installedWebAppsCount,
    1,
    "should be 1 after pinning one site"
  );

  const { taskbarTab: tab2 } = await TaskbarTabs.findOrCreateTaskbarTab(
    Services.io.newURI(kUri2),
    0
  );
  is(
    await ASRouterTargeting.Environment.installedWebAppsCount,
    2,
    "should be 2 after pinning a second site"
  );

  await TaskbarTabs.removeTaskbarTab(tab1.id);
  await TaskbarTabs.removeTaskbarTab(tab2.id);
  is(
    await ASRouterTargeting.Environment.installedWebAppsCount,
    0,
    "should be 0 after removing all Taskbar Tabs"
  );

  sandbox.restore();
});

add_task(async function check_pinned_tabs() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      is(
        await ASRouterTargeting.Environment.hasPinnedTabs,
        false,
        "No pin tabs yet"
      );

      let tab = gBrowser.getTabForBrowser(browser);
      gBrowser.pinTab(tab);

      is(
        await ASRouterTargeting.Environment.hasPinnedTabs,
        true,
        "Should detect pinned tab"
      );

      gBrowser.unpinTab(tab);
    }
  );
});

add_task(async function check_has_active_ai_window() {
  is(
    await ASRouterTargeting.Environment.hasActiveAIWindow,
    false,
    "No active Smart Window without one open"
  );
});

add_task(async function check_has_active_ai_window_true() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();
  win.document.documentElement.setAttribute("ai-window", "");

  is(
    await ASRouterTargeting.Environment.hasActiveAIWindow,
    true,
    "Should detect an active Smart Window while one is open"
  );

  win.document.documentElement.removeAttribute("ai-window");
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_tabsOpenInTopWindow() {
  const baseline = await ASRouterTargeting.Environment.tabsOpenInTopWindow;

  const tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  is(
    await ASRouterTargeting.Environment.tabsOpenInTopWindow,
    baseline + 1,
    "Should count one additional tab"
  );

  const tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  is(
    await ASRouterTargeting.Environment.tabsOpenInTopWindow,
    baseline + 2,
    "Should count two additional tabs"
  );

  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab1);
  is(
    await ASRouterTargeting.Environment.tabsOpenInTopWindow,
    baseline,
    "Should return to baseline after closing tabs"
  );
});

class FakeTabWithNote extends EventTarget {
  /**
   * @param {string} canonicalUrl
   */
  constructor(canonicalUrl) {
    super();
    this.canonicalUrl = canonicalUrl;
  }
}

add_task(async function check_tabNotesCount() {
  const tab1 = new FakeTabWithNote("https://www.example.com/1");
  const tab2 = new FakeTabWithNote("https://www.example.com/2");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.notes.enabled", true]],
  });
  await TabNotes.init();

  Assert.equal(
    await ASRouterTargeting.Environment.tabNotesCount,
    0,
    "No tab notes yet"
  );

  await TabNotes.set(tab1, "Test note 1");
  Assert.equal(
    await ASRouterTargeting.Environment.tabNotesCount,
    1,
    "One tab note"
  );

  await TabNotes.set(tab2, "Test note 2");
  Assert.equal(
    await ASRouterTargeting.Environment.tabNotesCount,
    2,
    "Two tab notes"
  );

  await TabNotes.delete(tab2);
  Assert.equal(
    await ASRouterTargeting.Environment.tabNotesCount,
    1,
    "One tab note again"
  );

  await TabNotes.reset();
  Assert.equal(
    await ASRouterTargeting.Environment.tabNotesCount,
    0,
    "No tab notes again"
  );

  await TabNotes.deinit();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_hasAccessedFxAPanel() {
  is(
    await ASRouterTargeting.Environment.hasAccessedFxAPanel,
    false,
    "Not accessed yet"
  );

  await pushPrefs(["identity.fxaccounts.toolbar.accessed", true]);

  is(
    await ASRouterTargeting.Environment.hasAccessedFxAPanel,
    true,
    "Should detect panel access"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function checkCFRFeaturesUserPref() {
  await pushPrefs([
    "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
    false,
  ]);
  is(
    ASRouterTargeting.Environment.userPrefs.cfrFeatures,
    false,
    "cfrFeature should be false according to pref"
  );
  const message = { id: "foo", targeting: "userPrefs.cfrFeatures == false" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by cfrFeature"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function checkCFRAddonsUserPref() {
  await pushPrefs([
    "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
    false,
  ]);
  is(
    ASRouterTargeting.Environment.userPrefs.cfrAddons,
    false,
    "cfrFeature should be false according to pref"
  );
  const message = { id: "foo", targeting: "userPrefs.cfrAddons == false" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by cfrAddons"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_blockedCountByType() {
  const message = {
    id: "foo",
    targeting:
      "blockedCountByType.cryptominerCount == 0 && blockedCountByType.socialCount == 0",
  };

  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item"
  );
});

add_task(async function checkPatternMatches() {
  const now = Date.now();
  const timeMinutesAgo = numMinutes => now - numMinutes * 60 * 1000;
  const messages = [
    {
      id: "message_with_pattern",
      targeting: "true",
      trigger: { id: "frequentVisits", patterns: ["*://*.github.com/"] },
    },
  ];
  const trigger = {
    id: "frequentVisits",
    context: {
      recentVisits: [
        { timestamp: timeMinutesAgo(33) },
        { timestamp: timeMinutesAgo(17) },
        { timestamp: timeMinutesAgo(1) },
      ],
    },
    param: { host: "github.com", url: "https://gist.github.com" },
  };

  is(
    (await ASRouterTargeting.findMatchingMessage({ messages, trigger })).id,
    "message_with_pattern",
    "should select PIN_TAB mesage"
  );
});

add_task(async function checkPatternsValid() {
  const messages = (await CFRMessageProvider.getMessages()).filter(
    m => m.trigger?.patterns
  );

  for (const message of messages) {
    Assert.ok(new MatchPatternSet(message.trigger.patterns));
  }
});

add_task(async function check_userId() {
  await SpecialPowers.pushPrefEnv({
    set: [["app.normandy.user_id", "foo123"]],
  });
  is(
    await ASRouterTargeting.Environment.userId,
    "foo123",
    "should read userID from normandy user id pref"
  );
});

add_task(async function check_profileRestartCount() {
  ok(
    !isNaN(ASRouterTargeting.Environment.profileRestartCount),
    "it should return a number"
  );
});

add_task(async function check_homePageSettings_default() {
  let settings = ASRouterTargeting.Environment.homePageSettings;

  ok(settings.isDefault, "should set as default");
  ok(!settings.isLocked, "should not set as locked");
  ok(!settings.isWebExt, "should not be web extension");
  ok(!settings.isCustomUrl, "should not be custom URL");
  is(settings.urls.length, 1, "should be an 1-entry array");
  is(settings.urls[0].url, "about:home", "should be about:home");
  is(settings.urls[0].host, "", "should be an empty string");
});

add_task(async function check_homePageSettings_locked() {
  const PREF = "browser.startup.homepage";
  Services.prefs.lockPref(PREF);
  let settings = ASRouterTargeting.Environment.homePageSettings;

  ok(settings.isDefault, "should set as default");
  ok(settings.isLocked, "should set as locked");
  ok(!settings.isWebExt, "should not be web extension");
  ok(!settings.isCustomUrl, "should not be custom URL");
  is(settings.urls.length, 1, "should be an 1-entry array");
  is(settings.urls[0].url, "about:home", "should be about:home");
  is(settings.urls[0].host, "", "should be an empty string");
  Services.prefs.unlockPref(PREF);
});

add_task(async function check_homePageSettings_customURL() {
  await HomePage.set("https://www.google.com");
  let settings = ASRouterTargeting.Environment.homePageSettings;

  ok(!settings.isDefault, "should not be the default");
  ok(!settings.isLocked, "should set as locked");
  ok(!settings.isWebExt, "should not be web extension");
  ok(settings.isCustomUrl, "should be custom URL");
  is(settings.urls.length, 1, "should be an 1-entry array");
  is(settings.urls[0].url, "https://www.google.com", "should be a custom URL");
  is(
    settings.urls[0].host,
    "google.com",
    "should be the host name without 'www.'"
  );

  HomePage.reset();
});

add_task(async function check_homePageSettings_customURL_multiple() {
  await HomePage.set("https://www.google.com|https://www.youtube.com");
  let settings = ASRouterTargeting.Environment.homePageSettings;

  ok(!settings.isDefault, "should not be the default");
  ok(!settings.isLocked, "should not set as locked");
  ok(!settings.isWebExt, "should not be web extension");
  ok(settings.isCustomUrl, "should be custom URL");
  is(settings.urls.length, 2, "should be a 2-entry array");
  is(settings.urls[0].url, "https://www.google.com", "should be a custom URL");
  is(
    settings.urls[0].host,
    "google.com",
    "should be the host name without 'www.'"
  );
  is(settings.urls[1].url, "https://www.youtube.com", "should be a custom URL");
  is(
    settings.urls[1].host,
    "youtube.com",
    "should be the host name without 'www.'"
  );

  HomePage.reset();
});

add_task(async function check_homePageSettings_webExtension() {
  const extURI =
    "moz-extension://0d735548-ba3c-aa43-a0e4-7089584fbb53/homepage.html";
  await HomePage.set(extURI);
  let settings = ASRouterTargeting.Environment.homePageSettings;

  ok(!settings.isDefault, "should not be the default");
  ok(!settings.isLocked, "should not set as locked");
  ok(settings.isWebExt, "should be a web extension");
  ok(!settings.isCustomUrl, "should be custom URL");
  is(settings.urls.length, 1, "should be an 1-entry array");
  is(settings.urls[0].url, extURI, "should be a webExtension URI");
  is(settings.urls[0].host, "", "should be an empty string");

  HomePage.reset();
});

add_task(async function check_newtabSettings_default() {
  let settings = ASRouterTargeting.Environment.newtabSettings;

  ok(settings.isDefault, "should set as default");
  ok(!settings.isWebExt, "should not be web extension");
  ok(!settings.isCustomUrl, "should not be custom URL");
  is(settings.url, "about:newtab", "should be about:home");
  is(settings.host, "", "should be an empty string");
});

add_task(async function check_newTabSettings_customURL() {
  AboutNewTab.newTabURL = "https://www.google.com";
  let settings = ASRouterTargeting.Environment.newtabSettings;

  ok(!settings.isDefault, "should not be the default");
  ok(!settings.isWebExt, "should not be web extension");
  ok(settings.isCustomUrl, "should be custom URL");
  is(settings.url, "https://www.google.com", "should be a custom URL");
  is(settings.host, "google.com", "should be the host name without 'www.'");

  AboutNewTab.resetNewTabURL();
});

add_task(async function check_newTabSettings_webExtension() {
  const extURI =
    "moz-extension://0d735548-ba3c-aa43-a0e4-7089584fbb53/homepage.html";
  AboutNewTab.newTabURL = extURI;
  let settings = ASRouterTargeting.Environment.newtabSettings;

  ok(!settings.isDefault, "should not be the default");
  ok(settings.isWebExt, "should not be web extension");
  ok(!settings.isCustomUrl, "should be custom URL");
  is(settings.url, extURI, "should be the web extension URI");
  is(settings.host, "", "should be an empty string");

  AboutNewTab.resetNewTabURL();
});

add_task(async function check_openUrlTrigger_context() {
  const message = {
    ...(await CFRMessageProvider.getMessages()).find(
      m => m.id === "YOUTUBE_ENHANCE_3"
    ),
    targeting: "visitsCount == 3",
  };
  const trigger = {
    id: "openURL",
    context: { visitsCount: 3 },
    param: { host: "youtube.com", url: "https://www.youtube.com" },
  };

  is(
    (
      await ASRouterTargeting.findMatchingMessage({
        messages: [message],
        trigger,
      })
    ).id,
    message.id,
    `should select ${message.id} mesage`
  );
});

add_task(async function check_is_major_upgrade() {
  let message = {
    id: "check_is_major_upgrade",
    targeting: `isMajorUpgrade != undefined && isMajorUpgrade == ${
      Cc["@mozilla.org/browser/clh;1"].getService(Ci.nsIBrowserHandler)
        .majorUpgrade
    }`,
  };

  is(
    (await ASRouterTargeting.findMatchingMessage({ messages: [message] })).id,
    message.id,
    "Should select the message"
  );
});

add_task(async function check_userMonthlyActivity() {
  ok(
    Array.isArray(await ASRouterTargeting.Environment.userMonthlyActivity),
    "value is an array"
  );
});

add_task(async function check_doesAppNeedPin() {
  is(
    typeof (await ASRouterTargeting.Environment.doesAppNeedPin),
    "boolean",
    "Should return a boolean"
  );
});

add_task(async function check_doesAppNeedPrivatePin() {
  is(
    typeof (await ASRouterTargeting.Environment.doesAppNeedPrivatePin),
    "boolean",
    "Should return a boolean"
  );
});

add_task(async function check_isBackgroundTaskMode() {
  if (!AppConstants.MOZ_BACKGROUNDTASKS) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_BACKGROUNDTASKS");
    return;
  }

  const bts = Cc["@mozilla.org/backgroundtasks;1"].getService(
    Ci.nsIBackgroundTasks
  );

  // Pretend that this is a background task.
  bts.overrideBackgroundTaskNameForTesting("taskName");
  is(
    await ASRouterTargeting.Environment.isBackgroundTaskMode,
    true,
    "Is in background task mode"
  );
  is(
    await ASRouterTargeting.Environment.backgroundTaskName,
    "taskName",
    "Has expected background task name"
  );

  // Unset, so that subsequent test functions don't see background task mode.
  bts.overrideBackgroundTaskNameForTesting(null);
  is(
    await ASRouterTargeting.Environment.isBackgroundTaskMode,
    false,
    "Is not in background task mode"
  );
  is(
    await ASRouterTargeting.Environment.backgroundTaskName,
    null,
    "Has no background task name"
  );
});

add_task(async function check_userPrefersReducedMotion() {
  is(
    typeof (await ASRouterTargeting.Environment.userPrefersReducedMotion),
    "boolean",
    "Should return a boolean"
  );
});

add_task(async function test_distributionId() {
  let expectedDefault = Services.prefs
    .getDefaultBranch(null)
    .getCharPref("distribution.id", "");
  is(
    ASRouterTargeting.Environment.distributionId,
    expectedDefault,
    "Should return the expected default distribution Id"
  );

  Services.prefs.getDefaultBranch(null).setCharPref("distribution.id", "test");
  is(
    ASRouterTargeting.Environment.distributionId,
    "test",
    "Should return the correct distribution Id"
  );

  if (expectedDefault) {
    Services.prefs
      .getDefaultBranch(null)
      .setCharPref("distribution.id", expectedDefault);
  } else {
    Services.prefs.getDefaultBranch(null).deleteBranch("distribution.id");
  }
});

add_task(async function test_fxViewButtonAreaType_default() {
  FirefoxViewTestUtils.enableFirefoxViewButton(window);

  is(
    typeof (await ASRouterTargeting.Environment.fxViewButtonAreaType),
    "string",
    "Should return a string"
  );

  is(
    await ASRouterTargeting.Environment.fxViewButtonAreaType,
    "toolbar",
    "Should return name of container if button hasn't been removed"
  );
});

add_task(async function test_fxViewButtonAreaType_removed() {
  CustomizableUI.removeWidgetFromArea("firefox-view-button");

  is(
    await ASRouterTargeting.Environment.fxViewButtonAreaType,
    null,
    "Should return null if button has been removed"
  );
  CustomizableUI.reset();
});

add_task(async function test_alltabsButtonAreaType_default() {
  is(
    typeof (await ASRouterTargeting.Environment.alltabsButtonAreaType),
    "string",
    "Should return a string"
  );

  is(
    await ASRouterTargeting.Environment.alltabsButtonAreaType,
    "toolbar",
    "Should return name of container if button hasn't been removed"
  );
});

add_task(async function test_alltabsButtonAreaType_removed() {
  CustomizableUI.removeWidgetFromArea("alltabs-button");

  is(
    await ASRouterTargeting.Environment.alltabsButtonAreaType,
    null,
    "Should return null if button has been removed"
  );
  CustomizableUI.reset();
});

add_task(async function test_creditCardsSaved() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.creditCards.supported", "on"],
      ["extensions.formautofill.creditCards.enabled", true],
    ],
  });

  is(
    await ASRouterTargeting.Environment.creditCardsSaved,
    0,
    "Should return 0 when no credit cards are saved"
  );

  let creditcard = {
    "cc-name": "Test User",
    "cc-number": "5038146897157463",
    "cc-exp-month": "11",
    "cc-exp-year": "20",
  };

  // Intermittently fails on macOS, likely related to Bug 1714221. So, mock the
  // autofill actor.
  if (AppConstants.platform === "macosx") {
    const sandbox = sinon.createSandbox();
    registerCleanupFunction(async () => sandbox.restore());
    let stub = sandbox
      .stub(
        gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
          "FormAutofill"
        ),
        "getRecords"
      )
      .withArgs(sandbox.match({ collectionName: "creditCards" }))
      .resolves([creditcard])
      .callThrough();

    is(
      await ASRouterTargeting.Environment.creditCardsSaved,
      1,
      "Should return 1 when 1 credit card is saved"
    );
    ok(
      stub.calledWithMatch({ collectionName: "creditCards" }),
      "Targeting called getRecords"
    );

    sandbox.restore();
  } else {
    let observePromise = TestUtils.topicObserved(
      "formautofill-storage-changed"
    );
    await sendFormAutofillMessage("FormAutofill:SaveCreditCard", {
      creditcard,
    });
    await observePromise;

    is(
      await ASRouterTargeting.Environment.creditCardsSaved,
      1,
      "Should return 1 when 1 credit card is saved"
    );
    await removeAutofillRecords();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_addressesSaved() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.addresses.supported", "on"],
      ["extensions.formautofill.addresses.enabled", true],
    ],
  });

  is(
    await ASRouterTargeting.Environment.addressesSaved,
    0,
    "Should return 0 when no addresses are saved"
  );

  let observePromise = TestUtils.topicObserved("formautofill-storage-changed");
  await sendFormAutofillMessage("FormAutofill:SaveAddress", {
    address: {
      "given-name": "John",
      "additional-name": "R.",
      "family-name": "Smith",
      organization: "World Wide Web Consortium",
      "street-address": "32 Vassar Street\nMIT Room 32-G524",
      "address-level2": "Cambridge",
      "address-level1": "MA",
      "postal-code": "02139",
      country: "US",
      tel: "+16172535702",
      email: "timbl@w3.org",
    },
  });
  await observePromise;

  is(
    await ASRouterTargeting.Environment.addressesSaved,
    1,
    "Should return 1 when 1 address is saved"
  );

  await removeAutofillRecords();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_migrationInteractions() {
  const PREF_GETTER_MAPPING = new Map([
    ["browser.migrate.interactions.bookmarks", "hasMigratedBookmarks"],
    ["browser.migrate.interactions.csvpasswords", "hasMigratedCSVPasswords"],
    ["browser.migrate.interactions.history", "hasMigratedHistory"],
    ["browser.migrate.interactions.passwords", "hasMigratedPasswords"],
  ]);

  for (let [pref, getterName] of PREF_GETTER_MAPPING) {
    await pushPrefs([pref, false]);
    ok(!(await ASRouterTargeting.Environment[getterName]));
    await pushPrefs([pref, true]);
    ok(await ASRouterTargeting.Environment[getterName]);
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function check_useEmbeddedMigrationWizard() {
  await pushPrefs([
    "browser.migrate.content-modal.about-welcome-behavior",
    "default",
  ]);

  ok(!(await ASRouterTargeting.Environment.useEmbeddedMigrationWizard));

  await pushPrefs([
    "browser.migrate.content-modal.about-welcome-behavior",
    "autoclose",
  ]);

  ok(!(await ASRouterTargeting.Environment.useEmbeddedMigrationWizard));

  await pushPrefs([
    "browser.migrate.content-modal.about-welcome-behavior",
    "embedded",
  ]);

  ok(await ASRouterTargeting.Environment.useEmbeddedMigrationWizard);

  await pushPrefs([
    "browser.migrate.content-modal.about-welcome-behavior",
    "standalone",
  ]);

  ok(!(await ASRouterTargeting.Environment.useEmbeddedMigrationWizard));
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_isMSIX() {
  is(
    typeof ASRouterTargeting.Environment.isMSIX,
    "boolean",
    "Should return a boolean"
  );
  if (AppConstants.platform !== "win") {
    is(
      ASRouterTargeting.Environment.isMSIX,
      false,
      "Should always be false on non-Windows"
    );
    return;
  }

  is(
    ASRouterTargeting.Environment.isMSIX,
    Services.sysinfo.getProperty("hasWinPackageId"),
    "Should match the value from sysinfo"
  );
});

add_task(async function check_packageFamilyName() {
  if (AppConstants.platform !== "win") {
    is(
      ASRouterTargeting.Environment.packageFamilyName,
      null,
      "Should always be null on non-Windows"
    );
    return;
  }

  let winPackageFamilyName = Services.sysinfo.getProperty(
    "winPackageFamilyName"
  );
  if (winPackageFamilyName === "") {
    is(
      ASRouterTargeting.Environment.packageFamilyName,
      null,
      "Should be null if sysinfo is empty"
    );
  } else {
    is(
      ASRouterTargeting.Environment.packageFamilyName,
      winPackageFamilyName,
      "Should match non-empty sysinfo"
    );
  }
});

add_task(async function check_msixConsistency() {
  if (ASRouterTargeting.Environment.isMSIX) {
    Assert.greater(
      ASRouterTargeting.Environment.packageFamilyName.length,
      0,
      "packageFamilyName should be non-empty if installed by MSIX"
    );
  } else {
    is(
      ASRouterTargeting.Environment.packageFamilyName,
      null,
      "packageFamilyName should be empty if not installed by MSIX"
    );
  }

  if (ASRouterTargeting.Environment.packageFamilyName === null) {
    is(
      ASRouterTargeting.Environment.isMSIX,
      false,
      "isMSIX should be false if packageFamilyName is not present"
    );
  } else {
    is(
      ASRouterTargeting.Environment.isMSIX,
      true,
      "isMSIX should be true if packageFamilyName is present"
    );
  }
});

add_task(async function check_isRTAMO() {
  is(
    typeof ASRouterTargeting.Environment.isRTAMO,
    "boolean",
    "Should return a boolean"
  );

  const TEST_CASES = [
    {
      title: "no attribution data",
      attributionData: {},
      expected: false,
    },
    {
      title: "null attribution data",
      attributionData: null,
      expected: false,
    },
    {
      title: "no content",
      attributionData: {
        source: "addons.mozilla.org",
      },
      expected: false,
    },
    {
      title: "empty content",
      attributionData: {
        source: "addons.mozilla.org",
        content: "",
      },
      expected: false,
    },
    {
      title: "null content",
      attributionData: {
        source: "addons.mozilla.org",
        content: null,
      },
      expected: false,
    },
    {
      title: "empty source",
      attributionData: {
        source: "",
      },
      expected: false,
    },
    {
      title: "null source",
      attributionData: {
        source: null,
      },
      expected: false,
    },
    {
      title: "valid attribution data for RTAMO with content not encoded",
      attributionData: {
        source: "addons.mozilla.org",
        content: "rta:<encoded-addon-id>",
      },
      expected: true,
    },
    {
      title: "valid attribution data for RTAMO with content encoded once",
      attributionData: {
        source: "addons.mozilla.org",
        content: "rta%3A<encoded-addon-id>",
      },
      expected: true,
    },
    {
      title: "valid attribution data for RTAMO with content encoded twice",
      attributionData: {
        source: "addons.mozilla.org",
        content: "rta%253A<encoded-addon-id>",
      },
      expected: true,
    },
    {
      title: "invalid source",
      attributionData: {
        source: "www.mozilla.org",
        content: "rta%3A<encoded-addon-id>",
      },
      expected: false,
    },
  ];

  const sandbox = sinon.createSandbox();
  registerCleanupFunction(async () => {
    sandbox.restore();
  });

  const stub = sandbox.stub(AttributionCode, "getCachedAttributionData");

  for (const { title, attributionData, expected } of TEST_CASES) {
    stub.returns(attributionData);

    is(
      ASRouterTargeting.Environment.isRTAMO,
      expected,
      `${title} - Expected isRTAMO to have the expected value`
    );
  }

  sandbox.restore();
});

add_task(async function check_isDeviceMigration() {
  is(
    typeof ASRouterTargeting.Environment.isDeviceMigration,
    "boolean",
    "Should return a boolean"
  );

  const TEST_CASES = [
    {
      title: "no attribution data",
      attributionData: {},
      expected: false,
    },
    {
      title: "null attribution data",
      attributionData: null,
      expected: false,
    },
    {
      title: "no campaign",
      attributionData: {
        source: "support.mozilla.org",
      },
      expected: false,
    },
    {
      title: "empty campaign",
      attributionData: {
        source: "support.mozilla.org",
        campaign: "",
      },
      expected: false,
    },
    {
      title: "null campaign",
      attributionData: {
        source: "addons.mozilla.org",
        campaign: null,
      },
      expected: false,
    },
    {
      title: "empty source",
      attributionData: {
        source: "",
      },
      expected: false,
    },
    {
      title: "null source",
      attributionData: {
        source: null,
      },
      expected: false,
    },
    {
      title: "other source",
      attributionData: {
        source: "www.mozilla.org",
        campaign: "migration",
      },
      expected: true,
    },
    {
      title: "valid attribution data for isDeviceMigration",
      attributionData: {
        source: "support.mozilla.org",
        campaign: "migration",
      },
      expected: true,
    },
  ];

  const sandbox = sinon.createSandbox();
  registerCleanupFunction(async () => {
    sandbox.restore();
  });

  const stub = sandbox.stub(AttributionCode, "getCachedAttributionData");

  for (const { title, attributionData, expected } of TEST_CASES) {
    stub.returns(attributionData);

    is(
      ASRouterTargeting.Environment.isDeviceMigration,
      expected,
      `${title} - Expected isDeviceMigration to have the expected value`
    );
  }

  sandbox.restore();
});

add_task(async function check_primaryResolution() {
  is(
    typeof ASRouterTargeting.Environment.primaryResolution,
    "object",
    "Should return an object"
  );

  is(
    typeof ASRouterTargeting.Environment.primaryResolution.width,
    "number",
    "Width property should return a number"
  );

  is(
    typeof ASRouterTargeting.Environment.primaryResolution.height,
    "number",
    "Height property should return a number"
  );
});

add_task(async function check_archBits() {
  const bits = ASRouterTargeting.Environment.archBits;
  is(typeof bits, "number", "archBits should be a number");
  ok(bits === 32 || bits === 64, "archBits is either 32 or 64");
});

add_task(async function check_systemArch() {
  const arch = ASRouterTargeting.Environment.systemArch;
  is(typeof arch, "string", "systemArch should be a string");
  ok(
    ["x86", "x86-64", "aarch64"].includes(arch),
    "systemArch is either x86, x86-64 or aarch64"
  );
});

add_task(async function check_memoryMB() {
  const memory = ASRouterTargeting.Environment.memoryMB;
  is(typeof memory, "number", "Memory is a number");
  // To make sure we get a sensible number we verify that whatever system
  // runs this unit test it has between 500MB and 1TB of RAM.
  ok(memory > 500 && memory < 5_000_000);
});

add_task(async function check_totalSearches() {
  await pushPrefs(["browser.search.totalSearches", 20]);
  is(
    typeof ASRouterTargeting.Environment.totalSearches,
    "number",
    "should return a number"
  );

  is(
    await ASRouterTargeting.Environment.totalSearches,
    20,
    "should return a value of 20"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function checkisDefaultBrowserUncached() {
  const expected = ShellService.isDefaultBrowser();
  const result = await ASRouterTargeting.Environment.isDefaultBrowserUncached;
  is(
    typeof result,
    "boolean",
    "isDefaultBrowserUncached should be a boolean value"
  );
  is(
    result,
    expected,
    "isDefaultBrowserUncached should be equal to ShellService.isDefaultBrowser()"
  );
  const message = {
    id: "foo",
    targeting: `isDefaultBrowserUncached == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by isDefaultBrowserUncached"
  );
});

add_task(async function check_doesAppNeedPin() {
  const expected = await ShellService.doesAppNeedPin();
  const result = await ASRouterTargeting.Environment.doesAppNeedPinUncached;
  is(
    typeof (await ASRouterTargeting.Environment.doesAppNeedPinUncached),
    "boolean",
    "Should return a boolean"
  );
  is(
    result,
    expected,
    "doesAppNeedPinUncached should be equal to ShellService.doesAppNeedPin()"
  );
  const message = {
    id: "foo",
    targeting: `doesAppNeedPinUncached == ${expected.toString()}`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by doesAppNeedPinUncached"
  );
});

add_task(
  async function check_activeNotifications_newtab_topic_selection_modal_shown_past() {
    // 10 minutes ago
    let timestamp10MinsAgo = `${new Date().getTime() - 600000}`;
    await pushPrefs([
      "browser.newtabpage.activity-stream.discoverystream.topicSelection.onboarding.lastDisplayed",
      timestamp10MinsAgo,
    ]);

    is(
      await ASRouterTargeting.Environment.activeNotifications,
      false,
      "activeNotifications should be false if the topic selection modal on newtab was last shown more than a minute ago"
    );
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function check_activeNotifications_infobar_shown() {
  let message = {
    ...(await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    ),
  };

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  is(
    await ASRouterTargeting.Environment.activeNotifications,
    true,
    "activeNotifications should be true when an Infobar is rendered"
  );

  // dismiss infobar
  infobar.notification.closeButton.click();
  dispatchStub.reset();
});

add_task(
  async function check_activeNotifications_newtab_topic_selection_modal_shown_recently() {
    // 1 second ago
    let timestamp1SecAgo = `${new Date().getTime() - 1000}`;
    await pushPrefs([
      "browser.newtabpage.activity-stream.discoverystream.topicSelection.onboarding.lastDisplayed",
      timestamp1SecAgo,
    ]);

    is(
      await ASRouterTargeting.Environment.activeNotifications,
      true,
      "activeNotifications should be true if the topic selection modal on newtab was last shown less than a minute ago"
    );
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function check_activeNotifications_newtabMessages() {
  const win = BrowserWindowTracker.getTopWindow();

  // Simulate logic from NewtabMessage.sys.mjs
  const testObserver = {
    observe(subject, topic) {
      if (topic === "newtab-message-query") {
        if (subject.wrappedJSObject.browser === win.gBrowser) {
          subject.wrappedJSObject.activeNewtabMessage = true;
        }
      }
    },
  };

  Services.obs.addObserver(testObserver, "newtab-message-query");

  is(
    await ASRouterTargeting.Environment.activeNotifications,
    true,
    "activeNotifications should be true if observer sets activeNewtabMessage"
  );

  // clean up
  Services.obs.removeObserver(testObserver, "newtab-message-query");
});

add_task(async function activeNotifications_default_prompt_shown() {
  let sb = sinon.createSandbox();

  const win = await BrowserTestUtils.openNewBrowserWindow();

  sb.stub(DefaultBrowserCheck, "willCheckDefaultBrowser").returns(true);
  const promptSpy = sb.spy(DefaultBrowserCheck, "prompt");

  await BROWSER_GLUE._maybeShowDefaultBrowserPrompt();

  Assert.equal(promptSpy.callCount, 1, "default prompt should be called");

  // BrowserGlue doesn't await the prompt, and the dialog only opens after
  // some some async things happen (eg pin checks, localization), so we should
  // wait for it to actually be showing in the window that received it
  const [promptWin] = promptSpy.firstCall.args;
  await TestUtils.waitForCondition(
    () => promptWin.gDialogBox?.isOpen,
    "Waiting for the default browser prompt to open",
    100,
    100
  );
  // activeNotifications only inspects the top window, so make sure that's the
  // window showing the prompt.
  await SimpleTest.promiseFocus(promptWin);

  is(
    await ASRouterTargeting.Environment.activeNotifications,
    true,
    "activeNotifications should be true if the set to default prompt is being shown"
  );
  let dialogClosed = BrowserTestUtils.waitForEvent(
    promptWin,
    "DOMModalDialogClosed"
  );
  promptWin.gDialogBox.dialog?.close();
  await dialogClosed;
  await BrowserTestUtils.closeWindow(win);
  sb.restore();
});

add_task(async function activeNotifications_feature_callout_shown() {
  let sb = sinon.createSandbox();
  const win = await BrowserTestUtils.openNewBrowserWindow();

  let callout = await FeatureCalloutBroker.showFeatureCallout(
    win.gBrowser.selectedBrowser,
    testFeatureCallout
  );
  ok(callout, "Callout shown");

  is(
    await ASRouterTargeting.Environment.activeNotifications,
    true,
    "activeNotifications should be true if a feature callout is being shown"
  );
  await BrowserTestUtils.closeWindow(win);
  sb.restore();
});

add_task(async function activeNotifications_spotlight_shown() {
  let sb = sinon.createSandbox();
  const IMPORT_SCREEN = {
    id: "AW_IMPORT",
    content: {
      primary_button: {
        label: "import",
        action: {
          navigate: true,
          type: "SHOW_MIGRATION_WIZARD",
        },
      },
    },
  };
  const win = await BrowserTestUtils.openNewBrowserWindow();

  Spotlight.showSpotlightDialog(win.gBrowser.selectedBrowser, {
    content: { modal: "tab", screens: [IMPORT_SCREEN] },
  });

  await TestUtils.waitForCondition(
    () => win.document.readyState === "complete",
    "Waiting for spotlight dialog to finish loading"
  );

  is(
    await ASRouterTargeting.Environment.activeNotifications,
    true,
    "activeNotifications should be true if a spotlight is being shown"
  );
  await BrowserTestUtils.closeWindow(win);
  sb.restore();
});

add_task(async function check_unhandledCampaignAction() {
  is(
    typeof ASRouterTargeting.Environment.unhandledCampaignAction,
    "object",
    "Should return an object" // is null unless an unhandled action is present
  );

  const DID_HANDLE_CAMAPAIGN_ACTION_PREF =
    "trailhead.firstrun.didHandleCampaignAction";

  const TEST_CASES = [
    {
      title: "unsupported open_url campaign action",
      attributionData: {
        campaign: "open_url",
      },
      expected: null,
      after: () => {
        QueryCache.queries.UnhandledCampaignAction.expire();
      },
    },
    {
      title:
        "supported (pin and set default) and unhandled set default browser campaign action",
      attributionData: {
        campaign: "pin_and_default",
      },
      expected: "PIN_AND_DEFAULT",
      after: () => {
        QueryCache.queries.UnhandledCampaignAction.expire();
      },
    },
    {
      title:
        "supported (pin to taskbar) and unhandled set default browser campaign action",
      attributionData: {
        campaign: "pin_firefox_to_taskbar",
      },
      expected: "PIN_FIREFOX_TO_TASKBAR",
      after: () => {
        QueryCache.queries.UnhandledCampaignAction.expire();
      },
    },
    {
      title:
        "supported (set default) and unhandled set default browser campaign action",
      attributionData: {
        campaign: "set_default_browser",
      },
      expected: "SET_DEFAULT_BROWSER",
      after: () => {
        QueryCache.queries.UnhandledCampaignAction.expire();
      },
    },
    {
      title:
        "supported (set default) and handled set default browser campaign action",
      attributionData: {
        campaign: "set_default_browser",
      },
      expected: null,
      before: async () => {
        await pushPrefs([DID_HANDLE_CAMAPAIGN_ACTION_PREF, true]);
      },
      after: async () => {
        await SpecialPowers.popPrefEnv();
        QueryCache.queries.UnhandledCampaignAction.expire();
      },
    },
  ];

  const sandbox = sinon.createSandbox();
  registerCleanupFunction(async () => {
    sandbox.restore();
  });

  const stub = sandbox.stub(AttributionCode, "getCachedAttributionData");

  for (const {
    title,
    attributionData,
    expected,
    before,
    after,
  } of TEST_CASES) {
    if (before) {
      await before();
    }
    stub.returns(attributionData);
    is(
      ASRouterTargeting.Environment.unhandledCampaignAction,
      expected,
      `${title} - Expected unhandledCampaignAction to have the expected value`
    );
    if (after) {
      after();
    }
  }
});

add_task(async function check_profileGroupIdTargeting() {
  const expected = await ClientID.getCachedProfileGroupID();
  const result = await ASRouterTargeting.Environment.profileGroupId;

  is(typeof result, "string", "profileGroupId should be a string");

  is(result, expected, "it should be equal to the profile group id");

  const message = {
    id: "foo",
    targeting: `profileGroupId == "${expected.toString()}"`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by profile group id"
  );
});

add_task(async function check_currentProfileIdTargeting() {
  is(
    typeof ASRouterTargeting.Environment.currentProfileId,
    "string",
    "Should return a string"
  );

  const message = {
    id: "foo",
    targeting: `currentProfileId == "test-profile-id"`,
  };

  const result = await ASRouterTargeting.findMatchingMessage({
    messages: [message],
    context: { currentProfileId: "test-profile-id" },
  });

  is(result, message, "should select correct item by profile id");
});

add_task(async function check_profileGroupProfileCountTargeting() {
  await pushPrefs(
    ["browser.profiles.enabled", false],
    ["browser.profiles.created", false]
  );
  const resultFalse =
    await ASRouterTargeting.Environment.profileGroupProfileCount;

  is(typeof resultFalse, "number", "Should return a number");

  is(resultFalse, 0, "should be zero because profiles are disabled");

  await pushPrefs(
    ["browser.profiles.enabled", true],
    ["browser.profiles.created", true]
  );

  const expected = await SelectableProfileService.getProfileCount();
  const resultTrue =
    await ASRouterTargeting.Environment.profileGroupProfileCount;

  is(resultTrue, expected, "it should be equal to the profile group count");

  const message = {
    id: "foo",
    targeting: `profileGroupProfileCount == "${expected}"`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item by number of profiles in the group"
  );

  //Clean up the prefs
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_buildId() {
  is(
    typeof ASRouterTargeting.Environment.buildId,
    "number",
    "Should return a number"
  );

  const message = {
    id: "foo",
    // Later than January 2025
    targeting: `buildId >= 20251010000`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by build ID"
  );
});

add_task(async function test_newtabAddonVersion() {
  const FAKE_NEWTAB_VERSION = "145.0.2123.2131";
  const sandbox = sinon.createSandbox();

  sandbox
    .stub(AboutNewTabResourceMapping, "addonVersion")
    .get(() => FAKE_NEWTAB_VERSION);

  is(
    ASRouterTargeting.Environment.newtabAddonVersion,
    FAKE_NEWTAB_VERSION,
    "Should return the newtab addon version as reported by AboutNewTabResourceMapping"
  );

  const message = {
    id: "foo",
    targeting: `newtabAddonVersion|versionCompare('143.1.0') >= 0`,
  };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "should select correct item when filtering by newtabAddonVersion"
  );
});

add_task(async function check_backupsInfo() {
  if (AppConstants.platform === "macosx") {
    // Bug 2033325: backupsInfo short-circuits on macOS.
    Assert.deepEqual(
      await ASRouterTargeting.Environment.backupsInfo,
      { found: false },
      "Should return {found: false} on macOS"
    );
    return;
  }

  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  const testBackupResponse = {
    found: true,
    backupFileToRestore: "/tmp/profile-backup.jsonlz4",
    multipleBackupsFound: false,
  };

  const stub = sandbox
    .stub(QueryCache.getters.backupsInfo, "get")
    .resolves(testBackupResponse);

  Assert.deepEqual(
    await ASRouterTargeting.Environment.backupsInfo,
    testBackupResponse,
    "Should return structured backup info from the cached getter"
  );

  const message = { id: "foo", targeting: "backupsInfo.found" };
  is(
    await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
    message,
    "Should select message when a backup was found"
  );

  Assert.ok(stub.called, "backupsInfo getter was called");
});

add_task(async function check_isEncryptedBackup() {
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  is(
    await ASRouterTargeting.Environment.isEncryptedBackup,
    false,
    "should return false if the pref is unset"
  );

  await pushPrefs(["messaging-system-action.backupChooser", "easy"]);
  is(
    await ASRouterTargeting.Environment.isEncryptedBackup,
    false,
    "should return false if the pref value is easy"
  );

  await pushPrefs(["messaging-system-action.backupChooser", "full"]);
  is(
    await ASRouterTargeting.Environment.isEncryptedBackup,
    true,
    "should return true if the pref value is full"
  );
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_backupArchiveEnabled() {
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  await pushPrefs(["browser.backup.archive.enabled", true]);

  // Backup is disabled whenever SQLite at-rest encryption is on, so the
  // targeting value is false regardless of the killswitch in that build.
  const sqliteEncryptionDisablesBackup = Services.prefs.getBoolPref(
    "security.storage.encryption.sqlite.enabled",
    false
  );
  is(
    await ASRouterTargeting.Environment.backupArchiveEnabled,
    !sqliteEncryptionDisablesBackup,
    sqliteEncryptionDisablesBackup
      ? "should be false when SQLite at-rest encryption disables backup"
      : "should return true if the killswitch is not on"
  );
  await SpecialPowers.popPrefEnv();
  const archiveExperiment = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "backupService",
    value: { archiveKillswitch: true },
  });
  await pushPrefs(["browser.backup.archive.enabled", true]);

  is(
    await ASRouterTargeting.Environment.backupArchiveEnabled,
    false,
    "should return false if the killswitch is on"
  );

  // End the experiment.
  await archiveExperiment();
  await SpecialPowers.popPrefEnv();
});

add_task(async function check_backupRestoreEnabled() {
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  await pushPrefs(["browser.backup.restore.enabled", true]);

  // Backup is disabled whenever SQLite at-rest encryption is on, so the
  // targeting value is false regardless of the killswitch in that build.
  const sqliteEncryptionDisablesBackup = Services.prefs.getBoolPref(
    "security.storage.encryption.sqlite.enabled",
    false
  );
  is(
    await ASRouterTargeting.Environment.backupRestoreEnabled,
    !sqliteEncryptionDisablesBackup,
    sqliteEncryptionDisablesBackup
      ? "should be false when SQLite at-rest encryption disables backup"
      : "should return true if the killswitch is not on"
  );
  await SpecialPowers.popPrefEnv();
  await pushPrefs(["browser.backup.restore.enabled", true]);

  const restoreExperiment = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "backupService",
    value: { restoreKillswitch: true },
  });

  is(
    await ASRouterTargeting.Environment.backupRestoreEnabled,
    false,
    "should return false if the killswitch is on"
  );

  // End the experiment.
  await restoreExperiment();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function check_userWeekdaysActiveInLastMonth_counts_only_weekdays() {
    const sandbox = sinon.createSandbox();
    try {
      QueryCache.queries.UserMonthlyActivity.expire();
      sandbox
        .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
        .resolves([
          [50, "2024-01-08"], // Monday, 50 visits
          [30, "2024-01-09"], // Tue
          [10, "2024-01-13"], // Sat
          [5, "2024-01-14"], // Sun
        ]);
      is(
        await ASRouterTargeting.Environment.userWeekdaysActiveInLastMonth,
        2,
        "should count only weekday entries(2)"
      );
    } finally {
      sandbox.restore();
    }
  }
);

add_task(async function check_userWeekdaysActiveInLastMonth_all_weekends() {
  const sandbox = sinon.createSandbox();
  try {
    QueryCache.queries.UserMonthlyActivity.expire();
    sandbox
      .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
      .resolves([
        [20, "2024-01-13"], // Sat
        [20, "2024-01-14"], // Sun
      ]);
    is(
      await ASRouterTargeting.Environment.userWeekdaysActiveInLastMonth,
      0,
      "should return 0 when all activity is on weekends"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_userWeekdaysActiveInLastMonth_emptyActivity() {
  const sandbox = sinon.createSandbox();
  try {
    QueryCache.queries.UserMonthlyActivity.expire();
    sandbox
      .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
      .resolves([]);
    is(
      await ASRouterTargeting.Environment.userWeekdaysActiveInLastMonth,
      0,
      "should return 0 for empty activity"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(
  async function check_userActiveDaysWithHundredPlusSites_countsDaysAtOrAbove100() {
    const sandbox = sinon.createSandbox();
    try {
      QueryCache.queries.UserMonthlyActivity.expire();
      sandbox
        .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
        .resolves([
          [150, "2024-01-08"],
          [99, "2024-01-09"],
          [100, "2024-01-10"],
          [50, "2024-01-11"],
        ]);
      is(
        await ASRouterTargeting.Environment.userActiveDaysWithHundredPlusSites,
        2,
        "should count days with >= 100 URL visits"
      );
    } finally {
      sandbox.restore();
    }
  }
);

add_task(async function check_userActiveDaysWithHundredPlusSites_none() {
  const sandbox = sinon.createSandbox();
  try {
    QueryCache.queries.UserMonthlyActivity.expire();
    sandbox
      .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
      .resolves([
        [10, "2024-01-08"],
        [99, "2024-01-09"],
      ]);
    is(
      await ASRouterTargeting.Environment.userActiveDaysWithHundredPlusSites,
      0,
      "should return 0 when no days reach 100 visits"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(
  async function check_userActiveDaysWithHundredPlusSites_emptyActivityReturnsZero() {
    const sandbox = sinon.createSandbox();
    try {
      QueryCache.queries.UserMonthlyActivity.expire();
      sandbox
        .stub(NewTabUtils.activityStreamProvider, "getUserMonthlyActivity")
        .resolves([]);
      is(
        await ASRouterTargeting.Environment.userActiveDaysWithHundredPlusSites,
        0,
        "should return 0 for empty activity"
      );
    } finally {
      sandbox.restore();
    }
  }
);

add_task(async function check_crashCount_noCrashesReturnsZero() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([]);
    is(
      await ASRouterTargeting.Environment.crashCount,
      0,
      "should return 0 for empty crash dumps"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_crashCount_returnsNumberOfCrashes() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox
      .stub(QueryCache.getters.crashData, "get")
      .resolves(testCrashDumpFiles);
    is(
      await ASRouterTargeting.Environment.crashCount,
      3,
      "should return 3 for three crash dumps"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_daysSinceLastCrash_noCrashesReturnsNull() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([]);
    is(
      await ASRouterTargeting.Environment.daysSinceLastCrash,
      null,
      "should return null for no crashes"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_daysSinceLastCrash_returnsDaysSinceLastCrash() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox
      .stub(QueryCache.getters.crashData, "get")
      .resolves(testCrashDumpFiles);
    is(
      await ASRouterTargeting.Environment.daysSinceLastCrash,
      10,
      "should return 10 for most recent crash from 10 days ago"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_crashCountInLastDay_noCrashesReturnsZero() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([]);
    is(
      await ASRouterTargeting.Environment.crashCountInLastDay,
      0,
      "should return 0 for empty crash dumps"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_crashCountInLastDay_onlyCountsRecentCrashes() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([
      ...testCrashDumpFiles,
      {
        path: "/path/to/crash4.dmp",
        id: "crash4",
        date: new Date().getTime() - 60 * 60 * 1000,
      },
    ]);
    is(
      await ASRouterTargeting.Environment.crashCountInLastDay,
      1,
      "should only count the crash from within the last 24 hours"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_crashCountInLastWeek_noCrashesReturnsZero() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([]);
    is(
      await ASRouterTargeting.Environment.crashCountInLastWeek,
      0,
      "should return 0 for empty crash dumps"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_crashCountInLastWeek_onlyCountsRecentCrashes() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(QueryCache.getters.crashData, "get").resolves([
      ...testCrashDumpFiles,
      {
        path: "/path/to/crash4.dmp",
        id: "crash4",
        date: new Date().getTime() - 2 * 24 * 60 * 60 * 1000,
      },
      {
        path: "/path/to/crash5.dmp",
        id: "crash5",
        date: new Date().getTime() - 5 * 24 * 60 * 60 * 1000,
      },
    ]);
    is(
      await ASRouterTargeting.Environment.crashCountInLastWeek,
      2,
      "should only count crashes from within the last 7 days"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_previousSessionCrashed() {
  const sandbox = sinon.createSandbox();
  try {
    sandbox.stub(SessionStartup, "previousSessionCrashed").get(() => true);
    is(
      ASRouterTargeting.Environment.previousSessionCrashed,
      true,
      "should be true when the previous session crashed"
    );

    const message = {
      id: "check_previousSessionCrashed",
      targeting: "previousSessionCrashed",
    };
    is(
      (await ASRouterTargeting.findMatchingMessage({ messages: [message] }))
        ?.id,
      message.id,
      "should select message targeting previousSessionCrashed when it is true"
    );

    sandbox.restore();
    sandbox.stub(SessionStartup, "previousSessionCrashed").get(() => false);
    is(
      ASRouterTargeting.Environment.previousSessionCrashed,
      false,
      "should be false when the previous session did not crash"
    );
    is(
      await ASRouterTargeting.findMatchingMessage({ messages: [message] }),
      null,
      "should not select message targeting previousSessionCrashed when it is false"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function check_isLaunchOnLogin() {
  const result = ASRouterTargeting.Environment.isLaunchOnLogin;
  is(typeof result, "boolean", "isLaunchOnLogin should be a boolean");
  is(
    result,
    BrowserInitState.isLaunchOnLogin,
    "isLaunchOnLogin should reflect BrowserInitState.isLaunchOnLogin"
  );

  const message = {
    id: "check_isLaunchOnLogin",
    targeting: `isLaunchOnLogin == ${BrowserInitState.isLaunchOnLogin}`,
  };
  is(
    (await ASRouterTargeting.findMatchingMessage({ messages: [message] })).id,
    message.id,
    "should select message matching current isLaunchOnLogin value"
  );
});
