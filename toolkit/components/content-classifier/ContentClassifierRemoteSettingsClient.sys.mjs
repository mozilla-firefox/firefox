/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  return console.createInstance({
    maxLogLevelPref:
      "privacy.trackingprotection.content.remote_settings.loglevel",
    logModule: "ContentClassifier",
  });
});

const COLLECTION_NAME = "content-classifier-lists";

/**
 * RemoteSettings client for the content classifier. Owns the filter
 * list bytes (via the RS attachments cache on disk), notifies the
 * C++ ContentClassifierService of list updates and removals, and
 * serves bytes back on demand via getListBytes().
 */
export class ContentClassifierRemoteSettingsClient {
  classID = Components.ID("{C7DDDBF2-8BC4-41A1-AC90-5144BEC5ABDF}");
  QueryInterface = ChromeUtils.generateQI([
    "nsIContentClassifierRemoteSettingsClient",
  ]);

  #rs = null;
  #onSyncCallback = null;
  #service = null;
  #initialized = false;

  constructor() {
    this.#rs = lazy.RemoteSettings(COLLECTION_NAME);
  }

  /**
   * Initialize the client and register a sync
   * listener for future updates. Resolves once the initial import has
   * finished (or failed).
   *
   * The service keeps a strong reference to this client via mRSClient
   * and we keep a strong reference back via #service. The resulting
   * cycle is broken explicitly when the service calls shutdown().
   *
   * @param {nsIContentClassifierService} service
   */
  async init(service) {
    if (!service) {
      throw new Error("Missing required argument service");
    }
    if (this.#initialized) {
      lazy.log.debug(`init: already initialized`);
      return;
    }
    this.#initialized = true;
    this.#service = service;

    lazy.log.debug(`init: starting import`);
    // Register the sync listener before the async import so a sync event
    // that fires during the import is not lost.
    this.#onSyncCallback = this.onSync.bind(this);
    this.#rs.on("sync", this.#onSyncCallback);
    await this.importAllLists();
    lazy.log.debug(`init: done, sync listener registered`);
  }

  /**
   * Shut down the client and unregister the sync listener.
   */
  shutdown() {
    lazy.log.debug(`shutdown`);
    if (this.#onSyncCallback) {
      this.#rs.off("sync", this.#onSyncCallback);
      this.#onSyncCallback = null;
    }
    this.#service = null;
    this.#initialized = false;
  }

  /**
   * Handle a RemoteSettings sync event. Notifies the service,
   * which then pulls list bytes lazily via
   * getListBytes when it actually rebuilds an engine.
   *
   * @param {object} event
   * @param {object} event.data
   * @param {Array} event.data.created  Newly added records.
   * @param {Array} event.data.updated  Records with {old, new} pairs.
   * @param {Array} event.data.deleted  Removed records.
   */
  async onSync({ data: { created = [], updated = [], deleted = [] } }) {
    if (!this.#service) {
      return;
    }

    lazy.log.debug(
      `onSync: ${created.length} created, ${updated.length} updated, ${deleted.length} deleted`
    );

    for (let record of deleted) {
      try {
        await this.#rs.attachments.deleteDownloaded(record);
      } catch (e) {
        lazy.log.error(`Failed to delete attachment for "${record.Name}":`, e);
      }
    }

    let updatedNames = [
      ...updated.map(({ new: r }) => r.Name),
      ...created.map(r => r.Name),
    ];
    let removedNames = deleted.map(r => r.Name);
    this.#service?.onListsChanged(updatedNames, removedNames);
  }

  /**
   * Fetch all records from the collection and notify the service.
   */
  async importAllLists() {
    if (!this.#service) {
      return;
    }

    let records = [];
    try {
      records = await this.#rs.get();
      lazy.log.debug(`importAllLists: got ${records.length} records`);
    } catch (error) {
      lazy.log.error(`Error importing lists:`, error);
    }

    this.#service?.onListsChanged(
      records.map(r => r.Name),
      []
    );
  }

  /**
   * Return a Uint8Array containing the bytes of the named list's
   * attachment. Throws if no record with that name exists or the
   * attachment cannot be obtained.
   *
   * @param {string} name  The Name field of the RemoteSettings record.
   * @returns {Promise<Uint8Array>}
   */
  async getListBytes(name) {
    let records = await this.#rs.get();
    let record = records.find(r => r.Name === name);
    if (!record) {
      throw new Error(`No record with name "${name}"`);
    }
    if (!record.attachment) {
      throw new Error(`Record "${name}" has no attachment`);
    }
    let result;
    try {
      result = await this.#rs.attachments.download(record, {
        fallbackToCache: true,
        fallbackToDump: true,
      });
    } catch (e) {
      lazy.log.error(`Failed to download attachment for "${name}":`, e);
      throw new Error(`Failed to download attachment for "${name}"`);
    }
    return new Uint8Array(result.buffer);
  }
}
