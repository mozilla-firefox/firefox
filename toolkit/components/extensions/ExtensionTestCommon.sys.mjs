/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This module contains extension testing helper logic which is common
 * between all test suites.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  Assert: "resource://testing-common/Assert.sys.mjs",
  Extension: "resource://gre/modules/Extension.sys.mjs",
  ExtensionAddonObserver: "resource://gre/modules/Extension.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  clearInterval: "resource://gre/modules/Timer.sys.mjs",
  setInterval: "resource://gre/modules/Timer.sys.mjs",
  apiManager: () => lazy.ExtensionParent.apiManager,
});

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { flushJarCache } = ExtensionUtils;

const { instanceOf } = ExtensionCommon;

// The tasks received here have already been registered as shutdown blockers
// (with AsyncShutdown). That means that in reality, if these tasks take
// long, that they affect Firefox's ability to quit, with a warning after 10
// seconds and a forced quit without waiting for shutdown after 60 seconds
// (or whatever is in the toolkit.asyncshutdown.crash_timeout pref).
//
// To help with detecting unreasonable slowness in tests, we log when these
// tasks are taking too long at shutdown.
const MS_SLOW_TASK_DURATION = 2000;

// Some test assertions to work in both mochitest and xpcshell.  This
// will be revisited later.
const ExtensionTestAssertions = {
  // Used by test extension wrappers terminateBackground methods to assert
  // that the background page has been terminate and log proper warnings
  // and failures to make it easier to investigate test failures related
  // to failing to terminate the background context (See Bug 1899772).
  assertBackgroundStatusStopped(extWrapper) {
    let policy = WebExtensionPolicy.getByID(extWrapper.id);
    const { backgroundState } = policy?.extension || extWrapper.extension;
    lazy.Assert.equal(
      backgroundState,
      "stopped",
      `Expect "${extWrapper.id}" background page state to be "stopped"`
    );
  },

  assertBackgroundStatusRunning(extWrapper) {
    let policy = WebExtensionPolicy.getByID(extWrapper.id);
    const { backgroundState } = policy?.extension || extWrapper.extension;
    lazy.Assert.equal(
      backgroundState,
      "running",
      `Expect "${extWrapper.id}" background page state to be "running"`
    );
  },

  getPersistentListeners(extWrapper, apiNs, apiEvent) {
    let policy = WebExtensionPolicy.getByID(extWrapper.id);
    const extension = policy?.extension || extWrapper.extension;

    if (!extension || !(extension instanceof lazy.Extension)) {
      throw new Error(
        `Unable to retrieve the Extension class instance for ${extWrapper.id}`
      );
    }

    const { persistentListeners } = extension;
    if (
      !persistentListeners?.size ||
      !persistentListeners.get(apiNs)?.has(apiEvent)
    ) {
      return [];
    }

    return Array.from(persistentListeners.get(apiNs).get(apiEvent).values());
  },

  assertPersistentListeners(
    extWrapper,
    apiNs,
    apiEvent,
    { primed, persisted = true, primedListenersCount }
  ) {
    if (primed && !persisted) {
      throw new Error(
        "Inconsistent assertion, can't assert a primed listener if it is not persisted"
      );
    }

    let listenersInfo = ExtensionTestAssertions.getPersistentListeners(
      extWrapper,
      apiNs,
      apiEvent
    );
    lazy.Assert.equal(
      persisted,
      !!listenersInfo?.length,
      `Got a persistent listener for ${apiNs}.${apiEvent}`
    );
    for (const info of listenersInfo) {
      if (primed) {
        lazy.Assert.ok(
          info.listeners.some(listener => listener.primed),
          `${apiNs}.${apiEvent} listener expected to be primed`
        );
      } else {
        lazy.Assert.ok(
          !info.listeners.some(listener => listener.primed),
          `${apiNs}.${apiEvent} listener expected to not be primed`
        );
      }
    }
    if (primed && primedListenersCount > 0) {
      lazy.Assert.equal(
        listenersInfo.reduce((acc, info) => {
          acc += info.listeners.length;
          return acc;
        }, 0),
        primedListenersCount,
        `Got the expected number of ${apiNs}.${apiEvent} listeners to be primed`
      );
    }
  },
};

