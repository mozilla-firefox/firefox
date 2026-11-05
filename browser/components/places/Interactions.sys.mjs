/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  InteractionsBlocklist:
    "moz-src:///browser/components/places/InteractionsBlocklist.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "InteractionsManager",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.places.interactions.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  idleService: ["@mozilla.org/widget/useridleservice;1", Ci.nsIUserIdleService],
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "pageViewIdleTime",
  "browser.places.interactions.pageViewIdleTime",
  60
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "saveInterval",
  "browser.places.interactions.saveInterval",
  10000
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isHistoryEnabled",
  "places.history.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "breakupIfNoUpdatesForSeconds",
  "browser.places.interactions.breakupIfNoUpdatesForSeconds",
  60 * 60
);

const DOMWINDOW_OPENED_TOPIC = "domwindowopened";
const RECENT_BROWSER_INTERACTION_EXPIRY_TIME_MS = 60000;

/**
 * Returns a monotonically increasing timestamp, that is critical to distinguish
 * database entries by creation time.
 */
let gLastTime = 0;
function monotonicNow() {
  let time = Date.now();
  if (time == gLastTime) {
    time++;
  }
  return (gLastTime = time);
}

/**
 * @typedef {object} DocumentInfo
 *   DocumentInfo is used to pass document information from the child process
 *   to _Interactions.
 * @property {boolean} isActive
 *   Set to true if the document is active, i.e. visible.
 * @property {string} url
 *   The url of the page that was interacted with.
 */

/**
 * @typedef {object} InteractionInfo
 *   InteractionInfo is used to store information associated with interactions.
 * @property {number} totalViewTime
 *   Time in milliseconds that the page has been actively viewed for.
 * @property {string} url
 *   The url of the page that was interacted with.
 * @property {Interactions.DOCUMENT_TYPE} documentType
 *   The type of the document.
 * @property {number} typingTime
 *   Time in milliseconds that the user typed on the page
 * @property {number} keypresses
 *   The number of keypresses made on the page
 * @property {number} scrollingTime
 *   Time in milliseconds that the user spent scrolling the page
 * @property {number} scrollingDistance
 *   The distance, in pixels, that the user scrolled the page
 * @property {number} created_at
 *   Creation time as the number of milliseconds since the epoch.
 * @property {number} updated_at
 *   Last updated time as the number of milliseconds since the epoch.
 * @property {string} referrer
 *   The referrer to the url of the page that was interacted with (may be empty)
 */

/**
 * The Interactions object sets up listeners and other approriate tools for
 * obtaining interaction information and passing it to the InteractionsManager.
 */
class _Interactions {
  DOCUMENT_TYPE = {
    // Used when the document type is unknown.
    GENERIC: 0,
    // Used for pages serving media, e.g. videos.
    MEDIA: 1,
  };

  /**
   * This is used to store potential interactions. It maps the browser
   * to the current interaction information.
   * The current interaction is updated to the database when it transitions
   * to non-active, which occurs before a browser tab is closed, hence this
   * can be a weak map.
   *
   * @type {WeakMap<browser, InteractionInfo>}
   */
  #interactions = new WeakMap();

  /**
   * Tracks the currently active window so that we can avoid recording
   * interactions in non-active windows.
   *
   * @type {DOMWindow}
   */
  #activeWindow = undefined;

  /**
   * Tracks if the user is idle.
   *
   * @type {boolean}
   */
  #userIsIdle = false;

  /**
   * This stores the page view start time of the current page view.
   * For any single page view, this may be moved multiple times as the
   * associated interaction is updated for the current total page view time.
   *
   * @type {number}
   */
  _pageViewStartTime = ChromeUtils.now();

  /**
   * Stores interactions in the database, see the {@link InteractionsStore}
   * class. This is created lazily, see the `store` getter.
   *
   * @type {InteractionsStore | undefined}
   */
  #store = undefined;

  /**
   * Whether the component has been initialized.
   */
  #initialized = false;

