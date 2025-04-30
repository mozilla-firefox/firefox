#filter dumbComments emptyLines substitution

// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Non-static prefs that are specific to desktop Firefox belong in this file
// (unless there is a compelling and documented reason for them to belong in
// another file).
//
// Please indent all prefs defined within #ifdef/#ifndef conditions. This
// improves readability, particular for conditional blocks that exceed a single
// screen.

#ifdef XP_UNIX
  #ifndef XP_MACOSX
    #define UNIX_BUT_NOT_MAC
  #endif
#endif

#ifdef XP_MACOSX
  pref("browser.hiddenWindowChromeURL", "chrome://browser/content/hiddenWindowMac.xhtml");
#endif

// Set add-ons abuse report related prefs specific to Firefox Desktop.
pref("extensions.abuseReport.enabled", true);

// Enables some extra Extension System Logging (can reduce performance)
pref("extensions.logging.enabled", false);

// Disables strict compatibility, making addons compatible-by-default.
pref("extensions.strictCompatibility", false);

pref("extensions.webextOptionalPermissionPrompts", true);
// If enabled, install origin permission verification happens after addons are downloaded.
pref("extensions.postDownloadThirdPartyPrompt", true);

// Preferences for AMO integration
pref("extensions.getAddons.cache.enabled", true);
pref("extensions.getAddons.get.url", "https://services.addons.mozilla.org/api/v4/addons/search/?guid=%IDS%&lang=%LOCALE%");
pref("extensions.getAddons.search.browseURL", "https://addons.mozilla.org/%LOCALE%/firefox/search?q=%TERMS%&platform=%OS%&appver=%VERSION%");
pref("extensions.getAddons.link.url", "https://addons.mozilla.org/%LOCALE%/firefox/");
pref("extensions.getAddons.langpacks.url", "https://services.addons.mozilla.org/api/v4/addons/language-tools/?app=firefox&type=language&appversion=%VERSION%");
pref("extensions.getAddons.discovery.api_url", "https://services.addons.mozilla.org/api/v4/discovery/?lang=%LOCALE%&edition=%DISTRIBUTION%");
pref("extensions.getAddons.browserMappings.url", "https://services.addons.mozilla.org/api/v5/addons/browser-mappings/?browser=%BROWSER%");

// The URL for the privacy policy related to recommended extensions.
pref("extensions.recommendations.privacyPolicyUrl", "https://www.mozilla.org/privacy/firefox/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_content=privacy-policy-link#addons");
// The URL for Firefox Color, recommended on the theme page in about:addons.
pref("extensions.recommendations.themeRecommendationUrl", "https://color.firefox.com/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_content=theme-footer-link");

pref("extensions.update.autoUpdateDefault", true);

// Check AUS for system add-on updates.
pref("extensions.systemAddon.update.url", "https://aus5.mozilla.org/update/3/SystemAddons/%VERSION%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/update.xml");
pref("extensions.systemAddon.update.enabled", true);

// Disable add-ons that are not installed by the user in all scopes by default.
// See the SCOPE constants in AddonManager.sys.mjs for values to use here.
pref("extensions.autoDisableScopes", 15);
// Scopes to scan for changes at startup.
pref("extensions.startupScanScopes", 0);

pref("extensions.geckoProfiler.acceptedExtensionIds", "geckoprofiler@mozilla.com,quantum-foxfooding@mozilla.com,raptor@mozilla.org");


pref("extensions.webextensions.remote", true);

// Require signed add-ons by default
pref("extensions.langpacks.signatures.required", true);
pref("xpinstall.signatures.required", true);

// Enable data collection permissions.
#ifdef NIGHTLY_BUILD
  pref("extensions.dataCollectionPermissions.enabled", true);
#else
  pref("extensions.dataCollectionPermissions.enabled", false);
#endif

// Dictionary download preference
pref("browser.dictionaries.download.url", "https://addons.mozilla.org/%LOCALE%/firefox/language-tools/");

// At startup, should we check to see if the installation
// date is older than some threshold
pref("app.update.checkInstallTime", true);

// The number of days a binary is permitted to be old without checking is defined in
// firefox-branding.js (app.update.checkInstallTime.days)

// The minimum delay in seconds for the timer to fire between the notification
// of each consumer of the timer manager.
// minimum=30 seconds, default=120 seconds, and maximum=300 seconds
pref("app.update.timerMinimumDelay", 120);

// The minimum delay in milliseconds for the first firing after startup of the timer
// to notify consumers of the timer manager.
// minimum=10 seconds, default=30 seconds, and maximum=120 seconds
pref("app.update.timerFirstInterval", 30000);

// App-specific update preferences

// The interval to check for updates (app.update.interval) is defined in
// firefox-branding.js

// Enables some extra Application Update Logging (can reduce performance)
pref("app.update.log", false);
// Causes Application Update Logging to be sent to a file in the profile
// directory. This preference is automatically disabled on application start to
// prevent it from being left on accidentally. Turning this pref on enables
// logging, even if app.update.log is false.
pref("app.update.log.file", false);

// The number of general background check failures to allow before notifying the
// user of the failure. User initiated update checks always notify the user of
// the failure.
pref("app.update.backgroundMaxErrors", 10);

// How many times we should let downloads fail before prompting the user to
// download a fresh installer.
pref("app.update.download.maxAttempts", 2);

// How many times we should let an elevation prompt fail before prompting the user to
// download a fresh installer.
pref("app.update.elevate.maxAttempts", 2);

#ifdef NIGHTLY_BUILD
  // Whether to delay popup notifications when an update is available and
  // suppress them when an update is installed and waiting for user to restart.
  // If set to true, these notifications will immediately be shown as banners in
  // the app menu and as badges on the app menu button. Update available
  // notifications will not create popup prompts until a week has passed without
  // the user installing the update. Update restart notifications will not
  // create popup prompts at all. This doesn't affect update notifications
  // triggered by errors/failures or manual install prompts.
  pref("app.update.suppressPrompts", false);
#endif

// If set to true, a message will be displayed in the hamburger menu while
// an update is being downloaded.
pref("app.update.notifyDuringDownload", false);

// If set to true, the Update Service will automatically download updates if the
// user can apply updates. This pref is no longer used on Windows, except as the
// default value to migrate to the new location that this data is now stored
// (which is in a file in the update directory). Because of this, this pref
// should no longer be used directly. Instead, getAppUpdateAutoEnabled and
// getAppUpdateAutoEnabled from UpdateUtils.sys.mjs should be used.
#ifndef XP_WIN
  pref("app.update.auto", true);
#endif

// If set to true, the Update Service will apply updates in the background
// when it finishes downloading them.
pref("app.update.staging.enabled", true);

// Update service URL:
// app.update.url was removed in Bug 1568994
// app.update.url.manual is in branding section
// app.update.url.details is in branding section

// app.update.badgeWaitTime is in branding section
// app.update.interval is in branding section
// app.update.promptWaitTime is in branding section

// Whether or not to attempt using the service for updates.
#ifdef MOZ_MAINTENANCE_SERVICE
  pref("app.update.service.enabled", true);
#endif

#ifdef MOZ_BITS_DOWNLOAD
  // If set to true, the Update Service will attempt to use Windows BITS to
  // download updates and will fallback to downloading internally if that fails.
  pref("app.update.BITS.enabled", true);
#endif

pref("app.update.langpack.enabled", true);

#if defined(MOZ_UPDATE_AGENT)
  pref("app.update.background.loglevel", "error");
  pref("app.update.background.timeoutSec", 600);
  // By default, check for updates when the browser is not running every 7 hours.
  pref("app.update.background.interval", 25200);
  // By default, snapshot Firefox Messaging System targeting for use by the
  // background update task every 60 minutes.
  pref("app.update.background.messaging.targeting.snapshot.intervalSec", 3600);
  // For historical reasons, the background update process requires the Mozilla
  // Maintenance Service to be available and enabled via the service registry
  // key.  When this value is `true`, allow the background update process to
  // update unelevated installations (that are writeable, etc).
  //
  // N.b. This feature impacts the `applications: firefox_desktop` Nimbus
  // application ID (and not the `firefox_desktop_background_task` application
  // ID).  However, the pref will be automatically mirrored to the background
  // update task profile. This means that experiments and enrollment impact the
  // Firefox Desktop browsing profile that _schedules_ the background update
  // task, and then the background update task collects telemetry in accordance
  // with the mirrored pref.
  pref("app.update.background.allowUpdatesForUnelevatedInstallations", false);
#endif

#ifdef XP_MACOSX
  // If set to true, Firefox will automatically restart if it is left running
  // with no browser windows open.
  pref("app.update.noWindowAutoRestart.enabled", true);
  // How long to wait after all browser windows are closed before restarting,
  // in milliseconds. 5 min = 300000 ms
  pref("app.update.noWindowAutoRestart.delayMs", 300000);
#endif

#if defined(MOZ_BACKGROUNDTASKS)
  // The amount of time, in seconds, before background tasks time out and exit.
  // Tasks can override this default (10 minutes).
  pref("toolkit.backgroundtasks.defaultTimeoutSec", 600);

  #if defined(ENABLE_TESTS)
    // Test prefs to verify background tasks inheret and override browser prefs
    // correctly.
    pref("toolkit.backgroundtasks.tests.browserPrefsInherited", 15);
    pref("toolkit.backgroundtasks.tests.browserPrefsOverriden", 16);
  #endif
#endif

// Symmetric (can be overridden by individual extensions) update preferences.
// e.g.
//  extensions.{GUID}.update.enabled
//  extensions.{GUID}.update.url
//  .. etc ..
//
pref("extensions.update.enabled", true);
pref("extensions.update.url", "https://versioncheck.addons.mozilla.org/update/VersionCheck.php?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");
pref("extensions.update.background.url", "https://versioncheck-bg.addons.mozilla.org/update/VersionCheck.php?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");
pref("extensions.update.interval", 86400);  // Check for updates to Extensions and
                                            // Themes every day

pref("lightweightThemes.getMoreURL", "https://addons.mozilla.org/%LOCALE%/firefox/themes");

#if defined(MOZ_WIDEVINE_EME)
  pref("browser.eme.ui.enabled", true);
#else
  pref("browser.eme.ui.enabled", false);
#endif

// UI tour experience.
pref("browser.uitour.enabled", true);
pref("browser.uitour.loglevel", "Error");
pref("browser.uitour.url", "https://www.mozilla.org/%LOCALE%/firefox/%VERSION%/tour/");
// How long to show a Hearbeat survey (two hours, in seconds)
pref("browser.uitour.surveyDuration", 7200);

pref("keyword.enabled", true);

// Fixup whitelists, the urlbar won't try to search for these words, but will
// instead consider them valid TLDs. Don't check these directly, use
// Services.uriFixup.isDomainKnown() instead.
pref("browser.fixup.domainwhitelist.localhost", true);
// https://tools.ietf.org/html/rfc2606
pref("browser.fixup.domainsuffixwhitelist.test", true);
pref("browser.fixup.domainsuffixwhitelist.example", true);
pref("browser.fixup.domainsuffixwhitelist.invalid", true);
pref("browser.fixup.domainsuffixwhitelist.localhost", true);
// https://tools.ietf.org/html/draft-wkumari-dnsop-internal-00
pref("browser.fixup.domainsuffixwhitelist.internal", true);
// https://tools.ietf.org/html/rfc6762
pref("browser.fixup.domainsuffixwhitelist.local", true);

// Whether to always go through the DNS server before sending a single word
// search string, that may contain a valid host, to a search engine.
pref("browser.fixup.dns_first_for_single_words", false);

#ifdef UNIX_BUT_NOT_MAC
  pref("general.autoScroll", false);
#else
  pref("general.autoScroll", true);
#endif

// UI density of the browser chrome. This mostly affects toolbarbutton
// and urlbar spacing. The possible values are 0=normal, 1=compact, 2=touch.
pref("browser.uidensity", 0);
// Whether Firefox will automatically override the uidensity to "touch"
// while the user is in a touch environment (such as Windows tablet mode).
pref("browser.touchmode.auto", true);
// Whether Firefox will show the Compact Mode UIDensity option.
pref("browser.compactmode.show", false);

// At startup, check if we're the default browser and prompt user if not.
pref("browser.shell.checkDefaultBrowser", true);
pref("browser.shell.shortcutFavicons",true);
pref("browser.shell.mostRecentDateSetAsDefault", "");
pref("browser.shell.skipDefaultBrowserCheckOnFirstRun", true);
pref("browser.shell.didSkipDefaultBrowserCheckOnFirstRun", false);
pref("browser.shell.defaultBrowserCheckCount", 0);
#if defined(XP_WIN)
// Attempt to set the default browser on Windows 10 using the UserChoice registry keys,
// before falling back to launching the modern Settings dialog.
pref("browser.shell.setDefaultBrowserUserChoice", true);
// When setting default via UserChoice, temporarily rename an ancestor registry key to
// prevent kernel drivers from locking the UserChoice subkeys.
pref("browser.shell.setDefaultBrowserUserChoice.regRename", true);
// When setting the default browser on Windows 10 using the UserChoice
// registry keys, also try to set Firefox as the default PDF handler.
pref("browser.shell.setDefaultPDFHandler", true);
// When setting Firefox as the default PDF handler (subject to conditions
// above), only set Firefox as the default PDF handler when the existing handler
// is a known browser, and not when existing handler is another PDF handler such
// as Acrobat Reader or Nitro PDF.
pref("browser.shell.setDefaultPDFHandler.onlyReplaceBrowsers", true);
// Whether or not to we are allowed to prompt the user to set Firefox as their
// default PDF handler.
pref("browser.shell.checkDefaultPDF", true);
// Will be set to `true` if the user indicates that they don't want to be asked
// again about Firefox being their default PDF handler any more.
pref("browser.shell.checkDefaultPDF.silencedByUser", false);
// Whether or not the user should be shown the guidance notifications when
// setting Firefox as their default browser.
pref("browser.shell.setDefaultGuidanceNotifications", true);
#endif


// 0 = blank, 1 = home (browser.startup.homepage), 2 = last visited page, 3 = resume previous browser session
// The behavior of option 3 is detailed at: http://wiki.mozilla.org/Session_Restore
pref("browser.startup.page",                1);
pref("browser.startup.homepage",            "about:home");
pref("browser.startup.homepage.abouthome_cache.enabled", true);
pref("browser.startup.homepage.abouthome_cache.loglevel", "Warn");

// Whether we should skip the homepage when opening the first-run page
pref("browser.startup.firstrunSkipsHomepage", true);

// Whether we should show the session-restore infobar on startup
pref("browser.startup.couldRestoreSession.count", 0);

// Show an about:blank window as early as possible for quick startup feedback.
// Held to nightly on Linux due to bug 1450626.
// Disabled on Mac because the bouncing dock icon already provides feedback.
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK) && defined(NIGHTLY_BUILD)
  pref("browser.startup.blankWindow", true);
#else
  pref("browser.startup.blankWindow", false);
#endif

// Show a skeleton UI window prior to loading libxul. Only visible for windows
// users as it is not implemented anywhere else.
#if defined(XP_WIN)
pref("browser.startup.preXulSkeletonUI", true);

// Whether the checkbox to enable Windows launch on login is shown
pref("browser.startup.windowsLaunchOnLogin.enabled", true);
// Whether to show the launch on login infobar notification
pref("browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt", false);
#endif

// Show an upgrade dialog on major upgrades.
pref("browser.startup.upgradeDialog.enabled", false);

pref("browser.chrome.site_icons", true);
// browser.warnOnQuit == false will override all other possible prompts when quitting or restarting
pref("browser.warnOnQuit", true);

// Whether to warn when quitting when using the shortcut key.
#if defined(XP_WIN)
  pref("browser.warnOnQuitShortcut", false);
#else
  pref("browser.warnOnQuitShortcut", true);
#endif

// TODO bug 1702563: Renable fullscreen autohide by default on macOS.
#ifdef XP_MACOSX
  pref("browser.fullscreen.autohide", false);
#else
  pref("browser.fullscreen.autohide", true);
#endif

pref("browser.overlink-delay", 80);

pref("browser.taskbarTabs.enabled", false);

#if defined(MOZ_WIDGET_GTK)
  pref("browser.theme.native-theme", true);
#else
  pref("browser.theme.native-theme", false);
#endif

// Whether using `ctrl` when hitting return/enter in the URL bar
// (or clicking 'go') should prefix 'www.' and suffix
// browser.fixup.alternate.suffix to the URL bar value prior to
// navigating.
pref("browser.urlbar.ctrlCanonizesURLs", true);

// Whether we announce to screen readers when tab-to-search results are
// inserted.
pref("browser.urlbar.accessibility.tabToSearch.announceResults", true);

// Control autoFill behavior
pref("browser.urlbar.autoFill", true);

// Whether enabling adaptive history autofill.
pref("browser.urlbar.autoFill.adaptiveHistory.enabled", false);

