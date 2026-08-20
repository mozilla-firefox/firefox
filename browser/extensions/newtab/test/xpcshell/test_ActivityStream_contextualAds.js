/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  useContextualAds: "resource://newtab/lib/ActivityStream.sys.mjs",
});

const REGION_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.contextualAds.region-config";
const LOCALE_CONFIG =
  "browser.newtabpage.activity-stream.discoverystream.sections.contextualAds.locale-config";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(REGION_CONFIG);
  Services.prefs.clearUserPref(LOCALE_CONFIG);
});

add_task(function test_shipped_defaults_enable_the_rollout() {
  Services.prefs.clearUserPref(REGION_CONFIG);
  Services.prefs.clearUserPref(LOCALE_CONFIG);

  Assert.ok(
    useContextualAds({ geo: "US", locale: "en-US" }),
    "Enabled for US/en-US from the pref defaults shipped in firefox.js"
  );
  Assert.ok(
    !useContextualAds({ geo: "XX", locale: "en-US" }),
    "Disabled when the region is not in the configured list"
  );
  Assert.ok(
    !useContextualAds({ geo: "US", locale: "zz-ZZ" }),
    "Disabled when the locale is not in the configured list"
  );
});

add_task(function test_empty_config_disables() {
  Services.prefs.setStringPref(REGION_CONFIG, "");
  Services.prefs.setStringPref(LOCALE_CONFIG, "");

  Assert.ok(
    !useContextualAds({ geo: "US", locale: "en-US" }),
    "An empty region/locale config disables contextual ads"
  );

  Services.prefs.setStringPref(REGION_CONFIG, "US");
  Services.prefs.setStringPref(LOCALE_CONFIG, "");
  Assert.ok(
    !useContextualAds({ geo: "US", locale: "en-US" }),
    "An empty locale config alone is enough to disable contextual ads"
  );

  Services.prefs.setStringPref(REGION_CONFIG, "");
  Services.prefs.setStringPref(LOCALE_CONFIG, "en-US");
  Assert.ok(
    !useContextualAds({ geo: "US", locale: "en-US" }),
    "An empty region config alone is enough to disable contextual ads"
  );
});

// A non-empty config must override the shipped default so Nimbus can still
// configure which regions/locales receive contextual ads.
add_task(function test_explicit_config_overrides_default() {
  Services.prefs.setStringPref(REGION_CONFIG, "CA");
  Services.prefs.setStringPref(LOCALE_CONFIG, "en-CA");

  Assert.ok(
    !useContextualAds({ geo: "US", locale: "en-US" }),
    "Disabled for US once the config is narrowed to CA"
  );
  Assert.ok(
    useContextualAds({ geo: "CA", locale: "en-CA" }),
    "Enabled for the explicitly configured CA/en-CA"
  );
});
