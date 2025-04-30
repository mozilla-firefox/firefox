/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  ExtensionTestCommon: "resource://testing-common/ExtensionTestCommon.sys.mjs",
  HiddenFrame: "resource://gre/modules/HiddenFrame.sys.mjs",
  PerTestCoverageUtils:
    "resource://testing-common/PerTestCoverageUtils.sys.mjs",
  ServiceWorkerCleanUp: "resource://gre/modules/ServiceWorkerCleanUp.sys.mjs",
  SpecialPowersSandbox:
    "resource://testing-common/SpecialPowersSandbox.sys.mjs",
});

class SpecialPowersError extends Error {
  get name() {
    return "SpecialPowersError";
  }
}

const PREF_TYPES = {
  [Ci.nsIPrefBranch.PREF_INVALID]: "INVALID",
  [Ci.nsIPrefBranch.PREF_INT]: "INT",
  [Ci.nsIPrefBranch.PREF_BOOL]: "BOOL",
  [Ci.nsIPrefBranch.PREF_STRING]: "STRING",
  number: "INT",
  boolean: "BOOL",
  string: "STRING",
};

// We share a single preference environment stack between all
// SpecialPowers instances, across all processes.
let prefUndoStack = [];
let inPrefEnvOp = false;

let permissionUndoStack = [];

function doPrefEnvOp(fn) {
  if (inPrefEnvOp) {
    throw new Error(
      "Reentrant preference environment operations not supported"
    );
  }
  inPrefEnvOp = true;
  try {
    return fn();
  } finally {
    inPrefEnvOp = false;
  }
}

async function createWindowlessBrowser({ isPrivate = false } = {}) {
  const { promiseDocumentLoaded, promiseEvent, promiseObserved } =
    ChromeUtils.importESModule(
      "resource://gre/modules/ExtensionUtils.sys.mjs"
    ).ExtensionUtils;

  let windowlessBrowser = Services.appShell.createWindowlessBrowser(true);

  if (isPrivate) {
    let loadContext = windowlessBrowser.docShell.QueryInterface(
      Ci.nsILoadContext
    );
    loadContext.usePrivateBrowsing = true;
  }

  let chromeShell = windowlessBrowser.docShell.QueryInterface(
    Ci.nsIWebNavigation
  );

  const system = Services.scriptSecurityManager.getSystemPrincipal();
  chromeShell.createAboutBlankDocumentViewer(system, system);
  windowlessBrowser.browsingContext.useGlobalHistory = false;
  chromeShell.loadURI(
    Services.io.newURI("chrome://extensions/content/dummy.xhtml"),
    {
      triggeringPrincipal: system,
    }
  );

  await promiseObserved(
    "chrome-document-global-created",
    win => win.document == chromeShell.document
  );

  let chromeDoc = await promiseDocumentLoaded(chromeShell.document);

  let browser = chromeDoc.createXULElement("browser");
  browser.setAttribute("type", "content");
  browser.setAttribute("disableglobalhistory", "true");
  browser.setAttribute("remote", "true");

  let promise = promiseEvent(browser, "XULFrameLoaderCreated");
  chromeDoc.documentElement.appendChild(browser);

  await promise;

  return { windowlessBrowser, browser };
}

// Supplies the unique IDs for tasks created by SpecialPowers.spawn(),
// used to bounce assertion messages back down to the correct child.
let nextTaskID = 1;

// The default actor to send assertions to if a task originated in a
// window without a test harness.
let defaultAssertHandler;

export class SpecialPowersParent extends JSWindowActorParent {
  constructor() {
    super();

    this._messageManager = Services.mm;
    this._serviceWorkerListener = null;

    this._observer = this.observe.bind(this);

    this.didDestroy = this.uninit.bind(this);

    this._registerObservers = {
      _self: this,
      _topics: [],
      _add(topic) {
        if (!this._topics.includes(topic)) {
          this._topics.push(topic);
          Services.obs.addObserver(this, topic);
        }
      },
      observe(aSubject, aTopic, aData) {
        var msg = { aData };
        switch (aTopic) {
          case "csp-on-violate-policy":
            // the subject is either an nsIURI or an nsISupportsCString
            let subject = null;
            if (aSubject instanceof Ci.nsIURI) {
              subject = aSubject.asciiSpec;
            } else if (aSubject instanceof Ci.nsISupportsCString) {
              subject = aSubject.data;
            } else {
              throw new Error("Subject must be nsIURI or nsISupportsCString");
            }
            msg = {
              subject,
              data: aData,
            };
            this._self.sendAsyncMessage("specialpowers-" + aTopic, msg);
            return;
          case "xfo-on-violate-policy":
            let uriSpec = null;
            if (aSubject instanceof Ci.nsIURI) {
              uriSpec = aSubject.asciiSpec;
            } else {
              throw new Error("Subject must be nsIURI");
            }
            msg = {
              subject: uriSpec,
              data: aData,
            };
            this._self.sendAsyncMessage("specialpowers-" + aTopic, msg);
            return;
          default:
            this._self.sendAsyncMessage("specialpowers-" + aTopic, msg);
        }
      },
    };

    this._basePrefs = null;
    this.init();

    this._crashDumpDir = null;
    this._processCrashObserversRegistered = false;
    this._chromeScriptListeners = [];
    this._extensions = new Map();
    this._taskActors = new Map();
  }

