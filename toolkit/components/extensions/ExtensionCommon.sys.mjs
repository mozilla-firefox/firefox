/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This module contains utilities and base classes for logic which is
 * common between the parent and child process, and in particular
 * between ExtensionParent.sys.mjs and ExtensionChild.sys.mjs.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ConsoleAPI: "resource://gre/modules/Console.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SchemaRoot: "resource://gre/modules/Schemas.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
  styleSheetService: {
    service: "@mozilla.org/content/style-sheet-service;1",
    iid: Ci.nsIStyleSheetService,
  },
  wptEnabled: {
    pref: "extensions.wpt.enabled",
    default: false,
  },
});

const ScriptError = Components.Constructor(
  "@mozilla.org/scripterror;1",
  "nsIScriptError",
  "initWithWindowID"
);

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

var {
  DefaultMap,
  DefaultWeakMap,
  ExtensionError,
  filterStack,
  getInnerWindowID,
  getUniqueId,
} = ExtensionUtils;

function getConsole() {
  return new lazy.ConsoleAPI({
    maxLogLevelPref: "extensions.webextensions.log.level",
    prefix: "WebExtensions",
  });
}

// Run a function and report exceptions.
function runSafeSyncWithoutClone(f, ...args) {
  try {
    return f(...args);
  } catch (e) {
    // This method is called with `this` unbound and it doesn't have
    // access to a BaseContext instance and so we can't check if `e`
    // is an instance of the extension context's Error constructor
    // (like we do in BaseContext applySafeWithoutClone method).
    dump(
      `Extension error: ${e} ${e?.fileName} ${
        e?.lineNumber
      }\n[[Exception stack\n${
        e?.stack ? filterStack(e) : undefined
      }Current stack\n${filterStack(Error())}]]\n`
    );
    Cu.reportError(e);
  }
}

// Return true if the given value is an instance of the given
// native type.
function instanceOf(value, type) {
  return (
    value &&
    typeof value === "object" &&
    ChromeUtils.getClassName(value) === type
  );
}

/**
 * Convert any of several different representations of a date/time to a Date object.
 * Accepts several formats:
 * a Date object, an ISO8601 string, or a number of milliseconds since the epoch as
 * either a number or a string.
 *
 * @param {Date|string|number} date
 *      The date to convert.
 * @returns {Date}
 *      A Date object
 */
function normalizeTime(date) {
  // Of all the formats we accept the "number of milliseconds since the epoch as a string"
  // is an outlier, everything else can just be passed directly to the Date constructor.
  return new Date(
    typeof date == "string" && /^\d+$/.test(date) ? parseInt(date, 10) : date
  );
}

function withHandlingUserInput(window, callable) {
  let handle = window.windowUtils.setHandlingUserInput(true);
  try {
    return callable();
  } finally {
    handle.destruct();
  }
}

/**
 * Defines a lazy getter for the given property on the given object. The
 * first time the property is accessed, the return value of the getter
 * is defined on the current `this` object with the given property name.
 * Importantly, this means that a lazy getter defined on an object
 * prototype will be invoked separately for each object instance that
 * it's accessed on.
 *
 * Note: for better type inference, prefer redefineGetter() below.
 *
 * @param {object} object
 *        The prototype object on which to define the getter.
 * @param {string | symbol} prop
 *        The property name for which to define the getter.
 * @param {callback} getter
 *        The function to call in order to generate the final property
 *        value.
 */
function defineLazyGetter(object, prop, getter) {
  Object.defineProperty(object, prop, {
    enumerable: true,
    configurable: true,
    get() {
      return redefineGetter(this, prop, getter.call(this), true);
    },
    set(value) {
      redefineGetter(this, prop, value, true);
    },
  });
}

/**
 * A more type-inference friendly version of defineLazyGetter() above.
 * Call it from a real getter (and setter) for your class or object.
 * On first run, it will redefine the property with the final value.
 *
 * @template Value
 * @param {object} object
 * @param {string | symbol} key
 * @param {Value} value
 * @returns {Value}
 */
function redefineGetter(object, key, value, writable = false) {
  Object.defineProperty(object, key, {
    enumerable: true,
    configurable: true,
    writable,
    value,
  });
  return value;
}

function checkLoadURI(uri, principal, options) {
  let ssm = Services.scriptSecurityManager;

  let flags = ssm.STANDARD;
  if (!options.allowScript) {
    flags |= ssm.DISALLOW_SCRIPT;
  }
  if (!options.allowInheritsPrincipal) {
    flags |= ssm.DISALLOW_INHERIT_PRINCIPAL;
  }
  if (options.dontReportErrors) {
    flags |= ssm.DONT_REPORT_ERRORS;
  }

  try {
    ssm.checkLoadURIWithPrincipal(principal, uri, flags);
  } catch (e) {
    return false;
  }
  return true;
}

function checkLoadURL(url, principal, options) {
  try {
    return checkLoadURI(Services.io.newURI(url), principal, options);
  } catch (e) {
    return false; // newURI threw.
  }
}

function makeWidgetId(id) {
  id = id.toLowerCase();
  // FIXME: This allows for collisions.
  return id.replace(/[^a-z0-9_-]/g, "_");
}

/**
 * A sentinel class to indicate that an array of values should be
 * treated as an array when used as a promise resolution value, but as a
 * spread expression (...args) when passed to a callback.
 */
class SpreadArgs extends Array {
  constructor(args) {
    super();
    this.push(...args);
  }
}

/**
 * Like SpreadArgs, but also indicates that the array values already
 * belong to the target compartment, and should not be cloned before
 * being passed.
 *
 * The `unwrappedValues` property contains an Array object which belongs
 * to the target compartment, and contains the same unwrapped values
 * passed the NoCloneSpreadArgs constructor.
 */
class NoCloneSpreadArgs {
  constructor(args) {
    this.unwrappedValues = args;
  }

  [Symbol.iterator]() {
    return this.unwrappedValues[Symbol.iterator]();
  }
}

const LISTENERS = Symbol("listeners");
const ONCE_MAP = Symbol("onceMap");

export class EventEmitter {
  constructor() {
    this[LISTENERS] = new Map();
    this[ONCE_MAP] = new WeakMap();
  }

  /**
   * Checks whether there is some listener for the given event.
   *
   * @param {string} event
   *       The name of the event to listen for.
   * @returns {boolean}
   */
  has(event) {
    return this[LISTENERS].has(event);
  }

  /**
   * Adds the given function as a listener for the given event.
   *
   * The listener function may optionally return a Promise which
   * resolves when it has completed all operations which event
   * dispatchers may need to block on.
   *
   * @param {string} event
   *       The name of the event to listen for.
   * @param {function(string, ...any): any} listener
   *        The listener to call when events are emitted.
   */
  on(event, listener) {
    let listeners = this[LISTENERS].get(event);
    if (!listeners) {
      listeners = new Set();
      this[LISTENERS].set(event, listeners);
    }

    listeners.add(listener);
  }

  /**
   * Removes the given function as a listener for the given event.
   *
   * @param {string} event
   *       The name of the event to stop listening for.
   * @param {function(string, ...any): any} listener
   *        The listener function to remove.
   */
  off(event, listener) {
    let set = this[LISTENERS].get(event);
    if (set) {
      set.delete(listener);
      set.delete(this[ONCE_MAP].get(listener));
      if (!set.size) {
        this[LISTENERS].delete(event);
      }
    }
  }

  /**
   * Adds the given function as a listener for the given event once.
   *
   * @param {string} event
   *       The name of the event to listen for.
   * @param {function(string, ...any): any} listener
   *        The listener to call when events are emitted.
   */
  once(event, listener) {
    let wrapper = (event, ...args) => {
      this.off(event, wrapper);
      this[ONCE_MAP].delete(listener);

      return listener(event, ...args);
    };
    this[ONCE_MAP].set(listener, wrapper);

    this.on(event, wrapper);
  }

  /**
   * Triggers all listeners for the given event. If any listeners return
   * a value, returns a promise which resolves when all returned
   * promises have resolved. Otherwise, returns undefined.
   *
   * @param {string} event
   *       The name of the event to emit.
   * @param {any} args
   *        Arbitrary arguments to pass to the listener functions, after
   *        the event name.
   * @returns {Promise?}
   */
  emit(event, ...args) {
    let listeners = this[LISTENERS].get(event);

    if (listeners) {
      let promises = [];

      for (let listener of listeners) {
        try {
          let result = listener(event, ...args);
          if (result !== undefined) {
            promises.push(result);
          }
        } catch (e) {
          Cu.reportError(e);
        }
      }

      if (promises.length) {
        return Promise.all(promises);
      }
    }
  }
}

/**
 * Base class for WebExtension APIs.  Each API creates a new class
 * that inherits from this class, the derived class is instantiated
 * once for each extension that uses the API.
 */
export class ExtensionAPI extends EventEmitter {
  constructor(extension) {
    super();

    this.extension = extension;

    extension.once("shutdown", (what, isAppShutdown) => {
      if (this.onShutdown) {
        this.onShutdown(isAppShutdown);
      }
      this.extension = null;
    });
  }

  destroy() {}

  /** @param {string} _entryName */
  onManifestEntry(_entryName) {}

  /** @param {boolean} _isAppShutdown */
  onShutdown(_isAppShutdown) {}

  /** @param {BaseContext} _context */
  getAPI(_context) {
    throw new Error("Not Implemented");
  }

  /** @param {string} _id */
  static onDisable(_id) {}

  /** @param {string} _id */
  static onUninstall(_id) {}

  /**
   * @param {string} _id
   * @param {object} _manifest
   */
  static onUpdate(_id, _manifest) {}
}

/**
 * Subclass to add APIs commonly used with persistent events.
 * If a namespace uses events, it should use this subclass.
 *
 * this.apiNamespace = class extends ExtensionAPIPersistent {};
 */
class ExtensionAPIPersistent extends ExtensionAPI {
  /** @type {Record<string, callback>} */
  PERSISTENT_EVENTS;

  /**
   * Check for event entry.
   *
   * @param {string} event The event name e.g. onStateChanged
   * @returns {boolean}
   */
  hasEventRegistrar(event) {
    return (
      this.PERSISTENT_EVENTS && Object.hasOwn(this.PERSISTENT_EVENTS, event)
    );
  }

  /**
   * Get the event registration fuction
   *
   * @param {string} event The event name e.g. onStateChanged
   * @returns {Function} register is used to start the listener
   *                     register returns an object containing
   *                     a convert and unregister function.
   */
  getEventRegistrar(event) {
    if (this.hasEventRegistrar(event)) {
      return this.PERSISTENT_EVENTS[event].bind(this);
    }
  }

