/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Base preferences file for web-platform-tests.
/* globals user_pref */
// Don't use the new tab page but about:blank for opened tabs
user_pref("browser.newtabpage.enabled", false);
// Don't restore the last open set of tabs if the browser has crashed
user_pref("browser.sessionstore.resume_from_crash", false);
// Don't show the Bookmarks Toolbar on any tab (the above pref that
// disables the New Tab Page ends up showing the toolbar on about:blank).
user_pref("browser.toolbars.bookmarks.visibility", "never");
// Expose TestUtils interface
user_pref("dom.testing.testutils.enabled", true);
// Only install add-ons from the profile and the application scope
// Also ensure that those are not getting disabled.
// see: https://developer.mozilla.org/en/Installing_extensions
user_pref("extensions.autoDisableScopes", 10);
// Don't open a dialog to show available add-on updates
user_pref("extensions.update.notifyUser", false);
// For cross-browser tests, don't fail on manifest warnings like unknown keys.
user_pref("extensions.webextensions.warnings-as-errors", false);
// Adjust behavior of browser.test API to be compatible across engines.
user_pref("extensions.wpt.enabled", true);
// Enable test mode to run multiple tests in parallel
user_pref("focusmanager.testmode", true);
// Enable fake media streams for getUserMedia
user_pref("media.navigator.streams.fake", true);
// Disable permission prompt for getUserMedia
user_pref("media.navigator.permission.disabled", true);
// Disable the use of mDNS candidates, since mDNS is flaky on wpt testers
user_pref("media.peerconnection.ice.obfuscate_host_addresses", false);
// Allow scroll amount larger than one page on a single mouse wheel
// event.
user_pref("mousewheel.allow_scrolling_more_than_one_page", true);
// Enable direct connection
user_pref("network.proxy.type", 0);
// Web-platform-tests load a lot of URLs very quickly. This puts avoidable and
// unnecessary I/O pressure on the Places DB (measured to be in the
// gigabytes).
user_pref("places.history.enabled", false);
// Suppress automatic safe mode after crashes
user_pref("toolkit.startup.max_resumed_crashes", -1);
// Run the font loader task eagerly for more predictable behavior
user_pref("gfx.font_loader.delay", 0);
// Disable antialiasing for the Ahem font.
user_pref("gfx.font_rendering.ahem_antialias_none", true);
// Disable safebrowsing components
user_pref("browser.safebrowsing.blockedURIs.enabled", false);
user_pref("browser.safebrowsing.downloads.enabled", false);
user_pref("browser.safebrowsing.malware.enabled", false);
user_pref("browser.safebrowsing.phishing.enabled", false);
user_pref("browser.safebrowsing.update.enabled", false);
// Disable high DPI
user_pref("layout.css.devPixelsPerPx", "1.0");
// Enable the parallel styling code.
user_pref("layout.css.stylo-threads", 4);
// sometime wpt runs test even before the document becomes visible, which would
// delay video.play() and cause play() running in wrong order.
user_pref("media.block-autoplay-until-in-foreground", false);
// Disable dark scrollbars as it can be semi-transparent that many reftests
// don't expect.
user_pref("widget.disable-dark-scrollbar", true);
// The Ubuntu Yaru theme has semi-transparent scrollbar track, which some tests
// assume doesn't exist.
user_pref("widget.gtk.theme-scrollbar-colors.enabled", false);
// Disable scrollbar animations. Otherwise reftests that use overlay scrollbars
// (only Android right now), might get a snapshot at different times during the
// animation.
user_pref("ui.scrollbarFadeDuration", 0);
user_pref("media.block-autoplay-until-in-foreground", false);
// Force a light color scheme unless explicitly overridden by pref.
user_pref("layout.css.prefers-color-scheme.content-override", 1);
// A lot of tests use the Reporting API for observing things
user_pref("dom.reporting.enabled", true);
// Enable WebDriver BiDi experimental commands and events during tests.
user_pref("remote.experimental.enabled", true);
// Dismiss file pickers for WebDriver BiDi sessions.
user_pref("remote.bidi.dismiss_file_pickers.enabled", true);
// Disable OCSP checks in WPT (webtransport triggers these occasionally)
user_pref("security.OCSP.enabled", 0);
// Disable download of intermediate certificates.
user_pref("security.remote_settings.intermediates.enabled", false);
// Disable prefers-reduced-motion to ensure that smooth scrolls can be tested.
user_pref("general.smoothScroll", true);
// Prevent default handlers being added, since these can cause network fetches
user_pref("gecko.handlerService.defaultHandlersVersion", 100);
// Enable virtual WebAuthn authenticators.
user_pref("security.webauth.webauthn_enable_softtoken", true);
// Disable hardware WebAuthn authenticators.
user_pref("security.webauth.webauthn_enable_usbtoken", false);
// Disable the WebAuthn direct attestation consent prompt.
user_pref("security.webauthn.always_allow_direct_attestation", true);
// Enable WebAuthn conditional mediation.
user_pref("security.webauthn.enable_conditional_mediation", true);
// Disable WebAuthn Related Origin Requests to prevent .well-known fetches to real hosts.
user_pref("security.webauthn.related_origin_requests_mode", 0);
// Disable captive portal service
user_pref("network.captive-portal-service.enabled", false);
// Enable http2 websockets support
user_pref("network.http.http2.websockets", true);
// Turn off update
user_pref("app.update.disabledForTesting", true);
// Use dummy server for geolocation
user_pref("geo.provider.network.url", "https://web-platform.test:8444/_mozilla/geolocation-API/dummy.py");
// If we are on a platform where we can detect that we don't have OS
// geolocation permission, and we can open it and wait for the user to give
// permission, then don't do that.
user_pref("geo.prompt.open_system_prefs", false);

// prefs to force font, specifically from ubuntu 18.04
user_pref("font.name.serif.x-western", "DejaVu Serif");
user_pref("font.name.sans-serif.x-western", "DejaVu Sans");
user_pref("font.name.monospace.x-western", "DejaVu Sans Mono");
user_pref("font.name.serif.x-unicode", "DejaVu Serif");
user_pref("font.name.sans-serif.x-unicode", "DejaVu Sans");
user_pref("font.name.monospace.x-unicode", "DejaVu Sans Mono");
