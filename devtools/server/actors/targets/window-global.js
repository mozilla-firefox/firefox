/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// protocol.js uses objects as exceptions in order to define
// error packets.
/* eslint-disable no-throw-literal */

/*
 * WindowGlobalTargetActor is an abstract class used by target actors that hold
 * documents, such as frames, chrome windows, etc.
 *
 * This class is extended by ParentProcessTargetActor.
 *
 * See devtools/docs/contributor/backend/actor-hierarchy.md for more details about all the targets.
 *
 * For performance matters, this file should only be loaded in the targeted context's
 * process. For example, it shouldn't be evaluated in the parent process until we try to
 * debug a document living in the parent process.
 */

var {
  ActorRegistry,
} = require("resource://devtools/server/actors/utils/actor-registry.js");
var DevToolsUtils = require("resource://devtools/shared/DevToolsUtils.js");
var { assert } = DevToolsUtils;
var {
  SourcesManager,
} = require("resource://devtools/server/actors/utils/sources-manager.js");
var makeDebugger = require("resource://devtools/server/actors/utils/make-debugger.js");
const Targets = require("resource://devtools/server/actors/targets/index.js");

const lazy = {};
// Modules loaded in the same module loader as this module.
// (may spawn distinct module instances with other devtools and firefox frontend)
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
    WEBEXTENSION_FALLBACK_DOC_URL:
      "resource://devtools/server/actors/watcher/browsing-context-helpers.sys.mjs",
    getAddonIdForWindowGlobal:
      "resource://devtools/server/actors/watcher/browsing-context-helpers.sys.mjs",
  },
  { global: "contextual" }
);

// Modules loaded from the shared module loader.
// Won't be as debuggable from the Browser Toolbox and may trigger breakpoints.
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    TargetActorRegistry:
      "resource://devtools/server/actors/targets/target-actor-registry.sys.mjs",
  },
  { global: "shared" }
);

const { Pool } = require("resource://devtools/shared/protocol.js");
const {
  LazyPool,
  createExtraActors,
} = require("resource://devtools/shared/protocol/lazy-pool.js");
const {
  windowGlobalTargetSpec,
} = require("resource://devtools/shared/specs/targets/window-global.js");
const Resources = require("resource://devtools/server/actors/resources/index.js");
const {
  BaseTargetActor,
} = require("resource://devtools/server/actors/targets/base-target-actor.js");

loader.lazyRequireGetter(
  this,
  ["ThreadActor"],
  "resource://devtools/server/actors/thread.js",
  true
);
loader.lazyRequireGetter(
  this,
  "WorkerDescriptorActorList",
  "resource://devtools/server/actors/worker/worker-descriptor-actor-list.js",
  true
);
loader.lazyRequireGetter(
  this,
  "StyleSheetsManager",
  "resource://devtools/server/actors/utils/stylesheets-manager.js",
  true
);
loader.lazyRequireGetter(
  this,
  "TouchSimulator",
  "resource://devtools/server/actors/emulation/touch-simulator.js",
  true
);

function getWindowID(window) {
  return window.windowGlobalChild.innerWindowId;
}

function getDocShellChromeEventHandler(docShell) {
  let handler = docShell.chromeEventHandler;
  if (!handler) {
    try {
      // Toplevel xul window's docshell doesn't have chromeEventHandler
      // attribute. The chrome event handler is just the global window object.
      handler = docShell.domWindow;
    } catch (e) {
      // ignore
    }
  }
  return handler;
}

/**
 * Helper to retrieve all children docshells of a given docshell.
 *
 * Given that docshell interfaces can only be used within the same process,
 * this only returns docshells for children documents that runs in the same process
 * as the given docshell.
 */
function getChildDocShells(parentDocShell) {
  return parentDocShell.browsingContext
    .getAllBrowsingContextsInSubtree()
    .filter(browsingContext => {
      // Filter out browsingContext which don't expose any docshell (e.g. remote frame)
      return browsingContext.docShell;
    })
    .map(browsingContext => {
      // Map BrowsingContext to DocShell
      return browsingContext.docShell;
    });
}

exports.getChildDocShells = getChildDocShells;

/**
 * Browser-specific actors.
 */

function getInnerId(window) {
  return window.windowGlobalChild.innerWindowId;
}