  /**
   * Used when instantiating an EventManager instance to register the listener.
   *
   * @param {object}      options         Options used for event registration
   * @param {BaseContext} options.context Extension Context passed when creating an EventManager instance.
   * @param {string}      options.event   The eAPI vent name.
   * @param {Function}    options.fire    The function passed to the listener to fire the event.
   * @param {Array<any>}  params          An optional array of parameters received along with the
   *                                      addListener request.
   * @returns {Function}                  The unregister function used in the EventManager.
   */
  registerEventListener(options, params) {
    const apiRegistar = this.getEventRegistrar(options.event);
    return apiRegistar?.(options, params).unregister;
  }

  /**
   * Used to prime a listener for when the background script is not running.
   *
   * @param {string} event The event name e.g. onStateChanged or captiveURL.onChange.
   * @param {Function} fire The function passed to the listener to fire the event.
   * @param {Array} params Params passed to the event listener.
   * @param {boolean} isInStartup unused here but passed for subclass use.
   * @returns {object} the unregister and convert functions used in the EventManager.
   */
  primeListener(event, fire, params, isInStartup) {
    const apiRegistar = this.getEventRegistrar(event);
    return apiRegistar?.({ fire, isInStartup }, params);
  }
}

/**
 * This class contains the information we have about an individual
 * extension.  It is never instantiated directly, instead subclasses
 * for each type of process extend this class and add members that are
 * relevant for that process.
 *
 * @abstract
 */
export class BaseContext {
  /** @type {boolean} */
  isTopContext;
  /** @type {string} */
  viewType;

  constructor(envType, extension) {
    this.envType = envType;
    this.onClose = new Set();
    this.checkedLastError = false;
    this._lastError = null;
    this.contextId = getUniqueId();
    this.unloaded = false;
    this.extension = extension;
    this.manifestVersion = extension.manifestVersion;
    this.jsonSandbox = null;
    this.active = true;
    this.incognito = null;
    this.messageManager = null;
    this.contentWindow = null;
    this.innerWindowID = 0;
    this.browserId = 0;

    // These two properties are assigned in ContentScriptContextChild subclass
    // to keep a copy of the content script sandbox Error and Promise globals
    // (which are used by the WebExtensions internals) before any extension
    // content script code had any chance to redefine them.
    this.cloneScopeError = null;
    this.cloneScopePromise = null;
  }

  get isProxyContextParent() {
    return false;
  }

  get Error() {
    // Return the copy stored in the context instance (when the context is an instance of
    // ContentScriptContextChild or the global from extension page window otherwise).
    return this.cloneScopeError || this.cloneScope.Error;
  }

  get Promise() {
    // Return the copy stored in the context instance (when the context is an instance of
    // ContentScriptContextChild or the global from extension page window otherwise).
    return this.cloneScopePromise || this.cloneScope.Promise;
  }

  get privateBrowsingAllowed() {
    return this.extension.privateBrowsingAllowed;
  }

  get isBackgroundContext() {
    if (this.viewType === "background") {
      if (this.isProxyContextParent) {
        return !!this.isTopContext; // Set in ExtensionPageContextParent.
      }
      const { contentWindow } = this;
      return !!contentWindow && contentWindow.top === contentWindow;
    }
    return this.viewType === "background_worker";
  }

  /**
   * Whether the extension context is using the WebIDL bindings for the
   * WebExtensions APIs.
   * To be overridden in subclasses (e.g. WorkerContextChild) and to be
   * optionally used in ExtensionAPI classes to customize the behavior of the
   * API when the calls to the extension API are originated from the WebIDL
   * bindings.
   */
  get useWebIDLBindings() {
    return false;
  }

  canAccessWindow(window) {
    return this.extension.canAccessWindow(window);
  }

  canAccessContainer(userContextId) {
    return this.extension.canAccessContainer(userContextId);
  }

  /**
   * Opens a conduit linked to this context, populating related address fields.
   * Only available in child contexts with an associated contentWindow.
   *
   * @type {ConduitGen}
   */
  openConduit(subject, address) {
    let wgc = this.contentWindow.windowGlobalChild;
    let conduit = wgc.getActor("Conduits").openConduit(subject, {
      id: subject.id || getUniqueId(),
      extensionId: this.extension.id,
      envType: this.envType,
      ...address,
    });
    this.callOnClose(conduit);
    conduit.setCloseCallback(() => {
      this.forgetOnClose(conduit);
    });
    return conduit;
  }

  setContentWindow(contentWindow) {
    if (!this.canAccessWindow(contentWindow)) {
      throw new Error(
        "BaseContext attempted to load when extension is not allowed due to incognito settings."
      );
    }

    this.browserId = contentWindow.browsingContext?.browserId;
    this.innerWindowID = getInnerWindowID(contentWindow);
    this.messageManager = contentWindow.docShell.messageManager;

    if (this.incognito == null) {
      this.incognito =
        lazy.PrivateBrowsingUtils.isContentWindowPrivate(contentWindow);
    }

    let wgc = contentWindow.windowGlobalChild;
    Object.defineProperty(this, "active", {
      configurable: true,
      enumerable: true,
      get: () => wgc.isCurrentGlobal && !wgc.windowContext.isInBFCache,
    });
    Object.defineProperty(this, "contentWindow", {
      configurable: true,
      enumerable: true,
      get: () => (this.active ? wgc.browsingContext.window : null),
    });
    this.callOnClose({
      close: () => {
        // Allow other "close" handlers to use these properties, until the next tick.
        Promise.resolve().then(() => {
          Object.defineProperty(this, "contentWindow", { value: null });
          Object.defineProperty(this, "active", { value: false });
          wgc = null;
        });
      },
    });
  }

  // All child contexts must implement logActivity.  This is handled if the child
  // context subclasses ExtensionBaseContextChild.  ProxyContextParent overrides
  // this with a noop for parent contexts.
  logActivity(_type, _name, _data) {
    throw new Error(`Not implemented for ${this.envType}`);
  }

  /** @type {object} */
  get cloneScope() {
    throw new Error("Not implemented");
  }

  /** @type {nsIPrincipal} */
  get principal() {
    throw new Error("Not implemented");
  }

  runSafe(callback, ...args) {
    return this.applySafe(callback, args);
  }

  runSafeWithoutClone(callback, ...args) {
    return this.applySafeWithoutClone(callback, args);
  }

  applySafe(callback, args, caller) {
    if (this.unloaded) {
      Cu.reportError("context.runSafe called after context unloaded", caller);
    } else if (!this.active) {
      Cu.reportError(
        "context.runSafe called while context is inactive",
        caller
      );
    } else {
      try {
        let { cloneScope } = this;
        args = args.map(arg => Cu.cloneInto(arg, cloneScope));
      } catch (e) {
        Cu.reportError(e);
        dump(
          `runSafe failure: cloning into ${
            this.cloneScope
          }: ${e}\n\n${filterStack(Error())}`
        );
      }

      return this.applySafeWithoutClone(callback, args, caller);
    }
  }

  applySafeWithoutClone(callback, args, caller) {
    if (this.unloaded) {
      Cu.reportError(
        "context.runSafeWithoutClone called after context unloaded",
        caller
      );
    } else if (!this.active) {
      Cu.reportError(
        "context.runSafeWithoutClone called while context is inactive",
        caller
      );
    } else {
      try {
        return Reflect.apply(callback, null, args);
      } catch (e) {
        // An extension listener may as well be throwing an object that isn't
        // an instance of Error, in that case we have to use fallbacks for the
        // error message, fileName, lineNumber and columnNumber properties.
        const isError = e instanceof this.Error;
        let message;
        let fileName;
        let lineNumber;
        let columnNumber;

        if (isError) {
          message = `${e.name}: ${e.message}`;
          lineNumber = e.lineNumber;
          columnNumber = e.columnNumber;
          fileName = e.fileName;
        } else {
          message = `uncaught exception: ${e}`;

          try {
            // TODO(Bug 1810582): the following fallback logic may go away once
            // we introduced a better way to capture and log the exception in
            // the right window and in all cases (included when the extension
            // code is raising undefined or an object that isn't an instance of
            // the Error constructor).
            //
            // Fallbacks for the error location:
            // - the callback location if it is registered directly from the
            //   extension code (and not wrapped by the child/ext-APINAMe.js
            //   implementation, like e.g. browser.storage, browser.devtools.network
            //   are doing and browser.menus).
            // - if the location of the extension callback is not directly
            //   available (e.g. browser.storage onChanged events, and similarly
            //   for browser.devtools.network and browser.menus events):
            //   - the extension page url if the context is an extension page
            //   - the extension base url if the context is a content script
            const cbLoc = Cu.getFunctionSourceLocation(callback);
            fileName = cbLoc.filename;
            lineNumber = cbLoc.lineNumber ?? lineNumber;

            const extBaseUrl = this.extension.baseURI.resolve("/");
            if (fileName.startsWith(extBaseUrl)) {
              fileName = cbLoc.filename;
              lineNumber = cbLoc.lineNumber ?? lineNumber;
            } else {
              fileName = this.contentWindow?.location?.href;
              if (!fileName || !fileName.startsWith(extBaseUrl)) {
                fileName = extBaseUrl;
              }
            }
          } catch {
            // Ignore errors on retrieving the callback source location.
          }
        }

        dump(
          `Extension error: ${message} ${fileName} ${lineNumber}\n[[Exception stack\n${
            isError ? filterStack(e) : undefined
          }Current stack\n${filterStack(Error())}]]\n`
        );

        // If the error is coming from an extension context associated
        // to a window (e.g. an extension page or extension content script).
        //
        // TODO(Bug 1810574): for the background service worker we will need to do
        // something similar, but not tied to the innerWindowID because there
        // wouldn't be one set for extension contexts related to the
        // background service worker.
        //
        // TODO(Bug 1810582): change the error associated to the innerWindowID to also
        // include a full stack from the original error.
        if (!this.isProxyContextParent && this.contentWindow) {
          this.logConsoleScriptError({
            message,
            fileName,
            lineNumber,
            columnNumber,
          });
        }
        // Also report the original error object (because it also includes
        // the full error stack).
        Cu.reportError(e);
      }
    }
  }

  logConsoleScriptError({
    message,
    fileName,
    lineNumber,
    columnNumber,
    flags = Ci.nsIScriptError.errorFlag,
    innerWindowID = this.innerWindowID,
  }) {
    if (innerWindowID) {
      Services.console.logMessage(
        new ScriptError(
          message,
          fileName,
          lineNumber,
          columnNumber,
          flags,
          "content javascript",
          innerWindowID
        )
      );
    } else {
      Cu.reportError(new Error(message));
    }
  }

  checkLoadURL(url, options = {}) {
    // As an optimization, f the URL starts with the extension's base URL,
    // don't do any further checks. It's always allowed to load it.
    if (url.startsWith(this.extension.baseURL)) {
      return true;
    }

    return checkLoadURL(url, this.principal, options);
  }

