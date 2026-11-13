const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { LangPackMatcher } = ChromeUtils.importESModule(
  "resource://gre/modules/LangPackMatcher.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const TEST_HOST = "example.com";

const MISSING_DETAILS_ERROR =
  "Missing addon details, locale/dictionary not installed";
const INVALID_TYPE_ERROR =
  "Invalid type provided, locale/dictionary not installed";
const DOWNLOAD_FAILED_ERROR = "Error: Install failed: onDownloadFailed";

const ADDON_IDS = new Set();

const LANGUAGE_PACKS = [
  {
    guid: "langpack-fr@tests.mozilla.org",
    name: {
      "en-US": "French language pack",
    },
    type: "language",
    target_locale: "fr",
    current_compatible_version: {
      version: "1.0",
      files: [
        {
          platform: "all",
          url: `http://${TEST_HOST}/addons/langpack-fr.xpi`,
          hash: "PLACEHOLDER",
        },
      ],
    },
  },
  {
    guid: "langpack-de@tests.mozilla.org",
    name: {
      "en-US": "German language pack",
    },
    type: "language",
    target_locale: "de",
    current_compatible_version: {
      version: "2.0",
      files: [
        {
          platform: "all",
          url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
          hash: "PLACEHOLDER",
        },
      ],
    },
  },
];

const DICTIONARIES = [
  {
    guid: "fr-one@tests.mozilla.org",
    name: "First French dictionary",
    type: "dictionary",
    current_version: {
      version: "3.0",
      files: [
        {
          platform: "all",
          url: `http://${TEST_HOST}/addons/dictionary-fr-one.xpi`,
          hash: "PLACEHOLDER",
        },
      ],
    },
  },
  {
    guid: "fr-two@tests.mozilla.org",
    name: "Second French dictionary",
    type: "dictionary",
    current_version: {
      version: "4.0",
      files: [
        {
          platform: "all",
          url: `http://${TEST_HOST}/addons/dictionary-fr-two.xpi`,
          hash: "PLACEHOLDER",
        },
      ],
    },
  },
];

const LANGUAGE_DICTIONARY_MAPPING = [
  {
    id: "fr",
    dictionaries: ["fr-one@tests.mozilla.org", "fr-two@tests.mozilla.org"],
  },
  {
    id: "de",
    dictionaries: [],
  },
];

let testServer;

async function setupLanguagePackAddon(languagePack) {
  const currentVersion = languagePack.current_compatible_version;
  const id = languagePack.guid;
  const addon = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      name: languagePack.name["en-US"],
      version: currentVersion.version,
      browser_specific_settings: {
        gecko: {
          id,
        },
      },
      langpack_id: languagePack.target_locale,
      languages: {},
    },
  });
  ADDON_IDS.add(id);
  currentVersion.files[0].hash = `sha256:${await IOUtils.computeHexDigest(addon.path, "sha256")}`;
  testServer.registerFile(new URL(currentVersion.files[0].url).pathname, addon);

  return addon;
}

async function setupDictionaryAddon(dictionary) {
  const currentVersion = dictionary.current_version;
  const id = dictionary.guid;
  const idPrefix = id.split("@")[0];
  const addon = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      name: dictionary.name,
      version: currentVersion.version,
      browser_specific_settings: {
        gecko: {
          id,
        },
      },
      dictionaries: {
        [idPrefix]: idPrefix + ".dic",
      },
    },
    files: {
      [idPrefix + ".aff"]: "",
      [idPrefix + ".dic"]: "",
    },
  });
  ADDON_IDS.add(id);
  currentVersion.files[0].hash = `sha256:${await IOUtils.computeHexDigest(addon.path, "sha256")}`;
  testServer.registerFile(new URL(currentVersion.files[0].url).pathname, addon);
  return addon;
}

async function testInstallAddon(
  addonURL,
  addonDetails,
  expectedError,
  description
) {
  const descriptionPrefix = `[${description}]: `;
  const expectedResult = !expectedError;

  // Run the test case.
  const stopListening = TestUtils.listenForConsoleMessages();
  const result = await LangPackMatcher.mockable._installAddon(
    addonURL,
    addonDetails
  );
  const messages = await stopListening();

  // Check the result and error message.
  Assert.equal(result, expectedResult, descriptionPrefix + "Correct result");
  if (expectedError) {
    Assert.ok(
      messages.some(
        message =>
          message.level === "error" && message.arguments[0] == expectedError
      ),
      descriptionPrefix + "Error message shown"
    );
  }

  // Check the add-on install.
  if (addonDetails?.id) {
    const addon = await AddonManager.getAddonByID(addonDetails.id);
    Assert.equal(
      !!addon,
      expectedResult,
      descriptionPrefix + "Add-on installed"
    );

    if (addon) {
      for (const key of ["id", "type", "version"]) {
        Assert.equal(addon[key], addonDetails[key], descriptionPrefix + key);
      }

      await addon.uninstall();
    }
  }
}