class WindowGlobalTargetActor extends BaseTargetActor {
  /**
   * WindowGlobalTargetActor is the target actor to debug (HTML) documents.
   *
   * WindowGlobal's are the Gecko representation for a given document's window object.
   * It relates to a given nsGlobalWindowInner instance.
   *
   * The main goal of this class is to expose the target-scoped actors being registered
   * via `ActorRegistry.registerModule` and manage their lifetimes. In addition, this
   * class also tracks the lifetime of the targeted window global.
   *
   * ### Main requests:
   *
   * `detach`:
   *  Stop document watching and cleanup everything that the target and its children actors created.
   *  It ultimately lead to destroy the target actor.
   * `switchToFrame`:
   *  Change the targeted document of the whole actor, and its child target-scoped actors
   *  to an iframe or back to its original document.
   *
   * Most properties (like `chromeEventHandler` or `docShells`) are meant to be
   * used by the various child target actors.
   *
   * ### RDP events:
   *
   *  - `tabNavigated`:
   *    Sent when the window global is about to navigate or has just navigated
   *    to a different document.
   *    This event contains the following attributes:
   *     * url (string)
   *       The new URI being loaded.
   *     * state (string)
   *       `start` if we just start requesting the new URL
   *       `stop`  if the new URL is done loading
   *     * isFrameSwitching (boolean)
   *       Indicates the event is dispatched when switching the actor context to a
   *       different frame. When we switch to an iframe, there is no document
   *       load. The targeted document is most likely going to be already done
   *       loading.
   *     * title (string)
   *       The document title being loaded. (sent only on state=stop)
   *
   *  - `frameUpdate`:
   *    Sent when there was a change in the child frames contained in the document
   *    or when the actor's context was switched to another frame.
   *    This event can have four different forms depending on the type of change:
   *    * One or many frames are updated:
   *      { frames: [{ id, url, title, parentID }, ...] }
   *    * One frame got destroyed:
   *      { frames: [{ id, destroy: true }]}
   *    * All frames got destroyed:
   *      { destroyAll: true }
   *    * We switched the context of the actor to a specific frame:
   *      { selected: #id }
   *
   * ### Internal, non-rdp events:
   *
   * Various events are also dispatched on the actor itself without being sent to
   * the client. They all relate to the documents tracked by this target actor
   * (its main targeted document, but also any of its iframes):
   *  - will-navigate
   *    This event fires once navigation starts. All pending user prompts are
   *    dealt with, but it is fired before the first request starts.
   *  - navigate
   *    This event is fired once the document's readyState is "complete".
   *  - window-ready
   *    This event is fired in various distinct scenarios:
   *     * When a new Window object is crafted, equivalent of `DOMWindowCreated`.
   *       It is dispatched before any page script is executed.
   *     * We will have already received a window-ready event for this window
   *       when it was created, but we received a window-destroyed event when
   *       it was frozen into the bfcache, and now the user navigated back to
   *       this page, so it's now live again and we should resume handling it.
   *     * For each existing document, when an `attach` request is received.
   *       At this point scripts in the page will be already loaded.
   *     * When `swapFrameLoaders` is used, such as with moving window globals
   *       between windows or toggling Responsive Design Mode.
   *  - window-destroyed
   *    This event is fired in two cases:
   *     * When the window object is destroyed, i.e. when the related document
   *       is garbage collected. This can happen when the window global is
   *       closed or the iframe is removed from the DOM.
   *       It is equivalent of `inner-window-destroyed` event.
   *     * When the page goes into the bfcache and gets frozen.
   *       The equivalent of `pagehide`.
   *  - changed-toplevel-document
   *    This event fires when we switch the actor's targeted document
   *    to one of its iframes, or back to its original top document.
   *    It is dispatched between window-destroyed and window-ready.
   *
   * Note that *all* these events are dispatched in the following order
   * when we switch the context of the actor to a given iframe:
   *  - will-navigate
   *  - window-destroyed
   *  - changed-toplevel-document
   *  - window-ready
   *  - navigate
   *
   * This class is subclassed by ParentProcessTargetActor and others.
   * Subclasses are expected to implement a getter for the docShell property.
   *
   * @param conn DevToolsServerConnection
   *        The conection to the client.
   * @param options Object
   *        Object with following attributes:
   *        - docShell nsIDocShell
   *          The |docShell| for the debugged frame.
   *        - followWindowGlobalLifeCycle Boolean
   *          If true, the target actor will only inspect the current WindowGlobal (and its children windows).
   *          But won't inspect next document loaded in the same BrowsingContext.
   *          The actor will behave more like a WindowGlobalTarget rather than a BrowsingContextTarget.
   *          This is always true for Tab debugging, but not yet for parent process/web extension.
   *        - isTopLevelTarget Boolean
   *          Should be set to true for all top-level targets. A top level target
   *          is the topmost target of a DevTools "session". For instance for a local
   *          tab toolbox, the WindowGlobalTargetActor for the content page is the top level target.
   *          For the Multiprocess Browser Toolbox, the parent process target is the top level
   *          target.
   *          At the moment this only impacts the WindowGlobalTarget `reconfigure`
   *          implementation. But for server-side target switching this flag will be exposed
   *          to the client and should be available for all target actor classes. It will be
   *          used to detect target switching. (Bug 1644397)
   *        - ignoreSubFrames Boolean
   *          If true, the actor will only focus on the passed docShell and not on the whole
   *          docShell tree. This should be enabled when we have targets for all documents.
   *        - sessionContext Object
   *          The Session Context to help know what is debugged.
   *          See devtools/server/actors/watcher/session-context.js
   */
  constructor(
    conn,
    {
      docShell,
      followWindowGlobalLifeCycle,
      isTopLevelTarget,
      ignoreSubFrames,
      sessionContext,
      customSpec = windowGlobalTargetSpec,
    }
  ) {
    super(conn, Targets.TYPES.FRAME, customSpec);

    this.followWindowGlobalLifeCycle = followWindowGlobalLifeCycle;
    this.isTopLevelTarget = !!isTopLevelTarget;
    this.ignoreSubFrames = ignoreSubFrames;
    this.sessionContext = sessionContext;

    // A map of actor names to actor instances provided by extensions.
    this._extraActors = {};
    this._sourcesManager = null;

    this._shouldAddNewGlobalAsDebuggee =
      this._shouldAddNewGlobalAsDebuggee.bind(this);

    this.makeDebugger = makeDebugger.bind(null, {
      findDebuggees: () => {
        const result = [];
        const inspectUAWidgets = Services.prefs.getBoolPref(
          "devtools.inspector.showAllAnonymousContent",
          false
        );
        for (const win of this.windows) {
          result.push(win);
          // Only expose User Agent internal (like <video controls>) when the
          // related pref is set.
          if (inspectUAWidgets) {
            const principal = win.document.nodePrincipal;
            // We don't use UA widgets for the system principal.
            if (!principal.isSystemPrincipal) {
              result.push(Cu.getUAWidgetScope(principal));
            }
          }
        }
        return result;
      },
      shouldAddNewGlobalAsDebuggee: this._shouldAddNewGlobalAsDebuggee,
    });

    // Flag eventually overloaded by sub classes in order to watch new docshells
    // Used by the ParentProcessTargetActor to list all frames in the Browser Toolbox
    this.watchNewDocShells = false;

    this._workerDescriptorActorList = null;
    this._workerDescriptorActorPool = null;
    this._onWorkerDescriptorActorListChanged =
      this._onWorkerDescriptorActorListChanged.bind(this);

    this._onConsoleApiProfilerEvent =
      this._onConsoleApiProfilerEvent.bind(this);
    Services.obs.addObserver(
      this._onConsoleApiProfilerEvent,
      "console-api-profiler"
    );

    // Start observing navigations as well as sub documents.
    // (This is probably meant to disappear once EFT is the only supported codepath)
    this._progressListener = new DebuggerProgressListener(this);

    lazy.TargetActorRegistry.registerTargetActor(this);

    if (docShell) {
      this.setDocShell(docShell);
    }
  }

  // Attribute only set when debugging web extensions, in order to distinguish:
  // - "fallback-document" created by DevTools for the top level target
  // - "background" for the background page
  // - "popup" for any popup document
  #isFallbackExtensionDocument = false;

  /**
   * Define the initial docshell.
   *
   * This is called from the constructor for WindowGlobalTargetActor,
   * or from sub class constructor of ParentProcessTargetActor.
   *
   * This is to circumvent the fact that sub classes need to call inner method
   * to compute the initial docshell and we can't call inner methods before calling
   * the base class constructor...
   */
  setDocShell(docShell) {
    Object.defineProperty(this, "docShell", {
      value: docShell,
      configurable: true,
      writable: true,
    });

    // Save references to the original document we attached to
    this._originalWindow = this.window;

    // Update isPrivate as window is based on docShell
    this.isPrivate = lazy.PrivateBrowsingUtils.isContentWindowPrivate(
      this.window
    );

    // Instantiate the Thread Actor immediately.
    // This is the only one actor instantiated right away by the target actor.
    // All the others are instantiated lazily on first request made the client,
    // via LazyPool API.
    this._createThreadActor();

    // Ensure notifying about the target actor first
    // before notifying about new docshells.
    // Otherwise we would miss these RDP event as the client hasn't
    // yet received the target actor's form.
    // (This is also probably meant to disappear once EFT is the only supported codepath)
    this._docShellsObserved = false;
    DevToolsUtils.executeSoon(() => this._watchDocshells());

    // The `watchedByDevTools` enables gecko behavior tied to this flag, such as:
    //  - reporting the contents of HTML loaded in the docshells,
    //  - or capturing stacks for the network monitor.
    //
    // This flag can only be set on top level BrowsingContexts.
    if (!this.browsingContext.parent) {
      this.browsingContext.watchedByDevTools = true;
    }

    if (this.sessionContext.type == "webextension") {
      if (
        this.window.location.href.startsWith(lazy.WEBEXTENSION_FALLBACK_DOC_URL)
      ) {
        this.#isFallbackExtensionDocument = true;
      }
    }
  }

  get docShell() {
    throw new Error(
      "A docShell should be provided as constructor argument of WindowGlobalTargetActor, or redefined by the subclass"
    );
  }

  /*
   * Return a Debugger instance or create one if there is none yet
   */
  get dbg() {
    if (!this._dbg) {
      this._dbg = this.makeDebugger();
    }
    return this._dbg;
  }

  /**
   * Try to locate the console actor if it exists.
   */
  get _consoleActor() {
    if (this.isDestroyed()) {
      return null;
    }
    const form = this.form();
    return this.conn._getOrCreateActor(form.consoleActor);
  }

  get _memoryActor() {
    if (this.isDestroyed()) {
      return null;
    }
    const form = this.form();
    return this.conn._getOrCreateActor(form.memoryActor);
  }

  _targetScopedActorPool = null;

