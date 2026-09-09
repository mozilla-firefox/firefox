/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchEngine: "moz-src:///toolkit/components/search/SearchEngine.sys.mjs",
  SearchEngineSelector:
    "moz-src:///toolkit/components/search/SearchEngineSelector.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  updateAppInfo: "resource://testing-common/AppInfo.sys.mjs",
});

/**
 * @import {AppProvidedConfigEngine} from "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs"
 */

const GLOBAL_SCOPE = this;
const TEST_DEBUG = Services.env.get("TEST_DEBUG");

const URLTYPE_SUGGEST_JSON = "application/x-suggestions+json";
const URLTYPE_SEARCH_HTML = "text/html";

const ENGINES_URLS = {
  "prod-main":
    "https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/search-config/records",
  "prod-preview":
    "https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/search-config/records",
  "stage-main":
    "https://firefox.settings.services.allizom.org/v1/buckets/main/collections/search-config/records",
  "stage-preview":
    "https://firefox.settings.services.allizom.org/v1/buckets/main-preview/collections/search-config/records",
};

/**
 * This function is used to override the remote settings configuration
 * if the SEARCH_CONFIG environment variable is set. This allows testing
 * against a remote server.
 */
async function maybeSetupConfig() {
  const SEARCH_CONFIG = Services.env.get("SEARCH_CONFIG");
  if (SEARCH_CONFIG) {
    if (!(SEARCH_CONFIG in ENGINES_URLS)) {
      throw new Error(`Invalid value for SEARCH_CONFIG`);
    }
    const url = ENGINES_URLS[SEARCH_CONFIG];
    const response = await fetch(url);
    const config = await response.json();
    SearchTestUtils.setRemoteSettingsConfig(config.data);
  }
}

/**
 * @typedef {object} RegionLocaleDetails
 * @property {string[]} [regions]
 *   The regions where the search engine should be available. Regions are typically
 *   displayed as lower case. If no regions are specified, it is assumed that
 *   any region will match.
 * @property {string[]} [locales]
 *   The locales where the search engine should be available. If no locales are
 *   specified, it is assumed that any locales will match.
 */

/**
 * @typedef {object} DeploymentDetails
 *   Where the search engine should be deployed to.
 *
 *   If neither included nor excluded are specified, the search engine should
 *   not be available anywhere.
 *
 *   If excluded is specified and included is not specified, the search engine
 *   should be available everywhere.
 *
 * @property {RegionLocaleDetails[]} [included]
 *   Where the search engine should be available.
 * @property {RegionLocaleDetails[]} [excluded]
 *   Where the search engine should not be available.
 */

/**
 * @typedef {object} EngineRuleDetails
 * @property {string} domain
 *   The expected domain of the search URL.
 * @property {string} telemetryId
 *   The expected telemetry ID of the search engine. This is deprecated, but
 *   we still test it.
 * @property {string} [partnerCode]
 *   The expected partner code property of the search engine.
 * @property {string} [searchUrlCode]
 *   The expected parameter name and value for the partner code in the search URL.
 * @property {string} [searchUrlParamNotInQuery]
 *   When specified, this parameter should not be included in the query part of
 *   the search URL.
 * @property {string} [suggestUrlCode]
 *   Expected parameter name and value within the suggestion URL, e.g. for checking
 *   regional parameters.
 * @property {string[]} [aliases]
 *   The expected aliases for the search engine.
 * @property {string[]} [required_aliases]
 *   Required aliases for the search engine. These aliases must be associated
 *   with the engine, but additional aliases may also be supplied.
 * @property {boolean} [noSuggestionsURL]
 *   Set to true when there are no suggestions available for this search engine.
 */

/**
 * @typedef {object} SearchConfigTestDetails
 *   Details for the search configuration tests for a single engine.
 * @property {string} [identifier]
 *   The identifier for the search engine under test. If not specified then
 *   `identifierStartsWith` must be specified.
 * @property {string} [identifierStartsWith]
 *   The prefix of the identifier of the search engine under test. This is used
 *   for search engines that have different set ups per locale/region and have
 *   different identifiers as a result, e.g. wikipedia, eBay.
 * @property {string[]} [aliases]
 *   The expected aliases for the search engine.
 * @property {DeploymentDetails} default
 *   Details of where the engine should be listed as default.
 * @property {DeploymentDetails} available
 *   Details of where the engine should be listed as available.
 * @property {string} [suggestionUrlBase]
 *   The base URL for search suggestion lookup.
 * @property {boolean} [noSuggestionsURL]
 *   Set to true when there are no suggestions available for this search engine.
 * @property {(EngineRuleDetails & DeploymentDetails)[]} details
 *   Specific details of the test for checking URL details and telemetry information.
 */

