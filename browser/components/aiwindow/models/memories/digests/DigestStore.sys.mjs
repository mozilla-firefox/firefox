/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  DIGEST_SCHEDULE_TYPE,
  DIGEST_STATUS,
} from "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs";
import { DIGEST_FILTERS } from "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStoreConstants.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "DigestStore",
    maxLogLevelPref: "browser.smartwindow.memoriesLogLevel",
  })
);

const DIGEST_STORE_FILE = "digests.json.lz4";
const DIGEST_STORE_VERSION = 1;

let gFile;
let gInitPromise;

/**
 * DigestSchedule schema
 *
 * Daily and weekly schedules use the serialized shape accepted by agents'
 * Schedule.fromJSON.
 *
 * @typedef {object} DigestSchedule
 * @property {string} type - One of {@link DIGEST_SCHEDULE_TYPE}.
 * @property {number} [weekday] - Day of the week (0=Sunday, 6=Saturday). Weekly schedules only.
 * @property {number} [hour] - Hour of the day (0-23). Daily and weekly schedules only.
 * @property {number} [minute] - Minute of the hour (0-59). Daily and weekly schedules only.
 */

/**
 * Main Digest schema
 *
 * @typedef {object} Digest
 * @property {string} id - UUID for the digest.
 * @property {string} status - One of {@link DIGEST_STATUS}.
 * @property {boolean} unread - Whether the user has not read the refreshed digest.
 * @property {string} memory_id - ID of the associated memory in MemoryStore.
 * @property {string|null} ai_tab_slug - Slug of the associated AI tab page, or null if the digest has no AI tab yet.
 * @property {string} title - Title of the digest.
 * @property {string} description - Description of the digest.
 * @property {string[]} domains - Domains the digest draws from; empty if none.
 * @property {DigestSchedule} schedule - When the digest should run.
 * @property {number|null} last_run_time - When the digest last ran, in milliseconds since Unix epoch or null if it has never run.
 * @property {number|null} last_read_time - When unread last changed from true to false, in milliseconds since Unix epoch or null if the user has never read the digest.
 * @property {number|null} next_run_time - When the digest is next due to run, in milliseconds since Unix epoch or null if it has no scheduled run.
 * @property {number} created_at - When the digest was created, in milliseconds since Unix epoch.
 * @property {number} updated_at - When the digest was last updated, in milliseconds since Unix epoch.
 */

// Reusable field validators
const nonblankString = value =>
  typeof value === "string" && !!value.trim().length;
const timestamp = value => Number.isSafeInteger(value) && value >= 0;
const clockHour = value => Number.isInteger(value) && value >= 0 && value <= 23;
const clockMinute = value =>
  Number.isInteger(value) && value >= 0 && value <= 59;
const weekday = value => Number.isInteger(value) && value >= 0 && value <= 6;

// Validator configs for DigestSchedule per schedule type
const SCHEDULE_FIELD_VALIDATORS = new Map([
  [DIGEST_SCHEDULE_TYPE.DAILY, { hour: clockHour, minute: clockMinute }],
  [
    DIGEST_SCHEDULE_TYPE.WEEKLY,
    { weekday, hour: clockHour, minute: clockMinute },
  ],
  [DIGEST_SCHEDULE_TYPE.ON_DEMAND, {}],
]);

/**
 * Validates a Digest's schedule.
 *
 * @param {*} schedule - Schedule to check.
 * @returns {boolean} Whether schedule is a valid {@link DigestSchedule}.
 */
function isValidSchedule(schedule) {
  if (!schedule || typeof schedule !== "object" || Array.isArray(schedule)) {
    return false;
  }
  const { type, ...scheduleFields } = schedule;
  const fieldValidators = SCHEDULE_FIELD_VALIDATORS.get(type);
  if (!fieldValidators) {
    return false;
  }

  const requiredFields = Object.keys(fieldValidators);
  return (
    Object.keys(scheduleFields).length === requiredFields.length &&
    requiredFields.every(
      field =>
        Object.hasOwn(scheduleFields, field) &&
        fieldValidators[field](scheduleFields[field])
    )
  );
}

