#filter dumbComments emptyLines substitution

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Non-static prefs that are specific to GeckoView belong in this file.
//
// Please indent all prefs defined within #ifdef/#ifndef conditions. This
// improves readability, particular for conditional blocks that exceed a single
// screen.

pref("accessibility.typeaheadfind", false);
pref("accessibility.typeaheadfind.flashBar", 1);
pref("accessibility.typeaheadfind.linksonly", false);
pref("accessibility.typeaheadfind.timeout", 5000);

pref("app.support.baseURL", "https://support.mozilla.org/1/mobile/%VERSION%/%OS%/%LOCALE%/");

// Prefs used by UpdateTimerManager (including blocklist pings) (bug 783909)
pref("app.update.timerFirstInterval", 30000); // milliseconds
pref("app.update.timerMinimumDelay", 30); // seconds

// Use a breakout angle of 30° (bug 1885506)
pref("apz.axis_lock.breakout_angle", "0.5235988");

// APZ content response timeout (bug 1247280)
pref("apz.content_response_timeout", 600);

// Disable scrollbar dragging on Android (bug 1339831)
pref("apz.drag.enabled", false);

// Tweak fling curving to make medium-length flings go a bit faster (bug 1243854)
pref("apz.fling_curve_function_x1", "0.59");
pref("apz.fling_curve_function_x2", "0.05");
pref("apz.fling_curve_function_y1", "0.46");
pref("apz.fling_curve_function_y2", "1.00");

// :gordonb from UX said this value makes fling curving
// feel a lot better (bug 1095727)
pref("apz.fling_curve_threshold_inches_per_ms", "0.01");

// Adjust fling physics based on UX feedback (bug 1229839)
pref("apz.fling_friction", "0.004");

// Use Android OverScroller class for fling animation (bug 1229462)
pref("apz.fling_stopped_threshold", "0.0");

// :gordonb from UX said this value makes fling curving
// feel a lot better (bug 1095727)
pref("apz.max_velocity_inches_per_ms", "0.07");

// Don't allow a faraway second tap to start a one-touch pinch gesture (bug 1391770)
pref("apz.second_tap_tolerance", "0.3");

// This value originates from bug 1174532
pref("apz.touch_move_tolerance", "0.03");

// Lower the touch-start tolerance threshold to reduce scroll lag with APZ (bug 1230077)
pref("apz.touch_start_tolerance", "0.06");

// The breakpad report server to link to in about:crashes
pref("breakpad.reportURL", "https://crash-stats.mozilla.org/report/index/");

// Prevent tooltips from showing up (bug 623063)
pref("browser.chrome.toolbar_tips", false);

// True if you always want dump() to work
//
// On Android, you also need to do the following for the output
// to show up in logcat:
//
// $ adb shell stop
// $ adb shell setprop log.redirect-stdio true
// $ adb shell start
pref("browser.dom.window.dump.enabled", true);

// Default to ~/Downloads (bug 437954)
pref("browser.download.folderList", 1);

// Use Android DownloadManager for scanning downloads (bug 816318)
pref("browser.download.manager.addToRecentDocs", true);

// Load PDF files inline with PDF.js (bug 1754499)
pref("browser.download.open_pdf_attachments_inline", true);

pref("browser.download.useDownloadDir", true);

// When enabled, Services.uriFixup.isDomainKnown('localhost') will return true
pref("browser.fixup.domainwhitelist.localhost", true);

// Open in tab preferences
pref("browser.link.open_newwindow", 3);

// Supported values:
//  - 0: Force all new windows to tabs
//  - 1: Don't force
//  - 2: Only force those with no features set
pref("browser.link.open_newwindow.restriction", 0);

// Don't allow meta-refresh when backgrounded (bug 518805)
pref("browser.meta_refresh_when_inactive.disabled", true);

// The download protection UI is not implemented yet (bug 1239094).
pref("browser.safebrowsing.downloads.enabled", false);

