/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * How does the clients engine work?
 *
 * - We use 2 files - commands.json and commands-syncing.json.
 *
 * - At sync upload time, we attempt a rename of commands.json to
 *   commands-syncing.json, and ignore errors (helps for crash during sync!).
 * - We load commands-syncing.json and stash the contents in
 *   _currentlySyncingCommands which lives for the duration of the upload process.
 * - We use _currentlySyncingCommands to build the outgoing records
 * - Immediately after successful upload, we delete commands-syncing.json from
 *   disk (and clear _currentlySyncingCommands). We reconcile our local records
 *   with what we just wrote in the server, and add failed IDs commands
 *   back in commands.json
 * - Any time we need to "save" a command for future syncs, we load
 *   commands.json, update it, and write it back out.
 */

import { Async } from "resource://services-common/async.sys.mjs";

import {
  DEVICE_TYPE_DESKTOP,
  DEVICE_TYPE_MOBILE,
  DEVICE_TYPE_TABLET,
  SINGLE_USER_THRESHOLD,
  SYNC_API_VERSION,
} from "resource://services-sync/constants.sys.mjs";

import {
  Store,
  SyncEngine,
  LegacyTracker,
} from "resource://services-sync/engines.sys.mjs";
import { CryptoWrapper } from "resource://services-sync/record.sys.mjs";
import { Resource } from "resource://services-sync/resource.sys.mjs";
import { Svc, Utils } from "resource://services-sync/util.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

import { PREF_ACCOUNT_ROOT } from "resource://gre/modules/FxAccountsCommon.sys.mjs";

const CLIENTS_TTL = 15552000; // 180 days
const CLIENTS_TTL_REFRESH = 604800; // 7 days
const STALE_CLIENT_REMOTE_AGE = 604800; // 7 days

// TTL of the message sent to another device when sending a tab
const NOTIFY_TAB_SENT_TTL_SECS = 1 * 3600; // 1 hour

// How often we force a refresh of the FxA device list.
const REFRESH_FXA_DEVICE_INTERVAL_MS = 2 * 60 * 60 * 1000; // 2 hours

// Reasons behind sending collection_changed push notifications.
const COLLECTION_MODIFIED_REASON_SENDTAB = "sendtab";
const COLLECTION_MODIFIED_REASON_FIRSTSYNC = "firstsync";

const SUPPORTED_PROTOCOL_VERSIONS = [SYNC_API_VERSION];
const LAST_MODIFIED_ON_PROCESS_COMMAND_PREF =
  "services.sync.clients.lastModifiedOnProcessCommands";

function hasDupeCommand(commands, action) {
  if (!commands) {
    return false;
  }
  return commands.some(
    other =>
      other.command == action.command &&
      Utils.deepEquals(other.args, action.args)
  );
}

export function ClientsRec(collection, id) {
  CryptoWrapper.call(this, collection, id);
}

ClientsRec.prototype = {
  _logName: "Sync.Record.Clients",
  ttl: CLIENTS_TTL,
};
Object.setPrototypeOf(ClientsRec.prototype, CryptoWrapper.prototype);

Utils.deferGetSet(ClientsRec, "cleartext", [
  "name",
  "type",
  "commands",
  "version",
  "protocols",
  "formfactor",
  "os",
  "appPackage",
  "application",
  "device",
  "fxaDeviceId",
]);

export function ClientEngine(service) {
  SyncEngine.call(this, "Clients", service);

  this.fxAccounts = lazy.fxAccounts;
  this.addClientCommandQueue = Async.asyncQueueCaller(this._log);
  Utils.defineLazyIDProperty(this, "localID", "services.sync.client.GUID");
}

