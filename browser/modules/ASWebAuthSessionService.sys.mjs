/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

const EPHEMERAL_CONTAINER_PREFIX = "ephemeral-aswebauth-";

class ASWebAuthSession {
  constructor(
    service,
    {
      request,
      uuid,
      win,
      browser,
      callbackScheme,
      userContextId,
      hasCallback,
      headers,
    }
  ) {
    this.service = service;
    this.request = request;
    this.uuid = uuid;
    this.window = win;
    this.browser = browser;
    this.callbackScheme = callbackScheme;
    this.userContextId = userContextId;
    this.hasCallback = hasCallback;
    this.headers = headers;

    this.completed = false;
    this.trackedBrowsers = new Set([browser]);
    this.observingHeaders = false;
  }

  start() {
    this.service.activeSessions.set(this.uuid, this);
    this.window.gBrowser.tabContainer.addEventListener("TabClose", this);
    this.window.addEventListener("unload", this);
    this.addHeaderObserver();
  }

  cleanup() {
    this.removeHeaderObserver();

    if (this.window?.gBrowser) {
      this.window.gBrowser.tabContainer.removeEventListener("TabClose", this);
    }

    if (this.window && !this.window.closed) {
      this.window.removeEventListener("unload", this);
    }

    if (this.userContextId) {
      lazy.ContextualIdentityService.remove(this.userContextId);
    }
  }

  // Shared teardown for both complete() and cancel().
  finish({ closeWindow = true } = {}) {
    this.service.activeSessions.delete(this.uuid);
    this.cleanup();

    if (closeWindow && this.window && !this.window.closed) {
      // The auth window is chromeless and dedicated to this request, so closing
      // the whole window loses no other tabs.
      this.window.close();
    }
  }

  // Success path: complete the native request, then tear down.
  complete(callbackURL) {
    if (this.completed) {
      return;
    }

    this.completed = true;
    this.request.complete(callbackURL);
    this.finish();
  }

  // Cancellation path: tell the requesting app, then tear down. The reply has
  // to go to the request object the native handler kept from the begin call,
  // which is what nsIASWebAuthSessionRequest::cancel() uses.
  cancel(options) {
    if (this.completed) {
      return;
    }

    this.completed = true;
    this.request.cancel();
    this.finish(options);
  }

  handleEvent(event) {
    switch (event.type) {
      case "TabClose":
        this.onTabClose(event);
        break;
      case "unload":
        this.onWindowUnload();
        break;
    }
  }

  observe(subject) {
    let channel = subject.QueryInterface(Ci.nsIHttpChannel);
    let bc = channel.loadInfo?.browsingContext;
    let browser = bc?.top?.embedderElement;

    // Only the top-level page load in this session's own auth tab.
    if (
      !channel.isDocument ||
      !bc ||
      bc !== bc.top ||
      !this.hasBrowser(browser)
    ) {
      return;
    }
    this.removeHeaderObserver();

    for (let [name, value] of Object.entries(this.headers)) {
      // Don't overwrite headers the browser already sends.
      try {
        channel.getRequestHeader(name);
        continue;
      } catch (e) {
        // getRequestHeader throws NS_ERROR_NOT_AVAILABLE when the header isn't set.
      }
      try {
        channel.setRequestHeader(name, value, false);
      } catch (e) {
        // The app supplied a name or value the channel rejects, skip it.
      }
    }
  }

  onTabClose(event) {
    let browser = event.target.linkedBrowser;
    if (!this.trackedBrowsers.delete(browser)) {
      return;
    }

    if (
      !this.completed &&
      (browser === this.browser || !this.hasOpenTrackedBrowser())
    ) {
      this.cancel();
    }
  }

  onWindowUnload() {
    if (this.service.activeSessions.get(this.uuid) === this) {
      this.cancel({ closeWindow: false });
    }
  }

  addHeaderObserver() {
    if (!this.headers || !Object.keys(this.headers).length) {
      return;
    }

    this.observingHeaders = true;
    Services.obs.addObserver(this, "http-on-modify-request");
  }

  removeHeaderObserver() {
    if (!this.observingHeaders) {
      return;
    }

    Services.obs.removeObserver(this, "http-on-modify-request");
    this.observingHeaders = false;
  }

  trackBrowserIfInSessionWindow(browser) {
    if (this.hasBrowser(browser)) {
      this.trackedBrowsers.add(browser);
    }
  }

  hasOpenTrackedBrowser() {
    for (let browser of this.trackedBrowsers) {
      let tab = this.window?.gBrowser?.getTabForBrowser(browser);
      if (tab && !tab.closing) {
        return true;
      }
    }
    return false;
  }