  /**
   * Maps a browser to its interactions which are less than
   * RECENT_BROWSER_INTERACTION_EXPIRY_TIME_MS old.
   *
   * @type {WeakMap<browser, InteractionInfo[]>}
   */
  #recentInteractions = new WeakMap();

  /**
   * Initializes, sets up actors and observers.
   */
  init() {
    if (
      !Services.prefs.getBoolPref("browser.places.interactions.enabled", false)
    ) {
      return;
    }

    ChromeUtils.registerWindowActor("Interactions", {
      parent: {
        esModuleURI: "resource:///actors/InteractionsParent.sys.mjs",
      },
      child: {
        esModuleURI: "resource:///actors/InteractionsChild.sys.mjs",
        events: {
          DOMContentLoaded: {},
          pagehide: { mozSystemGroup: true },
        },
      },
      messageManagerGroups: ["browsers"],
      safeForUntrustedWebProcess: true,
    });

    this.#activeWindow = Services.wm.getMostRecentBrowserWindow();

    for (let win of lazy.BrowserWindowTracker.orderedWindows) {
      if (!win.closed) {
        this.#registerWindow(win);
      }
    }
    Services.obs.addObserver(this, DOMWINDOW_OPENED_TOPIC, true);
    lazy.idleService.addIdleObserver(this, lazy.pageViewIdleTime);

    this.#initialized = true;
  }

  /**
   * Uninitializes, removes any observers that need cleaning up manually.
   */
  uninit() {
    if (this.#initialized) {
      lazy.idleService.removeIdleObserver(this, lazy.pageViewIdleTime);
    }
  }

  /**
   * Resets any stored user or interaction state.
   * Used by tests.
   */
  async reset() {
    lazy.logConsole.debug("Database reset");
    this.#interactions = new WeakMap();
    this.#userIsIdle = false;
    this._pageViewStartTime = ChromeUtils.now();
    ChromeUtils.consumeInteractionData();
    await _Interactions.interactionUpdatePromise;
    await this.store.reset();
  }

  /**
   * Retrieve the underlying InteractionsStore object. This exists for testing
   * purposes and should not be abused by production code (for example it'd be
   * a bad idea to force flushes).
   *
   * @returns {InteractionsStore}
   */
  get store() {
    if (!this.#store) {
      this.#store = new InteractionsStore();
    }
    return this.#store;
  }

  /**
   * Registers the start of a new interaction.
   *
   * @param {Browser} browser
   *   The browser object associated with the interaction.
   * @param {DocumentInfo} docInfo
   *   The document information of the page associated with the interaction.
   */
  registerNewInteraction(browser, docInfo) {
    if (
      !browser ||
      !lazy.isHistoryEnabled ||
      !browser.browsingContext.useGlobalHistory
    ) {
      return;
    }
    let interaction = this.#interactions.get(browser);
    if (interaction && interaction.url != docInfo.url) {
      this.registerEndOfInteraction(browser);
    }

    if (lazy.InteractionsBlocklist.isUrlBlocklisted(docInfo.url)) {
      lazy.logConsole.debug(
        "Ignoring a page as the URL is blocklisted",
        docInfo
      );
      return;
    }

    lazy.logConsole.debug("Tracking a new interaction", docInfo);
    let now = monotonicNow();
    interaction = {
      url: docInfo.url,
      referrer: docInfo.referrer,
      totalViewTime: 0,
      typingTime: 0,
      keypresses: 0,
      scrollingTime: 0,
      scrollingDistance: 0,
      created_at: now,
      updated_at: now,
    };
    this.#interactions.set(browser, interaction);

    // Only reset the time if this is being loaded in the active tab of the
    // active window.
    if (docInfo.isActive && browser.documentGlobal == this.#activeWindow) {
      this._pageViewStartTime = ChromeUtils.now();
    }

    this.#recentInteractions.set(browser, [
      ...(this.#recentInteractions.get(browser) ?? []),
      interaction,
    ]);

