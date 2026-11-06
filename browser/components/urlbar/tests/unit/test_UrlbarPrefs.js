/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(function test() {
  Assert.throws(
    () => UrlbarPrefs.get("browser.migration.version"),
    /Trying to access an unknown pref/,
    "Should throw when passing an untracked pref"
  );

  Assert.throws(
    () => UrlbarPrefs.set("browser.migration.version", 100),
    /Trying to access an unknown pref/,
    "Should throw when passing an untracked pref"
  );
  Assert.throws(
    () => UrlbarPrefs.set("maxRichResults", "10"),
    /Invalid value/,
    "Should throw when passing an invalid value type"
  );

  Assert.deepEqual(UrlbarPrefs.get("formatting.enabled"), true);
  UrlbarPrefs.set("formatting.enabled", false);
  Assert.deepEqual(UrlbarPrefs.get("formatting.enabled"), false);

  Assert.deepEqual(UrlbarPrefs.get("maxRichResults"), 10);
  UrlbarPrefs.set("maxRichResults", 6);
  Assert.deepEqual(UrlbarPrefs.get("maxRichResults"), 6);

  Assert.deepEqual(UrlbarPrefs.get("intentThreshold"), 0.5);
  UrlbarPrefs.set("intentThreshold", 0.75);
  // Due to rounding errors, floats are slightly imprecise, so we can't
  // directly compare what we set to what we retrieve.
  Assert.deepEqual(
    parseFloat(UrlbarPrefs.get("intentThreshold").toFixed(2)),
    0.75
  );

  UrlbarPrefs.set("placeholderName", "百度");
  Assert.equal(
    UrlbarPrefs.get("placeholderName"),
    "百度",
    "UTF-8 prefs are read corrently"
  );
  Services.prefs.clearUserPref("browser.urlbar.placeholderName");
});

const EXPECTED_SUGGESTIONS_FIRST_GROUPS = {
  children: [
    // heuristic
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_SEARCH_TIP },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE },
        {
          group: UrlbarShared.RESULT_GROUP.HEURISTIC_RESTRICT_KEYWORD_AUTOFILL,
        },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
      ],
    },
    // extensions using the omnibox API
    {
      group: UrlbarShared.RESULT_GROUP.OMNIBOX,
    },
    // main group
    {
      flexChildren: true,
      children: [
        // suggestions
        {
          flex: 2,
          children: [
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.TEMPORAL_SUGGESTION,
                },
                {
                  flex: 4,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
            },
          ],
        },
        // general
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          flex: 1,
          children: [
            {
              availableSpan: 3,
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 1,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_TAB,
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.GENERAL,
                  orderBy: "frecency",
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
          ],
        },
      ],
    },
  ],
};

const EXPECTED_NOT_SUGGESTIONS_FIRST_GROUPS = {
  children: [
    // heuristic
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_SEARCH_TIP },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE },
        {
          group: UrlbarShared.RESULT_GROUP.HEURISTIC_RESTRICT_KEYWORD_AUTOFILL,
        },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
      ],
    },
    // extensions using the omnibox API
    {
      group: UrlbarShared.RESULT_GROUP.OMNIBOX,
    },
    // main group
    {
      flexChildren: true,
      children: [
        // general
        {
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          flex: 2,
          children: [
            {
              availableSpan: 3,
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 1,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_TAB,
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.GENERAL,
                  orderBy: "frecency",
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
          ],
        },
        // suggestions
        {
          flex: 1,
          children: [
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.TEMPORAL_SUGGESTION,
                },
                {
                  flex: 4,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
            },
          ],
        },
      ],
    },
  ],
};

const EXPECTED_SMARTBAR_SUGGESTIONS_FIRST_GROUPS = {
  children: [
    // heuristic
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AI_CHAT },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
      ],
    },
    // main group
    {
      flexChildren: true,
      children: [
        // suggestions
        {
          flex: 2,
          children: [
            {
              availableSpan: 2,
              group: UrlbarShared.RESULT_GROUP.AI,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
                },
                {
                  flex: 4,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
            },
          ],
        },
        // general
        {
          flex: 1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          children: [
            {
              availableSpan: 3,
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.GENERAL,
                  orderBy: "frecency",
                },
                {
                  flex: 1,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_TAB,
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
          ],
        },
      ],
    },
  ],
};

