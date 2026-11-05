/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kStorageVersion = 1;

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * Returns a JSON schema validator for Taskbar Tabs persistent storage.
 *
 * @returns {Promise<Validator>} Resolves to JSON schema validator for Taskbar Tab's persistent storage.
 */
async function getJsonSchema() {
  const kJsonSchema =
    "chrome://browser/content/taskbartabs/TaskbarTabs.1.schema.json";
  let res = await fetch(kJsonSchema);
  let obj = await res.json();
  return new lazy.JsonSchema.Validator(obj);
}

/**
 * Storage class for a single Taskbar Tab's persistent storage.
 */
class TaskbarTab {
  // Unique identifier for the Taskbar Tab.
  #id;
  // List of scopes associated with this Taskbar Tab. A scope has a 'hostname'
  // property, and a 'prefix' property. If a 'prefix' is set, then the path
  // must literally start with that prefix; this matches the 'within scope'
  // algorithm of the Web App Manifest specification.
  //
  // @type {{ hostname: string; [prefix]: string }[]}
  #scopes = [];
  // Container the Taskbar Tab is opened in when opened from the Taskbar.
  #userContextId;
  // URL opened when a Taskbar Tab is opened from the Taskbar.
  #startUrl;
  // Human-readable name of this Taskbar Tab.
  #name;
  // The path to the shortcut associated with this Taskbar Tab, *relative
  // to the `Start Menu\Programs` folder.* Note that this shortcut might not
  // necessarily be pinned, or even exist (although it should have existed when
  // this property was initially set).
  #shortcutRelativePath;

  constructor({
    id,
    scopes,
    startUrl,
    name,
    userContextId,
    shortcutRelativePath,
  }) {
    this.#id = id;
    this.#scopes = scopes;
    this.#userContextId = userContextId;
    this.#startUrl = startUrl;
    this.#name = name;

    this.#shortcutRelativePath = shortcutRelativePath ?? null;
  }

  get id() {
    return this.#id;
  }

  get scopes() {
    return [...this.#scopes];
  }

  get userContextId() {
    return this.#userContextId;
  }

  get startUrl() {
    return this.#startUrl;
  }

  get name() {
    return this.#name;
  }

  get shortcutRelativePath() {
    return this.#shortcutRelativePath;
  }

  /**
   * Whether the provided URL is navigable from the Taskbar Tab.
   *
   * @param {nsIURI} aUrl - The URL to navigate to.
   * @returns {boolean} `true` if the URL is navigable from the Taskbar Tab associated to the ID.
   * @throws {Error} If `aId` is not a valid Taskbar Tabs ID.
   */
  isScopeNavigable(aUrl) {
    let baseDomain = Services.eTLD.getBaseDomain(aUrl);

    for (const scope of this.#scopes) {
      let scopeBaseDomain = Services.eTLD.getBaseDomainFromHost(scope.hostname);

      // Domains in the same base domain are valid navigation targets.
      if (baseDomain === scopeBaseDomain) {
        lazy.logConsole.info(`${aUrl} is navigable for scope ${scope}.`);
        return true;
      }
    }

    lazy.logConsole.info(
      `${aUrl} is not navigable for Taskbar Tab ID ${this.#id}.`
    );
    return false;
  }

  toJSON() {
    const maybe = (self, name) => (self[name] ? { [name]: self[name] } : {});
    return {
      id: this.id,
      scopes: this.scopes,
      userContextId: this.userContextId,
      startUrl: this.startUrl,
      name: this.name,
      ...maybe(this, "shortcutRelativePath"),
    };
  }

  /**
   * Applies mutable fields from aPatch to this object.
   *
   * Always use TaskbarTabsRegistry.patchTaskbarTab instead. Aside
   * from calling into this, it notifies other objects (especially
   * the saver) about the change.
   *
   * @param {object} aPatch - An object with properties to change.
   */
  _applyPatch(aPatch) {
    if ("shortcutRelativePath" in aPatch) {
      this.#shortcutRelativePath = aPatch.shortcutRelativePath;
    }
  }
}

/**
 * Storage class for Taskbar Tabs feature's persistent storage.
 */
export class TaskbarTabsRegistry {
  /**
   * List of registered Taskbar Tabs.
   *
   * @type {TaskbarTab[]}
   */
  #taskbarTabs;
  /**
   * Persistent storage handler for Taskbar Tabs.
   *
   * @type {TaskbarTabsRegistryStorage}
   */
  #storage;

