/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This module contains code for managing APIs that need to run in the
 * parent process, and handles the parent side of operations that need
 * to be proxied from ExtensionChild.sys.mjs.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  BroadcastConduit: "resource://gre/modules/ConduitsParent.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  DevToolsShim: "chrome://devtools-startup/content/DevToolsShim.sys.mjs",
  ExtensionActivityLog: "resource://gre/modules/ExtensionActivityLog.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  GeckoViewConnection: "resource://gre/modules/GeckoViewWebExtension.sys.mjs",
  MessageManagerProxy: "resource://gre/modules/MessageManagerProxy.sys.mjs",
  NativeApp: "resource://gre/modules/NativeMessaging.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
  getErrorNameForTelemetry: "resource://gre/modules/ExtensionTelemetry.sys.mjs",
  WebNavigationFrames: "resource://gre/modules/WebNavigationFrames.sys.mjs",
  aomStartup: {
    service: "@mozilla.org/addons/addon-manager-startup;1",
    iid: Ci.amIAddonManagerStartup,
  },
});

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const DUMMY_PAGE_URI = Services.io.newURI(
  "chrome://extensions/content/dummy.xhtml"
);

var { BaseContext, CanOfAPIs, SchemaAPIManager, SpreadArgs, redefineGetter } =
  ExtensionCommon;

var {
  DefaultMap,
  DefaultWeakMap,
  ExtensionError,
  promiseDocumentLoaded,
  promiseEvent,
  promiseObserved,
} = ExtensionUtils;

const ERROR_NO_RECEIVERS =
  "Could not establish connection. Receiving end does not exist.";

const BASE_SCHEMA = "chrome://extensions/content/schemas/manifest.json";
const CATEGORY_EXTENSION_MODULES = "webextension-modules";
const CATEGORY_EXTENSION_SCHEMAS = "webextension-schemas";
const CATEGORY_EXTENSION_SCRIPTS = "webextension-scripts";

let schemaURLs = new Set();

schemaURLs.add("chrome://extensions/content/schemas/experiments.json");

let GlobalManager;
let ParentAPIManager;

function verifyActorForContext(actor, context) {
  if (JSWindowActorParent.isInstance(actor)) {
    let target = actor.browsingContext.top.embedderElement;
    if (context.parentMessageManager !== target.messageManager) {
      throw new Error("Got message on unexpected message manager");
    }
  } else if (JSProcessActorParent.isInstance(actor)) {
    if (actor.manager.remoteType !== context.extension.remoteType) {
      throw new Error("Got message from unexpected process");
    }
  }
}

// This object loads the ext-*.js scripts that define the extension API.
let apiManager = new (class extends SchemaAPIManager {
  constructor() {
    super("main", lazy.Schemas);
    this.initialized = null;

    /* eslint-disable mozilla/balanced-listeners */
    this.on("startup", (e, extension) => {
      return extension.apiManager.onStartup(extension);
    });

    this.on("update", async (e, { id, resourceURI, isPrivileged }) => {
      let modules = this.eventModules.get("update");
      if (modules.size == 0) {
        return;
      }

      let extension = new lazy.ExtensionData(resourceURI, isPrivileged);
      await extension.loadManifest();

      return Promise.all(
        Array.from(modules).map(async apiName => {
          let module = await this.asyncLoadModule(apiName);
          module.onUpdate(id, extension.manifest);
        })
      );
    });

    this.on("uninstall", (e, { id }) => {
      let modules = this.eventModules.get("uninstall");
      return Promise.all(
        Array.from(modules).map(async apiName => {
          let module = await this.asyncLoadModule(apiName);
          return module.onUninstall(id);
        })
      );
    });
    /* eslint-enable mozilla/balanced-listeners */

    // Handle any changes that happened during startup
    let disabledIds = lazy.AddonManager.getStartupChanges(
      lazy.AddonManager.STARTUP_CHANGE_DISABLED
    );
    if (disabledIds.length) {
      this._callHandlers(disabledIds, "disable", "onDisable");
    }

    let uninstalledIds = lazy.AddonManager.getStartupChanges(
      lazy.AddonManager.STARTUP_CHANGE_UNINSTALLED
    );
    if (uninstalledIds.length) {
      this._callHandlers(uninstalledIds, "uninstall", "onUninstall");
    }
  }

  getModuleJSONURLs() {
    return Array.from(
      Services.catMan.enumerateCategory(CATEGORY_EXTENSION_MODULES),
      ({ value }) => value
    );
  }

  // Loads all the ext-*.js scripts currently registered.
  lazyInit() {
    if (this.initialized) {
      return this.initialized;
    }

    let modulesPromise = StartupCache.other.get(["parentModules"], () =>
      this.loadModuleJSON(this.getModuleJSONURLs())
    );

    let scriptURLs = [];
    for (let { value } of Services.catMan.enumerateCategory(
      CATEGORY_EXTENSION_SCRIPTS
    )) {
      scriptURLs.push(value);
    }

    let promise = (async () => {
      let scripts = await Promise.all(
        scriptURLs.map(url => ChromeUtils.compileScript(url))
      );

      this.initModuleData(await modulesPromise);

      this.initGlobal();
      for (let script of scripts) {
        script.executeInGlobal(this.global);
      }

      // Load order matters here. The base manifest defines types which are
      // extended by other schemas, so needs to be loaded first.
      return lazy.Schemas.load(BASE_SCHEMA).then(() => {
        let promises = [];
        for (let { value } of Services.catMan.enumerateCategory(
          CATEGORY_EXTENSION_SCHEMAS
        )) {
          promises.push(lazy.Schemas.load(value));
        }
        for (let [url, { content }] of this.schemaURLs) {
          promises.push(lazy.Schemas.load(url, content));
        }
        for (let url of schemaURLs) {
          promises.push(lazy.Schemas.load(url));
        }
        return Promise.all(promises).then(() => {
          lazy.Schemas.updateSharedSchemas();
        });
      });
    })();

    Services.mm.addMessageListener("Extension:GetFrameData", this);

    this.initialized = promise;
    return this.initialized;
  }

  receiveMessage({ target }) {
    let data = GlobalManager.frameData.get(target) || {};
    Object.assign(data, this.global.tabTracker.getBrowserData(target));
    return data;
  }

  // Call static handlers for the given event on the given extension ids,
  // and set up a shutdown blocker to ensure they all complete.
  _callHandlers(ids, event, method) {
    let promises = Array.from(this.eventModules.get(event))
      .map(async modName => {
        let module = await this.asyncLoadModule(modName);
        return ids.map(id => module[method](id));
      })
      .flat();
    if (event === "disable") {
      promises.push(...ids.map(id => this.emit("disable", id)));
    }
    if (event === "enabling") {
      promises.push(...ids.map(id => this.emit("enabling", id)));
    }

    lazy.AsyncShutdown.profileBeforeChange.addBlocker(
      `Extension API ${event} handlers for ${ids.join(",")}`,
      Promise.all(promises)
    );
  }
})();

/**
 * @typedef {object} ParentPort
 * @property {boolean} [native]
 * @property {string} [senderChildId]
 * @property {function(StructuredCloneHolder): any} onPortMessage
 * @property {Function} onPortDisconnect
 */