  static registerActor() {
    ChromeUtils.registerWindowActor("SpecialPowers", {
      allFrames: true,
      includeChrome: true,
      child: {
        esModuleURI: "resource://testing-common/SpecialPowersChild.sys.mjs",
        observers: [
          "chrome-document-global-created",
          "content-document-global-created",
        ],
      },
      parent: {
        esModuleURI: "resource://testing-common/SpecialPowersParent.sys.mjs",
      },
    });
  }

  static unregisterActor() {
    ChromeUtils.unregisterWindowActor("SpecialPowers");
  }

  init() {
    Services.obs.addObserver(this._observer, "http-on-modify-request");

    // We would like to check that tests don't leave service workers around
    // after they finish, but that information lives in the parent process.
    // Ideally, we'd be able to tell the child processes whenever service
    // workers are registered or unregistered so they would know at all times,
    // but service worker lifetimes are complicated enough to make that
    // difficult. For the time being, let the child process know when a test
    // registers a service worker so it can ask, synchronously, at the end if
    // the service worker had unregister called on it.
    let swm = Cc["@mozilla.org/serviceworkers/manager;1"].getService(
      Ci.nsIServiceWorkerManager
    );
    let self = this;
    this._serviceWorkerListener = {
      onRegister() {
        self.onRegister();
      },

      onUnregister() {
        // no-op
      },
    };
    swm.addListener(this._serviceWorkerListener);

    this.getBaselinePrefs();
  }

  uninit() {
    if (defaultAssertHandler === this) {
      defaultAssertHandler = null;
    }

    var obs = Services.obs;
    obs.removeObserver(this._observer, "http-on-modify-request");
    this._registerObservers._topics.splice(0).forEach(element => {
      obs.removeObserver(this._registerObservers, element);
    });
    this._removeProcessCrashObservers();

    let swm = Cc["@mozilla.org/serviceworkers/manager;1"].getService(
      Ci.nsIServiceWorkerManager
    );
    swm.removeListener(this._serviceWorkerListener);
  }

  observe(aSubject, aTopic) {
    function addDumpIDToMessage(propertyName) {
      try {
        var id = aSubject.getPropertyAsAString(propertyName);
      } catch (ex) {
        id = null;
      }
      if (id) {
        message.dumpIDs.push({ id, extension: "dmp" });
        message.dumpIDs.push({ id, extension: "extra" });
      }
    }

    switch (aTopic) {
      case "http-on-modify-request":
        if (aSubject instanceof Ci.nsIChannel) {
          let uri = aSubject.URI.spec;
          this.sendAsyncMessage("specialpowers-http-notify-request", { uri });
        }
        break;

      case "ipc:content-shutdown":
        aSubject = aSubject.QueryInterface(Ci.nsIPropertyBag2);
        if (!aSubject.hasKey("abnormal")) {
          return; // This is a normal shutdown, ignore it
        }

        var message = { type: "crash-observed", dumpIDs: [] };
        addDumpIDToMessage("dumpID");
        this.sendAsyncMessage("SPProcessCrashService", message);
        break;
    }
  }

  _getCrashDumpDir() {
    if (!this._crashDumpDir) {
      this._crashDumpDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
      this._crashDumpDir.append("minidumps");
    }
    return this._crashDumpDir;
  }

  _getPendingCrashDumpDir() {
    if (!this._pendingCrashDumpDir) {
      this._pendingCrashDumpDir = Services.dirsvc.get("UAppData", Ci.nsIFile);
      this._pendingCrashDumpDir.append("Crash Reports");
      this._pendingCrashDumpDir.append("pending");
    }
    return this._pendingCrashDumpDir;
  }

  _deleteCrashDumpFiles(aFilenames) {
    var crashDumpDir = this._getCrashDumpDir();
    if (!crashDumpDir.exists()) {
      return false;
    }

    var success = !!aFilenames.length;
    aFilenames.forEach(function (crashFilename) {
      var file = crashDumpDir.clone();
      file.append(crashFilename);
      if (file.exists()) {
        file.remove(false);
      } else {
        success = false;
      }
    });
    return success;
  }

  _findCrashDumpFiles(aToIgnore) {
    var crashDumpDir = this._getCrashDumpDir();
    var entries = crashDumpDir.exists() && crashDumpDir.directoryEntries;
    if (!entries) {
      return [];
    }

    var crashDumpFiles = [];
    while (entries.hasMoreElements()) {
      var file = entries.nextFile;
      var path = String(file.path);
      if (path.match(/\.(dmp|extra)$/) && !aToIgnore[path]) {
        crashDumpFiles.push(path);
      }
    }
    return crashDumpFiles.concat();
  }

  _deletePendingCrashDumpFiles() {
    var crashDumpDir = this._getPendingCrashDumpDir();
    var removed = false;
    if (crashDumpDir.exists()) {
      let entries = crashDumpDir.directoryEntries;
      while (entries.hasMoreElements()) {
        let file = entries.nextFile;
        if (file.isFile()) {
          file.remove(false);
          removed = true;
        }
      }
    }
    return removed;
  }

