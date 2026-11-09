/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UrlbarProviderSearchSuggestions } = ChromeUtils.importESModule(
  "moz-src:///browser/components/urlbar/UrlbarProviderSearchSuggestions.sys.mjs"
);

const KEYWORD_ENABLED = "keyword.enabled";
const SUGGEST_ENABLED = "browser.search.suggest.enabled";
const URLBAR_SUGGEST = "browser.urlbar.suggest.searches";

add_setup(async function () {
  await SearchService.init();
});

add_task(async function test_allowRemoteSuggestions() {
  let suggestionsProvider = new UrlbarProviderSearchSuggestions();

  let context = createContext("bacon", {
    isPrivate: false,
    sapName: "urlbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowRemoteSuggestions(context),
    "Remote suggestions should be enabled with keyword enabled"
  );

  info("Setting " + KEYWORD_ENABLED + "=false");
  Services.prefs.setBoolPref(KEYWORD_ENABLED, false);

  context = createContext("bacon", {
    isPrivate: false,
    sapName: "urlbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    !suggestionsProvider._allowRemoteSuggestions(context),
    "Remote suggestions should be disabled with keyword disabled"
  );

  context = createContext("bacon", {
    isPrivate: false,
    sapName: "searchbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowRemoteSuggestions(context),
    "Remote suggestions should still be enabled on searchbar"
  );

  Services.prefs.clearUserPref(KEYWORD_ENABLED);
});

add_task(async function test_allowSuggestions() {
  let suggestionsProvider = new UrlbarProviderSearchSuggestions();

  let context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "urlbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowSuggestions(context),
    "Suggestions in the urlbar should be enabled by default"
  );

  context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "searchbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowSuggestions(context),
    "Suggestions in the searchbar should be enabled by default"
  );

  info("Setting " + URLBAR_SUGGEST + "=false");
  Services.prefs.setBoolPref(URLBAR_SUGGEST, false);

  context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "urlbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    !suggestionsProvider._allowSuggestions(context),
    "Suggestions in the urlbar should be disabled"
  );

  context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "searchbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowSuggestions(context),
    "Suggestions in the searchbar should still be enabled"
  );

  info("Setting " + SUGGEST_ENABLED + "=false");
  Services.prefs.setBoolPref(SUGGEST_ENABLED, false);

  context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "urlbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    !suggestionsProvider._allowSuggestions(context),
    "Suggestions in the urlbar should be disabled"
  );

  context = createContext("bacon eggs", {
    isPrivate: false,
    sapName: "searchbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowSuggestions(context),
    "Form history in the searchbar should still be enabled"
  );

  context = createContext("bacon eggs", {
    isPrivate: true,
    sapName: "searchbar",
    sources: [UrlbarUtils.RESULT_SOURCE.SEARCH],
  });
  Assert.ok(
    suggestionsProvider._allowSuggestions(context),
    "Form history in the searchbar should still be enabled in private windows"
  );

  Services.prefs.clearUserPref(SUGGEST_ENABLED);
  Services.prefs.clearUserPref(URLBAR_SUGGEST);
});
