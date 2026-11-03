/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * `meta/syncids` preserves engine {version, syncID} pairs when legacy clients
 * remove meta/global entries. Per-device clients write here without changing
 * legacy engine choices; existing meta/global entries remain authoritative.
 */

import { Log } from "resource://gre/modules/Log.sys.mjs";
import { WBORecord } from "resource://services-sync/record.sys.mjs";

const REGISTRY_VERSION = 1;

function isPlainObject(value) {
  return !!value && typeof value == "object" && !Array.isArray(value);
}

export class EngineSyncIDs {
  constructor(service) {
    this.service = service;
    this._log = Log.repository.getLogger("Sync.EngineSyncIDs");
    this._log.manageLevelFromPref("services.sync.log.logger.service.main");
    this.clearCache();
  }

  clearCache() {
    this._record = null;
    this._changed = false;
    this._readOnly = false;
  }

  _empty() {
    let record = new WBORecord("meta", "syncids");
    record.payload = {
      version: REGISTRY_VERSION,
      globalSyncID: this.service.syncID,
      engines: {},
    };
    record.modified = 0;
    return record;
  }

  /**
   * @param {boolean} isNew whether meta/global is new, so the registry was
   *        removed with it.
   */
  async load(isNew = false) {
    if (this._record) {
      return;
    }
    if (isNew) {
      this._record = this._empty();
      return;
    }
    let record = new WBORecord("meta", "syncids");
    await record.fetch(this.service.resource(this.service.syncIDsURL));

    if (record.response.status == 404) {
      this._log.debug("No sync ID registry on the server yet.");
      this._record = this._empty();
      return;
    }
    if (!record.response.success) {
      throw record.response;
    }

    let payload = record.payload;

    // Don't overwrite data from an unsupported format.
    if (payload?.version > REGISTRY_VERSION) {
      this._log.warn("Ignoring a newer sync ID registry.", payload.version);
      this._record = this._empty();
      this._readOnly = true;
      return;
    }

    // Reject generations from a previous account reset.
    if (
      payload?.version !== REGISTRY_VERSION ||
      payload.globalSyncID !== this.service.syncID ||
      !isPlainObject(payload.engines)
    ) {
      this._log.warn("Discarding an unusable sync ID registry.", payload);
      this._record = this._empty();
      // Preserve conflict detection when replacing an invalid record.
      this._record.modified = record.modified;
      this._changed = true;
      return;
    }
    this._record = record;
  }

  get(name) {
    return this._record.payload.engines[name];
  }

  set(name, version, syncID) {
    let entry = this.get(name);
    if (
      this._readOnly ||
      (entry?.version == version && entry?.syncID == syncID)
    ) {
      return;
    }
    this._record.payload.engines[name] = { version, syncID };
    this._changed = true;
  }

  async maybeUpload() {
    if (!this._changed) {
      return;
    }
    this._log.debug("Uploading the sync ID registry.");
    let res = this.service.resource(this.service.syncIDsURL);
    res.setHeader("X-If-Unmodified-Since", this._record.modified);
    let response = await res.put(this._record);
    if (response.status == 412) {
      // Someone else wrote it; reload it next sync.
      this.clearCache();
    }
    if (!response.success) {
      throw response;
    }
    this._record.modified = response.obj;
    this._changed = false;
  }
}
