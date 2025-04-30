/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { ExtensionTaskScheduler } from "resource://gre/modules/ExtensionTaskScheduler.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  KeyValueService: "resource://gre/modules/kvstore.sys.mjs",
});

/**
 * User scripts are internally represented as the options to pass to the
 * WebExtensionContentScript constructor in the child.
 *
 * @typedef {WebExtensionContentScriptInit & {matches: string[]}} InternalUserScript
 *
 * The internal representation is derived from the public representation as
 * defined in user_scripts.json.
 *
 * @typedef {object} RegisteredUserScript
 */

/**
 * WorldProperties as received from userScripts.configureWorld.
 *
 * @typedef {object} WorldProperties
 * @property {string} worldId
 * @property {string|null} csp
 */

/**
 * User scripts are stored in the following format in a RKV database. A RKV
 * database is a KeyValue store where the keys are ordered lexicographically.
 *
 * Common operations are querying/removing from extensions.
 * UserScript IDs are arbitrary strings that do not start with _.
 *
 * <extensionId>/_script_/<script_id>
 */
class Store {
  async _init() {
    // NOTE: Ideally this would be the same location as other extension rkv
    // databases, but we cannot due to bug 1807010. So the directory name
    // chosen here differs from those that use rkv in other places, such as
    // ExtensionPermissions.sys.mjs and ExtensionScriptingStore.sys.mjs.
    const storePath = PathUtils.join(
      PathUtils.profileDir,
      "extension-store-userscripts"
    );
    await IOUtils.makeDirectory(storePath);
    this._store = await lazy.KeyValueService.getOrCreateWithOptions(
      storePath,
      "userScripts",
      { strategy: lazy.KeyValueService.RecoveryStrategy.RENAME }
    );
  }

  lazyInit() {
    if (!this._initPromise) {
      this._initPromise = this._init();
    }

    return this._initPromise;
  }

  _uninitForTesting() {
    this._store = null;
    this._initPromise = null;
  }

  /**
   * Retrieve all pairs, optionally with a range to select only the key-value
   * pairs from keys between fromKey (inclusive) and toKey (exclusive), with
   * the keys in lexicographical order.
   *
   * @param {string} [fromKey]
   * @param {string} [toKey]
   */
  async getAllEntries(fromKey, toKey) {
    await this.lazyInit();
    /** @type {Array<[string, *]>} */
    const pairs = [];
    const enumerator = await this._store.enumerate(fromKey, toKey);
    while (enumerator.hasMoreElements()) {
      const { key, value } = enumerator.getNext();
      pairs.push([key, JSON.parse(value)]);
    }
    return pairs;
  }

  /**
   * Write all pairs. If the value is the null value, the key is deleted
   * Otherwise the values must be JSON-serializable and are written.
   *
   * TODO: Drop if we move from rkv to skv and skv supports JSON (bug 1919618).
   *
   * @param {Array<[string,*]>} pairs
   */
  async writeMany(pairs) {
    pairs = pairs.map(([k, v]) => {
      return [k, v == null ? null : JSON.stringify(v)];
    });
    await this.lazyInit();
    return this._store.writeMany(pairs);
  }

  async deleteRange(fromKey, toKey) {
    await this.lazyInit();

    // TODO bug 1919530: Use efficient deleteRange if we switch to skv:
    // return this._store.deleteRange(fromKey, toKey);

    // skv supports deleteRange() with bug 1919674, but rkv does not. So as
    // part of deletion, we have to query the values despite not needing it.
    const enumerator = await this._store.enumerate(fromKey, toKey);
    const pairs = [];
    while (enumerator.hasMoreElements()) {
      const { key } = enumerator.getNext();
      pairs.push([key, null]);
    }
    return this._store.writeMany(pairs);
  }
}

const store = new Store();

const userScriptTaskQueues = new ExtensionTaskScheduler();

/**
 * Manages registered user scripts. At initialization, user scripts are loaded
 * from a database on disk. They are converted to an internal format and shared
 * with content processes with sharedData IPC (see makeInternalContentScript),
 * stored in extension.registeredContentScripts.
 *
 * The in-memory registration in extension.registeredContentScripts is the main
 * representation of registered user scripts. The database is only accessed at
 * startup (to load previous registrations) and on modifications.
 *
 * User script code strings are converted to blob:-URLs for use by content
 * processes, and also managed here.
 */