pref("browser.safebrowsing.features.harmfuladdon.update", true);
pref("browser.safebrowsing.features.cryptomining.update", true);
pref("browser.safebrowsing.features.fingerprinting.update", true);
pref("browser.safebrowsing.features.malware.update", true);
pref("browser.safebrowsing.features.phishing.update", true);
pref("browser.safebrowsing.features.trackingAnnotation.update", true);
pref("browser.safebrowsing.features.trackingProtection.update", true);

// Disable search suggestions by default (bug 784104)
pref("browser.search.suggest.enabled", false);

// Disable search engine updating (bug 431842)
pref("browser.search.update", false);

// Session history
pref("browser.sessionhistory.contentViewerTimeout", 360);
pref("browser.sessionhistory.max_entries", 50);

// Session store
pref("browser.sessionstore.interval", 10000); // milliseconds
pref("browser.sessionstore.max_resumed_crashes", 2);
pref("browser.sessionstore.max_tabs_undo", 10);
// Supported values:
//  - 0: all
//  - 1: unencrypted sites
//  - 2: never
pref("browser.sessionstore.privacy_level", 0);
pref("browser.sessionstore.resume_from_crash", true);

// Enables the translations engine
pref("browser.translations.enable", true);
// Used for mocking data for GeckoView Translations tests, should use in addition with an automation check.
pref("browser.translations.geckoview.enableAllTestMocks", false);

// Used for mocking data for GeckoView WebCompat tests, should use in addition with an automation check.
pref("browser.webcompat.geckoview.enableAllTestMocks", false);

// SSL error page behaviour (bug 437372)
pref("browser.xul.error_pages.expert_bad_cert", false);

// Enable sparse localization by setting a few package locale overrides (bug 792077)
pref("chrome.override_package.global", "browser");
pref("chrome.override_package.mozapps", "browser");
pref("chrome.override_package.passwordmgr", "browser");

// Allow Console API to log messages on stdout (bug 1480544)
pref("devtools.console.stdout.chrome", true);

// Enable capture attribute for file input (bug 1553603)
pref("dom.capture.enabled", true);

// Block popups by default (bug 436057)
pref("dom.disable_open_during_load", true);

// Don't allow JS to move and resize existing windows (bug 456081)
pref("dom.disable_window_move_resize", true);

// "graceful" process termination is misinterpreted as a process crash.
// To avoid this issue, we set dom.ipc.keepProcessesAlive.extension to 1.
// This stops Gecko from terminating the extension process. This also reduces
// the overhead of resuming a suspended (background) extension page.
// Note that this only covers "graceful" termination by Gecko.
// Android-triggered force-kills and OOM are not prevented and should still
// be accounted for. See bug 1847608 for more info
pref("dom.ipc.keepProcessesAlive.extension", 1);

// Keep empty content process alive on Android (bug 1447393)
pref("dom.ipc.keepProcessesAlive.web", 1);

// Enable the preallocated process on Android
pref("dom.ipc.processPrelaunch.enabled", true);

// Increase script timeouts (bug 485610)
pref("dom.max_script_run_time", 20);

// Enable meta-viewport support for font inflation code (bug 1106255)
pref("dom.meta-viewport.enabled", true);

// The maximum number of recent message IDs to store for each push
// subscription, to avoid duplicates for unacknowledged messages (bug 1207743)
pref("dom.push.maxRecentMessageIDsPerSubscription", 0);

// Enable WebShare support (bug 1402369)
pref("dom.webshare.enabled", true);

// Disable add-ons that are not installed by the user in all scopes by default (See the SCOPE
// constants in AddonManager.sys.mjs for values to use here, and Bug 1405528 for a rationale)
pref("extensions.autoDisableScopes", 15);

pref("extensions.enabledScopes", 5);

// If true, unprivileged extensions may use experimental APIs
pref("extensions.experiments.enabled", false);

// Support credit cards in GV autocomplete API (bug 1691819)
pref("extensions.formautofill.addresses.capture.enabled", true);

pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/android/collections/4757633/mob/?page=1&collection_sort=-popularity");
pref("extensions.getAddons.cache.enabled", true);
pref("extensions.getAddons.get.url", "https://services.addons.mozilla.org/api/v4/addons/search/?guid=%IDS%&lang=%LOCALE%");
pref("extensions.getAddons.langpacks.url", "https://services.addons.mozilla.org/api/v4/addons/language-tools/?app=android&type=language&appversion=%VERSION%");
pref("extensions.getAddons.search.browseURL", "https://addons.mozilla.org/%LOCALE%/android/search?q=%TERMS%&platform=%OS%&appver=%VERSION%");

