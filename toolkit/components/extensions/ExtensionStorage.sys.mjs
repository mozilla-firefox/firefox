/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const { DefaultWeakMap, ExtensionError } = ExtensionUtils;

const lazy = XPCOMUtils.declareLazy({
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
  enforceSessionQuota: {
    pref: "webextensions.storage.session.enforceQuota",
    default: false,
  },
});

function isStructuredCloneHolder(value) {
  return (
    value &&
    typeof value === "object" &&
    Cu.getClassName(value, true) === "StructuredCloneHolder"
  );
}

class SerializeableMap extends Map {
  toJSON() {
    let result = {};
    for (let [key, value] of this) {
      if (isStructuredCloneHolder(value)) {
        value = value.deserialize(globalThis);
        this.set(key, value);
      }

      result[key] = value;
    }
    return result;
  }

  /**
   * Like toJSON, but attempts to serialize every value separately, and
   * elides any which fail to serialize. Should only be used if initial
   * JSON serialization fails.
   *
   * @returns {object}
   */
  toJSONSafe() {
    let result = {};
    for (let [key, value] of this) {
      try {
        void JSON.stringify(value);

        result[key] = value;
      } catch (e) {
        Cu.reportError(
          new Error(`Failed to serialize browser.storage key "${key}": ${e}`)
        );
      }
    }
    return result;
  }
}

/**
 * Serializes an arbitrary value into a StructuredCloneHolder, if
 * appropriate. Existing StructuredCloneHolders are returned unchanged.
 * Non-object values are also returned unchanged. Anything else is
 * serialized, and a new StructuredCloneHolder returned.
 *
 * This allows us to avoid a second structured clone operation after
 * sending a storage value across a message manager, before cloning it
 * into an extension scope.
 *
 * @param {string} name
 *        A debugging name for the value, which will appear in the
 *        StructuredCloneHolder's about:memory path.
 * @param {string?} anonymizedName
 *        An anonymized version of `name`, to be used in anonymized memory
 *        reports. If `null`, then `name` will be used instead.
 * @param {StructuredCloneHolder|*} value
 *        A value to serialize.
 * @returns {*}
 */
function serialize(name, anonymizedName, value) {
  if (value && typeof value === "object" && !isStructuredCloneHolder(value)) {
    return new StructuredCloneHolder(name, anonymizedName, value);
  }
  return value;
}

