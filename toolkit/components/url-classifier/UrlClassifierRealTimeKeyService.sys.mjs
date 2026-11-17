/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const COLLECTION_NAME = "safebrowsing-v5-rt-key";
const RECORD_ID = "latest";
const KEY_CHANGED_TOPIC = "safebrowsing-realtime-key-changed";

/**
 * Parses the "latest" record of the collection into the OHTTP key config
 * bytes (RFC 9458 Section 3.1), or null if the record is missing or invalid.
 *
 * @param {object[]} records
 * @returns {Uint8Array|null}
 */
function parseKeyConfig(records) {
  let record = records.find(r => r.id == RECORD_ID);
  if (!record) {
    return null;
  }

  let { key_id: keyId, key_config: encoded } = record;
  if (!Number.isInteger(keyId) || keyId < 0 || keyId > 255) {
    console.warn(`Invalid key_id in ${COLLECTION_NAME}: ${keyId}`);
    return null;
  }

  let config;
  try {
    config = Uint8Array.fromBase64(encoded);
  } catch (e) {
    console.warn(`Invalid key_config in ${COLLECTION_NAME}`, e);
    return null;
  }

  if (!config.length || config[0] != keyId) {
    console.warn(`key_config in ${COLLECTION_NAME} does not match key_id`);
    return null;
  }
  return config;
}

function isSameConfig(a, b) {
  if (!a || !b) {
    return a === b;
  }
  return a.length == b.length && a.every((byte, i) => byte == b[i]);
}

export class UrlClassifierRealTimeKeyService {
  classID = Components.ID("{8e5cefe6-c7f7-424e-87e9-6156c0876e96}");
  QueryInterface = ChromeUtils.generateQI([
    "nsIUrlClassifierRealTimeKeyService",
  ]);

  #config = null;
  #loadPromise = null;

  constructor() {
    lazy.RemoteSettings(COLLECTION_NAME).on("sync", ({ data: { current } }) => {
      let config = parseKeyConfig(current);
      if (isSameConfig(config, this.#config)) {
        return;
      }
      this.#config = config;
      Services.obs.notifyObservers(null, KEY_CHANGED_TOPIC);
    });
  }

  async getKeyConfig() {
    // Retry until a key is loaded; sync events keep it up to date.
    if (!this.#config) {
      this.#loadPromise ??= this.#load().finally(() => {
        this.#loadPromise = null;
      });
      await this.#loadPromise;
    }
    return this.#config?.slice() ?? null;
  }

  async #load() {
    let records = [];
    try {
      records = await lazy.RemoteSettings(COLLECTION_NAME).get();
    } catch (e) {
      console.warn(`Failed to load ${COLLECTION_NAME}`, e);
    }
    this.#config ??= parseKeyConfig(records);
  }
}
