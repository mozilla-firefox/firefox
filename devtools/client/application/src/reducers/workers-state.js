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

function WorkersState() {
  return {
    // Array of all service worker registrations
    list: [],
    canDebugWorkers: false,
  };
}

function buildWorkerDataFromFronts({ registration, workers }) {
  return {
    id: registration.id,
    lastUpdateTime: registration.lastUpdateTime,
    registrationFront: registration,
    scope: registration.scope,
    workers: workers.map(worker => ({
      id: worker.id,
      url: worker.url,
      state: worker.state,
      stateText: worker.stateText,
      registrationFront: registration,
      workerDescriptorFront: worker.workerDescriptorFront,
    })),
  };
}

function workersReducer(state = WorkersState(), action) {
  switch (action.type) {
    case UPDATE_CAN_DEBUG_WORKERS: {
      return Object.assign({}, state, {
        canDebugWorkers: action.canDebugWorkers,
      });
    }
    case UPDATE_WORKERS: {
      const { workers } = action;
      return Object.assign({}, state, {
        list: workers.map(buildWorkerDataFromFronts).flat(),
      });
    }
    // these actions don't change the state, but get picked up by the
    // telemetry middleware
    case START_WORKER:
    case UNREGISTER_WORKER:
      return state;
    default:
      return state;
  }
}

module.exports = {
  WorkersState,
  workersReducer,
};
