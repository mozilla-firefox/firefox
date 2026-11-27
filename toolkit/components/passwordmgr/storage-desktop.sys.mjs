/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { LoginManagerStorage_json } from "resource://gre/modules/storage-json.sys.mjs";
import { LoginManagerRustStorage } from "resource://gre/modules/storage-rust.sys.mjs";
import { LoginStorageMigrator } from "resource://gre/modules/LoginStorageMigrator.sys.mjs";

// The class name of the error that aborted initialization. XPCOM exceptions
// all construct "Exception"; their `name` carries the result code instead.
function errorKind(error) {
  const name = error.constructor?.name;
  return name == "Exception" ? error.name : (name ?? "Error");
}

export class LoginManagerStorage extends LoginManagerStorage_json {
  static #jsonStorage = null;
  static #rustStorage = null;
  static #activeStore = null;
  static #initializationPromise = null;

  static create() {
    if (!this.#initializationPromise) {
      this.#jsonStorage = new LoginManagerStorage_json();
      this.#rustStorage = new LoginManagerRustStorage();

      const startedAt = ChromeUtils.now();
      this.#initializationPromise = this.#jsonStorage
        .initialize()
        .then(() => this.#rustStorage.initialize())
        .then(async () => {
          const store = await new LoginStorageMigrator(
            this.#jsonStorage,
            this.#rustStorage
          ).run();
          this.#activeStore = store;
          this.#jsonStorage.isActive = store === this.#jsonStorage;
          this.#rustStorage.isActive = store === this.#rustStorage;
          return store;
        })
        .then(
          store => {
            this.#recordInitStatus(startedAt, store.backendName, null);
            return store;
          },
          error => {
            this.#recordInitStatus(startedAt, null, error);
            throw error;
          }
        );
    }

    return this.#initializationPromise;
  }

  static #recordInitStatus(startedAt, backend, error) {
    const timings = this.#rustStorage.initTimings;
    Glean.pwmgr.rustInitStatus.record({
      init_rust_components_ms: timings?.initRustComponentsMs ?? null,
      create_rust_store_ms: timings?.createRustStoreMs ?? null,
      total_ms: Math.round(ChromeUtils.now() - startedAt),
      backend,
      result: error ? "error" : "ok",
      error_kind: error ? errorKind(error) : null,
    });
  }

  static getActiveStore() {
    return this.#activeStore;
  }

  /**
   * Called once per session from the browser-best-effort-idle-startup
   * category, so it may not run at all.
   */
  static async recordDecryptability() {
    const store = await this.#initializationPromise?.catch(() => null);
    if (store && store === this.#rustStorage) {
      await store.recordDecryptability();
    }
  }
}
