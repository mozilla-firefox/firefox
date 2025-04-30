/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const STORAGE_VERSION = 1; // This needs to be kept in-sync with the rust storage version

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { SyncEngine, Tracker } from "resource://services-sync/engines.sys.mjs";
import { Svc, Utils } from "resource://services-sync/util.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";
import {
  SCORE_INCREMENT_SMALL,
  STATUS_OK,
  URI_LENGTH_MAX,
} from "resource://services-sync/constants.sys.mjs";
import { CommonUtils } from "resource://services-common/utils.sys.mjs";
import { Async } from "resource://services-common/async.sys.mjs";
import {
  SyncRecord,
  SyncTelemetry,
} from "resource://services-sync/telemetry.sys.mjs";
import { BridgedEngine } from "resource://services-sync/bridged_engine.sys.mjs";

const FAR_FUTURE = 4102405200000; // 2100/01/01

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  getTabsStore: "resource://services-sync/TabsStore.sys.mjs",
  RemoteTabRecord: "resource://gre/modules/RustTabs.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "TABS_FILTERED_SCHEMES",
  "services.sync.engine.tabs.filteredSchemes",
  "",
  null,
  val => {
    return new Set(val.split("|"));
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "SYNC_AFTER_DELAY_MS",
  "services.sync.syncedTabs.syncDelayAfterTabChange",
  0
);

// A "bridged engine" to our tabs component.
export function TabEngine(service) {
  BridgedEngine.call(this, "Tabs", service);
}

