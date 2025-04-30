/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This file handles addon logic that is independent of the chrome process and
 * may run in all web content and extension processes.
 *
 * Don't put contentscript logic here, use ExtensionContent.sys.mjs instead.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionContent: "resource://gre/modules/ExtensionContent.sys.mjs",
  ExtensionPageChild: "resource://gre/modules/ExtensionPageChild.sys.mjs",
  ExtensionProcessScript:
    "resource://gre/modules/ExtensionProcessScript.sys.mjs",
  NativeApp: "resource://gre/modules/NativeMessaging.sys.mjs",
  finalizationService: {
    service: "@mozilla.org/toolkit/finalizationwitness;1",
    iid: Ci.nsIFinalizationWitnessService,
  },
});

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { DefaultMap, ExtensionError, LimitedSet, getUniqueId } = ExtensionUtils;

const {
  redefineGetter,
  EventEmitter,
  EventManager,
  LocalAPIImplementation,
  LocaleData,
  NoCloneSpreadArgs,
  SchemaAPIInterface,
  withHandlingUserInput,
} = ExtensionCommon;

const { sharedData } = Services.cpmm;

const MSG_SET_ENABLED = "Extension:ActivityLog:SetEnabled";
const MSG_LOG = "Extension:ActivityLog:DoLog";

export const ExtensionActivityLogChild = {
  _initialized: false,
  enabledExtensions: new Set(),

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;

    Services.cpmm.addMessageListener(MSG_SET_ENABLED, this);

    this.enabledExtensions = new Set(
      Services.cpmm.sharedData.get("extensions/logging")
    );
  },

  receiveMessage({ name, data }) {
    if (name === MSG_SET_ENABLED) {
      if (data.value) {
        this.enabledExtensions.add(data.id);
      } else {
        this.enabledExtensions.delete(data.id);
      }
    }
  },

  async log(context, type, name, data) {
    this.init();
    let { id } = context.extension;
    if (this.enabledExtensions.has(id)) {
      this._sendActivity({
        timeStamp: Date.now(),
        id,
        viewType: context.viewType,
        type,
        name,
        data,
        browsingContextId: context.browsingContextId,
      });
    }
  },

  _sendActivity(data) {
    Services.cpmm.sendAsyncMessage(MSG_LOG, data);
  },
};

// A helper to allow us to distinguish trusted errors from unsanitized errors.
// Extensions can create plain objects with arbitrary properties (such as
// mozWebExtLocation), but not create instances of ExtensionErrorHolder.
class ExtensionErrorHolder {
  constructor(trustedErrorObject) {
    this.trustedErrorObject = trustedErrorObject;
  }
}

/**
 * A finalization witness helper that wraps a sendMessage response and
 * guarantees to either get the promise resolved, or rejected when the
 * wrapped promise goes out of scope.
 */
const StrongPromise = {
  stillAlive: new Map(),

  wrap(promise, location) {
    let id = String(getUniqueId());
    let witness = lazy.finalizationService.make(
      "extensions-onMessage-witness",
      id
    );

    return new Promise((resolve, reject) => {
      this.stillAlive.set(id, { reject, location });
      promise.then(resolve, reject).finally(() => {
        this.stillAlive.delete(id);
        witness.forget();
      });
    });
  },

  observe(subject, topic, id) {
    let message = "Promised response from onMessage listener went out of scope";
    let { reject, location } = this.stillAlive.get(id);
    reject(new ExtensionErrorHolder({ message, mozWebExtLocation: location }));
    this.stillAlive.delete(id);
  },
};
Services.obs.addObserver(StrongPromise, "extensions-onMessage-witness");

// Simple single-event emitter-like helper, exposes the EventManager api.
export class SimpleEventAPI extends EventManager {
  constructor(context, name) {
    let fires = new Set();
    let register = fire => {
      fires.add(fire);
      fire.location = context.getCaller();
      return () => fires.delete(fire);
    };
    super({ context, name, register });
    this.fires = fires;
  }
  /** @returns {any} */
  emit(...args) {
    return [...this.fires].map(fire => fire.asyncWithoutClone(...args));
  }
}

