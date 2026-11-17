/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  BaseStorageActor,
  MAX_STORE_OBJECT_COUNT,
  SEPARATOR_GUID,
} = require("resource://devtools/server/actors/resources/storage/index.js");
const {
  LongStringActor,
} = require("resource://devtools/server/actors/string.js");
// We give this a funny name to avoid confusion with the global
// indexedDB.
loader.lazyGetter(this, "indexedDBForStorage", () => {
  // On xpcshell, we can't instantiate indexedDB without crashing
  try {
    const sandbox = Cu.Sandbox(
      Components.Constructor(
        "@mozilla.org/systemprincipal;1",
        "nsIPrincipal"
      )(),
      { wantGlobalProperties: ["indexedDB"] }
    );
    return sandbox.indexedDB;
  } catch (e) {
    return {};
  }
});
const lazy = {};
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
  },
  { global: "contextual" }
);

/**
 * An async method equivalent to setTimeout but using Promises
 *
 * @param {number} time
 *        The wait time in milliseconds.
 */
function sleep(time) {
  return new Promise(resolve => {
    setTimeout(() => {
      resolve(null);
    }, time);
  });
}

const SAFE_HOSTS_PREFIXES_REGEX = /^(about\+|https?\+|file\+|moz-extension\+)/;

// A RegExp for characters that cannot appear in a file/directory name. This is
// used to sanitize the host name for indexed db to lookup whether the file is
// present in <profileDir>/storage/default/ location
const illegalFileNameCharacters = [
  "[",
  // Control characters \001 to \036
  "\\x00-\\x24",
  // Special characters
  '/:*?\\"<>|\\\\',
  "]",
].join("");
const ILLEGAL_CHAR_REGEX = new RegExp(illegalFileNameCharacters, "g");

/**
 * Code related to the Indexed DB actor and front
 */

// Metadata holder objects for various components of Indexed DB

/**
 * Meta data object for a particular index in an object store
 */
class IndexMetadata {
  /**
   * @param {IDBIndex} index
   *        The particular index from the object store.
   */
  constructor(index) {
    this._name = index.name;
    this._keyPath = index.keyPath;
    this._unique = index.unique;
    this._multiEntry = index.multiEntry;
  }
  toObject() {
    return {
      name: this._name,
      keyPath: this._keyPath,
      unique: this._unique,
      multiEntry: this._multiEntry,
    };
  }
}

/**
 * Meta data object for a particular object store in a db
 */
class ObjectStoreMetadata {
  /**
   * @param {IDBObjectStore} objectStore
   *        The particular object store from the db.
   */
  constructor(objectStore) {
    this._name = objectStore.name;
    this._keyPath = objectStore.keyPath;
    this._autoIncrement = objectStore.autoIncrement;
    this._indexes = [];

    for (let i = 0; i < objectStore.indexNames.length; i++) {
      const index = objectStore.index(objectStore.indexNames[i]);

      const newIndex = {
        keypath: index.keyPath,
        multiEntry: index.multiEntry,
        name: index.name,
        objectStore: {
          autoIncrement: index.objectStore.autoIncrement,
          indexNames: [...index.objectStore.indexNames],
          keyPath: index.objectStore.keyPath,
          name: index.objectStore.name,
        },
      };

      this._indexes.push([newIndex, new IndexMetadata(index)]);
    }
  }
  toObject() {
    return {
      name: this._name,
      keyPath: this._keyPath,
      autoIncrement: this._autoIncrement,
      indexes: JSON.stringify(
        Array.from(this._indexes.values().map(index => index.toObject()))
      ),
    };
  }
}

/**
 * Meta data object for a particular indexed db in a host.
 */
