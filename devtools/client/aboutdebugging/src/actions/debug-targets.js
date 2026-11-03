/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs",
  // AddonManager is a singleton, never create two instances of it.
  { global: "shared" }
);
const {
  remoteClientManager,
} = require("resource://devtools/client/shared/remote-debugging/remote-client-manager.js");

const {
  l10n,
} = require("resource://devtools/client/aboutdebugging/src/modules/l10n.js");

const {
  isSupportedDebugTargetPane,
} = require("resource://devtools/client/aboutdebugging/src/modules/debug-target-support.js");

const {
  openTemporaryExtension,
} = require("resource://devtools/client/aboutdebugging/src/modules/extensions-helper.js");

const {
  getCurrentClient,
  getCurrentRuntime,
} = require("resource://devtools/client/aboutdebugging/src/modules/runtimes-state-helper.js");

const {
  gDevTools,
} = require("resource://devtools/client/framework/devtools.js");

const {
  DEBUG_TARGETS,
  DEBUG_TARGET_PANE,
  REQUEST_EXTENSIONS_FAILURE,
  REQUEST_EXTENSIONS_START,
  REQUEST_EXTENSIONS_SUCCESS,
  REQUEST_PROCESSES_FAILURE,
  REQUEST_PROCESSES_START,
  REQUEST_PROCESSES_SUCCESS,
  REQUEST_TABS_FAILURE,
  REQUEST_TABS_START,
  REQUEST_TABS_SUCCESS,
  REQUEST_WORKERS_FAILURE,
  REQUEST_WORKERS_START,
  REQUEST_WORKERS_SUCCESS,
  TEMPORARY_EXTENSION_INSTALL_FAILURE,
  TEMPORARY_EXTENSION_INSTALL_START,
  TEMPORARY_EXTENSION_INSTALL_SUCCESS,
  TEMPORARY_EXTENSION_RELOAD_FAILURE,
  TEMPORARY_EXTENSION_RELOAD_START,
  TEMPORARY_EXTENSION_RELOAD_SUCCESS,
  TERMINATE_EXTENSION_BGSCRIPT_FAILURE,
  TERMINATE_EXTENSION_BGSCRIPT_SUCCESS,
  TERMINATE_EXTENSION_BGSCRIPT_START,
  RUNTIMES,
} = require("resource://devtools/client/aboutdebugging/src/constants.js");

const Actions = require("resource://devtools/client/aboutdebugging/src/actions/index.js");

function getTabForUrl(url) {
  for (const navigator of Services.wm.getEnumerator("navigator:browser")) {
    for (const browser of navigator.gBrowser.browsers) {
      if (
        browser.contentWindow &&
        browser.contentWindow.location.href === url
      ) {
        return navigator.gBrowser.getTabForBrowser(browser);
      }
    }
  }

  return null;
}

function inspectDebugTarget(type, id) {
  return async ({ dispatch, getState }) => {
    const runtime = getCurrentRuntime(getState().runtimes);

    if (
      type == DEBUG_TARGETS.EXTENSION &&
      runtime.id === RUNTIMES.THIS_FIREFOX
    ) {
      // Bug 1780912: To avoid UX issues when debugging local web extensions,
      // we are opening the toolbox in an independant window.
      // Whereas all others are opened in new tabs.
      gDevTools.showToolboxForWebExtension(id);
    } else {
      const urlParams = {
        type,
      };
      // Main process may not provide any ID.
      if (id) {
        urlParams.id = id;
      }

      if (runtime.id !== RUNTIMES.THIS_FIREFOX) {
        urlParams.remoteId = remoteClientManager.getRemoteId(
          runtime.id,
          runtime.type
        );
      }

      const url = `about:devtools-toolbox?${new window.URLSearchParams(
        urlParams
      )}`;

      const existingTab = getTabForUrl(url);
      if (existingTab) {
        const navigator = existingTab.documentGlobal;
        navigator.gBrowser.selectedTab = existingTab;
        navigator.focus();
      } else {
        window.open(url);
      }
    }

    dispatch(
      Actions.recordTelemetryEvent("inspect", {
        target_type: type.toUpperCase(),
        runtime_type: runtime.type,
      })
    );
  };
}

function installTemporaryExtension() {
  const message = l10n.getString(
    "about-debugging-tmp-extension-install-message"
  );
  return async ({ dispatch }) => {
    dispatch({ type: TEMPORARY_EXTENSION_INSTALL_START });
    const file = await openTemporaryExtension(window, message);
    try {
      await AddonManager.installTemporaryAddon(file);
      dispatch({ type: TEMPORARY_EXTENSION_INSTALL_SUCCESS });
    } catch (e) {
      dispatch({ type: TEMPORARY_EXTENSION_INSTALL_FAILURE, error: e });
    }
  };
}

function pushServiceWorker(id, registrationFront) {
  return async () => {
    try {
      // The push button is only available if canDebugServiceWorkers is true.
      // With this configuration, `push` should always be called on the
      // registration front, and not on the (service) WorkerTargetActor.
      await registrationFront.push();
    } catch (e) {
      console.error(e);
    }
  };
}

function reloadTemporaryExtension(id) {
  return async ({ dispatch, getState }) => {
    dispatch({ type: TEMPORARY_EXTENSION_RELOAD_START, id });
    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const addonTargetFront = await clientWrapper.getAddon({ id });
      await addonTargetFront.reload();
      dispatch({ type: TEMPORARY_EXTENSION_RELOAD_SUCCESS, id });
    } catch (e) {
      const error = typeof e === "string" ? new Error(e) : e;
      dispatch({ type: TEMPORARY_EXTENSION_RELOAD_FAILURE, id, error });
    }
  };
}

