/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* This code is loaded in every child process that is started by mochitest.
 */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContentTaskUtils: "resource://testing-common/ContentTaskUtils.sys.mjs",
  MockColorPicker: "resource://testing-common/MockColorPicker.sys.mjs",
  MockFilePicker: "resource://testing-common/MockFilePicker.sys.mjs",
  MockPromptCollection:
    "resource://testing-common/MockPromptCollection.sys.mjs",
  MockSound: "resource://testing-common/MockSound.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PerTestCoverageUtils:
    "resource://testing-common/PerTestCoverageUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SpecialPowersSandbox:
    "resource://testing-common/SpecialPowersSandbox.sys.mjs",
  WrapPrivileged: "resource://testing-common/WrapPrivileged.sys.mjs",
});

Cu.crashIfNotInAutomation();

function bindDOMWindowUtils(aWindow) {
  return aWindow && lazy.WrapPrivileged.wrap(aWindow.windowUtils, aWindow);
}

function defineSpecialPowers(sp) {
  let window = sp.contentWindow;
  window.SpecialPowers = sp;
  if (window === window.wrappedJSObject) {
    return;
  }
  // We can't use a generic |defineLazyGetter| because it does not
  // allow customizing the re-definition behavior.
  Object.defineProperty(window.wrappedJSObject, "SpecialPowers", {
    get() {
      let value = lazy.WrapPrivileged.wrap(sp, window);
      // If we bind |window.wrappedJSObject| when defining the getter
      // and use it here, it might become a dead wrapper.
      // We have to retrieve |wrappedJSObject| again.
      Object.defineProperty(window.wrappedJSObject, "SpecialPowers", {
        configurable: true,
        enumerable: true,
        value,
        writable: true,
      });
      return value;
    },
    configurable: true,
    enumerable: true,
  });
}

// SPConsoleListener reflects nsIConsoleMessage objects into JS in a
// tidy, XPCOM-hiding way.  Messages that are nsIScriptError objects
// have their properties exposed in detail.  It also auto-unregisters
// itself when it receives a "sentinel" message.
function SPConsoleListener(callback, contentWindow) {
  this.callback = callback;
  this.contentWindow = contentWindow;
}

SPConsoleListener.prototype = {
  // Overload the observe method for both nsIConsoleListener and nsIObserver.
  // The topic will be null for nsIConsoleListener.
  observe(msg) {
    let m = {
      message: msg.message,
      errorMessage: null,
      cssSelectors: null,
      sourceName: null,
      lineNumber: null,
      columnNumber: null,
      category: null,
      windowID: null,
      isScriptError: false,
      isConsoleEvent: false,
      isWarning: false,
    };
    if (msg instanceof Ci.nsIScriptError) {
      m.errorMessage = msg.errorMessage;
      m.cssSelectors = msg.cssSelectors;
      m.sourceName = msg.sourceName;
      m.lineNumber = msg.lineNumber;
      m.columnNumber = msg.columnNumber;
      m.category = msg.category;
      m.windowID = msg.outerWindowID;
      m.innerWindowID = msg.innerWindowID;
      m.isScriptError = true;
      m.isWarning = (msg.flags & Ci.nsIScriptError.warningFlag) === 1;
    }

    Object.freeze(m);

    // Run in a separate runnable since console listeners aren't
    // supposed to touch content and this one might.
    Services.tm.dispatchToMainThread(() => {
      this.callback.call(undefined, Cu.cloneInto(m, this.contentWindow));
    });

    if (!m.isScriptError && !m.isConsoleEvent && m.message === "SENTINEL") {
      Services.console.unregisterListener(this);
    }
  },

  QueryInterface: ChromeUtils.generateQI(["nsIConsoleListener", "nsIObserver"]),
};

export class SpecialPowersChild extends JSWindowActorChild {
  constructor() {
    super();

    this._windowID = null;

    this._encounteredCrashDumpFiles = [];
    this._unexpectedCrashDumpFiles = {};
    this._crashDumpDir = null;
    this._serviceWorkerRegistered = false;
    this._serviceWorkerCleanUpRequests = new Map();
    Object.defineProperty(this, "Components", {
      configurable: true,
      enumerable: true,
      value: Components,
    });
    this._createFilesOnError = null;
    this._createFilesOnSuccess = null;

    this._messageListeners = new ExtensionUtils.DefaultMap(() => new Set());

    this._consoleListeners = [];
    this._spawnTaskImports = {};
    this._encounteredCrashDumpFiles = [];
    this._unexpectedCrashDumpFiles = {};
    this._crashDumpDir = null;
    this._mfl = null;
    this._asyncObservers = new WeakMap();
    this._xpcomabi = null;
    this._os = null;
    this._pu = null;

    this._nextExtensionID = 0;
    this._extensionListeners = null;

    lazy.WrapPrivileged.disableAutoWrap(
      this.unwrap,
      this.isWrapper,
      this.wrapCallback,
      this.wrapCallbackObject,
      this.setWrapped,
      this.nondeterministicGetWeakMapKeys,
      this.snapshotWindowWithOptions,
      this.snapshotWindow,
      this.snapshotRect
    );
  }

  observe() {
    // Ignore the "{chrome/content}-document-global-created" event. It
    // is only observed to force creation of the actor.
  }

  actorCreated() {
    this.attachToWindow();
  }

  attachToWindow() {
    let window = this.contentWindow;
    // We should not invoke the getter.
    if (!("SpecialPowers" in window.wrappedJSObject)) {
      this._windowID = window.windowGlobalChild.innerWindowId;

      defineSpecialPowers(this);
    }
  }

  get window() {
    return this.contentWindow;
  }

  // Hack around devtools sometimes trying to JSON stringify us.
  toJSON() {
    return {};
  }

  toString() {
    return "[SpecialPowers]";
  }
  sanityCheck() {
    return "foo";
  }

  _addMessageListener(msgname, listener) {
    this._messageListeners.get(msgname).add(listener);
  }

  _removeMessageListener(msgname, listener) {
    this._messageListeners.get(msgname).delete(listener);
  }

  receiveMessage(message) {
    if (this._messageListeners.has(message.name)) {
      for (let listener of this._messageListeners.get(message.name)) {
        try {
          if (typeof listener === "function") {
            listener(message);
          } else {
            listener.receiveMessage(message);
          }
        } catch (e) {
          console.error(e);
        }
      }
    }

    switch (message.name) {
      case "SPProcessCrashService":
        if (message.json.type == "crash-observed") {
          for (let e of message.json.dumpIDs) {
            this._encounteredCrashDumpFiles.push(e.id + "." + e.extension);
          }
        }
        break;

      case "SPServiceWorkerRegistered":
        this._serviceWorkerRegistered = message.data.registered;
        break;

      case "SpecialPowers.FilesCreated":
        var createdHandler = this._createFilesOnSuccess;
        this._createFilesOnSuccess = null;
        this._createFilesOnError = null;
        if (createdHandler) {
          createdHandler(Cu.cloneInto(message.data, this.contentWindow));
        }
        break;

      case "SpecialPowers.FilesError":
        var errorHandler = this._createFilesOnError;
        this._createFilesOnSuccess = null;
        this._createFilesOnError = null;
        if (errorHandler) {
          errorHandler(message.data);
        }
        break;

      case "Spawn": {
        let { task, args, caller, taskId, imports } = message.data;
        return this._spawnTask(task, args, caller, taskId, imports);
      }

      case "EnsureFocus": {
        // Ensure that the focus is in this child document. Returns a browsing
        // context of a child frame if a subframe should be focused or undefined
        // otherwise.

        // If a subframe node is focused, then the focus will actually
        // be within that subframe's document. If blurSubframe is true,
        // then blur the subframe so that this parent document is focused
        // instead. If blurSubframe is false, then return the browsing
        // context for that subframe. The parent process will then call back
        // into this same code but in the process for that subframe.
        let focusedNode = this.document.activeElement;
        let subframeFocused =
          ChromeUtils.getClassName(focusedNode) == "HTMLIFrameElement" ||
          ChromeUtils.getClassName(focusedNode) == "HTMLFrameElement" ||
          ChromeUtils.getClassName(focusedNode) == "XULFrameElement";
        if (subframeFocused) {
          if (message.data.blurSubframe) {
            Services.focus.clearFocus(this.contentWindow);
          } else {
            if (!this.document.hasFocus()) {
              this.contentWindow.focus();
            }
            return Promise.resolve(focusedNode.browsingContext);
          }
        }

        // A subframe is not focused, so if this document is
        // not focused, focus it and wait for the focus event.
        if (!this.document.hasFocus()) {
          return new Promise(resolve => {
            this.document.addEventListener(
              "focus",
              () => {
                resolve();
              },
              {
                capture: true,
                once: true,
              }
            );
            this.contentWindow.focus();
          });
        }
        break;
      }

      case "Assert":
        {
          // Handles info & Assert reports from SpecialPowersSandbox.sys.mjs.
          if ("info" in message.data) {
            (this.xpcshellScope || this.SimpleTest).info(message.data.info);
            break;
          }

          // An assertion has been done in a mochitest chrome script
          let { name, passed, stack, diag, expectFail } = message.data;

          if (this.xpcshellScope) {
            this.xpcshellScope.do_report_result(passed, name, stack);
          } else if (this.SimpleTest) {
            let expected = expectFail ? "fail" : "pass";
            this.SimpleTest.record(passed, name, diag, stack, expected);
          } else {
            // Well, this is unexpected.
            dump(name + "\n");
          }
        }
        break;
    }
    return undefined;
  }

