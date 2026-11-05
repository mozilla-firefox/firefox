/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
  PushService: "moz-src:///dom/push/PushService.sys.mjs",
});

// BroadcastService is exported for test purposes.
// We are supposed to ignore any updates with this version.
const DUMMY_VERSION_STRING = "____NOP____";

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "dom.push.loglevel",
    logModule: "BroadcastService",
  });
});

class InvalidSourceInfo extends Error {
  constructor(message) {
    super(message);
    this.name = "InvalidSourceInfo";
  }
}

const BROADCAST_SERVICE_VERSION = 1;

export var BroadcastService = class {
  constructor(pushService, path) {
    this.PHASES = {
      HELLO: "hello",
      REGISTER: "register",
      BROADCAST: "broadcast",
    };

    this.pushService = pushService;
    this.jsonFile = new lazy.JSONFile({
      path,
      dataPostProcessor: this._initializeJSONFile,
    });
    this.initializePromise = this.jsonFile.load();
  }

  /**
   * Convert the listeners from our on-disk format to the format
   * needed by a hello message.
   */
  async getListeners() {
    await this.initializePromise;
    return Object.entries(this.jsonFile.data.listeners).reduce(
      (acc, [k, v]) => {
        acc[k] = v.version;
        return acc;
      },
      {}
    );
  }

  _initializeJSONFile(data) {
    if (!data.version) {
      data.version = BROADCAST_SERVICE_VERSION;
    }
    if (!data.hasOwnProperty("listeners")) {
      data.listeners = {};
    }
    return data;
  }

  /**
   * Reset to a state akin to what you would get in a new profile.
   * In particular, wipe anything from storage.
   *
   * Used mainly for testing.
   */
  async _resetListeners() {
    await this.initializePromise;
    this.jsonFile.data = this._initializeJSONFile({});
    this.initializePromise = Promise.resolve();
  }

  /**
   * Ensure that a sourceInfo is correct (has the expected fields).
   */
  _validateSourceInfo(sourceInfo) {
    const { moduleURI, symbolName } = sourceInfo;
    if (typeof moduleURI !== "string") {
      throw new InvalidSourceInfo(
        `moduleURI must be a string (got ${typeof moduleURI})`
      );
    }
    if (typeof symbolName !== "string") {
      throw new InvalidSourceInfo(
        `symbolName must be a string (got ${typeof symbolName})`
      );
    }
  }

  /**
   * Add an entry for a given listener if it isn't present, or update
   * one if it is already present.
   *
   * Note that this means only a single listener can be set for a
   * given subscription. This is a limitation in the current API that
   * stems from the fact that there can only be one source of truth
   * for the subscriber's version. As a workaround, you can define a
   * listener which calls multiple other listeners.
   *
   * @param {string} broadcastId The broadcastID to listen for
   * @param {string} version The most recent version we have for
   *   updates from this broadcastID
   * @param {object} sourceInfo A description of the handler for
   *   updates on this broadcastID
   */
  async addListener(broadcastId, version, sourceInfo) {
    lazy.console.info(
      "addListener: adding listener",
      broadcastId,
      version,
      sourceInfo
    );
    await this.initializePromise;
    this._validateSourceInfo(sourceInfo);
    if (typeof version !== "string") {
      throw new TypeError("version should be a string");
    }
    if (!version) {
      throw new TypeError("version should not be an empty string");
    }

    const isNew = !this.jsonFile.data.listeners.hasOwnProperty(broadcastId);
    const oldVersion =
      !isNew && this.jsonFile.data.listeners[broadcastId].version;
    if (!isNew && oldVersion != version) {
      lazy.console.warn(
        "Versions differ while adding listener for",
        broadcastId,
        ". Got",
        version,
        "but JSON file says",
        oldVersion,
        "."
      );
    }

    // Update listeners before telling the pushService to subscribe,
    // in case it would disregard the update in the small window
    // between getting listeners and setting state to RUNNING.
    //
    // Keep the old version (if we have it) because Megaphone is
    // really the source of truth for the current version of this
    // broadcaster, and the old version is whatever we've either
    // gotten from Megaphone or what we've told to Megaphone and
    // haven't been corrected.
    this.jsonFile.data.listeners[broadcastId] = {
      version: oldVersion || version,
      sourceInfo,
    };
    this.jsonFile.saveSoon();

    if (isNew) {
      await this.pushService.subscribeBroadcast(broadcastId, version);
    }
  }

  /**
   * Call the listeners of the specified broadcasts.
   *
   * @param {Array<object>} broadcasts Map between broadcast ids and versions.
   * @param {object} context Additional information about the context in which the
   *  broadcast notification was originally received. This is transmitted to listeners.
   * @param {string} context.phase One of `BroadcastService.PHASES`
   */
  async receivedBroadcastMessage(broadcasts, context) {
    lazy.console.info("receivedBroadcastMessage:", broadcasts, context);
    await this.initializePromise;
    for (const broadcastId in broadcasts) {
      const version = broadcasts[broadcastId];
      if (version === DUMMY_VERSION_STRING) {
        lazy.console.info(
          "Ignoring",
          version,
          "because it's the dummy version"
        );
        continue;
      }
      // We don't know this broadcastID. This is probably a bug?
      if (!this.jsonFile.data.listeners.hasOwnProperty(broadcastId)) {
        lazy.console.warn(
          "receivedBroadcastMessage: unknown broadcastId",
          broadcastId
        );
        continue;
      }

      const { sourceInfo } = this.jsonFile.data.listeners[broadcastId];
      try {
        this._validateSourceInfo(sourceInfo);
      } catch (e) {
        lazy.console.error(
          "receivedBroadcastMessage: malformed sourceInfo",
          sourceInfo,
          e
        );
        continue;
      }

      const { moduleURI, symbolName } = sourceInfo;

      let module;
      try {
        module = ChromeUtils.importESModule(moduleURI);
      } catch (e) {
        lazy.console.error(
          "receivedBroadcastMessage: couldn't invoke",
          broadcastId,
          "because import of module",
          moduleURI,
          "failed",
          e
        );
        continue;
      }

      if (!module[symbolName]) {
        lazy.console.error(
          "receivedBroadcastMessage: couldn't invoke",
          broadcastId,
          "because module",
          moduleURI,
          "missing attribute",
          symbolName
        );
        continue;
      }

      const handler = module[symbolName];

      if (!handler.receivedBroadcastMessage) {
        lazy.console.error(
          "receivedBroadcastMessage: couldn't invoke",
          broadcastId,
          "because handler returned by",
          `${moduleURI}.${symbolName}`,
          "has no receivedBroadcastMessage method"
        );
        continue;
      }

      try {
        await handler.receivedBroadcastMessage(version, broadcastId, context);
      } catch (e) {
        lazy.console.error(
          "receivedBroadcastMessage: handler for",
          broadcastId,
          "threw error:",
          e
        );
        continue;
      }

      // Broadcast message applied successfully. Update the version we
      // received if it's different than the one we had.  We don't
      // enforce an ordering here (i.e. we use != instead of <)
      // because we don't know what the ordering of the service's
      // versions is going to be.
      if (this.jsonFile.data.listeners[broadcastId].version != version) {
        this.jsonFile.data.listeners[broadcastId].version = version;
        this.jsonFile.saveSoon();
      }
    }
  }

  // For test only.
  _saveImmediately() {
    return this.jsonFile._save();
  }
};

function initializeBroadcastService() {
  // Fallback path for xpcshell tests.
  let path = "broadcast-listeners.json";
  try {
    if (PathUtils.profileDir) {
      // Real path for use in a real profile.
      path = PathUtils.join(PathUtils.profileDir, path);
    }
  } catch (e) {}
  return new BroadcastService(lazy.PushService, path);
}

export var pushBroadcastService = initializeBroadcastService();