  /**
   * Safely call JSON.stringify() on an object that comes from an
   * extension.
   *
   * @param {[any, callback?, number?]} args for JSON.stringify()
   * @returns {string} The stringified representation of obj
   */
  jsonStringify(...args) {
    if (!this.jsonSandbox) {
      this.jsonSandbox = Cu.Sandbox(this.principal, {
        sameZoneAs: this.cloneScope,
        wantXrays: false,
      });
    }

    return Cu.waiveXrays(this.jsonSandbox.JSON).stringify(...args);
  }

  callOnClose(obj) {
    this.onClose.add(obj);
  }

  forgetOnClose(obj) {
    this.onClose.delete(obj);
  }

  get lastError() {
    this.checkedLastError = true;
    return this._lastError;
  }

  set lastError(val) {
    this.checkedLastError = false;
    this._lastError = val;
  }

  /**
   * Normalizes the given error object for use by the target scope. If
   * the target is an error object which belongs to that scope, it is
   * returned as-is. If it is an ordinary object with a `message`
   * property, it is converted into an error belonging to the target
   * scope. If it is an Error object which does *not* belong to the
   * clone scope, it is reported, and converted to an unexpected
   * exception error.
   *
   * @param {Error|object} error
   * @param {nsIStackFrame?} [caller]
   * @returns {Error}
   */
  normalizeError(error, caller) {
    if (error instanceof this.Error) {
      return error;
    }
    let message, fileName;
    if (error && typeof error === "object") {
      const isPlain = ChromeUtils.getClassName(error) === "Object";
      if (isPlain && error.mozWebExtLocation) {
        caller = error.mozWebExtLocation;
      }
      if (isPlain && caller && (error.mozWebExtLocation || !error.fileName)) {
        caller = Cu.cloneInto(caller, this.cloneScope);
        return ChromeUtils.createError(error.message, caller);
      }

      if (
        isPlain ||
        error instanceof ExtensionError ||
        this.principal.subsumes(Cu.getObjectPrincipal(error))
      ) {
        message = error.message;
        fileName = error.fileName;
      }
    }

    if (!message) {
      Cu.reportError(error);
      message = "An unexpected error occurred";
    }
    return new this.Error(message, fileName);
  }

  /**
   * Sets the value of `.lastError` to `error`, calls the given
   * callback, and reports an error if the value has not been checked
   * when the callback returns.
   *
   * @param {object} error An object with a `message` property. May
   *     optionally be an `Error` object belonging to the target scope.
   * @param {nsIStackFrame?} caller
   *        The optional caller frame which triggered this callback, to be used
   *        in error reporting.
   * @param {Function} callback The callback to call.
   * @returns {*} The return value of callback.
   */
  withLastError(error, caller, callback) {
    this.lastError = this.normalizeError(error);
    try {
      return callback();
    } finally {
      if (!this.checkedLastError) {
        Cu.reportError(`Unchecked lastError value: ${this.lastError}`, caller);
      }
      this.lastError = null;
    }
  }

  /**
   * Captures the most recent stack frame which belongs to the extension.
   *
   * @returns {nsIStackFrame?}
   */
  getCaller() {
    return ChromeUtils.getCallerLocation(this.principal);
  }

  /**
   * Wraps the given promise so it can be safely returned to extension
   * code in this context.
   *
   * If `callback` is provided, however, it is used as a completion
   * function for the promise, and no promise is returned. In this case,
   * the callback is called when the promise resolves or rejects. In the
   * latter case, `lastError` is set to the rejection value, and the
   * callback function must check `browser.runtime.lastError` or
   * `extension.runtime.lastError` in order to prevent it being reported
   * to the console.
   *
   * @param {Promise} promise The promise with which to wrap the
   *     callback. May resolve to a `SpreadArgs` instance, in which case
   *     each element will be used as a separate argument.
   *
   *     Unless the promise object belongs to the cloneScope global, its
   *     resolution value is cloned into cloneScope prior to calling the
   *     `callback` function or resolving the wrapped promise.
   *
   * @param {Function} [callback] The callback function to wrap
   *
   * @returns {Promise|undefined} If callback is null, a promise object
   *     belonging to the target scope. Otherwise, undefined.
   */
  wrapPromise(promise, callback = null) {
    let caller = this.getCaller();
    let applySafe = this.applySafe.bind(this);
    if (Cu.getGlobalForObject(promise) === this.cloneScope) {
      applySafe = this.applySafeWithoutClone.bind(this);
    }

    if (callback) {
      promise.then(
        args => {
          if (this.unloaded) {
            Cu.reportError(`Promise resolved after context unloaded\n`, caller);
          } else if (!this.active) {
            Cu.reportError(
              `Promise resolved while context is inactive\n`,
              caller
            );
          } else if (args instanceof NoCloneSpreadArgs) {
            this.applySafeWithoutClone(callback, args.unwrappedValues, caller);
          } else if (args instanceof SpreadArgs) {
            applySafe(callback, args, caller);
          } else {
            applySafe(callback, [args], caller);
          }
        },
        error => {
          this.withLastError(error, caller, () => {
            if (this.unloaded) {
              Cu.reportError(
                `Promise rejected after context unloaded\n`,
                caller
              );
            } else if (!this.active) {
              Cu.reportError(
                `Promise rejected while context is inactive\n`,
                caller
              );
            } else {
              this.applySafeWithoutClone(callback, [], caller);
            }
          });
        }
      );
    } else {
      return new this.Promise((resolve, reject) => {
        promise.then(
          value => {
            if (this.unloaded) {
              Cu.reportError(
                `Promise resolved after context unloaded\n`,
                caller
              );
            } else if (!this.active) {
              Cu.reportError(
                `Promise resolved while context is inactive\n`,
                caller
              );
            } else if (value instanceof NoCloneSpreadArgs) {
              let values = value.unwrappedValues;
              this.applySafeWithoutClone(
                resolve,
                values.length == 1 ? [values[0]] : [values],
                caller
              );
            } else if (value instanceof SpreadArgs) {
              applySafe(resolve, value.length == 1 ? value : [value], caller);
            } else {
              applySafe(resolve, [value], caller);
            }
          },
          value => {
            if (this.unloaded) {
              Cu.reportError(
                `Promise rejected after context unloaded: ${
                  value && value.message
                }\n`,
                caller
              );
            } else if (!this.active) {
              Cu.reportError(
                `Promise rejected while context is inactive: ${
                  value && value.message
                }\n`,
                caller
              );
            } else {
              this.applySafeWithoutClone(
                reject,
                [this.normalizeError(value, caller)],
                caller
              );
            }
          }
        );
      });
    }
  }

  unload() {
    this.unloaded = true;

    for (let obj of this.onClose) {
      obj.close();
    }
    this.onClose.clear();
  }

  /**
   * A simple proxy for unload(), for use with callOnClose().
   */
  close() {
    this.unload();
  }
}

/**
 * An object that runs the implementation of a schema API. Instantiations of
 * this interfaces are used by Schemas.sys.mjs.
 *
 * @interface
 */
export class SchemaAPIInterface {
  /**
   * Calls this as a function that returns its return value.
   *
   * @abstract
   * @param {Array} _args The parameters for the function.
   * @returns {*} The return value of the invoked function.
   */
  callFunction(_args) {
    throw new Error("Not implemented");
  }

  /**
   * Calls this as a function and ignores its return value.
   *
   * @abstract
   * @param {Array} _args The parameters for the function.
   */
  callFunctionNoReturn(_args) {
    throw new Error("Not implemented");
  }

  /**
   * Calls this as a function that completes asynchronously.
   *
   * @abstract
   * @param {Array} _args The parameters for the function.
   * @param {callback} [_callback] The callback to be called when the function
   *     completes.
   * @param {boolean} [_requireUserInput=false] If true, the function should
   *                  fail if the browser is not currently handling user input.
   * @returns {Promise|undefined} Must be void if `callback` is set, and a
   *     promise otherwise. The promise is resolved when the function completes.
   */
  callAsyncFunction(_args, _callback, _requireUserInput) {
    throw new Error("Not implemented");
  }

  /**
   * Retrieves the value of this as a property.
   *
   * @abstract
   * @returns {*} The value of the property.
   */
  getProperty() {
    throw new Error("Not implemented");
  }

  /**
   * Assigns the value to this as property.
   *
   * @abstract
   * @param {string} _value The new value of the property.
   */
  setProperty(_value) {
    throw new Error("Not implemented");
  }

  /**
   * Registers a `listener` to this as an event.
   *
   * @abstract
   * @param {Function} _listener The callback to be called when the event fires.
   * @param {Array} _args Extra parameters for EventManager.addListener.
   * @see EventManager.addListener
   */
  addListener(_listener, _args) {
    throw new Error("Not implemented");
  }

  /**
   * Checks whether `listener` is listening to this as an event.
   *
   * @abstract
   * @param {Function} _listener The event listener.
   * @returns {boolean} Whether `listener` is registered with this as an event.
   * @see EventManager.hasListener
   */
  hasListener(_listener) {
    throw new Error("Not implemented");
  }

  /**
   * Unregisters `listener` from this as an event.
   *
   * @abstract
   * @param {Function} _listener The event listener.
   * @see EventManager.removeListener
   */
  removeListener(_listener) {
    throw new Error("Not implemented");
  }

  /**
   * Revokes the implementation object, and prevents any further method
   * calls from having external effects.
   *
   * @abstract
   */
  revoke() {
    throw new Error("Not implemented");
  }
}

/**
 * An object that runs a locally implemented API.
 */
class LocalAPIImplementation extends SchemaAPIInterface {
  /**
   * Constructs an implementation of the `name` method or property of `pathObj`.
   *
   * @param {object} pathObj The object containing the member with name `name`.
   * @param {string} name The name of the implemented member.
   * @param {BaseContext} context The context in which the schema is injected.
   */
  constructor(pathObj, name, context) {
    super();
    this.pathObj = pathObj;
    this.name = name;
    this.context = context;
  }

  revoke() {
    if (this.pathObj[this.name][lazy.Schemas.REVOKE]) {
      this.pathObj[this.name][lazy.Schemas.REVOKE]();
    }

    this.pathObj = null;
    this.name = null;
    this.context = null;
  }

  callFunction(args) {
    try {
      return this.pathObj[this.name](...args);
    } catch (e) {
      throw this.context.normalizeError(e);
    }
  }

  callFunctionNoReturn(args) {
    try {
      this.pathObj[this.name](...args);
    } catch (e) {
      throw this.context.normalizeError(e);
    }
  }