// Receives messages related to the extension messaging API and forwards them
// to relevant child messengers.  Also handles Native messaging and GeckoView.
/** @typedef {typeof ProxyMessenger} NativeMessenger */
const ProxyMessenger = {
  /** @type {Map<number, ParentPort>} */
  ports: new Map(),
  /** @type {Map<number, Promise>} */
  portPromises: new Map(),

  init() {
    this.conduit = new lazy.BroadcastConduit(ProxyMessenger, {
      id: "ProxyMessenger",
      reportOnClosed: "portId",
      recv: ["PortConnect", "PortMessage", "NativeMessage", "RuntimeMessage"],
      cast: ["PortConnect", "PortMessage", "PortDisconnect", "RuntimeMessage"],
    });
  },

  openNative(nativeApp, sender) {
    let context = ParentAPIManager.getContextById(sender.childId);
    if (context.extension.hasPermission("geckoViewAddons")) {
      return new lazy.GeckoViewConnection(
        this.getSender(context.extension, sender),
        sender.actor.browsingContext.top.embedderElement,
        nativeApp,
        context.extension.hasPermission("nativeMessagingFromContent")
      );
    } else if (sender.verified) {
      return new lazy.NativeApp(context, nativeApp);
    }
    sender = this.getSender(context.extension, sender);
    throw new Error(`Native messaging not allowed: ${JSON.stringify(sender)}`);
  },

  recvNativeMessage({ nativeApp, holder }, { sender }) {
    const app = this.openNative(nativeApp, sender);

    // Track in-flight NativeApp sendMessage requests as
    // a NativeApp port destroyed when the request
    // has been handled.
    const promiseSendMessage = app.sendMessage(holder);
    const sendMessagePort = {
      native: true,
      senderChildId: sender.childId,
    };
    this.trackNativeAppPort(sendMessagePort);
    const untrackSendMessage = () => this.untrackNativeAppPort(sendMessagePort);
    promiseSendMessage.then(untrackSendMessage, untrackSendMessage);

    return promiseSendMessage;
  },

  getSender(extension, source) {
    let sender = {
      contextId: source.id,
      id: source.extensionId,
      envType: source.envType,
      url: source.url,
    };

    if (JSWindowActorParent.isInstance(source.actor)) {
      let { currentWindowContext, top } = source.actor.browsingContext;
      let browser = top.embedderElement;
      let data =
        browser && apiManager.global.tabTracker.getBrowserData(browser);
      if (data?.tabId > 0) {
        sender.tab = extension.tabManager.get(data.tabId, null)?.convert();
        // frameId is documented to only be set if sender.tab is set.
        sender.frameId = source.frameId;
      }

      let principal = currentWindowContext.documentPrincipal;
      // We intend the serialization of null principals *and* file scheme to be
      // "null".
      sender.origin = new URL(principal.originNoSuffix).origin;
    } else if (source.verified) {
      sender.origin = `moz-extension://${extension.uuid}`;
    }

    return sender;
  },

  getTopBrowsingContextId(tabId) {
    // If a tab alredy has content scripts, no need to check private browsing.
    let tab = apiManager.global.tabTracker.getTab(tabId, null);
    if (!tab || (tab.browser || tab).getAttribute("pending") === "true") {
      // No receivers in discarded tabs, so bail early to keep the browser lazy.
      throw new ExtensionError(ERROR_NO_RECEIVERS);
    }
    let browser = tab.linkedBrowser || tab.browser;
    return browser.browsingContext.id;
  },

  // TODO: Rework/simplify this and getSender/getTopBC after bug 1580766.
  async normalizeArgs(arg, sender) {
    arg.extensionId = arg.extensionId || sender.extensionId;
    let extension = GlobalManager.extensionMap.get(arg.extensionId);
    if (!extension) {
      return Promise.reject({ message: ERROR_NO_RECEIVERS });
    }
    // TODO bug 1852317: This should not be unconditional.
    await extension.wakeupBackground?.();

    arg.sender = this.getSender(extension, sender);
    arg.topBC = arg.tabId && this.getTopBrowsingContextId(arg.tabId);
    return arg.tabId ? "tab" : "messenger";
  },

  async recvRuntimeMessage(arg, { sender }) {
    arg.firstResponse = true;
    let kind = await this.normalizeArgs(arg, sender);
    arg.query = true;
    let result = await this.conduit.castRuntimeMessage(kind, arg);
    if (!result) {
      // "throw new ExtensionError" cannot be used because then the stack of the
      // sendMessage call would not be added to the error object generated by
      // context.normalizeError. Test coverage by test_ext_error_location.js.
      return Promise.reject({ message: ERROR_NO_RECEIVERS });
    }
    return result.value;
  },

  async recvPortConnect(arg, { sender }) {
    if (arg.native) {
      let port = this.openNative(arg.name, sender).onConnect(arg.portId, this);
      port.senderChildId = sender.childId;
      port.native = true;
      this.ports.set(arg.portId, port);
      this.trackNativeAppPort(port);
      return;
    }

    // PortMessages that follow will need to wait for the port to be opened.
    let { promise, resolve, reject } = Promise.withResolvers();
    this.portPromises.set(arg.portId, promise);

    try {
      let kind = await this.normalizeArgs(arg, sender);
      arg.query = true;
      let all = await this.conduit.castPortConnect(kind, arg);
      resolve();

      // If there are no active onConnect listeners.
      if (!all.some(x => x.value)) {
        throw new ExtensionError(ERROR_NO_RECEIVERS);
      }
    } catch (err) {
      // Throw _and_ reject with error, so everything awaiting this port fails.
      reject(err);
      throw err;
    } finally {
      this.portPromises.delete(arg.portId);
    }
  },

  async recvPortMessage({ holder }, { sender }) {
    if (sender.native) {
      // If the nativeApp port connect fails (e.g. if triggered by a content
      // script), the portId may not be in the map (because it did throw in
      // the openNative method).
      return this.ports.get(sender.portId)?.onPortMessage(holder);
    }
    // Wait for onConnect dispatch to complete to avoid missing onMessage.
    await this.portPromises.get(sender.portId);
    this.sendPortMessage(sender.portId, holder, !sender.source);
  },

  async recvConduitClosed(sender) {
    let app = this.ports.get(sender.portId);
    if (this.ports.delete(sender.portId) && sender.native) {
      this.untrackNativeAppPort(app);
      return app.onPortDisconnect();
    }
    // Wait for onConnect dispatch to complete to avoid missing onDisconnect.
    await this.portPromises.get(sender.portId);
    this.sendPortDisconnect(sender.portId, null, !sender.source);
  },

  sendPortMessage(portId, holder, source = true) {
    this.conduit.castPortMessage("port", { portId, source, holder });
  },

  sendPortDisconnect(portId, error, source = true) {
    let port = this.ports.get(portId);
    this.untrackNativeAppPort(port);
    this.ports.delete(portId);
    try {
      // This may throw: https://bugzilla.mozilla.org/show_bug.cgi?id=1931902#c3
      this.conduit.castPortDisconnect("port", { portId, source, error });
    } catch (e) {
      Cu.reportError(e);
    }
  },

  trackNativeAppPort(port) {
    if (!port?.native) {
      return;
    }

    try {
      let context = ParentAPIManager.getContextById(port.senderChildId);
      context?.trackNativeAppPort(port);
    } catch {
      // getContextById will throw if the context has been destroyed
      // in the meantime.
    }
  },

  untrackNativeAppPort(port) {
    if (!port?.native) {
      return;
    }

    try {
      let context = ParentAPIManager.getContextById(port.senderChildId);
      context?.untrackNativeAppPort(port);
    } catch {
      // getContextById will throw if the context has been destroyed
      // in the meantime.
    }
  },
};
ProxyMessenger.init();

// Responsible for loading extension APIs into the right globals.
GlobalManager = {
  // Map[extension ID -> Extension]. Determines which extension is
  // responsible for content under a particular extension ID.
  extensionMap: new Map(),
  initialized: false,

  /** @type {WeakMap<XULBrowserElement, object>} Extension Context init data. */
  frameData: new WeakMap(),

  init(extension) {
    if (this.extensionMap.size == 0) {
      apiManager.on("extension-browser-inserted", this._onExtensionBrowser);
      this.initialized = true;
    }
    this.extensionMap.set(extension.id, extension);
  },

  uninit(extension) {
    this.extensionMap.delete(extension.id);

    if (this.extensionMap.size == 0 && this.initialized) {
      apiManager.off("extension-browser-inserted", this._onExtensionBrowser);
      this.initialized = false;
    }
  },

  _onExtensionBrowser(type, browser, data = {}) {
    data.viewType = browser.getAttribute("webextension-view-type");
    if (data.viewType) {
      GlobalManager.frameData.set(browser, data);
    }
  },

  getExtension(extensionId) {
    return this.extensionMap.get(extensionId);
  },
};

/**
 * The proxied parent side of a context in ExtensionChild.sys.mjs, for the
 * parent side of a proxied API.
 */
class ProxyContextParent extends BaseContext {
  constructor(envType, extension, params, browsingContext, principal) {
    super(envType, extension);

    this.childId = params.childId;
    this.uri = Services.io.newURI(params.url);
    this.browsingContext = browsingContext;

    this.incognito = params.incognito;

    this.listenerPromises = new Set();

    // browsingContext is null when subclassed by BackgroundWorkerContextParent.
    const xulBrowser = browsingContext?.top.embedderElement;
    // This message manager is used by ParentAPIManager to send messages and to
    // close the ProxyContext if the underlying message manager closes. This
    // message manager object may change when `xulBrowser` swaps docshells, e.g.
    // when a tab is moved to a different window.
    // TODO: Is xulBrowser correct for ContentScriptContextParent? Messages
    // through the xulBrowser won't reach cross-process iframes.
    this.messageManagerProxy =
      xulBrowser && new lazy.MessageManagerProxy(xulBrowser);

    Object.defineProperty(this, "principal", {
      value: principal,
      enumerable: true,
      configurable: true,
    });

    this.listenerProxies = new Map();

    this.pendingEventBrowser = null;
    this.callContextData = null;

    // Set of active NativeApp ports.
    this.activeNativePorts = new WeakSet();

    // Set of pending queryRunListener promises.
    this.runListenerPromises = new Set();

    apiManager.emit("proxy-context-load", this);
  }

