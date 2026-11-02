/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  START_WORKER,
  UNREGISTER_WORKER,
  UPDATE_CAN_DEBUG_WORKERS,
  UPDATE_WORKERS,
} = require("resource://devtools/client/application/src/constants.js");

function startWorker(worker) {
  const { registrationFront } = worker;
  registrationFront.start();

  return {
    type: START_WORKER,
  };
}

function unregisterWorker(registration) {
  const { registrationFront } = registration;
  registrationFront.unregister();

  return {
    type: UNREGISTER_WORKER,
  };
}

function updateWorkers(workers) {
  return {
    type: UPDATE_WORKERS,
    workers,
  };
}

function updateCanDebugWorkers(canDebugWorkers) {
  return {
    type: UPDATE_CAN_DEBUG_WORKERS,
    canDebugWorkers,
  };
}

module.exports = {
  startWorker,
  unregisterWorker,
  updateCanDebugWorkers,
  updateWorkers,
};