  /**
   * Creates a new registry from the provided array of taskbar tabs and the
   * storage to save to.
   *
   * @param {TaskbarTabsRegistryStorage} aStorage - Storage to save to when
   * changes are made to the registry.
   * @param {TaskbarTab[]} aTaskbarTabs - List of taskbar tabs that already
   * existed.
   */
  constructor(aStorage, aTaskbarTabs) {
    this.#storage = aStorage;
    this.#taskbarTabs = aTaskbarTabs.map(
      tt => new TaskbarTab(migrateStoredTaskbarTab(tt))
    );
  }

  toJSON() {
    return {
      version: kStorageVersion,
      taskbarTabs: this.#taskbarTabs.map(tt => {
        return tt.toJSON();
      }),
    };
  }

  /**
   * Finds or creates a Taskbar Tab based on the provided URL and container.
   *
   * @param {nsIURI} aUrl - The URL to match or derive the scope and start URL from.
   * @param {number} aUserContextId - The container to start a Taskbar Tab in.
   * @param {object} aDetails - Additional options to use if it needs to be
   * created.
   * @param {object} aDetails.manifest - The Web app manifest that should be
   * associated with this Taskbar Tab.
   * @returns {{taskbarTab:TaskbarTab, created:bool}}
   *   The matching or created Taskbar Tab, along with whether it was created.
   */
  findOrCreateTaskbarTab(aUrl, aUserContextId, { manifest = {} } = {}) {
    let existing = this.findTaskbarTab(aUrl, aUserContextId);
    if (existing) {
      return {
        created: false,
        taskbarTab: existing,
      };
    }

    let scope = { hostname: aUrl.host };
    if ("scope" in manifest) {
      // Note: manifest.scope will not be set unless the start_url is
      // within scope. As such, this scope always contains the start_url.
      // If a manifest is used but there isn't a scope, it uses the parent
      // of the start_url; e.g. '/a/b/c.html' --> '/a/b'.
      const scopeUri = Services.io.newURI(manifest.scope);
      scope = {
        hostname: scopeUri.host,
        prefix: scopeUri.filePath,
      };
    }

    let startUrl;
    if (manifest.start_url) {
      startUrl = manifest.start_url;
    } else if (aUrl.schemeIs("moz-extension")) {
      // For extensions, the prePath will give an index of files in the
      // extension. Since this isn't useful, and it isn't easy to get to the
      // original page, remove the query/ref but preserve everything else.
      startUrl = aUrl.mutate().setQuery("").setRef("").finalize().spec;
    } else {
      // For non-extensions, it should be easy enough for the user to navigate
      // to the page they wanted. Without a manifest, we assume this is the
      // homepage of the web app.
      startUrl = aUrl.prePath;
    }

    let id = Services.uuid.generateUUID().toString().slice(1, -1);
    let taskbarTab = new TaskbarTab({
      id,
      scopes: [scope],
      userContextId: aUserContextId,
      name: manifest.name ?? generateName(aUrl),
      startUrl,
    });
    this.#taskbarTabs.push(taskbarTab);

    lazy.logConsole.info(`Created Taskbar Tab with ID ${id}`);

    Glean.webApp.install.record({});
    this.#storage.save(this);

    return {
      created: true,
      taskbarTab,
    };
  }

  /**
   * Removes a Taskbar Tab.
   *
   * @param {string} aId - The ID of the TaskbarTab to remove.
   * @returns {TaskbarTab?} The removed taskbar tab, or null if it wasn't
   * found.
   */
  removeTaskbarTab(aId) {
    let tts = this.#taskbarTabs;
    const i = tts.findIndex(tt => {
      return tt.id === aId;
    });

    if (i > -1) {
      lazy.logConsole.info(`Removing Taskbar Tab Id ${tts[i].id}`);
      let removed = tts.splice(i, 1);

      Glean.webApp.uninstall.record({});
      this.#storage.save(this);
      return removed[0];
    }

    lazy.logConsole.error(`Taskbar Tab ID ${aId} not found.`);
    return null;
  }

  /**
   * Searches for an existing Taskbar Tab matching the URL and Container.
   *
   * @param {nsIURL} aUrl - The URL to match.
   * @param {number} aUserContextId - The container to match.
   * @returns {TaskbarTab|null} The matching Taskbar Tab, or null if none match.
   */
  findTaskbarTab(aUrl, aUserContextId) {
    // Ensure that the caller uses the correct types. nsIURI alone isn't
    // enough---we need to know that there's a hostname and that the structure
    // is otherwise standard.
    if (!(aUrl instanceof Ci.nsIURL)) {
      throw new TypeError(
        "Invalid argument, `aUrl` should be instance of `nsIURL`"
      );
    }
    if (typeof aUserContextId !== "number") {
      throw new TypeError(
        "Invalid argument, `aUserContextId` should be type of `number`"
      );
    }

    for (const tt of this.#taskbarTabs) {
      let bestPrefix = "";
      for (const scope of tt.scopes) {
        if (aUrl.host !== scope.hostname) {
          continue;
        }
        if ("prefix" in scope) {
          if (scope.prefix.length < bestPrefix.length) {
            // We've already found something better.
            continue;
          }
          if (!aUrl.filePath.startsWith(scope.prefix)) {
            // This URL wouldn't be within scope.
            continue;
          }
        }

        if (aUserContextId !== tt.userContextId) {
          lazy.logConsole.info(
            `Matched TaskbarTab for URL ${aUrl.host} to ${scope.hostname}, but container ${aUserContextId} mismatched ${tt.userContextId}.`
          );
        } else {
          lazy.logConsole.info(
            `Matched TaskbarTab for URL ${aUrl.host} to ${scope.hostname} with container ${aUserContextId}.`
          );
          return tt;
        }
      }
    }

    lazy.logConsole.info(
      `No matching TaskbarTab found for URL ${aUrl.spec} and container ${aUserContextId}.`
    );
    return null;
  }

  /**
   * Retrieves the Taskbar Tab matching the ID.
   *
   * @param {string} aId - The ID of the Taskbar Tab.
   * @returns {TaskbarTab} The matching Taskbar Tab.
   * @throws {Error} If `aId` is not a valid Taskbar Tab ID.
   */
  getTaskbarTab(aId) {
    const tt = this.#taskbarTabs.find(aTaskbarTab => {
      return aTaskbarTab.id === aId;
    });
    if (!tt) {
      lazy.logConsole.error(`Taskbar Tab Id ${aId} not found.`);
      throw new Error(`Taskbar Tab Id ${aId} is invalid.`);
    }

    return tt;
  }

  /**
   * Updates properties within the provided Taskbar Tab.
   *
   * All fields from aPatch will be assigned to aTaskbarTab, except
   * for the ID.
   *
   * @param {TaskbarTab} aTaskbarTab - The taskbar tab to update.
   * @param {object} aPatch - An object with properties to change.
   * @throws {Error} If any taskbar tab in aTaskbarTabs is unknown.
   */
  patchTaskbarTab(aTaskbarTab, aPatch) {
    // This is done from the registry to make it more clear that I/O might be
    // possible.
    aTaskbarTab._applyPatch(aPatch);
    this.#storage.save(this);
  }

  /**
   * Gets the number of taskbar tabs that are registered in this registry.
   *
   * @returns {number} The number of registered taskbar tabs.
   */
  countTaskbarTabs() {
    return this.#taskbarTabs.length;
  }

  /**
   * Resets the in-memory Taskbar Tabs state for tests.
   */
  resetForTests() {
    this.#taskbarTabs = [];
  }
}

