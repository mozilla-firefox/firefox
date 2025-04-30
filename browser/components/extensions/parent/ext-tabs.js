/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  ExtensionControlledPopup:
    "resource:///modules/ExtensionControlledPopup.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "strBundle", function () {
  return Services.strings.createBundle(
    "chrome://global/locale/extensions.properties"
  );
});

var { DefaultMap, ExtensionError } = ExtensionUtils;

const TAB_HIDE_CONFIRMED_TYPE = "tabHideNotification";

const TAB_ID_NONE = -1;

ChromeUtils.defineLazyGetter(this, "tabHidePopup", () => {
  return new ExtensionControlledPopup({
    confirmedType: TAB_HIDE_CONFIRMED_TYPE,
    popupnotificationId: "extension-tab-hide-notification",
    descriptionId: "extension-tab-hide-notification-description",
    descriptionMessageId: "tabHideControlled.message",
    getLocalizedDescription: (doc, message, addonDetails) => {
      let image = doc.createXULElement("image");
      image.classList.add("extension-controlled-icon", "alltabs-icon");
      if (!doc.getElementById("alltabs-button")?.closest("#TabsToolbar")) {
        image.classList.add("alltabs-icon-generic");
      }
      return BrowserUIUtils.getLocalizedFragment(
        doc,
        message,
        addonDetails,
        image
      );
    },
    learnMoreLink: "extension-hiding-tabs",
  });
});

function showHiddenTabs(id) {
  for (let win of Services.wm.getEnumerator("navigator:browser")) {
    if (win.closed || !win.gBrowser) {
      continue;
    }

    for (let tab of win.gBrowser.tabs) {
      if (
        tab.hidden &&
        tab.ownerGlobal &&
        SessionStore.getCustomTabValue(tab, "hiddenBy") === id
      ) {
        win.gBrowser.showTab(tab);
      }
    }
  }
}

let tabListener = {
  tabReadyInitialized: false,
  // Map[tab -> Promise]
  tabBlockedPromises: new WeakMap(),
  // Map[tab -> Deferred]
  tabReadyPromises: new WeakMap(),
  initializingTabs: new WeakSet(),

  initTabReady() {
    if (!this.tabReadyInitialized) {
      windowTracker.addListener("progress", this);

      this.tabReadyInitialized = true;
    }
  },

  onLocationChange(browser, webProgress) {
    if (webProgress.isTopLevel) {
      let { gBrowser } = browser.ownerGlobal;
      let nativeTab = gBrowser.getTabForBrowser(browser);

      // Now we are certain that the first page in the tab was loaded.
      this.initializingTabs.delete(nativeTab);

      // browser.innerWindowID is now set, resolve the promises if any.
      let deferred = this.tabReadyPromises.get(nativeTab);
      if (deferred) {
        deferred.resolve(nativeTab);
        this.tabReadyPromises.delete(nativeTab);
      }
    }
  },

  blockTabUntilRestored(nativeTab) {
    let promise = ExtensionUtils.promiseEvent(nativeTab, "SSTabRestored").then(
      ({ target }) => {
        this.tabBlockedPromises.delete(target);
        return target;
      }
    );

    this.tabBlockedPromises.set(nativeTab, promise);
  },

  /**
   * Returns a promise that resolves when the tab is ready.
   * Tabs created via the `tabs.create` method are "ready" once the location
   * changes to the requested URL. Other tabs are assumed to be ready once their
   * inner window ID is known.
   *
   * @param {XULElement} nativeTab The <tab> element.
   * @returns {Promise} Resolves with the given tab once ready.
   */
  awaitTabReady(nativeTab) {
    let deferred = this.tabReadyPromises.get(nativeTab);
    if (!deferred) {
      let promise = this.tabBlockedPromises.get(nativeTab);
      if (promise) {
        return promise;
      }
      deferred = Promise.withResolvers();
      if (
        !this.initializingTabs.has(nativeTab) &&
        (nativeTab.linkedBrowser.innerWindowID ||
          nativeTab.linkedBrowser.currentURI.spec === "about:blank")
      ) {
        deferred.resolve(nativeTab);
      } else {
        this.initTabReady();
        this.tabReadyPromises.set(nativeTab, deferred);
      }
    }
    return deferred.promise;
  },
};

const allAttrs = new Set([
  "attention",
  "audible",
  "favIconUrl",
  "mutedInfo",
  "sharingState",
  "title",
  "autoDiscardable",
]);
const allProperties = new Set([
  "attention",
  "audible",
  "autoDiscardable",
  "discarded",
  "favIconUrl",
  "groupId",
  "hidden",
  "isArticle",
  "mutedInfo",
  "openerTabId",
  "pinned",
  "sharingState",
  "status",
  "title",
  "url",
]);
const restricted = new Set(["url", "favIconUrl", "title"]);

