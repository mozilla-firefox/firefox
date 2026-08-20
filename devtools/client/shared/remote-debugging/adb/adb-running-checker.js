/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * Uses host:version service to detect if ADB is running
 * Modified from adb-file-transfer from original ADB
 */

"use strict";

const client = require("resource://devtools/client/shared/remote-debugging/adb/adb-client.js");
const {
  logger,
} = require("resource://devtools/client/shared/remote-debugging/adb/adb-logger.js");

exports.check = async function check() {
  let socket;
  let state;
  let timerID;
  const TIMEOUT_TIME = 1000;

  logger.debug("Asking for host:version");

  return new Promise(resolve => {
    // On MacOSX connecting to a port which is not started listening gets
    // stuck (bug 1481963), to avoid the stuck, we do forcibly fail the
    // connection after |TIMEOUT_TIME| elapsed.
    timerID = setTimeout(() => {
      socket.close();
      resolve(false);
    }, TIMEOUT_TIME);

    function finish(returnValue) {
      clearTimeout(timerID);
      resolve(returnValue);
    }

    const runFSM = function runFSM(packetData) {
      logger.debug("runFSM " + state);
      switch (state) {
        case "start": {
          const req = client.createRequest("host:version");
          socket.send(req);
          state = "wait-version";
          break;
        }
        case "wait-version": {
          // TODO: Actually check the version number to make sure the daemon
          //       supports the commands we want to use
          const { length, data } = client.unpackPacket(packetData);
          logger.debug("length: ", length, "data: ", data);
          socket.close();
          const version = parseInt(data, 16);
          if (version >= 31) {
            finish(true);
          } else {
            logger.debug("killing existing adb as we need version >= 31");
            finish(false);
          }
          break;
        }
        default:
          logger.debug("Unexpected State: " + state);
          finish(false);
      }
    };

    const setupSocket = function () {
      socket.s.onerror = function () {
        logger.debug("running checker onerror");
        finish(false);
      };

      socket.s.onopen = function () {
        logger.debug("running checker onopen");
        state = "start";
        runFSM();
      };

      socket.s.onclose = function () {
        logger.debug("running checker onclose");
      };

      socket.s.ondata = function (event) {
        logger.debug("running checker ondata");
        runFSM(event.data);
      };
    };

    socket = client.connect();
    setupSocket();
  });
};
