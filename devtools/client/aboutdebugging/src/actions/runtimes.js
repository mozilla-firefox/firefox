/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Actions = require("resource://devtools/client/aboutdebugging/src/actions/index.js");

const {
  getAllRuntimes,
  getCurrentRuntime,
  findRuntimeById,
} = require("resource://devtools/client/aboutdebugging/src/modules/runtimes-state-helper.js");

const {
  l10n,
} = require("resource://devtools/client/aboutdebugging/src/modules/l10n.js");
const {
  setDefaultPreferencesIfNeeded,
  DEFAULT_PREFERENCES,
} = require("resource://devtools/client/aboutdebugging/src/modules/runtime-default-preferences.js");
const {
  createClientForRuntime,
} = require("resource://devtools/client/aboutdebugging/src/modules/runtime-client-factory.js");
const {
  isSupportedDebugTargetPane,
} = require("resource://devtools/client/aboutdebugging/src/modules/debug-target-support.js");

const {
  remoteClientManager,
} = require("resource://devtools/client/shared/remote-debugging/remote-client-manager.js");

const {
  CONNECT_RUNTIME_CANCEL,
  CONNECT_RUNTIME_FAILURE,
  CONNECT_RUNTIME_NOT_RESPONDING,
  CONNECT_RUNTIME_START,
  CONNECT_RUNTIME_SUCCESS,
  DEBUG_TARGET_PANE,
  DISCONNECT_RUNTIME_FAILURE,
  DISCONNECT_RUNTIME_START,
  DISCONNECT_RUNTIME_SUCCESS,
  PAGE_TYPES,
  REMOTE_RUNTIMES_UPDATED,
  RUNTIME_PREFERENCE,
  RUNTIMES,
  THIS_FIREFOX_RUNTIME_CREATED,
  UNWATCH_RUNTIME_FAILURE,
  UNWATCH_RUNTIME_START,
  UNWATCH_RUNTIME_SUCCESS,
  UPDATE_CONNECTION_PROMPT_SETTING_FAILURE,
  UPDATE_CONNECTION_PROMPT_SETTING_START,
  UPDATE_CONNECTION_PROMPT_SETTING_SUCCESS,
  WATCH_RUNTIME_FAILURE,
  WATCH_RUNTIME_START,
  WATCH_RUNTIME_SUCCESS,
} = require("resource://devtools/client/aboutdebugging/src/constants.js");

const CONNECTION_TIMING_OUT_DELAY = 3000;
const CONNECTION_CANCEL_DELAY = 13000;

async function getRuntimeIcon(runtime, channel) {
  if (runtime.isFenix) {
    switch (channel) {
      case "release":
      case "beta":
        return "chrome://devtools/skin/images/aboutdebugging-fenix.svg";
      case "aurora":
      default:
        return "chrome://devtools/skin/images/aboutdebugging-fenix-nightly.svg";
    }
  }

  return channel === "release" || channel === "beta" || channel === "aurora"
    ? `chrome://devtools/skin/images/aboutdebugging-firefox-${channel}.svg`
    : "chrome://devtools/skin/images/aboutdebugging-firefox-nightly.svg";
}

function onRemoteDevToolsClientClosed() {
  window.AboutDebugging.onNetworkLocationsUpdated();
  window.AboutDebugging.onUSBRuntimesUpdated();
}

