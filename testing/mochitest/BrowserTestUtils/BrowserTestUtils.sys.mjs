/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module implements a number of utilities useful for browser tests.
 *
 * All asynchronous helper methods should return promises, rather than being
 * callback based.
 */

// This file uses ContentTask & frame scripts, where these are available.
/* global ContentTaskUtils */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { TestUtils } from "resource://testing-common/TestUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContentTask: "resource://testing-common/ContentTask.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  ProtocolProxyService: [
    "@mozilla.org/network/protocol-proxy-service;1",
    Ci.nsIProtocolProxyService,
  ],
});

let gListenerId = 0;

const DISABLE_CONTENT_PROCESS_REUSE_PREF = "dom.ipc.disableContentProcessReuse";

const kAboutPageRegistrationContentScript =
  "chrome://mochikit/content/tests/BrowserTestUtils/content-about-page-utils.js";

/**
 * Create and register the BrowserTestUtils and ContentEventListener window
 * actors.
 */
function registerActors() {
  ChromeUtils.registerWindowActor("BrowserTestUtils", {
    parent: {
      esModuleURI: "resource://testing-common/BrowserTestUtilsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://testing-common/BrowserTestUtilsChild.sys.mjs",
      events: {
        DOMContentLoaded: { capture: true },
        load: { capture: true },
      },
    },
    allFrames: true,
    includeChrome: true,
  });

  ChromeUtils.registerWindowActor("ContentEventListener", {
    parent: {
      esModuleURI:
        "resource://testing-common/ContentEventListenerParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "resource://testing-common/ContentEventListenerChild.sys.mjs",
      events: {
        // We need to see the creation of all new windows, in case they have
        // a browsing context we are interested in.
        DOMWindowCreated: { capture: true },
      },
    },
    allFrames: true,
  });
}

registerActors();

/**
 * BrowserTestUtils provides useful test utilities for working with the browser
 * in browser mochitests. Most common operations (opening, closing and switching
 * between tabs and windows, loading URLs, waiting for events in the parent or
 * content process, clicking things in the content process, registering about
 * pages, etc.) have dedicated helpers on this object.
 *
 * @class
 */
