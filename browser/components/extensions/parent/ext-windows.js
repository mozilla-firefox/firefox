/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  HomePage: "resource:///modules/HomePage.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

var { ExtensionError, promiseObserved } = ExtensionUtils;

// Note: params from extensions are in CSS pixels, not desktop pixels.
// This method clamps values as needed, still in CSS pixels.
function sanitizePositionParams(params, window = null, positionOffset = 0) {
  if (params.left === null && params.top === null) {
    return;
  }

  if (params.left === null) {
    const baseLeft = window ? window.screenX : 0;
    params.left = baseLeft + positionOffset;
  }
  if (params.top === null) {
    const baseTop = window ? window.screenY : 0;
    params.top = baseTop + positionOffset;
  }

  // boundary check: don't put window out of visible area
  const baseWidth = window ? window.outerWidth : 0;
  const baseHeight = window ? window.outerHeight : 0;
  // Secure minimum size of an window should be same to the one
  // defined at nsGlobalWindowOuter::CheckSecurityWidthAndHeight.
  const minWidth = 100;
  const minHeight = 100;
  const width = Math.max(
    minWidth,
    params.width !== null ? params.width : baseWidth
  );
  const height = Math.max(
    minHeight,
    params.height !== null ? params.height : baseHeight
  );
  const screenManager = Cc["@mozilla.org/gfx/screenmanager;1"].getService(
    Ci.nsIScreenManager
  );
  // screenForRect and GetAvailRectDisplayPix expect desktop pixels, so we need
  // to map to desktop pixels (and back to CSS pixels later).
  const cssToDesktopScale = window
    ? window.devicePixelRatio / window.desktopToDeviceScale
    : 1;
  const desktopLeft = params.left * cssToDesktopScale;
  const desktopTop = params.top * cssToDesktopScale;
  const desktopWidth = width * cssToDesktopScale;
  const desktopHeight = height * cssToDesktopScale;
  const screen = screenManager.screenForRect(
    desktopLeft,
    desktopTop,
    desktopWidth,
    desktopHeight
  );
  const availLeft = {};
  const availTop = {};
  const availWidth = {};
  const availHeight = {};
  screen.GetAvailRectDisplayPix(availLeft, availTop, availWidth, availHeight);
  const slopX = (window?.screenEdgeSlopX || 0) * cssToDesktopScale;
  const slopY = (window?.screenEdgeSlopY || 0) * cssToDesktopScale;
  const minLeft = availLeft.value - slopX;
  const minTop = availTop.value - slopY;
  const maxLeft = availLeft.value + availWidth.value - desktopWidth;
  const maxTop = availTop.value + availHeight.value - desktopHeight;
  const clampedLeft = Math.min(maxLeft, Math.max(minLeft, desktopLeft));
  const clampedTop = Math.min(maxTop, Math.max(minTop, desktopTop));
  params.left = Math.round(clampedLeft / cssToDesktopScale);
  params.top = Math.round(clampedTop / cssToDesktopScale);
}