function connectRuntime(id) {
  // Create a random connection id to track the connection attempt in telemetry.
  const connectionId = (Math.random() * 100000) | 0;

  return async ({ dispatch, getState }) => {
    dispatch({ type: CONNECT_RUNTIME_START, connectionId, id });

    // The preferences test-connection-timing-out-delay and test-connection-cancel-delay
    // don't have a default value but will be overridden during our tests.
    const connectionTimingOutDelay = Services.prefs.getIntPref(
      "devtools.aboutdebugging.test-connection-timing-out-delay",
      CONNECTION_TIMING_OUT_DELAY
    );
    const connectionCancelDelay = Services.prefs.getIntPref(
      "devtools.aboutdebugging.test-connection-cancel-delay",
      CONNECTION_CANCEL_DELAY
    );

    const connectionNotRespondingTimer = setTimeout(() => {
      // If connecting to the runtime takes time over CONNECTION_TIMING_OUT_DELAY,
      // we assume the connection prompt is showing on the runtime, show a dialog
      // to let user know that.
      dispatch({ type: CONNECT_RUNTIME_NOT_RESPONDING, connectionId, id });
    }, connectionTimingOutDelay);
    const connectionCancelTimer = setTimeout(() => {
      // Connect button of the runtime will be disabled during connection, but the status
      // continues till the connection was either succeed or failed. This may have a
      // possibility that the disabling continues unless page reloading, user will not be
      // able to click again. To avoid this, revert the connect button status after
      // CONNECTION_CANCEL_DELAY ms.
      dispatch({ type: CONNECT_RUNTIME_CANCEL, connectionId, id });
    }, connectionCancelDelay);

    try {
      const runtime = findRuntimeById(id, getState().runtimes);
      const clientWrapper = await createClientForRuntime(runtime);

      await setDefaultPreferencesIfNeeded(clientWrapper, DEFAULT_PREFERENCES);

      const deviceDescription = await clientWrapper.getDeviceDescription();
      const compatibilityReport =
        await clientWrapper.checkVersionCompatibility();
      const icon = await getRuntimeIcon(runtime, deviceDescription.channel);

      const {
        CONNECTION_PROMPT,
        PERMANENT_PRIVATE_BROWSING,
        SERVICE_WORKERS_ENABLED,
      } = RUNTIME_PREFERENCE;
      const connectionPromptEnabled = await clientWrapper.getPreference(
        CONNECTION_PROMPT,
        false
      );
      const privateBrowsing = await clientWrapper.getPreference(
        PERMANENT_PRIVATE_BROWSING,
        false
      );
      const serviceWorkersEnabled = await clientWrapper.getPreference(
        SERVICE_WORKERS_ENABLED,
        true
      );
      const serviceWorkersAvailable = serviceWorkersEnabled && !privateBrowsing;

      // Fenix specific workarounds are needed until we can get proper server side APIs
      // to detect Fenix and get the proper application names and versions.
      // See https://github.com/mozilla-mobile/fenix/issues/2016.

      // For Fenix runtimes, the ADB runtime name is more accurate than the one returned
      // by the Device actor.
      const runtimeName = runtime.isFenix
        ? runtime.name
        : deviceDescription.name;

      // For Fenix runtimes, the version we should display is the application version
      // retrieved from ADB, and not the Gecko version returned by the Device actor.
      const version = runtime.isFenix
        ? runtime.extra.adbPackageVersion
        : deviceDescription.version;

      const runtimeDetails = {
        canDebugServiceWorkers: deviceDescription.canDebugServiceWorkers,
        clientWrapper,
        compatibilityReport,
        connectionPromptEnabled,
        info: {
          deviceName: deviceDescription.deviceName,
          icon,
          isFenix: runtime.isFenix,
          name: runtimeName,
          os: deviceDescription.os,
          type: runtime.type,
          version,
        },
        serviceWorkersAvailable,
      };

      if (runtime.type !== RUNTIMES.THIS_FIREFOX) {
        // `closed` event will be emitted when disabling remote debugging
        // on the connected remote runtime.
        clientWrapper.once("closed", onRemoteDevToolsClientClosed);
      }

      dispatch({
        type: CONNECT_RUNTIME_SUCCESS,
        connectionId,
        runtime: {
          id,
          runtimeDetails,
          type: runtime.type,
        },
      });
    } catch (e) {
      dispatch({ type: CONNECT_RUNTIME_FAILURE, connectionId, id, error: e });
    } finally {
      clearTimeout(connectionNotRespondingTimer);
      clearTimeout(connectionCancelTimer);
    }
  };
}

function createThisFirefoxRuntime() {
  return ({ dispatch }) => {
    const thisFirefoxRuntime = {
      id: RUNTIMES.THIS_FIREFOX,
      isConnecting: false,
      isConnectionFailed: false,
      isConnectionNotResponding: false,
      isConnectionTimeout: false,
      isUnavailable: false,
      isUnplugged: false,
      name: l10n.getString("about-debugging-this-firefox-runtime-name"),
      type: RUNTIMES.THIS_FIREFOX,
    };
    dispatch({
      type: THIS_FIREFOX_RUNTIME_CREATED,
      runtime: thisFirefoxRuntime,
    });
  };
}

function disconnectRuntime(id, shouldRedirect = false) {
  return async ({ dispatch, getState }) => {
    dispatch({ type: DISCONNECT_RUNTIME_START });
    try {
      const runtime = findRuntimeById(id, getState().runtimes);
      const { clientWrapper } = runtime.runtimeDetails;

      if (runtime.type !== RUNTIMES.THIS_FIREFOX) {
        clientWrapper.off("closed", onRemoteDevToolsClientClosed);
      }
      await clientWrapper.close();
      if (shouldRedirect) {
        await dispatch(
          Actions.selectPage(PAGE_TYPES.RUNTIME, RUNTIMES.THIS_FIREFOX)
        );
      }

      dispatch({
        type: DISCONNECT_RUNTIME_SUCCESS,
        runtime: {
          id,
          type: runtime.type,
        },
      });
    } catch (e) {
      dispatch({ type: DISCONNECT_RUNTIME_FAILURE, error: e });
    }
  };
}