function removeTemporaryExtension(id) {
  return async ({ getState }) => {
    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      await clientWrapper.uninstallAddon({ id });
    } catch (e) {
      console.error(e);
    }
  };
}

function terminateExtensionBackgroundScript(id) {
  return async ({ dispatch, getState }) => {
    dispatch({ type: TERMINATE_EXTENSION_BGSCRIPT_START, id });
    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const addonTargetFront = await clientWrapper.getAddon({ id });
      await addonTargetFront.terminateBackgroundScript();
      dispatch({ type: TERMINATE_EXTENSION_BGSCRIPT_SUCCESS, id });
    } catch (e) {
      const error = typeof e === "string" ? new Error(e) : e;
      dispatch({ type: TERMINATE_EXTENSION_BGSCRIPT_FAILURE, id, error });
    }
  };
}

function requestTabs() {
  return async ({ dispatch, getState }) => {
    dispatch({ type: REQUEST_TABS_START });

    const runtime = getCurrentRuntime(getState().runtimes);
    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const isSupported = isSupportedDebugTargetPane(
        runtime.runtimeDetails.info.type,
        DEBUG_TARGET_PANE.TAB
      );
      const tabs = isSupported ? await clientWrapper.listTabs() : [];

      // Fetch the favicon for all tabs.
      await Promise.all(
        tabs.map(descriptorFront => descriptorFront.retrieveFavicon())
      );

      dispatch({ type: REQUEST_TABS_SUCCESS, tabs });
    } catch (e) {
      dispatch({ type: REQUEST_TABS_FAILURE, error: e });
    }
  };
}

function requestExtensions() {
  return async ({ dispatch, getState }) => {
    dispatch({ type: REQUEST_EXTENSIONS_START });

    const runtime = getCurrentRuntime(getState().runtimes);
    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const isIconDataURLRequired = runtime.type !== RUNTIMES.THIS_FIREFOX;
      const addons = await clientWrapper.listAddons({
        iconDataURL: isIconDataURLRequired,
      });

      const showHiddenAddons = getState().ui.showHiddenAddons;

      // Filter out non-debuggable addons as well as hidden ones, unless the dedicated
      // preference is set to true.
      const extensions = addons.filter(
        a => a.debuggable && (!a.hidden || showHiddenAddons)
      );

      const installedExtensions = extensions.filter(
        e => !e.temporarilyInstalled
      );
      const temporaryExtensions = extensions.filter(
        e => e.temporarilyInstalled
      );

      dispatch({
        type: REQUEST_EXTENSIONS_SUCCESS,
        installedExtensions: sortTargetsByName(installedExtensions),
        temporaryExtensions: sortTargetsByName(temporaryExtensions),
      });
    } catch (e) {
      dispatch({ type: REQUEST_EXTENSIONS_FAILURE, error: e });
    }
  };
}

function requestProcesses() {
  return async ({ dispatch, getState }) => {
    dispatch({ type: REQUEST_PROCESSES_START });

    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const mainProcessDescriptorFront = await clientWrapper.getMainProcess();
      dispatch({
        type: REQUEST_PROCESSES_SUCCESS,
        mainProcess: {
          id: 0,
          processFront: mainProcessDescriptorFront,
        },
      });
    } catch (e) {
      dispatch({ type: REQUEST_PROCESSES_FAILURE, error: e });
    }
  };
}

function requestWorkers() {
  return async ({ dispatch, getState }) => {
    dispatch({ type: REQUEST_WORKERS_START });

    const clientWrapper = getCurrentClient(getState().runtimes);

    try {
      const { otherWorkers, serviceWorkers, sharedWorkers } =
        await clientWrapper.listWorkers();

      for (const serviceWorker of serviceWorkers) {
        const { registrationFront } = serviceWorker;
        if (!registrationFront) {
          continue;
        }

        const subscription = await registrationFront.getPushSubscription();
        serviceWorker.subscription = subscription;
      }

      dispatch({
        type: REQUEST_WORKERS_SUCCESS,
        otherWorkers: sortTargetsByName(otherWorkers),
        serviceWorkers: sortTargetsByName(serviceWorkers),
        sharedWorkers: sortTargetsByName(sharedWorkers),
      });
    } catch (e) {
      dispatch({ type: REQUEST_WORKERS_FAILURE, error: e });
    }
  };
}

function startServiceWorker(registrationFront) {
  return async () => {
    try {
      await registrationFront.start();
    } catch (e) {
      console.error(e);
    }
  };
}

function sortTargetsByName(targets) {
  return targets.sort((target1, target2) => {
    // Fallback to empty string in case some targets don't have a valid name.
    const name1 = target1.name || "";
    const name2 = target2.name || "";
    return name1.localeCompare(name2);
  });
}

function unregisterServiceWorker(registrationFront) {
  return async () => {
    try {
      await registrationFront.unregister();
    } catch (e) {
      console.error(e);
    }
  };
}

module.exports = {
  inspectDebugTarget,
  installTemporaryExtension,
  pushServiceWorker,
  reloadTemporaryExtension,
  removeTemporaryExtension,
  requestTabs,
  requestExtensions,
  requestProcesses,
  requestWorkers,
  startServiceWorker,
  terminateExtensionBackgroundScript,
  unregisterServiceWorker,
};
