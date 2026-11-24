/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  EventDispatcher:
    "chrome://remote/content/marionette/actors/MarionetteEventsParent.sys.mjs",
  getTimeoutMultiplier: "chrome://remote/content/shared/AppInfo.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  MarionettePrefs: "chrome://remote/content/marionette/prefs.sys.mjs",
  PageLoadStrategy:
    "chrome://remote/content/shared/webdriver/Capabilities.sys.mjs",
  ProgressListener: "chrome://remote/content/shared/Navigate.sys.mjs",
  TimedPromise: "chrome://remote/content/shared/Sync.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

// Timeout used to wait for the page to be unloaded.
const TIMEOUT_UNLOAD_EVENT = 5000;

/** @namespace */
export const navigate = {};

/**
 * Checks the value of readyState for the current page
 * load activity, and resolves the command if the load
 * has been finished. It also takes care of the selected
 * page load strategy.
 *
 * @param {PageLoadStrategy} pageLoadStrategy
 *     Strategy when navigation is considered as finished.
 * @param {object} eventData
 * @param {string} eventData.documentURI
 *     Current document URI of the document.
 * @param {string} eventData.readyState
 *     Current ready state of the document.
 *
 * @returns {boolean}
 *     True if the page load has been finished.
 */
function checkReadyState(pageLoadStrategy, eventData = {}) {
  const { documentURI, readyState, isUncommittedInitialDocument } = eventData;

  const result = { error: null, finished: false };

  switch (readyState) {
    case "interactive":
      if (documentURI.startsWith("about:certerror")) {
        result.error = new lazy.error.InsecureCertificateError();
        result.finished = true;
      } else if (/about:.*(error)\?/.exec(documentURI)) {
        result.error = new lazy.error.UnknownError(
          `Reached error page: ${documentURI}`
        );
        result.finished = true;

        // Return early with a page load strategy of eager, and also
        // special-case about:blocked pages which should be treated as
        // non-error pages but do not raise a pageshow event. about:blank
        // is also treaded specifically here, because it gets temporary
        // loaded for new content processes, and we only want to rely on
        // complete loads for it.
      } else if (
        (pageLoadStrategy === lazy.PageLoadStrategy.Eager &&
          documentURI != "about:blank") ||
        /about:blocked\?/.exec(documentURI)
      ) {
        result.finished = true;
      }
      break;

    case "complete":
      if (!isUncommittedInitialDocument) {
        result.finished = true;
      }
      break;
  }

  return result;
}

/**
 * Determines if we expect to get a DOM load event (DOMContentLoaded)
 * on navigating to the <code>future</code> URL.
 *
 * @param {URL} current
 *     URL the browser is currently visiting.
 * @param {object} options
 * @param {BrowsingContext=} options.browsingContext
 *     The current browsing context. Needed for targets of _parent and _top.
 * @param {URL=} options.future
 *     Destination URL, if known.
 * @param {target=} options.target
 *     Link target, if known.
 *
 * @returns {boolean}
 *     Full page load would be expected if future is followed.
 *
 * @throws TypeError
 *     If <code>current</code> is not defined, or any of
 *     <code>current</code> or <code>future</code>  are invalid URLs.
 */
navigate.isLoadEventExpected = function (current, options = {}) {
  const { browsingContext, future, target } = options;

  if (typeof current == "undefined") {
    throw new TypeError("Expected at least one URL");
  }

  if (["_parent", "_top"].includes(target) && !browsingContext) {
    throw new TypeError(
      "Expected browsingContext when target is _parent or _top"
    );
  }

  // Don't wait if the navigation happens in a different browsing context
  if (
    target === "_blank" ||
    (target === "_parent" && browsingContext.parent) ||
    (target === "_top" && browsingContext.top != browsingContext)
  ) {
    return false;
  }

  // Assume we will go somewhere exciting
  if (typeof future == "undefined") {
    return true;
  }

  // Assume javascript:<whatever> will modify the current document
  // but this is not an entirely safe assumption to make,
  // considering it could be used to set window.location
  if (future.protocol == "javascript:") {
    return false;
  }

  // If hashes are present and identical
  if (
    current.href.includes("#") &&
    future.href.includes("#") &&
    current.hash === future.hash
  ) {
    return false;
  }

  return true;
};

/**
 * Load the given URI in the specified browsing context.
 *
 * @param {CanonicalBrowsingContext} browsingContext
 *     Browsing context to load the URI into.
 * @param {nsIURI} uri
 *     URI to navigate to.
 */
navigate.navigateTo = function (browsingContext, uri) {
  const opts = {
    loadFlags: Ci.nsIWebNavigation.LOAD_FLAGS_IS_LINK,
    // Fake user activation.
    hasValidUserGestureActivation: true,
    // Prevent HTTPS-First upgrades.
    schemelessInput: Ci.nsILoadInfo.SchemelessInputTypeSchemeful,
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  };

  browsingContext.loadURI(uri, opts);
};