// Minimum char length of the user's search string to enable adaptive history
// autofill.
pref("browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0);

// Set default suggest intent threshold value of 0.5
pref("browser.urlbar.intentThreshold", "0.5");

// Set default NER threshold value of 0.5
pref("browser.urlbar.nerThreshold", "0.5");

// Whether to warm up network connections for autofill or search results.
pref("browser.urlbar.speculativeConnect.enabled", true);

// Whether bookmarklets should be filtered out of Address Bar matches.
// This is enabled for security reasons, when true it is still possible to
// search for bookmarklets typing "javascript: " followed by the actual query.
pref("browser.urlbar.filter.javascript", true);

// Focus the content document when pressing the Escape key, if there's no
// remaining typed history.
pref("browser.urlbar.focusContentDocumentOnEsc", true);

// Enable a certain level of urlbar logging to the Browser Console. See
// ConsoleInstance.webidl.
pref("browser.urlbar.loglevel", "Error");

// the maximum number of results to show in autocomplete when doing richResults
pref("browser.urlbar.maxRichResults", 10);

// The maximum number of historical search results to show.
pref("browser.urlbar.maxHistoricalSearchSuggestions", 2);

// The default behavior for the urlbar can be configured to use any combination
// of the match filters with each additional filter adding more results (union).
pref("browser.urlbar.suggest.bookmark",             true);
pref("browser.urlbar.suggest.clipboard",            true);
pref("browser.urlbar.suggest.history",              true);
pref("browser.urlbar.suggest.openpage",             true);
pref("browser.urlbar.suggest.remotetab",            true);
pref("browser.urlbar.suggest.searches",             true);
pref("browser.urlbar.suggest.topsites",             true);
pref("browser.urlbar.suggest.engines",              true);
pref("browser.urlbar.suggest.calculator",           true);
pref("browser.urlbar.suggest.recentsearches",       true);
pref("browser.urlbar.suggest.quickactions",         true);

pref("browser.urlbar.deduplication.enabled", false);
pref("browser.urlbar.deduplication.thresholdDays", 0);

#ifdef EARLY_BETA_OR_EARLIER
pref("browser.urlbar.scotchBonnet.enableOverride", true);
#else
pref("browser.urlbar.scotchBonnet.enableOverride", false);
#endif

// Whether or not Unified Search Button is shown always.
pref("browser.urlbar.unifiedSearchButton.always", false);

// Enable trending suggestions and recent searches.
pref("browser.urlbar.trending.featureGate", true);
pref("browser.urlbar.trending.requireSearchMode", false);
pref("browser.urlbar.recentsearches.featureGate", true);

// Enable Rich Entities.
pref("browser.urlbar.richSuggestions.featureGate", true);
pref("browser.search.param.search_rich_suggestions", "fen");

// Feature gate pref for weather suggestions in the urlbar.
pref("browser.urlbar.weather.featureGate", true);

// Enable clipboard suggestions feature, the pref should be removed once stable.
pref("browser.urlbar.clipboard.featureGate", false);

// The minimum prefix length of a weather keyword the user must type to trigger
// the suggestion. 0 means the min length should be taken from Nimbus or remote
// settings.
pref("browser.urlbar.weather.minKeywordLength", 0);

// The UI treatment to use for weather suggestions. Possible values:
// 0: (default) Simplest UI with current temperature and location
// 1: Simpler UI, adds current conditions, high/low, and URL
// 2: Full UI, adds forecast
pref("browser.urlbar.weather.uiTreatment", 0);

// If `browser.urlbar.weather.featureGate` is true, this controls whether
// weather suggestions are turned on.
pref("browser.urlbar.suggest.weather", true);

// If `browser.urlbar.trending.featureGate` is true, this controls whether
// trending suggestions are turned on.
pref("browser.urlbar.suggest.trending", true);

// Whether non-sponsored quick suggest results are shown in the urlbar. This
// pref is exposed to the user in the UI, and it's sticky so that its
// user-branch value persists regardless of whatever Firefox Suggest scenarios,
// with their various default-branch values, the user is enrolled in over time.
pref("browser.urlbar.suggest.quicksuggest.nonsponsored", false, sticky);

// Whether sponsored quick suggest results are shown in the urlbar. This pref is
// exposed to the user in the UI, and it's sticky so that its user-branch value
// persists regardless of whatever Firefox Suggest scenarios, with their various
// default-branch values, the user is enrolled in over time.
pref("browser.urlbar.suggest.quicksuggest.sponsored", false, sticky);

// Whether data collection is enabled for quick suggest results in the urlbar.
// This pref is exposed to the user in the UI, and it's sticky so that its
// user-branch value persists regardless of whatever Firefox Suggest scenarios,
// with their various default-branch values, the user is enrolled in over time.
pref("browser.urlbar.quicksuggest.dataCollection.enabled", false, sticky);

// Whether the Firefox Suggest contextual opt-in result is enabled.
pref("browser.urlbar.quicksuggest.contextualOptIn", false);

// Number that the user dismissed the Firefox Suggest contextual opt-in result.
pref("browser.urlbar.quicksuggest.contextualOptIn.dismissedCount", 0);

// Period until reshow the Firefox Suggest contextual opt-in result when first dismissed.
pref("browser.urlbar.quicksuggest.contextualOptIn.firstReshowAfterPeriodDays", 7);

// Period until reshow the Firefox Suggest contextual opt-in result when second dismissed.
pref("browser.urlbar.quicksuggest.contextualOptIn.secondReshowAfterPeriodDays", 14);

// Period until reshow the Firefox Suggest contextual opt-in result when third dismissed.
pref("browser.urlbar.quicksuggest.contextualOptIn.thirdReshowAfterPeriodDays", 60);

// Number of impression for the Firefox Suggest contextual opt-in result.
pref("browser.urlbar.quicksuggest.contextualOptIn.impressionCount", 0);

// Limit for impression to dismiss the Firefox Suggest contextual opt-in result.
pref("browser.urlbar.quicksuggest.contextualOptIn.impressionLimit", 20);

// Days until dismiss the Firefox Suggest contextual opt-in result after first impression.
pref("browser.urlbar.quicksuggest.contextualOptIn.impressionDaysLimit", 5);

// Whether the quick suggest feature in the urlbar is enabled.
pref("browser.urlbar.quicksuggest.enabled", false);

// Ranking mode of QuickSuggest. Currently used for relevance ranking
// experimentation. It can be any of "default", "interest", and "random".
pref("browser.urlbar.quicksuggest.rankingMode", "default");

// Whether Firefox Suggest will use the new Rust backend instead of the original
// JS backend.
pref("browser.urlbar.quicksuggest.rustEnabled", true);

// The index of sponsored Firefox Suggest results within the Firefox Suggest
// section when "Show search suggestions ahead of browsing history in address bar
// results" is checked. When not checked, the index is hardcoded as -1. Negative
// indexes are relative to the end of the section.
pref("browser.urlbar.quicksuggest.sponsoredIndex", 0);
pref("browser.urlbar.quicksuggest.nonSponsoredIndex", -1);

// Whether non-sponsored quick suggest results are subject to impression
// frequency caps.
pref("browser.urlbar.quicksuggest.impressionCaps.nonSponsoredEnabled", false);

// Whether sponsored quick suggest results are subject to impression frequency
// caps.
pref("browser.urlbar.quicksuggest.impressionCaps.sponsoredEnabled", false);

// When non-zero, this is the character-count threshold (inclusive) for showing
// AMP suggestions as top picks. If an AMP suggestion is triggered by a keyword
// at least this many characters long, it will be shown as a top pick.
pref("browser.urlbar.quicksuggest.ampTopPickCharThreshold", 5);

// The matching strategy for AMP suggestions. Zero is the usual default
// exact-keyword strategy. Other values are the integers defined on
// `AmpMatchingStrategy` in `RustSuggest.sys.mjs` (corresponding to the
// `AmpMatchingStrategy` enum in the Rust component coerced to a 1-based integer
// value).
pref("browser.urlbar.quicksuggest.ampMatchingStrategy", 0);

// Comma-separated list of Suggest exposure suggestion types to enable.
pref("browser.urlbar.quicksuggest.exposureSuggestionTypes", "");

// Whether Suggest will use the ML backend in addition to Rust.
pref("browser.urlbar.quicksuggest.mlEnabled", false);

// Which Suggest settings to show in the settings UI. See
// `QuickSuggest.SETTINGS_UI` for values.
pref("browser.urlbar.quicksuggest.settingsUi", 0);

// Whether unit conversion is enabled.
#ifdef NIGHTLY_BUILD
pref("browser.urlbar.unitConversion.enabled", true);
#else
pref("browser.urlbar.unitConversion.enabled", false);
#endif

// Whether to show search suggestions before general results like history and
// bookmarks.
pref("browser.urlbar.showSearchSuggestionsFirst", true);

// As a user privacy measure, don't fetch search suggestions if a pasted string
// is longer than this.
pref("browser.urlbar.maxCharsForSearchSuggestions", 100);

pref("browser.urlbar.trimURLs", true);
pref("browser.urlbar.trimHttps", false);
pref("browser.urlbar.untrimOnUserInteraction.featureGate", false);

// If changed to true, copying the entire URL from the location bar will put the
// human readable (percent-decoded) URL on the clipboard.
pref("browser.urlbar.decodeURLsOnCopy", false);

// Whether or not to move tabs into the active window when using the "Switch to
// Tab" feature of the awesomebar.
pref("browser.urlbar.switchTabs.adoptIntoActiveWindow", false);

// Controls whether searching for open tabs returns tabs from any container
// or only from the current container.
pref("browser.urlbar.switchTabs.searchAllContainers", true);

// Whether addresses and search results typed into the address bar
// should be opened in new tabs by default.
pref("browser.urlbar.openintab", false);

// Allow the result menu button to be reached with the Tab key.
pref("browser.urlbar.resultMenu.keyboardAccessible", true);

// If true, we show tail suggestions when available.
pref("browser.urlbar.richSuggestions.tail", true);

// If true, top sites may include sponsored ones.
pref("browser.urlbar.sponsoredTopSites", false);

// Global toggle for whether the show search terms feature
// can be used at all, and enabled/disabled by the user.
pref("browser.urlbar.showSearchTerms.featureGate", false);

// If true, show the search term in the Urlbar while on
// a default search engine results page.
pref("browser.urlbar.showSearchTerms.enabled", true);

// Whether the urlbar displays one-offs to filter searches to history,
// bookmarks, or tabs.
pref("browser.urlbar.shortcuts.bookmarks", true);
pref("browser.urlbar.shortcuts.tabs", true);
pref("browser.urlbar.shortcuts.history", true);

// When we send events to Urlbar extensions, we wait this amount of time in
// milliseconds for them to respond before timing out.
pref("browser.urlbar.extension.timeout", 400);

// Controls when to DNS resolve single word search strings, after they were
// searched for. If the string is resolved as a valid host, show a
// "Did you mean to go to 'host'" prompt.
// 0 - never resolve; 1 - use heuristics (default); 2 - always resolve
pref("browser.urlbar.dnsResolveSingleWordsAfterSearch", 0);

// Whether the results panel should be kept open during IME composition.
// The default value is false because some IME open a picker panel, and we end
// up with two panels on top of each other. Since for now we can't detect that
// we leave this choice to the user, hopefully in the future this can be flipped
// for everyone.
pref("browser.urlbar.keepPanelOpenDuringImeComposition", false);

// Whether Firefox Suggest group labels are shown in the urlbar view.
pref("browser.urlbar.groupLabels.enabled", true);

// The Merino endpoint URL, not including parameters.
pref("browser.urlbar.merino.endpointURL", "https://merino.services.mozilla.com/api/v1/suggest");

// Timeout for Merino fetches (ms).
pref("browser.urlbar.merino.timeoutMs", 200);

// Comma-separated list of providers to request from Merino
pref("browser.urlbar.merino.providers", "");

// Comma-separated list of client variants to send to Merino
pref("browser.urlbar.merino.clientVariants", "");

// Enable site specific search result.
pref("browser.urlbar.contextualSearch.enabled", true);

// Feature gate pref for addon suggestions in the urlbar.
pref("browser.urlbar.addons.featureGate", true);

// If `browser.urlbar.addons.featureGate` is true, this controls whether
// addons suggestions are turned on.
pref("browser.urlbar.suggest.addons", true);

// If `browser.urlbar.mdn.featureGate` is true, this controls whether
// mdn suggestions are turned on.
pref("browser.urlbar.suggest.mdn", true);

// Feature gate pref for Yelp suggestions in the urlbar.
pref("browser.urlbar.yelp.featureGate", true);

// The minimum prefix length of a Yelp keyword the user must type to trigger the
// suggestion. 0 means the min length should be taken from Nimbus.
pref("browser.urlbar.yelp.minKeywordLength", 4);

// Whether Yelp suggestions should be shown as top picks.
pref("browser.urlbar.yelp.priority", false);

// Whether Yelp suggestions will be served from the Suggest ML backend instead
// of Rust.
pref("browser.urlbar.yelp.mlEnabled", false);

// If `browser.urlbar.yelp.featureGate` is true, this controls whether
// Yelp suggestions are turned on.
pref("browser.urlbar.suggest.yelp", true);

// Feature gate pref for Fakespot suggestions in the urlbar.
pref("browser.urlbar.fakespot.featureGate", false);

// The minimum prefix length of a Fakespot keyword the user must type to trigger
// the suggestion. 0 means the min length should be taken from Nimbus.
pref("browser.urlbar.fakespot.minKeywordLength", 4);

// The index of Fakespot results within the Firefox Suggest section. A negative
// index is relative to the end of the section.
pref("browser.urlbar.fakespot.suggestedIndex", -1);

// If `browser.urlbar.fakespot.featureGate` is true, this controls whether
// Fakespot suggestions are turned on.
pref("browser.urlbar.suggest.fakespot", true);

// The minimum prefix length of addons keyword the user must type to trigger
// the suggestion. 0 means the min length should be taken from Nimbus.
pref("browser.urlbar.addons.minKeywordLength", 0);

// Feature gate pref for Pocket suggestions in the urlbar.
pref("browser.urlbar.pocket.featureGate", false);

// If `browser.urlbar.pocket.featureGate` is true, this controls whether Pocket
// suggestions are turned on.
pref("browser.urlbar.suggest.pocket", true);

pref("browser.altClickSave", false);

// Enable logging downloads operations to the Console.
pref("browser.download.loglevel", "Error");

// Number of milliseconds to wait for the http headers (and thus
// the Content-Disposition filename) before giving up and falling back to
// picking a filename without that info in hand so that the user sees some
// feedback from their action.
pref("browser.download.saveLinkAsFilenameTimeout", 4000);

pref("browser.download.useDownloadDir", true);
pref("browser.download.folderList", 1);
pref("browser.download.manager.addToRecentDocs", true);
pref("browser.download.manager.resumeOnWakeDelay", 10000);

// This records whether or not the panel has been shown at least once.
pref("browser.download.panel.shown", false);

// This records whether or not to show the 'Open in system viewer' context menu item when appropriate
pref("browser.download.openInSystemViewerContextMenuItem", true);

// This records whether or not to show the 'Always open...' context menu item when appropriate
pref("browser.download.alwaysOpenInSystemViewerContextMenuItem", true);

// Open downloaded file types internally for the given types.
// This is a comma-separated list, the empty string ("") means no types are
// viewable internally.
pref("browser.download.viewableInternally.enabledTypes", "xml,svg,webp,avif,jxl");


// This controls whether the button is automatically shown/hidden depending
// on whether there are downloads to show.
pref("browser.download.autohideButton", true);

// Controls whether to open the downloads panel every time a download begins.
// The first download ever run in a new profile will still open the panel.
pref("browser.download.alwaysOpenPanel", true);

// Determines the behavior of the "Delete" item in the downloads context menu.
// Valid values are 0, 1, and 2.
//   0 - Don't remove the download from session list or history.
//   1 - Remove the download from session list, but not history.
//   2 - Remove the download from both session list and history.
pref("browser.download.clearHistoryOnDelete", 0);

#ifndef XP_MACOSX
  pref("browser.helperApps.deleteTempFileOnExit", true);
#endif

// This controls the visibility of the radio button in the
// Unknown Content Type (Helper App) dialog that will open
// the content in the browser for PDF and for other
// Viewable Internally types
// (see browser.download.viewableInternally.enabledTypes)
pref("browser.helperApps.showOpenOptionForPdfJS", true);
pref("browser.helperApps.showOpenOptionForViewableInternally", true);

// search engines URL
pref("browser.search.searchEnginesURL",      "https://addons.mozilla.org/%LOCALE%/firefox/search-engines/");

// search bar results always open in a new tab
pref("browser.search.openintab", false);

// context menu searches open in the foreground
pref("browser.search.context.loadInBackground", false);

// Enables display of the options for the user using a separate default search
// engine in private browsing mode.
pref("browser.search.separatePrivateDefault.ui.enabled", false);
// The maximum amount of times the private default banner is shown.
pref("browser.search.separatePrivateDefault.ui.banner.max", 0);

// Enables search SERP telemetry page categorization.
pref("browser.search.serpEventTelemetryCategorization.enabled", true);

// A count of Glean SERP categorization event metrics that have been recorded
// but not yet submitted in a ping. Needed to prevent sending a ping with only
// experiment info (and not actual SERP categorizations) at startup.
pref("browser.search.serpMetricsRecordedCounter", 0);

// Search Bar removal from the toolbar for users who haven’t used it in 120
// days
pref("browser.search.widget.removeAfterDaysUnused", 120);

// The number of times the search function in the URL bar has been used,
// capped at 100.
pref("browser.search.totalSearches", 0);

// Enables the Review Checker feature in the Shopping sidebar.
// There are separate controls for user opt-in/opt-out.
pref("browser.shopping.experience2023.enabled", false);

// Ternary int-valued pref indicating if the user has opted into the new
// experimental shopping feature.
// 0 means the user has not opted in or out.
// 1 means the user has opted in.
// 2 means the user has opted out.
pref("browser.shopping.experience2023.optedIn", 0);

// Activates the new experimental shopping sidebar.
// True by default. This is handled by ShoppingUtils.handleAutoActivateOnProduct
// to auto-activate the sidebar for non-opted-in users up to 2 times.
pref("browser.shopping.experience2023.active", true);

// Enables the ad / recommended product feature for the shopping sidebar.
// If enabled, users can disable the ad card using the separate pref
// `browser.shopping.experience2023.ads.userEnabled` and visible toggle
// (this is just the feature flag).
pref("browser.shopping.experience2023.ads.enabled", false);

// Activates the ad card in the shopping sidebar.
// Unlike `browser.shopping.experience2023.ads.enabled`, this pref is controlled by users
// using the visible toggle.
pref("browser.shopping.experience2023.ads.userEnabled", true);

// Saves if shopping survey is enabled.
pref("browser.shopping.experience2023.survey.enabled", true);

// Saves if shopping survey is seen.
pref("browser.shopping.experience2023.survey.hasSeen", false);

// Number of PDP visits used to display shopping survey
pref("browser.shopping.experience2023.survey.pdpVisits", 0);

// Enables the auto-open feature for the shopping sidebar,
// including new callouts and settings UI changes
// (this is just the feature flag).
pref("browser.shopping.experience2023.autoOpen.enabled", false);

// Opens the shopping sidebar automatically when viewing a PDP.
pref("browser.shopping.experience2023.autoOpen.userEnabled", true);

// Number of times the sidebar has been closed in a session
pref("browser.shopping.experience2023.sidebarClosedCount", 0);

// When conditions are met, shows a prompt on the shopping sidebar asking users if they want to disable auto-open behavior
pref("browser.shopping.experience2023.showKeepSidebarClosedMessage", true);

// Integrates the Review Checker feature into the global sidebar.
// `enabled` pref should be opposite of this to disable
// the custom shopping sidebar.
pref("browser.shopping.experience2023.integratedSidebar", false);

// Adds the Review Checker tool to the sidebar if the integratedSidebar pref is enabled.
pref("sidebar.newTool.migration.reviewchecker", '{"visibilityPref": "browser.shopping.experience2023.integratedSidebar"}');

// If true, users have already seen a card in the Review Checker sidebar panel
// notifying users of the feature's new location and asking if they want to
// move the sidebar to the left or right side. Else if false, users are yet to
// see the card.
pref("browser.shopping.experience2023.newPositionCard.hasSeen", false);

// Spin the cursor while the page is loading
pref("browser.spin_cursor_while_busy", false);

// Enable display of contextual-password-manager option in browser sidebar
pref("browser.contextual-password-manager.enabled", false);

// Enables the display of the Mozilla VPN banner in private browsing windows
pref("browser.privatebrowsing.vpnpromourl", "https://vpn.mozilla.org/?utm_source=firefox-browser&utm_medium=firefox-%CHANNEL%-browser&utm_campaign=private-browsing-vpn-link");

// Whether the user has opted-in to recommended settings for data features.
pref("browser.dataFeatureRecommendations.enabled", false);

// Use dark theme variant for PBM windows. This is only supported if the theme
// sets darkTheme data.
pref("browser.theme.dark-private-windows", true);

// Pref to control whether or not Private Browsing windows show up
// as separate icons in the Windows taskbar.
pref("browser.privateWindowSeparation.enabled", true);

// Controls visibility of the privacy segmentation preferences section.
pref("browser.privacySegmentation.preferences.show", false);

pref("browser.sessionhistory.max_entries", 50);

// Built-in default permissions.
pref("permissions.manager.defaultsUrl", "resource://app/defaults/permissions");

// Set default fallback values for site permissions we want
// the user to be able to globally change.
pref("permissions.default.camera", 0);
pref("permissions.default.microphone", 0);
pref("permissions.default.geo", 0);
pref("permissions.default.xr", 0);
pref("permissions.default.desktop-notification", 0);
pref("permissions.default.shortcuts", 0);

pref("permissions.desktop-notification.postPrompt.enabled", true);
pref("permissions.desktop-notification.notNow.enabled", false);

pref("permissions.fullscreen.allowed", false);

#ifdef MOZ_WEBRTC
  // When users grant camera or microphone through a permission prompt
  // and leave "☐ Remember this decision" unchecked, Gecko persists
  // their choice to "Always ask" for this permission going forward.
  // This is exposed to websites through the permissions.query() API
  // as "granted", to ward off well-meaning attempts to further escalate
  // permission to always grant, to help sites respect this user choice.
  //
  // By default, these permissions are only visible in Tools / Page Info.
  // But turning this pref on also makes them show up as "Always Ask ✖"
  // in the more prominent site permissions dropdown.
  pref("permissions.media.show_always_ask.enabled", false);
#endif

// Force external link opens into the default user context ID instead of guessing
// the most appropriate one based on the URL (https://bugzilla.mozilla.org/show_bug.cgi?id=1874599#c8)
pref("browser.link.force_default_user_context_id_for_external_opens", false);

// handle links targeting new windows
// 1=current window/tab, 2=new window, 3=new tab in most recent window
pref("browser.link.open_newwindow", 3);

// handle external links (i.e. links opened from a different application)
// default: use browser.link.open_newwindow
// 1-3: see browser.link.open_newwindow for interpretation
pref("browser.link.open_newwindow.override.external", -1);

// 0: no restrictions - divert everything
// 1: don't divert window.open at all
// 2: don't divert window.open with features
pref("browser.link.open_newwindow.restriction", 2);

// If true, this pref causes windows opened by window.open to be forced into new
// tabs (rather than potentially opening separate windows, depending on
// window.open arguments) when the browser is in fullscreen mode.
// We set this differently on Mac because the fullscreen implementation there is
// different.
#ifdef XP_MACOSX
  pref("browser.link.open_newwindow.disabled_in_fullscreen", true);
#else
  pref("browser.link.open_newwindow.disabled_in_fullscreen", false);
#endif

// Tabbed browser
pref("browser.tabs.closeTabByDblclick", false);
pref("browser.tabs.closeWindowWithLastTab", true);
pref("browser.tabs.allowTabDetach", true);
// Open related links to a tab, e.g., link in current tab, at next to the
// current tab if |insertRelatedAfterCurrent| is true.  Otherwise, always
// append new tab to the end.
pref("browser.tabs.insertRelatedAfterCurrent", true);
// Open all links, e.g., bookmarks, history items at next to current tab
// if |insertAfterCurrent| is true.  Otherwise, append new tab to the end
// for non-related links. Note that if this is set to true, it will trump
// the value of browser.tabs.insertRelatedAfterCurrent.
pref("browser.tabs.insertAfterCurrent", false);
// When |insertRelatedAfterCurrent| is true opening a link from a pinned tab
// results in the tabbar scrolling back to the beginning. Setting
// |insertAfterCurrentExceptPinned| to true will add tabs at the end of the
// tabbar.
pref("browser.tabs.insertAfterCurrentExceptPinned", false);
pref("browser.tabs.warnOnClose", false);
pref("browser.tabs.warnOnCloseOtherTabs", true);
pref("browser.tabs.warnOnOpen", true);
pref("browser.tabs.maxOpenBeforeWarn", 15);
pref("browser.tabs.loadDivertedInBackground", false);
pref("browser.tabs.loadBookmarksInBackground", false);
pref("browser.tabs.loadBookmarksInTabs", false);
pref("browser.tabs.tabClipWidth", 140);
pref("browser.tabs.tabMinWidth", 76);
// Users running in any of the following language codes will have the
// secondary text on tabs hidden due to size constraints and readability
// of the text at small font sizes.
pref("browser.tabs.secondaryTextUnsupportedLocales", "ar,bn,bo,ckb,fa,gu,he,hi,ja,km,kn,ko,lo,mr,my,ne,pa,si,ta,te,th,ur,zh");

// When tabs opened by links in other tabs via a combination of
// browser.link.open_newwindow being set to 3 and target="_blank" etc are
// closed:
// true   return to the tab that opened this tab (its owner)
// false  return to the adjacent tab (old default)
pref("browser.tabs.selectOwnerOnClose", true);

// This should match Chromium's audio indicator delay.
pref("browser.tabs.delayHidingAudioPlayingIconMS", 3000);

// Pref to control whether we use a separate privileged content process
// for about: pages. This pref name did not age well: we will have multiple
// types of privileged content processes, each with different privileges.
// types of privleged content processes, each with different privleges.
pref("browser.tabs.remote.separatePrivilegedContentProcess", true);

#if defined(NIGHTLY_BUILD) && !defined(MOZ_ASAN)
  // This pref will cause assertions when a remoteType triggers a process switch
  // to a new remoteType it should not be able to trigger.
  pref("browser.tabs.remote.enforceRemoteTypeRestrictions", true);
#endif

// Pref to control whether we use a separate privileged content process
// for certain mozilla webpages (which are listed in the pref
// browser.tabs.remote.separatedMozillaDomains).
pref("browser.tabs.remote.separatePrivilegedMozillaWebContentProcess", true);

#ifdef NIGHTLY_BUILD
pref("browser.tabs.tooltipsShowPidAndActiveness", true);
#else
pref("browser.tabs.tooltipsShowPidAndActiveness", false);
#endif

pref("browser.tabs.hoverPreview.enabled", true);
pref("browser.tabs.hoverPreview.showThumbnails", true);

pref("browser.tabs.groups.enabled", true);

#ifdef NIGHTLY_BUILD
pref("browser.tabs.groups.smart.enabled", true);
#else
pref("browser.tabs.groups.smart.enabled", false);
#endif

// KMEANS_WITH_ANCHOR or NEAREST_NEIGHBOR
pref("browser.tabs.groups.smart.suggestOtherTabsMethod", "NEAREST_NEIGHBOR");

pref("browser.tabs.groups.smart.optin", false);

pref("browser.tabs.dragDrop.createGroup.delayMS", 240);
pref("browser.tabs.dragDrop.expandGroup.delayMS", 350);
pref("browser.tabs.dragDrop.selectTab.delayMS", 350);
pref("browser.tabs.dragDrop.moveOverThresholdPercent", 80);

pref("browser.tabs.firefox-view.logLevel", "Warn");

pref("browser.tabs.groups.smart.userEnabled", true);

// allow_eval_* is enabled on Firefox Desktop only at this
// point in time
pref("security.allow_eval_with_system_principal", false);
pref("security.allow_eval_in_parent_process", false);

pref("security.allow_parent_unrestricted_js_loads", false);

// Unload tabs when available memory is running low
#if defined(XP_MACOSX) || defined(XP_WIN)
    pref("browser.tabs.unloadOnLowMemory", true);
#else
    pref("browser.tabs.unloadOnLowMemory", false);
#endif

// Tab Unloader does not unload tabs whose last inactive period is longer than
// this value (in milliseconds).
pref("browser.tabs.min_inactive_duration_before_unload", 600000);

// Does middleclick paste of clipboard to new tab button
#ifdef UNIX_BUT_NOT_MAC
pref("browser.tabs.searchclipboardfor.middleclick", true);
#else
pref("browser.tabs.searchclipboardfor.middleclick", false);
#endif

#if defined(XP_MACOSX)
  // During low memory periods, poll with this frequency (milliseconds)
  // until memory is no longer low. Changes to the pref take effect immediately.
  // Browser restart not required. Chosen to be consistent with the windows
  // implementation, but otherwise the 10s value is arbitrary.
  pref("browser.lowMemoryPollingIntervalMS", 10000);

  // Pref to control the reponse taken on macOS when the OS is under memory
  // pressure. Changes to the pref take effect immediately. Browser restart not
  // required. The pref value is a bitmask:
  // 0x0: No response (other than recording for telemetry, crash reporting)
  // 0x1: Use the tab unloading feature to reduce memory use. Requires that
  //      the above "browser.tabs.unloadOnLowMemory" pref be set to true for tab
  //      unloading to occur.
  // 0x2: Issue the internal "memory-pressure" notification to reduce memory use
  // 0x3: Both 0x1 and 0x2.
  #if defined(NIGHTLY_BUILD)
  pref("browser.lowMemoryResponseMask", 3);
  #else
  pref("browser.lowMemoryResponseMask", 0);
  #endif

  // Controls which macOS memory-pressure level triggers the browser low memory
  // response. Changes to the pref take effect immediately. Browser restart not
  // required. By default, use the "critical" level as that occurs after "warn"
  // and we only want to trigger the low memory reponse when necessary.
  // The macOS system memory-pressure level is either none, "warn", or
  // "critical". The OS notifies the browser when the level changes. A false
  // value for the pref indicates the low memory response should occur when
  // reaching the "critical" level. A true value indicates the response should
  // occur when reaching the "warn" level.
  pref("browser.lowMemoryResponseOnWarn", false);
#endif

pref("browser.ctrlTab.sortByRecentlyUsed", false);

// By default, do not export HTML at shutdown.
// If true, at shutdown the bookmarks in your menu and toolbar will
// be exported as HTML to the bookmarks.html file.
pref("browser.bookmarks.autoExportHTML",          false);

// The maximum number of daily bookmark backups to
// keep in {PROFILEDIR}/bookmarkbackups. Special values:
// -1: unlimited
//  0: no backups created (and deletes all existing backups)
pref("browser.bookmarks.max_backups",             15);

// Whether menu should close after Ctrl-click, middle-click, etc.
pref("browser.bookmarks.openInTabClosesMenu", true);

// Where new bookmarks go by default.
// Use PlacesUIUtils.defaultParentGuid to read this; do NOT read the pref
// directly.
// The value is one of:
// - a bookmarks guid
// - "toolbar", "menu" or "unfiled" for those folders.
// If we use the pref but the value isn't any of these, we'll fall back to
// the bookmarks toolbar as a default.
pref("browser.bookmarks.defaultLocation", "toolbar");

pref("browser.tabs.allow_transparent_browser", false);

// Scripts & Windows prefs
pref("dom.disable_open_during_load",              true);

// allow JS to move and resize existing windows
pref("dom.disable_window_move_resize",            false);
// prevent JS from monkeying with window focus, etc
pref("dom.disable_window_flip",                   true);

pref("privacy.popups.showBrowserMessage",   true);

pref("privacy.clearOnShutdown.history",     true);
pref("privacy.clearOnShutdown.formdata",    true);
pref("privacy.clearOnShutdown.downloads",   true);
pref("privacy.clearOnShutdown.cookies",     true);
pref("privacy.clearOnShutdown.cache",       true);
pref("privacy.clearOnShutdown.sessions",    true);
pref("privacy.clearOnShutdown.offlineApps", false);
pref("privacy.clearOnShutdown.siteSettings", false);
pref("privacy.clearOnShutdown.openWindows", false);
// Clear on shutdown prefs used in the new dialog

// We can't remove the old pref yet since we need to use it to migrate the old
// pref values to the new pref values.
pref("privacy.clearOnShutdown_v2.historyFormDataAndDownloads", true);

pref("privacy.clearOnShutdown_v2.browsingHistoryAndDownloads", true);
pref("privacy.clearOnShutdown_v2.cookiesAndStorage", true);
pref("privacy.clearOnShutdown_v2.cache", true);
pref("privacy.clearOnShutdown_v2.siteSettings", false);
pref("privacy.clearOnShutdown_v2.formdata", false);

pref("privacy.cpd.history",                 true);
pref("privacy.cpd.formdata",                true);
pref("privacy.cpd.passwords",               false);
pref("privacy.cpd.downloads",               true);
pref("privacy.cpd.cookies",                 true);
pref("privacy.cpd.cache",                   true);
pref("privacy.cpd.sessions",                true);
pref("privacy.cpd.offlineApps",             false);
pref("privacy.cpd.siteSettings",            false);
pref("privacy.cpd.openWindows",             false);

// clearHistory and clearSiteData pref branches are used to
// remember user pref options based on the two different entry points
pref("privacy.clearHistory.historyFormDataAndDownloads", true);
pref("privacy.clearHistory.browsingHistoryAndDownloads", true);
pref("privacy.clearHistory.cookiesAndStorage", true);
pref("privacy.clearHistory.cache", true);
pref("privacy.clearHistory.siteSettings", false);
pref("privacy.clearHistory.formdata", false);
pref("privacy.clearSiteData.historyFormDataAndDownloads", false);
pref("privacy.clearSiteData.browsingHistoryAndDownloads", false);
pref("privacy.clearSiteData.cookiesAndStorage", true);
pref("privacy.clearSiteData.cache", true);
pref("privacy.clearSiteData.siteSettings", false);
pref("privacy.clearSiteData.formdata", false);

pref("privacy.history.custom",              false);

// What default should we use for the time span in the sanitizer:
// 0 - Clear everything
// 1 - Last Hour
// 2 - Last 2 Hours
// 3 - Last 4 Hours
// 4 - Today
// 5 - Last 5 minutes
// 6 - Last 24 hours
pref("privacy.sanitize.timeSpan", 1);

pref("privacy.sanitize.useOldClearHistoryDialog", false);

pref("privacy.sanitize.clearOnShutdown.hasMigratedToNewPrefs2", false);
pref("privacy.sanitize.clearOnShutdown.hasMigratedToNewPrefs3", false);
// flags to track migration of clear history dialog prefs, where cpd stands for
// clear private data
pref("privacy.sanitize.cpd.hasMigratedToNewPrefs2", false);
pref("privacy.sanitize.cpd.hasMigratedToNewPrefs3", false);

pref("privacy.panicButton.enabled",         true);

// Time until temporary permissions expire, in ms
pref("privacy.temporary_permission_expire_time_ms",  3600000);

// Enables protection mechanism against password spoofing for cross domain auth requests
// See bug 791594
pref("privacy.authPromptSpoofingProtection",         true);

// Enable GPC if the user turns it on in about:preferences
pref("privacy.globalprivacycontrol.functionality.enabled",  true);

// Enable GPC in private browsing mode
pref("privacy.globalprivacycontrol.pbmode.enabled", true);

pref("network.proxy.share_proxy_settings",  false); // use the same proxy settings for all protocols

// simple gestures support
pref("browser.gesture.swipe.left", "Browser:BackOrBackDuplicate");
pref("browser.gesture.swipe.right", "Browser:ForwardOrForwardDuplicate");
pref("browser.gesture.swipe.up", "cmd_scrollTop");
pref("browser.gesture.swipe.down", "cmd_scrollBottom");
pref("browser.gesture.pinch.latched", false);
pref("browser.gesture.pinch.threshold", 25);
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK)
  // Enabled for touch input display zoom.
  pref("browser.gesture.pinch.out", "cmd_fullZoomEnlarge");
  pref("browser.gesture.pinch.in", "cmd_fullZoomReduce");
  pref("browser.gesture.pinch.out.shift", "cmd_fullZoomReset");
  pref("browser.gesture.pinch.in.shift", "cmd_fullZoomReset");