ClientEngine.prototype = {
  _storeObj: ClientStore,
  _recordObj: ClientsRec,
  _trackerObj: ClientsTracker,
  allowSkippedRecord: false,
  _knownStaleFxADeviceIds: null,
  _lastDeviceCounts: null,
  _lastFxaDeviceRefresh: 0,

  async initialize() {
    // Reset the last sync timestamp on every startup so that we fetch all clients
    await this.resetLastSync();
  },

  // These two properties allow us to avoid replaying the same commands
  // continuously if we cannot manage to upload our own record.
  _localClientLastModified: 0,
  get _lastModifiedOnProcessCommands() {
    return Services.prefs.getIntPref(LAST_MODIFIED_ON_PROCESS_COMMAND_PREF, -1);
  },

  set _lastModifiedOnProcessCommands(value) {
    Services.prefs.setIntPref(LAST_MODIFIED_ON_PROCESS_COMMAND_PREF, value);
  },

  get isFirstSync() {
    return !this.lastRecordUpload;
  },

  // Always sync client data as it controls other sync behavior
  get enabled() {
    return true;
  },

  get lastRecordUpload() {
    return Svc.PrefBranch.getIntPref(this.name + ".lastRecordUpload", 0);
  },
  set lastRecordUpload(value) {
    Svc.PrefBranch.setIntPref(
      this.name + ".lastRecordUpload",
      Math.floor(value)
    );
  },

  get remoteClients() {
    // return all non-stale clients for external consumption.
    return Object.values(this._store._remoteClients).filter(v => !v.stale);
  },

  remoteClient(id) {
    let client = this._store._remoteClients[id];
    return client && !client.stale ? client : null;
  },

  remoteClientExists(id) {
    return !!this.remoteClient(id);
  },

  // Aggregate some stats on the composition of clients on this account
  get stats() {
    const ALL_MOBILE_TYPES = [DEVICE_TYPE_MOBILE, DEVICE_TYPE_TABLET];
    let stats = {
      // Currently this should never be true as this code only runs on Desktop, but
      // it doesn't cause harm.
      hasMobile: ALL_MOBILE_TYPES.includes(this.localType),
      names: [this.localName],
      numClients: 1,
    };

    for (let id in this._store._remoteClients) {
      let { name, type, stale } = this._store._remoteClients[id];
      if (!stale) {
        stats.hasMobile = stats.hasMobile || ALL_MOBILE_TYPES.includes(type);
        stats.names.push(name);
        stats.numClients++;
      }
    }

    return stats;
  },

  /**
   * Obtain information about device types.
   *
   * Returns a Map of device types to integer counts. Guaranteed to include
   * "desktop" (which will have at least 1 - this device) and "mobile" (which
   * may have zero) counts. It almost certainly will include only these 2.
   */
  get deviceTypes() {
    let counts = new Map();

    counts.set(this.localType, 1); // currently this must be DEVICE_TYPE_DESKTOP
    counts.set(DEVICE_TYPE_MOBILE, 0);

    for (let id in this._store._remoteClients) {
      let record = this._store._remoteClients[id];
      if (record.stale) {
        continue; // pretend "stale" records don't exist.
      }
      let type = record.type;
      // "tablet" and "mobile" are combined.
      if (type == DEVICE_TYPE_TABLET) {
        type = DEVICE_TYPE_MOBILE;
      }
      if (!counts.has(type)) {
        counts.set(type, 0);
      }

      counts.set(type, counts.get(type) + 1);
    }

    return counts;
  },

  get brandName() {
    let brand = Services.strings.createBundle(
      "chrome://branding/locale/brand.properties"
    );
    return brand.GetStringFromName("brandShortName");
  },

  get localName() {
    return this.fxAccounts.device.getLocalName();
  },
  set localName(value) {
    this.fxAccounts.device.setLocalName(value);
  },

  get localType() {
    return this.fxAccounts.device.getLocalType();
  },

  getClientName(id) {
    if (id == this.localID) {
      return this.localName;
    }
    let client = this._store._remoteClients[id];
    if (!client) {
      return "";
    }
    // Sometimes the sync clients don't always correctly update the device name
    // However FxA always does, so try to pull the name from there first
    let fxaDevice = this.fxAccounts.device.recentDeviceList?.find(
      device => device.id === client.fxaDeviceId
    );

    // should be very rare, but could happen if we have yet to fetch devices,
    // or the client recently disconnected
    if (!fxaDevice) {
      this._log.warn(
        "Couldn't find associated FxA device, falling back to client name"
      );
      return client.name;
    }
    return fxaDevice.name;
  },

  getClientFxaDeviceId(id) {
    if (this._store._remoteClients[id]) {
      return this._store._remoteClients[id].fxaDeviceId;
    }
    return null;
  },

  getClientByFxaDeviceId(fxaDeviceId) {
    for (let id in this._store._remoteClients) {
      let client = this._store._remoteClients[id];
      if (client.stale) {
        continue;
      }
      if (client.fxaDeviceId == fxaDeviceId) {
        return client;
      }
    }
    return null;
  },

  getClientType(id) {
    const client = this._store._remoteClients[id];
    if (client.type == DEVICE_TYPE_DESKTOP) {
      return "desktop";
    }
    if (client.formfactor && client.formfactor.includes("tablet")) {
      return "tablet";
    }
    return "phone";
  },

  async _readCommands() {
    let commands = await Utils.jsonLoad("commands", this);
    return commands || {};
  },

  /**
   * Low level function, do not use directly (use _addClientCommand instead).
   */
  async _saveCommands(commands) {
    try {
      await Utils.jsonSave("commands", this, commands);
    } catch (error) {
      this._log.error("Failed to save JSON outgoing commands", error);
    }
  },

  async _prepareCommandsForUpload() {
    try {
      await Utils.jsonMove("commands", "commands-syncing", this);
    } catch (e) {
      // Ignore errors
    }
    let commands = await Utils.jsonLoad("commands-syncing", this);
    return commands || {};
  },

  async _deleteUploadedCommands() {
    delete this._currentlySyncingCommands;
    try {
      await Utils.jsonRemove("commands-syncing", this);
    } catch (err) {
      this._log.error("Failed to delete syncing-commands file", err);
    }
  },

  // Gets commands for a client we are yet to write to the server. Doesn't
  // include commands for that client which are already on the server.
  // We should rename this!
  async getClientCommands(clientId) {
    const allCommands = await this._readCommands();
    return allCommands[clientId] || [];
  },

  async removeLocalCommand(command) {
    // the implementation of this engine is such that adding a command to
    // the local client is how commands are deleted! ¯\_(ツ)_/¯
    await this._addClientCommand(this.localID, command);
  },

  async _addClientCommand(clientId, command) {
    this.addClientCommandQueue.enqueueCall(async () => {
      try {
        const localCommands = await this._readCommands();
        const localClientCommands = localCommands[clientId] || [];
        const remoteClient = this._store._remoteClients[clientId];
        let remoteClientCommands = [];
        if (remoteClient && remoteClient.commands) {
          remoteClientCommands = remoteClient.commands;
        }
        const clientCommands = localClientCommands.concat(remoteClientCommands);
        if (hasDupeCommand(clientCommands, command)) {
          return false;
        }
        localCommands[clientId] = localClientCommands.concat(command);
        await this._saveCommands(localCommands);
        return true;
      } catch (e) {
        // Failing to save a command should not "break the queue" of pending operations.
        this._log.error(e);
        return false;
      }
    });

    return this.addClientCommandQueue.promiseCallsComplete();
  },

  async _removeClientCommands(clientId) {
    const allCommands = await this._readCommands();
    delete allCommands[clientId];
    await this._saveCommands(allCommands);
  },

  async updateKnownStaleClients() {
    this._log.debug("Updating the known stale clients");
    // _fetchFxADevices side effect updates this._knownStaleFxADeviceIds.
    await this._fetchFxADevices();
    let localFxADeviceId = await lazy.fxAccounts.device.getLocalId();
    // Process newer records first, so that if we hit a record with a device ID
    // we've seen before, we can mark it stale immediately.
    let clientList = Object.values(this._store._remoteClients).sort(
      (a, b) => b.serverLastModified - a.serverLastModified
    );
    let seenDeviceIds = new Set([localFxADeviceId]);
    for (let client of clientList) {
      // Clients might not have an `fxaDeviceId` if they fail the FxA
      // registration process.
      if (!client.fxaDeviceId) {
        continue;
      }
      if (this._knownStaleFxADeviceIds.includes(client.fxaDeviceId)) {
        this._log.info(
          `Hiding stale client ${client.id} - in known stale clients list`
        );
        client.stale = true;
      } else if (seenDeviceIds.has(client.fxaDeviceId)) {
        this._log.info(
          `Hiding stale client ${client.id}` +
            ` - duplicate device id ${client.fxaDeviceId}`
        );
        client.stale = true;
      } else {
        seenDeviceIds.add(client.fxaDeviceId);
      }
    }
  },

  async _fetchFxADevices() {
    // We only force a refresh periodically to keep the load on the servers
    // down, and because we expect FxA to have received a push message in
    // most cases when the FxA device list would have changed. For this reason
    // we still go ahead and check the stale list even if we didn't force a
    // refresh.
    let now = this.fxAccounts._internal.now(); // tests mock this .now() impl.
    if (now - REFRESH_FXA_DEVICE_INTERVAL_MS > this._lastFxaDeviceRefresh) {
      this._lastFxaDeviceRefresh = now;
      try {
        await this.fxAccounts.device.refreshDeviceList();
      } catch (e) {
        this._log.error("Could not refresh the FxA device list", e);
      }
    }

    // We assume that clients not present in the FxA Device Manager list have been
    // disconnected and so are stale
    this._log.debug("Refreshing the known stale clients list");
    let localClients = Object.values(this._store._remoteClients)
      .filter(client => client.fxaDeviceId) // iOS client records don't have fxaDeviceId
      .map(client => client.fxaDeviceId);
    const fxaClients = this.fxAccounts.device.recentDeviceList
      ? this.fxAccounts.device.recentDeviceList.map(device => device.id)
      : [];
    this._knownStaleFxADeviceIds = Utils.arraySub(localClients, fxaClients);
  },

  async _syncStartup() {
    // Reupload new client record periodically.
    if (Date.now() / 1000 - this.lastRecordUpload > CLIENTS_TTL_REFRESH) {
      await this._tracker.addChangedID(this.localID);
    }
    return SyncEngine.prototype._syncStartup.call(this);
  },

  async _processIncoming() {
    // Fetch all records from the server.
    await this.resetLastSync();
    this._incomingClients = {};
    try {
      await SyncEngine.prototype._processIncoming.call(this);
      // Update FxA Device list.
      await this._fetchFxADevices();
      // Since clients are synced unconditionally, any records in the local store
      // that don't exist on the server must be for disconnected clients. Remove
      // them, so that we don't upload records with commands for clients that will
      // never see them. We also do this to filter out stale clients from the
      // tabs collection, since showing their list of tabs is confusing.
      for (let id in this._store._remoteClients) {
        if (!this._incomingClients[id]) {
          this._log.info(`Removing local state for deleted client ${id}`);
          await this._removeRemoteClient(id);
        }
      }
      let localFxADeviceId = await lazy.fxAccounts.device.getLocalId();
      // Bug 1264498: Mobile clients don't remove themselves from the clients
      // collection when the user disconnects Sync, so we mark as stale clients
      // with the same name that haven't synced in over a week.
      // (Note we can't simply delete them, or we re-apply them next sync - see
      // bug 1287687)
      this._localClientLastModified = Math.round(
        this._incomingClients[this.localID]
      );
      delete this._incomingClients[this.localID];
      let names = new Set([this.localName]);
      let seenDeviceIds = new Set([localFxADeviceId]);
      let idToLastModifiedList = Object.entries(this._incomingClients).sort(
        (a, b) => b[1] - a[1]
      );
      for (let [id, serverLastModified] of idToLastModifiedList) {
        let record = this._store._remoteClients[id];
        // stash the server last-modified time on the record.
        record.serverLastModified = serverLastModified;
        if (
          record.fxaDeviceId &&
          this._knownStaleFxADeviceIds.includes(record.fxaDeviceId)
        ) {
          this._log.info(
            `Hiding stale client ${id} - in known stale clients list`
          );
          record.stale = true;
        }
        if (!names.has(record.name)) {
          if (record.fxaDeviceId) {
            seenDeviceIds.add(record.fxaDeviceId);
          }
          names.add(record.name);
          continue;
        }
        let remoteAge = Resource.serverTime - this._incomingClients[id];
        if (remoteAge > STALE_CLIENT_REMOTE_AGE) {
          this._log.info(`Hiding stale client ${id} with age ${remoteAge}`);
          record.stale = true;
          continue;
        }
        if (record.fxaDeviceId && seenDeviceIds.has(record.fxaDeviceId)) {
          this._log.info(
            `Hiding stale client ${record.id}` +
              ` - duplicate device id ${record.fxaDeviceId}`
          );
          record.stale = true;
        } else if (record.fxaDeviceId) {
          seenDeviceIds.add(record.fxaDeviceId);
        }
      }
    } finally {
      this._incomingClients = null;
    }
  },

  async _uploadOutgoing() {
    this._currentlySyncingCommands = await this._prepareCommandsForUpload();
    const clientWithPendingCommands = Object.keys(
      this._currentlySyncingCommands
    );
    for (let clientId of clientWithPendingCommands) {
      if (this._store._remoteClients[clientId] || this.localID == clientId) {
        this._modified.set(clientId, 0);
      }
    }
    let updatedIDs = this._modified.ids();
    await SyncEngine.prototype._uploadOutgoing.call(this);
    // Record the response time as the server time for each item we uploaded.
    let lastSync = await this.getLastSync();
    for (let id of updatedIDs) {
      if (id == this.localID) {
        this.lastRecordUpload = lastSync;
      } else {
        this._store._remoteClients[id].serverLastModified = lastSync;
      }
    }
  },

  async _onRecordsWritten(succeeded, failed) {
    // Reconcile the status of the local records with what we just wrote on the
    // server
    for (let id of succeeded) {
      const commandChanges = this._currentlySyncingCommands[id];
      if (id == this.localID) {
        if (this.isFirstSync) {
          this._log.info(
            "Uploaded our client record for the first time, notifying other clients."
          );
          this._notifyClientRecordUploaded();
        }
        if (this.localCommands) {
          this.localCommands = this.localCommands.filter(
            command => !hasDupeCommand(commandChanges, command)
          );
        }
      } else {
        const clientRecord = this._store._remoteClients[id];
        if (!commandChanges || !clientRecord) {
          // should be impossible, else we wouldn't have been writing it.
          this._log.warn(
            "No command/No record changes for a client we uploaded"
          );
          continue;
        }
        // fixup the client record, so our copy of _remoteClients matches what we uploaded.
        this._store._remoteClients[id] = await this._store.createRecord(id);
        // we could do better and pass the reference to the record we just uploaded,
        // but this will do for now
      }
    }

    // Re-add failed commands
    for (let id of failed) {
      const commandChanges = this._currentlySyncingCommands[id];
      if (!commandChanges) {
        continue;
      }
      await this._addClientCommand(id, commandChanges);
    }

    await this._deleteUploadedCommands();

    // Notify other devices that their own client collection changed
    const idsToNotify = succeeded.reduce((acc, id) => {
      if (id == this.localID) {
        return acc;
      }
      const fxaDeviceId = this.getClientFxaDeviceId(id);
      return fxaDeviceId ? acc.concat(fxaDeviceId) : acc;
    }, []);
    if (idsToNotify.length) {
      this._notifyOtherClientsModified(idsToNotify);
    }
  },

  _notifyOtherClientsModified(ids) {
    // We are not waiting on this promise on purpose.
    this._notifyCollectionChanged(
      ids,
      NOTIFY_TAB_SENT_TTL_SECS,
      COLLECTION_MODIFIED_REASON_SENDTAB
    );
  },

  _notifyClientRecordUploaded() {
    // We are not waiting on this promise on purpose.
    this._notifyCollectionChanged(
      null,
      0,
      COLLECTION_MODIFIED_REASON_FIRSTSYNC
    );
  },

  /**
   * @param {?string[]} ids FxA Client IDs to notify. null means everyone else.
   * @param {number} ttl TTL of the push notification.
   * @param {string} reason Reason for sending this push notification.
   */
  async _notifyCollectionChanged(ids, ttl, reason) {
    const message = {
      version: 1,
      command: "sync:collection_changed",
      data: {
        collections: ["clients"],
        reason,
      },
    };
    let excludedIds = null;
    if (!ids) {
      const localFxADeviceId = await lazy.fxAccounts.device.getLocalId();
      excludedIds = [localFxADeviceId];
    }
    try {
      await this.fxAccounts.notifyDevices(ids, excludedIds, message, ttl);
    } catch (e) {
      this._log.error("Could not notify of changes in the collection", e);
    }
  },

  async _syncFinish() {
    // Record histograms for our device types, and also write them to a pref
    // so non-histogram telemetry (eg, UITelemetry) and the sync scheduler
    // has easy access to them, and so they are accurate even before we've
    // successfully synced the first time after startup.
    let deviceTypeCounts = this.deviceTypes;
    for (let [deviceType, count] of deviceTypeCounts) {
      let prefName = this.name + ".devices.";
      switch (deviceType) {
        case DEVICE_TYPE_DESKTOP:
          Glean.sync.deviceCountDesktop.accumulateSingleSample(count);
          prefName += "desktop";
          break;
        case DEVICE_TYPE_MOBILE:
        case DEVICE_TYPE_TABLET:
          Glean.sync.deviceCountMobile.accumulateSingleSample(count);
          prefName += "mobile";
          break;
        default:
          this._log.warn(
            `Unexpected deviceType "${deviceType}" recording device telemetry.`
          );
          continue;
      }
      // Optimization: only write the pref if it changed since our last sync.
      if (
        this._lastDeviceCounts == null ||
        this._lastDeviceCounts.get(prefName) != count
      ) {
        Svc.PrefBranch.setIntPref(prefName, count);
      }
    }
    this._lastDeviceCounts = deviceTypeCounts;
    return SyncEngine.prototype._syncFinish.call(this);
  },

  async _reconcile(item) {
    // Every incoming record is reconciled, so we use this to track the
    // contents of the collection on the server.
    this._incomingClients[item.id] = item.modified;

    if (!(await this._store.itemExists(item.id))) {
      return true;
    }
    // Clients are synced unconditionally, so we'll always have new records.
    // Unfortunately, this will cause the scheduler to use the immediate sync
    // interval for the multi-device case, instead of the active interval. We
    // work around this by updating the record during reconciliation, and
    // returning false to indicate that the record doesn't need to be applied
    // later.
    await this._store.update(item);
    return false;
  },

  // Treat reset the same as wiping for locally cached clients
  async _resetClient() {
    await this._wipeClient();
  },

  async _wipeClient() {
    await SyncEngine.prototype._resetClient.call(this);
    this._knownStaleFxADeviceIds = null;
    delete this.localCommands;
    await this._store.wipe();
    try {
      await Utils.jsonRemove("commands", this);
    } catch (err) {
      this._log.warn("Could not delete commands.json", err);
    }
    try {
      await Utils.jsonRemove("commands-syncing", this);
    } catch (err) {
      this._log.warn("Could not delete commands-syncing.json", err);
    }
  },

  async removeClientData() {
    let res = this.service.resource(this.engineURL + "/" + this.localID);
    await res.delete();
  },

  // Override the default behavior to delete bad records from the server.
  async handleHMACMismatch(item, mayRetry) {
    this._log.debug("Handling HMAC mismatch for " + item.id);

    let base = await SyncEngine.prototype.handleHMACMismatch.call(
      this,
      item,
      mayRetry
    );
    if (base != SyncEngine.kRecoveryStrategy.error) {
      return base;
    }

    // It's a bad client record. Save it to be deleted at the end of the sync.
    this._log.debug("Bad client record detected. Scheduling for deletion.");
    await this._deleteId(item.id);

    // Neither try again nor error; we're going to delete it.
    return SyncEngine.kRecoveryStrategy.ignore;
  },

  /**
   * A hash of valid commands that the client knows about. The key is a command
   * and the value is a hash containing information about the command such as
   * number of arguments, description, and importance (lower importance numbers
   * indicate higher importance.
   */
  _commands: {
    resetAll: {
      args: 0,
      importance: 0,
      desc: "Clear temporary local data for all engines",
    },
    resetEngine: {
      args: 1,
      importance: 0,
      desc: "Clear temporary local data for engine",
    },
    wipeEngine: {
      args: 1,
      importance: 0,
      desc: "Delete all client data for engine",
    },
    logout: { args: 0, importance: 0, desc: "Log out client" },
  },

  /**
   * Sends a command+args pair to a specific client.
   *
   * @param command Command string
   * @param args Array of arguments/data for command
   * @param clientId Client to send command to
   */
  async _sendCommandToClient(command, args, clientId, telemetryExtra) {
    this._log.trace("Sending " + command + " to " + clientId);

    let client = this._store._remoteClients[clientId];
    if (!client) {
      throw new Error("Unknown remote client ID: '" + clientId + "'.");
    }
    if (client.stale) {
      throw new Error("Stale remote client ID: '" + clientId + "'.");
    }

    let action = {
      command,
      args,
      // We send the flowID to the other client so *it* can report it in its
      // telemetry - we record it in ours below.
      flowID: telemetryExtra.flowID,
    };

    if (await this._addClientCommand(clientId, action)) {
      this._log.trace(`Client ${clientId} got a new action`, [command, args]);
      await this._tracker.addChangedID(clientId);
      try {
        telemetryExtra.deviceID =
          this.service.identity.hashedDeviceID(clientId);
      } catch (_) {}

      Glean.syncClient.sendcommand.record({
        command,
        flow_id: telemetryExtra.flowID,
        reason: telemetryExtra.reason,
      });
    } else {
      this._log.trace(`Client ${clientId} got a duplicate action`, [
        command,
        args,
      ]);
    }
  },

  /**
   * Check if the local client has any remote commands and perform them.
   *
   * @return false to abort sync
   */
  async processIncomingCommands() {
    return this._notify("clients:process-commands", "", async function () {
      if (
        !this.localCommands ||
        (this._lastModifiedOnProcessCommands == this._localClientLastModified &&
          !this.ignoreLastModifiedOnProcessCommands)
      ) {
        return true;
      }
      this._lastModifiedOnProcessCommands = this._localClientLastModified;

      const clearedCommands = await this._readCommands()[this.localID];
      const commands = this.localCommands.filter(
        command => !hasDupeCommand(clearedCommands, command)
      );
      let didRemoveCommand = false;
      // Process each command in order.
      for (let rawCommand of commands) {
        let shouldRemoveCommand = true; // most commands are auto-removed.
        let { command, args, flowID } = rawCommand;
        this._log.debug("Processing command " + command, args);

        Glean.syncClient.processcommand.record({
          command,
          flow_id: flowID,
        });
        let engines = [args[0]];
        switch (command) {
          case "resetAll":
            engines = null;
          // Fallthrough
          case "resetEngine":
            await this.service.resetClient(engines);
            break;
          case "wipeEngine":
            await this.service.wipeClient(engines);
            break;
          case "logout":
            this.service.logout();
            return false;
          default:
            this._log.warn("Received an unknown command: " + command);
            break;
        }
        // Add the command to the "cleared" commands list
        if (shouldRemoveCommand) {
          await this.removeLocalCommand(rawCommand);
          didRemoveCommand = true;
        }
      }
      if (didRemoveCommand) {
        await this._tracker.addChangedID(this.localID);
      }

      return true;
    })();
  },

  /**
   * Validates and sends a command to a client or all clients.
   *
   * Calling this does not actually sync the command data to the server. If the
   * client already has the command/args pair, it won't receive a duplicate
   * command.
   * This method is async since it writes the command to a file.
   *
   * @param command
   *        Command to invoke on remote clients
   * @param args
   *        Array of arguments to give to the command
   * @param clientId
   *        Client ID to send command to. If undefined, send to all remote
   *        clients.
   * @param flowID
   *        A unique identifier used to track success for this operation across
   *        devices.
   */
  async sendCommand(command, args, clientId = null, telemetryExtra = {}) {
    let commandData = this._commands[command];
    // Don't send commands that we don't know about.
    if (!commandData) {
      this._log.error("Unknown command to send: " + command);
      return;
    } else if (!args || args.length != commandData.args) {
      // Don't send a command with the wrong number of arguments.
      this._log.error(
        "Expected " +
          commandData.args +
          " args for '" +
          command +
          "', but got " +
          args
      );
      return;
    }

    // We allocate a "flowID" here, so it is used for each client.
    telemetryExtra = Object.assign({}, telemetryExtra); // don't clobber the caller's object
    if (!telemetryExtra.flowID) {
      telemetryExtra.flowID = Utils.makeGUID();
    }

    if (clientId) {
      await this._sendCommandToClient(command, args, clientId, telemetryExtra);
    } else {
      for (let [id, record] of Object.entries(this._store._remoteClients)) {
        if (!record.stale) {
          await this._sendCommandToClient(command, args, id, telemetryExtra);
        }
      }
    }
  },

  async _removeRemoteClient(id) {
    delete this._store._remoteClients[id];
    await this._tracker.removeChangedID(id);
    await this._removeClientCommands(id);
    this._modified.delete(id);
  },
};
Object.setPrototypeOf(ClientEngine.prototype, SyncEngine.prototype);

