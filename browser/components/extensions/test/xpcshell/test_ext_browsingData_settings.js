"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
});

const PREF_DOMAIN = "privacy.cpd.";
const SETTINGS_LIST = [
  "cache",
  "cookies",
  "history",
  "formData",
  "downloads",
].sort();

add_task(async function testSettingsProperties() {
  function background() {
    browser.test.onMessage.addListener(() => {
      browser.browsingData.settings().then(settings => {
        browser.test.sendMessage("settings", settings);
      });
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      permissions: ["browsingData"],
    },
  });

  await extension.startup();

  extension.sendMessage("settings");
  let settings = await extension.awaitMessage("settings");

  // Verify that we get the keys back we expect.
  deepEqual(
    Object.keys(settings.dataToRemove).sort(),
    SETTINGS_LIST,
    "dataToRemove contains expected properties."
  );
  deepEqual(
    Object.keys(settings.dataRemovalPermitted).sort(),
    SETTINGS_LIST,
    "dataToRemove contains expected properties."
  );

  let dataTypeSet = settings.dataToRemove;
  for (let key of Object.keys(dataTypeSet)) {
    equal(
      Services.prefs.getBoolPref(`${PREF_DOMAIN}${key.toLowerCase()}`),
      dataTypeSet[key],
      `${key} property of dataToRemove matches the expected pref.`
    );
  }

  dataTypeSet = settings.dataRemovalPermitted;
  for (let key of Object.keys(dataTypeSet)) {
    equal(
      true,
      dataTypeSet[key],
      `${key} property of dataRemovalPermitted is true.`
    );
  }

  // Explicitly set a pref to both true and false and then check.
  const SINGLE_OPTION = "cache";
  const SINGLE_PREF = "privacy.cpd.cache";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(SINGLE_PREF);
  });

  Services.prefs.setBoolPref(SINGLE_PREF, true);

  extension.sendMessage("settings");
  settings = await extension.awaitMessage("settings");
  equal(
    settings.dataToRemove[SINGLE_OPTION],
    true,
    "Preference that was set to true returns true."
  );

  Services.prefs.setBoolPref(SINGLE_PREF, false);

  extension.sendMessage("settings");
  settings = await extension.awaitMessage("settings");
  equal(
    settings.dataToRemove[SINGLE_OPTION],
    false,
    "Preference that was set to false returns false."
  );

  await extension.unload();
});

add_task(async function testSettingsSince() {
  const TIMESPAN_PREF = "privacy.sanitize.timeSpan";
  const TEST_DATA = {
    TIMESPAN_5MIN: 5 * 60 * 1000,
    TIMESPAN_HOUR: 60 * 60 * 1000,
    TIMESPAN_2HOURS: 2 * 60 * 60 * 1000,
    TIMESPAN_EVERYTHING: null,
  };

  function background() {
    browser.test.onMessage.addListener(() => {
      browser.browsingData.settings().then(settings => {
        browser.test.sendMessage("settings", settings);
      });
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      permissions: ["browsingData"],
    },
  });

  await extension.startup();

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(TIMESPAN_PREF);
  });

  for (let timespan in TEST_DATA) {
    Services.prefs.setIntPref(TIMESPAN_PREF, Sanitizer[timespan]);

    let before = Date.now();
    extension.sendMessage("settings");
    let settings = await extension.awaitMessage("settings");
    let after = Date.now();

    if (TEST_DATA[timespan] === null) {
      Assert.equal(
        settings.options.since,
        0,
        `settings.options contains the expected since value for ${timespan}.`
      );
      continue;
    }

    // since is computed from the current time when settings() runs, so it
    // lies between the times the message was sent and the reply received.
    Assert.greaterOrEqual(
      settings.options.since,
      before - TEST_DATA[timespan],
      `settings.options.since is not earlier than expected for ${timespan}.`
    );
    Assert.lessOrEqual(
      settings.options.since,
      after - TEST_DATA[timespan],
      `settings.options.since is not later than expected for ${timespan}.`
    );
  }

  await extension.unload();
});