  get isProxyContextParent() {
    return true;
  }

  get frameId() {
    if (!this.browsingContext) {
      return -1;
    }

    return lazy.WebNavigationFrames.getFrameId(this.browsingContext);
  }

  get contextType() {
    switch (this.viewType) {
      case "background_worker": // intentionally fall-through
      case "background":
        return "BACKGROUND";
      case "popup":
        return "POPUP";
      case "sidebar":
        return "SIDE_PANEL";
      case "tab":
        return "TAB";
      default:
        throw new Error(
          `Unexpected missing contextType mapping for viewType "${this.viewType}"`
        );
    }
  }

  toExtensionContext() {
    // NOTE: implemented in subclasses that should be listed in runtime.getContexts results
    // when they match the ContextFilter, whereas instances from subclasses that don't
    // implement it will always be filtered out.
    return undefined;
  }

  trackRunListenerPromise(runListenerPromise) {
    if (
      // The extension was already shutdown.
      !this.extension ||
      // Not a non persistent background script context.
      !this.isBackgroundContext ||
      this.extension.persistentBackground
    ) {
      return;
    }
    const clearFromSet = () =>
      this.runListenerPromises.delete(runListenerPromise);
    runListenerPromise.then(clearFromSet, clearFromSet);
    this.runListenerPromises.add(runListenerPromise);
  }

  clearPendingRunListenerPromises() {
    this.runListenerPromises.clear();
  }

  get pendingRunListenerPromisesCount() {
    return this.runListenerPromises.size;
  }

  trackNativeAppPort(port) {
    if (
      // Not a native port.
      !port?.native ||
      // Not a non persistent background script context.
      !this.isBackgroundContext ||
      this.extension?.persistentBackground ||
      // The extension was already shutdown.
      !this.extension
    ) {
      return;
    }
    this.activeNativePorts.add(port);
  }

  untrackNativeAppPort(port) {
    this.activeNativePorts.delete(port);
  }

  get hasActiveNativeAppPorts() {
    return !!ChromeUtils.nondeterministicGetWeakSetKeys(this.activeNativePorts)
      .length;
  }

  /**
   * Call the `callable` parameter with `context.callContextData` set to the value passed
   * as the first parameter of this method.
   *
   * `context.callContextData` is expected to:
   * - don't be set when context.withCallContextData is being called
   * - be set back to null right after calling the `callable` function, without
   *   awaiting on any async code that the function may be running internally
   *
   * The callable method itself is responsabile of eventually retrieve the value initially set
   * on the `context.callContextData` before any code executed asynchronously (e.g. from a
   * callback or after awaiting internally on a promise if the `callable` function was async).
   *
   * @param {object} callContextData
   * @param {boolean} callContextData.isHandlingUserInput
   * @param {Function} callable
   *
   * @returns {any} Returns the value returned by calling the `callable` method.
   */
  withCallContextData({ isHandlingUserInput }, callable) {
    if (this.callContextData) {
      Cu.reportError(
        `Unexpected pre-existing callContextData on "${this.extension?.policy.debugName}" contextId ${this.contextId}`
      );
    }

    try {
      this.callContextData = {
        isHandlingUserInput,
      };
      return callable();
    } finally {
      this.callContextData = null;
    }
  }

  async withPendingBrowser(browser, callable) {
    let savedBrowser = this.pendingEventBrowser;
    this.pendingEventBrowser = browser;
    try {
      let result = await callable();
      return result;
    } finally {
      this.pendingEventBrowser = savedBrowser;
    }
  }

  logActivity() {
    // The base class will throw so we catch any subclasses that do not implement.
    // We do not want to throw here, but we also do not log here.
  }

  get cloneScope() {
    return this.sandbox;
  }

  applySafe(callback, args) {
    // There's no need to clone when calling listeners for a proxied
    // context.
    return this.applySafeWithoutClone(callback, args);
  }

  get xulBrowser() {
    return this.messageManagerProxy?.eventTarget;
  }

  get parentMessageManager() {
    // TODO bug 1595186: Replace use of parentMessageManager.
    return this.messageManagerProxy?.messageManager;
  }

  shutdown() {
    this.unload();
  }

  unload() {
    if (this.unloaded) {
      return;
    }

    this.messageManagerProxy?.dispose();

    super.unload();
    apiManager.emit("proxy-context-unload", this);
  }

  get apiCan() {
    const apiCan = new CanOfAPIs(this, this.extension.apiManager, {});
    return redefineGetter(this, "apiCan", apiCan);
  }

  get apiObj() {
    return redefineGetter(this, "apiObj", this.apiCan.root);
  }

  get sandbox() {
    // Note: Blob and URL globals are used in ext-contentScripts.js.
    const sandbox = Cu.Sandbox(this.principal, {
      sandboxName: this.uri.spec,
      wantGlobalProperties: ["Blob", "URL"],
    });
    return redefineGetter(this, "sandbox", sandbox);
  }
}

/**
 * The parent side of proxied API context for extension content script
 * running in ExtensionContent.sys.mjs.
 */
class ContentScriptContextParent extends ProxyContextParent {}

/**
 * The parent side of proxied API context for extension page, such as a
 * background script, a tab page, or a popup, running in
 * ExtensionChild.sys.mjs.
 */
class ExtensionPageContextParent extends ProxyContextParent {
  constructor(envType, extension, params, browsingContext) {
    super(envType, extension, params, browsingContext, extension.principal);

    this.viewType = params.viewType;
    this.isTopContext = browsingContext.top === browsingContext;

    this.extension.views.add(this);

    extension.emit("extension-proxy-context-load", this);
  }

  // The window that contains this context. This may change due to moving tabs.
  get appWindow() {
    let win = this.xulBrowser.ownerGlobal;
    return win.browsingContext.topChromeWindow;
  }

  get currentWindow() {
    if (this.viewType !== "background") {
      return this.appWindow;
    }
    return undefined;
  }

  get tabId() {
    let { tabTracker } = apiManager.global;
    let data = tabTracker.getBrowserData(this.xulBrowser);
    if (data.tabId >= 0) {
      return data.tabId;
    }
    return undefined;
  }

  toExtensionContext() {
    const { tabTracker } = apiManager.global;
    const { tabId, windowId } = tabTracker.getBrowserDataForContext(this);
    const windowContext = this.browsingContext?.currentWindowContext;
    return {
      // NOTE: the contextId property in the final set of properties returned to
      // extensions code is filled in on the ext-runtime.js and it is not to be
      // confused with the internal property called contextId.
      contextId: undefined,
      // NOTE: contextType is a getter that maps the viewType property used
      // internally with the value expected for the runtime.ExtensionContext
      // contextType property (which should be one of the values part of the
      // runtime.ContextType enum).
      contextType: this.contextType,
      // TODO(Bug 1891478): add documentId.
      // TODO(Bug 1890739): consider switching this to use webExposedOriginSerialization when available
      // Using nsIPrincipal.originNoSuffix to avoid including the
      // private browsing (or contextual identity ones)
      documentOrigin: windowContext?.documentPrincipal.originNoSuffix,
      documentUrl: windowContext?.documentURI.spec,
      incognito: this.incognito,
      frameId: this.frameId,
      tabId,
      windowId,
      // TODO: File followup to also add a Firefox-only userContextId?
    };
  }

  unload() {
    super.unload();
    this.extension.views.delete(this);
  }

  shutdown() {
    apiManager.emit("page-shutdown", this);
    super.shutdown();
  }
}

/**
 * The parent side of proxied API context for devtools extension page, such as a
 * devtools pages and panels running in ExtensionChild.sys.mjs.
 */
class DevToolsExtensionPageContextParent extends ExtensionPageContextParent {
  constructor(...params) {
    super(...params);

    // Set all attributes that are lazily defined to `null` here.
    //
    // Note that we can't do that for `this._devToolsToolbox` because it will
    // be defined when calling our parent constructor and so would override it back to `null`.
    this._devToolsCommands = null;
    this._onNavigatedListeners = null;

    this._onResourceAvailable = this._onResourceAvailable.bind(this);
  }

  toExtensionContext() {
    // NOTE: devtools extension contexts are currently omitted in getContexts
    // results.
    return undefined;
  }