#else
  // Disabled by default due to issues with track pad input.
  pref("browser.gesture.pinch.out", "");
  pref("browser.gesture.pinch.in", "");
  pref("browser.gesture.pinch.out.shift", "");
  pref("browser.gesture.pinch.in.shift", "");
#endif
pref("browser.gesture.twist.latched", false);
pref("browser.gesture.twist.threshold", 0);
pref("browser.gesture.twist.right", "cmd_gestureRotateRight");
pref("browser.gesture.twist.left", "cmd_gestureRotateLeft");
pref("browser.gesture.twist.end", "cmd_gestureRotateEnd");
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK)
  pref("browser.gesture.tap", "cmd_fullZoomReset");
#else
  pref("browser.gesture.tap", "");
#endif

pref("browser.history_swipe_animation.disabled", false);

// 0: Nothing happens
// 1: Scrolling contents
// 2: Go back or go forward, in your history
// 3: Zoom in or out (reflowing zoom).
// 4: Treat vertical wheel as horizontal scroll
// 5: Zoom in or out (pinch zoom).
#ifdef XP_MACOSX
  // On macOS, if the wheel has one axis only, shift+wheel comes through as a
  // horizontal scroll event. Thus, we can't assign anything other than normal
  // scrolling to shift+wheel.
  pref("mousewheel.with_shift.action", 1);
  pref("mousewheel.with_alt.action", 2);
  pref("mousewheel.with_control.action", 1);
#else
  // On the other platforms (non-macOS), user may use legacy mouse which
  // supports only vertical wheel but want to scroll horizontally.  For such
  // users, we should provide horizontal scroll with shift+wheel (same as
  // Chrome). However, shift+wheel was used for navigating history.  For users
  // who want to keep using this feature, let's enable it with alt+wheel.  This
  // is better for consistency with macOS users.
  pref("mousewheel.with_shift.action", 4);
  pref("mousewheel.with_alt.action", 2);
#endif

pref("mousewheel.with_meta.action", 1);

pref("browser.xul.error_pages.expert_bad_cert", false);
pref("browser.xul.error_pages.show_safe_browsing_details_on_load", false);

// Enable captive portal detection.
pref("network.captive-portal-service.enabled", true);

// If true, network link events will change the value of navigator.onLine
pref("network.manage-offline-status", true);

// We want to make sure mail URLs are handled externally...
pref("network.protocol-handler.external.mailto", true); // for mail

// ...without warning dialogs
pref("network.protocol-handler.warn-external.mailto", false);

// By default, all protocol handlers are exposed.  This means that
// the browser will respond to openURL commands for all URL types.
// It will also try to open link clicks inside the browser before
// failing over to the system handlers.
pref("network.protocol-handler.expose-all", true);
pref("network.protocol-handler.expose.mailto", false);
pref("network.protocol-handler.expose.news", false);
pref("network.protocol-handler.expose.snews", false);
pref("network.protocol-handler.expose.nntp", false);

pref("accessibility.typeaheadfind", false);
pref("accessibility.typeaheadfind.timeout", 5000);
pref("accessibility.typeaheadfind.linksonly", false);
pref("accessibility.typeaheadfind.flashBar", 1);

// Whether we can show the "Firefox Labs" section.
pref("browser.preferences.experimental", true);
// Whether we had to hide the "Firefox Labs" section because it would be empty.
pref("browser.preferences.experimental.hidden", false);
// Whether we show the "More from Mozilla" section.
pref("browser.preferences.moreFromMozilla", true);

// Used by settings to track whether the user customized advanced
// performance settings. Not used directly elsewhere.
// If set to false, we show advanced performance settings.
// If reset to true **by checking the box in the settings page**,
// we will revert those to the default settings.
pref("browser.preferences.defaultPerformanceSettings.enabled", true);

pref("browser.proton.toolbar.version", 0);

// Backspace and Shift+Backspace behavior
// 0 goes Back/Forward
// 1 act like PgUp/PgDown
// 2 and other values, nothing
pref("browser.backspace_action", 2);

pref("intl.regional_prefs.use_os_locales", false);

pref("browser.send_pings", false);

pref("browser.geolocation.warning.infoURL", "https://www.mozilla.org/%LOCALE%/firefox/geolocation/");
pref("browser.xr.warning.infoURL", "https://www.mozilla.org/%LOCALE%/firefox/xr/");

pref("browser.sessionstore.resume_from_crash", true);
pref("browser.sessionstore.resume_session_once", false);
pref("browser.sessionstore.resuming_after_os_restart", false);

// Toggle for the behavior to include closed tabs from all windows in
// recently-closed tab lists & counts, and re-open tabs into the current window
pref("browser.sessionstore.closedTabsFromAllWindows", true);
pref("browser.sessionstore.closedTabsFromClosedWindows", true);

// Minimal interval between two save operations in milliseconds (while the user is idle).
pref("browser.sessionstore.interval.idle", 3600000); // 1h

// Time (seconds) before we assume that the user is idle and that we don't need to
// collect/save the session quite as often.
pref("browser.sessionstore.idleDelay", 180); // 3 minutes

// Fine-grained default logging levels for each log appender
pref("browser.sessionstore.log.appender.console", "Fatal");
pref("browser.sessionstore.log.appender.dump", "Error");
pref("browser.sessionstore.log.appender.file.level", "Trace");
pref("browser.sessionstore.log.appender.file.logOnError", true);

// The default log level for all Session restore logs.
pref("browser.sessionstore.loglevel", "Warn");

#ifdef EARLY_BETA_OR_EARLIER
  pref("browser.sessionstore.loglevel", "Debug");
  pref("browser.sessionstore.log.appender.file.logOnSuccess", true);
#else
  pref("browser.sessionstore.log.appender.file.logOnSuccess", false);
#endif
// How old can a log file be before it gets deleted?
pref("browser.sessionstore.log.appender.file.maxErrorAge", 864000); // 10 days