    this.#pruneOldRecentInteractions(browser);
  }

  /**
   * Registers the end of an interaction, e.g. if the user navigates away
   * from the page. This will store the final interaction details and clear
   * the current interaction.
   *
   * @param {Browser} browser
   *   The browser object associated with the interaction.
   */
  registerEndOfInteraction(browser) {
    // Not having a browser passed to us probably means the tab has gone away
    // before we received the notification - due to the tab being a background
    // tab. Since that will be a non-active tab, it is acceptable that we don't
    // update the interaction. When switching away from active tabs, a TabSelect
    // notification is generated which we handle elsewhere.
    if (
      !browser ||
      !lazy.isHistoryEnabled ||
      !browser.browsingContext.useGlobalHistory
    ) {
      return;
    }
    lazy.logConsole.debug("Saw the end of an interaction");

    this.#updateInteraction(browser);
    this.#interactions.delete(browser);
  }

  /**
   * Updates the current interaction
   *
   * @param {Browser} [browser]
   *   The browser object that has triggered the update, if known. This is
   *   used to check if the browser is in the active window, and as an
   *   optimization to avoid obtaining the browser object.
   */
  #updateInteraction(browser = undefined) {
    _Interactions.#updateInteraction_async(
      browser,
      this.#activeWindow,
      this.#userIsIdle,
      this.#interactions,
      this._pageViewStartTime,
      this.store
    );
  }

  /**
   * Fetches recent interactions for a browser
   *
   * @param {Browser} browser
   *   The browser object that we are fetching recent interactions for.
   */
  async getRecentInteractionsForBrowser(browser) {
    // We need to force update the active interaction's total view time
    // to get an accurate reading.
    this.#updateInteraction();
    await _Interactions.interactionUpdatePromise;
    return this.#recentInteractions.get(browser);
  }

  /**
   * Removes stale interactions from #recentInteractions that were updated
   * more than RECENT_BROWSER_INTERACTION_EXPIRY_TIME_MS ago.
   *
   * @param {Browser} browser
   *  The browser object that we are pruning stale recent interactions for.
   */
  #pruneOldRecentInteractions(browser) {
    const now = Date.now();

    const interactions = this.#recentInteractions.get(browser);
    if (!interactions) {
      return;
    }

    const interactionstoTrack = interactions.filter(
      interaction =>
        now - interaction.updated_at <=
        RECENT_BROWSER_INTERACTION_EXPIRY_TIME_MS
    );

    if (interactionstoTrack.length) {
      this.#recentInteractions.set(browser, interactionstoTrack);
    } else {
      this.#recentInteractions.delete(browser);
    }
  }

  /**
   * Stores the promise created in updateInteraction_async so that we can await its fulfillment
   * when sychronization is needed.
   */
  static interactionUpdatePromise = Promise.resolve();

  /**
   * Returns the interactions update promise to be used when sychronization is needed from tests.
   *
   * @returns {Promise<void>}
   */
  get interactionUpdatePromise() {
    return _Interactions.interactionUpdatePromise;
  }

  /**
   * Updates the current interaction on fulfillment of the asynchronous collection of scrolling interactions.
   *
   * @param {Browser} browser
   *   The browser object that has triggered the update, if known.
   * @param {DOMWindow} activeWindow
   *   The active window.
   * @param {boolean} userIsIdle
   *   Whether the user is idle.
   * @param {WeakMap<Browser, InteractionInfo>} interactions
   *   A map of interactions for each browser instance
   * @param {number} pageViewStartTime
   *   The time the page was loaded.
   * @param {InteractionsStore} store
   *   The interactions store.
   */
  static async #updateInteraction_async(
    browser,
    activeWindow,
    userIsIdle,
    interactions,
    pageViewStartTime,
    store
  ) {
    if (!activeWindow || (browser && browser.documentGlobal != activeWindow)) {
      lazy.logConsole.debug(
        "Not updating interaction as there is no active window"
      );
      return;
    }

    // We do not update the interaction when the user is idle, since we will
    // have already updated it when idle was signalled.
    // Sometimes an interaction may be signalled before idle is cleared, however
    // worst case we'd only loose approx 2 seconds of interaction detail.
    if (userIsIdle) {
      lazy.logConsole.debug("Not updating interaction as the user is idle");
      return;
    }

    if (!browser) {
      browser = activeWindow.gBrowser.selectedTab.linkedBrowser;
    }

    let interaction = interactions.get(browser);
    if (!interaction) {
      lazy.logConsole.debug("No interaction to update");
      return;
    }

    interaction.totalViewTime += ChromeUtils.now() - pageViewStartTime;
    Interactions._pageViewStartTime = ChromeUtils.now();

    const interactionData = ChromeUtils.consumeInteractionData();
    const typing = interactionData.Typing;
    if (typing) {
      interaction.typingTime += typing.interactionTimeInMilliseconds;
      interaction.keypresses += typing.interactionCount;
    }

    // Collect the scrolling data and add the interaction to the store on completion
    _Interactions.interactionUpdatePromise =
      _Interactions.interactionUpdatePromise
        .then(async () => ChromeUtils.collectScrollingData())
        .then(
          result => {
            interaction.scrollingTime += result.interactionTimeInMilliseconds;
            interaction.scrollingDistance += result.scrollingDistanceInPixels;
          },
          reason => {
            console.error(reason);
          }
        )
        .then(() => {
          interaction.updated_at = monotonicNow();

          lazy.logConsole.debug("Add to store: ", interaction);
          store.add(interaction);
        });
  }

  /**
   * Handles a window becoming active.
   *
   * @param {DOMWindow} win
   *   The window that has become active.
   */
  #onActivateWindow(win) {
    lazy.logConsole.debug("Window activated");

    if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }

    this.#activeWindow = win;
    this._pageViewStartTime = ChromeUtils.now();
  }

  /**
   * Handles a window going inactive.
   */
  #onDeactivateWindow() {
    lazy.logConsole.debug("Window deactivate");

    this.#updateInteraction();
    this.#activeWindow = undefined;
  }

  /**
   * Handles the TabSelect notification. If enough time has passed between the
   * current time and the last time the current tab was selected and interacted
   * with, the existing interaction will end, and a new one will begin. This
   * approach accounts for scenarios where a user might leave a tab open for an
   * extended period (e.g. pinned tabs), and engage in distinct sessions. A
   * delay is used to prevent the creation of numerous short, separate
   * interactions that may occur when a user quickly switches between tabs.
   *
   * @param {Browser} previousBrowser
   *   The instance of the browser that the user switched away from.
   */
  #onTabSelect(previousBrowser) {
    lazy.logConsole.debug("Tab switched");

    this.#updateInteraction(previousBrowser);

    this._pageViewStartTime = ChromeUtils.now();

    let browser = this.#activeWindow?.gBrowser.selectedBrowser;
    if (browser && this.#interactions.has(browser)) {
      let interaction = this.#interactions.get(browser);
      let timePassedSinceUpdateSeconds =
        (Date.now() - interaction.updated_at) / 1000;
      if (timePassedSinceUpdateSeconds >= lazy.breakupIfNoUpdatesForSeconds) {
        this.registerEndOfInteraction(browser);
        this.registerNewInteraction(browser, {
          url: browser.currentURI.spec,
          referrer: null,
          isActive: true,
        });
      }
    }
  }

  /**
   * Handles various events and forwards them to appropriate functions.
   *
   * @param {DOMEvent} event
   *   The event that will be handled
   */
  handleEvent(event) {
    switch (event.type) {
      case "TabSelect":
        this.#onTabSelect(event.detail.previousTab.linkedBrowser);
        break;
      case "activate":
        this.#onActivateWindow(event.target);
        break;
      case "deactivate":
        this.#onDeactivateWindow(event.target);
        break;
      case "unload":
        this.#unregisterWindow(event.target);
        break;
    }
  }

  /**
   * Handles notifications from the observer service.
   *
   * @param {nsISupports} subject
   *   The subject of the notification.
   * @param {string} topic
   *   The topic of the notification.
   */
  observe(subject, topic) {
    switch (topic) {
      case DOMWINDOW_OPENED_TOPIC:
        this.#onWindowOpen(subject);
        break;
      case "idle":
        lazy.logConsole.debug("User went idle");
        // We save the state of the current interaction when we are notified
        // that the user is idle.
        this.#updateInteraction();
        this.#userIsIdle = true;
        break;
      case "active":
        lazy.logConsole.debug("User became active");
        this.#userIsIdle = false;
        this._pageViewStartTime = ChromeUtils.now();
        break;
    }
  }

  /**
   * Handles registration of listeners in a new window.
   *
   * @param {DOMWindow} win
   *   The window to register in.
   */
  #registerWindow(win) {
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
      return;
    }

    win.addEventListener("TabSelect", this, true);
    win.addEventListener("deactivate", this, true);
    win.addEventListener("activate", this, true);
  }

  /**
   * Handles removing of listeners from a window.
   *
   * @param {DOMWindow} win
   *   The window to remove listeners from.
   */
  #unregisterWindow(win) {
    win.removeEventListener("TabSelect", this, true);
    win.removeEventListener("deactivate", this, true);
    win.removeEventListener("activate", this, true);
  }

  /**
   * Handles a new window being opened, waits for load and checks that
   * it is a browser window, then adds listeners.
   *
   * @param {DOMWindow} win
   *   The window being opened.
   */
  #onWindowOpen(win) {
    win.addEventListener(
      "load",
      () => {
        if (
          win.document.documentElement.getAttribute("windowtype") !=
          "navigator:browser"
        ) {
          return;
        }
        this.#registerWindow(win);
      },
      { once: true }
    );
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]);
}