const EXPECTED_SMARTBAR_NOT_SUGGESTIONS_FIRST_GROUPS = {
  children: [
    // heuristic
    {
      maxResultCount: 1,
      children: [
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_AI_CHAT },
        { group: UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
      ],
    },
    // main group
    {
      flexChildren: true,
      children: [
        // general
        {
          flex: 1,
          group: UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
          children: [
            {
              availableSpan: 3,
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.GENERAL,
                  orderBy: "frecency",
                },
                {
                  flex: 1,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_TAB,
                },
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
            },
          ],
        },
        // suggestions
        {
          flex: 2,
          children: [
            {
              availableSpan: 2,
              group: UrlbarShared.RESULT_GROUP.AI,
            },
            {
              flexChildren: true,
              children: [
                {
                  flex: 2,
                  group: UrlbarShared.RESULT_GROUP.FORM_HISTORY,
                },
                {
                  flex: 99,
                  group: UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
                },
                {
                  flex: 4,
                  group: UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
                },
              ],
            },
            {
              group: UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
            },
          ],
        },
      ],
    },
  ],
};

// Tests interaction between showSearchSuggestionsFirst and resultGroups.
add_task(function showSearchSuggestionsFirst_resultGroups() {
  let context = createContext("test", { sapName: "urlbar" });

  // Check initial values.
  Assert.equal(
    UrlbarPrefs.get("showSearchSuggestionsFirst"),
    true,
    "showSearchSuggestionsFirst is true initially"
  );
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SUGGESTIONS_FIRST_GROUPS,
    "resultGroups is the same as the groups for which showSearchSuggestionsFirst is true"
  );

  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_NOT_SUGGESTIONS_FIRST_GROUPS,
    "resultGroups is updated after setting showSearchSuggestionsFirst = false"
  );

  UrlbarPrefs.set("showSearchSuggestionsFirst", true);
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SUGGESTIONS_FIRST_GROUPS,
    "resultGroups is updated after setting showSearchSuggestionsFirst = true"
  );

  // Set showSearchSuggestionsFirst = false again so we can clear it next.
  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_NOT_SUGGESTIONS_FIRST_GROUPS,
    "resultGroups is updated after setting showSearchSuggestionsFirst = false"
  );

  // Clear showSearchSuggestionsFirst.
  Services.prefs.clearUserPref("browser.urlbar.showSearchSuggestionsFirst");
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SUGGESTIONS_FIRST_GROUPS,
    "resultGroups is updated immediately after clearing showSearchSuggestionsFirst"
  );
  Assert.equal(
    UrlbarPrefs.get("showSearchSuggestionsFirst"),
    true,
    "showSearchSuggestionsFirst defaults to true after clearing it"
  );
});

// Tests interaction between showSearchSuggestionsFirst,
// smartbar.showSearchSuggestionsFirst and smartbar resultGroups.
add_task(function showSearchSuggestionsFirst_smartbar_resultGroups() {
  let context = createContext("test", { sapName: "smartbar" });

  Assert.equal(
    UrlbarPrefs.get("smartbar.showSearchSuggestionsFirst"),
    false,
    "smartbar.showSearchSuggestionsFirst is false by default"
  );
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SMARTBAR_NOT_SUGGESTIONS_FIRST_GROUPS,
    "smartbar resultGroups has general results first"
  );

  UrlbarPrefs.set("smartbar.showSearchSuggestionsFirst", true);
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SMARTBAR_SUGGESTIONS_FIRST_GROUPS,
    "smartbar resultGroups has suggestions first when smartbar.showSearchSuggestionsFirst is true"
  );

  UrlbarPrefs.set("showSearchSuggestionsFirst", false);
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SMARTBAR_SUGGESTIONS_FIRST_GROUPS,
    "smartbar resultGroups is unaffected by the pref showSearchSuggestionsFirst"
  );

  // Clear showSearchSuggestionsFirst prefs.
  Services.prefs.clearUserPref("browser.urlbar.showSearchSuggestionsFirst");
  Services.prefs.clearUserPref(
    "browser.urlbar.smartbar.showSearchSuggestionsFirst"
  );
  Assert.deepEqual(
    UrlbarPrefs.getResultGroups({
      context,
      showSearchSuggestionsFirst: context.showSearchSuggestionsFirst,
    }),
    EXPECTED_SMARTBAR_NOT_SUGGESTIONS_FIRST_GROUPS,
    "smartbar resultGroups is updated immediately after clearing prefs"
  );
  Assert.equal(
    UrlbarPrefs.get("showSearchSuggestionsFirst"),
    true,
    "showSearchSuggestionsFirst defaults to true after clearing it"
  );
  Assert.equal(
    UrlbarPrefs.get("smartbar.showSearchSuggestionsFirst"),
    false,
    "smartbar.showSearchSuggestionsFirst defaults to false after clearing it"
  );
});