// Don't let XPIProvider install distribution add-ons; we do our own thing on mobile
pref("extensions.installDistroAddons", false);

// Require language packs to be signed (bug 1454141)
pref("extensions.langpacks.signatures.required", true);

// Enables some extra Extension System Logging (can reduce performance)
pref("extensions.logging.enabled", false);

// Whether MV3 restrictions for actions popup urls should be extended to MV2 extensions
// (only allowing same extension urls to be used as action popup urls)
pref("extensions.manifestV2.actionsPopupURLRestricted", true);

// Disables strict compatibility, making addons compatible-by-default
pref("extensions.strictCompatibility", false);

// Allow system add-on updates (bug 1260213)
pref("extensions.systemAddon.update.enabled", true);
pref("extensions.systemAddon.update.url", "https://aus5.mozilla.org/update/3/SystemAddons/%VERSION%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/update.xml");

pref("extensions.update.background.url", "https://versioncheck-bg.addons.mozilla.org/update/VersionCheck.php?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");
pref("extensions.update.enabled", true);
pref("extensions.update.interval", 86400);
pref("extensions.update.url", "https://versioncheck.addons.mozilla.org/update/VersionCheck.php?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");

// Register the type of the background-video-playback intervention's kill-switch
// pref (bug 2060611) so Nimbus can toggle it. Defaults to disabled; the
// intervention is turned on for Nightly via a Nimbus rollout.
pref("extensions.webcompat.disabled_interventions.2060611", true);

// Enable prompts for browser.permissions.request() (bug 1392176)
pref("extensions.webextOptionalPermissionPrompts", true);

// Start (proxy) extensions as soon as a network request is observed, instead
// of waiting until the first browser window has opened. This is needed because
// GeckoView can trigger requests without opening geckoview.xhtml.
pref("extensions.webextensions.early_background_wakeup_on_request", true);

// Enable data collection permissions.
pref("extensions.dataCollectionPermissions.enabled", true);

// Scroll and zoom into editable form fields (bug 834613)
pref("formhelper.autozoom", true);

// Optionally send web console output to logcat (bug 1415318)
pref("geckoview.console.enabled", false);

// How long (ms) to keep an autocomplete selection prompt open after its field
// loses focus, so that moving focus between fields of the same form doesn't
// dismiss and immediately reopen the prompt. Tunable for slower devices.
// (bug 2040184)
pref("geckoview.autocomplete.selection_dismiss_delay_ms", 150);

// Inherit locale from the OS, used for multi-locale builds
pref("intl.locale.requested", "");

// Always tilt the caret to match the text selection guideline (bug 1097398)
pref("layout.accessiblecaret.always_tilt", true);

// Show the caret when long tapping on empty content (bug 1246064)
pref("layout.accessiblecaret.caret_shown_when_long_tapping_on_empty_content", true);

// Initial text selection on long-press is enhanced to provide
// a smarter phone-number selection for direct-dial ActionBar action (bug 1235508)
pref("layout.accessiblecaret.extend_selection_for_phone_number", true);

// Provide haptic feedback on longPress selection events (bug 1230613)
pref("layout.accessiblecaret.hapticfeedback", true);

// AccessibleCaret css for Android L style assets (bug 1097398)
pref("layout.accessiblecaret.height", "22.0");
pref("layout.accessiblecaret.margin-left", "-11.5");
pref("layout.accessiblecaret.width", "22.0");

// Update any visible carets for selection changes due to JS calls,
// but don't show carets if carets are hidden (bug 1463576)
pref("layout.accessiblecaret.script_change_update_mode", 1);

// Disable CSS error reporting by default to improve performance (bug 831123)
pref("layout.css.report_errors", false);

pref("layout.spellcheckDefault", 0);

// Enable EME permission prompts (bug 1620102)
pref("media.eme.require-app-approval", true);

