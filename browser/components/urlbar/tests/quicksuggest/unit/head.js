/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from ../../unit/head.js */
/* eslint-disable jsdoc/require-param */

ChromeUtils.defineESModuleGetters(this, {
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  SearchUtils: "resource://gre/modules/SearchUtils.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  UrlbarProviderAutofill: "resource:///modules/UrlbarProviderAutofill.sys.mjs",
  UrlbarProviderQuickSuggest:
    "resource:///modules/UrlbarProviderQuickSuggest.sys.mjs",
  UrlbarSearchUtils: "resource:///modules/UrlbarSearchUtils.sys.mjs",
});

add_setup(async function setUpQuickSuggestXpcshellTest() {
  // Initializing TelemetryEnvironment in an xpcshell environment requires
  // jumping through a bunch of hoops. Suggest's use of TelemetryEnvironment is
  // tested in browser tests, and there's no other necessary reason to wait for
  // TelemetryEnvironment initialization in xpcshell tests, so just skip it.
  QuickSuggest._testSkipTelemetryEnvironmentInit = true;
});

/**
 * Tests quick suggest prefs migrations.
 *
 * @param {object} options
 *   The options object.
 * @param {object} options.testOverrides
 *   An object that modifies how migration is performed. It has the following
 *   properties, and all are optional:
 *
 *   {number} migrationVersion
 *     Migration will stop at this version, so for example you can test
 *     migration only up to version 1 even when the current actual version is
 *     larger than 1.
 *   {object} defaultPrefs
 *     An object that maps pref names (relative to `browser.urlbar`) to
 *     default-branch values. These should be the default prefs for the given
 *     `migrationVersion` and will be set as defaults before migration occurs.
 *
 * @param {object} options.expectedPrefs
 *   The expected prefs after migration: `{ defaultBranch, userBranch }`
 *   Pref names should be relative to `browser.urlbar`.
 * @param {boolean} options.shouldEnable
 *   Whether Suggest should be enabled when migration occurs.
 * @param {object} [options.initialUserBranch]
 *   Prefs to set on the user branch before migration ocurs. Use these to
 *   simulate user actions like disabling prefs or opting in or out of the
 *   online modal. Pref names should be relative to `browser.urlbar`.
 */
