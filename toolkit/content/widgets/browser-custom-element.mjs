/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// TODO: Bug 1994968 - Fix most TypeScript issues in this file. Currently there
// are lots of errors that may show up in an editor due to our TypeScript
// configuration. Skip this for now, until these are resolved.

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ContentAnalysisUtils: "resource://gre/modules/ContentAnalysisUtils.sys.mjs",
  Finder: "resource://gre/modules/Finder.sys.mjs",
  FinderParent: "resource://gre/modules/FinderParent.sys.mjs",
  PopupAndRedirectBlocker:
    "resource://gre/actors/PopupAndRedirectBlockingParent.sys.mjs",
  SelectParentHelper: "resource://gre/actors/SelectParent.sys.mjs",
  RemoteWebNavigation:
    "moz-src:///toolkit/components/remotebrowserutils/RemoteWebNavigation.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "blankURI", () =>
  Services.io.newURI("about:blank")
);

let lazyPrefs = {};
XPCOMUtils.defineLazyPreferenceGetter(
  lazyPrefs,
  "unloadTimeoutMs",
  "dom.beforeunload_timeout_ms"
);

Object.defineProperty(lazy, "ProcessHangMonitor", {
  configurable: true,
  get() {
    // Import if we can - this is a browser/ module so it may not be
    // available, in which case we return null. We replace this getter
    // when the module becomes available (should be on delayed startup
    // when the first browser window loads, via BrowserGlue.sys.mjs).
    const kURL = "resource:///modules/ProcessHangMonitor.sys.mjs";
    if (Cu.isESModuleLoaded(kURL)) {
      let { ProcessHangMonitor } = ChromeUtils.importESModule(kURL);
      // eslint-disable-next-line mozilla/valid-lazy
      Object.defineProperty(lazy, "ProcessHangMonitor", {
        value: ProcessHangMonitor,
      });
      return ProcessHangMonitor;
    }
    return null;
  },
});

// Get SessionStore module in the same as ProcessHangMonitor above.
Object.defineProperty(lazy, "SessionStore", {
  configurable: true,
  get() {
    const kURL =
      "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs";
    if (Cu.isESModuleLoaded(kURL)) {
      let { SessionStore } = ChromeUtils.importESModule(kURL);
      // eslint-disable-next-line mozilla/valid-lazy
      Object.defineProperty(lazy, "SessionStore", {
        value: SessionStore,
      });
      return SessionStore;
    }
    return null;
  },
});

const elementsToDestroyOnUnload = new Set();

window.addEventListener(
  "unload",
  () => {
    for (let element of elementsToDestroyOnUnload.values()) {
      element.destroy();
    }
    elementsToDestroyOnUnload.clear();
  },
  { mozSystemGroup: true, once: true }
);

/**
 * @implements {nsIBrowser}
 */
export class MozBrowser extends MozElements.MozElementMixin(XULFrameElement) {
  static get observedAttributes() {
    return ["remote"];
  }

  constructor() {
    super();

    this.onPageHide = this.onPageHide.bind(this);

    this.isNavigating = false;

    this._documentURI = null;
    this._characterSet = null;
    this._documentContentType = null;

    this._inPermitUnload = new WeakSet();

    this._originalURI = null;
    this._searchTerms = "";
    // When we open a prompt in reaction to a 401, if this 401 comes from
    // a different base domain, the url of that site will be stored here
    // and will be used for auth prompt spoofing protections.
    // See bug 791594 for reference.
    this._currentAuthPromptURI = null;
    /**
     * These are managed by the tabbrowser:
     */
    this.droppedLinkHandler = null;
    this.mIconURL = null;
    this.lastURI = null;

    ChromeUtils.defineLazyGetter(this, "popupAndRedirectBlocker", () => {
      return new lazy.PopupAndRedirectBlocker(this);
    });

    this.addEventListener(
      "dragover",
      event => {
        if (!this.droppedLinkHandler || event.defaultPrevented) {
          return;
        }

        // For drags that appear to be internal text (for example, tab drags),
        // set the dropEffect to 'none'. This prevents the drop even if some
        // other listener cancelled the event.
        var types = event.dataTransfer.types;
        if (
          types.includes("text/x-moz-text-internal") &&
          !types.includes("text/plain")
        ) {
          event.dataTransfer.dropEffect = "none";
          event.stopPropagation();
          event.preventDefault();
        }

        // No need to handle "dragover" in e10s, since nsDocShellTreeOwner.cpp in the child process
        // handles that case using "@mozilla.org/content/dropped-link-handler;1" service.
        if (this.isRemoteBrowser) {
          return;
        }

        let linkHandler = Services.droppedLinkHandler;
        if (linkHandler.canDropLink(event, false)) {
          event.preventDefault();
        }
      },
      { mozSystemGroup: true }
    );

    this.addEventListener(
      "drop",
      event => {
        const contentAnalysis = Cc["@mozilla.org/contentanalysis;1"].getService(
          Ci.nsIContentAnalysis
        );
        if (contentAnalysis.isActive) {
          let dragService = Cc["@mozilla.org/widget/dragservice;1"].getService(
            Ci.nsIDragService
          );
          let dragSession = dragService.getCurrentSession(window);
          if (!dragSession) {
            return;
          }

          try {
            // Submit a content analysis request for the DataTransfer and
            // stop dispatching this drop event.  Reissue the drop if all
            // requests are permitted, otherwise issue a dragexit.
            let request =
              lazy.ContentAnalysisUtils.createContentAnalysisRequest(
                {
                  analysisType: Ci.nsIContentAnalysisRequest.eBulkDataEntry,
                  operationTypeForDisplay:
                    Ci.nsIContentAnalysisRequest.eDroppedText,
                  reason: Ci.nsIContentAnalysisRequest.eDragAndDrop,
                  url: contentAnalysis.getURIForDropEvent(event),
                  windowGlobalParent: this.browsingContext.currentWindowContext,
                },
                {
                  dataTransfer: event.dataTransfer,
                  sourceWindowGlobal: dragSession.sourceWindowContext,
                }
              );

            // Tell browser to record the event target and to delay EndDragSession
            // until the content analysis results are given.
            dragSession.sendStoreDropTargetAndDelayEndDragSession(event);

            contentAnalysis.analyzeBatchContentRequest(request, true).then(
              caResult => {
                let shouldAllowContent = true;
                if (caResult.length > 1) {
                  shouldAllowContent = caResult[1].shouldAllowContent;
                }
                dragSession.sendDispatchToDropTargetAndResumeEndDragSession(
                  shouldAllowContent,
                  caResult[0]
                );
              },
              () => {
                dragSession.sendDispatchToDropTargetAndResumeEndDragSession(
                  false,
                  []
                );
              }
            );

            // Do not allow this drop to continue dispatch.
            event.preventDefault();
            event.stopPropagation();
          } catch (e) {
            console.error(`content analysis dnd error: ${e}`);

            // On internal error, deny any drop.  CA has its own behavior to
            // handle internal errors, like a lost connection to the agent, but
            // we are more strict when facing errors here.
            event.preventDefault();
            event.stopPropagation();
          }
        }

        // No need to handle "drop" in e10s, since nsDocShellTreeOwner.cpp in the child process
        // handles that case using "@mozilla.org/content/dropped-link-handler;1" service.
        if (
          !this.droppedLinkHandler ||
          event.defaultPrevented ||
          this.isRemoteBrowser
        ) {
          return;
        }

        let linkHandler = Services.droppedLinkHandler;
        try {
          if (!linkHandler.canDropLink(event, false)) {
            return;
          }

          // Pass true to prevent the dropping of javascript:/data: URIs
          var links = linkHandler.dropLinks(event, true);
        } catch (ex) {
          return;
        }

        if (links.length) {
          let triggeringPrincipal = linkHandler.getTriggeringPrincipal(event);
          this.droppedLinkHandler(event, links, triggeringPrincipal);
        }
      },
      { mozSystemGroup: true }
    );

    this.addEventListener("dragstart", event => {
      // If we're a remote browser dealing with a dragstart, stop it
      // from propagating up, since our content process should be dealing
      // with the mouse movement.
      if (this.isRemoteBrowser) {
        event.stopPropagation();
      }
    });
  }