export var ExtensionStorage = {
  /** @type {Map<string, Promise<typeof lazy.JSONFile>>} */
  jsonFilePromises: new Map(),

  listeners: new Map(),

  /**
   * Asynchronously reads the storage file for the given extension ID
   * and returns a Promise for its initialized JSONFile object.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to return a file.
   * @returns {Promise<InstanceType<Lazy['JSONFile']>>}
   */
  async _readFile(extensionId) {
    await IOUtils.makeDirectory(this.getExtensionDir(extensionId));

    let jsonFile = new lazy.JSONFile({
      path: this.getStorageFile(extensionId),
    });
    await jsonFile.load();

    jsonFile.data = this._serializableMap(jsonFile.data);
    return jsonFile;
  },

  _serializableMap(data) {
    return new SerializeableMap(Object.entries(data));
  },

  /**
   * Returns a Promise for initialized JSONFile instance for the
   * extension's storage file.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to return a file.
   * @returns {Promise<InstanceType<Lazy['JSONFile']>>}
   */
  getFile(extensionId) {
    let promise = this.jsonFilePromises.get(extensionId);
    if (!promise) {
      promise = this._readFile(extensionId);
      this.jsonFilePromises.set(extensionId, promise);
    }
    return promise;
  },

  /**
   * Clear the cached jsonFilePromise for a given extensionId
   * (used by ExtensionStorageIDB to free the jsonFile once the data migration
   * has been completed).
   *
   * @param {string} extensionId
   *        The ID of the extension for which to return a file.
   */
  async clearCachedFile(extensionId) {
    let promise = this.jsonFilePromises.get(extensionId);
    if (promise) {
      this.jsonFilePromises.delete(extensionId);
      await promise.then(jsonFile => jsonFile.finalize());
    }
  },

  /**
   * Sanitizes the given value, and returns a JSON-compatible
   * representation of it, based on the privileges of the given global.
   *
   * @param {any} value
   *        The value to sanitize.
   * @param {BaseContext} context
   *        The extension context in which to sanitize the value
   * @returns {value}
   *        The sanitized value.
   */
  sanitize(value, context) {
    let json = context.jsonStringify(value === undefined ? null : value);
    if (json == undefined) {
      throw new ExtensionError(
        "DataCloneError: The object could not be cloned."
      );
    }
    return JSON.parse(json);
  },

  /**
   * Returns the path to the storage directory within the profile for
   * the given extension ID.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to return a directory path.
   * @returns {string}
   */
  getExtensionDir(extensionId) {
    return PathUtils.join(this.extensionDir, extensionId);
  },

  /**
   * Returns the path to the JSON storage file for the given extension
   * ID.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to return a file path.
   * @returns {string}
   */
  getStorageFile(extensionId) {
    return PathUtils.join(this.extensionDir, extensionId, "storage.js");
  },

  /**
   * Asynchronously sets the values of the given storage items for the
   * given extension.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to set storage values.
   * @param {object} items
   *        The storage items to set. For each property in the object,
   *        the storage value for that property is set to its value in
   *        said object. Any values which are StructuredCloneHolder
   *        instances are deserialized before being stored.
   * @returns {Promise<void>}
   */
  async set(extensionId, items) {
    let jsonFile = await this.getFile(extensionId);

    let changes = {};
    for (let prop in items) {
      let item = items[prop];
      changes[prop] = {
        oldValue: serialize(
          `set/${extensionId}/old/${prop}`,
          `set/${extensionId}/old/<anonymized>`,
          jsonFile.data.get(prop)
        ),
        newValue: serialize(
          `set/${extensionId}/new/${prop}`,
          `set/${extensionId}/new/<anonymized>`,
          item
        ),
      };
      jsonFile.data.set(prop, item);
    }

    this.notifyListeners(extensionId, changes);

    jsonFile.saveSoon();
    return null;
  },

  /**
   * Asynchronously removes the given storage items for the given
   * extension ID.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to remove storage values.
   * @param {Array<string>} items
   *        A list of storage items to remove.
   * @returns {Promise<void>}
   */
  async remove(extensionId, items) {
    let jsonFile = await this.getFile(extensionId);

    let changed = false;
    let changes = {};

    for (let prop of [].concat(items)) {
      if (jsonFile.data.has(prop)) {
        changes[prop] = {
          oldValue: serialize(
            `remove/${extensionId}/${prop}`,
            `remove/${extensionId}/<anonymized>`,
            jsonFile.data.get(prop)
          ),
        };
        jsonFile.data.delete(prop);
        changed = true;
      }
    }

    if (changed) {
      this.notifyListeners(extensionId, changes);
      jsonFile.saveSoon();
    }
    return null;
  },

  /**
   * Asynchronously clears all storage entries for the given extension
   * ID.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to clear storage.
   * @param {object} options
   * @param {boolean} [options.shouldNotifyListeners = true]
   *         Whether or not collect and send the changes to the listeners,
   *         used when the extension data is being cleared on uninstall.
   * @returns {Promise<void>}
   */
  async clear(extensionId, { shouldNotifyListeners = true } = {}) {
    let jsonFile = await this.getFile(extensionId);

    let changed = false;
    let changes = {};

    for (let [prop, oldValue] of jsonFile.data.entries()) {
      if (shouldNotifyListeners) {
        changes[prop] = {
          oldValue: serialize(
            `clear/${extensionId}/${prop}`,
            `clear/${extensionId}/<anonymized>`,
            oldValue
          ),
        };
      }

      jsonFile.data.delete(prop);
      changed = true;
    }

    if (changed) {
      if (shouldNotifyListeners) {
        this.notifyListeners(extensionId, changes);
      }

      jsonFile.saveSoon();
    }
    return null;
  },

  /**
   * Asynchronously retrieves the values for the given storage items for
   * the given extension ID.
   *
   * @param {string} extensionId
   *        The ID of the extension for which to get storage values.
   * @param {Array<string>|object|null} [keys]
   *        The storage items to get. If an array, the value of each key
   *        in the array is returned. If null, the values of all items
   *        are returned. If an object, the value for each key in the
   *        object is returned, or that key's value if the item is not
   *        set.
   * @returns {Promise<object>}
   *        An object which a property for each requested key,
   *        containing that key's storage value. Values are
   *        StructuredCloneHolder objects which can be deserialized to
   *        the original storage value.
   */
  async get(extensionId, keys) {
    let jsonFile = await this.getFile(extensionId);
    return this._filterProperties(extensionId, jsonFile.data, keys);
  },

  async _filterProperties(extensionId, data, keys) {
    let result = {};
    if (keys === null) {
      Object.assign(result, data.toJSON());
    } else if (typeof keys == "object" && !Array.isArray(keys)) {
      for (let prop in keys) {
        if (data.has(prop)) {
          result[prop] = serialize(
            `filterProperties/${extensionId}/${prop}`,
            `filterProperties/${extensionId}/<anonymized>`,
            data.get(prop)
          );
        } else {
          result[prop] = keys[prop];
        }
      }
    } else {
      for (let prop of [].concat(keys)) {
        if (data.has(prop)) {
          result[prop] = serialize(
            `filterProperties/${extensionId}/${prop}`,
            `filterProperties/${extensionId}/<anonymized>`,
            data.get(prop)
          );
        }
      }
    }

    return result;
  },

  addOnChangedListener(extensionId, listener) {
    let listeners = this.listeners.get(extensionId) || new Set();
    listeners.add(listener);
    this.listeners.set(extensionId, listeners);
  },

  removeOnChangedListener(extensionId, listener) {
    let listeners = this.listeners.get(extensionId);
    listeners.delete(listener);
  },

  notifyListeners(extensionId, changes) {
    let listeners = this.listeners.get(extensionId);
    if (listeners) {
      for (let listener of listeners) {
        listener(changes);
      }
    }
  },

  init() {
    if (Services.appinfo.processType != Services.appinfo.PROCESS_TYPE_DEFAULT) {
      return;
    }
    Services.obs.addObserver(this, "extension-invalidate-storage-cache");
    Services.obs.addObserver(this, "xpcom-shutdown");
  },

  observe(subject, topic) {
    if (topic == "xpcom-shutdown") {
      Services.obs.removeObserver(this, "extension-invalidate-storage-cache");
      Services.obs.removeObserver(this, "xpcom-shutdown");
    } else if (topic == "extension-invalidate-storage-cache") {
      for (let promise of this.jsonFilePromises.values()) {
        promise.then(jsonFile => {
          jsonFile.finalize();
        });
      }
      this.jsonFilePromises.clear();
    }
  },

  // Serializes an arbitrary value into a StructuredCloneHolder, if appropriate.
  serialize,

  /**
   * Serializes the given storage items for transporting between processes.
   *
   * @param {BaseContext} context
   *        The context to use for the created StructuredCloneHolder
   *        objects.
   * @param {Array<string>|object} items
   *        The items to serialize. If an object is provided, its
   *        values are serialized to StructuredCloneHolder objects.
   *        Otherwise, it is returned as-is.
   * @returns {Array<string>|object}
   */
  serializeForContext(context, items) {
    if (items && typeof items === "object" && !Array.isArray(items)) {
      let result = {};
      for (let [key, value] of Object.entries(items)) {
        try {
          result[key] = new StructuredCloneHolder(
            `serializeForContext/${context.extension.id}`,
            null,
            value,
            context.cloneScope
          );
        } catch (e) {
          throw new ExtensionError(String(e));
        }
      }
      return result;
    }
    return items;
  },

  /**
   * Deserializes the given storage items into the given extension context.
   *
   * @param {BaseContext} context
   *        The context to use to deserialize the StructuredCloneHolder objects.
   * @param {object} items
   *        The items to deserialize. Any property of the object which
   *        is a StructuredCloneHolder instance is deserialized into
   *        the extension scope. Any other object is cloned into the
   *        extension scope directly.
   * @returns {object}
   */
  deserializeForContext(context, items) {
    let result = new context.cloneScope.Object();
    for (let [key, value] of Object.entries(items)) {
      if (
        value &&
        typeof value === "object" &&
        Cu.getClassName(value, true) === "StructuredCloneHolder"
      ) {
        value = value.deserialize(context.cloneScope, true);
      } else {
        value = Cu.cloneInto(value, context.cloneScope);
      }
      result[key] = value;
    }
    return result;
  },
};