export const Interactions = new _Interactions();

/**
 * Store interactions data in the Places database.
 * To improve performance the writes are buffered every `saveInterval`
 * milliseconds. Even if this means we could be trying to write interaction for
 * pages that in the meanwhile have been removed, that's not a problem because
 * we won't be able to insert entries having a NULL place_id, they will just be
 * ignored.
 * Use .add(interaction) to request storing of an interaction.
 * Use .pendingPromise to await for any pending writes to have happened.
 */
class InteractionsStore {
  /**
   * Timer to run database updates on.
   */
  #timer = undefined;
  /**
   * Tracks interactions replicating the unique index in the underlying schema.
   * Interactions are keyed by url and then created_at.
   *
   * @type {Map<string, Map<number, InteractionInfo>>}
   */
  #interactions = new Map();
  /**
   * Used to unblock the queue of promises when the timer is cleared.
   */
  #timerResolve = undefined;

  constructor() {
    // Block async shutdown to ensure the last write goes through.
    this.progress = {};
    lazy.PlacesUtils.history.shutdownClient.jsclient.addBlocker(
      "Interactions.sys.mjs:: store",
      async () => this.flush(),
      { fetchState: () => this.progress }
    );

    // Can be used to wait for the last pending write to have happened.
    this.pendingPromise = Promise.resolve();
  }

