/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports the UrlbarPrefs singleton, which manages preferences for
 * the urlbar. It also provides access to urlbar Nimbus variables as if they are
 * preferences, but only for variables with fallback prefs.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
});

const PREF_URLBAR_BRANCH = "browser.urlbar.";

// Prefs are defined as [pref name, default value] or [pref name, [default
// value, type]].  In the former case, the getter method name is inferred from
// the typeof the default value.
//
// NOTE: Don't name prefs (relative to the `browser.urlbar` branch) the same as
// Nimbus urlbar features. Doing so would cause a name collision because pref
// names and Nimbus feature names are both kept as keys in UrlbarPref's map. For
// a list of Nimbus features, see toolkit/components/nimbus/FeatureManifest.yaml.
const PREF_URLBAR_DEFAULTS = new Map([
  // Whether we announce to screen readers when tab-to-search results are
  // inserted.
  ["accessibility.tabToSearch.announceResults", true],

  // Feature gate pref for addon suggestions in the urlbar.
  ["addons.featureGate", false],

  // The number of times the user has clicked the "Show less frequently" command
  // for addon suggestions.
  ["addons.showLessFrequentlyCount", 0],

  // "Autofill" is the name of the feature that automatically completes domains
  // and URLs that the user has visited as the user is typing them in the urlbar
  // textbox.  If false, autofill will be disabled.
  ["autoFill", true],

  // Whether enabling adaptive history autofill. This pref is a fallback for the
  // Nimbus variable `autoFillAdaptiveHistoryEnabled`.
  ["autoFill.adaptiveHistory.enabled", false],

  // Minimum char length of the user's search string to enable adaptive history
  // autofill. This pref is a fallback for the Nimbus variable
  // `autoFillAdaptiveHistoryMinCharsThreshold`.
  ["autoFill.adaptiveHistory.minCharsThreshold", 0],

  // Threshold for use count of input history that we handle as adaptive history
  // autofill. If the use count is this value or more, it will be a candidate.
  // Set the threshold to not be candidate the input history passed approximately
  // 30 days since user input it as the default.
  ["autoFill.adaptiveHistory.useCountThreshold", [0.47, "float"]],

  // Affects the frecency threshold of the autofill algorithm.  The threshold is
  // the mean of all origin frecencies plus one standard deviation multiplied by
  // this value.  See UrlbarProviderPlaces.
  ["autoFill.stddevMultiplier", [0.0, "float"]],

  // Feature gate pref for clipboard suggestions in the urlbar.
  ["clipboard.featureGate", false],

  // Whether to close other panels when the urlbar panel opens.
  // This feature gate exists just as an emergency rollback in case of
  // unexpected issues in Release. We normally want this behavior.
  ["closeOtherPanelsOnOpen", true],

  // Whether to show a link for using the search functionality provided by the
  // active view if the the view utilizes OpenSearch.
  ["contextualSearch.enabled", true],

  // Whether using `ctrl` when hitting return/enter in the URL bar
  // (or clicking 'go') should prefix 'www.' and suffix
  // browser.fixup.alternate.suffix to the URL bar value prior to
  // navigating.
  ["ctrlCanonizesURLs", true],

  // Whether copying the entire URL from the location bar will put a human
  // readable (percent-decoded) URL on the clipboard.
  ["decodeURLsOnCopy", false],

  // The amount of time (ms) to wait after the user has stopped typing before
  // fetching results.  However, we ignore this for the very first result (the
  // "heuristic" result).  We fetch it as fast as possible.
  ["delay", 50],

  // Ensure we use trailing dots for DNS lookups for single words that could
  // be hosts.
  ["dnsResolveFullyQualifiedNames", true],

  // Controls when to DNS resolve single word search strings, after they were
  // searched for. If the string is resolved as a valid host, show a
  // "Did you mean to go to 'host'" prompt.
  // 0 - never resolve; 1 - use heuristics (default); 2 - always resolve
  ["dnsResolveSingleWordsAfterSearch", 0],

  // Whether we expand the font size when when the urlbar is
  // focused.
  ["experimental.expandTextOnFocus", false],

  // Whether the heuristic result is hidden.
  ["experimental.hideHeuristic", false],

  // Comma-separated list of `source.providers` combinations, that are used to
  // determine if an exposure event should be fired. This can be set by a
  // Nimbus variable and is expected to be set via nimbus experiment
  // configuration.
  ["exposureResults", ""],

  // When we send events to (privileged) extensions (urlbar API), we wait this
  // amount of time in milliseconds for them to respond before timing out.
  ["extension.timeout", 400],

  // When we send events to extensions that use the omnibox API, we wait this
  // amount of time in milliseconds for them to respond before timing out.
  ["extension.omnibox.timeout", 3000],

  // Feature gate pref for Fakespot suggestions in the urlbar.
  ["fakespot.featureGate", false],

  // The minimum prefix length of a Fakespot keyword the user must type to
  // trigger the suggestion. 0 means the min length should be taken from Nimbus.
  ["fakespot.minKeywordLength", 4],

  // The number of times the user has clicked the "Show less frequently" command
  // for Fakespot suggestions.
  ["fakespot.showLessFrequentlyCount", 0],

  // The index of Fakespot results within the Firefox Suggest section. A
  // negative index is relative to the end of the section.
  ["fakespot.suggestedIndex", -1],

  // When true, `javascript:` URLs are not included in search results.
  ["filter.javascript", true],

  // Focus the content document when pressing the Escape key, if there's no
  // remaining typed history.
  ["focusContentDocumentOnEsc", true],

  // Applies URL highlighting and other styling to the text in the urlbar input.
  ["formatting.enabled", true],

  // Whether Firefox Suggest group labels are shown in the urlbar view in en-*
  // locales. Labels are not shown in other locales but likely will be in the
  // future.
  ["groupLabels.enabled", true],

  // Set default intent threshold value of 0.5
  ["intentThreshold", [0.5, "float"]],

  // Whether the results panel should be kept open during IME composition.
  ["keepPanelOpenDuringImeComposition", false],

  // Comma-separated list of result types that should trigger keyword-exposure
  // telemetry. Only applies to results with an `exposureTelemetry` value other
  // than `NONE`.
  ["keywordExposureResults", ""],

  // As a user privacy measure, don't fetch results from remote services for
  // searches that start by pasting a string longer than this. The pref name
  // indicates search suggestions, but this is used for all remote results.
  ["maxCharsForSearchSuggestions", 100],

  // The maximum number of form history results to include.
  ["maxHistoricalSearchSuggestions", 0],

  // The maximum number of results in the urlbar popup.
  ["maxRichResults", 10],

  // Feature gate pref for mdn suggestions in the urlbar.
  ["mdn.featureGate", true],

  // Comma-separated list of client variants to send to Merino
  ["merino.clientVariants", ""],

  // The Merino endpoint URL, not including parameters.
  ["merino.endpointURL", "https://merino.services.mozilla.com/api/v1/suggest"],

  // Comma-separated list of providers to request from Merino
  ["merino.providers", ""],

  // Timeout for Merino fetches (ms).
  ["merino.timeoutMs", 200],

  // Set default NER threshold value of 0.5
  ["nerThreshold", [0.5, "float"]],

  // Whether addresses and search results typed into the address bar
  // should be opened in new tabs by default.
  ["openintab", false],

  // Feature gate pref for Pocket suggestions in the urlbar.
  ["pocket.featureGate", false],

  // The number of times the user has clicked the "Show less frequently" command
  // for Pocket suggestions.
  ["pocket.showLessFrequentlyCount", 0],

  // If disabled, QuickActions will not be included in either the default search
  // mode or the QuickActions search mode.
  ["quickactions.enabled", true],

  // The number of times we should show the actions onboarding label.
  ["quickactions.timesToShowOnboardingLabel", 0],

  // The number of times we have shown the actions onboarding label.
  ["quickactions.timesShownOnboardingLabel", 0],

  // Whether we will match QuickActions within a phrase and not only a prefix.
  ["quickactions.matchInPhrase", true],

  // The minumum amount of characters required for the user to input before
  // matching actions. Setting this to 0 will show the actions in the
  // zero prefix state.
  ["quickactions.minimumSearchString", 3],

  // Whether we show the Actions section in about:preferences.
  ["quickactions.showPrefs", false],

  // When non-zero, this is the character-count threshold (inclusive) for
  // showing AMP suggestions as top picks. If an AMP suggestion is triggered by
  // a keyword at least this many characters long, it will be shown as a top
  // pick.
  ["quicksuggest.ampTopPickCharThreshold", 5],

  // Whether the Firefox Suggest data collection opt-in result is enabled.
  ["quicksuggest.contextualOptIn", false],

  // The last time (as seconds) the user dismissed the Firefox Suggest contextual
  // opt-in result.
  ["quicksuggest.contextualOptIn.lastDismissedTime", 0],

  // Number that the user dismissed the Firefox Suggest contextual opt-in result.
  ["quicksuggest.contextualOptIn.dismissedCount", 0],

  // Period until reshow the Firefox Suggest contextual opt-in result when first dismissed.
  ["quicksuggest.contextualOptIn.firstReshowAfterPeriodDays", 7],

  // Period until reshow the Firefox Suggest contextual opt-in result when second dismissed.
  ["quicksuggest.contextualOptIn.secondReshowAfterPeriodDays", 14],

  // Period until reshow the Firefox Suggest contextual opt-in result when third dismissed.
  ["quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays", 60],

  // Number of impression for the Firefox Suggest contextual opt-in result.
  ["quicksuggest.contextualOptIn.impressionCount", 0],

  // Limit for impression to dismiss the Firefox Suggest contextual opt-in
  // result.
  ["quicksuggest.contextualOptIn.impressionLimit", 20],

  // The first impression time (seconds) for the Firefox Suggest contextual
  // opt-in result.
  ["quicksuggest.contextualOptIn.firstImpressionTime", 0],

  // Days until dismiss the Firefox Suggest contextual opt-in result after first
  // impression.
  ["quicksuggest.contextualOptIn.impressionDaysLimit", 5],

  // Whether the user has opted in to data collection for quick suggest.
  ["quicksuggest.dataCollection.enabled", false],

  // Global toggle for whether the quick suggest feature is enabled, i.e.,
  // sponsored and recommended results related to the user's search string.
  ["quicksuggest.enabled", false],

  // Comma-separated list of Suggest exposure suggestion types to enable.
  ["quicksuggest.exposureSuggestionTypes", ""],

  // Whether non-sponsored quick suggest results are subject to impression
  // frequency caps. This pref is a fallback for the Nimbus variable
  // `quickSuggestImpressionCapsNonSponsoredEnabled`.
  ["quicksuggest.impressionCaps.nonSponsoredEnabled", false],

  // Whether sponsored quick suggest results are subject to impression frequency
  // caps. This pref is a fallback for the Nimbus variable
  // `quickSuggestImpressionCapsSponsoredEnabled`.
  ["quicksuggest.impressionCaps.sponsoredEnabled", false],

  // JSON'ed object of quick suggest impression stats. Used for implementing
  // impression frequency caps for quick suggest suggestions.
  ["quicksuggest.impressionCaps.stats", ""],

  // If the user has gone through a quick suggest prefs migration, then this
  // pref will have a user-branch value that records the latest prefs version.
  // Version changelog:
  //
  // 0: (Unversioned) When `suggest.quicksuggest` is false, all quick suggest
  //    results are disabled and `suggest.quicksuggest.sponsored` is ignored. To
  //    show sponsored suggestions, both prefs must be true.
  //
  // 1: `suggest.quicksuggest` is removed, `suggest.quicksuggest.nonsponsored`
  //    is introduced. `suggest.quicksuggest.nonsponsored` and
  //    `suggest.quicksuggest.sponsored` are independent:
  //    `suggest.quicksuggest.nonsponsored` controls non-sponsored results and
  //    `suggest.quicksuggest.sponsored` controls sponsored results.
  //    `quicksuggest.dataCollection.enabled` is introduced.
  //
  // 2: For online, the defaults for `suggest.quicksuggest.nonsponsored` and
  //    `suggest.quicksuggest.sponsored` are true. Previously they were false.
  ["quicksuggest.migrationVersion", 0],

  // Whether Suggest will use the ML backend in addition to Rust.
  ["quicksuggest.mlEnabled", false],

  // Whether Firefox Suggest will use the new Rust backend instead of the
  // original JS backend.
  ["quicksuggest.rustEnabled", true],

  // The Suggest Rust backend will ingest remote settings every N seconds as
  // defined by this pref. Ingestion uses nsIUpdateTimerManager so the interval
  // will persist across app restarts. The default value is 24 hours, same as
  // the interval used by the desktop remote settings client.
  ["quicksuggest.rustIngestIntervalSeconds", 60 * 60 * 24],

  // We only show recent searches within the past 3 days by default.
  // Stored as a string as some code handle timestamp sized int's.
  ["recentsearches.expirationMs", (1000 * 60 * 60 * 24 * 3).toString()],

  // Feature gate pref for recent searches being shown in the urlbar.
  ["recentsearches.featureGate", true],

  // Store the time the last default engine changed so we can only show
  // recent searches since then.
  // Stored as a string as some code handle timestamp sized int's.
  ["recentsearches.lastDefaultChanged", "-1"],

  // The maximum number of recent searches we will show.
  ["recentsearches.maxResults", 5],

  // When true, URLs in the user's history that look like search result pages
  // are styled to look like search engine results instead of the usual history
  // results.
  ["restyleSearches", false],

  // Allow the result menu button to be reached with the Tab key.
  ["resultMenu.keyboardAccessible", true],

  // Feature gate pref for rich suggestions being shown in the urlbar.
  ["richSuggestions.featureGate", true],

  // If true, we show tail suggestions when available.
  ["richSuggestions.tail", true],

  // Disable the urlbar OneOff panel from being shown.
  ["scotchBonnet.disableOneOffs", false],

  // A short-circuit pref to enable all the features that are part of a
  // grouped release.
  ["scotchBonnet.enableOverride", false],

  // Allow searchmode to be persisted as the user navigates the
  // search host.
  ["scotchBonnet.persistSearchMode", false],

  // Feature gate pref for search restrict keywords being shown in the urlbar.
  ["searchRestrictKeywords.featureGate", false],

  // Hidden pref. Disables checks that prevent search tips being shown, thus
  // showing them every time the newtab page or the default search engine
  // homepage is opened.
  ["searchTips.test.ignoreShowLimits", false],

  // Feature gate pref for secondary actions being shown in the urlbar.
  ["secondaryActions.featureGate", false],

  // Alternative switch to tab implementation using secondaryActions.
  ["secondaryActions.switchToTab", false],

  // Whether to show each local search shortcut button in the view.
  ["shortcuts.bookmarks", true],
  ["shortcuts.tabs", true],
  ["shortcuts.history", true],
  ["shortcuts.actions", true],

  // Boolean to determine if the providers defined in `exposureResults`
  // should be displayed in search results. This can be set by a
  // Nimbus variable and is expected to be set via nimbus experiment
  // configuration. For the control branch of an experiment this would be
  // false and true for the treatment.
  ["showExposureResults", false],

  // Whether to show search suggestions before general results.
  ["showSearchSuggestionsFirst", true],

  // If true, show the search term in the Urlbar while on
  // a default search engine results page.
  ["showSearchTerms.enabled", true],

  // Global toggle for whether the show search terms feature
  // can be used at all, and enabled/disabled by the user.
  ["showSearchTerms.featureGate", false],

  // Whether speculative connections should be enabled.
  ["speculativeConnect.enabled", true],

  // If true, top sites may include sponsored ones.
  ["sponsoredTopSites", false],

  // If `browser.urlbar.addons.featureGate` is true, this controls whether
  // addon suggestions are turned on.
  ["suggest.addons", true],

  // Whether results will include the user's bookmarks.
  ["suggest.bookmark", true],

  // Whether results will include a calculator.
  ["suggest.calculator", false],

  // Whether results will include clipboard results.
  ["suggest.clipboard", true],

  // Whether results will include search engines (e.g. tab-to-search).
  ["suggest.engines", true],

  // If `browser.urlbar.fakespot.featureGate` is true, this controls whether
  // Fakespot suggestions are turned on.
  ["suggest.fakespot", true],

  // Whether results will include the user's history.
  ["suggest.history", true],

  // If `browser.urlbar.mdn.featureGate` is true, this controls whether
  // mdn suggestions are turned on.
  ["suggest.mdn", true],

  // Whether results will include switch-to-tab results.
  ["suggest.openpage", true],

  // If `pocket.featureGate` is true, this controls whether Pocket suggestions
  // are turned on.
  ["suggest.pocket", true],

  // Whether results will include QuickActions in the default search mode.
  ["suggest.quickactions", false],

  // Whether results will include non-sponsored quick suggest suggestions.
  ["suggest.quicksuggest.nonsponsored", false],

  // Whether results will include sponsored quick suggest suggestions.
  ["suggest.quicksuggest.sponsored", false],

  // If `browser.urlbar.recentsearches.featureGate` is true, this controls whether
  // recentsearches are turned on.
  ["suggest.recentsearches", true],

  // Whether results will include synced tab results. The syncing of open tabs
  // must also be enabled, from Sync preferences.
  ["suggest.remotetab", true],

  // Whether results will include search suggestions.
  ["suggest.searches", false],

  // Whether results will include top sites and the view will open on focus.
  ["suggest.topsites", true],

  // If `browser.urlbar.trending.featureGate` is true, this controls whether
  // trending suggestions are turned on.
  ["suggest.trending", true],

  // If `browser.urlbar.weather.featureGate` is true, this controls whether
  // weather suggestions are turned on.
  ["suggest.weather", true],

  // If `browser.urlbar.yelp.featureGate` is true, this controls whether
  // Yelp suggestions are turned on.
  ["suggest.yelp", true],

  // Whether history results with the same title and URL excluding the ref
  // will be deduplicated.
  ["deduplication.enabled", false],

  // How old history results have to be to be deduplicated.
  ["deduplication.thresholdDays", 0],

  // When using switch to tabs, if set to true this will move the tab into the
  // active window.
  ["switchTabs.adoptIntoActiveWindow", false],

  // Controls whether searching for open tabs returns tabs from any container
  // or only from the current container.
  ["switchTabs.searchAllContainers", true],

  // The minimum number of characters needed to match a tab group name.
  ["tabGroups.minSearchLength", 1],

  // The number of remaining times the user can interact with tab-to-search
  // onboarding results before we stop showing them.
  ["tabToSearch.onboard.interactionsLeft", 3],

  // The number of times the user has been shown the onboarding search tip.
  ["tipShownCount.searchTip_onboard", 0],

  // The number of times the user has been shown the redirect search tip.
  ["tipShownCount.searchTip_redirect", 0],

  // Feature gate pref for trending suggestions in the urlbar.
  ["trending.featureGate", true],

  // The maximum number of trending results to show while not in search mode.
  ["trending.maxResultsNoSearchMode", 10],

  // The maximum number of trending results to show in search mode.
  ["trending.maxResultsSearchMode", 10],

  // Whether to only show trending results when the urlbar is in search
  // mode or when the user initially opens the urlbar without selecting
  // an engine.
  ["trending.requireSearchMode", false],

  // Remove 'https://' from url when urlbar is focused.
  ["trimHttps", false],

  // Remove redundant portions from URLs.
  ["trimURLs", true],

  // Whether unit conversion is enabled.
  ["unitConversion.enabled", false],

  // The index where we show unit conversion results.
  ["unitConversion.suggestedIndex", 1],

  // Untrim url, when urlbar is focused.
  // Note: This pref will be removed once the feature is stable.
  ["untrimOnUserInteraction.featureGate", false],

  // Whether or not Unified Search Button is shown always.
  ["unifiedSearchButton.always", false],

  // Feature gate pref for weather suggestions in the urlbar.
  ["weather.featureGate", true],

  // The minimum prefix length of a weather keyword the user must type to
  // trigger the suggestion. 0 means the min length should be taken from Nimbus
  // or remote settings.
  ["weather.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for weather suggestions.
  ["weather.showLessFrequentlyCount", 0],

  // Feature gate pref for Yelp suggestions in the urlbar.
  ["yelp.featureGate", false],

  // The minimum prefix length of a Yelp keyword the user must type to trigger
  // the suggestion. 0 means the min length should be taken from Nimbus.
  ["yelp.minKeywordLength", 4],

  // Whether Yelp suggestions will be served from the Suggest ML backend instead
  // of Rust.
  ["yelp.mlEnabled", false],

  // Whether Yelp suggestions should be shown as top picks. This is a fallback
  // pref for the `yelpSuggestPriority` Nimbus variable.
  ["yelp.priority", false],

  // The number of times the user has clicked the "Show less frequently" command
  // for Yelp suggestions.
  ["yelp.showLessFrequentlyCount", 0],
]);

const PREF_OTHER_DEFAULTS = new Map([
  ["browser.fixup.dns_first_for_single_words", false],
  ["browser.ml.enable", false],
  ["browser.search.suggest.enabled", true],
  ["browser.search.suggest.enabled.private", false],
  ["keyword.enabled", true],
  ["security.insecure_connection_text.enabled", true],
  ["ui.popup.disable_autohide", false],
]);

// Default values for Nimbus urlbar variables that do not have fallback prefs.
// Variables with fallback prefs do not need to be defined here because their
// defaults are the values of their fallbacks.
const NIMBUS_DEFAULTS = {
  addonsShowLessFrequentlyCap: 0,
  fakespotMinKeywordLength: null,
  pocketShowLessFrequentlyCap: 0,
  pocketSuggestIndex: null,
  quickSuggestScoreMap: null,
  weatherKeywordsMinimumLength: null,
  weatherShowLessFrequentlyCap: null,
  yelpMinKeywordLength: null,
  yelpSuggestNonPriorityIndex: null,
};

// Maps preferences under browser.urlbar.suggest to behavior names, as defined
// in mozIPlacesAutoComplete.
const SUGGEST_PREF_TO_BEHAVIOR = {
  history: "history",
  bookmark: "bookmark",
  openpage: "openpage",
  searches: "search",
};

const PREF_TYPES = new Map([
  ["boolean", "Bool"],
  ["float", "Float"],
  ["number", "Int"],
  ["string", "Char"],
]);

/**
 * Builds the standard result groups and returns the root group.  Result
 * groups determine the composition of results in the muxer, i.e., how they're
 * grouped and sorted.  Each group is an object that looks like this:
 *
 * {
 *   {UrlbarUtils.RESULT_GROUP} [group]
 *     This is defined only on groups without children, and it determines the
 *     result group that the group will contain.
 *   {number} [maxResultCount]
 *     An optional maximum number of results the group can contain.  If it's
 *     not defined and the parent group does not define `flexChildren: true`,
 *     then the max is the parent's max.  If the parent group defines
 *     `flexChildren: true`, then `maxResultCount` is ignored.
 *   {boolean} [flexChildren]
 *     If true, then child groups are "flexed", similar to flex in HTML.  Each
 *     child group should define the `flex` property (or, if they don't, `flex`
 *     is assumed to be zero).  `flex` is a number that defines the ratio of a
 *     child's result count to the total result count of all children.  More
 *     specifically, `flex: X` on a child means that the initial maximum result
 *     count of the child is `parentMaxResultCount * (X / N)`, where `N` is the
 *     sum of the `flex` values of all children.  If there are any child groups
 *     that cannot be completely filled, then the muxer will attempt to overfill
 *     the children that were completely filled, while still respecting their
 *     relative `flex` values.
 *   {number} [flex]
 *     The flex value of the group.  This should be defined only on groups
 *     where the parent defines `flexChildren: true`.  See `flexChildren` for a
 *     discussion of flex.
 *   {array} [children]
 *     An array of child group objects.
 * }
 *
 * @param {boolean} showSearchSuggestionsFirst
 *   If true, the suggestions group will come before the general group.
 * @returns {object}
 *   The root group.
 */
function makeResultGroups({ showSearchSuggestionsFirst }) {
  let rootGroup = {
    children: [
      // heuristic
      {
        maxResultCount: 1,
        children: [
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_TEST },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_EXTENSION },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_SEARCH_TIP },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_OMNIBOX },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_AUTOFILL },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE },
          {
            group:
              lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_RESTRICT_KEYWORD_AUTOFILL,
          },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_HISTORY_URL },
          { group: lazy.UrlbarUtils.RESULT_GROUP.HEURISTIC_FALLBACK },
        ],
      },
      // extensions using the omnibox API
      {
        group: lazy.UrlbarUtils.RESULT_GROUP.OMNIBOX,
      },
    ],
  };

  // Prepare the parent group for suggestions and general.
  let mainGroup = {
    flexChildren: true,
    children: [
      // suggestions
      {
        children: [
          {
            flexChildren: true,
            children: [
              {
                // If `maxHistoricalSearchSuggestions` == 0, the muxer forces
                // `maxResultCount` to be zero and flex is ignored, per query.
                flex: 2,
                group: lazy.UrlbarUtils.RESULT_GROUP.FORM_HISTORY,
              },
              {
                flex: 99,
                group: lazy.UrlbarUtils.RESULT_GROUP.RECENT_SEARCH,
              },
              {
                flex: 4,
                group: lazy.UrlbarUtils.RESULT_GROUP.REMOTE_SUGGESTION,
              },
            ],
          },
          {
            group: lazy.UrlbarUtils.RESULT_GROUP.TAIL_SUGGESTION,
          },
        ],
      },
      // general
      {
        group: lazy.UrlbarUtils.RESULT_GROUP.GENERAL_PARENT,
        children: [
          {
            availableSpan: 3,
            group: lazy.UrlbarUtils.RESULT_GROUP.INPUT_HISTORY,
          },
          {
            flexChildren: true,
            children: [
              {
                flex: 1,
                group: lazy.UrlbarUtils.RESULT_GROUP.REMOTE_TAB,
              },
              {
                flex: 2,
                group: lazy.UrlbarUtils.RESULT_GROUP.GENERAL,
              },
              {
                // We show relatively many about-page results because they're
                // only added for queries starting with "about:".
                flex: 2,
                group: lazy.UrlbarUtils.RESULT_GROUP.ABOUT_PAGES,
              },
              {
                flex: 99,
                group: lazy.UrlbarUtils.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD,
              },
            ],
          },
          {
            group: lazy.UrlbarUtils.RESULT_GROUP.INPUT_HISTORY,
          },
        ],
      },
    ],
  };
  if (!showSearchSuggestionsFirst) {
    mainGroup.children.reverse();
  }
  mainGroup.children[0].flex = 2;
  mainGroup.children[1].flex = 1;
  rootGroup.children.push(mainGroup);

  return rootGroup;
}

