/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests to ensure that the ignore list is handled correctly when loading
 * settings and when installing engines.
 */

"use strict";

const kSearchEngineID1 = "ignorelist_test_engine1";
const kSearchEngineID2 = "ignorelist_test_engine2";
const kSearchEngineID3 = "ignorelist_test_engine3";
const kSearchEngineURL1 =
  "https://example.com/?search={searchTerms}&ignore=true";
const kSearchEngineURL2 =
  "https://example.com/?search={searchTerms}&IGNORE=TRUE";
const kSearchEngineURL3 = "https://example.com/?search={searchTerms}";
const kExtensionID = "searchignore@mozilla.com";

add_setup(async () => {
  const settings = await RemoteSettings("hijack-blocklists");
  // Set up a couple of items in the ignore list.
  sinon.stub(settings, "get").returns([
    {
      id: "load-paths",
      matches: ["[addon]searchignore@mozilla.com"],
      _status: "synced",
    },
    {
      id: "submission-urls",
      matches: ["ignore=true", "opensearch=sng"],
      _status: "synced",
    },
  ]);

  SearchTestUtils.setRemoteSettingsConfig([{ identifier: "defaultEngine" }]);
  registerCleanupFunction(async () => {
    sinon.restore();
  });
});

add_task(async function test_ignoreListOnLoadSettings() {
  let finishListening = TestUtils.listenForConsoleMessages();

  Assert.ok(
    !SearchService.isInitialized,
    "Search service should not be initialized to begin with for this sub test"
  );

  let settingsTemplate = await readJSONFile(
    do_get_file("settings/ignorelist.json")
  );

  await promiseSaveSettingsData(settingsTemplate);

  let ignoreListUpdateCompleted = SearchTestUtils.promiseSearchNotification(
    "settings-update-complete"
  );

  await SearchService.init();

  await ignoreListUpdateCompleted;

  Assert.ok(
    !SearchService.getEngineByName("Test search engine"),
    "Should not have installed the add-on engine from settings"
  );

  Assert.ok(
    !SearchService.getEngineByName("OpenSearchTest"),
    "Should not have installed the OpenSearch engine from settings"
  );

  Assert.deepEqual(
    (await SearchService.getEngines()).map(e => e.id),
    ["defaultEngine"],
    "Should have correctly started and installed only the default engine"
  );

  assertPreference(
    "OpenSearchTest",
    "submission url",
    "https://www.example.com/test?search_query=&opensearch=sng"
  );

  let consoleMessages = await finishListening();
  Assert.deepEqual(
    consoleMessages
      .filter(msg => msg.level == "warn")
      .map(msg => {
        return {
          level: msg.level,
          arguments: msg.arguments,
        };
      }),
    [
      {
        level: "warn",
        arguments: [
          "Search engine",
          "Test search engine",
          "matches submission url",
          "https://www.example.com/search?q=&ignore=true&channel=sng",
        ],
      },
      {
        level: "warn",
        arguments: [
          "Search engine",
          "OpenSearchTest",
          "matches submission url",
          "https://www.example.com/test?search_query=&opensearch=sng",
        ],
      },
    ],
    "Should log console warnings for both search engines"
  );
});

add_task(async function test_ignoreListOnInstall() {
  Assert.ok(
    SearchService.isInitialized,
    "Search service should have been initialized to begin with for this sub test"
  );

  await SearchTestUtils.installSearchExtension({
    name: kSearchEngineID1,
    search_url: kSearchEngineURL1,
    search_url_get_params: "",
  });

  let engine = SearchService.getEngineByName(kSearchEngineID1);
  Assert.equal(
    engine,
    null,
    "Engine with ignored search params should not be added"
  );

  assertPreference(
    kSearchEngineID1,
    "submission url",
    kSearchEngineURL1.replace("{searchTerms}", "")
  );

  await SearchTestUtils.installSearchExtension({
    name: kSearchEngineID2,
    search_url: kSearchEngineURL2,
    search_url_get_params: "",
  });

  // An ignored engine shouldn't be available at all
  engine = SearchService.getEngineByName(kSearchEngineID2);
  Assert.equal(
    engine,
    null,
    "Engine with ignored search params of a different case should not exist"
  );

  assertPreference(
    kSearchEngineID2,
    "submission url",
    kSearchEngineURL2.replace("{searchTerms}", "").toLowerCase()
  );

  let finishListening = TestUtils.listenForConsoleMessages();

  await SearchTestUtils.installSearchExtension({
    id: kExtensionID,
    name: kSearchEngineID3,
    search_url: kSearchEngineURL3,
    search_url_get_params: "",
  });

  // An ignored engine shouldn't be available at all
  engine = SearchService.getEngineByName(kSearchEngineID3);
  Assert.equal(
    engine,
    null,
    "Engine with ignored extension id should not exist"
  );

  assertPreference(kSearchEngineID3, "load path", `[addon]${kExtensionID}`);

  let consoleMessages = await finishListening();
  Assert.deepEqual(
    consoleMessages
      .filter(msg => msg.level == "warn")
      .map(msg => {
        return {
          level: msg.level,
          arguments: msg.arguments,
        };
      }),
    [
      {
        level: "warn",
        arguments: [
          "Search engine",
          kSearchEngineID3,
          "matches load path",
          `[addon]${kExtensionID}`,
        ],
      },
    ],
    "Should have logged a console message"
  );
});

/**
 * Asserts that the lastEngineIgnored preference is set correctly.
 *
 * @param {string} engineName
 * @param {string} ignoreListtype
 * @param {string} url
 */
function assertPreference(engineName, ignoreListtype, url) {
  let prefValue = Services.prefs.getCharPref(
    SearchUtils.BROWSER_SEARCH_PREF + "lastEngineIgnored"
  );
  let matchResult = prefValue.match(/\d+ (.*?) (?:(https.*|\[.*))/);
  Assert.deepEqual(
    matchResult.slice(1),
    [`Search engine matches ${ignoreListtype} ignore list`, url],
    "Should have set the preference to the last engine ignored"
  );

  Assert.equal(
    Services.prefs.getStringPref(
      SearchUtils.BROWSER_SEARCH_PREF + "lastEngineIgnored.name"
    ),
    engineName,
    "Should have set lastEngineIgnored.name to the blocked engine's name"
  );
}