/**
 * Processes the configuration to get the search engines for the specified
 * region/locale.
 *
 * @param {SearchEngineSelector} engineSelector
 * @param {string} region
 * @param {string} locale
 * @returns {Promise<{engines: AppProvidedConfigEngine[], appDefaultEngineId: string}>}
 */
async function getEngines(engineSelector, region, locale) {
  let configs = await engineSelector.fetchEngineConfiguration({
    locale,
    region,
    channel: SearchUtils.MODIFIED_APP_CHANNEL,
  });

  return {
    engines: await SearchTestUtils.searchConfigToEngines(configs.engines),
    appDefaultEngineId: configs.appDefaultEngineId,
  };
}

/**
 * This class implements the test harness for search configuration tests.
 * These tests are designed to ensure that the correct search engines are
 * loaded for the various region/locale configurations.
 */
class SearchConfigTest {
  /**
   * @type {?SearchEngineSelector}
   */
  #engineSelector;

  /**
   * @type {SearchConfigTestDetails[]}
   */
  #testDetails;

  /**
   * @param {SearchConfigTestDetails[]} testDetails
   *   An array of configurations for testing multiple engines.
   */
  constructor(testDetails) {
    this.#testDetails = testDetails;
  }

  /**
   * Sets up the test.
   *
   * @param {string} [version]
   *   The version to simulate for running the tests.
   */
  async setup(version = "42.0") {
    updateAppInfo({
      name: "firefox",
      ID: "xpcshell@tests.mozilla.org",
      version,
      platformVersion: version,
    });

    await maybeSetupConfig();

    this.#engineSelector = new SearchEngineSelector();
  }

  /**
   * Runs the test.
   */
  async run() {
    const locales = await this.getLocales();
    const regions = this._regions;

    // We loop on region and then locale, so that we always cause a re-init
    // when updating the requested/available locales.
    for (let region of regions) {
      for (let locale of locales) {
        const { engines, appDefaultEngineId } = await getEngines(
          this.#engineSelector,
          region,
          locale
        );

        // Test each configuration in this single iteration
        for (let testDetails of this.#testDetails) {
          this._assertEngineRules(
            engines.filter(e => e.id == appDefaultEngineId),
            region,
            locale,
            "default",
            testDetails
          );
          const isPresent = this._assertAvailableEngines(
            region,
            locale,
            engines,
            testDetails
          );
          if (isPresent) {
            this._assertEngineDetails(region, locale, engines, testDetails);
          }
        }
      }
    }
  }

  /**
   * @returns {Set<?string>} the list of regions for the tests to run with.
   */
  get _regions() {
    if (TEST_DEBUG) {
      return new Set(["by", "cn", "kz", "us", "ru", "tr", "default"]);
    }
    return new Set([
      ...Services.intl.getAvailableLocaleDisplayNames("region"),
      "default",
    ]);
  }

  /**
   * @returns {Promise<string[]>} the list of locales for the tests to run with.
   */
  async getLocales() {
    if (TEST_DEBUG) {
      return ["be", "en-US", "kk", "tr", "ru", "zh-CN", "ach", "unknown"];
    }
    const data = await IOUtils.readUTF8(do_get_file("all-locales").path);
    // "en-US" is not in all-locales as it is the default locale
    // add it manually to ensure it is tested.
    let locales = [...data.split("\n").filter(e => e != ""), "en-US"];
    // BCP47 requires all variants are 5-8 characters long. Our
    // build sytem uses the short `mac` variant, this is invalid, and inside
    // the app we turn it into `ja-JP-macos`
    locales = locales.map(l => (l == "ja-JP-mac" ? "ja-JP-macos" : l));
    // The locale sometimes can be unknown or a strange name, e.g. if the updater
    // is disabled, it may be "und", add one here so we know what happens if we
    // hit it.
    locales.push("unknown");
    return locales;
  }

  /**
   * Determines if a locale/region pair matches a section of the test details.
   *
   * @param {RegionLocaleDetails[]} section
   *   The region/locale details to match against.
   * @param {string} region
   *   The two-letter region code.
   * @param {string} locale
   *   The two-letter locale code.
   * @returns {boolean}
   *   True if the locale/region pair matches the section.
   */
  _localeRegionInSection(section, region, locale) {
    for (const { regions, locales } of section) {
      // If we only specify a regions or locales section then
      // it is always considered included in the other section.
      const inRegions = !regions || regions.includes(region);
      const inLocales = !locales || locales.includes(locale);
      if (inRegions && inLocales) {
        return true;
      }
    }
    return false;
  }

