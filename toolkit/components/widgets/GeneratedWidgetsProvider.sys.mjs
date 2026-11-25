/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  Extension: "resource://gre/modules/Extension.sys.mjs",
  ExtensionAddonObserver: "resource://gre/modules/Extension.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  GeneratedWidgetExtension:
    "moz-src:///toolkit/components/widgets/GeneratedWidgetExtension.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enabled",
  "extensions.widgets.generated.enabled",
  false,
  (_pref, wasEnabled, enabled) => {
    if (!AppConstants.NIGHTLY_BUILD) {
      return;
    }
    if (enabled && !wasEnabled) {
      lazy.AddonManagerPrivate.registerProvider(GeneratedWidgetsProvider, [
        ADDON_TYPE,
      ]);
    } else if (wasEnabled && !enabled) {
      lazy.AddonManagerPrivate.unregisterProvider(GeneratedWidgetsProvider);
    }
  }
);

const ADDON_TYPE = "generatedwidget";
const STORE = "widgets.json.mozlz4";
const ID_SUFFIX = "@generated-widget.mozilla.org";
const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const VERSION_PATTERN = /^[1-9][0-9]*$/;
/** The longest name that GeneratedWidgetManifest in builtin_widget.json accepts. */
const MAX_NAME_LENGTH = 75;

export function widgetsDirectory() {
  return PathUtils.join(PathUtils.profileDir, "generated-widgets");
}

function storePath() {
  return PathUtils.join(widgetsDirectory(), STORE);
}

function packagePath(id, version) {
  return PathUtils.join(widgetsDirectory(), `${id}-${version}.zip`);
}

function packageURI(path) {
  return Services.io.newURI(`jar:${PathUtils.toFileURI(path)}!/`);
}

/**
 * Resolves a package path, following symlinks, and checks that it is a file in
 * the profile directory.
 *
 * @param {string} path - the path of a widget's zip file
 * @returns {string} the resolved path
 * @throws {Error} when the resolved path is not a file in the profile directory
 */
function resolveSource(path) {
  let file = new lazy.FileUtils.File(path);
  file.normalize();
  let profile = new lazy.FileUtils.File(PathUtils.profileDir);
  profile.normalize();
  if (!profile.contains(file) || !file.isFile()) {
    throw new Error(`${path} is not a file in the profile directory`);
  }
  return file.path;
}

function checkPolicy(id) {
  if (
    Services.policies &&
    !Services.policies.mayInstallAddon({
      id,
      type: "extension",
      permissions: lazy.GeneratedWidgetExtension.PERMISSIONS,
    })
  ) {
    throw new Error(`Generated widget ${id} is blocked by enterprise policy`);
  }
}

function checkInstall(id) {
  if (!Services.prefs.getBoolPref("xpinstall.enabled", true)) {
    throw new Error("Add-on installation is disabled");
  }
  checkPolicy(id);
}

/**
 * A widget's zip file, its version, its name and prompt, and its dates.
 */
class GeneratedWidgetPackage {
  /** @type {?GeneratedWidgetAddonWrapper} */
  #wrapper = null;

  /**
   * @param {object} details
   * @param {string} details.id - the widget's extension id
   * @param {string} details.version - the package's version
   * @param {string} [details.name] - the widget's name, set by readWidget()
   *   when missing
   * @param {string} [details.prompt] - the prompt the widget was generated
   *   from, set by readWidget() when missing
   * @param {number} [details.installDate] - when the widget was installed
   * @param {number} [details.updateDate] - when the widget was last updated
   */
  constructor({ id, version, name, prompt, installDate, updateDate }) {
    this.id = id;
    this.version = version;
    this.name = name;
    this.prompt = prompt;
    this.installDate = installDate;
    this.updateDate = updateDate;
  }

  /**
   * Creates a package from its store entry.
   *
   * @param {string} id - the widget's extension id
   * @param {?object} entry - the package's entry in the store file
   * @param {string} entry.version - the package's version
   * @param {string} entry.name - the widget's name
   * @param {string} entry.prompt - the prompt the widget was generated from
   * @param {number} entry.installDate - when the widget was installed
   * @param {number} entry.updateDate - when the widget was last updated
   * @returns {?GeneratedWidgetPackage} the package, or null when the entry is
   *   invalid
   */
  static fromJSON(id, entry) {
    if (!entry) {
      return null;
    }
    let { version, name, prompt, installDate, updateDate } = entry;
    let widgetPackage = new GeneratedWidgetPackage({
      id,
      version,
      name,
      prompt,
      installDate,
      updateDate,
    });
    if (!widgetPackage.isValid) {
      return null;
    }
    return widgetPackage;
  }