// runtime.OnMessage event helper, handles custom async/sendResponse logic.
export class MessageEvent extends SimpleEventAPI {
  emit(holder, sender) {
    if (!this.fires.size || !this.context.active) {
      return { received: false };
    }

    sender = Cu.cloneInto(sender, this.context.cloneScope);
    let message = holder.deserialize(this.context.cloneScope);

    let responses = [...this.fires]
      .map(fire => this.wrapResponse(fire, message, sender))
      .filter(x => x !== undefined);

    return !responses.length
      ? { received: true, response: false }
      : Promise.race(responses).then(
          value => ({ response: true, value }),
          error => Promise.reject(this.unwrapOrSanitizeError(error))
        );
  }

  unwrapOrSanitizeError(error) {
    if (error instanceof ExtensionErrorHolder) {
      return error.trustedErrorObject;
    }
    // If not a wrapped error, sanitize it and convert to ExtensionError, so
    // that context.normalizeError will use the error message.
    return new ExtensionError(error?.message ?? "An unexpected error occurred");
  }

  wrapResponse(fire, message, sender) {
    let response, sendResponse;
    let promise = new Promise(resolve => {
      sendResponse = Cu.exportFunction(value => {
        resolve(value);
        response = promise;
      }, this.context.cloneScope);
    });

    let result;
    try {
      result = fire.raw(message, sender, sendResponse);
    } catch (e) {
      return Promise.reject(e);
    }
    if (
      result &&
      typeof result === "object" &&
      Cu.getClassName(result, true) === "Promise" &&
      this.context.principal.subsumes(Cu.getObjectPrincipal(result))
    ) {
      return StrongPromise.wrap(result, fire.location);
    } else if (result === true) {
      return StrongPromise.wrap(promise, fire.location);
    }
    return response;
  }
}

function holdMessage(name, anonymizedName, data, native = null) {
  if (native && AppConstants.platform !== "android") {
    data = lazy.NativeApp.encodeMessage(native.context, data);
  }
  return new StructuredCloneHolder(name, anonymizedName, data);
}

// Implements the runtime.Port extension API object.
export class Port {
  /**
   * @param {BaseContext} context The context that owns this port.
   * @param {number} portId Uniquely identifies this port's channel.
   * @param {string} name Arbitrary port name as defined by the addon.
   * @param {boolean} native Is this a Port for native messaging.
   * @param {object} sender The `Port.sender` property.
   */
  constructor(context, portId, name, native, sender) {
    this.context = context;
    this.name = name;
    this.sender = sender;
    this.holdMessage = native
      ? (name, anonymizedName, data) =>
          holdMessage(name, anonymizedName, data, this)
      : holdMessage;
    this.conduit = context.openConduit(this, {
      portId,
      native,
      source: !sender,
      recv: ["PortMessage", "PortDisconnect"],
      send: ["PortMessage"],
    });
    this.initEventManagers();
  }

  initEventManagers() {
    const { context } = this;
    this.onMessage = new SimpleEventAPI(context, "Port.onMessage");
    this.onDisconnect = new SimpleEventAPI(context, "Port.onDisconnect");
  }

  getAPI() {
    // Public Port object handed to extensions from `connect()` and `onConnect`.
    return {
      name: this.name,
      sender: this.sender,
      error: null,
      onMessage: this.onMessage.api(),
      onDisconnect: this.onDisconnect.api(),
      postMessage: this.sendPortMessage.bind(this),
      disconnect: () => this.conduit.close(),
    };
  }

  recvPortMessage({ holder }) {
    this.onMessage.emit(holder.deserialize(this.api), this.api);
  }

  recvPortDisconnect({ error = null }) {
    this.conduit.close();
    if (this.context.active) {
      this.api.error = error && this.context.normalizeError(error);
      this.onDisconnect.emit(this.api);
    }
  }

  sendPortMessage(json) {
    if (this.conduit.actor) {
      return this.conduit.sendPortMessage({
        holder: this.holdMessage(
          `Port/${this.context.extension.id}/sendPortMessage/${this.name}`,
          `Port/${this.context.extension.id}/sendPortMessage/<anonymized>`,
          json
        ),
      });
    }
    throw new this.context.Error("Attempt to postMessage on disconnected port");
  }

  get api() {
    const scope = this.context.cloneScope;
    const value = Cu.cloneInto(this.getAPI(), scope, { cloneFunctions: true });
    return redefineGetter(this, "api", value);
  }
}

/**
 * Each extension context gets its own Messenger object. It handles the
 * basics of sendMessage, onMessage, connect and onConnect.
 */
