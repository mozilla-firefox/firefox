/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports the UrlbarPrefs singleton, which manages preferences for
 * the urlbar. It also provides access to urlbar Nimbus variables as if they are
 * preferences, but only for variables with fallback prefs.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { TelemetryReportingPolicy } from "resource://gre/modules/TelemetryReportingPolicy.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

const PREF_URLBAR_BRANCH = "browser.urlbar.";

/**
 * @typedef {boolean|number|string|[number, string]} PreferenceDefaultAndType
 * Prefs are defined as [pref name, default value] or [pref name, [default
 * value, type]]. In the former case, the getter method name is inferred from
 * the typeof the default value.
 *
 * @typedef {[string, PreferenceDefaultAndType]} PreferenceDefinition
 */

// NOTE: Don't name prefs (relative to the `browser.urlbar` branch) the same as
// Nimbus urlbar features. Doing so would cause a name collision because pref
// names and Nimbus feature names are both kept as keys in UrlbarPref's map. For
// a list of Nimbus features, see toolkit/components/nimbus/FeatureManifest.yaml.
const PREF_URLBAR_DEFAULTS = /** @type {PreferenceDefinition[]} */ ([
  // Whether we announce to screen readers when tab-to-search results are
  // inserted.
  ["accessibility.tabToSearch.announceResults", true],

  // Feature gate pref for addon suggestions in the urlbar.
  ["addons.featureGate", false],

  // The minimum prefix length of an addons keyword the user must type to
  // trigger the suggestion.
  ["addons.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for addon suggestions.
  ["addons.showLessFrequentlyCount", 0],

  // Feature gate pref for AMP suggestions in the urlbar.
  ["amp.featureGate", false],

  // The minimum prefix length of an AMP keyword the user must type to trigger
  // the suggestion.
  ["amp.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for AMP suggestions.
  ["amp.showLessFrequentlyCount", 0],

  // "Autofill" is the name of the feature that automatically completes domains
  // and URLs that the user has visited as the user is typing them in the urlbar
  // textbox.  If false, autofill will be disabled.
  ["autoFill", true],

  // Whether enabling adaptive history autofill. The Nimbus variable
  // `autoFillAdaptiveHistoryEnabled` writes to this pref via `setPref`.
  ["autoFill.adaptiveHistory.enabled", false],

  // Duration in ms to block after backspace penalty. Default: 2 days.
  ["autoFill.backspaceBlockDurationMs", 172800000],

  // How many times the user must consecutively backspace away an autofill
  // suggestion before we penalize and temporarily suppress it from autofilling.
  // The result may still appear as a regular history result.
  ["autoFill.backspaceThreshold", 3],

  // Duration in ms to block an origin/URL after dismiss. Default: 7 days.
  ["autoFill.dismissalBlockDurationMs", 604800000],

  // Minimum char length of the user's search string to enable adaptive history
  // autofill. The Nimbus variable `autoFillAdaptiveHistoryMinCharsThreshold`
  // writes to this pref via `setPref`.
  ["autoFill.adaptiveHistory.minCharsThreshold", 0],

  // Threshold for use count of input history that we handle as adaptive history
  // autofill. If the use count is this value or more, it will be a candidate.
  // Set the threshold to not be candidate the input history passed approximately
  // 30 days since user input it as the default.
  ["autoFill.adaptiveHistory.useCountThreshold", [0.47, "float"]],

  // Feature gate pref for clipboard suggestions in the urlbar.
  ["clipboard.featureGate", false],

  // Whether to close other panels when the urlbar panel opens.
  // This feature gate exists just as an emergency rollback in case of
  // unexpected issues in Release. We normally want this behavior.
  ["closeOtherPanelsOnOpen", true],

  // Feature gate pref for context menu on the urlbar.
  ["contextMenu.featureGate", false],

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

  // Maximum time (ms) the event bufferer defers events for. In automation
  // providers can be quite slow, thus we need a longer timeout to avoid
  // intermittent failures. Must be larger than
  // ProvidersManager.chunkResultsDelayMs.
  ["eventBufferer.deferringTimeoutMs", Cu.isInAutomation ? 1500 : 300],

  // If Suggest is disabled before these seconds from a search, then send a
  // disable event.
  ["events.disableSuggest.maxSecondsFromLastSearch", 300],

  // If a page is interacted with for less than these seconds, before navigating
  // away via browser chrome, then send a bounce event.
  ["events.bounce.maxSecondsFromLastSearch", 10],

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

  // When true, `javascript:` URLs are not included in search results.
  ["filter.javascript", true],

  // Feature gate pref for flight status suggestions in the urlbar.
  ["flightStatus.featureGate", false],

  // The minimum prefix length of a flight status keyword the user must type to
  // trigger the suggestion. 0 means the min length should be taken from Nimbus
  // or remote settings.
  ["flightStatus.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for flight status suggestions.
  ["flightStatus.showLessFrequentlyCount", 0],

  // Focus the content document when pressing the Escape key, if there's no
  // remaining typed history.
  ["focusContentDocumentOnEsc", true],

  // Applies URL highlighting and other styling to the text in the urlbar input.
  ["formatting.enabled", true],

  // Whether Firefox Suggest group labels are shown in the urlbar view in en-*
  // locales. Labels are not shown in other locales but likely will be in the
  // future.
  ["groupLabels.enabled", true],

  // Whether semantic history results are placed in their own result group that
  // fills only the space left after the other (non-semantic) result groups, so
  // semantic results never evict them. When false, semantic results share the
  // general group with plain history results.
  ["suggest.semanticHistory.separateGroup", false],

  // Feature gate pref for important-dates suggestions in the urlbar.
  ["importantDates.featureGate", false],

  // Set default intent threshold value of 0.5
  ["intentThreshold", [0.5, "float"]],

  // When true, in-process (chrome) `<moz-urlbar>` instances route through the
  // Urlbar actor's message-passing path instead of holding a direct
  // UrlbarParentController reference, exercising the same wire path a
  // content-process `<moz-urlbar>` uses. Off by default; intended for the
  // pref-on CI variant and local testing of the actor path.
  ["ipc.chromeMessagePassing", false],

  // Whether the results panel should be kept open during IME composition.
  ["keepPanelOpenDuringImeComposition", false],

  // Comma-separated list of result types that should trigger keyword-exposure
  // telemetry. Only applies to results with an `exposureTelemetry` value other
  // than `NONE`.
  ["keywordExposureResults", ""],

  // Enable a certain level of urlbar logging to the Browser Console. See
  // ConsoleInstance.webidl.
  ["loglevel", "Error"],

  // Feature gate pref for stock market suggestions in the urlbar.
  ["market.featureGate", false],

  // The minimum prefix length of a market keyword the user must type to
  // trigger the suggestion. 0 means the min length should be taken from Nimbus
  // or remote settings.
  ["market.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for Market suggestions.
  ["market.showLessFrequentlyCount", 0],

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

  // The minimum prefix length of a mdn keyword the user must type to trigger
  // the suggestion.
  ["mdn.minKeywordLength", 0],

  // If defined and non-zero, this is the maximum number of times the user
  // will be able to click the "Show less frequently" command for mdn
  // suggestions. If undefined or zero, the user will be able to click the
  // command without any limit.
  ["mdn.showLessFrequentlyCap", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for mdn suggestions.
  ["mdn.showLessFrequentlyCount", 0],

  // The maximum number of tab mentions the Smartbar suggests.
  ["mentions.maxResults", 5],

  // Comma-separated list of client variants to send to Merino
  ["merino.clientVariants", ""],

  // The Merino endpoint URL, not including parameters.
  ["merino.endpointURL", ""],

  // OHTTP config URL for Merino requests.
  ["merino.ohttpConfigURL", ""],

  // OHTTP relay URL for Merino requests.
  ["merino.ohttpRelayURL", ""],

  // Comma-separated list of providers to request from Merino
  ["merino.providers", ""],

  // Timeout for Merino fetches (ms).
  ["merino.timeoutMs", 500],

  // Merino endpoint prefs to be used for weather widgets
  ["merino.weather.reportEndpointURL", ""],
  ["merino.weather.hourlyEndpointURL", ""],

  // Set default NER threshold value of 0.5
  ["nerThreshold", [0.5, "float"]],

  // Feature gate pref for the <moz-urlbar> on about:newtab and about:home. When
  // enabled, it supersedes New Tab's handoff search bar.
  ["newtab.featureGate", false],

  // Whether addresses and search results typed into the address bar
  // should be opened in new tabs by default.
  ["openintab", false],

  // The cached name of the (private) default engine.
  // This is used to initialize the placeholder of the
  // urlbar before the search engine store is ready.
  ["placeholderName", ""],
  ["placeholderName.private", ""],

  // If disabled, QuickActions will not be included in either the default search
  // mode or the QuickActions search mode.
  ["quickactions.enabled", true],

  // The number of times we should show the actions onboarding label.
  ["quickactions.timesToShowOnboardingLabel", 3],

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

  // Whether or not use the Nova icon size for AMP suggestion.
  // Otherwise, use standard rich-suggestion size.
  ["quicksuggest.ampTopPickUseNovaIconSize", false],

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

  // Comma-separated list of Suggest dynamic suggestion types to enable.
  ["quicksuggest.dynamicSuggestionTypes", ""],

  // Global toggle for whether the quick suggest feature is enabled, i.e.,
  // sponsored and recommended results related to the user's search string.
  ["quicksuggest.enabled", false],

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
  // See `QuickSuggest` for details on each version.
  ["quicksuggest.migrationVersion", 0],

  // Whether Suggest will use the ML backend in addition to Rust.
  ["quicksuggest.mlEnabled", false],

  // NOTE: You should most likely access this pref via its Nimbus variable
  // instead: `UrlbarPrefs.get("quickSuggestOnlineAvailable"). It's listed here
  // mainly so tests can access it easily via `UrlbarPrefs`.
  //
  // Whether online Suggest is available to the user. This is only relevant when
  // Suggest overall is enabled.
  ["quicksuggest.online.available", false],

  // Whether online Suggest is enabled for the user. This is only relevant when
  // Suggest overall is enabled and online Suggest is available to the user.
  ["quicksuggest.online.enabled", true],

  // The last time (as seconds) the user selected 'Not Now' on Realtime
  // suggestion opt-in result.
  ["quicksuggest.realtimeOptIn.notNowTimeSeconds", 0],

  // Period until reshow Realtime suggestion opt-in after selecting "Not Now".
  ["quicksuggest.realtimeOptIn.notNowReshowAfterPeriodDays", 7],

  // The type of Realtime suggestion that the user selected 'Not Now' as CSV.
  ["quicksuggest.realtimeOptIn.notNowTypes", ""],

  // The type of Realtime suggestion that the user selected 'Dismiss' as CSV.
  ["quicksuggest.realtimeOptIn.dismissTypes", ""],

  // Whether Firefox Suggest will use the new Rust backend instead of the
  // original JS backend.
  ["quicksuggest.rustEnabled", true],

  // The Suggest Rust backend will ingest remote settings every N seconds as
  // defined by this pref. Ingestion uses nsIUpdateTimerManager so the interval
  // will persist across app restarts. The default value is 24 hours, same as
  // the interval used by the desktop remote settings client.
  ["quicksuggest.rustIngestIntervalSeconds", 60 * 60 * 24],

  // Which Suggest settings to show in the settings UI. See
  // `QuickSuggest.SETTINGS_UI` for values.
  ["quicksuggest.settingsUi", 0],

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

  // Timeout in milliseconds before stale rows are removed from the view.
  ["removeStaleRowsTimeout", 400],

  // Feature gate pref for rich suggestions being shown in the urlbar.
  ["richSuggestions.featureGate", true],

  // If true, we show tail suggestions when available.
  ["richSuggestions.tail", true],

  // Disable the urlbar OneOff panel from being shown.
  ["scotchBonnet.disableOneOffs", false],

  // A short-circuit pref to enable all the features that are part of a
  // grouped release.
  ["scotchBonnet.enableOverride", true],

  // Allow searchmode to be persisted as the user navigates the
  // search host.
  ["scotchBonnet.persistSearchMode", false],

  // Whether the search button declines to be the target of the toolbar tab
  // stop in front of the input. The shipping default is set in firefox.js,
  // where it's enabled on Nightly only.
  ["searchModeSwitcher.skipTabStop", false],

  // Feature gate pref for search restrict keywords being shown in the urlbar.
  ["searchRestrictKeywords.featureGate", false],

  // Hidden pref. Disables checks that prevent search tips being shown, thus
  // showing them every time the newtab page or the default search engine
  // homepage is opened.
  ["searchTips.test.ignoreShowLimits", false],

  // Feature gate pref for secondary actions being shown in the urlbar.
  ["secondaryActions.featureGate", false],

  // Maximum number of actions shown.
  ["secondaryActions.maxActionsShown", 3],

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

  // The maximum number of results in the smartbar popup.
  ["smartbar.maxResults", 7],

  // Whether to show search suggestions before general results in the smartbar.
  ["smartbar.showSearchSuggestionsFirst", false],

  // Whether speculative connections should be enabled.
  ["speculativeConnect.enabled", true],

  // If true, top sites may include sponsored ones.
  ["sponsoredTopSites", false],

  // Feature gate pref for realtime sports suggestions in the urlbar.
  ["sports.featureGate", false],

  // The minimum prefix length of sports keyword the user must type to trigger
  // the suggestion. 0 means the min length should be taken from Nimbus or
  // remote settings.
  ["sports.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for sports suggestions.
  ["sports.showLessFrequentlyCount", 0],

  // If `browser.urlbar.addons.featureGate` is true, this controls whether
  // addon suggestions are turned on.
  ["suggest.addons", true],

  // If `browser.urlbar.amp.featureGate` is true, this controls whether AMP
  // suggestions are turned on.
  ["suggest.amp", true],

  // Whether results will include the user's bookmarks.
  ["suggest.bookmark", true],

  // Whether results will include a calculator.
  ["suggest.calculator", false],

  // Whether results will include clipboard results.
  ["suggest.clipboard", true],

  // Whether results will include search engines (e.g. tab-to-search).
  ["suggest.engines", true],

  // If `browser.urlbar.flightStatus.featureGate` is true, this controls whether
  // flight status suggestions are turned on.
  ["suggest.flightStatus", true],

  // Whether results will include the user's history.
  ["suggest.history", true],

  // If `browser.urlbar.importantDates.featureGate` is true, this controls
  // whether important-dates suggestions are turned on.
  ["suggest.importantDates", true],

  // Whether results will include Market suggestions.
  ["suggest.market", true],

  // If `browser.urlbar.mdn.featureGate` is true, this controls whether
  // mdn suggestions are turned on.
  ["suggest.mdn", true],

  // Whether results will include switch-to-tab results.
  ["suggest.openpage", true],

  // Whether results will include QuickActions in the default search mode.
  ["suggest.quickactions", false],

  // Whether results will include Suggest suggestions.
  ["suggest.quicksuggest.all", false],

  // Whether results will include sponsored Suggest suggestions. Only relevant
  // if the `all` pref is true.
  ["suggest.quicksuggest.sponsored", false],

  // Whether results will include Realtime suggestion opt-in result.
  ["suggest.realtimeOptIn", true],

  // If `browser.urlbar.recentsearches.featureGate` is true, this controls whether
  // recentsearches are turned on.
  ["suggest.recentsearches", true],

  // Whether results will include synced tab results. The syncing of open tabs
  // must also be enabled, from Sync preferences.
  ["suggest.remotetab", true],

  // Whether results will include search suggestions.
  ["suggest.searches", false],

  // Whether results will include realtime sports suggestions.
  ["suggest.sports", true],

  // Whether results will include top sites and the view will open on focus.
  ["suggest.topsites", true],

  // If `browser.urlbar.trending.featureGate` is true, this controls whether
  // trending suggestions are turned on.
  ["suggest.trending", true],

  // If `browser.urlbar.weather.featureGate` is true, this controls whether
  // weather suggestions are turned on.
  ["suggest.weather", true],

  // If `browser.urlbar.wikipedia.featureGate` is true, this controls whether
  // Wikipedia suggestions are turned on.
  ["suggest.wikipedia", true],

  // If `browser.urlbar.yelp.featureGate` is true, this controls whether
  // Yelp suggestions are turned on.
  ["suggest.yelp", true],

  // If `browser.urlbar.yelpRealtime.featureGate` is true, this controls whether
  // Yelp realtime suggestions are turned on.
  ["suggest.yelpRealtime", true],

  // Whether history results with the same title and URL excluding the ref
  // will be deduplicated.
  ["deduplication.enabled", true],

  // semanticHistory search query minLength threshold to be enabled.
  ["suggest.semanticHistory.minLength", 5],

  // When using switch to tabs, if set to true this will move the tab into the
  // active window.
  ["switchTabs.adoptIntoActiveWindow", false],

  // The minimum number of characters needed to match a tab group name.
  ["tabGroups.minSearchLength", 1],

  // The number of remaining times the user can interact with tab-to-search
  // onboarding results before we stop showing them.
  ["tabToSearch.onboard.interactionsLeft", 3],

  // The number of times the user has been shown the onboarding search tip.
  ["tipShownCount.searchTip_onboard", 0],

  // The number of times the user has been shown the redirect search tip.
  ["tipShownCount.searchTip_redirect", 0],

  // Whether to show the tracker count pill in the urlbar.
  ["trackerCount.enabled", true],

  // Whether we've ever shown the pill in the address bar with the number of trackers (used to trigger a Feature Callout).
  ["trackerCountShown", false],

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

  // Remove leading 'www.' from url displayed in the urlbar.
  ["trimWww", false],

  // Enable the updated design combining the privacy and shield icon
  // and panels in the Urlbar.
  ["trustPanel.featureGate", false],

  // Enable the banner warning the user of breached websites in the trust panel:
  ["trustPanel.breachAlerts", false],

  // Feature gate to show/hide the breach alerts setting in Preferences:
  ["trustPanel.breachAlerts.featureGate", false],

  // Whether unit conversion is enabled.
  ["unitConversion.enabled", false],

  // The index where we show unit conversion results.
  ["unitConversion.suggestedIndex", 1],

  // Untrim url, when urlbar is focused.
  // Note: This pref will be removed once the feature is stable.
  ["untrimOnUserInteraction.featureGate", false],

  // Whether or not Unified Search Button is shown always.
  ["unifiedSearchButton.always", false],

  // Whether or not we show history and other results while in searchMode.
  ["unifiedSearchButton.historyInSearchMode", false],

  // Feature gate pref for weather suggestions in the urlbar.
  ["weather.featureGate", true],

  // The minimum prefix length of a weather keyword the user must type to
  // trigger the suggestion. 0 means the min length should be taken from Nimbus
  // or remote settings.
  ["weather.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for weather suggestions.
  ["weather.showLessFrequentlyCount", 0],

  // Feature gate pref for Wikipedia suggestions (part of Suggest).
  ["wikipedia.featureGate", false],

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

  // Whether to distinguish service type subjects. If true, we show special
  // titile for the suggestion. This is a fallback pref for the
  // `yelpServiceResultDistinction` Nimbus variable.
  ["yelp.serviceResultDistinction", false],

  // The number of times the user has clicked the "Show less frequently" command
  // for Yelp suggestions.
  ["yelp.showLessFrequentlyCount", 0],

  // Feature gate pref for Yelp realtime suggestions in the urlbar.
  ["yelpRealtime.featureGate", false],

  // The minimum prefix length of a Yelp keyword the user must type to trigger
  // the suggestion. 0 means the min length should be taken from Nimbus or remote
  // settings.
  ["yelpRealtime.minKeywordLength", 0],

  // The number of times the user has clicked the "Show less frequently" command
  // for Yelp realtime suggestions.
  ["yelpRealtime.showLessFrequentlyCount", 0],
]);

// This is defined separately to PREF_URLBAR_DEFAULTS, to avoid re-indenting
// the array so that we can preserve blame.
const PREF_URLBAR_DEFAULTS_MAP = new Map(PREF_URLBAR_DEFAULTS);

const PREF_OTHER_DEFAULTS = /** @type {PreferenceDefinition[]} */ ([
  ["browser.fixup.dns_first_for_single_words", false],
  ["browser.ml.enable", false],
  ["browser.nova.enabled", false],
  ["browser.search.openintab", false],
  ["browser.search.suggest.enabled", true],
  ["browser.search.suggest.enabled.private", false],
  ["browser.search.widget.new", true],
  ["browser.settings-redesign.enabled", true],
  ["browser.smartwindow.agent.enabled", false],
  ["browser.smartwindow.smartbarMentions.loglevel", "Error"],
  ["keyword.enabled", true],
  ["privacy.query_stripping.strip_on_share.enabled", true],
  ["security.insecure_connection_text.enabled", true],
  [TelemetryReportingPolicy.TOU_ACCEPTED_DATE_PREF, 0],
  ["ui.popup.disable_autohide", false],
]);

const PREF_OTHER_DEFAULTS_MAP = new Map(PREF_OTHER_DEFAULTS);

// Default values for Nimbus urlbar variables that do not have fallback prefs.
// Variables with fallback prefs do not need to be defined here because their
// defaults are the values of their fallbacks.
const NIMBUS_DEFAULTS = {
  addonsShowLessFrequentlyCap: 0,
  quickSuggestScoreMap: null,
  realtimeMinKeywordLength: null,
  realtimeShowLessFrequentlyCap: null,
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

let inParent =
  Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_DEFAULT;

/**
 * Builds the default result groups and returns the root group.  Result
 * groups determine the composition of results in the muxer, i.e., how they're
 * grouped and sorted.  Each group is an object that looks like this:
 *
 * @typedef {object} ResultGroup
 * @property {Values<typeof lazy.UrlbarShared.RESULT_GROUP>} [group]
 *     This is defined only on groups without children, and it determines the
 *     result group that the group will contain.
 * @property {number} [maxResultCount]
 *     An optional maximum number of results the group can contain.  If it's
 *     not defined and the parent group does not define `flexChildren: true`,
 *     then the max is the parent's max.  If the parent group defines
 *     `flexChildren: true`, then `maxResultCount` is ignored.
 * @property {boolean} [flexChildren]
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
 * @property {number} [flex]
 *     The flex value of the group.  This should be defined only on groups
 *     where the parent defines `flexChildren: true`.  See `flexChildren` for a
 *     discussion of flex.
 * @property {ResultGroup[]} [children]
 *     An array of child group objects.
 * @property {string} [orderBy]
 *     Results should be ordered descending by this payload property.
 *
 * @param {object} options
 * @param {boolean} options.showSearchSuggestionsFirst
 *   If true, the suggestions group will come before the general group.
 * @param {boolean} [options.separateSemanticGroup]
 *   If true, semantic history results get their own group that fills only the
 *   space left after the other general groups.
 */
function makeDefaultResultGroups({
  showSearchSuggestionsFirst,
  separateSemanticGroup = false,
}) {
  /**
   * @type {ResultGroup}
   */
  let rootGroup = {
    children: [
      // heuristic
      {
        maxResultCount: 1,
        children: [
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_EXTENSION },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_SEARCH_TIP },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_OMNIBOX },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_ENGINE_ALIAS },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_BOOKMARK_KEYWORD },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
          {
            group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_TOKEN_ALIAS_ENGINE,
          },
          {
            group:
              lazy.UrlbarShared.RESULT_GROUP
                .HEURISTIC_RESTRICT_KEYWORD_AUTOFILL,
          },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
        ],
      },
      // extensions using the omnibox API
      {
        group: lazy.UrlbarShared.RESULT_GROUP.OMNIBOX,
      },
    ],
  };

  // When semantic history has its own group, split the general slot so that
  // semantic history takes roughly 1 of every 10 of these results (9:1) and
  // otherwise yields to general results, while filling the space when there
  // are no general results. Nesting keeps the general group's weight relative
  // to its siblings (remote tabs, about pages) unchanged.
  let generalChild = separateSemanticGroup
    ? {
        flex: 2,
        flexChildren: true,
        children: [
          {
            flex: 9,
            group: lazy.UrlbarShared.RESULT_GROUP.GENERAL,
            orderBy: "frecency",
          },
          {
            flex: 1,
            group: lazy.UrlbarShared.RESULT_GROUP.SEMANTIC_HISTORY,
            orderBy: "frecency",
          },
        ],
      }
    : {
        flex: 2,
        group: lazy.UrlbarShared.RESULT_GROUP.GENERAL,
        orderBy: "frecency",
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
                group: lazy.UrlbarShared.RESULT_GROUP.FORM_HISTORY,
              },
              {
                flex: 99,
                group: lazy.UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
              },
              {
                flex: 4,
                group: lazy.UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
              },
            ],
          },
          {
            group: lazy.UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
          },
        ],
      },
      // general
      {
        group: lazy.UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        children: [
          {
            availableSpan: 3,
            group: lazy.UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
          },
          {
            flexChildren: true,
            children: [
              {
                flex: 1,
                group: lazy.UrlbarShared.RESULT_GROUP.REMOTE_TAB,
              },
              generalChild,
              {
                // We show relatively many about-page results because they're
                // only added for queries starting with "about:".
                flex: 2,
                group: lazy.UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
              },
              {
                flex: 99,
                group: lazy.UrlbarShared.RESULT_GROUP.RESTRICT_SEARCH_KEYWORD,
              },
            ],
          },
          {
            group: lazy.UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
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
 * @param {object} options
 * @param {boolean} options.showSearchSuggestionsFirst
 *   If true, the suggestions group will come before the general group.
 * @param {boolean} [options.separateSemanticGroup]
 *   If true, semantic history results get their own group that fills only the
 *   space left after the other general groups.
 */
function makeSmartBarGroups({
  showSearchSuggestionsFirst,
  separateSemanticGroup = false,
}) {
  // See makeDefaultResultGroups for the rationale of this 9:1 split.
  let generalChild = separateSemanticGroup
    ? {
        flex: 2,
        flexChildren: true,
        children: [
          {
            flex: 9,
            group: lazy.UrlbarShared.RESULT_GROUP.GENERAL,
            orderBy: "frecency",
          },
          {
            flex: 1,
            group: lazy.UrlbarShared.RESULT_GROUP.SEMANTIC_HISTORY,
            orderBy: "frecency",
          },
        ],
      }
    : {
        flex: 2,
        group: lazy.UrlbarShared.RESULT_GROUP.GENERAL,
        orderBy: "frecency",
      };

  let mainGroup = {
    flexChildren: true,
    children: [
      // search suggestions
      {
        children: [
          {
            availableSpan: 2,
            group: lazy.UrlbarShared.RESULT_GROUP.AI,
          },
          {
            flexChildren: true,
            children: [
              {
                // If `maxHistoricalSearchSuggestions` == 0, the muxer forces
                // `maxResultCount` to be zero and flex is ignored, per query.
                flex: 2,
                group: lazy.UrlbarShared.RESULT_GROUP.FORM_HISTORY,
              },
              {
                flex: 99,
                group: lazy.UrlbarShared.RESULT_GROUP.RECENT_SEARCH,
              },
              {
                flex: 4,
                group: lazy.UrlbarShared.RESULT_GROUP.REMOTE_SUGGESTION,
              },
            ],
          },
          {
            group: lazy.UrlbarShared.RESULT_GROUP.TAIL_SUGGESTION,
          },
        ],
      },
      // general
      {
        group: lazy.UrlbarShared.RESULT_GROUP.GENERAL_PARENT,
        children: [
          {
            availableSpan: 3,
            group: lazy.UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
          },
          {
            flexChildren: true,
            children: [
              generalChild,
              {
                flex: 1,
                group: lazy.UrlbarShared.RESULT_GROUP.REMOTE_TAB,
              },
              {
                // We show relatively many about-page results because they're
                // only added for queries starting with "about:".
                flex: 2,
                group: lazy.UrlbarShared.RESULT_GROUP.ABOUT_PAGES,
              },
            ],
          },
          {
            group: lazy.UrlbarShared.RESULT_GROUP.INPUT_HISTORY,
          },
        ],
      },
    ],
  };

  // The search branch always gets the larger share of results.
  let [searchBranch, generalBranch] = mainGroup.children;
  searchBranch.flex = 2;
  generalBranch.flex = 1;
  if (!showSearchSuggestionsFirst) {
    mainGroup.children.reverse();
  }
  return {
    children: [
      // heuristic
      {
        maxResultCount: 1,
        children: [
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_TEST },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_AUTOFILL },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_HISTORY_URL },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_AI_CHAT },
          { group: lazy.UrlbarShared.RESULT_GROUP.HEURISTIC_FALLBACK },
        ],
      },
      mainGroup,
    ],
  };
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
    for (let pref of PREF_OTHER_DEFAULTS_MAP.keys()) {
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
   * PREF_OTHER_DEFAULTS_MAP Map, specifying their full name.
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
   * PREF_OTHER_DEFAULTS_MAP Map, specifying their full name.
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
   * Adds a value to a preference that handles multiple comma-separated values.
   * Throws an error if the preference does not have a comma-separated value.
   *
   * @param {string} pref
   *   The name of the preference to set.
   * @param {*} value
   *   The preference value.
   */
  add(pref, value) {
    let maybeSet = this._getPrefValue(pref);

    if (!(maybeSet instanceof Set)) {
      throw new Error(
        `The pref ${pref} should handle the values as Set but '${typeof maybeSet}'`
      );
    }

    maybeSet.add(value);
    this.set(pref, [...maybeSet].join(","));
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

  #getShowSearchSuggestionsFirst(context, pref = "showSearchSuggestionsFirst") {
    let showSearchSuggestionsFirst =
      context.searchString ||
      (!this.get("suggest.trending") && !this.get("suggest.recentsearches"));

    let inSearchEngineMode = !!context.searchMode?.engineName;

    // If we're in a case were search suggestions would be shown first, but not
    // in search engine mode, then just use the preference.
    if (!inSearchEngineMode && showSearchSuggestionsFirst) {
      showSearchSuggestionsFirst = this.get(pref);
    }
    return showSearchSuggestionsFirst;
  }

  getResultGroups({ context }) {
    // We try to cache result groups so we don't have to rebuild them each time.
    // This key may be modified per each SAP, and will track the cached groups,
    // any additionally used condition must be added to the key.
    let key = context.sapName;
    let separateSemanticGroup = this.get(
      "suggest.semanticHistory.separateGroup"
    );
    key += separateSemanticGroup;
    switch (context.sapName) {
      case "urlbar": {
        let showSearchSuggestionsFirst =
          this.#getShowSearchSuggestionsFirst(context);
        key += showSearchSuggestionsFirst;
        return this.#getOrCacheResultGroups(key, () =>
          makeDefaultResultGroups({
            showSearchSuggestionsFirst,
            separateSemanticGroup,
          })
        );
      }
      case "searchbar":
      case "newtab_searchbar": {
        // This is a temporary placeholder until searchbar gets its own config.
        return this.#getOrCacheResultGroups(key, () =>
          makeDefaultResultGroups({
            showSearchSuggestionsFirst: true,
            separateSemanticGroup,
          })
        );
      }
      case "smartbar": {
        let showSearchSuggestionsFirst = this.#getShowSearchSuggestionsFirst(
          context,
          "smartbar.showSearchSuggestionsFirst"
        );
        key += showSearchSuggestionsFirst;
        return this.#getOrCacheResultGroups(key, () =>
          makeSmartBarGroups({
            showSearchSuggestionsFirst,
            separateSemanticGroup,
          })
        );
      }
      default: {
        throw new Error(`Unknown SAP name: ${context.sapName}`);
      }
    }
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
   *           passed the name of the changed Nimbus variable and the new value.
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
    if (
      !PREF_URLBAR_DEFAULTS_MAP.has(pref) &&
      !PREF_OTHER_DEFAULTS_MAP.has(pref)
    ) {
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
      case "smartbar.showSearchSuggestionsFirst":
      case "suggest.semanticHistory.separateGroup":
        this.#cachedResultGroups.clear();
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
        this.#notifyObservers("onNimbusChanged", name, newNimbus[name]);
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
      case "quicksuggest.dynamicSuggestionTypes":
      case "quicksuggest.realtimeOptIn.dismissTypes":
      case "quicksuggest.realtimeOptIn.notNowTypes":
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
    let defaultValue = PREF_URLBAR_DEFAULTS_MAP.get(pref);
    if (defaultValue === undefined) {
      branch = Services.prefs;
      defaultValue = PREF_OTHER_DEFAULTS_MAP.get(pref);
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
   * @type {Map<string, ResultGroup>}
   * Result groups cached by search access point and params used to build them.
   */
  #cachedResultGroups = new Map();
  #getOrCacheResultGroups(key, builder) {
    let groups = this.#cachedResultGroups.get(key);
    if (!groups) {
      groups = builder();
      this.#cachedResultGroups.set(key, groups);
    }
    return groups;
  }

  #notifyObservers(method, changed, ...rest) {
    for (let i = 0; i < this._observerWeakRefs.length; ) {
      let observer = this._observerWeakRefs[i].get();
      if (!observer) {
        // The observer has been GC'ed, so remove it from our list.
        this._observerWeakRefs.splice(i, 1);
        continue;
      }

      if (!inParent) {
        // This is necessary for observers created in non-chrome code.
        observer = Cu.waiveXrays(observer);
      }

      if (method in observer) {
        try {
          observer[method](changed, ...rest);
        } catch (ex) {
          console.error(ex);
        }
      }
      ++i;
    }
  }
}

export var UrlbarPrefs = new Preferences();