  _addProcessCrashObservers() {
    if (this._processCrashObserversRegistered) {
      return;
    }

    Services.obs.addObserver(this._observer, "ipc:content-shutdown");
    this._processCrashObserversRegistered = true;
  }

  _removeProcessCrashObservers() {
    if (!this._processCrashObserversRegistered) {
      return;
    }

    Services.obs.removeObserver(this._observer, "ipc:content-shutdown");
    this._processCrashObserversRegistered = false;
  }

  onRegister() {
    this.sendAsyncMessage("SPServiceWorkerRegistered", { registered: true });
  }

  _getURI(url) {
    return Services.io.newURI(url);
  }
  _notifyCategoryAndObservers(subject, topic, data) {
    const serviceMarker = "service,";

    // First create observers from the category manager.

    let observers = [];

    for (let { value: contractID } of Services.catMan.enumerateCategory(
      topic
    )) {
      let factoryFunction;
      if (contractID.substring(0, serviceMarker.length) == serviceMarker) {
        contractID = contractID.substring(serviceMarker.length);
        factoryFunction = "getService";
      } else {
        factoryFunction = "createInstance";
      }

      try {
        let handler = Cc[contractID][factoryFunction]();
        if (handler) {
          let observer = handler.QueryInterface(Ci.nsIObserver);
          observers.push(observer);
        }
      } catch (e) {}
    }

    // Next enumerate the registered observers.
    for (let observer of Services.obs.enumerateObservers(topic)) {
      if (observer instanceof Ci.nsIObserver && !observers.includes(observer)) {
        observers.push(observer);
      }
    }

    observers.forEach(function (observer) {
      try {
        observer.observe(subject, topic, data);
      } catch (e) {}
    });
  }

  /*
    Iterate through one atomic set of pref actions and perform sets/clears as appropriate.
    All actions performed must modify the relevant pref.

    Returns whether we need to wait for a refresh driver tick for the pref to
    have effect. This is only needed for ui. and font. prefs, which affect the
    look and feel code and have some change-coalescing going on.
  */
  _applyPrefs(actions) {
    let requiresRefresh = false;
    for (let pref of actions) {
      // This logic should match PrefRequiresRefresh in reftest.sys.mjs
      requiresRefresh =
        requiresRefresh ||
        pref.name == "layout.css.prefers-color-scheme.content-override" ||
        pref.name.startsWith("ui.") ||
        pref.name.startsWith("browser.display.") ||
        pref.name.startsWith("font.");
      if (pref.action == "set") {
        this._setPref(pref.name, pref.type, pref.value, pref.iid);
      } else if (pref.action == "clear") {
        Services.prefs.clearUserPref(pref.name);
      }
    }
    return requiresRefresh;
  }

  /**
   * Take in a list of pref changes to make, pushes their current values
   * onto the restore stack, and makes the changes.  When the test
   * finishes, these changes are reverted.
   *
   * |inPrefs| must be an object with up to two properties: "set" and "clear".
   * pushPrefEnv will set prefs as indicated in |inPrefs.set| and will unset
   * the prefs indicated in |inPrefs.clear|.
   *
   * For example, you might pass |inPrefs| as:
   *
   *  inPrefs = {'set': [['foo.bar', 2], ['magic.pref', 'baz']],
   *             'clear': [['clear.this'], ['also.this']] };
   *
   * Notice that |set| and |clear| are both an array of arrays.  In |set|, each
   * of the inner arrays must have the form [pref_name, value] or [pref_name,
   * value, iid].  (The latter form is used for prefs with "complex" values.)
   *
   * In |clear|, each inner array should have the form [pref_name].
   *
   * If you set the same pref more than once (or both set and clear a pref),
   * the behavior of this method is undefined.
   */
  pushPrefEnv(inPrefs) {
    return doPrefEnvOp(() => {
      let pendingActions = [];
      let cleanupActions = [];

      for (let [action, prefs] of Object.entries(inPrefs)) {
        for (let pref of prefs) {
          let name = pref[0];
          let value = null;
          let iid = null;
          let type = PREF_TYPES[Services.prefs.getPrefType(name)];
          let originalValue = null;

          if (pref.length == 3) {
            value = pref[1];
            iid = pref[2];
          } else if (pref.length == 2) {
            value = pref[1];
          }

          /* If pref is not found or invalid it doesn't exist. */
          if (type !== "INVALID") {
            if (
              (Services.prefs.prefHasUserValue(name) && action == "clear") ||
              action == "set"
            ) {
              originalValue = this._getPref(name, type);
            }
          } else if (action == "set") {
            /* name doesn't exist, so 'clear' is pointless */
            if (iid) {
              type = "COMPLEX";
            }
          }

          if (type === "INVALID") {
            type = PREF_TYPES[typeof value];
          }
          if (type === "INVALID") {
            throw new Error("Unexpected preference type for " + name);
          }

          pendingActions.push({ action, type, name, value, iid });

          /* Push original preference value or clear into cleanup array */
          var cleanupTodo = { type, name, value: originalValue, iid };
          if (originalValue == null) {
            cleanupTodo.action = "clear";
          } else {
            cleanupTodo.action = "set";
          }
          cleanupActions.push(cleanupTodo);
        }
      }

      prefUndoStack.push(cleanupActions);
      let requiresRefresh = this._applyPrefs(pendingActions);
      return { requiresRefresh };
    });
  }

