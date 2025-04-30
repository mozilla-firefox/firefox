/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  KeyValueService: "resource://gre/modules/kvstore.sys.mjs",
  MENU_STORE_WRITE_DEBOUNCE_TIME: {
    pref: "extensions.webextensions.menus.writeDebounceTime",
    // Minimum 0ms, max 1min
    transform: value => Math.min(Math.max(value, 0), 1 * 60 * 1000),
    default: 5000, // Default to 5s
  },
});

const SCHEMA_VERSION = 1;
const KVSTORE_DIRNAME = "extension-store-menus";

/**
 * MenuId represent the type of the ids associated to the extension
 * created menus, which is expected to be of type:
 *
 * - string
 * - or an auto-generated integer (for menus created without a pre-assigned menu id,
 *   only allowed for extensions with a persistent background script or without any background
 *   context at all).
 *
 * Only menus registered by extensions with a non-persistent backgrond context are going
 * to be persisted across sessions, and their id is always a string.
 *
 * @typedef {number} integer
 * @typedef {string|integer} MenuId
 *

/**
 * This class manages the extensions menus stored on disk across
 * all extensions (with kvstore as the underlying backend).
 */
class ExtensionMenusStore {
  #store = null;
  #initPromise = null;

  /**
   * Determine if the menus created by the given extension should
   * be persisted on disk.
   *
   * @param {Extension} extension
   *
   * @returns {boolean} Returns true if the menus should be persisted on disk.
   */
  static shouldPersistMenus(extension) {
    return extension.manifest.background && !extension.persistentBackground;
  }

  get storePath() {
    const { path: storePath } = lazy.FileUtils.getDir("ProfD", [
      KVSTORE_DIRNAME,
    ]);
    return storePath;
  }

  async #asyncInit() {
    const { storePath } = this;
    await IOUtils.makeDirectory(storePath, { ignoreExisting: true });
    this.#store = await lazy.KeyValueService.getOrCreateWithOptions(
      storePath,
      "menus",
      { strategy: lazy.KeyValueService.RecoveryStrategy.RENAME }
    );
  }

  #lazyInit() {
    if (!this.#initPromise) {
      this.#initPromise = this.#asyncInit();
    }

    return this.#initPromise;
  }

  #notifyPersistedMenusUpdatedForTesting(extensionId) {
    Services.obs.notifyObservers(
      null,
      "webext-persisted-menus-updated",
      extensionId
    );
  }

  /**
   * An helper method to check if the store includes data for the given extension (ID).
   *
   * @param {string} extensionId An extension ID
   * @returns {Promise<boolean>} true if the store includes data for the given
   * extension, false otherwise.
   */
  async hasExtensionData(extensionId) {
    await this.#lazyInit();
    return this.#store.has(extensionId);
  }

  /**
   * Returns all the persisted menus for a given extension (ID), or an empty map
   * if there isn't any data for the given extension id and extension version.
   *
   * @param {string} extensionId An extension ID
   * @param {string} currentExtensionVersion The current extension version
   * @returns {Promise<Map>} A map of persisted menu details.
   */
  async getPersistedMenus(extensionId, currentExtensionVersion) {
    await this.#lazyInit();

    let value;
    try {
      value = await this.#store.get(extensionId);
    } catch (err) {
      Cu.reportError(
        `Error on retrieving stored menus for ${extensionId}: ${err}\n`
      );
    }

    if (!value) {
      return new Map();
    }

    const { menuSchemaVersion, extensionVersion, menus } = JSON.parse(value);

    // Drop the stored data if the extension version is not matching the
    // current extension version.
    if (extensionVersion != currentExtensionVersion) {
      return new Map();
    }

    // NOTE: future version may use the following block to convert stored menus
    // data.
    if (menuSchemaVersion !== SCHEMA_VERSION) {
      Cu.reportError(
        `Dropping stored menus for ${extensionId} due to unxpected menuSchemaVersion ${menuSchemaVersion} (expected ${SCHEMA_VERSION})`
      );
      // TODO: should we consider firing onInstalled if we had to drop stored
      // menus due to a schema mismatch? should we do the same in case of
      // corrupted storage?
      return new Map();
    }

    return new Map(menus);
  }

  /**
   * Updates the map of persisted menus for a given extension (ID).
   *
   * We store each menu registered by extensions as an array of
   * key/value pairs (derived from the Map of MenuCreateProperties).
   *
   * The format on disk should look like this one:
   *
   * ```
   * {
   *   "@extension-id1": { menuSchemaVersion: N, menus: [["menuid-1", <MenuCreateProperties>], ...] },
   *   "@extension-id2": { menuSchemaVersion: N, menus: [["menuid-2", <MenuCreateProperties>], ...] },
   * }
   * ```
   *
   * @param {string} extensionId An extension ID
   * @returns {Promise<void>}
   */
  async updatePersistedMenus(extensionId, extensionVersion, menusMap) {
    await this.#lazyInit();
    await this.#store.put(
      extensionId,
      JSON.stringify({
        menuSchemaVersion: SCHEMA_VERSION,
        extensionVersion: extensionVersion,
        menus: Array.from(menusMap.entries()),
      })
    );
    this.#notifyPersistedMenusUpdatedForTesting(extensionId);
  }

  /**
   * Clears all the menus persisted on disk for a given extension (ID)
   * being uninstalled.
   *
   * @param {string} extensionId An extension ID
   */
  async clearPersistedMenusOnUninstall(extensionId) {
    const { storePath } = this;
    const kvstoreDirExists = await IOUtils.exists(storePath);
    if (!kvstoreDirExists) {
      // Avoid to create an unnecessary kvstore directory (through the call
      // to lazyInit). If one doesn't already, then there isn't any data
      // to clear.
      return;
    }

    await this.#lazyInit();
    await this.#store.delete(extensionId);
    this.#notifyPersistedMenusUpdatedForTesting(extensionId);
  }
}

