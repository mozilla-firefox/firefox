/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { BrowserUtils } from "resource://gre/modules/BrowserUtils.sys.mjs";
import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "ReferrerInfo", () =>
  Components.Constructor(
    "@mozilla.org/referrer-info;1",
    "nsIReferrerInfo",
    "init"
  )
);

function saveLink(window, url, params) {
  if ("isContentWindowPrivate" in params) {
    window.saveURL(
      url,
      null,
      null,
      null,
      true,
      true,
      params.referrerInfo,
      null,
      null,
      params.isContentWindowPrivate,
      params.originPrincipal
    );
  } else {
    if (!params.initiatingDoc) {
      console.error(
        "openUILink/openLinkIn was called with " +
          "where == 'save' but without initiatingDoc.  See bug 814264."
      );
      return;
    }
    window.saveURL(
      url,
      null,
      null,
      null,
      true,
      true,
      params.referrerInfo,
      null,
      params.initiatingDoc
    );
  }
}

function openInWindow(url, params, sourceWindow) {
  let {
    referrerInfo,
    forceNonPrivate,
    triggeringRemoteType,
    forceAllowDataURI,
    globalHistoryOptions,
    allowThirdPartyFixup,
    userContextId,
    postData,
    originPrincipal,
    originStoragePrincipal,
    triggeringPrincipal,
    policyContainer,
    resolveOnContentBrowserCreated,
    chromeless,
    width,
    height,
  } = params;
  const chromelessDimensions =
    chromeless && width && height ? `,width=${width},height=${height}` : "";
  const CHROMELESS_FEATURES = `resizable,minimizable,titlebar,close${chromelessDimensions}`;
  let features = `chrome,dialog=no,${chromeless ? CHROMELESS_FEATURES : "all"}`;
  if (params.private) {
    features += ",private";
    // To prevent regular browsing data from leaking to private browsing sites,
    // strip the referrer when opening a new private window. (See Bug: 1409226)
    referrerInfo = new lazy.ReferrerInfo(
      referrerInfo.referrerPolicy,
      false,
      referrerInfo.originalReferrer
    );
  } else if (forceNonPrivate) {
    features += ",non-private";
  }

  // This propagates to window.arguments.
  var sa = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);

  var wuri = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  wuri.data = url;

  let extraOptions = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
    Ci.nsIWritablePropertyBag2
  );
  if (triggeringRemoteType) {
    extraOptions.setPropertyAsACString(
      "triggeringRemoteType",
      triggeringRemoteType
    );
  }
  if (params.hasValidUserGestureActivation !== undefined) {
    extraOptions.setPropertyAsBool(
      "hasValidUserGestureActivation",
      params.hasValidUserGestureActivation
    );
  }
  if (params.textDirectiveUserActivation !== undefined) {
    extraOptions.setPropertyAsBool(
      "textDirectiveUserActivation",
      params.textDirectiveUserActivation
    );
  }
  if (forceAllowDataURI) {
    extraOptions.setPropertyAsBool("forceAllowDataURI", true);
  }
  if (params.fromExternal !== undefined) {
    extraOptions.setPropertyAsBool("fromExternal", params.fromExternal);
  }
  if (globalHistoryOptions?.triggeringSponsoredURL) {
    extraOptions.setPropertyAsACString(
      "triggeringSponsoredURL",
      globalHistoryOptions.triggeringSponsoredURL
    );
    if (globalHistoryOptions.triggeringSponsoredURLVisitTimeMS) {
      extraOptions.setPropertyAsUint64(
        "triggeringSponsoredURLVisitTimeMS",
        globalHistoryOptions.triggeringSponsoredURLVisitTimeMS
      );
    }
    if (globalHistoryOptions.triggeringSource) {
      extraOptions.setPropertyAsACString(
        "triggeringSource",
        globalHistoryOptions.triggeringSource
      );
    }
  }
  if (params.schemelessInput !== undefined) {
    extraOptions.setPropertyAsUint32("schemelessInput", params.schemelessInput);
  }
  if (params.aiWindow) {
    extraOptions.setPropertyAsBool("ai-window", true);
  }
  if (chromeless) {
    extraOptions.setPropertyAsBool("chromeless-window", true);
  }

  var allowThirdPartyFixupSupports = Cc[
    "@mozilla.org/supports-PRBool;1"
  ].createInstance(Ci.nsISupportsPRBool);
  allowThirdPartyFixupSupports.data = allowThirdPartyFixup;

  var userContextIdSupports = Cc[
    "@mozilla.org/supports-PRUint32;1"
  ].createInstance(Ci.nsISupportsPRUint32);
  userContextIdSupports.data = userContextId;

  sa.appendElement(wuri);
  sa.appendElement(extraOptions);
  sa.appendElement(referrerInfo);
  sa.appendElement(postData);
  sa.appendElement(allowThirdPartyFixupSupports);
  sa.appendElement(userContextIdSupports);
  sa.appendElement(originPrincipal);
  sa.appendElement(originStoragePrincipal);
  sa.appendElement(triggeringPrincipal);
  sa.appendElement(null); // allowInheritPrincipal
  sa.appendElement(policyContainer);

  let win;

  // Returns a promise that will be resolved when the new window's startup is finished.
  function waitForWindowStartup() {
    return new Promise(resolve => {
      const delayedStartupObserver = aSubject => {
        if (aSubject == win) {
          Services.obs.removeObserver(
            delayedStartupObserver,
            "browser-delayed-startup-finished"
          );
          resolve();
        }
      };
      Services.obs.addObserver(
        delayedStartupObserver,
        "browser-delayed-startup-finished"
      );
    });
  }

  if (params.frameID != undefined && sourceWindow) {
    // Only notify it as a WebExtensions' webNavigation.onCreatedNavigationTarget
    // event if it contains the expected frameID params.
    // (e.g. we should not notify it as a onCreatedNavigationTarget if the user is
    // opening a new window using the keyboard shortcut).
    const sourceTabBrowser = sourceWindow.gBrowser.selectedBrowser;
    waitForWindowStartup().then(() => {
      Services.obs.notifyObservers(
        {
          wrappedJSObject: {
            url,
            createdTabBrowser: win.gBrowser.selectedBrowser,
            sourceTabBrowser,
            sourceFrameID: params.frameID,
          },
        },
        "webNavigation-createdNavigationTarget"
      );
    });
  }

  if (resolveOnContentBrowserCreated) {
    waitForWindowStartup().then(() =>
      resolveOnContentBrowserCreated(win.gBrowser.selectedBrowser)
    );
  }

  win = Services.ww.openWindow(
    sourceWindow,
    AppConstants.BROWSER_CHROME_URL,
    null,
    features,
    sa
  );
}

