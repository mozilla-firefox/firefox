/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {object} HistoryVisit
 *   A visit that was fetched from the Places DB.
 * @property {Date} date
 *   The date this visit occurred.
 * @property {string | null} title
 *   The page's title, or `null` if unavailable.
 * @property {string} url
 *   The page's URL.
 * @property {string} guid
 *   The page's GUID.
 */

/**
 * A collection of visits from history.
 *
 * Supports loading and unloading. When loaded, its visits are accessible. When
 * unloaded, its visits are freed from memory. It is therefore advised to unload
 * the container once its contents are no longer visible in the UI.
 */
export class HistoryContainerBase {
  /** @type {HistoryVisit[]} */
  children = [];
  isLoaded = false;

  /**
   * An in-flight request to populate this container.
   *
   * If a response is received while `shouldCommit` is false, results are
   * silently discarded (not stored in memory).
   *
   * @type {{ promise: Promise<void>; shouldCommit: boolean }?}
   */
  #pendingLoad = null;

  /**
   * Load this container's contents.
   *
   * @returns {Promise<void>}
   */
  async load() {
    if (this.isLoaded) {
      return;
    }
    if (this.#pendingLoad) {
      this.#pendingLoad.shouldCommit = true;
    } else {
      const pendingLoad = {
        shouldCommit: true,
        promise: this.fetchHistory().then(
          children => {
            this.#pendingLoad = null;
            if (pendingLoad.shouldCommit) {
              this.children = children;
              this.isLoaded = true;
            }
          },
          error => {
            this.#pendingLoad = null;
            throw error;
          }
        ),
      };
      this.#pendingLoad = pendingLoad;
    }
    await this.#pendingLoad.promise;
  }

  /**
   * Release visits and discard pending results unless loaded again.
   */
  unload() {
    if (this.#pendingLoad) {
      this.#pendingLoad.shouldCommit = false;
    }
    this.children = [];
    this.isLoaded = false;
  }

  /**
   * Fetch this container's contents.
   *
   * @abstract
   * @returns {Promise<HistoryVisit[]>}
   *   Visits to store in the container.
   */
  fetchHistory() {
    throw new Error("Subclasses must implement fetchHistory()");
  }
}

/**
 * A registry of history containers.
 *
 * @template {HistoryContainerBase} T
 */
export class HistoryContainerRegistry {
  /** @type {T[]} */
  containers;

  /**
   * @param {T[]} containers
   */
  constructor(containers) {
    this.containers = containers;
  }

  /**
   * Release every container's visits.
   */
  unloadAll() {
    for (const container of this.containers) {
      container.unload();
    }
  }
}