// Validator config for top-level digest structure
const FIELD_VALIDATORS = new Map([
  ["id", nonblankString],
  ["status", value => Object.values(DIGEST_STATUS).includes(value)],
  ["unread", value => typeof value === "boolean"],
  ["memory_id", nonblankString],
  ["ai_tab_slug", value => value === null || nonblankString(value)],
  ["title", nonblankString],
  ["description", nonblankString],
  ["domains", value => Array.isArray(value) && value.every(nonblankString)],
  ["schedule", isValidSchedule],
  ["last_run_time", value => value === null || timestamp(value)],
  ["last_read_time", value => value === null || timestamp(value)],
  ["next_run_time", value => value === null || timestamp(value)],
  ["created_at", timestamp],
  ["updated_at", timestamp],
]);

// Fields addDigest requires when creating a new digest
const REQUIRED_FIELDS = ["memory_id", "title", "description", "schedule"];

/**
 * Throws if digestPartial is not an object or has an unknown or invalid field.
 *
 * @param {object} digestPartial
 * @throws {TypeError}
 */
function validateDigestPartial(digestPartial) {
  if (
    !digestPartial ||
    typeof digestPartial !== "object" ||
    Array.isArray(digestPartial)
  ) {
    throw new TypeError("digest must be an object");
  }
  for (const [field, value] of Object.entries(digestPartial)) {
    if (!FIELD_VALIDATORS.get(field)?.(value)) {
      throw new TypeError(`Invalid digest field: ${String(field)}`);
    }
  }
}

/**
 * Throws if the getDigests options are malformed or a filter is unsupported.
 *
 * @param {Set<string>} digestIds
 * @param {Array<object>} attributeFilters
 * @throws {TypeError}
 */
function validateGetDigestsOptions(digestIds, attributeFilters) {
  // `instanceof Set` fails for a Set created in another global, such as a
  // window, so check for the Set methods getDigests uses instead.
  if (
    typeof digestIds?.has !== "function" ||
    typeof digestIds.size !== "number"
  ) {
    throw new TypeError("digestIds must be a Set");
  }
  if (!Array.isArray(attributeFilters)) {
    throw new TypeError("attributeFilters must be an array");
  }
  for (const filter of attributeFilters) {
    if (!filter || typeof filter !== "object" || Array.isArray(filter)) {
      throw new TypeError("Each attribute filter must be an object");
    }
    const { field, comparator, value } = filter;
    const definition = DIGEST_FILTERS.get(comparator);
    if (!definition) {
      throw new TypeError(
        `Unsupported filter comparator: ${String(comparator)}`
      );
    }
    if (!definition.fields.includes(field)) {
      throw new TypeError(
        `Unsupported field for comparator ${comparator}: ${String(field)}`
      );
    }
    if (!definition.isValidValue(value)) {
      throw new TypeError(
        `Invalid value for comparator ${comparator}: ${String(value)}`
      );
    }
  }
}

/**
 * Resolves a filter field against a digest. Plain fields return
 * `digest[field]`; "schedule.type" returns the schedule's type.
 *
 * @param {Digest} digest
 * @param {string} field
 * @returns {*}
 */
function resolveFilterField(digest, field) {
  if (field === "schedule.type") {
    return digest.schedule.type;
  }
  return digest[field];
}

/**
 * Returns a copy of validated digest fields to store.
 *
 * This includes additional processing for some fields:
 * - Duplicated domains
 *
 * @param {object} fields - Digest fields that passed validateDigestPartial.
 * @returns {object}
 */
function prepareFieldsToStore(fields) {
  const prepared = structuredClone(fields);
  if (prepared.domains) {
    prepared.domains = [...new Set(prepared.domains)];
  }
  return prepared;
}

function makeEmptyState() {
  return { version: DIGEST_STORE_VERSION, digests: [] };
}

/**
 * Returns a validated copy containing only supported fields or null for an
 * invalid digest.
 *
 * @param {*} record - Digest as parsed from the file.
 * @returns {Digest|null}
 */