async function doMigrateTest({
  testOverrides,
  expectedPrefs,
  shouldEnable = true,
  initialUserBranch = {},
}) {
  info(
    "Testing migration: " +
      JSON.stringify({
        testOverrides,
        initialUserBranch,
        shouldEnable,
        expectedPrefs,
      })
  );

  function setPref(branch, name, value) {
    switch (typeof value) {
      case "boolean":
        branch.setBoolPref(name, value);
        break;
      case "number":
        branch.setIntPref(name, value);
        break;
      case "string":
        branch.setCharPref(name, value);
        break;
      default:
        Assert.ok(
          false,
          `Pref type not handled for setPref: ${name} = ${value}`
        );
        break;
    }
  }

  function getPref(branch, name) {
    let type = typeof UrlbarPrefs.get(name);
    switch (type) {
      case "boolean":
        return branch.getBoolPref(name);
      case "number":
        return branch.getIntPref(name);
      case "string":
        return branch.getCharPref(name);
      default:
        Assert.ok(false, `Pref type not handled for getPref: ${name} ${type}`);
        break;
    }
    return null;
  }

  let defaultBranch = Services.prefs.getDefaultBranch("browser.urlbar.");
  let userBranch = Services.prefs.getBranch("browser.urlbar.");

  // Set initial prefs. `initialDefaultBranch` are firefox.js values, i.e.,
  // defaults immediately after startup, before Suggest init and migration.
  UrlbarPrefs.clear("quicksuggest.migrationVersion");
  let initialDefaultBranch = {
    "suggest.quicksuggest.nonsponsored": false,
    "suggest.quicksuggest.sponsored": false,
    "quicksuggest.dataCollection.enabled": false,
  };
  for (let name of Object.keys(initialDefaultBranch)) {
    userBranch.clearUserPref(name);
  }
  for (let [branch, prefs] of [
    [defaultBranch, initialDefaultBranch],
    [userBranch, initialUserBranch],
  ]) {
    for (let [name, value] of Object.entries(prefs)) {
      if (value !== undefined) {
        setPref(branch, name, value);
      }
    }
  }

  // Reinitialize Suggest and check prefs twice. The first time the migration
  // should happen, and the second time the migration should not happen and
  // all the prefs should stay the same.
  for (let i = 0; i < 2; i++) {
    info(`Reinitializing Suggest, i=${i}`);

    // Reinitialize Suggest.
    await QuickSuggest._test_reinit({
      ...testOverrides,
      shouldEnable,
    });

    // Check expected pref values. Store expected effective values as we go so
    // we can check them afterward. For a given pref, the expected effective
    // value is the user value, or if there's not a user value, the default
    // value.
    let expectedEffectivePrefs = {};
    let {
      defaultBranch: expectedDefaultBranch,
      userBranch: expectedUserBranch,
    } = expectedPrefs;
    expectedDefaultBranch = expectedDefaultBranch || {};
    expectedUserBranch = expectedUserBranch || {};
    for (let [branch, prefs, branchType] of [
      [defaultBranch, expectedDefaultBranch, "default"],
      [userBranch, expectedUserBranch, "user"],
    ]) {
      let entries = Object.entries(prefs);
      if (!entries.length) {
        continue;
      }

      info(
        `Checking expected prefs on ${branchType} branch after Suggest init`
      );
      for (let [name, value] of entries) {
        expectedEffectivePrefs[name] = value;
        if (branch == userBranch) {
          Assert.ok(
            userBranch.prefHasUserValue(name),
            `Pref ${name} is on user branch`
          );
        }
        Assert.equal(
          getPref(branch, name),
          value,
          `Pref ${name} value on ${branchType} branch`
        );
      }
    }

    info(
      `Making sure prefs on the default branch without expected user-branch values are not on the user branch`
    );
    for (let name of Object.keys(initialDefaultBranch)) {
      if (!expectedUserBranch.hasOwnProperty(name)) {
        Assert.ok(
          !userBranch.prefHasUserValue(name),
          `Pref ${name} is not on user branch`
        );
      }
    }

    info(`Checking expected effective prefs`);
    for (let [name, value] of Object.entries(expectedEffectivePrefs)) {
      Assert.equal(
        UrlbarPrefs.get(name),
        value,
        `Pref ${name} effective value`
      );
    }

    let currentVersion =
      testOverrides?.migrationVersion === undefined
        ? QuickSuggest.MIGRATION_VERSION
        : testOverrides.migrationVersion;
    Assert.equal(
      UrlbarPrefs.get("quicksuggest.migrationVersion"),
      currentVersion,
      "quicksuggest.migrationVersion is correct after migration"
    );
  }

  // Clean up.
  UrlbarPrefs.clear("quicksuggest.migrationVersion");
  let userBranchNames = [
    ...Object.keys(initialUserBranch),
    ...Object.keys(expectedPrefs.userBranch || {}),
  ];
  for (let name of userBranchNames) {
    userBranch.clearUserPref(name);
  }
}

/**
 * Does a test that dismisses a single result by triggering a command on it.
 *
 * @param {object} options
 *   Options object.
 * @param {SuggestFeature} options.feature
 *   The feature that provides the dismissed result.
 * @param {UrlbarResult} options.result
 *   The result to trigger the command on.
 * @param {string} options.command
 *   The name of the command to trigger. It should dismiss one result.
 * @param {Array} options.queriesForDismissals
 *   Array of objects: `{ query, expectedResults }`
 *   For each object, the test will perform a search with `query` as the search
 *   string. After dismissing the result, the query shouldn't match any results.
 *   After clearing dismissals, the query should match the results in
 *   `expectedResults`. If `expectedResults` is omitted, `[result]` will be
 *   used.
 * @param {Array} options.queriesForOthers
 *   Array of objects: `{ query, expectedResults }`
 *   For each object, the test will perform a search with `query` as the search
 *   string. The query should always match `expectedResults`.
 */