  callAsyncFunction(args, callback, requireUserInput) {
    let promise;
    try {
      if (requireUserInput) {
        if (!this.context.contentWindow.windowUtils.isHandlingUserInput) {
          throw new ExtensionError(
            `${this.name} may only be called from a user input handler`
          );
        }
      }
      promise = this.pathObj[this.name](...args) || Promise.resolve();
    } catch (e) {
      promise = Promise.reject(e);
    }
    return this.context.wrapPromise(promise, callback);
  }

  getProperty() {
    return this.pathObj[this.name];
  }

  setProperty(value) {
    this.pathObj[this.name] = value;
  }

  addListener(listener, args) {
    try {
      this.pathObj[this.name].addListener.call(null, listener, ...args);
    } catch (e) {
      throw this.context.normalizeError(e);
    }
  }

  hasListener(listener) {
    return this.pathObj[this.name].hasListener.call(null, listener);
  }

  removeListener(listener) {
    this.pathObj[this.name].removeListener.call(null, listener);
  }
}

// Recursively copy properties from source to dest.
function deepCopy(dest, source) {
  for (let prop in source) {
    let desc = Object.getOwnPropertyDescriptor(source, prop);
    if (typeof desc.value == "object") {
      if (!(prop in dest)) {
        dest[prop] = {};
      }
      deepCopy(dest[prop], source[prop]);
    } else {
      Object.defineProperty(dest, prop, desc);
    }
  }
}

function getChild(map, key) {
  let child = map.children.get(key);
  if (!child) {
    child = {
      modules: new Set(),
      children: new Map(),
    };

    map.children.set(key, child);
  }
  return child;
}

function getPath(map, path) {
  for (let key of path) {
    map = getChild(map, key);
  }
  return map;
}

function mergePaths(dest, source) {
  for (let name of source.modules) {
    dest.modules.add(name);
  }

  for (let [name, child] of source.children.entries()) {
    mergePaths(getChild(dest, name), child);
  }
}

/**
 * Manages loading and accessing a set of APIs for a specific extension
 * context.
 *
 * @param {BaseContext} context
 *        The context to manage APIs for.
 * @param {SchemaAPIManager} apiManager
 *        The API manager holding the APIs to manage.
 * @param {object} root
 *        The root object into which APIs will be injected.
 */
class CanOfAPIs {
  constructor(context, apiManager, root) {
    this.context = context;
    this.scopeName = context.envType;
    this.apiManager = apiManager;
    this.root = root;

    this.apiPaths = new Map();

    this.apis = new Map();
  }

  /**
   * Synchronously loads and initializes an ExtensionAPI instance.
   *
   * @param {string} name
   *        The name of the API to load.
   */
  loadAPI(name) {
    if (this.apis.has(name)) {
      return;
    }

    let { extension } = this.context;

    let api = this.apiManager.getAPI(name, extension, this.scopeName);
    if (!api) {
      return;
    }

    this.apis.set(name, api);

    deepCopy(this.root, api.getAPI(this.context));
  }

  /**
   * Asynchronously loads and initializes an ExtensionAPI instance.
   *
   * @param {string} name
   *        The name of the API to load.
   */
  async asyncLoadAPI(name) {
    if (this.apis.has(name)) {
      return;
    }

    let { extension } = this.context;
    if (!lazy.Schemas.checkPermissions(name, extension)) {
      return;
    }

    let api = await this.apiManager.asyncGetAPI(
      name,
      extension,
      this.scopeName
    );
    // Check again, because async;
    if (this.apis.has(name)) {
      return;
    }

    this.apis.set(name, api);

    deepCopy(this.root, api.getAPI(this.context));
  }

  /**
   * Finds the API at the given path from the root object, and
   * synchronously loads the API that implements it if it has not
   * already been loaded.
   *
   * @param {string} path
   *        The "."-separated path to find.
   * @returns {*}
   */
  findAPIPath(path) {
    if (this.apiPaths.has(path)) {
      return this.apiPaths.get(path);
    }

    let obj = this.root;
    let modules = this.apiManager.modulePaths;

    let parts = path.split(".");
    for (let [i, key] of parts.entries()) {
      if (!obj) {
        return;
      }
      modules = getChild(modules, key);

      for (let name of modules.modules) {
        if (!this.apis.has(name)) {
          this.loadAPI(name);
        }
      }

      if (!(key in obj) && i < parts.length - 1) {
        obj[key] = {};
      }
      obj = obj[key];
    }

    this.apiPaths.set(path, obj);
    return obj;
  }

  /**
   * Finds the API at the given path from the root object, and
   * asynchronously loads the API that implements it if it has not
   * already been loaded.
   *
   * @param {string} path
   *        The "."-separated path to find.
   * @returns {Promise<*>}
   */
  async asyncFindAPIPath(path) {
    if (this.apiPaths.has(path)) {
      return this.apiPaths.get(path);
    }

    let obj = this.root;
    let modules = this.apiManager.modulePaths;

    let parts = path.split(".");
    for (let [i, key] of parts.entries()) {
      if (!obj) {
        return;
      }
      modules = getChild(modules, key);

      for (let name of modules.modules) {
        if (!this.apis.has(name)) {
          await this.asyncLoadAPI(name);
        }
      }

      if (!(key in obj) && i < parts.length - 1) {
        obj[key] = {};
      }

      if (typeof obj[key] === "function") {
        obj = obj[key].bind(obj);
      } else {
        obj = obj[key];
      }
    }

    this.apiPaths.set(path, obj);
    return obj;
  }
}

/**
 * @class APIModule
 * @abstract
 *
 * @property {string} url
 *       The URL of the script which contains the module's
 *       implementation. This script must define a global property
 *       matching the modules name, which must be a class constructor
 *       which inherits from {@link ExtensionAPI}.
 *
 * @property {string} schema
 *       The URL of the JSON schema which describes the module's API.
 *
 * @property {Array<string>} scopes
 *       The list of scope names into which the API may be loaded.
 *
 * @property {Array<string>} manifest
 *       The list of top-level manifest properties which will trigger
 *       the module to be loaded, and its `onManifestEntry` method to be
 *       called.
 *
 * @property {Array<string>} events
 *       The list events which will trigger the module to be loaded, and
 *       its appropriate event handler method to be called. Currently
 *       only accepts "startup".
 *
 * @property {Array<string>} permissions
 *       An optional list of permissions, any of which must be present
 *       in order for the module to load.
 *
 * @property {Array<Array<string>>} paths
 *       A list of paths from the root API object which, when accessed,
 *       will cause the API module to be instantiated and injected.
 */

/**
 * This object loads the ext-*.js scripts that define the extension API.
 *
 * This class instance is shared with the scripts that it loads, so that the
 * ext-*.js scripts and the instantiator can communicate with each other.
 */
class SchemaAPIManager extends EventEmitter {
  /**
   * @param {string} processType
   *     "main" - The main, one and only chrome browser process.
   *     "addon" - An addon process.
   *     "content" - A content process.
   *     "devtools" - A devtools process.
   * @param {import("Schemas.sys.mjs").SchemaInject} [schema]
   */
  constructor(processType, schema) {
    super();
    this.processType = processType;
    this.global = null;
    if (schema) {
      this.schema = schema;
    }

    this.modules = new Map();
    this.modulePaths = { children: new Map(), modules: new Set() };
    this.manifestKeys = new Map();
    this.eventModules = new DefaultMap(() => new Set());
    this.settingsModules = new Set();

    this._modulesJSONLoaded = false;

    this.schemaURLs = new Map();

    this.apis = new DefaultWeakMap(() => new Map());

    this._scriptScopes = [];
  }

  onStartup(extension) {
    let promises = [];
    for (let apiName of this.eventModules.get("startup")) {
      promises.push(
        extension.apiManager.asyncGetAPI(apiName, extension).then(api => {
          if (api) {
            api.onStartup();
          }
        })
      );
    }

    return Promise.all(promises);
  }

  async loadModuleJSON(urls) {
    let promises = urls.map(url => fetch(url).then(resp => resp.json()));

    return this.initModuleJSON(await Promise.all(promises));
  }

  initModuleJSON(blobs) {
    for (let json of blobs) {
      this.registerModules(json);
    }

    this._modulesJSONLoaded = true;

    return new StructuredCloneHolder("SchemaAPIManager/initModuleJSON", null, {
      modules: this.modules,
      modulePaths: this.modulePaths,
      manifestKeys: this.manifestKeys,
      eventModules: this.eventModules,
      settingsModules: this.settingsModules,
      schemaURLs: this.schemaURLs,
    });
  }

  initModuleData(moduleData) {
    if (!this._modulesJSONLoaded) {
      let data = moduleData.deserialize({}, true);

      this.modules = data.modules;
      this.modulePaths = data.modulePaths;
      this.manifestKeys = data.manifestKeys;
      this.eventModules = new DefaultMap(() => new Set(), data.eventModules);
      this.settingsModules = new Set(data.settingsModules);
      this.schemaURLs = data.schemaURLs;
    }

    this._modulesJSONLoaded = true;
  }

  /**
   * Registers a set of ExtensionAPI modules to be lazily loaded and
   * managed by this manager.
   *
   * @param {object} obj
   *        An object containing property for eacy API module to be
   *        registered. Each value should be an object implementing the
   *        APIModule interface.
   */
  registerModules(obj) {
    for (let [name, details] of Object.entries(obj)) {
      details.namespaceName = name;

      if (this.modules.has(name)) {
        throw new Error(`Module '${name}' already registered`);
      }
      this.modules.set(name, details);

      if (details.schema) {
        let content =
          details.scopes &&
          (details.scopes.includes("content_parent") ||
            details.scopes.includes("content_child"));
        this.schemaURLs.set(details.schema, { content });
      }

      for (let event of details.events || []) {
        this.eventModules.get(event).add(name);
      }

      if (details.settings) {
        this.settingsModules.add(name);
      }

      for (let key of details.manifest || []) {
        if (this.manifestKeys.has(key)) {
          throw new Error(
            `Manifest key '${key}' already registered by '${this.manifestKeys.get(
              key
            )}'`
          );
        }

        this.manifestKeys.set(key, name);
      }

      for (let path of details.paths || []) {
        getPath(this.modulePaths, path).modules.add(name);
      }
    }
  }