TabEngine.prototype = {
  _trackerObj: TabTracker,
  syncPriority: 3,

  async prepareTheBridge(isQuickWrite) {
    let clientsEngine = this.service.clientsEngine;
    // Tell the bridged engine about clients.
    // This is the same shape as ClientData in app-services.
    // schema: https://github.com/mozilla/application-services/blob/a1168751231ed4e88c44d85f6dccc09c3b412bd2/components/sync15/src/client_types.rs#L14
    let clientData = {
      local_client_id: clientsEngine.localID,
      recent_clients: {},
    };

    // We shouldn't upload tabs past what the server will accept
    let tabs = await this.getTabsWithinPayloadSize();
    await this._rustStore.setLocalTabs(
      tabs.map(tab => {
        // rust wants lastUsed in MS but the provider gives it in seconds
        tab.lastUsed = tab.lastUsed * 1000;
        return new lazy.RemoteTabRecord(tab);
      })
    );

    for (let remoteClient of clientsEngine.remoteClients) {
      let id = remoteClient.id;
      if (!id) {
        throw new Error("Remote client somehow did not have an id");
      }
      let client = {
        fxa_device_id: remoteClient.fxaDeviceId,
        // device_name and device_type are soft-deprecated - every client
        // prefers what's in the FxA record. But fill them correctly anyway.
        device_name: clientsEngine.getClientName(id) ?? "",
        device_type: clientsEngine.getClientType(id),
      };
      clientData.recent_clients[id] = client;
    }

    // put ourself in there too so we record the correct device info in our sync record.
    clientData.recent_clients[clientsEngine.localID] = {
      fxa_device_id: await clientsEngine.fxAccounts.device.getLocalId(),
      device_name: clientsEngine.localName,
      device_type: clientsEngine.localType,
    };

    // Quick write needs to adjust the lastSync so we can POST to the server
    // see quickWrite() for details
    if (isQuickWrite) {
      await this.setLastSync(FAR_FUTURE);
      await this._bridge.prepareForSync(JSON.stringify(clientData));
      return;
    }

    // Just incase we crashed while the lastSync timestamp was FAR_FUTURE, we
    // reset it to zero
    if ((await this.getLastSync()) === FAR_FUTURE) {
      await this._bridge.setLastSync(0);
    }
    await this._bridge.prepareForSync(JSON.stringify(clientData));
  },

  async _syncStartup() {
    await super._syncStartup();
    await this.prepareTheBridge();
  },

  async initialize() {
    await SyncEngine.prototype.initialize.call(this);

    this._rustStore = await lazy.getTabsStore();
    this._bridge = await this._rustStore.bridgedEngine();

    // Uniffi doesn't currently only support async methods, so we'll need to hardcode
    // these values for now (which is fine for now as these hardly ever change)
    this._bridge.storageVersion = STORAGE_VERSION;
    this._bridge.allowSkippedRecord = true;

    this._log.info("Got a bridged engine!");
    this._tracker.modified = true;
  },

  async getChangedIDs() {
    // No need for a proper timestamp (no conflict resolution needed).
    let changedIDs = {};
    if (this._tracker.modified) {
      changedIDs[this.service.clientsEngine.localID] = 0;
    }
    return changedIDs;
  },

  // API for use by Sync UI code to give user choices of tabs to open.
  async getAllClients() {
    let remoteTabs = await this._rustStore.getAll();
    let remoteClientTabs = [];
    for (let remoteClient of this.service.clientsEngine.remoteClients) {
      // We get the some client info from the rust tabs engine and some from
      // the clients engine.
      let rustClient = remoteTabs.find(
        x => x.clientId === remoteClient.fxaDeviceId
      );
      if (!rustClient) {
        continue;
      }
      let client = {
        // rust gives us ms but js uses seconds, so fix them up.
        tabs: rustClient.remoteTabs.map(tab => {
          tab.lastUsed = tab.lastUsed / 1000;
          return tab;
        }),
        lastModified: rustClient.lastModified / 1000,
        ...remoteClient,
      };
      remoteClientTabs.push(client);
    }
    return remoteClientTabs;
  },

  async removeClientData() {
    let url = this.engineURL + "/" + this.service.clientsEngine.localID;
    await this.service.resource(url).delete();
  },

  async trackRemainingChanges() {
    if (this._modified.count() > 0) {
      this._tracker.modified = true;
    }
  },

  async getTabsWithinPayloadSize() {
    const maxPayloadSize = this.service.getMaxRecordPayloadSize();
    // See bug 535326 comment 8 for an explanation of the estimation
    const maxSerializedSize = (maxPayloadSize / 4) * 3 - 1500;
    return TabProvider.getAllTabsWithEstimatedMax(true, maxSerializedSize);
  },

  // Support for "quick writes"
  _engineLock: Utils.lock,
  _engineLocked: false,

  // Tabs has a special lock to help support its "quick write"
  get locked() {
    return this._engineLocked;
  },
  lock() {
    if (this._engineLocked) {
      return false;
    }
    this._engineLocked = true;
    return true;
  },
  unlock() {
    this._engineLocked = false;
  },

  // Quickly do a POST of our current tabs if possible.
  // This does things that would be dangerous for other engines - eg, posting
  // without checking what's on the server could cause data-loss for other
  // engines, but because each device exclusively owns exactly 1 tabs record
  // with a known ID, it's safe here.
  // Returns true if we successfully synced, false otherwise (either on error
  // or because we declined to sync for any reason.) The return value is
  // primarily for tests.
  async quickWrite() {
    if (!this.enabled) {
      // this should be very rare, and only if tabs are disabled after the
      // timer is created.
      this._log.info("Can't do a quick-sync as tabs is disabled");
      return false;
    }
    // This quick-sync doesn't drive the login state correctly, so just
    // decline to sync if out status is bad
    if (this.service.status.checkSetup() != STATUS_OK) {
      this._log.info(
        "Can't do a quick-sync due to the service status",
        this.service.status.toString()
      );
      return false;
    }
    if (!this.service.serverConfiguration) {
      this._log.info("Can't do a quick sync before the first full sync");
      return false;
    }
    try {
      return await this._engineLock("tabs.js: quickWrite", async () => {
        // We want to restore the lastSync timestamp when complete so next sync
        // takes tabs written by other devices since our last real sync.
        // And for this POST we don't want the protections offered by
        // X-If-Unmodified-Since - we want the POST to work even if the remote
        // has moved on and we will catch back up next full sync.
        const origLastSync = await this.getLastSync();
        try {
          return this._doQuickWrite();
        } finally {
          // set the lastSync to it's original value for regular sync
          await this.setLastSync(origLastSync);
        }
      })();
    } catch (ex) {
      if (!Utils.isLockException(ex)) {
        throw ex;
      }
      this._log.info(
        "Can't do a quick-write as another tab sync is in progress"
      );
      return false;
    }
  },

  // The guts of the quick-write sync, after we've taken the lock, checked
  // the service status etc.
  async _doQuickWrite() {
    // We need to track telemetry for these syncs too!
    const name = "tabs";
    let telemetryRecord = new SyncRecord(
      SyncTelemetry.allowedEngines,
      "quick-write"
    );
    telemetryRecord.onEngineStart(name);
    try {
      Async.checkAppReady();
      // We need to prep the bridge before we try to POST since it grabs
      // the most recent local client id and properly sets a lastSync
      // which is needed for a proper POST request
      await this.prepareTheBridge(true);
      this._tracker.clearChangedIDs();
      this._tracker.resetScore();

      Async.checkAppReady();
      // now just the "upload" part of a sync,
      // which for a rust engine is  not obvious.
      // We need to do is ask the rust engine for the changes. Although
      // this is kinda abusing the bridged-engine interface, we know the tabs
      // implementation of it works ok
      let outgoing = await this._bridge.apply();
      // We know we always have exactly 1 record.
      let mine = outgoing[0];
      this._log.trace("outgoing bso", mine);
      // `this._recordObj` is a `BridgedRecord`, which isn't exported.
      let record = this._recordObj.fromOutgoingBso(this.name, JSON.parse(mine));
      let changeset = {};
      changeset[record.id] = { synced: false, record };
      this._modified.replace(changeset);

      Async.checkAppReady();
      await this._uploadOutgoing();
      telemetryRecord.onEngineStop(name, null);
      return true;
    } catch (ex) {
      this._log.warn("quicksync sync failed", ex);
      telemetryRecord.onEngineStop(name, ex);
      return false;
    } finally {
      // The top-level sync is never considered to fail here, just the engine
      telemetryRecord.finished(null);
      SyncTelemetry.takeTelemetryRecord(telemetryRecord);
    }
  },

  async _sync() {
    try {
      await this._engineLock("tabs.js: fullSync", async () => {
        await super._sync();
      })();
    } catch (ex) {
      if (!Utils.isLockException(ex)) {
        throw ex;
      }
      this._log.info(
        "Can't do full tabs sync as a quick-write is currently running"
      );
    }
  },
};
Object.setPrototypeOf(TabEngine.prototype, BridgedEngine.prototype);