// on which sites to save text data, POSTDATA and cookies
// 0 = everywhere, 1 = unencrypted sites, 2 = nowhere
pref("browser.sessionstore.privacy_level", 0);
// how many tabs can be reopened (per window)
pref("browser.sessionstore.max_tabs_undo", 25);
// how many windows will be saved and can be reopened per session - on non-macOS platforms this
// pref may be ignored when dealing with pop-up windows to ensure the user actually gets
// at least one window with a menu bar.
pref("browser.sessionstore.max_windows_undo", 5);
// number of crashes that can occur before the about:sessionrestore page is displayed
// (this pref has no effect if more than 6 hours have passed since the last crash)
pref("browser.sessionstore.max_resumed_crashes", 1);
// number of back button session history entries to restore (-1 = all of them)
pref("browser.sessionstore.max_serialize_back", 10);
// number of forward button session history entries to restore (-1 = all of them)
pref("browser.sessionstore.max_serialize_forward", -1);
// restore_on_demand overrides MAX_CONCURRENT_TAB_RESTORES (sessionstore constant)
// and restore_hidden_tabs. When true, tabs will not be restored until they are
// focused (also applies to tabs that aren't visible). When false, the values
// for MAX_CONCURRENT_TAB_RESTORES and restore_hidden_tabs are respected.
// Selected tabs are always restored regardless of this pref.
pref("browser.sessionstore.restore_on_demand", true);
// Whether to automatically restore hidden tabs (i.e., tabs in other tab groups) or not
pref("browser.sessionstore.restore_hidden_tabs", false);
// If restore_on_demand is set, pinned tabs are restored on startup by default.
// When set to true, this pref overrides that behavior, and pinned tabs will only
// be restored when they are focused.
pref("browser.sessionstore.restore_pinned_tabs_on_demand", false);
// The version at which we performed the latest upgrade backup
pref("browser.sessionstore.upgradeBackup.latestBuildID", "");
// How many upgrade backups should be kept
pref("browser.sessionstore.upgradeBackup.maxUpgradeBackups", 3);
// Toggle some debug behavior; end-users should not run sessionstore in debug mode
pref("browser.sessionstore.debug", false);
// Forget closed windows/tabs after two weeks
pref("browser.sessionstore.cleanup.forget_closed_after", 1209600000);

// temporary pref that will be removed in a future release, see bug 1836952
pref("browser.sessionstore.persist_closed_tabs_between_sessions", true);

// Don't quit the browser when Ctrl + Q is pressed.
pref("browser.quitShortcut.disabled", false);

// allow META refresh by default
pref("accessibility.blockautorefresh", false);

// Whether history is enabled or not.
pref("places.history.enabled", true);

// The default Places log level.
pref("places.loglevel", "Error");

// Whether or not diacritics must match in history text searches.
pref("places.search.matchDiacritics", false);

// the (maximum) number of the recent visits to sample
// when calculating frecency
pref("places.frecency.numVisits", 10);

// buckets (in days) for frecency calculation
pref("places.frecency.firstBucketCutoff", 4);
pref("places.frecency.secondBucketCutoff", 14);
pref("places.frecency.thirdBucketCutoff", 31);
pref("places.frecency.fourthBucketCutoff", 90);

// weights for buckets for frecency calculations
pref("places.frecency.firstBucketWeight", 100);
pref("places.frecency.secondBucketWeight", 70);
pref("places.frecency.thirdBucketWeight", 50);
pref("places.frecency.fourthBucketWeight", 30);
pref("places.frecency.defaultBucketWeight", 10);

// bonus (in percent) for visit transition types for frecency calculations
pref("places.frecency.embedVisitBonus", 0);
pref("places.frecency.framedLinkVisitBonus", 0);
pref("places.frecency.linkVisitBonus", 100);
pref("places.frecency.typedVisitBonus", 2000);
// The bookmarks bonus is always added on top of any other bonus, including
// the redirect source and the typed ones.
pref("places.frecency.bookmarkVisitBonus", 75);
// The redirect source bonus overwrites any transition bonus.
// 0 would hide these pages, instead we want them low ranked.  Thus we use
// linkVisitBonus - bookmarkVisitBonus, so that a bookmarked source is in par
// with a common link.
pref("places.frecency.redirectSourceVisitBonus", 25);
pref("places.frecency.downloadVisitBonus", 0);
// The perm/temp redirects here relate to redirect targets, not sources.
pref("places.frecency.permRedirectVisitBonus", 50);
pref("places.frecency.tempRedirectVisitBonus", 40);
pref("places.frecency.reloadVisitBonus", 0);
pref("places.frecency.defaultVisitBonus", 0);

// bonus (in percent) for place types for frecency calculations
pref("places.frecency.unvisitedBookmarkBonus", 140);
pref("places.frecency.unvisitedTypedBonus", 200);

// Enables alternative frecency calculation for origins.
pref("places.frecency.origins.alternative.featureGate", false);

// Whether to warm up network connections for places: menus and places: toolbar.
pref("browser.places.speculativeConnect.enabled", true);

// if true, use full page zoom instead of text zoom
pref("browser.zoom.full", true);

// Whether or not to update background tabs to the current zoom level.
pref("browser.zoom.updateBackgroundTabs", true);

// The breakpad report server to link to in about:crashes
pref("breakpad.reportURL", "https://crash-stats.mozilla.org/report/index/");

// URL for "Learn More" for DataCollection
pref("toolkit.datacollection.infoURL",
     "https://www.mozilla.org/legal/privacy/firefox.html");