  set devToolsToolbox(toolbox) {
    if (this._devToolsToolbox) {
      throw new Error("Cannot set the context DevTools toolbox twice");
    }

    this._devToolsToolbox = toolbox;
  }

  get devToolsToolbox() {
    return this._devToolsToolbox;
  }

  async addOnNavigatedListener(listener) {
    if (!this._onNavigatedListeners) {
      this._onNavigatedListeners = new Set();

      await this.devToolsToolbox.resourceCommand.watchResources(
        [this.devToolsToolbox.resourceCommand.TYPES.DOCUMENT_EVENT],
        {
          onAvailable: this._onResourceAvailable,
          ignoreExistingResources: true,
        }
      );
    }

    this._onNavigatedListeners.add(listener);
  }

  removeOnNavigatedListener(listener) {
    if (this._onNavigatedListeners) {
      this._onNavigatedListeners.delete(listener);
    }
  }

  /**
   * The returned "commands" object, exposing modules implemented from devtools/shared/commands.
   * Each attribute being a static interface to communicate with the server backend.
   *
   * @returns {Promise<object>}
   */
  async getDevToolsCommands() {
    // Ensure that we try to instantiate a commands only once,
    // even if createCommandsForTabForWebExtension is async.
    if (this._devToolsCommandsPromise) {
      return this._devToolsCommandsPromise;
    }
    if (this._devToolsCommands) {
      return this._devToolsCommands;
    }

    this._devToolsCommandsPromise = (async () => {
      const commands =
        await lazy.DevToolsShim.createCommandsForTabForWebExtension(
          this.devToolsToolbox.commands.descriptorFront.localTab
        );
      await commands.targetCommand.startListening();
      this._devToolsCommands = commands;
      this._devToolsCommandsPromise = null;
      return commands;
    })();
    return this._devToolsCommandsPromise;
  }

  unload() {
    // Bail if the toolbox reference was already cleared.
    if (!this.devToolsToolbox) {
      return;
    }

    if (this._onNavigatedListeners) {
      this.devToolsToolbox.resourceCommand.unwatchResources(
        [this.devToolsToolbox.resourceCommand.TYPES.DOCUMENT_EVENT],
        { onAvailable: this._onResourceAvailable }
      );
    }

    if (this._devToolsCommands) {
      this._devToolsCommands.destroy();
      this._devToolsCommands = null;
    }

    if (this._onNavigatedListeners) {
      this._onNavigatedListeners.clear();
      this._onNavigatedListeners = null;
    }

    this._devToolsToolbox = null;

    super.unload();
  }

  async _onResourceAvailable(resources) {
    for (const resource of resources) {
      const { targetFront } = resource;
      if (targetFront.isTopLevel && resource.name === "dom-complete") {
        for (const listener of this._onNavigatedListeners) {
          listener(targetFront.url);
        }
      }
    }
  }
}

/**
 * The parent side of proxied API context for extension background service
 * worker script.
 */
class BackgroundWorkerContextParent extends ProxyContextParent {
  constructor(envType, extension, params) {
    // TODO: split out from ProxyContextParent a base class that
    // doesn't expect a browsingContext and one for contexts that are
    // expected to have a browsingContext associated.
    super(envType, extension, params, null, extension.principal);

    this.viewType = params.viewType;
    this.workerDescriptorId = params.workerDescriptorId;

    this.extension.views.add(this);

    extension.emit("extension-proxy-context-load", this);
  }
}

