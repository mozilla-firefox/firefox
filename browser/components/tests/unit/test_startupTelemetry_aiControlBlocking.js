/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { StartupTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/StartupTelemetry.sys.mjs"
);

const GLOBAL_AI_PREF = "browser.ai.control.default";
const AI_CONTROL_PREFS = {
  [GLOBAL_AI_PREF]: "default",
  "browser.ai.control.translations": "translations",
  "browser.ai.control.pdfjsAltText": "pdfjsAltText",
  "browser.ai.control.smartTabGroups": "smartTabGroups",
  "browser.ai.control.linkPreviewKeyPoints": "linkPreviewKeyPoints",
  "browser.ai.control.sidebarChatbot": "sidebarChatbot",
  "browser.ai.control.smartWindow": "smartWindow",
  "browser.ai.control.speechRecognition": "speechRecognition",
  "browser.ai.control.generatedWidgets": "generatedWidgets",
};

function resetPrefs(changes = {}) {
  for (let [pref, name] of Object.entries(AI_CONTROL_PREFS)) {
    if (name in changes) {
      Services.prefs.setStringPref(pref, changes[name]);
    } else {
      Services.prefs.clearUserPref(pref);
    }
  }
}

function assertFeatureMetrics(expected) {
  for (let [, key] of Object.entries(AI_CONTROL_PREFS)) {
    Assert.equal(
      Glean.browser.aiControlIsBlocking[key].testGetValue(),
      expected[key],
      `${key} should be ${expected[key]}`
    );
  }
}

add_setup(function test_setup() {
  do_get_profile();
  Services.fog.initializeFOG();
  resetPrefs();

  let cleanupListeners = StartupTelemetry.aiControlBlocking();
  registerCleanupFunction(() => {
    cleanupListeners();
    resetPrefs();
  });
});

add_task(function test_defaults_no_prefs_set() {
  // Don't reset FOG/prefs since the setup grabbed the initial values.

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    false,
    "Global should be false when no prefs set"
  );
  assertFeatureMetrics({
    translations: false,
    pdfjsAltText: false,
    smartTabGroups: false,
    linkPreviewKeyPoints: false,
    sidebarChatbot: false,
    smartWindow: false,
    speechRecognition: false,
    generatedWidgets: false,
  });
});

add_task(function test_global_blocked_features_default() {
  Services.fog.testResetFOG();
  resetPrefs({
    default: "blocked",
  });

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    true,
    "Global should be true when blocked"
  );
  assertFeatureMetrics({
    translations: true,
    pdfjsAltText: true,
    smartTabGroups: true,
    linkPreviewKeyPoints: true,
    sidebarChatbot: true,
    smartWindow: true,
    speechRecognition: true,
    generatedWidgets: true,
  });
});

add_task(function test_global_available_one_feature_blocked() {
  Services.fog.testResetFOG();
  resetPrefs({
    default: "available",
    translations: "blocked",
  });

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    false,
    "Global should be false when available"
  );
  assertFeatureMetrics({
    translations: true,
    pdfjsAltText: false,
    smartTabGroups: false,
    linkPreviewKeyPoints: false,
    sidebarChatbot: false,
    smartWindow: false,
    speechRecognition: false,
    generatedWidgets: false,
  });
});

add_task(function test_global_blocked_one_feature_enabled() {
  Services.fog.testResetFOG();
  resetPrefs({
    default: "blocked",
    smartTabGroups: "enabled",
  });

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    true,
    "Global should be true when blocked"
  );
  assertFeatureMetrics({
    translations: true,
    pdfjsAltText: true,
    smartTabGroups: false,
    linkPreviewKeyPoints: true,
    sidebarChatbot: true,
    smartWindow: true,
    speechRecognition: true,
    generatedWidgets: true,
  });
});

add_task(function test_pref_observer_global_change() {
  Services.fog.testResetFOG();
  resetPrefs({
    default: "available",
  });

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    false,
    "Global should be false initially"
  );
  assertFeatureMetrics({
    translations: false,
    pdfjsAltText: false,
    smartTabGroups: false,
    linkPreviewKeyPoints: false,
    sidebarChatbot: false,
    smartWindow: false,
    speechRecognition: false,
    generatedWidgets: false,
  });

  Services.prefs.setStringPref(GLOBAL_AI_PREF, "blocked");

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    true,
    "Global should update to true after pref change"
  );
  assertFeatureMetrics({
    translations: true,
    pdfjsAltText: true,
    smartTabGroups: true,
    linkPreviewKeyPoints: true,
    sidebarChatbot: true,
    smartWindow: true,
    speechRecognition: true,
    generatedWidgets: true,
  });
});

add_task(function test_pref_observer_feature_change() {
  Services.fog.testResetFOG();
  resetPrefs({
    default: "available",
  });

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    false,
    "Global should be false"
  );
  Assert.equal(
    Glean.browser.aiControlIsBlocking.sidebarChatbot.testGetValue(),
    false,
    "sidebarChatbot should be false initially"
  );

  Services.prefs.setStringPref("browser.ai.control.sidebarChatbot", "blocked");

  Assert.equal(
    Glean.browser.globalAiControlIsBlocking.testGetValue(),
    false,
    "Global should remain false"
  );
  Assert.equal(
    Glean.browser.aiControlIsBlocking.sidebarChatbot.testGetValue(),
    true,
    "sidebarChatbot should update to true"
  );
});