this.tabs = class extends ExtensionAPIPersistent {
  static onUpdate(id, manifest) {
    if (!manifest.permissions || !manifest.permissions.includes("tabHide")) {
      showHiddenTabs(id);
    }
  }

  static onDisable(id) {
    showHiddenTabs(id);
    tabHidePopup.clearConfirmation(id);
  }

  static onUninstall(id) {
    tabHidePopup.clearConfirmation(id);
  }

  tabEventRegistrar({ event, listener }) {
    let { extension } = this;
    let { tabManager } = extension;
    return ({ fire }) => {
      let listener2 = (eventName, eventData, ...args) => {
        if (!tabManager.canAccessTab(eventData.nativeTab)) {
          return;
        }

        listener(fire, eventData, ...args);
      };

      tabTracker.on(event, listener2);
      return {
        unregister() {
          tabTracker.off(event, listener2);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    };
  }

  PERSISTENT_EVENTS = {
    onActivated: this.tabEventRegistrar({
      event: "tab-activated",
      listener: (fire, event) => {
        let { extension } = this;
        let { tabId, windowId, previousTabId, previousTabIsPrivate } = event;
        if (previousTabIsPrivate && !extension.privateBrowsingAllowed) {
          previousTabId = undefined;
        }
        fire.async({ tabId, previousTabId, windowId });
      },
    }),
    onAttached: this.tabEventRegistrar({
      event: "tab-attached",
      listener: (fire, event) => {
        fire.async(event.tabId, {
          newWindowId: event.newWindowId,
          newPosition: event.newPosition,
        });
      },
    }),
    onCreated: this.tabEventRegistrar({
      event: "tab-created",
      listener: (fire, event) => {
        let { tabManager } = this.extension;
        fire.async(tabManager.convert(event.nativeTab, event.currentTabSize));
      },
    }),
    onDetached: this.tabEventRegistrar({
      event: "tab-detached",
      listener: (fire, event) => {
        fire.async(event.tabId, {
          oldWindowId: event.oldWindowId,
          oldPosition: event.oldPosition,
        });
      },
    }),
    onRemoved: this.tabEventRegistrar({
      event: "tab-removed",
      listener: (fire, event) => {
        fire.async(event.tabId, {
          windowId: event.windowId,
          isWindowClosing: event.isWindowClosing,
        });
      },
    }),
    onMoved({ fire }) {
      let { tabManager } = this.extension;
      /**
       * @param {CustomEvent} event
       */
      let moveListener = event => {
        let nativeTab = event.originalTarget;
        let { previousTabState, currentTabState } = event.detail;
        let fromIndex = previousTabState.tabIndex;
        let toIndex = currentTabState.tabIndex;
        // TabMove also fires if its tab group changes; we should only fire
        // event if the position actually moved.
        if (fromIndex !== toIndex && tabManager.canAccessTab(nativeTab)) {
          fire.async(tabTracker.getId(nativeTab), {
            windowId: windowTracker.getId(nativeTab.ownerGlobal),
            fromIndex,
            toIndex,
          });
        }
      };

      windowTracker.addListener("TabMove", moveListener);
      return {
        unregister() {
          windowTracker.removeListener("TabMove", moveListener);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },

    onHighlighted({ fire, context }) {
      let { windowManager } = this.extension;
      let highlightListener = (eventName, event) => {
        // TODO see if we can avoid "context" here
        let window = windowTracker.getWindow(event.windowId, context, false);
        if (!window) {
          return;
        }
        let windowWrapper = windowManager.getWrapper(window);
        if (!windowWrapper) {
          return;
        }
        let tabIds = Array.from(
          windowWrapper.getHighlightedTabs(),
          tab => tab.id
        );
        fire.async({ tabIds: tabIds, windowId: event.windowId });
      };

      tabTracker.on("tabs-highlighted", highlightListener);
      return {
        unregister() {
          tabTracker.off("tabs-highlighted", highlightListener);
        },
        convert(_fire, _context) {
          fire = _fire;
          context = _context;
        },
      };
    },

    onUpdated({ fire, context }, params) {
      let { extension } = this;
      let { tabManager } = extension;
      let [filterProps] = params;
      let filter = { ...filterProps };
      if (filter.urls) {
        filter.urls = new MatchPatternSet(filter.urls, {
          restrictSchemes: false,
        });
      }
      let needsModified = true;
      if (filter.properties) {
        // Default is to listen for all events.
        needsModified = filter.properties.some(p => allAttrs.has(p));
        filter.properties = new Set(filter.properties);
      } else {
        filter.properties = allProperties;
      }

      function sanitize(tab, changeInfo) {
        let result = {};
        let nonempty = false;
        for (let prop in changeInfo) {
          // In practice, changeInfo contains at most one property from
          // restricted. Therefore it is not necessary to cache the value
          // of tab.hasTabPermission outside the loop.
          // Unnecessarily accessing tab.hasTabPermission can cause bugs, see
          // https://bugzilla.mozilla.org/show_bug.cgi?id=1694699#c21
          if (!restricted.has(prop) || tab.hasTabPermission) {
            nonempty = true;
            result[prop] = changeInfo[prop];
          }
        }
        return nonempty && result;
      }

      function getWindowID(windowId) {
        if (windowId === Window.WINDOW_ID_CURRENT) {
          let window = windowTracker.getTopWindow(context);
          if (!window) {
            return undefined;
          }
          return windowTracker.getId(window);
        }
        return windowId;
      }

      function matchFilters(tab) {
        if (!filterProps) {
          return true;
        }
        if (filter.tabId != null && tab.id != filter.tabId) {
          return false;
        }
        if (
          filter.windowId != null &&
          tab.windowId != getWindowID(filter.windowId)
        ) {
          return false;
        }
        if (filter.urls) {
          return filter.urls.matches(tab._uri) && tab.hasTabPermission;
        }
        return true;
      }

      let fireForTab = (tab, changed, nativeTab) => {
        // Tab may be null if private and not_allowed.
        if (!tab || !matchFilters(tab, changed)) {
          return;
        }

        let changeInfo = sanitize(tab, changed);
        if (changeInfo) {
          tabTracker.maybeWaitForTabOpen(nativeTab).then(() => {
            if (!nativeTab.parentNode) {
              // If the tab is already be destroyed, do nothing.
              return;
            }
            fire.async(tab.id, changeInfo, tab.convert());
          });
        }
      };

      let listener = event => {
        // Ignore any events prior to TabOpen
        // and events that are triggered while tabs are swapped between windows.
        if (
          event.originalTarget.initializingTab ||
          event.originalTarget.ownerGlobal.gBrowserInit?.isAdoptingTab()
        ) {
          return;
        }
        if (!extension.canAccessWindow(event.originalTarget.ownerGlobal)) {
          return;
        }
        let needed = [];
        if (event.type == "TabAttrModified") {
          let changed = event.detail.changed;
          if (
            changed.includes("image") &&
            filter.properties.has("favIconUrl")
          ) {
            needed.push("favIconUrl");
          }
          if (changed.includes("muted") && filter.properties.has("mutedInfo")) {
            needed.push("mutedInfo");
          }
          if (
            changed.includes("soundplaying") &&
            filter.properties.has("audible")
          ) {
            needed.push("audible");
          }
          if (
            changed.includes("undiscardable") &&
            filter.properties.has("autoDiscardable")
          ) {
            needed.push("autoDiscardable");
          }
          if (changed.includes("label") && filter.properties.has("title")) {
            needed.push("title");
          }
          if (
            changed.includes("sharing") &&
            filter.properties.has("sharingState")
          ) {
            needed.push("sharingState");
          }
          if (
            changed.includes("attention") &&
            filter.properties.has("attention")
          ) {
            needed.push("attention");
          }
        } else if (event.type == "TabPinned") {
          needed.push("pinned");
        } else if (event.type == "TabUnpinned") {
          needed.push("pinned");
        } else if (event.type == "TabBrowserInserted") {
          // This may be an adopted tab. Bail early to avoid asking tabManager
          // about the tab before we run the adoption logic in ext-browser.js.
          if (event.detail.insertedOnTabCreation) {
            return;
          }
          needed.push("discarded");
        } else if (event.type == "TabBrowserDiscarded") {
          needed.push("discarded");
        } else if (event.type === "TabGrouped") {
          needed.push("groupId");
        } else if (event.type === "TabUngrouped") {
          if (event.originalTarget.group) {
            // If there is still a group, that means that the group changed,
            // so TabGrouped will also fire. Ignore to avoid duplicate events.
            return;
          }
          needed.push("groupId");
        } else if (event.type == "TabShow") {
          needed.push("hidden");
        } else if (event.type == "TabHide") {
          needed.push("hidden");
        }

        let tab = tabManager.getWrapper(event.originalTarget);

        let changeInfo = {};
        for (let prop of needed) {
          changeInfo[prop] = tab[prop];
        }

        fireForTab(tab, changeInfo, event.originalTarget);
      };

      let statusListener = ({ browser, status, url }) => {
        let { gBrowser } = browser.ownerGlobal;
        let tabElem = gBrowser.getTabForBrowser(browser);
        if (tabElem) {
          if (!extension.canAccessWindow(tabElem.ownerGlobal)) {
            return;
          }

          let changed = {};
          if (filter.properties.has("status")) {
            changed.status = status;
          }
          if (url && filter.properties.has("url")) {
            changed.url = url;
          }

          fireForTab(tabManager.wrapTab(tabElem), changed, tabElem);
        }
      };

      let isArticleChangeListener = (messageName, message) => {
        let { gBrowser } = message.target.ownerGlobal;
        let nativeTab = gBrowser.getTabForBrowser(message.target);

        if (nativeTab && extension.canAccessWindow(nativeTab.ownerGlobal)) {
          let tab = tabManager.getWrapper(nativeTab);
          fireForTab(tab, { isArticle: message.data.isArticle }, nativeTab);
        }
      };

      let openerTabIdChangeListener = (_, { nativeTab, openerTabId }) => {
        let tab = tabManager.getWrapper(nativeTab);
        fireForTab(tab, { openerTabId }, nativeTab);
      };

      let listeners = new Map();
      if (filter.properties.has("status") || filter.properties.has("url")) {
        listeners.set("status", statusListener);
      }
      if (needsModified) {
        listeners.set("TabAttrModified", listener);
      }
      if (filter.properties.has("pinned")) {
        listeners.set("TabPinned", listener);
        listeners.set("TabUnpinned", listener);
      }
      if (filter.properties.has("discarded")) {
        listeners.set("TabBrowserInserted", listener);
        listeners.set("TabBrowserDiscarded", listener);
      }
      if (filter.properties.has("groupId")) {
        listeners.set("TabGrouped", listener);
        listeners.set("TabUngrouped", listener);
      }
      if (filter.properties.has("hidden")) {
        listeners.set("TabShow", listener);
        listeners.set("TabHide", listener);
      }

      for (let [name, listener] of listeners) {
        windowTracker.addListener(name, listener);
      }

      if (filter.properties.has("isArticle")) {
        tabTracker.on("tab-isarticle", isArticleChangeListener);
      }

      if (filter.properties.has("openerTabId")) {
        tabTracker.on("tab-openerTabId", openerTabIdChangeListener);
      }

      return {
        unregister() {
          for (let [name, listener] of listeners) {
            windowTracker.removeListener(name, listener);
          }

          if (filter.properties.has("isArticle")) {
            tabTracker.off("tab-isarticle", isArticleChangeListener);
          }

          if (filter.properties.has("openerTabId")) {
            tabTracker.off("tab-openerTabId", openerTabIdChangeListener);
          }
        },
        convert(_fire, _context) {
          fire = _fire;
          context = _context;
        },
      };
    },
  };

  getAPI(context) {
    let { extension } = context;
    let { tabManager, windowManager } = extension;
    let extensionApi = this;
    let module = "tabs";

    function getTabOrActive(tabId) {
      let tab =
        tabId !== null ? tabTracker.getTab(tabId) : tabTracker.activeTab;
      if (!tabManager.canAccessTab(tab)) {
        throw new ExtensionError(
          tabId === null
            ? "Cannot access activeTab"
            : `Invalid tab ID: ${tabId}`
        );
      }
      return tab;
    }

    function getNativeTabsFromIDArray(tabIds) {
      if (!Array.isArray(tabIds)) {
        tabIds = [tabIds];
      }
      return tabIds.map(tabId => {
        let tab = tabTracker.getTab(tabId);
        if (!tabManager.canAccessTab(tab)) {
          throw new ExtensionError(`Invalid tab ID: ${tabId}`);
        }
        return tab;
      });
    }

    async function promiseTabWhenReady(tabId) {
      let tab;
      if (tabId !== null) {
        tab = tabManager.get(tabId);
      } else {
        tab = tabManager.getWrapper(tabTracker.activeTab);
      }
      if (!tab) {
        throw new ExtensionError(
          tabId == null ? "Cannot access activeTab" : `Invalid tab ID: ${tabId}`
        );
      }

      await tabListener.awaitTabReady(tab.nativeTab);

      return tab;
    }

    function setContentTriggeringPrincipal(url, browser, options) {
      // For urls that we want to allow an extension to open in a tab, but
      // that it may not otherwise have access to, we set the triggering
      // principal to the url that is being opened.  This is used for newtab,
      // about: and moz-extension: protocols.
      options.triggeringPrincipal =
        Services.scriptSecurityManager.createContentPrincipal(
          Services.io.newURI(url),
          {
            userContextId: options.userContextId,
            privateBrowsingId: PrivateBrowsingUtils.isBrowserPrivate(browser)
              ? 1
              : 0,
          }
        );
    }

    let tabsApi = {
      tabs: {
        onActivated: new EventManager({
          context,
          module,
          event: "onActivated",
          extensionApi,
        }).api(),

        onCreated: new EventManager({
          context,
          module,
          event: "onCreated",
          extensionApi,
        }).api(),

        onHighlighted: new EventManager({
          context,
          module,
          event: "onHighlighted",
          extensionApi,
        }).api(),

        onAttached: new EventManager({
          context,
          module,
          event: "onAttached",
          extensionApi,
        }).api(),

        onDetached: new EventManager({
          context,
          module,
          event: "onDetached",
          extensionApi,
        }).api(),

        onRemoved: new EventManager({
          context,
          module,
          event: "onRemoved",
          extensionApi,
        }).api(),

        onReplaced: new EventManager({
          context,
          name: "tabs.onReplaced",
          register: () => {
            return () => {};
          },
        }).api(),

        onMoved: new EventManager({
          context,
          module,
          event: "onMoved",
          extensionApi,
        }).api(),

        onUpdated: new EventManager({
          context,
          module,
          event: "onUpdated",
          extensionApi,
        }).api(),

        create(createProperties) {
          return new Promise(resolve => {
            let window =
              createProperties.windowId !== null
                ? windowTracker.getWindow(createProperties.windowId, context)
                : windowTracker.getTopNormalWindow(context);
            if (!window || !context.canAccessWindow(window)) {
              throw new Error(
                "Not allowed to create tabs on the target window"
              );
            }
            let { gBrowserInit } = window;
            if (!gBrowserInit || !gBrowserInit.delayedStartupFinished) {
              let obs = finishedWindow => {
                if (finishedWindow != window) {
                  return;
                }
                Services.obs.removeObserver(
                  obs,
                  "browser-delayed-startup-finished"
                );
                resolve(window);
              };
              Services.obs.addObserver(obs, "browser-delayed-startup-finished");
            } else {
              resolve(window);
            }
          }).then(window => {
            let url;

            let options = { triggeringPrincipal: context.principal };
            if (createProperties.cookieStoreId) {
              // May throw if validation fails.
              options.userContextId = getUserContextIdForCookieStoreId(
                extension,
                createProperties.cookieStoreId,
                PrivateBrowsingUtils.isBrowserPrivate(window.gBrowser)
              );
            }

            if (createProperties.url !== null) {
              url = context.uri.resolve(createProperties.url);

              if (
                !url.startsWith("moz-extension://") &&
                !context.checkLoadURL(url, { dontReportErrors: true })
              ) {
                return Promise.reject({ message: `Illegal URL: ${url}` });
              }

              if (createProperties.openInReaderMode) {
                url = `about:reader?url=${encodeURIComponent(url)}`;
              }
            } else {
              url = window.BROWSER_NEW_TAB_URL;
            }
            let discardable = url && !url.startsWith("about:");
            // Handle moz-ext separately from the discardable flag to retain prior behavior.
            if (!discardable || url.startsWith("moz-extension://")) {
              setContentTriggeringPrincipal(url, window.gBrowser, options);
            }

            tabListener.initTabReady();
            const currentTab = window.gBrowser.selectedTab;
            const { frameLoader } = currentTab.linkedBrowser;
            const currentTabSize = {
              width: frameLoader.lazyWidth,
              height: frameLoader.lazyHeight,
            };

            if (createProperties.openerTabId !== null) {
              options.ownerTab = tabTracker.getTab(
                createProperties.openerTabId
              );
              options.openerBrowser = options.ownerTab.linkedBrowser;
              if (options.ownerTab.ownerGlobal !== window) {
                return Promise.reject({
                  message:
                    "Opener tab must be in the same window as the tab being created",
                });
              }
            }

            // Simple properties
            const properties = ["index", "pinned"];
            for (let prop of properties) {
              if (createProperties[prop] != null) {
                options[prop] = createProperties[prop];
              }
            }

            let active =
              createProperties.active !== null
                ? createProperties.active
                : !createProperties.discarded;
            if (createProperties.discarded) {
              if (active) {
                return Promise.reject({
                  message: `Active tabs cannot be created and discarded.`,
                });
              }
              if (createProperties.pinned) {
                return Promise.reject({
                  message: `Pinned tabs cannot be created and discarded.`,
                });
              }
              if (!discardable) {
                return Promise.reject({
                  message: `Cannot create a discarded new tab or "about" urls.`,
                });
              }
              options.createLazyBrowser = true;
              options.lazyTabTitle = createProperties.title;
            } else if (createProperties.title) {
              return Promise.reject({
                message: `Title may only be set for discarded tabs.`,
              });
            }

            let nativeTab = window.gBrowser.addTab(url, options);

            if (active) {
              window.gBrowser.selectedTab = nativeTab;
              if (!createProperties.url) {
                window.gURLBar.select();
              }
            }

            if (
              createProperties.url &&
              createProperties.url !== window.BROWSER_NEW_TAB_URL
            ) {
              // We can't wait for a location change event for about:newtab,
              // since it may be pre-rendered, in which case its initial
              // location change event has already fired.

              // Mark the tab as initializing, so that operations like
              // `executeScript` wait until the requested URL is loaded in
              // the tab before dispatching messages to the inner window
              // that contains the URL we're attempting to load.
              tabListener.initializingTabs.add(nativeTab);
            }

            if (createProperties.muted) {
              nativeTab.toggleMuteAudio(extension.id);
            }

            return tabManager.convert(nativeTab, currentTabSize);
          });
        },

        async remove(tabIds) {
          let nativeTabs = getNativeTabsFromIDArray(tabIds);

          if (nativeTabs.length === 1) {
            nativeTabs[0].ownerGlobal.gBrowser.removeTab(nativeTabs[0]);
            return;
          }

          // Or for multiple tabs, first group them by window
          let windowTabMap = new DefaultMap(() => []);
          for (let nativeTab of nativeTabs) {
            windowTabMap.get(nativeTab.ownerGlobal).push(nativeTab);
          }

          // Then make one call to removeTabs() for each window, to keep the
          // count accurate for SessionStore.getLastClosedTabCount().
          // Note: always pass options to disable animation and the warning
          // dialogue box, so that way all tabs are actually closed when the
          // browser.tabs.remove() promise resolves
          for (let [eachWindow, tabsToClose] of windowTabMap.entries()) {
            eachWindow.gBrowser.removeTabs(tabsToClose, {
              animate: false,
              suppressWarnAboutClosingWindow: true,
            });
          }
        },

        async discard(tabIds) {
          for (let nativeTab of getNativeTabsFromIDArray(tabIds)) {
            nativeTab.ownerGlobal.gBrowser.discardBrowser(nativeTab);
          }
        },

        async update(tabId, updateProperties) {
          let nativeTab = getTabOrActive(tabId);

          let tabbrowser = nativeTab.ownerGlobal.gBrowser;

          if (updateProperties.url !== null) {
            let url = context.uri.resolve(updateProperties.url);

            let options = {
              flags: updateProperties.loadReplace
                ? Ci.nsIWebNavigation.LOAD_FLAGS_REPLACE_HISTORY
                : Ci.nsIWebNavigation.LOAD_FLAGS_NONE,
              triggeringPrincipal: context.principal,
            };

            if (!context.checkLoadURL(url, { dontReportErrors: true })) {
              // We allow loading top level tabs for "other" extensions.
              if (url.startsWith("moz-extension://")) {
                setContentTriggeringPrincipal(url, tabbrowser, options);
              } else {
                return Promise.reject({ message: `Illegal URL: ${url}` });
              }
            }

            let browser = nativeTab.linkedBrowser;
            if (nativeTab.linkedPanel) {
              browser.fixupAndLoadURIString(url, options);
            } else {
              // Shift to fully loaded browser and make
              // sure load handler is instantiated.
              nativeTab.addEventListener(
                "SSTabRestoring",
                () => browser.fixupAndLoadURIString(url, options),
                { once: true }
              );
              tabbrowser._insertBrowser(nativeTab);
            }
          }

          if (updateProperties.active) {
            tabbrowser.selectedTab = nativeTab;
          }
          if (updateProperties.autoDiscardable !== null) {
            nativeTab.undiscardable = !updateProperties.autoDiscardable;
          }
          if (updateProperties.highlighted !== null) {
            if (updateProperties.highlighted) {
              if (!nativeTab.selected && !nativeTab.multiselected) {
                tabbrowser.addToMultiSelectedTabs(nativeTab);
                // Select the highlighted tab unless active:false is provided.
                // Note that Chrome selects it even in that case.
                if (updateProperties.active !== false) {
                  tabbrowser.lockClearMultiSelectionOnce();
                  tabbrowser.selectedTab = nativeTab;
                }
              }
            } else {
              tabbrowser.removeFromMultiSelectedTabs(nativeTab);
            }
          }
          if (updateProperties.muted !== null) {
            if (nativeTab.muted != updateProperties.muted) {
              nativeTab.toggleMuteAudio(extension.id);
            }
          }
          if (updateProperties.pinned !== null) {
            if (updateProperties.pinned) {
              tabbrowser.pinTab(nativeTab);
            } else {
              tabbrowser.unpinTab(nativeTab);
            }
          }
          if (updateProperties.openerTabId !== null) {
            tabTracker.setOpener(nativeTab, updateProperties.openerTabId);
          }
          if (updateProperties.successorTabId !== null) {
            let successor = null;
            if (updateProperties.successorTabId !== TAB_ID_NONE) {
              successor = tabTracker.getTab(
                updateProperties.successorTabId,
                null
              );
              if (!successor) {
                throw new ExtensionError("Invalid successorTabId");
              }
              // This also ensures "privateness" matches.
              if (successor.ownerDocument !== nativeTab.ownerDocument) {
                throw new ExtensionError(
                  "Successor tab must be in the same window as the tab being updated"
                );
              }
            }
            tabbrowser.setSuccessor(nativeTab, successor);
          }

          return tabManager.convert(nativeTab);
        },

        async reload(tabId, reloadProperties) {
          let nativeTab = getTabOrActive(tabId);

          let flags = Ci.nsIWebNavigation.LOAD_FLAGS_NONE;
          if (reloadProperties && reloadProperties.bypassCache) {
            flags |= Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;
          }
          nativeTab.linkedBrowser.reloadWithFlags(flags);
        },

        async warmup(tabId) {
          let nativeTab = tabTracker.getTab(tabId);
          if (!tabManager.canAccessTab(nativeTab)) {
            throw new ExtensionError(`Invalid tab ID: ${tabId}`);
          }
          let tabbrowser = nativeTab.ownerGlobal.gBrowser;
          tabbrowser.warmupTab(nativeTab);
        },

        async get(tabId) {
          return tabManager.get(tabId).convert();
        },

        getCurrent() {
          let tabData;
          if (context.tabId) {
            tabData = tabManager.get(context.tabId).convert();
          }
          return Promise.resolve(tabData);
        },

        async query(queryInfo) {
          return Array.from(tabManager.query(queryInfo, context), tab =>
            tab.convert()
          );
        },

        async captureTab(tabId, options) {
          let nativeTab = getTabOrActive(tabId);
          await tabListener.awaitTabReady(nativeTab);

          let browser = nativeTab.linkedBrowser;
          let window = browser.ownerGlobal;
          let zoom = window.ZoomManager.getZoomForBrowser(browser);

          let tab = tabManager.wrapTab(nativeTab);
          return tab.capture(context, zoom, options);
        },

        async captureVisibleTab(windowId, options) {
          let window =
            windowId == null
              ? windowTracker.getTopWindow(context)
              : windowTracker.getWindow(windowId, context);

          let tab = tabManager.getWrapper(window.gBrowser.selectedTab);
          if (
            !extension.hasPermission("<all_urls>") &&
            !tab.hasActiveTabPermission
          ) {
            throw new ExtensionError("Missing activeTab permission");
          }
          await tabListener.awaitTabReady(tab.nativeTab);

          let zoom = window.ZoomManager.getZoomForBrowser(
            tab.nativeTab.linkedBrowser
          );
          return tab.capture(context, zoom, options);
        },

        async detectLanguage(tabId) {
          let tab = await promiseTabWhenReady(tabId);
          let results = await tab.queryContent("DetectLanguage", {});
          return results[0];
        },

        async executeScript(tabId, details) {
          let tab = await promiseTabWhenReady(tabId);
          return tab.executeScript(context, details);
        },

        async insertCSS(tabId, details) {
          let tab = await promiseTabWhenReady(tabId);
          return tab.insertCSS(context, details);
        },

        async removeCSS(tabId, details) {
          let tab = await promiseTabWhenReady(tabId);
          return tab.removeCSS(context, details);
        },

        async move(tabIds, moveProperties) {
          let tabsMoved = [];
          if (!Array.isArray(tabIds)) {
            tabIds = [tabIds];
          }

          let destinationWindow = null;
          if (moveProperties.windowId !== null) {
            destinationWindow = windowTracker.getWindow(
              moveProperties.windowId,
              context
            );
            // Fail on an invalid window.
            if (!destinationWindow) {
              return Promise.reject({
                message: `Invalid window ID: ${moveProperties.windowId}`,
              });
            }
          }

          /*
            Indexes are maintained on a per window basis so that a call to
              move([tabA, tabB], {index: 0})
                -> tabA to 0, tabB to 1 if tabA and tabB are in the same window
              move([tabA, tabB], {index: 0})
                -> tabA to 0, tabB to 0 if tabA and tabB are in different windows
          */
          let lastInsertionMap = new Map();

          for (let nativeTab of getNativeTabsFromIDArray(tabIds)) {
            // If the window is not specified, use the window from the tab.
            let window = destinationWindow || nativeTab.ownerGlobal;
            let isSameWindow = nativeTab.ownerGlobal == window;
            let gBrowser = window.gBrowser;

            // If we are not moving the tab to a different window, and the window
            // only has one tab, do nothing.
            if (isSameWindow && gBrowser.tabs.length === 1) {
              lastInsertionMap.set(window, 0);
              continue;
            }
            // If moving between windows, be sure privacy matches.  While gBrowser
            // prevents this, we want to silently ignore it.
            if (
              !isSameWindow &&
              PrivateBrowsingUtils.isBrowserPrivate(gBrowser) !=
                PrivateBrowsingUtils.isBrowserPrivate(
                  nativeTab.ownerGlobal.gBrowser
                )
            ) {
              continue;
            }

            let insertionPoint;
            let lastInsertion = lastInsertionMap.get(window);
            if (lastInsertion == null) {
              insertionPoint = moveProperties.index;
              let maxIndex = gBrowser.tabs.length - (isSameWindow ? 1 : 0);
              if (insertionPoint == -1) {
                // If the index is -1 it should go to the end of the tabs.
                insertionPoint = maxIndex;
              } else {
                insertionPoint = Math.min(insertionPoint, maxIndex);
              }
            } else if (isSameWindow && nativeTab._tPos <= lastInsertion) {
              // lastInsertion is the current index of the last inserted tab.
              // insertionPoint is the desired index of the current tab *after* moving it.
              // When the tab is moved, the last inserted tab will no longer be at index
              // lastInsertion, but (lastInsertion - 1). To position the tabs adjacent to
              // each other, the tab should therefore be at index (lastInsertion - 1 + 1).
              insertionPoint = lastInsertion;
            } else {
              // In this case the last inserted tab will stay at index lastInsertion,
              // so we should move the current tab to index (lastInsertion + 1).
              insertionPoint = lastInsertion + 1;
            }

            // We can only move pinned tabs to a point within, or just after,
            // the current set of pinned tabs. Unpinned tabs, likewise, can only
            // be moved to a position after the current set of pinned tabs.
            // Attempts to move a tab to an illegal position are ignored.
            let numPinned = gBrowser.pinnedTabCount;
            let ok = nativeTab.pinned
              ? insertionPoint <= numPinned
              : insertionPoint >= numPinned;
            if (!ok) {
              continue;
            }

            if (isSameWindow) {
              // If the window we are moving is the same, just move the tab.
              gBrowser.moveTabTo(nativeTab, { tabIndex: insertionPoint });
            } else {
              // If the window we are moving the tab in is different, then move the tab
              // to the new window.
              nativeTab = gBrowser.adoptTab(nativeTab, {
                tabIndex: insertionPoint,
              });
            }
            lastInsertionMap.set(window, nativeTab._tPos);
            tabsMoved.push(nativeTab);
          }

          return tabsMoved.map(nativeTab => tabManager.convert(nativeTab));
        },

        duplicate(tabId, duplicateProperties) {
          const { active, index } = duplicateProperties || {};
          const inBackground = active === undefined ? false : !active;

          // Schema requires tab id.
          let nativeTab = getTabOrActive(tabId);

          let gBrowser = nativeTab.ownerGlobal.gBrowser;
          let newTab = gBrowser.duplicateTab(nativeTab, true, {
            inBackground,
            index,
          });

          tabListener.blockTabUntilRestored(newTab);
          return new Promise(resolve => {
            // Use SSTabRestoring to ensure that the tab's URL is ready before
            // resolving the promise.
            newTab.addEventListener(
              "SSTabRestoring",
              () => resolve(tabManager.convert(newTab)),
              { once: true }
            );
          });
        },

        getZoom(tabId) {
          let nativeTab = getTabOrActive(tabId);

          let { ZoomManager } = nativeTab.ownerGlobal;
          let zoom = ZoomManager.getZoomForBrowser(nativeTab.linkedBrowser);

          return Promise.resolve(zoom);
        },

        setZoom(tabId, zoom) {
          let nativeTab = getTabOrActive(tabId);

          let { FullZoom, ZoomManager } = nativeTab.ownerGlobal;

          if (zoom === 0) {
            // A value of zero means use the default zoom factor.
            return FullZoom.reset(nativeTab.linkedBrowser);
          } else if (zoom >= ZoomManager.MIN && zoom <= ZoomManager.MAX) {
            FullZoom.setZoom(zoom, nativeTab.linkedBrowser);
          } else {
            return Promise.reject({
              message: `Zoom value ${zoom} out of range (must be between ${ZoomManager.MIN} and ${ZoomManager.MAX})`,
            });
          }

          return Promise.resolve();
        },

        async getZoomSettings(tabId) {
          let nativeTab = getTabOrActive(tabId);

          let { FullZoom, ZoomUI } = nativeTab.ownerGlobal;

          return {
            mode: "automatic",
            scope: FullZoom.siteSpecific ? "per-origin" : "per-tab",
            defaultZoomFactor: await ZoomUI.getGlobalValue(),
          };
        },

        async setZoomSettings(tabId, settings) {
          let nativeTab = getTabOrActive(tabId);

          let currentSettings = await this.getZoomSettings(
            tabTracker.getId(nativeTab)
          );

          if (
            !Object.keys(settings).every(
              key => settings[key] === currentSettings[key]
            )
          ) {
            throw new ExtensionError(
              `Unsupported zoom settings: ${JSON.stringify(settings)}`
            );
          }
        },

        onZoomChange: new EventManager({
          context,
          name: "tabs.onZoomChange",
          register: fire => {
            let getZoomLevel = browser => {
              let { ZoomManager } = browser.ownerGlobal;

              return ZoomManager.getZoomForBrowser(browser);
            };

            // Stores the last known zoom level for each tab's browser.
            // WeakMap[<browser> -> number]
            let zoomLevels = new WeakMap();

            // Store the zoom level for all existing tabs.
            for (let window of windowTracker.browserWindows()) {
              if (!context.canAccessWindow(window)) {
                continue;
              }
              for (let nativeTab of window.gBrowser.tabs) {
                let browser = nativeTab.linkedBrowser;
                zoomLevels.set(browser, getZoomLevel(browser));
              }
            }

            let tabCreated = (eventName, event) => {
              let browser = event.nativeTab.linkedBrowser;
              if (!event.isPrivate || context.privateBrowsingAllowed) {
                zoomLevels.set(browser, getZoomLevel(browser));
              }
            };

            let zoomListener = async event => {
              let browser = event.originalTarget;

              // For non-remote browsers, this event is dispatched on the document
              // rather than on the <browser>.  But either way we have a node here.
              if (browser.nodeType == browser.DOCUMENT_NODE) {
                browser = browser.docShell.chromeEventHandler;
              }

              if (!context.canAccessWindow(browser.ownerGlobal)) {
                return;
              }

              let { gBrowser } = browser.ownerGlobal;
              let nativeTab = gBrowser.getTabForBrowser(browser);
              if (!nativeTab) {
                // We only care about zoom events in the top-level browser of a tab.
                return;
              }

              let oldZoomFactor = zoomLevels.get(browser);
              let newZoomFactor = getZoomLevel(browser);

              if (oldZoomFactor != newZoomFactor) {
                zoomLevels.set(browser, newZoomFactor);

                let tabId = tabTracker.getId(nativeTab);
                fire.async({
                  tabId,
                  oldZoomFactor,
                  newZoomFactor,
                  zoomSettings: await tabsApi.tabs.getZoomSettings(tabId),
                });
              }
            };

            tabTracker.on("tab-attached", tabCreated);
            tabTracker.on("tab-created", tabCreated);

            windowTracker.addListener("FullZoomChange", zoomListener);
            windowTracker.addListener("TextZoomChange", zoomListener);
            return () => {
              tabTracker.off("tab-attached", tabCreated);
              tabTracker.off("tab-created", tabCreated);

              windowTracker.removeListener("FullZoomChange", zoomListener);
              windowTracker.removeListener("TextZoomChange", zoomListener);
            };
          },
        }).api(),

        print() {
          let activeTab = getTabOrActive(null);
          let { PrintUtils } = activeTab.ownerGlobal;
          PrintUtils.startPrintWindow(activeTab.linkedBrowser.browsingContext);
        },

        // Legacy API
        printPreview() {
          return Promise.resolve(this.print());
        },

        saveAsPDF(pageSettings) {
          let activeTab = getTabOrActive(null);
          let picker = Cc["@mozilla.org/filepicker;1"].createInstance(
            Ci.nsIFilePicker
          );
          let title = strBundle.GetStringFromName(
            "saveaspdf.saveasdialog.title"
          );
          let filename;
          if (
            pageSettings.toFileName !== null &&
            pageSettings.toFileName != ""
          ) {
            filename = pageSettings.toFileName;
          } else if (activeTab.linkedBrowser.contentTitle != "") {
            filename = activeTab.linkedBrowser.contentTitle;
          } else {
            let url = new URL(activeTab.linkedBrowser.currentURI.spec);
            let path = decodeURIComponent(url.pathname);
            path = path.replace(/\/$/, "");
            filename = path.split("/").pop();
            if (filename == "") {
              filename = url.hostname;
            }
          }
          filename = DownloadPaths.sanitize(filename);

          picker.init(
            activeTab.ownerGlobal.browsingContext,
            title,
            Ci.nsIFilePicker.modeSave
          );
          picker.appendFilter("PDF", "*.pdf");
          picker.defaultExtension = "pdf";
          picker.defaultString = filename;

          return new Promise(resolve => {
            picker.open(function (retval) {
              if (retval == 0 || retval == 2) {
                // OK clicked (retval == 0) or replace confirmed (retval == 2)

                // Workaround: When trying to replace an existing file that is open in another application (i.e. a locked file),
                // the print progress listener is never called. This workaround ensures that a correct status is always returned.
                try {
                  let fstream = Cc[
                    "@mozilla.org/network/file-output-stream;1"
                  ].createInstance(Ci.nsIFileOutputStream);
                  fstream.init(picker.file, 0x2a, 0o666, 0); // ioflags = write|create|truncate, file permissions = rw-rw-rw-
                  fstream.close();
                } catch (e) {
                  resolve(retval == 0 ? "not_saved" : "not_replaced");
                  return;
                }

                let psService = Cc[
                  "@mozilla.org/gfx/printsettings-service;1"
                ].getService(Ci.nsIPrintSettingsService);
                let printSettings = psService.createNewPrintSettings();

                printSettings.printerName = "";
                printSettings.isInitializedFromPrinter = true;
                printSettings.isInitializedFromPrefs = true;

                printSettings.outputDestination =
                  Ci.nsIPrintSettings.kOutputDestinationFile;
                printSettings.toFileName = picker.file.path;

                printSettings.printSilent = true;

                printSettings.outputFormat =
                  Ci.nsIPrintSettings.kOutputFormatPDF;

                if (pageSettings.paperSizeUnit !== null) {
                  printSettings.paperSizeUnit = pageSettings.paperSizeUnit;
                }
                if (pageSettings.paperWidth !== null) {
                  printSettings.paperWidth = pageSettings.paperWidth;
                }
                if (pageSettings.paperHeight !== null) {
                  printSettings.paperHeight = pageSettings.paperHeight;
                }
                if (pageSettings.orientation !== null) {
                  printSettings.orientation = pageSettings.orientation;
                }
                if (pageSettings.scaling !== null) {
                  printSettings.scaling = pageSettings.scaling;
                }
                if (pageSettings.shrinkToFit !== null) {
                  printSettings.shrinkToFit = pageSettings.shrinkToFit;
                }
                if (pageSettings.showBackgroundColors !== null) {
                  printSettings.printBGColors =
                    pageSettings.showBackgroundColors;
                }
                if (pageSettings.showBackgroundImages !== null) {
                  printSettings.printBGImages =
                    pageSettings.showBackgroundImages;
                }
                if (pageSettings.edgeLeft !== null) {
                  printSettings.edgeLeft = pageSettings.edgeLeft;
                }
                if (pageSettings.edgeRight !== null) {
                  printSettings.edgeRight = pageSettings.edgeRight;
                }
                if (pageSettings.edgeTop !== null) {
                  printSettings.edgeTop = pageSettings.edgeTop;
                }
                if (pageSettings.edgeBottom !== null) {
                  printSettings.edgeBottom = pageSettings.edgeBottom;
                }
                if (pageSettings.marginLeft !== null) {
                  printSettings.marginLeft = pageSettings.marginLeft;
                }
                if (pageSettings.marginRight !== null) {
                  printSettings.marginRight = pageSettings.marginRight;
                }
                if (pageSettings.marginTop !== null) {
                  printSettings.marginTop = pageSettings.marginTop;
                }
                if (pageSettings.marginBottom !== null) {
                  printSettings.marginBottom = pageSettings.marginBottom;
                }
                if (pageSettings.headerLeft !== null) {
                  printSettings.headerStrLeft = pageSettings.headerLeft;
                }
                if (pageSettings.headerCenter !== null) {
                  printSettings.headerStrCenter = pageSettings.headerCenter;
                }
                if (pageSettings.headerRight !== null) {
                  printSettings.headerStrRight = pageSettings.headerRight;
                }
                if (pageSettings.footerLeft !== null) {
                  printSettings.footerStrLeft = pageSettings.footerLeft;
                }
                if (pageSettings.footerCenter !== null) {
                  printSettings.footerStrCenter = pageSettings.footerCenter;
                }
                if (pageSettings.footerRight !== null) {
                  printSettings.footerStrRight = pageSettings.footerRight;
                }

                activeTab.linkedBrowser.browsingContext
                  .print(printSettings)
                  .then(() => resolve(retval == 0 ? "saved" : "replaced"))
                  .catch(() =>
                    resolve(retval == 0 ? "not_saved" : "not_replaced")
                  );
              } else {
                // Cancel clicked (retval == 1)
                resolve("canceled");
              }
            });
          });
        },

        async toggleReaderMode(tabId) {
          let tab = await promiseTabWhenReady(tabId);
          if (!tab.isInReaderMode && !tab.isArticle) {
            throw new ExtensionError(
              "The specified tab cannot be placed into reader mode."
            );
          }
          let nativeTab = getTabOrActive(tabId);

          nativeTab.linkedBrowser.sendMessageToActor(
            "Reader:ToggleReaderMode",
            {},
            "AboutReader"
          );
        },

        moveInSuccession(tabIds, tabId, options) {
          const { insert, append } = options || {};
          const tabIdSet = new Set(tabIds);
          if (tabIdSet.size !== tabIds.length) {
            throw new ExtensionError(
              "IDs must not occur more than once in tabIds"
            );
          }
          if ((append || insert) && tabIdSet.has(tabId)) {
            throw new ExtensionError(
              "Value of tabId must not occur in tabIds if append or insert is true"
            );
          }

          const referenceTab = tabTracker.getTab(tabId, null);
          let referenceWindow = referenceTab && referenceTab.ownerGlobal;
          if (referenceWindow && !context.canAccessWindow(referenceWindow)) {
            throw new ExtensionError(`Invalid tab ID: ${tabId}`);
          }
          let previousTab, lastSuccessor;
          if (append) {
            previousTab = referenceTab;
            lastSuccessor =
              (insert && referenceTab && referenceTab.successor) || null;
          } else {
            lastSuccessor = referenceTab;
          }

          let firstTab;
          for (const tabId of tabIds) {
            const tab = tabTracker.getTab(tabId, null);
            if (tab === null) {
              continue;
            }
            if (!tabManager.canAccessTab(tab)) {
              throw new ExtensionError(`Invalid tab ID: ${tabId}`);
            }
            if (referenceWindow === null) {
              referenceWindow = tab.ownerGlobal;
            } else if (tab.ownerGlobal !== referenceWindow) {
              continue;
            }
            referenceWindow.gBrowser.replaceInSuccession(tab, tab.successor);
            if (append && tab === lastSuccessor) {
              lastSuccessor = tab.successor;
            }
            if (previousTab) {
              referenceWindow.gBrowser.setSuccessor(previousTab, tab);
            } else {
              firstTab = tab;
            }
            previousTab = tab;
          }

          if (previousTab) {
            if (!append && insert && lastSuccessor !== null) {
              referenceWindow.gBrowser.replaceInSuccession(
                lastSuccessor,
                firstTab
              );
            }
            referenceWindow.gBrowser.setSuccessor(previousTab, lastSuccessor);
          }
        },

        show(tabIds) {
          for (let tab of getNativeTabsFromIDArray(tabIds)) {
            if (tab.ownerGlobal) {
              tab.ownerGlobal.gBrowser.showTab(tab);
            }
          }
        },

        hide(tabIds) {
          let hidden = [];
          for (let tab of getNativeTabsFromIDArray(tabIds)) {
            if (tab.ownerGlobal && !tab.hidden) {
              tab.ownerGlobal.gBrowser.hideTab(tab, extension.id);
              if (tab.hidden) {
                hidden.push(tabTracker.getId(tab));
              }
            }
          }
          if (hidden.length) {
            let win = Services.wm.getMostRecentWindow("navigator:browser");

            // Before showing the hidden tabs warning,
            // move alltabs-button to somewhere visible if it isn't already.
            if (!CustomizableUI.widgetIsLikelyVisible("alltabs-button", win)) {
              CustomizableUI.addWidgetToArea(
                "alltabs-button",
                CustomizableUI.verticalTabsEnabled
                  ? CustomizableUI.AREA_NAVBAR
                  : CustomizableUI.AREA_TABSTRIP
              );
            }
            tabHidePopup.open(win, extension.id);
          }
          return hidden;
        },

        highlight(highlightInfo) {
          let { windowId, tabs, populate } = highlightInfo;
          if (windowId == null) {
            windowId = Window.WINDOW_ID_CURRENT;
          }
          let window = windowTracker.getWindow(windowId, context);
          if (!context.canAccessWindow(window)) {
            throw new ExtensionError(`Invalid window ID: ${windowId}`);
          }

          if (!Array.isArray(tabs)) {
            tabs = [tabs];
          } else if (!tabs.length) {
            throw new ExtensionError("No highlighted tab.");
          }
          window.gBrowser.selectedTabs = tabs.map(tabIndex => {
            let tab = window.gBrowser.tabs[tabIndex];
            if (!tab || !tabManager.canAccessTab(tab)) {
              throw new ExtensionError("No tab at index: " + tabIndex);
            }
            return tab;
          });
          return windowManager.convert(window, { populate });
        },

        goForward(tabId) {
          let nativeTab = getTabOrActive(tabId);
          nativeTab.linkedBrowser.goForward(false);
        },

        goBack(tabId) {
          let nativeTab = getTabOrActive(tabId);
          nativeTab.linkedBrowser.goBack(false);
        },

        group(options) {
          let nativeTabs = getNativeTabsFromIDArray(options.tabIds);
          let window = windowTracker.getWindow(
            options.createProperties?.windowId ?? Window.WINDOW_ID_CURRENT,
            context
          );
          const windowIsPrivate = PrivateBrowsingUtils.isWindowPrivate(window);
          for (const nativeTab of nativeTabs) {
            if (
              PrivateBrowsingUtils.isWindowPrivate(nativeTab.ownerGlobal) !==
              windowIsPrivate
            ) {
              if (windowIsPrivate) {
                throw new ExtensionError(
                  "Cannot move non-private tabs to private window"
                );
              }
              throw new ExtensionError(
                "Cannot move private tabs to non-private window"
              );
            }
          }
          function unpinTabsBeforeGrouping() {
            for (const nativeTab of nativeTabs) {
              nativeTab.ownerGlobal.gBrowser.unpinTab(nativeTab);
            }
          }
          let group;
          if (options.groupId == null) {
            // By default, tabs are appended after all other tabs in the
            // window. But if we are grouping tabs within a window, ideally the
            // tabs should just be grouped without moving positions.
            // TODO bug 1939214: when addTabGroup inserts tabs at the front as
            // needed (instead of always appending), simplify this logic.
            const tabInWin = nativeTabs.find(t => t.ownerGlobal === window);
            let insertBefore = tabInWin;
            if (tabInWin?.group) {
              if (tabInWin.group.tabs[0] === tabInWin) {
                // When tabInWin is at the front of a tab group, insert before
                // the tab group (instead of after it).
                insertBefore = tabInWin.group;
              } else {
                insertBefore = insertBefore.group.nextElementSibling;
              }
            }
            unpinTabsBeforeGrouping();
            group = window.gBrowser.addTabGroup(nativeTabs, { insertBefore });
            // Note: group is never null, because the only condition for which
            // it could be null is when all tabs are pinned, and we are already
            // explicitly unpinning them before moving.
          } else {
            group = window.gBrowser.getTabGroupById(
              getInternalTabGroupIdForExtTabGroupId(options.groupId)
            );
            if (!group) {
              throw new ExtensionError(`No group with id: ${options.groupId}`);
            }
            unpinTabsBeforeGrouping();
            // When moving tabs within the same window, try to maintain their
            // relative positions.
            const tabsBefore = [];
            const tabsAfter = [];
            const firstTabInGroup = group.tabs[0];
            for (const nativeTab of nativeTabs) {
              if (
                nativeTab.ownerGlobal === window &&
                nativeTab._tPos < firstTabInGroup._tPos
              ) {
                tabsBefore.push(nativeTab);
              } else {
                tabsAfter.push(nativeTab);
              }
            }
            if (tabsBefore.length) {
              window.gBrowser.moveTabsBefore(tabsBefore, firstTabInGroup);
            }
            if (tabsAfter.length) {
              group.addTabs(tabsAfter);
            }
          }
          return getExtTabGroupIdForInternalTabGroupId(group.id);
        },

        ungroup(tabIds) {
          const nativeTabs = getNativeTabsFromIDArray(tabIds);
          // Ungroup tabs while trying to preserve the relative order of tabs
          // within the tab strip as much as possible. This is not always
          // possible, e.g. when a tab group is only partially ungrouped.
          const ungroupOrder = new DefaultMap(() => []);
          for (const nativeTab of nativeTabs) {
            if (nativeTab.group) {
              ungroupOrder.get(nativeTab.group).push(nativeTab);
            }
          }
          for (const [group, tabs] of ungroupOrder) {
            // Preserve original order of ungrouped tabs.
            tabs.sort((a, b) => a._tPos - b._tPos);
            if (tabs[0] === tabs[0].group.tabs[0]) {
              // The tab is the front of the tab group, so insert before
              // current tab group to preserve order.
              tabs[0].ownerGlobal.gBrowser.moveTabsBefore(tabs, group);
            } else {
              tabs[0].ownerGlobal.gBrowser.moveTabsAfter(tabs, group);
            }
          }
        },
      },
    };
    return tabsApi;
  }
};