class DatabaseMetadata {
  /**
   * @param {string} origin
   *        The host associated with this indexed db.
   * @param {IDBDatabase} db
   *        The particular indexed db.
   * @param {string} storage
   *        Storage type, either "temporary", "default" or "persistent".
   */
  constructor(origin, db, storage) {
    this._origin = origin;
    this._name = db.name;
    this._version = db.version;
    this._objectStores = [];
    this.storage = storage;

    if (db.objectStoreNames.length) {
      const transaction = db.transaction(db.objectStoreNames, "readonly");

      for (let i = 0; i < transaction.objectStoreNames.length; i++) {
        const objectStore = transaction.objectStore(
          transaction.objectStoreNames[i]
        );
        this._objectStores.push([
          transaction.objectStoreNames[i],
          new ObjectStoreMetadata(objectStore),
        ]);
      }
    }
  }
  get objectStores() {
    return this._objectStores;
  }

  toObject() {
    return {
      uniqueKey: `${this._name}${SEPARATOR_GUID}${this.storage}`,
      name: this._name,
      storage: this.storage,
      origin: this._origin,
      version: this._version,
      objectStores: this._objectStores.size,
    };
  }
}

class IndexedDBStorageActor extends BaseStorageActor {
  constructor(storageActor) {
    super(storageActor, "indexedDB");

    this.objectsSize = {};
    this.storageActor = storageActor;
  }

  destroy() {
    this.objectsSize = null;

    super.destroy();
  }

  // We need to override this method because of custom, async getHosts method
  async populateStoresForHosts() {
    for (const host of await this.getHosts()) {
      await this.populateStoresForHost(host);
    }
  }

  async populateStoresForHost(host) {
    const storeMap = new Map();

    const win = this.storageActor.getWindowFromHost(host);
    const principal = this.getPrincipal(win);

    const { names } = await this.getDBNamesForHost(host, principal);

    for (const { name, storage } of names) {
      let metadata = await this.getDBMetaData(host, principal, name, storage);
      if (!metadata) {
        continue;
      }

      metadata = this.patchMetadataMapsAndProtos(metadata);

      storeMap.set(`${name} (${storage})`, metadata);
    }

    this.hostVsStores.set(host, storeMap);
  }

  /**
   * Returns a list of currently known hosts for the target window. This list
   * contains unique hosts from the window, all inner windows and all permanent
   * indexedDB hosts defined inside the browser.
   */
  async getHosts() {
    // Add internal hosts to this._internalHosts, which will be picked up by
    // the this.hosts getter. Because this.hosts is a property on the default
    // storage actor and inherited by all storage actors we have to do it this
    // way.
    // Only look up internal hosts if we are in the browser toolbox
    const isBrowserToolbox = this.storageActor.parentActor.isRootActor;

    this._internalHosts = isBrowserToolbox ? await this.getInternalHosts() : [];

    return this.hosts;
  }

  /**
   * Remove an indexedDB database from given host with a given name.
   */
  async removeDatabase(host, name) {
    const win = this.storageActor.getWindowFromHost(host);
    if (!win) {
      return { error: `Window for host ${host} not found` };
    }

    const principal = win.document.effectiveStoragePrincipal;
    return this.removeDB(host, principal, name);
  }

  async removeAll(host, name) {
    const [db, store] = JSON.parse(name);

    const win = this.storageActor.getWindowFromHost(host);
    if (!win) {
      return;
    }

    const principal = win.document.effectiveStoragePrincipal;
    this.clearDBStore(host, principal, db, store);
  }

  async removeItem(host, name) {
    const [db, store, id] = JSON.parse(name);

    const win = this.storageActor.getWindowFromHost(host);
    if (!win) {
      return;
    }

    const principal = win.document.effectiveStoragePrincipal;
    this.removeDBRecord(host, principal, db, store, id);
  }

  getNamesForHost(host) {
    const storesForHost = this.hostVsStores.get(host);
    if (!storesForHost) {
      return [];
    }

    const names = [];

    for (const [dbName, { objectStores }] of storesForHost) {
      if (objectStores.size) {
        for (const objectStore of objectStores.keys()) {
          names.push(JSON.stringify([dbName, objectStore]));
        }
      } else {
        names.push(JSON.stringify([dbName]));
      }
    }

    return names;
  }

