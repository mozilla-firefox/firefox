/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { NimbusEnrollments } from "./Enrollments.sys.mjs" */

import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
});

const IS_MAIN_PROCESS =
  Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

export class SharedDataMap extends EventEmitter {
  constructor(sharedDataKey) {
    super();

    this._sharedDataKey = sharedDataKey;
    this._isReady = false;
    this._readyDeferred = Promise.withResolvers();
    this._data = null;

    if (IS_MAIN_PROCESS) {
      this._shutdownBlocker = () => {
        this._checkIfShutdownStarted();
      };

      if (!this._checkIfShutdownStarted()) {
        lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
          "SharedDataMap: shutting down before init finished",
          this._shutdownBlocker
        );
      } // else we directly rejected our _readyDeferred promise.

      /**
       * The NimbusEnrollments instance.
       * Only nullable in tests.
       *
       * @type {NimbusEnrollments | null}
       */
      this._db = lazy.NimbusEnrollments.persistenceEnabled
        ? new lazy.NimbusEnrollments()
        : null;
    } else {
      this._syncFromParent();
      Services.cpmm.sharedData.addEventListener("change", this);
    }
  }

  async init() {
    if (!this._isReady && IS_MAIN_PROCESS) {
      try {
        this._data = this._db ? await this._db.init() : {};

        this._syncToChildren({ flush: true });
        this._checkIfReady();

        // Once we finished init, we do not need the blocker anymore.
        lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(
          this._shutdownBlocker
        );
      } catch (e) {
        console.error(e);
      }
    }
  }

  get sharedDataKey() {
    return this._sharedDataKey;
  }

  ready() {
    return this._readyDeferred.promise;
  }

  get(key) {
    if (!this._data) {
      return null;
    }

    let entry = this._data[key];

    return entry;
  }

  set(key, value) {
    if (!IS_MAIN_PROCESS) {
      throw new Error(
        "Setting values from within a content process is not allowed"
      );
    }

    this._data[key] = value;

    this._syncToChildren();
    this._notifyUpdate();
  }

  /**
   * Replace the stored data with an updated filtered dataset for cleanup
   * purposes. We don't notify of update because we're only filtering out
   * old unused entries.
   *
   * @param {string[]} keysToRemove - list of keys to remove from the persistent store
   */
  _removeEntriesByKeys(keysToRemove) {
    if (!keysToRemove.length) {
      return;
    }

    for (const key of keysToRemove) {
      try {
        delete this._data[key];
      } catch (e) {
        // It's ok if this fails
      }
    }
  }

  // Only used in tests
  _deleteForTests(key) {
    if (!IS_MAIN_PROCESS) {
      throw new Error(
        "Setting values from within a content process is not allowed"
      );
    }

    if (this.has(key)) {
      delete this._data[key];

      this._syncToChildren();
      this._notifyUpdate();
    }
  }

  has(key) {
    return Boolean(this.get(key));
  }

  /**
   * Notify store listeners of updates
   * Called both from Main and Content process
   */
  _notifyUpdate(process = "parent") {
    for (let key of Object.keys(this._data || {})) {
      this.emit(`${process}-store-update:${key}`, this._data[key]);
    }
  }

  _syncToChildren({ flush = false } = {}) {
    Services.ppmm.sharedData.set(this.sharedDataKey, {
      ...this._data,
    });
    if (flush) {
      Services.ppmm.sharedData.flush();
    }
  }

  _syncFromParent() {
    this._data = Services.cpmm.sharedData.get(this.sharedDataKey);
    this._checkIfReady();
    this._notifyUpdate("child");
  }

  _checkIfReady() {
    if (!this._isReady && this._data) {
      this._isReady = true;
      this._readyDeferred.resolve();
    }
  }

  _checkIfShutdownStarted() {
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      // This will unblock anybody waiting for our init and leave data == null
      // if it was not yet initialized, making get() return null.
      this._readyDeferred.reject(new Error("SharedDataMap: in shutdown"));
      lazy.AsyncShutdown.appShutdownConfirmed.removeBlocker(
        this._shutdownBlocker
      );
      return true;
    }
    return false;
  }

  handleEvent(event) {
    if (event.type === "change") {
      if (event.changedKeys.includes(this.sharedDataKey)) {
        this._syncFromParent();
      }
    }
  }
}