  registerProcessCrashObservers() {
    this.sendAsyncMessage("SPProcessCrashService", { op: "register-observer" });
  }

  unregisterProcessCrashObservers() {
    this.sendAsyncMessage("SPProcessCrashService", {
      op: "unregister-observer",
    });
  }

  /*
   * Privileged object wrapping API
   *
   * Usage:
   *   var wrapper = SpecialPowers.wrap(obj);
   *   wrapper.privilegedMethod(); wrapper.privilegedProperty;
   *   obj === SpecialPowers.unwrap(wrapper);
   *
   * These functions provide transparent access to privileged objects using
   * various pieces of deep SpiderMagic. Conceptually, a wrapper is just an
   * object containing a reference to the underlying object, where all method
   * calls and property accesses are transparently performed with the System
   * Principal. Moreover, objects obtained from the wrapper (including properties
   * and method return values) are wrapped automatically. Thus, after a single
   * call to SpecialPowers.wrap(), the wrapper layer is transitively maintained.
   *
   * Known Issues:
   *
   *  - The wrapping function does not preserve identity, so
   *    SpecialPowers.wrap(foo) !== SpecialPowers.wrap(foo). See bug 718543.
   *
   *  - The wrapper cannot see expando properties on unprivileged DOM objects.
   *    That is to say, the wrapper uses Xray delegation.
   *
   *  - The wrapper sometimes guesses certain ES5 attributes for returned
   *    properties. This is explained in a comment in the wrapper code above,
   *    and shouldn't be a problem.
   */
  wrap(obj) {
    return obj;
  }
  unwrap(obj) {
    return lazy.WrapPrivileged.unwrap(obj);
  }
  isWrapper(val) {
    return lazy.WrapPrivileged.isWrapper(val);
  }

  unwrapIfWrapped(obj) {
    return lazy.WrapPrivileged.isWrapper(obj)
      ? lazy.WrapPrivileged.unwrap(obj)
      : obj;
  }

  /*
   * Wrap objects on a specified global.
   */
  wrapFor(obj, win) {
    return lazy.WrapPrivileged.wrap(obj, win);
  }

  /*
   * When content needs to pass a callback or a callback object to an API
   * accessed over SpecialPowers, that API may sometimes receive arguments for
   * whom it is forbidden to create a wrapper in content scopes. As such, we
   * need a layer to wrap the values in SpecialPowers wrappers before they ever
   * reach content.
   */
  wrapCallback(func) {
    return lazy.WrapPrivileged.wrapCallback(func, this.contentWindow);
  }
  wrapCallbackObject(obj) {
    return lazy.WrapPrivileged.wrapCallbackObject(obj, this.contentWindow);
  }

  /*
   * Used for assigning a property to a SpecialPowers wrapper, without unwrapping
   * the value that is assigned.
   */
  setWrapped(obj, prop, val) {
    if (!lazy.WrapPrivileged.isWrapper(obj)) {
      throw new Error(
        "You only need to use this for SpecialPowers wrapped objects"
      );
    }

    obj = lazy.WrapPrivileged.unwrap(obj);
    return Reflect.set(obj, prop, val);
  }

  /*
   * Create blank privileged objects to use as out-params for privileged functions.
   */
  createBlankObject() {
    return {};
  }

  /*
   * Because SpecialPowers wrappers don't preserve identity, comparing with ==
   * can be hazardous. Sometimes we can just unwrap to compare, but sometimes
   * wrapping the underlying object into a content scope is forbidden. This
   * function strips any wrappers if they exist and compare the underlying
   * values.
   */
  compare(a, b) {
    return lazy.WrapPrivileged.unwrap(a) === lazy.WrapPrivileged.unwrap(b);
  }

  get MockFilePicker() {
    return lazy.MockFilePicker;
  }

  get MockColorPicker() {
    return lazy.MockColorPicker;
  }

  get MockPromptCollection() {
    return lazy.MockPromptCollection;
  }

  get MockSound() {
    return lazy.MockSound;
  }

  quit() {
    this.sendAsyncMessage("SpecialPowers.Quit", {});
  }

  // fileRequests is an array of file requests. Each file request is an object.
  // A request must have a field |name|, which gives the base of the name of the
  // file to be created in the profile directory. If the request has a |data| field
  // then that data will be written to the file.
  createFiles(fileRequests, onCreation, onError) {
    return this.sendQuery("SpecialPowers.CreateFiles", fileRequests).then(
      files => onCreation(Cu.cloneInto(files, this.contentWindow)),
      onError
    );
  }

  // Remove the files that were created using |SpecialPowers.createFiles()|.
  // This will be automatically called by |SimpleTest.finish()|.
  removeFiles() {
    this.sendAsyncMessage("SpecialPowers.RemoveFiles", {});
  }

  executeAfterFlushingMessageQueue(aCallback) {
    return this.sendQuery("Ping").then(aCallback);
  }

  async registeredServiceWorkers(aForceCheck) {
    // Please see the comment in SpecialPowersParent.sys.mjs above
    // this._serviceWorkerListener's assignment for what this returns.
    if (this._serviceWorkerRegistered || aForceCheck) {
      // This test registered at least one service worker. Send a synchronous
      // call to the parent to make sure that it called unregister on all of its
      // service workers.
      let { workers } = await this.sendQuery("SPCheckServiceWorkers");
      return workers;
    }

    return [];
  }

  _readUrlAsString(aUrl) {
    // Fetch script content as we can't use scriptloader's loadSubScript
    // to evaluate http:// urls...
    var scriptableStream = Cc[
      "@mozilla.org/scriptableinputstream;1"
    ].getService(Ci.nsIScriptableInputStream);

    var channel = lazy.NetUtil.newChannel({
      uri: aUrl,
      loadUsingSystemPrincipal: true,
    });
    var input = channel.open();
    scriptableStream.init(input);

    var str;
    var buffer = [];

    while ((str = scriptableStream.read(4096))) {
      buffer.push(str);
    }

    var output = buffer.join("");

    scriptableStream.close();
    input.close();

    var status;
    if (channel instanceof Ci.nsIHttpChannel) {
      status = channel.responseStatus;
    }

    if (status == 404) {
      throw new Error(
        `Error while executing chrome script '${aUrl}':\n` +
          "The script doesn't exist. Ensure you have registered it in " +
          "'support-files' in your mochitest.toml."
      );
    }

    return output;
  }