export var BrowserTestUtils = {
  // We define the function separately, rather than using an arrow function
  // inline due to https://github.com/jsdoc/jsdoc/issues/2143.
  /**
   * @template T
   * @typedef {Function} withNewTabTaskFn
   * @param {MozBrowser} browser
   * @returns {Promise<T> | T}
   */

  /**
   * Loads a page in a new tab, executes a Task and closes the tab.
   *
   * @template T
   * @param {object | string} options
   *        If this is a string it is the url to open and will be opened in the
   *        currently active browser window.
   * @param {tabbrowser} [options.gBrowser]
   *        A reference to the ``tabbrowser`` element where the new tab should
   *        be opened,
   * @param {string} options.url
   *        The URL of the page to load.
   * @param {withNewTabTaskFn<T>} taskFn
   *        Async function representing that will be executed while
   *        the tab is loaded. The first argument passed to the function is a
   *        reference to the browser object for the new tab.
   *
   * @return {Promise<T>} Resolves to the value that is returned from taskFn.
   * @rejects Any exception from taskFn is propagated.
   */
  async withNewTab(options, taskFn) {
    if (typeof options == "string") {
      options = {
        gBrowser: Services.wm.getMostRecentWindow("navigator:browser").gBrowser,
        url: options,
      };
    }
    let tab = await BrowserTestUtils.openNewForegroundTab(options);
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let originalWindow = tab.documentGlobal || tab.ownerGlobal;
    let result;
    try {
      result = await taskFn(tab.linkedBrowser);
    } finally {
      // @backward-compat { version 152 }
      // Get rid of the documentGlobal fallback once 152 makes it to release.
      let finalWindow = tab.documentGlobal || tab.ownerGlobal;
      if (originalWindow == finalWindow && !tab.closing && tab.linkedBrowser) {
        // taskFn may resolve within a tick after opening a new tab.
        // We shouldn't remove the newly opened tab in the same tick.
        // Wait for the next tick here.
        await TestUtils.waitForTick();
        BrowserTestUtils.removeTab(tab);
      } else {
        Services.console.logStringMessage(
          "BrowserTestUtils.withNewTab: Tab was already closed before " +
            "removeTab would have been called"
        );
      }
    }

    return Promise.resolve(result);
  },

  /**
   * Opens a new tab in the foreground.
   *
   * This function takes an options object (which is preferred) or actual
   * parameters. The names of the options must correspond to the names below.
   * gBrowser is required and all other options are optional.
   *
   * @param {tabbrowser} gBrowser
   *        The tabbrowser to open the tab new in.
   * @param {string|function} opening (or url)
   *        May be either a string URL to load in the tab, or a function that
   *        will be called to open a foreground tab. Defaults to "about:blank".
   * @param {boolean} waitForLoad
   *        True to wait for the page in the new tab to load. Defaults to true.
   * @param {boolean} waitForStateStop
   *        True to wait for the web progress listener to send STATE_STOP for the
   *        document in the tab. Defaults to false.
   * @param {boolean} forceNewProcess
   *        True to force the new tab to load in a new process. Defaults to
   *        false.
   *
   * @return {Promise}
   *         Resolves when the tab is ready and loaded as necessary.
   */
  openNewForegroundTab(tabbrowser, ...args) {
    let startTime = ChromeUtils.now();
    let options;
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = tabbrowser.documentGlobal || tabbrowser.ownerGlobal;
    if (win && tabbrowser === win.gBrowser) {
      // tabbrowser is a tabbrowser, read the rest of the arguments from args.
      let [
        opening = "about:blank",
        waitForLoad = true,
        waitForStateStop = false,
        forceNewProcess = false,
      ] = args;

      options = { opening, waitForLoad, waitForStateStop, forceNewProcess };
    } else {
      if ("url" in tabbrowser && !("opening" in tabbrowser)) {
        tabbrowser.opening = tabbrowser.url;
      }

      let {
        opening = "about:blank",
        waitForLoad = true,
        waitForStateStop = false,
        forceNewProcess = false,
      } = tabbrowser;

      tabbrowser = tabbrowser.gBrowser;
      options = { opening, waitForLoad, waitForStateStop, forceNewProcess };
      win = tabbrowser.documentGlobal || tabbrowser.ownerGlobal;
    }

    let {
      opening: opening,
      waitForLoad: aWaitForLoad,
      waitForStateStop: aWaitForStateStop,
    } = options;

    let promises, tab;
    try {
      // If we're asked to force a new process, set the pref to disable process
      // re-use while we insert this new tab.
      if (options.forceNewProcess) {
        Services.ppmm.releaseCachedProcesses();
        Services.prefs.setBoolPref(DISABLE_CONTENT_PROCESS_REUSE_PREF, true);
      }

      promises = [
        BrowserTestUtils.switchTab(tabbrowser, function () {
          if (typeof opening == "function") {
            opening();
            tab = tabbrowser.selectedTab;
          } else {
            tabbrowser.selectedTab = tab = BrowserTestUtils.addTab(
              tabbrowser,
              opening
            );
          }
        }).then(async result => {
          // The content process only gets the browser's size once we have
          // reflowed it, so until then callers that synthesize input would hit
          // test against a 0x0 viewport. Callers that opted out of waiting for
          // the load want the tab before it is ready, and tests don't interact
          // with a minimized or fully occluded window, whose refresh driver is
          // throttled anyway.
          if (aWaitForLoad && win.browsingContext.isActive) {
            await win.promiseDocumentFlushed(() => {});
            // Avoid ever resolving in the middle of a refresh driver tick,
            // which would otherwise happen whenever this is the last promise
            // to resolve.
            await TestUtils.waitForTick();
          }
          return result;
        }),
      ];

      if (aWaitForLoad) {
        // accept any load, including about:blank
        promises.push(
          BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
            wantLoad: () => true,
          })
        );
      }
      if (aWaitForStateStop) {
        promises.push(BrowserTestUtils.browserStopped(tab.linkedBrowser));
      }
    } finally {
      // Clear the pref once we're done, if needed.
      if (options.forceNewProcess) {
        Services.prefs.clearUserPref(DISABLE_CONTENT_PROCESS_REUSE_PREF);
      }
    }
    return Promise.all(promises).then(() => {
      let { innerWindowId } = win.windowGlobalChild;
      ChromeUtils.addProfilerMarker(
        "BrowserTestUtils",
        { startTime, category: "Test", innerWindowId },
        "openNewForegroundTab"
      );
      return tab;
    });
  },

  showOnlyTheseTabs(tabbrowser, tabs) {
    for (let tab of tabs) {
      tabbrowser.showTab(tab);
    }
    for (let tab of tabbrowser.tabs) {
      if (!tabs.includes(tab)) {
        tabbrowser.hideTab(tab);
      }
    }
  },

  /**
   * Checks if a DOM element is hidden.
   *
   * @param {Element} element
   *        The element which is to be checked.
   *
   * @return {boolean}
   */
  isHidden(element) {
    if (
      element.nodeType == Node.DOCUMENT_FRAGMENT_NODE &&
      element.containingShadowRoot == element
    ) {
      return BrowserTestUtils.isHidden(element.getRootNode().host);
    }

    let win = element.documentGlobal;
    let style = win.getComputedStyle(element);
    if (style.display == "none") {
      return true;
    }
    if (style.visibility != "visible") {
      return true;
    }
    if (win.XULPopupElement.isInstance(element)) {
      return ["hiding", "closed"].includes(element.state);
    }

    // Hiding a parent element will hide all its children
    if (element.parentNode != element.ownerDocument) {
      return BrowserTestUtils.isHidden(element.parentNode);
    }

    return false;
  },

  /**
   * Checks if a DOM element is visible.
   *
   * @param {Element} element
   *        The element which is to be checked.
   *
   * @return {boolean}
   */
  isVisible(element) {
    if (
      element.nodeType == Node.DOCUMENT_FRAGMENT_NODE &&
      element.containingShadowRoot == element
    ) {
      return BrowserTestUtils.isVisible(element.getRootNode().host);
    }

    let win = element.documentGlobal;
    let style = win.getComputedStyle(element);
    if (style.display == "none") {
      return false;
    }
    if (style.visibility != "visible") {
      return false;
    }
    if (win.XULPopupElement.isInstance(element) && element.state != "open") {
      return false;
    }

    // Hiding a parent element will hide all its children
    if (element.parentNode != element.ownerDocument) {
      return BrowserTestUtils.isVisible(element.parentNode);
    }

    return true;
  },

  /**
   * If the argument is a browsingContext, return it. If the
   * argument is a browser/frame, returns the browsing context for it.
   */
  getBrowsingContextFrom(browser) {
    if (Element.isInstance(browser)) {
      return browser.browsingContext;
    }

    return browser;
  },

  /**
   * Switches to a tab and resolves when it is ready.
   *
   * @param {tabbrowser} tabbrowser
   *        The tabbrowser.
   * @param {tab} tab
   *        Either a tab element to switch to or a function to perform the switch.
   *
   * @return {Promise}
   *         Resolves when the tab has been switched to.
   */
  switchTab(tabbrowser, tab) {
    let startTime = ChromeUtils.now();
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = tabbrowser.documentGlobal || tabbrowser.ownerGlobal;
    let { innerWindowId } = win.windowGlobalChild;

    // Some tests depend on the delay and TabSwitched only fires if the browser is visible.
    // Bug 1977993 tracks always dispatching TabSwitched.
    let switchEvent =
      Services.prefs.getBoolPref("test.wait300msAfterTabSwitch", false) ||
      tabbrowser.ownerDocument.hidden
        ? "TabSwitchDone"
        : "TabSwitched";

    let promise = new Promise(resolve => {
      tabbrowser.addEventListener(
        switchEvent,
        function () {
          TestUtils.executeSoon(() => {
            ChromeUtils.addProfilerMarker(
              "BrowserTestUtils",
              { category: "Test", startTime, innerWindowId },
              "switchTab"
            );
            resolve(tabbrowser.selectedTab);
          });
        },
        { once: true }
      );
    });

    if (typeof tab == "function") {
      tab();
    } else {
      tabbrowser.selectedTab = tab;
    }
    return promise;
  },

  /**
   * Waits for an ongoing page load in a browser window to complete. By default
   * about:blank loads are ignored.
   *
   * This can be used in conjunction with any synchronous method for starting a
   * load, like the "addTab" method on "tabbrowser", and must be called before
   * yielding control to the event loop.
   *
   * Note that calling this after multiple successive load operations can be racy,
   * so ``wantLoad`` should be specified in these cases. The same holds if we're
   * interested in about:blank to load.
   *
   * This function works by listening for custom load events on ``browser``. These
   * are sent by a BrowserTestUtils window actor in response to "load" and
   * "DOMContentLoaded" content events.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   * @param {object} options
   * @param {boolean} [options.includeSubFrames = false]
   *        A boolean indicating if loads from subframes should be included.
   * @param {string|function} [options.wantLoad]
   *        If a function, takes a URL and returns true if that's the load we're
   *        interested in. If a string, gives the URL of the load we're interested
   *        in. If not present, the first non-about:blank load resolves the promise.
   * @param {boolean} [options.maybeErrorPage = false]
   *        If true, this uses DOMContentLoaded event instead of load event.
   *        Also wantLoad will be called with visible URL, instead of
   *        'about:neterror?...' for error page.
   *
   * @return {Promise}
   *   Resovles when a load event is triggered for the browser.
   */
  browserLoaded(browser, ...args) {
    const options =
      args.length && typeof args[0] === "object"
        ? args[0]
        : {
            includeSubFrames: args[0] ?? false,
            wantLoad: args[1] ?? null,
            maybeErrorPage: args[2] ?? false,
          };
    const {
      includeSubFrames = false,
      wantLoad = null,
      maybeErrorPage = false,
    } = options;
    let startTime = ChromeUtils.now();
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = browser.documentGlobal || browser.ownerGlobal;
    let { innerWindowId } = win.windowGlobalChild;

    // Passing a url as second argument is a common mistake we should prevent.
    if (includeSubFrames && typeof includeSubFrames != "boolean") {
      throw new Error(
        "The second argument to browserLoaded should be a boolean."
      );
    }

    // Consumers may pass gBrowser instead of a browser, so adjust for that.
    if ("selectedBrowser" in browser) {
      browser = browser.selectedBrowser;
    }

    // If browser belongs to tabbrowser-tab, ensure it has been
    // inserted into the document.
    let tabbrowser = win.gBrowser;
    if (tabbrowser && tabbrowser.getTabForBrowser) {
      let tab = tabbrowser.getTabForBrowser(browser);
      if (tab) {
        tabbrowser._insertBrowser(tab);
      }
    }

    function isWanted(url) {
      if (!wantLoad) {
        return !url.startsWith("about:blank");
      } else if (typeof wantLoad == "function") {
        return wantLoad(url);
      }

      // HTTPS-First (Bug 1704453) TODO: In case we are waiting
      // for an http:// URL to be loaded and https-first is enabled,
      // then we also return true in case the backend upgraded
      // the load to https://.
      if (
        BrowserTestUtils._httpsFirstEnabled &&
        typeof wantLoad == "string" &&
        wantLoad.startsWith("http://")
      ) {
        let wantLoadHttps = wantLoad.replace("http://", "https://");
        if (wantLoadHttps == url) {
          return true;
        }
      }

      // It's a string.
      return wantLoad == url;
    }

    // Error pages are loaded slightly differently, so listen for the
    // DOMContentLoaded event for those instead.
    let loadEvent = maybeErrorPage ? "DOMContentLoaded" : "load";
    let eventName = `BrowserTestUtils:ContentEvent:${loadEvent}`;

    return new Promise((resolve, reject) => {
      function listener(event) {
        switch (event.type) {
          case eventName: {
            let { browsingContext, internalURL, visibleURL } = event.detail;

            // Sometimes we arrive here without an internalURL. If that's the
            // case, just keep waiting until we get one.
            if (!internalURL) {
              return;
            }

            // Ignore subframes if we only care about the top-level load.
            let subframe = browsingContext !== browsingContext.top;
            if (subframe && !includeSubFrames) {
              return;
            }

            // See testing/mochitest/BrowserTestUtils/content/BrowserTestUtilsChild.sys.mjs
            // for the difference between visibleURL and internalURL.
            if (!isWanted(maybeErrorPage ? visibleURL : internalURL)) {
              return;
            }

            ChromeUtils.addProfilerMarker(
              "BrowserTestUtils",
              { startTime, category: "Test", innerWindowId },
              "browserLoaded: " + internalURL
            );
            resolve(internalURL);
            break;
          }

          case "unload":
            reject(
              new Error(
                "The window unloaded while we were waiting for the browser to load - this should never happen."
              )
            );
            break;

          default:
            return;
        }

        browser.removeEventListener(eventName, listener, true);
        win.removeEventListener("unload", listener);
      }

      browser.addEventListener(eventName, listener, true);
      win.addEventListener("unload", listener);
    });
  },

  /**
   * Waits for the selected browser to load in a new window. This
   * is most useful when you've got a window that might not have
   * loaded its DOM yet, and where you can't easily use browserLoaded
   * on gBrowser.selectedBrowser since gBrowser doesn't yet exist.
   *
   * @param {xul:window} window
   *        A newly opened window for which we're waiting for the
   *        first browser load.
   * @param {boolean} aboutBlank [optional]
   *        If false, about:blank loads are ignored and we continue
   *        to wait.
   * @param {function|null} checkFn [optional]
   *        If checkFn(browser) returns false, the load is ignored
   *        and we continue to wait.
   *
   * @return {Promise<Event>}
   *   Resolves to the fired load event.
   */
  firstBrowserLoaded(win, aboutBlank = true, checkFn = null) {
    return this.waitForEvent(
      win,
      "BrowserTestUtils:ContentEvent:load",
      true,
      event => {
        if (checkFn) {
          return checkFn(event.target);
        }
        return (
          win.gBrowser.selectedBrowser.currentURI.spec !== "about:blank" ||
          aboutBlank
        );
      }
    );
  },

  _webProgressListeners: new Set(),

  _contentEventListenerSharedState: new Map(),

  _contentEventListeners: new Map(),

  /**
   * Waits for the web progress listener associated with this tab to fire a
   * state change that matches checkFn for the toplevel document.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   * @param {string} expectedURI (optional)
   *        A specific URL to check the channel load against
   * @param {Function} checkFn
   *        If checkFn(aStateFlags, aStatus) returns false, the state change
   *        is ignored and we continue to wait.
   *
   * @return {Promise<void>}
   *   Resolves when the desired state change reaches the tab's progress listener.
   */
  waitForBrowserStateChange(browser, expectedURI, checkFn) {
    return new Promise(resolve => {
      let wpl = {
        onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
          dump(
            "Saw state " +
              aStateFlags.toString(16) +
              " and status " +
              aStatus.toString(16) +
              "\n"
          );
          if (checkFn(aStateFlags, aStatus) && aWebProgress.isTopLevel) {
            let chan = aRequest.QueryInterface(Ci.nsIChannel);
            dump(
              "Browser got expected state change " +
                chan.originalURI.spec +
                "\n"
            );
            if (!expectedURI || chan.originalURI.spec == expectedURI) {
              browser.removeProgressListener(wpl);
              BrowserTestUtils._webProgressListeners.delete(wpl);
              resolve();
            }
          }
        },
        onSecurityChange() {},
        onStatusChange() {},
        onLocationChange() {},
        onContentBlockingEvent() {},
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsIWebProgressListener2",
          "nsISupportsWeakReference",
        ]),
      };
      browser.addProgressListener(wpl);
      this._webProgressListeners.add(wpl);
      dump(
        "Waiting for browser state change" +
          (expectedURI ? " of " + expectedURI : "") +
          "\n"
      );
    });
  },

  /**
   * Waits for the web progress listener associated with this tab to fire a
   * STATE_STOP for the toplevel document.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   * @param {string} expectedURI (optional)
   *        A specific URL to check the channel load against
   * @param {boolean} checkAborts (optional, defaults to false)
   *        Whether NS_BINDING_ABORTED stops 'count' as 'real' stops
   *        (e.g. caused by the stop button or equivalent APIs)
   *
   * @return {Promise<void>}
   *   Resolves when STATE_STOP reaches the tab's progress listener.
   */
  browserStopped(browser, expectedURI, checkAborts = false) {
    let testFn = function (aStateFlags, aStatus) {
      return (
        aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
        aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
        (checkAborts || aStatus != Cr.NS_BINDING_ABORTED)
      );
    };
    dump(
      "Waiting for browser load" +
        (expectedURI ? " of " + expectedURI : "") +
        "\n"
    );
    return BrowserTestUtils.waitForBrowserStateChange(
      browser,
      expectedURI,
      testFn
    );
  },

  /**
   * Waits for the web progress listener associated with this tab to fire a
   * STATE_START for the toplevel document.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   * @param {string} expectedURI (optional)
   *        A specific URL to check the channel load against
   *
   * @return {Promise<void>}
   *   Resolves when STATE_START reaches the tab's progress listener
   */
  browserStarted(browser, expectedURI) {
    let testFn = function (aStateFlags) {
      return (
        aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
        aStateFlags & Ci.nsIWebProgressListener.STATE_START
      );
    };
    dump(
      "Waiting for browser to start load" +
        (expectedURI ? " of " + expectedURI : "") +
        "\n"
    );
    return BrowserTestUtils.waitForBrowserStateChange(
      browser,
      expectedURI,
      testFn
    );
  },

  /**
   * Waits for a tab to open and load a given URL.
   *
   * By default, the method doesn't wait for the tab contents to load.
   *
   * @param {tabbrowser} tabbrowser
   *        The tabbrowser to look for the next new tab in.
   * @param {string|function} [wantLoad = null]
   *        If a function, takes a URL and returns true if that's the load we're
   *        interested in. If a string, gives the URL of the load we're interested
   *        in. If not present, the first non-about:blank load is used.
   * @param {boolean} [waitForLoad = false]
   *        True to wait for the page in the new tab to load. Defaults to false.
   * @param {boolean} [waitForAnyTab = false]
   *        True to wait for the url to be loaded in any new tab, not just the next
   *        one opened.
   * @param {boolean} [maybeErrorPage = false]
   *        See ``browserLoaded`` function.
   *
   * @return {Promise}
   *   Resolves with the {xul:tab} when a tab is opened and its location changes
   *   to the given URL and optionally that browser has loaded.
   *
   * NB: this method will not work if you open a new tab with e.g. BrowserCommands.openTab
   * and the tab does not load a URL, because no onLocationChange will fire.
   */
  waitForNewTab(
    tabbrowser,
    wantLoad = null,
    waitForLoad = false,
    waitForAnyTab = false,
    maybeErrorPage = false
  ) {
    let urlMatches;
    if (wantLoad && typeof wantLoad == "function") {
      urlMatches = wantLoad;
    } else if (wantLoad) {
      urlMatches = urlToMatch => urlToMatch == wantLoad;
    } else {
      urlMatches = urlToMatch => urlToMatch != "about:blank";
    }
    return new Promise(resolve => {
      tabbrowser.tabContainer.addEventListener(
        "TabOpen",
        function tabOpenListener(openEvent) {
          if (!waitForAnyTab) {
            tabbrowser.tabContainer.removeEventListener(
              "TabOpen",
              tabOpenListener
            );
          }
          let newTab = openEvent.target;
          if (wantLoad == "about:blank") {
            TestUtils.executeSoon(() => resolve(newTab));
            return;
          }
          let newBrowser = newTab.linkedBrowser;
          let result;
          if (waitForLoad) {
            // If waiting for load, resolve with promise for that, which when load
            // completes resolves to the new tab.
            result = BrowserTestUtils.browserLoaded(newBrowser, {
              includeSubFrames: false,
              wantLoad: urlMatches,
              maybeErrorPage,
            }).then(() => newTab);
          } else {
            // If not waiting for load, just resolve with the new tab.
            result = newTab;
          }

          let progressListener = {
            onLocationChange(aBrowser) {
              // Only interested in location changes on our browser.
              if (aBrowser != newBrowser) {
                return;
              }

              // Check that new location is the URL we want.
              if (!urlMatches(aBrowser.currentURI.spec)) {
                return;
              }
              if (waitForAnyTab) {
                tabbrowser.tabContainer.removeEventListener(
                  "TabOpen",
                  tabOpenListener
                );
              }
              tabbrowser.removeTabsProgressListener(progressListener);
              TestUtils.executeSoon(() => resolve(result));
            },
          };
          tabbrowser.addTabsProgressListener(progressListener);
        }
      );
    });
  },

  /**
   * Waits for onLocationChange.
   *
   * @param {tabbrowser} tabbrowser
   *        The tabbrowser to wait for the location change on.
   * @param {string} [url]
   *        The string URL to look for. The URL must match the URL in the
   *        location bar exactly.
   * @return {Promise<{webProgress: nsIWebProgress, request: nsIRequest, flags: number}>}
   */
  waitForLocationChange(tabbrowser, url) {
    return new Promise(resolve => {
      let progressListener = {
        onLocationChange(browser, webProgress, request, newURI, flags) {
          if (
            (url && newURI.spec != url) ||
            (!url && newURI.spec == "about:blank")
          ) {
            return;
          }

          tabbrowser.removeTabsProgressListener(progressListener);
          resolve({ webProgress, request, flags });
        },
      };
      tabbrowser.addTabsProgressListener(progressListener);
    });
  },

  /**
   * Waits for the next browser window to open and be fully loaded.
   *
   * @param {object} aParams
   * @param {string} [aParams.url]
   *        The url to await being loaded. If unset this may or may not wait for
   *        any page to be loaded, according to the waitForAnyURLLoaded param.
   * @param {bool} [aParams.waitForAnyURLLoaded] When `url` is unset, this
   *        controls whether to wait for any initial URL to be loaded.
   *        Defaults to false, that means the initial browser may or may not
   *        have finished loading its first page when this resolves.
   *        When `url` is set, this is ignored, thus the load is always awaited for.
   * @param {bool} [aParams.anyWindow]
   * @param {bool} [aParams.maybeErrorPage]
   *        See ``browserLoaded`` function.
   * @return {Promise}
   *         A Promise which resolves the next time that a DOM window
   *         opens and the delayed startup observer notification fires.
   */
  waitForNewWindow(aParams = {}) {
    let {
      url = null,
      anyWindow = false,
      maybeErrorPage = false,
      waitForAnyURLLoaded = false,
    } = aParams;

    if (anyWindow && !url) {
      throw new Error("url should be specified if anyWindow is true");
    }

    return new Promise((resolve, reject) => {
      let observe = async (win, topic) => {
        if (topic != "domwindowopened") {
          return;
        }

        try {
          if (!anyWindow) {
            Services.ww.unregisterNotification(observe);
          }

          let promises = [];

          if (url || waitForAnyURLLoaded) {
            await this.waitForEvent(win, "DOMContentLoaded");

            if (win.document.documentURI != AppConstants.BROWSER_CHROME_URL) {
              return;
            }
          }

          if (!(win.gBrowserInit && win.gBrowserInit.delayedStartupFinished)) {
            promises.push(
              TestUtils.topicObserved(
                "browser-delayed-startup-finished",
                subject => subject == win
              )
            );
          }

          if (url || waitForAnyURLLoaded) {
            let loadPromise = this.browserLoaded(win.gBrowser.selectedBrowser, {
              includeSubFrames: false,
              wantLoad: waitForAnyURLLoaded ? null : url,
              maybeErrorPage,
            });
            promises.push(loadPromise);
          }

          await Promise.all(promises);

          await this.ensureWindowActivated(win);

          if (anyWindow) {
            Services.ww.unregisterNotification(observe);
          }
          resolve(win);
        } catch (err) {
          // We failed to wait for the load in this URI. This is only an error
          // if `anyWindow` is not set, as if it is we can just wait for another
          // window.
          if (!anyWindow) {
            reject(err);
          }
        }
      };
      Services.ww.registerNotification(observe);
    });
  },

  /**
   * Starts the load of a new URI in the given browser, triggered by the system
   * principal.
   * Note this won't want for the load to be complete. For that you may either
   * use BrowserTestUtils.browserLoaded(), BrowserTestUtils.waitForErrorPage(),
   * or make your own handler.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   * @param {string} uri
   *        The URI to load.
   * @param {number} loadFlags [optional]
   *        Load flags to pass to nsIWebNavigation.loadURI.
   */
  startLoadingURIString(browser, uri, loadFlags) {
    browser.fixupAndLoadURIString(uri, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
      loadFlags,
    });
  },

  /**
   * Loads a given URI in the specified tab and waits for the load to complete.
   *
   * @param {object} options
   * @param {xul:browser} options.browser
   *   The browser to load the URI into.
   * @param {string} options.uriString
   *   The string URI to load.
   * @param {string} [options.finalURI]
   *   The expected final URI to wait for, e.g. if the load is automatically
   *   redirected.
   */
  loadURIString({ browser, uriString, finalURI = uriString }) {
    this.startLoadingURIString(browser, uriString);
    return this.browserLoaded(browser, { wantLoad: finalURI });
  },

  /**
   * Maybe create a preloaded browser and ensure it's finished loading.
   *
   * @param gBrowser (<xul:tabbrowser>)
   *        The tabbrowser in which to preload a browser.
   */
  async maybeCreatePreloadedBrowser(gBrowser) {
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = gBrowser.documentGlobal || gBrowser.ownerGlobal;
    win.NewTabPagePreloading.maybeCreatePreloadedBrowser(win);

    // We cannot use the regular BrowserTestUtils helper for waiting here, since that
    // would try to insert the preloaded browser, which would only break things.
    await lazy.ContentTask.spawn(gBrowser.preloadedBrowser, [], async () => {
      await ContentTaskUtils.waitForCondition(() => {
        return (
          this.content.document &&
          this.content.document.readyState == "complete"
        );
      });
    });
  },

  /**
   * @param win (optional)
   *        The window we should wait to have "domwindowopened" sent through
   *        the observer service for. If this is not supplied, we'll just
   *        resolve when the first "domwindowopened" notification is seen.
   * @param {function} checkFn [optional]
   *        Called with the nsIDOMWindow object as argument, should return true
   *        if the event is the expected one, or false if it should be ignored
   *        and observing should continue. If not specified, the first window
   *        resolves the returned promise.
   * @return {Promise}
   *         A Promise which resolves when a "domwindowopened" notification
   *         has been fired by the window watcher.
   */
  domWindowOpened(win, checkFn) {
    return new Promise(resolve => {
      async function observer(subject, topic) {
        if (topic == "domwindowopened" && (!win || subject === win)) {
          let observedWindow = subject;
          if (checkFn && !(await checkFn(observedWindow))) {
            return;
          }
          Services.ww.unregisterNotification(observer);
          resolve(observedWindow);
        }
      }
      Services.ww.registerNotification(observer);
    });
  },

  /**
   * @param win (optional)
   *        The window we should wait to have "domwindowopened" sent through
   *        the observer service for. If this is not supplied, we'll just
   *        resolve when the first "domwindowopened" notification is seen.
   *        The promise will be resolved once the new window's document has been
   *        loaded.
   *
   * @param {function} checkFn (optional)
   *        Called with the nsIDOMWindow object as argument, should return true
   *        if the event is the expected one, or false if it should be ignored
   *        and observing should continue. If not specified, the first window
   *        resolves the returned promise.
   *
   * @return {Promise}
   *         A Promise which resolves when a "domwindowopened" notification
   *         has been fired by the window watcher.
   */
  domWindowOpenedAndLoaded(win, checkFn) {
    return this.domWindowOpened(win, async observedWin => {
      await this.waitForEvent(observedWin, "load");
      if (checkFn && !(await checkFn(observedWin))) {
        return false;
      }
      return true;
    });
  },

  /**
   * Ensures a chrome window is activated. On Windows, new windows may be shown
   * without OS activation (SW_SHOWNOACTIVATE) when another process has the
   * foreground, so focus/activate events never fire. This checks whether
   * activation already occurred and forces it if not.
   *
   * @param {ChromeWindow} win
   */
  async ensureWindowActivated(win) {
    if (Services.focus.activeWindow !== win) {
      let activatePromise = Promise.all([
        this.waitForEvent(win, "focus", true),
        this.waitForEvent(win, "activate"),
      ]);
      win.focus();
      await activatePromise;
    }
  },

  /**
   * @param win (optional)
   *        The window we should wait to have "domwindowclosed" sent through
   *        the observer service for. If this is not supplied, we'll just
   *        resolve when the first "domwindowclosed" notification is seen.
   * @return {Promise}
   *         A Promise which resolves when a "domwindowclosed" notification
   *         has been fired by the window watcher.
   */
  domWindowClosed(win) {
    return new Promise(resolve => {
      function observer(subject, topic) {
        if (topic == "domwindowclosed" && (!win || subject === win)) {
          Services.ww.unregisterNotification(observer);
          resolve(subject);
        }
      }
      Services.ww.registerNotification(observer);
    });
  },

  /**
   * Open a new browser window from an existing one.
   * This relies on OpenBrowserWindow in browser.js, and waits for the window
   * to be completely loaded before resolving.
   *
   * @param {object} [options]
   *        Options to pass to OpenBrowserWindow. Additionally, supports:
   * @param {string} [options.waitForTabURL]
   *        Forces the initial browserLoaded check to wait for the tab to
   *        load the given URL (instead of about:blank)
   *
   * @return {Promise}
   *         Resolves with the new window once it is loaded.
   */
  async openNewBrowserWindow(options = {}) {
    let startTime = ChromeUtils.now();

    let openerWindow = lazy.BrowserWindowTracker.getTopWindow({
      private: false,
    });
    let win = lazy.BrowserWindowTracker.openWindow({
      openerWindow,
      ...options,
    });

    // Wait for browser-delayed-startup-finished notification, it indicates
    // that the window has loaded completely and is ready to be used for
    // testing.
    let promises = [
      TestUtils.topicObserved(
        "browser-delayed-startup-finished",
        subject => subject == win
      ).then(() => win),
      this.firstBrowserLoaded(win, !options.waitForTabURL, browser => {
        return (
          !options.waitForTabURL ||
          options.waitForTabURL == browser.currentURI.spec
        );
      }),
    ];

    await Promise.all(promises);

    await this.ensureWindowActivated(win);
    ChromeUtils.addProfilerMarker(
      "BrowserTestUtils",
      { startTime, category: "Test" },
      "openNewBrowserWindow"
    );

    return win;
  },

  /**
   * Closes a window.
   *
   * @param {Window} win
   *        A window to close.
   *
   * @return {Promise}
   *         Resolves when the provided window has been closed. For browser
   *         windows, the Promise will also wait until all final SessionStore
   *         messages have been sent up from all browser tabs.
   */
  closeWindow(win) {
    let closedPromise = BrowserTestUtils.windowClosed(win);
    win.close();
    return closedPromise;
  },

  /**
   * Returns a Promise that resolves when a window has finished closing.
   *
   * @param {Window} win
   *        The closing window.
   *
   * @return {Promise}
   *        Resolves when the provided window has been fully closed. For
   *        browser windows, the Promise will also wait until all final
   *        SessionStore messages have been sent up from all browser tabs.
   */
  windowClosed(win) {
    let domWinClosedPromise = BrowserTestUtils.domWindowClosed(win);
    let promises = [domWinClosedPromise];
    let winType = win.document.documentElement.getAttribute("windowtype");
    let flushTopic = "sessionstore-browser-shutdown-flush";

    if (winType == "navigator:browser") {
      let finalMsgsPromise = new Promise(resolve => {
        let browserSet = new Set(win.gBrowser.browsers);
        // Ensure all browsers have been inserted or we won't get
        // messages back from them.
        browserSet.forEach(browser => {
          win.gBrowser._insertBrowser(win.gBrowser.getTabForBrowser(browser));
        });

        let observer = subject => {
          if (browserSet.has(subject)) {
            browserSet.delete(subject);
          }
          if (!browserSet.size) {
            Services.obs.removeObserver(observer, flushTopic);
            // Give the TabStateFlusher a chance to react to this final
            // update and for the TabStateFlusher.flushWindow promise
            // to resolve before we resolve.
            TestUtils.executeSoon(resolve);
          }
        };

        Services.obs.addObserver(observer, flushTopic);
      });

      promises.push(finalMsgsPromise);
    }

    return Promise.all(promises);
  },

  /**
   * Returns a Promise that resolves once the SessionStore information for the
   * given tab is updated and all listeners are called.
   *
   * @param {xul:tab} tab
   *        The tab that will be removed.
   * @returns {Promise<void>}
   *   Resolves when the SessionStore information is updated.
   */
  waitForSessionStoreUpdate(tab) {
    let browser = tab.linkedBrowser;
    return TestUtils.topicObserved(
      "sessionstore-browser-shutdown-flush",
      s => s === browser
    );
  },

  /**
   * @returns {Promise<void>}
   *   Resolves when the locale has been changed.
   */
  enableRtlLocale() {
    let localeChanged = TestUtils.topicObserved("intl:app-locales-changed");
    Services.prefs.setStringPref("intl.l10n.pseudo", "bidi");
    return localeChanged;
  },

  /**
   * @returns {Promise<void>}
   *   Resolves when the locale has been changed.
   */
  disableRtlLocale() {
    let localeChanged = TestUtils.topicObserved("intl:app-locales-changed");
    Services.prefs.setStringPref("intl.l10n.pseudo", "");
    return localeChanged;
  },

  /**
   * Waits for an event to be fired on a specified element.
   *
   * @example
   *
   *    let promiseEvent = BrowserTestUtils.waitForEvent(element, "eventName");
   *    // Do some processing here that will cause the event to be fired
   *    // ...
   *    // Now wait until the Promise is fulfilled
   *    let receivedEvent = await promiseEvent;
   *
   * @example
   *    // The promise resolution/rejection handler for the returned promise is
   *    // guaranteed not to be called until the next event tick after the event
   *    // listener gets called, so that all other event listeners for the element
   *    // are executed before the handler is executed.
   *
   *    let promiseEvent = BrowserTestUtils.waitForEvent(element, "eventName");
   *    // Same event tick here.
   *    await promiseEvent;
   *    // Next event tick here.
   *
   * @example
   *    // If some code, such like adding yet another event listener, needs to be
   *    // executed in the same event tick, use raw addEventListener instead and
   *    // place the code inside the event listener.
   *
   *    element.addEventListener("load", () => {
   *      // Add yet another event listener in the same event tick as the load
   *      // event listener.
   *      p = BrowserTestUtils.waitForEvent(element, "ready");
   *    }, { once: true });
   *
   * @param {Element} subject
   *        The element that should receive the event.
   * @param {string} eventName
   *        Name of the event to listen to.
   * @param {bool} [capture]
   *        True to use a capturing listener.
   * @param {function} [checkFn]
   *        Called with the Event object as argument, should return true if the
   *        event is the expected one, or false if it should be ignored and
   *        listening should continue. If not specified, the first event with
   *        the specified name resolves the returned promise.
   * @param {bool} [wantsUntrusted=false]
   *        True to receive synthetic events dispatched by web content.
   *
   * Note: Because this function is intended for testing, any error in checkFn
   *       will cause the returned promise to be rejected instead of waiting for
   *       the next event, since this is probably a bug in the test.
   *
   * @returns {Promise<Event>}
   */
  waitForEvent(subject, eventName, capture, checkFn, wantsUntrusted) {
    let startTime = ChromeUtils.now();
    let innerWindowId = (() => {
      if (subject.windowGlobalChild) {
        return subject.windowGlobalChild.innerWindowId;
      }
      if ("ownerDocument" in subject) {
        // @backward-compat { version 152 }
        // Get rid of the documentGlobal fallback once 152 makes it to release.
        let win = subject.documentGlobal || subject.ownerGlobal;
        return win.windowGlobalChild.innerWindowId;
      }
      return null;
    })();

    return new Promise((resolve, reject) => {
      let removed = false;
      function listener(event) {
        function cleanup() {
          removed = true;
          // Avoid keeping references to objects after the promise resolves.
          subject = null;
          checkFn = null;
        }
        try {
          if (checkFn && !checkFn(event)) {
            return;
          }
          subject.removeEventListener(eventName, listener, capture);
          cleanup();
          TestUtils.executeSoon(() => {
            ChromeUtils.addProfilerMarker(
              "BrowserTestUtils",
              { startTime, category: "Test", innerWindowId },
              "waitForEvent: " + eventName
            );
            resolve(event);
          });
        } catch (ex) {
          try {
            subject.removeEventListener(eventName, listener, capture);
          } catch (ex2) {
            // Maybe the provided object does not support removeEventListener.
          }
          cleanup();
          TestUtils.executeSoon(() => reject(ex));
        }
      }

      subject.addEventListener(eventName, listener, capture, wantsUntrusted);

      TestUtils.promiseTestFinished?.then(() => {
        if (removed) {
          return;
        }

        subject.removeEventListener(eventName, listener, capture);
        let text = eventName + " listener";
        if (subject.id) {
          text += ` on #${subject.id}`;
        }
        text += " not removed before the end of test";
        reject(text);
        ChromeUtils.addProfilerMarker(
          "BrowserTestUtils",
          { startTime, category: "Test", innerWindowId },
          "waitForEvent: " + text
        );
      });
    });
  },

  /**
   * Like waitForEvent, but adds the event listener to the message manager
   * global for browser.
   *
   * @param {string} eventName
   *        Name of the event to listen to.
   * @param {bool} capture [optional]
   *        Whether to use a capturing listener.
   * @param {function} checkFn [optional]
   *        Called with the Event object as argument, should return true if the
   *        event is the expected one, or false if it should be ignored and
   *        listening should continue. If not specified, the first event with
   *        the specified name resolves the returned promise.
   * @param {bool} wantUntrusted [optional]
   *        Whether to accept untrusted events
   *
   * Note: As of bug 1588193, this function no longer rejects the returned
   *       promise in the case of a checkFn error. Instead, since checkFn is now
   *       called through eval in the content process, the error is thrown in
   *       the listener created by ContentEventListenerChild. Work to improve
   *       error handling (eg. to reject the promise as before and to preserve
   *       the filename/stack) is being tracked in bug 1593811.
   *
   * @returns {Promise<string>}
   *   Resolves with the event name.
   */
  waitForContentEvent(
    browser,
    eventName,
    capture = false,
    checkFn,
    wantUntrusted = false
  ) {
    return new Promise(resolve => {
      let removeEventListener = this.addContentEventListener(
        browser,
        eventName,
        () => {
          removeEventListener();
          resolve(eventName);
        },
        { capture, wantUntrusted },
        checkFn
      );
    });
  },

  /**
   * Like waitForEvent, but acts on a popup. It ensures the popup is not already
   * in the expected state.
   *
   * @param {Element} popup
   *        The popup element that should receive the event.
   * @param {string} eventSuffix
   *        The event suffix expected to be received, one of "shown" or "hidden".
   * @returns {Promise}
   */
  waitForPopupEvent(popup, eventSuffix) {
    let endState = { shown: "open", hidden: "closed" }[eventSuffix];

    if (popup.state == endState) {
      return Promise.resolve();
    }
    return this.waitForEvent(popup, "popup" + eventSuffix);
  },

  /**
   * Waits for the select popup to be shown. This is needed because the select
   * dropdown is created lazily.
   *
   * @param {Window} win
   *        A window to expect the popup in.
   *
   * @return {Promise}
   *        Resolves when the popup has been fully opened. The resolution value
   *        is the select popup.
   */
  async waitForSelectPopupShown(win) {
    let getMenulist = () =>
      win.document.getElementById("ContentSelectDropdown");
    let menulist = getMenulist();
    if (!menulist) {
      await this.waitForMutationCondition(
        win.document,
        { childList: true, subtree: true },
        getMenulist
      );
      menulist = getMenulist();
      if (menulist.menupopup.state == "open") {
        return menulist.menupopup;
      }
    }
    await this.waitForEvent(menulist.menupopup, "popupshown");
    return menulist.menupopup;
  },

  /**
   * Waits for the datetime picker popup to be shown.
   *
   * @param {Window} win
   *        A window to expect the popup in.
   *
   * @return {Promise}
   *        Resolves when the popup has been fully opened. The resolution value
   *        is the select popup.
   */
  async waitForDateTimePickerPanelShown(win) {
    let getPanel = () => win.document.getElementById("DateTimePickerPanel");
    let panel = getPanel();
    let ensureReady = async () => {
      let frame = panel.querySelector("#DateTimePickerPanelPopupFrame");
      let isValidUrl = () => {
        return (
          frame.browsingContext?.currentURI?.spec ==
          "chrome://global/content/datetimepicker.xhtml"
        );
      };

      // Ensure it's loaded.
      if (!isValidUrl() || frame.contentDocument.readyState != "complete") {
        await new Promise(resolve => {
          frame.addEventListener(
            "load",
            function listener() {
              if (isValidUrl()) {
                frame.removeEventListener("load", listener, { capture: true });
                resolve();
              }
            },
            { capture: true }
          );
        });
      }

      // Ensure it's ready.
      if (!frame.contentWindow.PICKER_READY) {
        await new Promise(resolve => {
          frame.contentDocument.addEventListener("PickerReady", resolve, {
            once: true,
          });
        });
      }
      // And that l10n mutations are flushed.
      // FIXME(bug 1828721): We should ideally localize everything before
      // showing the panel.
      if (frame.contentDocument.hasPendingL10nMutations) {
        await new Promise(resolve => {
          frame.contentDocument.addEventListener(
            "L10nMutationsFinished",
            resolve,
            {
              once: true,
            }
          );
        });
      }
    };

    if (!panel) {
      await this.waitForMutationCondition(
        win.document,
        { childList: true, subtree: true },
        getPanel
      );
      panel = getPanel();
      if (panel.state == "open") {
        await ensureReady();
        return panel;
      }
    }
    await this.waitForEvent(panel, "popupshown");
    await ensureReady();
    return panel;
  },

  /**
   * Adds a content event listener on the given browser
   * element. Similar to waitForContentEvent, but the listener will
   * fire until it is removed. A callable object is returned that,
   * when called, removes the event listener. Note that this function
   * works even if the browser's frameloader is swapped.
   *
   * @param {xul:browser} browser
   *        The browser element to listen for events in.
   * @param {string} eventName
   *        Name of the event to listen to.
   * @param {function} listener
   *        Function to call in parent process when event fires.
   *        Not passed any arguments.
   * @param {object} listenerOptions [optional]
   *        Options to pass to the event listener.
   * @param {function} checkFn [optional]
   *        Called with the Event object as argument, should return true if the
   *        event is the expected one, or false if it should be ignored and
   *        listening should continue. If not specified, the first event with
   *        the specified name resolves the returned promise. This is called
   *        within the content process and can have no closure environment.
   *
   * @returns function
   *        If called, the return value will remove the event listener.
   */
  addContentEventListener(
    browser,
    eventName,
    listener,
    listenerOptions = {},
    checkFn
  ) {
    let id = gListenerId++;
    let contentEventListeners = this._contentEventListeners;
    contentEventListeners.set(id, {
      listener,
      browserId: browser.browserId,
    });

    let eventListenerState = this._contentEventListenerSharedState;
    eventListenerState.set(id, {
      eventName,
      listenerOptions,
      checkFnSource: checkFn ? checkFn.toSource() : "",
    });

    Services.ppmm.sharedData.set(
      "BrowserTestUtils:ContentEventListener",
      eventListenerState
    );
    Services.ppmm.sharedData.flush();

    let unregisterFunction = function () {
      if (!eventListenerState.has(id)) {
        return;
      }
      eventListenerState.delete(id);
      contentEventListeners.delete(id);
      Services.ppmm.sharedData.set(
        "BrowserTestUtils:ContentEventListener",
        eventListenerState
      );
      Services.ppmm.sharedData.flush();
    };
    return unregisterFunction;
  },

  /**
   * This is an internal method to be invoked by
   * BrowserTestUtilsParent.sys.mjs when a content event we were listening for
   * happens.
   *
   * @private
   */
  _receivedContentEventListener(listenerId, browserId) {
    let listenerData = this._contentEventListeners.get(listenerId);
    if (!listenerData) {
      return;
    }
    if (listenerData.browserId != browserId) {
      return;
    }
    listenerData.listener();
  },

  /**
   * This is an internal method that cleans up any state from content event
   * listeners.
   *
   * @private
   */
  _cleanupContentEventListeners() {
    this._contentEventListeners.clear();

    if (this._contentEventListenerSharedState.size != 0) {
      this._contentEventListenerSharedState.clear();
      Services.ppmm.sharedData.set(
        "BrowserTestUtils:ContentEventListener",
        this._contentEventListenerSharedState
      );
      Services.ppmm.sharedData.flush();
    }

    if (this._contentEventListenerActorRegistered) {
      this._contentEventListenerActorRegistered = false;
      ChromeUtils.unregisterWindowActor("ContentEventListener");
    }
  },

  observe(subject, topic) {
    switch (topic) {
      case "test-complete":
        this._cleanupContentEventListeners();
        break;
    }
  },

  /**
   * Wait until DOM mutations cause the condition expressed in checkFn
   * to pass.
   *
   * Intended as an easy-to-use alternative to waitForCondition.
   *
   * @param {Element} target    The target in which to observe mutations.
   * @param {object}  options   The options to pass to MutationObserver.observe();
   * @param {function} checkFn  Function that returns true when it wants the promise to be
   * resolved.
   * @returns {Promise<any>}    The value returned by `checkFn`.
   */
  waitForMutationCondition(target, options, checkFn) {
    let retVal;
    if ((retVal = checkFn())) {
      return Promise.resolve(retVal);
    }
    return new Promise(resolve => {
      // @backward-compat { version 152 }
      // Get rid of the documentGlobal fallback once 152 makes it to release.
      let win = target.documentGlobal || target.ownerGlobal;
      let obs = new win.MutationObserver(function () {
        if ((retVal = checkFn())) {
          obs.disconnect();
          resolve(retVal);
        }
      });
      obs.observe(target, options);
    });
  },

  /**
   * Waits until painting is no longer suppressed in a browsing context.
   *
   * This is needed before synthesizing a mouse event on a freshly loaded
   * document, because hit testing ignores content while painting is
   * suppressed: the click resolves to the document root and fires no event.
   *
   * @param {BrowsingContext} browsingContext
   *        The browsing context to wait for. Note that painting is suppressed
   *        per document, so for content in an iframe this needs to be the
   *        iframe's browsing context rather than the top level one.
   */
  async waitForPaintingUnsuppressed(browsingContext) {
    try {
      await this.sendQuery(
        browsingContext,
        "BrowserTestUtils:WaitForPaintingUnsuppressed"
      );
    } catch (ex) {
      // The document may have gone away while we were waiting, eg. because the
      // error page was only transient. There is nothing left to paint then.
      console.warn(`waitForPaintingUnsuppressed: ${ex}`);
    }
  },

  /**
   * Like browserLoaded, but waits for an error page to appear.
   *
   * Also waits for painting to be unsuppressed in the browser, so that clicks
   * synthesized on the error page actually hit test to its content.
   *
   * @param {xul:browser} browser
   *        A xul:browser.
   *
   * @return {Promise<string>}
   *   Resolves when an error page has been loaded in the browser, with the name
   *   of the event.
   */
  async waitForErrorPage(browser) {
    let eventName = await this.waitForContentEvent(
      browser,
      "AboutNetErrorLoad",
      false,
      null,
      true
    );

    await this.waitForPaintingUnsuppressed(browser.browsingContext);

    return eventName;
  },

  /**
   * Waits for the next top-level document load in the current browser.  The URI
   * of the document is compared against expectedURL.  The load is then stopped
   * before it actually starts.
   *
   * @param {string} expectedURL
   *        The URL of the document that is expected to load.
   * @param {object} browser
   *        The browser to wait for.
   * @param {function} checkFn (optional)
   *        Function to run on the channel before stopping it.
   * @returns {Promise}
   */
  waitForDocLoadAndStopIt(expectedURL, browser, checkFn) {
    let isHttp = url => /^https?:/.test(url);

    return new Promise(resolve => {
      // Redirect non-http URIs to http://mochi.test:8888/, so we can still
      // use http-on-before-connect to listen for loads. Since we're
      // aborting the load as early as possible, it doesn't matter whether the
      // server handles it sensibly or not. However, this also means that this
      // helper shouldn't be used to load local URIs (about pages, chrome://
      // URIs, etc).
      let proxyFilter;
      if (!isHttp(expectedURL)) {
        proxyFilter = {
          proxyInfo: lazy.ProtocolProxyService.newProxyInfo(
            "http",
            "mochi.test",
            8888,
            "",
            "",
            0,
            4096,
            null
          ),

          applyFilter(channel, defaultProxyInfo, callback) {
            callback.onProxyFilterResult(
              isHttp(channel.URI.spec) ? defaultProxyInfo : this.proxyInfo
            );
          },
        };

        lazy.ProtocolProxyService.registerChannelFilter(proxyFilter, 0);
      }

      function observer(chan) {
        chan.QueryInterface(Ci.nsIHttpChannel);
        if (!chan.originalURI || chan.originalURI.spec !== expectedURL) {
          return;
        }
        if (checkFn && !checkFn(chan)) {
          return;
        }

        // TODO: We should check that the channel's BrowsingContext matches
        // the browser's. See bug 1587114.

        try {
          chan.cancel(Cr.NS_BINDING_ABORTED);
        } finally {
          if (proxyFilter) {
            lazy.ProtocolProxyService.unregisterChannelFilter(proxyFilter);
          }
          Services.obs.removeObserver(observer, "http-on-before-connect");
          resolve();
        }
      }

      Services.obs.addObserver(observer, "http-on-before-connect");
    });
  },

  /**
   *  Versions of EventUtils.sys.mjs synthesizeMouse functions that synthesize a
   *  mouse event in a child process and return promises that resolve when the
   *  event has fired and completed. Instead of a window, a browser or
   *  browsing context is required to be passed to this function.
   *
   * @param target
   *        One of the following:
   *        - a selector string that identifies the element to target. The syntax is as
   *          for querySelector.
   *        - a function to be run in the content process that returns the element to
   *        target
   *        - null, in which case the offset is from the content document's edge.
   * @param {integer} offsetX
   *        x offset from target's left bounding edge
   * @param {integer} offsetY
   *        y offset from target's top bounding edge
   * @param {object} event object
   *        Additional arguments, similar to the EventUtils.sys.mjs version
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   * @param {boolean} handlingUserInput
   *        Whether the synthesize should be perfomed while simulating
   *        user interaction (making windowUtils.isHandlingUserInput be true).
   *
   * @returns {Promise<boolean>}
   *   Resolves to true if the mouse event was cancelled.
   */
  synthesizeMouse(
    target,
    offsetX,
    offsetY,
    event,
    browsingContext,
    handlingUserInput
  ) {
    let targetFn = null;
    if (typeof target == "function") {
      targetFn = target.toString();
      target = null;
    } else if (typeof target != "string" && !Array.isArray(target)) {
      target = null;
    }

    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SynthesizeMouse", {
      target,
      targetFn,
      x: offsetX,
      y: offsetY,
      event,
      handlingUserInput,
    });
  },

  /**
   *  Versions of EventUtils.sys.mjs synthesizeTouch functions that synthesize a
   *  touch event in a child process and return promises that resolve when the
   *  event has fired and completed. Instead of a window, a browser or
   *  browsing context is required to be passed to this function.
   *
   * @param target
   *        One of the following:
   *        - a selector string that identifies the element to target. The syntax is as
   *          for querySelector.
   *        - a function to be run in the content process that returns the element to
   *        target
   *        - null, in which case the offset is from the content document's edge.
   * @param {integer} offsetX
   *        x offset from target's left bounding edge
   * @param {integer} offsetY
   *        y offset from target's top bounding edge
   * @param {object} event object
   *        Additional arguments, similar to the EventUtils.sys.mjs version
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   *
   * @returns {Promise<boolean>}
   *   Resolves to true if the touch event was cancelled.
   */
  synthesizeTouch(target, offsetX, offsetY, event, browsingContext) {
    let targetFn = null;
    if (typeof target == "function") {
      targetFn = target.toString();
      target = null;
    } else if (typeof target != "string" && !Array.isArray(target)) {
      target = null;
    }

    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SynthesizeTouch", {
      target,
      targetFn,
      x: offsetX,
      y: offsetY,
      event,
    });
  },

  /**
   * Wait for a message to be fired from a particular message manager
   *
   * @param {nsIMessageManager} messageManager
   *                            The message manager that should be used.
   * @param {string}            message
   *                            The message we're waiting for.
   * @param {Function}          checkFn (optional)
   *                            Optional function to invoke to check the message.
   */
  waitForMessage(messageManager, message, checkFn) {
    return new Promise(resolve => {
      messageManager.addMessageListener(message, function onMessage(msg) {
        if (!checkFn || checkFn(msg)) {
          messageManager.removeMessageListener(message, onMessage);
          resolve(msg.data);
        }
      });
    });
  },

  /**
   *  Version of synthesizeMouse that uses the center of the target as the mouse
   *  location. Arguments and the return value are the same.
   */
  synthesizeMouseAtCenter(target, event, browsingContext) {
    // Use a flag to indicate to center rather than having a separate message.
    event.centered = true;
    return BrowserTestUtils.synthesizeMouse(
      target,
      0,
      0,
      event,
      browsingContext
    );
  },

  /**
   *  Version of synthesizeMouse that uses a client point within the child
   *  window instead of a target as the offset. Otherwise, the arguments and
   *  return value are the same as synthesizeMouse.
   */
  synthesizeMouseAtPoint(offsetX, offsetY, event, browsingContext) {
    return BrowserTestUtils.synthesizeMouse(
      null,
      offsetX,
      offsetY,
      event,
      browsingContext
    );
  },

  /**
   * Removes the given tab from its parent tabbrowser.
   * This method doesn't SessionStore etc.
   *
   * @param (tab) tab
   *        The tab to remove.
   * @param (Object) options
   *        Extra options to pass to tabbrowser's removeTab method.
   */
  removeTab(tab, options = {}) {
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = tab.documentGlobal || tab.ownerGlobal;
    win.gBrowser.removeTab(tab, options);
  },

  /**
   * Returns a Promise that resolves once the tab starts closing.
   *
   * @param {tab} tab
   *        The tab that will be removed.
   * @returns {Promise<Event>}
   *   Resolves with the event when the tab starts closing.
   */
  waitForTabClosing(tab) {
    return this.waitForEvent(tab, "TabClose");
  },

  /**
   *
   * @param {tab} tab
   *        The tab that will be reloaded.
   * @param {object} [options]
   *        Options for the reload.
   * @param {boolean} options.includeSubFrames = false [optional]
   *        A boolean indicating if loads from subframes should be included
   *        when waiting for the frame to reload.
   * @param {boolean} options.bypassCache = false [optional]
   *        A boolean indicating if loads should bypass the cache.
   *        If bypassCache is true, this skips some steps that normally happen
   *        when a user reloads a tab.
   * @returns {Promise}
   *   Resolves when the tab finishes reloading.
   */
  reloadTab(tab, options = {}) {
    const finished = BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
      includeSubFrames: !!options.includeSubFrames,
    });
    if (options.bypassCache) {
      tab.linkedBrowser.reloadWithFlags(
        Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE
      );
    } else {
      // @backward-compat { version 152 }
      // Get rid of the documentGlobal fallback once 152 makes it to release.
      let win = tab.documentGlobal || tab.ownerGlobal;
      win.gBrowser.reloadTab(tab);
    }
    return finished;
  },

  /**
   * Create enough tabs to cause a tab overflow in the given window.
   *
   * @param {Function|null} registerCleanupFunction
   *    The test framework doesn't keep its cleanup stuff anywhere accessible,
   *    so the first argument is a reference to your cleanup registration
   *    function, allowing us to clean up after you if necessary. This can be
   *    null if you are using a temporary window for the test.
   * @param {Window} win
   *    The window where the tabs need to be overflowed.
   * @param {object} params [optional]
   *        Parameters object for BrowserTestUtils.overflowTabs.
   *        overflowAtStart: bool
   *          Determines whether the new tabs are added at the beginning of the
   *          URL bar or at the end of it.
   *        overflowTabFactor: 3 | 1.1
   *          Factor that helps in determining the tab count for overflow.
   */
  async overflowTabs(registerCleanupFunction, win, params = {}) {
    if (!params.hasOwnProperty("overflowAtStart")) {
      params.overflowAtStart = true;
    }
    if (!params.hasOwnProperty("overflowTabFactor")) {
      params.overflowTabFactor = 1.1;
    }
    let { gBrowser } = win;
    let overflowDirection = gBrowser.tabContainer.verticalMode
      ? "height"
      : "width";
    let tabIndex = params.overflowAtStart ? 0 : undefined;
    let arrowScrollbox = gBrowser.tabContainer.arrowScrollbox;
    if (arrowScrollbox.hasAttribute("overflowing")) {
      return;
    }
    let promises = [];
    promises.push(
      BrowserTestUtils.waitForEvent(
        arrowScrollbox,
        "overflow",
        false,
        e => e.target == arrowScrollbox
      )
    );
    const originalSmoothScroll = arrowScrollbox.smoothScroll;
    arrowScrollbox.smoothScroll = false;
    if (registerCleanupFunction) {
      registerCleanupFunction(() => {
        arrowScrollbox.smoothScroll = originalSmoothScroll;
      });
    }

    let size = ele => ele.getBoundingClientRect()[overflowDirection];
    let tabMinSize = gBrowser.tabContainer.verticalMode
      ? size(gBrowser.selectedTab)
      : parseInt(win.getComputedStyle(gBrowser.selectedTab).minWidth);
    let tabCountForOverflow = Math.ceil(
      (size(arrowScrollbox) / tabMinSize) * params.overflowTabFactor
    );
    while (gBrowser.tabs.length < tabCountForOverflow) {
      promises.push(
        BrowserTestUtils.addTab(gBrowser, "about:blank", {
          skipAnimation: true,
          tabIndex,
        })
      );
    }
    await Promise.all(promises);
  },

  /**
   * Crashes a remote frame tab and cleans up the generated minidumps.
   * Resolves with the data from the .extra file (the crash annotations).
   *
   * @param (Browser) browser
   *        A remote <xul:browser> element. Must not be null.
   * @param (bool) shouldShowTabCrashPage
   *        True if it is expected that the tab crashed page will be shown
   *        for this browser. If so, the Promise will only resolve once the
   *        tab crash page has loaded.
   * @param (bool) shouldClearMinidumps
   *        True if the minidumps left behind by the crash should be removed.
   * @param (BrowsingContext) browsingContext
   *        The context where the frame leaves. Default to
   *        top level context if not supplied.
   * @param (object?) options
   *        An object with any of the following fields:
   *          crashType: "CRASH_INVALID_POINTER_DEREF" | "CRASH_OOM" | "CRASH_SYSCALL"
   *            The type of crash. If unspecified, default to "CRASH_INVALID_POINTER_DEREF"
   *          asyncCrash: bool
   *            If specified and `true`, cause the crash asynchronously.
   *
   * @returns (Promise)
   *   An Object with key-value pairs representing the data from the crash
   *   report's extra file (if applicable).
   */
  async crashFrame(
    browser,
    shouldShowTabCrashPage = true,
    shouldClearMinidumps = true,
    browsingContext,
    options = {}
  ) {
    let extra = {};

    if (!browser.isRemoteBrowser) {
      throw new Error("<xul:browser> needs to be remote in order to crash");
    }

    /**
     * Returns the directory where crash dumps are stored.
     *
     * @return nsIFile
     */
    function getMinidumpDirectory() {
      let dir = Services.dirsvc.get("ProfD", Ci.nsIFile);
      dir.append("minidumps");
      return dir;
    }

    /**
     * Removes a file from a directory. This is a no-op if the file does not
     * exist.
     *
     * @param directory
     *        The nsIFile representing the directory to remove from.
     * @param filename
     *        A string for the file to remove from the directory.
     */
    function removeFile(directory, filename) {
      let file = directory.clone();
      file.append(filename);
      if (file.exists()) {
        file.remove(false);
      }
    }

    let expectedPromises = [];

    let crashCleanupPromise = new Promise((resolve, reject) => {
      let observer = (subject, topic) => {
        if (topic != "ipc:content-shutdown") {
          reject("Received incorrect observer topic: " + topic);
          return;
        }
        if (!(subject instanceof Ci.nsIPropertyBag2)) {
          reject("Subject did not implement nsIPropertyBag2");
          return;
        }
        // we might see this called as the process terminates due to previous tests.
        // We are only looking for "abnormal" exits...
        if (!subject.hasKey("abnormal")) {
          dump(
            "\nThis is a normal termination and isn't the one we are looking for...\n"
          );
          return;
        }

        Services.obs.removeObserver(observer, "ipc:content-shutdown");

        const dumpID = subject.get("dumpID");
        if (AppConstants.MOZ_CRASHREPORTER && !dumpID) {
          dump("dumpID was not present despite crash reporting being enabled");
        }

        let removalPromise = Promise.resolve();

        if (dumpID) {
          removalPromise = Services.crashmanager
            .ensureCrashIsPresent(dumpID)
            .then(async () => {
              let minidumpDirectory = getMinidumpDirectory();
              let extrafile = minidumpDirectory.clone();
              extrafile.append(dumpID + ".extra");
              if (extrafile.exists()) {
                if (AppConstants.MOZ_CRASHREPORTER) {
                  extra = await IOUtils.readJSON(extrafile.path);
                } else {
                  dump(
                    "\nCrashReporter not enabled - will not return any extra data\n"
                  );
                }
              } else {
                dump(`\nNo .extra file for dumpID: ${dumpID}\n`);
              }

              if (shouldClearMinidumps) {
                removeFile(minidumpDirectory, dumpID + ".dmp");
                removeFile(minidumpDirectory, dumpID + ".extra");
              }
            });
        }

        removalPromise.then(() => {
          dump("\nCrash cleaned up\n");
          // There might be other ipc:content-shutdown handlers that need to
          // run before we want to continue, so we'll resolve on the next tick
          // of the event loop.
          TestUtils.executeSoon(() => resolve());
        });
      };

      Services.obs.addObserver(observer, "ipc:content-shutdown");
    });

    expectedPromises.push(crashCleanupPromise);

    if (shouldShowTabCrashPage) {
      expectedPromises.push(
        new Promise(resolve => {
          browser.addEventListener(
            "AboutTabCrashedReady",
            function onCrash() {
              browser.removeEventListener("AboutTabCrashedReady", onCrash);
              dump("\nabout:tabcrashed loaded and ready\n");
              resolve();
            },
            false,
            true
          );
        })
      );
    }

    // Trigger crash by sending a message to BrowserTestUtils actor.
    this.sendAsyncMessage(
      browsingContext || browser.browsingContext,
      "BrowserTestUtils:CrashFrame",
      {
        crashType: options.crashType || "",
        asyncCrash: options.asyncCrash || false,
      }
    );

    await Promise.all(expectedPromises);

    if (shouldShowTabCrashPage) {
      let gBrowser = browser.documentGlobal.gBrowser;
      let tab = gBrowser.getTabForBrowser(browser);
      if (tab.getAttribute("crashed") != "true") {
        throw new Error("Tab should be marked as crashed");
      }
    }

    return extra;
  },

  /**
   * Attempts to simulate a launch fail by crashing a browser, but
   * stripping the browser of its childID so that the TabCrashHandler
   * thinks it was a launch fail.
   *
   * @param browser (<xul:browser>)
   *   The browser to simulate a content process launch failure on.
   * @return {Promise<void>}
   *   Resolves when the TabCrashHandler should be done handling the
   *   simulated crash.
   */
  simulateProcessLaunchFail(browser, dueToBuildIDMismatch = false) {
    const NORMAL_CRASH_TOPIC = "ipc:content-shutdown";

    Object.defineProperty(browser.frameLoader, "childID", {
      get: () => 0,
    });

    let sawNormalCrash = false;
    let observer = () => {
      sawNormalCrash = true;
    };

    Services.obs.addObserver(observer, NORMAL_CRASH_TOPIC);

    Services.obs.notifyObservers(
      browser.frameLoader,
      "oop-frameloader-crashed"
    );

    let eventType = dueToBuildIDMismatch
      ? "oop-browser-buildid-mismatch"
      : "oop-browser-crashed";

    let event = new browser.documentGlobal.CustomEvent(eventType, {
      bubbles: true,
    });
    event.isTopFrame = true;
    browser.dispatchEvent(event);

    Services.obs.removeObserver(observer, NORMAL_CRASH_TOPIC);

    if (sawNormalCrash) {
      throw new Error(`Unexpectedly saw ${NORMAL_CRASH_TOPIC}`);
    }

    return new Promise(resolve => TestUtils.executeSoon(resolve));
  },

  /**
   * Returns a promise that is resolved when element gains attribute (or,
   * optionally, when it is set to value).
   *
   * @param {string} attr
   *        The attribute to wait for
   * @param {Element} element
   *        The element which should gain the attribute
   * @param {string} value (optional)
   *        Optional, the value the attribute should have.
   *
   * @returns {Promise}
   */
  waitForAttribute(attr, element, value) {
    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = element.documentGlobal || element.ownerGlobal;
    let MutationObserver = win.MutationObserver;
    return new Promise(resolve => {
      let mut = new MutationObserver(() => {
        if (
          (!value && element.hasAttribute(attr)) ||
          (value && element.getAttribute(attr) === value)
        ) {
          resolve();
          mut.disconnect();
        }
      });

      mut.observe(element, { attributeFilter: [attr] });
    });
  },

  /**
   * Returns a promise that is resolved when element loses an attribute.
   *
   * @param {string} attr
   *        The attribute to wait for
   * @param {Element} element
   *        The element which should lose the attribute
   *
   * @returns {Promise}
   */
  waitForAttributeRemoval(attr, element) {
    if (!element.hasAttribute(attr)) {
      return Promise.resolve();
    }

    // @backward-compat { version 152 }
    // Get rid of the documentGlobal fallback once 152 makes it to release.
    let win = element.documentGlobal || element.ownerGlobal;
    let MutationObserver = win.MutationObserver;
    return new Promise(resolve => {
      dump("Waiting for removal\n");
      let mut = new MutationObserver(() => {
        if (!element.hasAttribute(attr)) {
          resolve();
          mut.disconnect();
        }
      });

      mut.observe(element, { attributeFilter: [attr] });
    });
  },

  /**
   * Version of EventUtils' `sendChar` function; it will synthesize a keypress
   * event in a child process and returns a Promise that will resolve when the
   * event was fired. Instead of a Window, a Browser or Browsing Context
   * is required to be passed to this function.
   *
   * @param {string} char
   *        A character for the keypress event that is sent to the browser.
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   *
   * @returns {Promise<boolean>}
   *   Resolves to true if the keypress event was synthesized.
   */
  sendChar(char, browsingContext) {
    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SendChar", { char });
  },

  /**
   * Version of EventUtils' `synthesizeKey` function; it will synthesize a key
   * event in a child process and returns a Promise that will resolve when the
   * event was fired. Instead of a Window, a Browser or Browsing Context
   * is required to be passed to this function.
   *
   * @param {string} key
   *        See the documentation available for EventUtils#synthesizeKey.
   * @param {object} event
   *        See the documentation available for EventUtils#synthesizeKey.
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   *
   * @returns {Promise}
   */
  synthesizeKey(key, event, browsingContext) {
    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SynthesizeKey", {
      key,
      event,
    });
  },

  /**
   * Version of EventUtils' `synthesizeComposition` function; it will synthesize
   * a composition event in a child process and returns a Promise that will
   * resolve when the event was fired. Instead of a Window, a Browser or
   * Browsing Context is required to be passed to this function.
   *
   * @param {object} event
   *        See the documentation available for EventUtils#synthesizeComposition.
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   *
   * @returns {Promise<boolean>}
   *   Resolves to false if the composition event could not be synthesized.
   */
  synthesizeComposition(event, browsingContext) {
    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SynthesizeComposition", {
      event,
    });
  },

  /**
   * Version of EventUtils' `synthesizeCompositionChange` function; it will
   * synthesize a compositionchange event in a child process and returns a
   * Promise that will resolve when the event was fired. Instead of a Window, a
   * Browser or Browsing Context object is required to be passed to this function.
   *
   * @param {object} event
   *        See the documentation available for EventUtils#synthesizeCompositionChange.
   * @param {BrowserContext|MozFrameLoaderOwner} browsingContext
   *        Browsing context or browser element, must not be null
   *
   * @returns {Promise}
   */
  synthesizeCompositionChange(event, browsingContext) {
    browsingContext = this.getBrowsingContextFrom(browsingContext);
    return this.sendQuery(browsingContext, "Test:SynthesizeCompositionChange", {
      event,
    });
  },

  // TODO: Fix consumers and remove me.
  waitForCondition: TestUtils.waitForCondition,

  /**
   * Waits for a <xul:notification> with a particular value to appear
   * for the <xul:notificationbox> of the passed in browser.
   *
   * @param {xul:tabbrowser} tabbrowser
   *        The gBrowser that hosts the browser that should show
   *        the notification. For most tests, this will probably be
   *        gBrowser.
   * @param {xul:browser} browser
   *        The browser that should be showing the notification.
   * @param {string} notificationValue
   *        The "value" of the notification, which is often used as
   *        a unique identifier. Example: "plugin-crashed".
   *
   * @return {Promise}
   *        Resolves to the <xul:notification> that is being shown.
   */
  waitForNotificationBar(tabbrowser, browser, notificationValue) {
    let notificationBox = tabbrowser.getNotificationBox(browser);
    return this.waitForNotificationInNotificationBox(
      notificationBox,
      notificationValue
    );
  },

  /**
   * Waits for a <xul:notification> with a particular value to appear
   * in the global <xul:notificationbox> of the given browser window.
   *
   * @param {Window} win
   *        The browser window in whose global notificationbox the
   *        notification is expected to appear.
   * @param {string} notificationValue
   *        The "value" of the notification, which is often used as
   *        a unique identifier. Example: "captive-portal-detected".
   *
   * @return {Promise}
   *        Resolves to the <xul:notification> that is being shown.
   */
  waitForGlobalNotificationBar(win, notificationValue) {
    return this.waitForNotificationInNotificationBox(
      win.gNotificationBox,
      notificationValue
    );
  },

  waitForNotificationInNotificationBox(notificationBox, notificationValue) {
    return new Promise(resolve => {
      let check = event => {
        return event.target.getAttribute("value") == notificationValue;
      };

      BrowserTestUtils.waitForEvent(
        notificationBox.stack,
        "AlertActive",
        false,
        check
      ).then(event => {
        // The originalTarget of the AlertActive on a notificationbox
        // will be the notification itself.
        resolve(event.originalTarget);
      });
    });
  },

  /**
   * Waits for CSS transitions to complete for an element. Tracks any
   * transitions that start after this function is called and resolves once all
   * started transitions complete.
   *
   * @param {Element} element
   *        The element that will transition.
   * @param {number} timeout
   *        The maximum time to wait in milliseconds. Defaults to 5 seconds.
   * @return {Promise}
   *        Resolves when transitions complete or rejects if the timeout is hit.
   */
  waitForTransition(element, timeout = 5000) {
    return new Promise((resolve, reject) => {
      let cleanup = () => {
        element.removeEventListener("transitionrun", listener);
        element.removeEventListener("transitionend", listener);
      };

      let timer = element.documentGlobal.setTimeout(() => {
        cleanup();
        reject();
      }, timeout);

      let transitionCount = 0;

      let listener = event => {
        if (event.type == "transitionrun") {
          transitionCount++;
        } else {
          transitionCount--;
          if (transitionCount == 0) {
            cleanup();
            element.documentGlobal.clearTimeout(timer);
            resolve();
          }
        }
      };

      element.addEventListener("transitionrun", listener);
      element.addEventListener("transitionend", listener);
      element.addEventListener("transitioncancel", listener);
    });
  },

  _knownAboutPages: new Set(),
  _loadedAboutContentScript: false,

  /**
   * Registers an about: page with particular flags in both the parent
   * and any content processes. Returns a promise that resolves when
   * registration is complete.
   *
   * @param {Function} registerCleanupFunction
   *        The test framework doesn't keep its cleanup stuff anywhere accessible,
   *        so the first argument is a reference to your cleanup registration
   *        function, allowing us to clean up after you if necessary.
   * @param {string} aboutModule
   *        The name of the about page.
   * @param {string} pageURI
   *        The URI the about: page should point to.
   * @param {number} flags
   *        The nsIAboutModule flags to use for registration.
   *
   * @returns {Promise}
   *        Promise that resolves when registration has finished.
   */
  registerAboutPage(registerCleanupFunction, aboutModule, pageURI, flags) {
    // Return a promise that resolves when registration finished.
    const kRegistrationMsgId =
      "browser-test-utils:about-registration:registered";
    let rv = this.waitForMessage(Services.ppmm, kRegistrationMsgId, msg => {
      return msg.data == aboutModule;
    });
    // Load a script that registers our page, then send it a message to execute the registration.
    if (!this._loadedAboutContentScript) {
      Services.ppmm.loadProcessScript(
        kAboutPageRegistrationContentScript,
        true
      );
      this._loadedAboutContentScript = true;
      registerCleanupFunction(this._removeAboutPageRegistrations.bind(this));
    }
    Services.ppmm.broadcastAsyncMessage(
      "browser-test-utils:about-registration:register",
      { aboutModule, pageURI, flags }
    );
    return rv.then(() => {
      this._knownAboutPages.add(aboutModule);
    });
  },

  unregisterAboutPage(aboutModule) {
    if (!this._knownAboutPages.has(aboutModule)) {
      return Promise.reject(
        new Error("We don't think this about page exists!")
      );
    }
    const kUnregistrationMsgId =
      "browser-test-utils:about-registration:unregistered";
    let rv = this.waitForMessage(Services.ppmm, kUnregistrationMsgId, msg => {
      return msg.data == aboutModule;
    });
    Services.ppmm.broadcastAsyncMessage(
      "browser-test-utils:about-registration:unregister",
      aboutModule
    );
    return rv.then(() => this._knownAboutPages.delete(aboutModule));
  },

  async _removeAboutPageRegistrations() {
    for (let aboutModule of this._knownAboutPages) {
      await this.unregisterAboutPage(aboutModule);
    }
    Services.ppmm.removeDelayedProcessScript(
      kAboutPageRegistrationContentScript
    );
  },

  /**
   * Waits for the dialog to open, and clicks the specified button.
   *
   * @param {string} buttonNameOrElementID
   *        The name of the button ("accept", "cancel", etc) or element ID to
   *        click.
   * @param {string} uri
   *        The URI of the dialog to wait for.  Defaults to the common dialog.
   * @return {Promise}
   *         A Promise which resolves when a "domwindowopened" notification
   *         for a dialog has been fired by the window watcher and the
   *         specified button is clicked.
   */
  async promiseAlertDialogOpen(
    buttonNameOrElementID,
    uri = "chrome://global/content/commonDialog.xhtml",
    options = { callback: null, isSubDialog: false }
  ) {
    let win;
    if (uri == "chrome://global/content/commonDialog.xhtml") {
      [win] = await TestUtils.topicObserved("common-dialog-loaded");
    } else if (options.isSubDialog) {
      for (let attempts = 0; attempts < 3; attempts++) {
        [win] = await TestUtils.topicObserved("subdialog-loaded");
        if (uri === undefined || uri === null || uri === "") {
          break;
        }
        if (win.document.documentURI === uri) {
          break;
        }
      }
    } else {
      // The test listens for the "load" event which guarantees that the alert
      // class has already been added (it is added when "DOMContentLoaded" is
      // fired).
      win = await this.domWindowOpenedAndLoaded(null, win => {
        return win.document.documentURI === uri;
      });
    }

    if (options.callback) {
      await options.callback(win);
      return win;
    }

    if (buttonNameOrElementID) {
      let dialog = win.document.querySelector("dialog");
      let element =
        dialog.getButton(buttonNameOrElementID) ||
        win.document.getElementById(buttonNameOrElementID);
      element.click();
    }

    return win;
  },

  /**
   * Wait for the containing dialog with the id `window-modal-dialog` to become
   * empty and close.
   *
   * @param  {HTMLDialogElement} dialog
   *           The dialog to wait on.
   * @return {Promise}
   *           Resolves once the the dialog has closed
   */
  async waitForDialogClose(dialog) {
    return this.waitForEvent(dialog, "close").then(() => {
      return this.waitForMutationCondition(
        dialog,
        { childList: true, attributes: true },
        () => !dialog.hasChildNodes() && !dialog.open
      );
    });
  },

  /**
   * Waits for the dialog to open, and clicks the specified button, and waits
   * for the dialog to close.
   *
   * @param {string} buttonNameOrElementID
   *        The name of the button ("accept", "cancel", etc) or element ID to
   *        click.
   * @param {string} uri
   *        The URI of the dialog to wait for.  Defaults to the common dialog.
   *
   * @return {Promise}
   *         A Promise which resolves when a "domwindowopened" notification
   *         for a dialog has been fired by the window watcher and the
   *         specified button is clicked, and the dialog has been fully closed.
   */
  async promiseAlertDialog(
    buttonNameOrElementID,
    uri = "chrome://global/content/commonDialog.xhtml",
    options = { callback: null, isSubDialog: false }
  ) {
    let win = await this.promiseAlertDialogOpen(
      buttonNameOrElementID,
      uri,
      options
    );
    if (!win.docShell.browsingContext.embedderElement) {
      return this.windowClosed(win);
    }
    const dialog = win.top.document.getElementById("window-modal-dialog");
    return this.waitForDialogClose(dialog);
  },

  /**
   * Opens a tab with a given uri and params object. If the params object is not set
   * or the params parameter does not include a triggeringPrincipal then this function
   * provides a params object using the systemPrincipal as the default triggeringPrincipal.
   *
   * @param {xul:tabbrowser} tabbrowser
   *        The gBrowser object to open the tab with.
   * @param {string} uri
   *        The URI to open in the new tab.
   * @param {object} params [optional]
   *        Parameters object for gBrowser.addTab.
   * @param {function} beforeLoadFunc [optional]
   *        A function to run after that xul:browser has been created but before the URL is
   *        loaded. Can spawn a content task in the tab, for example.
   */
  addTab(tabbrowser, uri, params = {}, beforeLoadFunc = null) {
    if (!params.triggeringPrincipal) {
      params.triggeringPrincipal =
        Services.scriptSecurityManager.getSystemPrincipal();
    }
    if (beforeLoadFunc) {
      // @backward-compat { version 152 }
      // Get rid of the documentGlobal fallback once 152 makes it to release.
      let win = tabbrowser.documentGlobal || tabbrowser.ownerGlobal;
      win.addEventListener(
        "TabOpen",
        function (e) {
          beforeLoadFunc(e.target);
        },
        { once: true }
      );
    }
    return tabbrowser.addTab(uri, params);
  },

  /**
   * There are two ways to listen for observers in a content process:
   *   1. Call contentTopicObserved which will watch for an observer notification
   *      in a content process to occur, and will return a promise which resolves
   *      when that notification occurs.
   *   2. Enclose calls to contentTopicObserved inside a pair of calls to
   *      startObservingTopics and stopObservingTopics. Usually this pair will be
   *      placed at the start and end of a test or set of tests. Any observer
   *      notification that happens between the start and stop that doesn't match
   *      any explicitly expected by using contentTopicObserved will cause
   *      stopObservingTopics to reject with an error.
   *      For example:
   *
   *        await BrowserTestUtils.startObservingTopics(bc, ["a", "b", "c"]);
   *        await BrowserTestUtils contentTopicObserved(bc, "a", 2);
   *        await BrowserTestUtils.stopObservingTopics(bc, ["a", "b", "c"]);
   *
   *      This will expect two "a" notifications to occur, but will fail if more
   *      than two occur, or if any "b" or "c" notifications occur.
   *
   * Note that this function doesn't handle adding a listener for the same topic
   * more than once. To do that, use the aCount argument.
   *
   * @param aBrowsingContext
   *        The browsing context associated with the content process to listen to.
   * @param {string} aTopic
   *        Observer topic to listen to. May be null to listen to any topic.
   * @param {number} aCount
   *        Number of such matching topics to listen to, defaults to 1. A match
   *        occurs when the topic and filter function match.
   * @param {function} aFilterFn
   *        Function to be evaluated in the content process which should
   *        return true if the notification matches. This function is passed
   *        the same arguments as nsIObserver.observe(). May be null to
   *        always match.
   * @returns {Promise} resolves when the notification occurs.
   */
  contentTopicObserved(aBrowsingContext, aTopic, aCount = 1, aFilterFn = null) {
    return this.sendQuery(aBrowsingContext, "BrowserTestUtils:ObserveTopic", {
      topic: aTopic,
      count: aCount,
      filterFunctionSource: aFilterFn ? aFilterFn.toSource() : null,
    });
  },

  /**
   * Starts observing a list of topics in a content process. Use contentTopicObserved
   * to allow an observer notification. Any other observer notification that occurs that
   * matches one of the specified topics will cause the promise to reject.
   *
   * Calling this function more than once adds additional topics to be observed without
   * replacing the existing ones.
   *
   * @param {BrowsingContext} aBrowsingContext
   *        The browsing context associated with the content process to listen to.
   * @param {string[]} aTopics array of observer topics
   * @returns {Promise} resolves when the listeners have been added.
   */
  startObservingTopics(aBrowsingContext, aTopics) {
    return this.sendQuery(
      aBrowsingContext,
      "BrowserTestUtils:StartObservingTopics",
      {
        topics: aTopics,
      }
    );
  },

  /**
   * Stop listening to a set of observer topics.
   *
   * @param {BrowsingContext} aBrowsingContext
   *        The browsing context associated with the content process to listen to.
   * @param {string[]} aTopics array of observer topics. If empty, then all
   *                           current topics being listened to are removed.
   * @returns {Promise} promise that fails if an unexpected observer occurs.
   */
  stopObservingTopics(aBrowsingContext, aTopics) {
    return this.sendQuery(
      aBrowsingContext,
      "BrowserTestUtils:StopObservingTopics",
      {
        topics: aTopics,
      }
    );
  },

  /**
   * Sends a message to a specific BrowserTestUtils window actor.
   *
   * @param {BrowsingContext} aBrowsingContext
   *        The browsing context where the actor lives.
   * @param {string} aMessageName
   *        Name of the message to be sent to the actor.
   * @param {object} aMessageData
   *        Extra information to pass to the actor.
   */
  async sendAsyncMessage(aBrowsingContext, aMessageName, aMessageData) {
    if (!aBrowsingContext.currentWindowGlobal) {
      await this.waitForCondition(() => aBrowsingContext.currentWindowGlobal);
    }

    let actor =
      aBrowsingContext.currentWindowGlobal.getActor("BrowserTestUtils");
    actor.sendAsyncMessage(aMessageName, aMessageData);
  },

  /**
   * Sends a query to a specific BrowserTestUtils window actor.
   *
   * @param {BrowsingContext} aBrowsingContext
   *        The browsing context where the actor lives.
   * @param {string} aMessageName
   *        Name of the message to be sent to the actor.
   * @param {object} aMessageData
   *        Extra information to pass to the actor.
   */
  async sendQuery(aBrowsingContext, aMessageName, aMessageData) {
    let startTime = ChromeUtils.now();
    if (!aBrowsingContext.currentWindowGlobal) {
      await this.waitForCondition(() => aBrowsingContext.currentWindowGlobal);
    }

    let actor =
      aBrowsingContext.currentWindowGlobal.getActor("BrowserTestUtils");
    return actor.sendQuery(aMessageName, aMessageData).then(val => {
      ChromeUtils.addProfilerMarker(
        "BrowserTestUtils",
        { startTime, category: "Test" },
        aMessageName
      );
      return val;
    });
  },

  /**
   * A helper function for this test that returns a Promise that resolves
   * once the migration wizard appears.
   *
   * @param {DOMWindow} window
   *   The top-level window that the about:preferences tab is likely to open
   *   in if the new migration wizard is enabled.
   * @returns {Promise<Element>}
   *   Resolves to the opened about:preferences tab with the migration wizard
   *   running and loaded in it.
   */
  async waitForMigrationWizard(window) {
    let wizardReady = this.waitForEvent(window, "MigrationWizard:Ready");
    let wizardTab = await this.waitForNewTab(window.gBrowser, url => {
      return url.startsWith("about:preferences");
    });
    await wizardReady;

    return wizardTab;
  },

  /**
   * Run a query selector that pierces into open and closed Shadow DOM roots.
   *
   * @param {Document | ShadowRoot | Element} root
   * @param {string} selector
   * @returns {Element | null}
   */
  querySelectorDeep(root, selector) {
    if (!root) {
      return null;
    }

    const direct = root.querySelector?.(selector);
    if (direct) {
      return direct;
    }

    const doc = root.ownerDocument ?? root;
    const treeWalker = doc.createTreeWalker(root, NodeFilter.SHOW_ELEMENT);

    // Walk child elements to find other shadow roots.
    let current = treeWalker.currentNode;
    while (current) {
      const shadow = current.openOrClosedShadowRoot;
      if (shadow) {
        const match = BrowserTestUtils.querySelectorDeep(shadow, selector);
        if (match) {
          return match;
        }
      }
      current = treeWalker.nextNode();
    }

    return null;
  },

  /**
   * When calling this function, the window will be hidden from various APIs,
   * so that they won't be able to find it.
   *
   * This makes it possible to hide the main window to test some behaviors when
   * it doesn't exist, e.g. when only private or non-browser windows exist.
   *
   * @param {ChromeWindow} window The window to be concealed.
   * @param {object} options
   * @param {AbortSignal} options.signal
   *        Unconceals the window when the signal aborts.
   */
  concealWindow(window, { signal }) {
    let oldWinType = window.document.documentElement.getAttribute("windowtype");
    // Check if we've already done this to allow calling multiple times:
    if (oldWinType != "navigator:testrunner") {
      // Make the main test window not count as a browser window any longer
      window.document.documentElement.setAttribute(
        "windowtype",
        "navigator:testrunner"
      );
      lazy.BrowserWindowTracker.untrackForTestsOnly(window);

      signal.addEventListener("abort", () => {
        lazy.BrowserWindowTracker.track(window);
        window.document.documentElement.setAttribute("windowtype", oldWinType);
      });
    }
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  BrowserTestUtils,
  "_httpsFirstEnabled",
  "dom.security.https_first",
  false
);

Services.obs.addObserver(BrowserTestUtils, "test-complete");
