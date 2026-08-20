/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  logger,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-logger.js");
const {
  runCommand,
} = require("resource://devtools/client/shared/remote-debugging/adb/commands/run-command.js");

// sends adb forward deviceId, localPort and devicePort
const forwardPort = function (deviceId, localPort, devicePort) {
  logger.debug("forwardPort " + localPort + " -- " + devicePort);
  // Send "host-serial:<serial-number>:<request>",
  // with <request> set to "forward:<local>;<remote>"
  // See https://android.googlesource.com/platform/system/core/+/jb-dev/adb/SERVICES.TXT
  return runCommand(
    `host-serial:${deviceId}:forward:${localPort};${devicePort}`
  ).then(function onSuccess(data) {
    return data;
  });
};

const getFreeTCPPort = function () {
  const serv = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  serv.init(-1, true, -1);
  const port = serv.port;
  serv.close();
  return port;
};

// Prepare TCP connection for provided device id and socket path.
// The returned value is a port number of localhost for the connection.
const prepareTCPConnection = async function (deviceId, socketPath) {
  const port = getFreeTCPPort();
  const local = `tcp:${port}`;
  const remote = socketPath.startsWith("@")
    ? `localabstract:${socketPath.substring(1)}`
    : `localfilesystem:${socketPath}`;
  await forwardPort(deviceId, local, remote);
  return port;
};
exports.prepareTCPConnection = prepareTCPConnection;
