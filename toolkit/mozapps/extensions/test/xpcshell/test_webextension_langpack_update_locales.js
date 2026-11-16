/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
"use strict";

/**
 * Updating langpacks of locales that are not negotiated as app locales must
 * not reorder the app locales nor fire intl:app-locales-changed
 * (Bug 2062777).
 */

Services.prefs.setBoolPref(PREF_EM_CHECK_UPDATE_SECURITY, false);

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "58");

// Region-range matches of the requested en-US, installed but not updated.
const STATIC_LOCALES = ["en-AU", "en-CA", "en-GB", "en-IE", "en-NZ", "en-ZA"];
// Updated langpacks, none of them negotiated as an app locale.
const UPDATED_LOCALES = ["de", "es-ES", "fr", "it", "ja", "pl"];
const LOCALES = [...STATIC_LOCALES, ...UPDATED_LOCALES];
const OLD_VERSION = "58.0.20230105.121014";
const NEW_VERSION = "58.0.20230207.112555";

const server = AddonTestUtils.createHttpServer({ hosts: ["example.com"] });

function langpackId(locale) {
  return `langpack-${locale}@test.mozilla.org`;
}

function langpackFiles(locale, version) {
  return {
    [`browser/localization/${locale}/browser.ftl`]: "message-browser = Value\n",
    "manifest.json": {
      name: `${locale} Language Pack`,
      version,
      manifest_version: 2,
      browser_specific_settings: {
        gecko: {
          id: langpackId(locale),
          strict_min_version: "58.0",
          strict_max_version: "58.*",
          update_url: "http://example.com/updates.json",
        },
      },
      sources: { browser: { base_path: "browser/" } },
      langpack_id: locale,
      languages: { [locale]: { chrome_resources: {}, version: "1" } },
    },
  };
}

const updates = { addons: {} };
for (const locale of UPDATED_LOCALES) {
  const id = langpackId(locale);
  const path = `/addons/${id}.xpi`;
  server.registerFile(
    path,
    AddonTestUtils.createTempXPIFile(langpackFiles(locale, NEW_VERSION))
  );
  updates.addons[id] = {
    updates: [
      {
        version: NEW_VERSION,
        update_link: `http://example.com${path}`,
        applications: {
          gecko: { strict_min_version: "58.0", strict_max_version: "58.*" },
        },
      },
    ],
  };
}
AddonTestUtils.registerJSON(server, "/updates.json", updates);

function promiseLangpackStartups(count) {
  return new Promise(resolve => {
    const TOPIC = "webextension-langpack-startup";
    let seen = 0;
    Services.obs.addObserver(function observer() {
      if (++seen === count) {
        Services.obs.removeObserver(observer, TOPIC);
        resolve();
      }
    }, TOPIC);
  });
}

add_task(async function test_langpack_updates_do_not_change_app_locales() {
  await promiseStartupManager("58");

  const started = promiseLangpackStartups(LOCALES.length);
  for (const locale of LOCALES) {
    await AddonTestUtils.promiseInstallXPI(langpackFiles(locale, OLD_VERSION));
  }
  await started;

  const originalRequested = Services.locale.requestedLocales;
  registerCleanupFunction(() => {
    Services.locale.requestedLocales = originalRequested;
  });
  Services.locale.requestedLocales = ["en-US"];
  const appLocales = Services.locale.appLocalesAsBCP47;

  let fired = 0;
  const observer = () => fired++;
  Services.obs.addObserver(observer, "intl:app-locales-changed");

  const updated = promiseLangpackStartups(UPDATED_LOCALES.length);
  const installs = [];
  for (const locale of UPDATED_LOCALES) {
    const addon = await promiseAddonByID(langpackId(locale));
    const update = await promiseFindAddonUpdates(
      addon,
      AddonManager.UPDATE_WHEN_USER_REQUESTED
    );
    Assert.ok(update.updateAvailable, `Update available for ${locale}`);
    installs.push(update.updateAvailable);
  }
  await Promise.all(installs.map(install => install.install()));
  await updated;

  Services.obs.removeObserver(observer, "intl:app-locales-changed");

  for (const locale of UPDATED_LOCALES) {
    const addon = await promiseAddonByID(langpackId(locale));
    Assert.equal(addon.version, NEW_VERSION, `${locale} langpack was updated`);
  }
  Assert.equal(fired, 0, "intl:app-locales-changed did not fire");
  Assert.deepEqual(
    Services.locale.appLocalesAsBCP47,
    appLocales,
    "App locales keep the same order"
  );

  await promiseShutdownManager();
});