async function doDismissOneTest({
  feature,
  result,
  command,
  queriesForDismissals,
  queriesForOthers,
}) {
  await QuickSuggest.clearDismissedSuggestions();
  await QuickSuggestTestUtils.forceSync();
  Assert.ok(
    !(await QuickSuggest.canClearDismissedSuggestions()),
    "Sanity check: canClearDismissedSuggestions should return false initially"
  );

  let changedPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );

  triggerCommand({
    result,
    command,
    feature,
    expectedCountsByCall: {
      removeResult: 1,
    },
  });

  info("Awaiting dismissals-changed promise");
  await changedPromise;

  Assert.ok(
    await QuickSuggest.canClearDismissedSuggestions(),
    "canClearDismissedSuggestions should return true after triggering command"
  );
  Assert.ok(
    await QuickSuggest.isResultDismissed(result),
    "The result should be dismissed"
  );

  for (let { query } of queriesForDismissals) {
    info("Doing search for dismissed suggestions: " + JSON.stringify(query));
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });
  }

  for (let { query, expectedResults } of queriesForOthers) {
    info(
      "Doing search for non-dismissed suggestions: " + JSON.stringify(query)
    );
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expectedResults,
    });
  }

  let clearedPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-cleared"
  );

  info("Clearing dismissals");
  await QuickSuggest.clearDismissedSuggestions();

  // It's not necessary to await this -- awaiting `clearDismissedSuggestions()`
  // is sufficient -- but we do it to make sure the notification is sent.
  info("Awaiting dismissals-cleared promise");
  await clearedPromise;

  Assert.ok(
    !(await QuickSuggest.canClearDismissedSuggestions()),
    "canClearDismissedSuggestions should return false after clearing dismissals"
  );

  for (let { query, expectedResults = [result] } of queriesForDismissals) {
    info("Doing search after clearing dismissals: " + JSON.stringify(query));
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expectedResults,
    });
  }
}

/**
 * Does a test that dismisses a suggestion type (i.e., all suggestions of a
 * certain type) by triggering a command on a result.
 *
 * @param {object} options
 *   Options object.
 * @param {SuggestFeature} options.feature
 *   The feature that provides the suggestion type.
 * @param {UrlbarResult} options.result
 *   The result to trigger the command on.
 * @param {string} options.command
 *   The name of the command to trigger. It should dismiss all results of a
 *   suggestion type.
 * @param {string} options.pref
 *   The name of the user-controlled pref (relative to `browser.urlbar.`) that
 *   controls the suggestion type. Should be the same as
 *   `feature.primaryUserControlledPreference`.
 * @param {Array} options.queries
 *   Array of objects: `{ query, expectedResults }`
 *   For each object, the test will perform a search with `query` as the search
 *   string. After dismissing the suggestion type, the query shouldn't match any
 *   results. After clearing dismissals, the query should match the results in
 *   `expectedResults`. If `expectedResults` is omitted, `[result]` will be
 *   used.
 */
async function doDismissAllTest({ feature, result, command, pref, queries }) {
  await QuickSuggest.clearDismissedSuggestions();
  await QuickSuggestTestUtils.forceSync();
  Assert.ok(
    !(await QuickSuggest.canClearDismissedSuggestions()),
    "Sanity check: canClearDismissedSuggestions should return false initially"
  );

  let changedPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-changed"
  );

  triggerCommand({
    result,
    command,
    feature,
    expectedCountsByCall: {
      removeResult: 1,
    },
  });

  info("Awaiting dismissals-changed promise");
  await changedPromise;

  Assert.ok(
    await QuickSuggest.canClearDismissedSuggestions(),
    "canClearDismissedSuggestions should return true after triggering command"
  );
  Assert.ok(
    !UrlbarPrefs.get(pref),
    "Pref should be false after triggering command: " + pref
  );

  for (let { query } of queries) {
    info("Doing search after triggering command: " + JSON.stringify(query));
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: [],
    });
  }

  let clearedPromise = TestUtils.topicObserved(
    "quicksuggest-dismissals-cleared"
  );

  info("Clearing dismissals");
  await QuickSuggest.clearDismissedSuggestions();

  // It's not necessary to await this -- awaiting `clearDismissedSuggestions()`
  // is sufficient -- but we do it to make sure the notification is sent.
  info("Awaiting dismissals-cleared promise");
  await clearedPromise;

  Assert.ok(
    !(await QuickSuggest.canClearDismissedSuggestions()),
    "canClearDismissedSuggestions should return false after clearing dismissals"
  );
  Assert.ok(
    UrlbarPrefs.get(pref),
    "Pref should be true after clearing it: " + pref
  );

  // Clearing the pref will trigger a sync, so wait for it.
  await QuickSuggestTestUtils.forceSync();

  for (let { query, expectedResults = [result] } of queries) {
    info("Doing search after clearing dismissals: " + JSON.stringify(query));
    await check_results({
      context: createContext(query, {
        providers: [UrlbarProviderQuickSuggest.name],
        isPrivate: false,
      }),
      matches: expectedResults,
    });
  }
}