this.windows = class extends ExtensionAPIPersistent {
  windowEventRegistrar(event, listener) {
    let { extension } = this;
    return ({ fire }) => {
      let listener2 = (window, ...args) => {
        if (extension.canAccessWindow(window)) {
          listener(fire, window, ...args);
        }
      };

      windowTracker.addListener(event, listener2);
      return {
        unregister() {
          windowTracker.removeListener(event, listener2);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    };
  }

  PERSISTENT_EVENTS = {
    onCreated: this.windowEventRegistrar("domwindowopened", (fire, window) => {
      fire.async(this.extension.windowManager.convert(window));
    }),
    onRemoved: this.windowEventRegistrar("domwindowclosed", (fire, window) => {
      fire.async(windowTracker.getId(window));
    }),
    onFocusChanged({ fire }) {
      let { extension } = this;
      // Keep track of the last windowId used to fire an onFocusChanged event
      let lastOnFocusChangedWindowId;

      let listener = () => {
        // Wait a tick to avoid firing a superfluous WINDOW_ID_NONE
        // event when switching focus between two Firefox windows.
        Promise.resolve().then(() => {
          let windowId = Window.WINDOW_ID_NONE;
          let window = Services.focus.activeWindow;
          if (window && extension.canAccessWindow(window)) {
            windowId = windowTracker.getId(window);
          }
          if (windowId !== lastOnFocusChangedWindowId) {
            fire.async(windowId);
            lastOnFocusChangedWindowId = windowId;
          }
        });
      };
      windowTracker.addListener("focus", listener);
      windowTracker.addListener("blur", listener);
      return {
        unregister() {
          windowTracker.removeListener("focus", listener);
          windowTracker.removeListener("blur", listener);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
  };

  getAPI(context) {
    let { extension } = context;

    const { windowManager } = extension;

    return {
      windows: {
        onCreated: new EventManager({
          context,
          module: "windows",
          event: "onCreated",
          extensionApi: this,
        }).api(),

        onRemoved: new EventManager({
          context,
          module: "windows",
          event: "onRemoved",
          extensionApi: this,
        }).api(),

        onFocusChanged: new EventManager({
          context,
          module: "windows",
          event: "onFocusChanged",
          extensionApi: this,
        }).api(),

        get: function (windowId, getInfo) {
          let window = windowTracker.getWindow(windowId, context);
          if (!window || !context.canAccessWindow(window)) {
            return Promise.reject({
              message: `Invalid window ID: ${windowId}`,
            });
          }
          return Promise.resolve(windowManager.convert(window, getInfo));
        },

        getCurrent: function (getInfo) {
          let window = context.currentWindow || windowTracker.topWindow;
          if (!context.canAccessWindow(window)) {
            return Promise.reject({ message: `Invalid window` });
          }
          return Promise.resolve(windowManager.convert(window, getInfo));
        },

        getLastFocused: function (getInfo) {
          let window = windowTracker.topWindow;
          if (!context.canAccessWindow(window)) {
            return Promise.reject({ message: `Invalid window` });
          }
          return Promise.resolve(windowManager.convert(window, getInfo));
        },

        getAll: function (getInfo) {
          let doNotCheckTypes =
            getInfo === null || getInfo.windowTypes === null;
          let windows = [];
          // incognito access is checked in getAll
          for (let win of windowManager.getAll()) {
            if (doNotCheckTypes || getInfo.windowTypes.includes(win.type)) {
              windows.push(win.convert(getInfo));
            }
          }
          return windows;
        },

        create: async function (createData) {
          let needResize =
            createData.left !== null ||
            createData.top !== null ||
            createData.width !== null ||
            createData.height !== null;
          if (createData.incognito && !context.privateBrowsingAllowed) {
            throw new ExtensionError(
              "Extension does not have permission for incognito mode"
            );
          }

          if (needResize) {
            if (createData.state !== null && createData.state != "normal") {
              throw new ExtensionError(
                `"state": "${createData.state}" may not be combined with "left", "top", "width", or "height"`
              );
            }
            createData.state = "normal";
          }

          function mkstr(s) {
            let result = Cc["@mozilla.org/supports-string;1"].createInstance(
              Ci.nsISupportsString
            );
            result.data = s;
            return result;
          }

          let args = Cc["@mozilla.org/array;1"].createInstance(
            Ci.nsIMutableArray
          );

          // Whether there is only one URL to load, and it is a moz-extension:-URL.
          let isOnlyMozExtensionUrl = false;

          // Creating a new window allows one single triggering principal for all tabs that
          // are created in the window.  Due to that, if we need a browser principal to load
          // some urls, we fallback to using a content principal like we do in the tabs api.
          // Throws if url is an array and any url can't be loaded by the extension principal.
          let principal = context.principal;
          function setContentTriggeringPrincipal(url) {
            principal = Services.scriptSecurityManager.createContentPrincipal(
              Services.io.newURI(url),
              {
                // Note: privateBrowsingAllowed was already checked before.
                privateBrowsingId: createData.incognito ? 1 : 0,
              }
            );
          }

          if (createData.tabId !== null) {
            if (createData.url !== null) {
              throw new ExtensionError(
                "`tabId` may not be used in conjunction with `url`"
              );
            }

            if (createData.allowScriptsToClose) {
              throw new ExtensionError(
                "`tabId` may not be used in conjunction with `allowScriptsToClose`"
              );
            }

            let tab = tabTracker.getTab(createData.tabId);
            if (!context.canAccessWindow(tab.documentGlobal)) {
              throw new ExtensionError(`Invalid tab ID: ${createData.tabId}`);
            }
            // Private browsing tabs can only be moved to private browsing
            // windows.
            let incognito = PrivateBrowsingUtils.isBrowserPrivate(
              tab.linkedBrowser
            );
            if (
              createData.incognito !== null &&
              createData.incognito != incognito
            ) {
              throw new ExtensionError(
                "`incognito` property must match the incognito state of tab"
              );
            }
            createData.incognito = incognito;

            if (
              createData.cookieStoreId &&
              createData.cookieStoreId !==
                getCookieStoreIdForTab(createData, tab)
            ) {
              throw new ExtensionError(
                "`cookieStoreId` must match the tab's cookieStoreId"
              );
            }

            // If the tab is part of a split view, move the whole split view
            // instead of just that single tab.
            const tabToAdopt = tab.splitview ?? tab;
            // For tab adoption logic, see getTabToAdopt() in browser-init.js.
            args.appendElement(tabToAdopt);
          } else if (createData.url !== null) {
            if (Array.isArray(createData.url)) {
              let array = Cc["@mozilla.org/array;1"].createInstance(
                Ci.nsIMutableArray
              );
              for (let url of createData.url.map(u => context.uri.resolve(u))) {
                // We can only provide a single triggering principal when
                // opening a window, so if the extension cannot normally
                // access a url, we fail.  This includes about and moz-ext
                // urls.
                if (!context.checkLoadURL(url, { dontReportErrors: true })) {
                  return Promise.reject({ message: `Illegal URL: ${url}` });
                }
                array.appendElement(mkstr(url));
              }
              args.appendElement(array);
              // TODO bug 1780583: support multiple triggeringPrincipals to
              // avoid having to use the system principal here.
              principal = Services.scriptSecurityManager.getSystemPrincipal();
            } else {
              let url = context.uri.resolve(createData.url);
              args.appendElement(mkstr(url));
              isOnlyMozExtensionUrl = ExtensionUtils.isExtensionUrl(url);
              if (!context.checkLoadURL(url, { dontReportErrors: true })) {
                if (isOnlyMozExtensionUrl) {
                  // For backwards-compatibility (also in tabs APIs), we allow
                  // extensions to open other moz-extension:-URLs even if that
                  // other resource is not listed in web_accessible_resources.
                  setContentTriggeringPrincipal(url);
                } else {
                  throw new ExtensionError(`Illegal URL: ${url}`);
                }
              }
            }
          } else {
            let url =
              createData.incognito &&
              !PrivateBrowsingUtils.permanentPrivateBrowsing
                ? "about:privatebrowsing"
                : HomePage.get().split("|", 1)[0];
            args.appendElement(mkstr(url));
            isOnlyMozExtensionUrl = ExtensionUtils.isExtensionUrl(url);

            if (!context.checkLoadURL(url, { dontReportErrors: true })) {
              // The extension principal cannot directly load about:-URLs,
              // except for about:blank, or other moz-extension:-URLs that are
              // not in web_accessible_resources. Ensure any page set as a home
              // page will load by using a content principal.
              setContentTriggeringPrincipal(url);
            }
          }

          // All types other than "normal" create "popup"-type windows.
          const isPopup =
            createData.type !== null && createData.type != "normal";

          let extraOptions = null;
          if (isPopup) {
            extraOptions = Cc[
              "@mozilla.org/hash-property-bag;1"
            ].createInstance(Ci.nsIWritablePropertyBag2);
            extraOptions.setPropertyAsBool("web-extension-popup-window", true);
          }
          args.appendElement(extraOptions); // extraOptions
          args.appendElement(null); // referrerInfo
          args.appendElement(null); // postData
          args.appendElement(null); // allowThirdPartyFixup

          if (createData.cookieStoreId) {
            let userContextIdSupports = Cc[
              "@mozilla.org/supports-PRUint32;1"
            ].createInstance(Ci.nsISupportsPRUint32);
            // May throw if validation fails.
            userContextIdSupports.data = getUserContextIdForCookieStoreId(
              extension,
              createData.cookieStoreId,
              createData.incognito
            );

            args.appendElement(userContextIdSupports); // userContextId
          } else {
            args.appendElement(null);
          }

          args.appendElement(context.principal); // originPrincipal - not important.
          args.appendElement(context.principal); // originStoragePrincipal - not important.
          args.appendElement(principal); // triggeringPrincipal
          args.appendElement(
            Cc["@mozilla.org/supports-PRBool;1"].createInstance(
              Ci.nsISupportsPRBool
            )
          ); // allowInheritPrincipal
          // There is no CSP associated with this extension, hence we explicitly pass null as the CSP argument.
          args.appendElement(null); // csp

          let features = ["chrome"];

          if (!isPopup) {
            features.push("dialog=no", "all");
          } else {
            features.push(
              "dialog",
              "resizable",
              "minimizable",
              "titlebar",
              "close"
            );
            if (createData.left === null && createData.top === null) {
              features.push("centerscreen");
            }
          }

          if (createData.incognito !== null) {
            if (createData.incognito) {
              if (!PrivateBrowsingUtils.enabled) {
                throw new ExtensionError(
                  "`incognito` cannot be used if incognito mode is disabled"
                );
              }
              features.push("private");
            } else {
              features.push("non-private");
            }
          }

          const baseWindow = windowTracker.getTopNormalWindow(context);
          // 10px offset is same to Chromium
          sanitizePositionParams(createData, baseWindow, 10);

          if (createData.width !== null) {
            features.push("outerWidth=" + createData.width);
          }
          if (createData.height !== null) {
            features.push("outerHeight=" + createData.height);
          }
          if (createData.left !== null) {
            features.push("left=" + createData.left);
          }
          if (createData.top !== null) {
            features.push("top=" + createData.top);
          }

          let window = Services.ww.openWindow(
            null,
            AppConstants.BROWSER_CHROME_URL,
            "_blank",
            features.join(","),
            args
          );

          let win = windowManager.getWrapper(window);

          // TODO: focused, type

          const contentLoaded = new Promise(resolve => {
            window.addEventListener(
              "DOMContentLoaded",
              function () {
                let { allowScriptsToClose } = createData;
                if (allowScriptsToClose === null && isOnlyMozExtensionUrl) {
                  allowScriptsToClose = true;
                }
                if (allowScriptsToClose) {
                  window.gBrowserAllowScriptsToCloseInitialTabs = true;
                }
                resolve();
              },
              { once: true }
            );
          });

          const startupFinished = promiseObserved(
            "browser-delayed-startup-finished",
            win => win == window
          );

          await contentLoaded;
          await startupFinished;

          if (
            [
              "minimized",
              "fullscreen",
              "docked",
              "normal",
              "maximized",
            ].includes(createData.state)
          ) {
            await win.setState(createData.state);
          }

          if (createData.titlePreface !== null) {
            win.setTitlePreface(createData.titlePreface);
          }
          return win.convert({ populate: true });
        },

        update: async function (windowId, updateInfo) {
          if (updateInfo.state !== null && updateInfo.state != "normal") {
            if (
              updateInfo.left !== null ||
              updateInfo.top !== null ||
              updateInfo.width !== null ||
              updateInfo.height !== null
            ) {
              throw new ExtensionError(
                `"state": "${updateInfo.state}" may not be combined with "left", "top", "width", or "height"`
              );
            }
          }

          let win = windowManager.get(windowId, context);
          if (!win) {
            throw new ExtensionError(`Invalid window ID: ${windowId}`);
          }
          if (updateInfo.focused) {
            win.window.focus();
          }

          if (updateInfo.state !== null) {
            await win.setState(updateInfo.state);
          }

          if (updateInfo.drawAttention) {
            // Bug 1257497 - Firefox can't cancel attention actions.
            win.window.getAttention();
          }

          sanitizePositionParams(updateInfo, win.window);
          win.updateGeometry(updateInfo);

          if (updateInfo.titlePreface !== null) {
            win.setTitlePreface(updateInfo.titlePreface);
            win.window.gBrowser.updateTitlebar();
          }

          // TODO: All the other properties, focused=false...

          return win.convert();
        },

        remove: function (windowId) {
          let window = windowTracker.getWindow(windowId, context);
          if (!context.canAccessWindow(window)) {
            return Promise.reject({
              message: `Invalid window ID: ${windowId}`,
            });
          }
          window.close();

          return new Promise(resolve => {
            let listener = () => {
              windowTracker.removeListener("domwindowclosed", listener);
              resolve();
            };
            windowTracker.addListener("domwindowclosed", listener);
          });
        },
      },
    };
  }
};