export class Messenger {
  constructor(context) {
    this.context = context;
    this.conduit = context.openConduit(this, {
      childId: context.childManager.id,
      query: ["NativeMessage", "RuntimeMessage", "PortConnect"],
      recv: ["RuntimeMessage", "PortConnect"],
    });
    this.initEventManagers();
  }

  initEventManagers() {
    const { context } = this;
    this.onConnect = new SimpleEventAPI(context, "runtime.onConnect");
    this.onConnectEx = new SimpleEventAPI(context, "runtime.onConnectExternal");
    this.onMessage = new MessageEvent(context, "runtime.onMessage");
    this.onMessageEx = new MessageEvent(context, "runtime.onMessageExternal");
  }

  get onUserScriptConnect() {
    return redefineGetter(
      this,
      "onUserScriptConnect",
      new SimpleEventAPI(this.context, "runtime.onUserScriptConnect")
    );
  }

  get onUserScriptMessage() {
    return redefineGetter(
      this,
      "onUserScriptMessage",
      new MessageEvent(this.context, "runtime.onUserScriptMessage")
    );
  }

  sendNativeMessage(nativeApp, json) {
    let holder = holdMessage(
      `Messenger/${this.context.extension.id}/sendNativeMessage/${nativeApp}`,
      null,
      json,
      this
    );
    return this.conduit.queryNativeMessage({ nativeApp, holder });
  }

  /** @type {(args: { context, extensionId?, message, callback, userScriptWorldId? }) => any} */
  sendRuntimeMessage({ context, extensionId, message, callback, ...args }) {
    // this.context is usually used, except with user scripts, where we pass a
    // custom context to ensure that the return value is cloned into the right
    // USER_SCRIPT world.
    context ??= this.context;
    let response = this.conduit.queryRuntimeMessage({
      extensionId: extensionId || this.context.extension.id,
      holder: holdMessage(
        `Messenger/${this.context.extension.id}/sendRuntimeMessage`,
        null,
        message
      ),
      ...args,
    });
    // If |response| is a rejected promise, the value will be sanitized by
    // wrapPromise, according to the rules of context.normalizeError.
    return context.wrapPromise(response, callback);
  }

  connect({ context, name, native = false, ...args }) {
    // this.context is usually used, except with user scripts, where we pass a
    // custom context to ensure that the return value is cloned into the right
    // USER_SCRIPT world.
    context ??= this.context;
    let portId = getUniqueId();
    let port = new Port(context, portId, name, !!native);
    this.conduit
      .queryPortConnect({ portId, name, native, ...args })
      .catch(error => port.recvPortDisconnect({ error }));
    return port.api;
  }

  recvPortConnect({ extensionId, portId, name, sender, userScriptWorldId }) {
    let event = sender.id === extensionId ? this.onConnect : this.onConnectEx;
    if (typeof userScriptWorldId == "string") {
      sender = { ...sender, userScriptWorldId };
      event = this.onUserScriptConnect;
    }
    if (this.context.active && event.fires.size) {
      let port = new Port(this.context, portId, name, false, sender);
      return event.emit(port.api).length;
    }
  }

  recvRuntimeMessage({ extensionId, holder, sender, userScriptWorldId }) {
    let event = sender.id === extensionId ? this.onMessage : this.onMessageEx;
    if (typeof userScriptWorldId == "string") {
      sender = { ...sender, userScriptWorldId };
      return this.onUserScriptMessage.emit(holder, sender);
    }
    return event.emit(holder, sender);
  }
}

// For test use only.
var ExtensionManager = {
  extensions: new Map(),
};

/**
 * Represents an extension instance in the child process.
 * Corresponds to the @see {Extension} instance in the parent.
 */
