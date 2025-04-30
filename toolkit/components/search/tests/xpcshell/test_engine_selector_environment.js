/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This tests the SearchEngineSelector's functionality in correctly filtering the
 * engines from the config based on the user's environment.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SearchEngineSelector: "resource://gre/modules/SearchEngineSelector.sys.mjs",
});

const CONFIG_EVERYWHERE = [
  {
    recordType: "engine",
    identifier: "engine-everywhere",
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
    identifier: "engine-everywhere-except-en-US",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          excludedLocales: ["en-US"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-everywhere-except-FI",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          excludedRegions: ["FI"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-everywhere-except-en-CA-and-CA",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          excludedRegions: ["CA"],
          excludedLocales: ["en-CA"],
        },
      },
    ],
  },
  // We use en-CA as a locale in the test, so we need to add that as an
  // available locale to avoid falling back to en-US.
  { recordType: "availableLocales", locales: ["en-CA", "en-GB", "en-US"] },
];

const CONFIG_EXPERIMENT = [
  {
    recordType: "engine",
    identifier: "engine-experiment",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          experiment: "experiment",
        },
      },
    ],
  },
];

const CONFIG_LOCALES_AND_REGIONS = [
  {
    recordType: "engine",
    identifier: "engine-canada",
    base: {},
    variants: [
      {
        environment: {
          locales: ["en-CA"],
          regions: ["CA"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-exclude-regions",
    base: {},
    variants: [
      {
        environment: {
          locales: ["en-GB"],
          excludedRegions: ["US"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-specific-locale-in-all-regions",
    base: {},
    variants: [
      {
        environment: {
          locales: ["en-US"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-exclude-locale",
    base: {},
    variants: [
      {
        environment: {
          excludedLocales: ["fr"],
          regions: ["BE"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-specific-region-with-any-locales",
    base: {},
    variants: [
      {
        environment: {
          regions: ["FI"],
        },
      },
    ],
  },
];

const CONFIG_DISTRIBUTION = [
  {
    recordType: "engine",
    identifier: "engine-distribution-1",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distribution-1"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-multiple-distributions",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distribution-2", "distribution-3"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-distribution-region-locales",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distribution-4"],
          locales: ["fi"],
          regions: ["FI"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-distribution-experiment",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distribution-5"],
          experiment: "experiment",
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-distribution-excluded",
    base: {},
    variants: [
      {
        environment: {
          distributions: ["distribution-include"],
          excludedDistributions: ["distribution-exclude"],
        },
      },
    ],
  },
];

const CONFIG_CHANNEL_APPLICATION = [
  {
    recordType: "engine",
    identifier: "engine-channel",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          channels: ["esr"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-application",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          applications: ["firefox"],
        },
      },
    ],
  },
];

const CONFIG_OPTIONAL = [
  {
    recordType: "engine",
    identifier: "engine-optional-true",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          applications: ["firefox"],
        },
        optional: true,
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-optional-false",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          applications: ["firefox"],
        },
        optional: false,
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-optional-undefined",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          applications: ["firefox"],
        },
      },
    ],
  },
];

const CONFIG_VERSIONS = [
  {
    recordType: "engine",
    identifier: "engine-min-1",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          minVersion: "1",
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-max-20",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          maxVersion: "20",
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-min-max-1-10",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          minVersion: "1",
          maxVersion: "10",
        },
      },
    ],
  },
];

const CONFIG_DEVICE_TYPE_LAYOUT = [
  {
    recordType: "engine",
    identifier: "engine-no-device-type",
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
    identifier: "engine-single-device-type",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          deviceType: ["tablet"],
        },
      },
    ],
  },
  {
    recordType: "engine",
    identifier: "engine-multiple-device-type",
    base: {},
    variants: [
      {
        environment: {
          allRegionsAndLocales: true,
          deviceType: ["tablet", "smartphone"],
        },
      },
    ],
  },
];

const engineSelector = new SearchEngineSelector();

add_setup(async () => {
  // In this test we have not specified the combinations of default search engine
  // according to the environment. This causes the selector to fall back to the first
  // engine in the list and throw an error about it. For the purposes of this test,
  // this is not an issue.
  consoleAllowList.push(
    "Could not find a matching default engine, using the first one in the list"
  );
});

/**
 * This function asserts if the actual engine identifiers returned equals
 * the expected engines.
 *
 * @param {object} config
 *   A mock search config contain engines.
 * @param {object} userEnv
 *   A fake user's environment including locale and region, experiment, etc.
 * @param {Array} expectedEngines
 *   The array of expected engine identifiers to be returned from the config.
 * @param {string} message
 *   The assertion message.
 */
async function assertActualEnginesEqualsExpected(
  config,
  userEnv,
  expectedEngines,
  message
) {
  engineSelector._configuration = null;
  SearchTestUtils.setRemoteSettingsConfig(config, []);

  if (expectedEngines.length) {
    let { engines } = await engineSelector.fetchEngineConfiguration(userEnv);
    let actualEngines = engines.map(engine => engine.identifier);
    Assert.deepEqual(actualEngines, expectedEngines, message);
  } else {
    await Assert.rejects(
      engineSelector.fetchEngineConfiguration(userEnv),
      /Could not find any engines in the filtered configuration/,
      message
    );
  }
}

add_task(async function test_selector_match_experiment() {
  await assertActualEnginesEqualsExpected(
    CONFIG_EXPERIMENT,
    {
      locale: "en-CA",
      region: "ca",
      experiment: "experiment",
    },
    ["engine-experiment"],
    "Should match engine with the same experiment."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_EXPERIMENT,
    {
      locale: "en-CA",
      region: "ca",
      experiment: "no-match-experiment",
    },
    [],
    "Should not match any engines without experiment."
  );
});

add_task(async function test_everywhere_and_excluded_locale() {
  await assertActualEnginesEqualsExpected(
    CONFIG_EVERYWHERE,
    {
      locale: "en-GB",
      region: "GB",
    },
    SearchUtils.rustSelectorFeatureGate
      ? [
          "engine-everywhere",
          "engine-everywhere-except-FI",
          "engine-everywhere-except-en-CA-and-CA",
          "engine-everywhere-except-en-US",
        ]
      : [
          "engine-everywhere",
          "engine-everywhere-except-en-CA-and-CA",
          "engine-everywhere-except-en-US",
          "engine-everywhere-except-FI",
        ],
    "Should match the engines for all locales and regions."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_EVERYWHERE,
    {
      locale: "en-US",
      region: "US",
    },
    SearchUtils.rustSelectorFeatureGate
      ? [
          "engine-everywhere",
          "engine-everywhere-except-FI",
          "engine-everywhere-except-en-CA-and-CA",
        ]
      : [
          "engine-everywhere",
          "engine-everywhere-except-en-CA-and-CA",
          "engine-everywhere-except-FI",
        ],
    "Should match engines that do not exclude user's locale."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_EVERYWHERE,
    {
      locale: "fi",
      region: "FI",
    },
    [
      "engine-everywhere",
      "engine-everywhere-except-en-CA-and-CA",
      "engine-everywhere-except-en-US",
    ],
    "Should match engines that do not exclude user's region."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_EVERYWHERE,
    {
      locale: "en-CA",
      region: "CA",
    },
    SearchUtils.rustSelectorFeatureGate
      ? [
          "engine-everywhere",
          "engine-everywhere-except-FI",
          "engine-everywhere-except-en-US",
        ]
      : [
          "engine-everywhere",
          "engine-everywhere-except-en-US",
          "engine-everywhere-except-FI",
        ],
    "Should match engine that do not exclude user's region and locale."
  );
});

add_task(async function test_selector_locales_and_regions() {
  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "en-CA",
      region: "CA",
    },
    ["engine-canada"],
    "Should match engine with same locale and region."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "en-GB",
      region: "US",
    },
    [],
    "Should not match any engines when region is excluded."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "en-US",
      region: "AU",
    },
    ["engine-specific-locale-in-all-regions"],
    "Should match engine with specified locale in any region."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "en-US",
      region: "NL",
    },
    ["engine-specific-locale-in-all-regions"],
    "Should match engine with specified locale in any region."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "fr",
      region: "BE",
    },
    [],
    "Should not match any engines when locale is excluded."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "fi",
      region: "FI",
    },
    ["engine-specific-region-with-any-locales"],
    "Should match engine with specified region with any locale."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_LOCALES_AND_REGIONS,
    {
      locale: "tlh",
      region: "FI",
    },
    ["engine-specific-region-with-any-locales"],
    "Should match engine with specified region with any locale."
  );
});

