/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @typedef {import("./DigestStore.sys.mjs").Digest} Digest */
/** @typedef {import("./DigestStore.sys.mjs").DigestSchedule} DigestSchedule */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  DIGEST_SCHEDULE_TYPE:
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs",
  DIGEST_STATUS:
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs",
  DigestStore:
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs",
  isValidSchedule:
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs",
  MEMORY_FILTER_COMPARATOR:
    "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs",
  Schedule:
    "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs",
});

// Digest fields that can be directly edited in the configuration modal.
const EDITABLE_FIELDS = new Set([
  "title",
  "description",
  "schedule",
  "domains",
]);

/**
 * Throw if `fields` has a field that is not in `allowedFields`.
 *
 * @param {object} fields
 * @param {Set<string>} allowedFields
 * @throws {TypeError}
 */
function assertAllowedFields(fields, allowedFields) {
  const field = Object.keys(fields).find(f => !allowedFields.has(f));
  if (field !== undefined) {
    throw new TypeError(`Digest field cannot be set: ${field}`);
  }
}

/**
 * Whether two flat schedules have the same fields and values.
 *
 * @param {DigestSchedule} a
 * @param {DigestSchedule} b
 * @returns {boolean}
 */
function isSameSchedule(a, b) {
  const keys = Object.keys(a);
  return (
    keys.length === Object.keys(b ?? {}).length &&
    keys.every(key => a[key] === b?.[key])
  );
}

/**
 * Public API for digests used by external callers.
 */