export class ExtensionChild extends EventEmitter {
  constructor(policy) {
    super();

    this.policy = policy;
    // Set a weak reference to this instance on the WebExtensionPolicy expando properties
    // (because it makes it easier to reach the extension instance from the policy object
    // without leaking it due to a circular dependency keeping it alive).
    this.policy.weakExtension = Cu.getWeakReference(this);

    this.instanceId = policy.instanceId;
    this.optionalPermissions = policy.optionalPermissions;

    if (WebExtensionPolicy.isExtensionProcess) {
      // Keep in sync with serializeExtended in Extension.sys.mjs
      let ed = this.getSharedData("extendedData");
      this.backgroundScripts = ed.backgroundScripts;
      this.backgroundWorkerScript = ed.backgroundWorkerScript;
      this.childModules = ed.childModules;
      this.dependencies = ed.dependencies;
      this.persistentBackground = ed.persistentBackground;
      this.schemaURLs = ed.schemaURLs;
    }

    this.MESSAGE_EMIT_EVENT = `Extension:EmitEvent:${this.instanceId}`;
    Services.cpmm.addMessageListener(this.MESSAGE_EMIT_EVENT, this);

    this.apiManager = this.getAPIManager();

    this._manifest = null;
    this._localeData = null;

    this.baseURI = Services.io.newURI(`moz-extension://${this.uuid}/`);
    this.baseURL = this.baseURI.spec;

    this.principal = Services.scriptSecurityManager.createContentPrincipal(
      this.baseURI,
      {}
    );

    // Only used in addon processes.
    this.blockedParsingDocuments = new WeakSet();
    this.views = new Set();

    // Only used for devtools views.
    this.devtoolsViews = new Set();

    ExtensionManager.extensions.set(this.id, this);
  }

  get id() {
    return this.policy.id;
  }

  get uuid() {
    return this.policy.mozExtensionHostname;
  }

  get permissions() {
    return new Set(this.policy.permissions);
  }

  get allowedOrigins() {
    return this.policy.allowedOrigins;
  }

  getSharedData(key) {
    return sharedData.get(`extension/${this.id}/${key}`);
  }

  get localeData() {
    if (!this._localeData) {
      this._localeData = new LocaleData(this.getSharedData("locales"));
    }
    return this._localeData;
  }

  get manifest() {
    if (!this._manifest) {
      this._manifest = this.getSharedData("manifest");
    }
    return this._manifest;
  }

  get manifestVersion() {
    return this.manifest.manifest_version;
  }

  get privateBrowsingAllowed() {
    return this.policy.privateBrowsingAllowed;
  }

  canAccessWindow(window) {
    return this.policy.canAccessWindow(window);
  }

  getAPIManager() {
    /** @type {InstanceType<typeof ExtensionCommon.LazyAPIManager>[]} */
    let apiManagers = [lazy.ExtensionPageChild.apiManager];

    if (this.dependencies) {
      for (let id of this.dependencies) {
        let extension = lazy.ExtensionProcessScript.getExtensionChild(id);
        if (extension) {
          apiManagers.push(extension.experimentAPIManager);
        }
      }
    }

    if (this.childModules) {
      this.experimentAPIManager = new ExtensionCommon.LazyAPIManager(
        "addon",
        this.childModules,
        this.schemaURLs
      );

      apiManagers.push(this.experimentAPIManager);
    }

    if (apiManagers.length == 1) {
      return apiManagers[0];
    }

    return new ExtensionCommon.MultiAPIManager("addon", apiManagers.reverse());
  }

  shutdown() {
    ExtensionManager.extensions.delete(this.id);
    lazy.ExtensionContent.shutdownExtension(this);
    Services.cpmm.removeMessageListener(this.MESSAGE_EMIT_EVENT, this);
    this.emit("shutdown");
  }

  getContext(window) {
    return lazy.ExtensionContent.getContext(this, window);
  }

  // Implementation of runtime.getURL / extension.getURL.
  // ExtensionData.prototype.getURL has a similar signature and return value.
  getURL(path = "") {
    if (path.startsWith(this.baseURL)) {
      // Historically, when the input is an already-resolved extension URL,
      // we return the parsed version of it as-is.
      return path;
    }
    if (path.startsWith("/")) {
      // this.baseURL already contains a "/".
      path = path.slice(1);
    }
    return this.baseURL + path;
  }

  emit(event, ...args) {
    Services.cpmm.sendAsyncMessage(this.MESSAGE_EMIT_EVENT, { event, args });
    return super.emit(event, ...args);
  }

  // TODO(Bug 1768471): consider folding this back into emit if we will change it to
  // return a value as EventEmitter and Extension emit methods do.
  emitLocalWithResult(event, ...args) {
    return super.emit(event, ...args);
  }

  receiveMessage({ name, data }) {
    if (name === this.MESSAGE_EMIT_EVENT) {
      super.emit(data.event, ...data.args);
    }
  }

  localizeMessage(...args) {
    return this.localeData.localizeMessage(...args);
  }

  localize(...args) {
    return this.localeData.localize(...args);
  }