function updateConnectionPromptSetting(connectionPromptEnabled) {
  return async ({ dispatch, getState }) => {
    dispatch({ type: UPDATE_CONNECTION_PROMPT_SETTING_START });
    try {
      const runtime = getCurrentRuntime(getState().runtimes);
      const { clientWrapper } = runtime.runtimeDetails;
      const promptPrefName = RUNTIME_PREFERENCE.CONNECTION_PROMPT;
      await clientWrapper.setPreference(
        promptPrefName,
        connectionPromptEnabled
      );
      // Re-get actual value from the runtime.
      connectionPromptEnabled = await clientWrapper.getPreference(
        promptPrefName,
        connectionPromptEnabled
      );

      dispatch({
        type: UPDATE_CONNECTION_PROMPT_SETTING_SUCCESS,
        connectionPromptEnabled,
        runtime,
      });
    } catch (e) {
      dispatch({ type: UPDATE_CONNECTION_PROMPT_SETTING_FAILURE, error: e });
    }
  };
}

function watchRuntime(id) {
  return async ({ dispatch, getState }) => {
    dispatch({ type: WATCH_RUNTIME_START });

    try {
      if (id === RUNTIMES.THIS_FIREFOX) {
        // THIS_FIREFOX connects and disconnects on the fly when opening the page.
        await dispatch(connectRuntime(RUNTIMES.THIS_FIREFOX));
      }

      // The selected runtime should already have a connected client assigned.
      const runtime = findRuntimeById(id, getState().runtimes);
      await dispatch({ type: WATCH_RUNTIME_SUCCESS, runtime });

      dispatch(Actions.requestExtensions());
      // we have to wait for tabs, otherwise the requests to getTarget may interfer
      // with listProcesses
      await dispatch(Actions.requestTabs());
      dispatch(Actions.requestWorkers());

      if (
        isSupportedDebugTargetPane(
          runtime.runtimeDetails.info.type,
          DEBUG_TARGET_PANE.PROCESSES
        )
      ) {
        dispatch(Actions.requestProcesses());
      }
    } catch (e) {
      dispatch({ type: WATCH_RUNTIME_FAILURE, error: e });
    }
  };
}

function unwatchRuntime(id) {
  return async ({ dispatch, getState }) => {
    const runtime = findRuntimeById(id, getState().runtimes);

    dispatch({ type: UNWATCH_RUNTIME_START, runtime });

    try {
      if (id === RUNTIMES.THIS_FIREFOX) {
        // THIS_FIREFOX connects and disconnects on the fly when opening the page.
        await dispatch(disconnectRuntime(RUNTIMES.THIS_FIREFOX));
      }

      dispatch({ type: UNWATCH_RUNTIME_SUCCESS });
    } catch (e) {
      dispatch({ type: UNWATCH_RUNTIME_FAILURE, error: e });
    }
  };
}

function updateNetworkRuntimes(locations) {
  const runtimes = locations.map(location => {
    const [host, port] = location.split(":");
    return {
      id: location,
      extra: {
        connectionParameters: { host, port: parseInt(port, 10) },
      },
      isConnecting: false,
      isConnectionFailed: false,
      isConnectionNotResponding: false,
      isConnectionTimeout: false,
      isFenix: false,
      isUnavailable: false,
      isUnplugged: false,
      isUnknown: false,
      name: location,
      type: RUNTIMES.NETWORK,
    };
  });
  return updateRemoteRuntimes(runtimes, RUNTIMES.NETWORK);
}

function updateUSBRuntimes(adbRuntimes) {
  const runtimes = adbRuntimes.map(adbRuntime => {
    // Set connectionParameters only for known runtimes.
    const socketPath = adbRuntime.socketPath;
    const deviceId = adbRuntime.deviceId;
    const connectionParameters = socketPath ? { deviceId, socketPath } : null;
    return {
      id: adbRuntime.id,
      extra: {
        connectionParameters,
        deviceName: adbRuntime.deviceName,
        adbPackageVersion: adbRuntime.versionName,
      },
      isConnecting: false,
      isConnectionFailed: false,
      isConnectionNotResponding: false,
      isConnectionTimeout: false,
      isFenix: adbRuntime.isFenix,
      isUnavailable: adbRuntime.isUnavailable,
      isUnplugged: adbRuntime.isUnplugged,
      name: adbRuntime.shortName,
      type: RUNTIMES.USB,
    };
  });
  return updateRemoteRuntimes(runtimes, RUNTIMES.USB);
}

/**
 * Check that a given runtime can still be found in the provided array of runtimes, and
 * that the connection of the associated DevToolsClient is still valid.
 * Note that this check is only valid for runtimes which match the type of the runtimes
 * in the array.
 */
