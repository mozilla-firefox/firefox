// Preference file for the reftest harness.
/* globals user_pref */
user_pref("app.update.staging.enabled", false);
user_pref("browser.safebrowsing.blockedURIs.enabled", false);
user_pref("browser.safebrowsing.downloads.enabled", false);
user_pref("browser.safebrowsing.downloads.remote.url", "http://127.0.0.1/safebrowsing-dummy/gethash");
user_pref("browser.safebrowsing.malware.enabled", false);
// Likewise for safebrowsing.
user_pref("browser.safebrowsing.phishing.enabled", false);
user_pref("browser.safebrowsing.provider.google.gethashURL", "http://127.0.0.1/safebrowsing-dummyg/gethash");
user_pref("browser.safebrowsing.provider.google.updateURL", "http://127.0.0.1/safebrowsing-dummyg/update");
user_pref("browser.safebrowsing.provider.google4.gethashURL", "http://127.0.0.1/safebrowsing-dummyg4/gethash");
user_pref("browser.safebrowsing.provider.google4.updateURL", "http://127.0.0.1/safebrowsing-dummyg4/update");
user_pref("browser.safebrowsing.provider.google5.gethashURL", "http://127.0.0.1/safebrowsing-dummyg5/gethash");
user_pref("browser.safebrowsing.provider.google5.updateURL", "http://127.0.0.1/safebrowsing-dummyg5/update");
user_pref("browser.safebrowsing.provider.mozilla.gethashURL", "http://127.0.0.1/safebrowsing-dummym/gethash");
user_pref("browser.safebrowsing.provider.mozilla.updateURL", "http://127.0.0.1/safebrowsing-dummym/update");
// use about:blank, not browser.startup.homepage
user_pref("browser.startup.page", 0);
// Since our tests are 800px wide, set the assume-designed-for width of all
// pages to be 800px (instead of the default of 980px). This ensures that
// in our 800px window we don't zoom out by default to try to fit the
// assumed 980px content.
user_pref("browser.viewport.desktopWidth", 800);
user_pref("datareporting.healthreport.uploadEnabled", false);
// Don't forcibly kill content processes after a timeout
user_pref("dom.ipc.tabs.shutdownTimeoutSecs", 0);
// Allow loading chrome://reftest/* documents in content processes.
user_pref("dom.ipc.validatePrincipal.testing.allowContentSystemDocuments", true);
// For mochitests, we're more interested in testing the behavior of in-
// content XBL bindings, so we set this pref to true. In reftests, we're
// more interested in testing the behavior of XBL as it works in chrome,
// so we want this pref to be false.
user_pref("dom.use_xbl_scopes_for_remote_xul", false);
user_pref("extensions.autoDisableScopes", 0);
// Disable blocklist updates so we don't have them reported as leaks
user_pref("extensions.blocklist.enabled", false);
user_pref("extensions.getAddons.cache.enabled", false);
user_pref("extensions.getAddons.get.url", "http://localhost/extensions-dummy/repositoryGetURL");
user_pref("extensions.systemAddon.update.url", "http://localhost/dummy-system-addons.xml");
user_pref("gfx.color_management.force_srgb", true);
user_pref("gfx.color_management.mode", 2);
user_pref("gfx.logging.level", 1);
// Ignoring the blocklist is necessary to run tests in CI on old Mesa versions
// (https://bugzilla.mozilla.org/show_bug.cgi?id=1985348).
user_pref("gfx.webgpu.ignore-blocklist", true);
// Disable downscale-during-decode, since it makes reftests more difficult.
user_pref("image.downscale-during-decode.enabled", false);
// We do the capturing the reftest results with sync decoding
// so we want to be consistent.
user_pref("image.testing.decode-sync.enabled", true);
// Disable interruptible reflow since (1) it's normally not going to
// happen, but (2) it might happen if we somehow end up with both
// pending user events and clock skew.  So to avoid having to change
// MakeProgress to deal with waiting for interruptible reflows to
// complete for a rare edge case, we just disable interruptible
// reflow so that that rare edge case doesn't lead to reftest
// failures.
user_pref("layout.interruptible-reflow.enabled", false);
// Disable the fade of overlay scrollbars, since we can't guarantee taking both
// reftest snapshots at the same point during the fade.
user_pref("layout.testing.overlay-scrollbars.always-visible", true);
user_pref("ui.scrollbarFadeDuration", 0);
// Disable dark and themed scrollbars because they might be
// semi-transparent.
user_pref("widget.gtk.theme-scrollbar-colors.enabled", false);
user_pref("widget.disable-dark-scrollbar", true);
// Disable antialiasing of ahem font.
user_pref("gfx.font_rendering.ahem_antialias_none", true);
// Run the "deferred" font-loader immediately, because if it finishes
// mid-test, the extra reflow that is triggered can disrupt the test.
user_pref("gfx.font_loader.delay", 0);
// Ensure bundled fonts are activated, even if not enabled by default
// on the platform, so that tests can rely on them.
user_pref("gfx.bundled-fonts.activate", 1);
// The broken image icon doesn't block the load event and thus there's no easy
// way to guarantee it's loaded by the time we take the reftest screenshot.
user_pref("layout.image.eager_broken_image_icon", true);
user_pref("media.gmp-manager.url.override", "http://localhost/dummy-gmp-manager.xml");
// Reftests load a lot of URLs very quickly. This puts avoidable and
// unnecessary I/O pressure on the Places DB (measured to be in the
// gigabytes).
user_pref("places.history.enabled", false);
user_pref("privacy.trackingprotection.annotate_channels", false);
user_pref("privacy.trackingprotection.enabled", false);
user_pref("privacy.trackingprotection.pbmode.enabled", false);
// Disable all recommended Remote Protocol preferences for Gecko tests.
// The prefs recommended by Remote Protocol are typically geared towards
// consumer automation; not vendor testing.
user_pref("remote.prefs.recommended", false);
// Checking whether two files are the same is slow on Windows.
// Setting this pref makes tests run much faster there. Reftests also
// rely on this to load downloadable fonts (which are restricted to same
// origin policy by default) from outside their directory.
user_pref("security.fileuri.strict_origin_policy", false);
user_pref("startup.homepage_override_url", "");
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
// A fake bool pref for "@supports -moz-bool-pref" sanify test.
user_pref("testing.supports.moz-bool-pref", false);
// Ensure that telemetry is disabled, so we don't connect to the telemetry
// server in the middle of the tests.
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.server", "https://%(server)s/telemetry-dummy/");
// Default Glean to "record but don't report" mode, and to never trigger
// activity-based ping submission. Docs:
// https://firefox-source-docs.mozilla.org/toolkit/components/glean/dev/preferences.html
user_pref("telemetry.fog.test.localhost_port", -1);
user_pref("telemetry.fog.test.activity_limit", -1);
user_pref("telemetry.fog.test.inactivity_limit", -1);
// Slow down early "events" pings to make testing easier.
user_pref("telemetry.fog.test.decelerate_early_events", true);
user_pref("ui.caretBlinkTime", -1);
user_pref("ui.caretWidth", 1);
user_pref("ui.prefersReducedMotion", 0);
user_pref("ui.systemUsesDarkTheme", 0);
user_pref("ui.useAccessibilityTheme", 0);
// Turn off the Push service.
user_pref("dom.push.serverURL", "");
// Disable intermittent telemetry collection
user_pref("toolkit.telemetry.initDelay", 99999999);
// Setting this pref to true for usercss reftests, since it relies on userContent.css
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
// Use a light color-scheme unless explicitly overriden.
user_pref("layout.css.prefers-color-scheme.content-override", 1);
// Turn off update
user_pref("app.update.disabledForTesting", true);
// Disable pixel alignment
user_pref("layout.disable-pixel-alignment", true);