add_task(async function test_selector_match_distribution() {
  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-1",
    },
    ["engine-distribution-1"],
    "Should match engine with the same distribution."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-2",
    },
    ["engine-multiple-distributions"],
    "Should match engine with multiple distributions."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-3",
    },
    ["engine-multiple-distributions"],
    "Should match engine with multiple distributions."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "fi",
      region: "FI",
      distroID: "distribution-4",
    },
    ["engine-distribution-region-locales"],
    "Should match engine with distribution, specific region and locale."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-4",
    },
    [],
    "Should not match any engines with no matching distribution, region and locale."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-5",
      experiment: "experiment",
    },
    ["engine-distribution-experiment"],
    "Should match engine with distribution and experiment."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-5",
      experiment: "no-match-experiment",
    },
    [],
    "Should not match any engines with no matching distribution and experiment."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-include",
    },
    ["engine-distribution-excluded"],
    "Should match engines with included distributions."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_DISTRIBUTION,
    {
      locale: "en-CA",
      region: "CA",
      distroID: "distribution-exclude",
    },
    [],
    "Should not match any engines with excluded distribution."
  );
});

add_task(async function test_engine_selector_match_applications() {
  await assertActualEnginesEqualsExpected(
    CONFIG_CHANNEL_APPLICATION,
    {
      locale: "en-CA",
      region: "CA",
      channel: "esr",
      appName: "firefox-android",
    },
    ["engine-channel"],
    "Should match engine for esr channel."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_CHANNEL_APPLICATION,
    {
      locale: "en-CA",
      region: "CA",
      appName: "firefox",
    },
    ["engine-application"],
    "Should match engine for application."
  );
});