export const TabProvider = {
  getWindowEnumerator() {
    return Services.wm.getEnumerator("navigator:browser");
  },

  shouldSkipWindow(win) {
    return win.closed || lazy.PrivateBrowsingUtils.isWindowPrivate(win);
  },

  getAllBrowserTabs() {
    let tabs = [];
    for (let win of this.getWindowEnumerator()) {
      if (this.shouldSkipWindow(win)) {
        continue;
      }
      // Get all the tabs from the browser
      for (let tab of win.gBrowser.tabs) {
        tabs.push(tab);
      }
    }

    return tabs.sort(function (a, b) {
      return b.lastAccessed - a.lastAccessed;
    });
  },

  // This function creates tabs records up to a specified amount of bytes
  // It is an "estimation" since we don't accurately calculate how much the
  // favicon and JSON overhead is and give a rough estimate (for optimization purposes)
  async getAllTabsWithEstimatedMax(filter, bytesMax) {
    let log = Log.repository.getLogger(`Sync.Engine.Tabs.Provider`);
    let tabRecords = [];
    let iconPromises = [];
    let runningByteLength = 0;
    let encoder = new TextEncoder();

    // Fetch all the tabs the user has open
    let winTabs = this.getAllBrowserTabs();

    for (let tab of winTabs) {
      // We don't want to process any more tabs than we can sync
      if (runningByteLength >= bytesMax) {
        log.warn(
          `Can't fit all tabs in sync payload: have ${winTabs.length},
              but can only fit ${tabRecords.length}.`
        );
        break;
      }

      // Note that we used to sync "tab history" (ie, the "back button") state,
      // but in practice this hasn't been used - only the current URI is of
      // interest to clients.
      // We stopped recording this in bug 1783991.
      if (!tab?.linkedBrowser) {
        continue;
      }
      let acceptable = !filter
        ? url => url
        : url =>
            url &&
            !lazy.TABS_FILTERED_SCHEMES.has(Services.io.extractScheme(url));

      let url = tab.linkedBrowser.currentURI?.spec;
      // Special case for reader mode.
      if (url && url.startsWith("about:reader?")) {
        url = lazy.ReaderMode.getOriginalUrl(url);
      }
      // We ignore the tab completely if the current entry url is
      // not acceptable (we need something accurate to open).
      if (!acceptable(url)) {
        continue;
      }

      if (url.length > URI_LENGTH_MAX) {
        log.trace("Skipping over-long URL.");
        continue;
      }

      let thisTab = new lazy.RemoteTabRecord({
        title: tab.linkedBrowser.contentTitle || "",
        urlHistory: [url],
        icon: "",
        lastUsed: Math.floor((tab.lastAccessed || 0) / 1000),
      });
      tabRecords.push(thisTab);

      // we don't want to wait for each favicon to resolve to get the bytes
      // so we estimate a conservative 100 chars for the favicon and json overhead
      // Rust will further optimize and trim if we happened to be wildly off
      runningByteLength +=
        encoder.encode(thisTab.title + thisTab.lastUsed + url).byteLength + 100;

      // Use the favicon service for the icon url - we can wait for the promises at the end.
      let iconPromise = lazy.PlacesUtils.favicons
        .getFaviconForPage(lazy.PlacesUtils.toURI(url))
        .then(favicon => {
          thisTab.icon = favicon.uri.spec;
        })
        .catch(() => {
          log.trace(
            `Failed to fetch favicon for ${url}`,
            thisTab.urlHistory[0]
          );
        });
      iconPromises.push(iconPromise);
    }

    await Promise.allSettled(iconPromises);
    return tabRecords;
  },
};