/**
 * Reload the page.
 *
 * @param {CanonicalBrowsingContext} browsingContext
 *     Browsing context to refresh.
 */
navigate.refresh = function (browsingContext) {
  const { sessionHistory } = browsingContext;
  const flags = Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;

  // Bug 2026546: As workaround use sessionHistory if available to avoid issues
  // with frames.
  if (sessionHistory?.count && sessionHistory?.index >= 0) {
    sessionHistory.reload(flags);
  } else {
    browsingContext.reload(flags);
  }
};

/**
 * Execute a callback and wait for a possible navigation to complete
 *
 * @param {GeckoDriver} driver
 *     Reference to driver instance.
 * @param {Function} callback
 *     Callback to execute that might trigger a navigation.
 * @param {object} options
 * @param {BrowsingContext=} options.browsingContext
 *     Browsing context to observe. Defaults to the current browsing context.
 * @param {boolean=} options.loadEventExpected
 *     If false, return immediately and don't wait for
 *     the navigation to be completed. Defaults to true.
 * @param {boolean=} options.requireBeforeUnload
 *     If false and no beforeunload event is fired, abort waiting
 *     for the navigation. Defaults to true.
 */
navigate.waitForNavigationCompleted = async function waitForNavigationCompleted(
  driver,
  callback,
  options = {}
) {
  const {
    // Skip the privilege check because observing a navigation needs to work
    // regardless of the current page. Callers are responsible for restricting
    // the navigation itself.
    browsingContextFn = () =>
      driver.getBrowsingContext({ skipPrivilegeCheck: true }),
    loadEventExpected = true,
    requireBeforeUnload = true,
  } = options;

  const browsingContext = browsingContextFn();
  const chromeWindow = browsingContext.topChromeWindow;
  const pageLoadStrategy = driver.currentSession.pageLoadStrategy;

  // Return immediately if no load event is expected
  if (!loadEventExpected) {
    await callback();
    return Promise.resolve();
  }

  // When not waiting for page load events, do not return until the navigation has actually started.
  if (pageLoadStrategy === lazy.PageLoadStrategy.None) {
    const listener = new lazy.ProgressListener(browsingContext.webProgress, {
      resolveWhenStarted: true,
      waitForExplicitStart: true,
    });
    const navigated = listener.start();
    navigated.finally(() => {
      if (listener.isStarted) {
        listener.stop();
      }
      listener.destroy();
    });

    await callback();
    await navigated;

    return Promise.resolve();
  }

  let rejectNavigation;
  let resolveNavigation;

  let browsingContextChanged = false;
  let seenBeforeUnload = false;
  let seenUnload = false;

  let unloadTimer;

  const checkDone = ({ finished, error }) => {
    if (finished) {
      if (error) {
        rejectNavigation(error);
      } else {
        resolveNavigation();
      }
    }
  };

  /**
   * For the command "Element Click" we want to detect a potential navigation
   * as early as possible. The `beforeunload` event is an indication for that
   * but could still cause the navigation to get aborted by the user. As such
   * wait a bit longer for the `unload` event to happen (only when the page
   * load strategy is `none`), which usually will occur pretty soon after
   * `beforeunload`.
   *
   * Note that with WebDriver BiDi enabled the `beforeunload` prompts might
   * not get implicitly accepted, so lets keep the timer around until we know
   * that it is really not required.
   *
   * @param {string=} reason
   *     Reason to log when the page load listener gets canceled because no
   *     navigation has been detected.
   */
  const checkForNavigation = (reason = "no navigation has been detected") => {
    if (seenBeforeUnload) {
      seenBeforeUnload = false;

      if (!unloadTimer) {
        unloadTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      }
      unloadTimer.initWithCallback(
        onTimer,
        TIMEOUT_UNLOAD_EVENT,
        Ci.nsITimer.TYPE_ONE_SHOT
      );

      // If no page unload has been detected, ensure to properly stop
      // the load listener, and return from the currently active command.
    } else if (!seenUnload) {
      lazy.logger.trace(`Canceled page load listener because ${reason}`);
      checkDone({ finished: true });
    }
  };

  const onPromptClosed = (_, data) => {
    if (data.detail.promptType === "beforeunload" && !data.detail.accepted) {
      // If a beforeunload prompt is dismissed there will be no navigation.
      lazy.logger.trace(
        `Canceled page load listener because a beforeunload prompt was dismissed`
      );
      checkDone({ finished: true });
    }
  };

  const onPromptOpened = (_, data) => {
    if (data.prompt.promptType === "beforeunload") {
      // WebDriver HTTP basically doesn't know anything about beforeunload
      // prompts. As such we always ignore the prompt opened event.
      return;
    }

    lazy.logger.trace(
      `Canceled page load listener because a ${data.prompt.promptType} prompt opened`
    );
    checkDone({ finished: true });
  };

  const onNavigation = (eventName, data) => {
    const browsingContext = browsingContextFn();

    // Ignore events from other browsing contexts than the selected one.
    if (data.browsingContext != browsingContext) {
      return;
    }

    lazy.logger.trace(
      lazy.truncate`[${data.browsingContext.id}] Received event ${data.type} for ${data.documentURI}`
    );

    switch (data.type) {
      case "beforeunload":
        seenBeforeUnload = true;
        break;

      case "pagehide":
        seenUnload = true;
        break;

      case "hashchange":
      case "popstate":
        checkDone({ finished: true });
        break;

      case "DOMContentLoaded":
      case "pageshow": {
        // Don't require an unload event when a top-level browsing context
        // change occurred.
        // The initial about:blank load has no previous page to unload.
        if (!seenUnload && !browsingContextChanged && !data.isInitialDocument) {
          return;
        }
        const result = checkReadyState(pageLoadStrategy, data);
        checkDone(result);
        break;
      }
    }
  };

  // In the case when the currently selected frame is closed,
  // there will be no further load events. Stop listening immediately.
  const onBrowsingContextDiscarded = (subject, topic, why) => {
    // If the BrowsingContext is being discarded to be replaced by another
    // context, we don't want to stop waiting for the pageload to complete, as
    // we will continue listening to the newly created context.
    if (subject == browsingContextFn() && why != "replace") {
      lazy.logger.trace(
        "Canceled page load listener " +
          `because browsing context with id ${subject.id} has been removed`
      );
      checkDone({ finished: true });
    }
  };

  // Detect changes to the top-level browsing context to not
  // necessarily require an unload event.
  const onBrowsingContextChanged = event => {
    if (event.target === driver.curBrowser.contentBrowser) {
      browsingContextChanged = true;
    }
  };

  const onTimer = () => checkForNavigation();

  const onUnload = () => {
    lazy.logger.trace(
      "Canceled page load listener " +
        "because the top-browsing context has been closed"
    );
    checkDone({ finished: true });
  };

  chromeWindow.addEventListener("TabClose", onUnload);
  chromeWindow.addEventListener("unload", onUnload);
  driver.curBrowser.tabBrowser?.addEventListener(
    "XULFrameLoaderCreated",
    onBrowsingContextChanged
  );
  driver.promptListener.on("closed", onPromptClosed);
  driver.promptListener.on("opened", onPromptOpened);
  Services.obs.addObserver(
    onBrowsingContextDiscarded,
    "browsing-context-discarded"
  );

  lazy.EventDispatcher.on("page-load", onNavigation);

  const waitForCompleted = async (resolve, reject) => {
    rejectNavigation = reject;
    resolveNavigation = resolve;

    try {
      await callback();

      // Certain commands like clickElement can cause a navigation. Setup a timer
      // to check if a "beforeunload" event has been emitted within the given
      // time frame. If not resolve the Promise.
      if (!requireBeforeUnload) {
        if (lazy.MarionettePrefs.navigateAfterClickEnabled) {
          unloadTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
          unloadTimer.initWithCallback(
            onTimer,
            lazy.MarionettePrefs.navigateAfterClickTimeout *
              lazy.getTimeoutMultiplier(),
            Ci.nsITimer.TYPE_ONE_SHOT
          );
        } else if (!seenUnload) {
          // Waiting for a navigation after a click is disabled, so only
          // handle a navigation that has already been started.
          checkForNavigation(
            "waiting for a navigation after a click is disabled"
          );
        }
      }
    } catch (e) {
      // Executing the callback above could destroy the actor pair before the
      // command returns. Such an error has to be ignored.
      if (e.name !== "AbortError") {
        checkDone({ finished: true, error: e });
      }
    }
  };

  const pageLoadTimeout = driver.currentSession.timeouts.pageLoad;
  const promise =
    pageLoadTimeout === null
      ? new Promise(waitForCompleted)
      : new lazy.TimedPromise(waitForCompleted, {
          errorMessage: "Navigation timed out",
          timeout: pageLoadTimeout,
        });

  return promise.finally(() => {
    // Clean-up all registered listeners and timers
    Services.obs.removeObserver(
      onBrowsingContextDiscarded,
      "browsing-context-discarded"
    );
    chromeWindow.removeEventListener("TabClose", onUnload);
    chromeWindow.removeEventListener("unload", onUnload);
    driver.curBrowser.tabBrowser?.removeEventListener(
      "XULFrameLoaderCreated",
      onBrowsingContextChanged
    );
    driver.promptListener?.off("closed", onPromptClosed);
    driver.promptListener?.off("opened", onPromptOpened);
    unloadTimer?.cancel();

    lazy.EventDispatcher.off("page-load", onNavigation);
  });
};
