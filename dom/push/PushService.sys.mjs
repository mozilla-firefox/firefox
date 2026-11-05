/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { clearTimeout, setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { ChromePushSubscription } from "./ChromePushSubscription.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gPushNotifier",
  "@mozilla.org/push/Notifier;1",
  Ci.nsIPushNotifier
);
ChromeUtils.defineESModuleGetters(lazy, {
  PushCrypto: "moz-src:///dom/push/PushCrypto.sys.mjs",
  PushServiceWebSocket: "moz-src:///dom/push/PushServiceWebSocket.sys.mjs",
  pushBroadcastService: "moz-src:///dom/push/PushBroadcastService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "dom.push.loglevel",
    logModule: "PushService",
  });
});

const prefs = Services.prefs.getBranch("dom.push.");

const PUSH_SERVICE_UNINIT = 0;
const PUSH_SERVICE_INIT = 1; // No serverURI
const PUSH_SERVICE_ACTIVATING = 2; // activating db
const PUSH_SERVICE_CONNECTION_DISABLE = 3;
const PUSH_SERVICE_ACTIVE_OFFLINE = 4;
const PUSH_SERVICE_RUNNING = 5;

/**
 * State is change only in couple of functions:
 *   init - change state to PUSH_SERVICE_INIT if state was PUSH_SERVICE_UNINIT
 *   changeServerURL - change state to PUSH_SERVICE_ACTIVATING if serverURL
 *                     present or PUSH_SERVICE_INIT if not present.
 *   changeStateConnectionEnabledEvent - it is call on pref change or during
 *                                       the service activation and it can
 *                                       change state to
 *                                       PUSH_SERVICE_CONNECTION_DISABLE
 *   changeStateOfflineEvent - it is called when offline state changes or during
 *                             the service activation and it change state to
 *                             PUSH_SERVICE_ACTIVE_OFFLINE or
 *                             PUSH_SERVICE_RUNNING.
 *   uninit - change state to PUSH_SERVICE_UNINIT.
 */

// This is for starting and stopping service.
const STARTING_SERVICE_EVENT = 0;
const CHANGING_SERVICE_EVENT = 1;
const STOPPING_SERVICE_EVENT = 2;
const UNINIT_EVENT = 3;

// Returns the backend for the given server URI.
function getServiceForServerURI(uri) {
  // Insecure server URLs are allowed for development and testing.
  let allowInsecure = prefs.getBoolPref(
    "testing.allowInsecureServerURL",
    false
  );
  if (uri.scheme == "wss" || (allowInsecure && uri.scheme == "ws")) {
    return lazy.PushServiceWebSocket;
  }
  return null;
}

/**
 * Annotates an error with an XPCOM result code. We use this helper
 * instead of `Components.Exception` because the latter can assert in
 * `nsXPCComponents_Exception::HasInstance` when inspected at shutdown.
 */
function errorWithResult(message, result = Cr.NS_ERROR_FAILURE) {
  let error = new Error(message);
  error.result = result;
  return error;
}

/**
 * The implementation of the push system. It uses WebSockets
 * (PushServiceWebSocket) to communicate with the server and PushDB (IndexedDB)
 * for persistence.
 */