async function testInstallLangPack(
  langPack,
  expectedInstalledAddonIDs,
  description
) {
  const descriptionPrefix = `[${description}]: `;
  const expectedResult = !!expectedInstalledAddonIDs.length;

  const expectedInstallsFinished = Promise.all(
    expectedInstalledAddonIDs.map(id =>
      AddonTestUtils.promiseAddonEvent("onInstalled", addon => addon.id === id)
    )
  );

  const result = await LangPackMatcher.mockable.installLangPack(langPack);
  Assert.equal(result, expectedResult, descriptionPrefix + "Correct result");

  // Dictionaries are installed after the installLangPack result is returned,
  // so take care to wait for them to finish.
  await expectedInstallsFinished;

  const installedAddons = (
    await AddonManager.getAddonsByIDs(Array.from(ADDON_IDS))
  ).filter(addon => addon);
  const installedAddonIDs = installedAddons.map(addon => addon.id).sort();
  Assert.deepEqual(
    installedAddonIDs,
    expectedInstalledAddonIDs.sort(),
    descriptionPrefix + "Correct add-ons installed"
  );

  for (const addon of installedAddons) {
    await addon.uninstall();
  }
}

add_setup(async function setup() {
  AddonTestUtils.init(this);
  AddonTestUtils.overrideCertDB();
  AddonTestUtils.createAppInfo(
    "xpcshell@tests.mozilla.org",
    "XPCShell",
    "1",
    "1"
  );

  // Set up the testing HTTP server.
  testServer = AddonTestUtils.createHttpServer({ hosts: [TEST_HOST] });
  testServer.registerDirectory("/data/", do_get_file("data"));

  // Set up the language packs and dictionaries endpoints.
  AddonTestUtils.registerJSON(testServer, "/langpacks.json", {
    results: LANGUAGE_PACKS,
  });
  // Note: Skip handling the "?guid=%IDS%&lang=%LOCALE%" filtering, since
  //       `getAddonsByIDs()` filters results by the requested ID anyway.
  AddonTestUtils.registerJSON(testServer, "/dictionaries.json", {
    results: DICTIONARIES,
    next: null,
  });

  // Use the dictionary endpoint.
  Services.prefs.setStringPref(
    "extensions.getAddons.get.url",
    `http://${TEST_HOST}/dictionaries.json`
  );

  // Set up the language pack to dictionary mapping.
  const dictionaryClient = RemoteSettings("language-dictionaries");
  await dictionaryClient.db.importChanges(
    {},
    Date.now(),
    LANGUAGE_DICTIONARY_MAPPING,
    { clear: true }
  );

  // Create the language pack and dictionary addons.
  await Promise.all([
    ...LANGUAGE_PACKS.map(setupLanguagePackAddon),
    ...DICTIONARIES.map(setupDictionaryAddon),
  ]);

  await AddonTestUtils.promiseStartupManager();
});

// Test _installAddon installs locales/dictionaries correctly, but does not
// install non-language add-ons, or add-ons with incorrect or missing details.
add_task(async function test_installAddon_details_validated() {
  // Correct French language pack details.
  const {
    guid: id,
    current_compatible_version: {
      version,
      files: [{ hash, url }],
    },
  } = LANGUAGE_PACKS[0];
  const type = "locale";

  await testInstallAddon(url, {}, MISSING_DETAILS_ERROR, "No add-on details");

  await testInstallAddon(
    url,
    { version, type },
    MISSING_DETAILS_ERROR,
    "Missing add-on ID"
  );

  await testInstallAddon(
    url,
    { id, type },
    MISSING_DETAILS_ERROR,
    "Missing add-on version"
  );

  await testInstallAddon(
    url,
    { id, version },
    MISSING_DETAILS_ERROR,
    "Missing add-on type"
  );

  await testInstallAddon(
    url,
    { id, version, type: "extension" },
    INVALID_TYPE_ERROR,
    "Non-language add-on type"
  );

  await testInstallAddon(
    url,
    { id, version: "2.0", type },
    DOWNLOAD_FAILED_ERROR,
    "Wrong version"
  );

  await testInstallAddon(
    url,
    { id, version, type: "dictionary" },
    DOWNLOAD_FAILED_ERROR,
    "Wrong type"
  );

  await testInstallAddon(
    url,
    { id: "wrong", version, type },
    DOWNLOAD_FAILED_ERROR,
    "Wrong ID"
  );

  await testInstallAddon(
    url,
    { id, version, type, hash: "wrong" },
    DOWNLOAD_FAILED_ERROR,
    "Wrong hash"
  );

  await testInstallAddon(url, { id, version, type }, null, "Valid add-on");

  await testInstallAddon(
    url,
    { id, version, type, hash },
    null,
    "Valid add-on, with hash"
  );
});