ParentAPIManager = {
  proxyContexts: new Map(),

  init() {
    // TODO: Bug 1595186 - remove/replace all usage of MessageManager below.
    Services.obs.addObserver(this, "message-manager-close");

    this.conduit = new lazy.BroadcastConduit(this, {
      id: "ParentAPIManager",
      reportOnClosed: "childId",
      recv: [
        "CreateProxyContext",
        "ContextLoaded",
        "APICall",
        "AddListener",
        "RemoveListener",
      ],
      send: ["CallResult"],
      query: ["RunListener", "StreamFilterSuspendCancel"],
    });
  },

  attachMessageManager(extension, processMessageManager) {
    extension.parentMessageManager = processMessageManager;
  },

  async observe(subject, topic) {
    if (topic === "message-manager-close") {
      let mm = subject;
      for (let [childId, context] of this.proxyContexts) {
        if (context.parentMessageManager === mm) {
          this.closeProxyContext(childId);
        }
      }

      // Reset extension message managers when their child processes shut down.
      for (let extension of GlobalManager.extensionMap.values()) {
        if (extension.parentMessageManager === mm) {
          extension.parentMessageManager = null;
        }
      }
    }
  },

  shutdownExtension(extensionId, reason) {
    if (["ADDON_DISABLE", "ADDON_UNINSTALL"].includes(reason)) {
      apiManager._callHandlers([extensionId], "disable", "onDisable");
    }

    for (let [childId, context] of this.proxyContexts) {
      if (context.extension.id == extensionId) {
        context.shutdown();
        this.proxyContexts.delete(childId);
      }
    }
  },

  queryStreamFilterSuspendCancel(childId) {
    return this.conduit.queryStreamFilterSuspendCancel(childId);
  },

  recvCreateProxyContext(data, { actor, sender }) {
    let { envType, extensionId, childId, principal } = data;

    if (this.proxyContexts.has(childId)) {
      throw new Error(
        "A WebExtension context with the given ID already exists!"
      );
    }

    let extension = GlobalManager.getExtension(extensionId);
    if (!extension) {
      throw new Error(`No WebExtension found with ID ${extensionId}`);
    }

    let context;
    if (envType == "addon_parent" || envType == "devtools_parent") {
      if (!sender.verified) {
        throw new Error(`Bad sender context envType: ${sender.envType}`);
      }

      if (JSWindowActorParent.isInstance(actor)) {
        const target = actor.browsingContext.top.embedderElement;
        let processMessageManager =
          target.messageManager.processMessageManager ||
          Services.ppmm.getChildAt(0);

        if (!extension.parentMessageManager) {
          if (target.remoteType === extension.remoteType) {
            this.attachMessageManager(extension, processMessageManager);
          }
        }

        if (processMessageManager !== extension.parentMessageManager) {
          throw new Error(
            "Attempt to create privileged extension parent from incorrect child process"
          );
        }

        if (envType == "addon_parent") {
          context = new ExtensionPageContextParent(
            envType,
            extension,
            data,
            actor.browsingContext
          );
        } else if (envType == "devtools_parent") {
          context = new DevToolsExtensionPageContextParent(
            envType,
            extension,
            data,
            actor.browsingContext
          );
        }
      } else if (JSProcessActorParent.isInstance(actor)) {
        if (actor.manager.remoteType !== extension.remoteType) {
          throw new Error(
            "Attempt to create privileged extension parent from incorrect child process"
          );
        }

        if (envType !== "addon_parent") {
          throw new Error(
            `Unexpected envType ${envType} on an extension process actor`
          );
        }
        if (data.viewType !== "background_worker") {
          throw new Error(
            `Unexpected viewType ${data.viewType} on an extension process actor`
          );
        }
        context = new BackgroundWorkerContextParent(envType, extension, data);
      } else {
        // Unreacheable: JSWindowActorParent and JSProcessActorParent are the
        // only actors.
        throw new Error(
          "Attempt to create privileged extension parent via incorrect actor"
        );
      }
    } else if (envType == "content_parent") {
      // Note: actor is always a JSWindowActorParent, with a browsingContext.
      context = new ContentScriptContextParent(
        envType,
        extension,
        data,
        actor.browsingContext,
        principal
      );
    } else {
      throw new Error(`Invalid WebExtension context envType: ${envType}`);
    }
    this.proxyContexts.set(childId, context);
  },

  recvContextLoaded(data, { actor }) {
    let context = this.getContextById(data.childId);
    verifyActorForContext(actor, context);
    const { extension } = context;
    extension.emit("extension-proxy-context-load:completed", context);
  },

  recvConduitClosed(sender) {
    this.closeProxyContext(sender.id);
  },

  closeProxyContext(childId) {
    let context = this.proxyContexts.get(childId);
    if (context) {
      context.unload();
      this.proxyContexts.delete(childId);
    }
  },

  /**
   * Call the given function and also log the call as appropriate
   * (i.e., with activity logging and/or profiler markers)
   *
   * @param {BaseContext} context The context making this call.
   * @param {object} data Additional data about the call.
   * @param {Function} callable The actual implementation to invoke.
   */
  async callAndLog(context, data, callable) {
    let { id } = context.extension;
    // If we were called via callParentAsyncFunction we don't want
    // to log again, check for the flag.
    const { alreadyLogged } = data.options || {};
    if (!alreadyLogged) {
      lazy.ExtensionActivityLog.log(
        id,
        context.viewType,
        "api_call",
        data.path,
        {
          args: data.args,
        }
      );
    }

    let start = Cu.now();
    try {
      return callable();
    } finally {
      ChromeUtils.addProfilerMarker(
        "ExtensionParent",
        { startTime: start },
        `${id}, api_call: ${data.path}`
      );
    }
  },

  async recvAPICall(data, { actor }) {
    let context = this.getContextById(data.childId);
    let target = actor.browsingContext?.top.embedderElement;

    verifyActorForContext(actor, context);

    let reply = result => {
      if (target && !context.parentMessageManager) {
        Services.console.logStringMessage(
          "Cannot send function call result: other side closed connection " +
            `(call data: ${uneval({ path: data.path, args: data.args })})`
        );
        return;
      }

      this.conduit.sendCallResult(data.childId, {
        childId: data.childId,
        callId: data.callId,
        path: data.path,
        ...result,
      });
    };

    try {
      if (
        context.isBackgroundContext &&
        !context.extension.persistentBackground
      ) {
        context.extension.emit("background-script-reset-idle", {
          reason: "parentapicall",
          path: data.path,
        });
      }

      let args = data.args;
      let { isHandlingUserInput = false } = data.options || {};
      let pendingBrowser = context.pendingEventBrowser;
      let fun = await context.apiCan.asyncFindAPIPath(data.path);
      let result = this.callAndLog(context, data, () => {
        return context.withPendingBrowser(pendingBrowser, () =>
          context.withCallContextData({ isHandlingUserInput }, () =>
            fun(...args)
          )
        );
      });

      if (data.callId) {
        result = result || Promise.resolve();

        result.then(
          result => {
            result = result instanceof SpreadArgs ? [...result] : [result];

            let holder = new StructuredCloneHolder(
              `ExtensionParent/${context.extension.id}/recvAPICall/${data.path}`,
              null,
              result
            );

            reply({ result: holder });
          },
          error => {
            error = context.normalizeError(error);
            reply({
              error: { message: error.message, fileName: error.fileName },
            });
          }
        );
      }
    } catch (e) {
      if (data.callId) {
        let error = context.normalizeError(e);
        reply({ error: { message: error.message } });
      } else {
        Cu.reportError(e);
      }
    }
  },

  async recvAddListener(data, { actor }) {
    let context = this.getContextById(data.childId);

    verifyActorForContext(actor, context);

    let { childId, alreadyLogged = false } = data;
    let handlingUserInput = false;

    let listener = async (...listenerArgs) => {
      let startTime = Cu.now();
      // Extract urgentSend flag to avoid deserializing args holder later.
      let urgentSend = false;
      if (listenerArgs[0] && data.path.startsWith("webRequest.")) {
        urgentSend = listenerArgs[0].urgentSend;
        delete listenerArgs[0].urgentSend;
      }
      let runListenerPromise = this.conduit.queryRunListener(childId, {
        childId,
        handlingUserInput,
        listenerId: data.listenerId,
        path: data.path,
        urgentSend,
        get args() {
          return new StructuredCloneHolder(
            `ExtensionParent/${context.extension.id}/recvAddListener/${data.path}`,
            null,
            listenerArgs
          );
        },
      });
      context.trackRunListenerPromise(runListenerPromise);

      const result = await runListenerPromise;
      let rv = result && result.deserialize(globalThis);
      ChromeUtils.addProfilerMarker(
        "ExtensionParent",
        { startTime },
        `${context.extension.id}, api_event: ${data.path}`
      );
      lazy.ExtensionActivityLog.log(
        context.extension.id,
        context.viewType,
        "api_event",
        data.path,
        { args: listenerArgs, result: rv }
      );
      return rv;
    };

    context.listenerProxies.set(data.listenerId, listener);

    let args = data.args;
    let promise = context.apiCan.asyncFindAPIPath(data.path);

    // Store pending listener additions so we can be sure they're all
    // fully initialize before we consider extension startup complete.
    if (context.isBackgroundContext && context.listenerPromises) {
      const { listenerPromises } = context;
      listenerPromises.add(promise);
      let remove = () => {
        listenerPromises.delete(promise);
      };
      promise.then(remove, remove);
    }

    let handler = await promise;
    if (handler.setUserInput) {
      handlingUserInput = true;
    }
    handler.addListener(listener, ...args);
    if (!alreadyLogged) {
      lazy.ExtensionActivityLog.log(
        context.extension.id,
        context.viewType,
        "api_call",
        `${data.path}.addListener`,
        { args }
      );
    }
  },

  async recvRemoveListener(data) {
    let context = this.getContextById(data.childId);
    let listener = context.listenerProxies.get(data.listenerId);

    let handler = await context.apiCan.asyncFindAPIPath(data.path);
    handler.removeListener(listener);

    let { alreadyLogged = false } = data;
    if (!alreadyLogged) {
      lazy.ExtensionActivityLog.log(
        context.extension.id,
        context.viewType,
        "api_call",
        `${data.path}.removeListener`,
        { args: [] }
      );
    }
  },

  getContextById(childId) {
    let context = this.proxyContexts.get(childId);
    if (!context) {
      throw new Error("WebExtension context not found!");
    }
    return context;
  },
};

ParentAPIManager.init();

/**
 * A hidden window which contains the extension pages that are not visible
 * (i.e., background pages and devtools pages), and is also used by
 * ExtensionDebuggingUtils to contain the browser elements used by the
 * addon debugger to connect to the devtools actors running in the same
 * process of the target extension (and be able to stay connected across
 *  the addon reloads).
 */
class HiddenXULWindow {
  constructor() {
    this._windowlessBrowser = null;
    this.unloaded = false;
    this.waitInitialized = this.initWindowlessBrowser();
  }

  shutdown() {
    if (this.unloaded) {
      throw new Error(
        "Unable to shutdown an unloaded HiddenXULWindow instance"
      );
    }

    this.unloaded = true;

    this.waitInitialized = null;

    if (!this._windowlessBrowser) {
      Cu.reportError("HiddenXULWindow was shut down while it was loading.");
      // initWindowlessBrowser will close windowlessBrowser when possible.
      return;
    }

    this._windowlessBrowser.close();
    this._windowlessBrowser = null;
  }

  get chromeDocument() {
    return this._windowlessBrowser.document;
  }

  /**
   * Private helper that create a HTMLDocument in a windowless browser.
   *
   * @returns {Promise<void>}
   *          A promise which resolves when the windowless browser is ready.
   */
  async initWindowlessBrowser() {
    if (this.waitInitialized) {
      throw new Error("HiddenXULWindow already initialized");
    }

    // The invisible page is currently wrapped in a XUL window to fix an issue
    // with using the canvas API from a background page (See Bug 1274775).
    let windowlessBrowser = Services.appShell.createWindowlessBrowser(true);

    // The windowless browser is a thin wrapper around a docShell that keeps
    // its related resources alive. It implements nsIWebNavigation and
    // forwards its methods to the underlying docShell.
    let chromeShell = windowlessBrowser.docShell;
    let webNav = chromeShell.QueryInterface(Ci.nsIWebNavigation);

    if (lazy.PrivateBrowsingUtils.permanentPrivateBrowsing) {
      let attrs = chromeShell.getOriginAttributes();
      attrs.privateBrowsingId = 1;
      chromeShell.setOriginAttributes(attrs);
    }

    windowlessBrowser.browsingContext.useGlobalHistory = false;
    webNav.loadURI(DUMMY_PAGE_URI, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });

    await promiseObserved(
      "chrome-document-global-created",
      win => win.document == webNav.document
    );
    await promiseDocumentLoaded(windowlessBrowser.document);
    if (this.unloaded) {
      windowlessBrowser.close();
      return;
    }
    this._windowlessBrowser = windowlessBrowser;
  }

  /**
   * Creates the browser XUL element that will contain the WebExtension Page.
   *
   * @param {object} xulAttributes
   *        An object that contains the xul attributes to set of the newly
   *        created browser XUL element.
   *
   * @returns {Promise<XULBrowserElement>}
   *          A Promise which resolves to the newly created browser XUL element.
   */
  async createBrowserElement(xulAttributes) {
    if (!xulAttributes || Object.keys(xulAttributes).length === 0) {
      throw new Error("missing mandatory xulAttributes parameter");
    }

    await this.waitInitialized;

    const chromeDoc = this.chromeDocument;

    const browser = chromeDoc.createXULElement("browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("messagemanagergroup", "webext-browsers");
    browser.setAttribute("manualactiveness", "true");

    for (const [name, value] of Object.entries(xulAttributes)) {
      if (value != null) {
        browser.setAttribute(name, value);
      }
    }

    let awaitFrameLoader;

    if (browser.getAttribute("remote") === "true") {
      awaitFrameLoader = promiseEvent(browser, "XULFrameLoaderCreated");
    }

    chromeDoc.documentElement.appendChild(browser);

    // Forcibly flush layout so that we get a pres shell soon enough, see
    // bug 1274775.
    browser.getBoundingClientRect();
    await awaitFrameLoader;

    // FIXME(emilio): This unconditionally active frame seems rather
    // unfortunate, but matches previous behavior.
    browser.docShellIsActive = true;

    return browser;
  }
}