let store = new ExtensionMenusStore();

/**
 * This class manages the extensions menus for a specific extension.
 *
 * For extensions with a persistent background extension context
 * or without any background extension the menus are kept only in memory,
 * whereas for extensions with a non persistent background context
 * the menus are also persisted on disk.
 */
class ExtensionMenusManager {
  #writeToStoreTask = null;
  #shutdownBlockerFn = null;

  constructor(extension) {
    if (extension.hasShutdown) {
      throw new Error(
        `Error on creating new ExtensionMenusManager after extension shutdown: ${extension.id}`
      );
    }
    this.extensionId = extension.id;
    this.extensionVersion = extension.version;
    this.persistMenusData = ExtensionMenusStore.shouldPersistMenus(extension);
    // Map[MenuId -> MenuCreateProperties]
    this.menus = null;
    if (this.persistMenusData) {
      extension.callOnClose(this);
    }
  }

  async _finalizeStoreTaskForTesting() {
    if (this.#writeToStoreTask && !this.#writeToStoreTask.isFinalized) {
      await this.#writeToStoreTask;
    }
  }

  close() {
    if (!this.#shutdownBlockerFn) {
      return;
    }

    const shutdownBlockerFn = this.#shutdownBlockerFn;
    shutdownBlockerFn().then(() => {
      lazy.AsyncShutdown.profileBeforeChange.removeBlocker(shutdownBlockerFn);
    });
  }

  async asyncInit() {
    if (this.menus) {
      // ExtensionMenusManager is expected to be called only once from
      // ExtensionMenus.asyncInitForExtension, which is expected to be
      // called only once per extension (from the ext-menus onStartup
      // lifecycle method).
      Cu.reportError(
        `ExtensionMenusManager for ${this.extensionId} should not be initialized more than once`
      );

      return;
    }

    if (!this.persistMenusData) {
      this.menus = new Map();
    }

    this.menus = await store
      .getPersistedMenus(this.extensionId, this.extensionVersion)
      .catch(err => {
        Cu.reportError(
          `Error loading ${this.extensionId} persisted menus: ${err}`
        );
        return new Map();
      });
  }