function _isRuntimeValid(runtime, runtimes) {
  const isRuntimeAvailable = runtimes.some(r => r.id === runtime.id);
  const isConnectionValid =
    runtime.runtimeDetails && !runtime.runtimeDetails.clientWrapper.isClosed();
  return isRuntimeAvailable && isConnectionValid;
}

function updateRemoteRuntimes(runtimes, type) {
  return async ({ dispatch, getState }) => {
    const currentRuntime = getCurrentRuntime(getState().runtimes);

    // Check if the updated remote runtimes should trigger a navigation out of the current
    // runtime page.
    if (
      currentRuntime &&
      currentRuntime.type === type &&
      !_isRuntimeValid(currentRuntime, runtimes)
    ) {
      // Since current remote runtime is invalid, move to this firefox page.
      // This case is considered as followings and so on:
      // * Remove ADB addon
      // * (Physically) Disconnect USB runtime
      //
      // The reason we call selectPage before REMOTE_RUNTIMES_UPDATED is fired is below.
      // Current runtime can not be retrieved after REMOTE_RUNTIMES_UPDATED action, since
      // that updates runtime state. So, before that we fire selectPage action to execute
      // `unwatchRuntime` correctly.
      await dispatch(
        Actions.selectPage(PAGE_TYPES.RUNTIME, RUNTIMES.THIS_FIREFOX)
      );
    }

    // For existing runtimes, transfer all properties that are not available in the
    // runtime objects passed to this method:
    // - runtimeDetails (set by about:debugging after a successful connection)
    // - isConnecting (set by about:debugging during the connection)
    // - isConnectionFailed (set by about:debugging if connection was failed)
    // - isConnectionNotResponding
    //     (set by about:debugging if connection is taking too much time)
    // - isConnectionTimeout (set by about:debugging if connection was timeout)
    runtimes.forEach(runtime => {
      const existingRuntime = findRuntimeById(runtime.id, getState().runtimes);
      const isConnectionValid =
        existingRuntime?.runtimeDetails &&
        !existingRuntime.runtimeDetails.clientWrapper.isClosed();
      runtime.runtimeDetails = isConnectionValid
        ? existingRuntime.runtimeDetails
        : null;
      runtime.isConnecting = existingRuntime
        ? existingRuntime.isConnecting
        : false;
      runtime.isConnectionFailed = existingRuntime
        ? existingRuntime.isConnectionFailed
        : false;
      runtime.isConnectionNotResponding = existingRuntime
        ? existingRuntime.isConnectionNotResponding
        : false;
      runtime.isConnectionTimeout = existingRuntime
        ? existingRuntime.isConnectionTimeout
        : false;
    });

    const existingRuntimes = getAllRuntimes(getState().runtimes);
    for (const runtime of existingRuntimes) {
      // Runtime was connected before.
      const isConnected = runtime.runtimeDetails;
      // Runtime is of the same type as the updated runtimes array, so we should check it.
      const isSameType = runtime.type === type;
      if (isConnected && isSameType && !_isRuntimeValid(runtime, runtimes)) {
        // Disconnect runtimes that were no longer valid.
        await dispatch(disconnectRuntime(runtime.id));
      }
    }

    dispatch({ type: REMOTE_RUNTIMES_UPDATED, runtimes, runtimeType: type });

    for (const runtime of getAllRuntimes(getState().runtimes)) {
      if (runtime.type !== type) {
        continue;
      }

      // Reconnect clients already available in the RemoteClientManager.
      const isConnected = !!runtime.runtimeDetails;
      const hasConnectedClient = remoteClientManager.hasClient(
        runtime.id,
        runtime.type
      );
      if (!isConnected && hasConnectedClient) {
        await dispatch(connectRuntime(runtime.id));
      }
    }
  };
}

/**
 * Remove all the listeners added on client objects. Since those objects are persisted
 * regardless of the about:debugging lifecycle, all the added events should be removed
 * before leaving about:debugging.
 */
function removeRuntimeListeners() {
  return ({ getState }) => {
    const allRuntimes = getAllRuntimes(getState().runtimes);
    const remoteRuntimes = allRuntimes.filter(
      r => r.type !== RUNTIMES.THIS_FIREFOX
    );
    for (const runtime of remoteRuntimes) {
      if (runtime.runtimeDetails) {
        const { clientWrapper } = runtime.runtimeDetails;
        clientWrapper.off("closed", onRemoteDevToolsClientClosed);
      }
    }
  };
}

module.exports = {
  connectRuntime,
  createThisFirefoxRuntime,
  disconnectRuntime,
  removeRuntimeListeners,
  unwatchRuntime,
  updateConnectionPromptSetting,
  updateNetworkRuntimes,
  updateUSBRuntimes,
  watchRuntime,
};