  /**
   * Returns the total number of entries for various types of requests to
   * getStoreObjects for Indexed DB actor.
   *
   * @param {string} host
   *        The host for the request.
   * @param {array:string} names
   *        Array of stringified name objects for indexed db actor.
   *        The request type depends on the length of any parsed entry from this
   *        array. 0 length refers to request for the whole host. 1 length
   *        refers to request for a particular db in the host. 2 length refers
   *        to a particular object store in a db in a host. 3 length refers to
   *        particular items of an object store in a db in a host.
   * @param {object} options
   *        An options object containing following properties:
   *         - index {string} The IDBIndex for the object store in the db.
   */
  getObjectsSize(host, names, options) {
    // In Indexed DB, we are interested in only the first name, as the pattern
    // should follow in all entries.
    const name = names[0];
    const parsedName = JSON.parse(name);

    if (parsedName.length == 3) {
      // This is the case where specific entries from an object store were
      // requested
      return names.length;
    } else if (parsedName.length == 2) {
      // This is the case where all entries from an object store are requested.
      const index = options.index;
      const [db, objectStore] = parsedName;
      if (this.objectsSize[host + db + objectStore + index]) {
        return this.objectsSize[host + db + objectStore + index];
      }
    } else if (parsedName.length == 1) {
      // This is the case where details of all object stores in a db are
      // requested.
      if (
        this.hostVsStores.has(host) &&
        this.hostVsStores.get(host).has(parsedName[0])
      ) {
        return this.hostVsStores.get(host).get(parsedName[0]).objectStores.size;
      }
    } else if (!parsedName || !parsedName.length) {
      // This is the case were details of all dbs in a host are requested.
      if (this.hostVsStores.has(host)) {
        return this.hostVsStores.get(host).size;
      }
    }
    return 0;
  }

  /**
   * Returns the over-the-wire implementation of the indexed db entity.
   */
  toStoreObject(item) {
    if (!item) {
      return null;
    }

    if ("indexes" in item) {
      // Object store meta data
      return {
        objectStore: item.name,
        keyPath: item.keyPath,
        autoIncrement: item.autoIncrement,
        indexes: item.indexes,
      };
    }
    if ("objectStores" in item) {
      // DB meta data
      return {
        uniqueKey: `${item.name} (${item.storage})`,
        db: item.name,
        storage: item.storage,
        origin: item.origin,
        version: item.version,
        objectStores: item.objectStores,
      };
    }

    const value = JSON.stringify(item.value);

    // Indexed db entry
    return {
      name: item.name,
      value: new LongStringActor(this.conn, value),
    };
  }

  form() {
    const hosts = {};
    for (const host of this.hosts) {
      hosts[host] = this.getNamesForHost(host);
    }

    return {
      actor: this.actorID,
      hosts,
      traits: this._getTraits(),
    };
  }

  onItemUpdated(action, host, path) {
    dump(" IDX.onItemUpdated(" + action + " - " + host + " - " + path + "\n");
    // Database was removed, remove it from stores map
    if (action === "deleted" && path.length === 1) {
      if (this.hostVsStores.has(host)) {
        this.hostVsStores.get(host).delete(path[0]);
      }
    }

    this.storageActor.update(action, "indexedDB", {
      [host]: [JSON.stringify(path)],
    });
  }

  async getFields(subType) {
    switch (subType) {
      // Detail of database
      case "database":
        return [
          { name: "objectStore", editable: false },
          { name: "keyPath", editable: false },
          { name: "autoIncrement", editable: false },
          { name: "indexes", editable: false },
        ];

      // Detail of object store
      case "object store":
        return [
          { name: "name", editable: false },
          { name: "value", editable: false },
        ];

      // Detail of indexedDB for one origin
      default:
        return [
          { name: "uniqueKey", editable: false, private: true },
          { name: "db", editable: false },
          { name: "storage", editable: false },
          { name: "origin", editable: false },
          { name: "version", editable: false },
          { name: "objectStores", editable: false },
        ];
    }
  }

