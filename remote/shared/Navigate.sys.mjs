/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",

  Deferred: "chrome://remote/content/shared/Sync.sys.mjs",
  isUncommittedInitialDocument:
    "chrome://remote/content/shared/BrowsingContextUtils.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  NavigationListener:
    "chrome://remote/content/shared/listeners/NavigationListener.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
  NavigationError: "chrome://remote/content/shared/RemoteError.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.REMOTE_AGENT)
);

// Define a custom multiplier to apply to the unload timer on various platforms.
// This multiplier should only reflect the navigation performance of the
// platform and not the overall performance.
ChromeUtils.defineLazyGetter(lazy, "UNLOAD_TIMEOUT_MULTIPLIER", () => {
  if (AppConstants.MOZ_CODE_COVERAGE) {
    // Navigation on ccov platforms can be extremely slow because new processes
    // need to be instrumented for coverage on startup.
    return 16;
  }

  if (AppConstants.ASAN || AppConstants.DEBUG || AppConstants.TSAN) {
    // Use an extended timeout on slow platforms.
    return 8;
  }

  return 1;
});

export const DEFAULT_UNLOAD_TIMEOUT = 200;

// Load flag for an error page from the DocShell (0x0001U << 16)
const LOAD_FLAG_ERROR_PAGE = 0x10000;

const STATE_START = Ci.nsIWebProgressListener.STATE_START;
const STATE_STOP = Ci.nsIWebProgressListener.STATE_STOP;

/**
 * Returns the multiplier used for the unload timer. Useful for tests which
 * assert the behavior of this timeout.
 */
export function getUnloadTimeoutMultiplier() {
  return lazy.UNLOAD_TIMEOUT_MULTIPLIER;
}

// Used to keep weak references of webProgressListeners alive.
const webProgressListeners = new Set();

/**
 * Wait until the initial load of the given WebProgress is done.
 *
 * @param {WebProgress} webProgress
 *     The WebProgress instance to observe.
 * @param {object=} options
 * @param {boolean=} options.resolveWhenStarted
 *     Flag to indicate that the Promise has to be resolved when the
 *     page load has been started. Otherwise wait until the page has
 *     finished loading. Defaults to `false`.
 * @param {number=} options.unloadTimeout
 *     Time to allow before the page gets unloaded. See ProgressListener options.
 * @returns {Promise}
 *     Promise which resolves when the page load is in the expected state.
 *     Values as returned:
 *       - {nsIURI} currentURI The current URI of the page
 *       - {nsIURI} targetURI Target URI of the navigation
 */
export async function waitForInitialNavigationCompleted(
  webProgress,
  options = {}
) {
  const { resolveWhenStarted = false, unloadTimeout } = options;

  const browsingContext = webProgress.browsingContext;

  // Start the listener right away to avoid race conditions.
  const listener = new ProgressListener(webProgress, {
    resolveWhenStarted,
    unloadTimeout,
  });
  const navigated = listener.start();

  const isUncommittedInitial =
    lazy.isUncommittedInitialDocument(browsingContext);
  const isLoadingDocument = listener.isLoadingDocument;
  lazy.logger.trace(
    lazy.truncate`[${browsingContext.id}] Wait for initial navigation: isUncommittedInitial=${isUncommittedInitial}, isLoadingDocument=${isLoadingDocument}`
  );

  // If the current document is not the initial "about:blank" and is also
  // no longer loading, assume the navigation is done and return.
  if (!isUncommittedInitial && !isLoadingDocument) {
    lazy.logger.trace(
      lazy.truncate`[${browsingContext.id}] Document already finished loading: ${browsingContext.currentURI?.spec}`
    );

    // Will resolve the navigated promise.
    listener.stop();
  }

  try {
    await navigated;
  } catch (e) {
    // Ignore any error if the initial navigation failed.
    lazy.logger.debug(
      lazy.truncate`[${browsingContext.id}] Initial Navigation to ${listener.currentURI?.spec} failed: ${e}`
    );
  }

  const result = {
    currentURI: listener.currentURI,
    targetURI: listener.targetURI,
  };

  listener.destroy();

  return result;
}