/**
 * Preferences class.  The exported object is a singleton instance.
 */
class Preferences {
  /**
   * Constructor
   */
  constructor() {
    this._map = new Map();
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);

    Services.prefs.addObserver(PREF_URLBAR_BRANCH, this, true);
    for (let pref of PREF_OTHER_DEFAULTS.keys()) {
      Services.prefs.addObserver(pref, this, true);
    }
    this._observerWeakRefs = [];
    this.addObserver(this);

    // These prefs control the value of the shouldHandOffToSearchMode pref. They
    // are exposed as a class variable so UrlbarPrefs observers can watch for
    // changes in these prefs.
    this.shouldHandOffToSearchModePrefs = [
      "keyword.enabled",
      "suggest.searches",
    ];

    lazy.NimbusFeatures.urlbar.onUpdate(() => this._onNimbusUpdate());
  }

  /**
   * Returns the value for the preference with the given name.
   * For preferences in the "browser.urlbar."" branch, the passed-in name
   * should be relative to the branch. It's also possible to get prefs from the
   * PREF_OTHER_DEFAULTS Map, specifying their full name.
   *
   * @param {string} pref
   *        The name of the preference to get.
   * @returns {*} The preference value.
   */
  get(pref) {
    let value = this._map.get(pref);
    if (value === undefined) {
      value = this._getPrefValue(pref);
      this._map.set(pref, value);
    }
    return value;
  }

  /**
   * Sets the value for the preference with the given name.
   * For preferences in the "browser.urlbar."" branch, the passed-in name
   * should be relative to the branch. It's also possible to set prefs from the
   * PREF_OTHER_DEFAULTS Map, specifying their full name.
   *
   * @param {string} pref
   *        The name of the preference to set.
   * @param {*} value The preference value.
   */
  set(pref, value) {
    let { defaultValue, set } = this._getPrefDescriptor(pref);
    if (typeof value != typeof defaultValue) {
      throw new Error(`Invalid value type ${typeof value} for pref ${pref}`);
    }
    set(pref, value);
  }

  /**
   * Clears the value for the preference with the given name.
   *
   * @param {string} pref
   *        The name of the preference to clear.
   */
  clear(pref) {
    let { clear } = this._getPrefDescriptor(pref);
    clear(pref);
  }

  /**
   * Returns whether the given preference has a value on the user branch.
   *
   * @param {string} pref
   *   The name of the preference.
   * @returns {boolean}
   *   Whether the pref has a value on the user branch.
   */
  hasUserValue(pref) {
    let { hasUserValue } = this._getPrefDescriptor(pref);
    return hasUserValue(pref);
  }

  /**
   * Builds the standard result groups.  See makeResultGroups.
   *
   * @param {object} options
   *   See makeResultGroups.
   * @returns {object}
   *   The root group.
   */
  makeResultGroups(options) {
    return makeResultGroups(options);
  }

  /**
   * Gets a pref but allows the `scotchBonnet.enableOverride` pref to
   * short circuit them so one pref can be used to enable multiple
   * features.
   *
   * @param {string} pref
   *        The name of the preference to clear.
   * @returns {*} The preference value.
   */
  getScotchBonnetPref(pref) {
    return this.get("scotchBonnet.enableOverride") || this.get(pref);
  }

  get resultGroups() {
    if (!this.#resultGroups) {
      this.#resultGroups = makeResultGroups({
        showSearchSuggestionsFirst: this.get("showSearchSuggestionsFirst"),
      });
    }
    return this.#resultGroups;
  }

  /**
   * Adds a preference observer.  Observers are held weakly.
   *
   * @param {object} observer
   *        An object that may optionally implement one or both methods:
   *         - `onPrefChanged` invoked when one of the preferences listed here
   *           change. It will be passed the pref name.  For prefs in the
   *           `browser.urlbar.` branch, the name will be relative to the branch.
   *           For other prefs, the name will be the full name.
   *         - `onNimbusChanged` invoked when a Nimbus value changes. It will be
   *           passed the name of the changed Nimbus variable.
   */
  addObserver(observer) {
    this._observerWeakRefs.push(Cu.getWeakReference(observer));
  }

  /**
   * Removes a preference observer.
   *
   * @param {object} observer
   *   An observer previously added with `addObserver()`.
   */
  removeObserver(observer) {
    for (let i = 0; i < this._observerWeakRefs.length; i++) {
      let obs = this._observerWeakRefs[i].get();
      if (obs && obs == observer) {
        this._observerWeakRefs.splice(i, 1);
        break;
      }
    }
  }

  /**
   * Observes preference changes.
   *
   * @param {nsISupports} subject
   *   The subject of the notification.
   * @param {string} topic
   *   The topic of the notification.
   * @param {string} data
   *   The data attached to the notification.
   */
  observe(subject, topic, data) {
    let pref = data.replace(PREF_URLBAR_BRANCH, "");
    if (!PREF_URLBAR_DEFAULTS.has(pref) && !PREF_OTHER_DEFAULTS.has(pref)) {
      return;
    }
    this.#notifyObservers("onPrefChanged", pref);
  }

  /**
   * Called when a pref tracked by UrlbarPrefs changes.
   *
   * @param {string} pref
   *        The name of the pref, relative to `browser.urlbar.` if the pref is
   *        in that branch.
   */
  onPrefChanged(pref) {
    this._map.delete(pref);

    // Some prefs may influence others.
    switch (pref) {
      case "autoFill.adaptiveHistory.useCountThreshold":
        this._map.delete("autoFillAdaptiveHistoryUseCountThreshold");
        return;
      case "showSearchSuggestionsFirst":
        this.#resultGroups = null;
        return;
    }

    if (pref.startsWith("suggest.")) {
      this._map.delete("defaultBehavior");
    }

    if (this.shouldHandOffToSearchModePrefs.includes(pref)) {
      this._map.delete("shouldHandOffToSearchMode");
    }
  }

  /**
   * Called when the `NimbusFeatures.urlbar` value changes.
   */
  _onNimbusUpdate() {
    let oldNimbus = this._clearNimbusCache();
    let newNimbus = this._nimbus;

    // Callback to observers having onNimbusChanged.
    let variableNames = new Set(Object.keys(oldNimbus));
    for (let name of Object.keys(newNimbus)) {
      variableNames.add(name);
    }
    for (let name of variableNames) {
      if (
        oldNimbus.hasOwnProperty(name) != newNimbus.hasOwnProperty(name) ||
        oldNimbus[name] !== newNimbus[name]
      ) {
        this.#notifyObservers("onNimbusChanged", name);
      }
    }
  }

  /**
   * Clears cached Nimbus variables. The cache will be repopulated the next time
   * `_nimbus` is accessed.
   *
   * @returns {object}
   *   The value of the cache before it was cleared. It's an object that maps
   *   from variable names to values.
   */
  _clearNimbusCache() {
    let nimbus = this.__nimbus;
    if (nimbus) {
      for (let key of Object.keys(nimbus)) {
        this._map.delete(key);
      }
      this.__nimbus = null;
    }
    return nimbus || {};
  }

  get _nimbus() {
    if (!this.__nimbus) {
      this.__nimbus = lazy.NimbusFeatures.urlbar.getAllVariables({
        defaultValues: NIMBUS_DEFAULTS,
      });
    }
    return this.__nimbus;
  }

  /**
   * Returns the raw value of the given preference straight from Services.prefs.
   *
   * @param {string} pref
   *        The name of the preference to get.
   * @returns {*} The raw preference value.
   */
  _readPref(pref) {
    let { defaultValue, get } = this._getPrefDescriptor(pref);
    return get(pref, defaultValue);
  }

  /**
   * Returns a validated and/or fixed-up value of the given preference.  The
   * value may be validated for correctness, or it might be converted into a
   * different value that is easier to work with than the actual value stored in
   * the preferences branch.  Not all preferences require validation or fixup.
   *
   * The values returned from this method are the values that are made public by
   * this module.
   *
   * @param {string} pref
   *        The name of the preference to get.
   * @returns {*} The validated and/or fixed-up preference value.
   */
  _getPrefValue(pref) {
    switch (pref) {
      case "shortcuts.actions": {
        return this.get("scotchBonnet.enableOverride") && this._readPref(pref);
      }
      case "defaultBehavior": {
        let val = 0;
        for (let type of Object.keys(SUGGEST_PREF_TO_BEHAVIOR)) {
          let behavior = `BEHAVIOR_${SUGGEST_PREF_TO_BEHAVIOR[
            type
          ].toUpperCase()}`;
          val |=
            this.get("suggest." + type) && Ci.mozIPlacesAutoComplete[behavior];
        }
        return val;
      }
      case "shouldHandOffToSearchMode":
        return this.shouldHandOffToSearchModePrefs.some(
          prefName => !this.get(prefName)
        );
      case "autoFillAdaptiveHistoryUseCountThreshold": {
        const nimbusValue =
          this._nimbus.autoFillAdaptiveHistoryUseCountThreshold;
        return nimbusValue === undefined
          ? this.get("autoFill.adaptiveHistory.useCountThreshold")
          : parseFloat(nimbusValue);
      }
      case "exposureResults":
      case "keywordExposureResults":
      case "quicksuggest.exposureSuggestionTypes":
        return new Set(
          this._readPref(pref)
            .split(",")
            .map(s => s.trim())
            .filter(s => !!s)
        );
    }
    return this._readPref(pref);
  }

  /**
   * Returns a descriptor of the given preference.
   *
   * @param {string} pref The preference to examine.
   * @returns {object} An object describing the pref with the following shape:
   *          { defaultValue, get, set, clear }
   */
  _getPrefDescriptor(pref) {
    let branch = Services.prefs.getBranch(PREF_URLBAR_BRANCH);
    let defaultValue = PREF_URLBAR_DEFAULTS.get(pref);
    if (defaultValue === undefined) {
      branch = Services.prefs;
      defaultValue = PREF_OTHER_DEFAULTS.get(pref);
      if (defaultValue === undefined) {
        let nimbus = this._getNimbusDescriptor(pref);
        if (nimbus) {
          return nimbus;
        }
        throw new Error("Trying to access an unknown pref " + pref);
      }
    }

    let type;
    if (!Array.isArray(defaultValue)) {
      type = PREF_TYPES.get(typeof defaultValue);
    } else {
      if (defaultValue.length != 2) {
        throw new Error("Malformed pref def: " + pref);
      }
      [defaultValue, type] = defaultValue;
      type = PREF_TYPES.get(type);
    }
    if (!type) {
      throw new Error("Unknown pref type: " + pref);
    }
    return {
      defaultValue,
      get: branch[`get${type}Pref`],
      // Float prefs are stored as Char.
      set: branch[`set${type == "Float" ? "Char" : type}Pref`],
      clear: branch.clearUserPref,
      hasUserValue: branch.prefHasUserValue,
    };
  }

  /**
   * Returns a descriptor for the given Nimbus property, if it exists.
   *
   * @param {string} name
   *   The name of the desired property in the object returned from
   *   NimbusFeatures.urlbar.getAllVariables().
   * @returns {object}
   *   An object describing the property's value with the following shape (same
   *   as _getPrefDescriptor()):
   *     { defaultValue, get, set, clear }
   *   If the property doesn't exist, null is returned.
   */
  _getNimbusDescriptor(name) {
    if (!this._nimbus.hasOwnProperty(name)) {
      return null;
    }
    return {
      defaultValue: this._nimbus[name],
      get: () => this._nimbus[name],
      set() {
        throw new Error(`'${name}' is a Nimbus value and cannot be set`);
      },
      clear() {
        throw new Error(`'${name}' is a Nimbus value and cannot be cleared`);
      },
      hasUserValue() {
        throw new Error(
          `'${name}' is a Nimbus value and does not have a user value`
        );
      },
    };
  }

  /**
   * Initializes the showSearchSuggestionsFirst pref based on the matchGroups
   * pref.  This function can be removed when the corresponding UI migration in
   * BrowserGlue.sys.mjs is no longer needed.
   */
  initializeShowSearchSuggestionsFirstPref() {
    let matchGroups = [];
    let pref = Services.prefs.getCharPref("browser.urlbar.matchGroups", "");
    try {
      matchGroups = pref.split(",").map(v => {
        let group = v.split(":");
        return [group[0].trim().toLowerCase(), Number(group[1])];
      });
    } catch (ex) {}
    let groupNames = matchGroups.map(group => group[0]);
    let suggestionIndex = groupNames.indexOf("suggestion");
    let generalIndex = groupNames.indexOf("general");
    let showSearchSuggestionsFirst =
      generalIndex < 0 ||
      (suggestionIndex >= 0 && suggestionIndex < generalIndex);
    let oldValue = Services.prefs.getBoolPref(
      "browser.urlbar.showSearchSuggestionsFirst"
    );
    Services.prefs.setBoolPref(
      "browser.urlbar.showSearchSuggestionsFirst",
      showSearchSuggestionsFirst
    );

    // Pref observers aren't called when a pref is set to its current value, but
    // we always want to set matchGroups to the appropriate default value via
    // onPrefChanged, so call it now if necessary.  This is really only
    // necessary for tests since the only time this function is called outside
    // of tests is by a UI migration in BrowserGlue.
    if (oldValue == showSearchSuggestionsFirst) {
      this.onPrefChanged("showSearchSuggestionsFirst");
    }
  }

  /**
   * Return whether or not persisted search terms is enabled.
   *
   * @returns {boolean} true: if enabled.
   */
  isPersistedSearchTermsEnabled() {
    return (
      this.getScotchBonnetPref("showSearchTerms.featureGate") &&
      this.get("showSearchTerms.enabled") &&
      !lazy.CustomizableUI.getPlacementOfWidget("search-container")
    );
  }

  #notifyObservers(method, changed) {
    for (let i = 0; i < this._observerWeakRefs.length; ) {
      let observer = this._observerWeakRefs[i].get();
      if (!observer) {
        // The observer has been GC'ed, so remove it from our list.
        this._observerWeakRefs.splice(i, 1);
        continue;
      }
      if (method in observer) {
        try {
          observer[method](changed);
        } catch (ex) {
          console.error(ex);
        }
      }
      ++i;
    }
  }

  #resultGroups = null;
}

export var UrlbarPrefs = new Preferences();