  /**
   * Emits an `onManifestEntry` event for the top-level manifest entry
   * on all relevant {@link ExtensionAPI} instances for the given
   * extension.
   *
   * The API modules will be synchronously loaded if they have not been
   * loaded already.
   *
   * @param {Extension} extension
   *        The extension for which to emit the events.
   * @param {string} entry
   *        The name of the top-level manifest entry.
   *
   * @returns {*}
   */
  emitManifestEntry(extension, entry) {
    let apiName = this.manifestKeys.get(entry);
    if (apiName) {
      let api = extension.apiManager.getAPI(apiName, extension);
      return api.onManifestEntry(entry);
    }
  }
  /**
   * Emits an `onManifestEntry` event for the top-level manifest entry
   * on all relevant {@link ExtensionAPI} instances for the given
   * extension.
   *
   * The API modules will be asynchronously loaded if they have not been
   * loaded already.
   *
   * @param {Extension} extension
   *        The extension for which to emit the events.
   * @param {string} entry
   *        The name of the top-level manifest entry.
   *
   * @returns {Promise<*>}
   */
  async asyncEmitManifestEntry(extension, entry) {
    let apiName = this.manifestKeys.get(entry);
    if (apiName) {
      let api = await extension.apiManager.asyncGetAPI(apiName, extension);
      return api.onManifestEntry(entry);
    }
  }

  /**
   * Returns the {@link ExtensionAPI} instance for the given API module,
   * for the given extension, in the given scope, synchronously loading
   * and instantiating it if necessary.
   *
   * @param {string} name
   *        The name of the API module to load.
   * @param {Extension} extension
   *        The extension for which to load the API.
   * @param {string} [scope = null]
   *        The scope type for which to retrieve the API, or null if not
   *        being retrieved for a particular scope.
   *
   * @returns {ExtensionAPI?}
   */
  getAPI(name, extension, scope = null) {
    if (!this._checkGetAPI(name, extension, scope)) {
      return;
    }

    let apis = this.apis.get(extension);
    if (apis.has(name)) {
      return apis.get(name);
    }

    let module = this.loadModule(name);

    let api = new module(extension);
    apis.set(name, api);
    return api;
  }
  /**
   * Returns the {@link ExtensionAPI} instance for the given API module,
   * for the given extension, in the given scope, asynchronously loading
   * and instantiating it if necessary.
   *
   * @param {string} name
   *        The name of the API module to load.
   * @param {Extension} extension
   *        The extension for which to load the API.
   * @param {string} [scope = null]
   *        The scope type for which to retrieve the API, or null if not
   *        being retrieved for a particular scope.
   *
   * @returns {Promise<ExtensionAPI>?}
   */
  async asyncGetAPI(name, extension, scope = null) {
    if (!this._checkGetAPI(name, extension, scope)) {
      return;
    }

    let apis = this.apis.get(extension);
    if (apis.has(name)) {
      return apis.get(name);
    }

    let module = await this.asyncLoadModule(name);

    // Check again, because async.
    if (apis.has(name)) {
      return apis.get(name);
    }

    let api = new module(extension);
    apis.set(name, api);
    return api;
  }

  /**
   * Synchronously loads an API module, if not already loaded, and
   * returns its ExtensionAPI constructor.
   *
   * @param {string} name
   *        The name of the module to load.
   * @returns {typeof ExtensionAPI}
   */
  loadModule(name) {
    let module = this.modules.get(name);
    if (module.loaded) {
      return this.global[name];
    }

    this._checkLoadModule(module, name);

    this.initGlobal();

    Services.scriptloader.loadSubScript(module.url, this.global);

    module.loaded = true;

    return this.global[name];
  }
  /**
   * aSynchronously loads an API module, if not already loaded, and
   * returns its ExtensionAPI constructor.
   *
   * @param {string} name
   *        The name of the module to load.
   *
   * @returns {Promise<typeof ExtensionAPI>}
   */
  asyncLoadModule(name) {
    let module = this.modules.get(name);
    if (module.loaded) {
      return Promise.resolve(this.global[name]);
    }
    if (module.asyncLoaded) {
      return module.asyncLoaded;
    }

    this._checkLoadModule(module, name);

    module.asyncLoaded = ChromeUtils.compileScript(module.url).then(script => {
      // In some rare cases, loadModule() may have been called since we started
      // the async compileScript call. In that case, return the result that we
      // already got from loadModule.
      if (!module.loaded) {
        this.initGlobal();
        script.executeInGlobal(this.global);

        module.loaded = true;
      }

      return this.global[name];
    });

    return module.asyncLoaded;
  }

  asyncLoadSettingsModules() {
    return Promise.all(
      Array.from(this.settingsModules).map(apiName =>
        this.asyncLoadModule(apiName)
      )
    );
  }

  getModule(name) {
    return this.modules.get(name);
  }

  /**
   * Checks whether the given API module may be loaded for the given
   * extension, in the given scope.
   *
   * @param {string} name
   *        The name of the API module to check.
   * @param {Extension} extension
   *        The extension for which to check the API.
   * @param {string} [scope = null]
   *        The scope type for which to check the API, or null if not
   *        being checked for a particular scope.
   *
   * @returns {boolean}
   *        Whether the module may be loaded.
   */
  _checkGetAPI(name, extension, scope = null) {
    let module = this.getModule(name);
    if (!module) {
      // A module may not exist for a particular manifest version, but
      // we allow keys in the manifest.  An example is pageAction.
      return false;
    }

    if (
      module.permissions &&
      !module.permissions.some(perm => extension.hasPermission(perm))
    ) {
      return false;
    }

    if (!scope) {
      return true;
    }

    if (!module.scopes.includes(scope)) {
      return false;
    }

    if (!lazy.Schemas.checkPermissions(module.namespaceName, extension)) {
      return false;
    }

    return true;
  }

  _checkLoadModule(module, name) {
    if (!module) {
      throw new Error(`Module '${name}' does not exist`);
    }
    if (this.global && this.global[name]) {
      throw new Error(
        `Module '${name}' conflicts with existing global property`
      );
    }
  }

  /**
   * Create a global object that is used as the shared global for all ext-*.js
   * scripts that are loaded via `loadScript`.
   *
   * @returns {object} A sandbox that is used as the global by `loadScript`.
   */
  _createExtGlobal() {
    let global = Cu.Sandbox(
      Services.scriptSecurityManager.getSystemPrincipal(),
      {
        wantXrays: false,
        wantGlobalProperties: ["ChromeUtils"],
        sandboxName: `Namespace of ext-*.js scripts for ${this.processType} (from: resource://gre/modules/ExtensionCommon.sys.mjs)`,
      }
    );

    Object.assign(global, {
      AppConstants,
      Cc,
      ChromeWorker,
      Ci,
      Cr,
      Cu,
      ExtensionAPI,
      ExtensionAPIPersistent,
      ExtensionCommon,
      FileReader,
      Glean,
      GleanPings,
      IOUtils,
      L10nFileSource,
      L10nRegistry,
      MatchGlob,
      MatchPattern,
      MatchPatternSet,
      OffscreenCanvas,
      PathUtils,
      Services,
      StructuredCloneHolder,
      WebExtensionPolicy,
      XPCOMUtils,
      extensions: this,
      global,
    });

    ChromeUtils.defineLazyGetter(global, "console", getConsole);
    // eslint-disable-next-line mozilla/lazy-getter-object-name
    ChromeUtils.defineESModuleGetters(global, {
      ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
    });

    return global;
  }

  initGlobal() {
    if (!this.global) {
      this.global = this._createExtGlobal();
    }
  }

  /**
   * Load an ext-*.js script. The script runs in its own scope, if it wishes to
   * share state with another script it can assign to the `global` variable. If
   * it wishes to communicate with this API manager, use `extensions`.
   *
   * @param {string} scriptUrl The URL of the ext-*.js script.
   */
  loadScript(scriptUrl) {
    // Create the object in the context of the sandbox so that the script runs
    // in the sandbox's context instead of here.
    let scope = Cu.createObjectIn(this.global);

    Services.scriptloader.loadSubScript(scriptUrl, scope);

    // Save the scope to avoid it being garbage collected.
    this._scriptScopes.push(scope);
  }
}

class LazyAPIManager extends SchemaAPIManager {
  constructor(processType, moduleData, schemaURLs) {
    super(processType);

    /** @type {Promise | boolean} */
    this.initialized = false;

    this.initModuleData(moduleData);

    this.schemaURLs = schemaURLs;
  }

  lazyInit() {}
}

defineLazyGetter(LazyAPIManager.prototype, "schema", function () {
  let root = new lazy.SchemaRoot(lazy.Schemas.rootSchema, this.schemaURLs);
  root.parseSchemas();
  return root;
});

class MultiAPIManager extends SchemaAPIManager {
  constructor(processType, children) {
    super(processType);

    this.initialized = false;

    this.children = children;
  }

  async lazyInit() {
    if (!this.initialized) {
      this.initialized = true;

      for (let child of this.children) {
        if (child.lazyInit) {
          let res = child.lazyInit();
          if (res && typeof res.then === "function") {
            await res;
          }
        }

        mergePaths(this.modulePaths, child.modulePaths);
      }
    }
  }

  onStartup(extension) {
    return Promise.all(this.children.map(child => child.onStartup(extension)));
  }

  getModule(name) {
    for (let child of this.children) {
      if (child.modules.has(name)) {
        return child.modules.get(name);
      }
    }
  }

  loadModule(name) {
    for (let child of this.children) {
      if (child.modules.has(name)) {
        return child.loadModule(name);
      }
    }
  }

  asyncLoadModule(name) {
    for (let child of this.children) {
      if (child.modules.has(name)) {
        return child.asyncLoadModule(name);
      }
    }
  }
}

defineLazyGetter(MultiAPIManager.prototype, "schema", function () {
  let bases = this.children.map(child => child.schema);

  // All API manager schema roots should derive from the global schema root,
  // so it doesn't need its own entry.
  if (bases[bases.length - 1] === lazy.Schemas) {
    bases.pop();
  }

  if (bases.length === 1) {
    bases = bases[0];
  }
  return new lazy.SchemaRoot(bases, new Map());
});

export function LocaleData(data) {
  this.defaultLocale = data.defaultLocale;
  this.selectedLocale = data.selectedLocale;
  this.locales = data.locales || new Map();
  this.warnedMissingKeys = new Set();

  /**
   * Map(locale-name -> Map(message-key -> localized-string))
   *
   * Contains a key for each loaded locale, each of which is a
   * Map of message keys to their localized strings.
   *
   * @type {Map<string, Map<string, string>>}
   */
  this.messages = data.messages || new Map();

  if (data.builtinMessages) {
    this.messages.set(this.BUILTIN, data.builtinMessages);
  }
}

LocaleData.listLocaleVariations = function LocaleData_listLocaleVariations(
  locale
) {
  const subTags = locale.split("-");
  const result = [];
  while (subTags.length) {
    result.push(subTags.join("-"));
    subTags.pop();
  }
  return result;
};