class UserScriptsManager {
  constructor(extension) {
    extension.callOnClose(this);

    this.extension = extension;
    this.blobUrls = new Set();

    // Mapping from public script ID to the internal scriptId. This scriptId
    // is used by the content process to identify scripts when updating the
    // internal representation of the script via IPC, and also used as the key
    // in the extension.registeredContentScripts map.
    this.scriptIdsMap = new Map();

    this.worldConfigs = new Map();
  }

  runReadTask(callback) {
    return userScriptTaskQueues.runReadTask(this.extension.id, callback);
  }

  runWriteTask(callback) {
    return userScriptTaskQueues.runWriteTask(this.extension.id, callback);
  }

  close() {
    // Note: when an extension unloads, the content process clears registered
    // scripts. The parent process will GC extension.registeredContentScripts
    // eventually.
    this.scriptIdsMap.clear();
    this.#revokeUnusedBlobUrls();
    this.extension.userScriptsManager = null;
  }

  #makeDbKey(publicId = "", type = "_script_") {
    return `${this.extension.id}/${type}/${publicId}`;
  }

  async initializeFromDatabase() {
    let worldConfigInitPromise = this.#initializeWorldsFromDatabase();
    const dbScriptEntries = await store.getAllEntries(
      `${this.extension.id}/_script_/`,
      `${this.extension.id}/_script_0`
    );

    // Init worlds before registering scripts, to make sure that if the scripts
    // are injected, that thet have the expected world configuration.
    await worldConfigInitPromise;

    // The database returns them in lexicographical order, which enables
    // extensions to customize the order, as desired:
    // https://github.com/w3c/webextensions/issues/606
    const publicScripts = dbScriptEntries.map(([_m, script]) => script);
    await this.registerNewScripts(publicScripts, /* isReadFromDB */ true);
  }

  /**
   * Register user scripts internally. Also updates the database, unless isAPI
   * is false. Caller should make sure that each script has a unique ID.
   *
   * @param {RegisteredUserScript[]} publicScripts
   * @param {boolean} [isReadFromDB=false]
   * @throws {ExtensionError} if any of the scripts are invalid.
   */
  async registerNewScripts(publicScripts, isReadFromDB = false) {
    if (!isReadFromDB) {
      // Additional validation when userScripts.register() was called.
      // (Presumably the data read from the database is valid.)
      for (let publicScript of publicScripts) {
        if (this.scriptIdsMap.has(publicScript.id)) {
          throw new ExtensionUtils.ExtensionError(
            `User script with id "${publicScript.id}" is already registered.`
          );
        }
        this.#ensureValidUserScript(publicScript);
      }
    }

    // All valid, we don't expect code below to throw.

    let newInternalScripts = [];
    for (let publicScript of publicScripts) {
      let script = this.#makeInternalUserScript(publicScript);
      let scriptId = ExtensionUtils.getUniqueId();
      this.scriptIdsMap.set(publicScript.id, scriptId);
      this.extension.registeredContentScripts.set(scriptId, script);
      newInternalScripts.push({ scriptId, options: script });
    }

    this.extension.updateContentScripts();

    let promise;
    if (this.extension.shouldSendSharedData()) {
      // Broadcast changes to existing processes if we are past startup.
      promise = this.extension.broadcast("Extension:RegisterContentScripts", {
        id: this.extension.id,
        scripts: newInternalScripts,
      });
    }

    if (!isReadFromDB) {
      await store.writeMany(
        publicScripts.map(script => [this.#makeDbKey(script.id), script])
      );
    }

    await promise;
  }

  /**
   * Fully or partially update an existing script. Partial refers to the script
   * description; when this method returns either all scripts have been updated,
   * or none. Caller should make sure that each script has a unique ID.
   *
   * @param {RegisteredUserScript[]} partialPublicScripts
   * @throws {ExtensionError} if any of the updated script are invalid.
   */
  async updateScripts(partialPublicScripts) {
    let scriptsToUpdate = [];
    try {
      for (let partialPublicScript of partialPublicScripts) {
        let publicId = partialPublicScript.id;
        let scriptId = this.scriptIdsMap.get(publicId);
        let oldScript = this.extension.registeredContentScripts.get(scriptId);
        if (!oldScript) {
          throw new ExtensionUtils.ExtensionError(
            `User script with id "${publicId}" does not exist.`
          );
        }
        let newScript = this.#makeInternalUserScript(
          partialPublicScript,
          oldScript
        );
        this.#ensureValidUserScript(newScript);
        scriptsToUpdate.push({ scriptId, options: newScript });
      }
    } catch (e) {
      // The above #makeInternalUserScript() call may create new blob:-URLs,
      // but if a validation error occurred, they are never going to be used,
      // so we can revoke them.
      this.#revokeUnusedBlobUrls();
      throw e;
    }

    // All valid, we don't expect code below to throw.

    for (const { scriptId, options } of scriptsToUpdate) {
      this.extension.registeredContentScripts.set(scriptId, options);
    }
    this.extension.updateContentScripts();
    let promise = this.extension.broadcast("Extension:UpdateContentScripts", {
      id: this.extension.id,
      scripts: scriptsToUpdate,
    });

    // To save in the database, we need the stable public representation. This
    // is async because the code URLs have to be resolved to code strings.
    let publicScripts = await Promise.all(
      scriptsToUpdate.map(({ options }, i) => {
        let publicId = partialPublicScripts[i].id;
        return this.#makePublicUserScript(publicId, options);
      })
    );
    await store.writeMany(
      publicScripts.map(script => [this.#makeDbKey(script.id), script])
    );
    await promise;

    // When succeeded, we may have to revoke blob:-URLs of old scripts. Do this
    // last, after the content processes have processed the update, so that we
    // know for sure that nothing is going to use the old blob:-URLs any more.
    this.#revokeUnusedBlobUrls();
  }

  /**
   * @param {string[]} [publicScriptIds]
   */
  async unregisterScripts(publicScriptIds) {
    publicScriptIds = this.#filterExistingPublicScriptIds(publicScriptIds);
    let scriptIds = [];
    for (let publicId of publicScriptIds) {
      let scriptId = this.scriptIdsMap.get(publicId);
      this.extension.registeredContentScripts.delete(scriptId);
      this.scriptIdsMap.delete(publicId);
      scriptIds.push(scriptId);
    }
    this.extension.updateContentScripts();
    let promise = this.extension.broadcast(
      "Extension:UnregisterContentScripts",
      { id: this.extension.id, scriptIds }
    );
    await store.writeMany(
      publicScriptIds.map(id => [this.#makeDbKey(id), null])
    );
    await promise;
    // Revoke once the content process has acknowledged unregistration, so that
    // they are not going to use the blob:-URL any more.
    this.#revokeUnusedBlobUrls();
  }

  /**
   * @param {string[]} [publicScriptIds]
   */
  async getScripts(publicScriptIds) {
    publicScriptIds = this.#filterExistingPublicScriptIds(publicScriptIds);
    return Promise.all(
      publicScriptIds.map(publicId => {
        let scriptId = this.scriptIdsMap.get(publicId);
        return this.#makePublicUserScript(
          publicId,
          this.extension.registeredContentScripts.get(scriptId)
        );
      })
    );
  }

  async #initializeWorldsFromDatabase() {
    const dbWorldEntries = await store.getAllEntries(
      `${this.extension.id}/_world_/`,
      `${this.extension.id}/_world_0`
    );

    const allProperties = dbWorldEntries.map(([, properties]) => properties);
    for (let properties of allProperties) {
      this.worldConfigs.set(properties.worldId, properties);
    }
    this.extension.setSharedData("userScriptsWorldConfigs", this.worldConfigs);
    if (this.extension.shouldSendSharedData()) {
      // Broadcast changes to existing processes if we are past startup.
      await this.extension.broadcast("Extension:UpdateUserScriptWorlds", {
        id: this.extension.id,
        reset: null,
        update: allProperties,
      });
    }
  }

  /**
   * @param {WorldProperties} properties
   */
  async configureWorld(properties) {
    const worldId = properties.worldId;
    this.worldConfigs.set(worldId, properties);

    this.extension.setSharedData("userScriptsWorldConfigs", this.worldConfigs);
    await this.extension.broadcast("Extension:UpdateUserScriptWorlds", {
      id: this.extension.id,
      reset: null,
      update: [properties],
    });
    await store.writeMany([[this.#makeDbKey(worldId, "_world_"), properties]]);
  }

  /**
   * @param {string} worldId
   */
  async resetWorldConfiguration(worldId) {
    if (!this.worldConfigs.delete(worldId)) {
      return;
    }

    this.extension.setSharedData("userScriptsWorldConfigs", this.worldConfigs);
    await this.extension.broadcast("Extension:UpdateUserScriptWorlds", {
      id: this.extension.id,
      reset: [worldId],
      update: null,
    });
    await store.writeMany([[this.#makeDbKey(worldId, "_world_"), null]]);
  }

  /** @returns {Promise<WorldProperties[]>} */
  async getWorldConfigurations() {
    return Array.from(this.worldConfigs.values());
  }

  // userScripts.getScripts & userScripts.unregister accepts an ids filter.
  // It may contain non-existing IDs..
  #filterExistingPublicScriptIds(publicScriptIds) {
    if (publicScriptIds) {
      return publicScriptIds.filter(id => this.scriptIdsMap.has(id));
    }
    return Array.from(this.scriptIdsMap.keys());
  }

  #getJsPathUrlForCode(code) {
    // TODO: Consider data:-URLs for small chunks of code?
    let blobUrl = URL.createObjectURL(
      new Blob([code], { type: "text/javascript" })
    );
    this.blobUrls.add(blobUrl);
    return blobUrl;
  }

  #revokeUnusedBlobUrls() {
    let urlsToKeep = new Set();
    for (let scriptId of this.scriptIdsMap.values()) {
      let internalScript =
        this.extension.registeredContentScripts.get(scriptId);
      for (let jsPath of internalScript.jsPaths) {
        if (jsPath.startsWith("blob:")) {
          urlsToKeep.add(jsPath);
        }
      }
    }
    for (let blobUrl of this.blobUrls) {
      if (!urlsToKeep.has(blobUrl)) {
        URL.revokeObjectURL(blobUrl);
        this.blobUrls.delete(blobUrl);
      }
    }
  }

  // static because the functionality may be needed even without an Extension,
  // e.g. from clearOnUninstall.
  static async deleteAll(extensionId) {
    await store.deleteRange(`${extensionId}/`, `${extensionId}0`);
  }

  /**
   * Converts the public representation of a user script to the internal format
   * as expected by the WebExtensionContentScript constructor, and shared with
   * all content processes via sharedData IPC. The public representation can be
   * from the userScripts.register or userScripts.update APIs, or the database.
   *
   * In case of userScripts.update, the previous representaton of the internal
   * script can be passed via the oldScript parameter to allow for the object
   * to be completed.
   *
   * @param {RegisteredUserScript | object} publicScript
   *        A complete or partial representation of a user script, in the
   *        RegisteredUserScript format. Must be a complete representation if
   *        oldScript is not specified.
   * @param {InternalUserScript} [oldScript]
   *        The existing internal representation of a script, if |publicScript|
   *        is a partial update.
   *
   * @returns {InternalUserScript}
   */
  #makeInternalUserScript(publicScript, oldScript = null) {
    let jsPaths = oldScript?.jsPaths;
    if (publicScript.js) {
      jsPaths = publicScript.js.map(({ file, code }) => {
        if (file != null) {
          // file is a relative path, whether from userScripts.register/update,
          // which is enforced by the unresolvedRelativeUrl schema type, or from
          // the database. Turn into absolute moz-extension:-URL so that the URL
          // can be resolved to its actual source at injection time.
          return this.extension.getURL(file);
        }
        return this.#getJsPathUrlForCode(code);
      });
    }
    const nonEmptyOrNull = arr => (arr?.length ? arr : null);
    return {
      isUserScript: true,
      // Note: id not set because we don't need it internally. The absence of
      // the "id" property is also needed to hide this internal script from the
      // scripting API, in ExtensionScriptingStore.getInitialScriptIdsMap.
      allFrames: publicScript.allFrames ?? oldScript?.allFrames ?? false,
      jsPaths,
      // WebExtensionContentScript requires matches to be set to an Array.
      // Although "matches" is optional in the userScripts API (because
      // includeGlobs can be used instead with OR semantics), it is required
      // for most other use cases (content scripts and MozDocumentMatcher). For
      // clarity, WebExtensionContentScript.webidl therefore marks "matches" as
      // a required array, and we fall back to an empty array if needed.
      matches: publicScript.matches || oldScript?.matches || [],
      excludeMatches: nonEmptyOrNull(
        publicScript.excludeMatches || oldScript?.excludeMatches
      ),
      includeGlobs: nonEmptyOrNull(
        publicScript.includeGlobs || oldScript?.includeGlobs
      ),
      excludeGlobs: nonEmptyOrNull(
        publicScript.excludeGlobs || oldScript?.excludeGlobs
      ),
      runAt: publicScript.runAt || oldScript?.runAt || "document_idle",
      world: publicScript.world || oldScript?.world || "USER_SCRIPT",
      worldId: publicScript.worldId ?? oldScript?.worldId ?? "",
    };
  }

  /**
   * Converts the internal in-memory representation of a registered user script
   * to the public RegisteredUserScript type as defined in the userScripts API.
   *
   * @param {string} publicId
   *        The public script ID chosen by the extension (not used internally).
   * @param {InternalUserScript} internalScript
   *        The internal representation, see #makeInternalUserScript().
   *
   * @returns {Promise<object>}
   */
  async #makePublicUserScript(publicId, internalScript) {
    let hasCodePromise = false;
    /** @type {(Promise<{code: string}> | {file?: string, code?: string})[]} */
    let js = internalScript.jsPaths.map(jsPath => {
      if (jsPath.startsWith(this.extension.baseURL)) {
        // Return path without leading origin & /.
        return { file: jsPath.slice(this.extension.baseURL.length) };
      }
      hasCodePromise = true;
      // blob:-URL generated via #makeInternalUserScript() from "code" option.
      return fetch(jsPath)
        .then(res => res.text())
        .then(code => ({ code }));
    });

    if (hasCodePromise) {
      js = await Promise.all(js);
    }

    // Properties match order of RegisteredUserScript in user_scripts.json.
    let script = {
      id: publicId,
      allFrames: internalScript.allFrames,
      js,
      // See #makeInternalUserScript - "matches" is internally required, but if
      // it is an empty array, it is semantically equivalent to null.
      matches: internalScript.matches.length ? internalScript.matches : null,
      excludeMatches: internalScript.excludeMatches,
      includeGlobs: internalScript.includeGlobs,
      excludeGlobs: internalScript.excludeGlobs,
      runAt: internalScript.runAt,
      world: internalScript.world,
      worldId: internalScript.worldId,
    };

    return script;
  }

  /**
   * Some requirements of the RegisteredUserScript type are not validated
   * by the schema, because that is not feasible. E.g. to determine whether
   * an update is valid, we need to apply the update and check the final
   * result.
   *
   * @param {RegisteredUserScript|InternalUserScript} script
   */
  #ensureValidUserScript(script) {
    // Due to similarities between RegisteredUserScript and InternalUserScript,
    // we can re-use the same logic for both types.

    // Note: unlike similar logic in the scripting API, the userScripts API
    // permits an empty js array. The "js" property is guaranteed to exist
    // because it is a required key in userScripts.register.

    if (!script.matches?.length && !script.includeGlobs?.length) {
      throw new ExtensionUtils.ExtensionError(
        "matches or includeGlobs must be specified."
      );
    }

    if (script.matches) {
      // This will throw if a match pattern is invalid.
      ExtensionUtils.parseMatchPatterns(script.matches);
    }

    if (script.excludeMatches) {
      // This will throw if a match pattern is invalid.
      ExtensionUtils.parseMatchPatterns(script.excludeMatches);
    }

    if (script.worldId && script.world === "MAIN") {
      // worldId is only supported with world USER_SCRIPT (default).
      throw new ExtensionUtils.ExtensionError(
        "worldId cannot be used with MAIN world."
      );
    }
  }
}

export const ExtensionUserScripts = {
  async initExtension(extension) {
    if (extension.userScriptsManager) {
      throw new Error(`UserScriptsManager already exists for ${extension.id}`);
    }
    extension.userScriptsManager = new UserScriptsManager(extension);

    // userScripts API data persists across browser updates, but is cleared
    // whenever extensions update.
    switch (extension.startupReason) {
      case "ADDON_INSTALL":
      case "ADDON_UPGRADE":
      case "ADDON_DOWNGRADE":
        // Since we start with an empty state, and deletion will also result in
        // an empty state, we do not need to await the completion of deleteAll.
        // When a userScripts API call happens, it will be queued as desired.
        extension.userScriptsManager.runWriteTask(() =>
          UserScriptsManager.deleteAll(extension.id)
        );
        return;
    }

    // We consider initialization from database a write task because it should
    // block any other read/write tasks until the initialization has completed.
    await extension.userScriptsManager.runWriteTask(() =>
      extension.userScriptsManager.initializeFromDatabase()
    );
  },

  async clearOnUninstall(extensionId) {
    await userScriptTaskQueues.runWriteTask(extensionId, () =>
      UserScriptsManager.deleteAll(extensionId)
    );
  },

  // As its name implies, don't use this method for anything but an easy access
  // to the internal store for testing purposes.
  _getStoreForTesting() {
    return store;
  },
};
