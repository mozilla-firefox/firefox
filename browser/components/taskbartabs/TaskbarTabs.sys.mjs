/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file represents the entry point into the Taskbar Tabs system,
 * initializing necessary subsystems before the export can be used. Code driving
 * the Taskbar Tabs systems should interact with it through this interface.
 */

import { TaskbarTabsRegistryStorage } from "resource:///modules/taskbartabs/TaskbarTabsRegistry.sys.mjs";
import { TaskbarTabsWindowManager } from "resource:///modules/taskbartabs/TaskbarTabsWindowManager.sys.mjs";
import { TaskbarTabsPin } from "resource:///modules/taskbartabs/TaskbarTabsPin.sys.mjs";
import { TaskbarTabsUtils } from "resource:///modules/taskbartabs/TaskbarTabsUtils.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ManifestIcons: "moz-src:///dom/manifest/ManifestIcons.sys.mjs",
  ManifestObtainer: "moz-src:///dom/manifest/ManifestObtainer.sys.mjs",
  ManifestProcessor: "moz-src:///dom/manifest/ManifestProcessor.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * A Taskbar Tabs singleton which ensures the system has been initialized before
 * it can be interacted with. Methods on this object pass through to the Taskbar
 * Tabs registry or window manager.
 */
export const TaskbarTabs = new (class {
  #ready;
  #registry;
  #windowManager;

  constructor() {
    this.#ready = initRegistry().then(registry => {
      this.#registry = registry;
      this.#windowManager = initWindowManager(registry);

      this.#updateMetrics();
    });
  }

  #updateMetrics() {
    Glean.webApp.installedWebAppCount.set(this.#registry.countTaskbarTabs());
  }

  async waitUntilReady() {
    await this.#ready;
  }

  async getTaskbarTab(...args) {
    await this.#ready;
    return this.#registry.getTaskbarTab(...args);
  }

  /**
   * Finds an existing Taskbar Tab that matches aUrl within aUserContextId. If
   * one does not exist, it is created.
   *
   * Additionally, this will register the Taskbar Tab with the system and
   * request to create/pin the shortcut as applicable.
   *
   * @param {nsIURL} aUrl - The URL to create a Taskbar Tab for.
   * @param {number} aUserContextId - The container to create the Taskbar Tab
   * in.
   * @param {object} aDetails - Additional parameters for the Taskbar Tab. See
   * TaskbarTabsRegistry.findOrCreateTaskbarTab for other members.
   * @param {nsIURL} [aDetails.creatingForUrl] - The page that the Taskbar Tab
   * was created on. This allows getting the favicon of that page if there
   * isn't a better option.
   * @param {boolean} [aDetails.ensurePinned] - Whether pinning should be
   * attempted even if the Taskbar Tab already exists.
   * @param {DOMWindow?} [aDetails.window] - The window to associate any UI
   * with, if applicable.
   */
  async findOrCreateTaskbarTab(aUrl, aUserContextId, aDetails = {}) {
    if (aDetails.manifest) {
      // The manifest internally needs to be processed, so do that now.
      aDetails = {
        ...aDetails,
        manifest: lazy.ManifestProcessor.process({
          jsonText: JSON.stringify(aDetails.manifest),
          manifestURL: aUrl.prePath,
          docURL: aUrl.prePath,
        }),
      };
    }

    // The result of #findOrCreateTaskbarTab sometimes contains additional
    // properties for internal use, often for moveTabIntoTaskbarTab. Only a few
    // values should actually be given to outside callers.
    let result = await this.#findOrCreateTaskbarTab(
      aUrl,
      aUserContextId,
      aDetails
    );

    if (result.created || aDetails.ensurePinned) {
      // Don't wait for the pinning to complete.
      this.#pinTaskbarTab(result.taskbarTab, result.icon, {
        window: aDetails.window ?? null,
      });
    }

    return {
      created: result.created,
      taskbarTab: result.taskbarTab,
    };
  }

  // Used internally; can expose non-public members in its result.
  async #findOrCreateTaskbarTab(aUrl, aUserContextId, aDetails = {}) {
    await this.#ready;

    if (!aDetails.manifest?.name && aUrl.scheme === "moz-extension") {
      aDetails.manifest = {
        ...aDetails.manifest,
        name: WebExtensionPolicy.getByURI(aUrl)?.name,
      };
    }

    let result = this.#registry.findOrCreateTaskbarTab(
      aUrl,
      aUserContextId,
      aDetails
    );

    if (result.created) {
      this.#updateMetrics();

      let icon = await fetchIconForTaskbarTab(result.taskbarTab, aDetails);
      result.icon = icon;
    } else {
      result.icon = await loadSavedTaskbarTabIcon(result.taskbarTab.id);
    }

    return result;
  }

  async #pinTaskbarTab(aTaskbarTab, aIcon, aDetails) {
    let result = await TaskbarTabsPin.pinTaskbarTab(
      aTaskbarTab,
      aIcon,
      aDetails
    );
    this.#registry.patchTaskbarTab(aTaskbarTab, {
      shortcutRelativePath: result,
    });
    return result;
  }

  async findTaskbarTab(...args) {
    await this.#ready;
    return this.#registry.findTaskbarTab(...args);
  }

  async countTaskbarTabs() {
    await this.#ready;
    return this.#registry.countTaskbarTabs();
  }

  /**
   * Moves an existing tab into a new Taskbar Tab window.
   *
   * If there is already a Taskbar Tab for the tab's selected URL and container,
   * opens the existing Taskbar Tab. If not, a new Taskbar Tab is created.
   *
   * @param {MozTabbrowserTab} aTab - The tab to move into a Taskbar Tab window.
   * @returns {{window: DOMWindow, taskbarTab: TaskbarTab}} The created window
   * and the Taskbar Tab it is associated with.
   */
  async moveTabIntoTaskbarTab(aTab) {
    const browser = aTab.linkedBrowser;
    let url = browser.currentURI;
    let userContextId = aTab.userContextId;

    let [, manifest] = await Promise.all([
      this.#ready,
      lazy.ManifestObtainer.browserObtainManifest(browser).catch(e => {
        lazy.logConsole.error(e);
        return {};
      }),
    ]);

    let { taskbarTab, icon, created } = await this.#findOrCreateTaskbarTab(
      url,
      userContextId,
      {
        // 'manifest' can be null if the site doesn't have a manifest.
        ...(manifest ? { manifest } : {}),
        creatingForUrl: url,
        browser,
      }
    );

    let win = await this.#windowManager.replaceTabWithWindow(
      taskbarTab,
      aTab,
      icon
    );

    if (created) {
      // Don't wait for pinning to complete. (This is separate so we can call
      // it with the newly-created window.)
      this.#pinTaskbarTab(taskbarTab, icon, win);
    }

    return {
      created,
      taskbarTab,
      window: win,
    };
  }

  async resetForTests(...args) {
    await this.#ready;
    return this.#registry.resetForTests(...args);
  }

  async removeTaskbarTab(...args) {
    await this.#ready;

    let taskbarTab = this.#registry.removeTaskbarTab(...args);
    this.#updateMetrics();

    // Don't wait for unpinning to finish.
    TaskbarTabsPin.unpinTaskbarTab(taskbarTab);
  }

  async openWindow(aTaskbarTab) {
    await this.#ready;

    let icon = await loadSavedTaskbarTabIcon(aTaskbarTab.id);
    return this.#windowManager.openWindow(aTaskbarTab, icon);
  }

  async replaceTabWithWindow(aTaskbarTab, aTab) {
    await this.#ready;

    let icon = await loadSavedTaskbarTabIcon(aTaskbarTab.id);
    return this.#windowManager.replaceTabWithWindow(aTaskbarTab, aTab, icon);
  }

  async ejectWindow(...args) {
    await this.#ready;
    return this.#windowManager.ejectWindow(...args);
  }

  async getCountForId(...args) {
    await this.#ready;
    return this.#windowManager.getCountForId(...args);
  }
})();

