/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Wrapper around the ADB utility.

"use strict";

const {
  logger,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-logger.js");
const client = require("resource://devtools/client/shared/remote-debugging/adb/adb-client.js");

const OKAY = 0x59414b4f;

const shell = async function (deviceId, command) {
  if (!deviceId) {
    throw new Error("ADB shell command needs the device id");
  }

  let state;
  let stdout = "";

  logger.debug("shell " + command + " on " + deviceId);

  return new Promise((resolve, reject) => {
    const shutdown = function () {
      logger.debug("shell shutdown");
      socket.close();
      reject("BAD_RESPONSE");
    };

    const runFSM = function runFSM(data) {
      logger.debug("runFSM " + state);
      let req;
      let ignoreResponseCode = false;
      switch (state) {
        case "start":
          state = "send-transport";
          runFSM();
          break;
        case "send-transport":
          req = client.createRequest("host:transport:" + deviceId);
          socket.send(req);
          state = "wait-transport";
          break;
        case "wait-transport":
          if (!client.checkResponse(data, OKAY)) {
            shutdown();
            return;
          }
          state = "send-shell";
          runFSM();
          break;
        case "send-shell":
          req = client.createRequest("shell:" + command);
          socket.send(req);
          state = "rec-shell";
          break;
        case "rec-shell":
          if (!client.checkResponse(data, OKAY)) {
            shutdown();
            return;
          }
          state = "decode-shell";
          if (client.getBuffer(data).byteLength == 4) {
            break;
          }
          ignoreResponseCode = true;
        // eslint-disable-next-lined no-fallthrough
        case "decode-shell": {
          const decoder = new TextDecoder();
          const text = new Uint8Array(
            client.getBuffer(data),
            ignoreResponseCode ? 4 : 0
          );
          stdout += decoder.decode(text);
          break;
        }
        default:
          logger.debug("shell Unexpected State: " + state);
          reject("UNEXPECTED_STATE");
      }
    };

    const socket = client.connect();
    socket.s.onerror = function () {
      logger.debug("shell onerror");
      reject("SOCKET_ERROR");
    };

    socket.s.onopen = function () {
      logger.debug("shell onopen");
      state = "start";
      runFSM();
    };

    socket.s.onclose = function () {
      resolve(stdout);
      logger.debug("shell onclose");
    };

    socket.s.ondata = function (event) {
      logger.debug("shell ondata");
      runFSM(event.data);
    };
  });
};

exports.shell = shell;