LocaleData.prototype = {
  // Representation of the object to send to content processes. This
  // should include anything the content process might need.
  serialize() {
    return {
      defaultLocale: this.defaultLocale,
      selectedLocale: this.selectedLocale,
      messages: this.messages,
      locales: this.locales,
    };
  },

  BUILTIN: "@@BUILTIN_MESSAGES",

  has(locale) {
    return this.messages.has(locale);
  },

  getAvailableLocales(preferredLocale) {
    const locales = [this.BUILTIN];

    if (preferredLocale) {
      locales.push(...LocaleData.listLocaleVariations(preferredLocale));
    }

    if (!locales.includes(this.defaultLocale)) {
      locales.push(this.defaultLocale);
    }

    return locales.filter(locale => this.messages.has(locale));
  },

  // https://developer.chrome.com/extensions/i18n
  localizeMessage(message, substitutions = [], options = {}) {
    const defaultOptions = {
      defaultValue: "",
      cloneScope: null,
    };

    const locales = options.locale
      ? this.getAvailableLocales(options.locale)
      : this.availableLocales;

    options = Object.assign(defaultOptions, options);

    // Message names are case-insensitive, so normalize them to lower-case.
    message = message.toLowerCase();
    for (let locale of locales) {
      let messages = this.messages.get(locale);
      if (messages.has(message)) {
        let str = messages.get(message);

        if (!str.includes("$")) {
          return str;
        }

        if (!Array.isArray(substitutions)) {
          substitutions = [substitutions];
        }

        let replacer = (matched, index, dollarSigns) => {
          if (index) {
            // This is not quite Chrome-compatible. Chrome consumes any number
            // of digits following the $, but only accepts 9 substitutions. We
            // accept any number of substitutions.
            index = parseInt(index, 10) - 1;
            return index in substitutions ? substitutions[index] : "";
          }
          // For any series of contiguous `$`s, the first is dropped, and
          // the rest remain in the output string.
          return dollarSigns;
        };
        return str.replace(/\$(?:([1-9]\d*)|(\$+))/g, replacer);
      }
    }

    // Check for certain pre-defined messages.
    if (message == "@@ui_locale") {
      return this.uiLocale;
    } else if (message.startsWith("@@bidi_")) {
      let rtl = Services.locale.isAppLocaleRTL;

      if (message == "@@bidi_dir") {
        return rtl ? "rtl" : "ltr";
      } else if (message == "@@bidi_reversed_dir") {
        return rtl ? "ltr" : "rtl";
      } else if (message == "@@bidi_start_edge") {
        return rtl ? "right" : "left";
      } else if (message == "@@bidi_end_edge") {
        return rtl ? "left" : "right";
      }
    }

    if (!this.warnedMissingKeys.has(message)) {
      let error = `Unknown localization message ${message}`;
      if (options.cloneScope) {
        error = new options.cloneScope.Error(error);
      }
      Cu.reportError(error);
      this.warnedMissingKeys.add(message);
    }
    return options.defaultValue;
  },

  // Localize a string, replacing all |__MSG_(.*)__| tokens with the
  // matching string from the current locale, as determined by
  // |this.selectedLocale|.
  //
  // This may not be called before calling either |initLocale| or
  // |initAllLocales|.
  localize(str, locale = this.selectedLocale) {
    if (!str) {
      return str;
    }

    return str.replace(/__MSG_([A-Za-z0-9@_]+?)__/g, (matched, message) => {
      return this.localizeMessage(message, [], {
        locale,
        defaultValue: matched,
      });
    });
  },

  // Validates the contents of a locale JSON file, normalizes the
  // messages into a Map of message key -> localized string pairs.
  addLocale(locale, messages, extension) {
    let result = new Map();

    let isPlainObject = obj =>
      obj &&
      typeof obj === "object" &&
      ChromeUtils.getClassName(obj) === "Object";

    // Chrome does not document the semantics of its localization
    // system very well. It handles replacements by pre-processing
    // messages, replacing |$[a-zA-Z0-9@_]+$| tokens with the value of their
    // replacements. Later, it processes the resulting string for
    // |$[0-9]| replacements.
    //
    // Again, it does not document this, but it accepts any number
    // of sequential |$|s, and replaces them with that number minus
    // 1. It also accepts |$| followed by any number of sequential
    // digits, but refuses to process a localized string which
    // provides more than 9 substitutions.
    if (!isPlainObject(messages)) {
      extension.packagingError(`Invalid locale data for ${locale}`);
      return result;
    }

    for (let key of Object.keys(messages)) {
      let msg = messages[key];

      if (!isPlainObject(msg) || typeof msg.message != "string") {
        extension.packagingError(
          `Invalid locale message data for ${locale}, message ${JSON.stringify(
            key
          )}`
        );
        continue;
      }

      // Substitutions are case-insensitive, so normalize all of their names
      // to lower-case.
      let placeholders = new Map();
      if ("placeholders" in msg && isPlainObject(msg.placeholders)) {
        for (let key of Object.keys(msg.placeholders)) {
          placeholders.set(key.toLowerCase(), msg.placeholders[key]);
        }
      }

      let replacer = (match, name) => {
        let replacement = placeholders.get(name.toLowerCase());
        if (isPlainObject(replacement) && "content" in replacement) {
          return replacement.content;
        }
        return "";
      };

      let value = msg.message.replace(/\$([A-Za-z0-9@_]+)\$/g, replacer);

      // Message names are also case-insensitive, so normalize them to lower-case.
      result.set(key.toLowerCase(), value);
    }

    this.messages.set(locale, result);
    return result;
  },

  get acceptLanguages() {
    let result = Services.prefs.getComplexValue(
      "intl.accept_languages",
      Ci.nsIPrefLocalizedString
    ).data;
    return result.split(/\s*,\s*/g);
  },

  get uiLocale() {
    return Services.locale.appLocaleAsBCP47;
  },

  get availableLocales() {
    let value = this.getAvailableLocales(this.selectedLocale);
    return redefineGetter(this, "availableLocales", value);
  },
};

/**
 * This is a generic class for managing event listeners.
 *
 * @example
 * new EventManager({
 *   context,
 *   name: "api.subAPI",
 *   register:  fire => {
 *     let listener = (...) => {
 *       // Fire any listeners registered with addListener.
 *       fire.async(arg1, arg2);
 *     };
 *     // Register the listener.
 *     SomehowRegisterListener(listener);
 *     return () => {
 *       // Return a way to unregister the listener.
 *       SomehowUnregisterListener(listener);
 *     };
 *   }
 * }).api()
 *
 * The result is an object with addListener, removeListener, and
 * hasListener methods. `context` is an add-on scope (either an
 * ExtensionContext in the chrome process or ExtensionContext in a
 * content process).
 */
class EventManager {
  /*
   * A persistent event must provide module and name.  Additionally the
   * module must implement primeListeners in the ExtensionAPI class.
   *
   * A startup blocking event must also add the startupBlocking flag in
   * ext-toolkit.json or ext-browser.json.
   *
   * Listeners synchronously added from a background extension context
   * will be persisted, for a persistent background script only the
   * "startup blocking" events will be persisted.
   *
   * EventManager instances created in a child process can't persist any listener.
   *
   * @param {object} params
   *        Parameters that control this EventManager.
   * @param {BaseContext} params.context
   *        An object representing the extension instance using this event.
   * @param {string} params.module
   *        The API module name, required for persistent events.
   * @param {string} params.event
   *        The API event name, required for persistent events.
   * @param {ExtensionAPI} params.extensionApi
   *        The API intance.  If the API uses the ExtensionAPIPersistent class, some simplification is
   *        possible by passing the api (self or this) and the internal register function will be used.
   * @param {string} [params.name]
   *        A name used only for debugging.  If not provided, name is built from module and event.
   * @param {functon} params.register
   *        A function called whenever a new listener is added.
   * @param {boolean} [params.inputHandling=false]
   *        If true, the "handling user input" flag is set while handlers
   *        for this event are executing.
   */
  constructor(params) {
    let {
      context,
      module,
      event,
      name,
      register,
      extensionApi,
      inputHandling = false,
      resetIdleOnEvent = true,
    } = params;
    this.context = context;
    this.module = module;
    this.event = event;
    this.name = name;
    this.register = register;
    this.inputHandling = inputHandling;
    this.resetIdleOnEvent = resetIdleOnEvent;

    const isBackgroundParent =
      this.context.envType === "addon_parent" &&
      this.context.isBackgroundContext;

    // TODO(Bug 1844041): ideally we should restrict resetIdleOnEvent to
    // EventManager instances that belongs to the event page, but along
    // with that we should consider if calling sendMessage from an event
    // page should also reset idle timer, and so in the shorter term
    // here we are allowing listeners from other extension pages to
    // also reset the idle timer.
    const isAddonContext = ["addon_parent", "addon_child"].includes(
      this.context.envType
    );

    // Avoid resetIdleOnEvent overhead by only consider it when applicable.
    if (!isAddonContext || context.extension.persistentBackground) {
      this.resetIdleOnEvent = false;
    }

    if (!name) {
      this.name = `${module}.${event}`;
    }

    if (!this.register && extensionApi instanceof ExtensionAPIPersistent) {
      this.register = (fire, ...params) => {
        return extensionApi.registerEventListener(
          { context, event, fire },
          params
        );
      };
    }
    if (!this.register) {
      throw new Error(
        `EventManager requires register method for ${this.name}.`
      );
    }

    this.canPersistEvents = module && event && isBackgroundParent;

    if (this.canPersistEvents) {
      let { extension } = context;
      if (extension.persistentBackground) {
        // Persistent backgrounds will only persist startup blocking APIs.
        let api_module = extension.apiManager.getModule(this.module);
        if (!api_module?.startupBlocking) {
          this.canPersistEvents = false;
        }
      } else {
        // Event pages will persist all APIs that implement primeListener.
        // The api is already loaded so this does not have performance effect.
        let api = extension.apiManager.getAPI(
          this.module,
          extension,
          "addon_parent"
        );

        // If the api doesn't implement primeListener we do not persist the events.
        if (!api?.primeListener) {
          this.canPersistEvents = false;
        }
      }
    }

    this.unregister = new Map();
    this.remove = new Map();
  }