  /**
   * Fetches and stores all the metadata information for the given database
   * `name` for the given `host` with its `principal`. The stored metadata
   * information is of `DatabaseMetadata` type.
   */
  async getDBMetaData(host, principal, name, storage) {
    const request = this.openWithPrincipal(principal, name, storage);
    return new Promise(resolve => {
      request.onsuccess = event => {
        const db = event.target.result;
        const dbData = new DatabaseMetadata(host, db, storage);
        db.close();

        resolve(dbData);
      };
      request.onerror = ({ target }) => {
        // Ignore the AbortError: the database was deleted, see openWithPrincipal.
        if (target.error.name != "AbortError") {
          console.error(
            `Error opening indexeddb database ${name} for host ${host}`,
            target.error
          );
        }
        resolve(null);
      };
    });
  }

  splitNameAndStorage(name) {
    const lastOpenBracketIndex = name.lastIndexOf("(");
    const lastCloseBracketIndex = name.lastIndexOf(")");
    const delta = lastCloseBracketIndex - lastOpenBracketIndex - 1;

    const storage = name.substr(lastOpenBracketIndex + 1, delta);

    name = name.substr(0, lastOpenBracketIndex - 1);

    return { storage, name };
  }

  /**
   * Get all "internal" hosts. Internal hosts are database namespaces used by
   * the browser.
   */
  async getInternalHosts() {
    const profileDir = PathUtils.profileDir;
    const storagePath = PathUtils.join(profileDir, "storage", "permanent");
    const children = await IOUtils.getChildren(storagePath);
    const hosts = [];

    for (const path of children) {
      const exists = await IOUtils.exists(path);
      if (!exists) {
        continue;
      }

      const stats = await IOUtils.stat(path);
      if (
        stats.type === "directory" &&
        !SAFE_HOSTS_PREFIXES_REGEX.test(stats.path)
      ) {
        const basename = PathUtils.filename(path);
        hosts.push(basename);
      }
    }

    return hosts;
  }

  /**
   * Opens an indexed db connection for the given `principal` and
   * database `name`.
   */
  openWithPrincipal(principal, name, storage) {
    const request = indexedDBForStorage.openForPrincipal(principal, name, {
      storage,
    });
    // An oldVersion of 0 means the database doesn't exist (anymore), e.g. the
    // page deleted it. Abort so that inspecting storage doesn't create an
    // empty database the page would then find instead of creating its own.
    request.onupgradeneeded = event => {
      if (event.oldVersion == 0) {
        request.transaction.abort();
      }
    };
    return request;
  }

  async removeDB(host, principal, dbName) {
    const result = new Promise(resolve => {
      const { name, storage } = this.splitNameAndStorage(dbName);
      const request = indexedDBForStorage.deleteForPrincipal(principal, name, {
        storage,
      });

      request.onsuccess = () => {
        resolve({});
        this.onItemUpdated("deleted", host, [dbName]);
      };

      request.onblocked = () => {
        console.warn(
          `Deleting indexedDB database ${name} for host ${host} is blocked`
        );
        resolve({ blocked: true });
      };

      request.onerror = () => {
        const { error } = request;
        console.warn(
          `Error deleting indexedDB database ${name} for host ${host}: ${error}`
        );
        resolve({ error: error.message });
      };

      // If the database is blocked repeatedly, the onblocked event will not
      // be fired again. To avoid waiting forever, report as blocked if nothing
      // else happens after 3 seconds.
      setTimeout(() => resolve({ blocked: true }), 3000);
    });

    return result;
  }