  /**
   * The browser's permanent key. This was added temporarily for Session Store,
   * and will be removed in bug 1716788.
   *
   * @type {any}
   */
  permanentKey;

  resetFields() {
    if (this.observer) {
      try {
        Services.obs.removeObserver(
          this.observer,
          "browser:purge-session-history"
        );
      } catch (ex) {
        // It's not clear why this sometimes throws an exception.
      }
      this.observer = null;
    }

    let browser = this;
    this.observer = {
      observe(aSubject, aTopic, aState) {
        if (aTopic == "browser:purge-session-history") {
          browser.purgeSessionHistory();
        } else if (aTopic == "apz:cancel-autoscroll") {
          if (aState == browser._autoScrollScrollId) {
            // Set this._autoScrollScrollId to null, so in stopScroll() we
            // don't call stopApzAutoscroll() (since it's APZ that
            // initiated the stopping).
            browser._autoScrollScrollId = null;
            browser._autoScrollPresShellId = null;

            browser._autoScrollPopup.hidePopup();
          }
        }
      },
      QueryInterface: ChromeUtils.generateQI([
        "nsIObserver",
        "nsISupportsWeakReference",
      ]),
    };

    this._documentURI = null;

    this._originalURI = null;

    this._currentAuthPromptURI = null;

    this._searchTerms = "";

    this._documentContentType = null;

    this._loadContext = null;

    this._webBrowserFind = null;

    this._finder = null;

    this._remoteFinder = null;

    this._fastFind = null;

    this._lastSearchString = null;

    this._characterSet = "";

    this._mayEnableCharacterEncodingMenu = null;

    this._contentPrincipal = null;

    this._contentPartitionedPrincipal = null;

    this._policyContainer = null;

    this._referrerInfo = null;

    this._contentRequestContextID = null;

    this._rdmFullZoom = 1.0;

    this._isSyntheticDocument = false;

    this.mPrefs = Services.prefs;

    this._hasAnyPlayingMediaBeenBlocked = false;

    this._unselectedTabHoverMessageListenerCount = 0;

    this.urlbarChangeTracker = {
      _startedLoadSinceLastUserTyping: false,

      startedLoad() {
        this._startedLoadSinceLastUserTyping = true;
      },
      finishedLoad() {
        this._startedLoadSinceLastUserTyping = false;
      },
      userTyped() {
        this._startedLoadSinceLastUserTyping = false;
      },
    };

    this._userTypedValue = null;

    this._AUTOSCROLL_SNAP = 10;

    this._autoScrollBrowsingContext = null;

    this._startX = null;

    this._startY = null;

    this._autoScrollPopup = null;

    /**
     * These IDs identify the scroll frame being autoscrolled.
     */
    this._autoScrollScrollId = null;

    this._autoScrollPresShellId = null;
  }

  connectedCallback() {
    // We typically use this to avoid running JS that triggers a layout during parse
    // (see comment on the delayConnectedCallback implementation). In this case, we
    // are using it to avoid a leak - see https://bugzilla.mozilla.org/show_bug.cgi?id=1441935#c20.
    if (this.delayConnectedCallback()) {
      return;
    }

    this.construct();
  }

  connectedMoveCallback() {
    // No-op: Allows callers to move <browser> element in the DOM tree
    // without destruct() + construct(). This here is merely an optimization.
    //
    // For the content to be available (and not unexpectedly destroyed),
    // XULFrameElement::BindToTree and XULFrameElement::UnbindToTree skips
    // frame loader construction/reconstruction on move (bug 2007742).
  }

  disconnectedCallback() {
    this.destroy();
  }

  get autoscrollEnabled() {
    if (this.getAttribute("autoscroll") == "false") {
      return false;
    }

    return this.mPrefs.getBoolPref("general.autoScroll", true);
  }

  get canGoBack() {
    return this.webNavigation.canGoBack;
  }

  get canGoBackIgnoringUserInteraction() {
    return this.webNavigation.canGoBackIgnoringUserInteraction;
  }

  get canGoForward() {
    return this.webNavigation.canGoForward;
  }

  // While an auth prompt from a base domain different than the current sites is open, we want to display the url of the cross domain site.
  // This is to prevent possible auth spoofing scenarios.
  // The URL of the requesting origin is provided by 'currentAuthPromptURI', this will only be non null while an auth prompt is open.
  // See bug 791594 for reference.
  get currentURI() {
    if (this.currentAuthPromptURI) {
      return this.currentAuthPromptURI;
    }
    if (this.webNavigation) {
      return this.webNavigation.currentURI;
    }
    return null;
  }

  get documentURI() {
    return this.isRemoteBrowser
      ? this._documentURI
      : this.contentDocument?.documentURIObject;
  }

  get documentContentType() {
    if (this.isRemoteBrowser) {
      return this._documentContentType;
    }
    return this.contentDocument ? this.contentDocument.contentType : null;
  }

  set documentContentType(aContentType) {
    if (aContentType != null) {
      if (this.isRemoteBrowser) {
        this._documentContentType = aContentType;
      } else {
        this.contentDocument.documentContentType = aContentType;
      }
    }
  }

  get loadContext() {
    if (this._loadContext) {
      return this._loadContext;
    }

    let { frameLoader } = this;
    if (!frameLoader) {
      return null;
    }
    this._loadContext = frameLoader.loadContext;
    return this._loadContext;
  }

  get autoCompletePopup() {
    return document.getElementById(this.getAttribute("autocompletepopup"));
  }

  set suspendMediaWhenInactive(val) {
    this.browsingContext.suspendMediaWhenInactive = val;
  }

  get suspendMediaWhenInactive() {
    return !!this.browsingContext?.suspendMediaWhenInactive;
  }

  set docShellIsActive(val) {
    if (!this.browsingContext) {
      return;
    }
    this.browsingContext.isActive = val;
    if (this.isRemoteBrowser) {
      let remoteTab = this.frameLoader?.remoteTab;
      if (remoteTab) {
        remoteTab.renderLayers = val;
      }
    }
  }

  get docShellIsActive() {
    return !!this.browsingContext?.isActive;
  }

  set renderLayers(val) {
    if (this.isRemoteBrowser) {
      let remoteTab = this.frameLoader?.remoteTab;
      if (remoteTab) {
        remoteTab.renderLayers = val;
      }
    } else {
      this.docShellIsActive = val;
    }
  }

  get renderLayers() {
    if (this.isRemoteBrowser) {
      return !!this.frameLoader?.remoteTab?.renderLayers;
    }
    return this.docShellIsActive;
  }