  #ensureInitialized() {
    // ExtensionMenusStore instance for each extension using the menus API
    // is expected to be done from the menus API onStartup lifecycle method.
    if (!this.menus) {
      throw new Error(
        `ExtensionMenusStore instance for ${this.extensionId} is not initialized`
      );
    }
  }

  /**
   * Synchronously retrieve the map of the extension menus.
   *
   * For extensions that should persist menus across sessions the map is
   * initialized from the data stored on disk and so this method is expected
   * to only be called after ext-menus onStartup lifecycle method have
   * called asyncInit on the instance of this class.
   *
   * @returns {Map<MenuId, object>} Map of the menus createProperties keyed by
   * menu id.
   */
  getMenus() {
    this.#ensureInitialized();
    return this.menus;
  }

  /**
   * Add or update menu data and optionally reparent menus (used when the update to
   * a menu includes a different parentId). A DeferredTask scheduled by this
   * method will update all menus data stored on disk for extensions that should
   * persist menus across sessions.
   *
   * @param {object}  menuDetails The createProperties for the menu
   * to add or update.
   * @param {boolean} [reparent=false] Set to true if the menu should also
   * be reparented.
   */
  updateMenus(menuDetails, reparent = false) {
    this.#ensureInitialized();

    if (this.persistMenusData && reparent) {
      // Make sure the reparent menu item is appended at the end (and so for sure
      // after the menu item that will become its new parent).
      // This is necessary if menuDetails.parentId is set (because it may point
      // to a menu entry that appears after the current entry in the Map), but we
      // still do it unconditionally (even if parentId is null) to make sure the
      // relocated menu item is always rendered at the bottom.
      this.menus.delete(menuDetails.id);
    }
    this.menus.set(menuDetails.id, menuDetails);

    if (!this.persistMenusData) {
      return;
    }

    if (reparent) {
      // The menu items are ordered, with child menu items always following its parent.
      // The logic below moves menu item registrations as needed to ensure a consistent order.
      let menuIds = new Set();
      menuIds.add(menuDetails.id);
      // Iterate over a copy of the entries because we may modify the menus Map.
      for (let [id, menuCreateDetails] of Array.from(this.menus)) {
        if (menuIds.has(menuCreateDetails.parentId)) {
          // Remember menu ID to detect its children.
          menuIds.add(id);
          // Append menu items to the end, to ensure that child menu items always follow the parent.
          this.menus.delete(id);
          this.menus.set(id, menuCreateDetails);
        }
      }
    }

    this.#scheduleWriteToStoreTask();
  }

  /**
   * Delete the given menu ids. A DeferredTask will update all menus
   * data stored on disk for extensions that should persist menus across sessions.
   *
   * @param {Array<MenuId>}  menuIds Array of menu ids to remove.
   */
  deleteMenus(menuIds) {
    this.#ensureInitialized();
    for (const menuId of menuIds) {
      this.menus.delete(menuId);
    }
    if (!this.persistMenusData) {
      return;
    }
    this.#scheduleWriteToStoreTask();
  }

  /**
   * Delete all menus. A DeferredTask scheduled by this method will update all menus
   * data stored on disk for extensions that should persist menus across sessions.
   */
  deleteAllMenus() {
    this.#ensureInitialized();
    let alreadyEmpty = !this.menus.size;
    this.menus.clear();
    if (!this.persistMenusData || alreadyEmpty) {
      return;
    }
    this.#scheduleWriteToStoreTask();
  }

  #scheduleWriteToStoreTask() {
    this.#ensureInitialized();
    if (!this.#writeToStoreTask) {
      this.#writeToStoreTask = new lazy.DeferredTask(
        () => this.#writeToStoreNow(),
        lazy.MENU_STORE_WRITE_DEBOUNCE_TIME
      );
      this.#shutdownBlockerFn = async () => {
        if (!this.#writeToStoreTask || this.#writeToStoreTask.isFinalized) {
          return;
        }
        await this.#writeToStoreTask.finalize();
        this.#writeToStoreTask = null;
        this.#shutdownBlockerFn = null;
      };
      lazy.AsyncShutdown.profileBeforeChange.addBlocker(
        `Flush "${this.extensionId}" persisted menus to disk`,
        this.#shutdownBlockerFn
      );
    }
    this.#writeToStoreTask.arm();
  }

  async #writeToStoreNow() {
    this.#ensureInitialized();
    await store.updatePersistedMenus(
      this.extensionId,
      this.extensionVersion,
      this.menus
    );
  }
}

/**
 * Singleton providing a collection of methods used by
 * ext-menus.js (and tests) to interact with the underlying classes.
 */