  /**
   * An object on which listen for DOMWindowCreated and pageshow events.
   */
  get chromeEventHandler() {
    return getDocShellChromeEventHandler(this.docShell);
  }

  /**
   * Getter for the nsIMessageManager associated to the window global.
   */
  get messageManager() {
    try {
      return this.docShell.messageManager;
    } catch (e) {
      // In some cases we can't get a docshell.  We just have no message manager
      // then,
      return null;
    }
  }

  /**
   * Getter for the list of all `docShell`s in the window global.
   * @return {Array}
   */
  get docShells() {
    if (this.ignoreSubFrames) {
      return [this.docShell];
    }

    return getChildDocShells(this.docShell);
  }

  /**
   * Getter for the window global's current DOM window.
   */
  get window() {
    return this.docShell && !this.docShell.isBeingDestroyed()
      ? this.docShell.domWindow
      : null;
  }

  get targetGlobal() {
    return this.window;
  }

  get outerWindowID() {
    if (this.docShell) {
      return this.docShell.outerWindowID;
    }
    return null;
  }

  get browsingContext() {
    return this.docShell?.browsingContext;
  }

  get browsingContextID() {
    return this.browsingContext?.id;
  }

  get innerWindowId() {
    return this.window?.windowGlobalChild.innerWindowId;
  }

  get browserId() {
    return this.browsingContext?.browserId;
  }

  get openerBrowserId() {
    return this.browsingContext?.opener?.browserId;
  }

  /**
   * Getter for the list of all content DOM windows in the window global.
   * @return {Array}
   */
  get windows() {
    const windows = [];
    for (const docShell of this.docShells) {
      try {
        windows.push(docShell.domWindow);
      } catch (e) {
        // Ignore destroying docshells which may throw when accessing domWindow property.
      }
    }
    return windows;
  }

  /**
   * Getter for the original docShell this actor got attached to in the first
   * place.
   * Note that your actor should normally *not* rely on this top level docShell
   * if you want it to show information relative to the iframe that's currently
   * being inspected in the toolbox.
   */
  get originalDocShell() {
    if (!this._originalWindow) {
      return this.docShell;
    }

    return this._originalWindow.docShell;
  }

  /**
   * Getter for the original window this actor got attached to in the first
   * place.
   * Note that your actor should normally *not* rely on this top level window if
   * you want it to show information relative to the iframe that's currently
   * being inspected in the toolbox.
   */
  get originalWindow() {
    return this._originalWindow || this.window;
  }

  /**
   * Getter for the nsIWebProgress for watching this window.
   */
  get webProgress() {
    return this.docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
  }

  /**
   * Getter for the nsIWebNavigation for the target.
   */
  get webNavigation() {
    return this.docShell.QueryInterface(Ci.nsIWebNavigation);
  }

  /**
   * Getter for the window global's document.
   */
  get contentDocument() {
    return this.webNavigation.document;
  }

  /**
   * Getter for the window global's title.
   */
  get title() {
    return this.contentDocument.title;
  }

  /**
   * Getter for the window global's URL.
   */
  get url() {
    if (this.webNavigation.currentURI) {
      return this.webNavigation.currentURI.spec;
    }
    // Abrupt closing of the browser window may leave callbacks without a
    // currentURI.
    return null;
  }

  get sourcesManager() {
    if (!this._sourcesManager) {
      this._sourcesManager = new SourcesManager(this.threadActor);
    }
    return this._sourcesManager;
  }

  getStyleSheetsManager() {
    if (!this._styleSheetsManager) {
      this._styleSheetsManager = new StyleSheetsManager(this);
    }
    return this._styleSheetsManager;
  }

  _createExtraActors() {
    // Always use the same Pool, so existing actor instances
    // (created in createExtraActors) are not lost.
    if (!this._targetScopedActorPool) {
      this._targetScopedActorPool = new LazyPool(this.conn);
    }

    // Walk over target-scoped actor factories and make sure they are all
    // instantiated and added into the Pool.
    return createExtraActors(
      ActorRegistry.targetScopedActorFactories,
      this._targetScopedActorPool,
      this
    );
  }

  form() {
    assert(
      !this.isDestroyed(),
      "form() shouldn't be called on destroyed browser actor."
    );
    assert(this.actorID, "Actor should have an actorID.");

    // Note that we don't want the iframe dropdown to change our BrowsingContext.id/innerWindowId
    // We only want to refer to the topmost original window we attached to
    // as that's the one top document this target actor really represent.
    // The iframe dropdown is just a hack that temporarily focus the scope
    // of the target actor to a children iframe document.
    //
    // Also, for WebExtension, we want the target to represent the <browser> element
    // created by DevTools, which always exists and help better connect resources to the target
    // in the frontend. Otherwise all other <browser> element of webext may be reloaded or go away
    // and then we would have troubles matching targets for resources.
    const originalBrowsingContext = this
      .devtoolsSpawnedBrowsingContextForWebExtension
      ? this.devtoolsSpawnedBrowsingContextForWebExtension
      : this.originalDocShell.browsingContext;

    // When toggling the toolbox on/off many times in a row,
    // we may try to destroy the actor while the related document is already destroyed.
    // In such scenario, return the minimum viable form
    if (!originalBrowsingContext.currentWindowContext) {
      return { actor: this.actorID };
    }

    const browsingContextID = originalBrowsingContext.id;
    const innerWindowId =
      originalBrowsingContext.currentWindowContext.innerWindowId;
    const parentInnerWindowId =
      originalBrowsingContext.parent?.currentWindowContext.innerWindowId;
    // Doesn't only check `!!opener` as some iframe might have an opener
    // if their location was loaded via `window.open(url, "iframe-name")`.
    // So also ensure that the document is opened in a distinct tab.
    const isPopup =
      !!originalBrowsingContext.opener &&
      originalBrowsingContext.browserId !=
        originalBrowsingContext.opener.browserId;

    const response = {
      actor: this.actorID,
      targetType: this.targetType,

      browsingContextID,
      processID: Services.appinfo.processID,
      // True for targets created by JSWindowActors, see constructor JSDoc.
      followWindowGlobalLifeCycle: this.followWindowGlobalLifeCycle,
      innerWindowId,
      parentInnerWindowId,
      topInnerWindowId: this.browsingContext.topWindowContext.innerWindowId,
      isTopLevelTarget: this.isTopLevelTarget,
      ignoreSubFrames: this.ignoreSubFrames,
      isPopup,
      isPrivate: this.isPrivate,

      // Specific to Web Extension documents
      isFallbackExtensionDocument: this.#isFallbackExtensionDocument,
      addonId: lazy.getAddonIdForWindowGlobal(this.window.windowGlobalChild),

      traits: {
        // @backward-compat { version 64 } Exposes a new trait to help identify
        // BrowsingContextActor's inherited actors from the client side.
        isBrowsingContext: true,
        // Browsing context targets can compute the isTopLevelTarget flag on the
        // server. But other target actors don't support this yet. See Bug 1709314.
        supportsTopLevelTargetFlag: true,
        // Supports frame listing via `listFrames` request and `frameUpdate` events
        // as well as frame switching via `switchToFrame` request
        frames: true,
        // Supports the logInPage request.
        logInPage: true,
        // Supports watchpoints in the server. We need to keep this trait because target
        // actors that don't extend WindowGlobalTargetActor (Worker, ContentProcess, …)
        // might not support watchpoints.
        watchpoints: true,
        // Supports back and forward navigation
        navigation: true,
      },
    };

    // We may try to access window while the document is closing, then accessing window
    // throws.
    if (!this.docShell.isBeingDestroyed()) {
      response.title = this.title;
      response.url = this.url;
      response.outerWindowID = this.outerWindowID;
    }

    // If the actor is already being destroyed, avoid re-registering the target scoped actors
    if (this.destroying) {
      return response;
    }

    const actors = this._createExtraActors();
    Object.assign(response, actors);

    // The thread actor is the only actor manually created by the target actor.
    // It is not registered in targetScopedActorFactories and therefore needs
    // to be added here manually.
    if (this.threadActor) {
      Object.assign(response, {
        threadActor: this.threadActor.actorID,
      });
    }

    return response;
  }