function ClientStore(name, engine) {
  Store.call(this, name, engine);
}
ClientStore.prototype = {
  _remoteClients: {},

  async create(record) {
    await this.update(record);
  },

  async update(record) {
    if (record.id == this.engine.localID) {
      // Only grab commands from the server; local name/type always wins
      this.engine.localCommands = record.commands;
    } else {
      this._remoteClients[record.id] = record.cleartext;
    }
  },

  async createRecord(id, collection) {
    let record = new ClientsRec(collection, id);

    const commandsChanges = this.engine._currentlySyncingCommands
      ? this.engine._currentlySyncingCommands[id]
      : [];

    // Package the individual components into a record for the local client
    if (id == this.engine.localID) {
      try {
        record.fxaDeviceId = await this.engine.fxAccounts.device.getLocalId();
      } catch (error) {
        this._log.warn("failed to get fxa device id", error);
      }
      record.name = this.engine.localName;
      record.type = this.engine.localType;
      record.version = Services.appinfo.version;
      record.protocols = SUPPORTED_PROTOCOL_VERSIONS;

      // Substract the commands we recorded that we've already executed
      if (
        commandsChanges &&
        commandsChanges.length &&
        this.engine.localCommands &&
        this.engine.localCommands.length
      ) {
        record.commands = this.engine.localCommands.filter(
          command => !hasDupeCommand(commandsChanges, command)
        );
      }

      // Optional fields.
      record.os = Services.appinfo.OS; // "Darwin"
      record.appPackage = Services.appinfo.ID;
      record.application = this.engine.brandName; // "Nightly"

      // We can't compute these yet.
      // record.device = "";            // Bug 1100723
      // record.formfactor = "";        // Bug 1100722
    } else {
      record.cleartext = Object.assign({}, this._remoteClients[id]);
      delete record.cleartext.serverLastModified; // serverLastModified is a local only attribute.

      // Add the commands we have to send
      if (commandsChanges && commandsChanges.length) {
        const recordCommands = record.cleartext.commands || [];
        const newCommands = commandsChanges.filter(
          command => !hasDupeCommand(recordCommands, command)
        );
        record.cleartext.commands = recordCommands.concat(newCommands);
      }

      if (record.cleartext.stale) {
        // It's almost certainly a logic error for us to upload a record we
        // consider stale, so make log noise, but still remove the flag.
        this._log.error(
          `Preparing to upload record ${id} that we consider stale`
        );
        delete record.cleartext.stale;
      }
    }
    if (record.commands) {
      const maxPayloadSize =
        this.engine.service.getMemcacheMaxRecordPayloadSize();
      let origOrder = new Map(record.commands.map((c, i) => [c, i]));
      // we sort first by priority, and second by age (indicated by order in the
      // original list)
      let commands = record.commands.slice().sort((a, b) => {
        let infoA = this.engine._commands[a.command];
        let infoB = this.engine._commands[b.command];
        // Treat unknown command types as highest priority, to allow us to add
        // high priority commands in the future without worrying about clients
        // removing them on each-other unnecessarially.
        let importA = infoA ? infoA.importance : 0;
        let importB = infoB ? infoB.importance : 0;
        // Higher importantance numbers indicate that we care less, so they
        // go to the end of the list where they'll be popped off.
        let importDelta = importA - importB;
        if (importDelta != 0) {
          return importDelta;
        }
        let origIdxA = origOrder.get(a);
        let origIdxB = origOrder.get(b);
        // Within equivalent priorities, we put older entries near the end
        // of the list, so that they are removed first.
        return origIdxB - origIdxA;
      });
      let truncatedCommands = Utils.tryFitItems(commands, maxPayloadSize);
      if (truncatedCommands.length != record.commands.length) {
        this._log.warn(
          `Removing commands from client ${id} (from ${record.commands.length} to ${truncatedCommands.length})`
        );
        // Restore original order.
        record.commands = truncatedCommands.sort(
          (a, b) => origOrder.get(a) - origOrder.get(b)
        );
      }
    }
    return record;
  },

  async itemExists(id) {
    return id in (await this.getAllIDs());
  },

  async getAllIDs() {
    let ids = {};
    ids[this.engine.localID] = true;
    for (let id in this._remoteClients) {
      ids[id] = true;
    }
    return ids;
  },

  async wipe() {
    this._remoteClients = {};
  },
};
Object.setPrototypeOf(ClientStore.prototype, Store.prototype);

function ClientsTracker(name, engine) {
  LegacyTracker.call(this, name, engine);
}
ClientsTracker.prototype = {
  _enabled: false,

  onStart() {
    Svc.Obs.add("fxaccounts:new_device_id", this.asyncObserver);
    Services.prefs.addObserver(
      PREF_ACCOUNT_ROOT + "device.name",
      this.asyncObserver
    );
  },
  onStop() {
    Services.prefs.removeObserver(
      PREF_ACCOUNT_ROOT + "device.name",
      this.asyncObserver
    );
    Svc.Obs.remove("fxaccounts:new_device_id", this.asyncObserver);
  },

  async observe(subject, topic) {
    switch (topic) {
      case "nsPref:changed":
        this._log.debug("client.name preference changed");
      // Fallthrough intended.
      case "fxaccounts:new_device_id":
        await this.addChangedID(this.engine.localID);
        this.score += SINGLE_USER_THRESHOLD + 1; // ALWAYS SYNC NOW.
        break;
    }
  },
};
Object.setPrototypeOf(ClientsTracker.prototype, LegacyTracker.prototype);