function openInCurrentTab(targetBrowser, url, uriObj, params) {
  let loadFlags = Ci.nsIWebNavigation.LOAD_FLAGS_NONE;

  if (params.allowThirdPartyFixup) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_FIXUP_SCHEME_TYPOS;
  }
  // LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL isn't supported for javascript URIs,
  // i.e. it causes them not to load at all. Callers should strip
  // "javascript:" from pasted strings to prevent blank tabs
  if (!params.allowInheritPrincipal) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
  }

  if (params.allowPopups) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_POPUPS;
  }
  if (params.indicateErrorPageLoad) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_ERROR_LOAD_CHANGES_RV;
  }
  if (params.forceAllowDataURI) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_FORCE_ALLOW_DATA_URI;
  }
  if (params.fromExternal) {
    loadFlags |= Ci.nsIWebNavigation.LOAD_FLAGS_FROM_EXTERNAL;
  }

  let { URI_INHERITS_SECURITY_CONTEXT } = Ci.nsIProtocolHandler;
  if (
    params.forceAboutBlankViewerInCurrent &&
    (!uriObj ||
      Services.io.getDynamicProtocolFlags(uriObj) &
        URI_INHERITS_SECURITY_CONTEXT)
  ) {
    // Unless we know for sure we're not inheriting principals,
    // force the about:blank viewer to have the right principal:
    targetBrowser.createAboutBlankDocumentViewer(
      params.originPrincipal,
      params.originStoragePrincipal
    );
  }

  let {
    triggeringPrincipal,
    policyContainer,
    referrerInfo,
    postData,
    userContextId,
    hasValidUserGestureActivation,
    textDirectiveUserActivation,
    globalHistoryOptions,
    triggeringRemoteType,
    schemelessInput,
  } = params;

  targetBrowser.fixupAndLoadURIString(url, {
    triggeringPrincipal,
    policyContainer,
    loadFlags,
    referrerInfo,
    postData,
    userContextId,
    hasValidUserGestureActivation,
    textDirectiveUserActivation,
    globalHistoryOptions,
    triggeringRemoteType,
    schemelessInput,
  });

  params.resolveOnContentBrowserCreated?.(targetBrowser);
}

