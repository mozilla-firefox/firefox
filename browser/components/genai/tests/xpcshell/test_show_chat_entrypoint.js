/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "resource:///modules/GenAI.sys.mjs"
);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.ai.control.sidebarChatbot");
  Services.prefs.clearUserPref("browser.ml.chat.enabled");
  Services.prefs.clearUserPref("browser.ml.chat.page");
  Services.prefs.clearUserPref("browser.ml.chat.provider");
  Services.prefs.clearUserPref("sidebar.main.tools");
  Services.prefs.clearUserPref("sidebar.revamp");
});

/**
 * Check the sidebar chatbot AIFeature state transitions.
 */
add_task(async function test_chat_aifeature_states() {
  Services.prefs.setBoolPref("browser.ml.chat.enabled", false);
  Services.prefs.setBoolPref("browser.ml.chat.page", false);
  Services.prefs.setStringPref("browser.ml.chat.provider", "");

  Assert.equal(GenAI.isBlocked, true, "Blocked when chat is disabled");
  Assert.equal(GenAI.isEnabled, false, "Disabled without a provider");
  Assert.equal(GenAI.aiControlState, "blocked", "Blocked AI Controls state");

  await GenAI.makeAvailable();

  Assert.equal(
    Services.prefs.getBoolPref("browser.ml.chat.enabled"),
    true,
    "makeAvailable() enables the feature"
  );
  Assert.equal(
    Services.prefs.getBoolPref("browser.ml.chat.page"),
    true,
    "makeAvailable() restores the default page-chat state"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("browser.ml.chat.page"),
    "makeAvailable() clears the page-chat user pref"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("browser.ml.chat.provider"),
    "makeAvailable() clears the provider user pref"
  );
  Assert.equal(
    GenAI.aiControlState,
    "available",
    "Available without a chosen provider"
  );

  await GenAI.enable();

  Assert.equal(
    GenAI.aiControlState,
    "available",
    "enable() alone remains available until a provider is chosen"
  );
  Assert.equal(
    GenAI.isEnabled,
    false,
    "Not enabled until a provider is chosen"
  );

  Services.prefs.setStringPref(
    "browser.ml.chat.provider",
    "http://mochi.test:8888"
  );

  Assert.equal(GenAI.isEnabled, true, "Enabled once a provider is chosen");
  Assert.equal(
    GenAI.aiControlState,
    "enabled",
    "Choosing a provider makes the chatbot enabled"
  );

  await GenAI.block();

  Assert.equal(GenAI.isBlocked, true, "Blocked after block()");
  Assert.equal(GenAI.isEnabled, false, "Not enabled after block()");
  Assert.equal(
    Services.prefs.getBoolPref("browser.ml.chat.page"),
    false,
    "block() disables page chat"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("browser.ml.chat.provider"),
    "block() clears the provider user pref"
  );
  Assert.equal(GenAI.aiControlState, "blocked", "Blocked after block()");
});

/**
 * Check various prefs for showing chat
 */
add_task(async function test_show_chat() {
  // Test should start with sidebar.revamp set to false
  Services.prefs.setBoolPref("sidebar.revamp", false);

  Assert.ok(!GenAI.canShowChatEntrypoint, "Default no");

  Services.prefs.setBoolPref("browser.ml.chat.enabled", true);

  Assert.ok(!GenAI.canShowChatEntrypoint, "Not enough to just enable");

  Services.prefs.setStringPref(
    "browser.ml.chat.provider",
    "http://mochi.test:8888"
  );

  Assert.ok(GenAI.canShowChatEntrypoint, "Can show with provider");

  Services.prefs.setStringPref("sidebar.main.tools", "aichat");
  Services.prefs.setBoolPref("sidebar.revamp", true);

  Assert.ok(
    GenAI.canShowChatEntrypoint,
    "Can show with revamp and aichat tool"
  );

  Services.prefs.setStringPref("sidebar.main.tools", "history");

  Assert.ok(!GenAI.canShowChatEntrypoint, "Not shown without chatbot tool");

  Services.prefs.setBoolPref("sidebar.revamp", false);

  Assert.ok(GenAI.canShowChatEntrypoint, "Ignore tools without revamp");
});