  async removeDBRecord(host, principal, dbName, storeName, id) {
    let db;
    const { name, storage } = this.splitNameAndStorage(dbName);

    try {
      db = await new Promise((resolve, reject) => {
        const request = this.openWithPrincipal(principal, name, storage);
        request.onsuccess = ev => resolve(ev.target.result);
        request.onerror = ev => reject(ev.target.error);
      });

      const transaction = db.transaction(storeName, "readwrite");
      const store = transaction.objectStore(storeName);

      await new Promise((resolve, reject) => {
        const request = store.delete(id);
        request.onsuccess = () => resolve();
        request.onerror = ev => reject(ev.target.error);
      });

      this.onItemUpdated("deleted", host, [dbName, storeName, id]);
    } catch (error) {
      const recordPath = [dbName, storeName, id].join("/");
      console.error(
        `Failed to delete indexedDB record: ${recordPath}: ${error}`
      );
    }

    if (db) {
      db.close();
    }

    return null;
  }

  async clearDBStore(host, principal, dbName, storeName) {
    let db;
    const { name, storage } = this.splitNameAndStorage(dbName);

    try {
      db = await new Promise((resolve, reject) => {
        const request = this.openWithPrincipal(principal, name, storage);
        request.onsuccess = ev => resolve(ev.target.result);
        request.onerror = ev => reject(ev.target.error);
      });

      const transaction = db.transaction(storeName, "readwrite");
      const store = transaction.objectStore(storeName);

      await new Promise((resolve, reject) => {
        const request = store.clear();
        request.onsuccess = () => resolve();
        request.onerror = ev => reject(ev.target.error);
      });

      this.onItemUpdated("cleared", host, [dbName, storeName]);
    } catch (error) {
      const storePath = [dbName, storeName].join("/");
      console.error(`Failed to clear indexedDB store: ${storePath}: ${error}`);
    }

    if (db) {
      db.close();
    }

    return null;
  }

  /**
   * Fetches all the databases and their metadata for the given `host`.
   */
  async getDBNamesForHost(host, principal) {
    const sanitizedHost = this.getSanitizedHost(host) + principal.originSuffix;
    const profileDir = PathUtils.profileDir;
    const storagePath = PathUtils.join(profileDir, "storage");
    const files = [];
    const names = [];

    // We expect sqlite DB paths to look something like this:
    // - PathToProfileDir/storage/default/http+++www.example.com/
    //   idb/1556056096MeysDaabta.sqlite
    // - PathToProfileDir/storage/permanent/http+++www.example.com/
    //   idb/1556056096MeysDaabta.sqlite
    // - PathToProfileDir/storage/temporary/http+++www.example.com/
    //   idb/1556056096MeysDaabta.sqlite
    // The subdirectory inside the storage folder is determined by the storage
    // type:
    // - default:   { storage: "default" } or not specified.
    // - permanent: { storage: "persistent" }.
    // - temporary: { storage: "temporary" }.
    const sqliteFiles = await this.findSqlitePathsForHost(
      storagePath,
      sanitizedHost
    );

    for (const file of sqliteFiles) {
      const splitPath = PathUtils.split(file);
      const idbIndex = splitPath.indexOf("idb");
      const storage = splitPath[idbIndex - 2];
      const relative = file.substr(profileDir.length + 1);

      files.push({
        file: relative,
        storage: storage === "permanent" ? "persistent" : storage,
      });
    }

    if (files.length) {
      for (const { file, storage } of files) {
        const name = await this.getNameFromDatabaseFile(file);
        if (name) {
          names.push({
            name,
            storage,
          });
        }
      }
    }

    return { names };
  }

  /**
   * Find all SQLite files that hold IndexedDB data for a host, such as:
   *   storage/temporary/http+++www.example.com/idb/1556056096MeysDaabta.sqlite
   */
  async findSqlitePathsForHost(storagePath, sanitizedHost) {
    const sqlitePaths = [];
    const idbPaths = await this.findIDBPathsForHost(storagePath, sanitizedHost);
    for (const idbPath of idbPaths) {
      const children = await IOUtils.getChildren(idbPath);

      for (const path of children) {
        const exists = await IOUtils.exists(path);
        if (!exists) {
          continue;
        }

        const stats = await IOUtils.stat(path);
        if (stats.type !== "directory" && stats.path.endsWith(".sqlite")) {
          sqlitePaths.push(path);
        }
      }
    }
    return sqlitePaths;
  }