/**
 * Taskbar Tabs Registry initialization.
 *
 * @returns {TaskbarTabsRegistry} A registry after loading and hooking saving to persistent storage.
 */
async function initRegistry() {
  const kRegistryFilename = "taskbartabs.json";
  // Construct the path [Profile]/taskbartabs/taskbartabs.json.
  let registryFile = TaskbarTabsUtils.getTaskbarTabsFolder();
  registryFile.append(kRegistryFilename);

  return await new TaskbarTabsRegistryStorage(registryFile).load();
}

/**
 * Taskbar Tabs Window Manager initialization.
 *
 * @returns {TaskbarTabsWindowManager} The initialized Window Manager
 */
function initWindowManager() {
  let wm = new TaskbarTabsWindowManager();

  return wm;
}

async function fetchIconForTaskbarTab(aTaskbarTab, aDetails) {
  let startUri = Services.io.newURI(aTaskbarTab.startUrl);
  const choices = [
    async () => {
      if (!aDetails.manifest) {
        return null;
      }

      if (aDetails.browser) {
        let uri = await lazy.ManifestIcons.browserFetchIcon(
          aDetails.browser,
          aDetails.manifest,
          256,
          ["any"]
        );
        return Services.io.newURI(uri);
      }

      return Services.io.newURI(findBestManifestIcon(aDetails.manifest));
    },
    async () => await TaskbarTabsUtils.getFaviconUri(startUri),
    async () => await TaskbarTabsUtils.getFaviconUri(aDetails.createdForUrl),
  ];

  for (const choice of choices) {
    try {
      let uri = await choice();
      if (!uri) {
        continue;
      }
      let candidate = await TaskbarTabsUtils._remoteDecodeImageFromURI(
        uri,
        256,
        aDetails.browser
      );
      if (candidate) {
        return candidate;
      }
    } catch (e) {
      lazy.logConsole.warn("Could not load Taskbar Tab icon: ", e);
    }
  }

  lazy.logConsole.warn("Falling back to default Taskbar Tab icon.");
  return await TaskbarTabsUtils.getDefaultIcon();
}