  hasPermission(perm) {
    // If the permission is a "manifest property" permission, we check if the extension
    // does have the required property in its manifest.
    let manifest_ = "manifest:";
    if (perm.startsWith(manifest_)) {
      // Handle nested "manifest property" permission (e.g. as in "manifest:property.nested").
      let value = this.manifest;
      for (let prop of perm.substr(manifest_.length).split(".")) {
        if (!value) {
          break;
        }
        value = value[prop];
      }

      return value != null;
    }
    return this.permissions.has(perm);
  }

  trackBlockedParsingDocument(doc) {
    this.blockedParsingDocuments.add(doc);
  }

  untrackBlockedParsingDocument(doc) {
    this.blockedParsingDocuments.delete(doc);
  }

  hasContextBlockedParsingDocument(extContext) {
    return this.blockedParsingDocuments.has(extContext.contentWindow?.document);
  }
}

/**
 * An object that runs an remote implementation of an API.
 */
export class ProxyAPIImplementation extends SchemaAPIInterface {
  /**
   * @param {string} namespace The full path to the namespace that contains the
   *     `name` member. This may contain dots, e.g. "storage.local".
   * @param {string} name The name of the method or property.
   * @param {ChildAPIManager} childApiManager The owner of this implementation.
   * @param {boolean} alreadyLogged Whether the child already logged the event.
   */
  constructor(namespace, name, childApiManager, alreadyLogged = false) {
    super();
    this.path = `${namespace}.${name}`;
    this.childApiManager = childApiManager;
    this.alreadyLogged = alreadyLogged;
  }

  revoke() {
    let map = this.childApiManager.listeners.get(this.path);
    for (let listener of map.listeners.keys()) {
      this.removeListener(listener);
    }

    this.path = null;
    this.childApiManager = null;
  }

  callFunctionNoReturn(args) {
    this.childApiManager.callParentFunctionNoReturn(this.path, args);
  }

  callAsyncFunction(args, callback, requireUserInput) {
    const context = this.childApiManager.context;
    const isHandlingUserInput =
      context.contentWindow?.windowUtils?.isHandlingUserInput;
    if (requireUserInput) {
      if (!isHandlingUserInput) {
        let err = new context.cloneScope.Error(
          `${this.path} may only be called from a user input handler`
        );
        return context.wrapPromise(Promise.reject(err), callback);
      }
    }
    return this.childApiManager.callParentAsyncFunction(
      this.path,
      args,
      callback,
      {
        alreadyLogged: this.alreadyLogged,
        isHandlingUserInput,
      }
    );
  }

  addListener(listener, args) {
    let map = this.childApiManager.listeners.get(this.path);

    if (map.listeners.has(listener)) {
      // TODO: Called with different args?
      return;
    }

    let id = getUniqueId();

    map.ids.set(id, listener);
    map.listeners.set(listener, id);

    this.childApiManager.conduit.sendAddListener({
      childId: this.childApiManager.id,
      listenerId: id,
      path: this.path,
      args,
      alreadyLogged: this.alreadyLogged,
    });
  }

  removeListener(listener) {
    let map = this.childApiManager.listeners.get(this.path);

    if (!map.listeners.has(listener)) {
      return;
    }

    let id = map.listeners.get(listener);
    map.listeners.delete(listener);
    map.ids.delete(id);
    map.removedIds.add(id);

    this.childApiManager.conduit.sendRemoveListener({
      childId: this.childApiManager.id,
      listenerId: id,
      path: this.path,
      alreadyLogged: this.alreadyLogged,
    });
  }

  hasListener(listener) {
    let map = this.childApiManager.listeners.get(this.path);
    return map.listeners.has(listener);
  }
}

export class ChildLocalAPIImplementation extends LocalAPIImplementation {
  constructor(pathObj, namespace, name, childApiManager) {
    super(pathObj, name, childApiManager.context);
    this.childApiManagerId = childApiManager.id;
    this.fullname = `${namespace}.${name}`;
  }

  /**
   * Call the given function and also log the call as appropriate
   * (i.e., with activity logging and/or profiler markers)
   *
   * @param {Function} callable The actual implementation to invoke.
   * @param {Array} args Arguments to the function call.
   * @returns {any} The return result of callable.
   */
  callAndLog(callable, args) {
    this.context.logActivity("api_call", this.fullname, { args });
    let start = Cu.now();
    try {
      return callable();
    } finally {
      ChromeUtils.addProfilerMarker(
        "ExtensionChild",
        { startTime: start },
        `${this.context.extension.id}, api_call: ${this.fullname}`
      );
    }
  }