  /*
   * Information about listeners to persistent events is associated with
   * the extension to which they belong.  Any extension thas has such
   * listeners has a property called `persistentListeners` that is a
   * 3-level Map:
   *
   * - the first 2 keys are the module name (e.g., webRequest)
   *   and the name of the event within the module (e.g., onBeforeRequest).
   *
   * - the third level of the map is used to track multiple listeners for
   *   the same event, these listeners are distinguished by the extra arguments
   *   passed to addListener()
   *
   * - for quick lookups, the key to the third Map is the result of calling
   *   uneval() on the array of extra arguments.
   *
   * - the value stored in the Map or persistent listeners we keep in memory
   *   is a plain object with:
   *   - a property called `params` that is the original (ie, not uneval()ed)
   *     extra arguments to addListener()
   *   - and a property called `listeners` that is an array of plain object
   *     each representing a listener to be primed and a `primeId` autoincremented
   *     integer that represents each of the primed listeners that belongs to the
   *     group listeners with the same set of extra params.
   *   - a `nextPrimeId` property keeps track of the numeric primeId that should
   *     be assigned to new persistent listeners added for the same event and
   *     same set of extra params.
   *
   * For a primed listener (i.e., the stub listener created during browser startup
   * before the extension background page is started, and after an event page is
   * suspended on idle), the object will be later populated (by the callers of
   * EventManager.primeListeners) with an additional `primed` property that serves
   * as a placeholder listener, collecting all events that got emitted while the
   * background page was not yet started, and eventually replaced by a callback
   * registered from the extension code, once the background page scripts have been
   * executed (or dropped if the background page scripts do not register the same
   * listener anymore).
   *
   * @param {Extension} extension
   * @returns {boolean} True if the extension had any persistent listeners.
   */
  static _initPersistentListeners(extension) {
    if (extension.persistentListeners) {
      return !!extension.persistentListeners.size;
    }

    let listeners = new DefaultMap(() => new DefaultMap(() => new Map()));
    extension.persistentListeners = listeners;

    let persistentListeners = extension.startupData?.persistentListeners;
    if (!persistentListeners) {
      return false;
    }

    let found = false;
    for (let [module, savedModuleEntry] of Object.entries(
      persistentListeners
    )) {
      for (let [event, savedEventEntry] of Object.entries(savedModuleEntry)) {
        for (let paramList of savedEventEntry) {
          /* Before Bug 1795801 (Firefox < 113) each entry was related to a listener
           * registered with a different set of extra params (and so only one listener
           * could be persisted for the same set of extra params)
           *
           * After Bug 1795801 (Firefox >= 113) each entry still represents a listener
           * registered for that event, but multiple listeners registered with the same
           * set of extra params will be captured as multiple entries in the
           * paramsList array.
           *
           * NOTE: persisted listeners are stored in the startupData part of the Addon DB
           * and are expected to be preserved across Firefox and Addons upgrades and downgrades
           * (unlike the WebExtensions startupCache data which is cleared when Firefox or the
           * addon is updated) and so we are taking special care about forward and backward
           * compatibility of the persistentListeners on-disk format:
           *
           * - forward compatibility: when this new version of this startupData loading logic
           *   is loading the old persistentListeners on-disk format:
           *   - on the first run only one listener will be primed for each of the extra params
           *     recorded in the startupData (same as in older Firefox versions)
           *     and Bug 1795801 will still be hit, but once the background
           *     context is started once the startupData will be updated to
           *     include each of the listeners (indipendently if the set of
           *     extra params is the same as another listener already been
           *     persisted).
           *   - after the first run, all listeners will be primed separately, even if the extra
           *     params are the same as other listeners already primed, and so
           *     each of the listener will receive the pending events collected
           *     by their related primed listener and Bug 1795801 not to be hit anymore.
           *
           * - backward compatibility: when the old version of this startupData loading logic
           *   (https://searchfox.org/mozilla-central/rev/cd2121e7d8/toolkit/components/extensions/ExtensionCommon.jsm#2360-2371)
           *   is loading the new persistentListeners on-disk format, the last
           *   entry with the same set of extra params will be eventually overwritting the
           *   entry for another primed listener with the same extra params, Bug 1795801 will still
           *   be hit, but no actual change in behavior is expected.
           */
          let key = uneval(paramList);
          const eventEntry = listeners.get(module).get(event);

          if (eventEntry.has(key)) {
            const keyEntry = eventEntry.get(key);
            let primeId = keyEntry.nextPrimeId;
            keyEntry.listeners.push({ primeId });
            keyEntry.nextPrimeId++;
          } else {
            eventEntry.set(key, {
              params: paramList,
              nextPrimeId: 1,
              listeners: [{ primeId: 0 }],
            });
          }
          found = true;
        }
      }
    }
    return found;
  }

  // Extract just the information needed at startup for all persistent
  // listeners, and arrange for it to be saved.  This should be called
  // whenever the set of persistent listeners for an extension changes.
  static _writePersistentListeners(extension) {
    let startupListeners = {};
    for (let [module, moduleEntry] of extension.persistentListeners) {
      startupListeners[module] = {};
      for (let [event, eventEntry] of moduleEntry) {
        // Turn the per-event entries from the format they are being kept
        // in memory:
        //
        //   [
        //     { params: paramList1, listeners: [listener1, listener2, ...] },
        //     { params: paramList2, listeners: [listener3, listener3, ...] },
        //     ...
        //   ]
        //
        // into the format used for storing them on disk (in the startupData),
        // which is an array of the params for each listener (with the param list
        // included as many times as many listeners are persisted for the same
        // set of params):
        //
        //   [paramList1, paramList1, ..., paramList2, paramList2, ...]
        //
        // This format will also work as expected on older Firefox versions where
        // only one listener was being persisted for each set of params.
        startupListeners[module][event] = Array.from(
          eventEntry.values()
        ).flatMap(keyEntry => keyEntry.listeners.map(() => keyEntry.params));
      }
    }

    extension.startupData.persistentListeners = startupListeners;
    extension.saveStartupData();
  }

  // Set up "primed" event listeners for any saved event listeners
  // in an extension's startup data.
  // This function is only called during browser startup, it stores details
  // about all primed listeners in the extension's persistentListeners Map.
  static primeListeners(extension, isInStartup = false) {
    if (!EventManager._initPersistentListeners(extension)) {
      return;
    }

    for (let [module, moduleEntry] of extension.persistentListeners) {
      // If we're in startup, we only want to continue attempting to prime a
      // subset of events that should be startup blocking.
      if (isInStartup) {
        let api_module = extension.apiManager.getModule(module);
        if (!api_module.startupBlocking) {
          continue;
        }
      }

      let api = extension.apiManager.getAPI(module, extension, "addon_parent");

      // If an extension is upgraded and a permission, such as webRequest, is
      // removed, we will have been called but the API is no longer available.
      if (!api?.primeListener) {
        // The runtime module no longer implements primed listeners, drop them.
        extension.persistentListeners.delete(module);
        EventManager._writePersistentListeners(extension);
        continue;
      }
      for (let [event, eventEntry] of moduleEntry) {
        for (let [key, { params, listeners }] of eventEntry) {
          for (let listener of listeners) {
            // Reset the `listener.added` flag by setting it to `false` while
            // re-priming the listeners because the event page has suspended
            // and the previous converted listener is no longer listening.
            const listenerWasAdded = listener.added;
            listener.added = false;
            listener.params = params;
            let primed = { pendingEvents: [] };

            let fireEvent = (...args) =>
              new Promise((resolve, reject) => {
                if (!listener.primed) {
                  reject(
                    new Error(
                      `primed listener ${module}.${event} not re-registered`
                    )
                  );
                  return;
                }
                primed.pendingEvents.push({ args, resolve, reject });
                extension.emit("background-script-event");
              });

            let fire = {
              wakeup: () => extension.wakeupBackground(),
              sync: fireEvent,
              async: fireEvent,
              // fire.async for ProxyContextParent is already not cloning.
              raw: fireEvent,
            };

            try {
              let handler = api.primeListener(
                event,
                fire,
                listener.params,
                isInStartup
              );
              if (handler) {
                listener.primed = primed;
                Object.assign(primed, handler);
              }
            } catch (e) {
              Cu.reportError(
                `Error priming listener ${module}.${event}: ${e} :: ${e.stack}`
              );
              // Force this listener to be cleared.
              listener.error = true;
            }

            // If an attempt to prime a listener failed, ensure it is cleared now.
            // If a module is a startup blocking module, not all listeners may
            // get primed during early startup.  For that reason, we don't clear
            // persisted listeners during early startup.  At the end of background
            // execution any listeners that were not renewed will be cleared.
            //
            // TODO(Bug 1797474): consider priming runtime.onStartup and
            // avoid to special handling it here.
            if (
              listener.error ||
              (!isInStartup &&
                !(
                  (`${module}.${event}` === "runtime.onStartup" &&
                    listenerWasAdded) ||
                  listener.primed
                ))
            ) {
              EventManager.clearPersistentListener(
                extension,
                module,
                event,
                key,
                listener.primeId
              );
            }
          }
        }
      }
    }
  }

  /**
   * This is called as a result of background script startup-finished and shutdown.
   *
   * After startup, it removes any remaining primed listeners.  These exist if the
   * listener was not renewed during startup.  In this case the persisted listener
   * data is also removed.
   *
   * During shutdown, care should be taken to set clearPersistent to false.
   * persisted listener data should NOT be cleared during shutdown.
   *
   * @param {Extension} extension
   * @param {boolean} clearPersistent whether the persisted listener data should be cleared.
   */
  static clearPrimedListeners(extension, clearPersistent = true) {
    if (!extension.persistentListeners) {
      return;
    }

    for (let [module, moduleEntry] of extension.persistentListeners) {
      for (let [event, eventEntry] of moduleEntry) {
        for (let [key, { listeners }] of eventEntry) {
          for (let listener of listeners) {
            let { primed, added, primeId } = listener;
            // When a primed listener is added or renewed during initial
            // background execution we set an added flag.  If it was primed
            // when added, primed is set to null.
            if (added) {
              continue;
            }

            if (primed) {
              // When a primed listener was not renewed, primed will still be truthy.
              // These need to be cleared on shutdown (important for event pages), but
              // we only clear the persisted listener data after the startup of a background.
              // Release any pending events and unregister the primed handler.
              listener.primed = null;

              for (let evt of primed.pendingEvents) {
                evt.reject(new Error("listener not re-registered"));
              }
              primed.unregister();
            }

            // Clear any persisted events that were not renewed, should typically
            // only be done at the end of the background page load.
            if (clearPersistent) {
              EventManager.clearPersistentListener(
                extension,
                module,
                event,
                key,
                primeId
              );
            }
          }
        }
      }
    }
  }

  // Record the fact that there is a listener for the given event in
  // the given extension.  `args` is an Array containing any extra
  // arguments that were passed to addListener().
  static savePersistentListener(extension, module, event, args = []) {
    EventManager._initPersistentListeners(extension);
    let key = uneval(args);
    const eventEntry = extension.persistentListeners.get(module).get(event);

    let primeId;
    if (!eventEntry.has(key)) {
      // when writing, only args are written, other properties are dropped
      primeId = 0;
      eventEntry.set(key, {
        params: args,
        listeners: [{ added: true, primeId }],
        nextPrimeId: 1,
      });
    } else {
      const keyEntry = eventEntry.get(key);
      primeId = keyEntry.nextPrimeId;
      keyEntry.listeners.push({ added: true, primeId });
      keyEntry.nextPrimeId = primeId + 1;
    }

    EventManager._writePersistentListeners(extension);
    return [module, event, key, primeId];
  }