const SharedWindow = {
  _window: null,
  _count: 0,

  acquire() {
    if (this._window == null) {
      if (this._count != 0) {
        throw new Error(
          `Shared window already exists with count ${this._count}`
        );
      }

      this._window = new HiddenXULWindow();
    }

    this._count++;
    return this._window;
  },

  release() {
    if (this._count < 1) {
      throw new Error(`Releasing shared window with count ${this._count}`);
    }

    this._count--;
    if (this._count == 0) {
      this._window.shutdown();
      this._window = null;
    }
  },
};

/**
 * This is a base class used by the ext-backgroundPage and ext-devtools API implementations
 * to inherits the shared boilerplate code needed to create a parent document for the hidden
 * extension pages (e.g. the background page, the devtools page) in the BackgroundPage and
 * DevToolsPage classes.
 */
class HiddenExtensionPage {
  /**
   * @param {Extension} extension
   *        The Extension which owns the hidden extension page created (used to decide
   *        if the hidden extension page parent doc is going to be a windowlessBrowser or
   *        a visible XUL window).
   * @param {string} viewType
   *        The viewType of the WebExtension page that is going to be loaded
   *        in the created browser element (e.g. "background" or "devtools_page").
   */
  constructor(extension, viewType) {
    if (!extension || !viewType) {
      throw new Error("extension and viewType parameters are mandatory");
    }

    this.extension = extension;
    this.viewType = viewType;
    this.browser = null;
    this.unloaded = false;
  }

  /**
   * Destroy the created parent document.
   */
  shutdown() {
    if (this.unloaded) {
      throw new Error(
        "Unable to shutdown an unloaded HiddenExtensionPage instance"
      );
    }

    this.unloaded = true;

    if (this.browser) {
      this._releaseBrowser();
    }
  }

  _releaseBrowser() {
    this.browser.remove();
    this.browser = null;
    SharedWindow.release();
  }

  /**
   * Creates the browser XUL element that will contain the WebExtension Page.
   *
   * @returns {Promise<XULElement>}
   *          A Promise which resolves to the newly created browser XUL element.
   */
  async createBrowserElement() {
    if (this.browser) {
      throw new Error("createBrowserElement called twice");
    }

    let window = SharedWindow.acquire();
    try {
      this.browser = await window.createBrowserElement({
        "webextension-view-type": this.viewType,
        remote: this.extension.remote ? "true" : null,
        remoteType: this.extension.remoteType,
        initialBrowsingContextGroupId: this.extension.browsingContextGroupId,
      });
    } catch (e) {
      SharedWindow.release();
      throw e;
    }

    if (this.unloaded) {
      this._releaseBrowser();
      throw new Error("Extension shut down before browser element was created");
    }

    return this.browser;
  }
}

/** @typedef {import("resource://devtools/server/actors/descriptors/webextension.js")
              .WebExtensionDescriptorActor} WebExtensionDescriptorActor */

/**
 * This object provides utility functions needed by the devtools actors to
 * be able to connect and debug an extension (which can run in the main or in
 * a child extension process).
 */
const DebugUtils = {
  // A lazily created hidden XUL window, which contains the browser elements
  // which are used to connect the webextension patent actor to the extension process.
  hiddenXULWindow: null,

  /** @type {Map<string, Promise<XULBrowserElement> & { browser: XULBrowserElement }>} */
  debugBrowserPromises: new Map(),
  /** @type {WeakMap<Promise<XULBrowserElement>, Set<WebExtensionDescriptorActor>>} */
  debugActors: new DefaultWeakMap(() => new Set()),

  _extensionUpdatedWatcher: null,
  watchExtensionUpdated() {
    if (!this._extensionUpdatedWatcher) {
      // Watch the updated extension objects.
      this._extensionUpdatedWatcher = async (evt, extension) => {
        const browserPromise = this.debugBrowserPromises.get(extension.id);
        if (browserPromise) {
          const browser = await browserPromise;
          if (
            browser.isRemoteBrowser !== extension.remote &&
            this.debugBrowserPromises.get(extension.id) === browserPromise
          ) {
            // If the cached browser element is not anymore of the same
            // remote type of the extension, remove it.
            this.debugBrowserPromises.delete(extension.id);
            browser.remove();
          }
        }
      };

      apiManager.on("ready", this._extensionUpdatedWatcher);
    }
  },

  unwatchExtensionUpdated() {
    if (this._extensionUpdatedWatcher) {
      apiManager.off("ready", this._extensionUpdatedWatcher);
      delete this._extensionUpdatedWatcher;
    }
  },

  getExtensionManifestWarnings(id) {
    const addon = GlobalManager.extensionMap.get(id);
    if (addon) {
      return addon.warnings;
    }
    return [];
  },

  /**
   * Determine if the extension does have a non-persistent background script
   * (either an event page or a background service worker):
   *
   * Based on this the DevTools client will determine if this extension should provide
   * to the extension developers a button to forcefully terminate the background
   * script.
   *
   * @param {string} addonId
   *   The id of the addon
   *
   * @returns {void|boolean}
   *   - undefined => does not apply (no background script in the manifest)
   *   - true => the background script is persistent.
   *   - false => the background script is an event page or a service worker.
   */
  hasPersistentBackgroundScript(addonId) {
    const policy = WebExtensionPolicy.getByID(addonId);

    // The addon doesn't have any background script or we
    // can't be sure yet.
    if (
      policy?.extension?.type !== "extension" ||
      !policy?.extension?.manifest?.background
    ) {
      return undefined;
    }

    return policy.extension.persistentBackground;
  },

  /**
   * Determine if the extension background page is running.
   *
   * Based on this the DevTools client will show the status of the background
   * script in about:debugging.
   *
   * @param {string} addonId
   *   The id of the addon
   *
   * @returns {void|boolean}
   *   - undefined => does not apply (no background script in the manifest)
   *   - true => the background script is running.
   *   - false => the background script is stopped.
   */
  isBackgroundScriptRunning(addonId) {
    const policy = WebExtensionPolicy.getByID(addonId);

    // The addon doesn't have any background script or we
    // can't be sure yet.
    if (!(this.hasPersistentBackgroundScript(addonId) === false)) {
      return undefined;
    }

    const views = policy?.extension?.views || [];
    for (const view of views) {
      if (
        view.viewType === "background" ||
        (view.viewType === "background_worker" && !view.unloaded)
      ) {
        return true;
      }
    }

    return false;
  },

  async terminateBackgroundScript(addonId) {
    // Terminate the background if the extension does have
    // a non-persistent background script (event page or background
    // service worker).
    if (this.hasPersistentBackgroundScript(addonId) === false) {
      const policy = WebExtensionPolicy.getByID(addonId);
      // When the event page is being terminated through the Devtools
      // action, we should terminate it even if there are DevTools
      // toolboxes attached to the extension.
      return policy.extension.terminateBackground({
        ignoreDevToolsAttached: true,
      });
    }
    throw Error(`Unable to terminate background script for ${addonId}`);
  },

  /**
   * Determine whether a devtools toolbox attached to the extension.
   *
   * This method is called by the background page idle timeout handler,
   * to inhibit terminating the event page when idle while the extension
   * developer is debugging the extension through the Addon Debugging window
   * (similarly to how service workers are kept alive while the devtools are
   * attached).
   *
   * @param {string} id
   *        The id of the extension.
   *
   * @returns {boolean}
   *          true when a devtools toolbox is attached to an extension with
   *          the given id, false otherwise.
   */
  hasDevToolsAttached(id) {
    return this.debugBrowserPromises.has(id);
  },

  /**
   * Retrieve a XUL browser element which has been configured to be able to connect
   * the devtools actor with the process where the extension is running.
   *
   * @param {WebExtensionDescriptorActor} webExtensionParentActor
   *        The devtools actor that is retrieving the browser element.
   *
   * @returns {Promise<XULBrowserElement>}
   *          A promise which resolves to the configured browser XUL element.
   */
  async getExtensionProcessBrowser(webExtensionParentActor) {
    const extensionId = webExtensionParentActor.addonId;
    const extension = GlobalManager.getExtension(extensionId);
    if (!extension) {
      throw new Error(`Extension not found: ${extensionId}`);
    }

    const createBrowser = () => {
      if (!this.hiddenXULWindow) {
        this.hiddenXULWindow = new HiddenXULWindow();
        this.watchExtensionUpdated();
      }

      return this.hiddenXULWindow.createBrowserElement({
        "webextension-addon-debug-target": extensionId,
        remote: extension.remote ? "true" : null,
        remoteType: extension.remoteType,
        initialBrowsingContextGroupId: extension.browsingContextGroupId,
      });
    };

    let browserPromise = this.debugBrowserPromises.get(extensionId);

    // Create a new promise if there is no cached one in the map.
    if (!browserPromise) {
      browserPromise = createBrowser();
      this.debugBrowserPromises.set(extensionId, browserPromise);
      browserPromise.then(browser => {
        browserPromise.browser = browser;
      });
      browserPromise.catch(e => {
        Cu.reportError(e);
        this.debugBrowserPromises.delete(extensionId);
      });
    }

    this.debugActors.get(browserPromise).add(webExtensionParentActor);

    return browserPromise;
  },

  getFrameLoader(extensionId) {
    let promise = this.debugBrowserPromises.get(extensionId);
    return promise && promise.browser && promise.browser.frameLoader;
  },

  /**
   * Given the devtools actor that has retrieved an addon debug browser element,
   * it destroys the XUL browser element, and it also destroy the hidden XUL window
   * if it is not currently needed.
   *
   * @param {WebExtensionDescriptorActor} webExtensionParentActor
   *        The devtools actor that has retrieved an addon debug browser element.
   */
  async releaseExtensionProcessBrowser(webExtensionParentActor) {
    const extensionId = webExtensionParentActor.addonId;
    const browserPromise = this.debugBrowserPromises.get(extensionId);

    if (browserPromise) {
      const actorsSet = this.debugActors.get(browserPromise);
      actorsSet.delete(webExtensionParentActor);
      if (actorsSet.size === 0) {
        this.debugActors.delete(browserPromise);
        this.debugBrowserPromises.delete(extensionId);
        await browserPromise.then(browser => browser.remove());
      }
    }

    if (this.debugBrowserPromises.size === 0 && this.hiddenXULWindow) {
      this.hiddenXULWindow.shutdown();
      this.hiddenXULWindow = null;
      this.unwatchExtensionUpdated();
    }
  },
};

