/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  ClientWrapper,
} = require("resource://devtools/client/aboutdebugging/src/modules/client-wrapper.js");
const {
  COMPATIBILITY_STATUS,
} = require("resource://devtools/client/shared/remote-debugging/version-checker.js");

const runtimeInfo = {
  // device name which is running the runtime,
  // unavailable on this-firefox runtime
  deviceName: PropTypes.string,

  // icon which represents the kind of runtime
  icon: PropTypes.string.isRequired,

  // name of runtime such as "Firefox Nightly"
  name: PropTypes.string.isRequired,

  // operating system on which the runtime runs such as "Android", "Linux"
  os: PropTypes.string.isRequired,

  // runtime type, for instance "network", "usb" ...
  type: PropTypes.string.isRequired,

  // version of runtime
  version: PropTypes.string.isRequired,
};

const compatibilityReport = {
  // build ID for the current runtime (date formatted as yyyyMMdd eg "20193101")
  localID: PropTypes.string.isRequired,

  // "platform" version for the current runtime (eg "67.0a1")
  localVersion: PropTypes.string.isRequired,

  // minimum "platform" version supported for remote debugging by the current runtime
  minVersion: PropTypes.string.isRequired,

  // build ID for the target runtime (date formatted as yyyyMMdd eg "20193101")
  runtimeID: PropTypes.string.isRequired,

  // "platform" version for the target runtime (eg "67.0a1")
  runtimeVersion: PropTypes.string.isRequired,

  // report result, either COMPATIBLE, TOO_OLD or TOO_RECENT
  status: PropTypes.oneOf(Object.values(COMPATIBILITY_STATUS)).isRequired,
};
exports.compatibilityReport = PropTypes.shape(compatibilityReport);

const runtimeDetails = {
  // True if this runtime supports debugging service workers.
  // This might be undefined when connecting to runtimes older than Fx 66
  canDebugServiceWorkers: PropTypes.bool,

  // ClientWrapper built using a DevToolsClient for the runtime
  clientWrapper: PropTypes.instanceOf(ClientWrapper).isRequired,

  // compatibility report to check if the target runtime is in range of the backward
  // compatibility policy for DevTools remote debugging.
  compatibilityReport: PropTypes.shape(compatibilityReport).isRequired,

  // reflect devtools.debugger.prompt-connection preference of this runtime
  connectionPromptEnabled: PropTypes.bool.isRequired,

  // runtime information
  info: PropTypes.shape(runtimeInfo).isRequired,

  // True if service workers should be available in the target runtime. Service workers
  // can be disabled via preferences or if the runtime runs in fully private browsing
  // mode.
  serviceWorkersAvailable: PropTypes.bool.isRequired,
};
exports.runtimeDetails = PropTypes.shape(runtimeDetails);

const networkRuntimeConnectionParameter = {
  // host name of devtools server to connect
  host: PropTypes.string.isRequired,

  // port number of devtools server to connect
  port: PropTypes.number.isRequired,
};

const usbRuntimeConnectionParameter = {
  // device id
  deviceId: PropTypes.string.isRequired,
  // socket path to connect devtools server
  socketPath: PropTypes.string.isRequired,
};

const runtimeExtra = {
  // parameter to connect to devtools server
  // unavailable on unavailable/unplugged runtimes
  connectionParameters: PropTypes.oneOfType([
    PropTypes.shape(networkRuntimeConnectionParameter),
    PropTypes.shape(usbRuntimeConnectionParameter),
  ]),

  // device name
  // unavailable on this-firefox and network-location runtimes
  deviceName: PropTypes.string,

  // version of the application coming from ADB, only available via USB. Useful for Fenix
  // runtimes, because the version can't be retrieved from Service.appInfo.
  adbPackageVersion: PropTypes.string,
};

const runtime = {
  // unique id for the runtime
  id: PropTypes.string.isRequired,

  // object containing non standard properties that depend on the runtime type,
  // unavailable on this-firefox runtime
  extra: PropTypes.shape(runtimeExtra),

  // this flag will be true when start to connect to the runtime, will be false after
  // connected or has failures.
  isConnecting: PropTypes.bool.isRequired,

  // this flag will be true when the connection failed.
  isConnectionFailed: PropTypes.bool.isRequired,

  // will be true if connecting to runtime is taking time, will be false after connecting
  // or failing.
  isConnectionNotResponding: PropTypes.bool.isRequired,

  // this flag will be true when the connection was timeout.
  isConnectionTimeout: PropTypes.bool.isRequired,

  // this flag will be true when the detected runtime is Fenix (Firefox Preview).
  // Fenix need specific logic to get their display name, version and logos.
  // Discussion ongoing in https://github.com/mozilla-mobile/fenix/issues/2016
  isFenix: PropTypes.bool.isRequired,

  // unavailable runtimes are placeholders for devices where the runtime has not been
  // started yet. For instance an ADB device connected without a compatible runtime
  // running.
  isUnavailable: PropTypes.bool.isRequired,

  // unplugged runtimes are placeholders for devices that are no longer available. For
  // instance a USB device that was unplugged from the computer.
  isUnplugged: PropTypes.bool.isRequired,

  // display name of the runtime
  name: PropTypes.string.isRequired,

  // available after the connection to the runtime is established
  // unavailable on disconnected runtimes
  runtimeDetails: PropTypes.shape(runtimeDetails),

  // runtime type, for instance "network", "usb" ...
  type: PropTypes.string.isRequired,
};

/**
 * Export type of runtime
 */
exports.runtime = PropTypes.shape(runtime);