function updatePrincipals(window, params) {
  let { userContextId } = params;
  // Teach the principal about the right OA to use, e.g. in case when
  // opening a link in a new private window, or in a new container tab.
  // Please note we do not have to do that for SystemPrincipals and we
  // can not do it for NullPrincipals since NullPrincipals are only
  // identical if they actually are the same object (See Bug: 1346759)
  function useOAForPrincipal(principal) {
    if (principal && principal.isContentPrincipal) {
      let privateBrowsingId =
        params.private ||
        (window && PrivateBrowsingUtils.isWindowPrivate(window));
      let attrs = {
        userContextId,
        privateBrowsingId,
        firstPartyDomain: principal.originAttributes.firstPartyDomain,
      };
      return Services.scriptSecurityManager.principalWithOA(principal, attrs);
    }
    return principal;
  }
  params.originPrincipal = useOAForPrincipal(params.originPrincipal);
  params.originStoragePrincipal = useOAForPrincipal(
    params.originStoragePrincipal
  );
  params.triggeringPrincipal = useOAForPrincipal(params.triggeringPrincipal);
}

/* Creates a null principal using the userContextId
from the current selected tab or a passed in tab argument */
function _createNullPrincipalFromTabUserContextId(tab = null) {
  const window = lazy.BrowserWindowTracker.getTopWindow();
  if (!tab) {
    tab = window.gBrowser.selectedTab;
  }

  let userContextId;
  if (tab.hasAttribute("usercontextid")) {
    userContextId = tab.getAttribute("usercontextid");
  }
  return Services.scriptSecurityManager.createNullPrincipal({
    userContextId,
  });
}