  /**
   * Called when the actor is removed from the connection.
   *
   * @params {Object} options
   * @params {Boolean} options.isTargetSwitching: Set to true when this is called during
   *         a target switch.
   * @params {Boolean} options.isModeSwitching: Set to true true when this is called as the
   *         result of a change to the devtools.browsertoolbox.scope pref.
   */
  destroy({ isTargetSwitching = false, isModeSwitching = false } = {}) {
    // Avoid reentrancy. We will destroy the Transport when emitting "destroyed",
    // which will force destroying all actors.
    if (this.destroying) {
      return;
    }
    this.destroying = true;

    // Force flushing pending resources if the actor isn't already destroyed.
    // This helps notify the client about pending resources on navigation.
    if (!this.isDestroyed()) {
      this.emitResources();
    }

    // Tell the thread actor that the window global is closed, so that it may terminate
    // instead of resuming the debuggee script.
    // TODO: Bug 997119: Remove this coupling with thread actor
    if (this.threadActor) {
      this.threadActor._parentClosed = true;
    }

    if (this._touchSimulator) {
      this._touchSimulator.stop();
      this._touchSimulator = null;
    }

    // The watchedByDevTools flag is only set on top level BrowsingContext
    // (as it then cascades to all its children),
    // and when destroying the target, we should tell the platform we no longer
    // observe this BrowsingContext and set this attribute to false.
    if (
      this.browsingContext?.watchedByDevTools &&
      !this.browsingContext.parent &&
      // Avoid updating watchedByDevTools if the browsing context is discarded
      // otherwise the setter will throw.
      !this.browsingContext.isDiscarded
    ) {
      this.browsingContext.watchedByDevTools = false;
    }

    // Check for `docShell` availability, as it can be already gone during
    // Firefox shutdown.
    if (this.docShell) {
      this._unwatchDocShell(this.docShell);

      // If this target is being destroyed as part of a target switch or a mode switch,
      // we don't need to restore the configuration (this might cause the content page to
      // be focused again, causing issues in tests and disturbing the user when switching modes).
      if (!isTargetSwitching && !isModeSwitching) {
        this._restoreTargetConfiguration();
      }
    }
    this._unwatchDocshells();

    this._destroyThreadActor();

    if (this._styleSheetsManager) {
      this._styleSheetsManager.destroy();
      this._styleSheetsManager = null;
    }

    // Shut down actors that belong to this target's pool.
    if (this._targetScopedActorPool) {
      this._targetScopedActorPool.destroy();
      this._targetScopedActorPool = null;
    }

    // Make sure that no more workerListChanged notifications are sent.
    if (this._workerDescriptorActorList !== null) {
      this._workerDescriptorActorList.destroy();
      this._workerDescriptorActorList = null;
    }

    if (this._workerDescriptorActorPool !== null) {
      this._workerDescriptorActorPool.destroy();
      this._workerDescriptorActorPool = null;
    }

    if (this._dbg) {
      this._dbg.disable();
      this._dbg = null;
    }

    // Emit a last event before calling Actor.destroy
    // which will destroy the EventEmitter API
    this.emit("destroyed", { isTargetSwitching, isModeSwitching });

    // Destroy BaseTargetActor before nullifying docShell in case any child actor queries the window/docShell.
    super.destroy();

    this.docShell = null;
    this._extraActors = null;

    Services.obs.removeObserver(
      this._onConsoleApiProfilerEvent,
      "console-api-profiler"
    );

    lazy.TargetActorRegistry.unregisterTargetActor(this);
    Resources.unwatchAllResources(this);
  }

  /**
   * This is only used by WebExtensionTargetActor, which overrides this method.
   */
  _shouldAddNewGlobalAsDebuggee() {
    return false;
  }

  _watchDocshells() {
    // If for some unexpected reason, the actor is immediately destroyed,
    // avoid registering leaking observer listener.
    if (this.isDestroyed()) {
      return;
    }

    // This method is called asynchronously and the document may have been destroyed in the meantime.
    // In such case, automatically destroy the target actor.
    if (this.docShell.isBeingDestroyed()) {
      this.destroy();
      return;
    }

    // In child processes, we watch all docshells living in the process.
    Services.obs.addObserver(this, "webnavigation-create");
    Services.obs.addObserver(this, "webnavigation-destroy");
    this._docShellsObserved = true;

    // We watch for all child docshells under the current document,
    this._progressListener.watch(this.docShell);

    // And list all already existing ones.
    this._updateChildDocShells();
  }

  _unwatchDocshells() {
    if (this._progressListener) {
      this._progressListener.destroy();
      this._progressListener = null;
      this._originalWindow = null;
    }

    // Removes the observers being set in _watchDocshells, but only
    // if _watchDocshells has been called. The target actor may be immediately destroyed
    // and doesn't have time to register them.
    // (Calling removeObserver without having called addObserver throws)
    if (this._docShellsObserved) {
      Services.obs.removeObserver(this, "webnavigation-create");
      Services.obs.removeObserver(this, "webnavigation-destroy");
      this._docShellsObserved = false;
    }
  }

  _unwatchDocShell(docShell) {
    if (this._progressListener) {
      this._progressListener.unwatch(docShell);
    }
  }

  switchToFrame(request) {
    const windowId = request.windowId;
    let win;

    try {
      win = Services.wm.getOuterWindowWithId(windowId);
    } catch (e) {
      // ignore
    }
    if (!win) {
      throw {
        error: "noWindow",
        message: "The related docshell is destroyed or not found",
      };
    } else if (win == this.window) {
      return {};
    }

    // Reply first before changing the document
    DevToolsUtils.executeSoon(() => this._changeTopLevelDocument(win));

    return {};
  }

  listFrames() {
    const windows = this._docShellsToWindows(this.docShells);
    return { frames: windows };
  }

  ensureWorkerDescriptorActorList() {
    if (this._workerDescriptorActorList === null) {
      this._workerDescriptorActorList = new WorkerDescriptorActorList(
        this.conn,
        {
          type: Ci.nsIWorkerDebugger.TYPE_DEDICATED,
          window: this.window,
        }
      );
    }
    return this._workerDescriptorActorList;
  }

  pauseWorkersUntilAttach(shouldPause) {
    this.ensureWorkerDescriptorActorList().workerPauser.setPauseMatching(
      shouldPause
    );
  }

  listWorkers() {
    return this.ensureWorkerDescriptorActorList()
      .getList()
      .then(actors => {
        const pool = new Pool(this.conn, "worker-targets");
        for (const actor of actors) {
          pool.manage(actor);
        }

        // Do not destroy the pool before transfering ownership to the newly created
        // pool, so that we do not accidently destroy actors that are still in use.
        if (this._workerDescriptorActorPool) {
          this._workerDescriptorActorPool.destroy();
        }

        this._workerDescriptorActorPool = pool;
        this._workerDescriptorActorList.onListChanged =
          this._onWorkerDescriptorActorListChanged;

        return {
          workers: actors,
        };
      });
  }