  /**
   * Helper function to find an engine from within a list.
   *
   * @param {AppProvidedConfigEngine[]} engines
   *   The list of engines to check.
   * @param {SearchConfigTestDetails} testDetails
   *   The details of the test.
   * @returns {AppProvidedConfigEngine}
   *   Returns the engine if found, null otherwise.
   */
  _findEngine(engines, testDetails) {
    return engines.find(engine =>
      testDetails.identifier
        ? engine.id == testDetails.identifier
        : engine.id.startsWith(testDetails.identifierStartsWith)
    );
  }

  /**
   * Asserts whether the engines rules in the test section are met for the
   * associated engine.
   *
   * @param {AppProvidedConfigEngine[]} engines
   *   The list of engines to check.
   * @param {string} region
   *   The two-letter region code.
   * @param {string} locale
   *   The two-letter locale code.
   * @param {"default" | "available"} section
   *   The section of the test to check.
   * @param {SearchConfigTestDetails} testDetails
   *   The test details to use.
   * @returns {boolean}
   *   Returns true if the engine is expected to be present, false otherwise.
   */
  _assertEngineRules(engines, region, locale, section, testDetails) {
    const infoString = `region: "${region}" locale: "${locale}"`;
    const testSection = testDetails[section];
    const hasIncluded = "included" in testSection;
    const hasExcluded = "excluded" in testSection;
    const identifierIncluded = !!this._findEngine(engines, testDetails);

    // If there's not included/excluded, then this shouldn't be the default anywhere.
    if (section == "default" && !hasIncluded && !hasExcluded) {
      this.assertOk(
        !identifierIncluded,
        `${testDetails.identifier} should not be ${section} for any locale/region,
         currently set for ${infoString}`
      );
      return false;
    }

    // If there's no included section and no excluded, then we assume the
    // engine is not available anywhere. If there's no included section, but an
    // exluded section we assume it is available everywhere apart from the
    // exclusions.
    let included =
      hasIncluded &&
      this._localeRegionInSection(testSection.included, region, locale);

    let excluded =
      hasExcluded &&
      this._localeRegionInSection(testSection.excluded, region, locale);
    if (
      (included && (!hasExcluded || !excluded)) ||
      (!hasIncluded && hasExcluded && !excluded)
    ) {
      this.assertOk(
        identifierIncluded,
        `${testDetails.identifier} should be ${section} for ${infoString}`
      );
      return true;
    }
    this.assertOk(
      !identifierIncluded,
      `${testDetails.identifier} should not be ${section} for ${infoString}`
    );
    return false;
  }

  /**
   * Asserts whether the engine is correctly available or not.
   *
   * @param {string} region
   *   The two-letter region code.
   * @param {string} locale
   *   The two-letter locale code.
   * @param {AppProvidedConfigEngine[]} engines
   *   The current visible engines.
   * @param {SearchConfigTestDetails} testDetails
   *   The test details to use.
   * @returns {boolean}
   *   Returns true if the engine is expected to be present, false otherwise.
   */
  _assertAvailableEngines(region, locale, engines, testDetails) {
    return this._assertEngineRules(
      engines,
      region,
      locale,
      "available",
      testDetails
    );
  }

  /**
   * Asserts the engine follows various rules.
   *
   * @param {string} region
   *   The two-letter region code.
   * @param {string} locale
   *   The two-letter locale code.
   * @param {AppProvidedConfigEngine[]} engines
   *   The current visible engines.
   * @param {SearchConfigTestDetails} testDetails
   *   The test details to use.
   */
  _assertEngineDetails(region, locale, engines, testDetails) {
    const details = testDetails.details.filter(value => {
      const included = this._localeRegionInSection(
        value.included,
        region,
        locale
      );
      const excluded =
        value.excluded &&
        this._localeRegionInSection(value.excluded, region, locale);
      return included && !excluded;
    });
    this.assertEqual(
      details.length,
      1,
      `${testDetails.identifier} should have just one details section for region: ${region} locale: ${locale}`
    );

    const engine = this._findEngine(engines, testDetails);
    this.assertOk(
      engine,
      "${testDetails.identifier} should have an engine present"
    );

    if (testDetails.aliases) {
      this.assertDeepEqual(
        engine.aliases,
        testDetails.aliases,
        "Should have the correct aliases for the engine"
      );
    }

    const location = `in region:${region}, locale:${locale}`;

    for (const rule of details) {
      this._assertCorrectDomains(location, engine, rule, testDetails);
      this._assertCorrectUrlCode(location, engine, rule);
      if ("aliases" in rule) {
        this.assertDeepEqual(
          engine.aliases,
          rule.aliases,
          "Should have the correct aliases for the engine"
        );
      }
      if ("required_aliases" in rule) {
        this.assertOk(
          rule.required_aliases.every(a => engine.aliases.includes(a)),
          "Should have the required aliases for the engine"
        );
      }
      if ("telemetryId" in rule) {
        this.assertEqual(
          engine.telemetryId,
          rule.telemetryId,
          `Should have the correct telemetryId ${location}.`
        );
      }
      if ("partnerCode" in rule) {
        this.assertEqual(
          engine.partnerCode,
          rule.partnerCode,
          `Should have the correct partnerCode ${location}.`
        );
      }
    }
  }