add_task(async function test_engine_selector_match_version() {
  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "1",
    },
    ["engine-min-1", "engine-max-20", "engine-min-max-1-10"],
    "Should match engines with that support versions equal or above the minimum."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "30",
    },
    ["engine-min-1"],
    "Should match engines with that support versions above the minimum."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "20",
    },
    ["engine-min-1", "engine-max-20"],
    "Should match engines with that support versions equal or below the maximum."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "5",
    },
    ["engine-min-1", "engine-max-20", "engine-min-max-1-10"],
    "Should match engines with that support the versions above the minimum and below the maximum."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "15",
    },
    ["engine-min-1", "engine-max-20"],
    "Should match engines with that support the versions above the minimum and below the maximum."
  );

  await assertActualEnginesEqualsExpected(
    CONFIG_VERSIONS,
    {
      locale: "en-CA",
      region: "CA",
      version: "",
    },
    [],
    "Should match no engines with no matching versions."
  );
});

add_task(async function test_engine_selector_does_not_match_optional_engines() {
  await assertActualEnginesEqualsExpected(
    CONFIG_OPTIONAL,
    {
      locale: "en-CA",
      region: "CA",
      appName: "firefox",
    },
    ["engine-optional-false", "engine-optional-undefined"],
    "Should match engines where optional flag is false or undefined"
  );
});

add_task(async function test_engine_selector_match_device_type() {
  await assertActualEnginesEqualsExpected(
    CONFIG_DEVICE_TYPE_LAYOUT,
    {
      locale: "en-CA",
      region: "CA",
    },
    ["engine-no-device-type"],
    "Should only match engines with no device type."
  );
});