  callFunction(args) {
    return this.callAndLog(() => super.callFunction(args), args);
  }

  callFunctionNoReturn(args) {
    return this.callAndLog(() => super.callFunctionNoReturn(args), args);
  }

  callAsyncFunction(args, callback, requireUserInput) {
    return this.callAndLog(
      () => super.callAsyncFunction(args, callback, requireUserInput),
      args
    );
  }
}

// We create one instance of this class for every extension context that
// needs to use remote APIs. It uses the the JSWindowActor and
// JSProcessActor Conduits actors (see ConduitsChild.sys.mjs) to communicate
// with the ParentAPIManager singleton in ExtensionParent.sys.mjs.
// It handles asynchronous function calls as well as event listeners.
export class ChildAPIManager {
  constructor(context, messageManager, localAPICan, contextData) {
    this.context = context;
    this.messageManager = messageManager;
    this.url = contextData.url;

    // The root namespace of all locally implemented APIs. If an extension calls
    // an API that does not exist in this object, then the implementation is
    // delegated to the ParentAPIManager.
    this.localApis = localAPICan.root;
    this.apiCan = localAPICan;
    this.schema = this.apiCan.apiManager.schema;

    this.id = `${context.extension.id}.${context.contextId}`;

    this.conduit = context.openConduit(this, {
      childId: this.id,
      send: [
        "CreateProxyContext",
        "ContextLoaded",
        "APICall",
        "AddListener",
        "RemoveListener",
      ],
      recv: ["CallResult", "RunListener", "StreamFilterSuspendCancel"],
    });

    this.conduit.sendCreateProxyContext({
      childId: this.id,
      extensionId: context.extension.id,
      principal: context.principal,
      ...contextData,
    });

    this.listeners = new DefaultMap(() => ({
      ids: new Map(),
      listeners: new Map(),
      removedIds: new LimitedSet(10),
    }));

    // Map[callId -> Deferred]
    this.callPromises = new Map();

    this.permissionsChangedCallbacks = new Set();
    this.updatePermissions = null;
    if (this.context.extension.optionalPermissions.length) {
      this.updatePermissions = () => {
        for (let callback of this.permissionsChangedCallbacks) {
          try {
            callback();
          } catch (err) {
            Cu.reportError(err);
          }
        }
      };
      this.context.extension.on("update-permissions", this.updatePermissions);
    }
  }

  inject(obj) {
    this.schema.inject(obj, this);
  }

  recvCallResult(data) {
    let deferred = this.callPromises.get(data.callId);
    this.callPromises.delete(data.callId);
    if ("error" in data) {
      deferred.reject(data.error);
    } else {
      let result = data.result.deserialize(this.context.cloneScope);

      deferred.resolve(new NoCloneSpreadArgs(result));
    }
  }

  recvRunListener(data) {
    let map = this.listeners.get(data.path);
    let listener = map.ids.get(data.listenerId);

    if (listener) {
      if (!this.context.active) {
        Services.console.logStringMessage(
          `Ignored listener for inactive context at childId=${data.childId} path=${data.path} listenerId=${data.listenerId}\n`
        );
        return;
      }

      let args = data.args.deserialize(this.context.cloneScope);
      let fire = () => this.context.applySafeWithoutClone(listener, args);
      return Promise.resolve(
        data.handlingUserInput
          ? withHandlingUserInput(this.context.contentWindow, fire)
          : fire()
      ).then(result => {
        if (result !== undefined) {
          return new StructuredCloneHolder(
            `ChildAPIManager/${this.context.extension.id}/${data.path}`,
            null,
            result,
            this.context.cloneScope
          );
        }
        return result;
      });
    }
    if (!map.removedIds.has(data.listenerId)) {
      Services.console.logStringMessage(
        `Unknown listener at childId=${data.childId} path=${data.path} listenerId=${data.listenerId}\n`
      );
    }
  }

  async recvStreamFilterSuspendCancel() {
    const promise = this.context.extension.emitLocalWithResult(
      "internal:stream-filter-suspend-cancel"
    );
    // if all listeners throws emitLocalWithResult returns undefined.
    if (!promise) {
      return false;
    }

    return promise.then(results =>
      results.some(hasActiveStreamFilter => hasActiveStreamFilter === true)
    );
  }

