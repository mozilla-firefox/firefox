/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This tests the SearchEngineSelector in finding the correct default engine
 * and private default engine based on the user's environment.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SearchEngineSelector: "resource://gre/modules/SearchEngineSelector.sys.mjs",
});

const CONFIG = [
  {
    recordType: "engine",
    identifier: "global-default",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "global-default-private",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "default-specific-location",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "default-specific-distro",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "default-starts-with-ca",
    base: {},
    variants: [
      {
        environment: { locales: ["en-US"], regions: ["CA"] },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "default-starts-with-us",
    base: {},
    variants: [
      {
        environment: { locales: ["en-US"], regions: ["US"] },
      },
    ],
  },
  {
    recordType: "defaultEngines",
    globalDefault: "global-default",
    globalDefaultPrivate: "global-default-private",
    specificDefaults: [
      {
        environment: { locales: ["zh-CN"], regions: ["cn"] },
        default: "default-specific-location",
        defaultPrivate: "default-specific-location",
      },
      {
        environment: { distributions: ["specific-distro"] },
        default: "default-specific-distro",
        defaultPrivate: "default-specific-distro",
      },
      {
        environment: { locales: ["en-US"], regions: ["CA"] },
        default: "default-starts-with*",
        defaultPrivate: "default-starts-with*",
      },
      {
        environment: { locales: ["en-US"], regions: ["US"] },
        default: "default-starts-with*",
        defaultPrivate: "default-starts-with*",
      },
    ],
  },
  {
    recordType: "engineOrders",
    orders: [],
  },
  {
    recordType: "availableLocales",
    // We use en-CA as a locale in the test, so we need to add that as an
    // available locale to avoid falling back to en-US.
    locales: ["en-CA", "en-US", "zh-CN"],
  },
];

const CONFIG_DEFAULTS_OVERRIDE = [
  {
    recordType: "engine",
    identifier: "engine-global",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-locale-de",
    base: {},
    variants: [
      {
        environment: {
          locales: ["de"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-distro",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distro"],
          regions: ["FR"],
        },
      },
    ],
  },
  {
    recordType: "defaultEngines",
    globalDefault: "engine-global",
    specificDefaults: [
      {
        environment: { locales: ["de"] },
        default: "engine-locale-de",
      },
      {
        environment: { distributions: ["distro"] },
        default: "engine-distro",
      },
    ],
  },
  {
    recordType: "engineOrders",
    orders: [],
  },
];

const engineSelector = new SearchEngineSelector();

/**
 * This function asserts if the actual engine identifiers returned equals
 * the expected engines.
 *
 * @param {object} config
 *   A mock search config contain engines.
 * @param {object} userEnv
 *   A fake user's environment including locale and region, experiment, etc.
 * @param {string} expectedDefault
 *   The identifer of the expected default engine.
 * @param {string} expectedDefaultPrivate
 *   The identifer of the expected default private engine.
 * @param {string} message
 *   The description of the test.
 */
async function assertActualEnginesEqualsExpected(
  config,
  userEnv,
  expectedDefault,
  expectedDefaultPrivate,
  message
) {
  engineSelector._configuration = null;
  SearchTestUtils.setRemoteSettingsConfig(config, []);

  let { appDefaultEngineId, appPrivateDefaultEngineId } =
    await engineSelector.fetchEngineConfiguration(userEnv);

  info(`${message}`);
  Assert.equal(
    appDefaultEngineId,
    expectedDefault,
    `Should match the default engine ${expectedDefault}.`
  );

  Assert.equal(
    appPrivateDefaultEngineId,
    expectedDefaultPrivate,
    `Should match default private engine ${expectedDefaultPrivate}.`
  );
}

add_task(async function test_default_engines() {
  await assertActualEnginesEqualsExpected(
    CONFIG,
    {
      locale: "en-CA",
      region: "ca",
    },
    "global-default",
    "global-default-private",
    "Should use the global default engine and global default private when no specific defaults are matched."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG,
    {
      locale: "zh-CN",
      region: "cn",
    },
    "default-specific-location",
    "default-specific-location",
    "Should use the matched locale and region default engine."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG,
    {
      locale: "fi",
      region: "FI",
      distroID: "specific-distro",
    },
    "default-specific-distro",
    "default-specific-distro",
    "Should use the matched distribution default engine."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG,
    {
      locale: "en-US",
      region: "CA",
    },
    "default-starts-with-ca",
    "default-starts-with-ca",
    "Should use the matched default engine with specific suffix."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG,
    {
      locale: "en-US",
      region: "US",
    },
    "default-starts-with-us",
    "default-starts-with-us",
    "Should use the matched default engine with specific suffix."
  );
});

add_task(async function test_default_engines_override() {
  await assertActualEnginesEqualsExpected(
    CONFIG_DEFAULTS_OVERRIDE,
    {
      locale: "en-US",
      region: "US",
    },
    "engine-global",
    undefined,
    "Should use the globalDefault for default when no specific defaults are matched. Private default should be undefined when no default private."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DEFAULTS_OVERRIDE,
    {
      locale: "de",
      region: "US",
    },
    "engine-locale-de",
    undefined,
    "Should use the matched locale default engine."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DEFAULTS_OVERRIDE,
    {
      locale: "de",
      region: "FR",
    },
    "engine-locale-de",
    undefined,
    "Should use the matched locale default engine."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DEFAULTS_OVERRIDE,
    {
      locale: "en-US",
      region: "FR",
      distroID: "distro",
    },
    "engine-distro",
    undefined,
    "Should use the matched distro default engine."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DEFAULTS_OVERRIDE,
    {
      locale: "de",
      region: "FR",
      distroID: "distro",
    },
    "engine-distro",
    undefined,
    "Should use the last matched default engine when multiple default engines are matched."
  );
});
