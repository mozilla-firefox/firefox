/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import {OptInEntry} from "./lib/ExperimentManager.sys.mjs" */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  UnenrollmentCause: "resource://nimbus/lib/ExperimentManager.sys.mjs",

  log: () => {
    const { Logger } = ChromeUtils.importESModule(
      "resource://messaging-system/lib/Logger.sys.mjs"
    );

    return new Logger("FirefoxLabs");
  },
});

const IS_MAIN_PROCESS =
  Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

export class FirefoxLabs {
  /**
   * A map of experiment slugs to opt-in entries.
   *
   * @type {Map<string, OptInEntry>}
   */
  #optIns;

  /**
   * Construct a new FirefoxLabs instance from the given set of recipes.
   *
   * @param {OptInEntry[]} optIns The opt-ins.
   *
   * NB: You shiould use FirefoxLabs.create() directly instead of calling this constructor.
   */
  constructor(optIns) {
    this.#optIns = new Map(optIns.map(entry => [entry.recipe.slug, entry]));
  }

  /**
   * Create a new FirefoxLabs instance with all available opt-in recipes that match targeting and
   * bucketing.
   */
  static async create() {
    if (!IS_MAIN_PROCESS) {
      throw new Error("FirefoxLabs can only be created in the main process");
    }

    const entries = await lazy.ExperimentAPI.manager.getAvailableOptIns();
    return new FirefoxLabs(entries);
  }

  /**
   * Enroll in an opt-in.
   *
   * @param {string} slug The slug of the opt-in to enroll.
   * @param {string} branchSlug The slug of the branch to enroll in.
   * @param {object} options
   * @param {Map<string, unknown> | undefined} options.originalPrefValues
   */
  async enroll(slug, branchSlug, { originalPrefValues } = {}) {
    if (!slug || !branchSlug) {
      throw new TypeError("enroll: slug and branchSlug are required");
    }

    const entry = this.#optIns.get(slug);
    if (!entry) {
      lazy.log.error(`No recipe found with id ${slug}`);
      return;
    }

    const { recipe, source } = entry;

    if (!recipe.branches.find(branch => branch.slug === branchSlug)) {
      lazy.log.error(
        `Failed to enroll in ${slug} ${branchSlug}: branch does not exist`
      );
      return;
    }

    try {
      await lazy.ExperimentAPI.manager.enroll(recipe, source, {
        branchSlug,
        originalPrefValues,
      });
    } catch (e) {
      lazy.log.error(`Failed to enroll in ${slug} (branch ${branchSlug})`, e);
    }
  }

  /**
   * Unenroll from a opt-in.
   *
   * @param {string} slug The slug of the opt-in to unenroll.
   */
  unenroll(slug) {
    if (!slug) {
      throw new TypeError("slug is required");
    }

    if (!this.#optIns.has(slug)) {
      lazy.log.error(`Unknown opt-in ${slug}`);
      return;
    }

    try {
      lazy.ExperimentAPI.manager.unenroll(
        slug,
        lazy.UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.LABS_OPT_OUT
        )
      );
    } catch (e) {
      lazy.log.error(`unenroll: failed to unenroll from ${slug}`, e);
    }
  }

  /**
   * Return the number of eligible opt-ins.
   *
   * @return {number} The number of eligible opt-ins.
   */
  get count() {
    return this.#optIns.size;
  }

  /**
   * Yield all available opt-ins.
   *
   * @returns {Generator<object>} The recipes of the available opt-ins.
   */
  *all() {
    for (const entry of this.#optIns.values()) {
      yield entry.recipe;
    }
  }

  /**
   * Return an opt-in by its slug.
   *
   * @param {string} slug The slug of the opt-in to return.
   *
   * @returns {OptInEntry} The requested opt-in, if it exists.
   */
  get(slug) {
    return this.#optIns.get(slug);
  }
}