  get hasLayers() {
    if (this.isRemoteBrowser) {
      return !!this.frameLoader?.remoteTab?.hasLayers;
    }
    return this.docShellIsActive;
  }

  get isRemoteBrowser() {
    return this.hasAttribute("remote");
  }

  get remoteType() {
    return this.browsingContext?.currentRemoteType;
  }

  get isCrashed() {
    if (!this.isRemoteBrowser || !this.frameLoader) {
      return false;
    }

    return !this.frameLoader.remoteTab;
  }

  get messageManager() {
    // Bug 1524084 - Trying to get at the message manager while in the crashed state will
    // create a new message manager that won't shut down properly when the crashed browser
    // is removed from the DOM. We work around that right now by returning null if we're
    // in the crashed state.
    if (this.frameLoader && !this.isCrashed) {
      return this.frameLoader.messageManager;
    }
    return null;
  }

  get webBrowserFind() {
    if (!this._webBrowserFind) {
      this._webBrowserFind = this.docShell
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIWebBrowserFind);
    }
    return this._webBrowserFind;
  }

  get finder() {
    if (this.isRemoteBrowser) {
      if (!this._remoteFinder) {
        this._remoteFinder = new lazy.FinderParent(this);
      }
      return this._remoteFinder;
    }
    if (!this._finder) {
      if (!this.docShell) {
        return null;
      }

      this._finder = new lazy.Finder(this.docShell);
    }
    return this._finder;
  }

  get fastFind() {
    if (!this._fastFind) {
      if (!("@mozilla.org/typeaheadfind;1" in Cc)) {
        return null;
      }

      var tabBrowser = this.getTabBrowser();
      if (tabBrowser && "fastFind" in tabBrowser) {
        return (this._fastFind = tabBrowser.fastFind);
      }

      if (!this.docShell) {
        return null;
      }

      this._fastFind = Cc["@mozilla.org/typeaheadfind;1"].createInstance(
        Ci.nsITypeAheadFind
      );
      this._fastFind.init(this.docShell);
    }
    return this._fastFind;
  }

  get outerWindowID() {
    return this.browsingContext?.currentWindowGlobal?.outerWindowId;
  }

  get innerWindowID() {
    return this.browsingContext?.currentWindowGlobal?.innerWindowId || null;
  }

  get browsingContext() {
    if (this.frameLoader) {
      return /** @type {CanonicalBrowsingContext} */ (
        this.frameLoader.browsingContext
      );
    }
    return null;
  }
  /**
   * Note that this overrides webNavigation on XULFrameElement, and duplicates the return value for the non-remote case
   */
  get webNavigation() {
    return this.isRemoteBrowser
      ? this._remoteWebNavigation
      : this.docShell && this.docShell.QueryInterface(Ci.nsIWebNavigation);
  }

  get webProgress() {
    return this.browsingContext?.webProgress;
  }

  get sessionHistory() {
    return this.webNavigation.sessionHistory;
  }

  get contentTitle() {
    return (
      (this.isRemoteBrowser
        ? this.browsingContext?.currentWindowGlobal?.documentTitle
        : this.contentDocument.title) ?? ""
    );
  }

  forceEncodingDetection() {
    if (this.isRemoteBrowser) {
      this.sendMessageToActor("ForceEncodingDetection", {}, "BrowserTab");
    } else {
      this.docShell.forceEncodingDetection();
    }
  }

  get characterSet() {
    return this.isRemoteBrowser ? this._characterSet : this.docShell.charset;
  }

  get mayEnableCharacterEncodingMenu() {
    return this.isRemoteBrowser
      ? this._mayEnableCharacterEncodingMenu
      : this.docShell.mayEnableCharacterEncodingMenu;
  }

  set mayEnableCharacterEncodingMenu(aMayEnable) {
    if (this.isRemoteBrowser) {
      this._mayEnableCharacterEncodingMenu = aMayEnable;
    }
  }

  get contentPrincipal() {
    return this.isRemoteBrowser
      ? this._contentPrincipal
      : this.contentDocument.nodePrincipal;
  }

  get contentPartitionedPrincipal() {
    return this.isRemoteBrowser
      ? this._contentPartitionedPrincipal
      : this.contentDocument.partitionedPrincipal;
  }

  get cookieJarSettings() {
    return this.isRemoteBrowser
      ? this.browsingContext?.currentWindowGlobal?.cookieJarSettings
      : this.contentDocument.cookieJarSettings;
  }

  get policyContainer() {
    return this.isRemoteBrowser
      ? this._policyContainer
      : this.contentDocument.policyContainer;
  }

  get contentRequestContextID() {
    if (this.isRemoteBrowser) {
      return this._contentRequestContextID;
    }
    try {
      return this.contentDocument.documentLoadGroup.requestContextID;
    } catch (e) {
      return null;
    }
  }

  get referrerInfo() {
    return this.isRemoteBrowser
      ? this._referrerInfo
      : this.contentDocument.referrerInfo;
  }

  set fullZoom(val) {
    if (val.toFixed(2) == this.fullZoom.toFixed(2)) {
      return;
    }
    if (this.browsingContext.inRDMPane) {
      this._rdmFullZoom = val;
      let event = document.createEvent("Events");
      event.initEvent("FullZoomChange", true, false);
      this.dispatchEvent(event);
    } else {
      this.browsingContext.fullZoom = val;
    }
  }

  get fullZoom() {
    if (this.browsingContext.inRDMPane) {
      return this._rdmFullZoom;
    }
    return this.browsingContext.fullZoom;
  }

  set textZoom(val) {
    if (val.toFixed(2) == this.textZoom.toFixed(2)) {
      return;
    }
    this.browsingContext.textZoom = val;
  }

  get textZoom() {
    return this.browsingContext.textZoom;
  }

  enterResponsiveMode() {
    if (this.browsingContext.inRDMPane) {
      return;
    }
    this.browsingContext.inRDMPane = true;
    this._rdmFullZoom = this.browsingContext.fullZoom;
    this.browsingContext.fullZoom = 1.0;
  }

  leaveResponsiveMode() {
    if (!this.browsingContext.inRDMPane) {
      return;
    }
    this.browsingContext.inRDMPane = false;
    this.browsingContext.fullZoom = this._rdmFullZoom;
  }

  get isSyntheticDocument() {
    if (this.isRemoteBrowser) {
      return this._isSyntheticDocument;
    }
    return this.contentDocument.mozSyntheticDocument;
  }

  get hasContentOpener() {
    return !!this.browsingContext.opener;
  }

  get audioMuted() {
    return this.browsingContext?.mediaController?.isMuted ?? false;
  }

  get shouldHandleUnselectedTabHover() {
    return this._unselectedTabHoverMessageListenerCount > 0;
  }

  set shouldHandleUnselectedTabHover(value) {
    this._unselectedTabHoverMessageListenerCount += value ? 1 : -1;
  }

  get securityUI() {
    return this.browsingContext.secureBrowserUI;
  }

  set userTypedValue(val) {
    this.urlbarChangeTracker.userTyped();
    this._userTypedValue = val;
  }

  get userTypedValue() {
    return this._userTypedValue;
  }

  get dontPromptAndDontUnload() {
    return 1;
  }

  get dontPromptAndUnload() {
    return 2;
  }

  set originalURI(aURI) {
    if (aURI instanceof Ci.nsIURI) {
      this._originalURI = aURI;
    }
  }

  get originalURI() {
    return this._originalURI;
  }

  set searchTerms(val) {
    this._searchTerms = val;
  }

  get searchTerms() {
    return this._searchTerms;
  }

  set currentAuthPromptURI(aURI) {
    this._currentAuthPromptURI = aURI;
  }

  get currentAuthPromptURI() {
    return this._currentAuthPromptURI;
  }
  _wrapURIChangeCall(fn) {
    if (!this.isRemoteBrowser) {
      this.isNavigating = true;
      try {
        fn();
      } finally {
        this.isNavigating = false;
      }
    } else {
      fn();
    }
  }

  processCloseRequest() {
    this.browsingContext.currentWindowContext?.processCloseRequest();
  }

  goBack(
    requireUserInteraction = lazy.BrowserUtils.navigationRequireUserInteraction
  ) {
    var webNavigation = this.webNavigation;
    if (
      requireUserInteraction
        ? webNavigation.canGoBack
        : webNavigation.canGoBackIgnoringUserInteraction
    ) {
      this._wrapURIChangeCall(() =>
        webNavigation.goBack(requireUserInteraction)
      );
    }
  }

  goForward(
    requireUserInteraction = lazy.BrowserUtils.navigationRequireUserInteraction
  ) {
    var webNavigation = this.webNavigation;
    if (webNavigation.canGoForward) {
      this._wrapURIChangeCall(() =>
        webNavigation.goForward(requireUserInteraction)
      );
    }
  }

  reload() {
    const nsIWebNavigation = Ci.nsIWebNavigation;
    const flags = nsIWebNavigation.LOAD_FLAGS_NONE;
    this.reloadWithFlags(flags);
  }

  reloadWithFlags(aFlags) {
    this.webNavigation.reload(aFlags);
  }

  stop() {
    const nsIWebNavigation = Ci.nsIWebNavigation;
    const flags = nsIWebNavigation.STOP_ALL;
    this.webNavigation.stop(flags);
  }

  /**
   * throws exception for unknown schemes
   */
  loadURI(uri, params = {}) {
    if (!uri) {
      uri = lazy.blankURI;
    }
    this._wrapURIChangeCall(() => this.webNavigation.loadURI(uri, params));
  }

  /**
   * throws exception for unknown schemes
   */
  fixupAndLoadURIString(uriString, params = {}) {
    if (!uriString) {
      this.loadURI(null, params);
      return;
    }
    this._wrapURIChangeCall(() =>
      this.webNavigation.fixupAndLoadURIString(uriString, params)
    );
  }

  gotoIndex(aIndex) {
    this._wrapURIChangeCall(() => this.webNavigation.gotoIndex(aIndex));
  }

  preserveLayers(preserve) {
    if (!this.isRemoteBrowser) {
      return;
    }
    let { frameLoader } = this;
    if (frameLoader.remoteTab) {
      frameLoader.remoteTab.preserveLayers(preserve);
    }
  }

  deprioritize() {
    if (!this.isRemoteBrowser) {
      return;
    }
    let { remoteTab } = this.frameLoader;
    if (remoteTab) {
      remoteTab.priorityHint = false;
      remoteTab.deprioritize();
    }
  }

  getTabBrowser() {
    if (this?.documentGlobal?.gBrowser?.getTabForBrowser(this)) {
      return this.documentGlobal.gBrowser;
    }
    return null;
  }

  addProgressListener(aListener, aNotifyMask) {
    if (!aNotifyMask) {
      aNotifyMask = Ci.nsIWebProgress.NOTIFY_ALL;
    }

    this.webProgress.addProgressListener(aListener, aNotifyMask);
  }

  removeProgressListener(aListener) {
    this.webProgress.removeProgressListener(aListener);
  }

  onPageHide() {
    // If we're browsing from the tab crashed UI to a URI that keeps
    // this browser non-remote, we'll handle that here.
    lazy.SessionStore?.maybeExitCrashedState(this);

    if (!this.docShell || !this.fastFind) {
      return;
    }
    var tabBrowser = this.getTabBrowser();
    if (
      !tabBrowser ||
      !("fastFind" in tabBrowser) ||
      tabBrowser.selectedBrowser == this
    ) {
      this.fastFind.setDocShell(this.docShell);
    }
  }

  /**
   * When the pref "media.block-autoplay-until-in-foreground" is on,
   * Gecko delays starting playback of media resources in tabs until the
   * tab has been in the foreground or resumed by tab's play tab icon.
   * - When Gecko delays starting playback of a media resource in a window,
   * it sends a message to call activeMediaBlockStarted(). This causes the
   * tab audio indicator to show.
   * - When a tab is foregrounded, Gecko starts playing all delayed media
   * resources in that tab, and sends a message to call
   * activeMediaBlockStopped(). This causes the tab audio indicator to hide.
   */
  activeMediaBlockStarted() {
    this._hasAnyPlayingMediaBeenBlocked = true;
    let event = document.createEvent("Events");
    event.initEvent("DOMAudioPlaybackBlockStarted", true, false);
    this.dispatchEvent(event);
  }

  activeMediaBlockStopped() {
    if (!this._hasAnyPlayingMediaBeenBlocked) {
      return;
    }
    this._hasAnyPlayingMediaBeenBlocked = false;
    let event = document.createEvent("Events");
    event.initEvent("DOMAudioPlaybackBlockStopped", true, false);
    this.dispatchEvent(event);
  }

  resumeMedia() {
    this.frameLoader.browsingContext.notifyStartDelayedAutoplayMedia();
    if (this._hasAnyPlayingMediaBeenBlocked) {
      this._hasAnyPlayingMediaBeenBlocked = false;
      let event = document.createEvent("Events");
      event.initEvent("DOMAudioPlaybackBlockStopped", true, false);
      this.dispatchEvent(event);
    }
  }

  unselectedTabHover(hovered) {
    if (!this.shouldHandleUnselectedTabHover) {
      return;
    }
    this.sendMessageToActor(
      "Browser:UnselectedTabHover",
      {
        hovered,
      },
      "UnselectedTabHover",
      "roots"
    );
  }

  didStartLoadSinceLastUserTyping() {
    return (
      !this.isNavigating &&
      this.urlbarChangeTracker._startedLoadSinceLastUserTyping
    );
  }

  constrainPopup(popup) {
    if (this.getAttribute("constrainpopups") != "false") {
      let constraintRect = this.getBoundingClientRect();
      constraintRect = new DOMRect(
        constraintRect.left + window.mozInnerScreenX,
        constraintRect.top + window.mozInnerScreenY,
        constraintRect.width,
        constraintRect.height
      );
      popup.setConstraintRect(constraintRect);
    } else {
      popup.setConstraintRect(new DOMRect(0, 0, 0, 0));
    }
  }

  construct() {
    elementsToDestroyOnUnload.add(this);
    this.resetFields();
    this.mInitialized = true;
    if (this.isRemoteBrowser) {
      /*
       * Don't try to send messages from this function. The message manager for
       * the <browser> element may not be initialized yet.
       */

      this._remoteWebNavigation = new lazy.RemoteWebNavigation(this);

      // Initialize contentPrincipal to the about:blank principal for this loadcontext
      let aboutBlank = Services.io.newURI("about:blank");
      let ssm = Services.scriptSecurityManager;
      this._contentPrincipal = ssm.getLoadContextContentPrincipal(
        aboutBlank,
        this.loadContext
      );
      this._contentPartitionedPrincipal = this._contentPrincipal;
      // policyContainer for about:blank is null; if we ever change _contentPrincipal above,
      // we should re-evaluate the policyContainer here.
      this._policyContainer = null;

      if (!this.hasAttribute("disablehistory")) {
        Services.obs.addObserver(
          this.observer,
          "browser:purge-session-history",
          true
        );
      }
    }

    try {
      // |webNavigation.sessionHistory| will have been set by the frame
      // loader when creating the docShell as long as this xul:browser
      // doesn't have the 'disablehistory' attribute set.
      if (this.docShell && this.webNavigation.sessionHistory) {
        Services.obs.addObserver(
          this.observer,
          "browser:purge-session-history",
          true
        );

        // enable global history if we weren't told otherwise
        if (
          !this.hasAttribute("disableglobalhistory") &&
          !this.isRemoteBrowser
        ) {
          try {
            this.docShell.browsingContext.useGlobalHistory = true;
          } catch (ex) {
            // This can occur if the Places database is locked
            console.error("Error enabling browser global history: ", ex);
          }
        }
      }
    } catch (e) {
      console.error(e);
    }
    try {
      // Ensures the securityUI is initialized.
      var securityUI = this.securityUI; // eslint-disable-line no-unused-vars
    } catch (e) {}

    if (!this.isRemoteBrowser) {
      this._remoteWebNavigation = null;
      this.addEventListener("pagehide", this.onPageHide, true);
    }
  }

  /**
   * This is necessary because custom elements don't have a "real" destructor.
   * This method is called explicitly by tabbrowser, when changing remoteness,
   * and when we're disconnected or the window unloads.
   */
  destroy() {
    elementsToDestroyOnUnload.delete(this);

    // If we're browsing from the tab crashed UI to a URI that causes the tab
    // to go remote again, we catch this here, because swapping out the
    // non-remote browser for a remote one doesn't cause the pagehide event
    // to be fired. Previously, we used to do this in the frame script's
    // unload handler.
    lazy.SessionStore?.maybeExitCrashedState(this);

    // Make sure that any open select is closed.
    let menulist = document.getElementById("ContentSelectDropdown");
    if (menulist?.open) {
      lazy.SelectParentHelper.hide(menulist, this);
    }

    this.resetFields();

    if (!this.mInitialized) {
      return;
    }

    this.mInitialized = false;
    this.lastURI = null;

    if (!this.isRemoteBrowser) {
      this.removeEventListener("pagehide", this.onPageHide, true);
    }
  }

  updateForStateChange(aCharset, aDocumentURI, aContentType) {
    if (this.isRemoteBrowser && this.messageManager) {
      if (aCharset != null) {
        this._characterSet = aCharset;
      }

      if (aDocumentURI != null) {
        this._documentURI = aDocumentURI;
      }

      if (aContentType != null) {
        this._documentContentType = aContentType;
      }
    }
  }

  updateForLocationChange(
    aLocation,
    aCharset,
    aMayEnableCharacterEncodingMenu,
    aDocumentURI,
    aTitle,
    aContentPrincipal,
    aContentPartitionedPrincipal,
    aPolicyContainer,
    aReferrerInfo,
    aIsSynthetic,
    aHaveRequestContextID,
    aRequestContextID,
    aContentType
  ) {
    if (this.isRemoteBrowser && this.messageManager) {
      if (aCharset != null) {
        this._characterSet = aCharset;
        this._mayEnableCharacterEncodingMenu = aMayEnableCharacterEncodingMenu;
      }

      if (aContentType != null) {
        this._documentContentType = aContentType;
      }

      this._remoteWebNavigation._currentURI = aLocation;
      this._documentURI = aDocumentURI;
      this._contentPrincipal = aContentPrincipal;
      this._contentPartitionedPrincipal = aContentPartitionedPrincipal;
      this._policyContainer = aPolicyContainer;
      this._referrerInfo = aReferrerInfo;
      this._isSyntheticDocument = aIsSynthetic;
      this._contentRequestContextID = aHaveRequestContextID
        ? aRequestContextID
        : null;
    }
  }

  purgeSessionHistory() {
    try {
      let sessionHistory = this.browsingContext?.sessionHistory;
      if (!sessionHistory) {
        return;
      }

      // place the entry at current index at the end of the history list, so it won't get removed
      if (sessionHistory.index < sessionHistory.count - 1) {
        let indexEntry = sessionHistory.getEntryAtIndex(sessionHistory.index);
        sessionHistory.addEntry(indexEntry, true);
      }

      let purge = sessionHistory.count;
      if (
        this.browsingContext.currentWindowGlobal.documentURI != "about:blank"
      ) {
        --purge; // Don't remove the page the user's staring at from shistory
      }

      if (purge > 0) {
        sessionHistory.purgeHistory(purge);
      }
    } catch (ex) {
      // This can throw if the browser has started to go away.
      if (ex.result != Cr.NS_ERROR_NOT_INITIALIZED) {
        throw ex;
      }
    }
  }

  createAboutBlankDocumentViewer(aPrincipal, aPartitionedPrincipal) {
    let principal = lazy.BrowserUtils.principalWithMatchingOA(
      aPrincipal,
      this.contentPrincipal
    );
    let partitionedPrincipal = lazy.BrowserUtils.principalWithMatchingOA(
      aPartitionedPrincipal,
      this.contentPartitionedPrincipal
    );

    if (this.isRemoteBrowser) {
      this.frameLoader.remoteTab.createAboutBlankDocumentViewer(
        principal,
        partitionedPrincipal
      );
    } else {
      this.docShell.createAboutBlankDocumentViewer(
        principal,
        partitionedPrincipal
      );
    }
  }

  _acquireAutoScrollWakeLock() {
    const pm = Cc["@mozilla.org/power/powermanagerservice;1"].getService(
      Ci.nsIPowerManagerService
    );
    this._autoScrollWakelock = pm.newWakeLock("autoscroll", window);
  }

  _releaseAutoScrollWakeLock() {
    if (this._autoScrollWakelock) {
      try {
        this._autoScrollWakelock.unlock();
      } catch (e) {
        // Ignore error since wake lock is already unlocked
      }
      this._autoScrollWakelock = null;
    }
  }

  stopScroll() {
    if (this._autoScrollBrowsingContext) {
      window.removeEventListener("mousemove", this, true);
      window.removeEventListener("mousedown", this, true);
      window.removeEventListener("mouseup", this, true);
      window.removeEventListener("DOMMouseScroll", this, true);
      window.removeEventListener("contextmenu", this, true);
      window.removeEventListener("keydown", this, true);
      window.removeEventListener("keypress", this, true);
      window.removeEventListener("keyup", this, true);

      let autoScrollWnd = this._autoScrollBrowsingContext.currentWindowGlobal;
      if (autoScrollWnd) {
        autoScrollWnd
          .getActor("AutoScroll")
          .sendAsyncMessage("Autoscroll:Stop", {});
      }

      try {
        Services.obs.removeObserver(this.observer, "apz:cancel-autoscroll");
      } catch (ex) {
        // It's not clear why this sometimes throws an exception
      }

      if (this._autoScrollScrollId != null) {
        this._autoScrollBrowsingContext.stopApzAutoscroll(
          this._autoScrollScrollId,
          this._autoScrollPresShellId
        );

        this._autoScrollScrollId = null;
        this._autoScrollPresShellId = null;
      }

      this._autoScrollBrowsingContext = null;
      this._releaseAutoScrollWakeLock();
    }
  }

  _getAndMaybeCreateAutoScrollPopup() {
    let autoscrollPopup = document.getElementById("autoscroller");
    if (!autoscrollPopup) {
      autoscrollPopup = document.createXULElement("panel");
      autoscrollPopup.className = "autoscroller";
      autoscrollPopup.setAttribute("consumeoutsideclicks", "true");
      autoscrollPopup.setAttribute("rolluponmousewheel", "true");
      autoscrollPopup.id = "autoscroller";
    }

    return autoscrollPopup;
  }

  startScroll({
    scrolldir,
    screenXDevPx,
    screenYDevPx,
    scrollId,
    presShellId,
    browsingContext,
  }) {
    if (!this.autoscrollEnabled) {
      return { autoscrollEnabled: false, usingApz: false };
    }

    // The popup size is 32px for the circle plus space for a 4px box-shadow
    // on each side.
    const POPUP_SIZE = 40;
    if (!this._autoScrollPopup) {
      this._autoScrollPopup = this._getAndMaybeCreateAutoScrollPopup();
      document.documentElement.appendChild(this._autoScrollPopup);
      this._autoScrollPopup.removeAttribute("hidden");
      this._autoScrollPopup.setAttribute("noautofocus", "true");
      this._autoScrollPopup.style.height = POPUP_SIZE + "px";
      this._autoScrollPopup.style.width = POPUP_SIZE + "px";
      this._autoScrollPopup.style.margin = -POPUP_SIZE / 2 + "px";
    }

    // In desktop pixels.
    let screenXDesktopPx = screenXDevPx / window.desktopToDeviceScale;
    let screenYDesktopPx = screenYDevPx / window.desktopToDeviceScale;

    let screenManager = Cc["@mozilla.org/gfx/screenmanager;1"].getService(
      Ci.nsIScreenManager
    );
    let screen = screenManager.screenForRect(
      screenXDesktopPx,
      screenYDesktopPx,
      1,
      1
    );

    // we need these attributes so themers don't need to create per-platform packages
    if (screen.colorDepth > 8) {
      // need high color for transparency
      // Exclude second-rate platforms
      this._autoScrollPopup.setAttribute(
        "transparent",
        !/BeOS|OS\/2/.test(navigator.appVersion)
      );
      // Enable translucency on Windows and Mac
      this._autoScrollPopup.setAttribute(
        "translucent",
        AppConstants.platform == "win" || AppConstants.platform == "macosx"
      );
    }

    this._autoScrollPopup.setAttribute("scrolldir", scrolldir);
    this._autoScrollPopup.addEventListener("popuphidden", this, true);

    // In CSS pixels
    let popupX;
    let popupY;
    {
      let cssToDesktopScale =
        window.devicePixelRatio / window.desktopToDeviceScale;

      // Sanitize screenX/screenY for available screen size with half the size
      // of the popup removed. The popup uses negative margins to center on the
      // coordinates we pass. Use desktop pixels to deal correctly with
      // multi-monitor / multi-dpi scenarios.
      let left = {},
        top = {},
        width = {},
        height = {};
      screen.GetAvailRectDisplayPix(left, top, width, height);

      let popupSizeDesktopPx = POPUP_SIZE * cssToDesktopScale;
      let minX = left.value + 0.5 * popupSizeDesktopPx;
      let maxX = left.value + width.value - 0.5 * popupSizeDesktopPx;
      let minY = top.value + 0.5 * popupSizeDesktopPx;
      let maxY = top.value + height.value - 0.5 * popupSizeDesktopPx;

      popupX =
        Math.max(minX, Math.min(maxX, screenXDesktopPx)) / cssToDesktopScale;
      popupY =
        Math.max(minY, Math.min(maxY, screenYDesktopPx)) / cssToDesktopScale;
    }

    // In CSS pixels.
    let screenX = screenXDevPx / window.devicePixelRatio;
    let screenY = screenYDevPx / window.devicePixelRatio;

    this._autoScrollPopup.openPopupAtScreen(popupX, popupY);
    this._ignoreMouseEvents = true;
    this._startX = screenX;
    this._startY = screenY;
    this._autoScrollBrowsingContext = browsingContext;
    this._acquireAutoScrollWakeLock();

    window.addEventListener("mousemove", this, true);
    window.addEventListener("mousedown", this, true);
    window.addEventListener("mouseup", this, true);
    window.addEventListener("DOMMouseScroll", this, true);
    window.addEventListener("contextmenu", this, true);
    window.addEventListener("keydown", this, true);
    window.addEventListener("keypress", this, true);
    window.addEventListener("keyup", this, true);

    let usingApz = false;

    if (
      scrollId != null &&
      this.mPrefs.getBoolPref("apz.autoscroll.enabled", false)
    ) {
      // If APZ is handling the autoscroll, it may decide to cancel
      // it of its own accord, so register an observer to allow it
      // to notify us of that.
      Services.obs.addObserver(this.observer, "apz:cancel-autoscroll", true);

      usingApz = browsingContext.startApzAutoscroll(
        screenXDevPx,
        screenYDevPx,
        scrollId,
        presShellId
      );

      // Save the IDs for later
      this._autoScrollScrollId = scrollId;
      this._autoScrollPresShellId = presShellId;
    }

    // Store the time at which the auto scroll begins.
    this._autoScrollStartTime = performance.now();
    return { autoscrollEnabled: true, usingApz };
  }

  cancelScroll() {
    this._autoScrollPopup.hidePopup();
  }

  handleEvent(aEvent) {
    if (this._autoScrollBrowsingContext) {
      switch (aEvent.type) {
        case "mousemove": {
          var x = aEvent.screenX - this._startX;
          var y = aEvent.screenY - this._startY;

          if (
            x > this._AUTOSCROLL_SNAP ||
            x < -this._AUTOSCROLL_SNAP ||
            y > this._AUTOSCROLL_SNAP ||
            y < -this._AUTOSCROLL_SNAP
          ) {
            this._ignoreMouseEvents = false;
          }
          break;
        }
        case "mouseup":
        case "mousedown":
          // The following mouse click/auxclick event on the autoscroller
          // shouldn't be fired in web content for compatibility with Chrome.
          aEvent.preventClickEvent();
        // fallthrough
        case "contextmenu": {
          if (!this._ignoreMouseEvents) {
            // Use a timeout to prevent the mousedown from opening the popup again.
            // Ideally, we could use preventDefault here, but contenteditable
            // and middlemouse paste don't interact well. See bug 1188536.
            setTimeout(() => this._autoScrollPopup.hidePopup(), 0);
          }
          this._ignoreMouseEvents = false;
          break;
        }
        case "DOMMouseScroll": {
          // Check if the time elapsed since the auto scroll began is 500ms.
          // To avoid accidental cancellations of it.
          const scrollCooldownMs = this.mPrefs.getIntPref(
            "apz.autoscroll.scroll_wheel_cooldown"
          );
          if (
            performance.now() - this._autoScrollStartTime <
            scrollCooldownMs
          ) {
            aEvent.preventDefault();
            break;
          }
          this._autoScrollPopup.hidePopup();
          aEvent.preventDefault();
          break;
        }
        case "popuphidden": {
          // TODO: When the autoscroller is closed by clicking outside of it,
          //       we need to prevent following click event for compatibility
          //       with Chrome.  However, there is no way to do that for now.
          this._autoScrollPopup.removeEventListener("popuphidden", this, true);
          this.stopScroll();
          break;
        }
        case "keydown": {
          if (aEvent.keyCode == aEvent.DOM_VK_ESCAPE) {
            // the escape key will be processed by
            // nsXULPopupManager::KeyDown and the panel will be closed.
            // So, don't consume the key event here.
            break;
          }
          // don't break here. we need to eat keydown events.
        }
        // fall through
        case "keypress":
        case "keyup": {
          // All keyevents should be eaten here during autoscrolling.
          aEvent.stopPropagation();
          aEvent.preventDefault();
          break;
        }
      }
    }
  }

  closeBrowser() {
    // The request comes from a XPCOM component, we'd want to redirect
    // the request to tabbrowser.
    let tabbrowser = this.getTabBrowser();
    if (tabbrowser) {
      let tab = tabbrowser.getTabForBrowser(this);
      if (tab) {
        tabbrowser.removeTab(tab);
        return;
      }
    }

    throw new Error(
      "Closing a browser which was not attached to a tabbrowser is unsupported."
    );
  }

  swapBrowsers(aOtherBrowser) {
    // The request comes from a XPCOM component, we'd want to redirect
    // the request to tabbrowser so tabbrowser will be setup correctly,
    // and it will eventually call swapDocShells.
    let ourTabBrowser = this.getTabBrowser();
    let otherTabBrowser = aOtherBrowser.getTabBrowser();
    if (ourTabBrowser && otherTabBrowser) {
      let ourTab = ourTabBrowser.getTabForBrowser(this);
      let otherTab = otherTabBrowser.getTabForBrowser(aOtherBrowser);
      ourTabBrowser.swapBrowsers(ourTab, otherTab);
      return;
    }

    // One of us is not connected to a tabbrowser, so just swap.
    this.swapDocShells(aOtherBrowser);
  }

  swapDocShells(aOtherBrowser) {
    if (this.isRemoteBrowser != aOtherBrowser.isRemoteBrowser) {
      throw new Error(
        "Can only swap docshells between browsers in the same process."
      );
    }

    // Give others a chance to swap state.
    // IMPORTANT: Since a swapDocShells call does not swap the messageManager
    //            instances attached to a browser to aOtherBrowser, others
    //            will need to add the message listeners to the new
    //            messageManager.
    //            This is not a bug in swapDocShells or the FrameLoader,
    //            merely a design decision: If message managers were swapped,
    //            so that no new listeners were needed, the new
    //            aOtherBrowser.messageManager would have listeners pointing
    //            to the JS global of the current browser, which would rather
    //            easily create leaks while swapping.
    // IMPORTANT2: When the current browser element is removed from DOM,
    //             which is quite common after a swapDocShells call, its
    //             frame loader is destroyed, and that destroys the relevant
    //             message manager, which will remove the listeners.
    let event = new CustomEvent("SwapDocShells", { detail: aOtherBrowser });
    this.dispatchEvent(event);
    event = new CustomEvent("SwapDocShells", { detail: this });
    aOtherBrowser.dispatchEvent(event);

    // We need to swap fields that are tied to our docshell or related to
    // the loaded page
    // Fields which are built as a result of notifactions (pageshow/hide,
    // DOMLinkAdded/Removed, onStateChange) should not be swapped here,
    // because these notifications are dispatched again once the docshells
    // are swapped.
    var fieldsToSwap = ["_webBrowserFind", "_rdmFullZoom"];

    if (this.isRemoteBrowser) {
      fieldsToSwap.push(
        ...[
          "_remoteWebNavigation",
          "_remoteFinder",
          "_documentURI",
          "_documentContentType",
          "_characterSet",
          "_mayEnableCharacterEncodingMenu",
          "_contentPrincipal",
          "_contentPartitionedPrincipal",
          "_isSyntheticDocument",
          "_originalURI",
          "_userTypedValue",
        ]
      );
    }

    var ourFieldValues = {};
    var otherFieldValues = {};
    for (let field of fieldsToSwap) {
      ourFieldValues[field] = this[field];
      otherFieldValues[field] = aOtherBrowser[field];
    }

    if (window.PopupNotifications) {
      PopupNotifications._swapBrowserNotifications(aOtherBrowser, this);
    }

    try {
      this.swapFrameLoaders(aOtherBrowser);
    } catch (ex) {
      // This may not be implemented for browser elements that are not
      // attached to a BrowserDOMWindow.
    }

    for (let field of fieldsToSwap) {
      this[field] = otherFieldValues[field];
      aOtherBrowser[field] = ourFieldValues[field];
    }

    if (!this.isRemoteBrowser) {
      // Null the current nsITypeAheadFind instances so that they're
      // lazily re-created on access. We need to do this because they
      // might have attached the wrong docShell.
      this._fastFind = aOtherBrowser._fastFind = null;
    } else {
      // Rewire the remote listeners
      this._remoteWebNavigation.swapBrowser(this);
      aOtherBrowser._remoteWebNavigation.swapBrowser(aOtherBrowser);

      if (this._remoteFinder) {
        this._remoteFinder.swapBrowser(this);
      }
      if (aOtherBrowser._remoteFinder) {
        aOtherBrowser._remoteFinder.swapBrowser(aOtherBrowser);
      }
    }

    event = new CustomEvent("EndSwapDocShells", { detail: aOtherBrowser });
    this.dispatchEvent(event);
    event = new CustomEvent("EndSwapDocShells", { detail: this });
    aOtherBrowser.dispatchEvent(event);
  }

  getInPermitUnload(aCallback) {
    if (this.isRemoteBrowser) {
      let { remoteTab } = this.frameLoader;
      if (!remoteTab) {
        // If we're crashed, we're definitely not in this state anymore.
        aCallback(false);
        return;
      }

      aCallback(
        this._inPermitUnload.has(this.browsingContext.currentWindowGlobal)
      );
      return;
    }

    if (!this.docShell || !this.docShell.docViewer) {
      aCallback(false);
      return;
    }
    aCallback(this.docShell.docViewer.inPermitUnload);
  }

  async asyncPermitUnload(action) {
    let wgp = this.browsingContext.currentWindowGlobal;
    if (this._inPermitUnload.has(wgp)) {
      throw new Error("permitUnload is already running for this tab.");
    }

    this._inPermitUnload.add(wgp);
    try {
      let permitUnload = await wgp.permitUnload(
        action,
        lazyPrefs.unloadTimeoutMs
      );
      return { permitUnload };
    } finally {
      this._inPermitUnload.delete(wgp);
    }
  }

  get hasBeforeUnload() {
    function hasBeforeUnload(bc) {
      if (bc.currentWindowContext?.hasBeforeUnload) {
        return true;
      }
      return bc.children.some(hasBeforeUnload);
    }
    return hasBeforeUnload(this.browsingContext);
  }

  get hasActiveCloseWatcher() {
    function hasActiveCloseWatcher(bc) {
      if (bc.currentWindowContext?.hasActiveCloseWatcher) {
        return true;
      }
      return bc.children.some(hasActiveCloseWatcher);
    }
    return hasActiveCloseWatcher(this.browsingContext);
  }

  permitUnload(action) {
    if (this.isRemoteBrowser) {
      if (!this.hasBeforeUnload) {
        return { permitUnload: true };
      }

      // Don't bother asking if this browser is hung:
      if (
        lazy.ProcessHangMonitor?.findActiveReport(this) ||
        lazy.ProcessHangMonitor?.findPausedReport(this)
      ) {
        return { permitUnload: true };
      }

      let result;
      let success;

      this.asyncPermitUnload(action).then(
        val => {
          result = val;
          success = true;
        },
        err => {
          result = err;
          success = false;
        }
      );

      // The permitUnload() promise will, alas, not call its resolution
      // callbacks after the browser window the promise lives in has closed,
      // so we have to check for that case explicitly.
      Services.tm.spinEventLoopUntilOrQuit(
        "browser-custom-element.js:permitUnload",
        () => window.closed || success !== undefined
      );
      if (success) {
        return result;
      }
      throw result;
    }

    if (!this.docShell || !this.docShell.docViewer) {
      return { permitUnload: true };
    }
    return {
      permitUnload: this.docShell.docViewer.permitUnload(),
    };
  }

  /**
   * Gets a screenshot of this browser as an ImageBitmap.
   *
   * @param {number} x
   *   The x coordinate of the region from the underlying document to capture
   *   as a screenshot. This is ignored if fullViewport is true.
   * @param {number} y
   *   The y coordinate of the region from the underlying document to capture
   *   as a screenshot. This is ignored if fullViewport is true.
   * @param {number} w
   *   The width of the region from the underlying document to capture as a
   *   screenshot. This is ignored if fullViewport is true.
   * @param {number} h
   *   The height of the region from the underlying document to capture as a
   *   screenshot. This is ignored if fullViewport is true.
   * @param {number} scale
   *   The scale factor for the captured screenshot. See the documentation for
   *   WindowGlobalParent.drawSnapshot for more detail.
   * @param {string} backgroundColor
   *   The default background color for the captured screenshot. See the
   *   documentation for WindowGlobalParent.drawSnapshot for more detail.
   * @param {boolean|undefined} fullViewport
   *   True if the viewport rect should be captured. If this is true, the
   *   x, y, w and h parameters are ignored. Defaults to false.
   * @returns {Promise<ImageBitmap>}
   */
  async drawSnapshot(x, y, w, h, scale, backgroundColor, fullViewport = false) {
    let rect = fullViewport ? null : new DOMRect(x, y, w, h);
    try {
      return this.browsingContext.currentWindowGlobal.drawSnapshot(
        rect,
        scale,
        backgroundColor
      );
    } catch (e) {
      return false;
    }
  }

  dropLinks(aLinks, aTriggeringPrincipal) {
    if (!this.droppedLinkHandler) {
      return false;
    }
    let links = [];
    for (let i = 0; i < aLinks.length; i += 3) {
      links.push({
        url: aLinks[i],
        name: aLinks[i + 1],
        type: aLinks[i + 2],
      });
    }
    this.droppedLinkHandler(null, links, aTriggeringPrincipal);
    return true;
  }

  getContentBlockingLog() {
    let windowGlobal = this.browsingContext.currentWindowGlobal;
    if (!windowGlobal) {
      return null;
    }
    return windowGlobal.contentBlockingLog;
  }

  getContentBlockingEvents() {
    let windowGlobal = this.browsingContext.currentWindowGlobal;
    if (!windowGlobal) {
      return 0;
    }
    return windowGlobal.contentBlockingEvents;
  }

  // Send an asynchronous message to the remote child via an actor.
  // Note: use this only for messages through an actor. For old-style
  // messages, use the message manager.
  // The value of the scope argument determines which browsing contexts
  // are sent to:
  //   'all' - send to actors associated with all descendant child frames.
  //   'roots' - send only to actors associated with process roots.
  //   undefined/'' - send only to the top-level actor and not any descendants.
  sendMessageToActor(messageName, args, actorName, scope) {
    if (!this.frameLoader) {
      return;
    }

    function sendToChildren(browsingContext, childScope) {
      let windowGlobal = browsingContext.currentWindowGlobal;
      // If 'roots' is set, only send if windowGlobal.isProcessRoot is true.
      if (
        windowGlobal &&
        (childScope != "roots" || windowGlobal.isProcessRoot)
      ) {
        windowGlobal.getActor(actorName).sendAsyncMessage(messageName, args);
      }

      // Iterate as long as scope in assigned. Note that we use the original
      // passed in scope, not childScope here.
      if (scope) {
        for (let context of browsingContext.children) {
          sendToChildren(context, scope);
        }
      }
    }

    // Pass no second argument to always send to the top-level browsing context.
    sendToChildren(this.browsingContext);
  }

  enterModalState() {
    this.sendMessageToActor("EnterModalState", {}, "BrowserElement", "roots");
  }

  leaveModalState() {
    this.sendMessageToActor(
      "LeaveModalState",
      { forceLeave: true },
      "BrowserElement",
      "roots"
    );
  }

  /**
   * Can be called for a window with or without modal state.
   * If the window is not in modal state, this is a no-op.
   */
  maybeLeaveModalState() {
    this.sendMessageToActor(
      "LeaveModalState",
      { forceLeave: false },
      "BrowserElement",
      "roots"
    );
  }

  getDevicePermissionOrigins(key) {
    if (typeof key !== "string" || key.length === 0) {
      throw new Error("Key must be non empty string.");
    }
    if (!this._devicePermissionOrigins) {
      this._devicePermissionOrigins = new Map();
    }
    let origins = this._devicePermissionOrigins.get(key);
    if (!origins) {
      origins = new Set();
      this._devicePermissionOrigins.set(key, origins);
    }
    return origins;
  }

  // This method is replaced by frontend code in order to delay performing the
  // process switch until some async operatin is completed.
  //
  // This is used by tabbrowser to flush SessionStore before a process switch.
  async prepareToChangeRemoteness() {
    /* no-op unless replaced */
  }

  // This method is replaced by frontend code in order to handle restoring
  // remote session history
  //
  // Called immediately after changing remoteness.  If this method returns
  // `true`, Gecko will assume frontend handled resuming the load, and will
  // not attempt to resume the load itself.
  afterChangeRemoteness() {
    /* no-op unless replaced */
    return false;
  }

  // Called by Gecko before the remoteness change happens, allowing for
  // listeners, etc. to be stashed before the process switch.
  beforeChangeRemoteness() {
    // Fire the `WillChangeBrowserRemoteness` event, which may be hooked by
    // frontend code for custom behaviour.
    let event = document.createEvent("Events");
    event.initEvent("WillChangeBrowserRemoteness", true, false);
    this.dispatchEvent(event);

    // Destroy ourselves to unregister from observer notifications
    // FIXME: Can we get away with something less destructive here?
    this.destroy();
  }

  finishChangeRemoteness(redirectLoadSwitchId) {
    // Re-construct ourselves after the destroy in `beforeChangeRemoteness`.
    this.construct();

    // Fire the `DidChangeBrowserRemoteness` event, which may be hooked by
    // frontend code for custom behaviour.
    let event = document.createEvent("Events");
    event.initEvent("DidChangeBrowserRemoteness", true, false);
    this.dispatchEvent(event);

    // Call into frontend code which may want to handle the load (e.g. to
    // while restoring session state).
    return this.afterChangeRemoteness(redirectLoadSwitchId);
  }
}

MozXULElement.implementCustomInterface(MozBrowser, [Ci.nsIBrowser]);
customElements.define("browser", MozBrowser);