export const ExtensionMenus = {
  KVSTORE_DIRNAME,

  // WeakMap<Extension, { promise: Promise<ExtensionMenusManager>, instance: ExtensionMenusManager}>
  _menusManagers: new WeakMap(),

  /**
   * Determine if the menus created by the given extension should
   * be persisted on disk.
   *
   * @param {Extension} extension
   *
   * @returns {boolean} Returns true if the menus should be persisted on disk.
   */
  shouldPersistMenus(extension) {
    return ExtensionMenusStore.shouldPersistMenus(extension);
  },

  /**
   * Create and initialize ExtensionMenusManager instance
   * for the given extension.  Used by ext-menus.js onStartup
   * lifecycle method.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<void>} A promise resolved when the
   * ExtensionMenusManager instance is fully initialized
   * (and persisted menus data loaded from disk for the
   * extensions with a non-persistent background script).
   */
  async asyncInitForExtension(extension) {
    let { promise } = this._menusManagers.get(extension) ?? {};
    if (promise) {
      return promise;
    }

    const instance = new ExtensionMenusManager(extension);
    extension.callOnClose({
      close: () => {
        this._menusManagers.delete(extension);
      },
    });
    promise = instance.asyncInit().then(() => instance);
    this._menusManagers.set(extension, { promise, instance });
    return promise;
  },

  _getManager(extension) {
    const { instance } = this._menusManagers.get(extension) ?? {};
    if (!instance) {
      throw new Error(
        `No ExtensionMenusManager instance found for ${extension.id}`
      );
    }
    return instance;
  },

  /**
   * Helper function used to normalize and merge menus
   * create and update properties.
   *
   * @param {object} obj The target object
   * @param {object} properties The properties to merge
   * on the target object.
   *
   * @returns {object} The target object updated with
   * the merged properties.
   */
  mergeMenuProperties(obj, properties) {
    // The menu properties are being normalized based on
    // the API JSONSchema definitions, and so we can
    // rely on expecting properties not specified to be
    // set to null, besides "icons" which is expected to
    // be omitted when not explicitly specified (due to
    // the use of `"optional": "omit-key-if-missing"` in
    // its schema definition).
    for (let propName in properties) {
      if (properties[propName] === null) {
        // Omitted optional argument.
        continue;
      }
      obj[propName] = properties[propName];
    }

    if ("icons" in properties && properties.icons === null && obj.icons) {
      obj.icons = null;
    }

    return obj;
  },

  /**
   * Synchronously retrieve the map of the extension menus.
   * Expected to only be called after ext-menus onStartup lifecycle
   * method has already initialized the ExtensionMenusManager through
   * a call to ExtensionMenus.asyncInitForExtension.
   *
   * @returns {Map<MenuId, object>} Map of the menus createProperties keyed by
   * menu id.
   */
  getMenus(extension) {
    return this._getManager(extension).getMenus();
  },

  _getStoredMenusForTesting(extensionId, extensionVersion) {
    return store.getPersistedMenus(extensionId, extensionVersion);
  },

  _hasStoredExtensionData(extensionId) {
    return store.hasExtensionData(extensionId);
  },

  _getStoreForTesting() {
    return store;
  },

  _recreateStoreForTesting() {
    store = new ExtensionMenusStore();
    return store;
  },

  /**
   * Add a new extension menu for the given extension. A DeferredTask
   * will update all menus data stored on disk for extensions that should
   * persist menus across sessions.
   *
   * Used by menus.create API method.
   *
   * @param {Extension} extension
   * @param {object} createProperties The properties for the
   * newly created menu.
   */
  addMenu(extension, createProperties) {
    // Only keep properties that are necessary.
    const menuProperties = this.mergeMenuProperties({}, createProperties);
    return this._getManager(extension).updateMenus(menuProperties);
  },

  /**
   * Update menu data and optionally reparent menus (used when the update to
   * a menu includes a different parentId). A DeferredTask scheduled by this
   * method will update all menus data stored on disk for extensions that should
   * persist menus across sessions.
   *
   * Used by menus.update API method.
   *
   * @param {Extension} extension
   * @param {MenuId}    menuId           The id of the menu to be updated.
   * @param {object}    updateProperties The properties to update on an existing
   * menu.
   * be reparented.
   */
  updateMenu(extension, menuId, updateProperties) {
    let menuProperties = this.getMenus(extension).get(menuId);
    let needsReparenting =
      updateProperties.parentId != null &&
      menuProperties.parentId != updateProperties.parentId;
    // Only keep properties that are necessary.
    menuProperties = this.mergeMenuProperties(
      this.getMenus(extension).get(menuId),
      updateProperties
    );
    return this._getManager(extension).updateMenus(
      menuProperties,
      needsReparenting
    );
  },

  /**
   * Delete the given menu ids. A DeferredTask will update all menus
   * data stored on disk for extensions that should persist menus across sessions.
   *
   * @param {Extension} extension
   * @param {Array<MenuId>} menuIds Array of menu ids to remove.
   */
  deleteMenus(extension, menuIds) {
    this._getManager(extension).deleteMenus(menuIds);
  },

  /**
   * Delete all menus. A DeferredTask scheduled by this method will update all menus
   * data stored on disk for extensions that should persist menus across sessions.
   *
   * @param {Extension} extension
   */
  deleteAllMenus(extension) {
    this._getManager(extension).deleteAllMenus();
  },

  /**
   * Remove the entry for the given extensionId from the data stored on disk (if any).
   *
   * @param {string} extensionId
   *
   * @returns {Promise<void>} A promise resolved when the extension data has been
   * removed from the store.
   */
  clearPersistedMenusOnUninstall(extensionId) {
    return store.clearPersistedMenusOnUninstall(extensionId);
  },
};
