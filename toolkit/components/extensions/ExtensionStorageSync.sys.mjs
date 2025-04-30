/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  storageSyncService:
    "resource://gre/modules/ExtensionStorageComponents.sys.mjs",
  QuotaError: "resource://gre/modules/RustWebextstorage.sys.mjs",
});

// The backing implementation of the browser.storage.sync web extension API.
export class ExtensionStorageSync {
  constructor() {
    this.listeners = new Map();
    this.backend = "rust";
  }

  async #getRustStore() {
    return await lazy.storageSyncService.getStorageAreaInstance();
  }

  async callRustStoreFn(fnName, extension, ...args) {
    let sargs = args.map(val => JSON.stringify(val));

    try {
      let extId = extension.id;
      let rustStore = await this.#getRustStore();
      switch (fnName) {
        case "set": {
          let changes = this._parseRustStorageValueChangeList(
            await rustStore.set(extId, ...sargs)
          );
          this.notifyListeners(extId, changes);
          return null;
        }
        case "remove": {
          let changes = this._parseRustStorageValueChangeList(
            await rustStore.remove(extId, ...sargs)
          );
          this.notifyListeners(extId, changes);
          return null;
        }
        case "clear": {
          let changes = this._parseRustStorageValueChangeList(
            await rustStore.clear(extId)
          );
          this.notifyListeners(extId, changes);
          return null;
        }
        case "get": {
          let result = await rustStore.get(extId, ...sargs);
          return JSON.parse(result);
        }
        case "getBytesInUse": {
          let result = await rustStore.getBytesInUse(extId, ...sargs);
          return JSON.parse(result);
        }
      }
    } catch (ex) {
      // The only "public" exception here is for quota failure - all others
      // are sanitized.
      let sanitized =
        ex instanceof lazy.QuotaError
          ? // The same message as the local IDB implementation
            "QuotaExceededError: storage.sync API call exceeded its quota limitations."
          : // The standard, generic extension error.
            "An unexpected error occurred";
      throw new lazy.ExtensionUtils.ExtensionError(sanitized);
    }
  }

  async set(extension, items) {
    return await this.callRustStoreFn("set", extension, items);
  }

  async remove(extension, keys) {
    return await this.callRustStoreFn("remove", extension, keys);
  }

  async clear(extension) {
    return await this.callRustStoreFn("clear", extension);
  }

  async clearOnUninstall(extensionId) {
    // Resolve the returned promise once the request has been either resolved
    // or rejected (and report the error on the browser console in case of
    // unexpected clear failures on addon uninstall).
    try {
      let rustStore = await this.#getRustStore();
      await rustStore.clear(extensionId);
    } catch (err) {
      Cu.reportError(err);
    }
  }

  async get(extension, spec) {
    return await this.callRustStoreFn("get", extension, spec);
  }

  async getBytesInUse(extension, keys) {
    return await this.callRustStoreFn("getBytesInUse", extension, keys);
  }

  addOnChangedListener(extension, listener) {
    let listeners = this.listeners.get(extension.id) || new Set();
    listeners.add(listener);
    this.listeners.set(extension.id, listeners);
  }

  removeOnChangedListener(extension, listener) {
    let listeners = this.listeners.get(extension.id);
    listeners.delete(listener);
    if (listeners.size == 0) {
      this.listeners.delete(extension.id);
    }
  }

  _parseRustStorageValueChangeList(changeSets) {
    let changes = {};
    for (let change of changeSets.changes) {
      changes[change.key] = {};
      if (change.oldValue) {
        changes[change.key].oldValue = JSON.parse(change.oldValue);
      }
      if (change.newValue) {
        changes[change.key].newValue = JSON.parse(change.newValue);
      }
    }
    return changes;
  }

  notifyListeners(extId, changes) {
    let listeners = this.listeners.get(extId) || new Set();

    if (listeners) {
      for (let listener of listeners) {
        lazy.ExtensionCommon.runSafeSyncWithoutClone(listener, changes);
      }
    }
  }
}

export var extensionStorageSync = new ExtensionStorageSync();