/**
 * ExtensionUninstallTracker should be instantiated before extension shutdown,
 * and can be used to await the completion of the uninstall and post-uninstall
 * cleanup logic. Log messages are printed to aid debugging if the cleanup is
 * observed to be slow (i.e. taking longer than MS_SLOW_TASK_DURATION).
 *
 *   // Usage:
 *   let uninstallTracker = new ExtensionUninstallTracker(extension.id);
 *   await extension.shutdown();
 *   await uninstallTracker.waitForUninstallCleanupDone();
 */
export class ExtensionUninstallTracker {
  #resolveOnCleanupDone;
  constructor(addonId) {
    this.id = addonId;

    this.remainingTasks = new Set();
    // The uninstall/cleanup observer needs to be registered early, to not miss
    // the notifications.
    this._uninstallPromise = this.#promiseUninstallComplete();
    this._cleanupPromise = this.#promiseCleanupAfterUninstall();
  }

  async waitForUninstallCleanupDone() {
    // Call #addTask() now instead of in the constructor, so that we only track
    // the time after extension.shutdown() has completed.
    this.#addTask("Awaiting uninstall-complete", this._uninstallPromise);
    this.#addTask("Awaiting cleanupAfterUninstall", this._cleanupPromise);
    // For debugging purposes, if shutdown is slow, regularly print a message
    // with the remaining tasks.
    let timer = lazy.setInterval(
      () => this.#checkRemainingTasks(),
      2 * MS_SLOW_TASK_DURATION
    );
    await new Promise(resolve => {
      this.#resolveOnCleanupDone = resolve;
      this.#checkRemainingTasks();
    });
    lazy.clearInterval(timer);
  }

  #addTask(name, promise) {
    const task = { name, promise, timeStart: Date.now() };
    this.remainingTasks.add(task);
    promise.finally(() => {
      this.remainingTasks.delete(task);
      this.#checkRemainingTasks();
    });
  }

  #checkRemainingTasks() {
    for (let task of this.remainingTasks) {
      const timeSinceStart = Date.now() - task.timeStart;
      if (timeSinceStart > MS_SLOW_TASK_DURATION) {
        dump(
          `WARNING: Detected slow post-uninstall task: ${timeSinceStart}ms for extension ${this.id}: ${task.name}\n`
        );
      }
    }
    if (this.remainingTasks.size === 0) {
      this.#resolveOnCleanupDone?.();
    }
  }

  #promiseUninstallComplete() {
    return new Promise(resolve => {
      const onUninstallComplete = (eventName, { id }) => {
        if (id === this.id) {
          lazy.apiManager.off("uninstall-complete", onUninstallComplete);
          resolve();
        }
      };
      lazy.apiManager.on("uninstall-complete", onUninstallComplete);
    });
  }

  #promiseCleanupAfterUninstall() {
    return new Promise(resolve => {
      const onCleanupAfterUninstall = (eventName, id, tasks) => {
        if (id === this.id) {
          lazy.apiManager.off("cleanupAfterUninstall", onCleanupAfterUninstall);
          for (const task of tasks) {
            if (task.promise) {
              this.#addTask(task.name, task.promise);
            }
          }
          resolve();
        }
      };
      lazy.apiManager.on("cleanupAfterUninstall", onCleanupAfterUninstall);
    });
  }
}

/**
 * A skeleton Extension-like object, used for testing, which installs an
 * add-on via the add-on manager when startup() is called, and
 * uninstalles it on shutdown().
 *
 * @param {string} id
 * @param {nsIFile} file
 * @param {nsIURI} rootURI
 * @param {string} installType
 */
export class MockExtension {
  constructor(file, rootURI, addonData) {
    this.id = null;
    this.file = file;
    this.rootURI = rootURI;
    this.installType = addonData.useAddonManager;
    this.addonData = addonData;
    this.addon = null;

    let promiseEvent = eventName =>
      new Promise(resolve => {
        let onstartup = async (msg, extension) => {
          this.maybeSetID(extension.rootURI, extension.id);
          if (!this.id && this.addonPromise) {
            await this.addonPromise;
          }

          if (extension.id == this.id) {
            lazy.apiManager.off(eventName, onstartup);
            this._extension = extension;
            resolve(extension);
          }
        };
        lazy.apiManager.on(eventName, onstartup);
      });

    this._extension = null;
    this._extensionPromise = promiseEvent("startup");
    this._readyPromise = promiseEvent("ready");
  }