/**
 * Monitor for the Taskbar Tabs Registry that updates the save file as it
 * changes.
 *
 * Note: this intentionally does not save on schema updates to allow for
 * gracefall rollback to an earlier version of Firefox where possible. This is
 * desirable in cases where a user has unintentioally opened a profile on a
 * newer version of Firefox, or has reverted an update.
 */
export class TaskbarTabsRegistryStorage {
  // The file to save to and load from.
  #file;
  // Promise queue to ensure that async writes don't occur out of order.
  #saveQueue = Promise.resolve();

  /**
   * @param {nsIFile} aFile - The file to load and save.
   */
  constructor(aFile) {
    this.#file = aFile;
  }

  /**
   * Loads the associated file into a TaskbarTabsRegistry object.
   *
   * @returns {TaskbarTabsRegistry} The loaded registry.
   */
  async load() {
    lazy.logConsole.info(`Loading file ${this.#file.path} for Taskbar Tabs.`);

    const [schema, jsonObject] = await Promise.all([
      getJsonSchema(),
      IOUtils.readJSON(this.#file.path).catch(err => {
        // If taskbartabs.json doesn't exist, that's expected (e.g. it could
        // be a new profile.)
        if (err.name !== "NotFoundError") {
          lazy.logConsole.error(
            `Could not read Taskbar Tabs from ${this.#file.path}:`,
            err
          );
        }
        return null;
      }),
    ]);