function TabTracker(name, engine) {
  Tracker.call(this, name, engine);

  // Make sure "this" pointer is always set correctly for event listeners.
  this.onTab = Utils.bind2(this, this.onTab);
  this._unregisterListeners = Utils.bind2(this, this._unregisterListeners);
}
TabTracker.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  clearChangedIDs() {
    this.modified = false;
  },

  // We do not track TabSelect because that almost always triggers
  // the web progress listeners (onLocationChange), which we already track
  _topics: ["TabOpen", "TabClose"],

  _registerListenersForWindow(window) {
    this._log.trace("Registering tab listeners in window");
    for (let topic of this._topics) {
      window.addEventListener(topic, this.onTab);
    }
    window.addEventListener("unload", this._unregisterListeners);
    // If it's got a tab browser we can listen for things like navigation.
    if (window.gBrowser) {
      window.gBrowser.addProgressListener(this);
    }
  },

  _unregisterListeners(event) {
    this._unregisterListenersForWindow(event.target);
  },

  _unregisterListenersForWindow(window) {
    this._log.trace("Removing tab listeners in window");
    window.removeEventListener("unload", this._unregisterListeners);
    for (let topic of this._topics) {
      window.removeEventListener(topic, this.onTab);
    }
    if (window.gBrowser) {
      window.gBrowser.removeProgressListener(this);
    }
  },

  onStart() {
    Svc.Obs.add("domwindowopened", this.asyncObserver);
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      this._registerListenersForWindow(win);
    }
  },

  onStop() {
    Svc.Obs.remove("domwindowopened", this.asyncObserver);
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      this._unregisterListenersForWindow(win);
    }
  },

  async observe(subject, topic) {
    switch (topic) {
      case "domwindowopened":
        let onLoad = () => {
          subject.removeEventListener("load", onLoad);
          // Only register after the window is done loading to avoid unloads.
          this._registerListenersForWindow(subject);
        };

        // Add tab listeners now that a window has opened.
        subject.addEventListener("load", onLoad);
        break;
    }
  },

  onTab(event) {
    if (event.originalTarget.linkedBrowser) {
      let browser = event.originalTarget.linkedBrowser;
      if (
        lazy.PrivateBrowsingUtils.isBrowserPrivate(browser) &&
        !lazy.PrivateBrowsingUtils.permanentPrivateBrowsing
      ) {
        this._log.trace("Ignoring tab event from private browsing.");
        return;
      }
    }
    this._log.trace("onTab event: " + event.type);

    switch (event.type) {
      case "TabOpen":
        /* We do not have a reliable way of checking the URI on the TabOpen
         * so we will rely on the other methods (onLocationChange, getAllTabsWithEstimatedMax)
         * to filter these when going through sync
         */
        this.callScheduleSync(SCORE_INCREMENT_SMALL);
        break;
      case "TabClose":
        // If event target has `linkedBrowser`, the event target can be assumed <tab> element.
        // Else, event target is assumed <browser> element, use the target as it is.
        const tab = event.target.linkedBrowser || event.target;

        // TabClose means the tab has already loaded and we can check the URI
        // and ignore if it's a scheme we don't care about
        if (lazy.TABS_FILTERED_SCHEMES.has(tab.currentURI.scheme)) {
          return;
        }
        this.callScheduleSync(SCORE_INCREMENT_SMALL);
        break;
    }
  },

  // web progress listeners.
  onLocationChange(webProgress, request, locationURI, flags) {
    // We only care about top-level location changes. We do want location changes in the
    // same document because if a page uses the `pushState()` API, they *appear* as though
    // they are in the same document even if the URL changes. It also doesn't hurt to accurately
    // reflect the fragment changing - so we allow LOCATION_CHANGE_SAME_DOCUMENT
    if (
      flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD ||
      !webProgress.isTopLevel ||
      !locationURI
    ) {
      return;
    }

    // We can't filter out tabs that we don't sync here, because we might be
    // navigating from a tab that we *did* sync to one we do not, and that
    // tab we *did* sync should no longer be synced.
    this.callScheduleSync();
  },

  callScheduleSync(scoreIncrement) {
    this.modified = true;
    let { scheduler } = this.engine.service;
    let delayInMs = lazy.SYNC_AFTER_DELAY_MS;

    // Schedule a sync once we detect a tab change
    // to ensure the server always has the most up to date tabs
    if (
      delayInMs > 0 &&
      scheduler.numClients > 1 // Only schedule quick syncs for multi client users
    ) {
      if (this.tabsQuickWriteTimer) {
        this._log.debug(
          "Detected a tab change, but a quick-write is already scheduled"
        );
        return;
      }
      this._log.debug(
        "Detected a tab change: scheduling a quick-write in " + delayInMs + "ms"
      );
      CommonUtils.namedTimer(
        () => {
          this._log.trace("tab quick-sync timer fired.");
          this.engine
            .quickWrite()
            .then(() => {
              this._log.trace("tab quick-sync done.");
            })
            .catch(ex => {
              this._log.error("tab quick-sync failed.", ex);
            });
        },
        delayInMs,
        this,
        "tabsQuickWriteTimer"
      );
    } else if (scoreIncrement) {
      this._log.debug(
        "Detected a tab change, but conditions aren't met for a quick write - bumping score"
      );
      this.score += scoreIncrement;
    } else {
      this._log.debug(
        "Detected a tab change, but conditions aren't met for a quick write or a score bump"
      );
    }
  },
};
Object.setPrototypeOf(TabTracker.prototype, Tracker.prototype);
