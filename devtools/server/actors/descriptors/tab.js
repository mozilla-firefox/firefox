/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

loader.lazyGetter(this, "logger", function () {
  return console.createInstance({
    logModule: "devtools_tabdescriptor",
    maxLogLevel: "Warn",
  });
});

/*
 * Descriptor Actor that represents a Tab in the parent process. It
 * launches a WindowGlobalTargetActor in the content process to do the real work and tunnels the
 * data.
 *
 * See devtools/docs/backend/actor-hierarchy.md for more details.
 */

const { Actor } = require("resource://devtools/shared/protocol.js");
const {
  tabDescriptorSpec,
} = require("resource://devtools/shared/specs/descriptors/tab.js");

const lazy = {};
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  },
  { global: "contextual" }
);

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs",
  { global: "contextual" }
);
const {
  createBrowserElementSessionContext,
} = require("resource://devtools/server/actors/watcher/session-context.js");

loader.lazyRequireGetter(
  this,
  "WatcherActor",
  "resource://devtools/server/actors/watcher.js",
  true
);
loader.lazyRequireGetter(
  this,
  "connectToFrame",
  "resource://devtools/server/connectors/frame-connector.js",
  true
);

/**
 * Creates a target actor proxy for handling requests to a single browser frame.
 * Both <xul:browser> and <iframe mozbrowser> are supported.
 * This actor is a shim that connects to a WindowGlobalTargetActor in a remote browser process.
 * All RDP packets get forwarded using the message manager.
 *
 * @param connection The main RDP connection.
 * @param browser <xul:browser> or <iframe mozbrowser> element to connect to.
 */
class TabDescriptorActor extends Actor {
  constructor(connection, browser) {
    super(connection, tabDescriptorSpec);
    this._browser = browser;
  }

  form() {
    const form = {
      actor: this.actorID,
      browserId: this._browser.browserId,
      browsingContextID:
        this._browser && this._browser.browsingContext
          ? this._browser.browsingContext.id
          : null,
      isZombieTab: this._isZombieTab(),
      outerWindowID: this._getOuterWindowId(),
      selected: this.selected,
      title: this._getTitle(),
      traits: {
        // Supports the Watcher actor. Can be removed as part of Bug 1680280.
        watcher: true,
        supportsReloadDescriptor: true,
        // Tab descriptor is the only one to support navigation
        supportsNavigation: true,
      },
      url: this._getUrl(),
    };

    return form;
  }

  _getTitle() {
    // If the content already provides a title, use it.
    if (this._browser.contentTitle) {
      return this._browser.contentTitle;
    }

    // For zombie or lazy tabs (tab created, but content has not been loaded),
    // try to retrieve the title from the XUL Tab itself.
    // Note: this only works on Firefox desktop.
    if (this._tabbrowser) {
      const tab = this._tabbrowser.getTabForBrowser(this._browser);
      if (tab) {
        return tab.label;
      }
    }

    // No title available.
    return null;
  }

  _getUrl() {
    if (!this._browser || !this._browser.browsingContext) {
      return "";
    }

    const { browsingContext } = this._browser;
    return browsingContext.currentWindowGlobal.documentURI.spec;
  }

  _getOuterWindowId() {
    if (!this._browser || !this._browser.browsingContext) {
      return "";
    }

    const { browsingContext } = this._browser;
    return browsingContext.currentWindowGlobal.outerWindowId;
  }

  get selected() {
    // getMostRecentBrowserWindow will find the appropriate window on Firefox
    // Desktop and on GeckoView.
    const topAppWindow = Services.wm.getMostRecentBrowserWindow();

    const selectedBrowser = topAppWindow?.gBrowser?.selectedBrowser;
    if (!selectedBrowser) {
      // Note: gBrowser is not available on GeckoView.
      // We should find another way to know if this browser is the selected
      // browser. See Bug 1631020.
      return false;
    }

    return this._browser === selectedBrowser;
  }

  async getTarget() {
    if (!this.conn) {
      return {
        error: "tabDestroyed",
        message: "Tab destroyed while performing a TabDescriptorActor update",
      };
    }

    /* eslint-disable-next-line no-async-promise-executor */
    return new Promise(async (resolve, reject) => {
      const onDestroy = () => {
        // Reject the update promise if the tab was destroyed while requesting an update
        reject({
          error: "tabDestroyed",
          message: "Tab destroyed while performing a TabDescriptorActor update",
        });

        // Targets created from the TabDescriptor are not created via JSWindowActors and
        // we need to notify the watcher manually about their destruction.
        // TabDescriptor's targets are created via TabDescriptor.getTarget and are still using
        // message manager instead of JSWindowActors.
        if (this.watcher && this.targetActorForm) {
          this.watcher.notifyTargetDestroyed(this.targetActorForm);
        }
      };

      try {
        // Check if the browser is still connected before calling connectToFrame
        if (!this._browser.isConnected) {
          onDestroy();
          return;
        }

        const connectForm = await connectToFrame(
          this.conn,
          this._browser,
          onDestroy
        );
        this.targetActorForm = connectForm;
        resolve(connectForm);
      } catch (e) {
        reject({
          error: "tabDestroyed",
          message: "Tab destroyed while connecting to the frame",
        });
      }
    });
  }