  logInPage(request) {
    const { text, category, flags } = request;
    const scriptErrorClass = Cc["@mozilla.org/scripterror;1"];
    const scriptError = scriptErrorClass.createInstance(Ci.nsIScriptError);
    scriptError.initWithWindowID(
      text,
      null,
      0,
      0,
      flags,
      category,
      getInnerId(this.window)
    );
    Services.console.logMessage(scriptError);
    return {};
  }

  _onWorkerDescriptorActorListChanged() {
    this._workerDescriptorActorList.onListChanged = null;
    this.emit("workerListChanged");
  }

  _onConsoleApiProfilerEvent() {
    // TODO: We will receive console-api-profiler events for any browser running
    // in the same process as this target. We should filter irrelevant events,
    // but console-api-profiler currently doesn't emit any information to identify
    // the origin of the event. See Bug 1731033.

    // The new performance panel is not compatible with console.profile().
    const warningFlag = 1;
    this.logInPage({
      text:
        "console.profile is not compatible with the new Performance recorder. " +
        "See https://bugzilla.mozilla.org/show_bug.cgi?id=1730896",
      category: "console.profile unavailable",
      flags: warningFlag,
    });
  }

  observe(subject, topic) {
    // Ignore any event that comes before/after the actor is attached.
    // That typically happens during Firefox shutdown.
    if (this.isDestroyed()) {
      return;
    }

    subject.QueryInterface(Ci.nsIDocShell);

    if (topic == "webnavigation-create") {
      this._onDocShellCreated(subject);
    } else if (topic == "webnavigation-destroy") {
      this._onDocShellDestroy(subject);
    }
  }

  _onDocShellCreated(docShell) {
    // (chrome-)webnavigation-create is fired very early during docshell
    // construction. In new root docshells within child processes, involving
    // BrowserChild, this event is from within this call:
    //   https://hg.mozilla.org/mozilla-central/annotate/74d7fb43bb44/dom/ipc/TabChild.cpp#l912
    // whereas the chromeEventHandler (and most likely other stuff) is set
    // later:
    //   https://hg.mozilla.org/mozilla-central/annotate/74d7fb43bb44/dom/ipc/TabChild.cpp#l944
    // So wait a tick before watching it:
    DevToolsUtils.executeSoon(() => {
      // Bug 1142752: sometimes, the docshell appears to be immediately
      // destroyed, bailout early to prevent random exceptions.
      if (docShell.isBeingDestroyed()) {
        return;
      }

      // In child processes, we have new root docshells,
      // let's watch them and all their child docshells.
      if (this._isRootDocShell(docShell) && this.watchNewDocShells) {
        this._progressListener.watch(docShell);
      }
      this._notifyDocShellsUpdate([docShell]);
    });
  }

  _onDocShellDestroy(docShell) {
    // Stop watching this docshell (the unwatch() method will check if we
    // started watching it before).
    this._unwatchDocShell(docShell);

    const webProgress = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
    this._notifyDocShellDestroy(webProgress);

    if (webProgress.DOMWindow == this._originalWindow) {
      // If the original top level document we connected to is removed,
      // we try to switch to any other top level document
      const rootDocShells = this.docShells.filter(d => {
        // Ignore docshells without a working DOM Window.
        // When we close firefox we have a chrome://extensions/content/dummy.xhtml
        // which is in process of being destroyed and we might try to fallback to it.
        // Unfortunately docshell.isBeingDestroyed() doesn't return true...
        return d != this.docShell && this._isRootDocShell(d) && d.DOMWindow;
      });
      if (rootDocShells.length) {
        const newRoot = rootDocShells[0];
        this._originalWindow = newRoot.DOMWindow;
        this._changeTopLevelDocument(this._originalWindow);
      } else {
        // If for some reason (typically during Firefox shutdown), the original
        // document is destroyed, and there is no other top level docshell,
        // we detach the actor to unregister all listeners and prevent any
        // exception.
        this.destroy();
      }
      return;
    }

    // If the currently targeted window global is destroyed, and we aren't on
    // the top-level document, we have to switch to the top-level one.
    if (
      webProgress.DOMWindow == this.window &&
      this.window != this._originalWindow
    ) {
      this._changeTopLevelDocument(this._originalWindow);
    }
  }

  _isRootDocShell(docShell) {
    // Should report as root docshell:
    //  - New top level window's docshells, when using ParentProcessTargetActor against a
    // process. It allows tracking iframes of the newly opened windows
    // like Browser console or new browser windows.
    //  - MozActivities or window.open frames on B2G, where a new root docshell
    // is spawn in the child process of the app.
    return !docShell.parent;
  }

  _docShellToWindow(docShell) {
    const webProgress = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
    const window = webProgress.DOMWindow;
    const id = docShell.outerWindowID;
    let parentID = undefined;
    // Ignore the parent of the original document on non-e10s firefox,
    // as we get the xul window as parent and don't care about it.
    // Furthermore, ignore setting parentID when parent window is same as
    // current window in order to deal with front end. e.g. toolbox will be fall
    // into infinite loop due to recursive search with by using parent id.
    if (
      window.parent &&
      window.parent != window &&
      window != this._originalWindow
    ) {
      parentID = window.parent.docShell.outerWindowID;
    }

    return {
      id,
      parentID,
      isTopLevel: window == this.originalWindow && this.isTopLevelTarget,
      url: window.location.href,
      title: window.document.title,
    };
  }

  // Convert docShell list to windows objects list being sent to the client
  _docShellsToWindows(docshells) {
    return docshells
      .filter(docShell => {
        // Ensure docShell.document is available.
        docShell.QueryInterface(Ci.nsIWebNavigation);

        // don't include transient about:blank documents
        if (docShell.document.isInitialDocument) {
          return false;
        }

        return true;
      })
      .map(docShell => this._docShellToWindow(docShell));
  }

  _notifyDocShellsUpdate(docshells) {
    // Only top level target uses frameUpdate in order to update the iframe dropdown.
    // This may eventually be replaced by Target listening and target switching.
    if (!this.isTopLevelTarget) {
      return;
    }

    const windows = this._docShellsToWindows(docshells);

    // Do not send the `frameUpdate` event if the windows array is empty.
    if (!windows.length) {
      return;
    }

    this.emit("frameUpdate", {
      frames: windows,
    });
  }

  _updateChildDocShells() {
    this._notifyDocShellsUpdate(this.docShells);
  }

  _notifyDocShellDestroy(webProgress) {
    // Only top level target uses frameUpdate in order to update the iframe dropdown.
    // This may eventually be replaced by Target listening and target switching.
    if (!this.isTopLevelTarget) {
      return;
    }

    webProgress = webProgress.QueryInterface(Ci.nsIWebProgress);
    const id = webProgress.DOMWindow.docShell.outerWindowID;
    this.emit("frameUpdate", {
      frames: [
        {
          id,
          destroy: true,
        },
      ],
    });
  }

  /**
   * Creates and manages the thread actor as part of the Browsing Context Target pool.
   * This sets up the content window for being debugged
   */
  _createThreadActor() {
    this.threadActor = new ThreadActor(this);
    this.manage(this.threadActor);
  }

  /**
   * Exits the current thread actor and removes it from the Browsing Context Target pool.
   * The content window is no longer being debugged after this call.
   */
  _destroyThreadActor() {
    if (this.threadActor) {
      this.threadActor.destroy();
      this.threadActor = null;
    }

    if (this._sourcesManager) {
      this._sourcesManager.destroy();
      this._sourcesManager = null;
    }
  }

  // Protocol Request Handlers