  maybeSetID(uri, id) {
    if (
      !this.id &&
      uri instanceof Ci.nsIJARURI &&
      uri.JARFile.QueryInterface(Ci.nsIFileURL).file.equals(this.file)
    ) {
      this.id = id;
    }
  }

  testMessage(...args) {
    return this._extension.testMessage(...args);
  }

  get tabManager() {
    return this._extension.tabManager;
  }

  on(...args) {
    this._extensionPromise.then(extension => {
      extension.on(...args);
    });
    // Extension.sys.mjs emits a "startup" event on |extension| before emitting the
    // "startup" event on |apiManager|. Trigger the "startup" event anyway, to
    // make sure that the MockExtension behaves like an Extension with regards
    // to the startup event.
    if (args[0] === "startup" && !this._extension) {
      this._extensionPromise.then(extension => {
        args[1]("startup", extension);
      });
    }
  }

  off(...args) {
    this._extensionPromise.then(extension => {
      extension.off(...args);
    });
  }

  _setIncognitoOverride() {
    let { addonData } = this;
    if (addonData && addonData.incognitoOverride) {
      try {
        let { id } = addonData.manifest.browser_specific_settings.gecko;
        if (id) {
          return ExtensionTestCommon.setIncognitoOverride({ id, addonData });
        }
      } catch (e) {}
      throw new Error(
        "Extension ID is required for setting incognito permission."
      );
    }
  }

  async startup() {
    await this._setIncognitoOverride();

    if (this.installType == "temporary") {
      return lazy.AddonManager.installTemporaryAddon(this.file).then(
        async addon => {
          this.addon = addon;
          this.id = addon.id;
          return this._readyPromise;
        }
      );
    } else if (this.installType == "permanent") {
      this.addonPromise = new Promise(resolve => {
        this.resolveAddon = resolve;
      });
      let install = await lazy.AddonManager.getInstallForFile(this.file);
      return new Promise((resolve, reject) => {
        let listener = {
          onInstallFailed: reject,
          onInstallEnded: async (install, newAddon) => {
            this.addon = newAddon;
            this.id = newAddon.id;
            this.resolveAddon(newAddon);
            resolve(this._readyPromise);
          },
        };

        install.addListener(listener);
        install.install();
      });
    }
    throw new Error("installType must be one of: temporary, permanent");
  }

  shutdown() {
    this.addon.uninstall();
    return this.cleanupGeneratedFile();
  }

  cleanupGeneratedFile() {
    return this._extensionPromise
      .then(extension => {
        return extension.broadcast("Extension:FlushJarCache", {
          path: this.file.path,
        });
      })
      .then(() => {
        return IOUtils.remove(this.file.path, { retryReadonly: true });
      });
  }

  /**
   * @param {object} options
   * @param {boolean} [options.expectStopped]
   * @param {boolean} [options.ignoreDevToolsAttached]
   * @param {boolean} [options.disableResetIdleForTest]
   */
  async terminateBackground({ expectStopped = true, ...rest } = {}) {
    const extension = await this._extensionPromise;
    await extension.terminateBackground(rest);
    if (expectStopped) {
      ExtensionTestAssertions.assertBackgroundStatusStopped(this);
    } else {
      ExtensionTestAssertions.assertBackgroundStatusRunning(this);
    }
  }

  wakeupBackground() {
    return this._extensionPromise.then(extension => {
      return extension.wakeupBackground();
    });
  }
}

function provide(obj, keys, value, override = false) {
  if (keys.length == 1) {
    if (!(keys[0] in obj) || override) {
      obj[keys[0]] = value;
    }
  } else {
    if (!(keys[0] in obj)) {
      obj[keys[0]] = {};
    }
    provide(obj[keys[0]], keys.slice(1), value, override);
  }
}

