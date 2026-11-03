/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Preferences file used by the raptor harness
/* globals user_pref */
// prevents normandy from running updates during the tests
user_pref("app.normandy.enabled", false);

// Disable the preloaded new-tab page and its background process.
user_pref("browser.newtab.preload", false);

// Disable Safe Browsing list updates.
user_pref("browser.safebrowsing.update.enabled", false);

// Disable captive-portal detection requests.
user_pref("network.captive-portal-service.enabled", false);

// Disable background network-connectivity probes.
user_pref("network.connectivity-service.enabled", false);

// Disable the profile backup service.
user_pref("browser.backup.enabled", false);

// Defer periodic session store saves for one hour.
user_pref("browser.sessionstore.interval", 3600000);
user_pref("browser.sessionstore.interval.idle", 3600000);

// Match the release delay before the user-inactive shrinking GC. Nightly
// uses 15s, which lets it run during the measurements since automation never
// sends user input.
user_pref("javascript.options.compact_on_user_inactive_delay", 300000);

// Avoid the status panel indicating loading state which messes up
// screenshots.
user_pref("browser.tabs.hideStatusPanel", true);

user_pref("dom.performance.time_to_non_blank_paint.enabled", true);
user_pref("dom.performance.time_to_contentful_paint.enabled", true);
user_pref("dom.performance.time_to_first_interactive.enabled", true);

// required for geckoview logging
user_pref("geckoview.console.enabled", true);

// get the console logging out of the webext into the stdout
user_pref("browser.dom.window.dump.enabled", true);
user_pref("devtools.console.stdout.chrome", true);
user_pref("devtools.console.stdout.content", true);

// prevent pages from opening after a crash
user_pref("browser.sessionstore.resume_from_crash", false);

// disable the background hang monitor
user_pref("toolkit.content-background-hang-monitor.disabled", true);

// disable async stacks to match release builds
// https://developer.mozilla.org/en-US/docs/Mozilla/Benchmarking#Async_Stacks
user_pref('javascript.options.asyncstack', false);

// disable Firefox Telemetry (and some other things too)
// https://bugzilla.mozilla.org/show_bug.cgi?id=1533879
user_pref('datareporting.healthreport.uploadEnabled', false);

// Telemetry initialization happens on a delay, that may elapse exactly in the
// middle of some raptor tests. While it doesn't do a lot of expensive work, it
// causes some I/O and thread creation, that can add noise to performance
// profiles we use to analyze performance regressions.
// https://bugzilla.mozilla.org/show_bug.cgi?id=1706180
user_pref('toolkit.telemetry.initDelay', 99999999);

// disable autoplay for raptor tests
user_pref('media.autoplay.default', 5);
user_pref('media.autoplay.blocking_policy', 1);
user_pref('media.allowed-to-play.enabled', false);
user_pref('media.block-autoplay-until-in-foreground', true);

// Turn off update
user_pref("app.update.disabledForTesting", true);

// Disable all recommended Remote Protocol preferences for Gecko tests.
// The prefs recommended by Remote Protocol are typically geared towards
// consumer automation; not vendor testing.
user_pref("remote.prefs.recommended", false);

// Enforce some preferences which would normally be set by Remote RecommendedPrefs
// and are still useful for raptor tests.

// Avoid unexpected network connections and downloads during the tests.
user_pref("browser.discovery.enabled", false);
user_pref("browser.ml.enable", false);
user_pref("browser.newtabpage.activity-stream.testing.shouldInitializeFeeds", false);
user_pref("browser.search.update", false);
user_pref("browser.translations.enable", false);
user_pref("browser.urlbar.merino.endpointURL", "");
user_pref("browser.urlbar.merino.ohttpConfigURL", "");
user_pref("browser.urlbar.merino.ohttpRelayURL", "");
user_pref("browser.urlbar.suggest.searches", false);
user_pref("datareporting.policy.dataSubmissionPolicyAccepted", false);
user_pref("datareporting.usage.uploadEnabled", false);
user_pref("extensions.getAddons.cache.enabled", false);
user_pref("extensions.systemAddon.update.enabled", false);
user_pref("geo.provider.network.url", "");
user_pref("network.connectivity-service.enabled", false);
user_pref("places.semanticHistory.featureGate", false);
user_pref("security.remote_settings.intermediates.enabled", false);

// Modify the behavior of the browser, mostly to avoid prompts, panels and
// background work which could interfere with the tests.
user_pref("apz.axis_lock.mode", 0);
user_pref("apz.content_response_timeout", 60000);
user_pref("browser.backup.enabled", false);
user_pref("browser.contentblocking.introCount", 99);
user_pref("browser.download.alwaysOpenPanel", false);
user_pref("browser.download.panel.shown", true);
user_pref("browser.http.blank_page_with_error_response.enabled", true);
user_pref("browser.tabs.remote.unloadDelayMs", 0);
user_pref("browser.tabs.warnOnClose", false);
user_pref("browser.tabs.warnOnCloseOtherTabs", false);
user_pref("browser.tabs.warnOnOpen", false);
user_pref("browser.toolbars.bookmarks.visibility", "never");
user_pref("browser.usedOnWindows10.introURL", "");
user_pref("dom.file.createInChild", true);
user_pref("dom.ipc.processPriorityManager.enabled", false);
user_pref("dom.navigation.navigationRateLimit.count", 0);
user_pref("dom.permissions.testing.enabled", true);
user_pref("dom.screenorientation.allow-lock", true);
user_pref("dom.successive_dialog_time_limit", 0);
user_pref("geo.prompt.open_system_prefs", false);
user_pref("mousewheel.allow_scrolling_more_than_one_page", true);
user_pref("network.http.prompt-temp-redirect", false);
user_pref("security.notification_enable_delay", 0);
user_pref("security.webauthn.related_origin_requests_mode", 1);
user_pref("services.settings.loglevel", "off");
user_pref("signon.autofillForms", false);
user_pref("signon.management.page.breach-alerts.enabled", false);
user_pref("signon.management.page.vulnerable-passwords.enabled", false);
user_pref("signon.rememberSignons", false);
user_pref("termsofuse.bypassNotification", true);
user_pref("threads.lower_mainthread_priority_in_background.enabled", false);
user_pref("widget.windows.window_occlusion_tracking.enabled", false);