  /**
   * Validates the package.
   *
   * @type {boolean}
   */
  get isValid() {
    return (
      this.id.endsWith(ID_SUFFIX) &&
      UUID_PATTERN.test(this.id.slice(0, -ID_SUFFIX.length)) &&
      typeof this.version === "string" &&
      VERSION_PATTERN.test(this.version) &&
      typeof this.name === "string" &&
      typeof this.prompt === "string" &&
      Number.isFinite(this.installDate) &&
      Number.isFinite(this.updateDate)
    );
  }

  get path() {
    return packagePath(this.id, this.version);
  }

  get resourceURI() {
    return packageURI(this.path);
  }

  /**
   * The package's add-on, created once.
   *
   * @type {GeneratedWidgetAddonWrapper}
   */
  get wrapper() {
    this.#wrapper ??= new GeneratedWidgetAddonWrapper(this);
    return this.#wrapper;
  }

  async remove() {
    await IOUtils.remove(this.path, { ignoreAbsent: true }).catch(
      console.error
    );
  }

  flushJarCache() {
    lazy.ExtensionUtils.flushJarCache(this.path);
  }

  /**
   * Reads the widget's name and prompt from the package's widget.json.
   *
   * @throws {Error} when widget.json can not be read, or its name or prompt is
   *   not a string
   */
  async readWidget() {
    let widget;
    try {
      widget = await new lazy.ExtensionData(this.resourceURI).readJSON(
        "widget.json"
      );
    } finally {
      this.flushJarCache();
    }
    if (typeof widget?.name !== "string" || typeof widget.prompt !== "string") {
      throw new Error(`Generated widget ${this.id} has an invalid widget.json`);
    }
    this.name = widget.name;
    this.prompt = widget.prompt;
  }

  toJSON() {
    return {
      version: this.version,
      name: this.name,
      prompt: this.prompt,
      installDate: this.installDate,
      updateDate: this.updateDate,
    };
  }
}

/**
 * Exposes a generated widget through the AddonManager interface.
 */
class GeneratedWidgetAddonWrapper {
  #package;

  /**
   * @param {GeneratedWidgetPackage} widgetPackage - the widget's package
   */
  constructor(widgetPackage) {
    this.id = widgetPackage.id;
    this.installDate = new Date(widgetPackage.installDate);
    this.updateDate = new Date(widgetPackage.updateDate);
    this.#package = widgetPackage;
  }

  /**
   * The widget's name, cut to MAX_NAME_LENGTH characters.
   *
   * @type {string}
   */
  get name() {
    return this.#package.name.slice(0, MAX_NAME_LENGTH);
  }

  get description() {
    return this.#package.prompt;
  }

  get type() {
    return ADDON_TYPE;
  }

  get version() {
    return this.#package.version;
  }

  get isActive() {
    return GeneratedWidgetsProvider.isRunning(this.id);
  }

  get isCompatible() {
    return true;
  }

  get permissions() {
    return lazy.AddonManager.PERM_CAN_UNINSTALL;
  }

  get installTelemetryInfo() {
    return { source: "internal" };
  }

  uninstall() {
    return GeneratedWidgetsProvider.uninstall(this.id);
  }

  reload() {
    return GeneratedWidgetsProvider.reload(this.id);
  }
}

/**
 * Add-on manager provider for generated widgets. Each widget is an
 * unprivileged extension stored as `<id>-<version>.zip` in the profile's
 * generated-widgets directory, and listed in widgets.json.mozlz4.
 */
