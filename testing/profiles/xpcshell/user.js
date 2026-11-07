/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Base preferences file used by the xpcshell harness
/* globals user_pref */
/* eslint quotes: 0 */
user_pref("app.normandy.api_url", "https://%(server)s/selfsupport-dummy/");
user_pref("browser.safebrowsing.downloads.remote.url", "https://%(server)s/safebrowsing-dummy");
user_pref("extensions.systemAddon.update.url", "http://%(server)s/dummy-system-addons.xml");
// Treat WebExtension API/schema warnings as errors.
user_pref("extensions.webextensions.warnings-as-errors", true);
// Always use network provider for geolocation tests
// so we bypass the OSX dialog raised by the corelocation provider
user_pref("geo.provider.testing", true);
user_pref("browser.region.network.url", "");
user_pref("media.gmp-manager.updateEnabled", false);
user_pref("media.gmp-manager.url.override", "http://%(server)s/dummy-gmp-manager.xml");
user_pref("toolkit.telemetry.server", "https://%(server)s/telemetry-dummy");
// Default Glean to "record but don't report" mode, and to never trigger
// activity-based ping submission. Docs:
// https://firefox-source-docs.mozilla.org/toolkit/components/glean/dev/preferences.html
user_pref("telemetry.fog.test.localhost_port", -1);
user_pref("telemetry.fog.test.activity_limit", -1);
user_pref("telemetry.fog.test.inactivity_limit", -1);
// Slow down early "events" pings to make testing easier.
user_pref("telemetry.fog.test.decelerate_early_events", true);
// Prevent Remote Settings to issue non local connections.
user_pref("services.settings.server", "data:,#remote-settings-dummy/v1");
// Prevent intermediate preloads to be downloaded on Remote Settings polling.
user_pref("security.remote_settings.intermediates.enabled", false);
// The process priority manager only shifts priorities when it has at least
// one active tab. xpcshell tabs don't have any active tabs, which would mean
// all processes would run at low priority, which is not desirable, so we
// disable the process priority manager entirely here.
user_pref("dom.ipc.processPriorityManager.enabled", false);
// xpcshell tests composite nothing, so a GPU process is only overhead, and on
// macOS it can hang at shutdown.
user_pref("layers.gpu-process.enabled", false);
// Bug 455077 - Ensure we use sRGB as the output profile for test consistency.
user_pref("gfx.color_management.force_srgb", true);
user_pref("gfx.color_management.mode", 1);
// Don't enable remote tiles on new-tab pages in xpcshell
user_pref("browser.topsites.contile.enabled", false);
// Don't pull weather data from the network
user_pref("browser.newtabpage.activity-stream.system.showWeather", false);
// Don't render or load any New Tab widget
user_pref("browser.newtabpage.activity-stream.widgets.enabled", false);
// Widgets that load from the network stay off even when a test turns widgets
// back on
user_pref("browser.newtabpage.activity-stream.widgets.system.pictureOfTheDay.enabled", false);
user_pref("browser.newtabpage.activity-stream.widgets.system.crossword.enabled", false);
// Don't pull wallpaper content from the network
user_pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", false);
// Don't pull sponsored Top Sites content from the network
user_pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false);
user_pref("security.turn_off_all_security_so_that_viruses_can_take_over_this_computer", true);
user_pref("preferences.force-disable.check.once.policy", true);
// Turn off update
user_pref("app.update.disabledForTesting", true);
// Better stacks for errors.
user_pref("javascript.options.asyncstack_capture_debuggee_only", false);

// Turn off semantic history search as it triggers network connections to
// download ML models.
user_pref("places.semanticHistory.featureGate", false);

// Disable Nimbus persistence in all xpcshell tests. Persistence requires the
// ProfilesDatastoreService to be enabled, which is not initialized by default
// in xpcshell tests.
user_pref("nimbus.persistence.enabled", false);