  /**
   * Return a Watcher actor, allowing to keep track of targets which
   * already exists or will be created. It also helps knowing when they
   * are destroyed.
   */
  getWatcher(config) {
    if (!this.watcher) {
      this.watcher = new WatcherActor(
        this.conn,
        createBrowserElementSessionContext(this._browser, {
          isServerTargetSwitchingEnabled: config.isServerTargetSwitchingEnabled,
          isPopupDebuggingEnabled: config.isPopupDebuggingEnabled,
        })
      );
      this.manage(this.watcher);
    }
    return this.watcher;
  }

  get _tabbrowser() {
    if (this._browser && typeof this._browser.getTabBrowser == "function") {
      return this._browser.getTabBrowser();
    }
    return null;
  }

  async getFavicon() {
    if (!AppConstants.MOZ_PLACES) {
      // PlacesUtils is not supported
      return null;
    }

    try {
      const favicon = await lazy.PlacesUtils.favicons.getFaviconForPage(
        lazy.PlacesUtils.toURI(this._getUrl())
      );
      return favicon.rawData;
    } catch (e) {
      // Favicon unavailable for this url.
      return null;
    }
  }

  _isZombieTab() {
    // Note: GeckoView doesn't support zombie tabs
    const tabbrowser = this._tabbrowser;
    const tab = tabbrowser ? tabbrowser.getTabForBrowser(this._browser) : null;
    return tab?.hasAttribute && tab.hasAttribute("pending");
  }

  /**
   * Navigate this tab to a new URL.
   *
   * @param {string} url
   * @param {boolean} waitForLoad
   * @return {Promise}
   *         A promise which resolves only once the requested URL is fully loaded.
   */
  async navigateTo(url, waitForLoad = true) {
    if (!this._browser || !this._browser.browsingContext) {
      throw new Error("Tab is destroyed");
    }

    let validURL;
    try {
      validURL = Services.io.newURI(url);
    } catch (e) {
      throw new Error("Error: Cannot navigate to invalid URL: " + url);
    }

    // No platform supported object or id tracking a single navigation, create
    // a custom uuid.
    const navUUID = Services.uuid.generateUUID().toString().slice(1, -1);
    const navigationInfo = `context=${this._browser.browsingContext.id}, navUUID=${navUUID}`;
    logger.debug(
      `navigateTo starting with validURL=${validURL.spec} (${navigationInfo})`
    );

    // Setup a nsIWebProgressListener in order to be able to know when the
    // new document is done loading.
    const deferred = Promise.withResolvers();
    const listener = {
      onStateChange(webProgress, request, stateFlags, status) {
        // Avoid building the log string if not necessary.
        if (logger.shouldLog("Debug")) {
          logger.debug(
            `navigateTo onStateChange ` +
              `isStart=${!!(stateFlags & Ci.nsIWebProgressListener.STATE_START)} ` +
              `isStop=${!!(stateFlags & Ci.nsIWebProgressListener.STATE_STOP)} ` +
              `isWindow=${!!(stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)} ` +
              `status=${status} (${navigationInfo})`
          );
        }
        if (
          webProgress.isTopLevel &&
          stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW &&
          stateFlags &
            // Either wait for the start or the end of the document load
            (waitForLoad
              ? Ci.nsIWebProgressListener.STATE_STOP
              : Ci.nsIWebProgressListener.STATE_START)
        ) {
          const loadedURL = request.QueryInterface(Ci.nsIChannel).originalURI
            .spec;
          logger.debug(
            `navigateTo onStateChange check loadedURL=${loadedURL}, expectedURL=${validURL.spec} (${navigationInfo})`
          );
          if (loadedURL === validURL.spec) {
            logger.debug(
              `navigateTo onStateChange resolve (${navigationInfo})`
            );
            deferred.resolve();
          }
        }
      },

      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
    };
    this._browser.addProgressListener(
      listener,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
    );
    this._browser.browsingContext.loadURI(validURL, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });

    await deferred.promise;

    logger.debug(`navigateTo completed (${navigationInfo})`);
    this._browser.removeProgressListener(
      listener,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW
    );
  }

  goBack() {
    if (!this._browser || !this._browser.browsingContext) {
      throw new Error("Tab is destroyed");
    }

    this._browser.browsingContext.goBack();
  }

  goForward() {
    if (!this._browser || !this._browser.browsingContext) {
      throw new Error("Tab is destroyed");
    }

    this._browser.browsingContext.goForward();
  }

  reloadDescriptor({ bypassCache }) {
    if (!this._browser || !this._browser.browsingContext) {
      return;
    }

    this._browser.browsingContext.reload(
      bypassCache
        ? Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE
        : Ci.nsIWebNavigation.LOAD_FLAGS_NONE
    );
  }

  // When we move the tab to another top level window,
  // a TabClose with adoptedBy attribute refering to the new tab is fired.
  // We need to update to the newly create <browser> element.
  // Note that it keeps the same browserId/browsingContext.
  swapBrowser(newBrowser) {
    this._browser = newBrowser;
  }

  destroy() {
    this.emit("descriptor-destroyed");
    this._browser = null;

    super.destroy();
  }
}

exports.TabDescriptorActor = TabDescriptorActor;