  detach() {
    // Destroy the actor in the next event loop in order
    // to ensure responding to the `detach` request.
    DevToolsUtils.executeSoon(() => {
      this.destroy();
    });

    return {};
  }

  /**
   * Bring the window global's window to front.
   */
  focus() {
    if (this.window) {
      this.window.focus();
    }
    return {};
  }

  goForward() {
    // Wait a tick so that the response packet can be dispatched before the
    // subsequent navigation event packet.
    Services.tm.dispatchToMainThread(
      DevToolsUtils.makeInfallible(() => {
        // This won't work while the browser is shutting down and we don't really
        // care.
        if (Services.startup.shuttingDown) {
          return;
        }

        this.webNavigation.goForward();
      }, "WindowGlobalTargetActor.prototype.goForward's delayed body")
    );

    return {};
  }

  goBack() {
    // Wait a tick so that the response packet can be dispatched before the
    // subsequent navigation event packet.
    Services.tm.dispatchToMainThread(
      DevToolsUtils.makeInfallible(() => {
        // This won't work while the browser is shutting down and we don't really
        // care.
        if (Services.startup.shuttingDown) {
          return;
        }

        this.webNavigation.goBack();
      }, "WindowGlobalTargetActor.prototype.goBack's delayed body")
    );

    return {};
  }

  /**
   * Reload the page in this window global.
   *
   * @backward-compat { legacy }
   *                  reload is preserved for third party tools. See Bug 1717837.
   *                  DevTools should use Descriptor::reloadDescriptor instead.
   */
  reload(request) {
    const force = request?.options?.force;
    // Wait a tick so that the response packet can be dispatched before the
    // subsequent navigation event packet.
    Services.tm.dispatchToMainThread(
      DevToolsUtils.makeInfallible(() => {
        // This won't work while the browser is shutting down and we don't really
        // care.
        if (Services.startup.shuttingDown) {
          return;
        }

        this.webNavigation.reload(
          force
            ? Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE
            : Ci.nsIWebNavigation.LOAD_FLAGS_NONE
        );
      }, "WindowGlobalTargetActor.prototype.reload's delayed body")
    );
    return {};
  }

  /**
   * Navigate this window global to a new location
   */
  navigateTo(request) {
    // Wait a tick so that the response packet can be dispatched before the
    // subsequent navigation event packet.
    Services.tm.dispatchToMainThread(
      DevToolsUtils.makeInfallible(() => {
        this.window.location = request.url;
      }, "WindowGlobalTargetActor.prototype.navigateTo's delayed body:" + request.url)
    );
    return {};
  }

  /**
   * For browsing-context targets which can't use the watcher configuration
   * actor (eg webextension targets), the client directly calls `reconfigure`.
   * Once all targets support the watcher, this method can be removed.
   */
  reconfigure(request) {
    const options = request.options || {};
    return this.updateTargetConfiguration(options);
  }