  /**
   * Synchronizes the pending interactions with the storage device.
   *
   * @returns {Promise} resolved when the pending data is on disk.
   */
  async flush() {
    if (this.#timer) {
      lazy.clearTimeout(this.#timer);
      this.#timerResolve();
      await this.#updateDatabase();
    }
  }

  /**
   * Completely clears the store and any pending writes.
   * This exists for testing purposes.
   */
  async reset() {
    await lazy.PlacesUtils.withConnectionWrapper(
      "Interactions.sys.mjs::reset",
      async db => {
        await db.executeCached(`DELETE FROM moz_places_metadata`);
      }
    );
    if (this.#timer) {
      lazy.clearTimeout(this.#timer);
      this.#timer = undefined;
      this.#timerResolve();
      this.#interactions.clear();
    }
  }

  /**
   * Registers an interaction to be stored persistently. At the end of the call
   * the interaction has not yet been added to the store, tests can await
   * flushStore() for that.
   *
   * @param {InteractionInfo} interaction
   *   The document information to write.
   */
  add(interaction) {
    lazy.logConsole.debug("Preparing interaction for storage", interaction);

    let interactionsForUrl = this.#interactions.get(interaction.url);
    if (!interactionsForUrl) {
      interactionsForUrl = new Map();
      this.#interactions.set(interaction.url, interactionsForUrl);
    }
    interactionsForUrl.set(interaction.created_at, interaction);

    if (!this.#timer) {
      let promise = new Promise(resolve => {
        this.#timerResolve = resolve;
        this.#timer = lazy.setTimeout(() => {
          this.#updateDatabase().catch(console.error).then(resolve);
        }, lazy.saveInterval);
      });
      this.pendingPromise = this.pendingPromise.then(() => promise);
    }
  }