/**
 * Does some "show less frequently" tests where the cap is set in remote
 * settings and Nimbus. See `doOneShowLessFrequentlyTest()`. This function
 * assumes the matching behavior implemented by the given `SuggestFeature` is
 * based on matching prefixes of the given keyword starting at the first word.
 * It also assumes the `SuggestFeature` provides suggestions in remote settings.
 *
 * @param {object} options
 *   Options object.
 * @param {SuggestFeature} options.feature
 *   The feature that provides the suggestion matched by the searches.
 * @param {*} options.expectedResult
 *   The expected result that should be matched, for searches that are expected
 *   to match a result. Can also be a function; it's passed the current search
 *   string and it should return the expected result.
 * @param {string} options.showLessFrequentlyCountPref
 *   The name of the pref that stores the "show less frequently" count being
 *   tested.
 * @param {string} options.nimbusCapVariable
 *   The name of the Nimbus variable that stores the "show less frequently" cap
 *   being tested.
 * @param {object} options.keyword
 *   The primary keyword to use during the test.
 * @param {number} options.keywordBaseIndex
 *   The index in `keyword` to base substring checks around. This function will
 *   test substrings starting at the beginning of keyword and ending at the
 *   following indexes: one index before `keywordBaseIndex`,
 *   `keywordBaseIndex`, `keywordBaseIndex` + 1, `keywordBaseIndex` + 2, and
 *   `keywordBaseIndex` + 3.
 */
async function doShowLessFrequentlyTests({
  feature,
  expectedResult,
  showLessFrequentlyCountPref,
  nimbusCapVariable,
  keyword,
  keywordBaseIndex = keyword.indexOf(" "),
}) {
  // Do some sanity checks on the keyword. Any checks that fail are errors in
  // the test.
  if (keywordBaseIndex <= 0) {
    throw new Error(
      "keywordBaseIndex must be > 0, but it's " + keywordBaseIndex
    );
  }
  if (keyword.length < keywordBaseIndex + 3) {
    throw new Error(
      "keyword must have at least two chars after keywordBaseIndex"
    );
  }

  let tests = [
    {
      showLessFrequentlyCount: 0,
      canShowLessFrequently: true,
      newSearches: {
        [keyword.substring(0, keywordBaseIndex - 1)]: false,
        [keyword.substring(0, keywordBaseIndex)]: true,
        [keyword.substring(0, keywordBaseIndex + 1)]: true,
        [keyword.substring(0, keywordBaseIndex + 2)]: true,
        [keyword.substring(0, keywordBaseIndex + 3)]: true,
      },
    },
    {
      showLessFrequentlyCount: 1,
      canShowLessFrequently: true,
      newSearches: {
        [keyword.substring(0, keywordBaseIndex)]: false,
      },
    },
    {
      showLessFrequentlyCount: 2,
      canShowLessFrequently: true,
      newSearches: {
        [keyword.substring(0, keywordBaseIndex + 1)]: false,
      },
    },
    {
      showLessFrequentlyCount: 3,
      canShowLessFrequently: false,
      newSearches: {
        [keyword.substring(0, keywordBaseIndex + 2)]: false,
      },
    },
    {
      showLessFrequentlyCount: 3,
      canShowLessFrequently: false,
      newSearches: {},
    },
  ];

  info("Testing 'show less frequently' with cap in remote settings");
  await doOneShowLessFrequentlyTest({
    tests,
    feature,
    expectedResult,
    showLessFrequentlyCountPref,
    rs: {
      show_less_frequently_cap: 3,
    },
  });

  // Nimbus should override remote settings.
  info("Testing 'show less frequently' with cap in Nimbus and remote settings");
  await doOneShowLessFrequentlyTest({
    tests,
    feature,
    expectedResult,
    showLessFrequentlyCountPref,
    rs: {
      show_less_frequently_cap: 10,
    },
    nimbus: {
      [nimbusCapVariable]: 3,
    },
  });
}