  /**
   * Apply target-specific options.
   *
   * This will be called by the watcher when the DevTools target-configuration
   * is updated, or when a target is created via JSWindowActors.
   */
  updateTargetConfiguration(options = {}, calledFromDocumentCreation = false) {
    if (!this.docShell) {
      // The window global is already closed.
      return;
    }

    // Also update configurations which applies to all target types
    super.updateTargetConfiguration(options, calledFromDocumentCreation);

    let reload = false;
    if (typeof options.touchEventsOverride !== "undefined") {
      const enableTouchSimulator = options.touchEventsOverride === "enabled";

      // We want to reload the document if it's an "existing" top level target on which
      // the touch simulator will be toggled and the user has turned the
      // "reload on touch simulation" setting on.
      if (
        enableTouchSimulator !== this.touchSimulator.enabled &&
        options.reloadOnTouchSimulationToggle === true &&
        this.isTopLevelTarget &&
        !calledFromDocumentCreation
      ) {
        reload = true;
      }

      if (enableTouchSimulator) {
        this.touchSimulator.start();
      } else {
        this.touchSimulator.stop();
      }
    }

    if (typeof options.customFormatters !== "undefined") {
      this.customFormatters = options.customFormatters;
    }

    if (typeof options.useSimpleHighlightersForReducedMotion == "boolean") {
      this._useSimpleHighlightersForReducedMotion =
        options.useSimpleHighlightersForReducedMotion;
      this.emit("use-simple-highlighters-updated");
    }

    if (!this.isTopLevelTarget) {
      // Following DevTools target options should only apply to the top target and be
      // propagated through the window global tree via the platform.
      return;
    }
    if (typeof options.restoreFocus == "boolean") {
      this._restoreFocus = options.restoreFocus;
    }
    if (typeof options.recordAllocations == "object") {
      const actor = this._memoryActor;
      if (options.recordAllocations == null) {
        actor.stopRecordingAllocations();
      } else {
        actor.attach();
        actor.startRecordingAllocations(options.recordAllocations);
      }
    }

    if (reload) {
      this.webNavigation.reload(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
    }
  }

  get touchSimulator() {
    if (!this._touchSimulator) {
      this._touchSimulator = new TouchSimulator(this.chromeEventHandler);
    }

    return this._touchSimulator;
  }

  /**
   * Opposite of the updateTargetConfiguration method, that resets document
   * state when closing the toolbox.
   */
  _restoreTargetConfiguration() {
    if (this._restoreFocus && this.browsingContext?.isActive) {
      try {
        this.window.focus();
      } catch (e) {
        // When closing devtools while navigating, focus() may throw NS_ERROR_XPC_SECURITY_MANAGER_VETO
        if (e.result != Cr.NS_ERROR_XPC_SECURITY_MANAGER_VETO) {
          throw e;
        }
      }
    }
  }

  _changeTopLevelDocument(window) {
    // In case of WebExtension, still using one WindowGlobalTarget instance for many document,
    // when reloading the add-on we might not destroy the previous target and wait for the next
    // one to come and destroy it.
    if (this.window) {
      // Fake a will-navigate on the previous document
      // to let a chance to unregister it
      this._willNavigate({
        window: this.window,
        newURI: window.location.href,
        request: null,
        isFrameSwitching: true,
        navigationStart: Date.now(),
      });

      this._windowDestroyed(this.window, {
        isFrozen: true,
        isFrameSwitching: true,
      });
    }

    // Immediately change the window as this window, if in process of unload
    // may already be non working on the next cycle and start throwing
    this._setWindow(window);

    DevToolsUtils.executeSoon(() => {
      // No need to do anything more if the actor is destroyed.
      // e.g. the client has been closed and the actors destroyed in the meantime.
      if (this.isDestroyed()) {
        return;
      }

      // Then fake window-ready and navigate on the given document
      this._windowReady(window, { isFrameSwitching: true });
      DevToolsUtils.executeSoon(() => {
        this._navigate(window, true);
      });
    });
  }

  _setWindow(window) {
    // Here is the very important call where we switch the currently targeted
    // window global (it will indirectly update this.window and many other
    // attributes defined from docShell).
    this.docShell = window.docShell;
    this.emit("changed-toplevel-document");
    this.emit("frameUpdate", {
      selected: this.outerWindowID,
    });
  }

  /**
   * Handle location changes, by clearing the previous debuggees and enabling
   * debugging, which may have been disabled temporarily by the
   * DebuggerProgressListener.
   */
  _windowReady(window, { isFrameSwitching, isBFCache } = {}) {
    if (this.ignoreSubFrames) {
      return;
    }
    const isTopLevel = window == this.window;

    // We just reset iframe list on WillNavigate, so we now list all existing
    // frames when we load a new document in the original window
    if (window == this._originalWindow && !isFrameSwitching) {
      this._updateChildDocShells();
    }

    // If this follows WindowGlobal lifecycle, a new Target actor will be spawn for the top level
    // target document. Only notify about in-process iframes.
    // Note that OOP iframes won't emit window-ready and will also have their dedicated target.
    // Also, we allow window-ready to be fired for iframe switching of top level documents,
    // otherwise the iframe dropdown no longer works with server side targets.
    if (this.followWindowGlobalLifeCycle && isTopLevel && !isFrameSwitching) {
      return;
    }

    this.emit("window-ready", {
      window,
      isTopLevel,
      isBFCache,
      id: getWindowID(window),
      isFrameSwitching,
    });
  }

  _windowDestroyed(
    window,
    { id = null, isFrozen = false, isFrameSwitching = false }
  ) {
    if (this.ignoreSubFrames) {
      return;
    }
    const isTopLevel = window == this.window;

    // If this follows WindowGlobal lifecycle, this target will be destroyed, alongside its top level document.
    // Only notify about in-process iframes.
    // Note that OOP iframes won't emit window-ready and will also have their dedicated target.
    // Also, we allow window-destroyed to be fired for iframe switching of top level documents,
    // otherwise the iframe dropdown no longer works with server side targets.
    if (this.followWindowGlobalLifeCycle && isTopLevel && !isFrameSwitching) {
      return;
    }

    this.emit("window-destroyed", {
      window,
      isTopLevel,
      id: id || getWindowID(window),
      isFrozen,
    });
  }

  /**
   * Start notifying server and client about a new document being loaded in the
   * currently targeted window global.
   */
  _willNavigate({
    window,
    newURI,
    request,
    isFrameSwitching = false,
    navigationStart,
  }) {
    if (this.ignoreSubFrames) {
      return;
    }
    let isTopLevel = window == this.window;

    let reset = false;
    if (window == this._originalWindow && !isFrameSwitching) {
      // If the top level document changes and we are targeting an iframe, we
      // need to reset to the upcoming new top level document. But for this
      // will-navigate event, we will dispatch on the old window. (The inspector
      // codebase expect to receive will-navigate for the currently displayed
      // document in order to cleanup the markup view)
      if (this.window != this._originalWindow) {
        reset = true;
        window = this.window;
        isTopLevel = true;
      }
    }

    // will-navigate event needs to be dispatched synchronously, by calling the
    // listeners in the order or registration. This event fires once navigation
    // starts, (all pending user prompts are dealt with), but before the first
    // request starts.
    this.emit("will-navigate", {
      window,
      isTopLevel,
      newURI,
      request,
      navigationStart,
      isFrameSwitching,
    });

    // We don't do anything for inner frames here.
    // (we will only update thread actor on window-ready)
    if (!isTopLevel) {
      return;
    }

    // When the actor acts as a WindowGlobalTarget, will-navigate won't fired.
    // Instead we will receive a new top level target with isTargetSwitching=true.
    if (!this.followWindowGlobalLifeCycle) {
      this.emit("tabNavigated", {
        url: newURI,
        state: "start",
        isFrameSwitching,
      });
    }

    if (reset) {
      this._setWindow(this._originalWindow);
    }
  }

  /**
   * Notify server and client about a new document done loading in the current
   * targeted window global.
   */
  _navigate(window, isFrameSwitching = false) {
    if (this.ignoreSubFrames) {
      return;
    }
    const isTopLevel = window == this.window;

    // navigate event needs to be dispatched synchronously,
    // by calling the listeners in the order or registration.
    // This event is fired once the document is loaded,
    // after the load event, it's document ready-state is 'complete'.
    this.emit("navigate", {
      window,
      isTopLevel,
    });

    // We don't do anything for inner frames here.
    // (we will only update thread actor on window-ready)
    if (!isTopLevel) {
      return;
    }

    // We may still significate when the document is done loading, via navigate.
    // But as we no longer fire the "will-navigate", may be it is better to find
    // other ways to get to our means.
    // Listening to "navigate" is misleading as the document may already be loaded
    // if we just opened the DevTools. So it is better to use "watch" pattern
    // and instead have the actor either emit immediately resources as they are
    // already available, or later on as the load progresses.
    if (this.followWindowGlobalLifeCycle) {
      return;
    }

    this.emit("tabNavigated", {
      url: this.url,
      title: this.title,
      state: "stop",
      isFrameSwitching,
    });
  }

  removeActorByName(name) {
    if (name in this._extraActors) {
      const actor = this._extraActors[name];
      if (this._targetScopedActorPool.has(actor)) {
        this._targetScopedActorPool.removeActor(actor);
      }
      delete this._extraActors[name];
    }
  }
}

exports.WindowGlobalTargetActor = WindowGlobalTargetActor;

class DebuggerProgressListener {
  /**
   * The DebuggerProgressListener class is an nsIWebProgressListener which
   * handles onStateChange events for the targeted window global. If the user
   * tries to navigate away from a paused page, the listener makes sure that the
   * debuggee is resumed before the navigation begins.
   *
   * @param WindowGlobalTargetActor targetActor
   *        The window global target actor associated with this listener.
   */
  constructor(targetActor) {
    this._targetActor = targetActor;
    this._onWindowCreated = this.onWindowCreated.bind(this);
    this._onWindowHidden = this.onWindowHidden.bind(this);

    // Watch for windows destroyed (global observer that will need filtering)
    Services.obs.addObserver(this, "inner-window-destroyed");

    // XXX: for now we maintain the list of windows we know about in this instance
    // so that we can discriminate windows we care about when observing
    // inner-window-destroyed events. Bug 1016952 would remove the need for this.
    this._knownWindowIDs = new Map();

    this._watchedDocShells = new WeakSet();
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsISupportsWeakReference",
  ]);

  destroy() {
    Services.obs.removeObserver(this, "inner-window-destroyed");
    this._knownWindowIDs.clear();
    this._knownWindowIDs = null;
  }

  watch(docShell) {
    // Add the docshell to the watched set. We're actually adding the window,
    // because docShell objects are not wrappercached and would be rejected
    // by the WeakSet.
    const docShellWindow = docShell.domWindow;
    this._watchedDocShells.add(docShellWindow);

    const webProgress = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
    webProgress.addProgressListener(
      this,
      Ci.nsIWebProgress.NOTIFY_STATE_WINDOW |
        Ci.nsIWebProgress.NOTIFY_STATE_DOCUMENT
    );

    const handler = getDocShellChromeEventHandler(docShell);
    handler.addEventListener("DOMWindowCreated", this._onWindowCreated, true);
    handler.addEventListener("pageshow", this._onWindowCreated, true);
    handler.addEventListener("pagehide", this._onWindowHidden, true);

    // Dispatch the _windowReady event on the targetActor for pre-existing windows
    const windows = this._targetActor.ignoreSubFrames
      ? [docShellWindow]
      : this._getWindowsInDocShell(docShell);
    for (const win of windows) {
      this._targetActor._windowReady(win);
      this._knownWindowIDs.set(getWindowID(win), win);
    }

    // Immediately enable CSS error reports on new top level docshells, if this was already enabled.
    // This is specific to MBT and WebExtension targets (so the isRootActor check).
    if (
      this._targetActor.isRootActor &&
      this._targetActor.docShell.cssErrorReportingEnabled
    ) {
      docShell.cssErrorReportingEnabled = true;
    }
  }