ChromeUtils.defineLazyGetter(ExtensionStorage, "extensionDir", () =>
  PathUtils.join(PathUtils.profileDir, "browser-extension-data")
);

ExtensionStorage.init();

class QuotaMap extends Map {
  static QUOTA_BYTES = 10485760;

  bytesUsed = 0;

  /**
   * @param {string} key
   * @param {StructuredCloneHolder} holder
   */
  dataSize(key, holder) {
    // Using key.length is not really correct, but is probably less surprising
    // for developers. We don't need an exact count, just ensure it's bounded.
    return key.length + holder.dataSize;
  }

  /**
   * @param {string} key
   * @param {StructuredCloneHolder} holder
   */
  getSizeDelta(key, holder) {
    let before = this.has(key) ? this.dataSize(key, this.get(key)) : 0;
    return this.dataSize(key, holder) - before;
  }

  /** @param {Record<string, StructuredCloneHolder>} items */
  checkQuota(items) {
    let after = this.bytesUsed;
    for (let [key, holder] of Object.entries(items)) {
      after += this.getSizeDelta(key, holder);
    }

    if (after > QuotaMap.QUOTA_BYTES && lazy.enforceSessionQuota) {
      throw new ExtensionError(
        "QuotaExceededError: storage.session API call exceeded its quota limitations."
      );
    }
  }