  async #updateDatabase() {
    this.#timer = undefined;

    // Reset the buffer.
    let interactions = this.#interactions;
    if (!interactions.size) {
      return;
    }
    // Don't clear() this, since that would also clear interactions.
    this.#interactions = new Map();

    let params = {};
    let SQLInsertFragments = [];
    let i = 0;
    for (let interactionsForUrl of interactions.values()) {
      for (let interaction of interactionsForUrl.values()) {
        params[`url${i}`] = interaction.url;
        params[`referrer${i}`] = interaction.referrer;
        params[`created_at${i}`] = interaction.created_at;
        params[`updated_at${i}`] = interaction.updated_at;
        params[`document_type${i}`] =
          interaction.documentType ?? Interactions.DOCUMENT_TYPE.GENERIC;
        params[`total_view_time${i}`] =
          Math.round(interaction.totalViewTime) || 0;
        params[`typing_time${i}`] = Math.round(interaction.typingTime) || 0;
        params[`key_presses${i}`] = interaction.keypresses || 0;
        params[`scrolling_time${i}`] =
          Math.round(interaction.scrollingTime) || 0;
        params[`scrolling_distance${i}`] =
          Math.round(interaction.scrollingDistance) || 0;
        SQLInsertFragments.push(`(
          (SELECT id FROM moz_places_metadata
            WHERE place_id = (SELECT id FROM moz_places WHERE url_hash = hash(:url${i}) AND url = :url${i})
              AND created_at = :created_at${i}),
          (SELECT id FROM moz_places WHERE url_hash = hash(:url${i}) AND url = :url${i}),
          (SELECT id FROM moz_places WHERE url_hash = hash(:referrer${i}) AND url = :referrer${i} AND :referrer${i} != :url${i}),
          :created_at${i},
          :updated_at${i},
          :document_type${i},
          :total_view_time${i},
          :typing_time${i},
          :key_presses${i},
          :scrolling_time${i},
          :scrolling_distance${i}
        )`);
        i++;
      }
    }

    lazy.logConsole.debug(`Storing ${i} entries in the database`);

    this.progress.pendingUpdates = i;
    await lazy.PlacesUtils.withConnectionWrapper(
      "Interactions.sys.mjs::updateDatabase",
      async db => {
        await db.executeCached(
          `
          WITH inserts (id, place_id, referrer_place_id, created_at, updated_at, document_type, total_view_time, typing_time, key_presses, scrolling_time, scrolling_distance) AS (
            VALUES ${SQLInsertFragments.join(", ")}
          )
          INSERT OR REPLACE INTO moz_places_metadata (
            id, place_id, referrer_place_id, created_at, updated_at, document_type, total_view_time, typing_time, key_presses, scrolling_time, scrolling_distance
          ) SELECT * FROM inserts WHERE place_id NOT NULL;
        `,
          params
        );
      }
    );
    this.progress.pendingUpdates = 0;

    Services.obs.notifyObservers(null, "places-metadata-updated");
  }
}