// Test _installAddon installs add-ons correctly, but only when the served XPI's
// details actually match the advertised details for the locale/dictionary.
add_task(async function test_installAddon_xpi_details_validated() {
  // Correct French dictionary details.
  const frenchDictionary = DICTIONARIES[0];
  const {
    guid: id,
    current_version: {
      version,
      files: [{ url }],
    },
    type,
  } = frenchDictionary;

  // The install should work initially.
  await testInstallAddon(url, { id, version, type }, null, "Valid add-on");

  // Install should fail when the XPI details don't match.
  await setupDictionaryAddon({
    ...frenchDictionary,
    guid: "wrong@mozilla.org",
  });
  await testInstallAddon(
    url,
    { id, version, type },
    DOWNLOAD_FAILED_ERROR,
    "Wrong ID in XPI"
  );

  await setupDictionaryAddon({
    ...frenchDictionary,
    current_version: {
      ...frenchDictionary.current_version,
      version: "99.0",
    },
  });
  await testInstallAddon(
    url,
    { id, version, type },
    DOWNLOAD_FAILED_ERROR,
    "Wrong version in XPI"
  );

  await setupLanguagePackAddon({
    ...LANGUAGE_PACKS[0],
    guid: id,
    current_compatible_version: frenchDictionary.current_version,
  });
  await testInstallAddon(
    url,
    { id, version, type },
    DOWNLOAD_FAILED_ERROR,
    "Wrong type in XPI"
  );

  // Install should work again after the XPI is fixed.
  await setupDictionaryAddon(frenchDictionary);
  await testInstallAddon(
    url,
    { id, version, type },
    null,
    "Valid add-on, restored"
  );
});

// Test that installLangPack fails correctly where there is no matching locale
// available, or when the details are incorrect, or are missing.
add_task(async function test_installLangPack_failure() {
  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-wrong.xpi`,
      id: "wrong@tests.mozilla.org",
      version: "1.0",
      target_locale: "wrong",
    },
    [],
    "No matching language pack"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/dictionary-fr-one.xpi`,
      id: "fr-one@tests.mozilla.org",
      version: "3.0",
      target_locale: "fr",
    },
    [],
    "Wrong type (dictionary)"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "langpack-de@tests.mozilla.org",
      version: "1.0",
      target_locale: "de",
    },
    [],
    "Wrong version"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "wrong@tests.mozilla.org",
      version: "2.0",
      target_locale: "de",
    },
    [],
    "Wrong ID"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      version: "2.0",
      target_locale: "de",
    },
    [],
    "Missing ID"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "langpack-de@tests.mozilla.org",
      target_locale: "de",
    },
    [],
    "Missing version"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "langpack-de@tests.mozilla.org",
      version: "2.0",
      target_locale: "de",
      hash: "wrong",
    },
    [],
    "Wrong hash"
  );
});

// Test that installLangPack works correctly when installing valid language
// packs and dictionaries.
add_task(async function test_installLangPack_success() {
  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "langpack-de@tests.mozilla.org",
      version: "2.0",
      target_locale: "de",
    },
    ["langpack-de@tests.mozilla.org"],
    "Language pack with no dictionaries"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-de.xpi`,
      id: "langpack-de@tests.mozilla.org",
      version: "2.0",
      target_locale: "de",
      hash: LANGUAGE_PACKS[1].current_compatible_version.files[0].hash,
    },
    ["langpack-de@tests.mozilla.org"],
    "Language pack with no dictionaries (with hash)"
  );

  await testInstallLangPack(
    {
      url: `http://${TEST_HOST}/addons/langpack-fr.xpi`,
      id: "langpack-fr@tests.mozilla.org",
      version: "1.0",
      target_locale: "fr",
    },
    [
      "langpack-fr@tests.mozilla.org",
      "fr-one@tests.mozilla.org",
      "fr-two@tests.mozilla.org",
    ],
    "Language pack with two dictionaries"
  );
});
