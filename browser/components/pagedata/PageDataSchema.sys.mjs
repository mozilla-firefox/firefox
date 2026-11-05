/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  JsonSchemaValidator:
    "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs",
  OpenGraphPageData:
    "moz-src:///browser/components/pagedata/OpenGraphPageData.sys.mjs",
  SchemaOrgPageData:
    "moz-src:///browser/components/pagedata/SchemaOrgPageData.sys.mjs",
  TwitterPageData:
    "moz-src:///browser/components/pagedata/TwitterPageData.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "PageData",
    maxLogLevel: Services.prefs.getBoolPref("browser.pagedata.log", false)
      ? "Debug"
      : "Warn",
  });
});

/**
 * The list of page data collectors. These should be sorted in order of
 * specificity, if the same piece of data is provided by two collectors then the
 * earlier wins.
 *
 * Collectors must provide a `collect` function which will be passed the
 * document object and should return the PageData structure. The function may be
 * asynchronous if needed.
 *
 * The data returned need not be valid, collectors should return whatever they
 * can and then we drop anything that is invalid once all data is joined.
 */
ChromeUtils.defineLazyGetter(lazy, "DATA_COLLECTORS", function () {
  return [lazy.SchemaOrgPageData, lazy.OpenGraphPageData, lazy.TwitterPageData];
});

let SCHEMAS = new Map();

/**
 * Loads the schema for the given name.
 *
 * @param {string} schemaName
 *   The name of the schema to load.
 * @returns {object}
 *   The loaded schema.
 */
async function loadSchema(schemaName) {
  if (SCHEMAS.has(schemaName)) {
    return SCHEMAS.get(schemaName);
  }

  let url = `chrome://browser/content/pagedata/schemas/${schemaName.toLocaleLowerCase()}.schema.json`;
  let response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to load schema: ${response.statusText}`);
  }

  let schema = await response.json();
  SCHEMAS.set(schemaName, schema);
  return schema;
}

/**
 * Validates the data using the schema with the given name.
 *
 * @param {string} schemaName
 *   The name of the schema to validate against.
 * @param {object} data
 *   The data to validate.
 */
async function validateData(schemaName, data) {
  let schema = await loadSchema(schemaName.toLocaleLowerCase());

  let result = lazy.JsonSchemaValidator.validate(data, schema, {
    allowExplicitUndefinedProperties: true,
    // Allowed for future expansion of the schema.
    allowAdditionalProperties: true,
  });

  if (!result.valid) {
    throw result.error;
  }
}

/**
 * A shared API that can be used in parent or child processes
 */
export const PageDataSchema = {
  // Enumeration of data types. The keys must match the schema name.
  DATA_TYPE: Object.freeze({
    // Note that 1 and 2 were used as types in earlier versions and should not be used here.
    PRODUCT: 3,
    DOCUMENT: 4,
    ARTICLE: 5,
    AUDIO: 6,
    VIDEO: 7,
  }),

  /**
   * Gets the data type name.
   *
   * @param {DATA_TYPE} type
   *   The data type from the DATA_TYPE enumeration
   *
   * @returns {string | null} The name for the type or null if not found.
   */
  nameForType(type) {
    for (let [name, value] of Object.entries(this.DATA_TYPE)) {
      if (value == type) {
        return name;
      }
    }

    return null;
  },

  /**
   * Asynchronously validates some page data against the expected schema. Throws
   * an exception if validation fails.
   *
   * @param {DATA_TYPE} type
   *   The data type from the DATA_TYPE enumeration
   * @param {object} data
   *   The page data
   */
  async validateData(type, data) {
    let name = this.nameForType(type);

    if (!name) {
      throw new Error(`Unknown data type ${type}`);
    }

    await validateData(name, data);
  },

  /**
   * Asynchronously validates an entire PageData structure. Any invalid or
   * unknown data types are dropped.
   *
   * @param {PageData} pageData
   *   The page data
   *
   * @returns {PageData} The validated page data structure
   */
  async validatePageData(pageData) {
    let { data: dataMap = {}, ...general } = pageData;

    await validateData("general", general);

    let validData = {};

    for (let [type, data] of Object.entries(dataMap)) {
      let name = this.nameForType(type);
      // Ignore unknown types here.
      if (!name) {
        continue;
      }

      try {
        await validateData(name, data);

        validData[type] = data;
      } catch (e) {
        // Invalid data is dropped.
      }
    }

    return {
      ...general,
      data: validData,
    };
  },

  /**
   * Adds new page data into an existing data set. Any existing data is not
   * overwritten.
   *
   * @param {PageData} existingPageData
   *   The existing page data
   * @param {PageData} newPageData
   *   The new page data
   *
   * @returns {PageData} The joined data.
   */
  coalescePageData(existingPageData, newPageData) {
    // Split out the general data from the map of specific data.
    let { data: existingMap = {}, ...existingGeneral } = existingPageData;
    let { data: newMap = {}, ...newGeneral } = newPageData;

    Object.assign(newGeneral, existingGeneral);

    let dataMap = {};
    for (let [type, data] of Object.entries(existingMap)) {
      if (type in newMap) {
        dataMap[type] = Object.assign({}, newMap[type], data);
      } else {
        dataMap[type] = data;
      }
    }

    for (let [type, data] of Object.entries(newMap)) {
      if (!(type in dataMap)) {
        dataMap[type] = data;
      }
    }

    return {
      ...newGeneral,
      data: dataMap,
    };
  },

  /**
   * Collects page data from a DOM document.
   *
   * @param {Document} document
   *   The DOM document to collect data from
   *
   * @returns {Promise<PageData | null>} The data collected or null in case of
   *   error.
   */
  async collectPageData(document) {
    lazy.logConsole.debug("Starting collection", document.documentURI);

    let pending = lazy.DATA_COLLECTORS.map(async collector => {
      try {
        return await collector.collect(document);
      } catch (e) {
        lazy.logConsole.error("Error collecting page data", e);
        return null;
      }
    });

    let pageDataList = await Promise.all(pending);

    let pageData = pageDataList.reduce(PageDataSchema.coalescePageData, {
      date: Date.now(),
      url: document.documentURI,
    });

    try {
      return this.validatePageData(pageData);
    } catch (e) {
      lazy.logConsole.error("Failed to collect valid page data", e);
      return null;
    }
  },
};