/**
 * Returns a Promise which resolves with the message data when the given message
 * was received by the message manager. The promise is rejected if the message
 * manager was closed before a message was received.
 *
 * @param {MessageListenerManager} messageManager
 *        The message manager on which to listen for messages.
 * @param {string} messageName
 *        The message to listen for.
 * @returns {Promise<*>}
 */
function promiseMessageFromChild(messageManager, messageName) {
  return new Promise((resolve, reject) => {
    let unregister;
    function listener(message) {
      unregister();
      resolve(message.data);
    }
    function observer(subject) {
      if (subject === messageManager) {
        unregister();
        reject(
          new Error(
            `Message manager was disconnected before receiving ${messageName}`
          )
        );
      }
    }
    unregister = () => {
      Services.obs.removeObserver(observer, "message-manager-close");
      messageManager.removeMessageListener(messageName, listener);
    };
    messageManager.addMessageListener(messageName, listener);
    Services.obs.addObserver(observer, "message-manager-close");
  });
}

// This should be called before browser.loadURI is invoked.
async function promiseBackgroundViewLoaded(browser) {
  let { childId } = await promiseMessageFromChild(
    browser.messageManager,
    "Extension:BackgroundViewLoaded"
  );
  if (childId) {
    return ParentAPIManager.getContextById(childId);
  }
}

/**
 * This helper is used to subscribe a listener (e.g. in the ext-devtools API implementation)
 * to be called for every ExtensionProxyContext created for an extension page given
 * its related extension, viewType and browser element (both the top level context and any context
 * created for the extension urls running into its iframe descendants).
 *
 * @param {object} params
 * @param {object} params.extension
 *        The Extension on which we are going to listen for the newly created ExtensionProxyContext.
 * @param {string} params.viewType
 *        The viewType of the WebExtension page that we are watching (e.g. "background" or
 *        "devtools_page").
 * @param {XULElement} params.browser
 *        The browser element of the WebExtension page that we are watching.
 * @param {Function} onExtensionProxyContextLoaded
 *        The callback that is called when a new context has been loaded (as `callback(context)`);
 *
 * @returns {Function}
 *          Unsubscribe the listener.
 */
function watchExtensionProxyContextLoad(
  { extension, viewType, browser },
  onExtensionProxyContextLoaded
) {
  if (typeof onExtensionProxyContextLoaded !== "function") {
    throw new Error("Missing onExtensionProxyContextLoaded handler");
  }

  const listener = (event, context) => {
    if (context.viewType == viewType && context.xulBrowser == browser) {
      onExtensionProxyContextLoaded(context);
    }
  };

  extension.on("extension-proxy-context-load", listener);

  return () => {
    extension.off("extension-proxy-context-load", listener);
  };
}

/**
 * This helper is used to subscribe a listener (e.g. in the ext-backgroundPage)
 * to be called for every ExtensionProxyContext created for an extension
 * background service worker given its related extension.
 *
 * @param {object} params
 * @param {object} params.extension
 *        The Extension on which we are going to listen for the newly created ExtensionProxyContext.
 * @param {Function} onExtensionWorkerContextLoaded
 *        The callback that is called when the worker script has been fully loaded (as `callback(context)`);
 *
 * @returns {Function}
 *          Unsubscribe the listener.
 */
function watchExtensionWorkerContextLoaded(
  { extension },
  onExtensionWorkerContextLoaded
) {
  if (typeof onExtensionWorkerContextLoaded !== "function") {
    throw new Error("Missing onExtensionWorkerContextLoaded handler");
  }

  const listener = (event, context) => {
    if (context.viewType == "background_worker") {
      onExtensionWorkerContextLoaded(context);
    }
  };

  extension.on("extension-proxy-context-load:completed", listener);

  return () => {
    extension.off("extension-proxy-context-load:completed", listener);
  };
}