export const URILoadingHelper = {
  /**
   * openLinkIn opens a URL in a place specified by the parameter |where|.
   *
   * The params object is the same as for `openLinkIn` and documented below.
   *
   * @param {string}  where
   *   |where| can be:
   *    "current"     current tab            (if there aren't any browser windows, then in a new window instead)
   *    "tab"         new tab                (if there aren't any browser windows, then in a new window instead)
   *    "tabshifted"  same as "tab" but in background if default is to select new tabs, and vice versa
   *    "window"      new window
   *    "chromeless"  new minimal window     (no browser navigation UI)
   *    "save"        save to disk (with no filename hint!)
   *
   * @param {object}  params
   *
   * Options relating to what tab/window to use and how to open it:
   *
   * @param {boolean} params.private
   *                  Load the URL in a private window.
   * @param {boolean} params.forceNonPrivate
   *                  Force the load to happen in non-private windows.
   * @param {boolean} params.relatedToCurrent
   *                  Whether new tabs should go immediately next to the current tab.
   * @param {Element} params.targetBrowser
   *                  The browser to use for the load. Only used if where == "current".
   * @param {boolean} params.inBackground
   *                  If explicitly true or false, whether to switch to the tab immediately.
   *                  If null, will switch to the tab if `forceForeground` was true. If
   *                  neither is passed, will defer to the user preference browser.tabs.loadInBackground.
   * @param {boolean} params.forceForeground
   *                  Ignore the user preference and load in the foreground.
   * @param {boolean} params.allowPinnedTabHostChange
   *                  Allow even a pinned tab to change hosts.
   * @param {boolean} params.allowPopups
   *                  whether the link is allowed to open in a popup window (ie one with no browser
   *                  chrome)
   * @param {boolean} params.skipTabAnimation
   *                  Skip the tab opening animation.
   * @param {Element} params.openerBrowser
   *                  The browser that started the load.
   * @param {boolean} params.avoidBrowserFocus
   *                  Don't focus the browser element immediately after starting
   *                  the load. Used by the URL bar to avoid leaking user input
   *                  into web content, see bug 1641287.
   *
   * Options relating to the load itself:
   *
   * @param {boolean} params.allowThirdPartyFixup
   *                  Allow transforming the 'url' into a search query.
   * @param {nsIInputStream} params.postData
   *                  Data to post as part of the request.
   * @param {nsIReferrerInfo} params.referrerInfo
   *                  Referrer info for the request.
   * @param {boolean} params.indicateErrorPageLoad
   *                  Whether docshell should throw an exception (i.e. return non-NS_OK)
   *                  if the load fails.
   * @param {string}  params.charset
   *                  Character set to use for the load. Only honoured for tabs.
   *                  Legacy argument - do not use.
   * @param {SchemelessInputType}  params.schemelessInput
   *                  Whether the search/URL term was without an explicit scheme.
   *
   * Options relating to security, whether the load is allowed to happen,
   * and what cookie container to use for the load:
   *
   * @param {boolean} params.forceAllowDataURI
   *                  Force allow a data URI to load as a toplevel load.
   * @param {number}  params.userContextId
   *                  The userContextId (container identifier) to use for the load.
   *                  If where is "current" and the specified userContextId differs, a new tab is opened instead.
   * @param {boolean} params.allowInheritPrincipal
   *                  Allow the load to inherit the triggering principal.
   * @param {boolean} params.forceAboutBlankViewerInCurrent
   *                  Force load an about:blank page first. Only used if
   *                  allowInheritPrincipal is passed or no URL was provided.
   * @param {nsIPrincipal} params.triggeringPrincipal
   *                  Triggering principal to pass to docshell for the load.
   * @param {nsIPrincipal} params.originPrincipal
   *                  Origin principal to pass to docshell for the load.
   * @param {nsIPrincipal} params.originStoragePrincipal
   *                  Storage principal to pass to docshell for the load.
   * @param {string}  params.triggeringRemoteType
   *                  The remoteType triggering this load.
   * @param {nsIPolicyContainer} params.policyContainer
   *                  The policyContainer that should apply to the load.
   * @param {boolean} params.hasValidUserGestureActivation
   *                  Indicates if a valid user gesture caused this load. This
   *                  informs e.g. popup blocker decisions.
   * @param {boolean} params.fromExternal
   *                  Indicates the load was started outside of the browser,
   *                  e.g. passed on the commandline or through OS mechanisms.
   *
   * Options used to track the load elsewhere
   *
   * @param {function} params.resolveOnNewTabCreated
   *                   This callback will be called when a new tab is created.
   * @param {function} params.resolveOnContentBrowserCreated
   *                   This callback will be called with the content browser once it's created.
   * @param {object}   params.globalHistoryOptions
   *                   Used by places to keep track of search related metadata for loads.
   * @param {number}   params.frameID
   *                   Used by webextensions for their loads.
   *
   * Options used for where="save" only:
   *
   * @param {boolean}  params.isContentWindowPrivate
   *                   Save content as coming from a private window.
   * @param {Document} params.initiatingDoc
   *                   Used to determine where to prompt for a filename.
   */
  openLinkIn(window, url, where, params) {
    if (!where || !url) {
      return;
    }

    let {
      allowThirdPartyFixup,
      postData,
      charset,
      allowInheritPrincipal,
      forceAllowDataURI,
      forceNonPrivate,
      skipTabAnimation,
      allowPinnedTabHostChange,
      userContextId,
      triggeringPrincipal,
      originPrincipal,
      originStoragePrincipal,
      triggeringRemoteType,
      policyContainer,
      resolveOnNewTabCreated,
      resolveOnContentBrowserCreated,
      globalHistoryOptions,
      hasValidUserGestureActivation,
      textDirectiveUserActivation,
    } = params;

    // We want to overwrite some things for convenience when passing it to other
    // methods. To avoid impacting callers, copy the params.
    params = Object.assign({}, params);

    if (!params.referrerInfo) {
      params.referrerInfo = new lazy.ReferrerInfo(
        Ci.nsIReferrerInfo.EMPTY,
        true,
        null
      );
    }

    if (!triggeringPrincipal) {
      throw new Error("Must load with a triggering Principal");
    }

    if (where == "save") {
      saveLink(window, url, params);
      return;
    }

    // Establish which window we'll load the link in.
    let w = this._resolveInitialTargetWindow(
      where,
      params,
      window,
      forceNonPrivate
    );

    updatePrincipals(w, params);

    if (where == "chromeless") {
      params.chromeless = true;
      where = "window";
    }

    if (!w || where == "window") {
      openInWindow(url, params, w || window);
      return;
    }

    // We're now committed to loading the link in an existing browser window.

    // Raise the target window before loading the URI, since loading it may
    // result in a new frontmost window (e.g. "javascript:window.open('');").
    w.focus();

    let targetBrowser;
    let uriObj;
    let loadInBackground = BrowserUtils.willLoadInBackground(where, params);

    if (where == "current") {
      targetBrowser = params.targetBrowser || w.gBrowser.selectedBrowser;
      uriObj = URL.parse(url)?.URI;

      // In certain tabs, we restrict what if anything may replace the loaded
      // page. If a load request bounces off for the currently selected tab,
      // we'll open a new tab instead.
      let tab = w.gBrowser.getTabForBrowser(targetBrowser);
      if (tab == w.FirefoxViewHandler.tab) {
        where = "tab";
        targetBrowser = null;
      } else if (
        params.userContextId != null &&
        params.userContextId !==
          targetBrowser.browsingContext.originAttributes.userContextId
      ) {
        where = "tab";
        targetBrowser = null;
      } else if (
        !allowPinnedTabHostChange &&
        tab.pinned &&
        url != "about:crashcontent"
      ) {
        try {
          // nsIURI.host can throw for non-nsStandardURL nsIURIs.
          if (
            !uriObj ||
            (!uriObj.schemeIs("javascript") &&
              targetBrowser.currentURI.host != uriObj.host)
          ) {
            where = "tab";
            targetBrowser = null;
          }
        } catch (err) {
          where = "tab";
          targetBrowser = null;
        }
      }
    }

    let focusUrlBar = false;

    switch (where) {
      case "current":
        openInCurrentTab(targetBrowser, url, uriObj, params);

        // Don't focus the content area if focus is in the address bar and we're
        // loading the New Tab page.
        focusUrlBar =
          w.document.activeElement == w.gURLBar.inputField &&
          w.isBlankPageURL(url);
        break;
      case "tab":
      case "tabshifted": {
        focusUrlBar =
          !loadInBackground &&
          w.isBlankPageURL(url) &&
          !lazy.AboutNewTab.willNotifyUser;

        let tabUsedForLoad = w.gBrowser.addTab(url, {
          referrerInfo: params.referrerInfo,
          charset,
          postData,
          inBackground: loadInBackground,
          allowThirdPartyFixup,
          relatedToCurrent: params.relatedToCurrent,
          skipAnimation: skipTabAnimation,
          userContextId,
          originPrincipal,
          originStoragePrincipal,
          triggeringPrincipal,
          allowInheritPrincipal,
          triggeringRemoteType,
          policyContainer,
          forceAllowDataURI,
          focusUrlBar,
          openerBrowser: params.openerBrowser,
          fromExternal: params.fromExternal,
          globalHistoryOptions,
          schemelessInput: params.schemelessInput,
          hasValidUserGestureActivation,
          textDirectiveUserActivation,
        });
        targetBrowser = tabUsedForLoad.linkedBrowser;

        resolveOnNewTabCreated?.(targetBrowser);
        resolveOnContentBrowserCreated?.(targetBrowser);

        if (params.frameID != undefined && w) {
          // Only notify it as a WebExtensions' webNavigation.onCreatedNavigationTarget
          // event if it contains the expected frameID params.
          // (e.g. we should not notify it as a onCreatedNavigationTarget if the user is
          // opening a new tab using the keyboard shortcut).
          Services.obs.notifyObservers(
            {
              wrappedJSObject: {
                url,
                createdTabBrowser: targetBrowser,
                sourceTabBrowser: w.gBrowser.selectedBrowser,
                sourceFrameID: params.frameID,
              },
            },
            "webNavigation-createdNavigationTarget"
          );
        }
        break;
      }
    }

    // Potentially trigger a URL bar telemetry bounce event when navigating
    // away from a page using the browser chrome.
    // We avoid triggering for URL bar initiated loads since this gets called
    // right after a result is picked and the bounce event tracking is started.
    // We instead check for potential URL bar initiated bounce events directly
    // in gURLBar.controller.engagementEvent.startTrackingBounceEvent().
    if (!params.initiatedByURLBar && targetBrowser) {
      w.gURLBar.controller.engagementEvent.handleBounceEventTrigger(
        targetBrowser
      );
    }

    if (
      !params.avoidBrowserFocus &&
      !focusUrlBar &&
      targetBrowser == w.gBrowser.selectedBrowser
    ) {
      // Focus the content, but only if the browser used for the load is selected.
      targetBrowser.focus();
    }
  },
  /**
   * Resolve the initial browser window to use for a load, based on `where`.
   *
   * @param {string} where
   *        The target location for the load (e.g. "current", "tab", "window").
   * @param {object} params
   *        The full params object passed to openLinkIn.
   * @param {Window} win
   *        The reference window used as a fallback for getTargetWindow.
   * @param {boolean} forceNonPrivate
   *        Whether to force choosing a non-private target window.
   * @returns {Window}
   *          The browser window that should be used as the initial target.
   */
  _resolveInitialTargetWindow(where, params, win, forceNonPrivate) {
    if (where === "current" && params.targetBrowser) {
      return params.targetBrowser.documentGlobal;
    }

    if (where === "tab" || where === "tabshifted") {
      const target = this.getTargetWindow(win, {
        skipPopups: true,
        skipTaskbarTabs: true,
        forceNonPrivate,
      });
      if (win.top !== target) {
        params.relatedToCurrent = false;
      }
      return target;
    }
    return this.getTargetWindow(win, { forceNonPrivate });
  },
  /**
   * Finds a browser window suitable for opening a link matching the
   * requirements given in the `params` argument. If the current window matches
   * the requirements then it is returned otherwise the top-most window that
   * matches will be returned.
   *
   * @param {Window} window - The current window.
   * @param {object} params - Parameters for selecting the window.
   * @param {boolean} params.skipPopups - Require a non-popup window.
   * @param {boolean} params.skipTaskbarTabs - Require a non-taskbartab window.
   * @param {boolean} params.forceNonPrivate - Require a non-private window.
   * @returns {Window | null} A matching browser window or null if none matched.
   */
  getTargetWindow(
    window,
    { skipPopups, skipTaskbarTabs, forceNonPrivate } = {}
  ) {
    let { top } = window;
    // If this is called in a browser window, use that window regardless of
    // whether it's the frontmost window, since commands can be executed in
    // background windows (bug 626148).
    if (
      top.document.documentElement.getAttribute("windowtype") ==
        "navigator:browser" &&
      (!skipPopups || top.toolbar.visible) &&
      (!skipTaskbarTabs ||
        !top.document.documentElement.hasAttribute("taskbartab")) &&
      (!forceNonPrivate || !PrivateBrowsingUtils.isWindowPrivate(top))
    ) {
      return top;
    }

    return lazy.BrowserWindowTracker.getTopWindow({
      private: !forceNonPrivate && PrivateBrowsingUtils.isWindowPrivate(window),
      allowPopups: !skipPopups,
      allowTaskbarTabs: !skipTaskbarTabs,
    });
  },

  /**
   * openUILink handles clicks on UI elements that cause URLs to load.
   *
   * @param {string} url
   * @param {Event | object} event Event or JSON object representing an Event
   * @param {boolean | object} aIgnoreButton
   *                           Boolean or object with the same properties as
   *                           accepted by openLinkIn, plus "ignoreButton"
   *                           and "ignoreAlt".
   * @param {boolean} aIgnoreAlt
   * @param {boolean} aAllowThirdPartyFixup
   * @param {object} aPostData
   * @param {object} aReferrerInfo
   */
  openUILink(
    window,
    url,
    event,
    aIgnoreButton,
    aIgnoreAlt,
    aAllowThirdPartyFixup,
    aPostData,
    aReferrerInfo
  ) {
    event = BrowserUtils.getRootEvent(event);
    let params;

    if (aIgnoreButton && typeof aIgnoreButton == "object") {
      params = aIgnoreButton;

      // don't forward "ignoreButton" and "ignoreAlt" to openLinkIn
      aIgnoreButton = params.ignoreButton;
      aIgnoreAlt = params.ignoreAlt;
      delete params.ignoreButton;
      delete params.ignoreAlt;
    } else {
      params = {
        allowThirdPartyFixup: aAllowThirdPartyFixup,
        postData: aPostData,
        referrerInfo: aReferrerInfo,
        initiatingDoc: event ? event.target.ownerDocument : null,
      };
    }

    if (!params.triggeringPrincipal) {
      throw new Error(
        "Required argument triggeringPrincipal missing within openUILink"
      );
    }

    let where = BrowserUtils.whereToOpenLink(event, aIgnoreButton, aIgnoreAlt);
    params.forceForeground ??= true;
    this.openLinkIn(window, url, where, params);
  },

  /* openTrustedLinkIn will attempt to open the given URI using the SystemPrincipal
   * as the trigeringPrincipal, unless a more specific Principal is provided.
   *
   * Otherwise, parameters are the same as openLinkIn, but we will set `forceForeground`
   * to true.
   */
  openTrustedLinkIn(window, url, where, params = {}) {
    if (!params.triggeringPrincipal) {
      params.triggeringPrincipal =
        Services.scriptSecurityManager.getSystemPrincipal();
    }

    params.forceForeground ??= true;
    this.openLinkIn(window, url, where, params);
  },

  /* openWebLinkIn will attempt to open the given URI using the NullPrincipal
   * as the triggeringPrincipal, unless a more specific Principal is provided.
   *
   * Otherwise, parameters are the same as openLinkIn, but we will set `forceForeground`
   * to true.
   */
  openWebLinkIn(window, url, where, params = {}) {
    if (!params.triggeringPrincipal) {
      params.triggeringPrincipal =
        Services.scriptSecurityManager.createNullPrincipal({});
    }
    if (params.triggeringPrincipal.isSystemPrincipal) {
      throw new Error(
        "System principal should never be passed into openWebLinkIn()"
      );
    }
    params.forceForeground ??= true;
    this.openLinkIn(window, url, where, params);
  },

  /**
   * Given a URI, guess which container to use to open it. This is used for external
   * openers as a quality of life improvement (e.g. to open a document into the container
   * where you are logged in to the service that hosts it).
   * matches will be returned.
   * For now this can only use currently-open tabs, until history is tagged with the
   * container id (https://bugzilla.mozilla.org/show_bug.cgi?id=1283320).
   *
   * @param {nsIURI} aURI - The URI being opened.
   * @returns {number | null} The guessed userContextId, or null if none.
   */
  guessUserContextId(aURI) {
    let host;
    try {
      host = aURI.host;
    } catch (e) {}
    if (!host) {
      return null;
    }
    const containerScores = new Map();
    let guessedUserContextId = null;
    let maxCount = 0;
    for (let win of lazy.BrowserWindowTracker.orderedWindows) {
      for (let tab of win.gBrowser.visibleTabs) {
        let { userContextId } = tab;
        let currentURIHost = null;
        try {
          currentURIHost = tab.linkedBrowser.currentURI.host;
        } catch (e) {}

        if (currentURIHost == host) {
          let count = (containerScores.get(userContextId) ?? 0) + 1;
          containerScores.set(userContextId, count);
          if (count > maxCount) {
            guessedUserContextId = userContextId;
            maxCount = count;
          }
        }
      }
    }

    return guessedUserContextId;
  },
  /**
   * Switch to a tab that has a given URI, and focuses its browser window.
   * If a matching tab is in this window, it will be switched to. Otherwise, other
   * windows will be searched.
   *
   * @param window
   *        The current window
   * @param aURI
   *        URI to search for
   * @param aOpenNew
   *        True to open a new tab and switch to it, if no existing tab is found.
   *        If no suitable window is found, a new one will be opened.
   * @param aOpenParams
   *        If switching to this URI results in us opening a tab, aOpenParams
   *        will be the parameter object that gets passed to openTrustedLinkIn. Please
   *        see the documentation for openTrustedLinkIn to see what parameters can be
   *        passed via this object.
   *        This object also allows:
   *        - 'ignoreFragment' property to be set to true to exclude fragment-portion
   *        matching when comparing URIs.
   *          If set to "whenComparing", the fragment will be unmodified.
   *          If set to "whenComparingAndReplace", the fragment will be replaced.
   *        - 'ignoreQueryString' boolean property to be set to true to exclude query string
   *        matching when comparing URIs.
   *        - 'replaceQueryString' boolean property to be set to true to exclude query string
   *        matching when comparing URIs and overwrite the initial query string with
   *        the one from the new URI.
   *        - 'adoptIntoActiveWindow' boolean property to be set to true to adopt the tab
   *        into the current window.
   * @param aUserContextId
   *        If not null, will switch to the first found tab having the provided
   *        userContextId.
   * @param aSplitView
   *        If not null, will move the tab to the active split view instead of switching to tab
   * @return True if an existing tab was found, false otherwise
   */
  switchToTabHavingURI(
    window,
    aURI,
    aOpenNew,
    aOpenParams = {},
    aUserContextId = null,
    aSplitView = null
  ) {
    // Certain URLs can be switched to irrespective of the source or destination
    // window being in private browsing mode:
    const kPrivateBrowsingURLs = new Set(["about:addons"]);

    let ignoreFragment = aOpenParams.ignoreFragment;
    let ignoreQueryString = aOpenParams.ignoreQueryString;
    let replaceQueryString = aOpenParams.replaceQueryString;
    let adoptIntoActiveWindow = aOpenParams.adoptIntoActiveWindow;

    // These properties are only used by switchToTabHavingURI and should
    // not be used as a parameter for the new load.
    delete aOpenParams.ignoreFragment;
    delete aOpenParams.ignoreQueryString;
    delete aOpenParams.replaceQueryString;
    delete aOpenParams.adoptIntoActiveWindow;

    let isBrowserWindow = !!window.gBrowser;

    // This will switch to the tab in aWindow having aURI, if present.
    function switchIfURIInWindow(aWindow) {
      // We can switch tab only if if both the source and destination windows have
      // the same private-browsing status.
      if (
        !kPrivateBrowsingURLs.has(aURI.spec) &&
        PrivateBrowsingUtils.isWindowPrivate(window) !==
          PrivateBrowsingUtils.isWindowPrivate(aWindow)
      ) {
        return false;
      }

      // Remove the query string, fragment, both, or neither from a given url.
      function cleanURL(url, removeQuery, removeFragment) {
        let ret = url;
        if (removeFragment) {
          ret = ret.split("#")[0];
          if (removeQuery) {
            // This removes a query, if present before the fragment.
            ret = ret.split("?")[0];
          }
        } else if (removeQuery) {
          // This is needed in case there is a fragment after the query.
          let fragment = ret.split("#")[1];
          ret = ret
            .split("?")[0]
            .concat(fragment != undefined ? "#".concat(fragment) : "");
        }
        return ret;
      }

      // Need to handle nsSimpleURIs here too (e.g. about:...), which don't
      // work correctly with URL objects - so treat them as strings
      let ignoreFragmentWhenComparing =
        typeof ignoreFragment == "string" &&
        ignoreFragment.startsWith("whenComparing");
      let requestedCompare = cleanURL(
        aURI.displaySpec,
        ignoreQueryString || replaceQueryString,
        ignoreFragmentWhenComparing
      );
      let browsers = aWindow.gBrowser.browsers;
      for (let i = 0; i < browsers.length; i++) {
        let browser = browsers[i];
        let browserCompare = cleanURL(
          browser.currentURI.displaySpec,
          ignoreQueryString || replaceQueryString,
          ignoreFragmentWhenComparing
        );
        let browserUserContextId = browser.getAttribute("usercontextid") || "";
        if (aUserContextId != null && aUserContextId != browserUserContextId) {
          continue;
        }
        if (requestedCompare == browserCompare) {
          // If adoptIntoActiveWindow is set, and this is a cross-window switch,
          // adopt the tab into the current window, after the active tab.
          let doAdopt =
            adoptIntoActiveWindow && isBrowserWindow && aWindow != window;

          if (doAdopt) {
            const newTab = window.gBrowser.adoptTab(
              aWindow.gBrowser.getTabForBrowser(browser),
              {
                tabIndex: window.gBrowser.tabContainer.selectedIndex + 1,
                selectTab: true,
              }
            );
            if (!newTab) {
              doAdopt = false;
            }
          }
          if (!doAdopt) {
            aWindow.focus();
          }

          if (
            ignoreFragment == "whenComparingAndReplace" ||
            replaceQueryString
          ) {
            browser.loadURI(aURI, {
              triggeringPrincipal:
                aOpenParams.triggeringPrincipal ||
                _createNullPrincipalFromTabUserContextId(),
            });
          }

          if (!doAdopt) {
            if (aSplitView) {
              let tabToMove = aWindow.gBrowser.tabs[i];
              if (aSplitView.tabs.includes(tabToMove)) {
                aWindow.gBrowser.selectedTab = tabToMove;
              } else {
                let tabToReplace = aSplitView.tabs.find(tab => tab.selected);
                aSplitView.replaceTab(tabToReplace, tabToMove);
              }
              aSplitView.documentGlobal.focus();
            } else {
              aWindow.gBrowser.tabContainer.selectedIndex = i;
            }
          }

          return true;
        }
      }
      return false;
    }

    // This can be passed either nsIURI or a string.
    if (!(aURI instanceof Ci.nsIURI)) {
      aURI = Services.io.newURI(aURI);
    }

    // Prioritise this window.
    if (isBrowserWindow && switchIfURIInWindow(window)) {
      return true;
    }

    for (let browserWin of lazy.BrowserWindowTracker.orderedWindows) {
      // Skip closed (but not yet destroyed) windows,
      // and the current window (which was checked earlier).
      if (browserWin.closed || browserWin == window) {
        continue;
      }
      if (switchIfURIInWindow(browserWin)) {
        return true;
      }
    }

    // No opened tab has that url.
    if (aOpenNew) {
      if (aUserContextId != null) {
        aOpenParams.userContextId = aUserContextId;
      }
      if (isBrowserWindow && window.gBrowser.selectedTab.isEmpty) {
        this.openTrustedLinkIn(window, aURI.spec, "current", aOpenParams);
      } else {
        this.openTrustedLinkIn(window, aURI.spec, "tab", aOpenParams);
      }
    }

    return false;
  },
};