class _GeneratedWidgetsProvider {
  /** @type {Map<string, GeneratedWidgetPackage>} */
  #store = new Map();
  /** @type {Map<string, Extension>} */
  #widgets = new Map();
  #started = false;
  #queue = Promise.resolve();
  /** Aborted when the provider shuts down. */
  #session = new AbortController();
  /**
   * Aborted when a widget is uninstalled.
   *
   * @type {Map<string, AbortController>}
   */
  #uninstalling = new Map();
  /** @type {?Promise<void>} */
  #storeReady = null;
  /** @type {?Promise<void>} */
  #startupPromise = null;
  /** @type {?Promise<void>} */
  #shutdownPromise = null;

  get name() {
    return "GeneratedWidgetsProvider";
  }

  /**
   * Reads the store file and launches every stored widget.
   *
   * @returns {?Promise<void>} resolves once every widget has started or failed
   *   to start
   */
  startup() {
    if (this.#started || Services.appinfo.inSafeMode) {
      return this.#startupPromise;
    }
    this.#started = true;
    this.#session = new AbortController();
    this.#storeReady = this.#enqueue(async () => {
      this.#store = await this.#readStore();
    }).catch(console.error);
    this.#startupPromise = this.#enqueue(
      () => this.#launchAll(),
      this.#session.signal
    );
    this.#startupPromise.catch(error => {
      if (error.name !== "AbortError") {
        console.error(error);
      }
    });
    return this.#startupPromise;
  }

  shutdown() {
    if (!this.#started) {
      return this.#shutdownPromise;
    }
    this.#started = false;
    this.#session.abort();
    let reason = Services.startup.shuttingDown
      ? "APP_SHUTDOWN"
      : "ADDON_DISABLE";
    this.#shutdownPromise = this.#enqueue(() => this.#stopAll(reason));
    return this.#shutdownPromise;
  }

  /**
   * @param {string} id - the widget's extension id
   * @returns {boolean} whether the widget's extension is running
   */
  isRunning(id) {
    let extension = this.#widgets.get(id);
    return !!extension && !extension.hasShutdown;
  }

  async getAddonByID(id) {
    await this.#storeReady;
    return this.#store.has(id) ? this.#addon(id) : null;
  }

  async getAddonsByTypes(types) {
    if (!types?.includes(ADDON_TYPE)) {
      return [];
    }
    await this.#storeReady;
    return Array.from(this.#store.keys(), id => this.#addon(id));
  }

  /**
   * Installs a generated widget from its package and starts it under a new id
   * at version 1. The package file moves into the generated-widgets directory.
   *
   * @param {string} path - the path of the widget's zip file in the profile
   *   directory
   * @returns {Promise<GeneratedWidgetAddonWrapper>} the widget's add-on
   * @throws {Error} when the resolved path is not a file in the profile
   *   directory
   */
  async install(path) {
    let addon = await this.#enqueue(() => this.#install(path));
    lazy.AddonManagerPrivate.callAddonListeners("onInstalled", addon);
    return addon;
  }

  /**
   * Upgrades a running generated widget to a new package at the next version.
   * The previous version restarts if the upgrade fails.
   *
   * @param {string} id - the widget's extension id
   * @param {string} path - the path of the widget's zip file in the profile
   *   directory
   * @returns {Promise<GeneratedWidgetAddonWrapper>} the widget's add-on
   * @throws {Error} when the resolved path is not a file in the profile
   *   directory
   */
  async update(id, path) {
    let signal = this.#uninstallSignal(id);
    let addon = await this.#enqueue(
      () => this.#update(id, path, signal),
      AbortSignal.any([this.#session.signal, signal])
    );
    lazy.AddonManagerPrivate.callAddonListeners("onInstalled", addon);
    return addon;
  }

  /**
   * Stops a generated widget if it is running and deletes its package.
   * Pending launches, updates and reloads of the widget are aborted.
   *
   * @param {string} id - the widget's extension id
   * @returns {Promise<void>}
   */
  uninstall(id) {
    this.#uninstalling.get(id)?.abort();
    this.#uninstalling.delete(id);
    return this.#enqueue(() => this.#uninstall(id));
  }

  /**
   * Stops a running generated widget and starts it again.
   *
   * @param {string} id - the widget's extension id
   * @returns {Promise<void>}
   */
  reload(id) {
    let signal = this.#uninstallSignal(id);
    return this.#enqueue(
      () => this.#reload(id, signal),
      AbortSignal.any([this.#session.signal, signal])
    );
  }

  /**
   * @param {string} id - the widget's extension id
   * @returns {AbortSignal} aborted when the widget is uninstalled
   */
  #uninstallSignal(id) {
    let controller = this.#uninstalling.get(id);
    if (!controller) {
      controller = new AbortController();
      this.#uninstalling.set(id, controller);
    }
    return controller.signal;
  }

  /**
   * Runs a task once every earlier task has settled.
   *
   * @param {Function} task - the task to run
   * @param {AbortSignal} [signal] - skips the task when aborted before it runs
   * @returns {Promise<any>} the task's result
   */
  #enqueue(task, signal) {
    let result = this.#queue.then(() => {
      signal?.throwIfAborted();
      return task();
    });
    this.#queue = result.catch(() => {});
    return result;
  }

  async #saveStore() {
    await IOUtils.makeDirectory(widgetsDirectory());
    await IOUtils.writeJSON(storePath(), Object.fromEntries(this.#store), {
      compress: true,
      tmpPath: `${storePath()}.tmp`,
    });
  }

  async #install(path) {
    if (!this.#started) {
      throw new Error("The generated widgets provider is stopped");
    }
    let source = resolveSource(path);
    let id = `${Services.uuid.generateUUID().toString().slice(1, -1)}${ID_SUFFIX}`;
    checkInstall(id);
    let now = Date.now();
    let widgetPackage = new GeneratedWidgetPackage({
      id,
      version: "1",
      installDate: now,
      updateDate: now,
    });
    await IOUtils.move(source, widgetPackage.path, { noOverwrite: true });
    try {
      await widgetPackage.readWidget();
      this.#store.set(id, widgetPackage);
      await this.#saveStore();
      lazy.AddonManagerPrivate.callAddonListeners(
        "onInstalling",
        this.#addon(id),
        false
      );
      await this.#launch(id, "ADDON_INSTALL");
    } catch (error) {
      this.#store.delete(id);
      await this.#saveStore().catch(console.error);
      lazy.Extension.getBootstrapScope().uninstall({ id });
      lazy.ExtensionAddonObserver.clearOnUninstall(id);
      await widgetPackage.remove();
      throw error;
    }
    return this.#addon(id);
  }

  async #uninstall(id) {
    let widgetPackage = this.#store.get(id);
    if (!widgetPackage) {
      return;
    }
    let extension = this.#widgets.get(id);
    if (extension) {
      extension.uninstallURL = null;
    }
    let addon = this.#addon(id);
    lazy.AddonManagerPrivate.callAddonListeners("onUninstalling", addon, false);
    await this.#stop(id, "ADDON_UNINSTALL");
    this.#store.delete(id);
    await this.#saveStore();
    lazy.Extension.getBootstrapScope().uninstall({ id });
    await widgetPackage.remove();
    lazy.AddonManagerPrivate.callAddonListeners("onUninstalled", addon);
  }

  /**
   * Restarts a running generated widget at the next version. The previous
   * version restarts if the upgrade fails and is not aborted.
   *
   * @param {string} id - the widget's extension id
   * @param {string} path - the path of the new package
   * @param {AbortSignal} signal - aborted when the widget is uninstalled
   * @returns {Promise<GeneratedWidgetAddonWrapper>} the widget's add-on
   */
  async #update(id, path, signal) {
    if (!this.#widgets.has(id)) {
      throw new Error(`Generated widget ${id} is not running`);
    }
    let source = resolveSource(path);
    checkInstall(id);
    let previous = this.#store.get(id);
    let next = new GeneratedWidgetPackage({
      id,
      version: String(Number(previous.version) + 1),
      installDate: previous.installDate,
      updateDate: Date.now(),
    });
    await IOUtils.move(source, next.path);
    try {
      await next.readWidget();
    } catch (error) {
      await next.remove();
      throw error;
    }
    lazy.AddonManagerPrivate.callAddonListeners(
      "onInstalling",
      next.wrapper,
      false
    );
    await this.#stop(id, "ADDON_UPGRADE");
    this.#store.set(id, next);
    try {
      await this.#launch(id, "ADDON_UPGRADE", signal);
    } catch (error) {
      this.#store.set(id, previous);
      await next.remove();
      if (!signal.aborted) {
        await this.#launch(id, "ADDON_DOWNGRADE").catch(console.error);
      }
      throw error;
    }
    await this.#saveStore();
    await previous.remove();
    return next.wrapper;
  }

  async #reload(id, signal) {
    if (!this.#widgets.has(id)) {
      throw new Error(`Generated widget ${id} is not running`);
    }
    await this.#stop(id, "ADDON_DISABLE");
    await this.#launch(id, "ADDON_ENABLE", signal);
  }

  /**
   * Stops a generated widget if it is running and releases its package file.
   *
   * @param {string} id - the widget's extension id
   * @param {string} reason - the extension's shutdown reason
   */
  async #stop(id, reason) {
    let extension = this.#widgets.get(id);
    if (!extension) {
      return;
    }
    await extension.shutdown(reason);
    this.#widgets.delete(id);
    if (reason !== "APP_SHUTDOWN") {
      await extension.broadcast("Extension:FlushJarCache", {
        path: this.#store.get(id).path,
      });
    }
  }

  /**
   * Reads the store file, dropping invalid entries.
   *
   * @returns {Promise<Map<string, GeneratedWidgetPackage>>} the packages, by
   *   id, empty when the store is missing or unreadable
   */
  async #readStore() {
    let entries;
    try {
      entries = Object.entries(
        await IOUtils.readJSON(storePath(), { decompress: true })
      );
    } catch (error) {
      if (error.name !== "NotFoundError") {
        console.error(error);
      }
      return new Map();
    }
    let packages = new Map();
    for (let [id, entry] of entries) {
      let widgetPackage = GeneratedWidgetPackage.fromJSON(id, entry);
      if (widgetPackage) {
        packages.set(id, widgetPackage);
      }
    }
    return packages;
  }

  async #launchAll() {
    let launches = Array.from(this.#store.keys(), id =>
      this.#launch(
        id,
        "APP_STARTUP",
        AbortSignal.any([this.#session.signal, this.#uninstallSignal(id)])
      ).catch(error => {
        if (error.name !== "AbortError") {
          console.error(error);
        }
      })
    );
    await Promise.all(launches);
  }

  async #stopAll(reason) {
    let stops = Array.from(this.#widgets.keys(), id => this.#stop(id, reason));
    let results = await Promise.allSettled(stops);
    let errors = results
      .filter(result => result.status === "rejected")
      .map(result => result.reason);
    if (errors.length) {
      throw new AggregateError(errors, "Generated widgets failed to shut down");
    }
  }

  #addon(id) {
    return this.#store.get(id)?.wrapper;
  }

  /**
   * Starts a generated widget. An aborted launch stops the widget once its
   * startup settles.
   *
   * @param {string} id - the widget's extension id
   * @param {string} reason - the extension's startup reason
   * @param {AbortSignal} [signal] - aborts the launch
   */
  async #launch(id, reason, signal) {
    signal?.throwIfAborted();
    if (this.#widgets.has(id)) {
      throw new Error(`Generated widget ${id} is already running`);
    }
    checkPolicy(id);
    let widgetPackage = this.#store.get(id);
    let extension = new lazy.GeneratedWidgetExtension(
      {
        id,
        version: widgetPackage.version,
        resourceURI: widgetPackage.resourceURI,
        generatedWidget: {
          name: widgetPackage.wrapper.name,
          prompt: widgetPackage.prompt,
        },
        isPrivileged: false,
        temporarilyInstalled: false,
        locationHidden: true,
      },
      reason
    );
    this.#widgets.set(id, extension);
    try {
      await extension.startup();
      signal?.throwIfAborted();
    } catch (error) {
      if (this.#widgets.get(id) === extension) {
        this.#widgets.delete(id);
      }
      if (!extension.hasShutdown) {
        await extension.shutdown().catch(console.error);
      }
      widgetPackage.flushJarCache();
      throw error;
    }
  }
}

export const GeneratedWidgetsProvider = new _GeneratedWidgetsProvider();

if (AppConstants.NIGHTLY_BUILD && lazy.enabled) {
  lazy.AddonManagerPrivate.registerProvider(GeneratedWidgetsProvider, [
    ADDON_TYPE,
  ]);
}