  unwatch(docShell) {
    // If the docshell is being destroyed, we won't be able to retrieve its related window object,
    // which is the key ingredient for all cleanup operations done in this method.
    if (docShell.isBeingDestroyed()) {
      return;
    }

    const docShellWindow = docShell.domWindow;
    if (!this._watchedDocShells.has(docShellWindow)) {
      return;
    }
    this._watchedDocShells.delete(docShellWindow);

    const webProgress = docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebProgress);
    // During process shutdown, the docshell may already be cleaned up and throw
    try {
      webProgress.removeProgressListener(this);
    } catch (e) {
      // ignore
    }

    const handler = getDocShellChromeEventHandler(docShell);
    handler.removeEventListener(
      "DOMWindowCreated",
      this._onWindowCreated,
      true
    );
    handler.removeEventListener("pageshow", this._onWindowCreated, true);
    handler.removeEventListener("pagehide", this._onWindowHidden, true);

    const windows = this._targetActor.ignoreSubFrames
      ? [docShellWindow]
      : this._getWindowsInDocShell(docShell);
    for (const win of windows) {
      this._knownWindowIDs.delete(getWindowID(win));
    }
  }

  _getWindowsInDocShell(docShell) {
    return getChildDocShells(docShell).map(d => {
      return d.domWindow;
    });
  }

  onWindowCreated = DevToolsUtils.makeInfallible(function (evt) {
    if (this._targetActor.isDestroyed()) {
      return;
    }

    // If we're in a frame swap (which occurs when toggling RDM, for example), then we can
    // ignore this event, as the window never really went anywhere for our purposes.
    if (evt.inFrameSwap) {
      return;
    }

    const window = evt.target.defaultView;
    if (!window) {
      // Some old UIs might emit unrelated events called pageshow/pagehide on
      // elements which are not documents. Bail in this case. See Bug 1669666.
      return;
    }

    const innerID = getWindowID(window);

    // This handler is called for two events: "DOMWindowCreated" and "pageshow".
    // Bail out if we already processed this window.
    if (this._knownWindowIDs.has(innerID)) {
      return;
    }
    this._knownWindowIDs.set(innerID, window);

    // For a regular page navigation, "DOMWindowCreated" is fired before
    // "pageshow". If the current event is "pageshow" but we have not processed
    // the window yet, it means this is a BF cache navigation. In theory,
    // `event.persisted` should be set for BF cache navigation events, but it is
    // not always available, so we fallback on checking if "pageshow" is the
    // first event received for a given window (see Bug 1378133).
    const isBFCache = evt.type == "pageshow";

    this._targetActor._windowReady(window, { isBFCache });
  }, "DebuggerProgressListener.prototype.onWindowCreated");

  onWindowHidden = DevToolsUtils.makeInfallible(function (evt) {
    if (this._targetActor.isDestroyed()) {
      return;
    }

    // If we're in a frame swap (which occurs when toggling RDM, for example), then we can
    // ignore this event, as the window isn't really going anywhere for our purposes.
    if (evt.inFrameSwap) {
      return;
    }

    // Only act as if the window has been destroyed if the 'pagehide' event
    // was sent for a persisted window (persisted is set when the page is put
    // and frozen in the bfcache). If the page isn't persisted, the observer's
    // inner-window-destroyed event will handle it.
    if (!evt.persisted) {
      return;
    }

    const window = evt.target.defaultView;
    if (!window) {
      // Some old UIs might emit unrelated events called pageshow/pagehide on
      // elements which are not documents. Bail in this case. See Bug 1669666.
      return;
    }

    this._targetActor._windowDestroyed(window, { isFrozen: true });
    this._knownWindowIDs.delete(getWindowID(window));
  }, "DebuggerProgressListener.prototype.onWindowHidden");

  observe = DevToolsUtils.makeInfallible(function (subject) {
    if (this._targetActor.isDestroyed()) {
      return;
    }

    // Because this observer will be called for all inner-window-destroyed in
    // the application, we need to filter out events for windows we are not
    // watching
    const innerID = subject.QueryInterface(Ci.nsISupportsPRUint64).data;
    const window = this._knownWindowIDs.get(innerID);
    if (window) {
      this._knownWindowIDs.delete(innerID);
      this._targetActor._windowDestroyed(window, { id: innerID });
    }

    // Bug 1598364: when debugging browser.xhtml from the Browser Toolbox
    // the DOMWindowCreated/pageshow/pagehide event listeners have to be
    // re-registered against the next document when we reload browser.html
    // (or navigate to another doc).
    // That's because we registered the listener on docShell.domWindow as
    // top level windows don't have a chromeEventHandler.
    if (
      this._watchedDocShells.has(window) &&
      !window.docShell.chromeEventHandler
    ) {
      // First cleanup all the existing listeners
      this.unwatch(window.docShell);
      // Re-register new ones. The docShell is already referencing the new document.
      this.watch(window.docShell);
    }
  }, "DebuggerProgressListener.prototype.observe");

  onStateChange = DevToolsUtils.makeInfallible(function (
    progress,
    request,
    flag
  ) {
    if (this._targetActor.isDestroyed()) {
      return;
    }
    progress.QueryInterface(Ci.nsIDocShell);
    if (progress.isBeingDestroyed()) {
      return;
    }

    const isStart = flag & Ci.nsIWebProgressListener.STATE_START;
    const isStop = flag & Ci.nsIWebProgressListener.STATE_STOP;
    const isDocument = flag & Ci.nsIWebProgressListener.STATE_IS_DOCUMENT;
    const isWindow = flag & Ci.nsIWebProgressListener.STATE_IS_WINDOW;

    // Ideally, we would fetch navigationStart from window.performance.timing.navigationStart
    // but as WindowGlobal isn't instantiated yet we don't have access to it.
    // This is ultimately handed over to DocumentEventListener, which uses this.
    // See its comment about WILL_NAVIGATE_TIME_SHIFT for more details about the related workaround.
    const navigationStart = Date.now();

    // Catch any iframe location change
    if (isDocument && isStop) {
      // Watch document stop to ensure having the new iframe url.
      this._targetActor._notifyDocShellsUpdate([progress]);
    }

    const window = progress.DOMWindow;
    if (isDocument && isStart) {
      // One of the earliest events that tells us a new URI
      // is being loaded in this window.
      const newURI = request instanceof Ci.nsIChannel ? request.URI.spec : null;
      this._targetActor._willNavigate({
        window,
        newURI,
        request,
        isFrameSwitching: false,
        navigationStart,
      });
    }
    if (isWindow && isStop) {
      // Don't dispatch "navigate" event just yet when there is a redirect to
      // about:neterror page.
      // Navigating to about:neterror will make `status` be something else than NS_OK.
      // But for some error like NS_BINDING_ABORTED we don't want to emit any `navigate`
      // event as the page load has been cancelled and the related page document is going
      // to be a dead wrapper.
      if (
        request.status != Cr.NS_OK &&
        request.status != Cr.NS_BINDING_ABORTED
      ) {
        // Instead, listen for DOMContentLoaded as about:neterror is loaded
        // with LOAD_BACKGROUND flags and never dispatches load event.
        // That may be the same reason why there is no onStateChange event
        // for about:neterror loads.
        const handler = getDocShellChromeEventHandler(progress);
        const onLoad = evt => {
          // Ignore events from iframes
          if (evt.target === window.document) {
            handler.removeEventListener("DOMContentLoaded", onLoad, true);
            this._targetActor._navigate(window);
          }
        };
        handler.addEventListener("DOMContentLoaded", onLoad, true);
      } else {
        // Somewhat equivalent of load event.
        // (window.document.readyState == complete)
        this._targetActor._navigate(window);
      }
    }
  }, "DebuggerProgressListener.prototype.onStateChange");
}
