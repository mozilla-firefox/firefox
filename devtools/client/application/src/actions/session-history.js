/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

("use strict");

const {
  SET_AVAILABLE_SESSION_HISTORY,
  DISABLE_SESSION_HISTORY,
  UPDATE_SESSION_HISTORY,
  UPDATE_SESSION_HISTORY_ENTRY,
} = require("resource://devtools/client/application/src/constants.js");

function setAvailableSessionHistory(sessionHistory) {
  return {
    type: SET_AVAILABLE_SESSION_HISTORY,
    sessionHistory,
  };
}

function updateSessionHistory(sessionHistory) {
  return {
    type: UPDATE_SESSION_HISTORY,
    sessionHistory,
  };
}

function updateSessionHistoryEntry(sessionHistoryEntry) {
  return {
    type: UPDATE_SESSION_HISTORY_ENTRY,
    sessionHistoryEntry,
  };
}

function disableSessionHistory() {
  return {
    type: DISABLE_SESSION_HISTORY,
  };
}

module.exports = {
  setAvailableSessionHistory,
  updateSessionHistory,
  updateSessionHistoryEntry,
  disableSessionHistory,
};