/**
 * Looks up the saved icon for a Taskbar Tab on disk.
 *
 * @param {string} aTaskbarTabId - The ID of the Taskbar Tab to look up.
 * @returns {imgIContainer} The icon saved on disk.
 */
async function loadSavedTaskbarTabIcon(aTaskbarTabId) {
  let iconPath = TaskbarTabsUtils.getTaskbarTabsFolder();
  iconPath.append("icons");
  iconPath.append(
    aTaskbarTabId + "." + lazy.ShellService.shortcutIconType.extension
  );

  try {
    return await TaskbarTabsUtils._remoteDecodeImageFromFile(
      iconPath,
      256,
      null,
      lazy.ShellService.shortcutIconType.mimeType
    );
  } catch (e) {
    lazy.logConsole.warn("Couldn't load saved Taskbar Tab icon: ", e);
    return await TaskbarTabsUtils.getDefaultIcon();
  }
}

/**
 * This is a lightweight version of ManifestIcons.sys.mjs that runs in the
 * parent process. Selects the icon that is closest to 256x256 from the
 * processed manifest, and returns its URL.
 *
 * Note that this currently doesn't try to fall back if it fails---it'll use
 * the site's favicon, if any, or the default icon.
 *
 * @param {object} aManifest - The manifest, as returned by ManifestObtainer or
 * ManifestProcessor.
 * @returns {string?} The URL of the best icon, or null if no icons were found.
 */
function findBestManifestIcon(aManifest) {
  // Transform the icons from
  //   [{sizes: ["WIDTH1xHEIGHT1", "WIDTH2xHEIGHT2"], src: "...", ...}, ...]
  // to
  //   [{size: WIDTH1, src: "..."}, {size: WIDTH2, src: "..."}]
  let collectedIcons =
    aManifest.icons?.flatMap(icon => {
      // Discard any that have non-"any" purpose. (Mainly for symmetry within
      // the tests, this case shouldn't really happen.)
      if (!icon.purpose.includes("any")) {
        return [];
      }

      let sizes = icon.sizes ?? [""];
      return sizes.map(size => ({
        src: icon.src,
        // Use 'Infinity' so it always compares as larger to any other size.
        // Also note that parseInt("500x500") === 500, which is why parseInt is
        // used here.
        size: ["", "any"].includes(size) ? Infinity : parseInt(size),
      }));
    }) ?? [];

  collectedIcons.sort((a, b) => a.size - b.size);
  if (!collectedIcons.length) {
    return null;
  }

  let index = collectedIcons.findIndex(icon => icon.size >= 256);
  if (index >= 0) {
    return collectedIcons[index].src;
  }

  // Get the largest we can if that didn't work.
  return collectedIcons[collectedIcons.length - 1].src;
}