  /**
   * Call a function in the parent process and ignores its return value.
   *
   * @param {string} path The full name of the method, e.g. "tabs.create".
   * @param {Array} args The parameters for the function.
   */
  callParentFunctionNoReturn(path, args) {
    this.conduit.sendAPICall({ childId: this.id, path, args });
  }

  /**
   * Calls a function in the parent process and returns its result
   * asynchronously.
   *
   * @param {string} path The full name of the method, e.g. "tabs.create".
   * @param {Array} args The parameters for the function.
   * @param {callback} [callback] The callback to be called when the
   *      function completes.
   * @param {object} [options] Extra options.
   * @returns {Promise|undefined} Must be void if `callback` is set, and a
   *     promise otherwise. The promise is resolved when the function completes.
   */
  callParentAsyncFunction(path, args, callback, options = {}) {
    let callId = getUniqueId();
    let deferred = Promise.withResolvers();
    this.callPromises.set(callId, deferred);

    let {
      // Any child api that calls into a parent function will have already
      // logged the api_call.  Flag it so the parent doesn't log again.
      alreadyLogged = true,
      // Propagating the isHAndlingUserInput flag to the API call handler
      // executed on the parent process side.
      isHandlingUserInput = false,
    } = options;

    // TODO: conduit.queryAPICall()
    this.conduit.sendAPICall({
      childId: this.id,
      callId,
      path,
      args,
      options: { alreadyLogged, isHandlingUserInput },
    });
    return this.context.wrapPromise(deferred.promise, callback);
  }

  /**
   * Create a proxy for an event in the parent process. The returned event
   * object shares its internal state with other instances. For instance, if
   * `removeListener` is used on a listener that was added on another object
   * through `addListener`, then the event is unregistered.
   *
   * @param {string} path The full name of the event, e.g. "tabs.onCreated".
   * @returns {object} An object with the addListener, removeListener and
   *   hasListener methods. See SchemaAPIInterface for documentation.
   */
  getParentEvent(path) {
    let parts = path.split(".");

    let name = parts.pop();
    let namespace = parts.join(".");

    let impl = new ProxyAPIImplementation(namespace, name, this, true);
    return {
      addListener: (listener, ...args) => impl.addListener(listener, args),
      removeListener: listener => impl.removeListener(listener),
      hasListener: listener => impl.hasListener(listener),
    };
  }

  close() {
    // Reports CONDUIT_CLOSED on the parent side.
    this.conduit.close();

    if (this.updatePermissions) {
      this.context.extension.off("update-permissions", this.updatePermissions);
    }
  }

  get cloneScope() {
    return this.context.cloneScope;
  }

  get principal() {
    return this.context.principal;
  }

  get manifestVersion() {
    return this.context.manifestVersion;
  }

  shouldInject(namespace, name, allowedContexts) {
    // Do not generate content script APIs, unless explicitly allowed.
    if (
      this.context.envType === "content_child" &&
      !allowedContexts.includes("content")
    ) {
      return false;
    }

    // Do not generate devtools APIs, unless explicitly allowed.
    if (
      this.context.envType === "devtools_child" &&
      !allowedContexts.includes("devtools")
    ) {
      return false;
    }

    // Do not generate devtools APIs, unless explicitly allowed.
    if (
      this.context.envType !== "devtools_child" &&
      allowedContexts.includes("devtools_only")
    ) {
      return false;
    }

    // Do not generate content_only APIs, unless explicitly allowed.
    if (
      this.context.envType !== "content_child" &&
      allowedContexts.includes("content_only")
    ) {
      return false;
    }

    return true;
  }

  getImplementation(namespace, name) {
    this.apiCan.findAPIPath(`${namespace}.${name}`);
    let obj = this.apiCan.findAPIPath(namespace);

    if (obj && name in obj) {
      return new ChildLocalAPIImplementation(obj, namespace, name, this);
    }

    return this.getFallbackImplementation(namespace, name);
  }

  getFallbackImplementation(namespace, name) {
    // No local API found, defer implementation to the parent.
    return new ProxyAPIImplementation(namespace, name, this);
  }

  hasPermission(permission) {
    return this.context.extension.hasPermission(permission);
  }

  isPermissionRevokable(permission) {
    return this.context.extension.optionalPermissions.includes(permission);
  }

  setPermissionsChangedCallback(callback) {
    this.permissionsChangedCallbacks.add(callback);
  }
}