  async popPrefEnv() {
    return doPrefEnvOp(() => {
      let env = prefUndoStack.pop();
      if (env) {
        let requiresRefresh = this._applyPrefs(env);
        return { popped: true, requiresRefresh };
      }
      return { popped: false, requiresRefresh: false };
    });
  }

  flushPrefEnv() {
    let requiresRefresh = false;
    while (prefUndoStack.length) {
      requiresRefresh |= this.popPrefEnv().requiresRefresh;
    }
    return { requiresRefresh };
  }

  _setPref(name, type, value, iid) {
    switch (type) {
      case "BOOL":
        return Services.prefs.setBoolPref(name, value);
      case "INT":
        return Services.prefs.setIntPref(name, value);
      case "CHAR":
        return Services.prefs.setCharPref(name, value);
      case "COMPLEX":
        return Services.prefs.setComplexValue(name, iid, value);
      case "STRING":
        return Services.prefs.setStringPref(name, value);
    }
    switch (typeof value) {
      case "boolean":
        return Services.prefs.setBoolPref(name, value);
      case "number":
        return Services.prefs.setIntPref(name, value);
      case "string":
        return Services.prefs.setStringPref(name, value);
    }
    throw new Error(
      `Unexpected preference type: ${type} for ${name} with value ${value} and type ${typeof value}`
    );
  }

  _getPref(name, type, defaultValue, iid) {
    switch (type) {
      case "BOOL":
        if (defaultValue !== undefined) {
          return Services.prefs.getBoolPref(name, defaultValue);
        }
        return Services.prefs.getBoolPref(name);
      case "INT":
        if (defaultValue !== undefined) {
          return Services.prefs.getIntPref(name, defaultValue);
        }
        return Services.prefs.getIntPref(name);
      case "CHAR":
        if (defaultValue !== undefined) {
          return Services.prefs.getCharPref(name, defaultValue);
        }
        return Services.prefs.getCharPref(name);
      case "COMPLEX":
        return Services.prefs.getComplexValue(name, iid);
      case "STRING":
        if (defaultValue !== undefined) {
          return Services.prefs.getStringPref(name, defaultValue);
        }
        return Services.prefs.getStringPref(name);
    }
    throw new Error(
      `Unexpected preference type: ${type} for preference ${name}`
    );
  }

  getBaselinePrefs() {
    this._basePrefs = this._getAllPreferences();
  }

  _comparePrefs(base, target, ignorePrefs, partialMatches) {
    let failures = [];
    for (const [key, value] of base) {
      if (ignorePrefs.includes(key)) {
        continue;
      }
      let partialFind = false;
      partialMatches.forEach(pm => {
        if (key.startsWith(pm)) {
          partialFind = true;
        }
      });
      if (partialFind) {
        continue;
      }

      if (value === target.get(key)) {
        continue;
      }
      if (!failures.includes(key)) {
        failures.push(key);
      }
    }
    return failures;
  }

  comparePrefsToBaseline(ignorePrefs) {
    let newPrefs = this._getAllPreferences();

    // find all items in ignorePrefs that end in *, add to partialMatch
    let partialMatch = [];
    if (ignorePrefs === undefined) {
      ignorePrefs = [];
    }
    ignorePrefs.forEach(pref => {
      if (pref.endsWith("*")) {
        partialMatch.push(pref.split("*")[0]);
      }
    });

    // find all new prefs different than old
    let rv1 = this._comparePrefs(
      newPrefs,
      this._basePrefs,
      ignorePrefs,
      partialMatch
    );

    // find all old prefs different than new (in case we delete)
    let rv2 = this._comparePrefs(
      this._basePrefs,
      newPrefs,
      ignorePrefs,
      partialMatch
    );

    let failures = [...new Set([...rv1, ...rv2])];

    // reset failures
    failures.forEach(f => {
      if (this._basePrefs.get(f)) {
        this._setPref(
          f,
          PREF_TYPES[Services.prefs.getPrefType(f)],
          this._basePrefs.get(f)
        );
      } else {
        Services.prefs.clearUserPref(f);
      }
    });

    if (ignorePrefs.length > 1) {
      return failures;
    }
    return [];
  }

  _getAllPreferences() {
    let names = new Map();
    for (let prefName of Services.prefs.getChildList("")) {
      let prefType = PREF_TYPES[Services.prefs.getPrefType(prefName)];
      let prefValue = this._getPref(prefName, prefType);
      names.set(prefName, prefValue);
    }
    return names;
  }

  _toggleMuteAudio(aMuted) {
    let browser = this.browsingContext.top.embedderElement;
    if (aMuted) {
      browser.mute();
    } else {
      browser.unmute();
    }
  }

  _permOp(perm) {
    switch (perm.op) {
      case "add":
        Services.perms.addFromPrincipal(
          perm.principal,
          perm.type,
          perm.permission,
          perm.expireType,
          perm.expireTime
        );
        break;
      case "remove":
        Services.perms.removeFromPrincipal(perm.principal, perm.type);
        break;
      default:
        throw new Error(`Unexpected permission op: ${perm.op}`);
    }
  }