/**
 * Does a group of searches, increments the "show less frequently" count, and
 * repeats until all groups are done. The cap can be set by remote settings
 * config and/or Nimbus.
 *
 * @param {object} options
 *   Options object.
 * @param {SuggestFeature} options.feature
 *   The feature that provides the suggestion matched by the searches.
 * @param {*} options.expectedResult
 *   The expected result that should be matched, for searches that are expected
 *   to match a result. Can also be a function; it's passed the current search
 *   string and it should return the expected result.
 * @param {string} options.showLessFrequentlyCountPref
 *   The name of the pref that stores the "show less frequently" count being
 *   tested.
 * @param {object} options.tests
 *   An array where each item describes a group of new searches to perform and
 *   expected state. Each item should look like this:
 *   `{ showLessFrequentlyCount, canShowLessFrequently, newSearches }`
 *
 *   {number} showLessFrequentlyCount
 *     The expected value of `showLessFrequentlyCount` before the group of
 *     searches is performed.
 *   {boolean} canShowLessFrequently
 *     The expected value of `canShowLessFrequently` before the group of
 *     searches is performed.
 *   {object} newSearches
 *     An object that maps each search string to a boolean that indicates
 *     whether the first remote settings suggestion should be triggered by the
 *     search string. Searches are cumulative: The intended use is to pass a
 *     large initial group of searches in the first search group, and then each
 *     following `newSearches` is a diff against the previous.
 * @param {object} options.rs
 *   The remote settings config to set.
 * @param {object} options.nimbus
 *   The Nimbus variables to set.
 */
async function doOneShowLessFrequentlyTest({
  feature,
  expectedResult,
  showLessFrequentlyCountPref,
  tests,
  rs = {},
  nimbus = {},
}) {
  // Disable Merino so we trigger only remote settings suggestions. The
  // `SuggestFeature` is expected to add remote settings suggestions using
  // keywords start starting with the first word in each full keyword, but the
  // mock Merino server will always return whatever suggestion it's told to
  // return regardless of the search string. That means Merino will return a
  // suggestion for a keyword that's smaller than the first full word.
  UrlbarPrefs.set("quicksuggest.dataCollection.enabled", false);

  // Set Nimbus variables and RS config.
  let cleanUpNimbus = await UrlbarTestUtils.initNimbusFeature(nimbus);
  await QuickSuggestTestUtils.withConfig({
    config: rs,
    callback: async () => {
      let cumulativeSearches = {};

      for (let {
        showLessFrequentlyCount,
        canShowLessFrequently,
        newSearches,
      } of tests) {
        info(
          "Starting subtest: " +
            JSON.stringify({
              showLessFrequentlyCount,
              canShowLessFrequently,
              newSearches,
            })
        );

        Assert.equal(
          feature.showLessFrequentlyCount,
          showLessFrequentlyCount,
          "showLessFrequentlyCount should be correct initially"
        );
        Assert.equal(
          UrlbarPrefs.get(showLessFrequentlyCountPref),
          showLessFrequentlyCount,
          "Pref should be correct initially"
        );
        Assert.equal(
          feature.canShowLessFrequently,
          canShowLessFrequently,
          "canShowLessFrequently should be correct initially"
        );

        // Merge the current `newSearches` object into the cumulative object.
        cumulativeSearches = {
          ...cumulativeSearches,
          ...newSearches,
        };

        for (let [searchString, isExpected] of Object.entries(
          cumulativeSearches
        )) {
          info("Doing search: " + JSON.stringify({ searchString, isExpected }));

          let results = [];
          if (isExpected) {
            results.push(
              typeof expectedResult == "function"
                ? expectedResult(searchString)
                : expectedResult
            );
          }

          await check_results({
            context: createContext(searchString, {
              providers: [UrlbarProviderQuickSuggest.name],
              isPrivate: false,
            }),
            matches: results,
          });
        }

        feature.incrementShowLessFrequentlyCount();
      }
    },
  });

  await cleanUpNimbus();
  UrlbarPrefs.clear(showLessFrequentlyCountPref);
  UrlbarPrefs.set("quicksuggest.dataCollection.enabled", true);
}

