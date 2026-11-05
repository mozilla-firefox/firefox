/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({ logModule: "EME", maxLogLevel: "Off" });
});

const COLLECTION_NAME = "mfcdm-origins-list";

// These are defined in nsIWindowsMediaFoundationCDMOriginsListService.idl
const ORIGIN_BLOCKED = 0;
const ORIGIN_ALLOWED = 1;

function log(message) {
  lazy.console.debug(
    `WindowsMediaFoundationCDMOriginsListService : ${message}`
  );
}

/**
 * Represents an entry in the origin status list.
 *
 * @param {string} origin The origin URL.
 * @param {boolean} status The status of the origin.
 */
class OriginStatusEntry {
  constructor(origin, status) {
    this._origin = origin;
    this._status = status;
  }

  get origin() {
    return this._origin;
  }

  get status() {
    return this._status;
  }
}
OriginStatusEntry.prototype.classID = Components.ID(
  "{b1d5e2f2-8d65-41f3-86e8-9c6c21f2486d}"
);
OriginStatusEntry.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIOriginStatusEntry",
]);

export function WindowsMediaFoundationCDMOriginsListService() {
  log("Created service");
  Services.obs.addObserver(this, "xpcom-shutdown");
}

WindowsMediaFoundationCDMOriginsListService.prototype = {
  classID: Components.ID("{a9b28d1f-7e11-4c5f-85a0-4fd39db1f7c9}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIWindowsMediaFoundationCDMOriginsListService",
  ]),

  _rs: null,

  _entriesReady: false,

  // this callback will make content parents to broadcast the origin status entry.
  _callbacks: [],

  // An array of OriginStatusEntry.
  _originsList: null,

  async _getOriginsListFromRemoteService() {
    log(`Create the remote service and fetching data`);
    if (this._entriesReady) {
      return;
    }

    this._rs = lazy.RemoteSettings(COLLECTION_NAME);
    this._rsListener = this._onRemoteSettingSync.bind(this);
    this._rs.on("sync", this._rsListener);

    let entries;
    try {
      entries = await this._rs.get();
    } catch (e) {
      log(`Error fetching origins list: ${e}`);
    }

    this._onUpdateEntries(entries || []);
    this._entriesReady = true;
  },

  _onRemoteSettingSync(event) {
    let {
      data: { current },
    } = event;
    this._onUpdateEntries(current || []);
  },

  _onUpdateEntries(aEntries) {
    log(`Update entries, aEntries=${JSON.stringify(aEntries, null, 2)}`);
    const entries = Cc["@mozilla.org/array;1"].createInstance(
      Ci.nsIMutableArray
    );

    for (let entry of aEntries) {
      log(`Entry name: ${entry.origin}, allowed: ${entry.allowed}`);
      const status = entry.allowed ? ORIGIN_ALLOWED : ORIGIN_BLOCKED;
      const originStatusEntry = new OriginStatusEntry(entry.origin, status);
      entries.appendElement(originStatusEntry);
    }
    this._originsList = entries;

    if (this._callbacks.length) {
      log("Processing entries for all callbacks.");
      for (let callback of this._callbacks) {
        try {
          callback.onOriginsListLoaded(entries);
        } catch (e) {
          log(`Error calling callback: ${e}`);
        }
      }
    }
  },

  // xpcom-shutdown observer
  observe(_aSubject, aTopic, _aData) {
    if (aTopic == "xpcom-shutdown") {
      Services.obs.removeObserver(this, "xpcom-shutdown");
      this._callbacks.length = 0;
      this._callbacks = null;
      this._originsList = null;
      if (this._rs) {
        this._rs.off("sync", this._rsListener);
        this._rs = null;
        this._rsListener = null;
      }
    }
  },

  // Below methods are for nsIWindowsMediaFoundationCDMOriginsListService
  setCallback(aCallback) {
    log(`SetCallback`);
    this._callbacks.push(aCallback);

    if (!this._entriesReady) {
      this._getOriginsListFromRemoteService();
    } else {
      log("Entries already ready. Immediately notifying new callback.");
      aCallback.onOriginsListLoaded(this._originsList);
    }
  },

  removeCallback(aCallback) {
    log(`RemoveCallback`);
    const index = this._callbacks.indexOf(aCallback);
    if (index !== -1) {
      this._callbacks.splice(index, 1);
      log(`Callback removed successfully.`);
    } else {
      log(`Callback not found.`);
    }
  },
};