  pushPermissions(inPermissions) {
    let pendingPermissions = [];
    let cleanupPermissions = [];

    for (let permission of inPermissions) {
      let { principal } = permission;
      if (principal.isSystemPrincipal) {
        continue;
      }

      let originalValue = Services.perms.testPermissionFromPrincipal(
        principal,
        permission.type
      );

      let perm = permission.allow;
      if (typeof perm === "boolean") {
        perm = Ci.nsIPermissionManager[perm ? "ALLOW_ACTION" : "DENY_ACTION"];
      }

      if (permission.remove) {
        perm = Ci.nsIPermissionManager.UNKNOWN_ACTION;
      }

      if (originalValue == perm) {
        continue;
      }

      let todo = {
        op: "add",
        type: permission.type,
        permission: perm,
        value: perm,
        principal,
        expireType:
          typeof permission.expireType === "number" ? permission.expireType : 0, // default: EXPIRE_NEVER
        expireTime:
          typeof permission.expireTime === "number" ? permission.expireTime : 0,
      };

      var cleanupTodo = Object.assign({}, todo);

      if (permission.remove) {
        todo.op = "remove";
      }

      pendingPermissions.push(todo);

      if (originalValue == Ci.nsIPermissionManager.UNKNOWN_ACTION) {
        cleanupTodo.op = "remove";
      } else {
        cleanupTodo.value = originalValue;
        cleanupTodo.permission = originalValue;
      }
      cleanupPermissions.push(cleanupTodo);
    }

    permissionUndoStack.push(cleanupPermissions);

    for (let perm of pendingPermissions) {
      this._permOp(perm);
    }
  }

  popPermissions() {
    if (permissionUndoStack.length) {
      for (let perm of permissionUndoStack.pop()) {
        this._permOp(perm);
      }
    }
  }

  flushPermissions() {
    while (permissionUndoStack.length) {
      this.popPermissions();
    }
  }

