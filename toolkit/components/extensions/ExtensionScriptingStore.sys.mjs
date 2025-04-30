/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { StartupCache } from "resource://gre/modules/ExtensionParent.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  KeyValueService: "resource://gre/modules/kvstore.sys.mjs",
});

class Store {
  async _init() {
    const { path: storePath } = lazy.FileUtils.getDir("ProfD", [
      "extension-store",
    ]);
    // Make sure the folder exists.
    await IOUtils.makeDirectory(storePath, { ignoreExisting: true });
    this._store = await lazy.KeyValueService.getOrCreateWithOptions(
      storePath,
      "scripting-contentScripts",
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
   * Returns all the stored scripts for a given extension (ID).
   *
   * @param {string} extensionId An extension ID
   * @returns {Promise<Array>} An array of scripts
   */
  async getAll(extensionId) {
    await this.lazyInit();
    const pairs = await this.getByExtensionId(extensionId);

    return pairs.map(([_, script]) => script);
  }

  /**
   * Writes all the scripts provided for a given extension (ID) to the internal
   * store (which is eventually stored on disk).
   *
   * We store each script of an extension as a key/value pair where the key is
   * `<extensionId>/<scriptId>` and the value is the corresponding script
   * details as a JSON string.
   *
   * The format on disk should look like this one:
   *
   * ```
   * {
   *   "@extension-id/script-1": {"id: "script-1", <other props>},
   *   "@extension-id/script-2": {"id: "script-2", <other props>}
   * }
   * ```
   *
   * @param {string} extensionId An extension ID
   * @param {Array} scripts An array of scripts to store on disk
   */
  async writeMany(extensionId, scripts) {
    await this.lazyInit();

    return this._store.writeMany(
      scripts.map(script => [
        `${extensionId}/${script.id}`,
        JSON.stringify(script),
      ])
    );
  }

  /**
   * Deletes all the stored scripts for a given extension (ID).
   *
   * @param {string} extensionId An extension ID
   */
  async deleteAll(extensionId) {
    await this.lazyInit();
    const pairs = await this.getByExtensionId(extensionId);

    return Promise.all(pairs.map(([key, _]) => this._store.delete(key)));
  }

  /**
   * Returns an array of key/script pairs from the internal store belonging to
   * the given extension (ID).
   *
   * The data returned by this method should look like this (assuming we have
   * two scripts named `script-1` and `script-2` for the extension with ID
   * `@extension-id`):
   *
   * ```
   * [
   *   ["@extension-id/script-1", {"id: "script-1", <other props>}],
   *   ["@extension-id/script-2", {"id: "script-2", <other props>}]
   * ]
   * ```
   *
   * @param {string} extensionId An extension ID
   * @returns {Promise<Array>} An array of key/script pairs
   */
  async getByExtensionId(extensionId) {
    await this.lazyInit();

    const entries = [];
    // Retrieve all the scripts registered for the given extension ID by
    // enumerating all keys that are stored in a lexical order.
    const enumerator = await this._store.enumerate(
      `${extensionId}/`, // from_key (inclusive)
      `${extensionId}0` // to_key (exclusive)
    );

    while (enumerator.hasMoreElements()) {
      const { key, value } = enumerator.getNext();
      entries.push([key, JSON.parse(value)]);
    }

    return entries;
  }
}

const store = new Store();

/**
 * Given an extension and some content script options, this function returns
 * the content script representation we use internally, which is an object with
 * a `scriptId` and a nested object containing `options`. These (internal)
 * objects are shared with all content processes using IPC/sharedData.
 *
 * This function can optionally prepend the extension's base URL to the CSS and
 * JS paths, which is needed when we load internal scripts from the scripting
 * store (because the UUID in the base URL changes).
 *
 * @param {Extension} extension
 *        The extension that owns the content script.
 * @param {object} options
 *        Content script options.
 * @param {boolean} prependBaseURL
 *        Whether to prepend JS and CSS paths with the extension's base URL.
 *
 * @returns {object}
 */
export const makeInternalContentScript = (
  extension,
  options,
  prependBaseURL = false
) => {
  let cssPaths = options.css || [];
  let jsPaths = options.js || [];

  if (prependBaseURL) {
    cssPaths = cssPaths.map(css => `${extension.baseURL}${css}`);
    jsPaths = jsPaths.map(js => `${extension.baseURL}${js}`);
  }

  return {
    scriptId: ExtensionUtils.getUniqueId(),
    options: {
      // We need to store the user-supplied script ID for persisted scripts.
      id: options.id,
      allFrames: options.allFrames || false,
      // Although this flag defaults to true with MV3, it is not with MV2.
      // Check permissions at runtime since we aren't checking permissions
      // upfront.
      checkPermissions: true,
      cssPaths,
      excludeMatches: options.excludeMatches,
      jsPaths,
      matches: options.matches,
      matchOriginAsFallback: options.matchOriginAsFallback || false,
      originAttributesPatterns: null,
      persistAcrossSessions: options.persistAcrossSessions,
      runAt: options.runAt || "document_idle",
      world: options.world || "ISOLATED",
    },
  };
};

/**
 * Given an internal content script registered with the "scripting" API (and an
 * extension), this function returns a new object that matches the public
 * "scripting" API.
 *
 * This function is primarily in `scripting.getRegisteredContentScripts()`.
 *
 * @param {Extension} extension
 *        The extension that owns the content script.
 * @param {object} internalScript
 *        An internal script (see also: `makeInternalContentScript()`).
 *
 * @returns {object}
 */
export const makePublicContentScript = (extension, internalScript) => {
  let script = {
    id: internalScript.id,
    allFrames: internalScript.allFrames,
    matches: internalScript.matches,
    matchOriginAsFallback: internalScript.matchOriginAsFallback,
    runAt: internalScript.runAt,
    world: internalScript.world,
    persistAcrossSessions: internalScript.persistAcrossSessions,
  };

  if (internalScript.cssPaths.length) {
    script.css = internalScript.cssPaths.map(cssPath =>
      cssPath.replace(extension.baseURL, "")
    );
  }

  if (internalScript.excludeMatches?.length) {
    script.excludeMatches = internalScript.excludeMatches;
  }

  if (internalScript.jsPaths.length) {
    script.js = internalScript.jsPaths.map(jsPath =>
      jsPath.replace(extension.baseURL, "")
    );
  }

  return script;
};

export const ExtensionScriptingStore = {
  async initExtension(extension) {
    let scripts;

    // On downgrades/upgrades (and re-installation on top of an existing one),
    // we do clear any previously stored scripts and return earlier.
    switch (extension.startupReason) {
      case "ADDON_INSTALL":
      case "ADDON_UPGRADE":
      case "ADDON_DOWNGRADE":
        // On extension upgrades/downgrades the StartupCache data for the
        // extension would already be cleared, and so we set the hasPersistedScripts
        // flag here just to avoid having to check that (by loading the rkv store data)
        // on the next startup.
        StartupCache.general.set(
          [extension.id, extension.version, "scripting", "hasPersistedScripts"],
          false
        );
        store.deleteAll(extension.id);
        return;
    }

    const hasPersistedScripts = await StartupCache.get(
      extension,
      ["scripting", "hasPersistedScripts"],
      async () => {
        scripts = await store.getAll(extension.id);
        return !!scripts.length;
      }
    );

    if (!hasPersistedScripts) {
      return;
    }

    // Load the scripts from the storage, then convert them to their internal
    // representation and add them to the extension's registered scripts.
    scripts ??= await store.getAll(extension.id);

    scripts.forEach(script => {
      const { scriptId, options } = makeInternalContentScript(
        extension,
        script,
        true /* prepend the css/js paths with the extension base URL */
      );
      extension.registeredContentScripts.set(scriptId, options);
    });
  },

  getInitialScriptIdsMap(extension) {
    // This returns the current map of public script IDs to internal IDs.
    // `extension.registeredContentScripts` is initialized in `initExtension`,
    // which may be updated later via the scripting API. In practice, the map
    // of script IDs is retrieved before any scripting API method is exposed,
    // so the return value always matches the initial result from
    // `initExtension`.
    return new Map(
      Array.from(extension.registeredContentScripts.entries())
        .filter(
          // Filter out entries without an options.id property, which are the
          // ones registered through the contentScripts API namespace where the
          // id attribute is not allowed, while it is mandatory for the
          // scripting API namespace.
          ([_id, options]) => options.id?.length
        )
        .map(([scriptId, options]) => [options.id, scriptId])
    );
  },

  async persistAll(extension) {
    // We only persist the scripts that should be persisted and we convert each
    // script to their "public" representation before storing them. This is
    // because we don't want to deal with data migrations if we ever want to
    // change the internal representation (the "public" representation is less
    // likely to change because it is bound to the public scripting API).
    const scripts = Array.from(extension.registeredContentScripts.values())
      .filter(options => options.persistAcrossSessions)
      .map(options => makePublicContentScript(extension, options));

    // We want to replace all the scripts for the extension so we should delete
    // the existing ones first, and then write the new ones.
    //
    // TODO: Bug 1783131 - Implement individual updates without requiring all
    // data to be erased and written.
    await store.deleteAll(extension.id);
    await store.writeMany(extension.id, scripts);
    StartupCache.general.set(
      [extension.id, extension.version, "scripting", "hasPersistedScripts"],
      !!scripts.length
    );
  },

  // Delete all the persisted scripts for the given extension (id).
  //
  // NOTE: to be only used on addon uninstall, the extension entry in the StartupCache
  // is expected to also be fully cleared as part of handling the addon uninstall.
  async clearOnUninstall(extensionId) {
    await store.deleteAll(extensionId);
  },

  // As its name implies, don't use this method for anything but an easy access
  // to the internal store for testing purposes.
  _getStoreForTesting() {
    return store;
  },
};
