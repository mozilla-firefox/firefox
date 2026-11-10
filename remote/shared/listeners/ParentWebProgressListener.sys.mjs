/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowsingContextListener:
    "chrome://remote/content/shared/listeners/BrowsingContextListener.sys.mjs",
  isUncommittedInitialDocument:
    "chrome://remote/content/shared/BrowsingContextUtils.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  notifyFragmentNavigated:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  notifyHistoryUpdated:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  notifySameDocumentChanged:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  notifyNavigationFailed:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  notifyNavigationStarted:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  notifyNavigationStopped:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

/**
 * The ParentWebProgressListener is a listener that supports monitoring
 * navigations for the NavigationManager entirely from the parent process.
 *
 * This listener does not implement the same interface as our other listeners
 * and is designed to be instantiated only once from the NavigationRegistry
 * singleton.
 *
 * Bug 1984098: Fold this "listener" in the NavigationManager.
 */
export class ParentWebProgressListener {
  #contextListener;
  #listener;
  #listening;
  #monitoredWebProgress;

  constructor() {
    this.#monitoredWebProgress = new Map();

    this.#contextListener = new lazy.BrowsingContextListener();
    this.#contextListener.on("attached", this.#onContextAttached);
    this.#contextListener.on("discarded", this.#onContextDiscarded);

    this.#listener = {
      onLocationChange: this.#onLocationChange,
      onStateChange: this.#onStateChange,
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
    };
  }

  get listening() {
    return this.#listening;
  }

  destroy() {
    this.stopListening();
    this.#contextListener.destroy();
    this.#monitoredWebProgress = new Map();
  }

  #onLocationChange = (progress, request, location, flags) => {
    if (flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      const context = progress.browsingContext;

      const payload = {
        contextDetails: { context },
        url: location.spec,
      };

      if (
        // history.pushState / replaceState / document.open
        progress.loadType & Ci.nsIDocShell.LOAD_CMD_PUSHSTATE ||
        // history.go / back / forward to an entry created by pushState / replaceState
        (progress.loadType & Ci.nsIDocShell.LOAD_CMD_HISTORY &&
          // Bug 1969943: We need to only select history traversals which are not
          // fragment navigations. However we don't have a flag dedicated to
          // such traversals, they are identical to same document + same hash
          // navigations.
          flags === Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT)
      ) {
        this.#trace(
          lazy.truncate`Location=historyUpdated: ${location.spec}`,
          context.id
        );
        lazy.notifyHistoryUpdated(payload);
        return;
      }

      if (location.hasRef) {
        // If the target URL contains a hash, handle the navigation as a
        // fragment navigation.
        this.#trace(
          lazy.truncate`Location=fragmentNavigated: ${location.spec}`,
          context.id
        );

        lazy.notifyFragmentNavigated(payload);
        return;
      }

      this.#trace(
        lazy.truncate`Location=sameDocumentChanged: ${location.spec}`,
        context.id
      );

      lazy.notifySameDocumentChanged(payload);
    }
  };

  #onStateChange = (progress, request, stateFlags, status) => {
    const context = progress.browsingContext;
    const targetURI = this.#getTargetURI(request);

    const isBindingAborted = status == Cr.NS_BINDING_ABORTED;
    const isStart = !!(stateFlags & Ci.nsIWebProgressListener.STATE_START);
    const isStop = !!(stateFlags & Ci.nsIWebProgressListener.STATE_STOP);

    if (lazy.Log.isTraceLevelOrMore) {
      const isNetwork = !!(
        stateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK
      );
      this.#trace(
        `Loading state: flags: ${stateFlags}, status: ${status}, ` +
          ` isStart: ${isStart}, isStop: ${isStop}, isNetwork: ${isNetwork},` +
          ` isBindingAborted: ${isBindingAborted},` +
          lazy.truncate` targetURI: ${targetURI?.spec}`,
        context.id
      );
    }

    const url = targetURI?.spec;

    const isUncommittedInitialDocument =
      lazy.isUncommittedInitialDocument(context);
    if (isUncommittedInitialDocument && url === "about:blank") {
      this.#trace("Skip initial navigation to about:blank", context.id);
      return;
    }

    try {
      if (isStart) {
        lazy.notifyNavigationStarted({
          contextDetails: { context },
          url,
        });

        return;
      }

      if (isStop && !isBindingAborted) {
        const errorName = ChromeUtils.getXPCOMErrorName(status);
        if (this.#isContentBlocked(errorName)) {
          lazy.notifyNavigationFailed({
            contextDetails: { context },
            errorName,
            status,
            url,
          });
        } else {
          lazy.notifyNavigationStopped({
            contextDetails: { context },
            status,
            url,
          });
        }
      }
    } catch (e) {
      if (e.name === "InvalidStateError") {
        // We'll arrive here if we no longer have our manager, so we can
        // just swallow this error.
        return;
      }
      throw e;
    }
  };

  startListening() {
    if (this.#listening) {
      return;
    }

    this.#contextListener.startListening();
    // Start listening for navigation on all existing contexts.
    this.#getAllBrowsingContexts().forEach(browsingContext =>
      this.#startWatchingBrowsingContextNavigation(browsingContext)
    );

    this.#listening = true;
  }

  stopListening() {
    if (!this.#listening) {
      return;
    }

    this.#contextListener.stopListening();
    for (const webProgress of this.#monitoredWebProgress.keys()) {
      try {
        webProgress.removeProgressListener(this.#listener);
      } catch (e) {
        this.#trace(`Failed to remove the progress listener`);
      }
    }
    this.#monitoredWebProgress = new Map();

    this.#listening = false;
  }

  #getAllBrowsingContexts() {
    return lazy.TabManager.getBrowsers().flatMap(browser =>
      browser.browsingContext.getAllBrowsingContextsInSubtree()
    );
  }

  #getTargetURI(request) {
    try {
      return request.QueryInterface(Ci.nsIChannel).originalURI;
    } catch (e) {}

    return null;
  }

  #isContentBlocked(blockedReason) {
    return [
      // If a navigation was blocked because of the Connection-Allowlist header.
      "NS_ERROR_CONNECTION_ALLOWLIST_VIOLATION",
      // If content is blocked with e.g. CSP meta tag.
      "NS_ERROR_CONTENT_BLOCKED",
      // If a resource load was blocked because of the CSP header.
      "NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION",
      // If a resource load was blocked because of the Cross-Origin-Embedder-Policy header.
      "NS_ERROR_DOM_COEP_FAILED",
      // If a resource load was blocked because of the X-Frame-Options header.
      "NS_ERROR_XFO_VIOLATION",
    ].includes(blockedReason);
  }

  #onContextAttached = async (eventName, data) => {
    const { browsingContext } = data;

    if (browsingContext.isContent) {
      this.#startWatchingBrowsingContextNavigation(browsingContext);
    }
  };

  #onContextDiscarded = async (eventName, data = {}) => {
    const { browsingContext } = data;

    if (browsingContext.isContent) {
      this.#stopWatchingBrowsingContextNavigation(browsingContext);
    }
  };

  #startWatchingBrowsingContextNavigation(browsingContext) {
    if (browsingContext.parent) {
      // Frame contexts will be monitored through the webprogress listener of
      // the top window.
      return;
    }

    this.#trace(
      `Start watching updates for browsing context`,
      browsingContext.id
    );

    const webProgress = browsingContext.webProgress;
    if (!webProgress) {
      this.#trace(
        `No web progress attached to this browsing context, bailing out`,
        browsingContext.id
      );
      return;
    }

    if (!this.#monitoredWebProgress.has(webProgress)) {
      this.#trace(
        `The web progress was not monitored yet, adding a progress listener`,
        browsingContext.id
      );
      this.#monitoredWebProgress.set(webProgress, new Set());
      webProgress.addProgressListener(
        this.#listener,
        Ci.nsIWebProgress.NOTIFY_STATE_WINDOW |
          Ci.nsIWebProgress.NOTIFY_LOCATION
      );
    }

    this.#monitoredWebProgress.get(webProgress).add(browsingContext);
  }

  #stopWatchingBrowsingContextNavigation(browsingContext) {
    if (browsingContext.parent) {
      // Frame contexts will be monitored through the webprogress listener of
      // the top window.
      return;
    }

    this.#trace(
      `Stop watching updates for browsing context`,
      browsingContext.id
    );

    const webProgress = browsingContext.webProgress;
    if (!webProgress) {
      this.#trace(
        `No web progress attached to this browsing context, bailing out`,
        browsingContext.id
      );
      return;
    }

    const contexts = this.#monitoredWebProgress.get(webProgress);
    if (!contexts) {
      this.#trace(
        `No browsing context tracked for the web progress, bailing out`,
        browsingContext.id
      );
      return;
    }

    contexts.delete(browsingContext);
    if (!contexts.size) {
      this.#trace(
        `All browsing contexts for this web progress deleted, removing the progress listener`,
        browsingContext.id
      );
      try {
        webProgress.removeProgressListener(this.#listener);
      } catch (e) {
        this.#trace(
          `Failed to remove the progress listener`,
          browsingContext.id
        );
      }

      this.#trace(
        `Removing the web progress from monitored web progress`,
        browsingContext.id
      );
      this.#monitoredWebProgress.delete(webProgress);
    }
  }

  #trace(message, contextId = null) {
    if (contextId !== null) {
      lazy.logger.trace(
        `${this.constructor.name} ${message} [context=${contextId}]`
      );
    } else {
      lazy.logger.trace(`${this.constructor.name} ${message}`);
    }
  }
}