// base URL for web-based support pages
pref("app.support.baseURL", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/");

// base url for web-based feedback pages
pref("app.feedback.baseURL", "https://ideas.mozilla.org/");

pref("security.certerrors.permanentOverride", true);
pref("security.certerrors.mitm.priming.enabled", true);
pref("security.certerrors.mitm.priming.endpoint", "https://mitmdetection.services.mozilla.com/");
pref("security.certerrors.mitm.auto_enable_enterprise_roots", true);

// Whether the bookmark panel should be shown when bookmarking a page.
pref("browser.bookmarks.editDialog.showForNewBookmarks", true);

// Don't try to alter this pref, it'll be reset the next time you use the
// bookmarking dialog
pref("browser.bookmarks.editDialog.firstEditField", "namePicker");

// The number of recently selected folders in the edit bookmarks dialog.
pref("browser.bookmarks.editDialog.maxRecentFolders", 7);

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  // This controls the strength of the Windows content process sandbox for
  // testing purposes. This will require a restart.
  // On windows these levels are:
  // See - security/sandbox/win/src/sandboxbroker/sandboxBroker.cpp
  // SetSecurityLevelForContentProcess() for what the different settings mean.
  pref("security.sandbox.content.level", 8);

  // Pref controlling if messages relevant to sandbox violations are logged.
  pref("security.sandbox.logging.enabled", false);
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // This pref is discussed in bug 1083344, the naming is inspired from its
  // Windows counterpart, but on Mac it's an integer which means:
  // 0 -> "no sandbox" (nightly only)
  // 1 -> "preliminary content sandboxing enabled: write access to
  //       home directory is prevented"
  // 2 -> "preliminary content sandboxing enabled with profile protection:
  //       write access to home directory is prevented, read and write access
  //       to ~/Library and profile directories are prevented (excluding
  //       $PROFILE/{extensions,chrome})"
  // 3 -> "no global read/write access, read access permitted to
  //       $PROFILE/{extensions,chrome}"
  // This setting is read when the content process is started. On Mac the
  // content process is killed when all windows are closed, so a change will
  // take effect when the 1st window is opened.
  pref("security.sandbox.content.level", 3);

  // Disconnect content processes from the window server. Depends on
  // out-of-process WebGL and non-native theming. i.e., both in-process WebGL
  // and native theming depend on content processes having a connection to the
  // window server. Window server disconnection is automatically disabled (and
  // this pref overridden) if OOP WebGL is disabled. OOP WebGL is disabled
  // for some tests.
  pref("security.sandbox.content.mac.disconnect-windowserver", true);

  // Pref controlling if messages relevant to sandbox violations are logged.
  pref("security.sandbox.logging.enabled", false);
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  // This pref is introduced as part of bug 742434, the naming is inspired from
  // its Windows/Mac counterpart, but on Linux it's an integer which means:
  // 0 -> "no sandbox"
  // 1 -> no longer used; level will be clamped to 2
  // 2 -> "seccomp-bpf + write file broker"
  // 3 -> "seccomp-bpf + read/write file brokering"
  // 4 -> all of the above + network/socket restrictions + chroot
  //
  // The purpose of this setting is to allow Linux users or distros to disable
  // the sandbox while we fix their problems, or to allow running Firefox with
  // exotic configurations we can't reasonably support out of the box.
  //
  pref("security.sandbox.content.level", 4);
  // Introduced as part of bug 1608558.  Linux is currently the only platform
  // that uses a sandbox level for the socket process.  There are currently
  // only 2 levels:
  // 0 -> "no sandbox"
  // 1 -> "sandboxed, allows socket operations and reading necessary certs"
  pref("security.sandbox.socket.process.level", 1);
  pref("security.sandbox.content.write_path_whitelist", "");
  pref("security.sandbox.content.read_path_whitelist", "");
  pref("security.sandbox.content.syscall_whitelist", "");
#endif

#if defined(XP_OPENBSD) && defined(MOZ_SANDBOX)
  pref("security.sandbox.content.level", 1);
#endif

#ifdef XP_WIN
  pref("browser.taskbar.previews.enable", false);
  pref("browser.taskbar.previews.max", 20);
  pref("browser.taskbar.previews.cachetime", 5);

  pref("browser.taskbar.lists.enabled", true);
  pref("browser.taskbar.lists.frequent.enabled", true);
  pref("browser.taskbar.lists.recent.enabled", false);
  pref("browser.taskbar.lists.maxListItemCount", 7);
  pref("browser.taskbar.lists.tasks.enabled", true);
  pref("browser.taskbar.lists.refreshInSeconds", 120);
  pref("browser.startMenu.msixPinnedWhenLastChecked", false);
#endif

// Preferences to be synced by default.
// Preferences with the prefix `services.sync.prefs.sync-seen.` should have
// a value of `false`, and means the value of the pref will be synced as soon
// as a value for the pref is "seen", even if it is the default, and should be
// used for prefs we sync but which have different values on different channels,
// platforms or distributions.
pref("services.sync.prefs.sync.accessibility.blockautorefresh", true);
pref("services.sync.prefs.sync.accessibility.browsewithcaret", true);
pref("services.sync.prefs.sync.accessibility.typeaheadfind", true);
pref("services.sync.prefs.sync.accessibility.typeaheadfind.linksonly", true);
pref("services.sync.prefs.sync.addons.ignoreUserEnabledChanges", true);
pref("services.sync.prefs.sync.app.shield.optoutstudies.enabled", true);
// The addons prefs related to repository verification are intentionally
// not synced for security reasons. If a system is compromised, a user
// could weaken the pref locally, install an add-on from an untrusted
// source, and this would propagate automatically to other,
// uncompromised Sync-connected devices.
pref("services.sync.prefs.sync.browser.contentblocking.category", true);
pref("services.sync.prefs.sync.browser.contentblocking.features.strict", true);
pref("services.sync.prefs.sync.browser.crashReports.unsubmittedCheck.autoSubmit2", true);
pref("services.sync.prefs.sync.browser.ctrlTab.sortByRecentlyUsed", true);
pref("services.sync.prefs.sync.browser.discovery.enabled", true);
pref("services.sync.prefs.sync.browser.download.useDownloadDir", true);
pref("services.sync.prefs.sync.browser.firefox-view.feature-tour", true);
pref("services.sync.prefs.sync.browser.formfill.enable", true);
pref("services.sync.prefs.sync.browser.link.open_newwindow", true);
pref("services.sync.prefs.sync.browser.menu.showViewImageInfo", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.showSearch", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.showSponsored", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.showSponsoredTopSites", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.feeds.topsites", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.topSitesRows", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.feeds.section.topstories", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.topstories.rows", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.feeds.section.highlights", true);
// Some linux distributions disable all highlights by default.
pref("services.sync.prefs.sync-seen.browser.newtabpage.activity-stream.section.highlights", false);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.highlights.includeVisited", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.highlights.includeBookmarks", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.highlights.includeDownloads", true);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.highlights.includePocket", true);
// Some linux distributions disable just pocket by default.
pref("services.sync.prefs.sync-seen.browser.newtabpage.activity-stream.section.highlights.includePocket", false);
pref("services.sync.prefs.sync.browser.newtabpage.activity-stream.section.highlights.rows", true);
pref("services.sync.prefs.sync.browser.newtabpage.enabled", true);
pref("services.sync.prefs.sync.browser.newtabpage.pinned", true);
pref("services.sync.prefs.sync.browser.pdfjs.feature-tour", true);
pref("services.sync.prefs.sync.browser.safebrowsing.downloads.enabled", true);
pref("services.sync.prefs.sync.browser.safebrowsing.downloads.remote.block_potentially_unwanted", true);
pref("services.sync.prefs.sync.browser.safebrowsing.malware.enabled", true);
pref("services.sync.prefs.sync.browser.safebrowsing.phishing.enabled", true);
pref("services.sync.prefs.sync.browser.search.update", true);
pref("services.sync.prefs.sync.browser.startup.homepage", true);
pref("services.sync.prefs.sync.browser.startup.page", true);
pref("services.sync.prefs.sync.browser.startup.upgradeDialog.enabled", true);
pref("services.sync.prefs.sync.browser.tabs.loadInBackground", true);
pref("services.sync.prefs.sync.browser.tabs.warnOnClose", true);
pref("services.sync.prefs.sync.browser.tabs.warnOnOpen", true);
pref("services.sync.prefs.sync.browser.taskbar.previews.enable", true);
pref("services.sync.prefs.sync.browser.urlbar.maxRichResults", true);
pref("services.sync.prefs.sync.browser.urlbar.showSearchSuggestionsFirst", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.bookmark", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.history", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.openpage", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.searches", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.topsites", true);
pref("services.sync.prefs.sync.browser.urlbar.suggest.engines", true);
pref("services.sync.prefs.sync.dom.disable_open_during_load", true);
pref("services.sync.prefs.sync.dom.disable_window_flip", true);
pref("services.sync.prefs.sync.dom.disable_window_move_resize", true);
pref("services.sync.prefs.sync.dom.event.contextmenu.enabled", true);
pref("services.sync.prefs.sync.dom.security.https_only_mode", true);
pref("services.sync.prefs.sync.dom.security.https_only_mode_ever_enabled", true);
pref("services.sync.prefs.sync.dom.security.https_only_mode_ever_enabled_pbm", true);
pref("services.sync.prefs.sync.dom.security.https_only_mode_pbm", true);
pref("services.sync.prefs.sync.extensions.update.enabled", true);
pref("services.sync.prefs.sync.extensions.activeThemeID", true);
pref("services.sync.prefs.sync.general.autoScroll", true);
// general.autoScroll has a different default on Linux vs elsewhere.
pref("services.sync.prefs.sync-seen.general.autoScroll", false);
// general.smoothScroll is not synced, bug 1851024
pref("services.sync.prefs.sync.intl.accept_languages", true);
pref("services.sync.prefs.sync.intl.regional_prefs.use_os_locales", true);
pref("services.sync.prefs.sync.layout.spellcheckDefault", true);
pref("services.sync.prefs.sync.media.autoplay.default", true);
pref("services.sync.prefs.sync.media.eme.enabled", true);
// Some linux distributions disable eme by default.
pref("services.sync.prefs.sync-seen.media.eme.enabled", false);
pref("services.sync.prefs.sync.media.videocontrols.picture-in-picture.video-toggle.enabled", true);
pref("services.sync.prefs.sync.network.cookie.cookieBehavior", true);
pref("services.sync.prefs.sync.permissions.default.image", true);
pref("services.sync.prefs.sync.pref.downloads.disable_button.edit_actions", true);
pref("services.sync.prefs.sync.pref.privacy.disable_button.cookie_exceptions", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.cache", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.cache", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.cookies", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.cookiesAndStorage", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.downloads", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.downloads", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.formdata", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.formdata", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.history", true);
// We can't clear the old history pref until we're sure all clients have
// migrated to the new pref.
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.historyFormDataAndDownloads", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.browsingHistoryAndDownloads", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.offlineApps", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.sessions", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown.siteSettings", true);
pref("services.sync.prefs.sync.privacy.clearOnShutdown_v2.siteSettings", true);
pref("services.sync.prefs.sync.privacy.donottrackheader.enabled", true);
pref("services.sync.prefs.sync.privacy.globalprivacycontrol.enabled", true);
pref("services.sync.prefs.sync.privacy.sanitize.sanitizeOnShutdown", true);
pref("services.sync.prefs.sync.privacy.trackingprotection.enabled", true);
pref("services.sync.prefs.sync.privacy.trackingprotection.cryptomining.enabled", true);
pref("services.sync.prefs.sync.privacy.trackingprotection.fingerprinting.enabled", true);
pref("services.sync.prefs.sync.privacy.trackingprotection.pbmode.enabled", true);
// We do not sync `privacy.resistFingerprinting` by default as it's an undocumented,
// not-recommended footgun - see bug 1763278 for more.
pref("services.sync.prefs.sync.privacy.reduceTimerPrecision", true);
pref("services.sync.prefs.sync.privacy.resistFingerprinting.reduceTimerPrecision.microseconds", true);
pref("services.sync.prefs.sync.privacy.resistFingerprinting.reduceTimerPrecision.jitter", true);
pref("services.sync.prefs.sync.privacy.userContext.enabled", true);
pref("services.sync.prefs.sync.privacy.userContext.newTabContainerOnLeftClick.enabled", true);
pref("services.sync.prefs.sync.security.default_personal_cert", true);
pref("services.sync.prefs.sync.services.sync.syncedTabs.showRemoteIcons", true);
pref("services.sync.prefs.sync.signon.autofillForms", true);
pref("services.sync.prefs.sync.signon.generation.enabled", true);
pref("services.sync.prefs.sync.signon.management.page.breach-alerts.enabled", true);
pref("services.sync.prefs.sync.signon.rememberSignons", true);
pref("services.sync.prefs.sync.spellchecker.dictionary", true);
pref("services.sync.prefs.sync.ui.osk.enabled", true);

// A preference which, if false, means sync will only apply incoming preference
// changes if there's already a local services.sync.prefs.sync.* control pref.
// If true, all incoming preferences will be applied and the local "control
// pref" updated accordingly.
pref("services.sync.prefs.dangerously_allow_arbitrary", false);

// A preference that controls whether we should show the icon for a remote tab.
// This pref has no UI but exists because some people may be concerned that
// fetching these icons to show remote tabs may leak information about that
// user's tabs and bookmarks. Note this pref is also synced.
pref("services.sync.syncedTabs.showRemoteIcons", true);

// A preference (in milliseconds) controlling if we sync after a tab change and
// how long to delay before we schedule the sync
// Anything <= 0 means disabled
pref("services.sync.syncedTabs.syncDelayAfterTabChange", 5000);

// Whether the character encoding menu is under the main Firefox button. This
// preference is a string so that localizers can alter it.
pref("browser.menu.showCharacterEncoding", "chrome://browser/locale/browser.properties");

// Whether prompts should be content modal (1) tab modal (2) or window modal(3) by default
// This is a fallback value for when prompt callers do not specify a modalType.
pref("prompts.defaultModalType", 3);

// Whether to use the discrete Top Sites component.
pref("browser.topsites.component.enabled", false);

pref("browser.topsites.useRemoteSetting", true);
// Fetch sponsored Top Sites from Mozilla Tiles Service (Contile)
pref("browser.topsites.contile.enabled", true);
pref("browser.topsites.contile.endpoint", "https://contile.services.mozilla.com/v1/tiles");

// Whether to enable the Share-of-Voice feature for Sponsored Topsites via Contile.
pref("browser.topsites.contile.sov.enabled", true);

// The base URL for the Quick Suggest anonymizing proxy. To make a request to
// the proxy, include a campaign ID in the path.
pref("browser.partnerlink.attributionURL", "https://topsites.services.mozilla.com/cid/");
pref("browser.partnerlink.campaign.topsites", "amzn_2020_a1");

// Activates preloading of the new tab url.
pref("browser.newtab.preload", true);

// If an on-train limited rollout of the preonboarding modal is enabled, the
// percentage of the Mac, Linux, and MSIX population to enroll. Default to 25% of
// population (2500 / 10000).
pref("browser.preonboarding.onTrainRolloutPopulation",  2500);

// Show "Download Firefox for mobile" QR code modal on newtab
pref("browser.newtabpage.activity-stream.mobileDownloadModal.enabled", false);
pref("browser.newtabpage.activity-stream.mobileDownloadModal.variant-a", false);
pref("browser.newtabpage.activity-stream.mobileDownloadModal.variant-b", false);
pref("browser.newtabpage.activity-stream.mobileDownloadModal.variant-c", false);

// Mozilla Ad Routing Service (MARS) unified ads service
pref("browser.newtabpage.activity-stream.unifiedAds.tiles.enabled", true);
pref("browser.newtabpage.activity-stream.unifiedAds.spocs.enabled", true);
pref("browser.newtabpage.activity-stream.unifiedAds.endpoint", "https://ads.mozilla.org/");
pref("browser.newtabpage.activity-stream.unifiedAds.adsFeed.enabled", false);
pref("browser.newtabpage.activity-stream.unifiedAds.adsFeed.tiles.enabled", false);

// Weather widget for newtab
pref("browser.newtabpage.activity-stream.showWeather", true);
pref("browser.newtabpage.activity-stream.weather.query", "");
pref("browser.newtabpage.activity-stream.weather.display", "simple");

pref("browser.newtabpage.activity-stream.images.smart", true);

// enable location search for newtab weather widget
pref("browser.newtabpage.activity-stream.weather.locationSearchEnabled", true);

// List of regions that get weather by default.
pref("browser.newtabpage.activity-stream.discoverystream.region-weather-config", "US,CA");

// List of locales that weather widget supports.
pref("browser.newtabpage.activity-stream.discoverystream.locale-weather-config", "en-US,en-GB,en-CA");

// Preference to enable wallpaper selection in the Customize Menu of new tab page
pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", true);
pref("browser.newtabpage.activity-stream.newtabWallpapers.v2.enabled", true);
pref("browser.newtabpage.activity-stream.newtabWallpapers.customColor.enabled", false);
pref("browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.enabled", false);

// Utility preferences for custom wallpaper upload
pref("browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid", "");
pref("browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.fileSize", 0);
pref("browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.fileSize.enabled", false);

// Current new tab page background images.
pref("browser.newtabpage.activity-stream.newtabWallpapers.wallpaper", "");

// Preference to show feature highlight about wallpaper on new tab page
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightEnabled", false);
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightDismissed", false);
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightSeenCounter", 0);
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightHeaderText", "");
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightContentText", "");
pref("browser.newtabpage.activity-stream.newtabWallpapers.highlightCtaText", "");

pref("browser.newtabpage.activity-stream.newNewtabExperience.colors", "#004CA4,#009E97,#7550C2,#B63B39,#C96A00,#CA9600,#CC527F");

// Default layout experimentation
pref("browser.newtabpage.activity-stream.newtabLayouts.variant-a", false);
pref("browser.newtabpage.activity-stream.newtabLayouts.variant-b", true);

pref("browser.newtabpage.activity-stream.newtabShortcuts.refresh", true);

// Discovery stream ad size experiment
pref("browser.newtabpage.activity-stream.newtabAdSize.variant-a", false);
pref("browser.newtabpage.activity-stream.newtabAdSize.variant-b", false);

// Activity Stream prefs that control to which page to redirect
#ifndef RELEASE_OR_BETA
  pref("browser.newtabpage.activity-stream.debug", false);
#endif

// The remote FxA root content URL for the Activity Stream firstrun page.
pref("browser.newtabpage.activity-stream.fxaccounts.endpoint", "https://accounts.firefox.com/");

// The pref that controls if the search shortcuts experiment is on
pref("browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts", true);

// ASRouter provider configuration
pref("browser.newtabpage.activity-stream.asrouter.providers.message-groups", "{\"id\":\"message-groups\",\"enabled\":true,\"type\":\"remote-settings\",\"collection\":\"message-groups\",\"updateCycleInMs\":3600000}");
pref("browser.newtabpage.activity-stream.asrouter.providers.onboarding", "{\"id\":\"onboarding\",\"type\":\"local\",\"localProvider\":\"OnboardingMessageProvider\",\"enabled\":true,\"exclude\":[]}");
pref("browser.newtabpage.activity-stream.asrouter.providers.cfr", "{\"id\":\"cfr\",\"enabled\":true,\"type\":\"remote-settings\",\"collection\":\"cfr\",\"updateCycleInMs\":3600000}");
pref("browser.newtabpage.activity-stream.asrouter.providers.messaging-experiments", "{\"id\":\"messaging-experiments\",\"enabled\":true,\"type\":\"remote-experiments\",\"updateCycleInMs\":3600000}");

// ASRouter user prefs
pref("browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons", true);
pref("browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features", true);
pref("messaging-system.askForFeedback", true);

// The pref that controls if ASRouter uses the remote fluent files.
// It's enabled by default, but could be disabled to force ASRouter to use the local files.
pref("browser.newtabpage.activity-stream.asrouter.useRemoteL10n", true);

// These prefs control if Discovery Stream is enabled.
pref("browser.newtabpage.activity-stream.discoverystream.enabled", true);
pref("browser.newtabpage.activity-stream.discoverystream.hardcoded-basic-layout", false);
pref("browser.newtabpage.activity-stream.discoverystream.hybridLayout.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.hideCardBackground.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.fourCardLayout.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.newFooterSection.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.saveToPocketCard.enabled", true);
pref("browser.newtabpage.activity-stream.discoverystream.saveToPocketCardRegions", "");
pref("browser.newtabpage.activity-stream.discoverystream.hideDescriptions.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.hideDescriptionsRegions", "");
pref("browser.newtabpage.activity-stream.discoverystream.compactGrid.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.compactImages.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.imageGradient.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.titleLines", 3);
pref("browser.newtabpage.activity-stream.discoverystream.descLines", 3);
pref("browser.newtabpage.activity-stream.discoverystream.readTime.enabled", true);
pref("browser.newtabpage.activity-stream.discoverystream.newSponsoredLabel.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.essentialReadsHeader.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.recentSaves.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.editorsPicksHeader.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.spoc-positions", "1,5,7,11,18,20");

// For both spoc and tiles, count corresponds to the matching placement. So the first placement in an array corresponds to the first count.
pref("browser.newtabpage.activity-stream.discoverystream.placements.spocs", "newtab_spocs");
pref("browser.newtabpage.activity-stream.discoverystream.placements.spocs.counts", "6");
pref("browser.newtabpage.activity-stream.discoverystream.placements.tiles", "newtab_tile_1, newtab_tile_2, newtab_tile_3");
pref("browser.newtabpage.activity-stream.discoverystream.placements.tiles.counts", "1, 1, 1");

pref("browser.newtabpage.activity-stream.discoverystream.spoc-topsites-positions", "2");
// This is a 0-based index, for consistency with the other position CSVs,
// but Contile positions are a 1-based index, so we end up adding 1 to these before using them.
pref("browser.newtabpage.activity-stream.discoverystream.contile-topsites-positions", "0,1,2");
pref("browser.newtabpage.activity-stream.discoverystream.widget-positions", "");

pref("browser.newtabpage.activity-stream.discoverystream.spocs-endpoint", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocs-endpoint-query", "");
pref("browser.newtabpage.activity-stream.discoverystream.sponsored-collections.enabled", false);

// Changes the spoc content.
pref("browser.newtabpage.activity-stream.discoverystream.spocAdTypes", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocZoneIds", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocTopsitesAdTypes", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocTopsitesZoneIds", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocTopsitesPlacement.enabled", true);
pref("browser.newtabpage.activity-stream.discoverystream.spocSiteId", "");
pref("browser.newtabpage.activity-stream.discoverystream.ctaButtonSponsors", "");
pref("browser.newtabpage.activity-stream.discoverystream.ctaButtonVariant", "");
pref("browser.newtabpage.activity-stream.discoverystream.spocMessageVariant", "");

pref("browser.newtabpage.activity-stream.discoverystream.sendToPocket.enabled", true);

// Pref enabling content reporting
pref("browser.newtabpage.activity-stream.discoverystream.reportAds.enabled", false);

// List of regions that do not get stories, regardless of locale-list-config.
pref("browser.newtabpage.activity-stream.discoverystream.region-stories-block", "");
// List of locales that get stories, regardless of region-stories-config.
#ifdef NIGHTLY_BUILD
  pref("browser.newtabpage.activity-stream.discoverystream.locale-list-config", "en-US,en-CA,en-GB");
#else
  pref("browser.newtabpage.activity-stream.discoverystream.locale-list-config", "");
#endif
// List of regions that get stories by default.
pref("browser.newtabpage.activity-stream.discoverystream.region-stories-config", "US,DE,CA,GB,IE,CH,AT,BE,IN,FR,IT,ES");
// List of regions that support the new recommendations BFF, also requires region-stories-config
pref("browser.newtabpage.activity-stream.discoverystream.region-bff-config", "US,DE,CA,GB,IE,CH,AT,BE,IN,FR,IT,ES");
pref("browser.newtabpage.activity-stream.discoverystream.merino-provider.enabled", true);

// List of regions that get topics selection by default.
pref("browser.newtabpage.activity-stream.discoverystream.topicSelection.region-topics-config", "");
pref("browser.newtabpage.activity-stream.discoverystream.topicSelection.onboarding.enabled", false);

// List of locales that get topics selection by default.
pref("browser.newtabpage.activity-stream.discoverystream.topicLabels.region-topic-label-config", "US, CA");
pref("browser.newtabpage.activity-stream.discoverystream.topicSelection.locale-topics-config", "en-US, en-GB, en-CA");
pref("browser.newtabpage.activity-stream.discoverystream.topicLabels.locale-topic-label-config", "en-US, en-GB, en-CA");

// List of locales that get contextual content by default
pref("browser.newtabpage.activity-stream.discoverystream.contextualContent.locale-content-config", "en-US,en-GB,en-CA,de");
// List of regions that get contextual content by default- TODO: update once development is closer to being finished
pref("browser.newtabpage.activity-stream.discoverystream.contextualContent.region-content-config", "");

// List of locales that get section layout by default
pref("browser.newtabpage.activity-stream.discoverystream.sections.locale-content-config", "en-US,en-CA");
// List of regions that get section layout by default
pref("browser.newtabpage.activity-stream.discoverystream.sections.region-content-config", "");

pref("browser.newtabpage.activity-stream.discoverystream.sections.cards.enabled", true);

// List of regions that use inferred personalization.
pref("browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.region-config", "");
// List of locales that use inferred personalization.
pref("browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.locale-config", "en-US,en-GB,en-CA");

pref("browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.user.enabled", true);
pref("browser.newtabpage.activity-stream.discoverystream.sections.personalization.inferred.blocked", false);

pref("browser.newtabpage.activity-stream.discoverystream.sections.interestPicker.enabled", false);
pref("browser.newtabpage.activity-stream.discoverystream.sections.interestPicker.visibleSections", "");



pref("browser.newtabpage.activity-stream.discoverystream.merino-provider.endpoint", "merino.services.mozilla.com");
// List of regions that get spocs by default.
pref("browser.newtabpage.activity-stream.discoverystream.region-spocs-config", "US,CA,DE,GB,FR,IT,ES");
// List of regions that don't get the 7 row layout.
pref("browser.newtabpage.activity-stream.discoverystream.region-basic-config", "");

// Add parameters to Pocket feed URL.
pref("browser.newtabpage.activity-stream.discoverystream.pocket-feed-parameters", "");
pref("browser.newtabpage.activity-stream.discoverystream.merino-feed-experiment", false);

// Allows Pocket story collections to be dismissed.
pref("browser.newtabpage.activity-stream.discoverystream.isCollectionDismissible", true);
pref("browser.newtabpage.activity-stream.discoverystream.personalization.enabled", true);
// Configurable keys used by personalization.
pref("browser.newtabpage.activity-stream.discoverystream.personalization.modelKeys", "nb_model_arts_and_entertainment, nb_model_autos_and_vehicles, nb_model_beauty_and_fitness, nb_model_blogging_resources_and_services, nb_model_books_and_literature, nb_model_business_and_industrial, nb_model_computers_and_electronics, nb_model_finance, nb_model_food_and_drink, nb_model_games, nb_model_health, nb_model_hobbies_and_leisure, nb_model_home_and_garden, nb_model_internet_and_telecom, nb_model_jobs_and_education, nb_model_law_and_government, nb_model_online_communities, nb_model_people_and_society, nb_model_pets_and_animals, nb_model_real_estate, nb_model_reference, nb_model_science, nb_model_shopping, nb_model_sports, nb_model_travel");
// System pref to allow Pocket stories personalization to be turned on/off.
pref("browser.newtabpage.activity-stream.discoverystream.recs.personalized", false);
// System pref to allow Pocket sponsored content personalization to be turned on/off.
pref("browser.newtabpage.activity-stream.discoverystream.spocs.personalized", true);

// Flip this once the user has dismissed the Pocket onboarding experience,
pref("browser.newtabpage.activity-stream.discoverystream.onboardingExperience.dismissed", false);
pref("browser.newtabpage.activity-stream.discoverystream.onboardingExperience.enabled", false);

// List of locales that get thumbs up/down on recommended stories by default.
pref("browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.locale-thumbs-config", "en-US, en-GB, en-CA");

pref("browser.newtabpage.activity-stream.telemetry.privatePing.enabled", false);

// surface ID sent from merino to the client from the curated-recommendations request
pref("browser.newtabpage.activity-stream.telemetry.surfaceId", "");

// List of regions that get thumbs up/down on recommended stories by default.
#ifdef EARLY_BETA_OR_EARLIER
  pref("browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.region-thumbs-config", "US, CA");
#else
  pref("browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.region-thumbs-config", "");
#endif

// Shows users compact layout of Home New Tab page. Also requires region-thumbs-config.
pref("browser.newtabpage.activity-stream.discoverystream.thumbsUpDown.searchTopsitesCompact", true);

// Displays publisher favicons on recommended stories of New Tab page
pref("browser.newtabpage.activity-stream.discoverystream.publisherFavicon.enabled", false);

// User pref to show stories on newtab (feeds.system.topstories has to be set to true as well)
pref("browser.newtabpage.activity-stream.feeds.section.topstories", true);

// The pref controls if search hand-off is enabled for Activity Stream.
pref("browser.newtabpage.activity-stream.improvesearch.handoffToAwesomebar", true);

pref("browser.newtabpage.activity-stream.logowordmark.alwaysVisible", true);

// URLs from the user's history that contain this search param will be hidden
// from the top sites. The value is a string with one of the following forms:
// - "" (empty) - Disable this feature
// - "key" - Search param named "key" with any or no value
// - "key=" - Search param named "key" with no value
// - "key=value" - Search param named "key" with value "value"
pref("browser.newtabpage.activity-stream.hideTopSitesWithSearchParam", "mfadid=adm");

// Separate about welcome
pref("browser.aboutwelcome.enabled", true);
// Used to set multistage welcome UX
pref("browser.aboutwelcome.screens", "");

// Experiment Manager
// See Console.sys.mjs LOG_LEVELS for all possible values
pref("messaging-system.log", "warn");
pref("messaging-system.rsexperimentloader.collection_id", "nimbus-desktop-experiments");
pref("nimbus.debug", false);
pref("nimbus.validation.enabled", true);

// Enable the targeting context telemetry by default, but allow it to be
// disabled, e.g., for artifact builds.
// See-also: https://bugzilla.mozilla.org/show_bug.cgi?id=1936317
// See-also: https://bugzilla.mozilla.org/show_bug.cgi?id=1936319
#if defined(MOZ_ARTIFACT_BUILDS)
  pref("nimbus.telemetry.targetingContextEnabled", false);
#else
  pref("nimbus.telemetry.targetingContextEnabled", true);
#endif

// Nimbus QA prefs. Used to monitor pref-setting test experiments.
pref("nimbus.qa.pref-1", "default");
pref("nimbus.qa.pref-2", "default");

// Startup Crash Tracking
// number of startup crashes that can occur before starting into safe mode automatically
// (this pref has no effect if more than 6 hours have passed since the last crash)
pref("toolkit.startup.max_resumed_crashes", 3);

// Whether to use RegisterApplicationRestart to restart the browser and resume
// the session on next Windows startup
#if defined(XP_WIN)
  pref("toolkit.winRegisterApplicationRestart", true);
#endif

// The values of preferredAction and alwaysAskBeforeHandling before pdf.js
// became the default.
pref("pdfjs.previousHandler.preferredAction", 0);
pref("pdfjs.previousHandler.alwaysAskBeforeHandling", false);

// Try to convert PDFs sent as octet-stream
pref("pdfjs.handleOctetStream", true);

// Is the sidebar positioned ahead of the content browser
pref("sidebar.position_start", true);
pref("sidebar.revamp", false);
// Should the sidebar launcher default to visible or not with horizontal tabs
pref("sidebar.revamp.defaultLauncherVisible", true);
// This is nightly only for now, as we need to address bug 1933527 and bug 1934039.
#ifdef NIGHTLY_BUILD
pref("sidebar.revamp.round-content-area", true);
#else
pref("sidebar.revamp.round-content-area", false);
#endif
pref("sidebar.animation.enabled", true);
pref("sidebar.animation.duration-ms", 200);
pref("sidebar.animation.expand-on-hover.duration-ms", 400);
// This pref is used to store user customized tools in the sidebar launcher and shouldn't be changed.
// See https://firefox-source-docs.mozilla.org/browser/components/sidebar/docs/index.html for ways
// you can introduce a new tool to the sidebar launcher.
pref("sidebar.main.tools", "");
pref("sidebar.verticalTabs", false);
pref("sidebar.visibility", "always-show");
// Sidebar UI state is stored per-window via session restore. Use this pref
// as a backup to restore the sidebar UI state when a user has PPB mode on
// or has history cleared on browser close.
pref("sidebar.backupState", "{}");
pref("sidebar.expandOnHover", true);
pref("sidebar.old-sidebar.has-used", false);
pref("sidebar.new-sidebar.has-used", false);

pref("browser.ml.chat.enabled", true);
pref("browser.ml.chat.hideLocalhost", true);
pref("browser.ml.chat.prompt.prefix", '{"l10nId":"genai-prompt-prefix-selection"}');
pref("browser.ml.chat.prompts.0", '{"id":"summarize","l10nId":"genai-prompts-summarize"}');
pref("browser.ml.chat.prompts.1", '{"id":"explain","l10nId":"genai-prompts-explain"}');
pref("browser.ml.chat.prompts.2", '{"id":"simplify","l10nId":"genai-prompts-simplify","targeting":"channel==\'nightly\'"}');
pref("browser.ml.chat.prompts.3", '{"id":"quiz","l10nId":"genai-prompts-quiz","targeting":"!provider|regExpMatch(\'gemini\') || region == \'US\'"}');
pref("browser.ml.chat.prompts.4", '{"id":"proofread", "l10nId":"genai-prompts-proofread"}');
pref("browser.ml.chat.provider", "");
pref("browser.ml.chat.shortcuts", true);
pref("browser.ml.chat.shortcuts.custom", true);
pref("browser.ml.chat.shortcuts.longPress", 60000);
pref("browser.ml.chat.sidebar", true);

pref("browser.ml.linkPreview.allowedLanguages", "en");
pref("browser.ml.linkPreview.enabled", false);
pref("browser.ml.linkPreview.outputSentences", 3);
pref("browser.ml.linkPreview.blockListEnabled", true);
pref("browser.ml.linkPreview.noKeyPointsRegions", "AD,AT,BE,BG,CH,CY,CZ,DE,DK,EE,ES,FI,FR,GR,HR,HU,IE,IS,IT,LI,LT,LU,LV,MT,NL,NO,PL,PT,RO,SE,SI,SK");

// Block insecure active content on https pages
pref("security.mixed_content.block_active_content", true);

// Show "Not Secure" text for http pages.
pref("security.insecure_connection_text.enabled", true);
pref("security.insecure_connection_text.pbmode.enabled", true);

// If this turns true, Moz*Gesture events are not called stopPropagation()
// before content.
pref("dom.debug.propagate_gesture_events_through_content", false);

// CustomizableUI debug logging.
pref("browser.uiCustomization.debug", false);

// CustomizableUI state of the browser's user interface
pref("browser.uiCustomization.state", "");

// If set to false, FxAccounts and Sync will be unavailable.
// A restart is mandatory after flipping that preference.
pref("identity.fxaccounts.enabled", true);

// The remote FxA root content URL. Must use HTTPS.
pref("identity.fxaccounts.remote.root", "https://accounts.firefox.com/");

// The value of the context query parameter passed in fxa requests.
pref("identity.fxaccounts.contextParam", "oauth_webchannel_v1");

// Whether to use the oauth flow for desktop or not
pref("identity.fxaccounts.oauth.enabled", true);

// The remote URL of the FxA Profile Server
pref("identity.fxaccounts.remote.profile.uri", "https://profile.accounts.firefox.com/v1");

// The remote URL of the FxA OAuth Server
pref("identity.fxaccounts.remote.oauth.uri", "https://oauth.accounts.firefox.com/v1");

// Whether FxA pairing using QR codes is enabled.
pref("identity.fxaccounts.pairing.enabled", true);

// The remote URI of the FxA pairing server
pref("identity.fxaccounts.remote.pairing.uri", "wss://channelserver.services.mozilla.com");

// Token server used by the FxA Sync identity.
pref("identity.sync.tokenserver.uri", "https://token.services.mozilla.com/1.0/sync/1.5");

// Auto-config URL for FxA self-hosters, makes an HTTP request to
// [identity.fxaccounts.autoconfig.uri]/.well-known/fxa-client-configuration
// This is now the prefered way of pointing to a custom FxA server, instead
// of making changes to "identity.fxaccounts.*.uri".
pref("identity.fxaccounts.autoconfig.uri", "");

// URL for help link about Send Tab.
pref("identity.sendtabpromo.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/send-tab");

// URLs for promo links to mobile browsers. Note that consumers are expected to
// append a value for utm_campaign.
pref("identity.mobilepromo.android", "https://www.mozilla.org/firefox/android/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_campaign=");
pref("identity.mobilepromo.ios", "https://www.mozilla.org/firefox/ios/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_campaign=");

// Migrate any existing Firefox Account data from the default profile to the
// Developer Edition profile.
#ifdef MOZ_DEV_EDITION
  pref("identity.fxaccounts.migrateToDevEdition", true);
#else
  pref("identity.fxaccounts.migrateToDevEdition", false);
#endif

// How often should we try to fetch missed FxA commands on sync (in seconds).
// Default is 24 hours.
pref("identity.fxaccounts.commands.missed.fetch_interval", 86400);

// Controls whether this client can send and receive "close tab"
// commands from other FxA clients
pref("identity.fxaccounts.commands.remoteTabManagement.enabled", true);

// Controls whether or not the client association ping has values set on it
// when the sync-ui-state:update notification fires.
pref("identity.fxaccounts.telemetry.clientAssociationPing.enabled", true);

// Note: when media.gmp-*.visible is true, provided we're running on a
// supported platform/OS version, the corresponding CDM appears in the
// plugins list, Firefox will download the GMP/CDM if enabled, and our
// UI to re-enable EME prompts the user to re-enable EME if it's disabled
// and script requests EME. If *.visible is false, we won't show the UI
// to enable the CDM if its disabled; it's as if the keysystem is completely
// unsupported.

#ifdef MOZ_WIDEVINE_EME
  pref("media.gmp-manager.chromium-update-url", "https://update.googleapis.com/service/update2/crx?response=redirect&x=id%3D%GUID%%26uc&acceptformat=crx3&updaterversion=999");
  pref("media.gmp-widevinecdm.visible", true);
  pref("media.gmp-widevinecdm.enabled", true);
  pref("media.gmp-widevinecdm.chromium-guid", "oimompecagnajdejgnnjijobebaeigek");
  pref("media.gmp-widevinecdm.force-chromium-update", false);
  pref("media.gmp-widevinecdm.force-chromium-beta", false);
#if defined(MOZ_WMF_CDM) && defined(_M_AMD64)
  pref("media.gmp-widevinecdm-l1.forceInstall", false);
  pref("media.gmp-widevinecdm-l1.chromium-guid", "neifaoindggfcjicffkgpmnlppeffabd");
  pref("media.gmp-widevinecdm-l1.force-chromium-update", false);
  pref("media.gmp-widevinecdm-l1.force-chromium-beta", false);
#ifdef NIGHTLY_BUILD
  pref("media.gmp-widevinecdm-l1.visible", true);
  pref("media.gmp-widevinecdm-l1.enabled", true);
#else
  pref("media.gmp-widevinecdm-l1.visible", false);
  pref("media.gmp-widevinecdm-l1.enabled", false);
#endif
#endif
#endif

pref("media.gmp-gmpopenh264.visible", true);
pref("media.gmp-gmpopenh264.enabled", true);

pref("media.videocontrols.picture-in-picture.enabled", true);
pref("media.videocontrols.picture-in-picture.audio-toggle.enabled", true);
pref("media.videocontrols.picture-in-picture.video-toggle.enabled", true);
pref("media.videocontrols.picture-in-picture.video-toggle.visibility-threshold", "1.0");
pref("media.videocontrols.picture-in-picture.keyboard-controls.enabled", true);
pref("media.videocontrols.picture-in-picture.urlbar-button.enabled", true);
pref("media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled", false);

// TODO (Bug 1817084) - This pref is used for managing translation preferences
// in the Firefox Translations addon. It should be removed when the addon is
// removed.
pref("browser.translation.neverForLanguages", "");

// Enable Firefox translations powered by the Bergamot translations
// engine https://browser.mt/.
pref("browser.translations.enable", true);

// Enable the new Firefox Translations Settings UI Design
pref("browser.translations.newSettingsUI.enable", false);

// Enable Firefox Select translations powered by Bergamot translations
// engine https://browser.mt/.
pref("browser.translations.select.enable", true);

// Telemetry settings.
// Determines if Telemetry pings can be archived locally.
pref("toolkit.telemetry.archive.enabled", true);
// Enables sending the shutdown ping when Firefox shuts down.
pref("toolkit.telemetry.shutdownPingSender.enabled", true);
// Enables using the `pingsender` background task.
pref("toolkit.telemetry.shutdownPingSender.backgroundtask.enabled", false);
// Enables sending the shutdown ping using the pingsender from the first session.
pref("toolkit.telemetry.shutdownPingSender.enabledFirstSession", false);
// Enables sending a duplicate of the first shutdown ping from the first session.
pref("toolkit.telemetry.firstShutdownPing.enabled", true);
// Enables sending the 'new-profile' ping on new profiles.
pref("toolkit.telemetry.newProfilePing.enabled", true);
// Enables sending 'update' pings on Firefox updates.
pref("toolkit.telemetry.updatePing.enabled", true);
// Enables sending 'bhr' pings when the browser hangs.
pref("toolkit.telemetry.bhrPing.enabled", true);

// Enable GMP support in the addon manager.
pref("media.gmp-provider.enabled", true);

// Enable Dynamic First-Party Isolation by default.
pref("network.cookie.cookieBehavior", 5 /* BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN */);

// Enable Dynamic First-Party Isolation in the private browsing mode.
pref("network.cookie.cookieBehavior.pbmode", 5 /* BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN */);

// Enable fingerprinting blocking by default for all channels, only on desktop.
pref("privacy.trackingprotection.fingerprinting.enabled", true);

// Enable cryptomining blocking by default for all channels, only on desktop.
pref("privacy.trackingprotection.cryptomining.enabled", true);

// Skip earlyBlankFirstPaint by default if resistFingerprinting is enabled.
pref("privacy.resistFingerprinting.skipEarlyBlankFirstPaint", true);

pref("browser.contentblocking.database.enabled", true);

// Enable Strip on Share by default on desktop
pref("privacy.query_stripping.strip_on_share.enabled", true);

pref("browser.contentblocking.cryptomining.preferences.ui.enabled", true);
pref("browser.contentblocking.fingerprinting.preferences.ui.enabled", true);
// Enable cookieBehavior = BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN as an option in the custom category ui
pref("browser.contentblocking.reject-and-isolate-cookies.preferences.ui.enabled", true);

// Possible values for browser.contentblocking.features.strict pref:
//   Tracking Protection:
//     "tp": tracking protection enabled
//     "-tp": tracking protection disabled
//   Tracking Protection in private windows:
//     "tpPrivate": tracking protection in private windows enabled
//     "-tpPrivate": tracking protection in private windows disabled
//   Fingerprinting:
//     "fp": fingerprinting blocking enabled
//     "-fp": fingerprinting blocking disabled
//   Cryptomining Tracking Protection:
//     "cryptoTP": cryptomining blocking enabled
//     "-cryptoTP": cryptomining blocking disabled
//   Social Tracking Protection:
//     "stp": social tracking protection enabled
//     "-stp": social tracking protection disabled
//   Email Tracking Protection:
//     "emailTP": email tracking protection enabled
//     "-emailTP": email tracking protection disabled
//   Email Tracking Protection in private windows:
//     "emailTPPrivate": email tracking protection in private windows enabled
//     "-emailTPPrivate": email tracking protection in private windows disabled
//   Consent Manager Skipping:
//     "consentmanagerSkip": consent manager skipping enabled
//     "-consentmanagerSkip": consent manager skipping disabled
//   Consent Manager Skipping in private windows:
//     "consentmanagerSkipPrivate": consent manager skipping in private windows enabled
//     "-consentmanagerSkipPrivate": consent manager skipping in private windows disabled
//   Level 2 Tracking list in normal windows:
//     "lvl2": Level 2 tracking list enabled
//     "-lvl2": Level 2 tracking list disabled
//   Restrict relaxing default referrer policy:
//     "rp": Restrict relaxing default referrer policy enabled
//     "-rp": Restrict relaxing default referrer policy disabled
//   Restrict relaxing default referrer policy for top navigation:
//     "rpTop": Restrict relaxing default referrer policy enabled
//     "-rpTop": Restrict relaxing default referrer policy disabled
//   OCSP cache partitioning:
//     "ocsp": OCSP cache partitioning enabled
//     "-ocsp": OCSP cache partitioning disabled
//   Query parameter stripping:
//     "qps": Query parameter stripping enabled
//     "-qps": Query parameter stripping disabled
//   Query parameter stripping for private windows:
//     "qpsPBM": Query parameter stripping enabled in private windows
//     "-qpsPBM": Query parameter stripping disabled in private windows
//   Fingerprinting Protection:
//     "fpp": Fingerprinting Protection enabled
//     "-fpp": Fingerprinting Protection disabled
//   Fingerprinting Protection for private windows:
//     "fppPrivate": Fingerprinting Protection enabled in private windows
//     "-fppPrivate": Fingerprinting Protection disabled in private windows
//   Cookie behavior:
//     "cookieBehavior0": cookie behaviour BEHAVIOR_ACCEPT
//     "cookieBehavior1": cookie behaviour BEHAVIOR_REJECT_FOREIGN
//     "cookieBehavior2": cookie behaviour BEHAVIOR_REJECT
//     "cookieBehavior3": cookie behaviour BEHAVIOR_LIMIT_FOREIGN
//     "cookieBehavior4": cookie behaviour BEHAVIOR_REJECT_TRACKER
//     "cookieBehavior5": cookie behaviour BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN
//   Cookie behavior for private windows:
//     "cookieBehaviorPBM0": cookie behaviour BEHAVIOR_ACCEPT
//     "cookieBehaviorPBM1": cookie behaviour BEHAVIOR_REJECT_FOREIGN
//     "cookieBehaviorPBM2": cookie behaviour BEHAVIOR_REJECT
//     "cookieBehaviorPBM3": cookie behaviour BEHAVIOR_LIMIT_FOREIGN
//     "cookieBehaviorPBM4": cookie behaviour BEHAVIOR_REJECT_TRACKER
//     "cookieBehaviorPBM5": cookie behaviour BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN
//   Third-party cookie deprecation behavior:
//     "3pcd": Third-party cookie deprecation enabled
//     "-3pcd": Third-party cookie deprecation disabled
//   Bounce Tracking Protection:
//     "btp": BTP enabled
//     "-btp": BTP disabled
// One value from each section must be included in the browser.contentblocking.features.strict pref.
pref("browser.contentblocking.features.strict", "tp,tpPrivate,cookieBehavior5,cookieBehaviorPBM5,cryptoTP,fp,stp,emailTP,emailTPPrivate,-consentmanagerSkip,-consentmanagerSkipPrivate,lvl2,rp,rpTop,ocsp,qps,qpsPBM,fpp,fppPrivate,btp");

// Hide the "Change Block List" link for trackers/tracking content in the custom
// Content Blocking/ETP panel. By default, it will not be visible. There is also
// an UI migration in place to set this pref to true if a user has a custom block
// lists enabled.
pref("browser.contentblocking.customBlockList.preferences.ui.enabled", false);

// Enable Protections report's Lockwise card by default.
pref("browser.contentblocking.report.lockwise.enabled", true);

// Disable rotections report's Monitor card by default. The new Monitor API does
// not support this feature as of now. See Bug 1815751.
pref("browser.contentblocking.report.monitor.enabled", false);

// Disable Protections report's Proxy card by default.
pref("browser.contentblocking.report.proxy.enabled", false);

// Disable the mobile promotion by default.
pref("browser.contentblocking.report.show_mobile_app", true);

// Locales in which Send to Device emails are supported
// The most recent list of supported locales can be found at https://github.com/mozilla/bedrock/blob/6a08c876f65924651554decc57b849c00874b4e7/bedrock/settings/base.py#L963
pref("browser.send_to_device_locales", "de,en-GB,en-US,es-AR,es-CL,es-ES,es-MX,fr,id,pl,pt-BR,ru,zh-TW");

// Avoid advertising in certain regions. Comma separated string of two letter ISO 3166-1 country codes.
pref("browser.vpn_promo.disallowed_regions", "ae,by,cn,cu,iq,ir,kp,om,ru,sd,sy,tm,tr");

// Default to enabling VPN promo messages to be shown when specified and allowed
pref("browser.vpn_promo.enabled", true);
// Only show vpn card to certain regions. Comma separated string of two letter ISO 3166-1 country codes.
// The most recent list of supported countries can be found at https://support.mozilla.org/en-US/kb/mozilla-vpn-countries-available-subscribe
// The full lists of supported country codes can also be found at https://github.com/mozilla/bedrock/search?q=VPN_COUNTRY_CODES and https://github.com/mozilla/bedrock/search?q=VPN_MOBILE_SUB_COUNTRY_CODES
pref("browser.contentblocking.report.vpn_regions", "as,at,au,bd,be,bg,br,ca,ch,cl,co,cy,cz,de,dk,ee,eg,es,fi,fr,gb,gg,gr,hr,hu,id,ie,im,in,io,it,je,ke,kr,lt,lu,lv,ma,mp,mt,mx,my,ng,nl,no,nz,pl,pr,pt,ro,sa,se,sg,si,sk,sn,th,tr,tw,ua,ug,uk,um,us,vg,vi,vn,za");

// Avoid advertising Focus in certain regions.  Comma separated string of two letter
// ISO 3166-1 country codes.
pref("browser.promo.focus.disallowed_regions", "cn");

// Default to enabling focus promos to be shown where allowed.
pref("browser.promo.focus.enabled", true);

// Default to enabling pin promos to be shown where allowed.
pref("browser.promo.pin.enabled", true);

// Default to enabling cookie banner reduction promos to be shown where allowed.
// Set to true for Fx113 (see bug 1808611)
pref("browser.promo.cookiebanners.enabled", false);

pref("browser.contentblocking.report.hide_vpn_banner", false);
pref("browser.contentblocking.report.vpn_sub_id", "sub_HrfCZF7VPHzZkA");

pref("browser.contentblocking.report.monitor.url", "https://monitor.firefox.com/?entrypoint=protection_report_monitor&utm_source=about-protections");
pref("browser.contentblocking.report.monitor.how_it_works.url", "https://monitor.firefox.com/about");
pref("browser.contentblocking.report.monitor.sign_in_url", "https://monitor.firefox.com/oauth/init?entrypoint=protection_report_monitor&utm_source=about-protections&email=");
pref("browser.contentblocking.report.monitor.preferences_url", "https://monitor.firefox.com/user/preferences");
pref("browser.contentblocking.report.monitor.home_page_url", "https://monitor.firefox.com/user/dashboard");
pref("browser.contentblocking.report.manage_devices.url", "https://accounts.firefox.com/settings/clients");
pref("browser.contentblocking.report.endpoint_url", "https://monitor.firefox.com/user/breach-stats?includeResolved=true");
pref("browser.contentblocking.report.proxy_extension.url", "https://fpn.firefox.com/browser?utm_source=firefox-desktop&utm_medium=referral&utm_campaign=about-protections&utm_content=about-protections");
pref("browser.contentblocking.report.mobile-ios.url", "https://apps.apple.com/app/firefox-private-safe-browser/id989804926");
pref("browser.contentblocking.report.mobile-android.url", "https://play.google.com/store/apps/details?id=org.mozilla.firefox&referrer=utm_source%3Dprotection_report%26utm_content%3Dmobile_promotion");
pref("browser.contentblocking.report.vpn.url", "https://vpn.mozilla.org/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_campaign=about-protections-card");
pref("browser.contentblocking.report.vpn-promo.url", "https://vpn.mozilla.org/?utm_source=firefox-browser&utm_medium=firefox-browser&utm_campaign=about-protections-top-promo");
pref("browser.contentblocking.report.vpn-android.url", "https://play.google.com/store/apps/details?id=org.mozilla.firefox.vpn&referrer=utm_source%3Dfirefox-browser%26utm_medium%3Dfirefox-browser%26utm_campaign%3Dabout-protections-mobile-vpn%26anid%3D--");
pref("browser.contentblocking.report.vpn-ios.url", "https://apps.apple.com/us/app/firefox-private-network-vpn/id1489407738");

// Protection Report's SUMO urls
pref("browser.contentblocking.report.lockwise.how_it_works.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/password-manager-report");
pref("browser.contentblocking.report.social.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/social-media-tracking-report");
pref("browser.contentblocking.report.cookie.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/cross-site-tracking-report");
pref("browser.contentblocking.report.tracker.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/tracking-content-report");
pref("browser.contentblocking.report.fingerprinter.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/fingerprinters-report");
pref("browser.contentblocking.report.cryptominer.url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/cryptominers-report");

pref("browser.contentblocking.cfr-milestone.enabled", true);
pref("browser.contentblocking.cfr-milestone.milestone-achieved", 0);
// Milestones should always be in increasing order
pref("browser.contentblocking.cfr-milestone.milestones", "[1000, 5000, 10000, 25000, 50000, 100000, 250000, 314159, 500000, 750000, 1000000, 1250000, 1500000, 1750000, 2000000, 2250000, 2500000, 8675309]");

// Controls the initial state of the protections panel collapsible info message.
pref("browser.protections_panel.infoMessage.seen", false);

// Always enable newtab segregation using containers
pref("privacy.usercontext.about_newtab_segregation.enabled", true);
// Enable Contextual Identity Containers
#ifdef NIGHTLY_BUILD
  pref("privacy.userContext.enabled", true);
  pref("privacy.userContext.ui.enabled", true);
#else
  pref("privacy.userContext.enabled", false);
  pref("privacy.userContext.ui.enabled", false);
#endif
pref("privacy.userContext.extension", "");
// allows user to open container menu on a left click instead of a new
// tab in the default container
pref("privacy.userContext.newTabContainerOnLeftClick.enabled", false);

// Set to true to allow the user to silence all notifications when
// sharing the screen.
pref("privacy.webrtc.allowSilencingNotifications", true);
pref("privacy.webrtc.hideGlobalIndicator", false);

// Set to true to add toggles to the WebRTC indicator for globally
// muting the camera and microphone.
pref("privacy.webrtc.globalMuteToggles", false);

// Set to true to enable a warning displayed when attempting
// to switch tabs in a window that's being shared over WebRTC.
pref("privacy.webrtc.sharedTabWarning", false);

// Defines a grace period after camera or microphone use ends, where permission
// is granted (even past navigation) to this tab + origin + device. This avoids
// re-prompting without the user having to persist permission to the site, in a
// common case of a web conference asking them for the camera in a lobby page,
// before navigating to the actual meeting room page. Doesn't survive tab close.
pref("privacy.webrtc.deviceGracePeriodTimeoutMs", 3600000);

// Bug 1857254 - MacOS 14 displays two (microphone/camera/screen share) icons in the menu bar
// This pref can be used to hide the firefox camera icon on macos 14 and above to avoid
// duplicating the macos camera icon. We show the icon by default, users can choose to flip
// the pref to hide the icons
pref("privacy.webrtc.showIndicatorsOnMacos14AndAbove", true);

// Enable Smartblock embed placeholders
pref("extensions.webcompat.smartblockEmbeds.enabled", true);

// Enable including the content in the window title.
// PBM users might want to disable this to avoid a possible source of disk
// leaks.
pref("privacy.exposeContentTitleInWindow", true);
pref("privacy.exposeContentTitleInWindow.pbm", true);

// Run media transport in a separate process?
pref("media.peerconnection.mtransport_process", true);

// Whether the "Close duplicate tabs" tab context menu is enabled.
pref("browser.tabs.context.close-duplicate.enabled", true);

// For speculatively warming up tabs to improve perceived
// performance while using the async tab switcher.
pref("browser.tabs.remote.warmup.enabled", true);

// Caches tab layers to improve perceived performance
// of tab switches.
pref("browser.tabs.remote.tabCacheSize", 0);

pref("browser.tabs.remote.warmup.maxTabs", 3);
pref("browser.tabs.remote.warmup.unloadDelayMs", 2000);

// For the about:tabcrashed page
pref("browser.tabs.crashReporting.sendReport", true);
pref("browser.tabs.crashReporting.includeURL", false);

// Enables the "Unload Tab" context menu item
#ifdef EARLY_BETA_OR_EARLIER
pref("browser.tabs.unloadTabInContextMenu", true);
#else
pref("browser.tabs.unloadTabInContextMenu", false);
#endif

// Whether tabs that have been explicitly unloaded
// are faded out in the tab bar.
#ifdef EARLY_BETA_OR_EARLIER
pref("browser.tabs.fadeOutExplicitlyUnloadedTabs", true);
#else
pref("browser.tabs.fadeOutExplicitlyUnloadedTabs", false);
#endif

// Whether unloaded tabs (either from session restore or because
// they are explicitly unloaded) are faded out in the tab bar.
pref("browser.tabs.fadeOutUnloadedTabs", false);

// If true, unprivileged extensions may use experimental APIs on
// nightly and developer edition.
pref("extensions.experiments.enabled", false);

#if defined(XP_WIN)
  pref("dom.ipc.processPriorityManager.backgroundUsesEcoQoS", true);
#endif

// Don't limit how many nodes we care about on desktop:
pref("reader.parse-node-limit", 0);

// On desktop, we want the URLs to be included here for ease of debugging,
// and because (normally) these errors are not persisted anywhere.
pref("reader.errors.includeURLs", true);

pref("view_source.tab", true);

// These are the thumbnail width/height set in about:newtab.
// If you change this, ENSURE IT IS THE SAME SIZE SET
// by about:newtab. These values are in CSS pixels.
pref("toolkit.pageThumbs.minWidth", 280);
pref("toolkit.pageThumbs.minHeight", 190);

pref("browser.esedbreader.loglevel", "Error");

pref("browser.laterrun.enabled", false);

#ifdef FUZZING_SNAPSHOT
pref("dom.ipc.processPrelaunch.enabled", false);
#else
pref("dom.ipc.processPrelaunch.enabled", true);
#endif

pref("browser.migrate.bookmarks-file.enabled", true);
pref("browser.migrate.brave.enabled", true);
pref("browser.migrate.canary.enabled", true);

pref("browser.migrate.chrome.enabled", true);
// See comments in bug 1340115 on how we got to this number.
pref("browser.migrate.chrome.history.limit", 2000);
pref("browser.migrate.chrome.payment_methods.enabled", true);
pref("browser.migrate.chrome.extensions.enabled", true);
pref("browser.migrate.chrome.get_permissions.enabled", true);

pref("browser.migrate.chrome-beta.enabled", true);
pref("browser.migrate.chrome-dev.enabled", true);
pref("browser.migrate.chromium.enabled", true);
pref("browser.migrate.chromium-360se.enabled", true);
pref("browser.migrate.chromium-edge.enabled", true);
pref("browser.migrate.chromium-edge-beta.enabled", true);
pref("browser.migrate.edge.enabled", true);
pref("browser.migrate.firefox.enabled", true);
pref("browser.migrate.ie.enabled", true);
pref("browser.migrate.opera.enabled", true);
pref("browser.migrate.opera-gx.enabled", true);
pref("browser.migrate.safari.enabled", true);
pref("browser.migrate.vivaldi.enabled", true);

pref("browser.migrate.content-modal.import-all.enabled", true);

// Values can be: "default", "autoclose", "standalone", "embedded".
pref("browser.migrate.content-modal.about-welcome-behavior", "embedded");

// The maximum age of history entries we'll import, in days.
pref("browser.migrate.history.maxAgeInDays", 180);

// These following prefs are set to true if the user has at some
// point in the past migrated one of these resource types from
// another browser. We also attempt to transfer these preferences
// across profile resets.
pref("browser.migrate.interactions.bookmarks", false);
pref("browser.migrate.interactions.csvpasswords", false);
pref("browser.migrate.interactions.history", false);
pref("browser.migrate.interactions.passwords", false);

pref("browser.migrate.preferences-entrypoint.enabled", true);

pref("extensions.pocket.api", "api.getpocket.com");
pref("extensions.pocket.bffApi", "firefox-api-proxy.cdn.mozilla.net");
pref("extensions.pocket.bffRecentSaves", true);
pref("extensions.pocket.enabled", true);
pref("extensions.pocket.oAuthConsumerKey", "40249-e88c401e1b1f2242d9e441c4");
pref("extensions.pocket.oAuthConsumerKeyBff", "94110-6d5ff7a89d72c869766af0e0");
pref("extensions.pocket.site", "getpocket.com");

// Enable Pocket button home panel for non link pages.
pref("extensions.pocket.showHome", true);

// Control what version of the logged out doorhanger is displayed
// Possibilities are: `control`, `control-one-button`, `variant_a`, `variant_b`, `variant_c`
pref("extensions.pocket.loggedOutVariant", "control");

// Just for the new Pocket panels, enables the email signup button.
pref("extensions.pocket.refresh.emailButton.enabled", false);
// Hides the recently saved section in the home panel.
pref("extensions.pocket.refresh.hideRecentSaves.enabled", false);

// "available"      - user can see feature offer.
// "offered"        - we have offered feature to user and they have not yet made a decision.
// "enabled"        - user opted in to the feature.
// "disabled"       - user opted out of the feature.
pref("signon.firefoxRelay.feature", "available");
// Should Firefox show Relay to all browsers, or only those signed-in to FxA?
// Keep it hidden from about:config for now.
// pref("signon.firefoxRelay.showToAllBrowsers", false);
pref("signon.firefoxRelay.firstOfferVersionFallback", "control");
pref("signon.management.page.breach-alerts.enabled", true);
pref("signon.management.page.vulnerable-passwords.enabled", true);
pref("signon.management.page.sort", "name");
// The utm_creative value is appended within the code (specific to the location on
// where it is clicked). Be sure that if these two prefs are updated, that
// the utm_creative param be last.
pref("signon.management.page.breachAlertUrl",
     "https://monitor.firefox.com/breach-details/");
pref("signon.passwordEditCapture.enabled", true);
pref("signon.relatedRealms.enabled", false);
pref("signon.showAutoCompleteFooter", true);
pref("signon.showAutoCompleteImport", "import");
pref("signon.suggestImportCount", 3);

// Whether or not the browser should scan for unsubmitted
// crash reports, and then show a notification for submitting
// those reports.
#ifdef NIGHTLY_BUILD
  pref("browser.crashReports.unsubmittedCheck.enabled", true);
#else
  pref("browser.crashReports.unsubmittedCheck.enabled", false);
#endif

// chancesUntilSuppress is how many times we'll show the unsubmitted
// crash report notification across different days and shutdown
// without a user choice before we suppress the notification for
// some number of days.
pref("browser.crashReports.unsubmittedCheck.chancesUntilSuppress", 4);
pref("browser.crashReports.unsubmittedCheck.autoSubmit2", false);

// Preferences for the form autofill toolkit component.
// Checkbox in sync options for credit card data sync service
pref("services.sync.engine.creditcards.available", true);

// Whether or not to restore a session with lazy-browser tabs.
pref("browser.sessionstore.restore_tabs_lazily", true);

pref("browser.suppress_first_window_animation", true);

// Preference that allows individual users to disable Screenshots.
pref("extensions.screenshots.disabled", false);

// Preference that determines whether Screenshots uses the dedicated browser component
pref("screenshots.browser.component.enabled", true);

// Preference that determines what button to focus
pref("screenshots.browser.component.last-saved-method", "download");

// Preference that prevents events from reaching the content page.
pref("screenshots.browser.component.preventContentEvents", true);

// DoH Rollout: whether to clear the mode value at shutdown.
pref("doh-rollout.clearModeOnShutdown", false);

// Normandy client preferences
pref("app.normandy.api_url", "https://normandy.cdn.mozilla.net/api/v1");
pref("app.normandy.dev_mode", false);
pref("app.normandy.enabled", true);
pref("app.normandy.first_run", true);
pref("app.normandy.logging.level", 50); // Warn
pref("app.normandy.run_interval_seconds", 21600); // 6 hours
pref("app.normandy.shieldLearnMoreUrl", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/shield");
pref("app.normandy.last_seen_buildid", "");
pref("app.normandy.onsync_skew_sec", 600);
#ifdef MOZ_DATA_REPORTING
  pref("app.shield.optoutstudies.enabled", true);
#else
  pref("app.shield.optoutstudies.enabled", false);
#endif

// Multi-lingual preferences:
//  *.enabled - Are langpacks available for the build of Firefox?
//  *.downloadEnabled - Langpacks are allowed to be downloaded from AMO. AMO only serves
//      langpacks for release and beta. Unsupported releases (like Nightly) can be
//      manually tested with the following preference:
//      extensions.getAddons.langpacks.url: https://mock-amo-language-tools.glitch.me/?app=firefox&type=language&appversion=%VERSION%
//  *.liveReload - Switching a langpack will change the language without a restart.
//  *.liveReloadBidirectional - Allows switching when moving between LTR and RTL
//      languages without a full restart.
//  *.aboutWelcome.languageMismatchEnabled - Enables an onboarding menu in about:welcome
//      to allow a user to change their language when there is a language mismatch between
//      the app and browser.
#if defined(RELEASE_OR_BETA) && !defined(MOZ_DEV_EDITION)
  pref("intl.multilingual.enabled", true);
  pref("intl.multilingual.downloadEnabled", true);
  pref("intl.multilingual.liveReload", true);
  pref("intl.multilingual.liveReloadBidirectional", false);
  pref("intl.multilingual.aboutWelcome.languageMismatchEnabled", true);
#else
  pref("intl.multilingual.enabled", false);
  pref("intl.multilingual.downloadEnabled", false);
  pref("intl.multilingual.liveReload", false);
  pref("intl.multilingual.liveReloadBidirectional", false);
  pref("intl.multilingual.aboutWelcome.languageMismatchEnabled", false);
#endif

// Coverage ping is disabled by default.
pref("toolkit.coverage.enabled", false);
pref("toolkit.coverage.endpoint.base", "https://coverage.mozilla.org");

// Discovery prefs
pref("browser.discovery.enabled", true);
pref("browser.discovery.containers.enabled", true);
pref("browser.discovery.sites", "addons.mozilla.org");

pref("browser.engagement.recent_visited_origins.expiry", 86400); // 24 * 60 * 60 (24 hours in seconds)
pref("browser.engagement.downloads-button.has-used", false);
pref("browser.engagement.fxa-toolbar-menu-button.has-used", false);
pref("browser.engagement.home-button.has-used", false);
pref("browser.engagement.sidebar-button.has-used", false);
pref("browser.engagement.library-button.has-used", false);
pref("browser.engagement.ctrlTab.has-used", false);

pref("browser.aboutConfig.showWarning", true);

pref("browser.toolbars.keyboard_navigation", true);

// The visibility of the bookmarks toolbar.
// "newtab": Show on the New Tab Page
// "always": Always show
// "never": Never show
pref("browser.toolbars.bookmarks.visibility", "newtab");

// Visibility of the "Show Other Bookmarks" menuitem in the
// bookmarks toolbar contextmenu.
pref("browser.toolbars.bookmarks.showOtherBookmarks", true);


// Felt Privacy pref to control simplified private browsing UI
pref("browser.privatebrowsing.felt-privacy-v1", false);
pref("security.certerrors.felt-privacy-v1", false);

// Prefs to control the Firefox Account toolbar menu.
// This pref will surface existing Firefox Account information
// as a button next to the hamburger menu. It allows
// quick access to sign-in and manage your Firefox Account.
pref("identity.fxaccounts.toolbar.enabled", true);
pref("identity.fxaccounts.toolbar.accessed", false);
pref("identity.fxaccounts.toolbar.defaultVisible", true);

// Prefs to control Firefox Account panels that shows call to actions
// for other supported Mozilla products
pref("identity.fxaccounts.toolbar.pxiToolbarEnabled", true);
pref("identity.fxaccounts.toolbar.pxiToolbarEnabled.monitorEnabled", true);
pref("identity.fxaccounts.toolbar.pxiToolbarEnabled.relayEnabled", true);
pref("identity.fxaccounts.toolbar.pxiToolbarEnabled.vpnEnabled", true);

// Prefs to control Mozilla account panels that shows an updated flow
// for users who don't have sync enabled
pref("identity.fxaccounts.toolbar.syncSetup.panelAccessed", false);

// Toolbox preferences
pref("devtools.toolbox.footer.height", 250);
pref("devtools.toolbox.sidebar.width", 500);
pref("devtools.toolbox.host", "bottom");
pref("devtools.toolbox.previousHost", "right");
pref("devtools.toolbox.selectedTool", "inspector");
pref("devtools.toolbox.zoomValue", "1");
pref("devtools.toolbox.splitconsole.enabled", true);
pref("devtools.toolbox.splitconsole.open", false);
pref("devtools.toolbox.splitconsoleHeight", 100);
pref("devtools.toolbox.tabsOrder", "");
// This is only used for local Web Extension debugging,
// and allows to keep the window on top of all others,
// so that you can debug the Firefox window, while keeping the devtools
// always visible
pref("devtools.toolbox.alwaysOnTop", true);

// When the Multiprocess Browser Toolbox is enabled, you can configure the scope of it:
// - "everything" will enable debugging absolutely everything in the browser
//   All processes, all documents, all workers, all add-ons.
// - "parent-process" will restrict debugging to the parent process
//   All privileged javascript, documents and workers running in the parent process.
pref("devtools.browsertoolbox.scope", "parent-process");

// This preference will enable watching top-level targets from the server side.
pref("devtools.target-switching.server.enabled", true);

// In DevTools, create a target for each frame (i.e. not only for top-level document and
// remote frames).
pref("devtools.every-frame-target.enabled", true);

// Controls the hability to debug popups from the same DevTools
// of the original tab the popups are coming from
pref("devtools.popups.debug", false);

// Toolbox Button preferences
pref("devtools.command-button-pick.enabled", true);
pref("devtools.command-button-frames.enabled", true);
pref("devtools.command-button-responsive.enabled", true);
pref("devtools.command-button-screenshot.enabled", false);
pref("devtools.command-button-rulers.enabled", false);
pref("devtools.command-button-measure.enabled", false);
pref("devtools.command-button-noautohide.enabled", false);
pref("devtools.command-button-errorcount.enabled", true);
#ifndef MOZILLA_OFFICIAL
  pref("devtools.command-button-experimental-prefs.enabled", true);
#endif

// Inspector preferences
// Enable the Inspector
pref("devtools.inspector.enabled", true);
// What was the last active sidebar in the inspector
pref("devtools.inspector.selectedSidebar", "layoutview");
pref("devtools.inspector.activeSidebar", "layoutview");
pref("devtools.inspector.remote", false);

// Enable the 3 pane mode in the inspector
pref("devtools.inspector.three-pane-enabled", true);
// Enable the 3 pane mode in the chrome inspector
pref("devtools.inspector.chrome.three-pane-enabled", false);
// Collapse pseudo-elements by default in the rule-view
pref("devtools.inspector.show_pseudo_elements", false);
// The default size for image preview tooltips in the rule-view/computed-view/markup-view
pref("devtools.inspector.imagePreviewTooltipSize", 300);
// Enable user agent style inspection in rule-view
pref("devtools.inspector.showUserAgentStyles", false);
// Show native anonymous content and user agent shadow roots
pref("devtools.inspector.showAllAnonymousContent", false);
// Enable overflow debugging in the inspector.
pref("devtools.overflow.debugging.enabled", true);
// Enable drag to edit properties in the inspector rule view.
pref("devtools.inspector.draggable_properties", true);

// Grid highlighter preferences
pref("devtools.gridinspector.gridOutlineMaxColumns", 50);
pref("devtools.gridinspector.gridOutlineMaxRows", 50);
pref("devtools.gridinspector.showGridAreas", false);
pref("devtools.gridinspector.showGridLineNumbers", false);
pref("devtools.gridinspector.showInfiniteLines", false);
// Max number of grid highlighters that can be displayed
pref("devtools.gridinspector.maxHighlighters", 3);

// Whether or not simplified highlighters should be used when
// prefers-reduced-motion is enabled.
pref("devtools.inspector.simple-highlighters-reduced-motion", false);
// Wheter or not Enter on inplace editor in the Rules view moves focus and activates
// next inplace editor.
pref("devtools.inspector.rule-view.focusNextOnEnter", true);

// Whether or not the box model panel is opened in the layout view
pref("devtools.layout.boxmodel.opened", true);
// Whether or not the flexbox panel is opened in the layout view
pref("devtools.layout.flexbox.opened", true);
// Whether or not the flexbox container panel is opened in the layout view
pref("devtools.layout.flex-container.opened", true);
// Whether or not the flexbox item panel is opened in the layout view
pref("devtools.layout.flex-item.opened", true);
// Whether or not the grid inspector panel is opened in the layout view
pref("devtools.layout.grid.opened", true);

// Enable hovering Box Model values and jumping to their source CSS rule in the
// rule-view.
#if defined(NIGHTLY_BUILD)
  pref("devtools.layout.boxmodel.highlightProperty", true);
#else
  pref("devtools.layout.boxmodel.highlightProperty", false);
#endif

// By how many times eyedropper will magnify pixels
pref("devtools.eyedropper.zoom", 6);

// Enable to collapse attributes that are too long.
pref("devtools.markup.collapseAttributes", true);
// Length to collapse attributes
pref("devtools.markup.collapseAttributeLength", 120);
// Whether to auto-beautify the HTML on copy.
pref("devtools.markup.beautifyOnCopy", false);

// DevTools default color unit
pref("devtools.defaultColorUnit", "authored");

// Enable the Memory tools
pref("devtools.memory.enabled", true);

pref("devtools.memory.custom-census-displays", "{}");
pref("devtools.memory.custom-label-displays", "{}");
pref("devtools.memory.custom-tree-map-displays", "{}");

pref("devtools.memory.max-individuals", 1000);
pref("devtools.memory.max-retaining-paths", 10);

// Enable the Performance tools
pref("devtools.performance.enabled", true);

// The default cache UI setting
pref("devtools.cache.disabled", false);

// The default service workers UI setting
pref("devtools.serviceWorkers.testing.enabled", false);

// Enable the Network Monitor
pref("devtools.netmonitor.enabled", true);

pref("devtools.netmonitor.features.requestBlocking", true);

// Enable the Application panel
pref("devtools.application.enabled", true);

// Enable the custom formatters feature
// This preference represents the user's choice to enable the custom formatters feature.
// While the preference above will be removed once the feature is stable, this one is menat to stay.
pref("devtools.custom-formatters.enabled", false);

// The default Network Monitor UI settings
pref("devtools.netmonitor.panes-network-details-width", 550);
pref("devtools.netmonitor.panes-network-details-height", 450);
pref("devtools.netmonitor.panes-search-width", 550);
pref("devtools.netmonitor.panes-search-height", 450);
pref("devtools.netmonitor.filters", "[\"all\"]");
pref("devtools.netmonitor.requestfilter", "");
pref("devtools.netmonitor.visibleColumns",
    "[\"override\",\"status\",\"method\",\"domain\",\"file\",\"initiator\",\"type\",\"transferred\",\"contentSize\",\"waterfall\"]"
);
pref("devtools.netmonitor.columnsData",
  '[{"name":"override","minWidth":20,"width":2}, {"name":"status","minWidth":30,"width":5}, {"name":"method","minWidth":30,"width":5}, {"name":"domain","minWidth":30,"width":10}, {"name":"file","minWidth":30,"width":25}, {"name":"url","minWidth":30,"width":25},{"name":"initiator","minWidth":30,"width":10},{"name":"type","minWidth":30,"width":5},{"name":"transferred","minWidth":30,"width":10},{"name":"contentSize","minWidth":30,"width":5},{"name":"waterfall","minWidth":150,"width":15}]');
pref("devtools.netmonitor.msg.payload-preview-height", 128);
pref("devtools.netmonitor.msg.visibleColumns",
  '["data", "time"]'
);
pref("devtools.netmonitor.msg.displayed-messages.limit", 500);

pref("devtools.netmonitor.response.ui.limit", 10240);
pref("devtools.netmonitor.ui.default-raw-response", false);

// Save request/response bodies yes/no.
pref("devtools.netmonitor.saveRequestAndResponseBodies", true);

// The default Network monitor HAR export setting
pref("devtools.netmonitor.har.defaultLogDir", "");
pref("devtools.netmonitor.har.defaultFileName", "%hostname_Archive [%date]");
pref("devtools.netmonitor.har.jsonp", false);
pref("devtools.netmonitor.har.jsonpCallback", "");
pref("devtools.netmonitor.har.includeResponseBodies", true);
pref("devtools.netmonitor.har.compress", false);
pref("devtools.netmonitor.har.forceExport", false);
pref("devtools.netmonitor.har.pageLoadedTimeout", 1500);
pref("devtools.netmonitor.har.enableAutoExportToFile", false);
pref("devtools.netmonitor.har.multiple-pages", false);

// netmonitor audit
pref("devtools.netmonitor.audits.slow", 500);

// Enable the new Edit and Resend panel
  pref("devtools.netmonitor.features.newEditAndResend", true);

pref("devtools.netmonitor.customRequest", '{}');

// Enable the Storage Inspector
pref("devtools.storage.enabled", true);

// Enable the Style Editor.
pref("devtools.styleeditor.enabled", true);
pref("devtools.styleeditor.autocompletion-enabled", true);
pref("devtools.styleeditor.showAtRulesSidebar", true);
pref("devtools.styleeditor.atRulesSidebarWidth", 238);
pref("devtools.styleeditor.navSidebarWidth", 245);
pref("devtools.styleeditor.transitions", true);

// Screenshot Option Settings.
pref("devtools.screenshot.clipboard.enabled", false);
pref("devtools.screenshot.audio.enabled", true);

// Make sure the DOM panel is hidden by default
pref("devtools.dom.enabled", false);

// Enable the Accessibility panel.
pref("devtools.accessibility.enabled", true);

// Web console filters
pref("devtools.webconsole.filter.error", true);
pref("devtools.webconsole.filter.warn", true);
pref("devtools.webconsole.filter.info", true);
pref("devtools.webconsole.filter.log", true);
pref("devtools.webconsole.filter.debug", true);
pref("devtools.webconsole.filter.css", false);
pref("devtools.webconsole.filter.net", false);
pref("devtools.webconsole.filter.netxhr", false);

// Webconsole autocomplete preference
pref("devtools.webconsole.input.autocomplete",true);

// Set to true to eagerly show the results of webconsole terminal evaluations
// when they don't have side effects.
pref("devtools.webconsole.input.eagerEvaluation", true);

// Browser console filters
pref("devtools.browserconsole.filter.error", true);
pref("devtools.browserconsole.filter.warn", true);
pref("devtools.browserconsole.filter.info", true);
pref("devtools.browserconsole.filter.log", true);
pref("devtools.browserconsole.filter.debug", true);
pref("devtools.browserconsole.filter.css", false);
pref("devtools.browserconsole.filter.net", false);
pref("devtools.browserconsole.filter.netxhr", false);

// Max number of inputs to store in web console history.
pref("devtools.webconsole.inputHistoryCount", 300);

// Persistent logging: |true| if you want the relevant tool to keep all of the
// logged messages after reloading the page, |false| if you want the output to
// be cleared each time page navigation happens.
pref("devtools.webconsole.persistlog", false);
pref("devtools.netmonitor.persistlog", false);

// Web Console timestamp: |true| if you want the logs and instructions
// in the Web Console to display a timestamp, or |false| to not display
// any timestamps.
pref("devtools.webconsole.timestampMessages", false);

// Enable the webconsole sidebar toggle in Nightly builds.
#if defined(NIGHTLY_BUILD)
  pref("devtools.webconsole.sidebarToggle", true);
#else
  pref("devtools.webconsole.sidebarToggle", false);
#endif

// Saved editor mode state in the console.
pref("devtools.webconsole.input.editor", false);
pref("devtools.browserconsole.input.editor", false);

// Editor width for webconsole and browserconsole.
pref("devtools.webconsole.input.editorWidth", 0);
pref("devtools.browserconsole.input.editorWidth", 0);

// Display an onboarding UI for the Editor mode.
pref("devtools.webconsole.input.editorOnboarding", true);

// Enable message grouping in the console, true by default
pref("devtools.webconsole.groupWarningMessages", true);

// Enable network monitoring the browser toolbox console/browser console.
pref("devtools.browserconsole.enableNetworkMonitoring", false);

// Enable client-side mapping service for source maps
pref("devtools.source-map.client-service.enabled", true);

// The number of lines that are displayed in the web console.
pref("devtools.hud.loglimit", 10000);

// The developer tools editor configuration:
// - tabsize: how many spaces to use when a Tab character is displayed.
// - expandtab: expand Tab characters to spaces.
// - keymap: which keymap to use (can be 'default', 'emacs' or 'vim')
// - autoclosebrackets: whether to permit automatic bracket/quote closing.
// - detectindentation: whether to detect the indentation from the file
// - enableCodeFolding: Whether to enable code folding or not.
pref("devtools.editor.tabsize", 2);
pref("devtools.editor.expandtab", true);
pref("devtools.editor.keymap", "default");
pref("devtools.editor.autoclosebrackets", true);
pref("devtools.editor.detectindentation", true);
pref("devtools.editor.enableCodeFolding", true);
pref("devtools.editor.autocomplete", true);

// The angle of the viewport.
pref("devtools.responsive.viewport.angle", 0);
// The width of the viewport.
pref("devtools.responsive.viewport.width", 320);
// The height of the viewport.
pref("devtools.responsive.viewport.height", 480);
// The pixel ratio of the viewport.
pref("devtools.responsive.viewport.pixelRatio", 0);
// Whether or not the viewports are left aligned.
pref("devtools.responsive.leftAlignViewport.enabled", false);
// Whether to reload when touch simulation is toggled
pref("devtools.responsive.reloadConditions.touchSimulation", false);
// Whether to reload when user agent is changed
pref("devtools.responsive.reloadConditions.userAgent", false);
// Whether to show the notification about reloading to apply emulation
pref("devtools.responsive.reloadNotification.enabled", true);
// Whether or not touch simulation is enabled.
pref("devtools.responsive.touchSimulation.enabled", false);
// The user agent of the viewport.
pref("devtools.responsive.userAgent", "");
// Show the custom user agent input by default
pref("devtools.responsive.showUserAgentInput", true);

// Show tab debug targets for This Firefox (on by default for local builds).
#ifdef MOZILLA_OFFICIAL
  pref("devtools.aboutdebugging.local-tab-debugging", false);
#else
  pref("devtools.aboutdebugging.local-tab-debugging", true);
#endif

// Show process debug targets.
pref("devtools.aboutdebugging.process-debugging", true);
// Stringified array of network locations that users can connect to.
pref("devtools.aboutdebugging.network-locations", "[]");
// Debug target pane collapse/expand settings.
pref("devtools.aboutdebugging.collapsibilities.installedExtension", false);
pref("devtools.aboutdebugging.collapsibilities.otherWorker", false);
pref("devtools.aboutdebugging.collapsibilities.serviceWorker", false);
pref("devtools.aboutdebugging.collapsibilities.sharedWorker", false);
pref("devtools.aboutdebugging.collapsibilities.tab", false);
pref("devtools.aboutdebugging.collapsibilities.temporaryExtension", false);

// about:debugging: only show system and hidden extensions in local builds by
// default.
#ifdef MOZILLA_OFFICIAL
  pref("devtools.aboutdebugging.showHiddenAddons", false);
#else
  pref("devtools.aboutdebugging.showHiddenAddons", true);
#endif

// Map top-level await expressions in the console
pref("devtools.debugger.features.map-await-expression", true);

// This relies on javascript.options.asyncstack as well or it has no effect.
pref("devtools.debugger.features.async-captured-stacks", true);
pref("devtools.debugger.features.async-live-stacks", false);

// When debugging a website, this pref controls if extension content scripts applied
// to the currently debugged page should be shown in the Debugger Source Tree
pref("devtools.debugger.show-content-scripts", false);

pref("devtools.debugger.hide-ignored-sources", false);

// When `true` the debugger editor uses Codemirror v6
// and when `false` the debugger editor uses Codemirror v5
// This should be removed once the CM5 code is cleaned up. See Bug 1943909
pref("devtools.debugger.features.codemirror-next", true);

// Disable autohide for DevTools popups and tooltips.
// This is currently not exposed by any UI to avoid making
// about:devtools-toolbox tabs unusable by mistake.
pref("devtools.popup.disable_autohide", false);

// Add support for high contrast mode
#if defined(NIGHTLY_BUILD)
  pref("devtools.high-contrast-mode-support", true);
#else
  pref("devtools.high-contrast-mode-support", false);
#endif

// FirstStartup service time-out in ms
pref("first-startup.timeout", 30000);

// Enable the default browser agent.
// The agent still runs as scheduled if this pref is disabled,
// but it exits immediately before taking any action.
#ifdef XP_WIN
  pref("default-browser-agent.enabled", true);
#endif

// Test Prefs that do nothing for testing
#if defined(EARLY_BETA_OR_EARLIER)
  pref("app.normandy.test-prefs.bool", false);
  pref("app.normandy.test-prefs.integer", 0);
  pref("app.normandy.test-prefs.string", "");
#endif

// Shows 'View Image Info' item in the image context menu
#ifdef MOZ_DEV_EDITION
  pref("browser.menu.showViewImageInfo", true);
#else
  pref("browser.menu.showViewImageInfo", false);
#endif

// Handing URLs to external apps via the "Share URL" menu item could allow a proxy bypass
#ifdef MOZ_PROXY_BYPASS_PROTECTION
  pref("browser.menu.share_url.allow", false);
#endif

// Mozilla-controlled domains that are allowed to use non-standard
// context properties for SVG images for use in the browser UI. Please
// keep this list short. This preference (and SVG `context-` keyword support)
// are expected to go away once a standardized alternative becomes
// available.
pref("svg.context-properties.content.allowed-domains", "profile.accounts.firefox.com,profile.stage.mozaws.net");

// Preference that allows individual users to disable Firefox Translations.
#ifdef NIGHTLY_BUILD
  pref("extensions.translations.disabled", true);
#endif

#if defined(XP_MACOSX) || defined(XP_WIN)
pref("browser.firefoxbridge.enabled", false);
pref("browser.firefoxbridge.extensionOrigins",
    "chrome-extension://gkcbmfjnnjoambnfmihmnkneakghogca/"
);
#endif

// Turn on interaction measurements
pref("browser.places.interactions.enabled", true);

// If the user has seen the Firefox View feature tour this value reflects
// the id of the last screen they saw and whether they completed the tour
pref("browser.firefox-view.feature-tour", "{\"screen\":\"FIREFOX_VIEW_SPOTLIGHT\",\"complete\":false}");
// Number of times the user visited about:firefoxview
pref("browser.firefox-view.view-count", 0);
// Maximum number of rows to show on the "History" page (0 = unlimited).
pref("browser.firefox-view.max-history-rows", 0);
// Enables virtual list functionality in Firefox View.
pref("browser.firefox-view.virtual-list.enabled", true);

// If the user has seen the pdf.js feature tour this value reflects the tour
// message id, the id of the last screen they saw, and whether they completed the tour
pref("browser.pdfjs.feature-tour", "{\"screen\":\"\",\"complete\":false}");


pref("cookiebanners.ui.desktop.enabled", false);

// When true, shows a one-time feature callout for cookie banner blocking.
pref("cookiebanners.ui.desktop.showCallout", false);

// Controls which variant of the cookie banner CFR the user is presented with.
pref("cookiebanners.ui.desktop.cfrVariant", 0);

// Parameters for the swipe-to-navigation icon.
//
// `navigation-icon-{start|end}-position` is the start or the end position of
// the icon movement in response to the user's swipe gesture. `0` means the icon
// positions at the left edge of the browser window. For example on Mac, when
// the user started swipe gesture left to right, the icon appears at a point
// where left side 20px of the icon is outside of the browser window's view.
//
// `navigation-icon-{min|max}-radius` is the minimum or the maximum radius of
// the icon's outer circle size in response to the user's swipe gesture.  `-1`
// means that the circle radius never changes.
#ifdef XP_MACOSX
  pref("browser.swipe.navigation-icon-start-position", -20);
  pref("browser.swipe.navigation-icon-end-position", 0);
  pref("browser.swipe.navigation-icon-min-radius", -1);
  pref("browser.swipe.navigation-icon-max-radius", -1);
#else
  pref("browser.swipe.navigation-icon-start-position", -40);
  pref("browser.swipe.navigation-icon-end-position", 60);
  pref("browser.swipe.navigation-icon-min-radius", 12);
  pref("browser.swipe.navigation-icon-max-radius", 20);
#endif

// Trigger FOG's Artifact Build support on artifact builds.
#ifdef MOZ_ARTIFACT_BUILDS
  pref("telemetry.fog.artifact_build", true);
#endif

#ifdef NIGHTLY_BUILD
  pref("dom.security.credentialmanagement.identity.enabled", true);
  pref("dom.security.credentialmanagement.identity.heavyweight.enabled", true);
#endif

pref("ui.new-webcompat-reporter.enabled", true);

#if defined(EARLY_BETA_OR_EARLIER)
pref("ui.new-webcompat-reporter.send-more-info-link", true);
#else
pref("ui.new-webcompat-reporter.send-more-info-link", false);
#endif

# 0 = disabled, 1 = reason optional, 2 = reason required.
pref("ui.new-webcompat-reporter.reason-dropdown", 2);

pref("ui.new-webcompat-reporter.reason-dropdown.randomized", true);

// Reset Private Browsing Session feature
#if defined(NIGHTLY_BUILD)
  pref("browser.privatebrowsing.resetPBM.enabled", true);
#else
  pref("browser.privatebrowsing.resetPBM.enabled", false);
#endif
// Whether the reset private browsing panel should ask for confirmation before
// performing the clear action.
pref("browser.privatebrowsing.resetPBM.showConfirmationDialog", true);

// the preferences related to the Nimbus experiment, to activate and deactivate
// the the entire rollout (see: bug 1864216 - two prompts, 1877500 - set two in one prompt)
pref("browser.mailto.dualPrompt", false);
// Display a reminder prompt for known webmailers if the prompt was not
// dismissed before the next visit of that webmailer.
pref("browser.mailto.dualPrompt.onLocationChange", false);
// configures after how many minutes is the prompt shown again after it was
// dismissed. This differs from clicking the 'x' button and 'not now' (forever=0)
pref("browser.mailto.dualPrompt.dismissNotNowMinutes", 525600); // one year
pref("browser.mailto.dualPrompt.dismissXClickMinutes", 1440); // one day

// Pref to initialize the BackupService soon after startup.
pref("browser.backup.enabled", true);
// Pref to control whether scheduled backups run or not.
pref("browser.backup.scheduled.enabled", false);
// Pref to control the visibility of the backup section in about:preferences
pref("browser.backup.preferences.ui.enabled", false);
// The number of SQLite database pages to backup per step.
pref("browser.backup.sqlite.pages_per_step", 50);
// The delay between SQLite database backup steps in milliseconds.
pref("browser.backup.sqlite.step_delay_ms", 50);
pref("browser.backup.scheduled.idle-threshold-seconds", 15);
pref("browser.backup.scheduled.minimum-time-between-backups-seconds", 3600);
pref("browser.backup.template.fallback-download.release", "https://www.mozilla.org/firefox/download/thanks/?s=direct&utm_medium=firefox-desktop&utm_source=backup&utm_campaign=firefox-backup-2024&utm_content=control");
pref("browser.backup.template.fallback-download.beta", "https://www.mozilla.org/firefox/channel/desktop/?utm_medium=firefox-desktop&utm_source=backup&utm_campaign=firefox-backup-2024&utm_content=control#beta");
pref("browser.backup.template.fallback-download.aurora", "https://www.mozilla.org/firefox/channel/desktop/?utm_medium=firefox-desktop&utm_source=backup&utm_campaign=firefox-backup-2024&utm_content=control#developer");
pref("browser.backup.template.fallback-download.nightly", "https://www.mozilla.org/firefox/channel/desktop/?utm_medium=firefox-desktop&utm_source=backup&utm_campaign=firefox-backup-2024&utm_content=control#nightly");
pref("browser.backup.template.fallback-download.esr", "https://www.mozilla.org/firefox/enterprise/?utm_medium=firefox-desktop&utm_source=backup&utm_campaign=firefox-backup-2024&utm_content=control#download");

#ifdef NIGHTLY_BUILD
  // Pref to enable the new profiles
  pref("browser.profiles.enabled", true);
#else
  pref("browser.profiles.enabled", false);
#endif
pref("browser.profiles.profile-name.updated", false);
// Whether to allow the user to merge profile data
pref("browser.profiles.sync.allow-danger-merge", false);

pref("startup.homepage_override_url_nimbus", "");
// These prefs are referring to the Fx update version
pref("startup.homepage_override_nimbus_maxVersion", "");
pref("startup.homepage_override_nimbus_minVersion", "");
// Pref to disable all What's New pages
pref("startup.homepage_override_nimbus_disable_wnp", false);

// Pref to enable the content relevancy feature.
pref("toolkit.contentRelevancy.enabled", false);
// Pref to enable the ingestion through the Rust component.
pref("toolkit.contentRelevancy.ingestEnabled", false);
// Pref to enable extra logging for the content relevancy feature
pref("toolkit.contentRelevancy.log", false);