/**
 * Queries the Rust component directly and checks the returned suggestions. The
 * point is to make sure the Rust backend passes the correct providers to the
 * Rust component depending on the types of enabled suggestions. Assuming the
 * Rust component isn't buggy, it should return suggestions only for the
 * passed-in providers.
 *
 * @param {object} options
 *   Options object
 * @param {string} options.searchString
 *   The search string.
 * @param {Array} options.tests
 *   Array of test objects: `{ prefs, expectedUrls }`
 *
 *   For each object, the given prefs are set, the Rust component is queried
 *   using the given search string, and the URLs of the returned suggestions are
 *   compared to the given expected URLs (order doesn't matter).
 *
 *   {object} prefs
 *     An object mapping pref names (relative to `browser.urlbar`) to values.
 *     These prefs will be set before querying and should be used to enable or
 *     disable particular types of suggestions.
 *   {Array} expectedUrls
 *     An array of the URLs of the suggestions that are expected to be returned.
 *     The order doesn't matter.
 */
async function doRustProvidersTests({ searchString, tests }) {
  for (let { prefs, expectedUrls } of tests) {
    info(
      "Starting Rust providers test: " + JSON.stringify({ prefs, expectedUrls })
    );

    info("Setting prefs and forcing sync");
    for (let [name, value] of Object.entries(prefs)) {
      UrlbarPrefs.set(name, value);
    }
    await QuickSuggestTestUtils.forceSync();

    info("Querying with search string: " + JSON.stringify(searchString));
    let suggestions = await QuickSuggest.rustBackend.query(searchString);
    info("Got suggestions: " + JSON.stringify(suggestions));

    Assert.deepEqual(
      suggestions.map(s => s.url).sort(),
      expectedUrls.sort(),
      "query() should return the expected suggestions (by URL)"
    );

    info("Clearing prefs and forcing sync");
    for (let name of Object.keys(prefs)) {
      UrlbarPrefs.clear(name);
    }
    await QuickSuggestTestUtils.forceSync();
  }
}

/**
 * Simulates performing a command for a feature by calling its `onEngagement()`.
 *
 * @param {object} options
 *   Options object.
 * @param {SuggestFeature} options.feature
 *   The feature whose command will be triggered.
 * @param {string} options.command
 *   The name of the command to trigger.
 * @param {UrlbarResult} options.result
 *   The result that the command will act on.
 * @param {string} options.searchString
 *   The search string to pass to `onEngagement()`.
 * @param {object} options.expectedCountsByCall
 *   If non-null, this should map controller and view method names to the number
 *   of times they should be called in response to the command.
 * @returns {Map}
 *   A map from names of methods on the controller and view to the number of
 *   times they were called.
 */
function triggerCommand({
  feature,
  command,
  result,
  searchString = "",
  expectedCountsByCall = null,
}) {
  info(`Calling ${feature.name}.onEngagement() to trigger command: ${command}`);

  let countsByCall = new Map();
  let addCall = name => {
    if (!countsByCall.has(name)) {
      countsByCall.set(name, 0);
    }
    countsByCall.set(name, countsByCall.get(name) + 1);
  };

  feature.onEngagement(
    // query context
    {},
    // controller
    {
      removeResult() {
        addCall("removeResult");
      },
      view: {
        acknowledgeFeedback() {
          addCall("acknowledgeFeedback");
        },
        invalidateResultMenuCommands() {
          addCall("invalidateResultMenuCommands");
        },
      },
    },
    // details
    { result, selType: command },
    searchString
  );

  if (expectedCountsByCall) {
    for (let [name, expectedCount] of Object.entries(expectedCountsByCall)) {
      Assert.equal(
        countsByCall.get(name) ?? 0,
        expectedCount,
        "Function should have been called the expected number of times: " + name
      );
    }
  }

  return countsByCall;
}