function sanitizeDigest(record) {
  if (!record || typeof record !== "object" || Array.isArray(record)) {
    return null;
  }
  const clean = {};
  for (const [field, validate] of FIELD_VALIDATORS) {
    if (!Object.hasOwn(record, field) || !validate(record[field])) {
      return null;
    }
    clean[field] = record[field];
  }
  return clean;
}

/**
 * Cleans up data loaded from the file. An unsupported version or shape loads
 * as an empty store. Otherwise invalid digests, and every valid digest after
 * the first with the same id, are dropped.
 *
 * @param {object} data - Data as parsed from the file.
 * @returns {object} State to load.
 */
function sanitizeLoadedState(data) {
  // Unsupported version or digests is not an array
  if (data?.version !== DIGEST_STORE_VERSION || !Array.isArray(data.digests)) {
    return makeEmptyState();
  }

  const seenIds = new Set();
  const digests = [];
  let invalidCount = 0;
  let duplicateCount = 0;
  for (const digest of data.digests.map(sanitizeDigest)) {
    if (!digest) {
      invalidCount++;
    } else if (seenIds.has(digest.id)) {
      // Drop digests with the same ID as one that was already loaded
      duplicateCount++;
    } else {
      seenIds.add(digest.id);
      digests.push(digest);
    }
  }

  // Log errors
  if (invalidCount || duplicateCount) {
    lazy.console.warn(
      `Dropped ${invalidCount} invalid and ${duplicateCount} duplicate digests while loading`
    );
  }
  return { version: DIGEST_STORE_VERSION, digests };
}

/**
 * Loads digest data from disk into gFile.
 * Drops invalid data (see {@link sanitizeLoadedState})
 *
 * @returns {Promise<void>}
 */
async function loadDigests() {
  gFile = new lazy.JSONFile({
    path: PathUtils.join(PathUtils.profileDir, "memories", DIGEST_STORE_FILE),
    compression: "lz4",
    saveDelayMs: 1000,
    sanitizedBasename: "digests",
    saveFailureHandler: () => {
      lazy.console.error("Failed to save digests");
    },
    dataPostProcessor: sanitizeLoadedState,
  });

  try {
    await IOUtils.makeDirectory(PathUtils.parent(gFile.path));
    await gFile.load();
  } catch (ex) {
    lazy.console.error("Failed to load digests", ex);
    gFile.data = makeEmptyState();
  }
}

/**
 * Stores digests in the profile's memories directory.
 *
 * Data loads on first use. Mutations schedule a save and return without
 * awaiting the write.
 */