// Tests whether observer.onNimbusChanged works.
add_task(async function onNimbusChanged() {
  Services.prefs.setBoolPref("browser.urlbar.addons.featureGate", false);

  // Add an observer that throws an Error and an observer that does not define
  // anything to check whether the other observers can get notifications.
  UrlbarPrefs.addObserver({
    onPrefChanged() {
      throw new Error("From onPrefChanged");
    },
    onNimbusChanged() {
      throw new Error("From onNimbusChanged");
    },
  });
  UrlbarPrefs.addObserver({});

  const observer = {
    onPrefChanged(pref) {
      this.prefChangedList.push(pref);
    },
    onNimbusChanged(pref) {
      this.nimbusChangedList.push(pref);
    },
  };
  observer.prefChangedList = [];
  observer.nimbusChangedList = [];
  UrlbarPrefs.addObserver(observer);

  const doCleanup = await UrlbarTestUtils.initNimbusFeature({
    addonsFeatureGate: true,
  });
  Assert.equal(observer.prefChangedList.length, 0);
  Assert.ok(observer.nimbusChangedList.includes("addonsFeatureGate"));
  await doCleanup();
});

// Tests whether observer.onPrefChanged works.
add_task(async function onPrefChanged() {
  const doCleanup = await UrlbarTestUtils.initNimbusFeature({
    addonsFeatureGate: false,
  });
  Services.prefs.setBoolPref("browser.urlbar.addons.featureGate", false);

  // Add an observer that throws an Error and an observer that does not define
  // anything to check whether the other observers can get notifications.
  UrlbarPrefs.addObserver({
    onPrefChanged() {
      throw new Error("From onPrefChanged");
    },
    onNimbusChanged() {
      throw new Error("From onNimbusChanged");
    },
  });
  UrlbarPrefs.addObserver({});

  const deferred = Promise.withResolvers();
  const observer = {
    onPrefChanged(pref) {
      this.prefChangedList.push(pref);
      deferred.resolve();
    },
    onNimbusChanged(pref) {
      this.nimbusChangedList.push(pref);
      deferred.resolve();
    },
  };
  observer.prefChangedList = [];
  observer.nimbusChangedList = [];
  UrlbarPrefs.addObserver(observer);

  Services.prefs.setBoolPref("browser.urlbar.addons.featureGate", true);
  await deferred.promise;
  Assert.equal(observer.prefChangedList.length, 1);
  Assert.equal(observer.prefChangedList[0], "addons.featureGate");
  Assert.equal(observer.nimbusChangedList.length, 0);

  Services.prefs.clearUserPref("browser.urlbar.addons.featureGate");
  await doCleanup();
});

// Tests add function.
add_task(async function add() {
  info("Start from empty value");
  Services.prefs.setStringPref(
    "browser.urlbar.quicksuggest.realtimeOptIn.notNowTypes",
    ""
  );
  let result = UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTypes");
  Assert.equal(result.size, 0);
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "a");
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "b");
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "a");
  result = UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTypes");
  Assert.equal(result.size, 2);
  Assert.ok(result.has("a"));
  Assert.ok(result.has("b"));

  info("Start from some values");
  Services.prefs.setStringPref(
    "browser.urlbar.quicksuggest.realtimeOptIn.notNowTypes",
    "a,b,c"
  );
  result = UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTypes");
  Assert.equal(result.size, 3);
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "a");
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "b");
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "a");
  UrlbarPrefs.add("quicksuggest.realtimeOptIn.notNowTypes", "d");
  result = UrlbarPrefs.get("quicksuggest.realtimeOptIn.notNowTypes");
  Assert.equal(result.size, 4);
  Assert.ok(result.has("a"));
  Assert.ok(result.has("b"));
  Assert.ok(result.has("c"));
  Assert.ok(result.has("d"));

  info("Test for singular value pref");
  Assert.throws(
    () => UrlbarPrefs.add("merino.providers", "a"),
    /The pref merino.providers should handle the values as Set but 'string'/
  );
  Assert.throws(
    () => UrlbarPrefs.add("addons.featureGate", true),
    /The pref addons.featureGate should handle the values as Set but 'boolean'/
  );
});

// Tests that the result groups put general results before search results and
// flex them 7:6 when `showTemporalSuggestions` is true.
add_task(function showTemporalSuggestions_resultGroups() {
  let context = createContext("", { sapName: "urlbar" });

  let rootGroup = UrlbarPrefs.getResultGroups({
    context,
    showTemporalSuggestions: true,
  });
  let mainGroup = rootGroup.children.at(-1);
  let [general, suggestions] = mainGroup.children;
  Assert.equal(
    general.group,
    UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
    "General results come first"
  );
  Assert.equal(general.flex, 7, "General results have flex 7");
  Assert.equal(suggestions.flex, 6, "Search results have flex 6");

  rootGroup = UrlbarPrefs.getResultGroups({ context });
  mainGroup = rootGroup.children.at(-1);
  [general, suggestions] = mainGroup.children;
  Assert.equal(
    general.group,
    UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
    "General results come first by default"
  );
  Assert.equal(general.flex, 2, "General results have flex 2 by default");
  Assert.equal(suggestions.flex, 1, "Search results have flex 1 by default");
});