export var PushService = {
  _service: null,
  _state: PUSH_SERVICE_UNINIT,
  _ignoreNewMessages: false,
  _db: null,
  _options: null,
  _visibleNotifications: new Map(),

  // Callback that is called after attempting to
  // reduce the quota for a record. Used for testing purposes.
  _updateQuotaTestCallback: null,

  // Set of timeout ID of tasks to reduce quota.
  _updateQuotaTimeouts: new Set(),

  // When serverURI changes (this is used for testing), db is cleaned up and a
  // a new db is started. This events must be sequential.
  _stateChangeProcessQueue: null,
  _stateChangeProcessEnqueue(op) {
    if (!this._stateChangeProcessQueue) {
      this._stateChangeProcessQueue = Promise.resolve();
    }

    this._stateChangeProcessQueue = this._stateChangeProcessQueue
      .then(op)
      .catch(error => {
        lazy.console.error(
          "stateChangeProcessEnqueue: Error transitioning state",
          error
        );
        return this._shutdownService();
      })
      .catch(error => {
        lazy.console.error(
          "stateChangeProcessEnqueue: Error shutting down service",
          error
        );
      });
    return this._stateChangeProcessQueue;
  },

  // Pending request. If a worker try to register for the same scope again, do
  // not send a new registration request. Therefore we need queue of pending
  // register requests. This is the list of scopes which pending registration.
  _pendingRegisterRequest: {},
  _notifyActivated: null,
  _activated: null,
  _checkActivated() {
    if (this._state < PUSH_SERVICE_ACTIVATING) {
      return Promise.reject(new Error("Push service not active"));
    }
    if (this._state > PUSH_SERVICE_ACTIVATING) {
      return Promise.resolve();
    }
    if (!this._activated) {
      this._activated = new Promise((resolve, reject) => {
        this._notifyActivated = { resolve, reject };
      });
    }
    return this._activated;
  },

  _notifyReady: null,
  _ready: null,

  /**
   * Whether the push service has finished activating.
   *
   * @type {boolean}
   */
  get isReady() {
    return this._state > PUSH_SERVICE_ACTIVATING;
  },

  /**
   * Resolves once the push service has finished activating.
   *
   * @returns {Promise<void>}
   */
  ready() {
    if (this.isReady) {
      return Promise.resolve();
    }
    if (!this._ready) {
      this._ready = new Promise(resolve => {
        this._notifyReady = resolve;
      });
    }
    return this._ready;
  },

  _makePendingKey(aPageRecord) {
    return aPageRecord.scope + "|" + aPageRecord.originAttributes;
  },

  _lookupOrPutPendingRequest(aPageRecord) {
    let key = this._makePendingKey(aPageRecord);
    if (this._pendingRegisterRequest[key]) {
      return this._pendingRegisterRequest[key];
    }

    return (this._pendingRegisterRequest[key] =
      this._registerWithServer(aPageRecord));
  },

  _deletePendingRequest(aPageRecord) {
    let key = this._makePendingKey(aPageRecord);
    if (this._pendingRegisterRequest[key]) {
      delete this._pendingRegisterRequest[key];
    }
  },

  _setState(aNewState) {
    lazy.console.debug(
      "setState()",
      "new state",
      aNewState,
      "old state",
      this._state
    );

    if (this._state == aNewState) {
      return;
    }

    if (this._state == PUSH_SERVICE_ACTIVATING) {
      // It is not important what is the new state as soon as we leave
      // PUSH_SERVICE_ACTIVATING
      if (this._notifyActivated) {
        if (aNewState < PUSH_SERVICE_ACTIVATING) {
          this._notifyActivated.reject(new Error("Push service not active"));
        } else {
          this._notifyActivated.resolve();
        }
      }
      this._notifyActivated = null;
      this._activated = null;
    }
    this._state = aNewState;
    if (this._notifyReady && aNewState > PUSH_SERVICE_ACTIVATING) {
      this._notifyReady();
      this._notifyReady = null;
      this._ready = null;
    }
  },

  async _changeStateOfflineEvent(offline, calledFromConnEnabledEvent) {
    lazy.console.debug("changeStateOfflineEvent()", offline);

    if (
      this._state < PUSH_SERVICE_ACTIVE_OFFLINE &&
      this._state != PUSH_SERVICE_ACTIVATING &&
      !calledFromConnEnabledEvent
    ) {
      return;
    }

    if (offline) {
      if (this._state == PUSH_SERVICE_RUNNING) {
        this._service.disconnect();
      }
      this._setState(PUSH_SERVICE_ACTIVE_OFFLINE);
      return;
    }

    if (this._state == PUSH_SERVICE_RUNNING) {
      // PushService was not in the offline state, but got notification to
      // go online (a offline notification has not been sent).
      // Disconnect first.
      this._service.disconnect();
    }

    let broadcastListeners = await lazy.pushBroadcastService.getListeners();

    // In principle, a listener could be added to the
    // pushBroadcastService here, after we have gotten listeners and
    // before we're RUNNING, but this can't happen in practice because
    // the only caller that can add listeners is PushBroadcastService,
    // and it waits on the same promise we are before it can add
    // listeners. If PushBroadcastService gets woken first, it will
    // update the value that is eventually returned from
    // getListeners.
    this._setState(PUSH_SERVICE_RUNNING);

    this._service.connect(broadcastListeners);
  },

  _changeStateConnectionEnabledEvent(enabled) {
    lazy.console.debug("changeStateConnectionEnabledEvent()", enabled);

    if (
      this._state < PUSH_SERVICE_CONNECTION_DISABLE &&
      this._state != PUSH_SERVICE_ACTIVATING
    ) {
      return Promise.resolve();
    }

    if (enabled) {
      return this._changeStateOfflineEvent(Services.io.offline, true);
    }

    if (this._state == PUSH_SERVICE_RUNNING) {
      this._service.disconnect();
    }
    this._setState(PUSH_SERVICE_CONNECTION_DISABLE);
    return Promise.resolve();
  },

  // Used for testing.
  changeTestServer(url, options = {}) {
    lazy.console.debug("changeTestServer()");

    return this._stateChangeProcessEnqueue(_ => {
      if (this._state < PUSH_SERVICE_ACTIVATING) {
        lazy.console.debug("changeTestServer: PushService not activated?");
        return Promise.resolve();
      }

      return this._changeServerURL(url, CHANGING_SERVICE_EVENT, options);
    });
  },

  observe: function observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      /*
       * We need to call uninit() on shutdown to clean up things that modules
       * aren't very good at automatically cleaning up, so we don't get shutdown
       * leaks on browser shutdown.
       */
      case "quit-application":
        this.uninit();
        break;
      case "network:offline-status-changed":
        this._stateChangeProcessEnqueue(_ =>
          this._changeStateOfflineEvent(aData === "offline", false)
        );
        break;

      case "nsPref:changed":
        if (aData == "serverURL") {
          lazy.console.debug(
            "observe: dom.push.serverURL changed for websocket",
            prefs.getStringPref("serverURL")
          );
          this._stateChangeProcessEnqueue(_ =>
            this._changeServerURL(
              prefs.getStringPref("serverURL"),
              CHANGING_SERVICE_EVENT
            )
          );
        } else if (aData == "connection.enabled") {
          this._stateChangeProcessEnqueue(_ =>
            this._changeStateConnectionEnabledEvent(
              prefs.getBoolPref("connection.enabled")
            )
          );
        }
        break;

      case "idle-daily":
        this._dropExpiredRegistrations().catch(error => {
          lazy.console.error(
            "Failed to drop expired registrations on idle",
            error
          );
        });
        break;

      case "perm-changed":
        this._onPermissionChange(aSubject, aData).catch(error => {
          lazy.console.error(
            "onPermissionChange: Error updating registrations:",
            error
          );
        });
        break;

      case "clear-origin-attributes-data":
        this._clearOriginData(aData).catch(error => {
          lazy.console.error(
            "clearOriginData: Error clearing origin data:",
            error
          );
        });
        break;
    }
  },

  _clearOriginData(data) {
    lazy.console.log("clearOriginData()");

    if (!data) {
      return Promise.resolve();
    }

    let pattern = JSON.parse(data);
    return this._dropRegistrationsIf(record =>
      record.matchesOriginAttributes(pattern)
    );
  },

  /**
   * Sends an unregister request to the server in the background. If the
   * service is not connected, this function is a no-op.
   *
   * @param {PushRecord} record The record to unregister.
   * @param {number} reason An `nsIPushErrorReporter` unsubscribe reason,
   *  indicating why this record was removed.
   */
  _backgroundUnregister(record, reason) {
    lazy.console.debug("backgroundUnregister()");

    if (!this._service.isConnected() || !record) {
      return;
    }

    lazy.console.debug("backgroundUnregister: Notifying server", record);
    this._sendUnregister(record, reason)
      .then(() => {
        lazy.gPushNotifier.notifySubscriptionModified(
          record.scope,
          record.principal
        );
      })
      .catch(e => {
        lazy.console.error("backgroundUnregister: Error notifying server", e);
      });
  },

  _findService(serverURL) {
    lazy.console.debug("findService()");

    if (!serverURL) {
      lazy.console.warn("findService: No dom.push.serverURL found");
      return [];
    }

    let uri;
    try {
      uri = Services.io.newURI(serverURL);
    } catch (e) {
      lazy.console.warn(
        "findService: Error creating valid URI from",
        "dom.push.serverURL",
        serverURL
      );
      return [];
    }

    let service = getServiceForServerURI(uri);
    return [service, uri];
  },

  _changeServerURL(serverURI, event, options = {}) {
    lazy.console.debug("changeServerURL()");

    switch (event) {
      case UNINIT_EVENT:
        return this._stopService(event);

      case STARTING_SERVICE_EVENT: {
        let [service, uri] = this._findService(serverURI);
        if (!service) {
          this._setState(PUSH_SERVICE_INIT);
          return Promise.resolve();
        }
        return this._startService(service, uri, options).then(_ =>
          this._changeStateConnectionEnabledEvent(
            prefs.getBoolPref("connection.enabled")
          )
        );
      }
      case CHANGING_SERVICE_EVENT: {
        let [service, uri] = this._findService(serverURI);
        if (service) {
          if (this._state == PUSH_SERVICE_INIT) {
            this._setState(PUSH_SERVICE_ACTIVATING);
            // The service has not been running - start it.
            return this._startService(service, uri, options).then(_ =>
              this._changeStateConnectionEnabledEvent(
                prefs.getBoolPref("connection.enabled")
              )
            );
          }
          this._setState(PUSH_SERVICE_ACTIVATING);
          // If we already had running service - stop service, start the new
          // one and check connection.enabled and offline state(offline state
          // check is called in changeStateConnectionEnabledEvent function)
          return this._stopService(CHANGING_SERVICE_EVENT)
            .then(_ => this._startService(service, uri, options))
            .then(_ =>
              this._changeStateConnectionEnabledEvent(
                prefs.getBoolPref("connection.enabled")
              )
            );
        }
        if (this._state == PUSH_SERVICE_INIT) {
          return Promise.resolve();
        }
        // The new serverUri is empty or misconfigured - stop service.
        this._setState(PUSH_SERVICE_INIT);
        return this._stopService(STOPPING_SERVICE_EVENT);
      }
      default:
        lazy.console.error("Unexpected event in _changeServerURL", event);
        return Promise.reject(new Error(`Unexpected event ${event}`));
    }
  },

  /**
   * PushService initialization is divided into 4 parts:
   * init() - start listening for quit-application and serverURL changes.
   *          state is change to PUSH_SERVICE_INIT
   * startService() - if serverURL is present this function is called. It starts
   *                  listening for broadcasted messages, starts db and
   *                  PushService connection (WebSocket).
   *                  state is change to PUSH_SERVICE_ACTIVATING.
   * startObservers() - start other observers.
   * changeStateConnectionEnabledEvent  - checks prefs and offline state.
   *                                      It changes state to:
   *                                        PUSH_SERVICE_RUNNING,
   *                                        PUSH_SERVICE_ACTIVE_OFFLINE or
   *                                        PUSH_SERVICE_CONNECTION_DISABLE.
   */
  async init(options = {}) {
    lazy.console.debug("init()");

    if (this._state > PUSH_SERVICE_UNINIT) {
      return;
    }

    this._setState(PUSH_SERVICE_ACTIVATING);
    this._ignoreNewMessages = false;

    prefs.addObserver("serverURL", this);
    Services.obs.addObserver(this, "quit-application");

    if (options.serverURI) {
      // this is use for xpcshell test.

      await this._stateChangeProcessEnqueue(_ =>
        this._changeServerURL(
          options.serverURI,
          STARTING_SERVICE_EVENT,
          options
        )
      );
    } else {
      // This is only used for testing. Different tests require connecting to
      // slightly different URLs.
      await this._stateChangeProcessEnqueue(_ =>
        this._changeServerURL(
          prefs.getStringPref("serverURL"),
          STARTING_SERVICE_EVENT
        )
      );
    }
  },

  _startObservers() {
    lazy.console.debug("startObservers()");

    if (this._state != PUSH_SERVICE_ACTIVATING) {
      return;
    }

    Services.obs.addObserver(this, "clear-origin-attributes-data");

    // The offline-status-changed event is used to know
    // when to (dis)connect. It may not fire if the underlying OS changes
    // networks; in such a case we rely on timeout.
    Services.obs.addObserver(this, "network:offline-status-changed");

    // Used to monitor if the user wishes to disable Push.
    prefs.addObserver("connection.enabled", this);

    // Prunes expired registrations and notifies dormant service workers.
    Services.obs.addObserver(this, "idle-daily");

    // Prunes registrations for sites for which the user revokes push
    // permissions.
    Services.obs.addObserver(this, "perm-changed");
  },

  _startService(service, serverURI, options) {
    lazy.console.debug("startService()");

    if (this._state != PUSH_SERVICE_ACTIVATING) {
      return Promise.reject();
    }

    this._service = service;

    this._db = options.db;
    if (!this._db) {
      this._db = this._service.newPushDB();
    }

    return this._service.init(options, this, serverURI).then(() => {
      this._startObservers();
      return this._dropExpiredRegistrations();
    });
  },

  /**
   * PushService uninitialization is divided into 3 parts:
   * stopObservers() - stot observers started in startObservers.
   * stopService() - It stops listening for broadcasted messages, stops db and
   *                 PushService connection (WebSocket).
   *                 state is changed to PUSH_SERVICE_INIT.
   * uninit() - stop listening for quit-application and serverURL changes.
   *            state is change to PUSH_SERVICE_UNINIT
   */
  _stopService(event) {
    lazy.console.debug("stopService()");

    if (this._state < PUSH_SERVICE_ACTIVATING) {
      return Promise.resolve();
    }

    this._stopObservers();

    this._service.disconnect();
    this._service.uninit();
    this._service = null;

    this._updateQuotaTimeouts.forEach(timeoutID => clearTimeout(timeoutID));
    this._updateQuotaTimeouts.clear();

    if (!this._db) {
      return Promise.resolve();
    }
    if (event == UNINIT_EVENT) {
      // If it is uninitialized just close db.
      this._db.close();
      this._db = null;
      return Promise.resolve();
    }

    return this.dropUnexpiredRegistrations().then(
      _ => {
        this._db.close();
        this._db = null;
      },
      () => {
        this._db.close();
        this._db = null;
      }
    );
  },

  _stopObservers() {
    lazy.console.debug("stopObservers()");

    if (this._state < PUSH_SERVICE_ACTIVATING) {
      return;
    }

    prefs.removeObserver("connection.enabled", this);

    Services.obs.removeObserver(this, "network:offline-status-changed");
    Services.obs.removeObserver(this, "clear-origin-attributes-data");
    Services.obs.removeObserver(this, "idle-daily");
    Services.obs.removeObserver(this, "perm-changed");
  },

  _shutdownService() {
    let promiseChangeURL = this._changeServerURL("", UNINIT_EVENT);
    this._setState(PUSH_SERVICE_UNINIT);
    lazy.console.debug("shutdownService: shutdown complete!");
    return promiseChangeURL;
  },

  async uninit() {
    lazy.console.debug("uninit()");

    if (this._state == PUSH_SERVICE_UNINIT) {
      return;
    }

    prefs.removeObserver("serverURL", this);
    Services.obs.removeObserver(this, "quit-application");

    await this._stateChangeProcessEnqueue(_ => this._shutdownService());
  },

  /**
   * Drops all active registrations and notifies the associated service
   * workers. This function is called when the user switches Push servers,
   * or when the server invalidates all existing registrations.
   *
   * We ignore expired registrations because they're already handled in other
   * code paths. Registrations that expired after exceeding their quotas are
   * evicted at startup, or on the next `idle-daily` event. Registrations that
   * expired because the user revoked the notification permission are evicted
   * once the permission is reinstated.
   */
  dropUnexpiredRegistrations() {
    return this._db.clearIf(record => {
      if (record.isExpired()) {
        return false;
      }
      this._notifySubscriptionChangeObservers(record);
      return true;
    });
  },

  _notifySubscriptionChangeObservers(record) {
    if (!record) {
      return;
    }
    lazy.gPushNotifier.notifySubscriptionChange(
      record.scope,
      record.principal,
      new ChromePushSubscription(record.toSubscription())
    );
  },

  /**
   * Drops a registration and notifies the associated service worker. If the
   * registration does not exist, this function is a no-op.
   *
   * @param {string} keyID The registration ID to remove.
   * @returns {Promise} Resolves once the worker has been notified.
   */
  dropRegistrationAndNotifyApp(aKeyID) {
    return this._db
      .delete(aKeyID)
      .then(record => this._notifySubscriptionChangeObservers(record));
  },

  /**
   * Updates a registration and notifies the associated service worker.
   *
   * @param {string} keyID The registration ID to update.
   * @param {Function} updateFunc Returns the updated record.
   * @returns {Promise} Resolves with the updated record once the worker
   *  has been notified.
   */
  updateRecordAndNotifyApp(aKeyID, aUpdateFunc) {
    return this._db.update(aKeyID, aUpdateFunc).then(record => {
      this._notifySubscriptionChangeObservers(record);
      return record;
    });
  },

  ensureCrypto(record) {
    if (
      record.hasAuthenticationSecret() &&
      record.p256dhPublicKey &&
      record.p256dhPrivateKey
    ) {
      return Promise.resolve(record);
    }

    let keygen = Promise.resolve([]);
    if (!record.p256dhPublicKey || !record.p256dhPrivateKey) {
      keygen = lazy.PushCrypto.generateKeys();
    }
    // We do not have a encryption key. so we need to generate it. This
    // is only going to happen on db upgrade from version 4 to higher.
    return keygen.then(
      ([pubKey, privKey]) => {
        return this.updateRecordAndNotifyApp(record.keyID, recordToUpdate => {
          if (
            !recordToUpdate.p256dhPublicKey ||
            !recordToUpdate.p256dhPrivateKey
          ) {
            recordToUpdate.p256dhPublicKey = pubKey;
            recordToUpdate.p256dhPrivateKey = privKey;
          }
          if (!recordToUpdate.hasAuthenticationSecret()) {
            recordToUpdate.authenticationSecret =
              lazy.PushCrypto.generateAuthenticationSecret();
          }
          return recordToUpdate;
        });
      },
      error => {
        return this.dropRegistrationAndNotifyApp(record.keyID).then(() =>
          Promise.reject(error)
        );
      }
    );
  },

  /**
   * Stop handling new push messages. Messages already being handled keep
   * going. New ones aren't acked, so the server sends them again on the next
   * connection.
   */
  ignoreNewMessages() {
    lazy.console.debug("ignoreNewMessages()");
    this._ignoreNewMessages = true;
  },

  get ignoresNewMessages() {
    return this._ignoreNewMessages;
  },

  /**
   * Dispatches an incoming message to a service worker, recalculating the
   * quota for the associated push registration. If the quota is exceeded,
   * the registration and message will be dropped, and the worker will not
   * be notified.
   *
   * @param {string} keyID The push registration ID.
   * @param {string} messageID The message ID, used to report service worker
   *  delivery failures. For Web Push messages, this is the version. If empty,
   *  failures will not be reported.
   * @param {object} headers The encryption headers.
   * @param {ArrayBuffer|Uint8Array} data The encrypted message data.
   * @param {Function} updateFunc A function that receives the existing
   *  registration record as its argument, and returns a new record. If the
   *  function returns `null` or `undefined`, the record will not be updated.
   *  `PushServiceWebSocket` uses this to drop incoming updates with older
   *  versions.
   * @returns {Promise} Resolves with an `nsIPushErrorReporter` ack status
   *  code, indicating whether the message was delivered successfully.
   */
  receivedPushMessage(keyID, messageID, headers, data, updateFunc) {
    lazy.console.debug("receivedPushMessage()");

    return this._updateRecordAfterPush(keyID, updateFunc)
      .then(record => {
        if (record.quotaApplies()) {
          // Update quota after the delay, at which point
          // we check for visible notifications.
          let timeoutID = setTimeout(_ => {
            this._updateQuota(keyID);
            if (!this._updateQuotaTimeouts.delete(timeoutID)) {
              lazy.console.debug(
                "receivedPushMessage: quota update timeout missing?"
              );
            }
          }, prefs.getIntPref("quotaUpdateDelay"));
          this._updateQuotaTimeouts.add(timeoutID);
        }
        return this._decryptAndNotifyApp(record, messageID, headers, data);
      })
      .catch(error => {
        lazy.console.error("receivedPushMessage: Error notifying app", error);
        return Ci.nsIPushErrorReporter.ACK_NOT_DELIVERED;
      });
  },

  /**
   * Dispatches a broadcast notification to the BroadcastService.
   *
   * @param {object} message The reply received by PushServiceWebSocket
   * @param {object} context Additional information about the context in which the
   *  notification was received.
   */
  receivedBroadcastMessage(message, context) {
    lazy.pushBroadcastService
      .receivedBroadcastMessage(message.broadcasts, context)
      .catch(e => {
        lazy.console.error(e);
      });
  },

  /**
   * Updates a registration record after receiving a push message.
   *
   * @param {string} keyID The push registration ID.
   * @param {Function} updateFunc The function passed to `receivedPushMessage`.
   * @returns {Promise} Resolves with the updated record, or rejects if the
   *  record was not updated.
   */
  _updateRecordAfterPush(keyID, updateFunc) {
    return this.getByKeyID(keyID)
      .then(record => {
        if (!record) {
          throw new Error("No record for key ID " + keyID);
        }
        return record
          .getLastVisit()
          .then(lastVisit => {
            // As a special case, don't notify the service worker if the user
            // cleared their history.
            if (!isFinite(lastVisit)) {
              throw new Error("Ignoring message sent to unvisited origin");
            }
            return lastVisit;
          })
          .then(lastVisit => {
            // Update the record, resetting the quota if the user has visited the
            // site since the last push.
            return this._db.update(keyID, recordToUpdate => {
              let newRecord = updateFunc(recordToUpdate);
              if (!newRecord) {
                return null;
              }
              // Because `unregister` is advisory only, we can still receive messages
              // for stale Simple Push registrations from the server. To work around
              // this, we check if the record has expired before *and* after updating
              // the quota.
              if (newRecord.isExpired()) {
                return null;
              }
              newRecord.receivedPush(lastVisit);
              return newRecord;
            });
          });
      })
      .then(record => {
        lazy.gPushNotifier.notifySubscriptionModified(
          record.scope,
          record.principal
        );
        return record;
      });
  },

  /**
   * Decrypts an incoming message and notifies the associated service worker.
   *
   * @param {PushRecord} record The receiving registration.
   * @param {string} messageID The message ID.
   * @param {object} headers The encryption headers.
   * @param {ArrayBuffer|Uint8Array} data The encrypted message data.
   * @returns {Promise} Resolves with an ack status code.
   */
  _decryptAndNotifyApp(record, messageID, headers, data) {
    return lazy.PushCrypto.decrypt(
      record.p256dhPrivateKey,
      record.p256dhPublicKey,
      record.authenticationSecret,
      headers,
      data
    ).then(
      message => this._notifyApp(record, messageID, message),
      error => {
        lazy.console.warn(
          "decryptAndNotifyApp: Error decrypting message",
          record.scope,
          messageID,
          error
        );

        let message = error.format(record.scope);
        lazy.gPushNotifier.notifyError(
          record.scope,
          record.principal,
          message,
          Ci.nsIScriptError.errorFlag
        );
        return Ci.nsIPushErrorReporter.ACK_DECRYPTION_ERROR;
      }
    );
  },

  _updateQuota(keyID) {
    lazy.console.debug("updateQuota()");

    this._db
      .update(keyID, record => {
        // Record may have expired from an earlier quota update.
        if (record.isExpired()) {
          lazy.console.debug(
            "updateQuota: Trying to update quota for expired record",
            record
          );
          return null;
        }
        // If there are visible notifications, don't apply the quota penalty
        // for the message.
        if (record.uri && !this._visibleNotifications.has(record.uri.prePath)) {
          record.reduceQuota();
        }
        return record;
      })
      .then(record => {
        if (record.isExpired()) {
          // Drop the registration in the background. If the user returns to the
          // site, the service worker will be notified on the next `idle-daily`
          // event.
          this._backgroundUnregister(
            record,
            Ci.nsIPushErrorReporter.UNSUBSCRIBE_QUOTA_EXCEEDED
          );
        } else {
          lazy.gPushNotifier.notifySubscriptionModified(
            record.scope,
            record.principal
          );
        }
        if (this._updateQuotaTestCallback) {
          // Callback so that test may be notified when the quota update is complete.
          this._updateQuotaTestCallback();
        }
      })
      .catch(error => {
        lazy.console.debug(
          "updateQuota: Error while trying to update quota",
          error
        );
      });
  },

  notificationForOriginShown(origin) {
    lazy.console.debug("notificationForOriginShown()", origin);
    let count;
    if (this._visibleNotifications.has(origin)) {
      count = this._visibleNotifications.get(origin);
    } else {
      count = 0;
    }
    this._visibleNotifications.set(origin, count + 1);
  },

  notificationForOriginClosed(origin) {
    lazy.console.debug("notificationForOriginClosed()", origin);
    let count;
    if (this._visibleNotifications.has(origin)) {
      count = this._visibleNotifications.get(origin);
    } else {
      lazy.console.debug(
        "notificationForOriginClosed: closing notification that has not been shown?"
      );
      return;
    }
    if (count > 1) {
      this._visibleNotifications.set(origin, count - 1);
    } else {
      this._visibleNotifications.delete(origin);
    }
  },

  reportDeliveryError(messageID, reason) {
    lazy.console.debug("reportDeliveryError()", messageID, reason);
    if (this._state == PUSH_SERVICE_RUNNING && this._service.isConnected()) {
      // Only report errors if we're initialized and connected.
      this._service.reportDeliveryError(messageID, reason);
    }
  },

  _notifyApp(aPushRecord, messageID, message) {
    if (
      !aPushRecord ||
      !aPushRecord.scope ||
      aPushRecord.originAttributes === undefined
    ) {
      lazy.console.error("notifyApp: Invalid record", aPushRecord);
      return Ci.nsIPushErrorReporter.ACK_NOT_DELIVERED;
    }

    lazy.console.debug("notifyApp()", aPushRecord.scope);

    // If permission has been revoked, trash the message.
    if (!aPushRecord.hasPermission()) {
      lazy.console.warn("notifyApp: Missing push permission", aPushRecord);
      return Ci.nsIPushErrorReporter.ACK_NOT_DELIVERED;
    }

    let payload = ArrayBuffer.isView(message)
      ? new Uint8Array(message.buffer)
      : message;

    if (aPushRecord.quotaApplies()) {
      // Don't record telemetry for chrome push messages.
      Glean.webPush.apiNotify.add(1);
    }

    let handled;
    if (payload) {
      handled = lazy.gPushNotifier.notifyPushWithData(
        aPushRecord.scope,
        aPushRecord.principal,
        messageID,
        payload
      );
    } else {
      handled = lazy.gPushNotifier.notifyPush(
        aPushRecord.scope,
        aPushRecord.principal,
        messageID
      );
    }

    return handled.then(() => Ci.nsIPushErrorReporter.ACK_DELIVERED);
  },

  getByKeyID(aKeyID) {
    return this._db.getByKeyID(aKeyID);
  },

  getAllUnexpired() {
    return this._db.getAllUnexpired();
  },

  /**
   * Returns the unexpired subscriptions, excluding system subscriptions such
   * as the one held by Firefox Accounts.
   *
   * Resolves with an empty array when push has not started, is
   * disabled, or fails to activate.
   *
   * @returns {Promise<PushRecord[]>}
   */
  async getAllUnexpiredNonSystem() {
    if (!prefs.getBoolPref("connection.enabled")) {
      return [];
    }
    try {
      await this._checkActivated();
    } catch {
      return [];
    }
    if (!this._db) {
      return [];
    }
    let records = await this.getAllUnexpired();
    return records.filter(record => !record.systemRecord);
  },

  _sendRequest(action, ...params) {
    if (this._state == PUSH_SERVICE_CONNECTION_DISABLE) {
      return Promise.reject(new Error("Push service disabled"));
    }
    if (this._state == PUSH_SERVICE_ACTIVE_OFFLINE) {
      return Promise.reject(new Error("Push service offline"));
    }
    // Ensure the backend is ready. `getByPageRecord` already checks this, but
    // we need to check again here in case the service was restarted in the
    // meantime.
    return this._checkActivated().then(_ => {
      switch (action) {
        case "register":
          return this._service.register(...params);
        case "unregister":
          return this._service.unregister(...params);
      }
      return Promise.reject(new Error("Unknown request type: " + action));
    });
  },

  /**
   * Called on message from the child process. aPageRecord is an object sent by
   * the push manager, identifying the sending page and other fields.
   */
  _registerWithServer(aPageRecord) {
    lazy.console.debug("registerWithServer()", aPageRecord);

    return this._sendRequest("register", aPageRecord)
      .then(
        record => this._onRegisterSuccess(record),
        err => this._onRegisterError(err)
      )
      .then(
        record => {
          this._deletePendingRequest(aPageRecord);
          lazy.gPushNotifier.notifySubscriptionModified(
            record.scope,
            record.principal
          );
          return record.toSubscription();
        },
        err => {
          this._deletePendingRequest(aPageRecord);
          throw err;
        }
      );
  },

  _sendUnregister(aRecord, aReason) {
    return this._sendRequest("unregister", aRecord, aReason);
  },

  /**
   * Exceptions thrown in _onRegisterSuccess are caught by the promise obtained
   * from _service.request, causing the promise to be rejected instead.
   */
  _onRegisterSuccess(aRecord) {
    lazy.console.debug("_onRegisterSuccess()");

    return this._db.put(aRecord).catch(error => {
      // Unable to save. Destroy the subscription in the background.
      this._backgroundUnregister(
        aRecord,
        Ci.nsIPushErrorReporter.UNSUBSCRIBE_MANUAL
      );
      throw error;
    });
  },

  /**
   * Exceptions thrown in _onRegisterError are caught by the promise obtained
   * from _service.request, causing the promise to be rejected instead.
   */
  _onRegisterError(reply) {
    lazy.console.debug("_onRegisterError()");

    if (!reply.error) {
      lazy.console.warn(
        "onRegisterError: Called without valid error message!",
        reply
      );
      throw new Error("Registration error");
    }
    throw reply.error;
  },

  notificationsCleared() {
    this._visibleNotifications.clear();
  },

  _getByPageRecord(pageRecord) {
    return this._checkActivated().then(_ =>
      this._db.getByIdentifiers(pageRecord)
    );
  },

  register(aPageRecord) {
    lazy.console.debug("register()", aPageRecord);

    let keyPromise;
    if (aPageRecord.appServerKey && aPageRecord.appServerKey.length) {
      let keyView = new Uint8Array(aPageRecord.appServerKey);
      keyPromise = lazy.PushCrypto.validateAppServerKey(keyView).catch(() => {
        // Normalize Web Crypto exceptions. `nsIPushService` will forward the
        // error result to the DOM API implementation in `PushManager.cpp` or
        // `Push.js`, which will convert it to the correct `DOMException`.
        throw errorWithResult(
          "Invalid app server key",
          Cr.NS_ERROR_DOM_PUSH_INVALID_KEY_ERR
        );
      });
    } else {
      keyPromise = Promise.resolve(null);
    }

    return Promise.all([keyPromise, this._getByPageRecord(aPageRecord)]).then(
      ([appServerKey, record]) => {
        aPageRecord.appServerKey = appServerKey;
        if (!record) {
          return this._lookupOrPutPendingRequest(aPageRecord);
        }
        if (!record.matchesAppServerKey(appServerKey)) {
          throw errorWithResult(
            "Mismatched app server key",
            Cr.NS_ERROR_DOM_PUSH_MISMATCHED_KEY_ERR
          );
        }
        if (record.isExpired()) {
          return record
            .quotaChanged()
            .then(isChanged => {
              if (isChanged) {
                // If the user revisited the site, drop the expired push
                // registration and re-register.
                return this.dropRegistrationAndNotifyApp(record.keyID);
              }
              throw new Error("Push subscription expired");
            })
            .then(_ => this._lookupOrPutPendingRequest(aPageRecord));
        }
        return record.toSubscription();
      }
    );
  },

  /*
   * Called only by the PushBroadcastService on the receipt of a new
   * subscription. Don't call this directly. Go through PushBroadcastService.
   */
  async subscribeBroadcast(broadcastId, version) {
    if (this._state != PUSH_SERVICE_RUNNING) {
      // Ignore any request to subscribe before we send a hello.
      // We'll send all the broadcast listeners as part of the hello
      // anyhow.
      return;
    }

    await this._service.sendSubscribeBroadcast(broadcastId, version);
  },

  /**
   * Called on message from the child process.
   *
   * Why is the record being deleted from the local database before the server
   * is told?
   *
   * Unregistration is for the benefit of the app and the AppServer
   * so that the AppServer does not keep pinging a channel the UserAgent isn't
   * watching The important part of the transaction in this case is left to the
   * app, to tell its server of the unregistration.  Even if the request to the
   * PushServer were to fail, it would not affect correctness of the protocol,
   * and the server GC would just clean up the channelID/subscription
   * eventually.  Since the appserver doesn't ping it, no data is lost.
   *
   * If rather we were to unregister at the server and update the database only
   * on success: If the server receives the unregister, and deletes the
   * channelID/subscription, but the response is lost because of network
   * failure, the application is never informed. In addition the application may
   * retry the unregister when it fails due to timeout (websocket) or any other
   * reason at which point the server will say it does not know of this
   * unregistration.  We'll have to make the registration/unregistration phases
   * have retries and attempts to resend messages from the server, and have the
   * client acknowledge. On a server, data is cheap, reliable notification is
   * not.
   */
  unregister(aPageRecord) {
    lazy.console.debug("unregister()", aPageRecord);

    return this._getByPageRecord(aPageRecord).then(record => {
      if (record === null) {
        return false;
      }

      let reason = Ci.nsIPushErrorReporter.UNSUBSCRIBE_MANUAL;
      return Promise.all([
        this._sendUnregister(record, reason),
        this._db.delete(record.keyID).then(rec => {
          if (rec) {
            lazy.gPushNotifier.notifySubscriptionModified(
              rec.scope,
              rec.principal
            );
          }
        }),
      ]).then(([success]) => success);
    });
  },

  /**
   * Clear subscriptions matching either a principal or a domain and
   * OriginAttributesPattern. If domain="*" is passed all records will be
   * deleted.
   *
   * @param {*} options
   * @param {nsIPrincipal} [options.principal] - The principal to clear
   * subscriptions for. This does an exact origin match.
   * @param {string} [options.domain] - Clear all records matching the domain,
   * including subdomains.
   * @param {OriginAttributesPattern} [options.originAttributesPattern] -
   * Additional OriginAttributes filter for clearing by domain. Ignored for
   * domain == "*".
   * @returns {Promise} - A Promise which resolves once the operation has
   * completed.
   */
  clear({ principal, domain, originAttributesPattern }) {
    return this._checkActivated()
      .then(_ => {
        return this._dropRegistrationsIf(record => {
          // Drop all
          if (domain == "*") {
            return true;
          }
          // Can't match against record if it doesn't have a URI.
          if (record.uri == null) {
            return false;
          }

          let originAttributes;
          if (principal || originAttributesPattern) {
            // Restore OriginAttributes from record.originAttributes which
            // contains the OA suffix string. We need this to match against the
            // principal or the OriginAttributesPattern.
            try {
              originAttributes =
                ChromeUtils.CreateOriginAttributesFromOriginSuffix(
                  record.originAttributes
                );
            } catch (e) {
              console.warn("Error while parsing record OA suffix.", e);
              return false;
            }
          }

          // Drop all matching principal.
          if (principal) {
            // Build a principal from the record metadata so we can compare it
            // to the given principal.
            let recordPrincipal =
              Services.scriptSecurityManager.createContentPrincipal(
                record.uri,
                originAttributes
              );
            return recordPrincipal.equals(principal);
          }

          if (!record.uri.host) {
            return false;
          }

          // Drop all matching domain + OA pattern.
          return Services.clearData.hostMatchesSite(
            record.uri.host,
            originAttributes,
            domain,
            originAttributesPattern
          );
        });
      })
      .catch(e => {
        lazy.console.warn(
          "clear: Error dropping subscriptions for domain or principal",
          domain,
          e
        );
        return Promise.resolve();
      });
  },

  registration(aPageRecord) {
    lazy.console.debug("registration()");

    return this._getByPageRecord(aPageRecord).then(record => {
      if (!record) {
        return null;
      }
      if (record.isExpired()) {
        return record.quotaChanged().then(isChanged => {
          if (isChanged) {
            return this.dropRegistrationAndNotifyApp(record.keyID).then(
              _ => null
            );
          }
          return null;
        });
      }
      return record.toSubscription();
    });
  },

  _dropExpiredRegistrations() {
    lazy.console.debug("dropExpiredRegistrations()");

    return this._db.getAllExpired().then(records => {
      return Promise.all(
        records.map(record =>
          record
            .quotaChanged()
            .then(isChanged => {
              if (isChanged) {
                // If the user revisited the site, drop the expired push
                // registration and notify the associated service worker.
                this.dropRegistrationAndNotifyApp(record.keyID);
              }
            })
            .catch(error => {
              lazy.console.error(
                "dropExpiredRegistrations: Error dropping registration",
                record.keyID,
                error
              );
            })
        )
      );
    });
  },

  _onPermissionChange(subject, data) {
    lazy.console.debug("onPermissionChange()");

    if (data == "cleared") {
      return this._clearPermissions();
    }

    let permission = subject.QueryInterface(Ci.nsIPermission);
    if (permission.type != "desktop-notification") {
      return Promise.resolve();
    }

    return this._updatePermission(permission, data);
  },

  _clearPermissions() {
    lazy.console.debug("clearPermissions()");

    return this._db.clearIf(record => {
      if (!record.quotaApplies()) {
        // Only drop registrations that are subject to quota.
        return false;
      }
      this._backgroundUnregister(
        record,
        Ci.nsIPushErrorReporter.UNSUBSCRIBE_PERMISSION_REVOKED
      );
      return true;
    });
  },

  _updatePermission(permission, type) {
    lazy.console.debug("updatePermission()");

    let isAllow = permission.capability == Ci.nsIPermissionManager.ALLOW_ACTION;
    let isChange = type == "added" || type == "changed";

    if (isAllow && isChange) {
      // Permission set to "allow". Drop all expired registrations for this
      // site, notify the associated service workers, and reset the quota
      // for active registrations.
      return this._forEachPrincipal(permission.principal, (record, cursor) =>
        this._permissionAllowed(record, cursor)
      );
    } else if (isChange || (isAllow && type == "deleted")) {
      // Permission set to "block" or "always ask," or "allow" permission
      // removed. Expire all registrations for this site.
      return this._forEachPrincipal(permission.principal, (record, cursor) =>
        this._permissionDenied(record, cursor)
      );
    }

    return Promise.resolve();
  },

  _forEachPrincipal(principal, callback) {
    return this._db.forEachOrigin(
      principal.URI.prePath,
      ChromeUtils.originAttributesToSuffix(principal.originAttributes),
      callback
    );
  },

  /**
   * The update function called for each registration record if the push
   * permission is revoked. We only expire the record so we can notify the
   * service worker as soon as the permission is reinstated. If we just
   * deleted the record, the worker wouldn't be notified until the next visit
   * to the site.
   *
   * @param {PushRecord} record The record to expire.
   * @param {IDBCursor} cursor The IndexedDB cursor.
   */
  _permissionDenied(record, cursor) {
    lazy.console.debug("permissionDenied()");

    if (!record.quotaApplies() || record.isExpired()) {
      // Ignore already-expired records.
      return;
    }
    // Drop the registration in the background.
    this._backgroundUnregister(
      record,
      Ci.nsIPushErrorReporter.UNSUBSCRIBE_PERMISSION_REVOKED
    );
    record.setQuota(0);
    cursor.update(record);
  },

  /**
   * The update function called for each registration record if the push
   * permission is granted. If the record has expired, it will be dropped;
   * otherwise, its quota will be reset to the default value.
   *
   * @param {PushRecord} record The record to update.
   * @param {IDBCursor} cursor The IndexedDB cursor.
   */
  _permissionAllowed(record, cursor) {
    lazy.console.debug("permissionAllowed()");

    if (!record.quotaApplies()) {
      return;
    }
    if (record.isExpired()) {
      // If the registration has expired, drop and notify the worker
      // unconditionally.
      this._notifySubscriptionChangeObservers(record);
      cursor.delete();
      return;
    }
    record.resetQuota();
    cursor.update(record);
  },

  /**
   * Drops all matching registrations from the database. Notifies the
   * associated service workers if permission is granted, and removes
   * unexpired registrations from the server.
   *
   * @param {Function} predicate A function called for each record.
   * @returns {Promise} Resolves once the registrations have been dropped.
   */
  _dropRegistrationsIf(predicate) {
    return this._db.clearIf(record => {
      if (!predicate(record)) {
        return false;
      }
      if (record.hasPermission()) {
        // "Clear Recent History" and the Forget button remove permissions
        // before clearing registrations, but it's possible for the worker to
        // resubscribe if the "dom.push.testing.ignorePermission" pref is set.
        this._notifySubscriptionChangeObservers(record);
      }
      if (!record.isExpired()) {
        // Only unregister active registrations, since we already told the
        // server about expired ones.
        this._backgroundUnregister(
          record,
          Ci.nsIPushErrorReporter.UNSUBSCRIBE_MANUAL
        );
      }
      return true;
    });
  },
};