  /**
   * Asserts whether the engine is using the correct domains or not.
   *
   * @param {string} location
   *   Debug string with locale + region information.
   * @param {AppProvidedConfigEngine} engine
   *   The engine being tested.
   * @param {EngineRuleDetails & DeploymentDetails} rules
   *   Rules to test.
   * @param {SearchConfigTestDetails} testDetails
   *   The test details to use.
   */
  _assertCorrectDomains(location, engine, rules, testDetails) {
    let submission = engine.getSubmission("test", URLTYPE_SEARCH_HTML);

    if (rules.scheme) {
      this.assertEqual(
        submission.uri.scheme,
        rules.scheme,
        `Should have the correct scheme for type: ${URLTYPE_SEARCH_HTML} ${location}.`
      );
    } else {
      this.assertOk(
        rules.domain,
        `${testDetails.identifier} should have an expectedDomain for the engine ${location}`
      );

      this.assertOk(
        submission.uri.host.endsWith(rules.domain),
        `Should have the correct domain for type: ${URLTYPE_SEARCH_HTML} ${location}.
       Got "${submission.uri.host}", expected to end with "${rules.domain}".`
      );
    }

    submission = engine.getSubmission("test", URLTYPE_SUGGEST_JSON);
    if (testDetails.noSuggestionsURL || rules.noSuggestionsURL) {
      this.assertOk(!submission, "Should not have a submission url");
    } else if (testDetails.suggestionUrlBase) {
      this.assertEqual(
        submission.uri.prePath + submission.uri.filePath,
        testDetails.suggestionUrlBase,
        `Should have the correct domain for type: ${URLTYPE_SUGGEST_JSON} ${location}.`
      );
      this.assertOk(
        submission.uri.query.includes(rules.suggestUrlCode),
        `Should have the code in the uri`
      );
    }
  }

  /**
   * Asserts whether the engine is using the correct URL codes or not.
   *
   * @param {string} location
   *   Debug string with locale + region information.
   * @param {AppProvidedConfigEngine} engine
   *   The engine being tested.
   * @param {EngineRuleDetails & DeploymentDetails} rule
   *   Rules to test.
   */
  _assertCorrectUrlCode(location, engine, rule) {
    if (rule.searchUrlCode) {
      const submission = engine.getSubmission("test", URLTYPE_SEARCH_HTML);
      this.assertOk(
        submission.uri.query.split("&").includes(rule.searchUrlCode),
        `Expected "${rule.searchUrlCode}" in search url "${submission.uri.spec}"`
      );
    }
    if (rule.searchUrlParamNotInQuery) {
      const submission = engine.getSubmission("test", URLTYPE_SEARCH_HTML);
      this.assertOk(
        !submission.uri.query.includes(rule.searchUrlParamNotInQuery),
        `Expected "${rule.searchUrlParamNotInQuery}" should not be in search url "${submission.uri.spec}"`
      );
    }
    if (rule.suggestUrlCode) {
      const submission = engine.getSubmission("test", URLTYPE_SUGGEST_JSON);
      this.assertOk(
        submission.uri.query.split("&").includes(rule.suggestUrlCode),
        `Expected "${rule.suggestUrlCode}" in suggestion url "${submission.uri.spec}"`
      );
    }
  }

  /**
   * Helper functions which avoid outputting test results when there are no
   * failures. These help the tests to run faster, and avoid clogging up the
   * python test runner process.
   */

  assertOk(value, message) {
    if (!value || TEST_DEBUG) {
      Assert.ok(value, message);
    }
  }

  assertEqual(actual, expected, message) {
    if (actual != expected || TEST_DEBUG) {
      Assert.equal(actual, expected, message);
    }
  }

  assertDeepEqual(actual, expected, message) {
    if (!ObjectUtils.deepEqual(actual, expected)) {
      Assert.deepEqual(actual, expected, message);
    }
  }
}

async function checkUISchemaValid(configSchema, uiSchema) {
  for (let key of Object.keys(configSchema.properties)) {
    Assert.ok(
      uiSchema["ui:order"].includes(key),
      `Should have ${key} listed at the top-level of the ui schema`
    );
  }
}