  /**
   * Find all paths that hold IndexedDB data for a host, such as:
   *   storage/temporary/http+++www.example.com/idb
   */
  async findIDBPathsForHost(storagePath, sanitizedHost) {
    const idbPaths = [];
    const typePaths = await this.findStorageTypePaths(storagePath);
    for (const typePath of typePaths) {
      const idbPath = PathUtils.join(typePath, sanitizedHost, "idb");
      if (await IOUtils.exists(idbPath)) {
        idbPaths.push(idbPath);
      }
    }
    return idbPaths;
  }

  /**
   * Find all the storage types, such as "default", "permanent", or "temporary".
   * These names have changed over time, so it seems simpler to look through all
   * types that currently exist in the profile.
   */
  async findStorageTypePaths(storagePath) {
    const children = await IOUtils.getChildren(storagePath);
    const typePaths = [];

    for (const path of children) {
      const exists = await IOUtils.exists(path);
      if (!exists) {
        continue;
      }

      const stats = await IOUtils.stat(path);
      if (stats.type === "directory") {
        typePaths.push(path);
      }
    }

    return typePaths;
  }

  /**
   * Removes any illegal characters from the host name to make it a valid file
   * name.
   */
  getSanitizedHost(host) {
    if (host.startsWith("about:")) {
      host = "moz-safe-" + host;
    }
    return host.replace(ILLEGAL_CHAR_REGEX, "+");
  }

  /**
   * Retrieves the proper indexed db database name from the provided .sqlite
   * file location.
   */
  async getNameFromDatabaseFile(path) {
    let connection = null;

    // First establish a connection
    // Content pages might be having an open transaction for the same indexed db
    // which this sqlite file belongs to. In that case, sqlite.openConnection
    // will throw. Thus we retry for some time to see if lock is removed.
    for (let retries = 0; !connection && retries < 25; retries++) {
      try {
        connection = await lazy.Sqlite.openConnection({ path });
      } catch (ex) {
        // Continuously retrying is overkill. Waiting for 100ms before next try
        await sleep(100);
      }
    }

    // If the connection could not be established, bail out.
    if (connection === null) {
      console.error("Unable to open Sqlite connection to path: " + path);
      return null;
    }

    // Then try to read the name.
    let name = undefined;
    for (let retries = 0; name === undefined && retries < 25; retries++) {
      try {
        const rows = await connection.execute("SELECT name FROM database");
        name = rows[0].getResultByName("name");
      } catch {
        // Database might not be ready immediately, keep polling.
        await sleep(100);
      }
    }

    if (!name) {
      console.error("Unable to get the database name for path: " + path);
    }

    // Always close the connection, even if a name could not be retrieved.
    await connection.close();

    return name;
  }

  async getValuesForHost(
    host,
    name = "null",
    options,
    hostVsStores,
    principal
  ) {
    name = JSON.parse(name);
    if (!name || !name.length) {
      // This means that details about the db in this particular host are
      // requested.
      const dbs = [];
      if (hostVsStores.has(host)) {
        for (let [, db] of hostVsStores.get(host)) {
          db = this.patchMetadataMapsAndProtos(db);
          dbs.push(db.toObject());
        }
      }
      return { dbs };
    }

    const [db2, objectStore, id] = name;
    if (!objectStore) {
      // This means that details about all the object stores in this db are
      // requested.
      const objectStores = [];
      if (hostVsStores.has(host) && hostVsStores.get(host).has(db2)) {
        let db = hostVsStores.get(host).get(db2);

        db = this.patchMetadataMapsAndProtos(db);

        const objectStores2 = db.objectStores;

        for (const objectStore2 of objectStores2) {
          objectStores.push(objectStore2[1].toObject());
        }
      }
      return {
        objectStores,
      };
    }
    // Get either all entries from the object store, or a particular id
    const storage = hostVsStores.get(host).get(db2).storage;
    const result = await this.getObjectStoreData(
      host,
      principal,
      db2,
      storage,
      {
        objectStore,
        id,
        index: options.index,
        offset: options.offset,
        size: options.size,
      }
    );
    return { result };
  }