export const DigestStore = {
  /**
   * Set up the JSONFile and load from disk.
   *
   * @returns {Promise<void>}
   */
  async ensureInitialized() {
    if (!gInitPromise) {
      gInitPromise = loadDigests();
    }
    await gInitPromise;
  },

  /**
   * Request a debounced write of the current in-memory state to disk.
   *
   * @returns {Promise<void>}
   */
  async requestSave() {
    await this.ensureInitialized();
    gFile.saveSoon();
  },

  /**
   * Add a new digest, or update an existing one with the same id.
   *
   * A new digest requires memory_id, title, description, and schedule.
   * Omitted fields that aren't required default to:
   * - A generated id
   * - "suggested" status
   * - unread to false
   * - no associated AI tab
   * - empty domains array
   * - never run
   * - never read
   * - not scheduled
   * - created now
   * - updated now
   *
   * @param {object} digestPartial - Digest fields; see {@link Digest}.
   * @returns {Promise<Digest>} Independent copy of the stored digest.
   * @throws {TypeError} Rejects non-objects, unknown or invalid fields, and new
   *   digests missing a required field, without changing state.
   */
  async addDigest(digestPartial) {
    // Validate the digest first
    validateDigestPartial(digestPartial);
    await this.ensureInitialized();

    // Check if the digest is new or already exists using its ID
    const { id, ...fields } = digestPartial;
    if (id && gFile.data.digests.some(digest => digest.id === id)) {
      return this.updateDigest(id, fields);
    }

    // Check for missing required fields
    const missing = REQUIRED_FIELDS.find(
      field => !Object.hasOwn(fields, field)
    );
    if (missing) {
      throw new TypeError(`Missing required digest field: ${missing}`);
    }

    // Fill out the digest with defaults or provided values
    const now = Date.now();
    const digest = {
      id: id ?? crypto.randomUUID(),
      status: DIGEST_STATUS.SUGGESTED,
      unread: false,
      ai_tab_slug: null,
      domains: [],
      last_run_time: null,
      last_read_time: null,
      next_run_time: null,
      created_at: now,
      updated_at: now,
      ...prepareFieldsToStore(fields),
    };
    gFile.data.digests.push(digest);
    gFile.saveSoon();
    return structuredClone(digest);
  },

  /**
   * Update an existing digest by id.
   *
   * Omitted fields keep their existing saved values.
   * A new schedule completely replaces the existing one.
   * updated_at defaults to now.
   * last_read_time defaults to now when unread changes from true to false.
   *
   * @param {string} id - Digest ID
   * @param {object} updates - Digest fields other than id; see {@link Digest}.
   * @returns {Promise<Digest|null>} Independent copy of the updated digest or
   *   null if no digest has that id.
   * @throws {TypeError} Rejects non-objects, an id field, and unknown or
   *   invalid fields without changing state.
   */
  async updateDigest(id, updates) {
    // Validate the updates first
    validateDigestPartial(updates);

    // Throw if updates try to change the ID
    if (Object.hasOwn(updates, "id")) {
      throw new TypeError("A digest's id cannot be updated");
    }
    await this.ensureInitialized();

    // Check if the requested digest exists
    const digest = gFile.data.digests.find(d => d.id === id);
    if (!digest) {
      return null;
    }

    // Update the digest and return
    const now = Date.now();
    const defaults = { updated_at: now };
    if (digest.unread && updates.unread === false) {
      defaults.last_read_time = now;
    }
    Object.assign(digest, defaults, prepareFieldsToStore(updates));
    gFile.saveSoon();
    return structuredClone(digest);
  },

  /**
   * Delete a digest by ID.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<boolean>} True if a digest was removed.
   */
  async deleteDigest(id) {
    await this.ensureInitialized();
    const digests = gFile.data.digests;
    const index = digests.findIndex(digest => digest.id === id);

    // Return false if the requested digest doesn't exist
    if (index === -1) {
      return false;
    }

    digests.splice(index, 1);
    gFile.saveSoon();
    return true;
  },

  /**
   * Get all digests, optionally filtered.
   *
   * @param {object} [options]
   * @param {Set<string>} [options.digestIds=new Set()]
   *   Optional set of digest IDs; returns all digests if the set is empty.
   * @param {Array<object>} [options.attributeFilters=[]]
   *   Optional filters a digest must all match, each containing:
   *    1. field: The digest field the filter applies to.
   *    2. comparator: A comparator name from MEMORY_FILTER_COMPARATOR.
   *    3. value: The value to compare the field against.
   * @returns {Promise<Digest[]>} Independent copies of the matching digests,
   *   in stored order.
   * @throws {TypeError} Rejects a digestIds that is not a Set, an
   *   attributeFilters that is not an array, filters with an unsupported
   *   comparator, a field the comparator does not accept, or an invalid value.
   */
  async getDigests({ digestIds = new Set(), attributeFilters = [] } = {}) {
    // Validate the filtering options first
    validateGetDigestsOptions(digestIds, attributeFilters);
    await this.ensureInitialized();

    let digests = gFile.data.digests;

    // Filter on IDs first if digestIds is populated
    if (digestIds.size) {
      digests = digests.filter(digest => digestIds.has(digest.id));
    }

    // Filter on attributeFilters second
    for (const { field, comparator, value } of attributeFilters) {
      const { predicate } = DIGEST_FILTERS.get(comparator);
      digests = digests.filter(digest =>
        predicate(resolveFilterField(digest, field), value)
      );
    }

    // Return a copy of the resulting digest set
    return digests.map(digest => structuredClone(digest));
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