  hasBrowser(browser) {
    return (
      browser?.documentGlobal === this.window &&
      !!this.window?.gBrowser?.getTabForBrowser(browser)
    );
  }

  ownsBrowsingContext(bc) {
    let visited = new Set();
    for (let top = bc?.top; top && !visited.has(top); ) {
      visited.add(top);
      if (this.hasBrowser(top.embedderElement)) {
        return true;
      }

      let opener = top.crossGroupOpener ?? top.opener;
      top = opener?.top;
    }
    return false;
  }
}

export const ASWebAuthSessionService = new (class ASWebAuthSessionService {
  constructor() {
    this.activeSessions = new Map();
    this.pendingSetups = new Map();
    this.initialized = false;

    this.progressListener = {
      onLocationChange: (browser, webProgress, _webRequest, location) =>
        this.onLocationChange(browser, webProgress, location),
    };
  }

  init() {
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    Services.obs.addObserver(this, "http-on-examine-response");
    Services.obs.addObserver(this, "aswebauthsession-request-begin");
    Services.obs.addObserver(this, "aswebauthsession-request-cancel");
    Services.obs.addObserver(this, "aswebauthsession-native-ready");

    lazy.EveryWindow.registerCallback(
      "ASWebAuthSessionService",
      win => win?.gBrowser?.addTabsProgressListener(this.progressListener),
      win => win?.gBrowser?.removeTabsProgressListener(this.progressListener)
    );

    // Let the native handler replay requests that arrived before browser UI
    // startup installed this observer.
    Services.obs.notifyObservers(null, "aswebauthsession-service-ready");
  }

  uninit() {
    if (!this.initialized) {
      return;
    }
    this.initialized = false;

    Services.obs.removeObserver(this, "http-on-examine-response");
    Services.obs.removeObserver(this, "aswebauthsession-request-begin");
    Services.obs.removeObserver(this, "aswebauthsession-request-cancel");
    Services.obs.removeObserver(this, "aswebauthsession-native-ready");

    lazy.EveryWindow.unregisterCallback("ASWebAuthSessionService");

    for (let session of Array.from(this.activeSessions.values())) {
      session.cancel();
    }

    // Cancel setups that are still opening their window so their native
    // requests don't outlive shutdown.
    for (let pending of this.pendingSetups.values()) {
      if (!pending.cancelled) {
        pending.cancelled = true;
        pending.request.cancel();
      }
    }
    this.pendingSetups.clear();

    Services.obs.notifyObservers(null, "aswebauthsession-service-shutdown");
  }

  // Ephemeral containers only outlive their session when Firefox went away
  // without running cleanup. Not called from init(): getPublicIdentities()
  // would force the synchronous containers.json read onto the startup path,
  // while load() reads it off the main thread.
  async removeLeftoverEphemeralContainers() {
    await lazy.ContextualIdentityService.load();

    for (let identity of lazy.ContextualIdentityService.getPublicIdentities()) {
      if (!identity.name?.startsWith(EPHEMERAL_CONTAINER_PREFIX)) {
        continue;
      }
      // A request that arrived while this was waiting owns its container.
      let uuid = identity.name.slice(EPHEMERAL_CONTAINER_PREFIX.length);
      if (this.activeSessions.has(uuid) || this.pendingSetups.has(uuid)) {
        continue;
      }
      lazy.ContextualIdentityService.remove(identity.userContextId);
    }
  }

  // Handle request notifications from the native handler and response
  // notifications used to catch redirect callbacks.
  observe(subject, topic, data) {
    switch (topic) {
      case "http-on-examine-response":
        this.onExamineResponse(subject);
        break;
      case "aswebauthsession-request-begin":
        this.onBegin(subject);
        break;
      case "aswebauthsession-request-cancel":
        this.onCancel(data);
        break;
      case "aswebauthsession-native-ready":
        Services.obs.notifyObservers(null, "aswebauthsession-service-ready");
        break;
    }
  }

  shouldLoadCallback(contentLocation, loadInfo) {
    if (!this.activeSessions.size) {
      return Ci.nsIContentPolicy.ACCEPT;
    }

    let contentPolicyType = loadInfo.externalContentPolicyType;
    if (
      contentPolicyType !== Ci.nsIContentPolicy.TYPE_DOCUMENT &&
      contentPolicyType !== Ci.nsIContentPolicy.TYPE_SUBDOCUMENT
    ) {
      return Ci.nsIContentPolicy.ACCEPT;
    }

    let scheme = contentLocation.scheme?.toLowerCase();
    if (!scheme || scheme === "http" || scheme === "https") {
      return Ci.nsIContentPolicy.ACCEPT;
    }

    let session = this.findSessionForCallbackScheme(
      loadInfo.browsingContext,
      scheme
    );
    if (!session) {
      return Ci.nsIContentPolicy.ACCEPT;
    }

    if (contentPolicyType !== Ci.nsIContentPolicy.TYPE_DOCUMENT) {
      return Ci.nsIContentPolicy.REJECT_POLICY;
    }

    let callbackURL = contentLocation.spec;
    // Avoid closing the window from inside the content policy check.
    Services.tm.dispatchToMainThread(() => {
      if (this.activeSessions.get(session.uuid) === session) {
        session.complete(callbackURL);
      }
    });
    return Ci.nsIContentPolicy.REJECT_POLICY;
  }

  onExamineResponse(subject) {
    if (!this.activeSessions.size) {
      return;
    }

    try {
      let channel = subject.QueryInterface(Ci.nsIHttpChannel);
      let bc = channel.loadInfo?.browsingContext;

      // Auth callbacks are matched against main-frame navigations, so ignore
      // subframe loads.
      if (!channel.isDocument || !bc || bc !== bc.top) {
        return;
      }

      let status = channel.responseStatus;
      if (status < 300 || status >= 400) {
        return;
      }

      let location = channel.getResponseHeader("Location");
      if (!location) {
        return;
      }

      let locationUri;
      try {
        locationUri = Services.io.newURI(location);
      } catch (e) {
        return;
      }

      let session = this.findSessionForCallbackScheme(bc, locationUri.scheme);
      if (!session) {
        return;
      }

      channel.cancel(Cr.NS_BINDING_ABORTED);
      let callbackURL = location;
      // Defer so we don't tear the window down from inside the
      // http-on-examine-response notification.
      Services.tm.dispatchToMainThread(() => {
        if (this.activeSessions.get(session.uuid) === session) {
          session.complete(callbackURL);
        }
      });
    } catch (e) {
      // Not an HTTP channel or missing Location header, ignore.
    }
  }

  onLocationChange(browser, webProgress, location) {
    if (!this.activeSessions.size || !location || !webProgress?.isTopLevel) {
      return;
    }

    let scheme = location.scheme?.toLowerCase();
    if (!scheme) {
      return;
    }

    let bc = webProgress.browsingContext;
    if (scheme === "https" || scheme === "http") {
      for (let session of this.activeSessions.values()) {
        if (session.hasCallback && session.ownsBrowsingContext(bc)) {
          session.trackBrowserIfInSessionWindow(browser);
          if (session.request.matchesCallbackURL(location.spec)) {
            let callbackURL = location.spec;
            Services.tm.dispatchToMainThread(() => {
              if (this.activeSessions.get(session.uuid) === session) {
                session.complete(callbackURL);
              }
            });
          }
          break;
        }
      }
      return;
    }

    let session = this.findSessionForCallbackScheme(bc, scheme);
    if (!session) {
      return;
    }

    session.trackBrowserIfInSessionWindow(browser);
    let callbackURL = location.spec;
    // Avoid closing the window from inside the onLocationChange callback.
    Services.tm.dispatchToMainThread(() => {
      if (this.activeSessions.get(session.uuid) === session) {
        session.complete(callbackURL);
      }
    });
  }

  findSessionForCallbackScheme(bc, callbackScheme) {
    for (let session of this.activeSessions.values()) {
      if (
        session.callbackScheme === callbackScheme &&
        session.ownsBrowsingContext(bc)
      ) {
        return session;
      }
    }
    return null;
  }

  waitForWindowClosed(win) {
    if (!win || win.closed) {
      return Promise.resolve();
    }

    return new Promise(resolve => {
      function windowCloseObserver(subject, topic) {
        if (topic === "domwindowclosed" && subject === win) {
          Services.ww.unregisterNotification(windowCloseObserver);
          resolve();
        }
      }

      Services.ww.registerNotification(windowCloseObserver);
      if (win.closed) {
        Services.ww.unregisterNotification(windowCloseObserver);
        resolve();
      }
    });
  }

  observeNextWindowOpen() {
    let { promise, resolve } = Promise.withResolvers();
    let windowOpenObserver = (subject, topic) => {
      if (topic === "domwindowopened") {
        Services.ww.unregisterNotification(windowOpenObserver);
        resolve(subject);
      }
    };

    Services.ww.registerNotification(windowOpenObserver);
    return {
      promise,
      unregister() {
        Services.ww.unregisterNotification(windowOpenObserver);
      },
    };
  }

  async openAuthWindow(url, { ephemeral, userContextId }) {
    let { promise: browserPromise, resolve: resolveBrowser } =
      Promise.withResolvers();
    let sourceWindow =
      lazy.BrowserWindowTracker.getTopWindow({ private: !!ephemeral }) ||
      Services.appShell.hiddenDOMWindow;
    let screen = sourceWindow.screen;
    let width = Math.floor(Math.min(600, screen.availWidth * 0.9));
    let height = Math.floor(Math.min(700, screen.availHeight * 0.9));
    let openedWindow = this.observeNextWindowOpen();

    try {
      lazy.URILoadingHelper.openWebLinkIn(sourceWindow, url, "chromeless", {
        private: !!ephemeral,
        forceNonPrivate: !ephemeral,
        fromExternal: true,
        userContextId,
        resolveOnContentBrowserCreated: resolveBrowser,
        aswebauth: true,
        width,
        height,
        shouldCenter: true,
      });
    } catch (e) {
      openedWindow.unregister();
      throw e;
    }

    let win = await openedWindow.promise;

    // Wait for the browser to be created, but cancel setup if the window is closed first.
    let browser = await Promise.race([
      browserPromise,
      this.waitForWindowClosed(win).then(() => null),
    ]);

    if (browser?.documentGlobal !== win) {
      return { win, browser: null };
    }

    return { win, browser };
  }

  cleanupFailedSetup(pending, win, userContextId) {
    if (userContextId) {
      lazy.ContextualIdentityService.remove(userContextId);
    }
    if (win && !win.closed) {
      win.close();
    }
    if (!pending.cancelled) {
      pending.request.cancel();
    }
  }

  async onBegin(request) {
    request = request.QueryInterface(Ci.nsIASWebAuthSessionRequest);

    let uuid = request.uuid;
    let callbackScheme = request.callbackScheme.toLowerCase();
    let hasCallback = request.hasCallback;
    let ephemeral = request.useEphemeralSession;

    let headers = {};
    for (let name of request.additionalHeaderNames) {
      headers[name] = request.getAdditionalHeader(name);
    }

    let parsedURL = URL.parse(request.url);
    if (!uuid || parsedURL?.protocol !== "https:") {
      request.cancel();
      return;
    }

    let pending = { cancelled: false, request };
    this.pendingSetups.set(uuid, pending);

    let userContextId = 0;
    let win = null;
    try {
      // Wait for the startup windows, including restored ones, to open first so
      // that the auth window opens after them and appears in front.
      await lazy.SessionStore.promiseAllWindowsRestored;
      if (pending.cancelled) {
        return;
      }

      if (ephemeral) {
        let container = await lazy.ContextualIdentityService.create(
          EPHEMERAL_CONTAINER_PREFIX + uuid,
          "fingerprint",
          "blue"
        );
        userContextId = container.userContextId;
      }

      let result = await this.openAuthWindow(parsedURL.href, {
        ephemeral,
        userContextId,
      });
      win = result.win;

      if (!result.browser || win.closed || pending.cancelled) {
        this.cleanupFailedSetup(pending, win, userContextId);
        return;
      }
      win.focus();

      let session = new ASWebAuthSession(this, {
        request,
        uuid,
        win,
        browser: result.browser,
        callbackScheme,
        userContextId,
        hasCallback,
        headers,
      });
      session.start();
      userContextId = 0;
    } catch (e) {
      console.error("Failed to begin ASWebAuth request", e);
      this.cleanupFailedSetup(pending, win, userContextId);
    } finally {
      this.pendingSetups.delete(uuid);
    }
  }

  onCancel(uuid) {
    let session = this.activeSessions.get(uuid);
    if (session) {
      session.cancel();
      return;
    }

    let pending = this.pendingSetups.get(uuid);
    if (pending && !pending.cancelled) {
      pending.cancelled = true;
      pending.request.cancel();
    }
  }
})();

// Registered via components.conf. Catches custom-scheme callbacks
// (myapp://...) that don't hit the network, so the observers can't see them.
export function ASWebAuthSessionCallbackContentPolicy() {}

ASWebAuthSessionCallbackContentPolicy.prototype = {
  classDescription: "ASWebAuthSession callback scheme content policy",
  contractID: "@mozilla.org/aswebauthsession-content-policy;1",
  classID: Components.ID("{59826a4f-abcf-4d1f-b049-c236572bc823}"),

  QueryInterface: ChromeUtils.generateQI(["nsIContentPolicy"]),

  shouldLoad(contentLocation, loadInfo) {
    return ASWebAuthSessionService.shouldLoadCallback(
      contentLocation,
      loadInfo
    );
  },

  shouldProcess() {
    return Ci.nsIContentPolicy.ACCEPT;
  },
};