/**
 * WebProgressListener to observe for page loads.
 */
export class ProgressListener {
  #expectNavigation;
  #resolveWhenCommitted;
  #resolveWhenCommittedError;
  #resolveWhenStarted;
  #unloadTimeout;
  #waitForExplicitStart;
  #webProgress;

  #deferredNavigation;
  #errorName;
  #errorStatus;
  #ignoredLoadURI;
  #navigationId;
  #navigationListener;
  #seenNavigationCommitted;
  #seenStartFlag;
  #targetURI;
  #unloadTimerId;

  /**
   * Create a new WebProgressListener instance.
   *
   * @param {WebProgress} webProgress
   *     The web progress to attach the listener to.
   * @param {object=} options
   * @param {boolean=} options.expectNavigation
   *     Flag to indicate that a navigation is guaranteed to happen.
   *     When set to `true`, the ProgressListener will ignore options.unloadTimeout
   *     and will only resolve when the expected navigation happens.
   *     Defaults to `false`.
   * @param {NavigationManager=} options.navigationManager
   *     The NavigationManager where navigations for the current session are
   *     monitored.
   * @param {boolean=} options.resolveWhenCommitted
   *     Flag to indicate that the Promise has to be resolved when the
   *     navigation-committed event is received. Defaults to `false`.
   *     Cannot be used together with resolveWhenStarted. Requires to provide
   *     options.navigationManager.
   * @param {boolean=} options.resolveWhenStarted
   *     Flag to indicate that the Promise has to be resolved when the
   *     page load has been started. Otherwise wait until the navigation was
   *     committed or the page has finished loading. Defaults to `false`.
   *     Cannot be used together with resolveWhenCommitted.
   * @param {string=} options.targetURI
   *     The target URI for the navigation.
   * @param {number=} options.unloadTimeout
   *     Time to allow before the page gets unloaded. Defaults to 200ms on
   *     regular platforms. A multiplier will be applied on slower platforms
   *     (eg. debug, ccov...).
   *     Ignored if options.expectNavigation is set to `true`
   * @param {boolean=} options.waitForExplicitStart
   *     Flag to indicate that the Promise can only resolve after receiving a
   *     STATE_START state change. In other words, if the webProgress is already
   *     navigating, the Promise will only resolve for the next navigation.
   *     Defaults to `false`.
   */
  constructor(webProgress, options = {}) {
    const {
      expectNavigation = false,
      navigationManager = null,
      resolveWhenCommitted = false,
      resolveWhenStarted = false,
      targetURI,
      unloadTimeout = DEFAULT_UNLOAD_TIMEOUT,
      waitForExplicitStart = false,
    } = options;

    this.#expectNavigation = expectNavigation;
    this.#resolveWhenCommitted = resolveWhenCommitted;
    this.#resolveWhenStarted = resolveWhenStarted;
    this.#unloadTimeout = unloadTimeout * lazy.UNLOAD_TIMEOUT_MULTIPLIER;
    this.#waitForExplicitStart = waitForExplicitStart;
    this.#webProgress = webProgress;

    this.#deferredNavigation = null;
    this.#errorName = null;
    this.#errorStatus = null;
    this.#resolveWhenCommittedError = null;
    this.#seenNavigationCommitted = false;
    this.#seenStartFlag = false;
    this.#targetURI = targetURI;
    this.#unloadTimerId = null;

    if (resolveWhenCommitted) {
      if (resolveWhenStarted) {
        throw new Error(
          "Cannot use both resolveWhenStarted and resolveWhenCommitted"
        );
      }
      if (!navigationManager) {
        throw new Error(
          "Cannot use resolveWhenCommitted without a navigationManager"
        );
      }
    }

    if (navigationManager !== null) {
      this.#navigationListener = new lazy.NavigationListener(navigationManager);
      this.#navigationListener.on(
        "navigation-committed",
        this.#onNavigationCommitted
      );
      this.#navigationListener.on(
        "navigation-failed",
        this.#onNavigationFailed
      );
      this.#navigationListener.startListening();
    }
  }

  destroy() {
    if (this.#navigationListener) {
      this.#navigationListener.stopListening();
      this.#navigationListener.off(
        "navigation-committed",
        this.#onNavigationCommitted
      );
      this.#navigationListener.off(
        "navigation-failed",
        this.#onNavigationFailed
      );
      this.#navigationListener.destroy();
    }
  }

  get #messagePrefix() {
    return `[${this.browsingContext.id}] ${this.constructor.name}`;
  }

  get browsingContext() {
    return this.#webProgress.browsingContext;
  }

  get currentURI() {
    return this.#webProgress.browsingContext.currentURI;
  }

  get documentURI() {
    return this.#webProgress.browsingContext.currentWindowGlobal.documentURI;
  }

  get isLoadingDocument() {
    return this.#webProgress.isLoadingDocument;
  }

  get isStarted() {
    return !!this.#deferredNavigation;
  }

  get loadType() {
    return this.#webProgress.loadType;
  }

  get targetURI() {
    return this.#targetURI;
  }

  #checkLoadingState(request, options = {}) {
    const { isStart = false, isStop = false, status = 0 } = options;

    this.#trace(
      `Loading state: isStart=${isStart} isStop=${isStop} status=0x${status.toString(
        16
      )}, loadType=0x${this.loadType.toString(16)}, seenStartFlag=${this.#seenStartFlag}`
    );
    if (isStart) {
      if (this.#seenStartFlag) {
        this.#trace("Skip start state because seenStartFlag is already set");
      } else {
        this.#seenStartFlag = true;

        this.#targetURI = this.#getTargetURI(request);

        this.#trace(lazy.truncate`Started loading ${this.targetURI?.spec}`);

        this.#clearUnloadTimer();

        if (this.#resolveWhenStarted) {
          this.#trace("Request to stop listening when navigation started");
          this.stop();
          return;
        }
      }
    }

    if (isStop) {
      if (!this.#seenStartFlag) {
        // While a document is still loading, the STATE_START of a new load
        // can be suppressed by the web progress. Accept the stop state if it
        // belongs to a different load than the one already in progress.
        const uri = this.#getTargetURI(request);
        if (!this.#ignoredLoadURI || !uri || uri.equals(this.#ignoredLoadURI)) {
          this.#trace(
            lazy.truncate`Skip stop state without start state for ${uri?.spec} (ignored load: ${this.#ignoredLoadURI?.spec})`
          );
          return;
        }

        this.#trace(
          lazy.truncate`Stop state without start state for ${uri.spec}`
        );
        this.#seenStartFlag = true;
        this.#targetURI = uri;
        this.#clearUnloadTimer();
      }

      // Treat NS_ERROR_PARSED_DATA_CACHED as a success code
      // since navigation happened and content has been loaded.
      if (
        !Components.isSuccessCode(status) &&
        status != Cr.NS_ERROR_PARSED_DATA_CACHED
      ) {
        const errorName = ChromeUtils.getXPCOMErrorName(status);

        if (this.loadType & LOAD_FLAG_ERROR_PAGE) {
          // Wait for the next location change notification to ensure that the
          // real error page was loaded.
          this.#trace(`Error=${errorName}, wait for redirect to error page`);
          this.#seenNavigationCommitted = false;
          this.#errorName = errorName;
          this.#errorStatus = status;
          return;
        }

        this.stop({ error: new lazy.NavigationError(errorName, status) });
        return;
      }

      // If a page finished loading the navigation is done.
      this.stop();
    }
  }

  #clearUnloadTimer() {
    if (this.#unloadTimerId !== null) {
      lazy.clearTimeout(this.#unloadTimerId);
      this.#trace("Cleared the unload timer");
      this.#unloadTimerId = null;
    }
  }

  #getErrorName(documentURI) {
    try {
      // Otherwise try to retrieve it from the document URI if it is an
      // error page like `about:neterror?e=contentEncodingError&u=http%3A//...`
      const regex = /about:[^?]*\?e=([^&]*)/;
      return documentURI.spec.match(regex)[1];
    } catch (e) {
      // Or return a generic name
      return "Address rejected";
    }
  }

  #getTargetURI(request) {
    try {
      return request.QueryInterface(Ci.nsIChannel).originalURI;
    } catch (e) {}

    return null;
  }

  #onNavigationCommitted = (eventName, data) => {
    const { navigationId, url } = data;

    this.#seenNavigationCommitted = true;

    if (this.#resolveWhenCommitted && this.#navigationId === navigationId) {
      this.#targetURI = Services.io.newURI(url);
      if (this.#resolveWhenCommittedError) {
        this.#trace(
          `Received "navigation-committed" event for an error page` +
            ` (error: ${this.#resolveWhenCommittedError}). Stopping the navigation.`
        );
        this.stop({ error: new Error(this.#resolveWhenCommittedError) });
      } else {
        this.#trace(
          `Received "navigation-committed" event. Stopping the navigation.`
        );
        this.stop();
      }
    }
  };

  #onNavigationFailed = (eventName, data) => {
    const { errorName, navigationId } = data;

    if (this.#navigationId === navigationId) {
      this.#trace(
        `Received "navigation-failed" event with error=${errorName}. Stopping the navigation.`
      );
      this.stop({ error: new Error(errorName) });
    }
  };

  #setUnloadTimer() {
    if (this.#expectNavigation) {
      this.#trace("Skip setting the unload timer");
    } else {
      this.#trace(`Setting unload timer (${this.#unloadTimeout}ms)`);

      this.#unloadTimerId = lazy.setTimeout(() => {
        this.#trace(`No navigation detected: ${this.currentURI?.spec}`);
        // Assume the target is the currently loaded URI.
        this.#targetURI = this.currentURI;
        this.stop();
      }, this.#unloadTimeout);
    }
  }

  #trace(message) {
    lazy.logger.trace(lazy.truncate`${this.#messagePrefix} ${message}`);
  }

  onStateChange(progress, request, flag, status) {
    this.#checkLoadingState(request, {
      isStart: !!(flag & STATE_START),
      isStop: !!(flag & STATE_STOP),
      status,
    });
  }

  onLocationChange(progress, request, location, flag) {
    // Ignore all location changes from subframes.
    if (progress.browsingContext !== this.#webProgress.browsingContext) {
      return;
    }

    if (flag & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE) {
      // If an error page has been loaded abort the navigation.
      const errorName = this.#errorName || this.#getErrorName(this.documentURI);
      this.#trace(
        lazy.truncate`Location=errorPage, error=${errorName}, url=${this.documentURI.spec}`
      );
      if (this.#seenNavigationCommitted || !this.#navigationListener) {
        // If the navigation-committed event was already received, resolve immediately
        this.stop({
          error: new lazy.NavigationError(errorName, this.#errorStatus),
        });
      } else {
        this.#trace(
          `Waiting for the "navigation-committed" event for the error page navigation (error: ${errorName}).`
        );
        this.#resolveWhenCommittedError = errorName;
        this.#resolveWhenCommitted = true;
      }
      return;
    }

    if (flag & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      const stop = type => {
        this.#targetURI = location;
        this.#trace(`Location=${type}: ${this.#targetURI?.spec}`);
        this.stop();
      };

      if (location.hasRef) {
        // If the target URL contains a hash, handle the navigation as a
        // fragment navigation.
        stop("fragmentNavigated");
        return;
      }

      stop("sameDocument");
    }
  }

  /**
   * Start observing web progress changes.
   *
   * @param {string=} navigationId
   *     The UUID for the navigation.
   * @param {object=} options
   * @param {boolean=} options.waitForExplicitStart
   *     Overrides the value of the same option as passed to the constructor
   *     for this navigation only.
   * @returns {Promise}
   *     A promise that will resolve when the navigation has been finished.
   */
  start(navigationId, options = {}) {
    const { waitForExplicitStart = this.#waitForExplicitStart } = options;

    this.#navigationId = navigationId;

    if (this.#deferredNavigation) {
      throw new Error(`Progress listener already started`);
    }

    this.#trace(
      `Start: expectNavigation=${this.#expectNavigation} resolveWhenStarted=${
        this.#resolveWhenStarted
      } unloadTimeout=${this.#unloadTimeout} waitForExplicitStart=${waitForExplicitStart}`
    );

    this.#ignoredLoadURI = null;
    if (this.#webProgress.isLoadingDocument) {
      this.#targetURI = this.#getTargetURI(this.#webProgress.documentRequest);
      this.#trace(`Document already loading ${this.#targetURI?.spec}`);

      if (this.#resolveWhenStarted && !waitForExplicitStart) {
        this.#trace(
          "Resolve on document loading if not waiting for a load or a new navigation"
        );
        return Promise.resolve();
      }

      if (waitForExplicitStart) {
        this.#ignoredLoadURI = this.#targetURI;
        this.#trace(
          lazy.truncate`Wait for explicit start state, ignore stop state for ${this.#ignoredLoadURI?.spec}`
        );
      }
    }

    this.#deferredNavigation = lazy.Deferred();

    // Enable all location change and network state notifications to get
    // informed about an upcoming load as early as possible.
    this.#webProgress.addProgressListener(
      this,
      Ci.nsIWebProgress.NOTIFY_LOCATION | Ci.nsIWebProgress.NOTIFY_STATE_NETWORK
    );

    webProgressListeners.add(this);

    if (this.#webProgress.isLoadingDocument && !waitForExplicitStart) {
      this.#checkLoadingState(this.#webProgress.documentRequest, {
        isStart: true,
      });
    } else {
      // If the document is not loading yet wait some time for the navigation
      // to be started.
      this.#setUnloadTimer();
    }

    return this.#deferredNavigation.promise;
  }

  /**
   * Stop observing web progress changes.
   *
   * @param {object=} options
   * @param {Error=} options.error
   *     If specified the navigation promise will be rejected with this error.
   */
  stop(options = {}) {
    const { error } = options;

    this.#trace(
      lazy.truncate`Stop: has error=${!!error} url=${this.currentURI.spec}`
    );

    if (!this.#deferredNavigation) {
      throw new Error("Progress listener not yet started");
    }

    lazy.clearTimeout(this.#unloadTimerId);
    this.#unloadTimerId = null;

    this.#webProgress.removeProgressListener(this);
    webProgressListeners.delete(this);

    if (!this.#targetURI) {
      // If no target URI has been set yet it should be the current URI
      this.#targetURI = this.browsingContext.currentURI;
    }

    if (error) {
      this.#deferredNavigation.reject(error);
    } else {
      this.#deferredNavigation.resolve();
    }

    this.#deferredNavigation = null;
  }

  /**
   * Stop the progress listener if and only if we already detected a navigation
   * start.
   *
   * @param {object=} options
   * @param {Error=} options.error
   *     If specified the navigation promise will be rejected with this error.
   */
  stopIfStarted(options) {
    this.#trace(`Stop if started: seenStartFlag=${this.#seenStartFlag}`);
    if (this.#seenStartFlag) {
      this.stop(options);
    }
  }

  toString() {
    return `[object ${this.constructor.name}]`;
  }

  // XPCOM

  QueryInterface = ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]);
}