  // Remove the record for the given event listener from the extension's
  // startup data.  `key` must be a string, the result of calling uneval()
  // on the array of extra arguments originally passed to addListener().
  static clearPersistentListener(
    extension,
    module,
    event,
    key = uneval([]),
    primeId = undefined
  ) {
    let eventEntry = extension.persistentListeners.get(module).get(event);

    let keyEntry = eventEntry.get(key);

    if (primeId != undefined && keyEntry) {
      keyEntry.listeners = keyEntry.listeners.filter(
        listener => listener.primeId !== primeId
      );
    }

    if (primeId == undefined || keyEntry?.listeners.length === 0) {
      eventEntry.delete(key);
      if (eventEntry.size == 0) {
        let moduleEntry = extension.persistentListeners.get(module);
        moduleEntry.delete(event);
        if (moduleEntry.size == 0) {
          extension.persistentListeners.delete(module);
        }
      }
    }

    EventManager._writePersistentListeners(extension);
  }

  addListener(callback, ...args) {
    if (this.unregister.has(callback)) {
      return;
    }
    this.context.logActivity("api_call", `${this.name}.addListener`, { args });

    let shouldFire = () => {
      if (this.context.unloaded) {
        dump(`${this.name} event fired after context unloaded.\n`);
      } else if (!this.context.active) {
        dump(`${this.name} event fired while context is inactive.\n`);
      } else if (this.unregister.has(callback)) {
        return true;
      }
      return false;
    };

    let { extension } = this.context;
    const resetIdle = () => {
      if (this.resetIdleOnEvent) {
        extension?.emit("background-script-reset-idle", {
          reason: "event",
          eventName: this.name,
        });
      }
    };

    let fire = {
      // Bug 1754866 fire.sync doesn't match documentation.
      sync: (...args) => {
        if (shouldFire()) {
          resetIdle();
          let result = this.context.applySafe(callback, args);
          this.context.logActivity("api_event", this.name, { args, result });
          return result;
        }
      },
      async: (...args) => {
        return Promise.resolve().then(() => {
          if (shouldFire()) {
            resetIdle();
            let result = this.context.applySafe(callback, args);
            this.context.logActivity("api_event", this.name, { args, result });
            return result;
          }
        });
      },
      raw: (...args) => {
        if (!shouldFire()) {
          throw new Error("Called raw() on unloaded/inactive context");
        }
        resetIdle();
        let result = Reflect.apply(callback, null, args);
        this.context.logActivity("api_event", this.name, { args, result });
        return result;
      },
      asyncWithoutClone: (...args) => {
        return Promise.resolve().then(() => {
          if (shouldFire()) {
            resetIdle();
            let result = this.context.applySafeWithoutClone(callback, args);
            this.context.logActivity("api_event", this.name, { args, result });
            return result;
          }
        });
      },
    };

    let { module, event } = this;

    let unregister = null;
    let recordStartupData = false;

    // If this is a persistent event, check for a listener that was already
    // created during startup.  If there is one, use it and don't create a
    // new one.
    if (this.canPersistEvents) {
      // Once a background is started, listenerPromises is set to null. At
      // that point, we stop recording startup data.
      recordStartupData = !!this.context.listenerPromises;

      let key = uneval(args);
      EventManager._initPersistentListeners(extension);
      let keyEntry = extension.persistentListeners
        .get(module)
        .get(event)
        .get(key);

      // Get the first persistent listener which matches the module, event and extra arguments
      // and not added back by the extension yet, the persistent listener found may be either
      // primed or not (in particular API Events that belongs to APIs that should not be blocking
      // startup may have persistent listeners that are not primed during the first execution
      // of the background context happening as part of the applications startup, whereas they
      // will be primed when the background context will be suspended on the idle timeout).
      let listener = keyEntry?.listeners.find(listener => !listener.added);
      if (listener) {
        // During startup only a subset of persisted listeners are primed.  As
        // well, each API determines whether to prime a specific listener.
        let { primed } = listener;
        if (primed) {
          listener.primed = null;

          primed.convert(fire, this.context);
          unregister = primed.unregister;

          for (let evt of primed.pendingEvents) {
            evt.resolve(fire.async(...evt.args));
          }
        }
        listener.added = true;

        recordStartupData = false;

        // Do not clear the persistent listener for a non-persistent backgrond
        // context on removeListener calls got after the background context
        // was fully started. The persistent listener can instead be cleared
        // by not re-registering it on the next background context startup.
        //
        // This check prevents that for listeners that were already persisted
        // and primed (a separate one below prevents it for new listeners).
        //
        // TODO Bug 1899767: do not reprime if the listener has been
        // unregistered.
        if (extension.persistentBackground) {
          this.remove.set(callback, () => {
            EventManager.clearPersistentListener(
              extension,
              module,
              event,
              uneval(args),
              listener.primeId
            );
          });
        }
      }
    }

    if (!unregister) {
      unregister = this.register(fire, ...args);
    }

    this.unregister.set(callback, unregister);
    this.context.callOnClose(this);

    // If this is a new listener for a persistent event, record
    // the details for subsequent startups.
    if (recordStartupData) {
      const [, , , /* _module */ /* _event */ /* _key */ primeId] =
        EventManager.savePersistentListener(extension, module, event, args);

      // Do not clear the persistent listener for a non-persistent backgrond
      // context on removeListener calls got after the background context
      // was fully started. The persistent listener can instead be cleared
      // by not re-registering it on the next background context startup.
      //
      // This check prevents that for new listeners that were not already persisted
      // and primed.
      //
      // TODO Bug 1899767: do not reprime if the listener has been
      // unregistered.
      if (extension.persistentBackground) {
        this.remove.set(callback, () => {
          EventManager.clearPersistentListener(
            extension,
            module,
            event,
            uneval(args),
            primeId
          );
        });
      }
    }
  }

  removeListener(callback, clearPersistentListener = true) {
    if (!this.unregister.has(callback)) {
      return;
    }
    this.context.logActivity("api_call", `${this.name}.removeListener`, {
      args: [],
    });

    let unregister = this.unregister.get(callback);
    this.unregister.delete(callback);
    try {
      unregister();
    } catch (e) {
      Cu.reportError(e);
    }

    if (clearPersistentListener && this.remove.has(callback)) {
      let cleanup = this.remove.get(callback);
      this.remove.delete(callback);
      cleanup();
    }

    if (this.unregister.size == 0) {
      this.context.forgetOnClose(this);
    }
  }

  hasListener(callback) {
    return this.unregister.has(callback);
  }

  revoke() {
    for (let callback of this.unregister.keys()) {
      this.removeListener(callback, false);
    }
  }

  close() {
    this.revoke();
  }

  api() {
    return {
      addListener: (...args) => this.addListener(...args),
      removeListener: (...args) => this.removeListener(...args),
      hasListener: (...args) => this.hasListener(...args),
      setUserInput: this.inputHandling,
      [lazy.Schemas.REVOKE]: () => this.revoke(),
    };
  }
}

// Simple API for event listeners where events never fire.
function ignoreEvent(context, name) {
  return {
    addListener: function () {
      let id = context.extension.id;
      let frame = Components.stack.caller;
      let msg = `In add-on ${id}, attempting to use listener "${name}", which is unimplemented.`;
      let scriptError = Cc["@mozilla.org/scripterror;1"].createInstance(
        Ci.nsIScriptError
      );
      scriptError.init(
        msg,
        frame.filename,
        frame.lineNumber,
        frame.columnNumber,
        Ci.nsIScriptError.warningFlag,
        "content javascript"
      );
      Services.console.logMessage(scriptError);
    },
    removeListener: function () {},
    hasListener: function () {},
  };
}

const stylesheetMap = new DefaultMap(url => {
  let uri = Services.io.newURI(url);
  return lazy.styleSheetService.preloadSheet(
    uri,
    // Note: keep in sync with ext-browser-content.js. This used to be
    // AGENT_SHEET, but changed to AUTHOR_SHEET, see bug 1873024.
    lazy.styleSheetService.AUTHOR_SHEET
  );
});

/**
 * Updates the in-memory representation of extension host permissions, i.e.
 * policy.allowedOrigins.
 *
 * @param {WebExtensionPolicy} policy
 *        A policy. All MatchPattern instances in policy.allowedOrigins are
 *        expected to have been constructed with ignorePath: true.
 * @param {string[]} origins
 *        A list of already-normalized origins, equivalent to using the
 *        MatchPattern constructor with ignorePath: true.
 * @param {boolean} isAdd
 *        Whether to add instead of removing the host permissions.
 */
function updateAllowedOrigins(policy, origins, isAdd) {
  if (!origins.length) {
    // Nothing to modify.
    return;
  }
  let patternMap = new Map();
  for (let pattern of policy.allowedOrigins.patterns) {
    patternMap.set(pattern.pattern, pattern);
  }
  if (!isAdd) {
    for (let origin of origins) {
      patternMap.delete(origin);
    }
  } else {
    // In the parent process, policy.extension.restrictSchemes is available.
    // In the content process, we need to check the mozillaAddons permission,
    // which is only available if approved by the parent.
    const restrictSchemes =
      policy.extension?.restrictSchemes ??
      policy.hasPermission("mozillaAddons");
    for (let origin of origins) {
      if (patternMap.has(origin)) {
        continue;
      }
      patternMap.set(
        origin,
        new MatchPattern(origin, { restrictSchemes, ignorePath: true })
      );
    }
  }
  // patternMap contains only MatchPattern instances, so we don't need to set
  // the options parameter (with restrictSchemes, etc.) since that is only used
  // if the input is a string.
  policy.allowedOrigins = new MatchPatternSet(Array.from(patternMap.values()));
}

export var ExtensionCommon = {
  BaseContext,
  CanOfAPIs,
  EventManager,
  ExtensionAPI,
  ExtensionAPIPersistent,
  EventEmitter,
  LocalAPIImplementation,
  LocaleData,
  NoCloneSpreadArgs,
  SchemaAPIInterface,
  SchemaAPIManager,
  SpreadArgs,
  checkLoadURI,
  checkLoadURL,
  defineLazyGetter,
  redefineGetter,
  getConsole,
  ignoreEvent,
  instanceOf,
  makeWidgetId,
  normalizeTime,
  runSafeSyncWithoutClone,
  stylesheetMap,
  updateAllowedOrigins,
  withHandlingUserInput,

  MultiAPIManager,
  LazyAPIManager,

  // Whether we're running under Web Platform Tests mode,
  // required to adjust some cross-browser behaviors.
  get isInWPT() {
    return Cu.isInAutomation && lazy.wptEnabled;
  },
};