// Enable autoplay permission prompts (bug 1577596)
pref("media.geckoview.autoplay.request", true);

// Enable Widevine MediaKeySystem (bug 1306219)
pref("media.mediadrm-widevinecdm.visible", true);

// Ask for permission when enumerating WebRTC devices (bug 1369108)
pref("media.navigator.permission.device", true);

// On mobile we throttle the download once the readahead_limit is hit
// if we're using a cellular connection, even if the download is slow,
// this is to preserve battery and data (bug 1540573)
pref("media.throttle-cellular-regardless-of-download-rate", true);

// Increase necko buffer sizes for Android (bug 560591)
pref("network.buffer.cache.size",  16384);

// Set HPACK receive buffer size appropriately for Android (bug 1296280)
pref("network.http.http2.default-hpack-buffer", 4096);

// HTTP/2 Server Push (bug 790388)
pref("network.http.http2.push-allowance", 32768);

// Reduce HTTP Idle connection timeout on Android to improve battery life (bug 1007959)
pref("network.http.keep-alive.timeout", 109);

// Update connection limits; especially for proxies (bug 648603)
pref("network.http.max-persistent-connections-per-proxy", 20);

// Disable warning for mailto and tel protocols (bug 589403)
pref("network.protocol-handler.warn-external.mailto", false);
pref("network.protocol-handler.warn-external.tel", false);

// Disable warning for sms protocol (bug 819554)
pref("network.protocol-handler.warn-external.sms", false);

// Do not warn when opening YouTube (bug 630364)
pref("network.protocol-handler.warn-external.vnd.youtube", false);

// Transmit UDP busy-work to the LAN when anticipating low latency
// network reads and on wifi to mitigate 802.11 Power Save Polling delays
// (bug 888268)
pref("network.tickle-wifi.enabled", true);

// Controls OHTTP config URL
pref("network.ohttp.configURL", "https://prod.ohttp-gateway.prod.webservices.mozgcp.net/ohttp-configs");

// Controls OHTTP relay URL
pref("network.ohttp.relayURL", "https://mozilla-ohttp.fastly-edge.com/");

// Complete the page load progress bar at different places according to this pref.
// See the possible values below:
// 0 no change
// 1 complete progressbar at DOMContentLoaded
// 2 complete progressbar at first MozAfterPaint after DOMContentLoaded
pref("page_load.progressbar_completion", 2);

// Try to convert PDFs sent as octet-stream (bug 1754499)
pref("pdfjs.handleOctetStream", true);

// Disable tracking protection in PBM for GeckoView (bug 1436887)
pref("privacy.trackingprotection.pbmode.enabled", false);

pref("signon.showAutoCompleteFooter", true);

// Locked because any other value would break GeckoView
pref("toolkit.defaultChromeURI", "chrome://geckoview/content/geckoview.xhtml", locked);

// Whether to use unified telemetry behavior; requires a restart to take effect
pref("toolkit.telemetry.unified", false);

// Download protection lists are not available on Android (bug 1397938, bug 1394017)
pref("urlclassifier.downloadAllowTable", "");
pref("urlclassifier.downloadBlockTable", "");

// Delay the CRC32 check for URLClassifier to improve applink performance. (bug 1956920, bug 1971949)
pref("urlclassifier.delay_prefixes_crc32_check", true);

// The Potentially Harmful Apps list replaces the malware one on Android (bug 1394017)
pref("urlclassifier.malwareTable", "goog-harmful-proto,goog-unwanted-proto,moztest-harmful-simple,moztest-malware-simple,moztest-unwanted-simple");

// Android doesn't support the new sync storage yet (bug 1625257)
pref("webextensions.storage.sync.kinto", true);

// Require extensions to be signed (bug 1244329)
pref("xpinstall.signatures.required", true);

pref("xpinstall.whitelist.add", "https://addons.mozilla.org");
pref("xpinstall.whitelist.fileRequest", false);

// Pref to enable the IP protection feature
pref("browser.ipProtection.enabled", true);
pref("browser.ipProtection.guardian.endpoint", "https://vpn.mozilla.org/");
pref("toolkit.ipProtection.android.authProvider", "fxa");
