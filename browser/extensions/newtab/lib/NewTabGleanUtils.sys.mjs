/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "NewTabGleanUtils",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.newtab.builtin-addon.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

/**
 * Module for managing Glean telemetry metrics and pings in the New Tab page.
 * This object provides functionality to:
 * - Read and parse JSON configuration files containing metrics and ping definitions
 * - Register metrics and pings at runtime
 * - Convert between different naming conventions (dotted snake case, kebab case, camel case)
 * - Handle metric and ping registration with proper error handling and logging
 */
export const NewTabGleanUtils = {
  /**
   * Fetches and parses a JSON file from a given resource URI.
   *
   * @param {string} resourceURI - The URI of the JSON file to fetch and parse
   * @returns {Promise<Object>} A promise that resolves to the parsed JSON object
   */
  async readJSON(resourceURI) {
    let result = await fetch(resourceURI);
    return result.json();
  },
  /**
   * Processes and registers Glean metrics and pings from a JSON configuration file.
   * This method performs two main operations:
   * 1. Registers all pings defined in the configuration
   * 2. Registers all metrics under their respective categories
   * Example: await NewTabGleanUtils.registerMetricsAndPings("resource://path/to/metrics.json");
   *
   * @param {string} resourceURI - The URI of the JSON file containing metrics and pings definitions
   * @returns {Promise<boolean>} A promise that resolves when all metrics and pings are registered
   * If a metric or ping registration fails, all further registration halts and this Promise
   * will still resolve (errors will be logged to the console).
   */
  async registerMetricsAndPings(resourceURI) {
    try {
      const data = await this.readJSON(resourceURI);

      // Check if data exists and has either metrics or pings to register
      if (!data || (!data.metrics && !data.pings)) {
        lazy.logConsole.log("No metrics or pings found in the JSON file");
        return false;
      }

      // First register all pings from the JSON file
      if (data.pings) {
        for (const [pingName, pingConfig] of Object.entries(data.pings)) {
          await this.registerPingIfNeeded({
            name: pingName,
            ...pingConfig,
          });
        }
      }

      // Then register all metrics under their respective categories
      if (data.metrics) {
        for (const [category, metrics] of Object.entries(data.metrics)) {
          for (const [name, config] of Object.entries(metrics)) {
            await this.registerMetricIfNeeded({
              ...config,
              category,
              name,
            });
          }
        }
      }
      lazy.logConsole.debug(
        "Successfully registered metrics and pings found in the JSON file"
      );
      return true;
    } catch (e) {
      lazy.logConsole.error(
        "Failed to complete registration of metrics and pings found in runtime metrics JSON:",
        e
      );
      return false;
    }
  },

  /**
   * Registers a metric in Glean if it doesn't already exist.
   * @param {Object} options - The metric configuration options
   * @param {string} options.type - The type of metric (e.g., "text", "counter")
   * @param {string} options.category - The category the metric belongs to
   * @param {string} options.name - The name of the metric
   * @param {string[]} options.pings - Array of ping names this metric belongs to
   * @param {string} options.lifetime - The lifetime of the metric
   * @param {boolean} [options.disabled] - Whether the metric is disabled
   * @param {Object} [options.extraArgs] - Additional arguments for the metric
   * @throws {Error} If a new metrics registration fails and error will be logged in console
   */
  registerMetricIfNeeded(options) {
    const { type, category, name, pings, lifetime, disabled, extraArgs } =
      options;

    // Glean metric to record the success of metric registration for telemetry purposes.
    let gleanSuccessMetric = Glean.newtab.metricRegistered[name];

    try {
      let categoryName = this.dottedSnakeToCamel(category);
      let metricName = this.dottedSnakeToCamel(name);

      if (categoryName in Glean && metricName in Glean[categoryName]) {
        lazy.logConsole.warn(
          `Fail to register metric ${name} in category ${category} as it already exists`
        );
        return;
      }

      // Convert extraArgs to JSON string for metrics type event
      let extraArgsJson = null;
      if (type === "event" && extraArgs && Object.keys(extraArgs).length) {
        extraArgsJson = JSON.stringify(extraArgs);
      }

      // Metric doesn't exist, register it
      lazy.logConsole.debug(`Registering metric ${name} at runtime`);

      // Register the metric
      Services.fog.registerRuntimeMetric(
        type,
        category,
        name,
        pings,
        `"${lifetime}"`,
        disabled,
        extraArgsJson
      );
      gleanSuccessMetric.set(true);
    } catch (e) {
      gleanSuccessMetric.set(false);
      lazy.logConsole.error(`Error registering metric ${name}: ${e}`);
      throw new Error(`Failure while registering metrics ${name} `);
    }
  },

  /**
   * Registers a ping in Glean if it doesn't already exist.
   * @param {Object} options - The ping configuration options
   * @param {string} options.name - The name of the ping
   * @param {boolean} [options.includeClientId] - Whether to include client ID
   * @param {boolean} [options.sendIfEmpty] - Whether to send ping if empty
   * @param {boolean} [options.preciseTimestamps] - Whether to use precise timestamps
   * @param {boolean} [options.includeInfoSections] - Whether to include info sections
   * @param {boolean} [options.enabled] - Whether the ping is enabled
   * @param {string[]} [options.schedulesPings] - Array of scheduled ping times
   * @param {string[]} [options.reasonCodes] - Array of valid reason codes
   * @param {boolean} [options.followsCollectionEnabled] - Whether ping follows collection enabled state
   * @param {string[]} [options.uploaderCapabilities] - Array of uploader capabilities for this ping
   * @throws {Error} If a new ping registration fails and error will be logged in console
   */
  registerPingIfNeeded(options) {
    const {
      name,
      includeClientId,
      sendIfEmpty,
      preciseTimestamps,
      includeInfoSections,
      enabled,
      schedulesPings,
      reasonCodes,
      followsCollectionEnabled,
      uploaderCapabilities,
    } = options;

    // Glean metric to record the success of ping registration for telemetry purposes.
    let gleanSuccessPing = Glean.newtab.pingRegistered[name];
    try {
      let pingName = this.kebabToCamel(name);
      if (pingName in GleanPings) {
        lazy.logConsole.warn(
          `Fail to register ping ${name} as it already exists`
        );
        return;
      }

      // Ping doesn't exist, register it
      lazy.logConsole.debug(`Registering ping ${name} at runtime`);

      Services.fog.registerRuntimePing(
        name,
        includeClientId,
        sendIfEmpty,
        preciseTimestamps,
        includeInfoSections,
        enabled,
        schedulesPings,
        reasonCodes,
        followsCollectionEnabled,
        uploaderCapabilities
      );
      gleanSuccessPing.set(true);
    } catch (e) {
      gleanSuccessPing.set(false);
      lazy.logConsole.error(`Error registering ping ${name}: ${e}`);
      throw new Error(`Failure while registering ping ${name} `);
    }
  },

  /**
   * Converts a dotted snake case string to camel case.
   * Example: "foo.bar_baz" becomes "fooBarBaz"
   * @param {string} metricNameOrCategory - The string in dotted snake case format
   * @returns {string} The converted camel case string
   */
  dottedSnakeToCamel(metricNameOrCategory) {
    if (!metricNameOrCategory) {
      return "";
    }

    let camel = "";
    // Split by underscore and then by dots
    const segments = metricNameOrCategory.split("_");
    for (const segment of segments) {
      const parts = segment.split(".");
      for (const part of parts) {
        if (!camel) {
          camel += part;
        } else if (part.length) {
          const firstChar = part.charAt(0);
          if (firstChar >= "a" && firstChar <= "z") {
            // Capitalize first letter and append rest of the string
            camel += firstChar.toUpperCase() + part.slice(1);
          } else {
            // If first char is not a-z, append as is
            camel += part;
          }
        }
      }
    }
    return camel;
  },

  /**
   * Converts a kebab case string to camel case.
   * Example: "foo-bar-baz" becomes "fooBarBaz"
   * @param {string} pingName - The string in kebab case format
   * @returns {string} The converted camel case string
   */
  kebabToCamel(pingName) {
    if (!pingName) {
      return "";
    }

    let camel = "";
    // Split by hyphens
    const segments = pingName.split("-");
    for (const segment of segments) {
      if (!camel) {
        camel += segment;
      } else if (segment.length) {
        const firstChar = segment.charAt(0);
        if (firstChar >= "a" && firstChar <= "z") {
          // Capitalize first letter and append rest of the string
          camel += firstChar.toUpperCase() + segment.slice(1);
        } else {
          // If first char is not a-z, append as is
          camel += segment;
        }
      }
    }
    return camel;
  },
};