// Manages icon details for toolbar buttons in the |pageAction| and
// |browserAction| APIs.
let IconDetails = {
  DEFAULT_ICON: "chrome://mozapps/skin/extensions/extensionGeneric.svg",

  // WeakMap<Extension -> Map<url-string -> Map<iconType-string -> object>>>
  iconCache: new DefaultWeakMap(() => {
    return new DefaultMap(() => new DefaultMap(() => new Map()));
  }),

  // Normalizes the various acceptable input formats into an object
  // with icon size as key and icon URL as value.
  //
  // If a context is specified (function is called from an extension):
  // Throws an error if an invalid icon size was provided or the
  // extension is not allowed to load the specified resources.
  //
  // If no context is specified, instead of throwing an error, this
  // function simply logs a warning message.
  normalize(details, extension, context = null) {
    if (!details.imageData && details.path != null) {
      // Pick a cache key for the icon paths. If the path is a string,
      // use it directly. Otherwise, stringify the path object.
      let key = details.path;
      if (typeof key !== "string") {
        key = uneval(key);
      }

      let icons = this.iconCache
        .get(extension)
        .get(context && context.uri.spec)
        .get(details.iconType);

      let icon = icons.get(key);
      if (!icon) {
        icon = this._normalize(details, extension, context);
        icons.set(key, icon);
      }
      return icon;
    }

    return this._normalize(details, extension, context);
  },

  _normalize(details, extension, context = null) {
    let result = {};

    try {
      let { imageData, path, themeIcons } = details;

      if (imageData) {
        if (typeof imageData == "string") {
          imageData = { 19: imageData };
        }

        for (let size of Object.keys(imageData)) {
          result[size] = imageData[size];
        }
      }

      let baseURI = context ? context.uri : extension.baseURI;

      if (path != null) {
        if (typeof path != "object") {
          path = { 19: path };
        }

        for (let size of Object.keys(path)) {
          let url = path[size];
          if (url) {
            url = baseURI.resolve(path[size]);

            // The Chrome documentation specifies these parameters as
            // relative paths. We currently accept absolute URLs as well,
            // which means we need to check that the extension is allowed
            // to load them. This will throw an error if it's not allowed.
            this._checkURL(url, extension);
          }
          result[size] = url || this.DEFAULT_ICON;
        }
      }

      if (themeIcons) {
        themeIcons.forEach(({ size, light, dark }) => {
          let lightURL = baseURI.resolve(light);
          let darkURL = baseURI.resolve(dark);

          this._checkURL(lightURL, extension);
          this._checkURL(darkURL, extension);

          let defaultURL = result[size] || result[19]; // always fallback to default first
          result[size] = {
            default: defaultURL || darkURL, // Fallback to the dark url if no default is specified.
            light: lightURL,
            dark: darkURL,
          };
        });
      }
    } catch (e) {
      // Function is called from extension code, delegate error.
      if (context) {
        throw e;
      }
      // If there's no context, it's because we're handling this
      // as a manifest directive. Log a warning rather than
      // raising an error.
      extension.manifestError(`Invalid icon data: ${e}`);
    }

    return result;
  },

  // Checks if the extension is allowed to load the given URL with the specified principal.
  // This will throw an error if the URL is not allowed.
  _checkURL(url, extension) {
    if (!extension.checkLoadURL(url, { allowInheritsPrincipal: true })) {
      throw new ExtensionError(`Illegal URL ${url}`);
    }
  },

  // Returns the appropriate icon URL for the given icons object and the
  // screen resolution of the given window.
  getPreferredIcon(icons, extension, size = 16) {
    const DEFAULT = "chrome://mozapps/skin/extensions/extensionGeneric.svg";

    let bestSize = null;
    if (icons[size]) {
      bestSize = size;
    } else if (icons[2 * size]) {
      bestSize = 2 * size;
    } else {
      let sizes = Object.keys(icons)
        .map(key => parseInt(key, 10))
        .sort((a, b) => a - b);

      bestSize = sizes.find(candidate => candidate > size) || sizes.pop();
    }

    if (bestSize) {
      return { size: bestSize, icon: icons[bestSize] || DEFAULT };
    }

    return { size, icon: DEFAULT };
  },

  // These URLs should already be properly escaped, but make doubly sure CSS
  // string escape characters are escaped here, since they could lead to a
  // sandbox break.
  escapeUrl(url) {
    return url.replace(/[\\\s"]/g, encodeURIComponent);
  },
};

class CacheStore {
  constructor(storeName) {
    this.storeName = storeName;
  }

  async getStore(path = null) {
    let data = await StartupCache.dataPromise;

    let store = data.get(this.storeName);
    if (!store) {
      store = new Map();
      data.set(this.storeName, store);
    }

    let key = path;
    if (Array.isArray(path)) {
      for (let elem of path.slice(0, -1)) {
        let next = store.get(elem);
        if (!next) {
          next = new Map();
          store.set(elem, next);
        }
        store = next;
      }
      key = path[path.length - 1];
    }

    return [store, key];
  }

  async get(path, createFunc) {
    let [store, key] = await this.getStore(path);

    let result = store.get(key);

    if (result === undefined) {
      result = await createFunc(path);
      store.set(key, result);
      StartupCache.save();
    }

    return result;
  }

  async set(path, value) {
    let [store, key] = await this.getStore(path);

    store.set(key, value);
    StartupCache.save();
  }

  async getAll() {
    let [store] = await this.getStore();

    return new Map(store);
  }

  async delete(path) {
    let [store, key] = await this.getStore(path);

    if (store.delete(key)) {
      StartupCache.save();
    }
  }
}

// A cache to support faster initialization of extensions at browser startup.
// All cached data is removed when the browser is updated.
// Extension-specific data is removed when the add-on is updated.
export var StartupCache = {
  _ensureDirectoryPromise: null,
  _saveTask: null,

  _ensureDirectory() {
    if (this._ensureDirectoryPromise === null) {
      this._ensureDirectoryPromise = IOUtils.makeDirectory(
        PathUtils.parent(this.file),
        {
          ignoreExisting: true,
          createAncestors: true,
        }
      );
    }

    return this._ensureDirectoryPromise;
  },

  // When the application version changes, this file is removed by
  // RemoveComponentRegistries in nsAppRunner.cpp.
  file: PathUtils.join(
    Services.dirsvc.get("ProfLD", Ci.nsIFile).path,
    "startupCache",
    "webext.sc.lz4"
  ),

  async _saveNow() {
    let data = new Uint8Array(lazy.aomStartup.encodeBlob(this._data));
    await this._ensureDirectoryPromise;
    await IOUtils.write(this.file, data, { tmpPath: `${this.file}.tmp` });

    Glean.extensions.startupCacheWriteBytelength.set(data.byteLength);
  },

  save() {
    this._ensureDirectory();

    if (!this._saveTask) {
      this._saveTask = new lazy.DeferredTask(() => this._saveNow(), 5000);

      IOUtils.profileBeforeChange.addBlocker(
        "Flush WebExtension StartupCache",
        async () => {
          await this._saveTask.finalize();
          this._saveTask = null;
        }
      );
    }

    return this._saveTask.arm();
  },

  _data: null,
  async _readData() {
    let result = new Map();
    try {
      Glean.extensions.startupCacheLoadTime.start();
      let { buffer } = await IOUtils.read(this.file);

      result = lazy.aomStartup.decodeBlob(buffer);
      Glean.extensions.startupCacheLoadTime.stop();
    } catch (e) {
      Glean.extensions.startupCacheLoadTime.cancel();
      if (!DOMException.isInstance(e) || e.name !== "NotFoundError") {
        Cu.reportError(e);
      }
      let error = lazy.getErrorNameForTelemetry(e);
      Glean.extensions.startupCacheReadErrors[error].add(1);
    }

    this._data = result;
    return result;
  },

  get dataPromise() {
    if (!this._dataPromise) {
      this._dataPromise = this._readData();
    }
    return this._dataPromise;
  },

  clearAddonData(id) {
    return Promise.all([
      this.general.delete(id),
      this.locales.delete(id),
      this.manifests.delete(id),
      this.permissions.delete(id),
    ]).catch(() => {
      // Ignore the error. It happens when we try to flush the add-on
      // data after the AddonManager has flushed the entire startup cache.
    });
  },

  observe(subject, topic) {
    if (topic === "startupcache-invalidate") {
      this._data = new Map();
      this._dataPromise = Promise.resolve(this._data);
    }
  },

  get(extension, path, createFunc) {
    return this.general.get(
      [extension.id, extension.version, ...path],
      createFunc
    );
  },

  delete(extension, path) {
    return this.general.delete([extension.id, extension.version, ...path]);
  },

  general: new CacheStore("general"),
  locales: new CacheStore("locales"),
  manifests: new CacheStore("manifests"),
  other: new CacheStore("other"),
  permissions: new CacheStore("permissions"),
  schemas: new CacheStore("schemas"),
};

Services.obs.addObserver(StartupCache, "startupcache-invalidate");

export var ExtensionParent = {
  GlobalManager,
  HiddenExtensionPage,
  IconDetails,
  ParentAPIManager,
  StartupCache,
  WebExtensionPolicy,
  apiManager,
  promiseBackgroundViewLoaded,
  watchExtensionProxyContextLoad,
  watchExtensionWorkerContextLoaded,
  DebugUtils,
};

// browserPaintedPromise and browserStartupPromise are promises that
// resolve after the first browser window is painted and after browser
// windows have been restored, respectively. Alternatively,
// browserStartupPromise also resolves from the extensions-late-startup
// notification sent by Firefox Reality on desktop platforms, because it
// doesn't support SessionStore.
// _resetStartupPromises should only be called from outside this file in tests.
ExtensionParent._resetStartupPromises = () => {
  ExtensionParent.browserPaintedPromise = promiseObserved(
    "browser-delayed-startup-finished"
  ).then(() => {});
  ExtensionParent.browserStartupPromise = Promise.race([
    promiseObserved("sessionstore-windows-restored"),
    promiseObserved("extensions-late-startup"),
  ]).then(() => {});
};
ExtensionParent._resetStartupPromises();

ChromeUtils.defineLazyGetter(ExtensionParent, "PlatformInfo", () => {
  return Object.freeze({
    os: (function () {
      /** @type {string} */
      let os = AppConstants.platform;
      if (os == "macosx") {
        os = "mac";
      }
      return os;
    })(),
    arch: (function () {
      let abi = Services.appinfo.XPCOMABI;
      let [arch] = abi.split("-");
      if (arch == "x86") {
        arch = "x86-32";
      } else if (arch == "x86_64") {
        arch = "x86-64";
      }
      return arch;
    })(),
  });
});