  set(key, holder) {
    this.checkQuota({ [key]: holder });
    this.bytesUsed += this.getSizeDelta(key, holder);
    return super.set(key, holder);
  }

  delete(key) {
    if (this.has(key)) {
      this.bytesUsed -= this.dataSize(key, this.get(key));
    }
    return super.delete(key);
  }

  clear() {
    this.bytesUsed = 0;
    super.clear();
  }
}

export var extensionStorageSession = {
  /** @type {WeakMap<Extension, QuotaMap>} */
  buckets: new DefaultWeakMap(_extension => new QuotaMap()),

  /** @type {WeakMap<Extension, Set<callback>>} */
  listeners: new DefaultWeakMap(_extension => new Set()),

  get QUOTA_BYTES() {
    // Even if quota is not enforced yet, report the future default of 10MB.
    return QuotaMap.QUOTA_BYTES;
  },

  /**
   * @param {Extension} extension
   * @param {null | undefined | string | string[] | object} items
   * Schema normalization ensures items are normalized to one of above types.
   */
  get(extension, items) {
    let bucket = this.buckets.get(extension);

    let result = {};
    /** @type {Iterable<string>} */
    let keys = [];

    if (!items) {
      keys = bucket.keys();
    } else if (typeof items !== "object" || Array.isArray(items)) {
      keys = [].concat(items);
    } else {
      keys = Object.keys(items);
      result = items;
    }

    for (let prop of keys) {
      if (bucket.has(prop)) {
        result[prop] = bucket.get(prop);
      }
    }
    return result;
  },

  set(extension, items) {
    let bucket = this.buckets.get(extension);

    // set() below also checks the quota for each item, but
    // this check includes all inputs to avoid partial updates.
    bucket.checkQuota(items);

    let changes = {};
    for (let [key, value] of Object.entries(items)) {
      changes[key] = {
        oldValue: bucket.get(key),
        newValue: value,
      };
      bucket.set(key, value);
    }
    this.notifyListeners(extension, changes);
  },

  remove(extension, keys) {
    let bucket = this.buckets.get(extension);
    let changes = {};
    for (let k of [].concat(keys)) {
      if (bucket.has(k)) {
        changes[k] = { oldValue: bucket.get(k) };
        bucket.delete(k);
      }
    }
    this.notifyListeners(extension, changes);
  },

  clear(extension) {
    let bucket = this.buckets.get(extension);
    let changes = {};
    for (let k of bucket.keys()) {
      changes[k] = { oldValue: bucket.get(k) };
    }
    bucket.clear();
    this.notifyListeners(extension, changes);
  },

  /**
   * @param {Extension} extension
   * @param {null | undefined | string | string[] } keys
   */
  getBytesInUse(extension, keys) {
    let bucket = this.buckets.get(extension);
    if (keys == null) {
      return bucket.bytesUsed;
    }
    let result = 0;
    for (let k of [].concat(keys)) {
      if (bucket.has(k)) {
        result += bucket.dataSize(k, bucket.get(k));
      }
    }
    return result;
  },

  registerListener(extension, listener) {
    this.listeners.get(extension).add(listener);
    return () => {
      this.listeners.get(extension).delete(listener);
    };
  },

  notifyListeners(extension, changes) {
    if (!Object.keys(changes).length) {
      return;
    }
    for (let listener of this.listeners.get(extension)) {
      lazy.ExtensionCommon.runSafeSyncWithoutClone(listener, changes);
    }
  },
};