    if (!jsonObject) {
      return new TaskbarTabsRegistry(this, []);
    }

    if (!schema.validate(jsonObject).valid) {
      throw new Error(
        `JSON from file ${this.#file.path} is invalid for the Taskbar Tabs Schema.`
      );
    }

    if (jsonObject.version > kStorageVersion) {
      throw new Error(`File ${this.#file.path} has an unrecognized version.
      Current Version: ${kStorageVersion}
      File Version: ${jsonObject.version}`);
    }

    return new TaskbarTabsRegistry(this, jsonObject.taskbarTabs);
  }

  /**
   * Serializes the given Taskbar Tabs Registry into a JSON file.
   *
   * Note: file writes are strictly ordered, ensuring the sequence of serialized
   * object writes reflects the latest state even if any individual write
   * serializes the registry in a newer state than when it's associated event
   * was emitted.
   *
   * @param {TaskbarTabsRegistry} aRegistry - The registry to serialize.
   * @returns {Promise} Resolves once the current save operation completes.
   */
  save(aRegistry) {
    this.#saveQueue = this.#saveQueue
      .finally(async () => {
        lazy.logConsole.info(`Updating Taskbar Tabs storage file.`);

        const schema = await getJsonSchema();

        // Copy the JSON object to prevent awaits after validation risking
        // TOCTOU if the registry changes.
        let json = aRegistry.toJSON();

        let result = schema.validate(json);
        if (!result.valid) {
          throw new Error(
            "Generated invalid JSON for the Taskbar Tabs Schema:\n" +
              JSON.stringify(result.errors)
          );
        }

        await IOUtils.makeDirectory(this.#file.parent.path);
        await IOUtils.writeJSON(this.#file.path, json);

        lazy.logConsole.info(`Tasbkar Tabs storage file updated.`);
      })
      .catch(e => {
        lazy.logConsole.error(`Error writing Taskbar Tabs file: ${e}`);
      });

    lazy.AsyncShutdown.profileBeforeChange.addBlocker(
      "Taskbar Tabs: finalizing registry serialization to disk.",
      this.#saveQueue
    );

    return this.#saveQueue;
  }
}

/**
 * Mutates the provided Taskbar Tab object from storage so it contains all
 * current properties.
 *
 * @param {object} aStored - The object stored in the database; this will be
 * mutated as part of migrating it.
 * @returns {object} aStored exactly.
 */
function migrateStoredTaskbarTab(aStored) {
  if (typeof aStored.name !== "string") {
    try {
      aStored.name = generateName(Services.io.newURI(aStored.startUrl));
    } catch (e) {
      lazy.logConsole.warn(`Migrating ${aStored.id} failed:`, e);
    }
  }

  return aStored;
}

/**
 * Generates a name for the Taskbar Tab appropriate for user facing UI.
 *
 * @param {nsIURI} aUri - The URI to derive the name from.
 * @returns {string} A name suitable for user facing UI.
 */
export function generateName(aUri) {
  // https://www.subdomain.example.co.uk/test

  // ["www", "subdomain", "example", "co", "uk"]
  let hostParts = aUri.host.split(".");

  // ["subdomain", "example", "co", "uk"]
  if (hostParts[0] === "www") {
    hostParts.shift();
  }

  let suffixDomainCount = Services.eTLD
    .getKnownPublicSuffix(aUri)
    .split(".").length;

  // ["subdomain", "example"]
  hostParts.splice(-suffixDomainCount);

  let name = hostParts
    // ["example", "subdomain"]
    .reverse()
    // ["Example", "Subdomain"]
    .map(s => s.charAt(0).toUpperCase() + s.slice(1))
    // "Example Subdomain"
    .join(" ");

  return name;
}