export const DigestManager = {
  /**
   * Get all digests, optionally filtered.
   *
   * This passes directly through to DigestStore.getDigests and so has the same argument options.
   *
   * @param {object} [options]
   * @param {Set<string>} [options.digestIds=new Set()]
   *   Optional set of digest IDs; returns all digests if the set is empty.
   * @param {Array<object>} [options.attributeFilters=[]]
   *   Optional filters a digest must all match, each containing:
   *    1. field: The digest field the filter applies to.
   *    2. comparator: A comparator name from MEMORY_FILTER_COMPARATOR.
   *    3. value: The value to compare the field against.
   *   Supported comparators and fields:
   *    - EQUAL_TO: status, unread, memory_id, ai_tab_slug, schedule.type,
   *      last_run_time, last_read_time, next_run_time, created_at, and
   *      updated_at. The value must be a primitive other than undefined; use
   *      null to find an unset time or ai_tab_slug.
   *    - IN: status, unread, memory_id, ai_tab_slug, and schedule.type. The
   *      value must be an array.
   *    - GREATER_THAN, LESS_THAN, GREATER_THAN_OR_EQUAL_TO, and
   *      LESS_THAN_OR_EQUAL_TO: last_run_time, last_read_time, next_run_time,
   *      created_at, and updated_at. The value must be a finite number; a null
   *      time never matches.
   * @returns {Promise<Digest[]>} Independent copies of the matching digests,
   *   in stored order.
   * @throws {TypeError} Rejects a digestIds that is not a Set, an
   *   attributeFilters that is not an array, filters with an unsupported
   *   comparator, a field the comparator does not accept, or an invalid value.
   */
  async getDigests(options) {
    return lazy.DigestStore.getDigests(options);
  },

  /**
   * Return a digest for the requested ID.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<Digest|null>} The digest, or null if no digest has the requested id.
   */
  async getDigestById(id) {
    const [digest] = await lazy.DigestStore.getDigests({
      digestIds: new Set([id]),
    });
    return digest ?? null;
  },

  /**
   * Return all suggested-type digests.
   *
   * @returns {Promise<Digest[]>} Suggested digests, in stored order.
   */
  async getSuggestedDigests() {
    return lazy.DigestStore.getDigests({
      attributeFilters: [
        {
          field: "status",
          comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: lazy.DIGEST_STATUS.SUGGESTED,
        },
      ],
    });
  },

  /**
   * Return all enabled-type digests.
   *
   * @returns {Promise<Digest[]>} Enabled digests, in stored order.
   */
  async getEnabledDigests() {
    return lazy.DigestStore.getDigests({
      attributeFilters: [
        {
          field: "status",
          comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: lazy.DIGEST_STATUS.ENABLED,
        },
      ],
    });
  },

  /**
   * Return all enabled digests flagged as unread.
   *
   * @returns {Promise<Digest[]>} Unread enabled digests, in stored order.
   */
  async getUnreadDigests() {
    return lazy.DigestStore.getDigests({
      attributeFilters: [
        {
          field: "status",
          comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: lazy.DIGEST_STATUS.ENABLED,
        },
        {
          field: "unread",
          comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: true,
        },
      ],
    });
  },

  /**
   * Return all digests associated with a single memory ID.
   *
   * This may return multiple digests if their original memories merged into 1 over time.
   *
   * @param {string} memoryId - ID of the memory in MemoryStore.
   * @returns {Promise<Digest[]>} Digests of any status for that memory, in stored order.
   */
  async getDigestsByMemoryId(memoryId) {
    return lazy.DigestStore.getDigests({
      attributeFilters: [
        {
          field: "memory_id",
          comparator: lazy.MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: memoryId,
        },
      ],
    });
  },

  /**
   * Create a suggested digest or an enabled one with enableNow.
   *
   * @param {object} fields - memory_id, title, description, and schedule, and
   *   optionally domains; see {@link Digest}.
   * @param {object} [options]
   * @param {boolean} [options.enableNow=false] - Enable the digest and
   *   schedule its next run, as {@link DigestManager.enableDigest} does.
   * @returns {Promise<Digest>}
   * @throws {TypeError} Rejects any other field and invalid or missing fields.
   */
  async createDigest(fields, { enableNow = false } = {}) {
    const { memory_id, ...content } = fields;
    if (memory_id === undefined) {
      throw new TypeError("A digest requires a memory_id");
    }
    assertAllowedFields(content, EDITABLE_FIELDS);
    if (!enableNow) {
      return lazy.DigestStore.addDigest(fields);
    }
    return lazy.DigestStore.addDigest({
      ...fields,
      status: lazy.DIGEST_STATUS.ENABLED,
      next_run_time: this.computeNextRunTime(fields.schedule),
    });
  },

  /**
   * Edit a digest's content and schedule. Use the other helpers to change its
   * status or read state. Edits are validated together, so a rejected edit
   * changes nothing.
   *
   * @param {string} id - Digest ID
   * @param {object} edits - Any of title, description, schedule, and domains.
   * @returns {Promise<Digest|null>} The edited digest, or null if no digest
   *   has that id.
   * @throws {TypeError} Rejects any other field and invalid fields.
   */
  async editDigest(id, edits) {
    assertAllowedFields(edits, EDITABLE_FIELDS);
    const digest = await this.getDigestById(id);
    const updates = { ...edits };

    // Recompute next_run_time only for an enabled digest whose schedule changed
    if (
      digest?.status === lazy.DIGEST_STATUS.ENABLED &&
      Object.hasOwn(edits, "schedule") &&
      !isSameSchedule(digest.schedule, edits.schedule)
    ) {
      updates.next_run_time = this.computeNextRunTime(edits.schedule);
    }
    return lazy.DigestStore.updateDigest(id, updates);
  },

  /**
   * Delete a digest by ID.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<boolean>} True if a digest was removed.
   */
  async deleteDigest(id) {
    return lazy.DigestStore.deleteDigest(id);
  },

  /**
   * Enable a digest and schedule its next run after now.
   *
   * On-demand digests are left unscheduled because they do not have schedules.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<Digest|null>} The enabled digest, or null if no digest has that id.
   */
  async enableDigest(id) {
    const digest = await this.getDigestById(id);
    if (!digest) {
      return null;
    }
    return lazy.DigestStore.updateDigest(id, {
      status: lazy.DIGEST_STATUS.ENABLED,
      next_run_time: this.computeNextRunTime(digest.schedule),
    });
  },

  /**
   * Disable a digest and clear its next run.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<Digest|null>} The disabled digest, or null if no digest
   *   has that id.
   */
  async disableDigest(id) {
    return lazy.DigestStore.updateDigest(id, {
      status: lazy.DIGEST_STATUS.DISABLED,
      next_run_time: null,
    });
  },

  /**
   * Mark a digest as read.
   *
   * If it was unread, updateDigest will change last_read_time to now automatically.
   *
   * @param {string} id - Digest ID
   * @returns {Promise<Digest|null>} The read digest, or null if no digest has
   *   that id.
   */
  async markDigestRead(id) {
    return lazy.DigestStore.updateDigest(id, { unread: false });
  },

  /**
   * Return when a schedule next runs after a given time.
   *
   * @param {DigestSchedule} schedule
   * @param {number} [after] - Milliseconds since Unix epoch; defaults to now.
   * @returns {number|null} Milliseconds since Unix epoch, or null for an
   *   on-demand schedule.
   * @throws {TypeError} Rejects an invalid schedule.
   */
  computeNextRunTime(schedule, after = Date.now()) {
    if (!lazy.isValidSchedule(schedule)) {
      throw new TypeError("Invalid digest schedule");
    }
    if (schedule.type === lazy.DIGEST_SCHEDULE_TYPE.ON_DEMAND) {
      return null;
    }
    return lazy.Schedule.fromJSON(schedule).getNextRunTime(after).getTime();
  },
};