  /**
   * Returns requested entries (or at most MAX_STORE_OBJECT_COUNT) from a particular
   * objectStore from the db in the given host.
   *
   * @param {string} host
   *        The given host.
   * @param {nsIPrincipal} principal
   *        The principal of the given document.
   * @param {string} dbName
   *        The name of the indexed db from the above host.
   * @param {string} storage
   *        Storage type, either "temporary", "default" or "persistent".
   * @param {object} requestOptions
   *        An object in the following format:
   *        {
   *          objectStore: The name of the object store from the above db,
   *          id:          Id of the requested entry from the above object
   *                       store. null if all entries from the above object
   *                       store are requested,
   *          index:       Name of the IDBIndex to be iterated on while fetching
   *                       entries. null or "name" if no index is to be
   *                       iterated,
   *          offset:      offset of the entries to be fetched,
   *          size:        The intended size of the entries to be fetched
   *        }
   */
  getObjectStoreData(host, principal, dbName, storage, requestOptions) {
    const { name } = this.splitNameAndStorage(dbName);
    const request = this.openWithPrincipal(principal, name, storage);

    return new Promise(resolve => {
      let { objectStore, id, index, offset, size } = requestOptions;
      const data = [];
      let db;

      if (!size || size > MAX_STORE_OBJECT_COUNT) {
        size = MAX_STORE_OBJECT_COUNT;
      }

      request.onsuccess = event => {
        db = event.target.result;

        const transaction = db.transaction(objectStore, "readonly");
        let source = transaction.objectStore(objectStore);
        if (index && index != "name") {
          source = source.index(index);
        }

        source.count().onsuccess = event2 => {
          const objectsSize = [];
          const count = event2.target.result;
          objectsSize.push({
            key: host + dbName + objectStore + index,
            count,
          });

          if (!offset) {
            offset = 0;
          } else if (offset > count) {
            db.close();
            resolve([]);
            return;
          }

          if (id) {
            source.get(id).onsuccess = event3 => {
              db.close();
              resolve([{ name: id, value: event3.target.result }]);
            };
          } else {
            source.openCursor().onsuccess = event4 => {
              const cursor = event4.target.result;

              if (!cursor || data.length >= size) {
                db.close();
                resolve({
                  data,
                  objectsSize,
                });
                return;
              }
              if (offset-- <= 0) {
                data.push({ name: cursor.key, value: cursor.value });
              }
              cursor.continue();
            };
          }
        };
      };

      request.onerror = () => {
        resolve([]);
      };
    });
  }

  /**
   * When indexedDB metadata is parsed to and from JSON then the object's
   * prototype is dropped and any Maps are changed to arrays of arrays. This
   * method is used to repair the prototypes and fix any broken Maps.
   */
  patchMetadataMapsAndProtos(metadata) {
    const md = Object.create(DatabaseMetadata.prototype);
    Object.assign(md, metadata);

    md._objectStores = new Map(metadata._objectStores);

    for (const [name, store] of md._objectStores) {
      const obj = Object.create(ObjectStoreMetadata.prototype);
      Object.assign(obj, store);

      md._objectStores.set(name, obj);

      if (typeof store._indexes.length !== "undefined") {
        obj._indexes = new Map(store._indexes);
      }

      for (const [name2, value] of obj._indexes) {
        const obj2 = Object.create(IndexMetadata.prototype);
        Object.assign(obj2, value);

        obj._indexes.set(name2, obj2);
      }
    }

    return md;
  }
}
exports.IndexedDBStorageActor = IndexedDBStorageActor;