  _spawnChrome(task, args, caller, imports) {
    let sb = new lazy.SpecialPowersSandbox(
      null,
      data => {
        this.sendAsyncMessage("Assert", data);
      },
      { imports }
    );

    // If more variables are made available, don't forget to update
    // tools/lint/eslint/eslint-plugin-mozilla/lib/rules/import-content-task-globals.js.
    for (let [global, prop] of Object.entries({
      windowGlobalParent: "manager",
      browsingContext: "browsingContext",
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
   * messageManager callback function
   * This will get requests from our API in the window and process them in chrome for it
   **/
  // eslint-disable-next-line complexity
  async receiveMessage(aMessage) {
    let startTime = Cu.now();
    // Try block so we can use a finally statement to add a profiler marker
    // despite all the return statements.
    try {
      // We explicitly return values in the below code so that this function
      // doesn't trigger a flurry of warnings about "does not always return
      // a value".
      switch (aMessage.name) {
        case "SPToggleMuteAudio":
          return this._toggleMuteAudio(aMessage.data.mute);

        case "Ping":
          return undefined;

        case "SpecialPowers.Quit":
          if (
            !AppConstants.RELEASE_OR_BETA &&
            !AppConstants.DEBUG &&
            !AppConstants.MOZ_CODE_COVERAGE &&
            !AppConstants.ASAN &&
            !AppConstants.TSAN
          ) {
            if (Services.profiler.IsActive()) {
              let filename = Services.env.get("MOZ_PROFILER_SHUTDOWN");
              if (filename) {
                await Services.profiler.dumpProfileToFileAsync(filename);
                await Services.profiler.StopProfiler();
              }
            }
            Cu.exitIfInAutomation();
          } else {
            Services.startup.quit(Ci.nsIAppStartup.eForceQuit);
          }
          return undefined;

        case "EnsureFocus":
          let bc = aMessage.data.browsingContext;
          // Send a message to the child telling it to focus the window.
          // If the message responds with a browsing context, then
          // a child browsing context in a subframe should be focused.
          // Iterate until nothing is returned and we get to the most
          // deeply nested subframe that should be focused.
          do {
            let spParent = bc.currentWindowGlobal.getActor("SpecialPowers");
            if (spParent) {
              bc = await spParent.sendQuery("EnsureFocus", {
                blurSubframe: aMessage.data.blurSubframe,
              });
            }
          } while (bc && !aMessage.data.blurSubframe);
          return undefined;

        case "SpecialPowers.Focus":
          if (this.manager.rootFrameLoader) {
            this.manager.rootFrameLoader.ownerElement.focus();
          }
          return undefined;

        case "SpecialPowers.CreateFiles":
          return (async () => {
            let filePaths = [];
            if (!this._createdFiles) {
              this._createdFiles = [];
            }
            let createdFiles = this._createdFiles;

            let promises = [];
            aMessage.data.forEach(function (request) {
              const filePerms = 0o666;
              let testFile = Services.dirsvc.get("ProfD", Ci.nsIFile);
              if (request.name) {
                testFile.appendRelativePath(request.name);
              } else {
                testFile.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, filePerms);
              }
              let outStream = Cc[
                "@mozilla.org/network/file-output-stream;1"
              ].createInstance(Ci.nsIFileOutputStream);
              outStream.init(
                testFile,
                0x02 | 0x08 | 0x20, // PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE
                filePerms,
                0
              );
              if (request.data) {
                outStream.write(request.data, request.data.length);
              }
              outStream.close();
              promises.push(
                File.createFromFileName(testFile.path, request.options).then(
                  function (file) {
                    filePaths.push(file);
                  }
                )
              );
              createdFiles.push(testFile);
            });

            await Promise.all(promises);
            return filePaths;
          })().catch(e => {
            console.error(e);
            return Promise.reject(String(e));
          });

        case "SpecialPowers.RemoveFiles":
          if (this._createdFiles) {
            this._createdFiles.forEach(function (testFile) {
              try {
                testFile.remove(false);
              } catch (e) {}
            });
            this._createdFiles = null;
          }
          return undefined;

        case "Wakeup":
          return undefined;

        case "EvictAllDocumentViewers":
          this.browsingContext.top.sessionHistory.evictAllDocumentViewers();
          return undefined;

        case "getBaselinePrefs":
          return this.getBaselinePrefs();

        case "comparePrefsToBaseline":
          return this.comparePrefsToBaseline(aMessage.data);

        case "PushPrefEnv":
          return this.pushPrefEnv(aMessage.data);

        case "PopPrefEnv":
          return this.popPrefEnv();

        case "FlushPrefEnv":
          return this.flushPrefEnv();

        case "PushPermissions":
          return this.pushPermissions(aMessage.data);

        case "PopPermissions":
          return this.popPermissions();

        case "FlushPermissions":
          return this.flushPermissions();

        case "SPPrefService": {
          let prefs = Services.prefs;
          let prefType = aMessage.json.prefType.toUpperCase();
          let { prefName, prefValue, iid, defaultValue } = aMessage.json;

          if (aMessage.json.op == "get") {
            if (!prefName || !prefType) {
              throw new SpecialPowersError(
                "Invalid parameters for get in SPPrefService"
              );
            }

            // return null if the pref doesn't exist
            if (
              defaultValue === undefined &&
              prefs.getPrefType(prefName) == prefs.PREF_INVALID
            ) {
              return null;
            }
            return this._getPref(prefName, prefType, defaultValue, iid);
          } else if (aMessage.json.op == "set") {
            if (!prefName || !prefType || prefValue === undefined) {
              throw new SpecialPowersError(
                "Invalid parameters for set in SPPrefService"
              );
            }

            return this._setPref(prefName, prefType, prefValue, iid);
          } else if (aMessage.json.op == "clear") {
            if (!prefName) {
              throw new SpecialPowersError(
                "Invalid parameters for clear in SPPrefService"
              );
            }

            prefs.clearUserPref(prefName);
          } else {
            throw new SpecialPowersError("Invalid operation for SPPrefService");
          }

          return undefined; // See comment at the beginning of this function.
        }

        case "SPProcessCrashService": {
          switch (aMessage.json.op) {
            case "register-observer":
              this._addProcessCrashObservers();
              break;
            case "unregister-observer":
              this._removeProcessCrashObservers();
              break;
            case "delete-crash-dump-files":
              return this._deleteCrashDumpFiles(aMessage.json.filenames);
            case "find-crash-dump-files":
              return this._findCrashDumpFiles(
                aMessage.json.crashDumpFilesToIgnore
              );
            case "delete-pending-crash-dump-files":
              return this._deletePendingCrashDumpFiles();
            default:
              throw new SpecialPowersError(
                "Invalid operation for SPProcessCrashService"
              );
          }
          return undefined; // See comment at the beginning of this function.
        }

        case "SPProcessCrashManagerWait": {
          let promises = aMessage.json.crashIds.map(crashId => {
            return Services.crashmanager.ensureCrashIsPresent(crashId);
          });
          return Promise.all(promises);
        }

        case "SPPermissionManager": {
          let msg = aMessage.data;
          switch (msg.op) {
            case "add":
            case "remove":
              this._permOp(msg);
              break;
            case "has":
              let hasPerm = Services.perms.testPermissionFromPrincipal(
                msg.principal,
                msg.type
              );
              return hasPerm == Ci.nsIPermissionManager.ALLOW_ACTION;
            case "test":
              let testPerm = Services.perms.testPermissionFromPrincipal(
                msg.principal,
                msg.type
              );
              return testPerm == msg.value;
            default:
              throw new SpecialPowersError(
                "Invalid operation for SPPermissionManager"
              );
          }
          return undefined; // See comment at the beginning of this function.
        }

        case "SPObserverService": {
          let topic = aMessage.json.observerTopic;
          switch (aMessage.json.op) {
            case "notify":
              let data = aMessage.json.observerData;
              Services.obs.notifyObservers(null, topic, data);
              break;
            case "add":
              this._registerObservers._add(topic);
              break;
            default:
              throw new SpecialPowersError(
                "Invalid operation for SPObserverervice"
              );
          }
          return undefined; // See comment at the beginning of this function.
        }

        case "SPLoadChromeScript": {
          let id = aMessage.json.id;
          let scriptName;

          let jsScript = aMessage.json.function.body;
          if (aMessage.json.url) {
            scriptName = aMessage.json.url;
          } else if (aMessage.json.function) {
            scriptName =
              aMessage.json.function.name ||
              "<loadChromeScript anonymous function>";
          } else {
            throw new SpecialPowersError("SPLoadChromeScript: Invalid script");
          }

          // Setup a chrome sandbox that has access to sendAsyncMessage
          // and {add,remove}MessageListener in order to communicate with
          // the mochitest.
          let sb = new lazy.SpecialPowersSandbox(
            scriptName,
            data => {
              this.sendAsyncMessage("Assert", data);
            },
            aMessage.data
          );

          Object.assign(sb.sandbox, {
            createWindowlessBrowser,
            sendAsyncMessage: (name, message) => {
              this.sendAsyncMessage("SPChromeScriptMessage", {
                id,
                name,
                message,
              });
            },
            addMessageListener: (name, listener) => {
              this._chromeScriptListeners.push({ id, name, listener });
            },
            removeMessageListener: (name, listener) => {
              let index = this._chromeScriptListeners.findIndex(function (obj) {
                return (
                  obj.id == id && obj.name == name && obj.listener == listener
                );
              });
              if (index >= 0) {
                this._chromeScriptListeners.splice(index, 1);
              }
            },
            actorParent: this.manager,
            console,
          });

          // Evaluate the chrome script
          try {
            Cu.evalInSandbox(jsScript, sb.sandbox, "1.8", scriptName, 1);
          } catch (e) {
            throw new SpecialPowersError(
              "Error while executing chrome script '" +
                scriptName +
                "':\n" +
                e +
                "\n" +
                e.fileName +
                ":" +
                e.lineNumber
            );
          }
          return undefined; // See comment at the beginning of this function.
        }

        case "SPChromeScriptMessage": {
          let id = aMessage.json.id;
          let name = aMessage.json.name;
          let message = aMessage.json.message;
          let result;
          for (let listener of this._chromeScriptListeners) {
            if (listener.name == name && listener.id == id) {
              result = listener.listener(message);
            }
          }
          return result;
        }

        case "SPCleanUpSTSData": {
          let origin = aMessage.data.origin;
          let uri = Services.io.newURI(origin);
          let sss = Cc["@mozilla.org/ssservice;1"].getService(
            Ci.nsISiteSecurityService
          );
          sss.resetState(uri);
          return undefined;
        }

        case "SPRequestDumpCoverageCounters": {
          return lazy.PerTestCoverageUtils.afterTest();
        }

        case "SPRequestResetCoverageCounters": {
          return lazy.PerTestCoverageUtils.beforeTest();
        }

        case "SPCheckServiceWorkers": {
          let swm = Cc["@mozilla.org/serviceworkers/manager;1"].getService(
            Ci.nsIServiceWorkerManager
          );
          let regs = swm.getAllRegistrations();

          // XXX This code is shared with specialpowers.js.
          let workers = new Array(regs.length);
          for (let i = 0; i < regs.length; ++i) {
            let { scope, scriptSpec } = regs.queryElementAt(
              i,
              Ci.nsIServiceWorkerRegistrationInfo
            );
            workers[i] = { scope, scriptSpec };
          }
          return { workers };
        }

        case "SPLoadExtension": {
          let id = aMessage.data.id;
          let ext = aMessage.data.ext;
          if (AppConstants.MOZ_GECKOVIEW) {
            // Some extension APIs are partially implemented in Java, and the
            // interface between the JS and Java side (GeckoViewWebExtension)
            // expects extensions to be registered with the AddonManager.
            //
            // For simplicity, default to using an Addon Manager (if not null).
            if (ext.useAddonManager === undefined) {
              ext.useAddonManager = "geckoview-only";
            }
          }
          // delayedStartup is only supported in xpcshell
          if (ext.delayedStartup !== undefined) {
            throw new Error(
              `delayedStartup is only supported in xpcshell, use "useAddonManager".`
            );
          }

          let extension = lazy.ExtensionTestCommon.generate(ext);

          let resultListener = (...args) => {
            this.sendAsyncMessage("SPExtensionMessage", {
              id,
              type: "testResult",
              args,
            });
          };

          let messageListener = (...args) => {
            args.shift();
            this.sendAsyncMessage("SPExtensionMessage", {
              id,
              type: "testMessage",
              args,
            });
          };

          // Register pass/fail handlers.
          extension.on("test-result", resultListener);
          extension.on("test-eq", resultListener);
          extension.on("test-log", resultListener);
          extension.on("test-done", resultListener);
          // Web Platform Test subtest started and finished events.
          extension.on("test-task-start", resultListener);
          extension.on("test-task-done", resultListener);

          extension.on("test-message", messageListener);

          this._extensions.set(id, extension);
          return undefined;
        }

        case "SPStartupExtension": {
          let id = aMessage.data.id;
          // This is either an Extension, or (if useAddonManager is set) a MockExtension.
          let extension = this._extensions.get(id);
          extension.on("startup", (eventName, ext) => {
            if (AppConstants.platform === "android") {
              // We need a way to notify the embedding layer that a new extension
              // has been installed, so that the java layer can be updated too.
              Services.obs.notifyObservers(null, "testing-installed-addon", id);
            }
            // ext is always the "real" Extension object, even when "extension"
            // is a MockExtension.
            this.sendAsyncMessage("SPExtensionMessage", {
              id,
              type: "extensionSetId",
              args: [ext.id, ext.uuid],
            });
          });

          // Make sure the extension passes the packaging checks when
          // they're run on a bare archive rather than a running instance,
          // as the add-on manager runs them.
          let extensionData = new lazy.ExtensionData(extension.rootURI);
          return extensionData
            .loadManifest()
            .then(
              () => {
                return extensionData.initAllLocales().then(() => {
                  if (extensionData.errors.length) {
                    return Promise.reject(
                      "Extension contains packaging errors"
                    );
                  }
                  return undefined;
                });
              },
              () => {
                // loadManifest() will throw if we're loading an embedded
                // extension, so don't worry about locale errors in that
                // case.
              }
            )
            .then(async () => {
              // browser tests do not call startup in ExtensionXPCShellUtils or MockExtension,
              // in that case we have an ID here and we need to set the override.
              if (extension.id) {
                await lazy.ExtensionTestCommon.setIncognitoOverride(extension);
              }
              return extension.startup().then(
                () => {},
                e => {
                  dump(`Extension startup failed: ${e}\n${e.stack}`);
                  throw e;
                }
              );
            });
        }

        case "SPExtensionMessage": {
          let id = aMessage.data.id;
          let extension = this._extensions.get(id);
          extension.testMessage(...aMessage.data.args);
          return undefined;
        }

        case "SPExtensionGrantActiveTab": {
          let { id, tabId } = aMessage.data;
          let { tabManager } = this._extensions.get(id);
          tabManager.addActiveTabPermission(tabManager.get(tabId).nativeTab);
          return undefined;
        }

        case "SPUnloadExtension": {
          let id = aMessage.data.id;
          let extension = this._extensions.get(id);
          this._extensions.delete(id);
          return lazy.ExtensionTestCommon.unloadTestExtension(extension);
        }

        case "SPExtensionTerminateBackground": {
          let id = aMessage.data.id;
          let args = aMessage.data.args;
          let extension = this._extensions.get(id);
          return extension.terminateBackground(...args);
        }

        case "SPExtensionWakeupBackground": {
          let id = aMessage.data.id;
          let extension = this._extensions.get(id);
          return extension.wakeupBackground();
        }

        case "SetAsDefaultAssertHandler": {
          defaultAssertHandler = this;
          return undefined;
        }

        case "Spawn": {
          // Use a different variable for the profiler marker start time
          // so that a marker isn't added when we return, but instead when
          // our promise resolves.
          let spawnStartTime = startTime;
          startTime = undefined;
          let { browsingContext, task, args, caller, hasHarness, imports } =
            aMessage.data;

          let spParent =
            browsingContext.currentWindowGlobal.getActor("SpecialPowers");

          let taskId = nextTaskID++;
          if (hasHarness) {
            spParent._taskActors.set(taskId, this);
          }

          return spParent
            .sendQuery("Spawn", { task, args, caller, taskId, imports })
            .finally(() => {
              ChromeUtils.addProfilerMarker(
                "SpecialPowers",
                { startTime: spawnStartTime, category: "Test" },
                aMessage.name
              );
              return spParent._taskActors.delete(taskId);
            });
        }

        case "SpawnChrome": {
          let { task, args, caller, imports } = aMessage.data;

          return this._spawnChrome(task, args, caller, imports);
        }

        case "Snapshot": {
          let { browsingContext, rect, background, resetScrollPosition } =
            aMessage.data;

          return browsingContext.currentWindowGlobal
            .drawSnapshot(rect, 1.0, background, resetScrollPosition)
            .then(async image => {
              let hiddenFrame = new lazy.HiddenFrame();
              let win = await hiddenFrame.get();

              let canvas = win.document.createElement("canvas");
              canvas.width = image.width;
              canvas.height = image.height;

              const ctx = canvas.getContext("2d");
              ctx.drawImage(image, 0, 0);

              let data = ctx.getImageData(0, 0, image.width, image.height);
              hiddenFrame.destroy();
              return data;
            });
        }

        case "SecurityState": {
          let { browsingContext } = aMessage.data;
          return browsingContext.secureBrowserUI.state;
        }

        case "ProxiedAssert": {
          let { taskId, data } = aMessage.data;

          let actor = this._taskActors.get(taskId) || defaultAssertHandler;
          actor.sendAsyncMessage("Assert", data);

          return undefined;
        }

        case "SPRemoveAllServiceWorkers": {
          return lazy.ServiceWorkerCleanUp.removeAll();
        }

        case "SPRemoveServiceWorkerDataForExampleDomain": {
          return lazy.ServiceWorkerCleanUp.removeFromHost("example.com");
        }

        case "SPGenerateMediaControlKeyTestEvent": {
          // eslint-disable-next-line no-undef
          MediaControlService.generateMediaControlKey(aMessage.data.event);
          return undefined;
        }

        default:
          throw new SpecialPowersError(
            `Unrecognized Special Powers API: ${aMessage.name}`
          );
      }
      // This should be unreachable. If it ever becomes reachable, ESLint
      // will produce an error about inconsistent return values.
    } finally {
      if (startTime) {
        ChromeUtils.addProfilerMarker(
          "SpecialPowers",
          { startTime, category: "Test" },
          aMessage.name
        );
      }
    }
  }
}
