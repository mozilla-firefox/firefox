/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const COLLECTION_NAME = "query-stripping";
const SHARED_DATA_KEY = "URLQueryStripping";
const PREF_STRIP_LIST_NAME = "privacy.query_stripping.strip_list";
const PREF_ALLOW_LIST_NAME = "privacy.query_stripping.allow_list";
const PREF_TESTING_ENABLED = "privacy.query_stripping.testing";
const PREF_STRIP_IS_TEST =
  "privacy.query_stripping.strip_on_share.enableTestMode";

ChromeUtils.defineLazyGetter(lazy, "logger", () => {
  return console.createInstance({
    prefix: "URLQueryStrippingListService",
    maxLogLevelPref: "privacy.query_stripping.listService.logLevel",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "testStripOnShare",
  PREF_STRIP_IS_TEST
);

async function fetchList(fileName) {
  let response = await fetch(
    "chrome://global/content/antitracking/" + fileName
  );
  if (!response.ok) {
    lazy.logger.error(
      "Error fetching strip-on-share strip list" + response.status
    );
    throw new Error(
      "Error fetching strip-on-share strip list" + response.status
    );
  }
  return response.json();
}

// Lazy getter for the strip-on-share strip list.
ChromeUtils.defineLazyGetter(lazy, "StripOnShareList", async () => {
  let [stripOnShareList, stripOnShareLGPLParams] = await Promise.all([
    fetchList("StripOnShare.json"),
    fetchList("StripOnShareLGPL.json"),
  ]);

  if (!stripOnShareList || !stripOnShareLGPLParams) {
    lazy.logger.error("Error strip-on-share strip list were not loaded");
    throw new Error("Error fetching strip-on-share strip list were not loaded");
  }

  // Combines the mozilla licensed strip on share param
  // list and the LGPL licensed strip on share param list
  return combineAndParseLists(stripOnShareList, [stripOnShareLGPLParams]);
});

function combineAndParseLists(mainList, arrOfLists) {
  arrOfLists.forEach(additionalList => {
    for (let key in additionalList) {
      if (Object.hasOwn(mainList, key)) {
        mainList[key].queryParams.push(...additionalList[key].queryParams);

        // Ensure empty origins in global rules function as intended
        mainList[key].origins ??= [];
        additionalList[key].origins ??= [];

        mainList[key].origins.push(...additionalList[key].origins);

        mainList[key].schemelessSites ??= [];
        additionalList[key].schemelessSites ??= [];
        mainList[key].schemelessSites.push(
          ...additionalList[key].schemelessSites
        );
      } else {
        mainList[key] = additionalList[key];
      }
    }
  });

  for (let key in mainList) {
    mainList[key].queryParams = (mainList[key].queryParams ?? []).map(param =>
      param.toLowerCase()
    );

    // Origins can be missing for global rules (isGlobal: true) and for any bad input.
    mainList[key].origins = (mainList[key].origins ?? []).map(origin =>
      origin.toLowerCase()
    );
    mainList[key].schemelessSites = (mainList[key].schemelessSites ?? []).map(
      schemelessSites => schemelessSites.toLowerCase()
    );

    // Removes duplicate origins
    mainList[key].origins = [...new Set(mainList[key].origins)];
    mainList[key].schemelessSites = [...new Set(mainList[key].schemelessSites)];

    // Removes duplicate queryParams
    mainList[key].queryParams = [...new Set(mainList[key].queryParams)];
  }

  return mainList;
}

export class URLQueryStrippingListService {
  classId = Components.ID("{afff16f0-3fd2-4153-9ccd-c6d9abd879e4}");
  QueryInterface = ChromeUtils.generateQI(["nsIURLQueryStrippingListService"]);

  #isInitialized = false;
  #pendingInit = null;
  #initResolver;
  #stripOnShareTestList = null;

  #rs;
  #onSyncCallback;

  constructor() {
    lazy.logger.debug("constructor");
    this.observers = new Set();
    this.stripOnShareObservers = new Set();
    this.stripOnShareParams = null;
    this.prefStripList = new Set();
    this.prefAllowList = new Set();
    this.remoteStripList = new Set();
    this.remoteAllowList = new Set();
    this.isParentProcess =
      Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;
  }

  #onSync(event) {
    lazy.logger.debug("onSync", event);
    let {
      data: { current },
    } = event;
    this._onRemoteSettingsUpdate(current);
  }

  async testSetList(testList) {
    this.#stripOnShareTestList = combineAndParseLists(testList, []);
    await this._notifyStripOnShareObservers();
  }

  testHasStripOnShareObservers() {
    return !!this.stripOnShareObservers.size;
  }

  testHasQPSObservers() {
    return !!this.observers.size;
  }

  async #init() {
    // If there is already an init pending wait for it to complete.
    if (this.#pendingInit) {
      lazy.logger.debug("#init: Waiting for pending init");
      await this.#pendingInit;
      return;
    }

    if (this.#isInitialized) {
      lazy.logger.debug("#init: Skip, already initialized");
      return;
    }
    // Create a promise that resolves when init is complete. This allows us to
    // handle incoming init calls while we're still initializing.
    this.#pendingInit = new Promise(initResolve => {
      this.#initResolver = initResolve;
    });
    this.#isInitialized = true;

    lazy.logger.debug("#init: Run");

    // We can only access the remote settings in the parent process. For content
    // processes, we will use sharedData to sync the list to content processes.
    if (this.isParentProcess) {
      this.#rs = lazy.RemoteSettings(COLLECTION_NAME);

      if (!this.#onSyncCallback) {
        this.#onSyncCallback = this.#onSync.bind(this);
        this.#rs.on("sync", this.#onSyncCallback);
      }

      // Get the initially available entries for remote settings.
      let entries;
      try {
        entries = await this.#rs.get();
      } catch (e) {}
      this._onRemoteSettingsUpdate(entries || []);
    } else {
      // Register the message listener for the remote settings update from the
      // sharedData.
      Services.cpmm.sharedData.addEventListener("change", this);

      // Get the remote settings data from the shared data.
      let data = this._getListFromSharedData();

      this._onRemoteSettingsUpdate(data);
    }

    // Get the list from pref.
    await this._onPrefUpdate(
      PREF_STRIP_LIST_NAME,
      Services.prefs.getStringPref(PREF_STRIP_LIST_NAME, "")
    );
    await this._onPrefUpdate(
      PREF_ALLOW_LIST_NAME,
      Services.prefs.getStringPref(PREF_ALLOW_LIST_NAME, "")
    );

    Services.prefs.addObserver(PREF_STRIP_LIST_NAME, this);
    Services.prefs.addObserver(PREF_ALLOW_LIST_NAME, this);

    Services.obs.addObserver(this, "xpcom-shutdown");

    this.#initResolver();
    this.#pendingInit = null;
  }

  async #shutdown() {
    // Ensure any pending init is done before shutdown.
    if (this.#pendingInit) {
      await this.#pendingInit;
    }

    // Already shut down.
    if (!this.#isInitialized) {
      return;
    }
    this.#isInitialized = false;

    lazy.logger.debug("#shutdown");

    // Unregister RemoteSettings listener (if it was registered).
    if (this.#onSyncCallback) {
      this.#rs.off("sync", this.#onSyncCallback);
      this.#onSyncCallback = null;
    }

    Services.obs.removeObserver(this, "xpcom-shutdown");
    Services.prefs.removeObserver(PREF_STRIP_LIST_NAME, this);
    Services.prefs.removeObserver(PREF_ALLOW_LIST_NAME, this);
  }

  get hasObservers() {
    return !this.observers.size && !this.stripOnShareObservers.size;
  }
  _onRemoteSettingsUpdate(entries) {
    this.remoteStripList.clear();
    this.remoteAllowList.clear();

    for (let entry of entries) {
      for (let item of entry.stripList) {
        this.remoteStripList.add(item);
      }

      for (let item of entry.allowList) {
        this.remoteAllowList.add(item);
      }
    }

    // Because only the parent process will get the remote settings update, so
    // we will sync the list to the shared data so that content processes can
    // get the list.
    if (this.isParentProcess) {
      Services.ppmm.sharedData.set(SHARED_DATA_KEY, {
        stripList: this.remoteStripList,
        allowList: this.remoteAllowList,
      });

      if (Services.prefs.getBoolPref(PREF_TESTING_ENABLED, false)) {
        Services.ppmm.sharedData.flush();
      }
    }

    this._notifyObservers();
  }

  async _onPrefUpdate(pref, value) {
    switch (pref) {
      case PREF_STRIP_LIST_NAME:
        this.prefStripList = new Set(value ? value.split(" ") : []);
        break;

      case PREF_ALLOW_LIST_NAME:
        this.prefAllowList = new Set(value ? value.split(",") : []);
        break;

      default:
        console.error(`Unexpected pref name ${pref}`);
        return;
    }

    this._notifyObservers();
    await this._notifyStripOnShareObservers();
  }

  _getListFromSharedData() {
    let data = Services.cpmm.sharedData.get(SHARED_DATA_KEY);

    return data ? [data] : [];
  }

  _notifyObservers(observer) {
    let stripEntries = new Set([
      ...this.prefStripList,
      ...this.remoteStripList,
    ]);
    let allowEntries = new Set([
      ...this.prefAllowList,
      ...this.remoteAllowList,
    ]);
    let stripEntriesAsString = Array.from(stripEntries).join(" ").toLowerCase();
    let allowEntriesAsString = Array.from(allowEntries).join(",").toLowerCase();

    let observers = observer ? [observer] : this.observers;

    if (observer || this.observers.size) {
      lazy.logger.debug("_notifyObservers", {
        observerCount: observers.length,
        runObserverAfterRegister: observer != null,
        stripEntriesAsString,
        allowEntriesAsString,
      });
    }

    for (let obs of observers) {
      obs.onQueryStrippingListUpdate(
        stripEntriesAsString,
        allowEntriesAsString
      );
    }
  }

  async _notifyStripOnShareObservers(observer) {
    this.stripOnShareParams = await lazy.StripOnShareList;

    // Changing to different test list allows us to test
    // site specific params as the websites that current have
    // site specific params cannot be opened in a test env
    if (lazy.testStripOnShare) {
      this.stripOnShareParams = this.#stripOnShareTestList;
    }

    if (!this.stripOnShareParams) {
      lazy.logger.error("StripOnShare list is undefined");
      return;
    }

    // Add the qps params to the global rules of the strip-on-share list.
    let qpsParams = [...this.prefStripList, ...this.remoteStripList].map(
      param => param.toLowerCase()
    );

    this.stripOnShareParams.global.queryParams.push(...qpsParams);
    // Getting rid of duplicates.
    this.stripOnShareParams.global.queryParams = [
      ...new Set(this.stripOnShareParams.global.queryParams),
    ];

    // Build an array of StripOnShareRules.
    let rules = Object.values(this.stripOnShareParams);
    let stringifiedRules = [];
    // We need to stringify the rules so later we can initialise WebIDL dictionaries from them.
    // The dictionaries init call needs stringified json.
    rules.forEach(rule => {
      stringifiedRules.push(JSON.stringify(rule));
    });

    let observers = observer ? new Set([observer]) : this.stripOnShareObservers;

    if (observers.size) {
      lazy.logger.debug("_notifyStripOnShareObservers", {
        observerCount: observers.size,
        runObserverAfterRegister: observer != null,
        stringifiedRules,
      });
    }
    for (let obs of observers) {
      obs.onStripOnShareUpdate(stringifiedRules);
    }
  }

  async registerAndRunObserver(observer) {
    lazy.logger.debug("registerAndRunObserver", {
      isInitialized: this.#isInitialized,
      pendingInit: this.#pendingInit,
    });

    await this.#init();
    this.observers.add(observer);
    this._notifyObservers(observer);
  }

  async registerAndRunObserverStripOnShare(observer) {
    lazy.logger.debug("registerAndRunObserverStripOnShare", {
      isInitialized: this.#isInitialized,
      pendingInit: this.#pendingInit,
    });

    await this.#init();
    this.stripOnShareObservers.add(observer);
    await this._notifyStripOnShareObservers(observer);
  }

  async unregisterObserver(observer) {
    this.observers.delete(observer);

    if (this.hasObservers) {
      lazy.logger.debug("Last observer unregistered, shutting down...");
      await this.#shutdown();
    }
  }

  async unregisterStripOnShareObserver(observer) {
    this.stripOnShareObservers.delete(observer);

    if (this.hasObservers) {
      lazy.logger.debug("Last observer unregistered, shutting down...");
      await this.#shutdown();
    }
  }

  async clearLists() {
    if (!this.isParentProcess) {
      return;
    }

    // Ensure init.
    await this.#init();

    // Clear the lists of remote settings.
    this._onRemoteSettingsUpdate([]);

    // Clear the user pref for the strip list. The pref change observer will
    // handle the rest of the work.
    Services.prefs.clearUserPref(PREF_STRIP_LIST_NAME);
    Services.prefs.clearUserPref(PREF_ALLOW_LIST_NAME);
  }

  observe(subject, topic, data) {
    lazy.logger.debug("observe", { topic, data });
    switch (topic) {
      case "xpcom-shutdown":
        this.#shutdown();
        break;
      case "nsPref:changed": {
        let prefValue = Services.prefs.getStringPref(data, "");
        this._onPrefUpdate(data, prefValue);
        break;
      }
      default:
        console.error(`Unexpected event ${topic}`);
    }
  }

  handleEvent(event) {
    if (event.type != "change") {
      return;
    }

    if (!event.changedKeys.includes(SHARED_DATA_KEY)) {
      return;
    }

    let data = this._getListFromSharedData();
    this._onRemoteSettingsUpdate(data);
    this._notifyObservers();
  }

  async testWaitForInit() {
    if (this.#pendingInit) {
      await this.#pendingInit;
    }

    return this.#isInitialized;
  }
}