export var ExtensionTestCommon = class ExtensionTestCommon {
  static get testAssertions() {
    return ExtensionTestAssertions;
  }

  // Called by AddonTestUtils.promiseShutdownManager to reset startup promises
  static resetStartupPromises() {
    lazy.ExtensionParent._resetStartupPromises();
  }

  // Called to notify "browser-delayed-startup-finished", which resolves
  // ExtensionParent.browserPaintedPromise.  Thus must be resolved for
  // primed listeners to be able to wake the extension.
  static notifyEarlyStartup() {
    Services.obs.notifyObservers(null, "browser-delayed-startup-finished");
    return lazy.ExtensionParent.browserPaintedPromise;
  }

  // Called to notify "extensions-late-startup", which resolves
  // ExtensionParent.browserStartupPromise.  Normally, in Firefox, the
  // notification would be "sessionstore-windows-restored", however
  // mobile listens for "extensions-late-startup" so that is more useful
  // in testing.
  static notifyLateStartup() {
    Services.obs.notifyObservers(null, "extensions-late-startup");
    return lazy.ExtensionParent.browserStartupPromise;
  }

  /**
   * Shortcut to more easily access WebExtensionPolicy.backgroundServiceWorkerEnabled
   * from mochitest-plain tests.
   *
   * @returns {boolean} true if the background service worker are enabled.
   */
  static getBackgroundServiceWorkerEnabled() {
    return WebExtensionPolicy.backgroundServiceWorkerEnabled;
  }

  /**
   * A test helper mainly used to skip test tasks if running in "backgroundServiceWorker" test mode
   * (e.g. while running test files shared across multiple test modes: e.g. in-process-webextensions,
   * remote-webextensions, sw-webextensions etc.).
   *
   * The underlying pref "extension.backgroundServiceWorker.forceInTestExtension":
   * - is set to true in the xpcshell-serviceworker.ini and mochitest-serviceworker.ini manifests
   *   (and so it is going to be set to true while running the test files listed in those manifests)
   * - when set to true, all test extension using a background script without explicitly listing it
   *   in the test extension manifest will be automatically executed as background service workers
   *   (instead of background scripts loaded in a background page)
   *
   * @returns {boolean} true if the test is running in "background service worker mode"
   */
  static isInBackgroundServiceWorkerTests() {
    return Services.prefs.getBoolPref(
      "extensions.backgroundServiceWorker.forceInTestExtension",
      false
    );
  }

  /**
   * This code is designed to make it easy to test a WebExtension
   * without creating a bunch of files. Everything is contained in a
   * single JS object.
   *
   * Properties:
   *   "background": "<JS code>"
   *     A script to be loaded as the background script.
   *     The "background" section of the "manifest" property is overwritten
   *     if this is provided.
   *   "manifest": {...}
   *     Contents of manifest.json
   *   "files": {"filename1": "contents1", ...}
   *     Data to be included as files. Can be referenced from the manifest.
   *     If a manifest file is provided here, it takes precedence over
   *     a generated one. Always use "/" as a directory separator.
   *     Directories should appear here only implicitly (as a prefix
   *     to file names)
   *
   * To make things easier, the value of "background" and "files"[] can
   * be a function, which is converted to source that is run.
   *
   * @param {object} data
   * @returns {object}
   */
  static generateFiles(data) {
    let files = {};

    Object.assign(files, data.files);

    let manifest = data.manifest;
    if (manifest) {
      // Copy manifest so that modifications below do not affect the input.
      manifest = structuredClone(manifest);
    } else {
      manifest = {};
    }

    provide(manifest, ["name"], "Generated extension");
    provide(manifest, ["manifest_version"], 2);
    provide(manifest, ["version"], "1.0");

    // Make it easier to test same manifest in both MV2 and MV3 configurations.
    if (manifest.manifest_version === 2 && manifest.host_permissions) {
      manifest.permissions = [].concat(
        manifest.permissions || [],
        manifest.host_permissions
      );
      delete manifest.host_permissions;
    }

    if (data.useServiceWorker === undefined) {
      // If we're force-testing service workers we will turn the background
      // script part of ExtensionTestUtils test extensions into a background
      // service worker.
      data.useServiceWorker =
        ExtensionTestCommon.isInBackgroundServiceWorkerTests();
    }

    // allowInsecureRequests is a shortcut to removing upgrade-insecure-requests from default csp.
    if (data.allowInsecureRequests) {
      // upgrade-insecure-requests is only added automatically to MV3.
      // This flag is therefore not needed in MV2.
      if (manifest.manifest_version < 3) {
        throw new Error("allowInsecureRequests requires manifest_version 3");
      }
      if (manifest.content_security_policy) {
        throw new Error(
          "allowInsecureRequests cannot be used with manifest.content_security_policy"
        );
      }
      manifest.content_security_policy = {
        extension_pages: `script-src 'self'`,
      };
    }

    if (data.background) {
      let bgScript = Services.uuid.generateUUID().number + ".js";

      // If persistent is set keep the flag.
      let persistent = manifest.background?.persistent;
      let scriptKey = data.useServiceWorker
        ? ["background", "service_worker"]
        : ["background", "scripts"];
      let scriptVal = data.useServiceWorker ? bgScript : [bgScript];
      provide(manifest, scriptKey, scriptVal, true);
      provide(manifest, ["background", "persistent"], persistent);

      files[bgScript] = data.background;
    }

    provide(files, ["manifest.json"], JSON.stringify(manifest));

    for (let filename in files) {
      let contents = files[filename];
      if (typeof contents == "function") {
        files[filename] = this.serializeScript(contents);
      } else if (
        typeof contents != "string" &&
        !instanceOf(contents, "ArrayBuffer")
      ) {
        files[filename] = JSON.stringify(contents);
      }
    }

    return files;
  }

  /**
   * Write an xpi file to disk for a webextension.
   * The generated extension is stored in the system temporary directory,
   * and an nsIFile object pointing to it is returned.
   *
   * @param {object} data In the format handled by generateFiles.
   * @returns {nsIFile}
   */
  static generateXPI(data) {
    let files = this.generateFiles(data);
    return this.generateZipFile(files);
  }

  static generateZipFile(files, baseName = "generated-extension.xpi") {
    let ZipWriter = Components.Constructor(
      "@mozilla.org/zipwriter;1",
      "nsIZipWriter"
    );
    let zipW = new ZipWriter();

    let file = new lazy.FileUtils.File(
      PathUtils.join(PathUtils.tempDir, baseName)
    );
    file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, lazy.FileUtils.PERMS_FILE);

    const MODE_WRONLY = 0x02;
    const MODE_TRUNCATE = 0x20;
    zipW.open(file, MODE_WRONLY | MODE_TRUNCATE);

    // Needs to be in microseconds for some reason.
    let time = Date.now() * 1000;

    function generateFile(filename) {
      let components = filename.split("/");
      let path = "";
      for (let component of components.slice(0, -1)) {
        path += component + "/";
        if (!zipW.hasEntry(path)) {
          zipW.addEntryDirectory(path, time, false);
        }
      }
    }

    for (let filename in files) {
      let script = files[filename];
      if (!instanceOf(script, "ArrayBuffer")) {
        script = new TextEncoder().encode(script).buffer;
      }

      let stream = Cc[
        "@mozilla.org/io/arraybuffer-input-stream;1"
      ].createInstance(Ci.nsIArrayBufferInputStream);
      stream.setData(script, 0, script.byteLength);

      generateFile(filename);
      zipW.addEntryStream(filename, time, 0, stream, false);
    }

    zipW.close();

    return file;
  }

  /**
   * Properly serialize a function into eval-able code string.
   *
   * @param {Function} script
   * @returns {string}
   */
  static serializeFunction(script) {
    // Serialization of object methods doesn't include `function` anymore.
    const method = /^(async )?(?:(\w+)|"(\w+)\.js")\(/;

    let code = script.toString();
    let match = code.match(method);
    if (match && match[2] !== "function") {
      code = code.replace(method, "$1function $2$3(");
    }
    return code;
  }

  /**
   * Properly serialize a script into eval-able code string.
   *
   * @param {string | Function | Array} script
   * @returns {string}
   */
  static serializeScript(script) {
    if (Array.isArray(script)) {
      return Array.from(script, this.serializeScript, this).join(";");
    }
    if (typeof script !== "function") {
      return script;
    }
    return `(${this.serializeFunction(script)})();`;
  }

  static setIncognitoOverride(extension) {
    let { id, addonData } = extension;
    if (!addonData || !addonData.incognitoOverride) {
      return;
    }
    if (addonData.incognitoOverride == "not_allowed") {
      return lazy.ExtensionPermissions.remove(id, {
        permissions: ["internal:privateBrowsingAllowed"],
        origins: [],
      });
    }
    return lazy.ExtensionPermissions.add(id, {
      permissions: ["internal:privateBrowsingAllowed"],
      origins: [],
    });
  }

  static setExtensionID(data) {
    try {
      if (data.manifest.browser_specific_settings.gecko.id) {
        return;
      }
    } catch (e) {
      // No ID is set.
    }
    provide(
      data,
      ["manifest", "browser_specific_settings", "gecko", "id"],
      Services.uuid.generateUUID().number
    );
  }

  /**
   * Generates a new extension using |Extension.generateXPI|, and initializes a
   * new |Extension| instance which will execute it.
   *
   * @param {object} data
   * @returns {Partial<Extension>}
   */
  static generate(data) {
    if (data.useAddonManager === "geckoview-only") {
      // Some extension APIs are partially implemented in Java, and the
      // interface between the JS and Java side (GeckoViewWebExtension)
      // expects extensions to be registered with the AddonManager.
      // This is at least necessary for tests that use the following APIs:
      //   - browserAction/pageAction.
      //   - tabs.create, tabs.update, tabs.remove (uses GeckoViewTabBridge).
      //   - downloads API
      //   - browsingData API (via ExtensionBrowsingData.sys.mjs).
      //
      // In xpcshell tests, the AddonManager is optional, so the AddonManager
      // cannot unconditionally be enabled.
      // In mochitests, tests are run in an actual browser, so the AddonManager
      // is always enabled and hence useAddonManager is always set by default.
      if (AppConstants.MOZ_GECKOVIEW) {
        // Many MV3 tests set temporarilyInstalled for granted_host_permissions.
        // The granted_host_permissions flag is only effective for temporarily
        // installed extensions, so make sure to use "temporary" in this case.
        if (data.temporarilyInstalled) {
          data.useAddonManager = "temporary";
        } else {
          data.useAddonManager = "permanent";
        }
        // MockExtension requires data.manifest.applications.gecko.id to be set.
        // The AddonManager requires an ID in the manifest for unsigned XPIs.
        this.setExtensionID(data);
      } else {
        // On non-GeckoView, default to not using the AddonManager.
        data.useAddonManager = null;
      }
    }

    let file = this.generateXPI(data);

    flushJarCache(file.path);
    Services.ppmm.broadcastAsyncMessage("Extension:FlushJarCache", {
      path: file.path,
    });

    let fileURI = Services.io.newFileURI(file);
    let jarURI = Services.io.newURI("jar:" + fileURI.spec + "!/");

    // This may be "temporary" or "permanent".
    if (data.useAddonManager) {
      return new MockExtension(file, jarURI, data);
    }

    let id;
    if (data.manifest) {
      if (data.manifest.applications && data.manifest.applications.gecko) {
        id = data.manifest.applications.gecko.id;
      } else if (
        data.manifest.browser_specific_settings &&
        data.manifest.browser_specific_settings.gecko
      ) {
        id = data.manifest.browser_specific_settings.gecko.id;
      }
    }
    if (!id) {
      id = Services.uuid.generateUUID().number;
    }

    let signedState = lazy.AddonManager.SIGNEDSTATE_SIGNED;
    if (data.isPrivileged) {
      signedState = lazy.AddonManager.SIGNEDSTATE_PRIVILEGED;
    }
    if (data.isSystem) {
      signedState = lazy.AddonManager.SIGNEDSTATE_SYSTEM;
    }

    let isPrivileged = lazy.ExtensionData.getIsPrivileged({
      signedState,
      builtIn: false,
      temporarilyInstalled: !!data.temporarilyInstalled,
    });

    return new lazy.Extension(
      {
        id,
        resourceURI: jarURI,
        cleanupFile: file,
        signedState,
        incognitoOverride: data.incognitoOverride,
        temporarilyInstalled: !!data.temporarilyInstalled,
        isPrivileged,
        TEST_NO_ADDON_MANAGER: true,
        // By default we set TEST_NO_DELAYED_STARTUP to true
        TEST_NO_DELAYED_STARTUP: !data.delayedStartup,
      },
      data.startupReason ?? "ADDON_INSTALL"
    );
  }

  /**
   * Unload an extension and await completion of post-uninstall cleanup tasks.
   *
   * @param {Extension|MockExtension} extension
   */
  static async unloadTestExtension(extension) {
    const { id } = extension;
    const uninstallTracker = new ExtensionUninstallTracker(id);
    await extension.shutdown();
    if (extension instanceof lazy.Extension) {
      // AddonManager-managed add-ons run additional (cleanup) tasks after
      // shutting down an extension. Do the same even without useAddonManager.
      lazy.Extension.getBootstrapScope().uninstall({ id });

      // Data removal by ExtensionAddonObserver.onUninstalled:
      lazy.ExtensionAddonObserver.clearOnUninstall(id);
    }
    await uninstallTracker.waitForUninstallCleanupDone();
  }
};