  loadChromeScript(urlOrFunction, sandboxOptions) {
    // Create a unique id for this chrome script
    let id = Services.uuid.generateUUID().toString();

    // Tells chrome code to evaluate this chrome script
    let scriptArgs = { id, sandboxOptions };
    if (typeof urlOrFunction == "function") {
      scriptArgs.function = {
        body: "(" + urlOrFunction.toString() + ")();",
        name: urlOrFunction.name,
      };
    } else {
      // Note: We need to do this in the child since, even though
      // `_readUrlAsString` pretends to be synchronous, its channel
      // winds up spinning the event loop when loading HTTP URLs. That
      // leads to unexpected out-of-order operations if the child sends
      // a message immediately after loading the script.
      scriptArgs.function = {
        body: this._readUrlAsString(urlOrFunction),
      };
      scriptArgs.url = urlOrFunction;
    }
    this.sendAsyncMessage("SPLoadChromeScript", scriptArgs);

    // Returns a MessageManager like API in order to be
    // able to communicate with this chrome script
    let listeners = [];
    let chromeScript = {
      addMessageListener: (name, listener) => {
        listeners.push({ name, listener });
      },

      promiseOneMessage: name =>
        new Promise(resolve => {
          chromeScript.addMessageListener(name, function listener(message) {
            chromeScript.removeMessageListener(name, listener);
            resolve(message);
          });
        }),

      removeMessageListener: (name, listener) => {
        listeners = listeners.filter(
          o => o.name != name || o.listener != listener
        );
      },

      sendAsyncMessage: (name, message) => {
        this.sendAsyncMessage("SPChromeScriptMessage", { id, name, message });
      },

      sendQuery: (name, message) => {
        return this.sendQuery("SPChromeScriptMessage", { id, name, message });
      },

      destroy: () => {
        listeners = [];
        this._removeMessageListener("SPChromeScriptMessage", chromeScript);
      },

      receiveMessage: aMessage => {
        let messageId = aMessage.json.id;
        let name = aMessage.json.name;
        let message = aMessage.json.message;
        if (this.contentWindow) {
          message = new StructuredCloneHolder(
            `SpecialPowers/receiveMessage/${name}`,
            null,
            message
          ).deserialize(this.contentWindow);
        }
        // Ignore message from other chrome script
        if (messageId != id) {
          return null;
        }

        let result;
        if (aMessage.name == "SPChromeScriptMessage") {
          for (let listener of listeners.filter(o => o.name == name)) {
            result = listener.listener(message);
          }
        }
        return result;
      },
    };
    this._addMessageListener("SPChromeScriptMessage", chromeScript);

    return chromeScript;
  }

  get Services() {
    return Services;
  }

  /*
   * Convenient shortcuts to the standard Components abbreviations.
   */
  get Cc() {
    return Cc;
  }
  get Ci() {
    return Ci;
  }
  get Cu() {
    return Cu;
  }
  get Cr() {
    return Cr;
  }

  get ChromeUtils() {
    return ChromeUtils;
  }

