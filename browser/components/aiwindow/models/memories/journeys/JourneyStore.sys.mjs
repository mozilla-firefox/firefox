/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { JSONFile } from "resource://gre/modules/JSONFile.sys.mjs";

const JOURNEY_STORE_FILE = "journeys.json.lz4";
const JOURNEY_STORE_VERSION = 1;

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "JourneyStore",
    maxLogLevelPref: "browser.smartwindow.memoriesLogLevel",
  })
);

/**
 * @typedef {object} JourneyMetadata
 * @property {string} memory_id - Nonblank ID of related memory.
 * @property {string} headline - Nonblank LLM-generated summary of the journey.
 * @property {string} status - Nonblank LLM-generated description of progress.
 * @property {boolean} dismissed - Permanent dismissal; defaults to false.
 * @property {number|null} dismissed_until - Nonnegative safe integer epoch
 *   milliseconds; defaults to null. Retained after the dismissal expires.
 */

const nonblankString = value =>
  typeof value === "string" && !!value.trim().length;

const FIELD_VALIDATORS = new Map([
  ["headline", nonblankString],
  ["status", nonblankString],
  ["dismissed", value => typeof value === "boolean"],
  [
    "dismissed_until",
    value => value === null || (Number.isSafeInteger(value) && value >= 0),
  ],
]);

/**
 * Returns a validated copy containing only supported fields, or null for an
 * invalid record.
 *
 * @param {object} record - Record as parsed from the file.
 * @returns {JourneyMetadata|null}
 */
function sanitizeRecord(record) {
  if (!nonblankString(record?.memory_id)) {
    return null;
  }
  const clean = {
    memory_id: record.memory_id,
    dismissed: false,
    dismissed_until: null,
  };
  for (const [field, validate] of FIELD_VALIDATORS) {
    if (Object.hasOwn(record, field)) {
      if (!validate(record[field])) {
        return null;
      }
      clean[field] = record[field];
    }
  }
  return clean.headline && clean.status ? clean : null;
}

let gFile;
let gInitPromise;

function ensureInitialized() {
  if (!gInitPromise) {
    gFile = new JSONFile({
      path: PathUtils.join(
        PathUtils.profileDir,
        "memories",
        JOURNEY_STORE_FILE
      ),
      compression: "lz4",
      saveDelayMs: 1000,
      sanitizedBasename: "journeys",
      saveFailureHandler: () => {
        lazy.console.error("Failed to save metadata");
      },
      dataPostProcessor: data => ({
        version: JOURNEY_STORE_VERSION,
        journeys_metadata:
          data?.version === JOURNEY_STORE_VERSION &&
          Array.isArray(data.journeys_metadata)
            ? data.journeys_metadata.map(sanitizeRecord).filter(Boolean)
            : [],
      }),
    });
    gInitPromise = IOUtils.makeDirectory(PathUtils.parent(gFile.path)).then(
      () => gFile.load()
    );
  }
  return gInitPromise;
}

/**
 * Stores journey metadata independently of memories. Suppressing a journey does
 * not delete its related memory. JourneyManager is responsible for removing
 * journey metadata when the related memory is no longer present. The store returns
 * suppressed records; callers are responsible for filtering them.
 *
 * Data loads on first use. Mutations schedule a save and return without awaiting
 * the write.
 */
export const JourneyStore = {
  /**
   * @param {string} memoryId - Corresponding memory ID.
   * @returns {Promise<JourneyMetadata|null>} Independent copy, or null if absent.
   */
  async get(memoryId) {
    await ensureInitialized();
    const record = gFile.data.journeys_metadata.find(
      metadata => metadata.memory_id === memoryId
    );
    return record ? { ...record } : null;
  },

  /**
   * @returns {Promise<JourneyMetadata[]>} Independent copies of all records,
   *   including permanently and temporarily suppressed records, in unspecified
   *   order.
   */
  async getAll() {
    await ensureInitialized();
    return gFile.data.journeys_metadata.map(record => ({ ...record }));
  },

  /**
   * Creates or partially updates metadata. Omitted fields retain their values.
   * Creating a record requires headline and status.
   *
   * @param {object} journey - Memory ID and metadata fields to update.
   * @param {string} journey.memoryId - Nonblank corresponding memory ID.
   * @param {string} [journey.headline] - Nonblank headline, preserved verbatim.
   * @param {string} [journey.status] - Nonblank status, preserved verbatim.
   * @param {boolean} [journey.dismissed] - Permanent dismissal.
   * @param {number|null} [journey.dismissed_until] - Timed dismissal timestamp.
   * @returns {Promise<boolean>} True when created or updated; false if a new
   *   record lacks headline or status.
   * @throws {TypeError} Rejects invalid values, explicit undefined, or unknown
   *   fields without changing state.
   */
  async addOrUpdateJourney(journey) {
    if (!journey || typeof journey !== "object" || Array.isArray(journey)) {
      throw new TypeError("journey must be an object");
    }
    const { memoryId, ...fields } = journey;
    if (!nonblankString(memoryId)) {
      throw new TypeError("memoryId must be a nonblank string");
    }
    // Validate all supplied fields before changing stored metadata.
    for (const [field, value] of Object.entries(fields)) {
      if (!FIELD_VALIDATORS.get(field)?.(value)) {
        throw new TypeError(`Invalid metadata field: ${String(field)}`);
      }
    }

    await ensureInitialized();
    const records = gFile.data.journeys_metadata;
    const index = records.findIndex(record => record.memory_id === memoryId);
    // New records require both headline and status; updates may omit either.
    if (
      index === -1 &&
      (!Object.hasOwn(fields, "headline") || !Object.hasOwn(fields, "status"))
    ) {
      return false;
    }
    const record = {
      memory_id: memoryId,
      dismissed: false,
      dismissed_until: null,
      ...records[index],
      ...fields,
    };
    if (index === -1) {
      records.push(record);
    } else {
      records[index] = record;
    }
    gFile.saveSoon();
    return true;
  },

  /**
   * @param {string} memoryId - Corresponding memory ID.
   * @returns {Promise<boolean>} True if a record was removed.
   */
  async deleteJourney(memoryId) {
    await ensureInitialized();
    const records = gFile.data.journeys_metadata;
    const index = records.findIndex(record => record.memory_id === memoryId);
    if (index === -1) {
      return false;
    }
    records.splice(index, 1);
    gFile.saveSoon();
    return true;
  },

  /**
   * Remove all journey metadata.
   *
   * @returns {Promise<void>}
   */
  async clear() {
    await ensureInitialized();
    gFile.data.journeys_metadata = [];
    gFile.saveSoon();
  },

  /**
   * Finish pending writes and discard cached state. The next operation loads
   * from the profile path. Tests must await all store operations before resetting.
   *
   * @returns {Promise<void>}
   */
  async testOnlyFlushAndResetCache() {
    await gInitPromise;
    await gFile?.finalize();
    gFile = null;
    gInitPromise = null;
  },
};