  get isHeadless() {
    return Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo).isHeadless;
  }

  get addProfilerMarker() {
    return ChromeUtils.addProfilerMarker;
  }

  get DOMWindowUtils() {
    return this.contentWindow.windowUtils;
  }

  getDOMWindowUtils(aWindow) {
    if (aWindow == this.contentWindow) {
      return aWindow.windowUtils;
    }

    return bindDOMWindowUtils(Cu.unwaiveXrays(aWindow));
  }

  async toggleMuteState(aMuted, aWindow) {
    let actor = aWindow
      ? aWindow.windowGlobalChild.getActor("SpecialPowers")
      : this;
    return actor.sendQuery("SPToggleMuteAudio", { mute: aMuted });
  }

  /*
   * A method to get a DOMParser that can't parse XUL.
   */
  getNoXULDOMParser() {
    // If we create it with a system subject principal (so it gets a
    // nullprincipal), it won't be able to parse XUL by default.
    return new DOMParser();
  }

  get InspectorUtils() {
    return InspectorUtils;
  }

  get PromiseDebugging() {
    return PromiseDebugging;
  }

  async waitForCrashes(aExpectingProcessCrash) {
    if (!aExpectingProcessCrash) {
      return;
    }

    var crashIds = this._encounteredCrashDumpFiles
      .filter(filename => {
        return filename.length === 40 && filename.endsWith(".dmp");
      })
      .map(id => {
        return id.slice(0, -4); // Strip the .dmp extension to get the ID
      });

    await this.sendQuery("SPProcessCrashManagerWait", {
      crashIds,
    });
  }

  async removeExpectedCrashDumpFiles(aExpectingProcessCrash) {
    var success = true;
    if (aExpectingProcessCrash) {
      var message = {
        op: "delete-crash-dump-files",
        filenames: this._encounteredCrashDumpFiles,
      };
      if (!(await this.sendQuery("SPProcessCrashService", message))) {
        success = false;
      }
    }
    this._encounteredCrashDumpFiles.length = 0;
    return success;
  }

  async findUnexpectedCrashDumpFiles() {
    var self = this;
    var message = {
      op: "find-crash-dump-files",
      crashDumpFilesToIgnore: this._unexpectedCrashDumpFiles,
    };
    var crashDumpFiles = await this.sendQuery("SPProcessCrashService", message);
    crashDumpFiles.forEach(function (aFilename) {
      self._unexpectedCrashDumpFiles[aFilename] = true;
    });
    // The value is an Array of strings. Export into the scope of the window to
    // allow the caller to read its value without wrapper. Callers of
    // findUnexpectedCrashDumpFiles will automatically get a wrapper; call
    // SpecialPowers.unwrap() on its return value to access the raw value that
    // we are returning here (see bug 2007587 for context).
    crashDumpFiles = Cu.cloneInto(crashDumpFiles, this.contentWindow);
    return crashDumpFiles;
  }

  removePendingCrashDumpFiles() {
    var message = {
      op: "delete-pending-crash-dump-files",
    };
    return this.sendQuery("SPProcessCrashService", message);
  }

  _setTimeout(callback, delay = 0) {
    // for mochitest-browser
    if (typeof this.chromeWindow != "undefined") {
      this.chromeWindow.setTimeout(callback, delay);
    }
    // for mochitest-plain
    else {
      this.contentWindow.setTimeout(callback, delay);
    }
  }

  promiseTimeout(delay) {
    return new Promise(resolve => {
      this._setTimeout(resolve, delay);
    });
  }

  _delayCallbackTwice(callback) {
    let delayedCallback = () => {
      let delayAgain = aCallback => {
        // Using this._setTimeout doesn't work here
        // It causes failures in mochtests that use
        // multiple pushPrefEnv calls
        // For chrome/browser-chrome mochitests
        this._setTimeout(aCallback);
      };
      delayAgain(delayAgain.bind(this, callback));
    };
    return delayedCallback;
  }

  /* apply permissions to the system and when the test case is finished (SimpleTest.finish())
     we will revert the permission back to the original.

     inPermissions is an array of objects where each object has a type, action, context, ex:
     [{'type': 'SystemXHR', 'allow': 1, 'context': document},
      {'type': 'SystemXHR', 'allow': Ci.nsIPermissionManager.PROMPT_ACTION, 'context': document}]

     Allow can be a boolean value of true/false or ALLOW_ACTION/DENY_ACTION/PROMPT_ACTION/UNKNOWN_ACTION
  */
  async pushPermissions(inPermissions, callback) {
    let permissions = [];
    for (let perm of inPermissions) {
      let principal = this._getPrincipalFromArg(perm.context);
      permissions.push({
        ...perm,
        context: null,
        principal,
      });
    }

    await this.sendQuery("PushPermissions", permissions).then(callback);
    await this.promiseTimeout(0);
  }

  async popPermissions(callback = null) {
    await this.sendQuery("PopPermissions").then(callback);
    await this.promiseTimeout(0);
  }

  async flushPermissions(callback = null) {
    await this.sendQuery("FlushPermissions").then(callback);
    await this.promiseTimeout(0);
  }

  /*
   * This function should be used when specialpowers is in content process but
   * it want to get the notification from chrome space.
   *
   * This function will call Services.obs.addObserver in SpecialPowersParent
   * (that is in chrome process) and forward the data received to SpecialPowers
   * via messageManager.
   * You can use this._addMessageListener("specialpowers-YOUR_TOPIC") to fire
   * the callback.
   *
   * To get the expected data, you should modify
   * SpecialPowersParent.prototype._registerObservers.observe. Or the message
   * you received from messageManager will only contain 'aData' from Service.obs.
   */
  registerObservers(topic) {
    var msg = {
      op: "add",
      observerTopic: topic,
    };
    return this.sendQuery("SPObserverService", msg);
  }

  async pushPrefEnv(inPrefs, callback = null) {
    let { requiresRefresh } = await this.sendQuery("PushPrefEnv", inPrefs);
    if (callback) {
      await callback();
    }
    if (requiresRefresh) {
      await this._promiseEarlyRefresh();
    }
  }

  async popPrefEnv(callback = null) {
    let { popped, requiresRefresh } = await this.sendQuery("PopPrefEnv");
    if (callback) {
      await callback(popped);
    }
    if (requiresRefresh) {
      await this._promiseEarlyRefresh();
    }
  }

  async flushPrefEnv(callback = null) {
    let { requiresRefresh } = await this.sendQuery("FlushPrefEnv");
    if (callback) {
      await callback();
    }
    if (requiresRefresh) {
      await this._promiseEarlyRefresh();
    }
  }

  async prefEnv(inPrefs) {
    await this.pushPrefEnv(inPrefs);
    return {
      [Symbol.asyncDispose]: () => this.popPrefEnv(),
    };
  }

  /*
    Collect a snapshot of all preferences in Firefox (i.e. about:prefs).
    From this, store the results within specialpowers for later reference.
  */
  async getBaselinePrefs(callback = null) {
    await this.sendQuery("getBaselinePrefs");
    if (callback) {
      await callback();
    }
  }

  /*
    This uses the stored prefs from getBaselinePrefs, collects a new snapshot
    of preferences, then compares the new vs the baseline.  If there are differences
    they are recorded and returned as an array of preferences, in addition
    all the changed preferences are reset to the value found in the baseline.

    ignorePrefs: array of strings which are preferences.  If they end in *,
                 we do a partial match
  */
  async comparePrefsToBaseline(ignorePrefs, callback = null) {
    let retVal = await this.sendQuery("comparePrefsToBaseline", ignorePrefs);
    if (callback) {
      callback(retVal);
    }
    return retVal;
  }

  _promiseEarlyRefresh() {
    return new Promise(r => {
      // for mochitest-browser
      if (typeof this.chromeWindow != "undefined") {
        this.chromeWindow.requestAnimationFrame(r);
      }
      // for mochitest-plain
      else {
        this.contentWindow.requestAnimationFrame(r);
      }
    });
  }

  _addObserverProxy(notification) {
    if (notification in this._proxiedObservers) {
      this._addMessageListener(
        notification,
        this._proxiedObservers[notification]
      );
    }
  }
  _removeObserverProxy(notification) {
    if (notification in this._proxiedObservers) {
      this._removeMessageListener(
        notification,
        this._proxiedObservers[notification]
      );
    }
  }

  addObserver(obs, notification, weak) {
    // Make sure the parent side exists, or we won't get any notifications.
    this.sendAsyncMessage("Wakeup");

    this._addObserverProxy(notification);
    obs = Cu.waiveXrays(obs);
    if (
      typeof obs == "object" &&
      obs.observe.name != "SpecialPowersCallbackWrapper"
    ) {
      obs.observe = lazy.WrapPrivileged.wrapCallback(
        Cu.unwaiveXrays(obs.observe),
        this.contentWindow
      );
    }
    Services.obs.addObserver(obs, notification, weak);
  }
  removeObserver(obs, notification) {
    this._removeObserverProxy(notification);
    Services.obs.removeObserver(Cu.waiveXrays(obs), notification);
  }
  notifyObservers(subject, topic, data) {
    Services.obs.notifyObservers(subject, topic, data);
  }

  /**
   * An async observer is useful if you're listening for a
   * notification that normally is only used by C++ code or chrome
   * code (so it runs in the SystemGroup), but we need to know about
   * it for a test (which runs as web content). If we used
   * addObserver, we would assert when trying to enter web content
   * from a runnabled labeled by the SystemGroup. An async observer
   * avoids this problem.
   */
  addAsyncObserver(obs, notification, weak) {
    obs = Cu.waiveXrays(obs);
    if (
      typeof obs == "object" &&
      obs.observe.name != "SpecialPowersCallbackWrapper"
    ) {
      obs.observe = lazy.WrapPrivileged.wrapCallback(
        Cu.unwaiveXrays(obs.observe),
        this.contentWindow
      );
    }
    let asyncObs = (...args) => {
      Services.tm.dispatchToMainThread(() => {
        if (typeof obs == "function") {
          obs(...args);
        } else {
          obs.observe.call(undefined, ...args);
        }
      });
    };
    this._asyncObservers.set(obs, asyncObs);
    Services.obs.addObserver(asyncObs, notification, weak);
  }
  removeAsyncObserver(obs, notification) {
    let asyncObs = this._asyncObservers.get(Cu.waiveXrays(obs));
    Services.obs.removeObserver(asyncObs, notification);
  }

  can_QI(obj) {
    return obj.QueryInterface !== undefined;
  }
  do_QueryInterface(obj, iface) {
    return obj.QueryInterface(Ci[iface]);
  }

  call_Instanceof(obj1, obj2) {
    obj1 = lazy.WrapPrivileged.unwrap(obj1);
    obj2 = lazy.WrapPrivileged.unwrap(obj2);
    return obj1 instanceof obj2;
  }

  // Returns a privileged getter from an object. GetOwnPropertyDescriptor does
  // not work here because xray wrappers don't properly implement it.
  //
  // This terribleness is used by dom/base/test/test_object.html because
  // <object> and <embed> tags will spawn plugins if their prototype is touched,
  // so we need to get and cache the getter of |hasRunningPlugin| if we want to
  // call it without paradoxically spawning the plugin.
  do_lookupGetter(obj, name) {
    return Object.prototype.__lookupGetter__.call(obj, name);
  }

  // Mimic the get*Pref API
  getBoolPref(...args) {
    return Services.prefs.getBoolPref(...args);
  }
  getIntPref(...args) {
    return Services.prefs.getIntPref(...args);
  }
  getCharPref(...args) {
    return Services.prefs.getCharPref(...args);
  }
  getComplexValue(prefName, iid) {
    return Services.prefs.getComplexValue(prefName, iid);
  }
  getStringPref(...args) {
    return Services.prefs.getStringPref(...args);
  }

  getParentBoolPref(prefName, defaultValue) {
    return this._getParentPref(prefName, "BOOL", { defaultValue });
  }
  getParentIntPref(prefName, defaultValue) {
    return this._getParentPref(prefName, "INT", { defaultValue });
  }
  getParentCharPref(prefName, defaultValue) {
    return this._getParentPref(prefName, "CHAR", { defaultValue });
  }
  getParentStringPref(prefName, defaultValue) {
    return this._getParentPref(prefName, "STRING", { defaultValue });
  }

  // Mimic the set*Pref API
  setBoolPref(prefName, value) {
    return this._setPref(prefName, "BOOL", value);
  }
  setIntPref(prefName, value) {
    return this._setPref(prefName, "INT", value);
  }
  setCharPref(prefName, value) {
    return this._setPref(prefName, "CHAR", value);
  }
  setComplexValue(prefName, iid, value) {
    return this._setPref(prefName, "COMPLEX", value, iid);
  }
  setStringPref(prefName, value) {
    return this._setPref(prefName, "STRING", value);
  }

  // Mimic the clearUserPref API
  clearUserPref(prefName) {
    let msg = {
      op: "clear",
      prefName,
      prefType: "",
    };
    return this.sendQuery("SPPrefService", msg);
  }

  // Private pref functions to communicate to chrome
  async _getParentPref(prefName, prefType, { defaultValue, iid }) {
    let msg = {
      op: "get",
      prefName,
      prefType,
      iid, // Only used with complex prefs
      defaultValue, // Optional default value
    };
    let val = await this.sendQuery("SPPrefService", msg);
    if (val == null) {
      throw new Error(`Error getting pref '${prefName}'`);
    }
    return val;
  }
  _getPref(prefName, prefType) {
    switch (prefType) {
      case "BOOL":
        return Services.prefs.getBoolPref(prefName);
      case "INT":
        return Services.prefs.getIntPref(prefName);
      case "CHAR":
        return Services.prefs.getCharPref(prefName);
      case "STRING":
        return Services.prefs.getStringPref(prefName);
    }
    return undefined;
  }
  _setPref(prefName, prefType, prefValue, iid) {
    let msg = {
      op: "set",
      prefName,
      prefType,
      iid, // Only used with complex prefs
      prefValue,
    };
    return this.sendQuery("SPPrefService", msg);
  }

  _getMUDV(window) {
    return window.docShell.docViewer;
  }
  // XXX: these APIs really ought to be removed, they're not e10s-safe.
  // (also they're pretty Firefox-specific)
  _getTopChromeWindow(window) {
    return window.browsingContext.topChromeWindow;
  }
  _getAutoCompletePopup(window) {
    return this._getTopChromeWindow(window).document.getElementById(
      "PopupAutoComplete"
    );
  }
  addAutoCompletePopupEventListener(window, eventname, listener) {
    this._getAutoCompletePopup(window).addEventListener(eventname, listener);
  }
  removeAutoCompletePopupEventListener(window, eventname, listener) {
    this._getAutoCompletePopup(window).removeEventListener(eventname, listener);
  }
  getFormFillController() {
    return Cc["@mozilla.org/satchel/form-fill-controller;1"].getService(
      Ci.nsIFormFillController
    );
  }
  isBackButtonEnabled(window) {
    return !this._getTopChromeWindow(window)
      .document.getElementById("Browser:Back")
      .hasAttribute("disabled");
  }
  // XXX end of problematic APIs

  addChromeEventListener(type, listener, capture, allowUntrusted) {
    this.docShell.chromeEventHandler.addEventListener(
      type,
      listener,
      capture,
      allowUntrusted
    );
  }
  removeChromeEventListener(type, listener, capture) {
    this.docShell.chromeEventHandler.removeEventListener(
      type,
      listener,
      capture
    );
  }

  async generateMediaControlKeyTestEvent(event) {
    await this.sendQuery("SPGenerateMediaControlKeyTestEvent", { event });
  }

  // Note: each call to registerConsoleListener MUST be paired with a
  // call to postConsoleSentinel; when the callback receives the
  // sentinel it will unregister itself (_after_ calling the
  // callback).  SimpleTest.expectConsoleMessages does this for you.
  // If you register more than one console listener, a call to
  // postConsoleSentinel will zap all of them.
  registerConsoleListener(callback) {
    let listener = new SPConsoleListener(callback, this.contentWindow);
    Services.console.registerListener(listener);
  }
  postConsoleSentinel() {
    Services.console.logStringMessage("SENTINEL");
  }
  resetConsole() {
    Services.console.reset();
  }

  getFullZoom(window) {
    return BrowsingContext.getFromWindow(window).fullZoom;
  }

  getDeviceFullZoom(window) {
    return this._getMUDV(window).deviceFullZoomForTest;
  }
  setFullZoom(window, zoom) {
    BrowsingContext.getFromWindow(window).fullZoom = zoom;
  }
  getTextZoom(window) {
    return BrowsingContext.getFromWindow(window).textZoom;
  }
  setTextZoom(window, zoom) {
    BrowsingContext.getFromWindow(window).textZoom = zoom;
  }

  emulateMedium(window, mediaType) {
    BrowsingContext.getFromWindow(window).top.mediumOverride = mediaType;
  }

  stopEmulatingMedium(window) {
    BrowsingContext.getFromWindow(window).top.mediumOverride = "";
  }

  // Takes a snapshot of the given window and returns a <canvas>
  // containing the image. When the window is same-process, the canvas
  // is returned synchronously. When it is out-of-process (or when a
  // BrowsingContext or FrameLoaderOwner is passed instead of a Window),
  // a promise which resolves to such a canvas is returned instead.
  snapshotWindowWithOptions(content, rect, bgcolor, options) {
    function getImageData(rect, bgcolor, options) {
      let el = content.document.createElementNS(
        "http://www.w3.org/1999/xhtml",
        "canvas"
      );
      if (rect === undefined) {
        rect = {
          top: content.scrollY,
          left: content.scrollX,
          width: content.innerWidth,
          height: content.innerHeight,
        };
      }
      if (bgcolor === undefined) {
        bgcolor = "rgb(255,255,255)";
      }
      if (options === undefined) {
        options = {};
      }

      el.width = rect.width;
      el.height = rect.height;
      let ctx = el.getContext("2d");

      let flags = 0;
      for (let option in options) {
        flags |= options[option] && ctx[option];
      }

      ctx.drawWindow(
        content,
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        bgcolor,
        flags
      );

      return ctx.getImageData(0, 0, el.width, el.height);
    }

    let toCanvas = imageData => {
      let el = this.document.createElementNS(
        "http://www.w3.org/1999/xhtml",
        "canvas"
      );
      el.width = imageData.width;
      el.height = imageData.height;

      if (ImageData.isInstance(imageData)) {
        let ctx = el.getContext("2d");
        ctx.putImageData(imageData, 0, 0);
      }

      return el;
    };

    if (!Cu.isRemoteProxy(content) && Window.isInstance(content)) {
      // Hack around tests that try to snapshot 0 width or height
      // elements.
      if (rect && !(rect.width && rect.height)) {
        return toCanvas(rect);
      }

      // This is an in-process window. Snapshot it synchronously.
      return toCanvas(getImageData(rect, bgcolor, options));
    }

    // This is a remote window or frame. Snapshot it asynchronously and
    // return a promise for the result. Alas, consumers expect us to
    // return a <canvas> element rather than an ImageData object, so we
    // need to convert the result from the remote snapshot to a local
    // canvas.
    let promise = this.spawn(
      content,
      [rect, bgcolor, options],
      getImageData
    ).then(toCanvas);
    if (Cu.isXrayWrapper(this.contentWindow)) {
      return new this.contentWindow.Promise((resolve, reject) => {
        promise.then(resolve, reject);
      });
    }
    return promise;
  }

  snapshotWindow(win, withCaret, rect, bgcolor) {
    return this.snapshotWindowWithOptions(win, rect, bgcolor, {
      DRAWWINDOW_DRAW_CARET: withCaret,
    });
  }

  snapshotRect(win, rect, bgcolor) {
    return this.snapshotWindowWithOptions(win, rect, bgcolor);
  }

  gc() {
    this.contentWindow.windowUtils.garbageCollect();
  }

  forceGC() {
    Cu.forceGC();
  }

  forceShrinkingGC() {
    Cu.forceShrinkingGC();
  }

  forceCC() {
    Cu.forceCC();
  }

  finishCC() {
    Cu.finishCC();
  }

  ccSlice(budget) {
    Cu.ccSlice(budget);
  }

  // Due to various dependencies between JS objects and C++ objects, an ordinary
  // forceGC doesn't necessarily clear all unused objects, thus the GC and CC
  // needs to run several times and when no other JS is running.
  // The current number of iterations has been determined according to massive
  // cross platform testing.
  exactGC(callback) {
    let count = 0;

    function genGCCallback(cb) {
      return function () {
        Cu.forceCC();
        if (++count < 3) {
          Cu.schedulePreciseGC(genGCCallback(cb));
        } else if (cb) {
          cb();
        }
      };
    }

    Cu.schedulePreciseGC(genGCCallback(callback));
  }

  nondeterministicGetWeakMapKeys(m) {
    let keys = ChromeUtils.nondeterministicGetWeakMapKeys(m);
    if (!keys) {
      return undefined;
    }
    return this.contentWindow.Array.from(keys);
  }

  getMemoryReports() {
    try {
      Cc["@mozilla.org/memory-reporter-manager;1"]
        .getService(Ci.nsIMemoryReporterManager)
        .getReports(
          () => {},
          null,
          () => {},
          null,
          false
        );
    } catch (e) {}
  }

  setGCZeal(zeal) {
    Cu.setGCZeal(zeal);
  }

  isMainProcess() {
    try {
      return (
        Services.appinfo.processType == Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT
      );
    } catch (e) {}
    return true;
  }

  get XPCOMABI() {
    if (this._xpcomabi != null) {
      return this._xpcomabi;
    }

    var xulRuntime = Services.appinfo.QueryInterface(Ci.nsIXULRuntime);

    this._xpcomabi = xulRuntime.XPCOMABI;
    return this._xpcomabi;
  }

  // The optional aWin parameter allows the caller to specify a given window in
  // whose scope the runnable should be dispatched. If aFun throws, the
  // exception will be reported to aWin.
  executeSoon(aFun, aWin) {
    // Create the runnable in the scope of aWin to avoid running into COWs.
    var runnable = {};
    if (aWin) {
      runnable = Cu.createObjectIn(aWin);
    }
    runnable.run = aFun;
    Cu.dispatch(runnable, aWin);
  }

  get OS() {
    if (this._os != null) {
      return this._os;
    }

    this._os = Services.appinfo.OS;
    return this._os;
  }

  get useRemoteSubframes() {
    return this.docShell.nsILoadContext.useRemoteSubframes;
  }

  ISOLATION_STRATEGY = {
    IsolateNothing: 0,
    IsolateEverything: 1,
    IsolateHighValue: 2,
  };

  effectiveIsolationStrategy() {
    // If remote subframes are disabled, we always use the IsolateNothing strategy.
    if (!this.useRemoteSubframes) {
      return this.ISOLATION_STRATEGY.IsolateNothing;
    }
    return this.getIntPref("fission.webContentIsolationStrategy");
  }

  // helper method to check if the event is consumed by either default group's
  // event listener or system group's event listener.
  defaultPreventedInAnyGroup(event) {
    // FYI: Event.defaultPrevented returns false in content context if the
    //      event is consumed only by system group's event listeners.
    return event.defaultPrevented;
  }

  addCategoryEntry(category, entry, value, persists, replace) {
    Services.catMan.addCategoryEntry(category, entry, value, persists, replace);
  }

  deleteCategoryEntry(category, entry, persists) {
    Services.catMan.deleteCategoryEntry(category, entry, persists);
  }
  openDialog(win, args) {
    return win.openDialog.apply(win, args);
  }
  // This is a blocking call which creates and spins a native event loop
  spinEventLoop(win) {
    // simply do a sync XHR back to our windows location.
    var syncXHR = new win.XMLHttpRequest();
    syncXHR.open("GET", win.location, false);
    syncXHR.send();
  }

  // :jdm gets credit for this.  ex: getPrivilegedProps(window, 'location.href');
  getPrivilegedProps(obj, props) {
    var parts = props.split(".");
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i];
      if (obj[p] != undefined) {
        obj = obj[p];
      } else {
        return null;
      }
    }
    return obj;
  }

  _browsingContextForTarget(target) {
    if (BrowsingContext.isInstance(target)) {
      return target;
    }
    if (Element.isInstance(target)) {
      return target.browsingContext;
    }

    return BrowsingContext.getFromWindow(target);
  }

  getBrowsingContextID(target) {
    return this._browsingContextForTarget(target).id;
  }

  *getGroupTopLevelWindows(target) {
    let { group } = this._browsingContextForTarget(target);
    for (let bc of group.getToplevels()) {
      yield bc.window;
    }
  }

  /**
   * Runs a task in the context of the given frame, and returns a
   * promise which resolves to the return value of that task.
   *
   * The given frame may be in-process or out-of-process. Either way,
   * the task will run asynchronously, in a sandbox with access to the
   * frame's content window via its `content` global. Any arguments
   * passed will be copied via structured clone, as will its return
   * value.
   *
   * The sandbox also has access to an Assert object, as provided by
   * Assert.sys.mjs. Any assertion methods called before the task resolves
   * will be relayed back to the test environment of the caller.
   * Assertions triggered after a task returns may be relayed back if
   * setAsDefaultAssertHandler() has been called, until this SpecialPowers
   * instance is destroyed.
   *
   * If your assertions need to outlive this SpecialPowers instance,
   * use SpecialPowersForProcess from SpecialPowersProcessActor.sys.mjs,
   * which lives until the specified child process terminates.
   *
   * @param {BrowsingContext or FrameLoaderOwner or WindowProxy} target
   *        The target in which to run the task. This may be any element
   *        which implements the FrameLoaderOwner interface (including
   *        HTML <iframe> elements and XUL <browser> elements) or a
   *        WindowProxy (either in-process or remote).
   * @param {Array<any>} args
   *        An array of arguments to pass to the task. All arguments
   *        must be structured clone compatible, and will be cloned
   *        before being passed to the task.
   * @param {function} task
   *        The function to run in the context of the target. The
   *        function will be stringified and re-evaluated in the context
   *        of the target's content window. It may return any structured
   *        clone compatible value, or a Promise which resolves to the
   *        same, which will be returned to the caller.
   *
   * @returns {Promise<any>}
   *        A promise which resolves to the return value of the task, or
   *        which rejects if the task raises an exception. As this is
   *        being written, the rejection value will always be undefined
   *        in the cases where the task throws an error, though that may
   *        change in the future.
   */
  spawn(target, args, task) {
    let browsingContext = this._browsingContextForTarget(target);

    return this.sendQuery("Spawn", {
      browsingContext,
      args,
      task: String(task),
      caller: Cu.getFunctionSourceLocation(task),
      hasHarness:
        typeof this.SimpleTest === "object" ||
        typeof this.xpcshellScope === "object",
      imports: this._spawnTaskImports,
    });
  }

  /**
   * Like `spawn`, but spawns a chrome task in the parent process,
   * instead. The task additionally has access to `windowGlobalParent`
   * and `browsingContext` globals corresponding to the window from
   * which the task was spawned.
   */
  spawnChrome(args, task) {
    return this.sendQuery("SpawnChrome", {
      args,
      task: String(task),
      caller: Cu.getFunctionSourceLocation(task),
      imports: this._spawnTaskImports,
    });
  }

  snapshotContext(target, rect, background, resetScrollPosition = false) {
    let browsingContext = this._browsingContextForTarget(target);

    return this.sendQuery("Snapshot", {
      browsingContext,
      rect,
      background,
      resetScrollPosition,
    }).then(imageData => {
      return this.contentWindow.createImageBitmap(imageData);
    });
  }

  getSecurityState(target) {
    let browsingContext = this._browsingContextForTarget(target);

    return this.sendQuery("SecurityState", {
      browsingContext,
    });
  }

  _spawnTask(task, args, caller, taskId, imports) {
    let sb = new lazy.SpecialPowersSandbox(
      null,
      data => {
        this.sendAsyncMessage("ProxiedAssert", { taskId, data });
      },
      { imports }
    );

    // If more variables are made available, don't forget to update
    // tools/lint/eslint/eslint-plugin-mozilla/lib/rules/import-content-task-globals.js.
    sb.sandbox.SpecialPowers = this;
    sb.sandbox.ContentTaskUtils = lazy.ContentTaskUtils;
    for (let [global, prop] of Object.entries({
      content: "contentWindow",
      docShell: "docShell",
    })) {
      Object.defineProperty(sb.sandbox, global, {
        get: () => {
          return this[prop];
        },
        enumerable: true,
      });
    }

    return sb.execute(task, args, caller);
  }

  /**
   * Automatically imports the given symbol from the given sys.mjs for any
   * task spawned by this SpecialPowers instance.
   */
  addTaskImport(symbol, url) {
    this._spawnTaskImports[symbol] = url;
  }

  get SimpleTest() {
    return this._SimpleTest || this.contentWindow.wrappedJSObject.SimpleTest;
  }
  set SimpleTest(val) {
    this._SimpleTest = val;
  }

  get xpcshellScope() {
    return this._xpcshellScope;
  }
  set xpcshellScope(val) {
    this._xpcshellScope = val;
  }

  async evictAllDocumentViewers() {
    await this.sendQuery("EvictAllDocumentViewers");
  }

  /**
   * Sets this actor as the default assertion result handler for tasks
   * which originate in a window without a test harness.
   */
  setAsDefaultAssertHandler() {
    this.sendAsyncMessage("SetAsDefaultAssertHandler");
  }

  getFocusedElementForWindow(targetWindow, aDeep) {
    var outParam = {};
    Services.focus.getFocusedElementForWindow(targetWindow, aDeep, outParam);
    return outParam.value;
  }

  get focusManager() {
    return Services.focus;
  }

  activeWindow() {
    return Services.focus.activeWindow;
  }

  focusedWindow() {
    return Services.focus.focusedWindow;
  }

  clearFocus(aWindow) {
    Services.focus.clearFocus(aWindow);
  }

  focus(aWindow) {
    // This is called inside TestRunner._makeIframe without aWindow, because of assertions in oop mochitests
    // With aWindow, it is called in SimpleTest.waitForFocus to allow popup window opener focus switching
    if (aWindow) {
      aWindow.focus();
    }

    try {
      let actor = aWindow
        ? aWindow.windowGlobalChild.getActor("SpecialPowers")
        : this;
      actor.sendAsyncMessage("SpecialPowers.Focus", {});
    } catch (e) {
      console.error(e);
    }
  }

  ensureFocus(aBrowsingContext, aBlurSubframe) {
    return this.sendQuery("EnsureFocus", {
      browsingContext: aBrowsingContext,
      blurSubframe: aBlurSubframe,
    });
  }

  getClipboardData(flavor, whichClipboard) {
    if (whichClipboard === undefined) {
      whichClipboard = Services.clipboard.kGlobalClipboard;
    }

    var xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(this.docShell);
    xferable.addDataFlavor(flavor);
    Services.clipboard.getData(
      xferable,
      whichClipboard,
      this.browsingContext.currentWindowContext
    );
    var data = {};
    try {
      xferable.getTransferData(flavor, data);
    } catch (e) {}
    data = data.value || null;
    if (data == null) {
      return "";
    }

    return data.QueryInterface(Ci.nsISupportsString).data;
  }

  clipboardCopyString(str) {
    Cc["@mozilla.org/widget/clipboardhelper;1"]
      .getService(Ci.nsIClipboardHelper)
      .copyString(str);
  }

  cleanupAllClipboard() {
    // copied from widget/tests/clipboard_helper.js
    // there is a write there I didn't want to copy
    const clipboard = Services.clipboard;
    const clipboardTypes = [
      clipboard.kGlobalClipboard,
      clipboard.kSelectionClipboard,
      clipboard.kFindClipboard,
      clipboard.kSelectionCache,
    ];

    clipboardTypes.forEach(function (type) {
      if (clipboard.isClipboardTypeSupported(type)) {
        clipboard.emptyClipboard(type);
      }
    });
  }

  supportsSelectionClipboard() {
    return Services.clipboard.isClipboardTypeSupported(
      Services.clipboard.kSelectionClipboard
    );
  }

  swapFactoryRegistration(cid, contractID, newFactory) {
    newFactory = Cu.waiveXrays(newFactory);

    var componentRegistrar = Components.manager.QueryInterface(
      Ci.nsIComponentRegistrar
    );

    var currentCID = componentRegistrar.contractIDToCID(contractID);
    var currentFactory = Components.manager.getClassObject(
      Cc[contractID],
      Ci.nsIFactory
    );
    if (cid) {
      componentRegistrar.unregisterFactory(currentCID, currentFactory);
    } else {
      cid = Services.uuid.generateUUID();
    }

    // Restore the original factory.
    componentRegistrar.registerFactory(cid, "", contractID, newFactory);
    return { originalCID: currentCID };
  }

  _getElement(aWindow, id) {
    return typeof id == "string" ? aWindow.document.getElementById(id) : id;
  }

  dispatchEvent(aWindow, target, event) {
    var el = this._getElement(aWindow, target);
    return el.dispatchEvent(event);
  }

  get isDebugBuild() {
    return Cc["@mozilla.org/xpcom/debug;1"].getService(Ci.nsIDebug2)
      .isDebugBuild;
  }
  assertionCount() {
    var debugsvc = Cc["@mozilla.org/xpcom/debug;1"].getService(Ci.nsIDebug2);
    return debugsvc.assertionCount;
  }

  /**
   * @param arg one of the following:
   *            - A URI string.
   *            - A document node.
   *            - A dictionary including a URL (`url`) and origin attributes (`attr`).
   */
  _getPrincipalFromArg(arg) {
    arg = lazy.WrapPrivileged.unwrap(Cu.unwaiveXrays(arg));

    if (arg.nodePrincipal) {
      // It's a document.
      return arg.nodePrincipal;
    }

    let secMan = Services.scriptSecurityManager;
    if (typeof arg == "string") {
      // It's a URL.
      let uri = Services.io.newURI(arg);
      return secMan.createContentPrincipal(uri, {});
    }

    let uri = Services.io.newURI(arg.url);
    let attrs = arg.originAttributes || {};
    return secMan.createContentPrincipal(uri, attrs);
  }

  async addPermission(type, allow, arg, expireType, expireTime) {
    let principal = this._getPrincipalFromArg(arg);
    if (principal.isSystemPrincipal) {
      return; // nothing to do
    }

    let permission = allow;
    if (typeof permission === "boolean") {
      permission =
        Ci.nsIPermissionManager[allow ? "ALLOW_ACTION" : "DENY_ACTION"];
    }

    var msg = {
      op: "add",
      type,
      permission,
      principal,
      expireType: typeof expireType === "number" ? expireType : 0,
      expireTime: typeof expireTime === "number" ? expireTime : 0,
    };

    await this.sendQuery("SPPermissionManager", msg);
  }

  /**
   * @param type see nsIPermissionsManager::testPermissionFromPrincipal.
   * @param arg one of the following:
   *            - A URI string.
   *            - A document node.
   *            - A dictionary including a URL (`url`) and origin attributes (`attr`).
   */
  async removePermission(type, arg) {
    let principal = this._getPrincipalFromArg(arg);
    if (principal.isSystemPrincipal) {
      return; // nothing to do
    }

    var msg = {
      op: "remove",
      type,
      principal,
    };

    await this.sendQuery("SPPermissionManager", msg);
  }

  async hasPermission(type, arg) {
    let principal = this._getPrincipalFromArg(arg);
    if (principal.isSystemPrincipal) {
      return true; // system principals have all permissions
    }

    var msg = {
      op: "has",
      type,
      principal,
    };

    return this.sendQuery("SPPermissionManager", msg);
  }

  async testPermission(type, value, arg) {
    let principal = this._getPrincipalFromArg(arg);
    if (principal.isSystemPrincipal) {
      return true; // system principals have all permissions
    }

    var msg = {
      op: "test",
      type,
      value,
      principal,
    };
    return this.sendQuery("SPPermissionManager", msg);
  }

  isContentWindowPrivate(win) {
    return lazy.PrivateBrowsingUtils.isContentWindowPrivate(win);
  }

  async notifyObserversInParentProcess(subject, topic, data) {
    if (subject) {
      throw new Error("Can't send subject to another process!");
    }
    if (this.isMainProcess()) {
      this.notifyObservers(subject, topic, data);
      return;
    }
    var msg = {
      op: "notify",
      observerTopic: topic,
      observerData: data,
    };
    await this.sendQuery("SPObserverService", msg);
  }

  removeAllServiceWorkerData() {
    return this.sendQuery("SPRemoveAllServiceWorkers", {});
  }

  removeServiceWorkerDataForExampleDomain() {
    return this.sendQuery("SPRemoveServiceWorkerDataForExampleDomain", {});
  }

  cleanUpSTSData(origin) {
    return this.sendQuery("SPCleanUpSTSData", { origin });
  }

  async requestDumpCoverageCounters() {
    // We want to avoid a roundtrip between child and parent.
    if (!lazy.PerTestCoverageUtils.enabled) {
      return;
    }

    await this.sendQuery("SPRequestDumpCoverageCounters", {});
  }

  async requestResetCoverageCounters() {
    // We want to avoid a roundtrip between child and parent.
    if (!lazy.PerTestCoverageUtils.enabled) {
      return;
    }
    await this.sendQuery("SPRequestResetCoverageCounters", {});
  }

  loadExtension(ext, handler) {
    if (this._extensionListeners == null) {
      this._extensionListeners = new Set();

      this._addMessageListener("SPExtensionMessage", msg => {
        for (let listener of this._extensionListeners) {
          try {
            listener(msg);
          } catch (e) {
            console.error(e);
          }
        }
      });
    }

    // Note, this is not the addon is as used by the AddonManager etc,
    // this is just an identifier used for specialpowers messaging
    // between this content process and the chrome process.
    let id = this._nextExtensionID++;

    handler = Cu.waiveXrays(handler);
    ext = Cu.waiveXrays(ext);

    let sp = this;
    let state = "uninitialized";
    let extension = {
      get state() {
        return state;
      },

      startup() {
        state = "pending";
        return sp.sendQuery("SPStartupExtension", { id }).then(
          () => {
            state = "running";
          },
          () => {
            state = "failed";
            sp._extensionListeners.delete(listener);
            return Promise.reject("startup failed");
          }
        );
      },

      unload() {
        state = "unloading";
        return sp.sendQuery("SPUnloadExtension", { id }).finally(() => {
          sp._extensionListeners.delete(listener);
          state = "unloaded";
        });
      },

      sendMessage(...args) {
        sp.sendAsyncMessage("SPExtensionMessage", { id, args });
      },

      grantActiveTab(tabId) {
        sp.sendAsyncMessage("SPExtensionGrantActiveTab", { id, tabId });
      },

      terminateBackground(...args) {
        return sp.sendQuery("SPExtensionTerminateBackground", { id, args });
      },

      wakeupBackground() {
        return sp.sendQuery("SPExtensionWakeupBackground", { id });
      },
    };

    this.sendAsyncMessage("SPLoadExtension", { ext, id });

    let listener = msg => {
      if (msg.data.id == id) {
        if (msg.data.type == "extensionSetId") {
          extension.id = msg.data.args[0];
          extension.uuid = msg.data.args[1];
        } else if (msg.data.type in handler) {
          handler[msg.data.type](
            ...Cu.cloneInto(msg.data.args, this.contentWindow)
          );
        } else {
          dump(`Unexpected: ${msg.data.type}\n`);
        }
      }
    };

    this._extensionListeners.add(listener);
    return extension;
  }

  invalidateExtensionStorageCache() {
    this.notifyObserversInParentProcess(
      null,
      "extension-invalidate-storage-cache",
      ""
    );
  }

  allowMedia(window, enable) {
    window.docShell.allowMedia = enable;
  }

  createChromeCache(name, url) {
    let principal = this._getPrincipalFromArg(url);
    return new this.contentWindow.CacheStorage(name, principal);
  }

  loadChannelAndReturnStatus(url, loadUsingSystemPrincipal) {
    const BinaryInputStream = Components.Constructor(
      "@mozilla.org/binaryinputstream;1",
      "nsIBinaryInputStream",
      "setInputStream"
    );

    return new Promise(function (resolve) {
      let listener = {
        httpStatus: 0,

        onStartRequest(request) {
          request.QueryInterface(Ci.nsIHttpChannel);
          this.httpStatus = request.responseStatus;
        },

        onDataAvailable(request, stream, offset, count) {
          new BinaryInputStream(stream).readByteArray(count);
        },

        onStopRequest(request, status) {
          /* testing here that the redirect was not followed. If it was followed
            we would see a http status of 200 and status of NS_OK */

          let httpStatus = this.httpStatus;
          resolve({ status, httpStatus });
        },
      };
      let uri = lazy.NetUtil.newURI(url);
      let channel = lazy.NetUtil.newChannel({ uri, loadUsingSystemPrincipal });

      channel.loadFlags |= Ci.nsIChannel.LOAD_DOCUMENT_URI;
      channel.QueryInterface(Ci.nsIHttpChannelInternal);
      channel.documentURI = uri;
      channel.asyncOpen(listener);
    });
  }

  get ParserUtils() {
    if (this._pu != null) {
      return this._pu;
    }

    let pu = Cc["@mozilla.org/parserutils;1"].getService(Ci.nsIParserUtils);
    // We need to create and return our own wrapper.
    this._pu = {
      sanitize(src, flags) {
        return pu.sanitize(src, flags);
      },
      convertToPlainText(src, flags, wrapCol) {
        return pu.convertToPlainText(src, flags, wrapCol);
      },
      parseFragment(fragment, flags, isXML, baseURL, element) {
        let baseURI = baseURL ? lazy.NetUtil.newURI(baseURL) : null;
        return pu.parseFragment(
          lazy.WrapPrivileged.unwrap(fragment),
          flags,
          isXML,
          baseURI,
          lazy.WrapPrivileged.unwrap(element)
        );
      },
    };
    return this._pu;
  }

  /**
   * Which commands are available can be determined by checking which commands
   * are registered. See \ref
   * nsIControllerCommandTable.registerCommand(in String, in nsIControllerCommand).
   */
  doCommand(window, cmd, param) {
    switch (cmd) {
      case "cmd_align":
      case "cmd_backgroundColor":
      case "cmd_fontColor":
      case "cmd_fontFace":
      case "cmd_fontSize":
      case "cmd_formatBlock":
      case "cmd_highlight":
      case "cmd_insertImageNoUI":
      case "cmd_insertLinkNoUI":
      case "cmd_paragraphState": {
        const params = Cu.createCommandParams();
        params.setStringValue("state_attribute", param);
        return window.docShell.doCommandWithParams(cmd, params);
      }
      case "cmd_insertHTML":
      case "cmd_insertText": {
        const params = Cu.createCommandParams();
        params.setStringValue("state_data", param);
        return window.docShell.doCommandWithParams(cmd, params);
      }
      case "cmd_pasteTransferable": {
        const params = Cu.createCommandParams();
        params.setISupportsValue("transferable", param);
        return window.docShell.doCommandWithParams(cmd, params);
      }
      default:
        return window.docShell.doCommand(cmd);
    }
  }

  isCommandEnabled(window, cmd) {
    return window.docShell.isCommandEnabled(cmd);
  }

  /**
   * See \ref nsIDocumentViewerEdit.setCommandNode(in Node).
   */
  setCommandNode(window, node) {
    return window.docShell.docViewer
      .QueryInterface(Ci.nsIDocumentViewerEdit)
      .setCommandNode(node);
  }

  /* Bug 1339006 Runnables of nsIURIClassifier.classify may be labeled by
   * SystemGroup, but some test cases may run as web content. That would assert
   * when trying to enter web content from a runnable labeled by the
   * SystemGroup. To avoid that, we run classify from SpecialPowers which is
   * chrome-privileged and allowed to run inside SystemGroup
   */

  doUrlClassify(principal, callback) {
    let classifierService = Cc[
      "@mozilla.org/url-classifier/dbservice;1"
    ].getService(Ci.nsIURIClassifier);

    let wrapCallback = (...args) => {
      Services.tm.dispatchToMainThread(() => {
        if (typeof callback == "function") {
          callback(...args);
        } else {
          callback.onClassifyComplete.call(undefined, ...args);
        }
      });
    };

    return classifierService.classify(
      lazy.WrapPrivileged.unwrap(principal),
      wrapCallback
    );
  }

  // TODO: Bug 1353701 - Supports custom event target for labelling.
  doUrlClassifyLocal(uri, tables, callback) {
    let classifierService = Cc[
      "@mozilla.org/url-classifier/dbservice;1"
    ].getService(Ci.nsIURIClassifier);

    let wrapCallback = results => {
      Services.tm.dispatchToMainThread(() => {
        if (typeof callback == "function") {
          callback(lazy.WrapPrivileged.wrap(results, this.contentWindow));
        } else {
          callback.onClassifyComplete.call(
            undefined,
            lazy.WrapPrivileged.wrap(results, this.contentWindow)
          );
        }
      });
    };

    let feature = classifierService.createFeatureWithTables(
      "test",
      tables.split(","),
      []
    );
    return classifierService.asyncClassifyLocalWithFeatures(
      lazy.WrapPrivileged.unwrap(uri),
      [feature],
      Ci.nsIUrlClassifierFeature.blocklist,
      wrapCallback
    );
  }

  /* Content processes asynchronously receive child-to-parent transformations
   * when they are launched.  Until they are received, screen coordinates
   * reported to JS are wrong.  This is generally ok.  It behaves as if the
   * user repositioned the window.  But if we want to test screen coordinates,
   * we need to wait for the updated data.
   */
  contentTransformsReceived(win) {
    while (win) {
      try {
        return win.docShell.browserChild.contentTransformsReceived();
      } catch (ex) {
        // browserChild getter throws on non-e10s rather than returning null.
      }
      if (win == win.parent) {
        break;
      }
      win = win.parent;
    }
    return Promise.resolve();
  }

  /**
   * Gets the privileged button inside an <input> element
   * (clear / reveal / number spinner).
   *
   * @param {HTMLInputElement} input
   * @returns {HTMLButtonElement?} the privileged button, if any.
   */
  getInputButton(input) {
    if (ChromeUtils.getClassName(input) != "HTMLInputElement") {
      throw new Error("Not an <input> element");
    }
    let children = InspectorUtils.getChildrenForNode(input, true, false);
    return children.find(e => e.localName == "button");
  }
}

SpecialPowersChild.prototype._proxiedObservers = {
  "specialpowers-http-notify-request": function (aMessage) {
    let uri = aMessage.json.uri;
    Services.obs.notifyObservers(
      null,
      "specialpowers-http-notify-request",
      uri
    );
  },

  "specialpowers-service-worker-shutdown": function () {
    Services.obs.notifyObservers(null, "specialpowers-service-worker-shutdown");
  },

  "specialpowers-csp-on-violate-policy": function (aMessage) {
    let subject = null;

    try {
      subject = Services.io.newURI(aMessage.data.subject);
    } catch (ex) {
      // if it's not a valid URI it must be an nsISupportsCString
      subject = Cc["@mozilla.org/supports-cstring;1"].createInstance(
        Ci.nsISupportsCString
      );
      subject.data = aMessage.data.subject;
    }
    Services.obs.notifyObservers(
      subject,
      "specialpowers-csp-on-violate-policy",
      aMessage.data.data
    );
  },

  "specialpowers-xfo-on-violate-policy": function (aMessage) {
    let subject = Services.io.newURI(aMessage.data.subject);
    Services.obs.notifyObservers(
      subject,
      "specialpowers-xfo-on-violate-policy",
      aMessage.data.data
    );
  },
};
